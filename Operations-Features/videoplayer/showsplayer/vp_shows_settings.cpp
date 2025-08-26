#include "vp_shows_settings.h"
#include "operations_files.h"
#include <QDir>
#include <QFile>
#include <QDebug>

VP_ShowsSettings::VP_ShowsSettings(const QByteArray& encryptionKey, const QString& username)
    : m_encryptionKey(encryptionKey)
    , m_username(username)
{
    qDebug() << "VP_ShowsSettings: Initialized with username:" << m_username;
}

VP_ShowsSettings::~VP_ShowsSettings()
{
}

bool VP_ShowsSettings::loadShowSettings(const QString& showFolderPath, ShowSettings& settings)
{
    qDebug() << "VP_ShowsSettings: Loading settings for show folder:" << showFolderPath;
    
    // Validate folder path
    if (showFolderPath.isEmpty()) {
        qDebug() << "VP_ShowsSettings: Show folder path is empty";
        settings = ShowSettings(); // Use defaults
        return true; // Not an error, just use defaults
    }
    
    // Check if the folder exists
    QDir showDir(showFolderPath);
    if (!showDir.exists()) {
        qDebug() << "VP_ShowsSettings: Show folder does not exist:" << showFolderPath;
        settings = ShowSettings(); // Use defaults
        return true; // Not an error, just use defaults
    }
    
    // Generate the settings filename
    QString settingsFileName = generateSettingsFileName(showFolderPath);
    QString settingsFilePath = QDir(showFolderPath).absoluteFilePath(settingsFileName);
    
    qDebug() << "VP_ShowsSettings: Settings file path:" << settingsFilePath;
    
    // Check if the settings file exists
    if (!QFile::exists(settingsFilePath)) {
        qDebug() << "VP_ShowsSettings: Settings file does not exist, using defaults";
        // File doesn't exist, use default settings
        settings = ShowSettings(); // Initialize with defaults
        return true; // Not an error, just no saved settings yet
    }
    
    // Read and decrypt the settings file
    QString settingsData;
    if (!OperationsFiles::readEncryptedFile(settingsFilePath, m_encryptionKey, settingsData)) {
        qDebug() << "VP_ShowsSettings: Failed to read encrypted settings file";
        return false;
    }
    
    // Deserialize the settings
    if (!deserializeSettings(settingsData, settings)) {
        qDebug() << "VP_ShowsSettings: Failed to deserialize settings";
        return false;
    }
    
    qDebug() << "VP_ShowsSettings: Successfully loaded settings - ShowName:"
             << settings.showName << "SkipIntro:" 
             << settings.skipIntro << "SkipOutro:" << settings.skipOutro 
             << "Autoplay:" << settings.autoplay << "UseTMDB:" << settings.useTMDB
             << "AutoFullscreen:" << settings.autoFullscreen;
    return true;
}

bool VP_ShowsSettings::saveShowSettings(const QString& showFolderPath, const ShowSettings& settings)
{
    qDebug() << "VP_ShowsSettings: Saving settings for show folder:" << showFolderPath;
    qDebug() << "VP_ShowsSettings: Settings - ShowName:" << settings.showName
             << "SkipIntro:" << settings.skipIntro 
             << "SkipOutro:" << settings.skipOutro 
             << "Autoplay:" << settings.autoplay << "UseTMDB:" << settings.useTMDB
             << "AutoFullscreen:" << settings.autoFullscreen;
    
    // Validate folder path
    if (showFolderPath.isEmpty()) {
        qDebug() << "VP_ShowsSettings: Show folder path is empty";
        return false;
    }
    
    // Check if the folder exists
    QDir showDir(showFolderPath);
    if (!showDir.exists()) {
        qDebug() << "VP_ShowsSettings: Show folder does not exist:" << showFolderPath;
        return false;
    }
    
    // Generate the settings filename
    QString settingsFileName = generateSettingsFileName(showFolderPath);
    QString settingsFilePath = QDir(showFolderPath).absoluteFilePath(settingsFileName);
    
    qDebug() << "VP_ShowsSettings: Settings file path:" << settingsFilePath;
    
    // Serialize the settings
    QString settingsData = serializeSettings(settings);
    
    // Encrypt and save the settings file
    if (!OperationsFiles::writeEncryptedFile(settingsFilePath, m_encryptionKey, settingsData)) {
        qDebug() << "VP_ShowsSettings: Failed to write encrypted settings file";
        return false;
    }
    
    qDebug() << "VP_ShowsSettings: Successfully saved settings";
    return true;
}

bool VP_ShowsSettings::deleteShowSettings(const QString& showFolderPath)
{
    qDebug() << "VP_ShowsSettings: Deleting settings for show folder:" << showFolderPath;
    
    // Generate the settings filename
    QString settingsFileName = generateSettingsFileName(showFolderPath);
    QString settingsFilePath = QDir(showFolderPath).absoluteFilePath(settingsFileName);
    
    // Check if the file exists
    if (!QFile::exists(settingsFilePath)) {
        qDebug() << "VP_ShowsSettings: Settings file does not exist, nothing to delete";
        return true; // Not an error
    }
    
    // Delete the file
    if (!QFile::remove(settingsFilePath)) {
        qDebug() << "VP_ShowsSettings: Failed to delete settings file";
        return false;
    }
    
    qDebug() << "VP_ShowsSettings: Successfully deleted settings file";
    return true;
}

QString VP_ShowsSettings::generateSettingsFileName(const QString& showFolderPath) const
{
    // Generate the obfuscated folder name
    QDir showDir(showFolderPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showsettings_
    QString settingsFileName = QString("showsettings_%1").arg(obfuscatedName);
    
    return settingsFileName;
}

QString VP_ShowsSettings::serializeSettings(const ShowSettings& settings) const
{
    QString data;
    
    // Simple key-value format for settings
    data += QString("showName=%1\n").arg(settings.showName);
    data += QString("skipIntro=%1\n").arg(settings.skipIntro ? "true" : "false");
    data += QString("skipOutro=%1\n").arg(settings.skipOutro ? "true" : "false");
    data += QString("autoplay=%1\n").arg(settings.autoplay ? "true" : "false");
    data += QString("useTMDB=%1\n").arg(settings.useTMDB ? "true" : "false");
    data += QString("autoFullscreen=%1\n").arg(settings.autoFullscreen ? "true" : "false");
    
    // Add more settings here as needed in the future
    
    return data;
}

bool VP_ShowsSettings::deserializeSettings(const QString& data, ShowSettings& settings) const
{
    // Parse the key-value pairs
    QStringList lines = data.split('\n', Qt::SkipEmptyParts);
    
    for (const QString& line : lines) {
        QStringList parts = line.split('=');
        if (parts.size() != 2) {
            continue; // Skip invalid lines
        }
        
        QString key = parts[0].trimmed();
        QString value = parts[1].trimmed();
        
        if (key == "showName") {
            settings.showName = value;
        } else if (key == "skipIntro") {
            settings.skipIntro = (value == "true");
        } else if (key == "skipOutro") {
            settings.skipOutro = (value == "true");
        } else if (key == "autoplay") {
            settings.autoplay = (value == "true");
        } else if (key == "useTMDB") {
            settings.useTMDB = (value == "true");
        } else if (key == "autoFullscreen") {
            settings.autoFullscreen = (value == "true");
        }
        // Add more settings parsing here as needed
    }
    
    return true;
}
