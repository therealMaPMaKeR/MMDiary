#ifndef VP_VLCPLAYER_H
#define VP_VLCPLAYER_H

#include <QWidget>
#include <QString>
#include <memory>

// Forward declarations to avoid including vlc headers here
struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;

class QVBoxLayout;
class QWidget;

/**
 * @brief General VLC player bridge for Qt applications
 * 
 * This class provides a Qt-friendly interface to LibVLC that can be used
 * by multiple videoplayer implementations. It handles the low-level VLC
 * integration and provides Qt signals for state changes.
 */
class VP_VLCPlayer : public QWidget
{
    Q_OBJECT
    
public:
    explicit VP_VLCPlayer(QWidget *parent = nullptr);
    ~VP_VLCPlayer();
    
    /**
     * @brief Initialize VLC instance
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Load a video file
     * @param filePath Path to the video file
     * @return true if loading successful
     */
    bool loadVideo(const QString& filePath);
    
    /**
     * @brief Start playback
     */
    void play();
    
    /**
     * @brief Pause playback
     */
    void pause();
    
    /**
     * @brief Stop playback
     */
    void stop();
    
    /**
     * @brief Get current playback position
     * @return Position in milliseconds
     */
    qint64 position() const;
    
    /**
     * @brief Set playback position
     * @param position Position in milliseconds
     */
    void setPosition(qint64 position);
    
    /**
     * @brief Get video duration
     * @return Duration in milliseconds
     */
    qint64 duration() const;
    
    /**
     * @brief Get volume level
     * @return Volume from 0 to 100
     */
    int volume() const;
    
    /**
     * @brief Set volume level
     * @param volume Volume from 0 to 100
     */
    void setVolume(int volume);
    
    /**
     * @brief Check if currently playing
     * @return true if playing
     */
    bool isPlaying() const;
    
    /**
     * @brief Set playback speed
     * @param rate Speed multiplier (1.0 = normal)
     */
    void setPlaybackRate(float rate);
    
    /**
     * @brief Get the video rendering widget
     * @return Pointer to the video widget
     */
    QWidget* videoWidget() const { return m_videoWidget; }

signals:
    /**
     * @brief Emitted when playback position changes
     * @param position Current position in milliseconds
     */
    void positionChanged(qint64 position);
    
    /**
     * @brief Emitted when duration is available
     * @param duration Total duration in milliseconds
     */
    void durationChanged(qint64 duration);
    
    /**
     * @brief Emitted when playback state changes
     * @param playing true if playing, false if paused/stopped
     */
    void playbackStateChanged(bool playing);
    
    /**
     * @brief Emitted when an error occurs
     * @param errorString Description of the error
     */
    void errorOccurred(const QString& errorString);
    
    /**
     * @brief Emitted when video reaches the end
     */
    void mediaEndReached();

private:
    // VLC instances (using raw pointers wrapped in unique_ptr with custom deleters)
    libvlc_instance_t* m_vlcInstance;
    libvlc_media_player_t* m_mediaPlayer;
    
    // Video widget for rendering
    QWidget* m_videoWidget;
    
    // Current state
    QString m_currentFilePath;
    bool m_isInitialized;
    
    // Setup the video output widget
    void setupVideoWidget();
    
    // Cleanup resources
    void cleanup();
    
    // Static callback for VLC events
    static void handleVLCEvent(const struct libvlc_event_t* event, void* userData);
    
    // Register VLC event callbacks
    void registerEvents();
    
    // Unregister VLC event callbacks
    void unregisterEvents();
};

#endif // VP_VLCPLAYER_H
