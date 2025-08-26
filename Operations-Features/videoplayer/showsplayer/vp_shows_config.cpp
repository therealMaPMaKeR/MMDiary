#include "vp_shows_config.h"
#include "../../Operations-Global/operations_files.h"
#include "../../Operations-Global/inputvalidation.h"
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>

// Include the TMDB API key header if it exists
// The key is defined in tmdb_api_key.h (created from tmdb_api_key_TEMPLATE.h)
#if __has_include("../../../tmdb_api_key.h")
    #include "../../../tmdb_api_key.h"
#endif

// Fallback if the header doesn't exist
#ifndef TMDB_API_KEY
    #define TMDB_API_KEY ""
#endif

QString VP_ShowsConfig::getTMDBApiKey()
{
    // Return the compile-time embedded API key
    QString apiKey = QString(TMDB_API_KEY);
    
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsConfig: No TMDB API key found";
        qDebug() << "VP_ShowsConfig: Please copy tmdb_api_key_TEMPLATE.h to tmdb_api_key.h and add your API key";
        return QString();
    }
    
    // Log key details for debugging (without exposing the actual key)
    qDebug() << "VP_ShowsConfig: API key found, length:" << apiKey.length() << "characters";
    if (apiKey.startsWith("Bearer ")) {
        qDebug() << "VP_ShowsConfig: Using Bearer token authentication";
    } else {
        qDebug() << "VP_ShowsConfig: Using API key authentication";
    }
    
    // Validate the API key format
    // Bearer tokens can be very long (200+ characters)
    // Allow up to 512 characters for Bearer tokens
    int maxLength = 512;
    
    InputValidation::ValidationResult validationResult = 
        InputValidation::validateInput(apiKey, InputValidation::InputType::PlainText, maxLength);
    
    if (!validationResult.isValid) {
        qDebug() << "VP_ShowsConfig: Invalid API key format:" << validationResult.errorMessage;
        qDebug() << "VP_ShowsConfig: Key length was:" << apiKey.length();
        return QString();
    }
    
    qDebug() << "VP_ShowsConfig: TMDB API key loaded successfully";
    return apiKey;
}

bool VP_ShowsConfig::hasApiKey()
{
    // Check if API key is defined in the header file
    QString apiKey = QString(TMDB_API_KEY);
    return !apiKey.isEmpty();
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
