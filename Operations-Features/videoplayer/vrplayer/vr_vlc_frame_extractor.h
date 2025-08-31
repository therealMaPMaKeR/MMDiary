#ifndef VR_VLC_FRAME_EXTRACTOR_H
#define VR_VLC_FRAME_EXTRACTOR_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QOpenGLFunctions>
#include <memory>

#ifdef USE_LIBVLC
#include <vlc/vlc.h>
#endif

/**
 * @class VRVLCFrameExtractor
 * @brief Extracts video frames from libVLC for VR rendering
 * 
 * This class sets up libVLC callbacks to capture video frames
 * and convert them to OpenGL textures for VR display.
 */
class VRVLCFrameExtractor : public QObject
{
    Q_OBJECT

public:
#ifdef USE_LIBVLC
    explicit VRVLCFrameExtractor(libvlc_media_player_t* mediaPlayer, QObject *parent = nullptr);
#else
    explicit VRVLCFrameExtractor(void* mediaPlayer, QObject *parent = nullptr);
#endif
    ~VRVLCFrameExtractor();

    // Setup and cleanup
    bool initialize();
    void cleanup();
    
    // Frame access
    QImage getCurrentFrame() const;
    bool hasNewFrame() const { return m_hasNewFrame; }
    void markFrameUsed() { m_hasNewFrame = false; }
    
    // OpenGL texture access
    GLuint getTextureId() const { return m_textureId; }
    bool updateTexture();
    
    // Video info
    unsigned int getVideoWidth() const { return m_videoWidth; }
    unsigned int getVideoHeight() const { return m_videoHeight; }

signals:
    void frameReady();
    void formatChanged(unsigned int width, unsigned int height);

private:
#ifdef USE_LIBVLC
    // libVLC callbacks (must be static)
    static void* lockCallback(void* opaque, void** planes);
    static void unlockCallback(void* opaque, void* picture, void* const* planes);
    static void displayCallback(void* opaque, void* picture);
    static unsigned formatCallback(void** opaque, char* chroma,
                                  unsigned* width, unsigned* height,
                                  unsigned* pitches, unsigned* lines);
    static void formatCleanupCallback(void* opaque);
#endif

    // Instance methods called by static callbacks
    void* lock(void** planes);
    void unlock(void* picture, void* const* planes);
    void display(void* picture);
    unsigned format(char* chroma, unsigned* width, unsigned* height,
                   unsigned* pitches, unsigned* lines);
    void formatCleanup();

private:
#ifdef USE_LIBVLC
    libvlc_media_player_t* m_mediaPlayer;
#else
    void* m_mediaPlayer;
#endif
    
    // Video buffer
    std::unique_ptr<unsigned char[]> m_pixelBuffer;
    size_t m_bufferSize;
    
    // Video properties
    unsigned int m_videoWidth;
    unsigned int m_videoHeight;
    
    // Frame state
    mutable QMutex m_frameMutex;
    QImage m_currentFrame;
    bool m_hasNewFrame;
    
    // OpenGL texture
    GLuint m_textureId;
    bool m_textureInitialized;
    
    bool m_initialized;
};

#endif // VR_VLC_FRAME_EXTRACTOR_H
