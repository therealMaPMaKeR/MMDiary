#ifndef THUMBNAIL_CACHE_H
#define THUMBNAIL_CACHE_H

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QDir>

class ThumbnailCache : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailCache(const QString& username, const QByteArray& encryptionKey, QObject *parent = nullptr);
    ~ThumbnailCache();

    // Check if thumbnail exists in cache
    bool hasThumbnail(const QString& encryptedFilePath);

    // Get thumbnail from cache
    QPixmap getThumbnail(const QString& encryptedFilePath, int size = 64);

    // Store thumbnail in cache
    bool storeThumbnail(const QString& encryptedFilePath, const QPixmap& thumbnail);

    // Clear entire cache
    void clearCache();

    // Remove specific thumbnail from cache
    bool removeThumbnail(const QString& encryptedFilePath);

    // Clean old thumbnails (files that no longer exist)
    void cleanupOrphanedThumbnails(const QStringList& validEncryptedFilePaths);

private:
    QString getCacheKey(const QString& encryptedFilePath);
    QString getCacheFilePath(const QString& cacheKey);
    bool ensureCacheDirectory();

    QString m_username;
    QByteArray m_encryptionKey;
    QString m_cacheDirectory;

    bool validateThumbnailCacheFile(const QString& cacheFilePath);
};

#endif // THUMBNAIL_CACHE_H
