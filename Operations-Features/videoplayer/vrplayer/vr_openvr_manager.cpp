#include "vr_openvr_manager.h"
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QtMath>
#include <QStandardPaths>
#include <QDir>

VROpenVRManager::VROpenVRManager(QObject *parent)
    : QObject(parent)
    , m_vrSystem(nullptr)
    , m_vrCompositor(nullptr)
    , m_isInitialized(false)
    , m_status(VRStatus::NotInitialized)
    , m_hmdPoseValid(false)
#ifdef USE_OPENVR
    , m_actionSetVideo(vr::k_ulInvalidActionSetHandle)
    , m_actionRecenter(vr::k_ulInvalidActionHandle)
    , m_actionPlayPause(vr::k_ulInvalidActionHandle)
    , m_actionSeekAxis(vr::k_ulInvalidActionHandle)
    , m_actionGripModifier(vr::k_ulInvalidActionHandle)
#else
    , m_actionSetVideo(0)
    , m_actionRecenter(0)
    , m_actionPlayPause(0)
    , m_actionSeekAxis(0)
    , m_actionGripModifier(0)
#endif
    , m_controllerInputReady(false)
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

// Controller input implementation
bool VROpenVRManager::initializeControllerInput()
{
    qDebug() << "VROpenVRManager: Initializing controller input system";
    
    if (m_controllerInputReady) {
        qDebug() << "VROpenVRManager: Controller input already initialized";
        return true;
    }
    
#ifdef USE_OPENVR
    if (!m_vrSystem) {
        qDebug() << "VROpenVRManager: VR system not initialized";
        return false;
    }
    
    // Extract action manifest from resources and save to temp location
    QString manifestResourcePath = ":/Operations-Features/videoplayer/vrplayer/vr_action_manifest.json";
    qDebug() << "VROpenVRManager: Looking for action manifest at resource path:" << manifestResourcePath;
    
    QFile manifestResource(manifestResourcePath);
    if (!manifestResource.open(QIODevice::ReadOnly)) {
        qDebug() << "VROpenVRManager: Failed to open action manifest resource";
        qDebug() << "VROpenVRManager: Resource exists:" << QFile::exists(manifestResourcePath);
        setLastError("Failed to load VR controller action manifest");
        return false;
    }
    
    qDebug() << "VROpenVRManager: Successfully opened action manifest resource (" << manifestResource.size() << "bytes)";
    
    // Create temp directory for action manifest
    QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir userDataDir(userDataPath);
    if (!userDataDir.exists()) {
        userDataDir.mkpath(".");
    }
    
    QString tempDir = userDataPath + "/temp";
    QDir tempDirObj(tempDir);
    if (!tempDirObj.exists()) {
        tempDirObj.mkpath(".");
    }
    
    QString manifestPath = tempDir + "/vr_action_manifest.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        qDebug() << "VROpenVRManager: Failed to create temp action manifest file";
        setLastError("Failed to create temporary VR controller action manifest");
        return false;
    }
    
    manifestFile.write(manifestResource.readAll());
    manifestFile.close();
    manifestResource.close();
    
    qDebug() << "VROpenVRManager: Action manifest written to:" << manifestPath;
    
    // Set action manifest path
    vr::EVRInputError inputError = vr::VRInput()->SetActionManifestPath(manifestPath.toLocal8Bit().constData());
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to set action manifest path, error:" << inputError;
        setLastError("Failed to set VR controller action manifest path");
        return false;
    }
    
    // Get action set handle
    inputError = vr::VRInput()->GetActionSetHandle("/actions/video", &m_actionSetVideo);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get action set handle, error:" << inputError;
        setLastError("Failed to get VR controller action set handle");
        return false;
    }
    
    // Get individual action handles
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/recenter", &m_actionRecenter);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get recenter action handle, error:" << inputError;
        setLastError("Failed to get VR controller recenter action handle");
        return false;
    }
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/play_pause", &m_actionPlayPause);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get play/pause action handle, error:" << inputError;
        setLastError("Failed to get VR controller play/pause action handle");
        return false;
    }
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/seek_axis", &m_actionSeekAxis);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get seek axis action handle, error:" << inputError;
        setLastError("Failed to get VR controller seek axis action handle");
        return false;
    }
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/grip_modifier", &m_actionGripModifier);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get grip modifier action handle, error:" << inputError;
        setLastError("Failed to get VR controller grip modifier action handle");
        return false;
    }
    
    m_controllerInputReady = true;
    qDebug() << "VROpenVRManager: Controller input system initialized successfully";
    return true;
#else
    qDebug() << "VROpenVRManager: Controller input not available - OpenVR not compiled in";
    return false;
#endif
}

void VROpenVRManager::shutdownControllerInput()
{
    if (!m_controllerInputReady) {
        return;
    }
    
    qDebug() << "VROpenVRManager: Shutting down controller input system";
    
#ifdef USE_OPENVR
    m_actionSetVideo = vr::k_ulInvalidActionSetHandle;
    m_actionRecenter = vr::k_ulInvalidActionHandle;
    m_actionPlayPause = vr::k_ulInvalidActionHandle;
    m_actionSeekAxis = vr::k_ulInvalidActionHandle;
    m_actionGripModifier = vr::k_ulInvalidActionHandle;
#else
    m_actionSetVideo = 0;
    m_actionRecenter = 0;
    m_actionPlayPause = 0;
    m_actionSeekAxis = 0;
    m_actionGripModifier = 0;
#endif
    
    m_controllerInputReady = false;
    qDebug() << "VROpenVRManager: Controller input shutdown complete";
}

VROpenVRManager::VRControllerState VROpenVRManager::pollControllerInput()
{
    VRControllerState state;
    
    if (!m_controllerInputReady) {
        return state; // Returns default state (all false/zero)
    }
    
#ifdef USE_OPENVR
    // Update action states
    vr::VRActiveActionSet_t actionSet = { 0 };
    actionSet.ulActionSet = m_actionSetVideo;
    vr::VRInput()->UpdateActionState(&actionSet, sizeof(vr::VRActiveActionSet_t), 1);
    
    // Poll boolean actions
    vr::InputDigitalActionData_t recenterData;
    if (vr::VRInput()->GetDigitalActionData(m_actionRecenter, &recenterData, sizeof(recenterData), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None) {
        state.recenterPressed = recenterData.bActive && recenterData.bState && recenterData.bChanged;
    }
    
    vr::InputDigitalActionData_t playPauseData;
    if (vr::VRInput()->GetDigitalActionData(m_actionPlayPause, &playPauseData, sizeof(playPauseData), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None) {
        state.playPausePressed = playPauseData.bActive && playPauseData.bState && playPauseData.bChanged;
    }
    
    vr::InputDigitalActionData_t gripData;
    if (vr::VRInput()->GetDigitalActionData(m_actionGripModifier, &gripData, sizeof(gripData), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None) {
        state.gripPressed = gripData.bActive && gripData.bState;
    }
    
    // Poll axis action (touchpad/joystick)
    vr::InputAnalogActionData_t axisData;
    if (vr::VRInput()->GetAnalogActionData(m_actionSeekAxis, &axisData, sizeof(axisData), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None) {
        if (axisData.bActive) {
            state.seekAxis = QVector2D(axisData.x, axisData.y);
        }
    }
    
#endif
    
    return state;
}
