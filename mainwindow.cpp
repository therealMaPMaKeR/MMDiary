#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "CombinedDelegate.h"
#include "qlist_DiaryTextDisplay.h"
#include "CryptoUtils.h"
#include "operations.h"
#include <memory>  // SECURITY: For std::unique_ptr
#include <algorithm>  // SECURITY: For std::fill
#include "operations_diary.h"
#include "operations_passwordmanager.h"
#include "operations_tasklists.h"
#include "operations_settings.h"
#include "operations_encrypteddata.h"
#include "operations_vp_shows.h"
#include "passwordvalidation.h"
#include "operations_files.h"
#include "sqlite-database-auth.h"
#include "sqlite-database-settings.h"
#include "settings_default_usersettings.h"
#include "constants.h"
#include "loginscreen.h"
#include "settings_changepassword.h"
#include "ui_about_MMDiary.h"
#include "ui_changelog.h"
#include "noncechecker.h"
#include "CustomWidgets/tasklists/qtree_Tasklists_list.h"
#include <QApplication>
#include <QWindow>
#include <cstring> // For secure memory operations
#ifdef Q_OS_WIN
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , initFinished(false)
    , quitToLogin(false)
{
    qDebug() << "MainWindow: Constructor started";
    
    // Initialization
    ui->setupUi(this);
    this->setWindowTitle("MMDiary");
    // Ensure all pointers are initialized (redundant with header initialization, but explicit for safety)
    m_persistentSettingsManager = nullptr;
    Operations_Diary_ptr = nullptr;
    Operations_PasswordManager_ptr = nullptr;
    Operations_TaskLists_ptr = nullptr;
    Operations_Settings_ptr = nullptr;
    Operations_EncryptedData_ptr = nullptr;
    Operations_VP_Shows_ptr = nullptr;
    trayIcon = nullptr;
    trayMenu = nullptr;
    //set window sizes
    //this->setBaseSize(1280,573);
    //this->setMinimumWidth(800);
    //this->setMinimumHeight(573);
    //this->resize(1280,573);

    //hide task diary logging for now, might remove permanently later
    ui->label_Settings_DTLogs->setHidden(true);
    ui->checkBox_Diary_TManLogs->setHidden(true);
    ui->checkBox_Diary_CanEditRecent->setHidden(true);
    ui->groupBox_Setting_Diary_Misc->setHidden(true);

    // Set default values for Diary widgets
    ui->DiaryTextDisplay->clear();
    ui->DiaryTextInput->clear();
    ui->DiaryTextInput->setFocus();


    // about qt
    connect(ui->pushButton_AboutQT, &QPushButton::clicked, this, []() {
        QMessageBox::aboutQt(nullptr);  // Shows the standard About Qt dialog
    });



    // Initialize the system tray icon (don't show yet - wait for successful init)
    // SECURITY: Don't use 'this' as parent to avoid double-deletion
    trayIcon = new QSystemTrayIcon();
    trayIcon->setIcon(QIcon(":/icons/icon_tray.png")); // Path to your icon in the resource file
    // Don't show() yet - wait until initialization succeeds

    // Create the context menu for the tray icon
    trayMenu = new QMenu(this);
    QAction *openAction = trayMenu->addAction("Open");
    QAction *quitAction = trayMenu->addAction("Quit");
    trayIcon->setContextMenu(trayMenu);

    // Connect the "Open" action to show the window
    connect(openAction, &QAction::triggered, this, &MainWindow::showAndActivate);

    // Connect the "Quit" action to properly close the application with settings save
    connect(quitAction, &QAction::triggered, this, [this]() {
        qDebug() << "MainWindow: System tray quit action triggered";
        
        // SECURITY: Hide tray icon immediately to prevent zombie
        if (trayIcon) {
            trayIcon->hide();
            QApplication::processEvents(); // Ensure it takes effect
        }
        
        // Hide window immediately for responsive feel
        hide();
        
        // Save persistent settings
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            SavePersistentSettings();
            qDebug() << "MainWindow: Saved persistent settings from system tray quit";
        }
        
        // Clean up and quit
        PasswordValidation::clearGracePeriod(user_Username);
        OperationsFiles::cleanupAllUserTempFolders();
        
        if (Operations_Diary_ptr) {
            Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        }
        
        // Clear sensitive data - SecureByteArray handles this securely
        user_Key.clear();
        
        // Cleanup will be handled by destructor
        qApp->quit();
    });

    // Handle double-click on the tray icon to show the window
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showAndActivate();
        }
    });

    connect(qApp, &QCoreApplication::aboutToQuit, [&]() {
        qDebug() << "MainWindow: aboutToQuit signal received";
        
        // NOTE: DeleteEmptyCurrentDayDiary is now handled in closeEvent BEFORE key is cleared
        // This prevents the "corrupted file" error in release builds
        
        // Clear sensitive data - SecureByteArray handles this securely
        user_Key.clear();
    });
    // Connect the custom signal to our slot
    connect(ui->tabWidget_Main, &qtab_Main::passwordValidationRequested,
            this, &MainWindow::onPasswordValidationRequested);

    connect(ui->tabWidget_Main, &qtab_Main::unsavedChangesCheckRequested,
            this, &MainWindow::onUnsavedChangesCheckRequested);

    // Set up password protection for specific tabs using object names directly
    ui->tabWidget_Main->setRequirePasswordForTab("tab_Passwords", setting_PWMan_ReqPassword);
    ui->tabWidget_Main->setRequirePasswordForTab("tab_DataEncryption", setting_DataENC_ReqPassword);

    // Set the settings tab object name
    ui->tabWidget_Main->setSettingsTabObjectName("tab_Settings");
    ui->tabWidget_Main->ensureSettingsTabVisible();

    connect(ui->tabWidget_Main, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);

    //Setting-Always open on settings tab signal connection
    connect(ui->checkBox_OpenOnSettings, &QCheckBox::stateChanged,
            this, &MainWindow::on_checkBox_OpenOnSettings_stateChanged);
}

MainWindow::~MainWindow()
{
    qDebug() << "MainWindow: Destructor called - beginning cleanup sequence";
    
    // SECURITY: Step 0 - Store window handle for shutdown block cleanup (before UI deletion)
#ifdef Q_OS_WIN
    HWND shutdownHwnd = nullptr;
    if (m_windowsShutdownInProgress && ui) {
        shutdownHwnd = reinterpret_cast<HWND>(this->winId());
    }
#endif
    
    // SECURITY: Step 1 - Prevent any further operations from child objects
    initFinished = false;
    
    // SECURITY: Step 2 - IMMEDIATELY clean up tray icon to prevent zombies
    if (trayIcon) {
        qDebug() << "MainWindow: Emergency tray icon cleanup in destructor";
        trayIcon->hide();
        
        // Process events to ensure hide takes effect
        QApplication::processEvents();
        
        // Clear context menu
        trayIcon->setContextMenu(nullptr);
        
        // Delete immediately (no parent, so safe)
        delete trayIcon;
        trayIcon = nullptr;
    }
    
    if (trayMenu) {
        trayMenu->clear();
        delete trayMenu;
        trayMenu = nullptr;
    }
    
    // SECURITY: Step 3 - Disconnect all signals to prevent use-after-free
    if (ui) {
        ui->tabWidget_Main->disconnect();
        ui->checkBox_OpenOnSettings->disconnect();
    }
    disconnect(); // Disconnect all signals from this object
    
    // SECURITY: Step 4 - Close and save persistent settings BEFORE clearing data
    if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
        qDebug() << "MainWindow: Saving persistent settings in destructor";
        SavePersistentSettings();
        m_persistentSettingsManager->close();
        // Don't delete singleton - just null our pointer
        m_persistentSettingsManager = nullptr;
    }
    
    // SECURITY: Step 5 - Delete Operations objects explicitly (they may access user_Key)
    // Delete in reverse order of creation to avoid dependencies
    if (Operations_VP_Shows_ptr) {
        qDebug() << "MainWindow: Deleting VP_Shows operations";
        Operations_VP_Shows_ptr->disconnect();
        delete Operations_VP_Shows_ptr;
        Operations_VP_Shows_ptr = nullptr;
    }
    
    if (Operations_EncryptedData_ptr) {
        qDebug() << "MainWindow: Deleting EncryptedData operations";
        Operations_EncryptedData_ptr->disconnect();
        delete Operations_EncryptedData_ptr;
        Operations_EncryptedData_ptr = nullptr;
    }
    
    if (Operations_Settings_ptr) {
        qDebug() << "MainWindow: Deleting Settings operations";
        Operations_Settings_ptr->disconnect();
        delete Operations_Settings_ptr;
        Operations_Settings_ptr = nullptr;
    }
    
    if (Operations_TaskLists_ptr) {
        qDebug() << "MainWindow: Deleting TaskLists operations";
        Operations_TaskLists_ptr->disconnect();
        delete Operations_TaskLists_ptr;
        Operations_TaskLists_ptr = nullptr;
    }
    
    if (Operations_PasswordManager_ptr) {
        qDebug() << "MainWindow: Deleting PasswordManager operations";
        Operations_PasswordManager_ptr->disconnect();
        delete Operations_PasswordManager_ptr;
        Operations_PasswordManager_ptr = nullptr;
    }
    
    if (Operations_Diary_ptr) {
        qDebug() << "MainWindow: Deleting Diary operations";
        Operations_Diary_ptr->disconnect();
        delete Operations_Diary_ptr;
        Operations_Diary_ptr = nullptr;
    }
    
    // SECURITY: Step 6 - Clear sensitive data with explicit overwrite
    qDebug() << "MainWindow: Clearing sensitive data";
    user_Key.clear(); // SecureByteArray handles secure clearing
    
    // Overwrite QString data before clearing
    if (!user_Username.isEmpty()) {
        std::fill(user_Username.begin(), user_Username.end(), QChar('\0'));
        user_Username.clear();
    }
    if (!user_Displayname.isEmpty()) {
        std::fill(user_Displayname.begin(), user_Displayname.end(), QChar('\0'));
        user_Displayname.clear();
    }
    
    // SECURITY: Step 7 - Clean up any remaining pointers
    // Note: Tray icon already cleaned up in Step 2
    
    // SECURITY: Step 8 - Delete UI last (it may be parent to some widgets)
    delete ui;
    ui = nullptr;
    
    // SECURITY: Step 9 - Tell Windows we're done with cleanup if shutdown is in progress
#ifdef Q_OS_WIN
    if (m_windowsShutdownInProgress && shutdownHwnd) {
        qDebug() << "MainWindow: Removing Windows shutdown block";
        ShutdownBlockReasonDestroy(shutdownHwnd);
        qDebug() << "MainWindow: Windows can now continue shutdown";
    }
#endif
    
    qDebug() << "MainWindow: Destructor completed - all resources cleaned";
}

void MainWindow::cleanupPointers()
{
    qDebug() << "MainWindow: cleanupPointers called (tray cleanup handled in destructor)";
    
    // SECURITY: Tray icon and menu cleanup moved to destructor Step 2
    // to ensure it happens immediately and prevents zombie icons.
    // This function is kept for future non-tray cleanup needs.
    
    // Currently no other pointers to clean up here
    // Operations pointers are handled in destructor Step 5
}

void MainWindow::performEmergencyCleanup()
{
    // SECURITY: Emergency cleanup function for critical failures
    qDebug() << "MainWindow: EMERGENCY CLEANUP INITIATED";
    
    // FIRST: Clean up tray icon to prevent zombies
    if (trayIcon) {
        qDebug() << "MainWindow: Emergency tray icon cleanup";
        trayIcon->hide();
        QApplication::processEvents(); // Force immediate processing
        trayIcon->setContextMenu(nullptr);
        delete trayIcon;
        trayIcon = nullptr;
    }
    
    if (trayMenu) {
        trayMenu->clear();
        delete trayMenu;
        trayMenu = nullptr;
    }
    
    // Immediately clear all sensitive data
    user_Key.clear();
    
    // Overwrite and clear usernames
    if (!user_Username.isEmpty()) {
        std::fill(user_Username.begin(), user_Username.end(), QChar('\0'));
        user_Username.clear();
    }
    if (!user_Displayname.isEmpty()) {
        std::fill(user_Displayname.begin(), user_Displayname.end(), QChar('\0'));
        user_Displayname.clear();
    }
    
    // Force close database connections (singleton pattern - don't delete)
    if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
        m_persistentSettingsManager->close();
        m_persistentSettingsManager = nullptr;
    }
    
    // Clear grace periods
    PasswordValidation::clearGracePeriod();
    
    // Clean temp files
    OperationsFiles::cleanupAllUserTempFolders();
    
    qDebug() << "MainWindow: Emergency cleanup completed";
}

void MainWindow::validatePointersBeforeUse()
{
    // SECURITY: Validation helper to check pointer states
    if (!this) {
        qCritical() << "MainWindow: CRITICAL - 'this' pointer is invalid!";
        return;
    }
    
    if (!ui) {
        qCritical() << "MainWindow: UI pointer is null";
        initFinished = false;
    }
    
    // Check all Operations pointers are valid if initFinished is true
    if (initFinished) {
        if (!Operations_Diary_ptr) {
            qWarning() << "MainWindow: Operations_Diary_ptr is null when it should be initialized";
        }
        if (!Operations_PasswordManager_ptr) {
            qWarning() << "MainWindow: Operations_PasswordManager_ptr is null when it should be initialized";
        }
        if (!Operations_TaskLists_ptr) {
            qWarning() << "MainWindow: Operations_TaskLists_ptr is null when it should be initialized";
        }
        if (!Operations_Settings_ptr) {
            qWarning() << "MainWindow: Operations_Settings_ptr is null when it should be initialized";
        }
        if (!Operations_EncryptedData_ptr) {
            qWarning() << "MainWindow: Operations_EncryptedData_ptr is null when it should be initialized";
        }
        if (!Operations_VP_Shows_ptr) {
            qWarning() << "MainWindow: Operations_VP_Shows_ptr is null when it should be initialized";
        }
    }
    
    qDebug() << "MainWindow: Pointer validation complete";
}




//------------------------------ Functions-------------------------------------//

void MainWindow::FinishInitialization()
{
    initFinished = false;
    
    qDebug() << "MainWindow: Starting FinishInitialization";

    DatabaseAuthManager& authDb = DatabaseAuthManager::instance();
    DatabaseSettingsManager& settingsDb = DatabaseSettingsManager::instance();

    // Connect to the auth database
    if (!authDb.connect()) {
        qCritical() << "MainWindow: Failed to connect to auth database:" << authDb.lastError();
        return;
    }

    // Check if user exists
    if(authDb.GetUserData_String(user_Username,Constants::UserT_Index_Username) == "ERROR" ||
        authDb.GetUserData_String(user_Username,Constants::UserT_Index_Username) == "INVALIDUSER")
    {
        qDebug() << "MainWindow: ERROR ACCESSING USER DATA FROM DATABASE";
        this->close();
        return;
    }

    // Connect to or create the settings database
    // Use const reference conversion instead of .data() to prevent copy
    if (!settingsDb.connect(user_Username, user_Key)) {
        qCritical() << "Failed to connect to settings database";
        this->close();
        return;
    }

    // Check if settings database is empty (new user) and populate with defaults
    QString testSetting = settingsDb.GetSettingsData_String(Constants::SettingsT_Index_Displayname);
    if (testSetting == Constants::ErrorMessage_Default || testSetting.isEmpty()) {
        qDebug() << "Settings database appears to be new, setting defaults";
        // Use const reference conversion instead of .data() to prevent copy
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


    //Variables Init - Create operations objects with null checks
    qDebug() << "MainWindow: Creating Operations objects";
    
    // Delete any existing objects first (shouldn't happen, but defensive programming)
    if (Operations_Diary_ptr) {
        delete Operations_Diary_ptr;
        Operations_Diary_ptr = nullptr;
    }
    if (Operations_PasswordManager_ptr) {
        delete Operations_PasswordManager_ptr;
        Operations_PasswordManager_ptr = nullptr;
    }
    if (Operations_TaskLists_ptr) {
        delete Operations_TaskLists_ptr;
        Operations_TaskLists_ptr = nullptr;
    }
    if (Operations_Settings_ptr) {
        delete Operations_Settings_ptr;
        Operations_Settings_ptr = nullptr;
    }
    if (Operations_EncryptedData_ptr) {
        delete Operations_EncryptedData_ptr;
        Operations_EncryptedData_ptr = nullptr;
    }
    if (Operations_VP_Shows_ptr) {
        delete Operations_VP_Shows_ptr;
        Operations_VP_Shows_ptr = nullptr;
    }
    

    // Create new objects

    Operations_Settings_ptr = new Operations_Settings(this);
    if (!Operations_Settings_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_Settings";
        this->close();
        return;
    }

    Operations_Diary_ptr = new Operations_Diary(this);
    if (!Operations_Diary_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_Diary";
        this->close();
        return;
    }
    
    Operations_PasswordManager_ptr = new Operations_PasswordManager(this);
    if (!Operations_PasswordManager_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_PasswordManager";
        this->close();
        return;
    }
    
    Operations_TaskLists_ptr = new Operations_TaskLists(this);
    if (!Operations_TaskLists_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_TaskLists";
        this->close();
        return;
    }
    
    Operations_EncryptedData_ptr = new Operations_EncryptedData(this);
    if (!Operations_EncryptedData_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_EncryptedData";
        this->close();
        return;
    }
    
    Operations_VP_Shows_ptr = new Operations_VP_Shows(this);
    if (!Operations_VP_Shows_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_VP_Shows";
        this->close();
        return;
    }
    
    CombinedDelegate *delegate = new CombinedDelegate(this);
    if (!delegate) {
        qCritical() << "MainWindow: Failed to create CombinedDelegate";
        this->close();
        return;
    }

    // Initialize persistent settings manager
    m_persistentSettingsManager = &DatabasePersistentSettingsManager::instance();

    // Start grace period since user just successfully authenticated during login
    if (!user_Username.isEmpty()) {
        PasswordValidation::recordSuccessfulValidation(user_Username);
        qDebug() << "Started password grace period for user after login:" << user_Username;
    }

    // Connect to persistent settings database and load settings
    // Use const reference conversion instead of .data() to prevent copy
    if (m_persistentSettingsManager->connect(user_Username, user_Key)) {
        LoadPersistentSettings();
    } else {
        qDebug() << "Failed to connect to persistent settings database, using defaults";
    }

        //------------------ INITIALIZE SIGNALS ----------------//
        //Global Signals
        connect(ui->spinBox_ReqPWDelay, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::on_spinBox_ReqPWDelay_valueChanged);
        //Diary Signals
        connect(ui->DiaryTextInput, &qtextedit_DiaryTextInput::customSignal,Operations_Diary_ptr, &Operations_Diary::on_DiaryTextInput_returnPressed);
        connect(delegate, &CombinedDelegate::TextModificationsMade, ui->DiaryTextDisplay, &qlist_DiaryTextDisplay::TextWasEdited);
        connect(Operations_Diary_ptr, &Operations_Diary::UpdateFontSize, ui->DiaryTextInput, &qtextedit_DiaryTextInput::UpdateFontSizeTrigger);
        connect(Operations_Diary_ptr, &Operations_Diary::UpdateFontSize, ui->DiaryTextDisplay, &qlist_DiaryTextDisplay::UpdateFontSize_Slot);
        connect(ui->DiaryTextDisplay, &MainWindow::customContextMenuRequested, Operations_Diary_ptr, &Operations_Diary::showContextMenu_TextDisplay);
        connect(ui->DiaryListDays, &MainWindow::customContextMenuRequested, Operations_Diary_ptr, &Operations_Diary::showContextMenu_ListDays);
        //PasswordManager Signals
        //Encrypted Data Signals
        connect(ui->checkBox_DataENC_HideThumbnails_Image, &QCheckBox::stateChanged,
                this, &MainWindow::on_checkBox_DataENC_HideThumbnails_Image_stateChanged);

        connect(ui->checkBox_DataENC_HideThumbnails_Video, &QCheckBox::stateChanged,
                this, &MainWindow::on_checkBox_DataENC_HideThumbnails_Video_stateChanged);

        // Connect tag selection mode combo box
        connect(ui->comboBox_DataENC_TagSelectionMode, &QComboBox::currentTextChanged,
                Operations_EncryptedData_ptr, &Operations_EncryptedData::onTagSelectionModeChanged);

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
        ui->comboBox_PWSortBy->setCurrentIndex(Operations::GetIndexFromText(setting_PWMan_DefSortingMethod, ui->comboBox_PWSortBy)); // set current value for pwmanager combo box to that of saved settings for default
        Operations_TaskLists_ptr->UpdateTasklistsTextSize(setting_TLists_TextSize); // set tasklist text size
        // Diary signals (fix lag on resize and zoom)

        connect(ui->DiaryTextDisplay, &qlist_DiaryTextDisplay::sizeUpdateStarted,
                [this]() {
                    if(Operations_Diary_ptr) { // Safety check
                        Operations_Diary_ptr->prevent_onDiaryTextDisplay_itemChanged = true;
                    }
                });

        connect(ui->DiaryTextDisplay, &qlist_DiaryTextDisplay::sizeUpdateFinished,
                [this]() {
                    if(Operations_Diary_ptr) { // Safety check
                        Operations_Diary_ptr->prevent_onDiaryTextDisplay_itemChanged = false;
                    }
                });
        //EncryptedData Signals
        connect(ui->comboBox_DataENC_SortType, &QComboBox::currentTextChanged,
                Operations_EncryptedData_ptr, &Operations_EncryptedData::onSortTypeChanged);
        //trayIcon->showMessage("Title", "This is a notification message.", QSystemTrayIcon::Information, 3000);
        


        // SECURITY: Only show tray icon after successful initialization
        if (trayIcon && !trayIcon->isVisible()) {
            trayIcon->show();
            qDebug() << "MainWindow: Tray icon shown after successful initialization";
        }
        
        initFinished = true;
}

void MainWindow::ApplySettings()
{
    if(setting_Diary_CanEditRecent == true){ui->DiaryTextDisplay->setEditTriggers(QAbstractItemView::DoubleClicked);}else{ui->DiaryTextDisplay->setEditTriggers(QAbstractItemView::NoEditTriggers);} // set double click to edit if enabled
    
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->UpdateDisplayName();
        Operations_Diary_ptr->UpdateDelegate();
        Operations_Diary_ptr->DiaryLoader(); // reload diaries
    }

    // Update password protection settings for tabs
    ui->tabWidget_Main->setRequirePasswordForTab("tab_Passwords", setting_PWMan_ReqPassword);
    ui->tabWidget_Main->setRequirePasswordForTab("tab_DataEncryption", setting_DataENC_ReqPassword);
}

void MainWindow::showAndActivate() {
    qDebug() << "MainWindow: showAndActivate called";
    
    // SECURITY: Validate state before proceeding
    if (quitToLogin == true) {
        qDebug() << "MainWindow: Ignoring showAndActivate during logout";
        return;
    }
    
    if (!initFinished) {
        qWarning() << "MainWindow: showAndActivate called before initialization complete";
        return;
    }
    
    // SECURITY: Validate pointers before use
    if (!ui) {
        qCritical() << "MainWindow: UI pointer is null in showAndActivate";
        return;
    }

    // Check if password is required after minimize to tray
    if (setting_AskPWAfterMin && !isVisible()) {
        qDebug() << "MainWindow: Password required after minimize, checking grace period";
        
        // SECURITY: Validate username before use
        if (user_Username.isEmpty()) {
            qCritical() << "MainWindow: Username is empty during restore";
            performEmergencyCleanup();
            QApplication::quit();
            return;
        }
        
        // Respect grace period for minimize-to-tray unlock
        int gracePeriodSeconds = PasswordValidation::getGracePeriodForUser(user_Username);

        if (!PasswordValidation::isWithinGracePeriod(user_Username, gracePeriodSeconds)) {
            qDebug() << "MainWindow: Grace period expired, requesting password";
            
            // Grace period expired, require password validation
            bool validPassword = false;
            try {
                validPassword = PasswordValidation::validatePasswordWithCustomCancel(
                    this, "Unlock Application", user_Username, "Quit App");
            } catch (const std::exception& e) {
                qCritical() << "MainWindow: Exception during password validation:" << e.what();
                validPassword = false;
            }

            if (!validPassword) {
                qDebug() << "MainWindow: Password validation failed or cancelled, quitting";
                
                // SECURITY: Clear sensitive data before quitting
                performEmergencyCleanup();
                
                // If validation failed or was cancelled, quit the application
                QApplication::quit();
                return;
            }
            
            qDebug() << "MainWindow: Password validation successful";
        } else {
            qDebug() << "MainWindow: Within grace period, skipping password prompt";
        }
    }

    // Check if we should open on settings tab OR if grace period expired for password-protected tab
    bool shouldOpenOnSettings = setting_OpenOnSettings;

    // Also open on settings if current tab is password-protected and grace period expired
    if (!shouldOpenOnSettings && isCurrentTabPasswordProtected()) {
        int gracePeriodSeconds = PasswordValidation::getGracePeriodForUser(user_Username);
        if (!PasswordValidation::isWithinGracePeriod(user_Username, gracePeriodSeconds)) {
            shouldOpenOnSettings = true;
            qDebug() << "Grace period expired for password-protected tab, switching to settings";
        }
    }

    if (shouldOpenOnSettings) {
        // Ensure settings tab is visible
        ui->tabWidget_Main->ensureSettingsTabVisible();

        // Find the settings tab index dynamically
        int settingsTabIndex = Operations::GetTabIndexByObjectName("tab_Settings", ui->tabWidget_Main);
        if (settingsTabIndex >= 0) {
            ui->tabWidget_Main->setCurrentIndex(settingsTabIndex);
        }
    }

    // If we got here, either no password was required, validation succeeded, or grace period is active
    show();
    raise();        // Raise the window above others
    activateWindow(); // Activate it to ensure focus
}

bool MainWindow::isCurrentTabPasswordProtected() const
{
    // Get current tab index and corresponding widget
    int currentTabIndex = ui->tabWidget_Main->currentIndex();
    if (currentTabIndex < 0) {
        return false;
    }

    QWidget* currentWidget = ui->tabWidget_Main->widget(currentTabIndex);
    if (!currentWidget) {
        return false;
    }

    QString currentTabObjectName = currentWidget->objectName();

    // Check if current tab is password-protected and the setting is enabled
    if (currentTabObjectName == "tab_Passwords" && setting_PWMan_ReqPassword) {
        return true;
    } else if (currentTabObjectName == "tab_DataEncryption" && setting_DataENC_ReqPassword) {
        return true;
    }

    return false;
}

//-------- Persistent Settings ---------//

void MainWindow::LoadPersistentSettings()
{
    if (!m_persistentSettingsManager || !m_persistentSettingsManager->isConnected()) {
        return;
    }

    // Load main window size and position
    int sizeX = m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainWindow_SizeX);
    int sizeY = m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainWindow_SizeY);
    int posX = m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainWindow_PosX);
    int posY = m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainWindow_PosY);

    // Apply window size if valid (> 0)
    if (sizeX > 0 && sizeY > 0) {
        resize(sizeX, sizeY);
    }

    // Apply window position if valid (>= 0)
    if (posX >= 0 && posY >= 0) {
        move(posX, posY);
    }

    // Load and apply tab visibility settings
    struct TabVisibilityInfo {
        QString objectName;
        QString constantName;
    };

    QList<TabVisibilityInfo> tabVisibilityList = {
        {"tab_Diaries", Constants::PSettingsT_Index_TabVisible_Diaries},
        {"tab_Tasklists", Constants::PSettingsT_Index_TabVisible_Tasklists},
        {"tab_Passwords", Constants::PSettingsT_Index_TabVisible_Passwords},
        {"tab_DataEncryption", Constants::PSettingsT_Index_TabVisible_DataEncryption},
        {"tab_Settings", Constants::PSettingsT_Index_TabVisible_Settings},
        {"tab_VideoPlayer", Constants::PSettingsT_Index_TabVisible_VideoPlayer}
    };

    for (const auto& tabInfo : tabVisibilityList) {
        int isVisible = m_persistentSettingsManager->GetPersistentSettingsData_Int(tabInfo.constantName);

        // If value is -1 (not set), default to visible (1)
        if (isVisible == -1) {
            isVisible = 1;
        }

        // REMOVED: Special handling for settings tab - it can now be hidden like other tabs

        ui->tabWidget_Main->setTabVisibleByObjectName(tabInfo.objectName, isVisible == 1);
    }

    // This ensures settings tab cannot be hidden, even if older versions allowed it
    ui->tabWidget_Main->setTabVisibleByObjectName("tab_Settings", true);
    qDebug() << "Forced settings tab to be visible (settings tab cannot be hidden)";

    // Load and apply tab order
    struct TabOrderInfo {
        QString objectName;
        int savedPosition;
        int currentPosition;
        bool isValid;
    };

    QList<TabOrderInfo> tabOrder;

    // Get saved positions for each tab
    tabOrder.append({"tab_Settings",
                     m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_Settings),
                     Operations::GetTabIndexByObjectName("tab_Settings", ui->tabWidget_Main), false});
    tabOrder.append({"tab_Diaries",
                     m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_Diary),
                     Operations::GetTabIndexByObjectName("tab_Diaries", ui->tabWidget_Main), false});
    tabOrder.append({"tab_Tasklists",
                     m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists),
                     Operations::GetTabIndexByObjectName("tab_Tasklists", ui->tabWidget_Main), false});
    tabOrder.append({"tab_Passwords",
                     m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager),
                     Operations::GetTabIndexByObjectName("tab_Passwords", ui->tabWidget_Main), false});
    tabOrder.append({"tab_DataEncryption",
                     m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData),
                     Operations::GetTabIndexByObjectName("tab_DataEncryption", ui->tabWidget_Main), false});
    tabOrder.append({"tab_VideoPlayer",
                     m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_VideoPlayer),
                     Operations::GetTabIndexByObjectName("tab_VideoPlayer", ui->tabWidget_Main), false});

    // Validate saved positions and mark valid entries
    int totalTabs = ui->tabWidget_Main->count();
    QList<int> usedPositions;

    for (auto& tab : tabOrder) {
        if (tab.currentPosition != -1 && tab.savedPosition >= 0 && tab.savedPosition < totalTabs) {
            // Check for duplicate saved positions
            if (!usedPositions.contains(tab.savedPosition)) {
                tab.isValid = true;
                usedPositions.append(tab.savedPosition);
            }
        }
    }

    // Check if we have valid saved tab order data
    QList<TabOrderInfo> validTabs;
    for (const auto& tab : tabOrder) {
        if (tab.isValid) {
            validTabs.append(tab);
        }
    }

    // Debug: Print loaded positions
    qDebug() << "Loading tab order - Valid tabs:" << validTabs.size() << "of" << tabOrder.size();

    // Only reorder if we have valid data for all tabs AND positions are different from current
    if (validTabs.size() == tabOrder.size()) {
        // Check if reordering is actually needed
        bool needsReordering = false;
        for (const auto& tab : validTabs) {
            if (tab.currentPosition != tab.savedPosition) {
                needsReordering = true;
                break;
            }
        }

        if (needsReordering) {
            qDebug() << "Reordering tabs to match saved configuration";

            // Sort by saved position to get the desired order
            std::sort(validTabs.begin(), validTabs.end(), [](const TabOrderInfo& a, const TabOrderInfo& b) {
                return a.savedPosition < b.savedPosition;
            });

            // Apply the tab order by moving tabs to their correct positions
            for (int targetPos = 0; targetPos < validTabs.size(); ++targetPos) {
                const TabOrderInfo& tab = validTabs[targetPos];

                // Find current position of this tab (it may have changed due to previous moves)
                int currentPos = Operations::GetTabIndexByObjectName(tab.objectName, ui->tabWidget_Main);

                if (currentPos != -1 && currentPos != targetPos) {
                    qDebug() << "Moving tab" << tab.objectName << "from position" << currentPos << "to" << targetPos;
                    // Move tab from current position to target position
                    ui->tabWidget_Main->moveTab(currentPos, targetPos);
                }
            }

            qDebug() << "Tab order restored from persistent settings";
        } else {
            qDebug() << "Tab order already matches saved configuration";
        }
    } else {
        qDebug() << "Incomplete tab order data, keeping current order";
    }

    // Load main tab widget current tab (after reordering)
    int currentTabIndex = m_persistentSettingsManager->GetPersistentSettingsData_Int(Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex);

    // Check if we should override with settings tab
    if (setting_OpenOnSettings) {
        // Ensure settings tab is visible
        ui->tabWidget_Main->ensureSettingsTabVisible();

        // Find the settings tab index dynamically and set it
        int settingsTabIndex = Operations::GetTabIndexByObjectName("tab_Settings", ui->tabWidget_Main);
        if (settingsTabIndex >= 0) {
            ui->tabWidget_Main->setCurrentIndex(settingsTabIndex);
        }
    } else if (currentTabIndex >= 0 && currentTabIndex < ui->tabWidget_Main->count()) {
        // Use the saved tab index if not opening on settings
        ui->tabWidget_Main->setCurrentIndex(currentTabIndex);
    }

    // Load tasklist-specific settings
    QString currentList = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_TLists_CurrentList);
    QString foldedCategories = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_TLists_FoldedCategories);

    // Apply tasklist settings if they exist and are valid
    if (Operations_TaskLists_ptr) {
        qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(ui->treeWidget_TaskList_List);
        if (treeWidget) {
            // First, apply folded categories state
            if (!foldedCategories.isEmpty()) {
                QStringList foldedCategoriesList = foldedCategories.split(";", Qt::SkipEmptyParts);
                
                // Iterate through all categories and set their expanded state
                for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                    QTreeWidgetItem* categoryItem = treeWidget->topLevelItem(i);
                    if (categoryItem && treeWidget->isCategory(categoryItem)) {
                        QString categoryName = categoryItem->text(0);
                        
                        // If this category is in the folded list, collapse it
                        // Otherwise, ensure it's expanded (default state)
                        if (foldedCategoriesList.contains(categoryName)) {
                            categoryItem->setExpanded(false);
                            qDebug() << "MainWindow: Collapsed category:" << categoryName;
                        } else {
                            categoryItem->setExpanded(true);
                        }
                    }
                }
            }
            
            // Then, try to select and load the saved task list if it exists
            bool taskListLoaded = false;
            if (!currentList.isEmpty()) {
                // Find the tasklist item in the tree
                QTreeWidgetItem* tasklistItem = treeWidget->findTasklist(currentList);
                if (tasklistItem) {
                    // Select the tasklist
                    treeWidget->setCurrentItem(tasklistItem);
                    
                    // Ensure the parent category is expanded so the selection is visible
                    if (tasklistItem->parent()) {
                        tasklistItem->parent()->setExpanded(true);
                    }
                    
                    // Load the individual tasklist 
                    // Use a timer to ensure UI is ready
                    QTimer::singleShot(10, this, [this, currentList]() {
                        if (Operations_TaskLists_ptr) {
                            Operations_TaskLists_ptr->LoadIndividualTasklist(currentList, "NULL");
                        }
                    });
                    taskListLoaded = true;
                    qDebug() << "MainWindow: Loaded saved task list:" << currentList;
                }
            }
            
            // If no saved task list was loaded, try to load the first available tasklist
            if (!taskListLoaded) {
                QStringList allTasklists = treeWidget->getAllTasklists();
                if (!allTasklists.isEmpty()) {
                    QString firstTasklist = allTasklists.first();
                    QTreeWidgetItem* firstItem = treeWidget->findTasklist(firstTasklist);
                    if (firstItem) {
                        treeWidget->setCurrentItem(firstItem);
                        
                        // Ensure the parent category is expanded
                        if (firstItem->parent()) {
                            firstItem->parent()->setExpanded(true);
                        }
                        
                        QTimer::singleShot(10, this, [this, firstTasklist]() {
                            if (Operations_TaskLists_ptr) {
                                Operations_TaskLists_ptr->LoadIndividualTasklist(firstTasklist, "NULL");
                            }
                        });
                        qDebug() << "MainWindow: No saved task list found, loading first task list:" << firstTasklist;
                    }
                }
            }
        } else {
            qDebug() << "MainWindow: Task list tree widget is null when applying settings";
        }
    }
    // Load encrypted data settings
    QString savedSortType = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_DataENC_SortType);
    QString savedCategory = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_DataENC_CurrentCategory);
    QString savedTags = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_DataENC_CurrentTags);
    QString savedTagMode = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_DataENC_TagSelectionMode);

    // Apply encrypted data settings if they exist and are valid
    if (!savedSortType.isEmpty()) {
        // Set the sort type combobox
        int sortTypeIndex = ui->comboBox_DataENC_SortType->findText(savedSortType);
        if (sortTypeIndex >= 0) {
            ui->comboBox_DataENC_SortType->setCurrentIndex(sortTypeIndex);
        }
    }

    // Apply tag selection mode if saved (default to "And" if not found)
    if (!savedTagMode.isEmpty()) {
        int tagModeIndex = ui->comboBox_DataENC_TagSelectionMode->findText(savedTagMode);
        if (tagModeIndex >= 0) {
            ui->comboBox_DataENC_TagSelectionMode->setCurrentIndex(tagModeIndex);
        }
    } else {
        // Default to "And" mode if no saved setting
        int defaultIndex = ui->comboBox_DataENC_TagSelectionMode->findText("And");
        if (defaultIndex >= 0) {
            ui->comboBox_DataENC_TagSelectionMode->setCurrentIndex(defaultIndex);
        }
    }

    // Apply category selection if saved category exists
    if (!savedCategory.isEmpty()) {
        int categoryIndex = Operations::GetIndexFromText(savedCategory, ui->listWidget_DataENC_Categories);
        if (categoryIndex >= 0) {
            ui->listWidget_DataENC_Categories->setCurrentRow(categoryIndex);
        }
    }

    // Apply tags selection if saved tags exist (semicolon-separated)
    if (!savedTags.isEmpty()) {
        QStringList savedTagsList = savedTags.split(";", Qt::SkipEmptyParts);

        // Iterate through all items in the tags list widget
        if (ui->listWidget_DataENC_Tags) {
            int tagCount = ui->listWidget_DataENC_Tags->count();
            for (int i = 0; i < tagCount; ++i) {
                QListWidgetItem* item = ui->listWidget_DataENC_Tags->item(i);
                if (item) {
                    // Check if this tag text is in our saved tags list
                    QString tagText = item->text();
                    bool shouldBeChecked = savedTagsList.contains(tagText);
                    item->setCheckState(shouldBeChecked ? Qt::Checked : Qt::Unchecked);
                } else {
                    qDebug() << "MainWindow: Null item at index" << i << "when applying saved tags";
                }
            }
        } else {
            qDebug() << "MainWindow: Tags list widget is null when applying saved tags";
        }
    }

    // Load Video Player settings
    // Load and apply view mode
    QString savedViewMode = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_VP_Shows_ShowsListViewMode);
    if (!savedViewMode.isEmpty() && ui->comboBox_VP_Shows_ListViewMode) {
        bool ok;
        int viewMode = savedViewMode.toInt(&ok);
        if (ok && viewMode >= 0 && viewMode <= 1) {
            ui->comboBox_VP_Shows_ListViewMode->setCurrentIndex(viewMode);
            qDebug() << "MainWindow: Loaded VP Shows view mode:" << viewMode;
        }
    }

    // Load current show and restore if valid
    QString savedShow = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_VP_Shows_CurrentShow);
    if (!savedShow.isEmpty() && savedShow != "NULL" && Operations_VP_Shows_ptr) {
        qDebug() << "MainWindow: Attempting to restore show:" << savedShow;
        
        // Build the full path to check if the folder exists
        QString basePath = QDir::current().absoluteFilePath("Data");
        QString userPath = QDir(basePath).absoluteFilePath(user_Username);
        QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
        QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
        QString showFolderPath = QDir(showsPath).absoluteFilePath(savedShow);
        
        // Check if the folder exists
        if (QDir(showFolderPath).exists()) {
            // Use a timer to ensure UI is ready before opening the show
            QTimer::singleShot(200, this, [this, showFolderPath]() {
                if (Operations_VP_Shows_ptr) {
                    qDebug() << "MainWindow: Opening saved show at:" << showFolderPath;
                    // Call displayShowDetails with empty show name (it will read from settings)
                    Operations_VP_Shows_ptr->displayShowDetails("", showFolderPath);
                }
            });
        } else {
            qDebug() << "MainWindow: Saved show folder no longer exists:" << showFolderPath;
        }
    }

    qDebug() << "Persistent settings loaded successfully";
}

void MainWindow::SavePersistentSettings()
{
    if (!m_persistentSettingsManager || !m_persistentSettingsManager->isConnected()) {
        return;
    }

    // Save main window size and position
    QSize windowSize = size();
    QPoint windowPos = pos();

    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainWindow_SizeX, windowSize.width());
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainWindow_SizeY, windowSize.height());
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainWindow_PosX, windowPos.x());
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainWindow_PosY, windowPos.y());

    // Save tab visibility settings
    struct TabVisibilityInfo {
        QString objectName;
        QString constantName;
    };

    QList<TabVisibilityInfo> tabVisibilityList = {
        {"tab_Diaries", Constants::PSettingsT_Index_TabVisible_Diaries},
        {"tab_Tasklists", Constants::PSettingsT_Index_TabVisible_Tasklists},
        {"tab_Passwords", Constants::PSettingsT_Index_TabVisible_Passwords},
        {"tab_DataEncryption", Constants::PSettingsT_Index_TabVisible_DataEncryption},
        {"tab_Settings", Constants::PSettingsT_Index_TabVisible_Settings},
        {"tab_VideoPlayer", Constants::PSettingsT_Index_TabVisible_VideoPlayer}
    };

    for (const auto& tabInfo : tabVisibilityList) {
        bool isVisible = ui->tabWidget_Main->isTabVisibleByObjectName(tabInfo.objectName);
        m_persistentSettingsManager->UpdatePersistentSettingsData_INT(tabInfo.constantName, isVisible ? 1 : 0);
    }

    // Save main tab widget current tab
    int currentTabIndex = ui->tabWidget_Main->currentIndex();
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_CurrentTabIndex, currentTabIndex);

    // Save individual tab indices (for future use if tabs can be reordered)
    // For now, we'll save the current static indices
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_Settings,
                                                                  Operations::GetTabIndexByObjectName("tab_Settings", ui->tabWidget_Main));
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_Diary,
                                                                  Operations::GetTabIndexByObjectName("tab_Diaries", ui->tabWidget_Main));
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_Tasklists,
                                                                  Operations::GetTabIndexByObjectName("tab_Tasklists", ui->tabWidget_Main));
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_PWManager,
                                                                  Operations::GetTabIndexByObjectName("tab_Passwords", ui->tabWidget_Main));
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_EncryptedData,
                                                                  Operations::GetTabIndexByObjectName("tab_DataEncryption", ui->tabWidget_Main));
    m_persistentSettingsManager->UpdatePersistentSettingsData_INT(Constants::PSettingsT_Index_MainTabWidgetIndex_VideoPlayer,
                                                                  Operations::GetTabIndexByObjectName("tab_VideoPlayer", ui->tabWidget_Main));

    // Save tasklist-specific settings
    QString currentList = "";
    QString foldedCategories = "";

    // Get current selected tasklist from tree widget
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(ui->treeWidget_TaskList_List);
    if (treeWidget) {
        QTreeWidgetItem* currentItem = treeWidget->currentItem();
        if (currentItem) {
            // If a tasklist is selected (not a category)
            if (!treeWidget->isCategory(currentItem)) {
                currentList = currentItem->text(0);
            } else {
                // If a category is selected, we don't save it as current list
                qDebug() << "MainWindow: Current selection is a category, not saving as current list";
            }
        } else {
            qDebug() << "MainWindow: No current task list item selected";
        }
        
        // Get all folded (collapsed) categories
        QStringList foldedCategoriesList;
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem* categoryItem = treeWidget->topLevelItem(i);
            if (categoryItem && treeWidget->isCategory(categoryItem)) {
                // Check if category is collapsed (not expanded)
                if (!categoryItem->isExpanded()) {
                    foldedCategoriesList.append(categoryItem->text(0));
                }
            }
        }
        
        // Join folded categories with semicolon separator
        if (!foldedCategoriesList.isEmpty()) {
            foldedCategories = foldedCategoriesList.join(";");
        }
    } else {
        qDebug() << "MainWindow: Task list tree widget is null";
    }

    // Save tasklist settings (encrypted since they might be sensitive)
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_TLists_CurrentList, currentList);
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_TLists_FoldedCategories, foldedCategories);
    // Save encrypted data settings
    QString currentSortType = "";
    QString currentCategory = "";
    QString currentTags = "";
    QString currentTagMode = "";

    // Get current sort type from combobox
    if (ui->comboBox_DataENC_SortType->currentIndex() >= 0) {
        currentSortType = ui->comboBox_DataENC_SortType->currentText();
    }

    // Get current category selection
    if (ui->listWidget_DataENC_Categories) {
        QListWidgetItem* categoryItem = ui->listWidget_DataENC_Categories->currentItem();
        if (categoryItem) {
            currentCategory = categoryItem->text();
        } else {
            qDebug() << "MainWindow: No category selected for persistent settings";
        }
    } else {
        qDebug() << "MainWindow: Categories list widget is null";
    }

    // Get current tags selection (checked items)
    QStringList checkedTags;
    if (ui->listWidget_DataENC_Tags) {
        int tagCount = ui->listWidget_DataENC_Tags->count();
        for (int i = 0; i < tagCount; ++i) {
            QListWidgetItem* item = ui->listWidget_DataENC_Tags->item(i);
            if (item && item->checkState() == Qt::Checked) {
                QString tagText = item->text();
                checkedTags.append(tagText);
            } else if (!item) {
                qDebug() << "MainWindow: Null tag item at index" << i;
            }
        }
    } else {
        qDebug() << "MainWindow: Tags list widget is null";
    }

    // Get current tag selection mode from combobox
    if (ui->comboBox_DataENC_TagSelectionMode->currentIndex() >= 0) {
        currentTagMode = ui->comboBox_DataENC_TagSelectionMode->currentText();
    }

    // Join checked tags with semicolon separator
    if (!checkedTags.isEmpty()) {
        currentTags = checkedTags.join(";");
    }

    // Save encrypted data settings (encrypted since they might be sensitive)
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_DataENC_SortType, currentSortType);
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_DataENC_CurrentCategory, currentCategory);
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_DataENC_CurrentTags, currentTags);
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_DataENC_TagSelectionMode, currentTagMode);

    // Save Video Player settings
    // Save the view mode combo box state (0=icon, 1=list)
    int viewMode = 1; // Default to list mode
    if (ui->comboBox_VP_Shows_ListViewMode) {
        viewMode = ui->comboBox_VP_Shows_ListViewMode->currentIndex();
        qDebug() << "MainWindow: Saving VP Shows view mode:" << viewMode;
    }
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_VP_Shows_ShowsListViewMode, QString::number(viewMode));

    // Save current show if on display page
    QString currentShow = "NULL"; // Default to NULL
    if (ui->stackedWidget_VP_Shows && Operations_VP_Shows_ptr) {
        int currentPage = ui->stackedWidget_VP_Shows->currentIndex();
        if (currentPage == 1) { // Display page
            // Get the current show folder from Operations_VP_Shows
            QString showFolder = Operations_VP_Shows_ptr->m_currentShowFolder;
            if (!showFolder.isEmpty()) {
                // Extract just the folder name from the full path
                QDir dir(showFolder);
                currentShow = dir.dirName();
                qDebug() << "MainWindow: Saving current show folder:" << currentShow;
            }
        }
    }
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_VP_Shows_CurrentShow, currentShow);

    qDebug() << "Persistent settings saved successfully";
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
    qDebug() << "MainWindow: closeEvent called, quitToLogin=" << quitToLogin << ", setting_MinToTray=" << setting_MinToTray;
    
    // SECURITY: Validate state
    if (!initFinished && !quitToLogin) {
        qWarning() << "MainWindow: closeEvent called before initialization complete";
        event->accept();
        return;
    }
    
    if(quitToLogin == false && setting_MinToTray == true) // minimize to tray
    {
        qDebug() << "MainWindow: Minimizing to system tray";
        
        // Hide immediately for responsive feel
        hide();
        
        // SECURITY: Save persistent settings with error handling
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            try {
                SavePersistentSettings();
                qDebug() << "MainWindow: Saved persistent settings before minimizing to tray";
            } catch (const std::exception& e) {
                qWarning() << "MainWindow: Failed to save settings:" << e.what();
            }
        }
        
        // If current tab is password-protected, renew grace period
        if (isCurrentTabPasswordProtected() && !user_Username.isEmpty()) {
            PasswordValidation::recordSuccessfulValidation(user_Username);
            qDebug() << "MainWindow: Renewed grace period for password-protected tab on minimize to tray";
        }

        event->ignore(); // Ignore the close event to keep the app running
    }
    else if(quitToLogin == false && setting_MinToTray == false) // close app entirely
    {
        qDebug() << "MainWindow: Closing application entirely";
        
        // CRITICAL: Clean up empty diary BEFORE clearing encryption key
        // This must happen while we still have a valid key for validation
        if (Operations_Diary_ptr && !user_Key.isEmpty()) {
            qDebug() << "MainWindow: Cleaning up empty diary before shutdown";
            Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        }
        
        // SECURITY: Set flag to prevent any operations during shutdown
        initFinished = false;
        
        // SECURITY: Hide tray icon immediately to prevent zombie
        if (trayIcon && trayIcon->isVisible()) {
            trayIcon->hide();
            QApplication::processEvents();
            qDebug() << "MainWindow: Tray icon hidden in closeEvent";
        }
        
        // Hide window immediately for responsive feel
        hide();
        
        // SECURITY: Clear grace period for all users
        PasswordValidation::clearGracePeriod(user_Username);
        
        // Clean up temp folders
        OperationsFiles::cleanupAllUserTempFolders();
        
        // Save persistent settings before closing
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            SavePersistentSettings();
        }
        
        // Clear sensitive data here before quit
        user_Key.clear();
        
        // Quit the application (destructor will handle cleanup)
        qApp->quit();
    }
    else if(quitToLogin == true) // log out
    {
        qDebug() << "MainWindow: Logging out";
        hide(); // first hide mainwindow so the user doesn't notice any delay
        
        // CRITICAL: Clean up empty diary BEFORE clearing encryption key
        if (Operations_Diary_ptr && !user_Key.isEmpty()) {
            qDebug() << "MainWindow: Cleaning up empty diary before logout";
            Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        }
        
        // Clear grace period
        PasswordValidation::clearGracePeriod(user_Username);
        
        // Clean up temp folders
        OperationsFiles::cleanupAllUserTempFolders();
        
        // Save persistent settings before closing
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            SavePersistentSettings();
        }
        
        // Hide and disconnect tray icon for logout (don't delete, destructor will handle that)
        if (trayIcon) {
            trayIcon->hide();
            trayIcon->disconnect();
            qDebug() << "MainWindow: Tray icon hidden for logout";
        }
        
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

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    // Check for Windows-specific messages
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        
        // Handle Windows shutdown messages
        if (msg->message == WM_QUERYENDSESSION) {
            qDebug() << "MainWindow: WM_QUERYENDSESSION received - Windows wants to shutdown";
            
            // Set shutdown flag
            m_windowsShutdownInProgress = true;
            
            // Create shutdown block to tell Windows to wait
            HWND hwnd = reinterpret_cast<HWND>(this->winId());
            ShutdownBlockReasonCreate(hwnd, L"MMDiary is saving your data...");
            qDebug() << "MainWindow: Created shutdown block reason - Windows will wait";
            
            // Start the normal quit process
            qApp->quit();
            
            // Tell Windows we can be shut down (but we're asking it to wait)
            if (result) {
                *result = TRUE;
            }
            return true;
        }
        else if (msg->message == WM_ENDSESSION) {
            qDebug() << "MainWindow: WM_ENDSESSION received";
            // This comes after WM_QUERYENDSESSION if shutdown proceeds
            // Our cleanup should already be in progress from qApp->quit()
            if (result) {
                *result = TRUE;
            }
            return true;
        }
    }
#endif
    
    // Call base class implementation for other events
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::ReceiveDataLogin_Slot(QString username, SecureByteArray* key) // receives the userName from the login window.
{
    qDebug() << "MainWindow: Receiving login data from username:" << username;
    
    // SECURITY: Validate input parameters
    if (username.isEmpty()) {
        qCritical() << "MainWindow: Empty username received in login slot";
        delete key;
        QMessageBox::critical(this, "Login Error", "Invalid username received");
        close();
        return;
    }
    
    // SECURITY: Take ownership of key immediately with unique_ptr
    std::unique_ptr<SecureByteArray> keyOwner(key);
    
    if (!keyOwner || keyOwner->isEmpty()) {
        qCritical() << "MainWindow: Invalid or empty key received";
        QMessageBox::critical(this, "Login Error", "Invalid encryption key received");
        close();
        return;
    }
    
    // Store username first (needed for cleanup if initialization fails)
    user_Username = username;
    
    // SECURITY: Move key data securely
    user_Key = std::move(*keyOwner);
    keyOwner.reset(); // Explicitly reset after move
    
    // SECURITY: Initialize with rollback on failure
    bool initSuccess = false;
    try {
        FinishInitialization();
        ApplySettings();
        
        if (Operations_Diary_ptr) {
            Operations_Diary_ptr->UpdateDelegate();
        }
        initSuccess = true;
    } catch (const std::exception& e) {
        qCritical() << "MainWindow: Exception during initialization:" << e.what();
    } catch (...) {
        qCritical() << "MainWindow: Unknown exception during initialization";
    }
    
    if (!initSuccess) {
        qCritical() << "MainWindow: Initialization failed, performing cleanup";
        // SECURITY: Clear sensitive data immediately
        user_Key.clear();
        user_Username.clear();
        
        // Clean up any partially initialized components
        cleanupPointers();
        
        QMessageBox::critical(this, "Initialization Error", 
                            "Failed to initialize the application. Please try logging in again.");
        close();
    } else {
        qDebug() << "MainWindow: Login and initialization completed successfully";
    }
}

void MainWindow::refreshEncryptedDataDisplay()
{
    if (Operations_EncryptedData_ptr) {
        Operations_EncryptedData_ptr->refreshDisplayForSettingsChange();
    }
}

//------------DIARY SIGNALS----------//
void MainWindow::on_DiaryListYears_currentTextChanged(const QString &arg1)
{
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->on_DiaryListYears_currentTextChanged(arg1);
    }
}

void MainWindow::on_DiaryListMonths_currentTextChanged(const QString &currentText)
{
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->on_DiaryListMonths_currentTextChanged(currentText);
    }
}

void MainWindow::on_DiaryListDays_currentTextChanged(const QString &currentText)
{
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->on_DiaryListDays_currentTextChanged(currentText);
    }
}

void MainWindow::on_DiaryTextDisplay_itemChanged(QListWidgetItem *item)
{
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->on_DiaryTextDisplay_itemChanged();
    }
}

void MainWindow::on_DiaryTextDisplay_entered(const QModelIndex &index)
{
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->on_DiaryTextDisplay_entered(index);
    }
}

void MainWindow::on_DiaryTextDisplay_clicked(const QModelIndex &index)
{
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->on_DiaryTextDisplay_clicked();
    }
}

//-----------------Password Manager Signals -----------------//
void MainWindow::on_comboBox_PWSortBy_currentTextChanged(const QString &arg1)
{
    if (Operations_PasswordManager_ptr) {
        Operations_PasswordManager_ptr->on_SortByChanged(arg1);
    }
}


void MainWindow::on_pushButton_PWAdd_clicked()
{
    if (Operations_PasswordManager_ptr) {
        Operations_PasswordManager_ptr->on_AddPasswordClicked();
    }
}

//-----------------Task Lists Signals -----------------//
void MainWindow::on_pushButton_NewTaskList_clicked()
{
    if (Operations_TaskLists_ptr) {
        Operations_TaskLists_ptr->CreateNewTaskList();
    }
}


void MainWindow::on_listWidget_TaskList_List_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (current && Operations_TaskLists_ptr != nullptr) {
        //Operations_TaskLists_ptr->LoadIndividualTasklist(current->text());
        qDebug() << "MainWindow: Task list selection changed to:" << current->text();
    } else if (!current) {
        qDebug() << "MainWindow: Task list selection cleared";
    }
}


void MainWindow::on_listWidget_TaskList_List_itemClicked(QListWidgetItem *item)
{
    if (!item) {
        qDebug() << "MainWindow: Null item clicked in task list";
        return;
    }
    
    if (!Operations_TaskLists_ptr) {
        qDebug() << "MainWindow: Operations_TaskLists_ptr is null";
        return;
    }
    
    if (!ui->listWidget_TaskListDisplay) {
        qDebug() << "MainWindow: Task display widget is null";
        return;
    }
    
    // Check if this is actually a different task list than what's currently loaded
    QString currentTaskListName = ui->label_TaskListName->text();
    if (currentTaskListName == item->text()) {
        qDebug() << "MainWindow: Same task list clicked, not reloading";
        return;  // Don't reload if it's the same task list
    }
    
    // Always pass "NULL" to let LoadIndividualTasklist read the last selected task from metadata
    qDebug() << "MainWindow: Loading task list:" << item->text() << "with task selection from metadata";
    Operations_TaskLists_ptr->LoadIndividualTasklist(item->text(), "NULL");
}

void MainWindow::on_listWidget_TaskListDisplay_itemClicked(QListWidgetItem *item)
{
    if (!item) {
        qDebug() << "MainWindow: Null item clicked in task display";
        return;
    }
    
    if (!Operations_TaskLists_ptr) {
        qDebug() << "MainWindow: Operations_TaskLists_ptr is null";
        return;
    }
    
    if (item->text() != "No tasks in this list")
    {
        Operations_TaskLists_ptr->LoadTaskDetails(item->text());
    } else {
        qDebug() << "MainWindow: 'No tasks in this list' item clicked";
    }
}



void MainWindow::on_pushButton_AddTask_clicked()
{
    if (Operations_TaskLists_ptr) {
        Operations_TaskLists_ptr->CreateNewTask();
    }
}


//----------Settings Signals---------------//

void MainWindow::on_pushButton_Acc_Save_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveGlobal);
    }
}

void MainWindow::on_pushButton_Acc_Cancel_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelGlobal);
    }
}

void MainWindow::on_pushButton_Diary_Save_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveDiary);
    }
}

void MainWindow::on_pushButton_Diary_Cancel_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelDiary);
    }
}

void MainWindow::on_pushButton_Diary_RDefault_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetDiary);
    }
}

void MainWindow::on_pushButton_TList_Save_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveTasklists);
    }
}

void MainWindow::on_pushButton_TList_Cancel_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelTasklists);
    }
}

void MainWindow::on_pushButton_TList_RDefault_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetTasklists);
    }
}

void MainWindow::on_pushButton_PWMan_Save_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SavePWManager);
    }
}

void MainWindow::on_pushButton_PWMan_Cancel_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelPWManager);
    }
}

void MainWindow::on_pushButton_PWMan_RDefault_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetPWManager);
    }
}

void MainWindow::on_pushButton_VP_Shows_Save_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveVPShows);
    }
}

void MainWindow::on_pushButton_VP_Shows_Cancel_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelVPShows);
    }
}

void MainWindow::on_pushButton_VP_Shows_RDefault_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetVPShows);
    }
}

//----Settings Value Changed Signals----//

//Global
void MainWindow::on_lineEdit_DisplayName_textChanged(const QString &arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

void MainWindow::on_comboBox_DisplayNameColor_currentTextChanged(const QString &arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

void MainWindow::on_checkBox_MinToTray_stateChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

void MainWindow::on_checkBox_AskPW_stateChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

void MainWindow::on_spinBox_ReqPWDelay_valueChanged(int arg1)
{
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
    }
}

void MainWindow::on_checkBox_OpenOnSettings_stateChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Global);
}

//Diary
void MainWindow::on_spinBox_Diary_TextSize_valueChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

void MainWindow::on_spinBox_Diary_TStampTimer_valueChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

void MainWindow::on_spinBox_Diary_TStampReset_valueChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

void MainWindow::on_checkBox_Diary_CanEditRecent_stateChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}


void MainWindow::on_checkBox_Diary_TManLogs_stateChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Diary);
}

//Tasklists

void MainWindow::on_spinBox_TList_TextSize_valueChanged(int arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_Tasklists);
}

//Password Manager
void MainWindow::on_comboBox_PWMan_SortBy_currentTextChanged(const QString &arg1)
{
    if(initFinished == false || !Operations_Settings_ptr){return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_PWManager);
}

void MainWindow::on_checkBox_PWMan_HidePWS_stateChanged(int arg1)
{
    if (initFinished == false || !Operations_Settings_ptr) {return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_PWManager);

}

void MainWindow::on_checkBox_PWMan_ReqPW_stateChanged(int arg1)
{
    if (initFinished == false || !Operations_Settings_ptr) {return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_PWManager);
}

//Encrypted Data

void MainWindow::on_pushButton_DataENC_Save_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_SaveEncryptedData);
    }
}

void MainWindow::on_pushButton_DataENC_Cancel_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_CancelEncryptedData);
    }
}

void MainWindow::on_pushButton_DataENC_RDefault_clicked()
{
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ButtonPressed(Constants::SettingsButton_ResetEncryptedData);
    }
}

void MainWindow::on_checkBox_DataENC_ReqPW_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished == false || !Operations_Settings_ptr) {return;}
    Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData);
}

void MainWindow::on_checkBox_DataENC_HideThumbnails_Image_stateChanged(int arg1)
{
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData);
    }
}

void MainWindow::on_checkBox_DataENC_HideThumbnails_Video_stateChanged(int arg1)
{
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData);
    }
}

//Video Player - Shows
// Value changed handlers
void MainWindow::on_checkBox_VP_Shows_Autoplay_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_checkBox_VP_Shows_AutoplayRand_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_checkBox_VP_Shows_UseTMDB_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_checkBox_VP_Shows_DisplayFilenames_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_checkBox_VP_Shows_CheckNewEP_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_comboBox_VP_Shows_FileFolderParsing_currentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_comboBox_VP_Shows_AutoDelete_currentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_spinBox_VP_Shows_DefaultVolume_valueChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

void MainWindow::on_checkBox_VP_Shows_CheckNewEPStartup_stateChanged(int arg1)
{
    Q_UNUSED(arg1)
    if (initFinished && Operations_Settings_ptr) {
        Operations_Settings_ptr->Slot_ValueChanged(Constants::DBSettings_Type_VPShows);
    }
}

//---------- Encrypted Data Feature ----------//
void MainWindow::on_pushButton_DataENC_Encrypt_clicked()
{
    if (Operations_EncryptedData_ptr) {
        Operations_EncryptedData_ptr->encryptSelectedFile();
    }
}

void MainWindow::on_pushButton_NonceCheck_clicked()
{
    qDebug() << "MainWindow: Nonce integrity check button clicked";
    
    // Create nonce checker instance and perform check
    NonceChecker* checker = new NonceChecker(this);
    if (checker) {
        checker->performCheck();
        
        // The checker will delete itself when done
        connect(checker, &QObject::destroyed, [](){ 
            qDebug() << "MainWindow: NonceChecker cleaned up"; 
        });
    } else {
        qDebug() << "MainWindow: Failed to create NonceChecker";
    }
}

void MainWindow::on_pushButton_DataENC_SecureDel_clicked()
{
    if (Operations_EncryptedData_ptr) {
        Operations_EncryptedData_ptr->secureDeleteExternalItems();
    }
}

//------Video Player Debug Button-----//
void MainWindow::on_pushButton_Debug_clicked()
{

}

//------Custom Setting Signals-----//

void MainWindow::UpdateTasklistTextSize()
{
    if (Operations_TaskLists_ptr) {
        Operations_TaskLists_ptr->UpdateTasklistsTextSize(setting_TLists_TextSize);
    }
}

//----App Buttons---//

void MainWindow::on_pushButton_LogOut_clicked()
{
    qDebug() << "MainWindow: Log out initiated by user";
    
    // Clear sensitive data before logging out - SecureByteArray handles this
    user_Key.clear();
    
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
    qDebug() << "MainWindow: Close App button clicked";
    
    // SECURITY: Always hide tray icon immediately when closing
    if (trayIcon && trayIcon->isVisible()) {
        trayIcon->hide();
        QApplication::processEvents(); // Ensure hide takes effect
        qDebug() << "MainWindow: Tray icon hidden before app close";
    }
    
    // Check if minimize to tray is enabled
    if (setting_MinToTray == true) {
        qDebug() << "MainWindow: Closing with tray icon enabled";
        
        // Hide window immediately for responsive feel
        hide();
        
        // Save persistent settings
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            SavePersistentSettings();
            qDebug() << "MainWindow: Saved persistent settings from Close App button";
        }
        
        // CRITICAL: Clean up empty diary BEFORE clearing encryption key
        if (Operations_Diary_ptr && !user_Key.isEmpty()) {
            qDebug() << "MainWindow: Cleaning up empty diary from Close App button";
            Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        }
        
        // Clean up
        PasswordValidation::clearGracePeriod(user_Username);
        OperationsFiles::cleanupAllUserTempFolders();
        
        // Clear sensitive data - SecureByteArray handles this securely
        user_Key.clear();
        
        // Quit (destructor will handle cleanup)
        qApp->quit();
    } else {
        // When minimize to tray is disabled, the normal closeEvent will handle everything
        qApp->quit();
    }
}


void MainWindow::on_pushButton_Acc_ChangePW_clicked()
{
    ChangePassword* cpw = new ChangePassword(this);
    // Use const reference conversion instead of .data() to prevent copy
    cpw->initialize(user_Username, user_Key);
    cpw->exec();
}

//--------tab widget main ---------//

void MainWindow::onTabChanged(int index)
{
    // Find the Password Manager tab dynamically instead of using hardcoded index
    int passwordTabIndex = Operations::GetTabIndexByObjectName("tab_Passwords", ui->tabWidget_Main);

    if (passwordTabIndex != -1 && index == passwordTabIndex && Operations_PasswordManager_ptr) {
        // Update password masking state when switching to Password Manager tab
        Operations_PasswordManager_ptr->UpdatePasswordMasking();
    }
}

void MainWindow::on_tabWidget_Main_tabBarClicked(int index)
{

}

void MainWindow::onPasswordValidationRequested(int targetTabIndex, int currentIndex)
{
    Q_UNUSED(currentIndex);
    
    // SECURITY: Always reset validation state when done
    // Use a scope guard to ensure this happens even if exceptions occur
    struct ValidationGuard {
        MainWindow* window;
        ~ValidationGuard() {
            if (window && window->ui && window->ui->tabWidget_Main) {
                window->ui->tabWidget_Main->setValidationInProgress(false);
                qDebug() << "MainWindow: Reset validation state in password validation";
            }
        }
    } guard{this};
    
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
    Q_UNUSED(currentIndex);
    
    // SECURITY: Always reset validation state when done
    // Use a scope guard to ensure this happens even if exceptions occur
    struct ValidationGuard {
        MainWindow* window;
        bool shouldReset;
        ~ValidationGuard() {
            if (shouldReset && window && window->ui && window->ui->tabWidget_Main) {
                window->ui->tabWidget_Main->setValidationInProgress(false);
                qDebug() << "MainWindow: Reset validation state in unsaved changes check";
            }
        }
    } guard{this, true};
    
    // Check for unsaved settings changes
    bool canProceed = true;
    if (Operations_Settings_ptr) {
        canProceed = Operations_Settings_ptr->handleUnsavedChanges(Constants::DBSettings_Type_ALL, targetTabIndex);
    }

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
            // We need to handle password validation without double-resetting
            // Disable our guard since password validation will handle it
            guard.shouldReset = false;
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


