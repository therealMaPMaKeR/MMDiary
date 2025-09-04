#ifndef SECUREBYTEARRAY_H
#define SECUREBYTEARRAY_H

#include <QByteArray>
#include <QString>
#include <QDebug>
#include <openssl/crypto.h>
#include <windows.h>
#include <atomic>

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
    QByteArray data() const;
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
    
    // Operators for compatibility
    char operator[](int index) const;
    char& operator[](int index);
    
    // Append operations
    SecureByteArray& append(const QByteArray& data);
    SecureByteArray& append(char ch);
    
    // Comparison
    bool operator==(const SecureByteArray& other) const;
    bool operator!=(const SecureByteArray& other) const;
    
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
};

#endif // SECUREBYTEARRAY_H
