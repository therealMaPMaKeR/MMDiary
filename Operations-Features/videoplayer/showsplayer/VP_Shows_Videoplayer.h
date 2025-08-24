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
#include "vp_shows_watchhistory.h"

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
    
    // Watch history integration
    void setWatchHistoryManager(VP_ShowsWatchHistory* watchHistory);
    void setEpisodeInfo(const QString& showPath, const QString& episodePath, const QString& episodeIdentifier = QString());

    void forceUpdateSliderPosition(qint64 position);

signals:
    void errorOccurred(const QString& error);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void volumeChanged(int volume);
    void fullScreenChanged(bool isFullScreen);
    void aboutToClose(qint64 finalPosition);  // Emitted before window closes with final playback position

private slots:
    void on_playButton_clicked();
    void saveWatchProgress();  // Periodic save of watch progress
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
    void focusInEvent(QFocusEvent *event) override;

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
    QTimer* m_mouseCheckTimer;  // Timer to check for mouse movement
    QPoint m_lastMousePos;       // Last known mouse position
    void startCursorTimer();
    void stopCursorTimer();
    void hideCursor();
    void showCursor();
    void checkMouseMovement();   // Check if mouse has moved
    
    // Custom position slider for clickable seeking
    class ClickableSlider;
    ClickableSlider* createClickableSlider();
    
    // Watch history management
    VP_ShowsWatchHistory* m_watchHistory;
    QString m_showPath;
    QString m_episodePath;
    QString m_episodeIdentifier;
    QTimer* m_progressSaveTimer;
    qint64 m_lastSavedPosition;
    bool m_hasStartedPlaying;
    bool m_isClosing;
    
    void initializeWatchProgress();
    void finalizeWatchProgress();
    bool shouldUpdateProgress(qint64 currentPosition) const;
    
    // Focus management
    void ensureKeyboardFocus();
};

#endif // VP_SHOWS_VIDEOPLAYER_H
