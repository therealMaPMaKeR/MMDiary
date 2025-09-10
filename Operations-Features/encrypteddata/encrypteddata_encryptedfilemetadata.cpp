#include "encrypteddata_encryptedfilemetadata.h"
#include "inputvalidation.h"
#include "encryption/CryptoUtils.h"
#include "constants.h"
#include <QFile>
#include <QDebug>
#include <QIODevice>
#include <QCryptographicHash>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <cstring>
#include <QPainter>

EncryptedFileMetadata::EncryptedFileMetadata(const QByteArray& encryptionKey, const QString& username)
    : m_encryptionKey(encryptionKey), m_username(username)
{
    // Constructor - store encryption parameters
}

EncryptedFileMetadata::~EncryptedFileMetadata()
{
    // Destructor - nothing special needed
}

// ============================================================================
// Static Validation Methods (Updated)
// ============================================================================

bool EncryptedFileMetadata::isValidCategory(const QString& category)
{
    if (category.isEmpty()) {
        return true; // Empty category is valid
    }

    InputValidation::ValidationResult result = InputValidation::validateInput(
        category, InputValidation::InputType::CategoryTag, MAX_CATEGORY_LENGTH);
    return result.isValid;
}

bool EncryptedFileMetadata::isValidTag(const QString& tag)
{
    if (tag.isEmpty()) {
        return false; // Empty tags are not valid (but empty tag list is)
    }

    InputValidation::ValidationResult result = InputValidation::validateInput(
        tag, InputValidation::InputType::CategoryTag, MAX_TAG_LENGTH);
    return result.isValid;
}

bool EncryptedFileMetadata::isValidTagList(const QStringList& tags)
{
    if (tags.size() > MAX_TAGS) {
        return false;
    }

    for (const QString& tag : tags) {
        if (!isValidTag(tag)) {
            return false;
        }
    }

    return true;
}

bool EncryptedFileMetadata::isValidFilename(const QString& filename)
{
    InputValidation::ValidationResult result = InputValidation::validateInput(
        filename, InputValidation::InputType::FileName, 255);
    return result.isValid;
}

// ============================================================================
// NEW: Thumbnail Utility Methods
// ============================================================================

QPixmap EncryptedFileMetadata::createSquareThumbnail(const QPixmap& sourcePixmap, int size)
{
    if (sourcePixmap.isNull()) {
        qWarning() << "Source pixmap is null for square thumbnail creation";
        return QPixmap();
    }

    qDebug() << "Creating square thumbnail from source size:" << sourcePixmap.size() << "target size:" << size;

    // Scale the source to fit within the target size while keeping aspect ratio
    QPixmap scaled = sourcePixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    qDebug() << "Scaled source to:" << scaled.size();

    // Create a square black canvas
    QPixmap square(size, size);
    square.fill(Qt::black);

    // Calculate position to center the scaled image
    int x = (size - scaled.width()) / 2;
    int y = (size - scaled.height()) / 2;

    qDebug() << "Centering scaled image at position:" << x << "," << y;

    // Draw the scaled image onto the black canvas
    QPainter painter(&square);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(x, y, scaled);
    painter.end();

    qDebug() << "Created square thumbnail with black padding, final size:" << square.size();
    return square;
}

QByteArray EncryptedFileMetadata::compressThumbnail(const QPixmap& thumbnail, int quality)
{
    if (thumbnail.isNull()) {
        return QByteArray();
    }

    QByteArray thumbnailData;
    QBuffer buffer(&thumbnailData);
    buffer.open(QIODevice::WriteOnly);

    // Save as JPEG with specified quality for good compression
    if (!thumbnail.save(&buffer, "JPEG", quality)) {
        qWarning() << "Failed to compress thumbnail to JPEG";
        return QByteArray();
    }

    qDebug() << "Compressed thumbnail size:" << thumbnailData.size() << "bytes";
    return thumbnailData;
}

QPixmap EncryptedFileMetadata::decompressThumbnail(const QByteArray& thumbnailData)
{
    if (thumbnailData.isEmpty()) {
        return QPixmap();
    }

    QPixmap thumbnail;
    if (!thumbnail.loadFromData(thumbnailData, "JPEG")) {
        qWarning() << "Failed to decompress thumbnail from JPEG data";
        return QPixmap();
    }

    return thumbnail;
}

QPixmap EncryptedFileMetadata::createThumbnailFromImage(const QString& imagePath, int size)
{
    QPixmap originalPixmap;
    if (!originalPixmap.load(imagePath)) {
        qWarning() << "Failed to load image for thumbnail:" << imagePath;
        return QPixmap();
    }

    qDebug() << "Loaded original image:" << imagePath << "size:" << originalPixmap.size();

    // UPDATED: Create square thumbnail with black padding instead of aspect-ratio scaling
    QPixmap thumbnail = createSquareThumbnail(originalPixmap, size);

    if (!thumbnail.isNull()) {
        qDebug() << "Created square thumbnail from image:" << imagePath << "final size:" << thumbnail.size();
    } else {
        qWarning() << "Failed to create square thumbnail from image:" << imagePath;
    }

    return thumbnail;
}

// ============================================================================
// NEW: Thumbnail Access Methods
// ============================================================================

QPixmap EncryptedFileMetadata::getThumbnailFromFile(const QString& filePath, int size)
{
    FileMetadata metadata;
    if (!readMetadataFromFile(filePath, metadata)) {
        return QPixmap();
    }

    if (metadata.thumbnailData.isEmpty()) {
        return QPixmap();
    }

    QPixmap thumbnail = decompressThumbnail(metadata.thumbnailData);

    // Scale to requested size if different
    if (!thumbnail.isNull() && (thumbnail.width() != size || thumbnail.height() != size)) {
        thumbnail = thumbnail.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return thumbnail;
}

bool EncryptedFileMetadata::hasThumbnail(const QString& filePath)
{
    FileMetadata metadata;
    if (!readMetadataFromFile(filePath, metadata)) {
        return false;
    }

    return !metadata.thumbnailData.isEmpty();
}

// ============================================================================
// Core File Operations (existing methods unchanged)
// ============================================================================

bool EncryptedFileMetadata::writeMetadataToFile(const QString& filePath, const FileMetadata& metadata)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing metadata:" << filePath;
        return false;
    }

    bool result = writeMetadataToOpenFile(&file, metadata);
    file.close();
    return result;
}

bool EncryptedFileMetadata::readMetadataFromFile(const QString& filePath, FileMetadata& metadata)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for reading metadata:" << filePath;
        return false;
    }

    bool result = readMetadataFromOpenFile(&file, metadata);
    file.close();
    return result;
}

bool EncryptedFileMetadata::updateMetadataInFile(const QString& filePath, const FileMetadata& newMetadata)
{
    qDebug() << "Updating metadata in-place using fixed-size approach for:" << filePath;

    try {
        // Open file for read/write
        QFile file(filePath);
        if (!file.open(QIODevice::ReadWrite)) {
            qWarning() << "Failed to open file for metadata update:" << filePath;
            return false;
        }

        // Seek to beginning and overwrite the fixed-size metadata block
        file.seek(0);
        bool result = writeFixedSizeEncryptedMetadata(&file, newMetadata);

        if (result) {
            file.flush(); // Ensure data is written to disk
            qDebug() << "Successfully updated metadata in-place";
        } else {
            qWarning() << "Failed to write updated metadata";
        }

        file.close();
        return result;

    } catch (const std::exception& e) {
        qWarning() << "Exception updating metadata in file:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception updating metadata in file";
        return false;
    }
}

QString EncryptedFileMetadata::getFilenameFromFile(const QString& filePath)
{
    FileMetadata metadata;
    if (readMetadataFromFile(filePath, metadata)) {
        return metadata.filename;
    }
    return QString();
}

bool EncryptedFileMetadata::hasNewFormat(const QString& filePath)
{
    // Since we're only supporting new format now, always return true
    // if file exists and is large enough
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    bool hasNewFormat = (file.size() >= Constants::METADATA_RESERVED_SIZE);
    file.close();
    return hasNewFormat;
}

QByteArray EncryptedFileMetadata::createEncryptedMetadataChunk(const FileMetadata& metadata)
{
    return createFixedSizeEncryptedMetadata(metadata);
}

// ============================================================================
// UPDATED: Fixed-Size Metadata Operations (Updated for Thumbnails)
// ============================================================================

QByteArray EncryptedFileMetadata::createFixedSizeEncryptedMetadata(const FileMetadata& metadata)
{
    // Create raw metadata chunk (now includes thumbnail)
    QByteArray metadataChunk = createMetadataChunk(metadata);
    if (metadataChunk.isEmpty()) {
        qWarning() << "Failed to create metadata chunk";
        return QByteArray();
    }

    // Check if raw metadata exceeds maximum size
    if (metadataChunk.size() > Constants::MAX_RAW_METADATA_SIZE) {
        qWarning() << "Raw metadata too large:" << metadataChunk.size()
        << "bytes (max:" << Constants::MAX_RAW_METADATA_SIZE << ")";
        return QByteArray();
    }

    qDebug() << "Raw metadata size (with thumbnail):" << metadataChunk.size() << "bytes";

    // Encrypt the metadata chunk
    QByteArray encryptedMetadata = CryptoUtils::Encryption_EncryptBArray(
        m_encryptionKey, metadataChunk, m_username);

    if (encryptedMetadata.isEmpty()) {
        qWarning() << "Failed to encrypt metadata chunk";
        return QByteArray();
    }

    qDebug() << "Encrypted metadata size:" << encryptedMetadata.size() << "bytes";

    // Check if encrypted metadata fits in reserved space (leaving room for size header)
    const int availableSpace = Constants::METADATA_RESERVED_SIZE - sizeof(quint32);
    if (encryptedMetadata.size() > availableSpace) {
        qWarning() << "Encrypted metadata too large:" << encryptedMetadata.size()
        << "bytes (max:" << availableSpace << ")";
        return QByteArray();
    }

    // Create fixed-size block
    QByteArray fixedSizeBlock;
    fixedSizeBlock.reserve(Constants::METADATA_RESERVED_SIZE);

    // SECURITY: Validate size before conversion to unsigned
    if (encryptedMetadata.size() < 0) {
        qWarning() << "Invalid encrypted metadata size (negative):" << encryptedMetadata.size();
        return QByteArray();
    }
    
    // Write metadata size (4 bytes)
    quint32 metadataSize = static_cast<quint32>(encryptedMetadata.size());
    fixedSizeBlock.append(reinterpret_cast<const char*>(&metadataSize), sizeof(metadataSize));

    qDebug() << "Writing metadata size to fixed block:" << metadataSize << "bytes";

    // Write encrypted metadata
    fixedSizeBlock.append(encryptedMetadata);

    // SECURITY: Validate padding calculation to prevent negative values
    if (fixedSizeBlock.size() > Constants::METADATA_RESERVED_SIZE) {
        qWarning() << "Fixed-size block exceeds reserved size:" << fixedSizeBlock.size() 
                   << "(max:" << Constants::METADATA_RESERVED_SIZE << ")";
        return QByteArray();
    }
    
    // Pad to exactly METADATA_RESERVED_SIZE with zeros
    int paddingNeeded = Constants::METADATA_RESERVED_SIZE - fixedSizeBlock.size();
    if (paddingNeeded > 0) {
        fixedSizeBlock.append(QByteArray(paddingNeeded, 0));
    } else if (paddingNeeded < 0) {
        // This should never happen due to the check above, but defensive programming
        qCritical() << "EncryptedFileMetadata: Critical error - negative padding calculated:" << paddingNeeded;
        return QByteArray();
    }

    qDebug() << "Created fixed-size metadata block:" << fixedSizeBlock.size()
             << "bytes (data:" << (sizeof(metadataSize) + encryptedMetadata.size())
             << ", padding:" << paddingNeeded << ")";

    return fixedSizeBlock;
}

bool EncryptedFileMetadata::readFixedSizeEncryptedMetadata(QIODevice* file, FileMetadata& metadata)
{
    if (!file || !file->isReadable()) {
        qWarning() << "Invalid file for reading fixed-size metadata";
        return false;
    }

    try {
        // Read the entire fixed-size metadata block
        QByteArray metadataBlock = file->read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            qWarning() << "Failed to read complete metadata block, got:"
                       << metadataBlock.size() << "expected:" << Constants::METADATA_RESERVED_SIZE;
            return false;
        }

        // Extract metadata size from first 4 bytes
        quint32 metadataSize = 0;
        memcpy(&metadataSize, metadataBlock.constData(), sizeof(metadataSize));

        qDebug() << "Read metadata size from fixed block:" << metadataSize << "bytes";

        // Validate metadata size
        const quint32 maxAllowedSize = static_cast<quint32>(Constants::METADATA_RESERVED_SIZE - sizeof(quint32));
        if (metadataSize == 0 || metadataSize > maxAllowedSize) {
            qWarning() << "Invalid metadata size in fixed block:" << metadataSize
                       << "(max allowed:" << maxAllowedSize << ")";
            return false;
        }

        // Extract encrypted metadata
        QByteArray encryptedMetadata = metadataBlock.mid(sizeof(metadataSize), metadataSize);
        if (static_cast<quint32>(encryptedMetadata.size()) != metadataSize) {
            qWarning() << "Failed to extract encrypted metadata from fixed block - size mismatch: "
                       << encryptedMetadata.size() << " vs expected " << metadataSize;
            return false;
        }

        qDebug() << "Read fixed-size metadata: block size:" << metadataBlock.size()
                 << ", metadata size:" << metadataSize;

        // Decrypt metadata
        QByteArray metadataChunk = CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedMetadata);
        if (metadataChunk.isEmpty()) {
            qWarning() << "Failed to decrypt metadata chunk from fixed block";
            return false;
        }

        // Parse metadata (now includes thumbnail)
        return parseMetadataChunk(metadataChunk, metadata);

    } catch (const std::exception& e) {
        qWarning() << "Exception reading fixed-size metadata:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception reading fixed-size metadata";
        return false;
    }
}

bool EncryptedFileMetadata::writeFixedSizeEncryptedMetadata(QIODevice* file, const FileMetadata& metadata)
{
    if (!file || !file->isWritable()) {
        qWarning() << "Invalid file for writing fixed-size metadata";
        return false;
    }

    try {
        // Create fixed-size encrypted metadata block
        QByteArray fixedSizeBlock = createFixedSizeEncryptedMetadata(metadata);
        if (fixedSizeBlock.isEmpty()) {
            qWarning() << "Failed to create fixed-size metadata block";
            return false;
        }

        // Verify block size
        if (fixedSizeBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            qWarning() << "Fixed-size block has wrong size:" << fixedSizeBlock.size()
            << "expected:" << Constants::METADATA_RESERVED_SIZE;
            return false;
        }

        // Write the fixed-size block
        if (file->write(fixedSizeBlock) != fixedSizeBlock.size()) {
            qWarning() << "Failed to write complete fixed-size metadata block";
            return false;
        }

        qDebug() << "Successfully wrote fixed-size metadata block:" << fixedSizeBlock.size() << "bytes";
        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception writing fixed-size metadata:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception writing fixed-size metadata";
        return false;
    }
}

// ============================================================================
// UPDATED: Internal Metadata Chunk Operations (Updated for Thumbnails)
// ============================================================================

QByteArray EncryptedFileMetadata::createMetadataChunk(const FileMetadata& metadata)
{
    // Validate inputs
    if (!isValidFilename(metadata.filename)) {
        qWarning() << "Invalid filename for metadata:" << metadata.filename;
        return QByteArray();
    }

    if (!isValidCategory(metadata.category)) {
        qWarning() << "Invalid category for metadata:" << metadata.category;
        return QByteArray();
    }

    if (!isValidTagList(metadata.tags)) {
        qWarning() << "Invalid tag list for metadata";
        return QByteArray();
    }

    // Validate thumbnail size
    if (!metadata.thumbnailData.isEmpty() && metadata.thumbnailData.size() > MAX_THUMBNAIL_SIZE) {
        qWarning() << "Thumbnail data too large:" << metadata.thumbnailData.size()
        << "bytes (max:" << MAX_THUMBNAIL_SIZE << ")";
        return QByteArray();
    }

    QByteArray chunk;

    // 1. Write filename
    QByteArray filenameBytes = metadata.filename.toUtf8();
    // SECURITY: Validate size before unsigned conversion
    if (filenameBytes.size() < 0 || filenameBytes.size() > MAX_FILENAME_LENGTH) {
        qWarning() << "Invalid filename size:" << filenameBytes.size();
        return QByteArray();
    }
    quint32 filenameLength = static_cast<quint32>(filenameBytes.size());
    chunk.append(reinterpret_cast<const char*>(&filenameLength), sizeof(filenameLength));
    chunk.append(filenameBytes);

    // 2. Write category
    QByteArray categoryBytes = metadata.category.toUtf8();
    // SECURITY: Validate size before unsigned conversion
    if (categoryBytes.size() < 0 || categoryBytes.size() > MAX_CATEGORY_LENGTH) {
        qWarning() << "Invalid category size:" << categoryBytes.size();
        return QByteArray();
    }
    quint32 categoryLength = static_cast<quint32>(categoryBytes.size());
    chunk.append(reinterpret_cast<const char*>(&categoryLength), sizeof(categoryLength));
    chunk.append(categoryBytes);

    // 3. Write tags
    quint32 tagCount = static_cast<quint32>(metadata.tags.size());
    chunk.append(reinterpret_cast<const char*>(&tagCount), sizeof(tagCount));

    for (const QString& tag : metadata.tags) {
        QByteArray tagBytes = tag.toUtf8();
        quint32 tagLength = static_cast<quint32>(tagBytes.size());
        chunk.append(reinterpret_cast<const char*>(&tagLength), sizeof(tagLength));
        chunk.append(tagBytes);
    }

    // 4. Write thumbnail data
    // SECURITY: Validate size before unsigned conversion
    if (metadata.thumbnailData.size() < 0 || metadata.thumbnailData.size() > MAX_THUMBNAIL_SIZE) {
        qWarning() << "Invalid thumbnail data size:" << metadata.thumbnailData.size();
        return QByteArray();
    }
    quint32 thumbnailLength = static_cast<quint32>(metadata.thumbnailData.size());
    chunk.append(reinterpret_cast<const char*>(&thumbnailLength), sizeof(thumbnailLength));
    if (thumbnailLength > 0) {
        chunk.append(metadata.thumbnailData);
        qDebug() << "Added thumbnail data to metadata chunk:" << thumbnailLength << "bytes";
    }

    // 5. NEW: Write encryption datetime (if valid)
    if (metadata.encryptionDateTime.isValid()) {
        qint64 encryptionTimestamp = metadata.encryptionDateTime.toMSecsSinceEpoch();
        chunk.append(reinterpret_cast<const char*>(&encryptionTimestamp), sizeof(encryptionTimestamp));
        qDebug() << "Added encryption datetime to metadata chunk:" << metadata.encryptionDateTime.toString();
    }

    // Check size limit for raw metadata
    if (chunk.size() > Constants::MAX_RAW_METADATA_SIZE) {
        qWarning() << "Raw metadata chunk too large:" << chunk.size()
        << "bytes (max:" << Constants::MAX_RAW_METADATA_SIZE << ")";
        return QByteArray();
    }

    qDebug() << "Created metadata chunk with encryption date:" << chunk.size() << "bytes";
    return chunk;
}

bool EncryptedFileMetadata::parseMetadataChunk(const QByteArray& chunk, FileMetadata& metadata)
{
    metadata = FileMetadata(); // Clear previous data

    if (chunk.isEmpty()) {
        qWarning() << "Empty metadata chunk";
        return false;
    }

    // SECURITY: Use QDataStream for safe parsing instead of raw pointer operations
    QDataStream stream(chunk);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_5_15);

    try {
        // 1. Read filename
        quint32 filenameLength = 0;
        stream >> filenameLength;
        
        // SECURITY: Validate length before reading
        if (filenameLength > MAX_FILENAME_LENGTH || filenameLength == 0) {
            qWarning() << "Invalid filename length:" << filenameLength;
            return false;
        }
        
        QByteArray filenameBytes(filenameLength, '\0');
        int bytesRead = stream.readRawData(filenameBytes.data(), filenameLength);
        if (bytesRead != static_cast<int>(filenameLength)) {
            qWarning() << "Failed to read filename completely";
            return false;
        }
        metadata.filename = QString::fromUtf8(filenameBytes);

        // 2. Read category
        quint32 categoryLength = 0;
        stream >> categoryLength;
        
        // SECURITY: Validate length
        if (categoryLength > MAX_CATEGORY_LENGTH) {
            qWarning() << "Invalid category length:" << categoryLength;
            return false;
        }
        
        if (categoryLength > 0) {
            QByteArray categoryBytes(categoryLength, '\0');
            bytesRead = stream.readRawData(categoryBytes.data(), categoryLength);
            if (bytesRead != static_cast<int>(categoryLength)) {
                qWarning() << "Failed to read category completely";
                return false;
            }
            metadata.category = QString::fromUtf8(categoryBytes);
        }

        // 3. Read tags
        quint32 tagCount = 0;
        stream >> tagCount;
        
        // SECURITY: Validate tag count
        if (tagCount > MAX_TAGS) {
            qWarning() << "Invalid tag count:" << tagCount;
            return false;
        }
        
        for (quint32 i = 0; i < tagCount; ++i) {
            quint32 tagLength = 0;
            stream >> tagLength;
            
            // SECURITY: Validate tag length
            if (tagLength > MAX_TAG_LENGTH || tagLength == 0) {
                qWarning() << "Invalid tag length:" << tagLength;
                return false;
            }
            
            QByteArray tagBytes(tagLength, '\0');
            bytesRead = stream.readRawData(tagBytes.data(), tagLength);
            if (bytesRead != static_cast<int>(tagLength)) {
                qWarning() << "Failed to read tag completely";
                return false;
            }
            metadata.tags.append(QString::fromUtf8(tagBytes));
        }

        // 4. Read thumbnail data
        quint32 thumbnailLength = 0;
        stream >> thumbnailLength;
        
        // SECURITY: Validate thumbnail size
        if (thumbnailLength > MAX_THUMBNAIL_SIZE) {
            qWarning() << "Thumbnail too large:" << thumbnailLength << "bytes";
            return false;
        }
        
        if (thumbnailLength > 0) {
            metadata.thumbnailData.resize(thumbnailLength);
            bytesRead = stream.readRawData(metadata.thumbnailData.data(), thumbnailLength);
            if (bytesRead != static_cast<int>(thumbnailLength)) {
                qWarning() << "Failed to read thumbnail data completely";
                return false;
            }
            qDebug() << "Read thumbnail data from chunk:" << thumbnailLength << "bytes";
        }

        // 5. Read encryption datetime (if present)
        if (!stream.atEnd()) {
            qint64 encryptionTimestamp = 0;
            stream >> encryptionTimestamp;
            if (stream.status() == QDataStream::Ok && encryptionTimestamp > 0) {
                metadata.encryptionDateTime = QDateTime::fromMSecsSinceEpoch(encryptionTimestamp);
                qDebug() << "Read encryption datetime from chunk:" << metadata.encryptionDateTime.toString();
            }
        }

        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception parsing metadata chunk:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception parsing metadata chunk";
        return false;
    }
}

// ============================================================================
// File I/O Helpers (updated to use fixed-size operations)
// ============================================================================

bool EncryptedFileMetadata::readMetadataFromOpenFile(QIODevice* file, FileMetadata& metadata)
{
    return readFixedSizeEncryptedMetadata(file, metadata);
}

bool EncryptedFileMetadata::writeMetadataToOpenFile(QIODevice* file, const FileMetadata& metadata)
{
    return writeFixedSizeEncryptedMetadata(file, metadata);
}

// ============================================================================
// Safety Helpers (unchanged)
// ============================================================================

bool EncryptedFileMetadata::safeRead(const char* data, int& pos, int totalSize, void* dest, int size)
{
    if (pos + size > totalSize) {
        qWarning() << "Metadata chunk read overflow at position" << pos << "size" << size << "total" << totalSize;
        return false;
    }

    std::memcpy(dest, data + pos, size);
    pos += size;
    return true;
}
