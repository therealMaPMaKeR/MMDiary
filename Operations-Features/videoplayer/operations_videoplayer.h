#ifndef OPERATIONS_VIDEOPLAYER_H
#define OPERATIONS_VIDEOPLAYER_H

#include <QObject>
#include <QPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <memory>

// Forward declarations
class MainWindow;
class VideoPlayer;

class Operations_Videoplayer : public QObject
{
    Q_OBJECT

private:
    MainWindow* m_mainWindow;
    std::unique_ptr<VideoPlayer> m_testVideoPlayer;  // For testing purposes
    
    // Helper functions
    QString selectVideoFile();
    bool isValidVideoFile(const QString& filePath);

public:
    explicit Operations_Videoplayer(MainWindow* mainWindow);
    ~Operations_Videoplayer();
    
    // Friend class declaration
    friend class MainWindow;
    
    // Test function to open and play a video
    void testVideoPlayer();

public slots:
    // Slot for debug button
    void on_pushButton_Debug_clicked();

signals:
    void videoPlayerError(const QString& errorMessage);
};

#endif // OPERATIONS_VIDEOPLAYER_H
