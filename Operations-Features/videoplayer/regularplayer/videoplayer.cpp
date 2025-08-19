#include "videoplayer.h"
#include <QAudioOutput>
#include <QDebug>
#include <QFileInfo>
#include <QStyle>
#include <QTime>
#include <QCloseEvent>
#include <QApplication>
#include <QScreen>
#include <QTimer>

// Custom clickable slider class for seeking in video
class VideoPlayer::ClickableSlider : public QSlider
{
public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent) {}

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            // Calculate position based on click
            int value;
            if (orientation() == Qt::Horizontal) {
                value = minimum() + ((maximum() - minimum()) * event->x()) / width();
            } else {
                value = minimum() + ((maximum() - minimum()) * (height() - event->y())) / height();
            }
            
            setValue(value);
            emit sliderMoved(value);
            emit sliderPressed();
            
            // Continue with normal slider behavior
            QSlider::mousePressEvent(event);
        }
        else {
            QSlider::mousePressEvent(event);
        }
    }
};

VideoPlayer::VideoPlayer(QWidget *parent)
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
{
    qDebug() << "VideoPlayer: Constructor called";
    
    // Set window properties
    setWindowTitle(tr("Video Player"));
    resize(800, 600);
    
    // Set window flags to stay on top
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    
    // Initialize media player with audio output
    m_mediaPlayer = std::make_unique<QMediaPlayer>(this);
    QAudioOutput* audioOutput = new QAudioOutput(this);
    m_mediaPlayer->setAudioOutput(audioOutput);
    
    // Setup UI
    setupUI();
    
    // Connect signals
    connectSignals();
    
    qDebug() << "VideoPlayer: Initialization complete";
}

VideoPlayer::~VideoPlayer()
{
    qDebug() << "VideoPlayer: Destructor called";
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
}

void VideoPlayer::setupUI()
{
    qDebug() << "VideoPlayer: Setting up UI";
    
    // Create video widget
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumSize(400, 300);
    
    // Ensure video widget has a proper background
    m_videoWidget->setStyleSheet("background-color: black;");
    m_videoWidget->setAutoFillBackground(true);
    
    // Set aspect ratio mode to preserve aspect ratio while filling
    m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    
    // Set video output
    m_mediaPlayer->setVideoOutput(m_videoWidget);
    
    // Ensure video widget is visible
    m_videoWidget->show();
    
    // Install event filter on video widget for double-click fullscreen
    m_videoWidget->installEventFilter(this);
    
    // Create controls
    createControls();
    
    // Create layouts
    createLayouts();
}

VideoPlayer::ClickableSlider* VideoPlayer::createClickableSlider()
{
    return new ClickableSlider(Qt::Horizontal, this);
}

void VideoPlayer::createControls()
{
    qDebug() << "VideoPlayer: Creating controls";
    
    // Play/Pause button
    m_playButton = new QPushButton(this);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setToolTip(tr("Play"));
    
    // Stop button
    m_stopButton = new QPushButton(this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setToolTip(tr("Stop"));
    
    // Fullscreen button
    m_fullScreenButton = new QPushButton(this);
    m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_fullScreenButton->setToolTip(tr("Full Screen (F11)"));
    
    // Position slider - use custom clickable slider
    m_positionSlider = createClickableSlider();
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setToolTip(tr("Click to seek"));
    
    // Volume slider - extended range to 150%
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 150);  // Extended to 150%
    m_volumeSlider->setValue(70);
    m_volumeSlider->setMaximumWidth(100);
    m_volumeSlider->setToolTip(tr("Volume (up to 150%)"));
    
    // Labels
    m_positionLabel = new QLabel("00:00", this);
    m_positionLabel->setMinimumWidth(50);
    
    m_durationLabel = new QLabel("00:00", this);
    m_durationLabel->setMinimumWidth(50);
    
    m_volumeLabel = new QLabel(tr("Vol:"), this);
    
    // Set initial volume
    if (m_mediaPlayer->audioOutput()) {
        m_mediaPlayer->audioOutput()->setVolume(0.7f);
    }
}

void VideoPlayer::createLayouts()
{
    qDebug() << "VideoPlayer: Creating layouts";
    
    // Create a widget to hold all controls (for fullscreen mode)
    m_controlsWidget = new QWidget(this);
    
    // Control layout (buttons)
    m_controlLayout = new QHBoxLayout();
    m_controlLayout->addWidget(m_playButton);
    m_controlLayout->addWidget(m_stopButton);
    m_controlLayout->addWidget(m_fullScreenButton);
    m_controlLayout->addStretch();
    
    // Slider layout (position and volume)
    m_sliderLayout = new QHBoxLayout();
    m_sliderLayout->addWidget(m_positionLabel);
    m_sliderLayout->addWidget(m_positionSlider, 1);
    m_sliderLayout->addWidget(m_durationLabel);
    m_sliderLayout->addSpacing(20);
    m_sliderLayout->addWidget(m_volumeLabel);
    m_sliderLayout->addWidget(m_volumeSlider);
    
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

void VideoPlayer::connectSignals()
{
    qDebug() << "VideoPlayer: Connecting signals";
    
    // Button signals
    connect(m_playButton, &QPushButton::clicked,
            this, &VideoPlayer::on_playButton_clicked);
    
    connect(m_stopButton, &QPushButton::clicked,
            this, &VideoPlayer::stop);
    
    connect(m_fullScreenButton, &QPushButton::clicked,
            this, &VideoPlayer::on_fullScreenButton_clicked);
    
    // Slider signals
    connect(m_positionSlider, &QSlider::sliderMoved,
            this, &VideoPlayer::on_positionSlider_sliderMoved);
    
    connect(m_positionSlider, &QSlider::sliderPressed,
            this, &VideoPlayer::on_positionSlider_sliderPressed);
    
    connect(m_positionSlider, &QSlider::sliderReleased,
            this, &VideoPlayer::on_positionSlider_sliderReleased);
    
    connect(m_volumeSlider, &QSlider::sliderMoved,
            this, &VideoPlayer::on_volumeSlider_sliderMoved);
    
    // Media player signals
    connect(m_mediaPlayer.get(), &QMediaPlayer::positionChanged,
            this, &VideoPlayer::updatePosition);
    
    connect(m_mediaPlayer.get(), &QMediaPlayer::durationChanged,
            this, &VideoPlayer::updateDuration);
    
    connect(m_mediaPlayer.get(), &QMediaPlayer::playbackStateChanged,
            this, &VideoPlayer::handlePlaybackStateChanged);
    
    connect(m_mediaPlayer.get(), &QMediaPlayer::errorOccurred,
            this, &VideoPlayer::handleError);
}

bool VideoPlayer::loadVideo(const QString& filePath)
{
    qDebug() << "VideoPlayer: Loading video:" << filePath;
    
    // Handle empty path to clear the player
    if (filePath.isEmpty()) {
        qDebug() << "VideoPlayer: Clearing media player";
        m_mediaPlayer->stop();
        m_mediaPlayer->setSource(QUrl());
        m_mediaPlayer->setVideoOutput(nullptr);
        m_currentVideoPath.clear();
        return true;
    }
    
    // Check if file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qDebug() << "VideoPlayer: File does not exist:" << filePath;
        emit errorOccurred(tr("Video file does not exist: %1").arg(filePath));
        return false;
    }
    
    // Stop current playback
    if (m_mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        m_mediaPlayer->stop();
    }
    
    // Set the source
    m_currentVideoPath = filePath;
    m_mediaPlayer->setSource(QUrl::fromLocalFile(filePath));
    
    // Re-set video output to ensure proper rendering
    m_mediaPlayer->setVideoOutput(nullptr);
    m_mediaPlayer->setVideoOutput(m_videoWidget);
    
    // Force video widget to update
    m_videoWidget->update();
    m_videoWidget->show();
    
    // Process events to ensure rendering
    QApplication::processEvents();
    
    // Update window title
    setWindowTitle(tr("Video Player - %1").arg(fileInfo.fileName()));
    
    qDebug() << "VideoPlayer: Video loaded successfully";
    return true;
}

void VideoPlayer::play()
{
    qDebug() << "VideoPlayer: Play requested";
    
    if (m_currentVideoPath.isEmpty()) {
        qDebug() << "VideoPlayer: No video loaded";
        emit errorOccurred(tr("No video loaded"));
        return;
    }
    
    m_mediaPlayer->play();
}

void VideoPlayer::pause()
{
    qDebug() << "VideoPlayer: Pause requested";
    m_mediaPlayer->pause();
}

void VideoPlayer::stop()
{
    qDebug() << "VideoPlayer: Stop requested";
    m_mediaPlayer->stop();
    m_positionSlider->setValue(0);
    m_positionLabel->setText("00:00");
}

void VideoPlayer::setVolume(int volume)
{
    qDebug() << "VideoPlayer: Setting volume to" << volume << "%";
    
    if (m_mediaPlayer->audioOutput()) {
        // Allow volume up to 150%
        float volumeFloat = volume / 100.0f;
        m_mediaPlayer->audioOutput()->setVolume(volumeFloat);
        m_volumeSlider->setValue(volume);
        
        // Update volume label to show percentage
        if (volume > 100) {
            m_volumeLabel->setText(tr("Vol (%1%):").arg(volume));
        } else {
            m_volumeLabel->setText(tr("Vol:"));
        }
    }
}

void VideoPlayer::setPosition(qint64 position)
{
    qDebug() << "VideoPlayer: Setting position to" << position;
    m_mediaPlayer->setPosition(position);
}

void VideoPlayer::startInFullScreen()
{
    qDebug() << "VideoPlayer: Starting in fullscreen mode";
    
    if (!m_isFullScreen) {
        // Store the normal geometry before going fullscreen
        m_normalGeometry = QRect(pos(), size());
        
        // Hide controls initially in fullscreen
        m_controlsWidget->setVisible(false);
        
        // Remove all margins for fullscreen
        m_mainLayout->setContentsMargins(0, 0, 0, 0);
        
        // Remove window frame for true fullscreen
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        
        // Set fullscreen
        showFullScreen();
        m_isFullScreen = true;
        
        // Ensure we fill the entire screen
        QScreen *screen = QApplication::primaryScreen();
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
    }
}

void VideoPlayer::toggleFullScreen()
{
    if (m_isFullScreen) {
        exitFullScreen();
    } else {
        // Enter fullscreen
        qDebug() << "VideoPlayer: Entering fullscreen mode";
        
        // Store normal geometry
        m_normalGeometry = geometry();
        
        // Hide controls in fullscreen (can be shown on hover/click)
        m_controlsWidget->setVisible(false);
        
        // Remove all margins for fullscreen
        m_mainLayout->setContentsMargins(0, 0, 0, 0);
        
        // Remove window frame for true fullscreen
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        
        // Set fullscreen
        showFullScreen();
        m_isFullScreen = true;
        
        // Ensure we fill the entire screen
        QScreen *screen = QApplication::primaryScreen();
        if (screen) {
            setGeometry(screen->geometry());
        }
        
        // Update button icon
        m_fullScreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        m_fullScreenButton->setToolTip(tr("Exit Full Screen (ESC)"));
        
        emit fullScreenChanged(true);
    }
}

void VideoPlayer::exitFullScreen()
{
    if (m_isFullScreen) {
        qDebug() << "VideoPlayer: Exiting fullscreen mode";
        
        // Show controls
        m_controlsWidget->setVisible(true);
        
        // Restore normal layout margins
        m_mainLayout->setContentsMargins(m_normalMargins);
        
        // Restore normal window flags (with title bar and borders)
        setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | 
                      Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | 
                      Qt::WindowStaysOnTopHint);
        
        // Show in normal mode
        showNormal();
        
        // Restore geometry or use default size
        if (!m_normalGeometry.isNull()) {
            setGeometry(m_normalGeometry);
        } else {
            resize(800, 600);
            // Center the window on screen
            QScreen *screen = QApplication::primaryScreen();
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

bool VideoPlayer::isPlaying() const
{
    return m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState;
}

bool VideoPlayer::isPaused() const
{
    return m_mediaPlayer->playbackState() == QMediaPlayer::PausedState;
}

qint64 VideoPlayer::duration() const
{
    return m_mediaPlayer->duration();
}

qint64 VideoPlayer::position() const
{
    return m_mediaPlayer->position();
}

int VideoPlayer::volume() const
{
    if (m_mediaPlayer->audioOutput()) {
        return static_cast<int>(m_mediaPlayer->audioOutput()->volume() * 100);
    }
    return 0;
}

QString VideoPlayer::currentVideoPath() const
{
    return m_currentVideoPath;
}

void VideoPlayer::closeEvent(QCloseEvent *event)
{
    qDebug() << "VideoPlayer: Window closing, stopping playback";
    
    // Stop playback and clear media source to release file handle
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        // Clear the source to release file handle
        m_mediaPlayer->setSource(QUrl());
        // Clear video output as well
        m_mediaPlayer->setVideoOutput(nullptr);
    }
    
    // Clear current video path
    m_currentVideoPath.clear();
    
    // Exit fullscreen if active
    if (m_isFullScreen) {
        exitFullScreen();
    }
    
    event->accept();
}

void VideoPlayer::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "VideoPlayer: Key pressed:" << event->key();
    
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
            // Seek backward 10 seconds
            setPosition(qMax(qint64(0), position() - 10000));
            break;
            
        case Qt::Key_Right:
            // Seek forward 10 seconds
            setPosition(qMin(duration(), position() + 10000));
            break;
            
        case Qt::Key_Up:
            // Increase volume by 5%
            setVolume(qMin(150, volume() + 5));
            break;
            
        case Qt::Key_Down:
            // Decrease volume by 5%
            setVolume(qMax(0, volume() - 5));
            break;
            
        default:
            QWidget::keyPressEvent(event);
    }
}

bool VideoPlayer::eventFilter(QObject *watched, QEvent *event)
{
    // Handle double-click on video widget for fullscreen
    if (watched == m_videoWidget && event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleFullScreen();
            return true;
        }
    }
    
    // In fullscreen mode, show controls on mouse move
    if (m_isFullScreen && event->type() == QEvent::MouseMove) {
        m_controlsWidget->setVisible(true);
        
        // Hide controls after 3 seconds of no mouse movement
        static QTimer* hideTimer = nullptr;
        if (!hideTimer) {
            hideTimer = new QTimer(this);
            hideTimer->setSingleShot(true);
            connect(hideTimer, &QTimer::timeout, [this]() {
                if (m_isFullScreen && !m_controlsWidget->underMouse()) {
                    m_controlsWidget->setVisible(false);
                }
            });
        }
        hideTimer->stop();
        hideTimer->start(3000);
    }
    
    return QWidget::eventFilter(watched, event);
}

void VideoPlayer::on_playButton_clicked()
{
    qDebug() << "VideoPlayer: Play button clicked";
    
    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        pause();
    } else {
        play();
    }
}

void VideoPlayer::on_positionSlider_sliderMoved(int position)
{
    qDebug() << "VideoPlayer: Position slider moved to" << position;
    setPosition(position);
}

void VideoPlayer::on_positionSlider_sliderPressed()
{
    qDebug() << "VideoPlayer: Position slider pressed";
    m_isSliderBeingMoved = true;
}

void VideoPlayer::on_positionSlider_sliderReleased()
{
    qDebug() << "VideoPlayer: Position slider released";
    m_isSliderBeingMoved = false;
}

void VideoPlayer::on_volumeSlider_sliderMoved(int position)
{
    qDebug() << "VideoPlayer: Volume slider moved to" << position << "%";
    setVolume(position);
}

void VideoPlayer::on_fullScreenButton_clicked()
{
    qDebug() << "VideoPlayer: Fullscreen button clicked";
    toggleFullScreen();
}

void VideoPlayer::updatePosition(qint64 position)
{
    if (!m_isSliderBeingMoved) {
        m_positionSlider->setValue(static_cast<int>(position));
    }
    m_positionLabel->setText(formatTime(position));
    emit positionChanged(position);
}

void VideoPlayer::updateDuration(qint64 duration)
{
    qDebug() << "VideoPlayer: Duration changed to" << duration;
    
    m_positionSlider->setRange(0, static_cast<int>(duration));
    m_durationLabel->setText(formatTime(duration));
    emit durationChanged(duration);
}

void VideoPlayer::handleError(QMediaPlayer::Error error, const QString &errorString)
{
    Q_UNUSED(error);
    qDebug() << "VideoPlayer: Error occurred:" << errorString;
    
    QString errorMessage;
    
    switch(error) {
        case QMediaPlayer::NoError:
            return;
        case QMediaPlayer::ResourceError:
            errorMessage = tr("Resource Error: %1").arg(errorString);
            break;
        case QMediaPlayer::FormatError:
            errorMessage = tr("Format Error: The video format is not supported. %1").arg(errorString);
            break;
        case QMediaPlayer::NetworkError:
            errorMessage = tr("Network Error: %1").arg(errorString);
            break;
        case QMediaPlayer::AccessDeniedError:
            errorMessage = tr("Access Denied: %1").arg(errorString);
            break;
        default:
            errorMessage = tr("Unknown Error: %1").arg(errorString);
            break;
    }
    
    emit errorOccurred(errorMessage);
}

void VideoPlayer::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    qDebug() << "VideoPlayer: Playback state changed to" << state;
    
    switch(state) {
        case QMediaPlayer::PlayingState:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
            m_playButton->setToolTip(tr("Pause"));
            break;
        case QMediaPlayer::PausedState:
        case QMediaPlayer::StoppedState:
            m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            m_playButton->setToolTip(tr("Play"));
            break;
    }
    
    emit playbackStateChanged(state);
}

QString VideoPlayer::formatTime(qint64 milliseconds) const
{
    int seconds = static_cast<int>(milliseconds / 1000);
    int minutes = seconds / 60;
    int hours = minutes / 60;
    
    seconds %= 60;
    minutes %= 60;
    
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
