#include "CryptoUtils.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QMessageBox>

namespace CryptoUtils {

// Constants
const int SALT_SIZE = 16; // 16 bytes (128 bits) salt
const int PBKDF2_ITERATIONS = 1000000; // Number of iterations for PBKDF2

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

    // Compare the stored hash with computed hash
    return (computedHash == storedHash);
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
    // Check if key has correct size for AES-256
    if (encryptionKey.size() != 32) {
        qWarning() << "Invalid key size:" << encryptionKey.size() << "bytes (expected 32 bytes)";
        return QString();
    }

    try {
        // Convert base64 encoded data to byte array
        QByteArray cipherTextBytes = QByteArray::fromBase64(textToDecrypt.toLatin1());

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

        // Read file content
        QByteArray fileData = sourceFile.readAll();
        sourceFile.close();

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

        // Read encrypted content
        QByteArray fileData = sourceFile.readAll();
        sourceFile.close();

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
