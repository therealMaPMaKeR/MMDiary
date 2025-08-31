#include "vr_vlc_frame_extractor.h"
#include <QDebug>
#include <QOpenGLContext>
#include <cstring>

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
    , m_textureId(0)
    , m_textureInitialized(false)
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
    return m_currentFrame.copy();
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
    QMutexLocker locker(&m_frameMutex);
    
    if (!m_pixelBuffer) {
        qDebug() << "VRVLCFrameExtractor: Buffer not allocated";
        return nullptr;
    }
    
    *planes = m_pixelBuffer.get();
    return nullptr; // Picture identifier, not needed
}

void VRVLCFrameExtractor::unlock(void* picture, void* const* planes)
{
    Q_UNUSED(picture);
    Q_UNUSED(planes);
    // Nothing to do here, buffer remains locked until display
}

void VRVLCFrameExtractor::display(void* picture)
{
    Q_UNUSED(picture);
    
    QMutexLocker locker(&m_frameMutex);
    
    if (!m_pixelBuffer || m_videoWidth == 0 || m_videoHeight == 0) {
        return;
    }
    
    // Create QImage from pixel buffer
    // VLC provides RV32 format which is RGBA
    m_currentFrame = QImage(m_pixelBuffer.get(), 
                           m_videoWidth, 
                           m_videoHeight,
                           m_videoWidth * 4, // bytes per line
                           QImage::Format_RGBA8888).copy();
    
    m_hasNewFrame = true;
    emit frameReady();
}

unsigned VRVLCFrameExtractor::format(char* chroma, unsigned* width, unsigned* height,
                                    unsigned* pitches, unsigned* lines)
{
    qDebug() << "VRVLCFrameExtractor: Format callback - Input size:" 
             << *width << "x" << *height;
    
    // Store video dimensions
    m_videoWidth = *width;
    m_videoHeight = *height;
    
    // We want RGBA format for OpenGL
    memcpy(chroma, "RV32", 4); // RV32 = RGBA 32-bit
    
    // Calculate buffer size
    *pitches = m_videoWidth * 4; // 4 bytes per pixel (RGBA)
    *lines = m_videoHeight;
    
    m_bufferSize = (*pitches) * (*lines);
    
    // Allocate pixel buffer
    m_pixelBuffer = std::make_unique<unsigned char[]>(m_bufferSize);
    
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
