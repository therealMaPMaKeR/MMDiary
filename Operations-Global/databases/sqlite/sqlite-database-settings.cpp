#include "sqlite-database-settings.h"
#include "constants.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include "settings_default_usersettings.h"

DatabaseSettingsManager::DatabaseSettingsManager()
{
}

DatabaseSettingsManager::~DatabaseSettingsManager()
{
    close();
}

DatabaseSettingsManager& DatabaseSettingsManager::instance()
{
    static DatabaseSettingsManager instance;
    return instance;
}

QString DatabaseSettingsManager::getSettingsDatabasePath(const QString& username)
{
    return QString("Data/%1/settings.db").arg(username);
}

bool DatabaseSettingsManager::connect(const QString& username, const QByteArray& encryptionKey)
{
    m_currentUsername = username;
    m_encryptionKey = encryptionKey;

    QString dbPath = getSettingsDatabasePath(username);

    // Ensure the user directory exists
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Failed to create directory for settings database:" << dir.path();
            return false;
        }
    }

    bool success = m_dbManager.connect(dbPath);
    if (!success) {
        qWarning() << "Failed to connect to settings database:" << m_dbManager.lastError();
        return false;
    }

    // Check if this is a new database (no settings table)
    bool isNewDatabase = !m_dbManager.tableExists("settings");

    if (isNewDatabase) {
        // Initialize versioning for new database
        if (!initializeVersioning()) {
            qWarning() << "Failed to initialize versioning for settings database";
            return false;
        }

        // Run migrations to create tables
        if (!migrateSettingsDatabase()) {
            qWarning() << "Failed to migrate settings database";
            return false;
        }
    } else {
        // Validate encryption key for existing database
        if (!validateEncryptionKey()) {
            qWarning() << "Encryption key validation failed for settings database";
            close();
            QMessageBox::warning(nullptr, "Settings Database Error",
                                 "Encryption key doesn't match for the settings database. The settings database appears corrupted. It has been recreated with default settings.");
            return createOrRecreateSettingsDatabase(username, encryptionKey);
        }
        // Run migrations (CRITICAL: needed to update existing databases)
        if (!migrateSettingsDatabase()) {
            qWarning() << "Failed to migrate settings database";
            return false;
        }
    }

    return true;
}

bool DatabaseSettingsManager::validateEncryptionKey()
{
    if (!isConnected()) {
        return false;
    }

    // Check if settings table exists and has data
    if (!m_dbManager.tableExists("settings")) {
        return true; // New database, validation not needed
    }

    // Try to read some encrypted data to validate the key
    DatabaseResult results = m_dbManager.select("settings", QStringList(), "", QMap<QString, QVariant>(), QStringList(), 1);
    if (results.isEmpty()) {
        return true; // No data to validate
    }

    auto firstRow = results.first();
    if (!firstRow.has_value()) {
        return true; // No data to validate
    }
    
    // Try to decrypt some data to validate key
    // We can use any text field for validation
    QString testData = firstRow.value()[Constants::SettingsT_Index_DisplaynameColor].toString();
    if (testData.isEmpty()) {
        return true; // No encrypted data to validate
    }

    // Try to decrypt to see if key works
    try {
        QString decrypted = CryptoUtils::Encryption_Decrypt(m_encryptionKey, testData);
        if (decrypted.isEmpty()) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool DatabaseSettingsManager::createOrRecreateSettingsDatabase(const QString& username, const QByteArray& encryptionKey)
{
    m_currentUsername = username;
    m_encryptionKey = encryptionKey;

    QString dbPath = getSettingsDatabasePath(username);

    // Close any existing connection
    close();

    // Remove existing database file if it exists
    if (QFile::exists(dbPath)) {
        if (!QFile::remove(dbPath)) {
            qWarning() << "Failed to remove existing settings database";
            return false;
        }
    }

    // Ensure the user directory exists
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Failed to create directory for settings database:" << dir.path();
            return false;
        }
    }

    // Connect to new database
    bool success = m_dbManager.connect(dbPath);
    if (!success) {
        qWarning() << "Failed to connect to new settings database:" << m_dbManager.lastError();
        return false;
    }

    // Initialize versioning
    if (!initializeVersioning()) {
        qWarning() << "Failed to initialize versioning for settings database";
        return false;
    }

    // Run migrations to create tables
    if (!migrateSettingsDatabase()) {
        qWarning() << "Failed to migrate settings database";
        return false;
    }

    return true;
}

bool DatabaseSettingsManager::isConnected() const
{
    return m_dbManager.isConnected();
}

void DatabaseSettingsManager::close()
{
    m_dbManager.close();
}

bool DatabaseSettingsManager::IndexIsValid(QString index, QString type)
{
    // Create a static map for column types (only created once)
    static QMap<QString, QString> columnTypes;

    // Initialize the map on first call
    if (columnTypes.isEmpty()) {
        // Global Settings columns
        columnTypes[Constants::SettingsT_Index_Displayname] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DisplaynameColor] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_MinToTray] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_AskPWAfterMinToTray] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_ReqPWDelay] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_OpenOnSettings] = Constants::DataType_QString;

        // Diary Settings columns
        columnTypes[Constants::SettingsT_Index_Diary_TextSize] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_Diary_TStampTimer] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_Diary_TStampCounter] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_Diary_CanEditRecent] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_Diary_ShowTManLogs] = Constants::DataType_QString;

        // Tasklists Settings columns
        columnTypes[Constants::SettingsT_Index_TLists_TextSize] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_TLists_LogToDiary] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_TLists_TaskType] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_TLists_CMess] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_TLists_PMess] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_TLists_Notif] = Constants::DataType_QString;

        // Password Manager Settings columns
        columnTypes[Constants::SettingsT_Index_PWMan_DefSortingMethod] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_PWMan_ReqPassword] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_PWMan_HidePasswords] = Constants::DataType_QString;

        // Encrypted Data Settings columns
        columnTypes[Constants::SettingsT_Index_DataENC_ReqPassword] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DataENC_HideThumbnails_Image] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DataENC_HideThumbnails_Video] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DataENC_Hidden_Categories] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DataENC_Hidden_Tags] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DataENC_Hide_Categories] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_DataENC_Hide_Tags] = Constants::DataType_QString;
        
        // VideoPlayer Settings columns
        columnTypes[Constants::SettingsT_Index_VP_Shows_Autoplay] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_AutoplayRand] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_UseTMDB] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_DisplayFilenames] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_CheckNewEP] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_FileFolderParsing] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_AutoDelete] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_DefaultVolume] = Constants::DataType_QString;
        columnTypes[Constants::SettingsT_Index_VP_Shows_CheckNewEPStartup] = Constants::DataType_QString;
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

QString DatabaseSettingsManager::GetSettingsData_String(QString index)
{
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        return Constants::ErrorMessage_Default;
    }

    // Ensure connection
    if (!isConnected()) {
        qDebug() << "Settings database not connected";
        return Constants::ErrorMessage_Default;
    }

    // Ensure settings record exists
    if (!ensureSettingsRecord()) {
        qDebug() << "Failed to ensure settings record exists";
        return Constants::ErrorMessage_Default;
    }

    QStringList columns = {index};
    DatabaseResult results = m_dbManager.select("settings", columns);
    if (results.isEmpty()) {
        qDebug() << "No settings data found";
        return Constants::ErrorMessage_Default;
    }

    auto firstRow = results.first();
    if (!firstRow.has_value()) {
        qDebug() << "No settings data found";
        return Constants::ErrorMessage_Default;
    }
    
    QString encryptedValue = firstRow.value()[index].toString();
    if (encryptedValue.isEmpty()) {
        return ""; // Empty value is valid
    }

    // Decrypt the value
    try {
        return CryptoUtils::Encryption_Decrypt(m_encryptionKey, encryptedValue);
    } catch (...) {
        qDebug() << "Failed to decrypt settings value for index:" << index;
        return Constants::ErrorMessage_Default;
    }
}

QByteArray DatabaseSettingsManager::GetSettingsData_ByteA(QString index)
{
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        qDebug() << "Index is not valid for QByteArray:" << index;
        return QByteArray();
    }

    // Ensure connection
    if (!isConnected()) {
        qDebug() << "Settings database not connected";
        return QByteArray();
    }

    // Ensure settings record exists
    if (!ensureSettingsRecord()) {
        qDebug() << "Failed to ensure settings record exists";
        return QByteArray();
    }

    QStringList columns = {index};
    DatabaseResult results = m_dbManager.select("settings", columns);
    if (results.isEmpty()) {
        qDebug() << "No settings data found";
        return QByteArray();
    }

    auto firstRow = results.first();
    if (!firstRow.has_value()) {
        qDebug() << "No settings data found";
        return QByteArray();
    }
    
    QByteArray encryptedValue = firstRow.value()[index].toByteArray();
    if (encryptedValue.isEmpty()) {
        return QByteArray(); // Empty value
    }

    // Decrypt the value
    try {
        return CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedValue);
    } catch (...) {
        qDebug() << "Failed to decrypt settings ByteArray for index:" << index;
        return QByteArray();
    }
}

bool DatabaseSettingsManager::UpdateSettingsData_TEXT(QString index, QString data)
{
    // Validate index is for TEXT data
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        qDebug() << "Invalid index for TEXT data:" << index;
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        qDebug() << "Settings database not connected";
        return false;
    }

    // Ensure settings record exists
    if (!ensureSettingsRecord()) {
        qDebug() << "Failed to ensure settings record exists";
        return false;
    }

    // Encrypt the data
    QString encryptedData;
    if (!data.isEmpty()) {
        try {
            encryptedData = CryptoUtils::Encryption_Encrypt(m_encryptionKey, data, m_currentUsername);
        } catch (...) {
            qDebug() << "Failed to encrypt settings data for index:" << index;
            return false;
        }
    }

    // Update settings data
    QMap<QString, QVariant> updateData;
    updateData[index] = encryptedData;

    return m_dbManager.update("settings", updateData, "", QMap<QString, QVariant>());
}

bool DatabaseSettingsManager::UpdateSettingsData_BLOB(QString index, QByteArray data)
{
    // Validate index is for BLOB data
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        qDebug() << "Invalid index for BLOB data:" << index;
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        qDebug() << "Settings database not connected";
        return false;
    }

    // Ensure settings record exists
    if (!ensureSettingsRecord()) {
        qDebug() << "Failed to ensure settings record exists";
        return false;
    }

    // Encrypt the data
    QByteArray encryptedData;
    if (!data.isEmpty()) {
        try {
            encryptedData = CryptoUtils::Encryption_EncryptBArray(m_encryptionKey, data, m_currentUsername);
        } catch (...) {
            qDebug() << "Failed to encrypt settings ByteArray for index:" << index;
            return false;
        }
    }

    // Update settings data
    QMap<QString, QVariant> updateData;
    updateData[index] = encryptedData;

    return m_dbManager.update("settings", updateData, "", QMap<QString, QVariant>());
}

bool DatabaseSettingsManager::ensureSettingsRecord()
{
    // Check if settings record exists
    DatabaseResult results = m_dbManager.select("settings");
    if (results.isEmpty()) {
        // Create a settings record
        QMap<QString, QVariant> settingsData;
        settingsData["id"] = 1; // Single settings record
        return m_dbManager.insert("settings", settingsData);
    }
    return true;
}

bool DatabaseSettingsManager::migrateSettingsDatabase()
{
    if (!isConnected()) {
        qDebug() << "Settings database not connected for migration";
        return false;
    }

    // Use the generic migration system with settings-specific callbacks
    auto migrationCallback = [this](int version) -> bool {
        return settingsMigrationCallback(version);
    };

    auto rollbackCallback = [this](int version) -> bool {
        return settingsRollbackCallback(version);
    };

    return m_dbManager.migrateDatabase(LATEST_SETTINGS_VERSION, migrationCallback, rollbackCallback);
}

bool DatabaseSettingsManager::settingsMigrationCallback(int version)
{
    switch (version) {
    case 2:
        return migrateToV2();
    case 3:
        return migrateToV3();
    case 4:
        return migrateToV4();
    default:
        qWarning() << "No settings migration defined for version" << version;
        return false;
    }
}

bool DatabaseSettingsManager::settingsRollbackCallback(int version)
{
    switch (version) {
    case 2:
        return rollbackFromV2();
    case 3:
        return rollbackFromV3();
    case 4:
        return rollbackFromV4();
    default:
        qWarning() << "No settings rollback defined for version" << version;
        return false;
    }
}

bool DatabaseSettingsManager::migrateToV2()
{
    // Create the settings table
    QMap<QString, QString> settingsTableColumns;
    settingsTableColumns["id"] = "INTEGER PRIMARY KEY";

    // Global Settings
    settingsTableColumns[Constants::SettingsT_Index_Displayname] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DisplaynameColor] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_MinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_AskPWAfterMinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_ReqPWDelay] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_OpenOnSettings] = "TEXT";

    // Diary Settings
    settingsTableColumns[Constants::SettingsT_Index_Diary_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampTimer] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampCounter] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_CanEditRecent] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_ShowTManLogs] = "TEXT";

    // Tasklists Settings
    settingsTableColumns[Constants::SettingsT_Index_TLists_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_LogToDiary] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_TaskType] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_CMess] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_PMess] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_Notif] = "TEXT";

    // Password Manager Settings
    settingsTableColumns[Constants::SettingsT_Index_PWMan_DefSortingMethod] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_HidePasswords] = "TEXT";

    // Encrypted Data Settings
    settingsTableColumns[Constants::SettingsT_Index_DataENC_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Image] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Video] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Tags] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Tags] = "TEXT";

    if (!m_dbManager.createTable("settings", settingsTableColumns)) {
        qWarning() << "Failed to create settings table:" << m_dbManager.lastError();
        return false;
    }

    return true;
}

bool DatabaseSettingsManager::rollbackFromV2()
{
    // Drop the settings table
    if (!m_dbManager.dropTable("settings")) {
        qWarning() << "Failed to drop settings table:" << m_dbManager.lastError();
        return false;
    }
    return true;
}

bool DatabaseSettingsManager::migrateToV3()
{
    // Migration to version 3: Remove deprecated tasklist settings columns
    // The columns to remove are:
    // - TLists_LogToDiary
    // - TLists_TaskType  
    // - TLists_CMess
    // - TLists_PMess
    // - TLists_Notif
    // We keep TLists_TextSize as it's still in use
    
    // SQLite doesn't support dropping columns directly, so we need to:
    // 1. Create a new table with the desired schema
    // 2. Copy data from old table
    // 3. Drop old table
    // 4. Rename new table
    
    // Create new table without the deprecated columns
    QMap<QString, QString> settingsTableColumns;
    settingsTableColumns["id"] = "INTEGER PRIMARY KEY";

    // Global Settings
    settingsTableColumns[Constants::SettingsT_Index_Displayname] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DisplaynameColor] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_MinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_AskPWAfterMinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_ReqPWDelay] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_OpenOnSettings] = "TEXT";

    // Diary Settings
    settingsTableColumns[Constants::SettingsT_Index_Diary_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampTimer] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampCounter] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_CanEditRecent] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_ShowTManLogs] = "TEXT";

    // Tasklists Settings - Only TextSize remains
    settingsTableColumns[Constants::SettingsT_Index_TLists_TextSize] = "TEXT";
    // Note: We're NOT adding the removed columns here

    // Password Manager Settings
    settingsTableColumns[Constants::SettingsT_Index_PWMan_DefSortingMethod] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_HidePasswords] = "TEXT";

    // Encrypted Data Settings
    settingsTableColumns[Constants::SettingsT_Index_DataENC_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Image] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Video] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Tags] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Tags] = "TEXT";

    // Create temporary table with new schema
    if (!m_dbManager.createTable("settings_temp", settingsTableColumns)) {
        qWarning() << "Failed to create temporary settings table:" << m_dbManager.lastError();
        return false;
    }

    // Build column list for data migration (only columns that exist in both tables)
    QStringList columns;
    columns << "id"
            << Constants::SettingsT_Index_Displayname
            << Constants::SettingsT_Index_DisplaynameColor
            << Constants::SettingsT_Index_MinToTray
            << Constants::SettingsT_Index_AskPWAfterMinToTray
            << Constants::SettingsT_Index_ReqPWDelay
            << Constants::SettingsT_Index_OpenOnSettings
            << Constants::SettingsT_Index_Diary_TextSize
            << Constants::SettingsT_Index_Diary_TStampTimer
            << Constants::SettingsT_Index_Diary_TStampCounter
            << Constants::SettingsT_Index_Diary_CanEditRecent
            << Constants::SettingsT_Index_Diary_ShowTManLogs
            << Constants::SettingsT_Index_TLists_TextSize  // Only this tasklist setting remains
            << Constants::SettingsT_Index_PWMan_DefSortingMethod
            << Constants::SettingsT_Index_PWMan_ReqPassword
            << Constants::SettingsT_Index_PWMan_HidePasswords
            << Constants::SettingsT_Index_DataENC_ReqPassword
            << Constants::SettingsT_Index_DataENC_HideThumbnails_Image
            << Constants::SettingsT_Index_DataENC_HideThumbnails_Video
            << Constants::SettingsT_Index_DataENC_Hidden_Categories
            << Constants::SettingsT_Index_DataENC_Hidden_Tags
            << Constants::SettingsT_Index_DataENC_Hide_Categories
            << Constants::SettingsT_Index_DataENC_Hide_Tags;

    QString columnList = columns.join(", ");

    // Copy data from old table to new table
    QString copyQuery = QString("INSERT INTO settings_temp (%1) SELECT %1 FROM settings").arg(columnList);
    if (!m_dbManager.executeQuery(copyQuery)) {
        qWarning() << "Failed to copy data to temporary table:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }

    // Drop old table
    if (!m_dbManager.dropTable("settings")) {
        qWarning() << "Failed to drop old settings table:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }

    // Rename temporary table to settings
    QString renameQuery = "ALTER TABLE settings_temp RENAME TO settings";
    if (!m_dbManager.executeQuery(renameQuery)) {
        qWarning() << "Failed to rename temporary table:" << m_dbManager.lastError();
        return false;
    }

    qDebug() << "Successfully migrated settings database to version 3 (removed deprecated tasklist settings)";
    return true;
}

bool DatabaseSettingsManager::rollbackFromV3()
{
    // For rollback, we would need to recreate the old columns
    // Since we've removed data, we can't fully restore it,
    // but we can recreate the structure with default values
    
    // This is similar to migrateToV2 but with all columns
    QMap<QString, QString> settingsTableColumns;
    settingsTableColumns["id"] = "INTEGER PRIMARY KEY";

    // Global Settings
    settingsTableColumns[Constants::SettingsT_Index_Displayname] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DisplaynameColor] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_MinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_AskPWAfterMinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_ReqPWDelay] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_OpenOnSettings] = "TEXT";

    // Diary Settings
    settingsTableColumns[Constants::SettingsT_Index_Diary_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampTimer] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampCounter] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_CanEditRecent] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_ShowTManLogs] = "TEXT";

    // Tasklists Settings - Restore all columns
    settingsTableColumns[Constants::SettingsT_Index_TLists_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_LogToDiary] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_TaskType] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_CMess] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_PMess] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_TLists_Notif] = "TEXT";

    // Password Manager Settings
    settingsTableColumns[Constants::SettingsT_Index_PWMan_DefSortingMethod] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_HidePasswords] = "TEXT";

    // Encrypted Data Settings
    settingsTableColumns[Constants::SettingsT_Index_DataENC_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Image] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Video] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Tags] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Tags] = "TEXT";

    // Similar migration process as migrateToV3, but in reverse
    if (!m_dbManager.createTable("settings_temp", settingsTableColumns)) {
        qWarning() << "Failed to create temporary settings table for rollback:" << m_dbManager.lastError();
        return false;
    }

    // Copy existing data and set defaults for missing columns
    QStringList existingColumns;
    existingColumns << "id"
                   << Constants::SettingsT_Index_Displayname
                   << Constants::SettingsT_Index_DisplaynameColor
                   << Constants::SettingsT_Index_MinToTray
                   << Constants::SettingsT_Index_AskPWAfterMinToTray
                   << Constants::SettingsT_Index_ReqPWDelay
                   << Constants::SettingsT_Index_OpenOnSettings
                   << Constants::SettingsT_Index_Diary_TextSize
                   << Constants::SettingsT_Index_Diary_TStampTimer
                   << Constants::SettingsT_Index_Diary_TStampCounter
                   << Constants::SettingsT_Index_Diary_CanEditRecent
                   << Constants::SettingsT_Index_Diary_ShowTManLogs
                   << Constants::SettingsT_Index_TLists_TextSize
                   << Constants::SettingsT_Index_PWMan_DefSortingMethod
                   << Constants::SettingsT_Index_PWMan_ReqPassword
                   << Constants::SettingsT_Index_PWMan_HidePasswords
                   << Constants::SettingsT_Index_DataENC_ReqPassword
                   << Constants::SettingsT_Index_DataENC_HideThumbnails_Image
                   << Constants::SettingsT_Index_DataENC_HideThumbnails_Video
                   << Constants::SettingsT_Index_DataENC_Hidden_Categories
                   << Constants::SettingsT_Index_DataENC_Hidden_Tags
                   << Constants::SettingsT_Index_DataENC_Hide_Categories
                   << Constants::SettingsT_Index_DataENC_Hide_Tags;

    QString existingColumnList = existingColumns.join(", ");

    // Copy existing data
    QString copyQuery = QString("INSERT INTO settings_temp (%1, %2, %3, %4, %5, %6) "
                                "SELECT %1, '0', 'Simple', 'None', 'None', '1' FROM settings")
                        .arg(existingColumnList)
                        .arg(Constants::SettingsT_Index_TLists_LogToDiary)
                        .arg(Constants::SettingsT_Index_TLists_TaskType)
                        .arg(Constants::SettingsT_Index_TLists_CMess)
                        .arg(Constants::SettingsT_Index_TLists_PMess)
                        .arg(Constants::SettingsT_Index_TLists_Notif);

    if (!m_dbManager.executeQuery(copyQuery)) {
        qWarning() << "Failed to copy data for rollback:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }

    // Drop current table and rename
    if (!m_dbManager.dropTable("settings")) {
        qWarning() << "Failed to drop settings table during rollback:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }

    QString renameQuery = "ALTER TABLE settings_temp RENAME TO settings";
    if (!m_dbManager.executeQuery(renameQuery)) {
        qWarning() << "Failed to rename temporary table during rollback:" << m_dbManager.lastError();
        return false;
    }

    qDebug() << "Successfully rolled back settings database from version 3";
    return true;
}

bool DatabaseSettingsManager::migrateToV4()
{
    // Migration to version 4: Add VideoPlayer settings columns
    qDebug() << "========================================";
    qDebug() << "DatabaseSettingsManager: Starting migration to v4";
    qDebug() << "========================================";

    // Check current table structure first
    QString checkQuery = "PRAGMA table_info(settings)";
    qDebug() << "DatabaseSettingsManager: Checking current table structure...";
    if (m_dbManager.executeQuery(checkQuery)) {
        qDebug() << "DatabaseSettingsManager: Current settings table structure checked";
    }

    // SQLite doesn't support adding multiple columns efficiently, so we'll use the table recreation approach
    QMap<QString, QString> settingsTableColumns;
    settingsTableColumns["id"] = "INTEGER PRIMARY KEY";

    // Global Settings
    settingsTableColumns[Constants::SettingsT_Index_Displayname] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DisplaynameColor] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_MinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_AskPWAfterMinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_ReqPWDelay] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_OpenOnSettings] = "TEXT";

    // Diary Settings
    settingsTableColumns[Constants::SettingsT_Index_Diary_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampTimer] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampCounter] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_CanEditRecent] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_ShowTManLogs] = "TEXT";

    // Tasklists Settings
    settingsTableColumns[Constants::SettingsT_Index_TLists_TextSize] = "TEXT";

    // Password Manager Settings
    settingsTableColumns[Constants::SettingsT_Index_PWMan_DefSortingMethod] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_HidePasswords] = "TEXT";

    // Encrypted Data Settings
    settingsTableColumns[Constants::SettingsT_Index_DataENC_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Image] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Video] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Tags] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Tags] = "TEXT";

    // NEW VideoPlayer Settings
    qDebug() << "DatabaseSettingsManager: Adding VideoPlayer columns to schema...";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_Autoplay] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_AutoplayRand] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_UseTMDB] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_DisplayFilenames] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_CheckNewEP] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_FileFolderParsing] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_AutoDelete] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_DefaultVolume] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_VP_Shows_CheckNewEPStartup] = "TEXT";
    qDebug() << "DatabaseSettingsManager: Total columns in new schema:" << settingsTableColumns.size();

    // Create temporary table with new schema
    qDebug() << "DatabaseSettingsManager: Creating temporary table with v4 schema...";
    if (!m_dbManager.createTable("settings_temp", settingsTableColumns)) {
        qCritical() << "DatabaseSettingsManager: FAILED to create temporary settings table for v4 migration:" << m_dbManager.lastError();
        return false;
    }
    qDebug() << "DatabaseSettingsManager: Temporary table created successfully";

    // Build column list for data migration (only columns that exist in the old table)
    QStringList existingColumns;
    existingColumns << "id"
                    << Constants::SettingsT_Index_Displayname
                    << Constants::SettingsT_Index_DisplaynameColor
                    << Constants::SettingsT_Index_MinToTray
                    << Constants::SettingsT_Index_AskPWAfterMinToTray
                    << Constants::SettingsT_Index_ReqPWDelay
                    << Constants::SettingsT_Index_OpenOnSettings
                    << Constants::SettingsT_Index_Diary_TextSize
                    << Constants::SettingsT_Index_Diary_TStampTimer
                    << Constants::SettingsT_Index_Diary_TStampCounter
                    << Constants::SettingsT_Index_Diary_CanEditRecent
                    << Constants::SettingsT_Index_Diary_ShowTManLogs
                    << Constants::SettingsT_Index_TLists_TextSize
                    << Constants::SettingsT_Index_PWMan_DefSortingMethod
                    << Constants::SettingsT_Index_PWMan_ReqPassword
                    << Constants::SettingsT_Index_PWMan_HidePasswords
                    << Constants::SettingsT_Index_DataENC_ReqPassword
                    << Constants::SettingsT_Index_DataENC_HideThumbnails_Image
                    << Constants::SettingsT_Index_DataENC_HideThumbnails_Video
                    << Constants::SettingsT_Index_DataENC_Hidden_Categories
                    << Constants::SettingsT_Index_DataENC_Hidden_Tags
                    << Constants::SettingsT_Index_DataENC_Hide_Categories
                    << Constants::SettingsT_Index_DataENC_Hide_Tags;

    QString columnList = existingColumns.join(", ");
    qDebug() << "DatabaseSettingsManager: Copying" << existingColumns.size() << "columns from old table";

    // Copy data from old table to new table
    QString copyQuery = QString("INSERT INTO settings_temp (%1) SELECT %1 FROM settings").arg(columnList);
    qDebug() << "DatabaseSettingsManager: Executing copy query...";
    if (!m_dbManager.executeQuery(copyQuery)) {
        qCritical() << "DatabaseSettingsManager: FAILED to copy data to temporary table for v4 migration:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }
    qDebug() << "DatabaseSettingsManager: Data copied successfully";

    // Drop old table
    qDebug() << "DatabaseSettingsManager: Dropping old settings table...";
    if (!m_dbManager.dropTable("settings")) {
        qCritical() << "DatabaseSettingsManager: FAILED to drop old settings table for v4 migration:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }
    qDebug() << "DatabaseSettingsManager: Old table dropped successfully";

    // Rename temporary table to settings
    QString renameQuery = "ALTER TABLE settings_temp RENAME TO settings";
    qDebug() << "DatabaseSettingsManager: Renaming temporary table to settings...";
    if (!m_dbManager.executeQuery(renameQuery)) {
        qCritical() << "DatabaseSettingsManager: FAILED to rename temporary table for v4 migration:" << m_dbManager.lastError();
        return false;
    }
    qDebug() << "DatabaseSettingsManager: Table renamed successfully";

    // Now set default values for the new VideoPlayer settings columns
    qDebug() << "DatabaseSettingsManager: Setting default values for VideoPlayer settings...";
    
    // Ensure settings record exists
    if (!ensureSettingsRecord()) {
        qCritical() << "DatabaseSettingsManager: FAILED to ensure settings record exists for v4 migration";
        return false;
    }
    
    // Set default values for VideoPlayer settings directly within the migration transaction
    // We cannot call SetDefault_VideoPlayerSettings because it tries to begin its own transaction
    // and we're already inside a migration transaction
    bool success = true;
    
    // Set each VideoPlayer default value directly
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_Autoplay, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_AUTOPLAY);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_AutoplayRand, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_AUTOPLAY_RAND);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_UseTMDB, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_USE_TMDB);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_DisplayFilenames, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_DISPLAY_FILENAMES);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_CheckNewEP, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_CHECK_NEW_EP);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_FileFolderParsing, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_FILE_FOLDER_PARSING);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_AutoDelete, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_AUTO_DELETE);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_DefaultVolume, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_DEFAULT_VOLUME);
    success &= UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_CheckNewEPStartup, 
                                       Default_UserSettings::DEFAULT_VP_SHOWS_CHECK_NEW_EP_STARTUP);
    
    if (!success) {
        qWarning() << "DatabaseSettingsManager: Some VideoPlayer default values may not have been set during v4 migration";
        // Don't fail the migration - the columns exist and can be populated later
    } else {
        qDebug() << "DatabaseSettingsManager: All VideoPlayer default values set successfully";
    }

    qDebug() << "========================================";
    qDebug() << "DatabaseSettingsManager: Migration to v4 COMPLETED SUCCESSFULLY";
    qDebug() << "========================================";
    return true;
}

bool DatabaseSettingsManager::rollbackFromV4()
{
    // Rollback from version 4: Remove VideoPlayer settings columns
    qDebug() << "DatabaseSettingsManager: Rolling back from v4 - removing video player settings";
    
    // Create table with v3 schema (without VideoPlayer settings)
    QMap<QString, QString> settingsTableColumns;
    settingsTableColumns["id"] = "INTEGER PRIMARY KEY";

    // Global Settings
    settingsTableColumns[Constants::SettingsT_Index_Displayname] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DisplaynameColor] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_MinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_AskPWAfterMinToTray] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_ReqPWDelay] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_OpenOnSettings] = "TEXT";

    // Diary Settings
    settingsTableColumns[Constants::SettingsT_Index_Diary_TextSize] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampTimer] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_TStampCounter] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_CanEditRecent] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_Diary_ShowTManLogs] = "TEXT";

    // Tasklists Settings
    settingsTableColumns[Constants::SettingsT_Index_TLists_TextSize] = "TEXT";

    // Password Manager Settings
    settingsTableColumns[Constants::SettingsT_Index_PWMan_DefSortingMethod] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_PWMan_HidePasswords] = "TEXT";

    // Encrypted Data Settings
    settingsTableColumns[Constants::SettingsT_Index_DataENC_ReqPassword] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Image] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_HideThumbnails_Video] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hidden_Tags] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Categories] = "TEXT";
    settingsTableColumns[Constants::SettingsT_Index_DataENC_Hide_Tags] = "TEXT";
    
    // Create temporary table with v3 schema
    if (!m_dbManager.createTable("settings_temp", settingsTableColumns)) {
        qWarning() << "Failed to create temporary settings table for v4 rollback:" << m_dbManager.lastError();
        return false;
    }

    // Build column list for data migration (only columns that exist in v3)
    QStringList v3Columns;
    v3Columns << "id"
              << Constants::SettingsT_Index_Displayname
              << Constants::SettingsT_Index_DisplaynameColor
              << Constants::SettingsT_Index_MinToTray
              << Constants::SettingsT_Index_AskPWAfterMinToTray
              << Constants::SettingsT_Index_ReqPWDelay
              << Constants::SettingsT_Index_OpenOnSettings
              << Constants::SettingsT_Index_Diary_TextSize
              << Constants::SettingsT_Index_Diary_TStampTimer
              << Constants::SettingsT_Index_Diary_TStampCounter
              << Constants::SettingsT_Index_Diary_CanEditRecent
              << Constants::SettingsT_Index_Diary_ShowTManLogs
              << Constants::SettingsT_Index_TLists_TextSize
              << Constants::SettingsT_Index_PWMan_DefSortingMethod
              << Constants::SettingsT_Index_PWMan_ReqPassword
              << Constants::SettingsT_Index_PWMan_HidePasswords
              << Constants::SettingsT_Index_DataENC_ReqPassword
              << Constants::SettingsT_Index_DataENC_HideThumbnails_Image
              << Constants::SettingsT_Index_DataENC_HideThumbnails_Video
              << Constants::SettingsT_Index_DataENC_Hidden_Categories
              << Constants::SettingsT_Index_DataENC_Hidden_Tags
              << Constants::SettingsT_Index_DataENC_Hide_Categories
              << Constants::SettingsT_Index_DataENC_Hide_Tags;

    QString columnList = v3Columns.join(", ");

    // Copy data from current table to temporary table (excluding VideoPlayer columns)
    QString copyQuery = QString("INSERT INTO settings_temp (%1) SELECT %1 FROM settings").arg(columnList);
    if (!m_dbManager.executeQuery(copyQuery)) {
        qWarning() << "Failed to copy data for v4 rollback:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }

    // Drop current table
    if (!m_dbManager.dropTable("settings")) {
        qWarning() << "Failed to drop settings table during v4 rollback:" << m_dbManager.lastError();
        m_dbManager.dropTable("settings_temp");
        return false;
    }

    // Rename temporary table to settings
    QString renameQuery = "ALTER TABLE settings_temp RENAME TO settings";
    if (!m_dbManager.executeQuery(renameQuery)) {
        qWarning() << "Failed to rename temporary table during v4 rollback:" << m_dbManager.lastError();
        return false;
    }

    qDebug() << "Successfully rolled back settings database from version 4";
    return true;
}

bool DatabaseSettingsManager::initializeVersioning()
{
    return m_dbManager.initializeVersioning();
}

bool DatabaseSettingsManager::beginTransaction()
{
    return m_dbManager.beginTransaction();
}

bool DatabaseSettingsManager::commitTransaction()
{
    return m_dbManager.commitTransaction();
}

bool DatabaseSettingsManager::rollbackTransaction()
{
    return m_dbManager.rollbackTransaction();
}

QString DatabaseSettingsManager::lastError() const
{
    return m_dbManager.lastError();
}

int DatabaseSettingsManager::lastInsertId() const
{
    return m_dbManager.lastInsertId();
}
