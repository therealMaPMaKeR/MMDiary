#include "sqlite-database-persistentsettings.h"
#include "constants.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>

DatabasePersistentSettingsManager::DatabasePersistentSettingsManager()
{
}

DatabasePersistentSettingsManager::~DatabasePersistentSettingsManager()
{
    close();
}

DatabasePersistentSettingsManager& DatabasePersistentSettingsManager::instance()
{
    static DatabasePersistentSettingsManager instance;
    return instance;
}

QString DatabasePersistentSettingsManager::getPersistentSettingsDatabasePath(const QString& username)
{
    return QString("Data/%1/persistent.db").arg(username);
}

bool DatabasePersistentSettingsManager::connect(const QString& username, const QByteArray& encryptionKey)
{
    m_currentUsername = username;
    m_encryptionKey = encryptionKey;

    QString dbPath = getPersistentSettingsDatabasePath(username);

    // Ensure the user directory exists
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qDebug() << "Failed to create directory for persistent settings database:" << dir.path();
            return false;
        }
    }

    bool success = m_dbManager.connect(dbPath);
    if (!success) {
        qDebug() << "Failed to connect to persistent settings database:" << m_dbManager.lastError();
        return false;
    }

    // Check if this is a new database (no persistent settings table)
    bool isNewDatabase = !m_dbManager.tableExists("persistentSettingsTable");

    if (isNewDatabase) {
        // Initialize versioning for new database
        if (!initializeVersioning()) {
            qDebug() << "Failed to initialize versioning for persistent settings database";
            return false;
        }

        // Run migrations to create tables
        if (!migratePersistentSettingsDatabase()) {
            qDebug() << "Failed to migrate persistent settings database";
            return false;
        }
    } else {
        // Validate database integrity (silent corruption recovery)
        if (!isDatabaseValid()) {
            qDebug() << "Persistent settings database corrupted, recreating silently";
            close();
            return createOrRecreatePersistentSettingsDatabase(username, encryptionKey);
        }
    }

    return true;
}

bool DatabasePersistentSettingsManager::validateEncryptionKey()
{
    if (!isConnected()) {
        return false;
    }

    // Check if persistent settings table exists and has data
    if (!m_dbManager.tableExists("persistentSettingsTable")) {
        return true; // New database, validation not needed
    }

    // Try to read some encrypted data to validate the key
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("persistentSettingsTable", QStringList(), "", QMap<QString, QVariant>(), QStringList(), 1);
    if (results.isEmpty()) {
        return true; // No data to validate
    }

    // Try to decrypt some text data to validate key
    QString testData = results.first()[Constants::PSettingsT_Index_TLists_CurrentList].toString();
    if (testData.isEmpty()) {
        return true; // No encrypted data to validate
    }

    // Try to decrypt to see if key works
    try {
        QString decrypted = CryptoUtils::Encryption_Decrypt(m_encryptionKey, testData);
        return true; // Decryption succeeded
    } catch (...) {
        return false; // Decryption failed
    }
}

bool DatabasePersistentSettingsManager::isDatabaseValid()
{
    if (!isConnected()) {
        return false;
    }

    // Check if required table exists
    if (!m_dbManager.tableExists("persistentSettingsTable")) {
        return false;
    }

    // Validate encryption key
    return validateEncryptionKey();
}

bool DatabasePersistentSettingsManager::createOrRecreatePersistentSettingsDatabase(const QString& username, const QByteArray& encryptionKey)
{
    m_currentUsername = username;
    m_encryptionKey = encryptionKey;

    QString dbPath = getPersistentSettingsDatabasePath(username);

    // Close any existing connection
    close();

    // Remove existing database file if it exists (silent recovery)
    if (QFile::exists(dbPath)) {
        if (!QFile::remove(dbPath)) {
            qDebug() << "Failed to remove corrupted persistent settings database";
            return false;
        }
    }

    // Ensure the user directory exists
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qDebug() << "Failed to create directory for persistent settings database:" << dir.path();
            return false;
        }
    }

    // Connect to new database
    bool success = m_dbManager.connect(dbPath);
    if (!success) {
        qDebug() << "Failed to connect to new persistent settings database:" << m_dbManager.lastError();
        return false;
    }

    // Initialize versioning
    if (!initializeVersioning()) {
        qDebug() << "Failed to initialize versioning for persistent settings database";
        return false;
    }

    // Run migrations to create tables
    if (!migratePersistentSettingsDatabase()) {
        qDebug() << "Failed to migrate persistent settings database";
        return false;
    }

    return true;
}

bool DatabasePersistentSettingsManager::isConnected() const
{
    return m_dbManager.isConnected();
}

void DatabasePersistentSettingsManager::close()
{
    m_dbManager.close();
}

bool DatabasePersistentSettingsManager::IndexIsValid(QString index, QString type)
{
    // Create a static map for column types (only created once)
    static QMap<QString, QString> columnTypes;

    // Initialize the map on first call
    if (columnTypes.isEmpty()) {
        // Main window settings (INT)
        columnTypes[Constants::PSettingsT_Index_MainWindow_SizeX] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainWindow_SizeY] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainWindow_PosX] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainWindow_PosY] = Constants::DataType_INT;

        // Tab indices (INT)
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_Settings] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_Diary] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData] = Constants::DataType_INT;

        // Tasklist settings (TEXT - potentially sensitive)
        columnTypes[Constants::PSettingsT_Index_TLists_CurrentList] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_TLists_CurrentTask] = Constants::DataType_QString;
    }

    // Check if the column exists in our map
    if (!columnTypes.contains(index)) {
        qDebug() << "INDEXINVALID: Column does not exist in persistent settings mapping:" << index;
        return false;
    }

    // Check if the requested type matches the actual column type
    if (columnTypes[index] != type) {
        qDebug() << "INDEXINVALID: Type mismatch for persistent settings column" << index
                 << "- expected:" << columnTypes[index]
                 << "requested:" << type;
        return false;
    }

    return true;
}

QString DatabasePersistentSettingsManager::GetPersistentSettingsData_String(QString index)
{
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        return "";
    }

    // Ensure connection
    if (!isConnected()) {
        return "";
    }

    // Ensure persistent settings record exists
    if (!ensurePersistentSettingsRecord()) {
        return "";
    }

    QStringList columns = {index};
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("persistentSettingsTable", columns);
    if (results.isEmpty()) {
        return "";
    }

    QString encryptedValue = results.first()[index].toString();
    if (encryptedValue.isEmpty()) {
        return ""; // Empty value is valid
    }

    // Decrypt the value
    try {
        return CryptoUtils::Encryption_Decrypt(m_encryptionKey, encryptedValue);
    } catch (...) {
        qDebug() << "Failed to decrypt persistent settings value for index:" << index;
        return "";
    }
}

QByteArray DatabasePersistentSettingsManager::GetPersistentSettingsData_ByteA(QString index)
{
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        return QByteArray();
    }

    // Ensure connection
    if (!isConnected()) {
        return QByteArray();
    }

    // Ensure persistent settings record exists
    if (!ensurePersistentSettingsRecord()) {
        return QByteArray();
    }

    QStringList columns = {index};
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("persistentSettingsTable", columns);
    if (results.isEmpty()) {
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
        qDebug() << "Failed to decrypt persistent settings ByteArray for index:" << index;
        return QByteArray();
    }
}

int DatabasePersistentSettingsManager::GetPersistentSettingsData_Int(QString index)
{
    if (!IndexIsValid(index, Constants::DataType_INT)) {
        return -1; // Return -1 for invalid/empty int values
    }

    // Ensure connection
    if (!isConnected()) {
        return -1;
    }

    // Ensure persistent settings record exists
    if (!ensurePersistentSettingsRecord()) {
        return -1;
    }

    QStringList columns = {index};
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("persistentSettingsTable", columns);
    if (results.isEmpty()) {
        return -1;
    }

    // INT values are stored directly (not encrypted for performance)
    QVariant value = results.first()[index];
    if (value.isNull()) {
        return -1;
    }

    return value.toInt();
}

bool DatabasePersistentSettingsManager::UpdatePersistentSettingsData_TEXT(QString index, QString data)
{
    // Validate index is for TEXT data
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        return false;
    }

    // Ensure persistent settings record exists
    if (!ensurePersistentSettingsRecord()) {
        return false;
    }

    // Encrypt the data
    QString encryptedData;
    if (!data.isEmpty()) {
        try {
            encryptedData = CryptoUtils::Encryption_Encrypt(m_encryptionKey, data, m_currentUsername);
        } catch (...) {
            qDebug() << "Failed to encrypt persistent settings data for index:" << index;
            return false;
        }
    }

    // Update persistent settings data
    QMap<QString, QVariant> updateData;
    updateData[index] = encryptedData;

    return m_dbManager.update("persistentSettingsTable", updateData, "", QMap<QString, QVariant>());
}

bool DatabasePersistentSettingsManager::UpdatePersistentSettingsData_BLOB(QString index, QByteArray data)
{
    // Validate index is for BLOB data
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        return false;
    }

    // Ensure persistent settings record exists
    if (!ensurePersistentSettingsRecord()) {
        return false;
    }

    // Encrypt the data
    QByteArray encryptedData;
    if (!data.isEmpty()) {
        try {
            encryptedData = CryptoUtils::Encryption_EncryptBArray(m_encryptionKey, data, m_currentUsername);
        } catch (...) {
            qDebug() << "Failed to encrypt persistent settings ByteArray for index:" << index;
            return false;
        }
    }

    // Update persistent settings data
    QMap<QString, QVariant> updateData;
    updateData[index] = encryptedData;

    return m_dbManager.update("persistentSettingsTable", updateData, "", QMap<QString, QVariant>());
}

bool DatabasePersistentSettingsManager::UpdatePersistentSettingsData_INT(QString index, int data)
{
    // Validate index is for INT data
    if (!IndexIsValid(index, Constants::DataType_INT)) {
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        return false;
    }

    // Ensure persistent settings record exists
    if (!ensurePersistentSettingsRecord()) {
        return false;
    }

    // Update persistent settings data (INT values stored directly, not encrypted)
    QMap<QString, QVariant> updateData;
    updateData[index] = data;

    return m_dbManager.update("persistentSettingsTable", updateData, "", QMap<QString, QVariant>());
}

bool DatabasePersistentSettingsManager::ensurePersistentSettingsRecord()
{
    // Check if persistent settings record exists
    QVector<QMap<QString, QVariant>> results = m_dbManager.select("persistentSettingsTable");
    if (results.isEmpty()) {
        // Create a persistent settings record
        QMap<QString, QVariant> persistentSettingsData;
        persistentSettingsData["id"] = 1; // Single persistent settings record
        return m_dbManager.insert("persistentSettingsTable", persistentSettingsData);
    }
    return true;
}

bool DatabasePersistentSettingsManager::migratePersistentSettingsDatabase()
{
    if (!isConnected()) {
        return false;
    }

    // Use the generic migration system with persistent settings-specific callbacks
    auto migrationCallback = [this](int version) -> bool {
        return persistentSettingsMigrationCallback(version);
    };

    auto rollbackCallback = [this](int version) -> bool {
        return persistentSettingsRollbackCallback(version);
    };

    return m_dbManager.migrateDatabase(LATEST_PERSISTENT_SETTINGS_VERSION, migrationCallback, rollbackCallback);
}

bool DatabasePersistentSettingsManager::persistentSettingsMigrationCallback(int version)
{
    switch (version) {
    case 2:
        return migrateToV2();
    default:
        qWarning() << "No persistent settings migration defined for version" << version;
        return false;
    }
}

bool DatabasePersistentSettingsManager::persistentSettingsRollbackCallback(int version)
{
    switch (version) {
    case 2:
        return rollbackFromV2();
    default:
        qWarning() << "No persistent settings rollback defined for version" << version;
        return false;
    }
}

bool DatabasePersistentSettingsManager::migrateToV2()
{
    // Create the persistent settings table
    QMap<QString, QString> persistentSettingsTableColumns;
    persistentSettingsTableColumns["id"] = "INTEGER PRIMARY KEY";

    // Main window settings
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainWindow_SizeX] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainWindow_SizeY] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainWindow_PosX] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainWindow_PosY] = "INTEGER";

    // Tab indices
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_Settings] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_Diary] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager] = "INTEGER";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData] = "INTEGER";

    // Tasklist settings (TEXT - encrypted due to potential sensitivity)
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TLists_CurrentList] = "TEXT";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TLists_CurrentTask] = "TEXT";

    if (!m_dbManager.createTable("persistentSettingsTable", persistentSettingsTableColumns)) {
        qWarning() << "Failed to create persistent settings table:" << m_dbManager.lastError();
        return false;
    }

    return true;
}

bool DatabasePersistentSettingsManager::rollbackFromV2()
{
    // Drop the persistent settings table
    if (!m_dbManager.dropTable("persistentSettingsTable")) {
        qWarning() << "Failed to drop persistent settings table:" << m_dbManager.lastError();
        return false;
    }
    return true;
}

bool DatabasePersistentSettingsManager::initializeVersioning()
{
    return m_dbManager.initializeVersioning();
}

bool DatabasePersistentSettingsManager::beginTransaction()
{
    return m_dbManager.beginTransaction();
}

bool DatabasePersistentSettingsManager::commitTransaction()
{
    return m_dbManager.commitTransaction();
}

bool DatabasePersistentSettingsManager::rollbackTransaction()
{
    return m_dbManager.rollbackTransaction();
}

QString DatabasePersistentSettingsManager::lastError() const
{
    return m_dbManager.lastError();
}

int DatabasePersistentSettingsManager::lastInsertId() const
{
    return m_dbManager.lastInsertId();
}
