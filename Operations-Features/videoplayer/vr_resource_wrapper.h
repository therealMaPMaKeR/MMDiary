#ifndef VR_RESOURCE_WRAPPER_H
#define VR_RESOURCE_WRAPPER_H

#include <QDebug>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <memory>
#include <functional>
#include <atomic>

// Forward declarations for VLC types
struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;

// Forward declarations for OpenVR types
#ifdef USE_OPENVR
namespace vr {
    struct IVRSystem;
    struct IVRCompositor;
}
#endif

// ============================================================================
// Smart Pointer Wrappers for VLC and OpenVR Resources
// ============================================================================
// These wrappers provide RAII management for VLC and OpenVR resources,
// ensuring proper cleanup and preventing resource leaks and dangling pointers
// ============================================================================

namespace VideoPlayer {

// Base class for resource management with thread-safe reference counting
template<typename T>
class ResourceWrapper {
public:
    using Deleter = std::function<void(T*)>;
    
    // Constructors
    ResourceWrapper() noexcept 
        : m_resource(nullptr), m_refCount(0), m_isValid(false) {
        qDebug() << "ResourceWrapper: Default constructor";
    }
    
    explicit ResourceWrapper(T* resource, Deleter deleter = defaultDeleter) noexcept
        : m_resource(resource), m_deleter(deleter), m_refCount(1), m_isValid(resource != nullptr) {
        if (m_resource) {
            qDebug() << "ResourceWrapper: Wrapping resource at" << static_cast<void*>(m_resource);
        }
    }
    
    // Move constructor
    ResourceWrapper(ResourceWrapper&& other) noexcept {
        QMutexLocker otherLock(&other.m_mutex);
        m_resource = other.m_resource;
        m_deleter = std::move(other.m_deleter);
        m_refCount.store(other.m_refCount.load());
        m_isValid = other.m_isValid;
        
        other.m_resource = nullptr;
        other.m_isValid = false;
        other.m_refCount = 0;
        
        qDebug() << "ResourceWrapper: Move constructor, resource at" << static_cast<void*>(m_resource);
    }
    
    // Move assignment
    ResourceWrapper& operator=(ResourceWrapper&& other) noexcept {
        if (this != &other) {
            // Clean up current resource
            reset();
            
            // Move from other
            QMutexLocker otherLock(&other.m_mutex);
            m_resource = other.m_resource;
            m_deleter = std::move(other.m_deleter);
            m_refCount.store(other.m_refCount.load());
            m_isValid = other.m_isValid;
            
            other.m_resource = nullptr;
            other.m_isValid = false;
            other.m_refCount = 0;
            
            qDebug() << "ResourceWrapper: Move assignment, resource at" << static_cast<void*>(m_resource);
        }
        return *this;
    }
    
    // Destructor
    virtual ~ResourceWrapper() {
        reset();
    }
    
    // Delete copy constructor and copy assignment
    ResourceWrapper(const ResourceWrapper&) = delete;
    ResourceWrapper& operator=(const ResourceWrapper&) = delete;
    
    // Resource access
    T* get() const {
        QMutexLocker lock(&m_mutex);
        return m_isValid ? m_resource : nullptr;
    }
    
    T* operator->() const {
        return get();
    }
    
    explicit operator bool() const {
        QMutexLocker lock(&m_mutex);
        return m_isValid && m_resource != nullptr;
    }
    
    // Resource management
    void reset(T* resource = nullptr, Deleter deleter = defaultDeleter) {
        QMutexLocker lock(&m_mutex);
        
        // Clean up current resource
        if (m_resource && m_isValid) {
            if (--m_refCount == 0 && m_deleter) {
                qDebug() << "ResourceWrapper: Deleting resource at" << static_cast<void*>(m_resource);
                m_deleter(m_resource);
            }
        }
        
        // Set new resource
        m_resource = resource;
        m_deleter = deleter;
        m_refCount = resource ? 1 : 0;
        m_isValid = resource != nullptr;
        
        if (m_resource) {
            qDebug() << "ResourceWrapper: Reset with new resource at" << static_cast<void*>(m_resource);
        }
    }
    
    T* release() {
        QMutexLocker lock(&m_mutex);
        T* temp = m_resource;
        m_resource = nullptr;
        m_isValid = false;
        m_refCount = 0;
        qDebug() << "ResourceWrapper: Released resource at" << static_cast<void*>(temp);
        return temp;
    }
    
    bool isValid() const {
        QMutexLocker lock(&m_mutex);
        return m_isValid && m_resource != nullptr;
    }
    
    // Reference counting
    void addRef() {
        ++m_refCount;
        qDebug() << "ResourceWrapper: Reference count increased to" << m_refCount.load();
    }
    
    void releaseRef() {
        if (--m_refCount == 0) {
            reset();
        }
        qDebug() << "ResourceWrapper: Reference count decreased to" << m_refCount.load();
    }
    
    int refCount() const {
        return m_refCount.load();
    }
    
protected:
    T* m_resource;
    Deleter m_deleter;
    mutable QMutex m_mutex;
    std::atomic<int> m_refCount;
    bool m_isValid;
    
    static void defaultDeleter(T* ptr) {
        delete ptr;
    }
};

// ============================================================================
// VLC Resource Wrappers
// ============================================================================

// VLC Instance wrapper
class VLCInstance : public ResourceWrapper<libvlc_instance_t> {
public:
    VLCInstance() = default;
    explicit VLCInstance(libvlc_instance_t* instance);
    ~VLCInstance();
    
    // VLC-specific operations
    bool isValid() const;
    QString getVersion() const;
    
    // Factory method for safe creation
    static VLCInstance create(int argc = 0, const char* const* argv = nullptr);
    
private:
    static void vlcDeleter(libvlc_instance_t* instance);
};

// VLC Media Player wrapper
class VLCMediaPlayer : public ResourceWrapper<libvlc_media_player_t> {
public:
    VLCMediaPlayer() = default;
    explicit VLCMediaPlayer(libvlc_media_player_t* player);
    ~VLCMediaPlayer();
    
    // VLC-specific operations
    bool play();
    bool pause();
    bool stop();
    bool isPlaying() const;
    qint64 getPosition() const;
    qint64 getDuration() const;
    bool setPosition(qint64 position);
    
    // Factory method
    static VLCMediaPlayer create(const VLCInstance& instance);
    
private:
    static void vlcDeleter(libvlc_media_player_t* player);
};

// VLC Media wrapper
class VLCMedia : public ResourceWrapper<libvlc_media_t> {
public:
    VLCMedia() = default;
    explicit VLCMedia(libvlc_media_t* media);
    ~VLCMedia();
    
    // VLC-specific operations
    QString getUrl() const;
    qint64 getDuration() const;
    
    // Factory methods
    static VLCMedia createFromPath(const VLCInstance& instance, const QString& path);
    static VLCMedia createFromUrl(const VLCInstance& instance, const QString& url);
    
private:
    static void vlcDeleter(libvlc_media_t* media);
};

// ============================================================================
// OpenVR Resource Wrappers
// ============================================================================

#ifdef USE_OPENVR

// OpenVR System wrapper
class VRSystem : public ResourceWrapper<vr::IVRSystem> {
public:
    VRSystem() = default;
    explicit VRSystem(vr::IVRSystem* system);
    ~VRSystem();
    
    // VR-specific operations
    bool isHMDPresent() const;
    bool getRecommendedRenderTargetSize(uint32_t& width, uint32_t& height) const;
    QMatrix4x4 getHMDPoseMatrix() const;
    QMatrix4x4 getProjectionMatrix(bool leftEye, float nearPlane, float farPlane) const;
    
    // Controller operations
    bool isControllerConnected(uint32_t deviceIndex) const;
    bool getControllerState(uint32_t deviceIndex, void* state) const;
    
    // Factory method
    static VRSystem initialize(QString& errorMessage);
    
private:
    static void vrDeleter(vr::IVRSystem* system);
    mutable QMatrix4x4 m_cachedHMDPose;
    mutable bool m_hmdPoseValid;
};

// OpenVR Compositor wrapper
class VRCompositor : public ResourceWrapper<vr::IVRCompositor> {
public:
    VRCompositor() = default;
    explicit VRCompositor(vr::IVRCompositor* compositor);
    ~VRCompositor();
    
    // VR-specific operations
    bool submitFrame(uint32_t leftEyeTexture, uint32_t rightEyeTexture);
    void waitGetPoses();
    bool isReady() const;
    
    // Factory method
    static VRCompositor get();
    
private:
    static void vrDeleter(vr::IVRCompositor* compositor);
};

#endif // USE_OPENVR

// ============================================================================
// Utility Functions
// ============================================================================

// Thread-safe singleton manager for VLC instance
class VLCManager {
public:
    static VLCManager& instance();
    
    VLCInstance& getVLCInstance();
    void shutdown();
    
private:
    VLCManager() = default;
    ~VLCManager();
    
    VLCManager(const VLCManager&) = delete;
    VLCManager& operator=(const VLCManager&) = delete;
    
    VLCInstance m_instance;
    QMutex m_mutex;
};

#ifdef USE_OPENVR
// Thread-safe singleton manager for VR resources
class VRManager {
public:
    static VRManager& instance();
    
    VRSystem& getVRSystem();
    VRCompositor& getVRCompositor();
    bool initialize(QString& errorMessage);
    void shutdown();
    bool isInitialized() const;
    
private:
    VRManager() = default;
    ~VRManager();
    
    VRManager(const VRManager&) = delete;
    VRManager& operator=(const VRManager&) = delete;
    
    VRSystem m_system;
    VRCompositor m_compositor;
    mutable QMutex m_mutex;
    bool m_initialized = false;
};
#endif

// ============================================================================
// RAII Guards for automatic resource management
// ============================================================================

template<typename T>
class ResourceGuard {
public:
    explicit ResourceGuard(ResourceWrapper<T>& resource)
        : m_resource(resource), m_locked(false) {
        if (m_resource.isValid()) {
            m_resource.addRef();
            m_locked = true;
            qDebug() << "ResourceGuard: Acquired resource guard";
        }
    }
    
    ~ResourceGuard() {
        if (m_locked) {
            m_resource.releaseRef();
            qDebug() << "ResourceGuard: Released resource guard";
        }
    }
    
    // Disable copy
    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;
    
    // Enable move
    ResourceGuard(ResourceGuard&& other) noexcept
        : m_resource(other.m_resource), m_locked(other.m_locked) {
        other.m_locked = false;
    }
    
    ResourceGuard& operator=(ResourceGuard&& other) noexcept {
        if (this != &other) {
            if (m_locked) {
                m_resource.releaseRef();
            }
            m_resource = other.m_resource;
            m_locked = other.m_locked;
            other.m_locked = false;
        }
        return *this;
    }
    
    bool isLocked() const { return m_locked; }
    T* get() const { return m_resource.get(); }
    
private:
    ResourceWrapper<T>& m_resource;
    bool m_locked;
};

} // namespace VideoPlayer

// Convenience typedefs
using VLCInstancePtr = VideoPlayer::VLCInstance;
using VLCMediaPlayerPtr = VideoPlayer::VLCMediaPlayer;
using VLCMediaPtr = VideoPlayer::VLCMedia;

#ifdef USE_OPENVR
using VRSystemPtr = VideoPlayer::VRSystem;
using VRCompositorPtr = VideoPlayer::VRCompositor;
#endif

#endif // VR_RESOURCE_WRAPPER_H
