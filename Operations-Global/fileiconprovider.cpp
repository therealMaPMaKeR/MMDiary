#include "fileiconprovider.h"
#include "qdir.h"
#include <QApplication>
#include <QStyle>
#include <QFileInfo>
#include <QDebug>

#ifdef Q_OS_WIN
#include <QPixmap>
#include <QBitmap>
#include <QImage>
#include <commctrl.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlguid.h>
#endif

FileIconProvider::FileIconProvider(QObject *parent)
    : QObject(parent)
{
    qDebug() << "FileIconProvider constructor called";
    qDebug() << "FileIconProvider object address:" << this;

    // Explicitly initialize the caches (shouldn't be necessary, but let's be safe)
    m_iconCache.clear();
    m_defaultIconCache.clear();
    m_videoThumbnailCache.clear();

#ifdef Q_OS_WIN
    qDebug() << "TN-Video: Windows platform detected, initializing COM";
    // Initialize COM for Windows thumbnail extraction
    HRESULT hr = CoInitialize(nullptr);
    qDebug() << "TN-Video: CoInitialize result:" << QString("0x%1").arg(hr, 0, 16);

    if (FAILED(hr)) {
        qWarning() << "TN-Video: Failed to initialize COM for video thumbnails, HRESULT:" << QString("0x%1").arg(hr, 0, 16);
    } else {
        qDebug() << "TN-Video: COM initialized successfully for video thumbnails";
    }
#else
    qDebug() << "TN-Video: Non-Windows platform, skipping COM initialization";
#endif

    qDebug() << "FileIconProvider constructor completed";
}

FileIconProvider::~FileIconProvider()
{
    qDebug() << "TN-Video: FileIconProvider destructor called";

#ifdef Q_OS_WIN
    qDebug() << "TN-Video: Cleaning up COM";
    // Cleanup COM
    CoUninitialize();
    qDebug() << "TN-Video: COM cleanup completed";
#endif

    qDebug() << "TN-Video: Clearing caches";
    clearCache();
    qDebug() << "TN-Video: FileIconProvider destructor completed";
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

QPixmap FileIconProvider::getVideoThumbnail(const QString& videoFilePath, int size)
{
    qDebug() << "TN-Video: getVideoThumbnail called for:" << videoFilePath << "size:" << size;

    // Check cache first
    QString cacheKey = getVideoCacheKey(videoFilePath, size);
    qDebug() << "TN-Video: Cache key:" << cacheKey;

    if (m_videoThumbnailCache.contains(cacheKey)) {
        qDebug() << "TN-Video: Returning cached video thumbnail";
        return m_videoThumbnailCache[cacheKey];
    }

    qDebug() << "TN-Video: No cached thumbnail found, attempting extraction";
    QPixmap thumbnail;

#ifdef Q_OS_WIN
    qDebug() << "TN-Video: Windows platform detected, calling extractWindowsVideoThumbnail";
    // Try to extract Windows video thumbnail
    thumbnail = extractWindowsVideoThumbnail(videoFilePath, size);
    qDebug() << "TN-Video: extractWindowsVideoThumbnail returned, isNull:" << thumbnail.isNull();
#else
    qDebug() << "TN-Video: Non-Windows platform, skipping Windows API extraction";
#endif

    // Fallback to default video icon if extraction fails
    if (thumbnail.isNull()) {
        qDebug() << "TN-Video: Video thumbnail extraction failed, using default video icon";
        thumbnail = getDefaultVideoIcon(size);
    } else {
        qDebug() << "TN-Video: Successfully extracted video thumbnail, caching it";
        // Cache the successful thumbnail
        m_videoThumbnailCache[cacheKey] = thumbnail;
    }

    return thumbnail;
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

QPixmap FileIconProvider::extractWindowsVideoThumbnail(const QString& videoFilePath, int size)
{
    qDebug() << "TN-Video: extractWindowsVideoThumbnail starting for:" << videoFilePath;

    // Verify file exists
    QFileInfo fileInfo(videoFilePath);
    if (!fileInfo.exists()) {
        qWarning() << "TN-Video: Video file does not exist:" << videoFilePath;
        return QPixmap();
    }

    qDebug() << "TN-Video: File exists, size:" << fileInfo.size() << "bytes";
    qDebug() << "TN-Video: File is readable:" << fileInfo.isReadable();

    try {
        qDebug() << "TN-Video: Converting file path to Windows native format";
        // Convert to Windows native path format (backslashes)
        QString nativePath = QDir::toNativeSeparators(videoFilePath);
        qDebug() << "TN-Video: Native path:" << nativePath;

        // Convert QString to wide string for Windows API
        std::wstring wideFilePath = nativePath.toStdWString();
        qDebug() << "TN-Video: Wide string conversion successful, length:" << wideFilePath.length();

        qDebug() << "TN-Video: Calling SHCreateItemFromParsingName";
        // Create shell item from file path
        IShellItem* pShellItem = nullptr;
        HRESULT hr = SHCreateItemFromParsingName(wideFilePath.c_str(), nullptr, IID_PPV_ARGS(&pShellItem));

        qDebug() << "TN-Video: SHCreateItemFromParsingName result:" << QString("0x%1").arg(hr, 0, 16);
        if (FAILED(hr)) {
            qDebug() << "TN-Video: Failed to create shell item, HRESULT:" << QString("0x%1").arg(hr, 0, 16);

            // Try with absolute path
            QString absolutePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
            qDebug() << "TN-Video: Trying with absolute path:" << absolutePath;

            std::wstring wideAbsolutePath = absolutePath.toStdWString();
            hr = SHCreateItemFromParsingName(wideAbsolutePath.c_str(), nullptr, IID_PPV_ARGS(&pShellItem));
            qDebug() << "TN-Video: Retry SHCreateItemFromParsingName result:" << QString("0x%1").arg(hr, 0, 16);

            if (FAILED(hr)) {
                qDebug() << "TN-Video: Both attempts failed to create shell item";
                return QPixmap();
            }
        }
        qDebug() << "TN-Video: Shell item created successfully";

        qDebug() << "TN-Video: Querying for IShellItemImageFactory interface";
        // Get the image factory interface
        IShellItemImageFactory* pImageFactory = nullptr;
        hr = pShellItem->QueryInterface(IID_PPV_ARGS(&pImageFactory));

        qDebug() << "TN-Video: QueryInterface result:" << QString("0x%1").arg(hr, 0, 16);
        if (FAILED(hr)) {
            qDebug() << "TN-Video: Failed to get image factory interface, HRESULT:" << QString("0x%1").arg(hr, 0, 16);
            pShellItem->Release();
            return QPixmap();
        }
        qDebug() << "TN-Video: Image factory interface obtained successfully";

        // Create size structure
        SIZE thumbnailSize = { size, size };
        qDebug() << "TN-Video: Thumbnail size set to:" << size << "x" << size;

        qDebug() << "TN-Video: Calling GetImage with SIIGBF_THUMBNAILONLY flag";
        // Get the thumbnail bitmap
        HBITMAP hBitmap = nullptr;
        hr = pImageFactory->GetImage(thumbnailSize, SIIGBF_THUMBNAILONLY, &hBitmap);

        qDebug() << "TN-Video: GetImage result:" << QString("0x%1").arg(hr, 0, 16);
        qDebug() << "TN-Video: HBITMAP pointer:" << (void*)hBitmap;

        QPixmap result;
        if (SUCCEEDED(hr) && hBitmap) {
            qDebug() << "TN-Video: Successfully obtained video thumbnail bitmap";
            qDebug() << "TN-Video: Converting HBITMAP to QPixmap";
            result = pixmapFromHBitmap(hBitmap, size);
            qDebug() << "TN-Video: QPixmap conversion result - isNull:" << result.isNull();
            if (!result.isNull()) {
                qDebug() << "TN-Video: Final QPixmap size:" << result.size();
            }
            DeleteObject(hBitmap);
            qDebug() << "TN-Video: HBITMAP cleaned up";
        } else {
            qDebug() << "TN-Video: Failed to get thumbnail image, HRESULT:" << QString("0x%1").arg(hr, 0, 16);

            // Try with different flags
            qDebug() << "TN-Video: Retrying with SIIGBF_RESIZETOFIT flag";
            hr = pImageFactory->GetImage(thumbnailSize, SIIGBF_RESIZETOFIT, &hBitmap);
            qDebug() << "TN-Video: Retry GetImage result:" << QString("0x%1").arg(hr, 0, 16);

            if (SUCCEEDED(hr) && hBitmap) {
                qDebug() << "TN-Video: Retry successful, converting bitmap";
                result = pixmapFromHBitmap(hBitmap, size);
                DeleteObject(hBitmap);
            } else {
                qDebug() << "TN-Video: Retry also failed";

                // Try one more time with no flags
                qDebug() << "TN-Video: Final attempt with no flags";
                hr = pImageFactory->GetImage(thumbnailSize, 0, &hBitmap);
                qDebug() << "TN-Video: Final attempt result:" << QString("0x%1").arg(hr, 0, 16);

                if (SUCCEEDED(hr) && hBitmap) {
                    qDebug() << "TN-Video: Final attempt successful";
                    result = pixmapFromHBitmap(hBitmap, size);
                    DeleteObject(hBitmap);
                }
            }
        }

        // Cleanup
        qDebug() << "TN-Video: Cleaning up COM interfaces";
        pImageFactory->Release();
        pShellItem->Release();
        qDebug() << "TN-Video: COM interfaces cleaned up";

        return result;

    } catch (const std::exception& e) {
        qWarning() << "TN-Video: Exception in extractWindowsVideoThumbnail:" << e.what();
        return QPixmap();
    } catch (...) {
        qWarning() << "TN-Video: Unknown exception in extractWindowsVideoThumbnail";
        return QPixmap();
    }
}

QPixmap FileIconProvider::pixmapFromHBitmap(HBITMAP hBitmap, int size)
{
    qDebug() << "TN-Video: pixmapFromHBitmap called with size:" << size;
    qDebug() << "TN-Video: HBITMAP handle:" << (void*)hBitmap;

    if (!hBitmap) {
        qDebug() << "TN-Video: HBITMAP is null, returning empty QPixmap";
        return QPixmap();
    }

    // Get bitmap info
    BITMAP bitmap;
    if (!GetObject(hBitmap, sizeof(BITMAP), &bitmap)) {
        qWarning() << "TN-Video: Failed to get bitmap object info";
        return QPixmap();
    }

    qDebug() << "TN-Video: Bitmap info - width:" << bitmap.bmWidth << "height:" << bitmap.bmHeight;
    qDebug() << "TN-Video: Bitmap info - planes:" << bitmap.bmPlanes << "bitCount:" << bitmap.bmBitsPixel;

    // Create device contexts
    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);

    if (!hdcMem) {
        qDebug() << "TN-Video: Failed to create memory DC";
        ReleaseDC(NULL, hdc);
        return QPixmap();
    }

    qDebug() << "TN-Video: Device contexts created successfully";

    // Select the bitmap
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // Create bitmap info for DIB
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bitmap.bmWidth;
    bmi.bmiHeader.biHeight = -bitmap.bmHeight; // negative for top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    qDebug() << "TN-Video: BITMAPINFO configured";

    // Allocate buffer for pixel data
    int bufferSize = bitmap.bmWidth * bitmap.bmHeight * 4;
    qDebug() << "TN-Video: Buffer size needed:" << bufferSize << "bytes";

    std::vector<BYTE> buffer(bufferSize);

    // Get the pixel data
    qDebug() << "TN-Video: Calling GetDIBits";
    int scanLines = GetDIBits(hdcMem, hBitmap, 0, bitmap.bmHeight,
                              buffer.data(), &bmi, DIB_RGB_COLORS);

    qDebug() << "TN-Video: GetDIBits returned scan lines:" << scanLines;

    QPixmap result;
    if (scanLines > 0) {
        qDebug() << "TN-Video: Creating QImage from buffer";
        // Create QImage from the buffer
        QImage image(buffer.data(), bitmap.bmWidth, bitmap.bmHeight, QImage::Format_ARGB32);

        qDebug() << "TN-Video: QImage created - size:" << image.size() << "isNull:" << image.isNull();

        // Convert to QPixmap and scale to requested size
        result = QPixmap::fromImage(image);
        qDebug() << "TN-Video: QPixmap created - size:" << result.size() << "isNull:" << result.isNull();

        if (result.width() != size || result.height() != size) {
            qDebug() << "TN-Video: Scaling pixmap from" << result.size() << "to" << size << "x" << size;
            result = result.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            qDebug() << "TN-Video: Scaled pixmap size:" << result.size();
        }
    } else {
        qDebug() << "TN-Video: GetDIBits failed - no scan lines returned";
    }

    // Cleanup
    qDebug() << "TN-Video: Cleaning up device contexts";
    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);

    qDebug() << "TN-Video: pixmapFromHBitmap returning - isNull:" << result.isNull();
    return result;
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

QString FileIconProvider::getVideoCacheKey(const QString& filePath, int size)
{
    // Use file path + modification time for cache key to handle file changes
    QFileInfo fileInfo(filePath);
    QString modTime = fileInfo.exists() ? fileInfo.lastModified().toString(Qt::ISODate) : "";
    return QString("video_%1_%2_%3").arg(filePath).arg(size).arg(modTime);
}

void FileIconProvider::clearCache()
{
    m_iconCache.clear();
    m_defaultIconCache.clear();
    m_videoThumbnailCache.clear();
}

QPixmap FileIconProvider::getDefaultFileIcon(int size)
{
    QString cacheKey = QString("default_file_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    // Use Qt's standard file icon directly instead of system icon
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon).pixmap(size, size);

    if (!icon.isNull()) {
        m_defaultIconCache[cacheKey] = icon;
    }

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

    // Use Qt's standard file icon directly instead of system icon
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon).pixmap(size, size);

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

    // Use Qt's standard media play icon directly instead of system icon
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay).pixmap(size, size);

    if (!icon.isNull()) {
        m_defaultIconCache[cacheKey] = icon;
    }

    return icon;
}

QPixmap FileIconProvider::getDefaultAudioIcon(int size)
{
    QString cacheKey = QString("default_audio_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    // Use Qt's standard media play icon directly instead of system icon
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay).pixmap(size, size);

    if (!icon.isNull()) {
        m_defaultIconCache[cacheKey] = icon;
    }

    return icon;
}

QPixmap FileIconProvider::getDefaultDocumentIcon(int size)
{
    QString cacheKey = QString("default_document_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    // Use Qt's standard detailed view icon directly instead of system icon
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView).pixmap(size, size);

    if (!icon.isNull()) {
        m_defaultIconCache[cacheKey] = icon;
    }

    return icon;
}

QPixmap FileIconProvider::getDefaultArchiveIcon(int size)
{
    QString cacheKey = QString("default_archive_%1").arg(size);
    if (m_defaultIconCache.contains(cacheKey)) {
        return m_defaultIconCache[cacheKey];
    }

    // Use Qt's standard directory icon directly instead of system icon
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon).pixmap(size, size);

    if (!icon.isNull()) {
        m_defaultIconCache[cacheKey] = icon;
    }

    return icon;
}
