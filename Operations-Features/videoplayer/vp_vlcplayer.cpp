#include "vp_vlcplayer.h"
#include <vlc/vlc.h>
#include <QDebug>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>

VP_VLCPlayer::VP_VLCPlayer(QObject *parent)
    : QObject(parent)
    , m_vlcInstance(nullptr)
    , m_mediaPlayer(nullptr)
    , m_currentMedia(nullptr)
    , m_eventManager(nullptr)
    , m_state(PlayerState::Stopped)
    , m_isMuted(false)
    , m_savedVolume(100)
    , m_videoWidget(nullptr)
    , m_positionTimer(new QTimer(this))
    , m_lastPosition(-1)
    , m_duration(-1)
    , m_debugMode(true)  // Enable debug output
    , m_isDestroying(false)
{
    // Setup position update timer
    m_positionTimer->setInterval(100);  // Update every 100ms
    connect(m_positionTimer, &QTimer::timeout, this, &VP_VLCPlayer::updatePosition);
    
    // Initialize VLC
    if (!initialize()) {
        qDebug() << "VP_VLCPlayer: Failed to initialize VLC";
    }
}

VP_VLCPlayer::~VP_VLCPlayer()
{
    qDebug() << "VP_VLCPlayer: Destructor called";
    
    // Set flag to prevent callbacks during destruction
    m_isDestroying = true;
    
    // Stop position timer first to prevent callbacks during destruction
    if (m_positionTimer) {
        m_positionTimer->stop();
        disconnect(m_positionTimer, nullptr, this, nullptr);
    }
    
    // Clean up event callbacks before releasing media player
    if (m_mediaPlayer) {
        cleanupEventCallbacks();
        stop();
        
        // Detach video output to prevent access during destruction
        libvlc_media_player_set_hwnd(m_mediaPlayer, nullptr);
    }
    
    // Release current media if any
    if (m_currentMedia) {
        libvlc_media_release(m_currentMedia);
        m_currentMedia = nullptr;
    }
    
    // Release media player
    if (m_mediaPlayer) {
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    
    // Release VLC instance last
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
    }
    
    // Clear widget pointer
    m_videoWidget = nullptr;
}

bool VP_VLCPlayer::initialize()
{
    qDebug() << "VP_VLCPlayer: Initializing VLC instance";
    
    // Determine the plugin path
    QString pluginPath;
    
    // First, try to find plugins in the application directory
    QString appDir = QCoreApplication::applicationDirPath();
    
    // Security: Basic path cleaning for application directory (not user files)
    // We don't use sanitizePath here because it's meant for user file validation
    // Application directory is trusted and outside the user data directories
    // QDir::cleanPath removes redundant ".", ".." and normalizes separators
    appDir = QDir::cleanPath(appDir);
    
    // Security: Additional check for path traversal attempts
    // Although QDir::cleanPath should have removed these, we double-check for safety
    if (appDir.contains("..\\") || appDir.contains("../") || 
        appDir.endsWith("..") || appDir.contains("/..\\") || appDir.contains("/\\..")) {
        setLastError("Application directory path contains traversal attempts");
        qDebug() << "VP_VLCPlayer: Application directory path contains traversal attempts:" << appDir;
        return false;
    }
    
    QString appPlugins = appDir + "/plugins";
    
    // Check if plugins exist in app directory (for deployed version)
    if (QDir(appPlugins).exists()) {
        pluginPath = appPlugins;
        qDebug() << "VP_VLCPlayer: Using plugins from application directory:" << pluginPath;
    } else {
        // For development, use the project's 3rdparty folder
        // We need to go up from the build directory to find the project
        QDir buildDir(appDir);
        
        // Try to find the project directory by looking for the 3rdparty folder
        for (int i = 0; i < 5; ++i) {
            QString testPath = buildDir.absolutePath() + "/3rdparty/libvlc/bin/plugins";
            // Security: Clean and validate the constructed path
            testPath = QDir::cleanPath(testPath);
            
            // Check for traversal attempts
            if (!testPath.contains("..\\") && !testPath.contains("../") && QDir(testPath).exists()) {
                pluginPath = testPath;
                qDebug() << "VP_VLCPlayer: Using plugins from project directory:" << pluginPath;
                break;
            }
            if (!buildDir.cdUp()) {
                break;
            }
        }
        
        // If still not found, try the fallback path
        if (pluginPath.isEmpty()) {
            QString fallbackPath = "C:/Users/Gabriel/Storage/Coding/Projects/MMDiary/MMDiary/3rdparty/libvlc/bin/plugins";
            // Security: Clean the fallback path
            fallbackPath = QDir::cleanPath(fallbackPath);
            
            // Check for traversal attempts and verify existence
            if (!fallbackPath.contains("..\\") && !fallbackPath.contains("../") && QDir(fallbackPath).exists()) {
                pluginPath = fallbackPath;
                qDebug() << "VP_VLCPlayer: Using fallback plugin path:" << pluginPath;
            } else {
                qDebug() << "VP_VLCPlayer: Warning - Could not find VLC plugins!";
            }
        }
    }
    
    // Security: Final validation of plugin path
    if (!pluginPath.isEmpty()) {
        // Clean the path one more time
        pluginPath = QDir::cleanPath(pluginPath);
        
        // Final check for any traversal attempts
        if (pluginPath.contains("..\\") || pluginPath.contains("../")) {
            qDebug() << "VP_VLCPlayer: Warning - Plugin path contains traversal attempts";
            pluginPath.clear();
        } else {
            // Verify the path actually exists
            if (!QDir(pluginPath).exists()) {
                qDebug() << "VP_VLCPlayer: Warning - Plugin path does not exist:" << pluginPath;
                pluginPath.clear();
            } else {
                // Convert to native separators after validation
                pluginPath = QDir::toNativeSeparators(pluginPath);
            }
        }
    }
    
    // Prepare VLC arguments with the correct plugin path
    std::string pluginArg = "--plugin-path=" + pluginPath.toStdString();
    
    // VLC command line arguments
    const char* vlc_args[] = {
        "--no-xlib",  // Tell VLC not to use Xlib (for Linux compatibility)
        "--quiet",    // Suppress console output
        "--no-video-title-show",  // Don't show media title on video
        "--no-stats",  // Don't collect statistics
        "--no-snapshot-preview",  // Don't show snapshot preview
        "--intf=dummy",  // No interface
        "--no-media-library",  // Don't use media library
        "--no-one-instance",  // Allow multiple instances
        "--vout=dummy",  // Use dummy video output to suppress timing warnings
        "--verbose=0",  // Minimal verbosity
        "--no-osd",  // No on-screen display
        pluginArg.c_str()  // Plugin path
    };
    
    int vlc_argc = sizeof(vlc_args) / sizeof(vlc_args[0]);
    
    qDebug() << "VP_VLCPlayer: Initializing with arguments:";
    for (int i = 0; i < vlc_argc; i++) {
        qDebug() << "  " << vlc_args[i];
    }
    
    // Create VLC instance
    m_vlcInstance = libvlc_new(vlc_argc, vlc_args);
    
    if (!m_vlcInstance) {
        const char* error = libvlc_errmsg();
        QString errorMsg = error ? QString::fromUtf8(error) : "Unknown error";
        setLastError(QString("Failed to create VLC instance: %1. Make sure VLC libraries are properly installed.").arg(errorMsg));
        qDebug() << "VP_VLCPlayer: Failed to create VLC instance. Error:" << errorMsg;
        return false;
    }
    
    // Create media player
    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    
    if (!m_mediaPlayer) {
        setLastError("Failed to create VLC media player.");
        qDebug() << "VP_VLCPlayer: Failed to create media player";
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
        return false;
    }
    
    // Setup event callbacks
    setupEventCallbacks();
    
    qDebug() << "VP_VLCPlayer: VLC initialization successful";
    return true;
}

bool VP_VLCPlayer::loadMedia(const QString& filePath)
{
    if (!m_vlcInstance || !m_mediaPlayer) {
        setLastError("VLC is not initialized");
        return false;
    }
    
    qDebug() << "VP_VLCPlayer: Loading media:" << filePath;
    
    // Check if file exists
    if (!QFile::exists(filePath)) {
        setLastError(QString("File does not exist: %1").arg(filePath));
        qDebug() << "VP_VLCPlayer: File does not exist:" << filePath;
        return false;
    }
    
    // Clean up previous media
    if (m_currentMedia) {
        libvlc_media_release(m_currentMedia);
        m_currentMedia = nullptr;
    }
    
    // Create new media
    // Convert to native separators and then to UTF-8
    QString nativePath = QDir::toNativeSeparators(filePath);
    
#ifdef _WIN32
    // On Windows, libvlc expects either a URI or a Windows path
    m_currentMedia = libvlc_media_new_path(m_vlcInstance, nativePath.toUtf8().constData());
#else
    // On other platforms, use the path as-is
    m_currentMedia = libvlc_media_new_path(m_vlcInstance, filePath.toUtf8().constData());
#endif
    
    if (!m_currentMedia) {
        setLastError(QString("Failed to create media from file: %1").arg(filePath));
        qDebug() << "VP_VLCPlayer: Failed to create media from file:" << filePath;
        return false;
    }
    
    // Set media to player
    libvlc_media_player_set_media(m_mediaPlayer, m_currentMedia);
    
    // Store the media path
    m_currentMediaPath = filePath;
    
    // Update media info
    updateMediaInfo();
    
    // Emit signal
    emit mediaLoaded(filePath);
    
    qDebug() << "VP_VLCPlayer: Media loaded successfully";
    return true;
}

void VP_VLCPlayer::unloadMedia()
{
    qDebug() << "VP_VLCPlayer: Unloading media";
    
    // Stop playback first
    stop();
    
    // Release current media
    if (m_currentMedia) {
        libvlc_media_release(m_currentMedia);
        m_currentMedia = nullptr;
    }
    
    // Clear media from player
    if (m_mediaPlayer) {
        libvlc_media_player_set_media(m_mediaPlayer, nullptr);
    }
    
    m_currentMediaPath.clear();
    m_duration = -1;
    
    emit mediaUnloaded();
}

void VP_VLCPlayer::play()
{
    if (!m_mediaPlayer || !m_currentMedia) {
        setLastError("No media loaded");
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Starting playback";
    
    // Set video output window if available
    if (m_videoWidget) {
#ifdef _WIN32
        libvlc_media_player_set_hwnd(m_mediaPlayer, reinterpret_cast<void*>(m_videoWidget->winId()));
#elif defined(__APPLE__)
        libvlc_media_player_set_nsobject(m_mediaPlayer, reinterpret_cast<void*>(m_videoWidget->winId()));
#else
        libvlc_media_player_set_xwindow(m_mediaPlayer, m_videoWidget->winId());
#endif
        
        // Ensure libvlc input is disabled to allow Qt event handling
        setMouseInputEnabled(false);
        setKeyInputEnabled(false);
    }
    
    int result = libvlc_media_player_play(m_mediaPlayer);
    
    if (result == 0) {
        setState(PlayerState::Playing);
        m_positionTimer->start();
        emit playing();
        qDebug() << "VP_VLCPlayer: Playback started successfully";
    } else {
        setLastError("Failed to start playback");
        qDebug() << "VP_VLCPlayer: Failed to start playback, error code:" << result;
    }
}

void VP_VLCPlayer::pause()
{
    if (!m_mediaPlayer) {
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Pausing playback";
    
    libvlc_media_player_set_pause(m_mediaPlayer, 1);
    setState(PlayerState::Paused);
    m_positionTimer->stop();
    emit paused();
}

void VP_VLCPlayer::stop()
{
    if (!m_mediaPlayer) {
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Stopping playback";
    
    libvlc_media_player_stop(m_mediaPlayer);
    setState(PlayerState::Stopped);
    m_positionTimer->stop();
    m_lastPosition = -1;
    emit stopped();
}

void VP_VLCPlayer::togglePlayPause()
{
    if (isPlaying()) {
        pause();
    } else {
        play();
    }
}

qint64 VP_VLCPlayer::position() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_media_player_get_time(m_mediaPlayer);
}

qint64 VP_VLCPlayer::duration() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    libvlc_time_t dur = libvlc_media_player_get_length(m_mediaPlayer);
    if (dur == -1) {
        return m_duration;  // Return cached duration if available
    }
    
    return dur;
}

void VP_VLCPlayer::setPosition(qint64 position)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // Ensure position is valid
    if (position < 0) {
        qDebug() << "VP_VLCPlayer: Invalid negative position, setting to 0";
        position = 0;
    }
    
    // Check if the player is in a state that allows seeking
    if (!libvlc_media_player_is_playing(m_mediaPlayer) && m_state != PlayerState::Paused) {
        qDebug() << "VP_VLCPlayer: Warning - Setting position while not playing or paused, may not work correctly";
    }
    
    qDebug() << "VP_VLCPlayer: Setting position to" << position << "ms";
    libvlc_media_player_set_time(m_mediaPlayer, position);
    
    // Update cached position immediately
    m_lastPosition = position;
    emit positionChanged(position);
}

void VP_VLCPlayer::seekRelative(qint64 offset)
{
    qint64 newPosition = position() + offset;
    qint64 mediaDuration = duration();
    
    // Clamp to valid range
    if (newPosition < 0) {
        newPosition = 0;
    } else if (mediaDuration > 0 && newPosition > mediaDuration) {
        newPosition = mediaDuration;
    }
    
    setPosition(newPosition);
}

int VP_VLCPlayer::volume() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_audio_get_volume(m_mediaPlayer);
}

void VP_VLCPlayer::setVolume(int volume)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // Clamp volume to valid range (libvlc supports 0-200)
    if (volume < 0) volume = 0;
    if (volume > 200) volume = 200;
    
    qDebug() << "VP_VLCPlayer: Setting volume to" << volume << "%";
    
    libvlc_audio_set_volume(m_mediaPlayer, volume);
    
    if (!m_isMuted) {
        m_savedVolume = volume;
    }
    
    emit volumeChanged(volume);
}

void VP_VLCPlayer::mute()
{
    if (!m_mediaPlayer || m_isMuted) {
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Muting audio";
    
    m_savedVolume = volume();
    libvlc_audio_set_mute(m_mediaPlayer, 1);
    m_isMuted = true;
    
    emit mutedChanged(true);
}

void VP_VLCPlayer::unmute()
{
    if (!m_mediaPlayer || !m_isMuted) {
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Unmuting audio";
    
    libvlc_audio_set_mute(m_mediaPlayer, 0);
    m_isMuted = false;
    
    emit mutedChanged(false);
}

bool VP_VLCPlayer::isMuted() const
{
    if (!m_mediaPlayer) {
        return false;
    }
    
    return libvlc_audio_get_mute(m_mediaPlayer) != 0;
}

float VP_VLCPlayer::playbackRate() const
{
    if (!m_mediaPlayer) {
        return 1.0f;
    }
    
    return libvlc_media_player_get_rate(m_mediaPlayer);
}

void VP_VLCPlayer::setPlaybackRate(float rate)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // Clamp rate to reasonable values
    if (rate < 0.25f) rate = 0.25f;
    if (rate > 4.0f) rate = 4.0f;
    
    qDebug() << "VP_VLCPlayer: Setting playback rate to" << rate;
    
    libvlc_media_player_set_rate(m_mediaPlayer, rate);
}

bool VP_VLCPlayer::isPlaying() const
{
    if (!m_mediaPlayer) {
        return false;
    }
    
    return libvlc_media_player_is_playing(m_mediaPlayer) != 0;
}

bool VP_VLCPlayer::isPaused() const
{
    return m_state == PlayerState::Paused;
}

bool VP_VLCPlayer::isStopped() const
{
    return m_state == PlayerState::Stopped;
}

bool VP_VLCPlayer::hasMedia() const
{
    return m_currentMedia != nullptr;
}

void VP_VLCPlayer::setVideoWidget(QWidget* widget)
{
    m_videoWidget = widget;
    
    if (m_mediaPlayer && m_videoWidget) {
        // Set the video output window
#ifdef _WIN32
        libvlc_media_player_set_hwnd(m_mediaPlayer, reinterpret_cast<void*>(m_videoWidget->winId()));
#elif defined(__APPLE__)
        libvlc_media_player_set_nsobject(m_mediaPlayer, reinterpret_cast<void*>(m_videoWidget->winId()));
#else
        libvlc_media_player_set_xwindow(m_mediaPlayer, m_videoWidget->winId());
#endif
        
        // Disable libvlc's mouse and keyboard input to allow Qt event handling
        setMouseInputEnabled(false);
        setKeyInputEnabled(false);
        qDebug() << "VP_VLCPlayer: Disabled libvlc input handling to allow Qt events";
    }
}

void VP_VLCPlayer::setMouseInputEnabled(bool enabled)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // 0 = disable mouse input (allow Qt to handle), 1 = enable mouse input (libvlc handles)
    libvlc_video_set_mouse_input(m_mediaPlayer, enabled ? 1 : 0);
    qDebug() << "VP_VLCPlayer: Mouse input" << (enabled ? "enabled" : "disabled") << "for libvlc";
}

void VP_VLCPlayer::setKeyInputEnabled(bool enabled)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // 0 = disable keyboard input (allow Qt to handle), 1 = enable keyboard input (libvlc handles)
    libvlc_video_set_key_input(m_mediaPlayer, enabled ? 1 : 0);
    qDebug() << "VP_VLCPlayer: Keyboard input" << (enabled ? "enabled" : "disabled") << "for libvlc";
}

int VP_VLCPlayer::audioTrackCount() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_audio_get_track_count(m_mediaPlayer);
}

int VP_VLCPlayer::currentAudioTrack() const
{
    if (!m_mediaPlayer) {
        return -1;
    }
    
    return libvlc_audio_get_track(m_mediaPlayer);
}

void VP_VLCPlayer::setAudioTrack(int track)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Setting audio track to" << track;
    libvlc_audio_set_track(m_mediaPlayer, track);
}

QStringList VP_VLCPlayer::audioTrackDescriptions() const
{
    QStringList descriptions;
    
    if (!m_mediaPlayer) {
        return descriptions;
    }
    
    libvlc_track_description_t* tracks = libvlc_audio_get_track_description(m_mediaPlayer);
    libvlc_track_description_t* current = tracks;
    
    while (current) {
        descriptions.append(QString::fromUtf8(current->psz_name));
        current = current->p_next;
    }
    
    if (tracks) {
        libvlc_track_description_list_release(tracks);
    }
    
    return descriptions;
}

int VP_VLCPlayer::subtitleTrackCount() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_video_get_spu_count(m_mediaPlayer);
}

int VP_VLCPlayer::currentSubtitleTrack() const
{
    if (!m_mediaPlayer) {
        return -1;
    }
    
    return libvlc_video_get_spu(m_mediaPlayer);
}

void VP_VLCPlayer::setSubtitleTrack(int track)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    qDebug() << "VP_VLCPlayer: Setting subtitle track to" << track;
    libvlc_video_set_spu(m_mediaPlayer, track);
}

QStringList VP_VLCPlayer::subtitleTrackDescriptions() const
{
    QStringList descriptions;
    
    if (!m_mediaPlayer) {
        return descriptions;
    }
    
    libvlc_track_description_t* tracks = libvlc_video_get_spu_description(m_mediaPlayer);
    libvlc_track_description_t* current = tracks;
    
    while (current) {
        descriptions.append(QString::fromUtf8(current->psz_name));
        current = current->p_next;
    }
    
    if (tracks) {
        libvlc_track_description_list_release(tracks);
    }
    
    return descriptions;
}

QSize VP_VLCPlayer::videoSize() const
{
    if (!m_mediaPlayer) {
        return QSize();
    }
    
    unsigned int width = 0, height = 0;
    
    if (libvlc_video_get_size(m_mediaPlayer, 0, &width, &height) == 0) {
        return QSize(width, height);
    }
    
    return QSize();
}

float VP_VLCPlayer::aspectRatio() const
{
    QSize size = videoSize();
    if (size.isValid() && size.height() > 0) {
        return static_cast<float>(size.width()) / static_cast<float>(size.height());
    }
    
    return 0.0f;
}

void VP_VLCPlayer::updatePosition()
{
    if (!m_mediaPlayer || m_isDestroying) {
        return;
    }
    
    qint64 currentPos = position();
    
    if (currentPos != m_lastPosition) {
        m_lastPosition = currentPos;
        emit positionChanged(currentPos);
        
        // Calculate and emit progress
        qint64 dur = duration();
        if (dur > 0) {
            float progress = static_cast<float>(currentPos) / static_cast<float>(dur);
            emit progressChanged(progress);
        }
    }
}

void VP_VLCPlayer::setupEventCallbacks()
{
    if (!m_mediaPlayer) {
        return;
    }
    
    m_eventManager = libvlc_media_player_event_manager(m_mediaPlayer);
    
    if (!m_eventManager) {
        qDebug() << "VP_VLCPlayer: Failed to get event manager";
        return;
    }
    
    // Attach event callbacks
    libvlc_event_attach(m_eventManager, libvlc_MediaPlayerEndReached, handleVLCEvent, this);
    libvlc_event_attach(m_eventManager, libvlc_MediaPlayerEncounteredError, handleVLCEvent, this);
    libvlc_event_attach(m_eventManager, libvlc_MediaPlayerLengthChanged, handleVLCEvent, this);
    libvlc_event_attach(m_eventManager, libvlc_MediaPlayerBuffering, handleVLCEvent, this);
    
    qDebug() << "VP_VLCPlayer: Event callbacks setup complete";
}

void VP_VLCPlayer::cleanupEventCallbacks()
{
    if (!m_eventManager) {
        return;
    }
    
    // Detach event callbacks
    libvlc_event_detach(m_eventManager, libvlc_MediaPlayerEndReached, handleVLCEvent, this);
    libvlc_event_detach(m_eventManager, libvlc_MediaPlayerEncounteredError, handleVLCEvent, this);
    libvlc_event_detach(m_eventManager, libvlc_MediaPlayerLengthChanged, handleVLCEvent, this);
    libvlc_event_detach(m_eventManager, libvlc_MediaPlayerBuffering, handleVLCEvent, this);
    

}

void VP_VLCPlayer::handleVLCEvent(const libvlc_event_t* event, void* userData)
{
    VP_VLCPlayer* player = static_cast<VP_VLCPlayer*>(userData);
    
    if (!player || player->m_isDestroying) {
        return;
    }
    
    switch (event->type) {
        case libvlc_MediaPlayerEndReached:
            qDebug() << "VP_VLCPlayer: Media end reached";
            QMetaObject::invokeMethod(player, [player]() {
                player->setState(PlayerState::Stopped);
                player->m_positionTimer->stop();
                emit player->finished();
            }, Qt::QueuedConnection);
            break;
            
        case libvlc_MediaPlayerEncounteredError:
            qDebug() << "VP_VLCPlayer: Playback error encountered";
            QMetaObject::invokeMethod(player, [player]() {
                player->setState(PlayerState::Error);
                player->setLastError("Playback error occurred");
                player->m_positionTimer->stop();
            }, Qt::QueuedConnection);
            break;
            
        case libvlc_MediaPlayerLengthChanged:
            {
                libvlc_time_t duration = event->u.media_player_length_changed.new_length;
                qDebug() << "VP_VLCPlayer: Duration changed to" << duration << "ms";
                QMetaObject::invokeMethod(player, [player, duration]() {
                    player->m_duration = duration;
                    emit player->durationChanged(duration);
                }, Qt::QueuedConnection);
            }
            break;
            
        case libvlc_MediaPlayerBuffering:
            {
                float buffering = event->u.media_player_buffering.new_cache;
                QMetaObject::invokeMethod(player, [player, buffering]() {
                    emit player->bufferingProgress(static_cast<int>(buffering));
                }, Qt::QueuedConnection);
            }
            break;
            
        default:
            break;
    }
}

void VP_VLCPlayer::setState(PlayerState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void VP_VLCPlayer::setLastError(const QString& error)
{
    m_lastError = error;
    qDebug() << "VP_VLCPlayer: Error:" << error;
    emit errorOccurred(error);
}

void VP_VLCPlayer::updateMediaInfo()
{
    if (!m_currentMedia) {
        return;
    }
    
    // Parse media to get info
    libvlc_media_parse(m_currentMedia);
    
    // Get duration if available
    libvlc_time_t dur = libvlc_media_get_duration(m_currentMedia);
    if (dur > 0) {
        m_duration = dur;
        emit durationChanged(dur);
    }
    
    qDebug() << "VP_VLCPlayer: Media info updated, duration:" << m_duration << "ms";
}
