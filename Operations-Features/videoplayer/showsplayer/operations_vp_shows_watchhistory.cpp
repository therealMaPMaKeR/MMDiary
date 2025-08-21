#include "operations_vp_shows_watchhistory.h"
#include "operations_vp_shows.h"
#include "VP_Shows_Videoplayer.h"
#include <QDebug>
#include <QTimer>
#include <QDir>

Operations_VP_Shows_WatchHistory::Operations_VP_Shows_WatchHistory(Operations_VP_Shows* parent)
    : QObject(parent)
    , m_parent(parent)
    , m_progressTimer(new QTimer(this))
    , m_currentPlayer(nullptr)
    , m_isTracking(false)
    , m_lastSavedPosition(0)
{
    qDebug() << "Operations_VP_Shows_WatchHistory: Initializing watch history integration";
    
    // Setup progress update timer
    m_progressTimer->setInterval(VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000);
    connect(m_progressTimer, &QTimer::timeout, this, &Operations_VP_Shows_WatchHistory::updateProgress);
}

Operations_VP_Shows_WatchHistory::~Operations_VP_Shows_WatchHistory()
{
    qDebug() << "Operations_VP_Shows_WatchHistory: Destroying watch history integration";
    // Only call stopTracking if we're still tracking and haven't saved yet
    // The aboutToClose signal should have already handled saving
    if (m_isTracking) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Still tracking in destructor, calling stopTracking";
        stopTracking();
    } else {
        qDebug() << "Operations_VP_Shows_WatchHistory: Not tracking in destructor, position already saved";
    }
}

bool Operations_VP_Shows_WatchHistory::initializeForShow(const QString& showFolderPath,
                                                        const QByteArray& encryptionKey,
                                                        const QString& username)
{
    qDebug() << "Operations_VP_Shows_WatchHistory: ============================================";
    qDebug() << "Operations_VP_Shows_WatchHistory: Initializing for show:" << showFolderPath;
    qDebug() << "Operations_VP_Shows_WatchHistory: Username:" << username;
    qDebug() << "Operations_VP_Shows_WatchHistory: Encryption key present:" << !encryptionKey.isEmpty();
    qDebug() << "Operations_VP_Shows_WatchHistory: Encryption key length:" << encryptionKey.length();
    
    // Convert to absolute path
    QString absoluteShowPath = QDir(showFolderPath).absolutePath();
    qDebug() << "Operations_VP_Shows_WatchHistory: Absolute show path:" << absoluteShowPath;
    
    // Check if show folder exists
    QDir showDir(absoluteShowPath);
    if (!showDir.exists()) {
        qDebug() << "Operations_VP_Shows_WatchHistory: WARNING - Show folder does not exist:" << absoluteShowPath;
        // Try to create the folder
        if (showDir.mkpath(".")) {
            qDebug() << "Operations_VP_Shows_WatchHistory: Created show folder successfully";
        } else {
            qDebug() << "Operations_VP_Shows_WatchHistory: ERROR - Failed to create show folder";
        }
    } else {
        qDebug() << "Operations_VP_Shows_WatchHistory: Show folder exists";
        QStringList allFiles = showDir.entryList(QDir::Files | QDir::Hidden);
        qDebug() << "Operations_VP_Shows_WatchHistory: Folder files (including hidden):" << allFiles;
        
        // Check specifically for watch history file
        QString historyFileName = QString(".show_history.encrypted");
        if (allFiles.contains(historyFileName)) {
            qDebug() << "Operations_VP_Shows_WatchHistory: Watch history file exists in folder";
        } else {
            qDebug() << "Operations_VP_Shows_WatchHistory: Watch history file NOT found in folder";
        }
    }
    
    // Stop any existing tracking
    stopTracking();
    
    // Create new watch history instance
    try {
        qDebug() << "Operations_VP_Shows_WatchHistory: Creating VP_ShowsWatchHistory instance with absolute path...";
        m_watchHistory = std::make_unique<VP_ShowsWatchHistory>(
            absoluteShowPath, encryptionKey, username);  // Use absolute path
        
        qDebug() << "Operations_VP_Shows_WatchHistory: Instance created, loading history...";
        
        // Load existing history
        if (!m_watchHistory->loadHistory()) {
            qDebug() << "Operations_VP_Shows_WatchHistory: No existing history, starting fresh";
            // Try to save initial empty history
            if (m_watchHistory->saveHistory()) {
                qDebug() << "Operations_VP_Shows_WatchHistory: Initial empty history saved successfully";
            } else {
                qDebug() << "Operations_VP_Shows_WatchHistory: WARNING - Failed to save initial history";
            }
        } else {
            qDebug() << "Operations_VP_Shows_WatchHistory: History loaded successfully";
        }
        
        qDebug() << "Operations_VP_Shows_WatchHistory: Initialization complete";
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Exception during initialization:" << e.what();
        m_watchHistory.reset();
        return false;
    } catch (...) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Unknown exception during initialization";
        m_watchHistory.reset();
        return false;
    }
}

void Operations_VP_Shows_WatchHistory::startTracking(const QString& episodePath, VP_Shows_Videoplayer *player)
{
    qDebug() << "Operations_VP_Shows_WatchHistory: ============================================";
    qDebug() << "Operations_VP_Shows_WatchHistory: startTracking called";
    qDebug() << "Operations_VP_Shows_WatchHistory: Episode path:" << episodePath;
    qDebug() << "Operations_VP_Shows_WatchHistory: Player valid:" << (player != nullptr);
    qDebug() << "Operations_VP_Shows_WatchHistory: Watch history valid:" << (m_watchHistory != nullptr);
    
    if (!m_watchHistory || !player) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Cannot start tracking - not initialized";
        qDebug() << "Operations_VP_Shows_WatchHistory: m_watchHistory:" << m_watchHistory.get();
        qDebug() << "Operations_VP_Shows_WatchHistory: player:" << player;
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Starting tracking for:" << episodePath;
    
    // Stop any existing tracking
    stopTracking();
    
    // Set new tracking state
    m_currentEpisodePath = episodePath;
    m_currentPlayer = player;
    m_isTracking = true;
    
    // Capture initial position (might be 0 if video hasn't started yet)
    qint64 initialPosition = player->position();
    m_lastSavedPosition = initialPosition;
    qDebug() << "Operations_VP_Shows_WatchHistory: Initial position captured:" << initialPosition << "ms";
    
    // If initial position is 0, check for resume position
    if (initialPosition == 0 && m_watchHistory) {
        qint64 resumePos = m_watchHistory->getResumePosition(episodePath);
        if (resumePos > 0) {
            m_lastSavedPosition = resumePos;
            qDebug() << "Operations_VP_Shows_WatchHistory: Using resume position as last saved:" << resumePos << "ms";
        }
    }
    
    // Connect to player's position changed signal for immediate tracking
    // This ensures we capture position changes even between timer intervals
    connect(player, &VP_Shows_Videoplayer::positionChanged,
            this, [this](qint64 position) {
                if (m_isTracking && position > 0) {
                    // Always track the latest position (but don't save to DB every time)
                    m_lastSavedPosition = position;
                    // Only log every 1000ms to reduce noise
                    if (position % 1000 < 100) {
                        qDebug() << "Operations_VP_Shows_WatchHistory: Position tracked from signal:" << position << "ms";
                    }
                }
            });
    
    // CRITICAL: Connect to aboutToClose signal to capture final position
    // Use Qt::DirectConnection to ensure this runs immediately
    connect(player, &VP_Shows_Videoplayer::aboutToClose,
            this, [this](qint64 finalPosition) {
                qDebug() << "Operations_VP_Shows_WatchHistory: Received aboutToClose signal with position:" << finalPosition << "ms";
                // Immediately stop tracking with the provided position
                if (m_isTracking) {
                    stopTracking(finalPosition);
                }
            }, Qt::DirectConnection);
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Tracking state set";
    qDebug() << "Operations_VP_Shows_WatchHistory: Timer interval:" << m_progressTimer->interval() << "ms";
    
    // REMOVED: Resume position logic - this is now handled by operations_vp_shows.cpp
    // The resume position is set in operations_vp_shows.cpp::decryptAndPlayEpisode() after video loads
    // This prevents the double-setting issue where position was set twice
    
    // Check if we have a resume position (for tracking purposes only, not for setting)
    qint64 resumePosition = m_watchHistory->getResumePosition(episodePath);
    if (resumePosition > 0) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Episode has resume position:" << resumePosition << "ms (handled externally)";
    }
    
    // Skip initial progress update if we have a resume position
    // This prevents overwriting the saved position with 0
    if (resumePosition == 0) {
        // Do initial progress update only if starting from beginning
        qDebug() << "Operations_VP_Shows_WatchHistory: Performing initial progress update...";
        updateProgress();
    } else {
        qDebug() << "Operations_VP_Shows_WatchHistory: Skipping initial update, resuming from saved position";
    }
    
    // Start periodic updates with a shorter initial interval
    // Use 2 seconds for the first save to ensure we capture early positions
    qDebug() << "Operations_VP_Shows_WatchHistory: Starting periodic timer with initial 2-second interval...";
    m_progressTimer->setInterval(2000);  // 2 seconds for first save
    m_progressTimer->start();
    
    // After first save, switch to normal interval
    QTimer::singleShot(2100, [this]() {
        if (m_progressTimer->isActive()) {
            m_progressTimer->setInterval(VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS * 1000);
            qDebug() << "Operations_VP_Shows_WatchHistory: Switched to normal save interval:" 
                     << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds";
        }
    });
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Timer started, tracking is active";
}

void Operations_VP_Shows_WatchHistory::stopTracking(qint64 finalPosition)
{
    if (!m_isTracking) {
        qDebug() << "Operations_VP_Shows_WatchHistory: stopTracking called but not tracking, returning";
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: ============================================";
    qDebug() << "Operations_VP_Shows_WatchHistory: Stopping tracking";
    qDebug() << "Operations_VP_Shows_WatchHistory: Final position parameter:" << finalPosition << "ms";
    
    // CRITICAL: Set tracking to false immediately to prevent double calls
    m_isTracking = false;
    
    // Stop timer first to prevent any more automatic updates
    m_progressTimer->stop();
    
    // Do final progress update
    if (m_watchHistory && !m_currentEpisodePath.isEmpty()) {
        qint64 position = finalPosition;  // Use provided position if available
        qint64 duration = 0;
        
        // If no position was provided, try to get it from the player
        if (finalPosition < 0 && m_currentPlayer) {
            position = m_currentPlayer->position();
            duration = m_currentPlayer->duration();
        } else if (m_currentPlayer) {
            // We have a provided position, just get the duration
            duration = m_currentPlayer->duration();
        }
        
        qDebug() << "Operations_VP_Shows_WatchHistory: Final position provided:" << finalPosition << "ms";
        qDebug() << "Operations_VP_Shows_WatchHistory: Position to save:" << position << "ms";
        qDebug() << "Operations_VP_Shows_WatchHistory: Last saved position:" << m_lastSavedPosition << "ms";
        qDebug() << "Operations_VP_Shows_WatchHistory: Duration:" << duration << "ms";
        
        // If position is 0 or negative but we have a valid last saved position, use that instead
        if (position <= 0 && m_lastSavedPosition > 0) {
            position = m_lastSavedPosition;
            qDebug() << "Operations_VP_Shows_WatchHistory: Using last saved position:" << position << "ms";
        }
        
        // Always save the best position we have
        if (position > 0) {
            qDebug() << "Operations_VP_Shows_WatchHistory: Saving final position:" << position << "ms";
            m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
            m_watchHistory->saveHistory();
        } else if (position == 0 && m_lastSavedPosition == 0) {
            // We're genuinely at the start
            qDebug() << "Operations_VP_Shows_WatchHistory: Saving position 0 (at start)";
            m_watchHistory->updateWatchProgress(m_currentEpisodePath, 0, duration);
            m_watchHistory->saveHistory();
        } else {
            // Just ensure any pending changes are persisted
            qDebug() << "Operations_VP_Shows_WatchHistory: Ensuring history is saved";
            m_watchHistory->saveHistory();
        }
    }
    
    // Disconnect from player signals before clearing
    if (m_currentPlayer) {
        disconnect(m_currentPlayer, nullptr, this, nullptr);
    }
    
    // Clear tracking state
    m_currentPlayer = nullptr;
    m_currentEpisodePath.clear();
    // m_isTracking already set to false at the beginning
    m_lastSavedPosition = 0;
}

qint64 Operations_VP_Shows_WatchHistory::getResumePosition(const QString& episodePath) const
{
    if (!m_watchHistory) {
        return 0;
    }
    
    qint64 position = m_watchHistory->getResumePosition(episodePath);
    qDebug() << "Operations_VP_Shows_WatchHistory: Resume position for" << episodePath 
             << "is" << position << "ms";
    return position;
}

QString Operations_VP_Shows_WatchHistory::getNextEpisode(const QString& currentEpisodePath,
                                                        const QStringList& availableEpisodes) const
{
    if (!m_watchHistory) {
        return QString();
    }
    
    QString nextEpisode = m_watchHistory->getNextUnwatchedEpisode(currentEpisodePath, availableEpisodes);
    qDebug() << "Operations_VP_Shows_WatchHistory: Next episode after" << currentEpisodePath 
             << "is" << (nextEpisode.isEmpty() ? "none" : nextEpisode);
    return nextEpisode;
}

bool Operations_VP_Shows_WatchHistory::isAutoplayEnabled() const
{
    if (!m_watchHistory) {
        return false;
    }
    
    return m_watchHistory->isAutoplayEnabled();
}

void Operations_VP_Shows_WatchHistory::setAutoplayEnabled(bool enabled)
{
    if (!m_watchHistory) {
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Setting autoplay to:" << enabled;
    m_watchHistory->setAutoplayEnabled(enabled);
    m_watchHistory->saveHistory();
}

bool Operations_VP_Shows_WatchHistory::clearHistory()
{
    if (!m_watchHistory) {
        return false;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Clearing watch history";
    return m_watchHistory->clearHistory();
}

QString Operations_VP_Shows_WatchHistory::getLastWatchedEpisode() const
{
    if (!m_watchHistory) {
        return QString();
    }
    
    QString lastWatched = m_watchHistory->getLastWatchedEpisode();
    qDebug() << "Operations_VP_Shows_WatchHistory: Last watched episode:" 
             << (lastWatched.isEmpty() ? "none" : lastWatched);
    return lastWatched;
}

void Operations_VP_Shows_WatchHistory::markCurrentEpisodeCompleted()
{
    if (!m_watchHistory || m_currentEpisodePath.isEmpty()) {
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Marking episode as completed:" << m_currentEpisodePath;
    m_watchHistory->markEpisodeCompleted(m_currentEpisodePath);
    m_watchHistory->saveHistory();
    
    emit episodeCompleted(m_currentEpisodePath);
}

void Operations_VP_Shows_WatchHistory::updateProgress()
{
    qDebug() << "Operations_VP_Shows_WatchHistory: updateProgress called";
    qDebug() << "Operations_VP_Shows_WatchHistory: Is tracking:" << m_isTracking;
    qDebug() << "Operations_VP_Shows_WatchHistory: Current player valid:" << (m_currentPlayer != nullptr);
    qDebug() << "Operations_VP_Shows_WatchHistory: Watch history valid:" << (m_watchHistory != nullptr);
    qDebug() << "Operations_VP_Shows_WatchHistory: Current episode path:" << m_currentEpisodePath;
    
    if (!m_isTracking || !m_currentPlayer || !m_watchHistory || m_currentEpisodePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Cannot update progress - missing required components";
        return;
    }
    
    // Get current position and duration from player
    qint64 position = m_currentPlayer->position();
    qint64 duration = m_currentPlayer->duration();
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Player position:" << position << "ms";
    qDebug() << "Operations_VP_Shows_WatchHistory: Player duration:" << duration << "ms";
    qDebug() << "Operations_VP_Shows_WatchHistory: Last saved position:" << m_lastSavedPosition << "ms";
    qDebug() << "Operations_VP_Shows_WatchHistory: Position difference:" << qAbs(position - m_lastSavedPosition) << "ms";
    
    // Special case: if position is 0 but we have a saved position (resume case), don't overwrite
    if (position == 0 && m_lastSavedPosition > 0) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Position is 0 but have saved position" << m_lastSavedPosition << "ms, skipping save to avoid overwrite";
        return;
    }
    
    // Special case: if this is the first update and position is still 0, skip saving
    if (position == 0 && m_lastSavedPosition == 0) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Position still at 0, skipping save";
        return;
    }
    
    // Always track the latest position for when stopTracking is called
    qint64 previousSavedPosition = m_lastSavedPosition;
    if (position > 0) {
        m_lastSavedPosition = position;
    }
    
    // Only update database if position has changed significantly (more than 1 second)
    // Use previousSavedPosition for comparison since we just updated m_lastSavedPosition
    bool shouldSave = (qAbs(position - previousSavedPosition) >= 1000);
    
    if (!shouldSave) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Position change too small, skipping database update but position tracked";
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Updating progress -" 
             << "Episode:" << m_currentEpisodePath
             << "Position:" << position << "ms"
             << "Duration:" << duration << "ms";
    
    // Update watch progress
    qDebug() << "Operations_VP_Shows_WatchHistory: Calling updateWatchProgress...";
    m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
    
    // Check for completion
    qDebug() << "Operations_VP_Shows_WatchHistory: Checking for completion...";
    checkForCompletion();
    
    // Save to disk
    qDebug() << "Operations_VP_Shows_WatchHistory: Saving history to disk...";
    if (m_watchHistory->saveHistory()) {
        qDebug() << "Operations_VP_Shows_WatchHistory: History saved successfully";
        emit progressSaved();
    } else {
        qDebug() << "Operations_VP_Shows_WatchHistory: WARNING - Failed to save history";
    }
}

void Operations_VP_Shows_WatchHistory::checkForCompletion()
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
        // Check if we haven't already emitted this signal
        static QString lastCompletedEpisode;
        if (lastCompletedEpisode != m_currentEpisodePath) {
            qDebug() << "Operations_VP_Shows_WatchHistory: Episode near completion:" 
                     << m_currentEpisodePath;
            emit episodeNearCompletion(m_currentEpisodePath);
            lastCompletedEpisode = m_currentEpisodePath;
        }
        
        // If within 10 seconds of end, mark as completed
        if (remaining <= 10000) {
            markCurrentEpisodeCompleted();
        }
    }
}
