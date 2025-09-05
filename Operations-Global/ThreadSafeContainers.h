#ifndef THREADSAFECONTAINERS_H
#define THREADSAFECONTAINERS_H

#include <QMutex>
#include <QMutexLocker>
#include <QList>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QDebug>
#include <functional>
#include <optional>
#include <type_traits>

/**
 * @brief Thread-safe wrapper for Qt containers with security enhancements
 * 
 * This template class provides thread-safe access to Qt containers (QList, QVector, etc.)
 * with built-in protections against common security issues:
 * - Race conditions through mutex protection
 * - Iterator invalidation through safe iteration patterns
 * - Bounds checking for element access
 * - Memory exhaustion protection through size limits
 * 
 * Usage example:
 * ThreadSafeContainer<QList<QString>> safeList;
 * safeList.append("item");
 * auto value = safeList.at(0);
 */
template<typename Container>
class ThreadSafeContainer {
public:
    using value_type = typename Container::value_type;
    using size_type = typename Container::size_type;
    
private:
    mutable QMutex m_mutex;
    Container m_container;
    size_t m_maxSize;
    QString m_debugName;  // For debug output
    
    // Statistics for monitoring
    mutable size_t m_accessCount = 0;
    mutable size_t m_modificationCount = 0;
    
public:
    /**
     * @brief Constructor with optional max size limit and debug name
     * @param maxSize Maximum allowed container size (default: 1,000,000)
     * @param debugName Name for debug output (e.g., "DiaryEntries")
     */
    explicit ThreadSafeContainer(size_t maxSize = 1000000, const QString& debugName = "ThreadSafeContainer")
        : m_maxSize(maxSize), m_debugName(debugName) {
        qDebug() << m_debugName << ": Created with max size" << m_maxSize;
    }
    
    /**
     * @brief Destructor - logs final statistics
     */
    ~ThreadSafeContainer() {
        QMutexLocker locker(&m_mutex);
        qDebug() << m_debugName << ": Destroyed. Final size:" << m_container.size()
                 << "Total accesses:" << m_accessCount
                 << "Total modifications:" << m_modificationCount;
    }
    
    // ============================================================================
    // Basic Operations
    // ============================================================================
    
    /**
     * @brief Thread-safe append with size validation
     * @param value Value to append
     * @return true if successful, false if would exceed max size
     */
    bool append(const value_type& value) {
        QMutexLocker locker(&m_mutex);
        
        if (m_container.size() >= m_maxSize) {
            qWarning() << m_debugName << ": Cannot append - max size reached:" << m_maxSize;
            return false;
        }
        
        m_container.append(value);
        m_modificationCount++;
        return true;
    }
    
    /**
     * @brief Thread-safe prepend with size validation
     */
    bool prepend(const value_type& value) {
        QMutexLocker locker(&m_mutex);
        
        if (m_container.size() >= m_maxSize) {
            qWarning() << m_debugName << ": Cannot prepend - max size reached:" << m_maxSize;
            return false;
        }
        
        m_container.prepend(value);
        m_modificationCount++;
        return true;
    }
    
    /**
     * @brief Thread-safe remove first occurrence
     * @return true if item was found and removed
     */
    bool removeOne(const value_type& value) {
        QMutexLocker locker(&m_mutex);
        bool result = m_container.removeOne(value);
        if (result) {
            m_modificationCount++;
        }
        return result;
    }
    
    /**
     * @brief Thread-safe remove all occurrences
     * @return Number of items removed
     */
    int removeAll(const value_type& value) {
        QMutexLocker locker(&m_mutex);
        int count = m_container.removeAll(value);
        if (count > 0) {
            m_modificationCount++;
        }
        return count;
    }
    
    /**
     * @brief Thread-safe clear
     */
    void clear() {
        QMutexLocker locker(&m_mutex);
        m_container.clear();
        m_modificationCount++;
        qDebug() << m_debugName << ": Cleared container";
    }
    
    // ============================================================================
    // Safe Element Access with Bounds Checking
    // ============================================================================
    
    /**
     * @brief Safe element access with bounds checking
     * @param index Index to access
     * @return Optional containing value if valid index, empty optional otherwise
     */
    std::optional<value_type> at(size_type index) const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        
        if (index < 0 || index >= m_container.size()) {
            qWarning() << m_debugName << ": Index out of bounds:" << index 
                       << "Size:" << m_container.size();
            return std::nullopt;
        }
        
        return m_container.at(index);
    }
    
    /**
     * @brief Safe first element access
     * @return Optional containing first element if container not empty
     */
    std::optional<value_type> first() const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        
        if (m_container.isEmpty()) {
            qDebug() << m_debugName << ": Attempted to access first() on empty container";
            return std::nullopt;
        }
        
        return m_container.first();
    }
    
    /**
     * @brief Safe last element access
     * @return Optional containing last element if container not empty
     */
    std::optional<value_type> last() const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        
        if (m_container.isEmpty()) {
            qDebug() << m_debugName << ": Attempted to access last() on empty container";
            return std::nullopt;
        }
        
        return m_container.last();
    }
    
    /**
     * @brief Safe take first (remove and return first element)
     */
    std::optional<value_type> takeFirst() {
        QMutexLocker locker(&m_mutex);
        
        if (m_container.isEmpty()) {
            qDebug() << m_debugName << ": Attempted to takeFirst() on empty container";
            return std::nullopt;
        }
        
        m_modificationCount++;
        return m_container.takeFirst();
    }
    
    /**
     * @brief Safe take last (remove and return last element)
     */
    std::optional<value_type> takeLast() {
        QMutexLocker locker(&m_mutex);
        
        if (m_container.isEmpty()) {
            qDebug() << m_debugName << ": Attempted to takeLast() on empty container";
            return std::nullopt;
        }
        
        m_modificationCount++;
        return m_container.takeLast();
    }
    
    /**
     * @brief Safe take at index
     */
    std::optional<value_type> takeAt(size_type index) {
        QMutexLocker locker(&m_mutex);
        
        if (index < 0 || index >= m_container.size()) {
            qWarning() << m_debugName << ": takeAt() index out of bounds:" << index 
                       << "Size:" << m_container.size();
            return std::nullopt;
        }
        
        m_modificationCount++;
        return m_container.takeAt(index);
    }
    
    // ============================================================================
    // Safe Iteration Patterns
    // ============================================================================
    
    /**
     * @brief Safe iteration with read-only access
     * @param operation Function to apply to each element (const reference)
     * 
     * Creates a local copy for iteration to prevent iterator invalidation
     */
    void safeIterate(std::function<void(const value_type&)> operation) const {
        Container localCopy;
        {
            QMutexLocker locker(&m_mutex);
            localCopy = m_container;  // Copy while locked
            m_accessCount++;
        }
        // Iterate outside of lock to prevent deadlocks
        for (const auto& item : localCopy) {
            operation(item);
        }
    }
    
    /**
     * @brief Safe iteration with index
     * @param operation Function receiving index and value
     */
    void safeIterateWithIndex(std::function<void(size_type, const value_type&)> operation) const {
        Container localCopy;
        {
            QMutexLocker locker(&m_mutex);
            localCopy = m_container;
            m_accessCount++;
        }
        
        for (size_type i = 0; i < localCopy.size(); ++i) {
            operation(i, localCopy.at(i));
        }
    }
    
    /**
     * @brief Safe modification during iteration
     * @param predicate Function that returns true for items to keep
     * 
     * Removes all items for which predicate returns false
     */
    int removeIf(std::function<bool(const value_type&)> predicate) {
        QMutexLocker locker(&m_mutex);
        
        int removedCount = 0;
        auto it = m_container.begin();
        while (it != m_container.end()) {
            if (predicate(*it)) {
                it = m_container.erase(it);
                removedCount++;
            } else {
                ++it;
            }
        }
        
        if (removedCount > 0) {
            m_modificationCount++;
            qDebug() << m_debugName << ": Removed" << removedCount << "items";
        }
        
        return removedCount;
    }
    
    /**
     * @brief Transform all elements safely
     * @param transformer Function that modifies each element
     */
    void transform(std::function<void(value_type&)> transformer) {
        QMutexLocker locker(&m_mutex);
        
        for (auto& item : m_container) {
            transformer(item);
        }
        m_modificationCount++;
    }
    
    // ============================================================================
    // Search Operations
    // ============================================================================
    
    /**
     * @brief Thread-safe contains check
     */
    bool contains(const value_type& value) const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        return m_container.contains(value);
    }
    
    /**
     * @brief Find first element matching predicate
     */
    std::optional<value_type> findFirst(std::function<bool(const value_type&)> predicate) const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        
        for (const auto& item : m_container) {
            if (predicate(item)) {
                return item;
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief Find all elements matching predicate
     */
    Container findAll(std::function<bool(const value_type&)> predicate) const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        
        Container results;
        for (const auto& item : m_container) {
            if (predicate(item)) {
                results.append(item);
            }
        }
        return results;
    }
    
    /**
     * @brief Get index of value
     * @return Index if found, -1 otherwise
     */
    int indexOf(const value_type& value) const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        return m_container.indexOf(value);
    }
    
    // ============================================================================
    // Bulk Operations
    // ============================================================================
    
    /**
     * @brief Replace entire container contents (thread-safe swap)
     */
    bool setContents(const Container& newContainer) {
        QMutexLocker locker(&m_mutex);
        
        if (newContainer.size() > m_maxSize) {
            qWarning() << m_debugName << ": Cannot set contents - size" << newContainer.size() 
                       << "exceeds max" << m_maxSize;
            return false;
        }
        
        m_container = newContainer;
        m_modificationCount++;
        return true;
    }
    
    /**
     * @brief Get a snapshot copy of the container
     */
    Container getCopy() const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        return m_container;
    }
    
    /**
     * @brief Append multiple items with size check
     */
    bool appendMultiple(const Container& items) {
        QMutexLocker locker(&m_mutex);
        
        if (m_container.size() + items.size() > m_maxSize) {
            qWarning() << m_debugName << ": Cannot append" << items.size() 
                       << "items - would exceed max size";
            return false;
        }
        
        m_container.append(items);
        m_modificationCount++;
        return true;
    }
    
    // ============================================================================
    // Status and Statistics
    // ============================================================================
    
    /**
     * @brief Get current size
     */
    size_type size() const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        return m_container.size();
    }
    
    /**
     * @brief Check if empty
     */
    bool isEmpty() const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        return m_container.isEmpty();
    }
    
    /**
     * @brief Get maximum allowed size
     */
    size_t maxSize() const {
        return m_maxSize;
    }
    
    /**
     * @brief Set new maximum size
     */
    void setMaxSize(size_t newMax) {
        QMutexLocker locker(&m_mutex);
        m_maxSize = newMax;
        qDebug() << m_debugName << ": Max size changed to" << newMax;
    }
    
    /**
     * @brief Get access statistics
     */
    void getStatistics(size_t& accessCount, size_t& modificationCount) const {
        QMutexLocker locker(&m_mutex);
        accessCount = m_accessCount;
        modificationCount = m_modificationCount;
    }
    
    /**
     * @brief Reset statistics
     */
    void resetStatistics() {
        QMutexLocker locker(&m_mutex);
        m_accessCount = 0;
        m_modificationCount = 0;
    }
    
    /**
     * @brief Set debug name for logging
     */
    void setDebugName(const QString& name) {
        QMutexLocker locker(&m_mutex);
        m_debugName = name;
    }
    
    // ============================================================================
    // Atomic Operations
    // ============================================================================
    
    /**
     * @brief Atomic swap of two elements
     */
    bool swapItemsAt(size_type index1, size_type index2) {
        QMutexLocker locker(&m_mutex);
        
        if (index1 < 0 || index1 >= m_container.size() ||
            index2 < 0 || index2 >= m_container.size()) {
            qWarning() << m_debugName << ": swap indices out of bounds";
            return false;
        }
        
        if (index1 != index2) {
            std::swap(m_container[index1], m_container[index2]);
            m_modificationCount++;
        }
        return true;
    }
    
    /**
     * @brief Move item from one index to another
     */
    bool moveItem(size_type from, size_type to) {
        QMutexLocker locker(&m_mutex);
        
        if (from < 0 || from >= m_container.size() ||
            to < 0 || to >= m_container.size()) {
            qWarning() << m_debugName << ": move indices out of bounds";
            return false;
        }
        
        if (from != to) {
            m_container.move(from, to);
            m_modificationCount++;
        }
        return true;
    }
    
    // ============================================================================
    // Specialized for QMap/QHash
    // ============================================================================
    
    /**
     * @brief Insert key-value pair (for QMap/QHash)
     * Enabled only for map-like containers
     */
    template<typename K = typename Container::key_type, 
             typename V = typename Container::mapped_type>
    typename std::enable_if<std::is_same<Container, QMap<K,V>>::value || 
                           std::is_same<Container, QHash<K,V>>::value, bool>::type
    insert(const K& key, const V& value) {
        QMutexLocker locker(&m_mutex);
        
        if (m_container.size() >= m_maxSize && !m_container.contains(key)) {
            qWarning() << m_debugName << ": Cannot insert - max size reached";
            return false;
        }
        
        m_container.insert(key, value);
        m_modificationCount++;
        return true;
    }
    
    /**
     * @brief Get value for key (for QMap/QHash)
     */
    template<typename K = typename Container::key_type, 
             typename V = typename Container::mapped_type>
    typename std::enable_if<std::is_same<Container, QMap<K,V>>::value || 
                           std::is_same<Container, QHash<K,V>>::value, std::optional<V>>::type
    value(const K& key) const {
        QMutexLocker locker(&m_mutex);
        m_accessCount++;
        
        if (m_container.contains(key)) {
            return m_container.value(key);
        }
        return std::nullopt;
    }
    
    /**
     * @brief Remove key (for QMap/QHash)
     */
    template<typename K = typename Container::key_type>
    typename std::enable_if<std::is_same<Container, QMap<K,typename Container::mapped_type>>::value || 
                           std::is_same<Container, QHash<K,typename Container::mapped_type>>::value, bool>::type
    remove(const K& key) {
        QMutexLocker locker(&m_mutex);
        
        bool removed = (m_container.remove(key) > 0);
        if (removed) {
            m_modificationCount++;
        }
        return removed;
    }
    
    // ============================================================================
    // Operators (use with caution - prefer named methods for clarity)
    // ============================================================================
    
    /**
     * @brief Array subscript operator with bounds checking
     * Returns default-constructed value if out of bounds
     */
    value_type operator[](size_type index) const {
        auto result = at(index);
        if (result.has_value()) {
            return result.value();
        }
        qWarning() << m_debugName << ": operator[] returning default value for invalid index" << index;
        return value_type{};
    }
    
    // Disable copy constructor and assignment to prevent accidental copies
    ThreadSafeContainer(const ThreadSafeContainer&) = delete;
    ThreadSafeContainer& operator=(const ThreadSafeContainer&) = delete;
    
    // Enable move semantics
    ThreadSafeContainer(ThreadSafeContainer&& other) noexcept {
        QMutexLocker locker(&other.m_mutex);
        m_container = std::move(other.m_container);
        m_maxSize = other.m_maxSize;
        m_debugName = std::move(other.m_debugName);
        m_accessCount = other.m_accessCount;
        m_modificationCount = other.m_modificationCount;
    }
    
    ThreadSafeContainer& operator=(ThreadSafeContainer&& other) noexcept {
        if (this != &other) {
            QMutexLocker locker1(&m_mutex);
            QMutexLocker locker2(&other.m_mutex);
            m_container = std::move(other.m_container);
            m_maxSize = other.m_maxSize;
            m_debugName = std::move(other.m_debugName);
            m_accessCount = other.m_accessCount;
            m_modificationCount = other.m_modificationCount;
        }
        return *this;
    }
};

// ============================================================================
// Convenience Type Aliases
// ============================================================================

template<typename T>
using ThreadSafeList = ThreadSafeContainer<QList<T>>;

template<typename T>
using ThreadSafeVector = ThreadSafeContainer<QVector<T>>;

template<typename K, typename V>
using ThreadSafeMap = ThreadSafeContainer<QMap<K, V>>;

template<typename K, typename V>
using ThreadSafeHash = ThreadSafeContainer<QHash<K, V>>;

using ThreadSafeStringList = ThreadSafeContainer<QStringList>;

#endif // THREADSAFECONTAINERS_H
