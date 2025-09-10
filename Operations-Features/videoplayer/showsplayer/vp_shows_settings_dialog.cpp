#include "vp_shows_settings_dialog.h"
#include "ui_vp_shows_settings_dialog.h"
#include "vp_shows_config.h"
#include "vp_shows_metadata.h"
#include "vp_shows_settings.h"
#include "vp_shows_watchhistory.h"
#include "vp_shows_progressdialogs.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include "CryptoUtils.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QImageReader>
#include "mainwindow.h"
#include <QDebug>
#include <QListWidgetItem>
#include <QListView>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTemporaryFile>
#include <QTimer>
#include <QFont>
#include <QBrush>
#include <QPalette>
#include <QMouseEvent>
#include <QCursor>
#include <QColor>
#include <QRect>
#include <QRandomGenerator>
#include <QBuffer>
#include <QThread>
#include <QApplication>
#include <QUuid>

VP_ShowsSettingsDialog::VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsSettingsDialog)
    , m_showName(showName)  // This is likely the obfuscated name initially
    , m_showPath(showPath)
    , m_suggestionsList(nullptr)
    , m_searchTimer(nullptr)
    , m_tmdbApi(nullptr)
    , m_networkManager(nullptr)
    , m_currentCacheSize(0)
    , m_isShowingSuggestions(false)
    , m_hoveredItemIndex(-1)
    , m_itemJustSelected(false)
    , m_tmdbDataWasUpdated(false)
    , m_displayFileNamesChanged(false)
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsSettingsDialog: Created dialog for obfuscated show name:" << showName;
    qDebug() << "VP_ShowsSettingsDialog: Show path:" << showPath;
    
    // Load the actual show name from video metadata
    QString actualShowName = loadActualShowName();
    if (!actualShowName.isEmpty()) {
        m_showName = actualShowName;
        m_originalShowName = actualShowName;
        qDebug() << "VP_ShowsSettingsDialog: Loaded actual show name:" << m_showName;
    } else {
        // Fallback: try to decrypt the folder name if no video metadata found
        m_originalShowName = m_showName;
        qDebug() << "VP_ShowsSettingsDialog: Could not load show name from metadata, using:" << m_showName;
    }
    
    // Set window title with actual show name
    setWindowTitle(QString("Settings - %1").arg(m_showName));
    
    // Set initial show name in the line edit
    ui->lineEdit_ShowName->setText(m_showName);
    
    // Load and display original show poster and description
    loadAndDisplayOriginalShowData();
    
    // Initialize autofill functionality
    setupAutofillUI();
    
    // Load show-specific settings
    loadShowSettings();
    
    // Connect the UseTMDB checkbox to handle button states
    connect(ui->checkBox_UseTMDB, &QCheckBox::toggled,
            this, &VP_ShowsSettingsDialog::onUseTMDBCheckboxToggled);
    
    // Connect button click handlers
    connect(ui->pushButton_ResetWatchHistory, &QPushButton::clicked,
            this, &VP_ShowsSettingsDialog::onResetWatchHistoryClicked);
    connect(ui->pushButton_UseCustomPoster, &QPushButton::clicked,
            this, &VP_ShowsSettingsDialog::onUseCustomPosterClicked);
    connect(ui->pushButton_UseCustomDesc, &QPushButton::clicked,
            this, &VP_ShowsSettingsDialog::onUseCustomDescClicked);
    connect(ui->pushButton_ReacquireShowData, &QPushButton::clicked,
            this, &VP_ShowsSettingsDialog::onReacquireTMDBDataClicked);
    
    // Set initial button states based on checkbox state
    onUseTMDBCheckboxToggled(ui->checkBox_UseTMDB->isChecked());
}

VP_ShowsSettingsDialog::~VP_ShowsSettingsDialog()
{
    qDebug() << "VP_ShowsSettingsDialog: Destructor called";
    
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

void VP_ShowsSettingsDialog::setupAutofillUI()
{
    qDebug() << "VP_ShowsSettingsDialog: Setting up autofill UI";
    
    // Check if TMDB is enabled
    if (!VP_ShowsConfig::isTMDBEnabled()) {
        qDebug() << "VP_ShowsSettingsDialog: TMDB integration is disabled, skipping autofill setup";
        return;
    }
    
    // Check if TMDB API key is available
    if (!VP_ShowsConfig::hasApiKey()) {
        qDebug() << "VP_ShowsSettingsDialog: No TMDB API key found, disabling TMDB integration";
        qDebug() << "VP_ShowsSettingsDialog: Copy tmdb_api_key_TEMPLATE.h to tmdb_api_key.h and add your API key";
        return;
    }
    
    // Get TMDB API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: TMDB API key file is empty or invalid, skipping autofill setup";
        return;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: TMDB API key found, length:" << apiKey.length();
    
    // Initialize TMDB API
    m_tmdbApi = std::make_unique<VP_ShowsTMDB>(this);
    m_tmdbApi->setApiKey(apiKey);
    
    // Initialize network manager for image downloads
    m_networkManager = std::make_unique<QNetworkAccessManager>(this);
    connect(m_networkManager.get(), &QNetworkAccessManager::finished,
            this, &VP_ShowsSettingsDialog::onImageDownloadFinished);
    
    // Create suggestions list widget as a child of the dialog
    // This ensures proper event handling and positioning
    m_suggestionsList = new QListWidget(this);  // Parent is the dialog
    m_suggestionsList->setWindowFlags(Qt::FramelessWindowHint);  // Simple frameless widget
    m_suggestionsList->setFocusPolicy(Qt::NoFocus);  // No focus to prevent auto-selection
    m_suggestionsList->setMouseTracking(true);
    m_suggestionsList->viewport()->setMouseTracking(true);  // Enable mouse tracking on viewport too
    m_suggestionsList->setSelectionMode(QAbstractItemView::SingleSelection);  // Use single selection
    m_suggestionsList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_suggestionsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_suggestionsList->setLayoutMode(QListView::SinglePass);  // Optimize layout
    m_suggestionsList->setUniformItemSizes(true);  // Use uniform sizes for better performance
    m_suggestionsList->setSpacing(0);  // No spacing between items
    m_suggestionsList->setContentsMargins(2, 2, 2, 2);  // Small margins for the list
    m_suggestionsList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);  // Adjust size to content
    
    // Ensure the widget accepts mouse events
    m_suggestionsList->setAttribute(Qt::WA_Hover, true);
    m_suggestionsList->viewport()->setAttribute(Qt::WA_Hover, true);
    m_suggestionsList->setEnabled(true);
    
    // Set a high z-order to ensure it appears on top
    m_suggestionsList->raise();
    
    // Force text to be visible with explicit styling and consistent item heights
    m_suggestionsList->setStyleSheet(
        "QListWidget { "
        "    background-color: white; "
        "    color: black; "
        "    border: 1px solid #888; "
        "    font-family: Arial; "
        "    font-size: 11px; "
        "    outline: none; "
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
    
    qDebug() << "VP_ShowsSettingsDialog: Suggestions list widget created";
    
    // Create search timer for debouncing
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SEARCH_DELAY_MS);
    connect(m_searchTimer, &QTimer::timeout, this, &VP_ShowsSettingsDialog::onSearchTimerTimeout);
    
    qDebug() << "VP_ShowsSettingsDialog: Search timer created with interval:" << SEARCH_DELAY_MS << "ms";
    qDebug() << "VP_ShowsSettingsDialog: Timer is single shot:" << m_searchTimer->isSingleShot();
    
    // Connect lineEdit signals
    connect(ui->lineEdit_ShowName, &QLineEdit::textChanged,
            this, &VP_ShowsSettingsDialog::onShowNameTextChanged);
    
    // Connect suggestions list signals
    connect(m_suggestionsList, &QListWidget::itemClicked,
            this, &VP_ShowsSettingsDialog::onSuggestionItemClicked);
    
    // Note: We handle hover through viewport's mouse move events in eventFilter
    // instead of using itemEntered which is unreliable
    
    // Install event filter on the dialog itself for click outside detection
    this->installEventFilter(this);
    
    // Install event filter on suggestions list and its viewport for mouse tracking
    m_suggestionsList->installEventFilter(this);
    m_suggestionsList->viewport()->installEventFilter(this);
    
    qDebug() << "VP_ShowsSettingsDialog: Autofill UI setup complete";
}

void VP_ShowsSettingsDialog::onShowNameTextChanged(const QString& text)
{
    qDebug() << "VP_ShowsSettingsDialog: onShowNameTextChanged called with text:" << text;
    
    // Check if the text has changed from a previously selected show
    // If so, reset the selected show ID since the user is typing something new
    if (m_selectedShowId > 0) {
        // Check if we have a show in our suggestions with this ID
        bool textMatchesSelection = false;
        for (const auto& show : m_currentSuggestions) {
            if (show.tmdbId == m_selectedShowId && show.showName == text.trimmed()) {
                textMatchesSelection = true;
                break;
            }
        }
        
        if (!textMatchesSelection) {
            qDebug() << "VP_ShowsSettingsDialog: Text changed from selected show, resetting show ID";
            m_selectedShowId = 0;  // Reset since user is typing something different
        }
    }
    
    // Check if UseTMDB checkbox is checked
    if (!ui->checkBox_UseTMDB->isChecked()) {
        qDebug() << "VP_ShowsSettingsDialog: UseTMDB checkbox is unchecked, not searching";
        // Clear any existing suggestions
        if (m_isShowingSuggestions) {
            clearSuggestions();
            hideSuggestions(false);
        }
        return;
    }
    
    // Check if TMDB API is initialized
    if (!m_tmdbApi) {
        qDebug() << "VP_ShowsSettingsDialog: TMDB API not initialized, cannot search";
        return;
    }
    
    // Validate input
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(text, InputValidation::InputType::PlainText, 100);
    
    if (!result.isValid) {
        qDebug() << "VP_ShowsSettingsDialog: Invalid input detected:" << result.errorMessage;
        return;
    }
    
    // Clear current suggestions if text is too short
    if (text.trimmed().length() < 2) {
        qDebug() << "VP_ShowsSettingsDialog: Text too short (< 2 chars), clearing suggestions";
        if (m_isShowingSuggestions) {
            clearSuggestions();
            hideSuggestions(false);  // false = restore original values
        }
        return;
    }
    
    // Store current search text
    m_currentSearchText = text.trimmed();
    
    // Reset and start the search timer (debouncing)
    if (m_searchTimer) {
        m_searchTimer->stop();
        m_searchTimer->start();
        qDebug() << "VP_ShowsSettingsDialog: Text changed, starting search timer for:" << m_currentSearchText;
        qDebug() << "VP_ShowsSettingsDialog: Timer interval:" << m_searchTimer->interval() << "ms";
        qDebug() << "VP_ShowsSettingsDialog: Timer is active:" << m_searchTimer->isActive();
    } else {
        qDebug() << "VP_ShowsSettingsDialog: ERROR - m_searchTimer is null!";
    }
}

void VP_ShowsSettingsDialog::onSearchTimerTimeout()
{
    qDebug() << "VP_ShowsSettingsDialog: Search timer timeout, performing search for:" << m_currentSearchText;
    
    if (m_currentSearchText.isEmpty() || m_currentSearchText.length() < 2) {
        return;
    }
    
    performTMDBSearch(m_currentSearchText);
}

void VP_ShowsSettingsDialog::performTMDBSearch(const QString& searchText)
{
    if (!m_tmdbApi) {
        qDebug() << "VP_ShowsSettingsDialog: TMDB API not initialized";
        return;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Performing TMDB search for:" << searchText;
    qDebug() << "VP_ShowsSettingsDialog: MAX_SUGGESTIONS value:" << MAX_SUGGESTIONS;
    
    // Clear previous suggestions before starting new search
    m_currentSuggestions.clear();
    clearSuggestions();  // Clear the list widget
    
    // Search for TV shows and get multiple results
    m_currentSuggestions = m_tmdbApi->searchTVShows(searchText, MAX_SUGGESTIONS);
    
    qDebug() << "VP_ShowsSettingsDialog: Search returned" << m_currentSuggestions.size() << "results";
    
    if (!m_currentSuggestions.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Found suggestions, displaying them";
        for (int i = 0; i < m_currentSuggestions.size(); ++i) {
            qDebug() << "VP_ShowsSettingsDialog:   Result" << i+1 << ":" 
                     << m_currentSuggestions[i].showName 
                     << "(ID:" << m_currentSuggestions[i].tmdbId << ")";
        }
        displaySuggestions(m_currentSuggestions);
    } else {
        qDebug() << "VP_ShowsSettingsDialog: No results found for:" << searchText;
        clearSuggestions();
        hideSuggestions(false);  // false = restore original values when no results
    }
}

void VP_ShowsSettingsDialog::displaySuggestions(const QList<VP_ShowsTMDB::ShowInfo>& shows)
{
    qDebug() << "VP_ShowsSettingsDialog: displaySuggestions called with" << shows.size() << "shows";
    
    if (!m_suggestionsList) {
        qDebug() << "VP_ShowsSettingsDialog: ERROR - m_suggestionsList is null!";
        return;
    }
    
    // Clear the list widget items (but not m_currentSuggestions since shows is a reference to it)
    m_suggestionsList->clear();
    
    if (shows.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Shows list is empty, hiding suggestions";
        hideSuggestions(false);  // false = restore original values when empty
        return;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Adding suggestions to list widget";
    
    // Add suggestions to the list
    for (int i = 0; i < shows.size(); ++i) {
        const auto& show = shows[i];
        QString displayText = show.showName;
        if (!show.firstAirDate.isEmpty()) {
            // Extract year from date (format: YYYY-MM-DD)
            QString year = show.firstAirDate.left(4);
            displayText += QString(" (%1)").arg(year);
        }
        
        qDebug() << "VP_ShowsSettingsDialog: Adding item" << i+1 << ":" << displayText;
        
        // Simple approach - just add the text
        m_suggestionsList->addItem(displayText);
        
        // Get the item we just added and set its data
        QListWidgetItem* item = m_suggestionsList->item(m_suggestionsList->count() - 1);
        if (item) {
            item->setData(Qt::UserRole, QVariant::fromValue(show.tmdbId));
            item->setData(Qt::UserRole + 1, show.showName);
            item->setData(Qt::UserRole + 2, show.overview);
            item->setData(Qt::UserRole + 3, show.posterPath);
        }
    }
    
    qDebug() << "VP_ShowsSettingsDialog: List widget now has" << m_suggestionsList->count() << "items";
    
    // Debug: Verify items have text
    for (int i = 0; i < m_suggestionsList->count() && i < 3; ++i) {
        QListWidgetItem* debugItem = m_suggestionsList->item(i);
        if (debugItem) {
            qDebug() << "VP_ShowsSettingsDialog: Item" << i << "text:" << debugItem->text();
        }
    }
    
    // Force update the list widget
    m_suggestionsList->update();
    m_suggestionsList->repaint();
    
    // Position and show the suggestions list
    positionSuggestionsList();
    
    qDebug() << "VP_ShowsSettingsDialog: Showing suggestions list";
    qDebug() << "VP_ShowsSettingsDialog: List widget visible before show():" << m_suggestionsList->isVisible();
    
    // Set flag to indicate we're showing suggestions
    m_isShowingSuggestions = true;
    
    // Show the list immediately
    m_suggestionsList->show();
    m_suggestionsList->raise();  // Ensure it's on top of other widgets
    
    // Reset hover index and clear selection when showing new suggestions
    m_hoveredItemIndex = -1;
    m_itemJustSelected = false;  // Clear the selection flag
    m_suggestionsList->clearSelection();
    m_suggestionsList->setCurrentItem(nullptr);
    
    // Ensure viewport is properly set up
    m_suggestionsList->viewport()->update();
    
    // Force a repaint to ensure visibility
    m_suggestionsList->update();
    m_suggestionsList->repaint();
    
    // Ensure mouse tracking is enabled on the viewport after showing
    m_suggestionsList->viewport()->setMouseTracking(true);
    
    // Re-install event filter on viewport in case it was recreated
    m_suggestionsList->viewport()->installEventFilter(this);
    
    // Debug window properties
    qDebug() << "VP_ShowsSettingsDialog: After show - visible:" << m_suggestionsList->isVisible();
    qDebug() << "VP_ShowsSettingsDialog: Is active window:" << m_suggestionsList->isActiveWindow();
    qDebug() << "VP_ShowsSettingsDialog: Window geometry:" << m_suggestionsList->geometry();
    qDebug() << "VP_ShowsSettingsDialog: Window flags:" << m_suggestionsList->windowFlags();
    qDebug() << "VP_ShowsSettingsDialog: Mouse tracking enabled:" << m_suggestionsList->hasMouseTracking();
    qDebug() << "VP_ShowsSettingsDialog: Widget is enabled:" << m_suggestionsList->isEnabled();
    qDebug() << "VP_ShowsSettingsDialog: Item 0 text:" << (m_suggestionsList->count() > 0 ? m_suggestionsList->item(0)->text() : "No items");
}

void VP_ShowsSettingsDialog::clearSuggestions()
{
    if (m_suggestionsList) {
        m_suggestionsList->clear();
    }
    // Don't clear m_currentSuggestions here as it may be passed by reference to displaySuggestions
    // m_currentSuggestions.clear();  // REMOVED - this was causing the bug
}

void VP_ShowsSettingsDialog::hideSuggestions(bool itemWasSelected)
{
    qDebug() << "VP_ShowsSettingsDialog: hideSuggestions() called, itemWasSelected:" << itemWasSelected;
    
    // Clear the flags and hover index
    m_isShowingSuggestions = false;
    m_hoveredItemIndex = -1;
    // Note: We don't clear m_itemJustSelected here because it needs to be checked in the mouse leave event
    
    if (m_suggestionsList) {
        // Clear selection before hiding
        m_suggestionsList->clearSelection();
        qDebug() << "VP_ShowsSettingsDialog: Hiding suggestions list";
        m_suggestionsList->hide();
    }
    
    // Only restore original poster and description if no item was selected
    // Note: We don't restore the show name field - user can type freely
    if (!itemWasSelected) {
        // Restore original poster and description
        if (!m_originalPoster.isNull()) {
            ui->label_ShowPoster->setPixmap(m_originalPoster);
        } else {
            ui->label_ShowPoster->setText("No Poster Available");
        }
        ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
    }
    // If an item was selected, keep the selected item's poster and description
    
    // Clear the current suggestions when hiding
    m_currentSuggestions.clear();
}

void VP_ShowsSettingsDialog::positionSuggestionsList()
{
    if (!m_suggestionsList || !ui->lineEdit_ShowName) {
        qDebug() << "VP_ShowsSettingsDialog: Cannot position list - null pointers";
        return;
    }
    
    // Get the position relative to the dialog (since list is now a child)
    QPoint lineEditPos = ui->lineEdit_ShowName->pos();
    // Account for any parent widgets between the line edit and the dialog
    QWidget* parent = ui->lineEdit_ShowName->parentWidget();
    while (parent && parent != this) {
        lineEditPos += parent->pos();
        parent = parent->parentWidget();
    }
    
    // Position below the line edit
    int x = lineEditPos.x();
    int y = lineEditPos.y() + ui->lineEdit_ShowName->height();
    
    qDebug() << "VP_ShowsSettingsDialog: Positioning suggestions list at:" << QPoint(x, y);
    
    // Set the position of the suggestions list
    m_suggestionsList->move(x, y);
    
    // Set the width to match the line edit
    int width = ui->lineEdit_ShowName->width();
    m_suggestionsList->setFixedWidth(width);
    
    qDebug() << "VP_ShowsSettingsDialog: Setting list width to:" << width;
    
    // Set maximum height based on actual item heights
    int itemCount = m_suggestionsList->count();
    int height = 0;
    
    // Calculate actual height needed for visible items
    int visibleItems = qMin(itemCount, MAX_SUGGESTIONS);
    for (int i = 0; i < visibleItems; ++i) {
        QListWidgetItem* item = m_suggestionsList->item(i);
        if (item) {
            // Get the actual size hint for this row
            height += m_suggestionsList->sizeHintForRow(i);
        }
    }
    
    // Add small padding for borders and margins (reduced from 10 to 4)
    height += 4;
    
    // Ensure minimum height if we have items
    if (visibleItems > 0 && height < 20) {
        height = visibleItems * 20; // Fallback to minimum reasonable height
    }
    
    m_suggestionsList->setFixedHeight(height);
    
    qDebug() << "VP_ShowsSettingsDialog: Setting list height to:" << height << "(for" << itemCount << "items)";
    qDebug() << "VP_ShowsSettingsDialog: Final geometry:" << m_suggestionsList->geometry();
    
    // Debug item dimensions
    if (itemCount > 0) {
        QListWidgetItem* firstItem = m_suggestionsList->item(0);
        if (firstItem) {
            QRect itemRect = m_suggestionsList->visualItemRect(firstItem);
            qDebug() << "VP_ShowsSettingsDialog: First item rect:" << itemRect;
            qDebug() << "VP_ShowsSettingsDialog: Viewport size:" << m_suggestionsList->viewport()->size();
        }
    }
}

void VP_ShowsSettingsDialog::onSuggestionItemHovered()
{
    // Get the current item (which is set when hovering)
    QListWidgetItem* item = m_suggestionsList->currentItem();
    if (!item) {
        qDebug() << "VP_ShowsSettingsDialog: No current item to display";
        return;
    }
    
    // Get show data from item
    QString showName = item->data(Qt::UserRole + 1).toString();
    QString overview = item->data(Qt::UserRole + 2).toString();
    QString posterPath = item->data(Qt::UserRole + 3).toString();
    
    qDebug() << "VP_ShowsSettingsDialog: Hovering over:" << showName;
    
    // Display show description
    if (!overview.isEmpty()) {
        ui->textBrowser_ShowDescription->setPlainText(overview);
    } else {
        ui->textBrowser_ShowDescription->setPlainText("No description available.");
    }
    
    // Download and display poster if available
    if (!posterPath.isEmpty()) {
        downloadAndDisplayPoster(posterPath);
    } else {
        ui->label_ShowPoster->clear();
        ui->label_ShowPoster->setText("No Poster Available");
    }
}

void VP_ShowsSettingsDialog::onSuggestionItemClicked(QListWidgetItem* item)
{
    if (!item) {
        qDebug() << "VP_ShowsSettingsDialog: onSuggestionItemClicked - null item";
        return;
    }
    
    // Extract the TMDB ID from the item data
    int tmdbId = item->data(Qt::UserRole).toInt();
    QString showName = item->data(Qt::UserRole + 1).toString();
    QString overview = item->data(Qt::UserRole + 2).toString();
    QString posterPath = item->data(Qt::UserRole + 3).toString();
    
    qDebug() << "VP_ShowsSettingsDialog: Selected show:" << showName << "with TMDB ID:" << tmdbId;
    
    // Store the selected show's TMDB ID
    m_selectedShowId = tmdbId;
    qDebug() << "VP_ShowsSettingsDialog: Stored selected show ID:" << m_selectedShowId;
    
    // Update the UI with the selected show info
    if (!overview.isEmpty()) {
        ui->textBrowser_ShowDescription->setPlainText(overview);
    } else {
        ui->textBrowser_ShowDescription->setPlainText("No description available.");
    }
    
    // Download and display poster if available
    if (!posterPath.isEmpty()) {
        downloadAndDisplayPoster(posterPath);
    }
    
    // Set the selected show name in the line edit
    ui->lineEdit_ShowName->setText(showName);
    
    // Stop any pending searches
    if (m_searchTimer) {
        m_searchTimer->stop();
    }
    
    // Set flag to prevent restoration on mouse leave
    m_itemJustSelected = true;
    
    // Hide suggestions (pass true to indicate an item was selected)
    hideSuggestions(true);
}

void VP_ShowsSettingsDialog::onUseTMDBCheckboxToggled(bool checked)
{
    qDebug() << "VP_ShowsSettingsDialog: UseTMDB checkbox toggled to:" << checked;
    
    // Define styles for enabled and disabled states
    // Using opacity to make disabled buttons appear faded without changing dimensions
    QString enabledStyle = "";
    QString disabledStyle = "QPushButton { "
                           "    color: rgba(255, 255, 255, 0.4); "
                           "    background-color: rgba(60, 60, 60, 0.3); "
                           "}";
    
    // Update button states based on checkbox state
    if (checked) {
        // When TMDB is enabled:
        // - Disable custom poster and description buttons
        // - Enable re-acquire show data button
        ui->pushButton_UseCustomPoster->setEnabled(false);
        ui->pushButton_UseCustomDesc->setEnabled(false);
        ui->pushButton_ReacquireShowData->setEnabled(true);
        
        // Apply styles
        ui->pushButton_UseCustomPoster->setStyleSheet(disabledStyle);
        ui->pushButton_UseCustomDesc->setStyleSheet(disabledStyle);
        ui->pushButton_ReacquireShowData->setStyleSheet(enabledStyle);
        
        qDebug() << "VP_ShowsSettingsDialog: TMDB enabled - disabled custom buttons, enabled re-acquire button";
    } else {
        // When TMDB is disabled:
        // - Enable custom poster and description buttons
        // - Disable re-acquire show data button
        // - Clear any selected TMDB show ID since TMDB is disabled
        ui->pushButton_UseCustomPoster->setEnabled(true);
        ui->pushButton_UseCustomDesc->setEnabled(true);
        ui->pushButton_ReacquireShowData->setEnabled(false);
        
        // Apply styles
        ui->pushButton_UseCustomPoster->setStyleSheet(enabledStyle);
        ui->pushButton_UseCustomDesc->setStyleSheet(enabledStyle);
        ui->pushButton_ReacquireShowData->setStyleSheet(disabledStyle);
        
        // Clear any existing suggestions
        if (m_isShowingSuggestions) {
            clearSuggestions();
            hideSuggestions(false);
        }
        
        // Stop any pending search
        if (m_searchTimer && m_searchTimer->isActive()) {
            m_searchTimer->stop();
        }
        
        // Clear the selected show ID when TMDB is disabled
        if (m_selectedShowId > 0) {
            qDebug() << "VP_ShowsSettingsDialog: Clearing selected show ID since TMDB is disabled";
            m_selectedShowId = 0;
        }
        
        qDebug() << "VP_ShowsSettingsDialog: TMDB disabled - enabled custom buttons, disabled re-acquire button";
    }
}

void VP_ShowsSettingsDialog::downloadAndDisplayPoster(const QString& posterPath)
{
    if (posterPath.isEmpty() || !m_tmdbApi) {
        qDebug() << "VP_ShowsSettingsDialog: Cannot download poster - empty path or no TMDB API";
        return;
    }
    
    // Get the target size from the label
    QSize labelSize = ui->label_ShowPoster->size();
    qDebug() << "VP_ShowsSettingsDialog: Label size for poster:" << labelSize;
    
    // Check if we already have this poster in cache
    if (m_posterCache.contains(posterPath)) {
        qDebug() << "VP_ShowsSettingsDialog: Using cached poster for:" << posterPath;
        
        // Update access order for LRU
        m_cacheAccessOrder.removeAll(posterPath);
        m_cacheAccessOrder.append(posterPath);
        
        // Display the pre-scaled poster from cache
        ui->label_ShowPoster->setPixmap(m_posterCache[posterPath].scaledPixmap);
        return;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Poster not in cache, downloading:" << posterPath;
    
    // Get the user's temp directory using VP_ShowsConfig
    QString username = OperationsFiles::getUsername();
    if (username.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Cannot get username for temp directory";
        ui->label_ShowPoster->setText("Failed to Get User");
        return;
    }
    
    QString tempDir = VP_ShowsConfig::getTempDirectory(username);
    if (tempDir.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to get temp directory";
        ui->label_ShowPoster->setText("No Temp Directory");
        return;
    }
    
    // Create a temporary file path in the user's temp directory using QUuid for unpredictability
    QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString tempFileName = QString("tmdb_poster_%1.jpg").arg(uniqueId);
    QString tempFilePath = QDir(tempDir).absoluteFilePath(tempFileName);
    
    qDebug() << "VP_ShowsSettingsDialog: Downloading poster to temp file:" << tempFilePath;
    
    // Download the poster
    bool success = m_tmdbApi->downloadImage(posterPath, tempFilePath, true); // true = isPoster
    
    if (success && QFile::exists(tempFilePath)) {
        qDebug() << "VP_ShowsSettingsDialog: Successfully downloaded poster to:" << tempFilePath;
        
        // Load the downloaded image
        QPixmap poster(tempFilePath);
        if (!poster.isNull()) {
            qDebug() << "VP_ShowsSettingsDialog: Loaded poster, original size:" << poster.size();
            
            // Scale to fit the label and cache the scaled version
            QPixmap scaledPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            qDebug() << "VP_ShowsSettingsDialog: Scaled poster to:" << scaledPoster.size();
            
            // Add to cache (this will handle size limits)
            addToCache(posterPath, scaledPoster);
            
            // Display the scaled poster
            ui->label_ShowPoster->setPixmap(scaledPoster);
            
            // Update the original poster reference if this is from a TMDB selection
            // This ensures the poster persists when hovering over other suggestions
            if (m_selectedShowId > 0 || m_tmdbDataWasUpdated) {
                m_originalPoster = scaledPoster;
                qDebug() << "VP_ShowsSettingsDialog: Updated original poster reference";
            }
            
            // Clean up the temp file using secure delete from operations_files
            // Use 1 pass for temp files and allow external files since it's in Data/username/temp
            if (!OperationsFiles::secureDelete(tempFilePath, 1, false)) {
                qDebug() << "VP_ShowsSettingsDialog: Failed to securely delete temp file:" << tempFilePath;
                // Try regular delete as fallback
                QFile::remove(tempFilePath);
            } else {
                qDebug() << "VP_ShowsSettingsDialog: Securely deleted temp file:" << tempFilePath;
            }
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to load poster image from:" << tempFilePath;
            ui->label_ShowPoster->setText("Failed to Load");
            
            // Clean up the temp file
            OperationsFiles::secureDelete(tempFilePath, 1, false);
        }
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Failed to download poster";
        ui->label_ShowPoster->setText("Download Failed");
        
        // Clean up any partial temp file
        if (QFile::exists(tempFilePath)) {
            OperationsFiles::secureDelete(tempFilePath, 1, false);
        }
    }
}

void VP_ShowsSettingsDialog::addToCache(const QString& posterPath, const QPixmap& scaledPixmap)
{
    if (posterPath.isEmpty() || scaledPixmap.isNull()) {
        return;
    }
    
    // Estimate the size of the pixmap
    qint64 pixmapSize = estimatePixmapSize(scaledPixmap);
    
    qDebug() << "VP_ShowsSettingsDialog: Adding poster to cache:" << posterPath 
             << "Size:" << pixmapSize << "bytes";
    
    // Create cache entry
    CachedPoster cachedPoster;
    cachedPoster.scaledPixmap = scaledPixmap;
    cachedPoster.posterPath = posterPath;
    cachedPoster.sizeInBytes = pixmapSize;
    
    // Add to cache
    m_posterCache[posterPath] = cachedPoster;
    m_currentCacheSize += pixmapSize;
    
    // Update access order
    m_cacheAccessOrder.removeAll(posterPath);
    m_cacheAccessOrder.append(posterPath);
    
    // Enforce cache limits
    enforeCacheLimits();
    
    qDebug() << "VP_ShowsSettingsDialog: Cache now contains" << m_posterCache.size() 
             << "items, total size:" << m_currentCacheSize << "bytes";
}

void VP_ShowsSettingsDialog::enforeCacheLimits()
{
    // Remove items if we exceed the item count limit or size limit
    while ((m_posterCache.size() > MAX_CACHE_ITEMS || m_currentCacheSize > MAX_CACHE_SIZE) 
           && !m_cacheAccessOrder.isEmpty()) {
        
        // Remove the least recently used item (first in the list)
        QString oldestPath = m_cacheAccessOrder.takeFirst();
        
        if (m_posterCache.contains(oldestPath)) {
            qint64 removedSize = m_posterCache[oldestPath].sizeInBytes;
            m_currentCacheSize -= removedSize;
            m_posterCache.remove(oldestPath);
            
            qDebug() << "VP_ShowsSettingsDialog: Removed from cache:" << oldestPath 
                     << "Freed:" << removedSize << "bytes";
        }
    }
}

qint64 VP_ShowsSettingsDialog::estimatePixmapSize(const QPixmap& pixmap)
{
    if (pixmap.isNull()) {
        return 0;
    }
    
    // Estimate: width * height * 4 bytes per pixel (RGBA)
    // This is an approximation as the actual memory usage may vary
    return static_cast<qint64>(pixmap.width()) * pixmap.height() * 4;
}

void VP_ShowsSettingsDialog::onReacquireTMDBDataClicked()
{
    qDebug() << "VP_ShowsSettingsDialog: Re-acquire TMDB data button clicked";
    
    // Check if TMDB is enabled
    if (!ui->checkBox_UseTMDB->isChecked()) {
        QMessageBox::information(this, tr("TMDB Disabled"),
                               tr("Please enable TMDB integration to re-acquire show data."));
        return;
    }
    
    // Check if TMDB API is initialized
    if (!m_tmdbApi) {
        QMessageBox::warning(this, tr("TMDB Not Available"),
                           tr("TMDB API is not initialized. Please check your API key."));
        return;
    }
    
    // Collect all video files from the show
    QList<VideoFileInfo> videoFiles = collectVideoFiles();
    
    if (videoFiles.isEmpty()) {
        QMessageBox::information(this, tr("No Videos Found"),
                               tr("No video files found in this show."));
        return;
    }
    
    // Get the show name from the line edit
    QString showName = ui->lineEdit_ShowName->text().trimmed();
    if (showName.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Show Name"),
                           tr("Please enter a valid show name."));
        return;
    }
    
    // Check if we're using a different show ID than what's saved
    QString confirmMessage;
    if (m_selectedShowId > 0) {
        // Check if it's different from the saved ID
        bool savedIdOk = false;
        int savedId = m_currentSettings.showId.toInt(&savedIdOk);
        if (!savedIdOk) savedId = 0;
        
        if (savedId != m_selectedShowId) {
            confirmMessage = tr("You have selected a different show from TMDB.\n\n"
                               "This will re-fetch metadata from TMDB for all %1 video files "
                               "using the newly selected show: %2\n\n"
                               "This operation may take several minutes due to API rate limits.\n\n"
                               "Do you want to continue?").arg(videoFiles.size()).arg(showName);
        } else {
            confirmMessage = tr("This will re-fetch metadata from TMDB for all %1 video files in this show.\n\n"
                               "This operation may take several minutes due to API rate limits.\n\n"
                               "Do you want to continue?").arg(videoFiles.size());
        }
    } else {
        confirmMessage = tr("This will re-fetch metadata from TMDB for all %1 video files in this show.\n\n"
                           "This operation may take several minutes due to API rate limits.\n\n"
                           "Do you want to continue?").arg(videoFiles.size());
    }
    
    // Confirm the operation
    int result = QMessageBox::question(this,
                                      tr("Re-acquire TMDB Data"),
                                      confirmMessage,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);
    
    if (result != QMessageBox::Yes) {
        return;
    }
    
    // Create progress dialog
    VP_ShowsTMDBReacquisitionDialog* progressDialog = new VP_ShowsTMDBReacquisitionDialog(this);
    progressDialog->setTotalEpisodes(videoFiles.size());
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAttribute(Qt::WA_DeleteOnClose, false);  // We'll delete it manually
    
    // Connect cancel signal
    bool operationCancelled = false;
    connect(progressDialog, &VP_ShowsTMDBReacquisitionDialog::cancelRequested,
            [&operationCancelled]() { operationCancelled = true; });
    
    // Show the progress dialog immediately
    progressDialog->show();
    progressDialog->raise();
    progressDialog->activateWindow();
    
    // First, try to get the show ID from current selection or settings
    VP_ShowsTMDB::ShowInfo showInfo;
    bool showInfoLoaded = false;
    int showIdToUse = 0;
    
    // Priority 1: Use the currently selected show ID if available (from dropdown selection)
    if (m_selectedShowId > 0) {
        showIdToUse = m_selectedShowId;
        qDebug() << "VP_ShowsSettingsDialog: Using currently selected show ID:" << showIdToUse;
    }
    // Priority 2: Use the show ID from saved settings
    else if (!m_currentSettings.showId.isEmpty() && m_currentSettings.showId != "error") {
        bool idOk = false;
        showIdToUse = m_currentSettings.showId.toInt(&idOk);
        if (!idOk || showIdToUse <= 0) {
            showIdToUse = 0;
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Using show ID from settings:" << showIdToUse;
        }
    }
    
    // Try to load show info using the ID if we have one
    if (showIdToUse > 0) {
        progressDialog->setStatusMessage(tr("Loading show information using ID: %1").arg(showIdToUse));
        QApplication::processEvents();
        
        if (m_tmdbApi->getShowById(showIdToUse, showInfo)) {
            showInfoLoaded = true;
            qDebug() << "VP_ShowsSettingsDialog: Successfully loaded show info using ID:" << showIdToUse;
            
            // Update the stored settings ID if we used the selected ID
            if (m_selectedShowId > 0 && m_selectedShowId != showIdToUse) {
                // This shouldn't happen but handle it just in case
                qDebug() << "VP_ShowsSettingsDialog: Warning - selected ID doesn't match used ID";
            }
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to load show info using ID:" << showIdToUse;
        }
    }
    
    // Fallback to searching by name if ID is not available or failed
    if (!showInfoLoaded) {
        progressDialog->setStatusMessage(tr("Searching for show: %1").arg(showName));
        QApplication::processEvents();
        
        if (!m_tmdbApi->searchTVShow(showName, showInfo)) {
            progressDialog->setStatusMessage(tr("Failed to find show on TMDB"));
            QMessageBox::warning(this, tr("Show Not Found"),
                               tr("Could not find '%1' on TMDB. Please check the show name.").arg(showName));
            delete progressDialog;
            return;
        }
        
        // Update the selected show ID and mark that we need to save it
        if (showInfo.tmdbId > 0) {
            m_selectedShowId = showInfo.tmdbId;
            qDebug() << "VP_ShowsSettingsDialog: Updated selected show ID:" << m_selectedShowId;
            // Note: We don't save settings here - that will happen when the user clicks OK
        }
    }
    
    // Check if operation was cancelled or app is closing
    if (operationCancelled || !progressDialog->isVisible()) {
        qDebug() << "VP_ShowsSettingsDialog: Operation cancelled or dialog closed during show search";
        delete progressDialog;
        return;
    }
    
    // Build episode map for the show
    progressDialog->setStatusMessage(tr("Building episode information map..."));
    QApplication::processEvents();
    
    QMap<int, VP_ShowsTMDB::EpisodeMapping> episodeMap = m_tmdbApi->buildEpisodeMap(showInfo.tmdbId);
    
    // Get parent MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        QMessageBox::critical(this, tr("Error"),
                            tr("Unable to access encryption key."));
        delete progressDialog;
        return;
    }
    
    // Process each video file
    int processedCount = 0;
    int successCount = 0;
    int failedCount = 0;
    int rateLimitRetries = 0;
    const int MAX_RATE_LIMIT_RETRIES = 60; // Max 60 seconds of retrying
    
    // Note: progressDialog->show() was already called above
    
    for (const VideoFileInfo& videoInfo : videoFiles) {
        // Check if dialog was closed (app closing) or operation cancelled
        if (operationCancelled || !progressDialog->isVisible()) {
            qDebug() << "VP_ShowsSettingsDialog: Operation cancelled by user or dialog closed";
            operationCancelled = true;
            break;
        }
        
        processedCount++;
        progressDialog->updateProgress(processedCount, videoInfo.episodeName);
        
        // Search for episode information
        VP_ShowsTMDB::EpisodeInfo episodeInfo;
        bool foundEpisode = false;
        
        // Try to get episode info with rate limit handling
        bool rateLimitHit = false;
        int retryCount = 0;
        
        do {
            rateLimitHit = false;
            
            // Try to get episode info
            if (m_tmdbApi->getEpisodeInfo(showInfo.tmdbId, videoInfo.season, videoInfo.episode, episodeInfo)) {
                foundEpisode = true;
                rateLimitRetries = 0; // Reset retry counter on success
                break;
            }
            
            // Check if we hit rate limit (we can detect this from the API response)
            // The VP_ShowsTMDB class logs rate limit errors
            // We'll assume rate limit if the call fails and retry
            if (!foundEpisode && retryCount < 5) {
                rateLimitHit = true;
                retryCount++;
                rateLimitRetries++;
                
                if (rateLimitRetries > MAX_RATE_LIMIT_RETRIES) {
                    progressDialog->setStatusMessage(tr("Too many rate limit retries. Aborting."));
                    operationCancelled = true;
                    break;
                }
                
                // Show rate limit message and wait
                progressDialog->showRateLimitMessage(1);
                QThread::sleep(1); // Wait 1 second before retry
                QApplication::processEvents();
                
                // Check if dialog was closed during wait
                if (!progressDialog->isVisible()) {
                    operationCancelled = true;
                    break;
                }
            }
        } while (rateLimitHit && !operationCancelled);
        
        if (operationCancelled) {
            break;
        }
        
        if (foundEpisode) {
            // Update the video metadata
            if (updateVideoMetadataWithTMDB(videoInfo, episodeInfo)) {
                successCount++;
                qDebug() << "VP_ShowsSettingsDialog: Successfully updated metadata for:" << videoInfo.episodeName;
            } else {
                failedCount++;
                qDebug() << "VP_ShowsSettingsDialog: Failed to update metadata for:" << videoInfo.episodeName;
            }
        } else {
            failedCount++;
            qDebug() << "VP_ShowsSettingsDialog: Could not find TMDB info for:" << videoInfo.episodeName;
        }
        
        // Add a small delay between requests to avoid hitting rate limits
        QThread::msleep(100); // 100ms delay between requests
        QApplication::processEvents();
        
        // Check if dialog was closed
        if (!progressDialog->isVisible()) {
            operationCancelled = true;
            break;
        }
    }
    
    // Check if operation was truly cancelled (not just completed)
    bool wasActuallyCancelled = operationCancelled && (processedCount < videoFiles.size());
    
    // Close progress dialog if it's still open
    if (progressDialog->isVisible()) {
        progressDialog->close();
    }
    delete progressDialog;
    
    // Show summary
    QString summary = tr("TMDB data re-acquisition completed.\n\n"
                        "Processed: %1 files\n"
                        "Successful: %2\n"
                        "Failed: %3")
                     .arg(processedCount)
                     .arg(successCount)
                     .arg(failedCount);
    
    if (wasActuallyCancelled) {
        summary += tr("\n\nOperation was cancelled by user.");
    }
    
    QMessageBox::information(this, tr("Re-acquisition Complete"), summary);
    
    // Set flag to indicate TMDB data was updated if any files were successfully processed
    if (successCount > 0) {
        m_tmdbDataWasUpdated = true;
        qDebug() << "VP_ShowsSettingsDialog: TMDB data was updated for" << successCount << "files";
        
        // If we used a newly selected show, update the UI to reflect the new show's data
        if (showInfoLoaded && showInfo.tmdbId > 0) {
            // Update the show description
            if (!showInfo.overview.isEmpty()) {
                ui->textBrowser_ShowDescription->setPlainText(showInfo.overview);
                m_originalDescription = showInfo.overview;  // Update the original too
            }
            
            // Update the show poster if available
            if (!showInfo.posterPath.isEmpty()) {
                downloadAndDisplayPoster(showInfo.posterPath);
                // Note: m_originalPoster will be updated in downloadAndDisplayPoster
            }
            
            qDebug() << "VP_ShowsSettingsDialog: Updated UI with new show information";
        }
    }
    
    qDebug() << "VP_ShowsSettingsDialog: TMDB reacquisition finished. Success:" << successCount << "Failed:" << failedCount;
}

QList<VP_ShowsSettingsDialog::VideoFileInfo> VP_ShowsSettingsDialog::collectVideoFiles()
{
    QList<VideoFileInfo> videoFiles;
    
    qDebug() << "VP_ShowsSettingsDialog: Collecting video files from:" << m_showPath;
    
    QDir showDir(m_showPath);
    if (!showDir.exists()) {
        qDebug() << "VP_ShowsSettingsDialog: Show directory does not exist";
        return videoFiles;
    }
    
    // Get all encrypted video files in the directory (using .mmvid extension)
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    
    showDir.setNameFilters(videoExtensions);
    QStringList files = showDir.entryList(QDir::Files);
    
    qDebug() << "VP_ShowsSettingsDialog: Found" << files.size() << ".mmvid files in directory";
    if (files.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Directory contents:" << showDir.entryList(QDir::Files);
    }
    
    // Get parent MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Cannot access MainWindow for encryption key";
        return videoFiles;
    }
    
    // Create VP_ShowsMetadata instance to read metadata
    VP_ShowsMetadata metadataReader(mainWindow->user_Key, mainWindow->user_Username);
    
    for (const QString& fileName : files) {
        QString filePath = showDir.absoluteFilePath(fileName);
        
        // Read metadata from the encrypted video file
        VP_ShowsMetadata::ShowMetadata metadata;
        if (metadataReader.readMetadataFromFile(filePath, metadata)) {
            VideoFileInfo info;
            info.filePath = filePath;
            info.relativePath = fileName;
            info.episodeName = metadata.EPName.isEmpty() ? fileName : metadata.EPName;
            
            // Parse season and episode from QString to int
            bool seasonOk = false, episodeOk = false;
            int seasonNum = metadata.season.toInt(&seasonOk);
            int episodeNum = metadata.episode.toInt(&episodeOk);
            
            info.season = seasonOk ? seasonNum : 0;
            info.episode = episodeOk ? episodeNum : 0;
            info.language = metadata.language;
            info.translation = metadata.translation;
            
            videoFiles.append(info);
            qDebug() << "VP_ShowsSettingsDialog: Found video:" << info.episodeName 
                     << "S" << info.season << "E" << info.episode;
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to read metadata from:" << fileName;
        }
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Collected" << videoFiles.size() << "video files";
    return videoFiles;
}

bool VP_ShowsSettingsDialog::updateVideoMetadataWithTMDB(const VideoFileInfo& videoInfo, const VP_ShowsTMDB::EpisodeInfo& episodeInfo)
{
    qDebug() << "VP_ShowsSettingsDialog: Updating metadata for:" << videoInfo.filePath;
    
    // Get parent MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Cannot access MainWindow for encryption key";
        return false;
    }
    
    // Create VP_ShowsMetadata instance
    VP_ShowsMetadata metadataManager(mainWindow->user_Key, mainWindow->user_Username);
    
    // Read current metadata
    VP_ShowsMetadata::ShowMetadata metadata;
    if (!metadataManager.readMetadataFromFile(videoInfo.filePath, metadata)) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to read current metadata";
        return false;
    }
    
    // Update metadata with TMDB info
    if (!episodeInfo.episodeName.isEmpty()) {
        metadata.EPName = episodeInfo.episodeName;
    }
    if (!episodeInfo.overview.isEmpty()) {
        metadata.EPDescription = episodeInfo.overview;
    }
    if (episodeInfo.seasonNumber > 0) {
        metadata.season = QString::number(episodeInfo.seasonNumber);
    }
    if (episodeInfo.episodeNumber > 0) {
        metadata.episode = QString::number(episodeInfo.episodeNumber);
    }
    if (!episodeInfo.airDate.isEmpty()) {
        metadata.airDate = episodeInfo.airDate;
    }
    
    // Download and save episode still image if available
    if (!episodeInfo.stillPath.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Episode has still image, downloading...";
        
        // Get temp directory for the user
        QString tempDir = VP_ShowsConfig::getTempDirectory(mainWindow->user_Username);
        if (!tempDir.isEmpty()) {
            // Create unique temp file path
            QString tempThumbPath = tempDir + "/temp_episode_thumb_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
            
            // Download the episode still image (false = not a poster, it's an episode still)
            if (m_tmdbApi && m_tmdbApi->downloadImage(episodeInfo.stillPath, tempThumbPath, false)) {
                QFile thumbFile(tempThumbPath);
                if (thumbFile.open(QIODevice::ReadOnly)) {
                    QByteArray thumbData = thumbFile.readAll();
                    thumbFile.close();
                    
                    // Scale to 128x128 for metadata storage
                    QByteArray scaledThumb = VP_ShowsTMDB::scaleImageToSize(thumbData, 128, 128);
                    
                    if (!scaledThumb.isEmpty() && scaledThumb.size() <= VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
                        metadata.EPImage = scaledThumb;
                        qDebug() << "VP_ShowsSettingsDialog: Added episode thumbnail (" 
                                << scaledThumb.size() << "bytes)";
                    } else {
                        qDebug() << "VP_ShowsSettingsDialog: Scaled thumb too large or empty";
                    }
                    
                    // Securely delete temp file (use 1 pass for temp files)
                    OperationsFiles::secureDelete(tempThumbPath, 1, false);
                } else {
                    qDebug() << "VP_ShowsSettingsDialog: Failed to open temp thumb file";
                }
            } else {
                qDebug() << "VP_ShowsSettingsDialog: Failed to download episode still image";
            }
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Could not get temp directory";
        }
    } else {
        qDebug() << "VP_ShowsSettingsDialog: No episode still path available";
    }
    
    // Note: EpisodeInfo doesn't have runtime field
    // metadata.runtime = episodeInfo.runtime;
    
    // Note: ShowMetadata doesn't have metadataUpdateDate field
    // We can update the encryptionDateTime to track when it was modified
    metadata.encryptionDateTime = QDateTime::currentDateTime();
    
    // Write updated metadata back to file
    if (!metadataManager.updateMetadataInFile(videoInfo.filePath, metadata)) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to write updated metadata";
        return false;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Successfully updated metadata with TMDB info";
    return true;
}

void VP_ShowsSettingsDialog::onImageDownloadFinished(QNetworkReply* reply)
{
    // This slot can be used for direct network downloads if needed
    // Currently, we're using the TMDB API's downloadImage method
    reply->deleteLater();
}

void VP_ShowsSettingsDialog::loadShowSettings()
{
    qDebug() << "VP_ShowsSettingsDialog: Loading show settings";
    qDebug() << "VP_ShowsSettingsDialog: Show path:" << m_showPath;
    
    // Check if the show folder exists
    if (m_showPath.isEmpty() || !QDir(m_showPath).exists()) {
        qDebug() << "VP_ShowsSettingsDialog: Show folder does not exist or path is empty:" << m_showPath;
        // Use defaults if folder doesn't exist
        m_currentSettings = VP_ShowsSettings::ShowSettings();
        ui->checkBox_Autoplay->setChecked(m_currentSettings.autoplay);
        ui->checkBox_AutoplayRandom->setChecked(m_currentSettings.autoplayRandom);
        ui->checkBox_SkipIntro->setChecked(m_currentSettings.skipIntro);
        ui->checkBox_SkipOutro->setChecked(m_currentSettings.skipOutro);
        ui->checkBox_UseTMDB->setChecked(m_currentSettings.useTMDB);
        ui->checkBox_AutoFullscreen->setChecked(m_currentSettings.autoFullscreen);
        return;
    }
    
    // Get parent MainWindow to access encryption key and username
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return;
    }
    
    // Get encryption key and username
    QByteArray encryptionKey = mainWindow->user_Key;
    QString username = mainWindow->user_Username;
    
    // Validate encryption key (must be exactly 32 bytes for AES-256)
    if (encryptionKey.isEmpty() || encryptionKey.size() != 32) {
        qDebug() << "VP_ShowsSettingsDialog: Invalid encryption key size:" << encryptionKey.size();
        QMessageBox::critical(this, tr("Authentication Error"), 
                            tr("Invalid encryption key. Please log out and log in again."));
        return;
    }
    
    if (username.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Username is empty";
        return;
    }
    
    // Create settings manager
    VP_ShowsSettings settingsManager(encryptionKey, username);
    
    // Load settings for this show
    if (!settingsManager.loadShowSettings(m_showPath, m_currentSettings)) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to load show settings, using defaults";
        // loadShowSettings returns true even when file doesn't exist (uses defaults)
        // So this block only executes on actual errors
    }
    
    // If settings don't have show name yet, set it now
    if (m_currentSettings.showName.isEmpty()) {
        m_currentSettings.showName = m_showName;
        qDebug() << "VP_ShowsSettingsDialog: Settings didn't have show name, setting it to:" << m_showName;
    }
    
    // Update UI with loaded settings
    ui->checkBox_Autoplay->setChecked(m_currentSettings.autoplay);
    ui->checkBox_AutoplayRandom->setChecked(m_currentSettings.autoplayRandom);
    ui->checkBox_SkipIntro->setChecked(m_currentSettings.skipIntro);
    ui->checkBox_SkipOutro->setChecked(m_currentSettings.skipOutro);
    ui->checkBox_UseTMDB->setChecked(m_currentSettings.useTMDB);
    ui->checkBox_AutoFullscreen->setChecked(m_currentSettings.autoFullscreen);
    ui->checkBox_DisplayFileNames->setChecked(m_currentSettings.displayFileNames);
    
    // Initialize the selected show ID from loaded settings
    if (!m_currentSettings.showId.isEmpty() && m_currentSettings.showId != "error") {
        bool ok = false;
        int showId = m_currentSettings.showId.toInt(&ok);
        if (ok && showId > 0) {
            m_selectedShowId = showId;
            qDebug() << "VP_ShowsSettingsDialog: Loaded existing show ID:" << m_selectedShowId;
        }
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Settings loaded - Autoplay:" << m_currentSettings.autoplay
             << "AutoplayRandom:" << m_currentSettings.autoplayRandom
             << "SkipIntro:" << m_currentSettings.skipIntro
             << "SkipOutro:" << m_currentSettings.skipOutro
             << "UseTMDB:" << m_currentSettings.useTMDB
             << "AutoFullscreen:" << m_currentSettings.autoFullscreen
             << "DisplayFileNames:" << m_currentSettings.displayFileNames
             << "ShowId:" << m_currentSettings.showId;
}

void VP_ShowsSettingsDialog::saveShowSettings()
{
    qDebug() << "VP_ShowsSettingsDialog: Saving show settings";
    qDebug() << "VP_ShowsSettingsDialog: Show path:" << m_showPath;
    
    // Check if the show folder exists
    if (m_showPath.isEmpty() || !QDir(m_showPath).exists()) {
        qDebug() << "VP_ShowsSettingsDialog: Show folder does not exist or path is empty:" << m_showPath;
        QMessageBox::warning(this, tr("Settings Error"), 
                           tr("Could not find the folder for this show."));
        return;
    }
    
    // Get parent MainWindow to access encryption key and username
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return;
    }
    
    // Get encryption key and username
    QByteArray encryptionKey = mainWindow->user_Key;
    QString username = mainWindow->user_Username;
    
    // Validate encryption key (must be exactly 32 bytes for AES-256)
    if (encryptionKey.isEmpty() || encryptionKey.size() != 32) {
        qDebug() << "VP_ShowsSettingsDialog: Invalid encryption key size:" << encryptionKey.size();
        QMessageBox::critical(this, tr("Authentication Error"), 
                            tr("Invalid encryption key. Please log out and log in again."));
        return;
    }
    
    if (username.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Username is empty";
        return;
    }
    
    // Get the new show name from the UI and update our member variable
    QString newShowName = ui->lineEdit_ShowName->text().trimmed();
    if (!newShowName.isEmpty()) {
        m_showName = newShowName;  // Update member variable with new name
    }
    
    // Update settings from UI
    m_currentSettings.showName = m_showName;  // Now uses the updated show name
    m_currentSettings.autoplay = ui->checkBox_Autoplay->isChecked();
    m_currentSettings.autoplayRandom = ui->checkBox_AutoplayRandom->isChecked();
    m_currentSettings.skipIntro = ui->checkBox_SkipIntro->isChecked();
    m_currentSettings.skipOutro = ui->checkBox_SkipOutro->isChecked();
    m_currentSettings.useTMDB = ui->checkBox_UseTMDB->isChecked();
    m_currentSettings.autoFullscreen = ui->checkBox_AutoFullscreen->isChecked();
    m_currentSettings.displayFileNames = ui->checkBox_DisplayFileNames->isChecked();
    
    // Update the show ID if we have a valid selection from TMDB
    if (m_selectedShowId > 0) {
        m_currentSettings.showId = QString::number(m_selectedShowId);
        qDebug() << "VP_ShowsSettingsDialog: Updating show ID in settings to:" << m_currentSettings.showId;
    } else {
        qDebug() << "VP_ShowsSettingsDialog: No TMDB show selected, keeping existing show ID:" << m_currentSettings.showId;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Settings to save - Autoplay:" << m_currentSettings.autoplay
             << "AutoplayRandom:" << m_currentSettings.autoplayRandom
             << "SkipIntro:" << m_currentSettings.skipIntro
             << "SkipOutro:" << m_currentSettings.skipOutro
             << "UseTMDB:" << m_currentSettings.useTMDB
             << "AutoFullscreen:" << m_currentSettings.autoFullscreen
             << "DisplayFileNames:" << m_currentSettings.displayFileNames
             << "ShowId:" << m_currentSettings.showId;
    
    // Create settings manager
    VP_ShowsSettings settingsManager(encryptionKey, username);
    
    // Save the settings
    if (!settingsManager.saveShowSettings(m_showPath, m_currentSettings)) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to save show settings";
        QMessageBox::warning(this, tr("Settings Error"), 
                           tr("Failed to save show settings."));
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Show settings saved successfully";
    }
}

void VP_ShowsSettingsDialog::updateAllVideosMetadata()
{
    qDebug() << "VP_ShowsSettingsDialog: Updating metadata for all videos in show folder";
    qDebug() << "VP_ShowsSettingsDialog: Show path:" << m_showPath;
    
    // Check if the show folder exists
    if (m_showPath.isEmpty() || !QDir(m_showPath).exists()) {
        qDebug() << "VP_ShowsSettingsDialog: Show folder does not exist or path is empty:" << m_showPath;
        QMessageBox::warning(this, tr("Metadata Error"), 
                           tr("Could not find the folder for this show."));
        return;
    }
    
    // Get the new show name from the UI
    QString newShowName = ui->lineEdit_ShowName->text().trimmed();
    if (newShowName.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Show name is empty, not updating metadata";
        return;
    }
    
    // Get parent MainWindow to access encryption key and username
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return;
    }
    
    // Get encryption key and username
    QByteArray encryptionKey = mainWindow->user_Key;
    QString username = mainWindow->user_Username;
    
    if (encryptionKey.isEmpty() || username.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Encryption key or username is empty";
        return;
    }
    
    // Find all video files in the show folder (now using .mmvid extension)
    QDir showDir(m_showPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files);
    
    qDebug() << "VP_ShowsSettingsDialog: Found" << videoFiles.size() << "video files to update";
    
    // Debug: List the files found
    if (videoFiles.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: No video files found in folder. Checking all files...";
        QStringList allFiles = showDir.entryList(QDir::Files);
        qDebug() << "VP_ShowsSettingsDialog: Total files in folder:" << allFiles.size();
        for (const QString& file : allFiles) {
            qDebug() << "VP_ShowsSettingsDialog:   File:" << file;
        }
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Video files found:";
        for (const QString& file : videoFiles) {
            qDebug() << "VP_ShowsSettingsDialog:   Video:" << file;
        }
    }
    
    // Create metadata manager
    VP_ShowsMetadata metadataManager(encryptionKey, username);
    
    int successCount = 0;
    int failCount = 0;
    
    // Update metadata for each video file
    for (const QString& videoFile : videoFiles) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        qDebug() << "VP_ShowsSettingsDialog: Updating metadata for:" << videoFile;
        
        // Read existing metadata
        VP_ShowsMetadata::ShowMetadata metadata;
        if (metadataManager.readMetadataFromFile(videoPath, metadata)) {
            // Update only the show name, preserve episode-specific data
            metadata.showName = newShowName;
            
            // Write updated metadata back
            if (metadataManager.writeMetadataToFile(videoPath, metadata)) {
                qDebug() << "VP_ShowsSettingsDialog: Successfully updated metadata for:" << videoFile;
                successCount++;
            } else {
                qDebug() << "VP_ShowsSettingsDialog: Failed to write metadata for:" << videoFile;
                failCount++;
            }
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to read metadata for:" << videoFile;
            // Try to create new metadata with just the show name
            metadata.showName = newShowName;
            if (metadataManager.writeMetadataToFile(videoPath, metadata)) {
                qDebug() << "VP_ShowsSettingsDialog: Created new metadata for:" << videoFile;
                successCount++;
            } else {
                qDebug() << "VP_ShowsSettingsDialog: Failed to create metadata for:" << videoFile;
                failCount++;
            }
        }
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Metadata update complete - Success:" << successCount 
             << "Failed:" << failCount;
    
    if (failCount > 0) {
        QMessageBox::warning(this, tr("Metadata Update"), 
                           tr("Some video files could not be updated. Successfully updated %1 of %2 files.")
                           .arg(successCount).arg(videoFiles.size()));
    }
}

void VP_ShowsSettingsDialog::updateShowDescription()
{
    qDebug() << "VP_ShowsSettingsDialog: Updating show description file";
    
    // Get the current description from the text browser
    QString currentDescription = ui->textBrowser_ShowDescription->toPlainText();
    
    // Get parent MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return;
    }
    
    // Generate the obfuscated folder name
    QDir showDir(m_showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showdesc_
    QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descFilePath = showDir.absoluteFilePath(descFileName);
    
    // If description is empty and file exists, delete it
    if (currentDescription.isEmpty() || currentDescription == "No description available.") {
        if (QFile::exists(descFilePath)) {
            QFile::remove(descFilePath);
            qDebug() << "VP_ShowsSettingsDialog: Removed empty description file";
        }
        return;
    }
    
    // Encrypt and save the description
    if (OperationsFiles::writeEncryptedFile(descFilePath, mainWindow->user_Key, currentDescription)) {
        qDebug() << "VP_ShowsSettingsDialog: Successfully saved show description";
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Failed to save show description";
    }
}

void VP_ShowsSettingsDialog::updateShowImage()
{
    qDebug() << "VP_ShowsSettingsDialog: Updating show image file";
    
    // Get parent MainWindow to access encryption key and username
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return;
    }
    
    // Check if we have a current pixmap in the label
    QPixmap currentPixmap = ui->label_ShowPoster->pixmap();
    if (currentPixmap.isNull()) {
        qDebug() << "VP_ShowsSettingsDialog: No poster image to save";
        
        // Delete existing image file if there's no image
        QDir showDir(m_showPath);
        QString obfuscatedName = showDir.dirName();
        QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
        QString imageFilePath = showDir.absoluteFilePath(imageFileName);
        
        if (QFile::exists(imageFilePath)) {
            QFile::remove(imageFilePath);
            qDebug() << "VP_ShowsSettingsDialog: Removed empty image file";
        }
        return;
    }
    
    // Convert pixmap to byte array
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    currentPixmap.save(&buffer, "PNG");
    buffer.close();
    
    // Generate the obfuscated folder name
    QDir showDir(m_showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showimage_
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    // Encrypt the image data
    QByteArray encryptedData = CryptoUtils::Encryption_EncryptBArray(
        mainWindow->user_Key, imageData, mainWindow->user_Username);
    
    if (encryptedData.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to encrypt image data";
        return;
    }
    
    // Save the encrypted image data to file
    QFile file(imageFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to open image file for writing:" << file.errorString();
        return;
    }
    
    qint64 written = file.write(encryptedData);
    file.close();
    
    if (written != encryptedData.size()) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to write complete image data";
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Successfully saved show image";
    }
}

void VP_ShowsSettingsDialog::onResetWatchHistoryClicked()
{
    qDebug() << "VP_ShowsSettingsDialog: Reset Watch History button clicked";
    
    // Confirm with the user
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Reset Watch History"),
        tr("Are you sure you want to reset the watch history for this show?\n\n"
           "This will mark all episodes as unwatched and reset their playback positions to the beginning."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (reply != QMessageBox::Yes) {
        qDebug() << "VP_ShowsSettingsDialog: User cancelled reset watch history";
        return;
    }
    
    // Get parent MainWindow to access encryption key and username
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Unable to access main window."));
        return;
    }
    
    // Create watch history manager
    VP_ShowsWatchHistory watchHistory(m_showPath, mainWindow->user_Key, mainWindow->user_Username);
    
    // Load existing history
    if (!watchHistory.loadHistory()) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to load watch history";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to load watch history."));
        return;
    }
    
    // Get all episodes from the watch history
    QStringList allEpisodes = watchHistory.getAllWatchedEpisodes();
    
    qDebug() << "VP_ShowsSettingsDialog: Found" << allEpisodes.size() << "episodes in watch history";
    
    // Mark each episode as unwatched and reset position
    for (const QString& episodePath : allEpisodes) {
        watchHistory.setEpisodeWatched(episodePath, false);
        watchHistory.resetEpisodePosition(episodePath);
    }
    
    // Clear the last watched episode
    watchHistory.clearLastWatchedEpisode();
    
    // Save the updated history
    if (watchHistory.saveHistoryWithBackup()) {
        qDebug() << "VP_ShowsSettingsDialog: Successfully reset watch history";
        QMessageBox::information(this, tr("Success"), 
                               tr("Watch history has been reset."));
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Failed to save reset watch history";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to save the reset watch history."));
    }
}

void VP_ShowsSettingsDialog::onUseCustomPosterClicked()
{
    qDebug() << "VP_ShowsSettingsDialog: Use Custom Poster button clicked";
    
    // Open file dialog to select an image
    QString selectedFile = QFileDialog::getOpenFileName(
        this,
        tr("Select Show Poster Image"),
        QDir::homePath(),
        tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)")
    );
    
    if (selectedFile.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: No image file selected";
        return;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Selected image file:" << selectedFile;
    
    // Validate the file path using InputValidation
    InputValidation::ValidationResult pathResult = InputValidation::validateInput(
        selectedFile, InputValidation::InputType::ExternalFilePath);
    
    if (!pathResult.isValid) {
        QMessageBox::warning(this, tr("Invalid File"), 
                           tr("Invalid file path: %1").arg(pathResult.errorMessage));
        return;
    }
    
    // Check file size (limit to 10MB)
    QFileInfo fileInfo(selectedFile);
    if (fileInfo.size() > 10 * 1024 * 1024) {
        QMessageBox::warning(this, tr("File Too Large"), 
                           tr("Please select an image smaller than 10MB."));
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
    QPixmap scaledPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // Display the custom poster
    ui->label_ShowPoster->setPixmap(scaledPoster);
    
    // Get parent MainWindow to access encryption key and username
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Unable to save poster."));
        return;
    }
    
    // Convert image to byte array for encryption
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    
    // Save as PNG for quality
    if (!poster.save(&buffer, "PNG")) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to convert image to byte array";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to process the image."));
        return;
    }
    
    // Generate the obfuscated folder name
    QDir showDir(m_showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showimage_
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    // Encrypt the image data
    QByteArray encryptedData = CryptoUtils::Encryption_EncryptBArray(
        mainWindow->user_Key, imageData, mainWindow->user_Username);
    
    if (encryptedData.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to encrypt image data";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to encrypt the image."));
        return;
    }
    
    // Save the encrypted image data to file
    QFile file(imageFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to open image file for writing:" << file.errorString();
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to save the image."));
        return;
    }
    
    qint64 written = file.write(encryptedData);
    file.close();
    
    if (written != encryptedData.size()) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to write complete image data";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to save the complete image."));
        return;
    }
    
    // Update the original poster reference
    m_originalPoster = scaledPoster;
    
    qDebug() << "VP_ShowsSettingsDialog: Successfully saved custom poster";
}

void VP_ShowsSettingsDialog::onUseCustomDescClicked()
{
    qDebug() << "VP_ShowsSettingsDialog: Use Custom Description button clicked";
    
    // Get current description to show as default
    QString currentDescription = ui->textBrowser_ShowDescription->toPlainText();
    if (currentDescription == "No description available.") {
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
        qDebug() << "VP_ShowsSettingsDialog: Description input cancelled";
        return;
    }
    
    // Validate the description using InputValidation
    if (!description.isEmpty()) {
        InputValidation::ValidationResult result = InputValidation::validateInput(
            description, InputValidation::InputType::PlainText, 5000);  // Allow up to 5000 chars
        
        if (!result.isValid) {
            QMessageBox::warning(this, tr("Invalid Description"), 
                               tr("The description contains invalid characters: %1").arg(result.errorMessage));
            return;
        }
    }
    
    // If description is empty, set default text
    if (description.isEmpty()) {
        description = "No description available.";
    }
    
    // Update the UI
    ui->textBrowser_ShowDescription->setPlainText(description);
    
    // Get parent MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Unable to save description."));
        return;
    }
    
    // Generate the obfuscated folder name
    QDir showDir(m_showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showdesc_
    QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descFilePath = showDir.absoluteFilePath(descFileName);
    
    // If description is the default text and file exists, delete it
    if (description == "No description available.") {
        if (QFile::exists(descFilePath)) {
            if (QFile::remove(descFilePath)) {
                qDebug() << "VP_ShowsSettingsDialog: Removed empty description file";
            }
        }
        m_originalDescription = description;
        return;
    }
    
    // Encrypt and save the description using OperationsFiles
    if (OperationsFiles::writeEncryptedFile(descFilePath, mainWindow->user_Key, description)) {
        qDebug() << "VP_ShowsSettingsDialog: Successfully saved show description";
        m_originalDescription = description;
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Failed to save show description";
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to save the description."));
    }
}

void VP_ShowsSettingsDialog::accept()
{
    qDebug() << "VP_ShowsSettingsDialog: OK button pressed, processing changes";
    
    // Check if show name changed
    QString newShowName = ui->lineEdit_ShowName->text().trimmed();
    bool showNameChanged = (!newShowName.isEmpty() && newShowName != m_originalShowName);
    
    if (showNameChanged) {
        qDebug() << "VP_ShowsSettingsDialog: Show name changed from" << m_originalShowName << "to" << newShowName;
        m_showName = newShowName;  // Update member variable
    }
    
    // Check if displayFileNames setting changed before saving
    bool oldDisplayFileNames = m_currentSettings.displayFileNames;
    bool newDisplayFileNames = ui->checkBox_DisplayFileNames->isChecked();
    
    if (oldDisplayFileNames != newDisplayFileNames) {
        m_displayFileNamesChanged = true;
        qDebug() << "VP_ShowsSettingsDialog: Display file names setting changed from" 
                 << oldDisplayFileNames << "to" << newDisplayFileNames;
    }
    
    // First update all video metadata with the new show name
    updateAllVideosMetadata();
    
    // Save/update the show description file
    updateShowDescription();
    
    // Save/update the show image file
    updateShowImage();
    
    // Then save the settings
    saveShowSettings();
    
    // Finally, call the base class accept to close the dialog
    QDialog::accept();
}

bool VP_ShowsSettingsDialog::eventFilter(QObject* obj, QEvent* event)
{
    // Handle events on the suggestions list viewport for better mouse tracking
    if (m_suggestionsList && obj == m_suggestionsList->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint pos = mouseEvent->pos();
            QListWidgetItem* item = m_suggestionsList->itemAt(pos);
            
            // Debug every 5th mouse move to avoid spam
            static int moveCount = 0;
            if (++moveCount % 5 == 0) {
                qDebug() << "VP_ShowsSettingsDialog: Mouse at viewport pos:" << pos 
                         << "item found:" << (item ? "yes" : "no");
            }
            
            if (item) {
                int index = m_suggestionsList->row(item);
                if (index != m_hoveredItemIndex) {
                    m_hoveredItemIndex = index;
                    m_suggestionsList->setCurrentItem(item);  // This will trigger the hover style
                    qDebug() << "VP_ShowsSettingsDialog: Hovering item" << index << ":" << item->text();
                    onSuggestionItemHovered();
                }
            } else {
                // Clear selection if not over any item
                if (m_hoveredItemIndex >= 0) {
                    qDebug() << "VP_ShowsSettingsDialog: Mouse not over any item, restoring original display";
                    m_hoveredItemIndex = -1;
                    m_suggestionsList->clearSelection();
                    
                    // Restore original poster and description when not hovering over any item
                    if (!m_originalPoster.isNull()) {
                        ui->label_ShowPoster->setPixmap(m_originalPoster);
                    } else {
                        ui->label_ShowPoster->setText("No Poster Available");
                    }
                    ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
                }
            }
            return false;
        }
        else if (event->type() == QEvent::Leave) {
            // Check if an item was just selected - if so, don't restore originals
            if (m_itemJustSelected) {
                qDebug() << "VP_ShowsSettingsDialog: Mouse left suggestions viewport after selection, keeping selected values";
                m_itemJustSelected = false;  // Clear the flag
            } else {
                qDebug() << "VP_ShowsSettingsDialog: Mouse left suggestions viewport, restoring original display";
                // Restore original poster and description when mouse leaves suggestions
                if (!m_originalPoster.isNull()) {
                    ui->label_ShowPoster->setPixmap(m_originalPoster);
                } else {
                    ui->label_ShowPoster->setText("No Poster Available");
                }
                ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
            }
            
            m_hoveredItemIndex = -1;
            m_suggestionsList->clearSelection();
            return false;
        }
        else if (event->type() == QEvent::Enter) {
            qDebug() << "VP_ShowsSettingsDialog: Mouse entered suggestions viewport";
            return false;
        }
    }
    
    // Handle events on the suggestions list widget itself
    if (obj == m_suggestionsList) {
        // Debug output for important events
        if (event->type() == QEvent::MouseButtonPress || 
            event->type() == QEvent::Enter ||
            event->type() == QEvent::Leave) {
            qDebug() << "VP_ShowsSettingsDialog: Suggestions list event:" << event->type();
        }
        return false;
    }
    
    // Handle mouse press events on the dialog
    if (obj == this && event->type() == QEvent::MouseButtonPress) {
        // Only hide suggestions if clicking outside both the line edit and suggestions list
        if (m_suggestionsList && m_suggestionsList->isVisible()) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint pos = mouseEvent->pos();  // Position relative to dialog
            
            // Check if click is within suggestions list geometry
            QRect suggestionsRect = m_suggestionsList->geometry();
            
            // Check if click is within line edit geometry
            QRect lineEditRect = ui->lineEdit_ShowName->geometry();
            // Adjust for parent widgets
            QWidget* parent = ui->lineEdit_ShowName->parentWidget();
            while (parent && parent != this) {
                lineEditRect.translate(parent->pos());
                parent = parent->parentWidget();
            }
            
            if (!suggestionsRect.contains(pos) && !lineEditRect.contains(pos)) {
                qDebug() << "VP_ShowsSettingsDialog: Click outside suggestions at pos:" << pos;
                qDebug() << "VP_ShowsSettingsDialog: Suggestions rect:" << suggestionsRect;
                qDebug() << "VP_ShowsSettingsDialog: LineEdit rect:" << lineEditRect;
                hideSuggestions(false);  // false = restore original values when clicking outside
            }
        }
        return false;
    }
    
    return QDialog::eventFilter(obj, event);
}

void VP_ShowsSettingsDialog::onLineEditFocusOut()
{
    // Additional handling for focus out if needed
    qDebug() << "VP_ShowsSettingsDialog: Line edit lost focus";
}

void VP_ShowsSettingsDialog::keyPressEvent(QKeyEvent* event)
{
    // Handle ESC key to close suggestions and restore original values
    if (event->key() == Qt::Key_Escape) {
        if (m_suggestionsList && m_suggestionsList->isVisible()) {
            qDebug() << "VP_ShowsSettingsDialog: ESC pressed, hiding suggestions and restoring original values";
            hideSuggestions(false);  // false = no item was selected, restore originals
            event->accept();
            return;
        }
    }
    
    // Pass to base class for default handling
    QDialog::keyPressEvent(event);
}

void VP_ShowsSettingsDialog::displayShowInfo(const VP_ShowsTMDB::ShowInfo& showInfo)
{
    // Display show information in the UI
    if (!showInfo.overview.isEmpty()) {
        ui->textBrowser_ShowDescription->setPlainText(showInfo.overview);
    } else {
        ui->textBrowser_ShowDescription->setPlainText("No description available.");
    }
    
    // Download and display poster
    if (!showInfo.posterPath.isEmpty()) {
        downloadAndDisplayPoster(showInfo.posterPath);
    } else {
        ui->label_ShowPoster->clear();
        ui->label_ShowPoster->setText("No Poster Available");
    }
}

QString VP_ShowsSettingsDialog::loadActualShowName()
{
    qDebug() << "VP_ShowsSettingsDialog: Loading actual show name from video metadata";
    
    // Get the parent widget and cast to MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return QString();
    }
    
    // Get encryption key and username from MainWindow member variables
    QByteArray encryptionKey = mainWindow->user_Key;
    QString username = mainWindow->user_Username;
    
    // Validate encryption key (must be exactly 32 bytes for AES-256)
    if (encryptionKey.isEmpty() || encryptionKey.size() != 32) {
        qDebug() << "VP_ShowsSettingsDialog: Invalid encryption key size:" << encryptionKey.size();
        return QString();
    }
    
    if (username.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Username is empty";
        return QString();
    }
    
    // Find any video file in the show folder (now using .mmvid extension)
    QDir showDir(m_showPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files);
    
    if (videoFiles.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: No video files found in show folder";
        return QString();
    }
    
    // Try to read metadata from the first video file
    QString firstVideoPath = showDir.absoluteFilePath(videoFiles.first());
    qDebug() << "VP_ShowsSettingsDialog: Reading metadata from:" << firstVideoPath;
    
    VP_ShowsMetadata metadataManager(encryptionKey, username);
    VP_ShowsMetadata::ShowMetadata metadata;
    
    if (metadataManager.readMetadataFromFile(firstVideoPath, metadata)) {
        qDebug() << "VP_ShowsSettingsDialog: Successfully read show name:" << metadata.showName;
        return metadata.showName;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Failed to read metadata from video file";
    return QString();
}

void VP_ShowsSettingsDialog::loadAndDisplayOriginalShowData()
{
    qDebug() << "VP_ShowsSettingsDialog: Loading original show poster and description";
    
    // Get the parent widget and cast to MainWindow to access encryption key
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWindow) {
        qDebug() << "VP_ShowsSettingsDialog: Parent is not MainWindow";
        return;
    }
    
    // Get encryption key from MainWindow member variables
    QByteArray encryptionKey = mainWindow->user_Key;
    
    if (encryptionKey.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Encryption key is empty";
        return;
    }
    
    // Generate the obfuscated folder name for loading files
    QDir showDir(m_showPath);
    QString obfuscatedName = showDir.dirName();
    
    // Load show description from showdesc_[obfuscated] file
    QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descFilePath = showDir.absoluteFilePath(descFileName);
    
    if (QFile::exists(descFilePath)) {
        QString description;
        if (OperationsFiles::readEncryptedFile(descFilePath, encryptionKey, description)) {
            m_originalDescription = description;
            ui->textBrowser_ShowDescription->setPlainText(description);
            qDebug() << "VP_ShowsSettingsDialog: Loaded show description";
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to decrypt show description";
            m_originalDescription = "No description available.";
            ui->textBrowser_ShowDescription->setPlainText(m_originalDescription);
        }
    } else {
        qDebug() << "VP_ShowsSettingsDialog: No description file found";
        m_originalDescription = "No description available.";
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
            QString username = mainWindow->user_Username;
            QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(encryptionKey, encryptedData);
            
            if (!decryptedData.isEmpty()) {
                // Load the decrypted image into a pixmap
                QPixmap poster;
                if (poster.loadFromData(decryptedData)) {
                    // Scale to fit the label
                    QSize labelSize = ui->label_ShowPoster->size();
                    m_originalPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    ui->label_ShowPoster->setPixmap(m_originalPoster);
                    qDebug() << "VP_ShowsSettingsDialog: Loaded and displayed show poster";
                } else {
                    qDebug() << "VP_ShowsSettingsDialog: Failed to load poster from decrypted data";
                    m_originalPoster = QPixmap();
                    ui->label_ShowPoster->setText("No Poster Available");
                }
            } else {
                qDebug() << "VP_ShowsSettingsDialog: Failed to decrypt poster data";
                m_originalPoster = QPixmap();
                ui->label_ShowPoster->setText("No Poster Available");
            }
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to open poster file";
            m_originalPoster = QPixmap();
            ui->label_ShowPoster->setText("No Poster Available");
        }
    } else {
        qDebug() << "VP_ShowsSettingsDialog: No poster file found";
        m_originalPoster = QPixmap();
        ui->label_ShowPoster->setText("No Poster Available");
    }
}
