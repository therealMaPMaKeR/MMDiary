#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "CombinedDelegate.h"
#include "qlist_DiaryTextDisplay.h"
#include "CryptoUtils.h"
#include "operations.h"
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
#include <QApplication>
#include <QWindow>
#include <cstring> // For secure memory operations

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

    // Connect the "Quit" action to properly close the application with settings save
    connect(quitAction, &QAction::triggered, this, [this]() {
        qDebug() << "MainWindow: System tray quit action triggered";
        
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
        
        // Clear sensitive data
        QByteArray ba(user_Key);
        ba.detach();            // ensure unique buffer
        ba.fill('\0');          // overwrite all bytes
        ba.clear();             // reset size to 0
        
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
        
        // Ensure Operations_Diary_ptr is valid before use
        if (Operations_Diary_ptr) {
            Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        }
        
        // Clear sensitive data
        QByteArray ba(user_Key);
        ba.detach();            // ensure unique buffer
        ba.fill('\0');          // overwrite all bytes
        ba.clear();             // reset size to 0
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
    qDebug() << "MainWindow: Destructor called - clearing sensitive data";
    
    // Securely clear the encryption key from memory
    if (!user_Key.isEmpty()) {
        std::memset(user_Key.data(), 0, user_Key.size());
        user_Key.clear();
    }
    
    // Clear username and other sensitive strings
    user_Username.clear();
    user_Displayname.clear();
    
    // Perform centralized cleanup
    cleanupPointers();
    
    // Delete UI last
    delete ui;
    ui = nullptr;
    
    qDebug() << "MainWindow: Destructor completed";
}

void MainWindow::cleanupPointers()
{
    qDebug() << "MainWindow: Starting pointer cleanup";
    
    // Clean up tray icon and menu
    if (trayIcon) {
        // Always hide the tray icon first to ensure it's removed from system tray
        if (trayIcon->isVisible()) {
            trayIcon->hide();
            qDebug() << "MainWindow: Tray icon hidden during cleanup";
        }
        trayIcon->disconnect();
        if (trayIcon->parent() != this) {
            // Only delete if not parented to this
            delete trayIcon;
        }
        trayIcon = nullptr;
    }
    
    if (trayMenu) {
        trayMenu->disconnect();
        if (trayMenu->parent() != this) {
            // Only delete if not parented to this
            delete trayMenu;
        }
        trayMenu = nullptr;
    }
    
    // Clean up Operations pointers (they have 'this' as parent, so Qt will delete them)
    // But we null them to prevent any use-after-free
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->disconnect();
        Operations_Diary_ptr = nullptr;
    }
    
    if (Operations_PasswordManager_ptr) {
        Operations_PasswordManager_ptr->disconnect();
        Operations_PasswordManager_ptr = nullptr;
    }
    
    if (Operations_TaskLists_ptr) {
        Operations_TaskLists_ptr->disconnect();
        Operations_TaskLists_ptr = nullptr;
    }
    
    if (Operations_Settings_ptr) {
        Operations_Settings_ptr->disconnect();
        Operations_Settings_ptr = nullptr;
    }
    
    if (Operations_EncryptedData_ptr) {
        Operations_EncryptedData_ptr->disconnect();
        Operations_EncryptedData_ptr = nullptr;
    }
    
    if (Operations_VP_Shows_ptr) {
        Operations_VP_Shows_ptr->disconnect();
        Operations_VP_Shows_ptr = nullptr;
    }
    
    // Don't delete m_persistentSettingsManager as it's a singleton
    // Just close connection if needed
    if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
        m_persistentSettingsManager->close();
    }
    m_persistentSettingsManager = nullptr;
    
    qDebug() << "MainWindow: Pointer cleanup completed";
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
    
    Operations_Settings_ptr = new Operations_Settings(this);
    if (!Operations_Settings_ptr) {
        qCritical() << "MainWindow: Failed to create Operations_Settings";
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

    // NEW: Start grace period since user just successfully authenticated during login
    if (!user_Username.isEmpty()) {
        PasswordValidation::recordSuccessfulValidation(user_Username);
        qDebug() << "Started password grace period for user after login:" << user_Username;
    }

    // Connect to persistent settings database and load settings
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
    if (quitToLogin == true) return;

    // Check if password is required after minimize to tray
    if (setting_AskPWAfterMin && !isVisible()) {
        // NEW: Respect grace period for minimize-to-tray unlock
        int gracePeriodSeconds = PasswordValidation::getGracePeriodForUser(user_Username);

        if (!PasswordValidation::isWithinGracePeriod(user_Username, gracePeriodSeconds)) {
            // Grace period expired, require password validation
            bool validPassword = PasswordValidation::validatePasswordWithCustomCancel(
                this, "Unlock Application", user_Username, "Quit App");

            if (!validPassword) {
                // If validation failed or was cancelled, quit the application
                QApplication::quit();
                return;
            }
        }
        // If within grace period, continue without password prompt
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
    QString currentTask = m_persistentSettingsManager->GetPersistentSettingsData_String(Constants::PSettingsT_Index_TLists_CurrentTask);

    // Apply tasklist settings if they exist and are valid
    if (!currentList.isEmpty() && Operations_TaskLists_ptr) {
        // Find and select the tasklist if it exists
        QListWidget* taskListWidget = ui->listWidget_TaskList_List;
        for (int i = 0; i < taskListWidget->count(); ++i) {
            if (taskListWidget->item(i)->text() == currentList) {
                taskListWidget->setCurrentRow(i);

                // Load the individual tasklist and try to select the task
                if (!currentTask.isEmpty()) {
                    Operations_TaskLists_ptr->LoadIndividualTasklist(currentList, currentTask);
                } else {
                    Operations_TaskLists_ptr->LoadIndividualTasklist(currentList, "NULL");
                }
                break;
            }
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
        for (int i = 0; i < ui->listWidget_DataENC_Tags->count(); ++i) {
            QListWidgetItem* item = ui->listWidget_DataENC_Tags->item(i);
            if (item) {
                // Check if this tag text is in our saved tags list
                QString tagText = item->text();
                bool shouldBeChecked = savedTagsList.contains(tagText);
                item->setCheckState(shouldBeChecked ? Qt::Checked : Qt::Unchecked);
            }
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
    QString currentTask = "";

    // Get current selected tasklist
    QListWidget* taskListWidget = ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem()) {
        currentList = taskListWidget->currentItem()->text();
    }

    // Get current selected task
    QListWidget* taskDisplayWidget = ui->listWidget_TaskListDisplay;
    if (taskDisplayWidget->currentItem() && taskDisplayWidget->currentItem()->text() != "No tasks in this list") {
        currentTask = taskDisplayWidget->currentItem()->text();
    }

    // Save tasklist settings (encrypted since they might be sensitive)
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_TLists_CurrentList, currentList);
    m_persistentSettingsManager->UpdatePersistentSettingsData_TEXT(Constants::PSettingsT_Index_TLists_CurrentTask, currentTask);

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
    if (ui->listWidget_DataENC_Categories->currentItem()) {
        currentCategory = ui->listWidget_DataENC_Categories->currentItem()->text();
    }

    // Get current tags selection (checked items)
    QStringList checkedTags;
    for (int i = 0; i < ui->listWidget_DataENC_Tags->count(); ++i) {
        QListWidgetItem* item = ui->listWidget_DataENC_Tags->item(i);
        if (item && item->checkState() == Qt::Checked) {
            QString tagText = item->text();
            checkedTags.append(tagText);
        }
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
    
    if(quitToLogin == false && setting_MinToTray == true) // minimize to tray
    {
        // Hide immediately for responsive feel
        hide();
        
        // Save persistent settings after hiding
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            SavePersistentSettings();
            qDebug() << "MainWindow: Saved persistent settings before minimizing to tray";
        }
        
        // If current tab is password-protected, renew grace period
        if (isCurrentTabPasswordProtected()) {
            PasswordValidation::recordSuccessfulValidation(user_Username);
            qDebug() << "MainWindow: Renewed grace period for password-protected tab on minimize to tray";
        }

        event->ignore(); // Ignore the close event to keep the app running
    }
    else if(quitToLogin == false && setting_MinToTray == false) // close app entirely
    {
        qDebug() << "MainWindow: Closing app entirely";
        hide(); // first hide mainwindow so the user doesn't notice any delay
        
        // Clear grace period
        PasswordValidation::clearGracePeriod(user_Username);
        
        // Clean up temp folders
        OperationsFiles::cleanupAllUserTempFolders();
        
        // Save persistent settings before closing
        if (m_persistentSettingsManager && m_persistentSettingsManager->isConnected()) {
            SavePersistentSettings();
        }
        
        // Quit the application (destructor will handle cleanup)
        qApp->quit();
    }
    else if(quitToLogin == true) // log out
    {
        qDebug() << "MainWindow: Logging out";
        hide(); // first hide mainwindow so the user doesn't notice any delay
        
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

void MainWindow::ReceiveDataLogin_Slot(QString username, QByteArray key) // receives the userName from the login window.
{
    qDebug() << "MainWindow: Receiving login data";
    user_Username = username;
    user_Key = key;  // Store the key - will be cleared in destructor
    FinishInitialization(); // This function also sets the diaries directory since it is based off of the username, it also executes the diaryloader function.
    ApplySettings();
    
    if (Operations_Diary_ptr) {
        Operations_Diary_ptr->UpdateDelegate();
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
    if (current && Operations_TaskLists_ptr != nullptr) {
        //Operations_TaskLists_ptr->LoadIndividualTasklist(current->text());
    }
}


void MainWindow::on_listWidget_TaskList_List_itemClicked(QListWidgetItem *item)
{
    if (!item || !Operations_TaskLists_ptr) return;
    
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
    if(!item || !Operations_TaskLists_ptr){return;}
    if(item->text() != "No tasks in this list")
    {
        Operations_TaskLists_ptr->LoadTaskDetails(item->text());
    }
}



void MainWindow::on_pushButton_AddTask_clicked()
{
    if (Operations_TaskLists_ptr) {
        Operations_TaskLists_ptr->ShowTaskMenu(false);
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
    
    // Clear sensitive data before logging out
    if (!user_Key.isEmpty()) {
        std::memset(user_Key.data(), 0, user_Key.size());
        user_Key.clear();
    }
    
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
        
        // Clean up
        PasswordValidation::clearGracePeriod(user_Username);
        OperationsFiles::cleanupAllUserTempFolders();
        
        if (Operations_Diary_ptr) {
            Operations_Diary_ptr->DeleteEmptyCurrentDayDiary();
        }
        
        // Clear sensitive data
        QByteArray ba(user_Key);
        ba.detach();            // ensure unique buffer
        ba.fill('\0');          // overwrite all bytes
        ba.clear();             // reset size to 0
        
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
    cpw->initialize(user_Username,user_Key);
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


