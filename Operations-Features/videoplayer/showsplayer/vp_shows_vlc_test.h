#ifndef VP_SHOWS_VLC_TEST_H
#define VP_SHOWS_VLC_TEST_H

#include <QWidget>
#include <QComboBox>

class VP_Shows_VLCPlayer;
class QPushButton;
class QSlider;
class QLabel;
class QVBoxLayout;
class QTimer;

/**
 * @brief Test widget for verifying LibVLC integration
 * 
 * This widget provides a simple interface to test that LibVLC
 * is properly integrated and working in the project.
 */
class VP_Shows_VLCTest : public QWidget
{
    Q_OBJECT
    
public:
    explicit VP_Shows_VLCTest(QWidget *parent = nullptr);
    ~VP_Shows_VLCTest();
    
private slots:
    void openVideo();
    void play();
    void pause();
    void stop();
    void seek(int position);
    void changeVolume(int volume);
    void changeSpeed();
    void updatePosition();
    
    // Player signal handlers
    void onDurationChanged(qint64 duration);
    void onPlaybackStateChanged(bool playing);
    void onError(const QString& error);
    void onMediaEndReached();
    
private:
    void setupUI();
    void connectSignals();
    QString formatTime(qint64 milliseconds);
    
    // VLC player
    VP_Shows_VLCPlayer* m_player;
    
    // UI elements
    QVBoxLayout* m_layout;
    QPushButton* m_openButton;
    QPushButton* m_playButton;
    QPushButton* m_pauseButton;
    QPushButton* m_stopButton;
    QSlider* m_positionSlider;
    QSlider* m_volumeSlider;
    QLabel* m_positionLabel;
    QLabel* m_durationLabel;
    QLabel* m_statusLabel;
    QComboBox* m_speedComboBox;
    
    // Timer for position updates
    QTimer* m_positionUpdateTimer;
};

#endif // VP_SHOWS_VLC_TEST_H
