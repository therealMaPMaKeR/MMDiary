#include "vp_shows_add_dialog.h"
#include "ui_vp_shows_add_dialog.h"
#include "inputvalidation.h"
#include "vp_shows_config.h"
#include "operations_files.h"
#include "vp_shows_metadata.h"
#include "vp_shows_settings.h"
#include "CryptoUtils.h"
#include "mainwindow.h"
#include <QMessageBox>
#include <QDebug>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QDateTime>
#include <QRandomGenerator>
#include <QApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QImageReader>
#include <QUuid>

VP_ShowsAddDialog::VP_ShowsAddDialog(const QString& folderName, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsAddDialog)
    , m_folderName(folderName)
    , m_parentWidget(parent)
    , m_isAddingToExistingShow(false)
    , m_isCheckingExistingShow(false)
    , m_lastCheckedShowName("")
    , m_hasTMDBData(false)
    , m_settingsLoaded(false)
    , m_existingAutoplay(true)
    , m_existingSkipIntro(false)
    , m_existingSkipOutro(false)
    , m_existingUseTMDB(true)
    , m_suggestionsList(nullptr)
    , m_searchTimer(nullptr)
    , m_existingShowCheckTimer(nullptr)
    , m_currentCacheSize(0)  // Track if we have TMDB data loaded
    , m_isShowingSuggestions(false)
    , m_hoveredItemIndex(-1)  // Default to true
    , m_itemJustSelected(false)  // Default to false
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsAddDialog: Initializing dialog with folder name:" << folderName;
    
    // Set window title
    setWindowTitle(tr("Add TV Show"));
    
    // Pre-fill the show name with the folder name
    ui->lineEdit_ShowName->setText(folderName);
    
    // Pre-fill the language with English
    ui->lineEdit_Language->setText("English");
    
    // Set up the translation mode combo box
    ui->comboBox_TranslationMode->clear();
    ui->comboBox_TranslationMode->addItem("Dubbed");
    ui->comboBox_TranslationMode->addItem("Subbed");
    ui->comboBox_TranslationMode->setCurrentIndex(0); // Default to Dubbed
    
    // Store original (empty) poster and description
    m_originalDescription = "No description available.";
    m_originalPoster = QPixmap();
    m_hasTMDBData = false;  // Initialize the flag
    ui->textBrowser_ShowDescription->clear();  // Clear any default HTML
    ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
    

    
    // Connect signals
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &VP_ShowsAddDialog::on_buttonBox_accepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &VP_ShowsAddDialog::on_buttonBox_rejected);
    
    // Connect the UseTMDB checkbox
    connect(ui->checkBox_UseTMDB, &QCheckBox::toggled,
            this, &VP_ShowsAddDialog::onUseTMDBCheckboxToggled);
    
    // Apply initial button states based on checkbox state
    onUseTMDBCheckboxToggled(ui->checkBox_UseTMDB->isChecked());
    
    // Connect the custom poster and description buttons
    connect(ui->pushButton_UseCustomPoster, &QPushButton::clicked,
            this, &VP_ShowsAddDialog::onUseCustomPosterClicked);
    connect(ui->pushButton_UseCustomDescription, &QPushButton::clicked,
            this, &VP_ShowsAddDialog::onUseCustomDescriptionClicked);
    
    // Initialize custom data flags EARLY before any checks
    m_hasCustomDescription = false;
    m_customPoster = QPixmap();  // Explicitly initialize as null
    m_customDescription = QString();  // Explicitly initialize as empty
    m_userSelectedFromDropdown = false;  // Initialize user selection flag
    
    // Initialize TMDB autofill functionality
    setupAutofillUI();
    
    // Initialize existing show check timer for debouncing
    m_existingShowCheckTimer = new QTimer(this);
    m_existingShowCheckTimer->setSingleShot(true);
    m_existingShowCheckTimer->setInterval(500);  // 500ms delay for existing show check
    connect(m_existingShowCheckTimer, &QTimer::timeout, this, &VP_ShowsAddDialog::onExistingShowCheckTimeout);
    qDebug() << "VP_ShowsAddDialog: Existing show check timer initialized with 500ms delay";
    
    // After setting the folder name, check if we have an existing show with that name
    // This ensures we load the poster/description if the show exists,
    // or trigger TMDB search if it doesn't
    if (!folderName.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Checking for existing show with folder name:" << folderName;
        
        // Check if a show with this name already exists
        checkForExistingShow(folderName);
        
        // Only trigger TMDB search if the show doesn't exist in our library
        // If we found an existing show, it will have loaded the poster/description
        bool existingShowFound = !m_originalPoster.isNull() || m_originalDescription != "No description available.";
        
        if (!existingShowFound && ui->checkBox_UseTMDB->isChecked() && m_tmdbApi) {
            qDebug() << "VP_ShowsAddDialog: No existing show found, triggering TMDB search for folder name:" << folderName;
            
            // Store the current search text
            m_currentSearchText = folderName;
            
            // Start the search timer to trigger TMDB search
            if (m_searchTimer) {
                m_searchTimer->stop();
                m_searchTimer->start();
            }
            
            // Set the TMDB data flag since we're about to show TMDB results
            m_hasTMDBData = true;
        } else if (existingShowFound) {
            qDebug() << "VP_ShowsAddDialog: Existing show found, skipping TMDB search";
        }
    }
    
    qDebug() << "VP_ShowsAddDialog: Dialog initialized successfully";
}

VP_ShowsAddDialog::~VP_ShowsAddDialog()
{
    qDebug() << "VP_ShowsAddDialog: Destructor called";
    
    // Clean up suggestions list if it exists
    if (m_suggestionsList) {
        m_suggestionsList->deleteLater();
    }
    
    // Clean up timer
    if (m_searchTimer) {
        m_searchTimer->stop();
        delete m_searchTimer;
    }
    
    // Clean up existing show check timer
    if (m_existingShowCheckTimer) {
        m_existingShowCheckTimer->stop();
        delete m_existingShowCheckTimer;
    }
    
    delete ui;
}

void VP_ShowsAddDialog::setDefaultSettings(bool useTMDB, int fileFolderParsing)
{
    qDebug() << "VP_ShowsAddDialog: Setting default settings - UseTMDB:" << useTMDB
             << "FileFolderParsing:" << fileFolderParsing;

    // Set TMDB checkbox
    ui->checkBox_UseTMDB->setChecked(useTMDB);

    // Set parsing mode radio buttons
    // 0 = Folder Name, 1 = File Name
    if (fileFolderParsing == 0) {
        ui->radioButton_FolderName->setChecked(true);
        ui->radioButton_FileName->setChecked(false);
    } else {
        ui->radioButton_FolderName->setChecked(false);
        ui->radioButton_FileName->setChecked(true);
    }
}

QString VP_ShowsAddDialog::getShowName() const
{
    return ui->lineEdit_ShowName->text().trimmed();
}

QString VP_ShowsAddDialog::getLanguage() const
{
    return ui->lineEdit_Language->text().trimmed();
}

QString VP_ShowsAddDialog::getTranslationMode() const
{
    return ui->comboBox_TranslationMode->currentText();
}

bool VP_ShowsAddDialog::isUsingTMDB() const
{
    bool usingTMDB = ui->checkBox_UseTMDB->isChecked();
    qDebug() << "VP_ShowsAddDialog::isUsingTMDB() returning:" << usingTMDB;
    return usingTMDB;
}


VP_ShowsAddDialog::ParseMode VP_ShowsAddDialog::getParseMode() const
{
    if (ui->radioButton_FolderName->isChecked()) {
        qDebug() << "VP_ShowsAddDialog::getParseMode() returning: ParseFromFolder";
        return ParseFromFolder;
    } else {
        qDebug() << "VP_ShowsAddDialog::getParseMode() returning: ParseFromFile";
        return ParseFromFile;
    }
}

QPixmap VP_ShowsAddDialog::getCustomPoster() const
{
    qDebug() << "VP_ShowsAddDialog::getCustomPoster() - poster null:" << m_customPoster.isNull()
             << "size:" << m_customPoster.size();
    return m_customPoster;
}

QString VP_ShowsAddDialog::getCustomDescription() const
{
    qDebug() << "VP_ShowsAddDialog::getCustomDescription() - description length:" << m_customDescription.length()
             << "preview:" << m_customDescription.left(50);
    return m_customDescription;
}

bool VP_ShowsAddDialog::hasCustomPoster() const
{
    bool hasPoster = !m_customPoster.isNull();
    qDebug() << "VP_ShowsAddDialog::hasCustomPoster() returning:" << hasPoster
             << "(poster null:" << m_customPoster.isNull() << "size:" << m_customPoster.size() << ")";
    return hasPoster;
}

bool VP_ShowsAddDialog::hasCustomDescription() const
{
    qDebug() << "VP_ShowsAddDialog::hasCustomDescription() returning:" << m_hasCustomDescription
             << "(description length:" << m_customDescription.length() << ")";
    return m_hasCustomDescription;
}

bool VP_ShowsAddDialog::validateInputs()
{
    qDebug() << "VP_ShowsAddDialog: Validating inputs";
    
    // Validate show name
    QString showName = getShowName();
    if (!validateShowName(showName)) {
        return false;
    }
    
    // Validate language
    QString language = getLanguage();
    if (!validateLanguage(language)) {
        return false;
    }
    
    // Translation mode is from combo box, so it's always valid
    QString translationMode = getTranslationMode();
    if (translationMode.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Please select a translation mode."));
        return false;
    }
    
    qDebug() << "VP_ShowsAddDialog: All inputs valid - Show:" << showName 
             << "Language:" << language << "Translation:" << translationMode;
    
    return true;
}

bool VP_ShowsAddDialog::validateShowName(const QString& showName)
{
    if (showName.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Show name cannot be empty."));
        ui->lineEdit_ShowName->setFocus();
        return false;
    }
    
    // Use InputValidation for proper validation - TVShowName allows special characters
    InputValidation::ValidationResult result = InputValidation::validateInput(
        showName, InputValidation::InputType::TVShowName, 200);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Invalid show name: %1").arg(result.errorMessage));
        ui->lineEdit_ShowName->setFocus();
        ui->lineEdit_ShowName->selectAll();
        return false;
    }
    
    qDebug() << "VP_ShowsAddDialog: Show name validated:" << showName;
    return true;
}

bool VP_ShowsAddDialog::validateLanguage(const QString& language)
{
    if (language.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Language cannot be empty."));
        ui->lineEdit_Language->setFocus();
        return false;
    }
    
    // Use InputValidation for proper validation
    InputValidation::ValidationResult result = InputValidation::validateInput(
        language, InputValidation::InputType::PlainText, 50);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Invalid language: %1").arg(result.errorMessage));
        ui->lineEdit_Language->setFocus();
        ui->lineEdit_Language->selectAll();
        return false;
    }
    
    // Additional check: Language should only contain letters, spaces, and hyphens
    QRegularExpression languageRegex("^[a-zA-Z\\s\\-]+$");
    if (!languageRegex.match(language).hasMatch()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Language can only contain letters, spaces, and hyphens."));
        ui->lineEdit_Language->setFocus();
        ui->lineEdit_Language->selectAll();
        return false;
    }
    
    qDebug() << "VP_ShowsAddDialog: Language validated:" << language;
    return true;
}

void VP_ShowsAddDialog::on_buttonBox_accepted()
{
    qDebug() << "VP_ShowsAddDialog: OK button clicked";
    qDebug() << "VP_ShowsAddDialog: Current selected show ID:" << m_selectedShowId
             << "User selected from dropdown:" << m_userSelectedFromDropdown;
    
    if (validateInputs()) {
        qDebug() << "VP_ShowsAddDialog: Inputs validated, accepting dialog with show ID:" << m_selectedShowId;
        accept();
    } else {
        qDebug() << "VP_ShowsAddDialog: Input validation failed";
        // Don't accept the dialog if validation fails
    }
}

void VP_ShowsAddDialog::on_buttonBox_rejected()
{
    qDebug() << "VP_ShowsAddDialog: Cancel button clicked";
    reject();
}

void VP_ShowsAddDialog::setShowNameReadOnly(bool readOnly)
{
    qDebug() << "VP_ShowsAddDialog: Setting show name read-only:" << readOnly;
    ui->lineEdit_ShowName->setReadOnly(readOnly);
    
    // If read-only, also change the style to indicate it's disabled
    if (readOnly) {
        ui->lineEdit_ShowName->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
        m_isAddingToExistingShow = true;
        
        // Update the checkbox text for adding episodes to existing show
        ui->checkBox_UseTMDB->setText("Use TMDB for episode information");
        
        // Disable custom poster and description buttons for existing shows
        ui->pushButton_UseCustomPoster->setEnabled(false);
        ui->pushButton_UseCustomDescription->setEnabled(false);
        qDebug() << "VP_ShowsAddDialog: Disabled custom buttons for existing show";
    } else {
        ui->lineEdit_ShowName->setStyleSheet("");  // Reset to default style
        m_isAddingToExistingShow = false;
        
        // Reset to default text for new show
        ui->checkBox_UseTMDB->setText("Use TMDB for show information");
        
        // Re-enable custom buttons based on TMDB checkbox state
        bool tmdbChecked = ui->checkBox_UseTMDB->isChecked();
        ui->pushButton_UseCustomPoster->setEnabled(!tmdbChecked);
        ui->pushButton_UseCustomDescription->setEnabled(!tmdbChecked);
    }
}

void VP_ShowsAddDialog::initializeForExistingShow(const QString& showPath, const QByteArray& encryptionKey, const QString& username)
{
    qDebug() << "VP_ShowsAddDialog: Initializing for existing show at path:" << showPath;
    
    if (showPath.isEmpty() || encryptionKey.isEmpty() || username.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Invalid parameters for existing show initialization";
        return;
    }
    
    // Load the existing show's poster and description
    loadExistingShowData(showPath, encryptionKey, username);
    
    // Load the existing show's settings
    loadShowSettings(showPath, encryptionKey, username);
}

void VP_ShowsAddDialog::loadExistingShowData(const QString& showPath, const QByteArray& encryptionKey, const QString& username)
{
    qDebug() << "VP_ShowsAddDialog: Loading existing show data from:" << showPath;
    
    // Get the obfuscated folder name
    QDir showDir(showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Load show description from showdesc_[obfuscated] file
    QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descFilePath = showDir.absoluteFilePath(descFileName);
    
    if (QFile::exists(descFilePath)) {
        QString description;
        if (OperationsFiles::readEncryptedFile(descFilePath, encryptionKey, description)) {
            m_originalDescription = description;
            ui->textBrowser_ShowDescription->clear();
            ui->textBrowser_ShowDescription->setPlainText(description);
            qDebug() << "VP_ShowsAddDialog: Loaded show description";
        } else {
            qDebug() << "VP_ShowsAddDialog: Failed to decrypt show description";
        }
    } else {
        qDebug() << "VP_ShowsAddDialog: No description file found";
    }
    
    // Load show image from showimage_[obfuscated] file
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    if (QFile::exists(imageFilePath)) {
        QFile imageFile(imageFilePath);
        if (imageFile.open(QIODevice::ReadOnly)) {
            QByteArray encryptedData = imageFile.readAll();
            imageFile.close();
            
            // Decrypt the image data
            QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(encryptionKey, encryptedData);
            
            if (!decryptedData.isEmpty()) {
                QPixmap poster;
                if (poster.loadFromData(decryptedData)) {
                    m_originalPoster = poster;
                    ui->label_ShowPoster->setPixmap(poster.scaled(200, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    qDebug() << "VP_ShowsAddDialog: Loaded show poster";
                } else {
                    qDebug() << "VP_ShowsAddDialog: Failed to load poster from decrypted data";
                }
            } else {
                qDebug() << "VP_ShowsAddDialog: Failed to decrypt image data";
            }
        } else {
            qDebug() << "VP_ShowsAddDialog: Failed to open image file";
        }
    } else {
        qDebug() << "VP_ShowsAddDialog: No image file found";
    }
    
    qDebug() << "VP_ShowsAddDialog: Finished loading existing show data";
}

void VP_ShowsAddDialog::loadShowSettings(const QString& showPath, const QByteArray& encryptionKey, const QString& username)
{
    qDebug() << "VP_ShowsAddDialog: Loading show settings from:" << showPath;
    
    // Check parameters
    if (showPath.isEmpty() || encryptionKey.isEmpty() || username.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Invalid parameters for loading settings";
        return;
    }
    
    // Create settings manager
    VP_ShowsSettings settingsManager(encryptionKey, username);
    
    // Load settings for this show
    VP_ShowsSettings::ShowSettings settings;
    if (settingsManager.loadShowSettings(showPath, settings)) {
        qDebug() << "VP_ShowsAddDialog: Successfully loaded show settings";
        qDebug() << "VP_ShowsAddDialog: Autoplay:" << settings.autoplay << "SkipIntro:" << settings.skipIntro << "SkipOutro:" << settings.skipOutro << "UseTMDB:" << settings.useTMDB;
        
        // Update checkboxes with loaded settings
        ui->checkBox_UseTMDB->setChecked(settings.useTMDB);
        
        // Store that we loaded settings
        m_settingsLoaded = true;
        m_existingAutoplay = settings.autoplay;
        m_existingSkipIntro = settings.skipIntro;
        m_existingSkipOutro = settings.skipOutro;
        m_existingUseTMDB = settings.useTMDB;
    } else {
        qDebug() << "VP_ShowsAddDialog: No settings file found or failed to load, will use defaults from MainWindow";
        // Note: MainWindow will pass its settings via setDefaultSettings() after construction
        m_settingsLoaded = false;
    }
    
    // Note: Parsing mode (Folder vs File) is not stored per-show, so we keep the MainWindow setting
}

void VP_ShowsAddDialog::setupAutofillUI()
{
    qDebug() << "VP_ShowsAddDialog: Setting up autofill UI";
    
    // Check if TMDB is enabled
    if (!VP_ShowsConfig::isTMDBEnabled()) {
        qDebug() << "VP_ShowsAddDialog: TMDB integration is disabled, skipping autofill setup";
        ui->checkBox_UseTMDB->setChecked(false);
        ui->checkBox_UseTMDB->setEnabled(false);
        return;
    }
    
    // Check if TMDB API key is available
    if (!VP_ShowsConfig::hasApiKey()) {
        qDebug() << "VP_ShowsAddDialog: No TMDB API key found, disabling TMDB integration";
        qDebug() << "VP_ShowsAddDialog: Copy tmdb_api_key_TEMPLATE.h to tmdb_api_key.h and add your API key";
        ui->checkBox_UseTMDB->setChecked(false);
        ui->checkBox_UseTMDB->setEnabled(false);
        return;
    }
    
    // Get TMDB API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: TMDB API key file is empty or invalid";
        ui->checkBox_UseTMDB->setChecked(false);
        ui->checkBox_UseTMDB->setEnabled(false);
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: TMDB API key found, length:" << apiKey.length();
    
    // Initialize TMDB API
    m_tmdbApi = std::make_unique<VP_ShowsTMDB>(this);
    m_tmdbApi->setApiKey(apiKey);
    
    // Initialize network manager (kept for potential future use)
    m_networkManager = std::make_unique<QNetworkAccessManager>(this);
    // Note: Not connecting to onImageDownloadFinished since we use m_tmdbApi->downloadImage() instead
    
    // Create suggestions list widget as a child of the dialog
    // Don't use window flags - keep it as a regular child widget
    m_suggestionsList = new QListWidget(this);
    m_suggestionsList->setParent(this);  // Ensure it's a child of the dialog
    m_suggestionsList->setFocusPolicy(Qt::NoFocus);  // Prevent stealing focus
    m_suggestionsList->setMouseTracking(true);
    m_suggestionsList->setAttribute(Qt::WA_ShowWithoutActivating, true);  // Show without activating
    m_suggestionsList->setAttribute(Qt::WA_X11DoNotAcceptFocus, true);  // X11 specific but doesn't hurt on Windows
    m_suggestionsList->setAutoFillBackground(true);  // Ensure it has a background
    m_suggestionsList->setTabletTracking(false);  // Don't track tablet events
    m_suggestionsList->setFocusProxy(ui->lineEdit_ShowName);  // Redirect focus to the line edit
    m_suggestionsList->setStyleSheet(
        "QListWidget { "
        "    border: 1px solid #ccc; "
        "    background-color: white; "
        "    selection-background-color: #cce8ff; "
        "    padding: 1px; "
        "} "
        "QListWidget::item { "
        "    color: black; "
        "    background-color: white; "
        "    padding: 3px 5px; "
        "    min-height: 16px; "
        "    max-height: 20px; "
        "    border: none; "
        "    border-bottom: 1px solid #eee; "
        "} "
        "QListWidget::item:last { "
        "    border-bottom: none; "
        "} "
        "QListWidget::item:hover { "
        "    background-color: #e6f3ff; "
        "    color: black; "
        "} "
        "QListWidget::item:selected { "
        "    background-color: #cce8ff; "
        "    color: black; "
        "} "
    );
    m_suggestionsList->hide();
    
    qDebug() << "VP_ShowsAddDialog: Suggestions list widget created";
    
    // Create search timer for debouncing
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SEARCH_DELAY_MS);
    connect(m_searchTimer, &QTimer::timeout, this, &VP_ShowsAddDialog::onSearchTimerTimeout);
    
    qDebug() << "VP_ShowsAddDialog: Search timer created with interval:" << SEARCH_DELAY_MS << "ms";
    
    // Connect lineEdit signals
    connect(ui->lineEdit_ShowName, &QLineEdit::textChanged,
            this, &VP_ShowsAddDialog::onShowNameTextChanged);
    
    // Connect suggestions list signals
    connect(m_suggestionsList, &QListWidget::itemClicked,
            this, &VP_ShowsAddDialog::onSuggestionItemClicked);
    
    // Install event filter on the dialog itself for click outside detection
    this->installEventFilter(this);
    
    // Install event filter on suggestions list and its viewport for mouse tracking
    m_suggestionsList->installEventFilter(this);
    m_suggestionsList->viewport()->installEventFilter(this);
    
    qDebug() << "VP_ShowsAddDialog: Autofill UI setup complete";
}

void VP_ShowsAddDialog::onUseTMDBCheckboxToggled(bool checked)
{
    qDebug() << "VP_ShowsAddDialog: UseTMDB checkbox toggled to:" << checked;
    qDebug() << "VP_ShowsAddDialog: Current custom poster null:" << m_customPoster.isNull();
    qDebug() << "VP_ShowsAddDialog: Has custom description:" << m_hasCustomDescription;
    
    // Define styles for enabled and disabled states (matching showsettings dialog)
    QString enabledStyle = "";
    QString disabledStyle = "QPushButton { "
                           "    color: rgba(255, 255, 255, 0.4); "
                           "    background-color: rgba(60, 60, 60, 0.3); "
                           "}";
    
    // Update button states - enable custom buttons when TMDB is off, disable when on
    // But only if we're not adding to an existing show
    if (!m_isAddingToExistingShow) {
        ui->pushButton_UseCustomPoster->setEnabled(!checked);
        ui->pushButton_UseCustomDescription->setEnabled(!checked);
        
        // Apply styles to indicate button state
        if (checked) {
            // TMDB enabled - disable custom buttons
            ui->pushButton_UseCustomPoster->setStyleSheet(disabledStyle);
            ui->pushButton_UseCustomDescription->setStyleSheet(disabledStyle);
        } else {
            // TMDB disabled - enable custom buttons
            ui->pushButton_UseCustomPoster->setStyleSheet(enabledStyle);
            ui->pushButton_UseCustomDescription->setStyleSheet(enabledStyle);
        }
        
        qDebug() << "VP_ShowsAddDialog: Custom buttons enabled:" << !checked;
    }
    
    if (!checked) {
        // TMDB disabled - reset poster and description for new shows
        // Only reset if we're not adding to an existing show and haven't set custom data
        if (!m_isAddingToExistingShow && !m_hasCustomDescription && m_customPoster.isNull()) {
            qDebug() << "VP_ShowsAddDialog: TMDB disabled - resetting poster and description to empty state";
            
            // Reset the poster to empty state
            ui->label_ShowPoster->clear();
            ui->label_ShowPoster->setText("No Poster Available");
            
            // Reset the description to empty state
            ui->textBrowser_ShowDescription->clear();
            ui->textBrowser_ShowDescription->setPlainText("No description available.");
            
            // Clear any TMDB data
            m_hasTMDBData = false;
            m_currentSuggestions.clear();
        }
        
        // Clear any existing suggestions when TMDB is disabled
        if (m_isShowingSuggestions) {
            clearSuggestions();
            hideSuggestions(false);
        }
        
        // Stop any pending search
        if (m_searchTimer && m_searchTimer->isActive()) {
            m_searchTimer->stop();
        }
        
        // Reset TMDB data flag
        m_hasTMDBData = false;
        
        // When TMDB is disabled for new shows, we need to reset or show custom data
        if (!m_isAddingToExistingShow) {
            // If we have custom data, show it
            if (!m_customPoster.isNull()) {
                ui->label_ShowPoster->setPixmap(m_customPoster);
                qDebug() << "VP_ShowsAddDialog: TMDB disabled - displaying custom poster";
            } else {
                // No custom poster, reset to empty state (not original which might be TMDB data)
                ui->label_ShowPoster->clear();
                ui->label_ShowPoster->setText("No Poster Available");
                qDebug() << "VP_ShowsAddDialog: TMDB disabled - reset poster to empty state";
            }
            
            if (m_hasCustomDescription && !m_customDescription.isEmpty()) {
                ui->textBrowser_ShowDescription->clear();
                ui->textBrowser_ShowDescription->setPlainText(m_customDescription);
                qDebug() << "VP_ShowsAddDialog: TMDB disabled - displaying custom description";
            } else {
                // No custom description, reset to empty state (not original which might be TMDB data)
                ui->textBrowser_ShowDescription->clear();
                ui->textBrowser_ShowDescription->setPlainText("No description available.");
                qDebug() << "VP_ShowsAddDialog: TMDB disabled - reset description to empty state";
            }
            
            // Clear the stored "originals" since they might contain TMDB data
            m_originalPoster = QPixmap();
            m_originalDescription = "No description available.";
            
            qDebug() << "VP_ShowsAddDialog: TMDB disabled - reset complete";
        } else {
            // For existing shows, preserve the loaded show data
            qDebug() << "VP_ShowsAddDialog: TMDB disabled for existing show - preserving show data";
        }
    } else {
        // TMDB enabled - preserve custom data internally but show TMDB/original display
        // Custom data remains stored for if user toggles back
        qDebug() << "VP_ShowsAddDialog: TMDB enabled - custom data preserved internally";
        
        // Show original/default display (not custom)
        if (!m_originalPoster.isNull()) {
            ui->label_ShowPoster->setPixmap(m_originalPoster);
        } else {
            ui->label_ShowPoster->setText("No Poster Available");
        }
        ui->textBrowser_ShowDescription->clear();
        ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
        
        // When enabled and we have TMDB API, automatically search for the current show name
        if (m_tmdbApi && !m_isAddingToExistingShow) {
            QString currentShowName = ui->lineEdit_ShowName->text().trimmed();
            if (!currentShowName.isEmpty() && currentShowName.length() >= 2) {
                qDebug() << "VP_ShowsAddDialog: TMDB enabled - auto-searching for:" << currentShowName;
                
                // Store the current search text
                m_currentSearchText = currentShowName;
                
                // Start the search timer to trigger TMDB search
                if (m_searchTimer) {
                    m_searchTimer->stop();
                    m_searchTimer->start();
                }
                
                // Set the TMDB data flag since we're about to show TMDB results
                m_hasTMDBData = true;
            }
        }
    }
    // When enabled, the user can start typing to search (only for new shows)
}

void VP_ShowsAddDialog::onShowNameTextChanged(const QString& text)
{
    qDebug() << "VP_ShowsAddDialog: onShowNameTextChanged called with text:" << text;
    
    // Don't search if we're adding to an existing show (show name is read-only)
    if (m_isAddingToExistingShow) {
        qDebug() << "VP_ShowsAddDialog: Adding to existing show, not searching";
        return;
    }
    
    // Check if we need to reset the m_itemJustSelected flag
    // This happens when the user manually edits the text after selecting from TMDB
    static QString lastTextFromSelection;
    static int lastSelectedShowId = 0;  // Remember the last selected ID
    
    if (m_itemJustSelected) {
        // We just selected from TMDB, store this text and the selected ID
        lastTextFromSelection = text;
        lastSelectedShowId = m_selectedShowId;  // Preserve the ID
        qDebug() << "VP_ShowsAddDialog: Text changed from TMDB selection, storing text:" << text 
                 << "and preserving show ID:" << m_selectedShowId;
        // Don't reset flags here - keep them set
    } else if (!lastTextFromSelection.isEmpty() && text != lastTextFromSelection) {
        // Text changed after a TMDB selection - user is manually editing
        qDebug() << "VP_ShowsAddDialog: User manually edited text after TMDB selection";
        qDebug() << "VP_ShowsAddDialog: Text changed from '" << lastTextFromSelection << "' to '" << text << "'";
        qDebug() << "VP_ShowsAddDialog: Resetting m_hasTMDBData from" << m_hasTMDBData << "to false";
        qDebug() << "VP_ShowsAddDialog: Resetting selected show ID from" << m_selectedShowId << "to 0";
        m_hasTMDBData = false;  // Reset the TMDB data flag
        m_selectedShowId = 0;  // Reset the selected show ID
        m_userSelectedFromDropdown = false;  // Reset the user selection flag
        lastTextFromSelection.clear();
        lastSelectedShowId = 0;
    } else if (!lastTextFromSelection.isEmpty() && text == lastTextFromSelection && m_userSelectedFromDropdown) {
        // Text hasn't changed from selection - preserve the selected ID
        if (lastSelectedShowId > 0 && m_selectedShowId != lastSelectedShowId) {
            qDebug() << "VP_ShowsAddDialog: Restoring selected show ID from" << m_selectedShowId << "to" << lastSelectedShowId;
            m_selectedShowId = lastSelectedShowId;
        }
    }
    
    // Don't check for existing show if we just selected a TMDB item
    if (m_itemJustSelected) {
        qDebug() << "VP_ShowsAddDialog: Text changed from TMDB selection, skipping all processing";
        qDebug() << "VP_ShowsAddDialog: Preserving selected show ID:" << m_selectedShowId 
                 << "and user selection flag:" << m_userSelectedFromDropdown;
        m_lastCheckedShowName = text;  // Update the last checked name so future edits will trigger check
        // Don't do any TMDB searching or existing show checking
        // Reset the flag here after we've used it
        m_itemJustSelected = false;
        return;  // Return early to prevent any further processing
    } else {
        // Use debounce timer for checking existing show
        if (!m_isCheckingExistingShow && text != m_lastCheckedShowName && !text.isEmpty()) {
            qDebug() << "VP_ShowsAddDialog: Scheduling existing show check for:" << text;
            m_pendingShowNameCheck = text;
            
            // Start or restart the existing show check timer
            if (m_existingShowCheckTimer) {
                m_existingShowCheckTimer->stop();
                m_existingShowCheckTimer->start();
            }
        }
    }
    
    // Check if UseTMDB checkbox is checked
    if (!ui->checkBox_UseTMDB->isChecked()) {
        qDebug() << "VP_ShowsAddDialog: UseTMDB checkbox is unchecked, not searching";
        return;
    }
    
    // Check if TMDB API is initialized
    if (!m_tmdbApi) {
        qDebug() << "VP_ShowsAddDialog: TMDB API not initialized, cannot search";
        return;
    }
    
    // Validate input
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(text, InputValidation::InputType::PlainText, 100);
    
    if (!result.isValid) {
        qDebug() << "VP_ShowsAddDialog: Invalid input detected:" << result.errorMessage;
        return;
    }
    
    // Clear current suggestions if text is too short
    if (text.trimmed().length() < 2) {
        qDebug() << "VP_ShowsAddDialog: Text too short (< 2 chars), clearing suggestions";
        if (m_isShowingSuggestions) {
            clearSuggestions();
            hideSuggestions(false);
        }
        return;
    }
    
    // Store current search text
    m_currentSearchText = text.trimmed();
    
    // Reset and start the search timer (debouncing)
    if (m_searchTimer) {
        m_searchTimer->stop();
        m_searchTimer->start();
        qDebug() << "VP_ShowsAddDialog: Text changed, starting search timer for:" << m_currentSearchText;
    }
}

void VP_ShowsAddDialog::onSearchTimerTimeout()
{
    qDebug() << "VP_ShowsAddDialog: Search timer timeout, performing search for:" << m_currentSearchText;
    
    if (m_currentSearchText.isEmpty() || m_currentSearchText.length() < 2) {
        return;
    }
    
    // First check if this show already exists in our library
    QString showName = ui->lineEdit_ShowName->text().trimmed();
    if (!showName.isEmpty() && showName != m_lastCheckedShowName) {
        checkForExistingShow(showName);
        
        // If we found an existing show, it will have loaded the data
        if (!m_originalPoster.isNull() || m_originalDescription != "No description available.") {
            qDebug() << "VP_ShowsAddDialog: Existing show data loaded, skipping TMDB search and suggestions";
            // Don't show suggestions when the show already exists in our library
            // Just return early without performing TMDB search
            return;
        }
    }
    
    performTMDBSearch(m_currentSearchText);
}

void VP_ShowsAddDialog::onExistingShowCheckTimeout()
{
    qDebug() << "VP_ShowsAddDialog: Existing show check timer timeout, checking for:" << m_pendingShowNameCheck;
    
    if (m_pendingShowNameCheck.isEmpty()) {
        return;
    }
    
    // Perform the actual check
    checkForExistingShow(m_pendingShowNameCheck);
    
    // Clear the pending check
    m_pendingShowNameCheck.clear();
}

void VP_ShowsAddDialog::performTMDBSearch(const QString& searchText)
{
    if (!m_tmdbApi) {
        qDebug() << "VP_ShowsAddDialog: TMDB API not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: Performing TMDB search for:" << searchText;
    
    // Clear previous suggestions before starting new search
    qDebug() << "VP_ShowsAddDialog: Clearing m_currentSuggestions (had" << m_currentSuggestions.size() << "items)";
    qDebug() << "VP_ShowsAddDialog: Before clearing - Selected ID:" << m_selectedShowId 
             << "User selection flag:" << m_userSelectedFromDropdown;
    m_currentSuggestions.clear();
    clearSuggestions();
    // Don't reset m_hasTMDBData here - we're about to load new TMDB data
    // But do reset selection flags since this is a new search
    // UNLESS the user has manually selected something and the search text matches their selection
    QString currentText = ui->lineEdit_ShowName ? ui->lineEdit_ShowName->text() : "";
    if (!m_userSelectedFromDropdown || currentText != searchText) {
        qDebug() << "VP_ShowsAddDialog: Resetting selection for new search";
        m_userSelectedFromDropdown = false;
        m_selectedShowId = 0;
    } else {
        qDebug() << "VP_ShowsAddDialog: Keeping user selection despite new search (same text)";
    }
    
    // Search for TV shows and get multiple results
    m_currentSuggestions = m_tmdbApi->searchTVShows(searchText, MAX_SUGGESTIONS);
    
    qDebug() << "VP_ShowsAddDialog: Search returned" << m_currentSuggestions.size() << "results";
    
    if (!m_currentSuggestions.isEmpty()) {
        displaySuggestions(m_currentSuggestions);
        
        // Only auto-display the first match if user hasn't already selected something
        if (!m_userSelectedFromDropdown) {
            // Automatically display the first match's poster and description
            // This provides immediate visual feedback while keeping suggestions visible
            qDebug() << "VP_ShowsAddDialog: Auto-displaying first TMDB match (no user selection yet)";
            displayShowInfo(m_currentSuggestions.first());
        } else {
            qDebug() << "VP_ShowsAddDialog: Keeping user's selected show ID:" << m_selectedShowId;
        }
        
        // Mark that we have TMDB data loaded
        m_hasTMDBData = true;
    } else {
        qDebug() << "VP_ShowsAddDialog: No results found for:" << searchText;
        clearSuggestions();
        hideSuggestions(false);
    }
}

void VP_ShowsAddDialog::displaySuggestions(const QList<VP_ShowsTMDB::ShowInfo>& shows)
{
    qDebug() << "VP_ShowsAddDialog: displaySuggestions called with" << shows.size() << "shows";
    
    if (!m_suggestionsList) {
        qDebug() << "VP_ShowsAddDialog: ERROR - m_suggestionsList is null!";
        return;
    }
    
    // Clear existing items
    clearSuggestions();
    
    // Store the suggestions for later use
    m_currentSuggestions = shows;
    qDebug() << "VP_ShowsAddDialog: Stored" << m_currentSuggestions.size() << "suggestions in m_currentSuggestions";
    
    // Add items to the list
    for (const VP_ShowsTMDB::ShowInfo& show : shows) {
        QString displayText = show.showName;
        if (!show.firstAirDate.isEmpty() && show.firstAirDate.length() >= 4) {
            displayText += QString(" (%1)").arg(show.firstAirDate.left(4));
        }
        
        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, show.tmdbId);
        m_suggestionsList->addItem(item);
        
        qDebug() << "VP_ShowsAddDialog: Added item:" << displayText << "with TMDB ID:" << show.tmdbId;
    }
    
    // Position and show the suggestions list
    positionSuggestionsList();
    m_suggestionsList->show();
    m_suggestionsList->raise();  // Ensure it's on top
    m_isShowingSuggestions = true;
    
    // Keep focus on the show name field
    ui->lineEdit_ShowName->setFocus();
    ui->lineEdit_ShowName->activateWindow();
    
    // Use a timer to ensure focus stays on the text field
    QTimer::singleShot(0, this, [this]() {
        if (ui->lineEdit_ShowName) {
            ui->lineEdit_ShowName->setFocus();
            ui->lineEdit_ShowName->activateWindow();
        }
    });
    
    // Note: Auto-display of first match is now handled in performTMDBSearch()
    // to ensure it happens consistently whether suggestions are shown or not
    
    qDebug() << "VP_ShowsAddDialog: Suggestions list shown with" << m_suggestionsList->count() << "items";
}

void VP_ShowsAddDialog::clearSuggestions()
{
    if (m_suggestionsList) {
        m_suggestionsList->clear();
    }
    // Note: We do NOT clear m_currentSuggestions here - it needs to persist
    // until after a selection is made or the suggestions are hidden
}

void VP_ShowsAddDialog::hideSuggestions(bool itemWasSelected)
{
    qDebug() << "VP_ShowsAddDialog: hideSuggestions() called, itemWasSelected:" << itemWasSelected;
    qDebug() << "VP_ShowsAddDialog: m_currentSuggestions has" << m_currentSuggestions.size() << "items before hiding";
    
    // Clear the flags and hover index
    m_isShowingSuggestions = false;
    m_hoveredItemIndex = -1;
    
    // Reset the item selection flag after hiding suggestions
    if (itemWasSelected) {
        // Keep the flag set a bit longer to prevent interference
        // It will be reset on the next text change or other interaction
    } else {
        m_itemJustSelected = false;  // Reset if no item was selected
    }
    
    if (m_suggestionsList) {
        // Clear selection before hiding
        m_suggestionsList->clearSelection();
        qDebug() << "VP_ShowsAddDialog: Hiding suggestions list";
        m_suggestionsList->hide();
    }
    
    // Only restore original poster and description if no item was selected
    if (!itemWasSelected) {
        qDebug() << "VP_ShowsAddDialog: No item selected, checking for existing show or TMDB data";
        
        // Get the current show name from the text field
        QString currentShowName = ui->lineEdit_ShowName->text().trimmed();
        
        // First priority: Check if show exists in library
        if (!currentShowName.isEmpty() && currentShowName != m_lastCheckedShowName) {
            checkForExistingShow(currentShowName);
            
            // If we found an existing show, keep its data displayed
            if (!m_originalPoster.isNull() || m_originalDescription != "No description available.") {
                qDebug() << "VP_ShowsAddDialog: Keeping existing show data after closing suggestions";
                // Don't clear m_currentSuggestions yet - might be needed for future interactions
                return;  // Exit early, keeping the existing show data
            }
        }
        
        // Second priority: If TMDB is enabled and we have suggestions, auto-load first match
        if (ui->checkBox_UseTMDB->isChecked() && !m_currentSuggestions.isEmpty()) {
            qDebug() << "VP_ShowsAddDialog: Auto-loading first TMDB match after closing suggestions";
            displayShowInfo(m_currentSuggestions.first());
            m_hasTMDBData = true;
            // Don't clear m_currentSuggestions - we're using them
            return;  // Exit early, keeping the TMDB data
        }
        
        // No existing show or TMDB data found, restore to empty state
        qDebug() << "VP_ShowsAddDialog: No data found, restoring to empty state";
        if (!m_originalPoster.isNull()) {
            ui->label_ShowPoster->setPixmap(m_originalPoster);
        } else {
            ui->label_ShowPoster->setText("No Poster Available");
        }
        ui->textBrowser_ShowDescription->clear();  // Clear any HTML
        ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
        m_hasTMDBData = false;
        
        // Clear the current suggestions when hiding (but not if an item was selected)
        m_currentSuggestions.clear();
    }
}

void VP_ShowsAddDialog::positionSuggestionsList()
{
    if (!m_suggestionsList || !ui->lineEdit_ShowName) {
        return;
    }
    
    // Get the position of the lineEdit relative to the dialog
    QPoint lineEditPos = ui->lineEdit_ShowName->mapTo(this, QPoint(0, ui->lineEdit_ShowName->height()));
    
    // Set the position of the suggestions list relative to the dialog
    m_suggestionsList->move(lineEditPos);
    
    // Set the width to match the lineEdit
    m_suggestionsList->setFixedWidth(ui->lineEdit_ShowName->width());
    
    // Calculate height based on number of items (max 8 items visible)
    int itemHeight = 24;  // Approximate height per item
    int maxVisibleItems = qMin(m_suggestionsList->count(), 8);
    int listHeight = maxVisibleItems * itemHeight + 4;  // +4 for borders/padding
    m_suggestionsList->setFixedHeight(listHeight);
    
    // Ensure the suggestions list is on top of other widgets
    m_suggestionsList->raise();
    
    // Ensure the show name field keeps focus
    ui->lineEdit_ShowName->setFocus();
}

void VP_ShowsAddDialog::onSuggestionItemClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: Suggestion item clicked:" << item->text();
    
    // Get the TMDB ID from the item data
    int tmdbId = item->data(Qt::UserRole).toInt();
    qDebug() << "VP_ShowsAddDialog: Retrieved TMDB ID from item:" << tmdbId;
    
    // Debug: Print all current suggestions
    qDebug() << "VP_ShowsAddDialog: Current suggestions count:" << m_currentSuggestions.size();
    for (const VP_ShowsTMDB::ShowInfo& show : m_currentSuggestions) {
        qDebug() << "VP_ShowsAddDialog:   - Show:" << show.showName << "ID:" << show.tmdbId;
    }
    
    // Find the corresponding show info
    VP_ShowsTMDB::ShowInfo selectedShow;
    for (const VP_ShowsTMDB::ShowInfo& show : m_currentSuggestions) {
        if (show.tmdbId == tmdbId) {
            selectedShow = show;
            break;
        }
    }
    
    if (selectedShow.tmdbId == 0) {
        qDebug() << "VP_ShowsAddDialog: Could not find show info for TMDB ID:" << tmdbId;
        return;
    }
    
    // Store the selected show's TMDB ID
    m_selectedShowId = selectedShow.tmdbId;
    m_userSelectedFromDropdown = true;  // Mark that user explicitly selected this
    qDebug() << "VP_ShowsAddDialog: User selected show from dropdown:" << selectedShow.showName 
             << "ID:" << m_selectedShowId;
    
    // Set flag BEFORE setting text to prevent checkForExistingShow from being called
    m_itemJustSelected = true;
    
    // Display the show info (this should NOT reset m_selectedShowId since m_userSelectedFromDropdown is true)
    displayShowInfo(selectedShow);
    qDebug() << "VP_ShowsAddDialog: After displayShowInfo, ID is:" << m_selectedShowId;
    
    // Update the "original" poster and description to the TMDB selection
    // This ensures they persist as the new baseline
    m_originalDescription = selectedShow.overview.isEmpty() ? "No description available." : selectedShow.overview;
    // Note: m_originalPoster is updated directly in downloadAndDisplayPoster when m_itemJustSelected is true
    
    // Mark that we have TMDB data loaded
    qDebug() << "VP_ShowsAddDialog: Setting m_hasTMDBData to true (from onSuggestionItemClicked)";
    m_hasTMDBData = true;
    
    // Set the selected show name in the line edit
    // This will trigger onShowNameTextChanged, but m_itemJustSelected flag will prevent existing show check
    qDebug() << "VP_ShowsAddDialog: About to set text to:" << selectedShow.showName 
             << "Current ID:" << m_selectedShowId << "User selection flag:" << m_userSelectedFromDropdown;
    ui->lineEdit_ShowName->setText(selectedShow.showName);
    qDebug() << "VP_ShowsAddDialog: After setText, ID is:" << m_selectedShowId 
             << "User selection flag:" << m_userSelectedFromDropdown;
    
    // Stop any pending searches
    if (m_searchTimer) {
        m_searchTimer->stop();
    }
    
    // Hide suggestions (pass true to indicate an item was selected)
    hideSuggestions(true);
    
    // Return focus to the show name field
    ui->lineEdit_ShowName->setFocus();
    ui->lineEdit_ShowName->setCursorPosition(ui->lineEdit_ShowName->text().length());  // Place cursor at end
}

void VP_ShowsAddDialog::displayShowInfo(const VP_ShowsTMDB::ShowInfo& showInfo)
{
    // Note: When called from onSuggestionItemClicked, m_selectedShowId is already set
    // and m_userSelectedFromDropdown is true, so we should NOT overwrite it
    // When called from auto-display, m_userSelectedFromDropdown is false so we need to set it
    if (!m_userSelectedFromDropdown) {
        // Auto-display case - update the ID
        m_selectedShowId = showInfo.tmdbId;
        qDebug() << "VP_ShowsAddDialog: Displaying show info (auto-display), stored ID:" << m_selectedShowId;
    } else {
        qDebug() << "VP_ShowsAddDialog: Displaying show info (user selection), keeping ID:" << m_selectedShowId
                 << "(show being displayed has ID:" << showInfo.tmdbId << ")";
    }
    
    // Display show description
    ui->textBrowser_ShowDescription->clear();  // Clear any existing HTML
    if (!showInfo.overview.isEmpty()) {
        ui->textBrowser_ShowDescription->setPlainText(showInfo.overview);
    } else {
        ui->textBrowser_ShowDescription->setPlainText("No description available.");
    }
    
    // Download and display poster if available
    if (!showInfo.posterPath.isEmpty()) {
        downloadAndDisplayPoster(showInfo.posterPath);
    }
}

void VP_ShowsAddDialog::downloadAndDisplayPoster(const QString& posterPath)
{
    if (posterPath.isEmpty() || !m_tmdbApi) {
        qDebug() << "VP_ShowsAddDialog: Cannot download poster - empty path or no TMDB API";
        return;
    }
    
    // Get the target size from the label
    QSize labelSize = ui->label_ShowPoster->size();
    qDebug() << "VP_ShowsAddDialog: Label size for poster:" << labelSize;
    
    // Check if we already have this poster in cache
    if (m_posterCache.contains(posterPath)) {
        qDebug() << "VP_ShowsAddDialog: Using cached poster for:" << posterPath;
        
        // Update access order for LRU
        m_cacheAccessOrder.removeAll(posterPath);
        m_cacheAccessOrder.append(posterPath);
        
        // Display the pre-scaled poster from cache
        ui->label_ShowPoster->setPixmap(m_posterCache[posterPath].scaledPixmap);
        
        // If we're selecting from TMDB, update the original poster
        if (m_itemJustSelected) {
            m_originalPoster = m_posterCache[posterPath].scaledPixmap;
            qDebug() << "VP_ShowsAddDialog: Updated original poster from TMDB cache";
        }
        
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: Poster not in cache, downloading:" << posterPath;
    
    // Get the user's temp directory
    QString username = OperationsFiles::getUsername();
    if (username.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Cannot get username for temp directory";
        ui->label_ShowPoster->setText("Failed to Get User");
        return;
    }
    
    QString tempDir = VP_ShowsConfig::getTempDirectory(username);
    if (tempDir.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Failed to get temp directory";
        ui->label_ShowPoster->setText("No Temp Directory");
        return;
    }
    
    // Create temp directory if it doesn't exist
    QDir dir;
    if (!dir.exists(tempDir)) {
        if (!dir.mkpath(tempDir)) {
            qDebug() << "VP_ShowsAddDialog: Failed to create temp directory:" << tempDir;
            ui->label_ShowPoster->setText("Failed to Create Temp Dir");
            return;
        }
    }
    
    // Create a temporary file path in the user's temp directory using QUuid for unpredictability
    QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString tempFileName = QString("tmdb_poster_%1.jpg").arg(uniqueId);
    QString tempFilePath = QDir(tempDir).absoluteFilePath(tempFileName);
    
    qDebug() << "VP_ShowsAddDialog: Downloading poster to temp file:" << tempFilePath;
    
    // Show loading text
    ui->label_ShowPoster->setText("Loading...");
    
    // Download the poster using TMDB API
    bool success = m_tmdbApi->downloadImage(posterPath, tempFilePath, true); // true = isPoster
    
    if (success && QFile::exists(tempFilePath)) {
        qDebug() << "VP_ShowsAddDialog: Successfully downloaded poster to:" << tempFilePath;
        
        // Load the downloaded image
        QPixmap poster(tempFilePath);
        if (!poster.isNull()) {
            qDebug() << "VP_ShowsAddDialog: Loaded poster, original size:" << poster.size();
            
            // Scale to fit the label and cache the scaled version
            QPixmap scaledPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            qDebug() << "VP_ShowsAddDialog: Scaled poster to:" << scaledPoster.size();
            
            // Add to cache (this will handle size limits)
            addToCache(posterPath, scaledPoster);
            
            // Display the scaled poster
            ui->label_ShowPoster->setPixmap(scaledPoster);
            
            // If we're selecting from TMDB, update the original poster
            if (m_itemJustSelected) {
                m_originalPoster = scaledPoster;
                qDebug() << "VP_ShowsAddDialog: Updated original poster from TMDB download";
            }
            
            // Clean up the temp file using secure delete from operations_files
            // Use 1 pass for temp files and allow external files since it's in Data/username/temp
            if (!OperationsFiles::secureDelete(tempFilePath, 1, false)) {
                qDebug() << "VP_ShowsAddDialog: Failed to securely delete temp file:" << tempFilePath;
                // Try regular delete as fallback
                QFile::remove(tempFilePath);
            } else {
                qDebug() << "VP_ShowsAddDialog: Securely deleted temp file:" << tempFilePath;
            }
        } else {
            qDebug() << "VP_ShowsAddDialog: Failed to load poster image from:" << tempFilePath;
            ui->label_ShowPoster->setText("Failed to Load");
            
            // Clean up the temp file
            OperationsFiles::secureDelete(tempFilePath, 1, false);
        }
    } else {
        qDebug() << "VP_ShowsAddDialog: Failed to download poster";
        ui->label_ShowPoster->setText("Download Failed");
        
        // Clean up any partial temp file
        if (QFile::exists(tempFilePath)) {
            OperationsFiles::secureDelete(tempFilePath, 1, false);
        }
    }
}

void VP_ShowsAddDialog::onImageDownloadFinished(QNetworkReply* reply)
{
    // This method is no longer used since we're using m_tmdbApi->downloadImage() instead
    // of direct network downloads. Keeping empty implementation for compatibility.
    if (reply) {
        reply->deleteLater();
    }
}

void VP_ShowsAddDialog::addToCache(const QString& posterPath, const QPixmap& scaledPixmap)
{
    qDebug() << "VP_ShowsAddDialog: Adding poster to cache:" << posterPath;
    
    // Estimate size of this pixmap
    qint64 pixmapSize = estimatePixmapSize(scaledPixmap);
    
    // Check if we need to make room
    enforeCacheLimits();
    
    // Add to cache
    CachedPoster cachedPoster;
    cachedPoster.scaledPixmap = scaledPixmap;
    cachedPoster.posterPath = posterPath;
    cachedPoster.sizeInBytes = pixmapSize;
    
    m_posterCache[posterPath] = cachedPoster;
    m_cacheAccessOrder.append(posterPath);
    m_currentCacheSize += pixmapSize;
    
    qDebug() << "VP_ShowsAddDialog: Cache now contains" << m_posterCache.size() << "posters, total size:" << m_currentCacheSize;
}

void VP_ShowsAddDialog::enforeCacheLimits()
{
    // Remove items if we exceed the cache limits
    while ((m_posterCache.size() >= MAX_CACHE_ITEMS || m_currentCacheSize >= MAX_CACHE_SIZE) && !m_cacheAccessOrder.isEmpty()) {
        // Remove least recently used item
        QString oldestPath = m_cacheAccessOrder.takeFirst();
        
        if (m_posterCache.contains(oldestPath)) {
            qint64 removedSize = m_posterCache[oldestPath].sizeInBytes;
            m_posterCache.remove(oldestPath);
            m_currentCacheSize -= removedSize;
            
            qDebug() << "VP_ShowsAddDialog: Removed from cache:" << oldestPath << "freed:" << removedSize << "bytes";
        }
    }
}

qint64 VP_ShowsAddDialog::estimatePixmapSize(const QPixmap& pixmap)
{
    // Estimate memory usage: width * height * 4 bytes per pixel (RGBA)
    return pixmap.width() * pixmap.height() * 4;
}

bool VP_ShowsAddDialog::eventFilter(QObject* obj, QEvent* event)
{
    // Prevent the suggestions list from ever getting focus
    if (obj == m_suggestionsList && event->type() == QEvent::FocusIn) {
        qDebug() << "VP_ShowsAddDialog: Suggestions list tried to get focus, redirecting to show name field";
        ui->lineEdit_ShowName->setFocus();
        return true;  // Consume the event
    }
    
    // Handle mouse move events on the suggestions list viewport
    if (obj == m_suggestionsList->viewport() && event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint pos = mouseEvent->pos();
        
        // Get the item at the mouse position
        QListWidgetItem* item = m_suggestionsList->itemAt(pos);
        if (item) {
            int itemIndex = m_suggestionsList->row(item);
            
            // Only update if we're hovering over a different item
            if (itemIndex != m_hoveredItemIndex && itemIndex < m_currentSuggestions.size()) {
                m_hoveredItemIndex = itemIndex;
                
                // Display the hovered show's information
                const VP_ShowsTMDB::ShowInfo& hoveredShow = m_currentSuggestions[itemIndex];
                displayShowInfo(hoveredShow);
                
                // Mark that we're showing TMDB data
                m_hasTMDBData = true;
                
                qDebug() << "VP_ShowsAddDialog: Hovering over:" << hoveredShow.showName;
            }
        }
        return false;
    }
    
    // Handle mouse leave events on the suggestions list
    if (obj == m_suggestionsList && event->type() == QEvent::Leave) {
        // Only restore if we haven't just selected an item
        if (!m_itemJustSelected) {
            qDebug() << "VP_ShowsAddDialog: Mouse left suggestions list, restoring original display";
            
            // Restore original poster and description
            if (!m_originalPoster.isNull()) {
                ui->label_ShowPoster->setPixmap(m_originalPoster);
            } else {
                ui->label_ShowPoster->setText("No Poster Available");
            }
            ui->textBrowser_ShowDescription->clear();  // Clear any HTML
            ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
            
            m_hoveredItemIndex = -1;
            
            // Clear TMDB data flag since we're restoring to original (non-TMDB) content
            // unless the original content is also from TMDB (e.g., from a previous selection)
            // We can tell by checking if m_originalDescription is not the default
            if (m_originalDescription == "No description available.") {
                m_hasTMDBData = false;
            }
        }
        // Reset the flag after checking
        m_itemJustSelected = false;
        return false;
    }
    
    // Handle clicks outside the suggestions list to hide it
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint pos = mouseEvent->pos();  // Position relative to dialog
        
        if (m_suggestionsList && m_suggestionsList->isVisible()) {
            // Get the global position of the click
            QPoint globalPos = mouseEvent->globalPos();
            
            // Check if the click is within the suggestions list widget or its children
            QWidget* clickedWidget = QApplication::widgetAt(globalPos);
            
            qDebug() << "VP_ShowsAddDialog: Clicked widget:" << (clickedWidget ? clickedWidget->metaObject()->className() : "null");
            
            // Check if the clicked widget is the suggestions list or one of its children
            bool clickedOnSuggestions = false;
            QWidget* widget = clickedWidget;
            while (widget) {
                if (widget == m_suggestionsList || widget == m_suggestionsList->viewport()) {
                    clickedOnSuggestions = true;
                    break;
                }
                widget = widget->parentWidget();
            }
            
            // Also check if click is on the line edit
            bool clickedOnLineEdit = false;
            widget = clickedWidget;
            while (widget) {
                if (widget == ui->lineEdit_ShowName) {
                    clickedOnLineEdit = true;
                    break;
                }
                widget = widget->parentWidget();
            }
            
            if (!clickedOnSuggestions && !clickedOnLineEdit) {
                qDebug() << "VP_ShowsAddDialog: Click outside suggestions";
                hideSuggestions(false);  // false = restore original values
                // Clear TMDB data flag if we're restoring to non-TMDB content
                if (m_originalDescription == "No description available.") {
                    m_hasTMDBData = false;
                }
            }
        }
        return false;
    }
    
    return QDialog::eventFilter(obj, event);
}

void VP_ShowsAddDialog::onUseCustomPosterClicked()
{
    qDebug() << "VP_ShowsAddDialog: Use custom poster button clicked";
    
    // Get username for temp directory
    QString username = OperationsFiles::getUsername();
    if (username.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot determine username"));
        return;
    }
    
    // Open file dialog to select image
    QString filter = tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
    QString selectedFile = QFileDialog::getOpenFileName(
        this,
        tr("Select Show Poster"),
        QDir::homePath(),
        filter
    );
    
    if (selectedFile.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: No poster file selected";
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: Selected poster file:" << selectedFile;
    
    // Validate the file path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        selectedFile, InputValidation::InputType::ExternalFilePath, 1000);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Invalid File"), 
                           tr("The selected file path is invalid: %1").arg(result.errorMessage));
        return;
    }
    
    // Check if file exists and is readable
    QFileInfo fileInfo(selectedFile);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        QMessageBox::warning(this, tr("File Error"), 
                           tr("Cannot read the selected file."));
        return;
    }
    
    // Check file size (limit to 10MB for posters)
    qint64 fileSize = fileInfo.size();
    if (fileSize > 10 * 1024 * 1024) {
        QMessageBox::warning(this, tr("File Too Large"), 
                           tr("The selected image is too large. Please select an image smaller than 10MB."));
        return;
    }
    
    // Load the image
    QImageReader reader(selectedFile);
    reader.setAutoTransform(true);  // Handle EXIF orientation
    
    QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Invalid Image"), 
                           tr("Failed to load the selected image file."));
        return;
    }
    
    // Convert to pixmap and scale to fit the label
    QPixmap poster = QPixmap::fromImage(image);
    QSize labelSize = ui->label_ShowPoster->size();
    m_customPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // Display the custom poster
    ui->label_ShowPoster->setPixmap(m_customPoster);
    
    qDebug() << "VP_ShowsAddDialog: Custom poster loaded and displayed";
    qDebug() << "VP_ShowsAddDialog: Original size:" << poster.size() << "Scaled size:" << m_customPoster.size();
    qDebug() << "VP_ShowsAddDialog: m_customPoster.isNull() =" << m_customPoster.isNull();
    qDebug() << "VP_ShowsAddDialog: hasCustomPoster() would return:" << !m_customPoster.isNull();
}

void VP_ShowsAddDialog::onUseCustomDescriptionClicked()
{
    qDebug() << "VP_ShowsAddDialog: Use custom description button clicked";
    
    // Get current description to show as default
    QString currentDescription;
    if (m_hasCustomDescription) {
        currentDescription = m_customDescription;
    } else if (!m_originalDescription.isEmpty() && m_originalDescription != "No description available.") {
        currentDescription = m_originalDescription;
    } else {
        currentDescription = "";
    }
    
    // Open multi-line input dialog
    bool ok;
    QString description = QInputDialog::getMultiLineText(
        this,
        tr("Enter Show Description"),
        tr("Enter a custom description for the show:"),
        currentDescription,
        &ok
    );
    
    if (!ok) {
        qDebug() << "VP_ShowsAddDialog: Description input cancelled";
        return;
    }
    
    // Validate the description
    if (!description.isEmpty()) {
        InputValidation::ValidationResult result = InputValidation::validateInput(
            description, InputValidation::InputType::PlainText, 5000);  // Allow up to 5000 chars
        
        if (!result.isValid) {
            QMessageBox::warning(this, tr("Invalid Description"), 
                               tr("The description contains invalid characters: %1").arg(result.errorMessage));
            return;
        }
    }
    
    // Store the custom description
    m_customDescription = description.isEmpty() ? "No description available." : description;
    m_hasCustomDescription = true;
    
    // Display the custom description
    ui->textBrowser_ShowDescription->clear();
    ui->textBrowser_ShowDescription->setPlainText(m_customDescription);
    
    qDebug() << "VP_ShowsAddDialog: Custom description set";
    qDebug() << "VP_ShowsAddDialog: Description length:" << m_customDescription.length();
    qDebug() << "VP_ShowsAddDialog: m_hasCustomDescription =" << m_hasCustomDescription;
    qDebug() << "VP_ShowsAddDialog: hasCustomDescription() would return:" << m_hasCustomDescription;
    qDebug() << "VP_ShowsAddDialog: Description preview:" << m_customDescription.left(100);
}

void VP_ShowsAddDialog::keyPressEvent(QKeyEvent* event)
{
    // Handle arrow keys for navigating suggestions
    if (m_suggestionsList && m_suggestionsList->isVisible()) {
        if (event->key() == Qt::Key_Down) {
            // Move selection down
            int currentRow = m_suggestionsList->currentRow();
            if (currentRow < m_suggestionsList->count() - 1) {
                m_suggestionsList->setCurrentRow(currentRow + 1);
                m_hoveredItemIndex = currentRow + 1;
                
                // Update preview for newly selected item
                if (m_hoveredItemIndex < m_currentSuggestions.size()) {
                    displayShowInfo(m_currentSuggestions[m_hoveredItemIndex]);
                    m_hasTMDBData = true;  // Mark that we're showing TMDB data
                }
            }
            // Keep focus on the text field
            ui->lineEdit_ShowName->setFocus();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Up) {
            // Move selection up
            int currentRow = m_suggestionsList->currentRow();
            if (currentRow > 0) {
                m_suggestionsList->setCurrentRow(currentRow - 1);
                m_hoveredItemIndex = currentRow - 1;
                
                // Update preview for newly selected item
                if (m_hoveredItemIndex >= 0 && m_hoveredItemIndex < m_currentSuggestions.size()) {
                    displayShowInfo(m_currentSuggestions[m_hoveredItemIndex]);
                    m_hasTMDBData = true;  // Mark that we're showing TMDB data
                }
            }
            // Keep focus on the text field
            ui->lineEdit_ShowName->setFocus();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            // Select current item
            QListWidgetItem* currentItem = m_suggestionsList->currentItem();
            if (currentItem) {
                onSuggestionItemClicked(currentItem);
            }
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Escape) {
            // Hide suggestions and restore original values
            qDebug() << "VP_ShowsAddDialog: ESC pressed, hiding suggestions and restoring original values";
            hideSuggestions(false);  // false = no item was selected, restore originals
            // Clear TMDB data flag if we're restoring to non-TMDB content
            if (m_originalDescription == "No description available.") {
                m_hasTMDBData = false;
            }
            event->accept();
            return;
        }
    }
    
    // Pass to base class for default handling
    QDialog::keyPressEvent(event);
}

void VP_ShowsAddDialog::checkForExistingShow(const QString& showName)
{
    qDebug() << "VP_ShowsAddDialog: Checking for existing show:" << showName;
    
    // Prevent recursive calls
    if (m_isCheckingExistingShow) {
        return;
    }
    
    // If we have custom data set, don't overwrite it with existing show data
    if (!m_customPoster.isNull() || m_hasCustomDescription) {
        qDebug() << "VP_ShowsAddDialog: Custom data already set, skipping existing show check";
        return;
    }
    
    // Mark that we're checking this show name
    m_lastCheckedShowName = showName;
    
    // Validate the input first - use TVShowName type for proper validation
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(showName, InputValidation::InputType::TVShowName, 200);
    
    if (!result.isValid || showName.trimmed().isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Invalid or empty show name, skipping check";
        return;
    }
    
    // Try to cast parent to MainWindow to get user credentials
    MainWindow* mainWindow = qobject_cast<MainWindow*>(m_parentWidget);
    if (!mainWindow) {
        qDebug() << "VP_ShowsAddDialog: Could not cast parent to MainWindow";
        return;
    }
    
    // Get user credentials
    QString username = mainWindow->user_Username;
    QByteArray encryptionKey = mainWindow->user_Key;
    
    if (username.isEmpty() || encryptionKey.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: Username or encryption key not available";
        return;
    }
    
    // Build the path to the shows directory
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    QDir showsDir(showsPath);
    if (!showsDir.exists()) {
        qDebug() << "VP_ShowsAddDialog: Shows directory does not exist yet";
        return;
    }
    
    // Get all subdirectories in the shows folder
    QStringList showFolders = showsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    if (showFolders.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: No show folders found";
        return;
    }
    
    // Create metadata manager for reading show metadata
    VP_ShowsMetadata metadataManager(encryptionKey, username);
    
    // Check each show folder
    for (const QString& folderName : showFolders) {
        QString folderPath = showsDir.absoluteFilePath(folderName);
        QDir showFolder(folderPath);
        
        // Find the first video file in this folder to read its metadata
        QStringList videoExtensions;
        videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
        
        showFolder.setNameFilters(videoExtensions);
        QStringList videoFiles = showFolder.entryList(QDir::Files);
        
        if (videoFiles.isEmpty()) {
            continue;
        }
        
        // Read metadata from the first video file
        QString firstVideoPath = showFolder.absoluteFilePath(videoFiles.first());
        VP_ShowsMetadata::ShowMetadata metadata;
        
        if (metadataManager.readMetadataFromFile(firstVideoPath, metadata)) {
            // Check if this show name matches (case-insensitive)
            if (metadata.showName.compare(showName, Qt::CaseInsensitive) == 0) {
                qDebug() << "VP_ShowsAddDialog: Found existing show:" << metadata.showName;
                qDebug() << "VP_ShowsAddDialog: Show folder path:" << folderPath;
                
                // Set flag to prevent recursive calls
                m_isCheckingExistingShow = true;
                
                // Update the show name field with the exact case from the existing show
                ui->lineEdit_ShowName->setText(metadata.showName);
                
                // Load the existing show's poster and description
                loadExistingShowData(folderPath, encryptionKey, username);
                
                // Load the existing show's settings and update checkboxes
                qDebug() << "VP_ShowsAddDialog: Loading show settings for existing show";
                loadShowSettings(folderPath, encryptionKey, username);
                
                // Reset flag
                m_isCheckingExistingShow = false;
                
                // Update the last checked name to the correctly cased version
                m_lastCheckedShowName = metadata.showName;
                
                // Clear TMDB data flag since this is existing show data, not TMDB data
                m_hasTMDBData = false;
                
                return; // Found a match, no need to continue
            }
        }
    }
    
    // No existing show found, check if we need to clear previously loaded data
    // Only clear if we had previously loaded data for a different show
    // Don't clear if we have TMDB data loaded
    if (!m_originalPoster.isNull() || m_originalDescription != "No description available.") {
        // If we have TMDB data loaded, don't clear it
        qDebug() << "VP_ShowsAddDialog: Checking m_hasTMDBData flag:" << m_hasTMDBData;
        if (m_hasTMDBData) {
            qDebug() << "VP_ShowsAddDialog: No existing show found, but keeping TMDB data";
            // Don't reset the flag here - let it persist until user manually edits
            return;
        }
        
        qDebug() << "VP_ShowsAddDialog: No existing show found, clearing previously loaded data";
        
        // Reset to default empty state
        ui->label_ShowPoster->setText("No Poster Available");
        ui->textBrowser_ShowDescription->clear();
        ui->textBrowser_ShowDescription->setPlainText("No description available.");
        
        // Reset the stored originals
        m_originalPoster = QPixmap();
        m_originalDescription = "No description available.";
        
        // Reset settings checkboxes to defaults since no existing show was found
        qDebug() << "VP_ShowsAddDialog: Resetting settings checkboxes to defaults";
        ui->checkBox_UseTMDB->setChecked(true);  // Default TMDB to true
        m_settingsLoaded = false;
    }
}
