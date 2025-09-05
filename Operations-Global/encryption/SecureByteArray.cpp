#include "SecureByteArray.h"
#include <QRandomGenerator>
#include <cstring>

// Initialize static members
std::atomic<size_t> SecureByteArray::s_totalLockedMemory{0};
char SecureByteArray::s_dummyChar = 0;

// Default constructor
SecureByteArray::SecureByteArray() 
    : m_locked(false) {
    qDebug() << "SecureByteArray: Default constructor called";
}

// Constructor with data
SecureByteArray::SecureByteArray(const QByteArray& data) 
    : m_data(data), m_locked(false) {
    qDebug() << "SecureByteArray: Constructor with data called, size:" << data.size() << "bytes";
    tryLockMemory();
}

// Constructor with size
SecureByteArray::SecureByteArray(int size) 
    : m_locked(false) {
    // Validate size parameter
    if (size < 0) {
        qWarning() << "SecureByteArray: Constructor called with negative size:" << size << ". Using size 0.";
        size = 0;
    }
    
    qDebug() << "SecureByteArray: Constructor with size called, size:" << size << "bytes";
    m_data.resize(size);
    tryLockMemory();
}

// Destructor
SecureByteArray::~SecureByteArray() {
    qDebug() << "SecureByteArray: Destructor called, clearing" << m_data.size() << "bytes";
    secureClear();
    forceUnlockMemory();
}

// Move constructor
SecureByteArray::SecureByteArray(SecureByteArray&& other) noexcept 
    : m_locked(false) {
    qDebug() << "SecureByteArray: Move constructor called";
    moveFrom(std::move(other));
}

// Move assignment operator
SecureByteArray& SecureByteArray::operator=(SecureByteArray&& other) noexcept {
    qDebug() << "SecureByteArray: Move assignment operator called";
    if (this != &other) {
        // Clear current data first
        secureClear();
        forceUnlockMemory();
        
        // Move from other
        moveFrom(std::move(other));
    }
    return *this;
}

// Set data
void SecureByteArray::setData(const QByteArray& data) {
    // Validate input
    if (data.isNull()) {
        qWarning() << "SecureByteArray: setData() called with null QByteArray. Clearing data instead.";
        clear();
        return;
    }
    
    qDebug() << "SecureByteArray: Setting data, new size:" << data.size() << "bytes";
    
    // Clear old data securely
    secureClear();
    
    // If locked, unlock before changing size
    if (m_locked) {
        forceUnlockMemory();
    }
    
    // Set new data
    m_data = data;
    
    // Try to lock new memory
    tryLockMemory();
}

// Get data (const) - returns a copy
QByteArray SecureByteArray::data() const {
    return m_data;
}

// Get const reference to data - for passing to functions
const QByteArray& SecureByteArray::constDataRef() const {
    return m_data;
}

// Get const pointer to data
const char* SecureByteArray::constData() const {
    if (m_data.isEmpty()) {
        qDebug() << "SecureByteArray: constData() called on empty array";
    }
    return m_data.constData();
}

// Get pointer to data
char* SecureByteArray::data() {
    if (m_data.isEmpty()) {
        qWarning() << "SecureByteArray: data() called on empty array, returning nullptr";
        return nullptr;
    }
    return m_data.data();
}

// Get size
int SecureByteArray::size() const {
    return m_data.size();
}

// Check if empty
bool SecureByteArray::isEmpty() const {
    return m_data.isEmpty();
}

// Resize
void SecureByteArray::resize(int size) {
    // Validate size parameter
    if (size < 0) {
        qWarning() << "SecureByteArray: Invalid resize request with negative size:" << size << ". Ignoring.";
        return;
    }
    
    qDebug() << "SecureByteArray: Resizing from" << m_data.size() << "to" << size << "bytes";
    
    // If reducing size, securely clear the removed portion
    if (size < m_data.size() && !m_data.isEmpty()) {
        char* dataPtr = m_data.data();
        OPENSSL_cleanse(dataPtr + size, m_data.size() - size);
    }
    
    // Unlock before resize if needed
    bool wasLocked = m_locked;
    if (wasLocked) {
        forceUnlockMemory();
    }
    
    m_data.resize(size);
    
    // Re-lock if it was locked
    if (wasLocked) {
        tryLockMemory();
    }
}

// Reserve capacity
void SecureByteArray::reserve(int size) {
    // Validate size parameter
    if (size < 0) {
        qWarning() << "SecureByteArray: Invalid reserve request with negative size:" << size << ". Ignoring.";
        return;
    }
    
    qDebug() << "SecureByteArray: Reserving" << size << "bytes";
    m_data.reserve(size);
}

// Clear data securely
void SecureByteArray::clear() {
    qDebug() << "SecureByteArray: Clearing" << m_data.size() << "bytes";
    secureClear();
    forceUnlockMemory();
}

// Check if memory is locked
bool SecureByteArray::isLocked() const {
    return m_locked;
}

// Lock memory
bool SecureByteArray::lockMemory() {
    if (!m_locked && !m_data.isEmpty()) {
        return tryLockMemory();
    }
    return m_locked;
}

// Unlock memory
void SecureByteArray::unlockMemory() {
    forceUnlockMemory();
}

// Convert to Base64
QByteArray SecureByteArray::toBase64() const {
    return m_data.toBase64();
}

// Create from Base64
SecureByteArray SecureByteArray::fromBase64(const QByteArray& base64) {
    SecureByteArray result;
    result.setData(QByteArray::fromBase64(base64));
    return result;
}

// Array access operator (const) - with bounds checking
char SecureByteArray::operator[](int index) const {
    // Check bounds
    if (index < 0 || index >= m_data.size()) {
        qWarning() << "SecureByteArray: Out-of-bounds access attempted at index" << index 
                   << "(size:" << m_data.size() << "). Returning 0.";
        return 0;  // Return safe default value
    }
    return m_data[index];
}

// Array access operator - with bounds checking
char& SecureByteArray::operator[](int index) {
    // Check bounds
    if (index < 0 || index >= m_data.size()) {
        qWarning() << "SecureByteArray: Out-of-bounds access attempted at index" << index
                   << "(size:" << m_data.size() << "). Returning reference to dummy char.";
        s_dummyChar = 0;  // Reset dummy char to 0
        return s_dummyChar;  // Return reference to safe static variable
    }
    return m_data[index];
}

// Safe access with exception throwing (const)
char SecureByteArray::at(int index) const {
    // Strict bounds checking
    if (index < 0 || index >= m_data.size()) {
        QString errorMsg = QString("SecureByteArray::at(): Index %1 out of range [0, %2)")
                          .arg(index).arg(m_data.size());
        qCritical() << "SecureByteArray: Exception thrown -" << errorMsg;
        throw std::out_of_range(errorMsg.toStdString());
    }
    return m_data[index];
}

// Safe access with exception throwing (non-const)
char& SecureByteArray::at(int index) {
    // Strict bounds checking
    if (index < 0 || index >= m_data.size()) {
        QString errorMsg = QString("SecureByteArray::at(): Index %1 out of range [0, %2)")
                          .arg(index).arg(m_data.size());
        qCritical() << "SecureByteArray: Exception thrown -" << errorMsg;
        throw std::out_of_range(errorMsg.toStdString());
    }
    return m_data[index];
}

// Append data
SecureByteArray& SecureByteArray::append(const QByteArray& data) {
    // Validate input
    if (data.isEmpty()) {
        qDebug() << "SecureByteArray: Append called with empty data, no operation performed.";
        return *this;
    }
    
    // Check if append would exceed reasonable size limits
    if (m_data.size() + data.size() < 0) {  // Integer overflow check
        qWarning() << "SecureByteArray: Append would cause integer overflow. Current size:" 
                   << m_data.size() << ", attempting to append:" << data.size() << "bytes. Operation aborted.";
        return *this;
    }
    
    qDebug() << "SecureByteArray: Appending" << data.size() << "bytes";
    
    // Unlock before modifying
    bool wasLocked = m_locked;
    if (wasLocked) {
        forceUnlockMemory();
    }
    
    m_data.append(data);
    
    // Re-lock if it was locked
    if (wasLocked) {
        tryLockMemory();
    }
    
    return *this;
}

// Append single character
SecureByteArray& SecureByteArray::append(char ch) {
    qDebug() << "SecureByteArray: Appending single byte";
    
    // Unlock before modifying
    bool wasLocked = m_locked;
    if (wasLocked) {
        forceUnlockMemory();
    }
    
    m_data.append(ch);
    
    // Re-lock if it was locked
    if (wasLocked) {
        tryLockMemory();
    }
    
    return *this;
}

// Equality operator
bool SecureByteArray::operator==(const SecureByteArray& other) const {
    // Use constant-time comparison for security
    if (m_data.size() != other.m_data.size()) {
        return false;
    }
    
    const char* data1 = m_data.constData();
    const char* data2 = other.m_data.constData();
    volatile unsigned char result = 0;
    
    for (int i = 0; i < m_data.size(); ++i) {
        result |= static_cast<unsigned char>(data1[i]) ^ static_cast<unsigned char>(data2[i]);
    }
    
    return result == 0;
}

// Inequality operator
bool SecureByteArray::operator!=(const SecureByteArray& other) const {
    return !(*this == other);
}

// Private: Secure clear implementation
void SecureByteArray::secureClear() {
    if (!m_data.isEmpty()) {
        qDebug() << "SecureByteArray: Securely clearing" << m_data.size() << "bytes";
        
        // Use OpenSSL's secure clearing function
        // This is designed to not be optimized away by the compiler
        OPENSSL_cleanse(m_data.data(), m_data.size());
        
        // Additional overwrite passes with random data for extra security
        for (int pass = 0; pass < 2; ++pass) {
            char* dataPtr = m_data.data();
            for (int i = 0; i < m_data.size(); ++i) {
                dataPtr[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
            }
        }
        
        // Final clear with zeros
        OPENSSL_cleanse(m_data.data(), m_data.size());
        
        // Clear the QByteArray
        m_data.clear();
    }
}

// Private: Try to lock memory
bool SecureByteArray::tryLockMemory() {
    if (m_data.isEmpty() || m_locked) {
        return m_locked;
    }
    
    // Check if we would exceed the memory limit
    size_t currentTotal = s_totalLockedMemory.load();
    size_t newTotal = currentTotal + m_data.size();
    
    if (newTotal > MAX_LOCKED_MEMORY) {
        qWarning() << "SecureByteArray: Cannot lock memory - would exceed limit."
                   << "Current:" << currentTotal << "bytes,"
                   << "Requested:" << m_data.size() << "bytes,"
                   << "Limit:" << MAX_LOCKED_MEMORY << "bytes";
        return false;
    }
    
    // Try to lock the memory using Windows API
    if (VirtualLock(m_data.data(), m_data.size())) {
        m_locked = true;
        s_totalLockedMemory.fetch_add(m_data.size());
        qDebug() << "SecureByteArray: Successfully locked" << m_data.size() << "bytes in memory."
                 << "Total locked:" << s_totalLockedMemory.load() << "bytes";
        return true;
    } else {
        DWORD error = GetLastError();
        qWarning() << "SecureByteArray: Failed to lock" << m_data.size() << "bytes in memory."
                   << "Windows error code:" << error;
        
        // Common error codes:
        // ERROR_WORKING_SET_QUOTA (1453): The working set is not big enough
        // ERROR_INVALID_ADDRESS (487): Attempting to access invalid address
        if (error == ERROR_WORKING_SET_QUOTA) {
            qWarning() << "SecureByteArray: Working set quota exceeded. Consider increasing process working set size.";
        }
        
        m_locked = false;
        return false;
    }
}

// Private: Force unlock memory
void SecureByteArray::forceUnlockMemory() {
    if (m_locked && !m_data.isEmpty()) {
        if (VirtualUnlock(m_data.data(), m_data.size())) {
            qDebug() << "SecureByteArray: Successfully unlocked" << m_data.size() << "bytes from memory";
        } else {
            // VirtualUnlock can fail if the memory wasn't locked, which is okay
            DWORD error = GetLastError();
            if (error != ERROR_NOT_LOCKED) {
                qWarning() << "SecureByteArray: Failed to unlock memory. Windows error code:" << error;
            }
        }
        
        // Update the total locked memory counter
        s_totalLockedMemory.fetch_sub(m_data.size());
        m_locked = false;
        
        qDebug() << "SecureByteArray: Total locked memory now:" << s_totalLockedMemory.load() << "bytes";
    }
}

// Private: Move from another instance
void SecureByteArray::moveFrom(SecureByteArray&& other) noexcept {
    m_data = std::move(other.m_data);
    m_locked.store(other.m_locked.load());
    
    // Clear the other instance's lock flag without unlocking
    // (the memory lock moves with the data)
    other.m_locked = false;
    
    qDebug() << "SecureByteArray: Moved" << m_data.size() << "bytes from another instance";
}
