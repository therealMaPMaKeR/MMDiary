#include "vr_openvr_manager.h"
#include <QDebug>
#include <QFile>
#include <QCoreApplication>

VROpenVRManager::VROpenVRManager(QObject *parent)
    : QObject(parent)
    , m_vrSystem(nullptr)
    , m_vrCompositor(nullptr)
    , m_isInitialized(false)
    , m_status(VRStatus::NotInitialized)
    , m_hmdPoseValid(false)
{
    qDebug() << "VROpenVRManager: Constructor called";
}

VROpenVRManager::~VROpenVRManager()
{
    qDebug() << "VROpenVRManager: Destructor called";
    shutdown();
}

bool VROpenVRManager::initialize()
{
    qDebug() << "VROpenVRManager: Initializing OpenVR system";
    
    if (m_isInitialized) {
        qDebug() << "VROpenVRManager: Already initialized";
        return true;
    }
    
    // First check if SteamVR runtime is available
    if (!checkSteamVRRuntime()) {
        m_status = VRStatus::SteamVRNotFound;
        setLastError("SteamVR runtime not found. Please ensure Steam and SteamVR are installed.");
        qDebug() << "VROpenVRManager: SteamVR runtime not detected";
        emit statusChanged(m_status);
        emit error(m_lastError);
        return false;
    }
    
    // Try to initialize OpenVR
    if (!initializeOpenVR()) {
        qDebug() << "VROpenVRManager: Failed to initialize OpenVR";
        emit statusChanged(m_status);
        emit error(m_lastError);
        return false;
    }
    
    m_isInitialized = true;
    m_status = VRStatus::Ready;
    emit statusChanged(m_status);
    emit hmdConnected();
    
    qDebug() << "VROpenVRManager: Successfully initialized";
    qDebug() << "VROpenVRManager: HMD: " << m_systemInfo.hmdName;
    qDebug() << "VROpenVRManager: Resolution: " << m_systemInfo.renderWidth 
             << "x" << m_systemInfo.renderHeight;
    qDebug() << "VROpenVRManager: Refresh Rate: " << m_systemInfo.refreshRate << "Hz";
    
    return true;
}

void VROpenVRManager::shutdown()
{
    if (!m_isInitialized) {
        return;
    }
    
    qDebug() << "VROpenVRManager: Shutting down OpenVR system";
    
#ifdef USE_OPENVR
    if (m_vrSystem) {
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
        m_vrCompositor = nullptr;
    }
#endif
    
    m_isInitialized = false;
    m_status = VRStatus::NotInitialized;
    emit statusChanged(m_status);
    emit hmdDisconnected();
    
    qDebug() << "VROpenVRManager: Shutdown complete";
}

bool VROpenVRManager::checkSteamVRRuntime()
{
#ifdef USE_OPENVR
    // Check if OpenVR runtime is installed
    if (!vr::VR_IsRuntimeInstalled()) {
        qDebug() << "VROpenVRManager: OpenVR runtime is not installed";
        return false;
    }
    
    // Check if HMD is present (this doesn't initialize the system yet)
    if (!vr::VR_IsHmdPresent()) {
        m_status = VRStatus::NoHMDConnected;
        setLastError("No VR headset detected. Please connect your VR headset and ensure it's powered on.");
        qDebug() << "VROpenVRManager: No HMD present";
        return false;
    }
    
    return true;
#else
    qDebug() << "VROpenVRManager: OpenVR support not compiled in";
    setLastError("VR support is not available in this build.");
    return false;
#endif
}

bool VROpenVRManager::initializeOpenVR()
{
#ifdef USE_OPENVR
    vr::EVRInitError vrError = vr::VRInitError_None;
    m_vrSystem = vr::VR_Init(&vrError, vr::VRApplication_Scene);
    
    if (vrError != vr::VRInitError_None) {
        m_vrSystem = nullptr;
        m_status = VRStatus::InitializationFailed;
        
        const char* errorStr = vr::VR_GetVRInitErrorAsEnglishDescription(vrError);
        QString errorMsg = QString("Failed to initialize VR: %1").arg(errorStr);
        setLastError(errorMsg);
        
        qDebug() << "VROpenVRManager: VR_Init failed:" << errorStr;
        return false;
    }
    
    // Get compositor
    m_vrCompositor = vr::VRCompositor();
    if (!m_vrCompositor) {
        setLastError("Failed to get VR compositor");
        qDebug() << "VROpenVRManager: Failed to get compositor";
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
        m_status = VRStatus::InitializationFailed;
        return false;
    }
    
    // Get system information
    m_systemInfo.hmdName = getTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, 
                                                  static_cast<uint32_t>(vr::Prop_TrackingSystemName_String));
    
    // Get recommended render target size
    uint32_t renderWidth, renderHeight;
    m_vrSystem->GetRecommendedRenderTargetSize(&renderWidth, &renderHeight);
    m_systemInfo.renderWidth = renderWidth;
    m_systemInfo.renderHeight = renderHeight;
    
    // Get refresh rate
    vr::ETrackedPropertyError propError;
    m_systemInfo.refreshRate = m_vrSystem->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, 
        vr::Prop_DisplayFrequency_Float, 
        &propError);
    
    if (propError != vr::TrackedProp_Success) {
        m_systemInfo.refreshRate = 90.0f; // Default to 90Hz
    }
    
    // Check for controllers
    m_systemInfo.hasControllers = false;
    for (uint32_t device = 0; device < vr::k_unMaxTrackedDeviceCount; ++device) {
        if (m_vrSystem->GetTrackedDeviceClass(device) == vr::TrackedDeviceClass_Controller) {
            m_systemInfo.hasControllers = true;
            break;
        }
    }
    
    return true;
#else
    setLastError("OpenVR support not compiled in");
    return false;
#endif
}

bool VROpenVRManager::isSteamVRAvailable() const
{
#ifdef USE_OPENVR
    return vr::VR_IsRuntimeInstalled();
#else
    return false;
#endif
}

bool VROpenVRManager::isHMDPresent() const
{
#ifdef USE_OPENVR
    return vr::VR_IsHmdPresent();
#else
    return false;
#endif
}

QString VROpenVRManager::getStatusMessage() const
{
    switch (m_status) {
        case VRStatus::NotInitialized:
            return "VR system not initialized";
        case VRStatus::SteamVRNotFound:
            return "SteamVR not found. Please install Steam and SteamVR.";
        case VRStatus::NoHMDConnected:
            return "No VR headset connected";
        case VRStatus::InitializationFailed:
            return "Failed to initialize VR system";
        case VRStatus::Ready:
            return "VR system ready";
        case VRStatus::Error:
            return m_lastError;
        default:
            return "Unknown status";
    }
}

void VROpenVRManager::getRecommendedRenderTargetSize(uint32_t& width, uint32_t& height) const
{
    width = m_systemInfo.renderWidth;
    height = m_systemInfo.renderHeight;
}

QMatrix4x4 VROpenVRManager::getHMDPoseMatrix() const
{
    return m_hmdPoseMatrix;
}

QMatrix4x4 VROpenVRManager::getProjectionMatrix(bool leftEye, float nearPlane, float farPlane) const
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return QMatrix4x4();
    }
    
    vr::HmdMatrix44_t mat = m_vrSystem->GetProjectionMatrix(
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
#else
    Q_UNUSED(leftEye);
    Q_UNUSED(nearPlane);
    Q_UNUSED(farPlane);
    return QMatrix4x4();
#endif
}

QMatrix4x4 VROpenVRManager::getEyePosMatrix(bool leftEye) const
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return QMatrix4x4();
    }
    
    vr::HmdMatrix34_t mat = m_vrSystem->GetEyeToHeadTransform(
        leftEye ? vr::Eye_Left : vr::Eye_Right);
    
    QMatrix4x4 result(
        mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
        mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
        mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
        0.0f, 0.0f, 0.0f, 1.0f
    );
    
    return result.inverted();
#else
    Q_UNUSED(leftEye);
    return QMatrix4x4();
#endif
}

bool VROpenVRManager::submitFrame(uint32_t leftEyeTexture, uint32_t rightEyeTexture)
{
#ifdef USE_OPENVR
    if (!m_vrCompositor || !m_vrSystem) {
        return false;
    }
    
    vr::Texture_t leftEyeTextureVR = { (void*)(uintptr_t)leftEyeTexture, 
                                       vr::TextureType_OpenGL, 
                                       vr::ColorSpace_Gamma };
    vr::Texture_t rightEyeTextureVR = { (void*)(uintptr_t)rightEyeTexture, 
                                        vr::TextureType_OpenGL, 
                                        vr::ColorSpace_Gamma };
    
    vr::EVRCompositorError error;
    error = m_vrCompositor->Submit(vr::Eye_Left, &leftEyeTextureVR);
    if (error != vr::VRCompositorError_None) {
        qDebug() << "VROpenVRManager: Failed to submit left eye:" << error;
        return false;
    }
    
    error = m_vrCompositor->Submit(vr::Eye_Right, &rightEyeTextureVR);
    if (error != vr::VRCompositorError_None) {
        qDebug() << "VROpenVRManager: Failed to submit right eye:" << error;
        return false;
    }
    
    return true;
#else
    Q_UNUSED(leftEyeTexture);
    Q_UNUSED(rightEyeTexture);
    return false;
#endif
}

void VROpenVRManager::compositorWaitGetPoses()
{
#ifdef USE_OPENVR
    if (!m_vrCompositor) {
        return;
    }
    
    m_vrCompositor->WaitGetPoses(m_trackedDevicePoses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    
    // Update HMD pose
    updateHMDPose();
#endif
}

bool VROpenVRManager::isCompositorReady() const
{
#ifdef USE_OPENVR
    return m_vrCompositor != nullptr;
#else
    return false;
#endif
}

void VROpenVRManager::updateHMDPose()
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return;
    }
    
    for (uint32_t device = 0; device < vr::k_unMaxTrackedDeviceCount; ++device) {
        if (m_vrSystem->GetTrackedDeviceClass(device) == vr::TrackedDeviceClass_HMD) {
            if (m_trackedDevicePoses[device].bPoseIsValid) {
                vr::HmdMatrix34_t mat = m_trackedDevicePoses[device].mDeviceToAbsoluteTracking;
                
                m_hmdPoseMatrix = QMatrix4x4(
                    mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
                    mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
                    mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
                    0.0f, 0.0f, 0.0f, 1.0f
                );
                m_hmdPoseValid = true;
            } else {
                m_hmdPoseValid = false;
            }
            break;
        }
    }
#endif
}

void VROpenVRManager::setLastError(const QString& error)
{
    m_lastError = error;
    qDebug() << "VROpenVRManager: Error -" << error;
}

QString VROpenVRManager::getTrackedDeviceString(uint32_t device, uint32_t prop)
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return QString();
    }
    
    vr::TrackedPropertyError error;
    uint32_t bufferLen = m_vrSystem->GetStringTrackedDeviceProperty(
        static_cast<vr::TrackedDeviceIndex_t>(device), 
        static_cast<vr::TrackedDeviceProperty>(prop), 
        nullptr, 0, &error);
    if (bufferLen == 0 || error != vr::TrackedProp_Success) {
        return QString();
    }
    
    char* buffer = new char[bufferLen];
    m_vrSystem->GetStringTrackedDeviceProperty(
        static_cast<vr::TrackedDeviceIndex_t>(device),
        static_cast<vr::TrackedDeviceProperty>(prop),
        buffer, bufferLen, &error);
    QString result = QString::fromLocal8Bit(buffer);
    delete[] buffer;
    
    return result;
#else
    Q_UNUSED(device);
    Q_UNUSED(prop);
    return QString();
#endif
}
