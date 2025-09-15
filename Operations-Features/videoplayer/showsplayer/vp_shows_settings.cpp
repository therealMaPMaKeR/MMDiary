#include "vp_shows_settings.h"
#include "operations_files.h"
#include "datastorage_field_manager.h"
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
    
    // Create the data storage field manager
    DataStorage_FieldManager fieldManager(m_encryptionKey, m_username);
    
    // Read and validate settings using the field manager
    QMap<QString, QVariant> settingsMap;
    DataStorage_FieldManager::ValidationResult result =
        fieldManager.readAndValidateData(settingsFilePath,
                                        DataStorage_FieldDefinitions::TVShowSettings,
                                        settingsMap);
    
    if (!result.success) {
        qDebug() << "VP_ShowsSettings: Failed to read/validate settings:" << result.errorMessage;
        settings = ShowSettings(); // Use defaults
        return true; // Still return true to use defaults instead of failing
    }
    
    // Log if settings were modified during validation
    if (result.wasModified) {
        qDebug() << "VP_ShowsSettings: Settings file was automatically repaired";
        if (!result.addedFields.isEmpty()) {
            qDebug() << "VP_ShowsSettings: Added missing fields:" << result.addedFields.join(", ");
        }
        if (!result.removedFields.isEmpty()) {
            qDebug() << "VP_ShowsSettings: Removed obsolete fields:" << result.removedFields.join(", ");
        }
    }
    
    // Convert QMap to ShowSettings struct
    convertMapToSettings(settingsMap, settings);
    
    qDebug() << "VP_ShowsSettings: Successfully loaded settings - ShowName:"
             << settings.showName << "ShowId:" << settings.showId << "SkipIntro:" 
             << settings.skipIntro << "SkipOutro:" << settings.skipOutro 
             << "Autoplay:" << settings.autoplay << "AutoplayRandom:" << settings.autoplayRandom
             << "UseTMDB:" << settings.useTMDB
             << "DisplayFileNames:" << settings.displayFileNames << "DisplayNewEpNotif:" << settings.DisplayNewEpNotif;
    return true;
}

bool VP_ShowsSettings::saveShowSettings(const QString& showFolderPath, const ShowSettings& settings)
{
    qDebug() << "VP_ShowsSettings: Saving settings for show folder:" << showFolderPath;
    qDebug() << "VP_ShowsSettings: Settings - ShowName:" << settings.showName
             << "ShowId:" << settings.showId
             << "SkipIntro:" << settings.skipIntro 
             << "SkipOutro:" << settings.skipOutro 
             << "Autoplay:" << settings.autoplay << "AutoplayRandom:" << settings.autoplayRandom
             << "UseTMDB:" << settings.useTMDB
             << "DisplayFileNames:" << settings.displayFileNames << "DisplayNewEpNotif:" << settings.DisplayNewEpNotif;
    
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
    
    // Convert ShowSettings struct to QMap
    QMap<QString, QVariant> settingsMap;
    convertSettingsToMap(settings, settingsMap);
    
    // Create the data storage field manager
    DataStorage_FieldManager fieldManager(m_encryptionKey, m_username);
    
    // Write validated settings using the field manager
    if (!fieldManager.writeValidatedData(settingsFilePath, 
                                        DataStorage_FieldDefinitions::TVShowSettings,
                                        settingsMap)) {
        qDebug() << "VP_ShowsSettings: Failed to write validated settings";
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

void VP_ShowsSettings::convertMapToSettings(const QMap<QString, QVariant>& settingsMap, ShowSettings& settings) const
{
    qDebug() << "VP_ShowsSettings: Converting QMap to ShowSettings struct";
    
    // Initialize with defaults
    settings = ShowSettings();
    
    // Convert each field from QMap to struct
    if (settingsMap.contains("showName")) {
        settings.showName = settingsMap["showName"].toString();
    }
    
    if (settingsMap.contains("showId")) {
        settings.showId = settingsMap["showId"].toString();
    }
    
    // Backward compatibility: Only override useTMDB if it's not already set in the map
    // This preserves the user's choice even if the show ID is missing
    if ((settings.showId.isEmpty() || settings.showId == "error") && !settingsMap.contains("useTMDB")) {
        settings.useTMDB = false;
        qDebug() << "VP_ShowsSettings: showId is invalid and useTMDB not set, defaulting TMDB to false for backward compatibility";
    }
    
    if (settingsMap.contains("skipIntro")) {
        settings.skipIntro = settingsMap["skipIntro"].toBool();
    }
    
    if (settingsMap.contains("skipOutro")) {
        settings.skipOutro = settingsMap["skipOutro"].toBool();
    }
    
    if (settingsMap.contains("autoplay")) {
        settings.autoplay = settingsMap["autoplay"].toBool();
    }
    
    if (settingsMap.contains("autoplayRandom")) {
        settings.autoplayRandom = settingsMap["autoplayRandom"].toBool();
    }
    
    if (settingsMap.contains("useTMDB")) {
        // Always respect the user's saved preference
        settings.useTMDB = settingsMap["useTMDB"].toBool();
    }
    
    // REMOVED autoFullscreen - now using global setting_VP_Shows_AutoFullScreen
    
    if (settingsMap.contains("displayFileNames")) {
        settings.displayFileNames = settingsMap["displayFileNames"].toBool();
    }
    
    if (settingsMap.contains("DisplayNewEpNotif")) {
        settings.DisplayNewEpNotif = settingsMap["DisplayNewEpNotif"].toBool();
    }
    
    // New episode tracking fields
    if (settingsMap.contains("NewEPCheckDate")) {
        settings.NewEPCheckDate = settingsMap["NewEPCheckDate"].toString();
    }
    
    if (settingsMap.contains("NewAvailableEPCount")) {
        settings.NewAvailableEPCount = settingsMap["NewAvailableEPCount"].toInt();
    }
    
    if (settingsMap.contains("LastAvailableEP")) {
        settings.LastAvailableEP = settingsMap["LastAvailableEP"].toString();
    }
    
    qDebug() << "VP_ShowsSettings: Conversion completed - converted" << settingsMap.size() << "fields";
}

void VP_ShowsSettings::convertSettingsToMap(const ShowSettings& settings, QMap<QString, QVariant>& settingsMap) const
{
    qDebug() << "VP_ShowsSettings: Converting ShowSettings struct to QMap";
    
    settingsMap.clear();
    
    // Convert each field from struct to QMap
    settingsMap["showName"] = settings.showName;
    settingsMap["showId"] = settings.showId;
    settingsMap["skipIntro"] = settings.skipIntro;
    settingsMap["skipOutro"] = settings.skipOutro;
    settingsMap["autoplay"] = settings.autoplay;
    settingsMap["autoplayRandom"] = settings.autoplayRandom;
    settingsMap["useTMDB"] = settings.useTMDB;
    // REMOVED autoFullscreen - now using global setting_VP_Shows_AutoFullScreen
    settingsMap["displayFileNames"] = settings.displayFileNames;
    settingsMap["DisplayNewEpNotif"] = settings.DisplayNewEpNotif;
    
    // New episode tracking fields
    settingsMap["NewEPCheckDate"] = settings.NewEPCheckDate;
    settingsMap["NewAvailableEPCount"] = settings.NewAvailableEPCount;
    settingsMap["LastAvailableEP"] = settings.LastAvailableEP;
    
    qDebug() << "VP_ShowsSettings: Conversion completed - created" << settingsMap.size() << "fields";
}
