#include "vp_shows_playback_tracker.h"
#include "operations_vp_shows.h"
#include "VP_Shows_Videoplayer.h"
#include <QDebug>
#include <QDir>

VP_ShowsPlaybackTracker::VP_ShowsPlaybackTracker(Operations_VP_Shows* parent)
    : QObject(parent)
    , m_parent(parent)
    , m_progressTimer(new SafeTimer(this, "VP_ShowsPlaybackTracker"))
    , m_currentPlayer(nullptr)
    , m_isTracking(false)
    , m_lastSavedPosition(0)
    , m_lastNearCompletionEpisode()
    , m_trackingSessionId(0)
{
    qDebug() << "VP_ShowsPlaybackTracker: Initializing playback tracker";
    
    // Setup progress update timer
    int intervalMs = VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000;
    m_progressTimer->setInterval(intervalMs);
    qDebug() << "VP_ShowsPlaybackTracker: Timer interval set to:" << intervalMs << "ms (" << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds)";
    
    // SafeTimer uses callbacks directly, no need for signal connection
    qDebug() << "VP_ShowsPlaybackTracker: SafeTimer initialized with interval" << intervalMs << "ms";
    
    // Verify COMPLETION_THRESHOLD_MS value
    qDebug() << "VP_ShowsPlaybackTracker: COMPLETION_THRESHOLD_MS =" << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS << "ms (" << (VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS / 1000) << "seconds)";
}

VP_ShowsPlaybackTracker::~VP_ShowsPlaybackTracker()
{
    qDebug() << "VP_ShowsPlaybackTracker: Destroying playback tracker";
    
    // Invalidate session to prevent any pending lambdas from executing
    m_trackingSessionId = -1;
    
    // Stop tracking if still active
    if (m_isTracking) {
        qDebug() << "VP_ShowsPlaybackTracker: Still tracking in destructor, calling stopTracking";
        stopTracking();
    }
    
    // Ensure timer is deleted properly
    if (m_progressTimer) {
        m_progressTimer->stop();
        disconnect(m_progressTimer, nullptr, this, nullptr);
        delete m_progressTimer;
        m_progressTimer = nullptr;
    }
    
    // Clear the player pointer
    m_currentPlayer = nullptr;
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
            if (m_watchHistory->saveHistoryWithBackup()) {
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
    
    // Increment tracking session ID for this new session
    m_trackingSessionId++;
    int currentSessionId = m_trackingSessionId;
    qDebug() << "VP_ShowsPlaybackTracker: Starting new tracking session ID:" << currentSessionId;
    
    // Set new tracking state
    m_currentEpisodePath = episodePath;
    m_currentPlayer = player;
    m_isTracking = true;
    
    // Reset the near completion tracking for the new episode
    m_lastNearCompletionEpisode.clear();
    qDebug() << "VP_ShowsPlaybackTracker: Reset near-completion tracking for new episode";
    
    // Connect to player signals with current session ID
    connectPlayerSignals(player, currentSessionId);
    
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
    
    // Start periodic updates with appropriate interval based on position
    // If we're starting near the end, use a faster interval
    int initialInterval = 2000; // Default 2 seconds for first save
    if (initialDuration > 0) {
        qint64 remainingTime = initialDuration - initialPosition;
        if (remainingTime > 0 && remainingTime <= 180000) { // Within 3 minutes of end
            initialInterval = 1000; // 1 second interval when starting near end
            qDebug() << "VP_ShowsPlaybackTracker: Starting near end - using 1-second interval";
        }
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Starting periodic timer with" << initialInterval << "ms interval...";
    if (m_progressTimer) {
        m_progressTimer->setInterval(initialInterval);
        bool started = m_progressTimer->start([this]() {
            this->updateProgress();
        });
        
        if (started && m_progressTimer->isActive()) {
            qDebug() << "VP_ShowsPlaybackTracker: Timer successfully started and is active";
        } else {
            qDebug() << "VP_ShowsPlaybackTracker: ERROR - Timer failed to start!";
        }
        
        // Only schedule interval switch if we didn't start near the end
        if (initialInterval != 1000) {
            // After first save, switch to normal interval
            // Capture the session ID to ensure this lambda only runs for the current session
            SafeTimer::singleShot(2100, this, [this, currentSessionId, initialDuration]() {
                // Check if we're still in the same tracking session
                if (m_trackingSessionId != currentSessionId) {
                    qDebug() << "VP_ShowsPlaybackTracker: Ignoring timer interval switch - session ID mismatch"
                             << "(current:" << m_trackingSessionId << "expected:" << currentSessionId << ")";
                    return;
                }
                
                // Now safe to check timer state
                if (m_progressTimer && m_progressTimer->isActive()) {
                    // Get current position to check if we're near the end
                    qint64 currentPos = m_currentPlayer ? m_currentPlayer->position() : 0;
                    qint64 remainingTime = initialDuration - currentPos;
                    
                    // Only switch to normal interval if we're NOT near the end
                    if (remainingTime > 180000) { // More than 3 minutes remaining
                        int normalInterval = VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000;
                        m_progressTimer->setInterval(normalInterval);
                        qDebug() << "VP_ShowsPlaybackTracker: Switched to normal save interval:" 
                                 << normalInterval << "ms (" << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds)";
                    } else {
                        qDebug() << "VP_ShowsPlaybackTracker: Staying with fast interval - near end";
                    }
                } else {
                    qDebug() << "VP_ShowsPlaybackTracker: Timer not active when trying to switch interval";
                }
            });
        }
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Timer is null, cannot start tracking!";
    }
    
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
    
    // Stop timer first (with null check)
    if (m_progressTimer) {
        m_progressTimer->stop();
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: Progress timer is null or deleted";
    }
    
    // Do final progress update
    if (m_watchHistory && !m_currentEpisodePath.isEmpty()) {
        qint64 position = finalPosition;
        qint64 duration = 0;
        
        // If no position was provided, try to get it from the player
        if (finalPosition < 0 && !m_currentPlayer.isNull()) {
            position = m_currentPlayer->position();
            duration = m_currentPlayer->duration();
        } else if (!m_currentPlayer.isNull()) {
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
            m_watchHistory->saveHistoryWithBackup();
        } else {
            // Just ensure any pending changes are persisted
            qDebug() << "VP_ShowsPlaybackTracker: Ensuring history is saved with backup";
            m_watchHistory->saveHistoryWithBackup();
        }
    }
    
    // Disconnect from player signals
    disconnectPlayerSignals();
    
    // Clear tracking state
    m_currentPlayer = nullptr;
    m_currentEpisodePath.clear();
    m_lastSavedPosition = 0;
    m_lastNearCompletionEpisode.clear();
    
    // Increment session ID to invalidate any pending lambdas
    m_trackingSessionId++;
    qDebug() << "VP_ShowsPlaybackTracker: Session ID incremented to" << m_trackingSessionId << "to invalidate pending operations";
    
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
    m_watchHistory->saveHistoryWithBackup();
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
    m_watchHistory->saveHistoryWithBackup();
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
    
    qDebug() << "VP_ShowsPlaybackTracker: Manually saving history with backup";
    return m_watchHistory->saveHistoryWithBackup();
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
    m_watchHistory->saveHistoryWithBackup();
    
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
    m_watchHistory->saveHistoryWithBackup();
    
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
    m_watchHistory->saveHistoryWithBackup();
    
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
    m_watchHistory->saveHistoryWithBackup();
}

void VP_ShowsPlaybackTracker::resetEpisodePosition(const QString& episodePath)
{
    if (!m_watchHistory) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot reset position - watch history not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Resetting position for episode:" << episodePath;
    m_watchHistory->resetEpisodePosition(episodePath);
    m_watchHistory->saveHistoryWithBackup();
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
    
    if (!m_isTracking || !m_currentPlayer || m_currentPlayer.isNull() || !m_watchHistory || m_currentEpisodePath.isEmpty()) {
        qDebug() << "VP_ShowsPlaybackTracker: Cannot update progress - missing required components";
        qDebug() << "VP_ShowsPlaybackTracker: Tracking:" << m_isTracking 
                 << "Player valid:" << (!m_currentPlayer.isNull())
                 << "History valid:" << (m_watchHistory != nullptr)
                 << "Episode empty:" << m_currentEpisodePath.isEmpty();
        return;
    }
    
    // Get current position and duration from player (player already checked above)
    qint64 position = m_currentPlayer->position();
    qint64 duration = m_currentPlayer->duration();
    
    // Check if we have valid duration
    if (duration <= 0) {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Duration is invalid:" << duration << "ms";
        qDebug() << "VP_ShowsPlaybackTracker: Cannot proceed without valid duration";
        return;
    }
    
    // CRITICAL FIX: Dynamically adjust timer frequency based on position
    qint64 remainingTime = duration - position;
    if (m_progressTimer) {
        int currentInterval = m_progressTimer->interval();
        
        if (remainingTime > 0 && remainingTime <= 180000) { // Within 3 minutes of end
            if (currentInterval > 1000) { // Switch to 1-second interval
                m_progressTimer->setInterval(1000);
                qDebug() << "VP_ShowsPlaybackTracker: Near end (" << (remainingTime/1000) 
                         << "s remaining) - switched to 1-second interval for accurate detection";
            }
        } else if (remainingTime > 180000) { // More than 3 minutes remaining
            int normalInterval = VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000;
            if (currentInterval != normalInterval) { // Reset to normal interval
                m_progressTimer->setInterval(normalInterval);
                qDebug() << "VP_ShowsPlaybackTracker: Not near end - reset to normal" 
                         << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "second interval";
            }
        }
    }
    
    // CRITICAL: Check for near-completion FIRST, before any other early returns
    // This ensures we don't miss the near-completion window
    if (duration > 0) {
        // Note: remainingTime already calculated above
        
        // Check if near end (within 2 minutes)
        if (remainingTime <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && remainingTime > 0) {
            // Only emit once per episode
            if (m_lastNearCompletionEpisode != m_currentEpisodePath) {
                qDebug() << "VP_ShowsPlaybackTracker: *** EPISODE NEAR COMPLETION DETECTED ***";
                qDebug() << "VP_ShowsPlaybackTracker: Remaining time:" << remainingTime << "ms (" << (remainingTime/1000) << "seconds)";
                qDebug() << "VP_ShowsPlaybackTracker: Emitting episodeNearCompletion signal for:" << m_currentEpisodePath;
                emit episodeNearCompletion(m_currentEpisodePath);
                m_lastNearCompletionEpisode = m_currentEpisodePath;
            }
        } else if (remainingTime > VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && 
                   m_lastNearCompletionEpisode == m_currentEpisodePath) {
            // If we moved back out of the near-completion zone (e.g., user seeked backward)
            // Reset the flag so we can emit again if they come back
            qDebug() << "VP_ShowsPlaybackTracker: Moved out of near-completion zone, resetting flag";
            m_lastNearCompletionEpisode.clear();
        }
    }
    
    // Special case: if position is 0 but we have a saved position, don't overwrite
    if (position == 0 && m_lastSavedPosition > 0) {
        qDebug() << "VP_ShowsPlaybackTracker: Position is 0 but have saved position, skipping save";
        return;
    }
    
    // Skip if position hasn't changed significantly
    if (position > 0 && qAbs(position - m_lastSavedPosition) < 1000) {
        // Note: We already checked for near-completion above, so it's safe to return here
        return;
    }
    
    // Update last saved position
    m_lastSavedPosition = position;
    
    // Log position updates when near end for debugging
    if (remainingTime <= 180000 && updateCallCount % 5 == 0) { // Log every 5th update when near end
        qDebug() << "VP_ShowsPlaybackTracker: Position update - Pos:" << (position/1000) << "s / Duration:" 
                 << (duration/1000) << "s - Remaining:" << (remainingTime/1000) << "s";
    }
    
    // Update watch progress
    m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
    
    // Check if within COMPLETION_THRESHOLD_MS of end to mark as completed
    if (duration > 0) {
        if (remainingTime <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && remainingTime > 0) {
            qDebug() << "VP_ShowsPlaybackTracker: Within " << (VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS/1000) 
                     << " seconds of end, marking as completed";
            markCurrentEpisodeCompleted();
        }
    }
    
    // Save to disk with backup for safety
    if (!m_watchHistory->saveHistoryWithBackup()) {
        qDebug() << "VP_ShowsPlaybackTracker: WARNING - Failed to save history";
    } else {
        emit progressSaved();
    }
}

// checkForCompletion functionality has been integrated directly into updateProgress()

void VP_ShowsPlaybackTracker::connectPlayerSignals(VP_Shows_Videoplayer* player, int sessionId)
{
    if (!player) {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Player is null, cannot connect signals";
        return;
    }
    
    qDebug() << "VP_ShowsPlaybackTracker: Connecting to player signals for session" << sessionId;
    qDebug() << "VP_ShowsPlaybackTracker: Player pointer:" << player;
    
    // Store player as QPointer for safer lambda access
    QPointer<VP_Shows_Videoplayer> safePlayer = player;
    
    // Connect to position changed for continuous tracking
    QMetaObject::Connection posConnection = connect(player, &VP_Shows_Videoplayer::positionChanged,
            this, [this, sessionId](qint64 position) {
                // Validate we're still in the same tracking session
                if (m_trackingSessionId != sessionId) {
                    return; // Ignore signal from old session
                }
                
                if (m_isTracking && position > 0) {
                    // Detect large position jumps (seeks)
                    qint64 positionJump = qAbs(position - m_lastSavedPosition);
                    
                    // If position jumped more than 5 seconds, it's likely a seek
                    // This catches manual skips, keyboard arrow seeks, and slider drags
                    if (positionJump > 5000) {
                        // Check if we seeked to near the end
                        if (!m_currentPlayer.isNull()) {
                            qint64 duration = m_currentPlayer->duration();
                            if (duration > 0) {
                                qint64 remaining = duration - position;
                                
                                // If we seeked to within 2 minutes of the end
                                if (remaining <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && remaining > 0) {
                                    if (m_lastNearCompletionEpisode != m_currentEpisodePath) {
                                        qDebug() << "VP_ShowsPlaybackTracker: *** SEEK DETECTED TO NEAR END ***";
                                        qDebug() << "VP_ShowsPlaybackTracker: Position jumped" << (positionJump/1000) << "seconds";
                                        qDebug() << "VP_ShowsPlaybackTracker: Remaining time after seek:" << remaining << "ms (" << (remaining/1000) << "seconds)";
                                        qDebug() << "VP_ShowsPlaybackTracker: Emitting episodeNearCompletion signal";
                                        emit episodeNearCompletion(m_currentEpisodePath);
                                        m_lastNearCompletionEpisode = m_currentEpisodePath;
                                    }
                                } else if (remaining > VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && 
                                          m_lastNearCompletionEpisode == m_currentEpisodePath) {
                                    // If we seeked back out of the near-completion zone
                                    qDebug() << "VP_ShowsPlaybackTracker: Seeked out of near-completion zone, resetting flag";
                                    m_lastNearCompletionEpisode.clear();
                                }
                            }
                        }
                    }
                    
                    // Track the latest position
                    m_lastSavedPosition = position;
                }
            });
    
    if (posConnection) {
        qDebug() << "VP_ShowsPlaybackTracker: Successfully connected to positionChanged signal";
    } else {
        qDebug() << "VP_ShowsPlaybackTracker: ERROR - Failed to connect to positionChanged signal";
    }
    
    // Connect to aboutToClose signal to capture final position
    QMetaObject::Connection closeConnection = connect(player, &VP_Shows_Videoplayer::aboutToClose,
            this, [this, sessionId](qint64 finalPosition) {
                // Validate we're still in the same tracking session
                if (m_trackingSessionId != sessionId) {
                    qDebug() << "VP_ShowsPlaybackTracker: Ignoring aboutToClose from old session"
                             << "(current:" << m_trackingSessionId << "signal from:" << sessionId << ")";
                    return; // Ignore signal from old session
                }
                
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
    if (!m_currentPlayer.isNull()) {
        qDebug() << "VP_ShowsPlaybackTracker: Disconnecting from player signals";
        disconnect(m_currentPlayer, nullptr, this, nullptr);
    }
}
