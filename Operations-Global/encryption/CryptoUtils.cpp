#include "CryptoUtils.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QMessageBox>
#include <climits>
#include <cstddef>  // For SIZE_MAX
#include <openssl/crypto.h> // For OPENSSL_cleanse

namespace CryptoUtils {

// Constants
const int SALT_SIZE = 16; // 16 bytes (128 bits) salt
// Size limit for file operations - consistent with operations_files.cpp
// For larger files, use dedicated encryption worker classes
const qint64 MAX_FILE_SIZE = 50 * 1024 * 1024; // 50MB limit
#ifdef QT_DEBUG
const int PBKDF2_ITERATIONS = 500; // Number of iterations for PBKDF2 in debug mode
#else
const int PBKDF2_ITERATIONS = 1000000; // Number of iterations for PBKDF2 in release mode
#endif

QByteArray generateSalt() {
    QByteArray salt;
    salt.resize(SALT_SIZE);

    // Fill with random bytes using system() for better randomness
    for (int i = 0; i < SALT_SIZE; i++) {
        salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }

    return salt;
}

QString Hashing_HashPassword(const QString& password) {
    // Generate salt
    QByteArray salt = generateSalt();

    // Use Qt's QPasswordDigestor to create PBKDF2 hash with SHA256
    QByteArray passwordBytes = password.toUtf8();
    QByteArray hash = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256,
        passwordBytes,
        salt,
        PBKDF2_ITERATIONS,
        32 // 32 bytes output length
        );

    // Combine salt and hash (salt:hash)
    QByteArray result = salt.toBase64() + ":" + hash.toBase64();

    return QString::fromLatin1(result);
}

bool Hashing_CompareHash(const QString& hashedPassword, const QString& password) {
    // Split stored hash into salt and hash components
    QStringList parts = hashedPassword.split(':');
    if (parts.size() != 2) {
        qWarning() << "Invalid hash format";
        return false;
    }

    QByteArray salt = QByteArray::fromBase64(parts[0].toLatin1());
    QByteArray storedHash = QByteArray::fromBase64(parts[1].toLatin1());

    // Compute hash with same salt and iterations
    QByteArray passwordBytes = password.toUtf8();
    QByteArray computedHash = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256,
        passwordBytes,
        salt,
        PBKDF2_ITERATIONS,
        32 // 32 bytes output length
        );

    // Clear password bytes immediately after use with OPENSSL_cleanse
    if (!passwordBytes.isEmpty()) {
        OPENSSL_cleanse(passwordBytes.data(), passwordBytes.size());
        passwordBytes.clear();
    }

    // Constant-time comparison to prevent timing attacks
    if (computedHash.size() != storedHash.size()) {
        return false;
    }
    
    volatile unsigned char result = 0;
    for (int i = 0; i < computedHash.size(); ++i) {
        result |= static_cast<unsigned char>(computedHash[i]) ^ static_cast<unsigned char>(storedHash[i]);
    }
    
    // Clear sensitive data with OPENSSL_cleanse
    if (!computedHash.isEmpty()) {
        OPENSSL_cleanse(computedHash.data(), computedHash.size());
        computedHash.clear();
    }
    
    return result == 0;
}

QByteArray Encryption_GenerateKey() {
    // Generate a random 32-byte key for AES-256
    QByteArray key;
    key.resize(32); // 32 bytes = 256 bits

    // Fill with random bytes using system() for better randomness
    for (int i = 0; i < 32; i++) {
        key[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }

    return key;
}

QByteArray Encryption_DeriveWithSalt(const QString& deriveFrom, const QByteArray& salt) {
    // Use Qt's QPasswordDigestor to create PBKDF2 hash with SHA256
    QByteArray inputBytes = deriveFrom.toUtf8();
    QByteArray derivedKey = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256,
        inputBytes,
        salt,
        PBKDF2_ITERATIONS,
        32 // 32 bytes output length
        );

    // Clear input bytes after use
    if (!inputBytes.isEmpty()) {
        OPENSSL_cleanse(inputBytes.data(), inputBytes.size());
        inputBytes.clear();
    }

    return derivedKey;
}

QByteArray Encryption_DeriveKey(const QString& deriveFrom, QByteArray* outSalt) {
    // Generate salt
    QByteArray salt = generateSalt();

    // If outSalt pointer provided, store the salt there for external use
    if (outSalt) {
        *outSalt = salt;
    }

    // Use Qt's QPasswordDigestor to create PBKDF2 hash with SHA256
    QByteArray inputBytes = deriveFrom.toUtf8();
    QByteArray derivedKey = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256,
        inputBytes,
        salt,
        PBKDF2_ITERATIONS,
        32 // 32 bytes output length
        );

    // Clear input bytes after use
    if (!inputBytes.isEmpty()) {
        OPENSSL_cleanse(inputBytes.data(), inputBytes.size());
        inputBytes.clear();
    }

    // Combine salt and key
    QByteArray result = salt + derivedKey;

    return result;
}

// Updated encryption function using AESGCM256Crypto with QByteArray key
QString Encryption_Encrypt(const QByteArray& encryptionKey, const QString& textToEncrypt, const QString& username) {
    // Check if key has correct size for AES-256
    if (encryptionKey.size() != 32) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return QString();
    }

    // SECURITY: Check input text size to prevent overflow
    if (textToEncrypt.isEmpty()) {
        qWarning() << "Empty text provided for encryption";
        return QString();
    }
    
    // Check for integer overflow in size calculation
    qint64 textSize = static_cast<qint64>(textToEncrypt.size());
    if (textSize > INT_MAX / 4 || textSize < 0) { // UTF-8 can expand up to 4x
        qWarning() << "Text too large for encryption or invalid size";
        return QString();
    }

    try {
        // Create AESGCM256Crypto instance with our key
        std::string key(encryptionKey.constData(), encryptionKey.size());
        AESGCM256Crypto crypto(key);

        // Encrypt the data
        QByteArray encryptedData = crypto.encrypt(textToEncrypt, username);

        // Return base64 encoded result
        return QString::fromLatin1(encryptedData.toBase64());
    } catch (const std::exception& e) {
        qCritical() << "Exception during encryption:" << e.what();
        return QString();
    }
}

QString Encryption_Decrypt(const QByteArray& encryptionKey, const QString& textToDecrypt) {
    // Check if key has correct size for AES-256 using constant-time comparison
    volatile bool validKeySize = (encryptionKey.size() == 32);
    if (!validKeySize) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return QString();
    }

    // SECURITY: Validate base64 input size
    if (textToDecrypt.isEmpty()) {
        qWarning() << "Empty text provided for decryption";
        return QString();
    }
    
    // Check for valid base64 and size limits
    qint64 inputSize = static_cast<qint64>(textToDecrypt.size());
    if (inputSize > INT_MAX || inputSize < 0) {
        qWarning() << "Invalid base64 input size";
        return QString();
    }
    
    // Basic base64 validation - length should be multiple of 4
    if (textToDecrypt.size() % 4 != 0) {
        qWarning() << "Invalid base64 format - incorrect padding";
        return QString();
    }

    try {
        // Convert base64 encoded data to byte array
        QByteArray cipherTextBytes = QByteArray::fromBase64(textToDecrypt.toLatin1());
        
        // SECURITY: Validate decoded size
        if (cipherTextBytes.isEmpty()) {
            qWarning() << "Failed to decode base64 data";
            return QString();
        }

        // Create AESGCM256Crypto instance with our key
        std::string key(encryptionKey.constData(), encryptionKey.size());
        AESGCM256Crypto crypto(key);

        // Decrypt the data
        return crypto.decrypt(cipherTextBytes);
    } catch (const std::exception& e) {
        qCritical() << "Exception during decryption:" << e.what();
        return QString();
    }
}

bool Encryption_EncryptFile(const QByteArray& encryptionKey, const QString& sourceFilePath, const QString& destFilePath, const QString& username) {
    // Check if key has correct size for AES-256
    if (encryptionKey.size() != 32) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return false;
    }

    try {
        // Open source file
        QFile sourceFile(sourceFilePath);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Could not open source file for reading:" << sourceFilePath;
            return false;
        }

        // SECURITY: Check file size before reading into memory
        qint64 fileSize = sourceFile.size();
        if (fileSize < 0 || fileSize > MAX_FILE_SIZE) {
            qWarning() << "CryptoUtils: File too large for Encryption_EncryptFile:"
                       << fileSize << "bytes (max:" << MAX_FILE_SIZE << "bytes)";
            qWarning() << "CryptoUtils: Use dedicated encryption worker classes for large files";
            sourceFile.close();
            return false;
        }

        // Read file content
        QByteArray fileData = sourceFile.readAll();
        sourceFile.close();
        
        // SECURITY: Validate read data size matches file size
        if (fileData.size() != static_cast<int>(fileSize)) {
            qWarning() << "File read size mismatch. Expected:" << fileSize << "Got:" << fileData.size();
            return false;
        }

        // Create AESGCM256Crypto instance with our key
        std::string key(encryptionKey.constData(), encryptionKey.size());
        AESGCM256Crypto crypto(key);

        // Encrypt the data
        QByteArray encryptedData = crypto.encrypt(QString::fromUtf8(fileData), username);

        // Open destination file
        QFile destFile(destFilePath);
        if (!destFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Could not open destination file for writing:" << destFilePath;
            return false;
        }

        // Write encrypted data
        destFile.write(encryptedData);
        destFile.close();

        return true;
    } catch (const std::exception& e) {
        qCritical() << "Exception during file encryption:" << e.what();
        return false;
    }
}

bool Encryption_DecryptFile(const QByteArray& encryptionKey, const QString& sourceFilePath, const QString& destFilePath) {
    // Check if key has correct size for AES-256
    if (encryptionKey.size() != 32) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return false;
    }

    try {
        // Open source file
        QFile sourceFile(sourceFilePath);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Could not open encrypted file for reading:" << sourceFilePath;
            return false;
        }

        // SECURITY: Check file size before reading into memory
        qint64 fileSize = sourceFile.size();
        if (fileSize < 0 || fileSize > MAX_FILE_SIZE) {
            qWarning() << "CryptoUtils: File too large for Encryption_DecryptFile:"
                       << fileSize << "bytes (max:" << MAX_FILE_SIZE << "bytes)";
            qWarning() << "CryptoUtils: Use dedicated encryption worker classes for large files";
            sourceFile.close();
            return false;
        }

        // Read encrypted content
        QByteArray fileData = sourceFile.readAll();
        sourceFile.close();
        
        // SECURITY: Validate read data size matches file size
        if (fileData.size() != static_cast<int>(fileSize)) {
            qWarning() << "File read size mismatch. Expected:" << fileSize << "Got:" << fileData.size();
            return false;
        }

        // Create AESGCM256Crypto instance with our key
        std::string key(encryptionKey.constData(), encryptionKey.size());
        AESGCM256Crypto crypto(key);

        // Decrypt the data
        QString decryptedText = crypto.decrypt(fileData);
        QByteArray decryptedData = decryptedText.toUtf8();

        // Open destination file
        QFile destFile(destFilePath);
        if (!destFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Could not open destination file for writing:" << destFilePath;
            return false;
        }

        // Write decrypted data
        qint64 bytesWritten = destFile.write(decryptedData);
        destFile.close();

        return (bytesWritten == decryptedData.size());
    } catch (const std::exception& e) {
        qCritical() << "Exception during file decryption:" << e.what();
        return false;
    }
}

void DebugKey(const QByteArray& encryptionKey, const QString& label) {
    qDebug() << "========== DEBUG KEY: " << label << " ==========";
    qDebug() << "Key size:" << encryptionKey.size() << "bytes";

    if (!encryptionKey.isEmpty()) {
        qDebug() << "First few bytes of key (hex):" << encryptionKey.left(8).toHex();
    }

    qDebug() << "==============================================";
}

QByteArray Encryption_EncryptBArray(const QByteArray& encryptionKey, const QByteArray& byteArrayToEncrypt, const QString& username) {
    // Check if key has correct size for AES-256
    if (encryptionKey.size() != 32) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return QByteArray();
    }

    // SECURITY: Check input size to prevent overflow
    if (byteArrayToEncrypt.size() < 0 || byteArrayToEncrypt.size() > INT_MAX - 28) { // 28 = nonce(12) + tag(16)
        qWarning() << "Input byte array too large for encryption";
        return QByteArray();
    }

    try {
        // Create AESGCM256Crypto instance with our key (using the QByteArray constructor)
        AESGCM256Crypto crypto(encryptionKey);

        // Use the new binary method
        QByteArray encryptedData = crypto.encryptBinary(byteArrayToEncrypt, username);

        // Return encrypted data
        return encryptedData;
    } catch (const std::exception& e) {
        qCritical() << "Exception during encryption:" << e.what();
        return QByteArray();
    }
}

QByteArray Encryption_DecryptBArray(const QByteArray& encryptionKey, const QByteArray& dataToDecrypt) {
    // Check if key has correct size for AES-256
    if (encryptionKey.size() != 32) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return QByteArray();
    }

    // SECURITY: Validate minimum size (nonce + tag)
    if (dataToDecrypt.size() < 28) { // 28 = nonce(12) + tag(16)
        qWarning() << "Input too small for valid encrypted data";
        return QByteArray();
    }
    
    // SECURITY: Check maximum size
    if (dataToDecrypt.size() > INT_MAX) {
        qWarning() << "Input too large for decryption";
        return QByteArray();
    }

    try {
        // Create AESGCM256Crypto instance with our key (using the QByteArray constructor)
        AESGCM256Crypto crypto(encryptionKey);

        // Use the new binary method
        return crypto.decryptBinary(dataToDecrypt);
    } catch (const std::exception& e) {
        qCritical() << "Exception during decryption:" << e.what();
        return QByteArray();
    }
}
} // namespace CryptoUtils
