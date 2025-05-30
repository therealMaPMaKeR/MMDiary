#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "CustomWidgets/CombinedDelegate.h"
#include "CustomWidgets/custom_qlistwidget.h"
#include "Operations-Global/CryptoUtils.h"
#include "Operations-Global/Operations.h"
#include "Operations-Features/operations_diary.h"
#include "Operations-Features/operations_passwordmanager.h"
#include "Operations-Features/operations_tasklists.h"
#include "Operations-Features/operations_settings.h"
#include "Operations-Features/operations_encrypteddata.h"
#include "Operations-Global/passwordvalidation.h"
#include "Operations-Global/operations_files.h"
#include "Operations-Global/sqlite-database-auth.h"
#include "Operations-Global/sqlite-database-settings.h"
#include "Operations-Global/default_usersettings.h"
#include "constants.h"
#include "loginscreen.h"
#include "changepassword.h"
#include "ui_about_MMDiary.h"
#include "ui_changelog.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // Initialization
    ui->setupUi(this);
    this->setWindowTitle("MMDiary");
    //set window sizes
    //this->setBaseSize(1280,573);
    //this->setMinimumWidth(800);
    //this->setMinimumHeight(573);
    //this->resize(1280,573);

    //hide task diary logging for now, might remove permanently later
    ui->label_Settings_DTLogs->setHidden(true);
    ui->label_Settings_TDLog->setHidden(true);
    ui->checkBox_Diary_TManLogs->setHidden(true);
    ui->checkBox_TList_LogToDiary->setHidden(true);

    //hide buttons in encrypted data tab, might remove permanently later
    ui->pushButton_DataENC_Decrypt->setHidden(true);
    ui->pushButton_DataENC_DeleteFile->setHidden(true);
    ui->pushButton_DataENC_DecryptALL->setHidden(true);

    // Set default values for Diary widgets
    ui->DiaryTextDisplay->clear();
    ui->DiaryTextInput->clear();
    ui->DiaryTextInput->setFocus();


    // about qt
    connect(ui->pushButton_AboutQT, &QPushButton::clicked, this, []() {
        QMessageBox::aboutQt(nullptr);  // Shows the standard About Qt dialog
    });



    // Initialize the system tray icon
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon(":/icons/icon_tray.png")); // Path to your icon in the resource file
    trayIcon->show();

    // Create the context menu for the tray icon
    trayMenu = new QMenu(this);
    QAction *openAction = trayMenu->addAction("Open");
    QAction *quitAction = trayMenu->addAction("Quit");
    trayIcon->setContextMenu(trayMenu);

    // Connect the "Open" action to show the window
    connect(openAction, &QAction::triggered, this, &MainWindow::showAndActivate);

    // Connect the "Quit" action to exit the application
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    // Handle double-click on the tray icon to show the window
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showAndActivate();
        }
    });

    connect(qApp, &QCoreApplication::aboutToQuit, [&]() {
        // Your custom code here
        Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        QByteArray ba(user_Key);
        ba.detach();            // ensure unique buffer
        ba.fill('\0');          // overwrite all bytes
        ba.clear();             // reset size to 0
    });
    // Connect the custom signal to our slot
    connect(ui->tabWidget_Main, &custom_QTabWidget_Main::passwordValidationRequested,
            this, &MainWindow::onPasswordValidationRequested);

    connect(ui->tabWidget_Main, &custom_QTabWidget_Main::unsavedChangesCheckRequested,
            this, &MainWindow::onUnsavedChangesCheckRequested);

    // Set up password protection for specific tabs using object names directly
    ui->tabWidget_Main->setRequirePasswordForTab("tab_Passwords", setting_PWMan_ReqPassword);
    ui->tabWidget_Main->setRequirePasswordForTab("tab_DataEncryption", setting_DataENC_ReqPassword);

    // Set the settings tab object name
    ui->tabWidget_Main->setSettingsTabObjectName("tab_Settings");

    connect(ui->tabWidget_Main, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);
}
MainWindow::~MainWindow()
{
    trayIcon->destroyed();
    delete ui;
}




//------------------------------ Functions-------------------------------------//

void MainWindow::FinishInitialization()
{
    initFinished = false;

    DatabaseAuthManager& authDb = DatabaseAuthManager::instance();
    DatabaseSettingsManager& settingsDb = DatabaseSettingsManager::instance();

    // Connect to the auth database
    if (!authDb.connect()) {
        qCritical() << "Failed to connect to auth database:" << authDb.lastError();
        return;
    }

    // Check if user exists
    if(authDb.GetUserData_String(user_Username,Constants::UserT_Index_Username) == "ERROR" ||
        authDb.GetUserData_String(user_Username,Constants::UserT_Index_Username) == "INVALIDUSER")
    {
        qDebug() << "ERROR ACCESSING USER DATA FROM DATABASE";
        this->close();
        return;
    }

    // Connect to or create the settings database
    if (!settingsDb.connect(user_Username, user_Key)) {
        qCritical() << "Failed to connect to settings database";
        this->close();
        return;
    }

    // Check if settings database is empty (new user) and populate with defaults
    QString testSetting = settingsDb.GetSettingsData_String(Constants::SettingsT_Index_Displayname);
    if (testSetting == Constants::ErrorMessage_Default || testSetting.isEmpty()) {
        qDebug() << "Settings database appears to be new, setting defaults";
        if (!Default_UserSettings::SetAllDefaults(user_Username, user_Key)) {
            qDebug() << "Failed to set default settings";
            this->close();
            return;
        }
    }

    // Load settings from the settings database
    user_Displayname = settingsDb.GetSettingsData_String(Constants::SettingsT_Index_Displayname);
    user_nameColor = settingsDb.GetSettingsData_String(Constants::SettingsT_Index_DisplaynameColor);
    fontSize = settingsDb.GetSettingsData_String(Constants::SettingsT_Index_Diary_TextSize).toInt();

    //Variables Init
    Operations_Diary_ptr = new Operations_Diary(this);
    Operations_PasswordManager_ptr = new Operations_PasswordManager(this);
    Operations_TaskLists_ptr = new Operations_TaskLists(this, Operations_Diary_ptr);
    Operations_Settings_ptr = new Operations_Settings(this);
    Operations_EncryptedData_ptr = new Operations_EncryptedData(this);
    CombinedDelegate *delegate = new CombinedDelegate(this);
        //------------------ INITIALIZE SIGNALS ----------------//
        //Diary Signals
        connect(ui->DiaryTextInput, &custom_QTextEditWidget::customSignal,Operations_Diary_ptr, &Operations_Diary::on_DiaryTextInput_returnPressed);
        connect(delegate, &CombinedDelegate::TextModificationsMade, ui->DiaryTextDisplay, &custom_QListWidget::TextWasEdited);
        connect(Operations_Diary_ptr, &Operations_Diary::UpdateFontSize, ui->DiaryTextInput, &custom_QTextEditWidget::UpdateFontSizeTrigger);
        connect(Operations_Diary_ptr, &Operations_Diary::UpdateFontSize, ui->DiaryTextDisplay, &custom_QListWidget::UpdateFontSize_Slot);
        connect(ui->DiaryTextDisplay, &MainWindow::customContextMenuRequested, Operations_Diary_ptr, &Operations_Diary::showContextMenu_TextDisplay);
        connect(ui->DiaryListDays, &MainWindow::customContextMenuRequested, Operations_Diary_ptr, &Operations_Diary::showContextMenu_ListDays);
        //PasswordManager Signals
        //Diary Settings
        if(setting_Diary_CanEditRecent == true){ui->DiaryTextDisplay->setEditTriggers(QAbstractItemView::DoubleClicked);} // set double click to edit if enabled
        Operations_Diary_ptr->DiaryLoader(); // begin loading diaries
        //PasswordManager Settings
        QTimer::singleShot(25, this, [=]() {
            if(setting_PWMan_HidePasswords == true && setting_PWMan_DefSortingMethod == "Password")
            {
                Operations_PasswordManager_ptr->on_SortByChanged("Account");
            }
            else
            {
                Operations_PasswordManager_ptr->on_SortByChanged(setting_PWMan_DefSortingMethod);
            }
        });
        //start index for tab widget
        ui->tabWidget_Main->setCurrentIndex(0); // set tab to Diary
        ui->comboBox_PWSortBy->setCurrentIndex(Operations::GetIndexFromText(setting_PWMan_DefSortingMethod, ui->comboBox_PWSortBy)); // set current value for pwmanager combo box to that of saved settings for default
        Operations_TaskLists_ptr->UpdateTasklistsTextSize(setting_TLists_TextSize); // set tasklist text size
        // Diary signals (fix lag on resize and zoom)

        connect(ui->DiaryTextDisplay, &custom_QListWidget::sizeUpdateStarted,
                [this]() {
                    if(Operations_Diary_ptr) { // Safety check
                        Operations_Diary_ptr->prevent_onDiaryTextDisplay_itemChanged = true;
                    }
                });

        connect(ui->DiaryTextDisplay, &custom_QListWidget::sizeUpdateFinished,
                [this]() {
                    if(Operations_Diary_ptr) { // Safety check
                        Operations_Diary_ptr->prevent_onDiaryTextDisplay_itemChanged = false;
                    }
                });
        //EncryptedData Signals
        connect(ui->comboBox_DataENC_SortType, &QComboBox::currentTextChanged,
                Operations_EncryptedData_ptr, &Operations_EncryptedData::onSortTypeChanged);
        //trayIcon->showMessage("Title", "This is a notification message.", QSystemTrayIcon::Information, 3000);
        initFinished = true;

}

void MainWindow::ApplySettings()
{
    if(setting_Diary_CanEditRecent == true){ui->DiaryTextDisplay->setEditTriggers(QAbstractItemView::DoubleClicked);}else{ui->DiaryTextDisplay->setEditTriggers(QAbstractItemView::NoEditTriggers);} // set double click to edit if enabled
    Operations_Diary_ptr->UpdateDisplayName();
    Operations_Diary_ptr->UpdateDelegate();
    Operations_Diary_ptr->DiaryLoader(); // reload diaries

    // Update password protection settings for tabs
    ui->tabWidget_Main->setRequirePasswordForTab("tab_Passwords", setting_PWMan_ReqPassword);
    ui->tabWidget_Main->setRequirePasswordForTab("tab_DataEncryption", setting_DataENC_ReqPassword);
}

void MainWindow::showAndActivate() {
    if (quitToLogin == true) return;

    // Check if password is required after minimize to tray
    if (setting_AskPWAfterMin && !isVisible()) {
        // Use custom password validation with "Quit App" as cancel button text
        bool validPassword = PasswordValidation::validatePasswordWithCustomCancel(
            this, "Unlock Application", user_Username, "Quit App");

        if (!validPassword) {
            // If validation failed or was cancelled, quit the application
            QApplication::quit();
            return;
        }
    }

    // If we got here, either no password was required or validation succeeded
    show();
    raise();        // Raise the window above others
    activateWindow(); // Activate it to ensure focus
}

//----------------EventFilter--------------//

bool MainWindow::eventFilter( QObject * obj, QEvent * event ) // event filter, used for the context menu
{

    // Handle lineEdit_DisplayName focus loss
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (ui->lineEdit_DisplayName->hasFocus()) {
            // Check if the click is outside the lineEdit_DisplayName
            QPoint lineEditPos = ui->lineEdit_DisplayName->mapToGlobal(QPoint(0, 0));
            QRect lineEditRect(lineEditPos, ui->lineEdit_DisplayName->size());

            if (!lineEditRect.contains(mouseEvent->globalPos())) {
                ui->lineEdit_DisplayName->clearFocus();
            }
        }
    }

    bool val= QObject::eventFilter(obj, event); // borrowed code, don't understand it well.

    if(QApplication::activePopupWidget()) // if context menu is open
  {
        if(event->type() == QEvent::KeyPress)
        {
            ui->DiaryTextDisplay->clearSelection(); // clear the selection so that we cant open the context menu on the current item by right clicking a different item
            QApplication::activePopupWidget()->close(); // close the context menu
            QKeyEvent * ev = dynamic_cast<QKeyEvent*>(event); //convert event to QKeyEvent
            ui->DiaryTextDisplay->keyPressEvent(ev); // activate the command to type in DiaryTextInput when we are not focused on it and press a key, with the pressed key as parameter
            return val; // return the value of the keypress. might change to true in the future if conflicts happen.
        }

    QMenu * menu = dynamic_cast<QMenu*>(obj); // borrowed code, don't understand it well. I think its supposed to check if context menu is open but it doesnt actually work
    if(menu && event->type() == QEvent::MouseButtonPress) // Mouse button is pressed
    {
        QMouseEvent * ev = dynamic_cast<QMouseEvent*>(event); //convert event to QMouseEvent
        if(ev)
        {
            if(ev->button() == Qt::RightButton) // if event is right button
            {
                ev->ignore(); // ignore the event
                if(!QApplication::activePopupWidget()->underMouse()) // if the users mouse is over the context menu
                {
                ui->DiaryTextDisplay->clearSelection(); // clear the selection so that we cant open the context menu on the current item by right clicking a different item
                QApplication::activePopupWidget()->close(); // close the context menu
                }
                return true; // yes we filter the event
            }
        }
    }
    else if(menu && event->type() == QEvent::MouseButtonRelease) // if mouse button released
    {
        QMouseEvent * ev = dynamic_cast<QMouseEvent*>(event); //convert event to QMouseEvent
        if(ev)
        {
            if(ev->button() == Qt::RightButton)
            {
                ev->ignore();
                return true; // yes we filter the event
            }
        }
    }

  }

    return val;
}

//------------SIGNALS/SLOTS----------//

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QSize oldSize = event->oldSize();
    QSize newSize = event->size();

    QMainWindow::resizeEvent(event);

    if (ui->DiaryTextDisplay) {
        // Check if the new size is smaller than the old size
        if (newSize.width() < oldSize.width() || newSize.height() < oldSize.height()) {
            ui->DiaryTextDisplay->scrollToBottom();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(quitToLogin == false && setting_MinToTray == true) // minimize to tray
    {
    hide();
    event->ignore(); // Ignore the close event to keep the app running
    }
    else if(quitToLogin == false && setting_MinToTray == false) // close app entirely
    {
        OperationsFiles::cleanupAllUserTempFolders();
        trayIcon->hide();
        trayIcon->disconnect();
        trayIcon->deleteLater();
        qApp->quit();
    }
    else if(quitToLogin == true) // log out
    {
        OperationsFiles::cleanupAllUserTempFolders();
        trayIcon->hide();
        trayIcon->disconnect();
        trayIcon->deleteLater();
        event->accept();
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    // always call the base class *unless* we're in the "quitToLogin" state:
    if (quitToLogin == true)
    {
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::ReceiveDataLogin_Slot(QString username, QByteArray key) // receives the userName from the login window.
{
    user_Username = username;
    user_Key = key;
    FinishInitialization(); // This function also sets the diaries directory since it is based off of the username, it also executes the diaryloader function.
    ApplySettings();
    Operations_Diary_ptr->UpdateDelegate();
}

//------------DIARY SIGNALS----------//
void MainWindow::on_DiaryListYears_currentTextChanged(const QString &arg1)
{
    Operations_Diary_ptr->on_DiaryListYears_currentTextChanged(arg1);
}

void MainWindow::on_DiaryListMonths_currentTextChanged(const QString &currentText)
{
    Operations_Diary_ptr->on_DiaryListMonths_currentTextChanged(currentText);
}

void MainWindow::on_DiaryListDays_currentTextChanged(const QString &currentText)
{
    Operations_Diary_ptr->on_DiaryListDays_currentTextChanged(currentText);
}

void MainWindow::on_DiaryTextDisplay_itemChanged(QListWidgetItem *item)
{
    Operations_Diary_ptr->on_DiaryTextDisplay_itemChanged();
}

void MainWindow::on_DiaryTextDisplay_entered(const QModelIndex &index)
{
    Operations_Diary_ptr->on_DiaryTextDisplay_entered(index);
}

void MainWindow::on_DiaryTextDisplay_clicked(const QModelIndex &index)
{
    Operations_Diary_ptr->on_DiaryTextDisplay_clicked();
}

//-----------------Password Manager Signals -----------------//
void MainWindow::on_comboBox_PWSortBy_currentTextChanged(const QString &arg1)
{
    Operations_PasswordManager_ptr->on_SortByChanged(arg1);
}


void MainWindow::on_pushButton_PWAdd_clicked()
{
    Operations_PasswordManager_ptr->on_AddPasswordClicked();
}

//-----------------Task Lists Signals -----------------//
void MainWindow::on_pushButton_NewTaskList_clicked()
{
    Operations_TaskLists_ptr->CreateNewTaskList();
}


void MainWindow::on_listWidget_TaskList_List_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (current && Operations_TaskLists_ptr != nullptr) {
        //Operations_TaskLists_ptr->LoadIndividualTasklist(current->text());
    }
}


void MainWindow::on_listWidget_TaskList_List_itemClicked(QListWidgetItem *item)
{
    if(ui->listWidget_TaskListDisplay->count()-1 > -1) // if the listdisplay is populated.
    {
    Operations_TaskLists_ptr->LoadIndividualTasklist(item->text(), ui->listWidget_TaskListDisplay->item(ui->listWidget_TaskListDisplay->count()-1)->text());
    }
    else
    {
    Operations_TaskLists_ptr->LoadIndividualTasklist(item->text(), "NULL"); // if there is no item in the tasklistdisplay. used to fix bug that happens when deleting a task list.
    }
}

void MainWindow::on_listWidget_TaskListDisplay_itemClicked(QListWidgetItem *item)
{
    if(!item){return;}
    if(item->text() != "No tasks in this list")
    {
    Operations_TaskLists_ptr->LoadTaskDetails(item->text());
    }
}



void MainWindow::on_pushButton_AddTask_clicked()
{
    Operations_TaskLists_ptr->ShowTaskMenu(false);
}


//----------Settings Signals---------------//

void MainWindow::on_pushButton_Acc_Save_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveGlobal);
}

void MainWindow::on_pushButton_Acc_Cancel_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelGlobal);
}

void MainWindow::on_pushButton_Diary_Save_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveDiary);
}

void MainWindow::on_pushButton_Diary_Cancel_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelDiary);
}

void MainWindow::on_pushButton_Diary_RDefault_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetDiary);
}

void MainWindow::on_pushButton_TList_Save_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveTasklists);
}

void MainWindow::on_pushButton_TList_Cancel_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelTasklists);
}

void MainWindow::on_pushButton_TList_RDefault_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetTasklists);
}

void MainWindow::on_pushButton_PWMan_Save_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SavePWManager);
}

void MainWindow::on_pushButton_PWMan_Cancel_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelPWManager);
}

void MainWindow::on_pushButton_PWMan_RDefault_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetPWManager);
}

//----Settings Value Changed Signals----//

//Global
void MainWindow::on_lineEdit_DisplayName_textChanged(const QString &arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

void MainWindow::on_comboBox_DisplayNameColor_currentTextChanged(const QString &arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

void MainWindow::on_checkBox_MinToTray_stateChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}


void MainWindow::on_checkBox_AskPW_stateChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

//Diary
void MainWindow::on_spinBox_Diary_TextSize_valueChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

void MainWindow::on_spinBox_Diary_TStampTimer_valueChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

void MainWindow::on_spinBox_Diary_TStampReset_valueChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

void MainWindow::on_checkBox_Diary_CanEditRecent_stateChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}


void MainWindow::on_checkBox_Diary_TManLogs_stateChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

//Tasklists


void MainWindow::on_comboBox_TList_TaskType_currentTextChanged(const QString &arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}

void MainWindow::on_comboBox_TList_CMess_currentTextChanged(const QString &arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}

void MainWindow::on_comboBox_TList_PMess_currentTextChanged(const QString &arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}

void MainWindow::on_spinBox_TList_TextSize_valueChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}

void MainWindow::on_checkBox_TList_LogToDiary_stateChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}


void MainWindow::on_checkBox_TList_Notif_stateChanged(int arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}

//Password Manager
void MainWindow::on_comboBox_PWMan_SortBy_currentTextChanged(const QString &arg1)
{
    if(initFinished == false){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_PWManager);
}

void MainWindow::on_checkBox_PWMan_HidePWS_stateChanged(int arg1)
{
    if (initFinished == false) {return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_PWManager);

}

void MainWindow::on_checkBox_PWMan_ReqPW_stateChanged(int arg1)
{
    if (initFinished == false) {return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_PWManager);
}

//Encrypted Data

void MainWindow::on_pushButton_DataENC_Save_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveEncryptedData);
}

void MainWindow::on_pushButton_DataENC_Cancel_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelEncryptedData);
}

void MainWindow::on_pushButton_DataENC_RDefault_clicked()
{
    Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetEncryptedData);
}

void MainWindow::on_checkBox_DataENC_ReqPW_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished == false) {return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData);
}

//---------- Encrypted Data Feature ----------//
void MainWindow::on_pushButton_DataENC_Encrypt_clicked()
{
    Operations_EncryptedData_ptr->encryptSelectedFile();
}

void MainWindow::on_pushButton_DataENC_Decrypt_clicked()
{
    Operations_EncryptedData_ptr->decryptSelectedFile();
}

void MainWindow::on_pushButton_DataENC_DeleteFile_clicked()
{
    Operations_EncryptedData_ptr->deleteSelectedFile();
}

void MainWindow::on_pushButton_DataENC_SecureDel_clicked()
{
    Operations_EncryptedData_ptr->secureDeleteExternalItems();
}

void MainWindow::on_pushButton_DataENC_DecryptALL_clicked()
{
    Operations_EncryptedData_ptr->decryptAndExportVisibleFiles();
}

//------Custom Setting Signals-----//

void MainWindow::UpdateTasklistTextSize()
{
    Operations_TaskLists_ptr->UpdateTasklistsTextSize(setting_TLists_TextSize);
}

//----App Buttons---//

void MainWindow::on_pushButton_LogOut_clicked()
{
    loginscreen* w = new loginscreen(this->parentWidget());
    w->show();
    quitToLogin = true;
    close();
}


void MainWindow::on_pushButton_MinToTray_clicked()
{
    hide();
}


void MainWindow::on_pushButton_CloseApp_clicked()
{
    qApp->quit();
}


void MainWindow::on_pushButton_Acc_ChangePW_clicked()
{
    ChangePassword* cpw = new ChangePassword(this);
    cpw->initialize(user_Username,user_Key);
    cpw->exec();
}

//--------tab widget main ---------//

void MainWindow::onTabChanged(int index)
{
    // Find the Password Manager tab dynamically instead of using hardcoded index
    int passwordTabIndex = Operations::GetTabIndexByObjectName("tab_Passwords", ui->tabWidget_Main);

    if (passwordTabIndex != -1 && index == passwordTabIndex) {
        // Update password masking state when switching to Password Manager tab
        Operations_PasswordManager_ptr->UpdatePasswordMasking();
    }
}

void MainWindow::on_tabWidget_Main_tabBarClicked(int index)
{

}

void MainWindow::onPasswordValidationRequested(int targetTabIndex, int currentIndex)
{
    bool passwordRequired = false;
    QString operationName;

    // Get tab indices dynamically
    int passwordTabIndex = Operations::GetTabIndexByObjectName("tab_Passwords", ui->tabWidget_Main);
    int dataEncTabIndex = Operations::GetTabIndexByObjectName("tab_DataEncryption", ui->tabWidget_Main);

    // Determine which tab requires password validation
    if (targetTabIndex == passwordTabIndex && setting_PWMan_ReqPassword) {
        passwordRequired = true;
        operationName = "Access Password Manager";
    } else if (targetTabIndex == dataEncTabIndex && setting_DataENC_ReqPassword) {
        passwordRequired = true;
        operationName = "Access Encrypted Data";
    }

    if (passwordRequired) {
        // Use the static method to validate the password
        bool passwordValid = PasswordValidation::validatePasswordForOperation(
            this, operationName, user_Username);

        if (passwordValid) {
            // Password validated, proceed with tab change
            ui->tabWidget_Main->setCurrentIndex(targetTabIndex);
        }
        // Otherwise, stay on current tab
    } else {
        // No password required, just switch tabs
        ui->tabWidget_Main->setCurrentIndex(targetTabIndex);
    }
}

void MainWindow::onUnsavedChangesCheckRequested(int targetTabIndex, int currentIndex)
{
    // Check for unsaved settings changes
    bool canProceed = Operations_Settings_ptr->handleUnsavedChanges(Constants::DBSettings_Type_ALL, targetTabIndex);

    if (canProceed) {
        // Get tab indices dynamically
        int passwordTabIndex = Operations::GetTabIndexByObjectName("tab_Passwords", ui->tabWidget_Main);
        int dataEncTabIndex = Operations::GetTabIndexByObjectName("tab_DataEncryption", ui->tabWidget_Main);

        // Check if the target tab requires password validation
        bool needsPasswordValidation = false;

        if (targetTabIndex == passwordTabIndex && setting_PWMan_ReqPassword) {
            needsPasswordValidation = true;
        } else if (targetTabIndex == dataEncTabIndex && setting_DataENC_ReqPassword) {
            needsPasswordValidation = true;
        }

        if (needsPasswordValidation) {
            onPasswordValidationRequested(targetTabIndex, currentIndex);
        } else {
            // Otherwise just switch tabs
            ui->tabWidget_Main->setCurrentIndex(targetTabIndex);
        }
    }
    // If canProceed is false, we stay on the current tab
}

void MainWindow::on_pushButton_AboutMMDiary_clicked()
{
    QDialog dialog(this);
    Ui::about_MMDiary aboutUi;
    aboutUi.setupUi(&dialog);
    dialog.setWindowTitle("About MMDiary");
    dialog.exec();
}


void MainWindow::on_pushButton_ChangeLog_clicked()
{
    // Create a new dialog
    QDialog dialog(this);

    // Set up the UI from the changelog.ui file
    Ui::changelog changelogUi;
    changelogUi.setupUi(&dialog);

    // Set window title
    dialog.setWindowTitle("Version: " + Constants::AppVer);

    // Show the dialog and wait for it to close
    dialog.exec();
}


