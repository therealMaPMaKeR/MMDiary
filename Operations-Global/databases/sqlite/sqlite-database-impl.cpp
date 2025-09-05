// DatabaseManager.cpp
#include <QDir>
#include <QFileInfo>
#include "sqlite-database-handler.h"
#include<QFile>
#include "../../constants.h"
#include <cstring> // For secure memory clearing

// ============================================================================
// DatabaseManager Implementation
// ============================================================================
// Note: DatabaseResult methods are defined inline in the header file

DatabaseManager::DatabaseManager()
{
    // Ensure SQLite driver is available
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        qCritical() << "DatabaseManager: SQLite driver not available";
    }
    qDebug() << "DatabaseManager: Instance created";
}

DatabaseManager::~DatabaseManager()
{
    qDebug() << "DatabaseManager: Destroying instance";
    close();
}


bool DatabaseManager::connect(const QString& dbPath)
{
    QMutexLocker locker(&m_mutex);
    qDebug() << "DatabaseManager: Connecting to database:" << dbPath;
    
    // Close any existing connection
    if (m_db.isOpen()) {
        m_db.close();
    }

    // Create a unique connection name based on the database path
    QString connectionName = QString("CONNECTION_%1").arg(QString(dbPath).replace("/", "_").replace("\\", "_").replace(".", "_"));

    // Remove any existing connection with this name
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
    }

    // Create database connection with the unique connection name
    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(dbPath);

    bool success = m_db.open();
    if (!success) {
        m_lastError = m_db.lastError().text();
        qWarning() << "DatabaseManager: Failed to connect to database:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Successfully connected to database";
        // Enable security and integrity features
        enableIntegrityCheck();
        
        // Verify database integrity on connect
        if (!verifyDatabaseIntegrity()) {
            qWarning() << "DatabaseManager: Database integrity verification failed on connect";
            // Don't fail the connection, but log the warning
        }
    }

    return success;
}

bool DatabaseManager::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_db.isOpen();
}

void DatabaseManager::close()
{
    QMutexLocker locker(&m_mutex);
    qDebug() << "DatabaseManager: Closing database connection";
    
    QString connectionName;
    if (m_db.isOpen()) {
        connectionName = m_db.connectionName();
        m_db.close();
    }
    if (!connectionName.isEmpty() && QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
    }
}

void DatabaseManager::clearSensitiveResults(QVector<QMap<QString, QVariant>>& results, const QStringList& sensitiveColumns)
{
    // Clear sensitive data from result sets to prevent memory inspection attacks
    for (auto& row : results) {
        for (const QString& column : sensitiveColumns) {
            if (row.contains(column)) {
                QVariant& value = row[column];
                if (value.type() == QVariant::ByteArray) {
                    QByteArray data = value.toByteArray();
                    if (!data.isEmpty()) {
                        // Overwrite memory with zeros
                        std::memset(data.data(), 0, data.size());
                    }
                } else if (value.type() == QVariant::String) {
                    QString str = value.toString();
                    if (!str.isEmpty()) {
                        // Overwrite string data
                        std::fill(str.begin(), str.end(), QChar('\0'));
                    }
                }
                // Clear the variant
                value.clear();
            }
        }
    }
    // Clear the entire results vector
    results.clear();
}

bool DatabaseManager::beginTransaction()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot begin transaction - database not connected";
        return false;
    }

    bool success = m_db.transaction();
    if (!success) {
        m_lastError = m_db.lastError().text();
        qWarning() << "DatabaseManager: Failed to begin transaction:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Transaction started";
    }
    return success;
}

bool DatabaseManager::commitTransaction()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot commit transaction - database not connected";
        return false;
    }

    bool success = m_db.commit();
    if (!success) {
        m_lastError = m_db.lastError().text();
        qWarning() << "DatabaseManager: Failed to commit transaction:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Transaction committed";
    }
    return success;
}

bool DatabaseManager::rollbackTransaction()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot rollback transaction - database not connected";
        return false;
    }

    bool success = m_db.rollback();
    if (!success) {
        m_lastError = m_db.lastError().text();
        qWarning() << "DatabaseManager: Failed to rollback transaction:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Transaction rolled back";
    }
    return success;
}

bool DatabaseManager::executeQuery(const QString& query)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot execute query - database not connected";
        return false;
    }

    QSqlQuery sqlQuery(m_db);
    bool success = sqlQuery.exec(query);
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Query failed:" << m_lastError;
        qWarning() << "DatabaseManager: Query was:" << query;
    }
    return success;
}

bool DatabaseManager::executeQuery(QSqlQuery& query)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot execute query - database not connected";
        return false;
    }

    bool success = query.exec();
    if (!success) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: Query failed:" << m_lastError;
        qWarning() << "DatabaseManager: Query was:" << query.lastQuery();
    }
    return success;
}

DatabaseResult DatabaseManager::select(const QString& tableName,
                                       const QStringList& columns,
                                       const QString& whereClause,
                                       const QMap<QString, QVariant>& whereBindValues,
                                       const QStringList& orderBy,
                                       int limit)
{
    QMutexLocker locker(&m_mutex);
    qDebug() << "DatabaseManager: Executing SELECT on table:" << tableName;
    
    QVector<QMap<QString, QVariant>> results = selectInternal(tableName, columns, whereClause, whereBindValues, orderBy, limit);
    return DatabaseResult(results);
}

QVector<QMap<QString, QVariant>> DatabaseManager::selectRaw(const QString& tableName,
                                                            const QStringList& columns,
                                                            const QString& whereClause,
                                                            const QMap<QString, QVariant>& whereBindValues,
                                                            const QStringList& orderBy,
                                                            int limit)
{
    QMutexLocker locker(&m_mutex);
    qWarning() << "DatabaseManager: Using deprecated selectRaw() method - consider using select() for thread-safe access";
    
    return selectInternal(tableName, columns, whereClause, whereBindValues, orderBy, limit);
}

QVector<QMap<QString, QVariant>> DatabaseManager::selectInternal(const QString& tableName,
                                                                 const QStringList& columns,
                                                                 const QString& whereClause,
                                                                 const QMap<QString, QVariant>& whereBindValues,
                                                                 const QStringList& orderBy,
                                                                 int limit)
{
    // Note: This method assumes mutex is already locked by caller
    QVector<QMap<QString, QVariant>> results;

    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot execute select - database not connected";
        return results;
    }

    // Security: Enforce maximum result set size to prevent memory exhaustion
    const int MAX_RESULT_SIZE = 10000;
    if (limit <= 0 || limit > MAX_RESULT_SIZE) {
        limit = MAX_RESULT_SIZE;
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

    query += " LIMIT " + QString::number(limit);

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = whereBindValues.constBegin(); it != whereBindValues.constEnd(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }

    if (sqlQuery.exec()) {
        QSqlRecord record = sqlQuery.record();
        int rowCount = 0;
        while (sqlQuery.next() && rowCount < limit) {
            QMap<QString, QVariant> row;
            for (int i = 0; i < record.count(); ++i) {
                row[record.fieldName(i)] = sqlQuery.value(i);
            }
            results.append(row);
            rowCount++;
        }
        
        // Clear the query to release resources
        sqlQuery.finish();
        qDebug() << "DatabaseManager: SELECT query returned" << rowCount << "rows";
    } else {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Select query failed:" << m_lastError;
        qWarning() << "DatabaseManager: Query was:" << query;
    }

    return results;
}

bool DatabaseManager::insert(const QString& tableName, const QMap<QString, QVariant>& data)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen() || data.isEmpty()) {
        m_lastError = !m_db.isOpen() ? "Database not connected" : "No data to insert";
        qWarning() << "DatabaseManager: Cannot insert -" << m_lastError;
        return false;
    }
    
    qDebug() << "DatabaseManager: Inserting into table:" << tableName;

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

    bool success = sqlQuery.exec();
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Insert failed:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Insert successful";
    }
    return success;
}

bool DatabaseManager::insertMultiple(const QString& tableName, const QVector<QMap<QString, QVariant>>& dataList)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen() || dataList.isEmpty()) {
        m_lastError = !m_db.isOpen() ? "Database not connected" : "No data to insert";
        qWarning() << "DatabaseManager: Cannot insert multiple -" << m_lastError;
        return false;
    }
    
    qDebug() << "DatabaseManager: Inserting" << dataList.size() << "rows into table:" << tableName;

    // Use internal transaction methods to avoid nested locking
    bool transactionStarted = m_db.transaction();
    if (!transactionStarted) {
        qWarning() << "DatabaseManager: Failed to start transaction for batch insert";
        return false;
    }
    
    bool success = true;
    int insertedCount = 0;

    for (const auto& data : dataList) {
        // Prepare insert query without locking again
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

        if (!sqlQuery.exec()) {
            m_lastError = sqlQuery.lastError().text();
            qWarning() << "DatabaseManager: Batch insert failed at row" << insertedCount << ":" << m_lastError;
            success = false;
            break;
        }
        insertedCount++;
    }

    if (success) {
        m_db.commit();
        qDebug() << "DatabaseManager: Successfully inserted" << insertedCount << "rows";
    } else {
        m_db.rollback();
        qWarning() << "DatabaseManager: Rolled back batch insert after" << insertedCount << "rows";
    }

    return success;
}

bool DatabaseManager::update(const QString& tableName,
                             const QMap<QString, QVariant>& data,
                             const QString& whereClause,
                             const QMap<QString, QVariant>& whereBindValues)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen() || data.isEmpty()) {
        m_lastError = !m_db.isOpen() ? "Database not connected" : "No data to update";
        qWarning() << "DatabaseManager: Cannot update -" << m_lastError;
        return false;
    }
    
    qDebug() << "DatabaseManager: Updating table:" << tableName;

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

    bool success = sqlQuery.exec();
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Update failed:" << m_lastError;
    } else {
        int rowsAffected = sqlQuery.numRowsAffected();
        qDebug() << "DatabaseManager: Update successful, rows affected:" << rowsAffected;
    }
    return success;
}

bool DatabaseManager::remove(const QString& tableName,
                             const QString& whereClause,
                             const QMap<QString, QVariant>& bindValues)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot remove - database not connected";
        return false;
    }
    
    qDebug() << "DatabaseManager: Removing from table:" << tableName;

    QString query = "DELETE FROM " + tableName;
    if (!whereClause.isEmpty()) {
        query += " WHERE " + whereClause;
    }

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = bindValues.constBegin(); it != bindValues.constEnd(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }

    bool success = sqlQuery.exec();
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Remove failed:" << m_lastError;
    } else {
        int rowsAffected = sqlQuery.numRowsAffected();
        qDebug() << "DatabaseManager: Remove successful, rows affected:" << rowsAffected;
    }
    return success;
}

bool DatabaseManager::tableExists(const QString& tableName)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot check table existence - database not connected";
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
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen() || columnsWithTypes.isEmpty()) {
        m_lastError = !m_db.isOpen() ? "Database not connected" : "No columns specified";
        qWarning() << "DatabaseManager: Cannot create table -" << m_lastError;
        return false;
    }
    
    qDebug() << "DatabaseManager: Creating table:" << tableName;

    QStringList columnDefs;
    for (auto it = columnsWithTypes.constBegin(); it != columnsWithTypes.constEnd(); ++it) {
        columnDefs.append(QString("%1 %2").arg(it.key(), it.value()));
    }

    QString query = QString("CREATE TABLE IF NOT EXISTS %1 (%2)")
                        .arg(tableName, columnDefs.join(", "));

    QSqlQuery sqlQuery(m_db);
    bool success = sqlQuery.exec(query);
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Create table failed:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Table created successfully";
    }
    return success;
}

bool DatabaseManager::dropTable(const QString& tableName)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot drop table - database not connected";
        return false;
    }
    
    qDebug() << "DatabaseManager: Dropping table:" << tableName;

    QString query = "DROP TABLE IF EXISTS " + tableName;
    QSqlQuery sqlQuery(m_db);
    bool success = sqlQuery.exec(query);
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Drop table failed:" << m_lastError;
    } else {
        qDebug() << "DatabaseManager: Table dropped successfully";
    }
    return success;
}

QString DatabaseManager::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

int DatabaseManager::lastInsertId() const
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        qWarning() << "DatabaseManager: Cannot get last insert ID - database not connected";
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
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        qWarning() << "DatabaseManager: Cannot get affected rows - database not connected";
        return -1;
    }

    QSqlQuery query(m_db);
    query.exec("SELECT changes()");
    if (query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

bool DatabaseManager::verifyDatabaseIntegrity()
{
    // Note: This method assumes mutex is already locked by caller
    // or is called during connection setup
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot verify integrity - database not connected";
        return false;
    }

    QSqlQuery query(m_db);
    
    // Run SQLite's built-in integrity check
    if (!query.exec("PRAGMA integrity_check")) {
        m_lastError = "Failed to run integrity check: " + query.lastError().text();
        return false;
    }
    
    // Check results
    if (query.next()) {
        QString result = query.value(0).toString();
        if (result != "ok") {
            m_lastError = "Database integrity check failed: " + result;
            qWarning() << "DatabaseManager: Database integrity check failed:" << result;
            return false;
        }
    }
    
    query.finish();
    return true;
}

bool DatabaseManager::enableIntegrityCheck()
{
    // Note: This method assumes mutex is already locked by caller
    // or is called during connection setup
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot enable integrity check - database not connected";
        return false;
    }

    QSqlQuery query(m_db);
    
    // Enable foreign key constraints for integrity
    if (!query.exec("PRAGMA foreign_keys = ON")) {
        m_lastError = "Failed to enable foreign keys: " + query.lastError().text();
        return false;
    }
    
    // Set journal mode to WAL for better integrity and performance
    if (!query.exec("PRAGMA journal_mode = WAL")) {
        // WAL mode might fail on some systems, fallback to DELETE mode
        query.exec("PRAGMA journal_mode = DELETE");
    }
    
    // Enable secure delete to overwrite deleted data
    query.exec("PRAGMA secure_delete = ON");
    
    query.finish();
    return true;
}

// Generic migration system with callback support

bool DatabaseManager::initializeVersioning() {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot initialize versioning - database not connected";
        return false;
    }
    
    qDebug() << "DatabaseManager: Initializing database versioning";

    // Create a version table if it doesn't exist
    QMap<QString, QString> versionTableColumns;
    versionTableColumns["id"] = "INTEGER PRIMARY KEY AUTOINCREMENT";
    versionTableColumns["version"] = "INTEGER NOT NULL";
    versionTableColumns["applied_at"] = "TIMESTAMP DEFAULT CURRENT_TIMESTAMP";
    versionTableColumns["description"] = "TEXT";

    // Create table without recursive locking
    QStringList columnDefs;
    for (auto it = versionTableColumns.constBegin(); it != versionTableColumns.constEnd(); ++it) {
        columnDefs.append(QString("%1 %2").arg(it.key(), it.value()));
    }
    
    QString createQuery = QString("CREATE TABLE IF NOT EXISTS db_version (%1)")
                              .arg(columnDefs.join(", "));
    
    QSqlQuery sqlQuery(m_db);
    if (!sqlQuery.exec(createQuery)) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Failed to create version table:" << m_lastError;
        return false;
    }

    // Check if we need to insert initial version
    QVector<QMap<QString, QVariant>> results = selectInternal("db_version", QStringList(), "", QMap<QString, QVariant>(), QStringList(), -1);
    if (results.isEmpty()) {
        // Insert initial version (1)
        QMap<QString, QVariant> versionData;
        versionData["version"] = 1;
        versionData["description"] = "Initial database schema";
        // Insert without recursive locking
        QStringList columns = versionData.keys();
        QStringList placeholders;
        for (const QString& col : columns) {
            placeholders.append(":" + col);
        }
        
        QString insertQuery = QString("INSERT INTO db_version (%1) VALUES (%2)")
                                  .arg(columns.join(", "), placeholders.join(", "));
        
        QSqlQuery insertSql(m_db);
        insertSql.prepare(insertQuery);
        for (auto it = versionData.constBegin(); it != versionData.constEnd(); ++it) {
            insertSql.bindValue(":" + it.key(), it.value());
        }
        
        if (!insertSql.exec()) {
            m_lastError = insertSql.lastError().text();
            qWarning() << "DatabaseManager: Failed to insert initial version:" << m_lastError;
            return false;
        }
    }

    return true;
}

int DatabaseManager::getCurrentVersion()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot get current version - database not connected";
        return 0;
    }

    // Check if version table exists without recursive locking
    QSqlQuery checkQuery(m_db);
    checkQuery.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='db_version'");
    
    if (!checkQuery.exec() || !checkQuery.next()) {
        // Table doesn't exist, need to initialize
        locker.unlock();
        if (!initializeVersioning()) {
            return 0;
        }
        locker.relock();
    }

    QVector<QMap<QString, QVariant>> results = selectInternal("db_version", QStringList() << "version", "", QMap<QString, QVariant>(), QStringList() << "version DESC", 1);
    if (!results.isEmpty()) {
        return results.first()["version"].toInt();
    }
    return 0; // No version found
}

bool DatabaseManager::updateVersion(int newVersion) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot update version - database not connected";
        return false;
    }
    
    qDebug() << "DatabaseManager: Updating database version to" << newVersion;

    QMap<QString, QVariant> versionData;
    versionData["version"] = newVersion;
    versionData["description"] = QString("Migration to version %1").arg(newVersion);
    
    // Insert without recursive locking
    QStringList columns = versionData.keys();
    QStringList placeholders;
    for (const QString& col : columns) {
        placeholders.append(":" + col);
    }
    
    QString query = QString("INSERT INTO db_version (%1) VALUES (%2)")
                        .arg(columns.join(", "), placeholders.join(", "));
    
    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(query);
    for (auto it = versionData.constBegin(); it != versionData.constEnd(); ++it) {
        sqlQuery.bindValue(":" + it.key(), it.value());
    }
    
    bool success = sqlQuery.exec();
    if (!success) {
        m_lastError = sqlQuery.lastError().text();
        qWarning() << "DatabaseManager: Failed to update version:" << m_lastError;
    }
    return success;
}

bool DatabaseManager::migrateDatabase(int latestVersion,
                                      std::function<bool(int)> migrationCallback,
                                      std::function<bool(int)> rollbackCallback) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot migrate database - database not connected";
        return false;
    }

    // Get current version without recursive locking
    locker.unlock();
    int currentVersion = getCurrentVersion();
    locker.relock();
    
    qInfo() << "DatabaseManager: Current database version:" << currentVersion;

    // Initialize the versioning system if needed
    if (currentVersion == 0) {
        locker.unlock();
        if (!initializeVersioning()) {
            qWarning() << "DatabaseManager: Failed to initialize versioning system";
            return false;
        }
        locker.relock();
        currentVersion = 1; // After initialization, we're at version 1
    }

    if (currentVersion >= latestVersion) {
        qInfo() << "DatabaseManager: Database is already at the latest version:" << currentVersion;
        return true;
    }

    // Start a transaction for all migrations
    bool transactionStarted = m_db.transaction();
    if (!transactionStarted) {
        qWarning() << "DatabaseManager: Failed to start transaction for migration";
        return false;
    }
    
    bool success = true;

    // Apply migrations in order
    for (int version = currentVersion + 1; version <= latestVersion; ++version) {
        qInfo() << "DatabaseManager: Migrating to version" << version;

        // Unlock mutex for callback
        locker.unlock();
        bool migrationSuccess = migrationCallback(version);
        locker.relock();
        
        if (!migrationSuccess) {
            qWarning() << "DatabaseManager: Failed to migrate to version" << version;
            success = false;
            break;
        }

        // Update version number after successful migration
        locker.unlock();
        bool updateSuccess = updateVersion(version);
        locker.relock();
        
        if (!updateSuccess) {
            qWarning() << "DatabaseManager: Failed to update version to" << version;
            success = false;
            break;
        }
    }

    if (success) {
        m_db.commit();
        qInfo() << "DatabaseManager: Database successfully migrated to version" << latestVersion;
        return true;
    } else {
        m_db.rollback();
        qWarning() << "DatabaseManager: Database migration failed, rolled back to version" << currentVersion;
        return false;
    }
}

bool DatabaseManager::rollbackToVersion(int targetVersion,
                                        std::function<bool(int)> rollbackCallback) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot rollback - database not connected";
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

        if (!rollbackCallback(version)) {
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
    QMutexLocker locker(&m_mutex);
    
    if (!m_db.isOpen()) {
        m_lastError = "Database not connected";
        qWarning() << "DatabaseManager: Cannot remove column - database not connected";
        return false;
    }
    
    qDebug() << "DatabaseManager: Removing column" << columnToRemove << "from table" << tableName;

    // Try to start a transaction - if it fails, we're already in one
    bool weStartedTransaction = m_db.transaction();

    // Get all columns except the one we want to remove  
    QVector<QMap<QString, QVariant>> columns = selectInternal("pragma_table_info('" + tableName + "')", QStringList(), "", QMap<QString, QVariant>(), QStringList(), -1);
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
        qWarning() << "DatabaseManager: Cannot remove column - invalid column information";
        if (weStartedTransaction) {
            m_db.rollback();
        }
        return false;
    }

    // Create temporary table without recursive locking
    QString tempTableName = tableName + "_temp";
    QStringList columnDefs;
    for (auto it = columnDefinitions.constBegin(); it != columnDefinitions.constEnd(); ++it) {
        columnDefs.append(QString("%1 %2").arg(it.key(), it.value()));
    }
    
    QString createQuery = QString("CREATE TABLE IF NOT EXISTS %1 (%2)")
                              .arg(tempTableName, columnDefs.join(", "));
    
    QSqlQuery createSql(m_db);
    if (!createSql.exec(createQuery)) {
        m_lastError = createSql.lastError().text();
        qWarning() << "DatabaseManager: Failed to create temporary table:" << m_lastError;
        if (weStartedTransaction) {
            m_db.rollback();
        }
        return false;
    }

    // Copy data to the temporary table
    QString copyQuery = "INSERT INTO " + tempTableName + " SELECT " + columnNames.join(", ") + " FROM " + tableName;
    QSqlQuery copySql(m_db);
    if (!copySql.exec(copyQuery)) {
        m_lastError = copySql.lastError().text();
        qWarning() << "DatabaseManager: Failed to copy data to temporary table:" << m_lastError;
        if (weStartedTransaction) {
            m_db.rollback();
        }
        return false;
    }

    // Drop original table
    QString dropQuery = "DROP TABLE IF EXISTS " + tableName;
    QSqlQuery dropSql(m_db);
    if (!dropSql.exec(dropQuery)) {
        m_lastError = dropSql.lastError().text();
        qWarning() << "DatabaseManager: Failed to drop original table:" << m_lastError;
        if (weStartedTransaction) {
            m_db.rollback();
        }
        return false;
    }

    // Rename temporary table
    QString renameQuery = "ALTER TABLE " + tempTableName + " RENAME TO " + tableName;
    QSqlQuery renameSql(m_db);
    if (!renameSql.exec(renameQuery)) {
        m_lastError = renameSql.lastError().text();
        qWarning() << "DatabaseManager: Failed to rename temporary table:" << m_lastError;
        if (weStartedTransaction) {
            m_db.rollback();
        }
        return false;
    }

    // Commit the transaction only if we started it
    if (weStartedTransaction) {
        bool success = m_db.commit();
        if (success) {
            qDebug() << "DatabaseManager: Successfully removed column" << columnToRemove;
        } else {
            m_lastError = m_db.lastError().text();
            qWarning() << "DatabaseManager: Failed to commit column removal:" << m_lastError;
        }
        return success;
    }
    
    qDebug() << "DatabaseManager: Successfully removed column" << columnToRemove;
    return true;
}
