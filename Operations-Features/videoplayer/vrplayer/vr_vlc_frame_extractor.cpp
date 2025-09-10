#include "vr_vlc_frame_extractor.h"
#include <QDebug>
#include <QOpenGLContext>
#include <cstring>
#include <algorithm>

#ifdef USE_LIBVLC
VRVLCFrameExtractor::VRVLCFrameExtractor(libvlc_media_player_t* mediaPlayer, QObject *parent)
#else
VRVLCFrameExtractor::VRVLCFrameExtractor(void* mediaPlayer, QObject *parent)
#endif
    : QObject(parent)
    , m_mediaPlayer(mediaPlayer)
    , m_bufferSize(0)
    , m_videoWidth(0)
    , m_videoHeight(0)
    , m_hasNewFrame(false)
    , m_bufferLocked(false)
    , m_textureId(0)
    , m_textureInitialized(false)
    , m_frameCount(0)
    , m_droppedFrames(0)
    , m_lastFrameTime(std::chrono::steady_clock::now())
    , m_initialized(false)
{
    qDebug() << "VRVLCFrameExtractor: Constructor called";
}

VRVLCFrameExtractor::~VRVLCFrameExtractor()
{
    qDebug() << "VRVLCFrameExtractor: Destructor called";
    cleanup();
}

bool VRVLCFrameExtractor::initialize()
{
    if (m_initialized) {
        qDebug() << "VRVLCFrameExtractor: Already initialized";
        return true;
    }
    
    if (!m_mediaPlayer) {
        qDebug() << "VRVLCFrameExtractor: No media player provided";
        return false;
    }
    
#ifdef USE_LIBVLC
    qDebug() << "VRVLCFrameExtractor: Setting up video callbacks";
    
    // Set callbacks for video rendering
    libvlc_video_set_callbacks(m_mediaPlayer,
                               lockCallback,
                               unlockCallback,
                               displayCallback,
                               this);
    
    // Set format callbacks
    libvlc_video_set_format_callbacks(m_mediaPlayer,
                                      formatCallback,
                                      formatCleanupCallback);
    
    m_initialized = true;
    qDebug() << "VRVLCFrameExtractor: Initialization complete";
    return true;
#else
    qDebug() << "VRVLCFrameExtractor: LibVLC not available";
    return false;
#endif
}

void VRVLCFrameExtractor::cleanup()
{
    if (!m_initialized) {
        return;
    }
    
    qDebug() << "VRVLCFrameExtractor: Cleaning up";
    
#ifdef USE_LIBVLC
    // Clear callbacks
    if (m_mediaPlayer) {
        libvlc_video_set_callbacks(m_mediaPlayer, nullptr, nullptr, nullptr, nullptr);
        libvlc_video_set_format_callbacks(m_mediaPlayer, nullptr, nullptr);
    }
#endif
    
    // Clean up OpenGL texture
    if (m_textureId && QOpenGLContext::currentContext()) {
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        gl->glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    
    m_pixelBuffer.reset();
    m_bufferSize = 0;
    m_initialized = false;
    
    qDebug() << "VRVLCFrameExtractor: Cleanup complete";
}

QImage VRVLCFrameExtractor::getCurrentFrame() const
{
    QMutexLocker locker(&m_frameMutex);
    
    // Create QImage from pixel buffer if we haven't already
    if (m_currentFrame.isNull() && m_pixelBuffer && m_videoWidth > 0 && m_videoHeight > 0) {
        // VLC provides RV32 format which is RGBA
        m_currentFrame = QImage(m_pixelBuffer.get(), 
                               m_videoWidth, 
                               m_videoHeight,
                               m_videoWidth * 4, // bytes per line
                               QImage::Format_RGBA8888).copy();
    }
    
    return QImage();
}

bool VRVLCFrameExtractor::updateTexture()
{
    if (!m_hasNewFrame || !m_pixelBuffer) {
        return false;
    }
    
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    if (!gl) {
        qDebug() << "VRVLCFrameExtractor: No OpenGL context available";
        return false;
    }
    
    // Create texture if not exists
    if (!m_textureId) {
        gl->glGenTextures(1, &m_textureId);
        m_textureInitialized = false;
    }
    
    gl->glBindTexture(GL_TEXTURE_2D, m_textureId);
    
    if (!m_textureInitialized) {
        // Initial texture setup
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                        m_videoWidth, m_videoHeight,
                        0, GL_RGBA, GL_UNSIGNED_BYTE,
                        nullptr);
        
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        m_textureInitialized = true;
    }
    
    // Update texture data
    {
        QMutexLocker locker(&m_frameMutex);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                          m_videoWidth, m_videoHeight,
                          GL_RGBA, GL_UNSIGNED_BYTE,
                          m_pixelBuffer.get());
    }
    
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    
    m_hasNewFrame = false;
    return true;
}

bool VRVLCFrameExtractor::lockFrameBuffer(void** buffer, unsigned int& width, unsigned int& height)
{
    QMutexLocker locker(&m_frameMutex);
    
    if (!m_pixelBuffer || !m_hasNewFrame || m_bufferLocked) {
        return false;
    }
    
    // Security: Validate dimensions and buffer
    if (m_videoWidth == 0 || m_videoHeight == 0 || m_bufferSize == 0) {
        qDebug() << "VRVLCFrameExtractor: Invalid dimensions or buffer size in lockFrameBuffer";
        return false;
    }
    
    // Security: Verify expected buffer size matches actual
    size_t expectedSize = static_cast<size_t>(m_videoWidth) * 4 * m_videoHeight;
    if (expectedSize != m_bufferSize) {
        qDebug() << "VRVLCFrameExtractor: Buffer size mismatch. Expected:" 
                 << expectedSize << "Actual:" << m_bufferSize;
        return false;
    }
    
    *buffer = m_pixelBuffer.get();
    width = m_videoWidth;
    height = m_videoHeight;
    m_bufferLocked = true;
    
    return true;
}

void VRVLCFrameExtractor::unlockFrameBuffer()
{
    QMutexLocker locker(&m_frameMutex);
    m_bufferLocked = false;
    m_hasNewFrame = false;
}

#ifdef USE_LIBVLC
// Static callback implementations
void* VRVLCFrameExtractor::lockCallback(void* opaque, void** planes)
{
    VRVLCFrameExtractor* extractor = static_cast<VRVLCFrameExtractor*>(opaque);
    return extractor->lock(planes);
}

void VRVLCFrameExtractor::unlockCallback(void* opaque, void* picture, void* const* planes)
{
    VRVLCFrameExtractor* extractor = static_cast<VRVLCFrameExtractor*>(opaque);
    extractor->unlock(picture, planes);
}

void VRVLCFrameExtractor::displayCallback(void* opaque, void* picture)
{
    VRVLCFrameExtractor* extractor = static_cast<VRVLCFrameExtractor*>(opaque);
    extractor->display(picture);
}

unsigned VRVLCFrameExtractor::formatCallback(void** opaque, char* chroma,
                                            unsigned* width, unsigned* height,
                                            unsigned* pitches, unsigned* lines)
{
    VRVLCFrameExtractor* extractor = static_cast<VRVLCFrameExtractor*>(*opaque);
    return extractor->format(chroma, width, height, pitches, lines);
}

void VRVLCFrameExtractor::formatCleanupCallback(void* opaque)
{
    VRVLCFrameExtractor* extractor = static_cast<VRVLCFrameExtractor*>(opaque);
    extractor->formatCleanup();
}
#endif

// Instance method implementations
void* VRVLCFrameExtractor::lock(void** planes)
{
    m_frameMutex.lock();  // Lock here and hold through VLC write.
    if (!m_pixelBuffer) {
        qDebug() << "VRVLCFrameExtractor: Buffer not allocated";
        m_frameMutex.unlock();  // Early unlock on error.
        return nullptr;
    }
    
    // Security: Validate buffer size is still valid
    if (m_bufferSize == 0) {
        qDebug() << "VRVLCFrameExtractor: Invalid buffer size";
        m_frameMutex.unlock();
        return nullptr;
    }
    
    *planes = m_pixelBuffer.get();
    return nullptr;  // Return; mutex stays locked.
}

void VRVLCFrameExtractor::unlock(void* picture, void* const* planes)
{
    Q_UNUSED(picture);
    Q_UNUSED(planes);
    m_frameMutex.unlock();  // Unlock after VLC write completes.
}

void VRVLCFrameExtractor::display(void* picture)
{
    Q_UNUSED(picture);
    
    m_frameCount++;
    
    // Calculate frame timing
    auto now = std::chrono::steady_clock::now();
    auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFrameTime).count();
    
    // Check if we should skip this frame (if previous frame hasn't been consumed)
    if (m_hasNewFrame && frameDuration < 100) { // Skip if frame is less than 100ms old
        m_droppedFrames++;
        if (m_frameCount % 30 == 0) {
            qDebug() << "VRVLCFrameExtractor: Dropping frame, previous not consumed. Total dropped:" << m_droppedFrames;
        }
        return;
    }
    
    QMutexLocker locker(&m_frameMutex);
    
    if (!m_pixelBuffer || m_videoWidth == 0 || m_videoHeight == 0) {
        if (m_frameCount % 30 == 0) {
            qDebug() << "VRVLCFrameExtractor: Invalid buffer or dimensions";
        }
        return;
    }
    
    // Only create QImage copy if absolutely necessary (for fallback)
    // Primary path should use direct buffer access
    if (!m_bufferLocked) {
        m_hasNewFrame = true;
        m_lastFrameTime = now;
        
        if (m_frameCount % 30 == 0) {
            qDebug() << "VRVLCFrameExtractor: Frame" << m_frameCount << "ready, size:" 
                     << m_videoWidth << "x" << m_videoHeight 
                     << "Dropped frames:" << m_droppedFrames;
        }
        emit frameReady();
    }
}

unsigned VRVLCFrameExtractor::format(char* chroma, unsigned* width, unsigned* height,
                                    unsigned* pitches, unsigned* lines)
{
    qDebug() << "VRVLCFrameExtractor: Format callback - Input size:" 
             << *width << "x" << *height;
    
    // Security: Validate video dimensions to prevent buffer overflow
    const unsigned MAX_VIDEO_WIDTH = 8192;  // 8K max width
    const unsigned MAX_VIDEO_HEIGHT = 4320; // 8K max height
    const size_t MAX_BUFFER_SIZE = 512 * 1024 * 1024; // 512MB max buffer
    
    if (*width == 0 || *height == 0) {
        qDebug() << "VRVLCFrameExtractor: Invalid zero dimensions";
        return 0;
    }
    
    if (*width > MAX_VIDEO_WIDTH || *height > MAX_VIDEO_HEIGHT) {
        qDebug() << "VRVLCFrameExtractor: Video dimensions exceed maximum allowed:"
                 << *width << "x" << *height << "(max:" << MAX_VIDEO_WIDTH << "x" << MAX_VIDEO_HEIGHT << ")";
        return 0;
    }
    
    // Store video dimensions
    m_videoWidth = *width;
    m_videoHeight = *height;
    
    // We want RGBA format for OpenGL
    memcpy(chroma, "RV32", 4); // RV32 = RGBA 32-bit
    
    // Calculate buffer size with overflow protection
    // Check for multiplication overflow
    size_t pitch = m_videoWidth * 4; // 4 bytes per pixel (RGBA)
    if (pitch / 4 != m_videoWidth) {
        qDebug() << "VRVLCFrameExtractor: Pitch calculation overflow";
        return 0;
    }
    
    size_t bufferSize = pitch * m_videoHeight;
    if (bufferSize / pitch != m_videoHeight) {
        qDebug() << "VRVLCFrameExtractor: Buffer size calculation overflow";
        return 0;
    }
    
    if (bufferSize > MAX_BUFFER_SIZE) {
        qDebug() << "VRVLCFrameExtractor: Buffer size" << bufferSize 
                 << "exceeds maximum" << MAX_BUFFER_SIZE;
        return 0;
    }
    
    *pitches = static_cast<unsigned>(pitch);
    *lines = m_videoHeight;
    m_bufferSize = bufferSize;
    
    // Allocate pixel buffer
    try {
        m_pixelBuffer = std::make_unique<unsigned char[]>(m_bufferSize);
    } catch (const std::bad_alloc&) {
        qDebug() << "VRVLCFrameExtractor: Failed to allocate buffer of size" << m_bufferSize;
        m_bufferSize = 0;
        return 0;
    }
    
    qDebug() << "VRVLCFrameExtractor: Allocated buffer of" << m_bufferSize << "bytes";
    qDebug() << "VRVLCFrameExtractor: Video format set to" << m_videoWidth 
             << "x" << m_videoHeight;
    
    emit formatChanged(m_videoWidth, m_videoHeight);
    
    return 1; // Number of buffers
}

void VRVLCFrameExtractor::formatCleanup()
{
    qDebug() << "VRVLCFrameExtractor: Format cleanup called";
    
    QMutexLocker locker(&m_frameMutex);
    m_pixelBuffer.reset();
    m_bufferSize = 0;
    m_videoWidth = 0;
    m_videoHeight = 0;
}
