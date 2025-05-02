#include "default_usersettings.h"
#include "sqlite-database-handler.h"
#include "../constants.h"

namespace Default_UserSettings{
bool SetDefault_GlobalSettings(QString username)
{
    DatabaseManager& db = DatabaseManager::instance();
    // Connect to the database
    if (!db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        qDebug() << "Unable to set default Global settings";
        return false;
    }
    if(username.isEmpty()){qDebug() << "Unable to set default Global settings";return false;} // if username is empty, stop here.
    if(db.GetUserData_String(username,Constants::UserT_Index_Username) != Constants::ErrorMessage_INVUSER) // if username is valid
    {
        // Update global settings using constants
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_DisplaynameColor, DEFAULT_DISPLAY_NAME_COLOR);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_MinToTray, DEFAULT_MIN_TO_TRAY);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_AskPWAfterMinToTray, DEFAULT_ASK_PW_AFTER_MIN);
        return true;
    }
    qDebug() << "Unable to set default Global settings";
    return false;
}

bool SetDefault_DiarySettings(QString username)
{
    DatabaseManager& db = DatabaseManager::instance();
    // Connect to the database
    if (!db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        qDebug() << "Unable to set default settings for Diary";
        return false;
    }
    if(username.isEmpty()){qDebug() << "Unable to set default settings for Diary";return false;} // if username is empty, stop here.
    if(db.GetUserData_String(username,Constants::UserT_Index_Username) != Constants::ErrorMessage_INVUSER) // if username is valid
    {
        // Update diary settings using constants
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_TextSize, DEFAULT_DIARY_TEXT_SIZE);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_TStampTimer, DEFAULT_DIARY_TSTAMP_TIMER);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_TStampCounter, DEFAULT_DIARY_TSTAMP_COUNTER);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_CanEditRecent, DEFAULT_DIARY_CAN_EDIT_RECENT);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_ShowTManLogs, DEFAULT_DIARY_SHOW_TMAN_LOGS);
        return true;
    }
    qDebug() << "Unable to set default settings for Diary";
    return false;
}

bool SetDefault_TasklistsSettings(QString username)
{
    DatabaseManager& db = DatabaseManager::instance();
    // Connect to the database
    if (!db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        qDebug() << "Unable to set default settings for Tasklists";
        return false;
    }
    if(username.isEmpty()){qDebug() << "Unable to set default settings for Tasklists";return false;} // if username is empty, stop here.
    if(db.GetUserData_String(username,Constants::UserT_Index_Username) != Constants::ErrorMessage_INVUSER) // if username is valid
    {
        // Update tasklists settings using constants
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_TextSize, DEFAULT_TLISTS_TEXT_SIZE);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_LogToDiary, DEFAULT_TLISTS_LOG_TO_DIARY);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_TaskType, DEFAULT_TLISTS_TASK_TYPE);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_CMess, DEFAULT_TLISTS_CMESS);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_PMess, DEFAULT_TLISTS_PMESS);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_Notif, DEFAULT_TLISTS_NOTIF);
        return true;
    }
    qDebug() << "Unable to set default settings for Tasklists";
    return false;
}

bool SetDefault_PWManagerSettings(QString username)
{
    DatabaseManager& db = DatabaseManager::instance();
    // Connect to the database
    if (!db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        qDebug() << "Unable to set default settings for PWManager";
        return false;
    }
    if(username.isEmpty()){qDebug() << "Unable to set default settings for PWManager";return false;} // if username is empty, stop here.
    if(db.GetUserData_String(username,Constants::UserT_Index_Username) != Constants::ErrorMessage_INVUSER) // if username is valid
    {
        // Update password manager settings using constants
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_PWMan_DefSortingMethod, DEFAULT_PWMAN_DEF_SORTING_METHOD);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_PWMan_ReqPassword, DEFAULT_PWMAN_REQ_PASSWORD);
        db.UpdateUserData_TEXT(username, Constants::UserT_Index_PWMan_HidePasswords, DEFAULT_PWMAN_HIDE_PASSWORDS);
        return true;
    }
    qDebug() << "Unable to set default settings for PWManager";
    return false;
}

bool SetAllDefaults(QString username)
{
    if(SetDefault_GlobalSettings(username) && SetDefault_DiarySettings(username) && SetDefault_TasklistsSettings(username) && SetDefault_PWManagerSettings(username))
    {
        return true;
    }
    return false;
}

} // end of namespace
