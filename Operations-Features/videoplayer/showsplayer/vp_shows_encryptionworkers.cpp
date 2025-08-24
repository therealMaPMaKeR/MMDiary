#include "vp_shows_encryptionworkers.h"
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

//---------------- VP_ShowsEncryptionWorker ----------------//

VP_ShowsEncryptionWorker::VP_ShowsEncryptionWorker(const QStringList& sourceFiles, 
                                                   const QStringList& targetFiles,
                                                   const QString& showName,
                                                   const QByteArray& encryptionKey, 
                                                   const QString& username,
                                                   const QString& language,
                                                   const QString& translation)
    : m_sourceFiles(sourceFiles)
    , m_targetFiles(targetFiles)
    , m_showName(showName)
    , m_language(language)
    , m_translation(translation)
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
    
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
    if (m_tmdbManager) {
        delete m_tmdbManager;
        m_tmdbManager = nullptr;
    }
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
    
    // Load existing episodes from the target folder to detect duplicates
    loadExistingEpisodes();
    
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
    if (m_showName.isEmpty()) {
        qDebug() << "VP_ShowsEncryptionWorker: Cannot fetch TMDB data without show name";
        return false;
    }
    
    // Search for the show on TMDB
    bool success = m_tmdbManager->searchTVShow(m_showName, m_showInfo);
    
    if (success) {
        qDebug() << "VP_ShowsEncryptionWorker: Found TMDB data for show:" << m_showInfo.showName;
        
        // Build the episode map for absolute numbering support
        if (m_showInfo.tmdbId > 0) {
            qDebug() << "VP_ShowsEncryptionWorker: Building episode map for absolute numbering...";
            m_episodeMap = m_tmdbManager->buildEpisodeMap(m_showInfo.tmdbId);
            qDebug() << "VP_ShowsEncryptionWorker: Episode map built with" << m_episodeMap.size() << "episodes";
            
            // Fetch movie titles for content detection
            m_movieTitles = m_tmdbManager->getShowMovieTitles(m_showInfo.tmdbId);
            if (!m_movieTitles.isEmpty()) {
                qDebug() << "VP_ShowsEncryptionWorker: Found" << m_movieTitles.size() << "related movies";
            }
            
            // Fetch OVA/special titles from Season 0 that are likely actual OVAs
            // Only include titles with OVA/OAD keywords to avoid false matches
            QList<VP_ShowsTMDB::EpisodeInfo> specials = m_tmdbManager->getShowSpecials(m_showInfo.tmdbId);
            for (const auto& special : specials) {
                if (!special.episodeName.isEmpty()) {
                    QString lowerName = special.episodeName.toLower();
                    // Only add if it has OVA/OAD indicators or is clearly a special
                    if (lowerName.contains("ova") || lowerName.contains("oad") || 
                        lowerName.contains("special") || lowerName.contains("short") ||
                        lowerName.contains("bonus") || lowerName.contains("extra")) {
                        m_ovaTitles.append(special.episodeName);
                        qDebug() << "VP_ShowsEncryptionWorker: Added OVA/Special for matching:" << special.episodeName;
                    }
                }
            }
            
            if (!m_ovaTitles.isEmpty()) {
                qDebug() << "VP_ShowsEncryptionWorker: Found" << m_ovaTitles.size() << "special/OVA titles for matching";
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
    
    // Save show description if available
    if (!m_showInfo.overview.isEmpty()) {
        // Generate the obfuscated folder name
        QDir showDir(targetFolder);
        QString obfuscatedName = showDir.dirName();
        
        // Create the filename with prefix showdesc_
        QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
        QString descFilePath = showDir.absoluteFilePath(descFileName);
        
        // Encrypt and save the description
        bool descSaved = OperationsFiles::writeEncryptedFile(descFilePath, m_encryptionKey, m_showInfo.overview);
        
        if (descSaved) {
            qDebug() << "VP_ShowsEncryptionWorker: Successfully saved show description";
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: Failed to save show description";
        }
    }
    
    // Save show image if available
    if (m_showInfo.posterPath.isEmpty()) {
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
    
    m_showImagePath = encryptedImagePath;
    qDebug() << "VP_ShowsEncryptionWorker: Successfully encrypted show image to:" << encryptedImagePath;
    
    return true;
}

VP_ShowsMetadata::ShowMetadata VP_ShowsEncryptionWorker::createMetadataWithTMDB(const QString& filename)
{
    VP_ShowsMetadata::ShowMetadata metadata;
    metadata.filename = filename;
    metadata.showName = m_showName;
    metadata.language = m_language;
    metadata.translation = m_translation;
    
    // Initialize content type to Regular by default
    metadata.contentType = VP_ShowsMetadata::Regular;
    qDebug() << "VP_ShowsEncryptionWorker: Starting content type detection for:" << filename;
    
    // Try to parse season and episode from filename FIRST
    int season = 0, episode = 0;
    bool parsedSuccessfully = VP_ShowsTMDB::parseEpisodeFromFilename(filename, season, episode);
    
    qDebug() << "VP_ShowsEncryptionWorker: Parse result - Success:" << parsedSuccessfully 
             << "Season:" << season << "Episode:" << episode;
    
    // Determine content type based on parsed results
    if (parsedSuccessfully && episode > 0) {
        if (season > 0) {
            // Valid season and episode - this is ALWAYS a regular episode
            // No need to check for OVA/Movie patterns
            metadata.contentType = VP_ShowsMetadata::Regular;
            qDebug() << "VP_ShowsEncryptionWorker: Valid S" << season << "E" << episode 
                     << "found - setting as Regular episode (skipping OVA/Movie detection)";
        } else if (season == 0) {
            // Season 0 episodes are specials/extras
            metadata.contentType = VP_ShowsMetadata::Extra;
            qDebug() << "VP_ShowsEncryptionWorker: Season 0 episode detected - setting as Extra";
        }
    } else {
        // Only detect content type for files without valid episode numbers
        metadata.contentType = VP_ShowsMetadata::detectContentType(filename, m_movieTitles, m_ovaTitles);
        qDebug() << "VP_ShowsEncryptionWorker: No valid episode numbers - auto-detected content type:" 
                 << metadata.contentType << "(" << metadata.getContentTypeString() << ")";
    }
    
    // Check if this is a movie that should also appear in regular episodes (dual display)
    // This is for movies that are part of the series continuity
    // BUT don't apply this if we already determined it's a Regular episode from S##E## parsing
    if (metadata.contentType == VP_ShowsMetadata::Movie && parsedSuccessfully && episode > 0 && season <= 0) {
        metadata.isDualDisplay = true;
        qDebug() << "VP_ShowsEncryptionWorker: Movie with episode numbering detected - will display in both categories";
    }
    
    if (parsedSuccessfully && episode > 0) {  // Note: season can be 0 for absolute numbering
        qDebug() << "VP_ShowsEncryptionWorker: Parsed episode from filename:" << filename
                 << "-> Season:" << season << "Episode:" << episode;
        
        // Check if this episode is a duplicate
        if (checkForDuplicateEpisode(season, episode, m_language, m_translation)) {
            qDebug() << "VP_ShowsEncryptionWorker: Duplicate episode detected - S" << season << "E" << episode
                     << "for" << m_language << m_translation;
            qDebug() << "VP_ShowsEncryptionWorker: Marking as error:" << filename;
            
            // Mark this episode as an error
            metadata.season = "error";
            metadata.episode = "error";
            // Don't fetch TMDB data for error episodes
            return metadata;
        }
        
        // Not a duplicate, add to processed set
        QString episodeKey;
        if (season == 0 && m_episodeMap.contains(episode)) {
            // For absolute numbering, use the actual season/episode from the map
            const VP_ShowsTMDB::EpisodeMapping& mapping = m_episodeMap[episode];
            episodeKey = QString("S%1E%2_%3_%4")
                .arg(mapping.season, 2, 10, QChar('0'))
                .arg(mapping.episode, 2, 10, QChar('0'))
                .arg(m_language)
                .arg(m_translation);
            
            // Check if the mapped season is 0 (specials) - override content type
            // But ONLY if we didn't already determine it's a Regular episode from standard S##E## parsing
            if (mapping.season == 0 && metadata.contentType != VP_ShowsMetadata::Regular) {
                metadata.contentType = VP_ShowsMetadata::Extra;
                qDebug() << "VP_ShowsEncryptionWorker: Episode mapped to Season 0 - marking as Extra content";
            } else if (mapping.season > 0 && season == 0) {
                // Absolute numbering mapped to a regular season - ensure it's marked as Regular
                metadata.contentType = VP_ShowsMetadata::Regular;
                qDebug() << "VP_ShowsEncryptionWorker: Absolute episode" << episode 
                         << "mapped to S" << mapping.season << "E" << mapping.episode 
                         << "- setting as Regular episode";
            }
        } else {
            episodeKey = QString("S%1E%2_%3_%4")
                .arg(season, 2, 10, QChar('0'))
                .arg(episode, 2, 10, QChar('0'))
                .arg(m_language)
                .arg(m_translation);
        }
        m_processedEpisodes.insert(episodeKey);
        
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
                    // No mapping found - use a fallback calculation
                    qDebug() << "VP_ShowsEncryptionWorker: No mapping for absolute episode" << episode 
                             << "in map of" << m_episodeMap.size() << "episodes";
                    
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
            
            if (needToFetchEpisodeInfo && m_tmdbManager->getEpisodeInfo(m_showInfo.tmdbId, tmdbSeason, tmdbEpisode, episodeInfo)) {
                metadata.EPName = episodeInfo.episodeName;
                metadata.airDate = episodeInfo.airDate;  // Store the air date from TMDB
                
                // Download and scale episode thumbnail if available
                if (!episodeInfo.stillPath.isEmpty()) {
                    QString tempDir = VP_ShowsConfig::getTempDirectory(m_username);
                    // Use consistent naming pattern with unique identifier for cleanup
                    QString tempThumbPath = tempDir + "/temp_episode_thumb_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
                    
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
                
                qDebug() << "VP_ShowsEncryptionWorker: Added TMDB episode data:" << metadata.EPName 
                         << "Air date:" << metadata.airDate;
            } else {
                qDebug() << "VP_ShowsEncryptionWorker: Failed to get TMDB episode info for S" << tmdbSeason << "E" << tmdbEpisode;
                qDebug() << "VP_ShowsEncryptionWorker: TMDB ID:" << m_showInfo.tmdbId << "Original absolute episode:" << episode;
            }
        } else {
            qDebug() << "VP_ShowsEncryptionWorker: TMDB data not available or invalid show ID";
        }
    } else {
        qDebug() << "VP_ShowsEncryptionWorker: Could not parse episode info from filename:" << filename;
        
        // If we couldn't parse valid episode numbers and it's not Movie or OVA, mark it as Extra
        if (metadata.contentType != VP_ShowsMetadata::Movie && 
            metadata.contentType != VP_ShowsMetadata::OVA) {
            qDebug() << "VP_ShowsEncryptionWorker: No valid episode number found, marking as Extra content";
            metadata.contentType = VP_ShowsMetadata::Extra;
            metadata.season = "0";
            metadata.episode = "0";
        }
    }
    
    return metadata;
}

bool VP_ShowsEncryptionWorker::checkForDuplicateEpisode(int season, int episode, const QString& language, const QString& translation)
{
    QString episodeKey;
    
    // For absolute numbering (season == 0), we need to check against the actual season/episode mapping
    if (season == 0 && m_episodeMap.contains(episode)) {
        // Use the actual season/episode from the map for the key
        const VP_ShowsTMDB::EpisodeMapping& mapping = m_episodeMap[episode];
        episodeKey = QString("S%1E%2_%3_%4")
            .arg(mapping.season, 2, 10, QChar('0'))
            .arg(mapping.episode, 2, 10, QChar('0'))
            .arg(language)
            .arg(translation);
    } else {
        // Create the episode key with language and translation
        episodeKey = QString("S%1E%2_%3_%4")
            .arg(season, 2, 10, QChar('0'))
            .arg(episode, 2, 10, QChar('0'))
            .arg(language)
            .arg(translation);
    }
    
    // Check if this episode already exists in the target folder or was already processed in this batch
    return m_existingEpisodes.contains(episodeKey) || m_processedEpisodes.contains(episodeKey);
}

void VP_ShowsEncryptionWorker::loadExistingEpisodes()
{
    qDebug() << "VP_ShowsEncryptionWorker: Loading existing episodes to detect duplicates";
    
    m_existingEpisodes.clear();
    m_processedEpisodes.clear();
    
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
        
        if (m_metadataManager->readMetadataFromFile(filePath, existingMetadata)) {
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
                    
                m_existingEpisodes.insert(episodeKey);
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
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
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
    , m_cancelled(false)
{
    qDebug() << "VP_ShowsExportWorker: Constructor called for" << files.size() << "files";
    m_metadataManager = new VP_ShowsMetadata(encryptionKey, username);
}

VP_ShowsExportWorker::~VP_ShowsExportWorker()
{
    qDebug() << "VP_ShowsExportWorker: Destructor called";
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
}

void VP_ShowsExportWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
    qDebug() << "VP_ShowsExportWorker: Cancellation requested";
}

void VP_ShowsExportWorker::doExport()
{
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
        // Check for cancellation
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_cancelled) {
                qDebug() << "VP_ShowsExportWorker: Export cancelled by user";
                emit exportFinished(false, "Export cancelled by user", 
                                   successfulFiles, failedFiles);
                return;
            }
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
    
    QFile target(fileInfo.targetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsExportWorker: Failed to open target file:" << target.errorString();
        source.close();
        return false;
    }
    
    // Read and verify metadata (but don't write it to target)
    VP_ShowsMetadata::ShowMetadata metadata;
    if (!m_metadataManager->readFixedSizeEncryptedMetadata(&source, metadata)) {
        qDebug() << "VP_ShowsExportWorker: Failed to read metadata";
        source.close();
        target.close();
        target.remove();
        return false;
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
