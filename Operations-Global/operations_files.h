#ifndef OPERATIONS_FILES_H
#define OPERATIONS_FILES_H

#include <QString>
#include <QStringList>
#include <QFile>
#include <QDir>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <functional>
#include <memory>
#include <QMutex>
#include <QHash>
#include <QStorageInfo>

namespace OperationsFiles {

// File type enum used for validation and processing
enum class FileType {
    Generic,
    Diary,
    Password,
    TaskList
};

// Default secure permissions - owner read/write only
extern const QFile::Permissions DEFAULT_FILE_PERMISSIONS;
extern const QFile::Permissions DEFAULT_DIR_PERMISSIONS;

// Size limits for operations_files encryption functions
// These functions are designed for text-based files (diary, tasklist, settings)
// For larger files (videos, bulk data), use dedicated encryption worker classes
extern const qint64 MAX_ENCRYPTED_FILE_SIZE; // 50MB limit for encrypted files
extern const qint64 MAX_CONTENT_SIZE; // 50MB limit for content to encrypt

// Resource limits for temp directory management
extern const qint64 MAX_TEMP_DIRECTORY_SIZE; // 20GB default limit
extern const qint64 TEMP_CLEANUP_THRESHOLD; // 80% of max size (16GB)
extern const qint64 MIN_DISK_SPACE_REQUIRED; // 1GB minimum free space after operations

// RAII class for file locking to prevent concurrent access
class FileLocker {
public:
    FileLocker(const QString& filePath);
    ~FileLocker();
    
    bool isLocked() const { return m_locked; }
    bool tryLock(int timeoutMs = 5000);
    void unlock();
    
private:
    QString m_filePath;
    bool m_locked;
#ifdef Q_OS_WIN
    void* m_fileHandle;  // HANDLE type
#else
    int m_fd;
#endif
    
    // Static map to track file locks across the application
    static QMutex s_lockMapMutex;
    static QHash<QString, FileLocker*> s_activeLocks;
};

// RAII class for temporary file management
class TempFileCleaner {
public:
    TempFileCleaner(const QString& filePath, FileType fileType = FileType::Generic);
    ~TempFileCleaner();

    void disableCleanup();
    void cleanup();

private:
    QString m_filePath;
    FileType m_fileType;
    bool m_cleanup;
};

bool cleanupAllUserTempFolders(); // fallback function, triggered on app launch.

// Resource management for temp directory
qint64 getTempDirectorySize(const QString& username = QString()); // Get current size of temp directory
qint64 getAvailableDiskSpace(const QString& path = QString()); // Get available disk space for path
bool checkTempDirectoryLimits(qint64 requiredSize, const QString& username = QString()); // Check if we can create new temp file
bool cleanupOldTempFiles(const QString& username = QString(), qint64 targetSize = 0); // Cleanup old temp files to free space
bool canDecryptToTemp(const QString& encryptedFilePath, const QString& username = QString()); // Check if encrypted file can be decrypted to temp

void setUsername(const QString& username);
QString getUsername();

// Helper function to verify that permissions were correctly applied
bool verifyPermissions(const QString& path, QFile::Permissions expectedPermissions);

// Directory operations
bool ensureDirectoryExists(const QString& dirPath, QFile::Permissions permissions = QFile::Permissions());
bool createHierarchicalDirectory(const QStringList& pathComponents, const QString& basePath);

// File creation and opening
bool createSecureFile(const QString& filePath, QFile::Permissions permissions = QFile::Permissions());
std::unique_ptr<QFile> openSecureFile(const QString& filePath, QIODevice::OpenMode mode);

// Temporary file operations
std::unique_ptr<QTemporaryFile> createTempFile(const QString& baseFileTemplate = QString(), bool autoRemove = false);
bool secureDelete(const QString& filePath, int passes = 3, bool allowExternalFiles = false);

// Encryption and file processing operations
bool encryptToTargetAndCleanup(QTemporaryFile* tempFile, const QString& targetPath, const QByteArray& encryptionKey);
bool decryptToTempAndProcess(const QString& encryptedFilePath, const QByteArray& encryptionKey,
                             std::function<bool(QFile*)> processFunction, FileType fileType = FileType::Generic);

// Common file operations
bool readEncryptedFileLines(const QString& filePath, const QByteArray& encryptionKey, QStringList& outLines);
bool writeEncryptedFileLines(const QString& filePath, const QByteArray& encryptionKey, const QStringList& lines);
bool deleteFileAndCleanEmptyDirs(const QString& filePath, const QStringList& hierarchyLevels, const QString& basePath);
bool readEncryptedFile(const QString& filePath, const QByteArray& encryptionKey, QString& outContent);
bool writeEncryptedFile(const QString& filePath, const QByteArray& encryptionKey, const QString& content);
bool processEncryptedFile(const QString& filePath, const QByteArray& encryptionKey,
                          std::function<bool(QString&)> processFunction);
bool searchEncryptedFile(const QString& filePath, const QByteArray& encryptionKey,
                         const QRegularExpression& searchPattern, QStringList& results);

// Validation and security operations
bool validateFilePath(const QString& filePath, FileType fileType, const QByteArray& encryptionKey);
bool isWithinAllowedDirectory(const QString& filePath, const QString& baseDirectory);
QString sanitizePath(const QString& path);
QString enableWindowsLongPath(const QString& path);
bool validatePathLength(const QString& path);
QString securePathJoin(const QString& basePath, const QString& component);
QString normalizePathForComparison(const QString& path);

// Security validation  
bool isWeakEncryptionKey(const QByteArray& key);

// Task list operations
bool readTasklistFile(const QString& filePath, const QByteArray& encryptionKey, QStringList& taskLines);
bool writeTasklistFile(const QString& filePath, const QByteArray& encryptionKey, const QStringList& taskLines);
bool findTaskEntry(const QString& filePath, const QByteArray& encryptionKey, const QString& taskName, QString& taskEntry);
bool addTaskEntry(const QString& filePath, const QByteArray& encryptionKey, const QString& taskEntry);
bool modifyTaskEntry(const QString& filePath, const QByteArray& encryptionKey,
                     const QString& taskName, const QString& newTaskEntry);
bool removeTaskEntry(const QString& filePath, const QByteArray& encryptionKey, const QString& taskName);
bool createNewTasklistFile(const QString& filePath, const QByteArray& encryptionKey);

} // namespace OperationsFiles

#endif // OPERATIONS_FILES_H
