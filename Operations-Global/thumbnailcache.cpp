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
            qWarning() << "Failed to decrypt cached thumbnail (corrupted or wrong key):" << cacheFilePath;
            // Delete the invalid cache file
            QFile::remove(cacheFilePath);
            return QPixmap();
        }

        qDebug() << "Decrypted cache data, size:" << decryptedData.size() << "chars";

        // Convert base64 data back to QPixmap
        QByteArray pixmapData = QByteArray::fromBase64(decryptedData.toLatin1());
        QPixmap thumbnail;
        if (!thumbnail.loadFromData(pixmapData, "PNG")) {
            qWarning() << "Failed to load pixmap from cached data:" << cacheFilePath;
            // Delete the invalid cache file
            QFile::remove(cacheFilePath);
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
        // Delete the problematic cache file
        QFile::remove(cacheFilePath);
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
        QString encryptedData = CryptoUtils::Encryption_Encrypt(m_encryptionKey, base64Data);
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
    qDebug() << "=== CLEANUP ORPHANED THUMBNAILS DEBUG ===";
    qDebug() << "Valid file paths count:" << validEncryptedFilePaths.size();

    QDir cacheDir(m_cacheDirectory);
    if (!cacheDir.exists()) {
        qDebug() << "Cache directory doesn't exist, nothing to clean up";
        return;
    }

    // Create set of valid cache keys for faster lookup
    QSet<QString> validCacheKeys;
    for (const QString& filePath : validEncryptedFilePaths) {
        QString cacheKey = getCacheKey(filePath);
        validCacheKeys.insert(cacheKey);
        qDebug() << "VALID PATH:" << filePath;
        qDebug() << "VALID KEY:" << cacheKey;
    }

    qDebug() << "Total valid cache keys:" << validCacheKeys.size();

    // Get all thumbnail files
    QStringList filters;
    filters << "*.thumbnail";
    QFileInfoList fileList = cacheDir.entryInfoList(filters, QDir::Files);

    qDebug() << "Found" << fileList.size() << "thumbnail cache files";

    int removedCount = 0;
    for (const QFileInfo& fileInfo : fileList) {
        QString baseName = fileInfo.baseName(); // filename without .thumbnail extension
        QString filePath = fileInfo.absoluteFilePath();

        qDebug() << "CACHE FILE:" << filePath;
        qDebug() << "CACHE KEY (basename):" << baseName;
        qDebug() << "IS VALID:" << validCacheKeys.contains(baseName);

        if (!validCacheKeys.contains(baseName)) {
            qDebug() << "DELETING ORPHANED CACHE FILE:" << filePath;
            if (QFile::remove(filePath)) {
                removedCount++;
                qDebug() << "Successfully deleted orphaned cache file";
            } else {
                qDebug() << "Failed to delete orphaned cache file";
            }
        } else {
            qDebug() << "Keeping valid cache file:" << filePath;
        }
    }

    if (removedCount > 0) {
        qDebug() << "Cleaned up" << removedCount << "orphaned thumbnail cache files";
    } else {
        qDebug() << "No orphaned thumbnail files found";
    }
    qDebug() << "=== END CLEANUP DEBUG ===";
}


bool ThumbnailCache::validateThumbnailCacheFile(const QString& cacheFilePath)
{
    if (!QFile::exists(cacheFilePath)) {
        return false;
    }

    // For thumbnail cache files, we'll do a simpler validation:
    // Just check if the file exists and has reasonable size
    QFileInfo fileInfo(cacheFilePath);

    // Basic size check - thumbnail cache files should be between 1KB and 50KB
    qint64 fileSize = fileInfo.size();
    if (fileSize < 1024 || fileSize > 51200) {
        qDebug() << "Thumbnail cache file size out of range:" << fileSize << "bytes";
        return false;
    }

    // File exists and has reasonable size - assume it's valid
    // We'll discover if it's actually corrupt when we try to decrypt it in getThumbnail()
    qDebug() << "Thumbnail cache file validation passed (basic check):" << cacheFilePath;
    return true;
}
