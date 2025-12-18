#include "operations_encrypteddata.h"
#include "../videoplayer/BaseVideoPlayer.h"
#include "../videoplayer/vrplayer/vr_video_player.h"
#include "CryptoUtils.h"
#include "operations_files.h"
#include <memory>
#include "constants.h"
#include "encryptedfileitemwidget.h"
#include "encrypteddata_fileiconprovider.h"
#include "imageviewer.h"
#include "BaseVideoPlayer.h"
#include "encrypteddata_editencryptedfiledialog.h"
#include "ui_mainwindow.h"
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QThread>
#include <QMutexLocker>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QRegularExpression>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QApplication>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QStandardPaths>
#include <QPainter>

// Windows-specific includes for file association checking
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>  // For SHOpenFolderAndSelectItems
#include <QSettings>
#endif

// Windows-specific helper function
#ifdef Q_OS_WIN
QString getOpenWithExePath()
{
    wchar_t systemDir[MAX_PATH] = {0};
    UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (len == 0 || len > MAX_PATH)
        return QString(); // Failed

    return QString::fromWCharArray(systemDir) + "\\OpenWith.exe";
}
#endif

// ============================================================================
// Operations_EncryptedData Implementation
// ============================================================================

Operations_EncryptedData::Operations_EncryptedData(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_progressDialog(nullptr)
    , m_encryptionProgressDialog(nullptr)
    , m_worker(nullptr)
    , m_workerThread(nullptr)
    , m_decryptWorker(nullptr)
    , m_decryptWorkerThread(nullptr)
    , m_tempDecryptWorker(nullptr)
    , m_tempDecryptWorkerThread(nullptr)
    , m_tempFileCleanupTimer(nullptr)
    , m_updatingFilters(false)
    , m_batchDecryptWorker(nullptr)
    , m_batchDecryptWorkerThread(nullptr)
    , m_batchProgressDialog(nullptr)
    , m_secureDeletionWorker(nullptr)
    , m_secureDeletionWorkerThread(nullptr)
    , m_secureDeletionProgressDialog(nullptr)
    , m_fileMetadataCache(100000, "Operations_EncryptedData::FileMetadataCache") // Thread-safe with max 100k files
    , m_currentFilteredFiles(50000, "Operations_EncryptedData::CurrentFilteredFiles") // Thread-safe with max 50k filtered files
    , m_thumbnailCache(10000, "Operations_EncryptedData::ThumbnailCache") // Thread-safe with max 10k cached thumbnails
{
    qDebug() << "Operations_EncryptedData: Constructor started";

    // Create MetaData Manager Instance
    m_metadataManager = std::make_unique<EncryptedFileMetadata>(m_mainWindow->user_Key, m_mainWindow->user_Username);

    // Scan for corrupted metadata and prompt user for repairs
    repairCorruptedMetadata();

    // Initialize tag filter debounce timer
    m_tagFilterDebounceTimer = new SafeTimer(this, "Operations_EncryptedData::TagFilterDebounce");
    m_tagFilterDebounceTimer->setSingleShot(true);
    m_tagFilterDebounceTimer->setInterval(TAG_FILTER_DEBOUNCE_DELAY);

    // Initialize search functionality
    m_currentSearchText = "";
    m_searchDebounceTimer = new SafeTimer(this, "Operations_EncryptedData::SearchDebounce");
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(SEARCH_DEBOUNCE_DELAY);

    // Connect search bar text changes
    connect(m_mainWindow->ui->lineEdit_DataENC_SearchBar, &QLineEdit::textChanged,
            this, &Operations_EncryptedData::onSearchTextChanged);

    // Connect enter with search bar to stop debounce and update immediately
    connect(m_mainWindow->ui->lineEdit_DataENC_SearchBar, &QLineEdit::returnPressed, [this]() {
        // Stop debounce timer and update immediately
        m_searchDebounceTimer->stop();
        updateFileListDisplay();
    });

    // Install event filter on search bar for escape/delete
    m_mainWindow->ui->lineEdit_DataENC_SearchBar->installEventFilter(this);

    // Connect selection changed signal to update button states
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::itemSelectionChanged,
            this, &Operations_EncryptedData::updateButtonStates);

    // Connect double-click signal
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::itemDoubleClicked,
            this, &Operations_EncryptedData::onFileListDoubleClicked);

    // Install event filter for Delete key functionality
    m_mainWindow->ui->listWidget_DataENC_FileList->installEventFilter(this);

    // Add connections for new filtering system
    connect(m_mainWindow->ui->listWidget_DataENC_Categories, &QListWidget::currentItemChanged,
            this, &Operations_EncryptedData::onCategorySelectionChanged);

    // Set initial button states (disabled since no files loaded yet)
    updateButtonStates();

    // Start temp file monitoring
    startTempFileMonitoring();

    // Clean up any orphaned temp files from previous sessions
    cleanupTempFiles();

    // Initialize icon provider (still needed for default icons and video thumbnail extraction)
    qDebug() << "Operations_EncryptedData: About to create FileIconProvider...";
    m_iconProvider = new FileIconProvider(this);
    qDebug() << "Operations_EncryptedData: FileIconProvider created, address:" << m_iconProvider;

    // Set up context menu for the encrypted files list
    m_mainWindow->ui->listWidget_DataENC_FileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::customContextMenuRequested,
            this, &Operations_EncryptedData::showContextMenu_FileList);

    onSortTypeChanged("All");

    qDebug() << "Operations_EncryptedData: Constructor completed";
}

Operations_EncryptedData::~Operations_EncryptedData()
{
    qDebug() << "Operations_EncryptedData: Destructor started";

    // Stop the cleanup timer
    if (m_tempFileCleanupTimer) {
        m_tempFileCleanupTimer->stop();
        delete m_tempFileCleanupTimer;
        m_tempFileCleanupTimer = nullptr;
    }

    // Handle encryption worker
    if (m_worker) {
        // CRITICAL: Disconnect signals BEFORE cancelling to prevent race conditions
        disconnect(m_worker, nullptr, this, nullptr);  // Disconnect all signals to this object
        disconnect(m_worker, nullptr, nullptr, nullptr);  // Disconnect all remaining signals
        m_worker->cancel();
    }
    
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        if (!m_workerThread->wait(10000)) {  // Wait 10 seconds
            qWarning() << "Operations_EncryptedData: Encryption worker thread failed to stop gracefully";
            m_workerThread->terminate();
            if (!m_workerThread->wait(2000)) {
                qCritical() << "Operations_EncryptedData: Failed to terminate encryption worker thread";
            }
        }
    }

    // Handle decryption worker
    if (m_decryptWorker) {
        // CRITICAL: Disconnect signals BEFORE cancelling to prevent race conditions
        disconnect(m_decryptWorker, nullptr, this, nullptr);  // Disconnect all signals to this object
        disconnect(m_decryptWorker, nullptr, nullptr, nullptr);  // Disconnect all remaining signals
        m_decryptWorker->cancel();
    }
    
    if (m_decryptWorkerThread && m_decryptWorkerThread->isRunning()) {
        m_decryptWorkerThread->quit();
        if (!m_decryptWorkerThread->wait(10000)) {  // Wait 10 seconds
            qWarning() << "Operations_EncryptedData: Decryption worker thread failed to stop gracefully";
            m_decryptWorkerThread->terminate();
            if (!m_decryptWorkerThread->wait(2000)) {
                qCritical() << "Operations_EncryptedData: Failed to terminate decryption worker thread";
            }
        }
    }

    // Handle temp decryption worker
    if (m_tempDecryptWorker) {
        // CRITICAL: Disconnect signals BEFORE cancelling to prevent race conditions
        disconnect(m_tempDecryptWorker, nullptr, this, nullptr);  // Disconnect all signals to this object
        disconnect(m_tempDecryptWorker, nullptr, nullptr, nullptr);  // Disconnect all remaining signals
        m_tempDecryptWorker->cancel();
    }
    
    if (m_tempDecryptWorkerThread && m_tempDecryptWorkerThread->isRunning()) {
        m_tempDecryptWorkerThread->quit();
        if (!m_tempDecryptWorkerThread->wait(10000)) {  // Wait 10 seconds
            qWarning() << "Operations_EncryptedData: Temp decryption worker thread failed to stop gracefully";
            m_tempDecryptWorkerThread->terminate();
            if (!m_tempDecryptWorkerThread->wait(2000)) {
                qCritical() << "Operations_EncryptedData: Failed to terminate temp decryption worker thread";
            }
        }
    }

    // Handle batch decryption worker
    if (m_batchDecryptWorker) {
        // CRITICAL: Disconnect signals BEFORE cancelling to prevent race conditions
        disconnect(m_batchDecryptWorker, nullptr, this, nullptr);  // Disconnect all signals to this object
        disconnect(m_batchDecryptWorker, nullptr, nullptr, nullptr);  // Disconnect all remaining signals
        m_batchDecryptWorker->cancel();
    }
    
    if (m_batchDecryptWorkerThread && m_batchDecryptWorkerThread->isRunning()) {
        m_batchDecryptWorkerThread->quit();
        if (!m_batchDecryptWorkerThread->wait(10000)) {  // Wait 10 seconds
            qWarning() << "Operations_EncryptedData: Batch decryption worker thread failed to stop gracefully";
            m_batchDecryptWorkerThread->terminate();
            if (!m_batchDecryptWorkerThread->wait(2000)) {
                qCritical() << "Operations_EncryptedData: Failed to terminate batch decryption worker thread";
            }
        }
    }

    // Handle secure deletion worker
    if (m_secureDeletionWorker) {
        // CRITICAL: Disconnect signals BEFORE cancelling to prevent race conditions
        disconnect(m_secureDeletionWorker, nullptr, this, nullptr);  // Disconnect all signals to this object
        disconnect(m_secureDeletionWorker, nullptr, nullptr, nullptr);  // Disconnect all remaining signals
        m_secureDeletionWorker->cancel();
    }
    
    if (m_secureDeletionWorkerThread && m_secureDeletionWorkerThread->isRunning()) {
        m_secureDeletionWorkerThread->quit();
        if (!m_secureDeletionWorkerThread->wait(10000)) {  // Wait 10 seconds
            qWarning() << "Operations_EncryptedData: Secure deletion worker thread failed to stop gracefully";
            m_secureDeletionWorkerThread->terminate();
            if (!m_secureDeletionWorkerThread->wait(2000)) {
                qCritical() << "Operations_EncryptedData: Failed to terminate secure deletion worker thread";
            }
        }
    }

    // Clean up workers and threads
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_decryptWorker) {
        m_decryptWorker->deleteLater();
        m_decryptWorker = nullptr;
    }
    if (m_decryptWorkerThread) {
        m_decryptWorkerThread->deleteLater();
        m_decryptWorkerThread = nullptr;
    }

    if (m_tempDecryptWorker) {
        m_tempDecryptWorker->deleteLater();
        m_tempDecryptWorker = nullptr;
    }
    if (m_tempDecryptWorkerThread) {
        m_tempDecryptWorkerThread->deleteLater();
        m_tempDecryptWorkerThread = nullptr;
    }

    if (m_batchDecryptWorker) {
        m_batchDecryptWorker->deleteLater();
        m_batchDecryptWorker = nullptr;
    }
    if (m_batchDecryptWorkerThread) {
        m_batchDecryptWorkerThread->deleteLater();
        m_batchDecryptWorkerThread = nullptr;
    }

    if (m_secureDeletionWorker) {
        m_secureDeletionWorker->deleteLater();
        m_secureDeletionWorker = nullptr;
    }
    if (m_secureDeletionWorkerThread) {
        m_secureDeletionWorkerThread->deleteLater();
        m_secureDeletionWorkerThread = nullptr;
    }

    // Clean up progress dialogs
    if (m_progressDialog) {
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
    if (m_encryptionProgressDialog) {
        m_encryptionProgressDialog->deleteLater();
        m_encryptionProgressDialog = nullptr;
    }
    if (m_batchProgressDialog) {
        m_batchProgressDialog->deleteLater();
        m_batchProgressDialog = nullptr;
    }
    if (m_secureDeletionProgressDialog) {
        m_secureDeletionProgressDialog->deleteLater();
        m_secureDeletionProgressDialog = nullptr;
    }

    // Clean up metadata manager (handled automatically by unique_ptr)
    m_metadataManager.reset();

    // Stop and clean up timers
    if (m_tagFilterDebounceTimer) {
        m_tagFilterDebounceTimer->stop();
        delete m_tagFilterDebounceTimer;
        m_tagFilterDebounceTimer = nullptr;
    }
    if (m_searchDebounceTimer) {
        m_searchDebounceTimer->stop();
        delete m_searchDebounceTimer;
        m_searchDebounceTimer = nullptr;
    }

    // Clean up icon provider
    if (m_iconProvider) {
        m_iconProvider->deleteLater();
        m_iconProvider = nullptr;
    }

    qDebug() << "Operations_EncryptedData: Destructor completed";
}

// ============================================================================
// Main Encryption Function
// ============================================================================

void Operations_EncryptedData::encryptSelectedFile()
{
    qDebug() << "Operations_EncryptedData: encryptSelectedFile() called";

    // Open file dialog to select multiple files for encryption
    QStringList filePaths = QFileDialog::getOpenFileNames(
        m_mainWindow,
        "Select Files to Encrypt",
        QString(),
        "All Files (*.*)"
    );

    // Check if user selected files (didn't cancel)
    if (filePaths.isEmpty()) {
        qDebug() << "Operations_EncryptedData: User cancelled file selection";
        return;
    }

    qDebug() << "Operations_EncryptedData: Selected" << filePaths.size() << "files for encryption";

    // Validate each file path
    QStringList validFiles;
    QStringList invalidFiles;

    for (const QString& filePath : filePaths) {
        InputValidation::ValidationResult result = InputValidation::validateInput(
            filePath, InputValidation::InputType::ExternalFilePath, 1000);

        if (!result.isValid) {
            invalidFiles.append(QString("%1 (%2)").arg(QFileInfo(filePath).fileName(), result.errorMessage));
            continue;
        }

        // Check if file exists and is readable
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            invalidFiles.append(QString("%1 (cannot be read or does not exist)").arg(fileInfo.fileName()));
            continue;
        }

        // SECURITY: Validate file format matches content
        InputValidation::FileValidationResult formatResult = InputValidation::validateFileFormat(filePath);
        if (!formatResult.isValid) {
            invalidFiles.append(QString("%1 (Invalid format: %2)").arg(fileInfo.fileName(), formatResult.errorMessage));
            qDebug() << "Operations_EncryptedData: File format validation failed:" << filePath << formatResult.errorMessage;
            continue;
        }
        
        if (!formatResult.contentMatchesExtension) {
            // Warning but allow if valid file format detected
            if (formatResult.hasValidHeader) {
                qDebug() << "Operations_EncryptedData: File extension mismatch warning for:" << filePath
                         << "Detected:" << formatResult.detectedMimeType;
                // You could show a warning to user here if desired
            } else {
                invalidFiles.append(QString("%1 (File content does not match extension)").arg(fileInfo.fileName()));
                continue;
            }
        }

        validFiles.append(filePath);
    }

    // Show validation results if there are invalid files
    if (!invalidFiles.isEmpty()) {
        QString message = QString("The following files cannot be encrypted:\n\n%1").arg(invalidFiles.join("\n"));

        if (validFiles.isEmpty()) {
            message += "\n\nNo valid files selected.";
            QMessageBox::warning(m_mainWindow, "Invalid Files", message);
            return;
        } else {
            message += QString("\n\nContinue with %1 valid files?").arg(validFiles.size());
            int ret = QMessageBox::question(m_mainWindow, "Some Invalid Files", message,
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::Yes);
            if (ret != QMessageBox::Yes) {
                return;
            }
        }
    }

    // Extract video thumbnails in main thread before encryption
    QMap<QString, QPixmap> videoThumbnails;
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v",
                                   "3gp", "mpg", "mpeg", "m2v", "divx", "xvid"};

    for (const QString& sourceFile : validFiles) {
        QFileInfo fileInfo(sourceFile);
        QString extension = fileInfo.suffix().toLower();

        // Check if this is a video file
        if (videoExtensions.contains(extension)) {
            qDebug() << "Operations_EncryptedData: Pre-extracting video thumbnail for:" << sourceFile;

            if (m_iconProvider) {
                QPixmap videoThumbnail = m_iconProvider->getVideoThumbnail(sourceFile, 64);

                if (!videoThumbnail.isNull()) {
                    videoThumbnails[sourceFile] = videoThumbnail;
                    qDebug() << "Operations_EncryptedData: Successfully pre-extracted video thumbnail for:"
                             << fileInfo.fileName();
                } else {
                    qDebug() << "Operations_EncryptedData: Failed to pre-extract video thumbnail for:"
                             << fileInfo.fileName();
                }
            } else {
                qWarning() << "Operations_EncryptedData: FileIconProvider not available for video thumbnail extraction";
            }
        }
    }

    // Get username from mainwindow
    QString username = m_mainWindow->user_Username;
    QByteArray encryptionKey = m_mainWindow->user_Key;

    // Create target paths for all valid files
    QStringList targetPaths;
    QStringList failedPaths;

    for (const QString& filePath : validFiles) {
        QString targetPath = createTargetPath(filePath, username);
        if (targetPath.isEmpty()) {
            failedPaths.append(QFileInfo(filePath).fileName());
        } else {
            targetPaths.append(targetPath);
        }
    }

    // Handle files that couldn't get target paths
    if (!failedPaths.isEmpty()) {
        QString message = QString("Failed to create target paths for:\n%1").arg(failedPaths.join("\n"));

        if (targetPaths.isEmpty()) {
            QMessageBox::critical(m_mainWindow, "Error", message + "\n\nNo files can be encrypted.");
            return;
        } else {
            message += QString("\n\nContinue with %1 remaining files?").arg(targetPaths.size());
            int ret = QMessageBox::question(m_mainWindow, "Path Creation Failed", message,
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::Yes);
            if (ret != QMessageBox::Yes) {
                return;
            }

            // Remove failed files from the valid list
            for (int i = validFiles.size() - 1; i >= 0; --i) {
                if (failedPaths.contains(QFileInfo(validFiles[i]).fileName())) {
                    validFiles.removeAt(i);
                }
            }
        }
    }

    // Set up enhanced progress dialog
    m_encryptionProgressDialog = new EncryptionProgressDialog(m_mainWindow);

    // Set initial status based on file count
    if (validFiles.size() == 1) {
        QFileInfo singleFile(validFiles.first());
        m_encryptionProgressDialog->setStatusText(QString("Encrypting: %1").arg(singleFile.fileName()));
        m_encryptionProgressDialog->setFileCountText("File: 1/1");
    } else {
        m_encryptionProgressDialog->setStatusText("Preparing to encrypt files...");
        m_encryptionProgressDialog->setFileCountText(QString("Files: 0/%1").arg(validFiles.size()));
    }

    // Set up worker thread
    m_workerThread = new QThread(this);
    m_worker = new EncryptionWorker(validFiles, targetPaths, encryptionKey, username, videoThumbnails);
    m_worker->moveToThread(m_workerThread);

    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &EncryptionWorker::doEncryption);
    connect(m_worker, &EncryptionWorker::progressUpdated, this, &Operations_EncryptedData::onEncryptionProgress);
    connect(m_encryptionProgressDialog, &EncryptionProgressDialog::cancelled,
            this, &Operations_EncryptedData::onEncryptionCancelled);

    // Connect the file progress update signal (works for both single and multiple files)
    connect(m_worker, &EncryptionWorker::fileProgressUpdate,
            this, &Operations_EncryptedData::onFileProgressUpdate);

    // Connect current file progress signal
    connect(m_worker, &EncryptionWorker::currentFileProgressUpdated,
            this, &Operations_EncryptedData::onCurrentFileProgressUpdate);

    // Connect completion signals based on file count
    if (validFiles.size() == 1) {
        // Single file - use backward compatible signal
        connect(m_worker, &EncryptionWorker::encryptionFinished,
                this, &Operations_EncryptedData::onEncryptionFinished);
    } else {
        // Multiple files - use new signal
        connect(m_worker, &EncryptionWorker::multiFileEncryptionFinished,
                this, &Operations_EncryptedData::onMultiFileEncryptionFinished);
    }

    // Start encryption
    m_workerThread->start();
    m_encryptionProgressDialog->exec();
}

// ============================================================================
// Encryption Slots
// ============================================================================

void Operations_EncryptedData::onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName)
{
    if (m_encryptionProgressDialog) {
        QString statusText = QString("Encrypting: %1").arg(fileName);
        QString fileCountText = QString("File: %1/%2").arg(currentFile).arg(totalFiles);

        m_encryptionProgressDialog->setStatusText(statusText);
        m_encryptionProgressDialog->setFileCountText(fileCountText);

        // Reset file progress for the new file
        m_encryptionProgressDialog->setFileProgress(0);
    }
}

void Operations_EncryptedData::onCurrentFileProgressUpdate(int percentage)
{
    if (m_encryptionProgressDialog) {
        m_encryptionProgressDialog->setFileProgress(percentage);
    }
}

void Operations_EncryptedData::onEncryptionProgress(int percentage)
{
    if (m_encryptionProgressDialog) {
        m_encryptionProgressDialog->setOverallProgress(percentage);
    }
}

void Operations_EncryptedData::onEncryptionFinished(bool success, const QString& errorMessage)
{
    qDebug() << "Operations_EncryptedData: onEncryptionFinished - success:" << success;

    if (m_encryptionProgressDialog) {
        m_encryptionProgressDialog->close();
        m_encryptionProgressDialog = nullptr;
    }

    // Disconnect worker signals before thread cleanup
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
    }

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(5000)) {  // Wait up to 5 seconds
            qWarning() << "Operations_EncryptedData: Worker thread didn't finish cleanly in onEncryptionFinished";
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_worker) {
        // For single file encryption (backward compatibility), access first element from the lists
        QString originalFile = m_worker->getSourceFiles().first();
        QString encryptedFile = m_worker->getTargetFiles().first();

        if (success) {
            // Refresh the list and select appropriate category/file
            refreshAfterEncryption(encryptedFile);
            showSuccessDialog(encryptedFile, originalFile);
        } else {
            QMessageBox::critical(m_mainWindow, "Encryption Failed",
                                  "File encryption failed: " + errorMessage);
        }

        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

void Operations_EncryptedData::onMultiFileEncryptionFinished(bool success, const QString& errorMessage,
                                                             const QStringList& successfulFiles,
                                                             const QStringList& failedFiles)
{
    qDebug() << "Operations_EncryptedData: onMultiFileEncryptionFinished - success:" << success;

    if (m_encryptionProgressDialog) {
        m_encryptionProgressDialog->close();
        m_encryptionProgressDialog = nullptr;
    }

    // Disconnect worker signals before thread cleanup
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
    }

    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(5000)) {  // Wait up to 5 seconds
            qWarning() << "Operations_EncryptedData: Worker thread didn't finish cleanly in onMultiFileEncryptionFinished";
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_worker) {
        if (success && !m_worker->getTargetFiles().isEmpty()) {
            // For multiple files, refresh to show the first successfully encrypted file
            QString firstSuccessfulEncryptedFile;
            QStringList sourceFiles = m_worker->getSourceFiles();
            QStringList targetFiles = m_worker->getTargetFiles();
            for (int i = 0; i < sourceFiles.size(); ++i) {
                QString sourceFileName = QFileInfo(sourceFiles[i]).fileName();
                if (successfulFiles.contains(sourceFileName)) {
                    firstSuccessfulEncryptedFile = targetFiles[i];
                    break;
                }
            }

            if (!firstSuccessfulEncryptedFile.isEmpty()) {
                refreshAfterEncryption(firstSuccessfulEncryptedFile);
            } else {
                // Fallback: just refresh the list normally
                populateEncryptedFilesList();
            }

            // Show success dialog with multiple file handling
            showMultiFileSuccessDialog(m_worker->getSourceFiles(), successfulFiles, failedFiles);
        } else {
            QMessageBox::critical(m_mainWindow, "Encryption Failed", errorMessage);
        }

        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

void Operations_EncryptedData::onEncryptionCancelled()
{
    qDebug() << "Operations_EncryptedData: Encryption cancelled by user";

    if (m_encryptionProgressDialog) {
        m_encryptionProgressDialog->setStatusText("Cancelling...");
    }

    if (m_worker) {
        // Disconnect signals first to prevent race conditions
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->cancel();
    }
}

// ============================================================================
// Decryption Functions
// ============================================================================

void Operations_EncryptedData::decryptSelectedFile()
{
    qDebug() << "Operations_EncryptedData: decryptSelectedFile() called";

    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        QMessageBox::warning(m_mainWindow, "No Selection",
                             "Please select a file to decrypt.");
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get encryption key
    QByteArray encryptionKey = m_mainWindow->user_Key;

    // Validate encryption key before proceeding
    qDebug() << "Operations_EncryptedData: Validating encryption key for file:" << encryptedFilePath;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Operations_EncryptedData: Encryption key validation successful";

    // Get the original filename
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to extract original filename from encrypted file.");
        return;
    }

    // Open save dialog with original filename pre-filled
    QString suggestedDir = QDir::homePath();
    QString targetDirectory = QFileDialog::getExistingDirectory(
        m_mainWindow,
        "Select Directory to Save Decrypted File",
        suggestedDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (targetDirectory.isEmpty()) {
        qDebug() << "Operations_EncryptedData: User cancelled directory selection";
        return;
    }

    // Generate unique target path (automatically handles duplicates)
    QString targetPath = generateUniqueFilePath(targetDirectory, originalFilename);

    // Validate the target path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        targetPath, InputValidation::InputType::ExternalFilePath, 1000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "The generated save path is invalid: " + result.errorMessage);
        return;
    }

    // Inform user if filename was modified due to duplicates
    QFileInfo targetInfo(targetPath);
    if (targetInfo.fileName() != originalFilename) {
        QMessageBox::information(m_mainWindow, "Filename Modified",
                                 QString("A file with the name '%1' already exists.\n\n"
                                         "The file will be saved as '%2' instead.")
                                     .arg(originalFilename)
                                     .arg(targetInfo.fileName()));
    }

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting file...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("File Decryption");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread
    m_decryptWorkerThread = new QThread(this);
    m_decryptWorker = new DecryptionWorker(encryptedFilePath, targetPath, encryptionKey);
    m_decryptWorker->moveToThread(m_decryptWorkerThread);

    // Connect signals
    connect(m_decryptWorkerThread, &QThread::started,
            m_decryptWorker, &DecryptionWorker::doDecryption);
    connect(m_decryptWorker, &DecryptionWorker::progressUpdated,
            this, &Operations_EncryptedData::onDecryptionProgress);
    connect(m_decryptWorker, &DecryptionWorker::decryptionFinished,
            this, &Operations_EncryptedData::onDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled,
            this, &Operations_EncryptedData::onDecryptionCancelled);

    // Start decryption
    m_decryptWorkerThread->start();
    m_progressDialog->exec();
}

void Operations_EncryptedData::openWithVRVideoPlayer(const QString& encryptedFilePath, const QString& originalFilename)
{
    qDebug() << "Operations_EncryptedData: Opening video with VR VideoPlayer:" << originalFilename;

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Validate encryption key before proceeding
    qDebug() << "Operations_EncryptedData: Validating encryption key for VR VideoPlayer:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Operations_EncryptedData: Encryption key validation successful for VR VideoPlayer";

    // Verify this is actually a video file
    if (!isVideoFile(originalFilename)) {
        QMessageBox::warning(m_mainWindow, "Not a Video",
                             "The selected file is not a video file.");
        return;
    }

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store "vrvideoplayer" as the app to open
    {
        QMutexLocker locker(&m_stateMutex);
        m_pendingAppToOpen = "vrvideoplayer";
        qDebug() << "Operations_EncryptedData: Stored 'vrvideoplayer' in m_pendingAppToOpen";
    }

    qDebug() << "Operations_EncryptedData: Starting temporary decryption for VR VideoPlayer";

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting video for VR playback...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("Opening VR Video File");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread for temp decryption
    m_tempDecryptWorkerThread = new QThread(this);
    m_tempDecryptWorker = new TempDecryptionWorker(encryptedFilePath, tempFilePath, encryptionKey);
    m_tempDecryptWorker->moveToThread(m_tempDecryptWorkerThread);

    // Connect signals
    connect(m_tempDecryptWorkerThread, &QThread::started,
            m_tempDecryptWorker, &TempDecryptionWorker::doDecryption);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::progressUpdated,
            this, &Operations_EncryptedData::onTempDecryptionProgress);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::decryptionFinished,
            this, &Operations_EncryptedData::onTempDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled,
            this, &Operations_EncryptedData::onTempDecryptionCancelled);

    // Start decryption
    m_tempDecryptWorkerThread->start();
    m_progressDialog->exec();
}

void Operations_EncryptedData::onDecryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onDecryptionFinished(bool success, const QString& errorMessage)
{
    qDebug() << "Operations_EncryptedData: onDecryptionFinished - success:" << success;

    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    // Disconnect worker signals before thread cleanup
    if (m_decryptWorker) {
        disconnect(m_decryptWorker, nullptr, this, nullptr);
    }

    if (m_decryptWorkerThread) {
        m_decryptWorkerThread->quit();
        if (!m_decryptWorkerThread->wait(5000)) {  // Wait up to 5 seconds
            qWarning() << "Operations_EncryptedData: Worker thread didn't finish cleanly in onDecryptionFinished";
            m_decryptWorkerThread->terminate();
            m_decryptWorkerThread->wait(1000);
        }
        m_decryptWorkerThread->deleteLater();
        m_decryptWorkerThread = nullptr;
    }

    if (m_decryptWorker) {
        QString encryptedFile = m_decryptWorker->getSourceFile();
        QString decryptedFile = m_decryptWorker->getTargetFile();

        if (success) {
            // Show success dialog and ask about deleting encrypted file
            QFileInfo decryptedFileInfo(decryptedFile);

            QMessageBox msgBox(m_mainWindow);
            msgBox.setWindowTitle("Decryption Complete");
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setText("File decrypted successfully!");
            msgBox.setInformativeText(QString("The file has been decrypted and saved as:\n%1\n\n"
                                              "Would you like to delete the encrypted copy?")
                                          .arg(decryptedFileInfo.fileName()));

            QPushButton* deleteButton = msgBox.addButton("Delete Encrypted Copy", QMessageBox::YesRole);
            QPushButton* keepButton = msgBox.addButton("Keep Encrypted Copy", QMessageBox::NoRole);
            msgBox.setDefaultButton(keepButton);

            msgBox.exec();

            if (msgBox.clickedButton() == deleteButton) {
                // Delete the encrypted file
                if (QFile::remove(encryptedFile)) {
                    QMessageBox::information(m_mainWindow, "File Deleted",
                                             "The encrypted copy has been deleted.");
                    // Refresh the file list
                    populateEncryptedFilesList();
                } else {
                    QMessageBox::warning(m_mainWindow, "Deletion Failed",
                                         "Failed to delete the encrypted copy.");
                }
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Decryption Failed",
                                  "Failed to decrypt the file: " + errorMessage);
        }

        m_decryptWorker->deleteLater();
        m_decryptWorker = nullptr;
    }
}

void Operations_EncryptedData::onDecryptionCancelled()
{
    qDebug() << "Operations_EncryptedData: Decryption cancelled by user";

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }

    if (m_decryptWorker) {
        // Disconnect signals first to prevent race conditions
        disconnect(m_decryptWorker, nullptr, this, nullptr);
        m_decryptWorker->cancel();
    }
}

// ============================================================================
// Double-click to Open Functionality
// ============================================================================
void Operations_EncryptedData::onFileListDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    qDebug() << "Operations_EncryptedData: File double-clicked";

    // Get the encrypted file path and original filename
    QString encryptedFilePath = item->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get the original filename
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to extract original filename from encrypted file.");
        return;
    }

    // Extract file extension
    QFileInfo fileInfo(originalFilename);
    QString extension = fileInfo.suffix().toLower();

    if (extension.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "No File Extension",
                             "The file has no extension. Cannot determine default application.");
        return;
    }

    // Validate encryption key before proceeding with file opening
    qDebug() << "Operations_EncryptedData: Validating encryption key for double-click open:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Operations_EncryptedData: Encryption key validation successful for double-click open";

    // Check for default application
    QString defaultApp = checkDefaultApp(extension);
    QString appToUse;

    qDebug() << "Operations_EncryptedData: File extension:" << extension;
    qDebug() << "Operations_EncryptedData: Default app found:" << (defaultApp.isEmpty() ? "None" : defaultApp);

    // Check if this is a video file - if so, use BaseVideoPlayer
    if (isVideoFile(originalFilename)) {
        qDebug() << "Operations_EncryptedData: Video file detected, using BaseVideoPlayer";
        openWithVideoPlayer(encryptedFilePath, originalFilename);
        return; // Exit early, BaseVideoPlayer will handle everything
    }

    if (defaultApp.isEmpty()) {
        // No default app - check if this is an image file
        if (isImageFile(originalFilename)) {
            qDebug() << "Operations_EncryptedData: No default app for image, using ImageViewer";
            // For images with no default app, use ImageViewer directly
            openWithImageViewer(encryptedFilePath, originalFilename);
            return; // Exit early, ImageViewer will handle everything
        } else {
            // Not an image - show simplified dialog (Cancel or Select App)
            AppChoice choice = showNoDefaultAppDialog();
            qDebug() << "Operations_EncryptedData: No default app dialog choice (int):" << static_cast<int>(choice);

            if (choice == AppChoice::Cancel) {
                qDebug() << "Operations_EncryptedData: User cancelled - no default app dialog";
                return;
            } else if (choice == AppChoice::SelectApp) {
                qDebug() << "Operations_EncryptedData: User chose to select app - will use Windows Open With dialog";
                appToUse = "openwith"; // Use Windows native Open With dialog
            }
        }
    } else {
        // Default app exists - use it automatically
        appToUse = "default";
        qDebug() << "Operations_EncryptedData: Using default app automatically:" << defaultApp;
    }

    // Safety check
    if (appToUse.isEmpty()) {
        qDebug() << "Operations_EncryptedData: ERROR: appToUse is still empty after dialog logic!";
        QMessageBox::critical(m_mainWindow, "Error",
                              "No application was selected to open the file.");
        return;
    }

    qDebug() << "Operations_EncryptedData: Final appToUse value before proceeding:" << appToUse;

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store the app to open for later use after decryption
    {
        QMutexLocker locker(&m_stateMutex);
        m_pendingAppToOpen = appToUse;
        qDebug() << "Operations_EncryptedData: Stored in m_pendingAppToOpen:" << m_pendingAppToOpen;
    }

    qDebug() << "Operations_EncryptedData: Starting temporary decryption with validated encryption key";

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting file for opening...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("Opening Encrypted File");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread for temp decryption
    m_tempDecryptWorkerThread = new QThread(this);
    m_tempDecryptWorker = new TempDecryptionWorker(encryptedFilePath, tempFilePath, encryptionKey);
    m_tempDecryptWorker->moveToThread(m_tempDecryptWorkerThread);

    // Connect signals
    connect(m_tempDecryptWorkerThread, &QThread::started,
            m_tempDecryptWorker, &TempDecryptionWorker::doDecryption);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::progressUpdated,
            this, &Operations_EncryptedData::onTempDecryptionProgress);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::decryptionFinished,
            this, &Operations_EncryptedData::onTempDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled,
            this, &Operations_EncryptedData::onTempDecryptionCancelled);

    // Start decryption
    m_tempDecryptWorkerThread->start();
    m_progressDialog->exec();
}

// ============================================================================
// Temp Decryption Slots
// ============================================================================
void Operations_EncryptedData::onTempDecryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onTempDecryptionFinished(bool success, const QString& errorMessage)
{
    qDebug() << "Operations_EncryptedData: === onTempDecryptionFinished called ===";
    qDebug() << "Operations_EncryptedData: Success:" << success;
    
    // Store the app choice in a local variable immediately to prevent it from being cleared
    QString localAppToOpen;
    {
        QMutexLocker locker(&m_stateMutex);
        qDebug() << "Operations_EncryptedData: m_pendingAppToOpen at start:" << m_pendingAppToOpen;
        localAppToOpen = m_pendingAppToOpen;
        qDebug() << "Operations_EncryptedData: Stored in localAppToOpen:" << localAppToOpen;
    }

    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    // Disconnect worker signals before thread cleanup
    if (m_tempDecryptWorker) {
        disconnect(m_tempDecryptWorker, nullptr, this, nullptr);
    }

    if (m_tempDecryptWorkerThread) {
        m_tempDecryptWorkerThread->quit();
        if (!m_tempDecryptWorkerThread->wait(5000)) {  // Wait up to 5 seconds
            qWarning() << "Operations_EncryptedData: Worker thread didn't finish cleanly in onTempDecryptionFinished";
            m_tempDecryptWorkerThread->terminate();
            m_tempDecryptWorkerThread->wait(1000);
        }
        m_tempDecryptWorkerThread->deleteLater();
        m_tempDecryptWorkerThread = nullptr;
    }

    if (m_tempDecryptWorker) {
        QString tempFilePath = m_tempDecryptWorker->getTargetFile();
        qDebug() << "Operations_EncryptedData: Got tempFilePath:" << tempFilePath;

        if (success) {
            // Add a brief delay to ensure file system operations are complete
            QCoreApplication::processEvents();
            QThread::msleep(200);

            // Verify file exists and has content before trying to open
            QFileInfo fileInfo(tempFilePath);
            qDebug() << "Operations_EncryptedData: Temp decryption finished. File:" << tempFilePath;
            qDebug() << "Operations_EncryptedData: File exists:" << fileInfo.exists()
                     << "Size:" << fileInfo.size() << "bytes";

            if (!fileInfo.exists() || fileInfo.size() == 0) {
                QMessageBox::critical(m_mainWindow, "File Error",
                                      QString("The decrypted temporary file is missing or empty.\n\n"
                                              "Expected location: %1").arg(tempFilePath));
            } else {
                // Handle ImageViewer case
                if (localAppToOpen == "imageviewer") {
                    qDebug() << "Operations_EncryptedData: Opening with ImageViewer:" << tempFilePath;

                    // Create ImageViewer instance and open the image
                    ImageViewer* viewer = new ImageViewer(m_mainWindow);

                    // Call the file path overload (single parameter) for proper GIF detection
                    if (viewer->loadImage(tempFilePath)) {
                        viewer->show();
                        qDebug() << "Operations_EncryptedData: ImageViewer opened successfully";
                    } else {
                        QMessageBox::critical(m_mainWindow, "Image Viewer Error",
                                              "Failed to load the image in the Image Viewer.");
                        viewer->deleteLater();
                    }
                } else if (localAppToOpen == "videoplayer") {
                    qDebug() << "Operations_EncryptedData: Opening with BaseVideoPlayer:" << tempFilePath;

                    // Create BaseVideoPlayer instance without parent for independent window
                    // Get the default volume from MainWindow settings
                    int defaultVolume = m_mainWindow ? m_mainWindow->setting_VP_Shows_DefaultVolume : 100;
                    qDebug() << "Operations_EncryptedData: Using default volume:" << defaultVolume << "%";

                    BaseVideoPlayer* player = new BaseVideoPlayer(nullptr, defaultVolume);
                    
                    // Set auto-delete for this instance since it's not managed by a smart pointer
                    player->setAttribute(Qt::WA_DeleteOnClose);
                    
                    // Load the video file
                    if (player->loadVideo(tempFilePath)) {
                        player->show();
                        
                        // Auto-play the video after loading
                        player->play();
                        qDebug() << "Operations_EncryptedData: BaseVideoPlayer opened successfully and playing";
                        
                        // Player will auto-delete on close due to WA_DeleteOnClose set above
                        // Temp file will be cleaned by the existing cleanup timer
                    } else {
                        QMessageBox::critical(m_mainWindow, "Video Player Error",
                                              "Failed to load the video in the Video Player.");
                        player->deleteLater();
                    }
                } else if (localAppToOpen == "vrvideoplayer") {
                    qDebug() << "Operations_EncryptedData: Opening with VRVideoPlayer:" << tempFilePath;

                    // Create VRVideoPlayer instance without parent for independent window
                    VRVideoPlayer* vrPlayer = new VRVideoPlayer(nullptr);
                    
                    // Set auto-delete for this instance since it's not managed by a smart pointer
                    vrPlayer->setAttribute(Qt::WA_DeleteOnClose);
                    
                    // Load the video file with autoEnterVR=true since user explicitly chose VR player
                    if (vrPlayer->loadVideo(tempFilePath, true)) {
                        vrPlayer->show();

                        qDebug() << "Operations_EncryptedData: VRVideoPlayer opened successfully";
                        
                        // VRVideoPlayer will handle VR availability checking internally
                        // and show appropriate messages if VR is not available
                        
                        // Player will auto-delete on close due to WA_DeleteOnClose set above
                        // Temp file will be cleaned by the existing cleanup timer
                    } else {
                        QMessageBox::critical(m_mainWindow, "VR Video Player Error",
                                              "Failed to load the video in the VR Video Player.");
                        vrPlayer->deleteLater();
                    }
                } else {
                    qDebug() << "Operations_EncryptedData: About to call openFileWithApp with localAppToOpen:"
                             << localAppToOpen;
                    // Use the local variable instead of the member variable
                    openFileWithApp(tempFilePath, localAppToOpen);
                }
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Decryption Failed",
                                  "Failed to decrypt file for opening: " + errorMessage);

            // Clean up the partial temp file if it exists
            if (QFile::exists(tempFilePath)) {
                QFile::remove(tempFilePath);
            }
        }

        m_tempDecryptWorker->deleteLater();
        m_tempDecryptWorker = nullptr;
    }

    // Clear pending app only after we're done using it
    {
        QMutexLocker locker(&m_stateMutex);
        qDebug() << "Operations_EncryptedData: Clearing m_pendingAppToOpen at end of function";
        m_pendingAppToOpen.clear();
    }
}

void Operations_EncryptedData::onTempDecryptionCancelled()
{
    qDebug() << "Operations_EncryptedData: === onTempDecryptionCancelled called ===";

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }

    if (m_tempDecryptWorker) {
        // Disconnect signals first to prevent race conditions
        disconnect(m_tempDecryptWorker, nullptr, this, nullptr);
        m_tempDecryptWorker->cancel();
    }

    // Clear pending app
    {
        QMutexLocker locker(&m_stateMutex);
        m_pendingAppToOpen.clear();
    }
}

// ============================================================================
// File Opening Helper Functions
// ============================================================================
QString Operations_EncryptedData::checkDefaultApp(const QString& extension)
{
#ifdef Q_OS_WIN
    // Check Windows registry for default application
    QSettings regSettings(QString("HKEY_CLASSES_ROOT\\.%1").arg(extension), QSettings::NativeFormat);
    QString fileType = regSettings.value(".", "").toString();

    if (fileType.isEmpty()) {
        return QString(); // No association found
    }

    QSettings appSettings(QString("HKEY_CLASSES_ROOT\\%1\\shell\\open\\command").arg(fileType),
                         QSettings::NativeFormat);
    QString command = appSettings.value(".", "").toString();

    if (command.isEmpty()) {
        return QString(); // No command found
    }

    // Extract application name from command
    // Commands usually look like: "C:\Program Files\App\app.exe" "%1"
    QRegularExpression regex("\"([^\"]+)\"");
    QRegularExpressionMatch match = regex.match(command);
    if (match.hasMatch()) {
        QString appPath = match.captured(1);
        QFileInfo appInfo(appPath);
        return appInfo.baseName(); // Return just the application name
    } else {
        // Try to extract first word as app path
        QStringList parts = command.split(' ', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            QFileInfo appInfo(parts.first());
            return appInfo.baseName();
        }
    }
#endif
    return QString(); // Fallback for non-Windows or if detection fails
}

Operations_EncryptedData::AppChoice Operations_EncryptedData::showDefaultAppDialog(const QString& appName)
{
    qDebug() << "Operations_EncryptedData: Showing default app dialog for app:" << appName;

    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Open Encrypted File");
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(QString("'%1' is set as default for this type of file.").arg(appName));
    msgBox.setInformativeText("Do you want to open it with the default app or select a specific one?");

    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* useDefaultButton = msgBox.addButton("Use Default", QMessageBox::AcceptRole);
    QPushButton* selectAppButton = msgBox.addButton("Select an App", QMessageBox::ActionRole);

    msgBox.setDefaultButton(useDefaultButton);
    msgBox.exec();

    AppChoice choice = AppChoice::Cancel; // Default

    if (msgBox.clickedButton() == cancelButton) {
        choice = AppChoice::Cancel;
    } else if (msgBox.clickedButton() == useDefaultButton) {
        choice = AppChoice::UseDefault;
    } else if (msgBox.clickedButton() == selectAppButton) {
        choice = AppChoice::SelectApp;
    }

    qDebug() << "Operations_EncryptedData: Final default app dialog result:" << static_cast<int>(choice);
    return choice;
}

Operations_EncryptedData::AppChoice Operations_EncryptedData::showNoDefaultAppDialog()
{
    qDebug() << "Operations_EncryptedData: Showing simplified no default app dialog";

    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Open Encrypted File");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("No default app defined for this type of file.");

    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* selectAppButton = msgBox.addButton("Select an App", QMessageBox::AcceptRole);

    msgBox.setDefaultButton(selectAppButton);
    msgBox.exec();

    AppChoice choice = AppChoice::Cancel; // Default

    if (msgBox.clickedButton() == cancelButton) {
        choice = AppChoice::Cancel;
    } else if (msgBox.clickedButton() == selectAppButton) {
        choice = AppChoice::SelectApp;
    }

    qDebug() << "Operations_EncryptedData: Final no default app dialog result:" << static_cast<int>(choice);
    return choice;
}

void Operations_EncryptedData::showWindowsOpenWithDialog(const QString& tempFilePath)
{
#ifdef Q_OS_WIN
    // Ensure file exists and is readable
    QFileInfo fileInfo(tempFilePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        QMessageBox::critical(m_mainWindow, "File Error",
                              "The temporary file could not be accessed.");
        return;
    }

    qDebug() << "Operations_EncryptedData: Showing Windows Open With dialog for:" << tempFilePath;

    // Convert path to native separators
    QString nativePath = QDir::toNativeSeparators(tempFilePath);

    // Method 1: Try using OpenWith.exe (available on Windows Vista+)
    QString openWithPath = getOpenWithExePath();
    QStringList openWithArgs;
    openWithArgs << nativePath;

    if (!openWithPath.isEmpty() && QFile::exists(openWithPath) &&
        QProcess::startDetached(openWithPath, openWithArgs)) {
        qDebug() << "Operations_EncryptedData: Successfully launched OpenWith.exe dialog";
        return;
    }

    qDebug() << "Operations_EncryptedData: OpenWith.exe not available, trying alternative method";

    // Method 2: Use rundll32 with different parameters that show the full dialog
    QString command = "rundll32.exe";
    QStringList args;
    args << "shell32.dll,OpenAs_RunDLLW" << nativePath;

    if (QProcess::startDetached(command, args)) {
        qDebug() << "Operations_EncryptedData: Successfully launched rundll32 OpenAs_RunDLLW dialog";
        return;
    }

    // Method 3: Fallback to the simpler dialog
    args.clear();
    args << "shell32.dll,OpenAs_RunDLL" << nativePath;

    if (QProcess::startDetached(command, args)) {
        qDebug() << "Operations_EncryptedData: Successfully launched rundll32 OpenAs_RunDLL dialog";
        return;
    }

    qWarning() << "Operations_EncryptedData: All Windows Open With methods failed";

    // Final fallback: manual app selection
    QString appPath = selectApplication();
    if (!appPath.isEmpty()) {
        openFileWithApp(tempFilePath, appPath);
    }
#else
    // Non-Windows fallback
    QString appPath = selectApplication();
    if (!appPath.isEmpty()) {
        openFileWithApp(tempFilePath, appPath);
    }
#endif
}

QString Operations_EncryptedData::selectApplication()
{
    qDebug() << "Operations_EncryptedData: Opening application selection dialog";

    QString appPath = QFileDialog::getOpenFileName(
        m_mainWindow,
        "Select Application",
        QString(),
        "Executable Files (*.exe);;All Files (*.*)"
    );

    qDebug() << "Operations_EncryptedData: Application selection result:"
             << (appPath.isEmpty() ? "User cancelled" : appPath);

    // Validate the selected application path
    if (!appPath.isEmpty()) {
        QFileInfo appInfo(appPath);
        if (!appInfo.exists() || !appInfo.isExecutable()) {
            QMessageBox::warning(m_mainWindow, "Invalid Application",
                                 "The selected file is not a valid executable.");
            qDebug() << "Operations_EncryptedData: Invalid application selected:" << appPath;
            return QString();
        }
    }

    return appPath;
}

QString Operations_EncryptedData::createTempFilePath(const QString& originalFilename)
{
    // Get the temp decrypt directory
    QString tempDir = getTempDecryptDir();

    // Ensure temp directory exists
    QDir dir(tempDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Operations_EncryptedData: Failed to create temp decrypt directory:" << tempDir;
            return QString();
        }
    }

    // Extract extension from original filename
    QFileInfo fileInfo(originalFilename);
    QString extension = fileInfo.suffix();

    // Generate obfuscated filename
    QString obfuscatedBaseName = generateRandomFilename("");
    obfuscatedBaseName = obfuscatedBaseName.replace(".mmenc", "");

    // Build final temp filename with original extension
    QString obfuscatedName;
    if (!extension.isEmpty()) {
        obfuscatedName = obfuscatedBaseName + "." + extension;
    } else {
        obfuscatedName = obfuscatedBaseName;
    }

    // Ensure filename is unique in temp directory
    QString finalPath;
    int attempts = 0;
    const int maxAttempts = 100;

    do {
        if (attempts > 0) {
            // Add number suffix for uniqueness
            QString nameWithoutExt = QFileInfo(obfuscatedName).baseName();
            QString finalName = QString("%1_%2").arg(nameWithoutExt).arg(attempts);
            if (!extension.isEmpty()) {
                finalName += "." + extension;
            }
            finalPath = QDir(tempDir).absoluteFilePath(finalName);
        } else {
            finalPath = QDir(tempDir).absoluteFilePath(obfuscatedName);
        }

        attempts++;

        if (attempts > maxAttempts) {
            qWarning() << "Operations_EncryptedData: Failed to generate unique temp filename after"
                       << maxAttempts << "attempts";
            return QString();
        }

    } while (QFile::exists(finalPath));

    return finalPath;
}

void Operations_EncryptedData::openFileWithApp(const QString& tempFile, const QString& appPath)
{
    // Ensure file exists and is readable
    QFileInfo fileInfo(tempFile);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        QMessageBox::critical(m_mainWindow, "File Error",
                              "The temporary file could not be accessed.");
        return;
    }

    qDebug() << "Operations_EncryptedData: Attempting to open file:" << tempFile;
    qDebug() << "Operations_EncryptedData: File size:" << fileInfo.size() << "bytes";
    qDebug() << "Operations_EncryptedData: Using app:"
             << (appPath == "default" ? "default system app" :
                 appPath == "openwith" ? "Windows Open With dialog" : appPath);

    // Handle "Open With" dialog case
    if (appPath == "openwith") {
        showWindowsOpenWithDialog(tempFile);
        return;
    }

    // Safety check for empty app path
    if (appPath.isEmpty()) {
        qDebug() << "Operations_EncryptedData: WARNING: Empty app path, falling back to default app";
        QMessageBox::StandardButton reply = QMessageBox::question(
            m_mainWindow,
            "No Application Selected",
            "No application was selected. Would you like to try opening with the system default application?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );

        if (reply != QMessageBox::Yes) {
            return;
        }

        // Fall back to default app
        QUrl fileUrl = QUrl::fromLocalFile(tempFile);
        QCoreApplication::processEvents();
        QThread::msleep(100);

        if (!QDesktopServices::openUrl(fileUrl)) {
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << "start" << "" << QDir::toNativeSeparators(tempFile);
            if (QProcess::startDetached(command, args)) {
                qDebug() << "Operations_EncryptedData: Opened file with Windows start command:" << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the default application.\n\n"
                                         "File location: %1").arg(tempFile));
        } else {
            qDebug() << "Operations_EncryptedData: Opened file with fallback default app:" << tempFile;
        }
        return;
    }

    if (appPath == "default") {
        // Use system default application
        QUrl fileUrl = QUrl::fromLocalFile(tempFile);
        QCoreApplication::processEvents();
        QThread::msleep(100);

        if (!QDesktopServices::openUrl(fileUrl)) {
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << "start" << "" << QDir::toNativeSeparators(tempFile);
            if (QProcess::startDetached(command, args)) {
                qDebug() << "Operations_EncryptedData: Opened file with fallback Windows start command:"
                         << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the default application.\n\n"
                                         "File location: %1").arg(tempFile));
        } else {
            qDebug() << "Operations_EncryptedData: Opened file with default app:" << tempFile;
        }
    } else {
        // Use specific application
        QStringList arguments;
        arguments << QDir::toNativeSeparators(tempFile);

        // Set working directory to the app's directory
        QFileInfo appInfo(appPath);
        QString workingDir = appInfo.absolutePath();

        QCoreApplication::processEvents();
        QThread::msleep(100);

        qint64 pid = 0;
        bool success = QProcess::startDetached(appPath, arguments, workingDir, &pid);

        if (!success) {
            // Try with quoted path as a single argument
            QStringList quotedArgs;
            quotedArgs << QString("\"%1\"").arg(QDir::toNativeSeparators(tempFile));
            success = QProcess::startDetached(appPath, quotedArgs, workingDir, &pid);
        }

        if (!success) {
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << QString("\"%1\" \"%2\"").arg(
                QDir::toNativeSeparators(appPath),
                QDir::toNativeSeparators(tempFile));

            if (QProcess::startDetached(command, args)) {
                qDebug() << "Operations_EncryptedData: Opened file with Windows cmd command:"
                         << appPath << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the selected application.\n\n"
                                         "App: %1\nFile: %2\n\n"
                                         "Try opening the file manually from the temp folder.")
                                     .arg(appPath, tempFile));
        } else {
            qDebug() << "Operations_EncryptedData: Opened file with app:" << appPath
                     << "file:" << tempFile << "PID:" << pid;
        }
    }
}

QString Operations_EncryptedData::getTempDecryptDir()
{
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString tempPath = QDir(userPath).absoluteFilePath("Temp");
    return QDir(tempPath).absoluteFilePath("tempdecrypt");
}

// ============================================================================
// Icon and Thumbnail Management
// ============================================================================
QPixmap Operations_EncryptedData::getIconForFileType(const QString& originalFilename, const QString& fileType)
{
    if (!m_iconProvider) {
        qWarning() << "Operations_EncryptedData: Icon provider not initialized";
        // Return empty pixmap as fallback
        return QPixmap();
    }
    
    QPixmap icon;
    int iconSize = EncryptedFileItemWidget::getIconSize();

    if (fileType == "Video") {
        icon = m_iconProvider->getDefaultVideoIcon(iconSize);
    } else if (fileType == "Image") {
        icon = m_iconProvider->getDefaultImageIcon(iconSize);
    } else if (fileType == "Audio") {
        icon = m_iconProvider->getDefaultAudioIcon(iconSize);
    } else if (fileType == "Document") {
        icon = m_iconProvider->getDefaultDocumentIcon(iconSize);
    } else if (fileType == "Archive") {
        icon = m_iconProvider->getDefaultArchiveIcon(iconSize);
    } else {
        // Default fallback
        icon = m_iconProvider->getDefaultFileIcon(iconSize);
    }

    // If still null, create a basic pixmap as ultimate fallback
    if (icon.isNull()) {
        qWarning() << "Operations_EncryptedData: Failed to get icon for file type:" << fileType;
        icon = QPixmap(iconSize, iconSize);
        icon.fill(Qt::gray);
    }

    return icon;
}

void Operations_EncryptedData::cleanupImageViewerTracking()
{
    // This function would be called periodically to clean up null QPointers
    // from m_openImageViewers map if such a map exists
    // Note: Based on the code review, this appears to be in operations_diary.cpp
    // not in operations_encrypteddata.cpp, but we add the stub here for completeness
    
    // If you have a m_openImageViewers member, uncomment and adapt:
    /*
    QMutableMapIterator<QString, QPointer<ImageViewer>> it(m_openImageViewers);
    while (it.hasNext()) {
        it.next();
        if (it.value().isNull()) {
            qDebug() << "Operations_EncryptedData: Removing null ImageViewer pointer for:" << it.key();
            it.remove();
        }
    }
    */
}

// ============================================================================
// Temp File Monitoring and Cleanup
// ============================================================================
void Operations_EncryptedData::startTempFileMonitoring()
{
    if (!m_tempFileCleanupTimer) {
        m_tempFileCleanupTimer = new SafeTimer(this, "Operations_EncryptedData::TempFileCleanup");
        m_tempFileCleanupTimer->setInterval(60000); // 1 minute = 60000 ms
        m_tempFileCleanupTimer->start([this]() {
            onCleanupTimerTimeout();
        });
        qDebug() << "Operations_EncryptedData: Started temp file cleanup timer with 1-minute interval";
    }
}

void Operations_EncryptedData::onCleanupTimerTimeout()
{
    cleanupTempFiles();
}

void Operations_EncryptedData::cleanupTempFiles()
{
    QString tempDir = getTempDecryptDir();
    QDir dir(tempDir);

    if (!dir.exists()) {
        return; // No temp directory exists yet
    }

    // Get all files in the temp directory
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    int filesDeleted = 0;
    for (const QFileInfo& fileInfo : fileList) {
        QString filePath = fileInfo.absoluteFilePath();

        // Check if file is in use
        if (!isFileInUse(filePath)) {
            // File is not in use, securely delete it
            if (QFile::remove(filePath)) {
                filesDeleted++;
                qDebug() << "Operations_EncryptedData: Cleaned up temp file:" << filePath;
            } else {
                qWarning() << "Operations_EncryptedData: Failed to clean up temp file:" << filePath;
            }
        } else {
            qDebug() << "Operations_EncryptedData: Temp file still in use:" << filePath;
        }
    }

    if (filesDeleted > 0) {
        qDebug() << "Operations_EncryptedData: Cleanup completed. Deleted" << filesDeleted << "temp files";
    }
}

bool Operations_EncryptedData::isFileInUse(const QString& filePath)
{
#ifdef Q_OS_WIN
    // On Windows, try to open the file with exclusive access
    HANDLE fileHandle = CreateFileA(
        filePath.toLocal8Bit().constData(),
        GENERIC_READ | GENERIC_WRITE,
        0, // No sharing - exclusive access
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (fileHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED) {
            // File is in use
            return true;
        }
        // Other errors might indicate the file doesn't exist or other issues
        return false;
    } else {
        // Successfully opened with exclusive access, file is not in use
        CloseHandle(fileHandle);
        return false;
    }
#else
    // For non-Windows systems, this is a fallback
    QFile file(filePath);
    if (file.open(QIODevice::ReadWrite)) {
        file.close();
        return false; // Not in use
    }
    return true; // Assume in use if we can't open it
#endif
}

// ============================================================================
// Helper Functions
// ============================================================================
QString Operations_EncryptedData::determineFileType(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();

    // Video files
    QStringList videoExtensions = {
        "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp",
        "mpg", "mpeg", "m2v", "divx", "xvid", "asf", "rm", "rmvb", "vob",
        "ts", "mts", "m2ts", "f4v", "ogv", "mxf", "dv", "m1v", "mp2v",
        "3g2", "3gp2", "amv", "dnxhd", "prores"
    };
    if (videoExtensions.contains(extension)) {
        return "Video";
    }

    // Image files
    QStringList imageExtensions = {
        "jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "svg", "ico",
        "webp", "heic", "heif", "raw", "cr2", "nef", "arw", "dng", "psd",
        "xcf", "eps", "ai", "indd"
    };
    if (imageExtensions.contains(extension)) {
        return "Image";
    }

    // Audio files
    QStringList audioExtensions = {
        "mp3", "wav", "flac", "aac", "ogg", "wma", "m4a", "ape", "ac3",
        "dts", "opus", "aiff", "au", "ra", "amr", "3ga", "caf", "m4b",
        "m4p", "m4r", "oga", "mogg", "xm", "it", "s3m", "mod"
    };
    if (audioExtensions.contains(extension)) {
        return "Audio";
    }

    // Document files
    QStringList documentExtensions = {
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf",
        "odt", "ods", "odp", "pages", "numbers", "key", "tex", "md",
        "epub", "mobi", "azw", "azw3", "fb2", "lit", "pdb", "tcr", "lrf"
    };
    if (documentExtensions.contains(extension)) {
        return "Document";
    }

    // Archive files
    QStringList archiveExtensions = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "lzma", "cab",
        "iso", "dmg", "img", "nrg", "mdf", "cue", "bin", "deb", "rpm",
        "pkg", "apk", "ipa", "msi", "exe"
    };
    if (archiveExtensions.contains(extension)) {
        return "Archive";
    }

    // Default to "Other" for unrecognized file types
    return "Other";
}

QString Operations_EncryptedData::generateRandomFilename(const QString& originalExtension)
{
    const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const int length = 32; // Generate a 32-character random string

    QString randomString;
    for (int i = 0; i < length; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        randomString.append(chars[index]);
    }

    // Build filename: randomstring.originalext.mmenc
    if (originalExtension.isEmpty()) {
        return randomString + ".mmenc";
    } else {
        return randomString + "." + originalExtension.toLower() + ".mmenc";
    }
}

bool Operations_EncryptedData::checkFilenameExists(const QString& folderPath, const QString& filename)
{
    QDir dir(folderPath);
    return dir.exists(filename);
}

QString Operations_EncryptedData::createTargetPath(const QString& sourceFile, const QString& username)
{
    // Determine file type
    QString fileType = determineFileType(sourceFile);

    // Create folder path: Data/username/EncryptedData/FileType/
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");
    QString typePath = QDir(encDataPath).absoluteFilePath(fileType);

    // Ensure directories exist
    QDir typeDir(typePath);
    if (!typeDir.exists()) {
        if (!typeDir.mkpath(".")) {
            qWarning() << "Operations_EncryptedData: Failed to create directory:" << typePath;
            return QString();
        }
    }

    // Extract original file extension
    QFileInfo sourceFileInfo(sourceFile);
    QString originalExtension = sourceFileInfo.suffix();

    // Generate unique filename with original extension preserved
    QString filename;
    int attempts = 0;
    const int maxAttempts = 100;

    do {
        filename = generateRandomFilename(originalExtension);
        attempts++;

        if (attempts > maxAttempts) {
            qWarning() << "Operations_EncryptedData: Failed to generate unique filename after"
                       << maxAttempts << "attempts";
            return QString();
        }

    } while (checkFilenameExists(typePath, filename));

    return QDir(typePath).absoluteFilePath(filename);
}

// ============================================================================
// Success and Result Dialogs
// ============================================================================
void Operations_EncryptedData::showSuccessDialog(const QString& encryptedFile, const QString& originalFile)
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Encryption Complete");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("The file has been encrypted and saved securely.\n\n"
                   "Choose how to handle the original unencrypted file:");

    QPushButton* deleteButton = msgBox.addButton("Delete Files", QMessageBox::ActionRole);
    QPushButton* safeDeleteButton = msgBox.addButton("Safe Delete Files", QMessageBox::ActionRole);
    QPushButton* keepButton = msgBox.addButton("Keep Files", QMessageBox::RejectRole);
    msgBox.setDefaultButton(keepButton);

    msgBox.exec();

    // Handle user choice
    if (msgBox.clickedButton() == deleteButton) {
        // Regular deletion
        bool deleted = QFile::remove(originalFile);

        if (deleted) {
            QMessageBox::information(m_mainWindow, "File Deleted",
                                     "The original file has been deleted.");
        } else {
            QMessageBox::warning(m_mainWindow, "Deletion Failed",
                                 "Failed to delete the original file. "
                                 "You may need to delete it manually.");
        }
    } else if (msgBox.clickedButton() == safeDeleteButton) {
        // Secure deletion
        bool deleted = QFile::remove(originalFile); // allow external file

        if (deleted) {
            QMessageBox::information(m_mainWindow, "File Safely Deleted",
                                     "The original file has been securely deleted.");
        } else {
            QMessageBox::warning(m_mainWindow, "Safe Deletion Failed",
                                 "Failed to securely delete the original file. "
                                 "You may need to delete it manually.");
        }
    }
    // If keepButton was clicked, do nothing
}

void Operations_EncryptedData::showMultiFileSuccessDialog(const QStringList& originalFiles,
                                                          const QStringList& successfulFiles, const QStringList& failedFiles)
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Encryption Complete");
    msgBox.setIcon(QMessageBox::Information);

    QString mainText;

    if (failedFiles.isEmpty()) {
        // All files succeeded
        mainText = QString("All %1 files encrypted successfully!\n\n"
                          "Choose how to handle the original unencrypted files:").arg(successfulFiles.size());
    } else if (successfulFiles.isEmpty()) {
        // All files failed
        mainText = QString("Failed to encrypt any files.\n\nFailed files:\n%1").arg(failedFiles.join("\n"));
        msgBox.setText(mainText);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();
        return;
    } else {
        // Partial success
        mainText = QString("Partial success: %1 of %2 files encrypted successfully.\n\n"
                          "Choose how to handle the original files that were successfully encrypted:")
                          .arg(successfulFiles.size()).arg(originalFiles.size());
    }

    msgBox.setText(mainText);

    QPushButton* deleteButton = msgBox.addButton("Delete Files", QMessageBox::ActionRole);
    QPushButton* safeDeleteButton = msgBox.addButton("Safe Delete Files", QMessageBox::ActionRole);
    QPushButton* keepButton = msgBox.addButton("Keep Files", QMessageBox::RejectRole);
    msgBox.setDefaultButton(keepButton);

    msgBox.exec();

    // Handle user choice
    if (msgBox.clickedButton() == deleteButton || msgBox.clickedButton() == safeDeleteButton) {
        bool useSecureDeletion = (msgBox.clickedButton() == safeDeleteButton);

        // Find the original file paths for successfully encrypted files
        QStringList filesToDelete;
        for (const QString& originalFile : originalFiles) {
            QString fileName = QFileInfo(originalFile).fileName();
            if (successfulFiles.contains(fileName)) {
                filesToDelete.append(originalFile);
            }
        }

        if (!filesToDelete.isEmpty()) {
            // Delete the original files using the chosen method
            QStringList deletedFiles;
            QStringList deletionFailures;

            for (const QString& filePath : filesToDelete) {
                bool deleted = false;

                deleted = QFile::remove(filePath);
            }

            // Show deletion results
            QString deletionMessage;
            QString deletionTitle;

            if (useSecureDeletion) {
                deletionTitle = deletionFailures.isEmpty() ? "Files Safely Deleted" : "Partial Safe Deletion";
                if (deletionFailures.isEmpty()) {
                    deletionMessage = QString("All %1 original files have been securely deleted.").arg(deletedFiles.size());
                } else {
                    deletionMessage = QString("Successfully securely deleted %1 files.\n\nFailed to delete:\n%2")
                        .arg(deletedFiles.size())
                        .arg(deletionFailures.join("\n"));
                }
            } else {
                deletionTitle = deletionFailures.isEmpty() ? "Files Deleted" : "Partial Deletion";
                if (deletionFailures.isEmpty()) {
                    deletionMessage = QString("All %1 original files have been deleted.").arg(deletedFiles.size());
                } else {
                    deletionMessage = QString("Successfully deleted %1 files.\n\nFailed to delete:\n%2")
                        .arg(deletedFiles.size())
                        .arg(deletionFailures.join("\n"));
                }
            }

            if (deletionFailures.isEmpty()) {
                QMessageBox::information(m_mainWindow, deletionTitle, deletionMessage);
            } else {
                QMessageBox::warning(m_mainWindow, deletionTitle, deletionMessage);
            }
        }
    }
    // If keepButton was clicked, do nothing
}


// ============================================================================
// File List Population and Display
// ============================================================================
void Operations_EncryptedData::populateEncryptedFilesList()
{
    qDebug() << "Starting populateEncryptedFilesList with embedded thumbnails and case-insensitive categories/tags";

    // Clear thumbnail cache when repopulating files
    clearThumbnailCache();

    // Prevent recursive updates
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_updatingFilters) {
            return;
        }
        m_updatingFilters = true;

        // Clear case-insensitive display name caches
        m_categoryDisplayNames.clear();
        m_tagDisplayNames.clear();
    }
    
    // Clear thread-safe containers (no mutex needed for these)
    m_fileMetadataCache.clear();
    m_currentFilteredFiles.clear();

    // Get current sort type from combo box
    QString currentSortType = m_mainWindow->ui->comboBox_DataENC_SortType->currentText();
    QString username = m_mainWindow->user_Username;

    qDebug() << "Scanning files for user:" << username << "sort type:" << currentSortType;

    // Build base path to encrypted data
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");

    QDir encDataDir(encDataPath);
    if (!encDataDir.exists()) {
        qDebug() << "EncryptedData directory doesn't exist for user:" << username;
        {
            QMutexLocker locker(&m_stateMutex);
            m_updatingFilters = false;
        }
        return;
    }

    // Determine directories to scan based on file type filter
    QStringList directoriesToScan;
    if (currentSortType == "All") {
        directoriesToScan << "Document" << "Image" << "Audio" << "Video" << "Archive" << "Other";
    } else {
        QString mappedDirectory = mapSortTypeToDirectory(currentSortType);
        directoriesToScan << mappedDirectory;
    }

    // Scan each directory for encrypted files and load metadata
    for (const QString& dirName : directoriesToScan) {
        QString dirPath = QDir(encDataPath).absoluteFilePath(dirName);
        QDir dir(dirPath);

        if (!dir.exists()) {
            continue;
        }

        QStringList filters;
        filters << "*.mmenc";
        QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);

        for (const QFileInfo& fileInfo : fileList) {
            QString encryptedFilePath = fileInfo.absoluteFilePath();

            // Try to read metadata for this file (now includes embedded thumbnail)
            EncryptedFileMetadata::FileMetadata metadata;
            if (m_metadataManager && m_metadataManager->readMetadataFromFile(encryptedFilePath, metadata)) {
                // Valid metadata found - use thread-safe insert
                m_fileMetadataCache.insert(encryptedFilePath, metadata);

                qDebug() << "Operations_EncryptedData: Loaded metadata for:" << metadata.filename
                         << "category:" << metadata.category
                         << "tags:" << metadata.tags.join(", ")
                         << "has thumbnail:" << (!metadata.thumbnailData.isEmpty());
            } else {
                // Fallback: try to get original filename using legacy method
                QString originalFilename = getOriginalFilename(encryptedFilePath);
                if (!originalFilename.isEmpty()) {
                    // Create basic metadata with just filename
                    metadata = EncryptedFileMetadata::FileMetadata(originalFilename);
                    // Use thread-safe insert
                    m_fileMetadataCache.insert(encryptedFilePath, metadata);

                    qDebug() << "Using legacy filename for:" << originalFilename;
                }
            }
        }
    }

    qDebug() << "Operations_EncryptedData: Loaded metadata for" << m_fileMetadataCache.size() << "files";

    // NEW: Analyze case-insensitive display names after loading all metadata
    analyzeCaseInsensitiveDisplayNames();

    // Populate categories list based on loaded metadata (now case-insensitive)
    populateCategoriesList();

    // Reset category selection to "All"
    if (m_mainWindow->ui->listWidget_DataENC_Categories->count() > 0) {
        m_mainWindow->ui->listWidget_DataENC_Categories->setCurrentRow(0); // "All" is always first
    }

    {
        QMutexLocker locker(&m_stateMutex);
        m_updatingFilters = false;
    }

    // This will trigger onCategorySelectionChanged which will handle the rest
    qDebug() << "Finished populateEncryptedFilesList with case-insensitive analysis, category selection will trigger rest of filtering";
}

QString Operations_EncryptedData::getOriginalFilename(const QString& encryptedFilePath)
{
    if (!m_metadataManager) {
        qWarning() << "Metadata manager not initialized";
        return QString();
    }

    return m_metadataManager->getFilenameFromFile(encryptedFilePath);
}

void Operations_EncryptedData::updateFileListDisplay()
{
    qDebug() << "Updating file list display with embedded thumbnails, hiding settings, case-insensitive filtering, and search";

    // Clear current file list
    m_mainWindow->ui->listWidget_DataENC_FileList->clear();

    // Get checked tags (display names)
    QStringList checkedTagsDisplay;
    for (int i = 0; i < m_mainWindow->ui->listWidget_DataENC_Tags->count(); ++i) {
        QListWidgetItem* item = m_mainWindow->ui->listWidget_DataENC_Tags->item(i);
        if (item && item->checkState() == Qt::Checked) {
            checkedTagsDisplay.append(item->data(Qt::UserRole).toString());
        }
    }

    qDebug() << "Checked tags (display names):" << checkedTagsDisplay;
    qDebug() << "Current search text:" << m_currentSearchText;

    // Filter files by checked tags (AND logic, case-insensitive) and tag hiding settings
    QStringList tagFilteredFiles;
    
    // Get a thread-safe copy of filtered files to iterate over
    QStringList currentFilteredFilesCopy = m_currentFilteredFiles.getCopy();
    
    for (const QString& filePath : currentFilteredFilesCopy) {
        // Check if file exists in cache using thread-safe contains
        if (!m_fileMetadataCache.contains(filePath)) {
            continue;
        }

        // Get metadata using thread-safe value() method
        auto metadataOpt = m_fileMetadataCache.value(filePath);
        if (!metadataOpt.has_value()) {
            continue; // Skip if metadata not found
        }
        const EncryptedFileMetadata::FileMetadata& metadata = metadataOpt.value();

            // Check if file should be hidden by tag settings (case-insensitive)
            if (shouldHideFileByTags(metadata)) {
                continue; // Skip this file, it has hidden tags
            }

            bool includeFile = true;

            if (!checkedTagsDisplay.isEmpty()) {
                // Get tag selection mode from combo box
                QString tagSelectionMode = m_mainWindow->ui->comboBox_DataENC_TagSelectionMode->currentText();
                bool useAndLogic = (tagSelectionMode == "And");

                if (useAndLogic) {
                    // AND logic: File must have ALL selected tags
                    for (const QString& requiredTagDisplay : checkedTagsDisplay) {
                        bool fileHasThisTag = false;

                        // Check if any of the file's tags match this required tag (case-insensitive)
                        for (const QString& fileTag : metadata.tags) {
                            if (fileTag.compare(requiredTagDisplay, Qt::CaseInsensitive) == 0) {
                                fileHasThisTag = true;
                                break;
                            }
                        }

                        if (!fileHasThisTag) {
                            includeFile = false;
                            break; // File doesn't have this required tag
                        }
                    }
                } else {
                    // OR logic: File needs ANY of the selected tags
                    includeFile = false; // Start with false, set to true if any tag matches

                    for (const QString& requiredTagDisplay : checkedTagsDisplay) {
                        // Check if any of the file's tags match this required tag (case-insensitive)
                        for (const QString& fileTag : metadata.tags) {
                            if (fileTag.compare(requiredTagDisplay, Qt::CaseInsensitive) == 0) {
                                includeFile = true;
                                break; // Found a matching tag, include the file
                            }
                        }

                        if (includeFile) {
                            break; // Already found a match, no need to check more tags
                        }
                    }
                }
            }

            if (includeFile) {
                tagFilteredFiles.append(filePath);
            }
        }

        qDebug() << "Tag filtered files count:" << tagFilteredFiles.size()
                 << "(case-insensitive, after applying tag hiding settings, using"
                 << m_mainWindow->ui->comboBox_DataENC_TagSelectionMode->currentText() << "logic)";

    // NEW: Apply search filter to tag-filtered files
    QStringList finalFilteredFiles;
    for (const QString& filePath : tagFilteredFiles) {
        if (!m_fileMetadataCache.contains(filePath)) {
            continue;
        }

        // Get metadata using thread-safe value() method
        auto metadataOpt = m_fileMetadataCache.value(filePath);
        if (!metadataOpt.has_value()) {
            continue;
        }
        const EncryptedFileMetadata::FileMetadata& metadata = metadataOpt.value();

        // Check if filename OR tags match search criteria
        if (matchesSearchCriteriaWithTags(metadata, m_currentSearchText)) {
            finalFilteredFiles.append(filePath);
        }
    }

    qDebug() << "Final filtered files count (after search):" << finalFilteredFiles.size()
             << "Search text: '" << m_currentSearchText << "'";

    // Sort files by encryption date (newest first), with files without date at bottom
    std::sort(finalFilteredFiles.begin(), finalFilteredFiles.end(), [this](const QString& a, const QString& b) {
        // Get metadata using thread-safe value() method
        auto metadataAOpt = m_fileMetadataCache.value(a);
        auto metadataBOpt = m_fileMetadataCache.value(b);
        
        // If either metadata is missing, put it at the end
        if (!metadataAOpt.has_value()) return false;
        if (!metadataBOpt.has_value()) return true;
        
        const EncryptedFileMetadata::FileMetadata& metadataA = metadataAOpt.value();
        const EncryptedFileMetadata::FileMetadata& metadataB = metadataBOpt.value();

        bool hasDateA = metadataA.hasEncryptionDateTime();
        bool hasDateB = metadataB.hasEncryptionDateTime();

        // Files without encryption date go to bottom
        if (!hasDateA && !hasDateB) {
            // Both files have no date - maintain current order (or sort alphabetically)
            return metadataA.filename < metadataB.filename;
        }
        if (!hasDateA && hasDateB) {
            return false; // A goes after B (A has no date)
        }
        if (hasDateA && !hasDateB) {
            return true; // A goes before B (B has no date)
        }

        // Both files have dates - sort by date (newest first)
        return metadataA.encryptionDateTime > metadataB.encryptionDateTime;
    });

    qDebug() << "Sorted files by encryption date (newest first, files without date at bottom)";

    // Create list items for filtered files with thumbnail caching and hiding logic
    for (const QString& encryptedFilePath : finalFilteredFiles) {
        // Get metadata using thread-safe value() method
        auto metadataOpt = m_fileMetadataCache.value(encryptedFilePath);
        if (!metadataOpt.has_value()) {
            qWarning() << "Operations_EncryptedData: Metadata not found for file:" << encryptedFilePath;
            continue; // Skip this file if metadata not found
        }
        const EncryptedFileMetadata::FileMetadata& metadata = metadataOpt.value();

        QFileInfo fileInfo(encryptedFilePath);
        QString fileTypeDir = fileInfo.dir().dirName();

        EncryptedFileItemWidget* customWidget = new EncryptedFileItemWidget(m_mainWindow->ui->listWidget_DataENC_FileList);
        customWidget->setFileInfo(metadata.filename, encryptedFilePath, fileTypeDir, metadata.tags);

        // Thumbnail logic with hiding settings
        QPixmap icon;
        bool hasEmbeddedThumbnail = !metadata.thumbnailData.isEmpty();

        // Check if we should hide thumbnails for this file type
        if (hasEmbeddedThumbnail && shouldHideThumbnail(fileTypeDir)) {
            qDebug() << "Hiding thumbnail for" << fileTypeDir << "file:" << metadata.filename;
            hasEmbeddedThumbnail = false; // Force use of default icon
        }

        if (hasEmbeddedThumbnail) {
            // Check cache first using thread-safe method
            auto cachedIconOpt = m_thumbnailCache.value(encryptedFilePath);
            if (cachedIconOpt.has_value()) {
                icon = cachedIconOpt.value();
                qDebug() << "Operations_EncryptedData: Using cached thumbnail for:" << metadata.filename;
            } else {
                // Decompress and cache the thumbnail
                icon = EncryptedFileMetadata::decompressThumbnail(metadata.thumbnailData);
                if (!icon.isNull()) {
                    int iconSize = EncryptedFileItemWidget::getIconSize();
                    if (icon.width() != iconSize || icon.height() != iconSize) {
                        icon = icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }
                    // Cache the processed thumbnail using thread-safe insert
                    m_thumbnailCache.insert(encryptedFilePath, icon);
                    qDebug() << "Operations_EncryptedData: Decompressed and cached thumbnail for:" << metadata.filename;
                    } else {
                        qWarning() << "Failed to decompress embedded thumbnail for:" << metadata.filename;
                        hasEmbeddedThumbnail = false;
                    }
                }
        }


        // If no embedded thumbnail, thumbnail hiding is active, or decompression failed, use default icon
        if (!hasEmbeddedThumbnail) {
            icon = getIconForFileType(metadata.filename, fileTypeDir);
        }

        customWidget->setIcon(icon);

        // Create list widget item
        QListWidgetItem* item = new QListWidgetItem();
        item->setData(Qt::UserRole, encryptedFilePath);
        item->setData(Qt::UserRole + 1, fileTypeDir);
        item->setData(Qt::UserRole + 2, metadata.filename);

        // Store Encryption datetime for potential future use
        if (metadata.hasEncryptionDateTime()) {
            item->setData(Qt::UserRole + 3, metadata.encryptionDateTime);
        }

        int itemHeight = EncryptedFileItemWidget::getIconSize() + 8;
        item->setSizeHint(QSize(0, itemHeight));

        m_mainWindow->ui->listWidget_DataENC_FileList->addItem(item);
        m_mainWindow->ui->listWidget_DataENC_FileList->setItemWidget(item, customWidget);
    }

    updateButtonStates();
    qDebug() << "File list display updated with" << finalFilteredFiles.size()
             << "items (case-insensitive with thumbnail caching, hiding settings, and search applied)";
}

void Operations_EncryptedData::updateButtonStates()
{
    // Check if any item is selected in the file list
    bool hasSelection = (m_mainWindow->ui->listWidget_DataENC_FileList->currentItem() != nullptr);

    // Style for disabled buttons (same as operations_settings.cpp)
    QString disabledStyle = "color: #888888; background-color: #444444;";
    QString enabledStyle = ""; // Default style

}

void Operations_EncryptedData::refreshAfterEncryption(const QString& encryptedFilePath)
{
    qDebug() << "Refreshing after encryption for file:" << encryptedFilePath;

    // Determine the file type that was just encrypted
    QFileInfo fileInfo(encryptedFilePath);
    QString fileTypeDir = fileInfo.dir().dirName(); // e.g., "Image", "Video", etc.
    QString uiSortType = mapDirectoryToSortType(fileTypeDir);

    // Check if combo box needs to be changed
    QString currentSortType = m_mainWindow->ui->comboBox_DataENC_SortType->currentText();

    if (currentSortType != uiSortType && currentSortType != "All") {
        qDebug() << "Changing sort type from" << currentSortType << "to" << uiSortType;

        // Change combo box, which will trigger onSortTypeChanged() and repopulate everything
        int targetIndex = Operations::GetIndexFromText(uiSortType, m_mainWindow->ui->comboBox_DataENC_SortType);
        if (targetIndex != -1) {
            m_mainWindow->ui->comboBox_DataENC_SortType->setCurrentIndex(targetIndex);
            // This will automatically trigger populateEncryptedFilesList()
        } else {
            qWarning() << "Failed to find combo box index for:" << uiSortType;
            populateEncryptedFilesList();
        }
    } else {
        // Same file type or showing "All", just refresh
        populateEncryptedFilesList();
    }

    // After the list is populated, read the metadata to determine the actual category
    EncryptedFileMetadata::FileMetadata metadata;
    QString categoryToSelect = "Uncategorized"; // Default assumption

    if (m_metadataManager && m_metadataManager->readMetadataFromFile(encryptedFilePath, metadata)) {
        if (metadata.category.isEmpty()) {
            categoryToSelect = "Uncategorized";
        } else {
            categoryToSelect = metadata.category;
        }
        qDebug() << "Detected category for newly encrypted file:" << categoryToSelect;
    } else {
        qDebug() << "Could not read metadata, assuming Uncategorized";
    }

    // Select the appropriate category and file
    selectCategoryAndFile(categoryToSelect, encryptedFilePath);
}

void Operations_EncryptedData::refreshAfterEdit(const QString& encryptedFilePath)
{
    qDebug() << "Refreshing after edit for file:" << encryptedFilePath;

    // Read the updated metadata to determine the new category
    EncryptedFileMetadata::FileMetadata metadata;
    QString categoryToSelect = "Uncategorized"; // Default assumption

    if (m_metadataManager && m_metadataManager->readMetadataFromFile(encryptedFilePath, metadata)) {
        if (metadata.category.isEmpty()) {
            categoryToSelect = "Uncategorized";
        } else {
            categoryToSelect = metadata.category;
        }
        qDebug() << "Detected category for edited file:" << categoryToSelect;
    } else {
        qDebug() << "Could not read metadata, assuming Uncategorized";
    }

    // Refresh the entire list (this will reload metadata cache)
    populateEncryptedFilesList();

    // Select the appropriate category and file
    selectCategoryAndFile(categoryToSelect, encryptedFilePath);
}

void Operations_EncryptedData::selectCategoryAndFile(const QString& categoryToSelect, const QString& filePathToSelect)
{
    qDebug() << "Selecting category:" << categoryToSelect << "and file:" << filePathToSelect;

    // Find and select the category
    QListWidget* categoriesList = m_mainWindow->ui->listWidget_DataENC_Categories;
    bool categoryFound = false;

    for (int i = 0; i < categoriesList->count(); ++i) {
        QListWidgetItem* item = categoriesList->item(i);
        if (item && item->data(Qt::UserRole).toString() == categoryToSelect) {
            categoriesList->setCurrentItem(item);
            categoryFound = true;
            qDebug() << "Selected category:" << categoryToSelect;
            break;
        }
    }

    if (!categoryFound) {
        qWarning() << "Category not found:" << categoryToSelect << "- selecting 'All'";
        if (categoriesList->count() > 0) {
            categoriesList->setCurrentRow(0); // "All" is always first
        }
    }

    // If a specific file should be selected, wait for the category change to propagate, then select it
    if (!filePathToSelect.isEmpty()) {
        // Use a single-shot timer to ensure the file list has been updated after category selection
        SafeTimer::singleShot(50, this, [this, filePathToSelect]() {
            QListWidget* filesList = m_mainWindow->ui->listWidget_DataENC_FileList;

            for (int i = 0; i < filesList->count(); ++i) {
                QListWidgetItem* item = filesList->item(i);
                if (item && item->data(Qt::UserRole).toString() == filePathToSelect) {
                    filesList->setCurrentItem(item);
                    filesList->scrollToItem(item); // Ensure the selected item is visible
                    qDebug() << "Selected file in list:" << filePathToSelect;
                    break;
                }
            }
        }, "Operations_EncryptedData::SelectCategoryAndFile");
    }
}

void Operations_EncryptedData::removeFileFromCacheAndRefresh(const QString& encryptedFilePath)
{
    qDebug() << "Operations_EncryptedData: Removing file from cache and refreshing display:" << encryptedFilePath;

    // Remove from metadata cache using thread-safe method
    if (m_fileMetadataCache.contains(encryptedFilePath)) {
        m_fileMetadataCache.remove(encryptedFilePath);
        qDebug() << "Operations_EncryptedData: Removed file from metadata cache";
    }

    // Remove from current filtered files list using thread-safe method
    m_currentFilteredFiles.removeAll(encryptedFilePath);

    // Check if any categories now have no files and need to be removed
    // We need to rebuild the categories list
    populateCategoriesList();

    // Get the currently selected category to maintain selection if possible
    QString selectedCategory = "All"; // Default fallback
    QListWidgetItem* currentCategoryItem = m_mainWindow->ui->listWidget_DataENC_Categories->currentItem();
    if (currentCategoryItem) {
        selectedCategory = currentCategoryItem->data(Qt::UserRole).toString();
    }

    // Check if the selected category still exists after deletion
    bool categoryStillExists = false;
    for (int i = 0; i < m_mainWindow->ui->listWidget_DataENC_Categories->count(); ++i) {
        QListWidgetItem* item = m_mainWindow->ui->listWidget_DataENC_Categories->item(i);
        if (item && item->data(Qt::UserRole).toString() == selectedCategory) {
            m_mainWindow->ui->listWidget_DataENC_Categories->setCurrentItem(item);
            categoryStillExists = true;
            break;
        }
    }

    // If the category no longer exists, select "All"
    if (!categoryStillExists && m_mainWindow->ui->listWidget_DataENC_Categories->count() > 0) {
        m_mainWindow->ui->listWidget_DataENC_Categories->setCurrentRow(0); // "All" is always first
        selectedCategory = "All";
    }

    // Manually trigger category selection to refresh tags and file list
    onCategorySelectionChanged();
}


// ============================================================================
// Delete Operations
// ============================================================================
void Operations_EncryptedData::deleteSelectedFile()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        QMessageBox::warning(m_mainWindow, "No Selection",
                             "Please select a file to delete.");
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        // Remove from cache and refresh since file is gone
        removeFileFromCacheAndRefresh(encryptedFilePath);
        return;
    }

    // Get the original filename for display in the confirmation dialog
    QString originalFilename;

    // Try to get from cache first (faster) using thread-safe method
    if (m_fileMetadataCache.contains(encryptedFilePath)) {
        auto metadataOpt = m_fileMetadataCache.value(encryptedFilePath);
        if (metadataOpt.has_value()) {
            originalFilename = metadataOpt.value().filename;
        }
    } else {
        // Fallback to reading metadata
        originalFilename = getOriginalFilename(encryptedFilePath);
        if (originalFilename.isEmpty()) {
            // If we can't get the original filename, use the encrypted filename
            QFileInfo fileInfo(encryptedFilePath);
            originalFilename = fileInfo.fileName();
        }
    }

    // Show confirmation dialog
    int ret = QMessageBox::question(
        m_mainWindow,
        "Confirm Deletion",
        QString("Are you sure you want to delete '%1'?").arg(originalFilename),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No // Default to No for safety
        );

    if (ret != QMessageBox::Yes) {
        return; // User cancelled
    }

    // Delete the encrypted file (regular deletion is fine since it's encrypted)
    bool deleted = QFile::remove(encryptedFilePath);

    if (deleted) {
        // Remove from cache and refresh the display
        removeFileFromCacheAndRefresh(encryptedFilePath);
        // Success - no dialog shown, file is just deleted silently
    } else {
        QMessageBox::critical(m_mainWindow, "Deletion Failed",
                              QString("Failed to delete '%1'. The file may be in use or you may not have sufficient permissions.").arg(originalFilename));
    }
}

void Operations_EncryptedData::secureDeleteExternalItems()
{
    qDebug() << "Starting enhanced secure deletion process";

    // Step 1: Show selection type dialog
    DeletionType deletionType = showDeletionTypeDialog();
    if (deletionType == DeletionType::Cancel) {
        qDebug() << "User cancelled deletion type selection";
        return;
    }

    // Step 2: Get user selection based on type
    QList<DeletionItem> itemsToDelete;

    if (deletionType == DeletionType::Files) {
        // Multiple file selection
        QStringList filePaths = QFileDialog::getOpenFileNames(
            m_mainWindow,
            "Select Files to Securely Delete",
            QDir::homePath(),
            "All Files (*.*)"
            );

        if (filePaths.isEmpty()) {
            qDebug() << "User cancelled file selection";
            return;
        }

        qDebug() << "Selected" << filePaths.size() << "files for deletion";

        // Validate and add files
        for (const QString& filePath : filePaths) {
            if (validateExternalItem(filePath, false)) {
                int fileCount = 0;
                qint64 size = calculateItemSize(filePath, false, fileCount);
                QFileInfo fileInfo(filePath);
                itemsToDelete.append(DeletionItem(filePath, fileInfo.fileName(), size, false));
            }
        }

    } else if (deletionType == DeletionType::Folder) {
        // Single folder selection
        QString folderPath = QFileDialog::getExistingDirectory(
            m_mainWindow,
            "Select Folder to Securely Delete",
            QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
            );

        if (folderPath.isEmpty()) {
            qDebug() << "User cancelled folder selection";
            return;
        }

        qDebug() << "Selected folder for deletion:" << folderPath;

        // Validate and add folder
        if (validateExternalItem(folderPath, true)) {
            int fileCount = 0;
            qint64 size = calculateItemSize(folderPath, true, fileCount);
            QFileInfo folderInfo(folderPath);
            itemsToDelete.append(DeletionItem(folderPath, folderInfo.fileName(), size, true));
        }
    }

    // Step 3: Check if we have valid items
    if (itemsToDelete.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "No Valid Items",
                             "No valid items were selected for deletion.");
        return;
    }

    // Step 4: Show confirmation dialog
    if (!showDeletionConfirmationDialog(itemsToDelete)) {
        qDebug() << "User cancelled deletion confirmation";
        return;
    }

    // Step 5: Set up progress dialog
    m_secureDeletionProgressDialog = new SecureDeletionProgressDialog(m_mainWindow);
    m_secureDeletionProgressDialog->setStatusText("Preparing secure deletion...");

    // Step 6: Set up worker thread
    m_secureDeletionWorkerThread = new QThread(this);
    m_secureDeletionWorker = new SecureDeletionWorker(itemsToDelete);
    m_secureDeletionWorker->moveToThread(m_secureDeletionWorkerThread);

    // Connect signals
    connect(m_secureDeletionWorkerThread, &QThread::started,
            m_secureDeletionWorker, &SecureDeletionWorker::doSecureDeletion);
    connect(m_secureDeletionWorker, &SecureDeletionWorker::progressUpdated,
            this, &Operations_EncryptedData::onSecureDeletionProgress);
    connect(m_secureDeletionWorker, &SecureDeletionWorker::currentItemChanged,
            this, &Operations_EncryptedData::onSecureDeletionCurrentItem);
    connect(m_secureDeletionWorker, &SecureDeletionWorker::deletionFinished,
            this, &Operations_EncryptedData::onSecureDeletionFinished);
    connect(m_secureDeletionProgressDialog, &SecureDeletionProgressDialog::cancelled,
            this, &Operations_EncryptedData::onSecureDeletionCancelled);

    // Start deletion
    m_secureDeletionWorkerThread->start();
    m_secureDeletionProgressDialog->exec();
}

DeletionType Operations_EncryptedData::showDeletionTypeDialog()
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Secure Deletion");
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText("What would you like to securely delete?");
    msgBox.setInformativeText("Choose the type of items to delete permanently.");

    QPushButton* filesButton = msgBox.addButton("Files", QMessageBox::ActionRole);
    QPushButton* folderButton = msgBox.addButton("Folder", QMessageBox::ActionRole);
    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);

    msgBox.setDefaultButton(cancelButton);
    msgBox.exec();

    if (msgBox.clickedButton() == filesButton) {
        return DeletionType::Files;
    } else if (msgBox.clickedButton() == folderButton) {
        return DeletionType::Folder;
    } else {
        return DeletionType::Cancel;
    }
}

bool Operations_EncryptedData::validateExternalItem(const QString& itemPath, bool isFolder)
{
    // Validate path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        itemPath, InputValidation::InputType::ExternalFilePath, 1000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Path",
                             QString("Invalid path: %1\n%2").arg(itemPath, result.errorMessage));
        return false;
    }

    // Check existence and type
    QFileInfo itemInfo(itemPath);
    if (!itemInfo.exists()) {
        QMessageBox::warning(m_mainWindow, "Item Not Found",
                             QString("Item does not exist: %1").arg(itemPath));
        return false;
    }

    if (isFolder && !itemInfo.isDir()) {
        QMessageBox::warning(m_mainWindow, "Not a Folder",
                             QString("Selected item is not a folder: %1").arg(itemPath));
        return false;
    }

    if (!isFolder && itemInfo.isDir()) {
        QMessageBox::warning(m_mainWindow, "Not a File",
                             QString("Selected item is not a file: %1").arg(itemPath));
        return false;
    }

    // Check permissions
    if (!itemInfo.isWritable()) {
        QMessageBox::warning(m_mainWindow, "Access Denied",
                             QString("Cannot delete item (read-only or in use): %1").arg(itemPath));
        return false;
    }

    return true;
}

qint64 Operations_EncryptedData::calculateItemSize(const QString& itemPath, bool isFolder, int& fileCount)
{
    qint64 totalSize = 0;
    fileCount = 0;

    try {
        if (isFolder) {
            QDirIterator it(itemPath, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString filePath = it.next();
                QFileInfo fileInfo(filePath);
                totalSize += fileInfo.size();
                fileCount++;
            }
        } else {
            QFileInfo fileInfo(itemPath);
            totalSize = fileInfo.size();
            fileCount = 1;
        }
    } catch (...) {
        qWarning() << "Error calculating size for:" << itemPath;
    }

    return totalSize;
}

bool Operations_EncryptedData::showDeletionConfirmationDialog(const QList<DeletionItem>& items)
{
    // Calculate totals
    qint64 totalSize = 0;
    int totalFiles = 0;

    for (const DeletionItem& item : items) {
        totalSize += item.size;
        if (item.isFolder) {
            int folderFileCount = 0;
            calculateItemSize(item.path, true, folderFileCount);
            totalFiles += folderFileCount;
        } else {
            totalFiles++;
        }
    }

    QString sizeString = formatFileSize(totalSize);

    // Build item list for display
    QStringList displayItems;
    for (const DeletionItem& item : items) {
        if (item.isFolder) {
            int folderFileCount = 0;
            calculateItemSize(item.path, true, folderFileCount);
            displayItems.append(QString("📁 %1 (%2 files)").arg(item.displayName).arg(folderFileCount));
        } else {
            displayItems.append(QString("📄 %1").arg(item.displayName));
        }
    }

    // Show confirmation
    QMessageBox confirmBox(m_mainWindow);
    confirmBox.setWindowTitle("Confirm Secure Deletion");
    confirmBox.setIcon(QMessageBox::Warning);

    QString mainText;
    if (items.size() == 1) {
        if (items.first().isFolder) {
            mainText = QString("Are you sure you want to permanently delete the folder '%1' and all its contents?")
            .arg(items.first().displayName);
        } else {
            mainText = QString("Are you sure you want to permanently delete the file '%1'?")
            .arg(items.first().displayName);
        }
    } else {
        mainText = QString("Are you sure you want to permanently delete %1 items?").arg(items.size());
    }

    confirmBox.setText(mainText);

    QString infoText = QString("Total: %1 files (%2)\n\nThis action cannot be undone. Files will be securely overwritten.")
                           .arg(totalFiles).arg(sizeString);

    if (items.size() <= 10) {
        infoText += "\n\nItems to delete:\n" + displayItems.join("\n");
    }

    confirmBox.setInformativeText(infoText);

    QPushButton* deleteButton = confirmBox.addButton("Delete", QMessageBox::YesRole);
    QPushButton* cancelButton = confirmBox.addButton("Cancel", QMessageBox::NoRole);
    confirmBox.setDefaultButton(cancelButton);

    confirmBox.exec();

    return (confirmBox.clickedButton() == deleteButton);
}

void Operations_EncryptedData::showDeletionResultsDialog(const DeletionResult& result)
{
    QString title;
    QMessageBox::Icon icon;
    QString message;

    if (result.failedItems.isEmpty()) {
        // Complete success
        title = "Deletion Complete";
        icon = QMessageBox::Information;
        message = QString("Successfully deleted %1 files (%2).")
                      .arg(result.totalFiles).arg(formatFileSize(result.totalSize));
    } else if (result.successfulItems.isEmpty()) {
        // Complete failure
        title = "Deletion Failed";
        icon = QMessageBox::Critical;
        message = QString("Failed to delete any items.\n\nFailed items:\n%1")
                      .arg(result.failedItems.join("\n"));
    } else {
        // Partial success
        title = "Deletion Partially Complete";
        icon = QMessageBox::Warning;
        message = QString("Partially completed: %1 items succeeded, %2 items failed.\n\n")
                      .arg(result.successfulItems.size()).arg(result.failedItems.size());

        message += QString("Successfully deleted %1 files (%2).\n\n")
                       .arg(result.totalFiles).arg(formatFileSize(result.totalSize));

        message += QString("Failed items:\n%1").arg(result.failedItems.join("\n"));
    }

    QMessageBox resultBox(m_mainWindow);
    resultBox.setWindowTitle(title);
    resultBox.setIcon(icon);
    resultBox.setText(message);
    resultBox.exec();
}

// ============================================================================
// Secure Deletion Slots
// ============================================================================
void Operations_EncryptedData::onSecureDeletionProgress(int percentage)
{
    if (m_secureDeletionProgressDialog) {
        m_secureDeletionProgressDialog->setOverallProgress(percentage);
    }
}

void Operations_EncryptedData::onSecureDeletionCurrentItem(const QString& itemName)
{
    if (m_secureDeletionProgressDialog) {
        m_secureDeletionProgressDialog->setCurrentItem(itemName);
    }
}

void Operations_EncryptedData::onSecureDeletionFinished(bool success, const DeletionResult& result, const QString& errorMessage)
{
    if (m_secureDeletionProgressDialog) {
        m_secureDeletionProgressDialog->close();
        m_secureDeletionProgressDialog = nullptr;
    }

    if (m_secureDeletionWorkerThread) {
        m_secureDeletionWorkerThread->quit();
        m_secureDeletionWorkerThread->wait();
        m_secureDeletionWorkerThread->deleteLater();
        m_secureDeletionWorkerThread = nullptr;
    }

    if (m_secureDeletionWorker) {
        m_secureDeletionWorker->deleteLater();
        m_secureDeletionWorker = nullptr;
    }

    // Show results
    if (success) {
        showDeletionResultsDialog(result);
    } else {
        QMessageBox::critical(m_mainWindow, "Deletion Failed",
                              "Secure deletion failed: " + errorMessage);
    }
}

void Operations_EncryptedData::onSecureDeletionCancelled()
{
    if (m_secureDeletionWorker) {
        m_secureDeletionWorker->cancel();
    }

    if (m_secureDeletionProgressDialog) {
        m_secureDeletionProgressDialog->setStatusText("Cancelling operation...");
    }
}


// ============================================================================
// Batch Decryption Operations
// ============================================================================
void Operations_EncryptedData::decryptAndExportVisibleFiles()
{
    qDebug() << "Starting batch decrypt and export operation for visible files";

    // Step 1: Get currently visible encrypted files from the UI
    QList<FileExportInfo> visibleFiles = enumerateVisibleEncryptedFiles();

    // Step 2: Check if there are any files to decrypt
    if (visibleFiles.isEmpty()) {
        QMessageBox::information(m_mainWindow, "No Files to Export",
                                 "No files are currently visible to export.\n\n"
                                 "Adjust your filters or add files to see exportable content.");
        return;
    }

    qDebug() << "Found" << visibleFiles.size() << "visible files to decrypt";

    // Step 3: Show folder selection dialog
    QString exportBasePath = QFileDialog::getExistingDirectory(
        m_mainWindow,
        "Select Export Location",
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (exportBasePath.isEmpty()) {
        qDebug() << "User cancelled export location selection";
        return;
    }

    qDebug() << "Export location selected:" << exportBasePath;

    // Step 4: Calculate total size and create target paths with unique naming
    qint64 totalSize = 0;
    QString username = m_mainWindow->user_Username;
    QString decryptedDataPath = QDir(exportBasePath).absoluteFilePath("DecryptedData");

    // Track used filenames per directory to handle duplicates efficiently
    QMap<QString, QStringList> usedFilenamesPerDir;

    for (FileExportInfo& fileInfo : visibleFiles) {
        // Calculate size
        QFileInfo sourceFileInfo(fileInfo.sourceFile);
        fileInfo.fileSize = sourceFileInfo.size();
        totalSize += fileInfo.fileSize;

        // Create target directory path: ExportLocation/DecryptedData/FileType/
        QString targetDirectory = QDir(decryptedDataPath).absoluteFilePath(fileInfo.fileType);

        // Generate unique filename for this directory
        QString uniqueFilename = generateUniqueFilenameInDirectory(
            targetDirectory, fileInfo.originalFilename, usedFilenamesPerDir[targetDirectory]);

        // Add the unique filename to the used list for this directory
        usedFilenamesPerDir[targetDirectory].append(uniqueFilename);

        // Create full target path
        fileInfo.targetFile = QDir(targetDirectory).absoluteFilePath(uniqueFilename);

        qDebug() << "Mapped:" << fileInfo.sourceFile << "=>" << fileInfo.targetFile;
        qDebug() << "File type:" << fileInfo.fileType << "Original name:" << fileInfo.originalFilename;

        if (uniqueFilename != fileInfo.originalFilename) {
            qDebug() << "Filename modified for uniqueness: " << fileInfo.originalFilename << "=>" << uniqueFilename;
        }
    }

    // Step 5: Show confirmation dialog
    QString sizeString = formatFileSize(totalSize);

    // Get current filter information for user context
    QString currentCategory = "All";
    QListWidgetItem* categoryItem = m_mainWindow->ui->listWidget_DataENC_Categories->currentItem();
    if (categoryItem) {
        currentCategory = categoryItem->text();
    }

    // Count selected tags
    int selectedTagCount = 0;
    for (int i = 0; i < m_mainWindow->ui->listWidget_DataENC_Tags->count(); ++i) {
        QListWidgetItem* tagItem = m_mainWindow->ui->listWidget_DataENC_Tags->item(i);
        if (tagItem && tagItem->checkState() == Qt::Checked) {
            selectedTagCount++;
        }
    }

    QString filterInfo;
    if (currentCategory != "All" || selectedTagCount > 0) {
        filterInfo = QString("\n\nCurrent filters:\n• Category: %1").arg(currentCategory);
        if (selectedTagCount > 0) {
            filterInfo += QString("\n• Tags: %1 selected").arg(selectedTagCount);
        }
    } else {
        filterInfo = "\n\nShowing all files (no filters applied).";
    }

    QMessageBox confirmBox(m_mainWindow);
    confirmBox.setWindowTitle("Confirm Export");
    confirmBox.setIcon(QMessageBox::Question);
    confirmBox.setText("You are about to decrypt and export the currently visible files.");
    confirmBox.setInformativeText(QString("%1 file(s) will be decrypted, for a total approximate size of %2.%3\n\n"
                                          "Files with duplicate names will be automatically renamed to prevent overwrites.\n\n"
                                          "Are you sure you wish to continue?")
                                      .arg(visibleFiles.size())
                                      .arg(sizeString)
                                      .arg(filterInfo));

    QPushButton* continueButton = confirmBox.addButton("Continue", QMessageBox::YesRole);
    QPushButton* cancelButton = confirmBox.addButton("Cancel", QMessageBox::NoRole);
    confirmBox.setDefaultButton(continueButton);

    confirmBox.exec();

    if (confirmBox.clickedButton() != continueButton) {
        qDebug() << "User cancelled export operation";
        return;
    }

    // Step 6: Create DecryptedData directory structure (no need to check if it exists - we handle duplicates now)
    QDir exportDir(exportBasePath);
    if (!exportDir.mkpath("DecryptedData")) {
        QMessageBox::critical(m_mainWindow, "Export Failed",
                              "Failed to create DecryptedData directory in the selected location.");
        return;
    }

    // Step 7: Set up progress dialog
    BatchDecryptionProgressDialog* progressDialog = new BatchDecryptionProgressDialog(m_mainWindow);
    progressDialog->setStatusText("Preparing to decrypt files...");

    // Step 8: Set up worker thread
    m_batchDecryptWorkerThread = new QThread(this);
    m_batchDecryptWorker = new BatchDecryptionWorker(visibleFiles, m_mainWindow->user_Key);
    m_batchDecryptWorker->moveToThread(m_batchDecryptWorkerThread);

    // Connect signals
    connect(m_batchDecryptWorkerThread, &QThread::started,
            m_batchDecryptWorker, &BatchDecryptionWorker::doDecryption);
    connect(m_batchDecryptWorker, &BatchDecryptionWorker::overallProgressUpdated,
            this, &Operations_EncryptedData::onBatchDecryptionOverallProgress);
    connect(m_batchDecryptWorker, &BatchDecryptionWorker::fileProgressUpdated,
            this, &Operations_EncryptedData::onBatchDecryptionFileProgress);
    connect(m_batchDecryptWorker, &BatchDecryptionWorker::fileStarted,
            this, &Operations_EncryptedData::onBatchDecryptionFileStarted);
    connect(m_batchDecryptWorker, &BatchDecryptionWorker::batchDecryptionFinished,
            this, &Operations_EncryptedData::onBatchDecryptionFinished);

    connect(progressDialog, &BatchDecryptionProgressDialog::cancelled,
            this, &Operations_EncryptedData::onBatchDecryptionCancelled);

    // Store progress dialog reference for signal handlers
    m_batchProgressDialog = progressDialog;

    // Start decryption
    m_batchDecryptWorkerThread->start();
    progressDialog->exec();
}

QList<FileExportInfo> Operations_EncryptedData::enumerateVisibleEncryptedFiles()
{
    QList<FileExportInfo> visibleFiles;

    // Get files directly from the currently displayed list widget
    QListWidget* fileList = m_mainWindow->ui->listWidget_DataENC_FileList;

    for (int i = 0; i < fileList->count(); ++i) {
        QListWidgetItem* item = fileList->item(i);
        if (!item) {
            continue;
        }

        // Get encrypted file path from item data
        QString encryptedFilePath = item->data(Qt::UserRole).toString();
        QString fileTypeDir = item->data(Qt::UserRole + 1).toString();
        QString originalFilename = item->data(Qt::UserRole + 2).toString();

        if (encryptedFilePath.isEmpty() || originalFilename.isEmpty() || fileTypeDir.isEmpty()) {
            qWarning() << "Incomplete item data for list item" << i;
            continue;
        }

        // Verify file still exists
        if (!QFile::exists(encryptedFilePath)) {
            qWarning() << "Visible file no longer exists:" << encryptedFilePath;
            continue;
        }

        // Create export info structure
        FileExportInfo info;
        info.sourceFile = encryptedFilePath;
        info.originalFilename = originalFilename;
        info.fileType = fileTypeDir;
        // targetFile and fileSize will be set later

        visibleFiles.append(info);
    }

    qDebug() << "Enumerated" << visibleFiles.size() << "visible encrypted files";
    return visibleFiles;
}

QList<FileExportInfo> Operations_EncryptedData::enumerateAllEncryptedFiles()
{
    QList<FileExportInfo> allFiles;
    QString username = m_mainWindow->user_Username;

    // Build path to encrypted data
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");

    QDir encDataDir(encDataPath);
    if (!encDataDir.exists()) {
        qDebug() << "EncryptedData directory doesn't exist for user:" << username;
        return allFiles;
    }

    // Scan all subdirectories
    QStringList typeDirectories = {"Document", "Image", "Audio", "Video", "Archive", "Other"};

    for (const QString& typeDir : typeDirectories) {
        QString typePath = QDir(encDataPath).absoluteFilePath(typeDir);
        QDir dir(typePath);

        if (!dir.exists()) {
            continue;
        }

        QStringList filters;
        filters << "*.mmenc";
        QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);

        for (const QFileInfo& fileInfo : fileList) {
            QString encryptedFilePath = fileInfo.absoluteFilePath();

            // Try to get original filename
            QString originalFilename;
            if (m_metadataManager) {
                originalFilename = m_metadataManager->getFilenameFromFile(encryptedFilePath);
            }

            if (originalFilename.isEmpty()) {
                // Fallback: use encrypted filename without .mmenc
                originalFilename = fileInfo.baseName();
            }

            FileExportInfo info;
            info.sourceFile = encryptedFilePath;
            info.originalFilename = originalFilename;
            info.fileType = typeDir;
            // targetFile will be set later
            // fileSize will be calculated later

            allFiles.append(info);
        }
    }

    qDebug() << "Enumerated" << allFiles.size() << "encrypted files";
    return allFiles;
}


// ============================================================================
// Batch Decryption Slots
// ============================================================================
void Operations_EncryptedData::onBatchDecryptionOverallProgress(int percentage)
{
    if (m_batchProgressDialog) {
        m_batchProgressDialog->setOverallProgress(percentage);
    }
}

void Operations_EncryptedData::onBatchDecryptionFileProgress(int percentage)
{
    if (m_batchProgressDialog) {
        m_batchProgressDialog->setFileProgress(percentage);
    }
}

void Operations_EncryptedData::onBatchDecryptionFileStarted(int currentFile, int totalFiles, const QString& fileName)
{
    if (m_batchProgressDialog) {
        // CHANGED: Use separate labels for filename and file count
        QString statusText = QString("Exporting: %1").arg(fileName);
        QString fileCountText = QString("File: %1/%2").arg(currentFile).arg(totalFiles);

        m_batchProgressDialog->setStatusText(statusText);
        m_batchProgressDialog->setFileCountText(fileCountText);
    }
}

void Operations_EncryptedData::onBatchDecryptionFinished(bool success, const QString& errorMessage,
                                                         const QStringList& successfulFiles,
                                                         const QStringList& failedFiles)
{
    if (m_batchProgressDialog) {
        m_batchProgressDialog->close();
        m_batchProgressDialog = nullptr;
    }

    if (m_batchDecryptWorkerThread) {
        m_batchDecryptWorkerThread->quit();
        m_batchDecryptWorkerThread->wait();
        m_batchDecryptWorkerThread->deleteLater();
        m_batchDecryptWorkerThread = nullptr;
    }

    if (m_batchDecryptWorker) {
        m_batchDecryptWorker->deleteLater();
        m_batchDecryptWorker = nullptr;
    }

    // Show results
    if (success) {
        QString message;
        if (failedFiles.isEmpty()) {
            message = QString("Export completed successfully!\n\nAll %1 visible files were decrypted and exported.\n\n"
                            "Note: Files with duplicate names were automatically renamed to prevent overwrites.")
            .arg(successfulFiles.size());
            QMessageBox::information(m_mainWindow, "Export Complete", message);
        } else {
            message = QString("Export completed with some issues.\n\n%1 files succeeded, %2 files failed.\n\n"
                            "Note: Files with duplicate names were automatically renamed to prevent overwrites.\n\n"
                            "Failed files:\n%3")
            .arg(successfulFiles.size())
                .arg(failedFiles.size())
                .arg(failedFiles.join("\n"));
            QMessageBox::warning(m_mainWindow, "Export Complete with Issues", message);
        }
    } else {
        QMessageBox::critical(m_mainWindow, "Export Failed", "Export failed: " + errorMessage);
    }
}

void Operations_EncryptedData::onBatchDecryptionCancelled()
{
    if (m_batchDecryptWorker) {
        m_batchDecryptWorker->cancel();
    }

    // Fix: Use proper dialog reference for batch decryption
    if (m_batchProgressDialog) {
        m_batchProgressDialog->setStatusText("Cancelling operation...");
    }
}

// ============================================================================
// Context Menu Handling
// ============================================================================
void Operations_EncryptedData::showContextMenu_FileList(const QPoint& pos)
{
    // Get the item at the clicked position
    QListWidgetItem* item = m_mainWindow->ui->listWidget_DataENC_FileList->itemAt(pos);

    if (!item) {
        // No item at this position, don't show context menu
        return;
    }

    // Select the item that was right-clicked
    m_mainWindow->ui->listWidget_DataENC_FileList->setCurrentItem(item);

    // Get the original filename to check if it's an image or video
    QString originalFilename = item->data(Qt::UserRole + 2).toString();
    bool isImage = isImageFile(originalFilename);
    bool isVideo = isVideoFile(originalFilename);

    // Create context menu
    QMenu contextMenu(m_mainWindow);

    // Add Edit action
    QAction* editAction = contextMenu.addAction("Edit");
    editAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(editAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuEdit);

    // Add separator
    contextMenu.addSeparator();

    // Add Open action // disabled
    /*
    QAction* openAction = contextMenu.addAction("Open");
    openAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(openAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuOpen);
    */

    // Add Open With action
    QAction* openWithAction = contextMenu.addAction("Open With...");
    openWithAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(openWithAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuOpenWith);

    // Add "Open With Video Player" action only for videos
    if (isVideo) {
        QAction* videoPlayerAction = contextMenu.addAction("Open with Video Player");
        videoPlayerAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
        connect(videoPlayerAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuOpenWithVideoPlayer);
        
        // Add "Open With VR Video Player" action for videos
        QAction* vrVideoPlayerAction = contextMenu.addAction("Open with VR Video Player");
        vrVideoPlayerAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
        connect(vrVideoPlayerAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuOpenWithVRVideoPlayer);
    }

    // Add "Open With Image Viewer" action only for images
    if (isImage) {
        QAction* imageViewerAction = contextMenu.addAction("Open With Image Viewer");
        imageViewerAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        connect(imageViewerAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuOpenWithImageViewer);
    }

    // Add separator
    contextMenu.addSeparator();

    // Add "Show in File Explorer" action
    QAction* showInExplorerAction = contextMenu.addAction("Show in File Explorer");
    showInExplorerAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(showInExplorerAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuShowInExplorer);

    // Add separator
    contextMenu.addSeparator();

    // Add Decrypt and Export action (single file)
    QAction* decryptAction = contextMenu.addAction("Decrypt and Export");
    decryptAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(decryptAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuDecryptExport);

    // Add Export Listed Files action (all visible files)
    QAction* exportListedAction = contextMenu.addAction("Export All Listed Files");
    exportListedAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    connect(exportListedAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuExportListed);

    // Add separator
    contextMenu.addSeparator();

    // Add Delete action
    QAction* deleteAction = contextMenu.addAction("Delete");
    deleteAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
    connect(deleteAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuDelete);

#ifdef QT_DEBUG
    // Add debug separator and debug options (only in debug builds)
    contextMenu.addSeparator();

    // Add debug section label
    QAction* debugLabelAction = contextMenu.addAction("--- DEBUG OPTIONS ---");
    debugLabelAction->setEnabled(false); // Make it a non-clickable label

    // Add debug corrupt metadata action
    QAction* debugCorruptAction = contextMenu.addAction("DEBUG: Corrupt Metadata");
    debugCorruptAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical));
    connect(debugCorruptAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuDebugCorruptMetadata);
#endif

    // Show context menu at the cursor position
    QPoint globalPos = m_mainWindow->ui->listWidget_DataENC_FileList->mapToGlobal(pos);
    contextMenu.exec(globalPos);
}

void Operations_EncryptedData::onContextMenuOpen()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (currentItem) {
        // Use the existing double-click functionality
        onFileListDoubleClicked(currentItem);
    }
}

void Operations_EncryptedData::onContextMenuOpenWith()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get the original filename
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to extract original filename from encrypted file.");
        return;
    }

    // Validate encryption key before proceeding
    qDebug() << "Validating encryption key for Open With:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful for Open With";

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store "openwith" as the app to open (special marker for Open With dialog)
    {
        QMutexLocker locker(&m_stateMutex);
        m_pendingAppToOpen = "openwith";
        qDebug() << "Stored 'openwith' in m_pendingAppToOpen for Open With dialog";
    }

    qDebug() << "Starting temporary decryption for Open With";

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting file for opening...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("Opening Encrypted File");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread for temp decryption
    m_tempDecryptWorkerThread = new QThread(this);
    m_tempDecryptWorker = new TempDecryptionWorker(encryptedFilePath, tempFilePath, encryptionKey);
    m_tempDecryptWorker->moveToThread(m_tempDecryptWorkerThread);

    // Connect signals
    connect(m_tempDecryptWorkerThread, &QThread::started, m_tempDecryptWorker, &TempDecryptionWorker::doDecryption);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::progressUpdated, this, &Operations_EncryptedData::onTempDecryptionProgress);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::decryptionFinished, this, &Operations_EncryptedData::onTempDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onTempDecryptionCancelled);

    // Start decryption
    m_tempDecryptWorkerThread->start();
    m_progressDialog->exec();
}

void Operations_EncryptedData::onContextMenuEdit()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get encryption key and username
    QByteArray encryptionKey = m_mainWindow->user_Key;
    QString username = m_mainWindow->user_Username;

    // Validate encryption key before proceeding
    qDebug() << "Validating encryption key for edit operation:" << encryptedFilePath;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful for edit operation";

    // NOTE: Thumbnail preservation logic removed - thumbnails are now embedded and preserved automatically

    // Create and show edit dialog
    EditEncryptedFileDialog editDialog(m_mainWindow);
    editDialog.initialize(encryptedFilePath, encryptionKey, username);

    // Show dialog and handle result
    int result = editDialog.exec();

    if (result == QDialog::Accepted) {
        // Changes were saved, refresh and select the edited file
        refreshAfterEdit(encryptedFilePath);

        // NOTE: Thumbnail restoration removed - thumbnails are preserved automatically in metadata

        // Success - no dialog shown, just silently refresh the display
        qDebug() << "File metadata updated successfully, display refreshed";
    } else {
        qDebug() << "Edit dialog cancelled, no changes made";
    }
}

void Operations_EncryptedData::onContextMenuDecryptExport()
{
    // Use the existing decrypt functionality
    decryptSelectedFile();
}

void Operations_EncryptedData::onContextMenuDelete()
{
    // Use the existing delete functionality
    deleteSelectedFile();
}

void Operations_EncryptedData::onContextMenuExportListed()
{
    qDebug() << "Context menu: Export Listed Files triggered";

    // Call the existing function to export all visible files
    decryptAndExportVisibleFiles();
}

void Operations_EncryptedData::onContextMenuOpenWithImageViewer()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path and original filename from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    QString originalFilename = currentItem->data(Qt::UserRole + 2).toString();

    if (encryptedFilePath.isEmpty() || originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve file information.");
        return;
    }

    // Verify this is actually an image file
    if (!isImageFile(originalFilename)) {
        QMessageBox::warning(m_mainWindow, "Not an Image",
                             "The selected file is not an image file.");
        return;
    }

    // Use the ImageViewer opening functionality
    openWithImageViewer(encryptedFilePath, originalFilename);
}

void Operations_EncryptedData::onContextMenuOpenWithVideoPlayer()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path and original filename from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    QString originalFilename = currentItem->data(Qt::UserRole + 2).toString();

    if (encryptedFilePath.isEmpty() || originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve file information.");
        return;
    }

    // Verify this is actually a video file
    if (!isVideoFile(originalFilename)) {
        QMessageBox::warning(m_mainWindow, "Not a Video",
                             "The selected file is not a video file.");
        return;
    }

    // Use the VideoPlayer opening functionality
    openWithVideoPlayer(encryptedFilePath, originalFilename);
}

void Operations_EncryptedData::onContextMenuOpenWithVRVideoPlayer()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path and original filename from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    QString originalFilename = currentItem->data(Qt::UserRole + 2).toString();

    if (encryptedFilePath.isEmpty() || originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve file information.");
        return;
    }

    // Verify this is actually a video file
    if (!isVideoFile(originalFilename)) {
        QMessageBox::warning(m_mainWindow, "Not a Video",
                             "The selected file is not a video file.");
        return;
    }

    // Use the VR VideoPlayer opening functionality
    openWithVRVideoPlayer(encryptedFilePath, originalFilename);
}

void Operations_EncryptedData::onContextMenuShowInExplorer()
{
    qDebug() << "Operations_EncryptedData: Show in File Explorer triggered";

    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

#ifdef Q_OS_WIN
    // Windows-specific implementation to open Explorer and select the file
    QString nativePath = QDir::toNativeSeparators(encryptedFilePath);
    bool explorerOpened = false;
    
    // Method 1: Try using Windows Shell API (most reliable)
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(result) || result == S_FALSE) {  // S_FALSE means already initialized
        // Convert QString to wide string for Windows API
        std::wstring wPath = nativePath.toStdWString();
        
        // Parse the file path to get an ITEMIDLIST
        LPITEMIDLIST pidl = ILCreateFromPathW(wPath.c_str());
        if (pidl) {
            // Open Explorer and select the file
            HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            if (SUCCEEDED(hr)) {
                qDebug() << "Operations_EncryptedData: Successfully opened Explorer with file selected:" << encryptedFilePath;
                explorerOpened = true;
            } else {
                qWarning() << "Operations_EncryptedData: SHOpenFolderAndSelectItems failed with HRESULT:" << hr;
            }
            ILFree(pidl);
        } else {
            qWarning() << "Operations_EncryptedData: Failed to create ITEMIDLIST from path";
        }
        
        if (result != S_FALSE) {
            CoUninitialize();
        }
    }
    
    // Method 2: Fallback to explorer.exe command if Shell API fails
    if (!explorerOpened) {
        QString explorerCommand = "explorer.exe";
        QStringList args;
        args << "/select," + nativePath;
        
        if (QProcess::startDetached(explorerCommand, args)) {
            qDebug() << "Operations_EncryptedData: Opened Explorer with /select command for:" << encryptedFilePath;
        } else {
            QMessageBox::warning(m_mainWindow, "Failed to Open Explorer",
                                 "Could not open File Explorer to show the file.\n\n" +
                                 encryptedFilePath);
        }
    }
#else
    // Non-Windows fallback: just open the containing folder
    QFileInfo fileInfo(encryptedFilePath);
    QString folderPath = fileInfo.absolutePath();
    
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath))) {
        QMessageBox::warning(m_mainWindow, "Failed to Open Folder",
                             "Could not open the folder containing the file.\n\n" +
                             folderPath);
    }
#endif
}


#ifdef QT_DEBUG
void Operations_EncryptedData::onContextMenuDebugCorruptMetadata()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get original filename for display
    QString originalFilename;
    if (m_fileMetadataCache.contains(encryptedFilePath)) {
        auto metadataOpt = m_fileMetadataCache.value(encryptedFilePath);
        if (metadataOpt.has_value()) {
            originalFilename = metadataOpt.value().filename;
        }
    } else {
        originalFilename = getOriginalFilename(encryptedFilePath);
        if (originalFilename.isEmpty()) {
            QFileInfo fileInfo(encryptedFilePath);
            originalFilename = fileInfo.fileName();
        }
    }

    // Show confirmation dialog
    int ret = QMessageBox::question(
        m_mainWindow,
        "DEBUG: Corrupt Metadata",
        QString("Are you sure you want to purposefully corrupt the metadata of '%1'?\n\n"
                "This is for testing purposes only. The file content will remain intact, "
                "but the metadata will become unreadable until repaired.\n\n"
                "Note: The file extension is preserved in the encrypted filename, so repair "
                "will restore a generic filename with the correct extension.").arg(originalFilename),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No // Default to No for safety
        );

    if (ret != QMessageBox::Yes) {
        return; // User cancelled
    }

    // Attempt to corrupt the metadata
    bool corruptionSuccess = debugCorruptFileMetadata(encryptedFilePath);

    if (corruptionSuccess) {
        // Remove from cache and refresh display since metadata is now corrupted
        removeFileFromCacheAndRefresh(encryptedFilePath);

        QMessageBox::information(m_mainWindow, "DEBUG: Corruption Complete",
                                 QString("Metadata for '%1' has been purposefully corrupted.\n\n"
                                         "The file will no longer appear in the list until its metadata is repaired. "
                                         "You can test the repair functionality by restarting the application.\n\n"
                                         "When repaired, the file will have a generic name but keep its original extension.")
                                     .arg(originalFilename));
    } else {
        QMessageBox::critical(m_mainWindow, "DEBUG: Corruption Failed",
                              QString("Failed to corrupt metadata for '%1'. Please check the application logs for details.")
                                  .arg(originalFilename));
    }
}
#endif


// ============================================================================
// Category and Tag Filtering
// ============================================================================
void Operations_EncryptedData::onSortTypeChanged(const QString& sortType)
{
    Q_UNUSED(sortType)
    qDebug() << "Sort type changed, repopulating file list and resetting filters";

    // Temporarily disconnect category selection to prevent intermediate updates
    disconnect(m_mainWindow->ui->listWidget_DataENC_Categories, &QListWidget::currentItemChanged,
               this, &Operations_EncryptedData::onCategorySelectionChanged);

    // Repopulate the list when sort type changes (this will reset category to "All")
    populateEncryptedFilesList();

    // Reconnect category selection
    connect(m_mainWindow->ui->listWidget_DataENC_Categories, &QListWidget::currentItemChanged,
            this, &Operations_EncryptedData::onCategorySelectionChanged);

    // Manually trigger category selection to ensure file list updates
    onCategorySelectionChanged();
}

void Operations_EncryptedData::onCategorySelectionChanged()
{
    if (m_updatingFilters) {
        return;
    }

    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_Categories->currentItem();
    if (!currentItem) {
        qDebug() << "No category selected, clearing lists";
        m_mainWindow->ui->listWidget_DataENC_Tags->clear();
        m_mainWindow->ui->listWidget_DataENC_FileList->clear();
        updateButtonStates();
        return;
    }

    QString selectedCategory = currentItem->data(Qt::UserRole).toString();
    qDebug() << "Category selection changed to:" << selectedCategory;

    // Filter files by selected category (case-insensitive)
    m_currentFilteredFiles.clear();

    // Use thread-safe iteration over metadata cache
    m_fileMetadataCache.safeIterate([this, selectedCategory](const QString& filePath, const EncryptedFileMetadata::FileMetadata& metadata) {

        // First, check if file should be hidden by category settings (case-insensitive)
        if (shouldHideFileByCategory(metadata)) {
            return; // Skip this file, it's in a hidden category (use return instead of continue in lambda)
        }

        bool includeFile = false;

        if (selectedCategory == "All") {
            includeFile = true;
        } else if (selectedCategory == "Uncategorized") {
            includeFile = metadata.category.isEmpty();
        } else {
            // Case-insensitive category comparison
            QString fileCategory = metadata.category.isEmpty() ? "Uncategorized" : metadata.category;
            includeFile = (fileCategory.compare(selectedCategory, Qt::CaseInsensitive) == 0);
        }

        if (includeFile) {
            m_currentFilteredFiles.append(filePath);
        }
    });

    qDebug() << "Operations_EncryptedData: Filtered to" << m_currentFilteredFiles.size() << "files for category:" << selectedCategory
             << "(case-insensitive, after applying category hiding settings)";

    // Populate tags list based on filtered files (case-insensitive)
    populateTagsList();

    // Update file list display
    updateFileListDisplay();
}

void Operations_EncryptedData::onTagSelectionModeChanged(const QString& mode)
{
    Q_UNUSED(mode)
    qDebug() << "Tag selection mode changed to:" << mode;

    // Update the file list display with the new tag logic
    updateFileListDisplay();
}

void Operations_EncryptedData::onTagCheckboxChanged()
{
    if (m_updatingFilters) {
        return;
    }

    qDebug() << "Tag selection changed, scheduling update";

    // Stop any existing timer and restart it with callback
    // This batches rapid checkbox changes together
    m_tagFilterDebounceTimer->stop();
    m_tagFilterDebounceTimer->start([this]() {
        updateFileListDisplay();
    });
}

void Operations_EncryptedData::populateCategoriesList()
{
    qDebug() << "Populating categories list (case-insensitive with hiding settings)";

    // Clear current categories list
    m_mainWindow->ui->listWidget_DataENC_Categories->clear();

    // Get list of hidden categories for filtering (case-insensitive)
    QStringList hiddenCategories;
    if (m_mainWindow->setting_DataENC_Hide_Categories) {
        hiddenCategories = parseHiddenItems(m_mainWindow->setting_DataENC_Hidden_Categories);
        // Convert to lowercase for comparison
        for (QString& hiddenCategory : hiddenCategories) {
            hiddenCategory = hiddenCategory.toLower();
        }
    }

    // Collect unique categories (using case-insensitive display names)
    QSet<QString> visibleCategories;

    // Use the analyzed display names instead of raw categories
    for (auto it = m_categoryDisplayNames.begin(); it != m_categoryDisplayNames.end(); ++it) {
        const QString& lowercaseCategory = it.key();
        const QString& displayName = it.value();

        // Check if this category should be hidden (case-insensitive)
        if (!hiddenCategories.contains(lowercaseCategory)) {
            visibleCategories.insert(displayName);
        }
    }

    // Always add "All" at the top
    QListWidgetItem* allItem = new QListWidgetItem("All");
    allItem->setData(Qt::UserRole, "All");
    m_mainWindow->ui->listWidget_DataENC_Categories->addItem(allItem);

    // Add visible categories in alphabetical order (by display name)
    QStringList sortedCategories(visibleCategories.begin(), visibleCategories.end());
    sortedCategories.sort();

    // Remove "Uncategorized" if it exists (we'll add it at the end)
    sortedCategories.removeAll("Uncategorized");

    for (const QString& displayCategory : sortedCategories) {
        QListWidgetItem* item = new QListWidgetItem(displayCategory);
        // Store the display name (not lowercase) for filtering logic
        item->setData(Qt::UserRole, displayCategory);
        m_mainWindow->ui->listWidget_DataENC_Categories->addItem(item);
    }

    // Add "Uncategorized" at the bottom if it has files and is not hidden
    if (visibleCategories.contains("Uncategorized")) {
        QListWidgetItem* uncategorizedItem = new QListWidgetItem("Uncategorized");
        uncategorizedItem->setData(Qt::UserRole, "Uncategorized");
        m_mainWindow->ui->listWidget_DataENC_Categories->addItem(uncategorizedItem);
    }

    qDebug() << "Added" << m_mainWindow->ui->listWidget_DataENC_Categories->count()
             << "categories (including All, case-insensitive with hiding settings applied)";
}

void Operations_EncryptedData::populateTagsList()
{
    qDebug() << "Populating tags list (case-insensitive with hiding settings)";

    // Clear current tags list
    m_mainWindow->ui->listWidget_DataENC_Tags->clear();

    // Get list of hidden tags for filtering (case-insensitive)
    QStringList hiddenTags;
    if (m_mainWindow->setting_DataENC_Hide_Tags) {
        hiddenTags = parseHiddenItems(m_mainWindow->setting_DataENC_Hidden_Tags);
        // Convert to lowercase for comparison
        for (QString& hiddenTag : hiddenTags) {
            hiddenTag = hiddenTag.toLower();
        }
    }

    // Collect unique tags from currently filtered files (case-insensitive)
    QSet<QString> allTagsLowercase; // Track which lowercase tags we've seen
    QSet<QString> visibleTagsDisplay; // The actual display names to show

    // Get a thread-safe copy of filtered files
    QStringList currentFilteredFilesCopy = m_currentFilteredFiles.getCopy();
    
    for (const QString& filePath : currentFilteredFilesCopy) {
        if (m_fileMetadataCache.contains(filePath)) {
            // Get metadata using thread-safe value() method
            auto metadataOpt = m_fileMetadataCache.value(filePath);
            if (!metadataOpt.has_value()) {
                continue;
            }
            const EncryptedFileMetadata::FileMetadata& metadata = metadataOpt.value();
            for (const QString& tag : metadata.tags) {
                if (!tag.isEmpty()) {
                    QString tagLower = tag.toLower();

                    // Check if this tag should be hidden (case-insensitive)
                    if (!hiddenTags.contains(tagLower)) {
                        // Only add if we haven't seen this lowercase version yet
                        if (!allTagsLowercase.contains(tagLower)) {
                            allTagsLowercase.insert(tagLower);

                            // Get the display name from our cached analysis
                            QString displayName = m_tagDisplayNames.value(tagLower, tag);
                            visibleTagsDisplay.insert(displayName);
                        }
                    }
                }
            }
        }
    }

    // Add tags as checkbox items in alphabetical order (by display name)
    QStringList sortedTags(visibleTagsDisplay.begin(), visibleTagsDisplay.end());
    sortedTags.sort();

    for (const QString& displayTag : sortedTags) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(displayTag);
        // Store the display name (not lowercase) for filtering logic
        item->setData(Qt::UserRole, displayTag);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);

        m_mainWindow->ui->listWidget_DataENC_Tags->addItem(item);
    }

    // Connect to checkbox changes
    connect(m_mainWindow->ui->listWidget_DataENC_Tags, &QListWidget::itemChanged,
            this, &Operations_EncryptedData::onTagCheckboxChanged);

    qDebug() << "Added" << sortedTags.size() << "tags with checkboxes (case-insensitive with hiding settings applied)";
}


// ============================================================================
// Filter Helper Functions
// ============================================================================
QStringList Operations_EncryptedData::parseHiddenItems(const QString& hiddenString)
{
    if (hiddenString.isEmpty()) {
        return QStringList();
    }

    QStringList items = hiddenString.split(';', Qt::SkipEmptyParts);

    // Trim whitespace from each item
    for (QString& item : items) {
        item = item.trimmed();
    }

    return items;
}

bool Operations_EncryptedData::shouldHideFileByCategory(const EncryptedFileMetadata::FileMetadata& metadata)
{
    if (!m_mainWindow->setting_DataENC_Hide_Categories) {
        return false; // Setting is disabled, don't hide anything
    }

    QStringList hiddenCategories = parseHiddenItems(m_mainWindow->setting_DataENC_Hidden_Categories);

    if (hiddenCategories.isEmpty()) {
        return false; // No hidden categories specified
    }

    QString fileCategory = metadata.category.isEmpty() ? "Uncategorized" : metadata.category;

    // Case-insensitive comparison
    for (const QString& hiddenCategory : hiddenCategories) {
        if (fileCategory.compare(hiddenCategory, Qt::CaseInsensitive) == 0) {
            return true; // File is in a hidden category
        }
    }

    return false;
}

bool Operations_EncryptedData::shouldHideFileByTags(const EncryptedFileMetadata::FileMetadata& metadata)
{
    if (!m_mainWindow->setting_DataENC_Hide_Tags) {
        return false; // Setting is disabled, don't hide anything
    }

    QStringList hiddenTags = parseHiddenItems(m_mainWindow->setting_DataENC_Hidden_Tags);

    if (hiddenTags.isEmpty()) {
        return false; // No hidden tags specified
    }

    // Check if file has any of the hidden tags (case-insensitive)
    for (const QString& fileTag : metadata.tags) {
        for (const QString& hiddenTag : hiddenTags) {
            if (fileTag.compare(hiddenTag, Qt::CaseInsensitive) == 0) {
                return true; // File has a hidden tag, should be hidden
            }
        }
    }

    return false; // File doesn't have any hidden tags
}

bool Operations_EncryptedData::shouldHideThumbnail(const QString& fileTypeDir)
{
    if (fileTypeDir == "Image" && m_mainWindow->setting_DataENC_HideThumbnails_Image) {
        return true;
    }

    if (fileTypeDir == "Video" && m_mainWindow->setting_DataENC_HideThumbnails_Video) {
        return true;
    }

    return false;
}

void Operations_EncryptedData::analyzeCaseInsensitiveDisplayNames()
{
    qDebug() << "Analyzing case-insensitive display names for categories and tags";

    // Clear existing cached display names
    m_categoryDisplayNames.clear();
    m_tagDisplayNames.clear();

    // Maps to count occurrences of each casing variant
    // Key: lowercase version, Value: Map of (actual casing -> count)
    QMap<QString, QMap<QString, int>> categoryVariants;
    QMap<QString, QMap<QString, int>> tagVariants;

    // Analyze all files in metadata cache using thread-safe iteration
    m_fileMetadataCache.safeIterate([&categoryVariants, &tagVariants](const QString& filePath, const EncryptedFileMetadata::FileMetadata& metadata) {
        // Analyze category
        QString category = metadata.category.isEmpty() ? "Uncategorized" : metadata.category;
        QString categoryLower = category.toLower();
        categoryVariants[categoryLower][category]++;

        // Analyze tags
        for (const QString& tag : metadata.tags) {
            if (!tag.isEmpty()) {
                QString tagLower = tag.toLower();
                tagVariants[tagLower][tag]++;
            }
        }
    });

    // Find most common casing for each category
    for (auto it = categoryVariants.begin(); it != categoryVariants.end(); ++it) {
        const QString& lowercaseCategory = it.key();
        const QMap<QString, int>& variants = it.value();

        QString mostCommonCasing;
        int highestCount = 0;

        for (auto variantIt = variants.begin(); variantIt != variants.end(); ++variantIt) {
            const QString& casing = variantIt.key();
            int count = variantIt.value();

            if (count > highestCount) {
                highestCount = count;
                mostCommonCasing = casing;
            }
        }

        m_categoryDisplayNames[lowercaseCategory] = mostCommonCasing;
        qDebug() << "Category:" << lowercaseCategory << "-> display as:" << mostCommonCasing
                 << "(" << highestCount << "files)";
    }

    // Find most common casing for each tag
    for (auto it = tagVariants.begin(); it != tagVariants.end(); ++it) {
        const QString& lowercaseTag = it.key();
        const QMap<QString, int>& variants = it.value();

        QString mostCommonCasing;
        int highestCount = 0;

        for (auto variantIt = variants.begin(); variantIt != variants.end(); ++variantIt) {
            const QString& casing = variantIt.key();
            int count = variantIt.value();

            if (count > highestCount) {
                highestCount = count;
                mostCommonCasing = casing;
            }
        }

        m_tagDisplayNames[lowercaseTag] = mostCommonCasing;
        qDebug() << "Tag:" << lowercaseTag << "-> display as:" << mostCommonCasing
                 << "(" << highestCount << "files)";
    }

    qDebug() << "Analysis complete. Found" << m_categoryDisplayNames.size() << "unique categories and"
             << m_tagDisplayNames.size() << "unique tags";
}

// ============================================================================
// Search Functionality
// ============================================================================
// ============================================================================
// Search Functionality Implementation
// ============================================================================

// Search text changed slot - triggers debounced search
void Operations_EncryptedData::onSearchTextChanged()
{
    qDebug() << "Operations_EncryptedData: Search text changed";

    // Get the current search text
    {
        QMutexLocker locker(&m_stateMutex);
        m_currentSearchText = m_mainWindow->ui->lineEdit_DataENC_SearchBar->text().trimmed();
        qDebug() << "Operations_EncryptedData: New search text:" << m_currentSearchText;
    }

    // Reset and start the debounce timer with callback
    m_searchDebounceTimer->stop();
    m_searchDebounceTimer->start([this]() {
        updateFileListDisplay();
    });
}

// Clear search functionality
void Operations_EncryptedData::clearSearch()
{
    qDebug() << "Operations_EncryptedData: Clearing search";

    // Clear the search bar text
    m_mainWindow->ui->lineEdit_DataENC_SearchBar->clear();

    // Clear the stored search text
    {
        QMutexLocker locker(&m_stateMutex);
        m_currentSearchText.clear();
    }

    // Stop any pending search timer
    m_searchDebounceTimer->stop();

    // Update the display immediately
    updateFileListDisplay();
}

// Check if a file matches the search criteria (filename only - legacy function)
bool Operations_EncryptedData::matchesSearchCriteria(const QString& filename, const QString& searchText)
{
    // If search text is empty, all files match
    if (searchText.isEmpty()) {
        return true;
    }

    // Check if the filename contains the search text (case-insensitive)
    return filename.contains(searchText, Qt::CaseInsensitive);
}

// Check if a file matches the search criteria (includes both filename and tags)
bool Operations_EncryptedData::matchesSearchCriteriaWithTags(
    const EncryptedFileMetadata::FileMetadata& metadata,
    const QString& searchText)
{
    // If search text is empty, all files match
    if (searchText.isEmpty()) {
        return true;
    }

    // Check if the filename contains the search text (case-insensitive)
    if (metadata.filename.contains(searchText, Qt::CaseInsensitive)) {
        qDebug() << "Operations_EncryptedData: File matches search (filename):" << metadata.filename;
        return true;
    }

    // Check if any tag contains the search text (case-insensitive)
    for (const QString& tag : metadata.tags) {
        if (tag.contains(searchText, Qt::CaseInsensitive)) {
            qDebug() << "Operations_EncryptedData: File matches search (tag):" << metadata.filename
                     << "matching tag:" << tag;
            return true;
        }
    }

    // No match found in filename or tags
    return false;
}

// ============================================================================
// Mapping and Conversion Functions
// ============================================================================
QString Operations_EncryptedData::mapSortTypeToDirectory(const QString& sortType)
{
    if (sortType == "Text") {
        return "Document";
    } else if (sortType == "Image") {
        return "Image";
    } else if (sortType == "Audio") {
        return "Audio";
    } else if (sortType == "Video") {
        return "Video";
    } else if (sortType == "Archive") {
        return "Archive";
    } else if (sortType == "Other") {
        return "Other";
    } else if (sortType == "All") {
        return "All";
    }

    return "All"; // Default fallback
}

QString Operations_EncryptedData::mapDirectoryToSortType(const QString& directoryName)
{
    if (directoryName == "Document") {
        return "Text";
    } else if (directoryName == "Image") {
        return "Image";
    } else if (directoryName == "Audio") {
        return "Audio";
    } else if (directoryName == "Video") {
        return "Video";
    } else if (directoryName == "Archive") {
        return "Archive";
    } else if (directoryName == "Other") {
        return "Other";
    }

    return "All"; // Default fallback
}

QString Operations_EncryptedData::formatFileSize(qint64 bytes)
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;

    if (bytes >= GB) {
        return QString::number(bytes / (double)GB, 'f', 2) + " GB";
    } else if (bytes >= MB) {
        return QString::number(bytes / (double)MB, 'f', 2) + " MB";
    } else if (bytes >= KB) {
        return QString::number(bytes / (double)KB, 'f', 2) + " KB";
    } else {
        return QString::number(bytes) + " bytes";
    }
}

// ============================================================================
// Unique File Path Generation
// ============================================================================
QString Operations_EncryptedData::generateUniqueFilePath(const QString& targetDirectory, const QString& originalFilename)
{
    // Ensure target directory exists
    QDir dir(targetDirectory);
    if (!dir.exists()) {
        // If directory doesn't exist, the original filename will be unique
        return QDir(targetDirectory).absoluteFilePath(originalFilename);
    }

    // Start with the original filename
    QString basePath = QDir(targetDirectory).absoluteFilePath(originalFilename);

    // If the file doesn't exist, return the original path
    if (!QFile::exists(basePath)) {
        return basePath;
    }

    // Extract the base name and extension for suffix generation
    QFileInfo fileInfo(originalFilename);
    QString baseName = fileInfo.completeBaseName(); // filename without extension
    QString extension = fileInfo.suffix(); // extension without the dot

    // Generate unique filename with suffix pattern: "filename (n).ext"
    int counter = 1;
    QString uniquePath;

    do {
        QString uniqueFilename;
        if (extension.isEmpty()) {
            // No extension case
            uniqueFilename = QString("%1 (%2)").arg(baseName).arg(counter);
        } else {
            // With extension case
            uniqueFilename = QString("%1 (%2).%3").arg(baseName).arg(counter).arg(extension);
        }

        uniquePath = QDir(targetDirectory).absoluteFilePath(uniqueFilename);
        counter++;

        // Safety check to prevent infinite loop
        if (counter > 9999) {
            qWarning() << "Failed to generate unique filename after 9999 attempts for:" << originalFilename;
            break;
        }

    } while (QFile::exists(uniquePath));

    qDebug() << "Generated unique path:" << uniquePath << "for original:" << originalFilename;
    return uniquePath;
}

QString Operations_EncryptedData::generateUniqueFilenameInDirectory(const QString& targetDirectory,
                                                                    const QString& originalFilename,
                                                                    const QStringList& usedFilenames)
{
    // Start with the original filename
    QString candidateFilename = originalFilename;

    // If it's not in the used list and won't conflict with existing files, use it
    if (!usedFilenames.contains(candidateFilename)) {
        QString fullPath = QDir(targetDirectory).absoluteFilePath(candidateFilename);
        if (!QFile::exists(fullPath)) {
            return candidateFilename;
        }
    }

    // Extract the base name and extension for suffix generation
    QFileInfo fileInfo(originalFilename);
    QString baseName = fileInfo.completeBaseName(); // filename without extension
    QString extension = fileInfo.suffix(); // extension without the dot

    // Generate unique filename with suffix pattern: "filename (n).ext"
    int counter = 1;

    do {
        if (extension.isEmpty()) {
            // No extension case
            candidateFilename = QString("%1 (%2)").arg(baseName).arg(counter);
        } else {
            // With extension case
            candidateFilename = QString("%1 (%2).%3").arg(baseName).arg(counter).arg(extension);
        }

        counter++;

        // Safety check to prevent infinite loop
        if (counter > 9999) {
            qWarning() << "Failed to generate unique filename after 9999 attempts for:" << originalFilename;
            break;
        }

    } while (usedFilenames.contains(candidateFilename) ||
             QFile::exists(QDir(targetDirectory).absoluteFilePath(candidateFilename)));

    return candidateFilename;
}


// ============================================================================
// Icon and Thumbnail Management
// ============================================================================
void Operations_EncryptedData::clearThumbnailCache()
{
    // Thread-safe clear operation - no manual mutex needed
    m_thumbnailCache.clear();
    qDebug() << "Operations_EncryptedData: Thumbnail cache cleared";
}

// ============================================================================
// Image Viewer Functions
// ============================================================================
bool Operations_EncryptedData::isImageFile(const QString& filename) const
{
    QFileInfo fileInfo(filename);
    QString extension = fileInfo.suffix().toLower();

    // Image file extensions (same list as used in determineFileType)
    QStringList imageExtensions = {
        "jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "svg", "ico",
        "webp", "heic", "heif", "raw", "cr2", "nef", "arw", "dng", "psd",
        "xcf", "eps", "ai", "indd"
    };

    return imageExtensions.contains(extension);
}

// ============================================================================
// Video Player Functions
// ============================================================================
bool Operations_EncryptedData::isVideoFile(const QString& filename) const
{
    QFileInfo fileInfo(filename);
    QString extension = fileInfo.suffix().toLower();

    // Video file extensions (same list as used in determineFileType)
    QStringList videoExtensions = {
        "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp",
        "mpg", "mpeg", "m2v", "divx", "xvid", "asf", "rm", "rmvb", "vob",
        "ts", "mts", "m2ts", "f4v", "ogv", "mxf", "dv", "m1v", "mp2v",
        "3g2", "3gp2", "amv", "dnxhd", "prores"
    };

    return videoExtensions.contains(extension);
}

void Operations_EncryptedData::openWithVideoPlayer(const QString& encryptedFilePath, const QString& originalFilename)
{
    qDebug() << "Operations_EncryptedData: Opening video with BaseVideoPlayer:" << originalFilename;

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Validate encryption key before proceeding
    qDebug() << "Operations_EncryptedData: Validating encryption key for VideoPlayer:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Operations_EncryptedData: Encryption key validation successful for VideoPlayer";

    // Verify this is actually a video file
    if (!isVideoFile(originalFilename)) {
        QMessageBox::warning(m_mainWindow, "Not a Video",
                             "The selected file is not a video file.");
        return;
    }

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store "videoplayer" as the app to open
    {
        QMutexLocker locker(&m_stateMutex);
        m_pendingAppToOpen = "videoplayer";
        qDebug() << "Operations_EncryptedData: Stored 'videoplayer' in m_pendingAppToOpen";
    }

    qDebug() << "Operations_EncryptedData: Starting temporary decryption for VideoPlayer";

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting video for playback...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("Opening Video File");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread for temp decryption
    m_tempDecryptWorkerThread = new QThread(this);
    m_tempDecryptWorker = new TempDecryptionWorker(encryptedFilePath, tempFilePath, encryptionKey);
    m_tempDecryptWorker->moveToThread(m_tempDecryptWorkerThread);

    // Connect signals
    connect(m_tempDecryptWorkerThread, &QThread::started,
            m_tempDecryptWorker, &TempDecryptionWorker::doDecryption);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::progressUpdated,
            this, &Operations_EncryptedData::onTempDecryptionProgress);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::decryptionFinished,
            this, &Operations_EncryptedData::onTempDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled,
            this, &Operations_EncryptedData::onTempDecryptionCancelled);

    // Start decryption
    m_tempDecryptWorkerThread->start();
    m_progressDialog->exec();
}

void Operations_EncryptedData::openWithImageViewer(const QString& encryptedFilePath, const QString& originalFilename)
{
    qDebug() << "Opening image with ImageViewer:" << originalFilename;

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Validate encryption key before proceeding
    qDebug() << "Validating encryption key for ImageViewer:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful for ImageViewer";

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store "imageviewer" as the app to open (special marker for ImageViewer)
    m_pendingAppToOpen = "imageviewer";
    qDebug() << "Stored 'imageviewer' in m_pendingAppToOpen for ImageViewer";

    qDebug() << "Starting temporary decryption for ImageViewer";

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting image for viewing...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("Opening Image");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread for temp decryption
    m_tempDecryptWorkerThread = new QThread(this);
    m_tempDecryptWorker = new TempDecryptionWorker(encryptedFilePath, tempFilePath, encryptionKey);
    m_tempDecryptWorker->moveToThread(m_tempDecryptWorkerThread);

    // Connect signals
    connect(m_tempDecryptWorkerThread, &QThread::started, m_tempDecryptWorker, &TempDecryptionWorker::doDecryption);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::progressUpdated, this, &Operations_EncryptedData::onTempDecryptionProgress);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::decryptionFinished, this, &Operations_EncryptedData::onTempDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onTempDecryptionCancelled);

    // Start decryption
    m_tempDecryptWorkerThread->start();
    m_progressDialog->exec();
}


// ============================================================================
// Metadata Repair Functions
// ============================================================================
void Operations_EncryptedData::repairCorruptedMetadata()
{
    qDebug() << "Starting metadata corruption scan...";

    // Scan for corrupted metadata files
    QStringList corruptedFiles = scanForCorruptedMetadata();

    if (corruptedFiles.isEmpty()) {
        qDebug() << "No corrupted metadata files found";
        return;
    }

    qDebug() << "Found" << corruptedFiles.size() << "files with corrupted metadata";

    // Show repair dialog to user
    bool userWantsRepair = showMetadataRepairDialog(corruptedFiles.size());

    if (!userWantsRepair) {
        qDebug() << "User declined metadata repair";
        return;
    }

    // Attempt to repair the corrupted files
    bool repairSuccess = repairMetadataFiles(corruptedFiles);

    if (repairSuccess) {
        QMessageBox::information(m_mainWindow, "Repair Complete",
                                 QString("Successfully repaired %1 files with corrupted metadata.\n\n"
                                         "The files have been given generic names with their original file extensions preserved "
                                         "and can now be accessed normally.")
                                     .arg(corruptedFiles.size()));
    } else {
        QMessageBox::warning(m_mainWindow, "Repair Partially Complete",
                             "Some files could not be repaired. Please check the application logs for details.");
    }
}

QStringList Operations_EncryptedData::scanForCorruptedMetadata()
{
    QStringList corruptedFiles;
    QString username = m_mainWindow->user_Username;

    // Build path to encrypted data
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");

    QDir encDataDir(encDataPath);
    if (!encDataDir.exists()) {
        qDebug() << "EncryptedData directory doesn't exist for user:" << username;
        return corruptedFiles;
    }

    // Scan all subdirectories for .mmenc files
    QStringList typeDirectories = {"Document", "Image", "Audio", "Video", "Archive", "Other"};

    for (const QString& typeDir : typeDirectories) {
        QString typePath = QDir(encDataPath).absoluteFilePath(typeDir);
        QDir dir(typePath);

        if (!dir.exists()) {
            continue;
        }

        // Get all .mmenc files in this directory
        QStringList filters;
        filters << "*.mmenc";
        QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);

        for (const QFileInfo& fileInfo : fileList) {
            QString encryptedFilePath = fileInfo.absoluteFilePath();

            // Try to read metadata for this file
            EncryptedFileMetadata::FileMetadata metadata;
            bool metadataValid = false;

            try {
                if (m_metadataManager) {
                    metadataValid = m_metadataManager->readMetadataFromFile(encryptedFilePath, metadata);
                }
            } catch (const std::exception& e) {
                qWarning() << "Exception reading metadata for" << encryptedFilePath << ":" << e.what();
                metadataValid = false;
            } catch (...) {
                qWarning() << "Unknown exception reading metadata for" << encryptedFilePath;
                metadataValid = false;
            }

            if (!metadataValid) {
                qDebug() << "Found corrupted metadata in file:" << encryptedFilePath;
                corruptedFiles.append(encryptedFilePath);
            }
        }
    }

    qDebug() << "Metadata scan complete. Found" << corruptedFiles.size() << "corrupted files";
    return corruptedFiles;
}

bool Operations_EncryptedData::showMetadataRepairDialog(int corruptedCount)
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Metadata Corruption Detected");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(QString("%1 files found with invalid metadata.").arg(corruptedCount));
    msgBox.setInformativeText("This may prevent these files from being displayed or accessed properly.\n\n"
                              "Do you want to attempt repairs?\n\n"
                              "Note: Repaired files will be given generic names based on their encrypted filenames, "
                              "but their original file extensions will be preserved. "
                              "The actual file content will not be affected.");

    QPushButton* repairButton = msgBox.addButton("Repair Files", QMessageBox::YesRole);
    QPushButton* skipButton = msgBox.addButton("Skip Repair", QMessageBox::NoRole);
    msgBox.setDefaultButton(repairButton);

    msgBox.exec();

    return (msgBox.clickedButton() == repairButton);
}

bool Operations_EncryptedData::repairMetadataFiles(const QStringList& corruptedFiles)
{
    if (corruptedFiles.isEmpty()) {
        return true;
    }

    qDebug() << "Starting repair of" << corruptedFiles.size() << "corrupted files";

    int successCount = 0;
    int failCount = 0;

    // Create a simple progress dialog for the repair process
    QProgressDialog progressDialog("Repairing corrupted metadata files...", "Cancel", 0, corruptedFiles.size(), m_mainWindow);
    progressDialog.setWindowTitle("Repairing Files");
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(0);
    progressDialog.setValue(0);

    for (int i = 0; i < corruptedFiles.size(); ++i) {
        // Check if user cancelled
        if (progressDialog.wasCanceled()) {
            qDebug() << "User cancelled metadata repair operation";
            break;
        }

        const QString& filePath = corruptedFiles[i];
        QFileInfo fileInfo(filePath);

        progressDialog.setLabelText(QString("Repairing: %1").arg(fileInfo.fileName()));
        progressDialog.setValue(i);

        // Allow GUI to update
        QCoreApplication::processEvents();

        // Attempt to repair this file
        if (repairSingleFileMetadata(filePath)) {
            successCount++;
            qDebug() << "Successfully repaired:" << filePath;
        } else {
            failCount++;
            qWarning() << "Failed to repair:" << filePath;
        }
    }

    progressDialog.setValue(corruptedFiles.size());

    qDebug() << "Repair operation complete. Success:" << successCount << "Failed:" << failCount;

    return (successCount > 0); // Return true if at least one file was repaired
}

bool Operations_EncryptedData::repairSingleFileMetadata(const QString& encryptedFilePath)
{
    if (!QFile::exists(encryptedFilePath)) {
        qWarning() << "File does not exist for repair:" << encryptedFilePath;
        return false;
    }

    try {
        QFileInfo fileInfo(encryptedFilePath);
        QString fullFileName = fileInfo.fileName(); // e.g., "randomstring.jpg.mmenc"

        // Extract the filename with preserved extension by removing only the final ".mmenc"
        QString obfuscatedName;
        if (fullFileName.endsWith(".mmenc", Qt::CaseInsensitive)) {
            // Remove the ".mmenc" extension, keeping everything else
            obfuscatedName = fullFileName.left(fullFileName.length() - 6); // Remove last 6 characters (".mmenc")
        } else {
            // Fallback: use baseName if the file doesn't end with .mmenc
            obfuscatedName = fileInfo.baseName();
        }

        if (obfuscatedName.isEmpty()) {
            qWarning() << "Could not extract filename from:" << encryptedFilePath;
            return false;
        }

        qDebug() << "Repairing metadata for" << encryptedFilePath;
        qDebug() << "Full filename:" << fullFileName;
        qDebug() << "Extracted name with extension:" << obfuscatedName;

        // Create generic metadata with the obfuscated filename (now includes original extension)
        EncryptedFileMetadata::FileMetadata genericMetadata;
        genericMetadata.filename = obfuscatedName;
        genericMetadata.category = "";  // Empty category
        genericMetadata.tags.clear();   // Empty tags list
        genericMetadata.thumbnailData.clear(); // Empty thumbnail data

        // Validate the generic metadata before attempting to write
        if (!EncryptedFileMetadata::isValidFilename(genericMetadata.filename)) {
            qWarning() << "Generated generic filename is invalid:" << genericMetadata.filename;
            return false;
        }

        // Attempt to update the metadata in place
        if (!m_metadataManager) {
            qWarning() << "Metadata manager not available for repair";
            return false;
        }

        bool updateSuccess = m_metadataManager->updateMetadataInFile(encryptedFilePath, genericMetadata);

        if (updateSuccess) {
            qDebug() << "Successfully updated metadata for:" << encryptedFilePath;

            // Verify the repair by trying to read the metadata back
            EncryptedFileMetadata::FileMetadata verifyMetadata;
            if (m_metadataManager->readMetadataFromFile(encryptedFilePath, verifyMetadata)) {
                qDebug() << "Repair verification successful for:" << encryptedFilePath;
                qDebug() << "Restored filename:" << verifyMetadata.filename;
                return true;
            } else {
                qWarning() << "Repair verification failed for:" << encryptedFilePath;
                return false;
            }
        } else {
            qWarning() << "Failed to update metadata for:" << encryptedFilePath;
            return false;
        }

    } catch (const std::exception& e) {
        qWarning() << "Exception during repair of" << encryptedFilePath << ":" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception during repair of" << encryptedFilePath;
        return false;
    }
}

#ifdef QT_DEBUG
bool Operations_EncryptedData::debugCorruptFileMetadata(const QString& encryptedFilePath)
{
    if (!QFile::exists(encryptedFilePath)) {
        qWarning() << "File does not exist for debug corruption:" << encryptedFilePath;
        return false;
    }

    try {
        qDebug() << "DEBUG: Purposefully corrupting metadata for:" << encryptedFilePath;

        // Open file for read/write
        QFile file(encryptedFilePath);
        if (!file.open(QIODevice::ReadWrite)) {
            qWarning() << "Failed to open file for debug corruption:" << encryptedFilePath;
            return false;
        }

        // Check file size to ensure it has the expected metadata block
        if (file.size() < Constants::METADATA_RESERVED_SIZE) {
            qWarning() << "File too small to contain metadata:" << file.size() << "bytes";
            file.close();
            return false;
        }

        // Seek to the beginning of the file (metadata area)
        file.seek(0);

        // Read the current metadata size (first 4 bytes)
        quint32 originalMetadataSize = 0;
        if (file.read(reinterpret_cast<char*>(&originalMetadataSize), sizeof(originalMetadataSize)) != sizeof(originalMetadataSize)) {
            qWarning() << "Failed to read original metadata size";
            file.close();
            return false;
        }

        qDebug() << "Original metadata size:" << originalMetadataSize << "bytes";

        // Go back to beginning and corrupt the metadata
        file.seek(0);

        // Create corrupted data - overwrite first 64 bytes with random data
        // This will corrupt both the size header and the beginning of the encrypted metadata
        QByteArray corruptedData(64, 0);
        for (int i = 0; i < corruptedData.size(); ++i) {
            corruptedData[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }

        // Write the corrupted data
        qint64 bytesWritten = file.write(corruptedData);
        if (bytesWritten != corruptedData.size()) {
            qWarning() << "Failed to write corrupted data, wrote:" << bytesWritten << "expected:" << corruptedData.size();
            file.close();
            return false;
        }

        // Ensure data is written to disk
        file.flush();
        file.close();

        qDebug() << "DEBUG: Successfully corrupted" << bytesWritten << "bytes of metadata";

        // Verify corruption by trying to read metadata
        EncryptedFileMetadata::FileMetadata testMetadata;
        bool canReadMetadata = false;

        try {
            if (m_metadataManager) {
                canReadMetadata = m_metadataManager->readMetadataFromFile(encryptedFilePath, testMetadata);
            }
        } catch (...) {
            canReadMetadata = false;
        }

        if (canReadMetadata) {
            qWarning() << "DEBUG: Corruption may not have been effective - metadata is still readable";
            return false;
        } else {
            qDebug() << "DEBUG: Corruption confirmed - metadata is no longer readable";
            return true;
        }

    } catch (const std::exception& e) {
        qWarning() << "Exception during debug corruption:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception during debug corruption";
        return false;
    }
}
#endif


// ============================================================================
// Event Filter
// ============================================================================
bool Operations_EncryptedData::eventFilter(QObject* watched, QEvent* event)
{
    // Check if this is a key press event on the file list
    if (watched == m_mainWindow->ui->listWidget_DataENC_FileList && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // Handle Delete key press
        if (keyEvent->key() == Qt::Key_Delete) {
            // Check if a file is selected
            QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
            if (currentItem) {
                deleteSelectedFile();
                return true; // Event handled
            }
        }
    }

    // NEW: Handle Escape key for search bar
       if (watched == m_mainWindow->ui->lineEdit_DataENC_SearchBar && event->type() == QEvent::KeyPress) {
           QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
           if (keyEvent->key() == Qt::Key_Escape) {
               clearSearch();
               return true;
           }
       }

    // Pass event to parent class
    return QObject::eventFilter(watched, event);
}


// ============================================================================
// Settings Bridge
// ============================================================================
void Operations_EncryptedData::refreshDisplayForSettingsChange()
{
    qDebug() << "Refreshing encrypted data display for settings change (case-insensitive)";

    // Clear thumbnail cache since thumbnail hiding settings may have changed
    clearThumbnailCache();

    // Re-analyze case-insensitive display names since hiding settings may have changed
    // which categories/tags are visible
    analyzeCaseInsensitiveDisplayNames();

    // Repopulate categories list with new hiding settings
    populateCategoriesList();

    // Reset category selection to "All" to ensure proper refresh
    if (m_mainWindow->ui->listWidget_DataENC_Categories->count() > 0) {
        m_mainWindow->ui->listWidget_DataENC_Categories->setCurrentRow(0); // "All" is always first
    }

    // Refresh the category selection which will trigger a complete refresh
    // This ensures that both category and tag filtering are applied with new settings
    onCategorySelectionChanged();
}
