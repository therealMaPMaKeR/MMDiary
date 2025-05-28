#include "encryptedfilemetadata.h"
#include "inputvalidation.h"
#include "CryptoUtils.h"
#include "constants.h"
#include <QFile>
#include <QDebug>
#include <QIODevice>
#include <QCryptographicHash>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <cstring>

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

    // Create thumbnail with proper aspect ratio
    QPixmap thumbnail = originalPixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    qDebug() << "Created thumbnail from image:" << imagePath << "size:" << thumbnail.size();
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

    // Write metadata size (4 bytes)
    quint32 metadataSize = static_cast<quint32>(encryptedMetadata.size());
    fixedSizeBlock.append(reinterpret_cast<const char*>(&metadataSize), sizeof(metadataSize));

    qDebug() << "Writing metadata size to fixed block:" << metadataSize << "bytes";

    // Write encrypted metadata
    fixedSizeBlock.append(encryptedMetadata);

    // Pad to exactly METADATA_RESERVED_SIZE with zeros
    int paddingNeeded = Constants::METADATA_RESERVED_SIZE - fixedSizeBlock.size();
    if (paddingNeeded > 0) {
        fixedSizeBlock.append(QByteArray(paddingNeeded, 0));
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
        const int maxAllowedSize = Constants::METADATA_RESERVED_SIZE - sizeof(quint32);
        if (metadataSize == 0 || metadataSize > static_cast<quint32>(maxAllowedSize)) {
            qWarning() << "Invalid metadata size in fixed block:" << metadataSize
                       << "(max allowed:" << maxAllowedSize << ")";
            return false;
        }

        // Extract encrypted metadata
        QByteArray encryptedMetadata = metadataBlock.mid(sizeof(metadataSize), metadataSize);
        if (encryptedMetadata.size() != static_cast<int>(metadataSize)) {
            qWarning() << "Failed to extract encrypted metadata from fixed block";
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
    quint32 filenameLength = static_cast<quint32>(filenameBytes.size());
    chunk.append(reinterpret_cast<const char*>(&filenameLength), sizeof(filenameLength));
    chunk.append(filenameBytes);

    // 2. Write category
    QByteArray categoryBytes = metadata.category.toUtf8();
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

    // 4. NEW: Write thumbnail data
    quint32 thumbnailLength = static_cast<quint32>(metadata.thumbnailData.size());
    chunk.append(reinterpret_cast<const char*>(&thumbnailLength), sizeof(thumbnailLength));
    if (thumbnailLength > 0) {
        chunk.append(metadata.thumbnailData);
        qDebug() << "Added thumbnail data to metadata chunk:" << thumbnailLength << "bytes";
    }

    // Check size limit for raw metadata
    if (chunk.size() > Constants::MAX_RAW_METADATA_SIZE) {
        qWarning() << "Raw metadata chunk too large:" << chunk.size()
        << "bytes (max:" << Constants::MAX_RAW_METADATA_SIZE << ")";
        return QByteArray();
    }

    qDebug() << "Created metadata chunk with thumbnail:" << chunk.size() << "bytes";
    return chunk;
}

bool EncryptedFileMetadata::parseMetadataChunk(const QByteArray& chunk, FileMetadata& metadata)
{
    metadata = FileMetadata(); // Clear previous data

    if (chunk.isEmpty()) {
        qWarning() << "Empty metadata chunk";
        return false;
    }

    const char* data = chunk.constData();
    int pos = 0;
    int totalSize = chunk.size();

    try {
        // 1. Read filename
        quint32 filenameLength = 0;
        if (!safeRead(data, pos, totalSize, &filenameLength, sizeof(filenameLength))) {
            return false;
        }

        if (filenameLength == 0 || filenameLength > 1000) {
            qWarning() << "Invalid filename length in metadata:" << filenameLength;
            return false;
        }

        QByteArray filenameBytes(filenameLength, '\0');
        if (!safeRead(data, pos, totalSize, filenameBytes.data(), filenameLength)) {
            return false;
        }

        metadata.filename = QString::fromUtf8(filenameBytes);
        if (!isValidFilename(metadata.filename)) {
            qWarning() << "Invalid filename in metadata:" << metadata.filename;
            return false;
        }

        // 2. Read category
        quint32 categoryLength = 0;
        if (!safeRead(data, pos, totalSize, &categoryLength, sizeof(categoryLength))) {
            return false;
        }

        if (categoryLength > MAX_CATEGORY_LENGTH) {
            qWarning() << "Invalid category length in metadata:" << categoryLength;
            return false;
        }

        if (categoryLength > 0) {
            QByteArray categoryBytes(categoryLength, '\0');
            if (!safeRead(data, pos, totalSize, categoryBytes.data(), categoryLength)) {
                return false;
            }

            metadata.category = QString::fromUtf8(categoryBytes);
            if (!isValidCategory(metadata.category)) {
                qWarning() << "Invalid category in metadata:" << metadata.category;
                return false;
            }
        }

        // 3. Read tags
        quint32 tagCount = 0;
        if (!safeRead(data, pos, totalSize, &tagCount, sizeof(tagCount))) {
            return false;
        }

        if (tagCount > MAX_TAGS) {
            qWarning() << "Too many tags in metadata:" << tagCount;
            return false;
        }

        for (quint32 i = 0; i < tagCount; ++i) {
            quint32 tagLength = 0;
            if (!safeRead(data, pos, totalSize, &tagLength, sizeof(tagLength))) {
                return false;
            }

            if (tagLength > MAX_TAG_LENGTH) {
                qWarning() << "Invalid tag length in metadata:" << tagLength;
                return false;
            }

            QString tag;
            if (tagLength > 0) {
                QByteArray tagBytes(tagLength, '\0');
                if (!safeRead(data, pos, totalSize, tagBytes.data(), tagLength)) {
                    return false;
                }

                tag = QString::fromUtf8(tagBytes);
                if (!isValidTag(tag)) {
                    qWarning() << "Invalid tag in metadata:" << tag;
                    return false;
                }
            }

            metadata.tags.append(tag);
        }

        // 4. NEW: Read thumbnail data
        quint32 thumbnailLength = 0;
        if (!safeRead(data, pos, totalSize, &thumbnailLength, sizeof(thumbnailLength))) {
            return false;
        }

        if (thumbnailLength > MAX_THUMBNAIL_SIZE) {
            qWarning() << "Invalid thumbnail length in metadata:" << thumbnailLength;
            return false;
        }

        if (thumbnailLength > 0) {
            metadata.thumbnailData.resize(thumbnailLength);
            if (!safeRead(data, pos, totalSize, metadata.thumbnailData.data(), thumbnailLength)) {
                return false;
            }
            qDebug() << "Read thumbnail data from metadata:" << thumbnailLength << "bytes";
        }

        // Verify we consumed all data
        if (pos != totalSize) {
            qWarning() << "Metadata chunk has extra data. Expected" << pos << "bytes, got" << totalSize;
            return false;
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
