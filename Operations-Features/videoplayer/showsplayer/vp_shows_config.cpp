#include "vp_shows_config.h"
#include "../../Operations-Global/operations_files.h"
#include "../../encryption/CryptoUtils.h"
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
    
    // Use the app's Data folder for temp files, NOT the system temp folder
    // Use the temp folder directly without creating subfolders to ensure proper cleanup
    QString tempPath = QDir::cleanPath(QDir::current().path() + "/Data/" + username + "/temp");
    
    qDebug() << "VP_ShowsConfig: Using temp directory:" << tempPath;
    
    // Ensure directory exists with secure permissions
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
        qDebug() << "VP_ShowsConfig: Temp path is empty, cannot cleanup";
        return;
    }
    
    QDir tempDir(tempPath);
    if (!tempDir.exists()) {
        qDebug() << "VP_ShowsConfig: Temp directory doesn't exist:" << tempPath;
        return;
    }
    
    // Get all files in temp directory that match our TMDB patterns
    // This ensures we only clean up TMDB-related temp files
    QStringList nameFilters;
    nameFilters << "tmdb_*" << "temp_show_*" << "temp_episode_*";
    tempDir.setNameFilters(nameFilters);
    
    QStringList files = tempDir.entryList(QDir::Files);
    
    qDebug() << "VP_ShowsConfig: Found" << files.size() << "TMDB temp files to clean up in:" << tempPath;
    
    int successCount = 0;
    int failCount = 0;
    
    for (const QString& file : files) {
        QString filePath = QDir::cleanPath(tempPath + "/" + file);
        
        // Securely delete each file (using 1 pass for temp files, allowExternalFiles=false since it's in our Data folder)
        if (OperationsFiles::secureDelete(filePath, 1, false)) {
            successCount++;
            qDebug() << "VP_ShowsConfig: Deleted temp file:" << file;
        } else {
            failCount++;
            qDebug() << "VP_ShowsConfig: Failed to delete temp file:" << filePath;
        }
    }
    
    qDebug() << "VP_ShowsConfig: TMDB cleanup complete - Success:" << successCount << "Failed:" << failCount;
}
