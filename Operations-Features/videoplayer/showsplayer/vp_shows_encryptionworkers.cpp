#include "vp_shows_encryptionworkers.h"
#include "qapplication.h"
#include "vp_shows_config.h"
#include "CryptoUtils.h"
#include "operations_files.h"
#include "inputvalidation.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <QUuid>
#include <QImage>
#include <QBuffer>
#include <QSet>
#include <QReadLocker>
#include <QWriteLocker>
#include <memory>
#include <cstring>  // For std::memset

// RAII wrapper for file operations
class FileGuard {
public:
    FileGuard(QFile* file) : m_file(file) {}
    ~FileGuard() {
        if (m_file && m_file->isOpen()) {
            m_file->close();
        }
    }
private:
    QFile* m_file;
};

//---------------- VP_ShowsEncryptionWorker ----------------//

VP_ShowsEncryptionWorker::VP_ShowsEncryptionWorker(const QStringList& sourceFiles, 
                                                   const QStringList& targetFiles,
                                                   const QString& showName,
                                                   const QByteArray& encryptionKey, 
                                                   const QString& username,
                                                   const QString& language,
                                                   const QString& translation,
                                                   bool useTMDB,
                                                   const QPixmap& customPoster,
                                                   const QString& customDescription,
                                                   ParseMode parseMode,
                                                   int showId)
    : m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_useTMDB(useTMDB)
    , m_customPoster(customPoster)
    , m_customDescription(customDescription)
    , m_parseMode(parseMode)
    , m_showId(showId)
    , m_tmdbDataAvailable(false)
    , m_metadataManager(nullptr)
    , m_tmdbManager(nullptr)
{
    // Thread-safe initialization of member variables
    QMutexLocker pointerLock(&m_pointerMutex);
    m_sourceFiles = sourceFiles;
    m_targetFiles = targetFiles;
    m_showName = showName;
    m_language = language;
    m_translation = translation;
    pointerLock.unlock();
    
    qDebug() << "VP_ShowsEncryptionWorker: Constructor called for" << sourceFiles.size() << "files";
    qDebug() << "VP_ShowsEncryptionWorker: Using TMDB:" << useTMDB;
    qDebug() << "VP_ShowsEncryptionWorker: Has custom poster:" << !customPoster.isNull();
    qDebug() << "VP_ShowsEncryptionWorker: Has custom description:" << !customDescription.isEmpty();
    qDebug() << "VP_ShowsEncryptionWorker: Parse mode:" << (parseMode == ParseFromFolder ? "Folder" : "File");
    qDebug() << "VP_ShowsEncryptionWorker: Show ID:" << m_showId;
    
    // Initialize pointers with thread safety
    {
        QMutexLocker pointerLock2(&m_pointerMutex);
        m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
        m_tmdbManager = new VP_ShowsTMDB(this);
    }
    
    // Only set TMDB API key if we're using TMDB
    if (useTMDB && VP_ShowsConfig::isTMDBEnabled()) {
        QString apiKey = VP_ShowsConfig::getTMDBApiKey();
        if (!apiKey.isEmpty()) {
            m_tmdbManager->setApiKey(apiKey);
            qDebug() << "VP_ShowsEncryptionWorker: TMDB API key configured";
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: No TMDB API key available";
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: TMDB integration disabled or not using TMDB";
    }
}

VP_ShowsEncryptionWorker::~VP_ShowsEncryptionWorker()
{
    qDebug() << "VP_ShowsEncryptionWorker: Destructor called";
    
    // Cancel any ongoing operation
    cancel();
    
    // Clean up temp directory
    VP_ShowsConfig::cleanupTempDirectory(m_username);
    
    // Thread-safe cleanup of pointers
    QMutexLocker pointerLock(&m_pointerMutex);
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
    if (m_tmdbManager) {
        // Safely disconnect signals without thread check to avoid deadlock
        m_tmdbManager->disconnect();
        delete m_tmdbManager;
        m_tmdbManager = nullptr;
    }
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
}

// Thread-safe getter methods implementation
QStringList VP_ShowsEncryptionWorker::getSourceFiles() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_sourceFiles;
}

QStringList VP_ShowsEncryptionWorker::getTargetFiles() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_targetFiles;
}

QString VP_ShowsEncryptionWorker::getShowName() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_showName;
}

QString VP_ShowsEncryptionWorker::getLanguage() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_language;
}

QString VP_ShowsEncryptionWorker::getTranslation() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_translation;
}

void VP_ShowsEncryptionWorker::cancel()
{
    qDebug() << "VP_ShowsEncryptionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "VP_ShowsEncryptionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: Already cancelled";
    }
}

void VP_ShowsEncryptionWorker::doEncryption()
{
    // CRITICAL: Thread affinity check - abort if not in worker thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "VP_ShowsEncryptionWorker: FATAL - doEncryption called from main thread! Aborting.";
        emit encryptionFinished(false, "Internal error: Worker executed in wrong thread", QStringList(), m_sourceFiles);
        return;
    }
    // Thread-safe access to member variables
    QStringList localSourceFiles;
    QStringList localTargetFiles;
    QString localShowName;
    QString localLanguage;
    QString localTranslation;
    {
        QMutexLocker locker(&m_pointerMutex);
        localSourceFiles = m_sourceFiles;
        localTargetFiles = m_targetFiles;
        localShowName = m_showName;
        localLanguage = m_language;
        localTranslation = m_translation;
    }
    
    qDebug() << "VP_ShowsEncryptionWorker: Starting encryption of" << localSourceFiles.size() << "files";
    
    if (localSourceFiles.isEmpty() || localTargetFiles.isEmpty()) {
        emit encryptionFinished(false, "No files to encrypt", QStringList(), QStringList());
        return;
    }
    
    if (localSourceFiles.size() != localTargetFiles.size()) {
        emit encryptionFinished(false, "Source and target file lists size mismatch", 
                               QStringList(), QStringList());
        return;
    }
    
    // Load existing episodes from the target folder to detect duplicates
    loadExistingEpisodes();
    
    // Get the target folder from the first target file
    QString targetFolder;
    if (!localTargetFiles.isEmpty()) {
        QFileInfo firstTargetInfo(localTargetFiles.first());
        targetFolder = firstTargetInfo.absolutePath();
    }
    
    // Handle TMDB or custom data
    if (m_useTMDB) {
        // Try to fetch TMDB data for the show
        m_tmdbDataAvailable = fetchTMDBShowData();
        
        // If we have TMDB data, download and encrypt the show image
        if (m_tmdbDataAvailable && !targetFolder.isEmpty()) {
            downloadAndEncryptShowImage(targetFolder);
        }
    } else {
        // Save custom poster and description if provided
        qDebug() << "VP_ShowsEncryptionWorker: TMDB not used, checking for custom data...";
        qDebug() << "VP_ShowsEncryptionWorker: Target folder empty:" << targetFolder.isEmpty();
        qDebug() << "VP_ShowsEncryptionWorker: Custom poster null:" << m_customPoster.isNull();
        qDebug() << "VP_ShowsEncryptionWorker: Custom description empty:" << m_customDescription.isEmpty();
        
        if (!targetFolder.isEmpty() && (!m_customPoster.isNull() || !m_customDescription.isEmpty())) {
            qDebug() << "VP_ShowsEncryptionWorker: Calling saveCustomShowData...";
            bool saved = saveCustomShowData(targetFolder);
            qDebug() << "VP_ShowsEncryptionWorker: saveCustomShowData returned:" << saved;
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: No custom data to save";
        }
        m_tmdbDataAvailable = false;  // Ensure TMDB data is not used
    }
    
    // Calculate total size for progress tracking
    qint64 totalSize = 0;
    for (const QString& sourceFile : localSourceFiles) {
        QFileInfo fileInfo(sourceFile);
        if (fileInfo.exists()) {
            totalSize += fileInfo.size();
        }
    }
    
    QStringList successfulFiles;
    QStringList failedFiles;
    qint64 totalProcessed = 0;
    
    // Process each file
    for (int i = 0; i < localSourceFiles.size(); ++i) {
        // Check for cancellation using atomic operation
        if (m_cancelled.loadAcquire() != 0) {
            qDebug() << "VP_ShowsEncryptionWorker: Encryption cancelled by user";
            emit encryptionFinished(false, "Encryption cancelled by user", 
                                   successfulFiles, failedFiles);
            return;
        }
        
        QString sourceFile = localSourceFiles[i];
        QString targetFile = localTargetFiles[i];
        
        QFileInfo fileInfo(sourceFile);
        QString originalFilename = fileInfo.fileName();
        
        // Emit file progress update
        emit fileProgressUpdate(i + 1, localSourceFiles.size(), originalFilename);
        
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
    FileGuard sourceGuard(&source);  // RAII - ensures file closes on any exit path
    
    QFile target(targetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to open target file:" << target.errorString();
        return false;
    }
    FileGuard targetGuard(&target);  // RAII - ensures file closes on any exit path
    
    // Create metadata for this file with TMDB data if available
    QFileInfo fileInfo(sourceFile);
    QString folderName = fileInfo.dir().dirName();  // Get immediate parent folder name
    VP_ShowsMetadata::ShowMetadata metadata = createMetadataWithTMDB(fileInfo.fileName(), folderName);
    
    // Write metadata header (fixed size) - check pointer validity
    {
        QMutexLocker pointerLock(&m_pointerMutex);
        if (!m_metadataManager) {
            qDebug() << "VP_ShowsEncryptionWorker: Metadata manager is null";
            target.remove();
            return false;
        }
        if (!m_metadataManager->writeFixedSizeEncryptedMetadata(&target, metadata)) {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to write metadata";
            target.remove();
            return false;
        }
    }
    
    // Encrypt file content in chunks
    const int chunkSize = 1024 * 1024; // 1MB chunks
    QByteArray buffer;
    qint64 fileProcessed = 0;
    qint64 fileSize = source.size();
    
    while (!source.atEnd()) {
        // Check for cancellation using atomic operation
        if (m_cancelled.loadAcquire() != 0) {
            source.close();
            target.close();
            target.remove();
            return false;
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
        qint64 bytesWritten = target.write(encryptedChunk);
        
        if (bytesWritten != encryptedChunk.size()) {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to write complete encrypted chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
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
    
    // Ensure all data is written to disk before closing
    target.flush();
    target.close();
    
#ifdef Q_OS_WIN
    // On Windows, ensure proper file permissions
    QFile::setPermissions(targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif
    
    qDebug() << "VP_ShowsEncryptionWorker: Successfully encrypted file:" << sourceFile;
    return true;
}

bool VP_ShowsEncryptionWorker::fetchTMDBShowData()
{
    if (m_showName.isEmpty() && m_showId <= 0) {
        qDebug() << "VP_ShowsEncryptionWorker: Cannot fetch TMDB data without show name or ID";
        return false;
    }
    
    // Get show info with thread-safe pointer access
    bool success = false;
    {
        QMutexLocker pointerLock(&m_pointerMutex);
        if (!m_tmdbManager) {
            qDebug() << "VP_ShowsEncryptionWorker: TMDB manager is null";
            return false;
        }
        
        // Get show info into a temporary variable first
        VP_ShowsTMDB::ShowInfo tempShowInfo;
        
        // If we have a show ID, use it directly (more accurate)
        if (m_showId > 0) {
            qDebug() << "VP_ShowsEncryptionWorker: Fetching TMDB data using show ID:" << m_showId;
            success = m_tmdbManager->getShowById(m_showId, tempShowInfo);
            if (!success) {
                qDebug() << "VP_ShowsEncryptionWorker: Failed to get show by ID, falling back to search by name";
                success = m_tmdbManager->searchTVShow(m_showName, tempShowInfo);
            }
        } else {
            // Fall back to searching by name if no ID is available
            qDebug() << "VP_ShowsEncryptionWorker: No show ID available, searching by name:" << m_showName;
            success = m_tmdbManager->searchTVShow(m_showName, tempShowInfo);
        }
        
        if (success) {
            // Write lock for updating shared data
            QWriteLocker dataLock(&m_dataLock);
            m_showInfo = tempShowInfo;
        }
    }
    
    if (success) {
        // Read lock to access show info safely
        QReadLocker dataLock(&m_dataLock);
        qDebug() << "VP_ShowsEncryptionWorker: Found TMDB data for show:" << m_showInfo.showName;
        int tmdbId = m_showInfo.tmdbId;
        dataLock.unlock();
        
        // Build the episode map for absolute numbering support
        if (tmdbId > 0) {
            qDebug() << "VP_ShowsEncryptionWorker: Building episode map for absolute numbering...";
            
            QMutexLocker pointerLock(&m_pointerMutex);
            if (!m_tmdbManager) {
                qDebug() << "VP_ShowsEncryptionWorker: TMDB manager became null";
                return false;
            }
            
            auto tempEpisodeMap = m_tmdbManager->buildEpisodeMap(tmdbId);
            auto tempMovieTitles = m_tmdbManager->getShowMovieTitles(tmdbId);
            pointerLock.unlock();
            
            // Write lock to update shared data
            QWriteLocker writeDataLock(&m_dataLock);
            m_episodeMap = tempEpisodeMap;
            m_movieTitles = tempMovieTitles;
            writeDataLock.unlock();
            
            qDebug() << "VP_ShowsEncryptionWorker: Episode map built with" << tempEpisodeMap.size() << "episodes";
            if (!tempMovieTitles.isEmpty()) {
                qDebug() << "VP_ShowsEncryptionWorker: Found" << tempMovieTitles.size() << "related movies";
            }
            
            // Fetch OVA/special titles from Season 0 that are likely actual OVAs
            // Only include titles with OVA/OAD keywords to avoid false matches
            QMutexLocker pointerLock2(&m_pointerMutex);  // Different name to avoid redefinition
            if (m_tmdbManager) {
                QList<VP_ShowsTMDB::EpisodeInfo> specials = m_tmdbManager->getShowSpecials(tmdbId);
                pointerLock2.unlock();
                
                QStringList tempOvaTitles;
                for (const auto& special : specials) {
                    if (!special.episodeName.isEmpty()) {
                        QString lowerName = special.episodeName.toLower();
                        // Only add if it has OVA/OAD indicators or is clearly a special
                        if (lowerName.contains("ova") || lowerName.contains("oad") || 
                            lowerName.contains("special") || lowerName.contains("short") ||
                            lowerName.contains("bonus") || lowerName.contains("extra")) {
                            tempOvaTitles.append(special.episodeName);
                            qDebug() << "VP_ShowsEncryptionWorker: Added OVA/Special for matching:" << special.episodeName;
                        }
                    }
                }
                
                // Update shared data with write lock
                if (!tempOvaTitles.isEmpty()) {
                    QWriteLocker writeDataLock(&m_dataLock);
                    m_ovaTitles.append(tempOvaTitles);
                    writeDataLock.unlock();
                    qDebug() << "VP_ShowsEncryptionWorker: Found" << tempOvaTitles.size() << "special/OVA titles for matching";
                }
            }
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: No TMDB data found for show:" << m_showName;
    }
    
    return success;
}

bool VP_ShowsEncryptionWorker::downloadAndEncryptShowImage(const QString& targetFolder)
{
    if (!m_tmdbDataAvailable) {
        qDebug() << "VP_ShowsEncryptionWorker: No TMDB data available";
        return false;
    }
    
    // Get show info safely
    VP_ShowsTMDB::ShowInfo safeShowInfo;
    {
        QReadLocker dataLock(&m_dataLock);
        safeShowInfo = m_showInfo;
    }
    
    // Save show description if available
    if (!safeShowInfo.overview.isEmpty()) {
        // Generate the obfuscated folder name
        QDir showDir(targetFolder);
        QString obfuscatedName = showDir.dirName();
        
        // Create the filename with prefix showdesc_
        QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
        QString descFilePath = showDir.absoluteFilePath(descFileName);
        
        // Encrypt and save the description
        bool descSaved = OperationsFiles::writeEncryptedFile(descFilePath, m_encryptionKey, safeShowInfo.overview);
        
        if (descSaved) {
            qDebug() << "VP_ShowsEncryptionWorker: Successfully saved show description";
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to save show description";
        }
    }
    
    // Save show image if available
    if (safeShowInfo.posterPath.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: No show poster available";
        return true; // Not an error, description might have been saved
    }
    
    // Create temp file for downloading the image
    QString tempDir = VP_ShowsConfig::getTempDirectory(m_username);
    if (tempDir.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: Failed to get temp directory";
        return false;
    }
    
    // Use consistent naming pattern for cleanup
    QString tempImagePath = tempDir + "/temp_show_poster_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
    
    // Download the poster with thread-safe pointer access
    bool downloadSuccess = false;
    {
        QMutexLocker pointerLock(&m_pointerMutex);
        if (m_tmdbManager) {
            downloadSuccess = m_tmdbManager->downloadImage(safeShowInfo.posterPath, tempImagePath, true);
        }
    }
    
    if (!downloadSuccess) {
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
    
    // Generate the obfuscated folder name for the image
    QDir showDir(targetFolder);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showimage_
    QString encryptedImagePath = targetFolder + "/showimage_" + obfuscatedName;
    
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
    
    // Securely delete the temp file (set allowExternalFiles to true for temp files)
    OperationsFiles::secureDelete(tempImagePath, 3, true);
    
    // Thread-safe update of show image path
    {
        QWriteLocker dataLock(&m_dataLock);
        m_showImagePath = encryptedImagePath;
    }
    qDebug() << "VP_ShowsEncryptionWorker: Successfully encrypted show image to:" << encryptedImagePath;
    
    return true;
}

VP_ShowsMetadata::ShowMetadata VP_ShowsEncryptionWorker::createMetadataWithTMDB(const QString& filename, const QString& folderName)
{
    VP_ShowsMetadata::ShowMetadata metadata;
    metadata.filename = filename;
    
    // Thread-safe access to member variables
    {
        QMutexLocker locker(&m_pointerMutex);
        metadata.showName = m_showName;
        metadata.language = m_language;
        metadata.translation = m_translation;
    }
    
    // Initialize content type to Regular by default
    metadata.contentType = VP_ShowsMetadata::Regular;
    qDebug() << "VP_ShowsEncryptionWorker: Starting content type detection for:" << filename;
    qDebug() << "VP_ShowsEncryptionWorker: Using parse mode:" << (m_parseMode == ParseFromFolder ? "Folder" : "File");
    if (m_parseMode == ParseFromFolder) {
        qDebug() << "VP_ShowsEncryptionWorker: Folder name for parsing:" << folderName;
    }
    
    // Try to parse season and episode
    int season = 0, episode = 0;
    bool parsedSuccessfully = false;
    bool hasContentOverrideFromFolder = false;  // Track if we have a content type override from folder
    
    // Check if this is a single-season show (only if we have TMDB data)
    bool isSingleSeason = false;
    if (m_tmdbDataAvailable && m_showInfo.tmdbId > 0) {
        isSingleSeason = VP_ShowsTMDB::hasSingleSeason(m_showInfo);
        qDebug() << "VP_ShowsEncryptionWorker: Show is single-season:" << isSingleSeason;
    }
    
    // Use appropriate parsing method based on parse mode and season count
    if (m_parseMode == ParseFromFolder && !folderName.isEmpty()) {
        // Parse season from folder, episode from filename
        // Also get content type override from folder keywords
        int contentTypeOverride = 0;
        bool folderParseResult = VP_ShowsTMDB::parseSeasonFromFolderName(folderName, filename, season, episode,
                                                                         contentTypeOverride, hasContentOverrideFromFolder);
        if (folderParseResult) {
            // Check if we actually parsed episode numbers or just got content type override
            if (episode > 0) {
                parsedSuccessfully = true;
                qDebug() << "VP_ShowsEncryptionWorker: Folder parse succeeded - S" << season << "E" << episode;
            } else if (hasContentOverrideFromFolder) {
                // We didn't parse episode numbers but we have content type override
                parsedSuccessfully = false;  // No episode numbers
                qDebug() << "VP_ShowsEncryptionWorker: No episode numbers parsed, but have content type override from folder";
            }
            
            if (hasContentOverrideFromFolder) {
                // Override content type based on folder keywords
                metadata.contentType = static_cast<VP_ShowsMetadata::ContentType>(contentTypeOverride);
                qDebug() << "VP_ShowsEncryptionWorker: Overriding content type from folder to:" 
                         << metadata.getContentTypeString();
            }
        } else {
            parsedSuccessfully = false;
        }
    } else if (isSingleSeason) {
        // For single-season shows, only parse episode number from filename
        parsedSuccessfully = VP_ShowsTMDB::parseEpisodeForSingleSeasonShow(filename, episode);
        if (parsedSuccessfully) {
            season = 1;  // Set season to 1 for single-season shows
            qDebug() << "VP_ShowsEncryptionWorker: Single-season parse succeeded - Episode:" << episode;
        }
    } else {
        // For multi-season shows (or when TMDB data unavailable), use standard parsing from filename
        parsedSuccessfully = VP_ShowsTMDB::parseEpisodeFromFilename(filename, season, episode);
        if (parsedSuccessfully) {
            qDebug() << "VP_ShowsEncryptionWorker: Filename parse succeeded - S" << season << "E" << episode;
        }
    }
    
    qDebug() << "VP_ShowsEncryptionWorker: Parse result - Success:" << parsedSuccessfully 
             << "Season:" << season << "Episode:" << episode;
    
    // Determine content type based on parsed results
    // Only determine content type if we don't have a folder override
    if (!hasContentOverrideFromFolder) {
        if (parsedSuccessfully && episode > 0) {
            if (season > 0) {
                // Valid season and episode - this is ALWAYS a regular episode
                // No need to check for OVA/Movie patterns
                metadata.contentType = VP_ShowsMetadata::Regular;
                qDebug() << "VP_ShowsEncryptionWorker: Valid S" << season << "E" << episode 
                         << "found - setting as Regular episode (skipping OVA/Movie detection)";
            } else if (season == 0) {
                // Season 0 means absolute numbering in our parser, NOT Season 0 (specials)
                // Default to Regular, will be overridden if TMDB maps it to Season 0
                metadata.contentType = VP_ShowsMetadata::Regular;
                qDebug() << "VP_ShowsEncryptionWorker: Absolute numbering (season=0) detected - defaulting to Regular";
                qDebug() << "VP_ShowsEncryptionWorker: Will check TMDB mapping to determine actual content type";
            }
        } else {
            // Only detect content type for files without valid episode numbers
            // Need thread-safe access to movie/OVA titles
            QStringList safeMovieTitles;
            QStringList safeOvaTitles;
            {
                QReadLocker dataLock(&m_dataLock);
                safeMovieTitles = m_movieTitles;
                safeOvaTitles = m_ovaTitles;
            }
            metadata.contentType = VP_ShowsMetadata::detectContentType(filename, safeMovieTitles, safeOvaTitles);
            qDebug() << "VP_ShowsEncryptionWorker: No valid episode numbers - auto-detected content type:" 
                     << metadata.contentType << "(" << metadata.getContentTypeString() << ")";
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: Content type override from folder is active - keeping:" 
                 << metadata.getContentTypeString();
    }
    
    // Check if this is a movie that should also appear in regular episodes (dual display)
    // This is for movies that are part of the series continuity
    // BUT don't apply this if we already determined it's a Regular episode from S##E## parsing
    // Also don't apply if we have a folder-based content type override
    if (!hasContentOverrideFromFolder && metadata.contentType == VP_ShowsMetadata::Movie && 
        parsedSuccessfully && episode > 0 && season <= 0) {
        metadata.isDualDisplay = true;
        qDebug() << "VP_ShowsEncryptionWorker: Movie with episode numbering detected - will display in both categories";
    }
    
    if (parsedSuccessfully && episode > 0) {  // Note: season can be 0 for absolute numbering
        qDebug() << "VP_ShowsEncryptionWorker: Parsed episode from filename:" << filename
                 << "-> Season:" << season << "Episode:" << episode;
        
        // Check if this episode is a duplicate
        if (checkForDuplicateEpisode(season, episode, metadata.language, metadata.translation)) {
            qDebug() << "VP_ShowsEncryptionWorker: Duplicate episode detected - S" << season << "E" << episode
                     << "for" << metadata.language << metadata.translation;
            qDebug() << "VP_ShowsEncryptionWorker: Marking as error:" << filename;
            
            // Mark this episode as an error
            metadata.season = "error";
            metadata.episode = "error";
            // Don't fetch TMDB data for error episodes
            return metadata;
        }
        
        // Not a duplicate, add to processed set
        QString episodeKey;
        if (season == 0) {
            QReadLocker dataLock(&m_dataLock);
            if (m_episodeMap.contains(episode)) {
                // For absolute numbering, use the actual season/episode from the map
                const VP_ShowsTMDB::EpisodeMapping& mapping = m_episodeMap[episode];
                episodeKey = QString("S%1E%2_%3_%4")
                    .arg(mapping.season, 2, 10, QChar('0'))
                    .arg(mapping.episode, 2, 10, QChar('0'))
                    .arg(metadata.language)
                    .arg(metadata.translation);
            
            // Check if the mapped season determines content type
            if (mapping.season == 0) {
                // TMDB maps this to Season 0 (specials) - mark as Extra
                metadata.contentType = VP_ShowsMetadata::Extra;
                qDebug() << "VP_ShowsEncryptionWorker: Episode mapped to Season 0 (specials) - marking as Extra content";
            } else if (mapping.season > 0) {
                // TMDB maps this to a regular season - ensure it's marked as Regular
                metadata.contentType = VP_ShowsMetadata::Regular;
                qDebug() << "VP_ShowsEncryptionWorker: Absolute episode" << episode 
                         << "mapped to S" << mapping.season << "E" << mapping.episode 
                         << "- confirming as Regular episode";
            }
            } else {
                dataLock.unlock();
                episodeKey = QString("S%1E%2_%3_%4")
                    .arg(season, 2, 10, QChar('0'))
                    .arg(episode, 2, 10, QChar('0'))
                    .arg(metadata.language)
                    .arg(metadata.translation);
            }
        } else {
            episodeKey = QString("S%1E%2_%3_%4")
                .arg(season, 2, 10, QChar('0'))
                .arg(episode, 2, 10, QChar('0'))
                .arg(metadata.language)
                .arg(metadata.translation);
        }
        
        // Thread-safe insertion into processed episodes set
        QMutexLocker episodesLock(&m_episodesMutex);
        m_processedEpisodes.insert(episodeKey);
        episodesLock.unlock();
        
        qDebug() << "VP_ShowsEncryptionWorker: Final content type before saving:" 
                 << metadata.contentType << "(" << metadata.getContentTypeString() << ")";
        
        // If we couldn't parse a season number, use absolute numbering
        if (season == 0) {
            // No season detected, use absolute numbering
            metadata.season = "0";  // Use "0" as marker for absolute numbering
            metadata.episode = QString::number(episode);
            qDebug() << "VP_ShowsEncryptionWorker: Using absolute numbering for episode" << episode;
        } else {
            // Season detected, use traditional numbering
            metadata.season = QString::number(season);
            metadata.episode = QString::number(episode);
        }
        
        qDebug() << "VP_ShowsEncryptionWorker: Parsed episode info - S" << season << "E" << episode;
        
        // If we have TMDB data and valid episode info, try to get episode details
        if (m_tmdbDataAvailable && m_showInfo.tmdbId > 0) {
            VP_ShowsTMDB::EpisodeInfo episodeInfo;
            
            // Add small delay to avoid rate limiting (TMDB allows 40 requests/10 seconds)
            QThread::msleep(250); // 250ms delay between API calls
            
            // For absolute numbering, we need to try to map to season/episode for TMDB
            int tmdbSeason = season;
            int tmdbEpisode = episode;
            
            if (season == 0 && episode > 0) {
                // This is absolute numbering - use our episode map to convert
                QReadLocker dataLock(&m_dataLock);
                if (m_episodeMap.contains(episode)) {
                    // We have a mapping for this episode!
                    const VP_ShowsTMDB::EpisodeMapping& mapping = m_episodeMap[episode];
                    tmdbSeason = mapping.season;
                    tmdbEpisode = mapping.episode;
                    
                    qDebug() << "VP_ShowsEncryptionWorker: Using episode map - absolute episode" << episode 
                             << "-> S" << tmdbSeason << "E" << tmdbEpisode;
                    
                    // Use episode name and airDate from the map if available
                    if (!mapping.episodeName.isEmpty() && metadata.EPName.isEmpty()) {
                        metadata.EPName = mapping.episodeName;
                        qDebug() << "VP_ShowsEncryptionWorker: Got episode name from map:" << metadata.EPName;
                    }
                    if (!mapping.airDate.isEmpty() && metadata.airDate.isEmpty()) {
                        metadata.airDate = mapping.airDate;
                        qDebug() << "VP_ShowsEncryptionWorker: Got air date from map:" << metadata.airDate;
                    }
                } else {
                    int mapSize = m_episodeMap.size();
                    dataLock.unlock();
                    // No mapping found - use a fallback calculation
                    qDebug() << "VP_ShowsEncryptionWorker: No mapping for absolute episode" << episode 
                             << "in map of" << mapSize << "episodes";
                    
                    // Fallback: estimate based on common patterns
                    const int episodesPerSeason = 26;
                    tmdbSeason = ((episode - 1) / episodesPerSeason) + 1;
                    tmdbEpisode = ((episode - 1) % episodesPerSeason) + 1;
                    
                    qDebug() << "VP_ShowsEncryptionWorker: Fallback conversion - episode" << episode 
                             << "-> S" << tmdbSeason << "E" << tmdbEpisode;
                }
            }
            
            // Only fetch episode info if we're missing critical data
            // If we already have both name and airDate from the map, skip the API call
            bool needToFetchEpisodeInfo = metadata.EPName.isEmpty() || metadata.airDate.isEmpty();
            
            // Get TMDB ID safely
            int tmdbId = 0;
            {
                QReadLocker dataLock(&m_dataLock);
                tmdbId = m_showInfo.tmdbId;
            }
            
            bool episodeInfoFetched = false;
            if (needToFetchEpisodeInfo) {
                QMutexLocker pointerLock(&m_pointerMutex);
                if (m_tmdbManager) {
                    episodeInfoFetched = m_tmdbManager->getEpisodeInfo(tmdbId, tmdbSeason, tmdbEpisode, episodeInfo);
                } else {
                    qDebug() << "VP_ShowsEncryptionWorker: TMDB manager is null during episode info fetch";
                }
            }
            
            if (episodeInfoFetched) {
                metadata.EPName = episodeInfo.episodeName;
                metadata.EPDescription = episodeInfo.overview;  // Store the episode description from TMDB
                metadata.airDate = episodeInfo.airDate;  // Store the air date from TMDB
                
                // Download and scale episode thumbnail if available
                if (!episodeInfo.stillPath.isEmpty()) {
                    QString tempDir = VP_ShowsConfig::getTempDirectory(m_username);
                    // Use consistent naming pattern with unique identifier for cleanup
                    QString tempThumbPath = tempDir + "/temp_episode_thumb_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
                    
                    // Thread-safe download
                    bool thumbDownloaded = false;
                    {
                        QMutexLocker pointerLock3(&m_pointerMutex);  // Different name to avoid conflicts
                        if (m_tmdbManager) {
                            thumbDownloaded = m_tmdbManager->downloadImage(episodeInfo.stillPath, tempThumbPath, false);
                        }
                    }
                    
                    if (thumbDownloaded) {
                        QFile thumbFile(tempThumbPath);
                        if (thumbFile.open(QIODevice::ReadOnly)) {
                            FileGuard thumbGuard(&thumbFile);  // RAII
                            QByteArray thumbData = thumbFile.readAll();
                            
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
                
                qDebug() << "VP_ShowsEncryptionWorker: Added TMDB episode data:" << metadata.EPName 
                         << "Air date:" << metadata.airDate;
            } else if (needToFetchEpisodeInfo) {
                qDebug() << "VP_ShowsEncryptionWorker: Failed to get TMDB episode info for S" << tmdbSeason << "E" << tmdbEpisode;
                qDebug() << "VP_ShowsEncryptionWorker: TMDB ID:" << tmdbId << "Original absolute episode:" << episode;
            }
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: TMDB data not available or invalid show ID";
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: Could not parse episode info from filename:" << filename;
        
        // If we have a folder-based content type override, use it
        // Otherwise, if we couldn't parse valid episode numbers and it's not Movie or OVA, mark it as Extra
        if (hasContentOverrideFromFolder) {
            qDebug() << "VP_ShowsEncryptionWorker: Using folder-based content type override:" << metadata.getContentTypeString();
            metadata.season = "0";
            metadata.episode = "0";
        } else if (metadata.contentType != VP_ShowsMetadata::Movie && 
                   metadata.contentType != VP_ShowsMetadata::OVA) {
            qDebug() << "VP_ShowsEncryptionWorker: No valid episode number found, marking as Extra content";
            metadata.contentType = VP_ShowsMetadata::Extra;
            metadata.season = "0";
            metadata.episode = "0";
        }
    }
    
    return metadata;
}

bool VP_ShowsEncryptionWorker::saveCustomShowData(const QString& targetFolder)
{
    qDebug() << "VP_ShowsEncryptionWorker: Saving custom show data to:" << targetFolder;
    qDebug() << "VP_ShowsEncryptionWorker: Has custom poster:" << !m_customPoster.isNull() 
             << "Size:" << (m_customPoster.isNull() ? QSize() : m_customPoster.size());
    qDebug() << "VP_ShowsEncryptionWorker: Has custom description:" << !m_customDescription.isEmpty()
             << "Length:" << m_customDescription.length();
    
    // Get the obfuscated folder name
    QDir showDir(targetFolder);
    QString obfuscatedName = showDir.dirName();
    
    // Save custom description if available
    if (!m_customDescription.isEmpty()) {
        QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
        QString descFilePath = showDir.absoluteFilePath(descFileName);
        
        qDebug() << "VP_ShowsEncryptionWorker: Saving description to:" << descFilePath;
        
        // Encrypt and save the description
        bool descSaved = OperationsFiles::writeEncryptedFile(descFilePath, m_encryptionKey, m_customDescription);
        
        if (descSaved) {
            qDebug() << "VP_ShowsEncryptionWorker: Successfully saved custom show description";
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to save custom show description";
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: No custom description to save (is empty)";
    }
    
    // Save custom poster if available
    if (!m_customPoster.isNull()) {
        qDebug() << "VP_ShowsEncryptionWorker: Converting custom poster to byte array...";
        // Convert pixmap to byte array
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        bool savedToBuffer = m_customPoster.save(&buffer, "PNG");
        buffer.close();
        
        qDebug() << "VP_ShowsEncryptionWorker: Pixmap saved to buffer:" << savedToBuffer 
                 << "Size:" << imageData.size() << "bytes";
        
        // Create the filename with prefix showimage_
        QString encryptedImagePath = targetFolder + "/showimage_" + obfuscatedName;
        qDebug() << "VP_ShowsEncryptionWorker: Target image path:" << encryptedImagePath;
        
        // Encrypt the image data
        QByteArray encryptedImage = CryptoUtils::Encryption_EncryptBArray(m_encryptionKey, imageData, m_username);
        
        if (!encryptedImage.isEmpty()) {
            // Write encrypted image to file
            QFile encryptedFile(encryptedImagePath);
            if (encryptedFile.open(QIODevice::WriteOnly)) {
                encryptedFile.write(encryptedImage);
                encryptedFile.close();
                
                m_showImagePath = encryptedImagePath;
                qDebug() << "VP_ShowsEncryptionWorker: Successfully saved custom show poster to:" << encryptedImagePath;
            } else {
                qDebug() << "VP_ShowsEncryptionWorker: Failed to create encrypted poster file";
                return false;
            }
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to encrypt custom poster";
            return false;
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: No custom poster to save (is null)";
    }
    
    return true;
}

bool VP_ShowsEncryptionWorker::checkForDuplicateEpisode(int season, int episode, const QString& language, const QString& translation)
{
    QString episodeKey;
    
    // For absolute numbering (season == 0), we need to check against the actual season/episode mapping
    if (season == 0) {
        QReadLocker dataLock(&m_dataLock);
        if (m_episodeMap.contains(episode)) {
            // Use the actual season/episode from the map for the key
            const VP_ShowsTMDB::EpisodeMapping& mapping = m_episodeMap[episode];
            episodeKey = QString("S%1E%2_%3_%4")
                .arg(mapping.season, 2, 10, QChar('0'))
                .arg(mapping.episode, 2, 10, QChar('0'))
                .arg(language)
                .arg(translation);
        } else {
            dataLock.unlock();
            // Create the episode key with language and translation
            episodeKey = QString("S%1E%2_%3_%4")
                .arg(season, 2, 10, QChar('0'))
                .arg(episode, 2, 10, QChar('0'))
                .arg(language)
                .arg(translation);
        }
    } else {
        // Create the episode key with language and translation
        episodeKey = QString("S%1E%2_%3_%4")
            .arg(season, 2, 10, QChar('0'))
            .arg(episode, 2, 10, QChar('0'))
            .arg(language)
            .arg(translation);
    }
    
    // Check if this episode already exists in the target folder or was already processed in this batch
    QMutexLocker episodesLock(&m_episodesMutex);
    return m_existingEpisodes.contains(episodeKey) || m_processedEpisodes.contains(episodeKey);
}

void VP_ShowsEncryptionWorker::loadExistingEpisodes()
{
    qDebug() << "VP_ShowsEncryptionWorker: Loading existing episodes to detect duplicates";
    
    QMutexLocker episodesLock(&m_episodesMutex);
    m_existingEpisodes.clear();
    m_processedEpisodes.clear();
    episodesLock.unlock();
    
    // Get the target folder from the first target file
    if (m_targetFiles.isEmpty()) {
        return;
    }
    
    QFileInfo firstTargetInfo(m_targetFiles.first());
    QString targetFolder = firstTargetInfo.absolutePath();
    
    qDebug() << "VP_ShowsEncryptionWorker: Checking for existing episodes in:" << targetFolder;
    
    // List all existing video files in the target folder
    QDir targetDir(targetFolder);
    if (!targetDir.exists()) {
        qDebug() << "VP_ShowsEncryptionWorker: Target folder doesn't exist yet";
        return;
    }
    
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
    targetDir.setNameFilters(videoExtensions);
    QStringList existingFiles = targetDir.entryList(QDir::Files);
    
    qDebug() << "VP_ShowsEncryptionWorker: Found" << existingFiles.size() << "existing video files";
    
    // Read metadata from each existing file to get season/episode info
    for (const QString& existingFile : existingFiles) {
        QString filePath = targetDir.absoluteFilePath(existingFile);
        VP_ShowsMetadata::ShowMetadata existingMetadata;
        
        // Thread-safe metadata manager access
        QMutexLocker pointerLock(&m_pointerMutex);
        if (!m_metadataManager) {
            qDebug() << "VP_ShowsEncryptionWorker: Metadata manager is null during load";
            return;
        }
        bool readSuccess = m_metadataManager->readMetadataFromFile(filePath, existingMetadata);
        pointerLock.unlock();
        
        if (readSuccess) {
            // Skip files marked as errors
            if (existingMetadata.season == "error" || existingMetadata.episode == "error") {
                qDebug() << "VP_ShowsEncryptionWorker: Skipping error episode:" << existingFile;
                continue;
            }
            
            int seasonNum = existingMetadata.season.toInt();
            int episodeNum = existingMetadata.episode.toInt();
            
            // If metadata doesn't have valid numbers, try parsing from filename
            if (seasonNum == 0 || episodeNum == 0) {
                VP_ShowsTMDB::parseEpisodeFromFilename(existingMetadata.filename, seasonNum, episodeNum);
            }
            
            if (seasonNum > 0 && episodeNum > 0) {
                // Create episode key with language and translation
                QString episodeKey = QString("S%1E%2_%3_%4")
                    .arg(seasonNum, 2, 10, QChar('0'))
                    .arg(episodeNum, 2, 10, QChar('0'))
                    .arg(existingMetadata.language)
                    .arg(existingMetadata.translation);
                    
                // Thread-safe insertion
                QMutexLocker episodesLock(&m_episodesMutex);
                m_existingEpisodes.insert(episodeKey);
                episodesLock.unlock();
                qDebug() << "VP_ShowsEncryptionWorker: Found existing episode:" << episodeKey;
            }
        }
    }
    
    qDebug() << "VP_ShowsEncryptionWorker: Loaded" << m_existingEpisodes.size() << "existing episode identifiers";
}

//---------------- VP_ShowsDecryptionWorker ----------------//

VP_ShowsDecryptionWorker::VP_ShowsDecryptionWorker(const QString& sourceFile, 
                                                   const QString& targetFile,
                                                   const QByteArray& encryptionKey,
                                                   const QString& username)
    : m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_metadataManager(nullptr)
{
    qDebug() << "VP_ShowsDecryptionWorker: Constructor called";
    QMutexLocker pointerLock(&m_pointerMutex);
    m_sourceFile = sourceFile;
    m_targetFile = targetFile;
    m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
}

VP_ShowsDecryptionWorker::~VP_ShowsDecryptionWorker()
{
    qDebug() << "VP_ShowsDecryptionWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // Thread-safe cleanup of pointers
    QMutexLocker pointerLock(&m_pointerMutex);
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
}

// Thread-safe getter methods implementation
QString VP_ShowsDecryptionWorker::getSourceFile() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_sourceFile;
}

QString VP_ShowsDecryptionWorker::getTargetFile() const
{
    QMutexLocker locker(&m_pointerMutex);
    return m_targetFile;
}

void VP_ShowsDecryptionWorker::cancel()
{
    qDebug() << "VP_ShowsDecryptionWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "VP_ShowsDecryptionWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "VP_ShowsDecryptionWorker: Already cancelled";
    }
}

void VP_ShowsDecryptionWorker::doDecryption()
{
    // CRITICAL: Thread affinity check - abort if not in worker thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "VP_ShowsDecryptionWorker: FATAL - doDecryption called from main thread! Aborting.";
        emit decryptionFinished(false, "Internal error: Worker executed in wrong thread");
        return;
    }
    // Thread-safe access to member variables
    QString localSourceFile;
    QString localTargetFile;
    {
        QMutexLocker locker(&m_pointerMutex);
        localSourceFile = m_sourceFile;
        localTargetFile = m_targetFile;
    }
    
    qDebug() << "VP_ShowsDecryptionWorker: Starting decryption of" << localSourceFile;
    
    QFile source(localSourceFile);
    if (!source.open(QIODevice::ReadOnly)) {
        emit decryptionFinished(false, "Failed to open source file: " + source.errorString());
        return;
    }
    FileGuard sourceGuard(&source);  // RAII - ensures file closes on any exit path
    
    QFile target(localTargetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        emit decryptionFinished(false, "Failed to open target file: " + target.errorString());
        return;
    }
    FileGuard targetGuard(&target);  // RAII - ensures file closes on any exit path
    
    // Read and verify metadata (but don't write it to target)
    VP_ShowsMetadata::ShowMetadata metadata;
    {
        QMutexLocker pointerLock(&m_pointerMutex);
        if (!m_metadataManager) {
            qDebug() << "VP_ShowsDecryptionWorker: Metadata manager is null";
            target.remove();
            emit decryptionFinished(false, "Metadata manager unavailable");
            return;
        }
        if (!m_metadataManager->readFixedSizeEncryptedMetadata(&source, metadata)) {
            qDebug() << "VP_ShowsDecryptionWorker: Failed to read metadata";
            target.remove();
            emit decryptionFinished(false, "Failed to read file metadata");
            return;
        }
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
        // Check for cancellation using atomic operation
        if (m_cancelled.loadAcquire() != 0) {
            source.close();
            target.close();
            target.remove();
            emit decryptionFinished(false, "Decryption cancelled by user");
            return;
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
    
    // Ensure all data is written to disk before closing
    target.flush();
    target.close();
    
#ifdef Q_OS_WIN
    // On Windows, ensure the file is not marked as read-only
    QFile::setPermissions(m_targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif
    
    qDebug() << "VP_ShowsDecryptionWorker: Successfully decrypted file to" << m_targetFile;
    emit decryptionFinished(true, "Decryption completed successfully");
}

//---------------- VP_ShowsExportWorker ----------------//

VP_ShowsExportWorker::VP_ShowsExportWorker(const QList<ExportFileInfo>& files,
                                           const QByteArray& encryptionKey,
                                           const QString& username)
    : m_files(files)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(0)  // 0 = false, 1 = true for atomic
    , m_metadataManager(nullptr)
{
    qDebug() << "VP_ShowsExportWorker: Constructor called for" << files.size() << "files";
    QMutexLocker pointerLock(&m_pointerMutex);
    m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
}

VP_ShowsExportWorker::~VP_ShowsExportWorker()
{
    qDebug() << "VP_ShowsExportWorker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // Thread-safe cleanup of pointers
    QMutexLocker pointerLock(&m_pointerMutex);
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
    
    // SECURITY: Clear sensitive data
    if (!m_encryptionKey.isEmpty()) {
        volatile char* keyData = const_cast<volatile char*>(m_encryptionKey.data());
        std::memset(const_cast<char*>(keyData), 0, m_encryptionKey.size());
        m_encryptionKey.clear();
    }
}

void VP_ShowsExportWorker::cancel()
{
    qDebug() << "VP_ShowsExportWorker: Cancellation requested from thread" << QThread::currentThreadId();
    // THREAD SAFETY: Use only atomic operation, no mutex needed
    int oldValue = m_cancelled.fetchAndStoreOrdered(1);
    if (oldValue == 0) {
        qDebug() << "VP_ShowsExportWorker: Cancellation flag set successfully";
    } else {
        qDebug() << "VP_ShowsExportWorker: Already cancelled";
    }
}

void VP_ShowsExportWorker::doExport()
{
    // CRITICAL: Thread affinity check - abort if not in worker thread
    if (QThread::currentThread() == QApplication::instance()->thread()) {
        qCritical() << "VP_ShowsExportWorker: FATAL - doExport called from main thread! Aborting.";
        QStringList allFiles;
        for (const auto& fileInfo : m_files) {
            allFiles.append(fileInfo.sourceFile);
        }
        emit exportFinished(false, "Internal error: Worker executed in wrong thread", QStringList(), allFiles);
        return;
    }
    qDebug() << "VP_ShowsExportWorker: Starting export of" << m_files.size() << "files";
    
    if (m_files.isEmpty()) {
        emit exportFinished(false, "No files to export", QStringList(), QStringList());
        return;
    }
    
    // Calculate total size for overall progress
    qint64 totalSize = 0;
    for (const ExportFileInfo& fileInfo : m_files) {
        totalSize += fileInfo.fileSize;
    }
    
    QStringList successfulFiles;
    QStringList skippedFiles;  // Track files skipped due to duplicates
    QStringList failedFiles;   // Track files that failed for other reasons
    qint64 totalProcessed = 0;
    
    // Process each file
    for (int i = 0; i < m_files.size(); ++i) {
        // Check for cancellation using atomic operation
        if (m_cancelled.loadAcquire() != 0) {
            qDebug() << "VP_ShowsExportWorker: Export cancelled by user";
            emit exportFinished(false, "Export cancelled by user", 
                               successfulFiles, failedFiles);
            return;
        }
        
        const ExportFileInfo& fileInfo = m_files[i];
        
        // Emit file progress update
        emit fileProgressUpdate(i + 1, m_files.size(), fileInfo.displayName);
        
            // Check if file already exists BEFORE attempting export
        // This prevents overwriting existing files in the target folder
        QFileInfo targetInfo(fileInfo.targetFile);
        if (targetInfo.exists()) {
            // File already exists, skip it to avoid overwriting
            skippedFiles.append(fileInfo.sourceFile);
            qDebug() << "VP_ShowsExportWorker: Skipping duplicate file:" << fileInfo.displayName;
            
            // Emit a warning about the skipped file so user knows why it wasn't exported
            emit fileExportWarning(fileInfo.displayName, 
                QString("Skipped - file already exists in target folder"));
            
            // Still update overall progress to reflect that we processed this file (by skipping it)
            totalProcessed += fileInfo.fileSize;
            if (totalSize > 0) {
                int overallProgress = static_cast<int>((totalProcessed * 100) / totalSize);
                emit overallProgressUpdated(overallProgress);
            }
            continue;  // Move to next file
        }
        
        // Export the file
        int currentFileProgress = 0;
        bool success = exportSingleFile(fileInfo, currentFileProgress);
        
        if (success) {
            successfulFiles.append(fileInfo.targetFile);
            totalProcessed += fileInfo.fileSize;
        } else {
            // Check again if it's because file exists (in case it was created between our check and export attempt)
            if (QFileInfo(fileInfo.targetFile).exists()) {
                skippedFiles.append(fileInfo.sourceFile);
            } else {
                failedFiles.append(fileInfo.sourceFile);
            }
        }
        
        // Update overall progress
        if (totalSize > 0) {
            int overallProgress = static_cast<int>((totalProcessed * 100) / totalSize);
            emit overallProgressUpdated(overallProgress);
        }
    }
    
    // Determine overall success and create appropriate message
    bool overallSuccess = false;
    QString errorMessage;
    
    // Check if all files were skipped
    if (successfulFiles.isEmpty() && failedFiles.isEmpty() && !skippedFiles.isEmpty()) {
        errorMessage = QString("All %1 files already exist in the target folder - no files were exported")
                      .arg(skippedFiles.size());
        overallSuccess = false;
    }
    // Check if no files could be processed at all
    else if (successfulFiles.isEmpty() && !failedFiles.isEmpty()) {
        errorMessage = QString("Failed to export all %1 files").arg(failedFiles.size());
        overallSuccess = false;
    }
    // Some files were successful
    else if (!successfulFiles.isEmpty()) {
        overallSuccess = true;
        
        if (skippedFiles.isEmpty() && failedFiles.isEmpty()) {
            errorMessage = QString("Successfully exported %1 files").arg(successfulFiles.size());
        } else {
            QStringList messageParts;
            messageParts.append(QString("Exported %1 files").arg(successfulFiles.size()));
            
            if (!skippedFiles.isEmpty()) {
                messageParts.append(QString("%1 files skipped (already exist)").arg(skippedFiles.size()));
            }
            
            if (!failedFiles.isEmpty()) {
                messageParts.append(QString("%1 files failed").arg(failedFiles.size()));
            }
            
            errorMessage = messageParts.join(", ");
        }
    }
    // Nothing succeeded but we have a mix of skipped and failed
    else {
        errorMessage = QString("No files exported - %1 skipped (duplicates), %2 failed")
                      .arg(skippedFiles.size())
                      .arg(failedFiles.size());
        overallSuccess = false;
    }
    
    qDebug() << "VP_ShowsExportWorker:" << errorMessage;
    qDebug() << "VP_ShowsExportWorker: Successful:" << successfulFiles.size() 
             << "Skipped:" << skippedFiles.size() 
             << "Failed:" << failedFiles.size();
    
    emit exportFinished(overallSuccess, errorMessage, successfulFiles, failedFiles);
}

bool VP_ShowsExportWorker::exportSingleFile(const ExportFileInfo& fileInfo, int& currentFileProgress)
{
    qDebug() << "VP_ShowsExportWorker: Exporting" << fileInfo.sourceFile << "to" << fileInfo.targetFile;
    
    // Validate the target file path using InputValidation
    InputValidation::ValidationResult targetValidation = 
        InputValidation::validateInput(fileInfo.targetFile, InputValidation::InputType::ExternalFilePath);
    
    if (!targetValidation.isValid) {
        qDebug() << "VP_ShowsExportWorker: Invalid target file path:" << targetValidation.errorMessage;
        return false;
    }
    
    // Ensure the parent directory exists using operations_files functions
    QFileInfo finalTargetInfo(fileInfo.targetFile);
    QString targetDir = finalTargetInfo.absolutePath();
    
    if (!QDir(targetDir).exists()) {
        qDebug() << "VP_ShowsExportWorker: Target directory doesn't exist, creating:" << targetDir;
        if (!QDir().mkpath(targetDir)) {
            qDebug() << "VP_ShowsExportWorker: Failed to create target directory:" << targetDir;
            return false;
        }
    }
    
    QFile source(fileInfo.sourceFile);
    if (!source.open(QIODevice::ReadOnly)) {
        qDebug() << "VP_ShowsExportWorker: Failed to open source file:" << source.errorString();
        return false;
    }
    FileGuard sourceGuard(&source);  // RAII - ensures file closes on any exit path
    
    QFile target(fileInfo.targetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsExportWorker: Failed to open target file:" << target.errorString();
        return false;
    }
    FileGuard targetGuard(&target);  // RAII - ensures file closes on any exit path
    
    // Read and verify metadata (but don't write it to target)
    VP_ShowsMetadata::ShowMetadata metadata;
    {
        QMutexLocker pointerLock(&m_pointerMutex);
        if (!m_metadataManager) {
            qDebug() << "VP_ShowsExportWorker: Metadata manager is null";
            target.remove();
            return false;
        }
        if (!m_metadataManager->readFixedSizeEncryptedMetadata(&source, metadata)) {
            qDebug() << "VP_ShowsExportWorker: Failed to read metadata";
            target.remove();
            return false;
        }
    }
    
    qDebug() << "VP_ShowsExportWorker: Exporting episode:" << metadata.EPName 
             << "from show:" << metadata.showName;
    
    // Skip past metadata (already read)
    source.seek(VP_ShowsMetadata::METADATA_RESERVED_SIZE);
    
    // Calculate file size for progress
    qint64 encryptedContentSize = source.size() - VP_ShowsMetadata::METADATA_RESERVED_SIZE;
    qint64 processedSize = 0;
    
    // Decrypt file content in chunks
    QDataStream stream(&source);
    
    while (!source.atEnd()) {
        // Check for cancellation using atomic operation
        if (m_cancelled.loadAcquire() != 0) {
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Read chunk size
        qint32 chunkSize;
        stream >> chunkSize;
        
        if (chunkSize <= 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
            qDebug() << "VP_ShowsExportWorker: Invalid chunk size:" << chunkSize;
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Read encrypted chunk
        QByteArray encryptedChunk = source.read(chunkSize);
        if (encryptedChunk.size() != chunkSize) {
            qDebug() << "VP_ShowsExportWorker: Failed to read complete chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Decrypt chunk
        QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedChunk);
        if (decryptedChunk.isEmpty()) {
            qDebug() << "VP_ShowsExportWorker: Failed to decrypt chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Write decrypted chunk
        qint64 written = target.write(decryptedChunk);
        if (written != decryptedChunk.size()) {
            qDebug() << "VP_ShowsExportWorker: Failed to write decrypted chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Update progress
        processedSize += sizeof(qint32) + chunkSize;
        if (encryptedContentSize > 0) {
            currentFileProgress = static_cast<int>((processedSize * 100) / encryptedContentSize);
            emit currentFileProgressUpdated(currentFileProgress);
        }
        
        // Allow other threads to run
        QThread::msleep(1);
    }
    
    source.close();
    
    // Ensure all data is written to disk
    target.flush();
    target.close();
    
#ifdef Q_OS_WIN
    // On Windows, ensure proper file permissions
    QFile::setPermissions(fileInfo.targetFile, 
                         QFile::ReadOwner | QFile::WriteOwner | 
                         QFile::ReadUser | QFile::WriteUser);
#endif
    
    qDebug() << "VP_ShowsExportWorker: Successfully exported file to" << fileInfo.targetFile;
    return true;
}
