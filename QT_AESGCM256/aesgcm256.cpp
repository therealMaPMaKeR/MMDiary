#include "AESGCM256.h"
#include "Operations-Global/CryptoUtils.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QTextStream>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QElapsedTimer>
#include <climits>

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
    unsigned long errc = ERR_get_error();
    if (res <= 0 || errc != 0) {
        if (errc == 0) {
            return;
        }
        std::vector<std::string> errors;
        while (errc != 0) {
            std::vector<uint8_t> buf(256);
            ERR_error_string(errc, (char*) buf.data());
            errors.push_back(std::string(buf.begin(), buf.end()));
            errc = ERR_get_error();
        }

        std::stringstream ss;
        ss << "\n";
        for (auto&& err : errors) {
            if (file != nullptr) {
                ss << file << ":" << (line - 1) << " ";
            }
            ss << err << "\n";
        }
        const std::string err_all = ss.str();
        throw openssl_error(errc, err_all);
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

    // Allocate output buffer for encrypted data and auth tag
    // The output buffer needs to hold: encrypted data + auth tag
    std::vector<uint8_t> cryptooutput(cryptoinput.size() + GCM_TAG_LENGTH);
    std::vector<uint8_t> tag(GCM_TAG_LENGTH);

    if (cryptoinput.size() > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for encryption. Maximum supported size is 2GB.");
    }
    int inlen = static_cast<int>(cryptoinput.size());
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    throw_if_error(1, __FILE__, __LINE__);

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
    QByteArray result;

    // Add nonce (12 bytes)
    for (size_t i = 0; i < nonce.size(); ++i) {
        result.append(nonce[i]);
    }

    // Add ciphertext
    for (size_t i = 0; i < cryptooutput.size(); ++i) {
        result.append(cryptooutput[i]);
    }

    // Add tag (16 bytes)
    for (size_t i = 0; i < tag.size(); ++i) {
        result.append(tag[i]);
    }

    return result;
}

QString AESGCM256Crypto::decrypt(const QByteArray& data) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Decryption key is not set. Call setKey() before decrypting.");
    }

    // Data format: nonce (12 bytes) + ciphertext + tag (16 bytes)
    if (data.size() < GCM_NONCE_LENGTH + GCM_TAG_LENGTH) {
        throw error("Invalid encrypted data size");
    }

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
    if ((data.size() - GCM_NONCE_LENGTH - GCM_TAG_LENGTH) > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for decryption. Maximum supported size is 2GB.");
    }
    int ciphertextLength = static_cast<int>(data.size() - GCM_NONCE_LENGTH - GCM_TAG_LENGTH);
    std::vector<uint8_t> ciphertext(ciphertextLength);
    for (int i = 0; i < ciphertextLength; ++i) {
        ciphertext[i] = static_cast<uint8_t>(data[GCM_NONCE_LENGTH + i]);
    }

    // Prepare output buffer for decrypted data
    std::vector<uint8_t> plaintext(ciphertextLength);
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    throw_if_error(1, __FILE__, __LINE__);

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
        throw error("Authentication failed: Data may be corrupted or tampered with");
    }
    total_out += outlen;

    // Ensure output buffer is the right size
    plaintext.resize(total_out);

    // Convert decrypted data to QString
    QString result;
    for (size_t i = 0; i < plaintext.size(); ++i) {
        result.append(QLatin1Char(static_cast<char>(plaintext[i])));
    }

    return result;
}

QByteArray AESGCM256Crypto::encryptBinary(const QByteArray& data, const QString& username) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Encryption key is not set. Call setKey() before encrypting.");
    }

    //qDebug() << "encryptBinary: Input data size:" << data.size() << "bytes";

    // Convert QByteArray directly to vector<uint8_t> for encryption
    std::vector<uint8_t> cryptoinput(data.size());
    for (int i = 0; i < data.size(); ++i) {
        cryptoinput[i] = static_cast<uint8_t>(data.at(i));
    }

    // Generate system nonce for this encryption operation
    std::vector<uint8_t> nonce = generateNonce(username);

    // Allocate output buffer for encrypted data and auth tag
    std::vector<uint8_t> cryptooutput(cryptoinput.size() + GCM_TAG_LENGTH);
    std::vector<uint8_t> tag(GCM_TAG_LENGTH);

    if (cryptoinput.size() > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for encryption. Maximum supported size is 2GB.");
    }
    int inlen = static_cast<int>(cryptoinput.size());
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    throw_if_error(1, __FILE__, __LINE__);

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
    QByteArray result;

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

    //qDebug() << "encryptBinary: Output data size:" << result.size() << "bytes";
    return result;
}

QByteArray AESGCM256Crypto::decryptBinary(const QByteArray& data) {
    // Check if key is set
    if (m_key.empty()) {
        throw error("Decryption key is not set. Call setKey() before decrypting.");
    }

    qDebug() << "decryptBinary: Input data size:" << data.size() << "bytes";

    // Data format: nonce (12 bytes) + ciphertext + tag (16 bytes)
    if (data.size() < GCM_NONCE_LENGTH + GCM_TAG_LENGTH) {
        throw error("Invalid encrypted data size");
    }

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
    if ((data.size() - GCM_NONCE_LENGTH - GCM_TAG_LENGTH) > static_cast<size_t>(INT_MAX)) {
        throw error("Input too large for decryption. Maximum supported size is 2GB.");
    }
    int ciphertextLength = static_cast<int>(data.size() - GCM_NONCE_LENGTH - GCM_TAG_LENGTH);
    std::vector<uint8_t> ciphertext(ciphertextLength);
    for (int i = 0; i < ciphertextLength; ++i) {
        ciphertext[i] = static_cast<uint8_t>(data[GCM_NONCE_LENGTH + i]);
    }

    // Prepare output buffer for decrypted data
    std::vector<uint8_t> plaintext(ciphertextLength);
    int outlen = 0;
    size_t total_out = 0;

    EVP_CIPHER_CTX_t ctx(EVP_CIPHER_CTX_new());
    throw_if_error(1, __FILE__, __LINE__);

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

    qDebug() << "decryptBinary: Output data size:" << result.size() << "bytes";
    return result;
}

std::vector<uint8_t> AESGCM256Crypto::generateNonce(const QString& username) {
    // Create a fully random 96-bit nonce (12 bytes) using CSPRNG
    std::vector<uint8_t> nonce(GCM_NONCE_LENGTH);

    // Use QRandomGenerator::system() for cryptographically system random generation
    for (int i = 0; i < GCM_NONCE_LENGTH; ++i) {
        nonce[i] = static_cast<uint8_t>(QRandomGenerator::system()->bounded(256));
    }

    return nonce;
}

std::vector<uint8_t> AESGCM256Crypto::str2Bytes(const std::string& message) {
    //qDebug() << "Converting string to bytes, length:" << message.length() << "bytes";
    if (message.empty()) {
        qWarning() << "Warning: Empty string passed to str2Bytes";
    }

    std::vector<uint8_t> out(message.size());
    for(size_t n = 0; n < message.size(); n++) {
        out[n] = message[n];
    }

    //qDebug() << "String converted to" << out.size() << "bytes";
    return out;
}

// New helper method for QByteArray to vector<uint8_t> conversion
std::vector<uint8_t> AESGCM256Crypto::QByteArray2Bytes(const QByteArray& data) {
    //qDebug() << "Converting QByteArray to bytes, length:" << data.length() << "bytes";
    if (data.isEmpty()) {
        qWarning() << "Warning: Empty QByteArray passed to QByteArray2Bytes";
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
