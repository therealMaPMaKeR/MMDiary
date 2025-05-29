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
#include <QMutex>
#include <QDirIterator>

struct FileExportInfo {
    QString sourceFile;
    QString targetFile;
    QString originalFilename;
    qint64 fileSize;
    QString fileType;
};

enum class DeletionType {
    Files,
    Folder,
    Cancel
};

struct DeletionItem {
    QString path;
    QString displayName;
    qint64 size;
    bool isFolder;

    DeletionItem(const QString& p, const QString& dn, qint64 s, bool folder)
        : path(p), displayName(dn), size(s), isFolder(folder) {}
};

struct DeletionResult {
    QStringList successfulItems;
    QStringList failedItems;
    qint64 totalSize;
    int totalFiles;

    DeletionResult() : totalSize(0), totalFiles(0) {}
};

class MainWindow;
class SecureDeletionProgressDialog;
class SecureDeletionWorker;
class EncryptionProgressDialog;

class EncryptionProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EncryptionProgressDialog(QWidget* parent = nullptr);

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

    void currentFileProgressUpdated(int percentage);

    // REMOVED: Video thumbnail extraction signal (no longer needed with embedded thumbnails)
    // void videoThumbnailExtracted(const QString& encryptedFilePath, const QPixmap& thumbnail);

private:
    QByteArray m_encryptionKey;
    QString m_username;
    bool m_cancelled;
    QMutex m_cancelMutex;
    QMap<QString, QPixmap> m_videoThumbnails; // Still used for pre-extracted video thumbnails

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

// Progress dialog class declaration:
class SecureDeletionProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SecureDeletionProgressDialog(QWidget* parent = nullptr);

    void setOverallProgress(int percentage);
    void setCurrentItem(const QString& itemName);
    void setStatusText(const QString& text);
    bool wasCancelled() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void onCancelClicked();

private:
    void setupUI();

    QProgressBar* m_overallProgress;
    QLabel* m_statusLabel;
    QLabel* m_currentItemLabel;
    QPushButton* m_cancelButton;
    bool m_cancelled;

signals:
    void cancelled();
};

// Worker class declaration:
class SecureDeletionWorker : public QObject
{
    Q_OBJECT

public:
    SecureDeletionWorker(const QList<DeletionItem>& items);
    ~SecureDeletionWorker();

public slots:
    void doSecureDeletion();
    void cancel();

signals:
    void progressUpdated(int percentage);
    void currentItemChanged(const QString& itemName);
    void deletionFinished(bool success, const DeletionResult& result, const QString& errorMessage = QString());

private:
    bool secureDeleteSingleFile(const QString& filePath);
    bool secureDeleteFolder(const QString& folderPath, int& processedFiles, int totalFiles);
    QStringList enumerateFilesInFolder(const QString& folderPath);

    QList<DeletionItem> m_items;
    bool m_cancelled;
    QMutex m_cancelMutex;
};

class Operations_EncryptedData : public QObject
{
    Q_OBJECT

private:
    MainWindow* m_mainWindow;

    // Helper functions
    QString determineFileType(const QString& filePath);
    QString generateRandomFilename(const QString& originalExtension = QString());
    bool checkFilenameExists(const QString& folderPath, const QString& filename);
    QString createTargetPath(const QString& sourceFile, const QString& username);
    void showSuccessDialog(const QString& encryptedFile, const QString& originalFile);

    // Progress dialog
    QProgressDialog* m_progressDialog;
    EncryptionProgressDialog* m_encryptionProgressDialog;
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

    // UPDATED: Icon management (removed thumbnail cache dependencies)
    FileIconProvider* m_iconProvider = nullptr;

    // REMOVED: Thumbnail cache and lazy loading members
    // ThumbnailCache* m_thumbnailCache = nullptr;
    // QTimer* m_lazyLoadTimer = nullptr;
    // QList<QListWidgetItem*> m_pendingThumbnailItems;

    // Icon management
    QPixmap getIconForFileType(const QString& originalFilename, const QString& fileType);

    // REMOVED: Thumbnail generation and lazy loading methods
    // void generateThumbnailForItem(QListWidgetItem* item);
    // void generateImageThumbnail(const QString& encryptedFilePath, const QString& originalFilename);
    // QPixmap createImageThumbnail(const QString& tempImagePath, int size = 64);
    // void checkVisibleItems();
    // void startLazyLoadTimer();
    // void updateItemThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail);

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

    SecureDeletionWorker* m_secureDeletionWorker;
    QThread* m_secureDeletionWorkerThread;
    SecureDeletionProgressDialog* m_secureDeletionProgressDialog;

    DeletionType showDeletionTypeDialog();
    bool validateExternalItem(const QString& itemPath, bool isFolder);
    qint64 calculateItemSize(const QString& itemPath, bool isFolder, int& fileCount);
    bool showDeletionConfirmationDialog(const QList<DeletionItem>& items);
    void showDeletionResultsDialog(const DeletionResult& result);

    // Metadata repair functions
    void repairCorruptedMetadata();
    QStringList scanForCorruptedMetadata();
    bool showMetadataRepairDialog(int corruptedCount);
    bool repairMetadataFiles(const QStringList& corruptedFiles);
    bool repairSingleFileMetadata(const QString& encryptedFilePath);

#ifdef QT_DEBUG
    // Debug function to purposefully corrupt metadata for testing
    bool debugCorruptFileMetadata(const QString& encryptedFilePath);
    void onContextMenuDebugCorruptMetadata();
#endif

    QString generateUniqueFilePath(const QString& targetDirectory, const QString& originalFilename);
    QString generateUniqueFilenameInDirectory(const QString& targetDirectory, const QString& originalFilename,
                                              const QStringList& usedFilenames);

    QList<FileExportInfo> enumerateVisibleEncryptedFiles();

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

    void secureDeleteExternalItems();

    // Main batch decrypt function
    void decryptAndExportVisibleFiles();

    // REMOVED: Video thumbnail storage method (no longer needed with embedded thumbnails)
    // void storeVideoThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail);

public slots:
    void onSortTypeChanged(const QString& sortType);
    void onFileListDoubleClicked(QListWidgetItem* item);

private slots:
    void onCurrentFileProgressUpdate(int percentage);
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

    // REMOVED: Lazy loading and thumbnail-related slots
    // void onLazyLoadTimeout();
    // void onListScrolled();

    void onCategorySelectionChanged();
    void onTagCheckboxChanged();

    void onSecureDeletionProgress(int percentage);
    void onSecureDeletionCurrentItem(const QString& itemName);
    void onSecureDeletionFinished(bool success, const DeletionResult& result, const QString& errorMessage);
    void onSecureDeletionCancelled();

    void onContextMenuExportListed();

protected:
    // Event filter for Delete key functionality
    bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif // OPERATIONS_ENCRYPTEDDATA_H
