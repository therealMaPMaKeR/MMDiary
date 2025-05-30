#include "sqlite-database-auth.h"
#include "constants.h"
#include <QDebug>

DatabaseAuthManager::DatabaseAuthManager() : m_dbManager(DatabaseManager::instance())
{
    // Constructor - database manager reference is initialized to singleton instance
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

        // Global Settings columns
        columnTypes[Constants::UserT_Index_Displayname] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_DisplaynameColor] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_MinToTray] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_AskPWAfterMinToTray] = Constants::DataType_QString;

        // Diary Settings columns
        columnTypes[Constants::UserT_Index_Diary_TextSize] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_Diary_TStampTimer] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_Diary_TStampCounter] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_Diary_CanEditRecent] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_Diary_ShowTManLogs] = Constants::DataType_QString;

        // Tasklists Settings columns
        columnTypes[Constants::UserT_Index_TLists_TextSize] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_TLists_LogToDiary] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_TLists_TaskType] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_TLists_CMess] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_TLists_PMess] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_TLists_Notif] = Constants::DataType_QString;

        // Password Manager Settings columns
        columnTypes[Constants::UserT_Index_PWMan_DefSortingMethod] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_PWMan_ReqPassword] = Constants::DataType_QString;
        columnTypes[Constants::UserT_Index_PWMan_HidePasswords] = Constants::DataType_QString;

        // Encrypted Data Settings columns
        columnTypes[Constants::UserT_Index_DataENC_ReqPassword] = Constants::DataType_QString;
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
    default:
        qWarning() << "No auth rollback defined for version" << version;
        return false;
    }
}

// Migrate to v2, Technically the First Version.
bool DatabaseAuthManager::migrateToV2()
{
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
    userTableColumns[Constants::UserT_Index_Displayname] = "TEXT";
    userTableColumns[Constants::UserT_Index_DisplaynameColor] = "TEXT";
    userTableColumns[Constants::UserT_Index_MinToTray] = "TEXT";
    userTableColumns[Constants::UserT_Index_AskPWAfterMinToTray] = "TEXT";
    // Diary Settings
    userTableColumns[Constants::UserT_Index_Diary_TextSize] = "TEXT";
    userTableColumns[Constants::UserT_Index_Diary_TStampTimer] = "TEXT";
    userTableColumns[Constants::UserT_Index_Diary_TStampCounter] = "TEXT";
    userTableColumns[Constants::UserT_Index_Diary_CanEditRecent] = "TEXT";
    userTableColumns[Constants::UserT_Index_Diary_ShowTManLogs] = "TEXT";
    // Tasklists Settings
    userTableColumns[Constants::UserT_Index_TLists_TextSize] = "TEXT";
    userTableColumns[Constants::UserT_Index_TLists_LogToDiary] = "TEXT";
    userTableColumns[Constants::UserT_Index_TLists_TaskType] = "TEXT";
    userTableColumns[Constants::UserT_Index_TLists_CMess] = "TEXT";
    userTableColumns[Constants::UserT_Index_TLists_PMess] = "TEXT";
    userTableColumns[Constants::UserT_Index_TLists_Notif] = "TEXT";
    // Password Manager Settings
    userTableColumns[Constants::UserT_Index_PWMan_DefSortingMethod] = "TEXT";
    userTableColumns[Constants::UserT_Index_PWMan_ReqPassword] = "TEXT";
    userTableColumns[Constants::UserT_Index_PWMan_HidePasswords] = "TEXT";

    if (!m_dbManager.createTable("users", userTableColumns)) {
        qWarning() << "Failed to create users table:" << m_dbManager.lastError();
        return false;
    }

    return true;
}

bool DatabaseAuthManager::migrateToV3()
{
    if (!m_dbManager.executeQuery("ALTER TABLE users ADD COLUMN " + Constants::UserT_Index_DataENC_ReqPassword + " TEXT")) {
        qWarning() << "Failed to add DataENC_ReqPassword column to users table:" << m_dbManager.lastError();
        return false;
    }
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
    if (!m_dbManager.removeColumn("users", Constants::UserT_Index_DataENC_ReqPassword)) {
        qWarning() << "Failed to remove DataENC_ReqPassword column:" << m_dbManager.lastError();
        return false;
    }
    return true;
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

    // Prepare user data
    QMap<QString, QVariant> userData;
    userData[Constants::UserT_Index_Username] = username;
    userData[Constants::UserT_Index_Password] = hashedPassword;
    userData[Constants::UserT_Index_EncryptionKey] = encryptionKey;
    userData[Constants::UserT_Index_Salt] = salt;
    userData[Constants::UserT_Index_Iterations] = "500000"; // Default iterations
    userData[Constants::UserT_Index_Displayname] = displayName;

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
