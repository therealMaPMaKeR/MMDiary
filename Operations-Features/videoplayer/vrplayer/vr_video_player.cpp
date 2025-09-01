#include "vr_video_player.h"
#include <QShowEvent>
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
#include <QGroupBox>
#include <QGridLayout>
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
    , m_closeButton(nullptr)
    , m_positionLabel(nullptr)
    , m_positionSlider(nullptr)
    , m_formatComboBox(nullptr)
    , m_projectionComboBox(nullptr)
    , m_ipdSpinBox(nullptr)
    , m_zoomSlider(nullptr)
    , m_formatLabel(nullptr)
    , m_projectionLabel(nullptr)
    , m_ipdLabel(nullptr)
    , m_zoomLabel(nullptr)
    , m_zoomValueLabel(nullptr)
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
    
    // Set window flags for modal behavior and always on top
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | 
                   Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                   Qt::WindowStaysOnTopHint);
    setWindowModality(Qt::ApplicationModal);
    qDebug() << "VRVideoPlayer: Set window flags for modal and always on top behavior";
    
    // Try to initialize VR on startup to check availability
    initializeVR();
}

VRVideoPlayer::~VRVideoPlayer()
{
    qDebug() << "VRVideoPlayer: Destructor called";
    
    // Stop playback first
    if (m_isPlaying) {
        stop();
    }
    
    // Exit VR mode if active
    if (m_vrActive) {
        exitVRMode();
    }
    
    // Clean up VLC player
    if (m_vlcPlayer) {
        m_vlcPlayer->stop();
        m_vlcPlayer->unloadMedia();
    }
    
    // Shutdown VR (this will properly clean up OpenGL resources)
    shutdownVR();
    
    // Clean up frame extractor
    if (m_frameExtractor) {
        m_frameExtractor->cleanup();
        m_frameExtractor.reset();
    }
    
    // Delete OpenGL widget last
    if (m_glWidget) {
        delete m_glWidget;
        m_glWidget = nullptr;
    }
    
    qDebug() << "VRVideoPlayer: Destructor complete";
}

void VRVideoPlayer::setupUI()
{
    qDebug() << "VRVideoPlayer: Setting up UI";
    
    // Create main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Status label (hidden per user request)
    m_statusLabel = new QLabel("VR Video Player Ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: white; background-color: #2c3e50; padding: 10px; border-radius: 5px; }");
    m_statusLabel->hide(); // Hide the play status text as requested
    // mainLayout->addWidget(m_statusLabel); // Commented out to not show
    
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
                         "\u25cf Press Spacebar to recenter the video view\n\n"
                         "\u25cf Press P to play/pause the video\n\n"
                         "\u25cf Press Ctrl+V to toggle VR mode\n\n"
                         "\u25cf Press Escape to exit VR mode\n\n"
                         "\u25cf Use the controls below to adjust video format, zoom, and IPD");
    mainLayout->addWidget(vrInfoLabel, 1);
    
    // VR Format Controls
    QGroupBox* formatGroup = new QGroupBox("Video Format Settings", this);
    formatGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #3498db; border-radius: 5px; padding-top: 10px; margin-top: 10px; } "
                                "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QGridLayout* formatLayout = new QGridLayout(formatGroup);
    
    // Video format combo box (mono/stereo)
    m_formatLabel = new QLabel("Video Mode:", this);
    m_formatComboBox = new QComboBox(this);
    m_formatComboBox->addItem("Mono");
    m_formatComboBox->addItem("Stereo Top-Bottom");
    m_formatComboBox->addItem("Stereo Side-by-Side");
    m_formatComboBox->setCurrentIndex(0);
    m_formatComboBox->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    connect(m_formatComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VRVideoPlayer::onFormatComboBoxChanged);
    formatLayout->addWidget(m_formatLabel, 0, 0);
    formatLayout->addWidget(m_formatComboBox, 0, 1);
    
    // Projection type combo box (flat/180/360)
    m_projectionLabel = new QLabel("Projection:", this);
    m_projectionComboBox = new QComboBox(this);
    m_projectionComboBox->addItem("Flat 2D");
    m_projectionComboBox->addItem("180°");
    m_projectionComboBox->addItem("360°");
    m_projectionComboBox->setCurrentIndex(1); // Default to 180
    m_projectionComboBox->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    connect(m_projectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VRVideoPlayer::onProjectionComboBoxChanged);
    formatLayout->addWidget(m_projectionLabel, 0, 2);
    formatLayout->addWidget(m_projectionComboBox, 0, 3);
    
    // IPD adjustment spinbox
    m_ipdLabel = new QLabel("IPD Scale:", this);
    m_ipdSpinBox = new QSpinBox(this);
    m_ipdSpinBox->setRange(10, 300); // 0.1x to 3.0x (as percentage)
    m_ipdSpinBox->setValue(100); // Default 1.0x
    m_ipdSpinBox->setSuffix("%");
    m_ipdSpinBox->setSingleStep(5);
    m_ipdSpinBox->setToolTip("Adjust eye separation to fix double vision");
    m_ipdSpinBox->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    connect(m_ipdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &VRVideoPlayer::onIPDSpinBoxChanged);
    formatLayout->addWidget(m_ipdLabel, 1, 0);
    formatLayout->addWidget(m_ipdSpinBox, 1, 1);
    
    // Zoom slider
    m_zoomLabel = new QLabel("Zoom:", this);
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(10, 500); // 0.1x to 5.0x (as percentage)
    m_zoomSlider->setValue(100); // Default 1.0x
    m_zoomSlider->setSingleStep(10);
    m_zoomSlider->setPageStep(50);
    m_zoomSlider->setToolTip("Adjust video zoom level");
    m_zoomSlider->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    m_zoomValueLabel = new QLabel("100%", this);
    m_zoomValueLabel->setFixedWidth(50);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &VRVideoPlayer::onZoomSliderChanged);
    formatLayout->addWidget(m_zoomLabel, 1, 2);
    formatLayout->addWidget(m_zoomSlider, 1, 3);
    formatLayout->addWidget(m_zoomValueLabel, 1, 4);
    
    mainLayout->addWidget(formatGroup);
    
    // Position slider for seeking
    QLabel* positionSliderLabel = new QLabel("Position:", this);
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setEnabled(false);
    m_positionSlider->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    m_positionSlider->setToolTip("Seek through video playback");
    connect(m_positionSlider, &QSlider::sliderPressed, this, &VRVideoPlayer::onPositionSliderPressed);
    connect(m_positionSlider, &QSlider::sliderReleased, this, &VRVideoPlayer::onPositionSliderReleased);
    connect(m_positionSlider, &QSlider::valueChanged, this, &VRVideoPlayer::onPositionSliderMoved);
    
    QHBoxLayout* positionLayout = new QHBoxLayout();
    positionLayout->addWidget(positionSliderLabel);
    positionLayout->addWidget(m_positionSlider, 1);
    mainLayout->addLayout(positionLayout);
    
    // Control buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    m_playPauseButton = new QPushButton("Play", this);
    m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playPauseButton->setEnabled(false);
    m_playPauseButton->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    connect(m_playPauseButton, &QPushButton::clicked, this, &VRVideoPlayer::onPlayPauseClicked);
    buttonLayout->addWidget(m_playPauseButton);
    
    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setEnabled(false);
    m_stopButton->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    connect(m_stopButton, &QPushButton::clicked, this, &VRVideoPlayer::onStopClicked);
    buttonLayout->addWidget(m_stopButton);
    
    m_closeButton = new QPushButton("Close", this);
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_closeButton->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    connect(m_closeButton, &QPushButton::clicked, this, &VRVideoPlayer::onCloseClicked);
    buttonLayout->addWidget(m_closeButton);
    
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
    
    // Set focus policy to receive keyboard events
    setFocusPolicy(Qt::StrongFocus);
    
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
    
    // Stop VR rendering if active
    if (m_vrActive) {
        stopVRRendering();
        m_vrActive = false;
    }
    
    // Clean up VR components (render thread and renderer)
    cleanupVRComponents();
    
    // Shutdown VR manager
    if (m_vrManager) {
        m_vrManager->shutdown();
        m_vrManager.reset();
    }
    
    m_vrInitialized = false;
    m_vrAvailable = false;
    
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
    
    // First stop the render thread
    if (m_renderThread) {
        if (m_renderThread->isRunning()) {
            qDebug() << "VRVideoPlayer: Stopping render thread";
            m_renderThread->stopRendering();
            if (!m_renderThread->wait(2000)) {
                qDebug() << "VRVideoPlayer: WARNING - Render thread did not stop gracefully";
            }
        }
        m_renderThread.reset();
    }
    
    // Now clean up the renderer with proper context
    if (m_glWidget && m_vrRenderer) {
        // Ensure we have the GL widget context current
        m_glWidget->makeCurrent();
        
        // Check if context is valid
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx) {
            qDebug() << "VRVideoPlayer: Cleaning up VR renderer with valid context";
            m_vrRenderer->cleanup();
        } else {
            qDebug() << "VRVideoPlayer: WARNING - No valid OpenGL context for renderer cleanup";
        }
        
        m_vrRenderer.reset();
        m_glWidget->doneCurrent();
    } else if (m_vrRenderer) {
        qDebug() << "VRVideoPlayer: WARNING - VR renderer exists but no GL widget for cleanup";
        m_vrRenderer.reset();
    }
    
    qDebug() << "VRVideoPlayer: VR components cleanup complete";
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
            //connect(m_frameExtractor.get(), &VRVLCFrameExtractor::frameReady,
            //        this, &VRVideoPlayer::onFrameReady);
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
    // m_statusLabel->setText("Video loaded - Ready to play in VR"); // Status label hidden
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
        // m_statusLabel->setText("Playing in VR headset"); // Status label hidden
        
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
        // m_statusLabel->setText("Paused"); // Status label hidden
        
        emit playbackStateChanged(false);
    }
}

void VRVideoPlayer::stop()
{
    qDebug() << "VRVideoPlayer: Custom stop requested - seek to 0 and pause";
    
    // Custom stop behavior: seek to position 0 and pause instead of full stop
    // This prevents temp file cleanup while allowing user to restart from beginning
    if (m_vlcPlayer) {
        // Seek to beginning
        m_vlcPlayer->setPosition(0);
        m_position = 0;
        
        // Pause playback instead of stopping
        if (m_isPlaying) {
            m_vlcPlayer->pause();
            m_isPlaying = false;
            
            // Stop frame timer if in VR mode
            if (m_vrActive) {
                m_frameTimer->stop();
            }
        }
        
        // Force the play button text to "Play" as requested
        m_playPauseButton->setText("Play");
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        updatePlaybackPosition();
        
        emit playbackStateChanged(false);
        emit positionChanged(0);
        
        qDebug() << "VRVideoPlayer: Custom stop complete - video paused at position 0";
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
    
    // Update UI controls to match the format
    if (m_formatComboBox && m_projectionComboBox) {
        // Determine combo box indices based on format
        int formatIndex = 0; // Default to mono
        int projectionIndex = 1; // Default to 180
        
        switch (format) {
            case VRVideoRenderer::VideoFormat::Flat2D:
                projectionIndex = 0; // Flat
                formatIndex = 0; // Mono (doesn't matter for flat)
                break;
            case VRVideoRenderer::VideoFormat::Mono180:
                projectionIndex = 1; // 180
                formatIndex = 0; // Mono
                break;
            case VRVideoRenderer::VideoFormat::Stereo180_TB:
                projectionIndex = 1; // 180
                formatIndex = 1; // Stereo TB
                break;
            case VRVideoRenderer::VideoFormat::Stereo180_SBS:
                projectionIndex = 1; // 180
                formatIndex = 2; // Stereo SBS
                break;
            case VRVideoRenderer::VideoFormat::Mono360:
                projectionIndex = 2; // 360
                formatIndex = 0; // Mono
                break;
            case VRVideoRenderer::VideoFormat::Stereo360_TB:
                projectionIndex = 2; // 360
                formatIndex = 1; // Stereo TB
                break;
            case VRVideoRenderer::VideoFormat::Stereo360_SBS:
                projectionIndex = 2; // 360
                formatIndex = 2; // Stereo SBS
                break;
            default:
                break;
        }
        
        // Block signals to prevent recursive calls
        m_formatComboBox->blockSignals(true);
        m_projectionComboBox->blockSignals(true);
        
        m_formatComboBox->setCurrentIndex(formatIndex);
        m_projectionComboBox->setCurrentIndex(projectionIndex);
        
        m_formatComboBox->blockSignals(false);
        m_projectionComboBox->blockSignals(false);
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
    // m_statusLabel->setText("VR Mode Active - Space: recenter | P: play/pause"); // Status label hidden
    // Note: Close button is always enabled (no longer exitVR button)
    
    // Ensure this widget has keyboard focus for spacebar handling
    setFocus(Qt::OtherFocusReason);
    qDebug() << "VRVideoPlayer: Setting focus to widget for keyboard input";
    
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
    // m_statusLabel->setText("VR Mode Exited"); // Status label hidden
    // Note: Close button remains enabled (no longer exitVR button)
    
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
    
    // Stop the frame timer
    if (m_frameTimer) {
        m_frameTimer->stop();
    }
    
    // Clear the frame extractor from render thread and stop it
    if (m_renderThread) {
        // Remove the frame extractor reference first
        m_renderThread->setFrameExtractor(nullptr);
        
        if (m_renderThread->isRendering()) {
            m_renderThread->stopRendering();
            if (!m_renderThread->wait(2000)) {
                qDebug() << "VRVideoPlayer: WARNING - Render thread did not stop in time";
            }
        }
    }
    
    qDebug() << "VRVideoPlayer: VR rendering stopped";
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
    qDebug() << "VRVideoPlayer: keyPressEvent - Key:" << event->key() << "VR Active:" << m_vrActive;
    
    // Handle only spacebar and P key as requested
    switch (event->key()) {
        case Qt::Key_Space:
            // In VR mode, spacebar recenters the view
            if (m_vrActive && m_renderThread) {
                qDebug() << "VRVideoPlayer: Spacebar pressed - recentering VR view";
                m_renderThread->recenterView();
                event->accept();
                return; // Important: don't let parent handle this
            } else {
                // When not in VR, spacebar controls play/pause
                qDebug() << "VRVideoPlayer: Spacebar pressed - toggling play/pause";
                onPlayPauseClicked();
                event->accept();
                return; // Important: don't let parent handle this
            }
            break;
        case Qt::Key_P:
            // P key for play/pause (useful in VR mode since spacebar recenters)
            qDebug() << "VRVideoPlayer: P key pressed - toggling play/pause";
            onPlayPauseClicked();
            event->accept();
            break;
        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void VRVideoPlayer::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    
    // Ensure we have keyboard focus when shown
    setFocus(Qt::OtherFocusReason);
    qDebug() << "VRVideoPlayer: showEvent - Setting focus for keyboard input";
}

void VRVideoPlayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "VRVideoPlayer: Close event received";
    
    // Stop playback first
    if (m_isPlaying) {
        stop();
    }
    
    // Exit VR mode if active
    if (m_vrActive) {
        qDebug() << "VRVideoPlayer: Exiting VR mode before closing";
        exitVRMode();
    }
    
    // Clean up VR properly with context
    if (m_vrInitialized) {
        qDebug() << "VRVideoPlayer: Shutting down VR before closing";
        shutdownVR();
    }
    
    QWidget::closeEvent(event);
    qDebug() << "VRVideoPlayer: Close event handled";
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

void VRVideoPlayer::onCloseClicked()
{
    qDebug() << "VRVideoPlayer: Close button clicked - closing player";
    close();
}

void VRVideoPlayer::onPositionSliderPressed()
{
    qDebug() << "VRVideoPlayer: Position slider pressed - pausing updates";
    m_isSliderBeingMoved = true;
}

void VRVideoPlayer::onPositionSliderReleased()
{
    qDebug() << "VRVideoPlayer: Position slider released - resuming updates";
    m_isSliderBeingMoved = false;
    
    // Immediately seek to the new position
    if (m_videoLoaded && m_vlcPlayer) {
        qint64 newPosition = (m_positionSlider->value() * m_duration) / 1000;
        qDebug() << "VRVideoPlayer: Seeking to position" << newPosition << "ms";
        seek(newPosition);
    }
}

void VRVideoPlayer::onPositionSliderMoved(int position)
{
    if (m_isSliderBeingMoved && m_videoLoaded) {
        // Update position label while dragging but don't seek yet
        qint64 newPosition = (position * m_duration) / 1000;
        
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
            .arg(formatTime(newPosition))
            .arg(formatTime(m_duration)));
    }
}

void VRVideoPlayer::onFormatComboBoxChanged(int index)
{
    if (!m_vrRenderer || !m_vrActive) return;
    
    // Get current projection type from projection combo box
    int projectionIndex = m_projectionComboBox ? m_projectionComboBox->currentIndex() : 1;
    
    // Map combo box indices to video formats
    // Format: Mono=0, Stereo TB=1, Stereo SBS=2
    // Projection: Flat=0, 180=1, 360=2
    
    VRVideoRenderer::VideoFormat format = VRVideoRenderer::VideoFormat::Mono180;
    
    if (projectionIndex == 0) { // Flat 2D
        format = VRVideoRenderer::VideoFormat::Flat2D;
    } else if (projectionIndex == 1) { // 180
        switch (index) {
            case 0: format = VRVideoRenderer::VideoFormat::Mono180; break;
            case 1: format = VRVideoRenderer::VideoFormat::Stereo180_TB; break;
            case 2: format = VRVideoRenderer::VideoFormat::Stereo180_SBS; break;
        }
    } else if (projectionIndex == 2) { // 360
        switch (index) {
            case 0: format = VRVideoRenderer::VideoFormat::Mono360; break;
            case 1: format = VRVideoRenderer::VideoFormat::Stereo360_TB; break;
            case 2: format = VRVideoRenderer::VideoFormat::Stereo360_SBS; break;
        }
    }
    
    m_vrRenderer->setVideoFormat(format);
    qDebug() << "VRVideoPlayer: Format changed to index" << index << "with projection" << projectionIndex;
}

void VRVideoPlayer::onProjectionComboBoxChanged(int index)
{
    if (!m_vrRenderer || !m_vrActive) return;
    
    // Get current format from format combo box
    int formatIndex = m_formatComboBox ? m_formatComboBox->currentIndex() : 0;
    
    // Trigger format change to update the combined format
    onFormatComboBoxChanged(formatIndex);
    
    qDebug() << "VRVideoPlayer: Projection changed to index" << index;
}

void VRVideoPlayer::onIPDSpinBoxChanged(int value)
{
    if (!m_renderThread || !m_vrActive) return;
    
    float scale = value / 100.0f;
    m_renderThread->setIPDScale(scale);
    qDebug() << "VRVideoPlayer: IPD scale changed to" << scale;
}

void VRVideoPlayer::onZoomSliderChanged(int value)
{
    if (!m_renderThread || !m_vrActive) return;
    
    float scale = value / 100.0f;
    m_renderThread->setVideoScale(scale);
    
    // Update the value label
    if (m_zoomValueLabel) {
        m_zoomValueLabel->setText(QString("%1%").arg(value));
    }
    
    qDebug() << "VRVideoPlayer: Zoom scale changed to" << scale;
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
    
    // Update position slider if not being moved by user
    if (m_positionSlider && !m_isSliderBeingMoved && m_duration > 0) {
        int sliderPosition = (int)((m_position * 1000) / m_duration);
        m_positionSlider->setValue(sliderPosition);
    }
}

void VRVideoPlayer::updateUIState()
{
    // Enable/disable buttons and controls based on state
    bool hasVideo = m_videoLoaded;
    m_playPauseButton->setEnabled(hasVideo);
    m_stopButton->setEnabled(hasVideo && (m_isPlaying || m_position > 0));
    
    // Enable position slider when video is loaded
    if (m_positionSlider) {
        m_positionSlider->setEnabled(hasVideo);
        if (hasVideo && m_duration > 0) {
            m_positionSlider->setRange(0, 1000); // 0-1000 for percentage-based seeking
        }
    }
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
    , m_needsRecenter(false)
    , m_videoScale(1.0f)
    , m_ipdScale(1.0f)
{
    m_recenterRotationOffset.setToIdentity();
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
    
    qDebug() << "VRRenderThread: Exiting render loop";
    
    // Clean up the renderer's OpenGL resources in this thread's context
    if (m_vrRenderer && m_vrRenderer->isInitialized()) {
        qDebug() << "VRRenderThread: Cleaning up renderer resources in thread context";
        if (context->makeCurrent(surface)) {
            m_vrRenderer->cleanup();
            context->doneCurrent();
        } else {
            qDebug() << "VRRenderThread: WARNING - Could not make context current for cleanup";
        }
    }
    
    // Clean up the context and surface
    if (context->isValid()) {
        context->doneCurrent();
    }
    delete surface;
    delete context;
    
    qDebug() << "VRRenderThread: Thread stopped and cleaned up";
}

void VRRenderThread::renderFrame()
{
    static int renderCount = 0;
    renderCount++;
    
    // Check if we're being shut down
    if (m_stopRequested) {
        return;
    }
    
    // Validate pointers - they might become null during shutdown
    if (!m_vrManager || !m_vrRenderer) {
        if (renderCount % 90 == 0) { // Log every second at 90 FPS
            qDebug() << "VRRenderThread: Missing manager or renderer - likely shutting down";
        }
        return;
    }
    
    // Double-check renderer is still initialized
    if (!m_vrRenderer->isInitialized()) {
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: Renderer not initialized";
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
    } else {
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: No new frame to render";
        }
    }
    
    // Get HMD pose components
    QMatrix4x4 hmdPose = m_vrManager->getHMDPoseMatrix();
    QVector3D hmdPosition = m_vrManager->getHMDPosition();
    QMatrix4x4 hmdRotation = m_vrManager->getHMDRotationMatrix();
    
    // Handle recentering if requested
    if (m_needsRecenter) {
        // Store the inverse of current rotation to reset orientation
        // Apply a -90 degree rotation correction around Y-axis to fix horizontal alignment
        QMatrix4x4 rotationCorrection;
        rotationCorrection.setToIdentity();
        rotationCorrection.rotate(-90.0f, 0.0f, 1.0f, 0.0f); // Rotate -90 degrees around Y-axis
        
        // Apply the correction before inverting to properly align the video
        m_recenterRotationOffset = (hmdRotation * rotationCorrection).inverted();
        m_needsRecenter = false;
        qDebug() << "VRRenderThread: View recentered with rotation correction - video centered in front of user";
    }
    
    // Apply recenter offset to rotation
    QMatrix4x4 adjustedRotation = m_recenterRotationOffset * hmdRotation;
    
    // Get eye position matrices for stereoscopic separation
    // These contain the IPD (interpupillary distance) offsets
    QMatrix4x4 leftEyePos = m_vrManager->getEyePosMatrix(true);
    QMatrix4x4 rightEyePos = m_vrManager->getEyePosMatrix(false);
    
    // Log original IPD values
    float originalLeftX = leftEyePos(0, 3);
    float originalRightX = rightEyePos(0, 3);
    
    // Apply IPD scale to adjust eye separation
    // Scale the X translation component (horizontal eye separation)
    leftEyePos(0, 3) *= m_ipdScale;
    rightEyePos(0, 3) *= m_ipdScale;
    
    // Log IPD adjustment periodically
    static int ipdLogCount = 0;
    if (++ipdLogCount % 300 == 0) { // Every ~3 seconds
        qDebug() << "VRRenderThread: Original IPD - Left:" << originalLeftX << "Right:" << originalRightX;
        qDebug() << "VRRenderThread: Scaled IPD - Left:" << leftEyePos(0, 3) << "Right:" << rightEyePos(0, 3);
        qDebug() << "VRRenderThread: IPD separation:" << (leftEyePos(0, 3) - rightEyePos(0, 3));
    }
    
    // Build the complete eye poses with rotation but without HMD translation
    // This keeps the video sphere centered on the viewer while maintaining stereoscopy
    QMatrix4x4 leftEyePose = adjustedRotation * leftEyePos;
    QMatrix4x4 rightEyePose = adjustedRotation * rightEyePos;
    
    // Create view matrices by inverting the eye poses
    QMatrix4x4 leftView = leftEyePose.inverted();
    QMatrix4x4 rightView = rightEyePose.inverted();
    
    // Get projection matrices WITHOUT zoom - all zoom is handled by dome/texture
    QMatrix4x4 leftProj = m_vrManager->getProjectionMatrixWithZoom(true, 0.1f, 1000.0f, 1.0f);
    QMatrix4x4 rightProj = m_vrManager->getProjectionMatrixWithZoom(false, 0.1f, 1000.0f, 1.0f);
    
    // Using hybrid zoom approach:
    // - Zoom 0-1: Dome angular coverage adjustment (DeoVR-style)
    // - Zoom >1: Texture coordinate zoom (maintains aspect ratio, no distortion)
    
    // Log scale application periodically
    static int scaleLogCount = 0;
    if (++scaleLogCount % 300 == 0) { // Every ~3 seconds
        qDebug() << "VRRenderThread: Video scale (zoom):" << m_videoScale;
        qDebug() << "VRRenderThread: IPD scale:" << m_ipdScale;
        if (m_videoScale <= 1.0f) {
            qDebug() << "VRRenderThread: Using dome angular coverage adjustment (zoom out)";
        } else {
            qDebug() << "VRRenderThread: Using texture coordinate zoom (zoom in, no distortion)";
        }
    }
    
    // Render each eye - pass m_videoScale for DeoVR-style dome zoom
    m_vrRenderer->renderEye(true, leftView, leftProj, m_videoScale);
    m_vrRenderer->renderEye(false, rightView, rightProj, m_videoScale);
    
    // Debug output to verify matrices are different for stereoscopic effect
    static int debugCount = 0;
    if (++debugCount % 900 == 0) { // Log every 10 seconds at 90 FPS
        qDebug() << "VRRenderThread: Stereoscopic check:";
        
        // Get the position components from the transforms
        float leftX = leftEyePose(0, 3);
        float rightX = rightEyePose(0, 3);
        float separation = leftX - rightX;
        
        qDebug() << "VRRenderThread: Left eye X position:" << leftX;
        qDebug() << "VRRenderThread: Right eye X position:" << rightX;
        qDebug() << "VRRenderThread: Eye separation:" << separation;
        
        // Also log the raw eye position matrices
        QVector3D leftEyeOffset(leftEyePos(0,3), leftEyePos(1,3), leftEyePos(2,3));
        QVector3D rightEyeOffset(rightEyePos(0,3), rightEyePos(1,3), rightEyePos(2,3));
        qDebug() << "VRRenderThread: Left eye offset from HMD:" << leftEyeOffset;
        qDebug() << "VRRenderThread: Right eye offset from HMD:" << rightEyeOffset;
        
        // Log view matrix info to check sphere positioning
        QVector3D leftViewPos = leftView.inverted().column(3).toVector3D();
        QVector3D rightViewPos = rightView.inverted().column(3).toVector3D();
        qDebug() << "VRRenderThread: Left view position:" << leftViewPos;
        qDebug() << "VRRenderThread: Right view position:" << rightViewPos;
        
        // Check if recenter offset is identity (no offset)
        bool isIdentity = m_recenterRotationOffset.isIdentity();
        qDebug() << "VRRenderThread: Recenter offset is identity:" << isIdentity;
        
        // Log video format and scale
        if (m_vrRenderer) {
            qDebug() << "VRRenderThread: Video format:" << (int)m_vrRenderer->getVideoFormat();
            qDebug() << "VRRenderThread: Video scale/zoom:" << m_videoScale;
            qDebug() << "VRRenderThread: IPD scale:" << m_ipdScale;
        }
    }
    
    // Submit frames to VR compositor
    GLuint leftTex = m_vrRenderer->getEyeTexture(true);
    GLuint rightTex = m_vrRenderer->getEyeTexture(false);
    
    if (leftTex == 0 || rightTex == 0) {
        if (renderCount % 90 == 0) {
            qDebug() << "VRRenderThread: Invalid eye textures - left:" << leftTex << "right:" << rightTex;
        }
        return; // Don't submit invalid textures
    }
    
    // Debug: Verify textures are different
    if (renderCount % 180 == 0) { // Log every 2 seconds
        qDebug() << "VRRenderThread: Submitting textures - Left:" << leftTex << "Right:" << rightTex
                 << "(Different:" << (leftTex != rightTex) << ")";
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
