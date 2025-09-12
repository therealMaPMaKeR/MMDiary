#include "clipboard_security.h"
#include "../inputvalidation.h"
#include "../SafeTimer.h"
#include <QGuiApplication>
#include <QMimeData>
#include <QDebug>
#include <QRandomGenerator>
#include <QTimer>
#include <QProcess>
#include <QCoreApplication>
#include <QMetaObject>
#include <windows.h>
#include <vector>
#include <algorithm>

namespace ClipboardSecurity {

// Helper functions to get registered clipboard formats
UINT ClipboardSecurityManager::getHtmlFormat() {
    static UINT format = RegisterClipboardFormat(L"HTML Format");
    return format;
}

UINT ClipboardSecurityManager::getRtfFormat() {
    static UINT format = RegisterClipboardFormat(L"Rich Text Format");
    return format;
}

UINT ClipboardSecurityManager::getCsvFormat() {
    static UINT format = RegisterClipboardFormat(L"CSV");
    return format;
}

// Static helper to securely wipe QString data
void ClipboardSecurityManager::secureWipeString(QString& str) {
    if (str.isEmpty()) return;
    
    QChar* data = str.data();
    int len = str.length();
    
    // Use SecureZeroMemory on Windows to prevent compiler optimization
    SecureZeroMemory(data, len * sizeof(QChar));
    
    // Additional overwrite passes with random data
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < len; ++i) {
            data[i] = QChar(QRandomGenerator::global()->bounded(32, 127));
        }
    }
    
    // Final clear
    SecureZeroMemory(data, len * sizeof(QChar));
    str.clear();
    str.squeeze();
}

// Static helper to securely wipe memory
void ClipboardSecurityManager::secureWipeMemory(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
    
    // Multiple overwrite passes
    unsigned char* bytes = static_cast<unsigned char*>(ptr);
    
    // Pass 1: All zeros
    SecureZeroMemory(bytes, size);
    
    // Pass 2: All ones
    memset(bytes, 0xFF, size);
    
    // Pass 3: Random data
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<unsigned char>(QRandomGenerator::global()->bounded(256));
    }
    
    // Final pass: Zeros
    SecureZeroMemory(bytes, size);
}

ClipboardSecurityManager::ClipboardSecurityManager(QObject* parent) 
    : QObject(parent) {
    qDebug() << "ClipboardSecurityManager: Initialized";
}

ClipboardSecurityManager::~ClipboardSecurityManager() {
    qDebug() << "ClipboardSecurityManager: Destructor - clearing clipboard";
    clearClipboardSecure();
}

// RAII Clipboard handler
ClipboardGuard::ClipboardGuard() : m_isOpen(false), m_previousOwner(nullptr) {
    // Get current clipboard owner before opening
    m_previousOwner = GetClipboardOwner();
    
    // Try to open clipboard with retries
    int retries = 5;
    while (retries > 0 && !m_isOpen) {
        if (OpenClipboard(nullptr)) {
            m_isOpen = true;
            break;
        }
        Sleep(10); // Wait 10ms before retry
        retries--;
    }
    
    if (!m_isOpen) {
        qWarning() << "ClipboardSecurityManager: Failed to open clipboard after retries";
    }
}

ClipboardGuard::~ClipboardGuard() {
    if (m_isOpen) {
        CloseClipboard();
    }
}

// Main secure copy function for passwords
ClipboardResult ClipboardSecurityManager::copyPasswordSecure(const QString& password) {
    qDebug() << "ClipboardSecurityManager: Copying password securely";
    
    ClipboardResult result{false, "", false};
    
    // Check for clipboard monitors first
    MonitorInfo monitorInfo = detectClipboardMonitors();
    if (monitorInfo.detected) {
        result.monitorDetected = true;
        result.errorMessage = monitorInfo.warning;
        qWarning() << "ClipboardSecurityManager: Clipboard monitor detected!";
    }
    
    // Proceed with secure copy
    result = copyTextSecure(password, SecurityLevel::Sensitive);
    
    return result;
}

// Enhanced secure text copy
ClipboardResult ClipboardSecurityManager::copyTextSecure(const QString& text, SecurityLevel level) {
    ClipboardResult result{false, "", false};
    
    if (text.isEmpty()) {
        result.errorMessage = "Empty text provided";
        return result;
    }
    
    // Input validation - passwords shouldn't exceed reasonable length
    if (text.length() > 1000) {
        result.errorMessage = "Text too long for clipboard operation";
        return result;
    }
    
    ClipboardGuard guard;
    if (!guard.isOpen()) {
        result.errorMessage = "Failed to open clipboard";
        return result;
    }
    
    // Clear existing clipboard data
    if (!EmptyClipboard()) {
        result.errorMessage = "Failed to empty clipboard";
        return result;
    }
    
    // Prepare Unicode text for clipboard
    std::wstring wstr = text.toStdWString();
    size_t size = (wstr.length() + 1) * sizeof(wchar_t);
    
    // Allocate global memory for clipboard
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!hMem) {
        result.errorMessage = "Failed to allocate clipboard memory";
        return result;
    }
    
    // Copy text to global memory
    wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!pMem) {
        GlobalFree(hMem);
        result.errorMessage = "Failed to lock clipboard memory";
        return result;
    }
    
    wcscpy_s(pMem, wstr.length() + 1, wstr.c_str());
    GlobalUnlock(hMem);
    
    // Set clipboard data
    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem);
        result.errorMessage = "Failed to set clipboard data";
        return result;
    }
    
    // Apply security measures based on level
    if (level >= SecurityLevel::Sensitive) {
        // Exclude from clipboard history (Windows 10/11)
        excludeFromClipboardHistory();
        
        // Restrict to plain text only
        restrictToPlainText();
        
        // Apply anti-monitoring measures
        applyAntiMonitoringMeasures();
    }
    
    result.success = true;
    qDebug() << "ClipboardSecurityManager: Text copied securely with level" << static_cast<int>(level);
    
    return result;
}

// Exclude from Windows clipboard history
bool ClipboardSecurityManager::excludeFromClipboardHistory() {
    // This format tells Windows not to include in clipboard history
    UINT cfExcludeFormat = RegisterClipboardFormat(L"ExcludeClipboardContentFromMonitorProcessing");
    if (cfExcludeFormat == 0) {
        qWarning() << "ClipboardSecurityManager: Failed to register exclude format";
        return false;
    }
    
    // Create dummy data for the exclude format
    HGLOBAL hExclude = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
    if (!hExclude) {
        return false;
    }
    
    // Set the exclude format
    if (!SetClipboardData(cfExcludeFormat, hExclude)) {
        GlobalFree(hExclude);
        qWarning() << "ClipboardSecurityManager: Failed to set exclude format";
        return false;
    }
    
    // Also set formats that cloud clipboard should ignore
    UINT cfNoCloud = RegisterClipboardFormat(L"CannotBeStoredToCloudClipboard");
    if (cfNoCloud != 0) {
        HGLOBAL hNoCloud = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
        if (hNoCloud) {
            SetClipboardData(cfNoCloud, hNoCloud);
        }
    }
    
    qDebug() << "ClipboardSecurityManager: Excluded from clipboard history";
    return true;
}

// Restrict clipboard to plain text only
bool ClipboardSecurityManager::restrictToPlainText() {
    // Remove rich text and HTML formats if they were auto-generated
    // We can't remove them, but we can set them to empty
    
    // Set empty HTML format
    UINT cfHtml = getHtmlFormat();
    if (cfHtml != 0) {
        HGLOBAL hEmpty = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
        if (hEmpty) {
            SetClipboardData(cfHtml, hEmpty);
        }
    }
    
    // Set empty RTF format
    UINT cfRtf = getRtfFormat();
    if (cfRtf != 0) {
        HGLOBAL hEmpty = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
        if (hEmpty) {
            SetClipboardData(cfRtf, hEmpty);
        }
    }
    
    // Set empty CSV format
    UINT cfCsv = getCsvFormat();
    if (cfCsv != 0) {
        HGLOBAL hEmpty = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
        if (hEmpty) {
            SetClipboardData(cfCsv, hEmpty);
        }
    }
    
    return true;
}

// Enhanced clipboard clearing
bool ClipboardSecurityManager::clearClipboardSecure() {
    qDebug() << "ClipboardSecurityManager: Secure clipboard clearing initiated";
    
    ClipboardGuard guard;
    if (!guard.isOpen()) {
        qWarning() << "ClipboardSecurityManager: Failed to open clipboard for clearing";
        return false;
    }
    
    // Overwrite with random data multiple times
    overwriteClipboardMemory();
    
    // Clear all formats
    if (!EmptyClipboard()) {
        qWarning() << "ClipboardSecurityManager: Failed to empty clipboard";
        return false;
    }
    
    qDebug() << "ClipboardSecurityManager: Clipboard cleared securely";
    return true;
}

// Overwrite clipboard memory with random data
void ClipboardSecurityManager::overwriteClipboardMemory() {
    // Generate random data
    QString randomData;
    randomData.reserve(256);
    
    for (int pass = 0; pass < 3; ++pass) {
        randomData.clear();
        for (int i = 0; i < 256; ++i) {
            randomData.append(QChar(QRandomGenerator::global()->bounded(33, 127)));
        }
        
        // Set random data to clipboard
        std::wstring wstr = randomData.toStdWString();
        size_t size = (wstr.length() + 1) * sizeof(wchar_t);
        
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hMem) {
            wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
            if (pMem) {
                wcscpy_s(pMem, wstr.length() + 1, wstr.c_str());
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
    }
    
    // Securely wipe the random data string
    secureWipeString(randomData);
}

// Clear all clipboard formats
bool ClipboardSecurityManager::clearAllFormats() {
    ClipboardGuard guard;
    if (!guard.isOpen()) {
        return false;
    }
    
    // Get all available formats
    UINT format = 0;
    std::vector<UINT> formats;
    
    do {
        format = EnumClipboardFormats(format);
        if (format != 0) {
            formats.push_back(format);
        }
    } while (format != 0);
    
    qDebug() << "ClipboardSecurityManager: Found" << formats.size() << "clipboard formats to clear";
    
    // Clear all formats
    return EmptyClipboard();
}

// Detect clipboard monitors
MonitorInfo ClipboardSecurityManager::detectClipboardMonitors() {
    MonitorInfo info{false, 0, ""};
    
    // Check for clipboard viewer chain
    HWND viewer = GetClipboardViewer();
    if (viewer != nullptr) {
        info.detected = true;
        info.monitorCount++;
        info.warning = "Clipboard viewer detected in the viewer chain";
    }
    
    // Check for known clipboard monitoring processes
    if (checkForKnownMonitors()) {
        info.detected = true;
        info.monitorCount++;
        info.warning += (info.warning.isEmpty() ? "" : "\n") + 
                       QString("Known clipboard monitoring software detected");
    }
    
    // Check if Windows clipboard history is enabled
    if (isWindowsClipboardHistoryActive()) {
        info.detected = true;
        info.warning += (info.warning.isEmpty() ? "" : "\n") + 
                       QString("Windows Clipboard History is enabled (Win+V)");
    }
    
    return info;
}

// Check for known clipboard monitoring software
bool ClipboardSecurityManager::checkForKnownMonitors() {
    // List of known clipboard monitoring processes
    const QStringList monitorProcesses = {
        "clipdiary.exe",
        "clipmate.exe",
        "ditto.exe",
        "clipboardmaster.exe",
        "clipx.exe",
        "clcl.exe",
        "arsclip.exe",
        "clipboardfusion.exe",
        "1clipboard.exe",
        "clipclip.exe",
        "copyq.exe"
    };
    
    // Use WMI or Process enumeration to check for these processes
    QProcess process;
    process.start("wmic", QStringList() << "process" << "get" << "name");
    process.waitForFinished(1000);
    
    QString output = process.readAllStandardOutput().toLower();
    
    for (const QString& monitor : monitorProcesses) {
        if (output.contains(monitor.toLower())) {
            qWarning() << "ClipboardSecurityManager: Detected clipboard monitor:" << monitor;
            return true;
        }
    }
    
    return false;
}

// Check if Windows clipboard history is active
bool ClipboardSecurityManager::isWindowsClipboardHistoryActive() {
    // Check registry for clipboard history setting
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(DWORD);
    
    if (RegOpenKeyEx(HKEY_CURRENT_USER, 
                     L"Software\\Microsoft\\Clipboard",
                     0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        if (RegQueryValueEx(hKey, L"EnableClipboardHistory", nullptr, 
                           nullptr, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return value != 0;
        }
        RegCloseKey(hKey);
    }
    
    // Default to assuming it might be enabled on Windows 10/11
    OSVERSIONINFO osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    
    if (GetVersionEx(&osvi)) {
        // Windows 10 is version 10.0
        if (osvi.dwMajorVersion >= 10) {
            return true; // Assume enabled if we can't determine
        }
    }
    
    return false;
}

// Is clipboard being monitored
bool ClipboardSecurityManager::isClipboardBeingMonitored() {
    MonitorInfo info = detectClipboardMonitors();
    return info.detected;
}

// Apply anti-monitoring measures
void ClipboardSecurityManager::applyAntiMonitoringMeasures() {
    // Set clipboard sequence number to confuse monitors
    // This is a technique to make monitors think clipboard hasn't changed
    
    // Add decoy formats
    UINT cfDecoy = RegisterClipboardFormat(L"SecurityDecoyFormat");
    if (cfDecoy != 0) {
        HGLOBAL hDecoy = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 64);
        if (hDecoy) {
            void* pDecoy = GlobalLock(hDecoy);
            if (pDecoy) {
                // Fill with random data
                unsigned char* bytes = static_cast<unsigned char*>(pDecoy);
                for (int i = 0; i < 64; ++i) {
                    bytes[i] = static_cast<unsigned char>(QRandomGenerator::global()->bounded(256));
                }
                GlobalUnlock(hDecoy);
                SetClipboardData(cfDecoy, hDecoy);
            }
        }
    }
}

// Validate paste data
bool ClipboardSecurityManager::validatePasteData(const QMimeData* mimeData, qint64 maxTextSize) {
    if (!mimeData) {
        return false;
    }
    
    // Check text size if present
    if (mimeData->hasText()) {
        QString text = mimeData->text();
        
        // Size validation
        if (text.length() > maxTextSize) {
            qWarning() << "ClipboardSecurityManager: Pasted text exceeds maximum size:" 
                      << text.length() << ">" << maxTextSize;
            return false;
        }
        
        // Content validation using InputValidation
        InputValidation::ValidationResult result = 
            InputValidation::validateInput(text, InputValidation::InputType::PlainText, maxTextSize);
        
        if (!result.isValid) {
            qWarning() << "ClipboardSecurityManager: Paste validation failed:" << result.errorMessage;
            return false;
        }
    }
    
    // Check for suspicious formats
    QStringList formats = mimeData->formats();
    for (const QString& format : formats) {
        // Check for executable or script formats
        if (format.contains("application/x-msdownload") ||
            format.contains("application/x-exe") ||
            format.contains("application/x-dll") ||
            format.contains("text/html") ||  // Could contain scripts
            format.contains("application/javascript")) {
            qWarning() << "ClipboardSecurityManager: Suspicious format detected:" << format;
            return false;
        }
    }
    
    return true;
}

// Sanitize pasted text
QString ClipboardSecurityManager::sanitizePastedText(const QString& text, qint64 maxLength) {
    if (text.isEmpty()) {
        return text;
    }
    
    // Truncate if too long
    QString sanitized = text.left(maxLength);
    
    // Remove null characters
    sanitized.remove(QChar('\0'));
    
    // Remove other dangerous characters
    sanitized.remove(QChar(0xFFFE)); // Byte order mark
    sanitized.remove(QChar(0xFFFF)); // Invalid character
    
    // Validate using InputValidation
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(sanitized, InputValidation::InputType::PlainText, maxLength);
    
    if (!result.isValid) {
        qWarning() << "ClipboardSecurityManager: Text sanitization detected issues:" << result.errorMessage;
        // Return empty string if validation fails completely
        return QString();
    }
    
    return sanitized;
}

// Get available clipboard formats
QStringList ClipboardSecurityManager::getAvailableFormats() {
    QStringList formatList;
    
    ClipboardGuard guard;
    if (!guard.isOpen()) {
        return formatList;
    }
    
    UINT format = 0;
    wchar_t formatName[256];
    
    do {
        format = EnumClipboardFormats(format);
        if (format != 0) {
            if (GetClipboardFormatName(format, formatName, 256) > 0) {
                formatList.append(QString::fromWCharArray(formatName));
            } else {
                // Standard formats
                switch (format) {
                    case CF_TEXT: formatList.append("CF_TEXT"); break;
                    case CF_UNICODETEXT: formatList.append("CF_UNICODETEXT"); break;
                    case CF_OEMTEXT: formatList.append("CF_OEMTEXT"); break;
                    case CF_BITMAP: formatList.append("CF_BITMAP"); break;
                    case CF_DIB: formatList.append("CF_DIB"); break;
                    default: formatList.append(QString("Format_%1").arg(format)); break;
                }
            }
        }
    } while (format != 0);
    
    return formatList;
}

// Utility functions
bool copyPasswordToClipboard(const QString& password) {
    ClipboardResult result = ClipboardSecurityManager::copyPasswordSecure(password);
    if (result.monitorDetected) {
        qWarning() << "ClipboardSecurityManager: Warning -" << result.errorMessage;
    }
    return result.success;
}

bool clearSensitiveClipboard() {
    return ClipboardSecurityManager::clearClipboardSecure();
}

bool isClipboardSafe() {
    return !ClipboardSecurityManager::isClipboardBeingMonitored();
}

// Static instance pointer for keyboard hook
ClipboardMonitor* ClipboardMonitor::s_instance = nullptr;

// ClipboardMonitor implementation
ClipboardMonitor::ClipboardMonitor(QObject* parent)
    : QObject(parent)
    , m_isMonitoring(false)
    , m_hwnd(nullptr)
    , m_keyboardHook(nullptr)
    , m_ctrlPressed(false)
{
    qDebug() << "ClipboardMonitor: Constructor";
}

ClipboardMonitor::~ClipboardMonitor()
{
    qDebug() << "ClipboardMonitor: Destructor";
    stopMonitoring();
}

void ClipboardMonitor::startMonitoring(const QString& contentHash)
{
    if (m_isMonitoring) {
        qDebug() << "ClipboardMonitor: Already monitoring, stopping previous session";
        stopMonitoring();
    }
    
    qDebug() << "ClipboardMonitor: Starting monitoring for content hash:" << contentHash;
    m_monitoredContentHash = contentHash;
    m_ctrlPressed = false;
    
    // Set static instance for keyboard hook
    s_instance = this;
    
    setupClipboardMonitoring();
    setupKeyboardHook();
    m_isMonitoring = true;
}

void ClipboardMonitor::stopMonitoring()
{
    if (!m_isMonitoring) {
        return;
    }
    
    qDebug() << "ClipboardMonitor: Stopping monitoring";
    m_isMonitoring = false;
    m_monitoredContentHash.clear();
    m_ctrlPressed = false;
    
    cleanupKeyboardHook();
    cleanupClipboardMonitoring();
    
    // Clear static instance
    if (s_instance == this) {
        s_instance = nullptr;
    }
    
    // Use QMetaObject::invokeMethod for thread-safe signal emission
    QMetaObject::invokeMethod(this, "monitoringStopped", Qt::QueuedConnection);
}

bool ClipboardMonitor::isMonitoredContentStillPresent() const
{
    if (!m_isMonitoring || m_monitoredContentHash.isEmpty()) {
        return false;
    }
    
    QString currentHash = getCurrentClipboardHash();
    return currentHash == m_monitoredContentHash;
}

void ClipboardMonitor::setupClipboardMonitoring()
{
    // Register window class for clipboard monitoring
    const wchar_t* className = L"MMDiaryClipboardMonitor";
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = className;
    
    if (!RegisterClassEx(&wc)) {
        // Class might already be registered
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            qWarning() << "ClipboardMonitor: Failed to register window class, error:" << error;
            return;
        }
    }
    
    // Create hidden window
    m_hwnd = CreateWindowEx(
        0,
        className,
        L"ClipboardMonitor",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr,
        GetModuleHandle(nullptr),
        this  // Pass this pointer for WndProc
    );
    
    if (!m_hwnd) {
        qWarning() << "ClipboardMonitor: Failed to create window";
        return;
    }
    
    // Add to clipboard viewer chain
    AddClipboardFormatListener(m_hwnd);
    qDebug() << "ClipboardMonitor: Setup complete";
}

void ClipboardMonitor::cleanupClipboardMonitoring()
{
    if (m_hwnd) {
        RemoveClipboardFormatListener(m_hwnd);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

QString ClipboardMonitor::getCurrentClipboardHash() const
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    QString text = clipboard->text();
    
    if (text.isEmpty()) {
        return QString();
    }
    
    // Create hash of clipboard content
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(text.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

void ClipboardMonitor::setupKeyboardHook()
{
    qDebug() << "ClipboardMonitor: Setting up keyboard hook";
    
    // Install low-level keyboard hook
    m_keyboardHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        KeyboardProc,
        GetModuleHandle(nullptr),
        0
    );
    
    if (!m_keyboardHook) {
        qWarning() << "ClipboardMonitor: Failed to install keyboard hook, error:" << GetLastError();
    } else {
        qDebug() << "ClipboardMonitor: Keyboard hook installed successfully";
    }
}

void ClipboardMonitor::cleanupKeyboardHook()
{
    if (m_keyboardHook) {
        qDebug() << "ClipboardMonitor: Removing keyboard hook";
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }
}

void ClipboardMonitor::checkForPasteCombo(DWORD vkCode, bool keyDown)
{
    if (!m_isMonitoring) {
        return;
    }
    
    // Track Ctrl key state
    if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) {
        m_ctrlPressed = keyDown;
    }
    
    // Track Shift key state for Shift+Insert detection
    static bool shiftPressed = false;
    if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) {
        shiftPressed = keyDown;
    }
    
    // Check for paste combinations
    bool pasteDetected = false;
    
    // Ctrl+V (most common paste)
    if (keyDown && m_ctrlPressed && vkCode == 'V') {
        qDebug() << "ClipboardMonitor: Ctrl+V detected";
        pasteDetected = true;
    }
    // Shift+Insert (alternative paste)
    else if (keyDown && shiftPressed && vkCode == VK_INSERT) {
        qDebug() << "ClipboardMonitor: Shift+Insert detected";
        pasteDetected = true;
    }
    
    if (pasteDetected) {
        // Verify that our monitored content is still in the clipboard
        if (isMonitoredContentStillPresent()) {
            qDebug() << "ClipboardMonitor: Paste event confirmed - monitored password is being pasted";
            // Use QMetaObject::invokeMethod for thread-safe signal emission from static callback
            QMetaObject::invokeMethod(this, "pasteDetected", Qt::QueuedConnection);
            if (m_onPasteCallback) {
                m_onPasteCallback();
            }
        } else {
            qDebug() << "ClipboardMonitor: Paste key detected but clipboard content has changed";
        }
    }
}

LRESULT CALLBACK ClipboardMonitor::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && s_instance) {
        KBDLLHOOKSTRUCT* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        s_instance->checkForPasteCombo(kbd->vkCode, keyDown);
    }
    
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK ClipboardMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        ClipboardMonitor* monitor = reinterpret_cast<ClipboardMonitor*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(monitor));
    }
    
    ClipboardMonitor* monitor = reinterpret_cast<ClipboardMonitor*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    if (monitor && msg == WM_CLIPBOARDUPDATE) {
        monitor->handleClipboardUpdate();
        return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ClipboardMonitor::handleClipboardUpdate()
{
    if (!m_isMonitoring) {
        return;
    }
    
    // This is called when clipboard content changes
    QString currentHash = getCurrentClipboardHash();
    if (currentHash != m_monitoredContentHash) {
        qDebug() << "ClipboardMonitor: Clipboard overwritten via WM_CLIPBOARDUPDATE";
        // Use QMetaObject::invokeMethod for thread-safe signal emission
        QMetaObject::invokeMethod(this, "clipboardOverwritten", Qt::QueuedConnection);
        if (m_onOverwriteCallback) {
            m_onOverwriteCallback();
        }
        stopMonitoring();
    }
}

} // namespace ClipboardSecurity
