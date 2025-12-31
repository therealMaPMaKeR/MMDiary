#ifndef VR_VIDEO_PLAYER_H
#define VR_VIDEO_PLAYER_H

#include <QWidget>
#include <QString>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QThread>
#include <QMutex>
#include "../../Operations-Global/SafeTimer.h"
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <memory>
#include "vr_openvr_manager.h"
#include "vr_video_renderer.h"
#include "vr_vlc_frame_extractor.h"
#include "../vp_vlcplayer.h"

class VRRenderThread;
class ClickableSlider;

/**
 * @class VRVideoPlayer
 * @brief Standalone VR video player widget
 * 
 * This class provides VR video playback capabilities:
 * - Direct VR rendering without screen display
 * - Support for 360°/180° video formats
 * - Stereoscopic rendering
 * - VLC-based video decoding
 */
class VRVideoPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit VRVideoPlayer(QWidget *parent = nullptr);
    ~VRVideoPlayer();

    // Video control methods
    bool loadVideo(const QString& filePath);
    bool loadVideo(const QString& filePath, bool autoEnterVR);  // Overload with VR auto-enter option
    void play();
    void pause();
    void stop();
    void seek(qint64 position);
    qint64 duration() const;
    qint64 position() const;
    bool isPlaying() const { return m_isPlaying; }
    
    // VR-specific methods
    bool initializeVR();
    void shutdownVR();
    bool isVRAvailable() const;
    bool isVRActive() const { return m_vrActive; }
    
    // Video format detection and setting
    void setVideoFormat(VRVideoRenderer::VideoFormat format);
    VRVideoRenderer::VideoFormat getVideoFormat() const;
    VRVideoRenderer::VideoFormat detectVideoFormat(const QString& filePath) const;
    
    // VR control
    void enterVRMode();
    void exitVRMode();
    void toggleVRMode();
    
    // Video adjustments
    void setVideoBrightness(float brightness);
    void setVideoContrast(float contrast);
    void setVideoSaturation(float saturation);

signals:
    void vrStatusChanged(bool vrActive);
    void vrError(const QString& error);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void playbackStateChanged(bool playing);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onVRStatusChanged(VROpenVRManager::VRStatus status);
    void onVRError(const QString& error);
    void onRenderFrame();
    void updateVideoFrame();
    void onPlayPauseClicked();
    void onStopClicked();
    void onCloseClicked();
    void onPositionSliderPressed();
    void onPositionSliderReleased();
    void onPositionSliderMoved(int position);
    void onFormatComboBoxChanged(int index);
    void onProjectionComboBoxChanged(int index);
    void onIPDSpinBoxChanged(int value);
    void onZoomSliderChanged(int value);
    void onSpeedSliderChanged(int value);
    void onVolumeSliderChanged(int value);
    void updatePlaybackPosition();
    void processControllerInput();  // VR controller input processing

private:
    bool setupVRComponents();
    void cleanupVRComponents();
    bool startVRRendering();
    void stopVRRendering();
    void showVRErrorMessage(const QString& message);
    
    // OpenGL widget for VR rendering context
    QOpenGLWidget* createOpenGLWidget();
    
    // UI methods
    void setupUI();
    void updateUIState();
    ClickableSlider* createClickableSlider();
    
    // Focus restoration
    void restoreFocusDelayed();
    
    // Playback speed control methods
    void increasePlaybackSpeed();
    void decreasePlaybackSpeed();
    void resetPlaybackSpeed();
    void setPlaybackSpeed(qreal speed);
    
    // VLC volume control methods
    void increaseVLCVolume();
    void decreaseVLCVolume();

private:
    // VR components
    std::unique_ptr<VROpenVRManager> m_vrManager;
    std::unique_ptr<VRVideoRenderer> m_vrRenderer;
    std::unique_ptr<VRRenderThread> m_renderThread;
    std::unique_ptr<VRVLCFrameExtractor> m_frameExtractor;
    
    // OpenGL context
    QOpenGLWidget* m_glWidget;
    QOpenGLContext* m_glContext;
    QMutex m_glContextMutex;  // Mutex for thread-safe context operations
    
    // UI components  
    QLabel* m_statusLabel;
    QLabel* m_fileLabel;
    QPushButton* m_playPauseButton;
    QPushButton* m_stopButton;
    QPushButton* m_closeButton;
    QLabel* m_positionLabel;
    QLabel* m_currentTimeLabel;     // Current playback time label
    QLabel* m_totalTimeLabel;       // Total duration time label
    QSlider* m_positionSlider;      // New position slider for seeking
    
    // VR-specific UI controls
    QComboBox* m_formatComboBox;      // Video format selection (mono/stereo)
    QComboBox* m_projectionComboBox;  // Projection type (flat/180/360)
    QSpinBox* m_ipdSpinBox;           // IPD adjustment
    QSlider* m_zoomSlider;            // Zoom control
    QSlider* m_speedSlider;           // Playback speed control
    QSlider* m_volumeSlider;          // VLC volume control
    QLabel* m_formatLabel;
    QLabel* m_projectionLabel;
    QLabel* m_ipdLabel;
    QLabel* m_zoomLabel;
    QLabel* m_zoomValueLabel;
    QLabel* m_speedLabel;
    QLabel* m_speedValueLabel;
    QLabel* m_volumeLabel;
    QLabel* m_volumeValueLabel;
    
    // VR state
    bool m_vrAvailable;
    bool m_vrActive;
    bool m_vrInitialized;
    
    // Video state
    QString m_currentFilePath;
    bool m_isPlaying;
    bool m_videoLoaded;
    bool m_isSliderBeingMoved;
    qint64 m_duration;
    qint64 m_position;
    bool m_firstPlay;                 // Track if this is the first play for auto-centering
    qreal m_currentPlaybackSpeed;     // Current playback speed (1.0 = normal)
    
    // VLC player for video decoding
    std::unique_ptr<VP_VLCPlayer> m_vlcPlayer;
    
    // Video format
    VRVideoRenderer::VideoFormat m_videoFormat;
    
    // Timer manager for all timers
    SafeTimerManager m_timerManager;
    
    // Current video frame (from libVLC)
    QImage m_currentFrame;
    QMutex m_frameMutex;
    
    // VR Controller input state
    QVector2D m_lastSeekAxis;           // Previous axis values for smooth interaction
    bool m_controllerInputActive;       // Whether controller input is being processed
    
    // Continuous recenter state
    bool m_spacebarHeld;                // Track if spacebar is currently held
    bool m_grabButtonHeld;              // Track if grab button is currently held
};

/**
 * @class VRRenderThread
 * @brief Dedicated thread for VR rendering to maintain performance
 */
class VRRenderThread : public QThread
{
    Q_OBJECT

public:
    explicit VRRenderThread(VROpenVRManager* vrManager, 
                           VRVideoRenderer* vrRenderer,
                           VRVLCFrameExtractor* frameExtractor,
                           QObject *parent = nullptr);
    ~VRRenderThread();

    void startRendering();
    void stopRendering();
    void setShareContext(QOpenGLContext* context) { m_shareContext = context; }
    void setFrameExtractor(VRVLCFrameExtractor* extractor) { m_frameExtractor = extractor; }
    
    bool isRendering() const { return m_rendering; }
    
    // VR recentering
    void recenterView() { m_needsRecenter = true; }
    void resetRecenterOffset() { 
        m_recenterRotationOffset.setToIdentity(); 
        qDebug() << "VRRenderThread: Recenter offset reset to identity";
    }
    
    // Video scale/zoom control
    float getVideoScale() const { return m_videoScale; }
    void setVideoScale(float scale) { 
        m_videoScale = qBound(0.1f, scale, 5.0f);
        qDebug() << "VRRenderThread: Video scale set to" << m_videoScale;
    }
    void adjustVideoScale(float delta) {
        setVideoScale(m_videoScale + delta);
    }
    
    // IPD (interpupillary distance) adjustment
    float getIPDScale() const { return m_ipdScale; }
    void setIPDScale(float scale) {
        m_ipdScale = qBound(0.1f, scale, 3.0f);
        qDebug() << "VRRenderThread: IPD scale set to" << m_ipdScale;
    }
    void adjustIPDScale(float delta) {
        setIPDScale(m_ipdScale + delta);
    }

signals:
    void frameRendered();
    void error(const QString& errorMessage);

protected:
    void run() override;

private:
    void renderFrame();

private:
    VROpenVRManager* m_vrManager;
    VRVideoRenderer* m_vrRenderer;
    VRVLCFrameExtractor* m_frameExtractor;
    
    bool m_rendering;
    bool m_stopRequested;
    
    QImage m_currentFrame;
    QMutex m_frameMutex;
    bool m_frameUpdated;
    
    QOpenGLContext* m_shareContext;
    
    // VR recentering
    QMatrix4x4 m_recenterRotationOffset;
    bool m_needsRecenter;
    
    // Video scale/zoom
    float m_videoScale;
    
    // IPD adjustment
    float m_ipdScale;
    
    // Thread safety
    mutable QMutex m_contextMutex;
};

#endif // VR_VIDEO_PLAYER_H
