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

VP_ShowsSettingsDialog::VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsSettingsDialog)
    , m_showName(showName)
    , m_showPath(showPath)
    , m_suggestionsList(nullptr)
    , m_searchTimer(nullptr)
    , m_tmdbApi(nullptr)
    , m_networkManager(nullptr)
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
    
    // Initialize TMDB API
    m_tmdbApi = std::make_unique<VP_ShowsTMDB>(this);
    m_tmdbApi->setApiKey(apiKey);
    
    // Initialize network manager for image downloads
    m_networkManager = std::make_unique<QNetworkAccessManager>(this);
    connect(m_networkManager.get(), &QNetworkAccessManager::finished,
            this, &VP_ShowsSettingsDialog::onImageDownloadFinished);
    
    // Create suggestions list widget (initially hidden)
    m_suggestionsList = new QListWidget(this);
    m_suggestionsList->setWindowFlags(Qt::Popup);
    m_suggestionsList->setFocusPolicy(Qt::NoFocus);
    m_suggestionsList->setMouseTracking(true);
    m_suggestionsList->setStyleSheet(
        "QListWidget {"
        "    border: 1px solid #ccc;"
        "    background-color: white;"
        "    selection-background-color: #e0e0e0;"
        "}"
        "QListWidget::item {"
        "    padding: 5px;"
        "    min-height: 50px;"
        "}"
        "QListWidget::item:hover {"
        "    background-color: #f0f0f0;"
        "}"
    );
    m_suggestionsList->hide();
    
    // Create search timer for debouncing
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SEARCH_DELAY_MS);
    connect(m_searchTimer, &QTimer::timeout, this, &VP_ShowsSettingsDialog::onSearchTimerTimeout);
    
    // Connect lineEdit signals
    connect(ui->lineEdit_ShowName, &QLineEdit::textChanged,
            this, &VP_ShowsSettingsDialog::onShowNameTextChanged);
    
    // Connect suggestions list signals
    connect(m_suggestionsList, &QListWidget::itemClicked,
            this, &VP_ShowsSettingsDialog::onSuggestionItemClicked);
    connect(m_suggestionsList, &QListWidget::itemEntered,
            this, &VP_ShowsSettingsDialog::onSuggestionItemHovered);
    
    // Install event filter on line edit to handle focus events
    ui->lineEdit_ShowName->installEventFilter(this);
    
    // Install event filter on the dialog to hide suggestions when clicking elsewhere
    this->installEventFilter(this);
    
    qDebug() << "VP_ShowsSettingsDialog: Autofill UI setup complete";
}

void VP_ShowsSettingsDialog::onShowNameTextChanged(const QString& text)
{
    // Validate input
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(text, InputValidation::InputType::PlainText, 100);
    
    if (!result.isValid) {
        qDebug() << "VP_ShowsSettingsDialog: Invalid input detected:" << result.errorMessage;
        return;
    }
    
    // Clear current suggestions if text is too short
    if (text.trimmed().length() < 2) {
        clearSuggestions();
        hideSuggestions();
        return;
    }
    
    // Store current search text
    m_currentSearchText = text.trimmed();
    
    // Reset and start the search timer (debouncing)
    m_searchTimer->stop();
    m_searchTimer->start();
    
    qDebug() << "VP_ShowsSettingsDialog: Text changed, starting search timer for:" << text;
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
    
    // Clear previous suggestions
    m_currentSuggestions.clear();
    
    // Search for TV shows and get multiple results
    m_currentSuggestions = m_tmdbApi->searchTVShows(searchText, MAX_SUGGESTIONS);
    
    if (!m_currentSuggestions.isEmpty()) {
        qDebug() << "VP_ShowsSettingsDialog: Found" << m_currentSuggestions.size() << "suggestions";
        displaySuggestions(m_currentSuggestions);
    } else {
        qDebug() << "VP_ShowsSettingsDialog: No results found for:" << searchText;
        clearSuggestions();
        hideSuggestions();
    }
}

void VP_ShowsSettingsDialog::displaySuggestions(const QList<VP_ShowsTMDB::ShowInfo>& shows)
{
    if (!m_suggestionsList) {
        return;
    }
    
    clearSuggestions();
    
    if (shows.isEmpty()) {
        hideSuggestions();
        return;
    }
    
    qDebug() << "VP_ShowsSettingsDialog: Displaying" << shows.size() << "suggestions";
    
    // Add suggestions to the list
    for (const auto& show : shows) {
        QString displayText = show.showName;
        if (!show.firstAirDate.isEmpty()) {
            // Extract year from date (format: YYYY-MM-DD)
            QString year = show.firstAirDate.left(4);
            displayText += QString(" (%1)").arg(year);
        }
        
        QListWidgetItem* item = new QListWidgetItem(displayText, m_suggestionsList);
        item->setData(Qt::UserRole, QVariant::fromValue(show.tmdbId));
        item->setData(Qt::UserRole + 1, show.showName);
        item->setData(Qt::UserRole + 2, show.overview);
        item->setData(Qt::UserRole + 3, show.posterPath);
        
        // Set item height
        item->setSizeHint(QSize(item->sizeHint().width(), SUGGESTION_HEIGHT));
    }
    
    // Position and show the suggestions list
    positionSuggestionsList();
    m_suggestionsList->show();
    m_suggestionsList->raise();
}

void VP_ShowsSettingsDialog::clearSuggestions()
{
    if (m_suggestionsList) {
        m_suggestionsList->clear();
    }
    m_currentSuggestions.clear();
}

void VP_ShowsSettingsDialog::hideSuggestions()
{
    if (m_suggestionsList) {
        m_suggestionsList->hide();
    }
    
    // Clear poster and description when hiding suggestions
    ui->label_ShowPoster->clear();
    ui->label_ShowPoster->setText("No Poster");
    ui->textBrowser_ShowDescription->clear();
}

void VP_ShowsSettingsDialog::positionSuggestionsList()
{
    if (!m_suggestionsList || !ui->lineEdit_ShowName) {
        return;
    }
    
    // Get the global position of the line edit
    QPoint globalPos = ui->lineEdit_ShowName->mapToGlobal(QPoint(0, ui->lineEdit_ShowName->height()));
    
    // Set the position of the suggestions list
    m_suggestionsList->move(globalPos);
    
    // Set the width to match the line edit
    m_suggestionsList->setFixedWidth(ui->lineEdit_ShowName->width());
    
    // Set maximum height (show up to MAX_SUGGESTIONS items)
    int itemCount = m_suggestionsList->count();
    int height = qMin(itemCount, MAX_SUGGESTIONS) * SUGGESTION_HEIGHT + 10; // +10 for borders/padding
    m_suggestionsList->setFixedHeight(height);
}

void VP_ShowsSettingsDialog::onSuggestionItemHovered()
{
    QListWidgetItem* item = m_suggestionsList->currentItem();
    if (!item) {
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
        return;
    }
    
    QString showName = item->data(Qt::UserRole + 1).toString();
    
    qDebug() << "VP_ShowsSettingsDialog: Selected show:" << showName;
    
    // Set the selected show name in the line edit
    ui->lineEdit_ShowName->setText(showName);
    
    // Hide suggestions
    hideSuggestions();
    
    // Stop any pending searches
    m_searchTimer->stop();
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
    // Handle focus out event for line edit
    if (obj == ui->lineEdit_ShowName && event->type() == QEvent::FocusOut) {
        // Hide suggestions when line edit loses focus
        // But not if the focus went to the suggestions list
        if (!m_suggestionsList || !m_suggestionsList->hasFocus()) {
            QTimer::singleShot(200, this, &VP_ShowsSettingsDialog::hideSuggestions);
        }
        return false;
    }
    
    // Handle mouse press events on the dialog
    if (obj == this && event->type() == QEvent::MouseButtonPress) {
        // Hide suggestions when clicking elsewhere in the dialog
        if (m_suggestionsList && m_suggestionsList->isVisible()) {
            QPoint globalPos = QCursor::pos();
            QRect suggestionsRect = m_suggestionsList->geometry();
            suggestionsRect.moveTopLeft(m_suggestionsList->mapToGlobal(QPoint(0, 0)));
            
            if (!suggestionsRect.contains(globalPos)) {
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
