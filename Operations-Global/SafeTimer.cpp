#include "SafeTimer.h"
#include <QCoreApplication>
#include <QThread>

// ============================================================================
// SafeTimer Implementation
// ============================================================================

SafeTimer::SafeTimer(QObject* parent, const QString& debugPrefix)
    : QObject(parent)
    , m_parent(parent)
    , m_debugPrefix(debugPrefix)
    , m_isCleaningUp(false)
{
    if (!m_parent) {
        qCritical() << m_debugPrefix << "SafeTimer created without parent - this is unsafe!";
        return;
    }
    
    // Create the actual timer
    m_timer = new QTimer(this);
    
    // Connect to parent's destroyed signal for safety
    connect(m_parent, &QObject::destroyed, this, &SafeTimer::onParentDestroyed);
    
    // Connect timer's timeout to our internal handler
    connect(m_timer, &QTimer::timeout, this, &SafeTimer::onTimerTimeout);
    
    qDebug() << m_debugPrefix << "SafeTimer created with parent:" << m_parent;
}

SafeTimer::~SafeTimer()
{
    qDebug() << m_debugPrefix << "SafeTimer destructor called";
    cleanup();
}

void SafeTimer::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isCleaningUp) {
        return; // Prevent recursive cleanup
    }
    m_isCleaningUp = true;
    
    qDebug() << m_debugPrefix << "Cleaning up SafeTimer";
    
    // Stop timer first
    if (m_timer) {
        if (m_timer->isActive()) {
            m_timer->stop();
            qDebug() << m_debugPrefix << "Timer stopped during cleanup";
        }
        
        // Disconnect all signals
        disconnect(m_timer, nullptr, nullptr, nullptr);
        
        // Delete the timer
        delete m_timer;
        m_timer = nullptr;
    }
    
    // Clear callback
    m_callback = nullptr;
    
    // Clear receivers list
    m_receivers.clear();
    
    // Emit stopped signal
    emit stopped();
}

bool SafeTimer::start(std::function<void()> callback)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isParentValid()) {
        qWarning() << m_debugPrefix << "Cannot start timer - parent is invalid";
        return false;
    }
    
    if (!m_timer) {
        qWarning() << m_debugPrefix << "Cannot start timer - timer is null";
        return false;
    }
    
    // Get the current interval
    int currentInterval = m_timer->interval();
    if (currentInterval <= 0) {
        qWarning() << m_debugPrefix << "Cannot start timer - interval is not set (" << currentInterval << "ms)";
        return false;
    }
    
    m_callback = callback;
    // Pass the interval explicitly to ensure it's used
    m_timer->start(currentInterval);
    
    qDebug() << m_debugPrefix << "Timer started with interval:" << currentInterval << "ms";
    emit started();
    return true;
}

bool SafeTimer::start(int msec, std::function<void()> callback)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isParentValid()) {
        qWarning() << m_debugPrefix << "Cannot start timer - parent is invalid";
        return false;
    }
    
    if (!m_timer) {
        qWarning() << m_debugPrefix << "Cannot start timer - timer is null";
        return false;
    }
    
    m_timer->setInterval(msec);
    m_callback = callback;
    // Pass the interval explicitly to ensure it's used
    m_timer->start(msec);
    
    qDebug() << m_debugPrefix << "Timer started with interval:" << msec << "ms";
    emit started();
    return true;
}

void SafeTimer::stop()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
        qDebug() << m_debugPrefix << "Timer stopped";
        emit stopped();
    }
}

bool SafeTimer::isActive() const
{
    QMutexLocker locker(&m_mutex);
    return m_timer && m_timer->isActive();
}

void SafeTimer::setInterval(int msec)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timer) {
        m_timer->setInterval(msec);
        qDebug() << m_debugPrefix << "Timer interval set to:" << msec << "ms";
    }
}

int SafeTimer::interval() const
{
    QMutexLocker locker(&m_mutex);
    return m_timer ? m_timer->interval() : 0;
}

void SafeTimer::setSingleShot(bool singleShot)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timer) {
        m_timer->setSingleShot(singleShot);
        qDebug() << m_debugPrefix << "Timer single shot mode set to:" << singleShot;
    }
}

bool SafeTimer::isSingleShot() const
{
    QMutexLocker locker(&m_mutex);
    return m_timer ? m_timer->isSingleShot() : false;
}

void SafeTimer::disconnectAll()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timer) {
        disconnect(m_timer, nullptr, nullptr, nullptr);
        qDebug() << m_debugPrefix << "All connections disconnected";
    }
    
    m_receivers.clear();
    m_callback = nullptr;
}

bool SafeTimer::isParentValid() const
{
    return !m_parent.isNull();
}

int SafeTimer::remainingTime() const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timer && m_timer->isActive()) {
        return m_timer->remainingTime();
    }
    return -1;
}

SafeTimer* SafeTimer::create(QObject* parent, const QString& debugPrefix)
{
    if (!parent) {
        qWarning() << "SafeTimer::create: Cannot create timer without parent";
        return nullptr;
    }
    
    return new SafeTimer(parent, debugPrefix);
}

void SafeTimer::onTimerTimeout()
{
    // Check if parent still exists
    if (!isParentValid()) {
        qDebug() << m_debugPrefix << "Timer fired but parent is gone - stopping timer";
        stop();
        return;
    }
    
    // Check if receivers are still valid (for connected slots)
    if (!areReceiversValid()) {
        qDebug() << m_debugPrefix << "Timer fired but receiver is gone - stopping timer";
        stop();
        return;
    }
    
    // Emit the timeout signal first (for direct connections)
    emit timeout();
    
    // Then call the callback if set
    if (m_callback) {
        // Extra safety check before calling
        if (isParentValid()) {
            m_callback();
        } else {
            qDebug() << m_debugPrefix << "Parent deleted during timeout signal - skipping callback";
        }
    }
}

void SafeTimer::onParentDestroyed()
{
    qDebug() << m_debugPrefix << "Parent destroyed - cleaning up timer";
    cleanup();
}

bool SafeTimer::areReceiversValid() const
{
    for (const auto& receiver : m_receivers) {
        if (receiver.isNull()) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Static Single Shot Functions
// ============================================================================

void SafeTimer::singleShot(int msec, QObject* parent, std::function<void()> callback, 
                           const QString& debugPrefix)
{
    if (!parent) {
        qWarning() << debugPrefix << "singleShot called with null parent - aborting";
        return;
    }
    
    // Create QPointer for safety
    QPointer<QObject> safeParent = parent;
    
    qDebug() << debugPrefix << "Setting up single shot timer for" << msec << "ms";
    
    // Use Qt's single shot with lambda that checks parent validity
    QTimer::singleShot(msec, parent, [safeParent, callback, debugPrefix]() {
        if (safeParent.isNull()) {
            qDebug() << debugPrefix << "Single shot timer fired but parent is gone - skipping callback";
            return;
        }
        
        qDebug() << debugPrefix << "Single shot timer fired - executing callback";
        callback();
    });
}

void SafeTimer::singleShotMultiCheck(int msec, QObject* parent, 
                                     const QList<QPointer<QObject>>& additionalParents,
                                     std::function<void()> callback,
                                     const QString& debugPrefix)
{
    if (!parent) {
        qWarning() << debugPrefix << "singleShotMultiCheck called with null parent - aborting";
        return;
    }
    
    // Create QPointer for main parent
    QPointer<QObject> safeParent = parent;
    
    // Copy additional parents list (they're already QPointers)
    QList<QPointer<QObject>> safeAdditionalParents = additionalParents;
    
    qDebug() << debugPrefix << "Setting up multi-check single shot timer for" << msec << "ms with"
             << additionalParents.size() << "additional parent checks";
    
    QTimer::singleShot(msec, parent, [safeParent, safeAdditionalParents, callback, debugPrefix]() {
        // Check main parent
        if (safeParent.isNull()) {
            qDebug() << debugPrefix << "Single shot timer fired but main parent is gone - skipping callback";
            return;
        }
        
        // Check all additional parents
        for (const auto& additionalParent : safeAdditionalParents) {
            if (additionalParent.isNull()) {
                qDebug() << debugPrefix << "Single shot timer fired but an additional parent is gone - skipping callback";
                return;
            }
        }
        
        qDebug() << debugPrefix << "Multi-check single shot timer fired - all parents valid, executing callback";
        callback();
    });
}

// ============================================================================
// SafeTimerManager Implementation
// ============================================================================

SafeTimerManager::SafeTimerManager(QObject* parent)
    : QObject(parent)
    , m_parent(parent)
    , m_debugPrefix("SafeTimerManager")
{
    if (!m_parent) {
        qCritical() << m_debugPrefix << "Created without parent - this is unsafe!";
        return;
    }
    
    qDebug() << m_debugPrefix << "Created with parent:" << m_parent;
}

SafeTimerManager::~SafeTimerManager()
{
    qDebug() << m_debugPrefix << "Destructor called - cleaning up" << m_timers.size() << "timers";
    cleanup();
}

void SafeTimerManager::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    // Stop all timers first
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        SafeTimer* timer = it.value();
        if (timer) {
            timer->stop();
            delete timer;
        }
    }
    
    m_timers.clear();
    qDebug() << m_debugPrefix << "All timers cleaned up";
}

SafeTimer* SafeTimerManager::createTimer(const QString& name, const QString& debugPrefix)
{
    QMutexLocker locker(&m_mutex);
    
    if (!isParentValid()) {
        qWarning() << m_debugPrefix << "Cannot create timer - parent is invalid";
        return nullptr;
    }
    
    // Check if timer already exists
    if (m_timers.contains(name)) {
        qWarning() << m_debugPrefix << "Timer with name" << name << "already exists";
        return m_timers[name];
    }
    
    // Create new timer
    QString timerPrefix = debugPrefix.isEmpty() ? 
        QString("%1::%2").arg(m_debugPrefix, name) : debugPrefix;
    
    SafeTimer* timer = new SafeTimer(m_parent, timerPrefix);
    m_timers[name] = timer;
    
    qDebug() << m_debugPrefix << "Created timer:" << name;
    return timer;
}

SafeTimer* SafeTimerManager::getTimer(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    return m_timers.value(name, nullptr);
}

bool SafeTimerManager::removeTimer(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_timers.contains(name)) {
        qDebug() << m_debugPrefix << "Timer" << name << "not found";
        return false;
    }
    
    SafeTimer* timer = m_timers.take(name);
    if (timer) {
        timer->stop();
        delete timer;
        qDebug() << m_debugPrefix << "Removed timer:" << name;
    }
    
    return true;
}

void SafeTimerManager::stopAll()
{
    QMutexLocker locker(&m_mutex);
    
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        SafeTimer* timer = it.value();
        if (timer && timer->isActive()) {
            timer->stop();
        }
    }
    
    qDebug() << m_debugPrefix << "Stopped all timers";
}

bool SafeTimerManager::singleShot(const QString& name, int msec, std::function<void()> callback)
{
    if (!isParentValid()) {
        qWarning() << m_debugPrefix << "Cannot create single shot - parent is invalid";
        return false;
    }
    
    // Remove any existing timer with this name
    removeTimer(name);
    
    // Create new single shot timer
    SafeTimer* timer = createTimer(name);
    if (!timer) {
        return false;
    }
    
    timer->setSingleShot(true);
    timer->setInterval(msec);
    
    // Connect to remove timer after it fires
    connect(timer, &SafeTimer::timeout, this, [this, name]() {
        QTimer::singleShot(0, this, [this, name]() {
            removeTimer(name);
        });
    });
    
    return timer->start(callback);
}

bool SafeTimerManager::hasTimer(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    return m_timers.contains(name);
}

int SafeTimerManager::activeTimerCount() const
{
    QMutexLocker locker(&m_mutex);
    
    int count = 0;
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        SafeTimer* timer = it.value();
        if (timer && timer->isActive()) {
            count++;
        }
    }
    
    return count;
}

QStringList SafeTimerManager::timerNames() const
{
    QMutexLocker locker(&m_mutex);
    return m_timers.keys();
}

bool SafeTimerManager::isParentValid() const
{
    return !m_parent.isNull();
}
