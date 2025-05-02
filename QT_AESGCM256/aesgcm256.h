#ifndef AESGCM256_H
#define AESGCM256_H

#include <string>
#include <vector>
#include <QString>
#include <QByteArray>
#include <QFile>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <memory>
#include <sstream>

class AESGCM256Crypto {
public:
    // Constructors
    ~AESGCM256Crypto(); // destructor
    AESGCM256Crypto();
    explicit AESGCM256Crypto(const std::string& customKey);
    explicit AESGCM256Crypto(const QByteArray& customKey); // New constructor

    // Key management
    void setKey(const std::string& newKey);
    void setKey(const QByteArray& newKey); // New method
    void validateKey(const std::vector<uint8_t>& key);

    // Encryption/decryption methods
    QByteArray encrypt(const QString& data, const QString& username = QString());
    QString decrypt(const QByteArray& data);

    // Nonce management
    std::vector<uint8_t> generateNonce(const QString& username);

    // Helper methods
    static std::vector<uint8_t> str2Bytes(const std::string& message);
    static std::vector<uint8_t> QByteArray2Bytes(const QByteArray& data); // New helper
    static std::string bytes2Str(const std::vector<uint8_t>& bytes);

    QByteArray encryptBinary(const QByteArray& data, const QString& username);
    QByteArray decryptBinary(const QByteArray& data);

    // Key data - public for internal operations
    std::vector<uint8_t> m_key;

private:
    static const int GCM_TAG_LENGTH = 16; // 16 bytes (128 bits) for GCM tag
    static const int GCM_NONCE_LENGTH = 12; // 12 bytes (96 bits) for GCM nonce, optimal for GCM
};

#endif // AESGCM256_H
