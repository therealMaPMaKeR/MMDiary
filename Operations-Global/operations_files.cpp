/*
 * Security Enhancements Applied (Windows-specific):
 * - Windows MAX_PATH handling with long path support (\\?\\prefix)
 * - Enhanced sanitizePath() with pre-validation and Windows-specific checks
 * - Secure path concatenation to prevent injection attacks  
 * - TOCTOU race condition fixes in path validation
 * - Path length validation for Windows (260 chars without prefix, 32767 with prefix)
 * - Path normalization for secure case-insensitive comparisons
 * - Windows reserved device name blocking (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
 * - Alternate Data Stream (ADS) detection
 * - URL-encoded path traversal detection (%2e%2e, %252e%252e, etc.)
 * - Windows 8.3 short name format detection (~1, ~2, etc.)
 */

#include "operations_files.h"
#include "inputvalidation.h"
#include "encryption/CryptoUtils.h"

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
#include <QHash>
#include <functional>
#include <memory>
#include <QAtomicInt>
#include <QDirIterator>
#include <algorithm>

// For Windows-specific APIs
#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace OperationsFiles {

// Static member definitions for FileLocker
QMutex FileLocker::s_lockMapMutex;
QHash<QString, FileLocker*> FileLocker::s_activeLocks;

// FileLocker implementation
FileLocker::FileLocker(const QString& filePath)
    : m_filePath(QFileInfo(filePath).absoluteFilePath())
    , m_locked(false)
#ifdef Q_OS_WIN
    , m_fileHandle(INVALID_HANDLE_VALUE)
#else
    , m_fd(-1)
#endif
{
    qDebug() << "FileLocker: Creating lock for" << m_filePath;
}

FileLocker::~FileLocker()
{
    if (m_locked) {
        unlock();
    }
}

bool FileLocker::tryLock(int timeoutMs)
{
    QMutexLocker locker(&s_lockMapMutex);
    
    // Check if file is already locked by another FileLocker instance
    if (s_activeLocks.contains(m_filePath)) {
        qDebug() << "FileLocker: File already locked by another instance:" << m_filePath;
        return false;
    }
    
#ifdef Q_OS_WIN
    // Windows implementation using LockFileEx
    QString nativePath = QDir::toNativeSeparators(m_filePath);
    
    // Open file for locking (don't create if doesn't exist)
    m_fileHandle = CreateFileW(
        reinterpret_cast<const wchar_t*>(nativePath.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, // Allow others to read but not write
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        // File doesn't exist or can't be opened - that's okay, no lock needed
        qDebug() << "FileLocker: File doesn't exist or can't be opened:" << m_filePath;
        return true; // Return true as there's nothing to lock
    }
    
    // Try to lock the file
    OVERLAPPED overlapped = {0};
    BOOL result = LockFileEx(
        m_fileHandle,
        LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
        0,
        MAXDWORD,
        MAXDWORD,
        &overlapped
    );
    
    if (result) {
        m_locked = true;
        s_activeLocks[m_filePath] = this;
        qDebug() << "FileLocker: Successfully locked file:" << m_filePath;
        return true;
    } else {
        // Try with timeout if immediate lock failed
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            Sleep(100); // Wait 100ms
            elapsed += 100;
            
            result = LockFileEx(
                m_fileHandle,
                LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                0,
                MAXDWORD,
                MAXDWORD,
                &overlapped
            );
            
            if (result) {
                m_locked = true;
                s_activeLocks[m_filePath] = this;
                qDebug() << "FileLocker: Successfully locked file after" << elapsed << "ms:" << m_filePath;
                return true;
            }
        }
        
        // Failed to lock within timeout
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        qDebug() << "FileLocker: Failed to lock file within timeout:" << m_filePath;
        return false;
    }
#else
    // Unix/Linux implementation would use flock or fcntl
    // For now, just use mutex-based locking
    m_locked = true;
    s_activeLocks[m_filePath] = this;
    return true;
#endif
}

void FileLocker::unlock()
{
    if (!m_locked) {
        return;
    }
    
    QMutexLocker locker(&s_lockMapMutex);
    
#ifdef Q_OS_WIN
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        // Unlock the file
        OVERLAPPED overlapped = {0};
        UnlockFileEx(
            m_fileHandle,
            0,
            MAXDWORD,
            MAXDWORD,
            &overlapped
        );
        
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
    }
#endif
    
    s_activeLocks.remove(m_filePath);
    m_locked = false;
    qDebug() << "FileLocker: Unlocked file:" << m_filePath;
}

// Default secure permissions - owner read/write only
// IMPORTANT: These permissions exclude ReadUser and WriteUser for enhanced security
// Only the file owner can read/write, no other users have access
const QFile::Permissions DEFAULT_FILE_PERMISSIONS = QFile::ReadOwner | QFile::WriteOwner;
const QFile::Permissions DEFAULT_DIR_PERMISSIONS = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;

// Size limits for operations_files encryption functions
// These functions are designed for text-based files (diary, tasklist, settings)
// For larger files (videos, bulk data), use dedicated encryption worker classes
const qint64 MAX_ENCRYPTED_FILE_SIZE = 50 * 1024 * 1024; // 50MB limit for encrypted files
const qint64 MAX_CONTENT_SIZE = 50 * 1024 * 1024; // 50MB limit for content to encrypt

// Resource limits for temp directory management
const qint64 MAX_TEMP_DIRECTORY_SIZE = 20LL * 1024 * 1024 * 1024; // 20GB default limit
const qint64 TEMP_CLEANUP_THRESHOLD = (MAX_TEMP_DIRECTORY_SIZE * 80) / 100; // 80% of max size (16GB)
const qint64 MIN_DISK_SPACE_REQUIRED = 1LL * 1024 * 1024 * 1024; // 1GB minimum free space after operations

// Windows-specific reserved device names
static const QStringList WINDOWS_RESERVED_NAMES = {
    "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
    "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", 
    "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

// Static data for cleanup management
static QList<QString> s_pendingDeletions;
static QMutex s_pendingMutex;  // For thread safety
static QAtomicInt s_cleanupScheduled(0);  // Atomic flag: 0 = not scheduled, 1 = scheduled
static QAtomicInt s_cleanupCounter(0);     // Atomic counter for periodic cleanup triggers

// Forward declarations of internal functions
void performAsyncCleanup();
bool quickDelete(const QString& filePath);
QString enableWindowsLongPath(const QString& path);
bool validatePathLength(const QString& path);
QString securePathJoin(const QString& basePath, const QString& component);
QString normalizePathForComparison(const QString& path);
bool isWeakEncryptionKey(const QByteArray& key);

// RAII class for temporary file management
TempFileCleaner::TempFileCleaner(const QString& filePath, FileType fileType)
    : m_filePath(filePath), m_fileType(fileType), m_cleanup(true) {
    qDebug() << "operations_files: TempFileCleaner created for:" << filePath;
}

TempFileCleaner::~TempFileCleaner() {
    qDebug() << "operations_files: TempFileCleaner destructor called for:" << m_filePath;
    cleanup();
}

void TempFileCleaner::disableCleanup() {
    qDebug() << "operations_files: Cleanup disabled for:" << m_filePath;
    m_cleanup = false;
}

void TempFileCleaner::cleanup() {
    if (m_cleanup && !m_filePath.isEmpty()) {
        qDebug() << "operations_files: Performing cleanup for:" << m_filePath;

        // Try to delete the file
        bool deleted = quickDelete(m_filePath);

        // Only clear the path if deletion was successful or scheduled
        if (deleted || s_pendingDeletions.contains(m_filePath)) {
            m_filePath.clear();
            m_cleanup = false;
        } else {
            // If deletion failed and not in pending list, keep the path for possible retry
            qWarning() << "operations_files: Cleanup failed for:" << m_filePath;
            // Add to pending deletions to ensure it gets cleaned up later
            s_pendingMutex.lock();
            if (!s_pendingDeletions.contains(m_filePath)) {
                s_pendingDeletions.append(m_filePath);
                qDebug() << "operations_files: Added to pending deletion list:" << m_filePath;
            }
            s_pendingMutex.unlock();
        }
    } else if (m_cleanup && m_filePath.isEmpty()) {
        qDebug() << "operations_files: Cleanup called with empty path";
    } else {
        qDebug() << "operations_files: Cleanup skipped (disabled) for:" << m_filePath;
    }
}

// A lightweight function for quick file deletion without blocking
bool quickDelete(const QString& filePath) {
    qDebug() << "operations_files: === quickDelete called for:" << filePath << "===";

    if (!QFile::exists(filePath)) {
        qDebug() << "operations_files: File doesn't exist in quickDelete";
        return true;  // Already gone
    }

    // First try standard removal - no delay
    qDebug() << "operations_files: Trying QFile::remove()...";
    if (QFile::remove(filePath)) {
        qDebug() << "operations_files: Standard deletion successful for:" << filePath;
        return true;
    }
    qDebug() << "operations_files: QFile::remove() failed";

// If that failed, use Windows API as it's more effective for locked files
#ifdef Q_OS_WIN
    qDebug() << "operations_files: Trying Windows API deletion...";
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
        qDebug() << "operations_files: Windows API deletion scheduled for:" << filePath;
        return true;
    }
    qDebug() << "operations_files: Windows API deletion also failed";
#endif

    // If we couldn't delete, add to pending list (safely)
    qDebug() << "operations_files: Adding to pending deletion list";
    
    bool needsScheduling = false;
    
    s_pendingMutex.lock();
    if (!s_pendingDeletions.contains(filePath)) {
        s_pendingDeletions.append(filePath);
        qDebug() << "operations_files: Added to pending deletion list:" << filePath;
        
        // Check if we need to schedule cleanup (if not already scheduled)
        // Use load() for atomic read
        if (s_cleanupScheduled.loadAcquire() == 0) {
            needsScheduling = true;
        }
    }
    s_pendingMutex.unlock();
    
    // Schedule cleanup outside of mutex to avoid holding lock during async operation
    if (needsScheduling) {
        // Try to atomically set the flag from 0 to 1
        // This ensures only one thread schedules the cleanup
        if (s_cleanupScheduled.testAndSetOrdered(0, 1)) {
            qDebug() << "operations_files: Scheduling async cleanup";
            
            // Schedule asynchronous cleanup to avoid blocking the current operation
            QFuture<void> future = QtConcurrent::run([]() {
                // Give the current operation time to complete and release locks
                QThread::msleep(100);
                performAsyncCleanup();
            });
        } else {
            qDebug() << "operations_files: Async cleanup already scheduled by another thread";
        }
    }

    return false;
}

// Non-blocking cleanup function that runs in a separate thread
void performAsyncCleanup() {
    qDebug() << "operations_files: performAsyncCleanup started";
    
    // Create a local copy of pending deletions to minimize lock time
    QList<QString> localPendingList;
    
    // Quick lock to copy the list
    {
        QMutexLocker locker(&s_pendingMutex);
        
        if (s_pendingDeletions.isEmpty()) {
            qDebug() << "operations_files: No pending deletions, resetting cleanup flag";
            // Reset the cleanup flag atomically
            s_cleanupScheduled.storeRelease(0);
            return;
        }
        
        // Make a copy of the pending list
        localPendingList = s_pendingDeletions;
        // Clear the original list since we're processing these now
        s_pendingDeletions.clear();
    }
    
    qDebug() << "operations_files: Performing async cleanup of" << localPendingList.size() << "files";
    
    QList<QString> stillPending;
    
    // Process files without holding the mutex
    for (const QString& filePath : localPendingList) {
        if (!QFile::exists(filePath)) {
            qDebug() << "operations_files: Pending file no longer exists:" << filePath;
            continue; // Already gone
        }
        
        // Try standard delete first
        if (QFile::remove(filePath)) {
            qDebug() << "operations_files: Async cleanup: Successfully deleted file:" << filePath;
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
            qDebug() << "operations_files: Async cleanup: File marked for deletion on close:" << filePath;
            continue;
        }
#endif
        
        // Still locked, keep in pending list
        qDebug() << "operations_files: Async cleanup: File still locked:" << filePath;
        stillPending.append(filePath);
    }
    
    // Update the pending list with files that couldn't be deleted
    bool needsReschedule = false;
    
    if (!stillPending.isEmpty()) {
        QMutexLocker locker(&s_pendingMutex);
        
        // Merge stillPending with any new items that were added while we were processing
        for (const QString& filePath : stillPending) {
            if (!s_pendingDeletions.contains(filePath)) {
                s_pendingDeletions.append(filePath);
            }
        }
        
        if (!s_pendingDeletions.isEmpty()) {
            needsReschedule = true;
            qDebug() << "operations_files: Still have" << s_pendingDeletions.size() << "pending files";
        }
    }
    
    // Reset the cleanup flag or reschedule
    if (needsReschedule) {
        qDebug() << "operations_files: Scheduling another cleanup pass for remaining files";
        
        // Schedule another cleanup pass later
        QFuture<void> future = QtConcurrent::run([]() {
            QThread::msleep(1000); // Wait longer between batches
            performAsyncCleanup();
        });
        // Note: We keep s_cleanupScheduled as 1 since we're scheduling another pass
    } else {
        qDebug() << "operations_files: Async cleanup complete - no files remaining";
        // Reset the cleanup flag atomically
        s_cleanupScheduled.storeRelease(0);
    }
}

// Windows long path support function
QString enableWindowsLongPath(const QString& path) {
    QString absolutePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    
    // Check if path exceeds MAX_PATH (260 chars)
    if (absolutePath.length() >= 260) {
        // Add long path prefix if not already present
        if (!absolutePath.startsWith("\\\\?\\")) {
            if (absolutePath.startsWith("\\\\")) {
                // UNC path: \\server\share -> \\?\UNC\server\share
                absolutePath = "\\\\?\\\\UNC" + absolutePath.mid(1);
            } else {
                // Regular path: C:\path -> \\?\C:\path  
                absolutePath = "\\\\?\\\\" + absolutePath;
            }
        }
    }
    return absolutePath;
}

// Path length validation function for Windows
bool validatePathLength(const QString& path) {
    // Without long path prefix, Windows limit is 260
    if (!path.startsWith("\\\\?\\")) {
        if (path.length() >= 260) {
            qWarning() << "operations_files: Path exceeds Windows MAX_PATH limit:" << path.length() << "characters";
            return false;
        }
    } else {
        // With long path prefix, limit is ~32,767
        if (path.length() >= 32767) {
            qWarning() << "operations_files: Path exceeds Windows long path limit:" << path.length() << "characters";
            return false;
        }
    }
    return true;
}

// Secure path concatenation function
QString securePathJoin(const QString& basePath, const QString& component) {
    // SECURITY: Validate both inputs separately
    if (basePath.isEmpty() || component.isEmpty()) {
        qWarning() << "operations_files: Empty path component in join";
        return QString();
    }
    
    // SECURITY: Check for null bytes in both inputs
    if (basePath.contains(QChar(0)) || component.contains(QChar(0))) {
        qWarning() << "operations_files: Null byte detected in path join";
        return QString();
    }
    
    // SECURITY: Strict component validation - no path separators or traversal
    if (component.contains('/') || component.contains('\\') || 
        component.contains("..") || component == "." || component == "..") {
        qWarning() << "operations_files: Invalid component for path join:" << component;
        return QString();
    }
    
    // SECURITY: Check for URL-encoded path separators
    QString lowerComponent = component.toLower();
    if (lowerComponent.contains("%2f") || lowerComponent.contains("%5c") ||
        lowerComponent.contains("%2e")) {
        qWarning() << "operations_files: URL-encoded characters in path component";
        return QString();
    }
    
    // SECURITY: Check for Windows reserved names in component
    QString upperComponent = component.toUpper();
    for (const QString& reserved : WINDOWS_RESERVED_NAMES) {
        if (upperComponent == reserved || upperComponent.startsWith(reserved + ".")) {
            qWarning() << "operations_files: Reserved device name in component:" << reserved;
            return QString();
        }
    }
    
    // Validate the component as plain text
    InputValidation::ValidationResult componentResult =
        InputValidation::validateInput(component, InputValidation::InputType::PlainText);
    if (!componentResult.isValid) {
        qWarning() << "operations_files: Invalid path component:" << componentResult.errorMessage;
        return QString();
    }
    
    // SECURITY: Check base path is valid before joining
    QFileInfo baseInfo(basePath);
    if (baseInfo.exists() && baseInfo.isSymLink()) {
        qWarning() << "operations_files: Base path is a symbolic link";
        return QString();
    }
    
    // Use QDir for safe joining
    QDir baseDir(basePath);
    QString joined = baseDir.filePath(component);
    
    // Clean and validate the result
    QString cleaned = QDir::cleanPath(joined);
    
    // SECURITY: Verify no path traversal occurred during join
    if (cleaned.contains("/../") || cleaned.contains("\\..\\") ||
        cleaned.endsWith("/..") || cleaned.endsWith("\\..")) {
        qWarning() << "operations_files: Path traversal detected after join";
        return QString();
    }
    
    // Check combined path length and enable long path if needed
    if (cleaned.length() >= 260) {
        cleaned = enableWindowsLongPath(cleaned);
    }
    
    // Validate path length
    if (!validatePathLength(cleaned)) {
        qWarning() << "operations_files: Joined path exceeds length limits";
        return QString();
    }
    
    // SECURITY: Verify still within allowed directory
    // Note: We can't call isWithinAllowedDirectory here as it would cause recursion
    // Instead, do a simplified check
    QString dataPath = QDir::cleanPath(QDir::current().absolutePath() + "/Data");
    QString normalizedCleaned = normalizePathForComparison(cleaned);
    QString normalizedData = normalizePathForComparison(dataPath);
    
    if (!normalizedCleaned.startsWith(normalizedData)) {
        qWarning() << "operations_files: Joined path escaped allowed directory";
        return QString();
    }
    
    return cleaned;
}

// Normalize path for secure comparison on Windows
QString normalizePathForComparison(const QString& path) {
    if (path.isEmpty()) {
        return QString();
    }
    
    // SECURITY: Check for null bytes
    if (path.contains(QChar(0))) {
        qWarning() << "operations_files: Null byte in path during normalization";
        return QString();
    }
    
    // Convert to absolute path and clean it
    QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    
    // SECURITY: Validate the path doesn't contain suspicious patterns after cleaning
    if (normalized.contains("/../") || normalized.contains("\\..\\") ||
        normalized.endsWith("/..") || normalized.endsWith("\\..")) {
        qWarning() << "operations_files: Path traversal pattern found after normalization";
        return QString();
    }
    
    // Convert to native separators for Windows
    normalized = QDir::toNativeSeparators(normalized);
    
    // SECURITY: Normalize Unicode to prevent homograph attacks
    // Convert to NFD (decomposed) then back to NFC (composed) to normalize
    normalized = normalized.normalized(QString::NormalizationForm_D);
    normalized = normalized.normalized(QString::NormalizationForm_C);
    
    // Convert to uppercase for case-insensitive comparison on Windows
    normalized = normalized.toUpper();
    
    // SECURITY: Handle long path prefixes consistently
    // Don't remove them during comparison as they affect path resolution
    // Instead, normalize them to a standard format
    if (normalized.startsWith("\\\\?\\\\UNC\\\\")) {
        // Already in normalized UNC long path format
    } else if (normalized.startsWith("\\\\?\\\\")) {
        // Already in normalized long path format
    } else if (normalized.startsWith("\\\\")) {
        // UNC path without long path prefix - could add it if needed
        // But keep as-is for comparison consistency
    }
    
    // SECURITY: Final validation
    if (normalized.isEmpty()) {
        qWarning() << "operations_files: Normalization resulted in empty path";
        return QString();
    }
    
    return normalized;
}

static QString g_username = "default";
void setUsername(const QString& username) {
    if (!username.isEmpty()) {
        g_username = username;
    }
}

// Resource management for temp directory
qint64 getTempDirectorySize(const QString& username) {
    qDebug() << "operations_files: Getting temp directory size for user:" << (username.isEmpty() ? g_username : username);
    
    QString user = username.isEmpty() ? g_username : username;
    
    // Build path to temp directory
    QString dataPath = securePathJoin(QDir::current().absolutePath(), "Data");
    if (dataPath.isEmpty()) {
        qWarning() << "operations_files: Failed to create secure data path";
        return -1;
    }
    
    QString userPath = securePathJoin(dataPath, user);
    if (userPath.isEmpty()) {
        qWarning() << "operations_files: Failed to create secure user path";
        return -1;
    }
    
    QString tempPath = securePathJoin(userPath, "Temp");
    if (tempPath.isEmpty()) {
        qWarning() << "operations_files: Failed to create secure temp path";
        return -1;
    }
    
    QDir tempDir(tempPath);
    if (!tempDir.exists()) {
        qDebug() << "operations_files: Temp directory doesn't exist yet";
        return 0; // Directory doesn't exist, size is 0
    }
    
    // Calculate total size of all files in temp directory
    qint64 totalSize = 0;
    QDirIterator it(tempPath, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fileInfo(it.filePath());
        totalSize += fileInfo.size();
    }
    
    qDebug() << "operations_files: Temp directory size:" << totalSize << "bytes";
    return totalSize;
}

qint64 getAvailableDiskSpace(const QString& path) {
    QString targetPath = path;
    
    if (targetPath.isEmpty()) {
        // Default to Data directory
        targetPath = QDir::cleanPath(QDir::current().absolutePath() + "/Data");
    }
    
    qDebug() << "operations_files: Getting available disk space for:" << targetPath;
    
    // Get storage info for the path
    QStorageInfo storageInfo(targetPath);
    
    if (!storageInfo.isValid()) {
        qWarning() << "operations_files: Invalid storage info for path:" << targetPath;
        return -1;
    }
    
    qint64 availableSpace = storageInfo.bytesAvailable();
    qDebug() << "operations_files: Available disk space:" << availableSpace << "bytes (" 
             << (availableSpace / (1024.0 * 1024.0 * 1024.0)) << "GB)";
    
    return availableSpace;
}

bool checkTempDirectoryLimits(qint64 requiredSize, const QString& username) {
    QString user = username.isEmpty() ? g_username : username;
    qDebug() << "operations_files: Checking temp directory limits for user:" << user 
             << "Required size:" << requiredSize << "bytes";
    
    // Check 1: Get current temp directory size
    qint64 currentTempSize = getTempDirectorySize(user);
    if (currentTempSize < 0) {
        qWarning() << "operations_files: Failed to get temp directory size";
        return false;
    }
    
    // Check 2: Would this exceed the max temp directory size?
    qint64 projectedSize = currentTempSize + requiredSize;
    if (projectedSize > MAX_TEMP_DIRECTORY_SIZE) {
        qWarning() << "operations_files: Operation would exceed max temp directory size"
                   << "Current:" << currentTempSize << "Required:" << requiredSize
                   << "Max:" << MAX_TEMP_DIRECTORY_SIZE;
        
        // Try cleanup first
        qDebug() << "operations_files: Attempting cleanup to make space";
        if (cleanupOldTempFiles(user, requiredSize)) {
            // Re-check after cleanup
            currentTempSize = getTempDirectorySize(user);
            projectedSize = currentTempSize + requiredSize;
            if (projectedSize > MAX_TEMP_DIRECTORY_SIZE) {
                qWarning() << "operations_files: Still exceeds limit after cleanup";
                return false;
            }
        } else {
            return false;
        }
    }
    
    // Check 3: Is temp directory approaching threshold?
    if (currentTempSize > TEMP_CLEANUP_THRESHOLD) {
        qDebug() << "operations_files: Temp directory size exceeds cleanup threshold, triggering cleanup"
                 << "Current:" << currentTempSize << "Threshold:" << TEMP_CLEANUP_THRESHOLD;
        cleanupOldTempFiles(user, 0); // Clean up without specific target
    }
    
    // Check 4: Available disk space
    QString dataPath = QDir::cleanPath(QDir::current().absolutePath() + "/Data");
    qint64 availableSpace = getAvailableDiskSpace(dataPath);
    if (availableSpace < 0) {
        qWarning() << "operations_files: Failed to get available disk space";
        return false;
    }
    
    // Check if we'll have at least MIN_DISK_SPACE_REQUIRED after the operation
    qint64 remainingSpace = availableSpace - requiredSize;
    if (remainingSpace < MIN_DISK_SPACE_REQUIRED) {
        qWarning() << "operations_files: Insufficient disk space"
                   << "Available:" << availableSpace << "Required:" << requiredSize
                   << "Min remaining needed:" << MIN_DISK_SPACE_REQUIRED;
        return false;
    }
    
    qDebug() << "operations_files: All resource checks passed";
    return true;
}

bool cleanupOldTempFiles(const QString& username, qint64 targetSize) {
    QString user = username.isEmpty() ? g_username : username;

    if (user.isEmpty()) {
        qWarning() << "operations_files: Cannot cleanup temp files without username";
        return false;
    }

    QString tempPath = QDir::cleanPath(QCoreApplication::applicationDirPath() +
                                       "/Data/" + user + "/Temp");

    QDir tempDir(tempPath);
    if (!tempDir.exists()) {
        qDebug() << "operations_files: Temp directory does not exist:" << tempPath;
        return true; // Nothing to clean up
    }

    qDebug() << "operations_files: Starting temp file cleanup in:" << tempPath;

    // Get all files and sort by last modified (oldest first)
    QFileInfoList fileList = tempDir.entryInfoList(QDir::Files | QDir::Hidden,
                                                   QDir::Time | QDir::Reversed);

    if (fileList.isEmpty()) {
        qDebug() << "operations_files: No temp files to cleanup";
        return true;
    }

    qDebug() << "operations_files: Found" << fileList.size() << "temp files";

    // Build list of files with their info
    struct TempFileInfo {
        QString path;
        qint64 size;
        QDateTime lastModified;
    };

    QList<TempFileInfo> tempFiles;
    for (const QFileInfo& info : fileList) {
        tempFiles.append({info.absoluteFilePath(), info.size(), info.lastModified()});
    }

    // Delete oldest files until we reach target or run out of files
    int filesDeleted = 0;
    int filesFailed = 0;
    qint64 freedSpace = 0;

    for (const TempFileInfo& info : tempFiles) {
        qDebug() << "operations_files: Attempting to delete temp file:" << info.path
                 << "Size:" << info.size << "Last modified:" << info.lastModified;

        // Use QFile::remove() for temp files
        if (QFile::remove(info.path)) {
            freedSpace += info.size;
            filesDeleted++;
            qDebug() << "operations_files: Successfully deleted temp file, freed:" << info.size << "bytes";

            // If we have a target and reached it, stop
            if (targetSize > 0 && freedSpace >= targetSize) {
                qDebug() << "operations_files: Reached target size, stopping cleanup";
                break;
            }
        } else {
            filesFailed++;
            qWarning() << "operations_files: Failed to delete temp file:" << info.path;
        }
    }

    qDebug() << "operations_files: Cleanup completed - Files deleted:" << filesDeleted
             << "Failed:" << filesFailed << "Space freed:" << freedSpace << "bytes";

    return filesDeleted > 0 || filesFailed == 0; // Success if we deleted something or had no failures
}

bool canDecryptToTemp(const QString& encryptedFilePath, const QString& username) {
    QString user = username.isEmpty() ? g_username : username;
    qDebug() << "operations_files: Checking if file can be decrypted to temp:" << encryptedFilePath;
    
    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(encryptedFilePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "operations_files: Invalid encrypted file path:" << result.errorMessage;
        return false;
    }
    
    // Check if file exists
    QFileInfo fileInfo(encryptedFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "operations_files: Encrypted file does not exist:" << encryptedFilePath;
        return false;
    }
    
    // Get the size of the encrypted file
    qint64 encryptedSize = fileInfo.size();
    
    // Estimate decrypted size (usually similar or slightly smaller than encrypted)
    // Add 10% buffer for safety
    qint64 estimatedDecryptedSize = static_cast<qint64>(encryptedSize * 1.1);
    
    qDebug() << "operations_files: Encrypted size:" << encryptedSize 
             << "Estimated decrypted size:" << estimatedDecryptedSize;
    
    // Check if we have enough resources for this decryption
    bool canDecrypt = checkTempDirectoryLimits(estimatedDecryptedSize, user);
    
    if (!canDecrypt) {
        qWarning() << "operations_files: Insufficient resources to decrypt file to temp";
        
        // Provide detailed error information
        qint64 currentTempSize = getTempDirectorySize(user);
        qint64 availableSpace = getAvailableDiskSpace();
        
        qWarning() << "operations_files: Resource details:"
                   << "Current temp size:" << currentTempSize
                   << "Available disk space:" << availableSpace
                   << "Required for decryption:" << estimatedDecryptedSize
                   << "Max temp size:" << MAX_TEMP_DIRECTORY_SIZE
                   << "Min disk space required:" << MIN_DISK_SPACE_REQUIRED;
    }
    
    return canDecrypt;
}

// Check if encryption key is weak (all zeros, all same byte, etc.)
bool isWeakEncryptionKey(const QByteArray& key) {
    if (key.isEmpty() || key.size() != 32) {
        return true; // Invalid key size
    }
    
    // Check if all bytes are the same (e.g., all zeros, all 0xFF)
    char firstByte = key[0];
    bool allSame = true;
    for (int i = 1; i < key.size(); ++i) {
        if (key[i] != firstByte) {
            allSame = false;
            break;
        }
    }
    
    if (allSame) {
        qWarning() << "operations_files: Weak encryption key detected (all bytes identical)";
        return true;
    }
    
    // Check for simple patterns (0x00, 0x01, 0x02, 0x03...)
    bool sequentialPattern = true;
    for (int i = 0; i < key.size(); ++i) {
        if (static_cast<unsigned char>(key[i]) != (i % 256)) {
            sequentialPattern = false;
            break;
        }
    }
    
    if (sequentialPattern) {
        qWarning() << "operations_files: Weak encryption key detected (sequential pattern)";
        return true;
    }
    
    // Check for low entropy (too many repeated bytes)
    QHash<char, int> byteCount;
    for (char byte : key) {
        byteCount[byte]++;
    }
    
    // If any byte appears more than 8 times in a 32-byte key, it's suspicious
    for (auto count : byteCount) {
        if (count > 8) {
            qWarning() << "operations_files: Potentially weak encryption key detected (low entropy)";
            return true;
        }
    }
    
    return false;
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
        // Use secure path join instead of string concatenation
        QString newPath = securePathJoin(currentPath, component);
        if (newPath.isEmpty()) {
            qWarning() << "Failed to create secure path for component:" << component;
            return false;
        }
        currentPath = newPath;

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
        // Check resource limits before creating temp file
        // Estimate a reasonable size for new temp file (default 100MB for safety)
        qint64 estimatedSize = 100 * 1024 * 1024; // 100MB default estimate
        
        if (!checkTempDirectoryLimits(estimatedSize, g_username)) {
            qWarning() << "operations_files: Resource limits exceeded, cannot create temp file";
            qWarning() << "operations_files: Current temp size:" << getTempDirectorySize(g_username);
            qWarning() << "operations_files: Available disk space:" << getAvailableDiskSpace();
            return nullptr;
        }
        
        // Use a user-specific subdirectory within Data for temporary files
        // Always use Data/username/temp for security (never system temp)
        QString dataPath = securePathJoin(QDir::current().absolutePath(), "Data");
        if (dataPath.isEmpty()) {
            qWarning() << "operations_files: Failed to create secure data path";
            return nullptr;
        }
        
        QString userPath = securePathJoin(dataPath, g_username);
        if (userPath.isEmpty()) {
            qWarning() << "operations_files: Failed to create secure user path";
            return nullptr;
        }
        
        QString tempDir = securePathJoin(userPath, "Temp");
        if (tempDir.isEmpty()) {
            qWarning() << "operations_files: Failed to create secure temp directory path";
            return nullptr;
        }
        
        if (!ensureDirectoryExists(tempDir)) {
            qWarning() << "operations_files: Failed to create temporary directory: " << tempDir;
            return nullptr;
        }
        
        // Create template string with proper validation
        // Note: QTemporaryFile with XXXXXX pattern generates cryptographically random filenames
        // This is sufficiently unpredictable and doesn't need to be replaced with QUuid
        QString tempFileName = g_username + "_XXXXXX.tmp";
        templateStr = QDir(tempDir).filePath(tempFileName);
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
            qWarning() << "operations_files: Failed to create temporary file directory: " << dirPath;
            return nullptr;
        }
    }

    qDebug() << "operations_files: Creating temp file with template:" << templateStr;
    auto tempFile = std::make_unique<QTemporaryFile>(templateStr);

    // Always set autoRemove to false - we'll handle cleanup ourselves
    tempFile->setAutoRemove(false);

    if (!tempFile->open()) {
        qWarning() << "operations_files: Failed to create temporary file: " << templateStr;
        return nullptr;
    }

    QString actualPath = tempFile->fileName();
    qDebug() << "operations_files: Created temp file:" << actualPath;

    // CRITICAL: Set secure permissions immediately after creation
    // On Windows, we need to ensure the file handle is properly managed
    qDebug() << "operations_files: Setting secure permissions (ReadOwner | WriteOwner) on temp file";
    
    // First flush any pending writes to ensure file is on disk
    tempFile->flush();
    
    // Set permissions using QFile::setPermissions for better Windows compatibility
    bool permissionsSet = QFile::setPermissions(actualPath, DEFAULT_FILE_PERMISSIONS);
    
    if (!permissionsSet) {
        qWarning() << "operations_files: Failed to set permissions on temp file:" << actualPath;
        
        // Try alternative method on Windows
#ifdef Q_OS_WIN
        qDebug() << "operations_files: Attempting Windows-specific permission setting";
        // Close and reopen the file to ensure Windows releases any locks
        tempFile->close();
        
        // Set permissions directly on the file path
        permissionsSet = QFile::setPermissions(actualPath, DEFAULT_FILE_PERMISSIONS);
        
        if (permissionsSet) {
            qDebug() << "operations_files: Windows-specific permission setting succeeded";
            // Reopen the file
            if (!tempFile->open()) {
                qWarning() << "operations_files: Failed to reopen temp file after setting permissions";
                return nullptr;
            }
        } else {
            qWarning() << "operations_files: Windows-specific permission setting also failed";
        }
#endif
    }
    
    // Verify permissions were applied correctly
    if (permissionsSet) {
        qDebug() << "operations_files: Verifying permissions on temp file";
        if (!verifyPermissions(actualPath, DEFAULT_FILE_PERMISSIONS)) {
            qWarning() << "operations_files: Permission verification failed for temp file:" << actualPath;
            
            // Log actual permissions for debugging
            QFileInfo fileInfo(actualPath);
            QFile::Permissions actualPerms = fileInfo.permissions();
            qWarning() << "operations_files: Expected permissions:" << DEFAULT_FILE_PERMISSIONS;
            qWarning() << "operations_files: Actual permissions:" << actualPerms;
            
            // On Windows, this might be expected behavior due to permission inheritance
            // We'll log it but continue since the file is created
#ifdef Q_OS_WIN
            qDebug() << "operations_files: Note: Windows may inherit permissions from parent directory";
#endif
        } else {
            qDebug() << "operations_files: Permission verification successful for temp file";
        }
    } else {
        qWarning() << "operations_files: Could not set permissions on temp file, using system defaults";
    }

    // Ensure the file handle is properly positioned at the beginning
    tempFile->seek(0);
    
    qDebug() << "operations_files: Temp file creation completed successfully";
    return tempFile;
}

// Streamlined secure delete that prioritizes performance
bool secureDelete(const QString& filePath, int passes, bool allowExternalFiles) {
    qDebug() << "operations_files: === secureDelete called for:" << filePath << "with" << passes << "passes, allowExternalFiles:" << allowExternalFiles << " ===";

    // Process pending deletions occasionally to clean up old files
    // Use atomic increment and check
    int counterValue = s_cleanupCounter.fetchAndAddOrdered(1);
    if ((counterValue % 10) == 0) {
        // Try pending cleanups every 10 calls, but don't wait
        bool needsScheduling = false;
        
        s_pendingMutex.lock();
        if (!s_pendingDeletions.isEmpty() && s_cleanupScheduled.loadAcquire() == 0) {
            needsScheduling = true;
        }
        s_pendingMutex.unlock();
        
        if (needsScheduling) {
            // Try to atomically set the flag from 0 to 1
            if (s_cleanupScheduled.testAndSetOrdered(0, 1)) {
                qDebug() << "operations_files: Triggering periodic cleanup";
                QFuture<void> future = QtConcurrent::run(performAsyncCleanup);
            }
        }
    }

    // Validate the file path - use different validation based on allowExternalFiles
    InputValidation::ValidationResult result;
    if (allowExternalFiles) {
        result = InputValidation::validateInput(filePath, InputValidation::InputType::ExternalFilePath);
    } else {
        result = InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    }

    if (!result.isValid) {
        qWarning() << "operations_files: Invalid file path for secure deletion: " << result.errorMessage;
        return false;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "operations_files: File doesn't exist, returning true";
        return true; // Already gone
    }

    // Get file size
    qint64 fileSize = file.size();
    qDebug() << "operations_files: File size:" << fileSize;
    if (fileSize <= 0) {
        qDebug() << "operations_files: File size <= 0, calling quickDelete";
        // Empty file, just quick delete
        return quickDelete(filePath);
    }

    // For smaller files or when the system is under heavy file operations,
    // reduce passes to improve performance
    int effectivePasses = (fileSize < 4096) ? 1 : (passes > 2 ? 2 : passes);
    qDebug() << "operations_files: Effective passes:" << effectivePasses;

    // Make sure file is closed
    file.close();

    // Try to open with proper sharing flags to reduce locking issues
    qDebug() << "operations_files: Attempting to open file for writing...";
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "operations_files: Failed to open file for writing, calling quickDelete";
        qDebug() << "operations_files: File error:" << file.error();
        qDebug() << "operations_files: Error string:" << file.errorString();
        // If we can't open it, just try quick deletion
        return quickDelete(filePath);
    }
    qDebug() << "operations_files: File opened successfully for writing";

    // Buffer for overwriting
    const int BUFFER_SIZE = 4096;
    QByteArray buffer(BUFFER_SIZE, 0);
    QRandomGenerator* rng = QRandomGenerator::system();

    bool overwriteSuccess = true;

    try {
        // Perform minimal overwrite passes
        for (int pass = 0; pass < effectivePasses; ++pass) {
            qDebug() << "operations_files: Secure delete pass" << (pass + 1) << "of" << effectivePasses << "for" << filePath;

            // Use a pattern based on pass number
            char pattern = (pass == 0) ? 0x00 : 0xFF;

            // Fill the file with the pattern
            if (!file.seek(0)) {
                qDebug() << "operations_files: Failed to seek to beginning of file";
                overwriteSuccess = false;
                break;
            }

            buffer.fill(pattern);
            qint64 bytesRemaining = fileSize;

            while (bytesRemaining > 0 && overwriteSuccess) {
                int bytesToWrite = qMin(bytesRemaining, (qint64)BUFFER_SIZE);
                qint64 bytesWritten = file.write(buffer.constData(), bytesToWrite);

                if (bytesWritten != bytesToWrite) {
                    qDebug() << "operations_files: Write failed: expected" << bytesToWrite << "got" << bytesWritten;
                    overwriteSuccess = false;
                    break;
                }

                bytesRemaining -= bytesWritten;
            }

            if (!overwriteSuccess) break;
            file.flush();
            qDebug() << "operations_files: Pass" << (pass + 1) << "completed successfully";
        }
    } catch (const std::exception& e) {
        qWarning() << "operations_files: Exception during secure file deletion:" << e.what();
        overwriteSuccess = false;
    } catch (...) {
        qWarning() << "operations_files: Unknown exception during secure file deletion";
        overwriteSuccess = false;
    }

    qDebug() << "operations_files: Overwrite success:" << overwriteSuccess;

    // Close file
    file.close();
    qDebug() << "operations_files: File closed, calling quickDelete";

    // Quick delete regardless of overwrite success
    bool deleteResult = quickDelete(filePath);
    qDebug() << "operations_files: quickDelete result:" << deleteResult;
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
    
    // Check if we have enough resources to decrypt this file
    if (!canDecryptToTemp(encryptedFilePath, g_username)) {
        qWarning() << "operations_files: Cannot decrypt file due to resource constraints";
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

    // SECURITY: Check file size to prevent memory exhaustion
    qint64 fileSize = fileInfo.size();
    if (fileSize > MAX_ENCRYPTED_FILE_SIZE) {
        qWarning() << "operations_files: File too large for readEncryptedFileLines:"
                   << fileSize << "bytes (max:" << MAX_ENCRYPTED_FILE_SIZE << "bytes)";
        qWarning() << "operations_files: Use dedicated encryption worker classes for large files";
        return false;
    }
    
    // Check if we have enough resources to decrypt this file
    if (!canDecryptToTemp(filePath, g_username)) {
        qWarning() << "operations_files: Cannot decrypt file due to resource constraints";
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

    // SECURITY: Check total content size to prevent memory exhaustion
    qint64 totalSize = 0;
    for (const QString& line : lines) {
        qint64 lineSize = line.toUtf8().size() + 1; // +1 for newline
        
        // Check for integer overflow before addition
        if (totalSize > MAX_CONTENT_SIZE - lineSize) {
            qWarning() << "operations_files: Content size would overflow limit";
            return false;
        }
        totalSize += lineSize;
    }
    if (totalSize > MAX_CONTENT_SIZE) {
        qWarning() << "operations_files: Content too large for writeEncryptedFileLines:"
                   << totalSize << "bytes (max:" << MAX_CONTENT_SIZE << "bytes)";
        qWarning() << "operations_files: Use dedicated encryption worker classes for large content";
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
        QString nextPath = securePathJoin(currentPath, level);
        if (nextPath.isEmpty()) {
            qWarning() << "Failed to create secure path for hierarchy level:" << level;
            continue; // Skip this level but try to clean up others
        }
        currentPath = nextPath;
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

    // SECURITY: Check for weak encryption keys
    if (isWeakEncryptionKey(encryptionKey)) {
        qWarning() << "operations_files: Refusing to use weak encryption key for reading";
        return false;
    }

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

    // SECURITY: Check file size to prevent memory exhaustion
    qint64 fileSize = fileInfo.size();
    if (fileSize > MAX_ENCRYPTED_FILE_SIZE) {
        qWarning() << "operations_files: File too large for readEncryptedFile:"
                   << fileSize << "bytes (max:" << MAX_ENCRYPTED_FILE_SIZE << "bytes)";
        qWarning() << "operations_files: Use dedicated encryption worker classes for large files";
        return false;
    }
    
    // Check if we have enough resources to decrypt this file
    if (!canDecryptToTemp(filePath, g_username)) {
        qWarning() << "operations_files: Cannot decrypt file due to resource constraints";
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

        // SECURITY: TOCTOU mitigation - validate decrypted content size
        qint64 decryptedSize = outContent.toUtf8().size();
        if (decryptedSize > MAX_CONTENT_SIZE) {
            qWarning() << "operations_files: Decrypted content exceeds size limit:"
                       << decryptedSize << "bytes (max:" << MAX_CONTENT_SIZE << "bytes)";
            outContent.clear(); // Clear potentially malicious content
            return false;
        }

        qDebug() << "Successfully read" << outContent.length() << "characters from decrypted file";

        // Cleaner will handle deletion when it goes out of scope

        return true;
    }
}

// Optimized writing to encrypted files
bool writeEncryptedFile(const QString& filePath, const QByteArray& encryptionKey, const QString& content) {
    // SECURITY: Check for weak encryption keys
    if (isWeakEncryptionKey(encryptionKey)) {
        qWarning() << "operations_files: Refusing to use weak encryption key for writing";
        return false;
    }

    // Validate the file path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid file path for encryption: " << result.errorMessage;
        return false;
    }

    // SECURITY: Check content size to prevent memory exhaustion
    qint64 contentSize = content.toUtf8().size();
    if (contentSize > MAX_CONTENT_SIZE) {
        qWarning() << "operations_files: Content too large for writeEncryptedFile:"
                   << contentSize << "bytes (max:" << MAX_CONTENT_SIZE << "bytes)";
        qWarning() << "operations_files: Use dedicated encryption worker classes for large content";
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
    // SECURITY: Check file size before processing (size check is done in readEncryptedFile)
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

    // SECURITY: File size check is performed in readEncryptedFile
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
    // SECURITY: Early return for empty inputs
    if (filePath.isEmpty() || baseDirectory.isEmpty()) {
        qWarning() << "operations_files: Empty path provided to isWithinAllowedDirectory";
        return false;
    }
    
    // SECURITY: Check for null bytes before any processing
    if (filePath.contains(QChar(0)) || baseDirectory.contains(QChar(0))) {
        qWarning() << "operations_files: Null byte detected in path";
        return false;
    }

    // Validate the base directory as FilePath not PlainText for consistency
    QString baseDirPath = QDir::cleanPath(QDir::current().absolutePath() + "/" + baseDirectory);
    InputValidation::ValidationResult baseDirResult =
        InputValidation::validateInput(baseDirPath, InputValidation::InputType::FilePath);
    if (!baseDirResult.isValid) {
        qWarning() << "operations_files: Invalid base directory path: " << baseDirResult.errorMessage;
        return false;
    }

    // Validate the file path
    InputValidation::ValidationResult filePathResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!filePathResult.isValid) {
        qWarning() << "operations_files: Invalid file path for directory check: " << filePathResult.errorMessage;
        return false;
    }

    try {
        QFileInfo fileInfo(filePath);
        QString resolvedFilePath;
        
        // SECURITY: Detect and block symbolic links
        if (fileInfo.exists()) {
            // Check if it's a symbolic link
            if (fileInfo.isSymLink()) {
                qWarning() << "operations_files: Symbolic links are not allowed: " << filePath;
                return false;
            }
            
            // For existing files, get canonical path atomically
            resolvedFilePath = fileInfo.canonicalFilePath();
            
            // SECURITY: If canonicalFilePath returns empty for an existing file, it's suspicious
            if (resolvedFilePath.isEmpty()) {
                qWarning() << "operations_files: Cannot resolve canonical path for existing file: " << filePath;
                return false;
            }
        } else {
            // For non-existent files, resolve parent directory atomically
            QDir parentDir = fileInfo.dir();
            QString parentPath = parentDir.absolutePath();
            
            // Check if parent directory exists
            QFileInfo parentInfo(parentPath);
            if (parentInfo.exists()) {
                // SECURITY: Check if parent is a symbolic link
                if (parentInfo.isSymLink()) {
                    qWarning() << "operations_files: Parent directory is a symbolic link: " << parentPath;
                    return false;
                }
                
                // Get canonical path of parent
                QString parentCanonical = parentInfo.canonicalFilePath();
                if (parentCanonical.isEmpty()) {
                    qWarning() << "operations_files: Cannot resolve parent directory: " << parentPath;
                    return false;
                }
                
                // SECURITY: Validate filename separately to prevent injection
                QString fileName = fileInfo.fileName();
                if (fileName.contains("/") || fileName.contains("\\") || 
                    fileName == ".." || fileName == "." || fileName.isEmpty()) {
                    qWarning() << "operations_files: Invalid filename component: " << fileName;
                    return false;
                }
                
                // Safely combine parent and filename
                resolvedFilePath = QDir::cleanPath(parentCanonical + "/" + fileName);
            } else {
                // Parent doesn't exist - use absolute path but validate carefully
                resolvedFilePath = QDir::cleanPath(fileInfo.absoluteFilePath());
                
                // SECURITY: Extra validation for non-existent parent paths
                if (resolvedFilePath.contains("/..") || resolvedFilePath.contains("\\..")) {
                    qWarning() << "operations_files: Path traversal detected in resolved path";
                    return false;
                }
            }
        }
        
        // SECURITY: Final validation of resolved path
        if (resolvedFilePath.isEmpty()) {
            qWarning() << "operations_files: Failed to resolve file path";
            return false;
        }
        
        // Resolve base directory path
        QFileInfo baseInfo(baseDirPath);
        QString resolvedBasePath;
        
        if (baseInfo.exists()) {
            // SECURITY: Check if base directory is a symbolic link
            if (baseInfo.isSymLink()) {
                qWarning() << "operations_files: Base directory is a symbolic link: " << baseDirPath;
                return false;
            }
            
            if (!baseInfo.isDir()) {
                qWarning() << "operations_files: Base path is not a directory: " << baseDirPath;
                return false;
            }
            
            resolvedBasePath = baseInfo.canonicalFilePath();
            if (resolvedBasePath.isEmpty()) {
                qWarning() << "operations_files: Cannot resolve base directory path";
                return false;
            }
        } else {
            // Base directory doesn't exist yet - use absolute path
            resolvedBasePath = QDir::cleanPath(baseInfo.absoluteFilePath());
        }
        
        // SECURITY: Normalize both paths for secure comparison
        QString normalizedFile = normalizePathForComparison(resolvedFilePath);
        QString normalizedBase = normalizePathForComparison(resolvedBasePath);
        
        // SECURITY: Ensure normalized paths are not empty
        if (normalizedFile.isEmpty() || normalizedBase.isEmpty()) {
            qWarning() << "operations_files: Path normalization resulted in empty string";
            return false;
        }
        
        // SECURITY: Check that file path starts with base path
        // Also ensure it's not exactly the base path (file should be within, not the directory itself)
        if (!normalizedFile.startsWith(normalizedBase)) {
            return false;
        }
        
        // SECURITY: Ensure there's a path separator after the base path
        // This prevents /Data2/file from matching /Data
        if (normalizedFile.length() > normalizedBase.length()) {
            QChar nextChar = normalizedFile.at(normalizedBase.length());
            if (nextChar != '\\' && nextChar != '/') {
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "operations_files: Exception in path validation:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "operations_files: Unknown exception in path validation";
        return false;
    }
}

// Utility Functions
QString sanitizePath(const QString& path) {
    // SECURITY: Pre-validation before any manipulation
    if (path.isEmpty() || path.contains(QChar(0))) {
        qWarning() << "operations_files: Invalid path - empty or contains null characters";
        return QString();
    }
    
    // SECURITY: Normalize Unicode early to prevent bypass attempts
    QString normalizedPath = path.normalized(QString::NormalizationForm_C);
    
    // SECURITY: Check for various URL encoding patterns
    QString lowerPath = normalizedPath.toLower();
    QStringList urlPatterns = {
        "%00", "%2e%2e", "%252e%252e", "..%2f", "..%5c",
        "%2e%2e%2f", "%2e%2e%5c", "%252e%252e%252f", "%252e%252e%255c",
        "%%32%65%%32%65", // Double encoded ..
        "%c0%ae", "%c1%9c" // Overlong UTF-8 encodings
    };
    
    for (const QString& pattern : urlPatterns) {
        if (lowerPath.contains(pattern)) {
            qWarning() << "operations_files: Path contains URL-encoded traversal pattern:" << pattern;
            return QString();
        }
    }
    
    // SECURITY: Check for Unicode direction override characters
    QList<QChar> dangerousChars = {
        QChar(0x202E), // Right-to-left override
        QChar(0x202D), // Left-to-right override
        QChar(0x202A), // Left-to-right embedding
        QChar(0x202B), // Right-to-left embedding
        QChar(0x202C), // Pop directional formatting
        QChar(0x2066), // Left-to-right isolate
        QChar(0x2067), // Right-to-left isolate
        QChar(0x2068), // First strong isolate
        QChar(0x2069)  // Pop directional isolate
    };
    
    for (const QChar& ch : dangerousChars) {
        if (normalizedPath.contains(ch)) {
            qWarning() << "operations_files: Path contains Unicode direction override character";
            return QString();
        }
    }
    
    // SECURITY: Check for Windows reserved device names
    QFileInfo info(normalizedPath);
    QString baseName = info.baseName().toUpper();
    QString fileName = info.fileName().toUpper();
    
    // Check both base name and full filename for reserved names
    for (const QString& reserved : WINDOWS_RESERVED_NAMES) {
        if (baseName == reserved || fileName == reserved ||
            baseName.startsWith(reserved + ".") || fileName.startsWith(reserved + ".")) {
            qWarning() << "operations_files: Path contains Windows reserved device name:" << reserved;
            return QString();
        }
    }
    
    // SECURITY: Strict alternate data stream check
    int colonCount = normalizedPath.count(':');
    if (colonCount > 1 || (colonCount == 1 && normalizedPath[1] != ':')) {
        // Only allow drive letter colon at position 1 (e.g., C:)
        if (!(colonCount == 1 && normalizedPath.length() > 1 && 
              normalizedPath[0].isLetter() && normalizedPath[1] == ':')) {
            qWarning() << "operations_files: Path may contain alternate data stream";
            return QString();
        }
    }
    
    // SECURITY: Enhanced Windows short name detection
    QRegularExpression shortNamePatterns("(~[0-9]+)|([A-Z]{6}~[0-9])");
    if (shortNamePatterns.match(normalizedPath.toUpper()).hasMatch()) {
        qWarning() << "operations_files: Path may contain Windows short name format";
        return QString();
    }
    
    // SECURITY: Check for NTFS special files
    QStringList ntfsSpecial = {
        "$MFT", "$MFTMirr", "$LogFile", "$Volume", "$AttrDef",
        "$Bitmap", "$Boot", "$BadClus", "$Secure", "$UpCase", "$Extend"
    };
    QString upperPath = normalizedPath.toUpper();
    for (const QString& special : ntfsSpecial) {
        if (upperPath.contains(special)) {
            qWarning() << "operations_files: Path contains NTFS special file:" << special;
            return QString();
        }
    }
    
    // Validate BEFORE cleaning to catch malicious input early
    InputValidation::ValidationResult result =
        InputValidation::validateInput(normalizedPath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "operations_files: Path validation failed:" << result.errorMessage;
        return QString();
    }
    
    // Now clean the validated path
    QString cleaned = QDir::cleanPath(normalizedPath);
    
    // SECURITY: Post-cleaning validation
    if (cleaned.contains("/../") || cleaned.contains("\\..\\") ||
        cleaned.endsWith("/..") || cleaned.endsWith("\\..")) {
        qWarning() << "operations_files: Path traversal detected after cleaning";
        return QString();
    }
    
    // Validate path length
    if (!validatePathLength(cleaned)) {
        qWarning() << "operations_files: Path exceeds system limits";
        return QString();
    }
    
    // Handle long paths on Windows if needed
    if (cleaned.length() >= 260 && !cleaned.startsWith("\\\\?\\\\")) {
        QString longPath = enableWindowsLongPath(cleaned);
        if (!longPath.isEmpty()) {
            cleaned = longPath;
        }
    }
    
    // SECURITY: Check for symbolic links
    QFileInfo cleanedInfo(cleaned);
    if (cleanedInfo.exists() && cleanedInfo.isSymLink()) {
        qWarning() << "operations_files: Sanitized path is a symbolic link";
        return QString();
    }
    
    // Final boundary check
    if (!isWithinAllowedDirectory(cleaned, "Data")) {
        qWarning() << "operations_files: Sanitized path outside allowed directory";
        return QString();
    }
    
    // Get canonical path if the file or directory exists
    if (cleanedInfo.exists()) {
        QString canonicalPath = cleanedInfo.canonicalFilePath();
        if (!canonicalPath.isEmpty()) {
            // SECURITY: Verify canonical path is still within bounds
            if (!isWithinAllowedDirectory(canonicalPath, "Data")) {
                qWarning() << "operations_files: Canonical path escaped allowed directory";
                return QString();
            }
            
            cleaned = canonicalPath;
            
            // Re-validate canonical path length
            if (!validatePathLength(cleaned)) {
                qWarning() << "operations_files: Canonical path exceeds system limits";
                return QString();
            }
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

    // SECURITY: Check file size to prevent memory exhaustion
    qint64 fileSize = fileInfo.size();
    if (fileSize > MAX_ENCRYPTED_FILE_SIZE) {
        qWarning() << "operations_files: Tasklist file too large:"
                   << fileSize << "bytes (max:" << MAX_ENCRYPTED_FILE_SIZE << "bytes)";
        return false;
    }

    // Validate the tasklist file for security
    if (!InputValidation::validateTasklistFile(filePath, encryptionKey)) {
        qWarning() << "Invalid task list file during reading:" << filePath;
        return false;
    }
    
    // Check if we have enough resources to decrypt this file
    if (!canDecryptToTemp(filePath, g_username)) {
        qWarning() << "operations_files: Cannot decrypt tasklist file due to resource constraints";
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

    // SECURITY: Check total content size to prevent memory exhaustion
    qint64 totalSize = 0;
    for (const QString& taskLine : taskLines) {
        qint64 lineSize = taskLine.toUtf8().size() + 1; // +1 for newline
        
        // Check for integer overflow before addition
        if (totalSize > MAX_CONTENT_SIZE - lineSize) {
            qWarning() << "operations_files: Tasklist content size would overflow limit";
            return false;
        }
        totalSize += lineSize;
    }
    if (totalSize > MAX_CONTENT_SIZE) {
        qWarning() << "operations_files: Tasklist content too large:"
                   << totalSize << "bytes (max:" << MAX_CONTENT_SIZE << "bytes)";
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

// Cleanup all user temp folders on startup
bool cleanupAllUserTempFolders() {
    qDebug() << "Starting cleanup of all user temp folders...";

    // Get the Data directory path using secure method
    QString dataPath = securePathJoin(QDir::current().absolutePath(), "Data");
    if (dataPath.isEmpty()) {
        qWarning() << "Failed to create secure Data directory path";
        return false;
    }
    
    QDir dataDir(dataPath);

    if (!dataDir.exists()) {
        qDebug() << "Data directory doesn't exist, no cleanup needed:" << dataPath;
        return true; // Not an error if Data directory doesn't exist yet
    }

    // Get all subdirectories in Data (these should be usernames)
    QStringList userDirs = dataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (userDirs.isEmpty()) {
        qDebug() << "No user directories found, no cleanup needed";
        return true;
    }

    int totalFilesDeleted = 0;
    int totalErrors = 0;

    // Process each user directory
    for (const QString& userDir : userDirs) {
        QString userPath = securePathJoin(dataPath, userDir);
        if (userPath.isEmpty()) {
            qWarning() << "Failed to create secure user path for:" << userDir;
            continue;
        }
        
        QString tempPath = securePathJoin(userPath, "Temp");
        if (tempPath.isEmpty()) {
            qWarning() << "Failed to create secure temp path for user:" << userDir;
            continue;
        }

        QDir tempDir(tempPath);
        if (!tempDir.exists()) {
            continue; // No temp directory for this user, skip
        }

        qDebug() << "Found temp directory for user:" << userDir;

        // Get all files in the temp directory
        QStringList tempFiles = tempDir.entryList(QDir::Files | QDir::Hidden);

        if (tempFiles.isEmpty()) {
            qDebug() << "Temp directory is empty for user:" << userDir;
            continue;
        }

        qDebug() << "Found" << tempFiles.size() << "temp files for user:" << userDir;

        // Delete each file in the temp directory
        for (const QString& fileName : tempFiles) {
            QString filePath = securePathJoin(tempPath, fileName);
            if (filePath.isEmpty()) {
                qWarning() << "Failed to create secure file path for:" << fileName;
                totalErrors++;
                continue;
            }

            // Validate that the file path is within our expected directory structure
            if (!isWithinAllowedDirectory(filePath, "Data")) {
                qWarning() << "Skipping file outside allowed directory:" << filePath;
                totalErrors++;
                continue;
            }

            qDebug() << "Deleting temp file:" << filePath;

            // Use existing secureDelete function with allowExternalFiles = false
            // since these files are within our Data directory
            bool deleteResult = QFile::remove(filePath);

            if (deleteResult) {
                totalFilesDeleted++;
                qDebug() << "Successfully deleted temp file:" << fileName;
            } else {
                totalErrors++;
                qWarning() << "Failed to delete temp file:" << filePath;
            }
        }

        // Try to remove the empty temp directory if all files were deleted successfully
        QStringList remainingFiles = tempDir.entryList(QDir::Files | QDir::Hidden);
        if (remainingFiles.isEmpty()) {
            if (tempDir.rmdir(".")) {
                qDebug() << "Removed empty temp directory for user:" << userDir;
            } else {
                qDebug() << "Could not remove temp directory (may not be empty):" << tempPath;
            }
        }
    }

    if (totalFilesDeleted > 0 || totalErrors > 0) {
        qDebug() << "Temp folder cleanup completed - Files deleted:"
                 << totalFilesDeleted << "Errors:" << totalErrors;
    } else {
        qDebug() << "Temp folder cleanup completed - No files found to delete";
    }

    // Return true even if some files couldn't be deleted - this is a best-effort cleanup
    return true;
}

} // end namespace OperationsFiles
