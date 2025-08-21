#include "operations_vp_shows_watchhistory.h"
#include "operations_vp_shows.h"
#include "regularplayer/videoplayer.h"
#include <QDebug>

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
    stopTracking();
}

bool Operations_VP_Shows_WatchHistory::initializeForShow(const QString& showFolderPath,
                                                        const QByteArray& encryptionKey,
                                                        const QString& username)
{
    qDebug() << "Operations_VP_Shows_WatchHistory: Initializing for show:" << showFolderPath;
    
    // Stop any existing tracking
    stopTracking();
    
    // Create new watch history instance
    try {
        m_watchHistory = std::make_unique<VP_ShowsWatchHistory>(
            showFolderPath, encryptionKey, username);
        
        // Load existing history
        if (!m_watchHistory->loadHistory()) {
            qDebug() << "Operations_VP_Shows_WatchHistory: No existing history, starting fresh";
        }
        
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Failed to initialize:" << e.what();
        m_watchHistory.reset();
        return false;
    }
}

void Operations_VP_Shows_WatchHistory::startTracking(const QString& episodePath, VideoPlayer* player)
{
    if (!m_watchHistory || !player) {
        qDebug() << "Operations_VP_Shows_WatchHistory: Cannot start tracking - not initialized";
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Starting tracking for:" << episodePath;
    
    // Stop any existing tracking
    stopTracking();
    
    // Set new tracking state
    m_currentEpisodePath = episodePath;
    m_currentPlayer = player;
    m_isTracking = true;
    m_lastSavedPosition = 0;
    
    // Do initial progress update
    updateProgress();
    
    // Start periodic updates
    m_progressTimer->start();
}

void Operations_VP_Shows_WatchHistory::stopTracking()
{
    if (!m_isTracking) {
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Stopping tracking";
    
    // Stop timer
    m_progressTimer->stop();
    
    // Do final progress update
    if (m_watchHistory && m_currentPlayer && !m_currentEpisodePath.isEmpty()) {
        updateProgress();
        m_watchHistory->saveHistory();
    }
    
    // Clear tracking state
    m_currentPlayer = nullptr;
    m_currentEpisodePath.clear();
    m_isTracking = false;
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
    if (!m_isTracking || !m_currentPlayer || !m_watchHistory || m_currentEpisodePath.isEmpty()) {
        return;
    }
    
    // Get current position and duration from player
    qint64 position = m_currentPlayer->position();
    qint64 duration = m_currentPlayer->duration();
    
    // Only update if position has changed significantly (more than 1 second)
    if (qAbs(position - m_lastSavedPosition) < 1000 && position > 0) {
        return;
    }
    
    qDebug() << "Operations_VP_Shows_WatchHistory: Updating progress -" 
             << "Episode:" << m_currentEpisodePath
             << "Position:" << position << "ms"
             << "Duration:" << duration << "ms";
    
    // Update watch progress
    m_watchHistory->updateWatchProgress(m_currentEpisodePath, position, duration);
    m_lastSavedPosition = position;
    
    // Check for completion
    checkForCompletion();
    
    // Save to disk
    if (m_watchHistory->saveHistory()) {
        emit progressSaved();
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
