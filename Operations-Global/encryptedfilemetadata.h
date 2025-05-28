#ifndef ENCRYPTEDFILEMETADATA_H
#define ENCRYPTEDFILEMETADATA_H

#include "qfiledevice.h"
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QPixmap>
#include "constants.h"

class EncryptedFileMetadata
{
public:
    // Metadata structure for encrypted files
    struct FileMetadata {
        QString filename;
        QString category;
        QStringList tags;
        QByteArray thumbnailData; // NEW: Embedded thumbnail data (compressed JPEG)

        // Default constructor
        FileMetadata() = default;

        // Constructor with filename only (for initial encryption)
        explicit FileMetadata(const QString& fname) : filename(fname) {}

        // Full constructor without thumbnail
        FileMetadata(const QString& fname, const QString& cat, const QStringList& tagList)
            : filename(fname), category(cat), tags(tagList) {}

        // NEW: Full constructor with thumbnail
        FileMetadata(const QString& fname, const QString& cat, const QStringList& tagList, const QByteArray& thumb)
            : filename(fname), category(cat), tags(tagList), thumbnailData(thumb) {}
    };

    // Constructor - takes encryption key and username for operations
    explicit EncryptedFileMetadata(const QByteArray& encryptionKey, const QString& username);

    // Destructor
    ~EncryptedFileMetadata();

    // Core file operations
    bool writeMetadataToFile(const QString& filePath, const FileMetadata& metadata);
    bool readMetadataFromFile(const QString& filePath, FileMetadata& metadata);
    bool updateMetadataInFile(const QString& filePath, const FileMetadata& newMetadata);

    // Convenience methods
    QString getFilenameFromFile(const QString& filePath);
    bool hasNewFormat(const QString& filePath);

    // Create metadata chunk for use during encryption (without writing to file)
    QByteArray createEncryptedMetadataChunk(const FileMetadata& metadata);

    // NEW: Thumbnail handling methods
    QPixmap getThumbnailFromFile(const QString& filePath, int size = 64);
    bool hasThumbnail(const QString& filePath);

    // NEW: Static thumbnail utility methods
    static QByteArray compressThumbnail(const QPixmap& thumbnail, int quality = 85);
    static QPixmap decompressThumbnail(const QByteArray& thumbnailData);
    static QPixmap createThumbnailFromImage(const QString& imagePath, int size = 64);

    // Static validation methods
    static bool isValidCategory(const QString& category);
    static bool isValidTag(const QString& tag);
    static bool isValidTagList(const QStringList& tags);
    static bool isValidFilename(const QString& filename);

    // Constants for validation limits
    static const int MAX_TAGS = 50;
    static const int MAX_CATEGORY_LENGTH = 50;
    static const int MAX_TAG_LENGTH = 50;
    static const int MAX_THUMBNAIL_SIZE = 15360; // 15KB max for thumbnail data

    static QPixmap createSquareThumbnail(const QPixmap& sourcePixmap, int size = 64);

private:
    QByteArray m_encryptionKey;
    QString m_username;

    // Internal metadata chunk operations
    QByteArray createMetadataChunk(const FileMetadata& metadata);
    bool parseMetadataChunk(const QByteArray& chunk, FileMetadata& metadata);

    // UPDATED: Fixed-size metadata operations
    QByteArray createFixedSizeEncryptedMetadata(const FileMetadata& metadata);
    bool readFixedSizeEncryptedMetadata(QIODevice* file, FileMetadata& metadata);
    bool writeFixedSizeEncryptedMetadata(QIODevice* file, const FileMetadata& metadata);

    // File I/O helpers
    bool readMetadataFromOpenFile(QIODevice* file, FileMetadata& metadata);
    bool writeMetadataToOpenFile(QIODevice* file, const FileMetadata& metadata);

    // Safety helpers
    bool safeRead(const char* data, int& pos, int totalSize, void* dest, int size);
};

#endif // ENCRYPTEDFILEMETADATA_H
