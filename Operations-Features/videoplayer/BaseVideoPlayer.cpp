#include "BaseVideoPlayer.h"
#include "inputvalidation.h"
#include <QGuiApplication>
#include <QDebug>
#include <QFileInfo>
#include <QStyle>
#include <QTime>
#include <QCloseEvent>
#include <QShowEvent>
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QFocusEvent>
#include <QDateTime>
#include <climits>

// Initialize static members for window state persistence
QScreen* BaseVideoPlayer::s_lastUsedScreen = nullptr;
QRect BaseVideoPlayer::s_lastWindowGeometry = QRect();
bool BaseVideoPlayer::s_wasFullScreen = false;
bool BaseVideoPlayer::s_wasMaximized = false;
bool BaseVideoPlayer::s_wasMinimized = false;
int BaseVideoPlayer::s_lastVolume = 70;  // Default volume
qreal BaseVideoPlayer::s_lastPlaybackSpeed = 1.0;  // Default playback speed
bool BaseVideoPlayer::s_hasStoredSettings = false;

// Custom clickable slider class for seeking in video
class BaseVideoPlayer::ClickableSlider : public QSlider
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
            
            value = qBound(static_cast<qint64>(minimum()), value, static_cast<qint64>(maximum()));
            
            qDebug() << "BaseVideoPlayer::ClickableSlider: Calculated value:" << value;
            
            setValue(static_cast<int>(value));
            emit sliderMoved(static_cast<int>(value));
            emit sliderPressed();
            
            QSlider::mousePressEvent(event);
        }
        else {
            QSlider::mousePressEvent(event);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_isPressed) {
            m_isPressed = false;
            emit sliderReleased();
        }
        QSlider::mouseReleaseEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        if (m_isPressed) {
            m_isPressed = false;
            emit sliderReleased();
        }
        QSlider::focusOutEvent(event);
    }

private:
    bool m_isPressed;
};

BaseVideoPlayer::BaseVideoPlayer(QWidget *parent, int initialVolume)
    : QWidget(parent, Qt::Window)  // Always create as a window
    , m_videoWidget(nullptr)
    , m_playButton(nullptr)
    , m_stopButton(nullptr)
    , m_fullScreenButton(nullptr)
    , m_muteButton(nullptr)
    , m_positionSlider(nullptr)
    , m_volumeSlider(nullptr)
    , m_positionLabel(nullptr)
    , m_durationLabel(nullptr)
    , m_volumeLabel(nullptr)
    , m_controlsWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_controlLayout(nullptr)
    , m_sliderLayout(nullptr)
    , m_isSliderBeingMoved(false)
    , m_isFullScreen(false)
    , m_isMuted(false)
    , m_volumeBeforeMute(70)
    , m_isClosing(false)
    , m_playbackStartedEmitted(false)
    , m_cursorTimer(nullptr)
    , m_mouseCheckTimer(nullptr)
    , m_lastMousePos(QPoint(-1, -1))
    , m_targetScreen(nullptr)
{
    qDebug() << "BaseVideoPlayer: Constructor called";
    
    // Set window properties
    setWindowTitle(tr("Video Player"));
    setWindowFlags(windowFlags() | Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    // Note: WA_DeleteOnClose is NOT set here because VP_Shows_Videoplayer is managed by unique_ptr
    resize(800, 600);
    
    // Center window on screen
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        move(screenGeometry.center() - rect().center());
    }
    
    if (!s_hasStoredSettings) {
        s_lastVolume = initialVolume;
        qDebug() << "BaseVideoPlayer: Setting initial volume to" << initialVolume << "%";
    }

    // Initialize the player
    initializePlayer();
}

BaseVideoPlayer::~BaseVideoPlayer()
{
    qDebug() << "BaseVideoPlayer: Destructor called";
    
#ifdef Q_OS_WIN
    // If Windows is shutting down, ensure we stop the player immediately
    if (m_windowsShutdownInProgress && m_mediaPlayer) {
        qDebug() << "BaseVideoPlayer: Destructor during Windows shutdown - emergency stop";
        m_mediaPlayer->stop();
        QApplication::processEvents(); // Ensure stop completes
    }
#endif
    
    // Stop timers
    if (m_cursorTimer) {
        m_cursorTimer->stop();
        delete m_cursorTimer;
    }
    
    if (m_mouseCheckTimer) {
        m_mouseCheckTimer->stop();
        delete m_mouseCheckTimer;
    }
    
    // Stop media player
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
}

void BaseVideoPlayer::initializePlayer()
{
    qDebug() << "BaseVideoPlayer: Initializing player";

    // Enable mouse tracking for auto-hide cursor functionality
    setMouseTracking(true);

    // Create VLC player instance
    m_mediaPlayer = std::make_unique<VP_VLCPlayer>(this);

    if (!m_mediaPlayer->initialize()) {
        qDebug() << "BaseVideoPlayer: Failed to initialize VLC player";
        emit errorOccurred(tr("Failed to initialize video player"));
        return;
    }

    // Setup UI
    setupUI();

    // Connect signals
    connectSignals();

    // Initialize cursor timers for fullscreen auto-hide
    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    connect(m_cursorTimer, &QTimer::timeout, this, &BaseVideoPlayer::hideCursor);

    m_mouseCheckTimer = new QTimer(this);
    m_mouseCheckTimer->setInterval(100);
    connect(m_mouseCheckTimer, &QTimer::timeout, this, &BaseVideoPlayer::checkMouseMovement);

    // Initialize volume
    // Always use s_lastVolume which contains the actual volume value
    qDebug() << "BaseVideoPlayer: Setting initial volume to" << s_lastVolume;
    if (m_mediaPlayer) {
        m_mediaPlayer->setVolume(s_lastVolume);
    }
    if (m_volumeSlider) {
        m_volumeSlider->setValue(s_lastVolume);
    }
    if (m_volumeLabel) {
        m_volumeLabel->setText(tr("Vol (%1%):").arg(s_lastVolume));
    }

    // Initialize m_volumeBeforeMute with the actual volume
    m_volumeBeforeMute = s_lastVolume;
    
    // Initialize from previous settings if available
    initializeFromPreviousSettings();
    
    qDebug() << "BaseVideoPlayer: Initialization complete";
}

void BaseVideoPlayer::initializeFromPreviousSettings()
{
    // This can be overridden by child classes
    // Base implementation restores window geometry, volume, and monitor
    
    qDebug() << "BaseVideoPlayer: Initializing from previous settings";
    qDebug() << "BaseVideoPlayer: Has stored settings:" << s_hasStoredSettings;
    qDebug() << "BaseVideoPlayer: Was fullscreen:" << s_wasFullScreen 
             << "Was maximized:" << s_wasMaximized 
             << "Was minimized:" << s_wasMinimized;
    
    // Always restore volume and playback speed (even for first play)
    setVolume(s_lastVolume);
    setPlaybackSpeed(s_lastPlaybackSpeed);
    
    // Always try to restore to the last used monitor if available
    if (s_lastUsedScreen && QGuiApplication::screens().contains(s_lastUsedScreen)) {
        qDebug() << "BaseVideoPlayer: Restoring to last used screen";
        QRect screenGeometry = s_lastUsedScreen->availableGeometry();
        
        // Move window to the target screen
        if (!s_lastWindowGeometry.isEmpty()) {
            // Use the saved geometry but ensure it's on the right screen
            QRect targetGeometry = s_lastWindowGeometry;
            
            // Adjust position to be on the correct screen if needed
            if (!screenGeometry.contains(targetGeometry.center())) {
                // Window was on a different screen, center it on the target screen
                targetGeometry.moveCenter(screenGeometry.center());
            }
            
            setGeometry(targetGeometry);
        } else {
            // No saved geometry, center on the target screen
            move(screenGeometry.center() - rect().center());
        }
    } else if (!s_lastWindowGeometry.isEmpty()) {
        // Screen not available but we have geometry, just restore the geometry
        qDebug() << "BaseVideoPlayer: Last screen not available, using saved geometry";
        setGeometry(s_lastWindowGeometry);
    }
    
    // Note: Window state (minimized, maximized, fullscreen) will be handled by child classes
    // as they may have different requirements (e.g., autoplay vs manual play)
}

void BaseVideoPlayer::setupUI()
{
    qDebug() << "BaseVideoPlayer: Setting up UI";
    
    // Create video widget - regular QWidget for VLC rendering
    m_videoWidget = new QWidget(this);
    m_videoWidget->setMinimumSize(400, 300);
    
    // Ensure video widget has a proper background
    m_videoWidget->setStyleSheet("background-color: black;");
    m_videoWidget->setAutoFillBackground(true);
    
    // Set VLC video output widget
    m_mediaPlayer->setVideoWidget(m_videoWidget);
    
    // Ensure video widget is visible
    m_videoWidget->show();
    
    // Install event filter on video widget for double-click fullscreen
    m_videoWidget->installEventFilter(this);
    
    // Enable mouse tracking on video widget too
    m_videoWidget->setMouseTracking(true);
    
    // Set focus policy to accept keyboard input
    setFocusPolicy(Qt::StrongFocus);
    m_videoWidget->setFocusPolicy(Qt::StrongFocus);
    
    // Create controls
    createControls();
    
    // Create layouts
    createLayouts();
}

BaseVideoPlayer::ClickableSlider* BaseVideoPlayer::createClickableSlider()
{
    return new ClickableSlider(Qt::Horizontal, this);
}

void BaseVideoPlayer::createControls()
{
    qDebug() << "BaseVideoPlayer: Creating controls";
    
    // Play/Pause button
    m_playButton = new QPushButton(this);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setToolTip(tr("Play"));
    m_playButton->setFocusPolicy(Qt::NoFocus);
    
    // Stop button
    m_stopButton = new QPushButton(this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setToolTip(tr("Stop"));
    m_stopButton->setFocusPolicy(Qt::NoFocus);
    
    // Fullscreen button
    m_fullScreenButton = new QPushButton(this);
    m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_fullScreenButton->setToolTip(tr("Full Screen (F11)"));
    m_fullScreenButton->setFocusPolicy(Qt::NoFocus);
    
    // Mute button
    m_muteButton = new QPushButton(this);
    m_muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
    m_muteButton->setToolTip(tr("Mute (M)"));
    m_muteButton->setFocusPolicy(Qt::NoFocus);
    
    // Position slider - use custom clickable slider
    m_positionSlider = createClickableSlider();
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setToolTip(tr("Click to seek\nLeft/Right: Seek 10s"));
    m_positionSlider->setFocusPolicy(Qt::ClickFocus);
    
    // Volume slider - use custom clickable slider, extended range to 200%
    m_volumeSlider = createClickableSlider();
    m_volumeSlider->setRange(0, 200);
    m_volumeSlider->setValue(s_lastVolume);
    m_volumeSlider->setMaximumWidth(100);
    m_volumeSlider->setToolTip(tr("Volume (up to 200%)\nUp/Down: Adjust volume\nMouse Wheel: Adjust volume\nCtrl+Mouse Wheel: Adjust playback speed"));
    m_volumeSlider->setFocusPolicy(Qt::ClickFocus);
    
    // Speed spin box
    m_speedSpinBox = new QDoubleSpinBox(this);
    m_speedSpinBox->setRange(0.1, 5.0);
    m_speedSpinBox->setSingleStep(0.1);
    m_speedSpinBox->setValue(s_lastPlaybackSpeed);  // Use saved speed
    m_speedSpinBox->setSuffix("x");
    m_speedSpinBox->setDecimals(1);
    m_speedSpinBox->setMaximumWidth(80);
    m_speedSpinBox->setToolTip(tr("Playback Speed\nCtrl+Up / Ctrl+MouseWheel Up: Increase speed by 0.1\nCtrl+Down / Ctrl+MouseWheel Down: Decrease speed by 0.1"));
    m_speedSpinBox->setFocusPolicy(Qt::NoFocus);
    
    // Labels
    m_positionLabel = new QLabel("00:00", this);
    m_positionLabel->setMinimumWidth(50);
    
    m_durationLabel = new QLabel("00:00", this);
    m_durationLabel->setMinimumWidth(50);
    
    m_volumeLabel = new QLabel(tr("Vol (%1%):").arg(s_lastVolume), this);
    
    m_speedLabel = new QLabel(tr("Speed:"), this);
    
    // Set initial volume on VLC player
    m_mediaPlayer->setVolume(s_lastVolume);
}

void BaseVideoPlayer::createLayouts()
{
    qDebug() << "BaseVideoPlayer: Creating layouts";
    
    // Create a widget to hold all controls (for fullscreen mode)
    m_controlsWidget = new QWidget(this);
    
    // Enable mouse tracking on controls widget to detect mouse movement
    m_controlsWidget->setMouseTracking(true);
    
    // Install event filter on controls widget for mouse tracking
    m_controlsWidget->installEventFilter(this);
    
    // Control layout (buttons)
    m_controlLayout = new QHBoxLayout();
    m_controlLayout->addWidget(m_playButton);
    m_controlLayout->addWidget(m_stopButton);
    m_controlLayout->addWidget(m_fullScreenButton);
    m_controlLayout->addStretch();
    
    // Slider layout (position, volume, and speed)
    m_sliderLayout = new QHBoxLayout();
    m_sliderLayout->addWidget(m_positionLabel);
    m_sliderLayout->addWidget(m_positionSlider, 1);
    m_sliderLayout->addWidget(m_durationLabel);
    m_sliderLayout->addSpacing(20);
    m_sliderLayout->addWidget(m_muteButton);
    m_sliderLayout->addWidget(m_volumeLabel);
    m_sliderLayout->addWidget(m_volumeSlider);
    m_sliderLayout->addSpacing(20);
    m_sliderLayout->addWidget(m_speedLabel);
    m_sliderLayout->addWidget(m_speedSpinBox);
    
    // Controls widget layout
    QVBoxLayout* controlsLayout = new QVBoxLayout(m_controlsWidget);
    controlsLayout->addLayout(m_controlLayout);
    controlsLayout->addLayout(m_sliderLayout);
    controlsLayout->setContentsMargins(5, 5, 5, 5);
    
    // Main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->addWidget(m_videoWidget, 1);
    m_mainLayout->addWidget(m_controlsWidget);
    
    // Store normal margins for restoration later
    m_normalMargins = m_mainLayout->contentsMargins();
    
    setLayout(m_mainLayout);
}

void BaseVideoPlayer::connectSignals()
{
    qDebug() << "BaseVideoPlayer: Connecting signals";
    
    // Button signals - check pointers before connecting
    if (m_playButton) {
        connect(m_playButton, &QPushButton::clicked,
                this, &BaseVideoPlayer::on_playButton_clicked);
    }
    
    if (m_stopButton) {
        connect(m_stopButton, &QPushButton::clicked,
                this, &BaseVideoPlayer::close);
    }
    
    if (m_fullScreenButton) {
        connect(m_fullScreenButton, &QPushButton::clicked,
                this, &BaseVideoPlayer::on_fullScreenButton_clicked);
    }
    
    if (m_muteButton) {
        connect(m_muteButton, &QPushButton::clicked,
                this, &BaseVideoPlayer::on_muteButton_clicked);
    }
    
    // Slider signals - check pointers before connecting
    if (m_positionSlider) {
        connect(m_positionSlider, &QSlider::sliderMoved,
                this, &BaseVideoPlayer::on_positionSlider_sliderMoved);
        
        connect(m_positionSlider, &QSlider::sliderPressed,
                this, &BaseVideoPlayer::on_positionSlider_sliderPressed);
        
        connect(m_positionSlider, &QSlider::sliderReleased,
                this, &BaseVideoPlayer::on_positionSlider_sliderReleased);
        
        // Also connect valueChanged to catch direct clicks
        connect(m_positionSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_isSliderBeingMoved && 
            m_mediaPlayer && 
            m_mediaPlayer->hasMedia() && 
            m_mediaPlayer->duration() > 0) {
            
            qint64 currentPos = m_mediaPlayer->position();
            qint64 valueDiff = qAbs(static_cast<qint64>(value) - currentPos);
            
            if (valueDiff > 1000) {
                qDebug() << "BaseVideoPlayer: Slider value changed significantly - seeking to" << value;
                setPosition(value);
            }
        }
        });
    }
    
    if (m_volumeSlider) {
        connect(m_volumeSlider, &QSlider::sliderMoved,
                this, &BaseVideoPlayer::on_volumeSlider_sliderMoved);
    }
    
    if (m_speedSpinBox) {
        connect(m_speedSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &BaseVideoPlayer::on_speedSpinBox_valueChanged);
    }
    
    // Media player signals
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::positionChanged,
            this, &BaseVideoPlayer::updatePosition);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::durationChanged,
            this, &BaseVideoPlayer::updateDuration);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::stateChanged,
            this, &BaseVideoPlayer::handlePlaybackStateChanged);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::errorOccurred,
            this, &BaseVideoPlayer::handleError);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::finished,
            this, &BaseVideoPlayer::handleVideoFinished);
}

bool BaseVideoPlayer::loadVideo(const QString& filePath)
{
    qDebug() << "BaseVideoPlayer: Loading video:" << filePath;
    
    QFileInfo fileInfo(filePath);
    
    if (!fileInfo.exists()) {
        qDebug() << "BaseVideoPlayer: File does not exist:" << filePath;
        //emit errorOccurred(tr("File not found: %1").arg(filePath)); // commented out because otherwise it blocks the retry attempt until the user interacts with the error message.
        return false;
    }
    
    // SECURITY: Validate that the file is actually a video before processing
    if (!InputValidation::isValidVideoFile(filePath)) {
        qDebug() << "BaseVideoPlayer: File is not a valid video:" << filePath;
        emit errorOccurred(tr("Invalid video file: %1\nThe file does not appear to be a valid video format.").arg(fileInfo.fileName()));
        return false;
    }
    
    qDebug() << "BaseVideoPlayer: Validated video file format";
    
    // Stop current playback if any
    if (m_mediaPlayer->isPlaying()) {
        m_mediaPlayer->stop();
    }
    
    // Load the media with VLC
    if (!m_mediaPlayer->loadMedia(filePath)) {
        qDebug() << "BaseVideoPlayer: Failed to load media with VLC";
        //emit errorOccurred(tr("Failed to load video: %1").arg(m_mediaPlayer->lastError())); // commented out because otherwise it blocks the retry attempt until the user interacts with the error message.
        return false;
    }
    
    // Store the media path
    m_currentVideoPath = filePath;
    
    // Force video widget to update
    m_videoWidget->update();
    m_videoWidget->show();
    
    // Process events to ensure rendering
    QApplication::processEvents();
    
    // Update window title with filename
    QString displayName = fileInfo.fileName();
    // Remove obfuscated extensions if present
    displayName.replace(".mmenc", "");
    displayName.replace(".mmvid", "");
    setWindowTitle(tr("Video Player - %1").arg(displayName));
    
    // Ensure the widget has focus for keyboard input
    setFocus();
    
    qDebug() << "BaseVideoPlayer: Video loaded successfully";
    return true;
}

void BaseVideoPlayer::play()
{
    qDebug() << "BaseVideoPlayer: Play requested";
    
    if (m_currentVideoPath.isEmpty()) {
        qDebug() << "BaseVideoPlayer: No video loaded";
        emit errorOccurred(tr("No video loaded"));
        return;
    }
    
    m_mediaPlayer->play();
    
    // Ensure we have focus for keyboard shortcuts
    setFocus();
}

void BaseVideoPlayer::pause()
{
    qDebug() << "BaseVideoPlayer: Pause requested";
    m_mediaPlayer->pause();
}

void BaseVideoPlayer::stop()
{
    qDebug() << "BaseVideoPlayer: Stop requested";
    
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
    if (m_positionSlider) {
        m_positionSlider->setValue(0);
    }
    if (m_positionLabel) {
        m_positionLabel->setText("00:00");
    }
}

void BaseVideoPlayer::unloadVideo()
{
    qDebug() << "BaseVideoPlayer: Unloading video";
    
    // Stop playback if active
    if (m_mediaPlayer && m_mediaPlayer->isPlaying()) {
        m_mediaPlayer->stop();
    }
    
    // Unload the media from VLC player
    if (m_mediaPlayer) {
        m_mediaPlayer->unloadMedia();
    }
    
    // Clear the current video path
    m_currentVideoPath.clear();
    
    // Reset UI elements
    if (m_positionSlider) {
        m_positionSlider->setValue(0);
    }
    if (m_positionLabel) {
        m_positionLabel->setText("00:00");
    }
    if (m_durationLabel) {
        m_durationLabel->setText("00:00");
    }
    
    // Update window title
    setWindowTitle(tr("Video Player"));
    
    qDebug() << "BaseVideoPlayer: Video unloaded successfully";
}

void BaseVideoPlayer::setVolume(int volume)
{
    qDebug() << "BaseVideoPlayer: Setting volume to" << volume << "%";

    // Clamp volume to valid range
    volume = qBound(0, volume, 200);

    if (m_mediaPlayer) {
        m_mediaPlayer->setVolume(volume);
    }

    if (m_volumeLabel) {
        m_volumeLabel->setText(tr("Vol (%1%):").arg(volume));
    }

    // Update slider position if not being moved by user
    if (m_volumeSlider && m_volumeSlider->value() != volume && !m_volumeSlider->isSliderDown()) {
        m_volumeSlider->setValue(volume);
    }

    // REMOVED THE AUTOMATIC UNMUTE LOGIC HERE
    // The mute state should be controlled only by toggleMute()

    // Save volume for next session
    // Always save the actual volume value, regardless of mute state
    s_lastVolume = volume;

    // Only update m_volumeBeforeMute if we're not currently muted
    if (!m_isMuted) {
        m_volumeBeforeMute = volume;
    }

    emit volumeChanged(volume);
}

void BaseVideoPlayer::setPosition(qint64 position)
{
    qDebug() << "BaseVideoPlayer: Setting position to" << position << "ms";
    
    if (!m_mediaPlayer || !m_mediaPlayer->hasMedia()) {
        qDebug() << "BaseVideoPlayer: No media loaded, cannot set position";
        return;
    }
    
    // Validate position
    qint64 duration = m_mediaPlayer->duration();
    if (duration > 0) {
        position = qBound(static_cast<qint64>(0), position, duration);
    }
    
    m_mediaPlayer->setPosition(position);
    
    // Update display immediately
    if (!m_isSliderBeingMoved && m_positionSlider) {
        m_positionSlider->setValue(static_cast<int>(position));
    }
    if (m_positionLabel) {
        m_positionLabel->setText(formatTime(position));
    }
}

void BaseVideoPlayer::setPlaybackSpeed(qreal speed)
{
    qDebug() << "BaseVideoPlayer: Setting playback speed to" << speed;
    
    // Clamp speed to valid range
    speed = qBound(0.1, speed, 5.0);
    
    if (m_mediaPlayer) {
        m_mediaPlayer->setPlaybackRate(static_cast<float>(speed));
    }
    
    // Update spin box to reflect the speed
    if (m_speedSpinBox && !qFuzzyCompare(m_speedSpinBox->value(), speed)) {
        m_speedSpinBox->blockSignals(true);
        m_speedSpinBox->setValue(speed);
        m_speedSpinBox->blockSignals(false);
    }
    
    // Save playback speed for next session (like volume)
    s_lastPlaybackSpeed = speed;
    
    emit playbackSpeedChanged(speed);
}

void BaseVideoPlayer::toggleMute()
{
    qDebug() << "BaseVideoPlayer: Toggle mute called, current mute state:" << m_isMuted;

    if (m_isMuted) {
        // Unmute - restore previous volume
        qDebug() << "BaseVideoPlayer: Unmuting, restoring volume to" << m_volumeBeforeMute << "%";
        m_isMuted = false;

        // Restore the previous volume
        if (m_mediaPlayer) {
            m_mediaPlayer->setVolume(m_volumeBeforeMute);
        }

        // Update UI
        if (m_volumeSlider) {
            m_volumeSlider->setValue(m_volumeBeforeMute);
        }
        if (m_volumeLabel) {
            m_volumeLabel->setText(tr("Vol (%1%):").arg(m_volumeBeforeMute));
        }
        if (m_muteButton) {
            m_muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
            m_muteButton->setToolTip(tr("Mute (M)"));
        }

        // Update s_lastVolume to the restored volume
        s_lastVolume = m_volumeBeforeMute;

        emit volumeChanged(m_volumeBeforeMute);
    } else {
        // Mute - save current volume and set to 0
        m_volumeBeforeMute = m_mediaPlayer ? m_mediaPlayer->volume() : s_lastVolume;
        qDebug() << "BaseVideoPlayer: Muting, saving current volume" << m_volumeBeforeMute << "%";
        m_isMuted = true;

        // Set volume to 0
        if (m_mediaPlayer) {
            m_mediaPlayer->setVolume(0);
        }

        // Update UI
        if (m_volumeSlider) {
            m_volumeSlider->setValue(0);
        }
        if (m_volumeLabel) {
            m_volumeLabel->setText(tr("Vol (0%):"));
        }
        if (m_muteButton) {
            m_muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolumeMuted));
            m_muteButton->setToolTip(tr("Unmute (M)"));
        }

        // Don't change s_lastVolume when muting - keep the actual volume value

        emit volumeChanged(0);
    }
}

void BaseVideoPlayer::toggleFullScreen()
{
    if (m_isFullScreen) {
        exitFullScreen();
    } else {
        enterFullScreen();
    }
}

void BaseVideoPlayer::enterFullScreen()
{
    if (!m_isFullScreen) {
        qDebug() << "BaseVideoPlayer: Entering fullscreen mode";
        
        // Store normal geometry before going fullscreen
        m_normalGeometry = geometry();
        
        // Always use the current screen where the player is located
        // This ensures fullscreen happens on the monitor where the player window currently is
        QScreen* screen = getCurrentScreen();
        if (!screen) {
            // Fallback to target screen if set, otherwise primary screen
            screen = m_targetScreen ? m_targetScreen.data() : QGuiApplication::primaryScreen();
        }
        
        // Store the screen we're using
        s_lastUsedScreen = screen;
        
        // Get the screen geometry
        QRect screenGeometry = screen->geometry();
        
        // Move to the target screen first
        move(screenGeometry.topLeft());
        
        // Then show fullscreen
        showFullScreen();
        
        // Remove margins in fullscreen
        m_mainLayout->setContentsMargins(0, 0, 0, 0);
        
        // Set fullscreen flag BEFORE starting timers so startCursorTimer() works correctly
        m_isFullScreen = true;
        
        // Initialize mouse position to current cursor position to properly track movement
        m_lastMousePos = QCursor::pos();
        qDebug() << "BaseVideoPlayer: Initialized mouse position to" << m_lastMousePos;
        
        // Start timer to auto-hide controls (must be called AFTER m_isFullScreen is set to true)
        startCursorTimer();
        m_mouseCheckTimer->start();
        
        // Update button icon
        if (m_fullScreenButton) {
            m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
            m_fullScreenButton->setToolTip(tr("Exit Full Screen (F11/Esc)"));
        }
        
        emit fullScreenChanged(true);
    }
}

void BaseVideoPlayer::exitFullScreen()
{
    if (m_isFullScreen) {
        qDebug() << "BaseVideoPlayer: Exiting fullscreen mode";
        
        // Stop cursor hide timers
        stopCursorTimer();
        m_mouseCheckTimer->stop();
        
        // Reset mouse position tracking
        m_lastMousePos = QPoint(-1, -1);
        
        // Show cursor and controls
        showCursor();
        m_controlsWidget->setVisible(true);
        
        // Restore margins
        m_mainLayout->setContentsMargins(m_normalMargins);
        
        // Exit fullscreen mode
        showNormal();
        
        // Restore geometry
        if (!m_normalGeometry.isEmpty()) {
            setGeometry(m_normalGeometry);
        } else {
            // If no stored geometry, center on screen
            QScreen* screen = m_targetScreen.data();
            if (!screen) {
                screen = (s_lastUsedScreen && QGuiApplication::screens().contains(s_lastUsedScreen)) 
                        ? s_lastUsedScreen 
                        : QGuiApplication::primaryScreen();
            }
            if (screen) {
                QRect screenGeometry = screen->availableGeometry();
                move(screenGeometry.center() - rect().center());
            }
        }
        
        // Ensure window is raised and active
        raise();
        activateWindow();
        
        m_isFullScreen = false;
        
        // Update button icon
        if (m_fullScreenButton) {
            m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
            m_fullScreenButton->setToolTip(tr("Full Screen (F11)"));
        }
        
        emit fullScreenChanged(false);
    }
}

void BaseVideoPlayer::startInFullScreen()
{
    qDebug() << "BaseVideoPlayer: Starting in fullscreen mode";
    
    // Set flag before showing window
    m_isFullScreen = false;  // Reset flag so enterFullScreen works
    
    // Show window first
    show();
    
    // Then enter fullscreen
    QTimer::singleShot(100, this, &BaseVideoPlayer::enterFullScreen);
}

// State query functions
bool BaseVideoPlayer::isPlaying() const
{
    return m_mediaPlayer && m_mediaPlayer->isPlaying();
}

bool BaseVideoPlayer::isPaused() const
{
    return m_mediaPlayer && m_mediaPlayer->isPaused();
}

qint64 BaseVideoPlayer::duration() const
{
    return m_mediaPlayer ? m_mediaPlayer->duration() : 0;
}

qint64 BaseVideoPlayer::position() const
{
    return m_mediaPlayer ? m_mediaPlayer->position() : 0;
}

int BaseVideoPlayer::volume() const
{
    return m_mediaPlayer ? m_mediaPlayer->volume() : 0;
}

bool BaseVideoPlayer::isMuted() const
{
    return m_isMuted;
}

qreal BaseVideoPlayer::playbackSpeed() const
{
    return m_mediaPlayer ? static_cast<qreal>(m_mediaPlayer->playbackRate()) : 1.0;
}

QString BaseVideoPlayer::currentVideoPath() const
{
    return m_currentVideoPath;
}

// Slot implementations
void BaseVideoPlayer::on_playButton_clicked()
{
    qDebug() << "BaseVideoPlayer: Play button clicked";
    
    if (m_mediaPlayer->isPlaying()) {
        pause();
    } else {
        play();
    }
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void BaseVideoPlayer::on_positionSlider_sliderMoved(int position)
{
    qDebug() << "BaseVideoPlayer: Position slider moved to" << position;
    setPosition(position);
}

void BaseVideoPlayer::on_positionSlider_sliderPressed()
{
    qDebug() << "BaseVideoPlayer: Position slider pressed";
    m_isSliderBeingMoved = true;
}

void BaseVideoPlayer::on_positionSlider_sliderReleased()
{
    qDebug() << "BaseVideoPlayer: Position slider released";
    m_isSliderBeingMoved = false;
    
    // Return focus to main widget for keyboard shortcuts after a small delay
    QTimer::singleShot(100, this, &BaseVideoPlayer::ensureKeyboardFocus);
}

void BaseVideoPlayer::on_volumeSlider_sliderMoved(int position)
{
    qDebug() << "BaseVideoPlayer: Volume slider moved to" << position << "%";
    setVolume(position);
}

void BaseVideoPlayer::on_speedSpinBox_valueChanged(double value)
{
    qDebug() << "BaseVideoPlayer: Speed spin box changed to" << value;
    
    setPlaybackSpeed(value);
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void BaseVideoPlayer::on_fullScreenButton_clicked()
{
    qDebug() << "BaseVideoPlayer: Fullscreen button clicked";
    toggleFullScreen();
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void BaseVideoPlayer::on_muteButton_clicked()
{
    qDebug() << "BaseVideoPlayer: Mute button clicked";
    toggleMute();
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void BaseVideoPlayer::updatePosition(qint64 position)
{
    if (!m_isSliderBeingMoved && m_positionSlider) {
        m_positionSlider->setValue(static_cast<int>(position));
    }
    if (m_positionLabel) {
        m_positionLabel->setText(formatTime(position));
    }
    emit positionChanged(position);
}

void BaseVideoPlayer::updateDuration(qint64 duration)
{
    qDebug() << "BaseVideoPlayer: Duration updated to" << duration << "ms";
    
    if (m_positionSlider) {
        m_positionSlider->setMaximum(static_cast<int>(duration));
    }
    if (m_durationLabel) {
        m_durationLabel->setText(formatTime(duration));
    }
    
    emit durationChanged(duration);
}

void BaseVideoPlayer::handleError(const QString &errorString)
{
    qDebug() << "BaseVideoPlayer: Error occurred:" << errorString;
    emit errorOccurred(errorString);
}

void BaseVideoPlayer::handlePlaybackStateChanged(VP_VLCPlayer::PlayerState state)
{
    qDebug() << "BaseVideoPlayer: Playback state changed to" << static_cast<int>(state);
    
    if (!m_playButton) {
        emit playbackStateChanged(state);
        return;
    }
    
    switch (state) {
        case VP_VLCPlayer::PlayerState::Playing:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
            m_playButton->setToolTip(tr("Pause"));
            
            if (!m_playbackStartedEmitted) {
                m_playbackStartedEmitted = true;
                emit playbackStarted();
            }
            break;
            
        case VP_VLCPlayer::PlayerState::Paused:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_playButton->setToolTip(tr("Play"));
            break;
            
        case VP_VLCPlayer::PlayerState::Stopped:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_playButton->setToolTip(tr("Play"));
            m_playbackStartedEmitted = false;
            break;
            
        default:
            break;
    }
    
    emit playbackStateChanged(state);
}

void BaseVideoPlayer::handleVideoFinished()
{
    qDebug() << "BaseVideoPlayer: Video finished - closing player";
    
    // Emit the finished signal first for any handlers
    emit finished();
    
    // Close the player window
    close();
}

// Windows shutdown detection
bool BaseVideoPlayer::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    // Check for Windows-specific messages
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        
        // Handle Windows shutdown messages
        if (msg->message == WM_QUERYENDSESSION) {
            qDebug() << "BaseVideoPlayer: WM_QUERYENDSESSION received - Windows wants to shutdown";
            
            // Set shutdown flag to prevent any further operations
            m_windowsShutdownInProgress = true;
            
            // CRITICAL: Stop video playback immediately to prevent memory access errors
            if (m_mediaPlayer) {
                qDebug() << "BaseVideoPlayer: Emergency stopping video playback for Windows shutdown";
                m_mediaPlayer->stop();
                
                // Process events to ensure stop completes
                QApplication::processEvents();
            }
            
            // Create shutdown block to tell Windows to wait while we clean up
            HWND hwnd = reinterpret_cast<HWND>(this->winId());
            ShutdownBlockReasonCreate(hwnd, L"Video Player is closing...");
            qDebug() << "BaseVideoPlayer: Created shutdown block reason";
            
            // Mark that we're closing to prevent any further operations
            m_isClosing = true;
            
            // Close the player window properly
            close();
            
            // Tell Windows we can be shut down (but we're asking it to wait)
            if (result) {
                *result = TRUE;
            }
            return true;
        }
        else if (msg->message == WM_ENDSESSION) {
            qDebug() << "BaseVideoPlayer: WM_ENDSESSION received";
            
            // This comes after WM_QUERYENDSESSION if shutdown proceeds
            // Ensure video is stopped (belt and suspenders approach)
            if (m_mediaPlayer) {
                m_mediaPlayer->stop();
            }
            
            if (result) {
                *result = TRUE;
            }
            return true;
        }
    }
#endif
    
    // Call base class implementation for other events
    return QWidget::nativeEvent(eventType, message, result);
}

// Event handlers
void BaseVideoPlayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "BaseVideoPlayer: Close event received";
    
    if (!m_isClosing) {
        m_isClosing = true;
        
        // Save window state
        s_wasFullScreen = m_isFullScreen;
        s_wasMaximized = isMaximized();
        s_wasMinimized = isMinimized();
        
        qDebug() << "BaseVideoPlayer: Saving window state - Fullscreen:" << s_wasFullScreen
                 << "Maximized:" << s_wasMaximized << "Minimized:" << s_wasMinimized;
        
        if (!m_isFullScreen && !isMaximized() && !isMinimized()) {
            s_lastWindowGeometry = geometry();
            qDebug() << "BaseVideoPlayer: Saved normal window geometry:" << s_lastWindowGeometry;
        }
        
        s_lastUsedScreen = getCurrentScreen();
        s_hasStoredSettings = true;
        
        // Get final position before stopping
        qint64 finalPosition = m_mediaPlayer ? m_mediaPlayer->position() : 0;
        
        // Stop playback
        if (m_mediaPlayer) {
            m_mediaPlayer->stop();
        }
        
        // Emit signal with final position
        emit aboutToClose(finalPosition);
        
#ifdef Q_OS_WIN
        // If Windows is shutting down, destroy the shutdown block
        if (m_windowsShutdownInProgress) {
            qDebug() << "BaseVideoPlayer: Removing Windows shutdown block";
            HWND hwnd = reinterpret_cast<HWND>(this->winId());
            ShutdownBlockReasonDestroy(hwnd);
            qDebug() << "BaseVideoPlayer: Windows can now continue shutdown";
        }
#endif
    }
    
    event->accept();
}

void BaseVideoPlayer::showEvent(QShowEvent *event)
{
    qDebug() << "BaseVideoPlayer: Show event received";
    qDebug() << "BaseVideoPlayer: Has stored settings:" << s_hasStoredSettings;
    qDebug() << "BaseVideoPlayer: Last used screen valid:" << (s_lastUsedScreen && QGuiApplication::screens().contains(s_lastUsedScreen));
    
    // Always try to restore to the correct monitor if we have one stored
    if (!m_isClosing && s_lastUsedScreen && QGuiApplication::screens().contains(s_lastUsedScreen)) {
        // Ensure we're on the correct screen after showing
        if (windowHandle() && windowHandle()->screen() != s_lastUsedScreen) {
            qDebug() << "BaseVideoPlayer: Moving window to last used screen after show";
            windowHandle()->setScreen(s_lastUsedScreen);
        }
    }
    
    // Mark that we now have stored settings for future use
    // This ensures monitor persistence works even for the first manual play
    if (!m_isClosing) {
        s_hasStoredSettings = true;
    }
    
    // Ensure we have focus
    setFocus();
    
    QWidget::showEvent(event);
}

void BaseVideoPlayer::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "BaseVideoPlayer: Key press event - Key:" << event->key() << "Modifiers:" << event->modifiers();
    
    // Check if Ctrl key is pressed (works for both left and right Ctrl)
    bool ctrlPressed = (event->modifiers() & Qt::ControlModifier);
    
    switch (event->key()) {
        case Qt::Key_Space:
            on_playButton_clicked();
            event->accept();
            break;
            
        case Qt::Key_M:
            // M key for mute/unmute
            toggleMute();
            event->accept();
            break;
            
        case Qt::Key_F11:
            toggleFullScreen();
            event->accept();
            break;
            
        case Qt::Key_Escape:
            if (m_isFullScreen) {
                exitFullScreen();
                event->accept();
            } else {
                // Close the player when not in fullscreen
                qDebug() << "BaseVideoPlayer: ESC pressed while not in fullscreen, closing player";
                close();
                event->accept();
            }
            break;
            
        case Qt::Key_Right:
            // Normal Right: Seek forward 10 seconds
            if (m_mediaPlayer && m_mediaPlayer->hasMedia()) {
                qint64 newPos = m_mediaPlayer->position() + 10000;
                setPosition(newPos);
                event->accept();
            }
            break;
            
        case Qt::Key_Left:
            // Normal Left: Seek backward 10 seconds
            if (m_mediaPlayer && m_mediaPlayer->hasMedia()) {
                qint64 newPos = m_mediaPlayer->position() - 10000;
                setPosition(newPos);
                event->accept();
            }
            break;
            
        case Qt::Key_Up:
            if (ctrlPressed) {
                // Ctrl+Up: Increase playback speed by 0.1
                if (m_speedSpinBox) {
                    qreal currentSpeed = m_speedSpinBox->value();
                    qreal newSpeed = currentSpeed + 0.1;
                    if (newSpeed <= 5.0) {
                        qDebug() << "BaseVideoPlayer: Ctrl+Up - increasing playback speed from" << currentSpeed << "to" << newSpeed;
                        setPlaybackSpeed(newSpeed);
                        event->accept();
                    } else {
                        qDebug() << "BaseVideoPlayer: Already at maximum playback speed (5.0x)";
                    }
                }
            } else {
                // Normal Up: Increase volume by 5%
                setVolume(volume() + 5);
                event->accept();
            }
            break;
            
        case Qt::Key_Down:
            if (ctrlPressed) {
                // Ctrl+Down: Decrease playback speed by 0.1
                if (m_speedSpinBox) {
                    qreal currentSpeed = m_speedSpinBox->value();
                    qreal newSpeed = currentSpeed - 0.1;
                    if (newSpeed >= 0.1) {
                        qDebug() << "BaseVideoPlayer: Ctrl+Down - decreasing playback speed from" << currentSpeed << "to" << newSpeed;
                        setPlaybackSpeed(newSpeed);
                        event->accept();
                    } else {
                        qDebug() << "BaseVideoPlayer: Already at minimum playback speed (0.1x)";
                    }
                }
            } else {
                // Normal Down: Decrease volume by 5%
                setVolume(volume() - 5);
                event->accept();
            }
            break;
            
        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void BaseVideoPlayer::wheelEvent(QWheelEvent *event)
{
    // Check if Ctrl key is pressed
    bool ctrlPressed = (event->modifiers() & Qt::ControlModifier);
    int delta = event->angleDelta().y();
    
    if (ctrlPressed) {
        // Ctrl + mouse wheel: Adjust playback speed
        if (delta > 0) {
            // Ctrl + wheel up: Increase playback speed by 0.1
            if (m_speedSpinBox) {
                qreal currentSpeed = m_speedSpinBox->value();
                qreal newSpeed = currentSpeed + 0.1;
                if (newSpeed <= 5.0) {
                    qDebug() << "BaseVideoPlayer: Ctrl+MouseWheel Up - increasing playback speed from" << currentSpeed << "to" << newSpeed;
                    setPlaybackSpeed(newSpeed);
                } else {
                    qDebug() << "BaseVideoPlayer: Already at maximum playback speed (5.0x)";
                }
            }
        } else if (delta < 0) {
            // Ctrl + wheel down: Decrease playback speed by 0.1
            if (m_speedSpinBox) {
                qreal currentSpeed = m_speedSpinBox->value();
                qreal newSpeed = currentSpeed - 0.1;
                if (newSpeed >= 0.1) {
                    qDebug() << "BaseVideoPlayer: Ctrl+MouseWheel Down - decreasing playback speed from" << currentSpeed << "to" << newSpeed;
                    setPlaybackSpeed(newSpeed);
                } else {
                    qDebug() << "BaseVideoPlayer: Already at minimum playback speed (0.1x)";
                }
            }
        }
    } else {
        // Normal mouse wheel: Adjust volume
        if (delta > 0) {
            setVolume(volume() + 5);
        } else if (delta < 0) {
            setVolume(volume() - 5);
        }
    }
    
    event->accept();
}

void BaseVideoPlayer::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    
    if (m_isFullScreen) {
        // Show cursor and controls
        showCursor();
        
        if (!m_controlsWidget->isVisible()) {
            m_controlsWidget->setVisible(true);
        }
        
        // Restart the hide timer
        startCursorTimer();
    }
}

bool BaseVideoPlayer::eventFilter(QObject *watched, QEvent *event)
{
    // Handle double-click on video widget for fullscreen toggle
    if (watched == m_videoWidget && event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleFullScreen();
            return true;
        }
    }
    
    // Handle mouse movement on controls to show them in fullscreen
    if (m_isFullScreen && (watched == m_controlsWidget || watched == m_videoWidget)) {
        if (event->type() == QEvent::MouseMove) {
            showCursor();
            if (!m_controlsWidget->isVisible()) {
                m_controlsWidget->setVisible(true);
            }
            startCursorTimer();
        }
    }
    
    return QWidget::eventFilter(watched, event);
}

void BaseVideoPlayer::focusInEvent(QFocusEvent *event)
{
    qDebug() << "BaseVideoPlayer: Focus in event - Reason:" << event->reason();
    QWidget::focusInEvent(event);
}

// Helper methods
QString BaseVideoPlayer::formatTime(qint64 milliseconds) const
{
    if (milliseconds < 0) {
        return "00:00";
    }
    
    int hours = milliseconds / 3600000;
    int minutes = (milliseconds % 3600000) / 60000;
    int seconds = (milliseconds % 60000) / 1000;
    
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
}

void BaseVideoPlayer::ensureKeyboardFocus()
{
    qDebug() << "BaseVideoPlayer: Ensuring keyboard focus";
    
    // Clear focus from any control widgets
    if (m_positionSlider->hasFocus()) {
        m_positionSlider->clearFocus();
    }
    if (m_volumeSlider->hasFocus()) {
        m_volumeSlider->clearFocus();
    }
    
    // Set focus to the main widget
    setFocus(Qt::OtherFocusReason);
    
    // Also raise and activate the window
    raise();
    activateWindow();
}

void BaseVideoPlayer::startCursorTimer()
{
    if (m_isFullScreen && m_cursorTimer) {
        m_cursorTimer->stop();
        m_cursorTimer->start(3000);  // Hide after 3 seconds
    }
}

void BaseVideoPlayer::stopCursorTimer()
{
    if (m_cursorTimer) {
        m_cursorTimer->stop();
    }
}

void BaseVideoPlayer::hideCursor()
{
    if (m_isFullScreen) {
        setCursor(Qt::BlankCursor);
        m_videoWidget->setCursor(Qt::BlankCursor);
        m_controlsWidget->setVisible(false);
        qDebug() << "BaseVideoPlayer: Cursor and controls hidden";
    }
}

void BaseVideoPlayer::showCursor()
{
    setCursor(Qt::ArrowCursor);
    m_videoWidget->setCursor(Qt::ArrowCursor);
    qDebug() << "BaseVideoPlayer: Cursor shown";
}

void BaseVideoPlayer::checkMouseMovement()
{
    if (!m_isFullScreen) {
        return;
    }
    
    QPoint currentPos = QCursor::pos();
    
    // Check if this is the first check (initialization)
    if (m_lastMousePos == QPoint(-1, -1)) {
        // First time checking - initialize position but don't show controls
        m_lastMousePos = currentPos;
        qDebug() << "BaseVideoPlayer: Initial mouse position set to" << currentPos;
        // The cursor timer should already be running from enterFullScreen()
        return;
    }
    
    // Check if mouse has moved
    if (m_lastMousePos != currentPos) {
        // Mouse moved
        QScreen* currentScreen = getCurrentScreen();
        QScreen* mouseScreen = QGuiApplication::screenAt(currentPos);
        
        if (currentScreen == mouseScreen) {
            qDebug() << "BaseVideoPlayer: Mouse movement detected on same screen from"
                      << m_lastMousePos << "to" << currentPos;
            
            // Show cursor and controls
            showCursor();
            
            if (!m_controlsWidget->isVisible()) {
                m_controlsWidget->setVisible(true);
            }
            
            // Restart the hide timer
            startCursorTimer();
        }
    }
    
    m_lastMousePos = currentPos;
}

QScreen* BaseVideoPlayer::getCurrentScreen() const
{
    qDebug() << "BaseVideoPlayer: Getting current screen for player window";
    
    // First try to get the screen through the window handle
    if (windowHandle()) {
        QScreen* screen = windowHandle()->screen();
        if (screen) {
            return screen;
        }
    }
    
    // Fallback: Find which screen contains the center of our window
    QPoint center = geometry().center();
    
    if (isWindow()) {
        center = geometry().center();
    } else {
        center = mapToGlobal(rect().center());
    }
    
    QScreen* screen = QGuiApplication::screenAt(center);
    if (screen) {
        return screen;
    }
    
    // Last fallback: return primary screen
    return QGuiApplication::primaryScreen();
}

void BaseVideoPlayer::forceUpdateSliderPosition(qint64 position)
{
    qDebug() << "BaseVideoPlayer: Force updating slider position to" << position;
    
    // Temporarily disable the flag to force update
    bool wasBeingMoved = m_isSliderBeingMoved;
    m_isSliderBeingMoved = false;
    
    // Update the slider
    if (m_positionSlider) {
        m_positionSlider->setValue(static_cast<int>(position));
    }
    if (m_positionLabel) {
        m_positionLabel->setText(formatTime(position));
    }
    
    // Restore the flag
    m_isSliderBeingMoved = wasBeingMoved;
}

bool BaseVideoPlayer::shouldUpdateProgress(qint64 currentPosition) const
{
    // This method can be used by derived classes to determine if progress should be saved
    // For now, always return true in base class
    Q_UNUSED(currentPosition)
    return true;
}
