#include "VP_Shows_Videoplayer.h"
#include <QGuiApplication>
#include <QDebug>
#include <QFileInfo>
#include <QCloseEvent>
#include <QShowEvent>
#include <QApplication>
#include <QDateTime>
#include <QWindowStateChangeEvent>

VP_Shows_Videoplayer::VP_Shows_Videoplayer(QWidget *parent)
    : BaseVideoPlayer(parent)
    , m_watchHistory(nullptr)
    , m_progressSaveTimer(nullptr)
    , m_lastSavedPosition(0)
    , m_hasStartedPlaying(false)
    , m_shouldRestoreFullscreen(false)
    , m_shouldRestoreMaximized(false)
    , m_shouldRestoreMinimized(false)
    , m_minimizeTimer(nullptr)
    , m_hasBeenMinimized(false)
{
    qDebug() << "VP_Shows_Videoplayer: Constructor called";
    
    // Initialize progress save timer for watch history
    m_progressSaveTimer = new SafeTimer(this, "VP_Shows_Videoplayer::progressSaveTimer");
    m_progressSaveTimer->setInterval(5000);  // Save every 5 seconds
}

VP_Shows_Videoplayer::~VP_Shows_Videoplayer()
{
    qDebug() << "VP_Shows_Videoplayer: Destructor called";
    
    // Save final progress if needed
    if (m_hasStartedPlaying) {
        finalizeWatchProgress();
    }
    
    // Stop and clean up timers
    if (m_progressSaveTimer) {
        m_progressSaveTimer->stop();
        delete m_progressSaveTimer;
        m_progressSaveTimer = nullptr;
    }
    
    // Clean up minimize timer if it exists
    if (m_minimizeTimer) {
        m_minimizeTimer->stop();
        delete m_minimizeTimer;
        m_minimizeTimer = nullptr;
    }
}

void VP_Shows_Videoplayer::play()
{
    qDebug() << "VP_Shows_Videoplayer: Play requested (show-specific override)";
    
    // Mark that playback has started for watch history
    if (!m_hasStartedPlaying) {
        m_hasStartedPlaying = true;
        initializeWatchProgress();
        
        // Start the progress save timer with callback
        if (m_progressSaveTimer && !m_progressSaveTimer->isActive()) {
            m_progressSaveTimer->start([this]() {
                this->saveWatchProgress();
            });
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
    
#ifdef Q_OS_WIN
    // During Windows shutdown, skip watch progress save to prevent delays
    if (!m_windowsShutdownInProgress) {
#endif
        // Save final watch progress
        if (m_hasStartedPlaying) {
            finalizeWatchProgress();
        }
#ifdef Q_OS_WIN
    } else {
        qDebug() << "VP_Shows_Videoplayer: Skipping watch progress save due to Windows shutdown";
    }
#endif
    
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
    
    // Set up window state restoration flags based on static variables
    // This needs to be done here because initializeFromPreviousSettings() is called
    // from base class constructor before derived class is fully constructed
    m_shouldRestoreFullscreen = s_wasFullScreen;
    m_shouldRestoreMaximized = s_wasMaximized && !s_wasFullScreen;
    m_shouldRestoreMinimized = s_wasMinimized && !s_wasFullScreen && !s_wasMaximized;
    
    qDebug() << "VP_Shows_Videoplayer: Read static states - Fullscreen:" << s_wasFullScreen
             << "Maximized:" << s_wasMaximized << "Minimized:" << s_wasMinimized;
    qDebug() << "VP_Shows_Videoplayer: Should restore - Fullscreen:" << m_shouldRestoreFullscreen
             << "Maximized:" << m_shouldRestoreMaximized << "Minimized:" << m_shouldRestoreMinimized;
    
    // Call base class implementation first
    BaseVideoPlayer::showEvent(event);
    
    // Update position slider tooltip to include Ctrl+Left/Right for shows
    if (m_positionSlider) {
        m_positionSlider->setToolTip(tr("Click to seek\nLeft/Right: Seek 10s\nCtrl+Left: Jump to beginning\nCtrl+Right: Jump to end"));
    }
    
    // Now apply the window state based on what was stored
    // This needs to happen after the window is shown
    if (!m_isClosing) {
        // Apply window state restoration
        if (m_shouldRestoreFullscreen) {
            qDebug() << "VP_Shows_Videoplayer: Restoring fullscreen state";
            // Use a timer to ensure the window is fully shown before going fullscreen
            SafeTimer::singleShot(100, this, [this]() {
                this->enterFullScreen();
            }, "VP_Shows_Videoplayer");
            // Reset the flag after scheduling
            m_shouldRestoreFullscreen = false;
        } else if (m_shouldRestoreMaximized) {
            qDebug() << "VP_Shows_Videoplayer: Restoring maximized state";
            showMaximized();
            m_shouldRestoreMaximized = false;
        } else if (m_shouldRestoreMinimized && !m_hasBeenMinimized) {
            qDebug() << "VP_Shows_Videoplayer: Scheduling minimized state restoration (for background listening)";
            
            // Cancel any existing timer
            if (m_minimizeTimer) {
                m_minimizeTimer->stop();
                delete m_minimizeTimer;
            }
            
            // Create a new timer for minimizing
            m_minimizeTimer = new SafeTimer(this, "VP_Shows_Videoplayer::minimizeTimer");
            m_minimizeTimer->setSingleShot(true);
            m_minimizeTimer->setInterval(500);
            
            m_minimizeTimer->start([this]() {
                if (!m_isClosing && !m_hasBeenMinimized) {
                    qDebug() << "VP_Shows_Videoplayer: Minimizing window for background playback";
                    
                    // Mark that we've minimized to prevent re-minimizing
                    m_hasBeenMinimized = true;
                    
                    // Simply minimize the window - it should be restorable from taskbar
                    showMinimized();
                    
                    qDebug() << "VP_Shows_Videoplayer: Window minimized - State:" << windowState()
                             << "isMinimized():" << isMinimized()
                             << "isVisible():" << isVisible();
                }
                
                // Clean up timer after it fires
                delete m_minimizeTimer;
                m_minimizeTimer = nullptr;
            });
            m_shouldRestoreMinimized = false;
        }
    }
    
    // Additional show-specific initialization if needed
    if (m_watchHistory && !m_episodePath.isEmpty()) {
        // Could restore playback position here if needed
    }
}

void VP_Shows_Videoplayer::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        Qt::WindowStates oldState = stateEvent->oldState();
        Qt::WindowStates newState = windowState();
        
        qDebug() << "VP_Shows_Videoplayer: Window state changed from" << oldState << "to" << newState;
        
        // Handle restoration from minimized state
        if ((oldState & Qt::WindowMinimized) && !(newState & Qt::WindowMinimized)) {
            qDebug() << "VP_Shows_Videoplayer: Window restored from minimized state";
            
            // Cancel any pending minimize timer
            if (m_minimizeTimer) {
                qDebug() << "VP_Shows_Videoplayer: Cancelling pending minimize timer";
                m_minimizeTimer->stop();
                delete m_minimizeTimer;
                m_minimizeTimer = nullptr;
            }
            
            // Clear the minimized flag so we don't re-minimize
            m_hasBeenMinimized = false;
            m_shouldRestoreMinimized = false;
            
            // Ensure the window is properly shown and activated
            raise();
            activateWindow();
            setFocus();
        }
    }
    
    // Call base class implementation
    QWidget::changeEvent(event);
}

void VP_Shows_Videoplayer::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Key press event - Key:" << event->key() << "Modifiers:" << event->modifiers();
    
    // Check if Ctrl key is pressed (works for both left and right Ctrl)
    bool ctrlPressed = (event->modifiers() & Qt::ControlModifier);
    
    // Handle show-specific keybinds
    switch (event->key()) {
        case Qt::Key_Right:
            if (ctrlPressed) {
                // Ctrl+Right: Jump to end of video
                if (m_mediaPlayer && m_mediaPlayer->hasMedia()) {
                    qint64 videoDuration = duration();
                    if (videoDuration > 0) {
                        // Jump to 1 second before the end to avoid immediate finish
                        qint64 endPosition = videoDuration - 1000;
                        if (endPosition < 0) endPosition = 0;
                        qDebug() << "VP_Shows_Videoplayer: Ctrl+Right - jumping to end position:" << endPosition;
                        setPosition(endPosition);
                        event->accept();
                        return;  // Don't call base class for this key combo
                    }
                }
            }
            break;
            
        case Qt::Key_Left:
            if (ctrlPressed) {
                // Ctrl+Left: Jump to beginning of video
                if (m_mediaPlayer && m_mediaPlayer->hasMedia()) {
                    qDebug() << "VP_Shows_Videoplayer: Ctrl+Left - jumping to beginning";
                    setPosition(0);
                    event->accept();
                    return;  // Don't call base class for this key combo
                }
            }
            break;
    }
    
    // For all other keys, call base class implementation
    BaseVideoPlayer::keyPressEvent(event);
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
                m_progressSaveTimer->start([this]() {
                    this->saveWatchProgress();
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
                SafeTimer::singleShot(100, this, [this]() {
                    this->close();
                }, "VP_Shows_Videoplayer");
            }
            break;
            
        default:
            break;
    }
}

void VP_Shows_Videoplayer::handleVideoFinished()
{
    qDebug() << "VP_Shows_Videoplayer: Video finished - preserving autoplay behavior";
    
    // For shows player, we just emit the finished signal
    // This allows the autoplay functionality in Operations_VP_Shows to work
    // We do NOT reset position or pause like the base class does
    emit finished();
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
