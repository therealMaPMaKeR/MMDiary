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
extern const QString SettingsButton_SaveVPShows;
extern const QString SettingsButton_CancelVPShows;
extern const QString SettingsButton_ResetVPShows;
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
extern const QString DBSettings_Type_VPShows;
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
extern const QString SettingsT_Index_ReqPWDelay;
extern const QString SettingsT_Index_OpenOnSettings;
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
extern const QString SettingsT_Index_DataENC_HideThumbnails_Image;
extern const QString SettingsT_Index_DataENC_HideThumbnails_Video;
extern const QString SettingsT_Index_DataENC_Hidden_Categories;
extern const QString SettingsT_Index_DataENC_Hidden_Tags;
extern const QString SettingsT_Index_DataENC_Hide_Categories;
extern const QString SettingsT_Index_DataENC_Hide_Tags;
// Settings Database Table Indexes - VideoPlayer Settings
extern const QString SettingsT_Index_VP_Shows_Autoplay;
extern const QString SettingsT_Index_VP_Shows_AutoplayRand;
extern const QString SettingsT_Index_VP_Shows_UseTMDB;
extern const QString SettingsT_Index_VP_Shows_DisplayFilenames;
extern const QString SettingsT_Index_VP_Shows_CheckNewEP;
extern const QString SettingsT_Index_VP_Shows_FileFolderParsing;
extern const QString SettingsT_Index_VP_Shows_AutoDelete;
extern const QString SettingsT_Index_VP_Shows_DefaultVolume;
extern const QString SettingsT_Index_VP_Shows_CheckNewEPStartup;
// User Database Table Indexes - Backup Management
extern const QString UserT_Index_BackupDeletionMode;
extern const QString UserT_Index_BackupDeletionDate;
//------------- Persistent Settings Database -----------//
//Main window
extern const QString PSettingsT_Index_MainWindow_SizeX;
extern const QString PSettingsT_Index_MainWindow_SizeY;
extern const QString PSettingsT_Index_MainWindow_PosX;
extern const QString PSettingsT_Index_MainWindow_PosY;
//MainTabWidget Tab Indexes
extern const QString PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex;
extern const QString PSettingsT_Index_MainTabWidgetIndex_Settings;
extern const QString PSettingsT_Index_MainTabWidgetIndex_Diary;
extern const QString PSettingsT_Index_MainTabWidgetIndex_Tasklists;
extern const QString PSettingsT_Index_MainTabWidgetIndex_PWManager;
extern const QString PSettingsT_Index_MainTabWidgetIndex_EncryptedData;
extern const QString PSettingsT_Index_MainTabWidgetIndex_VideoPlayer;
//Tab Visibility
extern const QString PSettingsT_Index_TabVisible_Diaries;
extern const QString PSettingsT_Index_TabVisible_Tasklists;
extern const QString PSettingsT_Index_TabVisible_Passwords;
extern const QString PSettingsT_Index_TabVisible_DataEncryption;
extern const QString PSettingsT_Index_TabVisible_Settings;
extern const QString PSettingsT_Index_TabVisible_VideoPlayer;
//Tasklists
extern const QString PSettingsT_Index_TLists_CurrentList;
extern const QString PSettingsT_Index_TLists_CurrentTask;
extern const QString PSettingsT_Index_TLists_FoldedCategories;
//Password Manager
//Settings
//Encrypted Data
extern const QString PSettingsT_Index_DataENC_CurrentCategory;
extern const QString PSettingsT_Index_DataENC_CurrentTags;
extern const QString PSettingsT_Index_DataENC_SortType;
extern const QString PSettingsT_Index_DataENC_TagSelectionMode;
//VideoPlayer
extern const QString PSettingsT_Index_VP_Shows_ShowsListViewMode;
extern const QString PSettingsT_Index_VP_Shows_CurrentShow;
//-------------- Diary Text Codes ---------------//
extern const QString Diary_Spacer;
extern const QString Diary_TextBlockStart;
extern const QString Diary_TextBlockEnd;
extern const QString Diary_TimeStampStart;
extern const QString Diary_TaskManagerStart;
extern const QString Diary_ImageStart;
extern const QString Diary_ImageEnd;
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
}
#endif // CONSTANTS_H
