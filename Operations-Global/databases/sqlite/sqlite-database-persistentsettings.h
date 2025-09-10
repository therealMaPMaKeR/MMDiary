#ifndef SQLITE_DATABASE_PERSISTENTSETTINGS_H
#define SQLITE_DATABASE_PERSISTENTSETTINGS_H

#include "sqlite-database-handler.h"
#include "../encryption/CryptoUtils.h"
#include <QString>
#include <QByteArray>
#include <functional>
#include "../../constants.h"

class DatabasePersistentSettingsManager
{
public:
    // Singleton access method
    static DatabasePersistentSettingsManager& instance();

    // Delete copy constructor and assignment operator
    DatabasePersistentSettingsManager(const DatabasePersistentSettingsManager&) = delete;
    DatabasePersistentSettingsManager& operator=(const DatabasePersistentSettingsManager&) = delete;

    // Database connection management
    bool connect(const QString& username, const QByteArray& encryptionKey);
    bool isConnected() const;
    void close();

    // Database initialization and migration
    bool initializeVersioning();
    bool migratePersistentSettingsDatabase();

    // Create/recreate persistent settings database (silent corruption recovery)
    bool createOrRecreatePersistentSettingsDatabase(const QString& username, const QByteArray& encryptionKey);

    // Transaction management (wrapper methods)
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Persistent settings data access
    bool IndexIsValid(QString index, QString type);
    QString GetPersistentSettingsData_String(QString index);
    QByteArray GetPersistentSettingsData_ByteA(QString index);
    int GetPersistentSettingsData_Int(QString index);
    bool UpdatePersistentSettingsData_TEXT(QString index, QString data);
    bool UpdatePersistentSettingsData_BLOB(QString index, QByteArray data);
    bool UpdatePersistentSettingsData_INT(QString index, int data);

    // Validation and corruption detection
    bool validateEncryptionKey();
    bool isDatabaseValid();

    // Get last error from underlying database manager
    QString lastError() const;

    // Get last insert ID
    int lastInsertId() const;

private:
    DatabasePersistentSettingsManager();
    ~DatabasePersistentSettingsManager();

    DatabaseManager m_dbManager;

    // Current user info
    QString m_currentUsername;
    QByteArray m_encryptionKey;

    // Persistent settings-specific migration methods
    bool migrateToV2();
    bool migrateToV3();
    // Add more migration methods as needed

    bool rollbackFromV2();
    bool rollbackFromV3();
    // Add more rollback methods as needed

    // Migration callback function for generic migration system
    bool persistentSettingsMigrationCallback(int version);
    bool persistentSettingsRollbackCallback(int version);

    // Latest version for persistent settings database
    static const int LATEST_PERSISTENT_SETTINGS_VERSION = 3;

    // Helper methods
    QString getPersistentSettingsDatabasePath(const QString& username);
    bool ensurePersistentSettingsRecord();
};

#endif // SQLITE_DATABASE_PERSISTENTSETTINGS_H
