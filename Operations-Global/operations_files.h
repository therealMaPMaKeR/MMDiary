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
bool secureDelete(const QString& filePath, int passes = 3);

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
