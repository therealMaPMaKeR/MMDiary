#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <memory>

class VideoPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayer(QWidget *parent = nullptr);
    ~VideoPlayer();

    // Video control functions
    bool loadVideo(const QString& filePath);
    void play();
    void pause();
    void stop();
    void setVolume(int volume);
    void setPosition(qint64 position);
    
    // State query functions
    bool isPlaying() const;
    bool isPaused() const;
    qint64 duration() const;
    qint64 position() const;
    int volume() const;
    QString currentVideoPath() const;

signals:
    void errorOccurred(const QString& error);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void volumeChanged(int volume);

private slots:
    void on_playButton_clicked();
    void on_positionSlider_sliderMoved(int position);
    void on_volumeSlider_sliderMoved(int position);
    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void handleError(QMediaPlayer::Error error, const QString &errorString);
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);

private:
    void setupUI();
    void createControls();
    void createLayouts();
    void connectSignals();
    QString formatTime(qint64 milliseconds) const;
    
    // Core media components
    std::unique_ptr<QMediaPlayer> m_mediaPlayer;
    QVideoWidget* m_videoWidget;
    
    // Control widgets
    QPushButton* m_playButton;
    QPushButton* m_stopButton;
    QSlider* m_positionSlider;
    QSlider* m_volumeSlider;
    QLabel* m_positionLabel;
    QLabel* m_durationLabel;
    QLabel* m_volumeLabel;
    
    // Layout containers
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_controlLayout;
    QHBoxLayout* m_sliderLayout;
    
    // State tracking
    QString m_currentVideoPath;
    bool m_isSliderBeingMoved;
};

#endif // VIDEOPLAYER_H
