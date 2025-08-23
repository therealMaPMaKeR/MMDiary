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
    int intervalMs = VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000;
    m_progressTimer->setInterval(intervalMs);
    qDebug() << "VP_ShowsPlaybackTracker: Timer interval set to:" << intervalMs << "ms (" << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds)";
    
    QMetaObject::Connection timerConnection = connect(m_progressTimer, &QTimer::timeout, this, &VP_ShowsPlaybackTracker::updateProgress);
    if (timerConnection) {
        qDebug() << "VP_ShowsPlaybackTracker: Successfully connected timer to updateProgress";
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Failed to connect timer to updateProgress!";
    }
    
    // Verify COMPLETION_THRESHOLD_MS value
    qDebug() << "VP_ShowsPlaybackTracker: COMPLETION_THRESHOLD_MS =" << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS << "ms (" << (VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS / 1000) << "seconds)";
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
    qint64 initialDuration = player->duration();
    m_lastSavedPosition = initialPosition;
    qDebug() << "VP_ShowsPlaybackTracker: Initial position captured:" << initialPosition << "ms";
    qDebug() << "VP_ShowsPlaybackTracker: Initial duration:" << initialDuration << "ms";
    
    if (initialDuration <= 0) {
        qDebug() << "VP_ShowsPlaybackTracker: WARNING - Duration not yet available at start of tracking";
    }
    
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
    
    if (m_progressTimer->isActive()) {
        qDebug() << "VP_ShowsPlaybackTracker: Timer successfully started and is active";
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Timer failed to start!";
    }
    
    // After first save, switch to normal interval
    QTimer::singleShot(2100, [this]() {
        if (m_progressTimer->isActive()) {
            int normalInterval = VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000;
            m_progressTimer->setInterval(normalInterval);
            qDebug() << "VP_ShowsPlaybackTracker: Switched to normal save interval:" 
                     << normalInterval << "ms (" << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds)";
        } else {
            qDebug() << "VP_ShowsPlaybackTracker: Timer not active when trying to switch interval";
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
    qint64 emittedPosition = finalPosition >= 0 ? finalPosition : m_lastSavedPosition;
    emit trackingStopped(episodePath, emittedPosition);
    
    qDebug() << "VP_ShowsPlaybackTracker: Tracking stopped successfully";
    qDebug() << "VP_ShowsPlaybackTracker: === TRACKING SUMMARY ===";
    qDebug() << "VP_ShowsPlaybackTracker:   Episode:" << episodePath;
    qDebug() << "VP_ShowsPlaybackTracker:   Final position:" << emittedPosition << "ms";
    qDebug() << "VP_ShowsPlaybackTracker:   Timer was active:" << (m_progressTimer && m_progressTimer->isActive());
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
        qDebug() << "VP_ShowsPlaybackTracker: Cannot mark episode completed - missing components";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: *** MARKING EPISODE AS COMPLETED ***";
    qDebug() << "VP_ShowsPlaybackTracker: Episode:" << m_currentEpisodePath;
    m_watchHistory->markEpisodeCompleted(m_currentEpisodePath);
    m_watchHistory->saveHistory();
    
    qDebug() << "VP_ShowsPlaybackTracker: Emitting episodeCompleted signal";
    emit episodeCompleted(m_currentEpisodePath);
}

void VP_ShowsPlaybackTracker::setEpisodeWatched(const QString& episodePath, bool watched)
{
    if (!m_watchHistory) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot set watched status - watch history not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Setting episode" << episodePath << "watched status to:" << watched;
    m_watchHistory->setEpisodeWatched(episodePath, watched);
    m_watchHistory->saveHistory();
    
    if (watched) {
        emit episodeCompleted(episodePath);
    }
}

void VP_ShowsPlaybackTracker::markEpisodeWatched(const QString& episodePath)
{
    if (!m_watchHistory) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot mark as watched - watch history not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Marking episode as watched:" << episodePath;
    m_watchHistory->markEpisodeCompleted(episodePath);
    m_watchHistory->saveHistory();
    
    emit episodeCompleted(episodePath);
}

void VP_ShowsPlaybackTracker::markEpisodeUnwatched(const QString& episodePath)
{
    if (!m_watchHistory) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot mark as unwatched - watch history not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Marking episode as unwatched:" << episodePath;
    m_watchHistory->markEpisodeUnwatched(episodePath);
    m_watchHistory->saveHistory();
}

void VP_ShowsPlaybackTracker::resetEpisodePosition(const QString& episodePath)
{
    if (!m_watchHistory) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot reset position - watch history not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Resetting position for episode:" << episodePath;
    m_watchHistory->resetEpisodePosition(episodePath);
    m_watchHistory->saveHistory();
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
    static int updateCallCount = 0;
    updateCallCount++;
    qDebug() << "VP_ShowsPlaybackTracker: === updateProgress called (call #" << updateCallCount << ") ===";
    
    if (!m_isTracking || !m_currentPlayer || !m_watchHistory || m_currentEpisodePath.isEmpty()) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot update progress - missing required components";
        qDebug() << "VP_ShowsPlaybackTracker:   - m_isTracking:" << m_isTracking;
        qDebug() << "VP_ShowsPlaybackTracker:   - m_currentPlayer:" << (m_currentPlayer != nullptr);
        qDebug() << "VP_ShowsPlaybackTracker:   - m_watchHistory:" << (m_watchHistory != nullptr);
        qDebug() << "VP_ShowsPlaybackTracker:   - m_currentEpisodePath:" << m_currentEpisodePath;
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: All components present, getting player position...";
    
    // Get current position and duration from player
    qint64 position = m_currentPlayer->position();
    qint64 duration = m_currentPlayer->duration();
    
    qDebug() << "VP_ShowsPlaybackTracker: Got position:" << position << "ms, duration:" << duration << "ms";
    qDebug() << "VP_ShowsPlaybackTracker: Last saved position:" << m_lastSavedPosition << "ms";
    qDebug() << "VP_ShowsPlaybackTracker: Player state valid:" << (m_currentPlayer != nullptr);
    
    // Check if we have valid duration
    if (duration <= 0) {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Duration is invalid:" << duration << "ms";
        qDebug() << "VP_ShowsPlaybackTracker: Cannot proceed without valid duration";
        return;
    }
    
    // Special case: if position is 0 but we have a saved position, don't overwrite
    if (position == 0 && m_lastSavedPosition > 0) {
        qDebug() << "VP_ShowsPlaybackTracker: Position is 0 but have saved position, skipping save";
        return;
    }
    
    // Skip if position hasn't changed significantly
    if (position > 0 && qAbs(position - m_lastSavedPosition) < 1000) {
        qDebug() << "VP_ShowsPlaybackTracker: Position change too small (" << qAbs(position - m_lastSavedPosition) << "ms), skipping save";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Position change is significant, proceeding with update";
    
    // Update last saved position
    m_lastSavedPosition = position;
    
    // Update watch progress
    m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
    
    // Check for completion - emit signal when near end
    if (duration > 0) {
        qint64 remaining = duration - position;
        
        // Only log position info when getting close (within 3 minutes)
        if (remaining <= 180000) { // 3 minutes in ms
            qDebug() << "VP_ShowsPlaybackTracker: === POSITION CHECK (near end) ===";
            qDebug() << "VP_ShowsPlaybackTracker:   Position:" << position << "ms (" << (position/1000) << "s)";
            qDebug() << "VP_ShowsPlaybackTracker:   Duration:" << duration << "ms (" << (duration/1000) << "s)";
            qDebug() << "VP_ShowsPlaybackTracker:   Remaining:" << remaining << "ms (" << (remaining/1000) << "s)";
            qDebug() << "VP_ShowsPlaybackTracker:   Threshold:" << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS << "ms (" << (VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS/1000) << "s)";
            qDebug() << "VP_ShowsPlaybackTracker:   Is within threshold?" << (remaining <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS);
        }
        
        // Check if near end (within 2 minutes)
        if (remaining <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && remaining > 0) {
            // Only emit once per episode - use a simple flag approach
            static QString lastNearCompletionEpisode;
            qDebug() << "VP_ShowsPlaybackTracker: Near end condition MET!";
            qDebug() << "VP_ShowsPlaybackTracker: Last completion episode:" << lastNearCompletionEpisode;
            qDebug() << "VP_ShowsPlaybackTracker: Current episode:" << m_currentEpisodePath;
            qDebug() << "VP_ShowsPlaybackTracker: Are they different?" << (lastNearCompletionEpisode != m_currentEpisodePath);
            
            if (lastNearCompletionEpisode != m_currentEpisodePath) {
                qDebug() << "VP_ShowsPlaybackTracker: *** EPISODE NEAR COMPLETION ***";
                qDebug() << "VP_ShowsPlaybackTracker: Episode path:" << m_currentEpisodePath;
                qDebug() << "VP_ShowsPlaybackTracker: Remaining time:" << remaining << "ms";
                qDebug() << "VP_ShowsPlaybackTracker: About to emit episodeNearCompletion signal";
                emit episodeNearCompletion(m_currentEpisodePath);
                qDebug() << "VP_ShowsPlaybackTracker: Signal emitted!";
                lastNearCompletionEpisode = m_currentEpisodePath;
                qDebug() << "VP_ShowsPlaybackTracker: Updated lastNearCompletionEpisode to:" << lastNearCompletionEpisode;
            } else {
                qDebug() << "VP_ShowsPlaybackTracker: Signal already emitted for this episode";
            }
            
            // If within 10 seconds of end, mark as completed
            if (remaining <= 10000) {
                qDebug() << "VP_ShowsPlaybackTracker: Within 10 seconds of end, marking as completed";
                markCurrentEpisodeCompleted();
            }
        }
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: Duration is 0 or negative:" << duration;
    }
    
    // Save to disk
    qDebug() << "VP_ShowsPlaybackTracker: About to save history to disk...";
    if (m_watchHistory->saveHistory()) {
        qDebug() << "VP_ShowsPlaybackTracker: History saved successfully";
        emit progressSaved();
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: WARNING - Failed to save history";
    }
}

// checkForCompletion functionality has been integrated directly into updateProgress()

void VP_ShowsPlaybackTracker::connectPlayerSignals(VP_Shows_Videoplayer* player)
{
    if (!player) {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Player is null, cannot connect signals";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Connecting to player signals";
    qDebug() << "VP_ShowsPlaybackTracker: Player pointer:" << player;
    
    // Connect to position changed for continuous tracking
    QMetaObject::Connection posConnection = connect(player, &VP_Shows_Videoplayer::positionChanged,
            this, [this](qint64 position) {
                if (m_isTracking && position > 0) {
                    // Track the latest position but don't save every time
                    m_lastSavedPosition = position;
                    // Add periodic debug to see position updates
                    static int posUpdateCounter = 0;
                    if (++posUpdateCounter % 10 == 0) { // Log every 10th update
                        qDebug() << "VP_ShowsPlaybackTracker: Position update #" << posUpdateCounter << ": " << position << "ms";
                    }
                }
            });
    
    if (posConnection) {
        qDebug() << "VP_ShowsPlaybackTracker: Successfully connected to positionChanged signal";
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Failed to connect to positionChanged signal";
    }
    
    // Connect to aboutToClose signal to capture final position
    QMetaObject::Connection closeConnection = connect(player, &VP_Shows_Videoplayer::aboutToClose,
            this, [this](qint64 finalPosition) {
                qDebug() << "VP_ShowsPlaybackTracker: Received aboutToClose signal with position:" << finalPosition << "ms";
                if (m_isTracking) {
                    stopTracking(finalPosition);
                }
            }, Qt::DirectConnection);
    
    if (closeConnection) {
        qDebug() << "VP_ShowsPlaybackTracker: Successfully connected to aboutToClose signal";
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Failed to connect to aboutToClose signal";
    }
}

void VP_ShowsPlaybackTracker::disconnectPlayerSignals()
{
    if (m_currentPlayer) {
        qDebug() << "VP_ShowsPlaybackTracker: Disconnecting from player signals";
        disconnect(m_currentPlayer, nullptr, this, nullptr);
    }
}
