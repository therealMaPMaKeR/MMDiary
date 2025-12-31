#ifndef OPERATIONS_FILES_H
#define OPERATIONS_FILES_H

#include <QString>
#include <QStringList>
#include <QFile>
#include <QDir>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <QIODevice>
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

// =============================================================================
// PagefileBackedBuffer - Windows pagefile-backed memory buffer
// =============================================================================
// This class creates a memory region backed by the Windows system pagefile.
// The buffer lives in RAM when there's enough available memory, and Windows
// automatically pages it to disk (pagefile) when RAM is low.
//
// Key benefits:
// - No physical temp files created in the application's data directory
// - Windows manages RAM/pagefile decisions automatically and efficiently
// - Memory is securely cleared on destruction
// - Simpler than manual RAM checking and disk fallback logic
//
// Usage:
//   PagefileBackedBuffer buffer(fileSize);
//   if (buffer.isValid()) {
//       char* data = buffer.data();
//       // Use the buffer...
//   }
// =============================================================================
class PagefileBackedBuffer {
public:
    // Create a pagefile-backed buffer of the specified size
    explicit PagefileBackedBuffer(qint64 size);
    ~PagefileBackedBuffer();

    // Non-copyable, but movable
    PagefileBackedBuffer(const PagefileBackedBuffer&) = delete;
    PagefileBackedBuffer& operator=(const PagefileBackedBuffer&) = delete;
    PagefileBackedBuffer(PagefileBackedBuffer&& other) noexcept;
    PagefileBackedBuffer& operator=(PagefileBackedBuffer&& other) noexcept;

    // Check if the buffer was successfully allocated
    bool isValid() const { return m_data != nullptr; }

    // Get pointer to the buffer data
    char* data() { return m_data; }
    const char* data() const { return m_data; }

    // Get the size of the buffer
    qint64 size() const { return m_size; }

    // Securely clear the buffer contents (fills with zeros)
    void secureClear();

    // Get the last error message if allocation failed
    QString lastError() const { return m_lastError; }

    // Convert buffer contents to QByteArray (creates a copy)
    QByteArray toByteArray() const;

    // Write data to the buffer at the specified offset
    bool write(const char* source, qint64 sourceSize, qint64 offset = 0);
    bool write(const QByteArray& source, qint64 offset = 0);

private:
    void cleanup();

    char* m_data;
    qint64 m_size;
    QString m_lastError;

#ifdef Q_OS_WIN
    void* m_mappingHandle;  // HANDLE for the file mapping object
#endif
};

// =============================================================================
// PagefileBackedFile - QIODevice wrapper for PagefileBackedBuffer
// =============================================================================
// Provides a QIODevice interface to a PagefileBackedBuffer, allowing it to be
// used with Qt classes that expect QIODevice (like QTextStream).
// =============================================================================
class PagefileBackedFile : public QIODevice {
    Q_OBJECT

public:
    explicit PagefileBackedFile(qint64 size, QObject* parent = nullptr);
    ~PagefileBackedFile() override;

    // QIODevice interface
    bool open(OpenMode mode) override;
    void close() override;
    bool isSequential() const override { return false; }
    qint64 size() const override { return m_buffer ? m_buffer->size() : 0; }
    bool seek(qint64 pos) override;
    qint64 pos() const override { return m_position; }
    bool atEnd() const override;

    // Check if the underlying buffer is valid
    bool isValid() const { return m_buffer && m_buffer->isValid(); }

    // Get the last error message
    QString lastError() const { return m_buffer ? m_buffer->lastError() : "Buffer not created"; }

    // Get direct access to the underlying buffer
    PagefileBackedBuffer* buffer() { return m_buffer.get(); }
    const PagefileBackedBuffer* buffer() const { return m_buffer.get(); }

    // Securely clear the buffer contents
    void secureClear();

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;

private:
    std::unique_ptr<PagefileBackedBuffer> m_buffer;
    qint64 m_position;
    qint64 m_dataSize;  // Actual data written (may be less than buffer size)
};

// =============================================================================
// FileLocker - RAII class for file locking
// =============================================================================
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

// Encryption and file processing operations
bool encryptToTargetAndCleanup(QTemporaryFile* tempFile, const QString& targetPath, const QByteArray& encryptionKey);
bool decryptToTempAndProcess(const QString& encryptedFilePath, const QByteArray& encryptionKey,
                             std::function<bool(QFile*)> processFunction, FileType fileType = FileType::Generic);

// Pagefile-backed decryption operations (RAM with automatic pagefile fallback)
// These functions decrypt to a pagefile-backed memory buffer instead of a physical temp file.
// Windows automatically manages RAM usage and pages to disk when needed.
bool decryptToPagefileBuffer(const QString& encryptedFilePath, const QByteArray& encryptionKey,
                             std::unique_ptr<PagefileBackedBuffer>& outBuffer);
bool decryptToPagefileBufferAndProcess(const QString& encryptedFilePath, const QByteArray& encryptionKey,
                                       std::function<bool(PagefileBackedFile*)> processFunction);

// Read encrypted file content using pagefile-backed buffer
bool readEncryptedFileToBuffer(const QString& filePath, const QByteArray& encryptionKey,
                               QString& outContent);
bool readEncryptedFileLinesToBuffer(const QString& filePath, const QByteArray& encryptionKey,
                                    QStringList& outLines);

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
