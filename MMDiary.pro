QT       += core gui
QT += core sql
QT += network
QT += multimedia multimediawidgets


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# TMDB API Key Configuration (compile-time embedding)
# Read API key from file if it exists
TMDB_KEY_FILE = $$PWD/tmdb_api_key.txt
exists($TMDB_KEY_FILE) {
    TMDB_API_KEY = $cat($TMDB_KEY_FILE)
    # Remove any trailing whitespace or newlines
    TMDB_API_KEY = $replace(TMDB_API_KEY, \n, )
    TMDB_API_KEY = $replace(TMDB_API_KEY, \r, )
    # Define as preprocessor macro (properly escaped)
    DEFINES += TMDB_API_KEY=\"$TMDB_API_KEY\"
    message("TMDB API key loaded from tmdb_api_key.txt")
} else {
    # No API key file found - TMDB features will be disabled
    DEFINES += TMDB_API_KEY=\"\"
    message("Warning: tmdb_api_key.txt not found - TMDB integration will be disabled")
    message("Create tmdb_api_key.txt from tmdb_api_key.txt.example to enable TMDB")
}

# Add include paths for project directories
INCLUDEPATH += $$PWD \
               $$PWD/CustomWidgets \
               $$PWD/CustomWidgets/diary \
               $$PWD/CustomWidgets/encrypteddata \
               $$PWD/CustomWidgets/tasklists \
               $$PWD/Operations-Features \
               $$PWD/Operations-Features/diary \
               $$PWD/Operations-Features/encrypteddata \
               $$PWD/Operations-Features/passwordmanager \
               $$PWD/Operations-Features/settings \
               $$PWD/Operations-Features/tasklists \
               $$PWD/Operations-Features/videoplayer \
               $$PWD/Operations-Features/videoplayer/showsplayer \
               $$PWD/Operations-Global \
               $$PWD/Operations-Global/databases \
               $$PWD/Operations-Global/encryption \
               $$PWD/Operations-Global/encryption/QT_AESGCM256

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
    CustomWidgets/diary/CombinedDelegate.cpp \
    CustomWidgets/diary/qlist_DiaryTextDisplay.cpp \
    CustomWidgets/diary/qtextedit_DiaryTextInput.cpp \
    CustomWidgets/qcheckbox_PWValidation.cpp \
    CustomWidgets/qtab_Main.cpp \
    CustomWidgets/encrypteddata/encryptedfileitemwidget.cpp \
    CustomWidgets/tasklists/qlist_TasklistDisplay.cpp \
    Operations-Features/diary/operations_diary.cpp \
    Operations-Features/encrypteddata/operations_encrypteddata.cpp \
    Operations-Features/encrypteddata/encrypteddata_encryptionworkers.cpp \
    Operations-Features/encrypteddata/encrypteddata_progressdialogs.cpp \
    Operations-Features/passwordmanager/operations_passwordmanager.cpp \
    Operations-Features/settings/operations_settings.cpp \
    Operations-Features/tasklists/operations_tasklists.cpp \
    Operations-Features/videoplayer/showsplayer/operations_vp_shows.cpp \
    Operations-Features/videoplayer/showsplayer/operations_vp_shows_settings_handlers.cpp \
    Operations-Features/videoplayer/showsplayer/VP_Shows_Videoplayer.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_metadata.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_encryptionworkers.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_progressdialogs.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_tmdb.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_config.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_settings.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_settings_dialog.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_add_dialog.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_edit_metadata_dialog.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_watchhistory.cpp \
    Operations-Features/videoplayer/showsplayer/vp_shows_playback_tracker.cpp \
    Operations-Features/encrypteddata/encrypteddata_encryptedfilemetadata.cpp \
    Operations-Features/encrypteddata/encrypteddata_fileiconprovider.cpp \
    Operations-Features/encrypteddata/encrypteddata_editencryptedfiledialog.cpp \
    Operations-Features/settings/settings_default_usersettings.cpp \
    Operations-Features/settings/settings_changepassword.cpp \
    Operations-Global/imageviewer.cpp \
    Operations-Global/inputvalidation.cpp \
    Operations-Global/operations.cpp \
    Operations-Global/operations_files.cpp \
    Operations-Global/passwordvalidation.cpp \
    Operations-Global/databases/sqlite-database-auth.cpp \
    Operations-Global/databases/sqlite-database-impl.cpp \
    Operations-Global/databases/sqlite-database-persistentsettings.cpp \
    Operations-Global/databases/sqlite-database-settings.cpp \
    Operations-Global/encryption/CryptoUtils.cpp \
    Operations-Global/encryption/noncechecker.cpp \
    Operations-Global/encryption/QT_AESGCM256/aesgcm256.cpp \
    constants.cpp \
    loginscreen.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    CustomWidgets/diary/CombinedDelegate.h \
    CustomWidgets/diary/qlist_DiaryTextDisplay.h \
    CustomWidgets/diary/qtextedit_DiaryTextInput.h \
    CustomWidgets/qcheckbox_PWValidation.h \
    CustomWidgets/qtab_Main.h \
    CustomWidgets/encrypteddata/encryptedfileitemwidget.h \
    CustomWidgets/tasklists/qlist_TasklistDisplay.h \
    Operations-Features/diary/operations_diary.h \
    Operations-Features/encrypteddata/operations_encrypteddata.h \
    Operations-Features/encrypteddata/encrypteddata_encryptionworkers.h \
    Operations-Features/encrypteddata/encrypteddata_progressdialogs.h \
    Operations-Features/passwordmanager/operations_passwordmanager.h \
    Operations-Features/settings/operations_settings.h \
    Operations-Features/tasklists/operations_tasklists.h \
    Operations-Features/videoplayer/showsplayer/operations_vp_shows.h \
    Operations-Features/videoplayer/showsplayer/VP_Shows_Videoplayer.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_metadata.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_encryptionworkers.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_progressdialogs.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_tmdb.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_config.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_settings.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_settings_dialog.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_add_dialog.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_edit_metadata_dialog.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_watchhistory.h \
    Operations-Features/videoplayer/showsplayer/vp_shows_playback_tracker.h \
    Operations-Features/encrypteddata/encrypteddata_encryptedfilemetadata.h \
    Operations-Features/encrypteddata/encrypteddata_fileiconprovider.h \
    Operations-Features/encrypteddata/encrypteddata_editencryptedfiledialog.h \
    Operations-Features/settings/settings_default_usersettings.h \
    Operations-Features/settings/settings_changepassword.h \
    Operations-Global/imageviewer.h \
    Operations-Global/inputvalidation.h \
    Operations-Global/operations.h \
    Operations-Global/operations_files.h \
    Operations-Global/passwordvalidation.h \
    Operations-Global/databases/sqlite-database-auth.h \
    Operations-Global/databases/sqlite-database-handler.h \
    Operations-Global/databases/sqlite-database-persistentsettings.h \
    Operations-Global/databases/sqlite-database-settings.h \
    Operations-Global/encryption/CryptoUtils.h \
    Operations-Global/encryption/noncechecker.h \
    Operations-Global/encryption/QT_AESGCM256/aesgcm256.h \
    constants.h \
    loginscreen.h \
    mainwindow.h

FORMS += \
    HiddenItemsList.ui \
    Operations-Features/passwordmanager/passwordmanager_addpassword.ui \
    Operations-Features/videoplayer/showsplayer/vp_shows_add_dialog.ui \
    Operations-Features/videoplayer/showsplayer/vp_shows_edit_metadata_dialog.ui \
    Operations-Features/videoplayer/showsplayer/vp_shows_settings_dialog.ui \
    Operations-Features/tasklists/tasklists_addtask.ui \
    Operations-Global/imageviewer.ui \
    Operations-Global/passwordvalidation.ui \
    about_MMDiary.ui \
    changelog.ui \
    Operations-Features/encrypteddata/encrypteddata_editencryptedfiledialog.ui \
    Operations-Features/settings/settings_changepassword.ui \
    loginscreen.ui \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES +=

RESOURCES += \
    resources.qrc
