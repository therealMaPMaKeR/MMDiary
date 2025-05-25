#include "thumbnailcache.h"
#include "CryptoUtils.h"
#include "inputvalidation.h"
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QDebug>
#include <QBuffer>

ThumbnailCache::ThumbnailCache(const QString& username, const QByteArray& encryptionKey, QObject *parent)
    : QObject(parent)
    , m_username(username)
    , m_encryptionKey(encryptionKey)
{
    qDebug() << "ThumbnailCache constructor called";
    qDebug() << "Username:" << username;
    qDebug() << "Username empty?" << username.isEmpty();

    // Validate inputs
    if (username.isEmpty()) {
        qWarning() << "ThumbnailCache: Username is empty!";
        m_cacheDirectory = QString(); // Set to empty to prevent crashes
        return;
    }

    try {
        // Set up cache directory path: Data/username/ThumbnailCache/
        QString basePath = QDir::current().absoluteFilePath("Data");
        qDebug() << "Base path:" << basePath;

        QString userPath = QDir(basePath).absoluteFilePath(username);
        qDebug() << "User path:" << userPath;

        m_cacheDirectory = QDir(userPath).absoluteFilePath("ThumbnailCache");
        qDebug() << "Cache directory:" << m_cacheDirectory;

        ensureCacheDirectory();
        qDebug() << "ThumbnailCache constructor completed successfully";

    } catch (const std::exception& e) {
        qWarning() << "Exception in ThumbnailCache constructor:" << e.what();
        m_cacheDirectory = QString();
    } catch (...) {
        qWarning() << "Unknown exception in ThumbnailCache constructor";
        m_cacheDirectory = QString();
    }
}

ThumbnailCache::~ThumbnailCache()
{
    // Nothing special needed for cleanup
}

bool ThumbnailCache::ensureCacheDirectory()
{
    QDir cacheDir(m_cacheDirectory);
    if (!cacheDir.exists()) {
        if (!cacheDir.mkpath(".")) {
            qWarning() << "Failed to create thumbnail cache directory:" << m_cacheDirectory;
            return false;
        }
    }
    return true;
}

QString ThumbnailCache::getCacheKey(const QString& encryptedFilePath)
{
    qDebug() << "=== getCacheKey called ===";
    qDebug() << "encryptedFilePath:" << encryptedFilePath;

    // Create a hash of the file path for the cache key
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(encryptedFilePath.toUtf8());

    // Also include file modification time to invalidate cache if file changes
    QFileInfo fileInfo(encryptedFilePath);
    if (fileInfo.exists()) {
        QString modTime = fileInfo.lastModified().toString(Qt::ISODate);
        qDebug() << "File modification time:" << modTime;
        hash.addData(modTime.toUtf8());
    } else {
        qWarning() << "File does not exist for cache key generation:" << encryptedFilePath;
    }

    QString cacheKey = QString(hash.result().toHex());
    qDebug() << "Generated cache key:" << cacheKey;

    return cacheKey;
}

QString ThumbnailCache::getCacheFilePath(const QString& cacheKey)
{
    qDebug() << "getCacheFilePath called with cacheKey:" << cacheKey;
    qDebug() << "m_cacheDirectory:" << m_cacheDirectory;

    if (m_cacheDirectory.isEmpty()) {
        qWarning() << "Cache directory is empty!";
        return QString();
    }

    if (cacheKey.isEmpty()) {
        qWarning() << "Cache key is empty!";
        return QString();
    }

    try {
        QDir cacheDir(m_cacheDirectory);
        QString result = cacheDir.absoluteFilePath(cacheKey + ".thumbnail");
        qDebug() << "Generated cache file path:" << result;
        return result;
    } catch (...) {
        qWarning() << "Exception in getCacheFilePath";
        return QString();
    }
}

bool ThumbnailCache::hasThumbnail(const QString& encryptedFilePath)
{
    qDebug() << "=== hasThumbnail called ===";
    qDebug() << "encryptedFilePath:" << encryptedFilePath;

    if (m_cacheDirectory.isEmpty()) {
        qDebug() << "Cache directory is empty, returning false";
        return false;
    }

    if (encryptedFilePath.isEmpty()) {
        qDebug() << "Encrypted file path is empty, returning false";
        return false;
    }

    try {
        QString cacheKey = getCacheKey(encryptedFilePath);
        if (cacheKey.isEmpty()) {
            qDebug() << "Cache key is empty, returning false";
            return false;
        }

        QString cacheFilePath = getCacheFilePath(cacheKey);
        if (cacheFilePath.isEmpty()) {
            qDebug() << "Cache file path is empty, returning false";
            return false;
        }

        qDebug() << "Checking for cache file:" << cacheFilePath;
        bool exists = QFile::exists(cacheFilePath);
        qDebug() << "Cache file exists:" << exists;

        if (exists) {
            QFileInfo cacheFileInfo(cacheFilePath);
            qDebug() << "Cache file size:" << cacheFileInfo.size() << "bytes";
            qDebug() << "Cache file modified:" << cacheFileInfo.lastModified().toString(Qt::ISODate);
        }

        return exists;

    } catch (...) {
        qWarning() << "Exception in hasThumbnail";
        return false;
    }
}

QPixmap ThumbnailCache::getThumbnail(const QString& encryptedFilePath, int size)
{
    qDebug() << "=== getThumbnail called ===";

    QString cacheKey = getCacheKey(encryptedFilePath);
    QString cacheFilePath = getCacheFilePath(cacheKey);

    if (!QFile::exists(cacheFilePath)) {
        qDebug() << "Cache file does not exist:" << cacheFilePath;
        return QPixmap();
    }

    // Validate that we can decrypt this cache file with our key
    if (!validateThumbnailCacheFile(cacheFilePath)) {
        qWarning() << "Thumbnail cache file validation failed (wrong key or corrupted):" << cacheFilePath;
        // Delete the invalid cache file
        QFile::remove(cacheFilePath);
        return QPixmap();
    }
    qDebug() << "=== getThumbnail called ===";
    qDebug() << "encryptedFilePath:" << encryptedFilePath;
    qDebug() << "requested size:" << size;

    if (!QFile::exists(cacheFilePath)) {
        qDebug() << "Cache file does not exist:" << cacheFilePath;
        return QPixmap(); // No cached thumbnail
    }

    qDebug() << "Loading cached thumbnail from:" << cacheFilePath;

    try {
        // Read the encrypted cache file
        QFile cacheFile(cacheFilePath);
        if (!cacheFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open cache file:" << cacheFilePath;
            return QPixmap();
        }

        QByteArray encryptedData = cacheFile.readAll();
        cacheFile.close();

        qDebug() << "Read encrypted cache data, size:" << encryptedData.size() << "bytes";

        // Decrypt the cached thumbnail
        QString decryptedData = CryptoUtils::Encryption_Decrypt(m_encryptionKey,
                                                                QString::fromLatin1(encryptedData));

        if (decryptedData.isEmpty()) {
            qWarning() << "Failed to decrypt cached thumbnail:" << cacheFilePath;
            return QPixmap();
        }

        qDebug() << "Decrypted cache data, size:" << decryptedData.size() << "chars";

        // Convert base64 data back to QPixmap
        QByteArray pixmapData = QByteArray::fromBase64(decryptedData.toLatin1());
        QPixmap thumbnail;
        if (!thumbnail.loadFromData(pixmapData, "PNG")) {
            qWarning() << "Failed to load pixmap from cached data:" << cacheFilePath;
            return QPixmap();
        }

        qDebug() << "Successfully loaded thumbnail from cache, size:" << thumbnail.size();

        // Scale to requested size if different
        if (thumbnail.width() != size || thumbnail.height() != size) {
            thumbnail = thumbnail.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            qDebug() << "Scaled thumbnail to:" << thumbnail.size();
        }

        return thumbnail;

    } catch (const std::exception& e) {
        qWarning() << "Exception while loading cached thumbnail:" << e.what();
        return QPixmap();
    }
}

bool ThumbnailCache::storeThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail)
{
    qDebug() << "=== storeThumbnail called ===";
    qDebug() << "encryptedFilePath:" << encryptedFilePath;
    qDebug() << "thumbnail size:" << thumbnail.size();

    if (thumbnail.isNull() || !ensureCacheDirectory()) {
        qDebug() << "Thumbnail is null or cache directory creation failed";
        return false;
    }

    QString cacheKey = getCacheKey(encryptedFilePath);
    QString cacheFilePath = getCacheFilePath(cacheKey);

    qDebug() << "Storing thumbnail to:" << cacheFilePath;

    try {
        // Convert QPixmap to PNG data
        QByteArray pixmapData;
        QBuffer buffer(&pixmapData);
        buffer.open(QIODevice::WriteOnly);

        if (!thumbnail.save(&buffer, "PNG")) {
            qWarning() << "Failed to save thumbnail to buffer";
            return false;
        }

        qDebug() << "Converted thumbnail to PNG, size:" << pixmapData.size() << "bytes";

        // Convert to base64 for text-based encryption
        QString base64Data = QString::fromLatin1(pixmapData.toBase64());
        qDebug() << "Converted to base64, size:" << base64Data.size() << "chars";

        // Encrypt the thumbnail data
        QString encryptedData = CryptoUtils::Encryption_Encrypt(m_encryptionKey, base64Data, m_username);
        if (encryptedData.isEmpty()) {
            qWarning() << "Failed to encrypt thumbnail data";
            return false;
        }

        qDebug() << "Encrypted thumbnail data, size:" << encryptedData.size() << "chars";

        // Write encrypted data to cache file
        QFile cacheFile(cacheFilePath);
        if (!cacheFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to open cache file for writing:" << cacheFilePath;
            return false;
        }

        QByteArray encryptedBytes = encryptedData.toLatin1();
        qint64 bytesWritten = cacheFile.write(encryptedBytes);
        cacheFile.close();

        if (bytesWritten != encryptedBytes.size()) {
            qWarning() << "Failed to write complete encrypted data to cache file";
            QFile::remove(cacheFilePath); // Clean up partial file
            return false;
        }

        qDebug() << "Successfully stored thumbnail cache file, size:" << bytesWritten << "bytes";

        // Verify the file was written correctly
        if (QFile::exists(cacheFilePath)) {
            QFileInfo info(cacheFilePath);
            qDebug() << "Cache file verification - exists:" << true << "size:" << info.size() << "bytes";
        } else {
            qWarning() << "Cache file does not exist after writing!";
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception while storing thumbnail:" << e.what();
        return false;
    }
}

bool ThumbnailCache::removeThumbnail(const QString& encryptedFilePath)
{
    QString cacheKey = getCacheKey(encryptedFilePath);
    QString cacheFilePath = getCacheFilePath(cacheKey);

    if (QFile::exists(cacheFilePath)) {
        return QFile::remove(cacheFilePath);
    }

    return true; // Consider it successful if file doesn't exist
}

void ThumbnailCache::clearCache()
{
    QDir cacheDir(m_cacheDirectory);
    if (!cacheDir.exists()) {
        return;
    }

    // Get all thumbnail files
    QStringList filters;
    filters << "*.thumbnail";
    QFileInfoList fileList = cacheDir.entryInfoList(filters, QDir::Files);

    for (const QFileInfo& fileInfo : fileList) {
        QFile::remove(fileInfo.absoluteFilePath());
    }

    qDebug() << "Cleared thumbnail cache, removed" << fileList.size() << "files";
}

void ThumbnailCache::cleanupOrphanedThumbnails(const QStringList& validEncryptedFilePaths)
{
    QDir cacheDir(m_cacheDirectory);
    if (!cacheDir.exists()) {
        return;
    }

    // Create set of valid cache keys for faster lookup
    QSet<QString> validCacheKeys;
    for (const QString& filePath : validEncryptedFilePaths) {
        validCacheKeys.insert(getCacheKey(filePath));
    }

    // Get all thumbnail files
    QStringList filters;
    filters << "*.thumbnail";
    QFileInfoList fileList = cacheDir.entryInfoList(filters, QDir::Files);

    int removedCount = 0;
    for (const QFileInfo& fileInfo : fileList) {
        QString baseName = fileInfo.baseName(); // filename without .thumbnail extension

        if (!validCacheKeys.contains(baseName)) {
            if (QFile::remove(fileInfo.absoluteFilePath())) {
                removedCount++;
            }
        }
    }

    if (removedCount > 0) {
        qDebug() << "Cleaned up" << removedCount << "orphaned thumbnail cache files";
    }
}


bool ThumbnailCache::validateThumbnailCacheFile(const QString& cacheFilePath)
{
    if (!QFile::exists(cacheFilePath)) {
        return false;
    }

    try {
        // Try to decrypt a small portion to validate the key
        QFile cacheFile(cacheFilePath);
        if (!cacheFile.open(QIODevice::ReadOnly)) {
            return false;
        }

        // Read just a small portion for validation (first 100 bytes should be enough)
        QByteArray testData = cacheFile.read(100);
        cacheFile.close();

        if (testData.isEmpty()) {
            return false;
        }

        // Try to decrypt - if key is wrong, this will fail
        QString testDecrypt = CryptoUtils::Encryption_Decrypt(m_encryptionKey, QString::fromLatin1(testData));

        // If decryption succeeds (returns non-empty), the key is correct
        // Note: We don't care about the actual content, just that decryption didn't fail
        return !testDecrypt.isEmpty();

    } catch (...) {
        return false;
    }
}
