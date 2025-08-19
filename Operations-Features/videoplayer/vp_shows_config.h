#ifndef VP_SHOWS_CONFIG_H
#define VP_SHOWS_CONFIG_H

#include <QString>
#include <QSettings>

class VP_ShowsConfig
{
public:
    // Get the TMDB API key (should be stored securely)
    static QString getTMDBApiKey();
    
    // Set the TMDB API key (for initial configuration)
    static void setTMDBApiKey(const QString& apiKey);
    
    // Check if TMDB integration is enabled
    static bool isTMDBEnabled();
    
    // Enable/disable TMDB integration
    static void setTMDBEnabled(bool enabled);
    
    // Get temp directory for TV shows (uses Data/username/temp, NOT system temp)
    // Returns empty string if directory creation fails
    static QString getTempDirectory(const QString& username);
    
    // Clean up TMDB temp files (removes only TMDB-related files in Data/username/temp)
    // Only cleans files matching patterns: tmdb_*, temp_show_*, temp_episode_*
    static void cleanupTempDirectory(const QString& username);
    
private:
    VP_ShowsConfig() = default; // Static class
};

#endif // VP_SHOWS_CONFIG_H
