#include "vp_shows_vlc_test.h"
#include "vp_shows_vlc_player.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFileDialog>
#include <QTimer>
#include <QDebug>
#include <QStyle>

VP_Shows_VLCTest::VP_Shows_VLCTest(QWidget *parent)
    : QWidget(parent)
    , m_player(nullptr)
    , m_positionUpdateTimer(nullptr)
{
    qDebug() << "VP_Shows_VLCTest: Creating test window";
    setupUI();
    
    // Create VLC player
    m_player = new VP_Shows_VLCPlayer(this);
    m_layout->insertWidget(0, m_player, 1); // Add player at top with stretch
    
    // Connect signals
    connectSignals();
    
    // Initialize VLC
    if (!m_player->initialize()) {
        qDebug() << "VP_Shows_VLCTest: Failed to initialize VLC player";
        m_statusLabel->setText("Failed to initialize VLC - Check that VLC SDK files are in place");
    } else {
        qDebug() << "VP_Shows_VLCTest: VLC initialized successfully";
        m_statusLabel->setText("VLC initialized successfully - Ready to load video");
    }
    
    // Setup position update timer
    m_positionUpdateTimer = new QTimer(this);
    m_positionUpdateTimer->setInterval(100); // Update every 100ms
    connect(m_positionUpdateTimer, &QTimer::timeout, this, &VP_Shows_VLCTest::updatePosition);
}

VP_Shows_VLCTest::~VP_Shows_VLCTest()
{
    qDebug() << "VP_Shows_VLCTest: Destructor";
    if (m_positionUpdateTimer) {
        m_positionUpdateTimer->stop();
    }
}

void VP_Shows_VLCTest::setupUI()
{
    setWindowTitle("LibVLC Integration Test");
    resize(800, 600);
    
    m_layout = new QVBoxLayout(this);
    
    // Control buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    m_openButton = new QPushButton("Open Video", this);
    m_playButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "Play", this);
    m_pauseButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), "Pause", this);
    m_stopButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), "Stop", this);
    
    buttonLayout->addWidget(m_openButton);
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addWidget(m_pauseButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addStretch();
    
    m_layout->addLayout(buttonLayout);
    
    // Position slider
    QHBoxLayout* sliderLayout = new QHBoxLayout();
    
    m_positionLabel = new QLabel("00:00", this);
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_durationLabel = new QLabel("00:00", this);
    
    sliderLayout->addWidget(m_positionLabel);
    sliderLayout->addWidget(m_positionSlider, 1);
    sliderLayout->addWidget(m_durationLabel);
    
    m_layout->addLayout(sliderLayout);
    
    // Volume control
    QHBoxLayout* volumeLayout = new QHBoxLayout();
    
    QLabel* volumeLabel = new QLabel("Volume:", this);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setMaximumWidth(150);
    
    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(m_volumeSlider);
    volumeLayout->addStretch();
    
    m_layout->addLayout(volumeLayout);
    
    // Speed control
    QHBoxLayout* speedLayout = new QHBoxLayout();
    
    QLabel* speedLabel = new QLabel("Speed:", this);
    m_speedComboBox = new QComboBox(this);
    m_speedComboBox->addItems({"0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x"});
    m_speedComboBox->setCurrentText("1.0x");
    
    speedLayout->addWidget(speedLabel);
    speedLayout->addWidget(m_speedComboBox);
    speedLayout->addStretch();
    
    m_layout->addLayout(speedLayout);
    
    // Status label
    m_statusLabel = new QLabel("Initializing...", this);
    m_statusLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 5px; }");
    m_layout->addWidget(m_statusLabel);
    
    // Initial button states
    m_playButton->setEnabled(false);
    m_pauseButton->setEnabled(false);
    m_stopButton->setEnabled(false);
}

void VP_Shows_VLCTest::connectSignals()
{
    // Button signals
    connect(m_openButton, &QPushButton::clicked, this, &VP_Shows_VLCTest::openVideo);
    connect(m_playButton, &QPushButton::clicked, this, &VP_Shows_VLCTest::play);
    connect(m_pauseButton, &QPushButton::clicked, this, &VP_Shows_VLCTest::pause);
    connect(m_stopButton, &QPushButton::clicked, this, &VP_Shows_VLCTest::stop);
    
    // Slider signals
    connect(m_positionSlider, &QSlider::sliderMoved, this, &VP_Shows_VLCTest::seek);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &VP_Shows_VLCTest::changeVolume);
    
    // Speed control
    connect(m_speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VP_Shows_VLCTest::changeSpeed);
    
    // Player signals
    connect(m_player, &VP_Shows_VLCPlayer::durationChanged, 
            this, &VP_Shows_VLCTest::onDurationChanged);
    connect(m_player, &VP_Shows_VLCPlayer::playbackStateChanged,
            this, &VP_Shows_VLCTest::onPlaybackStateChanged);
    connect(m_player, &VP_Shows_VLCPlayer::errorOccurred,
            this, &VP_Shows_VLCTest::onError);
    connect(m_player, &VP_Shows_VLCPlayer::mediaEndReached,
            this, &VP_Shows_VLCTest::onMediaEndReached);
}

void VP_Shows_VLCTest::openVideo()
{
    qDebug() << "VP_Shows_VLCTest: Opening video dialog";
    
    QString fileName = QFileDialog::getOpenFileName(this, 
        "Open Video File",
        "",
        "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        qDebug() << "VP_Shows_VLCTest: Loading video:" << fileName;
        if (m_player->loadVideo(fileName)) {
            m_statusLabel->setText("Video loaded: " + QFileInfo(fileName).fileName());
            m_playButton->setEnabled(true);
            m_stopButton->setEnabled(true);
            
            // Set volume
            m_player->setVolume(m_volumeSlider->value());
        } else {
            m_statusLabel->setText("Failed to load video");
        }
    }
}

void VP_Shows_VLCTest::play()
{
    qDebug() << "VP_Shows_VLCTest: Play clicked";
    m_player->play();
    m_positionUpdateTimer->start();
}

void VP_Shows_VLCTest::pause()
{
    qDebug() << "VP_Shows_VLCTest: Pause clicked";
    m_player->pause();
}

void VP_Shows_VLCTest::stop()
{
    qDebug() << "VP_Shows_VLCTest: Stop clicked";
    m_player->stop();
    m_positionUpdateTimer->stop();
    m_positionSlider->setValue(0);
    m_positionLabel->setText("00:00");
}

void VP_Shows_VLCTest::seek(int position)
{
    if (m_player->duration() > 0) {
        qint64 newPosition = (qint64)position * m_player->duration() / m_positionSlider->maximum();
        m_player->setPosition(newPosition);
    }
}

void VP_Shows_VLCTest::changeVolume(int volume)
{
    m_player->setVolume(volume);
}

void VP_Shows_VLCTest::changeSpeed()
{
    QString speedText = m_speedComboBox->currentText();
    speedText.remove('x');
    float speed = speedText.toFloat();
    m_player->setPlaybackRate(speed);
}

void VP_Shows_VLCTest::updatePosition()
{
    if (!m_player->isPlaying()) {
        return;
    }
    
    qint64 position = m_player->position();
    qint64 duration = m_player->duration();
    
    if (duration > 0) {
        m_positionSlider->setValue(position * m_positionSlider->maximum() / duration);
    }
    
    m_positionLabel->setText(formatTime(position));
}

void VP_Shows_VLCTest::onDurationChanged(qint64 duration)
{
    qDebug() << "VP_Shows_VLCTest: Duration changed to" << duration << "ms";
    m_durationLabel->setText(formatTime(duration));
    m_positionSlider->setRange(0, 1000); // Use 0-1000 for precision
}

void VP_Shows_VLCTest::onPlaybackStateChanged(bool playing)
{
    qDebug() << "VP_Shows_VLCTest: Playback state changed, playing:" << playing;
    m_playButton->setEnabled(!playing);
    m_pauseButton->setEnabled(playing);
    
    if (playing) {
        m_statusLabel->setText("Playing");
        m_positionUpdateTimer->start();
    } else {
        m_statusLabel->setText("Paused/Stopped");
        m_positionUpdateTimer->stop();
    }
}

void VP_Shows_VLCTest::onError(const QString& error)
{
    qDebug() << "VP_Shows_VLCTest: Error:" << error;
    m_statusLabel->setText("Error: " + error);
}

void VP_Shows_VLCTest::onMediaEndReached()
{
    qDebug() << "VP_Shows_VLCTest: Media end reached";
    m_statusLabel->setText("Playback completed");
    m_positionUpdateTimer->stop();
}

QString VP_Shows_VLCTest::formatTime(qint64 milliseconds)
{
    int seconds = (milliseconds / 1000) % 60;
    int minutes = (milliseconds / 60000) % 60;
    int hours = (milliseconds / 3600000);
    
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
