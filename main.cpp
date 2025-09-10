#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QIcon>
#include <QDir>
#include <QLocalSocket>
#include <QLocalServer>
#include <QMessageBox>
#include "loginscreen.h"
#include "mainwindow.h"
#include "constants.h"
#include <QFile>
#include <QtDebug>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include "operations_files.h"
#include "passwordvalidation.h"  // SECURITY: For grace period cleanup
#include <QSystemTrayIcon>  // SECURITY: For tray icon cleanup

// Crash handler includes
#include <csignal>
#include <cstdlib>
#include <cstring>  // for memset
#include <atomic>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// for writing debug to text file
/*
static QtMessageHandler qtOldMsgHandler = nullptr;

void fileMessageHandler(QtMsgType type,
                        const QMessageLogContext &context,
                        const QString &msg)
{
    // Format the message
    QString formatted = qFormatLogMessage(type, context, msg);

    // Thread-safe, append to file
    static QFile logFile("app_debug.log");
    if (!logFile.isOpen())
        logFile.open(QIODevice::Append | QIODevice::Text);

    QTextStream ts(&logFile);
    ts << formatted << "\n";
    ts.flush();

    // Also forward to the default handler (e.g., debugger output)
    if (qtOldMsgHandler)
        qtOldMsgHandler(type, context, msg);
}
*/


// Define a single application ID
#define APP_ID "MMDiary_SingleInstance"

// Global atomic flag to prevent recursive crash handling
static std::atomic<bool> g_crashHandlerActive(false);
static std::atomic<bool> g_emergencyCleanupDone(false);

// Emergency cleanup function - must be async-signal-safe
void performEmergencyCrashCleanup()
{
    // Prevent recursive calls
    bool expected = false;
    if (!g_emergencyCleanupDone.compare_exchange_strong(expected, true)) {
        return; // Already cleaning up
    }
    
    // NOTE: We can't use qDebug() here as it's not async-signal-safe
    // Using fprintf directly to stderr for emergency logging
    fprintf(stderr, "main: CRASH HANDLER - Performing emergency cleanup\n");
    
    try {
        // Attempt to clean up temp folders
        // This is a best-effort attempt - may not succeed in all crash scenarios
        OperationsFiles::cleanupAllUserTempFolders();
        fprintf(stderr, "main: CRASH HANDLER - Temp folder cleanup attempted\n");
    } catch (...) {
        fprintf(stderr, "main: CRASH HANDLER - Exception during cleanup\n");
    }
    
    // Force flush stderr to ensure messages are written
    fflush(stderr);
}

// Signal handler for crashes (works on Windows with limited signal support)
extern "C" void crashSignalHandler(int signum)
{
    // Prevent recursive crash handling
    bool expected = false;
    if (!g_crashHandlerActive.compare_exchange_strong(expected, true)) {
        return; // Already handling a crash
    }
    
    const char* signalName = "UNKNOWN";
    switch (signum) {
        case SIGSEGV: signalName = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: signalName = "SIGABRT (Abort)"; break;
        case SIGTERM: signalName = "SIGTERM (Termination)"; break;
        case SIGILL:  signalName = "SIGILL (Illegal instruction)"; break;
        case SIGFPE:  signalName = "SIGFPE (Floating point exception)"; break;
        case SIGINT:  signalName = "SIGINT (Interrupt)"; break;
    }
    
    fprintf(stderr, "\n=== CRASH DETECTED ===\n");
    fprintf(stderr, "main: CRASH HANDLER - Caught signal %d: %s\n", signum, signalName);
    
    // Perform emergency cleanup
    performEmergencyCrashCleanup();
    
    fprintf(stderr, "main: CRASH HANDLER - Cleanup complete, terminating...\n");
    fprintf(stderr, "=== END CRASH HANDLER ===\n\n");
    fflush(stderr);
    
    // Re-raise the signal with default handler to get proper core dump
    signal(signum, SIG_DFL);
    raise(signum);
}

#ifdef Q_OS_WIN
// Windows structured exception handler
LONG WINAPI windowsExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo)
{
    // Prevent recursive crash handling
    bool expected = false;
    if (!g_crashHandlerActive.compare_exchange_strong(expected, true)) {
        return EXCEPTION_CONTINUE_SEARCH; // Already handling a crash
    }
    
    fprintf(stderr, "\n=== WINDOWS EXCEPTION DETECTED ===\n");
    
    const char* exceptionName = "UNKNOWN";
    if (ExceptionInfo && ExceptionInfo->ExceptionRecord) {
        switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION:
                exceptionName = "ACCESS_VIOLATION";
                break;
            case EXCEPTION_STACK_OVERFLOW:
                exceptionName = "STACK_OVERFLOW";
                break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                exceptionName = "ARRAY_BOUNDS_EXCEEDED";
                break;
            case EXCEPTION_DATATYPE_MISALIGNMENT:
                exceptionName = "DATATYPE_MISALIGNMENT";
                break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                exceptionName = "FLOAT_DIVIDE_BY_ZERO";
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                exceptionName = "INTEGER_DIVIDE_BY_ZERO";
                break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                exceptionName = "ILLEGAL_INSTRUCTION";
                break;
            case EXCEPTION_IN_PAGE_ERROR:
                exceptionName = "IN_PAGE_ERROR";
                break;
            case EXCEPTION_PRIV_INSTRUCTION:
                exceptionName = "PRIVILEGED_INSTRUCTION";
                break;
        }
        fprintf(stderr, "main: WINDOWS EXCEPTION - Code: 0x%08lX (%s)\n", 
                ExceptionInfo->ExceptionRecord->ExceptionCode, exceptionName);
        fprintf(stderr, "main: WINDOWS EXCEPTION - Address: 0x%p\n", 
                ExceptionInfo->ExceptionRecord->ExceptionAddress);
    }
    
    // Perform emergency cleanup
    performEmergencyCrashCleanup();
    
    fprintf(stderr, "main: WINDOWS EXCEPTION - Cleanup complete\n");
    fprintf(stderr, "=== END WINDOWS EXCEPTION HANDLER ===\n\n");
    fflush(stderr);
    
    // Continue with default exception handling (will likely terminate)
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Function to install crash handlers (Windows-specific)
void installCrashHandlers()
{
    qDebug() << "main: Installing Windows crash handlers...";
    
    // SECURITY: MMDiary is Windows-only, so we always use Windows-specific handlers
    // Install signal handlers for signals supported by Windows
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
    signal(SIGTERM, crashSignalHandler);
    signal(SIGILL, crashSignalHandler);
    signal(SIGFPE, crashSignalHandler);
    signal(SIGINT, crashSignalHandler);
    
    qDebug() << "main: Windows signal handlers installed";
    
    // Install Windows structured exception handler for comprehensive crash handling
    SetUnhandledExceptionFilter(windowsExceptionHandler);
    qDebug() << "main: Windows exception handler installed";
    
    qDebug() << "main: Crash handlers installed successfully";
}

int main(int argc, char *argv[])
{
    // Initialize Qt first to enable GUI dialogs
    QApplication a(argc, argv);
    
    // SECURITY: OS Platform Check - MMDiary is Windows-only
    #ifndef Q_OS_WIN
    qCritical() << "main: MMDiary launched on non-Windows platform - blocking execution";
    QMessageBox::critical(nullptr, "Platform Not Supported", 
                         "MMDiary is only available on Windows.\n\n"
                         "This application requires Windows-specific features and security mechanisms "
                         "that are not available on other platforms.");
    return 1; // Exit with error code
    #endif
    
    // Install crash handlers after Qt initialization but before any risky operations
    // This ensures they're active for all application-specific code
    installCrashHandlers();
    
    // SECURITY: Verify we're on Windows before proceeding with Windows-specific initialization
    qDebug() << "main: Running on Windows - proceeding with initialization";
    
    // Properly initialize OpenSSL 3.x with all required components
    // SECURITY: Initialize SSL and crypto libraries with proper error strings
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | 
                     OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
                     OPENSSL_INIT_ADD_ALL_CIPHERS |
                     OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
                       OPENSSL_INIT_ADD_ALL_CIPHERS |
                       OPENSSL_INIT_ADD_ALL_DIGESTS |
                       OPENSSL_INIT_LOAD_CONFIG, NULL);
    
    // Initialize error strings for proper error reporting
    ERR_load_crypto_strings();
    
    #ifdef QT_DEBUG
    // Verify OpenSSL initialization in debug mode
    const EVP_CIPHER *cipher = EVP_aes_256_gcm();
    if (!cipher) {
        qCritical() << "main: AES-256-GCM cipher not available in OpenSSL";
        return -1;
    }
    if (EVP_CIPHER_key_length(cipher) != 32) {
        qCritical() << "main: AES-256-GCM key length mismatch";
        return -1;
    }
    qDebug() << "main: OpenSSL initialized successfully with AES-256-GCM support";
    #endif

    QDir appDir(QCoreApplication::applicationDirPath());
    if (!appDir.exists("Data")) {
        qDebug() << "Data directory doesn't exist, creating it...";
        if (!appDir.mkdir("Data")) {
            qCritical() << "Failed to create Data directory at:" << appDir.absoluteFilePath("Data");
        } else {
            qDebug() << "Data directory created successfully at:" << appDir.absoluteFilePath("Data");
        }
    } else {
        qDebug() << "Data directory already exists at:" << appDir.absoluteFilePath("Data");
    }

    // SECURITY: Cleanup any leftover resources from previous runs
    OperationsFiles::cleanupAllUserTempFolders();
    
    // SECURITY: Clear any stale grace periods from previous app instances
    // This prevents memory leaks and ensures fresh state
    PasswordValidation::clearGracePeriod();
    qDebug() << "main: Cleared stale grace periods from previous sessions";

    a.setQuitOnLastWindowClosed(false); // prevents closing the app on last window closed.
    a.setStyle("Fusion");
    QPalette pal_dark;

    #ifdef QT_DEBUG
    qDebug() << "Running in Debug mode";
    #else
    qDebug() << "Running in Release mode";
    //socket system to track application and not allow duplicate. disabled when working on the app

    // Check if another instance is already running
    QLocalSocket socket;
    socket.connectToServer(APP_ID);
    if (socket.waitForConnected(500)) {
        // Another instance exists, send "show" message
        socket.write("SHOW");
        socket.waitForBytesWritten(1000);
        socket.close();
        qDebug() << "Application instance already running. Exiting.";
        return 0; // Exit the application
    }

    // If we get here, this is the first instance
    // Set up a local server to listen for other instances
    QLocalServer* server = new QLocalServer(&a);

    // Make sure no previous server instance is left over
    QLocalServer::removeServer(APP_ID);

    if (!server->listen(APP_ID)) {
        qDebug() << "Failed to start local server:" << server->errorString();
    }

    // Connect the local server to the app so we can find the MainWindow later

    QObject::connect(server, &QLocalServer::newConnection, [&]() {
        QLocalSocket* socket = server->nextPendingConnection();
        if (socket->waitForReadyRead(1000)) {
            QByteArray message = socket->readAll();
            if (message == "SHOW") {
                // Find the MainWindow to show it
                // This is a bit tricky since we start with loginscreen
                // We'll need to iterate through all top-level widgets
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    // Check if this is a MainWindow
                    MainWindow* mainWindow = qobject_cast<MainWindow*>(widget);
                    if (mainWindow) {
                        mainWindow->showAndActivate();
                        break;
                    }
                }
            }
        }
        socket->close();
        socket->deleteLater();
    });
    #endif

    // Register cleanup handler for OpenSSL and tray icons
    auto cleanup_handler = []() {
        #ifdef QT_DEBUG
        qDebug() << "main: Application cleanup handler triggered";
        #endif
        
        // SECURITY: Clean up any visible tray icons to prevent zombies
        // Note: This is a best-effort cleanup for abnormal terminations
        // The MainWindow destructor handles normal cleanup
        // QSystemTrayIcon cleanup is handled per-window in MainWindow destructor
        QApplication::processEvents(); // Force processing
        
        // Clean up OpenSSL resources
        EVP_cleanup();
        ERR_free_strings();
        CRYPTO_cleanup_all_ex_data();
        OPENSSL_cleanup();
    };
    
    // Connect cleanup to application aboutToQuit signal
    QObject::connect(&a, &QApplication::aboutToQuit, cleanup_handler);
    
    loginscreen w;

    //set dark theme palette
    pal_dark.setColor(QPalette::Window, QColor(53,53,53));
    pal_dark.setColor(QPalette::WindowText, Qt::white);
    pal_dark.setColor(QPalette::Base, QColor(25,25,25));
    pal_dark.setColor(QPalette::AlternateBase, QColor(53,53,53));
    pal_dark.setColor(QPalette::ToolTipBase, Qt::black);
    pal_dark.setColor(QPalette::ToolTipText, Qt::white);
    pal_dark.setColor(QPalette::Text, Qt::white);
    //pal_dark.setColor(QPalette::Disabled, QPalette::Text, QColor(25,25,25));   // fixes the ugly color of disabled context menu items. Need to override datestamp and timestamp colors before activating this code.
    pal_dark.setColor(QPalette::Button, QColor(53, 53, 53));
    pal_dark.setColor(QPalette::ButtonText, Qt::white);
    pal_dark.setColor(QPalette::BrightText, Qt::red);
    pal_dark.setColor(QPalette::Link, QColor(42, 130, 218));
    pal_dark.setColor(QPalette::Highlight, QColor(35, 35, 35));
    pal_dark.setColor(QPalette::HighlightedText, QColor(255,255,255));
    pal_dark.setColor(QPalette::PlaceholderText, QColor(100,100,100));
    a.setPalette(pal_dark); // set application palette to dark theme
    //qtOldMsgHandler = qInstallMessageHandler(fileMessageHandler); // for writing debug to text file
    w.show();
    return a.exec();
}
