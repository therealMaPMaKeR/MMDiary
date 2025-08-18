#ifndef OPERATIONS_VP_SHOWS_H
#define OPERATIONS_VP_SHOWS_H

#include <QObject>
#include <QPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <memory>

// Forward declarations
class MainWindow;
class VideoPlayer;

class Operations_VP_Shows : public QObject
{
    Q_OBJECT

private:
    MainWindow* m_mainWindow;
    std::unique_ptr<VideoPlayer> m_testVideoPlayer;  // For testing purposes
    
    // Helper functions
    QString selectVideoFile();
    bool isValidVideoFile(const QString& filePath);

public:
    explicit Operations_VP_Shows(MainWindow* mainWindow);
    ~Operations_VP_Shows();
    
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

#endif // OPERATIONS_VP_SHOWS_H
