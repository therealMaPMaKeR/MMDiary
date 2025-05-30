QT       += core gui
QT += core sql
QT += network


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# OpenSSL configuration for Windows
win32 {
    OPENSSL_PATH = $$PWD/3rdparty/openssl

    CONFIG(debug, debug|release) {
        # Debug mode: Use system OpenSSL DLLs
        message("Debug build: Using system OpenSSL at C:/Projects/OpenSSL-Win64")

        # Use system OpenSSL headers and libraries
        INCLUDEPATH += "C:/Projects/OpenSSL-Win64/include"
        #LIBS += -L"C:/Projects/OpenSSL-Win64/lib" -llibssl -llibcrypto
        #LIBS += -L"C:/Projects/OpenSSL-Win64/lib" -lssl -lcrypto
        LIBS += -L"C:/Projects/OpenSSL-Win64/lib/VC/x64/MDd" -llibssl -llibcrypto

        # Windows dependencies
        LIBS += -lUser32 -lAdvapi32 -lGdi32 -lCrypt32 -lWs2_32

        # No OPENSSL_STATIC for dynamic linking
    }

    CONFIG(release, debug|release) {
        # Release mode: Use your bundled static libraries
        message("Release build: Using bundled static OpenSSL")

        !exists($$OPENSSL_PATH/lib/VC/x64/MD/libssl_static.lib) {
            error("OpenSSL static libraries not found.")
        }

        INCLUDEPATH += $$OPENSSL_PATH/include
        LIBS += -L$$OPENSSL_PATH/lib/VC/x64/MD -llibssl_static -llibcrypto_static
        LIBS += -lUser32 -lAdvapi32 -lGdi32 -lCrypt32 -lWs2_32
        DEFINES += OPENSSL_STATIC
    }
}
win32:LIBS += -lole32 -lshell32 -luuid
#win32:QMAKE_LFLAGS += /NODEFAULTLIB:MSVCRT
CONFIG(release, debug|release): DEFINES += QT_NO_DEBUG_OUTPUT QT_NO_WARNING_OUTPUT
TEMPLATE = app
#CONFIG += console
win32:RC_FILE = app.rc
SOURCES += \
    CustomWidgets/CombinedDelegate.cpp \
    CustomWidgets/custom_QListWidget.cpp \
    CustomWidgets/custom_QTextEditWidget.cpp \
    CustomWidgets/custom_qcheckboxwidget.cpp \
    CustomWidgets/custom_qlistwidget_task.cpp \
    CustomWidgets/custom_qtabwidget_main.cpp \
    CustomWidgets/encryptedfileitemwidget.cpp \
    Operations-Features/operations_diary.cpp \
    Operations-Features/operations_encrypteddata.cpp \
    Operations-Features/operations_passwordmanager.cpp \
    Operations-Features/operations_settings.cpp \
    Operations-Features/operations_tasklists.cpp \
    Operations-Global/CryptoUtils.cpp \
    Operations-Global/default_usersettings.cpp \
    Operations-Global/encryptedfilemetadata.cpp \
    Operations-Global/fileiconprovider.cpp \
    Operations-Global/inputvalidation.cpp \
    Operations-Global/operations.cpp \
    Operations-Global/operations_files.cpp \
    Operations-Global/passwordvalidation.cpp \
    Operations-Global/sqlite-database-auth.cpp \
    Operations-Global/sqlite-database-impl.cpp \
    QT_AESGCM256/aesgcm256.cpp \
    changepassword.cpp \
    constants.cpp \
    editencryptedfiledialog.cpp \
    loginscreen.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    CustomWidgets/CombinedDelegate.h \
    CustomWidgets/custom_QListWidget.h \
    CustomWidgets/custom_QTextEditWidget.h \
    CustomWidgets/custom_qcheckboxwidget.h \
    CustomWidgets/custom_qlistwidget_task.h \
    CustomWidgets/custom_qtabwidget_main.h \
    CustomWidgets/encryptedfileitemwidget.h \
    Operations-Features/operations_diary.h \
    Operations-Features/operations_encrypteddata.h \
    Operations-Features/operations_passwordmanager.h \
    Operations-Features/operations_settings.h \
    Operations-Features/operations_tasklists.h \
    Operations-Global/CryptoUtils.h \
    Operations-Global/default_usersettings.h \
    Operations-Global/encryptedfilemetadata.h \
    Operations-Global/fileiconprovider.h \
    Operations-Global/inputvalidation.h \
    Operations-Global/operations.h \
    Operations-Global/operations_files.h \
    Operations-Global/passwordvalidation.h \
    Operations-Global/sqlite-database-auth.h \
    Operations-Global/sqlite-database-handler.h \
    QT_AESGCM256/aesgcm256.h \
    changepassword.h \
    constants.h \
    editencryptedfiledialog.h \
    loginscreen.h \
    mainwindow.h

FORMS += \
    Operations-Features/passwordmanager_addpassword.ui \
    Operations-Features/tasklists_addtask.ui \
    Operations-Global/passwordvalidation.ui \
    about_MMDiary.ui \
    changelog.ui \
    changepassword.ui \
    editencryptedfiledialog.ui \
    loginscreen.ui \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES +=

RESOURCES += \
    resources.qrc

