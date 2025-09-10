#ifndef VP_SHOWS_ENCRYPTIONWORKERS_H
#define VP_SHOWS_ENCRYPTIONWORKERS_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QReadWriteLock>
#include <QStringList>
#include <QByteArray>
#include <QMap>
#include <QPixmap>
#include <QPointer>
#include <QAtomicInt>
#include <memory>
#include "vp_shows_metadata.h"
#include "vp_shows_tmdb.h"

// Forward declarations
class VP_ShowsMetadata;

// Worker class for encrypting TV show video files
class VP_ShowsEncryptionWorker : public QObject
{
    Q_OBJECT

public:
    // Enum for parsing mode
    enum ParseMode {
        ParseFromFolder = 0,
        ParseFromFile = 1
    };
    
    // Constructor for multiple files
    VP_ShowsEncryptionWorker(const QStringList& sourceFiles, 
    const QStringList& targetFiles,
    const QString& showName,
    const QByteArray& encryptionKey, 
    const QString& username,
    const QString& language = "English",
    const QString& translation = "Dubbed",
                             bool useTMDB = true,
                             const QPixmap& customPoster = QPixmap(),
                             const QString& customDescription = QString(),
                             ParseMode parseMode = ParseFromFile,
                             int showId = 0);
    
    ~VP_ShowsEncryptionWorker();
    
    // Thread-safe getter methods
    QStringList getSourceFiles() const;
    QStringList getTargetFiles() const;
    QString getShowName() const;
    QString getLanguage() const;
    QString getTranslation() const;
    
    void cancel();

public slots:
    void doEncryption();

signals:
    void progressUpdated(int percentage);
    
    // Signal for file-specific progress
    void fileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);
    
    // Signal for current file progress
    void currentFileProgressUpdated(int percentage);
    
    // Signal when encryption is complete
    void encryptionFinished(bool success, const QString& errorMessage,
                           const QStringList& successfulFiles,
                           const QStringList& failedFiles);

private:
    // Member variables that were previously public (now protected by mutex)
    QStringList m_sourceFiles;
    QStringList m_targetFiles;
    QString m_showName;
    QString m_language;
    QString m_translation;
    
    // Other member variables
    QByteArray m_encryptionKey;
    QString m_username;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation (0=false, 1=true)
    
    // Custom poster and description
    bool m_useTMDB;
    QPixmap m_customPoster;
    QString m_customDescription;
    
    // Parsing mode
    ParseMode m_parseMode;
    
    // TMDB show ID (0 if not set)
    int m_showId;
    
    // Thread-safe pointer management
    mutable QMutex m_pointerMutex;  // Protects pointer access
    VP_ShowsMetadata* m_metadataManager;
    VP_ShowsTMDB* m_tmdbManager;
    
    // TMDB data cache - protected by read-write lock for better performance
    mutable QReadWriteLock m_dataLock;
    VP_ShowsTMDB::ShowInfo m_showInfo;
    bool m_tmdbDataAvailable;
    QString m_showImagePath;  // Path to encrypted show image
    QMap<int, VP_ShowsTMDB::EpisodeMapping> m_episodeMap;  // Absolute number -> season/episode mapping
    QStringList m_movieTitles;  // List of movie titles for content detection
    QStringList m_ovaTitles;    // List of OVA/special titles for content detection
    
    // Track existing episodes to detect duplicates - protected by mutex
    mutable QMutex m_episodesMutex;
    QSet<QString> m_existingEpisodes;  // Set of "S##E##" strings from existing files
    QSet<QString> m_processedEpisodes; // Episodes processed in current batch
    
    bool encryptSingleFile(const QString& sourceFile, 
                          const QString& targetFile,
                          qint64 currentTotalProcessed, 
                          qint64 totalSize);
    
    // New helper methods
    bool fetchTMDBShowData();
    bool downloadAndEncryptShowImage(const QString& targetFolder);
    bool saveCustomShowData(const QString& targetFolder);  // Save custom poster and description
    VP_ShowsMetadata::ShowMetadata createMetadataWithTMDB(const QString& filename, const QString& folderName = QString());
    bool checkForDuplicateEpisode(int season, int episode, const QString& language, const QString& translation);
    void loadExistingEpisodes();
};

// Worker class for decrypting TV show video files (for playback)
class VP_ShowsDecryptionWorker : public QObject
{
    Q_OBJECT

public:
    VP_ShowsDecryptionWorker(const QString& sourceFile, 
                            const QString& targetFile,
                            const QByteArray& encryptionKey,
                            const QString& username);
    
    ~VP_ShowsDecryptionWorker();
    
    // Thread-safe getter methods
    QString getSourceFile() const;
    QString getTargetFile() const;
    
    void cancel();

public slots:
    void doDecryption();

signals:
    void progressUpdated(int percentage);
    void decryptionFinished(bool success, const QString& errorMessage = QString());

private:
    // Member variables that were previously public (now protected by mutex)
    QString m_sourceFile;
    QString m_targetFile;
    
    // Other member variables
    QByteArray m_encryptionKey;
    QString m_username;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation (0=false, 1=true)
    mutable QMutex m_pointerMutex;  // Protects pointer access
    VP_ShowsMetadata* m_metadataManager;
};

// Worker class for exporting (decrypting) entire TV shows
class VP_ShowsExportWorker : public QObject
{
    Q_OBJECT

public:
    struct ExportFileInfo {
        QString sourceFile;
        QString targetFile;
        QString displayName;
        qint64 fileSize;
    };
    
    VP_ShowsExportWorker(const QList<ExportFileInfo>& files,
                        const QByteArray& encryptionKey,
                        const QString& username);
    
    ~VP_ShowsExportWorker();
    
    void cancel();

public slots:
    void doExport();

signals:
    // Overall progress (0-100)
    void overallProgressUpdated(int percentage);
    
    // Current file progress (0-100)
    void currentFileProgressUpdated(int percentage);
    
    // File progress (current file index, total files, current filename)
    void fileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);
    
    // Warning signal for duplicate files or other non-fatal issues
    void fileExportWarning(const QString& fileName, const QString& warningMessage);
    
    // Export complete signal
    void exportFinished(bool success, const QString& errorMessage,
                       const QStringList& successfulFiles,
                       const QStringList& failedFiles);

private:
    QList<ExportFileInfo> m_files;
    QByteArray m_encryptionKey;
    QString m_username;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation (0=false, 1=true)
    mutable QMutex m_pointerMutex;  // Protects pointer access
    VP_ShowsMetadata* m_metadataManager;
    
    bool exportSingleFile(const ExportFileInfo& fileInfo, int& currentFileProgress);
};

#endif // VP_SHOWS_ENCRYPTIONWORKERS_H
