#ifndef ENCRYPTEDDATA_ENCRYPTIONWORKERS_H
#define ENCRYPTEDDATA_ENCRYPTIONWORKERS_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QByteArray>
#include <QPixmap>
#include <QMap>
#include <QAtomicInt>
#include <memory>
#include "encrypteddata_encryptedfilemetadata.h"

// Forward declarations
class EncryptedFileMetadata;

struct FileExportInfo {
    QString sourceFile;
    QString targetFile;
    QString originalFilename;
    qint64 fileSize;
    QString fileType;
};

struct DeletionItem {
    QString path;
    QString displayName;
    qint64 size;
    bool isFolder;

    DeletionItem(const QString& p, const QString& dn, qint64 s, bool folder)
        : path(p), displayName(dn), size(s), isFolder(folder) {}
};

struct DeletionResult {
    QStringList successfulItems;
    QStringList failedItems;
    qint64 totalSize;
    int totalFiles;

    DeletionResult() : totalSize(0), totalFiles(0) {}
};

// Worker class for encryption in separate thread
class EncryptionWorker : public QObject
{
    Q_OBJECT

public:
    //multiple files
    EncryptionWorker(const QStringList& sourceFiles, const QStringList& targetFiles,
                     const QByteArray& encryptionKey, const QString& username,
                     const QMap<QString, QPixmap>& videoThumbnails = QMap<QString, QPixmap>());
    //single file
    EncryptionWorker(const QString& sourceFile, const QString& targetFile,
                     const QByteArray& encryptionKey, const QString& username,
                     const QMap<QString, QPixmap>& videoThumbnails = QMap<QString, QPixmap>());

    ~EncryptionWorker();

    // Thread-safe getter functions with mutex protection
    QStringList getSourceFiles() const;
    QStringList getTargetFiles() const;

    void cancel();

public slots:
    void doEncryption();

signals:
    void progressUpdated(int percentage);

    // New signal for file-specific progress
    void fileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);

    // Backward compatible: keep old signal for single files
    void encryptionFinished(bool success, const QString& errorMessage = QString());

    // New signal for multiple files
    void multiFileEncryptionFinished(bool success, const QString& errorMessage,
                                     const QStringList& successfulFiles,
                                     const QStringList& failedFiles);

    void currentFileProgressUpdated(int percentage);

private:
    // Thread safety mutex for container access
    mutable QMutex m_containerMutex;
    
    // Member variables (protected by mutex)
    QStringList m_sourceFiles;
    QStringList m_targetFiles;
    
    // Other member variables
    QByteArray m_encryptionKey;
    QString m_username;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation
    QMap<QString, QImage> m_videoThumbnailImages; // Thread-safe QImage instead of QPixmap

    std::unique_ptr<EncryptedFileMetadata> m_metadataManager;  // Smart pointer for automatic cleanup
};

class DecryptionWorker : public QObject
{
    Q_OBJECT

public:
    DecryptionWorker(const QString& sourceFile, const QString& targetFile,
                     const QByteArray& encryptionKey);

    ~DecryptionWorker();

    // Thread-safe getter functions with mutex protection
    QString getSourceFile() const;
    QString getTargetFile() const;

    void cancel();

public slots:
    void doDecryption();

signals:
    void progressUpdated(int percentage);
    void decryptionFinished(bool success, const QString& errorMessage = QString());

private:
    // Thread safety mutex for member access
    mutable QMutex m_memberMutex;
    
    // Member variables (protected by mutex)
    QString m_sourceFile;
    QString m_targetFile;
    
    // Other member variables
    QByteArray m_encryptionKey;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation
    std::unique_ptr<EncryptedFileMetadata> m_metadataManager;  // Smart pointer for automatic cleanup
};

// Worker class for temporary decryption (for opening files)
class TempDecryptionWorker : public QObject
{
    Q_OBJECT

public:
    TempDecryptionWorker(const QString& sourceFile, const QString& targetFile,
                         const QByteArray& encryptionKey);

    ~TempDecryptionWorker();

    // Thread-safe getter functions with mutex protection
    QString getSourceFile() const;
    QString getTargetFile() const;

    void cancel();

public slots:
    void doDecryption();

signals:
    void progressUpdated(int percentage);
    void decryptionFinished(bool success, const QString& errorMessage = QString());

private:
    // Thread safety mutex for member access
    mutable QMutex m_memberMutex;
    
    // Member variables (protected by mutex)
    QString m_sourceFile;
    QString m_targetFile;
    
    // Other member variables
    QByteArray m_encryptionKey;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation
    std::unique_ptr<EncryptedFileMetadata> m_metadataManager;  // Smart pointer for automatic cleanup
};

// Batch decryption worker
class BatchDecryptionWorker : public QObject
{
    Q_OBJECT

public:
    BatchDecryptionWorker(const QList<FileExportInfo>& fileInfos,
                          const QByteArray& encryptionKey);
    ~BatchDecryptionWorker();

    void cancel();

public slots:
    void doDecryption();

signals:
    void overallProgressUpdated(int percentage);
    void fileProgressUpdated(int percentage);
    void fileStarted(int currentFile, int totalFiles, const QString& fileName);
    void batchDecryptionFinished(bool success, const QString& errorMessage,
                                 const QStringList& successfulFiles,
                                 const QStringList& failedFiles);

private:
    bool decryptSingleFile(const FileExportInfo& fileInfo,
                           qint64 currentTotalProcessed, qint64 totalSize);

    // Thread safety mutex for container access
    mutable QMutex m_containerMutex;
    
    // Member variables (protected by mutex)
    QList<FileExportInfo> m_fileInfos;
    
    // Other member variables
    QByteArray m_encryptionKey;
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation
    std::unique_ptr<EncryptedFileMetadata> m_metadataManager;  // Smart pointer for automatic cleanup
};

// Worker class for secure deletion
class SecureDeletionWorker : public QObject
{
    Q_OBJECT

public:
    SecureDeletionWorker(const QList<DeletionItem>& items);
    ~SecureDeletionWorker();

    void cancel();

public slots:
    void doSecureDeletion();

signals:
    void progressUpdated(int percentage);
    void currentItemChanged(const QString& itemName);
    void deletionFinished(bool success, const DeletionResult& result, const QString& errorMessage = QString());

private:
    bool secureDeleteSingleFile(const QString& filePath);
    bool secureDeleteFolder(const QString& folderPath, int& processedFiles, int totalFiles);
    QStringList enumerateFilesInFolder(const QString& folderPath);

    // Thread safety mutex for container access
    mutable QMutex m_containerMutex;
    
    // Member variables (protected by mutex)
    QList<DeletionItem> m_items;
    
    // Other member variables
    QAtomicInt m_cancelled;  // Using atomic for thread-safe cancellation
};

#endif // ENCRYPTEDDATA_ENCRYPTIONWORKERS_H
