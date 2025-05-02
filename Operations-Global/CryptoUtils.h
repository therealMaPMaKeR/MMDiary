#ifndef CRYPTOUTILS_H
#define CRYPTOUTILS_H
#include <QString>
#include <QFile>
#include <QByteArray>
// Replace AES256Crypto with AESGCM256
#include "../QT_AESGCM256/AESGCM256.h"

namespace CryptoUtils {

// Password hashing functions
QString Hashing_HashPassword(const QString& password);
bool Hashing_CompareHash(const QString& hashedPassword, const QString& password);

// Encryption key generation and derivation
QByteArray Encryption_GenerateKey();
QByteArray Encryption_DeriveKey(const QString& deriveFrom, QByteArray* outSalt = nullptr);
QByteArray Encryption_DeriveWithSalt(const QString& deriveFrom, const QByteArray& salt);

// Text encryption/decryption
QString Encryption_Encrypt(const QByteArray& encryptionKey, const QString& textToEncrypt, const QString& username = "");
QString Encryption_Decrypt(const QByteArray& encryptionKey, const QString& textToDecrypt);

// File encryption/decryption
bool Encryption_EncryptFile(const QByteArray& encryptionKey, const QString& sourceFilePath, const QString& destFilePath, const QString& username = "");
bool Encryption_DecryptFile(const QByteArray& encryptionKey, const QString& sourceFilePath, const QString& destFilePath);
// ByteArray encryption/decryption
QByteArray Encryption_EncryptBArray(const QByteArray& encryptionKey, const QByteArray& byteArrayToEncrypt, const QString& username);
QByteArray Encryption_DecryptBArray(const QByteArray& encryptionKey, const QByteArray& dataToDecrypt);
// Debug helper
void DebugKey(const QByteArray& encryptionKey, const QString& label);

} // namespace CryptoUtils
#endif // CRYPTOUTILS_H
