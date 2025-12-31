#include "vr_openvr_manager.h"
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QtMath>
#include <QStandardPaths>
#include <QDir>
#include <QStringList>
#include <QThread>
#include <QSet>
#include <QPair>
#include <cstring>

VROpenVRManager::VROpenVRManager(QObject *parent)
    : QObject(parent)
    , m_vrSystem(nullptr)
    , m_vrCompositor(nullptr)
    , m_isInitialized(false)
    , m_status(VRStatus::NotInitialized)
    , m_hmdPoseValid(false)
#ifdef USE_OPENVR
    , m_leftControllerIndex(vr::k_unTrackedDeviceIndexInvalid)
    , m_rightControllerIndex(vr::k_unTrackedDeviceIndexInvalid)
#else
    , m_leftControllerIndex(0)
    , m_rightControllerIndex(0)
    , m_lastLeftControllerState(nullptr)
    , m_lastRightControllerState(nullptr)
#endif
    , m_controllerInputReady(false)
    , m_lastButtonPressed(0)
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
    
    // Shutdown controller input first
    shutdownControllerInput();
    
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

void VROpenVRManager::getProjectionRawValues(bool leftEye, float& left, float& right, float& top, float& bottom) const
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        // Default symmetric frustum values
        left = -1.0f;
        right = 1.0f;
        top = 1.0f;
        bottom = -1.0f;
        return;
    }
    
    // Get the raw projection values from OpenVR
    // These define the tangent of the half-angles from the center view axis
    m_vrSystem->GetProjectionRaw(
        leftEye ? vr::Eye_Left : vr::Eye_Right,
        &left, &right, &top, &bottom);
    
#else
    Q_UNUSED(leftEye);
    // Default symmetric frustum values
    left = -1.0f;
    right = 1.0f;
    top = 1.0f;
    bottom = -1.0f;
#endif
}

QMatrix4x4 VROpenVRManager::getProjectionMatrixWithZoom(bool leftEye, float nearPlane, float farPlane, float zoomFactor) const
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return QMatrix4x4();
    }
    
    // Clamp zoom factor to reasonable range
    zoomFactor = qBound(0.1f, zoomFactor, 5.0f);
    
    // Get the original projection matrix from OpenVR
    // This ensures we have the correct format and conventions
    vr::HmdMatrix44_t mat = m_vrSystem->GetProjectionMatrix(
        leftEye ? vr::Eye_Left : vr::Eye_Right,
        nearPlane,
        farPlane);
    
    // Convert to QMatrix4x4
    QMatrix4x4 result(
        mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
        mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
        mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
        mat.m[3][0], mat.m[3][1], mat.m[3][2], mat.m[3][3]
    );
    
    // Apply zoom by scaling the focal length (diagonal elements)
    // This adjusts the FOV while maintaining the asymmetric stereo offsets
    result(0, 0) *= zoomFactor;  // Scale horizontal focal length
    result(1, 1) *= zoomFactor;  // Scale vertical focal length
    
    // The (0,2) and (1,2) elements contain the stereo asymmetry
    // We DON'T scale these as they need to remain constant for proper stereo
    
    // Log for debugging
    static int logCount = 0;
    if (++logCount % 300 == 0) { // Every ~3 seconds
        qDebug() << "VROpenVRManager: Zoom factor:" << zoomFactor << "for" << (leftEye ? "LEFT" : "RIGHT") << "eye";
        qDebug() << "VROpenVRManager: Projection matrix diagonal:" << result(0,0) << "," << result(1,1);
        qDebug() << "VROpenVRManager: Stereo offset values:" << result(0,2) << "," << result(1,2);
        
        // Calculate approximate FOV from the matrix
        float hFov = 2.0f * qAtan(1.0f / result(0,0));
        float vFov = 2.0f * qAtan(1.0f / result(1,1));
        qDebug() << "VROpenVRManager: Approximate FOV - H:" << qRadiansToDegrees(hFov) 
                 << "deg, V:" << qRadiansToDegrees(vFov) << "deg";
    }
    
    return result;
#else
    Q_UNUSED(leftEye);
    Q_UNUSED(nearPlane);
    Q_UNUSED(farPlane);
    Q_UNUSED(zoomFactor);
    return QMatrix4x4();
#endif
}

bool VROpenVRManager::submitFrame(uint32_t leftEyeTexture, uint32_t rightEyeTexture)
{
#ifdef USE_OPENVR
    // Validate pointers and VR state
    if (!m_vrCompositor || !m_vrSystem) {
        return false;
    }
    
    // Additional safety check - ensure VR is still connected
    if (!vr::VR_IsHmdPresent()) {
        qDebug() << "VROpenVRManager: HMD disconnected during frame submission";
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
        m_hmdPoseValid = false;
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
    
    // Method 1: Try the standard two-call approach
    vr::TrackedPropertyError error = vr::TrackedProp_Success;
    uint32_t requiredBufferLen = m_vrSystem->GetStringTrackedDeviceProperty(
        static_cast<vr::TrackedDeviceIndex_t>(device), 
        static_cast<vr::TrackedDeviceProperty>(prop), 
        nullptr, 0, &error);
    
    // If we got a valid buffer length, proceed with standard approach
    if (error == vr::TrackedProp_Success && requiredBufferLen > 0) {
        // Allocate buffer with extra space for null terminator
        uint32_t bufferSize = requiredBufferLen + 1;
        char* buffer = new char[bufferSize];
        memset(buffer, 0, bufferSize);
        
        error = vr::TrackedProp_Success;
        m_vrSystem->GetStringTrackedDeviceProperty(
            static_cast<vr::TrackedDeviceIndex_t>(device),
            static_cast<vr::TrackedDeviceProperty>(prop),
            buffer, requiredBufferLen, &error);
        
        if (error == vr::TrackedProp_Success) {
            QString result = QString::fromUtf8(buffer);
            delete[] buffer;
            return result;
        }
        delete[] buffer;
    }
    
    // Method 2: Fallback - try with a fixed-size buffer
    // Some drivers might have issues with the two-call approach
    const uint32_t fixedBufferSize = 256;  // Most property strings are well under this
    char fixedBuffer[fixedBufferSize];
    memset(fixedBuffer, 0, fixedBufferSize);
    
    error = vr::TrackedProp_Success;
    m_vrSystem->GetStringTrackedDeviceProperty(
        static_cast<vr::TrackedDeviceIndex_t>(device),
        static_cast<vr::TrackedDeviceProperty>(prop),
        fixedBuffer, fixedBufferSize, &error);
    
    if (error == vr::TrackedProp_Success) {
        return QString::fromUtf8(fixedBuffer);
    }
    
    // If both methods failed and it's not just "value not provided", log it
    if (error != vr::TrackedProp_ValueNotProvidedByDevice) {
        static QSet<QPair<uint32_t, uint32_t>> loggedErrors;
        QPair<uint32_t, uint32_t> errorKey(device, prop);
        if (!loggedErrors.contains(errorKey)) {
            loggedErrors.insert(errorKey);
            qDebug() << "VROpenVRManager: Failed to get property" << prop 
                     << "for device" << device << "error:" << error;
        }
    }
    
    return QString();
#else
    Q_UNUSED(device);
    Q_UNUSED(prop);
    return QString();
#endif
}

QVector3D VROpenVRManager::getHMDPosition() const
{
    return extractPosition(m_hmdPoseMatrix);
}

QMatrix4x4 VROpenVRManager::getHMDRotationMatrix() const
{
    return extractRotationMatrix(m_hmdPoseMatrix);
}

QMatrix4x4 VROpenVRManager::extractRotationMatrix(const QMatrix4x4& matrix)
{
    // Extract the 3x3 rotation part from the 4x4 matrix
    QMatrix4x4 rotation;
    rotation.setToIdentity();
    
    // Copy the rotation components (upper-left 3x3)
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            rotation(row, col) = matrix(row, col);
        }
    }
    
    return rotation;
}

QVector3D VROpenVRManager::extractPosition(const QMatrix4x4& matrix)
{
    // Extract position from the last column of the matrix
    return QVector3D(matrix(0, 3), matrix(1, 3), matrix(2, 3));
}

// Controller input implementation (Legacy Direct Input)
bool VROpenVRManager::initializeControllerInput()
{
    qDebug() << "VROpenVRManager: ========================================";
    qDebug() << "VROpenVRManager: Initializing LEGACY DIRECT INPUT system";
    qDebug() << "VROpenVRManager: Bypassing SteamVR binding system completely";
    qDebug() << "VROpenVRManager: ========================================";
    
    if (m_controllerInputReady) {
        qDebug() << "VROpenVRManager: Controller input already initialized";
        return true;
    }
    
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        qDebug() << "VROpenVRManager: VR system not initialized";
        return false;
    }
    
    // Initialize controller indices
    m_leftControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
    m_rightControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
    m_lastButtonPressed = 0;
    
    // Clear last controller states
    memset(&m_lastLeftControllerState, 0, sizeof(vr::VRControllerState_t));
    memset(&m_lastRightControllerState, 0, sizeof(vr::VRControllerState_t));
    
    // Try to detect controllers
    int controllersFound = tryDetectControllers();
    
    if (controllersFound == 0) {
        qDebug() << "VROpenVRManager: No VR controllers detected at startup";
        qDebug() << "VROpenVRManager: Controllers can be turned on later - hot-plug supported";
    }
    
    // Always mark as ready - we support hot-plug now
    m_controllerInputReady = true;
    
    qDebug() << "VROpenVRManager: ";
    qDebug() << "VROpenVRManager: BUTTON MAPPINGS (Legacy Direct Input):";
    qDebug() << "VROpenVRManager:   Trigger -> Recenter View (hold for continuous)";
    qDebug() << "VROpenVRManager:   Menu/Application Button -> Play/Pause";
    qDebug() << "VROpenVRManager:   Grip -> Modifier (hold for combinations)";
    qDebug() << "VROpenVRManager:   Grip + Trigger -> Reduce Playback Speed";
    qDebug() << "VROpenVRManager:   Grip + Menu -> Increase Playback Speed";
    qDebug() << "VROpenVRManager:   Trackpad/Joystick:";
    qDebug() << "VROpenVRManager:     - Normal: X=Seek 10s, Y=Zoom";
    qDebug() << "VROpenVRManager:     - With Grip: X=Seek 60s, Y=Volume";
    qDebug() << "VROpenVRManager: ";
    qDebug() << "VROpenVRManager: Legacy input ready - hot-plug enabled!";
    
    return true;
#else
    qDebug() << "VROpenVRManager: Controller input not available - OpenVR not compiled in";
    return false;
#endif
}

int VROpenVRManager::tryDetectControllers()
{
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return 0;
    }
    
    int controllersFound = 0;
    bool leftWasInvalid = (m_leftControllerIndex == vr::k_unTrackedDeviceIndexInvalid);
    bool rightWasInvalid = (m_rightControllerIndex == vr::k_unTrackedDeviceIndexInvalid);
    
    // Scan for controller devices
    for (vr::TrackedDeviceIndex_t device = 0; device < vr::k_unMaxTrackedDeviceCount; ++device) {
        if (!m_vrSystem->IsTrackedDeviceConnected(device)) {
            continue;
        }
        
        vr::ETrackedDeviceClass deviceClass = m_vrSystem->GetTrackedDeviceClass(device);
        if (deviceClass != vr::TrackedDeviceClass_Controller) {
            continue;
        }
        
        // Skip if we already have this device assigned
        if (device == m_leftControllerIndex || device == m_rightControllerIndex) {
            controllersFound++;
            continue;
        }
        
        vr::ETrackedControllerRole role = m_vrSystem->GetControllerRoleForTrackedDeviceIndex(device);
        
        // Get controller model name for debugging
        QString modelName = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_ModelNumber_String));
        QString controllerType = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_ControllerType_String));
        
        // Skip gamepads and non-VR controllers
        if (controllerType == "gamepad" || modelName.contains("XInput", Qt::CaseInsensitive)) {
            continue;
        }
        
        // Assign based on role or to first available slot
        if (role == vr::TrackedControllerRole_LeftHand && m_leftControllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
            m_leftControllerIndex = device;
            memset(&m_lastLeftControllerState, 0, sizeof(vr::VRControllerState_t));
            qDebug() << "VROpenVRManager: Found LEFT controller at index" << device << "Model:" << modelName;
            controllersFound++;
        } else if (role == vr::TrackedControllerRole_RightHand && m_rightControllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
            m_rightControllerIndex = device;
            memset(&m_lastRightControllerState, 0, sizeof(vr::VRControllerState_t));
            qDebug() << "VROpenVRManager: Found RIGHT controller at index" << device << "Model:" << modelName;
            controllersFound++;
        } else if (m_leftControllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
            m_leftControllerIndex = device;
            memset(&m_lastLeftControllerState, 0, sizeof(vr::VRControllerState_t));
            qDebug() << "VROpenVRManager: Found controller (assigned to LEFT) at index" << device << "Model:" << modelName;
            controllersFound++;
        } else if (m_rightControllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
            m_rightControllerIndex = device;
            memset(&m_lastRightControllerState, 0, sizeof(vr::VRControllerState_t));
            qDebug() << "VROpenVRManager: Found controller (assigned to RIGHT) at index" << device << "Model:" << modelName;
            controllersFound++;
        }
    }
    
    // Emit signal if we found new controllers
    bool leftNowValid = (m_leftControllerIndex != vr::k_unTrackedDeviceIndexInvalid);
    bool rightNowValid = (m_rightControllerIndex != vr::k_unTrackedDeviceIndexInvalid);
    
    if ((leftWasInvalid && leftNowValid) || (rightWasInvalid && rightNowValid)) {
        qDebug() << "VROpenVRManager: Controller(s) connected - total:" << controllersFound;
        emit controllerConnected(controllersFound);
    }
    
    return controllersFound;
#else
    return 0;
#endif
}

bool VROpenVRManager::hasValidController() const
{
#ifdef USE_OPENVR
    return m_leftControllerIndex != vr::k_unTrackedDeviceIndexInvalid ||
           m_rightControllerIndex != vr::k_unTrackedDeviceIndexInvalid;
#else
    return false;
#endif
}

void VROpenVRManager::shutdownControllerInput()
{
    if (!m_controllerInputReady) {
        return;
    }
    
    qDebug() << "VROpenVRManager: Shutting down legacy controller input system";
    
#ifdef USE_OPENVR
    m_leftControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
    m_rightControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
    m_lastButtonPressed = 0;
    memset(&m_lastLeftControllerState, 0, sizeof(vr::VRControllerState_t));
    memset(&m_lastRightControllerState, 0, sizeof(vr::VRControllerState_t));
#endif
    
    m_controllerInputReady = false;
    qDebug() << "VROpenVRManager: Legacy controller input shutdown complete";
}

VROpenVRManager::VRControllerState VROpenVRManager::pollControllerInput()
{
    VRControllerState state;
    
    if (!m_controllerInputReady) {
        return state; // Returns default state (all false/zero)
    }
    
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        return state;
    }
    
    // Hot-plug detection: if no valid controllers, try to find newly connected ones
    // We do this periodically (not every frame) to avoid performance impact
    static int hotPlugCheckCounter = 0;
    if (!hasValidController()) {
        if (++hotPlugCheckCounter >= 30) { // Check every ~0.5 seconds at 60Hz polling
            hotPlugCheckCounter = 0;
            int found = tryDetectControllers();
            if (found > 0) {
                qDebug() << "VROpenVRManager: Hot-plug detected" << found << "controller(s)";
            }
        }
        return state; // No controllers available yet
    }
    hotPlugCheckCounter = 0; // Reset counter when we have valid controllers
    
    // Button constants for different controller types
    // These are the raw button masks used by OpenVR
    static const uint64_t k_ButtonTrigger = (1ULL << 33);      // Trigger button
    static const uint64_t k_ButtonApplicationMenu = (1ULL << 1); // Menu button
    static const uint64_t k_ButtonGrip = (1ULL << 2);          // Grip button  
    static const uint64_t k_ButtonTouchpad = (1ULL << 32);     // Touchpad/Joystick button
    static const uint64_t k_TouchpadTouched = (1ULL << 32);    // Touchpad being touched
    
    // Track if we had controllers before this poll (for disconnect detection)
    bool hadLeftController = (m_leftControllerIndex != vr::k_unTrackedDeviceIndexInvalid);
    bool hadRightController = (m_rightControllerIndex != vr::k_unTrackedDeviceIndexInvalid);
    
    // Check both controllers
    vr::TrackedDeviceIndex_t controllers[2] = { m_leftControllerIndex, m_rightControllerIndex };
    
    for (int i = 0; i < 2; ++i) {
        vr::TrackedDeviceIndex_t device = controllers[i];
        if (device == vr::k_unTrackedDeviceIndexInvalid || device >= vr::k_unMaxTrackedDeviceCount) {
            continue;
        }
        
        // Validate controller is still connected
        if (!m_vrSystem->IsTrackedDeviceConnected(device)) {
            // Controller was disconnected - reset its index
            if (i == 0) {
                m_leftControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
            } else {
                m_rightControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
            }
            continue;
        }
        
        // Get the controller state
        vr::VRControllerState_t controllerState;
        if (!m_vrSystem->GetControllerState(device, &controllerState, sizeof(controllerState))) {
            continue;
        }
        
        // Get previous state for edge detection
        vr::VRControllerState_t& lastState = (i == 0) ? m_lastLeftControllerState : m_lastRightControllerState;
        
        // Detect button changes (rising edge)
        uint64_t buttonPressed = controllerState.ulButtonPressed;
        uint64_t buttonChanged = buttonPressed ^ lastState.ulButtonPressed;
        
        // GRIP -> Modifier (hold state) - Check this first for combinations
        bool gripHeld = (buttonPressed & k_ButtonGrip) != 0;
        if (gripHeld) {
            state.gripPressed = true;
            
            // Check for Grip + Trigger combination (Reduce Speed)
            if ((buttonChanged & k_ButtonTrigger) && (buttonPressed & k_ButtonTrigger)) {
                state.decreaseSpeedPressed = true;
                qDebug() << "VROpenVRManager: GRIP + TRIGGER PRESSED - Reduce Playback Speed";
            }
            
            // Check for Grip + Menu combination (Increase Speed)
            if ((buttonChanged & k_ButtonApplicationMenu) && (buttonPressed & k_ButtonApplicationMenu)) {
                state.increaseSpeedPressed = true;
                qDebug() << "VROpenVRManager: GRIP + MENU PRESSED - Increase Playback Speed";
            }
            
            static int gripLogCount = 0;
            if (++gripLogCount % 60 == 0) { // Log every second while held
                qDebug() << "VROpenVRManager: GRIP HELD - Modifier active (zoom/volume/speed)";
            }
        } else {
            // Only process individual button functions when grip is NOT held
            
            // MENU/APPLICATION -> Play/Pause (only when grip not held)
            if ((buttonChanged & k_ButtonApplicationMenu) && (buttonPressed & k_ButtonApplicationMenu)) {
                state.playPausePressed = true;
                qDebug() << "VROpenVRManager: MENU PRESSED - Play/Pause";
            }
            
            // TRIGGER -> Recenter (only when grip not held)
            if (buttonPressed & k_ButtonTrigger) {
                state.recenterHeld = true;
                static int triggerLogCount = 0;
                if (++triggerLogCount % 60 == 0) { // Log every second while held
                    qDebug() << "VROpenVRManager: TRIGGER HELD - Continuous recenter active";
                }
            }
        }
        
        // TRACKPAD/JOYSTICK -> Seek/Zoom axis (CLICK required for trackpads)
        bool trackpadActive = false;
        float xAxis = 0.0f;
        float yAxis = 0.0f;
        
        // For touchpad controllers (Vive, Windows MR) - require CLICK not just touch
        if (controllerState.ulButtonPressed & k_ButtonTouchpad) {
            trackpadActive = true;
            xAxis = controllerState.rAxis[0].x;  // Axis 0 is touchpad
            yAxis = controllerState.rAxis[0].y;
        }
        // For joystick controllers (Oculus, Index) - check if joystick is moved
        // Joysticks don't need to be clicked, just moved
        else if (qAbs(controllerState.rAxis[0].x) > 0.1f || qAbs(controllerState.rAxis[0].y) > 0.1f) {
            // Check if this is actually a joystick (not a trackpad)
            // Joystick controllers typically don't have the touchpad button
            bool hasTrackpadButton = (controllerState.ulButtonTouched & k_TouchpadTouched) || 
                                    (lastState.ulButtonTouched & k_TouchpadTouched);
            if (!hasTrackpadButton) {
                trackpadActive = true;
                xAxis = controllerState.rAxis[0].x;  // Axis 0 is also joystick
                yAxis = controllerState.rAxis[0].y;
            }
        }
        // Alternative axis check for some controllers
        else if (qAbs(controllerState.rAxis[2].x) > 0.1f || qAbs(controllerState.rAxis[2].y) > 0.1f) {
            trackpadActive = true;
            xAxis = controllerState.rAxis[2].x;  // Axis 2 might be joystick on some controllers
            yAxis = controllerState.rAxis[2].y;
        }
        
        if (trackpadActive) {
            state.seekAxis = QVector2D(xAxis, yAxis);
            
            static int axisLogCount = 0;
            if (++axisLogCount % 30 == 0 && (qAbs(xAxis) > 0.2f || qAbs(yAxis) > 0.2f)) {
                if (state.gripPressed) {
                    qDebug() << "VROpenVRManager: TRACKPAD/JOYSTICK - Zoom/Volume Y:" << yAxis;
                } else {
                    qDebug() << "VROpenVRManager: TRACKPAD/JOYSTICK - Seek X:" << xAxis;
                }
            }
        }
        
        // Debug output for all button states (periodic)
        static int debugCounter = 0;
        if (++debugCounter % 600 == 0) { // Every 10 seconds
            qDebug() << "VROpenVRManager: Controller" << i << "state:";
            qDebug() << "VROpenVRManager:   Buttons:" << QString::number(buttonPressed, 16);
            qDebug() << "VROpenVRManager:   Touched:" << QString::number(controllerState.ulButtonTouched, 16);
            qDebug() << "VROpenVRManager:   Axis0:" << controllerState.rAxis[0].x << "," << controllerState.rAxis[0].y;
            qDebug() << "VROpenVRManager:   Axis1:" << controllerState.rAxis[1].x << "," << controllerState.rAxis[1].y;
        }
        
        // Store current state for next frame
        lastState = controllerState;
    }
    
    // Emit disconnect signal if we lost all controllers
    bool hasLeftNow = (m_leftControllerIndex != vr::k_unTrackedDeviceIndexInvalid);
    bool hasRightNow = (m_rightControllerIndex != vr::k_unTrackedDeviceIndexInvalid);
    
    if ((hadLeftController || hadRightController) && !hasLeftNow && !hasRightNow) {
        qDebug() << "VROpenVRManager: All controllers disconnected";
        emit controllerDisconnected();
    }
    
#endif
    
    return state;
}
