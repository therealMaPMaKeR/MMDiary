#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QIcon>
#include <QDir>
#include <QLocalSocket>
#include <QLocalServer>
#include "loginscreen.h"
#include "mainwindow.h"
#include "constants.h"
#include <QFile>
#include <QtDebug>
#include <openssl/ssl.h>

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

int main(int argc, char *argv[])
{
    OPENSSL_init_ssl(0, NULL);
    QApplication a(argc, argv);
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

    //socket system to track application and not allow duplicate. disabled when working on the app
    // disabled when working on the app
    //
    return a.exec();
}
