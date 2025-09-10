#ifndef OPERATIONS_ENCRYPTEDDATA_H
#define OPERATIONS_ENCRYPTEDDATA_H

#include <QObject>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QAbstractButton>
#include <QTimer>
#include <QProcess>
#include <QPointer>
#include "../../mainwindow.h"
#include "operations.h"
#include "inputvalidation.h"
#include "../Operations-Global/SafeTimer.h"
#include "encryptedfileitemwidget.h"
#include "encrypteddata_fileiconprovider.h"
#include "encrypteddata_encryptedfilemetadata.h"
#include "ThreadSafeContainers.h"
#include <QScrollBar>
#include <QEvent>
#include <QKeyEvent>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>
#include <QDialog>
#include <QCloseEvent>
#include <QDirIterator>
#include <QListWidgetItem>
#include <memory>

// Include the separated headers
#include "encrypteddata_encryptionworkers.h"
#include "encrypteddata_progressdialogs.h"

// Forward declarations
class MainWindow;
class EncryptedFileMetadata;
class FileIconProvider;

// Enum for deletion type selection
enum class DeletionType {
    Files,
    Folder,
    Cancel
};

class Operations_EncryptedData : public QObject
{
    Q_OBJECT

public:
    explicit Operations_EncryptedData(MainWindow* mainWindow);
    ~Operations_EncryptedData();
    
    friend class MainWindow;

    // Main public functions
    void encryptSelectedFile();
    void decryptSelectedFile();
    void deleteSelectedFile();
    void secureDeleteExternalItems();
    void decryptAndExportVisibleFiles();
    void populateEncryptedFilesList();
    QString getOriginalFilename(const QString& encryptedFilePath);
    void refreshDisplayForSettingsChange();
    void clearSearch();

public slots:
    void onSortTypeChanged(const QString& sortType);
    void onFileListDoubleClicked(QListWidgetItem* item);
    void onTagSelectionModeChanged(const QString& mode);

private slots:
    // Encryption slots
    void onCurrentFileProgressUpdate(int percentage);
    void onEncryptionProgress(int percentage);
    void onEncryptionFinished(bool success, const QString& errorMessage = QString());
    void onEncryptionCancelled();
    void onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);
    void onMultiFileEncryptionFinished(bool success, const QString& errorMessage,
                                       const QStringList& successfulFiles,
                                       const QStringList& failedFiles);

    // Decryption slots
    void onDecryptionProgress(int percentage);
    void onDecryptionFinished(bool success, const QString& errorMessage = QString());
    void onDecryptionCancelled();

    // Temp decryption slots
    void onTempDecryptionProgress(int percentage);
    void onTempDecryptionFinished(bool success, const QString& errorMessage = QString());
    void onTempDecryptionCancelled();

    // Batch decryption slots
    void onBatchDecryptionOverallProgress(int percentage);
    void onBatchDecryptionFileProgress(int percentage);
    void onBatchDecryptionFileStarted(int currentFile, int totalFiles, const QString& fileName);
    void onBatchDecryptionFinished(bool success, const QString& errorMessage,
                                   const QStringList& successfulFiles,
                                   const QStringList& failedFiles);
    void onBatchDecryptionCancelled();

    // Secure deletion slots
    void onSecureDeletionProgress(int percentage);
    void onSecureDeletionCurrentItem(const QString& itemName);
    void onSecureDeletionFinished(bool success, const DeletionResult& result, const QString& errorMessage);
    void onSecureDeletionCancelled();

    // UI interaction slots
    void onCategorySelectionChanged();
    void onTagCheckboxChanged();
    void onCleanupTimerTimeout();
    void onSearchTextChanged();

    // Context menu slots
    void onContextMenuOpen();
    void onContextMenuOpenWith();
    void onContextMenuOpenWithImageViewer();
    void onContextMenuOpenWithVideoPlayer();
    void onContextMenuOpenWithVRVideoPlayer();
    void onContextMenuShowInExplorer();
    void onContextMenuEdit();
    void onContextMenuDecryptExport();
    void onContextMenuDelete();
    void onContextMenuExportListed();

#ifdef QT_DEBUG
    void onContextMenuDebugCorruptMetadata();
#endif

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Member variables
    // SECURITY: Use QPointer for automatic null checking when MainWindow is destroyed
    QPointer<MainWindow> m_mainWindow;
    std::unique_ptr<EncryptedFileMetadata> m_metadataManager;
    FileIconProvider* m_iconProvider;

    // Progress dialogs
    QProgressDialog* m_progressDialog;
    EncryptionProgressDialog* m_encryptionProgressDialog;
    BatchDecryptionProgressDialog* m_batchProgressDialog;
    SecureDeletionProgressDialog* m_secureDeletionProgressDialog;

    // Worker threads and objects
    EncryptionWorker* m_worker;
    QThread* m_workerThread;
    
    DecryptionWorker* m_decryptWorker;
    QThread* m_decryptWorkerThread;
    
    TempDecryptionWorker* m_tempDecryptWorker;
    QThread* m_tempDecryptWorkerThread;
    
    BatchDecryptionWorker* m_batchDecryptWorker;
    QThread* m_batchDecryptWorkerThread;
    
    SecureDeletionWorker* m_secureDeletionWorker;
    QThread* m_secureDeletionWorkerThread;

    // Temp file management
    QString m_pendingAppToOpen;
    SafeTimer* m_tempFileCleanupTimer;

    // File metadata and filtering - Thread-safe containers
    ThreadSafeMap<QString, EncryptedFileMetadata::FileMetadata> m_fileMetadataCache;
    ThreadSafeStringList m_currentFilteredFiles;
    bool m_updatingFilters;

    // Tag filter optimization
    SafeTimer* m_tagFilterDebounceTimer;
    static const int TAG_FILTER_DEBOUNCE_DELAY = 150;

    // Thumbnail cache - Thread-safe container
    ThreadSafeMap<QString, QPixmap> m_thumbnailCache;

    // Case-insensitive display name caching
    QMap<QString, QString> m_categoryDisplayNames;
    QMap<QString, QString> m_tagDisplayNames;

    // Search functionality
    QString m_currentSearchText;
    SafeTimer* m_searchDebounceTimer;
    static const int SEARCH_DEBOUNCE_DELAY = 200;

    // Thread safety - mutex for protecting shared state
    mutable QMutex m_stateMutex;

    // Helper functions - File operations
    QString determineFileType(const QString& filePath);
    QString generateRandomFilename(const QString& originalExtension = QString());
    bool checkFilenameExists(const QString& folderPath, const QString& filename);
    QString createTargetPath(const QString& sourceFile, const QString& username);
    QString generateUniqueFilePath(const QString& targetDirectory, const QString& originalFilename);
    QString generateUniqueFilenameInDirectory(const QString& targetDirectory, const QString& originalFilename,
                                              const QStringList& usedFilenames);

    // Helper functions - UI and display
    void showSuccessDialog(const QString& encryptedFile, const QString& originalFile);
    void showMultiFileSuccessDialog(const QStringList& originalFiles,
                                    const QStringList& successfulFiles,
                                    const QStringList& failedFiles);
    void updateButtonStates();
    void updateFileListDisplay();
    void populateCategoriesList();
    void populateTagsList();
    void refreshAfterEncryption(const QString& encryptedFilePath);
    void refreshAfterEdit(const QString& encryptedFilePath);
    void selectCategoryAndFile(const QString& categoryToSelect, const QString& filePathToSelect = QString());
    void removeFileFromCacheAndRefresh(const QString& encryptedFilePath);
    void clearThumbnailCache();
    void analyzeCaseInsensitiveDisplayNames();

    // Helper functions - Mapping and conversion
    QString mapSortTypeToDirectory(const QString& sortType);
    QString mapDirectoryToSortType(const QString& directoryName);
    QString formatFileSize(qint64 bytes);

    // Helper functions - Icon management
    QPixmap getIconForFileType(const QString& originalFilename, const QString& fileType);
    void cleanupImageViewerTracking();

    // Helper functions - File opening
    QString checkDefaultApp(const QString& extension);
    enum class AppChoice { Cancel, UseDefault, SelectApp };
    AppChoice showDefaultAppDialog(const QString& appName);
    AppChoice showNoDefaultAppDialog();
    QString selectApplication();
    QString createTempFilePath(const QString& originalFilename);
    void openFileWithApp(const QString& tempFile, const QString& appPath);
    void showWindowsOpenWithDialog(const QString& tempFilePath);
    bool isImageFile(const QString& filename) const;
    void openWithImageViewer(const QString& encryptedFilePath, const QString& originalFilename);

    // Helper functions - Temp file management
    void startTempFileMonitoring();
    void cleanupTempFiles();
    bool isFileInUse(const QString& filePath);
    QString getTempDecryptDir();

    // Helper functions - Context menu
    void showContextMenu_FileList(const QPoint& pos);

    // Helper functions - Batch operations
    QList<FileExportInfo> enumerateAllEncryptedFiles();
    QList<FileExportInfo> enumerateVisibleEncryptedFiles();

    // Helper functions - Secure deletion
    DeletionType showDeletionTypeDialog();
    bool validateExternalItem(const QString& itemPath, bool isFolder);
    qint64 calculateItemSize(const QString& itemPath, bool isFolder, int& fileCount);
    bool showDeletionConfirmationDialog(const QList<DeletionItem>& items);
    void showDeletionResultsDialog(const DeletionResult& result);

    // Helper functions - Metadata repair
    void repairCorruptedMetadata();
    QStringList scanForCorruptedMetadata();
    bool showMetadataRepairDialog(int corruptedCount);
    bool repairMetadataFiles(const QStringList& corruptedFiles);
    bool repairSingleFileMetadata(const QString& encryptedFilePath);

    // Helper functions - Video player
    bool isVideoFile(const QString& filename) const;
    void openWithVideoPlayer(const QString& encryptedFilePath, const QString& originalFilename);
    void openWithVRVideoPlayer(const QString& encryptedFilePath, const QString& originalFilename);

    // Helper functions - Filtering
    QStringList parseHiddenItems(const QString& hiddenString);
    bool shouldHideFileByCategory(const EncryptedFileMetadata::FileMetadata& metadata);
    bool shouldHideFileByTags(const EncryptedFileMetadata::FileMetadata& metadata);
    bool shouldHideThumbnail(const QString& fileTypeDir);

    // Helper functions - Search
    bool matchesSearchCriteria(const QString& filename, const QString& searchText);
    bool matchesSearchCriteriaWithTags(const EncryptedFileMetadata::FileMetadata& metadata, 
                                       const QString& searchText);

#ifdef QT_DEBUG
    bool debugCorruptFileMetadata(const QString& encryptedFilePath);
#endif
};

#endif // OPERATIONS_ENCRYPTEDDATA_H
