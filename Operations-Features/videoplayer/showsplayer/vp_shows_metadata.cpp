#include "vp_shows_metadata.h"
#include "inputvalidation.h"
#include "CryptoUtils.h"
#include "../vp_metadata_lock_manager.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QRegularExpression>

// Initialize the reserved size constant
const int VP_ShowsMetadata::METADATA_RESERVED_SIZE;

VP_ShowsMetadata::VP_ShowsMetadata(const QByteArray& encryptionKey, const QString& username)
    : m_encryptionKey(encryptionKey)
    , m_username(username)
{
    qDebug() << "VP_ShowsMetadata: Constructor called";
}

VP_ShowsMetadata::~VP_ShowsMetadata()
{
    qDebug() << "VP_ShowsMetadata: Destructor called";
}

bool VP_ShowsMetadata::writeMetadataToFile(const QString& filePath, const ShowMetadata& metadata)
{
    qDebug() << "VP_ShowsMetadata: Writing metadata to file:" << filePath;
    
    // Acquire lock for metadata operation
    VP_MetadataLockManager::LockGuard lock(VP_MetadataLockManager::instance(), filePath);
    if (!lock.isLocked()) {
        qDebug() << "VP_ShowsMetadata: Failed to acquire lock for writing metadata. Lock result:" << static_cast<int>(lock.result());
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite)) {
        qDebug() << "VP_ShowsMetadata: Failed to open file for metadata writing:" << file.errorString();
        return false;
    }
    
    bool success = writeFixedSizeEncryptedMetadata(&file, metadata);
    file.close();
    
    return success;
}

bool VP_ShowsMetadata::readMetadataFromFile(const QString& filePath, ShowMetadata& metadata)
{
    qDebug() << "VP_ShowsMetadata: Reading metadata from file:" << filePath;
    
    // Acquire lock for metadata operation
    VP_MetadataLockManager::LockGuard lock(VP_MetadataLockManager::instance(), filePath);
    if (!lock.isLocked()) {
        qDebug() << "VP_ShowsMetadata: Failed to acquire lock for reading metadata. Lock result:" << static_cast<int>(lock.result());
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "VP_ShowsMetadata: Failed to open file for metadata reading:" << file.errorString();
        return false;
    }
    
    bool success = readFixedSizeEncryptedMetadata(&file, metadata);
    file.close();
    
    return success;
}

bool VP_ShowsMetadata::updateMetadataInFile(const QString& filePath, const ShowMetadata& newMetadata)
{
    qDebug() << "VP_ShowsMetadata: Updating metadata in file:" << filePath;
    
    // For now, just overwrite the metadata
    return writeMetadataToFile(filePath, newMetadata);
}

QString VP_ShowsMetadata::getFilenameFromFile(const QString& filePath)
{
    ShowMetadata metadata;
    if (readMetadataFromFile(filePath, metadata)) {
        return metadata.filename;
    }
    return QString();
}

QString VP_ShowsMetadata::getShowNameFromFile(const QString& filePath)
{
    ShowMetadata metadata;
    if (readMetadataFromFile(filePath, metadata)) {
        return metadata.showName;
    }
    return QString();
}

QByteArray VP_ShowsMetadata::createEncryptedMetadataChunk(const ShowMetadata& metadata)
{
    return createFixedSizeEncryptedMetadata(metadata);
}

bool VP_ShowsMetadata::isValidShowName(const QString& showName)
{
    if (showName.isEmpty()) {
        return false;
    }
    
    // Use the new TVShowName validation type that allows special characters
    InputValidation::ValidationResult result = InputValidation::validateInput(
        showName, InputValidation::InputType::TVShowName, MAX_SHOW_NAME_LENGTH);
    
    return result.isValid;
}

bool VP_ShowsMetadata::isValidFilename(const QString& filename)
{
    if (filename.isEmpty() || filename.length() > MAX_FILENAME_LENGTH) {
        return false;
    }
    
    // Check for invalid characters
    if (filename.contains(QChar('\0'))) {
        return false;
    }
    
    return true;
}

QByteArray VP_ShowsMetadata::createMetadataChunk(const ShowMetadata& metadata)
{
    // Security: Validate metadata fields before serialization
    if (metadata.filename.length() > MAX_FILENAME_LENGTH) {
        qDebug() << "VP_ShowsMetadata: Filename too long:" << metadata.filename.length();
        return QByteArray();
    }
    
    if (metadata.showName.length() > MAX_SHOW_NAME_LENGTH) {
        qDebug() << "VP_ShowsMetadata: Show name too long:" << metadata.showName.length();
        return QByteArray();
    }
    
    if (metadata.EPName.length() > MAX_EP_NAME_LENGTH) {
        qDebug() << "VP_ShowsMetadata: Episode name too long:" << metadata.EPName.length();
        return QByteArray();
    }
    
    if (metadata.EPDescription.length() > MAX_EP_DESCRIPTION_LENGTH) {
        qDebug() << "VP_ShowsMetadata: Episode description too long:" << metadata.EPDescription.length();
        return QByteArray();
    }
    
    if (metadata.EPImage.size() > MAX_EP_IMAGE_SIZE) {
        qDebug() << "VP_ShowsMetadata: Episode image too large:" << metadata.EPImage.size() << "bytes";
        return QByteArray();
    }
    
    QByteArray chunk;
    QDataStream stream(&chunk, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Write all metadata fields in order
    stream << metadata.filename;
    stream << metadata.showName;
    stream << metadata.season;
    stream << metadata.episode;
    stream << metadata.EPName;
    stream << metadata.EPDescription;
    stream << metadata.EPImage;
    stream << metadata.language;
    stream << metadata.translation;
    stream << metadata.airDate;
    stream << static_cast<qint32>(metadata.contentType);  // Write content type as int
    stream << metadata.isDualDisplay;
    stream << metadata.encryptionDateTime;
    
    return chunk;
}

bool VP_ShowsMetadata::parseMetadataChunk(const QByteArray& chunk, ShowMetadata& metadata)
{
    if (chunk.isEmpty()) {
        qDebug() << "VP_ShowsMetadata: Empty metadata chunk";
        return false;
    }
    
    QDataStream stream(chunk);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Read all metadata fields in order
    stream >> metadata.filename;
    stream >> metadata.showName;
    stream >> metadata.season;
    stream >> metadata.episode;
    stream >> metadata.EPName;
    stream >> metadata.EPDescription;
    stream >> metadata.EPImage;
    stream >> metadata.language;
    stream >> metadata.translation;
    stream >> metadata.airDate;
    
    // Read content type and dual display fields
    qint32 contentTypeInt;
    stream >> contentTypeInt;
    metadata.contentType = static_cast<VP_ShowsMetadata::ContentType>(contentTypeInt);
    stream >> metadata.isDualDisplay;
    stream >> metadata.encryptionDateTime;
    
    if (stream.status() != QDataStream::Ok) {
        qDebug() << "VP_ShowsMetadata: Failed to parse metadata chunk";
        return false;
    }
    
    return true;
}

QByteArray VP_ShowsMetadata::createFixedSizeEncryptedMetadata(const ShowMetadata& metadata)
{
    // Create the raw metadata chunk
    QByteArray rawMetadata = createMetadataChunk(metadata);
    
    // Check size limit (40KB for raw metadata before encryption)
    if (rawMetadata.size() > 40960) {
        qDebug() << "VP_ShowsMetadata: Metadata too large:" << rawMetadata.size() << "bytes";
        return QByteArray();
    }
    
    // Encrypt the metadata
    QByteArray encryptedMetadata = CryptoUtils::Encryption_EncryptBArray(m_encryptionKey, rawMetadata, m_username);
    
    if (encryptedMetadata.isEmpty()) {
        qDebug() << "VP_ShowsMetadata: Failed to encrypt metadata";
        return QByteArray();
    }
    
    // Security: Validate encrypted size fits in buffer with header
    // Header size: 4 bytes (magic) + 4 bytes (size) = 8 bytes
    const int HEADER_SIZE = 8;
    const int MAX_ENCRYPTED_SIZE = METADATA_RESERVED_SIZE - HEADER_SIZE;
    
    if (encryptedMetadata.size() > MAX_ENCRYPTED_SIZE) {
        qDebug() << "VP_ShowsMetadata: Encrypted metadata too large:" 
                 << encryptedMetadata.size() << "bytes (max:" << MAX_ENCRYPTED_SIZE << ")";
        return QByteArray();
    }
    
    // Create fixed-size buffer
    QByteArray fixedSizeBuffer(METADATA_RESERVED_SIZE, '\0');
    
    // Write size and encrypted data
    QDataStream stream(&fixedSizeBuffer, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Write magic number for validation
    stream << quint32(0x56504D44); // "VPMD" in hex
    
    // Write the size of encrypted metadata
    stream << qint32(encryptedMetadata.size());
    
    // Security: Verify stream position before writing data
    if (stream.device()->pos() + encryptedMetadata.size() > METADATA_RESERVED_SIZE) {
        qDebug() << "VP_ShowsMetadata: Stream position check failed";
        return QByteArray();
    }
    
    // Write the encrypted metadata
    int written = stream.writeRawData(encryptedMetadata.constData(), encryptedMetadata.size());
    if (written != encryptedMetadata.size()) {
        qDebug() << "VP_ShowsMetadata: Failed to write complete encrypted metadata";
        return QByteArray();
    }
    
    return fixedSizeBuffer;
}

bool VP_ShowsMetadata::readFixedSizeEncryptedMetadata(QIODevice* file, ShowMetadata& metadata)
{
    if (!file || !file->isOpen()) {
        qDebug() << "VP_ShowsMetadata: Invalid file device";
        return false;
    }
    
    // Seek to beginning
    file->seek(0);
    
    // Read the fixed-size metadata buffer
    QByteArray fixedSizeBuffer = file->read(METADATA_RESERVED_SIZE);
    
    if (fixedSizeBuffer.size() != METADATA_RESERVED_SIZE) {
        qDebug() << "VP_ShowsMetadata: Invalid metadata size:" << fixedSizeBuffer.size();
        return false;
    }
    
    QDataStream stream(fixedSizeBuffer);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Read and verify magic number
    quint32 magic;
    stream >> magic;
    
    if (magic != 0x56504D44) { // "VPMD"
        qDebug() << "VP_ShowsMetadata: Invalid magic number:" << QString::number(magic, 16);
        return false;
    }
    
    // Read the size of encrypted metadata
    qint32 encryptedSize;
    stream >> encryptedSize;
    
    if (encryptedSize <= 0 || encryptedSize > METADATA_RESERVED_SIZE - 8) {
        qDebug() << "VP_ShowsMetadata: Invalid encrypted metadata size:" << encryptedSize;
        return false;
    }
    
    // Read the encrypted metadata
    QByteArray encryptedMetadata(encryptedSize, '\0');
    stream.readRawData(encryptedMetadata.data(), encryptedSize);
    
    // Decrypt the metadata
    QByteArray decryptedMetadata = CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedMetadata);
    
    if (decryptedMetadata.isEmpty()) {
        qDebug() << "VP_ShowsMetadata: Failed to decrypt metadata";
        return false;
    }
    
    // Parse the decrypted metadata
    return parseMetadataChunk(decryptedMetadata, metadata);
}

bool VP_ShowsMetadata::writeFixedSizeEncryptedMetadata(QIODevice* file, const ShowMetadata& metadata)
{
    if (!file || !file->isOpen()) {
        qDebug() << "VP_ShowsMetadata: Invalid file device";
        return false;
    }
    
    // Create the fixed-size encrypted metadata
    QByteArray fixedSizeMetadata = createFixedSizeEncryptedMetadata(metadata);
    
    if (fixedSizeMetadata.isEmpty()) {
        qDebug() << "VP_ShowsMetadata: Failed to create fixed-size metadata";
        return false;
    }
    
    // Seek to beginning
    file->seek(0);
    
    // Write the metadata
    qint64 written = file->write(fixedSizeMetadata);
    
    if (written != METADATA_RESERVED_SIZE) {
        qDebug() << "VP_ShowsMetadata: Failed to write complete metadata. Written:" << written;
        return false;
    }
    
    return true;
}

bool VP_ShowsMetadata::safeRead(const char* data, int& pos, int totalSize, void* dest, int size)
{
    if (pos + size > totalSize) {
        return false;
    }
    
    memcpy(dest, data + pos, size);
    pos += size;
    return true;
}

// Content type detection implementation
VP_ShowsMetadata::ContentType VP_ShowsMetadata::detectContentType(const QString& filename, 
                                                                  const QStringList& tmdbMovieTitles,
                                                                  const QStringList& tmdbOvaTitles)
{
    // Check for OVA first (most specific)
    if (isOVAContent(filename, tmdbOvaTitles)) {
        return VP_ShowsMetadata::OVA;
    }
    
    // Check for movie content
    if (isMovieContent(filename, tmdbMovieTitles)) {
        return VP_ShowsMetadata::Movie;
    }
    
    // Check for extra/special content
    if (isExtraContent(filename)) {
        return VP_ShowsMetadata::Extra;
    }
    
    // Default to regular episode
    return VP_ShowsMetadata::Regular;
}

bool VP_ShowsMetadata::isMovieContent(const QString& filename, const QStringList& tmdbMovieTitles)
{
    QString lowerFilename = filename.toLower();
    
    // Direct movie indicators
    if (lowerFilename.contains("movie") || lowerFilename.contains("film")) {
        return true;
    }
    
    // Check against TMDB movie titles if provided
    if (!tmdbMovieTitles.isEmpty()) {
        for (const QString& movieTitle : tmdbMovieTitles) {
            // Normalize the movie title for comparison
            QString normalizedTitle = movieTitle.toLower();
            normalizedTitle.remove(QRegularExpression("[^a-z0-9 ]"));  // Remove special characters
            normalizedTitle.replace(" ", "");  // Remove spaces
            
            // Normalize filename for comparison
            QString normalizedFilename = lowerFilename;
            normalizedFilename.remove(QRegularExpression("[^a-z0-9 ]"));
            normalizedFilename.replace(" ", "");
            
            // Also check with underscores replaced
            QString underscoreVersion = movieTitle.toLower();
            underscoreVersion.replace(" ", "_");
            
            if (normalizedFilename.contains(normalizedTitle) || 
                lowerFilename.contains(underscoreVersion)) {
                qDebug() << "VP_ShowsMetadata: Detected movie content from TMDB title match:" << movieTitle;
                return true;
            }
        }
    }
    
    return false;
}

bool VP_ShowsMetadata::isOVAContent(const QString& filename, const QStringList& tmdbOvaTitles)
{
    QString lowerFilename = filename.toLower();
    
    // Direct OVA/OAD indicators
    if (lowerFilename.contains("ova") || 
        lowerFilename.contains("oad") ||
        (lowerFilename.contains("original") && lowerFilename.contains("animation")) ||
        (lowerFilename.contains("original") && lowerFilename.contains("video"))) {
        return true;
    }
    
    // Check against TMDB OVA/special titles if provided
    if (!tmdbOvaTitles.isEmpty()) {
        for (const QString& ovaTitle : tmdbOvaTitles) {
            // Normalize the OVA title for comparison
            QString normalizedTitle = ovaTitle.toLower();
            normalizedTitle.remove(QRegularExpression("[^a-z0-9 ]"));  // Remove special characters
            normalizedTitle.replace(" ", "");  // Remove spaces
            
            // Normalize filename for comparison
            QString normalizedFilename = lowerFilename;
            normalizedFilename.remove(QRegularExpression("[^a-z0-9 ]"));
            normalizedFilename.replace(" ", "");
            
            // Also check with underscores replaced
            QString underscoreVersion = ovaTitle.toLower();
            underscoreVersion.replace(" ", "_");
            
            if (normalizedFilename.contains(normalizedTitle) || 
                lowerFilename.contains(underscoreVersion)) {
                qDebug() << "VP_ShowsMetadata: Detected OVA content from TMDB title match:" << ovaTitle;
                return true;
            }
        }
    }
    
    return false;
}

bool VP_ShowsMetadata::isExtraContent(const QString& filename)
{
    QString lowerFilename = filename.toLower();
    
    // Check for special/extra content indicators
    if (lowerFilename.contains("special") ||
        lowerFilename.contains("extra") ||
        lowerFilename.contains("bonus") ||
        (lowerFilename.contains("behind") && lowerFilename.contains("scenes")) ||
        (lowerFilename.contains("deleted") && lowerFilename.contains("scene")) ||
        lowerFilename.contains("interview") ||
        lowerFilename.contains("preview") ||
        lowerFilename.contains("recap") ||
        lowerFilename.contains("crossover")) {
        return true;
    }
    
    // Check for Season 0 indicators (TMDB specials)
    QRegularExpression seasonZeroRegex("s(eason)?[\\s_-]?0", QRegularExpression::CaseInsensitiveOption);
    if (seasonZeroRegex.match(filename).hasMatch()) {
        return true;
    }
    
    return false;
}
