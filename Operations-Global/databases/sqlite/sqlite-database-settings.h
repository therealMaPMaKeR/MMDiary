#ifndef SQLITE_DATABASE_SETTINGS_H
#define SQLITE_DATABASE_SETTINGS_H

#include "sqlite-database-handler.h"
#include "../encryption/CryptoUtils.h"
#include <QString>
#include <QByteArray>
#include <functional>
#include "../../constants.h"

class DatabaseSettingsManager
{
public:
    // Singleton access method
    static DatabaseSettingsManager& instance();

    // Delete copy constructor and assignment operator
    DatabaseSettingsManager(const DatabaseSettingsManager&) = delete;
    DatabaseSettingsManager& operator=(const DatabaseSettingsManager&) = delete;

    // Database connection management
    bool connect(const QString& username, const QByteArray& encryptionKey);
    bool isConnected() const;
    void close();

    // Database initialization and migration
    bool initializeVersioning();
    bool migrateSettingsDatabase();

    // Create/recreate settings database
    bool createOrRecreateSettingsDatabase(const QString& username, const QByteArray& encryptionKey);

    // Transaction management (wrapper methods)
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Settings data validation and access
    bool IndexIsValid(QString index, QString type);
    QString GetSettingsData_String(QString index);
    QByteArray GetSettingsData_ByteA(QString index);
    bool UpdateSettingsData_TEXT(QString index, QString data);
    bool UpdateSettingsData_BLOB(QString index, QByteArray data);

    // Get last error from underlying database manager
    QString lastError() const;

    // Get last insert ID
    int lastInsertId() const;

private:
    DatabaseSettingsManager();
    ~DatabaseSettingsManager();


    DatabaseManager m_dbManager;

    // Current user info
    QString m_currentUsername;
    QByteArray m_encryptionKey;

    // Settings-specific migration methods
    bool migrateToV2();
    bool migrateToV3();
    // Add more migration methods as needed

    bool rollbackFromV2();
    bool rollbackFromV3();
    // Add more rollback methods as needed

    // Migration callback function for generic migration system
    bool settingsMigrationCallback(int version);
    bool settingsRollbackCallback(int version);

    // Latest version for settings database
    static const int LATEST_SETTINGS_VERSION = 3;

    // Helper methods
    QString getSettingsDatabasePath(const QString& username);
    bool validateEncryptionKey();
    bool ensureSettingsRecord();
};

#endif // SQLITE_DATABASE_SETTINGS_H
