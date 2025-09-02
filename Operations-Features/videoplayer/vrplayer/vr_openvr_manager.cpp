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
    
    // Update device poses before querying properties
    qDebug() << "VROpenVRManager: Updating device poses before controller detection...";
    compositorWaitGetPoses();
    
    // Poll for VR events to ensure devices are activated
    qDebug() << "VROpenVRManager: Polling for VR events...";
    vr::VREvent_t vrEvent;
    while (m_vrSystem->PollNextEvent(&vrEvent, sizeof(vrEvent))) {
        if (vrEvent.eventType == vr::VREvent_TrackedDeviceActivated ||
            vrEvent.eventType == vr::VREvent_TrackedDeviceUpdated) {
            qDebug() << "VROpenVRManager: Device" << vrEvent.trackedDeviceIndex << "activated/updated";
        }
    }
    
    // Add a small delay to ensure devices are ready
    QThread::msleep(100);
    
    // Create temp directory for action manifest and binding files
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
    
    qDebug() << "VROpenVRManager: Using temp directory:" << tempDir;
    
    // List of all binding files to extract
    QStringList bindingFiles = {
        "vr_action_bindings_vive_controller.json",
        "vr_action_bindings_knuckles.json",
        "vr_action_bindings_oculus_touch.json",
        "vr_action_bindings_generic.json"
    };
    
    // Extract all binding files first
    for (const QString& bindingFile : bindingFiles) {
        QString bindingResourcePath = ":/Operations-Features/videoplayer/vrplayer/" + bindingFile;
        QString bindingDestPath = tempDir + "/" + bindingFile;
        
        qDebug() << "VROpenVRManager: Extracting binding file:" << bindingFile;
        
        QFile bindingResource(bindingResourcePath);
        if (!bindingResource.open(QIODevice::ReadOnly)) {
            qDebug() << "VROpenVRManager: WARNING - Failed to open binding resource:" << bindingResourcePath;
            continue;
        }
        
        QFile bindingDest(bindingDestPath);
        if (!bindingDest.open(QIODevice::WriteOnly)) {
            qDebug() << "VROpenVRManager: WARNING - Failed to create binding file:" << bindingDestPath;
            bindingResource.close();
            continue;
        }
        
        bindingDest.write(bindingResource.readAll());
        bindingDest.close();
        bindingResource.close();
        
        qDebug() << "VROpenVRManager: Successfully extracted binding file to:" << bindingDestPath;
    }
    
    // Extract action manifest from resources and modify it with absolute paths
    QString manifestResourcePath = ":/Operations-Features/videoplayer/vrplayer/vr_action_manifest.json";
    qDebug() << "VROpenVRManager: Extracting action manifest from:" << manifestResourcePath;
    
    QFile manifestResource(manifestResourcePath);
    if (!manifestResource.open(QIODevice::ReadOnly)) {
        qDebug() << "VROpenVRManager: Failed to open action manifest resource";
        qDebug() << "VROpenVRManager: Resource exists:" << QFile::exists(manifestResourcePath);
        setLastError("Failed to load VR controller action manifest");
        return false;
    }
    
    // Read the manifest content
    QByteArray manifestContent = manifestResource.readAll();
    manifestResource.close();
    
    qDebug() << "VROpenVRManager: Successfully loaded action manifest (" << manifestContent.size() << "bytes)";
    
    // Convert to string for modification
    QString manifestString = QString::fromUtf8(manifestContent);
    
    // Replace relative paths with absolute paths in the manifest
    // The manifest contains binding_url entries like "vr_action_bindings_vive_controller.json"
    // We need to replace them with absolute paths
    for (const QString& bindingFile : bindingFiles) {
        QString relativePath = bindingFile;
        QString absolutePath = tempDir + "/" + bindingFile;
        
        // Replace in the manifest - use proper file URL format
        absolutePath.replace("\\", "/");  // Convert Windows backslashes to forward slashes for JSON
        
        // Add file:/// prefix for proper URL format
        QString fileUrl = "file:///" + absolutePath;
        
        // Replace the relative binding URL with absolute file URL
        manifestString.replace(
            QString("\"%1\"").arg(relativePath),
            QString("\"%1\"").arg(fileUrl)
        );
        
        qDebug() << "VROpenVRManager: Replaced binding path:" << relativePath << "->" << fileUrl;
    }
    
    // Write the modified manifest
    QString manifestPath = tempDir + "/vr_action_manifest.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        qDebug() << "VROpenVRManager: Failed to create temp action manifest file";
        setLastError("Failed to create temporary VR controller action manifest");
        return false;
    }
    
    manifestFile.write(manifestString.toUtf8());
    manifestFile.close();
    
    qDebug() << "VROpenVRManager: Modified action manifest written to:" << manifestPath;
    
    // Debug: Show a sample of the modified manifest to verify paths
    if (manifestString.contains("vive_controller")) {
        int startIdx = manifestString.indexOf("vive_controller") - 50;
        int endIdx = manifestString.indexOf("vive_controller") + 150;
        startIdx = qMax(0, startIdx);
        endIdx = qMin(manifestString.length(), endIdx);
        qDebug() << "VROpenVRManager: Sample of modified manifest:" << manifestString.mid(startIdx, endIdx - startIdx);
    }
    
    // Verify all files were extracted
    qDebug() << "VROpenVRManager: Verifying extracted files:";
    QDir extractedDir(tempDir);
    QStringList extractedFiles = extractedDir.entryList(QStringList() << "*.json", QDir::Files);
    for (const QString& file : extractedFiles) {
        qDebug() << "VROpenVRManager:   Found:" << file;
    }
    
    // Set action manifest path
    qDebug() << "VROpenVRManager: Setting action manifest path to OpenVR:" << manifestPath;
    vr::EVRInputError inputError = vr::VRInput()->SetActionManifestPath(manifestPath.toLocal8Bit().constData());
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to set action manifest path, error code:" << inputError;
        
        // Provide more detailed error information
        switch(inputError) {
            case vr::VRInputError_InvalidParam:
                qDebug() << "VROpenVRManager: Error: Invalid parameter";
                break;
            case vr::VRInputError_InvalidDevice:
                qDebug() << "VROpenVRManager: Error: Invalid device";
                break;
            case vr::VRInputError_MismatchedActionManifest:
                qDebug() << "VROpenVRManager: Error: Mismatched action manifest";
                break;
            case vr::VRInputError_InvalidHandle:
                qDebug() << "VROpenVRManager: Error: Invalid handle";
                break;
            case vr::VRInputError_NameNotFound:
                qDebug() << "VROpenVRManager: Error: Name not found in manifest";
                break;
            case vr::VRInputError_WrongType:
                qDebug() << "VROpenVRManager: Error: Wrong type";
                break;
            case vr::VRInputError_NoData:
                qDebug() << "VROpenVRManager: Error: No data available";
                break;
            case vr::VRInputError_BufferTooSmall:
                qDebug() << "VROpenVRManager: Error: Buffer too small";
                break;
            default:
                qDebug() << "VROpenVRManager: Unknown error code:" << inputError;
                break;
        }
        
        setLastError("Failed to set VR controller action manifest path");
        return false;
    }
    
    qDebug() << "VROpenVRManager: Action manifest successfully loaded by OpenVR";
    
    // Get action set handle
    qDebug() << "VROpenVRManager: Getting action set handle for /actions/video";
    inputError = vr::VRInput()->GetActionSetHandle("/actions/video", &m_actionSetVideo);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get action set handle, error:" << inputError;
        setLastError("Failed to get VR controller action set handle");
        return false;
    }
    qDebug() << "VROpenVRManager: Action set handle obtained:" << m_actionSetVideo;
    
    // Get individual action handles
    qDebug() << "VROpenVRManager: Getting individual action handles...";
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/recenter", &m_actionRecenter);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get recenter action handle, error:" << inputError;
        setLastError("Failed to get VR controller recenter action handle");
        return false;
    }
    qDebug() << "VROpenVRManager:   Recenter action handle:" << m_actionRecenter;
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/play_pause", &m_actionPlayPause);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get play/pause action handle, error:" << inputError;
        setLastError("Failed to get VR controller play/pause action handle");
        return false;
    }
    qDebug() << "VROpenVRManager:   Play/Pause action handle:" << m_actionPlayPause;
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/seek_axis", &m_actionSeekAxis);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get seek axis action handle, error:" << inputError;
        setLastError("Failed to get VR controller seek axis action handle");
        return false;
    }
    qDebug() << "VROpenVRManager:   Seek axis action handle:" << m_actionSeekAxis;
    
    inputError = vr::VRInput()->GetActionHandle("/actions/video/in/grip_modifier", &m_actionGripModifier);
    if (inputError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to get grip modifier action handle, error:" << inputError;
        setLastError("Failed to get VR controller grip modifier action handle");
        return false;
    }
    qDebug() << "VROpenVRManager:   Grip modifier action handle:" << m_actionGripModifier;
    
    // Detect connected controller types with retry logic
    qDebug() << "VROpenVRManager: Detecting connected controllers...";
    bool hasViveController = false;
    bool hasKnuckles = false;
    bool hasOculusTouch = false;
    int controllerCount = 0;
    
    // Retry controller detection up to 3 times with delays
    for (int retryCount = 0; retryCount < 3; ++retryCount) {
        if (retryCount > 0) {
            qDebug() << "VROpenVRManager: Retry" << retryCount << "for controller detection...";
            QThread::msleep(500);  // Wait 500ms between retries
            compositorWaitGetPoses();  // Update poses again
        }
        
        controllerCount = 0;
        hasViveController = false;
        hasKnuckles = false;
        hasOculusTouch = false;
        
        for (uint32_t device = 0; device < vr::k_unMaxTrackedDeviceCount; ++device) {
            if (m_vrSystem->GetTrackedDeviceClass(device) == vr::TrackedDeviceClass_Controller) {
                // Get the controller properties first to check if it's a real VR controller
                QString modelName = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_ModelNumber_String));
                QString renderModelName = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_RenderModelName_String));
                QString controllerType = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_ControllerType_String));
                QString manufacturer = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_ManufacturerName_String));
                QString trackingSystem = getTrackedDeviceString(device, static_cast<uint32_t>(vr::Prop_TrackingSystemName_String));
                
                // Skip non-VR controllers like gamepads
                if (controllerType == "gamepad" || 
                    trackingSystem == "gamepad" || 
                    renderModelName == "generic_controller" ||
                    modelName.contains("XInput", Qt::CaseInsensitive)) {
                    qDebug() << "VROpenVRManager:   Skipping non-VR controller at index" << device;
                    qDebug() << "VROpenVRManager:     Type:" << controllerType << "Model:" << modelName;
                    continue;  // Skip this device
                }
                
                // This is a real VR controller, count it
                controllerCount++;
                
                qDebug() << "VROpenVRManager:   Controller" << controllerCount << "found at index" << device;
                qDebug() << "VROpenVRManager:     Model:" << modelName;
                qDebug() << "VROpenVRManager:     Render Model:" << renderModelName;
                qDebug() << "VROpenVRManager:     Controller Type:" << controllerType;
                qDebug() << "VROpenVRManager:     Manufacturer:" << manufacturer;
                qDebug() << "VROpenVRManager:     Tracking System:" << trackingSystem;
                
                // Note unusual device indices which might indicate mixed setups
                if (device > 10) {
                    qDebug() << "VROpenVRManager:     WARNING: Unusually high device index" << device;
                    qDebug() << "VROpenVRManager:     This might indicate virtual devices, base stations, or mixed VR setup";
                }
            
                // Detect controller type - check all available properties
                if (!controllerType.isEmpty()) {
                    // Controller type is properly detected
                    if (controllerType.contains("vive", Qt::CaseInsensitive)) {
                        hasViveController = true;
                        qDebug() << "VROpenVRManager:     -> Detected as HTC Vive Controller (by type)";
                    } else if (controllerType.contains("knuckles", Qt::CaseInsensitive)) {
                        hasKnuckles = true;
                        qDebug() << "VROpenVRManager:     -> Detected as Valve Index Controller (by type)";
                    } else if (controllerType.contains("oculus", Qt::CaseInsensitive)) {
                        hasOculusTouch = true;
                        qDebug() << "VROpenVRManager:     -> Detected as Oculus Touch Controller (by type)";
                    }
                } else {
                    // Fallback detection using other properties
                    qDebug() << "VROpenVRManager:     WARNING: Controller type is empty, using fallback detection";
                    
                    if (renderModelName.contains("vive", Qt::CaseInsensitive) || 
                        modelName.contains("vive", Qt::CaseInsensitive) ||
                        manufacturer.contains("htc", Qt::CaseInsensitive) ||
                        trackingSystem.contains("lighthouse", Qt::CaseInsensitive)) {
                        hasViveController = true;
                        qDebug() << "VROpenVRManager:     -> Detected as HTC Vive Controller (by fallback)";
                    } else if (renderModelName.contains("knuckles", Qt::CaseInsensitive) || 
                               modelName.contains("index", Qt::CaseInsensitive) ||
                               manufacturer.contains("valve", Qt::CaseInsensitive)) {
                        hasKnuckles = true;
                        qDebug() << "VROpenVRManager:     -> Detected as Valve Index Controller (by fallback)";
                    } else if (renderModelName.contains("oculus", Qt::CaseInsensitive) || 
                               modelName.contains("quest", Qt::CaseInsensitive) ||
                               manufacturer.contains("oculus", Qt::CaseInsensitive) ||
                               manufacturer.contains("meta", Qt::CaseInsensitive)) {
                        hasOculusTouch = true;
                        qDebug() << "VROpenVRManager:     -> Detected as Oculus Touch Controller (by fallback)";
                    } else {
                        qDebug() << "VROpenVRManager:     -> Could not determine controller type";
                    }
                }
            }
        }
        
        // If we successfully detected controllers with types, break out of retry loop
        if (controllerCount > 0 && (hasViveController || hasKnuckles || hasOculusTouch)) {
            qDebug() << "VROpenVRManager: Successfully detected controller types after" << (retryCount + 1) << "attempt(s)";
            break;
        }
    }
    
    if (controllerCount == 0) {
        qDebug() << "VROpenVRManager: WARNING - No controllers detected!";
        qDebug() << "VROpenVRManager: Please turn on your controllers and try again";
    } else {
        qDebug() << "VROpenVRManager: Total controllers found:" << controllerCount;
    }
    
    // Trigger initial action set update to ensure bindings are loaded
    qDebug() << "VROpenVRManager: Triggering initial action set update...";
    vr::VRActiveActionSet_t actionSet = { 0 };
    actionSet.ulActionSet = m_actionSetVideo;
    actionSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
    actionSet.nPriority = 0;
    
    vr::EVRInputError updateError = vr::VRInput()->UpdateActionState(&actionSet, sizeof(vr::VRActiveActionSet_t), 1);
    if (updateError != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: WARNING - Initial UpdateActionState failed with error:" << updateError;
    } else {
        qDebug() << "VROpenVRManager: Initial action set update successful";
    }
    
    // Check binding configuration by testing if any action is active
    qDebug() << "VROpenVRManager: Checking if actions are bound to controllers...";
    
    // Test if recenter action has any bindings
    vr::InputDigitalActionData_t testData;
    vr::EVRInputError testError = vr::VRInput()->GetDigitalActionData(m_actionRecenter, &testData, sizeof(testData), vr::k_ulInvalidInputValueHandle);
    if (testError == vr::VRInputError_None) {
        if (!testData.bActive) {
            qDebug() << "VROpenVRManager: WARNING - Recenter action is not active (no bindings)";
        } else {
            qDebug() << "VROpenVRManager: Recenter action has active bindings";
        }
    }
    
    // Check binding status and attempt automatic configuration
    if (controllerCount > 0 && !testData.bActive) {
        qDebug() << "VROpenVRManager: ========================================";
        qDebug() << "VROpenVRManager: Controller bindings not active for detected VR controllers";
        qDebug() << "VROpenVRManager: Detected controller types:";
        if (hasViveController) qDebug() << "VROpenVRManager:   - vive_controller";
        if (hasKnuckles) qDebug() << "VROpenVRManager:   - knuckles";
        if (hasOculusTouch) qDebug() << "VROpenVRManager:   - oculus_touch";
        qDebug() << "VROpenVRManager: Attempting automatic configuration...";
        
        // Try to reload the manifest to force binding activation
        qDebug() << "VROpenVRManager: Reloading action manifest to activate bindings...";
        
        // Clear and reload the action manifest
        inputError = vr::VRInput()->SetActionManifestPath(manifestPath.toLocal8Bit().constData());
        if (inputError != vr::VRInputError_None) {
            qDebug() << "VROpenVRManager: Failed to reload action manifest, error:" << inputError;
        } else {
            qDebug() << "VROpenVRManager: Action manifest reloaded successfully";
            
            // Add a delay to let SteamVR process the manifest
            qDebug() << "VROpenVRManager: Waiting for SteamVR to process manifest...";
            QThread::msleep(500);
            
            // Re-get action handles after reload (they might have changed)
            vr::VRActionSetHandle_t newActionSetVideo;
            inputError = vr::VRInput()->GetActionSetHandle("/actions/video", &newActionSetVideo);
            if (inputError == vr::VRInputError_None) {
                m_actionSetVideo = newActionSetVideo;
                qDebug() << "VROpenVRManager: Re-obtained action set handle:" << m_actionSetVideo;
            }
            
            // Re-get all action handles
            vr::VRInput()->GetActionHandle("/actions/video/in/recenter", &m_actionRecenter);
            vr::VRInput()->GetActionHandle("/actions/video/in/play_pause", &m_actionPlayPause);
            vr::VRInput()->GetActionHandle("/actions/video/in/seek_axis", &m_actionSeekAxis);
            vr::VRInput()->GetActionHandle("/actions/video/in/grip_modifier", &m_actionGripModifier);
            
            // Force an action state update after reload
            vr::VRActiveActionSet_t reloadActionSet = { 0 };
            reloadActionSet.ulActionSet = m_actionSetVideo;
            reloadActionSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
            reloadActionSet.nPriority = 0;
            
            vr::EVRInputError reloadUpdateError = vr::VRInput()->UpdateActionState(&reloadActionSet, sizeof(vr::VRActiveActionSet_t), 1);
            if (reloadUpdateError == vr::VRInputError_None) {
                qDebug() << "VROpenVRManager: Action state updated after reload";
            }
            
            // Add another delay after update
            QThread::msleep(200);
            
            // Test again after reload and update with multiple attempts
            bool bindingsActive = false;
            for (int testAttempt = 0; testAttempt < 3; ++testAttempt) {
                if (testAttempt > 0) {
                    QThread::msleep(500);
                    // Update action state again
                    vr::VRInput()->UpdateActionState(&reloadActionSet, sizeof(vr::VRActiveActionSet_t), 1);
                }
                
                vr::InputDigitalActionData_t retestData;
                vr::EVRInputError retestError = vr::VRInput()->GetDigitalActionData(m_actionRecenter, &retestData, sizeof(retestData), vr::k_ulInvalidInputValueHandle);
                if (retestError == vr::VRInputError_None && retestData.bActive) {
                    qDebug() << "VROpenVRManager: SUCCESS! Bindings are now active after reload (attempt" << (testAttempt + 1) << ")";
                    qDebug() << "VROpenVRManager: Controller is ready to use!";
                    bindingsActive = true;
                    break;
                }
                qDebug() << "VROpenVRManager: Binding test attempt" << (testAttempt + 1) << "- Active:" << retestData.bActive;
            }
            
            if (!bindingsActive) {
                qDebug() << "VROpenVRManager: Bindings still not active after reload";
                qDebug() << "VROpenVRManager: ";
                qDebug() << "VROpenVRManager: Attempting to automatically open binding configuration UI...";
                
                // Try to automatically open the binding configuration UI
                // We need to temporarily set m_controllerInputReady to true for this to work
                m_controllerInputReady = true;
                showBindingConfiguration();
                m_controllerInputReady = false;  // Set it back since bindings aren't actually ready
                
                qDebug() << "VROpenVRManager: ";
                qDebug() << "VROpenVRManager: MANUAL CONFIGURATION REQUIRED:";
                qDebug() << "VROpenVRManager: 1. The binding configuration UI should now be open";
                qDebug() << "VROpenVRManager: 2. Select 'MMDiary VR Video Player' from the application list";
                qDebug() << "VROpenVRManager: 3. Choose the default binding for your controller";
                qDebug() << "VROpenVRManager: 4. Click 'Replace Default Binding' if prompted";
                qDebug() << "VROpenVRManager: 5. Close the configuration and restart the VR player";
                qDebug() << "VROpenVRManager: ";
                qDebug() << "VROpenVRManager: Button mappings:";
                qDebug() << "VROpenVRManager:    - Trigger -> Recenter View";
                qDebug() << "VROpenVRManager:    - Menu Button -> Play/Pause";
                qDebug() << "VROpenVRManager:    - Grip -> Modifier (for zoom)";
                qDebug() << "VROpenVRManager:    - Trackpad -> Seek/Zoom Control";
            }
        }
        qDebug() << "VROpenVRManager: ========================================";
    } else if (controllerCount > 0 && testData.bActive) {
        qDebug() << "VROpenVRManager: Controller bindings are active and ready!";
        qDebug() << "VROpenVRManager: Button mappings:";
        qDebug() << "VROpenVRManager:    - Trigger: Recenter View";
        qDebug() << "VROpenVRManager:    - Menu Button: Play/Pause";
        qDebug() << "VROpenVRManager:    - Grip: Hold for zoom modifier";
        qDebug() << "VROpenVRManager:    - Trackpad: Seek (X-axis) / Zoom (Y-axis with Grip)";
    }
    
    m_controllerInputReady = true;
    qDebug() << "VROpenVRManager: Controller input system initialized successfully";
    
    // Log summary
    qDebug() << "VROpenVRManager: ======== CONTROLLER SUMMARY ========";
    qDebug() << "VROpenVRManager: VR Controllers detected:" << controllerCount;
    if (hasViveController) qDebug() << "VROpenVRManager:   - HTC Vive Controller";
    if (hasKnuckles) qDebug() << "VROpenVRManager:   - Valve Index Controller";
    if (hasOculusTouch) qDebug() << "VROpenVRManager:   - Oculus Touch Controller";
    qDebug() << "VROpenVRManager: =====================================";
    
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
    actionSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle; // Apply to all devices
    actionSet.nPriority = 0;
    
    vr::EVRInputError updateError = vr::VRInput()->UpdateActionState(&actionSet, sizeof(vr::VRActiveActionSet_t), 1);
    if (updateError != vr::VRInputError_None) {
        static int errorLogCount = 0;
        if (++errorLogCount % 300 == 0) { // Log every 5 seconds at 60Hz
            qDebug() << "VROpenVRManager: UpdateActionState error:" << updateError;
            qDebug() << "VROpenVRManager:   ActionSet handle:" << m_actionSetVideo;
        }
        return state;
    }
    
    // Poll boolean actions with detailed debugging
    vr::InputDigitalActionData_t recenterData;
    vr::EVRInputError recenterError = vr::VRInput()->GetDigitalActionData(m_actionRecenter, &recenterData, sizeof(recenterData), vr::k_ulInvalidInputValueHandle);
    if (recenterError == vr::VRInputError_None) {
        // Check for any activity on this action
        if (recenterData.bActive) {
            state.recenterPressed = recenterData.bState && recenterData.bChanged;
            
            // Log when state changes
            if (recenterData.bChanged) {
                qDebug() << "VROpenVRManager: Recenter button state changed - Pressed:" << recenterData.bState;
            }
        }
        
        static int debugCount = 0;
        if (++debugCount % 600 == 0) { // Debug every 10 seconds
            qDebug() << "VROpenVRManager: Recenter - Active:" << recenterData.bActive 
                     << "State:" << recenterData.bState 
                     << "Changed:" << recenterData.bChanged
                     << "Handle:" << m_actionRecenter;
            
            // If not active after 10 seconds, show additional help
            static bool shownHelp = false;
            if (!recenterData.bActive && !shownHelp) {
                shownHelp = true;
                qDebug() << "VROpenVRManager: ========================================";
                qDebug() << "VROpenVRManager: CONTROLLER BINDINGS NOT ACTIVE!";
                qDebug() << "VROpenVRManager: To fix this:";
                qDebug() << "VROpenVRManager: 1. Right-click on SteamVR status window";
                qDebug() << "VROpenVRManager: 2. Select 'Settings' -> 'Controllers' -> 'Manage Controller Bindings'";
                qDebug() << "VROpenVRManager: 3. Find 'MMDiary VR Video Player' in the application dropdown";
                qDebug() << "VROpenVRManager: 4. Select a binding configuration or create a custom one";
                qDebug() << "VROpenVRManager: 5. Click 'Replace Default Binding' if prompted";
                qDebug() << "VROpenVRManager: ========================================";
            }
        }
        
        if (state.recenterPressed) {
            qDebug() << "VROpenVRManager: RECENTER PRESSED!";
        }
    } else {
        static int recenterErrorCount = 0;
        if (++recenterErrorCount % 300 == 0) {
            qDebug() << "VROpenVRManager: Recenter action error:" << recenterError << "Handle:" << m_actionRecenter;
        }
    }
    
    vr::InputDigitalActionData_t playPauseData;
    vr::EVRInputError playPauseError = vr::VRInput()->GetDigitalActionData(m_actionPlayPause, &playPauseData, sizeof(playPauseData), vr::k_ulInvalidInputValueHandle);
    if (playPauseError == vr::VRInputError_None) {
        state.playPausePressed = playPauseData.bActive && playPauseData.bState && playPauseData.bChanged;
        
        static int playPauseDebugCount = 0;
        if (++playPauseDebugCount % 300 == 0) {
            qDebug() << "VROpenVRManager: PlayPause - Active:" << playPauseData.bActive << "State:" << playPauseData.bState << "Changed:" << playPauseData.bChanged;
        }
        
        if (state.playPausePressed) {
            qDebug() << "VROpenVRManager: PLAY/PAUSE PRESSED!";
        }
    } else {
        static int playPauseErrorCount = 0;
        if (++playPauseErrorCount % 300 == 0) {
            qDebug() << "VROpenVRManager: PlayPause action error:" << playPauseError;
        }
    }
    
    vr::InputDigitalActionData_t gripData;
    vr::EVRInputError gripError = vr::VRInput()->GetDigitalActionData(m_actionGripModifier, &gripData, sizeof(gripData), vr::k_ulInvalidInputValueHandle);
    if (gripError == vr::VRInputError_None) {
        // Check for any activity on this action
        if (gripData.bActive) {
            state.gripPressed = gripData.bState;
            
            // Log when state changes
            static bool lastGripState = false;
            if (gripData.bState != lastGripState) {
                qDebug() << "VROpenVRManager: Grip button state changed - Pressed:" << gripData.bState;
                lastGripState = gripData.bState;
            }
        }
        
        static int gripDebugCount = 0;
        if (++gripDebugCount % 600 == 0) { // Debug every 10 seconds
            qDebug() << "VROpenVRManager: Grip - Active:" << gripData.bActive 
                     << "State:" << gripData.bState
                     << "Changed:" << gripData.bChanged
                     << "Handle:" << m_actionGripModifier;
        }
        
        if (state.gripPressed) {
            static int gripPressedCount = 0;
            if (++gripPressedCount % 60 == 0) { // Log every second when pressed
                qDebug() << "VROpenVRManager: GRIP IS BEING HELD!";
            }
        }
    } else {
        static int gripErrorCount = 0;
        if (++gripErrorCount % 300 == 0) {
            qDebug() << "VROpenVRManager: Grip action error:" << gripError << "Handle:" << m_actionGripModifier;
        }
    }
    
    // Poll axis action (touchpad/joystick)
    vr::InputAnalogActionData_t axisData;
    vr::EVRInputError axisError = vr::VRInput()->GetAnalogActionData(m_actionSeekAxis, &axisData, sizeof(axisData), vr::k_ulInvalidInputValueHandle);
    if (axisError == vr::VRInputError_None) {
        if (axisData.bActive) {
            state.seekAxis = QVector2D(axisData.x, axisData.y);
            
            static int axisDebugCount = 0;
            if (++axisDebugCount % 300 == 0) {
                qDebug() << "VROpenVRManager: Axis - Active:" << axisData.bActive << "X:" << axisData.x << "Y:" << axisData.y;
            }
            
            // Log significant axis movement
            if (qAbs(axisData.x) > 0.3f || qAbs(axisData.y) > 0.3f) {
                static int axisMovementCount = 0;
                if (++axisMovementCount % 30 == 0) { // Log every 0.5 seconds during movement
                    qDebug() << "VROpenVRManager: AXIS MOVEMENT - X:" << axisData.x << "Y:" << axisData.y;
                }
            }
        }
    } else {
        static int axisErrorCount = 0;
        if (++axisErrorCount % 300 == 0) {
            qDebug() << "VROpenVRManager: Axis action error:" << axisError;
        }
    }
    
#endif
    
    return state;
}

void VROpenVRManager::showBindingConfiguration()
{
#ifdef USE_OPENVR
    if (!m_controllerInputReady || !m_vrSystem) {
        qDebug() << "VROpenVRManager: Cannot show binding configuration - system not ready";
        return;
    }
    
    qDebug() << "VROpenVRManager: Opening SteamVR binding configuration UI...";
    
    vr::VRActiveActionSet_t actionSet = { 0 };
    actionSet.ulActionSet = m_actionSetVideo;
    actionSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
    
    vr::EVRInputError error = vr::VRInput()->ShowBindingsForActionSet(
        &actionSet, 
        sizeof(vr::VRActiveActionSet_t), 
        1, 
        vr::k_ulInvalidInputValueHandle);
    
    if (error != vr::VRInputError_None) {
        qDebug() << "VROpenVRManager: Failed to show binding configuration UI, error:" << error;
    } else {
        qDebug() << "VROpenVRManager: Binding configuration UI opened successfully";
        qDebug() << "VROpenVRManager: Please configure your controller bindings and then restart the VR player";
    }
#else
    qDebug() << "VROpenVRManager: OpenVR not available";
#endif
}
