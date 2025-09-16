#ifndef VR_OPENVR_MANAGER_H
#define VR_OPENVR_MANAGER_H

#include <QObject>
#include <QString>
#include <QMatrix4x4>
#include <QVector3D>
#include <memory>

#ifdef USE_OPENVR
#include <openvr.h>
#endif

/**
 * @class VROpenVRManager
 * @brief Manages OpenVR/SteamVR initialization, shutdown, and HMD operations
 * 
 * This class provides a wrapper around OpenVR functionality, handling:
 * - SteamVR runtime detection
 * - VR system initialization/shutdown
 * - HMD presence checking
 * - Frame submission to the VR compositor
 */
class VROpenVRManager : public QObject
{
    Q_OBJECT

public:
    enum class VRStatus {
        NotInitialized,
        SteamVRNotFound,
        NoHMDConnected,
        InitializationFailed,
        Ready,
        Error
    };

    struct VRSystemInfo {
        QString hmdName;
        uint32_t renderWidth;
        uint32_t renderHeight;
        float refreshRate;
        bool hasControllers;
    };

    explicit VROpenVRManager(QObject *parent = nullptr);
    ~VROpenVRManager();

    // Initialization and shutdown
    bool initialize();
    void shutdown();
    
    // Status queries
    bool isInitialized() const { return m_isInitialized; }
    bool isSteamVRAvailable() const;
    bool isHMDPresent() const;
    VRStatus getStatus() const { return m_status; }
    QString getStatusMessage() const;
    QString getLastError() const { return m_lastError; }
    
    // System information
    VRSystemInfo getSystemInfo() const { return m_systemInfo; }
    void getRecommendedRenderTargetSize(uint32_t& width, uint32_t& height) const;
    
    // Matrix operations for VR rendering
    QMatrix4x4 getHMDPoseMatrix() const;
    QMatrix4x4 getProjectionMatrix(bool leftEye, float nearPlane, float farPlane) const;
    QMatrix4x4 getEyePosMatrix(bool leftEye) const;
    
    // Custom projection matrix with zoom support (proper FOV adjustment)
    QMatrix4x4 getProjectionMatrixWithZoom(bool leftEye, float nearPlane, float farPlane, float zoomFactor) const;
    void getProjectionRawValues(bool leftEye, float& left, float& right, float& top, float& bottom) const;
    
    // Helper methods for position and rotation extraction
    QVector3D getHMDPosition() const;
    QMatrix4x4 getHMDRotationMatrix() const;
    static QMatrix4x4 extractRotationMatrix(const QMatrix4x4& matrix);
    static QVector3D extractPosition(const QMatrix4x4& matrix);
    
    // Frame submission
    bool submitFrame(uint32_t leftEyeTexture, uint32_t rightEyeTexture);
    
    // Compositor operations
    void compositorWaitGetPoses();
    bool isCompositorReady() const;
    
    // Controller input system (Legacy Direct Input)
    struct VRControllerState {
        bool recenterHeld;          // Trigger button (held state for continuous recenter)
        bool playPausePressed;      // Menu/Application button
        bool gripPressed;           // Grip button (modifier for combinations)
        QVector2D seekAxis;         // Trackpad/Joystick - Normal: X=seek 10s, Y=zoom | With Grip: X=seek 60s, Y=volume
        bool increaseSpeedPressed;  // Grip + Menu combination
        bool decreaseSpeedPressed;  // Grip + Trigger combination
        
        VRControllerState() : recenterHeld(false), playPausePressed(false), 
                              gripPressed(false), seekAxis(0.0f, 0.0f),
                              increaseSpeedPressed(false), decreaseSpeedPressed(false) {}
    };
    
    bool initializeControllerInput();
    void shutdownControllerInput();
    VRControllerState pollControllerInput();
    bool isControllerInputReady() const { return m_controllerInputReady; }

signals:
    void statusChanged(VRStatus status);
    void hmdConnected();
    void hmdDisconnected();
    void error(const QString& errorMessage);

private:
    bool checkSteamVRRuntime();
    bool initializeOpenVR();
    void updateHMDPose();
    void setLastError(const QString& error);
    QString getTrackedDeviceString(uint32_t device, uint32_t prop);

private:
#ifdef USE_OPENVR
    vr::IVRSystem* m_vrSystem;
    vr::IVRCompositor* m_vrCompositor;
    vr::TrackedDevicePose_t m_trackedDevicePoses[vr::k_unMaxTrackedDeviceCount];
#else
    void* m_vrSystem;
    void* m_vrCompositor;
    void* m_trackedDevicePoses;
#endif
    
    bool m_isInitialized;
    VRStatus m_status;
    VRSystemInfo m_systemInfo;
    QString m_lastError;
    
    // HMD pose matrix
    QMatrix4x4 m_hmdPoseMatrix;
    bool m_hmdPoseValid;
    
    // Controller input (Legacy Direct Input)
#ifdef USE_OPENVR
    vr::TrackedDeviceIndex_t m_leftControllerIndex;
    vr::TrackedDeviceIndex_t m_rightControllerIndex;
    vr::VRControllerState_t m_lastLeftControllerState;
    vr::VRControllerState_t m_lastRightControllerState;
#else
    uint32_t m_leftControllerIndex;
    uint32_t m_rightControllerIndex;
    void* m_lastLeftControllerState;
    void* m_lastRightControllerState;
#endif
    bool m_controllerInputReady;
    uint64_t m_lastButtonPressed;  // Track last button state for edge detection
};

#endif // VR_OPENVR_MANAGER_H
