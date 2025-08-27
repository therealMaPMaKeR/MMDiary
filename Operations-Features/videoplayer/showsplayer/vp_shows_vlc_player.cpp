#include "vp_shows_vlc_player.h"

#ifdef USE_LIBVLC

#include <vlc/vlc.h>
#include <QVBoxLayout>
#include <QDebug>
#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

VP_Shows_VLCPlayer::VP_Shows_VLCPlayer(QWidget *parent)
    : QWidget(parent)
    , m_vlcInstance(nullptr)
    , m_mediaPlayer(nullptr)
    , m_videoWidget(nullptr)
    , m_isInitialized(false)
{
    qDebug() << "VP_Shows_VLCPlayer: Constructor called";
    setupVideoWidget();
}

VP_Shows_VLCPlayer::~VP_Shows_VLCPlayer()
{
    qDebug() << "VP_Shows_VLCPlayer: Destructor called";
    cleanup();
}

void VP_Shows_VLCPlayer::setupVideoWidget()
{
    qDebug() << "VP_Shows_VLCPlayer: Setting up video widget";
    
    // Create layout
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Create video widget
    m_videoWidget = new QWidget(this);
    m_videoWidget->setStyleSheet("background-color: black;");
    m_videoWidget->setMinimumSize(640, 480);
    
    layout->addWidget(m_videoWidget);
}

bool VP_Shows_VLCPlayer::initialize()
{
    qDebug() << "VP_Shows_VLCPlayer: Initializing VLC";
    
    if (m_isInitialized) {
        qDebug() << "VP_Shows_VLCPlayer: Already initialized";
        return true;
    }
    
    // Create VLC instance with arguments
    const char* vlc_args[] = {
        "--no-xlib",  // Avoid X11 threading issues (for Linux compatibility)
        "--quiet",    // Reduce console output
        "--no-video-title-show", // Don't show media title on video
        "--no-stats", // Don't collect statistics
        "--no-snapshot-preview", // Don't show snapshot preview
        "--intf=dummy" // No interface
    };
    
    m_vlcInstance = libvlc_new(sizeof(vlc_args)/sizeof(vlc_args[0]), vlc_args);
    
    if (!m_vlcInstance) {
        qDebug() << "VP_Shows_VLCPlayer: Failed to create VLC instance";
        emit errorOccurred("Failed to initialize VLC. Please ensure VLC libraries are properly installed.");
        return false;
    }
    
    // Create media player
    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    
    if (!m_mediaPlayer) {
        qDebug() << "VP_Shows_VLCPlayer: Failed to create media player";
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
        emit errorOccurred("Failed to create VLC media player.");
        return false;
    }
    
    // Set video output to our widget
#ifdef Q_OS_WIN
    libvlc_media_player_set_hwnd(m_mediaPlayer, (void*)m_videoWidget->winId());
#elif defined(Q_OS_LINUX)
    libvlc_media_player_set_xwindow(m_mediaPlayer, m_videoWidget->winId());
#elif defined(Q_OS_MAC)
    libvlc_media_player_set_nsobject(m_mediaPlayer, (void*)m_videoWidget->winId());
#endif
    
    // Register event callbacks
    registerEvents();
    
    m_isInitialized = true;
    qDebug() << "VP_Shows_VLCPlayer: Initialization successful";
    
    return true;
}

void VP_Shows_VLCPlayer::cleanup()
{
    qDebug() << "VP_Shows_VLCPlayer: Cleaning up VLC resources";
    
    if (m_mediaPlayer) {
        // Stop playback
        libvlc_media_player_stop(m_mediaPlayer);
        
        // Unregister events
        unregisterEvents();
        
        // Release media player
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
    }
    
    m_isInitialized = false;
}

bool VP_Shows_VLCPlayer::loadVideo(const QString& filePath)
{
    qDebug() << "VP_Shows_VLCPlayer: Loading video:" << filePath;
    
    if (!m_isInitialized) {
        if (!initialize()) {
            qDebug() << "VP_Shows_VLCPlayer: Failed to initialize before loading video";
            return false;
        }
    }
    
    // Create media from file path
    libvlc_media_t* media = libvlc_media_new_path(m_vlcInstance, filePath.toUtf8().constData());
    
    if (!media) {
        qDebug() << "VP_Shows_VLCPlayer: Failed to create media from path:" << filePath;
        emit errorOccurred(QString("Failed to load video: %1").arg(filePath));
        return false;
    }
    
    // Parse media to get information
    libvlc_media_parse(media);
    
    // Set media to player
    libvlc_media_player_set_media(m_mediaPlayer, media);
    
    // Release media (player keeps its own reference)
    libvlc_media_release(media);
    
    m_currentFilePath = filePath;
    
    qDebug() << "VP_Shows_VLCPlayer: Video loaded successfully";
    return true;
}

void VP_Shows_VLCPlayer::play()
{
    qDebug() << "VP_Shows_VLCPlayer: Play requested";
    
    if (!m_mediaPlayer) {
        qDebug() << "VP_Shows_VLCPlayer: No media player instance";
        return;
    }
    
    if (libvlc_media_player_play(m_mediaPlayer) == 0) {
        qDebug() << "VP_Shows_VLCPlayer: Playback started";
        emit playbackStateChanged(true);
    } else {
        qDebug() << "VP_Shows_VLCPlayer: Failed to start playback";
        emit errorOccurred("Failed to start playback");
    }
}

void VP_Shows_VLCPlayer::pause()
{
    qDebug() << "VP_Shows_VLCPlayer: Pause requested";
    
    if (!m_mediaPlayer) {
        return;
    }
    
    libvlc_media_player_pause(m_mediaPlayer);
    emit playbackStateChanged(false);
}

void VP_Shows_VLCPlayer::stop()
{
    qDebug() << "VP_Shows_VLCPlayer: Stop requested";
    
    if (!m_mediaPlayer) {
        return;
    }
    
    libvlc_media_player_stop(m_mediaPlayer);
    emit playbackStateChanged(false);
}

qint64 VP_Shows_VLCPlayer::position() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_media_player_get_time(m_mediaPlayer);
}

void VP_Shows_VLCPlayer::setPosition(qint64 position)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    libvlc_media_player_set_time(m_mediaPlayer, position);
}

qint64 VP_Shows_VLCPlayer::duration() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_media_player_get_length(m_mediaPlayer);
}

int VP_Shows_VLCPlayer::volume() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    
    return libvlc_audio_get_volume(m_mediaPlayer);
}

void VP_Shows_VLCPlayer::setVolume(int volume)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // Clamp volume to 0-100 range
    volume = qBound(0, volume, 100);
    libvlc_audio_set_volume(m_mediaPlayer, volume);
}

bool VP_Shows_VLCPlayer::isPlaying() const
{
    if (!m_mediaPlayer) {
        return false;
    }
    
    return libvlc_media_player_is_playing(m_mediaPlayer) == 1;
}

void VP_Shows_VLCPlayer::setPlaybackRate(float rate)
{
    if (!m_mediaPlayer) {
        return;
    }
    
    // VLC supports rates from 0.25 to 4.0
    rate = qBound(0.25f, rate, 4.0f);
    libvlc_media_player_set_rate(m_mediaPlayer, rate);
    
    qDebug() << "VP_Shows_VLCPlayer: Playback rate set to" << rate;
}

void VP_Shows_VLCPlayer::handleVLCEvent(const libvlc_event_t* event, void* userData)
{
    VP_Shows_VLCPlayer* player = static_cast<VP_Shows_VLCPlayer*>(userData);
    if (!player) {
        return;
    }
    
    switch (event->type) {
        case libvlc_MediaPlayerTimeChanged:
            emit player->positionChanged(event->u.media_player_time_changed.new_time);
            break;
            
        case libvlc_MediaPlayerLengthChanged:
            emit player->durationChanged(event->u.media_player_length_changed.new_length);
            break;
            
        case libvlc_MediaPlayerEndReached:
            qDebug() << "VP_Shows_VLCPlayer: Media end reached";
            emit player->mediaEndReached();
            emit player->playbackStateChanged(false);
            break;
            
        case libvlc_MediaPlayerEncounteredError:
            qDebug() << "VP_Shows_VLCPlayer: Playback error encountered";
            emit player->errorOccurred("Playback error occurred");
            break;
            
        case libvlc_MediaPlayerPlaying:
            qDebug() << "VP_Shows_VLCPlayer: Playback started event";
            emit player->playbackStateChanged(true);
            break;
            
        case libvlc_MediaPlayerPaused:
            qDebug() << "VP_Shows_VLCPlayer: Playback paused event";
            emit player->playbackStateChanged(false);
            break;
            
        case libvlc_MediaPlayerStopped:
            qDebug() << "VP_Shows_VLCPlayer: Playback stopped event";
            emit player->playbackStateChanged(false);
            break;
            
        default:
            break;
    }
}

void VP_Shows_VLCPlayer::registerEvents()
{
    if (!m_mediaPlayer) {
        return;
    }
    
    qDebug() << "VP_Shows_VLCPlayer: Registering VLC events";
    
    libvlc_event_manager_t* eventManager = libvlc_media_player_event_manager(m_mediaPlayer);
    if (!eventManager) {
        qDebug() << "VP_Shows_VLCPlayer: Failed to get event manager";
        return;
    }
    
    // Register for various events
    libvlc_event_attach(eventManager, libvlc_MediaPlayerTimeChanged, handleVLCEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerLengthChanged, handleVLCEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerEndReached, handleVLCEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerEncounteredError, handleVLCEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerPlaying, handleVLCEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerPaused, handleVLCEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerStopped, handleVLCEvent, this);
}

void VP_Shows_VLCPlayer::unregisterEvents()
{
    if (!m_mediaPlayer) {
        return;
    }
    
    qDebug() << "VP_Shows_VLCPlayer: Unregistering VLC events";
    
    libvlc_event_manager_t* eventManager = libvlc_media_player_event_manager(m_mediaPlayer);
    if (!eventManager) {
        return;
    }
    
    // Unregister all events
    libvlc_event_detach(eventManager, libvlc_MediaPlayerTimeChanged, handleVLCEvent, this);
    libvlc_event_detach(eventManager, libvlc_MediaPlayerLengthChanged, handleVLCEvent, this);
    libvlc_event_detach(eventManager, libvlc_MediaPlayerEndReached, handleVLCEvent, this);
    libvlc_event_detach(eventManager, libvlc_MediaPlayerEncounteredError, handleVLCEvent, this);
    libvlc_event_detach(eventManager, libvlc_MediaPlayerPlaying, handleVLCEvent, this);
    libvlc_event_detach(eventManager, libvlc_MediaPlayerPaused, handleVLCEvent, this);
    libvlc_event_detach(eventManager, libvlc_MediaPlayerStopped, handleVLCEvent, this);
}

#else // USE_LIBVLC not defined

// Stub implementation when libvlc is not available
VP_Shows_VLCPlayer::VP_Shows_VLCPlayer(QWidget *parent) : QWidget(parent) 
{
    qDebug() << "VP_Shows_VLCPlayer: LibVLC support not compiled in";
}

VP_Shows_VLCPlayer::~VP_Shows_VLCPlayer() {}
bool VP_Shows_VLCPlayer::initialize() { return false; }
bool VP_Shows_VLCPlayer::loadVideo(const QString&) { return false; }
void VP_Shows_VLCPlayer::play() {}
void VP_Shows_VLCPlayer::pause() {}
void VP_Shows_VLCPlayer::stop() {}
qint64 VP_Shows_VLCPlayer::position() const { return 0; }
void VP_Shows_VLCPlayer::setPosition(qint64) {}
qint64 VP_Shows_VLCPlayer::duration() const { return 0; }
int VP_Shows_VLCPlayer::volume() const { return 0; }
void VP_Shows_VLCPlayer::setVolume(int) {}
bool VP_Shows_VLCPlayer::isPlaying() const { return false; }
void VP_Shows_VLCPlayer::setPlaybackRate(float) {}
void VP_Shows_VLCPlayer::setupVideoWidget() {}
void VP_Shows_VLCPlayer::cleanup() {}
void VP_Shows_VLCPlayer::handleVLCEvent(const libvlc_event_t*, void*) {}
void VP_Shows_VLCPlayer::registerEvents() {}
void VP_Shows_VLCPlayer::unregisterEvents() {}

#endif // USE_LIBVLC
