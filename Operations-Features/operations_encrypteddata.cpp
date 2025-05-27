#include "operations_encrypteddata.h"
#include "../Operations-Global/CryptoUtils.h"
#include "../Operations-Global/operations_files.h"
#include "../constants.h"
#include "../CustomWidgets/encryptedfileitemwidget.h"
#include "../Operations-Global/fileiconprovider.h"
#include "../Operations-Global/thumbnailcache.h"
#include "qpainter.h"
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
#include "editencryptedfiledialog.h"
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>

// Windows-specific includes for file association checking
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <QSettings>
#endif


// ============================================================================
// EncryptionWorker Implementation
// ============================================================================

EncryptionWorker::EncryptionWorker(const QStringList& sourceFiles, const QStringList& targetFiles,
                                   const QByteArray& encryptionKey, const QString& username,
                                   const QMap<QString, QPixmap>& videoThumbnails)
    : m_sourceFiles(sourceFiles)
    , m_targetFiles(targetFiles)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(false)
    , m_videoThumbnails(videoThumbnails)
    , m_metadataManager(new EncryptedFileMetadata(encryptionKey, username))
{
}

EncryptionWorker::EncryptionWorker(const QString& sourceFile, const QString& targetFile,
                                   const QByteArray& encryptionKey, const QString& username,
                                   const QMap<QString, QPixmap>& videoThumbnails)
    : m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(false)
    , m_videoThumbnails(videoThumbnails)
    , m_metadataManager(new EncryptedFileMetadata(encryptionKey, username))
{
    m_sourceFiles << sourceFile;
    m_targetFiles << targetFile;
}

// Add cleanup in EncryptionWorker destructor (add if not exists):
EncryptionWorker::~EncryptionWorker()
{
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
}

void EncryptionWorker::doEncryption()
{
    try {
        if (m_sourceFiles.size() != m_targetFiles.size()) {
            if (m_sourceFiles.size() == 1) {
                emit encryptionFinished(false, "Mismatch between source and target file counts");
            } else {
                emit multiFileEncryptionFinished(false, "Mismatch between source and target file counts",
                                                 QStringList(), QStringList());
            }
            return;
        }

        if (m_sourceFiles.isEmpty()) {
            emit encryptionFinished(false, "No files to encrypt");
            return;
        }

        // Determine if this is single or multiple file operation
        bool isMultipleFiles = (m_sourceFiles.size() > 1);

        // *** REMOVED: Video thumbnail extraction (now done in main thread) ***

        // Calculate total size of all files for progress tracking
        qint64 totalSize = 0;
        QList<qint64> fileSizes;

        for (const QString& sourceFile : m_sourceFiles) {
            QFile file(sourceFile);
            if (!file.exists()) {
                QString errorMsg = QString("Source file does not exist: %1").arg(sourceFile);
                if (isMultipleFiles) {
                    emit multiFileEncryptionFinished(false, errorMsg, QStringList(), QStringList());
                } else {
                    emit encryptionFinished(false, errorMsg);
                }
                return;
            }
            qint64 fileSize = file.size();
            fileSizes.append(fileSize);
            totalSize += fileSize;
        }

        qint64 processedTotalSize = 0;
        QStringList successfulFiles;
        QStringList failedFiles;

        // Process each file
        for (int fileIndex = 0; fileIndex < m_sourceFiles.size(); ++fileIndex) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    // Clean up any partial files created so far
                    for (int i = 0; i < fileIndex; ++i) {
                        if (QFile::exists(m_targetFiles[i])) {
                            QFile::remove(m_targetFiles[i]);
                        }
                    }
                    if (isMultipleFiles) {
                        emit multiFileEncryptionFinished(false, "Operation was cancelled",
                                                         QStringList(), QStringList());
                    } else {
                        emit encryptionFinished(false, "Operation was cancelled");
                    }
                    return;
                }
            }

            const QString& sourceFile = m_sourceFiles[fileIndex];
            const QString& targetFile = m_targetFiles[fileIndex];
            qint64 currentFileSize = fileSizes[fileIndex];

            // Update progress to show which file we're working on (only for multiple files)
            if (isMultipleFiles) {
                emit fileProgressUpdate(fileIndex + 1, m_sourceFiles.size(), QFileInfo(sourceFile).fileName());
            }

            QFile source(sourceFile);
            if (!source.open(QIODevice::ReadOnly)) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (failed to open for reading)").arg(fileName));
                processedTotalSize += currentFileSize; // Still count it for progress
                continue;
            }

            // Create target directory if it doesn't exist
            QFileInfo targetInfo(targetFile);
            QDir targetDir = targetInfo.dir();
            if (!targetDir.exists()) {
                if (!targetDir.mkpath(".")) {
                    QString fileName = QFileInfo(sourceFile).fileName();
                    failedFiles.append(QString("%1 (failed to create target directory)").arg(fileName));
                    source.close();
                    processedTotalSize += currentFileSize;
                    continue;
                }
            }

            QFile target(targetFile);
            if (!target.open(QIODevice::WriteOnly)) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (failed to create target file)").arg(fileName));
                source.close();
                processedTotalSize += currentFileSize;
                continue;
            }

            // Write encrypted metadata header
            QFileInfo sourceInfo(sourceFile);
            QString originalFilename = sourceInfo.fileName();

            // Create metadata with just filename (empty category and tags)
            EncryptedFileMetadata::FileMetadata metadata(originalFilename);

            // Create encrypted metadata chunk
            QByteArray encryptedMetadata = m_metadataManager->createEncryptedMetadataChunk(metadata);
            if (encryptedMetadata.isEmpty()) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (failed to create metadata)").arg(fileName));
                source.close();
                processedTotalSize += currentFileSize;
                continue;
            }

            // Write metadata size and encrypted metadata
            quint32 metadataSize = static_cast<quint32>(encryptedMetadata.size());
            target.write(reinterpret_cast<const char*>(&metadataSize), sizeof(metadataSize));
            target.write(encryptedMetadata);

            // Encrypt and write file content in chunks
            const qint64 chunkSize = 1024 * 1024; // 1MB chunks
            QByteArray buffer;
            qint64 processedFileSize = 0;
            bool fileSuccess = true;

            while (!source.atEnd() && fileSuccess) {
                // Check for cancellation
                {
                    QMutexLocker locker(&m_cancelMutex);
                    if (m_cancelled) {
                        target.close();
                        source.close();
                        QFile::remove(targetFile); // Clean up partial file

                        // Clean up any other partial files
                        for (int i = 0; i < fileIndex; ++i) {
                            if (QFile::exists(m_targetFiles[i])) {
                                QFile::remove(m_targetFiles[i]);
                            }
                        }
                        if (isMultipleFiles) {
                            emit multiFileEncryptionFinished(false, "Operation was cancelled",
                                                             QStringList(), QStringList());
                        } else {
                            emit encryptionFinished(false, "Operation was cancelled");
                        }
                        return;
                    }
                }

                // Read chunk
                buffer = source.read(chunkSize);
                if (buffer.isEmpty()) {
                    break;
                }

                // Encrypt chunk
                QByteArray encryptedChunk = CryptoUtils::Encryption_EncryptBArray(
                    m_encryptionKey, buffer, m_username);

                if (encryptedChunk.isEmpty()) {
                    fileSuccess = false;
                    break;
                }

                // Write encrypted chunk size and data
                quint32 chunkDataSize = static_cast<quint32>(encryptedChunk.size());
                target.write(reinterpret_cast<const char*>(&chunkDataSize), sizeof(chunkDataSize));
                target.write(encryptedChunk);

                processedFileSize += buffer.size();
                processedTotalSize += buffer.size();

                // Update overall progress
                int percentage = static_cast<int>((processedTotalSize * 100) / totalSize);
                emit progressUpdated(percentage);

                // Allow other threads to run
                QCoreApplication::processEvents();
            }

            source.close();
            target.close();

            if (fileSuccess) {
                successfulFiles.append(QFileInfo(sourceFile).fileName());

                // *** UPDATED: Use pre-extracted video thumbnail if available ***
                if (m_videoThumbnails.contains(sourceFile)) {
                    qDebug() << "Using pre-extracted video thumbnail for encrypted file:" << targetFile;
                    emit videoThumbnailExtracted(targetFile, m_videoThumbnails[sourceFile]);
                }
            } else {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (encryption failed)").arg(fileName));
                QFile::remove(targetFile); // Clean up failed file
            }

            // Ensure we account for any remaining bytes in the progress
            if (processedFileSize < currentFileSize) {
                processedTotalSize += (currentFileSize - processedFileSize);
            }
        }

        // Emit appropriate completion signal based on operation type
        if (isMultipleFiles) {
            // Multiple files - use new signal
            bool overallSuccess = !successfulFiles.isEmpty();
            QString resultMessage;

            if (successfulFiles.size() == m_sourceFiles.size()) {
                resultMessage = QString("All %1 files encrypted successfully").arg(successfulFiles.size());
            } else if (successfulFiles.isEmpty()) {
                resultMessage = "All files failed to encrypt:\n" + failedFiles.join("\n");
                overallSuccess = false;
            } else {
                resultMessage = QString("Partial success: %1 of %2 files encrypted successfully\n\nFailed files:\n%3")
                .arg(successfulFiles.size())
                    .arg(m_sourceFiles.size())
                    .arg(failedFiles.join("\n"));
            }

            emit multiFileEncryptionFinished(overallSuccess, resultMessage, successfulFiles, failedFiles);
        } else {
            // Single file - use old signal for backward compatibility
            if (successfulFiles.size() == 1) {
                emit encryptionFinished(true);
            } else {
                QString errorMsg = failedFiles.isEmpty() ? "Unknown encryption error" : failedFiles.first();
                emit encryptionFinished(false, errorMsg);
            }
        }

    } catch (const std::exception& e) {
        QString errorMsg = QString("Encryption error: %1").arg(e.what());
        if (m_sourceFiles.size() > 1) {
            emit multiFileEncryptionFinished(false, errorMsg, QStringList(), QStringList());
        } else {
            emit encryptionFinished(false, errorMsg);
        }
    } catch (...) {
        QString errorMsg = "Unknown encryption error occurred";
        if (m_sourceFiles.size() > 1) {
            emit multiFileEncryptionFinished(false, errorMsg, QStringList(), QStringList());
        } else {
            emit encryptionFinished(false, errorMsg);
        }
    }
}


void EncryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

// ============================================================================
// DecryptionWorker Implementation
// ============================================================================

DecryptionWorker::DecryptionWorker(const QString& sourceFile, const QString& targetFile,
                                   const QByteArray& encryptionKey)
    : m_sourceFile(sourceFile)
    , m_targetFile(targetFile)
    , m_encryptionKey(encryptionKey)
    , m_cancelled(false)
    , m_metadataManager(new EncryptedFileMetadata(encryptionKey, QString()))
{
}

DecryptionWorker::~DecryptionWorker()
{
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
}

void DecryptionWorker::doDecryption()
{
    try {
        QFile sourceFile(m_sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit decryptionFinished(false, "Failed to open encrypted file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        qint64 processedSize = 0;

        // Skip the encrypted metadata header
        quint32 metadataSize = 0;
        if (sourceFile.read(reinterpret_cast<char*>(&metadataSize), sizeof(metadataSize)) != sizeof(metadataSize)) {
            emit decryptionFinished(false, "Failed to read metadata size from encrypted file");
            return;
        }

        // Validate metadata size
        if (metadataSize == 0 || metadataSize > EncryptedFileMetadata::MAX_ENCRYPTED_METADATA_SIZE) {
            emit decryptionFinished(false, "Invalid metadata size in encrypted file");
            return;
        }

        // Skip the encrypted metadata chunk
        QByteArray encryptedMetadata = sourceFile.read(metadataSize);
        if (encryptedMetadata.size() != static_cast<int>(metadataSize)) {
            emit decryptionFinished(false, "Failed to skip metadata in encrypted file");
            return;
        }

        processedSize += sizeof(metadataSize) + metadataSize;

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(m_targetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit decryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(m_targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit decryptionFinished(false, "Failed to create target file");
            return;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(m_targetFile); // Clean up partial file
                    emit decryptionFinished(false, "Operation was cancelled");
                    return;
                }
            }

            // Read chunk size
            quint32 chunkSize = 0;
            qint64 bytesRead = sourceFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (bytesRead == 0) {
                break; // End of file
            }
            if (bytesRead != sizeof(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read chunk size");
                return;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Invalid chunk size in encrypted file");
                return;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = sourceFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read complete encrypted chunk");
                return;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(
                m_encryptionKey, encryptedChunk);

            if (decryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Decryption failed for file chunk");
                return;
            }

            // Write decrypted chunk
            if (targetFile.write(decryptedChunk) != decryptedChunk.size()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to write decrypted data");
                return;
            }

            processedSize += sizeof(chunkSize) + chunkSize;

            // Update progress
            int percentage = static_cast<int>((processedSize * 100) / totalSize);
            emit progressUpdated(percentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        sourceFile.close();

        // Ensure all data is written to disk before closing
        targetFile.flush();
        targetFile.close();

// Set proper file permissions for reading
#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(m_targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(m_targetFile)) {
            emit decryptionFinished(false, "Target file was not created successfully");
            return;
        }

        qDebug() << "Temp decryption completed successfully:" << m_targetFile;
        emit decryptionFinished(true);

    } catch (const std::exception& e) {
        emit decryptionFinished(false, QString("Decryption error: %1").arg(e.what()));
    } catch (...) {
        emit decryptionFinished(false, "Unknown decryption error occurred");
    }
}

void DecryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

// ============================================================================
// TempDecryptionWorker Implementation
// ============================================================================

TempDecryptionWorker::TempDecryptionWorker(const QString& sourceFile, const QString& targetFile,
                                           const QByteArray& encryptionKey)
    : m_sourceFile(sourceFile)
    , m_targetFile(targetFile)
    , m_encryptionKey(encryptionKey)
    , m_cancelled(false)
    , m_metadataManager(new EncryptedFileMetadata(encryptionKey, QString()))
{
}

TempDecryptionWorker::~TempDecryptionWorker()
{
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
}
void TempDecryptionWorker::doDecryption()
{
    try {
        QFile sourceFile(m_sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit decryptionFinished(false, "Failed to open encrypted file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        qint64 processedSize = 0;

        // Skip the encrypted metadata header
        quint32 metadataSize = 0;
        if (sourceFile.read(reinterpret_cast<char*>(&metadataSize), sizeof(metadataSize)) != sizeof(metadataSize)) {
            emit decryptionFinished(false, "Failed to read metadata size from encrypted file");
            return;
        }

        // Validate metadata size
        if (metadataSize == 0 || metadataSize > EncryptedFileMetadata::MAX_ENCRYPTED_METADATA_SIZE) {
            emit decryptionFinished(false, "Invalid metadata size in encrypted file");
            return;
        }

        // Skip the encrypted metadata chunk
        QByteArray encryptedMetadata = sourceFile.read(metadataSize);
        if (encryptedMetadata.size() != static_cast<int>(metadataSize)) {
            emit decryptionFinished(false, "Failed to skip metadata in encrypted file");
            return;
        }

        processedSize += sizeof(metadataSize) + metadataSize;

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(m_targetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit decryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(m_targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit decryptionFinished(false, "Failed to create target file");
            return;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(m_targetFile); // Clean up partial file
                    emit decryptionFinished(false, "Operation was cancelled");
                    return;
                }
            }

            // Read chunk size
            quint32 chunkSize = 0;
            qint64 bytesRead = sourceFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (bytesRead == 0) {
                break; // End of file
            }
            if (bytesRead != sizeof(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read chunk size");
                return;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Invalid chunk size in encrypted file");
                return;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = sourceFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read complete encrypted chunk");
                return;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(
                m_encryptionKey, encryptedChunk);

            if (decryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Decryption failed for file chunk");
                return;
            }

            // Write decrypted chunk
            if (targetFile.write(decryptedChunk) != decryptedChunk.size()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to write decrypted data");
                return;
            }

            processedSize += sizeof(chunkSize) + chunkSize;

            // Update progress
            int percentage = static_cast<int>((processedSize * 100) / totalSize);
            emit progressUpdated(percentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        sourceFile.close();

        // Ensure all data is written to disk before closing
        targetFile.flush();
        targetFile.close();

// Set proper file permissions for reading
#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(m_targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(m_targetFile)) {
            emit decryptionFinished(false, "Target file was not created successfully");
            return;
        }

        qDebug() << "Temp decryption completed successfully:" << m_targetFile;
        emit decryptionFinished(true);

    } catch (const std::exception& e) {
        emit decryptionFinished(false, QString("Decryption error: %1").arg(e.what()));
    } catch (...) {
        emit decryptionFinished(false, "Unknown decryption error occurred");
    }
}

void TempDecryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}



// ============================================================================
// Operations_EncryptedData Implementation
// ============================================================================

Operations_EncryptedData::Operations_EncryptedData(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_progressDialog(nullptr)
    , m_worker(nullptr)
    , m_workerThread(nullptr)
    , m_decryptWorker(nullptr)
    , m_decryptWorkerThread(nullptr)
    , m_tempDecryptWorker(nullptr)
    , m_tempDecryptWorkerThread(nullptr)
    , m_tempFileCleanupTimer(nullptr)
    , m_updatingFilters(false) // Initialize new member
{
    //Create MetaData Manager Instance
    m_metadataManager = new EncryptedFileMetadata(m_mainWindow->user_Key, m_mainWindow->user_Username);
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

    // Note: Tag checkboxes will be connected dynamically when created

    // Set initial button states (disabled since no files loaded yet)
    updateButtonStates();

    // Start temp file monitoring
    startTempFileMonitoring();

    // Clean up any orphaned temp files from previous sessions
    cleanupTempFiles();

    // Initialize icon provider and thumbnail cache
    qDebug() << "About to create FileIconProvider...";
    m_iconProvider = new FileIconProvider(this);
    qDebug() << "FileIconProvider created, address:" << m_iconProvider;

    qDebug() << "About to create ThumbnailCache...";
    m_thumbnailCache = new ThumbnailCache(m_mainWindow->user_Username, m_mainWindow->user_Key, this);
    qDebug() << "ThumbnailCache created, address:" << m_thumbnailCache;

    // Initialize lazy loading timer
    m_lazyLoadTimer = new QTimer(this);
    m_lazyLoadTimer->setSingleShot(true);
    m_lazyLoadTimer->setInterval(100); // 100ms delay for lazy loading
    connect(m_lazyLoadTimer, &QTimer::timeout, this, &Operations_EncryptedData::onLazyLoadTimeout);

    // Connect scroll events for lazy loading
    connect(m_mainWindow->ui->listWidget_DataENC_FileList->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &Operations_EncryptedData::onListScrolled);

    // Set up context menu for the encrypted files list
    m_mainWindow->ui->listWidget_DataENC_FileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::customContextMenuRequested,
            this, &Operations_EncryptedData::showContextMenu_FileList);

    onSortTypeChanged("All");
}

Operations_EncryptedData::~Operations_EncryptedData()
{
    // Stop the cleanup timer
    if (m_tempFileCleanupTimer) {
        m_tempFileCleanupTimer->stop();
        m_tempFileCleanupTimer->deleteLater();
        m_tempFileCleanupTimer = nullptr;
    }

    // Handle encryption worker (existing code)
    if (m_workerThread && m_workerThread->isRunning()) {
        if (m_worker) {
            m_worker->cancel();
        }
        m_workerThread->quit();
        m_workerThread->wait(5000);
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }

    // Handle decryption worker
    if (m_decryptWorkerThread && m_decryptWorkerThread->isRunning()) {
        if (m_decryptWorker) {
            m_decryptWorker->cancel();
        }
        m_decryptWorkerThread->quit();
        m_decryptWorkerThread->wait(5000);
        if (m_decryptWorkerThread->isRunning()) {
            m_decryptWorkerThread->terminate();
            m_decryptWorkerThread->wait(1000);
        }
    }

    // Handle temp decryption worker
    if (m_tempDecryptWorkerThread && m_tempDecryptWorkerThread->isRunning()) {
        if (m_tempDecryptWorker) {
            m_tempDecryptWorker->cancel();
        }
        m_tempDecryptWorkerThread->quit();
        m_tempDecryptWorkerThread->wait(5000);
        if (m_tempDecryptWorkerThread->isRunning()) {
            m_tempDecryptWorkerThread->terminate();
            m_tempDecryptWorkerThread->wait(1000);
        }
    }

    // Clean up workers
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

    if (m_progressDialog) {
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
    // Clean up metadata manager
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
}

// ============================================================================
// Double-click to open functionality
// ============================================================================

void Operations_EncryptedData::onFileListDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

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
    qDebug() << "Validating encryption key for double-click open:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful for double-click open";

    // Check for default application
    QString defaultApp = checkDefaultApp(extension);
    QString appToUse;

    qDebug() << "File extension:" << extension;
    qDebug() << "Default app found:" << (defaultApp.isEmpty() ? "None" : defaultApp);

    if (defaultApp.isEmpty()) {
        // No default app - show dialog to select one
        AppChoice choice = showNoDefaultAppDialog();
        qDebug() << "No default app dialog choice (int):" << static_cast<int>(choice);
        qDebug() << "Comparing with SelectApp (" << static_cast<int>(AppChoice::SelectApp) << ")";

        if (choice == AppChoice::Cancel) {
            qDebug() << "User cancelled - no default app dialog";
            return;
        } else if (choice == AppChoice::SelectApp) {
            qDebug() << "User chose to select app - calling selectApplication()";
            appToUse = selectApplication();
            qDebug() << "selectApplication() returned:" << appToUse;
            if (appToUse.isEmpty()) {
                qDebug() << "User cancelled app selection";
                return; // User cancelled app selection
            }
        } else {
            qDebug() << "WARNING: Unexpected choice value in no default app dialog:" << static_cast<int>(choice);
        }
    } else {
        // Default app exists - show dialog with options
        AppChoice choice = showDefaultAppDialog(defaultApp);
        qDebug() << "Default app dialog choice (int):" << static_cast<int>(choice);
        qDebug() << "Comparing with Cancel (" << static_cast<int>(AppChoice::Cancel) << "), UseDefault (" << static_cast<int>(AppChoice::UseDefault) << "), SelectApp (" << static_cast<int>(AppChoice::SelectApp) << ")";

        if (choice == AppChoice::Cancel) {
            qDebug() << "User cancelled - default app dialog";
            return;
        } else if (choice == AppChoice::UseDefault) {
            appToUse = "default"; // Special marker for default app
            qDebug() << "User chose UseDefault - appToUse set to 'default'";
        } else if (choice == AppChoice::SelectApp) {
            qDebug() << "User chose to select app - calling selectApplication()";
            appToUse = selectApplication();
            qDebug() << "selectApplication() returned:" << appToUse;
            if (appToUse.isEmpty()) {
                qDebug() << "User cancelled app selection";
                return; // User cancelled app selection
            }
        } else {
            qDebug() << "WARNING: Unexpected choice value in default app dialog:" << static_cast<int>(choice);
        }
    }

    // Safety check - this should never happen but let's handle it
    if (appToUse.isEmpty()) {
        qDebug() << "ERROR: appToUse is still empty after dialog logic!";
        qDebug() << "This should not happen - showing fallback dialog";
        QMessageBox::critical(m_mainWindow, "Error",
                              "No application was selected to open the file.");
        return;
    }

    qDebug() << "Final appToUse value before proceeding:" << appToUse;

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store the app to open for later use after decryption
    m_pendingAppToOpen = appToUse;
    qDebug() << "Stored in m_pendingAppToOpen:" << m_pendingAppToOpen;

    qDebug() << "Starting temporary decryption with validated encryption key";

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

QString Operations_EncryptedData::checkDefaultApp(const QString& extension)
{
#ifdef Q_OS_WIN
    // Check Windows registry for default application
    QSettings regSettings(QString("HKEY_CLASSES_ROOT\\.%1").arg(extension), QSettings::NativeFormat);
    QString fileType = regSettings.value(".", "").toString();

    if (fileType.isEmpty()) {
        return QString(); // No association found
    }

    QSettings appSettings(QString("HKEY_CLASSES_ROOT\\%1\\shell\\open\\command").arg(fileType), QSettings::NativeFormat);
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
    qDebug() << "Showing default app dialog for app:" << appName;

    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Open Encrypted File");
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(QString("'%1' is set as default for this type of file.").arg(appName));
    msgBox.setInformativeText("Do you want to open it with the default app or select a specific one?");

    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* useDefaultButton = msgBox.addButton("Use Default", QMessageBox::AcceptRole);
    QPushButton* selectAppButton = msgBox.addButton("Select an App", QMessageBox::ActionRole);

    msgBox.setDefaultButton(useDefaultButton);
    int result = msgBox.exec();

    qDebug() << "Dialog exec result:" << result;
    qDebug() << "Clicked button text:" << (msgBox.clickedButton() ? msgBox.clickedButton()->text() : "nullptr");
    qDebug() << "Clicked button pointer:" << msgBox.clickedButton();
    qDebug() << "Cancel button pointer:" << cancelButton;
    qDebug() << "Use Default button pointer:" << useDefaultButton;
    qDebug() << "Select App button pointer:" << selectAppButton;

    AppChoice choice = AppChoice::Cancel; // Default

    if (msgBox.clickedButton() == cancelButton) {
        choice = AppChoice::Cancel;
        qDebug() << "User clicked Cancel button (pointer match)";
    } else if (msgBox.clickedButton() == useDefaultButton) {
        choice = AppChoice::UseDefault;
        qDebug() << "User clicked Use Default button (pointer match)";
    } else if (msgBox.clickedButton() == selectAppButton) {
        choice = AppChoice::SelectApp;
        qDebug() << "User clicked Select an App button (pointer match)";
    } else {
        qDebug() << "Button pointer comparison failed - using text fallback";
        // Fallback based on button text in case button comparison fails
        if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Use Default") {
            choice = AppChoice::UseDefault;
            qDebug() << "Fallback: Detected Use Default by text";
        } else if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Select an App") {
            choice = AppChoice::SelectApp;
            qDebug() << "Fallback: Detected Select an App by text";
        } else {
            choice = AppChoice::Cancel;
            qDebug() << "Fallback: Defaulting to Cancel";
        }
    }

    qDebug() << "Final default app dialog result:" << static_cast<int>(choice);
    return choice;
}

Operations_EncryptedData::AppChoice Operations_EncryptedData::showNoDefaultAppDialog()
{
    qDebug() << "Showing no default app dialog";

    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Open Encrypted File");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("No default app defined for this type of file.");

    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* selectAppButton = msgBox.addButton("Select an App", QMessageBox::AcceptRole);

    msgBox.setDefaultButton(selectAppButton);
    int result = msgBox.exec();

    qDebug() << "Dialog exec result:" << result;
    qDebug() << "Clicked button text:" << (msgBox.clickedButton() ? msgBox.clickedButton()->text() : "nullptr");
    qDebug() << "Clicked button pointer:" << msgBox.clickedButton();
    qDebug() << "Cancel button pointer:" << cancelButton;
    qDebug() << "Select App button pointer:" << selectAppButton;

    AppChoice choice = AppChoice::Cancel; // Default

    if (msgBox.clickedButton() == cancelButton) {
        choice = AppChoice::Cancel;
        qDebug() << "User clicked Cancel button (pointer match)";
    } else if (msgBox.clickedButton() == selectAppButton) {
        choice = AppChoice::SelectApp;
        qDebug() << "User clicked Select an App button (pointer match)";
    } else {
        qDebug() << "Button pointer comparison failed - using text fallback";
        // Fallback based on button text
        if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Select an App") {
            choice = AppChoice::SelectApp;
            qDebug() << "Fallback: Detected Select an App by text";
        } else {
            choice = AppChoice::Cancel;
            qDebug() << "Fallback: Defaulting to Cancel";
        }
    }

    qDebug() << "Final no default app dialog result:" << static_cast<int>(choice);
    return choice;
}

QString Operations_EncryptedData::selectApplication()
{
    qDebug() << "Opening application selection dialog";

    QString appPath = QFileDialog::getOpenFileName(
        m_mainWindow,
        "Select Application",
        QString(),
        "Executable Files (*.exe);;All Files (*.*)"
        );

    qDebug() << "Application selection result:" << (appPath.isEmpty() ? "User cancelled" : appPath);

    // Validate the selected application path
    if (!appPath.isEmpty()) {
        QFileInfo appInfo(appPath);
        if (!appInfo.exists() || !appInfo.isExecutable()) {
            QMessageBox::warning(m_mainWindow, "Invalid Application",
                                 "The selected file is not a valid executable.");
            qDebug() << "Invalid application selected:" << appPath;
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
            qWarning() << "Failed to create temp decrypt directory:" << tempDir;
            return QString();
        }
    }

    // Extract extension from original filename
    QFileInfo fileInfo(originalFilename);
    QString extension = fileInfo.suffix();

    // Generate obfuscated filename with original extension
    QString obfuscatedName = generateRandomFilename();

    // Replace .mmenc extension with the original file extension
    if (!extension.isEmpty()) {
        obfuscatedName = obfuscatedName.replace(".mmenc", "." + extension);
    } else {
        obfuscatedName = obfuscatedName.replace(".mmenc", "");
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
            qWarning() << "Failed to generate unique temp filename after" << maxAttempts << "attempts";
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

    qDebug() << "Attempting to open file:" << tempFile;
    qDebug() << "File size:" << fileInfo.size() << "bytes";
    qDebug() << "Using app:" << (appPath == "default" ? "default system app" : appPath);

    // Safety check for empty app path
    if (appPath.isEmpty()) {
        qDebug() << "WARNING: Empty app path, falling back to default app";
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

        // Add a small delay to ensure file is fully written
        QCoreApplication::processEvents();
        QThread::msleep(100);

        if (!QDesktopServices::openUrl(fileUrl)) {
            // Try alternative method on Windows
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << "start" << "" << QDir::toNativeSeparators(tempFile);
            if (QProcess::startDetached(command, args)) {
                qDebug() << "Opened file with Windows start command:" << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the default application.\n\nFile location: %1").arg(tempFile));
        } else {
            qDebug() << "Opened file with fallback default app:" << tempFile;
        }
        return;
    }

    if (appPath == "default") {
        // Use system default application
        QUrl fileUrl = QUrl::fromLocalFile(tempFile);

        // Add a small delay to ensure file is fully written
        QCoreApplication::processEvents();
        QThread::msleep(100);

        if (!QDesktopServices::openUrl(fileUrl)) {
            // Try alternative method on Windows
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << "start" << "" << QDir::toNativeSeparators(tempFile);
            if (QProcess::startDetached(command, args)) {
                qDebug() << "Opened file with fallback Windows start command:" << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the default application.\n\nFile location: %1").arg(tempFile));
        } else {
            qDebug() << "Opened file with default app:" << tempFile;
        }
    } else {
        // Use specific application
        QStringList arguments;
        arguments << QDir::toNativeSeparators(tempFile);

        // Set working directory to the app's directory
        QFileInfo appInfo(appPath);
        QString workingDir = appInfo.absolutePath();

        // Add a small delay to ensure file is fully written
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
            // Try using cmd.exe on Windows
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << QString("\"%1\" \"%2\"").arg(
                QDir::toNativeSeparators(appPath),
                QDir::toNativeSeparators(tempFile));

            if (QProcess::startDetached(command, args)) {
                qDebug() << "Opened file with Windows cmd command:" << appPath << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the selected application.\n\nApp: %1\nFile: %2\n\nTry opening the file manually from the temp folder.").arg(appPath, tempFile));
        } else {
            qDebug() << "Opened file with app:" << appPath << "file:" << tempFile << "PID:" << pid;
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
// Temp file monitoring and cleanup
// ============================================================================

void Operations_EncryptedData::startTempFileMonitoring()
{
    if (!m_tempFileCleanupTimer) {
        m_tempFileCleanupTimer = new QTimer(this);
        connect(m_tempFileCleanupTimer, &QTimer::timeout, this, &Operations_EncryptedData::onCleanupTimerTimeout);
        m_tempFileCleanupTimer->start(60000); // 1 minute = 60000 ms
        qDebug() << "Started temp file cleanup timer with 1-minute interval";
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
            if (OperationsFiles::secureDelete(filePath, 3)) {
                filesDeleted++;
                qDebug() << "Cleaned up temp file:" << filePath;
            } else {
                qWarning() << "Failed to clean up temp file:" << filePath;
            }
        } else {
            qDebug() << "Temp file still in use:" << filePath;
        }
    }

    if (filesDeleted > 0) {
        qDebug() << "Cleanup completed. Deleted" << filesDeleted << "temp files";
    }
}

bool Operations_EncryptedData::isFileInUse(const QString& filePath)
{
#ifdef Q_OS_WIN
    // On Windows, try to open the file with exclusive access
    // If it fails, the file is likely in use
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
        // For safety, consider it not in use so it can be cleaned up
        return false;
    } else {
        // Successfully opened with exclusive access, file is not in use
        CloseHandle(fileHandle);
        return false;
    }
#else
    // For non-Windows systems, this is a fallback
    // Simply try to open and close the file
    QFile file(filePath);
    if (file.open(QIODevice::ReadWrite)) {
        file.close();
        return false; // Not in use
    }
    return true; // Assume in use if we can't open it
#endif
}

// ============================================================================
// Temp decryption slots
// ============================================================================

void Operations_EncryptedData::onTempDecryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onTempDecryptionFinished(bool success, const QString& errorMessage)
{
    qDebug() << "=== onTempDecryptionFinished called ===";
    qDebug() << "Success:" << success;
    qDebug() << "m_pendingAppToOpen at start of onTempDecryptionFinished:" << m_pendingAppToOpen;

    // Store the app choice in a local variable immediately to prevent it from being cleared
    QString localAppToOpen = m_pendingAppToOpen;
    qDebug() << "Stored in localAppToOpen:" << localAppToOpen;

    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_tempDecryptWorkerThread) {
        m_tempDecryptWorkerThread->quit();
        m_tempDecryptWorkerThread->wait();
        m_tempDecryptWorkerThread->deleteLater();
        m_tempDecryptWorkerThread = nullptr;
    }

    if (m_tempDecryptWorker) {
        QString tempFilePath = m_tempDecryptWorker->m_targetFile;
        qDebug() << "Got tempFilePath:" << tempFilePath;
        qDebug() << "m_pendingAppToOpen after getting tempFilePath:" << m_pendingAppToOpen;
        qDebug() << "localAppToOpen after getting tempFilePath:" << localAppToOpen;

        if (success) {
            qDebug() << "m_pendingAppToOpen in success branch:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen in success branch:" << localAppToOpen;

            // Add a brief delay to ensure file system operations are complete
            QCoreApplication::processEvents();
            qDebug() << "m_pendingAppToOpen after processEvents:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen after processEvents:" << localAppToOpen;

            QThread::msleep(200);
            qDebug() << "m_pendingAppToOpen after msleep:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen after msleep:" << localAppToOpen;

            // Verify file exists and has content before trying to open
            QFileInfo fileInfo(tempFilePath);
            qDebug() << "Temp decryption finished. File:" << tempFilePath;
            qDebug() << "File exists:" << fileInfo.exists() << "Size:" << fileInfo.size() << "bytes";
            qDebug() << "Pending app JUST BEFORE openFileWithApp:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen JUST BEFORE openFileWithApp:" << localAppToOpen;

            if (!fileInfo.exists() || fileInfo.size() == 0) {
                QMessageBox::critical(m_mainWindow, "File Error",
                                      QString("The decrypted temporary file is missing or empty.\n\nExpected location: %1").arg(tempFilePath));
            } else {
                qDebug() << "About to call openFileWithApp with localAppToOpen:" << localAppToOpen;
                // Use the local variable instead of the member variable
                openFileWithApp(tempFilePath, localAppToOpen);
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
    qDebug() << "About to clear m_pendingAppToOpen at end of function";
    m_pendingAppToOpen.clear();
    qDebug() << "Cleared m_pendingAppToOpen at end of function";
}

void Operations_EncryptedData::onTempDecryptionCancelled()
{
    qDebug() << "=== onTempDecryptionCancelled called ===";
    qDebug() << "m_pendingAppToOpen before cancel processing:" << m_pendingAppToOpen;

    if (m_tempDecryptWorker) {
        m_tempDecryptWorker->cancel();
    }

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }

    // Clear pending app
    qDebug() << "About to clear m_pendingAppToOpen in onTempDecryptionCancelled";
    m_pendingAppToOpen.clear();
    qDebug() << "Cleared m_pendingAppToOpen in onTempDecryptionCancelled";
}

// ============================================================================
// Existing functionality (encryption, regular decryption, etc.)
// ============================================================================

// [Insert all the existing methods from the original file here...]
// I'll continue with the rest of the existing methods in the next part

void Operations_EncryptedData::encryptSelectedFile()
{
    // Open file dialog to select multiple files for encryption
    QStringList filePaths = QFileDialog::getOpenFileNames(
        m_mainWindow,
        "Select Files to Encrypt",
        QString(),
        "All Files (*.*)"
        );

    // Check if user selected files (didn't cancel)
    if (filePaths.isEmpty()) {
        qDebug() << "User cancelled file selection";
        return;
    }

    qDebug() << "Selected" << filePaths.size() << "files for encryption";

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

    // *** NEW: Extract video thumbnails in main thread before encryption ***
    QMap<QString, QPixmap> videoThumbnails;
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "mpg", "mpeg", "m2v", "divx", "xvid"};

    for (const QString& sourceFile : validFiles) {
        QFileInfo fileInfo(sourceFile);
        QString extension = fileInfo.suffix().toLower();

        // Check if this is a video file
        if (videoExtensions.contains(extension)) {
            qDebug() << "Pre-extracting video thumbnail for:" << sourceFile;

            if (m_iconProvider) {
                QPixmap videoThumbnail = m_iconProvider->getVideoThumbnail(sourceFile, 64);

                if (!videoThumbnail.isNull()) {
                    videoThumbnails[sourceFile] = videoThumbnail;
                    qDebug() << "Successfully pre-extracted video thumbnail for:" << fileInfo.fileName();
                } else {
                    qDebug() << "Failed to pre-extract video thumbnail for:" << fileInfo.fileName();
                }
            } else {
                qWarning() << "FileIconProvider not available for video thumbnail extraction";
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

    // Set up progress dialog
    QString progressText;
    if (validFiles.size() == 1) {
        progressText = "Encrypting file...";
    } else {
        progressText = QString("Encrypting %1 files...").arg(validFiles.size());
    }

    m_progressDialog = new QProgressDialog(progressText, "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("File Encryption");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread - PASS PRE-EXTRACTED THUMBNAILS
    m_workerThread = new QThread(this);
    m_worker = new EncryptionWorker(validFiles, targetPaths, encryptionKey, username, videoThumbnails);
    m_worker->moveToThread(m_workerThread);

    // Connect signals - choose appropriate connections based on file count
    connect(m_workerThread, &QThread::started, m_worker, &EncryptionWorker::doEncryption);
    connect(m_worker, &EncryptionWorker::progressUpdated, this, &Operations_EncryptedData::onEncryptionProgress);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onEncryptionCancelled);
    connect(m_worker, &EncryptionWorker::videoThumbnailExtracted,
            this, &Operations_EncryptedData::storeVideoThumbnail);

    if (validFiles.size() == 1) {
        // Single file - use backward compatible signal
        connect(m_worker, &EncryptionWorker::encryptionFinished, this, &Operations_EncryptedData::onEncryptionFinished);
    } else {
        // Multiple files - use new signals
        connect(m_worker, &EncryptionWorker::fileProgressUpdate, this, &Operations_EncryptedData::onFileProgressUpdate);
        connect(m_worker, &EncryptionWorker::multiFileEncryptionFinished, this, &Operations_EncryptedData::onMultiFileEncryptionFinished);
    }

    // Start encryption
    m_workerThread->start();
    m_progressDialog->exec();
}

// New slot for file progress updates
void Operations_EncryptedData::onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName)
{
    if (m_progressDialog) {
        QString progressText = QString("Encrypting file %1 of %2: %3").arg(currentFile).arg(totalFiles).arg(fileName);
        m_progressDialog->setLabelText(progressText);
    }
}


void Operations_EncryptedData::onMultiFileEncryptionFinished(bool success, const QString& errorMessage,
                                                             const QStringList& successfulFiles, const QStringList& failedFiles)
{
    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_worker) {
        if (success && !m_worker->m_targetFiles.isEmpty()) {
            // For multiple files, refresh to show the first successfully encrypted file
            // Find the first successful file's encrypted path
            QString firstSuccessfulEncryptedFile;
            for (int i = 0; i < m_worker->m_sourceFiles.size(); ++i) {
                QString sourceFileName = QFileInfo(m_worker->m_sourceFiles[i]).fileName();
                if (successfulFiles.contains(sourceFileName)) {
                    firstSuccessfulEncryptedFile = m_worker->m_targetFiles[i];
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
            showMultiFileSuccessDialog(m_worker->m_sourceFiles, successfulFiles, failedFiles);
        } else {
            QMessageBox::critical(m_mainWindow, "Encryption Failed", errorMessage);
        }

        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

// Multi-file success dialog
void Operations_EncryptedData::showMultiFileSuccessDialog(const QStringList& originalFiles,
                                                          const QStringList& successfulFiles, const QStringList& failedFiles)
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Encryption Complete");
    msgBox.setIcon(QMessageBox::Information);

    QString message;
    if (failedFiles.isEmpty()) {
        // All files succeeded
        message = QString("All %1 files encrypted successfully!").arg(successfulFiles.size());
        msgBox.setText(message);
        msgBox.setInformativeText("Would you like to securely delete the original unencrypted files?");
    } else if (successfulFiles.isEmpty()) {
        // All files failed
        message = QString("Failed to encrypt any files.");
        msgBox.setText(message);
        msgBox.setInformativeText(QString("Failed files:\n%1").arg(failedFiles.join("\n")));
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();
        return;
    } else {
        // Partial success
        message = QString("Partial success: %1 of %2 files encrypted successfully").arg(successfulFiles.size()).arg(originalFiles.size());
        msgBox.setText(message);
        msgBox.setInformativeText("Would you like to securely delete the original files that were successfully encrypted?");
    }

    QPushButton* deleteButton = msgBox.addButton("Delete Originals", QMessageBox::YesRole);
    QPushButton* keepButton = msgBox.addButton("Keep Originals", QMessageBox::NoRole);
    msgBox.setDefaultButton(keepButton);

    msgBox.exec();

    // Check if the delete button was clicked
    if (msgBox.clickedButton() == deleteButton) {
        // Find the original file paths for successfully encrypted files
        QStringList filesToDelete;
        for (const QString& originalFile : originalFiles) {
            QString fileName = QFileInfo(originalFile).fileName();
            if (successfulFiles.contains(fileName)) {
                filesToDelete.append(originalFile);
            }
        }

        if (!filesToDelete.isEmpty()) {
            // Securely delete the original files
            QStringList deletedFiles;
            QStringList deletionFailures;

            for (const QString& filePath : filesToDelete) {
                if (OperationsFiles::secureDelete(filePath, 3, true)) {
                    deletedFiles.append(QFileInfo(filePath).fileName());
                } else {
                    deletionFailures.append(QFileInfo(filePath).fileName());
                }
            }

            // Show deletion results
            QString deletionMessage;
            if (deletionFailures.isEmpty()) {
                deletionMessage = QString("All %1 original files have been securely deleted.").arg(deletedFiles.size());
                QMessageBox::information(m_mainWindow, "Files Deleted", deletionMessage);
            } else {
                deletionMessage = QString("Successfully deleted %1 files.\n\nFailed to delete:\n%2")
                .arg(deletedFiles.size())
                    .arg(deletionFailures.join("\n"));
                QMessageBox::warning(m_mainWindow, "Partial Deletion", deletionMessage);
            }
        }
    }
}

QString Operations_EncryptedData::determineFileType(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();

    // Video files - expanded list for better Windows thumbnail support
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
        "pkg", "apk", "ipa", "msi", "exe" // executable archives
    };
    if (archiveExtensions.contains(extension)) {
        return "Archive";
    }

    // Default to "Other" for unrecognized file types
    return "Other";
}

QString Operations_EncryptedData::generateRandomFilename()
{
    const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const int length = 32; // Generate a 32-character random string

    QString randomString;
    for (int i = 0; i < length; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        randomString.append(chars[index]);
    }

    return randomString + ".mmenc";
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
            qWarning() << "Failed to create directory:" << typePath;
            return QString();
        }
    }

    // Generate unique filename
    QString filename;
    int attempts = 0;
    const int maxAttempts = 100;

    do {
        filename = generateRandomFilename();
        attempts++;

        if (attempts > maxAttempts) {
            qWarning() << "Failed to generate unique filename after" << maxAttempts << "attempts";
            return QString();
        }

    } while (checkFilenameExists(typePath, filename));

    return QDir(typePath).absoluteFilePath(filename);
}

void Operations_EncryptedData::showSuccessDialog(const QString& encryptedFile, const QString& originalFile)
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Encryption Complete");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("File encrypted successfully!");
    msgBox.setInformativeText("The file has been encrypted and saved securely.\n\n"
                              "Would you like to securely delete the original unencrypted file?");

    QPushButton* deleteButton = msgBox.addButton("Delete Original", QMessageBox::YesRole);
    QPushButton* keepButton = msgBox.addButton("Keep Original", QMessageBox::NoRole);
    msgBox.setDefaultButton(keepButton);

    msgBox.exec();

    // Check if the delete button was clicked by comparing button text
    if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Delete Original") {
        // Securely delete the original file
        bool deleted = OperationsFiles::secureDelete(originalFile, 3, true); // allow external file

        if (deleted) {
            QMessageBox::information(m_mainWindow, "File Deleted",
                                     "The original file has been securely deleted.");
        } else {
            QMessageBox::warning(m_mainWindow, "Deletion Failed",
                                 "Failed to securely delete the original file. "
                                 "You may need to delete it manually.");
        }
    }
}

void Operations_EncryptedData::onEncryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onEncryptionFinished(bool success, const QString& errorMessage)
{
    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_worker) {
        // For single file encryption (backward compatibility), access first element from the lists
        QString originalFile = m_worker->m_sourceFiles.first();
        QString encryptedFile = m_worker->m_targetFiles.first();

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

void Operations_EncryptedData::onEncryptionCancelled()
{
    if (m_worker) {
        m_worker->cancel();
    }

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }
}

void Operations_EncryptedData::decryptSelectedFile()
{
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
    qDebug() << "Validating encryption key for file:" << encryptedFilePath;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey, true)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful";

    // Get the original filename
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to extract original filename from encrypted file.");
        return;
    }

    // Open save dialog with original filename pre-filled
    QString suggestedPath = QDir::homePath() + "/" + originalFilename;
    QString targetPath = QFileDialog::getSaveFileName(
        m_mainWindow,
        "Save Decrypted File As",
        suggestedPath,
        "All Files (*.*)"
        );

    if (targetPath.isEmpty()) {
        qDebug() << "User cancelled file save dialog";
        return;
    }

    // Validate the target path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        targetPath, InputValidation::InputType::ExternalFilePath, 1000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "The selected save path is invalid: " + result.errorMessage);
        return;
    }

    // Check if target file already exists
    if (QFile::exists(targetPath)) {
        int ret = QMessageBox::question(m_mainWindow, "File Exists",
                                        "The target file already exists. Do you want to overwrite it?",
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
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
    connect(m_decryptWorkerThread, &QThread::started, m_decryptWorker, &DecryptionWorker::doDecryption);
    connect(m_decryptWorker, &DecryptionWorker::progressUpdated, this, &Operations_EncryptedData::onDecryptionProgress);
    connect(m_decryptWorker, &DecryptionWorker::decryptionFinished, this, &Operations_EncryptedData::onDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onDecryptionCancelled);

    // Start decryption
    m_decryptWorkerThread->start();
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
    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_decryptWorkerThread) {
        m_decryptWorkerThread->quit();
        m_decryptWorkerThread->wait();
        m_decryptWorkerThread->deleteLater();
        m_decryptWorkerThread = nullptr;
    }

    if (m_decryptWorker) {
        QString encryptedFile = m_decryptWorker->m_sourceFile;
        QString decryptedFile = m_decryptWorker->m_targetFile;

        if (success) {
            // Show success dialog and ask about deleting encrypted file
            QMessageBox msgBox(m_mainWindow);
            msgBox.setWindowTitle("Decryption Complete");
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setText("File decrypted successfully!");
            msgBox.setInformativeText("The file has been decrypted and saved.\n\n"
                                      "Would you like to delete the encrypted copy?");

            QPushButton* deleteButton = msgBox.addButton("Delete Encrypted Copy", QMessageBox::YesRole);
            QPushButton* keepButton = msgBox.addButton("Keep Encrypted Copy", QMessageBox::NoRole);
            msgBox.setDefaultButton(keepButton);

            msgBox.exec();

            // Check if the delete button was clicked
            if (msgBox.clickedButton() == deleteButton) {
                // Delete the encrypted file
                if (QFile::remove(encryptedFile)) {
                    // Refresh the list since we removed a file
                    populateEncryptedFilesList();
                    QMessageBox::information(m_mainWindow, "File Deleted",
                                             "The encrypted file has been deleted.");
                } else {
                    QMessageBox::warning(m_mainWindow, "Deletion Failed",
                                         "Failed to delete the encrypted file. "
                                         "You may need to delete it manually.");
                }
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Decryption Failed",
                                  "File decryption failed: " + errorMessage);
        }

        m_decryptWorker->deleteLater();
        m_decryptWorker = nullptr;
    }
}

void Operations_EncryptedData::onDecryptionCancelled()
{
    if (m_decryptWorker) {
        m_decryptWorker->cancel();
    }

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }
}

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

    // Try to get from cache first (faster)
    if (m_fileMetadataCache.contains(encryptedFilePath)) {
        originalFilename = m_fileMetadataCache[encryptedFilePath].filename;
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

QString Operations_EncryptedData::getOriginalFilename(const QString& encryptedFilePath)
{
    if (!m_metadataManager) {
        qWarning() << "Metadata manager not initialized";
        return QString();
    }

    return m_metadataManager->getFilenameFromFile(encryptedFilePath);
}

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

void Operations_EncryptedData::populateEncryptedFilesList()
{
    qDebug() << "Starting populateEncryptedFilesList with hierarchical filtering";

    // Prevent recursive updates
    if (m_updatingFilters) {
        return;
    }
    m_updatingFilters = true;

    // Clear current state
    m_fileMetadataCache.clear();
    m_currentFilteredFiles.clear();
    m_pendingThumbnailItems.clear();

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
        m_updatingFilters = false;
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

    // Collect all valid file paths for cache cleanup
    QStringList allValidFilePaths;

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

            // Try to read metadata for this file
            EncryptedFileMetadata::FileMetadata metadata;
            if (m_metadataManager && m_metadataManager->readMetadataFromFile(encryptedFilePath, metadata)) {
                // Valid metadata found
                m_fileMetadataCache[encryptedFilePath] = metadata;
                allValidFilePaths.append(encryptedFilePath);

                qDebug() << "Loaded metadata for:" << metadata.filename
                         << "category:" << metadata.category
                         << "tags:" << metadata.tags.join(", ");
            } else {
                // Fallback: try to get original filename using legacy method
                QString originalFilename = getOriginalFilename(encryptedFilePath);
                if (!originalFilename.isEmpty()) {
                    // Create basic metadata with just filename
                    metadata = EncryptedFileMetadata::FileMetadata(originalFilename);
                    m_fileMetadataCache[encryptedFilePath] = metadata;
                    allValidFilePaths.append(encryptedFilePath);

                    qDebug() << "Using legacy filename for:" << originalFilename;
                }
            }
        }
    }

    qDebug() << "Loaded metadata for" << m_fileMetadataCache.size() << "files";

    // Clean up orphaned thumbnail cache entries
    if (m_thumbnailCache && currentSortType == "All") {
        qDebug() << "Running thumbnail cleanup (showing All files)";
        m_thumbnailCache->cleanupOrphanedThumbnails(allValidFilePaths);
    }

    // Populate categories list based on loaded metadata
    populateCategoriesList();

    // Reset category selection to "All"
    if (m_mainWindow->ui->listWidget_DataENC_Categories->count() > 0) {
        m_mainWindow->ui->listWidget_DataENC_Categories->setCurrentRow(0); // "All" is always first
    }

    m_updatingFilters = false;

    // This will trigger onCategorySelectionChanged which will handle the rest
    qDebug() << "Finished populateEncryptedFilesList, category selection will trigger rest of filtering";
}

// New method: Populate categories list
void Operations_EncryptedData::populateCategoriesList()
{
    qDebug() << "Populating categories list";

    // Clear current categories list
    m_mainWindow->ui->listWidget_DataENC_Categories->clear();

    // Collect all unique categories that have files
    QSet<QString> categoriesWithFiles;

    for (auto it = m_fileMetadataCache.begin(); it != m_fileMetadataCache.end(); ++it) {
        const EncryptedFileMetadata::FileMetadata& metadata = it.value();

        if (metadata.category.isEmpty()) {
            categoriesWithFiles.insert("Uncategorized");
        } else {
            categoriesWithFiles.insert(metadata.category);
        }
    }

    // Always add "All" at the top
    QListWidgetItem* allItem = new QListWidgetItem("All");
    allItem->setData(Qt::UserRole, "All");
    m_mainWindow->ui->listWidget_DataENC_Categories->addItem(allItem);

    // Add categories in alphabetical order
    QStringList sortedCategories(categoriesWithFiles.begin(), categoriesWithFiles.end());
    sortedCategories.sort();

    // Remove "Uncategorized" if it exists (we'll add it at the end)
    sortedCategories.removeAll("Uncategorized");

    for (const QString& category : sortedCategories) {
        QListWidgetItem* item = new QListWidgetItem(category);
        item->setData(Qt::UserRole, category);
        m_mainWindow->ui->listWidget_DataENC_Categories->addItem(item);
    }

    // Add "Uncategorized" at the bottom if it has files
    if (categoriesWithFiles.contains("Uncategorized")) {
        QListWidgetItem* uncategorizedItem = new QListWidgetItem("Uncategorized");
        uncategorizedItem->setData(Qt::UserRole, "Uncategorized");
        m_mainWindow->ui->listWidget_DataENC_Categories->addItem(uncategorizedItem);
    }

    qDebug() << "Added" << m_mainWindow->ui->listWidget_DataENC_Categories->count()
             << "categories (including All)";
}

// New method: Handle category selection changes
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

    // Filter files by selected category
    m_currentFilteredFiles.clear();

    for (auto it = m_fileMetadataCache.begin(); it != m_fileMetadataCache.end(); ++it) {
        const QString& filePath = it.key();
        const EncryptedFileMetadata::FileMetadata& metadata = it.value();

        bool includeFile = false;

        if (selectedCategory == "All") {
            includeFile = true;
        } else if (selectedCategory == "Uncategorized") {
            includeFile = metadata.category.isEmpty();
        } else {
            includeFile = (metadata.category == selectedCategory);
        }

        if (includeFile) {
            m_currentFilteredFiles.append(filePath);
        }
    }

    qDebug() << "Filtered to" << m_currentFilteredFiles.size() << "files for category:" << selectedCategory;

    // Populate tags list based on filtered files
    populateTagsList();

    // Update file list display
    updateFileListDisplay();
}

// New method: Populate tags list with checkboxes
void Operations_EncryptedData::populateTagsList()
{
    qDebug() << "Populating tags list";

    // Clear current tags list
    m_mainWindow->ui->listWidget_DataENC_Tags->clear();

    // Collect all unique tags from currently filtered files
    QSet<QString> allTags;

    for (const QString& filePath : m_currentFilteredFiles) {
        if (m_fileMetadataCache.contains(filePath)) {
            const EncryptedFileMetadata::FileMetadata& metadata = m_fileMetadataCache[filePath];
            for (const QString& tag : metadata.tags) {
                if (!tag.isEmpty()) {
                    allTags.insert(tag);
                }
            }
        }
    }

    // Add tags as checkbox items in alphabetical order
    QStringList sortedTags(allTags.begin(), allTags.end());
    sortedTags.sort();

    for (const QString& tag : sortedTags) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(tag);
        item->setData(Qt::UserRole, tag);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);

        m_mainWindow->ui->listWidget_DataENC_Tags->addItem(item);
    }

    // Connect to checkbox changes
    connect(m_mainWindow->ui->listWidget_DataENC_Tags, &QListWidget::itemChanged,
            this, &Operations_EncryptedData::onTagCheckboxChanged);

    qDebug() << "Added" << sortedTags.size() << "tags with checkboxes";
}

// New method: Handle tag checkbox changes
void Operations_EncryptedData::onTagCheckboxChanged()
{
    if (m_updatingFilters) {
        return;
    }

    qDebug() << "Tag selection changed, updating file list";

    // Update file list display based on current tag selection
    updateFileListDisplay();
}

// New method: Update the actual file list display
void Operations_EncryptedData::updateFileListDisplay()
{
    qDebug() << "Updating file list display";

    // Clear current file list and pending items
    m_mainWindow->ui->listWidget_DataENC_FileList->clear();
    m_pendingThumbnailItems.clear();

    // Get checked tags
    QStringList checkedTags;
    for (int i = 0; i < m_mainWindow->ui->listWidget_DataENC_Tags->count(); ++i) {
        QListWidgetItem* item = m_mainWindow->ui->listWidget_DataENC_Tags->item(i);
        if (item && item->checkState() == Qt::Checked) {
            checkedTags.append(item->data(Qt::UserRole).toString());
        }
    }

    qDebug() << "Checked tags:" << checkedTags;

    // Filter files by checked tags (AND logic)
    QStringList finalFilteredFiles;

    for (const QString& filePath : m_currentFilteredFiles) {
        if (!m_fileMetadataCache.contains(filePath)) {
            continue;
        }

        const EncryptedFileMetadata::FileMetadata& metadata = m_fileMetadataCache[filePath];

        bool includeFile = true;

        if (!checkedTags.isEmpty()) {
            // AND logic: file must have ALL checked tags
            for (const QString& requiredTag : checkedTags) {
                if (!metadata.tags.contains(requiredTag)) {
                    includeFile = false;
                    break;
                }
            }
        }

        if (includeFile) {
            finalFilteredFiles.append(filePath);
        }
    }

    qDebug() << "Final filtered files count:" << finalFilteredFiles.size();

    // Create list items for filtered files
    for (const QString& encryptedFilePath : finalFilteredFiles) {
        const EncryptedFileMetadata::FileMetadata& metadata = m_fileMetadataCache[encryptedFilePath];

        // Determine file type directory from path
        QFileInfo fileInfo(encryptedFilePath);
        QString fileTypeDir = fileInfo.dir().dirName(); // e.g., "Image", "Video", etc.

        // Create custom widget for this file
        EncryptedFileItemWidget* customWidget = new EncryptedFileItemWidget();

        // Set file information
        customWidget->setFileInfo(metadata.filename, encryptedFilePath, fileTypeDir);

        // Check if we have a cached thumbnail
        QPixmap icon;
        bool hasCachedThumbnail = false;

        if (m_thumbnailCache) {
            // Check for cached thumbnails for both images and videos
            if (fileTypeDir == "Image" || fileTypeDir == "Video") {
                hasCachedThumbnail = m_thumbnailCache->hasThumbnail(encryptedFilePath);

                if (hasCachedThumbnail) {
                    icon = m_thumbnailCache->getThumbnail(encryptedFilePath, EncryptedFileItemWidget::getIconSize());
                    if (!icon.isNull()) {
                        qDebug() << "Successfully loaded cached thumbnail for:" << metadata.filename;
                    } else {
                        qWarning() << "Failed to load cached thumbnail for:" << metadata.filename;
                    }
                }
            }
        }

        // If no cached thumbnail, use default icon
        if (icon.isNull()) {
            icon = getIconForFileType(metadata.filename, fileTypeDir);
        }

        customWidget->setIcon(icon);

        // Create list widget item
        QListWidgetItem* item = new QListWidgetItem();
        item->setData(Qt::UserRole, encryptedFilePath);
        item->setData(Qt::UserRole + 1, fileTypeDir);
        item->setData(Qt::UserRole + 2, metadata.filename);

        // Set item size to accommodate custom widget
        int itemHeight = EncryptedFileItemWidget::getIconSize() + 8; // icon size + padding
        item->setSizeHint(QSize(0, itemHeight));

        // Add item to list and set custom widget
        m_mainWindow->ui->listWidget_DataENC_FileList->addItem(item);
        m_mainWindow->ui->listWidget_DataENC_FileList->setItemWidget(item, customWidget);

        // Add to pending thumbnails if it's an image or video without cached thumbnail
        if ((fileTypeDir == "Image" || fileTypeDir == "Video") && m_thumbnailCache && !hasCachedThumbnail) {
            m_pendingThumbnailItems.append(item);
            qDebug() << "Added to pending thumbnails:" << metadata.filename;
        }
    }

    // Start lazy loading for visible items
    startLazyLoadTimer();

    // Update button states after populating the list
    updateButtonStates();

    qDebug() << "File list display updated with" << finalFilteredFiles.size() << "items";
}

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

void Operations_EncryptedData::updateButtonStates()
{
    // Check if any item is selected in the file list
    bool hasSelection = (m_mainWindow->ui->listWidget_DataENC_FileList->currentItem() != nullptr);

    // Style for disabled buttons (same as operations_settings.cpp)
    QString disabledStyle = "color: #888888; background-color: #444444;";
    QString enabledStyle = ""; // Default style

    // Update Decrypt button
    m_mainWindow->ui->pushButton_DataENC_Decrypt->setEnabled(hasSelection);
    m_mainWindow->ui->pushButton_DataENC_Decrypt->setStyleSheet(hasSelection ? enabledStyle : disabledStyle);

    // Update Delete File button
    m_mainWindow->ui->pushButton_DataENC_DeleteFile->setEnabled(hasSelection);
    m_mainWindow->ui->pushButton_DataENC_DeleteFile->setStyleSheet(hasSelection ? enabledStyle : disabledStyle);
}


void Operations_EncryptedData::secureDeleteExternalFile()
{
    // Open file dialog to select a file for secure deletion
    QString filePath = QFileDialog::getOpenFileName(
        m_mainWindow,
        "Select File to Securely Delete",
        QDir::homePath(), // Start in user's home directory
        "All Files (*.*)"
        );

    // Check if user selected a file (didn't cancel)
    if (filePath.isEmpty()) {
        qDebug() << "User cancelled file selection for secure deletion";
        return;
    }

    qDebug() << "Selected file for secure deletion:" << filePath;

    // Validate the file path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        filePath, InputValidation::InputType::ExternalFilePath, 1000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "The selected file path is invalid: " + result.errorMessage);
        return;
    }

    // Check if file exists and is accessible
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(m_mainWindow, "File Not Found",
                             "The selected file does not exist or cannot be accessed.");
        return;
    }

    // Check if it's a directory (we only want to delete files)
    if (fileInfo.isDir()) {
        QMessageBox::warning(m_mainWindow, "Directory Selected",
                             "Please select a file, not a directory. Directory deletion is not supported.");
        return;
    }

    // Check if file is writable (needed for secure deletion)
    if (!fileInfo.isWritable()) {
        QMessageBox::warning(m_mainWindow, "File Access Error",
                             "The selected file cannot be deleted. It may be read-only or in use by another application.");
        return;
    }

    // Get just the filename for the confirmation dialog
    QString filename = fileInfo.fileName();

    // Show confirmation dialog
    QMessageBox confirmBox(m_mainWindow);
    confirmBox.setWindowTitle("Confirm Secure Deletion");
    confirmBox.setIcon(QMessageBox::Warning);
    confirmBox.setText(QString("Are you sure you want to permanently delete the file '%1'?").arg(filename));
    confirmBox.setInformativeText("This action cannot be undone. The file will be securely overwritten and permanently deleted.");

    QPushButton* deleteButton = confirmBox.addButton("Delete", QMessageBox::YesRole);
    QPushButton* cancelButton = confirmBox.addButton("Cancel", QMessageBox::NoRole);
    confirmBox.setDefaultButton(cancelButton); // Default to Cancel for safety

    confirmBox.exec();

    // Check if the delete button was clicked
    if (confirmBox.clickedButton() != deleteButton) {
        qDebug() << "User cancelled secure deletion";
        return; // User cancelled
    }

    // Perform secure deletion
    qDebug() << "Starting secure deletion of:" << filePath;
    bool deleted = OperationsFiles::secureDelete(filePath, 3, true); // Allow external files , 3 passes for secure delete.

    if (deleted) {
        QMessageBox::information(m_mainWindow, "File Deleted",
                                 QString("'%1' has been securely deleted.").arg(filename));
        qDebug() << "Secure deletion successful:" << filePath;
    } else {
        QMessageBox::critical(m_mainWindow, "Deletion Failed",
                              QString("Failed to securely delete '%1'. The file may be in use by another application or you may not have sufficient permissions.").arg(filename));
        qWarning() << "Secure deletion failed:" << filePath;
    }

}

// Thumbnail Generation

QPixmap Operations_EncryptedData::getIconForFileType(const QString& originalFilename, const QString& fileType)
{
    QPixmap icon;

    if (fileType == "Video") {
        // For videos, check if we have a cached thumbnail first
        // (This will be set during encryption process)
        // For now, return default video icon - thumbnail will be loaded by lazy loading
        icon = m_iconProvider->getDefaultVideoIcon(EncryptedFileItemWidget::getIconSize());
    } else if (fileType == "Image") {
        icon = m_iconProvider->getDefaultImageIcon(EncryptedFileItemWidget::getIconSize());
    } else if (fileType == "Audio") {
        icon = m_iconProvider->getDefaultAudioIcon(EncryptedFileItemWidget::getIconSize());
    } else if (fileType == "Document") {
        icon = m_iconProvider->getDefaultDocumentIcon(EncryptedFileItemWidget::getIconSize());
    } else if (fileType == "Archive") {
        icon = m_iconProvider->getDefaultArchiveIcon(EncryptedFileItemWidget::getIconSize());
    } else {
        icon = m_iconProvider->getDefaultFileIcon(EncryptedFileItemWidget::getIconSize());
    }

    return icon;
}

void Operations_EncryptedData::startLazyLoadTimer()
{
    if (m_lazyLoadTimer && !m_pendingThumbnailItems.isEmpty()) {
        m_lazyLoadTimer->start();
    }
}

void Operations_EncryptedData::onLazyLoadTimeout()
{
    checkVisibleItems();
}

void Operations_EncryptedData::onListScrolled()
{
    // Restart the lazy load timer when user scrolls
    if (m_lazyLoadTimer) {
        m_lazyLoadTimer->start();
    }
}

void Operations_EncryptedData::checkVisibleItems()
{
    if (!m_mainWindow || !m_mainWindow->ui->listWidget_DataENC_FileList) {
        return;
    }

    QListWidget* listWidget = m_mainWindow->ui->listWidget_DataENC_FileList;

    // Get visible area
    QRect visibleRect = listWidget->viewport()->rect();

    // Check each pending thumbnail item
    QList<QListWidgetItem*> itemsToProcess;
    for (QListWidgetItem* item : m_pendingThumbnailItems) {
        if (!item) continue;

        QRect itemRect = listWidget->visualItemRect(item);
        if (visibleRect.intersects(itemRect)) {
            itemsToProcess.append(item);
        }

        // Limit processing to avoid blocking the UI
        if (itemsToProcess.size() >= 3) {
            break;
        }
    }

    // Process visible items
    for (QListWidgetItem* item : itemsToProcess) {
        generateThumbnailForItem(item);
        m_pendingThumbnailItems.removeOne(item);
    }

    // Continue processing if there are more items
    if (!m_pendingThumbnailItems.isEmpty() && m_lazyLoadTimer) {
        m_lazyLoadTimer->start();
    }
}

void Operations_EncryptedData::generateThumbnailForItem(QListWidgetItem* item)
{
    if (!item) return;

    QString encryptedFilePath = item->data(Qt::UserRole).toString();
    QString fileType = item->data(Qt::UserRole + 1).toString();
    QString originalFilename = item->data(Qt::UserRole + 2).toString();

    // Generate thumbnails for both images and videos
    if (fileType == "Image") {
        generateImageThumbnail(encryptedFilePath, originalFilename);
    } else if (fileType == "Video") {
        // For videos, we should already have the thumbnail cached from encryption
        // If not, we can't generate it from the encrypted file, so just use default icon
        qDebug() << "Video file without cached thumbnail (expected after encryption):" << originalFilename;
    }
}

void Operations_EncryptedData::generateImageThumbnail(const QString& encryptedFilePath, const QString& originalFilename)
{
    if (!m_thumbnailCache) {
        qWarning() << "ThumbnailCache not available for thumbnail generation";
        return;
    }

    qDebug() << "Generating thumbnail for:" << originalFilename;

    // Create a temporary file for decryption
    QString tempDir = QDir::tempPath();
    QString tempFileName = QString("temp_thumb_%1_%2").arg(
                                                          QDateTime::currentMSecsSinceEpoch()).arg(QFileInfo(originalFilename).fileName());
    QString tempFilePath = QDir(tempDir).absoluteFilePath(tempFileName);

    try {
        // Decrypt the image file to temp location
        QByteArray encryptionKey = m_mainWindow->user_Key;

        // Validate encryption key
        if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey)) {
            qWarning() << "Invalid encryption key for thumbnail generation:" << encryptedFilePath;
            return;
        }

        // Read and decrypt the file
        QFile encryptedFile(encryptedFilePath);
        if (!encryptedFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open encrypted file for thumbnail:" << encryptedFilePath;
            return;
        }

        // Skip the filename header
        quint32 filenameLength = 0;
        if (encryptedFile.read(reinterpret_cast<char*>(&filenameLength), sizeof(filenameLength)) != sizeof(filenameLength)) {
            qWarning() << "Failed to read filename length for thumbnail";
            return;
        }

        if (filenameLength == 0 || filenameLength > 1000) {
            qWarning() << "Invalid filename length for thumbnail:" << filenameLength;
            return;
        }

        // Skip the filename bytes
        if (encryptedFile.read(filenameLength).size() != static_cast<int>(filenameLength)) {
            qWarning() << "Failed to skip filename for thumbnail";
            return;
        }

        // Read and decrypt the image data in chunks
        QFile tempFile(tempFilePath);
        if (!tempFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to create temp file for thumbnail:" << tempFilePath;
            return;
        }

        while (!encryptedFile.atEnd()) {
            // Read chunk size
            quint32 chunkSize = 0;
            qint64 bytesRead = encryptedFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (bytesRead == 0) {
                break; // End of file
            }
            if (bytesRead != sizeof(chunkSize)) {
                qWarning() << "Failed to read chunk size for thumbnail";
                break;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                qWarning() << "Invalid chunk size for thumbnail:" << chunkSize;
                break;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = encryptedFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                qWarning() << "Failed to read complete encrypted chunk for thumbnail";
                break;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(encryptionKey, encryptedChunk);
            if (decryptedChunk.isEmpty()) {
                qWarning() << "Decryption failed for thumbnail chunk";
                break;
            }

            // Write decrypted chunk
            tempFile.write(decryptedChunk);
        }

        encryptedFile.close();
        tempFile.close();

        // Generate thumbnail from temp file
        QPixmap thumbnail = createImageThumbnail(tempFilePath, EncryptedFileItemWidget::getIconSize());

        if (!thumbnail.isNull()) {
            // Cache the thumbnail
            if (m_thumbnailCache->storeThumbnail(encryptedFilePath, thumbnail)) {
                qDebug() << "Successfully generated and cached thumbnail for:" << originalFilename;

                // Update the UI with the new thumbnail
                updateItemThumbnail(encryptedFilePath, thumbnail);
            } else {
                qWarning() << "Failed to cache thumbnail for:" << originalFilename;
            }
        } else {
            qWarning() << "Failed to create thumbnail from:" << tempFilePath;
        }

    } catch (const std::exception& e) {
        qWarning() << "Exception during thumbnail generation:" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception during thumbnail generation";
    }

    // Clean up temp file
    if (QFile::exists(tempFilePath)) {
        QFile::remove(tempFilePath);
    }
}

QPixmap Operations_EncryptedData::createImageThumbnail(const QString& tempImagePath, int size)
{
    QPixmap originalPixmap;
    if (!originalPixmap.load(tempImagePath)) {
        qWarning() << "Failed to load image for thumbnail:" << tempImagePath;
        return QPixmap();
    }

    // Create thumbnail with proper aspect ratio
    QPixmap thumbnail = originalPixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Add a subtle border to make thumbnails look more polished
    QPixmap borderedThumbnail(size, size);
    borderedThumbnail.fill(Qt::transparent);

    QPainter painter(&borderedThumbnail);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the thumbnail
    int x = (size - thumbnail.width()) / 2;
    int y = (size - thumbnail.height()) / 2;

    // Draw thumbnail
    painter.drawPixmap(x, y, thumbnail);

    // Draw subtle border
    painter.setPen(QPen(QColor(100, 100, 100), 1));
    painter.drawRect(x, y, thumbnail.width() - 1, thumbnail.height() - 1);

    return borderedThumbnail;
}

void Operations_EncryptedData::updateItemThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail)
{
    // Find the item in the list and update its thumbnail
    QListWidget* listWidget = m_mainWindow->ui->listWidget_DataENC_FileList;

    for (int i = 0; i < listWidget->count(); ++i) {
        QListWidgetItem* item = listWidget->item(i);
        if (item && item->data(Qt::UserRole).toString() == encryptedFilePath) {
            EncryptedFileItemWidget* widget = qobject_cast<EncryptedFileItemWidget*>(listWidget->itemWidget(item));
            if (widget) {
                widget->setIcon(thumbnail);
                qDebug() << "Updated thumbnail in UI for:" << widget->getOriginalFilename();
            }
            break;
        }
    }
}

void Operations_EncryptedData::storeVideoThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail)
{
    if (!m_thumbnailCache || thumbnail.isNull()) {
        return;
    }

    qDebug() << "Storing video thumbnail for encrypted file:" << encryptedFilePath;

    if (m_thumbnailCache->storeThumbnail(encryptedFilePath, thumbnail)) {
        qDebug() << "Successfully stored video thumbnail in cache";

        // Update the UI if this file is currently visible
        updateItemThumbnail(encryptedFilePath, thumbnail);
    } else {
        qWarning() << "Failed to store video thumbnail in cache";
    }
}


// New helper method: Refresh after encryption
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

// New helper method: Refresh after edit
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

// New helper method: Select category and optionally a file
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
        QTimer::singleShot(50, [this, filePathToSelect]() {
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
        });
    }
}

// New helper method: Remove file from cache and refresh display
void Operations_EncryptedData::removeFileFromCacheAndRefresh(const QString& encryptedFilePath)
{
    qDebug() << "Removing file from cache and refreshing display:" << encryptedFilePath;

    // Remove from metadata cache
    if (m_fileMetadataCache.contains(encryptedFilePath)) {
        m_fileMetadataCache.remove(encryptedFilePath);
        qDebug() << "Removed file from metadata cache";
    }

    // Remove from current filtered files list
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
// Context Menu Implementation
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

    // Create context menu
    QMenu contextMenu(m_mainWindow);

    // Add Open action
    QAction* openAction = contextMenu.addAction("Open");
    openAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(openAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuOpen);

    // Add Edit action
    QAction* editAction = contextMenu.addAction("Edit");
    editAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(editAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuEdit);

    // Add separator
    contextMenu.addSeparator();

    // Add Decrypt and Export action
    QAction* decryptAction = contextMenu.addAction("Decrypt and Export");
    decryptAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(decryptAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuDecryptExport);

    // Add separator
    contextMenu.addSeparator();

    // Add Delete action
    QAction* deleteAction = contextMenu.addAction("Delete");
    deleteAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
    connect(deleteAction, &QAction::triggered, this, &Operations_EncryptedData::onContextMenuDelete);

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

    // NEW: Check if file has an existing thumbnail and save it temporarily
    QPixmap existingThumbnail;
    bool hadThumbnail = false;

    if (m_thumbnailCache && m_thumbnailCache->hasThumbnail(encryptedFilePath)) {
        existingThumbnail = m_thumbnailCache->getThumbnail(encryptedFilePath, EncryptedFileItemWidget::getIconSize());
        hadThumbnail = !existingThumbnail.isNull();

        if (hadThumbnail) {
            qDebug() << "Preserving existing thumbnail for metadata edit:" << encryptedFilePath;
        }
    }

    // Create and show edit dialog
    EditEncryptedFileDialog editDialog(m_mainWindow);
    editDialog.initialize(encryptedFilePath, encryptionKey, username);

    // Show dialog and handle result
    int result = editDialog.exec();

    if (result == QDialog::Accepted) {
        // Changes were saved, refresh and select the edited file
        refreshAfterEdit(encryptedFilePath);

        // NEW: Restore the thumbnail if we had one before editing
        if (hadThumbnail && m_thumbnailCache) {
            // The file modification time has changed, so we need to store the thumbnail
            // with the new cache key that will be generated
            if (m_thumbnailCache->storeThumbnail(encryptedFilePath, existingThumbnail)) {
                qDebug() << "Successfully restored thumbnail after metadata edit";

                // Update the UI with the restored thumbnail
                updateItemThumbnail(encryptedFilePath, existingThumbnail);
            } else {
                qWarning() << "Failed to restore thumbnail after metadata edit";
            }
        }

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

// event filter
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

    // Pass event to parent class
    return QObject::eventFilter(watched, event);
}


//----------- Batch Decrypt Classes Impl ------------//

// =============================================================================
// BATCHDECRYPTIONPROGRESSDIALOG METHOD IMPLEMENTATIONS
// =============================================================================

BatchDecryptionProgressDialog::BatchDecryptionProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_overallProgress(nullptr)
    , m_fileProgress(nullptr)
    , m_statusLabel(nullptr)
    , m_cancelButton(nullptr)
    , m_cancelled(false)
{
    setupUI();
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle("Decrypting and Exporting Files");
    setFixedSize(500, 200);
}

void BatchDecryptionProgressDialog::setOverallProgress(int percentage)
{
    if (m_overallProgress) {
        m_overallProgress->setValue(percentage);
    }
}

void BatchDecryptionProgressDialog::setFileProgress(int percentage)
{
    if (m_fileProgress) {
        m_fileProgress->setValue(percentage);
    }
}

void BatchDecryptionProgressDialog::setStatusText(const QString& text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}

bool BatchDecryptionProgressDialog::wasCancelled() const
{
    return m_cancelled;
}

void BatchDecryptionProgressDialog::closeEvent(QCloseEvent* event)
{
    // Trigger cancellation if not already cancelled
    if (!m_cancelled) {
        onCancelClicked();
    }

    // Allow the dialog to close immediately
    event->accept();
}

void BatchDecryptionProgressDialog::reject()
{
    // Trigger cancellation if not already cancelled
    if (!m_cancelled) {
        onCancelClicked();
    }

    // Allow the dialog to close
    QDialog::reject();
}

void BatchDecryptionProgressDialog::onCancelClicked()
{
    m_cancelled = true;
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");

    // Emit signal to notify that cancellation was requested
    emit cancelled();
}

void BatchDecryptionProgressDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Status label - CHANGED: Left aligned, for filename only
    m_statusLabel = new QLabel("Preparing to decrypt files...");
    m_statusLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(m_statusLabel);

    // NEW: File count label - separate label for "File: x/y"
    m_fileCountLabel = new QLabel("");
    m_fileCountLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(m_fileCountLabel);

    // Overall progress
    QLabel* overallLabel = new QLabel("Overall Progress:");
    layout->addWidget(overallLabel);

    m_overallProgress = new QProgressBar();
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    layout->addWidget(m_overallProgress);

    // File progress
    QLabel* fileLabel = new QLabel("Current File Progress:");
    layout->addWidget(fileLabel);

    m_fileProgress = new QProgressBar();
    m_fileProgress->setRange(0, 100);
    m_fileProgress->setValue(0);
    layout->addWidget(m_fileProgress);

    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel");

    // Use direct connection instead of signal/slot
    connect(m_cancelButton, &QPushButton::clicked, [this]() {
        onCancelClicked();
    });

    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);
}

// Method to set file count text separately
void BatchDecryptionProgressDialog::setFileCountText(const QString& text)
{
    if (m_fileCountLabel) {
        m_fileCountLabel->setText(text);
    }
}

// =============================================================================
// BATCHDECRYPTIONWORKER METHOD IMPLEMENTATIONS
// =============================================================================

BatchDecryptionWorker::BatchDecryptionWorker(const QList<FileExportInfo>& fileInfos,
                                             const QByteArray& encryptionKey)
    : m_fileInfos(fileInfos)
    , m_encryptionKey(encryptionKey)
    , m_cancelled(false)
    , m_metadataManager(new EncryptedFileMetadata(encryptionKey, QString()))
{
}

BatchDecryptionWorker::~BatchDecryptionWorker()
{
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
}

void BatchDecryptionWorker::doDecryption()
{
    try {
        if (m_fileInfos.isEmpty()) {
            emit batchDecryptionFinished(false, "No files to decrypt", QStringList(), QStringList());
            return;
        }

        // Calculate total size for progress tracking
        qint64 totalSize = 0;
        for (const FileExportInfo& info : m_fileInfos) {
            totalSize += info.fileSize;
        }

        qint64 processedTotalSize = 0;
        QStringList successfulFiles;
        QStringList failedFiles;

        // Process each file
        for (int fileIndex = 0; fileIndex < m_fileInfos.size(); ++fileIndex) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    emit batchDecryptionFinished(false, "Operation was cancelled",
                                                 successfulFiles, failedFiles);
                    return;
                }
            }

            const FileExportInfo& fileInfo = m_fileInfos[fileIndex];

            // Emit file started signal
            emit fileStarted(fileIndex + 1, m_fileInfos.size(), fileInfo.originalFilename);

            // Create target directory if needed
            QFileInfo targetFileInfo(fileInfo.targetFile);
            QDir targetDir = targetFileInfo.dir();
            if (!targetDir.exists()) {
                if (!targetDir.mkpath(".")) {
                    failedFiles.append(QString("%1 (failed to create target directory)").arg(fileInfo.originalFilename));
                    processedTotalSize += fileInfo.fileSize;
                    continue;
                }
            }

            // Decrypt the file
            bool fileSuccess = decryptSingleFile(fileInfo, processedTotalSize, totalSize);

            if (fileSuccess) {
                successfulFiles.append(fileInfo.originalFilename);
            } else {
                failedFiles.append(fileInfo.originalFilename);
            }

            processedTotalSize += fileInfo.fileSize;

            // Update overall progress
            int overallPercentage = static_cast<int>((processedTotalSize * 100) / totalSize);
            emit overallProgressUpdated(overallPercentage);

            // Reset file progress for next file
            emit fileProgressUpdated(0);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        // Emit completion signal
        bool overallSuccess = !successfulFiles.isEmpty();
        QString resultMessage;

        if (successfulFiles.size() == m_fileInfos.size()) {
            resultMessage = QString("All %1 files decrypted successfully").arg(successfulFiles.size());
        } else if (successfulFiles.isEmpty()) {
            resultMessage = "All files failed to decrypt";
            overallSuccess = false;
        } else {
            resultMessage = QString("Partial success: %1 of %2 files decrypted successfully")
            .arg(successfulFiles.size()).arg(m_fileInfos.size());
        }

        emit batchDecryptionFinished(overallSuccess, resultMessage, successfulFiles, failedFiles);

    } catch (const std::exception& e) {
        emit batchDecryptionFinished(false, QString("Decryption error: %1").arg(e.what()),
                                     QStringList(), QStringList());
    } catch (...) {
        emit batchDecryptionFinished(false, "Unknown decryption error occurred",
                                     QStringList(), QStringList());
    }
}

void BatchDecryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

bool BatchDecryptionWorker::decryptSingleFile(const FileExportInfo& fileInfo,
                                              qint64 currentTotalProcessed, qint64 totalSize)
{
    try {
        QFile sourceFile(fileInfo.sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open source file:" << fileInfo.sourceFile;
            return false;
        }

        qint64 fileSize = sourceFile.size();
        qint64 processedFileSize = 0;

        // Skip the encrypted metadata header (same as in DecryptionWorker)
        quint32 metadataSize = 0;
        if (sourceFile.read(reinterpret_cast<char*>(&metadataSize), sizeof(metadataSize)) != sizeof(metadataSize)) {
            qWarning() << "Failed to read metadata size:" << fileInfo.sourceFile;
            return false;
        }

        if (metadataSize == 0 || metadataSize > EncryptedFileMetadata::MAX_ENCRYPTED_METADATA_SIZE) {
            qWarning() << "Invalid metadata size:" << metadataSize;
            return false;
        }

        // Skip the encrypted metadata chunk
        QByteArray encryptedMetadata = sourceFile.read(metadataSize);
        if (encryptedMetadata.size() != static_cast<int>(metadataSize)) {
            qWarning() << "Failed to skip metadata:" << fileInfo.sourceFile;
            return false;
        }

        processedFileSize += sizeof(metadataSize) + metadataSize;

        // Create target file
        QFile targetFile(fileInfo.targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to create target file:" << fileInfo.targetFile;
            return false;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(fileInfo.targetFile);
                    return false;
                }
            }

            // Read chunk size
            quint32 chunkSize = 0;
            qint64 bytesRead = sourceFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (bytesRead == 0) {
                break; // End of file
            }
            if (bytesRead != sizeof(chunkSize)) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                return false;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                return false;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = sourceFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                return false;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedChunk);
            if (decryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                return false;
            }

            // Write decrypted chunk
            if (targetFile.write(decryptedChunk) != decryptedChunk.size()) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                return false;
            }

            processedFileSize += sizeof(chunkSize) + chunkSize;

            // Update file progress
            int filePercentage = static_cast<int>((processedFileSize * 100) / fileSize);
            emit fileProgressUpdated(filePercentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        sourceFile.close();
        targetFile.flush();
        targetFile.close();

        // Verify file was created successfully
        if (!QFile::exists(fileInfo.targetFile)) {
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception during file decryption:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception during file decryption";
        return false;
    }
}

// =============================================================================
// MAIN IMPLEMENTATION of batch decrypt
// =============================================================================

void Operations_EncryptedData::decryptAndExportAllFiles()
{
    qDebug() << "Starting batch decrypt and export operation";

    // Step 1: Enumerate all encrypted files
    QList<FileExportInfo> allFiles = enumerateAllEncryptedFiles();

    // Step 2: Check if there are any files to decrypt
    if (allFiles.isEmpty()) {
        QMessageBox::information(m_mainWindow, "Database is Empty",
                                 "Database is Empty. There is no file to decrypt and export.");
        return;
    }

    qDebug() << "Found" << allFiles.size() << "files to decrypt";

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

    // Step 3.5: Check if DecryptedData folder already exists
    QString decryptedDataPath = QDir(exportBasePath).absoluteFilePath("DecryptedData");
    if (QDir(decryptedDataPath).exists()) {
        QMessageBox::critical(m_mainWindow, "Folder Already Exists",
                              "A DecryptedData folder already exists at the specified location.\n\n"
                              "Choose a different location or delete the folder.");
        qDebug() << "DecryptedData folder already exists at:" << decryptedDataPath;
        return;
    }

    // Step 4: Calculate total size and create target paths
    qint64 totalSize = 0;
    QString username = m_mainWindow->user_Username;
    decryptedDataPath = QDir(exportBasePath).absoluteFilePath("DecryptedData");

    for (FileExportInfo& fileInfo : allFiles) {
        // Calculate size
        QFileInfo sourceFileInfo(fileInfo.sourceFile);
        fileInfo.fileSize = sourceFileInfo.size();
        totalSize += fileInfo.fileSize;

        // FIXED: Create target path using fileType and original filename
        // Structure: ExportLocation/DecryptedData/FileType/originalFilename
        QString targetPath = QDir(decryptedDataPath).absoluteFilePath(fileInfo.fileType);
        targetPath = QDir(targetPath).absoluteFilePath(fileInfo.originalFilename);

        fileInfo.targetFile = targetPath;

        qDebug() << "Mapped:" << fileInfo.sourceFile << "=>" << fileInfo.targetFile;
        qDebug() << "File type:" << fileInfo.fileType << "Original name:" << fileInfo.originalFilename;
    }

    // Step 5: Show confirmation dialog
    QString sizeString = formatFileSize(totalSize);
    QMessageBox confirmBox(m_mainWindow);
    confirmBox.setWindowTitle("Confirm Batch Export");
    confirmBox.setIcon(QMessageBox::Question);
    confirmBox.setText("You are about to decrypt and export your entire encrypted file database.");
    confirmBox.setInformativeText(QString("%1 file(s) will be decrypted, for a total approximate size of %2.\n\nAre you sure you wish to continue?")
                                      .arg(allFiles.size()).arg(sizeString));

    QPushButton* continueButton = confirmBox.addButton("Continue", QMessageBox::YesRole);
    QPushButton* cancelButton = confirmBox.addButton("Cancel", QMessageBox::NoRole);
    confirmBox.setDefaultButton(cancelButton);

    confirmBox.exec();

    if (confirmBox.clickedButton() != continueButton) {
        qDebug() << "User cancelled batch export operation";
        return;
    }

    // Step 6: Create DecryptedData directory structure
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
    m_batchDecryptWorker = new BatchDecryptionWorker(allFiles, m_mainWindow->user_Key);
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

    // FIXED: Connect cancellation signal properly
    connect(progressDialog, &BatchDecryptionProgressDialog::cancelled,
            this, &Operations_EncryptedData::onBatchDecryptionCancelled);

    // Store progress dialog reference for signal handlers
    m_batchProgressDialog = progressDialog;

    // Start decryption
    m_batchDecryptWorkerThread->start();
    progressDialog->exec();
}

// Helper function to enumerate all encrypted files
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

// Helper function to format file sizes
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

// Progress signal handlers
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
        m_batchProgressDialog = nullptr; // Clear the pointer
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
            message = QString("Export completed successfully!\n\nAll %1 files were decrypted and exported.")
            .arg(successfulFiles.size());
            QMessageBox::information(m_mainWindow, "Export Complete", message);
        } else {
            message = QString("Export completed with some issues.\n\n%1 files succeeded, %2 files failed.\n\nFailed files:\n%3")
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

    if (m_progressDialog) {
        BatchDecryptionProgressDialog* batchDialog =
            qobject_cast<BatchDecryptionProgressDialog*>(m_progressDialog);
        if (batchDialog) {
            batchDialog->setStatusText("Cancelling operation...");
        }
    }
}
