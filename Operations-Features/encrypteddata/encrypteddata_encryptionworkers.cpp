#include "encrypteddata_encryptionworkers.h"
#include "CryptoUtils.h"
#include "operations_files.h"
#include "constants.h"
#include "encrypteddata_fileiconprovider.h"
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
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QDirIterator>
#include <QDateTime>
#include <QPointer>
#include <memory>
#include <QStorageInfo>
#include <QImage>
#include <QPainter>
#include <cstring>  // For std::memset

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ============================================================================
// Helper Functions
// ============================================================================

// Get available system memory in bytes
static qint64 getAvailableSystemMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        return static_cast<qint64>(memStatus.ullAvailPhys);
    }
#elif defined(Q_OS_LINUX)
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&meminfo);
        QString line;
        while (!stream.atEnd()) {
            line = stream.readLine();
            if (line.startsWith("MemAvailable:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    bool ok;
                    qint64 kb = parts[1].toLongLong(&ok);
                    if (ok) {
                        return kb * 1024; // Convert KB to bytes
                    }
                }
                break;
            }
        }
        meminfo.close();
    }
#elif defined(Q_OS_MAC)
    // macOS implementation would go here
    // For now, return a conservative estimate
    return 2LL * 1024 * 1024 * 1024; // 2GB default
#endif
    
    // Fallback: return conservative estimate
    return 2LL * 1024 * 1024 * 1024; // 2GB default
}

// Check if a file can be processed given available memory
static bool canProcessFile(qint64 fileSize, QString& errorMessage)
{
    qint64 availableMemory = getAvailableSystemMemory();
    
    // Calculate the memory limit: 50% of available RAM, min 1GB, max 10GB
    const qint64 MIN_LIMIT = 1LL * 1024 * 1024 * 1024;  // 1GB minimum
    const qint64 MAX_LIMIT = 10LL * 1024 * 1024 * 1024; // 10GB maximum
    qint64 memoryLimit = availableMemory / 2; // Use 50% of available RAM
    
    // Apply min/max constraints
    if (memoryLimit < MIN_LIMIT) {
        memoryLimit = MIN_LIMIT;
    } else if (memoryLimit > MAX_LIMIT) {
        memoryLimit = MAX_LIMIT;
    }
    
    qDebug() << "EncryptionWorker: Available memory:" << (availableMemory / (1024*1024)) << "MB"
             << "Memory limit:" << (memoryLimit / (1024*1024)) << "MB"
             << "File size:" << (fileSize / (1024*1024)) << "MB";
    
    if (fileSize > memoryLimit) {
        // Format sizes for user-friendly message
        auto formatSize = [](qint64 bytes) -> QString {
            const qint64 kb = 1024;
            const qint64 mb = kb * 1024;
            const qint64 gb = mb * 1024;
            
            if (bytes >= gb) {
                return QString("%1 GB").arg(bytes / double(gb), 0, 'f', 2);
            } else if (bytes >= mb) {
                return QString("%1 MB").arg(bytes / double(mb), 0, 'f', 2);
            } else if (bytes >= kb) {
                return QString("%1 KB").arg(bytes / double(kb), 0, 'f', 2);
            } else {
                return QString("%1 bytes").arg(bytes);
            }
        };
        
        errorMessage = QString("File size (%1) exceeds memory limit (%2). "
                             "Available RAM: %3. Please free up memory or process smaller files.")
                      .arg(formatSize(fileSize))
                      .arg(formatSize(memoryLimit))
                      .arg(formatSize(availableMemory));
        return false;
    }
    
    return true;
}

// ============================================================================
// EncryptionWorker Implementation
// ============================================================================

EncryptionWorker::EncryptionWorker(const QStringList& sourceFiles, const QStringList& targetFiles,
                                   const QByteArray& encryptionKey, const QString& username,
                                   const QMap<QString, QPixmap>& videoThumbnails)
    : QObject(nullptr)  // No parent - will be moved to thread
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_metadataManager(std::make_unique<EncryptedFileMetadata>(encryptionKey, username))
{
    // Thread-safe initialization of containers
    QMutexLocker locker(&m_containerMutex);
    m_sourceFiles = sourceFiles;
    m_targetFiles = targetFiles;
    qDebug() << "EncryptionWorker: Constructor - creating worker for" << sourceFiles.size() << "files";
    
    // SECURITY: Convert QPixmap to QImage for thread safety
    // QPixmap can only be used in GUI thread, QImage is thread-safe
    for (auto it = videoThumbnails.constBegin(); it != videoThumbnails.constEnd(); ++it) {
        if (!it.value().isNull()) {
            m_videoThumbnailImages[it.key()] = it.value().toImage();
        }
    }
}

EncryptionWorker::EncryptionWorker(const QString& sourceFile, const QString& targetFile,
                                   const QByteArray& encryptionKey, const QString& username,
                                   const QMap<QString, QPixmap>& videoThumbnails)
    : QObject(nullptr)  // No parent - will be moved to thread
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(0)  // Use consistent initialization: 0 = false, 1 = true for atomic
    , m_metadataManager(std::make_unique<EncryptedFileMetadata>(encryptionKey, username))
{
    // Thread-safe initialization of containers
    QMutexLocker locker(&m_containerMutex);
    m_sourceFiles << sourceFile;
    m_targetFiles << targetFile;
    qDebug() << "EncryptionWorker: Constructor - creating worker for single file";
    
    // SECURITY: Convert QPixmap to QImage for thread safety
    // QPixmap can only be used in GUI thread, QImage is thread-safe
    for (auto it = videoThumbnails.constBegin(); it != videoThumbnails.constEnd(); ++it) {
        if (!it.value().isNull()) {
            m_videoThumbnailImages[it.key()] = it.value().toImage();
        }
    }
}

// Add cleanup in EncryptionWorker destructor (add if not exists):
EncryptionWorker::~EncryptionWorker()
{
    qDebug() << "EncryptionWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
    
    // Clear video thumbnail images
    m_videoThumbnailImages.clear();
    
    // Smart pointer will automatically clean up m_metadataManager
}

void EncryptionWorker::doEncryption()
{
    qDebug() << "EncryptionWorker: doEncryption() started in thread" << QThread::currentThreadId();
    
    // Safety check - ensure we're not in the main thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "EncryptionWorker: CRITICAL ERROR - Running in main thread! Aborting operation.";
        // Abort execution to prevent UI freeze
        if (m_sourceFiles.size() == 1) {
            emit encryptionFinished(false, "Internal error: Worker running in main thread");
        } else {
            emit multiFileEncryptionFinished(false, "Internal error: Worker running in main thread",
                                             QStringList(), QStringList());
        }
        return;
    }
    
    try {
        // Thread-safe access to containers
        QStringList localSourceFiles;
        QStringList localTargetFiles;
        {
            QMutexLocker locker(&m_containerMutex);
            localSourceFiles = m_sourceFiles;
            localTargetFiles = m_targetFiles;
        }
        
        if (localSourceFiles.size() != localTargetFiles.size()) {
            if (localSourceFiles.size() == 1) {
                emit encryptionFinished(false, "Mismatch between source and target file counts");
            } else {
                emit multiFileEncryptionFinished(false, "Mismatch between source and target file counts",
                                                 QStringList(), QStringList());
            }
            return;
        }

        if (localSourceFiles.isEmpty()) {
            emit encryptionFinished(false, "No files to encrypt");
            return;
        }

        // Determine if this is single or multiple file operation
        bool isMultipleFiles = (localSourceFiles.size() > 1);

        // Calculate total size of all files for progress tracking
        qint64 totalSize = 0;
        QList<qint64> fileSizes;

        for (const QString& sourceFile : localSourceFiles) {
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

        // Define file extensions for thumbnail generation
        QStringList imageExtensions = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "webp"};
        QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "mpg", "mpeg"};

        // Get current datetime for new encryptions
                QDateTime encryptionDateTime = QDateTime::currentDateTime();
                qDebug() << "EncryptionWorker: Setting encryption datetime for new files:" << encryptionDateTime.toString();

        // Process each file
        for (int fileIndex = 0; fileIndex < localSourceFiles.size(); ++fileIndex) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                // Clean up any partial files created so far
                for (int i = 0; i < fileIndex; ++i) {
                    if (QFile::exists(localTargetFiles[i])) {
                        QFile::remove(localTargetFiles[i]);
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

            const QString& sourceFile = localSourceFiles[fileIndex];
            const QString& targetFile = localTargetFiles[fileIndex];
            qint64 currentFileSize = fileSizes[fileIndex];
            
            // SECURITY: Check if file can be processed with available memory
            QString memoryErrorMsg;
            if (!canProcessFile(currentFileSize, memoryErrorMsg)) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (%2)").arg(fileName).arg(memoryErrorMsg));
                processedTotalSize += currentFileSize; // Still count it for progress
                qWarning() << "EncryptionWorker: Skipping file due to memory limit:" << fileName;
                continue;
            }

            // Update progress to show which file we're working on (only for multiple files)
            if (isMultipleFiles) {
                emit fileProgressUpdate(fileIndex + 1, localSourceFiles.size(), QFileInfo(sourceFile).fileName());
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

            // UPDATED: Generate thumbnail during encryption with square padding
            QByteArray thumbnailData;
            QFileInfo sourceInfo(sourceFile);
            QString originalFilename = sourceInfo.fileName();
            QString extension = sourceInfo.suffix().toLower();

            // Generate thumbnail based on file type
            if (imageExtensions.contains(extension)) {
                qDebug() << "EncryptionWorker: Generating square thumbnail for image:" << originalFilename;
                // THREAD SAFETY: Load image directly as QImage (thread-safe)
                QImage imageThumb;
                if (imageThumb.load(sourceFile)) {
                    // Scale to 64x64 with aspect ratio preserved
                    imageThumb = imageThumb.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    
                    // Create square thumbnail with padding if needed
                    if (imageThumb.width() != 64 || imageThumb.height() != 64) {
                        QImage squareImage(64, 64, QImage::Format_RGB32);
                        squareImage.fill(Qt::black);
                        QPainter painter(&squareImage);
                        int x = (64 - imageThumb.width()) / 2;
                        int y = (64 - imageThumb.height()) / 2;
                        painter.drawImage(x, y, imageThumb);
                        painter.end();
                        imageThumb = squareImage;
                    }
                    
                    // Convert to QPixmap for compression (safe in worker thread context)
                    QPixmap pixmapForCompression = QPixmap::fromImage(imageThumb);
                    thumbnailData = EncryptedFileMetadata::compressThumbnail(pixmapForCompression, 85);
                    qDebug() << "EncryptionWorker: Generated square image thumbnail, compressed size:" << thumbnailData.size() << "bytes";
                } else {
                    qDebug() << "EncryptionWorker: Failed to load image for thumbnail:" << originalFilename;
                }
            } else if (videoExtensions.contains(extension)) {
                qDebug() << "EncryptionWorker: Generating square thumbnail for video:" << originalFilename;
                // Check if we have pre-extracted video thumbnail
                if (m_videoThumbnailImages.contains(sourceFile)) {
                    QImage videoThumbnail = m_videoThumbnailImages[sourceFile];
                    if (!videoThumbnail.isNull()) {
                        // THREAD SAFETY: Work with QImage instead of QPixmap
                        // Create square thumbnail with black padding for video
                        QImage scaledThumb = videoThumbnail.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        
                        // Add padding if needed
                        if (scaledThumb.width() != 64 || scaledThumb.height() != 64) {
                            QImage squareImage(64, 64, QImage::Format_RGB32);
                            squareImage.fill(Qt::black);
                            QPainter painter(&squareImage);
                            int x = (64 - scaledThumb.width()) / 2;
                            int y = (64 - scaledThumb.height()) / 2;
                            painter.drawImage(x, y, scaledThumb);
                            painter.end();
                            scaledThumb = squareImage;
                        }
                        
                        // Convert to QPixmap for compression
                        QPixmap pixmapForCompression = QPixmap::fromImage(scaledThumb);
                        thumbnailData = EncryptedFileMetadata::compressThumbnail(pixmapForCompression, 85);
                        qDebug() << "EncryptionWorker: Using pre-extracted video thumbnail with square padding, compressed size:" << thumbnailData.size() << "bytes";
                    }
                } else {
                    qDebug() << "EncryptionWorker: No pre-extracted video thumbnail available for:" << originalFilename;
                }
            }

            // Create metadata with filename and thumbnail and encryption datetime
            EncryptedFileMetadata::FileMetadata metadata(originalFilename, "", QStringList(), thumbnailData, encryptionDateTime);

            // Create fixed-size encrypted metadata block (40KB) - this includes the size header internally
            QByteArray fixedSizeMetadata = m_metadataManager->createEncryptedMetadataChunk(metadata);
            if (fixedSizeMetadata.isEmpty()) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (failed to create metadata)").arg(fileName));
                source.close();
                target.close();
                processedTotalSize += currentFileSize;
                continue;
            }

            // Verify fixed-size metadata block
            if (fixedSizeMetadata.size() != Constants::METADATA_RESERVED_SIZE) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (invalid metadata size %2, expected %3)")
                                       .arg(fileName).arg(fixedSizeMetadata.size()).arg(Constants::METADATA_RESERVED_SIZE));
                source.close();
                target.close();
                processedTotalSize += currentFileSize;
                continue;
            }

            qDebug() << "EncryptionWorker: About to write" << fixedSizeMetadata.size() << "bytes of metadata with square thumbnail";

            // Write the complete fixed-size metadata block (no additional headers needed)
            qint64 bytesWritten = target.write(fixedSizeMetadata);
            if (bytesWritten != fixedSizeMetadata.size()) {
                QString fileName = QFileInfo(sourceFile).fileName();
                failedFiles.append(QString("%1 (failed to write metadata, wrote %2 of %3 bytes)")
                                       .arg(fileName).arg(bytesWritten).arg(fixedSizeMetadata.size()));
                source.close();
                target.close();
                processedTotalSize += currentFileSize;
                continue;
            }

            qDebug() << "EncryptionWorker: Successfully wrote" << bytesWritten << "bytes of metadata with square thumbnail";

            // Encrypt and write file content in chunks (UPDATED with file progress)
            const qint64 chunkSize = 1024 * 1024; // 1MB chunks
            QByteArray buffer;
            qint64 processedFileSize = 0;
            bool fileSuccess = true;

            while (!source.atEnd() && fileSuccess) {
                // Check for cancellation
                // THREAD SAFETY: No mutex needed for atomic check
                if (m_cancelled.loadAcquire() != 0) {
                    target.close();
                    source.close();
                    QFile::remove(targetFile); // Clean up partial file

                    // Clean up any other partial files
                    for (int i = 0; i < fileIndex; ++i) {
                        if (QFile::exists(localTargetFiles[i])) {
                            QFile::remove(localTargetFiles[i]);
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
                int overallPercentage = static_cast<int>((processedTotalSize * 100) / totalSize);
                emit progressUpdated(overallPercentage);

                // NEW: Update current file progress
                int filePercentage = static_cast<int>((processedFileSize * 100) / currentFileSize);
                emit currentFileProgressUpdated(filePercentage);

                // Yield to other threads without using processEvents (which is dangerous in worker threads)
                QThread::yieldCurrentThread();
            }

            source.close();
            target.close();

            if (fileSuccess) {
                successfulFiles.append(QFileInfo(sourceFile).fileName());
                qDebug() << "EncryptionWorker: Successfully encrypted file with embedded square thumbnail:" << originalFilename;
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

            if (successfulFiles.size() == localSourceFiles.size()) {
                resultMessage = QString("All %1 files encrypted successfully").arg(successfulFiles.size());
            } else if (successfulFiles.isEmpty()) {
                resultMessage = "All files failed to encrypt:\n" + failedFiles.join("\n");
                overallSuccess = false;
            } else {
                resultMessage = QString("Partial success: %1 of %2 files encrypted successfully\n\nFailed files:\n%3")
                .arg(successfulFiles.size())
                    .arg(localSourceFiles.size())
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
        QMutexLocker locker(&m_containerMutex);
        if (m_sourceFiles.size() > 1) {
            emit multiFileEncryptionFinished(false, errorMsg, QStringList(), QStringList());
        } else {
            emit encryptionFinished(false, errorMsg);
        }
    } catch (...) {
        QString errorMsg = "Unknown encryption error occurred";
        QMutexLocker locker(&m_containerMutex);
        if (m_sourceFiles.size() > 1) {
            emit multiFileEncryptionFinished(false, errorMsg, QStringList(), QStringList());
        } else {
            emit encryptionFinished(false, errorMsg);
        }
    }
}


// Thread-safe getter methods implementation
QStringList EncryptionWorker::getSourceFiles() const
{
    QMutexLocker locker(&m_containerMutex);
    return m_sourceFiles;
}

QStringList EncryptionWorker::getTargetFiles() const
{
    QMutexLocker locker(&m_containerMutex);
    return m_targetFiles;
}

void EncryptionWorker::cancel()
{
    qDebug() << "EncryptionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "EncryptionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "EncryptionWorker: Already cancelled";
    }
}

// ============================================================================
// DecryptionWorker Implementation
// ============================================================================

DecryptionWorker::DecryptionWorker(const QString& sourceFile, const QString& targetFile,
                                   const QByteArray& encryptionKey)
    : QObject(nullptr)  // No parent - will be moved to thread
    , m_encryptionKey(encryptionKey)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_metadataManager(std::make_unique<EncryptedFileMetadata>(encryptionKey, QString()))
{
    // Thread-safe initialization
    QMutexLocker locker(&m_memberMutex);
    m_sourceFile = sourceFile;
    m_targetFile = targetFile;
    qDebug() << "DecryptionWorker: Constructor - creating worker for decryption";
}

DecryptionWorker::~DecryptionWorker()
{
    qDebug() << "DecryptionWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
    
    // Smart pointer will automatically clean up m_metadataManager
}

void DecryptionWorker::doDecryption()
{
    qDebug() << "DecryptionWorker: doDecryption() started in thread" << QThread::currentThreadId();
    
    // Safety check - ensure we're not in the main thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "DecryptionWorker: CRITICAL ERROR - Running in main thread! Aborting operation.";
        // Abort execution to prevent UI freeze
        emit decryptionFinished(false, "Internal error: Worker running in main thread");
        return;
    }
    
    try {
        // Thread-safe access to member variables
        QString localSourceFile;
        QString localTargetFile;
        {
            QMutexLocker locker(&m_memberMutex);
            localSourceFile = m_sourceFile;
            localTargetFile = m_targetFile;
        }
        
        QFile sourceFile(localSourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit decryptionFinished(false, "Failed to open encrypted file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        
        // SECURITY: Check if file can be processed with available memory
        QString memoryErrorMsg;
        if (!canProcessFile(totalSize, memoryErrorMsg)) {
            sourceFile.close();
            emit decryptionFinished(false, memoryErrorMsg);
            qWarning() << "DecryptionWorker: File too large for available memory:" << localSourceFile;
            return;
        }
        qint64 processedSize = 0;

        // Skip the fixed-size encrypted metadata header (40KB)
        QByteArray metadataBlock = sourceFile.read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            emit decryptionFinished(false, "Failed to skip fixed-size metadata header");
            return;
        }

        processedSize += Constants::METADATA_RESERVED_SIZE;

        qDebug() << "DecryptionWorker: Skipped" << Constants::METADATA_RESERVED_SIZE << "bytes of metadata";

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(localTargetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit decryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(localTargetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit decryptionFinished(false, "Failed to create target file");
            return;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                targetFile.close();
                QFile::remove(localTargetFile); // Clean up partial file
                emit decryptionFinished(false, "Operation was cancelled");
                return;
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

            // Yield to other threads without using processEvents (which is dangerous in worker threads)
            QThread::yieldCurrentThread();
        }

        sourceFile.close();

        // Ensure all data is written to disk before closing
        targetFile.flush();
        targetFile.close();

// Set proper file permissions for reading
#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(localTargetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(localTargetFile)) {
            emit decryptionFinished(false, "Target file was not created successfully");
            return;
        }

        qDebug() << "DecryptionWorker: Temp decryption completed successfully:" << localTargetFile;
        emit decryptionFinished(true);

    } catch (const std::exception& e) {
        emit decryptionFinished(false, QString("Decryption error: %1").arg(e.what()));
    } catch (...) {
        emit decryptionFinished(false, "Unknown decryption error occurred");
    }
}

// Thread-safe getter methods implementation
QString DecryptionWorker::getSourceFile() const
{
    QMutexLocker locker(&m_memberMutex);
    return m_sourceFile;
}

QString DecryptionWorker::getTargetFile() const
{
    QMutexLocker locker(&m_memberMutex);
    return m_targetFile;
}

void DecryptionWorker::cancel()
{
    qDebug() << "DecryptionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "DecryptionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "DecryptionWorker: Already cancelled";
    }
}

// ============================================================================
// TempDecryptionWorker Implementation
// ============================================================================

TempDecryptionWorker::TempDecryptionWorker(const QString& sourceFile, const QString& targetFile,
                                           const QByteArray& encryptionKey)
    : QObject(nullptr)  // No parent - will be moved to thread
    , m_encryptionKey(encryptionKey)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_metadataManager(std::make_unique<EncryptedFileMetadata>(encryptionKey, QString()))
{
    // Thread-safe initialization
    QMutexLocker locker(&m_memberMutex);
    m_sourceFile = sourceFile;
    m_targetFile = targetFile;
    qDebug() << "TempDecryptionWorker: Constructor - creating worker for temp decryption";
}

TempDecryptionWorker::~TempDecryptionWorker()
{
    qDebug() << "TempDecryptionWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
    
    // Smart pointer will automatically clean up m_metadataManager
}

void TempDecryptionWorker::doDecryption()
{
    qDebug() << "TempDecryptionWorker: doDecryption() started in thread" << QThread::currentThreadId();
    
    // Safety check - ensure we're not in the main thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "TempDecryptionWorker: CRITICAL ERROR - Running in main thread! Aborting operation.";
        // Abort execution to prevent UI freeze
        emit decryptionFinished(false, "Internal error: Worker running in main thread");
        return;
    }
    
    try {
        // Thread-safe access to member variables
        QString localSourceFile;
        QString localTargetFile;
        {
            QMutexLocker locker(&m_memberMutex);
            localSourceFile = m_sourceFile;
            localTargetFile = m_targetFile;
        }
        
        QFile sourceFile(localSourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit decryptionFinished(false, "Failed to open encrypted file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        
        // SECURITY: Check if file can be processed with available memory
        QString memoryErrorMsg;
        if (!canProcessFile(totalSize, memoryErrorMsg)) {
            sourceFile.close();
            emit decryptionFinished(false, memoryErrorMsg);
            qWarning() << "TempDecryptionWorker: File too large for available memory:" << localSourceFile;
            return;
        }
        qint64 processedSize = 0;

        // Skip the fixed-size encrypted metadata header (40KB)
        QByteArray metadataBlock = sourceFile.read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            emit decryptionFinished(false, "Failed to skip fixed-size metadata header");
            return;
        }

        processedSize += Constants::METADATA_RESERVED_SIZE;

        qDebug() << "TempDecryptionWorker: Skipped" << Constants::METADATA_RESERVED_SIZE << "bytes of metadata";

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(localTargetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit decryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(localTargetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit decryptionFinished(false, "Failed to create target file");
            return;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                targetFile.close();
                QFile::remove(localTargetFile); // Clean up partial file
                emit decryptionFinished(false, "Operation was cancelled");
                return;
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

            // Yield to other threads without using processEvents (which is dangerous in worker threads)
            QThread::yieldCurrentThread();
        }

        sourceFile.close();

        // Ensure all data is written to disk before closing
        targetFile.flush();
        targetFile.close();

// Set proper file permissions for reading
#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(localTargetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(localTargetFile)) {
            emit decryptionFinished(false, "Target file was not created successfully");
            return;
        }

        qDebug() << "TempDecryptionWorker: Temp decryption completed successfully:" << localTargetFile;
        emit decryptionFinished(true);

    } catch (const std::exception& e) {
        emit decryptionFinished(false, QString("Decryption error: %1").arg(e.what()));
    } catch (...) {
        emit decryptionFinished(false, "Unknown decryption error occurred");
    }
}

// Thread-safe getter methods implementation
QString TempDecryptionWorker::getSourceFile() const
{
    QMutexLocker locker(&m_memberMutex);
    return m_sourceFile;
}

QString TempDecryptionWorker::getTargetFile() const
{
    QMutexLocker locker(&m_memberMutex);
    return m_targetFile;
}

void TempDecryptionWorker::cancel()
{
    qDebug() << "TempDecryptionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "TempDecryptionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "TempDecryptionWorker: Already cancelled";
    }
}

// ============================================================================
// BatchDecryptionWorker Implementation
// ============================================================================

BatchDecryptionWorker::BatchDecryptionWorker(const QList<FileExportInfo>& fileInfos,
                                             const QByteArray& encryptionKey)
    : QObject(nullptr)  // No parent - will be moved to thread
    , m_encryptionKey(encryptionKey)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_metadataManager(std::make_unique<EncryptedFileMetadata>(encryptionKey, QString()))
{
    // Thread-safe initialization
    QMutexLocker locker(&m_containerMutex);
    m_fileInfos = fileInfos;
    qDebug() << "BatchDecryptionWorker: Constructor - creating worker for" << fileInfos.size() << "files";
}

BatchDecryptionWorker::~BatchDecryptionWorker()
{
    qDebug() << "BatchDecryptionWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
    
    // Clear file info list (may contain sensitive paths)
    {
        QMutexLocker locker(&m_containerMutex);
        m_fileInfos.clear();
    }
    
    // Smart pointer will automatically clean up m_metadataManager
}

void BatchDecryptionWorker::doDecryption()
{
    qDebug() << "BatchDecryptionWorker: doDecryption() started in thread" << QThread::currentThreadId();
    
    // Safety check - ensure we're not in the main thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "BatchDecryptionWorker: CRITICAL ERROR - Running in main thread! Aborting operation.";
        // Abort execution to prevent UI freeze
        emit batchDecryptionFinished(false, "Internal error: Worker running in main thread", 
                                     QStringList(), QStringList());
        return;
    }
    
    try {
        // Thread-safe access to container
        QList<FileExportInfo> localFileInfos;
        {
            QMutexLocker locker(&m_containerMutex);
            localFileInfos = m_fileInfos;
        }
        
        if (localFileInfos.isEmpty()) {
            emit batchDecryptionFinished(false, "No files to decrypt", QStringList(), QStringList());
            return;
        }

        // Calculate total size for progress tracking
        qint64 totalSize = 0;
        for (const FileExportInfo& info : localFileInfos) {
            totalSize += info.fileSize;
        }

        qint64 currentTotalProcessed = 0;
        QStringList successfulFiles;
        QStringList failedFiles;

        for (int i = 0; i < localFileInfos.size(); ++i) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                emit batchDecryptionFinished(false, "Operation was cancelled",
                                             successfulFiles, failedFiles);
                return;
            }

            const FileExportInfo& fileInfo = localFileInfos[i];

            // Update progress
            emit fileStarted(i + 1, localFileInfos.size(), fileInfo.originalFilename);

            // Decrypt single file
            bool success = decryptSingleFile(fileInfo, currentTotalProcessed, totalSize);

            if (success) {
                successfulFiles.append(fileInfo.originalFilename);
                qDebug() << "BatchDecryptionWorker: Successfully decrypted:" << fileInfo.originalFilename;
            } else {
                failedFiles.append(fileInfo.originalFilename);
                qDebug() << "BatchDecryptionWorker: Failed to decrypt:" << fileInfo.originalFilename;
            }

            currentTotalProcessed += fileInfo.fileSize;

            // Update overall progress
            int overallPercentage = static_cast<int>((currentTotalProcessed * 100) / totalSize);
            emit overallProgressUpdated(overallPercentage);
        }

        // Emit completion
        bool overallSuccess = !successfulFiles.isEmpty();
        QString resultMessage;

        if (successfulFiles.size() == localFileInfos.size()) {
            resultMessage = QString("All %1 files exported successfully").arg(successfulFiles.size());
        } else if (successfulFiles.isEmpty()) {
            resultMessage = "All files failed to export";
            overallSuccess = false;
        } else {
            resultMessage = QString("Partial success: %1 of %2 files exported successfully")
                .arg(successfulFiles.size())
                .arg(localFileInfos.size());
        }

        emit batchDecryptionFinished(overallSuccess, resultMessage, successfulFiles, failedFiles);

    } catch (const std::exception& e) {
        emit batchDecryptionFinished(false, QString("Batch decryption error: %1").arg(e.what()),
                                     QStringList(), QStringList());
    } catch (...) {
        emit batchDecryptionFinished(false, "Unknown batch decryption error occurred",
                                     QStringList(), QStringList());
    }
}

bool BatchDecryptionWorker::decryptSingleFile(const FileExportInfo& fileInfo,
                                               qint64 currentTotalProcessed, qint64 totalSize)
{
    try {
        // SECURITY: Check if file can be processed with available memory
        QString memoryErrorMsg;
        if (!canProcessFile(fileInfo.fileSize, memoryErrorMsg)) {
            qWarning() << "BatchDecryptionWorker: Skipping file due to memory limit:" << fileInfo.originalFilename;
            qDebug() << "BatchDecryptionWorker:" << memoryErrorMsg;
            return false;
        }
        
        QFile sourceFile(fileInfo.sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            qDebug() << "BatchDecryptionWorker: Failed to open encrypted file:" << fileInfo.sourceFile;
            return false;
        }

        // Skip the fixed-size encrypted metadata header (40KB)
        QByteArray metadataBlock = sourceFile.read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            sourceFile.close();
            qDebug() << "BatchDecryptionWorker: Failed to skip metadata header for:" << fileInfo.sourceFile;
            return false;
        }

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(fileInfo.targetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                sourceFile.close();
                qDebug() << "BatchDecryptionWorker: Failed to create target directory for:" << fileInfo.targetFile;
                return false;
            }
        }

        QFile targetFile(fileInfo.targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            sourceFile.close();
            qDebug() << "BatchDecryptionWorker: Failed to create target file:" << fileInfo.targetFile;
            return false;
        }

        // Decrypt file content chunk by chunk
        qint64 processedFileSize = 0;
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                sourceFile.close();
                return false;
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
                sourceFile.close();
                return false;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                sourceFile.close();
                return false;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = sourceFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                sourceFile.close();
                return false;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(
                m_encryptionKey, encryptedChunk);

            if (decryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                sourceFile.close();
                return false;
            }

            // Write decrypted chunk
            if (targetFile.write(decryptedChunk) != decryptedChunk.size()) {
                targetFile.close();
                QFile::remove(fileInfo.targetFile);
                sourceFile.close();
                return false;
            }

            processedFileSize += decryptedChunk.size();

            // Update file progress
            int filePercentage = static_cast<int>((processedFileSize * 100) / fileInfo.fileSize);
            emit fileProgressUpdated(filePercentage);

            // Update overall progress
            qint64 totalProcessed = currentTotalProcessed + processedFileSize;
            int overallPercentage = static_cast<int>((totalProcessed * 100) / totalSize);
            emit overallProgressUpdated(overallPercentage);

            // Yield to other threads without using processEvents (which is dangerous in worker threads)
            QThread::yieldCurrentThread();
        }

        sourceFile.close();
        targetFile.flush();
        targetFile.close();

#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(fileInfo.targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(fileInfo.targetFile)) {
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        qDebug() << "BatchDecryptionWorker: Exception during file decryption:" << e.what();
        return false;
    } catch (...) {
        qDebug() << "BatchDecryptionWorker: Unknown exception during file decryption";
        return false;
    }
}

void BatchDecryptionWorker::cancel()
{
    qDebug() << "BatchDecryptionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "BatchDecryptionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "BatchDecryptionWorker: Already cancelled";
    }
}

// ============================================================================
// SecureDeletionWorker Implementation
// ============================================================================

SecureDeletionWorker::SecureDeletionWorker(const QList<DeletionItem>& items)
    : QObject(nullptr)  // No parent - will be moved to thread
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
{
    // Thread-safe initialization
    QMutexLocker locker(&m_containerMutex);
    m_items = items;
    qDebug() << "SecureDeletionWorker: Constructor - creating worker for" << items.size() << "items";
}

SecureDeletionWorker::~SecureDeletionWorker()
{
    qDebug() << "SecureDeletionWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // Clear items list (may contain sensitive paths)
    {
        QMutexLocker locker(&m_containerMutex);
        m_items.clear();
    }
}

void SecureDeletionWorker::doSecureDeletion()
{
    qDebug() << "SecureDeletionWorker: doSecureDeletion() started in thread" << QThread::currentThreadId();
    
    // Safety check - ensure we're not in the main thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "SecureDeletionWorker: CRITICAL ERROR - Running in main thread! Aborting operation.";
        // Abort execution to prevent UI freeze
        DeletionResult emptyResult;
        emit deletionFinished(false, emptyResult, "Internal error: Worker running in main thread");
        return;
    }
    
    try {
        DeletionResult result;

        // Thread-safe access to container
        QList<DeletionItem> localItems;
        {
            QMutexLocker locker(&m_containerMutex);
            localItems = m_items;
        }
        
        // Calculate total number of files for progress tracking
        int totalFiles = 0;
        for (const DeletionItem& item : localItems) {
            if (item.isFolder) {
                QStringList filesInFolder = enumerateFilesInFolder(item.path);
                totalFiles += filesInFolder.size();
            } else {
                totalFiles += 1;
            }
        }

        int processedFiles = 0;

        for (const DeletionItem& item : localItems) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                result.failedItems.append(QString("Cancelled - %1").arg(item.displayName));
                break;
            }

            emit currentItemChanged(item.displayName);

            bool success = false;
            if (item.isFolder) {
                success = secureDeleteFolder(item.path, processedFiles, totalFiles);
            } else {
                success = secureDeleteSingleFile(item.path);
                processedFiles++;
            }

            if (success) {
                result.successfulItems.append(item.displayName);
                result.totalSize += item.size;
                if (item.isFolder) {
                    QStringList filesInFolder = enumerateFilesInFolder(item.path);
                    result.totalFiles += filesInFolder.size();
                } else {
                    result.totalFiles += 1;
                }
                qDebug() << "SecureDeletionWorker: Successfully deleted:" << item.displayName;
            } else {
                result.failedItems.append(item.displayName);
                qDebug() << "SecureDeletionWorker: Failed to delete:" << item.displayName;
            }

            // Update progress
            int percentage = (totalFiles > 0) ? static_cast<int>((processedFiles * 100) / totalFiles) : 100;
            emit progressUpdated(percentage);
        }

        bool overallSuccess = !result.successfulItems.isEmpty() && result.failedItems.isEmpty();
        QString errorMessage;
        if (!result.failedItems.isEmpty()) {
            errorMessage = QString("Failed to delete: %1").arg(result.failedItems.join(", "));
        }

        emit deletionFinished(overallSuccess, result, errorMessage);

    } catch (const std::exception& e) {
        DeletionResult result;
        emit deletionFinished(false, result, QString("Secure deletion error: %1").arg(e.what()));
    } catch (...) {
        DeletionResult result;
        emit deletionFinished(false, result, "Unknown secure deletion error occurred");
    }
}

bool SecureDeletionWorker::secureDeleteSingleFile(const QString& filePath)
{
    return QFile::remove(filePath);
}

bool SecureDeletionWorker::secureDeleteFolder(const QString& folderPath, int& processedFiles, int totalFiles)
{
    try {
        QStringList filesInFolder = enumerateFilesInFolder(folderPath);

        // Delete all files in the folder first
        for (const QString& filePath : filesInFolder) {
            // Check for cancellation
            // THREAD SAFETY: No mutex needed for atomic check
            if (m_cancelled.loadAcquire() != 0) {
                return false;
            }

            if (!secureDeleteSingleFile(filePath)) {
                qDebug() << "SecureDeletionWorker: Failed to delete file in folder:" << filePath;
                return false;
            }

            processedFiles++;

            // Update progress for individual files
            int percentage = (totalFiles > 0) ? static_cast<int>((processedFiles * 100) / totalFiles) : 100;
            emit progressUpdated(percentage);

            // Yield to other threads without using processEvents (which is dangerous in worker threads)
            QThread::yieldCurrentThread();
        }

        // Now delete the empty folder
        QDir dir(folderPath);
        if (!dir.removeRecursively()) {
            qDebug() << "SecureDeletionWorker: Failed to remove folder:" << folderPath;
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        qDebug() << "SecureDeletionWorker: Exception during folder deletion:" << e.what();
        return false;
    } catch (...) {
        qDebug() << "SecureDeletionWorker: Unknown exception during folder deletion";
        return false;
    }
}

QStringList SecureDeletionWorker::enumerateFilesInFolder(const QString& folderPath)
{
    QStringList files;
    QDirIterator iterator(folderPath, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        files.append(iterator.next());
    }

    return files;
}

void SecureDeletionWorker::cancel()
{
    qDebug() << "SecureDeletionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "SecureDeletionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "SecureDeletionWorker: Already cancelled";
    }
}
