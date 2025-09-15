#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtWidgets/QMainWindow>
#include <QFile>
#include <QFont>
#include <QListWidget>
#include <QTimer>
#include <QModelIndex>
#include <QFileDialog>
#include <QTextStream>
#include <QDir>
#include <QDate>
#include <QKeyEvent>
#include <QClipboard>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QKeyEvent>
#include <QString>
#include <QChar>
#include <QSystemTrayIcon>
#include <QScreen>
#include "Operations-Global/encryption/SecureByteArray.h"
#include "qcheckbox_PWValidation.h"
#include "sqlite-database-handler.h" // everywhere that this is needed is somewhere that mainwindow is needed.// more practical to have this here
#include "sqlite-database-auth.h" // everywhere that this is needed is somewhere that mainwindow is needed.// more practical to have this here
#include "sqlite-database-settings.h" // same
#include "sqlite-database-persistentsettings.h" //same

//Forward Declarations
class PasswordValidation;
class ChangePassword;
class loginscreen;
class CombinedDelegate;
class qlist_DiaryTextDisplay;
class Operations_Diary;
class Operations_PasswordManager;
class Operations_TaskLists;
class Operations_EncryptedData;
class Operations_Settings;
class Operations_VP_Shows;

// Header's Class
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    friend class Operations_Diary;
    friend class Operations_PasswordManager;
    friend class Operations_TaskLists;
    friend class Operations_EncryptedData;
    friend class Operations_Settings;
    friend class Operations_VP_Shows;

    // Saved Settings
    QString user_Username;
    SecureByteArray user_Key;  // Changed from QByteArray for secure key storage
    QString user_Displayname, user_nameColor;

    // public functions
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

    // Bridge function to refresh encrypted data display when settings change
    void refreshEncryptedDataDisplay();

public slots:
    // Takes ownership of the SecureByteArray pointer
    void ReceiveDataLogin_Slot(QString username, SecureByteArray* key);
    void showAndActivate();
protected:
    void resizeEvent(QResizeEvent *event) override;
private slots:
    void FinishInitialization(); // This function also sets the diaries directory since it is based off of the username, it also executes the diaryloader function.

    void ApplySettings();

    //Diary Slots
    void on_DiaryListYears_currentTextChanged(const QString &arg1);

    void on_DiaryListMonths_currentTextChanged(const QString &currentText);

    void on_DiaryListDays_currentTextChanged(const QString &currentText);

    void on_DiaryTextDisplay_itemChanged(QListWidgetItem *item);

    void on_DiaryTextDisplay_entered(const QModelIndex &index);

    void on_DiaryTextDisplay_clicked(const QModelIndex &index);


    //Password Manager Slots
    void on_comboBox_PWSortBy_currentTextChanged(const QString &arg1);

    void on_pushButton_PWAdd_clicked();


    //TaskLists Slots


    void on_pushButton_NewTaskList_clicked();

    void on_listWidget_TaskList_List_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);


    void on_listWidget_TaskList_List_itemClicked(QListWidgetItem *item);

    void on_listWidget_TaskListDisplay_itemClicked(QListWidgetItem *item);

    void on_pushButton_AddTask_clicked();

private:

    // FUNCTIONS
    //----EventFilters------//
    bool eventFilter( QObject * obj, QEvent * event ) override;
    //Variables
    Ui::MainWindow *ui;
    bool initFinished;

    // Saved Settings
    int fontSize = 10;
    bool quitToLogin = false;

    // Global Settings
    bool setting_MinToTray = false;
    bool setting_AskPWAfterMin = false;
    int setting_ReqPWDelay = 30;
    bool setting_OpenOnSettings = false;

    // Diary Settings
    int setting_Diary_TextSize = 10;
    int setting_Diary_TStampTimer = 5;
    int setting_Diary_TStampCounter = 5;
    bool setting_Diary_CanEditRecent = true;
    bool setting_Diary_ShowTManLogs = true;

    // Task Lists Settings
    int setting_TLists_TextSize = 10;
    bool setting_TLists_LogToDiary = true;
    QString setting_TLists_TaskType = "Simple";
    QString setting_TLists_CMess = "Simple";
    QString setting_TLists_PMess = "Simple";
    bool setting_TLists_Notif = true;

    // Password Manager Settings
    QString setting_PWMan_DefSortingMethod = "Password";
    bool setting_PWMan_ReqPassword = true;
    bool setting_PWMan_HidePasswords = true;

    // Encrypted Data Settings
    bool setting_DataENC_ReqPassword = false;
    bool setting_DataENC_HideThumbnails_Image = false;
    bool setting_DataENC_HideThumbnails_Video = false;
    QString setting_DataENC_Hidden_Categories = "";
    QString setting_DataENC_Hidden_Tags = "";
    bool setting_DataENC_Hide_Categories = false;
    bool setting_DataENC_Hide_Tags = false;
    
    // VideoPlayer Settings
    bool setting_VP_Shows_Autoplay = true;
    bool setting_VP_Shows_AutoFullScreen = false;
    bool setting_VP_Shows_UseTMDB = true;
    bool setting_VP_Shows_DisplayFilenames = false;
    bool setting_VP_Shows_CheckNewEP = true;
    int setting_VP_Shows_FileFolderParsing = 0;  // 0 = Folder Name, 1 = File Name
    int setting_VP_Shows_AutoDelete = 0;  // 0 = Always Ask, 1 = Keep Files, 2 = Delete, 3 = Secure Delete
    int setting_VP_Shows_DefaultVolume = 100;
    bool setting_VP_Shows_CheckNewEPStartup = false;

    //Member Classes
    Operations_Diary* Operations_Diary_ptr = nullptr;
    Operations_PasswordManager* Operations_PasswordManager_ptr = nullptr;
    Operations_TaskLists* Operations_TaskLists_ptr = nullptr;
    Operations_Settings* Operations_Settings_ptr = nullptr;
    Operations_EncryptedData* Operations_EncryptedData_ptr = nullptr;
    Operations_VP_Shows* Operations_VP_Shows_ptr = nullptr;

    DatabasePersistentSettingsManager* m_persistentSettingsManager = nullptr;

    // NEW: Helper method to check if current tab requires password protection
    bool isCurrentTabPasswordProtected() const;

signals:
    void passUsername_Signal(QString username);
private slots:
    // Debug button for testing video player
    void on_pushButton_Debug_clicked();
    
    void on_pushButton_DataENC_Encrypt_clicked();
    
    void on_pushButton_NonceCheck_clicked();

    void on_pushButton_Acc_Save_clicked();

    void on_pushButton_Acc_Cancel_clicked();

    void on_pushButton_Diary_Save_clicked();

    void on_pushButton_Diary_Cancel_clicked();

    void on_pushButton_Diary_RDefault_clicked();

    void on_pushButton_TList_Save_clicked();

    void on_pushButton_TList_Cancel_clicked();

    void on_pushButton_TList_RDefault_clicked();

    void on_pushButton_PWMan_Save_clicked();

    void on_pushButton_PWMan_Cancel_clicked();

    void on_pushButton_PWMan_RDefault_clicked();

    void on_pushButton_LogOut_clicked();

    void on_pushButton_MinToTray_clicked();

    void on_pushButton_CloseApp_clicked();

    void on_lineEdit_DisplayName_textChanged(const QString &arg1);

    void on_comboBox_DisplayNameColor_currentTextChanged(const QString &arg1);

    void on_checkBox_MinToTray_stateChanged(int arg1);

    void on_checkBox_AskPW_stateChanged(int arg1);

    void on_spinBox_Diary_TextSize_valueChanged(int arg1);

    void on_spinBox_Diary_TStampTimer_valueChanged(int arg1);

    void on_spinBox_Diary_TStampReset_valueChanged(int arg1);

    void on_checkBox_Diary_CanEditRecent_stateChanged(int arg1);

    void on_checkBox_Diary_TManLogs_stateChanged(int arg1);

    void on_spinBox_TList_TextSize_valueChanged(int arg1);

    void on_comboBox_PWMan_SortBy_currentTextChanged(const QString &arg1);

    void on_checkBox_PWMan_ReqPW_stateChanged(int arg1);

    void on_checkBox_PWMan_HidePWS_stateChanged(int arg1);

    void onPasswordValidationRequested(int targetTabIndex, int currentIndex);

    void onUnsavedChangesCheckRequested(int targetTabIndex, int currentIndex);

    void on_tabWidget_Main_tabBarClicked(int index);

    void onTabChanged(int index);

    void on_pushButton_AboutMMDiary_clicked();

    void on_pushButton_ChangeLog_clicked();

    void on_pushButton_Acc_ChangePW_clicked();

    void on_pushButton_DataENC_SecureDel_clicked();

    void on_pushButton_DataENC_Save_clicked();

    void on_pushButton_DataENC_Cancel_clicked();

    void on_pushButton_DataENC_RDefault_clicked();

    void on_checkBox_DataENC_ReqPW_stateChanged(int arg1);

    void on_checkBox_DataENC_HideThumbnails_Image_stateChanged(int arg1);

    void on_checkBox_DataENC_HideThumbnails_Video_stateChanged(int arg1);

    void on_spinBox_ReqPWDelay_valueChanged(int arg1);

    void on_checkBox_OpenOnSettings_stateChanged(int arg1);
    
    // VideoPlayer Settings
    void on_pushButton_VP_Shows_Save_clicked();
    void on_pushButton_VP_Shows_Cancel_clicked();
    void on_pushButton_VP_Shows_RDefault_clicked();
    void on_checkBox_VP_Shows_Autoplay_stateChanged(int arg1);
    void on_checkBox_VP_Shows_AutoplayRand_stateChanged(int arg1);
    void on_checkBox_VP_Shows_UseTMDB_stateChanged(int arg1);
    void on_checkBox_VP_Shows_DisplayFilenames_stateChanged(int arg1);
    void on_checkBox_VP_Shows_CheckNewEP_stateChanged(int arg1);
    void on_comboBox_VP_Shows_FileFolderParsing_currentIndexChanged(int index);
    void on_comboBox_VP_Shows_AutoDelete_currentIndexChanged(int index);
    void on_spinBox_VP_Shows_DefaultVolume_valueChanged(int arg1);
    void on_checkBox_VP_Shows_CheckNewEPStartup_stateChanged(int arg1);

private:
    void UpdateTasklistTextSize();
    QSystemTrayIcon *trayIcon = nullptr;
    QMenu *trayMenu = nullptr;
    void LoadPersistentSettings();
    void SavePersistentSettings();
    void cleanupPointers();  // Centralized cleanup method
    
    // SECURITY: New helper methods for robust resource management
    void performEmergencyCleanup();  // Emergency cleanup for critical failures
    void validatePointersBeforeUse();  // Validate pointer states before operations
    bool isShuttingDown() const { return !initFinished || quitToLogin; }  // Check if in shutdown state
    
    // Windows shutdown handling
    bool m_windowsShutdownInProgress = false;
};
#endif // MAINWINDOW_H
