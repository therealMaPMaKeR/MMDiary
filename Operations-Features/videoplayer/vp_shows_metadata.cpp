#include "vp_shows_metadata.h"
#include "CryptoUtils.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>

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
    if (showName.isEmpty() || showName.length() > MAX_SHOW_NAME_LENGTH) {
        return false;
    }
    
    // Check for invalid characters (basic check)
    if (showName.contains(QChar('\0'))) {
        return false;
    }
    
    return true;
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
    QByteArray chunk;
    QDataStream stream(&chunk, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Write metadata fields
    stream << metadata.filename;
    stream << metadata.showName;
    stream << metadata.season;
    stream << metadata.episode;
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
    
    // Read metadata fields
    stream >> metadata.filename;
    stream >> metadata.showName;
    stream >> metadata.season;
    stream >> metadata.episode;
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
    
    // Create fixed-size buffer
    QByteArray fixedSizeBuffer(METADATA_RESERVED_SIZE, '\0');
    
    // Write size and encrypted data
    QDataStream stream(&fixedSizeBuffer, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Write magic number for validation
    stream << quint32(0x56504D44); // "VPMD" in hex
    
    // Write the size of encrypted metadata
    stream << qint32(encryptedMetadata.size());
    
    // Write the encrypted metadata
    stream.writeRawData(encryptedMetadata.constData(), encryptedMetadata.size());
    
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
