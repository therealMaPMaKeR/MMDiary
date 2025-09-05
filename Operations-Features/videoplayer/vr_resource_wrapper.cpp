#include "vr_resource_wrapper.h"
#include <QDebug>

#ifdef USE_LIBVLC
#include <vlc/vlc.h>
#endif

#ifdef USE_OPENVR
#include <openvr.h>
#endif

namespace VideoPlayer {

// ============================================================================
// VLC Resource Implementations
// ============================================================================

// VLCInstance Implementation
VLCInstance::VLCInstance(libvlc_instance_t* instance)
    : ResourceWrapper(instance, vlcDeleter) {
    qDebug() << "VLCInstance: Wrapping VLC instance";
}

VLCInstance::~VLCInstance() {
    qDebug() << "VLCInstance: Destructor called";
}

bool VLCInstance::isValid() const {
    return ResourceWrapper::isValid();
}

QString VLCInstance::getVersion() const {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        const char* version = libvlc_get_version();
        return QString::fromUtf8(version);
    }
#endif
    return QString();
}

VLCInstance VLCInstance::create(int argc, const char* const* argv) {
#ifdef USE_LIBVLC
    qDebug() << "VLCInstance: Creating new VLC instance with" << argc << "arguments";
    libvlc_instance_t* instance = libvlc_new(argc, argv);
    if (instance) {
        qDebug() << "VLCInstance: Successfully created VLC instance";
        return VLCInstance(instance);
    } else {
        qDebug() << "VLCInstance: Failed to create VLC instance";
    }
#else
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    qDebug() << "VLCInstance: LibVLC not available";
#endif
    return VLCInstance();
}

void VLCInstance::vlcDeleter(libvlc_instance_t* instance) {
#ifdef USE_LIBVLC
    if (instance) {
        qDebug() << "VLCInstance: Releasing VLC instance";
        libvlc_release(instance);
    }
#else
    Q_UNUSED(instance);
#endif
}

// VLCMediaPlayer Implementation
VLCMediaPlayer::VLCMediaPlayer(libvlc_media_player_t* player)
    : ResourceWrapper(player, vlcDeleter) {
    qDebug() << "VLCMediaPlayer: Wrapping VLC media player";
}

VLCMediaPlayer::~VLCMediaPlayer() {
    qDebug() << "VLCMediaPlayer: Destructor called";
    stop(); // Ensure playback is stopped before destruction
}

bool VLCMediaPlayer::play() {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        int result = libvlc_media_player_play(m_resource);
        qDebug() << "VLCMediaPlayer: Play result:" << (result == 0 ? "success" : "failed");
        return result == 0;
    }
#endif
    return false;
}

bool VLCMediaPlayer::pause() {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        libvlc_media_player_pause(m_resource);
        qDebug() << "VLCMediaPlayer: Paused";
        return true;
    }
#endif
    return false;
}

bool VLCMediaPlayer::stop() {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        libvlc_media_player_stop(m_resource);
        qDebug() << "VLCMediaPlayer: Stopped";
        return true;
    }
#endif
    return false;
}

bool VLCMediaPlayer::isPlaying() const {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        return libvlc_media_player_is_playing(m_resource) != 0;
    }
#endif
    return false;
}

qint64 VLCMediaPlayer::getPosition() const {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        return libvlc_media_player_get_time(m_resource);
    }
#endif
    return -1;
}

qint64 VLCMediaPlayer::getDuration() const {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        return libvlc_media_player_get_length(m_resource);
    }
#endif
    return -1;
}

bool VLCMediaPlayer::setPosition(qint64 position) {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid && position >= 0) {
        libvlc_media_player_set_time(m_resource, position);
        qDebug() << "VLCMediaPlayer: Set position to" << position << "ms";
        return true;
    }
#else
    Q_UNUSED(position);
#endif
    return false;
}

VLCMediaPlayer VLCMediaPlayer::create(const VLCInstance& instance) {
#ifdef USE_LIBVLC
    libvlc_instance_t* vlcInstance = instance.get();
    if (vlcInstance) {
        qDebug() << "VLCMediaPlayer: Creating new media player";
        libvlc_media_player_t* player = libvlc_media_player_new(vlcInstance);
        if (player) {
            qDebug() << "VLCMediaPlayer: Successfully created media player";
            return VLCMediaPlayer(player);
        } else {
            qDebug() << "VLCMediaPlayer: Failed to create media player";
        }
    }
#else
    Q_UNUSED(instance);
    qDebug() << "VLCMediaPlayer: LibVLC not available";
#endif
    return VLCMediaPlayer();
}

void VLCMediaPlayer::vlcDeleter(libvlc_media_player_t* player) {
#ifdef USE_LIBVLC
    if (player) {
        qDebug() << "VLCMediaPlayer: Releasing media player";
        libvlc_media_player_release(player);
    }
#else
    Q_UNUSED(player);
#endif
}

// VLCMedia Implementation
VLCMedia::VLCMedia(libvlc_media_t* media)
    : ResourceWrapper(media, vlcDeleter) {
    qDebug() << "VLCMedia: Wrapping VLC media";
}

VLCMedia::~VLCMedia() {
    qDebug() << "VLCMedia: Destructor called";
}

QString VLCMedia::getUrl() const {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        char* url = libvlc_media_get_mrl(m_resource);
        if (url) {
            QString result = QString::fromUtf8(url);
            libvlc_free(url);
            return result;
        }
    }
#endif
    return QString();
}

qint64 VLCMedia::getDuration() const {
#ifdef USE_LIBVLC
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        return libvlc_media_get_duration(m_resource);
    }
#endif
    return -1;
}

VLCMedia VLCMedia::createFromPath(const VLCInstance& instance, const QString& path) {
#ifdef USE_LIBVLC
    libvlc_instance_t* vlcInstance = instance.get();
    if (vlcInstance && !path.isEmpty()) {
        qDebug() << "VLCMedia: Creating media from path:" << path;
        libvlc_media_t* media = libvlc_media_new_path(vlcInstance, path.toUtf8().constData());
        if (media) {
            qDebug() << "VLCMedia: Successfully created media";
            return VLCMedia(media);
        } else {
            qDebug() << "VLCMedia: Failed to create media from path";
        }
    }
#else
    Q_UNUSED(instance);
    Q_UNUSED(path);
    qDebug() << "VLCMedia: LibVLC not available";
#endif
    return VLCMedia();
}

VLCMedia VLCMedia::createFromUrl(const VLCInstance& instance, const QString& url) {
#ifdef USE_LIBVLC
    libvlc_instance_t* vlcInstance = instance.get();
    if (vlcInstance && !url.isEmpty()) {
        qDebug() << "VLCMedia: Creating media from URL:" << url;
        libvlc_media_t* media = libvlc_media_new_location(vlcInstance, url.toUtf8().constData());
        if (media) {
            qDebug() << "VLCMedia: Successfully created media";
            return VLCMedia(media);
        } else {
            qDebug() << "VLCMedia: Failed to create media from URL";
        }
    }
#else
    Q_UNUSED(instance);
    Q_UNUSED(url);
    qDebug() << "VLCMedia: LibVLC not available";
#endif
    return VLCMedia();
}

void VLCMedia::vlcDeleter(libvlc_media_t* media) {
#ifdef USE_LIBVLC
    if (media) {
        qDebug() << "VLCMedia: Releasing media";
        libvlc_media_release(media);
    }
#else
    Q_UNUSED(media);
#endif
}

// ============================================================================
// OpenVR Resource Implementations
// ============================================================================

#ifdef USE_OPENVR

// VRSystem Implementation
VRSystem::VRSystem(vr::IVRSystem* system)
    : ResourceWrapper(system, vrDeleter), m_hmdPoseValid(false) {
    qDebug() << "VRSystem: Wrapping VR system";
}

VRSystem::~VRSystem() {
    qDebug() << "VRSystem: Destructor called";
}

bool VRSystem::isHMDPresent() const {
    return vr::VR_IsHmdPresent();
}

bool VRSystem::getRecommendedRenderTargetSize(uint32_t& width, uint32_t& height) const {
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        m_resource->GetRecommendedRenderTargetSize(&width, &height);
        return true;
    }
    return false;
}

QMatrix4x4 VRSystem::getHMDPoseMatrix() const {
    QMutexLocker lock(&m_mutex);
    if (!m_hmdPoseValid) {
        return QMatrix4x4();
    }
    return m_cachedHMDPose;
}

QMatrix4x4 VRSystem::getProjectionMatrix(bool leftEye, float nearPlane, float farPlane) const {
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        vr::HmdMatrix44_t mat = m_resource->GetProjectionMatrix(
            leftEye ? vr::Eye_Left : vr::Eye_Right,
            nearPlane,
            farPlane);
        
        QMatrix4x4 result(
            mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
            mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
            mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
            mat.m[3][0], mat.m[3][1], mat.m[3][2], mat.m[3][3]
        );
        return result;
    }
    return QMatrix4x4();
}

bool VRSystem::isControllerConnected(uint32_t deviceIndex) const {
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid && deviceIndex < vr::k_unMaxTrackedDeviceCount) {
        return m_resource->IsTrackedDeviceConnected(deviceIndex);
    }
    return false;
}

bool VRSystem::getControllerState(uint32_t deviceIndex, void* state) const {
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid && state && deviceIndex < vr::k_unMaxTrackedDeviceCount) {
        vr::VRControllerState_t* controllerState = static_cast<vr::VRControllerState_t*>(state);
        return m_resource->GetControllerState(deviceIndex, controllerState, sizeof(vr::VRControllerState_t));
    }
    return false;
}

VRSystem VRSystem::initialize(QString& errorMessage) {
    qDebug() << "VRSystem: Initializing VR system";
    
    if (!vr::VR_IsRuntimeInstalled()) {
        errorMessage = "OpenVR runtime not installed";
        qDebug() << "VRSystem:" << errorMessage;
        return VRSystem();
    }
    
    if (!vr::VR_IsHmdPresent()) {
        errorMessage = "No VR headset detected";
        qDebug() << "VRSystem:" << errorMessage;
        return VRSystem();
    }
    
    vr::EVRInitError vrError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&vrError, vr::VRApplication_Scene);
    
    if (vrError != vr::VRInitError_None || !system) {
        const char* errorStr = vr::VR_GetVRInitErrorAsEnglishDescription(vrError);
        errorMessage = QString("Failed to initialize VR: %1").arg(errorStr);
        qDebug() << "VRSystem:" << errorMessage;
        return VRSystem();
    }
    
    qDebug() << "VRSystem: Successfully initialized VR system";
    return VRSystem(system);
}

void VRSystem::vrDeleter(vr::IVRSystem* system) {
    if (system) {
        qDebug() << "VRSystem: Shutting down VR system";
        vr::VR_Shutdown();
    }
}

// VRCompositor Implementation
VRCompositor::VRCompositor(vr::IVRCompositor* compositor)
    : ResourceWrapper(compositor, vrDeleter) {
    qDebug() << "VRCompositor: Wrapping VR compositor";
}

VRCompositor::~VRCompositor() {
    qDebug() << "VRCompositor: Destructor called";
}

bool VRCompositor::submitFrame(uint32_t leftEyeTexture, uint32_t rightEyeTexture) {
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        vr::Texture_t leftEyeTextureVR = { 
            reinterpret_cast<void*>(static_cast<uintptr_t>(leftEyeTexture)),
            vr::TextureType_OpenGL,
            vr::ColorSpace_Gamma
        };
        
        vr::Texture_t rightEyeTextureVR = { 
            reinterpret_cast<void*>(static_cast<uintptr_t>(rightEyeTexture)),
            vr::TextureType_OpenGL,
            vr::ColorSpace_Gamma
        };
        
        vr::EVRCompositorError error;
        error = m_resource->Submit(vr::Eye_Left, &leftEyeTextureVR);
        if (error != vr::VRCompositorError_None) {
            qDebug() << "VRCompositor: Failed to submit left eye:" << error;
            return false;
        }
        
        error = m_resource->Submit(vr::Eye_Right, &rightEyeTextureVR);
        if (error != vr::VRCompositorError_None) {
            qDebug() << "VRCompositor: Failed to submit right eye:" << error;
            return false;
        }
        
        return true;
    }
    return false;
}

void VRCompositor::waitGetPoses() {
    QMutexLocker lock(&m_mutex);
    if (m_resource && m_isValid) {
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        m_resource->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    }
}

bool VRCompositor::isReady() const {
    QMutexLocker lock(&m_mutex);
    return m_resource != nullptr && m_isValid;
}

VRCompositor VRCompositor::get() {
    vr::IVRCompositor* compositor = vr::VRCompositor();
    if (compositor) {
        qDebug() << "VRCompositor: Got VR compositor";
        return VRCompositor(compositor);
    }
    qDebug() << "VRCompositor: Failed to get VR compositor";
    return VRCompositor();
}

void VRCompositor::vrDeleter(vr::IVRCompositor* compositor) {
    // Compositor is managed by OpenVR, no explicit deletion needed
    Q_UNUSED(compositor);
    qDebug() << "VRCompositor: Compositor cleanup (managed by OpenVR)";
}

#endif // USE_OPENVR

// ============================================================================
// Manager Implementations
// ============================================================================

// VLCManager Implementation
VLCManager& VLCManager::instance() {
    static VLCManager instance;
    return instance;
}

VLCInstance& VLCManager::getVLCInstance() {
    QMutexLocker lock(&m_mutex);
    if (!m_instance.isValid()) {
        qDebug() << "VLCManager: Creating VLC instance";
        
        // VLC command line arguments for better performance and stability
        const char* vlc_args[] = {
            "--no-xlib",           // Avoid X11 on Linux
            "--quiet",             // Suppress console output
            "--no-video-title",    // Don't show title on video
            "--no-stats",          // Don't collect stats
            "--no-snapshot-preview" // Don't show snapshot preview
        };
        
        m_instance = VLCInstance::create(sizeof(vlc_args) / sizeof(vlc_args[0]), vlc_args);
    }
    return m_instance;
}

void VLCManager::shutdown() {
    QMutexLocker lock(&m_mutex);
    qDebug() << "VLCManager: Shutting down";
    m_instance.reset();
}

VLCManager::~VLCManager() {
    shutdown();
}

#ifdef USE_OPENVR
// VRManager Implementation
VRManager& VRManager::instance() {
    static VRManager instance;
    return instance;
}

VRSystem& VRManager::getVRSystem() {
    QMutexLocker lock(&m_mutex);
    if (!m_system.isValid()) {
        QString error;
        m_system = VRSystem::initialize(error);
        if (!m_system.isValid()) {
            qDebug() << "VRManager: Failed to initialize VR system:" << error;
        } else {
            m_initialized = true;
        }
    }
    return m_system;
}

VRCompositor& VRManager::getVRCompositor() {
    QMutexLocker lock(&m_mutex);
    if (!m_compositor.isValid() && m_system.isValid()) {
        m_compositor = VRCompositor::get();
    }
    return m_compositor;
}

bool VRManager::initialize(QString& errorMessage) {
    QMutexLocker lock(&m_mutex);
    if (m_initialized) {
        return true;
    }
    
    m_system = VRSystem::initialize(errorMessage);
    if (m_system.isValid()) {
        m_compositor = VRCompositor::get();
        m_initialized = m_compositor.isValid();
    }
    
    return m_initialized;
}

void VRManager::shutdown() {
    QMutexLocker lock(&m_mutex);
    qDebug() << "VRManager: Shutting down";
    m_compositor.reset();
    m_system.reset();
    m_initialized = false;
}

bool VRManager::isInitialized() const {
    QMutexLocker lock(&m_mutex);
    return m_initialized;
}

VRManager::~VRManager() {
    shutdown();
}
#endif

} // namespace VideoPlayer
