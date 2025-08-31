#ifndef VR_VIDEO_PLAYER_H
#define VR_VIDEO_PLAYER_H

#include "../BaseVideoPlayer.h"
#include "vr_openvr_manager.h"
#include "vr_video_renderer.h"
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <memory>

class VRRenderThread;

/**
 * @class VRVideoPlayer
 * @brief VR-enabled video player that extends BaseVideoPlayer
 * 
 * This class provides VR video playback capabilities:
 * - Automatic VR headset detection
 * - Support for 360°/180° video formats
 * - Stereoscopic rendering
 * - Falls back to regular player if VR not available
 */
class VRVideoPlayer : public BaseVideoPlayer
{
    Q_OBJECT

public:
    explicit VRVideoPlayer(QWidget *parent = nullptr);
    ~VRVideoPlayer() override;

    // Override base class methods
    bool loadVideo(const QString& filePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    
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

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onVRStatusChanged(VROpenVRManager::VRStatus status);
    void onVRError(const QString& error);
    void onRenderFrame();
    void updateVideoFrame();

private:
    bool setupVRComponents();
    void cleanupVRComponents();
    bool startVRRendering();
    void stopVRRendering();
    void showVRErrorMessage(const QString& message);
    
    // OpenGL widget for VR rendering context
    QOpenGLWidget* createOpenGLWidget();

private:
    // VR components
    std::unique_ptr<VROpenVRManager> m_vrManager;
    std::unique_ptr<VRVideoRenderer> m_vrRenderer;
    std::unique_ptr<VRRenderThread> m_renderThread;
    
    // OpenGL context
    QOpenGLWidget* m_glWidget;
    QOpenGLContext* m_glContext;
    
    // State
    bool m_vrAvailable;
    bool m_vrActive;
    bool m_vrInitialized;
    
    // Video format
    VRVideoRenderer::VideoFormat m_videoFormat;
    
    // Frame update timer
    QTimer* m_frameTimer;
    
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
                           QObject *parent = nullptr);
    ~VRRenderThread();

    void startRendering();
    void stopRendering();
    void updateVideoFrame(const QImage& frame);
    
    bool isRendering() const { return m_rendering; }

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
    
    bool m_rendering;
    bool m_stopRequested;
    
    QImage m_currentFrame;
    QMutex m_frameMutex;
    bool m_frameUpdated;
};

#endif // VR_VIDEO_PLAYER_H
