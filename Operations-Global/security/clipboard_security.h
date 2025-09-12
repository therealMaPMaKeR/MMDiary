#ifndef CLIPBOARD_SECURITY_H
#define CLIPBOARD_SECURITY_H

#include <QString>
#include <QObject>
#include <QClipboard>
#include <QCryptographicHash>
#include <memory>
#include <functional>
#include <windows.h>

namespace ClipboardSecurity {

// Clipboard format identifiers
constexpr UINT CF_CLIPBOARD_VIEWER_IGNORE = 49363;  // Format to exclude from clipboard viewers
constexpr UINT CF_EXCLUDECLIPBOARD = 49637;         // Exclude from cloud clipboard

// Security flags
enum class SecurityLevel {
    Normal,      // Regular clipboard operation
    Sensitive,   // Sensitive data (passwords) - exclude from history
    Critical     // Critical data - maximum security
};

// Result structure for clipboard operations
struct ClipboardResult {
    bool success;
    QString errorMessage;
    bool monitorDetected;
};

// Monitor detection structure
struct MonitorInfo {
    bool detected;
    int monitorCount;
    QString warning;
};

class ClipboardSecurityManager : public QObject {
    Q_OBJECT

public:
    explicit ClipboardSecurityManager(QObject* parent = nullptr);
    ~ClipboardSecurityManager();

    // Secure copy operations
    static ClipboardResult copyTextSecure(const QString& text, SecurityLevel level = SecurityLevel::Normal);
    static ClipboardResult copyPasswordSecure(const QString& password);
    
    // Enhanced clipboard clearing
    static bool clearClipboardSecure();
    static bool clearAllFormats();
    
    // Monitor detection
    static MonitorInfo detectClipboardMonitors();
    static bool isClipboardBeingMonitored();
    
    // Paste validation
    static bool validatePasteData(const QMimeData* mimeData, qint64 maxTextSize = 100000);
    static QString sanitizePastedText(const QString& text, qint64 maxLength = 100000);
    
    // Windows clipboard history control
    static bool excludeFromClipboardHistory();
    static bool isClipboardHistoryEnabled();
    
    // Format control
    static bool restrictToPlainText();
    static QStringList getAvailableFormats();
    
signals:
    void clipboardMonitorDetected(const QString& warning);
    void securityEventOccurred(const QString& event);

private:
    // Internal helper functions
    static HWND getClipboardOwner();
    static bool setClipboardDataSecure(const QString& text, SecurityLevel level);
    static void overwriteClipboardMemory();
    static bool isWindowsClipboardHistoryActive();
    
    // Windows-specific clipboard formats we need to handle
    // Note: Custom formats like HTML need to be registered at runtime
    static UINT getHtmlFormat();
    static UINT getRtfFormat();
    static UINT getCsvFormat();
    
    // Anti-monitoring techniques
    static void applyAntiMonitoringMeasures();
    static bool checkForKnownMonitors();
    
    // Memory security
    static void secureWipeString(QString& str);
    static void secureWipeMemory(void* ptr, size_t size);
};

// Helper class for RAII clipboard handling
class ClipboardGuard {
public:
    ClipboardGuard();
    ~ClipboardGuard();
    
    bool isOpen() const { return m_isOpen; }
    
private:
    bool m_isOpen;
    HWND m_previousOwner;
};

// Clipboard Monitor class for detecting paste and overwrite events
class ClipboardMonitor : public QObject {
    Q_OBJECT

public:
    explicit ClipboardMonitor(QObject* parent = nullptr);
    ~ClipboardMonitor();

    // Start monitoring with a specific content hash
    void startMonitoring(const QString& contentHash);
    void stopMonitoring();
    bool isMonitoring() const { return m_isMonitoring; }
    
    // Set callbacks for events
    void setOnPasteCallback(std::function<void()> callback) { m_onPasteCallback = callback; }
    void setOnOverwriteCallback(std::function<void()> callback) { m_onOverwriteCallback = callback; }
    
    // Check if monitored content is still in clipboard
    bool isMonitoredContentStillPresent() const;

signals:
    void pasteDetected();
    void clipboardOverwritten();
    void monitoringStopped();

private:
    bool m_isMonitoring;
    QString m_monitoredContentHash;
    HWND m_hwnd;  // Hidden window for clipboard monitoring
    UINT m_clipboardSequenceNumber;
    
    // Callbacks
    std::function<void()> m_onPasteCallback;
    std::function<void()> m_onOverwriteCallback;
    
    // Helper functions
    void setupClipboardMonitoring();
    void cleanupClipboardMonitoring();
    QString getCurrentClipboardHash() const;
    bool detectPasteEvent();
    
    // Windows specific
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void handleClipboardUpdate();
};

// Utility functions for quick access
bool copyPasswordToClipboard(const QString& password);
bool clearSensitiveClipboard();
bool isClipboardSafe();

} // namespace ClipboardSecurity

#endif // CLIPBOARD_SECURITY_H
