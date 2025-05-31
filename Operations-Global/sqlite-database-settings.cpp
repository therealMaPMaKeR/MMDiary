#include "sqlite-database-settings.h"
#include "constants.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

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
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("settings", QStringList(), "", QMap<QString, QVariant>(), QStringList(), 1);
    if (results.isEmpty()) {
        return true; // No data to validate
    }

    // Try to decrypt some data to validate key
    // We can use any text field for validation
    QString testData = results.first()[Constants::SettingsT_Index_DisplaynameColor].toString();
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
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("settings", columns);
    if (results.isEmpty()) {
        qDebug() << "No settings data found";
        return Constants::ErrorMessage_Default;
    }

    QString encryptedValue = results.first()[index].toString();
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
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("settings", columns);
    if (results.isEmpty()) {
        qDebug() << "No settings data found";
        return QByteArray();
    }

    QByteArray encryptedValue = results.first()[index].toByteArray();
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
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("settings");
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
