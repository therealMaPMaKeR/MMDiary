#ifndef OPERATIONS_ENCRYPTEDDATA_H
#define OPERATIONS_ENCRYPTEDDATA_H

#include <QObject>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QThread>
#include <QMutex>
#include <QPushButton>
#include <QAbstractButton>
#include <QTimer>
#include <QProcess>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include "../CustomWidgets/encryptedfileitemwidget.h"
#include "../Operations-Global/fileiconprovider.h"
#include "../Operations-Global/thumbnailcache.h"
#include "Operations-Global/encryptedfilemetadata.h"
#include <QScrollBar>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>
#include <QDialog>
#include <QCloseEvent>

struct FileExportInfo {
    QString sourceFile;
    QString targetFile;
    QString originalFilename;
    qint64 fileSize;
    QString fileType;
};

class MainWindow;

// Worker class for encryption in separate thread
class EncryptionWorker : public QObject
{
    Q_OBJECT

public:
    //multiple files
    EncryptionWorker(const QStringList& sourceFiles, const QStringList& targetFiles,
                     const QByteArray& encryptionKey, const QString& username,
                     const QMap<QString, QPixmap>& videoThumbnails = QMap<QString, QPixmap>());
    //single file
    EncryptionWorker(const QString& sourceFile, const QString& targetFile,
                     const QByteArray& encryptionKey, const QString& username,
                     const QMap<QString, QPixmap>& videoThumbnails = QMap<QString, QPixmap>());

    ~EncryptionWorker();

    // Public member variables (updated for multiple files)
    QStringList m_sourceFiles;
    QStringList m_targetFiles;

public slots:
    void doEncryption();

signals:
    void progressUpdated(int percentage);

    // New signal for file-specific progress
    void fileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);

    // Backward compatible: keep old signal for single files
    void encryptionFinished(bool success, const QString& errorMessage = QString());

    // New signal for multiple files
    void multiFileEncryptionFinished(bool success, const QString& errorMessage,
                                     const QStringList& successfulFiles,
                                     const QStringList& failedFiles);

    // *** NEW: Signal for video thumbnail extraction ***
    void videoThumbnailExtracted(const QString& encryptedFilePath, const QPixmap& thumbnail);

private:
    QByteArray m_encryptionKey;
    QString m_username;
    bool m_cancelled;
    QMutex m_cancelMutex;
    QMap<QString, QPixmap> m_videoThumbnails;

    EncryptedFileMetadata* m_metadataManager;

public:
    void cancel();
};

class DecryptionWorker : public QObject
{
    Q_OBJECT

public:
    DecryptionWorker(const QString& sourceFile, const QString& targetFile,
                     const QByteArray& encryptionKey);

    ~DecryptionWorker();

    QString m_sourceFile;
    QString m_targetFile;

public slots:
    void doDecryption();

signals:
    void progressUpdated(int percentage);
    void decryptionFinished(bool success, const QString& errorMessage = QString());

private:
    QByteArray m_encryptionKey;
    bool m_cancelled;
    QMutex m_cancelMutex;
    EncryptedFileMetadata* m_metadataManager;

public:
    void cancel();
};

// Worker class for temporary decryption (for opening files)
class TempDecryptionWorker : public QObject
{
    Q_OBJECT

public:
    TempDecryptionWorker(const QString& sourceFile, const QString& targetFile,
                         const QByteArray& encryptionKey);

    ~TempDecryptionWorker();

    QString m_sourceFile;
    QString m_targetFile;

public slots:
    void doDecryption();

signals:
    void progressUpdated(int percentage);
    void decryptionFinished(bool success, const QString& errorMessage = QString());

private:
    QByteArray m_encryptionKey;
    bool m_cancelled;
    QMutex m_cancelMutex;
    EncryptedFileMetadata* m_metadataManager;

public:
    void cancel();
};

// Batch decryption progress dialog
class BatchDecryptionProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchDecryptionProgressDialog(QWidget* parent = nullptr);

    void setOverallProgress(int percentage);
    void setFileProgress(int percentage);
    void setStatusText(const QString& text);
    void setFileCountText(const QString& text);
    bool wasCancelled() const;

    std::function<void()> onCancelCallback;

private:
    void onCancelClicked();
    void setupUI();

    QProgressBar* m_overallProgress;
    QProgressBar* m_fileProgress;
    QLabel* m_statusLabel;
    QLabel* m_fileCountLabel;
    QPushButton* m_cancelButton;
    bool m_cancelled;
protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
signals:
    void cancelled();
};

// Forward declaration for Operations_EncryptedData
class Operations_EncryptedData;

// Batch decryption worker
class BatchDecryptionWorker : public QObject
{
    Q_OBJECT

public:
    BatchDecryptionWorker(const QList<FileExportInfo>& fileInfos,
                          const QByteArray& encryptionKey);
    ~BatchDecryptionWorker();

public slots:
    void doDecryption();
    void cancel();

signals:
    void overallProgressUpdated(int percentage);
    void fileProgressUpdated(int percentage);
    void fileStarted(int currentFile, int totalFiles, const QString& fileName);
    void batchDecryptionFinished(bool success, const QString& errorMessage,
                                 const QStringList& successfulFiles,
                                 const QStringList& failedFiles);

private:
    bool decryptSingleFile(const FileExportInfo& fileInfo,
                           qint64 currentTotalProcessed, qint64 totalSize);

    QList<FileExportInfo> m_fileInfos;
    QByteArray m_encryptionKey;
    bool m_cancelled;
    QMutex m_cancelMutex;
    EncryptedFileMetadata* m_metadataManager;
};

class Operations_EncryptedData : public QObject
{
    Q_OBJECT

private:
    MainWindow* m_mainWindow;

    // Helper functions
    QString determineFileType(const QString& filePath);
    QString generateRandomFilename();
    bool checkFilenameExists(const QString& folderPath, const QString& filename);
    QString createTargetPath(const QString& sourceFile, const QString& username);
    void showSuccessDialog(const QString& encryptedFile, const QString& originalFile);

    // Progress dialog
    QProgressDialog* m_progressDialog;
    EncryptionWorker* m_worker;
    QThread* m_workerThread;

    DecryptionWorker* m_decryptWorker;
    QThread* m_decryptWorkerThread;

    // Temporary decryption for opening files
    TempDecryptionWorker* m_tempDecryptWorker;
    QThread* m_tempDecryptWorkerThread;
    QString m_pendingAppToOpen; // Store the app path for opening after temp decryption

    // Helper function to map UI display names to directory names
    QString mapSortTypeToDirectory(const QString& sortType);
    // Helper function to map directory names back to UI display names
    QString mapDirectoryToSortType(const QString& directoryName);

    void updateButtonStates();

    // Double-click to open functionality
    QString checkDefaultApp(const QString& extension);
    enum class AppChoice { Cancel, UseDefault, SelectApp };
    AppChoice showDefaultAppDialog(const QString& appName);
    AppChoice showNoDefaultAppDialog();
    QString selectApplication();
    QString createTempFilePath(const QString& originalFilename);
    void openFileWithApp(const QString& tempFile, const QString& appPath);

    // Temp file monitoring and cleanup
    QTimer* m_tempFileCleanupTimer;
    void startTempFileMonitoring();
    void cleanupTempFiles();
    bool isFileInUse(const QString& filePath);
    QString getTempDecryptDir();

    // New method for handling multiple file success dialog
    void showMultiFileSuccessDialog(const QStringList& originalFiles,
                                    const QStringList& successfulFiles,
                                    const QStringList& failedFiles);

    // Icon and thumbnail management
    ThumbnailCache* m_thumbnailCache = nullptr;
    FileIconProvider* m_iconProvider = nullptr;

    // Lazy loading management
    QTimer* m_lazyLoadTimer = nullptr;
    QList<QListWidgetItem*> m_pendingThumbnailItems;

    // Thumbnail generation
    void generateThumbnailForItem(QListWidgetItem* item);
    void generateImageThumbnail(const QString& encryptedFilePath, const QString& originalFilename);
    QPixmap createImageThumbnail(const QString& tempImagePath, int size = 64);

    // Icon management
    QPixmap getIconForFileType(const QString& originalFilename, const QString& fileType);

    // Lazy loading
    void checkVisibleItems();
    void startLazyLoadTimer();

    void updateItemThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail);

    EncryptedFileMetadata* m_metadataManager;

    void showContextMenu_FileList(const QPoint& pos);
    void onContextMenuOpen();
    void onContextMenuEdit();
    void onContextMenuDecryptExport();
    void onContextMenuDelete();

    void updateFileListDisplay();
    void populateCategoriesList();
    void populateTagsList();

    QMap<QString, EncryptedFileMetadata::FileMetadata> m_fileMetadataCache;
    QStringList m_currentFilteredFiles; // Files matching current filters
    bool m_updatingFilters;

    void refreshAfterEncryption(const QString& encryptedFilePath);
    void refreshAfterEdit(const QString& encryptedFilePath);
    void selectCategoryAndFile(const QString& categoryToSelect, const QString& filePathToSelect = QString());
    void removeFileFromCacheAndRefresh(const QString& encryptedFilePath);

    // Batch decryption worker and thread
    BatchDecryptionWorker* m_batchDecryptWorker;
    QThread* m_batchDecryptWorkerThread;

    // Helper functions

    QList<FileExportInfo> enumerateAllEncryptedFiles();
    QString formatFileSize(qint64 bytes);

    // Batch decryption progress slots
    void onBatchDecryptionOverallProgress(int percentage);
    void onBatchDecryptionFileProgress(int percentage);
    void onBatchDecryptionFileStarted(int currentFile, int totalFiles, const QString& fileName);
    void onBatchDecryptionFinished(bool success, const QString& errorMessage,
                                   const QStringList& successfulFiles,
                                   const QStringList& failedFiles);
    void onBatchDecryptionCancelled();

    BatchDecryptionProgressDialog* m_batchProgressDialog;

public:
    explicit Operations_EncryptedData(MainWindow* mainWindow);
    ~Operations_EncryptedData();
    friend class MainWindow;

    // Main encryption function
    void encryptSelectedFile();
    // File listing functions
    void populateEncryptedFilesList();
    QString getOriginalFilename(const QString& encryptedFilePath);

    void decryptSelectedFile();

    void deleteSelectedFile();

    void secureDeleteExternalFile();

    // Main batch decrypt function
    void decryptAndExportAllFiles();

    // *** NEW: Method to store video thumbnail ***
    void storeVideoThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail);

public slots:
    void onSortTypeChanged(const QString& sortType);
    void onFileListDoubleClicked(QListWidgetItem* item);

private slots:
    void onEncryptionProgress(int percentage);
    void onEncryptionFinished(bool success, const QString& errorMessage = QString());
    void onEncryptionCancelled();

    void onDecryptionProgress(int percentage);
    void onDecryptionFinished(bool success, const QString& errorMessage = QString());
    void onDecryptionCancelled();

    // Temp decryption slots
    void onTempDecryptionProgress(int percentage);
    void onTempDecryptionFinished(bool success, const QString& errorMessage = QString());
    void onTempDecryptionCancelled();

    // Cleanup timer slot
    void onCleanupTimerTimeout();

    // New slot for file progress updates
    void onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);

    // New slot for multiple file encryption completion
    void onMultiFileEncryptionFinished(bool success, const QString& errorMessage,
                                       const QStringList& successfulFiles,
                                       const QStringList& failedFiles);

    void onLazyLoadTimeout();
    void onListScrolled();

    void onCategorySelectionChanged();
    void onTagCheckboxChanged();

protected:
    // Event filter for Delete key functionality
    bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif // OPERATIONS_ENCRYPTEDDATA_H
