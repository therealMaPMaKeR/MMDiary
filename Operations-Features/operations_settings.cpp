#include "operations_settings.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include "../Operations-Global/default_usersettings.h"

Operations_Settings::Operations_Settings(MainWindow* mainWindow)
    : m_mainWindow(mainWindow)
{
    m_mainWindow->ui->label_Username->setText(m_mainWindow->user_Username);
    m_mainWindow->ui->label_Settings_Desc_Name->setText("Description");
    m_mainWindow->ui->textBrowser_SettingDesc->clear();

    // Initialize setting descriptions and event filters
    SetupSettingDescriptions();

    LoadSettings();
    // Initialize button states for all settings types
    UpdateButtonStates(Constants::DBSettings_Type_Global);
    UpdateButtonStates(Constants::DBSettings_Type_Diary);
    UpdateButtonStates(Constants::DBSettings_Type_Tasklists);
    UpdateButtonStates(Constants::DBSettings_Type_PWManager);
    UpdateButtonStates(Constants::DBSettings_Type_EncryptedData);

    m_previousSettingsTabIndex = m_mainWindow->ui->tabWidget_Settings->currentIndex();
    m_previousMainTabIndex = m_mainWindow->ui->tabWidget_Main->currentIndex();

    // Connect tab widget signals
    connect(m_mainWindow->ui->tabWidget_Settings, &QTabWidget::currentChanged,
            this, &Operations_Settings::onSettingsTabChanged);

    connect(m_mainWindow->ui->tabWidget_Main, &QTabWidget::currentChanged,
            this, &Operations_Settings::onMainTabChanged);

    InitializeCustomCheckboxes();
}

void Operations_Settings::LoadSettings(const QString& settingsType)
{
    // Get the username from MainWindow
    QString username = m_mainWindow->user_Username;

    if (username.isEmpty()) {
        qDebug() << "Cannot load settings: No username provided";
        return;
    }

    // Get database manager instance
    DatabaseManager& dbManager = DatabaseManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        if (!dbManager.connect(Constants::DBPath_User)) {
            qDebug() << "Failed to connect to database";
            return;
        }
    }

    // ------- Load Global Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Global)
    {
        bool validationFailed = false;

        // Display Name
        QString displayName = dbManager.GetUserData_String(username, Constants::UserT_Index_Displayname);
        if (displayName != Constants::ErrorMessage_Default && displayName != Constants::ErrorMessage_INVUSER) {
            // Validate display name
            InputValidation::ValidationResult result = InputValidation::validateInput(
                displayName, InputValidation::InputType::DisplayName, 30);

            if (result.isValid) {
                m_mainWindow->ui->lineEdit_DisplayName->setText(displayName);
                m_mainWindow->user_Displayname = displayName; // Update member variable
            } else {
                qDebug() << "Invalid display name from database:" << result.errorMessage;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load display name";
            validationFailed = true;
        }

        // Display Name Color
        QString displayNameColor = dbManager.GetUserData_String(username, Constants::UserT_Index_DisplaynameColor);
        if (displayNameColor != Constants::ErrorMessage_Default && displayNameColor != Constants::ErrorMessage_INVUSER) {
            // Validate color name
            InputValidation::ValidationResult result = InputValidation::validateInput(
                displayNameColor, InputValidation::InputType::ColorName, 20);

            if (result.isValid) {
                int index = m_mainWindow->ui->comboBox_DisplayNameColor->findText(displayNameColor);
                if (index >= 0) {
                    m_mainWindow->ui->comboBox_DisplayNameColor->setCurrentIndex(index);
                    m_mainWindow->user_nameColor = displayNameColor; // Update member variable
                } else {
                    qDebug() << "Color not found in combobox:" << displayNameColor;
                    validationFailed = true;
                }
            } else {
                qDebug() << "Invalid color name from database:" << result.errorMessage;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load display name color";
            validationFailed = true;
        }

        // Minimize to Tray
        QString minToTray = dbManager.GetUserData_String(username, Constants::UserT_Index_MinToTray);
        if (minToTray != Constants::ErrorMessage_Default && minToTray != Constants::ErrorMessage_INVUSER) {
            if (minToTray == "0" || minToTray == "1") {
                bool value = (minToTray == "1");
                m_mainWindow->ui->checkBox_MinToTray->setChecked(value);
                m_mainWindow->setting_MinToTray = value; // Update member variable
            } else {
                qDebug() << "Invalid minimize to tray value:" << minToTray;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load minimize to tray setting";
            validationFailed = true;
        }

        // Ask Password After Minimize
        QString askPW = dbManager.GetUserData_String(username, Constants::UserT_Index_AskPWAfterMinToTray);
        if (askPW != Constants::ErrorMessage_Default && askPW != Constants::ErrorMessage_INVUSER) {
            if (askPW == "0" || askPW == "1") {
                bool value = (askPW == "1");
                m_mainWindow->ui->checkBox_AskPW->setChecked(value);
                m_mainWindow->setting_AskPWAfterMin = value; // Update member variable
            } else {
                qDebug() << "Invalid ask password value:" << askPW;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load ask password setting";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some global settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_GlobalSettings(username);
            LoadSettings(Constants::DBSettings_Type_Global); // Reload with default values
            return;
        }
    }

    // ------- Load Diary Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Diary)
    {
        bool validationFailed = false;

        // Diary Text Size
        QString diaryTextSize = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_TextSize);
        if (diaryTextSize != Constants::ErrorMessage_Default && diaryTextSize != Constants::ErrorMessage_INVUSER) {
            bool ok;
            int size = diaryTextSize.toInt(&ok);
            if (ok && size >= 5 && size <= 30) {
                m_mainWindow->ui->spinBox_Diary_TextSize->setValue(size);
                m_mainWindow->setting_Diary_TextSize = size; // Update member variable
                m_mainWindow->fontSize = size; // Also update the fontSize variable
            } else {
                qDebug() << "Invalid diary text size from database:" << diaryTextSize;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load diary text size";
            validationFailed = true;
        }

        // Timestamp Timer
        QString tsTimer = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_TStampTimer);
        if (tsTimer != Constants::ErrorMessage_Default && tsTimer != Constants::ErrorMessage_INVUSER) {
            bool ok;
            int timer = tsTimer.toInt(&ok);
            if (ok && timer >= 1 && timer <= 60) {
                m_mainWindow->ui->spinBox_Diary_TStampTimer->setValue(timer);
                m_mainWindow->setting_Diary_TStampTimer = timer; // Update member variable
            } else {
                qDebug() << "Invalid timestamp timer from database:" << tsTimer;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load timestamp timer";
            validationFailed = true;
        }

        // Timestamp Counter
        QString tsCounter = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_TStampCounter);
        if (tsCounter != Constants::ErrorMessage_Default && tsCounter != Constants::ErrorMessage_INVUSER) {
            bool ok;
            int counter = tsCounter.toInt(&ok);
            if (ok && counter >= 1 && counter <= 100) {
                m_mainWindow->ui->spinBox_Diary_TStampReset->setValue(counter);
                m_mainWindow->setting_Diary_TStampCounter = counter; // Update member variable
            } else {
                qDebug() << "Invalid timestamp counter from database:" << tsCounter;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load timestamp counter";
            validationFailed = true;
        }

        // Can Edit Recent
        QString canEditRecent = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_CanEditRecent);
        if (canEditRecent != Constants::ErrorMessage_Default && canEditRecent != Constants::ErrorMessage_INVUSER) {
            if (canEditRecent == "0" || canEditRecent == "1") {
                bool value = (canEditRecent == "1");
                m_mainWindow->ui->checkBox_Diary_CanEditRecent->setChecked(value);
                m_mainWindow->setting_Diary_CanEditRecent = value; // Update member variable
            } else {
                qDebug() << "Invalid can edit recent value:" << canEditRecent;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load can edit recent setting";
            validationFailed = true;
        }

        // Show Task Manager Logs
        QString showTManLogs = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_ShowTManLogs);
        if (showTManLogs != Constants::ErrorMessage_Default && showTManLogs != Constants::ErrorMessage_INVUSER) {
            if (showTManLogs == "0" || showTManLogs == "1") {
                bool value = (showTManLogs == "1");
                m_mainWindow->ui->checkBox_Diary_TManLogs->setChecked(value);
                m_mainWindow->setting_Diary_ShowTManLogs = value; // Update member variable
            } else {
                qDebug() << "Invalid show task manager logs value:" << showTManLogs;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load show task manager logs setting";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some diary settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_DiarySettings(username);
            LoadSettings(Constants::DBSettings_Type_Diary); // Reload with default values
            return;
        }
    }

    // ------- Load Task Lists Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Tasklists)
    {
        bool validationFailed = false;

        // Log to Diary
        QString logToDiary = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_LogToDiary);
        if (logToDiary != Constants::ErrorMessage_Default && logToDiary != Constants::ErrorMessage_INVUSER) {
            if (logToDiary == "0" || logToDiary == "1") {
                bool value = (logToDiary == "1");
                m_mainWindow->ui->checkBox_TList_LogToDiary->setChecked(value);
                m_mainWindow->setting_TLists_LogToDiary = value; // Update member variable
            } else {
                qDebug() << "Invalid log to diary value:" << logToDiary;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load log to diary setting";
            validationFailed = true;
        }

        // Task Type
        QString taskType = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_TaskType);
        if (taskType != Constants::ErrorMessage_Default && taskType != Constants::ErrorMessage_INVUSER) {
            QStringList validTaskTypes = {"Simple", "Time Limit", "Recurrent"};
            if (validTaskTypes.contains(taskType)) {
                int index = m_mainWindow->ui->comboBox_TList_TaskType->findText(taskType);
                if (index >= 0) {
                    m_mainWindow->ui->comboBox_TList_TaskType->setCurrentIndex(index);
                    m_mainWindow->setting_TLists_TaskType = taskType; // Update member variable
                } else {
                    qDebug() << "Task type not found in combobox:" << taskType;
                    validationFailed = true;
                }
            } else {
                qDebug() << "Invalid task type value:" << taskType;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load task type setting";
            validationFailed = true;
        }

        // Congratulatory Message
        QString cMess = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_CMess);
        if (cMess != Constants::ErrorMessage_Default && cMess != Constants::ErrorMessage_INVUSER) {
            QStringList validMessageTypes = {"None", "Simple", "Advanced", "Intense", "Extreme"};
            if (validMessageTypes.contains(cMess)) {
                int index = m_mainWindow->ui->comboBox_TList_CMess->findText(cMess);
                if (index >= 0) {
                    m_mainWindow->ui->comboBox_TList_CMess->setCurrentIndex(index);
                    m_mainWindow->setting_TLists_CMess = cMess; // Update member variable
                } else {
                    qDebug() << "Congratulatory message type not found in combobox:" << cMess;
                    validationFailed = true;
                }
            } else {
                qDebug() << "Invalid congratulatory message value:" << cMess;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load congratulatory message setting";
            validationFailed = true;
        }

        // Punitive Message
        QString pMess = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_PMess);
        if (pMess != Constants::ErrorMessage_Default && pMess != Constants::ErrorMessage_INVUSER) {
            QStringList validMessageTypes = {"None", "Simple", "Advanced", "Intense", "Extreme"};
            if (validMessageTypes.contains(pMess)) {
                int index = m_mainWindow->ui->comboBox_TList_PMess->findText(pMess);
                if (index >= 0) {
                    m_mainWindow->ui->comboBox_TList_PMess->setCurrentIndex(index);
                    m_mainWindow->setting_TLists_PMess = pMess; // Update member variable
                } else {
                    qDebug() << "Punitive message type not found in combobox:" << pMess;
                    validationFailed = true;
                }
            } else {
                qDebug() << "Invalid punitive message value:" << pMess;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load punitive message setting";
            validationFailed = true;
        }

        // Notifications
        QString notif = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_Notif);
        if (notif != Constants::ErrorMessage_Default && notif != Constants::ErrorMessage_INVUSER) {
            if (notif == "0" || notif == "1") {
                bool value = (notif == "1");
                m_mainWindow->ui->checkBox_TList_Notif->setChecked(value);
                m_mainWindow->setting_TLists_Notif = value; // Update member variable
            } else {
                qDebug() << "Invalid notifications value:" << notif;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load notifications setting";
            validationFailed = true;
        }

        // Text Size
        QString tlistTextSize = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_TextSize);
        if (tlistTextSize != Constants::ErrorMessage_Default && tlistTextSize != Constants::ErrorMessage_INVUSER) {
            bool ok;
            int size = tlistTextSize.toInt(&ok);
            if (ok && size >= 5 && size <= 30) {
                m_mainWindow->ui->spinBox_TList_TextSize->setValue(size);
                m_mainWindow->setting_TLists_TextSize = size; // Update member variable
            } else {
                qDebug() << "Invalid task list text size from database:" << tlistTextSize;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load task list text size";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some task list settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_TasklistsSettings(username);
            LoadSettings(Constants::DBSettings_Type_Tasklists); // Reload with default values
            return;
        }
    }

    // ------- Load Password Manager Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_PWManager)
    {
        bool validationFailed = false;

        // Default Sorting Method
        QString defSortingMethod = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_DefSortingMethod);
        if (defSortingMethod != Constants::ErrorMessage_Default && defSortingMethod != Constants::ErrorMessage_INVUSER) {
            QStringList validSortingMethods = {"Password", "Account", "Service"};
            if (validSortingMethods.contains(defSortingMethod)) {
                int index = m_mainWindow->ui->comboBox_PWMan_SortBy->findText(defSortingMethod);
                if (index >= 0) {
                    m_mainWindow->ui->comboBox_PWMan_SortBy->setCurrentIndex(index);
                    m_mainWindow->setting_PWMan_DefSortingMethod = defSortingMethod; // Update member variable
                } else {
                    qDebug() << "Sorting method not found in combobox:" << defSortingMethod;
                    validationFailed = true;
                }
            } else {
                qDebug() << "Invalid sorting method value:" << defSortingMethod;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load default sorting method";
            validationFailed = true;
        }

        // Require Password
        QString reqPassword = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_ReqPassword);
        if (reqPassword != Constants::ErrorMessage_Default && reqPassword != Constants::ErrorMessage_INVUSER) {
            if (reqPassword == "0" || reqPassword == "1") {
                bool value = (reqPassword == "1");
                m_mainWindow->ui->checkBox_PWMan_ReqPW->setChecked(value);
                m_mainWindow->setting_PWMan_ReqPassword = value; // Update member variable
            } else {
                qDebug() << "Invalid require password value:" << reqPassword;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load require password setting";
            validationFailed = true;
        }

        // Hide Passwords
        QString hidePasswords = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_HidePasswords);
        if (hidePasswords != Constants::ErrorMessage_Default && hidePasswords != Constants::ErrorMessage_INVUSER) {
            if (hidePasswords == "0" || hidePasswords == "1") {
                bool value = (hidePasswords == "1");
                m_mainWindow->ui->checkBox_PWMan_HidePWS->setChecked(value);
                m_mainWindow->setting_PWMan_HidePasswords = value; // Update member variable
            } else {
                qDebug() << "Invalid hide passwords value:" << hidePasswords;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load hide passwords setting";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some password manager settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_PWManagerSettings(username);
            LoadSettings(Constants::DBSettings_Type_PWManager); // Reload with default values
            return;
        }
    }

    // ------- Load Encrypted Data Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_EncryptedData)
    {
        bool validationFailed = false;

        // Require Password
        QString reqPassword = dbManager.GetUserData_String(username, Constants::UserT_Index_DataENC_ReqPassword);
        if (reqPassword != Constants::ErrorMessage_Default && reqPassword != Constants::ErrorMessage_INVUSER) {
            if (reqPassword == "0" || reqPassword == "1") {
                bool value = (reqPassword == "1");
                m_mainWindow->ui->checkBox_DataENC_ReqPW->setChecked(value);
                m_mainWindow->setting_DataENC_ReqPassword = value;
            } else {
                qDebug() << "Invalid encrypted data require password value:" << reqPassword;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load encrypted data require password setting";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some encrypted data settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_EncryptedDataSettings(username);
            LoadSettings(Constants::DBSettings_Type_EncryptedData); // Reload with default values
            return;
        }
    }

    // Update button states after loading
    UpdateButtonStates(settingsType);
    m_mainWindow->UpdateTasklistTextSize();
    qDebug() << "Settings loaded successfully for user:" << username;
}

void Operations_Settings::SaveSettings(const QString& settingsType)
{
    // Validate input before saving
    if (!ValidateSettingsInput(settingsType)) {
        return; // Validation failed, don't save
    }

    // Get the username from MainWindow
    QString username = m_mainWindow->user_Username;

    if (username.isEmpty()) {
        qDebug() << "Cannot save settings: No username provided";
        return;
    }

    // Get database manager instance
    DatabaseManager& dbManager = DatabaseManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        if (!dbManager.connect(Constants::DBPath_User)) {
            qDebug() << "Failed to connect to database";
            return;
        }
    }

    // ------- Save Global Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Global)
    {
        // Display Name
        QString displayName = m_mainWindow->ui->lineEdit_DisplayName->text();
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_Displayname, displayName);
        m_mainWindow->user_Displayname = displayName; // Update member variable

        // Display Name Color
        QString displayNameColor = m_mainWindow->ui->comboBox_DisplayNameColor->currentText();
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_DisplaynameColor, displayNameColor);
        m_mainWindow->user_nameColor = displayNameColor; // Update member variable

        // Minimize to Tray
        bool minToTray = m_mainWindow->ui->checkBox_MinToTray->isChecked();
        QString minToTrayStr = minToTray ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_MinToTray, minToTrayStr);
        m_mainWindow->setting_MinToTray = minToTray; // Update member variable

        // Ask Password After Minimize
        bool askPW = m_mainWindow->ui->checkBox_AskPW->isChecked();
        QString askPWStr = askPW ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_AskPWAfterMinToTray, askPWStr);
        m_mainWindow->setting_AskPWAfterMin = askPW; // Update member variable
    }

    // ------- Save Diary Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Diary)
    {
        // Diary Text Size
        int diaryTextSize = m_mainWindow->ui->spinBox_Diary_TextSize->value();
        QString diaryTextSizeStr = QString::number(diaryTextSize);
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_TextSize, diaryTextSizeStr);
        m_mainWindow->setting_Diary_TextSize = diaryTextSize; // Update member variable

        // Timestamp Timer
        int tsTimer = m_mainWindow->ui->spinBox_Diary_TStampTimer->value();
        QString tsTimerStr = QString::number(tsTimer);
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_TStampTimer, tsTimerStr);
        m_mainWindow->setting_Diary_TStampTimer = tsTimer; // Update member variable

        // Timestamp Counter
        int tsCounter = m_mainWindow->ui->spinBox_Diary_TStampReset->value();
        QString tsCounterStr = QString::number(tsCounter);
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_TStampCounter, tsCounterStr);
        m_mainWindow->setting_Diary_TStampCounter = tsCounter; // Update member variable

        // Can Edit Recent
        bool canEditRecent = m_mainWindow->ui->checkBox_Diary_CanEditRecent->isChecked();
        QString canEditRecentStr = canEditRecent ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_CanEditRecent, canEditRecentStr);
        m_mainWindow->setting_Diary_CanEditRecent = canEditRecent; // Update member variable

        // Show Task Manager Logs
        bool showTManLogs = m_mainWindow->ui->checkBox_Diary_TManLogs->isChecked();
        QString showTManLogsStr = showTManLogs ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_Diary_ShowTManLogs, showTManLogsStr);
        m_mainWindow->setting_Diary_ShowTManLogs = showTManLogs; // Update member variable
    }

    // ------- Save Task Lists Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Tasklists)
    {
        // Log to Diary
        bool logToDiary = m_mainWindow->ui->checkBox_TList_LogToDiary->isChecked();
        QString logToDiaryStr = logToDiary ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_LogToDiary, logToDiaryStr);
        m_mainWindow->setting_TLists_LogToDiary = logToDiary; // Update member variable

        // Task Type
        QString taskType = m_mainWindow->ui->comboBox_TList_TaskType->currentText();
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_TaskType, taskType);
        m_mainWindow->setting_TLists_TaskType = taskType; // Update member variable

        // Congratulatory Message
        QString cMess = m_mainWindow->ui->comboBox_TList_CMess->currentText();
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_CMess, cMess);
        m_mainWindow->setting_TLists_CMess = cMess; // Update member variable

        // Punitive Message
        QString pMess = m_mainWindow->ui->comboBox_TList_PMess->currentText();
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_PMess, pMess);
        m_mainWindow->setting_TLists_PMess = pMess; // Update member variable

        // Notifications
        bool notif = m_mainWindow->ui->checkBox_TList_Notif->isChecked();
        QString notifStr = notif ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_Notif, notifStr);
        m_mainWindow->setting_TLists_Notif = notif; // Update member variable

        // Text Size
        int tlistTextSize = m_mainWindow->ui->spinBox_TList_TextSize->value();
        QString tlistTextSizeStr = QString::number(tlistTextSize);
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_TLists_TextSize, tlistTextSizeStr);
        m_mainWindow->setting_TLists_TextSize = tlistTextSize; // Update member variable
    }

    // ------- Save Password Manager Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_PWManager)
    {
        // Default Sorting Method
        QString defSortingMethod = m_mainWindow->ui->comboBox_PWMan_SortBy->currentText();
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_PWMan_DefSortingMethod, defSortingMethod);
        m_mainWindow->setting_PWMan_DefSortingMethod = defSortingMethod; // Update member variable
        m_mainWindow->ui->comboBox_PWSortBy->setCurrentIndex(Operations::GetIndexFromText(m_mainWindow->setting_PWMan_DefSortingMethod, m_mainWindow->ui->comboBox_PWSortBy)); // set current value for pwmanager combo box to that of saved settings for default

        // Require Password
        bool reqPassword = m_mainWindow->ui->checkBox_PWMan_ReqPW->isChecked();
        QString reqPasswordStr = reqPassword ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_PWMan_ReqPassword, reqPasswordStr);
        m_mainWindow->setting_PWMan_ReqPassword = reqPassword; // Update member variable

        // Hide Passwords
        bool hidePasswords = m_mainWindow->ui->checkBox_PWMan_HidePWS->isChecked();
        QString hidePasswordsStr = hidePasswords ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_PWMan_HidePasswords, hidePasswordsStr);
        m_mainWindow->setting_PWMan_HidePasswords = hidePasswords; // Update member variable
    }

    // ------- Save Encrypted Data Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_EncryptedData)
    {
        // Require Password
        bool reqPassword = m_mainWindow->ui->checkBox_DataENC_ReqPW->isChecked();
        QString reqPasswordStr = reqPassword ? "1" : "0";
        dbManager.UpdateUserData_TEXT(username, Constants::UserT_Index_DataENC_ReqPassword, reqPasswordStr);
        m_mainWindow->setting_DataENC_ReqPassword = reqPassword; // update member variable
    }

    // Update button states after saving
    UpdateButtonStates(settingsType);
    m_mainWindow->UpdateTasklistTextSize();
    qDebug() << "Settings saved successfully for user:" << username;
    m_mainWindow->ui->tableWidget_PWDisplay->clear();
    m_mainWindow->ui->tableWidget_PWDisplay->setColumnCount(0);

    m_mainWindow->ApplySettings(); // applies the setting for diaries and reloads them.
}

bool Operations_Settings::ValidateSettingsInput(const QString& settingsType)
{
    bool isValid = true;
    QString errorMessage = "The following settings are invalid:\n";

    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Global)
    {
        // Validate Display Name
        InputValidation::ValidationResult displayNameResult = InputValidation::validateInput(
            m_mainWindow->ui->lineEdit_DisplayName->text(),
            InputValidation::InputType::DisplayName,
            30);

        if (!displayNameResult.isValid) {
            isValid = false;
            errorMessage += "- Display Name: " + displayNameResult.errorMessage + "\n";
        }

        // Validate Display Name Color (from combo box)
        InputValidation::ValidationResult colorResult = InputValidation::validateInput(
            m_mainWindow->ui->comboBox_DisplayNameColor->currentText(),
            InputValidation::InputType::ColorName,
            20);

        if (!colorResult.isValid) {
            isValid = false;
            errorMessage += "- Display Name Color: " + colorResult.errorMessage + "\n";
        }
    }

    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Diary)
    {
        // Validate numeric values
        int diaryTextSize = m_mainWindow->ui->spinBox_Diary_TextSize->value();
        if (diaryTextSize < 10 || diaryTextSize > 30) {
            isValid = false;
            errorMessage += "- Diary Text Size: Must be between 10 and 30\n";
        }

        int timestampTimer = m_mainWindow->ui->spinBox_Diary_TStampTimer->value();
        if (timestampTimer < 1 || timestampTimer > 60) {
            isValid = false;
            errorMessage += "- Timestamp Timer: Must be between 1 and 60\n";
        }

        int timestampCounter = m_mainWindow->ui->spinBox_Diary_TStampReset->value();
        if (timestampCounter < 0 || timestampCounter > 99) {
            isValid = false;
            errorMessage += "- Timestamp Counter: Must be between 0 and 99\n";
        }
    }

    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Tasklists)
    {
        // Validate Task Lists settings
        int textSize = m_mainWindow->ui->spinBox_TList_TextSize->value();
        if (textSize < 5 || textSize > 30) {
            isValid = false;
            errorMessage += "- Task List Text Size: Must be between 5 and 30\n";
        }

        // Task type validation - make sure it's one of the expected values
        QString taskType = m_mainWindow->ui->comboBox_TList_TaskType->currentText();
        if (taskType != "Simple" && taskType != "Time Limit" && taskType != "Recurrent") {
            isValid = false;
            errorMessage += "- Task Type: Invalid selection\n";
        }

        // Message type validation
        QString cMess = m_mainWindow->ui->comboBox_TList_CMess->currentText();
        QString pMess = m_mainWindow->ui->comboBox_TList_PMess->currentText();
        QStringList validMessageTypes = {"None", "Simple", "Advanced", "Intense", "Extreme"};

        if (!validMessageTypes.contains(cMess)) {
            isValid = false;
            errorMessage += "- Congratulatory Message: Invalid selection\n";
        }

        if (!validMessageTypes.contains(pMess)) {
            isValid = false;
            errorMessage += "- Punitive Message: Invalid selection\n";
        }
    }

    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_PWManager)
    {
        // Validate Password Manager settings
        QString sortBy = m_mainWindow->ui->comboBox_PWMan_SortBy->currentText();
        QStringList validSortTypes = {"Password", "Account", "Service"};

        if (!validSortTypes.contains(sortBy)) {
            isValid = false;
            errorMessage += "- Default Sorting Method: Invalid selection\n";
        }
    }

    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_EncryptedData)
    {
        // For now, no specific validation needed for encrypted data settings
        // The checkbox is either checked or not, both are valid states
    }

    if (!isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Settings", errorMessage);
    }

    return isValid;
}

void Operations_Settings::UpdateButtonStates(const QString& settingsType)
{
    QString username = m_mainWindow->user_Username;
    if (username.isEmpty()) {
        return;
    }

    DatabaseManager& dbManager = DatabaseManager::instance();
    if (!dbManager.isConnected()) {
        if (!dbManager.connect(Constants::DBPath_User)) {
            qDebug() << "Failed to connect to database";
            return;
        }
    }

    // Style for disabled buttons (grey out for dark theme)
    QString disabledStyle = "color: #888888; background-color: #444444;";
    QString enabledStyle = ""; // Default style

    if (settingsType == Constants::DBSettings_Type_Global) {
        // Check if current UI values match database values
        bool matchesDatabase = true;
        bool matchesDefault = true;

        // Display Name
        QString dbDisplayName = dbManager.GetUserData_String(username, Constants::UserT_Index_Displayname);
        QString uiDisplayName = m_mainWindow->ui->lineEdit_DisplayName->text();
        if (dbDisplayName != uiDisplayName) {
            matchesDatabase = false;
        }

        // Display Name Color
        QString dbColor = dbManager.GetUserData_String(username, Constants::UserT_Index_DisplaynameColor);
        QString uiColor = m_mainWindow->ui->comboBox_DisplayNameColor->currentText();
        if (dbColor != uiColor) {
            matchesDatabase = false;
        }
        if (uiColor != Default_UserSettings::DEFAULT_DISPLAY_NAME_COLOR) {
            matchesDefault = false;
        }

        // Minimize to Tray
        QString dbMinToTray = dbManager.GetUserData_String(username, Constants::UserT_Index_MinToTray);
        QString uiMinToTray = m_mainWindow->ui->checkBox_MinToTray->isChecked() ? "1" : "0";
        if (dbMinToTray != uiMinToTray) {
            matchesDatabase = false;
        }
        if (uiMinToTray != Default_UserSettings::DEFAULT_MIN_TO_TRAY) {
            matchesDefault = false;
        }

        // Ask Password After Minimize
        QString dbAskPW = dbManager.GetUserData_String(username, Constants::UserT_Index_AskPWAfterMinToTray);
        QString uiAskPW = m_mainWindow->ui->checkBox_AskPW->isChecked() ? "1" : "0";
        if (dbAskPW != uiAskPW) {
            matchesDatabase = false;
        }
        if (uiAskPW != Default_UserSettings::DEFAULT_ASK_PW_AFTER_MIN) {
            matchesDefault = false;
        }

        // Update button states
        m_mainWindow->ui->pushButton_Acc_Save->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_Acc_Cancel->setEnabled(!matchesDatabase);

        // Apply styling
        m_mainWindow->ui->pushButton_Acc_Save->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_Acc_Cancel->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
    }

    if (settingsType == Constants::DBSettings_Type_Diary) {
        // Check if current UI values match database values
        bool matchesDatabase = true;
        bool matchesDefault = true;

        // Diary Text Size
        QString dbTextSize = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_TextSize);
        QString uiTextSize = QString::number(m_mainWindow->ui->spinBox_Diary_TextSize->value());
        if (dbTextSize != uiTextSize) {
            matchesDatabase = false;
        }
        if (uiTextSize != Default_UserSettings::DEFAULT_DIARY_TEXT_SIZE) {
            matchesDefault = false;
        }

        // Timestamp Timer
        QString dbTimer = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_TStampTimer);
        QString uiTimer = QString::number(m_mainWindow->ui->spinBox_Diary_TStampTimer->value());
        if (dbTimer != uiTimer) {
            matchesDatabase = false;
        }
        if (uiTimer != Default_UserSettings::DEFAULT_DIARY_TSTAMP_TIMER) {
            matchesDefault = false;
        }

        // Timestamp Counter
        QString dbCounter = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_TStampCounter);
        QString uiCounter = QString::number(m_mainWindow->ui->spinBox_Diary_TStampReset->value());
        if (dbCounter != uiCounter) {
            matchesDatabase = false;
        }
        if (uiCounter != Default_UserSettings::DEFAULT_DIARY_TSTAMP_COUNTER) {
            matchesDefault = false;
        }

        // Can Edit Recent
        QString dbCanEdit = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_CanEditRecent);
        QString uiCanEdit = m_mainWindow->ui->checkBox_Diary_CanEditRecent->isChecked() ? "1" : "0";
        if (dbCanEdit != uiCanEdit) {
            matchesDatabase = false;
        }
        if (uiCanEdit != Default_UserSettings::DEFAULT_DIARY_CAN_EDIT_RECENT) {
            matchesDefault = false;
        }

        // Show Task Manager Logs
        QString dbShowLogs = dbManager.GetUserData_String(username, Constants::UserT_Index_Diary_ShowTManLogs);
        QString uiShowLogs = m_mainWindow->ui->checkBox_Diary_TManLogs->isChecked() ? "1" : "0";
        if (dbShowLogs != uiShowLogs) {
            matchesDatabase = false;
        }
        if (uiShowLogs != Default_UserSettings::DEFAULT_DIARY_SHOW_TMAN_LOGS) {
            matchesDefault = false;
        }

        // Update button states
        m_mainWindow->ui->pushButton_Diary_Save->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_Diary_Cancel->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_Diary_RDefault->setEnabled(!matchesDefault);

        // Apply styling
        m_mainWindow->ui->pushButton_Diary_Save->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_Diary_Cancel->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_Diary_RDefault->setStyleSheet(matchesDefault ? disabledStyle : enabledStyle);
    }

    if (settingsType == Constants::DBSettings_Type_Tasklists) {
        // Check if current UI values match database values
        bool matchesDatabase = true;
        bool matchesDefault = true;

        // Log to Diary
        QString dbLogToDiary = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_LogToDiary);
        QString uiLogToDiary = m_mainWindow->ui->checkBox_TList_LogToDiary->isChecked() ? "1" : "0";
        if (dbLogToDiary != uiLogToDiary) {
            matchesDatabase = false;
        }
        if (uiLogToDiary != Default_UserSettings::DEFAULT_TLISTS_LOG_TO_DIARY) {
            matchesDefault = false;
        }

        // Task Type
        QString dbTaskType = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_TaskType);
        QString uiTaskType = m_mainWindow->ui->comboBox_TList_TaskType->currentText();
        if (dbTaskType != uiTaskType) {
            matchesDatabase = false;
        }
        if (uiTaskType != Default_UserSettings::DEFAULT_TLISTS_TASK_TYPE) {
            matchesDefault = false;
        }

        // Congratulatory Message
        QString dbCMess = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_CMess);
        QString uiCMess = m_mainWindow->ui->comboBox_TList_CMess->currentText();
        if (dbCMess != uiCMess) {
            matchesDatabase = false;
        }
        if (uiCMess != Default_UserSettings::DEFAULT_TLISTS_CMESS) {
            matchesDefault = false;
        }

        // Punitive Message
        QString dbPMess = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_PMess);
        QString uiPMess = m_mainWindow->ui->comboBox_TList_PMess->currentText();
        if (dbPMess != uiPMess) {
            matchesDatabase = false;
        }
        if (uiPMess != Default_UserSettings::DEFAULT_TLISTS_PMESS) {
            matchesDefault = false;
        }

        // Notifications
        QString dbNotif = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_Notif);
        QString uiNotif = m_mainWindow->ui->checkBox_TList_Notif->isChecked() ? "1" : "0";
        if (dbNotif != uiNotif) {
            matchesDatabase = false;
        }
        if (uiNotif != Default_UserSettings::DEFAULT_TLISTS_NOTIF) {
            matchesDefault = false;
        }

        // Text Size
        QString dbTextSize = dbManager.GetUserData_String(username, Constants::UserT_Index_TLists_TextSize);
        QString uiTextSize = QString::number(m_mainWindow->ui->spinBox_TList_TextSize->value());
        if (dbTextSize != uiTextSize) {
            matchesDatabase = false;
        }
        if (uiTextSize != Default_UserSettings::DEFAULT_TLISTS_TEXT_SIZE) {
            matchesDefault = false;
        }

        // Update button states
        m_mainWindow->ui->pushButton_TList_Save->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_TList_Cancel->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_TList_RDefault->setEnabled(!matchesDefault);

        // Apply styling
        m_mainWindow->ui->pushButton_TList_Save->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_TList_Cancel->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_TList_RDefault->setStyleSheet(matchesDefault ? disabledStyle : enabledStyle);
    }

    if (settingsType == Constants::DBSettings_Type_PWManager) {
        // Check if current UI values match database values
        bool matchesDatabase = true;
        bool matchesDefault = true;

        // Default Sorting Method
        QString dbSortBy = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_DefSortingMethod);
        QString uiSortBy = m_mainWindow->ui->comboBox_PWMan_SortBy->currentText();
        if (dbSortBy != uiSortBy) {
            matchesDatabase = false;
        }
        if (uiSortBy != Default_UserSettings::DEFAULT_PWMAN_DEF_SORTING_METHOD) {
            matchesDefault = false;
        }

        // Require Password
        QString dbReqPW = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_ReqPassword);
        QString uiReqPW = m_mainWindow->ui->checkBox_PWMan_ReqPW->isChecked() ? "1" : "0";
        if (dbReqPW != uiReqPW) {
            matchesDatabase = false;
        }
        if (uiReqPW != Default_UserSettings::DEFAULT_PWMAN_REQ_PASSWORD) {
            matchesDefault = false;
        }

        // Hide Passwords
        QString dbHidePW = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_HidePasswords);
        QString uiHidePW = m_mainWindow->ui->checkBox_PWMan_HidePWS->isChecked() ? "1" : "0";
        if (dbHidePW != uiHidePW) {
            matchesDatabase = false;
        }
        if (uiHidePW != Default_UserSettings::DEFAULT_PWMAN_HIDE_PASSWORDS) {
            matchesDefault = false;
        }

        // Update button states
        m_mainWindow->ui->pushButton_PWMan_Save->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_PWMan_Cancel->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_PWMan_RDefault->setEnabled(!matchesDefault);

        // Apply styling
        m_mainWindow->ui->pushButton_PWMan_Save->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_PWMan_Cancel->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_PWMan_RDefault->setStyleSheet(matchesDefault ? disabledStyle : enabledStyle);
    }

    if (settingsType == Constants::DBSettings_Type_EncryptedData) {
        // Check if current UI values match database values
        bool matchesDatabase = true;
        bool matchesDefault = true;

        // Require Password
        QString dbReqPW = dbManager.GetUserData_String(username, Constants::UserT_Index_DataENC_ReqPassword);
        QString uiReqPW = m_mainWindow->ui->checkBox_DataENC_ReqPW->isChecked() ? "1" : "0";
        if (dbReqPW != uiReqPW) {
            matchesDatabase = false;
        }
        if (uiReqPW != Default_UserSettings::DEFAULT_DATAENC_REQ_PASSWORD) {
            matchesDefault = false;
        }

        // Update button states
        m_mainWindow->ui->pushButton_DataENC_Save->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_DataENC_Cancel->setEnabled(!matchesDatabase);
        m_mainWindow->ui->pushButton_DataENC_RDefault->setEnabled(!matchesDefault);

        // Apply styling
        m_mainWindow->ui->pushButton_DataENC_Save->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_DataENC_Cancel->setStyleSheet(matchesDatabase ? disabledStyle : enabledStyle);
        m_mainWindow->ui->pushButton_DataENC_RDefault->setStyleSheet(matchesDefault ? disabledStyle : enabledStyle);
    }
}

void Operations_Settings::InitializeCustomCheckboxes()
{
    QString username = m_mainWindow->user_Username;

    // Get the custom checkboxes
    custom_QCheckboxWidget* hidePasswordsCheckbox =
        qobject_cast<custom_QCheckboxWidget*>(m_mainWindow->ui->checkBox_PWMan_HidePWS);

    custom_QCheckboxWidget* reqPasswordCheckbox =
        qobject_cast<custom_QCheckboxWidget*>(m_mainWindow->ui->checkBox_PWMan_ReqPW);

    custom_QCheckboxWidget* askPWOnCloseCheckbox =
        qobject_cast<custom_QCheckboxWidget*>(m_mainWindow->ui->checkBox_AskPW);

    custom_QCheckboxWidget* dataEncReqPWCheckbox =
        qobject_cast<custom_QCheckboxWidget*>(m_mainWindow->ui->checkBox_DataENC_ReqPW);

    // Set validation info for each checkbox
    if (hidePasswordsCheckbox) {
        hidePasswordsCheckbox->setValidationInfo(
            "Disable 'Hide Passwords' in Password Manager", username);
        hidePasswordsCheckbox->setRequireValidation(true);
        hidePasswordsCheckbox->setValidationMode(custom_QCheckboxWidget::ValidateOnUncheck);
        // Add a database value getter for the Hide Passwords setting
        hidePasswordsCheckbox->setDatabaseValueGetter([ username]() {
            DatabaseManager& dbManager = DatabaseManager::instance();
            QString dbValue = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_HidePasswords);
            return (dbValue == "1");
        });
    }

    if (reqPasswordCheckbox) {
        reqPasswordCheckbox->setValidationInfo(
            "Disable 'Require Password' in Password Manager", username);
        reqPasswordCheckbox->setRequireValidation(true);
        reqPasswordCheckbox->setValidationMode(custom_QCheckboxWidget::ValidateOnUncheck);
        reqPasswordCheckbox->setDatabaseValueGetter([ username]() {
            DatabaseManager& dbManager = DatabaseManager::instance();
            QString dbValue = dbManager.GetUserData_String(username, Constants::UserT_Index_PWMan_ReqPassword);
            return (dbValue == "1");
        });
    }

    if (askPWOnCloseCheckbox) {
        askPWOnCloseCheckbox->setValidationInfo(
            "Disable 'Ask Password on Close' in Account Settings", username);
        askPWOnCloseCheckbox->setRequireValidation(true);
        askPWOnCloseCheckbox->setValidationMode(custom_QCheckboxWidget::ValidateOnUncheck);
        askPWOnCloseCheckbox->setDatabaseValueGetter([ username]() {
            DatabaseManager& dbManager = DatabaseManager::instance();
            QString dbValue = dbManager.GetUserData_String(username, Constants::UserT_Index_AskPWAfterMinToTray);
            return (dbValue == "1");
        });
    }

    if (dataEncReqPWCheckbox) {
        dataEncReqPWCheckbox->setValidationInfo(
            "Disable 'Require Password' in Encrypted Data Settings", username);
        dataEncReqPWCheckbox->setRequireValidation(true);
        dataEncReqPWCheckbox->setValidationMode(custom_QCheckboxWidget::ValidateOnUncheck);
        dataEncReqPWCheckbox->setDatabaseValueGetter([ username]() {
            DatabaseManager& dbManager = DatabaseManager::instance();
            QString dbValue = dbManager.GetUserData_String(username, Constants::UserT_Index_DataENC_ReqPassword);
            return (dbValue == "1");
        });
    }
}

bool Operations_Settings::ValidatePassword(const QString& settingsType) {
    QString username = m_mainWindow->user_Username;

    // Check which settings would change based on type
    if (settingsType == Constants::DBSettings_Type_Global) {
        // Check if Ask Password After Minimize would change
        QString defaultAskPW = Default_UserSettings::DEFAULT_ASK_PW_AFTER_MIN;
        bool currentAskPW = m_mainWindow->ui->checkBox_AskPW->isChecked();
        bool defaultAskPWValue = (defaultAskPW == "1");

        if (currentAskPW != defaultAskPWValue) {
            // This setting would change, so validate password
            return PasswordValidation::validatePasswordForOperation(
                m_mainWindow, "Reset Account Settings to Default", username);
        }
    }
    else if (settingsType == Constants::DBSettings_Type_PWManager) {
        // Check if Require Password would change
        QString defaultReqPW = Default_UserSettings::DEFAULT_PWMAN_REQ_PASSWORD;
        bool currentReqPW = m_mainWindow->ui->checkBox_PWMan_ReqPW->isChecked();
        bool defaultReqPWValue = (defaultReqPW == "1");

        // Check if Hide Passwords would change
        QString defaultHidePW = Default_UserSettings::DEFAULT_PWMAN_HIDE_PASSWORDS;
        bool currentHidePW = m_mainWindow->ui->checkBox_PWMan_HidePWS->isChecked();
        bool defaultHidePWValue = (defaultHidePW == "1");

        if (currentReqPW != defaultReqPWValue || currentHidePW != defaultHidePWValue) {
            // These settings would change, so validate password
            return PasswordValidation::validatePasswordForOperation(
                m_mainWindow, "Reset Password Manager Settings to Default", username);
        }
    }
    else if (settingsType == Constants::DBSettings_Type_EncryptedData) {
        // Check if Require Password would change
        QString defaultReqPW = Default_UserSettings::DEFAULT_DATAENC_REQ_PASSWORD;
        bool currentReqPW = m_mainWindow->ui->checkBox_DataENC_ReqPW->isChecked();
        bool defaultReqPWValue = (defaultReqPW == "1");

        if (currentReqPW != defaultReqPWValue) {
            // This setting would change, so validate password
            return PasswordValidation::validatePasswordForOperation(
                m_mainWindow, "Reset Encrypted Data Settings to Default", username);
        }
    }

    // No password validation needed
    return true;
}

//-----misc------//

bool Operations_Settings::hasUnsavedChanges(const QString& settingsType)
{
    if (settingsType == Constants::DBSettings_Type_Diary) {
        return m_mainWindow->ui->pushButton_Diary_Save->isEnabled();
    }
    else if (settingsType == Constants::DBSettings_Type_Tasklists) {
        return m_mainWindow->ui->pushButton_TList_Save->isEnabled();
    }
    else if (settingsType == Constants::DBSettings_Type_PWManager) {
        return m_mainWindow->ui->pushButton_PWMan_Save->isEnabled();
    }
    else if (settingsType == Constants::DBSettings_Type_Global) {
        return m_mainWindow->ui->pushButton_Acc_Save->isEnabled();
    }
    else if (settingsType == Constants::DBSettings_Type_EncryptedData) {
        return m_mainWindow->ui->pushButton_DataENC_Save->isEnabled();
    }
    else if (settingsType == Constants::DBSettings_Type_ALL) {
        return hasUnsavedChanges(Constants::DBSettings_Type_Diary) ||
               hasUnsavedChanges(Constants::DBSettings_Type_Tasklists) ||
               hasUnsavedChanges(Constants::DBSettings_Type_PWManager) ||
               hasUnsavedChanges(Constants::DBSettings_Type_Global) ||
               hasUnsavedChanges(Constants::DBSettings_Type_EncryptedData);
    }

    return false;
}

bool Operations_Settings::handleUnsavedChanges(const QString& settingsType, int newTabIndex)
{
    if (!hasUnsavedChanges(settingsType)) {
        // No unsaved changes, allow tab change
        return true;
    }

    QString message;
    if (settingsType == Constants::DBSettings_Type_ALL) {
        message = "Unsaved changes in settings tab.";
    } else {
        QString categoryName;
        if (settingsType == Constants::DBSettings_Type_Diary) {
            categoryName = "diary";
        } else if (settingsType == Constants::DBSettings_Type_Tasklists) {
            categoryName = "task list";
        } else if (settingsType == Constants::DBSettings_Type_PWManager) {
            categoryName = "password manager";
        } else if (settingsType == Constants::DBSettings_Type_Global) {
            categoryName = "account";
        } else if (settingsType == Constants::DBSettings_Type_EncryptedData) {
            categoryName = "encrypted data";
        }

        message = QString("Unsaved changes for %1 settings.").arg(categoryName);
    }

    // Create message box with three buttons
    QMessageBox msgBox(m_mainWindow);
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Information);

    QPushButton *saveButton = msgBox.addButton("Save Changes", QMessageBox::AcceptRole);
    QPushButton *cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton *discardButton = msgBox.addButton("Discard Changes", QMessageBox::DestructiveRole);

    msgBox.exec();

    if (msgBox.clickedButton() == discardButton) {
        // Discard changes and allow tab change
        LoadSettings(settingsType);
        return true;
    }
    else if (msgBox.clickedButton() == saveButton) {
        // Save changes and allow tab change
        if (settingsType == Constants::DBSettings_Type_ALL) {
            // Save each category individually if it has changes
            if (hasUnsavedChanges(Constants::DBSettings_Type_Global)) {SaveSettings(Constants::DBSettings_Type_Global);}
            if (hasUnsavedChanges(Constants::DBSettings_Type_Diary)) {
                SaveSettings(Constants::DBSettings_Type_Diary);
            }
            if (hasUnsavedChanges(Constants::DBSettings_Type_Tasklists)) {
                SaveSettings(Constants::DBSettings_Type_Tasklists);
            }
            if (hasUnsavedChanges(Constants::DBSettings_Type_PWManager)) {
                SaveSettings(Constants::DBSettings_Type_PWManager);
            }
            if (hasUnsavedChanges(Constants::DBSettings_Type_EncryptedData)) {
                SaveSettings(Constants::DBSettings_Type_EncryptedData);
            }
        } else {
            // Just save the specific category
            SaveSettings(settingsType);
        }
        return true;
    }
    else {
        // Cancel was clicked, prevent tab change
        return false;
    }
}

void Operations_Settings::onSettingsTabChanged(int newIndex)
{
    // Get the object name of the previous settings tab using the stored index
    QString previousTabObjectName = getTabObjectNameByIndex(m_mainWindow->ui->tabWidget_Settings, m_previousSettingsTabIndex);

    if (previousTabObjectName.isEmpty()) {
        qDebug() << "Could not determine previous settings tab object name for index:" << m_previousSettingsTabIndex;
        // Update the previous index and continue without checking for changes
        m_previousSettingsTabIndex = newIndex;
        return;
    }

    // Convert object name to settings type
    QString currentSettingsType = getSettingsTypeFromTabObjectName(previousTabObjectName);

    qDebug() << "Settings tab changed from object name:" << previousTabObjectName
             << "(" << currentSettingsType << ") to index:" << newIndex;

    // Handle unsaved changes
    if (!handleUnsavedChanges(currentSettingsType, newIndex)) {
        // Block the signal to prevent recursion when setting back the index
        m_mainWindow->ui->tabWidget_Settings->blockSignals(true);
        m_mainWindow->ui->tabWidget_Settings->setCurrentIndex(m_previousSettingsTabIndex);
        m_mainWindow->ui->tabWidget_Settings->blockSignals(false);
    } else {
        // Update the previous index for next time
        m_previousSettingsTabIndex = newIndex;
    }
}

void Operations_Settings::onMainTabChanged(int newIndex)
{
    // Use dynamic lookup instead of hardcoded index
    int settingsTabIndex = Operations::GetTabIndexByObjectName("tab_Settings", m_mainWindow->ui->tabWidget_Main);

    if (settingsTabIndex == -1) {
        qWarning() << "Could not find Settings tab by object name";
        return; // Safety check - if we can't find the settings tab, don't proceed
    }

    // Debug output
    qDebug() << "Main tab changed from" << m_previousMainTabIndex << "to" << newIndex;
    qDebug() << "Settings tab is at index:" << settingsTabIndex;

    // Only check for unsaved changes if we're moving away from the Settings tab
    if (m_previousMainTabIndex == settingsTabIndex && newIndex != settingsTabIndex) {
        qDebug() << "Moving away from settings tab, checking for unsaved changes";
        // Check for any unsaved changes in all settings categories
        if (!handleUnsavedChanges(Constants::DBSettings_Type_ALL, newIndex)) {
            // Block the signal to prevent recursion when setting back the index
            m_mainWindow->ui->tabWidget_Main->blockSignals(true);
            m_mainWindow->ui->tabWidget_Main->setCurrentIndex(settingsTabIndex);
            m_mainWindow->ui->tabWidget_Main->blockSignals(false);
        }
    }

    // Update the previous index for next time (only if we actually switched tabs)
    if (m_mainWindow->ui->tabWidget_Main->currentIndex() == newIndex) {
        m_previousMainTabIndex = newIndex;
    }
}

void Operations_Settings::SetupSettingDescriptions()
{
    // Global Settings
    m_settingNames[m_mainWindow->ui->lineEdit_DisplayName] = "Display Name";
    m_settingDescriptions[m_mainWindow->ui->lineEdit_DisplayName] = "This is the name that will be used to represent you.\n\nUsername cannot be changed, only Display Name.";

    m_settingNames[m_mainWindow->ui->comboBox_DisplayNameColor] = "Display Name Color";
    m_settingDescriptions[m_mainWindow->ui->comboBox_DisplayNameColor] = "The color of your Name.\n\nWill Also be used for Tasklist Manager Timestamps.";

    m_settingNames[m_mainWindow->ui->checkBox_MinToTray] = "Minimize to Tray";
    m_settingDescriptions[m_mainWindow->ui->checkBox_MinToTray] = "If you want to minimize to tray when you close the app or if you want to close it entirely.\n\nUseful if you use the app often or want to receive task reminders.";

    m_settingNames[m_mainWindow->ui->checkBox_AskPW] = "Ask Password After Minimize";
    m_settingDescriptions[m_mainWindow->ui->checkBox_AskPW] = "This option will make it so your password will be required when re-opening the app after you've minimized it.\n\nIt's great if you want security but still want to receive task reminders.";

    // Diary Settings
    m_settingNames[m_mainWindow->ui->spinBox_Diary_TextSize] = "Diary Text Size";
    m_settingDescriptions[m_mainWindow->ui->spinBox_Diary_TextSize] = "The default size of the text in the Diary.\n\nYou can zoom in and out with ctrl+mousewheel.";

    m_settingNames[m_mainWindow->ui->spinBox_Diary_TStampTimer] = "Timestamp Timer";
    m_settingDescriptions[m_mainWindow->ui->spinBox_Diary_TStampTimer] = "How many minutes should pass before a new timestamp is added when typing to diary.";

    m_settingNames[m_mainWindow->ui->spinBox_Diary_TStampReset] = "Timestamp Counter";
    m_settingDescriptions[m_mainWindow->ui->spinBox_Diary_TStampReset] = "How many entries before a new timestamp is added.";

    m_settingNames[m_mainWindow->ui->checkBox_Diary_CanEditRecent] = "Can Edit Recent";
    m_settingDescriptions[m_mainWindow->ui->checkBox_Diary_CanEditRecent] = "Allows you to edit today's and yesterday's diary entries.\n\nYou can't edit entries in diary files older than that, but you can delete the entire diary file.\n\nThe goal is to preserve the integrity of the journal.\n\nYou can't change the past, but you can choose to forget about it.";

    m_settingNames[m_mainWindow->ui->checkBox_Diary_TManLogs] = "Show Task Manager Logs";
    m_settingDescriptions[m_mainWindow->ui->checkBox_Diary_TManLogs] = "Whether to display Task Manager Logs in the diary or not.\n\nThe Task Manager Logs are still there, just hidden when this is activated.";

    // Task Lists Settings
    m_settingNames[m_mainWindow->ui->checkBox_TList_LogToDiary] = "Log to Diary";
    m_settingDescriptions[m_mainWindow->ui->checkBox_TList_LogToDiary] = "The default value to set the option for Log to Diary when creating a new task.";

    m_settingNames[m_mainWindow->ui->comboBox_TList_TaskType] = "Task Type";
    m_settingDescriptions[m_mainWindow->ui->comboBox_TList_TaskType] = "The default task type when creating a new task.\n\nMostly useful for Simple and Time Limit tasks, depending on your use case.";

    m_settingNames[m_mainWindow->ui->comboBox_TList_CMess] = "Congratulatory Message";
    m_settingDescriptions[m_mainWindow->ui->comboBox_TList_CMess] = "The default option for congratulatory message when creating a new task.\n\nRanges from banale to absolute glaze.\n\nIs shown via a message box and in the diary if logging is activated.";

    m_settingNames[m_mainWindow->ui->comboBox_TList_PMess] = "Punitive Message";
    m_settingDescriptions[m_mainWindow->ui->comboBox_TList_PMess] = "The default option for punitive message when creating a new task.\n\nRanges from banale to Insulting.\nExtreme is not recommended.\n\nMessage is shown in the notification when a time limit task is overdue and logged in the diary if logging is activated.";

    m_settingNames[m_mainWindow->ui->checkBox_TList_Notif] = "Notifications";
    m_settingDescriptions[m_mainWindow->ui->checkBox_TList_Notif] = "Used to Activate/Deactivate notifications for task lists.\n\nUseful if you want to disable notifications temporarily or permanently.";

    m_settingNames[m_mainWindow->ui->spinBox_TList_TextSize] = "Task List Text Size";
    m_settingDescriptions[m_mainWindow->ui->spinBox_TList_TextSize] = "Size of the text for tasklists.";

    // Password Manager Settings
    m_settingNames[m_mainWindow->ui->comboBox_PWMan_SortBy] = "Default Sorting Method";
    m_settingDescriptions[m_mainWindow->ui->comboBox_PWMan_SortBy] = "The sorting method to use by default when opening the password manager.";

    m_settingNames[m_mainWindow->ui->checkBox_PWMan_ReqPW] = "Require Password";
    m_settingDescriptions[m_mainWindow->ui->checkBox_PWMan_ReqPW] = "If you want your password to be required whenever you want to access the Password Manager.\n\nIt's useful if you use this app in a public setting.";

    m_settingNames[m_mainWindow->ui->checkBox_PWMan_HidePWS] = "Hide Passwords";
    m_settingDescriptions[m_mainWindow->ui->checkBox_PWMan_HidePWS] = "Used to hide passwords.\n\nYou can still copy them to clipboard, They are just not visible.\n\nThis is good for after you've entered all your passwords.\n\nIt allows you to be able to access them without worry.\n\nIf clipboard reset timer is set to 0, meaning it is disabled, make sure to clear your clipboard after use.";

    // Encrypted Data Settings
    m_settingNames[m_mainWindow->ui->checkBox_DataENC_ReqPW] = "Require Password for Tab";
    m_settingDescriptions[m_mainWindow->ui->checkBox_DataENC_ReqPW] = "If you want your password to be required whenever you want to access the Encrypted Data tab.\n\nIt's useful if you use this app in a public setting and want to protect your encrypted files from being viewed or accessed.";

    // Install event filters on all UI controls
    QMap<QObject*, QString>::iterator it;
    for (it = m_settingNames.begin(); it != m_settingNames.end(); ++it) {
        QWidget* widget = qobject_cast<QWidget*>(it.key());
        if (widget) {
            widget->setMouseTracking(true);
            widget->installEventFilter(this);
        }
    }
}

bool Operations_Settings::eventFilter(QObject* watched, QEvent* event)
{
    // Check if the watched object is in our settings map
    if (m_settingNames.contains(watched)) {
        if (event->type() == QEvent::Enter) {
            // Mouse entered widget - display description
            DisplaySettingDescription(watched);
            return false;
        } else if (event->type() == QEvent::Leave) {
            // Mouse left widget - clear description
            ClearSettingDescription();
            return false;
        }
    }

    // Call base class implementation for other events
    return QObject::eventFilter(watched, event);
}

void Operations_Settings::DisplaySettingDescription(QObject* control)
{
    if (m_settingNames.contains(control)) {
        QString settingName = m_settingNames[control];
        m_mainWindow->ui->label_Settings_Desc_Name->setText(settingName);

        // Set the description text from the descriptions map
        if (m_settingDescriptions.contains(control)) {
            m_mainWindow->ui->textBrowser_SettingDesc->setText(m_settingDescriptions[control]);
        } else {
            m_mainWindow->ui->textBrowser_SettingDesc->clear();
        }
    }
}

void Operations_Settings::ClearSettingDescription()
{
    m_mainWindow->ui->label_Settings_Desc_Name->setText("Description");
    m_mainWindow->ui->textBrowser_SettingDesc->clear();
}

QString Operations_Settings::getTabObjectNameByIndex(QTabWidget* tabWidget, int index)
{
    if (!tabWidget || index < 0 || index >= tabWidget->count()) {
        return QString(); // Invalid index or null widget
    }

    QWidget* tabPage = tabWidget->widget(index);
    if (tabPage) {
        return tabPage->objectName();
    }

    return QString();
}

QString Operations_Settings::getSettingsTypeFromTabObjectName(const QString& tabObjectName)
{
    if (tabObjectName == "tab_Settings_Diaries") {
        return Constants::DBSettings_Type_Diary;
    }
    else if (tabObjectName == "tab_Settings_Tasklists") {
        return Constants::DBSettings_Type_Tasklists;
    }
    else if (tabObjectName == "tab_Settings_PWManager") {
        return Constants::DBSettings_Type_PWManager;
    }
    else if (tabObjectName == "tab_Settings_EncryptedData") {
        return Constants::DBSettings_Type_EncryptedData;
    }
    else {
        qDebug() << "Unknown settings tab object name:" << tabObjectName;
        return Constants::DBSettings_Type_Diary; // Default fallback
    }
}

//-----------Slots------------//

void Operations_Settings::Slot_ButtonPressed(const QString button)
{
    // Get the username from MainWindow
    QString username = m_mainWindow->user_Username;

    if (username.isEmpty()) {
        qDebug() << "Cannot process button press: No username provided";
        return;
    }

    // --- Global Settings Buttons ---
    if (button == Constants::SettingsButton_SaveGlobal) {
        SaveSettings(Constants::DBSettings_Type_Global);
    }
    else if (button == Constants::SettingsButton_CancelGlobal) {
        LoadSettings(Constants::DBSettings_Type_Global);
    }
    else if (button == Constants::SettingsButton_ResetGlobal) {
        // Validate password if needed before reset
        if (ValidatePassword(Constants::DBSettings_Type_Global)) {
            if (Default_UserSettings::SetDefault_GlobalSettings(username)) {
                LoadSettings(Constants::DBSettings_Type_Global);
            }
        }
    }

    // --- Diary Settings Buttons ---
    else if (button == Constants::SettingsButton_SaveDiary) {
        SaveSettings(Constants::DBSettings_Type_Diary);
    }
    else if (button == Constants::SettingsButton_CancelDiary) {
        LoadSettings(Constants::DBSettings_Type_Diary);
    }
    else if (button == Constants::SettingsButton_ResetDiary) {
        if (Default_UserSettings::SetDefault_DiarySettings(username)) {
            LoadSettings(Constants::DBSettings_Type_Diary);
        }
    }

    // --- Task Lists Settings Buttons ---
    else if (button == Constants::SettingsButton_SaveTasklists) {
        SaveSettings(Constants::DBSettings_Type_Tasklists);
    }
    else if (button == Constants::SettingsButton_CancelTasklists) {
        LoadSettings(Constants::DBSettings_Type_Tasklists);
    }
    else if (button == Constants::SettingsButton_ResetTasklists) {
        if (Default_UserSettings::SetDefault_TasklistsSettings(username)) {
            LoadSettings(Constants::DBSettings_Type_Tasklists);
        }
    }

    // --- Password Manager Settings Buttons ---
    else if (button == Constants::SettingsButton_SavePWManager) {
        SaveSettings(Constants::DBSettings_Type_PWManager);
    }
    else if (button == Constants::SettingsButton_CancelPWManager) {
        LoadSettings(Constants::DBSettings_Type_PWManager);
    }
    else if (button == Constants::SettingsButton_ResetPWManager) {
        // Validate password if needed before reset
        if (ValidatePassword(Constants::DBSettings_Type_PWManager)) {
            if (Default_UserSettings::SetDefault_PWManagerSettings(username)) {
                LoadSettings(Constants::DBSettings_Type_PWManager);
            }
        }
    }

    // --- Encrypted Data Settings Buttons ---
    else if (button == Constants::SettingsButton_SaveEncryptedData) {
        SaveSettings(Constants::DBSettings_Type_EncryptedData);
    }
    else if (button == Constants::SettingsButton_CancelEncryptedData) {
        LoadSettings(Constants::DBSettings_Type_EncryptedData);
    }
    else if (button == Constants::SettingsButton_ResetEncryptedData) {
        // Validate password if needed before reset
        if (ValidatePassword(Constants::DBSettings_Type_EncryptedData)) {
            if (Default_UserSettings::SetDefault_EncryptedDataSettings(username)) {
                LoadSettings(Constants::DBSettings_Type_EncryptedData);
            }
        }
    }
    else {
        qDebug() << "Unknown settings button:" << button;
    }
}

void Operations_Settings::Slot_ValueChanged(const QString settingsType)
{
    // Update button states based on the current values
    qDebug() << "DEBUGSETTINGSTYPE: " << settingsType;
    UpdateButtonStates(settingsType);
}
