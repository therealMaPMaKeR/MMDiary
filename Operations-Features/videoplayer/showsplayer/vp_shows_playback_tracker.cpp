#include "vp_shows_playback_tracker.h"
#include "operations_vp_shows.h"
#include "VP_Shows_Videoplayer.h"
#include <QDebug>
#include <QTimer>
#include <QDir>

VP_ShowsPlaybackTracker::VP_ShowsPlaybackTracker(Operations_VP_Shows* parent)
    : QObject(parent)
    , m_parent(parent)
    , m_progressTimer(new QTimer(this))
    , m_currentPlayer(nullptr)
    , m_isTracking(false)
    , m_lastSavedPosition(0)
{
    qDebug() << "VP_ShowsPlaybackTracker: Initializing playback tracker";
    
    // Setup progress update timer
    m_progressTimer->setInterval(VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000);
    connect(m_progressTimer, &QTimer::timeout, this, &VP_ShowsPlaybackTracker::updateProgress);
}

VP_ShowsPlaybackTracker::~VP_ShowsPlaybackTracker()
{
    qDebug() << "VP_ShowsPlaybackTracker: Destroying playback tracker";
    
    // Stop tracking if still active
    if (m_isTracking) {
        qDebug() << "VP_ShowsPlaybackTracker: Still tracking in destructor, calling stopTracking";
        stopTracking();
    }
}

bool VP_ShowsPlaybackTracker::initializeForShow(const QString& showFolderPath,
                                                const QByteArray& encryptionKey,
                                                const QString& username)
{
    qDebug() << "VP_ShowsPlaybackTracker: ============================================";
    qDebug() << "VP_ShowsPlaybackTracker: Initializing for show:" << showFolderPath;
    qDebug() << "VP_ShowsPlaybackTracker: Username:" << username;
    qDebug() << "VP_ShowsPlaybackTracker: Encryption key present:" << !encryptionKey.isEmpty();
    
    // Stop any existing tracking
    stopTracking();
    
    // Convert to absolute path
    QString absoluteShowPath = QDir(showFolderPath).absolutePath();
    qDebug() << "VP_ShowsPlaybackTracker: Absolute show path:" << absoluteShowPath;
    
    // Check if show folder exists
    QDir showDir(absoluteShowPath);
    if (!showDir.exists()) {
        qDebug() << "VP_ShowsPlaybackTracker: WARNING - Show folder does not exist:" << absoluteShowPath;
        // Try to create the folder
        if (showDir.mkpath(".")) {
            qDebug() << "VP_ShowsPlaybackTracker: Created show folder successfully";
        } else {
            qDebug() << "VP_ShowsPlaybackTracker: ERROR - Failed to create show folder";
            return false;
        }
    }
    
    // Create new watch history instance
    try {
        qDebug() << "VP_ShowsPlaybackTracker: Creating VP_ShowsWatchHistory instance...";
        m_watchHistory = std::make_unique<VP_ShowsWatchHistory>(
            absoluteShowPath, encryptionKey, username);
        
        qDebug() << "VP_ShowsPlaybackTracker: Instance created, loading history...";
        
        // Load existing history
        if (!m_watchHistory->loadHistory()) {
            qDebug() << "VP_ShowsPlaybackTracker: No existing history, starting fresh";
            // Try to save initial empty history
            if (m_watchHistory->saveHistory()) {
                qDebug() << "VP_ShowsPlaybackTracker: Initial empty history saved successfully";
            } else {
                qDebug() << "VP_ShowsPlaybackTracker: WARNING - Failed to save initial history";
            }
        } else {
            qDebug() << "VP_ShowsPlaybackTracker: History loaded successfully";
            qDebug() << "VP_ShowsPlaybackTracker: Show name:" << m_watchHistory->getShowName();
            qDebug() << "VP_ShowsPlaybackTracker: Watched episodes:" << m_watchHistory->getWatchedEpisodeCount();
            qDebug() << "VP_ShowsPlaybackTracker: Completed episodes:" << m_watchHistory->getCompletedEpisodeCount();
        }
        
        qDebug() << "VP_ShowsPlaybackTracker: Initialization complete";
        return true;
        
    } catch (const std::exception& e) {
        qDebug() << "VP_ShowsPlaybackTracker: Exception during initialization:" << e.what();
        m_watchHistory.reset();
        return false;
    } catch (...) {
        qDebug() << "VP_ShowsPlaybackTracker: Unknown exception during initialization";
        m_watchHistory.reset();
        return false;
    }
}

void VP_ShowsPlaybackTracker::startTracking(const QString& episodePath, VP_Shows_Videoplayer* player)
{
    qDebug() << "VP_ShowsPlaybackTracker: ============================================";
    qDebug() << "VP_ShowsPlaybackTracker: startTracking called";
    qDebug() << "VP_ShowsPlaybackTracker: Episode path:" << episodePath;
    qDebug() << "VP_ShowsPlaybackTracker: Player valid:" << (player != nullptr);
    qDebug() << "VP_ShowsPlaybackTracker: Watch history valid:" << (m_watchHistory != nullptr);
    
    if (!m_watchHistory || !player) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot start tracking - not initialized";
        return;
    }
    
    // Stop any existing tracking
    stopTracking();
    
    // Set new tracking state
    m_currentEpisodePath = episodePath;
    m_currentPlayer = player;
    m_isTracking = true;
    
    // Connect to player signals
    connectPlayerSignals(player);
    
    // Capture initial position
    qint64 initialPosition = player->position();
    m_lastSavedPosition = initialPosition;
    qDebug() << "VP_ShowsPlaybackTracker: Initial position captured:" << initialPosition << "ms";
    
    // Check for resume position
    qint64 resumePosition = getResumePosition(episodePath);
    if (resumePosition > 0) {
        m_lastSavedPosition = resumePosition;
        qDebug() << "VP_ShowsPlaybackTracker: Episode has resume position:" << resumePosition << "ms";
    }
    
    // Emit tracking started signal
    emit trackingStarted(episodePath);
    
    // Skip initial progress update if we have a resume position
    if (resumePosition == 0) {
        // Do initial progress update only if starting from beginning
        qDebug() << "VP_ShowsPlaybackTracker: Performing initial progress update...";
        updateProgress();
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: Skipping initial update, resuming from saved position";
    }
    
    // Start periodic updates with a shorter initial interval
    qDebug() << "VP_ShowsPlaybackTracker: Starting periodic timer with initial 2-second interval...";
    m_progressTimer->setInterval(2000);  // 2 seconds for first save
    m_progressTimer->start();
    
    // After first save, switch to normal interval
    QTimer::singleShot(2100, [this]() {
        if (m_progressTimer->isActive()) {
            m_progressTimer->setInterval(VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000);
            qDebug() << "VP_ShowsPlaybackTracker: Switched to normal save interval:" 
                     << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds";
        }
    });
    
    qDebug() << "VP_ShowsPlaybackTracker: Tracking started successfully";
}

void VP_ShowsPlaybackTracker::stopTracking(qint64 finalPosition)
{
    if (!m_isTracking) {
        qDebug() << "VP_ShowsPlaybackTracker: stopTracking called but not tracking";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: ============================================";
    qDebug() << "VP_ShowsPlaybackTracker: Stopping tracking";
    qDebug() << "VP_ShowsPlaybackTracker: Final position parameter:" << finalPosition << "ms";
    
    // Store episode path before clearing
    QString episodePath = m_currentEpisodePath;
    
    // Set tracking to false immediately to prevent double calls
    m_isTracking = false;
    
    // Stop timer first
    m_progressTimer->stop();
    
    // Do final progress update
    if (m_watchHistory && !m_currentEpisodePath.isEmpty()) {
        qint64 position = finalPosition;
        qint64 duration = 0;
        
        // If no position was provided, try to get it from the player
        if (finalPosition < 0 && m_currentPlayer) {
            position = m_currentPlayer->position();
            duration = m_currentPlayer->duration();
        } else if (m_currentPlayer) {
            duration = m_currentPlayer->duration();
        }
        
        qDebug() << "VP_ShowsPlaybackTracker: Position to save:" << position << "ms";
        qDebug() << "VP_ShowsPlaybackTracker: Duration:" << duration << "ms";
        
        // If position is 0 or negative but we have a valid last saved position, use that
        if (position <= 0 && m_lastSavedPosition > 0) {
            position = m_lastSavedPosition;
            qDebug() << "VP_ShowsPlaybackTracker: Using last saved position:" << position << "ms";
        }
        
        // Save the final position
        if (position > 0) {
            qDebug() << "VP_ShowsPlaybackTracker: Saving final position:" << position << "ms";
            m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
            m_watchHistory->saveHistory();
        } else {
            // Just ensure any pending changes are persisted
            qDebug() << "VP_ShowsPlaybackTracker: Ensuring history is saved";
            m_watchHistory->saveHistory();
        }
    }
    
    // Disconnect from player signals
    disconnectPlayerSignals();
    
    // Clear tracking state
    m_currentPlayer = nullptr;
    m_currentEpisodePath.clear();
    m_lastSavedPosition = 0;
    
    // Emit tracking stopped signal
    emit trackingStopped(episodePath, finalPosition >= 0 ? finalPosition : m_lastSavedPosition);
    
    qDebug() << "VP_ShowsPlaybackTracker: Tracking stopped successfully";
}

qint64 VP_ShowsPlaybackTracker::getResumePosition(const QString& episodePath) const
{
    if (!m_watchHistory) {
        return 0;
    }
    
    qint64 position = m_watchHistory->getResumePosition(episodePath);
    qDebug() << "VP_ShowsPlaybackTracker: Resume position for" << episodePath 
             << "is" << position << "ms";
    return position;
}

QString VP_ShowsPlaybackTracker::getLastWatchedEpisode() const
{
    if (!m_watchHistory) {
        return QString();
    }
    
    QString lastWatched = m_watchHistory->getLastWatchedEpisode();
    qDebug() << "VP_ShowsPlaybackTracker: Last watched episode:" 
             << (lastWatched.isEmpty() ? "none" : lastWatched);
    return lastWatched;
}

QString VP_ShowsPlaybackTracker::getNextEpisode(const QString& currentEpisodePath,
                                                const QStringList& availableEpisodes) const
{
    if (!m_watchHistory) {
        return QString();
    }
    
    QString nextEpisode = m_watchHistory->getNextUnwatchedEpisode(currentEpisodePath, availableEpisodes);
    qDebug() << "VP_ShowsPlaybackTracker: Next episode after" << currentEpisodePath 
             << "is" << (nextEpisode.isEmpty() ? "none" : nextEpisode);
    return nextEpisode;
}

bool VP_ShowsPlaybackTracker::hasEpisodeBeenWatched(const QString& episodePath) const
{
    if (!m_watchHistory) {
        return false;
    }
    
    return m_watchHistory->hasEpisodeBeenWatched(episodePath);
}

bool VP_ShowsPlaybackTracker::isEpisodeCompleted(const QString& episodePath) const
{
    if (!m_watchHistory) {
        return false;
    }
    
    return m_watchHistory->isEpisodeCompleted(episodePath);
}

bool VP_ShowsPlaybackTracker::isAutoplayEnabled() const
{
    if (!m_watchHistory) {
        return false;
    }
    
    return m_watchHistory->isAutoplayEnabled();
}

void VP_ShowsPlaybackTracker::setAutoplayEnabled(bool enabled)
{
    if (!m_watchHistory) {
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Setting autoplay to:" << enabled;
    m_watchHistory->setAutoplayEnabled(enabled);
    m_watchHistory->saveHistory();
}

TVShowSettings VP_ShowsPlaybackTracker::getShowSettings() const
{
    if (!m_watchHistory) {
        return TVShowSettings();
    }
    
    return m_watchHistory->getSettings();
}

void VP_ShowsPlaybackTracker::updateShowSettings(const TVShowSettings& settings)
{
    if (!m_watchHistory) {
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Updating show settings";
    m_watchHistory->updateSettings(settings);
    m_watchHistory->saveHistory();
}

bool VP_ShowsPlaybackTracker::clearHistory()
{
    if (!m_watchHistory) {
        return false;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Clearing watch history";
    return m_watchHistory->clearHistory();
}

bool VP_ShowsPlaybackTracker::saveHistory()
{
    if (!m_watchHistory) {
        return false;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Manually saving history";
    return m_watchHistory->saveHistory();
}

void VP_ShowsPlaybackTracker::markCurrentEpisodeCompleted()
{
    if (!m_watchHistory || m_currentEpisodePath.isEmpty()) {
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Marking episode as completed:" << m_currentEpisodePath;
    m_watchHistory->markEpisodeCompleted(m_currentEpisodePath);
    m_watchHistory->saveHistory();
    
    emit episodeCompleted(m_currentEpisodePath);
}

qint64 VP_ShowsPlaybackTracker::getTotalWatchTime() const
{
    if (!m_watchHistory) {
        return 0;
    }
    
    return m_watchHistory->getTotalWatchTime();
}

int VP_ShowsPlaybackTracker::getWatchedEpisodeCount() const
{
    if (!m_watchHistory) {
        return 0;
    }
    
    return m_watchHistory->getWatchedEpisodeCount();
}

int VP_ShowsPlaybackTracker::getCompletedEpisodeCount() const
{
    if (!m_watchHistory) {
        return 0;
    }
    
    return m_watchHistory->getCompletedEpisodeCount();
}

void VP_ShowsPlaybackTracker::updateProgress()
{
    qDebug() << "VP_ShowsPlaybackTracker: updateProgress called";
    
    if (!m_isTracking || !m_currentPlayer || !m_watchHistory || m_currentEpisodePath.isEmpty()) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot update progress - missing required components";
        return;
    }
    
    // Get current position and duration from player
    qint64 position = m_currentPlayer->position();
    qint64 duration = m_currentPlayer->duration();
    
    qDebug() << "VP_ShowsPlaybackTracker: Player position:" << position << "ms, duration:" << duration << "ms";
    
    // Special case: if position is 0 but we have a saved position, don't overwrite
    if (position == 0 && m_lastSavedPosition > 0) {
        qDebug() << "VP_ShowsPlaybackTracker: Position is 0 but have saved position, skipping save";
        return;
    }
    
    // Skip if position hasn't changed significantly
    if (position > 0 && qAbs(position - m_lastSavedPosition) < 1000) {
        qDebug() << "VP_ShowsPlaybackTracker: Position change too small, skipping save";
        return;
    }
    
    // Update last saved position
    m_lastSavedPosition = position;
    
    // Update watch progress
    qDebug() << "VP_ShowsPlaybackTracker: Updating progress for episode:" << m_currentEpisodePath;
    m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
    
    // Check for completion
    checkForCompletion();
    
    // Save to disk
    if (m_watchHistory->saveHistory()) {
        qDebug() << "VP_ShowsPlaybackTracker: History saved successfully";
        emit progressSaved();
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: WARNING - Failed to save history";
    }
}

void VP_ShowsPlaybackTracker::checkForCompletion()
{
    if (!m_currentPlayer || !m_watchHistory) {
        return;
    }
    
    qint64 position = m_currentPlayer->position();
    qint64 duration = m_currentPlayer->duration();
    
    if (duration <= 0) {
        return;
    }
    
    // Check if near end (within 2 minutes)
    qint64 remaining = duration - position;
    if (remaining <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS) {
        // Track if we've already emitted the near completion signal for this episode
        static QString lastNearCompletionEpisode;
        if (lastNearCompletionEpisode != m_currentEpisodePath) {
            qDebug() << "VP_ShowsPlaybackTracker: Episode near completion:" << m_currentEpisodePath;
            emit episodeNearCompletion(m_currentEpisodePath);
            lastNearCompletionEpisode = m_currentEpisodePath;
        }
        
        // If within 10 seconds of end, mark as completed
        if (remaining <= 10000) {
            markCurrentEpisodeCompleted();
        }
    }
}

void VP_ShowsPlaybackTracker::connectPlayerSignals(VP_Shows_Videoplayer* player)
{
    if (!player) return;
    
    qDebug() << "VP_ShowsPlaybackTracker: Connecting to player signals";
    
    // Connect to position changed for continuous tracking
    connect(player, &VP_Shows_Videoplayer::positionChanged,
            this, [this](qint64 position) {
                if (m_isTracking && position > 0) {
                    // Track the latest position but don't save every time
                    m_lastSavedPosition = position;
                }
            });
    
    // Connect to aboutToClose signal to capture final position
    connect(player, &VP_Shows_Videoplayer::aboutToClose,
            this, [this](qint64 finalPosition) {
                qDebug() << "VP_ShowsPlaybackTracker: Received aboutToClose signal with position:" << finalPosition << "ms";
                if (m_isTracking) {
                    stopTracking(finalPosition);
                }
            }, Qt::DirectConnection);
}

void VP_ShowsPlaybackTracker::disconnectPlayerSignals()
{
    if (m_currentPlayer) {
        qDebug() << "VP_ShowsPlaybackTracker: Disconnecting from player signals";
        disconnect(m_currentPlayer, nullptr, this, nullptr);
    }
}
