#ifndef VP_SHOWS_VIDEOPLAYER_H
#define VP_SHOWS_VIDEOPLAYER_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMargins>
#include <QTimer>
#include <memory>

class VP_Shows_Videoplayer : public QWidget
{
    Q_OBJECT

public:
    explicit VP_Shows_Videoplayer(QWidget *parent = nullptr);
    ~VP_Shows_Videoplayer();

    // Video control functions
    bool loadVideo(const QString& filePath);
    void play();
    void pause();
    void stop();
    void setVolume(int volume);
    void setPosition(qint64 position);
    void toggleFullScreen();
    void exitFullScreen();
    void startInFullScreen();
    
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
    void fullScreenChanged(bool isFullScreen);

private slots:
    void on_playButton_clicked();
    void on_positionSlider_sliderMoved(int position);
    void on_positionSlider_sliderPressed();
    void on_positionSlider_sliderReleased();
    void on_volumeSlider_sliderMoved(int position);
    void on_fullScreenButton_clicked();
    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void handleError(QMediaPlayer::Error error, const QString &errorString);
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);
    
protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    QPushButton* m_fullScreenButton;
    QSlider* m_positionSlider;
    QSlider* m_volumeSlider;
    QLabel* m_positionLabel;
    QLabel* m_durationLabel;
    QLabel* m_volumeLabel;
    QWidget* m_controlsWidget;
    
    // Layout containers
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_controlLayout;
    QHBoxLayout* m_sliderLayout;
    
    // State tracking
    QString m_currentVideoPath;
    bool m_isSliderBeingMoved;
    bool m_isFullScreen;
    QRect m_normalGeometry;
    QMargins m_normalMargins;
    
    // Mouse cursor auto-hide
    QTimer* m_cursorTimer;
    void startCursorTimer();
    void stopCursorTimer();
    void hideCursor();
    void showCursor();
    
    // Custom position slider for clickable seeking
    class ClickableSlider;
    ClickableSlider* createClickableSlider();
};

#endif // VP_SHOWS_VIDEOPLAYER_H
