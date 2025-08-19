#include "vp_shows_encryptionworkers.h"
#include "vp_shows_config.h"
#include "CryptoUtils.h"
#include "operations_files.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <QUuid>
#include <QImage>
#include <QBuffer>

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
    , m_tmdbDataAvailable(false)
{
    qDebug() << "VP_ShowsEncryptionWorker: Constructor called for" << sourceFiles.size() << "files";
    m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
    m_tmdbManager = new VP_ShowsTMDB(this);
    
    // Set TMDB API key from configuration
    if (VP_ShowsConfig::isTMDBEnabled()) {
        QString apiKey = VP_ShowsConfig::getTMDBApiKey();
        if (!apiKey.isEmpty()) {
            m_tmdbManager->setApiKey(apiKey);
            qDebug() << "VP_ShowsEncryptionWorker: TMDB API key configured";
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: No TMDB API key available";
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: TMDB integration disabled";
    }
}

VP_ShowsEncryptionWorker::~VP_ShowsEncryptionWorker()
{
    qDebug() << "VP_ShowsEncryptionWorker: Destructor called";
    
    // Clean up temp directory
    VP_ShowsConfig::cleanupTempDirectory(m_username);
    
    delete m_metadataManager;
    delete m_tmdbManager;
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
    
    // Try to fetch TMDB data for the show
    m_tmdbDataAvailable = fetchTMDBShowData();
    
    // If we have TMDB data, download and encrypt the show image
    if (m_tmdbDataAvailable && !m_targetFiles.isEmpty()) {
        QFileInfo firstTargetInfo(m_targetFiles.first());
        QString targetFolder = firstTargetInfo.absolutePath();
        downloadAndEncryptShowImage(targetFolder);
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
    
    // Create metadata for this file with TMDB data if available
    QFileInfo fileInfo(sourceFile);
    VP_ShowsMetadata::ShowMetadata metadata = createMetadataWithTMDB(fileInfo.fileName());
    
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

bool VP_ShowsEncryptionWorker::fetchTMDBShowData()
{
    if (m_showName.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: Cannot fetch TMDB data without show name";
        return false;
    }
    
    // Search for the show on TMDB
    bool success = m_tmdbManager->searchTVShow(m_showName, m_showInfo);
    
    if (success) {
        qDebug() << "VP_ShowsEncryptionWorker: Found TMDB data for show:" << m_showInfo.showName;
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: No TMDB data found for show:" << m_showName;
    }
    
    return success;
}

bool VP_ShowsEncryptionWorker::downloadAndEncryptShowImage(const QString& targetFolder)
{
    if (!m_tmdbDataAvailable || m_showInfo.posterPath.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: No show poster available";
        return false;
    }
    
    // Create temp file for downloading the image
    QString tempDir = VP_ShowsConfig::getTempDirectory(m_username);
    if (tempDir.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to get temp directory";
        return false;
    }
    
    QString tempImagePath = tempDir + "/temp_show_poster.jpg";
    
    // Download the poster
    if (!m_tmdbManager->downloadImage(m_showInfo.posterPath, tempImagePath, true)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to download show poster";
        return false;
    }
    
    // Read the downloaded image
    QFile tempFile(tempImagePath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to open downloaded poster";
        OperationsFiles::secureDelete(tempImagePath, 3);
        return false;
    }
    
    QByteArray imageData = tempFile.readAll();
    tempFile.close();
    
    // Generate obfuscated filename
    QString obfuscatedName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString encryptedImagePath = targetFolder + "/showimage_" + obfuscatedName + ".enc";
    
    // Encrypt the image data
    QByteArray encryptedImage = CryptoUtils::Encryption_EncryptBArray(m_encryptionKey, imageData, m_username);
    
    if (encryptedImage.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to encrypt show image";
        OperationsFiles::secureDelete(tempImagePath, 3);
        return false;
    }
    
    // Write encrypted image to file
    QFile encryptedFile(encryptedImagePath);
    if (!encryptedFile.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to create encrypted image file";
        OperationsFiles::secureDelete(tempImagePath, 3);
        return false;
    }
    
    encryptedFile.write(encryptedImage);
    encryptedFile.close();
    
    // Securely delete the temp file
    OperationsFiles::secureDelete(tempImagePath, 3);
    
    m_showImagePath = encryptedImagePath;
    qDebug() << "VP_ShowsEncryptionWorker: Successfully encrypted show image to:" << encryptedImagePath;
    
    return true;
}

VP_ShowsMetadata::ShowMetadata VP_ShowsEncryptionWorker::createMetadataWithTMDB(const QString& filename)
{
    VP_ShowsMetadata::ShowMetadata metadata;
    metadata.filename = filename;
    metadata.showName = m_showName;
    
    // Try to parse season and episode from filename
    int season = 0, episode = 0;
    if (VP_ShowsTMDB::parseEpisodeFromFilename(filename, season, episode)) {
        metadata.season = QString::number(season);
        metadata.episode = QString::number(episode);
        
        qDebug() << "VP_ShowsEncryptionWorker: Parsed episode info - S" << season << "E" << episode;
        
        // If we have TMDB data and valid episode info, try to get episode details
        if (m_tmdbDataAvailable && m_showInfo.tmdbId > 0) {
            VP_ShowsTMDB::EpisodeInfo episodeInfo;
            
            // Add small delay to avoid rate limiting (TMDB allows 40 requests/10 seconds)
            QThread::msleep(250); // 250ms delay between API calls
            
            if (m_tmdbManager->getEpisodeInfo(m_showInfo.tmdbId, season, episode, episodeInfo)) {
                metadata.EPName = episodeInfo.episodeName;
                
                // Download and scale episode thumbnail if available
                if (!episodeInfo.stillPath.isEmpty()) {
                    QString tempDir = VP_ShowsConfig::getTempDirectory(m_username);
                    QString tempThumbPath = tempDir + "/temp_episode_thumb.jpg";
                    
                    if (m_tmdbManager->downloadImage(episodeInfo.stillPath, tempThumbPath, false)) {
                        QFile thumbFile(tempThumbPath);
                        if (thumbFile.open(QIODevice::ReadOnly)) {
                            QByteArray thumbData = thumbFile.readAll();
                            thumbFile.close();
                            
                            // Scale to 128x128
                            QByteArray scaledThumb = VP_ShowsTMDB::scaleImageToSize(thumbData, 128, 128);
                            
                            if (!scaledThumb.isEmpty() && scaledThumb.size() <= VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
                                metadata.EPImage = scaledThumb;
                                qDebug() << "VP_ShowsEncryptionWorker: Added episode thumbnail (" 
                                        << scaledThumb.size() << "bytes)";
                            }
                            
                            // Securely delete temp file
                            OperationsFiles::secureDelete(tempThumbPath, 3);
                        }
                    }
                }
                
                qDebug() << "VP_ShowsEncryptionWorker: Added TMDB episode data:" << metadata.EPName;
            }
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: Could not parse episode info from filename:" << filename;
    }
    
    return metadata;
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
