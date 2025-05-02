// DatabaseManager.cpp
#include <QDir>
#include <QFileInfo>
#include "sqlite-database-handler.h"
#include<QFile>
#include "../constants.h"

DatabaseManager::DatabaseManager()
{
    // Ensure SQLite driver is available
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        qCritical() << "SQLite driver not available";
    }
}

DatabaseManager::~DatabaseManager()
{
    close();
}

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::connect(const QString& dbPath)
{
    // Close any existing connection
    if (m_db.isOpen()) {
        m_db.close();
    }
    
    // Create database connection with a unique connection name
    m_db = QSqlDatabase::addDatabase("QSQLITE", "MAIN_CONNECTION");
    m_db.setDatabaseName(dbPath);
    
    bool success = m_db.open();
    if (!success) {
        m_lastError = m_db.lastError().text();
        qWarning() << "Failed to connect to database:" << m_lastError;
    } else {
        // Enable foreign keys
        QSqlQuery query(m_db);
        query.exec("PRAGMA foreign_keys = ON");
    }
    
    return success;
}

bool DatabaseManager::isConnected() const
{
    return m_db.isOpen();
}

void DatabaseManager::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase("MAIN_CONNECTION");
}

bool DatabaseManager::beginTransaction()
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    bool success = m_db.transaction();
    if (!success) {
        m_lastError = m_db.lastError().text();
    }
    return success;
}

bool DatabaseManager::commitTransaction()
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    bool success = m_db.commit();
    if (!success) {
        m_lastError = m_db.lastError().text();
    }
    return success;
}

bool DatabaseManager::rollbackTransaction()
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    bool success = m_db.rollback();
    if (!success) {
        m_lastError = m_db.lastError().text();
    }
    return success;
}

bool DatabaseManager::executeQuery(const QString& query)
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    QSqlQuery sqlQuery(m_db);
    bool success = sqlQuery.exec(query);
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "Query failed:" << m_lastError;
        qWarning() << "Query was:" << query;
    }
    return success;
}

bool DatabaseManager::executeQuery(QSqlQuery& query)
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    bool success = query.exec();
    if (!success) {
        m_lastError = query.lastError().text();
        qWarning() << "Query failed:" << m_lastError;
        qWarning() << "Query was:" << query.lastQuery();
    }
    return success;
}

QVector<QMap<QString, QVariant>> DatabaseManager::select(const QString& tableName,
                                                         const QStringList& columns,
                                                         const QString& whereClause,
                                                         const QMap<QString, QVariant>& whereBindValues,
                                                         const QStringList& orderBy,
                                                         int limit)
{
    QVector<QMap<QString, QVariant>> results;

    if (!isConnected()) {
        m_lastError = "Database not connected";
        return results;
    }

    QString query = "SELECT ";
    query += columns.isEmpty() ? "*" : columns.join(", ");
    query += " FROM " + tableName;

    if (!whereClause.isEmpty()) {
        query += " WHERE " + whereClause;
    }

    if (!orderBy.isEmpty()) {
        query += " ORDER BY " + orderBy.join(", ");
    }

    if (limit > 0) {
        query += " LIMIT " + QString::number(limit);
    }

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = whereBindValues.constBegin(); it != whereBindValues.constEnd(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }

    if (sqlQuery.exec()) {
        QSqlRecord record = sqlQuery.record();
        while (sqlQuery.next()) {
            QMap<QString, QVariant> row;
            for (int i = 0; i < record.count(); ++i) {
                row[record.fieldName(i)] = sqlQuery.value(i);
            }
            results.append(row);
        }
    } else {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "Select query failed:" << m_lastError;
        qWarning() << "Query was:" << query;
    }

    return results;
}

bool DatabaseManager::insert(const QString& tableName, const QMap<QString, QVariant>& data)
{
    if (!isConnected() || data.isEmpty()) {
        m_lastError = !isConnected() ? "Database not connected" : "No data to insert";
        return false;
    }

    QStringList columns = data.keys();
    QStringList placeholders;
    for (const QString& col : columns) {
        placeholders.append(":" + col);
    }

    QString query = QString("INSERT INTO %1 (%2) VALUES (%3)")
                        .arg(tableName, columns.join(", "), placeholders.join(", "));

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        sqlQuery.bindValue(":" + it.key(), it.value());
    }

    return executeQuery(sqlQuery);
}

bool DatabaseManager::insertMultiple(const QString& tableName, const QVector<QMap<QString, QVariant>>& dataList)
{
    if (!isConnected() || dataList.isEmpty()) {
        m_lastError = !isConnected() ? "Database not connected" : "No data to insert";
        return false;
    }
    
    beginTransaction();
    bool success = true;
    
    for (const auto& data : dataList) {
        if (!insert(tableName, data)) {
            success = false;
            break;
        }
    }
    
    if (success) {
        commitTransaction();
    } else {
        rollbackTransaction();
    }
    
    return success;
}

bool DatabaseManager::update(const QString& tableName,
                             const QMap<QString, QVariant>& data,
                             const QString& whereClause,
                             const QMap<QString, QVariant>& whereBindValues)
{
    if (!isConnected() || data.isEmpty()) {
        m_lastError = !isConnected() ? "Database not connected" : "No data to update";
        return false;
    }

    QStringList setList;
    for (const QString& key : data.keys()) {
        setList.append(key + " = :" + key);
    }

    QString query = QString("UPDATE %1 SET %2").arg(tableName, setList.join(", "));
    if (!whereClause.isEmpty()) {
        query += " WHERE " + whereClause;
    }

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        sqlQuery.bindValue(":" + it.key(), it.value());
    }
    for (auto it = whereBindValues.constBegin(); it != whereBindValues.constEnd(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }

    return executeQuery(sqlQuery);
}

bool DatabaseManager::remove(const QString& tableName,
                             const QString& whereClause,
                             const QMap<QString, QVariant>& bindValues)
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    QString query = "DELETE FROM " + tableName;
    if (!whereClause.isEmpty()) {
        query += " WHERE " + whereClause;
    }

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = bindValues.constBegin(); it != bindValues.constEnd(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }

    return executeQuery(sqlQuery);
}

bool DatabaseManager::tableExists(const QString& tableName)
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    QSqlQuery query(m_db);
    query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=:name");
    query.bindValue(":name", tableName);
    
    if (query.exec() && query.next()) {
        return true;
    }
    
    m_lastError = query.lastError().text();
    return false;
}

bool DatabaseManager::createTable(const QString& tableName, const QMap<QString, QString>& columnsWithTypes)
{
    if (!isConnected() || columnsWithTypes.isEmpty()) {
        m_lastError = !isConnected() ? "Database not connected" : "No columns specified";
        return false;
    }
    
    QStringList columnDefs;
    for (auto it = columnsWithTypes.constBegin(); it != columnsWithTypes.constEnd(); ++it) {
        columnDefs.append(QString("%1 %2").arg(it.key(), it.value()));
    }
    
    QString query = QString("CREATE TABLE IF NOT EXISTS %1 (%2)")
                    .arg(tableName, columnDefs.join(", "));
    
    return executeQuery(query);
}

bool DatabaseManager::dropTable(const QString& tableName)
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }
    
    QString query = "DROP TABLE IF EXISTS " + tableName;
    return executeQuery(query);
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

int DatabaseManager::lastInsertId() const
{
    if (!isConnected()) {
        return -1;
    }
    
    QSqlQuery query(m_db);
    query.exec("SELECT last_insert_rowid()");
    if (query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

int DatabaseManager::affectedRows() const
{
    if (!isConnected()) {
        return -1;
    }
    
    QSqlQuery query(m_db);
    query.exec("SELECT changes()");
    if (query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

QString DatabaseManager::buildInsertQuery(const QString& tableName, const QMap<QString, QVariant>& data)
{
    QStringList columns;
    QStringList values;
    
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        columns.append(it.key());
        values.append(formatValue(it.value()));
    }
    
    return QString("INSERT INTO %1 (%2) VALUES (%3)")
           .arg(tableName, columns.join(", "), values.join(", "));
}

QString DatabaseManager::buildUpdateQuery(const QString& tableName, 
                                        const QMap<QString, QVariant>& data, 
                                        const QString& whereClause)
{
    QStringList setList;
    
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        setList.append(QString("%1 = %2").arg(it.key(), formatValue(it.value())));
    }
    
    QString query = QString("UPDATE %1 SET %2").arg(tableName, setList.join(", "));
    
    if (!whereClause.isEmpty()) {
        query += " WHERE " + whereClause;
    }
    
    return query;
}

QString DatabaseManager::formatValue(const QVariant& value)
{
    switch (value.type()) {
        case QVariant::String:
            return QString("'%1'").arg(value.toString().replace("'", "''"));
        case QVariant::DateTime:
            return QString("'%1'").arg(value.toDateTime().toString(Qt::ISODate));
        case QVariant::Bool:
            return value.toBool() ? "1" : "0";
        case QVariant::Int:
        case QVariant::Double:
            return value.toString();
        case QVariant::ByteArray:
            // For binary data, use X'...' syntax with hex representation
            return QString("X'%1'").arg(QString(value.toByteArray().toHex()));
        default:
            if (value.isNull()) {
                return "NULL";
            }
            return QString("'%1'").arg(value.toString().replace("'", "''"));
    }
}

// Add these implementations to sqlite-database-impl.cpp

bool DatabaseManager::initializeVersioning() {
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    // Create a version table if it doesn't exist
    QMap<QString, QString> versionTableColumns;
    versionTableColumns["id"] = "INTEGER PRIMARY KEY AUTOINCREMENT";
    versionTableColumns["version"] = "INTEGER NOT NULL";
    versionTableColumns["applied_at"] = "TIMESTAMP DEFAULT CURRENT_TIMESTAMP";
    versionTableColumns["description"] = "TEXT";

    if (!createTable("db_version", versionTableColumns)) {
        qWarning() << "Failed to create version table:" << lastError();
        return false;
    }

    // Check if we need to insert initial version
    QVector<QMap<QString, QVariant>> results = select("db_version");
    if (results.isEmpty()) {
        // Insert initial version (1)
        QMap<QString, QVariant> versionData;
        versionData["version"] = 1;
        versionData["description"] = "Initial database schema";
        if (!insert("db_version", versionData)) {
            qWarning() << "Failed to insert initial version:" << lastError();
            return false;
        }
    }

    return true;
}

int DatabaseManager::getCurrentVersion()
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return 0;
    }

    // Create version table if it doesn't exist
    if (!tableExists("db_version")) {
        if (!initializeVersioning()) {
            return 0;
        }
    }

    QVector<QMap<QString, QVariant>> results = select("db_version", QStringList() << "version", "", QMap<QString, QVariant>(), QStringList() << "version DESC", 1);
    if (!results.isEmpty()) {
        return results.first()["version"].toInt();
    }
    return 0; // No version found
}

bool DatabaseManager::updateVersion(int newVersion) {
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    QMap<QString, QVariant> versionData;
    versionData["version"] = newVersion;
    versionData["description"] = QString("Migration to version %1").arg(newVersion);
    return insert("db_version", versionData);
}

bool DatabaseManager::migrateDatabase() {
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    int currentVersion = getCurrentVersion();
    qInfo() << "Current database version:" << currentVersion;

    // Initialize the versioning system if needed
    if (currentVersion == 0) {
        if (!initializeVersioning()) {
            qWarning() << "Failed to initialize versioning system";
            return false;
        }
        currentVersion = 1; // After initialization, we're at version 1
    }

    // Get the latest available version (based on available migration methods)
    latestVersion = 2; // Update this as you add more migrations

    if (currentVersion >= latestVersion) {
        qInfo() << "Database is already at the latest version:" << currentVersion;
        return true;
    }

    // Start a transaction for all migrations
    beginTransaction();
    bool success = true;

    // Apply migrations in order
    for (int version = currentVersion + 1; version <= latestVersion; ++version) {
        qInfo() << "Migrating to version" << version;

        if (!migrateToVersion(version)) {
            qWarning() << "Failed to migrate to version" << version;
            success = false;
            break;
        }
    }

    if (success) {
        commitTransaction();
        qInfo() << "Database successfully migrated to version" << latestVersion;
        return true;
    } else {
        rollbackTransaction();
        qWarning() << "Database migration failed, rolled back to version" << currentVersion;
        return false;
    }
}

bool DatabaseManager::rollbackToVersion(int targetVersion) {
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    int currentVersion = getCurrentVersion();

    if (targetVersion >= currentVersion) {
        qWarning() << "Cannot rollback to version" << targetVersion << "as current version is" << currentVersion;
        return false;
    }

    if (targetVersion < 1) {
        qWarning() << "Cannot rollback to version below 1";
        return false;
    }

    // Create a backup before attempting rollback
    if (!backupDatabase()) {
        qWarning() << "Failed to create backup before rollback";
        return false;
    }

    // Start a transaction for the entire rollback process
    beginTransaction();
    bool success = true;

    // Roll back each version in reverse order
    for (int version = currentVersion; version > targetVersion; --version) {
        qInfo() << "Rolling back from version" << version;

        if (!rollbackFromVersion(version)) {
            qWarning() << "Failed to roll back from version" << version;
            success = false;
            break;
        }

        // Remove version entry from db_version table
        QString whereClause = QString("version = %1").arg(version);
        if (!remove("db_version", whereClause)) {
            qWarning() << "Failed to remove version" << version << "from db_version table";
            success = false;
            break;
        }
    }

    if (success) {
        commitTransaction();
        qInfo() << "Database successfully rolled back to version" << targetVersion;
        return true;
    } else {
        rollbackTransaction();
        qWarning() << "Database rollback failed, attempting to restore from backup";
        return restoreFromBackup();
    }
}

bool DatabaseManager::migrateToVersion(int version) {
    switch (version) {
    case 2:
        return migrateToV2();
    //case 3:
        //return migrateToV3();
    // Add cases for future versions
    default:
        m_lastError = QString("No migration defined for version %1").arg(version);
        return false;
    }
}

bool DatabaseManager::rollbackFromVersion(int version) {
    switch (version) {
    case 2:
        return rollbackFromV2();
    //case 3:
        //return rollbackFromV3();
    // Add cases for future versions
    default:
        m_lastError = QString("No rollback defined for version %1").arg(version);
        return false;
    }
}

// Migrate to v2, Technically the First Version.
bool DatabaseManager::migrateToV2() {
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

    if (!createTable("users", userTableColumns)) {
        m_lastError = QString("Failed to create users table: %1").arg(lastError());
        return false;
    }
    // Update version number
    return updateVersion(2);
}
/*
//Example migration: //dont forget to uncomment the function from the function migrateToVersion(), otherwise it wont migrate.
bool DatabaseManager::migrateToV3() {

    if (!executeQuery("ALTER TABLE users ADD COLUMN salt BLOB")) {
        m_lastError = QString("Failed to add salt column to users table: %1").arg(lastError());
        return false;
    }
    return updateVersion(3);
}
*/
// Example rollback: Remove users table. This should'nt ever happen, because v2 is basically first version. Just an example for future implementation.
bool DatabaseManager::rollbackFromV2() {
    // Drop the settings table
    if (!dropTable("users")) {
        m_lastError = QString("Failed to drop settings table: %1").arg(lastError());
        return false;
    }

    return true;
}
/*
// Example rollback: Remove categories
bool DatabaseManager::rollbackFromV3() {

    if (!removeColumn("users", "salt")) {
        qWarning() << "Failed to remove salt column:" << lastError();
    }
}
*/
bool DatabaseManager::backupDatabase(const QString& backupPath) {
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    // Generate backup file path if not provided
    QString backupFile = backupPath;
    if (backupFile.isEmpty()) {
        QDateTime now = QDateTime::currentDateTime();
        backupFile = m_db.databaseName() + "." + now.toString("yyyyMMdd_hhmmss") + ".bak";
    }

    // Close the current connection
    QString dbPath = m_db.databaseName();
    close();

    // Copy the database file
    bool success = QFile::copy(dbPath, backupFile);

    // Reconnect to the database
    connect(dbPath);

    if (!success) {
        m_lastError = "Failed to create database backup";
    }

    return success;
}

bool DatabaseManager::restoreFromBackup(const QString& backupPath) {
    if (backupPath.isEmpty()) {
        // Find the most recent backup
        QDir dir(QFileInfo(m_db.databaseName()).dir());
        QStringList filters;
        filters << QFileInfo(m_db.databaseName()).fileName() + ".*.bak";
        QStringList backups = dir.entryList(filters, QDir::Files, QDir::Time);

        if (backups.isEmpty()) {
            m_lastError = "No backup files found";
            return false;
        }

        // Close the connection
        QString dbPath = m_db.databaseName();
        close();

        // Replace the current database with the backup
        if (QFile::exists(dbPath)) {
            if (!QFile::remove(dbPath)) {
                m_lastError = "Failed to remove current database file";
                connect(dbPath); // Reconnect
                return false;
            }
        }

        bool success = QFile::copy(dir.filePath(backups.first()), dbPath);

        // Reconnect to the database
        connect(dbPath);

        if (!success) {
            m_lastError = "Failed to restore database from backup";
        }

        return success;
    } else {
        // Use the specified backup file
        if (!QFile::exists(backupPath)) {
            m_lastError = "Specified backup file does not exist";
            return false;
        }

        // Close the connection
        QString dbPath = m_db.databaseName();
        close();

        // Replace the current database with the backup
        if (QFile::exists(dbPath)) {
            if (!QFile::remove(dbPath)) {
                m_lastError = "Failed to remove current database file";
                connect(dbPath); // Reconnect
                return false;
            }
        }

        bool success = QFile::copy(backupPath, dbPath);

        // Reconnect to the database
        connect(dbPath);

        if (!success) {
            m_lastError = "Failed to restore database from backup";
        }

        return success;
    }
}

bool DatabaseManager::removeColumn(const QString& tableName, const QString& columnToRemove)
{
    if (!isConnected()) {
        m_lastError = "Database not connected";
        return false;
    }

    // Start a transaction
    if (!beginTransaction()) {
        return false;
    }

    // Get all columns except the one we want to remove
    QVector<QMap<QString, QVariant>> columns = select("pragma_table_info('" + tableName + "')");
    QStringList columnNames;
    QMap<QString, QString> columnDefinitions;
    QString primaryKey;

    for (const auto& column : columns) {
        QString columnName = column["name"].toString();
        if (columnName != columnToRemove) {
            columnNames.append(columnName);

            // Save column type for recreating the table
            QString type = column["type"].toString();

            // Handle primary key
            if (column["pk"].toInt() > 0) {
                primaryKey = columnName;
            }

            // Add nullability constraint
            if (column["notnull"].toInt() > 0) {
                type += " NOT NULL";
            }

            // Add default value if present
            if (column.contains("dflt_value") && !column["dflt_value"].isNull()) {
                type += " DEFAULT " + column["dflt_value"].toString();
            }

            columnDefinitions[columnName] = type;
        }
    }

    if (columnNames.isEmpty()) {
        m_lastError = "Failed to get column information or table would be empty after removing the column";
        rollbackTransaction();
        return false;
    }

    // Create temporary table
    QString tempTableName = tableName + "_temp";
    if (!createTable(tempTableName, columnDefinitions)) {
        rollbackTransaction();
        return false;
    }

    // Copy data to the temporary table
    QString copyQuery = "INSERT INTO " + tempTableName + " SELECT " + columnNames.join(", ") + " FROM " + tableName;
    if (!executeQuery(copyQuery)) {
        rollbackTransaction();
        return false;
    }

    // Drop original table
    if (!dropTable(tableName)) {
        rollbackTransaction();
        return false;
    }

    // Rename temporary table
    QString renameQuery = "ALTER TABLE " + tempTableName + " RENAME TO " + tableName;
    if (!executeQuery(renameQuery)) {
        rollbackTransaction();
        return false;
    }

    // Commit the transaction
    return commitTransaction();
}

bool DatabaseManager::IndexIsValid(QString index, QString type)
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

QString DatabaseManager::GetUserData_String(QString username, QString index)
{
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        return Constants::ErrorMessage_Default;
    }
    QStringList columns = {index};
    // Use COLLATE NOCASE for case-insensitive comparison in SQLite
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> bindValues = {{":username", username}};
    QVector<QMap<QString, QVariant>> results = select("users", columns, whereClause, bindValues);
    if (results.isEmpty()) {
        qDebug() << "User not found:" << username;
        return Constants::ErrorMessage_INVUSER;
    }
    return results.first()[index].toString();
}

QByteArray DatabaseManager::GetUserData_ByteA(QString username, QString index)
{
    qDebug() << "GetUserData_ByteA called for username:" << username << "index:" << index;

    // Assuming this follows a similar pattern
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        qDebug() << "Index is not valid for QByteArray:" << index;
        return QByteArray();
    }
    QStringList columns = {index};
    // Case-insensitive comparison
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> bindValues = {{":username", username}};
    QVector<QMap<QString, QVariant>> results = select("users", columns, whereClause, bindValues);
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

bool DatabaseManager::UpdateUserData_TEXT(QString username, QString index, QString data)
{
    // Validate index is for TEXT data
    if (!IndexIsValid(index, Constants::DataType_QString)) {
        m_lastError = "Invalid index for TEXT data: " + index;
        qWarning() << m_lastError;
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        if (!connect(Constants::DBPath_User)) {
            m_lastError = "Failed to connect to database";
            qWarning() << m_lastError;
            return false;
        }
    }

    // Check if column exists in the table
    QSqlQuery checkQuery(m_db);
    checkQuery.prepare("PRAGMA table_info(users)");
    bool columnExists = false;

    if (executeQuery(checkQuery)) {
        while (checkQuery.next()) {
            if (checkQuery.value("name").toString() == index) {
                columnExists = true;
                break;
            }
        }
    } else {
        return false; // Failed to execute query
    }

    // Add column if it doesn't exist
    if (!columnExists) {
        QString alterQuery = QString("ALTER TABLE users ADD COLUMN %1 TEXT").arg(index);
        if (!executeQuery(alterQuery)) {
            return false; // Failed to add column
        }
    }

    // Update user data
    QMap<QString, QVariant> updateData;
    updateData[index] = data;
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> whereBindValues;
    whereBindValues[":username"] = username;

    return update("users", updateData, whereClause, whereBindValues);
}

bool DatabaseManager::UpdateUserData_BLOB(QString username, QString index, QByteArray data)
{
    // Validate index is for BLOB data
    if (!IndexIsValid(index, Constants::DataType_QByteArray)) {
        m_lastError = "Invalid index for BLOB data: " + index;
        qWarning() << m_lastError;
        return false;
    }

    // Ensure database connection
    if (!isConnected()) {
        if (!connect(Constants::DBPath_User)) {
            m_lastError = "Failed to connect to database";
            qWarning() << m_lastError;
            return false;
        }
    }

    // Check if column exists in the table
    QSqlQuery checkQuery(m_db);
    checkQuery.prepare("PRAGMA table_info(users)");
    bool columnExists = false;

    if (executeQuery(checkQuery)) {
        while (checkQuery.next()) {
            if (checkQuery.value("name").toString() == index) {
                columnExists = true;
                break;
            }
        }
    } else {
        return false; // Failed to execute query
    }

    // Add column if it doesn't exist
    if (!columnExists) {
        QString alterQuery = QString("ALTER TABLE users ADD COLUMN %1 BLOB").arg(index);
        if (!executeQuery(alterQuery)) {
            return false; // Failed to add column
        }
    }

    // Update user data
    QMap<QString, QVariant> updateData;
    updateData[index] = data;
    QString whereClause = "LOWER(username) = LOWER(:username)";
    QMap<QString, QVariant> whereBindValues;
    whereBindValues[":username"] = username;

    return update("users", updateData, whereClause, whereBindValues);
}
