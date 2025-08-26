#ifndef VP_SHOWS_SETTINGS_H
#define VP_SHOWS_SETTINGS_H

#include <QString>
#include <QByteArray>

class VP_ShowsSettings
{
public:
    // Structure to hold show-specific settings
    struct ShowSettings {
        bool skipIntro = false;      // Skip intro for episodes
        bool skipOutro = false;      // Skip outro for episodes
        bool autoplay = true;        // Changed default to true
        bool useTMDB = true;         // New field for TMDB usage
        
        // Additional settings can be added here in the future
        // For example:
        // int introSkipSeconds = 0;
        // int outroSkipSeconds = 0;
        // float playbackSpeed = 1.0;
        // int audioTrack = 0;
        // int subtitleTrack = -1;
    };
    
    // Constructor with encryption key and username
    VP_ShowsSettings(const QByteArray& encryptionKey, const QString& username);
    ~VP_ShowsSettings();
    
    // Load settings for a specific show
    bool loadShowSettings(const QString& showFolderPath, ShowSettings& settings);
    
    // Save settings for a specific show
    bool saveShowSettings(const QString& showFolderPath, const ShowSettings& settings);
    
    // Delete settings file for a show (used when show is deleted)
    bool deleteShowSettings(const QString& showFolderPath);
    
private:
    // Generate settings filename based on show folder
    QString generateSettingsFileName(const QString& showFolderPath) const;
    
    // Serialize settings to string
    QString serializeSettings(const ShowSettings& settings) const;
    
    // Deserialize settings from string
    bool deserializeSettings(const QString& data, ShowSettings& settings) const;
    
    QByteArray m_encryptionKey;
    QString m_username;
};

#endif // VP_SHOWS_SETTINGS_H
