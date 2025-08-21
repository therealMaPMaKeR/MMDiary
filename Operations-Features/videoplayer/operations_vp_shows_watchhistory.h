#ifndef OPERATIONS_VP_SHOWS_WATCHHISTORY_H
#define OPERATIONS_VP_SHOWS_WATCHHISTORY_H

#include <QObject>
#include <QTimer>
#include <memory>
#include "vp_shows_watchhistory.h"

// Forward declarations
class Operations_VP_Shows;
class VideoPlayer;

/**
 * @brief Integration helper class for watch history with video player
 * This class bridges the watch history system with the existing player
 * 
 * NOTE: This is a helper/example file showing how to integrate watch history.
 * The actual implementation should be added to operations_vp_shows.cpp
 */
class Operations_VP_Shows_WatchHistory : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief Constructor
     * @param parent Parent Operations_VP_Shows instance
     */
    explicit Operations_VP_Shows_WatchHistory(Operations_VP_Shows* parent);
    ~Operations_VP_Shows_WatchHistory();
    
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
    
    /**
     * @brief Start tracking playback for an episode
     * @param episodePath Relative path to the episode
     * @param player Video player instance
     */
    void startTracking(const QString& episodePath, VideoPlayer* player);
    
    /**
     * @brief Stop tracking playback
     */
    void stopTracking();
    
    /**
     * @brief Get resume position for an episode
     * @param episodePath Relative path to the episode
     * @return Resume position in milliseconds
     */
    qint64 getResumePosition(const QString& episodePath) const;
    
    /**
     * @brief Get the next episode to play
     * @param currentEpisodePath Current episode
     * @param availableEpisodes List of all available episodes
     * @return Path to next episode, or empty if none
     */
    QString getNextEpisode(const QString& currentEpisodePath,
                          const QStringList& availableEpisodes) const;
    
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
     * @brief Clear all watch history for current show
     * @return true if cleared successfully
     */
    bool clearHistory();
    
    /**
     * @brief Get last watched episode for continuing playback
     * @return Path to last watched episode
     */
    QString getLastWatchedEpisode() const;
    
    /**
     * @brief Mark current episode as completed
     */
    void markCurrentEpisodeCompleted();
    
signals:
    /**
     * @brief Emitted when episode is near completion
     * @param episodePath Path to the episode
     */
    void episodeNearCompletion(const QString& episodePath);
    
    /**
     * @brief Emitted when episode is completed
     * @param episodePath Path to the episode
     */
    void episodeCompleted(const QString& episodePath);
    
    /**
     * @brief Emitted when progress is saved
     */
    void progressSaved();
    
private slots:
    /**
     * @brief Periodic update of watch progress
     */
    void updateProgress();
    
private:
    Operations_VP_Shows* m_parent;
    std::unique_ptr<VP_ShowsWatchHistory> m_watchHistory;
    QTimer* m_progressTimer;
    VideoPlayer* m_currentPlayer;
    QString m_currentEpisodePath;
    bool m_isTracking;
    qint64 m_lastSavedPosition;
    
    // Helper methods
    void checkForCompletion();
};


#endif // OPERATIONS_VP_SHOWS_WATCHHISTORY_H
