#ifndef VP_SHOWS_PLAYBACK_TRACKER_H
#define VP_SHOWS_PLAYBACK_TRACKER_H

#include <QObject>
#include <QPointer>
#include <memory>
#include "../../Operations-Global/SafeTimer.h"
#include "vp_shows_watchhistory.h"

// Forward declarations
class Operations_VP_Shows;
class VP_Shows_Videoplayer;

/**
 * @brief Playback tracking integration for watch history
 * 
 * This class acts as a bridge between the watch history data management (VP_ShowsWatchHistory)
 * and the video player, handling real-time tracking during playback.
 * 
 * Responsibilities:
 * - Connects watch history system with video player
 * - Manages periodic progress saves during playback
 * - Handles player signals for tracking start/stop
 * - Provides UI-friendly methods for watch history operations
 */
class VP_ShowsPlaybackTracker : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief Constructor
     * @param parent Parent Operations_VP_Shows instance
     */
    explicit VP_ShowsPlaybackTracker(Operations_VP_Shows* parent);
    ~VP_ShowsPlaybackTracker();
    
    // === Initialization ===
    
    /**
     * @brief Initialize watch history for a show
     * @param showFolderPath Path to the show folder
     * @param encryptionKey User's encryption key
     * @param username Current username
     * @return true if initialized successfully
     */
    bool initializeForShow(const QString& showFolderPath,
                          const QByteArray& encryptionKey,
                          const QString& username);
    
    // === Playback Tracking ===
    
    /**
     * @brief Start tracking playback for an episode
     * @param episodePath Relative path to the episode within show folder
     * @param player Video player instance
     */
    void startTracking(const QString& episodePath, VP_Shows_Videoplayer* player);
    
    /**
     * @brief Stop tracking playback and save final position
     * @param finalPosition Optional final position to save (if already captured)
     */
    void stopTracking(qint64 finalPosition = -1);
    
    /**
     * @brief Check if currently tracking
     * @return true if actively tracking an episode
     */
    bool isTracking() const { return m_isTracking; }
    
    // === Watch History Queries ===
    
    /**
     * @brief Get resume position for an episode
     * @param episodePath Relative path to the episode
     * @return Resume position in milliseconds, or 0 if should start from beginning
     */
    qint64 getResumePosition(const QString& episodePath) const;
    
    /**
     * @brief Get the last watched episode path
     * @return Path to last watched episode, or empty if none
     */
    QString getLastWatchedEpisode() const;
    
    /**
     * @brief Get the next unwatched episode
     * @param currentEpisodePath Current episode path
     * @param availableEpisodes List of all available episode paths
     * @return Path to next episode, or empty if none
     */
    QString getNextEpisode(const QString& currentEpisodePath,
                          const QStringList& availableEpisodes) const;
    
    /**
     * @brief Check if an episode has been watched
     * @param episodePath Relative path to the episode
     * @return true if episode has been watched
     */
    bool hasEpisodeBeenWatched(const QString& episodePath) const;
    
    /**
     * @brief Check if an episode is completed
     * @param episodePath Relative path to the episode
     * @return true if episode was watched to completion
     */
    bool isEpisodeCompleted(const QString& episodePath) const;
    
    // === Settings Management ===
    
    /**
     * @brief Check if autoplay is enabled for current show
     * @return true if autoplay is enabled
     */
    bool isAutoplayEnabled() const;
    
    /**
     * @brief Toggle autoplay for current show
     * @param enabled Whether to enable autoplay
     */
    void setAutoplayEnabled(bool enabled);
    
    /**
     * @brief Get all show settings
     * @return Current show settings
     */
    TVShowSettings getShowSettings() const;
    
    /**
     * @brief Update show settings
     * @param settings New settings to apply
     */
    void updateShowSettings(const TVShowSettings& settings);
    
    // === Data Management ===
    
    /**
     * @brief Clear all watch history for current show
     * @return true if cleared successfully
     */
    bool clearHistory();
    
    /**
     * @brief Force save current watch history to disk
     * @return true if saved successfully
     */
    bool saveHistory();
    
    /**
     * @brief Mark current episode as completed
     */
    void markCurrentEpisodeCompleted();
    
    /**
     * @brief Mark any episode as watched/unwatched
     * @param episodePath Relative path to the episode
     * @param watched Whether to mark as watched (true) or unwatched (false)
     * @note This preserves the lastPosition for resume functionality
     */
    void setEpisodeWatched(const QString& episodePath, bool watched);
    
    /**
     * @brief Mark an episode as watched (completed)
     * @param episodePath Relative path to the episode
     */
    void markEpisodeWatched(const QString& episodePath);
    
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
    
    // === Statistics ===
    
    /**
     * @brief Get total watch time for the show
     * @return Total time watched in milliseconds
     */
    qint64 getTotalWatchTime() const;
    
    /**
     * @brief Get number of unique episodes watched
     * @return Count of episodes with watch history
     */
    int getWatchedEpisodeCount() const;
    
    /**
     * @brief Get number of completed episodes
     * @return Count of episodes watched to completion
     */
    int getCompletedEpisodeCount() const;
    
signals:
    /**
     * @brief Emitted when episode is near completion (within 2 minutes of end)
     * @param episodePath Path to the episode
     */
    void episodeNearCompletion(const QString& episodePath);
    
    /**
     * @brief Emitted when episode is marked as completed
     * @param episodePath Path to the episode
     */
    void episodeCompleted(const QString& episodePath);
    
    /**
     * @brief Emitted when progress is saved to disk
     */
    void progressSaved();
    
    /**
     * @brief Emitted when tracking starts for an episode
     * @param episodePath Path to the episode
     */
    void trackingStarted(const QString& episodePath);
    
    /**
     * @brief Emitted when tracking stops
     * @param episodePath Path to the episode
     * @param finalPosition Final position when stopped
     */
    void trackingStopped(const QString& episodePath, qint64 finalPosition);
    
private slots:
    /**
     * @brief Periodic update of watch progress
     */
    void updateProgress();
    
private:
    // Parent operations object
    Operations_VP_Shows* m_parent;
    
    // Core watch history data manager
    std::unique_ptr<VP_ShowsWatchHistory> m_watchHistory;
    
    // Tracking state
    SafeTimer* m_progressTimer;
    QPointer<VP_Shows_Videoplayer> m_currentPlayer;
    QString m_currentEpisodePath;
    bool m_isTracking;
    qint64 m_lastSavedPosition;
    
    // Helper methods
    void connectPlayerSignals(VP_Shows_Videoplayer* player, int sessionId);
    void disconnectPlayerSignals();
    
    // Track if we've already emitted near completion for current episode
    QString m_lastNearCompletionEpisode;
    
    // Tracking session counter to prevent stale lambda executions
    int m_trackingSessionId;
};

#endif // VP_SHOWS_PLAYBACK_TRACKER_H
