#include "videoplayer.h"
#include <QAudioOutput>
#include <QDebug>
#include <QFileInfo>
#include <QStyle>
#include <QTime>

VideoPlayer::VideoPlayer(QWidget *parent)
    : QWidget(parent)
    , m_videoWidget(nullptr)
    , m_playButton(nullptr)
    , m_stopButton(nullptr)
    , m_positionSlider(nullptr)
    , m_volumeSlider(nullptr)
    , m_positionLabel(nullptr)
    , m_durationLabel(nullptr)
    , m_volumeLabel(nullptr)
    , m_mainLayout(nullptr)
    , m_controlLayout(nullptr)
    , m_sliderLayout(nullptr)
    , m_isSliderBeingMoved(false)
{
    qDebug() << "VideoPlayer: Constructor called";
    
    // Set window properties
    setWindowTitle(tr("Video Player"));
    resize(800, 600);
    
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
    m_mediaPlayer->setVideoOutput(m_videoWidget);
    
    // Create controls
    createControls();
    
    // Create layouts
    createLayouts();
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
    
    // Position slider
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);
    
    // Volume slider
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(70);
    m_volumeSlider->setMaximumWidth(100);
    m_volumeSlider->setToolTip(tr("Volume"));
    
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
    
    // Control layout (buttons)
    m_controlLayout = new QHBoxLayout();
    m_controlLayout->addWidget(m_playButton);
    m_controlLayout->addWidget(m_stopButton);
    m_controlLayout->addStretch();
    
    // Slider layout (position and volume)
    m_sliderLayout = new QHBoxLayout();
    m_sliderLayout->addWidget(m_positionLabel);
    m_sliderLayout->addWidget(m_positionSlider, 1);
    m_sliderLayout->addWidget(m_durationLabel);
    m_sliderLayout->addSpacing(20);
    m_sliderLayout->addWidget(m_volumeLabel);
    m_sliderLayout->addWidget(m_volumeSlider);
    
    // Main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->addWidget(m_videoWidget, 1);
    m_mainLayout->addLayout(m_controlLayout);
    m_mainLayout->addLayout(m_sliderLayout);
    
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
    
    // Slider signals
    connect(m_positionSlider, &QSlider::sliderMoved,
            this, &VideoPlayer::on_positionSlider_sliderMoved);
    
    connect(m_positionSlider, &QSlider::sliderPressed,
            this, [this]() { m_isSliderBeingMoved = true; });
    
    connect(m_positionSlider, &QSlider::sliderReleased,
            this, [this]() { m_isSliderBeingMoved = false; });
    
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
    qDebug() << "VideoPlayer: Setting volume to" << volume;
    
    if (m_mediaPlayer->audioOutput()) {
        float volumeFloat = volume / 100.0f;
        m_mediaPlayer->audioOutput()->setVolume(volumeFloat);
        m_volumeSlider->setValue(volume);
    }
}

void VideoPlayer::setPosition(qint64 position)
{
    qDebug() << "VideoPlayer: Setting position to" << position;
    m_mediaPlayer->setPosition(position);
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

void VideoPlayer::on_volumeSlider_sliderMoved(int position)
{
    qDebug() << "VideoPlayer: Volume slider moved to" << position;
    setVolume(position);
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
