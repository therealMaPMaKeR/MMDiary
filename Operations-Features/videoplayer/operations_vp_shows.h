#ifndef OPERATIONS_VP_SHOWS_H
#define OPERATIONS_VP_SHOWS_H

#include <QObject>
#include <QPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <memory>
#include <QStringList>
#include <QMap>
#include <QPixmap>
#include <QListWidgetItem>
#include <QTreeWidgetItem>

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
    std::unique_ptr<VideoPlayer> m_episodePlayer;    // For episode playback
    VP_ShowsEncryptionProgressDialog* m_encryptionDialog;
    
    // Store mapping between show names and their folder paths
    QMap<QString, QString> m_showFolderMapping;
    
    // Store mapping between episode display names and their file paths
    // Key format: "ShowName_Season_Episode" -> filepath
    QMap<QString, QString> m_episodeFileMapping;
    
    // Current displayed show folder path
    QString m_currentShowFolder;
    
    // Current decrypted temp file path (for cleanup)
    QString m_currentTempFile;
    
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
    
    // Load and display TV shows in the list
    void loadTVShowsList();
    
    // Refresh the TV shows list (public version that can be called from outside)
    void refreshTVShowsList();
    
    // Open TV shows settings dialog
    void openSettings();
    
    // Save show description to encrypted file
    bool saveShowDescription(const QString& showFolderPath, const QString& description);
    
    // Load show description from encrypted file
    QString loadShowDescription(const QString& showFolderPath);
    
    // Save show image to encrypted file
    bool saveShowImage(const QString& showFolderPath, const QByteArray& imageData);
    
    // Load show image from encrypted file
    QPixmap loadShowImage(const QString& showFolderPath);
    
    // Display show details page
    void displayShowDetails(const QString& showName);
    
    // Handle show list double-click
    void onShowListItemDoubleClicked(QListWidgetItem* item);
    
    // Load episodes for a show and populate tree widget
    void loadShowEpisodes(const QString& showFolderPath);
    
    // Handle episode double-click in tree widget
    void onEpisodeDoubleClicked(QTreeWidgetItem* item, int column);
    
    // Decrypt and play episode
    void decryptAndPlayEpisode(const QString& encryptedFilePath, const QString& episodeName);
    
    // Clean up temporary decrypted file
    void cleanupTempFile();

public slots:
    // Slot for debug button
    void on_pushButton_Debug_clicked();
    
    // Slot for add show button
    void on_pushButton_VP_List_AddShow_clicked();
    
    // Slot for play/continue button
    void onPlayContinueClicked();
    
    // Slot for encryption completion
    void onEncryptionComplete(bool success, const QString& message,
                             const QStringList& successfulFiles,
                             const QStringList& failedFiles);

signals:
    void videoPlayerError(const QString& errorMessage);
};

#endif // OPERATIONS_VP_SHOWS_H
