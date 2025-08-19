#ifndef OPERATIONS_VP_SHOWS_H
#define OPERATIONS_VP_SHOWS_H

#include <QObject>
#include <QPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <memory>
#include <QStringList>

// Forward declarations
class MainWindow;
class VideoPlayer;
class VP_ShowsEncryptionProgressDialog;

class Operations_VP_Shows : public QObject
{
    Q_OBJECT

private:
    MainWindow* m_mainWindow;
    std::unique_ptr<VideoPlayer> m_testVideoPlayer;  // For testing purposes
    VP_ShowsEncryptionProgressDialog* m_encryptionDialog;
    
    // Helper functions
    QString selectVideoFile();
    bool isValidVideoFile(const QString& filePath);
    QStringList findVideoFiles(const QString& folderPath, bool recursive = true);
    QString generateRandomFileName(const QString& extension);
    bool createShowFolderStructure(QString& outputPath);

public:
    explicit Operations_VP_Shows(MainWindow* mainWindow);
    ~Operations_VP_Shows();
    
    // Friend class declaration
    friend class MainWindow;
    
    // Test function to open and play a video
    void testVideoPlayer();
    
    // Import TV show functionality
    void importTVShow();

public slots:
    // Slot for debug button
    void on_pushButton_Debug_clicked();
    
    // Slot for add show button
    void on_pushButton_VP_List_AddShow_clicked();
    
    // Slot for encryption completion
    void onEncryptionComplete(bool success, const QString& message,
                             const QStringList& successfulFiles,
                             const QStringList& failedFiles);

signals:
    void videoPlayerError(const QString& errorMessage);
};

#endif // OPERATIONS_VP_SHOWS_H
