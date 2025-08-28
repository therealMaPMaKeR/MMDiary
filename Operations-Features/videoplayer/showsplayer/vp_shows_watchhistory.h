#ifndef VP_SHOWS_WATCHHISTORY_H
#define VP_SHOWS_WATCHHISTORY_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QMap>
#include <memory>

/**
 * @brief Episode watch information structure
 * Stores detailed watch progress for individual episodes
 */
struct EpisodeWatchInfo {
    QString episodePath;          // Relative path within show folder
    QString episodeIdentifier;    // Episode identifier (e.g., "S01E01")
    QDateTime lastWatched;        // Last time this episode was watched
    qint64 lastPosition;          // Last playback position in milliseconds
    qint64 totalDuration;         // Total episode duration in milliseconds
    bool completed;               // Whether episode was watched to completion
    int watchCount;               // Number of times episode was watched
    
    // Constructor
    EpisodeWatchInfo() : lastPosition(0), totalDuration(0), completed(false), watchCount(0) {}
    
    // Convert to JSON
    QJsonObject toJson() const;
    
    // Create from JSON
    static EpisodeWatchInfo fromJson(const QJsonObject& json);
};

/**
 * @brief TV Show settings structure
 * Stores show-specific preferences and settings
 */
struct TVShowSettings {
    bool autoplayEnabled;         // Whether to automatically play next episode
    bool skipIntroEnabled;        // Whether to skip intro (future feature)
    bool skipOutroEnabled;        // Whether to skip outro (future feature)
    int introSkipSeconds;         // Number of seconds to skip for intro
    int outroSkipSeconds;         // Number of seconds to skip for outro
    QString preferredLanguage;    // Preferred audio language
    QString preferredTranslation; // Preferred subtitle/dub preference
    
    // Constructor with defaults
    TVShowSettings() : 
        autoplayEnabled(true),
        skipIntroEnabled(false),
        skipOutroEnabled(false),
        introSkipSeconds(0),
        outroSkipSeconds(0),
        preferredLanguage("English"),
        preferredTranslation("Subbed") {}
    
    // Convert to JSON
    QJsonObject toJson() const;
    
    // Create from JSON
    static TVShowSettings fromJson(const QJsonObject& json);
};

/**
 * @brief Complete watch history data for a TV show
 * Main data structure containing all watch history and settings
 */
struct TVShowWatchData {
    QString showName;                                  // Name of the TV show
    QString lastWatchedEpisode;                       // Path to last watched episode
    QDateTime lastWatchedTime;                        // When the show was last watched
    TVShowSettings settings;                          // Show-specific settings
    QMap<QString, EpisodeWatchInfo> watchHistory;     // Map of episode path to watch info
    
    // Convert to JSON
    QJsonObject toJson() const;
    
    // Create from JSON
    static TVShowWatchData fromJson(const QJsonObject& json);
};

/**
 * @brief TV Show Watch History Manager
 * Handles loading, saving, and managing watch history for TV shows
 */
class VP_ShowsWatchHistory
{
public:
    // Constants
    static constexpr const char* HISTORY_FILENAME = ".show_history.encrypted";
    static constexpr const char* BACKUP_FILENAME = ".show_history.backup.encrypted";
    static constexpr int MAX_BATCH_SIZE = 100; // Maximum episodes to process in one batch
    static constexpr qint64 COMPLETION_THRESHOLD_MS = 120000; // 2 minutes in milliseconds - used for all near-end operations
    static constexpr qint64 RESUME_THRESHOLD_MS = 60000; // Deprecated - kept for compatibility, use COMPLETION_THRESHOLD_MS instead
    static constexpr int SAVE_INTERVAL_SECONDS = 10; // Save progress every 10 seconds
    
    /**
     * @brief Constructor
     * @param showFolderPath Path to the TV show folder
     * @param encryptionKey Encryption key for secure storage
     * @param username Current user's username
     */
    VP_ShowsWatchHistory(const QString& showFolderPath, 
                        const QByteArray& encryptionKey,
                        const QString& username);
    
    /**
     * @brief Destructor - ensures data is saved
     */
    ~VP_ShowsWatchHistory();
    
    // Core functionality
    
    /**
     * @brief Load watch history from encrypted file
     * @return true if successfully loaded, false otherwise
     */
    bool loadHistory();
    
    /**
     * @brief Save watch history to encrypted file
     * @return true if successfully saved, false otherwise
     */
    bool saveHistory();
    
    /**
     * @brief Save history with automatic backup creation
     * @return true if successfully saved, false otherwise
     */
    bool saveHistoryWithBackup();
    
    /**
     * @brief Restore history from backup if main file is corrupted
     * @return true if successfully restored, false otherwise
     */
    bool restoreFromBackup();
    
    /**
     * @brief Validate JSON content before saving
     * @param jsonContent JSON string to validate
     * @return true if valid, false otherwise
     */
    bool validateJsonContent(const QString& jsonContent) const;
    
    /**
     * @brief Batch update multiple episodes efficiently
     * @param episodePaths List of episode paths
     * @param watched Whether to mark as watched or unwatched
     */
    void batchSetEpisodesWatched(const QStringList& episodePaths, bool watched);
    
    /**
     * @brief Clear all watch history for this show
     * @return true if successfully cleared, false otherwise
     */
    bool clearHistory();
    
    // Episode management
    
    /**
     * @brief Update watch progress for an episode
     * @param episodePath Relative path to the episode
     * @param position Current playback position in milliseconds
     * @param duration Total duration of the episode in milliseconds
     * @param episodeIdentifier Optional episode identifier (e.g., "S01E01")
     */
    void updateWatchProgress(const QString& episodePath, 
                           qint64 position, 
                           qint64 duration,
                           const QString& episodeIdentifier = QString());
    
    /**
     * @brief Mark an episode as completed
     * @param episodePath Relative path to the episode
     */
    void markEpisodeCompleted(const QString& episodePath);
    
    /**
     * @brief Toggle watched status of an episode
     * @param episodePath Relative path to the episode
     * @param watched Whether to mark as watched (true) or unwatched (false)
     * @note This preserves the lastPosition for resume functionality
     */
    void setEpisodeWatched(const QString& episodePath, bool watched);
    
    /**
     * @brief Mark an episode as unwatched
     * @param episodePath Relative path to the episode
     */
    void markEpisodeUnwatched(const QString& episodePath);
    
    /**
     * @brief Reset the resume position for an episode to start from beginning
     * @param episodePath Relative path to the episode
     * @note This only resets position, doesn't change watched status
     */
    void resetEpisodePosition(const QString& episodePath);
    
    /**
     * @brief Clear the last watched episode (used when marking it as unwatched)
     */
    void clearLastWatchedEpisode();
    
    /**
     * @brief Set the last watched episode
     * @param episodePath Relative path to the episode
     */
    void setLastWatchedEpisode(const QString& episodePath);
    
    /**
     * @brief Get watch info for a specific episode
     * @param episodePath Relative path to the episode
     * @return Episode watch info, or empty info if not found
     */
    EpisodeWatchInfo getEpisodeWatchInfo(const QString& episodePath) const;
    
    /**
     * @brief Check if an episode exists in watch history (regardless of watched status)
     * @param episodePath Relative path to the episode
     * @return true if episode exists in history, false otherwise
     * @note This does NOT check if episode is marked as watched/completed.
     *       Use isEpisodeCompleted() to check if actually watched.
     */
    bool hasEpisodeBeenWatched(const QString& episodePath) const;
    
    /**
     * @brief Check if an episode exists in the watch history
     * @param episodePath Relative path to the episode
     * @return true if episode is in history, false otherwise
     * @note Alias for hasEpisodeBeenWatched() with clearer naming
     */
    bool isEpisodeInHistory(const QString& episodePath) const;
    
    /**
     * @brief Check if an episode is completed
     * @param episodePath Relative path to the episode
     * @return true if episode is completed, false otherwise
     */
    bool isEpisodeCompleted(const QString& episodePath) const;
    
    /**
     * @brief Get the last watched episode path
     * @return Path to last watched episode, or empty string if none
     */
    QString getLastWatchedEpisode() const;
    
    /**
     * @brief Get the next unwatched episode after a given episode
     * @param currentEpisodePath Current episode path
     * @param availableEpisodes List of all available episode paths
     * @return Path to next unwatched episode, or empty string if none
     */
    QString getNextUnwatchedEpisode(const QString& currentEpisodePath,
                                   const QStringList& availableEpisodes) const;
    
    /**
     * @brief Get resume position for an episode
     * @param episodePath Relative path to the episode
     * @return Resume position in milliseconds, or 0 if should start from beginning
     */
    qint64 getResumePosition(const QString& episodePath) const;
    
    // Settings management
    
    /**
     * @brief Get show settings
     * @return Current show settings
     */
    TVShowSettings getSettings() const;
    
    /**
     * @brief Update show settings
     * @param settings New settings to apply
     */
    void updateSettings(const TVShowSettings& settings);
    
    /**
     * @brief Check if autoplay is enabled
     * @return true if autoplay is enabled, false otherwise
     */
    bool isAutoplayEnabled() const;
    
    /**
     * @brief Set autoplay enabled state
     * @param enabled Whether autoplay should be enabled
     */
    void setAutoplayEnabled(bool enabled);
    
    // Show metadata
    
    /**
     * @brief Get show name
     * @return Name of the TV show
     */
    QString getShowName() const;
    
    /**
     * @brief Set show name
     * @param showName Name of the TV show
     */
    void setShowName(const QString& showName);
    
    /**
     * @brief Get total watch time for the show
     * @return Total watch time in milliseconds
     */
    qint64 getTotalWatchTime() const;
    
    /**
     * @brief Get number of watched episodes
     * @return Count of unique episodes watched
     */
    int getWatchedEpisodeCount() const;
    
    /**
     * @brief Get number of completed episodes
     * @return Count of episodes watched to completion
     */
    int getCompletedEpisodeCount() const;
    
    /**
     * @brief Get all episode paths from watch history
     * @return List of all episode paths that have been watched
     */
    QStringList getAllWatchedEpisodes() const;

private:
    // Member variables
    QString m_showFolderPath;
    QByteArray m_encryptionKey;
    QString m_username;
    QString m_historyFilePath;
    QString m_backupFilePath;
    
    // Watch data
    std::unique_ptr<TVShowWatchData> m_watchData;
    
    // State tracking
    bool m_isDirty;  // Whether data needs to be saved
    
    // Helper methods
    
    /**
     * @brief Initialize empty watch data
     */
    void initializeEmptyData();
    
    /**
     * @brief Validate and sanitize episode path
     * @param episodePath Path to validate
     * @return Sanitized path, or empty string if invalid
     */
    QString validateEpisodePath(const QString& episodePath) const;
    
    /**
     * @brief Check if position is near the end of episode
     * @param position Current position in milliseconds
     * @param duration Total duration in milliseconds
     * @return true if within completion threshold of the end
     */
    bool isNearEnd(qint64 position, qint64 duration) const;
    
    /**
     * @brief Parse episode identifier from filename
     * @param episodePath Path to the episode file
     * @return Episode identifier (e.g., "S01E01"), or empty string if cannot parse
     */
    QString parseEpisodeIdentifier(const QString& episodePath) const;
};

#endif // VP_SHOWS_WATCHHISTORY_H
