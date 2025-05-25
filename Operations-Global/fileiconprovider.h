#ifndef FILE_ICON_PROVIDER_H
#define FILE_ICON_PROVIDER_H

#include <QObject>
#include <QPixmap>
#include <QMap>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

class FileIconProvider : public QObject
{
    Q_OBJECT

public:
    explicit FileIconProvider(QObject *parent = nullptr);
    ~FileIconProvider();

    // Get icon for file extension
    QPixmap getIconForExtension(const QString& extension, int size = 64);

    // Get icon for specific file (uses extension)
    QPixmap getIconForFile(const QString& filename, int size = 64);

    // Clear cache
    void clearCache();

    // Get default icons for various types
    QPixmap getDefaultFileIcon(int size = 64);
    QPixmap getDefaultImageIcon(int size = 64);
    QPixmap getDefaultVideoIcon(int size = 64);
    QPixmap getDefaultAudioIcon(int size = 64);
    QPixmap getDefaultDocumentIcon(int size = 64);
    QPixmap getDefaultArchiveIcon(int size = 64);

private:
#ifdef Q_OS_WIN
    QPixmap hIconToQPixmap(HICON hIcon, int size);
    QPixmap getSystemIcon(const QString& extension, int size);
#endif

    QString getCacheKey(const QString& extension, int size);

    // Cache for icons
    QMap<QString, QPixmap> m_iconCache;

    // Default icons cache
    mutable QMap<QString, QPixmap> m_defaultIconCache;
};

#endif // FILE_ICON_PROVIDER_H
