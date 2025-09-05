#ifndef SECUREBYTEARRAY_H
#define SECUREBYTEARRAY_H

#include <QByteArray>
#include <QString>
#include <QDebug>
#include <openssl/crypto.h>
#include <windows.h>
#include <atomic>
#include <stdexcept>

/**
 * SecureByteArray - A secure wrapper for sensitive byte data
 * 
 * This class provides secure storage for sensitive data like encryption keys.
 * Features:
 * - Memory locking to prevent swapping to disk
 * - Secure clearing using OpenSSL's OPENSSL_cleanse
 * - Prevention of accidental copies
 * - Automatic cleanup on destruction
 */
class SecureByteArray {
public:
    // Constructors and destructor
    SecureByteArray();
    explicit SecureByteArray(const QByteArray& data);
    explicit SecureByteArray(int size);
    ~SecureByteArray();
    
    // Delete copy operations to prevent accidental key duplication
    SecureByteArray(const SecureByteArray&) = delete;
    SecureByteArray& operator=(const SecureByteArray&) = delete;
    
    // Move operations for transferring ownership
    SecureByteArray(SecureByteArray&& other) noexcept;
    SecureByteArray& operator=(SecureByteArray&& other) noexcept;
    
    // Data access methods
    void setData(const QByteArray& data);
    QByteArray data() const;  // Returns a copy
    const QByteArray& constDataRef() const;  // Returns const reference (for passing to functions)
    const char* constData() const;
    char* data();
    
    // Size and state methods
    int size() const;
    bool isEmpty() const;
    void resize(int size);
    void reserve(int size);
    
    // Security methods
    void clear();
    bool isLocked() const;
    bool lockMemory();
    void unlockMemory();
    
    // Utility methods
    QByteArray toBase64() const;
    static SecureByteArray fromBase64(const QByteArray& base64);
    
    // Operators for compatibility (with bounds checking)
    char operator[](int index) const;
    char& operator[](int index);
    
    // Safe access methods with exception throwing
    char at(int index) const;
    char& at(int index);
    
    // Append operations
    SecureByteArray& append(const QByteArray& data);
    SecureByteArray& append(char ch);
    
    // Comparison
    bool operator==(const SecureByteArray& other) const;
    bool operator!=(const SecureByteArray& other) const;
    
    // Conversion operators for compatibility
    // Explicit conversion to prevent accidental copies
    explicit operator QByteArray() const { return m_data; }
    operator const QByteArray&() const { return m_data; }  // Implicit for const reference
    
private:
    QByteArray m_data;
    mutable std::atomic<bool> m_locked;
    
    // Track total locked memory across all instances
    static std::atomic<size_t> s_totalLockedMemory;
    static constexpr size_t MAX_LOCKED_MEMORY = 100 * 1024 * 1024; // 100 MB limit
    
    // Internal helper methods
    void secureClear();
    bool tryLockMemory();
    void forceUnlockMemory();
    void moveFrom(SecureByteArray&& other) noexcept;
    
    // Static dummy char for safe out-of-bounds access
    static char s_dummyChar;
};

#endif // SECUREBYTEARRAY_H
