#ifndef SAFETIMER_H
#define SAFETIMER_H

#include <QObject>
#include <QTimer>
#include <QPointer>
#include <QDebug>
#include <functional>
#include <QMutex>
#include <QMutexLocker>

/**
 * @class SafeTimer
 * @brief A safe wrapper around QTimer that prevents dangling pointer access and ensures proper cleanup
 * 
 * This class addresses security vulnerabilities related to timer usage:
 * - Prevents access to deleted objects in timer callbacks
 * - Ensures proper cleanup of timers in destructors
 * - Provides safe single-shot operations with automatic parent checking
 * - Thread-safe timer management
 * 
 * Usage Examples:
 * 
 * 1. Regular timer with safe callback:
 * @code
 * SafeTimer* timer = new SafeTimer(this);
 * timer->setInterval(1000);
 * timer->start([this]() {
 *     // Your callback code - automatically checks if parent still exists
 *     updateUI();
 * });
 * @endcode
 * 
 * 2. Single-shot timer:
 * @code
 * SafeTimer::singleShot(2000, this, [this]() {
 *     // Executes after 2 seconds only if parent still exists
 *     performDelayedAction();
 * });
 * @endcode
 * 
 * 3. Safe cleanup (automatic in destructor):
 * @code
 * delete timer; // Automatically stops timer and cleans up safely
 * @endcode
 */
class SafeTimer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param parent Parent QObject (required for safety checks)
     * @param debugPrefix Optional debug prefix for logging (defaults to "SafeTimer")
     */
    explicit SafeTimer(QObject* parent, const QString& debugPrefix = "SafeTimer");
    
    /**
     * @brief Destructor - ensures safe cleanup
     */
    virtual ~SafeTimer();
    
    // ============= Core Timer Functions =============
    
    /**
     * @brief Start the timer with a safe callback
     * @param callback Function to execute on timeout (only called if parent exists)
     * @return true if timer started successfully
     */
    bool start(std::function<void()> callback);
    
    /**
     * @brief Start the timer with interval and callback
     * @param msec Interval in milliseconds
     * @param callback Function to execute on timeout
     * @return true if timer started successfully
     */
    bool start(int msec, std::function<void()> callback);
    
    /**
     * @brief Stop the timer
     */
    void stop();
    
    /**
     * @brief Check if timer is active
     * @return true if timer is running
     */
    bool isActive() const;
    
    /**
     * @brief Set timer interval
     * @param msec Interval in milliseconds
     */
    void setInterval(int msec);
    
    /**
     * @brief Get timer interval
     * @return Interval in milliseconds
     */
    int interval() const;
    
    /**
     * @brief Set single shot mode
     * @param singleShot true for single shot, false for repeating
     */
    void setSingleShot(bool singleShot);
    
    /**
     * @brief Check if timer is in single shot mode
     * @return true if single shot mode
     */
    bool isSingleShot() const;
    
    // ============= Static Single Shot Functions =============
    
    /**
     * @brief Safe single-shot timer with parent checking
     * @param msec Delay in milliseconds
     * @param parent Parent object (must exist when timer fires)
     * @param callback Function to execute
     * @param debugPrefix Optional debug prefix for logging
     * 
     * This is the RECOMMENDED way to use single-shot timers safely.
     * The callback will only execute if the parent object still exists.
     */
    static void singleShot(int msec, QObject* parent, std::function<void()> callback, 
                           const QString& debugPrefix = "SafeTimer");
    
    /**
     * @brief Safe single-shot timer with multiple parent checking
     * @param msec Delay in milliseconds
     * @param parent Primary parent object
     * @param additionalParents Additional objects that must exist for callback to execute
     * @param callback Function to execute
     * @param debugPrefix Optional debug prefix for logging
     * 
     * Use this when your callback depends on multiple objects existing.
     * The callback only executes if ALL specified objects still exist.
     */
    static void singleShotMultiCheck(int msec, QObject* parent, 
                                     const QList<QPointer<QObject>>& additionalParents,
                                     std::function<void()> callback,
                                     const QString& debugPrefix = "SafeTimer");
    
    // ============= Connection Management =============
    
    /**
     * @brief Connect to the timeout signal with automatic disconnection on parent deletion
     * @param receiver Object to receive the signal
     * @param method Slot to call
     * @return QMetaObject::Connection for manual disconnection if needed
     */
    template<typename Func>
    QMetaObject::Connection connectTimeout(const typename QtPrivate::FunctionPointer<Func>::Object *receiver, 
                                           Func method) {
        if (!m_timer || !receiver) {
            qWarning() << m_debugPrefix << "Cannot connect - timer or receiver is null";
            return QMetaObject::Connection();
        }
        
        // Store receiver for safety checking
        m_receivers.append(QPointer<QObject>(const_cast<QObject*>(
            static_cast<const QObject*>(receiver))));
        
        return QObject::connect(m_timer, &QTimer::timeout, receiver, method);
    }
    
    /**
     * @brief Disconnect all connections
     */
    void disconnectAll();
    
    // ============= Safety Utilities =============
    
    /**
     * @brief Check if the timer's parent still exists
     * @return true if parent exists and is valid
     */
    bool isParentValid() const;
    
    /**
     * @brief Get remaining time until next timeout (for active timers)
     * @return Milliseconds until timeout, -1 if timer is not active
     */
    int remainingTime() const;
    
    /**
     * @brief Create a safe timer or return nullptr if parent is invalid
     * @param parent Parent object
     * @param debugPrefix Optional debug prefix
     * @return New SafeTimer instance or nullptr
     */
    static SafeTimer* create(QObject* parent, const QString& debugPrefix = "SafeTimer");

signals:
    /**
     * @brief Emitted when timer times out (only if parent still exists)
     */
    void timeout();
    
    /**
     * @brief Emitted when timer is stopped
     */
    void stopped();
    
    /**
     * @brief Emitted when timer is started
     */
    void started();

private slots:
    /**
     * @brief Internal slot to handle timer timeout
     */
    void onTimerTimeout();
    
    /**
     * @brief Internal slot to handle parent destruction
     */
    void onParentDestroyed();

private:
    // Disable copy constructor and assignment operator
    SafeTimer(const SafeTimer&) = delete;
    SafeTimer& operator=(const SafeTimer&) = delete;
    
    // Internal members
    QPointer<QTimer> m_timer;                    // The actual timer (QPointer for safety)
    QPointer<QObject> m_parent;                  // Parent object (monitored for deletion)
    std::function<void()> m_callback;            // User callback function
    QString m_debugPrefix;                        // Debug prefix for logging
    mutable QMutex m_mutex;                       // Mutex for thread safety
    bool m_isCleaningUp;                          // Flag to prevent recursive cleanup
    QList<QPointer<QObject>> m_receivers;        // Objects connected to this timer
    
    /**
     * @brief Internal cleanup method
     */
    void cleanup();
    
    /**
     * @brief Check if all receivers are still valid
     * @return true if all receivers exist
     */
    bool areReceiversValid() const;
};

/**
 * @class SafeTimerManager
 * @brief Manages multiple SafeTimer instances for a parent object
 * 
 * This class provides centralized management of multiple timers,
 * ensuring all are properly cleaned up when the parent is destroyed.
 * 
 * Usage:
 * @code
 * class MyClass : public QObject {
 *     SafeTimerManager m_timerManager;
 * public:
 *     MyClass() : m_timerManager(this) {
 *         // Create and manage timers
 *         SafeTimer* timer1 = m_timerManager.createTimer("timer1");
 *         timer1->start(1000, []() { ... });
 *         
 *         // Single shot with automatic management
 *         m_timerManager.singleShot("delayed_action", 2000, []() { ... });
 *     }
 * };
 * @endcode
 */
class SafeTimerManager : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief Constructor
     * @param parent Parent object that owns all managed timers
     */
    explicit SafeTimerManager(QObject* parent);
    
    /**
     * @brief Destructor - stops and deletes all managed timers
     */
    virtual ~SafeTimerManager();
    
    /**
     * @brief Create a new managed timer
     * @param name Unique name for the timer
     * @param debugPrefix Optional debug prefix
     * @return New SafeTimer instance (owned by manager)
     */
    SafeTimer* createTimer(const QString& name, const QString& debugPrefix = QString());
    
    /**
     * @brief Get an existing timer by name
     * @param name Timer name
     * @return Timer instance or nullptr if not found
     */
    SafeTimer* getTimer(const QString& name) const;
    
    /**
     * @brief Remove and delete a timer
     * @param name Timer name
     * @return true if timer was found and removed
     */
    bool removeTimer(const QString& name);
    
    /**
     * @brief Stop all managed timers
     */
    void stopAll();
    
    /**
     * @brief Create a managed single-shot timer
     * @param name Unique name for tracking
     * @param msec Delay in milliseconds
     * @param callback Function to execute
     * @return true if single-shot was created successfully
     */
    bool singleShot(const QString& name, int msec, std::function<void()> callback);
    
    /**
     * @brief Check if a timer exists
     * @param name Timer name
     * @return true if timer exists
     */
    bool hasTimer(const QString& name) const;
    
    /**
     * @brief Get count of active timers
     * @return Number of currently active timers
     */
    int activeTimerCount() const;
    
    /**
     * @brief Get names of all managed timers
     * @return List of timer names
     */
    QStringList timerNames() const;

private:
    QPointer<QObject> m_parent;                           // Parent object
    QHash<QString, SafeTimer*> m_timers;                  // Managed timers by name
    mutable QMutex m_mutex;                               // Thread safety
    QString m_debugPrefix;                                 // Debug prefix
    
    /**
     * @brief Internal cleanup of all timers
     */
    void cleanup();
    
    /**
     * @brief Check if the manager's parent still exists
     * @return true if parent exists and is valid
     */
    bool isParentValid() const;
};

#endif // SAFETIMER_H
