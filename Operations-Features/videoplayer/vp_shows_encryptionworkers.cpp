#include "vp_shows_encryptionworkers.h"
#include "CryptoUtils.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QThread>

//---------------- VP_ShowsEncryptionWorker ----------------//

VP_ShowsEncryptionWorker::VP_ShowsEncryptionWorker(const QStringList& sourceFiles, 
                                                   const QStringList& targetFiles,
                                                   const QString& showName,
                                                   const QByteArray& encryptionKey, 
                                                   const QString& username)
    : m_sourceFiles(sourceFiles)
    , m_targetFiles(targetFiles)
    , m_showName(showName)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(false)
{
    qDebug() << "VP_ShowsEncryptionWorker: Constructor called for" << sourceFiles.size() << "files";
    m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
}

VP_ShowsEncryptionWorker::~VP_ShowsEncryptionWorker()
{
    qDebug() << "VP_ShowsEncryptionWorker: Destructor called";
    delete m_metadataManager;
}

void VP_ShowsEncryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
    qDebug() << "VP_ShowsEncryptionWorker: Cancellation requested";
}

void VP_ShowsEncryptionWorker::doEncryption()
{
    qDebug() << "VP_ShowsEncryptionWorker: Starting encryption of" << m_sourceFiles.size() << "files";
    
    if (m_sourceFiles.isEmpty() || m_targetFiles.isEmpty()) {
        emit encryptionFinished(false, "No files to encrypt", QStringList(), QStringList());
        return;
    }
    
    if (m_sourceFiles.size() != m_targetFiles.size()) {
        emit encryptionFinished(false, "Source and target file lists size mismatch", 
                               QStringList(), QStringList());
        return;
    }
    
    // Calculate total size for progress tracking
    qint64 totalSize = 0;
    for (const QString& sourceFile : m_sourceFiles) {
        QFileInfo fileInfo(sourceFile);
        if (fileInfo.exists()) {
            totalSize += fileInfo.size();
        }
    }
    
    QStringList successfulFiles;
    QStringList failedFiles;
    qint64 totalProcessed = 0;
    
    // Process each file
    for (int i = 0; i < m_sourceFiles.size(); ++i) {
        // Check for cancellation
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_cancelled) {
                qDebug() << "VP_ShowsEncryptionWorker: Encryption cancelled by user";
                emit encryptionFinished(false, "Encryption cancelled by user", 
                                       successfulFiles, failedFiles);
                return;
            }
        }
        
        QString sourceFile = m_sourceFiles[i];
        QString targetFile = m_targetFiles[i];
        
        QFileInfo fileInfo(sourceFile);
        QString originalFilename = fileInfo.fileName();
        
        // Emit file progress update
        emit fileProgressUpdate(i + 1, m_sourceFiles.size(), originalFilename);
        
        // Encrypt the file
        bool success = encryptSingleFile(sourceFile, targetFile, totalProcessed, totalSize);
        
        if (success) {
            successfulFiles.append(sourceFile);
            totalProcessed += fileInfo.size();
        } else {
            failedFiles.append(sourceFile);
        }
        
        // Update overall progress
        if (totalSize > 0) {
            int overallProgress = static_cast<int>((totalProcessed * 100) / totalSize);
            emit progressUpdated(overallProgress);
        }
    }
    
    // Determine overall success
    bool overallSuccess = !successfulFiles.isEmpty();
    QString errorMessage;
    
    if (failedFiles.isEmpty()) {
        errorMessage = QString("Successfully encrypted %1 files").arg(successfulFiles.size());
    } else if (successfulFiles.isEmpty()) {
        errorMessage = QString("Failed to encrypt all %1 files").arg(failedFiles.size());
    } else {
        errorMessage = QString("Encrypted %1 files, failed %2 files")
                      .arg(successfulFiles.size())
                      .arg(failedFiles.size());
    }
    
    qDebug() << "VP_ShowsEncryptionWorker:" << errorMessage;
    emit encryptionFinished(overallSuccess, errorMessage, successfulFiles, failedFiles);
}

bool VP_ShowsEncryptionWorker::encryptSingleFile(const QString& sourceFile, 
                                                 const QString& targetFile,
                                                 qint64 currentTotalProcessed, 
                                                 qint64 totalSize)
{
    qDebug() << "VP_ShowsEncryptionWorker: Encrypting file:" << sourceFile << "to" << targetFile;
    
    QFile source(sourceFile);
    if (!source.open(QIODevice::ReadOnly)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to open source file:" << source.errorString();
        return false;
    }
    
    QFile target(targetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to open target file:" << target.errorString();
        source.close();
        return false;
    }
    
    // Create metadata for this file
    QFileInfo fileInfo(sourceFile);
    VP_ShowsMetadata::ShowMetadata metadata(
        fileInfo.fileName(),  // Original filename
        m_showName,          // Show name from folder
        QString(),           // Season (empty for now)
        QString()            // Episode (empty for now)
    );
    
    // Write metadata header (fixed size)
    if (!m_metadataManager->writeFixedSizeEncryptedMetadata(&target, metadata)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to write metadata";
        source.close();
        target.close();
        target.remove();
        return false;
    }
    
    // Encrypt file content in chunks
    const int chunkSize = 1024 * 1024; // 1MB chunks
    QByteArray buffer;
    qint64 fileProcessed = 0;
    qint64 fileSize = source.size();
    
    while (!source.atEnd()) {
        // Check for cancellation
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_cancelled) {
                source.close();
                target.close();
                target.remove();
                return false;
            }
        }
        
        // Read chunk
        buffer = source.read(chunkSize);
        if (buffer.isEmpty()) {
            break;
        }
        
        // Encrypt chunk
        QByteArray encryptedChunk = CryptoUtils::Encryption_EncryptBArray(m_encryptionKey, buffer, m_username);
        if (encryptedChunk.isEmpty()) {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to encrypt chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Write size of encrypted chunk followed by the chunk
        QDataStream stream(&target);
        stream << qint32(encryptedChunk.size());
        target.write(encryptedChunk);
        
        // Update progress
        fileProcessed += buffer.size();
        
        // Emit current file progress
        if (fileSize > 0) {
            int fileProgress = static_cast<int>((fileProcessed * 100) / fileSize);
            emit currentFileProgressUpdated(fileProgress);
        }
        
        // Also update overall progress if we have total size
        if (totalSize > 0) {
            qint64 totalProcessed = currentTotalProcessed + fileProcessed;
            int overallProgress = static_cast<int>((totalProcessed * 100) / totalSize);
            emit progressUpdated(overallProgress);
        }
        
        // Allow other threads to run
        QThread::msleep(1);
    }
    
    source.close();
    target.close();
    
    qDebug() << "VP_ShowsEncryptionWorker: Successfully encrypted file:" << sourceFile;
    return true;
}

//---------------- VP_ShowsDecryptionWorker ----------------//

VP_ShowsDecryptionWorker::VP_ShowsDecryptionWorker(const QString& sourceFile, 
                                                   const QString& targetFile,
                                                   const QByteArray& encryptionKey,
                                                   const QString& username)
    : m_sourceFile(sourceFile)
    , m_targetFile(targetFile)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(false)
{
    qDebug() << "VP_ShowsDecryptionWorker: Constructor called";
    m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
}

VP_ShowsDecryptionWorker::~VP_ShowsDecryptionWorker()
{
    qDebug() << "VP_ShowsDecryptionWorker: Destructor called";
    delete m_metadataManager;
}

void VP_ShowsDecryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
    qDebug() << "VP_ShowsDecryptionWorker: Cancellation requested";
}

void VP_ShowsDecryptionWorker::doDecryption()
{
    qDebug() << "VP_ShowsDecryptionWorker: Starting decryption of" << m_sourceFile;
    
    QFile source(m_sourceFile);
    if (!source.open(QIODevice::ReadOnly)) {
        emit decryptionFinished(false, "Failed to open source file: " + source.errorString());
        return;
    }
    
    QFile target(m_targetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        source.close();
        emit decryptionFinished(false, "Failed to open target file: " + target.errorString());
        return;
    }
    
    // Read and verify metadata (but don't write it to target)
    VP_ShowsMetadata::ShowMetadata metadata;
    if (!m_metadataManager->readFixedSizeEncryptedMetadata(&source, metadata)) {
        qDebug() << "VP_ShowsDecryptionWorker: Failed to read metadata";
        source.close();
        target.close();
        target.remove();
        emit decryptionFinished(false, "Failed to read file metadata");
        return;
    }
    
    qDebug() << "VP_ShowsDecryptionWorker: Decrypting file:" << metadata.filename 
             << "from show:" << metadata.showName;
    
    // Skip past metadata (already read)
    source.seek(VP_ShowsMetadata::METADATA_RESERVED_SIZE);
    
    // Calculate file size for progress
    qint64 encryptedContentSize = source.size() - VP_ShowsMetadata::METADATA_RESERVED_SIZE;
    qint64 processedSize = 0;
    
    // Decrypt file content in chunks
    QDataStream stream(&source);
    
    while (!source.atEnd()) {
        // Check for cancellation
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_cancelled) {
                source.close();
                target.close();
                target.remove();
                emit decryptionFinished(false, "Decryption cancelled by user");
                return;
            }
        }
        
        // Read chunk size
        qint32 chunkSize;
        stream >> chunkSize;
        
        if (chunkSize <= 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
            qDebug() << "VP_ShowsDecryptionWorker: Invalid chunk size:" << chunkSize;
            source.close();
            target.close();
            target.remove();
            emit decryptionFinished(false, "Invalid encrypted chunk size");
            return;
        }
        
        // Read encrypted chunk
        QByteArray encryptedChunk = source.read(chunkSize);
        if (encryptedChunk.size() != chunkSize) {
            qDebug() << "VP_ShowsDecryptionWorker: Failed to read complete chunk";
            source.close();
            target.close();
            target.remove();
            emit decryptionFinished(false, "Failed to read encrypted chunk");
            return;
        }
        
        // Decrypt chunk
        QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedChunk);
        if (decryptedChunk.isEmpty()) {
            qDebug() << "VP_ShowsDecryptionWorker: Failed to decrypt chunk";
            source.close();
            target.close();
            target.remove();
            emit decryptionFinished(false, "Failed to decrypt chunk");
            return;
        }
        
        // Write decrypted chunk
        target.write(decryptedChunk);
        
        // Update progress
        processedSize += sizeof(qint32) + chunkSize;
        if (encryptedContentSize > 0) {
            int progress = static_cast<int>((processedSize * 100) / encryptedContentSize);
            emit progressUpdated(progress);
        }
        
        // Allow other threads to run
        QThread::msleep(1);
    }
    
    source.close();
    target.close();
    
    qDebug() << "VP_ShowsDecryptionWorker: Successfully decrypted file to" << m_targetFile;
    emit decryptionFinished(true, "Decryption completed successfully");
}
