#include "encryptedfilemetadata.h"
#include "inputvalidation.h"
#include "CryptoUtils.h"
#include <QFile>
#include <QDebug>
#include <QIODevice>
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
// Static Validation Methods
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
// Core File Operations
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
    // This is a more complex operation that requires rewriting the entire file
    // For now, we'll implement a simple approach that reads all content and rewrites it

    QFile originalFile(filePath);
    if (!originalFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for metadata update:" << filePath;
        return false;
    }

    // Read current metadata to get the size
    quint32 currentMetadataSize = 0;
    if (originalFile.read(reinterpret_cast<char*>(&currentMetadataSize), sizeof(currentMetadataSize)) != sizeof(currentMetadataSize)) {
        qWarning() << "Failed to read current metadata size";
        originalFile.close();
        return false;
    }

    if (currentMetadataSize == 0 || currentMetadataSize > MAX_ENCRYPTED_METADATA_SIZE) {
        qWarning() << "Invalid current metadata size:" << currentMetadataSize;
        originalFile.close();
        return false;
    }

    // Skip current metadata
    originalFile.seek(sizeof(currentMetadataSize) + currentMetadataSize);

    // Read remaining file content (the actual data chunks)
    QByteArray remainingContent = originalFile.readAll();
    originalFile.close();

    // Create new metadata chunk
    QByteArray newEncryptedMetadata = createEncryptedMetadataChunk(newMetadata);
    if (newEncryptedMetadata.isEmpty()) {
        qWarning() << "Failed to create new metadata chunk";
        return false;
    }

    // Rewrite the file with new metadata
    QFile newFile(filePath);
    if (!newFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for rewriting with new metadata:" << filePath;
        return false;
    }

    // Write new metadata size and data
    quint32 newMetadataSize = static_cast<quint32>(newEncryptedMetadata.size());
    newFile.write(reinterpret_cast<const char*>(&newMetadataSize), sizeof(newMetadataSize));
    newFile.write(newEncryptedMetadata);

    // Write original content
    newFile.write(remainingContent);
    newFile.close();

    qDebug() << "Successfully updated metadata for file:" << filePath;
    return true;
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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    // Try to read metadata size
    quint32 metadataSize = 0;
    bool hasNewFormat = (file.read(reinterpret_cast<char*>(&metadataSize), sizeof(metadataSize)) == sizeof(metadataSize));

    if (hasNewFormat) {
        // Additional check: metadata size should be reasonable
        hasNewFormat = (metadataSize > 0 && metadataSize <= MAX_ENCRYPTED_METADATA_SIZE);
    }

    file.close();
    return hasNewFormat;
}

QByteArray EncryptedFileMetadata::createEncryptedMetadataChunk(const FileMetadata& metadata)
{
    QByteArray metadataChunk = createMetadataChunk(metadata);
    if (metadataChunk.isEmpty()) {
        return QByteArray();
    }

    // Encrypt the metadata chunk
    QByteArray encryptedMetadata = CryptoUtils::Encryption_EncryptBArray(
        m_encryptionKey, metadataChunk, m_username);

    if (encryptedMetadata.isEmpty()) {
        qWarning() << "Failed to encrypt metadata chunk";
        return QByteArray();
    }

    return encryptedMetadata;
}

// ============================================================================
// Internal Metadata Chunk Operations
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

    // Check size limit
    if (chunk.size() > MAX_METADATA_SIZE) {
        qWarning() << "Metadata chunk too large:" << chunk.size() << "bytes (max:" << MAX_METADATA_SIZE << ")";
        return QByteArray();
    }

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
// File I/O Helpers
// ============================================================================

bool EncryptedFileMetadata::readMetadataFromOpenFile(QIODevice* file, FileMetadata& metadata)
{
    if (!file || !file->isReadable()) {
        qWarning() << "Invalid file for reading metadata";
        return false;
    }

    try {
        // Read metadata size
        quint32 metadataSize = 0;
        if (file->read(reinterpret_cast<char*>(&metadataSize), sizeof(metadataSize)) != sizeof(metadataSize)) {
            qWarning() << "Failed to read metadata size";
            return false;
        }

        // Validate metadata size
        if (metadataSize == 0 || metadataSize > MAX_ENCRYPTED_METADATA_SIZE) {
            qWarning() << "Invalid metadata size:" << metadataSize;
            return false;
        }

        // Read encrypted metadata
        QByteArray encryptedMetadata = file->read(metadataSize);
        if (encryptedMetadata.size() != static_cast<int>(metadataSize)) {
            qWarning() << "Failed to read complete encrypted metadata";
            return false;
        }

        // Decrypt metadata
        QByteArray metadataChunk = CryptoUtils::Encryption_DecryptBArray(m_encryptionKey, encryptedMetadata);
        if (metadataChunk.isEmpty()) {
            qWarning() << "Failed to decrypt metadata chunk";
            return false;
        }

        // Parse metadata
        return parseMetadataChunk(metadataChunk, metadata);

    } catch (const std::exception& e) {
        qWarning() << "Exception reading metadata from file:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception reading metadata from file";
        return false;
    }
}

bool EncryptedFileMetadata::writeMetadataToOpenFile(QIODevice* file, const FileMetadata& metadata)
{
    if (!file || !file->isWritable()) {
        qWarning() << "Invalid file for writing metadata";
        return false;
    }

    try {
        // Create encrypted metadata chunk
        QByteArray encryptedMetadata = createEncryptedMetadataChunk(metadata);
        if (encryptedMetadata.isEmpty()) {
            qWarning() << "Failed to create encrypted metadata chunk";
            return false;
        }

        // Write metadata size
        quint32 metadataSize = static_cast<quint32>(encryptedMetadata.size());
        if (file->write(reinterpret_cast<const char*>(&metadataSize), sizeof(metadataSize)) != sizeof(metadataSize)) {
            qWarning() << "Failed to write metadata size";
            return false;
        }

        // Write encrypted metadata
        if (file->write(encryptedMetadata) != encryptedMetadata.size()) {
            qWarning() << "Failed to write encrypted metadata";
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception writing metadata to file:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception writing metadata to file";
        return false;
    }
}

// ============================================================================
// Safety Helpers
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
