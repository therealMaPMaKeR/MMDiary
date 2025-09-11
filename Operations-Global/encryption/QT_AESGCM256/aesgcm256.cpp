#include "AESGCM256.h"
#include "encryption/CryptoUtils.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QTextStream>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QElapsedTimer>
#include <openssl/rand.h>  // For RAND_bytes
#include <openssl/err.h>   // For ERR_* functions
#include <openssl/evp.h>   // For EVP_* functions
#include <openssl/crypto.h> // For OPENSSL_cleanse
#include <climits>
#include <cstddef>  // For SIZE_MAX
#include <algorithm> // For std::find

#define DECL_OPENSSL_PTR(tname, free_func) \
struct openssl_##tname##_dtor {            \
        void operator()(tname* v) {            \
            free_func(v);                      \
    }                                      \
};                                         \
    typedef std::unique_ptr<tname, openssl_##tname##_dtor> tname##_t

                DECL_OPENSSL_PTR(EVP_CIPHER_CTX, ::EVP_CIPHER_CTX_free);

struct error : public std::exception {
private:
    std::string m_msg;

public:
    error(const std::string& message)
        : m_msg(message) {
    }

    error(const char* msg)
        : m_msg(msg, msg + strlen(msg)) {
    }

    virtual const char* what() const noexcept override {
        return m_msg.c_str();
    }
};

struct openssl_error: public virtual error {
private:
    int m_code = -1;
    std::string m_msg;

public:
    openssl_error(int code, const std::string& message)
        : error(message),
        m_code(code) {
        std::stringstream ss;
        ss << "[" << m_code << "]: " << message;
        m_msg = ss.str();
    }

    openssl_error(int code, const char* msg)
        : error(msg),
        m_code(code) {
        std::stringstream ss;
        ss << "[" << m_code << "]: " << msg;
        m_msg = ss.str();
    }

    const char* what() const noexcept override {
        return m_msg.c_str();
    }
};

static void throw_if_error(int res = 1, const char* file = nullptr, uint64_t line = 0) {
    // SECURITY FIX: Properly handle OpenSSL errors
    if (res <= 0) {
        unsigned long errc = ERR_get_error();
        
        // If no error code but operation failed, throw generic error
        if (errc == 0) {
            std::stringstream ss;
            if (file != nullptr) {
                ss << file << ":" << line << " ";
            }
            ss << "OpenSSL operation failed (return code: " << res << ")";
            throw openssl_error(res, ss.str());
        }
        
        // Process error queue
        std::vector<std::string> errors;
        do {
            std::vector<uint8_t> buf(256);
            ERR_error_string_n(errc, (char*)buf.data(), buf.size());
            errors.push_back(std::string(buf.begin(), std::find(buf.begin(), buf.end(), '\0')));
            errc = ERR_get_error();
        } while (errc != 0);

        std::stringstream ss;
        ss << "\n";
        for (auto&& err : errors) {
            if (file != nullptr) {
                ss << file << ":" << line << " ";
            }
            ss << err << "\n";
        }
        const std::string err_all = ss.str();
        throw openssl_error(res, err_all);
    }
    
    // Also check for lingering errors in the queue
    unsigned long errc = ERR_peek_error();
    if (errc != 0) {
        // Clear and report any lingering errors
        std::vector<std::string> errors;
        while ((errc = ERR_get_error()) != 0) {
            std::vector<uint8_t> buf(256);
            ERR_error_string_n(errc, (char*)buf.data(), buf.size());
            errors.push_back(std::string(buf.begin(), std::find(buf.begin(), buf.end(), '\0')));
        }
        
        #ifdef QT_DEBUG
        std::stringstream ss;
        ss << "Warning: Lingering errors in OpenSSL error queue:\n";
        for (auto&& err : errors) {
            ss << "  " << err << "\n";
        }
        qWarning() << "AESGCM256Crypto:" << ss.str().c_str();
        #endif
    }
}

// Helper function to convert QString to vector<uint8_t>
static std::vector<uint8_t> QStringToVector(const QString& str) {
    QByteArray byteArray = str.toUtf8();
    const char* data = byteArray.constData();
    std::vector<uint8_t> result(data, data + byteArray.size());
    return result;
}

// Constructor (no default key - requires explicit key setting)
AESGCM256Crypto::AESGCM256Crypto() {
    // Default constructor with no key - requires explicit key setting before use
    
    #ifdef QT_DEBUG
    // Verify cipher availability at construction
    const EVP_CIPHER *cipher = EVP_aes_256_gcm();
    if (!cipher) {
        qCritical() << "AESGCM256Crypto: AES-256-GCM cipher not available in OpenSSL";
        throw error("AES-256-GCM cipher not available");
    }
    if (EVP_CIPHER_key_length(cipher) != 32) {
        qCritical() << "AESGCM256Crypto: AES-256-GCM key length mismatch. Expected 32, got" << EVP_CIPHER_key_length(cipher);
        throw error("AES-256-GCM cipher configuration error");
    }
    #endif
}

// Constructor with custom key (string)
AESGCM256Crypto::AESGCM256Crypto(const std::string& customKey)
    : m_key(str2Bytes(customKey))
{
    //qDebug() << "AESGCM256Crypto constructor called with key length:" << customKey.length() << "bytes";
    try {
        validateKey(m_key);
    }
    catch (const std::exception& e) {
        qWarning() << "Key validation failed in constructor:" << e.what();
        throw; // Re-throw the exception
    }
    //qDebug() << "AESGCM256Crypto constructor completed successfully";
}

// New constructor with QByteArray
AESGCM256Crypto::AESGCM256Crypto(const QByteArray& customKey)
    : m_key(QByteArray2Bytes(customKey))
{
    //qDebug() << "AESGCM256Crypto constructor called with QByteArray key length:" << customKey.length() << "bytes";
    try {
        validateKey(m_key);
    }
    catch (const std::exception& e) {
        qWarning() << "Key validation failed in constructor:" << e.what();
        throw; // Re-throw the exception
    }
    //qDebug() << "AESGCM256Crypto constructor completed successfully";
}

AESGCM256Crypto::~AESGCM256Crypto() {
    if (!m_key.empty()) {
        // Securely wipe the key using OpenSSL's cleanse function
        // This is designed to not be optimized away by the compiler
        OPENSSL_cleanse(m_key.data(), m_key.size());
        // Clear the vector afterwards
        m_key.clear();
        m_key.shrink_to_fit(); // Release memory back to system
    }
}

void AESGCM256Crypto::validateKey(const std::vector<uint8_t>& key) {
    //qDebug() << "Validating key, size:" << key.size() << "bytes";

    if (key.size() != 32) {
        qWarning() << "Invalid key size for AES-256-GCM. Required: 32 bytes, Actual:" << key.size() << "bytes";
        throw error("AES-256 GCM key must be exactly 32 bytes (256 bits)");
    }

    //qDebug() << "Key validation successful";
}

void AESGCM256Crypto::setKey(const std::string& newKey) {
    auto key = str2Bytes(newKey);
    validateKey(key);
    m_key = key;
}

// New setKey method for QByteArray
void AESGCM256Crypto::setKey(const QByteArray& newKey) {
    auto key = QByteArray2Bytes(newKey);
    validateKey(key);
    m_key = key;
}

QByteArray AESGCM256Crypto::encrypt(const QString& data, const QString& username) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Encryption key is not set. Call setKey() before encrypting.");
    }

    std::vector<uint8_t> cryptoinput = QStringToVector(data);

    // Generate system nonce for this encryption operation
    std::vector<uint8_t> nonce = generateNonce(username);

    // SECURITY: Check for integer overflow before allocating buffers
    if (cryptoinput.size() > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for encryption. Maximum supported size is 2GB.");
    }
    
    // SECURITY: Check for potential overflow in buffer size calculation
    if (cryptoinput.size() > SIZE_MAX - GCM_TAG_LENGTH) {
        throw error("Input size would cause integer overflow in buffer allocation.");
    }

    // Allocate output buffer for encrypted data and auth tag
    // The output buffer needs to hold: encrypted data + auth tag
    std::vector<uint8_t> cryptooutput(cryptoinput.size() + GCM_TAG_LENGTH);
    std::vector<uint8_t> tag(GCM_TAG_LENGTH);
    int inlen = static_cast<int>(cryptoinput.size());
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        throw openssl_error(0, "Failed to allocate EVP_CIPHER_CTX for encryption");
    }

    // Initialize AES-256-GCM encryption
    int res = EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, m_key.data(), nonce.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Encrypt data
    res = EVP_EncryptUpdate(ctx.get(), cryptooutput.data(), &outlen, cryptoinput.data(), inlen);
    throw_if_error(res, __FILE__, __LINE__);
    total_out += outlen;

    // Finalize encryption
    res = EVP_EncryptFinal_ex(ctx.get(), cryptooutput.data() + total_out, &outlen);
    throw_if_error(res, __FILE__, __LINE__);
    total_out += outlen;

    // Get the authentication tag
    res = EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, tag.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Ensure output buffer is the right size
    cryptooutput.resize(total_out);

    // Create result: nonce + ciphertext + tag
    // SECURITY: Calculate total size and check for overflow
    size_t totalSize = nonce.size() + cryptooutput.size() + tag.size();
    if (totalSize < nonce.size() || totalSize < cryptooutput.size() || totalSize < tag.size()) {
        throw error("Integer overflow in result buffer size calculation.");
    }
    
    // SECURITY: Check if total size exceeds reasonable limits
    if (totalSize > static_cast<size_t>(INT_MAX)) {
        throw error("Result buffer size exceeds maximum allowed size.");
    }
    
    QByteArray result;
    result.reserve(static_cast<int>(totalSize));

    // Add nonce (12 bytes)
    for (size_t i = 0; i < nonce.size(); ++i) {
        result.append(static_cast<char>(nonce[i]));
    }

    // Add ciphertext
    for (size_t i = 0; i < cryptooutput.size(); ++i) {
        result.append(static_cast<char>(cryptooutput[i]));
    }

    // Add tag (16 bytes)
    for (size_t i = 0; i < tag.size(); ++i) {
        result.append(static_cast<char>(tag[i]));
    }

    // SECURITY: Clean up sensitive data from memory
    OPENSSL_cleanse(cryptoinput.data(), cryptoinput.size());
    OPENSSL_cleanse(cryptooutput.data(), cryptooutput.size());
    OPENSSL_cleanse(tag.data(), tag.size());
    OPENSSL_cleanse(nonce.data(), nonce.size());

    return result;
}

QString AESGCM256Crypto::decrypt(const QByteArray& data) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Decryption key is not set. Call setKey() before decrypting.");
    }

    // SECURITY: Validate minimum size before any calculations
    const size_t minimumSize = GCM_NONCE_LENGTH + GCM_TAG_LENGTH;
    if (static_cast<size_t>(data.size()) < minimumSize) {
        throw error("Invalid encrypted data size: too small");
    }
    
    // SECURITY: Safe calculation of ciphertext length with underflow check
    if (static_cast<size_t>(data.size()) < (GCM_NONCE_LENGTH + GCM_TAG_LENGTH)) {
        throw error("Invalid encrypted data: size calculation underflow");
    }
    
    size_t ciphertextLengthSize = static_cast<size_t>(data.size()) - GCM_NONCE_LENGTH - GCM_TAG_LENGTH;
    
    if (ciphertextLengthSize > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for decryption. Maximum supported size is 2GB.");
    }
    
    int ciphertextLength = static_cast<int>(ciphertextLengthSize);

    // Extract nonce (first 12 bytes)
    std::vector<uint8_t> nonce(GCM_NONCE_LENGTH);
    for (int i = 0; i < GCM_NONCE_LENGTH; ++i) {
        nonce[i] = static_cast<uint8_t>(data[i]);
    }

    // Extract tag (last 16 bytes)
    std::vector<uint8_t> tag(GCM_TAG_LENGTH);
    for (int i = 0; i < GCM_TAG_LENGTH; ++i) {
        tag[i] = static_cast<uint8_t>(data[data.size() - GCM_TAG_LENGTH + i]);
    }
    // Extract ciphertext (everything in the middle)
    std::vector<uint8_t> ciphertext(ciphertextLength);
    for (int i = 0; i < ciphertextLength; ++i) {
        ciphertext[i] = static_cast<uint8_t>(data[GCM_NONCE_LENGTH + i]);
    }

    // Prepare output buffer for decrypted data
    std::vector<uint8_t> plaintext(ciphertextLength);
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        // Clean up extracted data before throwing
        OPENSSL_cleanse(nonce.data(), nonce.size());
        OPENSSL_cleanse(tag.data(), tag.size());
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        throw openssl_error(0, "Failed to allocate EVP_CIPHER_CTX for decryption");
    }

    // Initialize AES-256-GCM decryption
    int res = EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, m_key.data(), nonce.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Set the expected authentication tag
    res = EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, tag.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Decrypt data
    res = EVP_DecryptUpdate(ctx.get(), plaintext.data(), &outlen, ciphertext.data(), ciphertextLength);
    throw_if_error(res, __FILE__, __LINE__);
    total_out += outlen;

    // Finalize decryption and verify tag
    res = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + total_out, &outlen);
    if (res <= 0) {
        // SECURITY: Clean up sensitive data before throwing
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        OPENSSL_cleanse(nonce.data(), nonce.size());
        OPENSSL_cleanse(tag.data(), tag.size());
        throw error("Authentication failed: Data may be corrupted or tampered with");
    }
    total_out += outlen;

    // Ensure output buffer is the right size
    plaintext.resize(total_out);

    // Convert decrypted data to QString from UTF-8
    QByteArray decryptedBytes(reinterpret_cast<const char*>(plaintext.data()), static_cast<int>(plaintext.size()));
    QString result = QString::fromUtf8(decryptedBytes);

    // SECURITY: Clean up sensitive data from memory
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
    OPENSSL_cleanse(nonce.data(), nonce.size());
    OPENSSL_cleanse(tag.data(), tag.size());

    return result;
}

QByteArray AESGCM256Crypto::encryptBinary(const QByteArray& data, const QString& username) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Encryption key is not set. Call setKey() before encrypting.");
    }

    //qDebug() << "encryptBinary: Input data size:" << data.size() << "bytes";

    // SECURITY: Check input size before conversion
    if (data.size() < 0 || static_cast<size_t>(data.size()) > static_cast<size_t>(INT_MAX)) {
        throw error("Invalid input data size.");
    }

    // Convert QByteArray directly to vector<uint8_t> for encryption
    std::vector<uint8_t> cryptoinput(data.size());
    for (int i = 0; i < data.size(); ++i) {
        cryptoinput[i] = static_cast<uint8_t>(data.at(i));
    }

    // Generate system nonce for this encryption operation
    std::vector<uint8_t> nonce = generateNonce(username);

    // SECURITY: Check for integer overflow before allocating buffers
    if (cryptoinput.size() > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for encryption. Maximum supported size is 2GB.");
    }
    
    // SECURITY: Check for potential overflow in buffer size calculation
    if (cryptoinput.size() > SIZE_MAX - GCM_TAG_LENGTH) {
        throw error("Input size would cause integer overflow in buffer allocation.");
    }

    // Allocate output buffer for encrypted data and auth tag
    std::vector<uint8_t> cryptooutput(cryptoinput.size() + GCM_TAG_LENGTH);
    std::vector<uint8_t> tag(GCM_TAG_LENGTH);
    int inlen = static_cast<int>(cryptoinput.size());
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        throw openssl_error(0, "Failed to allocate EVP_CIPHER_CTX for binary encryption");
    }

    // Initialize AES-256-GCM encryption
    int res = EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, m_key.data(), nonce.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Encrypt data
    res = EVP_EncryptUpdate(ctx.get(), cryptooutput.data(), &outlen, cryptoinput.data(), inlen);
    throw_if_error(res, __FILE__, __LINE__);
    total_out += outlen;

    // Finalize encryption
    res = EVP_EncryptFinal_ex(ctx.get(), cryptooutput.data() + total_out, &outlen);
    throw_if_error(res, __FILE__, __LINE__);
    total_out += outlen;

    // Get the authentication tag
    res = EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, tag.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Ensure output buffer is the right size
    cryptooutput.resize(total_out);

    // Create result: nonce + ciphertext + tag
    // SECURITY: Calculate total size and check for overflow
    size_t totalSize = nonce.size() + cryptooutput.size() + tag.size();
    if (totalSize < nonce.size() || totalSize < cryptooutput.size() || totalSize < tag.size()) {
        throw error("Integer overflow in result buffer size calculation.");
    }
    
    // SECURITY: Check if total size exceeds reasonable limits
    if (totalSize > static_cast<size_t>(INT_MAX)) {
        throw error("Result buffer size exceeds maximum allowed size.");
    }
    
    QByteArray result;
    result.reserve(static_cast<int>(totalSize));

    // Add nonce (12 bytes)
    for (size_t i = 0; i < nonce.size(); ++i) {
        result.append(static_cast<char>(nonce[i]));
    }

    // Add ciphertext
    for (size_t i = 0; i < cryptooutput.size(); ++i) {
        result.append(static_cast<char>(cryptooutput[i]));
    }

    // Add tag (16 bytes)
    for (size_t i = 0; i < tag.size(); ++i) {
        result.append(static_cast<char>(tag[i]));
    }

    // SECURITY: Clean up sensitive data from memory
    OPENSSL_cleanse(cryptoinput.data(), cryptoinput.size());
    OPENSSL_cleanse(cryptooutput.data(), cryptooutput.size());
    OPENSSL_cleanse(tag.data(), tag.size());
    OPENSSL_cleanse(nonce.data(), nonce.size());

    //qDebug() << "encryptBinary: Output data size:" << result.size() << "bytes";
    return result;
}

QByteArray AESGCM256Crypto::decryptBinary(const QByteArray& data) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Decryption key is not set. Call setKey() before decrypting.");
    }

    qDebug() << "AESGCM256Crypto: decryptBinary: Input data size:" << data.size() << "bytes";

    // SECURITY: Validate minimum size before any calculations
    const size_t minimumSize = GCM_NONCE_LENGTH + GCM_TAG_LENGTH;
    if (static_cast<size_t>(data.size()) < minimumSize) {
        throw error("Invalid encrypted data size: too small");
    }
    
    // SECURITY: Safe calculation of ciphertext length with underflow check
    if (static_cast<size_t>(data.size()) < (GCM_NONCE_LENGTH + GCM_TAG_LENGTH)) {
        throw error("Invalid encrypted data: size calculation underflow");
    }
    
    size_t ciphertextLengthSize = static_cast<size_t>(data.size()) - GCM_NONCE_LENGTH - GCM_TAG_LENGTH;
    
    if (ciphertextLengthSize > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for decryption. Maximum supported size is 2GB.");
    }
    
    int ciphertextLength = static_cast<int>(ciphertextLengthSize);

    // Extract nonce (first 12 bytes)
    std::vector<uint8_t> nonce(GCM_NONCE_LENGTH);
    for (int i = 0; i < GCM_NONCE_LENGTH; ++i) {
        nonce[i] = static_cast<uint8_t>(data[i]);
    }

    // Extract tag (last 16 bytes)
    std::vector<uint8_t> tag(GCM_TAG_LENGTH);
    for (int i = 0; i < GCM_TAG_LENGTH; ++i) {
        tag[i] = static_cast<uint8_t>(data[data.size() - GCM_TAG_LENGTH + i]);
    }

    // Extract ciphertext (everything in the middle)
    std::vector<uint8_t> ciphertext(ciphertextLength);
    for (int i = 0; i < ciphertextLength; ++i) {
        ciphertext[i] = static_cast<uint8_t>(data[GCM_NONCE_LENGTH + i]);
    }

    // Prepare output buffer for decrypted data
    std::vector<uint8_t> plaintext(ciphertextLength);
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        // Clean up extracted data before throwing
        OPENSSL_cleanse(nonce.data(), nonce.size());
        OPENSSL_cleanse(tag.data(), tag.size());
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        throw openssl_error(0, "Failed to allocate EVP_CIPHER_CTX for binary decryption");
    }

    // Initialize AES-256-GCM decryption
    int res = EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, m_key.data(), nonce.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Set the expected authentication tag
    res = EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, tag.data());
    throw_if_error(res, __FILE__, __LINE__);

    // Decrypt data
    res = EVP_DecryptUpdate(ctx.get(), plaintext.data(), &outlen, ciphertext.data(), ciphertextLength);
    throw_if_error(res, __FILE__, __LINE__);
    total_out += outlen;

    // Finalize decryption and verify tag
    res = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + total_out, &outlen);
    if (res <= 0) {
        // SECURITY: Clean up sensitive data before throwing
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        OPENSSL_cleanse(nonce.data(), nonce.size());
        OPENSSL_cleanse(tag.data(), tag.size());
        throw error("Authentication failed: Data may be corrupted or tampered with");
    }
    total_out += outlen;

    // Ensure output buffer is the right size
    plaintext.resize(total_out);

    // Convert decrypted data to QByteArray directly (not QString)
    QByteArray result;
    for (size_t i = 0; i < plaintext.size(); ++i) {
        result.append(static_cast<char>(plaintext[i]));
    }

    // SECURITY: Clean up sensitive data from memory
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
    OPENSSL_cleanse(nonce.data(), nonce.size());
    OPENSSL_cleanse(tag.data(), tag.size());

    qDebug() << "AESGCM256Crypto: decryptBinary: Output data size:" << result.size() << "bytes";
    return result;
}

std::vector<uint8_t> AESGCM256Crypto::generateNonce(const QString& username) {
    // SECURITY: Create a fully random 96-bit nonce (12 bytes) using OpenSSL's CSPRNG
    std::vector<uint8_t> nonce(GCM_NONCE_LENGTH);

    // Primary: Use OpenSSL's RAND_bytes for cryptographically secure random generation
    // RAND_bytes returns 1 on success, 0 otherwise
    if (RAND_bytes(nonce.data(), GCM_NONCE_LENGTH) != 1) {
        // Log detailed OpenSSL error for debugging
        unsigned long err = ERR_get_error();
        #ifdef QT_DEBUG
        if (err != 0) {
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            qCritical() << "AESGCM256Crypto: RAND_bytes failed with OpenSSL error:" << err_buf;
        } else {
            qCritical() << "AESGCM256Crypto: RAND_bytes failed with no error code";
        }
        #else
        // In release mode, log minimal info
        qWarning() << "AESGCM256Crypto: RAND_bytes failed, using fallback RNG";
        #endif
        
        // Fallback: Use Qt's system random generator if OpenSSL fails
        // QRandomGenerator::system() uses OS-provided cryptographically secure RNG
        for (int i = 0; i < GCM_NONCE_LENGTH; ++i) {
            nonce[i] = static_cast<uint8_t>(QRandomGenerator::system()->bounded(256));
        }
    }

    return nonce;
}

std::vector<uint8_t> AESGCM256Crypto::str2Bytes(const std::string& message) {
    //qDebug() << "Converting string to bytes, length:" << message.length() << "bytes";
    if (message.empty()) {
        #ifdef QT_DEBUG
        qWarning() << "AESGCM256Crypto: Warning: Empty string passed to str2Bytes";
        #endif
    }

    // SECURITY: Validate size before allocation
    if (message.size() > SIZE_MAX) {
        throw error("String size exceeds maximum allowed size");
    }

    std::vector<uint8_t> out(message.size());
    for(size_t n = 0; n < message.size(); n++) {
        out[n] = static_cast<uint8_t>(message[n]);
    }

    //qDebug() << "String converted to" << out.size() << "bytes";
    return out;
}

// New helper method for QByteArray to vector<uint8_t> conversion
std::vector<uint8_t> AESGCM256Crypto::QByteArray2Bytes(const QByteArray& data) {
    //qDebug() << "Converting QByteArray to bytes, length:" << data.length() << "bytes";
    if (data.isEmpty()) {
        #ifdef QT_DEBUG
        qWarning() << "AESGCM256Crypto: Warning: Empty QByteArray passed to QByteArray2Bytes";
        #endif
    }

    // SECURITY: Validate size before allocation
    if (data.size() < 0 || static_cast<size_t>(data.size()) > SIZE_MAX) {
        throw error("Invalid QByteArray size for conversion");
    }

    std::vector<uint8_t> out(data.size());
    for(int n = 0; n < data.size(); n++) {
        out[n] = static_cast<uint8_t>(data[n]);
    }

    //qDebug() << "QByteArray converted to" << out.size() << "bytes";
    return out;
}

std::string AESGCM256Crypto::bytes2Str(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}
