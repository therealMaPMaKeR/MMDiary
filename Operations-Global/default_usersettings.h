#ifndef DEFAULT_USERSETTINGS_H
#define DEFAULT_USERSETTINGS_H
#include <QString>

class DatabaseManager;
namespace Default_UserSettings
{
// Global Settings
const QString DEFAULT_DISPLAY_NAME_COLOR = "Orange";
const QString DEFAULT_MIN_TO_TRAY = "1";
const QString DEFAULT_ASK_PW_AFTER_MIN = "1";

// Diary Settings
const QString DEFAULT_DIARY_TEXT_SIZE = "10";
const QString DEFAULT_DIARY_TSTAMP_TIMER = "5";
const QString DEFAULT_DIARY_TSTAMP_COUNTER = "5";
const QString DEFAULT_DIARY_CAN_EDIT_RECENT = "1";
const QString DEFAULT_DIARY_SHOW_TMAN_LOGS = "1";

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

bool SetDefault_GlobalSettings(QString username);
bool SetDefault_DiarySettings(QString username);
bool SetDefault_TasklistsSettings(QString username);
bool SetDefault_PWManagerSettings(QString username);
bool SetAllDefaults(QString username);
};

#endif // DEFAULT_USERSETTINGS_H
