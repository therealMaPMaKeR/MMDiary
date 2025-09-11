#ifndef BASEVIDEOPLAYER_H
#define BASEVIDEOPLAYER_H

#include <QWidget>
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
#include <QScreen>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <memory>
#include "vp_vlcplayer.h"

// Windows-specific includes for shutdown detection
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

/**
 * @class BaseVideoPlayer
 * @brief Base class for all video player implementations
 * 
 * This class provides common video playback functionality that can be
 * inherited by specialized video players (shows, movies, clips, etc.)
 */
class BaseVideoPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit BaseVideoPlayer(QWidget *parent = nullptr);
    virtual ~BaseVideoPlayer();

    // Core video control functions
    virtual bool loadVideo(const QString& filePath);
    virtual void unloadVideo();  // Unload current video without errors
    virtual void play();
    virtual void pause();
    virtual void stop();
    virtual void setVolume(int volume);
    virtual void setPosition(qint64 position);
    virtual void setPlaybackSpeed(qreal speed);
    
    // Fullscreen management
    virtual void toggleFullScreen();
    virtual void enterFullScreen();
    virtual void exitFullScreen();
    virtual void startInFullScreen();
    
    // State query functions
    bool isPlaying() const;
    bool isPaused() const;
    qint64 duration() const;
    qint64 position() const;
    int volume() const;
    qreal playbackSpeed() const;
    QString currentVideoPath() const;
    
    // UI updates
    void forceUpdateSliderPosition(qint64 position);
    
    // Screen management
    void setTargetScreen(QScreen* screen) { m_targetScreen = screen; }
    QScreen* getTargetScreen() const { return m_targetScreen; }

signals:
    void errorOccurred(const QString& error);
    void playbackStateChanged(VP_VLCPlayer::PlayerState state);
    void playbackStarted();
    void finished();
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void volumeChanged(int volume);
    void playbackSpeedChanged(qreal speed);
    void fullScreenChanged(bool isFullScreen);
    void aboutToClose(qint64 finalPosition);

protected slots:
    // UI control slots
    virtual void on_playButton_clicked();
    virtual void on_positionSlider_sliderMoved(int position);
    virtual void on_positionSlider_sliderPressed();
    virtual void on_positionSlider_sliderReleased();
    virtual void on_volumeSlider_sliderMoved(int position);
    virtual void on_speedSpinBox_valueChanged(double value);
    virtual void on_fullScreenButton_clicked();
    
    // Media player slots
    virtual void updatePosition(qint64 position);
    virtual void updateDuration(qint64 duration);
    virtual void handleError(const QString &errorString);
    virtual void handlePlaybackStateChanged(VP_VLCPlayer::PlayerState state);
    
    // Cursor management
    void hideCursor();
    void showCursor();
    void checkMouseMovement();
    
protected:
    // Event handlers - virtual so children can override
    virtual void closeEvent(QCloseEvent *event) override;
    virtual void showEvent(QShowEvent *event) override;
    virtual void keyPressEvent(QKeyEvent *event) override;
    virtual void wheelEvent(QWheelEvent *event) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;
    virtual void focusInEvent(QFocusEvent *event) override;
    
    // Windows shutdown detection
    virtual bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    
    // UI setup methods - virtual for customization
    virtual void setupUI();
    virtual void createControls();
    virtual void createLayouts();
    virtual void connectSignals();
    
    // Helper methods
    QString formatTime(qint64 milliseconds) const;
    void ensureKeyboardFocus();
    void startCursorTimer();
    void stopCursorTimer();
    QScreen* getCurrentScreen() const;
    
    // Custom slider class for clickable seeking
    class ClickableSlider;
    ClickableSlider* createClickableSlider();
    
    // Core media components
    std::unique_ptr<VP_VLCPlayer> m_mediaPlayer;
    QPointer<QWidget> m_videoWidget;
    
    // Control widgets - protected so children can access
    // Using QPointer for automatic null-setting on deletion
    QPointer<QPushButton> m_playButton;
    QPointer<QPushButton> m_stopButton;
    QPointer<QPushButton> m_fullScreenButton;
    QPointer<QSlider> m_positionSlider;
    QPointer<QSlider> m_volumeSlider;
    QPointer<QDoubleSpinBox> m_speedSpinBox;
    QPointer<QLabel> m_positionLabel;
    QPointer<QLabel> m_durationLabel;
    QPointer<QLabel> m_volumeLabel;
    QPointer<QLabel> m_speedLabel;
    QPointer<QWidget> m_controlsWidget;
    
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
    bool m_isClosing;
    bool m_playbackStartedEmitted;
    
    // Mouse cursor auto-hide
    QTimer* m_cursorTimer;
    QTimer* m_mouseCheckTimer;
    QPoint m_lastMousePos;
    
    // Screen management
    QPointer<QScreen> m_targetScreen;
    
    // Static members for window state persistence across instances
    static QScreen* s_lastUsedScreen;
    static QRect s_lastWindowGeometry;
    static bool s_wasFullScreen;
    static bool s_wasMaximized;
    static bool s_wasMinimized;
    static int s_lastVolume;
    static qreal s_lastPlaybackSpeed;  // Remember playback speed across videos
    static bool s_hasStoredSettings;
    
    // Virtual initialization for derived classes
    virtual void initializeFromPreviousSettings();
    
    // Helper to check if position update is needed
    bool shouldUpdateProgress(qint64 currentPosition) const;
    
    // Windows shutdown tracking (protected so derived classes can check it)
#ifdef Q_OS_WIN
    bool m_windowsShutdownInProgress = false;
#endif

private:
    void initializePlayer();
};

#endif // BASEVIDEOPLAYER_H
