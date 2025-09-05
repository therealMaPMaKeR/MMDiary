#ifndef POINTER_SAFETY_MACROS_H
#define POINTER_SAFETY_MACROS_H

#include <QDebug>
#include <QString>
#include <QPointer>

// ============================================================================
// Pointer Safety Macro System for MMDiary Video Player Components
// ============================================================================
// This header provides comprehensive pointer validation and safety macros
// to prevent crashes from null/dangling pointer access throughout the codebase
// ============================================================================

// Core validation macros with debug logging
#define PTR_CHECK(ptr, returnValue) \
    do { \
        if (!(ptr)) { \
            qDebug() << "PTR_CHECK: Null pointer detected at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

#define PTR_CHECK_VOID(ptr) \
    do { \
        if (!(ptr)) { \
            qDebug() << "PTR_CHECK_VOID: Null pointer detected at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return; \
        } \
    } while(0)

#define PTR_CHECK_CONTINUE(ptr) \
    if (!(ptr)) { \
        qDebug() << "PTR_CHECK_CONTINUE: Null pointer at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
        continue; \
    }

#define PTR_CHECK_BREAK(ptr) \
    if (!(ptr)) { \
        qDebug() << "PTR_CHECK_BREAK: Null pointer at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
        break; \
    }

// Safe deletion macros
#define SAFE_DELETE(ptr) \
    do { \
        if (ptr) { \
            delete ptr; \
            ptr = nullptr; \
        } \
    } while(0)

#define SAFE_DELETE_ARRAY(ptr) \
    do { \
        if (ptr) { \
            delete[] ptr; \
            ptr = nullptr; \
        } \
    } while(0)

// Qt-specific safe deletion with disconnect
#define SAFE_DELETE_QOBJECT(ptr) \
    do { \
        if (ptr) { \
            ptr->disconnect(); \
            ptr->deleteLater(); \
            ptr = nullptr; \
        } \
    } while(0)

// QPointer validation macros
#define QPTR_CHECK(qptr, returnValue) \
    do { \
        if ((qptr).isNull()) { \
            qDebug() << "QPTR_CHECK: QPointer is null at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

#define QPTR_CHECK_VOID(qptr) \
    do { \
        if ((qptr).isNull()) { \
            qDebug() << "QPTR_CHECK_VOID: QPointer is null at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return; \
        } \
    } while(0)

// Multi-pointer validation
#define PTR_CHECK_MULTIPLE(returnValue, ...) \
    do { \
        void* ptrs[] = { __VA_ARGS__ }; \
        for (size_t i = 0; i < sizeof(ptrs)/sizeof(void*); ++i) { \
            if (!ptrs[i]) { \
                qDebug() << "PTR_CHECK_MULTIPLE: Null pointer" << i << "at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
                return returnValue; \
            } \
        } \
    } while(0)

// Bounds checking macros
#define ARRAY_BOUNDS_CHECK(index, size, returnValue) \
    do { \
        if ((index) < 0 || (index) >= (size)) { \
            qDebug() << "ARRAY_BOUNDS_CHECK: Index" << (index) << "out of bounds [0," << (size) << ") at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

#define BUFFER_SIZE_CHECK(required, available, returnValue) \
    do { \
        if ((required) > (available)) { \
            qDebug() << "BUFFER_SIZE_CHECK: Required size" << (required) << "exceeds available" << (available) \
                     << "at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// VLC-specific pointer validation
#define VLC_PTR_CHECK(vlcPtr, returnValue) \
    do { \
        if (!(vlcPtr)) { \
            qDebug() << "VLC_PTR_CHECK: VLC pointer null at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// OpenVR-specific pointer validation
#ifdef USE_OPENVR
#define VR_PTR_CHECK(vrPtr, returnValue) \
    do { \
        if (!(vrPtr)) { \
            qDebug() << "VR_PTR_CHECK: VR pointer null at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

#define VR_DEVICE_CHECK(deviceIndex, returnValue) \
    do { \
        if ((deviceIndex) == vr::k_unTrackedDeviceIndexInvalid || \
            (deviceIndex) >= vr::k_unMaxTrackedDeviceCount) { \
            qDebug() << "VR_DEVICE_CHECK: Invalid device index" << (deviceIndex) \
                     << "at" << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)
#else
#define VR_PTR_CHECK(vrPtr, returnValue) PTR_CHECK(vrPtr, returnValue)
#define VR_DEVICE_CHECK(deviceIndex, returnValue) ((void)0)
#endif

// Thread safety validation
#define MUTEX_LOCK_CHECK(mutex, returnValue) \
    do { \
        if (!(mutex).tryLock(5000)) { \
            qDebug() << "MUTEX_LOCK_CHECK: Failed to acquire mutex within 5s at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// Memory allocation validation
#define ALLOC_CHECK(ptr, returnValue) \
    do { \
        if (!(ptr)) { \
            qDebug() << "ALLOC_CHECK: Memory allocation failed at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// Smart pointer validation
#define UNIQUE_PTR_CHECK(uptr, returnValue) \
    do { \
        if (!(uptr) || !(uptr).get()) { \
            qDebug() << "UNIQUE_PTR_CHECK: unique_ptr is empty at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

#define SHARED_PTR_CHECK(sptr, returnValue) \
    do { \
        if (!(sptr) || !(sptr).get()) { \
            qDebug() << "SHARED_PTR_CHECK: shared_ptr is empty at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// Widget validation macros
#define WIDGET_CHECK(widget, returnValue) \
    do { \
        if (!(widget) || !(widget)->isVisible()) { \
            qDebug() << "WIDGET_CHECK: Widget null or not visible at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

#define UI_ELEMENT_CHECK(element, returnValue) \
    do { \
        if (!(element)) { \
            qDebug() << "UI_ELEMENT_CHECK: UI element null at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// File operation validation
#define FILE_PTR_CHECK(filePtr, returnValue) \
    do { \
        if (!(filePtr) || !(filePtr)->isOpen()) { \
            qDebug() << "FILE_PTR_CHECK: File pointer null or not open at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// String validation
#define STRING_CHECK(str, returnValue) \
    do { \
        if ((str).isEmpty()) { \
            qDebug() << "STRING_CHECK: Empty string at" \
                     << __FILE__ << ":" << __LINE__ << __FUNCTION__; \
            return returnValue; \
        } \
    } while(0)

// Debug-only assertions (removed in release builds)
#ifdef QT_DEBUG
    #define DEBUG_PTR_ASSERT(ptr) \
        Q_ASSERT_X((ptr) != nullptr, __FUNCTION__, "Pointer must not be null")
    
    #define DEBUG_RANGE_ASSERT(value, min, max) \
        Q_ASSERT_X((value) >= (min) && (value) <= (max), __FUNCTION__, \
                   QString("Value %1 out of range [%2, %3]").arg(value).arg(min).arg(max).toLocal8Bit().data())
#else
    #define DEBUG_PTR_ASSERT(ptr) ((void)0)
    #define DEBUG_RANGE_ASSERT(value, min, max) ((void)0)
#endif

// Cleanup helper for multiple pointers
template<typename... Ptrs>
void cleanupPointers(Ptrs*&... ptrs) {
    ((SAFE_DELETE(ptrs)), ...);
}

// RAII helper for automatic cleanup
template<typename T>
class AutoCleanup {
public:
    explicit AutoCleanup(T* ptr) : m_ptr(ptr) {}
    ~AutoCleanup() { SAFE_DELETE(m_ptr); }
    T* release() { T* tmp = m_ptr; m_ptr = nullptr; return tmp; }
    T* get() const { return m_ptr; }
    
private:
    T* m_ptr;
    AutoCleanup(const AutoCleanup&) = delete;
    AutoCleanup& operator=(const AutoCleanup&) = delete;
};

// Performance monitoring macros
#ifdef ENABLE_PTR_PERF_MONITORING
    #define PTR_PERF_START() \
        QElapsedTimer ptrPerfTimer; \
        ptrPerfTimer.start()
    
    #define PTR_PERF_LOG(operation) \
        qDebug() << "PTR_PERF:" << (operation) << "took" << ptrPerfTimer.elapsed() << "ms"
#else
    #define PTR_PERF_START() ((void)0)
    #define PTR_PERF_LOG(operation) ((void)0)
#endif

#endif // POINTER_SAFETY_MACROS_H
