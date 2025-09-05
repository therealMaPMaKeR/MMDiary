#ifndef VP_METADATA_LOCK_MANAGER_H
#define VP_METADATA_LOCK_MANAGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QMap>
#include <QSharedPointer>
#include <QLockFile>
#include <QElapsedTimer>
#include <memory>
#include "Operations-Global/ThreadSafeContainers.h"

/**
 * @class VP_MetadataLockManager
 * @brief Manages file locking for video metadata operations to prevent concurrent access issues
 * 
 * This class provides thread-safe file locking mechanism to ensure that only one operation
 * can access a video file's metadata at a time. This prevents corruption and crashes when
 * multiple parts of the application try to read/write metadata simultaneously.
 * 
 * Can be used for TV shows, movies, and other video content types.
 */
class VP_MetadataLockManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Lock acquisition result
     */
    enum class LockResult {
        Success,           // Lock acquired successfully
        Timeout,          // Failed to acquire lock within timeout
        StaleLock,        // A stale lock was detected and removed
        Error             // An error occurred
    };

    /**
     * @brief RAII wrapper for automatic lock release
     */
    class LockGuard {
    public:
        LockGuard(VP_MetadataLockManager* manager, const QString& filePath);
        ~LockGuard();
        
        // Disable copy
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
        
        // Enable move
        LockGuard(LockGuard&& other) noexcept;
        LockGuard& operator=(LockGuard&& other) noexcept;
        
        bool isLocked() const { return m_locked; }
        LockResult result() const { return m_result; }
        
    private:
        VP_MetadataLockManager* m_manager;
        QString m_filePath;
        bool m_locked;
        LockResult m_result;
    };

    /**
     * @brief Get the singleton instance
     */
    static VP_MetadataLockManager* instance();
    
    /**
     * @brief Destructor
     */
    ~VP_MetadataLockManager();

    /**
     * @brief Acquire a lock for the specified file
     * @param filePath Path to the video file
     * @param timeoutMs Timeout in milliseconds (default: 5000ms)
     * @return LockResult indicating success or failure reason
     */
    LockResult acquireLock(const QString& filePath, int timeoutMs = 5000);
    
    /**
     * @brief Release a lock for the specified file
     * @param filePath Path to the video file
     * @return true if lock was released, false if no lock existed
     */
    bool releaseLock(const QString& filePath);
    
    /**
     * @brief Check if a file is currently locked
     * @param filePath Path to the video file
     * @return true if file is locked
     */
    bool isLocked(const QString& filePath) const;
    
    /**
     * @brief Force remove a stale lock (use with caution)
     * @param filePath Path to the video file
     * @return true if stale lock was removed
     */
    bool removeStaleLock(const QString& filePath);
    
    /**
     * @brief Get the number of active locks
     */
    int activeLocksCount() const;
    
    /**
     * @brief Clean up all locks (called on application shutdown)
     */
    void cleanup();

    /**
     * @brief Set the stale lock timeout (default: 30 seconds)
     * @param seconds Timeout in seconds after which a lock is considered stale
     */
    void setStaleLockTimeout(int seconds);

signals:
    /**
     * @brief Emitted when a lock is acquired
     */
    void lockAcquired(const QString& filePath);
    
    /**
     * @brief Emitted when a lock is released
     */
    void lockReleased(const QString& filePath);
    
    /**
     * @brief Emitted when a stale lock is detected and removed
     */
    void staleLockRemoved(const QString& filePath);
    
    /**
     * @brief Emitted when lock acquisition times out
     */
    void lockTimeout(const QString& filePath, int waitedMs);

private:
    // Private constructor for singleton
    VP_MetadataLockManager();
    
    // Disable copy
    VP_MetadataLockManager(const VP_MetadataLockManager&) = delete;
    VP_MetadataLockManager& operator=(const VP_MetadataLockManager&) = delete;
    
    /**
     * @brief Get or create a lock file for the specified path
     */
    QSharedPointer<QLockFile> getLockFile(const QString& filePath);
    
    /**
     * @brief Generate lock file path from video file path
     */
    QString getLockFilePath(const QString& videoFilePath) const;
    
    /**
     * @brief Clean up old lock files that are no longer needed
     */
    void cleanupOldLocks();

private:
    static VP_MetadataLockManager* s_instance;
    static QMutex s_instanceMutex;
    
    // Using ThreadSafeMap to prevent race conditions
    ThreadSafeMap<QString, QSharedPointer<QLockFile>> m_locks;
    ThreadSafeMap<QString, QElapsedTimer> m_lockTimers;
    
    int m_staleLockTimeoutMs;
    
    // Constants
    static const int DEFAULT_STALE_TIMEOUT_MS = 30000;  // 30 seconds
    static const int LOCK_CHECK_INTERVAL_MS = 100;      // Check every 100ms when waiting
    static const QString LOCK_FILE_EXTENSION;           // ".vpmlock"
};

#endif // VP_METADATA_LOCK_MANAGER_H
