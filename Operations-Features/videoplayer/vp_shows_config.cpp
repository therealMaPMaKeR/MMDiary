#include "vp_shows_config.h"
#include "operations_files.h"
#include "CryptoUtils.h"
#include <QDir>
#include <QDebug>
#include <QStandardPaths>

QString VP_ShowsConfig::getTMDBApiKey()
{
    QSettings settings("MMDiary", "TVShows");
    
    // Get encrypted API key from settings
    QString encryptedKey = settings.value("TMDB/ApiKey", QString()).toString();
    
    if (encryptedKey.isEmpty()) {
        qDebug() << "VP_ShowsConfig: No TMDB API key configured";
        return QString();
    }
    
    // For production, you should decrypt the API key here
    // For now, returning as-is (you should implement proper encryption)
    // Example: return CryptoUtils::decryptString(encryptedKey, masterKey);
    
    return encryptedKey;
}

void VP_ShowsConfig::setTMDBApiKey(const QString& apiKey)
{
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsConfig: Cannot set empty API key";
        return;
    }
    
    QSettings settings("MMDiary", "TVShows");
    
    // For production, encrypt the API key before storing
    // Example: QString encryptedKey = CryptoUtils::encryptString(apiKey, masterKey);
    
    settings.setValue("TMDB/ApiKey", apiKey);
    settings.sync();
    
    qDebug() << "VP_ShowsConfig: TMDB API key saved";
}

bool VP_ShowsConfig::isTMDBEnabled()
{
    QSettings settings("MMDiary", "TVShows");
    return settings.value("TMDB/Enabled", true).toBool(); // Default to enabled
}

void VP_ShowsConfig::setTMDBEnabled(bool enabled)
{
    QSettings settings("MMDiary", "TVShows");
    settings.setValue("TMDB/Enabled", enabled);
    settings.sync();
    
    qDebug() << "VP_ShowsConfig: TMDB integration" << (enabled ? "enabled" : "disabled");
}

QString VP_ShowsConfig::getTempDirectory(const QString& username)
{
    if (username.isEmpty()) {
        qDebug() << "VP_ShowsConfig: Cannot get temp directory without username";
        return QString();
    }
    
    QString tempPath = QDir::tempPath() + "/" + username + "/tvshows";
    
    // Ensure directory exists
    if (!OperationsFiles::ensureDirectoryExists(tempPath)) {
        qDebug() << "VP_ShowsConfig: Failed to create temp directory:" << tempPath;
        return QString();
    }
    
    return tempPath;
}

void VP_ShowsConfig::cleanupTempDirectory(const QString& username)
{
    QString tempPath = getTempDirectory(username);
    
    if (tempPath.isEmpty()) {
        return;
    }
    
    QDir tempDir(tempPath);
    if (!tempDir.exists()) {
        return;
    }
    
    // Get all files in temp directory
    QStringList files = tempDir.entryList(QDir::Files);
    
    for (const QString& file : files) {
        QString filePath = tempPath + "/" + file;
        
        // Securely delete each file
        if (!OperationsFiles::secureDelete(filePath, 3)) {
            qDebug() << "VP_ShowsConfig: Failed to delete temp file:" << filePath;
        }
    }
    
    qDebug() << "VP_ShowsConfig: Cleaned up" << files.size() << "temp files";
}
