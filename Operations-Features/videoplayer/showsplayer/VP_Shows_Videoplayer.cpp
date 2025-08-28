#include "VP_Shows_Videoplayer.h"
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
QScreen* VP_Shows_Videoplayer::s_lastUsedScreen = nullptr;
QRect VP_Shows_Videoplayer::s_lastWindowGeometry = QRect();
bool VP_Shows_Videoplayer::s_wasFullScreen = false;
bool VP_Shows_Videoplayer::s_wasMaximized = false;
bool VP_Shows_Videoplayer::s_wasMinimized = false;
int VP_Shows_Videoplayer::s_lastVolume = 70;  // Default volume
bool VP_Shows_Videoplayer::s_hasStoredSettings = false;

// Custom clickable slider class for seeking in video
class VP_Shows_Videoplayer::ClickableSlider : public QSlider
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
            
            // Get widget dimensions
            int widgetWidth = width();
            int widgetHeight = height();
            int mouseX = event->x();
            int mouseY = event->y();
            
            // Check if widget is properly sized
            if (widgetWidth <= 0 || widgetHeight <= 0) {
                qDebug() << "VP_Shows_Videoplayer::ClickableSlider: WARNING - Widget has invalid dimensions:" << widgetWidth << "x" << widgetHeight;
                // Fall back to default behavior
                QSlider::mousePressEvent(event);
                return;
            }
            
            // Debug logging for fullscreen issues
            static bool debugLogged = false;
            if (!debugLogged || (widgetWidth > 1000)) { // Log in fullscreen (wider widgets)
                qDebug() << "VP_Shows_Videoplayer::ClickableSlider: Mouse press at" << mouseX << "," << mouseY;
                qDebug() << "VP_Shows_Videoplayer::ClickableSlider: Widget dimensions:" << widgetWidth << "x" << widgetHeight;
                qDebug() << "VP_Shows_Videoplayer::ClickableSlider: Slider range:" << minimum() << "to" << maximum();
                debugLogged = true;
            }
            
            // Clamp mouse position to widget bounds
            mouseX = qBound(0, mouseX, widgetWidth);
            mouseY = qBound(0, mouseY, widgetHeight);
            
            // Calculate position based on click using qint64 to prevent overflow
            qint64 value;
            if (orientation() == Qt::Horizontal) {
                // Use qint64 for calculation to prevent overflow
                qint64 range = static_cast<qint64>(maximum()) - static_cast<qint64>(minimum());
                qint64 clickPos = static_cast<qint64>(mouseX);
                qint64 widgetSize = static_cast<qint64>(widgetWidth);
                
                // Prevent division by zero
                if (widgetSize > 0) {
                    value = minimum() + (range * clickPos) / widgetSize;
                } else {
                    value = minimum();
                }
            } else {
                // Vertical slider
                qint64 range = static_cast<qint64>(maximum()) - static_cast<qint64>(minimum());
                qint64 clickPos = static_cast<qint64>(widgetHeight - mouseY);
                qint64 widgetSize = static_cast<qint64>(widgetHeight);
                
                // Prevent division by zero
                if (widgetSize > 0) {
                    value = minimum() + (range * clickPos) / widgetSize;
                } else {
                    value = minimum();
                }
            }
            
            // Clamp the value to valid range
            value = qBound(static_cast<qint64>(minimum()), value, static_cast<qint64>(maximum()));
            
            qDebug() << "VP_Shows_Videoplayer::ClickableSlider: Calculated value:" << value;
            
            setValue(static_cast<int>(value));
            emit sliderMoved(static_cast<int>(value));
            emit sliderPressed();
            
            // Continue with normal slider behavior
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
        // If we lose focus while pressed, emit the release signal
        if (m_isPressed) {
            m_isPressed = false;
            emit sliderReleased();
        }
        QSlider::focusOutEvent(event);
    }
    
private:
    bool m_isPressed;
};

VP_Shows_Videoplayer::VP_Shows_Videoplayer(QWidget *parent)
    : QWidget(parent)
    , m_videoWidget(nullptr)
    , m_playButton(nullptr)
    , m_stopButton(nullptr)
    , m_fullScreenButton(nullptr)
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
    , m_cursorTimer(nullptr)
    , m_mouseCheckTimer(nullptr)
    , m_lastMousePos(QPoint(-1, -1))
    , m_watchHistory(nullptr)
    , m_progressSaveTimer(nullptr)
    , m_lastSavedPosition(0)
    , m_hasStartedPlaying(false)
    , m_isClosing(false)
    , m_playbackStartedEmitted(false)
    , m_targetScreen(nullptr)
{
    qDebug() << "VP_Shows_Videoplayer: Constructor called";
    
    // Set window properties
    setWindowTitle(tr("Video Player"));
    resize(800, 600);
    
    // Initialize VLC player
    m_mediaPlayer = std::make_unique<VP_VLCPlayer>(this);
    
    // Initialize VLC
    if (!m_mediaPlayer->initialize()) {
        qDebug() << "VP_Shows_Videoplayer: ERROR - Failed to initialize VLC player";
    }
    
    // Setup cursor timer for auto-hide
    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(2000); // 2 seconds
    connect(m_cursorTimer, &QTimer::timeout, this, &VP_Shows_Videoplayer::hideCursor);
    
    // Setup mouse check timer for movement detection
    m_mouseCheckTimer = new QTimer(this);
    m_mouseCheckTimer->setInterval(100); // Check every 100ms
    connect(m_mouseCheckTimer, &QTimer::timeout, this, &VP_Shows_Videoplayer::checkMouseMovement);
    
    qDebug() << "VP_Shows_Videoplayer: Mouse tracking timers initialized";
    
    // Note: Progress save timer is not needed since Operations_VP_Shows_WatchHistory handles it
    m_progressSaveTimer = nullptr;
    
    // Enable mouse tracking to detect mouse movement
    setMouseTracking(true);
    
    // Set focus policy for keyboard input
    setFocusPolicy(Qt::StrongFocus);
    
    // Setup UI
    setupUI();
    
    // Connect signals
    connectSignals();
    
    qDebug() << "VP_Shows_Videoplayer: Initialization complete";
}

VP_Shows_Videoplayer::~VP_Shows_Videoplayer()
{
    qDebug() << "VP_Shows_Videoplayer: Destructor called";
    
    // Note: Watch history is managed by Operations_VP_Shows_WatchHistory
    // The aboutToClose signal handles final position saving
    
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        m_mediaPlayer->unloadMedia();
    }
}

void VP_Shows_Videoplayer::toggleFullScreen()
{
    if (m_isFullScreen) {
        exitFullScreen();
    } else {
        // Enter fullscreen
        qDebug() << "VP_Shows_Videoplayer: Entering fullscreen mode";
        
        // Store normal geometry
        m_normalGeometry = geometry();
        
        // Hide controls in fullscreen (can be shown on hover/click)
        m_controlsWidget->setVisible(false);
        
        // Remove all margins for fullscreen
        m_mainLayout->setContentsMargins(0, 0, 0, 0);
        
        // Get the screen the player is currently on BEFORE changing window flags
        QScreen *screen = getCurrentScreen();
        if (!screen) {
            screen = QApplication::primaryScreen();
            qDebug() << "VP_Shows_Videoplayer: Fallback to primary screen";
        }
        
        if (screen) {
            // Remember this screen for when we close
            s_lastUsedScreen = screen;
            qDebug() << "VP_Shows_Videoplayer: Toggling fullscreen on screen:" << screen->name();
            
            // Move window to the target screen BEFORE going fullscreen
            // This ensures fullscreen happens on the correct monitor
            QRect screenGeometry = screen->geometry();
            move(screenGeometry.topLeft());
        }
        
        // Remove window frame for true fullscreen
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        
        // Now show fullscreen - it will use the screen we just moved to
        showFullScreen();
        m_isFullScreen = true;
        
        // Set geometry to fill the entire screen
        if (screen) {
            setGeometry(screen->geometry());
        }
        
        // Update button icon
        m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        m_fullScreenButton->setToolTip(tr("Exit Full Screen (ESC)"));
        
        emit fullScreenChanged(true);
        
        // Start cursor timer for auto-hide
        startCursorTimer();
        
        // Start mouse movement check timer
        if (m_mouseCheckTimer) {
            m_mouseCheckTimer->start();
            qDebug() << "VP_Shows_Videoplayer: Started mouse movement check timer";
        }
        
        // Ensure we have focus for keyboard input
        setFocus();
        raise();
        activateWindow();
        
        // Force a layout update to ensure slider is properly sized
        QTimer::singleShot(100, [this]() {
            if (m_controlsWidget && m_positionSlider) {
                qDebug() << "VP_Shows_Videoplayer: Fullscreen layout update - slider width:" << m_positionSlider->width();
                qDebug() << "VP_Shows_Videoplayer: Fullscreen layout update - controls width:" << m_controlsWidget->width();
                qDebug() << "VP_Shows_Videoplayer: Fullscreen layout update - window width:" << width();
                // Force the slider to update its geometry
                m_positionSlider->updateGeometry();
                m_controlsWidget->updateGeometry();
            }
        });
    }
}

void VP_Shows_Videoplayer::exitFullScreen()
{
    if (m_isFullScreen) {
        qDebug() << "VP_Shows_Videoplayer: Exiting fullscreen mode";
        
        // Stop both timers and restore cursor
        stopCursorTimer();
        showCursor();
        
        // Show controls
        m_controlsWidget->setVisible(true);
        
        // Restore normal layout margins
        m_mainLayout->setContentsMargins(m_normalMargins);
        
        // Restore normal window flags (with title bar and borders)
        setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | 
                      Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        
        // Show in normal mode
        showNormal();
        
        // Restore geometry or use default size
        if (!m_normalGeometry.isNull()) {
            setGeometry(m_normalGeometry);
        } else {
            resize(800, 600);
            // Center the window on the screen it was fullscreen on (or current screen)
            QScreen *screen = nullptr;
            if (windowHandle()) {
                screen = windowHandle()->screen();
            }
            if (!screen) {
                // Fallback to target screen or primary
                screen = m_targetScreen ? m_targetScreen : QApplication::primaryScreen();
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
        m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        m_fullScreenButton->setToolTip(tr("Full Screen (F11)"));
        
        emit fullScreenChanged(false);
    }
}

void VP_Shows_Videoplayer::setupUI()
{
    qDebug() << "VP_Shows_Videoplayer: Setting up UI";
    
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
    
    // Install event filter on video widget for double-click fullscreen and focus restoration
    // This works because we disable libvlc's mouse/keyboard input handling in VP_VLCPlayer
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

VP_Shows_Videoplayer::ClickableSlider* VP_Shows_Videoplayer::createClickableSlider()
{
    return new ClickableSlider(Qt::Horizontal, this);
}

void VP_Shows_Videoplayer::createControls()
{
    qDebug() << "VP_Shows_Videoplayer: Creating controls";
    
    // Play/Pause button
    m_playButton = new QPushButton(this);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setToolTip(tr("Play"));
    m_playButton->setFocusPolicy(Qt::NoFocus);  // Prevent button from taking keyboard focus
    
    // Stop button
    m_stopButton = new QPushButton(this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setToolTip(tr("Stop"));
    m_stopButton->setFocusPolicy(Qt::NoFocus);  // Prevent button from taking keyboard focus
    
    // Fullscreen button
    m_fullScreenButton = new QPushButton(this);
    m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_fullScreenButton->setToolTip(tr("Full Screen (F11)"));
    m_fullScreenButton->setFocusPolicy(Qt::NoFocus);  // Prevent button from taking keyboard focus
    
    // Position slider - use custom clickable slider
    m_positionSlider = createClickableSlider();
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setToolTip(tr("Click to seek"));
    m_positionSlider->setFocusPolicy(Qt::ClickFocus);  // Only take focus when clicked, and give it up easily
    
    // Volume slider - use custom clickable slider, extended range to 150%
    m_volumeSlider = createClickableSlider();
    m_volumeSlider->setRange(0, 150);  // Extended to 150%
    m_volumeSlider->setValue(s_lastVolume);  // Use saved volume from previous session
    m_volumeSlider->setMaximumWidth(100);
    m_volumeSlider->setToolTip(tr("Volume (up to 150%)"));
    m_volumeSlider->setFocusPolicy(Qt::ClickFocus);  // Only take focus when clicked, and give it up easily
    
    // Speed combo box
    m_speedComboBox = new QComboBox(this);
    m_speedComboBox->addItem("0.5x", 0.5);
    m_speedComboBox->addItem("1x", 1.0);
    m_speedComboBox->addItem("1.5x", 1.5);
    m_speedComboBox->addItem("2x", 2.0);
    m_speedComboBox->addItem("3x", 3.0);
    m_speedComboBox->setCurrentIndex(1);  // Default to 1x speed
    m_speedComboBox->setMaximumWidth(60);
    m_speedComboBox->setToolTip(tr("Playback Speed"));
    m_speedComboBox->setFocusPolicy(Qt::NoFocus);  // Prevent combo box from taking keyboard focus
    
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

void VP_Shows_Videoplayer::createLayouts()
{
    qDebug() << "VP_Shows_Videoplayer: Creating layouts";
    
    // Create a widget to hold all controls (for fullscreen mode)
    m_controlsWidget = new QWidget(this);
    
    // Enable mouse tracking on controls widget to detect mouse movement
    m_controlsWidget->setMouseTracking(true);
    
    // Install event filter on controls widget for mouse tracking
    m_controlsWidget->installEventFilter(this);
    
    qDebug() << "VP_Shows_Videoplayer: Mouse tracking enabled on controls widget";
    
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
    m_sliderLayout->addWidget(m_volumeLabel);
    m_sliderLayout->addWidget(m_volumeSlider);
    m_sliderLayout->addSpacing(20);
    m_sliderLayout->addWidget(m_speedLabel);
    m_sliderLayout->addWidget(m_speedComboBox);
    
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

void VP_Shows_Videoplayer::connectSignals()
{
    qDebug() << "VP_Shows_Videoplayer: Connecting signals";
    
    // Button signals
    connect(m_playButton, &QPushButton::clicked,
            this, &VP_Shows_Videoplayer::on_playButton_clicked);
    
    connect(m_stopButton, &QPushButton::clicked,
            this, &VP_Shows_Videoplayer::stop);
    
    connect(m_fullScreenButton, &QPushButton::clicked,
            this, &VP_Shows_Videoplayer::on_fullScreenButton_clicked);
    
    // Slider signals
    connect(m_positionSlider, &QSlider::sliderMoved,
            this, &VP_Shows_Videoplayer::on_positionSlider_sliderMoved);
    
    connect(m_positionSlider, &QSlider::sliderPressed,
            this, &VP_Shows_Videoplayer::on_positionSlider_sliderPressed);
    
    connect(m_positionSlider, &QSlider::sliderReleased,
            this, &VP_Shows_Videoplayer::on_positionSlider_sliderReleased);
    
    // Also connect valueChanged to catch direct clicks that don't trigger sliderMoved
    connect(m_positionSlider, &QSlider::valueChanged, this, [this](int value) {
        // Only handle if slider is not being actively moved and this is a significant change
        if (!m_isSliderBeingMoved && qAbs(value - m_mediaPlayer->position()) > 1000) {
            qDebug() << "VP_Shows_Videoplayer: Slider value changed without move - syncing position to" << value;
            setPosition(value);
        }
    });
    
    connect(m_volumeSlider, &QSlider::sliderMoved,
            this, &VP_Shows_Videoplayer::on_volumeSlider_sliderMoved);
    
    // Speed combo box signal
    connect(m_speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VP_Shows_Videoplayer::on_speedComboBox_currentIndexChanged);
    
    // VLC player signals
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::positionChanged,
            this, &VP_Shows_Videoplayer::updatePosition);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::durationChanged,
            this, &VP_Shows_Videoplayer::updateDuration);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::stateChanged,
            this, &VP_Shows_Videoplayer::handlePlaybackStateChanged);
    
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::errorOccurred,
            this, &VP_Shows_Videoplayer::handleError);
    
    // Connect finished signal for end of playback
    connect(m_mediaPlayer.get(), &VP_VLCPlayer::finished,
            this, [this]() {
                qDebug() << "VP_Shows_Videoplayer: Playback finished";
                emit finished();  // Forward the finished signal
            });
    
    // Note: Double-click is handled through Qt's event filter since we disabled libvlc's input handling
}

bool VP_Shows_Videoplayer::loadVideo(const QString& filePath)
{
    qDebug() << "VP_Shows_Videoplayer: Loading video:" << filePath;
    
    // Reset the closing flag when loading a new video (fixes auto-close on subsequent plays)
    m_isClosing = false;
    m_hasStartedPlaying = false;
    m_playbackStartedEmitted = false;  // Reset playback started flag
    qDebug() << "VP_Shows_Videoplayer: Reset flags for new video load";
    
    // Handle empty path to clear the player
    if (filePath.isEmpty()) {
        qDebug() << "VP_Shows_Videoplayer: Clearing media player";
        m_mediaPlayer->stop();
        m_mediaPlayer->unloadMedia();
        m_currentVideoPath.clear();
        return true;
    }
    
    // Check if file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qDebug() << "VP_Shows_Videoplayer: File does not exist:" << filePath;
        emit errorOccurred(tr("Video file does not exist: %1").arg(filePath));
        return false;
    }
    
    // Stop current playback if any
    if (m_mediaPlayer->isPlaying()) {
        m_mediaPlayer->stop();
    }
    
    // Load the media with VLC
    if (!m_mediaPlayer->loadMedia(filePath)) {
        qDebug() << "VP_Shows_Videoplayer: Failed to load media with VLC";
        emit errorOccurred(tr("Failed to load video: %1").arg(m_mediaPlayer->lastError()));
        return false;
    }
    
    // Store the media path
    m_currentVideoPath = filePath;
    
    // Force video widget to update
    m_videoWidget->update();
    m_videoWidget->show();
    
    // Process events to ensure rendering
    QApplication::processEvents();
    
    // Update window title
    setWindowTitle(tr("Video Player - %1").arg(fileInfo.fileName()));
    
    // Ensure the widget has focus for keyboard input
    setFocus();
    
    qDebug() << "VP_Shows_Videoplayer: Video loaded successfully";
    return true;
}

void VP_Shows_Videoplayer::play()
{
    qDebug() << "VP_Shows_Videoplayer: Play requested";
    
    if (m_currentVideoPath.isEmpty()) {
        qDebug() << "VP_Shows_Videoplayer: No video loaded";
        emit errorOccurred(tr("No video loaded"));
        return;
    }
    
    // Mark that playback has started (for the aboutToClose signal)
    if (!m_hasStartedPlaying) {
        m_hasStartedPlaying = true;
        // Note: Watch history is managed by Operations_VP_Shows_WatchHistory
        // We don't use the internal m_watchHistory
    }
    
    m_mediaPlayer->play();
    
    // Note: Progress timer is not needed since Operations_VP_Shows_WatchHistory handles it
    
    // Ensure we have focus for keyboard shortcuts
    setFocus();
}

void VP_Shows_Videoplayer::pause()
{
    qDebug() << "VP_Shows_Videoplayer: Pause requested";
    m_mediaPlayer->pause();
    
    // Note: Watch history is managed by Operations_VP_Shows_WatchHistory
    // We don't save progress here
}

void VP_Shows_Videoplayer::stop()
{
    qDebug() << "VP_Shows_Videoplayer: Stop requested";
    
    // Note: Watch history is managed by Operations_VP_Shows_WatchHistory
    // We don't save progress here
    
    m_mediaPlayer->stop();
    m_positionSlider->setValue(0);
    m_positionLabel->setText("00:00");
    
    // QUICK FIX: The handlePlaybackStateChanged will close the player when it enters StoppedState
    // This prevents crash from temp file being deleted
    qDebug() << "VP_Shows_Videoplayer: Stop will trigger player close via state change handler";
}

void VP_Shows_Videoplayer::setVolume(int volume)
{
    qDebug() << "VP_Shows_Videoplayer: Setting volume to" << volume << "%";
    
    // VLC volume is 0-100 but we allow up to 150% in UI
    m_mediaPlayer->setVolume(volume);
    
    // Update slider if it's not at the right position
    if (m_volumeSlider->value() != volume) {
        m_volumeSlider->setValue(volume);
    }
    
    // Always show volume percentage
    m_volumeLabel->setText(tr("Vol (%1%):").arg(volume));
    
    // Remember volume for next video
    s_lastVolume = volume;
    
    emit volumeChanged(volume);
}

void VP_Shows_Videoplayer::setPosition(qint64 position)
{
    qDebug() << "VP_Shows_Videoplayer: Setting position to" << position;
    
    // Validate position is within valid range
    qint64 duration = m_mediaPlayer->duration();
    if (duration > 0) {
        position = qBound(qint64(0), position, duration);
        if (position < 0) {
            qDebug() << "VP_Shows_Videoplayer: WARNING - Attempted to set negative position, clamping to 0";
            position = 0;
        }
    }
    
    m_mediaPlayer->setPosition(position);
    
    // Force update the slider position when manually setting position (e.g., for resume)
    forceUpdateSliderPosition(position);
}

void VP_Shows_Videoplayer::setPlaybackSpeed(qreal speed)
{
    qDebug() << "VP_Shows_Videoplayer: Setting playback speed to" << speed;
    
    // Clamp speed to reasonable values
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;
    
    m_mediaPlayer->setPlaybackRate(static_cast<float>(speed));
    
    // Update combo box if needed
    int index = -1;
    for (int i = 0; i < m_speedComboBox->count(); ++i) {
        if (qFuzzyCompare(m_speedComboBox->itemData(i).toDouble(), speed)) {
            index = i;
            break;
        }
    }
    
    if (index >= 0 && m_speedComboBox->currentIndex() != index) {
        m_speedComboBox->blockSignals(true);
        m_speedComboBox->setCurrentIndex(index);
        m_speedComboBox->blockSignals(false);
    }
    
    emit playbackSpeedChanged(speed);
}

qreal VP_Shows_Videoplayer::playbackSpeed() const
{
    return static_cast<qreal>(m_mediaPlayer->playbackRate());
}

bool VP_Shows_Videoplayer::isPlaying() const
{
    return m_mediaPlayer->isPlaying();
}

bool VP_Shows_Videoplayer::isPaused() const
{
    return m_mediaPlayer->isPaused();
}

qint64 VP_Shows_Videoplayer::duration() const
{
    return m_mediaPlayer->duration();
}

qint64 VP_Shows_Videoplayer::position() const
{
    return m_mediaPlayer->position();
}

int VP_Shows_Videoplayer::volume() const
{
    return m_mediaPlayer->volume();
}

QString VP_Shows_Videoplayer::currentVideoPath() const
{
    return m_currentVideoPath;
}

void VP_Shows_Videoplayer::on_playButton_clicked()
{
    qDebug() << "VP_Shows_Videoplayer: Play button clicked";
    
    if (m_mediaPlayer->isPlaying()) {
        pause();
    } else {
        play();
    }
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void VP_Shows_Videoplayer::on_positionSlider_sliderMoved(int position)
{
    qDebug() << "VP_Shows_Videoplayer: Position slider moved to" << position;
    
    // Additional debug info in fullscreen
    if (m_isFullScreen) {
        qDebug() << "VP_Shows_Videoplayer: [FULLSCREEN] Slider moved to position:" << position;
        qDebug() << "VP_Shows_Videoplayer: [FULLSCREEN] Slider max:" << m_positionSlider->maximum();
        qDebug() << "VP_Shows_Videoplayer: [FULLSCREEN] Media duration:" << m_mediaPlayer->duration();
    }
    
    // Check if we're near the end of the video
    qint64 duration = m_mediaPlayer->duration();
    if (duration > 0) {
        qint64 remaining = duration - position;
        if (remaining <= 120000) { // Within 2 minutes of end
            qDebug() << "VP_Shows_Videoplayer: Slider moved to near end position - remaining:" << remaining << "ms";
        }
    }
    
    // Validate position before setting
    if (position < 0) {
        qDebug() << "VP_Shows_Videoplayer: WARNING - Slider moved to negative position:" << position << "- clamping to 0";
        position = 0;
    }
    
    setPosition(position);
}

void VP_Shows_Videoplayer::on_positionSlider_sliderPressed()
{
    qDebug() << "VP_Shows_Videoplayer: Position slider pressed";
    m_isSliderBeingMoved = true;
    
    // Store the current position to detect if we're near the end
    qint64 currentPos = m_positionSlider->value();
    qint64 duration = m_mediaPlayer->duration();
    if (duration > 0) {
        qint64 remaining = duration - currentPos;
        if (remaining <= 120000) { // Within 2 minutes of end
            qDebug() << "VP_Shows_Videoplayer: Slider pressed near end - remaining:" << remaining << "ms";
        }
    }
}

void VP_Shows_Videoplayer::on_positionSlider_sliderReleased()
{
    qDebug() << "VP_Shows_Videoplayer: Position slider released";
    m_isSliderBeingMoved = false;
    
    // Check if we're near the end of the video
    qint64 currentPos = m_positionSlider->value();
    qint64 duration = m_mediaPlayer->duration();
    bool nearEnd = false;
    
    if (duration > 0) {
        qint64 remaining = duration - currentPos;
        nearEnd = (remaining <= 120000); // Within 2 minutes of end
        if (nearEnd) {
            qDebug() << "VP_Shows_Videoplayer: Slider released near end - forcing immediate focus restore";
            // Immediately restore focus when near end to prevent focus issues
            ensureKeyboardFocus();
            return;
        }
    }
    
    // Normal case: Return focus to main widget for keyboard shortcuts after a small delay
    // This prevents focus fighting while user is still interacting
    QTimer::singleShot(100, this, &VP_Shows_Videoplayer::ensureKeyboardFocus);
}

void VP_Shows_Videoplayer::on_volumeSlider_sliderMoved(int position)
{
    qDebug() << "VP_Shows_Videoplayer: Volume slider moved to" << position << "%";
    setVolume(position);
    // Don't steal focus while user is adjusting volume
}

void VP_Shows_Videoplayer::on_speedComboBox_currentIndexChanged(int index)
{
    if (index < 0 || index >= m_speedComboBox->count()) {
        return;
    }
    
    qreal speed = m_speedComboBox->itemData(index).toDouble();
    qDebug() << "VP_Shows_Videoplayer: Speed combo box changed to index" << index << "speed" << speed;
    
    setPlaybackSpeed(speed);
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void VP_Shows_Videoplayer::on_fullScreenButton_clicked()
{
    qDebug() << "VP_Shows_Videoplayer: Fullscreen button clicked";
    toggleFullScreen();
    
    // Return focus to main widget for keyboard shortcuts
    ensureKeyboardFocus();
}

void VP_Shows_Videoplayer::updatePosition(qint64 position)
{
    if (!m_isSliderBeingMoved) {
        m_positionSlider->setValue(static_cast<int>(position));
    } else {
        // Log when we're skipping slider update due to user interaction
        static qint64 lastLogTime = 0;
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - lastLogTime > 1000) { // Log at most once per second
            qDebug() << "VP_Shows_Videoplayer: Skipping slider update - user is moving slider";
            lastLogTime = currentTime;
        }
    }
    m_positionLabel->setText(formatTime(position));
    emit positionChanged(position);
}

void VP_Shows_Videoplayer::forceUpdateSliderPosition(qint64 position)
{
    qDebug() << "VP_Shows_Videoplayer: Force updating slider position to" << position;
    // Temporarily disable the flag to force update
    bool wasBeingMoved = m_isSliderBeingMoved;
    m_isSliderBeingMoved = false;
    
    // Update the slider
    m_positionSlider->setValue(static_cast<int>(position));
    m_positionLabel->setText(formatTime(position));
    
    // Restore the flag
    m_isSliderBeingMoved = wasBeingMoved;
}

void VP_Shows_Videoplayer::updateDuration(qint64 duration)
{
    qDebug() << "VP_Shows_Videoplayer: Duration changed to" << duration;
    
    // Check if duration would overflow int
    if (duration > INT_MAX) {
        qDebug() << "VP_Shows_Videoplayer: WARNING - Duration exceeds INT_MAX, clamping to INT_MAX";
        duration = INT_MAX;
    }
    
    m_positionSlider->setRange(0, static_cast<int>(duration));
    m_durationLabel->setText(formatTime(duration));
    emit durationChanged(duration);
    
    qDebug() << "VP_Shows_Videoplayer: Slider range set to 0 -" << m_positionSlider->maximum();
}

void VP_Shows_Videoplayer::handleError(const QString &errorString)
{
    qDebug() << "VP_Shows_Videoplayer: Error occurred:" << errorString;
    
    QString errorMessage = tr("Playback Error: %1").arg(errorString);
    emit errorOccurred(errorMessage);
}

void VP_Shows_Videoplayer::handlePlaybackStateChanged(VP_VLCPlayer::PlayerState state)
{
    qDebug() << "VP_Shows_Videoplayer: Playback state changed to" << static_cast<int>(state);
    
    switch(state) {
        case VP_VLCPlayer::PlayerState::Playing:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
            m_playButton->setToolTip(tr("Pause"));
            emit playbackStateChanged(state);
            // Emit playbackStarted signal only once per play session
            if (!m_playbackStartedEmitted) {
                m_playbackStartedEmitted = true;
                qDebug() << "VP_Shows_Videoplayer: Emitting playbackStarted signal";
                emit playbackStarted();
            }
            break;
            
        case VP_VLCPlayer::PlayerState::Paused:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_playButton->setToolTip(tr("Play"));
            emit playbackStateChanged(state);
            break;
            
        case VP_VLCPlayer::PlayerState::Stopped:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_playButton->setToolTip(tr("Play"));
            m_playbackStartedEmitted = false;  // Reset for next play
            
            // QUICK FIX: Close the player when playback stops to prevent crash
            // This happens because the temp decrypted file gets deleted by async cleanup
            // TODO: Implement proper fix to handle temp file lifecycle better
            qDebug() << "VP_Shows_Videoplayer: Playback stopped, checking if should close. m_isClosing =" << m_isClosing;
            
            // Don't close if we're already closing (to prevent recursive close)
            if (!m_isClosing) {
                qDebug() << "VP_Shows_Videoplayer: Scheduling close via timer";
                // Mark that we're scheduling a close to prevent multiple timers
                m_isClosing = true;
                // Use a timer to close the window to allow this event to complete
                QTimer::singleShot(100, this, &QWidget::close);
            } else {
                qDebug() << "VP_Shows_Videoplayer: Already closing, skipping auto-close";
            }
            
            emit playbackStateChanged(state);
            break;
            
        case VP_VLCPlayer::PlayerState::Buffering:
            // Show buffering state if needed
            qDebug() << "VP_Shows_Videoplayer: Buffering...";
            break;
            
        case VP_VLCPlayer::PlayerState::Error:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_playButton->setToolTip(tr("Play"));
            emit errorOccurred(m_mediaPlayer->lastError());
            emit playbackStateChanged(state);
            break;
    }
}

QString VP_Shows_Videoplayer::formatTime(qint64 milliseconds) const
{
    // Handle negative values (which shouldn't happen but do in the bug)
    bool isNegative = milliseconds < 0;
    if (isNegative) {
        milliseconds = -milliseconds;
        qDebug() << "VP_Shows_Videoplayer: WARNING - formatTime called with negative value:" << -milliseconds;
    }
    
    int seconds = static_cast<int>(milliseconds / 1000);
    int minutes = seconds / 60;
    int hours = minutes / 60;
    
    seconds %= 60;
    minutes %= 60;
    
    QString timeStr;
    if (hours > 0) {
        timeStr = QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    } else {
        timeStr = QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    
    // Prepend minus sign if it was negative
    if (isNegative) {
        return QString("-%1").arg(timeStr);
    }
    
    return timeStr;
}

void VP_Shows_Videoplayer::startInFullScreen()
{
    qDebug() << "VP_Shows_Videoplayer: startInFullScreen called";
    
    // Only auto-fullscreen if we don't have stored settings
    // If we have stored settings, the showEvent will handle restoration
    if (s_hasStoredSettings) {
        qDebug() << "VP_Shows_Videoplayer: Skipping auto-fullscreen - using stored window state";
        return;
    }
    
    qDebug() << "VP_Shows_Videoplayer: Starting in fullscreen mode (no stored settings)";
    
    if (!m_isFullScreen) {
        // Store the normal geometry before going fullscreen
        m_normalGeometry = QRect(pos(), size());
        
        // Hide controls initially in fullscreen
        m_controlsWidget->setVisible(false);
        
        // Remove all margins for fullscreen
        m_mainLayout->setContentsMargins(0, 0, 0, 0);
        
        // Get the screen the player is currently on BEFORE changing window flags
        QScreen *screen = getCurrentScreen();
        if (!screen) {
            screen = QApplication::primaryScreen();
            qDebug() << "VP_Shows_Videoplayer: Fallback to primary screen";
        }
        
        if (screen) {
            // Remember this screen for when we close
            s_lastUsedScreen = screen;
            qDebug() << "VP_Shows_Videoplayer: Going fullscreen on screen:" << screen->name();
            
            // Move window to the target screen BEFORE going fullscreen
            // This ensures fullscreen happens on the correct monitor
            QRect screenGeometry = screen->geometry();
            move(screenGeometry.topLeft());
        }
        
        // Remove window frame for true fullscreen
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        
        // Now show fullscreen - it will use the screen we just moved to
        showFullScreen();
        m_isFullScreen = true;
        
        // Set geometry to fill the entire screen
        if (screen) {
            setGeometry(screen->geometry());
        }
        
        // Update button icon
        m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        m_fullScreenButton->setToolTip(tr("Exit Full Screen (ESC)"));
        
        emit fullScreenChanged(true);
        
        // Show controls briefly then hide them
        m_controlsWidget->setVisible(true);
        QTimer::singleShot(2000, [this]() {
            if (m_isFullScreen && !m_controlsWidget->underMouse()) {
                m_controlsWidget->setVisible(false);
            }
        });
        
        // Start cursor timer for auto-hide and mouse check timer
        startCursorTimer();
        
        // Start mouse movement check timer
        if (m_mouseCheckTimer) {
            m_mouseCheckTimer->start();
            qDebug() << "VP_Shows_Videoplayer: Started mouse movement check timer";
        }
        
        // Ensure we have focus for keyboard input
        setFocus();
        raise();
        activateWindow();
    }
}

// Event handlers and other essential methods

void VP_Shows_Videoplayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Window closing, stopping playback. m_isClosing =" << m_isClosing;
    
    // Remember the screen we're closing on (stored in RAM via static variable)
    QScreen* currentScreen = getCurrentScreen();
    if (currentScreen) {
        s_lastUsedScreen = currentScreen;
        qDebug() << "VP_Shows_Videoplayer: Remembering last used screen:" << currentScreen->name();
    }
    
    // Save window state for autoplay restoration
    s_wasFullScreen = m_isFullScreen;
    s_wasMaximized = isMaximized();
    s_wasMinimized = isMinimized();
    
    // Save window geometry
    if (m_isFullScreen) {
        // If fullscreen, save the normal geometry from before fullscreen
        s_lastWindowGeometry = m_normalGeometry;
    } else if (isMinimized()) {
        // If minimized, we need to get the normal geometry (what it would be when restored)
        // Qt doesn't provide this directly when minimized, so use normalGeometry if we have it
        if (!m_normalGeometry.isNull()) {
            s_lastWindowGeometry = m_normalGeometry;
        } else {
            // Fallback: save a default size
            s_lastWindowGeometry = QRect(100, 100, 800, 600);
        }
    } else if (isMaximized()) {
        // If maximized, save the normal geometry from before maximize
        // We'll rely on Qt to restore the proper normal size when un-maximizing
        s_lastWindowGeometry = normalGeometry();
    } else {
        // Normal window state - save current geometry
        s_lastWindowGeometry = geometry();
        // Also update m_normalGeometry for future use
        m_normalGeometry = geometry();
    }
    
    // Mark that we have stored settings
    s_hasStoredSettings = true;
    
    qDebug() << "VP_Shows_Videoplayer: Saved window state - Fullscreen:" << s_wasFullScreen 
             << "Maximized:" << s_wasMaximized << "Minimized:" << s_wasMinimized
             << "Geometry:" << s_lastWindowGeometry << "Volume:" << s_lastVolume;
    
    // The m_isClosing flag is already set by handlePlaybackStateChanged if this is an auto-close
    // We don't need to set it here anymore
    
    // CRITICAL: Capture the current position BEFORE stopping the player
    qint64 finalPosition = 0;
    if (m_mediaPlayer) {
        finalPosition = m_mediaPlayer->position();
        qDebug() << "VP_Shows_Videoplayer: Captured final position before stop:" << finalPosition << "ms";
    }
    
    // IMPORTANT: We're NOT using the internal m_watchHistory here
    // The watch history is managed by Operations_VP_Shows_WatchHistory
    // Just emit a signal with the final position so it can be captured
    if (finalPosition > 0 && m_hasStartedPlaying) {
        qDebug() << "VP_Shows_Videoplayer: Emitting aboutToClose signal with position:" << finalPosition << "ms";
        emit aboutToClose(finalPosition);
    } else if (m_hasStartedPlaying) {
        qDebug() << "VP_Shows_Videoplayer: Player at position 0, still emitting aboutToClose";
        emit aboutToClose(0);
    }
    
    // Small delay to allow watch history to save
    QApplication::processEvents();
    
    // NOW stop playback and clear media source to release file handle
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        // Clear the media to release file handle
        m_mediaPlayer->unloadMedia();
    }
    
    // Clear current video path
    m_currentVideoPath.clear();
    
    // Exit fullscreen if active
    if (m_isFullScreen) {
        exitFullScreen();
    }
    
    // Reset state
    m_hasStartedPlaying = false;
    m_lastSavedPosition = 0;
    
    event->accept();
}

void VP_Shows_Videoplayer::showEvent(QShowEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Window shown, setting focus";
    
    // Reset the closing flag when window is shown (for reuse scenarios)
    m_isClosing = false;
    qDebug() << "VP_Shows_Videoplayer: Reset closing flag on show";
    
    // Determine which screen to use
    QScreen* screenToUse = nullptr;
    
    // Priority: 1) Last used screen (if available), 2) Target screen, 3) Current mouse screen
    if (s_lastUsedScreen && QGuiApplication::screens().contains(s_lastUsedScreen)) {
        screenToUse = s_lastUsedScreen;
        qDebug() << "VP_Shows_Videoplayer: Using last remembered screen:" << screenToUse->name();
    } else if (m_targetScreen && QGuiApplication::screens().contains(m_targetScreen)) {
        screenToUse = m_targetScreen;
        qDebug() << "VP_Shows_Videoplayer: Using target screen (no remembered screen):" << screenToUse->name();
    } else {
        // Fallback to screen at mouse position for first-time launch
        screenToUse = QGuiApplication::screenAt(QCursor::pos());
        if (!screenToUse) {
            screenToUse = QGuiApplication::primaryScreen();
        }
        qDebug() << "VP_Shows_Videoplayer: Using screen at mouse position:" << (screenToUse ? screenToUse->name() : "primary");
    }
    
    // Restore window state if we have stored settings (from autoplay)
    if (s_hasStoredSettings) {
        qDebug() << "VP_Shows_Videoplayer: Restoring saved window state";
        
        // IMPORTANT: Clear the flag immediately to prevent re-applying settings on subsequent show events
        // (like when user manually restores a minimized window)
        s_hasStoredSettings = false;
        qDebug() << "VP_Shows_Videoplayer: Cleared stored settings flag to prevent re-application";
        
        // Restore geometry first (unless we're going to fullscreen/maximize/minimize)
        if (!s_lastWindowGeometry.isNull() && !s_wasFullScreen && !s_wasMaximized && !s_wasMinimized) {
            setGeometry(s_lastWindowGeometry);
            qDebug() << "VP_Shows_Videoplayer: Restored geometry:" << s_lastWindowGeometry;
        }
        
        // Handle fullscreen restoration
        if (s_wasFullScreen) {
            qDebug() << "VP_Shows_Videoplayer: Restoring fullscreen state";
            // Store the normal geometry before fullscreen
            if (!s_lastWindowGeometry.isNull()) {
                m_normalGeometry = s_lastWindowGeometry;
            }
            // Use QTimer to ensure window is fully shown before going fullscreen
            QTimer::singleShot(100, this, [this]() {
                if (!m_isFullScreen) {
                    toggleFullScreen();
                }
            });
        }
        // Handle maximized restoration
        else if (s_wasMaximized) {
            qDebug() << "VP_Shows_Videoplayer: Restoring maximized state";
            showMaximized();
        }
        // Handle minimized restoration - show normally first, then minimize
        else if (s_wasMinimized) {
            qDebug() << "VP_Shows_Videoplayer: Restoring minimized state";
            // First, restore the normal geometry so it's correct when un-minimized
            if (!s_lastWindowGeometry.isNull()) {
                setGeometry(s_lastWindowGeometry);
            }
            // Show the window normally first
            show();
            // Then minimize it after a short delay to ensure proper state
            QTimer::singleShot(100, this, [this]() {
                qDebug() << "VP_Shows_Videoplayer: Minimizing window after show";
                showMinimized();
                // Ensure the window can be restored by setting proper flags
                setWindowState(windowState() | Qt::WindowMinimized);
                // Clear the minimized flag AFTER actually minimizing
                s_wasMinimized = false;
                qDebug() << "VP_Shows_Videoplayer: Cleared minimized flag after applying it";
            });
        }
        
        // Clear non-minimized window state flags after applying them
        // (minimized flag is cleared in its timer callback above)
        s_wasFullScreen = false;
        s_wasMaximized = false;
        // Note: s_wasMinimized is cleared in the timer callback above
        qDebug() << "VP_Shows_Videoplayer: Cleared fullscreen and maximized flags after restoration";
    }
    // No stored settings - use default positioning
    else {
        // Position on the chosen screen if not fullscreen
        if (screenToUse && !m_isFullScreen) {
            QRect screenGeometry = screenToUse->availableGeometry();
            // Center on the chosen screen
            move(screenGeometry.center() - rect().center());
            qDebug() << "VP_Shows_Videoplayer: Positioned on screen (no stored settings):" << screenToUse->name();
        }
    }
    
    // Ensure the widget has focus when shown
    setFocus();
    raise();
    activateWindow();
    
    QWidget::showEvent(event);
}

void VP_Shows_Videoplayer::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Key pressed:" << event->key();
    
    // Check if we have focus - if not, try to reclaim it
    if (!hasFocus()) {
        qDebug() << "VP_Shows_Videoplayer: Main widget doesn't have focus during keypress - attempting to reclaim";
        // Clear the slider flag in case it's stuck
        m_isSliderBeingMoved = false;
        // Try to reclaim focus
        ensureKeyboardFocus();
    }
    
    switch(event->key()) {
        case Qt::Key_Escape:
            if (m_isFullScreen) {
                exitFullScreen();
            }
            break;
            
        case Qt::Key_F11:
            toggleFullScreen();
            break;
            
        case Qt::Key_Space:
            on_playButton_clicked();
            break;
            
        case Qt::Key_Left:
            {
                // Clear slider flag to ensure position updates work
                m_isSliderBeingMoved = false;
                // Seek backward 10 seconds
                qint64 currentPos = m_mediaPlayer->position();
                qint64 newPos = qMax(qint64(0), currentPos - 10000);
                qDebug() << "VP_Shows_Videoplayer: Seeking backward from" << currentPos << "to" << newPos;
                m_mediaPlayer->setPosition(newPos);
                // Force slider update
                forceUpdateSliderPosition(newPos);
            }
            break;
            
        case Qt::Key_Right:
            {
                // Clear slider flag to ensure position updates work
                m_isSliderBeingMoved = false;
                // Seek forward 10 seconds
                qint64 currentPos = m_mediaPlayer->position();
                qint64 newPos = qMin(m_mediaPlayer->duration(), currentPos + 10000);
                qDebug() << "VP_Shows_Videoplayer: Seeking forward from" << currentPos << "to" << newPos;
                m_mediaPlayer->setPosition(newPos);
                // Force slider update
                forceUpdateSliderPosition(newPos);
            }
            break;
            
        case Qt::Key_Up:
            // Increase volume by 5%
            {
                int currentVolume = m_volumeSlider->value();
                int newVolume = qMin(150, currentVolume + 5);
                setVolume(newVolume);
                qDebug() << "VP_Shows_Videoplayer: Key_Up - Volume changed from" << currentVolume << "to" << newVolume;
            }
            break;
            
        case Qt::Key_Down:
            // Decrease volume by 5%
            {
                int currentVolume = m_volumeSlider->value();
                int newVolume = qMax(0, currentVolume - 5);
                setVolume(newVolume);
                qDebug() << "VP_Shows_Videoplayer: Key_Down - Volume changed from" << currentVolume << "to" << newVolume;
            }
            break;
            
        default:
            QWidget::keyPressEvent(event);
    }
    
    // Accept the event to prevent it from being propagated
    event->accept();
}

// Functions were missing when the code was refactored

void VP_Shows_Videoplayer::startCursorTimer()
{
    if (m_isFullScreen && m_cursorTimer) {
        qDebug() << "VP_Shows_Videoplayer: Starting cursor timer for auto-hide";
        m_cursorTimer->stop();
        m_cursorTimer->start();

        // Also record current mouse position
        m_lastMousePos = QCursor::pos();
    }
}

void VP_Shows_Videoplayer::stopCursorTimer()
{
    if (m_cursorTimer) {
        qDebug() << "VP_Shows_Videoplayer: Stopping cursor timer";
        m_cursorTimer->stop();
    }
    if (m_mouseCheckTimer) {
        qDebug() << "VP_Shows_Videoplayer: Stopping mouse check timer";
        m_mouseCheckTimer->stop();
    }
}

void VP_Shows_Videoplayer::hideCursor()
{
    if (m_isFullScreen && !m_controlsWidget->underMouse()) {
        // Always hide the control bar in fullscreen mode when timer triggers
        qDebug() << "VP_Shows_Videoplayer: Hiding controls";
        m_controlsWidget->setVisible(false);
        
        // Check if the mouse cursor is actually within the video player window
        QPoint globalMousePos = QCursor::pos();
        QRect playerGlobalRect = geometry();
        
        // Convert to global coordinates if we have a parent
        if (!isWindow()) {
            playerGlobalRect.moveTopLeft(mapToGlobal(QPoint(0, 0)));
        }
        
        // Only hide cursor if it's within the video player window
        if (playerGlobalRect.contains(globalMousePos)) {
            qDebug() << "VP_Shows_Videoplayer: Hiding cursor (cursor is within player window)";

            // Hide cursor only on video player widgets, not globally
            setCursor(Qt::BlankCursor);
            m_videoWidget->setCursor(Qt::BlankCursor);
            m_controlsWidget->setCursor(Qt::BlankCursor);
            
            // Do NOT use QApplication::setOverrideCursor as it affects the entire application
        } else {
            qDebug() << "VP_Shows_Videoplayer: Not hiding cursor (cursor is outside player window)";
        }
    }
}

void VP_Shows_Videoplayer::showCursor()
{
    // Only show if cursor is hidden
    if (cursor().shape() == Qt::BlankCursor) {
        qDebug() << "VP_Shows_Videoplayer: Showing cursor";

        // Restore cursor only on video player widgets
        setCursor(Qt::ArrowCursor);
        m_videoWidget->setCursor(Qt::ArrowCursor);
        m_controlsWidget->setCursor(Qt::ArrowCursor);
        
        // No need to deal with override cursors since we're not using them anymore
    }
}

QScreen* VP_Shows_Videoplayer::getCurrentScreen() const
{
    // Get the screen this window is currently on
    if (windowHandle() && windowHandle()->screen()) {
        return windowHandle()->screen();
    }

    // Fallback: get screen based on geometry
    QPoint center = geometry().center();
    if (parentWidget()) {
        center = mapToGlobal(center);
    }

    return QGuiApplication::screenAt(center);
}

void VP_Shows_Videoplayer::checkMouseMovement()
{
    if (!m_isFullScreen) {
        return;  // Only check in fullscreen mode
    }

    QPoint currentPos = QCursor::pos();

    // Check if mouse has moved
    if (currentPos != m_lastMousePos) {
        // Get the screen the player is currently on
        QScreen* playerScreen = getCurrentScreen();

        // Get the screen the mouse is currently on
        QScreen* mouseScreen = QGuiApplication::screenAt(currentPos);

        // Only respond if the mouse is on the same screen as the player
        if (playerScreen && mouseScreen && playerScreen == mouseScreen) {
            // Mouse has moved on the same screen
            qDebug() << "VP_Shows_Videoplayer: Mouse movement detected on player screen - from"
                     << m_lastMousePos << "to" << currentPos;

            // Show cursor and controls
            showCursor();

            if (!m_controlsWidget->isVisible()) {
                qDebug() << "VP_Shows_Videoplayer: Showing controls due to detected movement on same screen";
                m_controlsWidget->setVisible(true);
            }

            // Restart the hide timer
            startCursorTimer();
        } else {
            // Mouse is on a different screen, do not show controls
            qDebug() << "VP_Shows_Videoplayer: Mouse movement on different screen - ignoring";
        }

        // Always update last position to track movement
        m_lastMousePos = currentPos;
    }
}

void VP_Shows_Videoplayer::saveWatchProgress()
{
    if (!m_watchHistory || m_episodePath.isEmpty() || !m_hasStartedPlaying) {
        return;
    }

    qint64 currentPosition = m_mediaPlayer->position();
    qint64 duration = m_mediaPlayer->duration();

    // Only save if position has changed significantly (more than 1 second)
    if (!shouldUpdateProgress(currentPosition)) {
        return;
    }

    qDebug() << "VP_Shows_Videoplayer: Saving watch progress - Position:" << currentPosition
             << "Duration:" << duration;

    m_watchHistory->updateWatchProgress(m_episodePath, currentPosition, duration, m_episodeIdentifier);
    m_watchHistory->saveHistory();
    m_lastSavedPosition = currentPosition;
}

void VP_Shows_Videoplayer::ensureKeyboardFocus()
{
    qDebug() << "VP_Shows_Videoplayer: Ensuring keyboard focus";

    // First, clear focus from any control widgets
    if (m_positionSlider->hasFocus()) {
        qDebug() << "VP_Shows_Videoplayer: Clearing focus from position slider";
        m_positionSlider->clearFocus();
    }
    if (m_volumeSlider->hasFocus()) {
        qDebug() << "VP_Shows_Videoplayer: Clearing focus from volume slider";
        m_volumeSlider->clearFocus();
    }

    // Set focus to the main widget to ensure keyboard shortcuts work
    setFocus(Qt::OtherFocusReason);

    // Also raise and activate the window to be safe
    raise();
    activateWindow();

    // Verify focus was set
    if (hasFocus()) {
        qDebug() << "VP_Shows_Videoplayer: Focus successfully set to main widget";
    } else {
        qDebug() << "VP_Shows_Videoplayer: WARNING - Failed to set focus to main widget";
    }
}

void VP_Shows_Videoplayer::focusInEvent(QFocusEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Focus in event, reason:" << event->reason();
    QWidget::focusInEvent(event);
}

void VP_Shows_Videoplayer::wheelEvent(QWheelEvent *event)
{
    qDebug() << "VP_Shows_Videoplayer: Mouse wheel event detected";

    // Get the number of degrees the wheel was rotated
    QPoint numDegrees = event->angleDelta() / 8;

    if (!numDegrees.isNull()) {
        // Get the number of steps (typically 15 degrees per step)
        QPoint numSteps = numDegrees / 15;

        // Adjust volume based on vertical wheel movement
        int currentVolume = m_volumeSlider->value();
        int volumeChange = numSteps.y() * 5; // 5% per step
        int newVolume = qBound(0, currentVolume + volumeChange, 150);

        if (newVolume != currentVolume) {
            setVolume(newVolume);
            qDebug() << "VP_Shows_Videoplayer: Mouse wheel - Volume changed from" << currentVolume << "to" << newVolume;
        }
    }

    event->accept();
}

void VP_Shows_Videoplayer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isFullScreen) {
        // Update last mouse position to global position
        QPoint globalPos = mapToGlobal(event->pos());

        // Only process if position actually changed
        if (globalPos != m_lastMousePos) {
            // Get the screen the player is on and the screen the mouse is on
            QScreen* playerScreen = getCurrentScreen();
            QScreen* mouseScreen = QGuiApplication::screenAt(globalPos);

            // Only respond if mouse is on the same screen as the player
            if (playerScreen && mouseScreen && playerScreen == mouseScreen) {
                qDebug() << "VP_Shows_Videoplayer: Mouse movement detected on main widget at global position"
                         << globalPos << " on same screen";

                // Show cursor
                showCursor();

                // Show controls if hidden
                if (!m_controlsWidget->isVisible()) {
                    qDebug() << "VP_Shows_Videoplayer: Showing controls due to main widget mouse movement on same screen";
                    m_controlsWidget->setVisible(true);
                }

                // Restart the timer
                startCursorTimer();
            } else if (playerScreen && mouseScreen && playerScreen != mouseScreen) {
                // Mouse is on a different screen, log but don't show controls
                qDebug() << "VP_Shows_Videoplayer: Mouse movement on different screen (main widget) - ignoring";
            }

            // Always update last position to track movement
            m_lastMousePos = globalPos;
        }
    }

    // Call base class implementation
    QWidget::mouseMoveEvent(event);
}

bool VP_Shows_Videoplayer::eventFilter(QObject *watched, QEvent *event)
{
    // Handle double-click on video widget for fullscreen
    // This works now because we've disabled libvlc's mouse input handling
    if (watched == m_videoWidget && event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            qDebug() << "VP_Shows_Videoplayer: Double-click detected on video widget - toggling fullscreen";
            toggleFullScreen();
            return true;
        }
    }

    // Handle single click on video widget to restore focus
    if (watched == m_videoWidget && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            qDebug() << "VP_Shows_Videoplayer: Click on video widget - restoring focus and clearing slider flag";
            // Clear the slider being moved flag in case it's stuck
            m_isSliderBeingMoved = false;
            // Ensure keyboard focus returns to main widget
            ensureKeyboardFocus();
            // Don't consume the event, let it propagate
        }
    }

    // Handle mouse movement on video widget OR controls widget in fullscreen mode
    if ((watched == m_videoWidget || watched == m_controlsWidget) && m_isFullScreen && event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();

        // Only process if position actually changed
        if (globalPos != m_lastMousePos) {
            // Get the screen the player is on and the screen the mouse is on
            QScreen* playerScreen = getCurrentScreen();
            QScreen* mouseScreen = QGuiApplication::screenAt(globalPos);

            // Only respond if mouse is on the same screen as the player
            if (playerScreen && mouseScreen && playerScreen == mouseScreen) {
                qDebug() << "VP_Shows_Videoplayer: Mouse movement detected on"
                         << (watched == m_videoWidget ? "video widget" : "controls widget")
                         << "at global position" << globalPos << " on same screen";

                // Show cursor
                showCursor();

                // Show controls if hidden
                if (!m_controlsWidget->isVisible()) {
                    qDebug() << "VP_Shows_Videoplayer: Showing controls due to mouse movement on same screen";
                    m_controlsWidget->setVisible(true);
                }

                // Restart the cursor timer
                startCursorTimer();
            } else if (playerScreen && mouseScreen && playerScreen != mouseScreen) {
                // Mouse is on a different screen, log but don't show controls
                qDebug() << "VP_Shows_Videoplayer: Mouse movement on different screen - ignoring";
            }

            // Always update last position to track movement
            m_lastMousePos = globalPos;
        }

        // Don't return true here, let the event propagate
    }

    return QWidget::eventFilter(watched, event);
}

bool VP_Shows_Videoplayer::shouldUpdateProgress(qint64 currentPosition) const
{
    // Don't update if position hasn't changed significantly (at least 1 second difference)
    return qAbs(currentPosition - m_lastSavedPosition) >= 1000;
}

