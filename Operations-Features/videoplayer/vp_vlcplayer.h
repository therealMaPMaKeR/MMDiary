#ifndef VP_VLCPLAYER_H
#define VP_VLCPLAYER_H

#include <QObject>
#include <QWidget>
#include <QString>
#include <QTimer>
#include <memory>

// Forward declarations for libvlc types
struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;
struct libvlc_event_manager_t;
struct libvlc_event_t;

class VP_VLCPlayer : public QObject
{
    Q_OBJECT

public:
    // Player state enumeration
    enum class PlayerState {
        Stopped,
        Playing,
        Paused,
        Buffering,
        Error
    };

    // Constructor/Destructor
    explicit VP_VLCPlayer(QObject *parent = nullptr);
    ~VP_VLCPlayer();

    // Initialize VLC instance
    bool initialize();
    
    // Media loading
    bool loadMedia(const QString& filePath);
    void unloadMedia();
    
    // Basic playback controls
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    
    // Position and duration (in milliseconds)
    qint64 position() const;
    qint64 duration() const;
    void setPosition(qint64 position);
    void seekRelative(qint64 offset);  // Seek relative to current position
    
    // Volume control (0-200, where 100 is normal volume)
    int volume() const;
    void setVolume(int volume);
    void mute();
    void unmute();
    bool isMuted() const;
    
    // Playback speed (1.0 = normal speed)
    float playbackRate() const;
    void setPlaybackRate(float rate);
    
    // State queries
    PlayerState state() const { return m_state; }
    bool isPlaying() const;
    bool isPaused() const;
    bool isStopped() const;
    bool hasMedia() const;
    QString currentMediaPath() const { return m_currentMediaPath; }
    
    // Video rendering widget
    QWidget* videoWidget() const { return m_videoWidget; }
    void setVideoWidget(QWidget* widget);
    
    // Audio/Subtitle tracks
    int audioTrackCount() const;
    int currentAudioTrack() const;
    void setAudioTrack(int track);
    QStringList audioTrackDescriptions() const;
    
    int subtitleTrackCount() const;
    int currentSubtitleTrack() const;
    void setSubtitleTrack(int track);
    QStringList subtitleTrackDescriptions() const;
    
    // Video information
    QSize videoSize() const;
    float aspectRatio() const;
    
    // Error handling
    QString lastError() const { return m_lastError; }
    
    // Get raw libvlc media player (protected access with safety check)
    libvlc_media_player_t* getMediaPlayer() const { 
        return (m_isDestroying || !m_mediaPlayer) ? nullptr : m_mediaPlayer; 
    }

signals:
    // State changes
    void stateChanged(VP_VLCPlayer::PlayerState state);
    void playing();
    void paused();
    void stopped();
    void finished();  // Media playback reached the end
    
    // Position/Duration updates
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void progressChanged(float progress);  // 0.0 to 1.0
    
    // Volume changes
    void volumeChanged(int volume);
    void mutedChanged(bool muted);
    
    // Media changes
    void mediaLoaded(const QString& path);
    void mediaUnloaded();
    
    // Buffering
    void bufferingProgress(int percent);
    
    // Errors
    void errorOccurred(const QString& error);
    
    // Track changes
    void audioTracksAvailable(const QStringList& tracks);
    void subtitleTracksAvailable(const QStringList& tracks);

public slots:
    // Enable/disable libvlc mouse/keyboard input (to allow Qt event handling)
    void setMouseInputEnabled(bool enabled);
    void setKeyInputEnabled(bool enabled);
    
private slots:
    void updatePosition();
    
private:
    // LibVLC callbacks (static methods)
    static void handleVLCEvent(const libvlc_event_t* event, void* userData);
    
    // Internal helper methods
    void setupEventCallbacks();
    void cleanupEventCallbacks();
    void setState(PlayerState state);
    void setLastError(const QString& error);
    void updateMediaInfo();
    
    // LibVLC instances
    libvlc_instance_t* m_vlcInstance;
    libvlc_media_player_t* m_mediaPlayer;
    libvlc_media_t* m_currentMedia;
    libvlc_event_manager_t* m_eventManager;
    
    // State tracking
    PlayerState m_state;
    QString m_currentMediaPath;
    QString m_lastError;
    bool m_isMuted;
    int m_savedVolume;  // Volume before muting
    
    // Video widget
    QWidget* m_videoWidget;
    
    // Position update timer
    QTimer* m_positionTimer;
    qint64 m_lastPosition;
    qint64 m_duration;
    

    
    // Debug mode
    bool m_debugMode;
    
    // Destruction flag
    bool m_isDestroying;
};

#endif // VP_VLCPLAYER_H
