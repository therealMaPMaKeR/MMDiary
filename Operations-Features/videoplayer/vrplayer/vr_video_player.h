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
#include <QTimer>
#include <QMutex>
#include <memory>
#include "vr_openvr_manager.h"
#include "vr_video_renderer.h"
#include "vr_vlc_frame_extractor.h"
#include "../vp_vlcplayer.h"

class VRRenderThread;

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
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onVRStatusChanged(VROpenVRManager::VRStatus status);
    void onVRError(const QString& error);
    void onRenderFrame();
    void updateVideoFrame();
    void onPlayPauseClicked();
    void onStopClicked();
    void onExitVRClicked();
    void updatePlaybackPosition();

private:
    bool setupVRComponents();
    void cleanupVRComponents();
    bool startVRRendering();
    void stopVRRendering();
    void showVRErrorMessage(const QString& message);
    
    // OpenGL widget for VR rendering context
    QOpenGLWidget* createOpenGLWidget();
    
    void setupUI();
    void updateUIState();

private:
    // VR components
    std::unique_ptr<VROpenVRManager> m_vrManager;
    std::unique_ptr<VRVideoRenderer> m_vrRenderer;
    std::unique_ptr<VRRenderThread> m_renderThread;
    std::unique_ptr<VRVLCFrameExtractor> m_frameExtractor;
    
    // OpenGL context
    QOpenGLWidget* m_glWidget;
    QOpenGLContext* m_glContext;
    
    // UI components  
    QLabel* m_statusLabel;
    QLabel* m_fileLabel;
    QPushButton* m_playPauseButton;
    QPushButton* m_stopButton;
    QPushButton* m_exitVRButton;
    QLabel* m_positionLabel;
    
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
    
    // VLC player for video decoding
    std::unique_ptr<VP_VLCPlayer> m_vlcPlayer;
    
    // Video format
    VRVideoRenderer::VideoFormat m_videoFormat;
    
    // Timers
    QTimer* m_frameTimer;
    QTimer* m_positionTimer;
    
    // Current video frame (from libVLC)
    QImage m_currentFrame;
    QMutex m_frameMutex;
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
};

#endif // VR_VIDEO_PLAYER_H
