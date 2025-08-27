#include "vp_shows_metadata_lock_manager.h"
#include "operations_files.h"
#include "inputvalidation.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QThread>
#include <QCryptographicHash>

// Static member initialization
VP_ShowsMetadataLockManager* VP_ShowsMetadataLockManager::s_instance = nullptr;
QMutex VP_ShowsMetadataLockManager::s_instanceMutex;
const QString VP_ShowsMetadataLockManager::LOCK_FILE_EXTENSION = ".vpmlock";

// ============================================================================
// LockGuard Implementation
// ============================================================================

VP_ShowsMetadataLockManager::LockGuard::LockGuard(VP_ShowsMetadataLockManager* manager, const QString& filePath)
    : m_manager(manager)
    , m_filePath(filePath)
    , m_locked(false)
    , m_result(LockResult::Error)
{
    if (m_manager && !m_filePath.isEmpty()) {
        m_result = m_manager->acquireLock(m_filePath);
        m_locked = (m_result == LockResult::Success || m_result == LockResult::StaleLock);
        
        if (m_locked) {
            qDebug() << "VP_ShowsMetadataLockManager: LockGuard acquired lock for:" << m_filePath;
        } else {
            qDebug() << "VP_ShowsMetadataLockManager: LockGuard failed to acquire lock for:" << m_filePath 
                     << "Result:" << static_cast<int>(m_result);
        }
    }
}

VP_ShowsMetadataLockManager::LockGuard::~LockGuard()
{
    if (m_locked && m_manager) {
        m_manager->releaseLock(m_filePath);
        qDebug() << "VP_ShowsMetadataLockManager: LockGuard released lock for:" << m_filePath;
    }
}

VP_ShowsMetadataLockManager::LockGuard::LockGuard(LockGuard&& other) noexcept
    : m_manager(other.m_manager)
    , m_filePath(std::move(other.m_filePath))
    , m_locked(other.m_locked)
    , m_result(other.m_result)
{
    other.m_manager = nullptr;
    other.m_locked = false;
}

VP_ShowsMetadataLockManager::LockGuard& VP_ShowsMetadataLockManager::LockGuard::operator=(LockGuard&& other) noexcept
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
// VP_ShowsMetadataLockManager Implementation
// ============================================================================

VP_ShowsMetadataLockManager::VP_ShowsMetadataLockManager()
    : m_staleLockTimeoutMs(DEFAULT_STALE_TIMEOUT_MS)
{
    qDebug() << "VP_ShowsMetadataLockManager: Constructor called";
}

VP_ShowsMetadataLockManager::~VP_ShowsMetadataLockManager()
{
    qDebug() << "VP_ShowsMetadataLockManager: Destructor called";
    cleanup();
}

VP_ShowsMetadataLockManager* VP_ShowsMetadataLockManager::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new VP_ShowsMetadataLockManager();
        qDebug() << "VP_ShowsMetadataLockManager: Created singleton instance";
    }
    return s_instance;
}

VP_ShowsMetadataLockManager::LockResult VP_ShowsMetadataLockManager::acquireLock(const QString& filePath, int timeoutMs)
{
    qDebug() << "VP_ShowsMetadataLockManager: Attempting to acquire lock for:" << filePath;
    
    // Validate the file path
    InputValidation::ValidationResult validation = InputValidation::validateInput(
        filePath, InputValidation::InputType::FilePath);
    if (!validation.isValid) {
        qWarning() << "VP_ShowsMetadataLockManager: Invalid file path:" << validation.errorMessage;
        return LockResult::Error;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Get or create lock file
    QSharedPointer<QLockFile> lockFile = getLockFile(filePath);
    if (!lockFile) {
        qWarning() << "VP_ShowsMetadataLockManager: Failed to create lock file for:" << filePath;
        return LockResult::Error;
    }
    
    // Set stale lock timeout
    lockFile->setStaleLockTime(m_staleLockTimeoutMs);
    
    // Start timer for timeout tracking
    QElapsedTimer timer;
    timer.start();
    m_lockTimers[filePath] = timer;
    
    // Try to acquire the lock with timeout
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        if (lockFile->tryLock(LOCK_CHECK_INTERVAL_MS)) {
            qDebug() << "VP_ShowsMetadataLockManager: Successfully acquired lock for:" << filePath 
                     << "after" << elapsed << "ms";
            emit lockAcquired(filePath);
            return LockResult::Success;
        }
        
        // Check if it's a stale lock
        if (lockFile->error() == QLockFile::LockFailedError) {
            qint64 pid;
            QString hostname, appname;
            if (lockFile->getLockInfo(&pid, &hostname, &appname)) {
                qDebug() << "VP_ShowsMetadataLockManager: Lock held by PID:" << pid 
                         << "Host:" << hostname << "App:" << appname;
                
                // Check if the lock is stale
                if (lockFile->removeStaleLockFile()) {
                    qDebug() << "VP_ShowsMetadataLockManager: Removed stale lock for:" << filePath;
                    emit staleLockRemoved(filePath);
                    
                    // Try to acquire again
                    if (lockFile->tryLock(0)) {
                        qDebug() << "VP_ShowsMetadataLockManager: Acquired lock after removing stale lock";
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
    
    qWarning() << "VP_ShowsMetadataLockManager: Timeout acquiring lock for:" << filePath 
                << "after" << elapsed << "ms";
    emit lockTimeout(filePath, elapsed);
    m_lockTimers.remove(filePath);
    return LockResult::Timeout;
}

bool VP_ShowsMetadataLockManager::releaseLock(const QString& filePath)
{
    qDebug() << "VP_ShowsMetadataLockManager: Releasing lock for:" << filePath;
    
    QMutexLocker locker(&m_mutex);
    
    if (m_locks.contains(filePath)) {
        QSharedPointer<QLockFile> lockFile = m_locks[filePath];
        if (lockFile && lockFile->isLocked()) {
            lockFile->unlock();
            qDebug() << "VP_ShowsMetadataLockManager: Released lock for:" << filePath;
            emit lockReleased(filePath);
            
            // Remove from timers
            m_lockTimers.remove(filePath);
            
            // Remove the lock file from our map
            m_locks.remove(filePath);
            
            // Delete the actual lock file
            QString lockFilePath = getLockFilePath(filePath);
            if (QFile::exists(lockFilePath)) {
                QFile::remove(lockFilePath);
                qDebug() << "VP_ShowsMetadataLockManager: Deleted lock file:" << lockFilePath;
            }
            
            return true;
        }
    }
    
    qDebug() << "VP_ShowsMetadataLockManager: No lock found for:" << filePath;
    return false;
}

bool VP_ShowsMetadataLockManager::isLocked(const QString& filePath) const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_locks.contains(filePath)) {
        QSharedPointer<QLockFile> lockFile = m_locks[filePath];
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

bool VP_ShowsMetadataLockManager::removeStaleLock(const QString& filePath)
{
    qDebug() << "VP_ShowsMetadataLockManager: Attempting to remove stale lock for:" << filePath;
    
    QMutexLocker locker(&m_mutex);
    
    QString lockFilePath = getLockFilePath(filePath);
    
    // Create a temporary lock file to check
    QLockFile tempLock(lockFilePath);
    tempLock.setStaleLockTime(m_staleLockTimeoutMs);
    
    if (tempLock.removeStaleLockFile()) {
        qDebug() << "VP_ShowsMetadataLockManager: Successfully removed stale lock for:" << filePath;
        emit staleLockRemoved(filePath);
        
        // Remove from our map if it exists
        m_locks.remove(filePath);
        m_lockTimers.remove(filePath);
        
        return true;
    }
    
    return false;
}

int VP_ShowsMetadataLockManager::activeLocksCount() const
{
    QMutexLocker locker(&m_mutex);
    int count = 0;
    
    for (auto it = m_locks.constBegin(); it != m_locks.constEnd(); ++it) {
        if (it.value() && it.value()->isLocked()) {
            count++;
        }
    }
    
    return count;
}

void VP_ShowsMetadataLockManager::cleanup()
{
    qDebug() << "VP_ShowsMetadataLockManager: Starting cleanup of all locks";
    
    QMutexLocker locker(&m_mutex);
    
    // Release all locks
    for (auto it = m_locks.begin(); it != m_locks.end(); ++it) {
        if (it.value() && it.value()->isLocked()) {
            it.value()->unlock();
            qDebug() << "VP_ShowsMetadataLockManager: Released lock for:" << it.key();
            
            // Delete the lock file
            QString lockFilePath = getLockFilePath(it.key());
            if (QFile::exists(lockFilePath)) {
                QFile::remove(lockFilePath);
            }
        }
    }
    
    m_locks.clear();
    m_lockTimers.clear();
    
    // Clean up old lock files in the temp directory
    cleanupOldLocks();
    
    qDebug() << "VP_ShowsMetadataLockManager: Cleanup completed";
}

void VP_ShowsMetadataLockManager::setStaleLockTimeout(int seconds)
{
    m_staleLockTimeoutMs = seconds * 1000;
    qDebug() << "VP_ShowsMetadataLockManager: Set stale lock timeout to" << seconds << "seconds";
}

QSharedPointer<QLockFile> VP_ShowsMetadataLockManager::getLockFile(const QString& filePath)
{
    // Check if we already have a lock file for this path
    if (m_locks.contains(filePath)) {
        return m_locks[filePath];
    }
    
    // Create a new lock file
    QString lockFilePath = getLockFilePath(filePath);
    
    // Ensure the directory exists
    QFileInfo lockFileInfo(lockFilePath);
    QDir lockDir = lockFileInfo.dir();
    if (!lockDir.exists()) {
        if (!lockDir.mkpath(".")) {
            qWarning() << "VP_ShowsMetadataLockManager: Failed to create lock directory:" << lockDir.path();
            return QSharedPointer<QLockFile>();
        }
    }
    
    QSharedPointer<QLockFile> lockFile = QSharedPointer<QLockFile>::create(lockFilePath);
    m_locks[filePath] = lockFile;
    
    qDebug() << "VP_ShowsMetadataLockManager: Created lock file:" << lockFilePath;
    return lockFile;
}

QString VP_ShowsMetadataLockManager::getLockFilePath(const QString& videoFilePath) const
{
    // Generate a unique lock file name based on the video file path
    // Use a hash to avoid issues with long paths or special characters
    QByteArray pathBytes = videoFilePath.toUtf8();
    QByteArray hash = QCryptographicHash::hash(pathBytes, QCryptographicHash::Sha256);
    QString hashString = QString::fromLatin1(hash.toHex());
    
    // Get the app's temp directory
    QString tempDir = QCoreApplication::applicationDirPath() + "/Data/" + 
                     QDir::home().dirName() + "/temp/vp_shows_locks";
    
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

void VP_ShowsMetadataLockManager::cleanupOldLocks()
{
    qDebug() << "VP_ShowsMetadataLockManager: Cleaning up old lock files";
    
    QString tempDir = QCoreApplication::applicationDirPath() + "/Data/" + 
                     QDir::home().dirName() + "/temp/vp_shows_locks";
    
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
                qDebug() << "VP_ShowsMetadataLockManager: Removed orphaned lock file:" << fileInfo.fileName();
            }
        }
    }
    
    if (removedCount > 0) {
        qDebug() << "VP_ShowsMetadataLockManager: Cleaned up" << removedCount << "orphaned lock files";
    }
}