#include "VP_Shows_Videoplayer.h"
#include <QGuiApplication>
#include <QDebug>
#include <QFileInfo>
#include <QCloseEvent>
#include <QShowEvent>
#include <QApplication>
#include <QTimer>
#include <QDateTime>

VP_Shows_Videoplayer::VP_Shows_Videoplayer(QWidget *parent)
    : BaseVideoPlayer(parent)
    , m_watchHistory(nullptr)
    , m_progressSaveTimer(nullptr)
    , m_lastSavedPosition(0)
    , m_hasStartedPlaying(false)
    , m_shouldRestoreFullscreen(false)
    , m_shouldRestoreMaximized(false)
    , m_shouldRestoreMinimized(false)
    , m_hasAppliedMinimized(false)
{
    qDebug() << "VP_Shows_Videoplayer: Constructor called";
    
    // Initialize progress save timer for watch history
    m_progressSaveTimer = new QTimer(this);
    m_progressSaveTimer->setInterval(5000);  // Save every 5 seconds
    connect(m_progressSaveTimer, &QTimer::timeout, this, &VP_Shows_Videoplayer::saveWatchProgress);
}

VP_Shows_Videoplayer::~VP_Shows_Videoplayer()
{
    qDebug() << "VP_Shows_Videoplayer: Destructor called";
    
    // Save final progress if needed
    if (m_hasStartedPlaying) {
        finalizeWatchProgress();
    }
    
    // Stop timer
    if (m_progressSaveTimer) {
        m_progressSaveTimer->stop();
        delete m_progressSaveTimer;
    }
}

void VP_Shows_Videoplayer::play()
{
    qDebug() << "VP_Shows_Videoplayer: Play requested (show-specific override)";
    
    // Mark that playback has started for watch history
    if (!m_hasStartedPlaying) {
        m_hasStartedPlaying = true;
        initializeWatchProgress();
        
        // Start the progress save timer
        if (m_progressSaveTimer && !m_progressSaveTimer->isActive()) {
            m_progressSaveTimer->start();
        }
    }
    
    // Call base class implementation
    BaseVideoPlayer::play();
}

void VP_Shows_Videoplayer::pause()
{
    qDebug() << "VP_Shows_Videoplayer: Pause requested (show-specific override)";
    
    // Save progress when pausing
    if (m_hasStartedPlaying) {
        saveWatchProgress();
    }
    
    // Call base class implementation
    BaseVideoPlayer::pause();
}

void VP_Shows_Videoplayer::stop()
{
    qDebug() << "VP_Shows_Videoplayer: Stop requested (show-specific override)";
    
    // Save final progress before stopping
    if (m_hasStartedPlaying) {
        finalizeWatchProgress();
    }
    
    // Stop the progress timer
    if (m_progressSaveTimer) {
        m_progressSaveTimer->stop();
    }
    
    // Call base class implementation
    BaseVideoPlayer::stop();
    
    // Note: The handlePlaybackStateChanged will close the player when it enters StoppedState
    // This prevents crash from temp file being deleted
    qDebug() << "VP_Shows_Videoplayer: Stop will trigger player close via state change handler";
}

void VP_Shows_Videoplayer::setWatchHistoryManager(VP_ShowsWatchHistory* watchHistory)
{
    qDebug() << "VP_Shows_Videoplayer: Setting watch history manager";
    m_watchHistory = watchHistory;
}

void VP_Shows_Videoplayer::setEpisodeInfo(const QString& showPath, const QString& episodePath, const QString& episodeIdentifier)
{
    qDebug() << "VP_Shows_Videoplayer: Setting episode info";
    qDebug() << "  Show path:" << showPath;
    qDebug() << "  Episode path:" << episodePath;
    qDebug() << "  Episode identifier:" << episodeIdentifier;
    
    m_showPath = showPath;
    m_episodePath = episodePath;
    m_episodeIdentifier = episodeIdentifier;
    
    // Initialize watch progress if we have history manager
    if (m_watchHistory && !m_episodePath.isEmpty()) {
        initializeWatchProgress();
    }
}

void VP_Shows_Videoplayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Close event received (show-specific override)";
    
    // Save final watch progress
    if (m_hasStartedPlaying) {
        finalizeWatchProgress();
    }
    
    // Stop the progress timer
    if (m_progressSaveTimer) {
        m_progressSaveTimer->stop();
    }
    
    // Call base class implementation
    BaseVideoPlayer::closeEvent(event);
}

void VP_Shows_Videoplayer::showEvent(QShowEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Show event received (show-specific override)";
    qDebug() << "VP_Shows_Videoplayer: Should restore - Fullscreen:" << m_shouldRestoreFullscreen
             << "Maximized:" << m_shouldRestoreMaximized << "Minimized:" << m_shouldRestoreMinimized;
    
    // Call base class implementation first
    BaseVideoPlayer::showEvent(event);
    
    // Now apply the window state based on what was stored
    // This needs to happen after the window is shown
    if (!m_isClosing) {
        // Apply window state restoration
        if (m_shouldRestoreFullscreen) {
            qDebug() << "VP_Shows_Videoplayer: Restoring fullscreen state";
            // Use a timer to ensure the window is fully shown before going fullscreen
            QTimer::singleShot(100, this, &BaseVideoPlayer::enterFullScreen);
            // Reset the flag after scheduling
            m_shouldRestoreFullscreen = false;
        } else if (m_shouldRestoreMaximized) {
            qDebug() << "VP_Shows_Videoplayer: Restoring maximized state";
            showMaximized();
            m_shouldRestoreMaximized = false;
        } else if (m_shouldRestoreMinimized) {
            // For minimized state, we don't minimize here anymore
            // Instead, we wait for the video to start playing (handled in handlePlaybackStateChanged)
            qDebug() << "VP_Shows_Videoplayer: Will minimize after video starts playing (for background listening)";
            // Don't reset m_shouldRestoreMinimized here, it's used in handlePlaybackStateChanged
        }
    }
    
    // Additional show-specific initialization if needed
    if (m_watchHistory && !m_episodePath.isEmpty()) {
        // Could restore playback position here if needed
    }
}

void VP_Shows_Videoplayer::handlePlaybackStateChanged(VP_VLCPlayer::PlayerState state)
{
    qDebug() << "VP_Shows_Videoplayer: Playback state changed (show-specific override)";
    
    // Call base class implementation first
    BaseVideoPlayer::handlePlaybackStateChanged(state);
    
    // Handle show-specific state changes
    switch (state) {
        case VP_VLCPlayer::PlayerState::Playing:
            // Start progress timer if not already started
            if (m_progressSaveTimer && !m_progressSaveTimer->isActive()) {
                m_progressSaveTimer->start();
            }
            
            // Check if we need to minimize the window for background playback
            // This ensures the video is actually playing before we minimize
            if (m_shouldRestoreMinimized && !m_hasAppliedMinimized) {
                m_hasAppliedMinimized = true;
                qDebug() << "VP_Shows_Videoplayer: Video is playing, now minimizing for background playback";
                
                // Minimize after a short delay to ensure playback is stable
                QTimer::singleShot(200, this, [this]() {
                    if (!m_isClosing) {
                        qDebug() << "VP_Shows_Videoplayer: Minimizing window (triggered by playback start)";
                        setWindowState(windowState() | Qt::WindowMinimized);
                        
                        // Verify the state
                        QTimer::singleShot(100, this, [this]() {
                            qDebug() << "VP_Shows_Videoplayer: Final window state:" << windowState()
                                     << "isMinimized():" << isMinimized();
                        });
                    }
                });
            }
            break;
            
        case VP_VLCPlayer::PlayerState::Paused:
            // Save progress when paused
            if (m_hasStartedPlaying) {
                saveWatchProgress();
            }
            break;
            
        case VP_VLCPlayer::PlayerState::Stopped:
            // Handle stopped state - close the player
            // This is show-specific behavior to prevent crash from temp file deletion
            if (!m_isClosing) {
                qDebug() << "VP_Shows_Videoplayer: Stopped state detected - closing player";
                QTimer::singleShot(100, this, &VP_Shows_Videoplayer::close);
            }
            break;
            
        default:
            break;
    }
}

void VP_Shows_Videoplayer::initializeFromPreviousSettings()
{
    qDebug() << "VP_Shows_Videoplayer: Initializing from previous settings (show-specific override)";
    
    // Call base class implementation first (handles monitor and geometry)
    BaseVideoPlayer::initializeFromPreviousSettings();
    
    // Reset the minimized application flag
    m_hasAppliedMinimized = false;
    
    // Now handle window state restoration for shows player
    // This is important for autoplay to maintain the user's preferred window state
    qDebug() << "VP_Shows_Videoplayer: Checking window state for restoration";
    qDebug() << "VP_Shows_Videoplayer: Previous state - Fullscreen:" << s_wasFullScreen
             << "Maximized:" << s_wasMaximized << "Minimized:" << s_wasMinimized;
    
    // For autoplay, we want to maintain the exact window state from the previous episode
    // The window state will be applied in showEvent after the window is visible
    // Store flags so showEvent knows what to do
    m_shouldRestoreFullscreen = s_wasFullScreen;
    m_shouldRestoreMaximized = s_wasMaximized && !s_wasFullScreen;
    m_shouldRestoreMinimized = s_wasMinimized && !s_wasFullScreen && !s_wasMaximized;
    
    qDebug() << "VP_Shows_Videoplayer: Will restore - Fullscreen:" << m_shouldRestoreFullscreen
             << "Maximized:" << m_shouldRestoreMaximized << "Minimized:" << m_shouldRestoreMinimized;
}

void VP_Shows_Videoplayer::saveWatchProgress()
{
    if (!m_watchHistory || m_episodePath.isEmpty() || !m_hasStartedPlaying) {
        return;
    }
    
    qint64 currentPosition = position();
    qint64 videoDuration = duration();
    
    // Only save if position has changed significantly (more than 1 second)
    if (!shouldUpdateProgressForShows(currentPosition)) {
        return;
    }
    
    qDebug() << "VP_Shows_Videoplayer: Saving watch progress - Position:" << currentPosition
             << "Duration:" << videoDuration;
    
    m_watchHistory->updateWatchProgress(m_episodePath, currentPosition, videoDuration, m_episodeIdentifier);
    m_watchHistory->saveHistory();
    m_lastSavedPosition = currentPosition;
}

void VP_Shows_Videoplayer::initializeWatchProgress()
{
    if (!m_watchHistory || m_episodePath.isEmpty()) {
        return;
    }
    
    qDebug() << "VP_Shows_Videoplayer: Initializing watch progress for episode:" << m_episodePath;
    
    // Get existing progress if any
    qint64 savedPosition = m_watchHistory->getResumePosition(m_episodePath);
    if (savedPosition > 0) {
        qDebug() << "VP_Shows_Videoplayer: Found saved position:" << savedPosition << "ms";
        // We could restore position here, but that's handled by Operations_VP_Shows
    }
}

void VP_Shows_Videoplayer::finalizeWatchProgress()
{
    if (!m_watchHistory || m_episodePath.isEmpty() || !m_hasStartedPlaying) {
        return;
    }
    
    qint64 finalPosition = position();
    qint64 videoDuration = duration();
    
    qDebug() << "VP_Shows_Videoplayer: Finalizing watch progress - Position:" << finalPosition
             << "Duration:" << videoDuration;
    
    // Always save final position
    m_watchHistory->updateWatchProgress(m_episodePath, finalPosition, videoDuration, m_episodeIdentifier);
    m_watchHistory->saveHistory();
    
    // Check if episode was completed
    if (videoDuration > 0) {
        qreal progressPercentage = (static_cast<qreal>(finalPosition) / videoDuration) * 100.0;
        if (progressPercentage >= 90.0) {
            qDebug() << "VP_Shows_Videoplayer: Episode completed (>90% watched)";
            m_watchHistory->markEpisodeCompleted(m_episodePath);
            m_watchHistory->saveHistory();
        }
    }
}

bool VP_Shows_Videoplayer::shouldUpdateProgressForShows(qint64 currentPosition) const
{
    // Only update if position has changed by more than 1 second
    qint64 difference = qAbs(currentPosition - m_lastSavedPosition);
    return difference > 1000;  // 1000ms = 1 second
}

// Static method implementation
void VP_Shows_Videoplayer::resetStoredWindowSettings()
{
    // For manual play, we only reset the window state flags
    // We keep the monitor, volume, and geometry for consistency
    s_wasFullScreen = false;
    s_wasMaximized = false;
    s_wasMinimized = false;
    // Keep s_hasStoredSettings = true so monitor restoration still works
    // Keep s_lastWindowGeometry for positioning
    // Keep s_lastVolume for volume persistence
    // Keep s_lastUsedScreen for monitor persistence
    qDebug() << "VP_Shows_Videoplayer: Reset window state flags for manual play, keeping monitor and volume";
}
