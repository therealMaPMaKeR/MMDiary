#ifndef VP_SHOWS_METADATA_H
#define VP_SHOWS_METADATA_H

#include <QString>
#include <QByteArray>
#include <QDateTime>

class VP_ShowsMetadata
{
public:
    // Metadata structure for encrypted TV show video files
    struct ShowMetadata {
        QString filename;       // Original filename with extension
        QString showName;       // Name of the TV show (from folder name)
        QString season;         // Season number/name (leave empty for now)
        QString episode;        // Episode number/name (leave empty for now)
        QDateTime encryptionDateTime; // When the file was encrypted
        
        // Default constructor
        ShowMetadata() = default;
        
        // Constructor with all fields
        ShowMetadata(const QString& fname, const QString& show, 
                    const QString& seas = QString(), const QString& ep = QString())
            : filename(fname), showName(show), season(seas), episode(ep) 
        {
            encryptionDateTime = QDateTime::currentDateTime();
        }
    };
    
    // Constructor - takes encryption key and username for operations
    explicit VP_ShowsMetadata(const QByteArray& encryptionKey, const QString& username);
    
    // Destructor
    ~VP_ShowsMetadata();
    
    // Core file operations
    bool writeMetadataToFile(const QString& filePath, const ShowMetadata& metadata);
    bool readMetadataFromFile(const QString& filePath, ShowMetadata& metadata);
    bool updateMetadataInFile(const QString& filePath, const ShowMetadata& newMetadata);
    
    // Convenience methods
    QString getFilenameFromFile(const QString& filePath);
    QString getShowNameFromFile(const QString& filePath);
    
    // Create metadata chunk for use during encryption (without writing to file)
    QByteArray createEncryptedMetadataChunk(const ShowMetadata& metadata);
    
    // Static validation methods
    static bool isValidShowName(const QString& showName);
    static bool isValidFilename(const QString& filename);
    
    // Constants for validation limits
    static const int MAX_SHOW_NAME_LENGTH = 100;
    static const int MAX_FILENAME_LENGTH = 255;
    static const int MAX_SEASON_LENGTH = 50;
    static const int MAX_EPISODE_LENGTH = 100;
    
    // Fixed metadata size (same as encrypted data feature for consistency)
    static const int METADATA_RESERVED_SIZE = 51200; // 50KB reserved for metadata

    bool readFixedSizeEncryptedMetadata(QIODevice* file, ShowMetadata& metadata);
    bool writeFixedSizeEncryptedMetadata(QIODevice* file, const ShowMetadata& metadata);

private:
    QByteArray m_encryptionKey;
    QString m_username;
    
    // Internal metadata chunk operations
    QByteArray createMetadataChunk(const ShowMetadata& metadata);
    bool parseMetadataChunk(const QByteArray& chunk, ShowMetadata& metadata);
    
    // Fixed-size metadata operations
    QByteArray createFixedSizeEncryptedMetadata(const ShowMetadata& metadata);
    
    // Safety helpers
    bool safeRead(const char* data, int& pos, int totalSize, void* dest, int size);
};

#endif // VP_SHOWS_METADATA_H
