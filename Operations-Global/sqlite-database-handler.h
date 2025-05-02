// DatabaseManager.h
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

class DatabaseManager
{
public:
    // Singleton access method
    static DatabaseManager& instance();
    
    // Delete copy constructor and assignment operator
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    
    // Database initialization and connection
    bool connect(const QString& dbPath = "database.db");
    bool isConnected() const;
    void close();
    //Get User Data
    bool IndexIsValid(QString index, QString type); // check to make sure an index is valid, depending on the variable type a function returns
    QString GetUserData_String(QString username, QString index); // returns user data that is of type: QString
    QByteArray GetUserData_ByteA(QString username, QString index); // returns user data that is of type: QByteArray
    bool UpdateUserData_TEXT(QString username, QString index, QString data);
    bool UpdateUserData_BLOB(QString username, QString index, QByteArray data);
    // Transaction handling
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    //misc
    bool removeColumn(const QString& tableName, const QString& columnToRemove);

    // General query execution
    bool executeQuery(const QString& query);
    bool executeQuery(QSqlQuery& query);
    
    // Select operations
    QVector<QMap<QString, QVariant>> select(const QString& tableName,
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
    // Database versioning
    bool initializeVersioning();
    int getCurrentVersion();
    bool updateVersion(int newVersion);
    bool migrateDatabase();
    bool rollbackToVersion(int targetVersion);

    // Migration methods
    bool migrateToVersion(int version);
    bool rollbackFromVersion(int version);

    // Backup methods
    bool backupDatabase(const QString& backupPath = "");
    bool restoreFromBackup(const QString& backupPath = "");
    int latestVersion = 2; // update this as you add new versions
    bool rollbackFromV2();
    //bool rollbackFromV3();
    bool migrateToV2();
    //bool migrateToV3();
private:
    // Private constructor for singleton
    DatabaseManager();
    ~DatabaseManager();
    
    // Database instance
    QSqlDatabase m_db;
    
    // Last error message
    QString m_lastError;
    
    // Helper methods
    QString buildInsertQuery(const QString& tableName, const QMap<QString, QVariant>& data);
    QString buildUpdateQuery(const QString& tableName, 
                            const QMap<QString, QVariant>& data, 
                            const QString& whereClause);
    QString formatValue(const QVariant& value);
};

#endif // DATABASEMANAGER_H
