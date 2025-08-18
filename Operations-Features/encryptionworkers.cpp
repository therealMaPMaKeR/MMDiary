#include "encryptionworkers.h"
#include "../Operations-Global/CryptoUtils.h"
#include "../Operations-Global/operations_files.h"
#include "../constants.h"
#include "../Operations-Global/fileiconprovider.h"
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

        // Define file extensions for thumbnail generation
        QStringList imageExtensions = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "webp"};
        QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "mpg", "mpeg"};

        // Get current datetime for new encryptions
                QDateTime encryptionDateTime = QDateTime::currentDateTime();
                qDebug() << "EncryptionWorker: Setting encryption datetime for new files:" << encryptionDateTime.toString();

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

            // UPDATED: Generate thumbnail during encryption with square padding
            QByteArray thumbnailData;
            QFileInfo sourceInfo(sourceFile);
            QString originalFilename = sourceInfo.fileName();
            QString extension = sourceInfo.suffix().toLower();

            // Generate thumbnail based on file type
            if (imageExtensions.contains(extension)) {
                qDebug() << "EncryptionWorker: Generating square thumbnail for image:" << originalFilename;
                QPixmap thumbnail = EncryptedFileMetadata::createThumbnailFromImage(sourceFile, 64);
                if (!thumbnail.isNull()) {
                    thumbnailData = EncryptedFileMetadata::compressThumbnail(thumbnail, 85);
                    qDebug() << "EncryptionWorker: Generated square image thumbnail, compressed size:" << thumbnailData.size() << "bytes";
                } else {
                    qDebug() << "EncryptionWorker: Failed to generate square image thumbnail for:" << originalFilename;
                }
            } else if (videoExtensions.contains(extension)) {
                qDebug() << "EncryptionWorker: Generating square thumbnail for video:" << originalFilename;
                // Check if we have pre-extracted video thumbnail
                if (m_videoThumbnails.contains(sourceFile)) {
                    QPixmap videoThumbnail = m_videoThumbnails[sourceFile];
                    if (!videoThumbnail.isNull()) {
                        // UPDATED: Create square thumbnail with black padding for video
                        QPixmap squareVideoThumbnail = EncryptedFileMetadata::createSquareThumbnail(videoThumbnail, 64);
                        if (!squareVideoThumbnail.isNull()) {
                            thumbnailData = EncryptedFileMetadata::compressThumbnail(squareVideoThumbnail, 85);
                            qDebug() << "EncryptionWorker: Using pre-extracted video thumbnail with square padding, compressed size:" << thumbnailData.size() << "bytes";
                        } else {
                            qDebug() << "EncryptionWorker: Failed to create square thumbnail for video:" << originalFilename;
                        }
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
                int overallPercentage = static_cast<int>((processedTotalSize * 100) / totalSize);
                emit progressUpdated(overallPercentage);

                // NEW: Update current file progress
                int filePercentage = static_cast<int>((processedFileSize * 100) / currentFileSize);
                emit currentFileProgressUpdated(filePercentage);

                // Allow other threads to run
                QCoreApplication::processEvents();
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

        // Skip the fixed-size encrypted metadata header (40KB)
        QByteArray metadataBlock = sourceFile.read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            emit decryptionFinished(false, "Failed to skip fixed-size metadata header");
            return;
        }

        processedSize += Constants::METADATA_RESERVED_SIZE;

        qDebug() << "DecryptionWorker: Skipped" << Constants::METADATA_RESERVED_SIZE << "bytes of metadata";

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

        qDebug() << "DecryptionWorker: Temp decryption completed successfully:" << m_targetFile;
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

        // Skip the fixed-size encrypted metadata header (40KB)
        QByteArray metadataBlock = sourceFile.read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            emit decryptionFinished(false, "Failed to skip fixed-size metadata header");
            return;
        }

        processedSize += Constants::METADATA_RESERVED_SIZE;

        qDebug() << "TempDecryptionWorker: Skipped" << Constants::METADATA_RESERVED_SIZE << "bytes of metadata";

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

        qDebug() << "TempDecryptionWorker: Temp decryption completed successfully:" << m_targetFile;
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
// BatchDecryptionWorker Implementation
// ============================================================================

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

        qint64 currentTotalProcessed = 0;
        QStringList successfulFiles;
        QStringList failedFiles;

        for (int i = 0; i < m_fileInfos.size(); ++i) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    emit batchDecryptionFinished(false, "Operation was cancelled",
                                                 successfulFiles, failedFiles);
                    return;
                }
            }

            const FileExportInfo& fileInfo = m_fileInfos[i];

            // Update progress
            emit fileStarted(i + 1, m_fileInfos.size(), fileInfo.originalFilename);

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

        if (successfulFiles.size() == m_fileInfos.size()) {
            resultMessage = QString("All %1 files exported successfully").arg(successfulFiles.size());
        } else if (successfulFiles.isEmpty()) {
            resultMessage = "All files failed to export";
            overallSuccess = false;
        } else {
            resultMessage = QString("Partial success: %1 of %2 files exported successfully")
                .arg(successfulFiles.size())
                .arg(m_fileInfos.size());
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
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(fileInfo.targetFile);
                    sourceFile.close();
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

            // Allow other threads to run
            QCoreApplication::processEvents();
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
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

// ============================================================================
// SecureDeletionWorker Implementation
// ============================================================================

SecureDeletionWorker::SecureDeletionWorker(const QList<DeletionItem>& items)
    : m_items(items)
    , m_cancelled(false)
{
}

SecureDeletionWorker::~SecureDeletionWorker()
{
}

void SecureDeletionWorker::doSecureDeletion()
{
    try {
        DeletionResult result;

        // Calculate total number of files for progress tracking
        int totalFiles = 0;
        for (const DeletionItem& item : m_items) {
            if (item.isFolder) {
                QStringList filesInFolder = enumerateFilesInFolder(item.path);
                totalFiles += filesInFolder.size();
            } else {
                totalFiles += 1;
            }
        }

        int processedFiles = 0;

        for (const DeletionItem& item : m_items) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    result.failedItems.append(QString("Cancelled - %1").arg(item.displayName));
                    break;
                }
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
    return OperationsFiles::secureDelete(filePath, 3, true);
}

bool SecureDeletionWorker::secureDeleteFolder(const QString& folderPath, int& processedFiles, int totalFiles)
{
    try {
        QStringList filesInFolder = enumerateFilesInFolder(folderPath);

        // Delete all files in the folder first
        for (const QString& filePath : filesInFolder) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    return false;
                }
            }

            if (!secureDeleteSingleFile(filePath)) {
                qDebug() << "SecureDeletionWorker: Failed to delete file in folder:" << filePath;
                return false;
            }

            processedFiles++;

            // Update progress for individual files
            int percentage = (totalFiles > 0) ? static_cast<int>((processedFiles * 100) / totalFiles) : 100;
            emit progressUpdated(percentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
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
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}
