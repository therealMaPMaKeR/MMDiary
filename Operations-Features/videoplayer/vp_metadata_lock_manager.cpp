#include "vp_metadata_lock_manager.h"
#include "operations_files.h"
#include "inputvalidation.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QThread>
#include <QCryptographicHash>

// Static member initialization
VP_MetadataLockManager* VP_MetadataLockManager::s_instance = nullptr;
QMutex VP_MetadataLockManager::s_instanceMutex;
const QString VP_MetadataLockManager::LOCK_FILE_EXTENSION = ".vpmlock";

// ============================================================================
// LockGuard Implementation
// ============================================================================

VP_MetadataLockManager::LockGuard::LockGuard(VP_MetadataLockManager* manager, const QString& filePath)
    : m_manager(manager)
    , m_filePath(filePath)
    , m_locked(false)
    , m_result(LockResult::Error)
{
    if (m_manager && !m_filePath.isEmpty()) {
        m_result = m_manager->acquireLock(m_filePath);
        m_locked = (m_result == LockResult::Success || m_result == LockResult::StaleLock);
        
        if (m_locked) {
            qDebug() << "VP_MetadataLockManager: LockGuard acquired lock for:" << m_filePath;
        } else {
            qDebug() << "VP_MetadataLockManager: LockGuard failed to acquire lock for:" << m_filePath 
                     << "Result:" << static_cast<int>(m_result);
        }
    }
}

VP_MetadataLockManager::LockGuard::~LockGuard()
{
    if (m_locked && m_manager) {
        m_manager->releaseLock(m_filePath);
        qDebug() << "VP_MetadataLockManager: LockGuard released lock for:" << m_filePath;
    }
}

VP_MetadataLockManager::LockGuard::LockGuard(LockGuard&& other) noexcept
    : m_manager(other.m_manager)
    , m_filePath(std::move(other.m_filePath))
    , m_locked(other.m_locked)
    , m_result(other.m_result)
{
    other.m_manager = nullptr;
    other.m_locked = false;
}

VP_MetadataLockManager::LockGuard& VP_MetadataLockManager::LockGuard::operator=(LockGuard&& other) noexcept
{
    if (this != &other) {
        // Release current lock if any
        if (m_locked && m_manager) {
            m_manager->releaseLock(m_filePath);
        }
        
        m_manager = other.m_manager;
        m_filePath = std::move(other.m_filePath);
        m_locked = other.m_locked;
        m_result = other.m_result;
        
        other.m_manager = nullptr;
        other.m_locked = false;
    }
    return *this;
}

// ============================================================================
// VP_MetadataLockManager Implementation
// ============================================================================

VP_MetadataLockManager::VP_MetadataLockManager()
    : m_locks(10000, "VP_MetadataLockManager::m_locks")  // Allow up to 10000 concurrent locks
    , m_lockTimers(10000, "VP_MetadataLockManager::m_lockTimers")
    , m_staleLockTimeoutMs(DEFAULT_STALE_TIMEOUT_MS)
{
    qDebug() << "VP_MetadataLockManager: Constructor called";
}

VP_MetadataLockManager::~VP_MetadataLockManager()
{
    qDebug() << "VP_MetadataLockManager: Destructor called";
    cleanup();
}

VP_MetadataLockManager* VP_MetadataLockManager::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new VP_MetadataLockManager();
        qDebug() << "VP_MetadataLockManager: Created singleton instance";
    }
    return s_instance;
}

VP_MetadataLockManager::LockResult VP_MetadataLockManager::acquireLock(const QString& filePath, int timeoutMs)
{
    qDebug() << "VP_MetadataLockManager: Attempting to acquire lock for:" << filePath;
    
    // Validate the file path
    InputValidation::ValidationResult validation = InputValidation::validateInput(
        filePath, InputValidation::InputType::FilePath);
    if (!validation.isValid) {
        qWarning() << "VP_MetadataLockManager: Invalid file path:" << validation.errorMessage;
        return LockResult::Error;
    }
    
    // Get or create lock file
    QSharedPointer<QLockFile> lockFile = getLockFile(filePath);
    if (!lockFile) {
        qWarning() << "VP_MetadataLockManager: Failed to create lock file for:" << filePath;
        return LockResult::Error;
    }
    
    // Set stale lock timeout
    lockFile->setStaleLockTime(m_staleLockTimeoutMs);
    
    // Start timer for timeout tracking
    QElapsedTimer timer;
    timer.start();
    m_lockTimers.insert(filePath, timer);
    
    // Try to acquire the lock with timeout
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        if (lockFile->tryLock(LOCK_CHECK_INTERVAL_MS)) {
            qDebug() << "VP_MetadataLockManager: Successfully acquired lock for:" << filePath 
                     << "after" << elapsed << "ms";
            emit lockAcquired(filePath);
            return LockResult::Success;
        }
        
        // Check if it's a stale lock
        if (lockFile->error() == QLockFile::LockFailedError) {
            qint64 pid;
            QString hostname, appname;
            if (lockFile->getLockInfo(&pid, &hostname, &appname)) {
                qDebug() << "VP_MetadataLockManager: Lock held by PID:" << pid 
                         << "Host:" << hostname << "App:" << appname;
                
                // Check if the lock is stale
                if (lockFile->removeStaleLockFile()) {
                    qDebug() << "VP_MetadataLockManager: Removed stale lock for:" << filePath;
                    emit staleLockRemoved(filePath);
                    
                    // Try to acquire again
                    if (lockFile->tryLock(0)) {
                        qDebug() << "VP_MetadataLockManager: Acquired lock after removing stale lock";
                        emit lockAcquired(filePath);
                        return LockResult::StaleLock;
                    }
                }
            }
        }
        
        elapsed = timer.elapsed();
        
        // Allow processing of events to prevent UI freeze
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            QCoreApplication::processEvents();
        } else {
            QThread::msleep(LOCK_CHECK_INTERVAL_MS);
        }
    }
    
    qWarning() << "VP_MetadataLockManager: Timeout acquiring lock for:" << filePath 
                << "after" << elapsed << "ms";
    emit lockTimeout(filePath, elapsed);
    m_lockTimers.remove(filePath);
    return LockResult::Timeout;
}

bool VP_MetadataLockManager::releaseLock(const QString& filePath)
{
    qDebug() << "VP_MetadataLockManager: Releasing lock for:" << filePath;
    
    // Try to get the lock file from the map
    auto lockFileOpt = m_locks.value(filePath);
    if (lockFileOpt.has_value()) {
        QSharedPointer<QLockFile> lockFile = lockFileOpt.value();
        if (lockFile && lockFile->isLocked()) {
            lockFile->unlock();
            qDebug() << "VP_MetadataLockManager: Released lock for:" << filePath;
            emit lockReleased(filePath);
            
            // Remove from timers
            m_lockTimers.remove(filePath);
            
            // Remove the lock file from our map
            m_locks.remove(filePath);
            
            // Delete the actual lock file
            QString lockFilePath = getLockFilePath(filePath);
            if (QFile::exists(lockFilePath)) {
                QFile::remove(lockFilePath);
                qDebug() << "VP_MetadataLockManager: Deleted lock file:" << lockFilePath;
            }
            
            return true;
        }
    }
    
    qDebug() << "VP_MetadataLockManager: No lock found for:" << filePath;
    return false;
}

bool VP_MetadataLockManager::isLocked(const QString& filePath) const
{
    // Check if we have the lock in our map
    auto lockFileOpt = m_locks.value(filePath);
    if (lockFileOpt.has_value()) {
        QSharedPointer<QLockFile> lockFile = lockFileOpt.value();
        return lockFile && lockFile->isLocked();
    }
    
    // Check if a lock file exists on disk (might be locked by another process)
    QString lockFilePath = getLockFilePath(filePath);
    if (QFile::exists(lockFilePath)) {
        QLockFile tempLock(lockFilePath);
        return !tempLock.tryLock(0);  // If we can't lock it, it's already locked
    }
    
    return false;
}

bool VP_MetadataLockManager::removeStaleLock(const QString& filePath)
{
    qDebug() << "VP_MetadataLockManager: Attempting to remove stale lock for:" << filePath;
    
    QString lockFilePath = getLockFilePath(filePath);
    
    // Create a temporary lock file to check
    QLockFile tempLock(lockFilePath);
    tempLock.setStaleLockTime(m_staleLockTimeoutMs);
    
    if (tempLock.removeStaleLockFile()) {
        qDebug() << "VP_MetadataLockManager: Successfully removed stale lock for:" << filePath;
        emit staleLockRemoved(filePath);
        
        // Remove from our maps if they exist
        m_locks.remove(filePath);
        m_lockTimers.remove(filePath);
        
        return true;
    }
    
    return false;
}

int VP_MetadataLockManager::activeLocksCount() const
{
    int count = 0;
    
    // Use safe iteration to count active locks (QMap iteration with key-value pairs)
    m_locks.safeIterate([&count](const QString& key, const QSharedPointer<QLockFile>& lockFile) {
        if (lockFile && lockFile->isLocked()) {
            count++;
        }
    });
    
    return count;
}

void VP_MetadataLockManager::cleanup()
{
    qDebug() << "VP_MetadataLockManager: Starting cleanup of all locks";
    
    // Get a snapshot of all locks to avoid holding the lock during file operations
    QMap<QString, QSharedPointer<QLockFile>> locksCopy = m_locks.getCopy();
    
    // Release all locks
    for (auto it = locksCopy.begin(); it != locksCopy.end(); ++it) {
        if (it.value() && it.value()->isLocked()) {
            it.value()->unlock();
            qDebug() << "VP_MetadataLockManager: Released lock for:" << it.key();
            
            // Delete the lock file
            QString lockFilePath = getLockFilePath(it.key());
            if (QFile::exists(lockFilePath)) {
                QFile::remove(lockFilePath);
            }
        }
    }
    
    // Clear the containers
    m_locks.clear();
    m_lockTimers.clear();
    
    // Clean up old lock files in the temp directory
    cleanupOldLocks();
    
    qDebug() << "VP_MetadataLockManager: Cleanup completed";
}

void VP_MetadataLockManager::setStaleLockTimeout(int seconds)
{
    m_staleLockTimeoutMs = seconds * 1000;
    qDebug() << "VP_MetadataLockManager: Set stale lock timeout to" << seconds << "seconds";
}

QSharedPointer<QLockFile> VP_MetadataLockManager::getLockFile(const QString& filePath)
{
    // Check if we already have a lock file for this path
    auto existingLock = m_locks.value(filePath);
    if (existingLock.has_value()) {
        return existingLock.value();
    }
    
    // Create a new lock file
    QString lockFilePath = getLockFilePath(filePath);
    
    // Ensure the directory exists
    QFileInfo lockFileInfo(lockFilePath);
    QDir lockDir = lockFileInfo.dir();
    if (!lockDir.exists()) {
        if (!lockDir.mkpath(".")) {
            qWarning() << "VP_MetadataLockManager: Failed to create lock directory:" << lockDir.path();
            return QSharedPointer<QLockFile>();
        }
    }
    
    QSharedPointer<QLockFile> lockFile = QSharedPointer<QLockFile>::create(lockFilePath);
    m_locks.insert(filePath, lockFile);
    
    qDebug() << "VP_MetadataLockManager: Created lock file:" << lockFilePath;
    return lockFile;
}

QString VP_MetadataLockManager::getLockFilePath(const QString& videoFilePath) const
{
    // Generate a unique lock file name based on the video file path
    // Use a hash to avoid issues with long paths or special characters
    QByteArray pathBytes = videoFilePath.toUtf8();
    QByteArray hash = QCryptographicHash::hash(pathBytes, QCryptographicHash::Sha256);
    QString hashString = QString::fromLatin1(hash.toHex());
    
    // Get the app's temp_metadata_locks directory (separate from temp to avoid cleanup)
    QString tempDir = QCoreApplication::applicationDirPath() + "/Data/" + 
                     OperationsFiles::getUsername() + "/temp_metadata_locks";
    
    // Ensure the directory exists
    QDir dir(tempDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // Create the lock file path
    QFileInfo fileInfo(videoFilePath);
    QString lockFileName = fileInfo.fileName() + "_" + hashString.left(8) + LOCK_FILE_EXTENSION;
    
    return tempDir + "/" + lockFileName;
}

void VP_MetadataLockManager::cleanupOldLocks()
{
    qDebug() << "VP_MetadataLockManager: Cleaning up old lock files";
    
    QString tempDir = QCoreApplication::applicationDirPath() + "/Data/" + 
                     OperationsFiles::getUsername() + "/temp_metadata_locks";
    
    QDir dir(tempDir);
    if (!dir.exists()) {
        return;
    }
    
    // Get all lock files
    QStringList filters;
    filters << "*" + LOCK_FILE_EXTENSION;
    QFileInfoList lockFiles = dir.entryInfoList(filters, QDir::Files);
    
    int removedCount = 0;
    for (const QFileInfo& fileInfo : lockFiles) {
        // Try to lock the file - if we can, it's not in use
        QLockFile tempLock(fileInfo.absoluteFilePath());
        if (tempLock.tryLock(0)) {
            tempLock.unlock();
            
            // Remove the orphaned lock file
            if (QFile::remove(fileInfo.absoluteFilePath())) {
                removedCount++;
                qDebug() << "VP_MetadataLockManager: Removed orphaned lock file:" << fileInfo.fileName();
            }
        }
    }
    
    if (removedCount > 0) {
        qDebug() << "VP_MetadataLockManager: Cleaned up" << removedCount << "orphaned lock files";
    }
}