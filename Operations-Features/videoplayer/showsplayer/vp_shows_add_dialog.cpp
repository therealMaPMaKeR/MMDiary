#include "vp_shows_add_dialog.h"
#include "ui_vp_shows_add_dialog.h"
#include "inputvalidation.h"
#include "vp_shows_config.h"
#include "operations_files.h"
#include "vp_shows_metadata.h"
#include "CryptoUtils.h"
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

VP_ShowsAddDialog::VP_ShowsAddDialog(const QString& folderName, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsAddDialog)
    , m_folderName(folderName)
    , m_suggestionsList(nullptr)
    , m_searchTimer(nullptr)
    , m_currentCacheSize(0)
    , m_isShowingSuggestions(false)
    , m_hoveredItemIndex(-1)
    , m_itemJustSelected(false)
    , m_isAddingToExistingShow(false)
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
    ui->textBrowser_ShowDescription->clear();  // Clear any default HTML
    ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
    
    // Connect signals
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &VP_ShowsAddDialog::on_buttonBox_accepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &VP_ShowsAddDialog::on_buttonBox_rejected);
    
    // Connect the UseTMDB checkbox
    connect(ui->checkBox_UseTMDB, &QCheckBox::toggled,
            this, &VP_ShowsAddDialog::onUseTMDBCheckboxToggled);
    
    // Initialize TMDB autofill functionality
    setupAutofillUI();
    
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
    
    delete ui;
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
    
    // Use InputValidation for proper validation
    InputValidation::ValidationResult result = InputValidation::validateInput(
        showName, InputValidation::InputType::DisplayName, 100);
    
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
    
    if (validateInputs()) {
        qDebug() << "VP_ShowsAddDialog: Inputs validated, accepting dialog";
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
    } else {
        ui->lineEdit_ShowName->setStyleSheet("");  // Reset to default style
        m_isAddingToExistingShow = false;
        
        // Reset to default text for new show
        ui->checkBox_UseTMDB->setText("Use TMDB for show information");
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
}

void VP_ShowsAddDialog::loadExistingShowData(const QString& showPath, const QByteArray& encryptionKey, const QString& username)
{
    qDebug() << "VP_ShowsAddDialog: Loading existing show data from:" << showPath;
    
    // Get the obfuscated folder name
    QDir showDir(showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Load show description from showdesc_[obfuscated] file
    QString descriptionFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descriptionFilePath = showDir.absoluteFilePath(descriptionFileName);
    
    if (QFile::exists(descriptionFilePath)) {
        // Read the encrypted description file
        QFile file(descriptionFilePath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray encryptedData = file.readAll();
            file.close();
            
            // Decrypt the description
            QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(encryptionKey, encryptedData);
            
            if (!decryptedData.isEmpty()) {
                QString description = QString::fromUtf8(decryptedData);
                // Clear any existing HTML content first
                ui->textBrowser_ShowDescription->clear();
                ui->textBrowser_ShowDescription->setPlainText(description);
                m_originalDescription = description;
                qDebug() << "VP_ShowsAddDialog: Loaded and displayed show description";
                qDebug() << "VP_ShowsAddDialog: Description preview:" << description.left(100);
            } else {
                qDebug() << "VP_ShowsAddDialog: Failed to decrypt description data";
                m_originalDescription = "No description available.";
                ui->textBrowser_ShowDescription->clear();
                ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
            }
        } else {
            qDebug() << "VP_ShowsAddDialog: Failed to open description file";
            m_originalDescription = "No description available.";
            ui->textBrowser_ShowDescription->clear();
            ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
        }
    } else {
        qDebug() << "VP_ShowsAddDialog: No description file found";
        m_originalDescription = "No description available.";
        ui->textBrowser_ShowDescription->clear();
        ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
    }
    
    // Load show poster from showimage_[obfuscated] file
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    if (QFile::exists(imageFilePath)) {
        // Read the encrypted image file
        QFile file(imageFilePath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray encryptedData = file.readAll();
            file.close();
            
            // Decrypt the image data
            QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(encryptionKey, encryptedData);
            
            if (!decryptedData.isEmpty()) {
                // Load the decrypted image into a pixmap
                QPixmap poster;
                if (poster.loadFromData(decryptedData)) {
                    // Scale to fit the label
                    QSize labelSize = ui->label_ShowPoster->size();
                    m_originalPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    ui->label_ShowPoster->setPixmap(m_originalPoster);
                    qDebug() << "VP_ShowsAddDialog: Loaded and displayed show poster";
                } else {
                    qDebug() << "VP_ShowsAddDialog: Failed to load poster from decrypted data";
                    m_originalPoster = QPixmap();
                    ui->label_ShowPoster->setText("No Poster Available");
                }
            } else {
                qDebug() << "VP_ShowsAddDialog: Failed to decrypt poster data";
                m_originalPoster = QPixmap();
                ui->label_ShowPoster->setText("No Poster Available");
            }
        } else {
            qDebug() << "VP_ShowsAddDialog: Failed to open poster file";
            m_originalPoster = QPixmap();
            ui->label_ShowPoster->setText("No Poster Available");
        }
    } else {
        qDebug() << "VP_ShowsAddDialog: No poster file found";
        m_originalPoster = QPixmap();
        ui->label_ShowPoster->setText("No Poster Available");
    }
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
    
    // Get TMDB API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsAddDialog: No TMDB API key configured, skipping autofill setup";
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
    
    // Create suggestions list widget
    m_suggestionsList = new QListWidget(this);
    m_suggestionsList->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    m_suggestionsList->setFocusPolicy(Qt::NoFocus);
    m_suggestionsList->setMouseTracking(true);
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
    
    if (!checked) {
        // Clear any existing suggestions when TMDB is disabled
        if (m_isShowingSuggestions) {
            clearSuggestions();
            hideSuggestions(false);
        }
        
        // Stop any pending search
        if (m_searchTimer && m_searchTimer->isActive()) {
            m_searchTimer->stop();
        }
        
        // When adding to existing show, restore the original show poster/description
        // When adding new show, clear the display
        if (m_isAddingToExistingShow) {
            // Restore original show poster and description
            if (!m_originalPoster.isNull()) {
                ui->label_ShowPoster->setPixmap(m_originalPoster);
            } else {
                ui->label_ShowPoster->setText("No Poster Available");
            }
            ui->textBrowser_ShowDescription->clear();  // Clear any HTML
            ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
        } else {
            // Clear poster and description for new show
            ui->label_ShowPoster->setText("No Poster Available");
            ui->textBrowser_ShowDescription->clear();  // Clear any HTML
            ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
        }
        
        qDebug() << "VP_ShowsAddDialog: TMDB disabled - cleared suggestions and reset display";
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
    
    performTMDBSearch(m_currentSearchText);
}

void VP_ShowsAddDialog::performTMDBSearch(const QString& searchText)
{
    if (!m_tmdbApi) {
        qDebug() << "VP_ShowsAddDialog: TMDB API not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: Performing TMDB search for:" << searchText;
    
    // Clear previous suggestions before starting new search
    m_currentSuggestions.clear();
    clearSuggestions();
    
    // Search for TV shows and get multiple results
    m_currentSuggestions = m_tmdbApi->searchTVShows(searchText, MAX_SUGGESTIONS);
    
    qDebug() << "VP_ShowsAddDialog: Search returned" << m_currentSuggestions.size() << "results";
    
    if (!m_currentSuggestions.isEmpty()) {
        displaySuggestions(m_currentSuggestions);
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
    
    // Add items to the list
    for (const VP_ShowsTMDB::ShowInfo& show : shows) {
        QString displayText = show.showName;
        if (!show.firstAirDate.isEmpty() && show.firstAirDate.length() >= 4) {
            displayText += QString(" (%1)").arg(show.firstAirDate.left(4));
        }
        
        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, show.tmdbId);
        m_suggestionsList->addItem(item);
    }
    
    // Position and show the suggestions list
    positionSuggestionsList();
    m_suggestionsList->show();
    m_isShowingSuggestions = true;
    
    // Automatically show the first suggestion's preview
    if (!m_currentSuggestions.isEmpty()) {
        displayShowInfo(m_currentSuggestions.first());
        m_hoveredItemIndex = 0;
    }
    
    qDebug() << "VP_ShowsAddDialog: Suggestions list shown with" << m_suggestionsList->count() << "items";
}

void VP_ShowsAddDialog::clearSuggestions()
{
    if (m_suggestionsList) {
        m_suggestionsList->clear();
    }
}

void VP_ShowsAddDialog::hideSuggestions(bool itemWasSelected)
{
    qDebug() << "VP_ShowsAddDialog: hideSuggestions() called, itemWasSelected:" << itemWasSelected;
    
    // Clear the flags and hover index
    m_isShowingSuggestions = false;
    m_hoveredItemIndex = -1;
    
    if (m_suggestionsList) {
        // Clear selection before hiding
        m_suggestionsList->clearSelection();
        qDebug() << "VP_ShowsAddDialog: Hiding suggestions list";
        m_suggestionsList->hide();
    }
    
    // Only restore original poster and description if no item was selected
    if (!itemWasSelected) {
        // Restore original poster and description
        if (!m_originalPoster.isNull()) {
            ui->label_ShowPoster->setPixmap(m_originalPoster);
        } else {
            ui->label_ShowPoster->setText("No Poster Available");
        }
        ui->textBrowser_ShowDescription->clear();  // Clear any HTML
        ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
    }
    
    // Clear the current suggestions when hiding
    m_currentSuggestions.clear();
}

void VP_ShowsAddDialog::positionSuggestionsList()
{
    if (!m_suggestionsList || !ui->lineEdit_ShowName) {
        return;
    }
    
    // Get the position of the lineEdit in global coordinates
    QPoint globalPos = ui->lineEdit_ShowName->mapToGlobal(QPoint(0, ui->lineEdit_ShowName->height()));
    
    // Set the position of the suggestions list
    m_suggestionsList->move(globalPos);
    
    // Set the width to match the lineEdit
    m_suggestionsList->setFixedWidth(ui->lineEdit_ShowName->width());
    
    // Calculate height based on number of items (max 8 items visible)
    int itemHeight = 24;  // Approximate height per item
    int maxVisibleItems = qMin(m_suggestionsList->count(), 8);
    int listHeight = maxVisibleItems * itemHeight + 4;  // +4 for borders/padding
    m_suggestionsList->setFixedHeight(listHeight);
}

void VP_ShowsAddDialog::onSuggestionItemClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }
    
    qDebug() << "VP_ShowsAddDialog: Suggestion item clicked:" << item->text();
    
    // Get the TMDB ID from the item data
    int tmdbId = item->data(Qt::UserRole).toInt();
    
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
    
    // Display the show info
    displayShowInfo(selectedShow);
    
    // Set the selected show name in the line edit
    ui->lineEdit_ShowName->setText(selectedShow.showName);
    
    // Stop any pending searches
    if (m_searchTimer) {
        m_searchTimer->stop();
    }
    
    // Set flag to prevent restoration on mouse leave
    m_itemJustSelected = true;
    
    // Hide suggestions (pass true to indicate an item was selected)
    hideSuggestions(true);
}

void VP_ShowsAddDialog::displayShowInfo(const VP_ShowsTMDB::ShowInfo& showInfo)
{
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
    
    // Create a temporary file path in the user's temp directory
    QString tempFileName = QString("tmdb_poster_%1_%2.jpg")
                          .arg(QString::number(QDateTime::currentMSecsSinceEpoch()))
                          .arg(QRandomGenerator::global()->generate());
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
        }
        // Reset the flag after checking
        m_itemJustSelected = false;
        return false;
    }
    
    // Handle clicks outside the suggestions list to hide it
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint pos = mouseEvent->globalPos();
        
        if (m_suggestionsList && m_suggestionsList->isVisible()) {
            QRect suggestionsRect = m_suggestionsList->geometry();
            suggestionsRect.moveTo(m_suggestionsList->mapToGlobal(QPoint(0, 0)));
            
            // Also check if click is in the line edit
            QRect lineEditRect = ui->lineEdit_ShowName->rect();
            QWidget* parent = ui->lineEdit_ShowName->parentWidget();
            while (parent && parent != this) {
                lineEditRect.translate(parent->pos());
                parent = parent->parentWidget();
            }
            
            if (!suggestionsRect.contains(pos) && !lineEditRect.contains(pos)) {
                qDebug() << "VP_ShowsAddDialog: Click outside suggestions";
                hideSuggestions(false);  // false = restore original values
            }
        }
        return false;
    }
    
    return QDialog::eventFilter(obj, event);
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
                }
            }
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
                }
            }
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
            event->accept();
            return;
        }
    }
    
    // Pass to base class for default handling
    QDialog::keyPressEvent(event);
}
