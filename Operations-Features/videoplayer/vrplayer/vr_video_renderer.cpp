#include "vr_video_renderer.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QtMath>

VRVideoRenderer::VRVideoRenderer(QObject *parent)
    : QObject(parent)
    , m_sphereVertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_sphereIndexBuffer(QOpenGLBuffer::IndexBuffer)
    , m_sphereIndexCount(0)
    , m_renderWidth(2048)
    , m_renderHeight(2048)
    , m_videoTexture(0)
    , m_ownVideoTexture(false)
    , m_videoFormat(VideoFormat::Mono360)
    , m_brightness(1.0f)
    , m_contrast(1.0f)
    , m_saturation(1.0f)
    , m_sphereSegments(64)
    , m_sphereRings(32)
    , m_initialized(false)
{
    qDebug() << "VRVideoRenderer: Constructor called";
}

VRVideoRenderer::~VRVideoRenderer()
{
    qDebug() << "VRVideoRenderer: Destructor called";
    cleanup();
}

bool VRVideoRenderer::initialize()
{
    qDebug() << "VRVideoRenderer: Initializing OpenGL renderer";
    
    if (m_initialized) {
        qDebug() << "VRVideoRenderer: Already initialized";
        return true;
    }
    
    // Initialize OpenGL functions
    initializeOpenGLFunctions();
    
    // Create shader programs
    if (!createShaderPrograms()) {
        qDebug() << "VRVideoRenderer: Failed to create shader programs";
        emit error("Failed to create VR shader programs");
        return false;
    }
    
    // Create sphere mesh for 360 video
    if (!createSphereMesh()) {
        qDebug() << "VRVideoRenderer: Failed to create sphere mesh";
        emit error("Failed to create VR sphere mesh");
        return false;
    }
    
    // Create render targets
    if (!createRenderTargets()) {
        qDebug() << "VRVideoRenderer: Failed to create render targets";
        emit error("Failed to create VR render targets");
        return false;
    }
    
    m_initialized = true;
    qDebug() << "VRVideoRenderer: Initialization complete";
    return true;
}

void VRVideoRenderer::cleanup()
{
    if (!m_initialized) {
        return;
    }
    
    qDebug() << "VRVideoRenderer: Cleaning up resources";
    
    // Destroy render targets
    destroyRenderTargets();
    
    // Clean up sphere mesh
    if (m_sphereVAO.isCreated()) {
        m_sphereVAO.destroy();
    }
    if (m_sphereVertexBuffer.isCreated()) {
        m_sphereVertexBuffer.destroy();
    }
    if (m_sphereIndexBuffer.isCreated()) {
        m_sphereIndexBuffer.destroy();
    }
    
    // Clean up video texture if we own it
    if (m_ownVideoTexture && m_videoTexture) {
        glDeleteTextures(1, &m_videoTexture);
        m_videoTexture = 0;
    }
    
    // Clean up shaders
    m_sphereShader.reset();
    m_flatShader.reset();
    
    m_initialized = false;
    qDebug() << "VRVideoRenderer: Cleanup complete";
}

bool VRVideoRenderer::createShaderPrograms()
{
    qDebug() << "VRVideoRenderer: Creating shader programs";
    
    // Vertex shader for sphere rendering
    const char* sphereVertexShader = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 texCoord;
        
        uniform mat4 mvpMatrix;
        uniform vec2 texOffset;
        uniform vec2 texScale;
        
        out vec2 fragTexCoord;
        
        void main()
        {
            gl_Position = mvpMatrix * vec4(position, 1.0);
            fragTexCoord = texCoord * texScale + texOffset;
        }
    )";
    
    // Fragment shader for video rendering
    const char* videoFragmentShader = R"(
        #version 330 core
        in vec2 fragTexCoord;
        
        uniform sampler2D videoTexture;
        uniform float brightness;
        uniform float contrast;
        uniform float saturation;
        
        out vec4 fragColor;
        
        vec3 adjustColor(vec3 color)
        {
            // Brightness adjustment
            color = color * brightness;
            
            // Contrast adjustment
            color = (color - 0.5) * contrast + 0.5;
            
            // Saturation adjustment
            float gray = dot(color, vec3(0.299, 0.587, 0.114));
            color = mix(vec3(gray), color, saturation);
            
            return clamp(color, 0.0, 1.0);
        }
        
        void main()
        {
            vec4 texColor = texture(videoTexture, fragTexCoord);
            texColor.rgb = adjustColor(texColor.rgb);
            fragColor = texColor;
        }
    )";
    
    // Create sphere shader program
    m_sphereShader = std::make_unique<QOpenGLShaderProgram>();
    if (!m_sphereShader->addShaderFromSourceCode(QOpenGLShader::Vertex, sphereVertexShader)) {
        qDebug() << "VRVideoRenderer: Failed to compile sphere vertex shader";
        return false;
    }
    if (!m_sphereShader->addShaderFromSourceCode(QOpenGLShader::Fragment, videoFragmentShader)) {
        qDebug() << "VRVideoRenderer: Failed to compile video fragment shader";
        return false;
    }
    if (!m_sphereShader->link()) {
        qDebug() << "VRVideoRenderer: Failed to link sphere shader program";
        return false;
    }
    
    // Vertex shader for flat/2D rendering
    const char* flatVertexShader = R"(
        #version 330 core
        layout(location = 0) in vec2 position;
        layout(location = 1) in vec2 texCoord;
        
        out vec2 fragTexCoord;
        
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            fragTexCoord = texCoord;
        }
    )";
    
    // Create flat shader program
    m_flatShader = std::make_unique<QOpenGLShaderProgram>();
    if (!m_flatShader->addShaderFromSourceCode(QOpenGLShader::Vertex, flatVertexShader)) {
        qDebug() << "VRVideoRenderer: Failed to compile flat vertex shader";
        return false;
    }
    if (!m_flatShader->addShaderFromSourceCode(QOpenGLShader::Fragment, videoFragmentShader)) {
        qDebug() << "VRVideoRenderer: Failed to compile flat fragment shader";
        return false;
    }
    if (!m_flatShader->link()) {
        qDebug() << "VRVideoRenderer: Failed to link flat shader program";
        return false;
    }
    
    qDebug() << "VRVideoRenderer: Shader programs created successfully";
    return true;
}

bool VRVideoRenderer::createSphereMesh()
{
    qDebug() << "VRVideoRenderer: Creating sphere mesh with" << m_sphereSegments 
             << "segments and" << m_sphereRings << "rings";
    
    struct Vertex {
        float x, y, z;
        float u, v;
    };
    
    QVector<Vertex> vertices;
    QVector<GLuint> indices;
    
    // Generate sphere vertices
    for (int ring = 0; ring <= m_sphereRings; ++ring) {
        float theta = ring * M_PI / m_sphereRings;
        float sinTheta = qSin(theta);
        float cosTheta = qCos(theta);
        
        for (int segment = 0; segment <= m_sphereSegments; ++segment) {
            float phi = segment * 2 * M_PI / m_sphereSegments;
            float sinPhi = qSin(phi);
            float cosPhi = qCos(phi);
            
            Vertex vertex;
            vertex.x = cosPhi * sinTheta;
            vertex.y = cosTheta;
            vertex.z = sinPhi * sinTheta;
            
            // Texture coordinates for equirectangular projection
            vertex.u = 1.0f - (float)segment / m_sphereSegments;
            vertex.v = 1.0f - (float)ring / m_sphereRings;
            
            vertices.append(vertex);
        }
    }
    
    // Generate sphere indices
    for (int ring = 0; ring < m_sphereRings; ++ring) {
        for (int segment = 0; segment < m_sphereSegments; ++segment) {
            GLuint first = ring * (m_sphereSegments + 1) + segment;
            GLuint second = first + m_sphereSegments + 1;
            
            // First triangle
            indices.append(first);
            indices.append(second);
            indices.append(first + 1);
            
            // Second triangle
            indices.append(second);
            indices.append(second + 1);
            indices.append(first + 1);
        }
    }
    
    m_sphereIndexCount = indices.size();
    
    // Create VAO
    if (!m_sphereVAO.isCreated()) {
        m_sphereVAO.create();
    }
    m_sphereVAO.bind();
    
    // Create and upload vertex buffer
    if (!m_sphereVertexBuffer.isCreated()) {
        m_sphereVertexBuffer.create();
    }
    m_sphereVertexBuffer.bind();
    m_sphereVertexBuffer.allocate(vertices.constData(), vertices.size() * sizeof(Vertex));
    
    // Set vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         reinterpret_cast<void*>(3 * sizeof(float)));
    
    // Create and upload index buffer
    if (!m_sphereIndexBuffer.isCreated()) {
        m_sphereIndexBuffer.create();
    }
    m_sphereIndexBuffer.bind();
    m_sphereIndexBuffer.allocate(indices.constData(), indices.size() * sizeof(GLuint));
    
    m_sphereVAO.release();
    
    qDebug() << "VRVideoRenderer: Sphere mesh created with" << vertices.size() 
             << "vertices and" << indices.size() / 3 << "triangles";
    return true;
}

bool VRVideoRenderer::createRenderTargets()
{
    qDebug() << "VRVideoRenderer: Creating render targets" << m_renderWidth << "x" << m_renderHeight;
    
    // Create left eye framebuffer
    m_leftEyeFBO = std::make_unique<QOpenGLFramebufferObject>(
        QSize(m_renderWidth, m_renderHeight),
        QOpenGLFramebufferObject::CombinedDepthStencil);
    
    if (!m_leftEyeFBO->isValid()) {
        qDebug() << "VRVideoRenderer: Failed to create left eye framebuffer";
        return false;
    }
    
    // Create right eye framebuffer
    m_rightEyeFBO = std::make_unique<QOpenGLFramebufferObject>(
        QSize(m_renderWidth, m_renderHeight),
        QOpenGLFramebufferObject::CombinedDepthStencil);
    
    if (!m_rightEyeFBO->isValid()) {
        qDebug() << "VRVideoRenderer: Failed to create right eye framebuffer";
        return false;
    }
    
    qDebug() << "VRVideoRenderer: Render targets created successfully";
    return true;
}

void VRVideoRenderer::destroyRenderTargets()
{
    m_leftEyeFBO.reset();
    m_rightEyeFBO.reset();
}

void VRVideoRenderer::setRenderTargetSize(uint32_t width, uint32_t height)
{
    if (m_renderWidth == width && m_renderHeight == height) {
        return;
    }
    
    qDebug() << "VRVideoRenderer: Setting render target size to" << width << "x" << height;
    
    m_renderWidth = width;
    m_renderHeight = height;
    
    if (m_initialized) {
        destroyRenderTargets();
        createRenderTargets();
    }
}

bool VRVideoRenderer::updateVideoTexture(const QImage& frame)
{
    if (!m_initialized) {
        return false;
    }
    
    // Create texture if it doesn't exist
    if (!m_videoTexture) {
        glGenTextures(1, &m_videoTexture);
        m_ownVideoTexture = true;
    }
    
    glBindTexture(GL_TEXTURE_2D, m_videoTexture);
    
    // Convert image to OpenGL format
    QImage glFrame = frame.convertToFormat(QImage::Format_RGBA8888).mirrored();
    
    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
                 glFrame.width(), glFrame.height(), 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, 
                 glFrame.constBits());
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return true;
}

bool VRVideoRenderer::updateVideoTexture(GLuint textureId)
{
    if (!m_initialized) {
        return false;
    }
    
    // Clean up old texture if we own it
    if (m_ownVideoTexture && m_videoTexture && m_videoTexture != textureId) {
        glDeleteTextures(1, &m_videoTexture);
    }
    
    m_videoTexture = textureId;
    m_ownVideoTexture = false;
    
    return true;
}

void VRVideoRenderer::renderEye(bool leftEye, const QMatrix4x4& view, const QMatrix4x4& projection)
{
    if (!m_initialized || !m_videoTexture) {
        return;
    }
    
    // Bind appropriate framebuffer
    QOpenGLFramebufferObject* fbo = leftEye ? m_leftEyeFBO.get() : m_rightEyeFBO.get();
    fbo->bind();
    
    // Clear
    glViewport(0, 0, m_renderWidth, m_renderHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Calculate MVP matrix
    QMatrix4x4 mvpMatrix = projection * view;
    
    // Render based on video format
    switch (m_videoFormat) {
        case VideoFormat::Mono360:
        case VideoFormat::Stereo360_TB:
        case VideoFormat::Stereo360_SBS:
            renderSphere(mvpMatrix, leftEye);
            break;
            
        case VideoFormat::Mono180:
        case VideoFormat::Stereo180_TB:
        case VideoFormat::Stereo180_SBS:
            renderDome(mvpMatrix, leftEye);
            break;
            
        case VideoFormat::Flat2D:
            renderFlat(mvpMatrix);
            break;
    }
    
    fbo->release();
}

void VRVideoRenderer::renderSphere(const QMatrix4x4& mvpMatrix, bool leftEye)
{
    m_sphereShader->bind();
    
    // Set uniforms
    m_sphereShader->setUniformValue("mvpMatrix", mvpMatrix);
    m_sphereShader->setUniformValue("videoTexture", 0);
    m_sphereShader->setUniformValue("brightness", m_brightness);
    m_sphereShader->setUniformValue("contrast", m_contrast);
    m_sphereShader->setUniformValue("saturation", m_saturation);
    
    // Set texture coordinate offset and scale based on format
    QVector2D texOffset = getTextureCoordOffset(leftEye);
    QVector2D texScale = getTextureCoordScale();
    m_sphereShader->setUniformValue("texOffset", texOffset);
    m_sphereShader->setUniformValue("texScale", texScale);
    
    // Bind video texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_videoTexture);
    
    // Render sphere
    m_sphereVAO.bind();
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, nullptr);
    m_sphereVAO.release();
    
    m_sphereShader->release();
}

void VRVideoRenderer::renderDome(const QMatrix4x4& mvpMatrix, bool leftEye)
{
    // Similar to renderSphere but only render front hemisphere
    // This is a simplified version - you might want to create a separate dome mesh
    renderSphere(mvpMatrix, leftEye);
}

void VRVideoRenderer::renderFlat(const QMatrix4x4& mvpMatrix)
{
    Q_UNUSED(mvpMatrix);
    
    // For flat 2D video, render a simple quad
    m_flatShader->bind();
    
    m_flatShader->setUniformValue("videoTexture", 0);
    m_flatShader->setUniformValue("brightness", m_brightness);
    m_flatShader->setUniformValue("contrast", m_contrast);
    m_flatShader->setUniformValue("saturation", m_saturation);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_videoTexture);
    
    // Create a simple quad using triangle strip
    // We'll use the sphere VAO but only draw 4 vertices as a quad
    // In a proper implementation, you'd create a separate quad VAO
    static const GLfloat quadVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,  // Bottom-left
         1.0f, -1.0f, 1.0f, 0.0f,  // Bottom-right
        -1.0f,  1.0f, 0.0f, 1.0f,  // Top-left
         1.0f,  1.0f, 1.0f, 1.0f   // Top-right
    };
    
    // For now, just skip flat rendering - it's rarely used in VR
    // TODO: Create proper quad VAO for flat video rendering
    
    m_flatShader->release();
}

QVector2D VRVideoRenderer::getTextureCoordOffset(bool leftEye) const
{
    switch (m_videoFormat) {
        case VideoFormat::Stereo360_TB:
        case VideoFormat::Stereo180_TB:
            // Top-bottom: left eye uses top half, right eye uses bottom half
            return leftEye ? QVector2D(0.0f, 0.0f) : QVector2D(0.0f, 0.5f);
            
        case VideoFormat::Stereo360_SBS:
        case VideoFormat::Stereo180_SBS:
            // Side-by-side: left eye uses left half, right eye uses right half
            return leftEye ? QVector2D(0.0f, 0.0f) : QVector2D(0.5f, 0.0f);
            
        default:
            return QVector2D(0.0f, 0.0f);
    }
}

QVector2D VRVideoRenderer::getTextureCoordScale() const
{
    switch (m_videoFormat) {
        case VideoFormat::Stereo360_TB:
        case VideoFormat::Stereo180_TB:
            // Top-bottom: use half height
            return QVector2D(1.0f, 0.5f);
            
        case VideoFormat::Stereo360_SBS:
        case VideoFormat::Stereo180_SBS:
            // Side-by-side: use half width
            return QVector2D(0.5f, 1.0f);
            
        default:
            return QVector2D(1.0f, 1.0f);
    }
}

GLuint VRVideoRenderer::getEyeTexture(bool leftEye) const
{
    if (!m_initialized) {
        return 0;
    }
    
    QOpenGLFramebufferObject* fbo = leftEye ? m_leftEyeFBO.get() : m_rightEyeFBO.get();
    return fbo ? fbo->texture() : 0;
}

void VRVideoRenderer::setSphereTessellation(int segments, int rings)
{
    if (segments == m_sphereSegments && rings == m_sphereRings) {
        return;
    }
    
    qDebug() << "VRVideoRenderer: Updating sphere tessellation to" 
             << segments << "segments and" << rings << "rings";
    
    m_sphereSegments = segments;
    m_sphereRings = rings;
    
    if (m_initialized) {
        createSphereMesh();
    }
}
