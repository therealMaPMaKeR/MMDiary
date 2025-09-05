// DatabaseManager.h
/* Thread-Safe Database Operations Usage Example:
 * 
 * // Example 1: Basic thread-safe query
 * DatabaseManager db;
 * db.connect("mydb.db");
 * 
 * // Returns thread-safe DatabaseResult
 * DatabaseResult result = db.select("users", 
 *                                   QStringList() << "id" << "name",
 *                                   "age > :age",
 *                                   {{":age", 18}});
 * 
 * // Thread-safe iteration
 * result.iterate([](const QMap<QString, QVariant>& row) {
 *     qDebug() << "User:" << row["name"].toString();
 * });
 * 
 * // Example 2: Concurrent access from multiple threads
 * QThread* thread1 = QThread::create([&db]() {
 *     DatabaseResult result = db.select("products");
 *     qDebug() << "Thread 1: Found" << result.size() << "products";
 * });
 * 
 * QThread* thread2 = QThread::create([&db]() {
 *     db.insert("products", {{"name", "New Product"}});
 *     qDebug() << "Thread 2: Inserted product";
 * });
 * 
 * thread1->start();
 * thread2->start();
 * 
 * // Example 3: Sharing results between threads
 * DatabaseResult sharedResult = db.select("orders");
 * 
 * // Multiple threads can safely access the same result
 * auto worker = [&sharedResult](int threadId) {
 *     sharedResult.iterate([threadId](const QMap<QString, QVariant>& row) {
 *         qDebug() << "Thread" << threadId << "processing order" << row["id"];
 *     });
 * };
 * 
 * // All operations are thread-safe!
 */

#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QDateTime>
#include <QMap>
#include <QVector>
#include <QMutex>
#include <QMutexLocker>
#include <memory>
#include <functional>
#include "../../ThreadSafeContainers.h"

/**
 * @class DatabaseResult
 * @brief Thread-safe wrapper for database query results
 * 
 * This class provides thread-safe access to database query results using ThreadSafeVector.
 * All access to the underlying data is protected by mutexes to prevent race conditions.
 * 
 * Thread Safety Guarantees:
 * - All read operations are protected by read locks
 * - All write operations are protected by write locks
 * - Iteration creates a local copy to prevent iterator invalidation
 * - Bounds checking is performed on all index-based access
 * 
 * Usage Example:
 * @code
 * DatabaseResult result = db.select("users");
 * 
 * // Thread-safe iteration
 * result.iterate([](const QMap<QString, QVariant>& row) {
 *     qDebug() << row["username"].toString();
 * });
 * 
 * // Thread-safe access by index
 * auto firstRow = result.at(0);
 * if (firstRow.has_value()) {
 *     // Process row data
 * }
 * 
 * // Get total count (thread-safe)
 * int count = result.size();
 * @endcode
 */
class DatabaseResult
{
public:
    using RowType = QMap<QString, QVariant>;
    using ContainerType = ThreadSafeVector<RowType>;
    
    /**
     * @brief Default constructor - creates empty result set
     */
    DatabaseResult() 
        : m_data(std::make_shared<ContainerType>(10000, "DatabaseResult")) {
        qDebug() << "DatabaseManager: Created empty DatabaseResult";
    }
    
    /**
     * @brief Constructor from existing data
     * @param data Vector of row data to wrap
     */
    explicit DatabaseResult(const QVector<RowType>& data)
        : m_data(std::make_shared<ContainerType>(10000, "DatabaseResult")) {
        if (!m_data->setContents(data)) {
            qWarning() << "DatabaseManager: Failed to set DatabaseResult contents - size exceeds limit";
        }
        qDebug() << "DatabaseManager: Created DatabaseResult with" << data.size() << "rows";
    }
    
    /**
     * @brief Get row at specific index (thread-safe)
     * @param index Row index
     * @return Optional containing row data if valid index
     */
    std::optional<RowType> at(int index) const {
        return m_data->at(index);
    }
    
    /**
     * @brief Get first row (thread-safe)
     * @return Optional containing first row if result set not empty
     */
    std::optional<RowType> first() const {
        return m_data->first();
    }
    
    /**
     * @brief Get last row (thread-safe)
     * @return Optional containing last row if result set not empty
     */
    std::optional<RowType> last() const {
        return m_data->last();
    }
    
    /**
     * @brief Get number of rows (thread-safe)
     * @return Number of rows in result set
     */
    int size() const {
        return m_data->size();
    }
    
    /**
     * @brief Check if result set is empty (thread-safe)
     * @return true if no rows, false otherwise
     */
    bool isEmpty() const {
        return m_data->isEmpty();
    }
    
    /**
     * @brief Thread-safe iteration over all rows
     * @param operation Function to apply to each row
     * 
     * Creates a local copy for iteration to prevent race conditions
     */
    void iterate(std::function<void(const RowType&)> operation) const {
        m_data->safeIterate(operation);
    }
    
    /**
     * @brief Thread-safe iteration with index
     * @param operation Function receiving index and row
     */
    void iterateWithIndex(std::function<void(int, const RowType&)> operation) const {
        m_data->safeIterateWithIndex(operation);
    }
    
    /**
     * @brief Get a thread-safe copy of all data
     * @return Vector containing copies of all rows
     */
    QVector<RowType> toVector() const {
        return m_data->getCopy();
    }
    
    /**
     * @brief Clear all results (thread-safe)
     */
    void clear() {
        m_data->clear();
    }
    
    /**
     * @brief Append a row to results (thread-safe)
     * @param row Row data to append
     * @return true if successful, false if would exceed size limit
     */
    bool append(const RowType& row) {
        return m_data->append(row);
    }
    
    /**
     * @brief Find first row matching predicate (thread-safe)
     * @param predicate Function that returns true for matching row
     * @return Optional containing matching row if found
     */
    std::optional<RowType> findFirst(std::function<bool(const RowType&)> predicate) const {
        return m_data->findFirst(predicate);
    }
    
    /**
     * @brief Find all rows matching predicate (thread-safe)
     * @param predicate Function that returns true for matching rows
     * @return Vector of matching rows
     */
    QVector<RowType> findAll(std::function<bool(const RowType&)> predicate) const {
        return m_data->findAll(predicate);
    }
    
private:
    std::shared_ptr<ContainerType> m_data;
};

/**
 * @class DatabaseManager
 * @brief Thread-safe SQLite database manager
 * 
 * Thread Safety Policy:
 * - All public methods are thread-safe
 * - Internal database operations are protected by mutex
 * - Query results are returned as thread-safe DatabaseResult objects
 * - Transactions are thread-local (each thread manages its own transaction state)
 * 
 * Thread Safety Guidelines:
 * 1. Multiple threads can safely call any DatabaseManager method
 * 2. Each thread should manage its own transactions
 * 3. DatabaseResult objects can be safely shared between threads
 * 4. Connection management is centralized and thread-safe
 * 
 * Performance Considerations:
 * - Database operations are serialized (one at a time)
 * - Consider using connection pooling for high-concurrency scenarios
 * - Result sets are copied for thread-safe iteration
 */
class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    // Delete copy constructor and assignment operator
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // Database initialization and connection
    bool connect(const QString& dbPath = "database.db");
    bool isConnected() const;
    void close();
    
    // Security: Clear sensitive data from result sets
    static void clearSensitiveResults(QVector<QMap<QString, QVariant>>& results, const QStringList& sensitiveColumns);

    // Transaction handling
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Misc operations
    bool removeColumn(const QString& tableName, const QString& columnToRemove);

    // General query execution
    bool executeQuery(const QString& query);
    bool executeQuery(QSqlQuery& query);

    // Select operations (Thread-safe)
    /**
     * @brief Execute SELECT query and return thread-safe results
     * @param tableName Name of table to query
     * @param columns Columns to select (empty for all)
     * @param whereClause WHERE clause without "WHERE" keyword
     * @param whereBindValues Bind values for WHERE clause
     * @param orderBy ORDER BY columns
     * @param limit Maximum number of rows to return
     * @return Thread-safe DatabaseResult object
     * 
     * @thread_safety This method is thread-safe. Multiple threads can call simultaneously.
     *                The returned DatabaseResult is also thread-safe.
     */
    DatabaseResult select(const QString& tableName,
                         const QStringList& columns = QStringList(),
                         const QString& whereClause = QString(),
                         const QMap<QString, QVariant>& whereBindValues = QMap<QString, QVariant>(),
                         const QStringList& orderBy = QStringList(),
                         int limit = -1);
    
    /**
     * @brief Execute SELECT query and return raw results (non-thread-safe)
     * @deprecated Use select() instead for thread-safe access
     * 
     * This method is provided for backward compatibility only.
     * The returned QVector is NOT thread-safe.
     */
    QVector<QMap<QString, QVariant>> selectRaw(const QString& tableName,
                                                const QStringList& columns = QStringList(),
                                                const QString& whereClause = QString(),
                                                const QMap<QString, QVariant>& whereBindValues = QMap<QString, QVariant>(),
                                                const QStringList& orderBy = QStringList(),
                                                int limit = -1);

    // Insert operations
    bool insert(const QString& tableName, const QMap<QString, QVariant>& data);
    bool insertMultiple(const QString& tableName, const QVector<QMap<QString, QVariant>>& dataList);

    // Update operations
    bool update(const QString& tableName,
                const QMap<QString, QVariant>& data,
                const QString& whereClause,
                const QMap<QString, QVariant>& whereBindValues = QMap<QString, QVariant>());

    // Delete operations
    bool remove(const QString& tableName,
                const QString& whereClause,
                const QMap<QString, QVariant>& bindValues = QMap<QString, QVariant>());

    // Table operations
    bool tableExists(const QString& tableName);
    bool createTable(const QString& tableName, const QMap<QString, QString>& columnsWithTypes);
    bool dropTable(const QString& tableName);

    // Get last error
    QString lastError() const;

    // Get last inserted ID
    int lastInsertId() const;

    // Get the number of affected rows from the last operation
    int affectedRows() const;

    // Generic database versioning and migration system
    bool initializeVersioning();
    int getCurrentVersion();
    bool updateVersion(int newVersion);

    // Generic migration methods with callback support
    bool migrateDatabase(int latestVersion,
                         std::function<bool(int)> migrationCallback,
                         std::function<bool(int)> rollbackCallback = nullptr);
    bool rollbackToVersion(int targetVersion,
                           std::function<bool(int)> rollbackCallback);

    // Backup methods
    bool backupDatabase(const QString& backupPath = "");
    bool restoreFromBackup(const QString& backupPath = "");
    
    // Database integrity methods
    bool verifyDatabaseIntegrity();
    bool enableIntegrityCheck();

private:
    // Thread safety mutex for all database operations
    mutable QMutex m_mutex;
    
    // Database instance
    QSqlDatabase m_db;

    // Last error message (protected by mutex)
    QString m_lastError;
    
    // Security: Maximum result set size to prevent memory exhaustion
    static const int MAX_RESULT_SIZE = 10000;
    
    /**
     * @brief Internal select implementation (must be called with mutex locked)
     */
    QVector<QMap<QString, QVariant>> selectInternal(const QString& tableName,
                                                     const QStringList& columns,
                                                     const QString& whereClause,
                                                     const QMap<QString, QVariant>& whereBindValues,
                                                     const QStringList& orderBy,
                                                     int limit);
};

#endif // DATABASEMANAGER_H
