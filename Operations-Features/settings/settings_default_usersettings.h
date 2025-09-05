#ifndef DEFAULT_USERSETTINGS_H
#define DEFAULT_USERSETTINGS_H
#include <QString>
#include <QByteArray>

class DatabaseSettingsManager;
namespace Default_UserSettings
{
// Global Settings
const QString DEFAULT_DISPLAY_NAME_COLOR = "Orange";
const QString DEFAULT_MIN_TO_TRAY = "1";
const QString DEFAULT_ASK_PW_AFTER_MIN = "1";
const QString DEFAULT_REQ_PW_DELAY = "30";
const QString DEFAULT_OPEN_ON_SETTINGS = "0";

// Diary Settings
const QString DEFAULT_DIARY_TEXT_SIZE = "10";
const QString DEFAULT_DIARY_TSTAMP_TIMER = "5";
const QString DEFAULT_DIARY_TSTAMP_COUNTER = "4";
const QString DEFAULT_DIARY_CAN_EDIT_RECENT = "1";
const QString DEFAULT_DIARY_SHOW_TMAN_LOGS = "0";

// Task Lists Settings
const QString DEFAULT_TLISTS_TEXT_SIZE = "10";
const QString DEFAULT_TLISTS_LOG_TO_DIARY = "0";
const QString DEFAULT_TLISTS_TASK_TYPE = "Simple";
const QString DEFAULT_TLISTS_CMESS = "None";
const QString DEFAULT_TLISTS_PMESS = "None";
const QString DEFAULT_TLISTS_NOTIF = "1";

// Password Manager Settings
const QString DEFAULT_PWMAN_DEF_SORTING_METHOD = "Password";
const QString DEFAULT_PWMAN_REQ_PASSWORD = "0";
const QString DEFAULT_PWMAN_HIDE_PASSWORDS = "0";

// Encrypted Data Settings
const QString DEFAULT_DATAENC_REQ_PASSWORD = "0";
const QString DEFAULT_DATAENC_HIDE_THUMBNAILS_IMAGE = "0";
const QString DEFAULT_DATAENC_HIDE_THUMBNAILS_VIDEO = "0";
const QString DEFAULT_DATAENC_HIDDEN_CATEGORIES = "";
const QString DEFAULT_DATAENC_HIDDEN_TAGS = "";
const QString DEFAULT_DATAENC_HIDE_CATEGORIES = "0";
const QString DEFAULT_DATAENC_HIDE_TAGS = "0";

// SECURITY: Pass QString by const reference to avoid unnecessary copies
bool SetDefault_GlobalSettings(const QString& username, const QByteArray& encryptionKey);
bool SetDefault_DiarySettings(const QString& username, const QByteArray& encryptionKey);
bool SetDefault_TasklistsSettings(const QString& username, const QByteArray& encryptionKey);
bool SetDefault_PWManagerSettings(const QString& username, const QByteArray& encryptionKey);
bool SetDefault_EncryptedDataSettings(const QString& username, const QByteArray& encryptionKey);
bool SetAllDefaults(const QString& username, const QByteArray& encryptionKey);
};

#endif // DEFAULT_USERSETTINGS_H
