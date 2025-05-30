#ifndef SQLITE_DATABASE_AUTH_H
#define SQLITE_DATABASE_AUTH_H

#include "sqlite-database-handler.h"
#include <QString>
#include <QByteArray>
#include <functional>
#include "../constants.h"

class DatabaseAuthManager
{
public:
    // Singleton access method
    static DatabaseAuthManager& instance();

    // Delete copy constructor and assignment operator
    DatabaseAuthManager(const DatabaseAuthManager&) = delete;
    DatabaseAuthManager& operator=(const DatabaseAuthManager&) = delete;

    // Database connection management
    bool connect();
    bool isConnected() const;
    void close();

    // Database initialization and migration
    bool initializeVersioning();
    bool migrateAuthDatabase();

    // Transaction management (wrapper methods)
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // User management operations
    bool CreateUser(const QString& username, const QString& hashedPassword,
                    const QByteArray& encryptionKey, const QByteArray& salt,
                    const QString& displayName);
    bool UserExists(const QString& username);
    bool DeleteUser(const QString& username);

    // User data validation and access
    bool IndexIsValid(QString index, QString type);
    QString GetUserData_String(QString username, QString index);
    QByteArray GetUserData_ByteA(QString username, QString index);
    bool UpdateUserData_TEXT(QString username, QString index, QString data);
    bool UpdateUserData_BLOB(QString username, QString index, QByteArray data);

    // Get last error from underlying database manager
    QString lastError() const;

    // Get last insert ID
    int lastInsertId() const;

private:
    // Private constructor for singleton
    DatabaseAuthManager();
    ~DatabaseAuthManager();

    // Reference to the singleton database manager instance
    DatabaseManager& m_dbManager;

    // Auth-specific migration methods
    bool migrateToV2();
    bool migrateToV3();
    bool rollbackFromV2();
    bool rollbackFromV3();

    // Migration callback function for generic migration system
    bool authMigrationCallback(int version);
    bool authRollbackCallback(int version);

    // Latest version for auth database
    static const int LATEST_AUTH_VERSION = 3;
};

#endif // SQLITE_DATABASE_AUTH_H
