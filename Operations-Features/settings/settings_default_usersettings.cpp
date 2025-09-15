#include "settings_default_usersettings.h"
#include "sqlite-database-settings.h"
#include "../constants.h"
#include "inputvalidation.h"

namespace Default_UserSettings{
bool SetDefault_GlobalSettings(const QString& username, const QByteArray& encryptionKey)
{
    // SECURITY: Validate username before any operations
    if(username.isEmpty()){
        qDebug() << "settings_default_usersettings: Unable to set default Global settings - username is empty";
        return false;
    }
    
    // SECURITY: Validate username format to prevent injection
    InputValidation::ValidationResult usernameResult = InputValidation::validateInput(
        username, InputValidation::InputType::Username, 20);
    if (!usernameResult.isValid) {
        qDebug() << "settings_default_usersettings: Invalid username:" << usernameResult.errorMessage;
        return false;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "settings_default_usersettings: Failed to connect to settings database:" << db.lastError();
        qDebug() << "settings_default_usersettings: Unable to set default Global settings";
        return false;
    }

    // Update global settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Displayname, username)) {
        qDebug() << "settings_default_usersettings: Failed to set displayname";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DisplaynameColor, DEFAULT_DISPLAY_NAME_COLOR)) {
        qDebug() << "settings_default_usersettings: Failed to set displayname color";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_MinToTray, DEFAULT_MIN_TO_TRAY)) {
        qDebug() << "settings_default_usersettings: Failed to set min to tray";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_AskPWAfterMinToTray, DEFAULT_ASK_PW_AFTER_MIN)) {
        qDebug() << "settings_default_usersettings: Failed to set ask pw after min";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_ReqPWDelay, DEFAULT_REQ_PW_DELAY))
    {
        qDebug() << "settings_default_usersettings: Failed to set ReqPWDelay";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_OpenOnSettings, DEFAULT_OPEN_ON_SETTINGS)) {
        qDebug() << "settings_default_usersettings: Failed to set open on settings";
        return false;
    }
    return true;
}

bool SetDefault_DiarySettings(const QString& username, const QByteArray& encryptionKey)
{
    // SECURITY: Validate username before any operations
    if(username.isEmpty()){
        qDebug() << "settings_default_usersettings: Unable to set default settings for Diary - username is empty";
        return false;
    }
    
    // SECURITY: Validate username format to prevent injection
    InputValidation::ValidationResult usernameResult = InputValidation::validateInput(
        username, InputValidation::InputType::Username, 20);
    if (!usernameResult.isValid) {
        qDebug() << "settings_default_usersettings: Invalid username:" << usernameResult.errorMessage;
        return false;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "settings_default_usersettings: Failed to connect to settings database:" << db.lastError();
        qDebug() << "settings_default_usersettings: Unable to set default settings for Diary";
        return false;
    }

    // Update diary settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TextSize, DEFAULT_DIARY_TEXT_SIZE)) {
        qDebug() << "settings_default_usersettings: Failed to set diary text size";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TStampTimer, DEFAULT_DIARY_TSTAMP_TIMER)) {
        qDebug() << "settings_default_usersettings: Failed to set diary tstamp timer";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TStampCounter, DEFAULT_DIARY_TSTAMP_COUNTER)) {
        qDebug() << "settings_default_usersettings: Failed to set diary tstamp counter";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_CanEditRecent, DEFAULT_DIARY_CAN_EDIT_RECENT)) {
        qDebug() << "settings_default_usersettings: Failed to set diary can edit recent";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_ShowTManLogs, DEFAULT_DIARY_SHOW_TMAN_LOGS)) {
        qDebug() << "settings_default_usersettings: Failed to set diary show tman logs";
        return false;
    }

    return true;
}

bool SetDefault_TasklistsSettings(const QString& username, const QByteArray& encryptionKey)
{
    // SECURITY: Validate username before any operations
    if(username.isEmpty()){
        qDebug() << "settings_default_usersettings: Unable to set default settings for Tasklists - username is empty";
        return false;
    }
    
    // SECURITY: Validate username format to prevent injection
    InputValidation::ValidationResult usernameResult = InputValidation::validateInput(
        username, InputValidation::InputType::Username, 20);
    if (!usernameResult.isValid) {
        qDebug() << "settings_default_usersettings: Invalid username:" << usernameResult.errorMessage;
        return false;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "settings_default_usersettings: Failed to connect to settings database:" << db.lastError();
        qDebug() << "settings_default_usersettings: Unable to set default settings for Tasklists";
        return false;
    }

    // Update tasklists settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_TextSize, DEFAULT_TLISTS_TEXT_SIZE)) {
        qDebug() << "settings_default_usersettings: Failed to set tlists text size";
        return false;
    }

    return true;
}

bool SetDefault_PWManagerSettings(const QString& username, const QByteArray& encryptionKey)
{
    // SECURITY: Validate username before any operations
    if(username.isEmpty()){
        qDebug() << "settings_default_usersettings: Unable to set default settings for PWManager - username is empty";
        return false;
    }
    
    // SECURITY: Validate username format to prevent injection
    InputValidation::ValidationResult usernameResult = InputValidation::validateInput(
        username, InputValidation::InputType::Username, 20);
    if (!usernameResult.isValid) {
        qDebug() << "settings_default_usersettings: Invalid username:" << usernameResult.errorMessage;
        return false;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Connect to the settings database
    if (!db.connect(username, encryptionKey)) {
        qCritical() << "settings_default_usersettings: Failed to connect to settings database:" << db.lastError();
        qDebug() << "settings_default_usersettings: Unable to set default settings for PWManager";
        return false;
    }

    // Update password manager settings using constants
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_DefSortingMethod, DEFAULT_PWMAN_DEF_SORTING_METHOD)) {
        qDebug() << "settings_default_usersettings: Failed to set pwman def sorting method";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_ReqPassword, DEFAULT_PWMAN_REQ_PASSWORD)) {
        qDebug() << "settings_default_usersettings: Failed to set pwman req password";
        return false;
    }
    if (!db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_HidePasswords, DEFAULT_PWMAN_HIDE_PASSWORDS)) {
        qDebug() << "settings_default_usersettings: Failed to set pwman hide passwords";
        return false;
    }

    return true;
}

bool SetDefault_EncryptedDataSettings(const QString& username, const QByteArray& encryptionKey)
{
    // SECURITY: Validate username before any operations
    if(username.isEmpty()){
        qDebug() << "settings_default_usersettings: Unable to set default settings for EncryptedData - username is empty";
        return false;
    }
    
    // SECURITY: Validate username format to prevent injection
    InputValidation::ValidationResult usernameResult = InputValidation::validateInput(
        username, InputValidation::InputType::Username, 20);
    if (!usernameResult.isValid) {
        qDebug() << "settings_default_usersettings: Invalid username:" << usernameResult.errorMessage;
        return false;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Ensure database connection
    if (!db.isConnected()) {
        if (!db.connect(username, encryptionKey)) {
            qDebug() << "settings_default_usersettings: Failed to connect to settings database for EncryptedData defaults";
            return false;
        }
    }

    // Start transaction
    if (!db.beginTransaction()) {
        qDebug() << "settings_default_usersettings: Failed to begin transaction for EncryptedData defaults";
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
        qDebug() << "settings_default_usersettings: EncryptedData settings reset to defaults successfully";
    } else {
        db.rollbackTransaction();
        qDebug() << "settings_default_usersettings: Failed to reset EncryptedData settings to defaults";
    }

    return success;
}

bool SetDefault_VideoPlayerSettings(const QString& username, const QByteArray& encryptionKey)
{
    // SECURITY: Validate username before any operations
    if(username.isEmpty()){
        qDebug() << "settings_default_usersettings: Unable to set default settings for VideoPlayer - username is empty";
        return false;
    }
    
    // SECURITY: Validate username format to prevent injection
    InputValidation::ValidationResult usernameResult = InputValidation::validateInput(
        username, InputValidation::InputType::Username, 20);
    if (!usernameResult.isValid) {
        qDebug() << "settings_default_usersettings: Invalid username:" << usernameResult.errorMessage;
        return false;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Ensure database connection
    if (!db.isConnected()) {
        if (!db.connect(username, encryptionKey)) {
            qDebug() << "settings_default_usersettings: Failed to connect to settings database for VideoPlayer defaults";
            return false;
        }
    }

    // Start transaction
    if (!db.beginTransaction()) {
        qDebug() << "settings_default_usersettings: Failed to begin transaction for VideoPlayer defaults";
        return false;
    }

    // Set all video player defaults
    bool success = true;
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_Autoplay, DEFAULT_VP_SHOWS_AUTOPLAY);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_AutoplayRand, DEFAULT_VP_SHOWS_AUTOPLAY_RAND);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_UseTMDB, DEFAULT_VP_SHOWS_USE_TMDB);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_DisplayFilenames, DEFAULT_VP_SHOWS_DISPLAY_FILENAMES);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_CheckNewEP, DEFAULT_VP_SHOWS_CHECK_NEW_EP);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_FileFolderParsing, DEFAULT_VP_SHOWS_FILE_FOLDER_PARSING);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_AutoDelete, DEFAULT_VP_SHOWS_AUTO_DELETE);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_DefaultVolume, DEFAULT_VP_SHOWS_DEFAULT_VOLUME);
    success &= db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_VP_Shows_CheckNewEPStartup, DEFAULT_VP_SHOWS_CHECK_NEW_EP_STARTUP);

    if (success) {
        db.commitTransaction();
        qDebug() << "settings_default_usersettings: VideoPlayer settings reset to defaults successfully";
    } else {
        db.rollbackTransaction();
        qDebug() << "settings_default_usersettings: Failed to reset VideoPlayer settings to defaults";
    }

    return success;
}

bool SetAllDefaults(const QString& username, const QByteArray& encryptionKey)
{
    if(SetDefault_GlobalSettings(username, encryptionKey) &&
        SetDefault_DiarySettings(username, encryptionKey) &&
        SetDefault_TasklistsSettings(username, encryptionKey) &&
        SetDefault_PWManagerSettings(username, encryptionKey) &&
        SetDefault_EncryptedDataSettings(username, encryptionKey) &&
        SetDefault_VideoPlayerSettings(username, encryptionKey))
    {
        return true;
    }
    return false;
}

} // end of namespace
