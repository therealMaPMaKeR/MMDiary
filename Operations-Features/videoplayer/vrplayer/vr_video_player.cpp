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
#include <QMouseEvent>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <comdef.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

//=============================================================================
// Windows System Volume Control Functions
//=============================================================================

#ifdef Q_OS_WIN
// Function to adjust Windows system volume
static bool adjustWindowsSystemVolume(float volumeDelta)
{
    qDebug() << "VRVideoPlayer: Adjusting Windows system volume by" << volumeDelta;
    
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        qDebug() << "VRVideoPlayer: Failed to initialize COM";
        return false;
    }
    
    IMMDeviceEnumerator* deviceEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                         __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
    if (FAILED(hr)) {
        qDebug() << "VRVideoPlayer: Failed to create device enumerator";
        CoUninitialize();
        return false;
    }
    
    IMMDevice* defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) {
        qDebug() << "VRVideoPlayer: Failed to get default audio endpoint";
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    IAudioEndpointVolume* endpointVolume = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                                NULL, (LPVOID*)&endpointVolume);
    if (FAILED(hr)) {
        qDebug() << "VRVideoPlayer: Failed to activate audio endpoint volume";
        defaultDevice->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    // Get current volume
    float currentVolume = 0.0f;
    hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    if (FAILED(hr)) {
        qDebug() << "VRVideoPlayer: Failed to get current volume";
        endpointVolume->Release();
        defaultDevice->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    // Calculate new volume (clamp between 0.0 and 1.0)
    float newVolume = qBound(0.0f, currentVolume + volumeDelta, 1.0f);
    
    // Set new volume
    hr = endpointVolume->SetMasterVolumeLevelScalar(newVolume, NULL);
    if (FAILED(hr)) {
        qDebug() << "VRVideoPlayer: Failed to set new volume";
        endpointVolume->Release();
        defaultDevice->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    qDebug() << "VRVideoPlayer: Windows system volume changed from" << (currentVolume * 100) << "% to" << (newVolume * 100) << "%";
    
    // Cleanup
    endpointVolume->Release();
    defaultDevice->Release();
    deviceEnumerator->Release();
    CoUninitialize();
    
    return true;
}

static void increaseWindowsVolume()
{
    adjustWindowsSystemVolume(0.05f); // Increase by 5%
}

static void decreaseWindowsVolume()
{
    adjustWindowsSystemVolume(-0.05f); // Decrease by 5%
}
#endif

//=============================================================================
// ClickableSlider class for VR Video Player
//=============================================================================

class ClickableSlider : public QSlider
{
public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent), m_isPressed(false) {}

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_isPressed = true;
            
            // Calculate position based on click
            qint64 value = 0;
            
            if (orientation() == Qt::Horizontal) {
                qreal clickPos = event->position().x();
                qreal widgetWidth = width();
                
                qint64 range = static_cast<qint64>(maximum()) - static_cast<qint64>(minimum());
                qint64 widgetSize = static_cast<qint64>(widgetWidth);
                
                if (widgetSize > 0) {
                    value = minimum() + (range * clickPos) / widgetSize;
                } else {
                    value = minimum();
                }
            } else {
                qreal clickPos = height() - event->position().y();
                qreal widgetHeight = height();
                
                qint64 range = static_cast<qint64>(maximum()) - static_cast<qint64>(minimum());
                qint64 widgetSize = static_cast<qint64>(widgetHeight);
                
                if (widgetSize > 0) {
                    value = minimum() + (range * clickPos) / widgetSize;
                } else {
                    value = minimum();
                }
            }
            
            // Clamp to range
            value = qBound(static_cast<qint64>(minimum()), value, static_cast<qint64>(maximum()));
            
            // Set the value and emit signal
            setValue(static_cast<int>(value));
            emit sliderPressed();
            emit sliderMoved(static_cast<int>(value));
        }
        
        QSlider::mousePressEvent(event);
    }
    
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_isPressed)
        {
            m_isPressed = false;
            emit sliderReleased();
        }
        
        QSlider::mouseReleaseEvent(event);
    }

private:
    bool m_isPressed;
};

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
    , m_currentTimeLabel(nullptr)
    , m_totalTimeLabel(nullptr)
    , m_positionSlider(nullptr)
    , m_formatComboBox(nullptr)
    , m_projectionComboBox(nullptr)
    , m_ipdSpinBox(nullptr)
    , m_zoomSlider(nullptr)
    , m_speedSlider(nullptr)
    , m_formatLabel(nullptr)
    , m_projectionLabel(nullptr)
    , m_ipdLabel(nullptr)
    , m_zoomLabel(nullptr)
    , m_zoomValueLabel(nullptr)
    , m_speedLabel(nullptr)
    , m_speedValueLabel(nullptr)
    , m_vrAvailable(false)
    , m_vrActive(false)
    , m_vrInitialized(false)
    , m_isPlaying(false)
    , m_videoLoaded(false)
    , m_isSliderBeingMoved(false)
    , m_duration(0)
    , m_position(0)
    , m_firstPlay(true)
    , m_currentPlaybackSpeed(1.0)
    , m_videoFormat(VRVideoRenderer::VideoFormat::Mono360)
    , m_timerManager(this)  // Initialize timer manager with this as parent
    , m_lastSeekAxis(0.0f, 0.0f)
    , m_controllerInputActive(false)
    , m_spacebarHeld(false)
    , m_grabButtonHeld(false)
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
    
    // Create timers using SafeTimerManager
    SafeTimer* frameTimer = m_timerManager.createTimer("frameTimer", "VRVideoPlayer");
    SafeTimer* positionTimer = m_timerManager.createTimer("positionTimer", "VRVideoPlayer");
    SafeTimer* focusTimer = m_timerManager.createTimer("focusTimer", "VRVideoPlayer");
    SafeTimer* controllerInputTimer = m_timerManager.createTimer("controllerInputTimer", "VRVideoPlayer");
    
    // Set up frame update timer for VR rendering
    // Use a faster interval to match VR display refresh rate
    if (frameTimer) {
        frameTimer->setInterval(11); // ~90 FPS to match most VR displays
    }
    
    // Set up position update timer
    if (positionTimer) {
        positionTimer->setInterval(100); // Update position 10 times per second
    }
    
    // Set up focus restoration timer (single shot timer to restore focus after SteamVR launch)
    if (focusTimer) {
        focusTimer->setSingleShot(true);
    }
    
    // Set up VR controller input timer (60Hz polling - separate from 90Hz render loop)
    if (controllerInputTimer) {
        controllerInputTimer->setInterval(16); // ~60 FPS for controller input
    }
    
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
                         "\u25cf Press Shift+Spacebar or Ctrl+Spacebar to play/pause the video\n\n"
                         "\u25cf Press Tab or End to reset playback speed to 1x\n\n"
                         "\u25cf Press W/S or Up/Down to zoom in/out | A/D or Left/Right to seek 10s\n\n"
                         "\u25cf Press Shift+W/S or Ctrl/Shift+Up/Down to increase/decrease playback speed\n\n"
                         "\u25cf Press Shift+A/D or Ctrl/Shift+Left/Right to seek backward/forward 60s\n\n"
                         "\u25cf Press Q/E or Page Down/Up to decrease/increase Windows system volume\n\n"
                         "\u25cf Use the controls below to adjust video format, zoom, speed, and IPD");
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
    m_formatComboBox->setCurrentIndex(2);  // Default to Stereo Side-by-Side
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
    
    // Playback speed slider
    m_speedLabel = new QLabel("Speed:", this);
    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setRange(25, 400); // 0.25x to 4.0x (as percentage)
    m_speedSlider->setValue(100); // Default 1.0x
    m_speedSlider->setSingleStep(5);
    m_speedSlider->setPageStep(25);
    m_speedSlider->setToolTip("Adjust video playback speed");
    m_speedSlider->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    m_speedValueLabel = new QLabel("100%", this);
    m_speedValueLabel->setFixedWidth(50);
    connect(m_speedSlider, &QSlider::valueChanged, this, &VRVideoPlayer::onSpeedSliderChanged);
    formatLayout->addWidget(m_speedLabel, 2, 0);
    formatLayout->addWidget(m_speedSlider, 2, 1);
    formatLayout->addWidget(m_speedValueLabel, 2, 2);
    
    mainLayout->addWidget(formatGroup);
    
    // Position slider for seeking - with time labels on left and right
    m_currentTimeLabel = new QLabel("00:00", this);
    m_currentTimeLabel->setAlignment(Qt::AlignCenter);
    m_currentTimeLabel->setMinimumWidth(50);
    m_currentTimeLabel->setStyleSheet("QLabel { font-size: 12px; color: #95a5a6; }");
    
    m_positionSlider = createClickableSlider();
    m_positionSlider->setOrientation(Qt::Horizontal);
    m_positionSlider->setEnabled(false);
    m_positionSlider->setFocusPolicy(Qt::NoFocus); // Don't grab focus for keyboard shortcuts
    m_positionSlider->setToolTip("Click or drag to seek through video playback");
    connect(m_positionSlider, &QSlider::sliderPressed, this, &VRVideoPlayer::onPositionSliderPressed);
    connect(m_positionSlider, &QSlider::sliderReleased, this, &VRVideoPlayer::onPositionSliderReleased);
    connect(m_positionSlider, &QSlider::valueChanged, this, &VRVideoPlayer::onPositionSliderMoved);
    
    m_totalTimeLabel = new QLabel("00:00", this);
    m_totalTimeLabel->setAlignment(Qt::AlignCenter);
    m_totalTimeLabel->setMinimumWidth(50);
    m_totalTimeLabel->setStyleSheet("QLabel { font-size: 12px; color: #95a5a6; }");
    
    QHBoxLayout* positionLayout = new QHBoxLayout();
    positionLayout->addWidget(m_currentTimeLabel);
    positionLayout->addWidget(m_positionSlider, 1);
    positionLayout->addWidget(m_totalTimeLabel);
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
    
    // Initialize controller input
    if (!m_vrManager->initializeControllerInput()) {
        qDebug() << "VRVideoPlayer: Failed to initialize controller input";
        // Continue anyway - controller input is not critical
    } else {
        qDebug() << "VRVideoPlayer: Controller input initialized successfully";
    }
    
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
    play(); // play video right away to prevent temp file cleanup from deleting the file
    pause(); // pause it right away. While pause, autocleanup will not delete it.

    // Reset first play flag for new video to enable auto-centering
    m_firstPlay = true;
    qDebug() << "VRVideoPlayer: Reset first play flag for new video - auto-centering enabled";

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
        
        // Auto-center VR headset on first play
        if (m_firstPlay && m_vrActive && m_renderThread) {
            qDebug() << "VRVideoPlayer: First play detected - auto-centering VR headset";
            m_renderThread->recenterView();
            m_firstPlay = false; // Mark as no longer first play
        }
        
        // Start frame timer if in VR mode
        if (m_vrActive) {
            SafeTimer* frameTimer = m_timerManager.getTimer("frameTimer");
            if (frameTimer) {
                frameTimer->start([this]() { updateVideoFrame(); });
            }
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
            SafeTimer* frameTimer = m_timerManager.getTimer("frameTimer");
            if (frameTimer) {
                frameTimer->stop();
            }
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
                SafeTimer* frameTimer = m_timerManager.getTimer("frameTimer");
                if (frameTimer) {
                    frameTimer->stop();
                }
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
    
    // Default to Stereo180_SBS for VR player to match UI default
    // Side-by-side stereo provides the best compatibility with most VR content
    qDebug() << "VRVideoPlayer: No specific format detected, defaulting to Stereo180_SBS";
    return VRVideoRenderer::VideoFormat::Stereo180_SBS;
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
    
    // Start timer to restore focus after SteamVR potentially steals it
    SafeTimer* focusTimer = m_timerManager.getTimer("focusTimer");
    if (focusTimer) {
        focusTimer->setInterval(2000);  // 2 seconds
        focusTimer->start([this]() { restoreFocusDelayed(); });
        qDebug() << "VRVideoPlayer: Started focus restoration timer (2 seconds)";
    }
    
    // Start VR controller input polling
    if (m_vrManager && m_vrManager->isControllerInputReady()) {
        SafeTimer* controllerInputTimer = m_timerManager.getTimer("controllerInputTimer");
        if (controllerInputTimer) {
            controllerInputTimer->start([this]() { processControllerInput(); });
            m_controllerInputActive = true;
            qDebug() << "VRVideoPlayer: Started VR controller input polling (60Hz)";
        }
    } else {
        qDebug() << "VRVideoPlayer: Controller input not available";
    }
    
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
    
    // Stop focus restoration timer if it's running
    SafeTimer* focusTimer = m_timerManager.getTimer("focusTimer");
    if (focusTimer && focusTimer->isActive()) {
        focusTimer->stop();
        qDebug() << "VRVideoPlayer: Stopped focus restoration timer";
    }
    
    // Stop VR controller input polling
    SafeTimer* controllerInputTimer = m_timerManager.getTimer("controllerInputTimer");
    if (controllerInputTimer && controllerInputTimer->isActive()) {
        controllerInputTimer->stop();
        m_controllerInputActive = false;
        qDebug() << "VRVideoPlayer: Stopped VR controller input polling";
    }
    
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
        SafeTimer* frameTimer = m_timerManager.getTimer("frameTimer");
        if (frameTimer) {
            frameTimer->start([this]() { updateVideoFrame(); });
        }
    }
    
    return true;
}

void VRVideoPlayer::stopVRRendering()
{
    qDebug() << "VRVideoPlayer: Stopping VR rendering";
    
    // Stop the frame timer
    SafeTimer* frameTimer = m_timerManager.getTimer("frameTimer");
    if (frameTimer) {
        frameTimer->stop();
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
    
    switch (event->key()) {
        case Qt::Key_Space:
            // Check for Shift+Spacebar first (play/pause)
            if (event->modifiers() & Qt::ShiftModifier) {
                qDebug() << "VRVideoPlayer: Shift+Spacebar pressed - toggling play/pause";
                onPlayPauseClicked();
                event->accept();
                return;
            }
            // Check for Ctrl+Spacebar (also play/pause)
            else if (event->modifiers() & Qt::ControlModifier) {
                qDebug() << "VRVideoPlayer: Ctrl+Spacebar pressed - toggling play/pause";
                onPlayPauseClicked();
                event->accept();
                return;
            }
            // In VR mode, spacebar starts continuous recenter
            else if (m_vrActive && m_renderThread) {
                if (!m_spacebarHeld) {
                    qDebug() << "VRVideoPlayer: Spacebar pressed - starting continuous recenter";
                    m_spacebarHeld = true;
                }
                event->accept();
                return;
            } else {
                // When not in VR, spacebar controls play/pause
                qDebug() << "VRVideoPlayer: Spacebar pressed - toggling play/pause";
                onPlayPauseClicked();
                event->accept();
                return;
            }
            break;
            
        case Qt::Key_Tab:
            // Tab key for reset playback speed
            qDebug() << "VRVideoPlayer: Tab key pressed - resetting playback speed";
            resetPlaybackSpeed();
            event->accept();
            break;
            
        case Qt::Key_End:
            // End key mirrors Tab (reset playback speed)
            qDebug() << "VRVideoPlayer: End key pressed - resetting playback speed";
            resetPlaybackSpeed();
            event->accept();
            break;
            
        case Qt::Key_W:
            // Check for Shift+W first (increase playback speed)
            if (event->modifiers() & Qt::ShiftModifier) {
                qDebug() << "VRVideoPlayer: Shift+W pressed - increasing playback speed";
                increasePlaybackSpeed();
            }
            // W key for zoom in (only when Shift is NOT pressed)
            else if (m_renderThread && m_vrActive) {
                float currentScale = m_renderThread->getVideoScale();
                float newScale = qBound(0.1f, currentScale + 0.1f, 5.0f);
                m_renderThread->setVideoScale(newScale);
                
                // Update zoom slider UI
                if (m_zoomSlider) {
                    m_zoomSlider->blockSignals(true);
                    m_zoomSlider->setValue(static_cast<int>(newScale * 100));
                    m_zoomSlider->blockSignals(false);
                    if (m_zoomValueLabel) {
                        m_zoomValueLabel->setText(QString("%1%").arg(static_cast<int>(newScale * 100)));
                    }
                }
                
                qDebug() << "VRVideoPlayer: W key pressed - zoom in to" << newScale;
            }
            event->accept();
            break;
            
        case Qt::Key_S:
            // Check for Shift+S first (decrease playback speed)
            if (event->modifiers() & Qt::ShiftModifier) {
                qDebug() << "VRVideoPlayer: Shift+S pressed - decreasing playback speed";
                decreasePlaybackSpeed();
            }
            // S key for zoom out (only when Shift is NOT pressed)
            else if (m_renderThread && m_vrActive) {
                float currentScale = m_renderThread->getVideoScale();
                float newScale = qBound(0.1f, currentScale - 0.1f, 5.0f);
                m_renderThread->setVideoScale(newScale);
                
                // Update zoom slider UI
                if (m_zoomSlider) {
                    m_zoomSlider->blockSignals(true);
                    m_zoomSlider->setValue(static_cast<int>(newScale * 100));
                    m_zoomSlider->blockSignals(false);
                    if (m_zoomValueLabel) {
                        m_zoomValueLabel->setText(QString("%1%").arg(static_cast<int>(newScale * 100)));
                    }
                }
                
                qDebug() << "VRVideoPlayer: S key pressed - zoom out to" << newScale;
            }
            event->accept();
            break;
            
        case Qt::Key_D:
            // Check for Shift+D first (seek forward 60 seconds)
            if (event->modifiers() & Qt::ShiftModifier) {
                if (m_videoLoaded && m_vlcPlayer) {
                    qDebug() << "VRVideoPlayer: Shift+D pressed - seeking forward 60 seconds";
                    m_vlcPlayer->seekRelative(60000); // 60 seconds in milliseconds
                }
            }
            // D key for seek forward 10 seconds (only when Shift is NOT pressed)
            else if (m_videoLoaded && m_vlcPlayer) {
                qDebug() << "VRVideoPlayer: D key pressed - seeking forward 10 seconds";
                m_vlcPlayer->seekRelative(10000); // 10 seconds in milliseconds
            }
            event->accept();
            break;
            
        case Qt::Key_A:
            // Check for Shift+A first (seek backward 60 seconds)
            if (event->modifiers() & Qt::ShiftModifier) {
                if (m_videoLoaded && m_vlcPlayer) {
                    qDebug() << "VRVideoPlayer: Shift+A pressed - seeking backward 60 seconds";
                    m_vlcPlayer->seekRelative(-60000); // -60 seconds in milliseconds
                }
            }
            // A key for seek backward 10 seconds (only when Shift is NOT pressed)
            else if (m_videoLoaded && m_vlcPlayer) {
                qDebug() << "VRVideoPlayer: A key pressed - seeking backward 10 seconds";
                m_vlcPlayer->seekRelative(-10000); // -10 seconds in milliseconds
            }
            event->accept();
            break;
            
        case Qt::Key_E:
            // E key for Windows volume up
            qDebug() << "VRVideoPlayer: E key pressed - increasing Windows system volume";
#ifdef Q_OS_WIN
            increaseWindowsVolume();
#else
            qDebug() << "VRVideoPlayer: Windows volume control only available on Windows";
#endif
            event->accept();
            break;
            
        case Qt::Key_Q:
            // Q key for Windows volume down
            qDebug() << "VRVideoPlayer: Q key pressed - decreasing Windows system volume";
#ifdef Q_OS_WIN
            decreaseWindowsVolume();
#else
            qDebug() << "VRVideoPlayer: Windows volume control only available on Windows";
#endif
            event->accept();
            break;
            
        case Qt::Key_Up:
            // Check for Ctrl+Up or Shift+Up first (increase playback speed - mirrors Shift+W)
            if ((event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::ShiftModifier)) {
                qDebug() << "VRVideoPlayer: Ctrl+Up or Shift+Up pressed - increasing playback speed";
                increasePlaybackSpeed();
            }
            // Up key for zoom in (mirrors W key)
            else if (m_renderThread && m_vrActive) {
                float currentScale = m_renderThread->getVideoScale();
                float newScale = qBound(0.1f, currentScale + 0.1f, 5.0f);
                m_renderThread->setVideoScale(newScale);
                
                // Update zoom slider UI
                if (m_zoomSlider) {
                    m_zoomSlider->blockSignals(true);
                    m_zoomSlider->setValue(static_cast<int>(newScale * 100));
                    m_zoomSlider->blockSignals(false);
                    if (m_zoomValueLabel) {
                        m_zoomValueLabel->setText(QString("%1%").arg(static_cast<int>(newScale * 100)));
                    }
                }
                
                qDebug() << "VRVideoPlayer: Up key pressed - zoom in to" << newScale;
            }
            event->accept();
            break;
            
        case Qt::Key_Down:
            // Check for Ctrl+Down or Shift+Down first (decrease playback speed - mirrors Shift+S)
            if ((event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::ShiftModifier)) {
                qDebug() << "VRVideoPlayer: Ctrl+Down or Shift+Down pressed - decreasing playback speed";
                decreasePlaybackSpeed();
            }
            // Down key for zoom out (mirrors S key)
            else if (m_renderThread && m_vrActive) {
                float currentScale = m_renderThread->getVideoScale();
                float newScale = qBound(0.1f, currentScale - 0.1f, 5.0f);
                m_renderThread->setVideoScale(newScale);
                
                // Update zoom slider UI
                if (m_zoomSlider) {
                    m_zoomSlider->blockSignals(true);
                    m_zoomSlider->setValue(static_cast<int>(newScale * 100));
                    m_zoomSlider->blockSignals(false);
                    if (m_zoomValueLabel) {
                        m_zoomValueLabel->setText(QString("%1%").arg(static_cast<int>(newScale * 100)));
                    }
                }
                
                qDebug() << "VRVideoPlayer: Down key pressed - zoom out to" << newScale;
            }
            event->accept();
            break;
            
        case Qt::Key_Left:
            // Check for Ctrl+Left or Shift+Left first (seek backward 60 seconds - mirrors Shift+A)
            if ((event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::ShiftModifier)) {
                if (m_videoLoaded && m_vlcPlayer) {
                    qDebug() << "VRVideoPlayer: Ctrl+Left or Shift+Left pressed - seeking backward 60 seconds";
                    m_vlcPlayer->seekRelative(-60000); // -60 seconds in milliseconds
                }
            }
            // Left key for seek backward 10 seconds (mirrors A key)
            else if (m_videoLoaded && m_vlcPlayer) {
                qDebug() << "VRVideoPlayer: Left key pressed - seeking backward 10 seconds";
                m_vlcPlayer->seekRelative(-10000); // -10 seconds in milliseconds
            }
            event->accept();
            break;
            
        case Qt::Key_Right:
            // Check for Ctrl+Right or Shift+Right first (seek forward 60 seconds - mirrors Shift+D)
            if ((event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::ShiftModifier)) {
                if (m_videoLoaded && m_vlcPlayer) {
                    qDebug() << "VRVideoPlayer: Ctrl+Right or Shift+Right pressed - seeking forward 60 seconds";
                    m_vlcPlayer->seekRelative(60000); // 60 seconds in milliseconds
                }
            }
            // Right key for seek forward 10 seconds (mirrors D key)
            else if (m_videoLoaded && m_vlcPlayer) {
                qDebug() << "VRVideoPlayer: Right key pressed - seeking forward 10 seconds";
                m_vlcPlayer->seekRelative(10000); // 10 seconds in milliseconds
            }
            event->accept();
            break;
            
        case Qt::Key_PageUp:
            // Page Up key mirrors E key (Windows volume up)
            qDebug() << "VRVideoPlayer: Page Up key pressed - increasing Windows system volume";
#ifdef Q_OS_WIN
            increaseWindowsVolume();
#else
            qDebug() << "VRVideoPlayer: Windows volume control only available on Windows";
#endif
            event->accept();
            break;
            
        case Qt::Key_PageDown:
            // Page Down key mirrors Q key (Windows volume down)
            qDebug() << "VRVideoPlayer: Page Down key pressed - decreasing Windows system volume";
#ifdef Q_OS_WIN
            decreaseWindowsVolume();
#else
            qDebug() << "VRVideoPlayer: Windows volume control only available on Windows";
#endif
            event->accept();
            break;
            
        case Qt::Key_Escape:
            // Escape key to close the VR player
            qDebug() << "VRVideoPlayer: Escape key pressed - closing VR player";
            onCloseClicked();
            event->accept();
            break;
            
        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void VRVideoPlayer::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Space:
            // Stop continuous recenter when spacebar is released
            if (m_spacebarHeld) {
                qDebug() << "VRVideoPlayer: Spacebar released - stopping continuous recenter";
                m_spacebarHeld = false;
            }
            event->accept();
            break;
            
        default:
            QWidget::keyReleaseEvent(event);
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
        
        // Update individual time labels while dragging
        if (m_currentTimeLabel) {
            m_currentTimeLabel->setText(formatTime(newPosition));
        }
        if (m_totalTimeLabel) {
            m_totalTimeLabel->setText(formatTime(m_duration));
        }
        
        // Update old combined label for compatibility (if it exists)
        if (m_positionLabel) {
            m_positionLabel->setText(QString("%1 / %2")
                .arg(formatTime(newPosition))
                .arg(formatTime(m_duration)));
        }
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

void VRVideoPlayer::setPlaybackSpeed(qreal speed)
{
    qDebug() << "VRVideoPlayer: Setting playback speed to" << speed;
    
    // Clamp speed to valid range
    speed = qBound(0.25, speed, 4.0);
    
    m_currentPlaybackSpeed = speed;
    
    // Set speed in VLC player
    if (m_vlcPlayer) {
        m_vlcPlayer->setPlaybackRate(static_cast<float>(speed));
    }
    
    // Update slider if not already at this value
    if (m_speedSlider) {
        int sliderValue = static_cast<int>(speed * 100);
        if (m_speedSlider->value() != sliderValue) {
            m_speedSlider->blockSignals(true);
            m_speedSlider->setValue(sliderValue);
            m_speedSlider->blockSignals(false);
        }
    }
    
    // Update value label
    if (m_speedValueLabel) {
        m_speedValueLabel->setText(QString("%1%").arg(static_cast<int>(speed * 100)));
    }
}

void VRVideoPlayer::increasePlaybackSpeed()
{
    qreal newSpeed = m_currentPlaybackSpeed + 0.1;
    setPlaybackSpeed(newSpeed);
    qDebug() << "VRVideoPlayer: Increased playback speed to" << newSpeed;
}

void VRVideoPlayer::decreasePlaybackSpeed()
{
    qreal newSpeed = m_currentPlaybackSpeed - 0.1;
    setPlaybackSpeed(newSpeed);
    qDebug() << "VRVideoPlayer: Decreased playback speed to" << newSpeed;
}

void VRVideoPlayer::resetPlaybackSpeed()
{
    setPlaybackSpeed(1.0);
    qDebug() << "VRVideoPlayer: Reset playback speed to 1.0";
}

void VRVideoPlayer::onSpeedSliderChanged(int value)
{
    qreal speed = value / 100.0;
    setPlaybackSpeed(speed);
    
    // Update the value label
    if (m_speedValueLabel) {
        m_speedValueLabel->setText(QString("%1%").arg(value));
    }
    
    qDebug() << "VRVideoPlayer: Playback speed changed to" << speed;
}

void VRVideoPlayer::updatePlaybackPosition()
{
    if (!m_videoLoaded) {
        // Update individual time labels when no video is loaded
        if (m_currentTimeLabel) m_currentTimeLabel->setText("00:00");
        if (m_totalTimeLabel) m_totalTimeLabel->setText("00:00");
        if (m_positionLabel) m_positionLabel->setText("00:00 / 00:00");  // Keep old label for compatibility
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
    
    // Update individual time labels
    if (m_currentTimeLabel) {
        m_currentTimeLabel->setText(formatTime(m_position));
    }
    if (m_totalTimeLabel) {
        m_totalTimeLabel->setText(formatTime(m_duration));
    }
    
    // Update old combined label for compatibility (if it exists)
    if (m_positionLabel) {
        m_positionLabel->setText(QString("%1 / %2")
            .arg(formatTime(m_position))
            .arg(formatTime(m_duration)));
    }
    
    // Update position slider if not being moved by user
    if (m_positionSlider && !m_isSliderBeingMoved && m_duration > 0) {
        int sliderPosition = (int)((m_position * 1000) / m_duration);
        m_positionSlider->setValue(sliderPosition);
    }
}

void VRVideoPlayer::updateUIState()
{
    qDebug() << "VRVideoPlayer: Updating UI state - hasVideo:" << m_videoLoaded << "isPlaying:" << m_isPlaying;
    
    // Enable/disable buttons and controls based on state
    bool hasVideo = m_videoLoaded;
    m_playPauseButton->setEnabled(hasVideo);
    m_stopButton->setEnabled(hasVideo);  // Enable stop button when video is loaded
    
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

ClickableSlider* VRVideoPlayer::createClickableSlider()
{
    qDebug() << "VRVideoPlayer: Creating custom clickable slider";
    return new ClickableSlider(Qt::Horizontal, this);
}

void VRVideoPlayer::restoreFocusDelayed()
{
    qDebug() << "VRVideoPlayer: Restoring focus after SteamVR launch";
    
    // Restore focus to this window
    activateWindow();
    raise();
    setFocus(Qt::OtherFocusReason);
    
    qDebug() << "VRVideoPlayer: Focus restoration complete";
}

void VRVideoPlayer::processControllerInput()
{
    if (!m_controllerInputActive || !m_vrManager || !m_vrManager->isControllerInputReady()) {
        return;
    }
    
    // Poll controller input state
    VROpenVRManager::VRControllerState state = m_vrManager->pollControllerInput();
    
    // Process continuous recenter (trigger button held)
    if (state.recenterHeld) {
        if (!m_grabButtonHeld) {
            qDebug() << "VRVideoPlayer: Controller trigger button pressed - starting continuous recenter";
            m_grabButtonHeld = true;
        }
    } else {
        if (m_grabButtonHeld) {
            qDebug() << "VRVideoPlayer: Controller trigger button released - stopping continuous recenter";
            m_grabButtonHeld = false;
        }
    }
    
    // Perform recentering if either spacebar or menu button is held
    if ((m_spacebarHeld || m_grabButtonHeld) && m_renderThread && m_vrActive) {
        m_renderThread->recenterView();
    }
    
    if (state.playPausePressed) {
        qDebug() << "VRVideoPlayer: Controller menu button pressed - toggling play/pause";
        onPlayPauseClicked();
    }
    
    // Process new speed control inputs
    if (state.increaseSpeedPressed) {
        qDebug() << "VRVideoPlayer: Controller Grip+Menu pressed - increasing playback speed";
        increasePlaybackSpeed();
    }
    
    if (state.decreaseSpeedPressed) {
        qDebug() << "VRVideoPlayer: Controller Grip+Trigger pressed - decreasing playback speed";
        decreasePlaybackSpeed();
    }
    
    // Process analog inputs (touchpad/joystick)
    const float deadzone = 0.3f; // Ignore small movements
    const float seekThreshold = 0.7f; // Threshold for seeking
    const float zoomThreshold = 0.5f; // Threshold for zoom
    const float volumeThreshold = 0.5f; // Threshold for volume
    
    if (state.seekAxis.length() > deadzone) {
        float horizontalAxis = state.seekAxis.x(); // Left/right
        float verticalAxis = state.seekAxis.y();   // Up/down
        
        // Determine action based on grip modifier
        if (state.gripPressed) {
            // Grip + axis combinations
            
            // Horizontal: Seek 60 seconds
            if (qAbs(horizontalAxis) > seekThreshold && m_videoLoaded && m_vlcPlayer) {
                // Only trigger on edge (not continuous)
                if ((horizontalAxis > seekThreshold && m_lastSeekAxis.x() <= seekThreshold) ||
                    (horizontalAxis < -seekThreshold && m_lastSeekAxis.x() >= -seekThreshold)) {
                    
                    int seekMs = (horizontalAxis > 0) ? 60000 : -60000;
                    qDebug() << "VRVideoPlayer: Controller grip+horizontal - seeking" << (seekMs/1000) << "seconds";
                    m_vlcPlayer->seekRelative(seekMs);
                }
            }
            
            // Vertical: Volume control
            if (qAbs(verticalAxis) > volumeThreshold) {
                // Only trigger on edge (not continuous)
                if ((verticalAxis > volumeThreshold && m_lastSeekAxis.y() <= volumeThreshold) ||
                    (verticalAxis < -volumeThreshold && m_lastSeekAxis.y() >= -volumeThreshold)) {
                    
#ifdef Q_OS_WIN
                    if (verticalAxis > 0) {
                        qDebug() << "VRVideoPlayer: Controller grip+up - increasing Windows volume";
                        increaseWindowsVolume();
                    } else {
                        qDebug() << "VRVideoPlayer: Controller grip+down - decreasing Windows volume";
                        decreaseWindowsVolume();
                    }
#else
                    qDebug() << "VRVideoPlayer: Volume control only available on Windows";
#endif
                }
            }
        } else {
            // Normal axis (no grip) combinations
            
            // Horizontal: Seek 10 seconds
            if (qAbs(horizontalAxis) > seekThreshold && m_videoLoaded && m_vlcPlayer) {
                // Only trigger on edge (not continuous)
                if ((horizontalAxis > seekThreshold && m_lastSeekAxis.x() <= seekThreshold) ||
                    (horizontalAxis < -seekThreshold && m_lastSeekAxis.x() >= -seekThreshold)) {
                    
                    int seekMs = (horizontalAxis > 0) ? 10000 : -10000;
                    qDebug() << "VRVideoPlayer: Controller horizontal - seeking" << (seekMs/1000) << "seconds";
                    m_vlcPlayer->seekRelative(seekMs);
                }
            }
            
            // Vertical: Zoom in/out
            if (qAbs(verticalAxis) > zoomThreshold && m_renderThread && m_vrActive) {
                // Only trigger on edge (not continuous)
                if ((verticalAxis > zoomThreshold && m_lastSeekAxis.y() <= zoomThreshold) ||
                    (verticalAxis < -zoomThreshold && m_lastSeekAxis.y() >= -zoomThreshold)) {
                    
                    float currentScale = m_renderThread->getVideoScale();
                    float zoomDelta = (verticalAxis > 0) ? 0.1f : -0.1f;
                    float newScale = qBound(0.1f, currentScale + zoomDelta, 5.0f);
                    m_renderThread->setVideoScale(newScale);
                    
                    // Update zoom slider UI
                    if (m_zoomSlider) {
                        m_zoomSlider->blockSignals(true);
                        m_zoomSlider->setValue(static_cast<int>(newScale * 100));
                        m_zoomSlider->blockSignals(false);
                        if (m_zoomValueLabel) {
                            m_zoomValueLabel->setText(QString("%1%").arg(static_cast<int>(newScale * 100)));
                        }
                    }
                    
                    qDebug() << "VRVideoPlayer: Controller vertical - zoom to" << newScale;
                }
            }
        }
    }
    
    // Store current axis values for edge detection
    m_lastSeekAxis = state.seekAxis;
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
