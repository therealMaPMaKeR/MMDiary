#ifndef VR_VIDEO_RENDERER_H
#define VR_VIDEO_RENDERER_H

#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QMatrix4x4>
#include <memory>

/**
 * @class VRVideoRenderer
 * @brief Handles OpenGL rendering of 360°/180° video content for VR
 * 
 * This class manages:
 * - Sphere/dome mesh generation for video projection
 * - Shader programs for different video formats
 * - Render-to-texture for VR submission
 * - Stereoscopic rendering for left/right eyes
 */
class VRVideoRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum class VideoFormat {
        Mono360,           // 360° monoscopic (equirectangular)
        Stereo360_TB,      // 360° stereoscopic top-bottom
        Stereo360_SBS,     // 360° stereoscopic side-by-side
        Mono180,           // 180° monoscopic (half sphere equirectangular)
        Stereo180_TB,      // 180° stereoscopic top-bottom
        Stereo180_SBS,     // 180° stereoscopic side-by-side
        Flat2D,            // Regular 2D video
        Fisheye180,        // 180° fisheye (circular projection)
        Fisheye180_TB,     // 180° fisheye stereoscopic top-bottom
        Fisheye180_SBS     // 180° fisheye stereoscopic side-by-side
    };

    explicit VRVideoRenderer(QObject *parent = nullptr);
    ~VRVideoRenderer();

    // Initialization
    bool initialize();
    void cleanup();
    bool isInitialized() const { return m_initialized; }
    
    // Configuration
    void setVideoFormat(VideoFormat format) { m_videoFormat = format; }
    VideoFormat getVideoFormat() const { return m_videoFormat; }
    void setRenderTargetSize(uint32_t width, uint32_t height);
    
    // Video texture management
    bool updateVideoTexture(const QImage& frame);
    bool updateVideoTextureDirect(void* buffer, unsigned int width, unsigned int height);
    bool updateVideoTexture(GLuint textureId);
    
    // Rendering
    void renderEye(bool leftEye, const QMatrix4x4& view, const QMatrix4x4& projection, float zoomScale = 1.0f);
    GLuint getEyeTexture(bool leftEye) const;
    
    // Video adjustments
    void setVideoBrightness(float brightness) { m_brightness = brightness; }
    void setVideoContrast(float contrast) { m_contrast = contrast; }
    void setVideoSaturation(float saturation) { m_saturation = saturation; }
    
    // Sphere/dome configuration
    void setSphereTessellation(int segments, int rings);
    void updateDomeAngularCoverage(float horizontalDegrees, float verticalDegrees);
    
signals:
    void error(const QString& errorMessage);

private:
    bool createShaderPrograms();
    bool createSphereMesh();
    bool createDomeMesh();  // Add dome mesh creation for 180-degree video
    bool createDomeMeshWithCoverage(float horizontalDegrees, float verticalDegrees);
    bool createRenderTargets();
    void destroyRenderTargets();
    
    void renderSphere(const QMatrix4x4& mvpMatrix, bool leftEye, float zoomScale);
    void renderDome(const QMatrix4x4& mvpMatrix, bool leftEye, float zoomScale);
    void renderFisheye(const QMatrix4x4& mvpMatrix, bool leftEye, float zoomScale);
    void renderFlat(const QMatrix4x4& mvpMatrix, float zoomScale);
    
    QVector2D getTextureCoordOffset(bool leftEye) const;
    QVector2D getTextureCoordScale() const;

private:
    // Shader programs
    std::unique_ptr<QOpenGLShaderProgram> m_sphereShader;
    std::unique_ptr<QOpenGLShaderProgram> m_flatShader;
    
    // Vertex data
    QOpenGLBuffer m_sphereVertexBuffer;
    QOpenGLBuffer m_sphereIndexBuffer;
    QOpenGLVertexArrayObject m_sphereVAO;
    int m_sphereIndexCount;
    
    // Dome mesh for 180-degree video
    QOpenGLBuffer m_domeVertexBuffer;
    QOpenGLBuffer m_domeIndexBuffer;
    QOpenGLVertexArrayObject m_domeVAO;
    int m_domeIndexCount;
    
    // Render targets
    std::unique_ptr<QOpenGLFramebufferObject> m_leftEyeFBO;
    std::unique_ptr<QOpenGLFramebufferObject> m_rightEyeFBO;
    uint32_t m_renderWidth;
    uint32_t m_renderHeight;
    
    // Video texture
    GLuint m_videoTexture;
    bool m_ownVideoTexture;
    unsigned int m_textureWidth;
    unsigned int m_textureHeight;
    
    // Settings
    VideoFormat m_videoFormat;
    float m_brightness;
    float m_contrast;
    float m_saturation;
    
    // Sphere tessellation
    int m_sphereSegments;
    int m_sphereRings;
    
    // Dome angular coverage for zoom effect
    float m_domeHorizontalCoverage;  // in degrees
    float m_domeVerticalCoverage;    // in degrees
    float m_currentZoomScale;        // Track current zoom to avoid unnecessary mesh regeneration
    
    bool m_initialized;
};

#endif // VR_VIDEO_RENDERER_H
