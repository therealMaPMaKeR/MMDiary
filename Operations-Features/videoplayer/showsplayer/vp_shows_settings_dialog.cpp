#include "vp_shows_settings_dialog.h"
#include "ui_vp_shows_settings_dialog.h"
#include "vp_shows_config.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include <QDebug>
#include <QListWidgetItem>
#include <QEvent>
#include <QFocusEvent>
#include <QPixmap>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
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

VP_ShowsSettingsDialog::VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsSettingsDialog)
    , m_showName(showName)
    , m_showPath(showPath)
    , m_suggestionsList(nullptr)
    , m_searchTimer(nullptr)
    , m_tmdbApi(nullptr)
    , m_networkManager(nullptr)
    , m_isShowingSuggestions(false)
    , m_hoveredItemIndex(-1)
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsSettingsDialog: Created dialog for show:" << showName;
    qDebug() << "VP_ShowsSettingsDialog: Show path:" << showPath;
    
    // Set window title to include show name
    setWindowTitle(QString("Settings - %1").arg(showName));
    
    // Set initial show name in the line edit
    ui->lineEdit_ShowName->setText(showName);
    
    // Initialize autofill functionality
    setupAutofillUI();
    
    // TODO: Load show-specific settings here
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
    
    // Get TMDB API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: No TMDB API key configured, skipping autofill setup";
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
    m_suggestionsList->setSelectionMode(QAbstractItemView::NoSelection);  // No selection - we'll handle hover manually
    m_suggestionsList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_suggestionsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Ensure the widget accepts mouse events
    m_suggestionsList->setAttribute(Qt::WA_Hover, true);
    m_suggestionsList->setEnabled(true);
    
    // Set a high z-order to ensure it appears on top
    m_suggestionsList->raise();
    
    // Force text to be visible with explicit styling
    // Note: We handle hover manually, so we only use the default item style
    m_suggestionsList->setStyleSheet(
        "QListWidget { "
        "    background-color: white; "
        "    color: black; "
        "    border: 1px solid black; "
        "    font: 12px Arial; "
        "} "
        "QListWidget::item { "
        "    color: black; "
        "    background-color: white; "
        "    padding: 5px; "
        "    border: none; "
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
    
    // Don't connect currentItemChanged as it fires when we don't want it to
    // We'll handle hover through mouse events only
    
    // Install event filter on the dialog itself for click outside detection
    this->installEventFilter(this);
    
    // Also install event filter on suggestions list for additional debugging
    m_suggestionsList->installEventFilter(this);
    
    qDebug() << "VP_ShowsSettingsDialog: Autofill UI setup complete";
}

void VP_ShowsSettingsDialog::onShowNameTextChanged(const QString& text)
{
    qDebug() << "VP_ShowsSettingsDialog: onShowNameTextChanged called with text:" << text;
    
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
            hideSuggestions();
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
        hideSuggestions();
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
        hideSuggestions();
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
    
    // Reset hover index when showing new suggestions
    m_hoveredItemIndex = -1;
    
    // Force a repaint to ensure visibility
    m_suggestionsList->update();
    m_suggestionsList->repaint();
    
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

void VP_ShowsSettingsDialog::hideSuggestions()
{
    qDebug() << "VP_ShowsSettingsDialog: hideSuggestions() called";
    
    // Clear the flag and hover index
    m_isShowingSuggestions = false;
    m_hoveredItemIndex = -1;
    
    if (m_suggestionsList) {
        // Clear any hover styling before hiding
        for (int i = 0; i < m_suggestionsList->count(); ++i) {
            QListWidgetItem* item = m_suggestionsList->item(i);
            if (item) {
                item->setBackground(Qt::white);
                item->setForeground(Qt::black);
            }
        }
        qDebug() << "VP_ShowsSettingsDialog: Hiding suggestions list";
        m_suggestionsList->hide();
    }
    
    // Clear poster and description when hiding suggestions
    ui->label_ShowPoster->clear();
    ui->label_ShowPoster->setText("No Poster");
    ui->textBrowser_ShowDescription->clear();
    
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
    
    // Set maximum height (show up to MAX_SUGGESTIONS items)
    int itemCount = m_suggestionsList->count();
    int height = qMin(itemCount, MAX_SUGGESTIONS) * SUGGESTION_HEIGHT + 10; // +10 for borders/padding
    m_suggestionsList->setFixedHeight(height);
    
    qDebug() << "VP_ShowsSettingsDialog: Setting list height to:" << height << "(for" << itemCount << "items)";
    qDebug() << "VP_ShowsSettingsDialog: Final geometry:" << m_suggestionsList->geometry();
}

void VP_ShowsSettingsDialog::onSuggestionItemHovered()
{
    // Use the tracked hover index instead of currentItem
    if (m_hoveredItemIndex < 0 || m_hoveredItemIndex >= m_suggestionsList->count()) {
        qDebug() << "VP_ShowsSettingsDialog: No valid hover index";
        return;
    }
    
    QListWidgetItem* item = m_suggestionsList->item(m_hoveredItemIndex);
    if (!item) {
        qDebug() << "VP_ShowsSettingsDialog: No item at hover index" << m_hoveredItemIndex;
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
    
    QString showName = item->data(Qt::UserRole + 1).toString();
    QString overview = item->data(Qt::UserRole + 2).toString();
    QString posterPath = item->data(Qt::UserRole + 3).toString();
    
    qDebug() << "VP_ShowsSettingsDialog: Selected show:" << showName;
    
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
    
    // Hide suggestions
    hideSuggestions();
}

void VP_ShowsSettingsDialog::downloadAndDisplayPoster(const QString& posterPath)
{
    if (posterPath.isEmpty() || !m_tmdbApi) {
        return;
    }
    
    // Check if we already have this poster in cache
    if (m_posterCache.contains(posterPath)) {
        qDebug() << "VP_ShowsSettingsDialog: Using cached poster for:" << posterPath;
        QPixmap poster = m_posterCache[posterPath];
        
        // Scale to fit the label
        QSize labelSize = ui->label_ShowPoster->size();
        QPixmap scaledPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->label_ShowPoster->setPixmap(scaledPoster);
        return;
    }
    
    // Create a temporary file path for the image
    QString tempFileName = QString("tmdb_poster_%1_XXXXXX.jpg")
                          .arg(QString::number(QDateTime::currentMSecsSinceEpoch()));
    
    std::unique_ptr<QTemporaryFile> tempFile = OperationsFiles::createTempFile(tempFileName, false);
    if (!tempFile) {
        qDebug() << "VP_ShowsSettingsDialog: Failed to create temp file for poster";
        ui->label_ShowPoster->setText("Failed to Create Temp File");
        return;
    }
    
    QString tempFilePath = tempFile->fileName();
    tempFile->close(); // Close but keep the file
    
    // Download the poster
    bool success = m_tmdbApi->downloadImage(posterPath, tempFilePath, true); // true = isPoster
    
    if (success && QFile::exists(tempFilePath)) {
        qDebug() << "VP_ShowsSettingsDialog: Loading poster from:" << tempFilePath;
        
        QPixmap poster(tempFilePath);
        if (!poster.isNull()) {
            // Cache the poster
            m_posterCache[posterPath] = poster;
            
            // Scale to fit the label
            QSize labelSize = ui->label_ShowPoster->size();
            QPixmap scaledPoster = poster.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            ui->label_ShowPoster->setPixmap(scaledPoster);
            
            // Clean up the temp file
            QFile::remove(tempFilePath);
        } else {
            qDebug() << "VP_ShowsSettingsDialog: Failed to load poster image";
            ui->label_ShowPoster->setText("Failed to Load");
            QFile::remove(tempFilePath);
        }
    } else {
        qDebug() << "VP_ShowsSettingsDialog: Failed to download poster";
        ui->label_ShowPoster->setText("Download Failed");
        if (QFile::exists(tempFilePath)) {
            QFile::remove(tempFilePath);
        }
    }
}

void VP_ShowsSettingsDialog::onImageDownloadFinished(QNetworkReply* reply)
{
    // This slot can be used for direct network downloads if needed
    // Currently, we're using the TMDB API's downloadImage method
    reply->deleteLater();
}

bool VP_ShowsSettingsDialog::eventFilter(QObject* obj, QEvent* event)
{
    // Handle events on the suggestions list
    if (obj == m_suggestionsList) {
        // Debug output for important events
        if (event->type() == QEvent::MouseButtonPress || 
            event->type() == QEvent::MouseButtonRelease ||
            event->type() == QEvent::Enter ||
            event->type() == QEvent::Leave ||
            event->type() == QEvent::Show ||
            event->type() == QEvent::Hide) {
            qDebug() << "VP_ShowsSettingsDialog: Suggestions list event:" << event->type();
        }
        
        // Handle mouse move for hover tracking
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint pos = mouseEvent->pos();
            
            // Find which item is under the mouse
            QListWidgetItem* item = m_suggestionsList->itemAt(pos);
            int newHoverIndex = item ? m_suggestionsList->row(item) : -1;
            
            // Only update if hovering over a different item
            if (newHoverIndex != m_hoveredItemIndex) {
                // Clear previous hover state
                if (m_hoveredItemIndex >= 0 && m_hoveredItemIndex < m_suggestionsList->count()) {
                    QListWidgetItem* prevItem = m_suggestionsList->item(m_hoveredItemIndex);
                    if (prevItem) {
                        // Reset to normal style
                        prevItem->setBackground(Qt::white);
                        prevItem->setForeground(Qt::black);
                    }
                }
                
                // Set new hover state
                m_hoveredItemIndex = newHoverIndex;
                if (m_hoveredItemIndex >= 0) {
                    QListWidgetItem* currentItem = m_suggestionsList->item(m_hoveredItemIndex);
                    if (currentItem) {
                        qDebug() << "VP_ShowsSettingsDialog: Mouse hovering over item" << m_hoveredItemIndex << ":" << currentItem->text();
                        // Apply hover style
                        currentItem->setBackground(QColor("lightblue"));
                        currentItem->setForeground(Qt::black);
                        // Call hover handler
                        onSuggestionItemHovered();
                    }
                } else {
                    qDebug() << "VP_ShowsSettingsDialog: Mouse not over any item";
                }
            }
            return false;
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                // Use the currently hovered item
                if (m_hoveredItemIndex >= 0 && m_hoveredItemIndex < m_suggestionsList->count()) {
                    QListWidgetItem* item = m_suggestionsList->item(m_hoveredItemIndex);
                    if (item) {
                        qDebug() << "VP_ShowsSettingsDialog: Mouse press on item" << m_hoveredItemIndex << ":" << item->text();
                        // Manually call the click handler
                        onSuggestionItemClicked(item);
                        return true;  // Consume the event
                    }
                }
            }
            return false;
        }
        else if (event->type() == QEvent::Leave) {
            // Clear hover state when mouse leaves
            qDebug() << "VP_ShowsSettingsDialog: Mouse left suggestions list";
            if (m_hoveredItemIndex >= 0 && m_hoveredItemIndex < m_suggestionsList->count()) {
                QListWidgetItem* item = m_suggestionsList->item(m_hoveredItemIndex);
                if (item) {
                    // Reset to normal style
                    item->setBackground(Qt::white);
                    item->setForeground(Qt::black);
                }
            }
            m_hoveredItemIndex = -1;
        }
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
                hideSuggestions();
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
