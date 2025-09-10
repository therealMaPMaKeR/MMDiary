#include "operations_settings.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include "settings_default_usersettings.h"
#include "sqlite-database-settings.h"
#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include "ui_HiddenItemsList.h"

// SECURITY: Maximum number of hidden categories/tags allowed to prevent memory exhaustion
const int MAX_HIDDEN_ITEMS = 100;

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

    // SECURITY: Connect tab widget signals with verification
    bool connected = connect(m_mainWindow->ui->tabWidget_Settings, &QTabWidget::currentChanged,
            this, &Operations_Settings::onSettingsTabChanged);
    if (!connected) {
        qWarning() << "Operations_Settings: Failed to connect tabWidget_Settings signal";
    }

    connected = connect(m_mainWindow->ui->tabWidget_Main, &QTabWidget::currentChanged,
            this, &Operations_Settings::onMainTabChanged);
    if (!connected) {
        qWarning() << "Operations_Settings: Failed to connect tabWidget_Main signal";
    }

    // Connect the Hide Thumbnail Buttons Signals with verification
    connected = connect(m_mainWindow->ui->pushButton_DataENC_Hidden_Categories, &QPushButton::clicked,
            this, &Operations_Settings::onHiddenCategoriesClicked);
    if (!connected) {
        qWarning() << "Operations_Settings: Failed to connect Hidden_Categories button signal";
    }

    //Connect the Password Validation Delay spinbox signals
    connect(m_mainWindow->ui->spinBox_ReqPWDelay, QOverload<int>::of(&QSpinBox::valueChanged),
            [this]() { Slot_ValueChanged(Constants::DBSettings_Type_Global); });

    connect(m_mainWindow->ui->pushButton_DataENC_Hidden_Tags, &QPushButton::clicked,
            this, &Operations_Settings::onHiddenTagsClicked);

    // Connect the new checkbox signals for value change tracking
    connect(m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image, &QCheckBox::stateChanged,
            [this]() { Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData); });

    connect(m_mainWindow->ui->checkBox_OpenOnSettings, &QCheckBox::stateChanged,
            [this]() { Slot_ValueChanged(Constants::DBSettings_Type_Global); });

    connect(m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video, &QCheckBox::stateChanged,
            [this]() { Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData); });

    // Connect the new hide categories/tags checkbox signals for value change tracking
    connect(m_mainWindow->ui->checkBox_DataENC_HideCategories, &QCheckBox::stateChanged,
            [this]() { Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData); });

    connect(m_mainWindow->ui->checkBox_DataENC_HideTags, &QCheckBox::stateChanged,
            [this]() { Slot_ValueChanged(Constants::DBSettings_Type_EncryptedData); });


    InitializeCustomCheckboxes();
}

void Operations_Settings::LoadSettings(const QString& settingsType)
{
    // SECURITY: Check if MainWindow still exists
    if (!m_mainWindow) {
        qWarning() << "Operations_Settings: LoadSettings called with null MainWindow";
        return;
    }
    
    // Get the username and encryption key from MainWindow
    QString username = m_mainWindow->user_Username;
    QByteArray encryptionKey = m_mainWindow->user_Key;

    if (username.isEmpty()) {
        qDebug() << "Cannot load settings: No username provided";
        return;
    }

    // Get database manager instance
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Ensure database connection
    if (!db.isConnected()) {
        if (!db.connect(username, encryptionKey)) {
            qDebug() << "Failed to connect to settings database";
            return;
        }
    }

    // ------- Load Global Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Global)
    {
        bool validationFailed = false;

        // Display Name
        QString displayName = db.GetSettingsData_String(Constants::SettingsT_Index_Displayname);
        if (displayName != Constants::ErrorMessage_Default) {
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
        QString displayNameColor = db.GetSettingsData_String(Constants::SettingsT_Index_DisplaynameColor);
        if (displayNameColor != Constants::ErrorMessage_Default) {
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
        QString minToTray = db.GetSettingsData_String(Constants::SettingsT_Index_MinToTray);
        if (minToTray != Constants::ErrorMessage_Default) {
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
        QString askPW = db.GetSettingsData_String(Constants::SettingsT_Index_AskPWAfterMinToTray);
        if (askPW != Constants::ErrorMessage_Default) {
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

        // Password Request Delay
        QString reqPWDelay = db.GetSettingsData_String(Constants::SettingsT_Index_ReqPWDelay);
        if (reqPWDelay != Constants::ErrorMessage_Default) {
            bool ok;
            // SECURITY: Use long long to detect overflow before casting to int
            long long delayLong = reqPWDelay.toLongLong(&ok);
            if (ok && delayLong >= 0 && delayLong <= 300) {
                int delay = static_cast<int>(delayLong);
                m_mainWindow->ui->spinBox_ReqPWDelay->setValue(delay);
                m_mainWindow->setting_ReqPWDelay = delay; // Update member variable
            } else {
                qDebug() << "Operations_Settings: Invalid password request delay from database (overflow or out of range):" << reqPWDelay;
                validationFailed = true;
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load password request delay setting";
            validationFailed = true;
        }

        // Open on Settings
        QString openOnSettings = db.GetSettingsData_String(Constants::SettingsT_Index_OpenOnSettings);
        if (openOnSettings != Constants::ErrorMessage_Default) {
            if (openOnSettings == "0" || openOnSettings == "1") {
                bool value = (openOnSettings == "1");
                m_mainWindow->ui->checkBox_OpenOnSettings->setChecked(value);
                m_mainWindow->setting_OpenOnSettings = value; // Update member variable
            } else {
                qDebug() << "Invalid open on settings value:" << openOnSettings;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load open on settings setting";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some global settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_GlobalSettings(username, encryptionKey);
            LoadSettings(Constants::DBSettings_Type_Global); // Reload with default values
            return;
        }
    }

    // ------- Load Diary Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Diary)
    {
        bool validationFailed = false;

        // Diary Text Size
        QString diaryTextSize = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_TextSize);
        if (diaryTextSize != Constants::ErrorMessage_Default) {
            bool ok;
            // SECURITY: Use long long to detect overflow before casting to int
            long long sizeLong = diaryTextSize.toLongLong(&ok);
            // SECURITY FIX: Use consistent range 10-30 (not 5-30)
            if (ok && sizeLong >= 10 && sizeLong <= 30) {
                int size = static_cast<int>(sizeLong);
                m_mainWindow->ui->spinBox_Diary_TextSize->setValue(size);
                m_mainWindow->setting_Diary_TextSize = size; // Update member variable
                m_mainWindow->fontSize = size; // Also update the fontSize variable
            } else {
                qDebug() << "Operations_Settings: Invalid diary text size from database (overflow or out of range):" << diaryTextSize;
                validationFailed = true;
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load diary text size";
            validationFailed = true;
        }

        // Timestamp Timer
        QString tsTimer = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_TStampTimer);
        if (tsTimer != Constants::ErrorMessage_Default) {
            bool ok;
            // SECURITY: Use long long to detect overflow before casting to int
            long long timerLong = tsTimer.toLongLong(&ok);
            if (ok && timerLong >= 1 && timerLong <= 60) {
                int timer = static_cast<int>(timerLong);
                m_mainWindow->ui->spinBox_Diary_TStampTimer->setValue(timer);
                m_mainWindow->setting_Diary_TStampTimer = timer; // Update member variable
            } else {
                qDebug() << "Operations_Settings: Invalid timestamp timer from database (overflow or out of range):" << tsTimer;
                validationFailed = true;
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load timestamp timer";
            validationFailed = true;
        }

        // Timestamp Counter
        QString tsCounter = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_TStampCounter);
        if (tsCounter != Constants::ErrorMessage_Default) {
            bool ok;
            // SECURITY: Use long long to detect overflow before casting to int
            long long counterLong = tsCounter.toLongLong(&ok);
            // SECURITY FIX: Use consistent range 0-99 (as validated in ValidateSettingsInput)
            if (ok && counterLong >= 0 && counterLong <= 99) {
                int counter = static_cast<int>(counterLong);
                m_mainWindow->ui->spinBox_Diary_TStampReset->setValue(counter);
                m_mainWindow->setting_Diary_TStampCounter = counter; // Update member variable
            } else {
                qDebug() << "Operations_Settings: Invalid timestamp counter from database (overflow or out of range):" << tsCounter;
                validationFailed = true;
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load timestamp counter";
            validationFailed = true;
        }

        // Can Edit Recent
        QString canEditRecent = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_CanEditRecent);
        if (canEditRecent != Constants::ErrorMessage_Default) {
            if (canEditRecent == "0" || canEditRecent == "1") {
                bool value = (canEditRecent == "1");
                m_mainWindow->ui->checkBox_Diary_CanEditRecent->setChecked(value);
                // disabled m_mainWindow->setting_Diary_CanEditRecent = value; // Update member variable
                m_mainWindow->setting_Diary_CanEditRecent = 1; //the setting is disabled, default to 1 for safety of backwards compatibility
            } else {
                qDebug() << "Invalid can edit recent value:" << canEditRecent;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load can edit recent setting";
            validationFailed = true;
        }

        // Show Task Manager Logs
        QString showTManLogs = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_ShowTManLogs);
        if (showTManLogs != Constants::ErrorMessage_Default) {
            if (showTManLogs == "0" || showTManLogs == "1") {
                bool value = (showTManLogs == "1");
                m_mainWindow->ui->checkBox_Diary_TManLogs->setChecked(value);
                // disabled m_mainWindow->setting_Diary_ShowTManLogs = value; // Update member variable
                m_mainWindow->setting_Diary_ShowTManLogs = 0; //the setting is disabled, default to 0 for safety of backwards compatibility
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
            Default_UserSettings::SetDefault_DiarySettings(username, encryptionKey);
            LoadSettings(Constants::DBSettings_Type_Diary); // Reload with default values
            return;
        }
    }

    // ------- Load Task Lists Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Tasklists)
    {
        bool validationFailed = false;

        // Text Size
        QString tlistTextSize = db.GetSettingsData_String(Constants::SettingsT_Index_TLists_TextSize);
        if (tlistTextSize != Constants::ErrorMessage_Default) {
            bool ok;
            // SECURITY: Use long long to detect overflow before casting to int
            long long sizeLong = tlistTextSize.toLongLong(&ok);
            if (ok && sizeLong >= 5 && sizeLong <= 30) {
                int size = static_cast<int>(sizeLong);
                m_mainWindow->ui->spinBox_TList_TextSize->setValue(size);
                m_mainWindow->setting_TLists_TextSize = size; // Update member variable
                
                // Update the tasklist text size and checkbox width
                m_mainWindow->UpdateTasklistTextSize();
            } else {
                qDebug() << "Operations_Settings: Invalid task list text size from database (overflow or out of range):" << tlistTextSize;
                validationFailed = true;
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load task list text size";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some task list settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_TasklistsSettings(username, encryptionKey);
            LoadSettings(Constants::DBSettings_Type_Tasklists); // Reload with default values
            return;
        }
    }

    // ------- Load Password Manager Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_PWManager)
    {
        bool validationFailed = false;

        // Default Sorting Method
        QString defSortingMethod = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_DefSortingMethod);
        if (defSortingMethod != Constants::ErrorMessage_Default) {
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
        QString reqPassword = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_ReqPassword);
        if (reqPassword != Constants::ErrorMessage_Default) {
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
        QString hidePasswords = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_HidePasswords);
        if (hidePasswords != Constants::ErrorMessage_Default) {
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
            Default_UserSettings::SetDefault_PWManagerSettings(username, encryptionKey);
            LoadSettings(Constants::DBSettings_Type_PWManager); // Reload with default values
            return;
        }
    }

    // ------- Load Encrypted Data Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_EncryptedData)
    {
        bool validationFailed = false;

        // Require Password
        QString reqPassword = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_ReqPassword);
        if (reqPassword != Constants::ErrorMessage_Default) {
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

        // Hide Image Thumbnails
        QString hideImageThumbnails = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_HideThumbnails_Image);
        if (hideImageThumbnails != Constants::ErrorMessage_Default) {
            if (hideImageThumbnails == "0" || hideImageThumbnails == "1") {
                bool value = (hideImageThumbnails == "1");
                m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image->setChecked(value);
                m_mainWindow->setting_DataENC_HideThumbnails_Image = value;
            } else {
                qDebug() << "Invalid hide image thumbnails value:" << hideImageThumbnails;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load hide image thumbnails setting";
            validationFailed = true;
        }

        // Hide Video Thumbnails
        QString hideVideoThumbnails = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_HideThumbnails_Video);
        if (hideVideoThumbnails != Constants::ErrorMessage_Default) {
            if (hideVideoThumbnails == "0" || hideVideoThumbnails == "1") {
                bool value = (hideVideoThumbnails == "1");
                m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video->setChecked(value);
                m_mainWindow->setting_DataENC_HideThumbnails_Video = value;
            } else {
                qDebug() << "Invalid hide video thumbnails value:" << hideVideoThumbnails;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load hide video thumbnails setting";
            validationFailed = true;
        }

        // Hidden Categories
        QString hiddenCategories = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hidden_Categories);
        if (hiddenCategories != Constants::ErrorMessage_Default) {
            // SECURITY: Validate hidden categories before use
            if (!hiddenCategories.isEmpty()) {
                QStringList categories = hiddenCategories.split(';', Qt::SkipEmptyParts);
                QStringList validatedCategories;
                for (const QString& category : categories) {
                    QString trimmedCategory = category.trimmed();
                    if (!trimmedCategory.isEmpty()) {
                        InputValidation::ValidationResult result = InputValidation::validateInput(
                            trimmedCategory, InputValidation::InputType::CategoryTag, 50);
                        if (result.isValid) {
                            validatedCategories.append(trimmedCategory);
                        } else {
                            qDebug() << "Operations_Settings: Invalid hidden category detected and removed:" << trimmedCategory;
                        }
                    }
                }
                m_mainWindow->setting_DataENC_Hidden_Categories = validatedCategories.join(';');
            } else {
                m_mainWindow->setting_DataENC_Hidden_Categories = "";
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load hidden categories setting";
            validationFailed = true;
        }

        // Hidden Tags
        QString hiddenTags = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hidden_Tags);
        if (hiddenTags != Constants::ErrorMessage_Default) {
            // SECURITY: Validate hidden tags before use
            if (!hiddenTags.isEmpty()) {
                QStringList tags = hiddenTags.split(';', Qt::SkipEmptyParts);
                QStringList validatedTags;
                for (const QString& tag : tags) {
                    QString trimmedTag = tag.trimmed();
                    if (!trimmedTag.isEmpty()) {
                        InputValidation::ValidationResult result = InputValidation::validateInput(
                            trimmedTag, InputValidation::InputType::CategoryTag, 50);
                        if (result.isValid) {
                            validatedTags.append(trimmedTag);
                        } else {
                            qDebug() << "Operations_Settings: Invalid hidden tag detected and removed:" << trimmedTag;
                        }
                    }
                }
                m_mainWindow->setting_DataENC_Hidden_Tags = validatedTags.join(';');
            } else {
                m_mainWindow->setting_DataENC_Hidden_Tags = "";
            }
        } else {
            qDebug() << "Operations_Settings: Failed to load hidden tags setting";
            validationFailed = true;
        }

        // Hide Categories
        QString hideCategories = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hide_Categories);
        if (hideCategories != Constants::ErrorMessage_Default) {
            if (hideCategories == "0" || hideCategories == "1") {
                bool value = (hideCategories == "1");
                m_mainWindow->ui->checkBox_DataENC_HideCategories->setChecked(value);
                m_mainWindow->setting_DataENC_Hide_Categories = value;
            } else {
                qDebug() << "Invalid hide categories value:" << hideCategories;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load hide categories setting";
            validationFailed = true;
        }

        // Hide Tags
        QString hideTags = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hide_Tags);
        if (hideTags != Constants::ErrorMessage_Default) {
            if (hideTags == "0" || hideTags == "1") {
                bool value = (hideTags == "1");
                m_mainWindow->ui->checkBox_DataENC_HideTags->setChecked(value);
                m_mainWindow->setting_DataENC_Hide_Tags = value;
            } else {
                qDebug() << "Invalid hide tags value:" << hideTags;
                validationFailed = true;
            }
        } else {
            qDebug() << "Failed to load hide tags setting";
            validationFailed = true;
        }

        // If any validation failed, reset to defaults
        if (validationFailed) {
            qDebug() << "Some encrypted data settings failed validation, resetting to defaults";
            Default_UserSettings::SetDefault_EncryptedDataSettings(username, encryptionKey);
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
    // SECURITY: Check if MainWindow still exists
    if (!m_mainWindow) {
        qWarning() << "Operations_Settings: SaveSettings called with null MainWindow";
        return;
    }
    
    // Validate input before saving
    if (!ValidateSettingsInput(settingsType)) {
        return; // Validation failed, don't save
    }

    // Get the username and encryption key from MainWindow
    QString username = m_mainWindow->user_Username;
    QByteArray encryptionKey = m_mainWindow->user_Key;

    if (username.isEmpty()) {
        qDebug() << "Cannot save settings: No username provided";
        return;
    }

    // Get database manager instance
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Ensure database connection
    if (!db.isConnected()) {
        if (!db.connect(username, encryptionKey)) {
            qDebug() << "Failed to connect to settings database";
            return;
        }
    }

    // ------- Save Global Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Global)
    {
        // Display Name
        QString displayName = m_mainWindow->ui->lineEdit_DisplayName->text();
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Displayname, displayName);
        m_mainWindow->user_Displayname = displayName; // Update member variable

        // Display Name Color
        QString displayNameColor = m_mainWindow->ui->comboBox_DisplayNameColor->currentText();
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DisplaynameColor, displayNameColor);
        m_mainWindow->user_nameColor = displayNameColor; // Update member variable

        // Minimize to Tray
        bool minToTray = m_mainWindow->ui->checkBox_MinToTray->isChecked();
        QString minToTrayStr = minToTray ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_MinToTray, minToTrayStr);
        m_mainWindow->setting_MinToTray = minToTray; // Update member variable

        // Ask Password After Minimize
        bool askPW = m_mainWindow->ui->checkBox_AskPW->isChecked();
        QString askPWStr = askPW ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_AskPWAfterMinToTray, askPWStr);
        m_mainWindow->setting_AskPWAfterMin = askPW; // Update member variable

        // Password Request Delay
        int reqPWDelay = m_mainWindow->ui->spinBox_ReqPWDelay->value();
        QString reqPWDelayStr = QString::number(reqPWDelay);
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_ReqPWDelay, reqPWDelayStr);
        m_mainWindow->setting_ReqPWDelay = reqPWDelay; // Update member variable

        // Open on Settings
        bool openOnSettings = m_mainWindow->ui->checkBox_OpenOnSettings->isChecked();
        QString openOnSettingsStr = openOnSettings ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_OpenOnSettings, openOnSettingsStr);
        m_mainWindow->setting_OpenOnSettings = openOnSettings; // Update member variable

    }

    // ------- Save Diary Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Diary)
    {
        // Diary Text Size
        int diaryTextSize = m_mainWindow->ui->spinBox_Diary_TextSize->value();
        QString diaryTextSizeStr = QString::number(diaryTextSize);
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TextSize, diaryTextSizeStr);
        m_mainWindow->setting_Diary_TextSize = diaryTextSize; // Update member variable

        // Timestamp Timer
        int tsTimer = m_mainWindow->ui->spinBox_Diary_TStampTimer->value();
        QString tsTimerStr = QString::number(tsTimer);
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TStampTimer, tsTimerStr);
        m_mainWindow->setting_Diary_TStampTimer = tsTimer; // Update member variable

        // Timestamp Counter
        int tsCounter = m_mainWindow->ui->spinBox_Diary_TStampReset->value();
        QString tsCounterStr = QString::number(tsCounter);
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_TStampCounter, tsCounterStr);
        m_mainWindow->setting_Diary_TStampCounter = tsCounter; // Update member variable

        // Can Edit Recent
        bool canEditRecent = m_mainWindow->ui->checkBox_Diary_CanEditRecent->isChecked();
        QString canEditRecentStr = canEditRecent ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_CanEditRecent, canEditRecentStr);
        m_mainWindow->setting_Diary_CanEditRecent = canEditRecent; // Update member variable

        // Show Task Manager Logs
        bool showTManLogs = m_mainWindow->ui->checkBox_Diary_TManLogs->isChecked();
        QString showTManLogsStr = showTManLogs ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_Diary_ShowTManLogs, showTManLogsStr);
        m_mainWindow->setting_Diary_ShowTManLogs = showTManLogs; // Update member variable
    }

    // ------- Save Task Lists Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_Tasklists)
    {
        // Text Size
        int tlistTextSize = m_mainWindow->ui->spinBox_TList_TextSize->value();
        QString tlistTextSizeStr = QString::number(tlistTextSize);
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_TLists_TextSize, tlistTextSizeStr);
        m_mainWindow->setting_TLists_TextSize = tlistTextSize; // Update member variable
        
        // Update the tasklist text size and checkbox width immediately
        m_mainWindow->UpdateTasklistTextSize();
    }

    // ------- Save Password Manager Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_PWManager)
    {
        // Default Sorting Method
        QString defSortingMethod = m_mainWindow->ui->comboBox_PWMan_SortBy->currentText();
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_DefSortingMethod, defSortingMethod);
        m_mainWindow->setting_PWMan_DefSortingMethod = defSortingMethod; // Update member variable
        m_mainWindow->ui->comboBox_PWSortBy->setCurrentIndex(Operations::GetIndexFromText(m_mainWindow->setting_PWMan_DefSortingMethod, m_mainWindow->ui->comboBox_PWSortBy)); // set current value for pwmanager combo box to that of saved settings for default

        // Require Password
        bool reqPassword = m_mainWindow->ui->checkBox_PWMan_ReqPW->isChecked();
        QString reqPasswordStr = reqPassword ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_ReqPassword, reqPasswordStr);
        m_mainWindow->setting_PWMan_ReqPassword = reqPassword; // Update member variable

        // Hide Passwords
        bool hidePasswords = m_mainWindow->ui->checkBox_PWMan_HidePWS->isChecked();
        QString hidePasswordsStr = hidePasswords ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_PWMan_HidePasswords, hidePasswordsStr);
        m_mainWindow->setting_PWMan_HidePasswords = hidePasswords; // Update member variable
    }

    // ------- Save Encrypted Data Settings -------
    if (settingsType == Constants::DBSettings_Type_ALL || settingsType == Constants::DBSettings_Type_EncryptedData)
    {
        // Require Password
        bool reqPassword = m_mainWindow->ui->checkBox_DataENC_ReqPW->isChecked();
        QString reqPasswordStr = reqPassword ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_ReqPassword, reqPasswordStr);
        m_mainWindow->setting_DataENC_ReqPassword = reqPassword; // update member variable

        // Hide Image Thumbnails
        bool hideImageThumbnails = m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image->isChecked();
        QString hideImageThumbnailsStr = hideImageThumbnails ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_HideThumbnails_Image, hideImageThumbnailsStr);
        m_mainWindow->setting_DataENC_HideThumbnails_Image = hideImageThumbnails;

        // Hide Video Thumbnails
        bool hideVideoThumbnails = m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video->isChecked();
        QString hideVideoThumbnailsStr = hideVideoThumbnails ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_HideThumbnails_Video, hideVideoThumbnailsStr);
        m_mainWindow->setting_DataENC_HideThumbnails_Video = hideVideoThumbnails;

        // Hidden Categories (stored in member variable, not directly from UI)
        // SECURITY: Validate before saving
        QString categoriesToSave = m_mainWindow->setting_DataENC_Hidden_Categories;
        if (!categoriesToSave.isEmpty()) {
            QStringList categories = categoriesToSave.split(';', Qt::SkipEmptyParts);
            QStringList validatedCategories;
            for (const QString& category : categories) {
                QString trimmedCategory = category.trimmed();
                if (!trimmedCategory.isEmpty()) {
                    InputValidation::ValidationResult result = InputValidation::validateInput(
                        trimmedCategory, InputValidation::InputType::CategoryTag, 50);
                    if (result.isValid) {
                        validatedCategories.append(trimmedCategory);
                    } else {
                        qDebug() << "Operations_Settings: Removing invalid category before save:" << trimmedCategory;
                    }
                }
            }
            categoriesToSave = validatedCategories.join(';');
            m_mainWindow->setting_DataENC_Hidden_Categories = categoriesToSave;
        }
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hidden_Categories, categoriesToSave);

        // Hidden Tags (stored in member variable, not directly from UI)
        // SECURITY: Validate before saving
        QString tagsToSave = m_mainWindow->setting_DataENC_Hidden_Tags;
        if (!tagsToSave.isEmpty()) {
            QStringList tags = tagsToSave.split(';', Qt::SkipEmptyParts);
            QStringList validatedTags;
            for (const QString& tag : tags) {
                QString trimmedTag = tag.trimmed();
                if (!trimmedTag.isEmpty()) {
                    InputValidation::ValidationResult result = InputValidation::validateInput(
                        trimmedTag, InputValidation::InputType::CategoryTag, 50);
                    if (result.isValid) {
                        validatedTags.append(trimmedTag);
                    } else {
                        qDebug() << "Operations_Settings: Removing invalid tag before save:" << trimmedTag;
                    }
                }
            }
            tagsToSave = validatedTags.join(';');
            m_mainWindow->setting_DataENC_Hidden_Tags = tagsToSave;
        }
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hidden_Tags, tagsToSave);

        // Hide Categories
        bool hideCategories = m_mainWindow->ui->checkBox_DataENC_HideCategories->isChecked();
        QString hideCategoriesStr = hideCategories ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hide_Categories, hideCategoriesStr);
        m_mainWindow->setting_DataENC_Hide_Categories = hideCategories;

        // Hide Tags
        bool hideTags = m_mainWindow->ui->checkBox_DataENC_HideTags->isChecked();
        QString hideTagsStr = hideTags ? "1" : "0";
        db.UpdateSettingsData_TEXT(Constants::SettingsT_Index_DataENC_Hide_Tags, hideTagsStr);
        m_mainWindow->setting_DataENC_Hide_Tags = hideTags;


        m_mainWindow->refreshEncryptedDataDisplay();

        qDebug() << "Refreshed encrypted data display after settings change";
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

        // Password Request Delay validation
        int reqPWDelay = m_mainWindow->ui->spinBox_ReqPWDelay->value();
        if (reqPWDelay < 0 || reqPWDelay > 300) {
            isValid = false;
            errorMessage += "- Password Request Delay: Must be between 0 and 300 seconds\n";
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
    QByteArray encryptionKey = m_mainWindow->user_Key;

    if (username.isEmpty()) {
        return;
    }

    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
    if (!db.isConnected()) {
        if (!db.connect(username, encryptionKey)) {
            qDebug() << "Failed to connect to settings database";
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
        QString dbDisplayName = db.GetSettingsData_String(Constants::SettingsT_Index_Displayname);
        QString uiDisplayName = m_mainWindow->ui->lineEdit_DisplayName->text();
        if (dbDisplayName != uiDisplayName) {
            matchesDatabase = false;
        }

        // Display Name Color
        QString dbColor = db.GetSettingsData_String(Constants::SettingsT_Index_DisplaynameColor);
        QString uiColor = m_mainWindow->ui->comboBox_DisplayNameColor->currentText();
        if (dbColor != uiColor) {
            matchesDatabase = false;
        }
        if (uiColor != Default_UserSettings::DEFAULT_DISPLAY_NAME_COLOR) {
            matchesDefault = false;
        }

        // Minimize to Tray
        QString dbMinToTray = db.GetSettingsData_String(Constants::SettingsT_Index_MinToTray);
        QString uiMinToTray = m_mainWindow->ui->checkBox_MinToTray->isChecked() ? "1" : "0";
        if (dbMinToTray != uiMinToTray) {
            matchesDatabase = false;
        }
        if (uiMinToTray != Default_UserSettings::DEFAULT_MIN_TO_TRAY) {
            matchesDefault = false;
        }

        // Ask Password After Minimize
        QString dbAskPW = db.GetSettingsData_String(Constants::SettingsT_Index_AskPWAfterMinToTray);
        QString uiAskPW = m_mainWindow->ui->checkBox_AskPW->isChecked() ? "1" : "0";
        if (dbAskPW != uiAskPW) {
            matchesDatabase = false;
        }
        if (uiAskPW != Default_UserSettings::DEFAULT_ASK_PW_AFTER_MIN) {
            matchesDefault = false;
        }

        // Password Request Delay
        QString dbReqPWDelay = db.GetSettingsData_String(Constants::SettingsT_Index_ReqPWDelay);
        QString uiReqPWDelay = QString::number(m_mainWindow->ui->spinBox_ReqPWDelay->value());
        if (dbReqPWDelay != uiReqPWDelay) {
            matchesDatabase = false;
        }
        if (uiReqPWDelay != Default_UserSettings::DEFAULT_REQ_PW_DELAY) {
            matchesDefault = false;
        }

        // Open on Settings
        QString dbOpenOnSettings = db.GetSettingsData_String(Constants::SettingsT_Index_OpenOnSettings);
        QString uiOpenOnSettings = m_mainWindow->ui->checkBox_OpenOnSettings->isChecked() ? "1" : "0";
        if (dbOpenOnSettings != uiOpenOnSettings) {
            matchesDatabase = false;
        }
        if (uiOpenOnSettings != Default_UserSettings::DEFAULT_OPEN_ON_SETTINGS) {
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
        QString dbTextSize = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_TextSize);
        QString uiTextSize = QString::number(m_mainWindow->ui->spinBox_Diary_TextSize->value());
        if (dbTextSize != uiTextSize) {
            matchesDatabase = false;
        }
        if (uiTextSize != Default_UserSettings::DEFAULT_DIARY_TEXT_SIZE) {
            matchesDefault = false;
        }

        // Timestamp Timer
        QString dbTimer = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_TStampTimer);
        QString uiTimer = QString::number(m_mainWindow->ui->spinBox_Diary_TStampTimer->value());
        if (dbTimer != uiTimer) {
            matchesDatabase = false;
        }
        if (uiTimer != Default_UserSettings::DEFAULT_DIARY_TSTAMP_TIMER) {
            matchesDefault = false;
        }

        // Timestamp Counter
        QString dbCounter = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_TStampCounter);
        QString uiCounter = QString::number(m_mainWindow->ui->spinBox_Diary_TStampReset->value());
        if (dbCounter != uiCounter) {
            matchesDatabase = false;
        }
        if (uiCounter != Default_UserSettings::DEFAULT_DIARY_TSTAMP_COUNTER) {
            matchesDefault = false;
        }

        // Can Edit Recent
        QString dbCanEdit = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_CanEditRecent);
        QString uiCanEdit = m_mainWindow->ui->checkBox_Diary_CanEditRecent->isChecked() ? "1" : "0";
        if (dbCanEdit != uiCanEdit) {
            matchesDatabase = false;
        }
        if (uiCanEdit != Default_UserSettings::DEFAULT_DIARY_CAN_EDIT_RECENT) {
            matchesDefault = false;
        }

        // Show Task Manager Logs
        QString dbShowLogs = db.GetSettingsData_String(Constants::SettingsT_Index_Diary_ShowTManLogs);
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

        // Text Size
        QString dbTextSize = db.GetSettingsData_String(Constants::SettingsT_Index_TLists_TextSize);
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
        QString dbSortBy = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_DefSortingMethod);
        QString uiSortBy = m_mainWindow->ui->comboBox_PWMan_SortBy->currentText();
        if (dbSortBy != uiSortBy) {
            matchesDatabase = false;
        }
        if (uiSortBy != Default_UserSettings::DEFAULT_PWMAN_DEF_SORTING_METHOD) {
            matchesDefault = false;
        }

        // Require Password
        QString dbReqPW = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_ReqPassword);
        QString uiReqPW = m_mainWindow->ui->checkBox_PWMan_ReqPW->isChecked() ? "1" : "0";
        if (dbReqPW != uiReqPW) {
            matchesDatabase = false;
        }
        if (uiReqPW != Default_UserSettings::DEFAULT_PWMAN_REQ_PASSWORD) {
            matchesDefault = false;
        }

        // Hide Passwords
        QString dbHidePW = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_HidePasswords);
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
        QString dbReqPW = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_ReqPassword);
        QString uiReqPW = m_mainWindow->ui->checkBox_DataENC_ReqPW->isChecked() ? "1" : "0";
        if (dbReqPW != uiReqPW) {
            matchesDatabase = false;
        }
        if (uiReqPW != Default_UserSettings::DEFAULT_DATAENC_REQ_PASSWORD) {
            matchesDefault = false;
        }

        // Hide Image Thumbnails
        QString dbHideImageThumbnails = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_HideThumbnails_Image);
        QString uiHideImageThumbnails = m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image->isChecked() ? "1" : "0";
        if (dbHideImageThumbnails != uiHideImageThumbnails) {
            matchesDatabase = false;
        }
        if (uiHideImageThumbnails != Default_UserSettings::DEFAULT_DATAENC_HIDE_THUMBNAILS_IMAGE) {
            matchesDefault = false;
        }

        // Hide Video Thumbnails
        QString dbHideVideoThumbnails = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_HideThumbnails_Video);
        QString uiHideVideoThumbnails = m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video->isChecked() ? "1" : "0";
        if (dbHideVideoThumbnails != uiHideVideoThumbnails) {
            matchesDatabase = false;
        }
        if (uiHideVideoThumbnails != Default_UserSettings::DEFAULT_DATAENC_HIDE_THUMBNAILS_VIDEO) {
            matchesDefault = false;
        }

        // Hidden Categories
        QString dbHiddenCategories = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hidden_Categories);
        if (dbHiddenCategories != m_mainWindow->setting_DataENC_Hidden_Categories) {
            matchesDatabase = false;
        }
        if (m_mainWindow->setting_DataENC_Hidden_Categories != Default_UserSettings::DEFAULT_DATAENC_HIDDEN_CATEGORIES) {
            matchesDefault = false;
        }

        // Hidden Tags
        QString dbHiddenTags = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hidden_Tags);
        if (dbHiddenTags != m_mainWindow->setting_DataENC_Hidden_Tags) {
            matchesDatabase = false;
        }
        if (m_mainWindow->setting_DataENC_Hidden_Tags != Default_UserSettings::DEFAULT_DATAENC_HIDDEN_TAGS) {
            matchesDefault = false;
        }

        // Hide Categories
        QString dbHideCategories = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hide_Categories);
        QString uiHideCategories = m_mainWindow->ui->checkBox_DataENC_HideCategories->isChecked() ? "1" : "0";
        if (dbHideCategories != uiHideCategories) {
            matchesDatabase = false;
        }
        if (uiHideCategories != Default_UserSettings::DEFAULT_DATAENC_HIDE_CATEGORIES) {
            matchesDefault = false;
        }

        // Hide Tags
        QString dbHideTags = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hide_Tags);
        QString uiHideTags = m_mainWindow->ui->checkBox_DataENC_HideTags->isChecked() ? "1" : "0";
        if (dbHideTags != uiHideTags) {
            matchesDatabase = false;
        }
        if (uiHideTags != Default_UserSettings::DEFAULT_DATAENC_HIDE_TAGS) {
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
    // SECURITY: Check if MainWindow still exists
    if (!m_mainWindow) {
        qWarning() << "Operations_Settings: InitializeCustomCheckboxes called with null MainWindow";
        return;
    }
    
    QString username = m_mainWindow->user_Username;
    QByteArray encryptionKey = m_mainWindow->user_Key;

    // SECURITY: Get custom checkboxes with null checks
    qcheckbox_PWValidation* hidePasswordsCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_PWMan_HidePWS);

    qcheckbox_PWValidation* reqPasswordCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_PWMan_ReqPW);

    qcheckbox_PWValidation* askPWOnCloseCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_AskPW);

    qcheckbox_PWValidation* dataEncReqPWCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_DataENC_ReqPW);

    // Get the new thumbnail hiding checkboxes
    qcheckbox_PWValidation* hideThumbnailsImageCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image);

    qcheckbox_PWValidation* hideThumbnailsVideoCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video);

    // Get the new hide categories/tags checkboxes
    qcheckbox_PWValidation* hideCategoriesCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_DataENC_HideCategories);

    qcheckbox_PWValidation* hideTagsCheckbox =
        qobject_cast<qcheckbox_PWValidation*>(m_mainWindow->ui->checkBox_DataENC_HideTags);

    // Set validation info for Hide Passwords checkbox
    if (hidePasswordsCheckbox) {
        hidePasswordsCheckbox->setValidationInfo(
            "Disable 'Hide Passwords' in Password Manager", username);
        hidePasswordsCheckbox->setRequireValidation(true);
        hidePasswordsCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        // SECURITY: Fetch fresh values in lambda to avoid storing copies of sensitive data
        hidePasswordsCheckbox->setDatabaseValueGetter([this]() {
            if (!m_mainWindow) return false;
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(m_mainWindow->user_Username, m_mainWindow->user_Key)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_HidePasswords);
            return (dbValue == "1");
        });
        hidePasswordsCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    } else {
        qDebug() << "Operations_Settings: hidePasswordsCheckbox cast failed or widget not found";
    }

    // Set validation info for Require Password checkbox
    if (reqPasswordCheckbox) {
        reqPasswordCheckbox->setValidationInfo(
            "Disable 'Require Password' in Password Manager", username);
        reqPasswordCheckbox->setRequireValidation(true);
        reqPasswordCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        // SECURITY: Fetch fresh values in lambda to avoid storing copies of sensitive data
        reqPasswordCheckbox->setDatabaseValueGetter([this]() {
            if (!m_mainWindow) return false;
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(m_mainWindow->user_Username, m_mainWindow->user_Key)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_PWMan_ReqPassword);
            return (dbValue == "1");
        });
        reqPasswordCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    } else {
        qDebug() << "Operations_Settings: reqPasswordCheckbox cast failed or widget not found";
    }

    // Set validation info for Ask Password on Close checkbox
    if (askPWOnCloseCheckbox) {
        askPWOnCloseCheckbox->setValidationInfo(
            "Disable 'Ask Password on Close' in Account Settings", username);
        askPWOnCloseCheckbox->setRequireValidation(true);
        askPWOnCloseCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        askPWOnCloseCheckbox->setDatabaseValueGetter([username, encryptionKey]() {
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(username, encryptionKey)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_AskPWAfterMinToTray);
            return (dbValue == "1");
        });
        askPWOnCloseCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    }

    // Set validation info for Encrypted Data Require Password checkbox
    if (dataEncReqPWCheckbox) {
        dataEncReqPWCheckbox->setValidationInfo(
            "Disable 'Require Password' in Encrypted Data Settings", username);
        dataEncReqPWCheckbox->setRequireValidation(true);
        dataEncReqPWCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        dataEncReqPWCheckbox->setDatabaseValueGetter([username, encryptionKey]() {
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(username, encryptionKey)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_ReqPassword);
            return (dbValue == "1");
        });
        dataEncReqPWCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    }

    // Configure Hide Image Thumbnails checkbox
    if (hideThumbnailsImageCheckbox) {
        hideThumbnailsImageCheckbox->setValidationInfo(
            "Disable 'Hide Image Thumbnails' in Encrypted Data Settings", username);
        hideThumbnailsImageCheckbox->setRequireValidation(true);
        hideThumbnailsImageCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        hideThumbnailsImageCheckbox->setDatabaseValueGetter([username, encryptionKey]() {
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(username, encryptionKey)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_HideThumbnails_Image);
            return (dbValue == "1");
        });
        hideThumbnailsImageCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    }

    // Configure Hide Video Thumbnails checkbox
    if (hideThumbnailsVideoCheckbox) {
        hideThumbnailsVideoCheckbox->setValidationInfo(
            "Disable 'Hide Video Thumbnails' in Encrypted Data Settings", username);
        hideThumbnailsVideoCheckbox->setRequireValidation(true);
        hideThumbnailsVideoCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        hideThumbnailsVideoCheckbox->setDatabaseValueGetter([username, encryptionKey]() {
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(username, encryptionKey)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_HideThumbnails_Video);
            return (dbValue == "1");
        });
        hideThumbnailsVideoCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    }

    // Configure Hide Categories checkbox
    if (hideCategoriesCheckbox) {
        hideCategoriesCheckbox->setValidationInfo(
            "Disable 'Hide Categories' in Encrypted Data Settings", username);
        hideCategoriesCheckbox->setRequireValidation(true);
        hideCategoriesCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        hideCategoriesCheckbox->setDatabaseValueGetter([username, encryptionKey]() {
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(username, encryptionKey)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hide_Categories);
            return (dbValue == "1");
        });
        hideCategoriesCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
        });
    }

    // Configure Hide Tags checkbox
    if (hideTagsCheckbox) {
        hideTagsCheckbox->setValidationInfo(
            "Disable 'Hide Tags' in Encrypted Data Settings", username);
        hideTagsCheckbox->setRequireValidation(true);
        hideTagsCheckbox->setValidationMode(qcheckbox_PWValidation::ValidateOnUncheck);
        hideTagsCheckbox->setDatabaseValueGetter([username, encryptionKey]() {
            DatabaseSettingsManager& db = DatabaseSettingsManager::instance();
            if (!db.connect(username, encryptionKey)) {
                return false;
            }
            QString dbValue = db.GetSettingsData_String(Constants::SettingsT_Index_DataENC_Hide_Tags);
            return (dbValue == "1");
        });
        hideTagsCheckbox->setGracePeriodGetter([this]() {
            // SECURITY: Check if MainWindow still exists
            if (!m_mainWindow) return 30; // Default value if main window is gone
            return m_mainWindow->setting_ReqPWDelay;
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

        // Check if Hide Image Thumbnails would change
        QString defaultHideImageThumbs = Default_UserSettings::DEFAULT_DATAENC_HIDE_THUMBNAILS_IMAGE;
        bool currentHideImageThumbs = m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image->isChecked();
        bool defaultHideImageThumbsValue = (defaultHideImageThumbs == "1");

        // Check if Hide Video Thumbnails would change
        QString defaultHideVideoThumbs = Default_UserSettings::DEFAULT_DATAENC_HIDE_THUMBNAILS_VIDEO;
        bool currentHideVideoThumbs = m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video->isChecked();
        bool defaultHideVideoThumbsValue = (defaultHideVideoThumbs == "1");

        // Check if Hide Categories would change
        QString defaultHideCategories = Default_UserSettings::DEFAULT_DATAENC_HIDE_CATEGORIES;
        bool currentHideCategories = m_mainWindow->ui->checkBox_DataENC_HideCategories->isChecked();
        bool defaultHideCategoriesValue = (defaultHideCategories == "1");

        // Check if Hide Tags would change
        QString defaultHideTags = Default_UserSettings::DEFAULT_DATAENC_HIDE_TAGS;
        bool currentHideTags = m_mainWindow->ui->checkBox_DataENC_HideTags->isChecked();
        bool defaultHideTagsValue = (defaultHideTags == "1");

        // Check if Hidden Categories would change
        bool categoriesWouldChange = (m_mainWindow->setting_DataENC_Hidden_Categories != Default_UserSettings::DEFAULT_DATAENC_HIDDEN_CATEGORIES);

        // Check if Hidden Tags would change
        bool tagsWouldChange = (m_mainWindow->setting_DataENC_Hidden_Tags != Default_UserSettings::DEFAULT_DATAENC_HIDDEN_TAGS);

        if (currentReqPW != defaultReqPWValue ||
            currentHideImageThumbs != defaultHideImageThumbsValue ||
            currentHideVideoThumbs != defaultHideVideoThumbsValue ||
            currentHideCategories != defaultHideCategoriesValue ||
            currentHideTags != defaultHideTagsValue ||
            categoriesWouldChange || tagsWouldChange) {
            // These settings would change, so validate password
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

    m_settingNames[m_mainWindow->ui->spinBox_ReqPWDelay] = "Password Request Delay";
    m_settingDescriptions[m_mainWindow->ui->spinBox_ReqPWDelay] = "Duration in seconds before you can be asked to validate your password again after a successful validation.\n\n0 = Always ask for password\n30 = Wait 30 seconds (recommended)\n300 = Wait 5 minutes (maximum)\n\nThis prevents repetitive password prompts while maintaining security.";

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
    m_settingNames[m_mainWindow->ui->spinBox_TList_TextSize] = "Task list Text Size";
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

    m_settingNames[m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image] = "Hide Image Thumbnails";
    m_settingDescriptions[m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Image] = "Hide thumbnails for image files in the encrypted data view.\n\nThis can improve privacy and performance when dealing with many image files.";

    m_settingNames[m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video] = "Hide Video Thumbnails";
    m_settingDescriptions[m_mainWindow->ui->checkBox_DataENC_HideThumbnails_Video] = "Hide thumbnails for video files in the encrypted data view.\n\nThis can improve privacy and performance when dealing with many video files.";

    m_settingNames[m_mainWindow->ui->pushButton_DataENC_Hidden_Categories] = "Hidden Categories";
    m_settingDescriptions[m_mainWindow->ui->pushButton_DataENC_Hidden_Categories] = "Manage a list of categories to hide from the encrypted data view.\n\nFiles in these categories will not be displayed in the file list.";

    m_settingNames[m_mainWindow->ui->pushButton_DataENC_Hidden_Tags] = "Hidden Tags";
    m_settingDescriptions[m_mainWindow->ui->pushButton_DataENC_Hidden_Tags] = "Manage a list of tags to hide from the encrypted data view.\n\nFiles with these tags will not be displayed in the file list.";

    m_settingNames[m_mainWindow->ui->checkBox_DataENC_HideCategories] = "Hide Categories";
    m_settingDescriptions[m_mainWindow->ui->checkBox_DataENC_HideCategories] = "Hide files from categories that are in the hidden categories list.\n\nFiles in hidden categories will not be displayed in the file list.\n\nYou can manage the list of hidden categories using the 'Hidden Categories' button.";

    m_settingNames[m_mainWindow->ui->checkBox_DataENC_HideTags] = "Hide Tags";
    m_settingDescriptions[m_mainWindow->ui->checkBox_DataENC_HideTags] = "Hide files with tags that are in the hidden tags list.\n\nFiles with hidden tags will not be displayed in the file list.\n\nYou can manage the list of hidden tags using the 'Hidden Tags' button.";

    m_settingNames[m_mainWindow->ui->checkBox_OpenOnSettings] = "Open on Settings Tab";
    m_settingDescriptions[m_mainWindow->ui->checkBox_OpenOnSettings] = "When enabled, the application will always open on the Settings tab.\n\nThis applies both when launching the app and when showing it from the system tray.\n\nUseful if you frequently access settings or want quick access to configuration options.";

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
    // SECURITY: Check if the watched object is valid
    if (!watched || !event) {
        return false;
    }
    
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
    // SECURITY: Check if MainWindow and control are valid
    if (!m_mainWindow || !control) {
        return;
    }
    
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
    // Get the username and encryption key from MainWindow
    QString username = m_mainWindow->user_Username;
    QByteArray encryptionKey = m_mainWindow->user_Key;

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
            if (Default_UserSettings::SetDefault_GlobalSettings(username, encryptionKey)) {
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
        if (Default_UserSettings::SetDefault_DiarySettings(username, encryptionKey)) {
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
        if (Default_UserSettings::SetDefault_TasklistsSettings(username, encryptionKey)) {
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
            if (Default_UserSettings::SetDefault_PWManagerSettings(username, encryptionKey)) {
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
            if (Default_UserSettings::SetDefault_EncryptedDataSettings(username, encryptionKey)) {
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


//--------------- Encrypted Data Hidden Items Dialog ---------------//

// Slot implementations
void Operations_Settings::onHiddenCategoriesClicked()
{
    QString username = m_mainWindow->user_Username;
    int gracePeriod = m_mainWindow->setting_ReqPWDelay; // Get grace period from settings

    // Validate password before opening the dialog (category names could be sensitive)
    if (!PasswordValidation::validatePasswordForOperation(
            m_mainWindow, "Access Hidden Categories Settings", username, gracePeriod)) {
        return; // Password validation failed
    }

    showHiddenItemsDialog("Category", m_mainWindow->setting_DataENC_Hidden_Categories);
}

void Operations_Settings::onHiddenTagsClicked()
{
    QString username = m_mainWindow->user_Username;
    int gracePeriod = m_mainWindow->setting_ReqPWDelay; // Get grace period from settings

    // Validate password before opening the dialog (tag names could be sensitive)
    if (!PasswordValidation::validatePasswordForOperation(
            m_mainWindow, "Access Hidden Tags Settings", username, gracePeriod)) {
        return; // Password validation failed
    }

    showHiddenItemsDialog("Tag", m_mainWindow->setting_DataENC_Hidden_Tags);
}

// Main dialog implementation
void Operations_Settings::showHiddenItemsDialog(const QString& itemType, QString& settingValue)
{
    // Create dialog
    QDialog dialog(m_mainWindow);
    Ui::HiddenItemsList ui;
    ui.setupUi(&dialog);

    // Set window title and labels based on item type
    QString plural = itemType + "s";
    dialog.setWindowTitle(QString("Hidden %1").arg(plural));
    ui.label_HideItems->setText(QString("Manage Hidden %1:").arg(plural));
    ui.pushButton_AddItem->setText(QString("Add %1").arg(itemType));
    ui.pushButton_RemoveItem->setText(QString("Remove %1").arg(itemType));

    // Hide the checkbox since hide functionality is now on the main settings page
    ui.checkBox_HideItems->setVisible(false);

    // Load existing items
    QStringList currentItems = parseItemList(settingValue);
    // SECURITY: Limit to MAX_HIDDEN_ITEMS to prevent memory exhaustion
    if (currentItems.size() > MAX_HIDDEN_ITEMS) {
        currentItems = currentItems.mid(0, MAX_HIDDEN_ITEMS);
        QMessageBox::warning(&dialog, "Item Limit", 
            QString("Only the first %1 items were loaded. Maximum allowed is %1.").arg(MAX_HIDDEN_ITEMS));
    }
    for (const QString& item : currentItems) {
        ui.listWidget_ItemsList->addItem(item);
    }

    // Connect double-click signal for editing
    connect(ui.listWidget_ItemsList, &QListWidget::itemDoubleClicked, [&](QListWidgetItem* item) {
        if (!item) return;

        QString currentText = item->text();
        QString newText = QInputDialog::getText(&dialog,
                                                QString("Edit %1").arg(itemType),
                                                QString("Edit %1 name:").arg(itemType.toLower()),
                                                QLineEdit::Normal,
                                                currentText);

        if (!newText.isEmpty() && newText != currentText) {
            // Validate input
            InputValidation::ValidationResult result = InputValidation::validateInput(
                newText, InputValidation::InputType::CategoryTag, 50);

            if (result.isValid) {
                // Check for duplicates (excluding the current item)
                bool duplicate = false;
                for (int i = 0; i < ui.listWidget_ItemsList->count(); ++i) {
                    QListWidgetItem* listItem = ui.listWidget_ItemsList->item(i);
                    if (listItem != item && listItem->text() == newText) {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate) {
                    // SECURITY: Final validation before setting
                    item->setText(newText);
                } else {
                    QMessageBox::warning(&dialog, "Duplicate Entry",
                                         QString("This %1 already exists in the list.").arg(itemType.toLower()));
                }
            } else {
                QMessageBox::warning(&dialog, "Invalid Input", result.errorMessage);
            }
        }
    });

    // Connect dialog signals
    connect(ui.pushButton_AddItem, &QPushButton::clicked, [&]() {
        QString newItem = QInputDialog::getText(&dialog,
                                                QString("Add %1").arg(itemType),
                                                QString("Enter %1 name:").arg(itemType.toLower()));

        if (!newItem.isEmpty()) {
            // Validate input
            InputValidation::ValidationResult result = InputValidation::validateInput(
                newItem, InputValidation::InputType::CategoryTag, 50);

            if (result.isValid) {
                // Check for duplicates
                bool duplicate = false;
                for (int i = 0; i < ui.listWidget_ItemsList->count(); ++i) {
                    if (ui.listWidget_ItemsList->item(i)->text() == newItem) {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate) {
                    // SECURITY: Check item count limit
                    if (ui.listWidget_ItemsList->count() >= MAX_HIDDEN_ITEMS) {
                        QMessageBox::warning(&dialog, "Item Limit Reached",
                            QString("Cannot add more items. Maximum allowed is %1.").arg(MAX_HIDDEN_ITEMS));
                    } else {
                        ui.listWidget_ItemsList->addItem(newItem);
                    }
                } else {
                    QMessageBox::warning(&dialog, "Duplicate Entry",
                                         QString("This %1 already exists in the list.").arg(itemType.toLower()));
                }
            } else {
                QMessageBox::warning(&dialog, "Invalid Input", result.errorMessage);
            }
        }
    });

    connect(ui.pushButton_RemoveItem, &QPushButton::clicked, [&]() {
        QListWidgetItem* currentItem = ui.listWidget_ItemsList->currentItem();
        if (currentItem) {
            delete currentItem;
        } else {
            QMessageBox::information(&dialog, "No Selection",
                                     QString("Please select a %1 to remove.").arg(itemType.toLower()));
        }
    });

    connect(ui.pushButton_ClearList, &QPushButton::clicked, [&]() {
        if (ui.listWidget_ItemsList->count() > 0) {
            int ret = QMessageBox::question(&dialog, "Clear List",
                                            QString("Are you sure you want to clear all %1?").arg(plural.toLower()),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

            if (ret == QMessageBox::Yes) {
                ui.listWidget_ItemsList->clear();
            }
        }
    });

    connect(ui.pushButton_Cancel, &QPushButton::clicked, [&]() {
        dialog.reject();
    });

    connect(ui.pushButton_SaveChanges, &QPushButton::clicked, [&]() {
        dialog.accept();
    });

    // Execute dialog
    if (dialog.exec() == QDialog::Accepted) {
        // Collect items from list
        QStringList newItems;
        for (int i = 0; i < ui.listWidget_ItemsList->count(); ++i) {
            newItems.append(ui.listWidget_ItemsList->item(i)->text());
        }

        // Update the setting value
        settingValue = formatItemList(newItems);

        // Update button states to reflect changes
        UpdateButtonStates(Constants::DBSettings_Type_EncryptedData);

        qDebug() << QString("Updated hidden %1: %2").arg(plural.toLower(), settingValue);
    }
}

// Helper method implementations
QStringList Operations_Settings::parseItemList(const QString& itemString)
{
    if (itemString.isEmpty()) {
        return QStringList();
    }

    QStringList items = itemString.split(';', Qt::SkipEmptyParts);

    // Trim whitespace from each item
    for (QString& item : items) {
        item = item.trimmed();
    }

    // Remove empty items
    items.removeAll("");

    return items;
}

QString Operations_Settings::formatItemList(const QStringList& items)
{
    if (items.isEmpty()) {
        return QString();
    }

    // Filter out empty items and trim whitespace
    QStringList filteredItems;
    for (const QString& item : items) {
        QString trimmed = item.trimmed();
        if (!trimmed.isEmpty()) {
            filteredItems.append(trimmed);
        }
    }

    return filteredItems.join(';');
}
