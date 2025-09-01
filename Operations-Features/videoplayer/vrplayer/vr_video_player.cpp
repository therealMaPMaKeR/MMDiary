#include "vr_video_player.h"
#include "../vp_vlcplayer.h"
#include <QDebug>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QFileInfo>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QKeyEvent>
#include <QCloseEvent>

//=============================================================================
// VRVideoPlayer Implementation
//=============================================================================

VRVideoPlayer::VRVideoPlayer(QWidget *parent)
    : QWidget(parent)
    , m_glWidget(nullptr)
    , m_glContext(nullptr)
    , m_statusLabel(nullptr)
    , m_fileLabel(nullptr)
    , m_playPauseButton(nullptr)
    , m_stopButton(nullptr)
    , m_exitVRButton(nullptr)
    , m_positionLabel(nullptr)
    , m_vrAvailable(false)
    , m_vrActive(false)
    , m_vrInitialized(false)
    , m_isPlaying(false)
    , m_videoLoaded(false)
    , m_isSliderBeingMoved(false)
    , m_duration(0)
    , m_position(0)
    , m_videoFormat(VRVideoRenderer::VideoFormat::Mono360)
    , m_frameTimer(new QTimer(this))
    , m_positionTimer(new QTimer(this))
{
    qDebug() << "VRVideoPlayer: Constructor called";
    
    // Initialize VLC player first
    m_vlcPlayer = std::make_unique<VP_VLCPlayer>(this);
    if (!m_vlcPlayer->initialize()) {
        qDebug() << "VRVideoPlayer: Failed to initialize VLC player";
        QMessageBox::critical(this, "Error", "Failed to initialize VLC player. Make sure VLC is properly installed.");
    } else {
        qDebug() << "VRVideoPlayer: VLC player initialized successfully";
    }
    
    // Connect VLC player signals
    connect(m_vlcPlayer.get(), &VP_VLCPlayer::durationChanged,
            this, [this](qint64 duration) {
                qDebug() << "VRVideoPlayer: Duration changed to" << duration << "ms";
                m_duration = duration;
                emit durationChanged(duration);
                updatePlaybackPosition();
            });
    
    connect(m_vlcPlayer.get(), &VP_VLCPlayer::positionChanged,
            this, [this](qint64 position) {
                m_position = position;
                if (!m_isSliderBeingMoved) {
                    updatePlaybackPosition();
                }
                emit positionChanged(position);
            });
    
    connect(m_vlcPlayer.get(), &VP_VLCPlayer::errorOccurred,
            this, [this](const QString& error) {
                qDebug() << "VRVideoPlayer: VLC error:" << error;
                QMessageBox::critical(this, "Video Player Error", error);
            });
    
    // Set up UI
    setupUI();
    
    // Set up frame update timer for VR rendering
    // Use a faster interval to match VR display refresh rate
    m_frameTimer->setInterval(11); // ~90 FPS to match most VR displays
    connect(m_frameTimer, &QTimer::timeout, this, &VRVideoPlayer::updateVideoFrame);
    
    // Set up position update timer
    m_positionTimer->setInterval(100); // Update position 10 times per second
    connect(m_positionTimer, &QTimer::timeout, this, &VRVideoPlayer::updatePlaybackPosition);
    
    // Try to initialize VR on startup to check availability
    initializeVR();
}

VRVideoPlayer::~VRVideoPlayer()
{
    qDebug() << "VRVideoPlayer: Destructor called";
    
    // Stop playback
    if (m_isPlaying) {
        stop();
    }
    
    // Clean up VLC player
    if (m_vlcPlayer) {
        m_vlcPlayer->stop();
        m_vlcPlayer->unloadMedia();
    }
    
    // Shutdown VR
    shutdownVR();
    
    // Clean up frame extractor
    if (m_frameExtractor) {
        m_frameExtractor->cleanup();
        m_frameExtractor.reset();
    }
    
    if (m_glWidget) {
        delete m_glWidget;
        m_glWidget = nullptr;
    }
}

void VRVideoPlayer::setupUI()
{
    qDebug() << "VRVideoPlayer: Setting up UI";
    
    // Create main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Status label
    m_statusLabel = new QLabel("VR Video Player Ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; background-color: #2c3e50; padding: 10px; border-radius: 5px; }");
    mainLayout->addWidget(m_statusLabel);
    
    // File label
    m_fileLabel = new QLabel("No video loaded", this);
    m_fileLabel->setAlignment(Qt::AlignCenter);
    m_fileLabel->setStyleSheet("QLabel { font-size: 12px; color: #7f8c8d; margin: 5px; }");
    mainLayout->addWidget(m_fileLabel);
    
    // VR status info
    QLabel* vrInfoLabel = new QLabel(this);
    vrInfoLabel->setAlignment(Qt::AlignCenter);
    vrInfoLabel->setWordWrap(true);
    vrInfoLabel->setStyleSheet("QLabel { font-size: 14px; color: white; background-color: black; padding: 20px; margin: 10px; border: 2px solid #3498db; border-radius: 5px; }");
    vrInfoLabel->setText("\u25cf Video will be displayed in your VR headset\n\n"
                         "\u25cf Use headset controls for navigation\n\n"
                         "\u25cf Press Ctrl+V to toggle VR mode");
    mainLayout->addWidget(vrInfoLabel, 1);
    
    // Control buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    m_playPauseButton = new QPushButton("Play", this);
    m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playPauseButton->setEnabled(false);
    connect(m_playPauseButton, &QPushButton::clicked, this, &VRVideoPlayer::onPlayPauseClicked);
    buttonLayout->addWidget(m_playPauseButton);
    
    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &VRVideoPlayer::onStopClicked);
    buttonLayout->addWidget(m_stopButton);
    
    m_exitVRButton = new QPushButton("Exit VR Mode", this);
    m_exitVRButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_exitVRButton->setEnabled(false);
    connect(m_exitVRButton, &QPushButton::clicked, this, &VRVideoPlayer::onExitVRClicked);
    buttonLayout->addWidget(m_exitVRButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Position label
    m_positionLabel = new QLabel("00:00 / 00:00", this);
    m_positionLabel->setAlignment(Qt::AlignCenter);
    m_positionLabel->setStyleSheet("QLabel { font-size: 12px; color: #95a5a6; margin: 5px; }");
    mainLayout->addWidget(m_positionLabel);
    
    // Set window properties
    setWindowTitle("VR Video Player");
    setMinimumSize(400, 300);
    resize(500, 400);
    
    // Dark theme
    setStyleSheet(
        "QWidget {"
        "    background-color: #1e1e1e;"
        "    color: #ffffff;"
        "}"
        "QPushButton {"
        "    background-color: #3498db;"
        "    color: white;"
        "    border: none;"
        "    padding: 8px;"
        "    border-radius: 4px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: #2980b9;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #21618c;"
        "}"
        "QPushButton:disabled {"
        "    background-color: #7f8c8d;"
        "    color: #bdc3c7;"
        "}"
    );
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
        // VR not available - show error
        QString errorMsg = m_vrManager->getLastError();
        if (errorMsg.contains("SteamVR")) {
            showVRErrorMessage("SteamVR could not be found. Please ensure SteamVR is installed and running.");
        } else if (errorMsg.contains("headset")) {
            showVRErrorMessage("No VR headset detected. Please connect your VR headset and try again.");
        } else {
            showVRErrorMessage(QString("VR initialization failed: %1").arg(errorMsg));
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
    
    // Store the context for sharing
    m_glContext = QOpenGLContext::currentContext();
    
    // Create VR renderer (but don't initialize it here - let the render thread do it)
    m_vrRenderer = std::make_unique<VRVideoRenderer>(this);
    qDebug() << "VRVideoPlayer: Created VR renderer (will initialize in render thread)";
    
    // Create render thread with shared context
    // Note: frameExtractor will be set when video is loaded
    m_renderThread = std::make_unique<VRRenderThread>(
        m_vrManager.get(), m_vrRenderer.get(), nullptr, this);
    
    // Pass the context to the render thread for sharing
    m_renderThread->setShareContext(m_glContext);
    
    connect(m_renderThread.get(), &VRRenderThread::error,
            this, &VRVideoPlayer::onVRError);
    
    connect(m_renderThread.get(), &VRRenderThread::frameRendered,
            this, [this]() {
                // Frame successfully rendered to VR
            });
    
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
    
    // Check if file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::critical(this, "Error", "Video file not found: " + filePath);
        return false;
    }
    
    // Initialize frame extractor BEFORE loading media
    // This ensures VLC callbacks are set up before playing
    if (m_vlcPlayer->getMediaPlayer()) {
        qDebug() << "VRVideoPlayer: Initializing frame extractor";
        
        // Clean up old frame extractor if it exists
        if (m_frameExtractor) {
            m_frameExtractor->cleanup();
            m_frameExtractor.reset();
        }
        
        m_frameExtractor = std::make_unique<VRVLCFrameExtractor>(m_vlcPlayer->getMediaPlayer(), this);
        if (!m_frameExtractor->initialize()) {
            qDebug() << "VRVideoPlayer: Failed to initialize frame extractor";
            m_frameExtractor.reset();
        } else {
            qDebug() << "VRVideoPlayer: Frame extractor initialized successfully";
            
            // Update render thread with frame extractor if it exists
            if (m_renderThread) {
                m_renderThread->setFrameExtractor(m_frameExtractor.get());
            }
            
            // Connect frame ready signal
            connect(m_frameExtractor.get(), &VRVLCFrameExtractor::frameReady,
                    this, &VRVideoPlayer::onFrameReady);
        }
    }
    
    // IMPORTANT: Set video widget to nullptr BEFORE loading media
    // This prevents VLC from trying to render to a widget
    m_vlcPlayer->setVideoWidget(nullptr);
    
    // Now load video with VLC player
    if (!m_vlcPlayer->loadMedia(filePath)) {
        qDebug() << "VRVideoPlayer: Failed to load media in VLC";
        QMessageBox::critical(this, "Error", "Failed to load video file: " + filePath);
        return false;
    }
    
    // Store file path
    m_currentFilePath = filePath;
    m_videoLoaded = true;
    
    // Update UI
    m_fileLabel->setText(fileInfo.fileName());
    m_statusLabel->setText("Video loaded - Ready to play in VR");
    updateUIState();
    
    // Detect video format
    VRVideoRenderer::VideoFormat format = detectVideoFormat(filePath);
    setVideoFormat(format);
    
    // Log the format name for clarity
    const char* formatNames[] = {
        "Mono360", "Stereo360_TB", "Stereo360_SBS",
        "Mono180", "Stereo180_TB", "Stereo180_SBS", "Flat2D"
    };
    qDebug() << "VRVideoPlayer: Detected video format:" << formatNames[static_cast<int>(format)]
             << "(" << static_cast<int>(format) << ")";
    
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
    
    if (!m_videoLoaded) {
        qDebug() << "VRVideoPlayer: No video loaded";
        return;
    }
    
    // Play using VLC player
    if (m_vlcPlayer) {
        // Make sure VLC doesn't render to a widget - we want callbacks only
        m_vlcPlayer->setVideoWidget(nullptr);
        
        m_vlcPlayer->play();
        m_isPlaying = true;
        
        // Start frame timer if in VR mode
        if (m_vrActive) {
            m_frameTimer->start();
        }
        
        // Update UI
        m_playPauseButton->setText("Pause");
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        m_statusLabel->setText("Playing in VR headset");
        
        emit playbackStateChanged(true);
    }
}

void VRVideoPlayer::pause()
{
    qDebug() << "VRVideoPlayer: Pause requested";
    
    // Pause using VLC player
    if (m_vlcPlayer) {
        m_vlcPlayer->pause();
        m_isPlaying = false;
        
        // Stop frame timer if in VR mode
        if (m_vrActive) {
            m_frameTimer->stop();
        }
        
        // Update UI
        m_playPauseButton->setText("Play");
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_statusLabel->setText("Paused");
        
        emit playbackStateChanged(false);
    }
}

void VRVideoPlayer::stop()
{
    qDebug() << "VRVideoPlayer: Stop requested";
    
    // Stop using VLC player
    if (m_vlcPlayer) {
        m_vlcPlayer->stop();
        m_isPlaying = false;
        m_position = 0;
        
        // Stop frame timer
        m_frameTimer->stop();
        
        // Exit VR mode if active
        if (m_vrActive) {
            exitVRMode();
        }
        
        // Update UI
        m_playPauseButton->setText("Play");
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_statusLabel->setText("Stopped");
        updatePlaybackPosition();
        
        emit playbackStateChanged(false);
        emit positionChanged(0);
    }
}

void VRVideoPlayer::seek(qint64 position)
{
    qDebug() << "VRVideoPlayer: Seek to" << position;
    
    if (!m_videoLoaded) {
        return;
    }
    
    // Seek using VLC player
    if (m_vlcPlayer) {
        m_vlcPlayer->setPosition(position);
        m_position = position;
        emit positionChanged(m_position);
        updatePlaybackPosition();
    }
}

qint64 VRVideoPlayer::duration() const
{
    return m_duration;
}

qint64 VRVideoPlayer::position() const
{
    return m_position;
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
    
    // Default to Mono180 for VR player (better for most viewing)
    // 180-degree provides a more comfortable viewing experience
    qDebug() << "VRVideoPlayer: No specific format detected, defaulting to Mono180";
    return VRVideoRenderer::VideoFormat::Mono180;
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
    
    // Update UI to show VR is active
    m_statusLabel->setText("VR Mode Active - Video is displayed in your headset");
    m_exitVRButton->setEnabled(true);
    
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
    
    // Update UI
    m_statusLabel->setText("VR Mode Exited");
    m_exitVRButton->setEnabled(false);
    
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
    
    // Make sure render thread has access to frame extractor
    if (m_frameExtractor) {
        m_renderThread->setFrameExtractor(m_frameExtractor.get());
        qDebug() << "VRVideoPlayer: Frame extractor set in render thread";
    } else {
        qDebug() << "VRVideoPlayer: Warning - No frame extractor available";
    }
    
    m_renderThread->startRendering();
    
    // Start frame timer only if playing
    if (m_isPlaying) {
        m_frameTimer->start();
    }
    
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
    
    // Handle playback controls
    switch (event->key()) {
        case Qt::Key_Space:
            onPlayPauseClicked();
            break;
        case Qt::Key_S:
            onStopClicked();
            break;
        case Qt::Key_Escape:
            if (m_vrActive) {
                exitVRMode();
            }
            break;
        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void VRVideoPlayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "VRVideoPlayer: Close event received";
    
    // Stop playback
    if (m_isPlaying) {
        stop();
    }
    
    // Exit VR mode if active
    if (m_vrActive) {
        exitVRMode();
    }
    
    // Clean up VR
    shutdownVR();
    
    QWidget::closeEvent(event);
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

void VRVideoPlayer::onFrameReady()
{
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 30 == 0) { // Log every 30th frame to avoid spam
        qDebug() << "VRVideoPlayer: Frame ready from VLC, count:" << frameCount;
    }
    
    if (!m_vrActive || !m_renderThread || !m_frameExtractor) {
        if (frameCount % 30 == 0) {
            qDebug() << "VRVideoPlayer: Frame ready but VR not active or components missing";
            qDebug() << "VRVideoPlayer:   m_vrActive:" << m_vrActive 
                     << "m_renderThread:" << (m_renderThread != nullptr)
                     << "m_frameExtractor:" << (m_frameExtractor != nullptr);
        }
        return;
    }
    
    // Get the current frame from the extractor
    QImage frame = m_frameExtractor->getCurrentFrame();
    if (!frame.isNull()) {
        if (frameCount % 30 == 0) {
            qDebug() << "VRVideoPlayer: Passing frame to render thread, size:" 
                     << frame.width() << "x" << frame.height();
        }
        // Pass frame to render thread
        m_renderThread->updateVideoFrame(frame);
    } else {
        if (frameCount % 30 == 0) {
            qDebug() << "VRVideoPlayer: Frame is null!";
        }
    }
}

void VRVideoPlayer::updateVideoFrame()
{
    // This method is now mostly deprecated since the render thread
    // directly accesses frames from the frame extractor for better performance.
    // Keep it for potential fallback scenarios.
    
    if (!m_vrActive || !m_renderThread || !m_frameExtractor) {
        return;
    }
    
    // The render thread now handles frame updates directly
    // This timer just ensures the render thread keeps running
}

void VRVideoPlayer::onPlayPauseClicked()
{
    if (m_isPlaying) {
        pause();
    } else {
        play();
    }
}

void VRVideoPlayer::onStopClicked()
{
    stop();
}

void VRVideoPlayer::onExitVRClicked()
{
    exitVRMode();
}

void VRVideoPlayer::updatePlaybackPosition()
{
    if (!m_videoLoaded) {
        m_positionLabel->setText("00:00 / 00:00");
        return;
    }
    
    // Get current position from VLC player
    if (m_vlcPlayer && m_vlcPlayer->hasMedia()) {
        m_position = m_vlcPlayer->position();
        m_duration = m_vlcPlayer->duration();
    }
    
    // Format time
    auto formatTime = [](qint64 ms) -> QString {
        int seconds = (ms / 1000) % 60;
        int minutes = (ms / 60000) % 60;
        int hours = (ms / 3600000);
        
        if (hours > 0) {
            return QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        } else {
            return QString("%1:%2")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        }
    };
    
    m_positionLabel->setText(QString("%1 / %2")
        .arg(formatTime(m_position))
        .arg(formatTime(m_duration)));
}

void VRVideoPlayer::updateUIState()
{
    // Enable/disable buttons based on state
    bool hasVideo = m_videoLoaded;
    m_playPauseButton->setEnabled(hasVideo);
    m_stopButton->setEnabled(hasVideo && (m_isPlaying || m_position > 0));
    m_exitVRButton->setEnabled(m_vrActive);
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
                               VRVLCFrameExtractor* frameExtractor,
                               QObject *parent)
    : QThread(parent)
    , m_vrManager(vrManager)
    , m_vrRenderer(vrRenderer)
    , m_frameExtractor(frameExtractor)
    , m_rendering(false)
    , m_stopRequested(false)
    , m_frameUpdated(false)
    , m_shareContext(nullptr)
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
    
    // Create OpenGL context for this thread
    QOpenGLContext* context = new QOpenGLContext();
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    context->setFormat(format);
    
    // Share with the main context if available
    if (m_shareContext) {
        context->setShareContext(m_shareContext);
        qDebug() << "VRRenderThread: Sharing OpenGL context with main thread";
    } else {
        qDebug() << "VRRenderThread: Warning - No share context available";
    }
    
    if (!context->create()) {
        qDebug() << "VRRenderThread: Failed to create OpenGL context";
        delete context;
        return;
    }
    
    // Create an offscreen surface for this thread
    QOffscreenSurface* surface = new QOffscreenSurface();
    surface->setFormat(context->format());
    surface->create();
    
    if (!surface->isValid()) {
        qDebug() << "VRRenderThread: Failed to create valid surface";
        delete surface;
        delete context;
        return;
    }
    
    if (!context->makeCurrent(surface)) {
        qDebug() << "VRRenderThread: Failed to make context current";
        delete surface;
        delete context;
        return;
    }
    
    qDebug() << "VRRenderThread: OpenGL context created and made current";
    qDebug() << "VRRenderThread: OpenGL version:" << context->format().majorVersion() 
             << "." << context->format().minorVersion();
    
    // Initialize OpenGL functions
    QOpenGLFunctions* gl = context->functions();
    if (gl) {
        gl->initializeOpenGLFunctions();
        qDebug() << "VRRenderThread: OpenGL functions initialized";
        
        // Log OpenGL info
        const GLubyte* vendor = gl->glGetString(GL_VENDOR);
        const GLubyte* renderer = gl->glGetString(GL_RENDERER);
        const GLubyte* version = gl->glGetString(GL_VERSION);
        qDebug() << "VRRenderThread: OpenGL Vendor:" << reinterpret_cast<const char*>(vendor);
        qDebug() << "VRRenderThread: OpenGL Renderer:" << reinterpret_cast<const char*>(renderer);
        qDebug() << "VRRenderThread: OpenGL Version:" << reinterpret_cast<const char*>(version);
    }
    
    // Ensure VR renderer is initialized in this context
    if (m_vrRenderer) {
        if (!m_vrRenderer->isInitialized()) {
            qDebug() << "VRRenderThread: Initializing VR renderer in render thread context";
            if (!m_vrRenderer->initialize()) {
                qDebug() << "VRRenderThread: Failed to initialize VR renderer";
                context->doneCurrent();
                delete surface;
                delete context;
                return;
            }
            
            // Set render target size from VR system
            if (m_vrManager) {
                uint32_t width, height;
                m_vrManager->getRecommendedRenderTargetSize(width, height);
                m_vrRenderer->setRenderTargetSize(width, height);
                qDebug() << "VRRenderThread: Set render target size to" << width << "x" << height;
            }
        } else {
            qDebug() << "VRRenderThread: VR renderer already initialized";
        }
    }
    
    while (!m_stopRequested) {
        // Ensure context is current before rendering
        if (!context->makeCurrent(surface)) {
            qDebug() << "VRRenderThread: Lost OpenGL context, attempting to restore";
            msleep(100);
            continue;
        }
        
        renderFrame();
        
        // Sleep briefly to maintain frame rate
        msleep(11); // ~90 FPS to match most VR displays
    }
    
    context->doneCurrent();
    delete surface;
    delete context;
    
    qDebug() << "VRRenderThread: Thread stopped";
}

void VRRenderThread::renderFrame()
{
    static int renderCount = 0;
    renderCount++;
    
    if (!m_vrManager || !m_vrRenderer) {
        if (renderCount % 90 == 0) { // Log every second at 90 FPS
            qDebug() << "VRRenderThread: Missing manager or renderer";
        }
        return;
    }
    
    // Wait for poses from compositor
    m_vrManager->compositorWaitGetPoses();
    
    // Update video texture if we have a new frame
    // Use direct buffer access if possible to avoid QImage copies
    if (m_frameExtractor && m_frameExtractor->hasNewFrame()) {
        void* buffer = nullptr;
        unsigned int width = 0, height = 0;
        
        // Try direct buffer access first (much faster)
        if (m_frameExtractor->lockFrameBuffer(&buffer, width, height)) {
            if (renderCount % 90 == 0) {
                qDebug() << "VRRenderThread: Direct texture update from buffer" 
                         << width << "x" << height;
            }
            m_vrRenderer->updateVideoTextureDirect(buffer, width, height);
            m_frameExtractor->unlockFrameBuffer();
        }
        // Fallback to QImage if direct access fails
        else if (m_frameUpdated) {
            QMutexLocker locker(&m_frameMutex);
            if (renderCount % 90 == 0) {
                qDebug() << "VRRenderThread: Fallback to QImage update" 
                         << m_currentFrame.width() << "x" << m_currentFrame.height();
            }
            m_vrRenderer->updateVideoTexture(m_currentFrame);
            m_frameUpdated = false;
        }
    } else if (m_frameUpdated) {
        // No frame extractor, use QImage path
        QMutexLocker locker(&m_frameMutex);
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: QImage update (no extractor)" 
                     << m_currentFrame.width() << "x" << m_currentFrame.height();
        }
        m_vrRenderer->updateVideoTexture(m_currentFrame);
        m_frameUpdated = false;
    } else {
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: No new frame to render";
        }
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
    
    if (leftTex == 0 || rightTex == 0) {
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: Invalid eye textures - left:" << leftTex << "right:" << rightTex;
        }
    }
    
    if (!m_vrManager->submitFrame(leftTex, rightTex)) {
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: Failed to submit frame to VR compositor";
        }
        emit error("Failed to submit frame to VR compositor");
    } else {
        if (renderCount % 900 == 0) { // Log every 10 seconds
            qDebug() << "VRRenderThread: Successfully submitted frame" << renderCount;
        }
    }
    
    emit frameRendered();
}
