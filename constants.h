#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QString>

namespace Constants
{
//Global
extern const QString AppVer;
// Error Messages
extern const QString ErrorMessage_Default;
extern const QString ErrorMessage_INVUSER;
// Settings Buttons
extern const QString SettingsButton_SaveGlobal;
extern const QString SettingsButton_CancelGlobal;
extern const QString SettingsButton_ResetGlobal;
extern const QString SettingsButton_SaveDiary;
extern const QString SettingsButton_CancelDiary;
extern const QString SettingsButton_ResetDiary;
extern const QString SettingsButton_SaveTasklists;
extern const QString SettingsButton_CancelTasklists;
extern const QString SettingsButton_ResetTasklists;
extern const QString SettingsButton_SavePWManager;
extern const QString SettingsButton_CancelPWManager;
extern const QString SettingsButton_ResetPWManager;
extern const QString SettingsButton_SaveEncryptedData;
extern const QString SettingsButton_CancelEncryptedData;
extern const QString SettingsButton_ResetEncryptedData;
// Data Types
extern const QString DataType_QString;
extern const QString DataType_QByteArray;
extern const QString DataType_INT;
extern const QString DataType_BOOL;
// Settings Types
extern const QString DBSettings_Type_ALL;
extern const QString DBSettings_Type_Global;
extern const QString DBSettings_Type_Diary;
extern const QString DBSettings_Type_Tasklists;
extern const QString DBSettings_Type_PWManager;
extern const QString DBSettings_Type_EncryptedData;
// User Database
extern const QString DBPath_User;
// Users Database Table Indexes - User Info
extern const QString UserT_Index_Username;
extern const QString UserT_Index_Password;
extern const QString UserT_Index_EncryptionKey;
extern const QString UserT_Index_Salt;
extern const QString UserT_Index_Iterations;
// Settings Database Table Indexes - Global Settings
extern const QString SettingsT_Index_Displayname;
extern const QString SettingsT_Index_DisplaynameColor;
extern const QString SettingsT_Index_MinToTray;
extern const QString SettingsT_Index_AskPWAfterMinToTray;
// Settings Database Table Indexes - Diary Settings
extern const QString SettingsT_Index_Diary_TextSize;
extern const QString SettingsT_Index_Diary_TStampTimer;
extern const QString SettingsT_Index_Diary_TStampCounter;
extern const QString SettingsT_Index_Diary_CanEditRecent;
extern const QString SettingsT_Index_Diary_ShowTManLogs;
// Settings Database Table Indexes - Tasklist Settings
extern const QString SettingsT_Index_TLists_LogToDiary;
extern const QString SettingsT_Index_TLists_TaskType;
extern const QString SettingsT_Index_TLists_CMess;
extern const QString SettingsT_Index_TLists_PMess;
extern const QString SettingsT_Index_TLists_Notif;
extern const QString SettingsT_Index_TLists_TextSize;
// Settings Database Table Indexes - Password Manager Settings
extern const QString SettingsT_Index_PWMan_DefSortingMethod;
extern const QString SettingsT_Index_PWMan_ReqPassword;
extern const QString SettingsT_Index_PWMan_HidePasswords;
// Settings Database Table Indexes - Encrypted Data Settings
extern const QString SettingsT_Index_DataENC_ReqPassword;
// Diary Text Codes
extern const QString Diary_Spacer;
extern const QString Diary_TextBlockStart;
extern const QString Diary_TextBlockEnd;
extern const QString Diary_TimeStampStart;
extern const QString Diary_TaskManagerStart;
//Diary
extern const QString TASK_MANAGER_TEXT;
// Encrypted File Metadata
extern const int METADATA_RESERVED_SIZE;
extern const int MAX_RAW_METADATA_SIZE;
// Enum classes
enum class CPUNType {
    Congrat,
    Punish
};
enum class CPUNCategory {
    None,
    Simple,
    Advanced,
    Intense,
    Extreme
};
QString GetCPUNMessage(CPUNType type, CPUNCategory category);
}
#endif // CONSTANTS_H
