#include "fileiconprovider.h"
#include <QApplication>
#include <QStyle>
#include <QFileInfo>
#include <QDebug>

#ifdef Q_OS_WIN
#include <QPixmap>
#include <QBitmap>
#include <QImage>
#include <commctrl.h>
#endif

FileIconProvider::FileIconProvider(QObject *parent)
    : QObject(parent)
{
    qDebug() << "FileIconProvider constructor called";
    qDebug() << "FileIconProvider object address:" << this;

    // Explicitly initialize the caches (shouldn't be necessary, but let's be safe)
    m_iconCache.clear();
    m_defaultIconCache.clear();

    qDebug() << "FileIconProvider constructor completed";
}

FileIconProvider::~FileIconProvider()
{
    clearCache();
}

QPixmap FileIconProvider::getIconForExtension(const QString& extension, int size)
{
    QString cacheKey = getCacheKey(extension, size);

    // Check cache first
    if (m_iconCache.contains(cacheKey)) {
        return m_iconCache[cacheKey];
    }

    QPixmap icon;

#ifdef Q_OS_WIN
    icon = getSystemIcon(extension, size);
#endif

    // Fallback to default Qt icons if Windows API fails
    if (icon.isNull()) {
        icon = getDefaultFileIcon(size);
    }

    // Cache the result
    if (!icon.isNull()) {
        m_iconCache[cacheKey] = icon;
    }

    return icon;
}

QPixmap FileIconProvider::getIconForFile(const QString& filename, int size)
{
    QFileInfo fileInfo(filename);
    QString extension = fileInfo.suffix().toLower();
    return getIconForExtension(extension, size);
}

#ifdef Q_OS_WIN
QPixmap FileIconProvider::getSystemIcon(const QString& extension, int size)
{
    // Create a temporary filename with the extension
    QString tempFileName = QString("temp.%1").arg(extension);

    SHFILEINFOA sfi = {};
    DWORD flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES;

    // Determine icon size flag
    if (size <= 16) {
        flags |= SHGFI_SMALLICON;
    } else if (size <= 32) {
        flags |= SHGFI_LARGEICON;
    } else {
        // For larger sizes, we'll get the large icon and scale it
        flags |= SHGFI_LARGEICON;
    }

    DWORD_PTR result = SHGetFileInfoA(tempFileName.toLocal8Bit().constData(),
                                      FILE_ATTRIBUTE_NORMAL,
                                      &sfi,
                                      sizeof(sfi),
                                      flags);

    if (result && sfi.hIcon) {
        QPixmap pixmap = hIconToQPixmap(sfi.hIcon, size);
        DestroyIcon(sfi.hIcon); // Important: free the icon handle
        return pixmap;
    }

    return QPixmap();
}

QPixmap FileIconProvider::hIconToQPixmap(HICON hIcon, int size)
{
    if (!hIcon) {
        return QPixmap();
    }

    // Get icon info
    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo)) {
        return QPixmap();
    }

    // Create bitmap info for the icon
    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // negative for top-down bitmap
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);

    if (!hBitmap || !bits) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdc);
        DeleteObject(iconInfo.hbmColor);
        DeleteObject(iconInfo.hbmMask);
        return QPixmap();
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // Fill with transparent background
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, (HBRUSH)GetStockObject(NULL_BRUSH));

    // Draw the icon
    DrawIconEx(hdcMem, 0, 0, hIcon, size, size, 0, NULL, DI_NORMAL);

    // Create QImage from the bitmap data
    QImage image((uchar*)bits, size, size, QImage::Format_ARGB32);

    // Convert BGR to RGB and handle alpha
    QImage rgbaImage = image.rgbSwapped();

    // Clean up
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

    return QPixmap::fromImage(rgbaImage);
}
#endif

QString FileIconProvider::getCacheKey(const QString& extension, int size)
{
    return QString("%1_%2").arg(extension.toLower()).arg(size);
}

void FileIconProvider::clearCache()
{
    m_iconCache.clear();
    m_defaultIconCache.clear();
}

QPixmap FileIconProvider::getDefaultFileIcon(int size)
{
    QString cacheKey = QString("default_file_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon).pixmap(size, size);
    m_defaultIconCache[cacheKey] = icon;
    return icon;
}

QPixmap FileIconProvider::getDefaultImageIcon(int size)
{
    qDebug() << "getDefaultImageIcon called with size:" << size;
    qDebug() << "FileIconProvider object address:" << this;

    // Safety check for this pointer
    if (!this) {
        qWarning() << "FileIconProvider: this pointer is null!";
        return QPixmap();
    }

    QString cacheKey = QString("default_image_%1").arg(size);
    qDebug() << "Generated cacheKey:" << cacheKey;

    // Check if cache is valid before accessing
    try {
        qDebug() << "About to check cache contains...";
        if (m_defaultIconCache.contains(cacheKey)) {
            qDebug() << "Cache hit for:" << cacheKey;
            return m_defaultIconCache[cacheKey];
        }
        qDebug() << "Cache miss for:" << cacheKey;
    } catch (...) {
        qWarning() << "Exception while accessing cache!";
        return QPixmap();
    }

    // Try to get a more specific icon for images
    QPixmap icon = getIconForExtension("jpg", size);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon).pixmap(size, size);
    }

    if (!icon.isNull()) {
        try {
            m_defaultIconCache[cacheKey] = icon;
            qDebug() << "Cached icon for:" << cacheKey;
        } catch (...) {
            qWarning() << "Exception while caching icon!";
        }
    }

    return icon;
}

QPixmap FileIconProvider::getDefaultVideoIcon(int size)
{
    QString cacheKey = QString("default_video_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    // Try to get a video icon
    QPixmap icon = getIconForExtension("mp4", size);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(size, size);
    }
    m_defaultIconCache[cacheKey] = icon;
    return icon;
}

QPixmap FileIconProvider::getDefaultAudioIcon(int size)
{
    QString cacheKey = QString("default_audio_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    QPixmap icon = getIconForExtension("mp3", size);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay).pixmap(size, size);
    }
    m_defaultIconCache[cacheKey] = icon;
    return icon;
}

QPixmap FileIconProvider::getDefaultDocumentIcon(int size)
{
    QString cacheKey = QString("default_document_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    QPixmap icon = getIconForExtension("pdf", size);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView).pixmap(size, size);
    }
    m_defaultIconCache[cacheKey] = icon;
    return icon;
}

QPixmap FileIconProvider::getDefaultArchiveIcon(int size)
{
    QString cacheKey = QString("default_archive_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    QPixmap icon = getIconForExtension("zip", size);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon).pixmap(size, size);
    }
    m_defaultIconCache[cacheKey] = icon;
    return icon;
}
