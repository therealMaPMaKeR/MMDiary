#include "constants.h"
#include <QRandomGenerator>
#include <QStringList>

namespace Constants
{
// Global
const QString AppVer = "4.5.3";
// Error Messages
const QString ErrorMessage_Default = "ERROR";
const QString ErrorMessage_INVUSER = "ERROR - INVALID USER";
// Settings Buttons
const QString SettingsButton_SaveGlobal = "SaveGlobal";
const QString SettingsButton_CancelGlobal = "CancelGlobal";
const QString SettingsButton_ResetGlobal = "ResetGlobal";
const QString SettingsButton_SaveDiary = "SaveDiary";
const QString SettingsButton_CancelDiary = "CancelDiary";
const QString SettingsButton_ResetDiary = "ResetDiary";
const QString SettingsButton_SaveTasklists = "SaveTasklists";
const QString SettingsButton_CancelTasklists = "CancelTasklists";
const QString SettingsButton_ResetTasklists = "ResetTasklists";
const QString SettingsButton_SavePWManager = "SavePWManager";
const QString SettingsButton_CancelPWManager = "CancelPWManager";
const QString SettingsButton_ResetPWManager = "ResetPWManager";
const QString SettingsButton_SaveEncryptedData = "SaveENCRYPTEDDATA";
const QString SettingsButton_CancelEncryptedData = "CancelENCRYPTEDDATA";
const QString SettingsButton_ResetEncryptedData = "ResetENCRYPTEDDATA";
// Data Types
const QString DataType_QString = "QString";
const QString DataType_QByteArray = "QByteArray";
const QString DataType_INT = "INT";
const QString DataType_BOOL = "BOOL";
// Settings Types
const QString DBSettings_Type_ALL = "ALL";
const QString DBSettings_Type_Global = "Global";
const QString DBSettings_Type_Diary = "Diary";
const QString DBSettings_Type_Tasklists = "Tasklists";
const QString DBSettings_Type_PWManager = "PWManager";
const QString DBSettings_Type_EncryptedData = "ENCRYPTEDDATA";
// User Database
const QString DBPath_User = "Data/users.db";
// User Database Table Indexes - User Info
const QString UserT_Index_Username = "username";
const QString UserT_Index_Password = "password";
const QString UserT_Index_EncryptionKey = "encryptionkey";
const QString UserT_Index_Salt = "salt";
const QString UserT_Index_Iterations = "iterations";
// User Database Table Indexes - Global Settings
const QString UserT_Index_Displayname = "displayname";
const QString UserT_Index_DisplaynameColor = "displaynamecolor";
const QString UserT_Index_MinToTray = "MinToTray";
const QString UserT_Index_AskPWAfterMinToTray = "AskPWAfterMinToTray";
// User Database Table Indexes - Diary Settings
const QString UserT_Index_Diary_TextSize = "Diary_TextSize";
const QString UserT_Index_Diary_TStampTimer = "Diary_TStampTimer";
const QString UserT_Index_Diary_TStampCounter = "Diary_TStampCounter";
const QString UserT_Index_Diary_CanEditRecent = "Diary_CanEditRecent";
const QString UserT_Index_Diary_ShowTManLogs = "Diary_ShowTManLogs";
// User Database Table Indexes - Tasklist Settings
const QString UserT_Index_TLists_LogToDiary = "TLists_LogToDiary";
const QString UserT_Index_TLists_TaskType = "TLists_TaskType";
const QString UserT_Index_TLists_CMess = "TLists_CMess";
const QString UserT_Index_TLists_PMess = "TLists_PMess";
const QString UserT_Index_TLists_Notif = "TLists_Notif";
const QString UserT_Index_TLists_TextSize = "TLists_TextSize";
// User Database Table Indexes - Password Manager Settings
const QString UserT_Index_PWMan_DefSortingMethod = "PWMan_DefSortingMethod";
const QString UserT_Index_PWMan_ReqPassword = "PWMan_ReqPassword";
const QString UserT_Index_PWMan_HidePasswords = "PWMan_HidePasswords";
// User Database Table Indexes - Encrypted Data Settings
const QString UserT_Index_DataENC_ReqPassword = "ENCRYPTEDDATA_ReqPassword";
// Settings Database Table Indexes - Global Settings
const QString SettingsT_Index_Displayname = "displayname";
const QString SettingsT_Index_DisplaynameColor = "displaynamecolor";
const QString SettingsT_Index_MinToTray = "MinToTray";
const QString SettingsT_Index_AskPWAfterMinToTray = "AskPWAfterMinToTray";
const QString SettingsT_Index_ReqPWDelay = "ReqPWDelay";
const QString SettingsT_Index_OpenOnSettings = "OpenOnSettings";
// Settings Database Table Indexes - Diary Settings
const QString SettingsT_Index_Diary_TextSize = "Diary_TextSize";
const QString SettingsT_Index_Diary_TStampTimer = "Diary_TStampTimer";
const QString SettingsT_Index_Diary_TStampCounter = "Diary_TStampCounter";
const QString SettingsT_Index_Diary_CanEditRecent = "Diary_CanEditRecent";
const QString SettingsT_Index_Diary_ShowTManLogs = "Diary_ShowTManLogs";
// Settings Database Table Indexes - Tasklist Settings
const QString SettingsT_Index_TLists_LogToDiary = "TLists_LogToDiary";
const QString SettingsT_Index_TLists_TaskType = "TLists_TaskType";
const QString SettingsT_Index_TLists_CMess = "TLists_CMess";
const QString SettingsT_Index_TLists_PMess = "TLists_PMess";
const QString SettingsT_Index_TLists_Notif = "TLists_Notif";
const QString SettingsT_Index_TLists_TextSize = "TLists_TextSize";
// Settings Database Table Indexes - Password Manager Settings
const QString SettingsT_Index_PWMan_DefSortingMethod = "PWMan_DefSortingMethod";
const QString SettingsT_Index_PWMan_ReqPassword = "PWMan_ReqPassword";
const QString SettingsT_Index_PWMan_HidePasswords = "PWMan_HidePasswords";
// Settings Database Table Indexes - Encrypted Data Settings
const QString SettingsT_Index_DataENC_ReqPassword = "ENCRYPTEDDATA_ReqPassword";
const QString SettingsT_Index_DataENC_HideThumbnails_Image = "ENCRYPTEDDATA_HideThumbnails_Image";
const QString SettingsT_Index_DataENC_HideThumbnails_Video = "ENCRYPTEDDATA_HideThumbnails_Video";
const QString SettingsT_Index_DataENC_Hidden_Categories = "ENCRYPTEDDATA_Hidden_Categories";
const QString SettingsT_Index_DataENC_Hidden_Tags = "ENCRYPTEDDATA_Hidden_Tags";
const QString SettingsT_Index_DataENC_Hide_Categories = "ENCRYPTEDDATA_Hide_Categories";
const QString SettingsT_Index_DataENC_Hide_Tags = "ENCRYPTEDDATA_Hide_Tags";
// User Database Table Indexes - Backup Management
const QString UserT_Index_BackupDeletionMode = "backup_deletion_mode";
const QString UserT_Index_BackupDeletionDate = "backup_deletion_date";
//------------- Persistent Settings Database -----------//
//Main window
const QString PSettingsT_Index_MainWindow_SizeX = "PSettings_MW_SizeX";
const QString PSettingsT_Index_MainWindow_SizeY = "PSettings_MW_SizeY";
const QString PSettingsT_Index_MainWindow_PosX = "PSettings_MW_PosX";
const QString PSettingsT_Index_MainWindow_PosY = "PSettings_MW_PosY";
//MainTabWidget Tab Indexes
const QString PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex = "PSettings_MTWI_CurrentTab";
const QString PSettingsT_Index_MainTabWidgetIndex_Settings = "PSettings_MTWI_Settings";
const QString PSettingsT_Index_MainTabWidgetIndex_Diary = "PSettings_MTWI_Diary";
const QString PSettingsT_Index_MainTabWidgetIndex_Tasklists = "PSettings_MTWI_Tasklists";
const QString PSettingsT_Index_MainTabWidgetIndex_PWManager = "PSettings_MTWI_PWManager";
const QString PSettingsT_Index_MainTabWidgetIndex_EncryptedData = "PSettings_MTWI_EncryptedData";
const QString PSettingsT_Index_MainTabWidgetIndex_VideoPlayer = "PSettings_MTWI_VideoPlayer";
//Tab Visibility
const QString PSettingsT_Index_TabVisible_Diaries = "PSettings_TabVisible_Diaries";
const QString PSettingsT_Index_TabVisible_Tasklists = "PSettings_TabVisible_Tasklists";
const QString PSettingsT_Index_TabVisible_Passwords = "PSettings_TabVisible_Passwords";
const QString PSettingsT_Index_TabVisible_DataEncryption = "PSettings_TabVisible_DataEncryption";
const QString PSettingsT_Index_TabVisible_Settings = "PSettings_TabVisible_Settings";
const QString PSettingsT_Index_TabVisible_VideoPlayer = "PSettings_TabVisible_VideoPlayer";
//Tasklists
const QString PSettingsT_Index_TLists_CurrentList = "PSettings_TLists_CurrentList";
const QString PSettingsT_Index_TLists_CurrentTask = "PSettings_TLists_CurrentTask";
const QString PSettingsT_Index_TLists_FoldedCategories = "PSettings_TLists_FoldedCategories";
//Password Manager
//Settings
//Encrypted Data
//Encrypted Data
const QString PSettingsT_Index_DataENC_CurrentCategory = "PSettings_DataENC_CurrentCategory";
const QString PSettingsT_Index_DataENC_CurrentTags = "PSettings_DataENC_CurrentTags";
const QString PSettingsT_Index_DataENC_SortType = "PSettings_DataENC_SortType";
const QString PSettingsT_Index_DataENC_TagSelectionMode = "PSettings_DataENC_TagSelectionMode";
//VideoPlayer
const QString PSettingsT_Index_VP_Shows_ShowsListViewMode = "PSettings_VP_Shows_ShowsListViewMode";
const QString PSettingsT_Index_VP_Shows_CurrentShow = "PSettings_VP_Shows_CurrentShow";
//-------------- Diary Text Codes ---------------//
const QString Diary_Spacer = "<!spacer!>";
const QString Diary_TextBlockStart = "<!TextBlockStart!>";
const QString Diary_TextBlockEnd = "<!TextBlockEnd!>";
const QString Diary_TimeStampStart = "<!TimeStampStart!>";
const QString Diary_TaskManagerStart = "<!TaskManagerStart!>";
const QString Diary_ImageStart = "<!ImageStart!>";
const QString Diary_ImageEnd = "<!ImageEnd!>";
//Diary
const QString TASK_MANAGER_TEXT = "Task Manager";
// Encrypted File Metadata
const int METADATA_RESERVED_SIZE = 51200; // 50KB reserved for metadata (fixed size)
const int MAX_RAW_METADATA_SIZE = 40960; // 40KB limit for raw metadata before encryption


}
