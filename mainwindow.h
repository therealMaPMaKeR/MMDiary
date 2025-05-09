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
#include "CustomWidgets/custom_qcheckboxwidget.h"
#include "Operations-Global/sqlite-database-handler.h" // everywhere that this is needed is somewhere that mainwindow is needed.// more practical to have this here


//Forward Declarations
class PasswordValidation;
class ChangePassword;
class loginscreen;
class CombinedDelegate;
class custom_QListWidget;
class Operations_Diary;
class Operations_PasswordManager;
class Operations_TaskLists;
class Operations_TextsManager;
class Operations_Settings;

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
    friend class Operations_TextsManager;
    friend class Operations_Settings;
    // public functions
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

public slots:
    void ReceiveDataLogin_Slot(QString username, QByteArray key);
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
    QString user_Username;
    QByteArray user_Key;
    QString user_Displayname, user_nameColor;
    int fontSize = 10;
    bool quitToLogin = false;
    // Global Settings
    bool setting_MinToTray = false;
    bool setting_AskPWAfterMin = false;

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
    //Member Classes
    Operations_Diary* Operations_Diary_ptr;
    Operations_PasswordManager* Operations_PasswordManager_ptr;
    Operations_TaskLists* Operations_TaskLists_ptr;
    Operations_Settings* Operations_Settings_ptr;

signals:
    void passUsername_Signal(QString username);
private slots:

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

    void on_comboBox_TList_TaskType_currentTextChanged(const QString &arg1);

    void on_comboBox_TList_CMess_currentTextChanged(const QString &arg1);

    void on_comboBox_TList_PMess_currentTextChanged(const QString &arg1);

    void on_spinBox_TList_TextSize_valueChanged(int arg1);

    void on_checkBox_TList_LogToDiary_stateChanged(int arg1);

    void on_checkBox_TList_Notif_stateChanged(int arg1);

    void on_comboBox_PWMan_SortBy_currentTextChanged(const QString &arg1);

    void on_checkBox_PWMan_ReqPW_stateChanged(int arg1);

    void on_checkBox_PWMan_HidePWS_stateChanged(int arg1);

    void on_pushButton_Acc_ChangePW_clicked();

    void onPasswordValidationRequested(int targetTabIndex, int currentIndex);

    void onUnsavedChangesCheckRequested(int targetTabIndex, int currentIndex);

    void on_tabWidget_Main_tabBarClicked(int index);

    void onTabChanged(int index);

    void on_pushButton_AboutMMDiary_clicked();

    void on_pushButton_ChangeLog_clicked();

private:
    void UpdateTasklistTextSize();
    QSystemTrayIcon *trayIcon;
    QMenu *trayMenu;
};
#endif // MAINWINDOW_H
