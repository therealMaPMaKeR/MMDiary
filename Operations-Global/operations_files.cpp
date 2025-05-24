#include "operations_files.h"
#include "inputvalidation.h"
#include "Operations-Global/CryptoUtils.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QDebug>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QMutex>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <functional>
#include <memory>

// For Windows-specific APIs
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace OperationsFiles {

// Default secure permissions - owner read/write only
const QFile::Permissions DEFAULT_FILE_PERMISSIONS = QFile::ReadOwner | QFile::WriteOwner;
const QFile::Permissions DEFAULT_DIR_PERMISSIONS = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;

// Static data for cleanup management
static QList<QString> s_pendingDeletions;
static QMutex s_pendingMutex;  // For thread safety
static bool s_cleanupScheduled = false;

// Forward declarations of internal functions
void performAsyncCleanup();
bool quickDelete(const QString& filePath);

// RAII class for temporary file management
TempFileCleaner::TempFileCleaner(const QString& filePath, FileType fileType)
    : m_filePath(filePath), m_fileType(fileType), m_cleanup(true) {
    qDebug() << "TempFileCleaner created for:" << filePath;
}

TempFileCleaner::~TempFileCleaner() {
    qDebug() << "TempFileCleaner destructor called for:" << m_filePath;
    cleanup();
}

void TempFileCleaner::disableCleanup() {
    qDebug() << "Cleanup disabled for:" << m_filePath;
    m_cleanup = false;
}

void TempFileCleaner::cleanup() {
    if (m_cleanup && !m_filePath.isEmpty()) {
        qDebug() << "Performing cleanup for:" << m_filePath;

        // Try to delete the file
        bool deleted = quickDelete(m_filePath);

        // Only clear the path if deletion was successful or scheduled
        if (deleted || s_pendingDeletions.contains(m_filePath)) {
            m_filePath.clear();
            m_cleanup = false;
        } else {
            // If deletion failed and not in pending list, keep the path for possible retry
            qWarning() << "Cleanup failed for:" << m_filePath;
            // Add to pending deletions to ensure it gets cleaned up later
            s_pendingMutex.lock();
            if (!s_pendingDeletions.contains(m_filePath)) {
                s_pendingDeletions.append(m_filePath);
                qDebug() << "Added to pending deletion list:" << m_filePath;
            }
            s_pendingMutex.unlock();
        }
    } else if (m_cleanup && m_filePath.isEmpty()) {
        qDebug() << "Cleanup called with empty path";
    } else {
        qDebug() << "Cleanup skipped (disabled) for:" << m_filePath;
    }
}

// A lightweight function for quick file deletion without blocking
bool quickDelete(const QString& filePath) {
    qDebug() << "=== quickDelete called for:" << filePath << "===";

    if (!QFile::exists(filePath)) {
        qDebug() << "File doesn't exist in quickDelete";
        return true;  // Already gone
    }

    // First try standard removal - no delay
    qDebug() << "Trying QFile::remove()...";
    if (QFile::remove(filePath)) {
        qDebug() << "Standard deletion successful for:" << filePath;
        return true;
    }
    qDebug() << "QFile::remove() failed";

// If that failed, use Windows API as it's more effective for locked files
#ifdef Q_OS_WIN
    qDebug() << "Trying Windows API deletion...";
    HANDLE fileHandle = CreateFileW(
        reinterpret_cast<const wchar_t*>(filePath.utf16()),
        DELETE,
        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_DELETE_ON_CLOSE,
        NULL
        );

    if (fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(fileHandle);
        qDebug() << "Windows API deletion scheduled for:" << filePath;
        return true;
    }
    qDebug() << "Windows API deletion also failed";
#endif

    // If we couldn't delete, add to pending list (safely)
    qDebug() << "Adding to pending deletion list";
    s_pendingMutex.lock();
    if (!s_pendingDeletions.contains(filePath)) {
        s_pendingDeletions.append(filePath);
        qDebug() << "Added to pending deletion list:" << filePath;

        // Only schedule an async cleanup if one isn't already running
        if (!s_cleanupScheduled) {
            s_cleanupScheduled = true;
            qDebug() << "Scheduling async cleanup";

            // Schedule asynchronous cleanup to avoid blocking the current operation
            QFuture<void> future = QtConcurrent::run([]() {
                // Give the current operation time to complete and release locks
                QThread::msleep(100);
                performAsyncCleanup();
            });
        }
    }
    s_pendingMutex.unlock();

    return false;
}

// Non-blocking cleanup function that runs in a separate thread
void performAsyncCleanup() {
    QMutexLocker locker(&s_pendingMutex);

    if (s_pendingDeletions.isEmpty()) {
        s_cleanupScheduled = false;
        return;
    }

    qDebug() << "Performing async cleanup of" << s_pendingDeletions.size() << "files";

    QList<QString> stillPending;

    for (const QString& filePath : s_pendingDeletions) {
        if (!QFile::exists(filePath)) {
            qDebug() << "Pending file no longer exists:" << filePath;
            continue; // Already gone
        }

        // Try standard delete first
        if (QFile::remove(filePath)) {
            qDebug() << "Async cleanup: Successfully deleted file:" << filePath;
            continue;
        }

// Try Windows API for stubborn files
#ifdef Q_OS_WIN
        HANDLE fileHandle = CreateFileW(
            reinterpret_cast<const wchar_t*>(filePath.utf16()),
            DELETE,
            FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_DELETE_ON_CLOSE,
            NULL
            );

        if (fileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(fileHandle);
            qDebug() << "Async cleanup: File marked for deletion on close:" << filePath;
            continue;
        }
#endif

        // Still locked, keep in pending list
        qDebug() << "Async cleanup: File still locked:" << filePath;
        stillPending.append(filePath);
    }

    s_pendingDeletions = stillPending;

    // If we still have pending files, schedule another cleanup pass
    if (!s_pendingDeletions.isEmpty()) {
        qDebug() << "Scheduling another cleanup pass for" << s_pendingDeletions.size() << "files";
        // Schedule another cleanup pass later
        QFuture<void> future = QtConcurrent::run([]() {
            QThread::msleep(1000); // Wait longer between batches
            performAsyncCleanup();
        });
    } else {
        qDebug() << "Async cleanup complete - no files remaining";
        s_cleanupScheduled = false;
    }
}

static QString g_username = "default";
void setUsername(const QString& username) {
    if (!username.isEmpty()) {
        g_username = username;
    }
}

QString getUsername() {
    return g_username;
}

// Verify that permissions were correctly applied
bool verifyPermissions(const QString& path, QFile::Permissions expectedPermissions) {

    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        qWarning() << "Cannot verify permissions: path does not exist:" << path;
        return false;
    }

    QFile::Permissions actualPermissions = fileInfo.permissions();

    // Check if all expected permissions are set
    bool permissionsMatch = (actualPermissions & expectedPermissions) == expectedPermissions;

    if (!permissionsMatch) {
        qWarning() << "Permission verification failed for:" << path
                   << "Expected:" << expectedPermissions
                   << "Actual:" << actualPermissions;
    }

    return permissionsMatch;
}

// Directory Operations
bool ensureDirectoryExists(const QString& dirPath, QFile::Permissions permissions) {
    // Default to secure permissions if none specified
    if (permissions == QFile::Permissions()) {
        permissions = DEFAULT_DIR_PERMISSIONS;
    }

    // First validate the path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(dirPath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid directory path: " << result.errorMessage;
        return false;
    }

    QDir dir(dirPath);
    if (dir.exists()) {
        // Directory exists, verify permissions
        if (!verifyPermissions(dirPath, permissions)) {
            // Try to set correct permissions if they don't match
            return QFile::setPermissions(dirPath, permissions);
        }
        return true;
    }

    bool created = dir.mkpath(".");
    if (created) {
        // Set directory permissions
        bool permissionsSet = QFile::setPermissions(dirPath, permissions);

        // Verify the permissions were applied correctly
        if (permissionsSet) {
            permissionsSet = verifyPermissions(dirPath, permissions);
        }

        if (!permissionsSet) {
            qWarning() << "Failed to set permissions on directory:" << dirPath;
        }

        // Re-validate path after creation to ensure no manipulation occurred
        InputValidation::ValidationResult postResult =
            InputValidation::validateInput(dirPath, InputValidation::InputType::FilePath);
        if (!postResult.isValid) {
            qWarning() << "Post-creation path validation failed: " << postResult.errorMessage;
            return false;
        }

        return permissionsSet;
    }
    return false;
}

// Creates a hierarchical directory structure (e.g., year/month/day)
bool createHierarchicalDirectory(const QStringList& pathComponents, const QString& basePath) {
    // Validate base path
    InputValidation::ValidationResult baseResult =
        InputValidation::validateInput(basePath, InputValidation::InputType::FilePath);
    if (!baseResult.isValid) {
        qWarning() << "Invalid base path for hierarchical directory: " << baseResult.errorMessage;
        return false;
    }

    QString currentPath = basePath;

    // Ensure the base directory exists with secure permissions
    QDir baseDir(currentPath);
    if (!baseDir.exists()) {
        bool created = baseDir.mkpath(".");
        if (!created) {
            qWarning() << "Failed to create base directory: " << currentPath;
            return false;
        }

        // Set secure permissions on base directory
        if (!QFile::setPermissions(currentPath, DEFAULT_DIR_PERMISSIONS)) {
            qWarning() << "Failed to set permissions on base directory: " << currentPath;
            return false;
        }

        // Verify permissions
        if (!verifyPermissions(currentPath, DEFAULT_DIR_PERMISSIONS)) {
            qWarning() << "Permission verification failed for base directory: " << currentPath;
            return false;
        }
    }

    // Create each level of the directory structure
    for (const QString& component : pathComponents) {
        // Validate the component
        InputValidation::ValidationResult componentResult =
            InputValidation::validateInput(component, InputValidation::InputType::PlainText);
        if (!componentResult.isValid) {
            qWarning() << "Invalid directory component: " << componentResult.errorMessage;
            return false;
        }

        currentPath = QDir::cleanPath(currentPath + "/" + component);

        // Validate the full path after adding the component
        InputValidation::ValidationResult pathResult =
            InputValidation::validateInput(currentPath, InputValidation::InputType::FilePath);
        if (!pathResult.isValid) {
            qWarning() << "Invalid directory path: " << pathResult.errorMessage;
            return false;
        }

        QDir dir(currentPath);
        if (!dir.exists()) {
            bool created = dir.mkpath(".");
            if (!created) {
                qWarning() << "Failed to create directory: " << currentPath;
                return false;
            }

            // Set secure permissions
            if (!QFile::setPermissions(currentPath, DEFAULT_DIR_PERMISSIONS)) {
                qWarning() << "Failed to set permissions on directory: " << currentPath;
                return false;
            }

            // Verify permissions
            if (!verifyPermissions(currentPath, DEFAULT_DIR_PERMISSIONS)) {
                qWarning() << "Permission verification failed for directory: " << currentPath;
                return false;
            }
        }

        // Re-validate to ensure no manipulation occurred during creation
        InputValidation::ValidationResult postPathResult =
            InputValidation::validateInput(currentPath, InputValidation::InputType::FilePath);
        if (!postPathResult.isValid) {
            qWarning() << "Post-creation path validation failed: " << postPathResult.errorMessage;
            return false;
        }
    }

    return true;
}

// File Creation and Opening
bool createSecureFile(const QString& filePath, QFile::Permissions permissions) {
    // Default to secure permissions if none specified
    if (permissions == QFile::Permissions()) {
        permissions = DEFAULT_FILE_PERMISSIONS;
    }

    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid file path: " << result.errorMessage;
        return false;
    }

    // Ensure parent directory exists
    QFileInfo fileInfo(filePath);
    QString dirPath = fileInfo.dir().path();
    if (!ensureDirectoryExists(dirPath)) {
        return false;
    }

    // Use QSaveFile for atomic file creation
    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to create file: " << filePath;
        return false;
    }

    // Make empty file by opening and closing
    if (!saveFile.commit()) {
        qWarning() << "Failed to commit new empty file: " << filePath;
        return false;
    }

    // Set file permissions
    if (!QFile::setPermissions(filePath, permissions)) {
        qWarning() << "Failed to set permissions on file: " << filePath;
        return false;
    }

    // Verify permissions were applied correctly
    if (!verifyPermissions(filePath, permissions)) {
        qWarning() << "Permission verification failed for file: " << filePath;
        return false;
    }

    // Re-validate path after creation to ensure no manipulation occurred
    InputValidation::ValidationResult postResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!postResult.isValid) {
        qWarning() << "Post-creation path validation failed: " << postResult.errorMessage;
        return false;
    }

    return true;
}

std::unique_ptr<QFile> openSecureFile(const QString& filePath, QIODevice::OpenMode mode) {
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid file path: " << result.errorMessage;
        return nullptr;
    }

    auto file = std::make_unique<QFile>(filePath);
    if (!file->open(mode)) {
        qWarning() << "Failed to open file: " << filePath;
        return nullptr;
    }

    // Re-validate path after opening to ensure no manipulation occurred
    InputValidation::ValidationResult postResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!postResult.isValid) {
        qWarning() << "Post-open path validation failed: " << postResult.errorMessage;
        file->close();
        return nullptr;
    }

    return file;
}

// Optimized temp file creation that reduces file handle operations
std::unique_ptr<QTemporaryFile> createTempFile(const QString& baseFileTemplate, bool autoRemove) {
    QString templateStr;

    if (baseFileTemplate.isEmpty()) {
        // Use a user-specific subdirectory within Data for temporary files
        QString tempDir = QDir::cleanPath(QDir::current().path() + "/Data/" + g_username + "/Temp");
        if (!ensureDirectoryExists(tempDir)) {
            qWarning() << "Failed to create temporary directory: " << tempDir;
            return nullptr;
        }
        templateStr = tempDir + "/" + g_username + "_XXXXXX.tmp";
    } else {
        // Use provided template with minimal validation
        if (!baseFileTemplate.contains("XXXXXX")) {
            templateStr = baseFileTemplate + ".XXXXXX";
        } else {
            templateStr = baseFileTemplate;
        }

        // Ensure parent directory exists
        QFileInfo fileInfo(templateStr);
        QString dirPath = fileInfo.dir().path();
        if (!ensureDirectoryExists(dirPath)) {
            qWarning() << "Failed to create temporary file directory: " << dirPath;
            return nullptr;
        }
    }

    qDebug() << "Creating temp file with template:" << templateStr;
    auto tempFile = std::make_unique<QTemporaryFile>(templateStr);

    // Always set autoRemove to false - we'll handle cleanup ourselves
    tempFile->setAutoRemove(false);

    if (!tempFile->open()) {
        qWarning() << "Failed to create temporary file: " << templateStr;
        return nullptr;
    }

    qDebug() << "Created temp file:" << tempFile->fileName();

    // Set secure permissions - owner read/write only
    tempFile->setPermissions(DEFAULT_FILE_PERMISSIONS);

    return tempFile;
}

// Streamlined secure delete that prioritizes performance
bool secureDelete(const QString& filePath, int passes, bool allowExternalFiles) {
    qDebug() << "=== secureDelete called for:" << filePath << "with" << passes << "passes, allowExternalFiles:" << allowExternalFiles << " ===";

    // Process pending deletions occasionally to clean up old files
    static int cleanupCounter = 0;
    if (++cleanupCounter % 10 == 0) {
        // Try pending cleanups every 10 calls, but don't wait
        s_pendingMutex.lock();
        if (!s_cleanupScheduled && !s_pendingDeletions.isEmpty()) {
            s_cleanupScheduled = true;
            QFuture<void> future = QtConcurrent::run(performAsyncCleanup);
        }
        s_pendingMutex.unlock();
    }

    // Validate the file path - use different validation based on allowExternalFiles
    InputValidation::ValidationResult result;
    if (allowExternalFiles) {
        result = InputValidation::validateInput(filePath, InputValidation::InputType::ExternalFilePath);
    } else {
        result = InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    }

    if (!result.isValid) {
        qWarning() << "Invalid file path for secure deletion: " << result.errorMessage;
        return false;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "File doesn't exist, returning true";
        return true; // Already gone
    }

    // Get file size
    qint64 fileSize = file.size();
    qDebug() << "File size:" << fileSize;
    if (fileSize <= 0) {
        qDebug() << "File size <= 0, calling quickDelete";
        // Empty file, just quick delete
        return quickDelete(filePath);
    }

    // For smaller files or when the system is under heavy file operations,
    // reduce passes to improve performance
    int effectivePasses = (fileSize < 4096) ? 1 : (passes > 2 ? 2 : passes);
    qDebug() << "Effective passes:" << effectivePasses;

    // Make sure file is closed
    file.close();

    // Try to open with proper sharing flags to reduce locking issues
    qDebug() << "Attempting to open file for writing...";
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for writing, calling quickDelete";
        qDebug() << "File error:" << file.error();
        qDebug() << "Error string:" << file.errorString();
        // If we can't open it, just try quick deletion
        return quickDelete(filePath);
    }
    qDebug() << "File opened successfully for writing";

    // Buffer for overwriting
    const int BUFFER_SIZE = 4096;
    QByteArray buffer(BUFFER_SIZE, 0);
    QRandomGenerator* rng = QRandomGenerator::system();

    bool overwriteSuccess = true;

    try {
        // Perform minimal overwrite passes
        for (int pass = 0; pass < effectivePasses; ++pass) {
            qDebug() << "Secure delete pass" << (pass + 1) << "of" << effectivePasses << "for" << filePath;

            // Use a pattern based on pass number
            char pattern = (pass == 0) ? 0x00 : 0xFF;

            // Fill the file with the pattern
            if (!file.seek(0)) {
                qDebug() << "Failed to seek to beginning of file";
                overwriteSuccess = false;
                break;
            }

            buffer.fill(pattern);
            qint64 bytesRemaining = fileSize;

            while (bytesRemaining > 0 && overwriteSuccess) {
                int bytesToWrite = qMin(bytesRemaining, (qint64)BUFFER_SIZE);
                qint64 bytesWritten = file.write(buffer.constData(), bytesToWrite);

                if (bytesWritten != bytesToWrite) {
                    qDebug() << "Write failed: expected" << bytesToWrite << "got" << bytesWritten;
                    overwriteSuccess = false;
                    break;
                }

                bytesRemaining -= bytesWritten;
            }

            if (!overwriteSuccess) break;
            file.flush();
            qDebug() << "Pass" << (pass + 1) << "completed successfully";
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception during secure file deletion:" << e.what();
        overwriteSuccess = false;
    } catch (...) {
        qWarning() << "Unknown exception during secure file deletion";
        overwriteSuccess = false;
    }

    qDebug() << "Overwrite success:" << overwriteSuccess;

    // Close file
    file.close();
    qDebug() << "File closed, calling quickDelete";

    // Quick delete regardless of overwrite success
    bool deleteResult = quickDelete(filePath);
    qDebug() << "quickDelete result:" << deleteResult;
    return deleteResult;
}

bool encryptToTargetAndCleanup(QTemporaryFile* tempFile, const QString& targetPath, const QByteArray& encryptionKey) {
    if (!tempFile) {
        qWarning() << "Invalid temp file pointer provided for encryption";
        return false;
    }

    // Validate the target path
    InputValidation::ValidationResult targetResult =
        InputValidation::validateInput(targetPath, InputValidation::InputType::FilePath);

    if (!targetResult.isValid) {
        qWarning() << "Invalid target path for encryption: " << targetResult.errorMessage;
        return false;
    }

    // Get the temporary file path
    QString tempFilePath = tempFile->fileName();
    qDebug() << "Encrypting from temp file:" << tempFilePath << "to target:" << targetPath;

    // Using a separate scope to ensure TempFileCleaner is destroyed and cleanup() is called
    {
        // Create a temp file cleaner for secure deletion
        TempFileCleaner cleaner(tempFilePath);

        // Ensure target directory exists
        QFileInfo targetInfo(targetPath);
        QString targetDir = targetInfo.dir().path();
        if (!ensureDirectoryExists(targetDir)) {
            return false;
        }

        // Ensure temp file is closed but still exists
        tempFile->flush();
        tempFile->close();

        // Encrypt the temp file to the target
        bool encryptionSuccess = false;
        try {
            encryptionSuccess = CryptoUtils::Encryption_EncryptFile(encryptionKey, tempFilePath, targetPath);

            // If encryption succeeded, set secure permissions on the target file
            if (encryptionSuccess) {
                if (!QFile::setPermissions(targetPath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Failed to set permissions on encrypted file: " << targetPath;
                    return false;
                }

                // Verify permissions were set correctly
                if (!verifyPermissions(targetPath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Permission verification failed for encrypted file: " << targetPath;
                    return false;
                }

                // Re-validate target path after encryption
                InputValidation::ValidationResult postResult =
                    InputValidation::validateInput(targetPath, InputValidation::InputType::FilePath);
                if (!postResult.isValid) {
                    qWarning() << "Post-encryption path validation failed: " << postResult.errorMessage;
                    return false;
                }

                qDebug() << "Encryption completed successfully to:" << targetPath;
            } else {
                qWarning() << "Encryption failed for temp file:" << tempFilePath;
            }
        } catch (const std::exception& e) {
            qWarning() << "Exception during file encryption:" << e.what();
            encryptionSuccess = false;
        } catch (...) {
            qWarning() << "Unknown exception during file encryption";
            encryptionSuccess = false;
        }

        // Cleaner will handle deletion when it goes out of scope

        return encryptionSuccess;
    }
}

bool decryptToTempAndProcess(const QString& encryptedFilePath, const QByteArray& encryptionKey,
                             std::function<bool(QFile*)> processFunction, FileType fileType) {
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(encryptedFilePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid encrypted file path: " << result.errorMessage;
        return false;
    }

    // Check if encrypted file exists
    QFileInfo encryptedInfo(encryptedFilePath);
    if (!encryptedInfo.exists() || !encryptedInfo.isFile()) {
        qWarning() << "Encrypted file does not exist: " << encryptedFilePath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for decryption";
        return false;
    }

    // Close the temp file but keep it on disk
    QString tempFilePath = tempFile->fileName();
    tempFile->close();

    qDebug() << "Created temp file for decryption:" << tempFilePath;

    // Using a separate scope to ensure TempFileCleaner is destroyed and cleanup() is called
    {
        // Create a temp file cleaner for secure deletion
        TempFileCleaner cleaner(tempFilePath, fileType);

        // Decrypt to the temp file
        bool decryptionSuccess = false;
        try {
            decryptionSuccess = CryptoUtils::Encryption_DecryptFile(encryptionKey, encryptedFilePath, tempFilePath);
            if (!decryptionSuccess) {
                qWarning() << "Decryption failed for:" << encryptedFilePath;
                // Cleaner will handle deletion when it goes out of scope
                return false;
            }

            qDebug() << "Decryption successful to temp file:" << tempFilePath;
        } catch (const std::exception& e) {
            qWarning() << "Exception during file decryption:" << e.what();
            return false;
        } catch (...) {
            qWarning() << "Unknown exception during file decryption";
            return false;
        }

        // Process the decrypted temp file
        bool processingSuccess = false;
        try {
            // Reopen the temp file for processing
            auto reopenedTempFile = openSecureFile(tempFilePath, QIODevice::ReadWrite | QIODevice::Text);
            if (reopenedTempFile) {
                qDebug() << "Processing temp file:" << tempFilePath;
                processingSuccess = processFunction(reopenedTempFile.get());
                reopenedTempFile->close(); // Ensure file is closed after processing

                if (!processingSuccess) {
                    qWarning() << "Processing failed for decrypted file";
                }
            } else {
                qWarning() << "Failed to reopen temp file for processing: " << tempFilePath;
            }
        } catch (const std::exception& e) {
            qWarning() << "Exception during file processing:" << e.what();
            processingSuccess = false;
        } catch (...) {
            qWarning() << "Unknown exception during file processing";
            processingSuccess = false;
        }

        // Cleaner will handle deletion when it goes out of scope

        return processingSuccess;
    }
}

// New functions to abstract common file operations

// Optimized encrypted file reading with improved error handling
bool readEncryptedFileLines(const QString& filePath, const QByteArray& encryptionKey, QStringList& outLines) {
    outLines.clear();

    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid encrypted file path: " << result.errorMessage;
        return false;
    }

    // Check if the file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "Encrypted file does not exist: " << filePath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for decryption";
        return false;
    }

    // Close the temp file but keep it on disk
    QString tempFilePath = tempFile->fileName();
    tempFile->close();

    qDebug() << "Created temp file for reading encrypted lines:" << tempFilePath;

    // Use a separate scope for the TempFileCleaner to ensure proper cleanup
    {
        // Create a temp file cleaner for secure deletion
        TempFileCleaner cleaner(tempFilePath);

        // Decrypt to the temp file
        bool decryptionSuccess = false;
        try {
            decryptionSuccess = CryptoUtils::Encryption_DecryptFile(encryptionKey, filePath, tempFilePath);
            if (!decryptionSuccess) {
                qWarning() << "Decryption failed for file:" << filePath;
                return false;
            }

            qDebug() << "Successfully decrypted file to temp location";
        } catch (const std::exception& e) {
            qWarning() << "Exception during file decryption:" << e.what();
            return false;
        } catch (...) {
            qWarning() << "Unknown exception during file decryption";
            return false;
        }

        // Read the decrypted content line by line
        QFile file(tempFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open decrypted file for reading: " << tempFilePath;
            return false;
        }

        QTextStream in(&file);
        while (!in.atEnd()) {
            outLines.append(in.readLine());
        }
        file.close();

        qDebug() << "Successfully read" << outLines.size() << "lines from decrypted file";

        // Cleaner will handle deletion when it goes out of scope

        return true;
    }
}

// Optimized encrypted file writing
bool writeEncryptedFileLines(const QString& filePath, const QByteArray& encryptionKey, const QStringList& lines) {
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid file path for encryption: " << result.errorMessage;
        return false;
    }

    // Ensure the parent directory exists
    QFileInfo fileInfo(filePath);
    QString dirPath = fileInfo.dir().path();
    if (!ensureDirectoryExists(dirPath)) {
        qWarning() << "Failed to create directory for encrypted file: " << dirPath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for encryption";
        return false;
    }

    qDebug() << "Created temp file for writing encrypted lines:" << tempFile->fileName();

    // Use a separate scope for the TempFileCleaner to ensure proper cleanup
    {
        // Create a temp file cleaner for secure deletion
        QString tempFilePath = tempFile->fileName();
        TempFileCleaner cleaner(tempFilePath);

        // Write content to the temporary file
        QTextStream out(tempFile.get());
        for (const QString& line : lines) {
            out << line << "\n";
        }
        out.flush();
        tempFile->flush();
        tempFile->close();

        qDebug() << "Wrote" << lines.size() << "lines to temp file";

        // Encrypt the temporary file to the target file
        bool encryptionSuccess = false;
        try {
            encryptionSuccess = CryptoUtils::Encryption_EncryptFile(encryptionKey, tempFilePath, filePath);

            // If encryption succeeded, set secure permissions on the encrypted file
            if (encryptionSuccess) {
                if (!QFile::setPermissions(filePath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Failed to set permissions on encrypted file: " << filePath;
                    return false;
                }

                // Verify permissions were set correctly
                if (!verifyPermissions(filePath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Permission verification failed for encrypted file: " << filePath;
                    return false;
                }

                // Re-validate path after encryption
                InputValidation::ValidationResult postResult =
                    InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
                if (!postResult.isValid) {
                    qWarning() << "Post-encryption path validation failed: " << postResult.errorMessage;
                    return false;
                }

                qDebug() << "Successfully encrypted file to:" << filePath;
            } else {
                qWarning() << "Encryption failed for temp file:" << tempFilePath;
            }
        } catch (const std::exception& e) {
            qWarning() << "Exception during file encryption:" << e.what();
            encryptionSuccess = false;
        } catch (...) {
            qWarning() << "Unknown exception during file encryption";
            encryptionSuccess = false;
        }

        // Cleaner will handle deletion when it goes out of scope

        return encryptionSuccess;
    }
}

// Improved file and directory cleanup
bool deleteFileAndCleanEmptyDirs(const QString& filePath, const QStringList& hierarchyLevels, const QString& basePath) {
    // Validate the file path
    InputValidation::ValidationResult fileResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!fileResult.isValid) {
        qWarning() << "Invalid file path for delete operation: " << fileResult.errorMessage;
        return false;
    }

    // Validate the base path
    InputValidation::ValidationResult baseResult =
        InputValidation::validateInput(basePath, InputValidation::InputType::FilePath);
    if (!baseResult.isValid) {
        qWarning() << "Invalid base path for delete operation: " << baseResult.errorMessage;
        return false;
    }

    // Check if the file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "File does not exist: " << filePath;
        return false;
    }

    // Use quickDelete instead of secureDelete for better performance
    bool fileDeleted = quickDelete(filePath);
    if (!fileDeleted) {
        qWarning() << "Failed to delete file: " << filePath;
        return false;
    }

    qDebug() << "File deleted successfully, starting directory cleanup";

    // If hierarchyLevels is empty, we don't need to clean up directories
    if (hierarchyLevels.isEmpty()) {
        return true;
    }

    // Build paths for each level of the hierarchy starting from the deepest
    QString currentPath = basePath;
    QStringList fullHierarchyPaths;

    for (const QString& level : hierarchyLevels) {
        currentPath = QDir::cleanPath(currentPath + "/" + level);
        fullHierarchyPaths.append(currentPath);
    }

    // Start from the deepest directory and work our way up
    for (int i = fullHierarchyPaths.size() - 1; i >= 0; i--) {
        QString dirToCheck = fullHierarchyPaths.at(i);

        // Validate the directory path
        InputValidation::ValidationResult dirResult =
            InputValidation::validateInput(dirToCheck, InputValidation::InputType::FilePath);
        if (!dirResult.isValid) {
            qWarning() << "Invalid directory path for cleanup: " << dirResult.errorMessage;
            continue; // Skip this directory but try others
        }

        QDir dir(dirToCheck);
        if (!dir.exists()) {
            qDebug() << "Directory already removed: " << dirToCheck;
            continue;
        }

        // Check if directory is empty (only contains . and ..)
        QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        if (entries.isEmpty()) {
            qDebug() << "Removing empty directory: " << dirToCheck;

            // First ensure we have proper permissions to remove it
            if (!QFile::setPermissions(dirToCheck, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
                qWarning() << "Failed to set permissions for directory removal: " << dirToCheck;
            }

            if (!dir.rmdir(".")) {
                qWarning() << "Failed to remove empty directory: " << dirToCheck;

                // Try alternative removal method
                bool removed = QDir().rmdir(dirToCheck);
                if (!removed) {
                    qWarning() << "Alternative removal method also failed for: " << dirToCheck;
                    // Break here as we likely won't be able to remove parent directories
                    break;
                } else {
                    qDebug() << "Alternative removal succeeded for: " << dirToCheck;
                }
            } else {
                qDebug() << "Successfully removed directory: " << dirToCheck;
            }
        } else {
            qDebug() << "Directory not empty, stopping cleanup at: " << dirToCheck;
            // If a directory is not empty, we don't need to check parent directories
            break;
        }
    }

    return true;
}

// Optimized reading from encrypted files
bool readEncryptedFile(const QString& filePath, const QByteArray& encryptionKey, QString& outContent) {
    outContent.clear();

    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid encrypted file path: " << result.errorMessage;
        return false;
    }

    // Check if the file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "Encrypted file does not exist: " << filePath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for decryption";
        return false;
    }

    // Close the temp file but keep it on disk
    QString tempFilePath = tempFile->fileName();
    tempFile->close();

    qDebug() << "Created temp file for reading encrypted content:" << tempFilePath;

    // Use a separate scope for the TempFileCleaner to ensure proper cleanup
    {
        // Create a temp file cleaner for secure deletion
        TempFileCleaner cleaner(tempFilePath);

        // Decrypt to the temp file
        bool decryptionSuccess = false;
        try {
            decryptionSuccess = CryptoUtils::Encryption_DecryptFile(encryptionKey, filePath, tempFilePath);
            if (!decryptionSuccess) {
                qWarning() << "Decryption failed for file:" << filePath;
                return false;
            }

            qDebug() << "Successfully decrypted file to temp location";
        } catch (const std::exception& e) {
            qWarning() << "Exception during file decryption:" << e.what();
            return false;
        } catch (...) {
            qWarning() << "Unknown exception during file decryption";
            return false;
        }

        // Read the decrypted content
        QFile file(tempFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open decrypted file for reading: " << tempFilePath;
            return false;
        }

        QTextStream in(&file);
        outContent = in.readAll();
        file.close();

        qDebug() << "Successfully read" << outContent.length() << "characters from decrypted file";

        // Cleaner will handle deletion when it goes out of scope

        return true;
    }
}

// Optimized writing to encrypted files
bool writeEncryptedFile(const QString& filePath, const QByteArray& encryptionKey, const QString& content) {
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid file path for encryption: " << result.errorMessage;
        return false;
    }

    // Ensure the parent directory exists
    QFileInfo fileInfo(filePath);
    QString dirPath = fileInfo.dir().path();
    if (!ensureDirectoryExists(dirPath)) {
        qWarning() << "Failed to create directory for encrypted file: " << dirPath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for encryption";
        return false;
    }

    qDebug() << "Created temp file for writing encrypted content:" << tempFile->fileName();

    // Use a separate scope for the TempFileCleaner to ensure proper cleanup
    {
        // Create a temp file cleaner for secure deletion
        QString tempFilePath = tempFile->fileName();
        TempFileCleaner cleaner(tempFilePath);

        // Write content to the temporary file
        QTextStream out(tempFile.get());
        out << content;
        out.flush();
        tempFile->flush();
        tempFile->close();

        qDebug() << "Wrote" << content.length() << "characters to temp file";

        // Encrypt the temporary file to the target file
        bool encryptionSuccess = false;
        try {
            encryptionSuccess = CryptoUtils::Encryption_EncryptFile(encryptionKey, tempFilePath, filePath);

            // If encryption succeeded, set secure permissions on the encrypted file
            if (encryptionSuccess) {
                if (!QFile::setPermissions(filePath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Failed to set permissions on encrypted file: " << filePath;
                    return false;
                }

                // Verify permissions were set correctly
                if (!verifyPermissions(filePath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Permission verification failed for encrypted file: " << filePath;
                    return false;
                }

                // Re-validate path after encryption
                InputValidation::ValidationResult postResult =
                    InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
                if (!postResult.isValid) {
                    qWarning() << "Post-encryption path validation failed: " << postResult.errorMessage;
                    return false;
                }

                qDebug() << "Successfully encrypted file to:" << filePath;
            } else {
                qWarning() << "Encryption failed for temp file:" << tempFilePath;
            }
        } catch (const std::exception& e) {
            qWarning() << "Exception during file encryption:" << e.what();
            encryptionSuccess = false;
        } catch (...) {
            qWarning() << "Unknown exception during file encryption";
            encryptionSuccess = false;
        }

        // Cleaner will handle deletion when it goes out of scope

        return encryptionSuccess;
    }
}

// Process (modify) the content of an encrypted file
bool processEncryptedFile(const QString& filePath, const QByteArray& encryptionKey,
                          std::function<bool(QString&)> processFunction) {
    // Read the content
    QString content;
    if (!readEncryptedFile(filePath, encryptionKey, content)) {
        qWarning() << "Failed to read content from encrypted file: " << filePath;
        return false;
    }

    // Process the content
    bool processSuccess = false;
    try {
        processSuccess = processFunction(content);
    } catch (const std::exception& e) {
        qWarning() << "Exception during content processing: " << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception during content processing";
        return false;
    }

    if (!processSuccess) {
        qWarning() << "Content processing failed for file: " << filePath;
        return false;
    }

    // Write the modified content back to the file
    return writeEncryptedFile(filePath, encryptionKey, content);
}

// Search for patterns in an encrypted file
bool searchEncryptedFile(const QString& filePath, const QByteArray& encryptionKey,
                         const QRegularExpression& searchPattern, QStringList& results) {
    results.clear();

    // Read the content
    QString content;
    if (!readEncryptedFile(filePath, encryptionKey, content)) {
        qWarning() << "Failed to read content from encrypted file for search: " << filePath;
        return false;
    }

    // Perform the search
    QRegularExpressionMatchIterator matches = searchPattern.globalMatch(content);
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        results.append(match.captured());
    }

    return true;
}

// Validation Integration
bool validateFilePath(const QString& filePath, FileType fileType, const QByteArray& encryptionKey) {
    // First, perform basic path validation
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid file path: " << result.errorMessage;
        return false;
    }

    // Check if the file is within the allowed directory
    if (!isWithinAllowedDirectory(filePath, "Data")) {
        qWarning() << "File path is outside of allowed directory: " << filePath;
        return false;
    }

    // Check if the file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        // For some file types, non-existence is acceptable
        if (fileType == FileType::Password || fileType == FileType::TaskList) {
            return true;
        }
        qWarning() << "File does not exist: " << filePath;
        return false;
    }

    // Perform type-specific validation
    try {
        switch (fileType) {
        case FileType::Diary:
            return InputValidation::validateDiaryFile(filePath, encryptionKey);
        case FileType::Password:
            return InputValidation::validatePasswordFile(filePath, encryptionKey);
        case FileType::TaskList:
            return InputValidation::validateTasklistFile(filePath, encryptionKey);
        case FileType::Generic:
            // Just check if the file path is valid, which we already did
            return true;
        default:
            qWarning() << "Unknown file type for validation";
            return false;
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception during file validation: " << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception during file validation";
        return false;
    }
}

bool isWithinAllowedDirectory(const QString& filePath, const QString& baseDirectory) {
    // Validate the file path
    InputValidation::ValidationResult filePathResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!filePathResult.isValid) {
        qWarning() << "Invalid file path for directory check: " << filePathResult.errorMessage;
        return false;
    }

    // Validate the base directory
    InputValidation::ValidationResult baseDirResult =
        InputValidation::validateInput(baseDirectory, InputValidation::InputType::PlainText);
    if (!baseDirResult.isValid) {
        qWarning() << "Invalid base directory name: " << baseDirResult.errorMessage;
        return false;
    }

    // Create the base directory path
    QString basePath = QDir::cleanPath(QDir(QDir::current().path() + "/" + baseDirectory).absolutePath());

    // Create a clean path for the file
    QFileInfo fileInfo(filePath);
    QString absolutePath = fileInfo.absoluteFilePath();
    QString canonicalPath = QDir::cleanPath(absolutePath);

    // Get canonical paths if the files/directories exist
    if (fileInfo.exists()) {
        canonicalPath = fileInfo.canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            // Fallback to cleaned path if canonical path can't be determined
            canonicalPath = QDir::cleanPath(absolutePath);
        }
    }

    QFileInfo baseInfo(basePath);
    if (baseInfo.exists() && baseInfo.isDir()) {
        basePath = baseInfo.canonicalFilePath();
        if (basePath.isEmpty()) {
            // Fallback to cleaned path if canonical path can't be determined
            basePath = QDir::cleanPath(baseInfo.absoluteFilePath());
        }
    }

    // Check if canonical path starts with base path
    return canonicalPath.startsWith(basePath);
}

// Utility Functions
QString sanitizePath(const QString& path) {
    // Validate the path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(path, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid path for sanitization: " << result.errorMessage;
        return QString(); // Return empty string to indicate error
    }

    // First clean the path using Qt's built-in function
    QString cleaned = QDir::cleanPath(path);

    // Check if the cleaned path is within allowed directory
    if (!isWithinAllowedDirectory(cleaned, "Data")) {
        qWarning() << "Sanitized path outside allowed directory: " << cleaned;
        return QString(); // Return empty string to indicate error
    }

    // Get canonical path if the file or directory exists
    QFileInfo fileInfo(cleaned);
    if (fileInfo.exists()) {
        QString canonicalPath = fileInfo.canonicalFilePath();
        if (!canonicalPath.isEmpty()) {
            cleaned = canonicalPath;
        }
    }

    return cleaned;
}

// Read a tasklist file and return the task entries
bool readTasklistFile(const QString& filePath, const QByteArray& encryptionKey,
                      QStringList& taskLines) {
    taskLines.clear();

    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid tasklist file path:" << result.errorMessage;
        return false;
    }

    // Check if the file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "Tasklist file does not exist:" << filePath;
        return false;
    }

    // Validate the tasklist file for security
    if (!InputValidation::validateTasklistFile(filePath, encryptionKey)) {
        qWarning() << "Invalid task list file during reading:" << filePath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for decryption";
        return false;
    }

    // Close the temp file but keep it on disk
    QString tempFilePath = tempFile->fileName();
    tempFile->close();

    qDebug() << "Created temp file for reading tasklist:" << tempFilePath;

    // Use a separate scope for the TempFileCleaner to ensure proper cleanup
    {
        // Create a temp file cleaner for secure deletion
        TempFileCleaner cleaner(tempFilePath);

        // Decrypt to the temp file
        bool decryptionSuccess = false;
        try {
            decryptionSuccess = CryptoUtils::Encryption_DecryptFile(encryptionKey, filePath, tempFilePath);
            if (!decryptionSuccess) {
                qWarning() << "Decryption failed for tasklist file:" << filePath;
                return false;
            }

            qDebug() << "Successfully decrypted tasklist to temp file";
        } catch (const std::exception& e) {
            qWarning() << "Exception during tasklist file decryption:" << e.what();
            return false;
        } catch (...) {
            qWarning() << "Unknown exception during tasklist file decryption";
            return false;
        }

        // Read the decrypted content
        QFile file(tempFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open decrypted tasklist file for reading:" << tempFilePath;
            return false;
        }

        QTextStream in(&file);

        // Read all lines (task entries)
        while (!in.atEnd()) {
            taskLines.append(in.readLine());
        }

        file.close();

        qDebug() << "Successfully read" << taskLines.size() << "task lines";

        // Cleaner will handle deletion when it goes out of scope

        return true;
    }
}

// Write a tasklist file with task entries
bool writeTasklistFile(const QString& filePath, const QByteArray& encryptionKey,
                       const QStringList& taskLines) {
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid tasklist file path for writing:" << result.errorMessage;
        return false;
    }

    // Validate individual task entries
    for (const QString& taskLine : taskLines) {
        // Task entries are validated as PlainText
        InputValidation::ValidationResult taskResult =
            InputValidation::validateInput(taskLine, InputValidation::InputType::PlainText);
        if (!taskResult.isValid) {
            qWarning() << "Invalid task entry: " << taskResult.errorMessage;
            return false;
        }
    }

    // Ensure the parent directory exists
    QFileInfo fileInfo(filePath);
    QString dirPath = fileInfo.dir().path();
    if (!ensureDirectoryExists(dirPath)) {
        qWarning() << "Failed to create directory for tasklist file:" << dirPath;
        return false;
    }

    // Create a temporary file
    auto tempFile = createTempFile();
    if (!tempFile) {
        qWarning() << "Failed to create temporary file for tasklist encryption";
        return false;
    }

    qDebug() << "Created temp file for writing tasklist:" << tempFile->fileName();

    // Use a separate scope for the TempFileCleaner to ensure proper cleanup
    {
        // Create a temp file cleaner for secure deletion
        QString tempFilePath = tempFile->fileName();
        TempFileCleaner cleaner(tempFilePath);

        // Write content to the temporary file
        QTextStream out(tempFile.get());

        // Write task entries
        for (const QString& line : taskLines) {
            out << line << "\n";
        }

        out.flush();
        tempFile->flush();
        tempFile->close();

        qDebug() << "Wrote" << taskLines.size() << "task lines to temp file";

        // Encrypt the temporary file to the target file
        bool encryptionSuccess = false;
        try {
            encryptionSuccess = CryptoUtils::Encryption_EncryptFile(encryptionKey, tempFilePath, filePath);

            // If encryption succeeded, set secure permissions on the tasklist file
            if (encryptionSuccess) {
                if (!QFile::setPermissions(filePath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Failed to set permissions on tasklist file: " << filePath;
                    return false;
                }

                // Verify permissions were set correctly
                if (!verifyPermissions(filePath, DEFAULT_FILE_PERMISSIONS)) {
                    qWarning() << "Permission verification failed for tasklist file: " << filePath;
                    return false;
                }

                // Re-validate path after encryption
                InputValidation::ValidationResult postResult =
                    InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
                if (!postResult.isValid) {
                    qWarning() << "Post-encryption path validation failed: " << postResult.errorMessage;
                    return false;
                }

                qDebug() << "Successfully encrypted tasklist to:" << filePath;
            } else {
                qWarning() << "Encryption failed for tasklist temp file:" << tempFilePath;
            }
        } catch (const std::exception& e) {
            qWarning() << "Exception during tasklist file encryption:" << e.what();
            encryptionSuccess = false;
        } catch (...) {
            qWarning() << "Unknown exception during tasklist file encryption";
            encryptionSuccess = false;
        }

        // Cleaner will handle deletion when it goes out of scope

        return encryptionSuccess;
    }
}

// Find a specific task entry in a tasklist file
bool findTaskEntry(const QString& filePath, const QByteArray& encryptionKey,
                   const QString& taskName, QString& taskEntry) {
    taskEntry.clear();

    // Read the tasklist file
    QStringList taskLines;
    if (!readTasklistFile(filePath, encryptionKey, taskLines)) {
        return false;
    }

    // Search for the task entry
    for (const QString& line : taskLines) {
        if (line.isEmpty()) {
            continue;
        }

        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');

        // Ensure we have at least task type and name
        if (parts.size() < 2) {
            continue;
        }

        QString currentTaskName = parts[1];

        // Unescape any escaped pipe characters in the task name
        currentTaskName.replace("\\|", "|");

        // Check if this is the task we're looking for
        if (currentTaskName == taskName) {
            taskEntry = line;
            return true;
        }
    }

    return false; // Task not found
}

// Add a new task entry to a tasklist file
bool addTaskEntry(const QString& filePath, const QByteArray& encryptionKey,
                  const QString& taskEntry) {
    // Validate the file path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Invalid file path for task entry: " << pathResult.errorMessage;
        return false;
    }

    // Validate the task entry
    InputValidation::ValidationResult taskResult =
        InputValidation::validateInput(taskEntry, InputValidation::InputType::PlainText);
    if (!taskResult.isValid) {
        qWarning() << "Invalid task entry: " << taskResult.errorMessage;
        return false;
    }

    // Read the existing tasklist
    QStringList taskLines;

    // Check if file exists
    QFileInfo fileInfo(filePath);
    bool fileExists = fileInfo.exists() && fileInfo.isFile();

    if (fileExists) {
        // Read existing content
        if (!readTasklistFile(filePath, encryptionKey, taskLines)) {
            return false;
        }
    }

    // Add the new task entry
    taskLines.append(taskEntry);

    // Write the updated tasklist back to the file
    return writeTasklistFile(filePath, encryptionKey, taskLines);
}

// Find and modify a specific task entry in a tasklist file
bool modifyTaskEntry(const QString& filePath, const QByteArray& encryptionKey,
                     const QString& taskName, const QString& newTaskEntry) {
    // Validate the file path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Invalid file path for task modification: " << pathResult.errorMessage;
        return false;
    }

    // Validate the task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        qWarning() << "Invalid task name: " << nameResult.errorMessage;
        return false;
    }

    // Validate the new task entry
    InputValidation::ValidationResult entryResult =
        InputValidation::validateInput(newTaskEntry, InputValidation::InputType::PlainText);
    if (!entryResult.isValid) {
        qWarning() << "Invalid new task entry: " << entryResult.errorMessage;
        return false;
    }

    // Read the tasklist file
    QStringList taskLines;
    if (!readTasklistFile(filePath, encryptionKey, taskLines)) {
        return false;
    }

    bool taskFound = false;

    // Find and replace the task entry
    for (int i = 0; i < taskLines.size(); ++i) {
        QString line = taskLines[i];

        if (line.isEmpty()) {
            continue;
        }

        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');

        // Ensure we have at least task type and name
        if (parts.size() < 2) {
            continue;
        }

        QString currentTaskName = parts[1];

        // Unescape any escaped pipe characters in the task name
        currentTaskName.replace("\\|", "|");

        // Check if this is the task we're looking for
        if (currentTaskName == taskName) {
            taskLines[i] = newTaskEntry;
            taskFound = true;
            break;
        }
    }

    if (!taskFound) {
        qWarning() << "Task not found for modification:" << taskName;
        return false;
    }

    // Write the updated tasklist back to the file
    return writeTasklistFile(filePath, encryptionKey, taskLines);
}

// Remove a specific task entry from a tasklist file
bool removeTaskEntry(const QString& filePath, const QByteArray& encryptionKey,
                     const QString& taskName) {
    // Validate the file path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Invalid file path for task removal: " << pathResult.errorMessage;
        return false;
    }

    // Validate the task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        qWarning() << "Invalid task name for removal: " << nameResult.errorMessage;
        return false;
    }

    // Read the tasklist file
    QStringList taskLines;
    if (!readTasklistFile(filePath, encryptionKey, taskLines)) {
        return false;
    }

    bool taskFound = false;

    // Find and remove the task entry
    for (int i = 0; i < taskLines.size(); ++i) {
        QString line = taskLines[i];

        if (line.isEmpty()) {
            continue;
        }

        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');

        // Ensure we have at least task type and name
        if (parts.size() < 2) {
            continue;
        }

        QString currentTaskName = parts[1];

        // Unescape any escaped pipe characters in the task name
        currentTaskName.replace("\\|", "|");

        // Check if this is the task we're looking for
        if (currentTaskName == taskName) {
            taskLines.removeAt(i);
            taskFound = true;
            break;
        }
    }

    if (!taskFound) {
        qWarning() << "Task not found for removal:" << taskName;
        return false;
    }

    // Write the updated tasklist back to the file
    return writeTasklistFile(filePath, encryptionKey, taskLines);
}

// Utility function to create a new empty tasklist file
bool createNewTasklistFile(const QString& filePath, const QByteArray& encryptionKey) {
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid tasklist file path for creation:" << result.errorMessage;
        return false;
    }

    // Ensure the parent directory exists
    QFileInfo fileInfo(filePath);
    QString dirPath = fileInfo.dir().path();
    if (!ensureDirectoryExists(dirPath)) {
        qWarning() << "Failed to create directory for new tasklist file:" << dirPath;
        return false;
    }

    // Check if file already exists
    if (fileInfo.exists()) {
        qWarning() << "Tasklist file already exists:" << filePath;
        return false;
    }

    // Create an empty file
    QStringList emptyList;
    return writeTasklistFile(filePath, encryptionKey, emptyList);
}

} // end namespace OperationsFiles
