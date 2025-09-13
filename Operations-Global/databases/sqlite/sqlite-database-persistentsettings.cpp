#include "sqlite-database-persistentsettings.h"
#include "constants.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QCoreApplication>
// IMPORTANT: ONLY INT FIELDS SHOULD BE LEFT UNENCRYPTED. WE NEED TO ENCRYPT EVERYTHING ELSE

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
    // Try to use the existing relative path first (for backward compatibility)
    QString relativePath = QString("Data/%1/persistent.db").arg(username);
    QFileInfo relativeInfo(relativePath);
    
    // Check if the relative path exists or if its parent directory exists
    QDir relativeDir = relativeInfo.absoluteDir();
    if (relativeDir.exists() || QFile::exists(relativePath)) {
        return relativePath;
    }
    
    // Otherwise, use absolute path based on application directory
    QString appDir = QCoreApplication::applicationDirPath();
    QString dbPath = QString("%1/Data/%2/persistent.db").arg(appDir).arg(username);
    
    return dbPath;
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
            qDebug() << "DatabasePersistentSettingsManager: Failed to create directory for persistent settings database:" << dir.path();
            return false;
        }
    }

    bool success = m_dbManager.connect(dbPath);
    if (!success) {
        qDebug() << "DatabasePersistentSettingsManager: Failed to connect to persistent settings database:" << m_dbManager.lastError();
        return false;
    }

    // Check if this is a new database (no persistent settings table)
    bool isNewDatabase = !m_dbManager.tableExists("persistentSettingsTable");

    if (isNewDatabase) {
        // Initialize versioning for new database
        if (!initializeVersioning()) {
            qDebug() << "DatabasePersistentSettingsManager: Failed to initialize versioning for persistent settings database";
            return false;
        }

        // Run migrations to create tables
        if (!migratePersistentSettingsDatabase()) {
            qDebug() << "DatabasePersistentSettingsManager: Failed to migrate persistent settings database";
            return false;
        }
    } else {
        // Validate database integrity (silent corruption recovery)
        if (!isDatabaseValid()) {
            qDebug() << "DatabasePersistentSettingsManager: Persistent settings database corrupted, recreating silently";
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
    DatabaseResult results = m_dbManager.select("persistentSettingsTable", QStringList(), "", QMap<QString, QVariant>(), QStringList(), 1);
    if (results.isEmpty()) {
        return true; // No data to validate
    }

    // Try to decrypt some text data to validate key
    auto firstRow = results.first();
    if (!firstRow.has_value()) {
        return true; // No data to validate
    }
    
    QString testData = firstRow.value()[Constants::PSettingsT_Index_TLists_CurrentList].toString();
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
            qDebug() << "DatabasePersistentSettingsManager: Failed to remove corrupted persistent settings database";
            return false;
        }
    }

    // Ensure the user directory exists
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qDebug() << "DatabasePersistentSettingsManager: Failed to create directory for persistent settings database:" << dir.path();
            return false;
        }
    }

    // Connect to new database
    bool success = m_dbManager.connect(dbPath);
    if (!success) {
        qDebug() << "DatabasePersistentSettingsManager: Failed to connect to new persistent settings database:" << m_dbManager.lastError();
        return false;
    }

    // Initialize versioning
    if (!initializeVersioning()) {
        qDebug() << "DatabasePersistentSettingsManager: Failed to initialize versioning for persistent settings database";
        return false;
    }

    // Run migrations to create tables
    if (!migratePersistentSettingsDatabase()) {
        qDebug() << "DatabasePersistentSettingsManager: Failed to migrate persistent settings database";
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
        columnTypes[Constants::PSettingsT_Index_MainTabWidgetIndex_VideoPlayer] = Constants::DataType_INT;

        // Tab visibility (INT - 0 = hidden, 1 = visible)
        columnTypes[Constants::PSettingsT_Index_TabVisible_Diaries] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_TabVisible_Tasklists] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_TabVisible_Passwords] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_TabVisible_DataEncryption] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_TabVisible_Settings] = Constants::DataType_INT;
        columnTypes[Constants::PSettingsT_Index_TabVisible_VideoPlayer] = Constants::DataType_INT;

        // Tasklist settings (TEXT - potentially sensitive)
        columnTypes[Constants::PSettingsT_Index_TLists_CurrentList] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_TLists_CurrentTask] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_TLists_FoldedCategories] = Constants::DataType_QString;

        // Encrypted Data settings (TEXT - potentially sensitive)
        columnTypes[Constants::PSettingsT_Index_DataENC_CurrentCategory] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_DataENC_CurrentTags] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_DataENC_SortType] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_DataENC_TagSelectionMode] = Constants::DataType_QString;
        
        // Video Player settings (TEXT - potentially sensitive show information)
        columnTypes[Constants::PSettingsT_Index_VP_Shows_ShowsListViewMode] = Constants::DataType_QString;
        columnTypes[Constants::PSettingsT_Index_VP_Shows_CurrentShow] = Constants::DataType_QString;
    }

    // Check if the column exists in our map
    if (!columnTypes.contains(index)) {
        qDebug() << "DatabasePersistentSettingsManager: INDEXINVALID: Column does not exist in persistent settings mapping:" << index;
        return false;
    }

    // Check if the requested type matches the actual column type
    if (columnTypes[index] != type) {
        qDebug() << "DatabasePersistentSettingsManager: INDEXINVALID: Type mismatch for persistent settings column" << index
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
    DatabaseResult results = m_dbManager.select("persistentSettingsTable", columns);
    if (results.isEmpty()) {
        return "";
    }

    auto firstRow = results.first();
    if (!firstRow.has_value()) {
        return "";
    }
    
    QString encryptedValue = firstRow.value()[index].toString();
    if (encryptedValue.isEmpty()) {
        return ""; // Empty value is valid
    }

    // Decrypt the value
    try {
        return CryptoUtils::Encryption_Decrypt(m_encryptionKey, encryptedValue);
    } catch (...) {
        qDebug() << "DatabasePersistentSettingsManager: Failed to decrypt persistent settings value for index:" << index;
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
    DatabaseResult results = m_dbManager.select("persistentSettingsTable", columns);
    if (results.isEmpty()) {
        return QByteArray();
    }

    auto firstRow = results.first();
    if (!firstRow.has_value()) {
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
        qDebug() << "DatabasePersistentSettingsManager: Failed to decrypt persistent settings ByteArray for index:" << index;
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
    DatabaseResult results = m_dbManager.select("persistentSettingsTable", columns);
    if (results.isEmpty()) {
        return -1;
    }

    auto firstRow = results.first();
    if (!firstRow.has_value()) {
        return -1;
    }
    
    // INT values are stored directly (not encrypted for performance)
    QVariant value = firstRow.value()[index];
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
            qDebug() << "DatabasePersistentSettingsManager: Failed to encrypt persistent settings data for index:" << index;
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
            qDebug() << "DatabasePersistentSettingsManager: Failed to encrypt persistent settings ByteArray for index:" << index;
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
    DatabaseResult results = m_dbManager.select("persistentSettingsTable");
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
    case 3:
        return migrateToV3();
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
    case 3:
        return rollbackFromV3();
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

    // Tab visibility (0 = hidden, 1 = visible, default to 1 for all tabs)
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TabVisible_Diaries] = "INTEGER DEFAULT 1";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TabVisible_Tasklists] = "INTEGER DEFAULT 1";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TabVisible_Passwords] = "INTEGER DEFAULT 1";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TabVisible_DataEncryption] = "INTEGER DEFAULT 1";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TabVisible_Settings] = "INTEGER DEFAULT 1";

    // Tasklist settings (TEXT - encrypted due to potential sensitivity)
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TLists_CurrentList] = "TEXT";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_TLists_CurrentTask] = "TEXT";

    // Encrypted Data settings (TEXT - encrypted due to potential sensitivity)
    persistentSettingsTableColumns[Constants::PSettingsT_Index_DataENC_CurrentCategory] = "TEXT";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_DataENC_CurrentTags] = "TEXT";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_DataENC_SortType] = "TEXT";
    persistentSettingsTableColumns[Constants::PSettingsT_Index_DataENC_TagSelectionMode] = "TEXT";

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

bool DatabasePersistentSettingsManager::migrateToV3()
{
    qDebug() << "DatabasePersistentSettingsManager: Migrating persistent settings database to v3 - adding video player and tasklist columns";
    
    // Safer approach: create new table, copy data, drop old, rename
    // 1. Create a new table with the desired schema
    QMap<QString, QString> newTableColumns;
    newTableColumns["id"] = "INTEGER PRIMARY KEY";
    
    // Main window settings
    newTableColumns[Constants::PSettingsT_Index_MainWindow_SizeX] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainWindow_SizeY] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainWindow_PosX] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainWindow_PosY] = "INTEGER";
    
    // Tab indices
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_Settings] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_Diary] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData] = "INTEGER";
    newTableColumns[Constants::PSettingsT_Index_MainTabWidgetIndex_VideoPlayer] = "INTEGER"; // New in v3
    
    // Tab visibility
    newTableColumns[Constants::PSettingsT_Index_TabVisible_Diaries] = "INTEGER DEFAULT 1";
    newTableColumns[Constants::PSettingsT_Index_TabVisible_Tasklists] = "INTEGER DEFAULT 1";
    newTableColumns[Constants::PSettingsT_Index_TabVisible_Passwords] = "INTEGER DEFAULT 1";
    newTableColumns[Constants::PSettingsT_Index_TabVisible_DataEncryption] = "INTEGER DEFAULT 1";
    newTableColumns[Constants::PSettingsT_Index_TabVisible_Settings] = "INTEGER DEFAULT 1";
    newTableColumns[Constants::PSettingsT_Index_TabVisible_VideoPlayer] = "INTEGER DEFAULT 1"; // New in v3
    
    // Tasklist settings
    newTableColumns[Constants::PSettingsT_Index_TLists_CurrentList] = "TEXT";
    newTableColumns[Constants::PSettingsT_Index_TLists_CurrentTask] = "TEXT";
    newTableColumns[Constants::PSettingsT_Index_TLists_FoldedCategories] = "TEXT"; // New in v3
    
    // Encrypted Data settings
    newTableColumns[Constants::PSettingsT_Index_DataENC_CurrentCategory] = "TEXT";
    newTableColumns[Constants::PSettingsT_Index_DataENC_CurrentTags] = "TEXT";
    newTableColumns[Constants::PSettingsT_Index_DataENC_SortType] = "TEXT";
    newTableColumns[Constants::PSettingsT_Index_DataENC_TagSelectionMode] = "TEXT";
    
    // VideoPlayer settings (New in v3)
    newTableColumns[Constants::PSettingsT_Index_VP_Shows_ShowsListViewMode] = "TEXT";
    newTableColumns[Constants::PSettingsT_Index_VP_Shows_CurrentShow] = "TEXT";
    
    // Create the new table
    if (!m_dbManager.createTable("persistentSettingsTable_new", newTableColumns)) {
        qWarning() << "DatabasePersistentSettingsManager: Failed to create new table for v3 migration:" << m_dbManager.lastError();
        return false;
    }
    
    // 2. Copy data from old table (only columns that exist in v2)
    QString copyQuery = QString(
        "INSERT INTO persistentSettingsTable_new (id, %1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16, %17, %18, %19, %20) "
        "SELECT id, %1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16, %17, %18, %19, %20 "
        "FROM persistentSettingsTable")
        .arg(Constants::PSettingsT_Index_MainWindow_SizeX)
        .arg(Constants::PSettingsT_Index_MainWindow_SizeY)
        .arg(Constants::PSettingsT_Index_MainWindow_PosX)
        .arg(Constants::PSettingsT_Index_MainWindow_PosY)
        .arg(Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex)
        .arg(Constants::PSettingsT_Index_MainTabWidgetIndex_Settings)
        .arg(Constants::PSettingsT_Index_MainTabWidgetIndex_Diary)
        .arg(Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists)
        .arg(Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager)
        .arg(Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData)
        .arg(Constants::PSettingsT_Index_TabVisible_Diaries)
        .arg(Constants::PSettingsT_Index_TabVisible_Tasklists)
        .arg(Constants::PSettingsT_Index_TabVisible_Passwords)
        .arg(Constants::PSettingsT_Index_TabVisible_DataEncryption)
        .arg(Constants::PSettingsT_Index_TabVisible_Settings)
        .arg(Constants::PSettingsT_Index_TLists_CurrentList)
        .arg(Constants::PSettingsT_Index_TLists_CurrentTask)
        .arg(Constants::PSettingsT_Index_DataENC_CurrentCategory)
        .arg(Constants::PSettingsT_Index_DataENC_CurrentTags)
        .arg(Constants::PSettingsT_Index_DataENC_SortType)
        .arg(Constants::PSettingsT_Index_DataENC_TagSelectionMode);
    
    if (!m_dbManager.executeQuery(copyQuery)) {
        qWarning() << "DatabasePersistentSettingsManager: Failed to copy data to new table:" << m_dbManager.lastError();
        // Try to clean up
        m_dbManager.dropTable("persistentSettingsTable_new");
        return false;
    }
    
    // 3. Drop old table
    if (!m_dbManager.dropTable("persistentSettingsTable")) {
        qWarning() << "DatabasePersistentSettingsManager: Failed to drop old table:" << m_dbManager.lastError();
        // Try to clean up
        m_dbManager.dropTable("persistentSettingsTable_new");
        return false;
    }
    
    // 4. Rename new table
    QString renameQuery = "ALTER TABLE persistentSettingsTable_new RENAME TO persistentSettingsTable";
    if (!m_dbManager.executeQuery(renameQuery)) {
        qWarning() << "DatabasePersistentSettingsManager: Failed to rename new table:" << m_dbManager.lastError();
        return false;
    }
    
    qDebug() << "DatabasePersistentSettingsManager: Successfully migrated to v3";
    return true;
}

bool DatabasePersistentSettingsManager::rollbackFromV3()
{
    qDebug() << "DatabasePersistentSettingsManager: Rolling back from v3 - removing video player and new tasklist columns";
    
    // SQLite doesn't support DROP COLUMN, so we need to:
    // 1. Create a new temporary table without the new columns
    // 2. Copy data from the current table
    // 3. Drop the current table
    // 4. Rename the temporary table
    
    // Create temporary table with v2 schema
    QMap<QString, QString> v2Columns;
    v2Columns["id"] = "INTEGER PRIMARY KEY";
    
    // Main window settings
    v2Columns[Constants::PSettingsT_Index_MainWindow_SizeX] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainWindow_SizeY] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainWindow_PosX] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainWindow_PosY] = "INTEGER";
    
    // Tab indices (without VideoPlayer)
    v2Columns[Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainTabWidgetIndex_Settings] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainTabWidgetIndex_Diary] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager] = "INTEGER";
    v2Columns[Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData] = "INTEGER";
    
    // Tab visibility (without VideoPlayer)
    v2Columns[Constants::PSettingsT_Index_TabVisible_Diaries] = "INTEGER DEFAULT 1";
    v2Columns[Constants::PSettingsT_Index_TabVisible_Tasklists] = "INTEGER DEFAULT 1";
    v2Columns[Constants::PSettingsT_Index_TabVisible_Passwords] = "INTEGER DEFAULT 1";
    v2Columns[Constants::PSettingsT_Index_TabVisible_DataEncryption] = "INTEGER DEFAULT 1";
    v2Columns[Constants::PSettingsT_Index_TabVisible_Settings] = "INTEGER DEFAULT 1";
    
    // Tasklist settings (without FoldedCategories)
    v2Columns[Constants::PSettingsT_Index_TLists_CurrentList] = "TEXT";
    v2Columns[Constants::PSettingsT_Index_TLists_CurrentTask] = "TEXT";
    
    // Encrypted Data settings
    v2Columns[Constants::PSettingsT_Index_DataENC_CurrentCategory] = "TEXT";
    v2Columns[Constants::PSettingsT_Index_DataENC_CurrentTags] = "TEXT";
    v2Columns[Constants::PSettingsT_Index_DataENC_SortType] = "TEXT";
    v2Columns[Constants::PSettingsT_Index_DataENC_TagSelectionMode] = "TEXT";
    
    // Create temporary table
    if (!m_dbManager.createTable("persistentSettingsTable_temp", v2Columns)) {
        qWarning() << "Failed to create temporary table for rollback:" << m_dbManager.lastError();
        return false;
    }
    
    // Build column list for copying (v2 columns only)
    QStringList columnsList;
    columnsList << "id";
    for (auto it = v2Columns.begin(); it != v2Columns.end(); ++it) {
        if (it.key() != "id") {
            columnsList << it.key();
        }
    }
    QString columns = columnsList.join(", ");
    
    // Copy data from current table to temporary table
    QString sql = QString("INSERT INTO persistentSettingsTable_temp (%1) SELECT %1 FROM persistentSettingsTable")
                  .arg(columns);
    if (!m_dbManager.executeQuery(sql)) {
        qWarning() << "Failed to copy data to temporary table:" << m_dbManager.lastError();
        m_dbManager.dropTable("persistentSettingsTable_temp");
        return false;
    }
    
    // Drop the current table
    if (!m_dbManager.dropTable("persistentSettingsTable")) {
        qWarning() << "Failed to drop current table:" << m_dbManager.lastError();
        m_dbManager.dropTable("persistentSettingsTable_temp");
        return false;
    }
    
    // Rename temporary table to original name
    sql = "ALTER TABLE persistentSettingsTable_temp RENAME TO persistentSettingsTable";
    if (!m_dbManager.executeQuery(sql)) {
        qWarning() << "Failed to rename temporary table:" << m_dbManager.lastError();
        return false;
    }
    
    qDebug() << "DatabasePersistentSettingsManager: Successfully rolled back from v3";
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
