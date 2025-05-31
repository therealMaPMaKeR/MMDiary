#include "default_usersettings.h"
#include "sqlite-database-settings.h"
#include "../constants.h"

namespace Default_UserSettings{
bool SetDefault_GlobalSettings(QString username, const QByteArray& encryptionKey)
{
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "Failed to connect to settings database:" << db.lastError();
        qDebug() << "Unable to set default Global settings";
        return false;
    }

    if(username.isEmpty()){
        qDebug() << "Unable to set default Global settings - username is empty";
        return false;
    }

    // Update global settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Displayname, username)) {
        qDebug() << "Failed to set displayname";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DisplaynameColor, DEFAULT_DISPLAY_NAME_COLOR)) {
        qDebug() << "Failed to set displayname color";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_MinToTray, DEFAULT_MIN_TO_TRAY)) {
        qDebug() << "Failed to set min to tray";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_AskPWAfterMinToTray, DEFAULT_ASK_PW_AFTER_MIN)) {
        qDebug() << "Failed to set ask pw after min";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_ReqPWDelay, DEFAULT_REQ_PW_DELAY))
    {
        qDebug() << "Failed to set ReqPWDelay";
        return false;
    }
    return true;
}

bool SetDefault_DiarySettings(QString username, const QByteArray& encryptionKey)
{
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "Failed to connect to settings database:" << db.lastError();
        qDebug() << "Unable to set default settings for Diary";
        return false;
    }

    if(username.isEmpty()){
        qDebug() << "Unable to set default settings for Diary - username is empty";
        return false;
    }

    // Update diary settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TextSize, DEFAULT_DIARY_TEXT_SIZE)) {
        qDebug() << "Failed to set diary text size";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TStampTimer, DEFAULT_DIARY_TSTAMP_TIMER)) {
        qDebug() << "Failed to set diary tstamp timer";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TStampCounter, DEFAULT_DIARY_TSTAMP_COUNTER)) {
        qDebug() << "Failed to set diary tstamp counter";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_CanEditRecent, DEFAULT_DIARY_CAN_EDIT_RECENT)) {
        qDebug() << "Failed to set diary can edit recent";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_ShowTManLogs, DEFAULT_DIARY_SHOW_TMAN_LOGS)) {
        qDebug() << "Failed to set diary show tman logs";
        return false;
    }

    return true;
}

bool SetDefault_TasklistsSettings(QString username, const QByteArray& encryptionKey)
{
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "Failed to connect to settings database:" << db.lastError();
        qDebug() << "Unable to set default settings for Tasklists";
        return false;
    }

    if(username.isEmpty()){
        qDebug() << "Unable to set default settings for Tasklists - username is empty";
        return false;
    }

    // Update tasklists settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_TextSize, DEFAULT_TLISTS_TEXT_SIZE)) {
        qDebug() << "Failed to set tlists text size";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_LogToDiary, DEFAULT_TLISTS_LOG_TO_DIARY)) {
        qDebug() << "Failed to set tlists log to diary";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_TaskType, DEFAULT_TLISTS_TASK_TYPE)) {
        qDebug() << "Failed to set tlists task type";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_CMess, DEFAULT_TLISTS_CMESS)) {
        qDebug() << "Failed to set tlists cmess";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_PMess, DEFAULT_TLISTS_PMESS)) {
        qDebug() << "Failed to set tlists pmess";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_Notif, DEFAULT_TLISTS_NOTIF)) {
        qDebug() << "Failed to set tlists notif";
        return false;
    }

    return true;
}

bool SetDefault_PWManagerSettings(QString username, const QByteArray& encryptionKey)
{
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "Failed to connect to settings database:" << db.lastError();
        qDebug() << "Unable to set default settings for PWManager";
        return false;
    }

    if(username.isEmpty()){
        qDebug() << "Unable to set default settings for PWManager - username is empty";
        return false;
    }

    // Update password manager settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_DefSortingMethod, DEFAULT_PWMAN_DEF_SORTING_METHOD)) {
        qDebug() << "Failed to set pwman def sorting method";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_ReqPassword, DEFAULT_PWMAN_REQ_PASSWORD)) {
        qDebug() << "Failed to set pwman req password";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_HidePasswords, DEFAULT_PWMAN_HIDE_PASSWORDS)) {
        qDebug() << "Failed to set pwman hide passwords";
        return false;
    }

    return true;
}

bool SetDefault_EncryptedDataSettings(QString username, const QByteArray& encryptionKey)
{
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Ensure database connection
    if (!db.isConnected()) {
        if (!db.connect(username, encryptionKey)) {
            qDebug() << "Failed to connect to settings database for EncryptedData defaults";
            return false;
        }
    }

    // Start transaction
    if (!db.beginTransaction()) {
        qDebug() << "Failed to begin transaction for EncryptedData defaults";
        return false;
    }

    // Set all encrypted data defaults
    bool success = true;
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_ReqPassword, DEFAULT_DATAENC_REQ_PASSWORD);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_HideThumbnails_Image, DEFAULT_DATAENC_HIDE_THUMBNAILS_IMAGE);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_HideThumbnails_Video, DEFAULT_DATAENC_HIDE_THUMBNAILS_VIDEO);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hidden_Categories, DEFAULT_DATAENC_HIDDEN_CATEGORIES);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hidden_Tags, DEFAULT_DATAENC_HIDDEN_TAGS);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hide_Categories, DEFAULT_DATAENC_HIDE_CATEGORIES);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hide_Tags, DEFAULT_DATAENC_HIDE_TAGS);

    if (success) {
        db.commitTransaction();
        qDebug() << "EncryptedData settings reset to defaults successfully";
    } else {
        db.rollbackTransaction();
        qDebug() << "Failed to reset EncryptedData settings to defaults";
    }

    return success;
}

bool SetAllDefaults(QString username, const QByteArray& encryptionKey)
{
    if(SetDefault_GlobalSettings(username, encryptionKey) &&
        SetDefault_DiarySettings(username, encryptionKey) &&
        SetDefault_TasklistsSettings(username, encryptionKey) &&
        SetDefault_PWManagerSettings(username, encryptionKey) &&
        SetDefault_EncryptedDataSettings(username, encryptionKey))
    {
        return true;
    }
    return false;
}

} // end of namespace
