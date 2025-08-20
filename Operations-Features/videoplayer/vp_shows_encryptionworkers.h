#ifndef VP_SHOWS_ENCRYPTIONWORKERS_H
#define VP_SHOWS_ENCRYPTIONWORKERS_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QStringList>
#include <QByteArray>
#include "vp_shows_metadata.h"
#include "vp_shows_tmdb.h"

// Forward declarations
class VP_ShowsMetadata;

// Worker class for encrypting TV show video files
class VP_ShowsEncryptionWorker : public QObject
{
    Q_OBJECT

public:
    // Constructor for multiple files
    VP_ShowsEncryptionWorker(const QStringList& sourceFiles, 
                            const QStringList& targetFiles,
                            const QString& showName,
                            const QByteArray& encryptionKey, 
                            const QString& username,
                            const QString& language = "English",
                            const QString& translation = "Dubbed");
    
    ~VP_ShowsEncryptionWorker();
    
    // Public member variables
    QStringList m_sourceFiles;
    QStringList m_targetFiles;
    QString m_showName;
    QString m_language;
    QString m_translation;
    
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
    QByteArray m_encryptionKey;
    QString m_username;
    bool m_cancelled;
    QMutex m_cancelMutex;
    VP_ShowsMetadata* m_metadataManager;
    VP_ShowsTMDB* m_tmdbManager;
    
    // TMDB data cache
    VP_ShowsTMDB::ShowInfo m_showInfo;
    bool m_tmdbDataAvailable;
    QString m_showImagePath;  // Path to encrypted show image
    
    bool encryptSingleFile(const QString& sourceFile, 
                          const QString& targetFile,
                          qint64 currentTotalProcessed, 
                          qint64 totalSize);
    
    // New helper methods
    bool fetchTMDBShowData();
    bool downloadAndEncryptShowImage(const QString& targetFolder);
    VP_ShowsMetadata::ShowMetadata createMetadataWithTMDB(const QString& filename);
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
    
    QString m_sourceFile;
    QString m_targetFile;
    
    void cancel();

public slots:
    void doDecryption();

signals:
    void progressUpdated(int percentage);
    void decryptionFinished(bool success, const QString& errorMessage = QString());

private:
    QByteArray m_encryptionKey;
    QString m_username;
    bool m_cancelled;
    QMutex m_cancelMutex;
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
    bool m_cancelled;
    QMutex m_cancelMutex;
    VP_ShowsMetadata* m_metadataManager;
    
    bool exportSingleFile(const ExportFileInfo& fileInfo, int& currentFileProgress);
};

#endif // VP_SHOWS_ENCRYPTIONWORKERS_H
