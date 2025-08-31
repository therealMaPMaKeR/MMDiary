#include "vr_video_player.h"
#include <QDebug>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QFileInfo>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QLabel>

//=============================================================================
// VRVideoPlayer Implementation
//=============================================================================

VRVideoPlayer::VRVideoPlayer(QWidget *parent)
    : BaseVideoPlayer(parent)
    , m_glWidget(nullptr)
    , m_glContext(nullptr)
    , m_vrAvailable(false)
    , m_vrActive(false)
    , m_vrInitialized(false)
    , m_videoFormat(VRVideoRenderer::VideoFormat::Mono360)
    , m_frameTimer(new QTimer(this))
{
    qDebug() << "VRVideoPlayer: Constructor called";
    
    // Set up frame update timer for VR rendering
    m_frameTimer->setInterval(16); // ~60 FPS
    connect(m_frameTimer, &QTimer::timeout, this, &VRVideoPlayer::updateVideoFrame);
    
    // Try to initialize VR on startup to check availability
    initializeVR();
}

VRVideoPlayer::~VRVideoPlayer()
{
    qDebug() << "VRVideoPlayer: Destructor called";
    shutdownVR();
    
    if (m_glWidget) {
        delete m_glWidget;
        m_glWidget = nullptr;
    }
}

bool VRVideoPlayer::initializeVR()
{
    qDebug() << "VRVideoPlayer: Initializing VR system";
    
    if (m_vrInitialized) {
        qDebug() << "VRVideoPlayer: VR already initialized";
        return true;
    }
    
    // Create VR manager
    m_vrManager = std::make_unique<VROpenVRManager>(this);
    
    // Connect VR manager signals
    connect(m_vrManager.get(), &VROpenVRManager::statusChanged,
            this, &VRVideoPlayer::onVRStatusChanged);
    connect(m_vrManager.get(), &VROpenVRManager::error,
            this, &VRVideoPlayer::onVRError);
    
    // Try to initialize OpenVR
    if (!m_vrManager->initialize()) {
        // VR not available - show error and fall back to regular player
        QString errorMsg = m_vrManager->getLastError();
        if (errorMsg.contains("SteamVR")) {
            showVRErrorMessage("SteamVR could not be found. Falling back to regular video player.");
        } else if (errorMsg.contains("headset")) {
            showVRErrorMessage("No VR headset detected. Falling back to regular video player.");
        } else {
            showVRErrorMessage(QString("VR initialization failed: %1. Falling back to regular video player.").arg(errorMsg));
        }
        
        m_vrAvailable = false;
        m_vrManager.reset();
        return false;
    }
    
    m_vrAvailable = true;
    m_vrInitialized = true;
    
    // Set up VR components
    if (!setupVRComponents()) {
        qDebug() << "VRVideoPlayer: Failed to setup VR components";
        m_vrAvailable = false;
        m_vrInitialized = false;
        return false;
    }
    
    qDebug() << "VRVideoPlayer: VR initialization successful";
    return true;
}

void VRVideoPlayer::shutdownVR()
{
    qDebug() << "VRVideoPlayer: Shutting down VR system";
    
    if (m_vrActive) {
        stopVRRendering();
    }
    
    cleanupVRComponents();
    
    if (m_vrManager) {
        m_vrManager->shutdown();
        m_vrManager.reset();
    }
    
    m_vrInitialized = false;
    m_vrAvailable = false;
    m_vrActive = false;
    
    qDebug() << "VRVideoPlayer: VR shutdown complete";
}

bool VRVideoPlayer::isVRAvailable() const
{
    return m_vrAvailable && m_vrManager && m_vrManager->isHMDPresent();
}

bool VRVideoPlayer::setupVRComponents()
{
    qDebug() << "VRVideoPlayer: Setting up VR components";
    
    // Create OpenGL widget for rendering context
    m_glWidget = createOpenGLWidget();
    if (!m_glWidget) {
        qDebug() << "VRVideoPlayer: Failed to create OpenGL widget";
        return false;
    }
    
    // Make context current
    m_glWidget->makeCurrent();
    
    // Create VR renderer
    m_vrRenderer = std::make_unique<VRVideoRenderer>(this);
    if (!m_vrRenderer->initialize()) {
        qDebug() << "VRVideoPlayer: Failed to initialize VR renderer";
        m_glWidget->doneCurrent();
        return false;
    }
    
    // Set render target size from VR system
    uint32_t width, height;
    m_vrManager->getRecommendedRenderTargetSize(width, height);
    m_vrRenderer->setRenderTargetSize(width, height);
    
    // Create render thread
    m_renderThread = std::make_unique<VRRenderThread>(
        m_vrManager.get(), m_vrRenderer.get(), this);
    
    connect(m_renderThread.get(), &VRRenderThread::error,
            this, &VRVideoPlayer::onVRError);
    
    m_glWidget->doneCurrent();
    
    qDebug() << "VRVideoPlayer: VR components setup complete";
    return true;
}

void VRVideoPlayer::cleanupVRComponents()
{
    qDebug() << "VRVideoPlayer: Cleaning up VR components";
    
    if (m_renderThread) {
        if (m_renderThread->isRunning()) {
            m_renderThread->stopRendering();
            m_renderThread->wait(1000);
        }
        m_renderThread.reset();
    }
    
    if (m_glWidget) {
        m_glWidget->makeCurrent();
        
        if (m_vrRenderer) {
            m_vrRenderer->cleanup();
            m_vrRenderer.reset();
        }
        
        m_glWidget->doneCurrent();
    }
}

bool VRVideoPlayer::loadVideo(const QString& filePath)
{
    // Call the overloaded version with autoEnterVR = false
    return loadVideo(filePath, false);
}

bool VRVideoPlayer::loadVideo(const QString& filePath, bool autoEnterVR)
{
    qDebug() << "VRVideoPlayer: Loading video:" << filePath << "(autoEnterVR:" << autoEnterVR << ")";
    
    // Load video using base class
    if (!BaseVideoPlayer::loadVideo(filePath)) {
        return false;
    }
    
    // Detect video format
    VRVideoRenderer::VideoFormat format = detectVideoFormat(filePath);
    setVideoFormat(format);
    
    qDebug() << "VRVideoPlayer: Detected video format:" << static_cast<int>(format);
    
    // If autoEnterVR is true, enter VR mode regardless of format (user explicitly chose VR player)
    if (autoEnterVR && isVRAvailable()) {
        qDebug() << "VRVideoPlayer: Auto-entering VR mode as requested";
        enterVRMode();
    }
    // Otherwise, only offer for VR-formatted videos
    else if (!autoEnterVR && isVRAvailable() && format != VRVideoRenderer::VideoFormat::Flat2D) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            "VR Video Detected",
            "This appears to be a VR video. Would you like to view it in VR?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        
        if (reply == QMessageBox::Yes) {
            enterVRMode();
        }
    }
    
    return true;
}

void VRVideoPlayer::play()
{
    qDebug() << "VRVideoPlayer: Play requested";
    
    BaseVideoPlayer::play();
    
    if (m_vrActive) {
        m_frameTimer->start();
    }
}

void VRVideoPlayer::pause()
{
    qDebug() << "VRVideoPlayer: Pause requested";
    
    BaseVideoPlayer::pause();
    
    if (m_vrActive) {
        m_frameTimer->stop();
    }
}

void VRVideoPlayer::stop()
{
    qDebug() << "VRVideoPlayer: Stop requested";
    
    BaseVideoPlayer::stop();
    
    if (m_vrActive) {
        m_frameTimer->stop();
        exitVRMode();
    }
}

void VRVideoPlayer::setVideoFormat(VRVideoRenderer::VideoFormat format)
{
    m_videoFormat = format;
    
    if (m_vrRenderer) {
        m_vrRenderer->setVideoFormat(format);
    }
}

VRVideoRenderer::VideoFormat VRVideoPlayer::getVideoFormat() const
{
    return m_videoFormat;
}

VRVideoRenderer::VideoFormat VRVideoPlayer::detectVideoFormat(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName().toLower();
    
    qDebug() << "VRVideoPlayer: Detecting format for:" << fileName;
    
    // Check for common VR video naming patterns
    QRegularExpression stereo360TB("(360|vr).*(tb|top.?bottom|over.?under)", 
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpression stereo360SBS("(360|vr).*(sbs|side.?by.?side|lr|left.?right)", 
                                    QRegularExpression::CaseInsensitiveOption);
    QRegularExpression mono360("(360|spherical|equirectangular)", 
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpression stereo180TB("180.*(tb|top.?bottom|over.?under)", 
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpression stereo180SBS("180.*(sbs|side.?by.?side|lr|left.?right)", 
                                    QRegularExpression::CaseInsensitiveOption);
    QRegularExpression mono180("180|hemisphere", 
                               QRegularExpression::CaseInsensitiveOption);
    
    if (stereo360TB.match(fileName).hasMatch()) {
        return VRVideoRenderer::VideoFormat::Stereo360_TB;
    } else if (stereo360SBS.match(fileName).hasMatch()) {
        return VRVideoRenderer::VideoFormat::Stereo360_SBS;
    } else if (stereo180TB.match(fileName).hasMatch()) {
        return VRVideoRenderer::VideoFormat::Stereo180_TB;
    } else if (stereo180SBS.match(fileName).hasMatch()) {
        return VRVideoRenderer::VideoFormat::Stereo180_SBS;
    } else if (mono180.match(fileName).hasMatch()) {
        return VRVideoRenderer::VideoFormat::Mono180;
    } else if (mono360.match(fileName).hasMatch()) {
        return VRVideoRenderer::VideoFormat::Mono360;
    }
    
    // Default to flat 2D if no VR format detected
    return VRVideoRenderer::VideoFormat::Flat2D;
}

void VRVideoPlayer::enterVRMode()
{
    qDebug() << "VRVideoPlayer: Entering VR mode";
    
    if (!isVRAvailable()) {
        showVRErrorMessage("VR is not available. Please check your VR headset connection.");
        return;
    }
    
    if (m_vrActive) {
        qDebug() << "VRVideoPlayer: Already in VR mode";
        return;
    }
    
    // Start VR rendering
    if (!startVRRendering()) {
        showVRErrorMessage("Failed to start VR rendering.");
        return;
    }
    
    m_vrActive = true;
    
    // Hide regular video widget and show message
    if (m_videoWidget) {
        m_videoWidget->hide();
    }
    
    // Create a label to show VR status
    QLabel* vrLabel = new QLabel("VR Mode Active - Video is displayed in your headset", this);
    vrLabel->setAlignment(Qt::AlignCenter);
    vrLabel->setStyleSheet("QLabel { background-color: black; color: white; font-size: 24px; }");
    vrLabel->setGeometry(m_videoWidget->geometry());
    vrLabel->show();
    
    emit vrStatusChanged(true);
    
    qDebug() << "VRVideoPlayer: Entered VR mode successfully";
}

void VRVideoPlayer::exitVRMode()
{
    qDebug() << "VRVideoPlayer: Exiting VR mode";
    
    if (!m_vrActive) {
        qDebug() << "VRVideoPlayer: Not in VR mode";
        return;
    }
    
    // Stop VR rendering
    stopVRRendering();
    
    m_vrActive = false;
    
    // Show regular video widget
    if (m_videoWidget) {
        m_videoWidget->show();
    }
    
    emit vrStatusChanged(false);
    
    qDebug() << "VRVideoPlayer: Exited VR mode successfully";
}

void VRVideoPlayer::toggleVRMode()
{
    if (m_vrActive) {
        exitVRMode();
    } else {
        enterVRMode();
    }
}

bool VRVideoPlayer::startVRRendering()
{
    qDebug() << "VRVideoPlayer: Starting VR rendering";
    
    if (!m_renderThread) {
        qDebug() << "VRVideoPlayer: No render thread available";
        return false;
    }
    
    m_renderThread->startRendering();
    m_frameTimer->start();
    
    return true;
}

void VRVideoPlayer::stopVRRendering()
{
    qDebug() << "VRVideoPlayer: Stopping VR rendering";
    
    m_frameTimer->stop();
    
    if (m_renderThread && m_renderThread->isRendering()) {
        m_renderThread->stopRendering();
        m_renderThread->wait(1000);
    }
}

void VRVideoPlayer::setVideoBrightness(float brightness)
{
    if (m_vrRenderer) {
        m_vrRenderer->setVideoBrightness(brightness);
    }
}

void VRVideoPlayer::setVideoContrast(float contrast)
{
    if (m_vrRenderer) {
        m_vrRenderer->setVideoContrast(contrast);
    }
}

void VRVideoPlayer::setVideoSaturation(float saturation)
{
    if (m_vrRenderer) {
        m_vrRenderer->setVideoSaturation(saturation);
    }
}

void VRVideoPlayer::keyPressEvent(QKeyEvent *event)
{
    // Handle VR-specific keyboard shortcuts
    if (event->key() == Qt::Key_V && event->modifiers() & Qt::ControlModifier) {
        if (isVRAvailable()) {
            toggleVRMode();
        }
        return;
    }
    
    // Pass to base class
    BaseVideoPlayer::keyPressEvent(event);
}

void VRVideoPlayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "VRVideoPlayer: Close event received";
    
    // Exit VR mode if active
    if (m_vrActive) {
        exitVRMode();
    }
    
    // Clean up VR
    shutdownVR();
    
    // Pass to base class
    BaseVideoPlayer::closeEvent(event);
}

void VRVideoPlayer::onVRStatusChanged(VROpenVRManager::VRStatus status)
{
    qDebug() << "VRVideoPlayer: VR status changed:" << static_cast<int>(status);
    
    switch (status) {
        case VROpenVRManager::VRStatus::Ready:
            m_vrAvailable = true;
            break;
            
        case VROpenVRManager::VRStatus::SteamVRNotFound:
        case VROpenVRManager::VRStatus::NoHMDConnected:
        case VROpenVRManager::VRStatus::InitializationFailed:
        case VROpenVRManager::VRStatus::Error:
            m_vrAvailable = false;
            if (m_vrActive) {
                exitVRMode();
            }
            break;
            
        default:
            break;
    }
}

void VRVideoPlayer::onVRError(const QString& error)
{
    qDebug() << "VRVideoPlayer: VR error:" << error;
    emit vrError(error);
    
    // Exit VR mode on error
    if (m_vrActive) {
        exitVRMode();
    }
}

void VRVideoPlayer::onRenderFrame()
{
    // This slot can be used for additional frame rendering logic
}

void VRVideoPlayer::updateVideoFrame()
{
    if (!m_vrActive || !m_renderThread) {
        return;
    }
    
    // Get current frame from video player
    // This is a simplified version - you'll need to extract frames from libVLC
    // For now, we'll use a placeholder
    
    // TODO: Extract current frame from libVLC and pass to render thread
    // m_renderThread->updateVideoFrame(currentFrame);
}

void VRVideoPlayer::showVRErrorMessage(const QString& message)
{
    qDebug() << "VRVideoPlayer:" << message;
    
    // Show error message to user
    QMessageBox::warning(this, "VR Video Player", message);
}

QOpenGLWidget* VRVideoPlayer::createOpenGLWidget()
{
    qDebug() << "VRVideoPlayer: Creating OpenGL widget";
    
    // Set up OpenGL format
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0); // No multisampling for VR (handled by compositor)
    
    QOpenGLWidget* glWidget = new QOpenGLWidget();
    glWidget->setFormat(format);
    glWidget->setParent(nullptr); // Hidden widget for context only
    glWidget->resize(1, 1);
    glWidget->show();
    glWidget->hide();
    
    return glWidget;
}

//=============================================================================
// VRRenderThread Implementation
//=============================================================================

VRRenderThread::VRRenderThread(VROpenVRManager* vrManager, 
                               VRVideoRenderer* vrRenderer,
                               QObject *parent)
    : QThread(parent)
    , m_vrManager(vrManager)
    , m_vrRenderer(vrRenderer)
    , m_rendering(false)
    , m_stopRequested(false)
    , m_frameUpdated(false)
{
    qDebug() << "VRRenderThread: Constructor called";
}

VRRenderThread::~VRRenderThread()
{
    qDebug() << "VRRenderThread: Destructor called";
    stopRendering();
}

void VRRenderThread::startRendering()
{
    qDebug() << "VRRenderThread: Starting rendering";
    
    if (m_rendering) {
        qDebug() << "VRRenderThread: Already rendering";
        return;
    }
    
    m_rendering = true;
    m_stopRequested = false;
    start();
}

void VRRenderThread::stopRendering()
{
    qDebug() << "VRRenderThread: Stopping rendering";
    
    if (!m_rendering) {
        return;
    }
    
    m_stopRequested = true;
    
    if (isRunning()) {
        wait(1000);
        if (isRunning()) {
            qDebug() << "VRRenderThread: Force terminating thread";
            terminate();
        }
    }
    
    m_rendering = false;
}

void VRRenderThread::updateVideoFrame(const QImage& frame)
{
    QMutexLocker locker(&m_frameMutex);
    m_currentFrame = frame;
    m_frameUpdated = true;
}

void VRRenderThread::run()
{
    qDebug() << "VRRenderThread: Thread started";
    
    while (!m_stopRequested) {
        renderFrame();
        
        // Sleep briefly to maintain frame rate
        msleep(11); // ~90 FPS to match most VR displays
    }
    
    qDebug() << "VRRenderThread: Thread stopped";
}

void VRRenderThread::renderFrame()
{
    if (!m_vrManager || !m_vrRenderer) {
        return;
    }
    
    // Wait for poses from compositor
    m_vrManager->compositorWaitGetPoses();
    
    // Update video texture if we have a new frame
    if (m_frameUpdated) {
        QMutexLocker locker(&m_frameMutex);
        m_vrRenderer->updateVideoTexture(m_currentFrame);
        m_frameUpdated = false;
    }
    
    // Get view and projection matrices for each eye
    QMatrix4x4 hmdPose = m_vrManager->getHMDPoseMatrix();
    QMatrix4x4 leftProj = m_vrManager->getProjectionMatrix(true, 0.1f, 1000.0f);
    QMatrix4x4 rightProj = m_vrManager->getProjectionMatrix(false, 0.1f, 1000.0f);
    QMatrix4x4 leftEyePos = m_vrManager->getEyePosMatrix(true);
    QMatrix4x4 rightEyePos = m_vrManager->getEyePosMatrix(false);
    
    // Calculate view matrices
    QMatrix4x4 leftView = (hmdPose * leftEyePos).inverted();
    QMatrix4x4 rightView = (hmdPose * rightEyePos).inverted();
    
    // Render each eye
    m_vrRenderer->renderEye(true, leftView, leftProj);
    m_vrRenderer->renderEye(false, rightView, rightProj);
    
    // Submit frames to VR compositor
    GLuint leftTex = m_vrRenderer->getEyeTexture(true);
    GLuint rightTex = m_vrRenderer->getEyeTexture(false);
    
    if (!m_vrManager->submitFrame(leftTex, rightTex)) {
        emit error("Failed to submit frame to VR compositor");
    }
    
    emit frameRendered();
}
