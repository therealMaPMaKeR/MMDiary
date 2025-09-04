#include "vr_video_renderer.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QtMath>

VRVideoRenderer::VRVideoRenderer(QObject *parent)
    : QObject(parent)
    , m_sphereVertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_sphereIndexBuffer(QOpenGLBuffer::IndexBuffer)
    , m_sphereIndexCount(0)
    , m_domeVertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_domeIndexBuffer(QOpenGLBuffer::IndexBuffer)
    , m_domeIndexCount(0)
    , m_renderWidth(2048)
    , m_renderHeight(2048)
    , m_videoTexture(0)
    , m_ownVideoTexture(false)
    , m_textureWidth(0)
    , m_textureHeight(0)
    , m_videoFormat(VideoFormat::Mono180)
    , m_brightness(1.0f)
    , m_contrast(1.0f)
    , m_saturation(1.0f)
    , m_sphereSegments(64)
    , m_sphereRings(32)
    , m_domeHorizontalCoverage(180.0f)  // Default to 180 degrees
    , m_domeVerticalCoverage(180.0f)    // Default to 180 degrees
    , m_currentZoomScale(1.0f)          // Default zoom at 1.0
    , m_initialized(false)
{
    qDebug() << "VRVideoRenderer: Constructor called";
}

VRVideoRenderer::~VRVideoRenderer()
{
    qDebug() << "VRVideoRenderer: Destructor called";
    // Don't call cleanup here - it should be called with proper context
    // The cleanup should be done by the owner with the correct OpenGL context
    if (m_initialized) {
        qDebug() << "VRVideoRenderer: WARNING - Destructor called while still initialized!";
        qDebug() << "VRVideoRenderer: cleanup() should have been called with proper OpenGL context";
    }
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
    
    // Create dome mesh for 180 video
    if (!createDomeMesh()) {
        qDebug() << "VRVideoRenderer: Failed to create dome mesh";
        emit error("Failed to create VR dome mesh");
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
    
    // Check if we have a valid OpenGL context
    QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if (!currentContext) {
        qDebug() << "VRVideoRenderer: WARNING - No OpenGL context current during cleanup!";
        // Mark as not initialized but don't try to destroy OpenGL resources
        m_initialized = false;
        return;
    }
    
    // Destroy render targets
    destroyRenderTargets();
    
    // Clean up sphere mesh - with safety checks
    try {
        if (m_sphereVAO.isCreated()) {
            qDebug() << "VRVideoRenderer: Destroying sphere VAO";
            m_sphereVAO.destroy();
        }
        if (m_sphereVertexBuffer.isCreated()) {
            qDebug() << "VRVideoRenderer: Destroying sphere vertex buffer";
            m_sphereVertexBuffer.destroy();
        }
        if (m_sphereIndexBuffer.isCreated()) {
            qDebug() << "VRVideoRenderer: Destroying sphere index buffer";
            m_sphereIndexBuffer.destroy();
        }
        
        // Clean up dome mesh
        if (m_domeVAO.isCreated()) {
            qDebug() << "VRVideoRenderer: Destroying dome VAO";
            m_domeVAO.destroy();
        }
        if (m_domeVertexBuffer.isCreated()) {
            qDebug() << "VRVideoRenderer: Destroying dome vertex buffer";
            m_domeVertexBuffer.destroy();
        }
        if (m_domeIndexBuffer.isCreated()) {
            qDebug() << "VRVideoRenderer: Destroying dome index buffer";
            m_domeIndexBuffer.destroy();
        }
    } catch (...) {
        qDebug() << "VRVideoRenderer: Exception caught during OpenGL resource cleanup";
    }
    
    // Clean up video texture if we own it
    if (m_ownVideoTexture && m_videoTexture) {
        glDeleteTextures(1, &m_videoTexture);
        m_videoTexture = 0;
        m_textureWidth = 0;
        m_textureHeight = 0;
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
        layout(location = 2) in float isPole;
        
        uniform mat4 mvpMatrix;
        uniform vec2 texOffset;
        uniform vec2 texScale;
        uniform float zoomScale;  // Zoom scale for texture coordinates
        
        out vec2 fragTexCoord;
        out vec3 worldPos;
        out float fragIsPole;
        
        void main()
        {
            gl_Position = mvpMatrix * vec4(position, 1.0);
            
            // Apply zoom to texture coordinates when zoom > 1.0
            // Special handling for pole vertices to avoid singularity
            vec2 zoomedTexCoord = texCoord;
            if (zoomScale > 1.0) {
                if (isPole > 0.5) {
                    // For pole vertices, keep U at 0.5 and only zoom V
                    float zoomedV = (texCoord.y - 0.5) / zoomScale + 0.5;
                    zoomedTexCoord = vec2(0.5, zoomedV);
                } else {
                    // Normal zoom for non-pole vertices
                    // Use a smoother zoom that reduces distortion near poles
                    float distFromPole = min(texCoord.y, 1.0 - texCoord.y);
                    float zoomFactor = mix(1.0, zoomScale, smoothstep(0.0, 0.2, distFromPole));
                    zoomedTexCoord = (texCoord - 0.5) / zoomFactor + 0.5;
                }
            }
            
            fragTexCoord = zoomedTexCoord * texScale + texOffset;
            worldPos = position;  // Keep original position for texture calculations
            fragIsPole = isPole;
        }
    )";
    
    // Fragment shader for video rendering
    const char* videoFragmentShader = R"(
        #version 330 core
        in vec2 fragTexCoord;
        in vec3 worldPos;
        in float fragIsPole;
        
        uniform sampler2D videoTexture;
        uniform float brightness;
        uniform float contrast;
        uniform float saturation;
        uniform float fisheyeMode;  // 0.0 = normal, 1.0 = fisheye
        uniform vec2 texOffset;
        uniform vec2 texScale;
        uniform float swapChannels;  // 1.0 = swap R and B channels (for BGRA->RGBA fix)
        
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
        
        vec2 getFisheyeTexCoord(vec3 pos)
        {
            // For fisheye projection, map the hemisphere position to circular texture coordinates
            // Normalize the position to get direction
            vec3 dir = normalize(pos);
            
            // Calculate spherical coordinates
            float theta = atan(dir.x, -dir.z); // Horizontal angle from forward
            float phi = asin(dir.y); // Vertical angle from horizon
            
            // For 180° fisheye, we map angles to a circular area
            // Map theta from [-PI/2, PI/2] to [-1, 1]
            float x = theta / (3.14159265359 * 0.5);
            
            // Map phi from [-PI/2, PI/2] to [-1, 1]
            float y = phi / (3.14159265359 * 0.5);
            
            // Calculate radius for circular fisheye mapping
            float r = sqrt(x * x + y * y);
            
            // Apply equisolid angle mapping (common for fisheye lenses)
            // This provides better distribution than linear mapping
            if (r > 0.001) {
                float angleFromCenter = r * (3.14159265359 * 0.5);
                float newR = 2.0 * sin(angleFromCenter * 0.5);
                float scale = newR / r;
                x *= scale;
                y *= scale;
                r = newR;
            }
            
            // Limit to circular area
            if (r > 1.0) {
                return vec2(0.5, 0.5); // Center for out-of-bounds
            }
            
            // Convert to texture coordinates [0, 1]
            vec2 fisheyeCoord;
            fisheyeCoord.x = 0.5 + x * 0.5;
            fisheyeCoord.y = 0.5 - y * 0.5; // Flip Y for texture coordinates
            
            return fisheyeCoord;
        }
        
        void main()
        {
            vec2 texCoord = fragTexCoord;
            
            // If in fisheye mode, calculate fisheye texture coordinates
            if (fisheyeMode > 0.5) {
                texCoord = getFisheyeTexCoord(worldPos);
                // Apply stereoscopic offset and scale
                texCoord = texCoord * texScale + texOffset;
            }
            
            vec4 texColor = texture(videoTexture, texCoord);
            // Conditionally swap red and blue channels to fix color tint (BGRA -> RGBA)
            if (swapChannels > 0.5) {
                texColor.rgb = texColor.bgr;
            }
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
    
    // Security: Validate mesh parameters are within safe bounds
    if (m_sphereSegments > 256 || m_sphereRings > 128) {
        qDebug() << "VRVideoRenderer: Mesh tessellation exceeds safe limits";
        return false;
    }
    
    // Security: Pre-calculate and validate vertex/index counts
    int expectedVertices = (m_sphereSegments + 1) * (m_sphereRings + 1);
    int expectedIndices = m_sphereSegments * m_sphereRings * 6; // 2 triangles per quad, 3 indices per triangle
    
    if (expectedVertices > 100000 || expectedIndices > 600000) {
        qDebug() << "VRVideoRenderer: Mesh would be too large - vertices:" << expectedVertices
                 << "indices:" << expectedIndices;
        return false;
    }
    
    struct Vertex {
        float x, y, z;
        float u, v;
        float isPole;  // For compatibility with dome mesh
    };
    
    QVector<Vertex> vertices;
    QVector<GLuint> indices;
    
    // Reserve memory to avoid reallocations
    vertices.reserve(expectedVertices);
    indices.reserve(expectedIndices);
    
    // Generate sphere vertices
    for (int ring = 0; ring <= m_sphereRings; ++ring) {
        float theta = ring * M_PI / m_sphereRings;
        float sinTheta = qSin(theta);
        float cosTheta = qCos(theta);
        
        // Check if this is a pole (top or bottom of sphere)
        bool isTopPole = (ring == 0);
        bool isBottomPole = (ring == m_sphereRings);
        bool isPole = isTopPole || isBottomPole;
        
        for (int segment = 0; segment <= m_sphereSegments; ++segment) {
            float phi = segment * 2 * M_PI / m_sphereSegments;
            float sinPhi = qSin(phi);
            float cosPhi = qCos(phi);
            
            Vertex vertex;
            // IMPORTANT: Negate X and Z to invert the sphere for viewing from inside
            // Scale the sphere for consistent viewing
            float scale = 1.5f;
            vertex.x = -cosPhi * sinTheta * scale;  // Negated and scaled
            vertex.y = cosTheta * scale;
            vertex.z = -sinPhi * sinTheta * scale;  // Negated and scaled
            
            // Texture coordinates for equirectangular projection
            if (isPole) {
                // At poles, use center U coordinate to avoid singularity during zoom
                vertex.u = 0.5f;
                vertex.v = isTopPole ? 0.0f : 1.0f;
            } else {
                // U coordinate: horizontal wrapping around sphere
                vertex.u = (float)segment / m_sphereSegments;
                // V coordinate: vertical from top to bottom
                vertex.v = (float)ring / m_sphereRings;
            }
            
            // Mark pole vertices
            vertex.isPole = isPole ? 1.0f : 0.0f;
            
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
    glEnableVertexAttribArray(0);  // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    
    glEnableVertexAttribArray(1);  // Texture coordinates
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         reinterpret_cast<void*>(3 * sizeof(float)));
    
    glEnableVertexAttribArray(2);  // isPole flag
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         reinterpret_cast<void*>(5 * sizeof(float)));
    
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

bool VRVideoRenderer::createDomeMesh()
{
    // Create dome with default 180-degree coverage
    return createDomeMeshWithCoverage(180.0f, 180.0f);
}

bool VRVideoRenderer::createDomeMeshWithCoverage(float horizontalDegrees, float verticalDegrees)
{
    // Security: Validate coverage parameters
    horizontalDegrees = qBound(10.0f, horizontalDegrees, 360.0f);
    verticalDegrees = qBound(10.0f, verticalDegrees, 180.0f);
    
    qDebug() << "VRVideoRenderer: Creating dome mesh with coverage:" 
             << horizontalDegrees << "x" << verticalDegrees << "degrees"
             << "with" << m_sphereSegments << "segments and" << m_sphereRings << "rings";
    
    // Security: Validate mesh parameters are within safe bounds
    if (m_sphereSegments > 256 || m_sphereRings > 128) {
        qDebug() << "VRVideoRenderer: Mesh tessellation exceeds safe limits";
        return false;
    }
    
    // Store the coverage values
    m_domeHorizontalCoverage = horizontalDegrees;
    m_domeVerticalCoverage = verticalDegrees;
    
    // Security: Pre-calculate and validate vertex/index counts
    int expectedVertices = (m_sphereSegments + 1) * (m_sphereRings + 1);
    int expectedIndices = m_sphereSegments * m_sphereRings * 6; // 2 triangles per quad, 3 indices per triangle
    
    if (expectedVertices > 100000 || expectedIndices > 600000) {
        qDebug() << "VRVideoRenderer: Dome mesh would be too large - vertices:" << expectedVertices
                 << "indices:" << expectedIndices;
        return false;
    }
    
    struct Vertex {
        float x, y, z;
        float u, v;
        float isPole;  // 1.0 for pole vertices, 0.0 for others
    };
    
    QVector<Vertex> vertices;
    QVector<GLuint> indices;
    
    // Reserve memory to avoid reallocations
    vertices.reserve(expectedVertices);
    indices.reserve(expectedIndices);
    
    // Convert degrees to radians
    float horizontalRadians = qDegreesToRadians(horizontalDegrees);
    float verticalRadians = qDegreesToRadians(verticalDegrees);
    
    // Generate dome vertices with variable angular coverage
    for (int ring = 0; ring <= m_sphereRings; ++ring) {
        // For proper dome shape that stays centered:
        // Map ring from 0 to 1, then to angle range centered on horizon
        float ringRatio = (float)ring / m_sphereRings;
        // Vertical angle from -verticalRadians/2 to +verticalRadians/2 (centered on horizon)
        float theta = (ringRatio * verticalRadians) - (verticalRadians / 2.0f) + (M_PI / 2.0f);
        float sinTheta = qSin(theta);
        float cosTheta = qCos(theta);
        
        // Check if this is a pole (top or bottom)
        bool isTopPole = (ring == 0 && verticalDegrees >= 179.0f);
        bool isBottomPole = (ring == m_sphereRings && verticalDegrees >= 179.0f);
        bool isPole = isTopPole || isBottomPole;
        
        for (int segment = 0; segment <= m_sphereSegments; ++segment) {
            // Horizontal angle from -horizontalRadians/2 to +horizontalRadians/2
            float segmentRatio = (float)segment / m_sphereSegments;
            float phi = (segmentRatio * horizontalRadians) - (horizontalRadians / 2.0f);
            float sinPhi = qSin(phi);
            float cosPhi = qCos(phi);
            
            Vertex vertex;
            // Position the dome facing forward (negative Z)
            // Scale up the dome for better viewing
            float scale = 1.5f;
            // IMPORTANT: Negate X to create an inside-out dome for viewing from inside
            vertex.x = -cosPhi * sinTheta * scale;  // Left-right (negated for inside viewing)
            vertex.y = cosTheta * scale;             // Up-down (now properly centered)
            vertex.z = -sinPhi * sinTheta * scale;  // Forward-back (negated for forward)
            
            // Texture coordinates - special handling for poles
            if (isPole) {
                // At poles, use center U coordinate to avoid singularity
                vertex.u = 0.5f;
                vertex.v = isTopPole ? 0.0f : 1.0f;
            } else {
                // Normal texture mapping for non-pole vertices
                vertex.u = (float)segment / m_sphereSegments;
                vertex.v = (float)ring / m_sphereRings;
            }
            
            // Mark pole vertices for special handling in shader
            vertex.isPole = isPole ? 1.0f : 0.0f;
            
            vertices.append(vertex);
        }
    }
    
    // Generate dome indices
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
    
    m_domeIndexCount = indices.size();
    
    // Create VAO
    if (!m_domeVAO.isCreated()) {
        m_domeVAO.create();
    }
    m_domeVAO.bind();
    
    // Create and upload vertex buffer
    if (!m_domeVertexBuffer.isCreated()) {
        m_domeVertexBuffer.create();
    }
    m_domeVertexBuffer.bind();
    m_domeVertexBuffer.allocate(vertices.constData(), vertices.size() * sizeof(Vertex));
    
    // Set vertex attributes
    glEnableVertexAttribArray(0);  // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    
    glEnableVertexAttribArray(1);  // Texture coordinates
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         reinterpret_cast<void*>(3 * sizeof(float)));
    
    glEnableVertexAttribArray(2);  // isPole flag
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         reinterpret_cast<void*>(5 * sizeof(float)));
    
    // Create and upload index buffer
    if (!m_domeIndexBuffer.isCreated()) {
        m_domeIndexBuffer.create();
    }
    m_domeIndexBuffer.bind();
    m_domeIndexBuffer.allocate(indices.constData(), indices.size() * sizeof(GLuint));
    
    m_domeVAO.release();
    
    qDebug() << "VRVideoRenderer: Dome mesh created with" << vertices.size() 
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
    
    // Clear left eye framebuffer to black to remove any uninitialized data
    if (m_leftEyeFBO->bind()) {
        glViewport(0, 0, m_renderWidth, m_renderHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Clear to black
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_leftEyeFBO->release();
        qDebug() << "VRVideoRenderer: Left eye framebuffer cleared to black";
    }
    
    // Create right eye framebuffer
    m_rightEyeFBO = std::make_unique<QOpenGLFramebufferObject>(
        QSize(m_renderWidth, m_renderHeight),
        QOpenGLFramebufferObject::CombinedDepthStencil);
    
    if (!m_rightEyeFBO->isValid()) {
        qDebug() << "VRVideoRenderer: Failed to create right eye framebuffer";
        return false;
    }
    
    // Clear right eye framebuffer to black to remove any uninitialized data (especially blue tint)
    if (m_rightEyeFBO->bind()) {
        glViewport(0, 0, m_renderWidth, m_renderHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Clear to black (was potentially blue before)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_rightEyeFBO->release();
        qDebug() << "VRVideoRenderer: Right eye framebuffer cleared to black";
    }
    
    qDebug() << "VRVideoRenderer: Render targets created and cleared successfully";
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

bool VRVideoRenderer::updateVideoTextureDirect(void* buffer, unsigned int width, unsigned int height)
{
    static int updateCount = 0;
    updateCount++;
    
    if (!m_initialized || !buffer) {
        if (updateCount % 30 == 0) {
            qDebug() << "VRVideoRenderer: Cannot update texture - not initialized or null buffer";
        }
        return false;
    }
    
    // Security: Validate dimensions to prevent buffer overflow
    const unsigned int MAX_TEXTURE_WIDTH = 8192;  // 8K max
    const unsigned int MAX_TEXTURE_HEIGHT = 8192; // 8K max
    
    if (width == 0 || height == 0 || width > MAX_TEXTURE_WIDTH || height > MAX_TEXTURE_HEIGHT) {
        if (updateCount % 30 == 0) {
            qDebug() << "VRVideoRenderer: Invalid texture dimensions:" << width << "x" << height;
        }
        return false;
    }
    
    // Security: Check for buffer size overflow (RGBA = 4 bytes per pixel)
    size_t expectedSize = static_cast<size_t>(width) * height * 4;
    if (expectedSize / 4 != static_cast<size_t>(width) * height) {
        qDebug() << "VRVideoRenderer: Buffer size calculation overflow";
        return false;
    }
    
    // Check if we have a valid OpenGL context
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        if (updateCount % 30 == 0) {
            qDebug() << "VRVideoRenderer: No OpenGL context current";
        }
        return false;
    }
    
    // Clear any existing OpenGL errors
    while (glGetError() != GL_NO_ERROR) {}
    
    // Create texture if it doesn't exist
    if (!m_videoTexture) {
        glGenTextures(1, &m_videoTexture);
        m_ownVideoTexture = true;
        qDebug() << "VRVideoRenderer: Created new video texture with ID:" << m_videoTexture;
        
        // Bind and initialize texture with proper parameters first
        glBindTexture(GL_TEXTURE_2D, m_videoTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // Allocate texture storage
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 
                     width, height, 
                     0, GL_RGBA, GL_UNSIGNED_BYTE, 
                     nullptr);
        m_textureWidth = width;
        m_textureHeight = height;
    } else {
        glBindTexture(GL_TEXTURE_2D, m_videoTexture);
    }
    
    if (updateCount % 30 == 0) {
        qDebug() << "VRVideoRenderer: Direct texture update" << m_videoTexture 
                 << "with buffer" << width << "x" << height;
    }
    
    // Check if texture dimensions have changed
    if (m_textureWidth != width || m_textureHeight != height) {
        // Dimensions changed, need to reallocate texture
        if (updateCount % 10 == 0) {
            qDebug() << "VRVideoRenderer: Texture dimensions changed from" 
                     << m_textureWidth << "x" << m_textureHeight << "to" << width << "x" << height;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     width, height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE,
                     buffer);
        m_textureWidth = width;
        m_textureHeight = height;
    } else {
        // Update texture data using glTexSubImage2D (more efficient for updates)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        buffer);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Ensure texture update is completed
    glFlush();
    
    // Check for OpenGL errors
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        if (updateCount % 30 == 0) {
            const char* errorString = "Unknown";
            switch(err) {
                case GL_INVALID_ENUM: errorString = "GL_INVALID_ENUM"; break;
                case GL_INVALID_VALUE: errorString = "GL_INVALID_VALUE"; break;
                case GL_INVALID_OPERATION: errorString = "GL_INVALID_OPERATION"; break;
                case GL_OUT_OF_MEMORY: errorString = "GL_OUT_OF_MEMORY"; break;
            }
            qDebug() << "VRVideoRenderer: OpenGL error during direct texture update:" 
                     << err << "(" << errorString << ")";
        }
        return false;
    }
    
    return true;
}

bool VRVideoRenderer::updateVideoTexture(const QImage& frame)
{
    static int updateCount = 0;
    updateCount++;
    
    if (!m_initialized) {
        if (updateCount % 30 == 0) {
            qDebug() << "VRVideoRenderer: Cannot update texture - not initialized";
        }
        return false;
    }
    
    // Create texture if it doesn't exist
    if (!m_videoTexture) {
        glGenTextures(1, &m_videoTexture);
        m_ownVideoTexture = true;
        m_textureWidth = 0;  // Force allocation on first update
        m_textureHeight = 0;
        qDebug() << "VRVideoRenderer: Created new video texture with ID:" << m_videoTexture;
    }
    
    glBindTexture(GL_TEXTURE_2D, m_videoTexture);
    
    // Convert image to OpenGL format
    // Only flip vertically (false, true) because OpenGL has origin at bottom-left
    QImage glFrame = frame.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
    
    static QImage lastFrame;
    bool frameChanged = (lastFrame.isNull() || lastFrame != glFrame);
    
    if (updateCount % 30 == 0) {
        qDebug() << "VRVideoRenderer: Updating texture" << m_videoTexture 
                 << "with frame" << glFrame.width() << "x" << glFrame.height()
                 << "Frame changed:" << frameChanged;
    }
    
    lastFrame = glFrame;
    
    // Check if we need to reallocate texture
    if (m_textureWidth != (unsigned int)glFrame.width() || m_textureHeight != (unsigned int)glFrame.height()) {
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
        
        m_textureWidth = glFrame.width();
        m_textureHeight = glFrame.height();
    } else {
        // Just update the texture data
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        glFrame.width(), glFrame.height(),
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        glFrame.constBits());
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Ensure texture update is completed
    glFlush();
    
    // Check for OpenGL errors
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qDebug() << "VRVideoRenderer: OpenGL error during texture update:" << err;
        return false;
    }
    
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

void VRVideoRenderer::renderEye(bool leftEye, const QMatrix4x4& view, const QMatrix4x4& projection, float zoomScale)
{
    static int leftRenderCount = 0;
    static int rightRenderCount = 0;
    
    if (leftEye) {
        leftRenderCount++;
    } else {
        rightRenderCount++;
    }
    
    if (!m_initialized) {
        if ((leftEye && leftRenderCount % 90 == 0) || (!leftEye && rightRenderCount % 90 == 0)) {
            qDebug() << "VRVideoRenderer:" << (leftEye ? "Left" : "Right") << "eye - Not initialized";
        }
        return;
    }
    
    if (!m_videoTexture) {
        if ((leftEye && leftRenderCount % 90 == 0) || (!leftEye && rightRenderCount % 90 == 0)) {
            qDebug() << "VRVideoRenderer:" << (leftEye ? "Left" : "Right") << "eye - No video texture available";
        }
        return;
    }
    
    // Bind appropriate framebuffer
    QOpenGLFramebufferObject* fbo = leftEye ? m_leftEyeFBO.get() : m_rightEyeFBO.get();
    if (!fbo->bind()) {
        qDebug() << "VRVideoRenderer: Failed to bind framebuffer for" << (leftEye ? "left" : "right") << "eye";
        return;
    }
    
    // Clear to black (video should overlay this)
    glViewport(0, 0, m_renderWidth, m_renderHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Black background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Verify clear worked
    GLenum clearErr = glGetError();
    if (clearErr != GL_NO_ERROR && ((leftEye && leftRenderCount % 90 == 0) || (!leftEye && rightRenderCount % 90 == 0))) {
        qDebug() << "VRVideoRenderer: OpenGL error after clear for" << (leftEye ? "left" : "right") << "eye:" << clearErr;
    }
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // IMPORTANT: Disable face culling so we can see the sphere from inside
    glDisable(GL_CULL_FACE);
    
    // Calculate MVP matrix
    QMatrix4x4 mvpMatrix = projection * view;
    
    // Render based on video format
    switch (m_videoFormat) {
        case VideoFormat::Mono360:
        case VideoFormat::Stereo360_TB:
        case VideoFormat::Stereo360_SBS:
            renderSphere(mvpMatrix, leftEye, zoomScale);
            break;
            
        case VideoFormat::Mono180:
        case VideoFormat::Stereo180_TB:
        case VideoFormat::Stereo180_SBS:
            renderDome(mvpMatrix, leftEye, zoomScale);
            break;
            
        case VideoFormat::Fisheye180:
        case VideoFormat::Fisheye180_TB:
        case VideoFormat::Fisheye180_SBS:
            renderFisheye(mvpMatrix, leftEye, zoomScale);
            break;
            
        case VideoFormat::Flat2D:
            renderFlat(mvpMatrix, zoomScale);
            break;
    }
    
    // Log rendering success periodically
    if ((leftEye && leftRenderCount % 90 == 0) || (!leftEye && rightRenderCount % 90 == 0)) {
        // Check for OpenGL errors
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            qDebug() << "VRVideoRenderer:" << (leftEye ? "Left" : "Right") << "eye - OpenGL error:" << err;
        } else {
            qDebug() << "VRVideoRenderer: Rendered" << (leftEye ? "left" : "right") 
                     << "eye successfully, texture ID:" << m_videoTexture;
        }
    }
    
    fbo->release();
}

void VRVideoRenderer::renderSphere(const QMatrix4x4& mvpMatrix, bool leftEye, float zoomScale)
{
    static int sphereRenderCount = 0;
    sphereRenderCount++;
    
    static int leftDomeCount = 0;
    static int rightDomeCount = 0;
    
    if (leftEye) {
        leftDomeCount++;
    } else {
        rightDomeCount++;
    }
    
    // Clear any existing OpenGL errors
    while (glGetError() != GL_NO_ERROR) {}
    
    if (!m_sphereShader) {
        qDebug() << "VRVideoRenderer: No sphere shader available";
        return;
    }
    
    if (!m_sphereShader->bind()) {
        qDebug() << "VRVideoRenderer: Failed to bind sphere shader";
        return;
    }
    
    // Set uniforms
    m_sphereShader->setUniformValue("mvpMatrix", mvpMatrix);
    m_sphereShader->setUniformValue("videoTexture", 0);
    m_sphereShader->setUniformValue("brightness", m_brightness);
    m_sphereShader->setUniformValue("contrast", m_contrast);
    m_sphereShader->setUniformValue("saturation", m_saturation);
    m_sphereShader->setUniformValue("zoomScale", 1.0f);  // No texture zoom for 360 videos
    
    // Set texture coordinate offset and scale based on format
    QVector2D texOffset = getTextureCoordOffset(leftEye);
    QVector2D texScale = getTextureCoordScale();
    m_sphereShader->setUniformValue("fisheyeMode", 0.0f);  // Normal mode, not fisheye
    m_sphereShader->setUniformValue("texOffset", texOffset);
    m_sphereShader->setUniformValue("texScale", texScale);
    m_sphereShader->setUniformValue("swapChannels", 1.0f);  // Enable R/B channel swap to fix blue tint
    m_sphereShader->setUniformValue("swapChannels", 1.0f);  // Enable R/B channel swap to fix blue tint
    
    // Log periodically to debug
    if ((leftEye && leftDomeCount % 90 == 0) || (!leftEye && rightDomeCount % 90 == 0)) {
        qDebug() << "VRVideoRenderer: Dome rendering for" << (leftEye ? "LEFT" : "RIGHT") 
                 << "eye - texOffset:" << texOffset << "texScale:" << texScale;
    }
    
    // Bind video texture
    glActiveTexture(GL_TEXTURE0);
    if (m_videoTexture) {
        glBindTexture(GL_TEXTURE_2D, m_videoTexture);
        
        // Verify texture is bound
        GLint boundTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
        if (sphereRenderCount % 180 == 0) {
            qDebug() << "VRVideoRenderer: Rendering sphere for" << (leftEye ? "left" : "right") 
                     << "eye with texture" << m_videoTexture 
                     << "(bound:" << boundTexture << ")"
                     << "indices:" << m_sphereIndexCount
                     << "texOffset:" << texOffset << "texScale:" << texScale;
        }
    } else {
        if (sphereRenderCount % 180 == 0) {
            qDebug() << "VRVideoRenderer: No video texture available for sphere rendering";
        }
    }
    
    // Render sphere
    m_sphereVAO.bind();
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, nullptr);
    
    // Check for errors after drawing
    GLenum err = glGetError();
    if (err != GL_NO_ERROR && sphereRenderCount % 180 == 0) {
        qDebug() << "VRVideoRenderer: OpenGL error after sphere draw:" << err;
    }
    
    m_sphereVAO.release();
    
    glBindTexture(GL_TEXTURE_2D, 0);
    m_sphereShader->release();
}

void VRVideoRenderer::renderDome(const QMatrix4x4& mvpMatrix, bool leftEye, float zoomScale)
{
    static int leftDomeCount = 0;
    static int rightDomeCount = 0;
    
    if (leftEye) {
        leftDomeCount++;
    } else {
        rightDomeCount++;
    }
    
    // For DeoVR-style zoom, adjust dome angular coverage for zoom out,
    // and texture coordinates for zoom in
    float textureZoom = 1.0f;
    
    if (qAbs(zoomScale - m_currentZoomScale) > 0.001f) {
        float targetHorizontalCoverage, targetVerticalCoverage;
        
        // Calculate coverage based on zoom level
        if (zoomScale <= 1.0f) {
            // Zoom out: reduce dome angular coverage (creates "()" circular shape)
            targetHorizontalCoverage = 180.0f * zoomScale;
            targetVerticalCoverage = 180.0f * zoomScale;
            textureZoom = 1.0f; // No texture zoom needed
        } else {
            // Zoom in: Keep dome at 180° and zoom texture coordinates instead
            // This maintains aspect ratio and avoids distortion
            targetVerticalCoverage = 180.0f;
            targetHorizontalCoverage = 180.0f;
            textureZoom = zoomScale; // Pass zoom to shader for texture coordinate scaling
        }
        
        // Clamp to reasonable ranges
        targetHorizontalCoverage = qBound(45.0f, targetHorizontalCoverage, 360.0f);
        targetVerticalCoverage = qBound(45.0f, targetVerticalCoverage, 180.0f);
        
        // Update the dome mesh with new coverage
        if (qAbs(m_domeHorizontalCoverage - targetHorizontalCoverage) > 0.1f || 
            qAbs(m_domeVerticalCoverage - targetVerticalCoverage) > 0.1f) {
            updateDomeAngularCoverage(targetHorizontalCoverage, targetVerticalCoverage);
            m_currentZoomScale = zoomScale;
        }
    } else {
        // Use current zoom for texture if > 1.0
        textureZoom = (zoomScale > 1.0f) ? zoomScale : 1.0f;
    }
    
    if ((leftEye && leftDomeCount % 30 == 0) || (!leftEye && rightDomeCount % 30 == 0)) {
        qDebug() << "VRVideoRenderer: Dome zoom:" << zoomScale 
                 << "- Coverage:" << m_domeHorizontalCoverage << "x" << m_domeVerticalCoverage << "degrees"
                 << "- Texture zoom:" << textureZoom;
    }
    
    // Clear any existing OpenGL errors
    while (glGetError() != GL_NO_ERROR) {}
    
    if (!m_sphereShader) {
        qDebug() << "VRVideoRenderer: No sphere shader available for dome";
        return;
    }
    
    if (!m_sphereShader->bind()) {
        qDebug() << "VRVideoRenderer: Failed to bind shader for dome";
        return;
    }
    
    // Set uniforms
    m_sphereShader->setUniformValue("mvpMatrix", mvpMatrix);
    m_sphereShader->setUniformValue("videoTexture", 0);
    m_sphereShader->setUniformValue("brightness", m_brightness);
    m_sphereShader->setUniformValue("contrast", m_contrast);
    m_sphereShader->setUniformValue("saturation", m_saturation);
    m_sphereShader->setUniformValue("zoomScale", textureZoom);  // Pass texture zoom to shader
    
    // Set texture coordinate offset and scale based on format
    QVector2D texOffset = getTextureCoordOffset(leftEye);
    QVector2D texScale = getTextureCoordScale();
    m_sphereShader->setUniformValue("fisheyeMode", 0.0f);  // Normal mode, not fisheye
    m_sphereShader->setUniformValue("texOffset", texOffset);
    m_sphereShader->setUniformValue("texScale", texScale);
    m_sphereShader->setUniformValue("swapChannels", 1.0f);  // Enable R/B channel swap to fix blue tint
    
    // Bind video texture
    glActiveTexture(GL_TEXTURE0);
    if (m_videoTexture) {
        glBindTexture(GL_TEXTURE_2D, m_videoTexture);
        
        // Log separately for each eye
        if ((leftEye && leftDomeCount % 180 == 0) || (!leftEye && rightDomeCount % 180 == 0)) {
            qDebug() << "VRVideoRenderer: Rendering dome for" << (leftEye ? "LEFT" : "RIGHT") 
                     << "eye with texture" << m_videoTexture
                     << "indices:" << m_domeIndexCount
                     << "texOffset:" << texOffset << "texScale:" << texScale;
        }
    } else {
        if ((leftEye && leftDomeCount % 180 == 0) || (!leftEye && rightDomeCount % 180 == 0)) {
            qDebug() << "VRVideoRenderer: No video texture available for dome rendering (" 
                     << (leftEye ? "LEFT" : "RIGHT") << "eye)";
        }
    }
    
    // Render dome mesh
    m_domeVAO.bind();
    glDrawElements(GL_TRIANGLES, m_domeIndexCount, GL_UNSIGNED_INT, nullptr);
    
    // Check for errors after drawing
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        if ((leftEye && leftDomeCount % 180 == 0) || (!leftEye && rightDomeCount % 180 == 0)) {
            qDebug() << "VRVideoRenderer: OpenGL error after dome draw (" 
                     << (leftEye ? "LEFT" : "RIGHT") << "eye):" << err;
        }
    }
    
    m_domeVAO.release();
    
    glBindTexture(GL_TEXTURE_2D, 0);
    m_sphereShader->release();
}

void VRVideoRenderer::renderFisheye(const QMatrix4x4& mvpMatrix, bool leftEye, float zoomScale)
{
    static int leftFisheyeCount = 0;
    static int rightFisheyeCount = 0;
    
    if (leftEye) {
        leftFisheyeCount++;
    } else {
        rightFisheyeCount++;
    }
    
    // Clear any existing OpenGL errors
    while (glGetError() != GL_NO_ERROR) {}
    
    if (!m_sphereShader) {
        qDebug() << "VRVideoRenderer: No sphere shader available for fisheye";
        return;
    }
    
    if (!m_sphereShader->bind()) {
        qDebug() << "VRVideoRenderer: Failed to bind shader for fisheye";
        return;
    }
    
    // Set uniforms
    m_sphereShader->setUniformValue("mvpMatrix", mvpMatrix);
    m_sphereShader->setUniformValue("videoTexture", 0);
    m_sphereShader->setUniformValue("brightness", m_brightness);
    m_sphereShader->setUniformValue("contrast", m_contrast);
    m_sphereShader->setUniformValue("saturation", m_saturation);
    m_sphereShader->setUniformValue("zoomScale", 1.0f);  // ALWAYS 1.0 - zoom is handled by FOV
    
    // For fisheye, we use different texture coordinate mapping
    // The texture offset and scale depend on the stereoscopic format
    QVector2D texOffset = getTextureCoordOffset(leftEye);
    QVector2D texScale = getTextureCoordScale();
    
    // Set fisheye mode to 1.0 to enable fisheye texture coordinate calculation in shader
    m_sphereShader->setUniformValue("fisheyeMode", 1.0f);
    m_sphereShader->setUniformValue("texOffset", texOffset);
    m_sphereShader->setUniformValue("texScale", texScale);
    m_sphereShader->setUniformValue("swapChannels", 1.0f);  // Enable R/B channel swap to fix blue tint
    
    // Bind video texture
    glActiveTexture(GL_TEXTURE0);
    if (m_videoTexture) {
        glBindTexture(GL_TEXTURE_2D, m_videoTexture);
        
        // Log separately for each eye
        if ((leftEye && leftFisheyeCount % 180 == 0) || (!leftEye && rightFisheyeCount % 180 == 0)) {
            qDebug() << "VRVideoRenderer: Rendering fisheye for" << (leftEye ? "LEFT" : "RIGHT") 
                     << "eye with texture" << m_videoTexture
                     << "indices:" << m_domeIndexCount
                     << "texOffset:" << texOffset << "texScale:" << texScale;
        }
    } else {
        if ((leftEye && leftFisheyeCount % 180 == 0) || (!leftEye && rightFisheyeCount % 180 == 0)) {
            qDebug() << "VRVideoRenderer: No video texture available for fisheye rendering (" 
                     << (leftEye ? "LEFT" : "RIGHT") << "eye)";
        }
    }
    
    // Use dome mesh for fisheye (hemisphere)
    m_domeVAO.bind();
    glDrawElements(GL_TRIANGLES, m_domeIndexCount, GL_UNSIGNED_INT, nullptr);
    
    // Check for errors after drawing
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        if ((leftEye && leftFisheyeCount % 180 == 0) || (!leftEye && rightFisheyeCount % 180 == 0)) {
            qDebug() << "VRVideoRenderer: OpenGL error after fisheye draw (" 
                     << (leftEye ? "LEFT" : "RIGHT") << "eye):" << err;
        }
    }
    
    m_domeVAO.release();
    
    glBindTexture(GL_TEXTURE_2D, 0);
    m_sphereShader->release();
}

void VRVideoRenderer::renderFlat(const QMatrix4x4& mvpMatrix, float zoomScale)
{
    Q_UNUSED(mvpMatrix);
    Q_UNUSED(zoomScale);  // TODO: Implement zoom for flat rendering when needed
    
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
        case VideoFormat::Fisheye180_TB:
            // Top-bottom: left eye uses top half, right eye uses bottom half
            return leftEye ? QVector2D(0.0f, 0.0f) : QVector2D(0.0f, 0.5f);
            
        case VideoFormat::Stereo360_SBS:
        case VideoFormat::Stereo180_SBS:
        case VideoFormat::Fisheye180_SBS:
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
        case VideoFormat::Fisheye180_TB:
            // Top-bottom: use half height
            return QVector2D(1.0f, 0.5f);
            
        case VideoFormat::Stereo360_SBS:
        case VideoFormat::Stereo180_SBS:
        case VideoFormat::Fisheye180_SBS:
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
    // Security: Validate tessellation parameters to prevent excessive memory allocation
    const int MAX_SEGMENTS = 256;  // Reasonable maximum for segments
    const int MAX_RINGS = 128;     // Reasonable maximum for rings
    const int MIN_SEGMENTS = 8;    // Minimum for basic sphere
    const int MIN_RINGS = 4;       // Minimum for basic sphere
    
    // Clamp to valid range
    segments = qBound(MIN_SEGMENTS, segments, MAX_SEGMENTS);
    rings = qBound(MIN_RINGS, rings, MAX_RINGS);
    
    if (segments == m_sphereSegments && rings == m_sphereRings) {
        return;
    }
    
    // Security: Check for potential vertex count overflow
    // Vertices = (segments + 1) * (rings + 1)
    int vertexCount = (segments + 1) * (rings + 1);
    if (vertexCount > 100000) {  // Limit to 100k vertices
        qDebug() << "VRVideoRenderer: Tessellation would create too many vertices:" << vertexCount;
        return;
    }
    
    qDebug() << "VRVideoRenderer: Updating sphere tessellation to" 
             << segments << "segments and" << rings << "rings"
             << "(" << vertexCount << "vertices)";
    
    m_sphereSegments = segments;
    m_sphereRings = rings;
    
    if (m_initialized) {
        createSphereMesh();
    }
}

void VRVideoRenderer::updateDomeAngularCoverage(float horizontalDegrees, float verticalDegrees)
{
    // Check if coverage has changed significantly
    if (qAbs(m_domeHorizontalCoverage - horizontalDegrees) < 0.1f && 
        qAbs(m_domeVerticalCoverage - verticalDegrees) < 0.1f) {
        return;  // No significant change
    }
    
    qDebug() << "VRVideoRenderer: Updating dome angular coverage from"
             << m_domeHorizontalCoverage << "x" << m_domeVerticalCoverage
             << "to" << horizontalDegrees << "x" << verticalDegrees << "degrees";
    
    if (m_initialized) {
        // Clean up existing dome mesh
        if (m_domeVAO.isCreated()) {
            m_domeVAO.destroy();
        }
        if (m_domeVertexBuffer.isCreated()) {
            m_domeVertexBuffer.destroy();
        }
        if (m_domeIndexBuffer.isCreated()) {
            m_domeIndexBuffer.destroy();
        }
        
        // Create new dome mesh with updated coverage
        createDomeMeshWithCoverage(horizontalDegrees, verticalDegrees);
    } else {
        // Just store the values if not initialized yet
        m_domeHorizontalCoverage = horizontalDegrees;
        m_domeVerticalCoverage = verticalDegrees;
    }
}
