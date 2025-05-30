#include "sqlite-database-auth.h"
#include "constants.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>

DatabaseAuthManager::DatabaseAuthManager()
{
}

DatabaseAuthManager::~DatabaseAuthManager()
{
    close();
}

DatabaseAuthManager& DatabaseAuthManager::instance()
{
    static DatabaseAuthManager instance;
    return instance;
}

bool DatabaseAuthManager::connect()
{
    // Check for migration from MMDiary.db to users.db
    if (!checkForMigrationFromMMDiary()) {
        qCritical() << "Failed to migrate from MMDiary.db";
        return false;
    }

    return m_dbManager.connect(Constants::DBPath_User);
}

bool DatabaseAuthManager::isConnected() const
{
    return m_dbManager.isConnected();
}

void DatabaseAuthManager::close()
{
    m_dbManager.close();
}

QString DatabaseAuthManager::lastError() const
{
    return m_dbManager.lastError();
}

bool DatabaseAuthManager::IndexIsValid(QString index, QString type)
{
    // Create a static map for column types (only created once)
    static QMap<QString, QString> columnTypes;

    // Initialize the map on first call
    if (columnTypes.isEmpty()) {
        // User Info columns
        columnTypes[Constants::UserT_Index_Username] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_Password] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_EncryptionKey] = Constants::DataType_QByteArray;
        columnTypes[Constants::UserT_Index_Salt] = Constants::DataType_QByteArray;
        columnTypes[Constants::UserT_Index_Iterations] = Constants::DataType_QString;
    }

    // Check if the column exists in our map
    if (!columnTypes.contains(index)) {
        qDebug() << "INDEXINVALID: Column does not exist in mapping:" << index;
        return false;
    }

    // Check if the requested type matches the actual column type
    if (columnTypes[index] != type) {
        qDebug() << "INDEXINVALID: Type mismatch for column" << index
                 << "- expected:" << columnTypes[index]
                 << "requested:" << type;
        return false;
    }

    return true;
}

QString DatabaseAuthManager::GetUserData_String(QString username, QString index)
{
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        return Constants::ErrorMessage_Default;
    }

    // Ensure connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database";
        return Constants::ErrorMessage_Default;
    }

    QStringList columns = {index};
    // Use COLLATE NOCASE for case-insensitive comparison in SQLite
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> bindValues = {{":username", username}};
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("users", columns, whereClause, bindValues);
    if (results.isEmpty()) {
        qDebug() << "User not found:" << username;
        return Constants::ErrorMessage_INVUSER;
    }
    return results.first()[index].toString();
}

QByteArray DatabaseAuthManager::GetUserData_ByteA(QString username, QString index)
{
    qDebug() << "GetUserData_ByteA called for username:" << username << "index:" << index;

    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        qDebug() << "Index is not valid for QByteArray:" << index;
        return QByteArray();
    }

    // Ensure connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database";
        return QByteArray();
    }

    QStringList columns = {index};
    // Case-insensitive comparison
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> bindValues = {{":username", username}};
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("users", columns, whereClause, bindValues);
    if (results.isEmpty()) {
        qDebug() << "User not found:" << username;
        return QByteArray();
    }

    QVariant value = results.first()[index];
    qDebug() << "Value type:" << value.typeName() << "isNull:" << value.isNull();
    QByteArray result = value.toByteArray();
    qDebug() << "Result size:" << result.size() << "bytes";
    return result;
}

bool DatabaseAuthManager::UpdateUserData_TEXT(QString username, QString index, QString data)
{
    // Validate index is for TEXT data
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        qDebug() << "Invalid index for TEXT data:" << index;
        return false;
    }

    // Ensure database connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database";
        return false;
    }

    // Create backup before modification
    if (!createBackupBeforeWrite()) {
        qWarning() << "Failed to create backup before TEXT data update";
        // Continue anyway - backup failure shouldn't prevent data update
    }

    // Check if column exists in the table using the database manager's methods
    QVector<QMap<QString, QVariant>> pragmaResults = m_dbManager.select("pragma_table_info('users')");
    bool columnExists = false;

    for (const auto& column : pragmaResults) {
        if (column["name"].toString() == index) {
            columnExists = true;
            break;
        }
    }

    // Add column if it doesn't exist
    if (!columnExists) {
        QString alterQuery = QString("ALTER TABLE users ADD COLUMN %1 TEXT").arg(index);
        if (!m_dbManager.executeQuery(alterQuery)) {
            return false; // Failed to add column
        }
    }

    // Update user data
    QMap<QString, QVariant> updateData;
    updateData[index] = data;
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> whereBindValues;
    whereBindValues[":username"] = username;

    return m_dbManager.update("users", updateData, whereClause, whereBindValues);
}

bool DatabaseAuthManager::UpdateUserData_BLOB(QString username, QString index, QByteArray data)
{
    // Validate index is for BLOB data
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        qDebug() << "Invalid index for BLOB data:" << index;
        return false;
    }

    // Ensure database connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database";
        return false;
    }

    // Create backup before modification
    if (!createBackupBeforeWrite()) {
        qWarning() << "Failed to create backup before BLOB data update";
        // Continue anyway - backup failure shouldn't prevent data update
    }

    // Check if column exists in the table using the database manager's methods
    QVector<QMap<QString, QVariant>> pragmaResults = m_dbManager.select("pragma_table_info('users')");
    bool columnExists = false;

    for (const auto& column : pragmaResults) {
        if (column["name"].toString() == index) {
            columnExists = true;
            break;
        }
    }

    // Add column if it doesn't exist
    if (!columnExists) {
        QString alterQuery = QString("ALTER TABLE users ADD COLUMN %1 BLOB").arg(index);
        if (!m_dbManager.executeQuery(alterQuery)) {
            return false; // Failed to add column
        }
    }

    // Update user data
    QMap<QString, QVariant> updateData;
    updateData[index] = data;
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> whereBindValues;
    whereBindValues[":username"] = username;

    return m_dbManager.update("users", updateData, whereClause, whereBindValues);
}

bool DatabaseAuthManager::migrateAuthDatabase()
{
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database for migration";
        return false;
    }

    // Use the generic migration system with auth-specific callbacks
    auto migrationCallback = [this](int version) -> bool {
        return authMigrationCallback(version);
    };

    auto rollbackCallback = [this](int version) -> bool {
        return authRollbackCallback(version);
    };

    return m_dbManager.migrateDatabase(LATEST_AUTH_VERSION, migrationCallback, rollbackCallback);
}

bool DatabaseAuthManager::authMigrationCallback(int version)
{
    switch (version) {
    case 2:
        return migrateToV2();
    case 3:
        return migrateToV3();
    case 4:
        return migrateToV4();
    default:
        qWarning() << "No auth migration defined for version" << version;
        return false;
    }
}

bool DatabaseAuthManager::authRollbackCallback(int version)
{
    switch (version) {
    case 2:
        return rollbackFromV2();
    case 3:
        return rollbackFromV3();
    case 4:
        return rollbackFromV4();
    default:
        qWarning() << "No auth rollback defined for version" << version;
        return false;
    }
}

// Migrate to v2, Technically the First Version.
bool DatabaseAuthManager::migrateToV2()
{
    // Create backup before migration
    if (!createBackupBeforeWrite()) {
        qWarning() << "Failed to create backup before V2 migration";
        // Continue anyway - backup failure shouldn't prevent migration
    }

    // Create a users table if it doesn't exist
    QMap<QString, QString> userTableColumns;
    userTableColumns["id"] = "INTEGER PRIMARY KEY AUTOINCREMENT";
    // User Info
    userTableColumns[Constants::UserT_Index_Username] = "TEXT NOT NULL UNIQUE";
    userTableColumns[Constants::UserT_Index_Password] = "TEXT NOT NULL";
    userTableColumns[Constants::UserT_Index_EncryptionKey] = "BLOB NOT NULL";
    userTableColumns[Constants::UserT_Index_Salt] = "BLOB NOT NULL";
    userTableColumns[Constants::UserT_Index_Iterations] = "TEXT NOT NULL";
    // Global Settings
    userTableColumns[Constants::SettingsT_Index_Displayname] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_DisplaynameColor] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_MinToTray] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_AskPWAfterMinToTray] = "TEXT";
    // Diary Settings
    userTableColumns[Constants::SettingsT_Index_Diary_TextSize] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_Diary_TStampTimer] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_Diary_TStampCounter] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_Diary_CanEditRecent] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_Diary_ShowTManLogs] = "TEXT";
    // Tasklists Settings
    userTableColumns[Constants::SettingsT_Index_TLists_TextSize] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_TLists_LogToDiary] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_TLists_TaskType] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_TLists_CMess] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_TLists_PMess] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_TLists_Notif] = "TEXT";
    // Password Manager Settings
    userTableColumns[Constants::SettingsT_Index_PWMan_DefSortingMethod] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_PWMan_ReqPassword] = "TEXT";
    userTableColumns[Constants::SettingsT_Index_PWMan_HidePasswords] = "TEXT";

    if (!m_dbManager.createTable("users", userTableColumns)) {
        qWarning() << "Failed to create users table:" << m_dbManager.lastError();
        return false;
    }

    return true;
}

bool DatabaseAuthManager::migrateToV3()
{
    // Create backup before migration
    if (!createBackupBeforeWrite()) {
        qWarning() << "Failed to create backup before V3 migration";
        // Continue anyway - backup failure shouldn't prevent migration
    }

    if (!m_dbManager.executeQuery("ALTER TABLE users ADD COLUMN " + Constants::SettingsT_Index_DataENC_ReqPassword + " TEXT")) {
        qWarning() << "Failed to add DataENC_ReqPassword column to users table:" << m_dbManager.lastError();
        return false;
    }
    return true;
}

bool DatabaseAuthManager::migrateToV4()
{
    // Create backup before migration
    if (!createBackupBeforeWrite()) {
        qWarning() << "Failed to create backup before V4 migration";
        // Continue anyway - backup failure shouldn't prevent migration
    }

    // Instead of removing columns one by one (which recreates the table 19 times),
    // we'll recreate the table once with only the core user columns

    // Define the new table structure with only core user columns (no settings)
    QMap<QString, QString> newUserTableColumns;
    newUserTableColumns["id"] = "INTEGER PRIMARY KEY AUTOINCREMENT";
    newUserTableColumns[Constants::UserT_Index_Username] = "TEXT NOT NULL UNIQUE";
    newUserTableColumns[Constants::UserT_Index_Password] = "TEXT NOT NULL";
    newUserTableColumns[Constants::UserT_Index_EncryptionKey] = "BLOB NOT NULL";
    newUserTableColumns[Constants::UserT_Index_Salt] = "BLOB NOT NULL";
    newUserTableColumns[Constants::UserT_Index_Iterations] = "TEXT NOT NULL";

    // Create temporary table with new structure
    QString tempTableName = "users_temp";
    if (!m_dbManager.createTable(tempTableName, newUserTableColumns)) {
        qWarning() << "Failed to create temporary users table:" << m_dbManager.lastError();
        return false;
    }

    // Copy only the core user data (no settings columns)
    QStringList coreColumns = {
        "id",
        Constants::UserT_Index_Username,
        Constants::UserT_Index_Password,
        Constants::UserT_Index_EncryptionKey,
        Constants::UserT_Index_Salt,
        Constants::UserT_Index_Iterations
    };

    QString copyQuery = QString("INSERT INTO %1 (%2) SELECT %2 FROM users")
                            .arg(tempTableName, coreColumns.join(", "));

    if (!m_dbManager.executeQuery(copyQuery)) {
        qWarning() << "Failed to copy user data to temporary table:" << m_dbManager.lastError();
        // Clean up temporary table
        m_dbManager.dropTable(tempTableName);
        return false;
    }

    // Drop original users table
    if (!m_dbManager.dropTable("users")) {
        qWarning() << "Failed to drop original users table:" << m_dbManager.lastError();
        // Clean up temporary table
        m_dbManager.dropTable(tempTableName);
        return false;
    }

    // Rename temporary table to users
    QString renameQuery = QString("ALTER TABLE %1 RENAME TO users").arg(tempTableName);
    if (!m_dbManager.executeQuery(renameQuery)) {
        qWarning() << "Failed to rename temporary table to users:" << m_dbManager.lastError();
        return false;
    }

    qInfo() << "Migration to V4 completed - recreated users table with only core columns";
    return true;
}


// Example rollback: Remove users table. This shouldn't ever happen, because v2 is basically first version.
bool DatabaseAuthManager::rollbackFromV2()
{
    // Drop the users table
    if (!m_dbManager.dropTable("users")) {
        qWarning() << "Failed to drop users table:" << m_dbManager.lastError();
        return false;
    }
    return true;
}

bool DatabaseAuthManager::rollbackFromV3()
{
    if (!m_dbManager.removeColumn("users", Constants::SettingsT_Index_DataENC_ReqPassword)) {
        qWarning() << "Failed to remove DataENC_ReqPassword column:" << m_dbManager.lastError();
        return false;
    }
    return true;
}

bool DatabaseAuthManager::rollbackFromV4()
{
    // Re-add all the settings columns that were removed in V4
    // This is complex because we need to recreate the exact structure from V3

    QStringList alterQueries = {
        "ALTER TABLE users ADD COLUMN displayname TEXT",
        "ALTER TABLE users ADD COLUMN displaynamecolor TEXT",
        "ALTER TABLE users ADD COLUMN MinToTray TEXT",
        "ALTER TABLE users ADD COLUMN AskPWAfterMinToTray TEXT",
        "ALTER TABLE users ADD COLUMN Diary_TextSize TEXT",
        "ALTER TABLE users ADD COLUMN Diary_TStampTimer TEXT",
        "ALTER TABLE users ADD COLUMN Diary_TStampCounter TEXT",
        "ALTER TABLE users ADD COLUMN Diary_CanEditRecent TEXT",
        "ALTER TABLE users ADD COLUMN Diary_ShowTManLogs TEXT",
        "ALTER TABLE users ADD COLUMN TLists_TextSize TEXT",
        "ALTER TABLE users ADD COLUMN TLists_LogToDiary TEXT",
        "ALTER TABLE users ADD COLUMN TLists_TaskType TEXT",
        "ALTER TABLE users ADD COLUMN TLists_CMess TEXT",
        "ALTER TABLE users ADD COLUMN TLists_PMess TEXT",
        "ALTER TABLE users ADD COLUMN TLists_Notif TEXT",
        "ALTER TABLE users ADD COLUMN PWMan_DefSortingMethod TEXT",
        "ALTER TABLE users ADD COLUMN PWMan_ReqPassword TEXT",
        "ALTER TABLE users ADD COLUMN PWMan_HidePasswords TEXT",
        "ALTER TABLE users ADD COLUMN ENCRYPTEDDATA_ReqPassword TEXT"
    };

    bool success = true;
    for (const QString& query : alterQueries) {
        if (!m_dbManager.executeQuery(query)) {
            qWarning() << "Failed to execute rollback query:" << query << "Error:" << m_dbManager.lastError();
            success = false;
        }
    }

    if (success) {
        qInfo() << "Rollback from V4 completed - restored all settings columns to users table";
    } else {
        qWarning() << "Rollback from V4 had some failures";
    }

    return success;
}

//Generic Methods

bool DatabaseAuthManager::initializeVersioning()
{
    return m_dbManager.initializeVersioning();
}

bool DatabaseAuthManager::beginTransaction()
{
    return m_dbManager.beginTransaction();
}

bool DatabaseAuthManager::commitTransaction()
{
    return m_dbManager.commitTransaction();
}

bool DatabaseAuthManager::rollbackTransaction()
{
    return m_dbManager.rollbackTransaction();
}

int DatabaseAuthManager::lastInsertId() const
{
    return m_dbManager.lastInsertId();
}

bool DatabaseAuthManager::CreateUser(const QString& username, const QString& hashedPassword,
                                     const QByteArray& encryptionKey, const QByteArray& salt,
                                     const QString& displayName)
{
    // Ensure database connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database for user creation";
        return false;
    }

    // Check if user already exists
    if (UserExists(username)) {
        qDebug() << "User already exists:" << username;
        return false;
    }

    // Create backup before modification
    if (!createBackupBeforeWrite()) {
        qWarning() << "Failed to create backup before user creation";
        // Continue anyway - backup failure shouldn't prevent user creation
    }

    // Prepare user data
    QMap<QString, QVariant> userData;
    userData[Constants::UserT_Index_Username] = username;
    userData[Constants::UserT_Index_Password] = hashedPassword;
    userData[Constants::UserT_Index_EncryptionKey] = encryptionKey;
    userData[Constants::UserT_Index_Salt] = salt;
    userData[Constants::UserT_Index_Iterations] = "500000"; // Default iterations

    return m_dbManager.insert("users", userData);
}

bool DatabaseAuthManager::UserExists(const QString& username)
{
    // Ensure database connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database for user existence check";
        return false;
    }

    QStringList columns = {Constants::UserT_Index_Username};
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> bindValues = {{":username", username}};
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("users", columns, whereClause, bindValues);

    return !results.isEmpty();
}

bool DatabaseAuthManager::DeleteUser(const QString& username)
{
    // Ensure database connection
    if (!isConnected() && !connect()) {
        qDebug() << "Failed to connect to auth database for user deletion";
        return false;
    }

    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> bindValues = {{":username", username}};

    return m_dbManager.remove("users", whereClause, bindValues);
}

//Backup + MMDiary.db migration

bool DatabaseAuthManager::checkForMigrationFromMMDiary()
{
    QString oldDbPath = "Data/MMDiary.db";
    QString newDbPath = Constants::DBPath_User;

    // Check if migration is needed
    if (QFile::exists(oldDbPath) && !QFile::exists(newDbPath)) {
        qInfo() << "Migrating from MMDiary.db to users.db";

        // Ensure Data directory exists
        QDir dataDir("Data");
        if (!dataDir.exists()) {
            if (!dataDir.mkpath(".")) {
                qCritical() << "Failed to create Data directory for migration";
                return false;
            }
        }

        // Copy MMDiary.db to users.db
        if (!QFile::copy(oldDbPath, newDbPath)) {
            qCritical() << "Failed to copy MMDiary.db to users.db during migration";
            return false;
        }

        qInfo() << "Successfully migrated MMDiary.db to users.db";
    }

    return true;
}

bool DatabaseAuthManager::createBackupBeforeWrite()
{
    QString dbPath = Constants::DBPath_User;

    // Only create backup if the database file exists
    if (!QFile::exists(dbPath)) {
        return true; // No file to backup yet
    }

    // Rotate existing backups (delete oldest, shift others)
    QString backup5 = getBackupFileName(5);
    if (QFile::exists(backup5)) {
        if (!QFile::remove(backup5)) {
            qWarning() << "Failed to remove oldest backup:" << backup5;
        }
    }

    // Shift backups: 4->5, 3->4, 2->3, 1->2
    for (int i = 4; i >= 1; i--) {
        QString currentBackup = getBackupFileName(i);
        QString nextBackup = getBackupFileName(i + 1);

        if (QFile::exists(currentBackup)) {
            // Remove target if it exists (shouldn't happen due to rotation, but safety)
            if (QFile::exists(nextBackup)) {
                QFile::remove(nextBackup);
            }

            if (!QFile::rename(currentBackup, nextBackup)) {
                qWarning() << "Failed to rotate backup from" << currentBackup << "to" << nextBackup;
            }
        }
    }

    // Move current database to backup1
    QString backup1 = getBackupFileName(1);
    if (QFile::exists(backup1)) {
        QFile::remove(backup1);
    }

    if (!QFile::copy(dbPath, backup1)) {
        qWarning() << "Failed to create backup1 from current database";
        return false;
    }

    qDebug() << "Successfully created backup before database modification";

    // Check if we should clean up old MMDiary.db
    cleanupOldDatabaseIfNeeded();

    return true;
}

bool DatabaseAuthManager::cleanupOldDatabaseIfNeeded()
{
    QString oldDbPath = "Data/MMDiary.db";

    // Only clean up if MMDiary.db exists and we have 5 backups
    if (QFile::exists(oldDbPath) && countExistingBackups() >= 5) {
        if (QFile::remove(oldDbPath)) {
            qInfo() << "Cleaned up old MMDiary.db file - 5 backups now available";
            return true;
        } else {
            qWarning() << "Failed to remove old MMDiary.db file";
            return false;
        }
    }

    return true; // Nothing to clean up or not enough backups yet
}

QString DatabaseAuthManager::getBackupFileName(int index) const
{
    return QString("Data/usersdb%1.bkup").arg(index);
}

int DatabaseAuthManager::countExistingBackups() const
{
    int count = 0;
    for (int i = 1; i <= 5; i++) {
        if (QFile::exists(getBackupFileName(i))) {
            count++;
        }
    }
    return count;
}
