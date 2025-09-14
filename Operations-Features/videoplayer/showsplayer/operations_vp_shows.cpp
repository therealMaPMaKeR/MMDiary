#include "operations_vp_shows.h"
#include "mainwindow.h"
#include <QPainter>
#include "ui_mainwindow.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include "CustomWidgets/videoplayer/qlist_VP_ShowsList.h"
#include <QRandomGenerator>
#include "VP_Shows_Videoplayer.h"
#include "vp_shows_progressdialogs.h"
#include "vp_shows_metadata.h"
#include "vp_shows_settings_dialog.h"
#include "vp_shows_add_dialog.h"
#include "vp_shows_edit_metadata_dialog.h"
#include "vp_shows_edit_multiple_metadata_dialog.h"
#include "vp_shows_tmdb.h"
#include "vp_shows_config.h"
#include "vp_shows_settings.h"  // Add show settings
#include "inputvalidation.cpp"
#include "operations_files.h"  // Add operations_files for secure file operations
#include "CryptoUtils.h"
#include "vp_shows_watchhistory.h"  // Core watch history data management
#include "vp_shows_playback_tracker.h"  // Playback tracking integration
#include "vp_shows_favourites.h"  // Favourites management
#include "../vp_metadata_lock_manager.h"  // Metadata lock manager for concurrent access protection
#include <QCheckBox>
#include <QDataStream>
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QRandomGenerator>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextBrowser>
#include <QLabel>
#include <QStackedWidget>
#include <QBuffer>
#include <QPixmap>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCoreApplication>
#include <QProgressDialog>
#include <QMessageBox>
#include <QStorageInfo>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QRegularExpression>
#include <QPushButton>
#include <QScreen>
#include <QWindow>
#include <QComboBox>
#include <QListView>
#include <QPainter>
#include <QFont>
#include <QLineEdit>
#include <QThread>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QStyle>
#include <QIcon>
#include <algorithm>
#include <functional>

// Windows-specific includes for file explorer
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <QProcess>
#endif

// Helper function to convert showId QString to int
int getShowIdAsInt(const QString& showId) {
    if (showId.isEmpty() || showId == "error") {
        return 0;
    }
    bool ok = false;
    int id = showId.toInt(&ok);
    return ok ? id : 0;
}

Operations_VP_Shows::Operations_VP_Shows(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_encryptionDialog(nullptr)
    , m_watchHistory(nullptr)
    , m_playbackTracker(nullptr)
    , m_episodeDetector(nullptr)
    , m_searchDebounceTimer(nullptr)
    , m_isAutoplayInProgress(false)
    , m_episodeWasNearCompletion(false)
    , m_isDecrypting(false)
    , m_pendingAutoplayIsRandom(false)
{
    qDebug() << "Operations_VP_Shows: Constructor called";
    qDebug() << "Operations_VP_Shows: Autoplay system initialized";
    qDebug() << "Operations_VP_Shows: === CONFIGURATION ===";
    qDebug() << "Operations_VP_Shows:   COMPLETION_THRESHOLD_MS:" << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS << "ms (" << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS/1000 << " seconds)";
    qDebug() << "Operations_VP_Shows:   This threshold is used for:";
    qDebug() << "Operations_VP_Shows:     - Marking episodes as completed";
    qDebug() << "Operations_VP_Shows:     - Resetting resume position to beginning";
    qDebug() << "Operations_VP_Shows:     - Near-completion detection for autoplay";
    qDebug() << "Operations_VP_Shows:   SAVE_INTERVAL_SECONDS:" << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds";
    qDebug() << "Operations_VP_Shows:   Initial near-completion flag:" << m_episodeWasNearCompletion;
    
    // Set username for operations_files functions
    if (m_mainWindow && !m_mainWindow->user_Username.isEmpty()) {
        OperationsFiles::setUsername(m_mainWindow->user_Username);
        qDebug() << "Operations_VP_Shows: Set username for operations_files:" << m_mainWindow->user_Username;
    }
    
    // Initialize episode detector
    m_episodeDetector = std::make_unique<VP_ShowsEpisodeDetector>(m_mainWindow);
    qDebug() << "Operations_VP_Shows: Initialized episode detector";
    
    // Connect the Add Show button if it exists
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->pushButton_VP_List_AddShow) {
        connect(m_mainWindow->ui->pushButton_VP_List_AddShow, &QPushButton::clicked,
                this, &Operations_VP_Shows::on_pushButton_VP_List_AddShow_clicked);
        qDebug() << "Operations_VP_Shows: Connected Add Show button";
    }
    
    // Connect the Add Episode button if it exists
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->pushButton_VP_List_AddEpisode) {
        connect(m_mainWindow->ui->pushButton_VP_List_AddEpisode, &QPushButton::clicked,
                this, &Operations_VP_Shows::on_pushButton_VP_List_AddEpisode_clicked);
        qDebug() << "Operations_VP_Shows: Connected Add Episode button";
        
        // Initially disable the button (no show selected yet)
        m_mainWindow->ui->pushButton_VP_List_AddEpisode->setEnabled(false);
        
        // Apply disabled styling
        QString disabledStyle = "QPushButton { "
                              "    color: rgba(255, 255, 255, 0.4); "
                              "    background-color: rgba(60, 60, 60, 0.3); "
                              "}";
        m_mainWindow->ui->pushButton_VP_List_AddEpisode->setStyleSheet(disabledStyle);
    }
    
    // Connect double-click on shows list to display show details
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->listWidget_VP_List_List) {
        connect(m_mainWindow->ui->listWidget_VP_List_List, &QListWidget::itemDoubleClicked,
                this, &Operations_VP_Shows::onShowListItemDoubleClicked);
        qDebug() << "Operations_VP_Shows: Connected show list double-click handler";
        
        // Connect selection change signal to update Add Episodes button state
        connect(m_mainWindow->ui->listWidget_VP_List_List, &QListWidget::itemSelectionChanged,
                this, &Operations_VP_Shows::onShowListSelectionChanged);
        qDebug() << "Operations_VP_Shows: Connected show list selection change handler";
        
        // Connect the custom selectionCleared signal for when user clicks on empty space
        qlist_VP_ShowsList* customList = qobject_cast<qlist_VP_ShowsList*>(m_mainWindow->ui->listWidget_VP_List_List);
        if (customList) {
            connect(customList, &qlist_VP_ShowsList::selectionCleared,
                    this, &Operations_VP_Shows::onShowListSelectionChanged);
            qDebug() << "Operations_VP_Shows: Connected custom selection cleared signal";
        }
        
        // Setup context menu for the list widget
        setupContextMenu();
    }
    
    // Connect the view mode combo box
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->comboBox_VP_Shows_ListViewMode) {
        connect(m_mainWindow->ui->comboBox_VP_Shows_ListViewMode, 
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &Operations_VP_Shows::onViewModeChanged);
        qDebug() << "Operations_VP_Shows: Connected view mode combo box";
        
        // Set initial view mode based on combo box
        onViewModeChanged(m_mainWindow->ui->comboBox_VP_Shows_ListViewMode->currentIndex());
    }
    
    // Initialize search functionality
    m_currentSearchText = "";
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(300); // 300ms debounce delay
    connect(m_searchDebounceTimer, &QTimer::timeout,
            this, &Operations_VP_Shows::onSearchTimerTimeout);
    qDebug() << "Operations_VP_Shows: Search debounce timer initialized with 300ms delay";
    
    // Connect search bar text changes
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->lineEdit_VP_Shows_SearchBar) {
        connect(m_mainWindow->ui->lineEdit_VP_Shows_SearchBar, &QLineEdit::textChanged,
                this, &Operations_VP_Shows::onSearchTextChanged);
        
        // Connect enter key to stop debounce and search immediately
        connect(m_mainWindow->ui->lineEdit_VP_Shows_SearchBar, &QLineEdit::returnPressed, [this]() {
            m_searchDebounceTimer->stop();
            onSearchTimerTimeout();
        });
        
        qDebug() << "Operations_VP_Shows: Connected search bar signal handlers";
    }
    
    // Connect back to list button on display page
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->pushButton_VP_Shows_Display_ReturnToList) {
        connect(m_mainWindow->ui->pushButton_VP_Shows_Display_ReturnToList, &QPushButton::clicked,
                this, [this]() {
                    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->stackedWidget_VP_Shows) {
                        m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(0); // Go back to list page
                        qDebug() << "Operations_VP_Shows: Returned to shows list";
                    }
                });
        qDebug() << "Operations_VP_Shows: Connected return to list button";
    }
    
    // Install event filter on the stackedWidget to handle Escape key
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->stackedWidget_VP_Shows) {
        m_mainWindow->ui->stackedWidget_VP_Shows->installEventFilter(this);
        qDebug() << "Operations_VP_Shows: Installed event filter on stackedWidget_VP_Shows for Escape key handling";
        
        // Also install on child widgets to ensure we catch the key press
        if (m_mainWindow->ui->page_display) {
            m_mainWindow->ui->page_display->installEventFilter(this);
            qDebug() << "Operations_VP_Shows: Installed event filter on page_display (display page)";
        }
        
        // Install on the episode tree widget as well since it might have focus
        if (m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
            m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->installEventFilter(this);
            qDebug() << "Operations_VP_Shows: Installed event filter on episode tree widget";
        }
    }
    
    // Connect Play/Continue button on display page
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->pushButton_VP_Shows_Display_Play) {
        connect(m_mainWindow->ui->pushButton_VP_Shows_Display_Play, &QPushButton::clicked,
                this, &Operations_VP_Shows::onPlayContinueClicked);
        qDebug() << "Operations_VP_Shows: Connected play/continue button";
    }
    
    // Connect episode tree widget double-click
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        connect(m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList, &QTreeWidget::itemDoubleClicked,
                this, &Operations_VP_Shows::onEpisodeDoubleClicked);
        qDebug() << "Operations_VP_Shows: Connected episode tree widget double-click handler";
        
        // Setup context menu for the episode tree widget
        setupEpisodeContextMenu();
    }
    
    // Connect Settings button on display page
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->pushButton_VP_Shows_Display_Settings) {
        connect(m_mainWindow->ui->pushButton_VP_Shows_Display_Settings, &QPushButton::clicked,
                this, &Operations_VP_Shows::openShowSettings);
        qDebug() << "Operations_VP_Shows: Connected show settings button";
    }
    
    // REMOVED - Checkbox connections moved to settings dialog
    // Checkboxes are no longer on the display page
    /*
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->checkBox_VP_Shows_Display_SkipContent) {
        connect(m_mainWindow->ui->checkBox_VP_Shows_Display_SkipContent, &QCheckBox::stateChanged,
                this, &Operations_VP_Shows::onSkipContentCheckboxChanged);
        qDebug() << "Operations_VP_Shows: Connected skip intro/outro checkbox";
    }
    
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->checkBox_VP_Shows_Display_Autoplay) {
        connect(m_mainWindow->ui->checkBox_VP_Shows_Display_Autoplay, &QCheckBox::stateChanged,
                this, &Operations_VP_Shows::onAutoplayCheckboxChanged);
        qDebug() << "Operations_VP_Shows: Connected autoplay checkbox";
    }
    */
    
    // Clean up any incomplete show folders from previous sessions (crashed imports, etc.)
    // This should happen before loading the shows list
    cleanupIncompleteShowFolders();
    
    // Load the TV shows list on initialization
    // We use a small delay to ensure the UI is fully initialized
    QTimer::singleShot(100, this, &Operations_VP_Shows::loadTVShowsList);
}

Operations_VP_Shows::~Operations_VP_Shows()
{
    qDebug() << "Operations_VP_Shows: Destructor called";
    
    // Reset autoplay flags to prevent stuck state
    if (m_isAutoplayInProgress) {
        qDebug() << "Operations_VP_Shows: WARNING - Destructor called while autoplay in progress, resetting flag";
        m_isAutoplayInProgress = false;
    }
    if (m_episodeWasNearCompletion) {
        qDebug() << "Operations_VP_Shows: Resetting near-completion flag in destructor";
        m_episodeWasNearCompletion = false;
    }
    
    // Clear pending autoplay information
    if (!m_pendingAutoplayPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Clearing pending autoplay information in destructor";
        m_pendingAutoplayPath.clear();
        m_pendingAutoplayName.clear();
        m_pendingAutoplayIsRandom = false;
    }
    
    // Clear pending context menu play information
    if (!m_pendingContextMenuEpisodePath.isEmpty() || !m_pendingContextMenuEpisodeName.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Clearing pending context menu play information in destructor";
        m_pendingContextMenuEpisodePath.clear();
        m_pendingContextMenuEpisodeName.clear();
    }
    
    // Clear context menu data first to prevent dangling references
    clearContextMenuData();
    
    // Stop playback tracking first before destroying player
    if (m_playbackTracker) {
        qDebug() << "Operations_VP_Shows: Stopping playback tracking in destructor";
        m_playbackTracker->stopTracking();
        // Disconnect all signals from tracker to prevent callbacks
        disconnect(m_playbackTracker.get(), nullptr, this, nullptr);
    }
    
    // Force release and cleanup any playing video
    if (m_episodePlayer) {
        // Disconnect all signals from player first to prevent callbacks during destruction
        disconnect(m_episodePlayer.get(), nullptr, this, nullptr);
        
        // Stop any active playback
        if (m_episodePlayer->isPlaying()) {
            qDebug() << "Operations_VP_Shows: Stopping active playback before destruction";
            m_episodePlayer->stop();
        }
        
        forceReleaseVideoFile();
        // Reset the player to ensure it's properly closed
        m_episodePlayer.reset();
    }
    
    // Similarly for test player
    if (m_testVideoPlayer) {
        disconnect(m_testVideoPlayer.get(), nullptr, this, nullptr);
        if (m_testVideoPlayer->isPlaying()) {
            m_testVideoPlayer->stop();
        }
        m_testVideoPlayer.reset();
    }
    
    // Clean up playback tracker after player is destroyed
    if (m_playbackTracker) {
        m_playbackTracker.reset();
    }
    if (m_watchHistory) {
        m_watchHistory.reset();
    }
    if (m_showFavourites) {
        m_showFavourites.reset();
    }
    
    // m_encryptionDialog is a QPointer, will be automatically nulled when deleted
    if (m_encryptionDialog) {
        m_encryptionDialog->deleteLater();  // Use deleteLater for safer cleanup
    }
    
    // Clean up temp file if it exists
    cleanupTempFile();
    
    // Clean up any remaining metadata locks
    int activeLocks = VP_MetadataLockManager::instance()->activeLocksCount();
    if (activeLocks > 0) {
        qDebug() << "Operations_VP_Shows: Warning - Found" << activeLocks << "active locks during destructor, cleaning up";
        VP_MetadataLockManager::instance()->cleanup();
    }
    
    // Playback speeds are now handled globally in BaseVideoPlayer
}

// ============================================================================
// Event Filter Implementation
// ============================================================================

bool Operations_VP_Shows::eventFilter(QObject* watched, QEvent* event)
{
    // Check if this is a key press event
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // Check if the Escape key was pressed
        if (keyEvent->key() == Qt::Key_Escape) {
            // Check if we're currently on the display page (index 1)
            if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->stackedWidget_VP_Shows) {
                int currentIndex = m_mainWindow->ui->stackedWidget_VP_Shows->currentIndex();
                
                if (currentIndex == 1) {  // We're on the display page
                    qDebug() << "Operations_VP_Shows: Escape key pressed on display page, returning to list";
                    
                    // Switch back to the list page (index 0)
                    m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(0);
                    
                    // Event was handled
                    return true;
                }
            }
        }
    }
    // Check if this is a mouse button press event
    else if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        
        // Check if mouse button 4 (XButton1 - typically the "back" button) was pressed
        if (mouseEvent->button() == Qt::XButton1) {
            // Check if we're currently on the display page (index 1)
            if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->stackedWidget_VP_Shows) {
                int currentIndex = m_mainWindow->ui->stackedWidget_VP_Shows->currentIndex();
                
                if (currentIndex == 1) {  // We're on the display page
                    qDebug() << "Operations_VP_Shows: Mouse button 4 (back) pressed on display page, returning to list";
                    
                    // Switch back to the list page (index 0)
                    m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(0);
                    
                    // Event was handled
                    return true;
                }
            }
        }
    }
    
    // Pass the event to the base class
    return QObject::eventFilter(watched, event);
}

// ============================================================================
// Safe Widget Access Helper Functions
// ============================================================================

QListWidgetItem* Operations_VP_Shows::safeGetListItem(QListWidget* widget, int index) const
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: Widget is null in safeGetListItem";
        return nullptr;
    }
    
    if (index < 0 || index >= widget->count()) {
        qDebug() << "Operations_VP_Shows: Index out of bounds in safeGetListItem:" << index << "count:" << widget->count();
        return nullptr;
    }
    
    return widget->item(index);
}

QTreeWidgetItem* Operations_VP_Shows::safeGetTreeItem(QTreeWidget* widget, int index) const
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: Widget is null in safeGetTreeItem";
        return nullptr;
    }
    
    if (index < 0 || index >= widget->topLevelItemCount()) {
        qDebug() << "Operations_VP_Shows: Index out of bounds in safeGetTreeItem:" << index << "count:" << widget->topLevelItemCount();
        return nullptr;
    }
    
    return widget->topLevelItem(index);
}

QListWidgetItem* Operations_VP_Shows::safeTakeListItem(QListWidget* widget, int index)
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: Widget is null in safeTakeListItem";
        return nullptr;
    }
    
    if (index < 0 || index >= widget->count()) {
        qDebug() << "Operations_VP_Shows: Index out of bounds in safeTakeListItem:" << index << "count:" << widget->count();
        return nullptr;
    }
    
    return widget->takeItem(index);
}

bool Operations_VP_Shows::validateListWidget(QListWidget* widget) const
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: List widget is null";
        return false;
    }
    return true;
}

bool Operations_VP_Shows::validateTreeWidget(QTreeWidget* widget) const
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: Tree widget is null";
        return false;
    }
    return true;
}

int Operations_VP_Shows::safeGetListItemCount(QListWidget* widget) const
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: Widget is null in safeGetListItemCount";
        return 0;
    }
    return widget->count();
}

int Operations_VP_Shows::safeGetTreeItemCount(QTreeWidget* widget) const
{
    if (!widget) {
        qDebug() << "Operations_VP_Shows: Widget is null in safeGetTreeItemCount";
        return 0;
    }
    return widget->topLevelItemCount();
}

QString Operations_VP_Shows::selectVideoFile()
{
    qDebug() << "Operations_VP_Shows: Opening file dialog for video selection";
    
    QString filter = "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg *.3gp);;All Files (*.*)";
    
    QString filePath = QFileDialog::getOpenFileName(
        m_mainWindow,
        tr("Select Video File"),
        QDir::homePath(),
        filter
    );
    
    if (!filePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Selected file:" << filePath;
    } else {
        qDebug() << "Operations_VP_Shows: No file selected";
    }
    
    return filePath;
}

bool Operations_VP_Shows::isValidVideoFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    
    QFileInfo fileInfo(filePath);
    
    if (!fileInfo.exists()) {
        qDebug() << "Operations_VP_Shows: File does not exist:" << filePath;
        return false;
    }
    
    if (!fileInfo.isFile()) {
        qDebug() << "Operations_VP_Shows: Path is not a file:" << filePath;
        return false;
    }
    
    // Check file extension
    QStringList validExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "3gp"};
    QString extension = fileInfo.suffix().toLower();
    
    if (!validExtensions.contains(extension)) {
        qDebug() << "Operations_VP_Shows: Invalid video file extension:" << extension;
        return false;
    }
    
    return true;
}

void Operations_VP_Shows::on_pushButton_VP_List_AddShow_clicked()
{
    qDebug() << "Operations_VP_Shows: Add Show button clicked";
    importTVShow();
}

void Operations_VP_Shows::onShowListSelectionChanged()
{
    qDebug() << "Operations_VP_Shows: Show list selection changed";
    
    // Check if we have the Add Episodes button on the list page
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->pushButton_VP_List_AddEpisode) {
        qDebug() << "Operations_VP_Shows: Add Episodes button not available";
        return;
    }
    
    // Check if we have the shows list widget
    if (!m_mainWindow->ui->listWidget_VP_List_List) {
        qDebug() << "Operations_VP_Shows: Shows list widget not available";
        return;
    }
    
    // Get the current selection
    QListWidgetItem* selectedItem = m_mainWindow->ui->listWidget_VP_List_List->currentItem();
    
    // Define styles for enabled and disabled states
    QString enabledStyle = "";  // Default/empty style for enabled state
    QString disabledStyle = "QPushButton { "
                           "    color: rgba(255, 255, 255, 0.4); "
                           "    background-color: rgba(60, 60, 60, 0.3); "
                           "}";
    
    if (selectedItem) {
        // A show is selected - enable the button
        qDebug() << "Operations_VP_Shows: Show selected:" << selectedItem->text() << "- enabling Add Episodes button";
        m_mainWindow->ui->pushButton_VP_List_AddEpisode->setEnabled(true);
        m_mainWindow->ui->pushButton_VP_List_AddEpisode->setStyleSheet(enabledStyle);
    } else {
        // No show selected - disable the button
        qDebug() << "Operations_VP_Shows: No show selected - disabling Add Episodes button";
        m_mainWindow->ui->pushButton_VP_List_AddEpisode->setEnabled(false);
        m_mainWindow->ui->pushButton_VP_List_AddEpisode->setStyleSheet(disabledStyle);
    }
}

void Operations_VP_Shows::on_pushButton_VP_List_AddEpisode_clicked()
{
    qDebug() << "Operations_VP_Shows: Add Episode button clicked";
    
    // Check if a show is selected in the list
    QListWidgetItem* selectedShowItem = nullptr;
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->listWidget_VP_List_List) {
        selectedShowItem = m_mainWindow->ui->listWidget_VP_List_List->currentItem();
    }
    
    if (selectedShowItem) {
        // A show is selected - add episodes to this show
        QString showName = selectedShowItem->text();
        QString showPath = selectedShowItem->data(Qt::UserRole).toString();
        
        qDebug() << "Operations_VP_Shows: Adding episodes to selected show:" << showName;
        
        // Set up context menu data for the selected show
        clearContextMenuData();
        m_contextMenuShowName = showName;
        m_contextMenuShowPath = showPath;
        
        // Call the existing addEpisodesToShow function which now shows the selection dialog
        addEpisodesToShow();
        return;
    }
    
    // No show selected - create new show with episodes
    qDebug() << "Operations_VP_Shows: No show selected, creating new show with episodes";
    
    // Create a simple dialog to ask user to choose between files or folder (same as Add Show)
    QDialog selectionDialog(m_mainWindow);
    selectionDialog.setWindowTitle(tr("Select Import Method"));
    selectionDialog.setModal(true);
    selectionDialog.setFixedSize(300, 80);
    
    QVBoxLayout* layout = new QVBoxLayout(&selectionDialog);
    layout->setContentsMargins(10, 10, 10, 10);  // Tighter margins
    layout->setSpacing(10);  // Tighter spacing
    
    QLabel* label = new QLabel(tr("How would you like to add episodes?"), &selectionDialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    
    layout->addSpacing(5);  // Reduced from 20 to 5
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(5);  // Tighter button spacing
    
    // Create buttons with icons
    QPushButton* selectFilesBtn = new QPushButton(tr("Select Files"), &selectionDialog);
    selectFilesBtn->setIcon(QIcon::fromTheme("document-open", 
                            m_mainWindow->style()->standardIcon(QStyle::SP_FileIcon)));
    
    QPushButton* selectFolderBtn = new QPushButton(tr("Select Folder"), &selectionDialog);
    selectFolderBtn->setIcon(QIcon::fromTheme("folder-open", 
                            m_mainWindow->style()->standardIcon(QStyle::SP_DirIcon)));
    
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), &selectionDialog);
    
    buttonLayout->addWidget(selectFilesBtn);
    buttonLayout->addWidget(selectFolderBtn);
    buttonLayout->addWidget(cancelBtn);
    
    layout->addLayout(buttonLayout);
    
    // Variable to track which option was selected
    enum SelectionType { None, Files, Folder };
    SelectionType selectedType = None;
    
    connect(selectFilesBtn, &QPushButton::clicked, [&]() {
        selectedType = Files;
        selectionDialog.accept();
    });
    
    connect(selectFolderBtn, &QPushButton::clicked, [&]() {
        selectedType = Folder;
        selectionDialog.accept();
    });
    
    connect(cancelBtn, &QPushButton::clicked, [&]() {
        selectionDialog.reject();
    });
    
    if (selectionDialog.exec() != QDialog::Accepted || selectedType == None) {
        qDebug() << "Operations_VP_Shows: Import method selection cancelled";
        // Clear any pending events after dialog closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Clear any pending events after dialog closes
    QCoreApplication::processEvents();
    
    QStringList selectedFiles;
    QString folderPath;
    
    if (selectedType == Files) {
        // Select multiple files
        qDebug() << "Operations_VP_Shows: User chose to select files";
        
        QString filter = "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg *.3gp);;All Files (*.*)";
        
        selectedFiles = QFileDialog::getOpenFileNames(
            m_mainWindow,
            tr("Select Episode Video Files"),
            QDir::homePath(),
            filter
        );
        
        if (selectedFiles.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No files selected for adding episodes";
            // Clear any pending events after file dialog closes
            QCoreApplication::processEvents();
            return;
        }
        
        // Clear any pending events after file dialog closes
        QCoreApplication::processEvents();
        
        qDebug() << "Operations_VP_Shows: Selected" << selectedFiles.size() << "files";
        
    } else if (selectedType == Folder) {
        // Select folder
        qDebug() << "Operations_VP_Shows: User chose to select folder";
        
        folderPath = QFileDialog::getExistingDirectory(
            m_mainWindow,
            tr("Select Folder Containing Episodes"),
            QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );
        
        if (folderPath.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No folder selected";
            // Clear any pending events after folder dialog closes
            QCoreApplication::processEvents();
            return;
        }
        
        // Clear any pending events after folder dialog closes
        QCoreApplication::processEvents();
        
        qDebug() << "Operations_VP_Shows: Selected folder:" << folderPath;
        
        // Find all video files in the folder and subfolders
        selectedFiles = findVideoFiles(folderPath);
        
        if (selectedFiles.isEmpty()) {
            QMessageBox::warning(m_mainWindow,
                               tr("No Video Files Found"),
                               tr("The selected folder does not contain any compatible video files."));
            // Clear any pending events after message box closes
            QCoreApplication::processEvents();
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Found" << selectedFiles.size() << "video files in folder";
    }
    
    qDebug() << "Operations_VP_Shows: Selected" << selectedFiles.size() << "files for episodes";
    
    // Clear the source folder path since we're adding individual files (no folder cleanup needed)
    m_originalSourceFolderPath.clear();
    
    // Show the add dialog with empty show name field
    VP_ShowsAddDialog addDialog("", m_mainWindow);  // Pass empty string for show name
    addDialog.setWindowTitle(tr("Add Episodes to Library"));
    
    if (addDialog.exec() != QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Add episodes dialog cancelled";
        // Clear any pending events after dialog closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Clear any pending events after dialog closes
    QCoreApplication::processEvents();
    
    // Get the show details from the dialog
    QString showName = addDialog.getShowName();
    QString language = addDialog.getLanguage();
    QString translationMode = addDialog.getTranslationMode();
    
    // The dialog already validates that show name is not empty
    if (showName.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Show name is empty after dialog (should not happen)";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Adding episodes - Show:" << showName
             << "Language:" << language << "Translation:" << translationMode;
    
    // Check if a show with the same name already exists (regardless of language/translation)
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    bool showNameExists = false;
    
    QDir showsDir(showsPath);
    if (showsDir.exists()) {
        // Create settings manager to check for existing show names
        VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        
        QStringList showFolders = showsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& folderName : showFolders) {
            QString folderPath = showsDir.absoluteFilePath(folderName);
            
            // Try to read the show settings file
            VP_ShowsSettings::ShowSettings settings;
            if (settingsManager.loadShowSettings(folderPath, settings)) {
                if (settings.showName == showName) {
                    showNameExists = true;
                    qDebug() << "Operations_VP_Shows: Found existing show with same name:" << showName;
                    break;
                }
            }
        }
    }
    
    // Show warning if a show with the same name exists
    if (showNameExists) {
        QMessageBox msgBox(m_mainWindow);
        msgBox.setWindowTitle(tr("Show Name Already Exists"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText(tr("A show with the name '%1' already exists in your library.").arg(showName));
        msgBox.setInformativeText(tr("If you wish to add episodes to the existing show, please cancel and select the show from the list, then use the 'Add Episodes' button or context menu option.\n\n"
                                     "If this is a different show with the same name, you can continue to create a new entry."));
        
        QPushButton* cancelButton = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
        QPushButton* continueButton = msgBox.addButton(tr("Continue"), QMessageBox::AcceptRole);
        
        msgBox.setDefaultButton(cancelButton);
        msgBox.exec();
        
        if (msgBox.clickedButton() == cancelButton) {
            qDebug() << "Operations_VP_Shows: User cancelled import due to duplicate show name";
            return;
        }
        
        qDebug() << "Operations_VP_Shows: User chose to continue with duplicate show name";
    }
    
    // Now we proceed with the import as a new show
    QString outputPath;
    QStringList filesToImport = selectedFiles;  // Import all selected files
    QStringList targetFiles;
    
    qDebug() << "Operations_VP_Shows: This is a new show, importing all episodes";
    
    // Create the output folder structure using secure operations_files functions
    if (!createShowFolderStructure(outputPath)) {
        QMessageBox::critical(m_mainWindow,
                            tr("Folder Creation Failed"),
                            tr("Failed to create the necessary folder structure. Please check permissions and try again."));
        return;
    }
    m_currentImportOutputPath = outputPath; // Store for use in onEncryptionComplete
    
    // Generate random filenames for each video file to import
    for (const QString& sourceFile : filesToImport) {
        // Always use .mmvid extension for encrypted video files
        QString randomName = generateRandomFileName("mmvid");
        QString targetFile = QDir(outputPath).absoluteFilePath(randomName);
        
        // Validate the target path using operations_files
        if (!OperationsFiles::isWithinAllowedDirectory(targetFile, "Data")) {
            qDebug() << "Operations_VP_Shows: Generated target path is outside allowed directory:" << targetFile;
            QMessageBox::critical(m_mainWindow,
                                tr("Security Error"),
                                tr("Failed to generate secure file paths. Operation cancelled."));
            return;
        }
        
        targetFiles.append(targetFile);
    }
    
    // Get encryption key and username from mainwindow
    QByteArray encryptionKey = m_mainWindow->user_Key;
    QString username = m_mainWindow->user_Username;
    
    // This is always a new show import now
    m_isUpdatingExistingShow = false;
    m_originalEpisodeCount = selectedFiles.size();
    m_newEpisodeCount = filesToImport.size();
    
    // Create and show progress dialog
    if (!m_encryptionDialog) {
        m_encryptionDialog = new VP_ShowsEncryptionProgressDialog(m_mainWindow);
        connect(m_encryptionDialog, &VP_ShowsEncryptionProgressDialog::encryptionComplete,
                this, &Operations_VP_Shows::onEncryptionComplete);
    }
    
    // Get TMDB preference and custom data from the dialog
    bool useTMDB = addDialog.isUsingTMDB();
    QPixmap customPoster;
    QString customDescription;
    
    // Get the parse mode from the dialog
    VP_ShowsEncryptionWorker::ParseMode parseMode = 
        (addDialog.getParseMode() == VP_ShowsAddDialog::ParseFromFolder) ? 
        VP_ShowsEncryptionWorker::ParseFromFolder : 
        VP_ShowsEncryptionWorker::ParseFromFile;
    
    // Get playback settings from the dialog
    bool autoplay = addDialog.isAutoplayEnabled();
    bool skipIntro = addDialog.isSkipIntroEnabled();
    bool skipOutro = addDialog.isSkipOutroEnabled();
    
    // Store dialog settings for use in onEncryptionComplete
    m_dialogShowName = showName;
    m_dialogAutoplay = autoplay;
    m_dialogSkipIntro = skipIntro;
    m_dialogSkipOutro = skipOutro;
    m_dialogUseTMDB = useTMDB;
    m_dialogShowId = addDialog.getSelectedShowId();  // Store the selected TMDB show ID
    
    qDebug() << "Operations_VP_Shows: Selected TMDB show ID:" << m_dialogShowId;
    
    qDebug() << "Operations_VP_Shows: Dialog settings - Autoplay:" << autoplay << "SkipIntro:" << skipIntro << "SkipOutro:" << skipOutro;
    
    qDebug() << "Operations_VP_Shows: Dialog returned - Using TMDB:" << useTMDB;
    qDebug() << "Operations_VP_Shows: Checking for custom data...";
    
    // Get the parse mode from the dialog
    parseMode =
        (addDialog.getParseMode() == VP_ShowsAddDialog::ParseFromFolder) ? 
        VP_ShowsEncryptionWorker::ParseFromFolder : 
        VP_ShowsEncryptionWorker::ParseFromFile;
    
    qDebug() << "Operations_VP_Shows: Parse mode:" << (parseMode == VP_ShowsEncryptionWorker::ParseFromFolder ? "Folder" : "File");
    
    if (!useTMDB) {
        qDebug() << "Operations_VP_Shows: TMDB disabled, checking for custom data";
        qDebug() << "Operations_VP_Shows: Calling hasCustomPoster()...";
        bool hasPoster = addDialog.hasCustomPoster();
        qDebug() << "Operations_VP_Shows: hasCustomPoster() returned:" << hasPoster;
        
        if (hasPoster) {
            customPoster = addDialog.getCustomPoster();
            qDebug() << "Operations_VP_Shows: Using custom poster, size:" << customPoster.size();
            qDebug() << "Operations_VP_Shows: Custom poster is null:" << customPoster.isNull();
        } else {
            qDebug() << "Operations_VP_Shows: No custom poster set";
        }
        
        qDebug() << "Operations_VP_Shows: Calling hasCustomDescription()...";
        bool hasDesc = addDialog.hasCustomDescription();
        qDebug() << "Operations_VP_Shows: hasCustomDescription() returned:" << hasDesc;
        
        if (hasDesc) {
            customDescription = addDialog.getCustomDescription();
            qDebug() << "Operations_VP_Shows: Using custom description, length:" << customDescription.length();
            qDebug() << "Operations_VP_Shows: Description preview:" << customDescription.left(100);
        } else {
            qDebug() << "Operations_VP_Shows: No custom description set";
        }
    } else {
        qDebug() << "Operations_VP_Shows: Using TMDB, not retrieving custom data";
    }
    
    // Start encryption with language and translation info
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, encryptionKey, username, 
                                       language, translationMode, useTMDB, customPoster, customDescription, parseMode, m_dialogShowId);
}

void Operations_VP_Shows::importTVShow()
{
    qDebug() << "Operations_VP_Shows: Starting TV show import";
    
    // Ensure username is set for operations_files
    if (!m_mainWindow->user_Username.isEmpty()) {
        OperationsFiles::setUsername(m_mainWindow->user_Username);
    }
    
    // Create a simple dialog to ask user to choose between files or folder
    QDialog selectionDialog(m_mainWindow);
    selectionDialog.setWindowTitle(tr("Select Import Method"));
    selectionDialog.setModal(true);
    selectionDialog.setFixedSize(300, 80);
    
    QVBoxLayout* layout = new QVBoxLayout(&selectionDialog);
    layout->setContentsMargins(10, 10, 10, 10);  // Tighter margins
    layout->setSpacing(10);  // Tighter spacing
    
    QLabel* label = new QLabel(tr("How would you like to import your TV show?"), &selectionDialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    
    layout->addSpacing(5);  // Reduced from 20 to 5
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(5);  // Tighter button spacing
    
    // Create buttons with icons
    QPushButton* selectFilesBtn = new QPushButton(tr("Select Files"), &selectionDialog);
    selectFilesBtn->setIcon(QIcon::fromTheme("document-open", 
                            m_mainWindow->style()->standardIcon(QStyle::SP_FileIcon)));
    
    QPushButton* selectFolderBtn = new QPushButton(tr("Select Folder"), &selectionDialog);
    selectFolderBtn->setIcon(QIcon::fromTheme("folder-open", 
                            m_mainWindow->style()->standardIcon(QStyle::SP_DirIcon)));
    
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), &selectionDialog);
    
    buttonLayout->addWidget(selectFilesBtn);
    buttonLayout->addWidget(selectFolderBtn);
    buttonLayout->addWidget(cancelBtn);
    
    layout->addLayout(buttonLayout);
    
    // Variable to track which option was selected
    enum SelectionType { None, Files, Folder };
    SelectionType selectedType = None;
    
    connect(selectFilesBtn, &QPushButton::clicked, [&]() {
        selectedType = Files;
        selectionDialog.accept();
    });
    
    connect(selectFolderBtn, &QPushButton::clicked, [&]() {
        selectedType = Folder;
        selectionDialog.accept();
    });
    
    connect(cancelBtn, &QPushButton::clicked, [&]() {
        selectionDialog.reject();
    });
    
    if (selectionDialog.exec() != QDialog::Accepted || selectedType == None) {
        qDebug() << "Operations_VP_Shows: Import method selection cancelled";
        return;
    }
    
    QStringList selectedFiles;
    QString folderPath;
    QString folderName;
    
    if (selectedType == Files) {
        // Select multiple files
        qDebug() << "Operations_VP_Shows: User chose to select files";
        
        QString filter = "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg *.3gp);;All Files (*.*)";
        selectedFiles = QFileDialog::getOpenFileNames(
            m_mainWindow,
            tr("Select TV Show Episode Files"),
            QDir::homePath(),
            filter
        );
        
        if (selectedFiles.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No files selected";
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Selected" << selectedFiles.size() << "files";
        
        // Clear the source folder path since we're selecting individual files
        m_originalSourceFolderPath.clear();
        
        // For files mode, we don't have a folder name to pre-populate
        folderName = "";
        
    } else if (selectedType == Folder) {
        // Select folder
        qDebug() << "Operations_VP_Shows: User chose to select folder";
        
        folderPath = QFileDialog::getExistingDirectory(
            m_mainWindow,
            tr("Select TV Show Folder"),
            QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );
        
        if (folderPath.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No folder selected";
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Selected folder:" << folderPath;
        
        // Store the original source folder for cleanup boundary (folder import mode)
        m_originalSourceFolderPath = folderPath;
        qDebug() << "Operations_VP_Shows: Folder import mode - directory cleanup will be performed after file deletion";
        
        // Get the folder name to pre-populate the dialog
        QDir selectedDir(folderPath);
        folderName = selectedDir.dirName();
    }
    
    // Show the add show dialog
    VP_ShowsAddDialog addDialog(folderName, m_mainWindow);
    if (addDialog.exec() != QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Add show dialog cancelled";
        return;
    }
    
    // Get the show details from the dialog
    QString showName = addDialog.getShowName();
    QString language = addDialog.getLanguage();
    QString translationMode = addDialog.getTranslationMode();
    
    // Get custom poster and description if not using TMDB
    bool useTMDB = addDialog.isUsingTMDB();
    QPixmap customPoster;
    QString customDescription;
    
    // Get playback settings from the dialog
    bool autoplay = addDialog.isAutoplayEnabled();
    bool skipIntro = addDialog.isSkipIntroEnabled();
    bool skipOutro = addDialog.isSkipOutroEnabled();
    
    // Store dialog settings for use in onEncryptionComplete
    m_dialogShowName = showName;
    m_dialogAutoplay = autoplay;
    m_dialogSkipIntro = skipIntro;
    m_dialogSkipOutro = skipOutro;
    m_dialogUseTMDB = useTMDB;
    m_dialogShowId = addDialog.getSelectedShowId();  // Store the selected TMDB show ID
    
    qDebug() << "Operations_VP_Shows: Selected TMDB show ID:" << m_dialogShowId;
    qDebug() << "Operations_VP_Shows: Dialog settings - Autoplay:" << autoplay << "SkipIntro:" << skipIntro << "SkipOutro:" << skipOutro;
    qDebug() << "Operations_VP_Shows: Dialog returned - Using TMDB:" << useTMDB;
    qDebug() << "Operations_VP_Shows: Checking for custom data...";
    
    if (!useTMDB) {
        qDebug() << "Operations_VP_Shows: TMDB disabled, checking for custom data";
        qDebug() << "Operations_VP_Shows: Calling hasCustomPoster()...";
        bool hasPoster = addDialog.hasCustomPoster();
        qDebug() << "Operations_VP_Shows: hasCustomPoster() returned:" << hasPoster;
        
        if (hasPoster) {
            customPoster = addDialog.getCustomPoster();
            qDebug() << "Operations_VP_Shows: Using custom poster, size:" << customPoster.size();
            qDebug() << "Operations_VP_Shows: Custom poster is null:" << customPoster.isNull();
        } else {
            qDebug() << "Operations_VP_Shows: No custom poster set";
        }
        
        qDebug() << "Operations_VP_Shows: Calling hasCustomDescription()...";
        bool hasDesc = addDialog.hasCustomDescription();
        qDebug() << "Operations_VP_Shows: hasCustomDescription() returned:" << hasDesc;
        
        if (hasDesc) {
            customDescription = addDialog.getCustomDescription();
            qDebug() << "Operations_VP_Shows: Using custom description, length:" << customDescription.length();
            qDebug() << "Operations_VP_Shows: Description preview:" << customDescription.left(100);
        } else {
            qDebug() << "Operations_VP_Shows: No custom description set";
        }
    } else {
        qDebug() << "Operations_VP_Shows: Using TMDB, not retrieving custom data";
    }
    
    qDebug() << "Operations_VP_Shows: Show details - Name:" << showName 
             << "Language:" << language << "Translation:" << translationMode
             << "Using TMDB:" << useTMDB;
    
    // Determine which files to import based on selection type
    QStringList videoFiles;
    if (selectedType == Files) {
        // We already have selected files
        videoFiles = selectedFiles;
    } else if (selectedType == Folder) {
        // Find all video files in the folder and subfolders
        videoFiles = findVideoFiles(folderPath);
        
        if (videoFiles.isEmpty()) {
            QMessageBox::warning(m_mainWindow,
                               tr("No Video Files Found"),
                               tr("The selected folder does not contain any compatible video files."));
            return;
        }
    }
    
    qDebug() << "Operations_VP_Shows: Found/selected" << videoFiles.size() << "video files";
    
    // Check if a show with the same name already exists (regardless of language/translation)
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    bool showNameExists = false;
    
    QDir showsDir(showsPath);
    if (showsDir.exists()) {
        // Create settings manager to check for existing show names
        VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        
        QStringList showFolders = showsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& folderName : showFolders) {
            QString folderPath = showsDir.absoluteFilePath(folderName);
            
            // Try to read the show settings file
            VP_ShowsSettings::ShowSettings settings;
            if (settingsManager.loadShowSettings(folderPath, settings)) {
                if (settings.showName == showName) {
                    showNameExists = true;
                    qDebug() << "Operations_VP_Shows: Found existing show with same name:" << showName;
                    break;
                }
            }
        }
    }
    
    // Show warning if a show with the same name exists
    if (showNameExists) {
        QMessageBox msgBox(m_mainWindow);
        msgBox.setWindowTitle(tr("Show Name Already Exists"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText(tr("A show with the name '%1' already exists in your library.").arg(showName));
        msgBox.setInformativeText(tr("If you wish to add episodes to the existing show, please cancel and select the show from the list, then use the 'Add Episodes' button or context menu option.\n\n"
                                     "If this is a different show with the same name, you can continue to create a new entry."));
        
        QPushButton* cancelButton = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
        QPushButton* continueButton = msgBox.addButton(tr("Continue"), QMessageBox::AcceptRole);
        
        msgBox.setDefaultButton(cancelButton);
        msgBox.exec();
        
        if (msgBox.clickedButton() == cancelButton) {
            qDebug() << "Operations_VP_Shows: User cancelled import due to duplicate show name";
            return;
        }
        
        qDebug() << "Operations_VP_Shows: User chose to continue with duplicate show name";
    }
    
    // Now we proceed with the import as a new show
    QString outputPath;
    QStringList filesToImport = videoFiles;  // Import all selected/found files
    QStringList targetFiles;
    
    qDebug() << "Operations_VP_Shows: This is a new show, importing all episodes";
    
    // Create the output folder structure using secure operations_files functions
    if (!createShowFolderStructure(outputPath)) {
        QMessageBox::critical(m_mainWindow,
                            tr("Folder Creation Failed"),
                            tr("Failed to create the necessary folder structure. Please check permissions and try again."));
        return;
    }
    
    // Store the output path for use in onEncryptionComplete
    m_currentImportOutputPath = outputPath;
    
    // Generate random filenames for each video file to import
    for (const QString& sourceFile : filesToImport) {
        // Always use .mmvid extension for encrypted video files
        QString randomName = generateRandomFileName("mmvid");
        QString targetFile = QDir(outputPath).absoluteFilePath(randomName);
        
        // Validate the target path using operations_files
        if (!OperationsFiles::isWithinAllowedDirectory(targetFile, "Data")) {
            qDebug() << "Operations_VP_Shows: Generated target path is outside allowed directory:" << targetFile;
            QMessageBox::critical(m_mainWindow,
                                tr("Security Error"),
                                tr("Failed to generate secure file paths. Operation cancelled."));
            return;
        }
        
        targetFiles.append(targetFile);
    }
    
    // Get encryption key and username from mainwindow
    QByteArray encryptionKey = m_mainWindow->user_Key;
    QString username = m_mainWindow->user_Username;
    
    // This is always a new show import now
    m_isUpdatingExistingShow = false;
    m_originalEpisodeCount = videoFiles.size();
    m_newEpisodeCount = filesToImport.size();
    
    // Create and show progress dialog
    if (!m_encryptionDialog) {
        m_encryptionDialog = new VP_ShowsEncryptionProgressDialog(m_mainWindow);
        if (m_encryptionDialog) {  // Check if creation was successful
            connect(m_encryptionDialog, &VP_ShowsEncryptionProgressDialog::encryptionComplete,
                    this, &Operations_VP_Shows::onEncryptionComplete);
        } else {
            qDebug() << "Operations_VP_Shows: Failed to create encryption dialog";
            return;
        }
    }
    
    // Get the parse mode from the dialog
    VP_ShowsEncryptionWorker::ParseMode parseMode = 
        (addDialog.getParseMode() == VP_ShowsAddDialog::ParseFromFolder) ? 
        VP_ShowsEncryptionWorker::ParseFromFolder : 
        VP_ShowsEncryptionWorker::ParseFromFile;
    
    qDebug() << "Operations_VP_Shows: Parse mode:" << (parseMode == VP_ShowsEncryptionWorker::ParseFromFolder ? "Folder" : "File");
    
    // Start encryption with language and translation info, including custom data if not using TMDB
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, encryptionKey, username, 
                                       language, translationMode, useTMDB, customPoster, customDescription, parseMode, m_dialogShowId);
}

QStringList Operations_VP_Shows::findVideoFiles(const QString& folderPath, bool recursive)
{
    QStringList videoFiles;
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "3gp"}; // Regular video extensions for import
    
    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (recursive) {
        flags = QDirIterator::Subdirectories;
    }
    
    QDirIterator it(folderPath, flags);
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);
        
        if (fileInfo.isFile()) {
            QString extension = fileInfo.suffix().toLower();
            if (videoExtensions.contains(extension)) {
                // SECURITY: Validate that the file is actually a video
                if (InputValidation::isValidVideoFile(filePath)) {
                    videoFiles.append(filePath);
                    qDebug() << "Operations_VP_Shows: Found valid video file:" << fileInfo.fileName();
                } else {
                    qDebug() << "Operations_VP_Shows: Skipping file with video extension but invalid header:" << fileInfo.fileName();
                }
            }
        }
    }
    
    return videoFiles;
}

bool Operations_VP_Shows::checkForExistingShow(const QString& showName, const QString& language, 
                                              const QString& translation, QString& existingFolder,
                                              QStringList& existingEpisodes)
{
    qDebug() << "Operations_VP_Shows: Checking for existing show:" << showName 
             << "Language:" << language << "Translation:" << translation;
    
    // Build the path to the shows directory
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    QDir showsDir(showsPath);
    if (!showsDir.exists()) {
        qDebug() << "Operations_VP_Shows: Shows directory does not exist yet";
        return false;
    }
    
    // Get all subdirectories in the shows folder
    QStringList showFolders = showsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    // Create metadata manager for reading show metadata
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Create settings manager for reading show settings
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Check each show folder
    for (const QString& folderName : showFolders) {
        QString folderPath = showsDir.absoluteFilePath(folderName);
        QDir showFolder(folderPath);
        
        // First, try to read the show settings file to get the show name
        // This will work even if all video files have broken metadata
        VP_ShowsSettings::ShowSettings settings;
        if (settingsManager.loadShowSettings(folderPath, settings)) {
            // Check if this is the show we're looking for
            if (settings.showName == showName) {
                qDebug() << "Operations_VP_Shows: Found matching show via settings file in folder:" << folderPath;
                existingFolder = folderPath;
                
                // Now try to get existing episodes (they might have broken metadata)
                // We'll still try to read them but won't fail if metadata is broken
                QStringList videoExtensions;
                videoExtensions << "*.mmvid";
                showFolder.setNameFilters(videoExtensions);
                QStringList videoFiles = showFolder.entryList(QDir::Files);
                
                for (const QString& videoFile : videoFiles) {
                    QString videoPath = showFolder.absoluteFilePath(videoFile);
                    VP_ShowsMetadata::ShowMetadata epMetadata;
                    
                    if (metadataManager.readMetadataFromFile(videoPath, epMetadata)) {
                        // Only add episodes with matching language and translation
                        if (epMetadata.language == language && epMetadata.translation == translation) {
                            int seasonNum = epMetadata.season.toInt();
                            int episodeNum = epMetadata.episode.toInt();
                            
                            if (seasonNum == 0 || episodeNum == 0) {
                                VP_ShowsTMDB::parseEpisodeFromFilename(epMetadata.filename, seasonNum, episodeNum);
                            }
                            
                            QString episodeId;
                            if (seasonNum > 0 && episodeNum > 0) {
                                episodeId = QString("S%1E%2").arg(seasonNum, 2, 10, QChar('0'))
                                                            .arg(episodeNum, 2, 10, QChar('0'));
                            } else {
                                episodeId = epMetadata.filename;
                            }
                            
                            existingEpisodes.append(episodeId);
                            qDebug() << "Operations_VP_Shows: Found existing episode:" << episodeId;
                        }
                    }
                    // If metadata read fails, we just skip that episode (it has broken metadata)
                }
                
                return true; // Found the show via settings file
            }
        }
        
        // Fallback: Try the original method using video metadata
        // (in case there's no settings file or it can't be read)
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
            // Check if this is the same show with same language and translation
            if (metadata.showName == showName && 
                metadata.language == language && 
                metadata.translation == translation) {
                
                qDebug() << "Operations_VP_Shows: Found existing show in folder:" << folderPath;
                existingFolder = folderPath;
                
                // Get all existing episodes with their identifiers
                for (const QString& videoFile : videoFiles) {
                    QString videoPath = showFolder.absoluteFilePath(videoFile);
                    VP_ShowsMetadata::ShowMetadata epMetadata;
                    
                    if (metadataManager.readMetadataFromFile(videoPath, epMetadata)) {
                        // Create episode identifier based on season and episode numbers
                        int seasonNum = epMetadata.season.toInt();
                        int episodeNum = epMetadata.episode.toInt();
                        
                        // If metadata doesn't have valid numbers, try to parse from filename
                        if (seasonNum == 0 || episodeNum == 0) {
                            VP_ShowsTMDB::parseEpisodeFromFilename(epMetadata.filename, seasonNum, episodeNum);
                        }
                        
                        // Create identifier like "S01E01" or use original filename if parsing failed
                        QString episodeId;
                        if (seasonNum > 0 && episodeNum > 0) {
                            episodeId = QString("S%1E%2").arg(seasonNum, 2, 10, QChar('0'))
                                                        .arg(episodeNum, 2, 10, QChar('0'));
                        } else {
                            // Use the original filename as identifier
                            episodeId = epMetadata.filename;
                        }
                        
                        existingEpisodes.append(episodeId);
                        qDebug() << "Operations_VP_Shows: Found existing episode:" << episodeId;
                    }
                }
                
                return true; // Found existing show
            }
        }
    }
    
    return false; // No existing show found
}

QStringList Operations_VP_Shows::filterNewEpisodes(const QStringList& candidateFiles, 
                                                  const QStringList& existingEpisodes,
                                                  const QString& showName,
                                                  const QString& language,
                                                  const QString& translation)
{
    qDebug() << "Operations_VP_Shows: Filtering new episodes from" << candidateFiles.size() << "files";
    qDebug() << "Operations_VP_Shows: Existing episodes count:" << existingEpisodes.size();
    
    QStringList newEpisodes;
    
    for (const QString& candidateFile : candidateFiles) {
        QFileInfo fileInfo(candidateFile);
        QString filename = fileInfo.fileName();
        
        // Try to parse season and episode from filename
        int seasonNum = 0, episodeNum = 0;
        VP_ShowsTMDB::parseEpisodeFromFilename(filename, seasonNum, episodeNum);
        
        QString episodeId;
        if (seasonNum > 0 && episodeNum > 0) {
            episodeId = QString("S%1E%2").arg(seasonNum, 2, 10, QChar('0'))
                                        .arg(episodeNum, 2, 10, QChar('0'));
        } else {
            // Use the filename as identifier if parsing failed
            episodeId = filename;
        }
        
        // Check if this episode already exists
        if (!existingEpisodes.contains(episodeId)) {
            newEpisodes.append(candidateFile);
            qDebug() << "Operations_VP_Shows: New episode to import:" << episodeId << "-" << filename;
        } else {
            qDebug() << "Operations_VP_Shows: Episode already exists:" << episodeId << "-" << filename;
        }
    }
    
    qDebug() << "Operations_VP_Shows: Found" << newEpisodes.size() << "new episodes to import";
    return newEpisodes;
}

QString Operations_VP_Shows::generateRandomFileName(const QString& extension)
{
    // Generate a random filename with the specified extension
    // Usage:
    // - ALWAYS Pass "mmvid" for encrypted video files (regardless of original extension)
    // - Pass "" (empty) for folder names and extensionless files (showdesc_, showimage_, showsettings_)
    // - Pass other extensions as needed for non-video files
    const QString chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const int nameLength = 32;
    
    QString randomName;
    for (int i = 0; i < nameLength; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        randomName.append(chars.at(index));
    }
    
    // Add extension if provided
    if (!extension.isEmpty()) {
        randomName += "." + extension;
    }
    
    return randomName;
}

bool Operations_VP_Shows::createShowFolderStructure(QString& outputPath)
{
    // Get username from mainwindow
    QString username = m_mainWindow->user_Username;
    if (username.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Username is empty, cannot create folder structure";
        return false;
    }
    
    // Update username for operations_files if needed
    OperationsFiles::setUsername(username);
    
    // Build the proper path structure: Data/username/Videoplayer/Shows
    // Using the same pattern as operations_encrypteddata.cpp
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    qDebug() << "Operations_VP_Shows: Creating folder structure at:" << showsPath;
    
    // Use operations_files to create the directory hierarchy with proper permissions
    // Create Data/username if it doesn't exist
    if (!OperationsFiles::ensureDirectoryExists(userPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create user directory:" << userPath;
        return false;
    }
    
    // Create Data/username/Videoplayer if it doesn't exist
    if (!OperationsFiles::ensureDirectoryExists(videoplayerPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create Videoplayer directory:" << videoplayerPath;
        return false;
    }
    
    // Create Data/username/Videoplayer/Shows if it doesn't exist
    if (!OperationsFiles::ensureDirectoryExists(showsPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create Shows directory:" << showsPath;
        return false;
    }
    
    // Generate a random folder name for this specific show (no extension needed)
    QString randomFolderName = generateRandomFileName("");
    
    // Create the specific show folder with secure permissions
    QString showFolderPath = QDir(showsPath).absoluteFilePath(randomFolderName);
    if (!OperationsFiles::ensureDirectoryExists(showFolderPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create show folder:" << showFolderPath;
        return false;
    }
    
    outputPath = showFolderPath;
    qDebug() << "Operations_VP_Shows: Successfully created output folder with secure permissions:" << outputPath;
    
    return true;
}

void Operations_VP_Shows::onEncryptionComplete(bool success, const QString& message,
                                              const QStringList& successfulFiles,
                                              const QStringList& failedFiles)
{
    qDebug() << "Operations_VP_Shows: Encryption complete. Success:" << success;
    qDebug() << "Operations_VP_Shows: Message:" << message;
    qDebug() << "Operations_VP_Shows: Successful files:" << successfulFiles.size();
    qDebug() << "Operations_VP_Shows: Failed files:" << failedFiles.size();
    
    // Clear context menu data after operation completes
    m_contextMenuShowName.clear();
    m_contextMenuShowPath.clear();
    m_contextMenuEpisodePaths.clear();
    m_contextMenuEpisodePath.clear();
    
    if (success && !successfulFiles.isEmpty()) {
        // After successful encryption, create or update settings file
        if (!successfulFiles.isEmpty()) {
            // Use the stored output path (the encrypted folder in Data/username/Videoplayer/Shows/)
            // NOT the source folder path
            QString showFolderPath = m_currentImportOutputPath;
            
            if (showFolderPath.isEmpty()) {
                qDebug() << "Operations_VP_Shows: Warning - No output path stored, cannot save settings";
            } else {
                qDebug() << "Operations_VP_Shows: Saving settings to encrypted folder:" << showFolderPath;
                
                // Create settings manager
                VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
                
                VP_ShowsSettings::ShowSettings settings;
                
                // If updating existing show, load existing settings first to preserve show ID and other settings
                if (m_isUpdatingExistingShow) {
                    // Load existing settings to preserve values not changed by the dialog
                    if (!settingsManager.loadShowSettings(showFolderPath, settings)) {
                        qDebug() << "Operations_VP_Shows: Warning - Could not load existing settings, using defaults";
                        settings = VP_ShowsSettings::ShowSettings();  // Use defaults if load fails
                    }
                    qDebug() << "Operations_VP_Shows: Updating existing show settings, preserving show ID:" << settings.showId;
                } else {
                    // New show - start with defaults
                    settings = VP_ShowsSettings::ShowSettings();
                }
                
                // Update settings with values from dialog (stored when dialog was accepted)
                settings.showName = m_dialogShowName;  // Save the show name
                
                // Only update show ID if we have a valid one from dialog (from TMDB selection)
                // Otherwise preserve existing ID (for existing shows) or set to "error" (for new shows)
                if (m_dialogShowId > 0) {
                    settings.showId = QString::number(m_dialogShowId);
                    qDebug() << "Operations_VP_Shows: Setting show ID from dialog:" << settings.showId;
                } else if (!m_isUpdatingExistingShow) {
                    // Only set to "error" for new shows without TMDB selection
                    settings.showId = "error";
                    qDebug() << "Operations_VP_Shows: No TMDB selection for new show, setting show ID to 'error'";
                }
                // For existing shows, showId is already preserved from loading
                
                settings.autoplay = m_dialogAutoplay;
                settings.skipIntro = m_dialogSkipIntro;
                settings.skipOutro = m_dialogSkipOutro;
                settings.useTMDB = m_dialogUseTMDB;
                
                qDebug() << "Operations_VP_Shows: Final settings - ShowID:" << settings.showId 
                         << "UseTMDB:" << settings.useTMDB << "ShowName:" << settings.showName;
                
                // Save the settings file
                if (settingsManager.saveShowSettings(showFolderPath, settings)) {
                    qDebug() << "Operations_VP_Shows: Settings file created/updated successfully";
                } else {
                    qDebug() << "Operations_VP_Shows: Failed to create/update settings file";
                }
            }
        }
        
        // This is always a new show import now
        QString successMessage = tr("TV show imported successfully!\n%1").arg(message);
        
        // Show enhanced success dialog with options for handling original files
        QMessageBox msgBox(m_mainWindow);
        msgBox.setWindowTitle(tr("Import Successful"));
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setText(successMessage + "\n\nChoose how to handle the original video files:");
        
        QPushButton* keepButton = msgBox.addButton(tr("Keep Files"), QMessageBox::RejectRole);
        QPushButton* deleteButton = msgBox.addButton(tr("Delete Files"), QMessageBox::ActionRole);
        QPushButton* secureDeleteButton = msgBox.addButton(tr("Securely Delete Files"), QMessageBox::ActionRole);
        msgBox.setDefaultButton(keepButton);  // Safe default option
        
        msgBox.exec();
        
        // Handle user's choice for original files
        if (msgBox.clickedButton() == deleteButton || msgBox.clickedButton() == secureDeleteButton) {
            bool useSecureDeletion = (msgBox.clickedButton() == secureDeleteButton);
            
            // The successfulFiles list already contains the source file paths that were successfully encrypted
            // We can directly use it for deletion
            QStringList filesToDelete = successfulFiles;
            
            qDebug() << "Operations_VP_Shows: Files to delete:" << filesToDelete.size() << "files";
            for (const QString& file : filesToDelete) {
                qDebug() << "Operations_VP_Shows:   Will delete:" << file;
            }
            
            if (!filesToDelete.isEmpty()) {
                QStringList deletedFiles;
                QStringList deletionFailures;
                
                for (const QString& filePath : filesToDelete) {
                    bool deleted = false;
                    
                    // Check if file exists before attempting deletion
                    if (!QFile::exists(filePath)) {
                        qDebug() << "Operations_VP_Shows: File doesn't exist (already deleted?):" << filePath;
                        deletedFiles.append(QFileInfo(filePath).fileName());
                        continue;
                    }
                    
                    qDebug() << "Operations_VP_Shows: Attempting to delete:" << filePath;
                    
                    if (useSecureDeletion) {
                        // Secure deletion with 3 passes, allow external files
                        qDebug() << "Operations_VP_Shows: Using secure deletion...";
                        deleted = OperationsFiles::secureDelete(filePath, 3, true);
                    } else {
                        // Regular deletion
                        qDebug() << "Operations_VP_Shows: Using regular deletion...";
                        deleted = QFile::remove(filePath);
                    }
                    
                    if (deleted) {
                        deletedFiles.append(QFileInfo(filePath).fileName());
                        qDebug() << "Operations_VP_Shows: Successfully deleted original file:" << filePath;
                    } else {
                        deletionFailures.append(QFileInfo(filePath).fileName());
                        qDebug() << "Operations_VP_Shows: Failed to delete original file:" << filePath;
                        // Try to get more info about why it failed
                        QFileInfo fileInfo(filePath);
                        qDebug() << "Operations_VP_Shows:   File exists:" << fileInfo.exists();
                        qDebug() << "Operations_VP_Shows:   File readable:" << fileInfo.isReadable();
                        qDebug() << "Operations_VP_Shows:   File writable:" << fileInfo.isWritable();
                    }
                }
                
                // After deleting files, clean up empty directories (only for folder-based imports)
                qDebug() << "Operations_VP_Shows: Checking if directory cleanup is needed";
                
                // Only perform cleanup if we have a valid source folder path
                // This will only be set when importing a complete show via folder selection,
                // not when adding individual episode files
                if (m_originalSourceFolderPath.isEmpty()) {
                    qDebug() << "Operations_VP_Shows: No source folder path set (individual files import), skipping directory cleanup";
                } else {
                    qDebug() << "Operations_VP_Shows: Directory cleanup enabled (folder import mode)";
                    qDebug() << "Operations_VP_Shows: Cleanup boundary (original source folder):" << m_originalSourceFolderPath;
                    
                    // Helper function to recursively find all subdirectories
                    std::function<void(const QString&, QSet<QString>&)> collectAllSubdirectories;
                    collectAllSubdirectories = [&collectAllSubdirectories](const QString& path, QSet<QString>& dirs) {
                        QDir dir(path);
                        QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                        for (const QString& subdir : subdirs) {
                            QString subdirPath = dir.absoluteFilePath(subdir);
                            dirs.insert(subdirPath);
                            // Recursively collect subdirectories
                            collectAllSubdirectories(subdirPath, dirs);
                        }
                    };
                    
                    // Collect ALL subdirectories within the source folder
                    QSet<QString> allDirsToCheck;
                    collectAllSubdirectories(m_originalSourceFolderPath, allDirsToCheck);
                    
                    // Also include the source folder itself
                    allDirsToCheck.insert(m_originalSourceFolderPath);
                    
                    qDebug() << "Operations_VP_Shows: Found" << allDirsToCheck.size() << "directories to check for cleanup";
                    
                    // Convert to list and sort by path length (longest first) to delete from deepest level up
                    QStringList sortedDirs = allDirsToCheck.values();
                    std::sort(sortedDirs.begin(), sortedDirs.end(), 
                             [](const QString& a, const QString& b) { return a.length() > b.length(); });
                    
                    // Try to remove empty directories
                    int removedDirCount = 0;
                    for (const QString& dirPath : sortedDirs) {
                        QDir dir(dirPath);
                        if (dir.exists()) {
                            // Check if directory is empty (only contains . and ..)
                            QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
                            if (entries.isEmpty()) {
                                qDebug() << "Operations_VP_Shows: Found empty directory:" << dirPath;
                                
                                // Try to remove the empty directory
                                QString dirName = dir.dirName();
                                if (dir.cdUp()) {
                                    if (dir.rmdir(dirName)) {
                                        removedDirCount++;
                                        qDebug() << "Operations_VP_Shows: Successfully removed empty directory:" << dirPath;
                                    } else {
                                        qDebug() << "Operations_VP_Shows: Failed to remove empty directory:" << dirPath;
                                    }
                                }
                            } else {
                                qDebug() << "Operations_VP_Shows: Directory not empty, skipping:" << dirPath << "(contains" << entries.size() << "items)";
                            }
                        }
                    }
                    
                    if (removedDirCount > 0) {
                        qDebug() << "Operations_VP_Shows: Cleaned up" << removedDirCount << "empty directories";
                    } else {
                        qDebug() << "Operations_VP_Shows: No empty directories found to clean up";
                    }
                    
                    // Clear the stored path after cleanup
                    m_originalSourceFolderPath.clear();
                }
                
                // Show deletion results if there were any failures
                if (!deletionFailures.isEmpty()) {
                    QString deletionMessage = tr("Successfully deleted %1 file(s).\n\nFailed to delete:\n%2")
                        .arg(deletedFiles.size())
                        .arg(deletionFailures.join("\n"));
                    
                    QMessageBox::warning(m_mainWindow,
                                       useSecureDeletion ? tr("Secure Deletion Results") : tr("Deletion Results"),
                                       deletionMessage);
                }
                // If all deletions succeeded, no additional dialog is shown (silent success)
            }
        }
        // If keepButton was clicked, do nothing with the original files
        
        // Refresh the show list in the UI
        refreshTVShowsList();
    } else if (!success) {
        QString detailedMessage = message;
        if (!failedFiles.isEmpty()) {
            detailedMessage += "\n\nFailed files:\n";
            for (const QString& file : failedFiles) {
                QFileInfo fileInfo(file);
                detailedMessage += "- " + fileInfo.fileName() + "\n";
            }
        }
        
        QMessageBox::warning(m_mainWindow,
                           tr("Import Failed"),
                           detailedMessage);
        
        // Clean up any partially created folders if all files failed
        if (successfulFiles.isEmpty() && !failedFiles.isEmpty()) {
            qDebug() << "Operations_VP_Shows: All files failed for new show, cleaning up created folders";
            cleanupEmptyShowFolder(m_currentImportOutputPath);
        }
    }
    
    // Also check for cleanup on cancellation (when success is false and both lists are empty)
    if (!success && successfulFiles.isEmpty() && failedFiles.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Import was cancelled, checking for cleanup";
        if (!m_currentImportOutputPath.isEmpty()) {
            cleanupEmptyShowFolder(m_currentImportOutputPath);
        }
    }
    
    refreshTVShowsList();
    
    // Check if we need to reload the episode tree for the currently displayed show
    // This happens when adding episodes via context menu from either the display page or show list
    if (!m_currentImportOutputPath.isEmpty() && !m_currentShowFolder.isEmpty() && 
        m_currentImportOutputPath == m_currentShowFolder) {
        qDebug() << "Operations_VP_Shows: Added episodes to currently displayed show, reloading episode tree";
        loadShowEpisodes(m_currentShowFolder);

    }

    // Clear the stored output path
    m_currentImportOutputPath.clear();
    
    // Reset the flags (though m_isUpdatingExistingShow is always false now)
    m_isUpdatingExistingShow = false;
    m_originalEpisodeCount = 0;
    m_newEpisodeCount = 0;
}

void Operations_VP_Shows::cleanupEmptyShowFolder(const QString& folderPath)
{
    if (folderPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No folder path provided for cleanup";
        return;
    }
    
    // Validate the folder path
    if (!OperationsFiles::isWithinAllowedDirectory(folderPath, "Data")) {
        qDebug() << "Operations_VP_Shows: Folder path is outside allowed directory:" << folderPath;
        return;
    }
    
    QDir showDir(folderPath);
    if (!showDir.exists()) {
        qDebug() << "Operations_VP_Shows: Folder doesn't exist:" << folderPath;
        return;
    }
    
    // Check for .mmvid files in the folder
    QStringList videoExtensions;
    videoExtensions << "*.mmvid";
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files);
    
    if (videoFiles.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No video files found in folder, deleting:" << folderPath;
        
        // First delete all non-video files in the folder
        showDir.setNameFilters(QStringList());
        QStringList allFiles = showDir.entryList(QDir::Files);
        
        for (const QString& file : allFiles) {
            QString filePath = showDir.absoluteFilePath(file);
            if (!QFile::remove(filePath)) {
                qDebug() << "Operations_VP_Shows: Failed to delete file:" << filePath;
            }
        }
        
        // Now remove the empty directory
        QString folderName = showDir.dirName();
        if (showDir.cdUp()) {
            if (showDir.rmdir(folderName)) {
                qDebug() << "Operations_VP_Shows: Successfully deleted empty show folder:" << folderPath;
            } else {
                qDebug() << "Operations_VP_Shows: Failed to delete folder:" << folderPath;
            }
        }
    } else {
        qDebug() << "Operations_VP_Shows: Found" << videoFiles.size() << "video files, keeping folder:" << folderPath;
    }
}

void Operations_VP_Shows::cleanupIncompleteShowFolders()
{
    qDebug() << "Operations_VP_Shows: Starting cleanup of incomplete show folders";
    
    // Get username from mainwindow
    QString username = m_mainWindow->user_Username;
    if (username.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Username is empty, skipping cleanup";
        return;
    }
    
    // Build the shows path
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    QDir showsDir(showsPath);
    if (!showsDir.exists()) {
        qDebug() << "Operations_VP_Shows: Shows directory does not exist, skipping cleanup";
        return;
    }
    
    // Get all subdirectories in the shows folder
    QStringList showFolders = showsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    if (showFolders.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show folders found";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Checking" << showFolders.size() << "show folders for incomplete imports";
    
    int foldersDeleted = 0;
    
    // Check each show folder
    for (const QString& folderName : showFolders) {
        QString folderPath = showsDir.absoluteFilePath(folderName);
        QDir showFolder(folderPath);
        
        // Check for .mmvid files
        QStringList videoExtensions;
        videoExtensions << "*.mmvid";
        showFolder.setNameFilters(videoExtensions);
        QStringList videoFiles = showFolder.entryList(QDir::Files);
        
        if (videoFiles.isEmpty()) {
            qDebug() << "Operations_VP_Shows: Found incomplete show folder (no video files):" << folderName;
            
            // Delete all files in the folder first
            showFolder.setNameFilters(QStringList());
            QStringList allFiles = showFolder.entryList(QDir::Files);
            
            bool deletionSuccess = true;
            for (const QString& file : allFiles) {
                QString filePath = showFolder.absoluteFilePath(file);
                if (!QFile::remove(filePath)) {
                    qDebug() << "Operations_VP_Shows: Failed to delete file:" << filePath;
                    deletionSuccess = false;
                }
            }
            
            // Try to remove the directory if all files were deleted
            if (deletionSuccess) {
                if (showFolder.cdUp() && showsDir.rmdir(folderName)) {
                    qDebug() << "Operations_VP_Shows: Successfully deleted incomplete show folder:" << folderName;
                    foldersDeleted++;
                } else {
                    qDebug() << "Operations_VP_Shows: Failed to delete folder:" << folderName;
                }
            }
        }
    }
    
    if (foldersDeleted > 0) {
        qDebug() << "Operations_VP_Shows: Cleanup completed. Deleted" << foldersDeleted << "incomplete show folders";
    } else {
        qDebug() << "Operations_VP_Shows: Cleanup completed. No incomplete show folders found";
    }
}

// ============================================================================
// Playback Speed Management - Removed (now handled globally in BaseVideoPlayer)
// ============================================================================

void Operations_VP_Shows::loadTVShowsList()
{
    qDebug() << "Operations_VP_Shows: Loading TV shows list";
    
    // Check if we have the required UI elements
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        qDebug() << "Operations_VP_Shows: UI elements not ready for loading shows list";
        return;
    }
    
    // Make sure we have username and key
    if (m_mainWindow->user_Username.isEmpty() || m_mainWindow->user_Key.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Username or key not available yet";
        return;
    }
    
    // Clear the list widget and the mapping first
    m_mainWindow->ui->listWidget_VP_List_List->clear();
    m_showFolderMapping.clear();
    m_posterCache.clear();  // Clear poster cache when reloading
    
    // Build the path to the shows directory
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString videoplayerPath = QDir(userPath).absoluteFilePath("Videoplayer");
    QString showsPath = QDir(videoplayerPath).absoluteFilePath("Shows");
    
    qDebug() << "Operations_VP_Shows: Shows directory path:" << showsPath;
    
    // Check if the shows directory exists
    QDir showsDir(showsPath);
    if (!showsDir.exists()) {
        qDebug() << "Operations_VP_Shows: Shows directory does not exist yet";
        return;
    }
    
    // Get all subdirectories in the shows folder
    QStringList showFolders = showsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    if (showFolders.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show folders found";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Found" << showFolders.size() << "show folders";
    
    // Create managers for reading settings and metadata
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Process each show folder
    for (const QString& folderName : showFolders) {
        QString folderPath = showsDir.absoluteFilePath(folderName);
        QDir showFolder(folderPath);
        QString showName;
        
        // First, try to load the show name from settings file
        VP_ShowsSettings::ShowSettings settings;
        bool hasShowNameFromSettings = false;
        
        if (settingsManager.loadShowSettings(folderPath, settings)) {
            if (!settings.showName.isEmpty()) {
                showName = settings.showName;
                hasShowNameFromSettings = true;
                qDebug() << "Operations_VP_Shows: Found show from settings:" << showName;
            }
        }
        
        // If no show name in settings, fall back to reading from video metadata
        if (!hasShowNameFromSettings) {
            qDebug() << "Operations_VP_Shows: No show name in settings for folder:" << folderName;
            qDebug() << "Operations_VP_Shows: Attempting to read from video metadata";
            
            // Find video files in this folder
            QStringList videoExtensions;
            videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
            
            showFolder.setNameFilters(videoExtensions);
            QStringList videoFiles = showFolder.entryList(QDir::Files);
            
            if (videoFiles.isEmpty()) {
                qDebug() << "Operations_VP_Shows: No video files found in folder:" << folderName;
                continue;
            }
            
            // Try each video file until we find one with valid metadata
            bool foundValidMetadata = false;
            for (const QString& videoFile : videoFiles) {
                QString videoPath = showFolder.absoluteFilePath(videoFile);
                VP_ShowsMetadata::ShowMetadata metadata;
                
                if (metadataManager.readMetadataFromFile(videoPath, metadata)) {
                    if (!metadata.showName.isEmpty()) {
                        showName = metadata.showName;
                        foundValidMetadata = true;
                        qDebug() << "Operations_VP_Shows: Found show from video metadata:" << showName;
                        qDebug() << "Operations_VP_Shows: Read from file:" << videoFile;
                        
                        // Save the show name to settings for future use
                        settings.showName = showName;
                        if (!settingsManager.saveShowSettings(folderPath, settings)) {
                            qDebug() << "Operations_VP_Shows: Warning - Failed to save show name to settings";
                        }
                        break;
                    }
                }
            }
            
            if (!foundValidMetadata) {
                qDebug() << "Operations_VP_Shows: Could not read show name from any video in folder:" << folderName;
                qDebug() << "Operations_VP_Shows: This folder may contain only corrupted videos";
                continue;
            }
        }
        
        // Add the show to the list if we have a valid name
        if (!showName.isEmpty()) {
            // Add the show name to the list widget
            QListWidgetItem* item = new QListWidgetItem();
            
            // Store the folder path as user data for later use (when playing videos)
            item->setData(Qt::UserRole, folderPath);
            
            // Store the mapping in RAM for quick access (thread-safe)
            m_showFolderMapping.insert(showName, folderPath);
            
            // Set up the item based on current view mode
            refreshShowListItem(item, showName, folderPath);
            
            m_mainWindow->ui->listWidget_VP_List_List->addItem(item);
        }
    }
    
    // Sort the list alphabetically
    if (validateListWidget(m_mainWindow->ui->listWidget_VP_List_List)) {
        m_mainWindow->ui->listWidget_VP_List_List->sortItems(Qt::AscendingOrder);
    }
    
    qDebug() << "Operations_VP_Shows: Finished loading shows. Total shows:" 
             << safeGetListItemCount(m_mainWindow->ui->listWidget_VP_List_List);
}

void Operations_VP_Shows::refreshTVShowsList()
{
    qDebug() << "Operations_VP_Shows: Refreshing TV shows list";
    
    // Simply call loadTVShowsList to reload the entire list
    loadTVShowsList();
    
    // Update Add Episodes button state after refreshing
    onShowListSelectionChanged();
}

void Operations_VP_Shows::onViewModeChanged(int index)
{
    qDebug() << "Operations_VP_Shows: View mode changed to index:" << index;
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        qDebug() << "Operations_VP_Shows: UI elements not available for view mode change";
        return;
    }
    
    // Index 1 = List view, Index 0 = Icon view
    m_isIconViewMode = (index == 0);
    
    if (m_isIconViewMode) {
        qDebug() << "Operations_VP_Shows: Switching to Icon view mode";
        setupIconViewMode();
    } else {
        qDebug() << "Operations_VP_Shows: Switching to List view mode";
        setupListViewMode();
    }
    
    // Refresh all items with the new view mode
    int itemCount = safeGetListItemCount(m_mainWindow->ui->listWidget_VP_List_List);
    for (int i = 0; i < itemCount; ++i) {
        QListWidgetItem* item = safeGetListItem(m_mainWindow->ui->listWidget_VP_List_List, i);
        if (item) {
            QString folderPath = item->data(Qt::UserRole).toString();
            QString showName = item->text();
            
            // Find the actual show name if we have it in the mapping (thread-safe)
            m_showFolderMapping.safeIterate([&showName, &folderPath](const QString& key, const QString& value) {
                if (value == folderPath) {
                    showName = key;
                }
            });
            
            refreshShowListItem(item, showName, folderPath);
        }
    }
}

void Operations_VP_Shows::setupListViewMode()
{
    qDebug() << "Operations_VP_Shows: Setting up List view mode";
    
    qlist_VP_ShowsList* listWidget = qobject_cast<qlist_VP_ShowsList*>(m_mainWindow->ui->listWidget_VP_List_List);
    
    if (!listWidget) {
        qDebug() << "Critical-Operations_VP_Shows: Failed to cast to qlist_VP_ShowsList";
        return;
    }
    
    // Set to list mode
    listWidget->setViewMode(QListView::ListMode);
    listWidget->setResizeMode(QListView::Fixed);
    listWidget->setSpacing(0);
    listWidget->setUniformItemSizes(true);
    
    // Reset icon size to default for list mode
    listWidget->setIconSize(QSize(16, 16));
    
    // Clear grid size for list mode (this might be causing the issue)
    listWidget->setGridSize(QSize());
    
    // Set layout mode
    listWidget->setFlow(QListView::TopToBottom);
    listWidget->setWrapping(false);
    
    qDebug() << "Operations_VP_Shows: List view mode configured with tighter spacing";
}

void Operations_VP_Shows::setupIconViewMode()
{
    qDebug() << "Operations_VP_Shows: Setting up Icon view mode";
    
    qlist_VP_ShowsList* listWidget = qobject_cast<qlist_VP_ShowsList*>(m_mainWindow->ui->listWidget_VP_List_List);
    
    if (!listWidget) {
        qDebug() << "Critical-Operations_VP_Shows: Failed to cast to qlist_VP_ShowsList";
        return;
    }
    
    // Set to icon mode
    listWidget->setViewMode(QListView::IconMode);
    listWidget->setResizeMode(QListView::Adjust);
    listWidget->setSpacing(10);
    listWidget->setUniformItemSizes(true);
    
    // Set larger icon size for posters (aspect ratio approximately 2:3 for movie posters)
    listWidget->setIconSize(QSize(100, 150));
    
    // Set grid size to accommodate icon + text
    listWidget->setGridSize(QSize(120, 190));
    
    // Set layout mode
    listWidget->setFlow(QListView::LeftToRight);
    listWidget->setWrapping(true);
    
    // Enable word wrap for long show names
    listWidget->setWordWrap(true);
    
    // Disable drag and drop to prevent icon movement
    listWidget->setDragDropMode(QAbstractItemView::NoDragDrop);
    listWidget->setMovement(QListView::Static);
    listWidget->setDragEnabled(false);
    
    // Set the scroll speed multiplier for icon view (10x faster by default)
    // You can adjust this value to fine-tune the scrolling speed
    listWidget->setIconViewScrollMultiplier(10.0);
    
    qDebug() << "Operations_VP_Shows: Icon view mode configured with drag/drop disabled and 10x scroll speed";
}

void Operations_VP_Shows::setIconViewScrollMultiplier(double multiplier)
{
    qDebug() << "Operations_VP_Shows: Setting icon view scroll multiplier to:" << multiplier;
    
    qlist_VP_ShowsList* listWidget = qobject_cast<qlist_VP_ShowsList*>(m_mainWindow->ui->listWidget_VP_List_List);
    
    if (listWidget) {
        listWidget->setIconViewScrollMultiplier(multiplier);
    } else {
        qDebug() << "Critical-Operations_VP_Shows: Failed to cast to qlist_VP_ShowsList when setting scroll multiplier";
    }
}

double Operations_VP_Shows::getIconViewScrollMultiplier() const
{
    qlist_VP_ShowsList* listWidget = qobject_cast<qlist_VP_ShowsList*>(m_mainWindow->ui->listWidget_VP_List_List);
    
    if (listWidget) {
        return listWidget->getIconViewScrollMultiplier();
    } else {
        qDebug() << "Critical-Operations_VP_Shows: Failed to cast to qlist_VP_ShowsList when getting scroll multiplier";
        return 1.0;  // Return default value if cast fails
    }
}

void Operations_VP_Shows::refreshShowListItem(QListWidgetItem* item, const QString& showName, const QString& folderPath)
{
    if (!item) {
        qDebug() << "Operations_VP_Shows: Invalid item provided to refreshShowListItem";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Refreshing item for show:" << showName;
    
    // Always set the text
    item->setText(showName);
    
    if (m_isIconViewMode) {
        // Load and set the poster icon
        QSize iconSize = m_mainWindow->ui->listWidget_VP_List_List->iconSize();
        QPixmap poster = loadShowPoster(folderPath, iconSize);
        
        if (!poster.isNull()) {
            item->setIcon(QIcon(poster));
            qDebug() << "Operations_VP_Shows: Set poster icon for show:" << showName;
        } else {
            // Set a placeholder icon
            QPixmap placeholder(iconSize);
            placeholder.fill(Qt::darkGray);
            
            // Draw text on placeholder
            QPainter painter(&placeholder);
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            painter.drawText(placeholder.rect(), Qt::AlignCenter | Qt::TextWordWrap, "No\nPoster");
            
            item->setIcon(QIcon(placeholder));
            qDebug() << "Operations_VP_Shows: Set placeholder icon for show:" << showName;
        }
        
        // Set text alignment for icon mode
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    } else {
        // In list mode, clear the icon or set a small one
        item->setIcon(QIcon());  // Clear icon in list mode
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        
        // Reset size hint to default for list mode to prevent large gaps
        item->setSizeHint(QSize());
    }
    
    // Force layout update after changing item properties
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->listWidget_VP_List_List) {
        m_mainWindow->ui->listWidget_VP_List_List->doItemsLayout();
    }
}

QPixmap Operations_VP_Shows::loadShowPoster(const QString& showFolderPath, const QSize& targetSize)
{
    qDebug() << "Operations_VP_Shows: Loading poster for show folder:" << showFolderPath;
    
    // Check cache first (thread-safe)
    auto cachedPoster = m_posterCache.value(showFolderPath);
    if (cachedPoster.has_value()) {
        qDebug() << "Operations_VP_Shows: Found poster in cache";
        return cachedPoster.value();
    }
    
    // Get the obfuscated folder name
    QDir showDir(showFolderPath);
    QString obfuscatedName = showDir.dirName();
    
    // Look for showimage_ file
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    qDebug() << "Operations_VP_Shows: Looking for poster file:" << imageFilePath;
    
    if (!QFile::exists(imageFilePath)) {
        qDebug() << "Operations_VP_Shows: No poster file found";
        return QPixmap();
    }
    
    // Read and decrypt the poster
    QFile file(imageFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open poster file";
        return QPixmap();
    }
    
    QByteArray encryptedData = file.readAll();
    file.close();
    
    // Decrypt the image data
    QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(
        m_mainWindow->user_Key, encryptedData);
    
    if (decryptedData.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Failed to decrypt poster data";
        return QPixmap();
    }
    
    // Load the decrypted image
    QPixmap poster;
    if (!poster.loadFromData(decryptedData)) {
        qDebug() << "Operations_VP_Shows: Failed to load poster from decrypted data";
        return QPixmap();
    }
    
    // Scale to target size
    QPixmap scaledPoster = poster.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // Cache the scaled poster (thread-safe)
    m_posterCache.insert(showFolderPath, scaledPoster);
    
    qDebug() << "Operations_VP_Shows: Successfully loaded and cached poster, size:" << scaledPoster.size();
    return scaledPoster;
}


void Operations_VP_Shows::openShowSettings()
{
    qDebug() << "Operations_VP_Shows: Opening show-specific settings dialog";
    
    // Check if we have a current show
    if (m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show currently selected";
        QMessageBox::information(m_mainWindow, "No Show Selected", 
                                "Please select a show before opening settings.");
        return;
    }
    
    // Get the show name from the folder
    QDir showDir(m_currentShowFolder);
    QString showName = showDir.dirName();
    
    // Decrypt the show name if it's obfuscated
    QString decryptedShowName = CryptoUtils::Encryption_Decrypt(m_mainWindow->user_Key, showName);
    if (decryptedShowName.isEmpty() || decryptedShowName == showName) {
        // If decryption failed or returned the same string, it might not be encrypted
        decryptedShowName = showName;
    }
    
    VP_ShowsSettingsDialog settingsDialog(decryptedShowName, m_currentShowFolder, m_mainWindow);
    
    // Store the original show name before opening dialog
    QString originalShowName = decryptedShowName;
    
    // Show the dialog modally
    int dialogResult = settingsDialog.exec();
    
    // Check if watch history was reset - this needs to refresh the tree even if dialog was cancelled
    if (settingsDialog.wasWatchHistoryReset()) {
        qDebug() << "Operations_VP_Shows: Watch history was reset, reloading watch history and refreshing episode tree";
        
        // Force reload of watch history from disk to get the reset data
        if (m_watchHistory) {
            qDebug() << "Operations_VP_Shows: Reloading watch history from disk after reset";
            if (!m_watchHistory->loadHistory()) {
                qDebug() << "Operations_VP_Shows: Failed to reload watch history after reset";
            } else {
                qDebug() << "Operations_VP_Shows: Successfully reloaded watch history after reset";
            }
        }
        
        // Now refresh the tree widget with the updated watch history
        loadShowEpisodes(m_currentShowFolder);
        
        // Also explicitly refresh the colors to ensure they're updated
        refreshEpisodeTreeColors();
    }
    
    if (dialogResult == QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Show settings saved";
        
        // Reload show settings first to update m_currentShowSettings with the new values
        loadShowSettings(m_currentShowFolder);
        
        // Check if TMDB data was updated or display file names setting changed and reload tree widget if needed
        // Note: We already checked watch history above, so we don't need to check it again here
        if (!settingsDialog.wasWatchHistoryReset() && 
            (settingsDialog.wasTMDBDataUpdated() || settingsDialog.wasDisplayFileNamesChanged())) {
            qDebug() << "Operations_VP_Shows: Tree refresh needed - TMDB:" << settingsDialog.wasTMDBDataUpdated()
                     << "DisplayFileNames:" << settingsDialog.wasDisplayFileNamesChanged();
            // Now loadShowEpisodes will use the updated m_currentShowSettings values
            loadShowEpisodes(m_currentShowFolder);
        }
        
        // Update the show name display if it changed
        // We need to reload the metadata to get the updated show name
        VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        
        // Find any video file to get the updated show name  
        QDir showDir(m_currentShowFolder);
        QStringList videoExtensions;
        videoExtensions << "*.mmvid";  // Use the correct extension for encrypted video files
        showDir.setNameFilters(videoExtensions);
        QStringList videoFiles = showDir.entryList(QDir::Files);
        
        QString updatedShowName;
        if (!videoFiles.isEmpty()) {
            QString firstVideoPath = showDir.absoluteFilePath(videoFiles.first());
            VP_ShowsMetadata::ShowMetadata metadata;
            
            if (metadataManager.readMetadataFromFile(firstVideoPath, metadata)) {
                updatedShowName = metadata.showName;
                // Update the show name display
                if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
                    m_mainWindow->ui->label_VP_Shows_Display_Name->setText(metadata.showName);
                    qDebug() << "Operations_VP_Shows: Updated show name display to:" << metadata.showName;
                }
            }
        }
        
        // Check if the show name actually changed and refresh the shows list if it did
        if (!updatedShowName.isEmpty() && updatedShowName != originalShowName) {
            qDebug() << "Operations_VP_Shows: Show name changed from '" << originalShowName << "' to '" << updatedShowName << "', refreshing shows list";
            refreshTVShowsList();
        }
        
        // Update the show image display
        if (m_mainWindow->ui->label_VP_Shows_Display_Image) {
            QPixmap showImage = loadShowImage(m_currentShowFolder);
            
            if (!showImage.isNull()) {
                // Get the actual size of the label widget
                QSize labelSize = m_mainWindow->ui->label_VP_Shows_Display_Image->size();
                
                // Scale the image to fit the label
                QPixmap scaledImage = showImage.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                m_mainWindow->ui->label_VP_Shows_Display_Image->setPixmap(scaledImage);
                
                qDebug() << "Operations_VP_Shows: Updated show image display";
            } else {
                // Set a placeholder text if no image is available
                m_mainWindow->ui->label_VP_Shows_Display_Image->setText(tr("No Image Available"));
                qDebug() << "Operations_VP_Shows: No image available for show";
            }
        }
        
        // Update the show description display
        if (m_mainWindow->ui->textBrowser_VP_Shows_Display_Description) {
            QString description = loadShowDescription(m_currentShowFolder);
            
            if (!description.isEmpty()) {
                m_mainWindow->ui->textBrowser_VP_Shows_Display_Description->setPlainText(description);
                qDebug() << "Operations_VP_Shows: Updated show description display";
            } else {
                m_mainWindow->ui->textBrowser_VP_Shows_Display_Description->setPlainText(tr("No description available."));
                qDebug() << "Operations_VP_Shows: No description available for show";
            }
        }
        
        // Refresh the TV shows list to reflect any changes
        refreshTVShowsList();
    } else {
        qDebug() << "Operations_VP_Shows: Show settings dialog cancelled";
    }
}

bool Operations_VP_Shows::saveShowDescription(const QString& showFolderPath, const QString& description)
{
    qDebug() << "Operations_VP_Shows: Saving show description to folder:" << showFolderPath;
    
    if (description.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Description is empty, skipping save";
        return true; // Not an error, just nothing to save
    }
    
    // Generate the obfuscated folder name
    QDir showDir(showFolderPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showdesc_
    QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descFilePath = showDir.absoluteFilePath(descFileName);
    
    // Encrypt and save the description
    return OperationsFiles::writeEncryptedFile(descFilePath, m_mainWindow->user_Key, description);
}

QString Operations_VP_Shows::loadShowDescription(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Loading show description from folder:" << showFolderPath;
    
    // Generate the obfuscated folder name
    QDir showDir(showFolderPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showdesc_
    QString descFileName = QString("showdesc_%1").arg(obfuscatedName);
    QString descFilePath = showDir.absoluteFilePath(descFileName);
    
    // Check if the file exists
    if (!QFile::exists(descFilePath)) {
        qDebug() << "Operations_VP_Shows: Description file does not exist:" << descFilePath;
        return QString();
    }
    
    // Read and decrypt the description
    QString description;
    if (OperationsFiles::readEncryptedFile(descFilePath, m_mainWindow->user_Key, description)) {
        return description;
    }
    
    qDebug() << "Operations_VP_Shows: Failed to read description file";
    return QString();
}

bool Operations_VP_Shows::saveShowImage(const QString& showFolderPath, const QByteArray& imageData)
{
    qDebug() << "Operations_VP_Shows: Saving show image to folder:" << showFolderPath;
    
    if (imageData.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Image data is empty, skipping save";
        return true; // Not an error, just nothing to save
    }
    
    // Generate the obfuscated folder name
    QDir showDir(showFolderPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showimage_
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    // Encrypt the image data
    QByteArray encryptedData = CryptoUtils::Encryption_EncryptBArray(m_mainWindow->user_Key, imageData, m_mainWindow->user_Username);
    
    if (encryptedData.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Failed to encrypt image data";
        return false;
    }
    
    // Save the encrypted image data to file
    QFile file(imageFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open image file for writing:" << file.errorString();
        return false;
    }
    
    qint64 written = file.write(encryptedData);
    file.close();
    
    if (written != encryptedData.size()) {
        qDebug() << "Operations_VP_Shows: Failed to write complete image data";
        return false;
    }
    
    qDebug() << "Operations_VP_Shows: Successfully saved show image";
    return true;
}

QPixmap Operations_VP_Shows::loadShowImage(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Loading show image from folder:" << showFolderPath;
    
    // Generate the obfuscated folder name
    QDir showDir(showFolderPath);
    QString obfuscatedName = showDir.dirName();
    
    // Create the filename with prefix showimage_
    QString imageFileName = QString("showimage_%1").arg(obfuscatedName);
    QString imageFilePath = showDir.absoluteFilePath(imageFileName);
    
    // Check if the file exists
    if (!QFile::exists(imageFilePath)) {
        qDebug() << "Operations_VP_Shows: Image file does not exist:" << imageFilePath;
        return QPixmap();
    }
    
    // Read the encrypted image data
    QFile file(imageFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open image file for reading:" << file.errorString();
        return QPixmap();
    }
    
    QByteArray encryptedData = file.readAll();
    file.close();
    
    if (encryptedData.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Image file is empty";
        return QPixmap();
    }
    
    // Decrypt the image data
    QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(m_mainWindow->user_Key, encryptedData);
    
    if (decryptedData.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Failed to decrypt image data";
        return QPixmap();
    }
    
    // Convert to QPixmap
    QPixmap pixmap;
    if (!pixmap.loadFromData(decryptedData)) {
        qDebug() << "Operations_VP_Shows: Failed to load pixmap from decrypted data";
        return QPixmap();
    }
    
    qDebug() << "Operations_VP_Shows: Successfully loaded show image";
    return pixmap;
}

QPixmap Operations_VP_Shows::addNewEpisodeIndicator(const QPixmap& originalPoster, int newEpisodeCount)
{
    if (originalPoster.isNull()) {
        return originalPoster;
    }
    
    // Create a copy of the original poster to draw on
    QPixmap result = originalPoster;
    
    // Create a painter to draw the indicator
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Calculate dimensions for the indicator
    int posterWidth = result.width();
    int posterHeight = result.height();
    
    // Indicator size - make it proportional to poster size
    int indicatorWidth = posterWidth * 0.35;  // 35% of poster width
    int indicatorHeight = posterHeight * 0.12; // 12% of poster height
    
    // Minimum and maximum sizes for readability
    indicatorWidth = qMax(60, qMin(indicatorWidth, 120));
    indicatorHeight = qMax(25, qMin(indicatorHeight, 40));
    
    // Position in top-right corner with small margin
    int margin = 5;
    int x = posterWidth - indicatorWidth - margin;
    int y = margin;
    
    // Draw a semi-transparent background for the indicator
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 0, 0, 200)); // Red with some transparency
    
    // Draw rounded rectangle
    int cornerRadius = indicatorHeight / 4;
    painter.drawRoundedRect(x, y, indicatorWidth, indicatorHeight, cornerRadius, cornerRadius);
    
    // Draw text
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(indicatorHeight * 0.6); // Font size proportional to indicator height
    painter.setFont(font);
    
    // Prepare text
    QString text;
    if (newEpisodeCount > 1) {
        text = QString("NEW (%1)").arg(newEpisodeCount);
    } else {
        text = "NEW";
    }
    
    // Draw text centered in the indicator
    QRect textRect(x, y, indicatorWidth, indicatorHeight);
    painter.drawText(textRect, Qt::AlignCenter, text);
    
    painter.end();
    
    return result;
}

void Operations_VP_Shows::displayShowDetails(const QString& showName, const QString& folderPath)
{
    qDebug() << "Operations_VP_Shows: Displaying details for show:" << showName;

    // Check if we have the required UI elements
    if (!m_mainWindow || !m_mainWindow->ui) {
        qDebug() << "Operations_VP_Shows: UI elements not available";
        return;
    }

    QString actualFolderPath = folderPath;
    QString actualShowName = showName;

    // If show name is empty (called from persistent settings), load it from settings or metadata
    if (actualShowName.isEmpty() && !actualFolderPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Show name is empty, loading from settings or metadata";

        // Create managers for reading settings and metadata
        VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);

        // First, try to load the show name from settings file
        VP_ShowsSettings::ShowSettings settings;
        if (settingsManager.loadShowSettings(actualFolderPath, settings)) {
            if (!settings.showName.isEmpty()) {
                actualShowName = settings.showName;
                qDebug() << "Operations_VP_Shows: Loaded show name from settings:" << actualShowName;
            }
        }

        // If still empty, try to load from video metadata
        if (actualShowName.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No show name in settings, trying metadata";
            QDir showFolder(actualFolderPath);
            QStringList videoExtensions;
            videoExtensions << "*.mmvid";
            showFolder.setNameFilters(videoExtensions);
            QStringList videoFiles = showFolder.entryList(QDir::Files);

            if (!videoFiles.isEmpty()) {
                QString firstVideoPath = showFolder.absoluteFilePath(videoFiles.first());
                VP_ShowsMetadata::ShowMetadata metadata;

                if (metadataManager.readMetadataFromFile(firstVideoPath, metadata)) {
                    if (!metadata.showName.isEmpty()) {
                        actualShowName = metadata.showName;
                        qDebug() << "Operations_VP_Shows: Loaded show name from metadata:" << actualShowName;
                    }
                }
            }
        }

        // If still empty, use folder name as fallback
        if (actualShowName.isEmpty()) {
            QDir dir(actualFolderPath);
            actualShowName = dir.dirName();
            qDebug() << "Operations_VP_Shows: Using folder name as fallback:" << actualShowName;
        }
    }

    // If no folder path provided, try to look it up from the mapping
    if (actualFolderPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No folder path provided, looking up from mapping";
        // Get the folder path for this show (thread-safe)
        auto folderPathOpt = m_showFolderMapping.value(showName);
        if (!folderPathOpt.has_value()) {
            qDebug() << "Operations_VP_Shows: Show not found in mapping:" << showName;
            QMessageBox::warning(m_mainWindow, tr("Show Not Found"),
                               tr("Could not find the folder for this show. Please refresh the list."));
            return;
        }
        actualFolderPath = folderPathOpt.value();
    } else {
        qDebug() << "Operations_VP_Shows: Using provided folder path:" << actualFolderPath;
    }
    
    QString showFolderPath = actualFolderPath;
    qDebug() << "Operations_VP_Shows: Show folder path:" << showFolderPath;
    
    // Initialize watch history for direct access (needed for context menu)
    // Only recreate if it doesn't exist or if we're switching to a different show
    bool needNewWatchHistory = false;
    
    if (!m_watchHistory) {
        needNewWatchHistory = true;
        qDebug() << "Operations_VP_Shows: Initializing watch history for show:" << showFolderPath;
    } else if (m_currentShowFolder != showFolderPath) {
        // Different show, need to recreate
        needNewWatchHistory = true;
        qDebug() << "Operations_VP_Shows: Re-initializing watch history for different show:" << showFolderPath;
    } else {
        // Same show, just reload the history to get latest data
        qDebug() << "Operations_VP_Shows: Reloading watch history for current show:" << showFolderPath;
        if (!m_watchHistory->loadHistory()) {
            qDebug() << "Operations_VP_Shows: Failed to reload history, recreating";
            needNewWatchHistory = true;
        }
    }
    
    if (needNewWatchHistory) {
        m_watchHistory.reset();
        m_watchHistory = std::make_unique<VP_ShowsWatchHistory>(
            showFolderPath,
            m_mainWindow->user_Key,
            m_mainWindow->user_Username
        );

        // Try to load existing history
        if (!m_watchHistory->loadHistory()) {
            qDebug() << "Operations_VP_Shows: No existing history found, creating new";
            m_watchHistory->saveHistory();
        } else {
            qDebug() << "Operations_VP_Shows: Loaded existing watch history";
        }
    }
    
    // Initialize favourites manager for this show
    if (m_mainWindow && !m_mainWindow->user_Key.isEmpty() && !m_mainWindow->user_Username.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Initializing favourites manager for show";
        m_showFavourites = std::make_unique<VP_ShowsFavourites>(
            showFolderPath,
            m_mainWindow->user_Key,
            m_mainWindow->user_Username
        );
        
        // Try to load existing favourites
        if (!m_showFavourites->loadFavourites()) {
            qDebug() << "Operations_VP_Shows: No existing favourites found or failed to load";
        } else {
            qDebug() << "Operations_VP_Shows: Loaded existing favourites, count:" << m_showFavourites->getFavouriteCount();
        }
    }
    
    // Store current show folder for later use (after watch history initialization)
    m_currentShowFolder = showFolderPath;

    // Update the show name label
    if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
        m_mainWindow->ui->label_VP_Shows_Display_Name->setText(actualShowName);
    }
    
    // Check for new episodes if TMDB is enabled and we have a valid show ID
    m_currentShowHasNewEpisodes = false;
    m_currentShowNewEpisodeCount = 0;
    
    if (m_episodeDetector && VP_ShowsConfig::isTMDBEnabled()) {
        // Load settings to get the show ID
        VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        VP_ShowsSettings::ShowSettings settings;
        
        if (settingsManager.loadShowSettings(showFolderPath, settings)) {
            // Check if show ID is valid (not empty and not "error") and TMDB is enabled for this show
            if (!settings.showId.isEmpty() && settings.showId != "error" && settings.useTMDB) {
                // Convert QString showId to integer
                bool idOk = false;
                int tmdbShowId = settings.showId.toInt(&idOk);
                
                if (idOk && tmdbShowId > 0) {
                    qDebug() << "Operations_VP_Shows: Checking for new episodes with TMDB ID:" << tmdbShowId;
                    
                    VP_ShowsEpisodeDetector::NewEpisodeInfo newEpisodeInfo = 
                        m_episodeDetector->checkForNewEpisodes(showFolderPath, tmdbShowId);
                    
                    if (newEpisodeInfo.hasNewEpisodes) {
                        m_currentShowHasNewEpisodes = true;
                        m_currentShowNewEpisodeCount = newEpisodeInfo.newEpisodeCount;
                        
                        qDebug() << "Operations_VP_Shows: Found" << m_currentShowNewEpisodeCount 
                                 << "new episode(s) for show";
                        qDebug() << "Operations_VP_Shows: Latest new episode: S" << newEpisodeInfo.latestSeason 
                                 << "E" << newEpisodeInfo.latestEpisode 
                                 << "-" << newEpisodeInfo.latestNewEpisodeName;
                    } else {
                        qDebug() << "Operations_VP_Shows: No new episodes detected";
                    }
                }
            }
        }
    }
    
    // Load and display the show image
    if (m_mainWindow->ui->label_VP_Shows_Display_Image) {
        QPixmap showImage = loadShowImage(showFolderPath);
        
        if (!showImage.isNull()) {
            // Get the actual size of the label widget
            QSize labelSize = m_mainWindow->ui->label_VP_Shows_Display_Image->size();
            qDebug() << "Operations_VP_Shows: Label size for image scaling:" << labelSize.width() << "x" << labelSize.height();
            
            // Scale the image to fit the label using its actual size
            QPixmap scaledImage = showImage.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            
            // Add "NEW" indicator if new episodes are available
            if (m_currentShowHasNewEpisodes) {
                scaledImage = addNewEpisodeIndicator(scaledImage, m_currentShowNewEpisodeCount);
                qDebug() << "Operations_VP_Shows: Added NEW indicator to show poster";
            }
            
            m_mainWindow->ui->label_VP_Shows_Display_Image->setPixmap(scaledImage);
            
            qDebug() << "Operations_VP_Shows: Scaled image from" << showImage.size() << "to" << scaledImage.size();
        } else {
            // Set a placeholder text if no image is available
            m_mainWindow->ui->label_VP_Shows_Display_Image->setText(tr("No Image Available"));
            qDebug() << "Operations_VP_Shows: No image available for show, displaying placeholder text";
        }
        
        // Setup context menu for the poster
        setupPosterContextMenu();
    }
    
    // Load and display the show description
    if (m_mainWindow->ui->textBrowser_VP_Shows_Display_Description) {
        QString description = loadShowDescription(showFolderPath);
        
        if (!description.isEmpty()) {
            m_mainWindow->ui->textBrowser_VP_Shows_Display_Description->setPlainText(description);
        } else {
            m_mainWindow->ui->textBrowser_VP_Shows_Display_Description->setPlainText(tr("No description available."));
        }
    }
    
    // Load show-specific settings first so they're available for episode display
    loadShowSettings(showFolderPath);
    
    // Check for new episodes if TMDB is enabled
    int tmdbId = getShowIdAsInt(m_currentShowSettings.showId);

    if (VP_ShowsConfig::isTMDBEnabled() && m_currentShowSettings.useTMDB && tmdbId > 0) {
        checkAndDisplayNewEpisodes(showFolderPath, tmdbId);
    } else {
        // Clear any previous new episode indicator
        displayNewEpisodeIndicator(false, 0);
    }
    
    // Load and display the episode list (now using the correct settings)
    loadShowEpisodes(showFolderPath);
    
    // Update the Play button text based on watch history
    updatePlayButtonText();
    
    // Switch to the display page
    if (m_mainWindow->ui->stackedWidget_VP_Shows) {
        m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(1); // Switch to page_2 (display page)
        qDebug() << "Operations_VP_Shows: Switched to display page";
    }
}

void Operations_VP_Shows::updatePlayButtonText()
{
    qDebug() << "Operations_VP_Shows: Updating Play button text";
    
    // Check if we have the Play button
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->pushButton_VP_Shows_Display_Play) {
        qDebug() << "Operations_VP_Shows: Play button not available";
        return;
    }
    
    // Default to "Play" text
    QString buttonText = tr("Play");
    
    // Check if we have watch history for this show
    if (m_watchHistory && !m_currentShowFolder.isEmpty()) {
        // Get the last watched episode
        QString lastWatchedEpisode = m_watchHistory->getLastWatchedEpisode();
        
        if (!lastWatchedEpisode.isEmpty()) {
            // Check if this episode has a resume position
            qint64 resumePosition = m_watchHistory->getResumePosition(lastWatchedEpisode);
            
            if (resumePosition > 0) {
                // We have a resume position, change button text to "Resume"
                buttonText = tr("Resume");
                qDebug() << "Operations_VP_Shows: Found resume position for" << lastWatchedEpisode 
                         << "at" << resumePosition << "ms";
            } else {
                qDebug() << "Operations_VP_Shows: Last watched episode completed or at beginning:" << lastWatchedEpisode;
                
                // Check if there's a next unwatched episode
                QStringList allEpisodes = getAllAvailableEpisodes();
                QString nextEpisode = m_watchHistory->getNextUnwatchedEpisode(lastWatchedEpisode, allEpisodes);
                
                if (!nextEpisode.isEmpty()) {
                    // There's a next episode to play
                    buttonText = tr("Play Next");
                    qDebug() << "Operations_VP_Shows: Next unwatched episode available:" << nextEpisode;
                }
            }
        } else {
            // No watch history, check if any episode has been partially watched
            QTreeWidget* tree = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
            if (tree) {
                bool hasResumePosition = false;
                
                // Iterate through all episodes to check for resume positions with bounds checking
                int topLevelCount = safeGetTreeItemCount(tree);
                for (int i = 0; i < topLevelCount && !hasResumePosition; ++i) {
                    QTreeWidgetItem* seasonItem = safeGetTreeItem(tree, i);
                    if (!seasonItem) continue;
                    for (int j = 0; j < seasonItem->childCount(); ++j) {
                        QTreeWidgetItem* episodeItem = seasonItem->child(j);
                        QString episodePath = episodeItem->data(0, Qt::UserRole).toString();
                        
                        if (!episodePath.isEmpty()) {
                            qint64 resumePos = m_watchHistory->getResumePosition(episodePath);
                            if (resumePos > 0) {
                                hasResumePosition = true;
                                buttonText = tr("Resume");
                                qDebug() << "Operations_VP_Shows: Found episode with resume position:" << episodePath;
                                break;
                            }
                        }
                    }
                }
                
                if (!hasResumePosition) {
                    qDebug() << "Operations_VP_Shows: No episodes with resume positions found";
                }
            }
        }
    } else {
        qDebug() << "Operations_VP_Shows: No watch history available for current show";
    }
    
    // Update the button text
    m_mainWindow->ui->pushButton_VP_Shows_Display_Play->setText(buttonText);
    qDebug() << "Operations_VP_Shows: Set Play button text to:" << buttonText;
}

void Operations_VP_Shows::onShowListItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        qDebug() << "Operations_VP_Shows: Double-clicked item is null";
        return;
    }
    
    QString showName = item->text();
    QString folderPath = item->data(Qt::UserRole).toString();
    qDebug() << "Operations_VP_Shows: Double-clicked on show:" << showName;
    qDebug() << "Operations_VP_Shows: Folder path from item:" << folderPath;
    
    // Display the show details with the specific folder path
    displayShowDetails(showName, folderPath);
}

void Operations_VP_Shows::loadShowEpisodes(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Loading episodes from folder:" << showFolderPath;
    
    // Check if we have the tree widget
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Tree widget not available";
        return;
    }
    
    // Clear the tree widget and episode mapping (thread-safe)
    m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->clear();
    m_episodeFileMapping.clear();
    
    // IMPORTANT: Clear the context menu tree item pointer since we're clearing the tree
    // This prevents crashes from dangling pointers when the tree is refreshed
    m_contextMenuTreeItem = nullptr;
    
    // Set header for the tree widget
    m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->setHeaderLabel(tr("Episodes"));
    
    // Get all video files in the folder (now using .mmvid extension)
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files, QDir::Name);
    
    if (videoFiles.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No video files found in folder";
        QTreeWidgetItem* noEpisodesItem = new QTreeWidgetItem();
        noEpisodesItem->setText(0, tr("No episodes found"));
        noEpisodesItem->setFlags(noEpisodesItem->flags() & ~Qt::ItemIsSelectable);
        m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->addTopLevelItem(noEpisodesItem);
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Found" << videoFiles.size() << "video files";
    
    // Use the member watch history that was initialized in displayShowDetails
    bool historyLoaded = (m_watchHistory != nullptr);
    if (historyLoaded) {
        qDebug() << "Operations_VP_Shows: Using existing watch history for episode display";
    } else {
        qDebug() << "Operations_VP_Shows: No watch history available for episode display";
    }
    
    // Define light grey color for watched episodes (suitable for dark theme)
    QColor watchedColor = QColor(150, 150, 150); // Light grey for watched episodes
    
    // Create metadata manager
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Map to organize episodes by language/translation, then season
    // Key: "Language Translation" (e.g., "English Dubbed"), Value: map of seasons
    QMap<QString, QMap<int, QList<QPair<int, QTreeWidgetItem*>>>> languageVersions;
    
    // Maps for special content types per language/translation
    QMap<QString, QList<QTreeWidgetItem*>> moviesByLanguage;    // Movies
    QMap<QString, QList<QTreeWidgetItem*>> ovasByLanguage;      // OVAs
    QMap<QString, QList<QTreeWidgetItem*>> extrasByLanguage;    // Extras
    
    // Map to hold error episodes per language/translation
    // Key: "Language Translation", Value: list of error episode items
    QMap<QString, QList<QTreeWidgetItem*>> errorEpisodesByLanguage;
    
    // List to hold broken video files (files where metadata cannot be read)
    QList<QTreeWidgetItem*> brokenFiles;
    
    // Process each video file
    for (const QString& videoFile : videoFiles) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        
        // Check if file is currently locked (being accessed by player)
        bool isLocked = VP_MetadataLockManager::instance()->isLocked(videoPath);
        
        VP_ShowsMetadata::ShowMetadata metadata;
        bool metadataRead = false;
        
        if (isLocked) {
            qDebug() << "Operations_VP_Shows: File is currently locked:" << videoFile;
            
            // For locked files that are currently playing, use placeholder metadata
            if (!m_currentPlayingEpisodePath.isEmpty()) {
                QFileInfo currentInfo(m_currentPlayingEpisodePath);
                QFileInfo videoInfo(videoPath);
                if (currentInfo.absoluteFilePath() == videoInfo.absoluteFilePath()) {
                    // This is the currently playing file, create placeholder metadata
                    metadata.filename = videoFile;
                    
                    // Extract show name from folder name
                    QFileInfo folderInfo(showFolderPath);
                    QString folderName = folderInfo.fileName();
                    QString extractedShowName;
                    QStringList folderParts = folderName.split('_');
                    if (!folderParts.isEmpty()) {
                        extractedShowName = folderParts.first();
                    } else {
                        extractedShowName = folderName;
                    }
                    
                    metadata.showName = extractedShowName; // Use the extracted show name
                    metadata.season = "1"; // Default values
                    metadata.episode = "1";
                    metadata.EPName = tr("[Currently Playing]");
                    metadata.language = "English";
                    metadata.translation = "Dubbed";
                    metadata.contentType = VP_ShowsMetadata::Regular;
                    metadataRead = true;
                    qDebug() << "Operations_VP_Shows: Using placeholder metadata for currently playing file";
                }
            }
            
            if (!metadataRead) {
                // Skip other locked files that aren't currently playing
                qDebug() << "Operations_VP_Shows: Skipping locked file (not currently playing):" << videoFile;
                continue;
            }
        } else {
            // File is not locked, read metadata normally
            metadataRead = metadataManager.readMetadataFromFile(videoPath, metadata);
        }
        
        if (!metadataRead) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from:" << videoFile;
            
            // Create broken file item
            QTreeWidgetItem* brokenItem = new QTreeWidgetItem();
            
            // Use the actual filename (without path) for display
            QFileInfo fileInfo(videoFile);
            QString displayName = fileInfo.fileName();
            
            // For broken files, we always show the file name regardless of setting
            // since we can't read metadata to get episode names
            brokenItem->setText(0, displayName);
            brokenItem->setData(0, Qt::UserRole, videoPath);
            
            // Set red color to indicate broken file
            brokenItem->setForeground(0, QBrush(QColor(255, 100, 100))); // Light red for visibility
            
            // Set tooltip to explain the issue
            brokenItem->setToolTip(0, tr("Broken file: Unable to read metadata header"));
            
            // Add to broken files list
            brokenFiles.append(brokenItem);
            continue;
        }
        
        qDebug() << "Operations_VP_Shows: Read metadata - ContentType:" << metadata.contentType 
                 << "Season:" << metadata.season << "Episode:" << metadata.episode
                 << "for file:" << videoFile;
        
        // Check if this is an error episode (duplicate detected during import)
        if (metadata.season == "error" || metadata.episode == "error") {
            qDebug() << "Operations_VP_Shows: Found error episode:" << metadata.filename;
            
            // Create error episode item
            QTreeWidgetItem* errorItem = new QTreeWidgetItem();
            
            // Format the error episode name
            QFileInfo fileInfo(metadata.filename);
            QString errorName;
            
            if (m_currentShowSettings.displayFileNames) {
                // Display the actual file name with ERROR prefix
                errorName = QString("[ERROR] %1").arg(fileInfo.fileName());
                qDebug() << "Operations_VP_Shows: Using file name for error episode display:" << errorName;
            } else {
                // Use base name with ERROR prefix
                QString baseName = fileInfo.completeBaseName();
                errorName = QString("[ERROR] %1").arg(baseName);
            }
            
            errorItem->setText(0, errorName);
            errorItem->setData(0, Qt::UserRole, videoPath);
            
            // No special color - use same as regular episodes
            
            // Create language/translation key for grouping
            QString languageKey = QString("%1 %2").arg(metadata.language).arg(metadata.translation);
            
            // Add to error list for this language/translation
            errorEpisodesByLanguage[languageKey].append(errorItem);
            continue;
        }
        
        // Create language/translation key (e.g., "English Dubbed")
        QString languageKey = QString("%1 %2").arg(metadata.language).arg(metadata.translation);
        
        // Check content type and handle accordingly
        if (metadata.contentType == VP_ShowsMetadata::Movie ||
            metadata.contentType == VP_ShowsMetadata::OVA ||
            metadata.contentType == VP_ShowsMetadata::Extra) {
            
            // Create special content item
            QTreeWidgetItem* specialItem = new QTreeWidgetItem();
            
            // Format the name based on content type and display setting
            QString itemName;
            QFileInfo fileInfo(metadata.filename);
            
            if (m_currentShowSettings.displayFileNames) {
                // Display the actual file name
                itemName = fileInfo.fileName();
                qDebug() << "Operations_VP_Shows: Using file name for special content display:" << itemName;
            } else {
                // Use episode name or base name
                QString baseName = fileInfo.completeBaseName();
                if (!metadata.EPName.isEmpty()) {
                    itemName = metadata.EPName;
                } else {
                    itemName = baseName;
                }
            }
            
            specialItem->setText(0, itemName);
            specialItem->setData(0, Qt::UserRole, videoPath);
            
            // Check if watched
            if (historyLoaded) {
                QString relativeEpisodePath = showDir.relativeFilePath(videoPath);
                if (m_watchHistory->isEpisodeCompleted(relativeEpisodePath)) {
                    specialItem->setForeground(0, QBrush(watchedColor));
                    qDebug() << "Operations_VP_Shows: Special content marked as watched:" << itemName;
                }
            }
            
            // Add to appropriate category
            switch (metadata.contentType) {
                case VP_ShowsMetadata::Movie:
                    moviesByLanguage[languageKey].append(specialItem);
                    qDebug() << "Operations_VP_Shows: Added movie:" << itemName << "to" << languageKey;
                    break;
                case VP_ShowsMetadata::OVA:
                    ovasByLanguage[languageKey].append(specialItem);
                    qDebug() << "Operations_VP_Shows: Added OVA:" << itemName << "to" << languageKey;
                    break;
                case VP_ShowsMetadata::Extra:
                    extrasByLanguage[languageKey].append(specialItem);
                    qDebug() << "Operations_VP_Shows: Added extra:" << itemName << "to" << languageKey;
                    break;
                default:
                    break;
            }
            
            // If dual display (movie that's part of series), also add to regular episodes
            if (metadata.isDualDisplay) {
                // Continue processing as regular episode below
                qDebug() << "Operations_VP_Shows: Movie has dual display - also adding to regular episodes";
            } else {
                continue;  // Skip regular episode processing
            }
        }
        
        // Parse season and episode numbers for regular episodes or dual-display movies
        int seasonNum = metadata.season.toInt();
        int episodeNum = metadata.episode.toInt();
        
        // If season/episode numbers are invalid, try to parse from filename
        if (seasonNum == 0 || episodeNum == 0) {
            VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum);
        }
        
        // Skip regular episode processing if it's not a regular episode or dual-display content
        if (metadata.contentType != VP_ShowsMetadata::Regular && !metadata.isDualDisplay) {
            qDebug() << "Operations_VP_Shows: Content is not a regular episode, skipping regular processing";
            continue;
        }
        
        // If we still don't have a valid episode number, add to error category
        if (episodeNum == 0) {
            qDebug() << "Operations_VP_Shows: No valid episode number found for:" << metadata.filename;
            
            // Create error item for this episode
            QTreeWidgetItem* errorItem = new QTreeWidgetItem();
            
            // Use filename for display since we don't have valid episode info
            QString displayText = metadata.filename;
            if (displayText.endsWith(".mmvid", Qt::CaseInsensitive)) {
                displayText = displayText.left(displayText.length() - 6);
            }
            errorItem->setText(0, displayText);
            errorItem->setData(0, Qt::UserRole, videoPath);
            
            // Set error styling
            errorItem->setForeground(0, QBrush(QColor(255, 150, 150))); // Light red for error
            errorItem->setToolTip(0, tr("Invalid episode number (0) - needs metadata repair"));
            
            // Add to error episodes for this language
            errorEpisodesByLanguage[languageKey].append(errorItem);
            
            // Track in episode mapping for playback
            QString episodeKey = QString("%1_%2_%3").arg(metadata.language)
                                                     .arg(metadata.translation)
                                                     .arg(metadata.filename);
            m_episodeFileMapping.insert(episodeKey, videoPath);
            
            continue;  // Skip normal episode processing
        }
        
        // Default to season 1 if still no season number but we have an episode number
        if (seasonNum == 0) {
            seasonNum = 1;
        }
        
        // Create episode item
        QTreeWidgetItem* episodeItem = new QTreeWidgetItem();
        
        // Check if we should display file names instead of episode names
        QString displayText;
        if (m_currentShowSettings.displayFileNames) {
            // Display the actual file name
            QFileInfo fileInfo(metadata.filename);
            displayText = fileInfo.fileName();
            qDebug() << "Operations_VP_Shows: Using file name for display:" << displayText;
        } else {
            // Format the episode name based on numbering type
            if (metadata.isAbsoluteNumbering()) {
                // For absolute numbering, just use episode number without season
                if (!metadata.EPName.isEmpty()) {
                    displayText = QString("Episode %1 - %2").arg(episodeNum).arg(metadata.EPName);
                } else {
                    QFileInfo fileInfo(metadata.filename);
                    QString baseName = fileInfo.completeBaseName();
                    displayText = QString("Episode %1 - %2").arg(episodeNum).arg(baseName);
                }
            } else {
                // Traditional season/episode numbering
                if (!metadata.EPName.isEmpty()) {
                    displayText = QString("%1 - %2").arg(episodeNum).arg(metadata.EPName);
                } else {
                    QFileInfo fileInfo(metadata.filename);
                    QString baseName = fileInfo.completeBaseName();
                    displayText = QString("%1 - %2").arg(episodeNum).arg(baseName);
                }
            }
        }
        
        episodeItem->setText(0, displayText);
        
        // Store the file path in the item's data
        episodeItem->setData(0, Qt::UserRole, videoPath);
        
        // Check if this episode has been watched and apply grey color if it has
        if (historyLoaded) {
            // Get relative path for watch history check
            QString relativeEpisodePath = showDir.relativeFilePath(videoPath);
            if (m_watchHistory->isEpisodeCompleted(relativeEpisodePath)) {
                episodeItem->setForeground(0, QBrush(watchedColor));
                //qDebug() << "Operations_VP_Shows: Episode marked as watched:" << episodeName; // episodeName is undeclared identifier, disabled debug output because I'm lazy
            }
        }
        
        // Create mapping key and store the mapping (thread-safe)
        QString mappingKey = QString("%1_%2_S%3E%4")
            .arg(metadata.showName)
            .arg(languageKey)
            .arg(seasonNum, 2, 10, QChar('0'))
            .arg(episodeNum, 2, 10, QChar('0'));
        m_episodeFileMapping.insert(mappingKey, videoPath);
        
        // Add to language/season map
        // For absolute numbering, we'll use season 0 as a marker
        if (metadata.isAbsoluteNumbering()) {
            languageVersions[languageKey][0].append(QPair<int, QTreeWidgetItem*>(episodeNum, episodeItem));
        } else {
            languageVersions[languageKey][seasonNum].append(QPair<int, QTreeWidgetItem*>(episodeNum, episodeItem));
        }
        
        //qDebug() << "Operations_VP_Shows: Added episode" << languageKey
        //         << "S" << seasonNum << "E" << episodeNum << "-" << episodeName; // episodeName is undeclared identifier, disabled debug output because I'm lazy
    }
    
    // Create language/translation items, then season items, then add episodes
    // First, get all unique language keys from ALL content types
    QSet<QString> allLanguageKeys;
    
    // Add keys from regular episodes
    for (const QString& key : languageVersions.keys()) {
        allLanguageKeys.insert(key);
    }
    
    // Add keys from Movies
    for (const QString& key : moviesByLanguage.keys()) {
        allLanguageKeys.insert(key);
    }
    
    // Add keys from OVAs
    for (const QString& key : ovasByLanguage.keys()) {
        allLanguageKeys.insert(key);
    }
    
    // Add keys from Extras
    for (const QString& key : extrasByLanguage.keys()) {
        allLanguageKeys.insert(key);
    }
    
    // Add keys from error episodes
    for (const QString& key : errorEpisodesByLanguage.keys()) {
        allLanguageKeys.insert(key);
    }
    
    QList<QString> languageKeys = allLanguageKeys.values();
    std::sort(languageKeys.begin(), languageKeys.end());
    
    // Add "Broken" category as the first item if there are any broken files
    if (!brokenFiles.isEmpty()) {
        QTreeWidgetItem* brokenCategory = new QTreeWidgetItem();
        brokenCategory->setText(0, tr("Broken (%1)").arg(brokenFiles.size()));
        
        // Set a distinctive icon or color for the broken category
        brokenCategory->setForeground(0, QBrush(QColor(255, 100, 100))); // Light red
        brokenCategory->setFont(0, QFont(brokenCategory->font(0).family(), brokenCategory->font(0).pointSize(), QFont::Bold));
        
        // Add tooltip explaining what this category contains
        brokenCategory->setToolTip(0, tr("Video files with corrupted or unreadable metadata headers"));
        
        // Sort broken files by filename for consistent display
        std::sort(brokenFiles.begin(), brokenFiles.end(), 
                  [](const QTreeWidgetItem* a, const QTreeWidgetItem* b) {
                      return a->text(0).toLower() < b->text(0).toLower();
                  });
        
        // Add all broken files to the category
        for (QTreeWidgetItem* brokenItem : brokenFiles) {
            brokenCategory->addChild(brokenItem);
        }
        
        // Add broken category to tree widget as first item
        m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->addTopLevelItem(brokenCategory);
        
        // Always expand the broken category so users can see the problematic files
        brokenCategory->setExpanded(true);
        
        qDebug() << "Operations_VP_Shows: Added" << brokenFiles.size() << "broken files to tree";
    }
    
    for (const QString& languageKey : languageKeys) {
        // Create language/translation item
        QTreeWidgetItem* languageItem = new QTreeWidgetItem();
        languageItem->setText(0, languageKey);
        
        bool allEpisodesInLanguageWatched = true; // Track if all episodes in this language are watched
        int totalEpisodesInLanguage = 0;
        int watchedEpisodesInLanguage = 0;
        
        // ORDER 1: Add Extra category FIRST if there are any
        if (extrasByLanguage.contains(languageKey) && !extrasByLanguage[languageKey].isEmpty()) {
            QTreeWidgetItem* extraCategory = new QTreeWidgetItem();
            extraCategory->setText(0, tr("Extra (%1)").arg(extrasByLanguage[languageKey].size()));
            
            // Sort extras similar to OVAs
            QList<QTreeWidgetItem*>& extras = extrasByLanguage[languageKey];
            std::sort(extras.begin(), extras.end(), [&metadataManager](QTreeWidgetItem* a, QTreeWidgetItem* b) {
                QString pathA = a->data(0, Qt::UserRole).toString();
                QString pathB = b->data(0, Qt::UserRole).toString();
                VP_ShowsMetadata::ShowMetadata metaA, metaB;
                
                // Try to get episode numbers first
                int epA = 0, epB = 0;
                if (metadataManager.readMetadataFromFile(pathA, metaA)) {
                    epA = metaA.episode.toInt();
                }
                if (metadataManager.readMetadataFromFile(pathB, metaB)) {
                    epB = metaB.episode.toInt();
                }
                
                // If both have episode numbers, sort by episode
                if (epA > 0 && epB > 0) {
                    return epA < epB;
                }
                // Then by date if available
                if (!metaA.airDate.isEmpty() && !metaB.airDate.isEmpty()) {
                    return metaA.airDate < metaB.airDate;
                }
                // Keep original order
                return false;
            });
            
            // Add sorted extras to category
            for (QTreeWidgetItem* extraItem : extras) {
                extraCategory->addChild(extraItem);
            }
            
            languageItem->addChild(extraCategory);
            qDebug() << "Operations_VP_Shows: Added" << extras.size() << "extras for" << languageKey;
        }
        
        // ORDER 2: Add Movies category SECOND if there are any
        if (moviesByLanguage.contains(languageKey) && !moviesByLanguage[languageKey].isEmpty()) {
            QTreeWidgetItem* moviesCategory = new QTreeWidgetItem();
            moviesCategory->setText(0, tr("Movies (%1)").arg(moviesByLanguage[languageKey].size()));
            
            // Sort movies by release date if available, then by name
            QList<QTreeWidgetItem*>& movies = moviesByLanguage[languageKey];
            std::sort(movies.begin(), movies.end(), [&metadataManager](QTreeWidgetItem* a, QTreeWidgetItem* b) {
                QString pathA = a->data(0, Qt::UserRole).toString();
                QString pathB = b->data(0, Qt::UserRole).toString();
                VP_ShowsMetadata::ShowMetadata metaA, metaB;
                
                bool hasDateA = false, hasDateB = false;
                if (metadataManager.readMetadataFromFile(pathA, metaA) && !metaA.airDate.isEmpty()) {
                    hasDateA = true;
                }
                if (metadataManager.readMetadataFromFile(pathB, metaB) && !metaB.airDate.isEmpty()) {
                    hasDateB = true;
                }
                
                // If both have dates, sort by date
                if (hasDateA && hasDateB) {
                    return metaA.airDate < metaB.airDate;
                }
                // If only one has date, it comes first
                if (hasDateA && !hasDateB) return true;
                if (!hasDateA && hasDateB) return false;
                // Neither has date, keep original order
                return false;
            });
            
            // Add sorted movies to category
            for (QTreeWidgetItem* movieItem : movies) {
                moviesCategory->addChild(movieItem);
            }
            
            languageItem->addChild(moviesCategory);
            qDebug() << "Operations_VP_Shows: Added" << movies.size() << "movies for" << languageKey;
        }
        
        // ORDER 3 & 4: Add Episodes (absolute numbering) THIRD and Seasons FOURTH
        // Get seasons for this language/translation (if any regular episodes exist)
        if (languageVersions.contains(languageKey)) {
            QMap<int, QList<QPair<int, QTreeWidgetItem*>>>& seasons = languageVersions[languageKey];
            QList<int> seasonNumbers = seasons.keys();
            std::sort(seasonNumbers.begin(), seasonNumbers.end());
            
            // First add absolute numbering "Episodes" (season 0) if it exists
            if (seasonNumbers.contains(0)) {
                int seasonNum = 0;
                QTreeWidgetItem* episodesItem = new QTreeWidgetItem();
                episodesItem->setText(0, tr("Episodes"));
                
                // Sort episodes by episode number
                QList<QPair<int, QTreeWidgetItem*>>& episodes = seasons[seasonNum];
                std::sort(episodes.begin(), episodes.end(), 
                          [](const QPair<int, QTreeWidgetItem*>& a, const QPair<int, QTreeWidgetItem*>& b) {
                              return a.first < b.first;
                          });
                
                // Track if all episodes are watched
                bool allEpisodesWatched = true;
                int episodesCount = 0;
                int watchedCount = 0;
                
                // Add episodes to the item
                for (const auto& episode : episodes) {
                    episodesItem->addChild(episode.second);
                    episodesCount++;
                    totalEpisodesInLanguage++;
                    
                    // Check if this episode is watched by checking its foreground color
                    if (episode.second->foreground(0).color() == watchedColor) {
                        watchedCount++;
                        watchedEpisodesInLanguage++;
                    } else {
                        allEpisodesWatched = false;
                        allEpisodesInLanguageWatched = false;
                    }
                }
                
                // If all episodes are watched, grey out the item
                if (allEpisodesWatched && episodesCount > 0) {
                    episodesItem->setForeground(0, QBrush(watchedColor));
                    qDebug() << "Operations_VP_Shows: All absolute episodes watched, greying out Episodes";
                }
                
                // Add Episodes to language item
                languageItem->addChild(episodesItem);
            }
            
            // Then add regular seasons (season > 0)
            for (int seasonNum : seasonNumbers) {
                if (seasonNum == 0) continue; // Skip season 0, already handled above
                
                // Create season item
                QTreeWidgetItem* seasonItem = new QTreeWidgetItem();
                seasonItem->setText(0, tr("Season %1").arg(seasonNum));
                
                // Sort episodes by episode number
                QList<QPair<int, QTreeWidgetItem*>>& episodes = seasons[seasonNum];
                std::sort(episodes.begin(), episodes.end(), 
                          [](const QPair<int, QTreeWidgetItem*>& a, const QPair<int, QTreeWidgetItem*>& b) {
                              return a.first < b.first;
                          });
                
                // Track if all episodes in this season are watched
                bool allEpisodesInSeasonWatched = true;
                int episodesInSeason = 0;
                int watchedInSeason = 0;
                
                // Add episodes to season
                for (const auto& episode : episodes) {
                    seasonItem->addChild(episode.second);
                    episodesInSeason++;
                    totalEpisodesInLanguage++;
                    
                    // Check if this episode is watched by checking its foreground color
                    if (episode.second->foreground(0).color() == watchedColor) {
                        watchedInSeason++;
                        watchedEpisodesInLanguage++;
                    } else {
                        allEpisodesInSeasonWatched = false;
                        allEpisodesInLanguageWatched = false;
                    }
                }
                
                // If all episodes in season are watched, grey out the season item
                if (allEpisodesInSeasonWatched && episodesInSeason > 0) {
                    seasonItem->setForeground(0, QBrush(watchedColor));
                    qDebug() << "Operations_VP_Shows: All episodes in season watched, greying out:" << seasonItem->text(0);
                }
                
                // Add season to language item
                languageItem->addChild(seasonItem);
                
                // Don't expand by default - will be handled by expandToLastWatchedEpisode()
            }
        }
        
        // Add OVA category if there are any (keeping it after regular content)
        if (ovasByLanguage.contains(languageKey) && !ovasByLanguage[languageKey].isEmpty()) {
            QTreeWidgetItem* ovaCategory = new QTreeWidgetItem();
            ovaCategory->setText(0, tr("OVA (%1)").arg(ovasByLanguage[languageKey].size()));
            
            // Sort OVAs similar to movies
            QList<QTreeWidgetItem*>& ovas = ovasByLanguage[languageKey];
            std::sort(ovas.begin(), ovas.end(), [&metadataManager](QTreeWidgetItem* a, QTreeWidgetItem* b) {
                QString pathA = a->data(0, Qt::UserRole).toString();
                QString pathB = b->data(0, Qt::UserRole).toString();
                VP_ShowsMetadata::ShowMetadata metaA, metaB;
                
                // Try to get episode numbers first
                int epA = 0, epB = 0;
                if (metadataManager.readMetadataFromFile(pathA, metaA)) {
                    epA = metaA.episode.toInt();
                }
                if (metadataManager.readMetadataFromFile(pathB, metaB)) {
                    epB = metaB.episode.toInt();
                }
                
                // If both have episode numbers, sort by episode
                if (epA > 0 && epB > 0) {
                    return epA < epB;
                }
                // Then by date if available
                if (!metaA.airDate.isEmpty() && !metaB.airDate.isEmpty()) {
                    return metaA.airDate < metaB.airDate;
                }
                // Keep original order
                return false;
            });
            
            // Add sorted OVAs to category
            for (QTreeWidgetItem* ovaItem : ovas) {
                ovaCategory->addChild(ovaItem);
            }
            
            languageItem->addChild(ovaCategory);
            qDebug() << "Operations_VP_Shows: Added" << ovas.size() << "OVAs for" << languageKey;
        }
        
        // Add error episodes category for this language if there are any
        if (errorEpisodesByLanguage.contains(languageKey)) {
            QList<QTreeWidgetItem*>& errorEpisodes = errorEpisodesByLanguage[languageKey];
            if (!errorEpisodes.isEmpty()) {
                // Create error category item
                QTreeWidgetItem* errorCategory = new QTreeWidgetItem();
                errorCategory->setText(0, tr("Error - Duplicate Episodes (%1)").arg(errorEpisodes.size()));
                
                // Add all error episodes under this category
                for (QTreeWidgetItem* errorItem : errorEpisodes) {
                    errorCategory->addChild(errorItem);
                }
                
                // Add error category to language item (it will appear alongside seasons)
                languageItem->addChild(errorCategory);
                
                // Always expand error category so users can see the problematic episodes
                errorCategory->setExpanded(true);
                
                qDebug() << "Operations_VP_Shows: Added" << errorEpisodes.size() 
                         << "error episodes for" << languageKey;
            }
        }
        
        // If all episodes in this language are watched, grey out the language item
        if (allEpisodesInLanguageWatched && totalEpisodesInLanguage > 0) {
            languageItem->setForeground(0, QBrush(watchedColor));
            qDebug() << "Operations_VP_Shows: All episodes in language watched, greying out:" << languageKey;
        }
        
        // Add language item to tree widget
        m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->addTopLevelItem(languageItem);
        
        // Don't expand by default - will be handled by expandToLastWatchedEpisode()
    }
    
    // Refresh episode colors to ensure watched state is shown
    // This is needed in case episodes were marked as watched elsewhere
    if (m_watchHistory) {
        refreshEpisodeTreeColors();
    }

    qDebug() << "Operations_VP_Shows: Finished loading episodes. Total language versions:" << languageKeys.size();

    //Refresh the new available episode notification
    refreshShowPosterWithNotification();
}

void Operations_VP_Shows::onEpisodeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    
    if (!item) {
        qDebug() << "Operations_VP_Shows: Double-clicked item is null";
        return;
    }
    
    // Check if we're currently decrypting an episode
    if (m_isDecrypting) {
        qDebug() << "Operations_VP_Shows: Currently decrypting an episode, ignoring double-click";
        return;
    }
    
    // Check if this is an episode item (not a language or season item)
    if (item->childCount() > 0) {
        // This is a language or season item, not an episode
        qDebug() << "Operations_VP_Shows: Clicked on language/season item, not an episode";
        return;
    }
    
    // Get the file path from the item's data
    QString videoPath = item->data(0, Qt::UserRole).toString();
    
    if (videoPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No video path stored for this episode";
        return;
    }
    
    QString episodeName = item->text(0);
    qDebug() << "Operations_VP_Shows: Double-clicked on episode:" << episodeName;
    qDebug() << "Operations_VP_Shows: Video path:" << videoPath;
    
    // Check if this is a broken file by checking if the parent is the "Broken" category
    QTreeWidgetItem* parent = item->parent();
    if (parent && parent->text(0).startsWith("Broken")) {
        qDebug() << "Operations_VP_Shows: User attempted to play a broken video file";
        
        // Show warning dialog
        QMessageBox msgBox(m_mainWindow);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle(tr("Broken Video File"));
        msgBox.setText(tr("This video file has a corrupted metadata header and cannot be played."));
        msgBox.setInformativeText(tr("The file's metadata needs to be repaired before it can be played.\n\nFile: %1").arg(episodeName));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    // Check if we should start from the beginning due to near-end position
    // This is for direct double-click play
    bool forceStartFromBeginning = false;
    if (m_watchHistory && !m_currentShowFolder.isEmpty()) {
        QDir showDir(m_currentShowFolder);
        QString relativePath = showDir.relativeFilePath(videoPath);
        qint64 resumePosition = m_watchHistory->getResumePosition(relativePath);
        
        if (resumePosition > 0) {
            EpisodeWatchInfo watchInfo = m_watchHistory->getEpisodeWatchInfo(relativePath);
            if (watchInfo.totalDuration > 0) {
                qint64 remainingTime = watchInfo.totalDuration - resumePosition;
                if (remainingTime <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS) {
                    forceStartFromBeginning = true;
                    qDebug() << "Operations_VP_Shows: Double-click play - resume position is near end (" << remainingTime
                             << "ms remaining), will start from beginning instead";
                }
            }
        }
    }
    
    // Check if there's already a video player window open
    if (m_episodePlayer && m_episodePlayer->isVisible()) {
        qDebug() << "Operations_VP_Shows: Existing video player detected - closing it before playing new episode";
        
        // Store the episode information for playing after cleanup
        m_pendingContextMenuEpisodePath = videoPath;
        m_pendingContextMenuEpisodeName = episodeName;
        
        // Store flag to indicate we should start from beginning if needed
        m_forceStartFromBeginning = forceStartFromBeginning;
        
        // Stop playback tracking if active
        if (m_playbackTracker && m_playbackTracker->isTracking()) {
            qDebug() << "Operations_VP_Shows: Stopping active playback tracking";
            m_playbackTracker->stopTracking();
        }
        
        // Force release the video file and clean up
        forceReleaseVideoFile();
        
        // Close the player
        if (m_episodePlayer->isVisible()) {
            m_episodePlayer->close();
        }
        m_episodePlayer.reset();
        
        // Clean up any existing temp file
        cleanupTempFile();
        
        qDebug() << "Operations_VP_Shows: Previous video player closed and cleaned up";
        
        // Use a timer to ensure cleanup is complete before playing new episode
        QTimer::singleShot(100, this, [this]() {
            if (!m_pendingContextMenuEpisodePath.isEmpty() && !m_pendingContextMenuEpisodeName.isEmpty()) {
                qDebug() << "Operations_VP_Shows: Playing pending double-clicked episode after cleanup";
                
                // Decrypt and play the episode
                QString episodePath = m_pendingContextMenuEpisodePath;
                QString episodeName = m_pendingContextMenuEpisodeName;
                
                // Clear the pending values
                m_pendingContextMenuEpisodePath.clear();
                m_pendingContextMenuEpisodeName.clear();
                
                decryptAndPlayEpisode(episodePath, episodeName);
            }
        });
        
        return;
    }
    
    // No existing player, proceed with normal play
    // Store flag to indicate we should start from beginning
    m_forceStartFromBeginning = forceStartFromBeginning;
    
    // Decrypt and play the episode
    decryptAndPlayEpisode(videoPath, episodeName);
}

void Operations_VP_Shows::decryptAndPlayEpisode(const QString& encryptedFilePath, const QString& episodeName)
{
    qDebug() << "Operations_VP_Shows: Starting decryption and playback for:" << episodeName;
    qDebug() << "Operations_VP_Shows: Is autoplay:" << m_isAutoplayInProgress;
    qDebug() << "Operations_VP_Shows: Is random autoplay:" << m_isRandomAutoplay;
    
    // Set the decryption flag
    m_isDecrypting = true;
    qDebug() << "Operations_VP_Shows: Set decrypting flag to true";
    
    // Start timer to track decryption time (for small episode fix)
    QElapsedTimer decryptionTimer;
    decryptionTimer.start();
    const qint64 MINIMUM_DECRYPTION_TIME_MS = 2000; // 2 seconds minimum
    qDebug() << "Operations_VP_Shows: Started decryption timer with" << MINIMUM_DECRYPTION_TIME_MS << "ms minimum";
    
    // Clear any pending autoplay info if this is a manual play
    if (!m_isAutoplayInProgress && !m_pendingAutoplayPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Manual play detected, clearing pending autoplay info";
        m_pendingAutoplayPath.clear();
        m_pendingAutoplayName.clear();
        m_pendingAutoplayIsRandom = false;
    }
    
    // Clear any pending context menu play info (defensive cleanup)
    if (!m_pendingContextMenuEpisodePath.isEmpty() || !m_pendingContextMenuEpisodeName.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Clearing any stale pending context menu play info";
        m_pendingContextMenuEpisodePath.clear();
        m_pendingContextMenuEpisodeName.clear();
    }

    // Reset stored window settings if this is NOT autoplay
    // This ensures that manual play uses the autoFullscreen setting instead of previous window state
    if (!m_isAutoplayInProgress) {
        qDebug() << "Operations_VP_Shows: Manual play detected - resetting stored window settings";
        VP_Shows_Videoplayer::resetStoredWindowSettings();
        // Also reset random autoplay flag for manual play
        m_isRandomAutoplay = false;
    } else {
        qDebug() << "Operations_VP_Shows: Autoplay detected - keeping stored window settings";
    }

    // Check if there's already a video player open and close it before decryption
    if (m_episodePlayer) {
        qDebug() << "Operations_VP_Shows: Existing video player detected - closing it before starting new playback";

        // Stop playback tracking if active
        if (m_playbackTracker && m_playbackTracker->isTracking()) {
            qDebug() << "Operations_VP_Shows: Stopping active playback tracking";
            m_playbackTracker->stopTracking();
        }

        // Force release the video file and clean up
        forceReleaseVideoFile();

        // Close and reset the player
        if (m_episodePlayer->isVisible()) {
            m_episodePlayer->close();
        }
        m_episodePlayer.reset();

        // Clean up any existing temp file
        cleanupTempFile();

        qDebug() << "Operations_VP_Shows: Previous video player closed and cleaned up";
    }

    // Store the current playing episode path for autoplay
    m_currentPlayingEpisodePath = encryptedFilePath;
    qDebug() << "Operations_VP_Shows: Stored current playing episode path:" << m_currentPlayingEpisodePath;

    // Reset the near-completion flag for this new episode
    m_episodeWasNearCompletion = false;
    qDebug() << "Operations_VP_Shows: Reset near-completion flag for new episode";

    // Clean up any existing temp file first
    cleanupTempFile();
    
    // Reset any existing playback tracker before creating new one
    if (m_playbackTracker) {
    m_playbackTracker.reset();
    }
    // Don't reset m_watchHistory - it should persist for the context menu functionality
    // m_watchHistory is initialized in displayShowDetails and should remain available
    
    // Check MainWindow validity before proceeding
    if (!m_mainWindow) {
        qDebug() << "Critical-Operations_VP_Shows: MainWindow is null, cannot proceed with playback";
        // Reset autoplay flags if this was an autoplay attempt
        if (m_isAutoplayInProgress) {
            qDebug() << "Operations_VP_Shows: Resetting autoplay flags due to MainWindow being null";
            m_isAutoplayInProgress = false;
            m_isRandomAutoplay = false;
            
            // Clear pending autoplay info
            m_pendingAutoplayPath.clear();
            m_pendingAutoplayName.clear();
            m_pendingAutoplayIsRandom = false;
        }
        return;
    }
    
    // Initialize watch history integration for this show
    QString relativeEpisodePath;
    QString episodeIdentifier;
    
    if (!m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Initializing watch history integration for show folder:" << m_currentShowFolder;

        // Ensure m_watchHistory is initialized if it's not already
        if (!m_watchHistory) {
            qDebug() << "Operations_VP_Shows: Creating watch history for playback";
            m_watchHistory = std::make_unique<VP_ShowsWatchHistory>(
                m_currentShowFolder,
                m_mainWindow->user_Key,
                m_mainWindow->user_Username
            );
            m_watchHistory->loadHistory();
        }

        // Create the playback tracker
        m_playbackTracker = std::make_unique<VP_ShowsPlaybackTracker>(this);

        // Initialize it with the show folder
        bool initSuccess = m_playbackTracker->initializeForShow(
            m_currentShowFolder,
            m_mainWindow->user_Key,
            m_mainWindow->user_Username
        );

        if (initSuccess) {
            qDebug() << "Operations_VP_Shows: Playback tracker initialized successfully";
            qDebug() << "Operations_VP_Shows: Current show settings:";
            qDebug() << "Operations_VP_Shows:   - autoplay:" << m_currentShowSettings.autoplay;
            qDebug() << "Operations_VP_Shows:   - autoplayRandom:" << m_currentShowSettings.autoplayRandom;

            // Connect episodeNearCompletion signal to trigger autoplay when episode naturally completes
            QPointer<Operations_VP_Shows> safeThis = this;
            connect(m_playbackTracker.get(), &VP_ShowsPlaybackTracker::episodeNearCompletion,
                    this, [safeThis](const QString& episodePath) {
                if (!safeThis) {
                    qDebug() << "Critical-Operations_VP_Shows: episodeNearCompletion lambda but 'this' is invalid";
                    return;
                }
                
                qDebug() << "Operations_VP_Shows: Episode near completion signal received for:" << episodePath;
                
                // Check if autoplay is enabled and not already in progress
                if (safeThis->m_currentShowSettings.autoplay && !safeThis->m_isAutoplayInProgress) {
                    qDebug() << "Operations_VP_Shows: Autoplay enabled - will trigger when playback ends naturally";
                    // Set flag to indicate we should autoplay when episode finishes
                    safeThis->m_episodeWasNearCompletion = true;
                    qDebug() << "Operations_VP_Shows: m_episodeWasNearCompletion flag set to true";
                } else {
                    qDebug() << "Operations_VP_Shows: Autoplay disabled or already in progress";
                    qDebug() << "Operations_VP_Shows:   - Autoplay setting:" << safeThis->m_currentShowSettings.autoplay;
                    qDebug() << "Operations_VP_Shows:   - Already in progress:" << safeThis->m_isAutoplayInProgress;
                }
            });
            
            qDebug() << "Operations_VP_Shows: Connected episodeNearCompletion signal for autoplay";
            qDebug() << "Operations_VP_Shows: Ready for autoplay - waiting for episode to near completion";

            // Calculate relative path of episode within show folder
            QDir showDir(m_currentShowFolder);
            relativeEpisodePath = showDir.relativeFilePath(encryptedFilePath);

            // Try to extract episode identifier from the episode name
            QRegularExpression regex("S(\\d+)E(\\d+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = regex.match(episodeName);
            if (match.hasMatch()) {
                int season = match.captured(1).toInt();
                int episode = match.captured(2).toInt();
                episodeIdentifier = QString("S%1E%2").arg(season, 2, 10, QChar('0')).arg(episode, 2, 10, QChar('0'));
            }

            qDebug() << "Operations_VP_Shows: Episode relative path:" << relativeEpisodePath;
            qDebug() << "Operations_VP_Shows: Episode identifier:" << episodeIdentifier;

            // Check for resume position
            qint64 resumePosition = m_playbackTracker->getResumePosition(relativeEpisodePath);
            if (resumePosition > 0) {
                qDebug() << "Operations_VP_Shows: Found resume position:" << resumePosition << "ms";
            }
            
            // Reset playback position to 0 for random autoplay episodes
            if (m_isRandomAutoplay) {
                qDebug() << "Operations_VP_Shows: Random autoplay detected - resetting playback position to 0";
                m_playbackTracker->resetEpisodePosition(relativeEpisodePath);
                // Also force starting from beginning
                m_forceStartFromBeginning = true;
            }
        } else {
            qDebug() << "Operations_VP_Shows: WARNING - Failed to initialize playback tracker";
            m_playbackTracker.reset();
        }
    }

    // Ensure username is set for operations_files
    if (!m_mainWindow->user_Username.isEmpty()) {
        OperationsFiles::setUsername(m_mainWindow->user_Username);
    }

    // Build the temp folder path: Data/username/temp/tempdecrypt
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString tempPath = QDir(userPath).absoluteFilePath("temp");
    QString tempDecryptPath = QDir(tempPath).absoluteFilePath("tempdecrypt");

    qDebug() << "Operations_VP_Shows: Temp decrypt path:" << tempDecryptPath;

    // Ensure the temp folders exist using operations_files functions
    if (!OperationsFiles::ensureDirectoryExists(userPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create user directory";
        QMessageBox::critical(m_mainWindow,
                            tr("Playback Error"),
                            tr("Failed to create user directory."));
        return;
    }

    if (!OperationsFiles::ensureDirectoryExists(tempPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create temp directory";
        m_isDecrypting = false;
        qDebug() << "Operations_VP_Shows: Cleared decrypting flag after temp directory creation failure";
        QMessageBox::critical(m_mainWindow,
                            tr("Playback Error"),
                            tr("Failed to create temporary directory."));
        return;
    }

    if (!OperationsFiles::ensureDirectoryExists(tempDecryptPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create tempdecrypt directory";
        m_isDecrypting = false;
        qDebug() << "Operations_VP_Shows: Cleared decrypting flag after tempdecrypt directory creation failure";
        QMessageBox::critical(m_mainWindow,
                            tr("Playback Error"),
                            tr("Failed to create temporary decryption directory."));
        return;
    }

    // For decryption, we'll need to get the actual extension from metadata
    // For now, generate a temp name without knowing the extension
    // The actual extension will be determined after reading metadata
    QString tempRandomName = generateRandomFileName("");
    // Remove the .mmvid extension we just added
    tempRandomName = tempRandomName.left(tempRandomName.lastIndexOf('.'));

    // We'll determine the actual extension after reading metadata
    QString decryptedFilePath = QDir(tempDecryptPath).absoluteFilePath(tempRandomName);

    qDebug() << "Operations_VP_Shows: Base decrypt path:" << decryptedFilePath;

    // Decrypt the file with metadata handling
    // Note: decryptVideoWithMetadata will append the proper extension and store the actual path in m_lastDecryptedFilePath
    bool decryptSuccess = decryptVideoWithMetadata(encryptedFilePath, decryptedFilePath);

    if (!decryptSuccess) {
        qDebug() << "Operations_VP_Shows: Failed to decrypt video file";
        
        // Clear the decryption flag
        m_isDecrypting = false;
        qDebug() << "Operations_VP_Shows: Cleared decrypting flag after decryption failure";
        
        // Reset autoplay flags if this was an autoplay attempt
        if (m_isAutoplayInProgress) {
            qDebug() << "Operations_VP_Shows: Resetting autoplay flags due to decryption failure";
            m_isAutoplayInProgress = false;
            m_isRandomAutoplay = false;
            m_episodeWasNearCompletion = false;
            
            // Clear pending autoplay info
            m_pendingAutoplayPath.clear();
            m_pendingAutoplayName.clear();
            m_pendingAutoplayIsRandom = false;
        }
        
        QMessageBox::critical(m_mainWindow,
                            tr("Decryption Error"),
                            tr("Failed to decrypt the video file. The file may be corrupted or the encryption key may be incorrect."));
        return;
    }
    
    // Check if minimum time has elapsed since decryption started (fix for small episodes)
    qint64 elapsedTime = decryptionTimer.elapsed();
    if (elapsedTime < MINIMUM_DECRYPTION_TIME_MS) {
        qint64 remainingTime = MINIMUM_DECRYPTION_TIME_MS - elapsedTime;
        qDebug() << "Operations_VP_Shows: Decryption completed in" << elapsedTime << "ms";
        qDebug() << "Operations_VP_Shows: Waiting additional" << remainingTime << "ms to ensure stability";
        
        // Use event loop delay to keep UI responsive
        QEventLoop loop;
        QTimer::singleShot(remainingTime, &loop, &QEventLoop::quit);
        loop.exec();
        
        qDebug() << "Operations_VP_Shows: Minimum time requirement met, proceeding with playback";
    } else {
        qDebug() << "Operations_VP_Shows: Decryption took" << elapsedTime << "ms, no additional delay needed";
    }

    qDebug() << "Operations_VP_Shows: Decryption successful, starting playback";

    // Store the temp file path for cleanup later
    m_currentTempFile = decryptedFilePath;

    // Create video player if not exists
    if (!m_episodePlayer) {
        qDebug() << "Operations_VP_Shows: Creating new VP_Shows_Videoplayer instance for episode playback";
        m_episodePlayer = std::make_unique<VP_Shows_Videoplayer>();

        // Set the target screen to the same screen as the main window
        if (m_mainWindow) {
            QWindow* mainWindowHandle = m_mainWindow->windowHandle();
            if (mainWindowHandle) {
                QScreen* mainWindowScreen = mainWindowHandle->screen();
                if (mainWindowScreen) {
                    m_episodePlayer->setTargetScreen(mainWindowScreen);
                    qDebug() << "Operations_VP_Shows: Set video player to open on screen:" << mainWindowScreen->name();
                } else {
                    qDebug() << "Operations_VP_Shows: Could not get main window screen";
                }
            } else {
                qDebug() << "Operations_VP_Shows: Main window handle not available";
            }
        }

        // Connect error signal
        QPointer<Operations_VP_Shows> safeThisForError = this;
        connect(m_episodePlayer.get(), &VP_Shows_Videoplayer::errorOccurred,
                this, [safeThisForError](const QString& error) {
            if (!safeThisForError) {
                qDebug() << "Critical-Operations_VP_Shows: Error handler but 'this' is invalid";
                return;
            }
            
            qDebug() << "Critical-Operations_VP_Shows: VP_Shows_Videoplayer error:" << error;
            
            // Reset autoplay flags on error
            if (safeThisForError->m_isAutoplayInProgress) {
                qDebug() << "Operations_VP_Shows: Resetting autoplay flags due to player error";
                safeThisForError->m_isAutoplayInProgress = false;
                safeThisForError->m_isRandomAutoplay = false;
                safeThisForError->m_episodeWasNearCompletion = false;
                
                // Clear pending autoplay info
                safeThisForError->m_pendingAutoplayPath.clear();
                safeThisForError->m_pendingAutoplayName.clear();
                safeThisForError->m_pendingAutoplayIsRandom = false;
            }
            
            if (safeThisForError->m_mainWindow) {
                QMessageBox::critical(safeThisForError->m_mainWindow,
                                    QObject::tr("Video Player Error"),
                                    error);
            }
            
            // Clean up temp file on error
            safeThisForError->cleanupTempFile();
        });

        // Playback speed is now saved globally in BaseVideoPlayer, no need for per-show handling

        // Connect finished signal to trigger autoplay when episode completes naturally
        QPointer<Operations_VP_Shows> safeThis = this;
        connect(m_episodePlayer.get(), &VP_Shows_Videoplayer::finished,
                this, [safeThis]() {
            if (!safeThis) {
                qDebug() << "Critical-Operations_VP_Shows: Episode finished lambda but 'this' is invalid";
                return;
            }
            
            qDebug() << "Operations_VP_Shows: Episode finished naturally";
            qDebug() << "Operations_VP_Shows: Current autoplay state:";
            qDebug() << "Operations_VP_Shows:   - Autoplay enabled:" << safeThis->m_currentShowSettings.autoplay;
            qDebug() << "Operations_VP_Shows:   - Not in progress:" << !safeThis->m_isAutoplayInProgress;
            qDebug() << "Operations_VP_Shows:   - Was near completion:" << safeThis->m_episodeWasNearCompletion;
            
            // Check if we should autoplay next episode
            if (safeThis->m_currentShowSettings.autoplay && !safeThis->m_isAutoplayInProgress && safeThis->m_episodeWasNearCompletion) {
                qDebug() << "Operations_VP_Shows: All conditions met - triggering autoplay";
                safeThis->autoplayNextEpisode();
            } else {
                qDebug() << "Operations_VP_Shows: Episode finished but autoplay conditions not met";
                if (!safeThis->m_currentShowSettings.autoplay) {
                    qDebug() << "Operations_VP_Shows:   Reason: Autoplay is disabled";
                } else if (safeThis->m_isAutoplayInProgress) {
                    qDebug() << "Operations_VP_Shows:   Reason: Autoplay already in progress";
                } else if (!safeThis->m_episodeWasNearCompletion) {
                    qDebug() << "Operations_VP_Shows:   Reason: Episode did not reach near-completion threshold";
                }
            }
            
            // Reset the flag
            safeThis->m_episodeWasNearCompletion = false;
        });
        
        // Connect playback state changed to clean up when stopped
        QPointer<Operations_VP_Shows> safeThisForState = this;
        connect(m_episodePlayer.get(), &VP_Shows_Videoplayer::playbackStateChanged,
                this, [safeThisForState](VP_VLCPlayer::PlayerState state) {
            if (!safeThisForState) {
                qDebug() << "Critical-Operations_VP_Shows: playbackStateChanged lambda but 'this' is invalid";
                return;
            }
            
            if (state == VP_VLCPlayer::PlayerState::Stopped) {
                qDebug() << "Operations_VP_Shows: Playback stopped, scheduling cleanup";

                // Stop playback tracking to save final position
                if (safeThisForState->m_playbackTracker) {
                    qDebug() << "Operations_VP_Shows: Stopping playback tracking";
                    safeThisForState->m_playbackTracker->stopTracking();
                }

                // Note: Autoplay is now handled by the episodeNearCompletion signal or finished signal
                // We don't trigger autoplay when the user manually closes the player

                // Force the media player to release the file
                safeThisForState->forceReleaseVideoFile();
                
                // Use a timer to delay cleanup to ensure file handle is fully released
                QPointer<Operations_VP_Shows> safeThisForCleanup = safeThisForState;
                QTimer::singleShot(1000, safeThisForState, [safeThisForCleanup]() {
                    if (safeThisForCleanup) {
                        safeThisForCleanup->cleanupTempFile();
                    }
                });

                // Refresh the episode list to update watched status colors
                if (!safeThisForState->m_currentShowFolder.isEmpty()) {
                    QString showFolder = safeThisForState->m_currentShowFolder;
                    QPointer<Operations_VP_Shows> safeThisForRefresh = safeThisForState;
                    QTimer::singleShot(1500, safeThisForState, [safeThisForRefresh, showFolder]() {
                        if (!safeThisForRefresh) {
                            return;
                        }
                        qDebug() << "Operations_VP_Shows: Refreshing episode list after playback";

                        // Reload watch history to get the updated watch states
                        if (safeThisForRefresh->m_watchHistory) {
                            qDebug() << "Operations_VP_Shows: Reloading watch history for updated states";
                            if (!safeThisForRefresh->m_watchHistory->loadHistory()) {
                                qDebug() << "Operations_VP_Shows: Failed to reload watch history";
                            }
                        }

                        // Now refresh the episode list with updated watch states
                        safeThisForRefresh->loadShowEpisodes(showFolder);

                        // Update the Play/Resume button text after refreshing
                        safeThisForRefresh->updatePlayButtonText();
                    });
                }
            }
        });

        // Note: We can't connect to destroyed signal directly since unique_ptr manages the object
        // The cleanup will happen through playback state changes or when closing the window
    }

    // Start tracking with the integration layer after loading but before playing
    // We'll set this up after the video loads successfully

    // Store the actual decrypted file path (with proper extension) for cleanup
    m_currentTempFile = m_lastDecryptedFilePath;
    qDebug() << "Operations_VP_Shows: Actual decrypted file with extension:" << m_currentTempFile;

    // Load and play the video using the actual decrypted file path
    qDebug() << "Operations_VP_Shows: Loading decrypted video:" << m_lastDecryptedFilePath;
    
    // Try to load the video - add retry mechanism for rare race condition
    bool loadSuccess = m_episodePlayer->loadVideo(m_lastDecryptedFilePath);
    
    // Retry mechanism: If loading fails (possibly due to async cleanup), retry once
    if (!loadSuccess && QFile::exists(encryptedFilePath)) {
        qDebug() << "Operations_VP_Shows: First load attempt failed, likely due to async cleanup race condition";
        qDebug() << "Operations_VP_Shows: Attempting to decrypt and load again...";
        
        // Generate a new temp file name for the retry
        QString tempRandomName = generateRandomFileName("");
        tempRandomName = tempRandomName.left(tempRandomName.lastIndexOf('.'));
        QString retryDecryptedFilePath = QDir(tempDecryptPath).absoluteFilePath(tempRandomName);
        
        // Try decrypting again
        bool retryDecryptSuccess = decryptVideoWithMetadata(encryptedFilePath, retryDecryptedFilePath);
        
        if (retryDecryptSuccess) {
            qDebug() << "Operations_VP_Shows: Retry decryption successful, attempting to load again";
            
            // Update the current temp file path
            m_currentTempFile = m_lastDecryptedFilePath;
            
            // Try loading the newly decrypted file
            loadSuccess = m_episodePlayer->loadVideo(m_lastDecryptedFilePath);
            
            if (loadSuccess) {
                qDebug() << "Operations_VP_Shows: Retry successful! Video loaded on second attempt";
            } else {
                qDebug() << "Operations_VP_Shows: Retry failed - video still cannot be loaded";
            }
        } else {
            qDebug() << "Operations_VP_Shows: Retry decryption failed";
        }
    }
    
    // If loading ultimately failed, clear the decryption flag
    if (!loadSuccess) {
        m_isDecrypting = false;
        qDebug() << "Operations_VP_Shows: Cleared decrypting flag after load failure";
        // The function will return early later, so we clear the flag here
    }
    
    if (loadSuccess) {
        // Clear the decryption flag now that loading is successful
        m_isDecrypting = false;
        qDebug() << "Operations_VP_Shows: Cleared decrypting flag after successful load";
        
        // Show the window first
        m_episodePlayer->show();

        // Set window title to episode name
        m_episodePlayer->setWindowTitle(tr("Playing: %1").arg(episodeName));

        // Raise and activate to ensure it's on top
        m_episodePlayer->raise();
        m_episodePlayer->activateWindow();

        // Start in fullscreen mode if the setting is enabled AND this is manual play (not autoplay)
        // During autoplay, window state is preserved from the previous episode
        if (m_currentShowSettings.autoFullscreen && !m_isAutoplayInProgress) {
            qDebug() << "Operations_VP_Shows: Manual play with auto-fullscreen enabled, starting in fullscreen mode";
            m_episodePlayer->startInFullScreen();
        } else if (!m_isAutoplayInProgress) {
            qDebug() << "Operations_VP_Shows: Manual play with auto-fullscreen disabled, starting in windowed mode";
        } else {
            qDebug() << "Operations_VP_Shows: Autoplay in progress, maintaining previous window state";
            // Window state restoration is handled by VP_Shows_Videoplayer::initializeFromPreviousSettings()
        }

        // Add a small delay to ensure video widget is properly initialized
        QTimer::singleShot(100, [this, relativeEpisodePath]() {
            // Ensure player and mainWindow are still valid
            if (!m_episodePlayer || !m_mainWindow) {
                qDebug() << "Operations_VP_Shows: Player or MainWindow no longer valid";
                return;
            }
            
            if (m_episodePlayer) {
                // Start tracking with the playback tracker
                if (m_playbackTracker && !relativeEpisodePath.isEmpty()) {
                    qDebug() << "Operations_VP_Shows: Starting playback tracking for episode";
                    qDebug() << "Operations_VP_Shows: Autoplay settings check before tracking:";
                    qDebug() << "Operations_VP_Shows:   - m_currentShowSettings.autoplay:" << m_currentShowSettings.autoplay;
                    qDebug() << "Operations_VP_Shows:   - m_isAutoplayInProgress:" << m_isAutoplayInProgress;
                    qDebug() << "Operations_VP_Shows:   - m_episodeWasNearCompletion:" << m_episodeWasNearCompletion;
                    m_playbackTracker->startTracking(relativeEpisodePath, m_episodePlayer.get());

                    // Get resume position and check if we need to resume
                    qint64 resumePosition = m_playbackTracker->getResumePosition(relativeEpisodePath);

                    // Check if we should force start from beginning (for direct play when near end)
                    if (m_forceStartFromBeginning) {
                        qDebug() << "Operations_VP_Shows: Forcing start from beginning (direct play near end)";
                        resumePosition = 0;
                        m_forceStartFromBeginning = false; // Reset the flag
                    }

                    // Playback speed is now restored globally from BaseVideoPlayer's static member

                    // Determine if we should resume
                    bool shouldResume = (resumePosition > 1000);

                    if (shouldResume) {
                        qDebug() << "Operations_VP_Shows: Will resume from position:" << resumePosition << "ms after playback starts";

                        // Connect to the playing signal to set position after playback actually starts
                        // Use a single-shot connection to ensure it only happens once
                        QMetaObject::Connection* resumeConnection = new QMetaObject::Connection;
                        *resumeConnection = connect(m_episodePlayer.get(), &VP_Shows_Videoplayer::playbackStarted,
                                this, [this, resumePosition, resumeConnection]() {
                            // Disconnect the signal to ensure this only happens once
                            disconnect(*resumeConnection);
                            delete resumeConnection;

                            qDebug() << "Operations_VP_Shows: Playback started, now setting resume position to" << resumePosition << "ms";

                            // Small delay to ensure libVLC is fully ready
                            QTimer::singleShot(200, this, [this, resumePosition]() {
                                if (m_episodePlayer) {
                                    qDebug() << "Operations_VP_Shows: Setting resume position after delay";
                                    m_episodePlayer->setPosition(resumePosition);

                                    // Force update the slider position
                                    QTimer::singleShot(50, [this, resumePosition]() {
                                        if (m_episodePlayer) {
                                            qDebug() << "Operations_VP_Shows: Forcing slider update for resume";
                                            m_episodePlayer->forceUpdateSliderPosition(resumePosition);
                                        }
                                    });
                                }
                            });
                        });
                    } else {
                        qDebug() << "Operations_VP_Shows: No resume position or forced to start from beginning";
                    }
                } else {
                    qDebug() << "Critical-Operations_VP_Shows: Cannot start playback tracking!";
                    qDebug() << "Critical-Operations_VP_Shows:   - m_playbackTracker valid:" << (m_playbackTracker != nullptr);
                    qDebug() << "Critical-Operations_VP_Shows:   - relativeEpisodePath:" << relativeEpisodePath;
                    qDebug() << "Critical-Operations_VP_Shows: AUTOPLAY WILL NOT WORK WITHOUT TRACKING!";
                }

                // Start playback
                m_episodePlayer->play();
                qDebug() << "Operations_VP_Shows: Play command issued";
                
                // Reset autoplay flags now that we've successfully started the next episode
                if (m_isAutoplayInProgress) {
                    qDebug() << "Operations_VP_Shows: Autoplay successful - resetting flags";
                    m_isAutoplayInProgress = false;
                    m_isRandomAutoplay = false;
                    m_episodeWasNearCompletion = false;
                }
            }
        });
    } else {
        qDebug() << "Critical-Operations_VP_Shows: Failed to load decrypted video after retry";
        
        // Reset autoplay flags if this was an autoplay attempt
        if (m_isAutoplayInProgress) {
            qDebug() << "Operations_VP_Shows: Resetting autoplay flags due to video load failure";
            m_isAutoplayInProgress = false;
            m_isRandomAutoplay = false;
            m_episodeWasNearCompletion = false;
            
            // Clear pending autoplay info
            m_pendingAutoplayPath.clear();
            m_pendingAutoplayName.clear();
            m_pendingAutoplayIsRandom = false;
        }
        
        QMessageBox::warning(m_mainWindow,
                           tr("Load Failed"),
                           tr("Failed to load the decrypted video file after retry."));
        // Clean up temp file if loading failed
        cleanupTempFile();
    }
}

bool Operations_VP_Shows::decryptVideoWithMetadata(const QString& sourceFile, const QString& targetFile)
{
    qDebug() << "Operations_VP_Shows: Decrypting video with metadata from:" << sourceFile;
    
    // Check if file is locked - if it is, just fail immediately
    if (VP_MetadataLockManager::instance()->isLocked(sourceFile)) {
        qDebug() << "Operations_VP_Shows: File is locked, cannot decrypt";
        return false;
    }
    
    QFile source(sourceFile);
    if (!source.open(QIODevice::ReadOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open source file:" << source.errorString();
        return false;
    }
    
    // Create metadata manager to read the metadata
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Read and verify metadata (but don't write it to target)
    VP_ShowsMetadata::ShowMetadata metadata;
    if (!metadataManager.readFixedSizeEncryptedMetadata(&source, metadata)) {
        qDebug() << "Operations_VP_Shows: Failed to read metadata from encrypted file";
        source.close();
        return false;
    }
    
    qDebug() << "Operations_VP_Shows: Read metadata - Show:" << metadata.showName 
             << "Episode:" << metadata.EPName << "Original filename:" << metadata.filename;
    
    // Extract the actual file extension from the original filename in metadata
    QString actualExtension;
    if (!metadata.filename.isEmpty()) {
        QFileInfo originalFileInfo(metadata.filename);
        actualExtension = originalFileInfo.suffix();
    }
    
    // Update target filename with the actual extension
    QString actualTargetFile = targetFile;
    if (!actualExtension.isEmpty()) {
        actualTargetFile = targetFile + "." + actualExtension;
    }
    
    QFile target(actualTargetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open target file:" << target.errorString();
        source.close();
        return false;
    }
    
    // Skip past metadata (already read) - the metadata is METADATA_RESERVED_SIZE bytes
    source.seek(VP_ShowsMetadata::METADATA_RESERVED_SIZE);
    
    // Decrypt file content in chunks
    QDataStream stream(&source);
    
    // Track progress for large files
    qint64 fileSize = source.size();
    qint64 processedBytes = VP_ShowsMetadata::METADATA_RESERVED_SIZE;
    qint64 lastProgressUpdate = 0;
    
    while (!source.atEnd()) {
        // Process events every 10MB to keep UI responsive
        if (processedBytes - lastProgressUpdate > 10 * 1024 * 1024) {
            QCoreApplication::processEvents();
            lastProgressUpdate = processedBytes;
        }
        // Read chunk size
        qint32 chunkSize;
        stream >> chunkSize;
        
        if (chunkSize <= 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
            qDebug() << "Operations_VP_Shows: Invalid chunk size:" << chunkSize;
            source.close();
            target.close();
            // Use secure deletion for partially written temp file (1 pass for temp files, allowExternalFiles=false)
            if (!OperationsFiles::secureDelete(actualTargetFile, 1, false)) {
                qDebug() << "Operations_VP_Shows: Failed to securely delete partial temp file:" << actualTargetFile;
            }
            return false;
        }
        
        // Read encrypted chunk
        QByteArray encryptedChunk = source.read(chunkSize);
        if (encryptedChunk.size() != chunkSize) {
            qDebug() << "Operations_VP_Shows: Failed to read complete chunk";
            source.close();
            target.close();
            // Use secure deletion for partially written temp file (1 pass for temp files, allowExternalFiles=false)
            if (!OperationsFiles::secureDelete(actualTargetFile, 1, false)) {
                qDebug() << "Operations_VP_Shows: Failed to securely delete partial temp file:" << actualTargetFile;
            }
            return false;
        }
        
        // Decrypt chunk
        QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(m_mainWindow->user_Key, encryptedChunk);
        if (decryptedChunk.isEmpty()) {
            qDebug() << "Operations_VP_Shows: Failed to decrypt chunk";
            source.close();
            target.close();
            // Use secure deletion for partially written temp file (1 pass for temp files, allowExternalFiles=false)
            if (!OperationsFiles::secureDelete(actualTargetFile, 1, false)) {
                qDebug() << "Operations_VP_Shows: Failed to securely delete partial temp file:" << actualTargetFile;
            }
            return false;
        }
        
        // Write decrypted chunk
        qint64 written = target.write(decryptedChunk);
        if (written != decryptedChunk.size()) {
            qDebug() << "Operations_VP_Shows: Failed to write decrypted chunk";
            source.close();
            target.close();
            // Use secure deletion for partially written temp file (1 pass for temp files, allowExternalFiles=false)
            if (!OperationsFiles::secureDelete(actualTargetFile, 1, false)) {
                qDebug() << "Operations_VP_Shows: Failed to securely delete partial temp file:" << actualTargetFile;
            }
            return false;
        }
        
        processedBytes += chunkSize;
    }
    
    source.close();
    target.close();
    
    // Store the actual decrypted file path in the member variable for the caller
    m_lastDecryptedFilePath = actualTargetFile;
    
    qDebug() << "Operations_VP_Shows: Successfully decrypted video to:" << actualTargetFile;
    return true;
}

void Operations_VP_Shows::cleanupTempFile()
{
    if (m_currentTempFile.isEmpty()) {
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Cleaning up temp file:" << m_currentTempFile;
    
    // Check if file exists
    if (QFile::exists(m_currentTempFile)) {
        // Ensure file permissions allow deletion (Windows fix)
#ifdef Q_OS_WIN
        QFile::setPermissions(m_currentTempFile, 
                            QFile::ReadOwner | QFile::WriteOwner | 
                            QFile::ReadUser | QFile::WriteUser);
#endif
        
        // Use secure delete for temp file (3 passes for decrypted video content)
        // Set allowExternalFiles to false since this should be in Data/username/temp
        if (OperationsFiles::secureDelete(m_currentTempFile, 3, false)) {
            qDebug() << "Operations_VP_Shows: Successfully securely deleted temp file";
        } else {
            qDebug() << "Operations_VP_Shows: Failed to securely delete temp file, trying regular delete";
            // Try regular delete as fallback
            if (QFile::remove(m_currentTempFile)) {
                qDebug() << "Operations_VP_Shows: Successfully deleted temp file with regular delete";
            } else {
                qDebug() << "Operations_VP_Shows: Failed to delete temp file with regular delete";
                // Schedule another attempt later with secure delete
                QTimer::singleShot(2000, this, [this]() {
                    if (!m_currentTempFile.isEmpty() && QFile::exists(m_currentTempFile)) {
                        qDebug() << "Operations_VP_Shows: Retry secure deleting temp file";
                        if (!OperationsFiles::secureDelete(m_currentTempFile, 3, false)) {
                            // Final attempt with regular delete
                            qDebug() << "Operations_VP_Shows: Secure delete retry failed, trying regular delete";
                            QFile::remove(m_currentTempFile);
                        }
                    }
                });
            }
        }
    }
    
    m_currentTempFile.clear();
}

void Operations_VP_Shows::forceReleaseVideoFile()
{
    if (m_episodePlayer) {
        qDebug() << "Operations_VP_Shows: Forcing media player to release file";
        // Stop playback
        m_episodePlayer->stop();
        // Unload the media properly without triggering errors
        m_episodePlayer->unloadVideo();
        // Process events to ensure the release takes effect
        QCoreApplication::processEvents();
    }
}

void Operations_VP_Shows::clearContextMenuData()
{
    qDebug() << "Operations_VP_Shows: Clearing context menu data";
    
    // Clear the tree item pointer (raw pointer - must be manually cleared)
    m_contextMenuTreeItem = nullptr;
    
    // Clear stored paths
    m_contextMenuEpisodePath.clear();
    m_contextMenuEpisodePaths.clear();
    
    // Clear show-related context menu data
    m_contextMenuShowName.clear();
    m_contextMenuShowPath.clear();
    
    qDebug() << "Operations_VP_Shows: Context menu data cleared";
}

void Operations_VP_Shows::checkAndDisplayNewEpisodes(const QString& showFolderPath, int tmdbShowId)
{
    qDebug() << "Operations_VP_Shows: Checking for new episodes with TMDB ID:" << tmdbShowId;
    
    // Reset state
    m_currentShowHasNewEpisodes = false;
    m_currentShowNewEpisodeCount = 0;
    
    // Check if episode detector is available
    if (!m_episodeDetector) {
        qDebug() << "Operations_VP_Shows: Episode detector not available";
        displayNewEpisodeIndicator(false, 0);
        return;
    }
    
    // Check if TMDB is enabled globally
    if (!VP_ShowsConfig::isTMDBEnabled()) {
        qDebug() << "Operations_VP_Shows: TMDB is disabled globally";
        displayNewEpisodeIndicator(false, 0);
        return;
    }
    
    // Check if we have a valid TMDB ID
    if (tmdbShowId <= 0) {
        qDebug() << "Operations_VP_Shows: Invalid TMDB ID:" << tmdbShowId;
        displayNewEpisodeIndicator(false, 0);
        return;
    }
    
    // Use the episode detector to check for new episodes
    VP_ShowsEpisodeDetector::NewEpisodeInfo newEpisodeInfo = 
        m_episodeDetector->checkForNewEpisodes(showFolderPath, tmdbShowId);
    
    if (newEpisodeInfo.hasNewEpisodes) {
        m_currentShowHasNewEpisodes = true;
        m_currentShowNewEpisodeCount = newEpisodeInfo.newEpisodeCount;
        
        qDebug() << "Operations_VP_Shows: Found" << m_currentShowNewEpisodeCount 
                 << "new episode(s) for show";
        qDebug() << "Operations_VP_Shows: Latest new episode: S" << newEpisodeInfo.latestSeason 
                 << "E" << newEpisodeInfo.latestEpisode 
                 << "-" << newEpisodeInfo.latestNewEpisodeName;
        
        // Display the indicator
        displayNewEpisodeIndicator(true, m_currentShowNewEpisodeCount);
    } else {
        qDebug() << "Operations_VP_Shows: No new episodes detected";
        displayNewEpisodeIndicator(false, 0);
    }
}

void Operations_VP_Shows::displayNewEpisodeIndicator(bool hasNewEpisodes, int newEpisodeCount)
{
    qDebug() << "Operations_VP_Shows: Displaying new episode indicator - Has new:" << hasNewEpisodes
             << "Count:" << newEpisodeCount;

    // Check if we have the image label
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->label_VP_Shows_Display_Image) {
        qDebug() << "Operations_VP_Shows: Image label not available";
        return;
    }

    // Get the current pixmap from the label
    QPixmap currentPixmap = m_mainWindow->ui->label_VP_Shows_Display_Image->pixmap();
    if (currentPixmap.isNull()) {
        qDebug() << "Operations_VP_Shows: No poster currently displayed";
        return;
    }

    // Create a copy of the current pixmap to modify
    QPixmap modifiedPoster = currentPixmap;

    if (hasNewEpisodes && newEpisodeCount > 0) {
        // Add the new episode indicator
        QPainter painter(&modifiedPoster);
        painter.setRenderHint(QPainter::Antialiasing);

        // Calculate position for top-right corner
        int iconSize = 32;  // Size of the indicator
        int margin = 10;
        int x = modifiedPoster.width() - iconSize - margin;
        int y = margin;

        // Draw a semi-transparent background circle for the icon
        painter.setPen(Qt::NoPen);
        QColor bgColor(255, 69, 0, 200);  // Orange-red with transparency
        painter.setBrush(bgColor);
        painter.drawEllipse(x, y, iconSize, iconSize);

        // Get the "new" icon from Qt's standard icons
        QIcon newIcon = m_mainWindow->style()->standardIcon(QStyle::SP_FileDialogNewFolder);

        // Draw the icon (white colored)
        QPixmap iconPixmap = newIcon.pixmap(iconSize * 0.6, iconSize * 0.6);

        // Create a white version of the icon
        QPixmap whiteIcon(iconPixmap.size());
        whiteIcon.fill(Qt::transparent);
        QPainter iconPainter(&whiteIcon);
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        iconPainter.drawPixmap(0, 0, iconPixmap);
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        iconPainter.fillRect(whiteIcon.rect(), Qt::white);
        iconPainter.end();

        // Draw the white icon centered in the circle
        int iconX = x + (iconSize - whiteIcon.width()) / 2;
        int iconY = y + (iconSize - whiteIcon.height()) / 2;
        painter.drawPixmap(iconX, iconY, whiteIcon);

        // If there's more than one new episode, add a count badge
        if (newEpisodeCount > 1) {
            // Draw count badge below the icon circle
            int badgeWidth = 24;
            int badgeHeight = 16;
            int badgeX = x + (iconSize - badgeWidth) / 2;
            int badgeY = y + iconSize - 5;

            // Draw badge background
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(220, 20, 60, 230));  // Crimson color
            painter.drawRoundedRect(badgeX, badgeY, badgeWidth, badgeHeight, 3, 3);

            // Draw count text
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setBold(true);
            font.setPixelSize(11);
            painter.setFont(font);

            QString countText = QString::number(newEpisodeCount);
            if (newEpisodeCount > 99) {
                countText = "99+";
            }

            QRect textRect(badgeX, badgeY, badgeWidth, badgeHeight);
            painter.drawText(textRect, Qt::AlignCenter, countText);
        }

        painter.end();

        qDebug() << "Operations_VP_Shows: Added new episode indicator to poster";
    }
    // If hasNewEpisodes is false, we just use the unmodified poster (already in modifiedPoster)

    // Update the label with the modified poster
    m_mainWindow->ui->label_VP_Shows_Display_Image->setPixmap(modifiedPoster);
}

void Operations_VP_Shows::onPlayContinueClicked()
{
    qDebug() << "Operations_VP_Shows: Play/Continue button clicked";
    
    // Check if we're currently decrypting an episode
    if (m_isDecrypting) {
        qDebug() << "Operations_VP_Shows: Currently decrypting an episode, ignoring button press";
        return;
    }
    
    // Check if there's already a video player window open
    if (m_episodePlayer && m_episodePlayer->isVisible()) {
        qDebug() << "Operations_VP_Shows: Video player window already open, bringing to front";
        
        // Bring the existing window to the forefront
        m_episodePlayer->raise();
        m_episodePlayer->activateWindow();
        m_episodePlayer->setFocus();
        
        // If minimized, restore it
        if (m_episodePlayer->isMinimized()) {
            m_episodePlayer->showNormal();
        }
        
        return;
    }
    
    // Use the helper function to determine which episode to play
    QTreeWidgetItem* episodeToPlay = determineEpisodeToPlay();
    
    if (!episodeToPlay) {
        qDebug() << "Operations_VP_Shows: No episode to play";
        return;
    }
    
    // Play the selected episode
    onEpisodeDoubleClicked(episodeToPlay, 0);
}

void Operations_VP_Shows::setupContextMenu()
{
    qDebug() << "Operations_VP_Shows: Setting up context menu for shows list";
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        qDebug() << "Operations_VP_Shows: Cannot setup context menu - list widget not available";
        return;
    }
    
    // Enable context menu for the list widget
    m_mainWindow->ui->listWidget_VP_List_List->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Connect the context menu signal
    connect(m_mainWindow->ui->listWidget_VP_List_List, &QListWidget::customContextMenuRequested,
            this, &Operations_VP_Shows::showContextMenu);
    
    qDebug() << "Operations_VP_Shows: Context menu setup complete";
}

void Operations_VP_Shows::setupPosterContextMenu()
{
    qDebug() << "Operations_VP_Shows: Setting up context menu for poster on display page";
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->label_VP_Shows_Display_Image) {
        qDebug() << "Operations_VP_Shows: Cannot setup poster context menu - label widget not available";
        return;
    }
    
    // CRITICAL: Disconnect any existing connections first to prevent duplicate signals
    // This fixes the bug where context menu becomes hard to close after multiple setups
    disconnect(m_mainWindow->ui->label_VP_Shows_Display_Image, &QLabel::customContextMenuRequested,
               this, &Operations_VP_Shows::showPosterContextMenu);
    
    // Enable context menu for the poster label
    m_mainWindow->ui->label_VP_Shows_Display_Image->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Connect the context menu signal
    connect(m_mainWindow->ui->label_VP_Shows_Display_Image, &QLabel::customContextMenuRequested,
            this, &Operations_VP_Shows::showPosterContextMenu);
    
    // Also setup context menu for the show name label
    if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
        qDebug() << "Operations_VP_Shows: Setting up context menu for show name label";
        
        // CRITICAL: Disconnect any existing connections first
        disconnect(m_mainWindow->ui->label_VP_Shows_Display_Name, &QLabel::customContextMenuRequested,
                   this, &Operations_VP_Shows::showPosterContextMenu);
        
        m_mainWindow->ui->label_VP_Shows_Display_Name->setContextMenuPolicy(Qt::CustomContextMenu);
        
        // Connect the context menu signal for the name label
        connect(m_mainWindow->ui->label_VP_Shows_Display_Name, &QLabel::customContextMenuRequested,
                this, &Operations_VP_Shows::showPosterContextMenu);
        
        qDebug() << "Operations_VP_Shows: Show name label context menu setup complete";
    }
    
    qDebug() << "Operations_VP_Shows: Poster context menu setup complete";
}

void Operations_VP_Shows::showContextMenu(const QPoint& pos)
{
    qDebug() << "Operations_VP_Shows: Context menu requested";
    
    // Check for valid m_mainWindow pointer
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow pointer is null";
        return;
    }
    
    if (!m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        return;
    }
    
    // Get the item at the position
    QListWidgetItem* item = m_mainWindow->ui->listWidget_VP_List_List->itemAt(pos);
    
    if (!item) {
        qDebug() << "Operations_VP_Shows: No item at context menu position";
        return;
    }
    
    // Clear any previous context menu data before setting new values
    clearContextMenuData();
    
    // Store the show name and path for the context menu actions
    m_contextMenuShowName = item->text();
    m_contextMenuShowPath = item->data(Qt::UserRole).toString();
    
    if (m_contextMenuShowPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Show path not found for:" << m_contextMenuShowName;
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Creating context menu for show:" << m_contextMenuShowName;
    
    // Create the context menu
    QMenu* contextMenu = new QMenu(m_mainWindow);
    
    // Add Episodes action
    QAction* addEpisodesAction = contextMenu->addAction(tr("Add Episodes to \"%1\"").arg(m_contextMenuShowName));
    connect(addEpisodesAction, &QAction::triggered, this, &Operations_VP_Shows::addEpisodesToShow);
    
    // Decrypt and Export action
    QAction* exportAction = contextMenu->addAction(tr("Decrypt and Export \"%1\"").arg(m_contextMenuShowName));
    connect(exportAction, &QAction::triggered, this, &Operations_VP_Shows::decryptAndExportShow);
    
    // Delete action
    QAction* deleteAction = contextMenu->addAction(tr("Delete \"%1\"").arg(m_contextMenuShowName));
    connect(deleteAction, &QAction::triggered, this, &Operations_VP_Shows::deleteShow);
    
    // Add separator
    contextMenu->addSeparator();
    
    // Show in File Explorer action
    QAction* showInExplorerAction = contextMenu->addAction(tr("Show in File Explorer"));
    connect(showInExplorerAction, &QAction::triggered, this, &Operations_VP_Shows::showInFileExplorer);
    
    // Show the menu at the cursor position
    contextMenu->exec(m_mainWindow->ui->listWidget_VP_List_List->mapToGlobal(pos));
    
    // Clean up
    contextMenu->deleteLater();
    
    // Clear context menu data after use to prevent stale references
    clearContextMenuData();
}

void Operations_VP_Shows::showPosterContextMenu(const QPoint& pos)
{
    qDebug() << "Operations_VP_Shows: Poster context menu requested";
    
    // Check for valid m_mainWindow pointer
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow pointer is null";
        return;
    }
    
    if (!m_mainWindow->ui || !m_mainWindow->ui->label_VP_Shows_Display_Image) {
        return;
    }
    
    // Check if we have a current show being displayed
    if (m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show currently displayed";
        return;
    }
    
    // Get the show name from the display label
    QString showName;
    if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
        showName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
    }
    
    if (showName.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Show name not available";
        return;
    }
    
    // Clear any previous context menu data before setting new values
    clearContextMenuData();
    
    // Store the show name and path for the context menu actions
    m_contextMenuShowName = showName;
    m_contextMenuShowPath = m_currentShowFolder;
    
    qDebug() << "Operations_VP_Shows: Creating poster context menu for show:" << m_contextMenuShowName;
    
    // Create the context menu
    QMenu* contextMenu = new QMenu(m_mainWindow);
    
    // Add Episodes action
    QAction* addEpisodesAction = contextMenu->addAction(tr("Add Episodes to \"%1\"").arg(m_contextMenuShowName));
    connect(addEpisodesAction, &QAction::triggered, this, &Operations_VP_Shows::addEpisodesToShow);
    
    // Decrypt and Export action
    QAction* exportAction = contextMenu->addAction(tr("Decrypt and Export \"%1\"").arg(m_contextMenuShowName));
    connect(exportAction, &QAction::triggered, this, &Operations_VP_Shows::decryptAndExportShow);
    
    // Delete action
    QAction* deleteAction = contextMenu->addAction(tr("Delete \"%1\"").arg(m_contextMenuShowName));
    connect(deleteAction, &QAction::triggered, this, &Operations_VP_Shows::deleteShow);
    
    // Add separator
    contextMenu->addSeparator();
    
    // Show in File Explorer action
    QAction* showInExplorerAction = contextMenu->addAction(tr("Show in File Explorer"));
    connect(showInExplorerAction, &QAction::triggered, this, &Operations_VP_Shows::showInFileExplorer);
    
    // Determine which widget triggered the context menu
    // The sender() will be the widget that emitted the signal
    QWidget* senderWidget = qobject_cast<QWidget*>(sender());
    
    // Show the menu at the cursor position relative to the widget that triggered it
    if (senderWidget) {
        contextMenu->exec(senderWidget->mapToGlobal(pos));
    } else {
        // Fallback to using the image label if sender is not available
        contextMenu->exec(m_mainWindow->ui->label_VP_Shows_Display_Image->mapToGlobal(pos));
    }
    
    // Clean up
    contextMenu->deleteLater();
    
    // Clear context menu data after use to prevent stale references
    clearContextMenuData();
    
    // CRITICAL: Clear any pending events to prevent context menu from re-triggering
    // This fixes the bug where context menu reopens after dialogs are cancelled
    QCoreApplication::processEvents();
    
    // Also remove any posted customContextMenuRequested events for both labels
    if (m_mainWindow->ui->label_VP_Shows_Display_Image) {
        QCoreApplication::removePostedEvents(m_mainWindow->ui->label_VP_Shows_Display_Image, QEvent::ContextMenu);
    }
    if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
        QCoreApplication::removePostedEvents(m_mainWindow->ui->label_VP_Shows_Display_Name, QEvent::ContextMenu);
    }
}

void Operations_VP_Shows::addEpisodesToShow()
{
    // Clear any stale context menu episode paths to prevent confusion
    m_contextMenuEpisodePaths.clear();
    m_contextMenuEpisodePath.clear();
    
    // Determine the show name and path based on context
    // This function can be called from either the show list context menu or the episode tree context menu
    QString showName;
    QString showPath;
    
    // Check if we're being called from the episode tree context menu
    // In that case, use the current show being displayed
    if (m_contextMenuShowName.isEmpty() || m_contextMenuShowPath.isEmpty()) {
        // Try to use the current show being displayed
        if (!m_currentShowFolder.isEmpty() && m_mainWindow && m_mainWindow->ui && 
            m_mainWindow->ui->label_VP_Shows_Display_Name) {
            showName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
            showPath = m_currentShowFolder;
            qDebug() << "Operations_VP_Shows: Add episodes to current show:" << showName;
        } else {
            qDebug() << "Operations_VP_Shows: No show selected for adding episodes";
            return;
        }
    } else {
        // Being called from show list context menu
        showName = m_contextMenuShowName;
        showPath = m_contextMenuShowPath;
        qDebug() << "Operations_VP_Shows: Add episodes to show:" << showName;
    }
    
    // Create a simple dialog to ask user to choose between files or folder (same as Add Show)
    QDialog selectionDialog(m_mainWindow);
    selectionDialog.setWindowTitle(tr("Select Import Method"));
    selectionDialog.setModal(true);
    selectionDialog.setFixedSize(300, 80);
    
    QVBoxLayout* layout = new QVBoxLayout(&selectionDialog);
    layout->setContentsMargins(10, 10, 10, 10);  // Tighter margins
    layout->setSpacing(10);  // Tighter spacing
    
    QLabel* label = new QLabel(tr("How would you like to add episodes?"), &selectionDialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    
    layout->addSpacing(5);  // Reduced from 20 to 5
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(5);  // Tighter button spacing
    
    // Create buttons with icons
    QPushButton* selectFilesBtn = new QPushButton(tr("Select Files"), &selectionDialog);
    selectFilesBtn->setIcon(QIcon::fromTheme("document-open", 
                            m_mainWindow->style()->standardIcon(QStyle::SP_FileIcon)));
    
    QPushButton* selectFolderBtn = new QPushButton(tr("Select Folder"), &selectionDialog);
    selectFolderBtn->setIcon(QIcon::fromTheme("folder-open", 
                            m_mainWindow->style()->standardIcon(QStyle::SP_DirIcon)));
    
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), &selectionDialog);
    
    buttonLayout->addWidget(selectFilesBtn);
    buttonLayout->addWidget(selectFolderBtn);
    buttonLayout->addWidget(cancelBtn);
    
    layout->addLayout(buttonLayout);
    
    // Variable to track which option was selected
    enum SelectionType { None, Files, Folder };
    SelectionType selectedType = None;
    
    connect(selectFilesBtn, &QPushButton::clicked, [&]() {
        selectedType = Files;
        selectionDialog.accept();
    });
    
    connect(selectFolderBtn, &QPushButton::clicked, [&]() {
        selectedType = Folder;
        selectionDialog.accept();
    });
    
    connect(cancelBtn, &QPushButton::clicked, [&]() {
        selectionDialog.reject();
    });
    
    if (selectionDialog.exec() != QDialog::Accepted || selectedType == None) {
        qDebug() << "Operations_VP_Shows: Import method selection cancelled";
        return;
    }
    
    QStringList selectedFiles;
    QString folderPath;
    
    if (selectedType == Files) {
        // Select multiple files
        qDebug() << "Operations_VP_Shows: User chose to select files for adding episodes";
        
        QString filter = "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg *.3gp);;All Files (*.*)";
        
        selectedFiles = QFileDialog::getOpenFileNames(
            m_mainWindow,
            tr("Select Video Files to Add"),
            QDir::homePath(),
            filter
        );
        
        if (selectedFiles.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No files selected for adding";
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Selected" << selectedFiles.size() << "files for adding";
        
    } else if (selectedType == Folder) {
        // Select folder
        qDebug() << "Operations_VP_Shows: User chose to select folder for adding episodes";
        
        folderPath = QFileDialog::getExistingDirectory(
            m_mainWindow,
            tr("Select Folder Containing Episodes"),
            QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );
        
        if (folderPath.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No folder selected";
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Selected folder:" << folderPath;
        
        // Find all video files in the folder and subfolders
        selectedFiles = findVideoFiles(folderPath);
        
        if (selectedFiles.isEmpty()) {
            QMessageBox::warning(m_mainWindow,
                               tr("No Video Files Found"),
                               tr("The selected folder does not contain any compatible video files."));
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Found" << selectedFiles.size() << "video files in folder";
    }
    
    // At this point we have selectedFiles from either method
    qDebug() << "Operations_VP_Shows: Total files to add:" << selectedFiles.size();
    
    // Clear the source folder path since we're adding individual files (no folder cleanup needed)
    m_originalSourceFolderPath.clear();
    
    // Get the metadata from an existing episode to know the language and translation
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    VP_ShowsMetadata::ShowMetadata existingMetadata;
    
    // Find the first video file in the show folder to get its metadata
    QDir showDir(showPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList existingVideos = showDir.entryList(QDir::Files);
    
    QString language, translation;
    if (!existingVideos.isEmpty()) {
        QString firstVideoPath = showDir.absoluteFilePath(existingVideos.first());
        if (metadataManager.readMetadataFromFile(firstVideoPath, existingMetadata)) {
            language = existingMetadata.language;
            translation = existingMetadata.translation;
        }
    }
    
    // Show the add episodes dialog with the show name pre-filled and disabled
    VP_ShowsAddDialog addDialog(showName, m_mainWindow);
    
    // Set the show name field as read-only since we're adding to an existing show
    addDialog.setShowNameReadOnly(true);
    addDialog.setWindowTitle(tr("Add Episodes to %1").arg(showName));
    
    // Initialize with existing show data (poster and description)
    addDialog.initializeForExistingShow(showPath, m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    if (addDialog.exec() != QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Add episodes dialog cancelled";
        return;
    }
    
    // Get the language and translation from the dialog
    QString newLanguage = addDialog.getLanguage();
    QString newTranslation = addDialog.getTranslationMode();
    
    qDebug() << "Operations_VP_Shows: Adding episodes with Language:" << newLanguage 
             << "Translation:" << newTranslation;
    
    // Check which episodes are new
    QStringList existingEpisodes;
    for (const QString& videoFile : existingVideos) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        VP_ShowsMetadata::ShowMetadata epMetadata;
        
        if (metadataManager.readMetadataFromFile(videoPath, epMetadata)) {
            // Only consider episodes with matching language and translation
            if (epMetadata.language == newLanguage && epMetadata.translation == newTranslation) {
                int seasonNum = epMetadata.season.toInt();
                int episodeNum = epMetadata.episode.toInt();
                
                if (seasonNum == 0 || episodeNum == 0) {
                    VP_ShowsTMDB::parseEpisodeFromFilename(epMetadata.filename, seasonNum, episodeNum);
                }
                
                QString episodeId;
                if (seasonNum > 0 && episodeNum > 0) {
                    episodeId = QString("S%1E%2").arg(seasonNum, 2, 10, QChar('0'))
                                                .arg(episodeNum, 2, 10, QChar('0'));
                } else {
                    episodeId = epMetadata.filename;
                }
                
                existingEpisodes.append(episodeId);
            }
        }
    }
    
    // Filter new episodes
    QStringList filesToImport = filterNewEpisodes(selectedFiles, existingEpisodes, 
                                                 showName, newLanguage, newTranslation);
    
    if (filesToImport.isEmpty()) {
        QMessageBox::information(m_mainWindow,
                               tr("No New Episodes"),
                               tr("All selected episodes already exist in the show with the specified language and translation."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Found" << filesToImport.size() << "new episodes to add";
    
    // Generate target file paths
    QStringList targetFiles;
    for (const QString& sourceFile : filesToImport) {
        QFileInfo fileInfo(sourceFile);
        // Always use .mmvid extension for encrypted video files
        QString randomName = generateRandomFileName("mmvid");
        QString targetFile = showDir.absoluteFilePath(randomName);
        targetFiles.append(targetFile);
    }
    
    // Store state for completion message
    m_isUpdatingExistingShow = true;
    m_originalEpisodeCount = selectedFiles.size();
    m_newEpisodeCount = filesToImport.size();
    m_currentImportOutputPath = showPath; // Store the show path for use in onEncryptionComplete
    
    // Create and show progress dialog
    if (!m_encryptionDialog) {
        m_encryptionDialog = new VP_ShowsEncryptionProgressDialog(m_mainWindow);
        connect(m_encryptionDialog, &VP_ShowsEncryptionProgressDialog::encryptionComplete,
                this, &Operations_VP_Shows::onEncryptionComplete);
    }
    
    // Get TMDB preference from the dialog
    bool useTMDB = addDialog.isUsingTMDB();
    QPixmap customPoster;  // Empty pixmap (not used when adding to existing show)
    QString customDescription;  // Empty string
    
    // Get playback settings from the dialog
    bool autoplay = addDialog.isAutoplayEnabled();
    bool skipIntro = addDialog.isSkipIntroEnabled();
    bool skipOutro = addDialog.isSkipOutroEnabled();
    
    // Load existing show settings to preserve show ID and other settings
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    VP_ShowsSettings::ShowSettings existingSettings;
    if (settingsManager.loadShowSettings(showPath, existingSettings)) {
        // Preserve the existing show ID if it exists
        m_dialogShowId = 0;  // Default to 0
        if (!existingSettings.showId.isEmpty() && existingSettings.showId != "error") {
            bool ok = false;
            int showId = existingSettings.showId.toInt(&ok);
            if (ok && showId > 0) {
                m_dialogShowId = showId;
                qDebug() << "Operations_VP_Shows: Preserving existing show ID:" << m_dialogShowId;
            }
        }
        m_dialogShowName = existingSettings.showName;  // Preserve the show name
    } else {
        m_dialogShowId = 0;  // No existing settings or failed to load
        m_dialogShowName = showName;  // Use the current show name
    }
    
    // Store dialog settings for use in onEncryptionComplete
    m_dialogAutoplay = autoplay;
    m_dialogSkipIntro = skipIntro;
    m_dialogSkipOutro = skipOutro;
    m_dialogUseTMDB = useTMDB;
    
    qDebug() << "Operations_VP_Shows: Dialog settings - Autoplay:" << autoplay << "SkipIntro:" << skipIntro << "SkipOutro:" << skipOutro;
    
    // Get the parse mode from the dialog (default to ParseFromFile for adding episodes)
    VP_ShowsEncryptionWorker::ParseMode parseMode = VP_ShowsEncryptionWorker::ParseFromFile;
    
    // Start encryption with the show name and metadata
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, 
                                       m_mainWindow->user_Key, m_mainWindow->user_Username, 
                                       newLanguage, newTranslation, useTMDB, customPoster, customDescription, parseMode, m_dialogShowId);
}

void Operations_VP_Shows::decryptAndExportShow()
{
    qDebug() << "Operations_VP_Shows: Decrypt and export show:" << m_contextMenuShowName;
    
    if (m_contextMenuShowName.isEmpty() || m_contextMenuShowPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show selected for export";
        return;
    }
    
    // Select export folder
    QString exportPath = QFileDialog::getExistingDirectory(
        m_mainWindow,
        tr("Select Export Folder"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (exportPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No export folder selected";
        // Clear any pending events after folder dialog closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Clear any pending events after folder dialog closes
    QCoreApplication::processEvents();
    
    // Estimate the size needed
    qint64 estimatedSize = estimateDecryptedSize(m_contextMenuShowPath);
    
    // Check available disk space
    QStorageInfo storageInfo(exportPath);
    qint64 availableSpace = storageInfo.bytesAvailable();
    
    qDebug() << "Operations_VP_Shows: Estimated size:" << estimatedSize << "Available space:" << availableSpace;
    
    if (availableSpace < estimatedSize) {
        // Convert sizes to human-readable format
        auto formatSize = [](qint64 bytes) -> QString {
            const qint64 kb = 1024;
            const qint64 mb = kb * 1024;
            const qint64 gb = mb * 1024;
            
            if (bytes >= gb) {
                return QString("%1 GB").arg(bytes / double(gb), 0, 'f', 2);
            } else if (bytes >= mb) {
                return QString("%1 MB").arg(bytes / double(mb), 0, 'f', 2);
            } else if (bytes >= kb) {
                return QString("%1 KB").arg(bytes / double(kb), 0, 'f', 2);
            } else {
                return QString("%1 bytes").arg(bytes);
            }
        };
        
        QMessageBox::warning(m_mainWindow,
                           tr("Insufficient Disk Space"),
                           tr("There is not enough space on the disk to export this show.\n\n"
                              "Show size: %1\n"
                              "Available space: %2\n\n"
                              "Please free up some space and try again.")
                           .arg(formatSize(estimatedSize))
                           .arg(formatSize(availableSpace)));
        // Clear any pending events after message box closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Count seasons and episodes
    QDir showDir(m_contextMenuShowPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files);
    
    // Count unique seasons
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    QSet<int> seasons;
    int episodeCount = videoFiles.size();
    
    for (const QString& videoFile : videoFiles) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        VP_ShowsMetadata::ShowMetadata metadata;
        
        if (metadataManager.readMetadataFromFile(videoPath, metadata)) {
            int seasonNum = metadata.season.toInt();
            if (seasonNum > 0) {
                seasons.insert(seasonNum);
            }
        }
    }
    
    int seasonCount = seasons.isEmpty() ? 1 : seasons.size();
    
    // Format size for display
    auto formatSize = [](qint64 bytes) -> QString {
        const qint64 kb = 1024;
        const qint64 mb = kb * 1024;
        const qint64 gb = mb * 1024;
        
        if (bytes >= gb) {
            return QString("%1 GB").arg(bytes / double(gb), 0, 'f', 2);
        } else if (bytes >= mb) {
            return QString("%1 MB").arg(bytes / double(mb), 0, 'f', 2);
        } else {
            return QString("%1 KB").arg(bytes / double(kb), 0, 'f', 2);
        }
    };
    
    // Show confirmation dialog
    QString confirmMessage = tr("You are about to export and decrypt the show \"%1\"\n\n"
                               "Approximate size: %2\n"
                               "Seasons: %3\n"
                               "Episodes: %4\n\n"
                               "Do you want to proceed?")
                           .arg(m_contextMenuShowName)
                           .arg(formatSize(estimatedSize))
                           .arg(seasonCount)
                           .arg(episodeCount);
    
    int result = QMessageBox::question(m_mainWindow,
                                      tr("Export Confirmation"),
                                      confirmMessage,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);
    
    if (result != QMessageBox::Yes) {
        qDebug() << "Operations_VP_Shows: Export cancelled by user";
        // Clear any pending events after message box closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Clear any pending events after message box closes
    QCoreApplication::processEvents();
    
    // Prepare the export using the worker and progress dialog
    performExportWithWorker(m_contextMenuShowPath, exportPath, m_contextMenuShowName);
}

void Operations_VP_Shows::deleteShow()
{
    qDebug() << "Operations_VP_Shows: Delete show:" << m_contextMenuShowName;
    
    if (m_contextMenuShowName.isEmpty() || m_contextMenuShowPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show selected for deletion";
        return;
    }
    
    // First confirmation dialog
    QString firstMessage = tr("You are about to delete the show \"%1\" from your library.\n\n"
                             "Are you sure that you want to proceed?")
                         .arg(m_contextMenuShowName);
    
    int firstResult = QMessageBox::question(m_mainWindow,
                                           tr("Delete Show"),
                                           firstMessage,
                                           QMessageBox::No | QMessageBox::Yes,
                                           QMessageBox::No);
    
    if (firstResult != QMessageBox::Yes) {
        qDebug() << "Operations_VP_Shows: Deletion cancelled at first confirmation";
        // Clear any pending events after message box closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Clear any pending events after message box closes
    QCoreApplication::processEvents();
    
    // Second confirmation dialog
    QString secondMessage = tr("Are you really sure you want to delete \"%1\"?\n\n"
                              "This action cannot be undone.")
                          .arg(m_contextMenuShowName);
    
    QMessageBox secondConfirm(m_mainWindow);
    secondConfirm.setWindowTitle(tr("Final Confirmation"));
    secondConfirm.setText(secondMessage);
    secondConfirm.setIcon(QMessageBox::Warning);
    
    QPushButton* deleteButton = secondConfirm.addButton(tr("Delete \"%1\"").arg(m_contextMenuShowName), 
                                                        QMessageBox::DestructiveRole);
    QPushButton* noButton = secondConfirm.addButton(tr("No"), QMessageBox::RejectRole);
    
    secondConfirm.setDefaultButton(noButton);
    secondConfirm.exec();
    
    if (secondConfirm.clickedButton() != deleteButton) {
        qDebug() << "Operations_VP_Shows: Deletion cancelled at second confirmation";
        // Clear any pending events after message box closes
        QCoreApplication::processEvents();
        return;
    }
    
    // Clear any pending events after message box closes
    QCoreApplication::processEvents();
    
    qDebug() << "Operations_VP_Shows: User confirmed deletion, proceeding";
    
    // Delete all files in the show folder
    QDir showDir(m_contextMenuShowPath);
    QStringList allFiles = showDir.entryList(QDir::Files);
    
    bool allDeleted = true;
    for (const QString& file : allFiles) {
        QString filePath = showDir.absoluteFilePath(file);
        
        // Use regular delete for encrypted files (no need for secure deletion)
        if (!QFile::remove(filePath)) {
            qDebug() << "Operations_VP_Shows: Failed to delete file:" << file;
            allDeleted = false;
        }
    }
    
    // Remove the directory itself
    if (allDeleted) {
        if (!showDir.removeRecursively()) {
            qDebug() << "Operations_VP_Shows: Failed to remove show directory";
            QMessageBox::warning(m_mainWindow,
                               tr("Partial Deletion"),
                               tr("The show files were deleted but the folder could not be removed."));
            // Clear any pending events after message box closes
            QCoreApplication::processEvents();
        } else {
            qDebug() << "Operations_VP_Shows: Show folder deleted successfully";
        }
    } else {
        QMessageBox::warning(m_mainWindow,
                           tr("Deletion Error"),
                           tr("Some files could not be deleted. The show may be partially removed."));
        // Clear any pending events after message box closes
        QCoreApplication::processEvents();
    }
    
    // Refresh the shows list
    refreshTVShowsList();
    
    // If we're currently displaying this show, go back to the list
    if (m_currentShowFolder == m_contextMenuShowPath) {
        if (m_mainWindow->ui->stackedWidget_VP_Shows) {
            m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(0); // Go back to list page
        }
    }
    
    // Success dialog removed - absence of error dialog indicates success
    // QMessageBox::information(m_mainWindow,
    //                        tr("Show Deleted"),
    //                        tr("The show \"%1\" has been deleted from your library.").arg(m_contextMenuShowName));
}

qint64 Operations_VP_Shows::calculateShowSize(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Calculating show size for:" << showFolderPath;
    
    QDir showDir(showFolderPath);
    QStringList allFiles = showDir.entryList(QDir::Files);
    
    qint64 totalSize = 0;
    for (const QString& file : allFiles) {
        QString filePath = showDir.absoluteFilePath(file);
        QFileInfo fileInfo(filePath);
        totalSize += fileInfo.size();
    }
    
    qDebug() << "Operations_VP_Shows: Total show size:" << totalSize << "bytes";
    return totalSize;
}

qint64 Operations_VP_Shows::estimateDecryptedSize(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Estimating decrypted size for:" << showFolderPath;
    
    // Get the total encrypted size
    qint64 encryptedSize = calculateShowSize(showFolderPath);
    
    // Decrypted files are typically slightly smaller than encrypted ones
    // due to metadata overhead, but we'll estimate conservatively
    // Assume decrypted size is about 95% of encrypted size
    qint64 estimatedSize = static_cast<qint64>(encryptedSize * 0.95);
    
    qDebug() << "Operations_VP_Shows: Estimated decrypted size:" << estimatedSize << "bytes";
    return estimatedSize;
}

bool Operations_VP_Shows::exportShowEpisodes(const QString& showFolderPath, const QString& exportPath,
                                            const QString& showName)
{
    qDebug() << "Operations_VP_Shows: Exporting show from:" << showFolderPath << "to:" << exportPath;

    // Create show folder in export path
    QDir exportDir(exportPath);
    QString showFolderName = showName;

    // Sanitize the show name for use as a folder name
    showFolderName.replace(QRegularExpression("[<>:\"|?*]"), "_");

    if (!exportDir.mkdir(showFolderName)) {
        // Folder might already exist, try to use it
        qDebug() << "Operations_VP_Shows: Show folder already exists or couldn't be created";
    }

    QString showExportPath = exportDir.absoluteFilePath(showFolderName);
    QDir showExportDir(showExportPath);

    // Get all video files (now using .mmvid extension)
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files);

    // Create metadata manager
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);

    // Create progress dialog
    QProgressDialog progress(tr("Exporting %1...").arg(showName), tr("Cancel"), 0, videoFiles.size(), m_mainWindow);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    QCoreApplication::processEvents();

    int processed = 0;
    bool allSuccess = true;

    // Process each video file
    for (const QString& videoFile : videoFiles) {
        if (progress.wasCanceled()) {
            qDebug() << "Operations_VP_Shows: Export cancelled by user";
            return false;
        }
        
        QString sourceFilePath = showDir.absoluteFilePath(videoFile);
        
        // Read metadata to determine output path and filename
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(sourceFilePath, metadata)) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from:" << videoFile;
            allSuccess = false;
            processed++;
            progress.setValue(processed);
            continue;
        }
        
        // Parse season and episode numbers
        int seasonNum = metadata.season.toInt();
        int episodeNum = metadata.episode.toInt();
        if (seasonNum <= 0 || episodeNum <= 0) {
            // Try to parse from filename as fallback if either is missing
            VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum);
            if (seasonNum <= 0) seasonNum = 1;
            // episodeNum remains as parsed or 0 if parsing failed
        }
        
        // Create language/translation folder first
        QString languageFolderName = QString("%1 %2").arg(metadata.language).arg(metadata.translation);
        // Sanitize the language folder name
        languageFolderName.replace(QRegularExpression("[<>:\"|?*]"), "_");
        
        if (!showExportDir.exists(languageFolderName)) {
            if (!showExportDir.mkdir(languageFolderName)) {
                qDebug() << "Operations_VP_Shows: Failed to create language folder:" << languageFolderName;
                allSuccess = false;
                processed++;
                progress.setValue(processed);
                continue;
            }
        }
        
        QString languagePath = showExportDir.absoluteFilePath(languageFolderName);
        QDir languageDir(languagePath);
        
        // Check if using absolute numbering
        QString episodeFolderPath;
        if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
            // For absolute numbering, create "Episodes" folder instead of season folder
            QString episodesFolderName = "Episodes";
            if (!languageDir.exists(episodesFolderName)) {
                if (!languageDir.mkdir(episodesFolderName)) {
                    qDebug() << "Operations_VP_Shows: Failed to create episodes folder:" << episodesFolderName;
                    allSuccess = false;
                    processed++;
                    progress.setValue(processed);
                    continue;
                }
            }
            episodeFolderPath = languageDir.absoluteFilePath(episodesFolderName);
        } else {
            // Traditional season folder structure
            QString seasonFolderName = QString("Season %1").arg(seasonNum, 2, 10, QChar('0'));
            if (!languageDir.exists(seasonFolderName)) {
                if (!languageDir.mkdir(seasonFolderName)) {
                    qDebug() << "Operations_VP_Shows: Failed to create season folder:" << seasonFolderName;
                    allSuccess = false;
                    processed++;
                    progress.setValue(processed);
                    continue;
                }
            }
            episodeFolderPath = languageDir.absoluteFilePath(seasonFolderName);
        }
        
        // Generate output filename
        // episodeNum was already extracted above
        QString outputFileName;
        
        if (episodeNum > 0) {
            if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
                // For absolute numbering, use E## format
                outputFileName = QString("%1_E%2")
                               .arg(showName)
                               .arg(episodeNum, 3, 10, QChar('0')); // Use 3 digits for absolute numbering
            } else {
                // Traditional S##E## format
                outputFileName = QString("%1_S%2E%3")
                               .arg(showName)
                               .arg(seasonNum, 2, 10, QChar('0'))
                               .arg(episodeNum, 2, 10, QChar('0'));
            }
            
            if (!metadata.EPName.isEmpty()) {
                outputFileName += "_" + metadata.EPName;
            }
        } else {
            // Use original filename without extension
            QFileInfo fileInfo(metadata.filename);
            outputFileName = fileInfo.completeBaseName();
        }
        
        // Sanitize filename
        outputFileName.replace(QRegularExpression("[<>:\"|?*]"), "_");
        
        // Add extension from original filename in metadata
        QString originalExtension;
        if (!metadata.filename.isEmpty()) {
            QFileInfo originalFileInfo(metadata.filename);
            originalExtension = originalFileInfo.suffix();
        }
        if (!originalExtension.isEmpty()) {
            outputFileName += "." + originalExtension;
        } else {
            // Fallback to .mp4 if no extension found
            outputFileName += ".mp4";
        }
        
        QString outputFilePath = QDir(episodeFolderPath).absoluteFilePath(outputFileName);
        
        // Update label with file and size info
        QFileInfo sourceFileInfo(sourceFilePath);
        qint64 fileSizeMB = sourceFileInfo.size() / (1024 * 1024);
        progress.setLabelText(tr("Exporting: %1 (%2 MB)").arg(outputFileName).arg(fileSizeMB));
        QCoreApplication::processEvents();
        
        // Decrypt the file without metadata header
        bool decryptSuccess = decryptVideoWithMetadata(sourceFilePath, outputFilePath);
        
        // Process events periodically to keep UI responsive
        QCoreApplication::processEvents();
        
        if (!decryptSuccess) {
            qDebug() << "Operations_VP_Shows: Failed to decrypt and export:" << videoFile;
            allSuccess = false;
            // Try to clean up the failed file
            QFile::remove(outputFilePath);
        }
        
        processed++;
        progress.setValue(processed);
        QCoreApplication::processEvents();
    }
    
    return allSuccess;
}

void Operations_VP_Shows::performExportWithWorker(const QString& showFolderPath, const QString& exportPath, 
                                                  const QString& showName)
{
    qDebug() << "Operations_VP_Shows: Preparing export with worker for:" << showName;
    
    // Build the list of files to export
    QList<VP_ShowsExportWorker::ExportFileInfo> exportFiles;
    
    // Create show folder in export path
    QDir exportDir(exportPath);
    QString showFolderName = showName;
    
    // Sanitize the show name for use as a folder name
    showFolderName.replace(QRegularExpression("[<>:\"|?*]"), "_");
    
    if (!exportDir.mkdir(showFolderName)) {
        // Folder might already exist, try to use it
        qDebug() << "Operations_VP_Shows: Show folder already exists or couldn't be created";
    }
    
    QString showExportPath = exportDir.absoluteFilePath(showFolderName);
    QDir showExportDir(showExportPath);
    
    // Get all video files (now using .mmvid extension)
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files);
    
    // Create metadata manager to read episode info
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Process each video file to build export info
    for (const QString& videoFile : videoFiles) {
        QString sourceFilePath = showDir.absoluteFilePath(videoFile);
        
        // Read metadata to determine output path and filename
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(sourceFilePath, metadata)) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from:" << videoFile;
            continue;
        }
        
        // Parse season and episode numbers
        int seasonNum = metadata.season.toInt();
        int episodeNum = metadata.episode.toInt();
        if (seasonNum <= 0 || episodeNum <= 0) {
            // Try to parse from filename as fallback
            VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum);
            if (seasonNum <= 0) seasonNum = 1;
        }
        
        // Create language/translation folder path
        QString languageFolderName = QString("%1 %2").arg(metadata.language).arg(metadata.translation);
        // Sanitize the language folder name
        languageFolderName.replace(QRegularExpression("[<>:\"|?*]"), "_");
        
        if (!showExportDir.exists(languageFolderName)) {
            if (!showExportDir.mkdir(languageFolderName)) {
                qDebug() << "Operations_VP_Shows: Failed to create language folder:" << languageFolderName;
                continue;
            }
        }
        
        QString languagePath = showExportDir.absoluteFilePath(languageFolderName);
        QDir languageDir(languagePath);
        
        // Check if using absolute numbering
        QString episodeFolderPath;
        if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
            // For absolute numbering, create "Episodes" folder instead of season folder
            QString episodesFolderName = "Episodes";
            if (!languageDir.exists(episodesFolderName)) {
                if (!languageDir.mkdir(episodesFolderName)) {
                    qDebug() << "Operations_VP_Shows: Failed to create episodes folder:" << episodesFolderName;
                    continue;
                }
            }
            episodeFolderPath = languageDir.absoluteFilePath(episodesFolderName);
        } else {
            // Traditional season folder structure
            QString seasonFolderName = QString("Season %1").arg(seasonNum, 2, 10, QChar('0'));
            if (!languageDir.exists(seasonFolderName)) {
                if (!languageDir.mkdir(seasonFolderName)) {
                    qDebug() << "Operations_VP_Shows: Failed to create season folder:" << seasonFolderName;
                    continue;
                }
            }
            episodeFolderPath = languageDir.absoluteFilePath(seasonFolderName);
        }
        
        // Generate output filename
        QString outputFileName;
        
        if (episodeNum > 0) {
            if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
                // For absolute numbering, use E## format
                outputFileName = QString("%1_E%2")
                               .arg(showName)
                               .arg(episodeNum, 3, 10, QChar('0')); // Use 3 digits for absolute numbering
            } else {
                // Traditional S##E## format
                outputFileName = QString("%1_S%2E%3")
                               .arg(showName)
                               .arg(seasonNum, 2, 10, QChar('0'))
                               .arg(episodeNum, 2, 10, QChar('0'));
            }
            
            if (!metadata.EPName.isEmpty()) {
                outputFileName += "_" + metadata.EPName;
            }
        } else {
            // Use original filename without extension
            QFileInfo fileInfo(metadata.filename);
            outputFileName = fileInfo.completeBaseName();
        }
        
        // Sanitize filename
        outputFileName.replace(QRegularExpression("[<>:\"|?*]"), "_");
        
        // Add extension from original filename in metadata
        QString originalExtension;
        if (!metadata.filename.isEmpty()) {
            QFileInfo originalFileInfo(metadata.filename);
            originalExtension = originalFileInfo.suffix();
        }
        if (!originalExtension.isEmpty()) {
            outputFileName += "." + originalExtension;
        } else {
            // Fallback to .mp4 if no extension found
            outputFileName += ".mp4";
        }
        
        QString outputFilePath = QDir(episodeFolderPath).absoluteFilePath(outputFileName);
        
        // Create export info
        VP_ShowsExportWorker::ExportFileInfo fileInfo;
        fileInfo.sourceFile = sourceFilePath;
        fileInfo.targetFile = outputFilePath;
        fileInfo.displayName = outputFileName;
        fileInfo.fileSize = QFileInfo(sourceFilePath).size();
        
        exportFiles.append(fileInfo);
    }
    
    if (exportFiles.isEmpty()) {
        QMessageBox::warning(m_mainWindow,
                           tr("Export Error"),
                           tr("No valid files found to export."));
        return;
    }
    
    // Create and show export progress dialog
    VP_ShowsExportProgressDialog* exportDialog = new VP_ShowsExportProgressDialog(m_mainWindow);
    
    // Connect completion signal
    connect(exportDialog, &VP_ShowsExportProgressDialog::exportComplete,
            this, [this, exportDialog, showName](bool success, const QString& message,
                                                const QStringList& successfulFiles,
                                                const QStringList& failedFiles) {
        qDebug() << "Operations_VP_Shows: Export complete. Success:" << success;
        
        if (success) {
            // Success dialog removed - absence of error dialog indicates success
            // QMessageBox::information(m_mainWindow,
            //                        tr("Export Complete"),
            //                        tr("The show \"%1\" has been successfully exported.").arg(showName));
        } else {
            QString detailedMessage = message;
            if (!failedFiles.isEmpty()) {
                detailedMessage += tr("\n\nFailed files: %1").arg(failedFiles.size());
            }
            QMessageBox::warning(m_mainWindow,
                               tr("Export Failed"),
                               detailedMessage);
        }
        
        // Clean up the dialog
        exportDialog->deleteLater();
    });
    
    // Start the export
    exportDialog->startExport(exportFiles, m_mainWindow->user_Key, m_mainWindow->user_Username, showName);
}

void Operations_VP_Shows::setupEpisodeContextMenu()
{
    qDebug() << "Operations_VP_Shows: Setting up context menu for episode tree widget";
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Cannot setup episode context menu - tree widget not available";
        return;
    }
    
    // Enable multi-selection for the tree widget
    m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    // Enable context menu for the tree widget
    m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Connect the context menu signal
    connect(m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList, &QTreeWidget::customContextMenuRequested,
            this, &Operations_VP_Shows::showEpisodeContextMenu);
    
    // Connect selection changed signal to enforce broken file restrictions
    connect(m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList, &QTreeWidget::itemSelectionChanged,
            this, &Operations_VP_Shows::onTreeSelectionChanged);
    
    qDebug() << "Operations_VP_Shows: Episode context menu setup complete with multi-selection enabled";
}

void Operations_VP_Shows::showEpisodeContextMenu(const QPoint& pos)
{
    qDebug() << "Operations_VP_Shows: Episode context menu requested";
    
    // Check for valid m_mainWindow pointer
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow pointer is null";
        return;
    }
    
    if (!m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        return;
    }
    
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    
    // Get all selected items
    QList<QTreeWidgetItem*> selectedItems = treeWidget->selectedItems();
    
    if (selectedItems.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No items selected";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Number of selected items:" << selectedItems.size();
    
    // Store the clicked item (for single item operations)
    QTreeWidgetItem* clickedItem = treeWidget->itemAt(pos);
    if (!clickedItem) {
        qDebug() << "Operations_VP_Shows: No item at click position";
        return;
    }
    
    // Make sure the clicked item is among the selected items
    if (!selectedItems.contains(clickedItem)) {
        // If the clicked item is not in selection, make it the only selection
        treeWidget->clearSelection();
        clickedItem->setSelected(true);
        selectedItems.clear();
        selectedItems.append(clickedItem);
    }
    
    // Store the primary item for context menu actions
    m_contextMenuTreeItem = clickedItem;
    
    // Check if this is a broken item or category
    bool isBroken = isItemBroken(clickedItem);
    bool isBrokenCat = isBrokenCategory(clickedItem);
    
    // Special handling for broken category - only show "Delete broken files" option
    if (isBrokenCat) {
        qDebug() << "Operations_VP_Shows: Showing context menu for broken category";
        
        // Create simplified context menu for broken category
        QMenu* contextMenu = new QMenu(m_mainWindow);
        
        // Only add "Delete broken files" action
        QAction* deleteAllAction = contextMenu->addAction(tr("Delete broken files"));
        deleteAllAction->setIcon(QIcon(":/icons/delete.png"));
        connect(deleteAllAction, &QAction::triggered, this, &Operations_VP_Shows::deleteBrokenVideosFromCategory);
        
        // Show the menu
        contextMenu->exec(treeWidget->mapToGlobal(pos));
        contextMenu->deleteLater();
        return; // Exit early - don't show regular menu
    }
    
    // Special handling for broken items - only show "Repair file" and "Delete file" options
    if (isBroken) {
        qDebug() << "Operations_VP_Shows: Showing context menu for broken file";
        
        // Store the clicked item for context menu actions (important for delete function)
        m_contextMenuTreeItem = clickedItem;
        
        // Store the episode path for context menu actions
        m_contextMenuEpisodePaths.clear();
        m_contextMenuEpisodePath.clear();
        QString videoPath = clickedItem->data(0, Qt::UserRole).toString();
        if (!videoPath.isEmpty()) {
            m_contextMenuEpisodePaths.append(videoPath);
            m_contextMenuEpisodePath = videoPath;
        }
        
        // Create simplified context menu for broken items
        QMenu* contextMenu = new QMenu(m_mainWindow);
        
        // Add "Repair file" action
        QAction* repairAction = contextMenu->addAction(tr("Repair file"));
        repairAction->setIcon(QIcon(":/icons/repair.png"));
        connect(repairAction, &QAction::triggered, this, &Operations_VP_Shows::repairBrokenVideo);
        
        // Add "Delete file" action
        QAction* deleteAction = contextMenu->addAction(tr("Delete file"));
        deleteAction->setIcon(QIcon(":/icons/delete.png"));
        connect(deleteAction, &QAction::triggered, this, &Operations_VP_Shows::deleteEpisodeFromContextMenu);
        
        // Show the menu
        contextMenu->exec(treeWidget->mapToGlobal(pos));
        contextMenu->deleteLater();
        return; // Exit early - don't show regular menu
    }
    
    // Determine selection type and collect episode paths
    bool isMultiSelection = selectedItems.size() > 1;
    bool hasCategories = false;
    bool hasEpisodes = false;
    QString description;
    
    // Clear previous episode paths
    m_contextMenuEpisodePaths.clear();
    m_contextMenuEpisodePath.clear();
    
    // Use a QSet to collect unique episode paths to avoid duplicates
    QSet<QString> uniqueEpisodePaths;
    
    // Process all selected items
    for (QTreeWidgetItem* item : selectedItems) {
        if (item->childCount() > 0) {
            // This is a category (language, season, etc.)
            hasCategories = true;
            
            // Collect episodes from this category into a temporary list
            QStringList categoryEpisodes;
            collectEpisodesFromTreeItem(item, categoryEpisodes);
            
            // Add to the unique set
            for (const QString& episodePath : categoryEpisodes) {
                uniqueEpisodePaths.insert(episodePath);
            }
            
            if (!isMultiSelection) {
                // For single category selection, get detailed description
                QTreeWidgetItem* parent = item->parent();
                if (parent == nullptr) {
                    // Top-level item (Language/Translation)
                    description = item->text(0);
                    qDebug() << "Operations_VP_Shows: Context menu on language/translation:" << description;
                } else {
                    // Second-level item (Season)
                    description = item->text(0);
                    QString language = parent->text(0);
                    description = QString("%1 - %2").arg(language).arg(description);
                    qDebug() << "Operations_VP_Shows: Context menu on season:" << description;
                }
            }
        } else {
            // This is an episode item
            hasEpisodes = true;
            QString videoPath = item->data(0, Qt::UserRole).toString();
            if (!videoPath.isEmpty()) {
                uniqueEpisodePaths.insert(videoPath);
                if (!isMultiSelection) {
                    m_contextMenuEpisodePath = videoPath; // Store single episode path
                    description = item->text(0);
                    qDebug() << "Operations_VP_Shows: Context menu on episode:" << description;
                }
            }
        }
    }
    
    // Convert the unique set back to the list
    m_contextMenuEpisodePaths = uniqueEpisodePaths.values();
    
    qDebug() << "Operations_VP_Shows: Collected" << m_contextMenuEpisodePaths.size() << "unique episodes from" << selectedItems.size() << "selected items";
    
    // Determine the item type for menu construction
    QString itemType;
    if (isMultiSelection) {
        // Multi-selection
        if (hasCategories && hasEpisodes) {
            itemType = "mixed";
            description = tr("%1 items selected").arg(selectedItems.size());
        } else if (hasCategories) {
            itemType = "categories";
            description = tr("%1 categories selected").arg(selectedItems.size());
        } else {
            itemType = "episodes";
            description = tr("%1 episodes selected").arg(selectedItems.size());
        }
        qDebug() << "Operations_VP_Shows: Multi-selection context menu:" << description;
    } else {
        // Single selection
        if (hasCategories) {
            itemType = clickedItem->parent() == nullptr ? "language" : "season";
        } else {
            itemType = "episode";
        }
    }
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episode paths found for context menu";
        return;
    }
    
    // Create the context menu
    QMenu* contextMenu = new QMenu(m_mainWindow);
    
    // Mark as Watched/Unwatched action
    QAction* markWatchedAction = nullptr;
    if (m_watchHistory) {
        // For multi-selection or categories, use category watch state logic
        WatchState watchState;
        if (isMultiSelection || hasCategories) {
            // For multiple items, check if all are watched (completed)
            watchState = WatchState::Watched;
            for (const QString& episodePath : m_contextMenuEpisodePaths) {
                QDir showDir(m_currentShowFolder);
                QString relativePath = showDir.relativeFilePath(episodePath);
                // Use isEpisodeCompleted to check actual watched status
                if (!m_watchHistory->isEpisodeCompleted(relativePath)) {
                    watchState = WatchState::NotWatched;
                    break;
                }
            }
        } else {
            // Single episode selection
            watchState = getItemWatchState(clickedItem);
        }

        // Choose the appropriate icon and text based on state
        QString actionText;
        switch (watchState) {
        case WatchState::NotWatched:
            actionText = tr("Mark as Watched ☐");
            break;
        case WatchState::Watched:
            actionText = tr("Mark as Watched ☑");
            break;
        case WatchState::PartiallyWatched:
            actionText = tr("Mark as Watched ◉");
            break;
        }
        
        // Add appropriate suffix for multiple items or categories
        if (isMultiSelection || hasCategories) {
            int episodeCount = m_contextMenuEpisodePaths.size();
            actionText += tr(" (%1 episode%2)").arg(episodeCount).arg(episodeCount > 1 ? "s" : "");
        }

        markWatchedAction = contextMenu->addAction(actionText);
        connect(markWatchedAction, &QAction::triggered, this, &Operations_VP_Shows::toggleWatchedStateFromContextMenu);
    }
    
    // Add Mark as Favourite action
    QAction* markFavouriteAction = nullptr;
    if (m_showFavourites) {
        // Check favourite status for the selected episodes
        bool allFavourites = true;
        bool someFavourites = false;
        
        for (const QString& episodePath : m_contextMenuEpisodePaths) {
            QDir showDir(m_currentShowFolder);
            QString relativePath = showDir.relativeFilePath(episodePath);
            
            if (m_showFavourites->isEpisodeFavourite(relativePath)) {
                someFavourites = true;
            } else {
                allFavourites = false;
            }
        }
        
        // Determine action text based on current state
        QString favouriteActionText;
        if (allFavourites) {
            favouriteActionText = tr("Mark as Favourite ★");  // Filled star for favourited
        } else if (someFavourites) {
            favouriteActionText = tr("Mark as Favourite ☆");  // Half-filled or outline star for partial
        } else {
            favouriteActionText = tr("Mark as Favourite ☆");  // Outline star for not favourited
        }
        
        // Add appropriate suffix for multiple items or categories
        if (isMultiSelection || hasCategories) {
            int episodeCount = m_contextMenuEpisodePaths.size();
            favouriteActionText += tr(" (%1 episode%2)").arg(episodeCount).arg(episodeCount > 1 ? "s" : "");
        }
        
        markFavouriteAction = contextMenu->addAction(favouriteActionText);
        connect(markFavouriteAction, &QAction::triggered, this, &Operations_VP_Shows::toggleFavouriteStateFromContextMenu);
    }
    
    // Add a separator after the mark watched and favourite actions
    if (markWatchedAction || markFavouriteAction) {
        contextMenu->addSeparator();
    }

    // Play action (only for single episode selection)
    if (itemType == "episode" && !isMultiSelection) {
        QAction* playAction = contextMenu->addAction(tr("Play"));
        connect(playAction, &QAction::triggered, this, &Operations_VP_Shows::playEpisodeFromContextMenu);
    } else if (hasCategories && !isMultiSelection) {
        // Play first episode for single category selection
        QAction* playAction = contextMenu->addAction(tr("Play First Episode"));
        connect(playAction, &QAction::triggered, this, &Operations_VP_Shows::playEpisodeFromContextMenu);
    }
    // No play action for multi-selection
    
    // Decrypt and Export action
    QAction* exportAction;
    if (itemType == "episode" && !isMultiSelection) {
        exportAction = contextMenu->addAction(tr("Decrypt and Export"));
    } else {
        int episodeCount = m_contextMenuEpisodePaths.size();
        exportAction = contextMenu->addAction(tr("Decrypt and Export (%1 episode%2)").arg(episodeCount).arg(episodeCount > 1 ? "s" : ""));
    }
    connect(exportAction, &QAction::triggered, this, &Operations_VP_Shows::decryptAndExportEpisodeFromContextMenu);
    
    // Edit metadata action - handle both single and multiple episodes
    // Check if we have any episodes collected (from direct selection or categories)
    if (hasEpisodes || !m_contextMenuEpisodePaths.isEmpty()) {
        contextMenu->addSeparator();
        QAction* editMetadataAction = nullptr;
        
        if (m_contextMenuEpisodePaths.size() == 1) {
            // Single episode metadata editing (whether selected directly or from a category with one episode)
            editMetadataAction = contextMenu->addAction(tr("Edit metadata"));
            connect(editMetadataAction, &QAction::triggered, this, &Operations_VP_Shows::editEpisodeMetadata);
        } else if (m_contextMenuEpisodePaths.size() > 1) {
            // Multiple episodes metadata editing (from multiple episodes, categories, or mixed selection)
            int episodeCount = m_contextMenuEpisodePaths.size();
            editMetadataAction = contextMenu->addAction(tr("Edit metadata for %1 files").arg(episodeCount));
            connect(editMetadataAction, &QAction::triggered, this, &Operations_VP_Shows::editMultipleEpisodesMetadata);
        }
        
        // TMDB re-acquisition action - handle both single and multiple episodes
        QAction* tmdbAction = nullptr;
        if (m_contextMenuEpisodePaths.size() == 1) {
            // Single episode TMDB re-acquisition
            tmdbAction = contextMenu->addAction(tr("Re-acquire TMDB metadata"));
        } else if (m_contextMenuEpisodePaths.size() > 1) {
            // Multiple episodes TMDB re-acquisition
            int episodeCount = m_contextMenuEpisodePaths.size();
            tmdbAction = contextMenu->addAction(tr("Re-acquire TMDB metadata for %1 files").arg(episodeCount));
        }
        
        if (tmdbAction) {
            connect(tmdbAction, &QAction::triggered, this, &Operations_VP_Shows::reacquireTMDBFromContextMenu);
        }
        
        if (editMetadataAction || tmdbAction) {
            contextMenu->addSeparator();
        }
    }
    
    // Delete action
    QAction* deleteAction;
    if (itemType == "episode" && !isMultiSelection) {
        deleteAction = contextMenu->addAction(tr("Delete"));
    } else {
        int episodeCount = m_contextMenuEpisodePaths.size();
        deleteAction = contextMenu->addAction(tr("Delete (%1 episode%2)").arg(episodeCount).arg(episodeCount > 1 ? "s" : ""));
    }
    connect(deleteAction, &QAction::triggered, this, &Operations_VP_Shows::deleteEpisodeFromContextMenu);
    
    // Add separator before Show in File Explorer
    contextMenu->addSeparator();
    
    // Show in File Explorer action
    QAction* showInExplorerAction = nullptr;
    if (m_contextMenuEpisodePaths.size() == 1) {
        showInExplorerAction = contextMenu->addAction(tr("Show in File Explorer"));
    } else if (m_contextMenuEpisodePaths.size() > 1) {
        showInExplorerAction = contextMenu->addAction(tr("Show in File Explorer (%1 files)").arg(m_contextMenuEpisodePaths.size()));
    }
    if (showInExplorerAction) {
        connect(showInExplorerAction, &QAction::triggered, this, &Operations_VP_Shows::showEpisodesInFileExplorer);
    }
    
#ifdef QT_DEBUG
    // Debug-only option to corrupt metadata for testing
    if (!isMultiSelection && hasEpisodes) {
        contextMenu->addSeparator();
        QAction* corruptAction = contextMenu->addAction(tr("[DEBUG] Corrupt Metadata Header"));
        corruptAction->setIcon(QIcon(":/icons/warning.png"));
        connect(corruptAction, &QAction::triggered, this, &Operations_VP_Shows::corruptVideoMetadata);
    }
#endif
    
    // Show the menu at the cursor position
    contextMenu->exec(treeWidget->mapToGlobal(pos));
    
    // Clean up
    contextMenu->deleteLater();
}

void Operations_VP_Shows::collectEpisodesFromTreeItem(QTreeWidgetItem* item, QStringList& episodePaths)
{
    if (!item) return;
    
    // If this item has no children, it's an episode
    if (item->childCount() == 0) {
        QString videoPath = item->data(0, Qt::UserRole).toString();
        if (!videoPath.isEmpty()) {
            episodePaths.append(videoPath);
        }
    } else {
        // This item has children, recursively collect from all children
        for (int i = 0; i < item->childCount(); ++i) {
            collectEpisodesFromTreeItem(item->child(i), episodePaths);
        }
    }
}

// Helper function to check if item is a broken video
bool Operations_VP_Shows::isItemBroken(QTreeWidgetItem* item) const
{
    if (!item) return false;
    
    // Check if parent is the broken category
    QTreeWidgetItem* parent = item->parent();
    if (parent && parent->text(0).startsWith("Broken")) {
        return true;
    }
    
    return false;
}

// Helper function to check if item is the broken category itself
bool Operations_VP_Shows::isBrokenCategory(QTreeWidgetItem* item) const
{
    if (!item) return false;
    
    // Check if this is a top-level item starting with "Broken"
    return item->parent() == nullptr && item->text(0).startsWith("Broken");
}

// Check if any item in selection is broken
bool Operations_VP_Shows::hasAnyBrokenItemInSelection(const QList<QTreeWidgetItem*>& items) const
{
    for (QTreeWidgetItem* item : items) {
        if (isItemBroken(item) || isBrokenCategory(item)) {
            return true;
        }
    }
    return false;
}

// Check if any item in selection is working (not broken)
bool Operations_VP_Shows::hasAnyWorkingItemInSelection(const QList<QTreeWidgetItem*>& items) const
{
    for (QTreeWidgetItem* item : items) {
        if (!isItemBroken(item) && !isBrokenCategory(item)) {
            return true;
        }
    }
    return false;
}

// Enforce selection restrictions for broken files
void Operations_VP_Shows::enforceSelectionRestrictions()
{
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        return;
    }
    
    if (m_blockSelectionChange) {
        return;  // Prevent recursion
    }
    
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    QList<QTreeWidgetItem*> selectedItems = treeWidget->selectedItems();
    
    if (selectedItems.size() <= 1) {
        return;  // No restrictions for single selection
    }
    
    // Check if we have mixed selection (broken and working)
    bool hasBroken = hasAnyBrokenItemInSelection(selectedItems);
    bool hasWorking = hasAnyWorkingItemInSelection(selectedItems);
    
    if (hasBroken && hasWorking) {
        // Mixed selection not allowed
        qDebug() << "Operations_VP_Shows: Mixed selection of broken and working files not allowed";
        
        m_blockSelectionChange = true;
        
        // Keep only the last selected item
        QTreeWidgetItem* currentItem = treeWidget->currentItem();
        treeWidget->clearSelection();
        if (currentItem) {
            currentItem->setSelected(true);
        }
        
        m_blockSelectionChange = false;
        return;
    }
    
    // Check if multiple broken files are selected
    if (hasBroken) {
        int brokenCount = 0;
        for (QTreeWidgetItem* item : selectedItems) {
            if (isItemBroken(item)) {
                brokenCount++;
            }
        }
        
        if (brokenCount > 1) {
            // Multiple broken files not allowed
            qDebug() << "Operations_VP_Shows: Multiple broken files selection not allowed";
            
            m_blockSelectionChange = true;
            
            // Keep only the last selected item
            QTreeWidgetItem* currentItem = treeWidget->currentItem();
            treeWidget->clearSelection();
            if (currentItem) {
                currentItem->setSelected(true);
            }
            
            m_blockSelectionChange = false;
        }
    }
}

// Slot for tree widget selection changes
void Operations_VP_Shows::onTreeSelectionChanged()
{
    enforceSelectionRestrictions();
}

// Delete all broken videos from the broken category
void Operations_VP_Shows::deleteBrokenVideosFromCategory()
{
    qDebug() << "Operations_VP_Shows: Delete all broken videos from category";
    
    // Check MainWindow validity
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow is null";
        return;
    }
    
    // Check tree item validity
    if (!m_contextMenuTreeItem || !isBrokenCategory(m_contextMenuTreeItem)) {
        qDebug() << "Operations_VP_Shows: Not a broken category item or item no longer valid";
        return;
    }
    
    // Collect all broken file paths
    QStringList brokenFilePaths;
    for (int i = 0; i < m_contextMenuTreeItem->childCount(); ++i) {
        QTreeWidgetItem* child = m_contextMenuTreeItem->child(i);
        QString filePath = child->data(0, Qt::UserRole).toString();
        if (!filePath.isEmpty()) {
            brokenFilePaths.append(filePath);
        }
    }
    
    if (brokenFilePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No broken files to delete";
        return;
    }
    
    // Confirm deletion
    int fileCount = brokenFilePaths.size();
    QString message = tr("You are about to delete %1 broken video file%2.\n\n"
                         "These files have corrupted metadata headers and cannot be played.\n\n"
                         "Are you sure you want to delete them?")
                      .arg(fileCount)
                      .arg(fileCount > 1 ? "s" : "");
    
    int result = QMessageBox::question(m_mainWindow,
                                      tr("Delete Broken Videos"),
                                      message,
                                      QMessageBox::No | QMessageBox::Yes,
                                      QMessageBox::No);
    
    if (result != QMessageBox::Yes) {
        qDebug() << "Operations_VP_Shows: User cancelled deletion of broken videos";
        return;
    }
    
    // Delete the files
    int successCount = 0;
    int failCount = 0;
    
    for (const QString& filePath : brokenFilePaths) {
        if (QFile::remove(filePath)) {
            successCount++;
            qDebug() << "Operations_VP_Shows: Deleted broken file:" << filePath;
        } else {
            failCount++;
            qDebug() << "Operations_VP_Shows: Failed to delete broken file:" << filePath;
        }
    }
    
    if (failCount > 0) {
        QMessageBox::warning(m_mainWindow,
                           tr("Partial Success"),
                           tr("Deleted %1 broken file%2.\n"
                              "Failed to delete %3 file%4.")
                           .arg(successCount).arg(successCount != 1 ? "s" : "")
                           .arg(failCount).arg(failCount != 1 ? "s" : ""));
    }
    
    // Check if any video files remain in the show folder
    if (successCount > 0 && !m_currentShowFolder.isEmpty()) {
        QDir showDir(m_currentShowFolder);
        QStringList videoExtensions;
        videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
        showDir.setNameFilters(videoExtensions);
        QStringList remainingVideos = showDir.entryList(QDir::Files);
        
        if (remainingVideos.isEmpty()) {
            // No episodes left, delete the entire show
            qDebug() << "Operations_VP_Shows: No episodes left after deleting broken files, deleting entire show";
            
            // Get the show name
            QString showName;
            if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->label_VP_Shows_Display_Name) {
                showName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
            }
            
            // Delete the show folder
            if (!showDir.removeRecursively()) {
                qDebug() << "Operations_VP_Shows: Failed to remove empty show directory";
            }
            
            // Go back to the shows list
            if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->stackedWidget_VP_Shows) {
                m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(0);
            }
            
            // Refresh the shows list
            refreshTVShowsList();
        } else {
            // Some episodes remain, just refresh the episode list
            loadShowEpisodes(m_currentShowFolder);

        }
    } else if (successCount > 0) {
        // Just refresh if we don't have the current show folder
        loadShowEpisodes(m_currentShowFolder);
    }
}

// Repair broken video by attempting to decrypt the video content and allowing metadata recreation
void Operations_VP_Shows::repairBrokenVideo()
{
    qDebug() << "Operations_VP_Shows: Starting repair broken video process";
    
    // Check if we have a valid broken item selected
    if (!m_contextMenuTreeItem || !isItemBroken(m_contextMenuTreeItem)) {
        qDebug() << "Operations_VP_Shows: No broken item selected for repair";
        return;
    }
    
    // Get the video file path
    QString videoFilePath = m_contextMenuTreeItem->data(0, Qt::UserRole).toString();
    if (videoFilePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No file path for broken item";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Attempting to repair file:" << videoFilePath;
    
    // Validate file path using operations_files
    if (!OperationsFiles::isWithinAllowedDirectory(videoFilePath, "Data")) {
        qDebug() << "Operations_VP_Shows: File path outside allowed directory";
        QMessageBox::critical(m_mainWindow, tr("Error"),
                            tr("The file path is outside the allowed directory."));
        return;
    }
    
    // Check if file exists and has sufficient size
    QFile videoFile(videoFilePath);
    if (!videoFile.exists()) {
        qDebug() << "Operations_VP_Shows: File does not exist";
        QMessageBox::critical(m_mainWindow, tr("Error"),
                            tr("The video file no longer exists."));
        return;
    }
    
    // Check if file has at least the metadata header size + some video content
    qint64 fileSize = videoFile.size();
    if (fileSize <= VP_ShowsMetadata::METADATA_RESERVED_SIZE) {
        qDebug() << "Operations_VP_Shows: File too small, only metadata header or less";
        QMessageBox::critical(m_mainWindow, tr("Error"),
                            tr("The file is too small to contain video data."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: File size:" << fileSize << "bytes";
    
    // Attempt to decrypt the video portion (skipping metadata)
    if (!videoFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open file for reading";
        QMessageBox::critical(m_mainWindow, tr("Error"),
                            tr("Failed to open the video file for reading."));
        return;
    }
    
    // Skip the metadata header
    videoFile.seek(VP_ShowsMetadata::METADATA_RESERVED_SIZE);
    
    // Create a data stream to read the chunked format
    QDataStream stream(&videoFile);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // Read the first chunk to test decryption
    qint32 chunkSize = 0;
    stream >> chunkSize;
    
    if (stream.status() != QDataStream::Ok || chunkSize <= 0 || chunkSize > 10 * 1024 * 1024) {
        qDebug() << "Operations_VP_Shows: Invalid chunk size:" << chunkSize;
        videoFile.close();
        QMessageBox::critical(m_mainWindow, tr("Error"),
                            tr("The file appears to be corrupted (invalid chunk size)."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Reading test chunk of size:" << chunkSize;
    
    // Read the encrypted chunk data
    QByteArray encryptedChunk = videoFile.read(chunkSize);
    videoFile.close();
    
    if (encryptedChunk.size() != chunkSize) {
        qDebug() << "Operations_VP_Shows: Failed to read complete chunk. Expected:" << chunkSize << "Got:" << encryptedChunk.size();
        QMessageBox::critical(m_mainWindow, tr("Error"),
                            tr("Failed to read video data from file."));
        return;
    }
    
    // Attempt to decrypt the test chunk
    QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(
        m_mainWindow->user_Key, encryptedChunk);
    
    if (decryptedChunk.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Failed to decrypt video content";
        QMessageBox::critical(m_mainWindow, tr("Repair Failed"),
                            tr("Unable to repair the file. The video data could not be decrypted.\n\n"
                               "This may indicate the file is corrupted beyond repair or "
                               "was encrypted with a different key."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Video content successfully decrypted, proceeding with metadata repair";
    
    // Get the actual show name from the UI or settings
    QString actualShowName;
    
    // Method 1: Try to get from the UI label
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->label_VP_Shows_Display_Name) {
        actualShowName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
        qDebug() << "Operations_VP_Shows: Got show name from UI label:" << actualShowName;
    }
    
    // Method 2: If UI label is empty, try to load from show settings
    if (actualShowName.isEmpty() && !m_currentShowFolder.isEmpty()) {
        VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        VP_ShowsSettings::ShowSettings settings;
        if (settingsManager.loadShowSettings(m_currentShowFolder, settings)) {
            actualShowName = settings.showName;
            qDebug() << "Operations_VP_Shows: Got show name from settings file:" << actualShowName;
        }
    }
    
    // Method 3: If still empty, try to get from another video file's metadata in the same folder
    if (actualShowName.isEmpty() && !m_currentShowFolder.isEmpty()) {
        QDir showDir(m_currentShowFolder);
        QStringList videoExtensions;
        videoExtensions << "*.mmvid";
        showDir.setNameFilters(videoExtensions);
        QStringList videoFiles = showDir.entryList(QDir::Files);
        
        VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        for (const QString& videoFile : videoFiles) {
            QString fullPath = showDir.absoluteFilePath(videoFile);
            if (fullPath != videoFilePath) { // Skip the broken file
                VP_ShowsMetadata::ShowMetadata tempMetadata;
                if (metadataManager.readMetadataFromFile(fullPath, tempMetadata)) {
                    actualShowName = tempMetadata.showName;
                    qDebug() << "Operations_VP_Shows: Got show name from another video's metadata:" << actualShowName;
                    break;
                }
            }
        }
    }
    
    if (actualShowName.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Warning - Could not determine show name, dialog will prompt for it";
    }
    
    // Video content is intact, open the metadata edit dialog in repair mode
    VP_ShowsEditMetadataDialog* dialog = new VP_ShowsEditMetadataDialog(
        videoFilePath,
        m_mainWindow->user_Key,
        m_mainWindow->user_Username,
        true,  // repair mode flag
        actualShowName,  // Pass the actual show name
        m_mainWindow);
    
    if (dialog->exec() == QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Metadata repair completed successfully";
        
        // Refresh the episode list to reflect the repaired file
        loadShowEpisodes(m_currentShowFolder);

        
        QMessageBox::information(m_mainWindow, tr("Repair Successful"),
                               tr("The video file has been successfully repaired."));
    } else {
        qDebug() << "Operations_VP_Shows: User cancelled metadata repair";
    }
    
    delete dialog;
}

// Debug function to corrupt a video's metadata (for testing)
void Operations_VP_Shows::corruptVideoMetadata()
{
#ifdef QT_DEBUG
    qDebug() << "Operations_VP_Shows: DEBUG - Corrupt video metadata";
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episode selected for corruption";
        return;
    }
    
    QString videoPath = m_contextMenuEpisodePaths.first();
    
    // Confirm this debug action
    int result = QMessageBox::warning(m_mainWindow,
                                     tr("DEBUG: Corrupt Metadata"),
                                     tr("This DEBUG function will intentionally corrupt the metadata header of:\n\n%1\n\n"
                                        "The file will become unplayable and appear in the Broken category.\n\n"
                                        "This is for testing purposes only. Continue?")
                                     .arg(QFileInfo(videoPath).fileName()),
                                     QMessageBox::No | QMessageBox::Yes,
                                     QMessageBox::No);
    
    if (result != QMessageBox::Yes) {
        qDebug() << "Operations_VP_Shows: User cancelled metadata corruption";
        return;
    }
    
    // Open the file and corrupt the metadata header
    QFile file(videoPath);
    if (!file.open(QIODevice::ReadWrite)) {
        qDebug() << "Operations_VP_Shows: Failed to open file for corruption:" << file.errorString();
        QMessageBox::critical(m_mainWindow, tr("Error"), 
                            tr("Failed to open file for metadata corruption."));
        return;
    }
    
    // The metadata is stored in a fixed-size block at the beginning
    // We'll corrupt it by writing random data to the metadata area
    const int corruptSize = 100;  // Corrupt first 100 bytes of metadata
    QByteArray randomData(corruptSize, 0);
    
    // Generate random bytes
    for (int i = 0; i < corruptSize; ++i) {
        randomData[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    
    // Skip the magic number position (first 4 bytes) to make corruption more obvious
    file.seek(4);
    qint64 written = file.write(randomData);
    file.close();
    
    if (written != corruptSize) {
        qDebug() << "Operations_VP_Shows: Failed to write corruption data";
        QMessageBox::critical(m_mainWindow, tr("Error"), 
                            tr("Failed to corrupt metadata."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Successfully corrupted metadata for:" << videoPath;
    QMessageBox::information(m_mainWindow,
                           tr("DEBUG: Success"),
                           tr("Metadata has been corrupted successfully.\n\n"
                              "The file will now appear in the Broken category."));
    
    // Refresh the episode list to show the file in broken category
    loadShowEpisodes(m_currentShowFolder);
#else
    // This function is not available in release builds
    qDebug() << "Operations_VP_Shows: Corrupt metadata function is only available in debug builds";
#endif
}

void Operations_VP_Shows::playEpisodeFromContextMenu()
{
    qDebug() << "Operations_VP_Shows: Play episode from context menu";
    
    // Check if we're currently decrypting an episode
    if (m_isDecrypting) {
        qDebug() << "Operations_VP_Shows: Currently decrypting an episode, ignoring context menu play";
        return;
    }
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episodes to play";
        return;
    }
    
    // Play the first episode in the list
    QString firstEpisodePath = m_contextMenuEpisodePaths.first();
    
    // Get the episode name from the tree item
    QString episodeName;
    if (m_contextMenuTreeItem) {
        if (m_contextMenuTreeItem->childCount() == 0) {
            // It's an episode item
            episodeName = m_contextMenuTreeItem->text(0);
        } else {
            // It's a language or season item, find the first episode
            QTreeWidgetItem* firstEpisode = m_contextMenuTreeItem;
            while (firstEpisode && firstEpisode->childCount() > 0) {
                firstEpisode = firstEpisode->child(0);
            }
            if (firstEpisode) {
                episodeName = firstEpisode->text(0);
            }
        }
    }
    
    if (episodeName.isEmpty()) {
        QFileInfo fileInfo(firstEpisodePath);
        episodeName = fileInfo.fileName();
    }
    
    qDebug() << "Operations_VP_Shows: Playing episode:" << episodeName;
    
    // Check if there's already a video player window open
    if (m_episodePlayer && m_episodePlayer->isVisible()) {
        qDebug() << "Operations_VP_Shows: Existing video player detected - closing it before playing new episode";
        
        // Store the episode information for playing after cleanup
        m_pendingContextMenuEpisodePath = firstEpisodePath;
        m_pendingContextMenuEpisodeName = episodeName;
        
        // Stop playback tracking if active
        if (m_playbackTracker && m_playbackTracker->isTracking()) {
            qDebug() << "Operations_VP_Shows: Stopping active playback tracking";
            m_playbackTracker->stopTracking();
        }
        
        // Force release the video file and clean up
        forceReleaseVideoFile();
        
        // Close the player
        if (m_episodePlayer->isVisible()) {
            m_episodePlayer->close();
        }
        m_episodePlayer.reset();
        
        // Clean up any existing temp file
        cleanupTempFile();
        
        qDebug() << "Operations_VP_Shows: Previous video player closed and cleaned up";
        
        // Use a timer to ensure cleanup is complete before playing new episode
        QTimer::singleShot(100, this, [this]() {
            if (!m_pendingContextMenuEpisodePath.isEmpty() && !m_pendingContextMenuEpisodeName.isEmpty()) {
                qDebug() << "Operations_VP_Shows: Playing pending context menu episode after cleanup";
                
                // Check if we should start from the beginning due to near-end position
                bool forceStartFromBeginning = false;
                if (m_watchHistory && !m_currentShowFolder.isEmpty()) {
                    QDir showDir(m_currentShowFolder);
                    QString relativePath = showDir.relativeFilePath(m_pendingContextMenuEpisodePath);
                    qint64 resumePosition = m_watchHistory->getResumePosition(relativePath);
                    
                    if (resumePosition > 0) {
                        EpisodeWatchInfo watchInfo = m_watchHistory->getEpisodeWatchInfo(relativePath);
                        if (watchInfo.totalDuration > 0) {
                            qint64 remainingTime = watchInfo.totalDuration - resumePosition;
                            if (remainingTime <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS) {
                                forceStartFromBeginning = true;
                                qDebug() << "Operations_VP_Shows: Context menu play - resume position is near end (" << remainingTime
                                         << "ms remaining), will start from beginning instead";
                            }
                        }
                    }
                }
                
                // Store flag to indicate we should start from beginning
                m_forceStartFromBeginning = forceStartFromBeginning;
                
                // Decrypt and play the episode
                QString episodePath = m_pendingContextMenuEpisodePath;
                QString episodeName = m_pendingContextMenuEpisodeName;
                
                // Clear the pending values
                m_pendingContextMenuEpisodePath.clear();
                m_pendingContextMenuEpisodeName.clear();
                
                decryptAndPlayEpisode(episodePath, episodeName);
            }
        });
        
        return;
    }
    
    // No existing player, proceed with normal play
    // Check if we should start from the beginning due to near-end position
    // This is for direct play from context menu
    bool forceStartFromBeginning = false;
    if (m_watchHistory && !m_currentShowFolder.isEmpty()) {
        QDir showDir(m_currentShowFolder);
        QString relativePath = showDir.relativeFilePath(firstEpisodePath);
        qint64 resumePosition = m_watchHistory->getResumePosition(relativePath);
        
        if (resumePosition > 0) {
            EpisodeWatchInfo watchInfo = m_watchHistory->getEpisodeWatchInfo(relativePath);
            if (watchInfo.totalDuration > 0) {
                qint64 remainingTime = watchInfo.totalDuration - resumePosition;
                if (remainingTime <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS) {
                    forceStartFromBeginning = true;
                    qDebug() << "Operations_VP_Shows: Context menu play - resume position is near end (" << remainingTime
                             << "ms remaining), will start from beginning instead";
                }
            }
        }
    }
    
    // Store flag to indicate we should start from beginning
    m_forceStartFromBeginning = forceStartFromBeginning;
    
    // Decrypt and play the episode
    decryptAndPlayEpisode(firstEpisodePath, episodeName);
}

void Operations_VP_Shows::decryptAndExportEpisodeFromContextMenu()
{
    qDebug() << "Operations_VP_Shows: Decrypt and export episodes from context menu";
    qDebug() << "Operations_VP_Shows: Episodes to export:" << m_contextMenuEpisodePaths.size();
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episodes to export";
        return;
    }
    
    // Get the show name from the display label
    QString showName;
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->label_VP_Shows_Display_Name) {
        showName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
    }
    
    if (showName.isEmpty()) {
        showName = "TV Show";
    }
    
    // Select export folder
    QString exportPath = QFileDialog::getExistingDirectory(
        m_mainWindow,
        tr("Select Export Folder"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (exportPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No export folder selected";
        return;
    }
    
    // Estimate the size needed
    qint64 estimatedSize = 0;
    for (const QString& episodePath : m_contextMenuEpisodePaths) {
        QFileInfo fileInfo(episodePath);
        estimatedSize += static_cast<qint64>(fileInfo.size() * 0.95); // Estimate 95% of encrypted size
    }
    
    // Check available disk space
    QStorageInfo storageInfo(exportPath);
    qint64 availableSpace = storageInfo.bytesAvailable();
    
    qDebug() << "Operations_VP_Shows: Estimated size:" << estimatedSize << "Available space:" << availableSpace;
    
    if (availableSpace < estimatedSize) {
        // Convert sizes to human-readable format
        auto formatSize = [](qint64 bytes) -> QString {
            const qint64 kb = 1024;
            const qint64 mb = kb * 1024;
            const qint64 gb = mb * 1024;
            
            if (bytes >= gb) {
                return QString("%1 GB").arg(bytes / double(gb), 0, 'f', 2);
            } else if (bytes >= mb) {
                return QString("%1 MB").arg(bytes / double(mb), 0, 'f', 2);
            } else if (bytes >= kb) {
                return QString("%1 KB").arg(bytes / double(kb), 0, 'f', 2);
            } else {
                return QString("%1 bytes").arg(bytes);
            }
        };
        
        QMessageBox::warning(m_mainWindow,
                           tr("Insufficient Disk Space"),
                           tr("There is not enough space on the disk to export the selected episodes.\n\n"
                              "Required size: %1\n"
                              "Available space: %2\n\n"
                              "Please free up some space and try again.")
                           .arg(formatSize(estimatedSize))
                           .arg(formatSize(availableSpace)));
        return;
    }
    
    // Build description for the export
    QString description;
    if (m_contextMenuTreeItem) {
        if (m_contextMenuTreeItem->childCount() == 0) {
            // Single episode
            description = m_contextMenuTreeItem->text(0);
        } else if (m_contextMenuTreeItem->parent() == nullptr) {
            // Language/Translation level
            description = QString("%1 - %2").arg(showName).arg(m_contextMenuTreeItem->text(0));
        } else {
            // Season level
            QString language = m_contextMenuTreeItem->parent()->text(0);
            description = QString("%1 - %2 - %3").arg(showName).arg(language).arg(m_contextMenuTreeItem->text(0));
        }
    }
    
    // Show confirmation dialog
    int episodeCount = m_contextMenuEpisodePaths.size();
    QString confirmMessage = tr("You are about to export and decrypt %1 episode%2\n\n"
                               "Export to: %3\n\n"
                               "Do you want to proceed?")
                           .arg(episodeCount)
                           .arg(episodeCount > 1 ? "s" : "")
                           .arg(exportPath);
    
    int result = QMessageBox::question(m_mainWindow,
                                      tr("Export Confirmation"),
                                      confirmMessage,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);
    
    if (result != QMessageBox::Yes) {
        qDebug() << "Operations_VP_Shows: Export cancelled by user";
        return;
    }
    
    // Perform the export without folder structure (direct export to selected folder)
    performEpisodeExportWithWorker(m_contextMenuEpisodePaths, exportPath, description, false);
}

void Operations_VP_Shows::editEpisodeMetadata()
{
    qDebug() << "Operations_VP_Shows: Edit episode metadata from context menu";
    
    // Ensure we have exactly one episode selected
    if (m_contextMenuEpisodePaths.isEmpty() || m_contextMenuEpisodePaths.size() > 1) {
        qDebug() << "Operations_VP_Shows: Invalid selection for metadata editing";
        return;
    }
    
    // Get the video file path
    QString videoFilePath = m_contextMenuEpisodePaths.first();
    qDebug() << "Operations_VP_Shows: Editing metadata for:" << videoFilePath;
    
    // Store the episode text BEFORE any operations that might refresh the tree
    QString originalEpisodeText;
    if (m_contextMenuTreeItem) {
        originalEpisodeText = m_contextMenuTreeItem->text(0);
    }
    
    // Validate file path using operations_files security
    InputValidation::ValidationResult pathValidation = 
        InputValidation::validateInput(videoFilePath, InputValidation::InputType::FilePath);
    if (!pathValidation.isValid) {
        qDebug() << "Operations_VP_Shows: Invalid file path:" << pathValidation.errorMessage;
        QMessageBox::warning(m_mainWindow, tr("Invalid Path"),
                           tr("The video file path is invalid."));
        return;
    }
    
    // Check if file exists
    if (!QFile::exists(videoFilePath)) {
        qDebug() << "Operations_VP_Shows: Video file does not exist:" << videoFilePath;
        QMessageBox::warning(m_mainWindow, tr("File Not Found"),
                           tr("The video file could not be found."));
        return;
    }
    
    // Simply check if file is locked and return silently if it is
    if (VP_MetadataLockManager::instance()->isLocked(videoFilePath)) {
        qDebug() << "Operations_VP_Shows: File is currently locked, not opening edit dialog";
        return; // Silent return - no error message
    }
    
    // Note: m_contextMenuTreeItem might be invalid at this point if tree was refreshed
    // We already captured what we needed (originalEpisodeText) earlier
    
    // Create and show the edit metadata dialog
    // The dialog will handle reading the current metadata internally
    // Pass empty string for show name since it will be loaded from metadata in normal edit mode
    VP_ShowsEditMetadataDialog dialog(videoFilePath, m_mainWindow->user_Key, m_mainWindow->user_Username, false, QString(), m_mainWindow);
    
    if (dialog.exec() == QDialog::Accepted) {
        // Get the updated metadata
        VP_ShowsMetadata::ShowMetadata updatedMetadata = dialog.getMetadata();
        
        qDebug() << "Operations_VP_Shows: User accepted metadata changes";
        qDebug() << "Operations_VP_Shows: Updated metadata - Show:" << updatedMetadata.showName
                 << "Season:" << updatedMetadata.season
                 << "Episode:" << updatedMetadata.episode
                 << "Name:" << updatedMetadata.EPName;
        
        // Validate the updated metadata fields using inputvalidation
        InputValidation::ValidationResult showNameValidation = 
            InputValidation::validateInput(updatedMetadata.showName, InputValidation::InputType::PlainText, 100);
        if (!showNameValidation.isValid) {
            qDebug() << "Operations_VP_Shows: Invalid show name:" << showNameValidation.errorMessage;
            QMessageBox::warning(m_mainWindow, tr("Invalid Show Name"),
                               tr("The show name is invalid: %1").arg(showNameValidation.errorMessage));
            return;
        }
        
        // Create metadata manager for saving
        VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        
        // Write updated metadata back to file
        if (!metadataManager.writeMetadataToFile(videoFilePath, updatedMetadata)) {
            qDebug() << "Operations_VP_Shows: Failed to write metadata to file";
            QMessageBox::critical(m_mainWindow, tr("Save Error"),
                                tr("Failed to save metadata changes to the video file."));
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Metadata successfully updated";
        
        // Refresh the tree widget to show updated metadata
        loadShowEpisodes(m_currentShowFolder);

        // Try to find and expand to the edited episode
        if (!videoFilePath.isEmpty()) {
            // Search for the episode in the refreshed tree by file path
            QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
            QTreeWidgetItem* foundItem = nullptr;
            
            // Search through all items in the tree
            for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                QTreeWidgetItem* languageItem = treeWidget->topLevelItem(i);
                
                // Search within this language version
                for (int j = 0; j < languageItem->childCount(); ++j) {
                    QTreeWidgetItem* child = languageItem->child(j);
                    
                    // Check direct children (might be episodes or seasons)
                    if (child->childCount() == 0) {
                        // It's an episode
                        if (child->data(0, Qt::UserRole).toString() == videoFilePath) {
                            foundItem = child;
                            break;
                        }
                    } else {
                        // It's a season or category, check its children
                        for (int k = 0; k < child->childCount(); ++k) {
                            QTreeWidgetItem* episode = child->child(k);
                            if (episode->data(0, Qt::UserRole).toString() == videoFilePath) {
                                foundItem = episode;
                                break;
                            }
                        }
                    }
                    
                    if (foundItem) break;
                }
                
                if (foundItem) break;
            }
            
            // If found, expand to show it
            if (foundItem) {
                qDebug() << "Operations_VP_Shows: Found edited episode, expanding to show it";
                
                // Expand all parent items
                QTreeWidgetItem* parent = foundItem->parent();
                while (parent) {
                    parent->setExpanded(true);
                    parent = parent->parent();
                }
                
                // Scroll to the item
                treeWidget->scrollToItem(foundItem, QAbstractItemView::PositionAtCenter);
                
                // Select the item
                treeWidget->setCurrentItem(foundItem);
                foundItem->setSelected(true);
            } else {
                qDebug() << "Operations_VP_Shows: Could not find edited episode in refreshed tree";
            }
        }
        
        // Check if TMDB re-acquisition was requested
        if (dialog.shouldReacquireTMDB()) {
            qDebug() << "Operations_VP_Shows: TMDB re-acquisition requested for single episode";
            
            // Check if TMDB is enabled and API is available
            if (!VP_ShowsConfig::isTMDBEnabled()) {
                QMessageBox::information(m_mainWindow, tr("TMDB Disabled"),
                                       tr("TMDB integration is disabled. Please enable it in the settings."));
            } else if (!VP_ShowsConfig::hasApiKey()) {
                QMessageBox::warning(m_mainWindow, tr("No API Key"),
                                   tr("TMDB API key is not configured."));
            } else {
                // Perform TMDB re-acquisition for this single episode
                reacquireTMDBForSingleEpisode(videoFilePath, updatedMetadata);
                
                // After TMDB acquisition, refresh the tree widget to show the updated metadata
                qDebug() << "Operations_VP_Shows: Refreshing episode tree after TMDB update";
                loadShowEpisodes(m_currentShowFolder);
            }
        }
        
        // Success message removed - lack of error dialog is sufficient
        // QMessageBox::information(m_mainWindow, tr("Success"),
        //                        tr("Metadata has been successfully updated."));
    } else {
        qDebug() << "Operations_VP_Shows: User cancelled metadata editing";
    }
}

void Operations_VP_Shows::editMultipleEpisodesMetadata()
{
    qDebug() << "Operations_VP_Shows: Edit multiple episodes metadata from context menu";
    
    // Ensure we have multiple episodes selected
    if (m_contextMenuEpisodePaths.isEmpty() || m_contextMenuEpisodePaths.size() <= 1) {
        qDebug() << "Operations_VP_Shows: Invalid selection for multiple metadata editing";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Editing metadata for" << m_contextMenuEpisodePaths.size() << "episodes";
    
    // Check MainWindow validity
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow is null";
        return;
    }
    
    // Basic validation - check if any files are locked
    for (const QString& videoFilePath : m_contextMenuEpisodePaths) {
        if (VP_MetadataLockManager::instance()->isLocked(videoFilePath)) {
            qDebug() << "Operations_VP_Shows: One or more files are currently locked";
            QMessageBox::warning(m_mainWindow, tr("Files Locked"),
                               tr("One or more files are currently being edited. Please try again later."));
            return;
        }
    }
    
    // Create and show the multiple metadata edit dialog
    // The dialog will handle all validation, metadata changes, and success messages
    VP_ShowsEditMultipleMetadataDialog* dialog = new VP_ShowsEditMultipleMetadataDialog(m_contextMenuEpisodePaths, m_mainWindow->user_Key, m_mainWindow->user_Username, m_mainWindow);
    
    if (dialog->exec() == QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: User accepted multiple metadata changes";
        
        // Check if TMDB re-acquisition was requested
        if (dialog->shouldReacquireTMDB()) {
            qDebug() << "Operations_VP_Shows: TMDB re-acquisition requested for multiple episodes";
            
            // Check if TMDB is enabled and API is available
            if (!VP_ShowsConfig::isTMDBEnabled()) {
                QMessageBox::information(m_mainWindow, tr("TMDB Disabled"),
                                       tr("TMDB integration is disabled. Please enable it in the settings."));
            } else if (!VP_ShowsConfig::hasApiKey()) {
                QMessageBox::warning(m_mainWindow, tr("No API Key"),
                                   tr("TMDB API key is not configured."));
            } else {
                // Perform TMDB re-acquisition using pre-loaded metadata from dialog
                reacquireTMDBForMultipleEpisodesWithMetadata(dialog->getVideoFilePaths(), dialog->getAllMetadata(), dialog);
                
                // After TMDB processing, save all the updated metadata
                dialog->applyChangesAndSave();
            }
        }
        
        // The dialog has already applied all changes and shown success messages
        // We just need to refresh the tree widget to show the updated metadata
        qDebug() << "Operations_VP_Shows: Refreshing episode tree after multiple metadata edit";
        loadShowEpisodes(m_currentShowFolder);
    } else {
        qDebug() << "Operations_VP_Shows: User cancelled multiple metadata editing";
    }
    
    delete dialog;
}

void Operations_VP_Shows::reacquireTMDBFromContextMenu()
{
    qDebug() << "Operations_VP_Shows: Re-acquire TMDB metadata from context menu";
    
    // Check if we have episodes selected
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episodes selected for TMDB re-acquisition";
        return;
    }
    
    // Check MainWindow validity
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow is null";
        return;
    }
    
    // Check if TMDB is enabled and API is available
    if (!VP_ShowsConfig::isTMDBEnabled()) {
        QMessageBox::information(m_mainWindow, tr("TMDB Disabled"),
                               tr("TMDB integration is disabled. Please enable it in the settings."));
        return;
    }
    
    if (!VP_ShowsConfig::hasApiKey()) {
        QMessageBox::warning(m_mainWindow, tr("No API Key"),
                           tr("TMDB API key is not configured."));
        return;
    }
    
    // Check for valid show ID in settings
    QString showFolderPath = m_currentShowFolder;
    if (showFolderPath.isEmpty() && !m_contextMenuEpisodePaths.isEmpty()) {
        QFileInfo fileInfo(m_contextMenuEpisodePaths.first());
        showFolderPath = fileInfo.absolutePath();
    }
    
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    VP_ShowsSettings::ShowSettings showSettings;
    if (!settingsManager.loadShowSettings(showFolderPath, showSettings)) {
        QMessageBox::warning(m_mainWindow, tr("Error"),
                           tr("Could not load show settings."));
        return;
    }
    
    // Check if show ID is valid
    bool hasValidShowId = !showSettings.showId.isEmpty() && showSettings.showId != "error";
    if (!hasValidShowId) {
        QMessageBox::warning(m_mainWindow, tr("No Show ID"),
                           tr("This show does not have a valid TMDB show ID.\n\n"
                              "Please configure the show ID in the show settings first."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Number of episodes selected:" << m_contextMenuEpisodePaths.size();
    
    if (m_contextMenuEpisodePaths.size() == 1) {
        // Single episode - load metadata and call the existing function
        QString videoFilePath = m_contextMenuEpisodePaths.first();
        qDebug() << "Operations_VP_Shows: Single episode TMDB re-acquisition for:" << videoFilePath;
        
        // Load the current metadata
        VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        VP_ShowsMetadata::ShowMetadata metadata;
        
        if (!metadataManager.readMetadataFromFile(videoFilePath, metadata)) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from file";
            QMessageBox::warning(m_mainWindow, tr("Error"),
                               tr("Failed to read metadata from the selected file."));
            return;
        }
        
        // Call the existing single episode function
        reacquireTMDBForSingleEpisode(videoFilePath, metadata);
        
        // Refresh the episode tree to show updated metadata
        qDebug() << "Operations_VP_Shows: Refreshing episode tree after TMDB update";
        loadShowEpisodes(m_currentShowFolder);

        
    } else {
        // Multiple episodes - call the existing multiple episodes function
        qDebug() << "Operations_VP_Shows: Multiple episodes TMDB re-acquisition for" << m_contextMenuEpisodePaths.size() << "files";
        
        // Call the existing multiple episodes function which handles the progress dialog
        reacquireTMDBForMultipleEpisodes(m_contextMenuEpisodePaths);
        
        // Refresh the episode tree to show updated metadata
        qDebug() << "Operations_VP_Shows: Refreshing episode tree after TMDB updates";
        loadShowEpisodes(m_currentShowFolder);

    }
}

void Operations_VP_Shows::reacquireTMDBForSingleEpisode(const QString& videoFilePath, const VP_ShowsMetadata::ShowMetadata& metadata)
{
    qDebug() << "Operations_VP_Shows: Re-acquiring TMDB data for single episode:" << videoFilePath;
    
    // Check content type - only process regular episodes
    if (metadata.contentType != VP_ShowsMetadata::Regular) {
        qDebug() << "Operations_VP_Shows: Skipping TMDB re-acquisition for non-regular content type:" 
                 << metadata.getContentTypeString();
        return;
    }
    
    // Create TMDB API instance
    VP_ShowsTMDB tmdbApi;
    
    // Get and set API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No TMDB API key available";
        QMessageBox::warning(m_mainWindow, tr("API Key Missing"),
                           tr("TMDB API key is not configured. Please check tmdb_api_key.h."));
        return;
    }
    tmdbApi.setApiKey(apiKey);
    
    // Try to get show info using ID from settings if available
    VP_ShowsTMDB::ShowInfo showInfo;
    bool showInfoLoaded = false;
    
    // Get the show folder path from the video file path
    QFileInfo fileInfo(videoFilePath);
    QString showFolderPath = fileInfo.absolutePath();
    
    // Load show settings to get the show ID
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    VP_ShowsSettings::ShowSettings showSettings;
    
    if (settingsManager.loadShowSettings(showFolderPath, showSettings)) {
        // Check if we have a valid show ID
        if (!showSettings.showId.isEmpty() && showSettings.showId != "error") {
            bool idOk = false;
            int showId = showSettings.showId.toInt(&idOk);
            
            if (idOk && showId > 0) {
                qDebug() << "Operations_VP_Shows: Using stored show ID:" << showId;
                if (tmdbApi.getShowById(showId, showInfo)) {
                    showInfoLoaded = true;
                    qDebug() << "Operations_VP_Shows: Successfully loaded show info using ID";
                } else {
                    qDebug() << "Operations_VP_Shows: Failed to load show info using ID:" << showId;
                }
            }
        }
    }
    
    // Fallback to searching by name if ID is not available or failed
    if (!showInfoLoaded) {
        qDebug() << "Operations_VP_Shows: Falling back to search by show name:" << metadata.showName;
        if (!tmdbApi.searchTVShow(metadata.showName, showInfo)) {
            QMessageBox::warning(m_mainWindow, tr("Show Not Found"),
                               tr("Could not find '%1' on TMDB.").arg(metadata.showName));
            return;
        }
        
        // Update the show ID in settings for future use
        if (showInfo.tmdbId > 0) {
            showSettings.showId = QString::number(showInfo.tmdbId);
            settingsManager.saveShowSettings(showFolderPath, showSettings);
            qDebug() << "Operations_VP_Shows: Updated show ID in settings:" << showSettings.showId;
        }
    }
    
    // Parse season and episode numbers
    bool seasonOk = false, episodeOk = false;
    int seasonNum = metadata.season.toInt(&seasonOk);
    int episodeNum = metadata.episode.toInt(&episodeOk);
    
    if (!seasonOk || !episodeOk) {
        QMessageBox::warning(m_mainWindow, tr("Invalid Episode Info"),
                           tr("Could not parse season/episode numbers."));
        return;
    }
    
    // Check for absolute numbering and map if needed
    int tmdbSeason = seasonNum;
    int tmdbEpisode = episodeNum;
    
    if (seasonNum == 0 && episodeNum > 0) {
        qDebug() << "Operations_VP_Shows: Absolute numbering detected for single episode" << episodeNum;
        
        // Build episode map for this show
        QMap<int, VP_ShowsTMDB::EpisodeMapping> episodeMap = tmdbApi.buildEpisodeMap(showInfo.tmdbId);
        
        if (episodeMap.contains(episodeNum)) {
            const VP_ShowsTMDB::EpisodeMapping& mapping = episodeMap[episodeNum];
            tmdbSeason = mapping.season;
            tmdbEpisode = mapping.episode;
            qDebug() << "Operations_VP_Shows: Mapped absolute episode" << episodeNum 
                     << "to S" << tmdbSeason << "E" << tmdbEpisode;
        } else {
            // Fallback calculation
            const int episodesPerSeason = 26;
            tmdbSeason = ((episodeNum - 1) / episodesPerSeason) + 1;
            tmdbEpisode = ((episodeNum - 1) % episodesPerSeason) + 1;
            qDebug() << "Operations_VP_Shows: Using fallback mapping to S" << tmdbSeason << "E" << tmdbEpisode;
        }
    }
    
    // Validate mapped values
    if (tmdbSeason <= 0 || tmdbEpisode <= 0) {
        QMessageBox::warning(m_mainWindow, tr("Invalid Episode Info"),
                           tr("Could not map episode to valid TMDB season/episode."));
        return;
    }
    
    // Get episode info from TMDB
    VP_ShowsTMDB::EpisodeInfo episodeInfo;
    if (!tmdbApi.getEpisodeInfo(showInfo.tmdbId, tmdbSeason, tmdbEpisode, episodeInfo)) {
        QMessageBox::warning(m_mainWindow, tr("Episode Not Found"),
                           tr("Could not find S%1E%2 on TMDB.").arg(seasonNum, 2, 10, QChar('0')).arg(episodeNum, 2, 10, QChar('0')));
        return;
    }
    
    // Update the metadata with TMDB info
    VP_ShowsMetadata::ShowMetadata updatedMetadata = metadata;
    updatedMetadata.EPName = episodeInfo.episodeName;
    updatedMetadata.EPDescription = episodeInfo.overview;
    updatedMetadata.airDate = episodeInfo.airDate;
    
    // Download and update episode image if available
    if (!episodeInfo.stillPath.isEmpty()) {
        QString tempDir = VP_ShowsConfig::getTempDirectory(m_mainWindow->user_Username);
        if (!tempDir.isEmpty()) {
            QString tempImagePath = QDir(tempDir).absoluteFilePath("tmdb_episode_image.jpg");
            if (tmdbApi.downloadImage(episodeInfo.stillPath, tempImagePath)) {
                QFile imageFile(tempImagePath);
                if (imageFile.open(QIODevice::ReadOnly)) {
                    QByteArray imageData = imageFile.readAll();
                    imageFile.close();
                    
                    // Scale the image to 128x128 to reduce size
                    QByteArray scaledImage = VP_ShowsTMDB::scaleImageToSize(imageData, 128, 128);
                    
                    // Only store if scaled image is within size limit (32KB)
                    if (!scaledImage.isEmpty() && scaledImage.size() <= VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
                        updatedMetadata.EPImage = scaledImage;
                        qDebug() << "Operations_VP_Shows: Added scaled episode thumbnail (" 
                                 << scaledImage.size() << "bytes)";
                    } else {
                        qDebug() << "Operations_VP_Shows: Episode image too large even after scaling"
                                 << scaledImage.size() << "bytes (max:" << VP_ShowsMetadata::MAX_EP_IMAGE_SIZE << ")";
                    }
                }
                // Clean up temp file
                OperationsFiles::secureDelete(tempImagePath, 1, false);
            }
        }
    }
    
    // Save updated metadata
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    if (!metadataManager.writeMetadataToFile(videoFilePath, updatedMetadata)) {
        QMessageBox::critical(m_mainWindow, tr("Save Error"),
                            tr("Failed to save TMDB metadata to file."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Successfully updated episode with TMDB data";
    // Success message removed - lack of error dialog is sufficient
    // QMessageBox::information(m_mainWindow, tr("Success"),
    //                        tr("TMDB metadata has been successfully acquired."));
}

void Operations_VP_Shows::reacquireTMDBForMultipleEpisodesWithMetadata(const QStringList& videoFilePaths, 
                                                                       const QList<VP_ShowsMetadata::ShowMetadata>& metadataList,
                                                                       VP_ShowsEditMultipleMetadataDialog* dialog)
{
    qDebug() << "Operations_VP_Shows: Re-acquiring TMDB data for" << videoFilePaths.size() << "episodes with pre-loaded metadata";
    
    if (videoFilePaths.isEmpty() || metadataList.isEmpty() || videoFilePaths.size() != metadataList.size()) {
        qDebug() << "Operations_VP_Shows: Invalid input - paths and metadata count mismatch";
        return;
    }
    
    // Create progress dialog
    VP_ShowsTMDBReacquisitionDialog* progressDialog = new VP_ShowsTMDBReacquisitionDialog(m_mainWindow);
    progressDialog->setTotalEpisodes(videoFilePaths.size());
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Connect cancel signal
    bool operationCancelled = false;
    connect(progressDialog, &VP_ShowsTMDBReacquisitionDialog::cancelRequested,
            [&operationCancelled]() { operationCancelled = true; });
    
    // Show the progress dialog
    progressDialog->show();
    progressDialog->raise();
    progressDialog->activateWindow();
    
    // Create TMDB API instance
    VP_ShowsTMDB tmdbApi;
    
    // Get and set API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No TMDB API key available for multiple episodes";
        progressDialog->close();
        delete progressDialog;
        QMessageBox::warning(m_mainWindow, tr("API Key Missing"),
                           tr("TMDB API key is not configured. Please check tmdb_api_key.h."));
        return;
    }
    tmdbApi.setApiKey(apiKey);
    
    // Track results
    int successCount = 0;
    int failedCount = 0;
    int skippedCount = 0;
    QString currentShowName;
    int currentShowTmdbId = -1;
    
    // Process each episode using pre-loaded metadata
    for (int i = 0; i < videoFilePaths.size(); ++i) {
        if (operationCancelled) {
            break;
        }
        
        const QString& videoFilePath = videoFilePaths[i];
        QString fileName = QFileInfo(videoFilePath).fileName();
        
        progressDialog->updateProgress(i + 1, fileName);
        QApplication::processEvents();
        
        // Use pre-loaded metadata - apply any dialog changes first
        VP_ShowsMetadata::ShowMetadata metadata = metadataList[i];
        
        // Check content type - skip non-regular episodes
        if (metadata.contentType != VP_ShowsMetadata::Regular) {
            qDebug() << "Operations_VP_Shows: Skipping TMDB re-acquisition for non-regular content type:" 
                     << metadata.getContentTypeString() << "File:" << fileName;
            skippedCount++;
            continue;
        }
        
        // Apply dialog changes if present (like season updates)
        auto changes = dialog->getMetadataChanges();
        if (changes.changeSeason) {
            metadata.season = changes.season;
        }
        
        // Search for show if it's different from the previous one
        if (metadata.showName != currentShowName) {
            currentShowName = metadata.showName;
            currentShowTmdbId = -1;  // Reset ID when show changes
            
            // Try to get show info using ID from settings if available
            bool showInfoLoaded = false;
            VP_ShowsTMDB::ShowInfo showInfo;
            
            // Get the show folder path from the video file path
            QFileInfo fileInfo(videoFilePath);
            QString showFolderPath = fileInfo.absolutePath();
            
            // Load show settings to get the show ID
            VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
            VP_ShowsSettings::ShowSettings showSettings;
            
            if (settingsManager.loadShowSettings(showFolderPath, showSettings)) {
                // Check if we have a valid show ID
                if (!showSettings.showId.isEmpty() && showSettings.showId != "error") {
                    bool idOk = false;
                    int showId = showSettings.showId.toInt(&idOk);
                    
                    if (idOk && showId > 0) {
                        progressDialog->setStatusMessage(tr("Loading show information using ID: %1").arg(showId));
                        QApplication::processEvents();
                        
                        if (tmdbApi.getShowById(showId, showInfo)) {
                            showInfoLoaded = true;
                            currentShowTmdbId = showInfo.tmdbId;
                            qDebug() << "Operations_VP_Shows: Successfully loaded show info using ID:" << showId;
                        } else {
                            qDebug() << "Operations_VP_Shows: Failed to load show info using ID:" << showId;
                        }
                    }
                }
            }
            
            // Fallback to searching by name if ID is not available or failed
            if (!showInfoLoaded) {
                progressDialog->setStatusMessage(tr("Searching for show: %1").arg(currentShowName));
                QApplication::processEvents();
                
                if (!tmdbApi.searchTVShow(currentShowName, showInfo)) {
                    failedCount++;
                    continue;
                }
                currentShowTmdbId = showInfo.tmdbId;
                
                // Update the show ID in settings for future use
                if (showInfo.tmdbId > 0) {
                    showSettings.showId = QString::number(showInfo.tmdbId);
                    settingsManager.saveShowSettings(showFolderPath, showSettings);
                    qDebug() << "Operations_VP_Shows: Updated show ID in settings:" << showSettings.showId;
                }
            }
        }
        
        // Parse season and episode numbers
        bool seasonOk = false, episodeOk = false;
        int seasonNum = metadata.season.toInt(&seasonOk);
        int episodeNum = metadata.episode.toInt(&episodeOk);
        
        // Check for absolute numbering (season = 0)
        bool isAbsoluteNumbering = (seasonNum == 0 && episodeNum > 0);
        int tmdbSeason = seasonNum;
        int tmdbEpisode = episodeNum;
        
        if (isAbsoluteNumbering) {
            qDebug() << "Operations_VP_Shows: Absolute numbering detected for episode" << episodeNum;
            
            // For absolute numbering, we need to build an episode map
            // Build it once per show
            static QMap<int, VP_ShowsTMDB::EpisodeMapping> episodeMap;
            static int lastShowId = -1;
            
            if (currentShowTmdbId != lastShowId) {
                qDebug() << "Operations_VP_Shows: Building episode map for show ID" << currentShowTmdbId;
                episodeMap = tmdbApi.buildEpisodeMap(currentShowTmdbId);
                lastShowId = currentShowTmdbId;
                qDebug() << "Operations_VP_Shows: Episode map built with" << episodeMap.size() << "entries";
            }
            
            // Try to map the absolute episode number
            if (episodeMap.contains(episodeNum)) {
                const VP_ShowsTMDB::EpisodeMapping& mapping = episodeMap[episodeNum];
                tmdbSeason = mapping.season;
                tmdbEpisode = mapping.episode;
                qDebug() << "Operations_VP_Shows: Mapped absolute episode" << episodeNum 
                         << "to S" << tmdbSeason << "E" << tmdbEpisode;
            } else {
                qDebug() << "Operations_VP_Shows: No mapping found for absolute episode" << episodeNum;
                // Try a fallback calculation (26 episodes per season is common for anime)
                const int episodesPerSeason = 26;
                tmdbSeason = ((episodeNum - 1) / episodesPerSeason) + 1;
                tmdbEpisode = ((episodeNum - 1) % episodesPerSeason) + 1;
                qDebug() << "Operations_VP_Shows: Using fallback mapping to S" << tmdbSeason << "E" << tmdbEpisode;
            }
        }
        
        // Skip if we couldn't parse the numbers or if they're invalid for TMDB
        if (!seasonOk || !episodeOk || episodeNum <= 0 || currentShowTmdbId <= 0) {
            qDebug() << "Operations_VP_Shows: Skipping episode - Invalid episode numbers."
                     << "Season:" << metadata.season << "Episode:" << metadata.episode;
            failedCount++;
            continue;
        }
        
        // Also skip if the mapped values are invalid for TMDB
        if (tmdbSeason <= 0 || tmdbEpisode <= 0) {
            qDebug() << "Operations_VP_Shows: Skipping episode - Invalid TMDB mapping."
                     << "TMDB Season:" << tmdbSeason << "TMDB Episode:" << tmdbEpisode;
            failedCount++;
            continue;
        }
        
        // Get episode info from TMDB with rate limit handling
        VP_ShowsTMDB::EpisodeInfo episodeInfo;
        bool foundEpisode = false;
        int retryCount = 0;
        
        do {
            // Try to get episode info using mapped values for absolute numbering
            if (tmdbApi.getEpisodeInfo(currentShowTmdbId, tmdbSeason, tmdbEpisode, episodeInfo)) {
                foundEpisode = true;
                break;
            }

            // API call failed, assume rate limit and retry
            retryCount++;
            qDebug() << "Operations_VP_Shows: API call failed for episode, retry" << retryCount;

            // Show rate limit message and wait
            progressDialog->showRateLimitMessage(1);
            QThread::sleep(1); // Wait 1 second before retry
            QApplication::processEvents();

            // Check if dialog was closed during wait or operation cancelled
            if (!progressDialog->isVisible() || operationCancelled) {
                operationCancelled = true;
                break;
            }
        } while (!foundEpisode && !operationCancelled);
        
        if (!foundEpisode) {
            failedCount++;
            continue;
        }
        
        // Update metadata with TMDB info
        metadata.EPName = episodeInfo.episodeName;
        metadata.EPDescription = episodeInfo.overview;
        metadata.airDate = episodeInfo.airDate;
        
        // Download and update episode image if available
        if (!episodeInfo.stillPath.isEmpty()) {
            QString tempDir = VP_ShowsConfig::getTempDirectory(m_mainWindow->user_Username);
            if (!tempDir.isEmpty()) {
                QString tempImagePath = QDir(tempDir).absoluteFilePath(QString("tmdb_episode_%1.jpg").arg(i));
                if (tmdbApi.downloadImage(episodeInfo.stillPath, tempImagePath)) {
                    QFile imageFile(tempImagePath);
                    if (imageFile.open(QIODevice::ReadOnly)) {
                        QByteArray imageData = imageFile.readAll();
                        imageFile.close();
                        
                        // Scale the image to 128x128 to reduce size
                        QByteArray scaledImage = VP_ShowsTMDB::scaleImageToSize(imageData, 128, 128);
                        
                        // Only store if scaled image is within size limit (32KB)
                        if (!scaledImage.isEmpty() && scaledImage.size() <= VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
                            metadata.EPImage = scaledImage;
                            qDebug() << "Operations_VP_Shows: Added scaled episode thumbnail (" 
                                     << scaledImage.size() << "bytes) for episode" << i;
                        } else {
                            qDebug() << "Operations_VP_Shows: Episode image too large even after scaling for episode" << i
                                     << scaledImage.size() << "bytes (max:" << VP_ShowsMetadata::MAX_EP_IMAGE_SIZE << ")";
                        }
                    }
                    // Clean up temp file
                    OperationsFiles::secureDelete(tempImagePath, 1, false);
                }
            }
        }
        
        // Update the dialog's metadata list with TMDB data
        dialog->updateMetadataAfterTMDB(i, metadata);
        successCount++;
        
        // Small delay to avoid hitting rate limits
        QThread::msleep(100);
        QApplication::processEvents();
        
        // Check if dialog was closed
        if (!progressDialog->isVisible()) {
            operationCancelled = true;
            break;
        }
    }
    
    // Check if operation was truly cancelled (not just completed)
    int processedCount = successCount + failedCount;
    bool wasActuallyCancelled = operationCancelled && (processedCount < videoFilePaths.size());
    
    // Close progress dialog
    if (progressDialog->isVisible()) {
        progressDialog->close();
    }
    delete progressDialog;
    
    // Show summary
    QString summary = tr("TMDB data re-acquisition completed.\n\n"
                        "Successful: %1\n"
                        "Failed: %2")
                     .arg(successCount)
                     .arg(failedCount);
    
    if (skippedCount > 0) {
        summary += tr("\nSkipped: %1 (non-regular episodes)").arg(skippedCount);
    }
    
    if (wasActuallyCancelled) {
        summary += tr("\n\nOperation was cancelled by user.");
    }
    
    QMessageBox::information(m_mainWindow, tr("Re-acquisition Complete"), summary);
    
    qDebug() << "Operations_VP_Shows: TMDB reacquisition with pre-loaded metadata finished. Success:" << successCount << "Failed:" << failedCount;
}

void Operations_VP_Shows::reacquireTMDBForMultipleEpisodes(const QStringList& videoFilePaths)
{
    qDebug() << "Operations_VP_Shows: Re-acquiring TMDB data for" << videoFilePaths.size() << "episodes";
    
    if (videoFilePaths.isEmpty()) {
        return;
    }
    
    // Create progress dialog
    VP_ShowsTMDBReacquisitionDialog* progressDialog = new VP_ShowsTMDBReacquisitionDialog(m_mainWindow);
    progressDialog->setTotalEpisodes(videoFilePaths.size());
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Connect cancel signal
    bool operationCancelled = false;
    connect(progressDialog, &VP_ShowsTMDBReacquisitionDialog::cancelRequested,
            [&operationCancelled]() { operationCancelled = true; });
    
    // Show the progress dialog
    progressDialog->show();
    progressDialog->raise();
    progressDialog->activateWindow();
    
    // Create TMDB API instance
    VP_ShowsTMDB tmdbApi;
    
    // Get and set API key
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No TMDB API key available for multiple episodes";
        progressDialog->close();
        delete progressDialog;
        QMessageBox::warning(m_mainWindow, tr("API Key Missing"),
                           tr("TMDB API key is not configured. Please check tmdb_api_key.h."));
        return;
    }
    tmdbApi.setApiKey(apiKey);
    
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Track results
    int successCount = 0;
    int failedCount = 0;
    QString currentShowName;
    int currentShowTmdbId = -1;
    
    for (int i = 0; i < videoFilePaths.size(); ++i) {
        if (operationCancelled) {
            break;
        }
        
        const QString& videoFilePath = videoFilePaths[i];
        QString fileName = QFileInfo(videoFilePath).fileName();
        
        progressDialog->updateProgress(i + 1, fileName);
        QApplication::processEvents();
        
        // Read current metadata
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(videoFilePath, metadata)) {
            failedCount++;
            continue;
        }
        
        // Check content type - skip non-regular episodes
        if (metadata.contentType != VP_ShowsMetadata::Regular) {
            qDebug() << "Operations_VP_Shows: Skipping TMDB re-acquisition for non-regular content type:" 
                     << metadata.getContentTypeString() << "File:" << fileName;
            continue;
        }
        
        // Search for show if it's different from the previous one
        if (metadata.showName != currentShowName) {
            currentShowName = metadata.showName;
            currentShowTmdbId = -1;  // Reset ID when show changes
            
            // Try to get show info using ID from settings if available
            bool showInfoLoaded = false;
            VP_ShowsTMDB::ShowInfo showInfo;
            
            // Get the show folder path from the video file path
            QFileInfo fileInfo(videoFilePath);
            QString showFolderPath = fileInfo.absolutePath();
            
            // Load show settings to get the show ID
            VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
            VP_ShowsSettings::ShowSettings showSettings;
            
            if (settingsManager.loadShowSettings(showFolderPath, showSettings)) {
                // Check if we have a valid show ID
                if (!showSettings.showId.isEmpty() && showSettings.showId != "error") {
                    bool idOk = false;
                    int showId = showSettings.showId.toInt(&idOk);
                    
                    if (idOk && showId > 0) {
                        progressDialog->setStatusMessage(tr("Loading show information using ID: %1").arg(showId));
                        QApplication::processEvents();
                        
                        if (tmdbApi.getShowById(showId, showInfo)) {
                            showInfoLoaded = true;
                            currentShowTmdbId = showInfo.tmdbId;
                            qDebug() << "Operations_VP_Shows: Successfully loaded show info using ID:" << showId;
                        } else {
                            qDebug() << "Operations_VP_Shows: Failed to load show info using ID:" << showId;
                        }
                    }
                }
            }
            
            // Fallback to searching by name if ID is not available or failed
            if (!showInfoLoaded) {
                progressDialog->setStatusMessage(tr("Searching for show: %1").arg(currentShowName));
                QApplication::processEvents();
                
                if (!tmdbApi.searchTVShow(currentShowName, showInfo)) {
                    failedCount++;
                    continue;
                }
                currentShowTmdbId = showInfo.tmdbId;
                
                // Update the show ID in settings for future use
                if (showInfo.tmdbId > 0) {
                    showSettings.showId = QString::number(showInfo.tmdbId);
                    settingsManager.saveShowSettings(showFolderPath, showSettings);
                    qDebug() << "Operations_VP_Shows: Updated show ID in settings:" << showSettings.showId;
                }
            }
        }
        
        // Parse season and episode numbers
        bool seasonOk = false, episodeOk = false;
        int seasonNum = metadata.season.toInt(&seasonOk);
        int episodeNum = metadata.episode.toInt(&episodeOk);
        
        // Check for absolute numbering (season = 0)
        bool isAbsoluteNumbering = (seasonNum == 0 && episodeNum > 0);
        int tmdbSeason = seasonNum;
        int tmdbEpisode = episodeNum;
        
        if (isAbsoluteNumbering) {
            qDebug() << "Operations_VP_Shows: Absolute numbering detected for episode" << episodeNum;
            
            // For absolute numbering, we need to build an episode map
            // Build it once per show
            static QMap<int, VP_ShowsTMDB::EpisodeMapping> episodeMap;
            static int lastShowId = -1;
            
            if (currentShowTmdbId != lastShowId) {
                qDebug() << "Operations_VP_Shows: Building episode map for show ID" << currentShowTmdbId;
                episodeMap = tmdbApi.buildEpisodeMap(currentShowTmdbId);
                lastShowId = currentShowTmdbId;
                qDebug() << "Operations_VP_Shows: Episode map built with" << episodeMap.size() << "entries";
            }
            
            // Try to map the absolute episode number
            if (episodeMap.contains(episodeNum)) {
                const VP_ShowsTMDB::EpisodeMapping& mapping = episodeMap[episodeNum];
                tmdbSeason = mapping.season;
                tmdbEpisode = mapping.episode;
                qDebug() << "Operations_VP_Shows: Mapped absolute episode" << episodeNum 
                         << "to S" << tmdbSeason << "E" << tmdbEpisode;
            } else {
                qDebug() << "Operations_VP_Shows: No mapping found for absolute episode" << episodeNum;
                // Try a fallback calculation (26 episodes per season is common for anime)
                const int episodesPerSeason = 26;
                tmdbSeason = ((episodeNum - 1) / episodesPerSeason) + 1;
                tmdbEpisode = ((episodeNum - 1) % episodesPerSeason) + 1;
                qDebug() << "Operations_VP_Shows: Using fallback mapping to S" << tmdbSeason << "E" << tmdbEpisode;
            }
        }
        
        // Skip if we couldn't parse the numbers or if they're invalid for TMDB
        if (!seasonOk || !episodeOk || episodeNum <= 0 || currentShowTmdbId <= 0) {
            qDebug() << "Operations_VP_Shows: Skipping episode - Invalid episode numbers."
                     << "Season:" << metadata.season << "Episode:" << metadata.episode;
            failedCount++;
            continue;
        }
        
        // Also skip if the mapped values are invalid for TMDB
        if (tmdbSeason <= 0 || tmdbEpisode <= 0) {
            qDebug() << "Operations_VP_Shows: Skipping episode - Invalid TMDB mapping."
                     << "TMDB Season:" << tmdbSeason << "TMDB Episode:" << tmdbEpisode;
            failedCount++;
            continue;
        }
        
        // Get episode info from TMDB with rate limit handling
        VP_ShowsTMDB::EpisodeInfo episodeInfo;
        bool foundEpisode = false;
        int retryCount = 0;
        
        do {
            // Try to get episode info using mapped values for absolute numbering
            if (tmdbApi.getEpisodeInfo(currentShowTmdbId, tmdbSeason, tmdbEpisode, episodeInfo)) {
                foundEpisode = true;
                break;
            }

            // API call failed, assume rate limit and retry
            // The VP_ShowsTMDB class logs rate limit errors
            retryCount++;
            qDebug() << "Operations_VP_Shows: API call failed for episode, retry" << retryCount;

            // Show rate limit message and wait
            progressDialog->showRateLimitMessage(1);
            QThread::sleep(1); // Wait 1 second before retry
            QApplication::processEvents();

            // Check if dialog was closed during wait or operation cancelled
            if (!progressDialog->isVisible() || operationCancelled) {
                operationCancelled = true;
                break;
            }
        } while (!foundEpisode && !operationCancelled);
        
        if (!foundEpisode) {
            failedCount++;
            continue;
        }
        
        // Update metadata with TMDB info
        metadata.EPName = episodeInfo.episodeName;
        metadata.EPDescription = episodeInfo.overview;
        metadata.airDate = episodeInfo.airDate;
        
        // Download and update episode image if available
        if (!episodeInfo.stillPath.isEmpty()) {
            QString tempDir = VP_ShowsConfig::getTempDirectory(m_mainWindow->user_Username);
            if (!tempDir.isEmpty()) {
                QString tempImagePath = QDir(tempDir).absoluteFilePath(QString("tmdb_episode_%1.jpg").arg(i));
                if (tmdbApi.downloadImage(episodeInfo.stillPath, tempImagePath)) {
                    QFile imageFile(tempImagePath);
                    if (imageFile.open(QIODevice::ReadOnly)) {
                        QByteArray imageData = imageFile.readAll();
                        imageFile.close();
                        
                        // Scale the image to 128x128 to reduce size
                        QByteArray scaledImage = VP_ShowsTMDB::scaleImageToSize(imageData, 128, 128);
                        
                        // Only store if scaled image is within size limit (32KB)
                        if (!scaledImage.isEmpty() && scaledImage.size() <= VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
                            metadata.EPImage = scaledImage;
                            qDebug() << "Operations_VP_Shows: Added scaled episode thumbnail (" 
                                     << scaledImage.size() << "bytes) for episode" << i;
                        } else {
                            qDebug() << "Operations_VP_Shows: Episode image too large even after scaling for episode" << i
                                     << scaledImage.size() << "bytes (max:" << VP_ShowsMetadata::MAX_EP_IMAGE_SIZE << ")";
                        }
                    }
                    // Clean up temp file
                    OperationsFiles::secureDelete(tempImagePath, 1, false);
                }
            }
        }
        
        // Save updated metadata
        if (metadataManager.writeMetadataToFile(videoFilePath, metadata)) {
            successCount++;
        } else {
            failedCount++;
        }
        
        // Small delay to avoid hitting rate limits
        QThread::msleep(100);
        QApplication::processEvents();
        
        // Check if dialog was closed
        if (!progressDialog->isVisible()) {
            operationCancelled = true;
            break;
        }
    }
    
    // Check if operation was truly cancelled (not just completed)
    int processedCount = successCount + failedCount;
    bool wasActuallyCancelled = operationCancelled && (processedCount < videoFilePaths.size());
    
    // Close progress dialog
    if (progressDialog->isVisible()) {
        progressDialog->close();
    }
    delete progressDialog;
    
    // Show summary
    QString summary = tr("TMDB data re-acquisition completed.\n\n"
                        "Successful: %1\n"
                        "Failed: %2")
                     .arg(successCount)
                     .arg(failedCount);
    
    if (wasActuallyCancelled) {
        summary += tr("\n\nOperation was cancelled by user.");
    }
    
    QMessageBox::information(m_mainWindow, tr("Re-acquisition Complete"), summary);
    
    qDebug() << "Operations_VP_Shows: TMDB reacquisition finished. Success:" << successCount << "Failed:" << failedCount;
}

void Operations_VP_Shows::deleteEpisodeFromContextMenu()
{
    qDebug() << "Operations_VP_Shows: Delete episodes from context menu";
    qDebug() << "Operations_VP_Shows: Episodes to delete:" << m_contextMenuEpisodePaths.size();
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episodes to delete";
        return;
    }
    
    // Check MainWindow validity
    if (!m_mainWindow) {
        qDebug() << "Operations_VP_Shows: MainWindow is null";
        return;
    }
    
    // Build description for deletion BEFORE any operations that might refresh tree
    QString description;
    if (m_contextMenuTreeItem) {  // Check pointer validity
        if (m_contextMenuTreeItem->childCount() == 0) {
            // Single episode
            description = m_contextMenuTreeItem->text(0);
        } else if (m_contextMenuTreeItem->parent() == nullptr) {
            // Language/Translation level
            description = m_contextMenuTreeItem->text(0);
        } else {
            // Season level
            QString language = m_contextMenuTreeItem->parent()->text(0);
            description = QString("%1 - %2").arg(language).arg(m_contextMenuTreeItem->text(0));
        }
    }
    
    // Delete episodes with confirmation
    if (deleteEpisodesWithConfirmation(m_contextMenuEpisodePaths, description)) {
        // Check if we need to go back to the shows list
        // This happens if we deleted all episodes of the show
        
        // Get all video files in the current show folder to see if any remain
        if (!m_currentShowFolder.isEmpty()) {
            QDir showDir(m_currentShowFolder);
            QStringList videoExtensions;
            videoExtensions << "*.mmvid"; // Custom extension for encrypted video files
            showDir.setNameFilters(videoExtensions);
            QStringList remainingVideos = showDir.entryList(QDir::Files);
            
            if (remainingVideos.isEmpty()) {
                // No episodes left, delete the entire show
                qDebug() << "Operations_VP_Shows: No episodes left, deleting entire show";
                
                // Get the show name
                QString showName;
                if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->label_VP_Shows_Display_Name) {
                    showName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
                }
                
                // Delete the show folder
                if (!showDir.removeRecursively()) {
                    qDebug() << "Operations_VP_Shows: Failed to remove empty show directory";
                }
                
                // Go back to the shows list
                if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->stackedWidget_VP_Shows) {
                    m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(0);
                }
                
                // Refresh the shows list
                refreshTVShowsList();
                
                if (!showName.isEmpty()) {
                    // Success dialog removed - absence of error dialog indicates success
                    // QMessageBox::information(m_mainWindow,
                    //                        tr("Show Deleted"),
                    //                        tr("All episodes have been deleted. The show \"%1\" has been removed from your library.").arg(showName));
                }
            } else {
                // Some episodes remain, just refresh the episode list
                loadShowEpisodes(m_currentShowFolder);

            }
        }
    }
}

bool Operations_VP_Shows::deleteEpisodesWithConfirmation(const QStringList& episodePaths, const QString& description)
{
    if (episodePaths.isEmpty()) {
        return false;
    }
    
    int episodeCount = episodePaths.size();
    
    // First confirmation dialog
    QString firstMessage;
    if (episodeCount == 1) {
        firstMessage = tr("You are about to delete the episode \"%1\" from your library.\n\n"
                         "Are you sure that you want to proceed?").arg(description);
    } else {
        firstMessage = tr("You are about to delete %1 episodes (%2) from your library.\n\n"
                         "Are you sure that you want to proceed?")
                       .arg(episodeCount)
                       .arg(description);
    }
    
    int firstResult = QMessageBox::question(m_mainWindow,
                                           tr("Delete Episode%1").arg(episodeCount > 1 ? "s" : ""),
                                           firstMessage,
                                           QMessageBox::No | QMessageBox::Yes,
                                           QMessageBox::No);
    
    if (firstResult != QMessageBox::Yes) {
        qDebug() << "Operations_VP_Shows: Deletion cancelled at first confirmation";
        return false;
    }
    
    // Second confirmation dialog
    QString secondMessage;
    if (episodeCount == 1) {
        secondMessage = tr("Are you really sure you want to delete \"%1\"?\n\n"
                          "This action cannot be undone.").arg(description);
    } else {
        secondMessage = tr("Are you really sure you want to delete %1 episodes?\n\n"
                          "This action cannot be undone.").arg(episodeCount);
    }
    
    QMessageBox secondConfirm(m_mainWindow);
    secondConfirm.setWindowTitle(tr("Final Confirmation"));
    secondConfirm.setText(secondMessage);
    secondConfirm.setIcon(QMessageBox::Warning);
    
    QPushButton* deleteButton = secondConfirm.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    QPushButton* noButton = secondConfirm.addButton(tr("No"), QMessageBox::RejectRole);
    
    secondConfirm.setDefaultButton(noButton);
    secondConfirm.exec();
    
    if (secondConfirm.clickedButton() != deleteButton) {
        qDebug() << "Operations_VP_Shows: Deletion cancelled at second confirmation";
        return false;
    }
    
    qDebug() << "Operations_VP_Shows: User confirmed deletion, proceeding to delete" << episodeCount << "episode(s)";
    
    // Delete the episodes
    bool allDeleted = true;
    int deletedCount = 0;
    
    for (const QString& episodePath : episodePaths) {
        // Use regular delete for encrypted files (no need for secure deletion)
        if (QFile::remove(episodePath)) {
            deletedCount++;
            qDebug() << "Operations_VP_Shows: Successfully deleted episode:" << episodePath;
        } else {
            qDebug() << "Operations_VP_Shows: Failed to delete episode:" << episodePath;
            allDeleted = false;
        }
    }
    
    if (allDeleted) {
        // Success dialog removed - absence of error dialog indicates success
        // QMessageBox::information(m_mainWindow,
        //                        tr("Episodes Deleted"),
        //                        tr("%1 episode%2 deleted successfully.")
        //                        .arg(deletedCount)
        //                        .arg(deletedCount > 1 ? "s have been" : " has been"));
    } else {
        QMessageBox::warning(m_mainWindow,
                           tr("Partial Deletion"),
                           tr("Some episodes could not be deleted. %1 out of %2 episode%3 deleted.")
                           .arg(deletedCount)
                           .arg(episodeCount)
                           .arg(episodeCount > 1 ? "s were" : " was"));
    }
    
    return deletedCount > 0;
}

// ============================================================================
// Autoplay Functionality Implementation
// ============================================================================

QStringList Operations_VP_Shows::getAllAvailableEpisodes() const
{
    qDebug() << "Operations_VP_Shows: Building list of all available episodes";
    
    QStringList allEpisodes;
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Tree widget not available";
        return allEpisodes;
    }
    
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    
    // Iterate through all language versions (top-level items) with bounds checking
    int topLevelCount = safeGetTreeItemCount(treeWidget);
    for (int langIndex = 0; langIndex < topLevelCount; ++langIndex) {
        QTreeWidgetItem* languageItem = safeGetTreeItem(treeWidget, langIndex);
        if (!languageItem) continue;
        
        // Skip if this is an error category
        if (languageItem->text(0).contains("Error - Duplicate Episodes")) {
            continue;
        }
        
        // Iterate through all children of language item
        for (int seasonIndex = 0; seasonIndex < languageItem->childCount(); ++seasonIndex) {
            QTreeWidgetItem* seasonItem = languageItem->child(seasonIndex);
            
            // Skip special categories (Movies, OVA, Extra, Error)
            QString categoryText = seasonItem->text(0);
            if (categoryText.contains("Error - Duplicate Episodes") ||
                categoryText.startsWith("Movies") ||
                categoryText.startsWith("OVA") ||
                categoryText.startsWith("Extra")) {
                continue;
            }
            
            // Iterate through all episodes in this season
            for (int epIndex = 0; epIndex < seasonItem->childCount(); ++epIndex) {
                QTreeWidgetItem* episodeItem = seasonItem->child(epIndex);
                
                // Get the file path from the episode item
                QString episodePath = episodeItem->data(0, Qt::UserRole).toString();
                if (!episodePath.isEmpty()) {
                    allEpisodes.append(episodePath);
                }
            }
        }
    }
    
    qDebug() << "Operations_VP_Shows: Found" << allEpisodes.size() << "total episodes";
    return allEpisodes;
}

QString Operations_VP_Shows::findNextEpisode(const QString& currentEpisodePath) const
{
    qDebug() << "Operations_VP_Shows: Finding next episode after:" << currentEpisodePath;
    
    if (currentEpisodePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Current episode path is empty";
        return QString();
    }
    
    // Get metadata for the current episode to determine language/translation
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    VP_ShowsMetadata::ShowMetadata currentMetadata;
    
    QString currentLanguage;
    QString currentTranslation;
    
    if (metadataManager.readMetadataFromFile(currentEpisodePath, currentMetadata)) {
        currentLanguage = currentMetadata.language;
        currentTranslation = currentMetadata.translation;
        qDebug() << "Operations_VP_Shows: Current episode language:" << currentLanguage 
                 << "Translation:" << currentTranslation;
    } else {
        qDebug() << "Operations_VP_Shows: Could not read metadata for current episode";
    }
    
    QString nextEpisode;
    QString nextEpisodeSameLanguage;
    QString nextEpisodeAnyLanguage;
    bool foundCurrent = false;
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Tree widget not available";
        return QString();
    }
    
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    
    // Iterate through all language versions
    for (int langIndex = 0; langIndex < treeWidget->topLevelItemCount(); ++langIndex) {
        QTreeWidgetItem* languageItem = treeWidget->topLevelItem(langIndex);
        
        // Skip error categories
        if (languageItem->text(0).contains("Error - Duplicate Episodes")) {
            continue;
        }
        
        QString languageKey = languageItem->text(0); // e.g., "English Dubbed"
        bool isSameLanguage = !currentLanguage.isEmpty() && 
                              !currentTranslation.isEmpty() && 
                              languageKey == QString("%1 %2").arg(currentLanguage).arg(currentTranslation);
        
        // Iterate through all seasons
        for (int seasonIndex = 0; seasonIndex < languageItem->childCount(); ++seasonIndex) {
            QTreeWidgetItem* seasonItem = languageItem->child(seasonIndex);
            
            // Skip error categories
            if (seasonItem->text(0).contains("Error - Duplicate Episodes")) {
                continue;
            }
            
            // Iterate through all episodes
            for (int epIndex = 0; epIndex < seasonItem->childCount(); ++epIndex) {
                QTreeWidgetItem* episodeItem = seasonItem->child(epIndex);
                QString episodePath = episodeItem->data(0, Qt::UserRole).toString();
                
                if (episodePath.isEmpty()) {
                    continue;
                }
                
                // Check if this is the current episode
                if (episodePath == currentEpisodePath) {
                    foundCurrent = true;
                    qDebug() << "Operations_VP_Shows: Found current episode in tree";
                    continue; // Skip the current episode
                }
                
                // If we've found the current episode, look for the next one
                if (foundCurrent) {
                    // Get relative path for checking completion status
                    QDir showDir(m_currentShowFolder);
                    QString relativeEpisodePath = showDir.relativeFilePath(episodePath);
                    
                    // Check if this episode has been completed
                    if (m_playbackTracker && m_playbackTracker->isEpisodeCompleted(relativeEpisodePath)) {
                        qDebug() << "Operations_VP_Shows: Skipping completed episode:" << episodePath;
                        continue; // Skip completed episodes
                    }
                    
                    // Prioritize same language/translation
                    if (isSameLanguage && nextEpisodeSameLanguage.isEmpty()) {
                        nextEpisodeSameLanguage = episodePath;
                        qDebug() << "Operations_VP_Shows: Found next episode in same language:" << episodePath;
                        // Continue searching in case we find a better match
                    }
                    
                    // Store as fallback if different language
                    if (!isSameLanguage && nextEpisodeAnyLanguage.isEmpty()) {
                        nextEpisodeAnyLanguage = episodePath;
                        qDebug() << "Operations_VP_Shows: Found next episode in different language:" << episodePath;
                    }
                    
                    // If we found a same-language episode and we're past the current episode's position,
                    // we can return it immediately
                    if (!nextEpisodeSameLanguage.isEmpty() && isSameLanguage) {
                        return nextEpisodeSameLanguage;
                    }
                }
            }
        }
    }
    
    // Return the best match we found
    if (!nextEpisodeSameLanguage.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Returning next episode in same language";
        return nextEpisodeSameLanguage;
    } else if (!nextEpisodeAnyLanguage.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Returning next episode in different language";
        return nextEpisodeAnyLanguage;
    }
    
    qDebug() << "Operations_VP_Shows: No next episode found";
    return QString();
}

QString Operations_VP_Shows::findRandomEpisode()
{
    qDebug() << "Operations_VP_Shows: Finding random episode for autoplay";
    
    if (!m_watchHistory) {
        qDebug() << "Operations_VP_Shows: No watch history available for random episode selection";
        return QString();
    }
    
    if (m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No current show folder set";
        return QString();
    }
    
    // Get all available episodes
    QStringList allEpisodes = getAllAvailableEpisodes();
    if (allEpisodes.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episodes available";
        return QString();
    }
    
    qDebug() << "Operations_VP_Shows: Total episodes found:" << allEpisodes.size();
    
    QDir showDir(m_currentShowFolder);
    QStringList candidateEpisodes;
    
    // Step 1: Try to find unwatched episodes (not marked as completed)
    qDebug() << "Operations_VP_Shows: Step 1 - Looking for unwatched episodes";
    for (const QString& episodePath : allEpisodes) {
        QString relativePath = showDir.relativeFilePath(episodePath);
        
        // Check if episode is completed (marked as watched)
        if (!m_watchHistory->isEpisodeCompleted(relativePath)) {
            candidateEpisodes.append(episodePath);
            qDebug() << "Operations_VP_Shows: Found unwatched episode:" << QFileInfo(episodePath).fileName();
        }
    }
    
    qDebug() << "Operations_VP_Shows: Found" << candidateEpisodes.size() << "unwatched episodes";
    
    // Step 2: If no unwatched episodes, find episodes with playback position = 0
    if (candidateEpisodes.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Step 2 - No unwatched episodes, looking for episodes with position = 0";
        for (const QString& episodePath : allEpisodes) {
            QString relativePath = showDir.relativeFilePath(episodePath);
            
            // Check if episode has playback position of 0
            qint64 resumePosition = m_watchHistory->getResumePosition(relativePath);
            if (resumePosition == 0) {
                candidateEpisodes.append(episodePath);
                qDebug() << "Operations_VP_Shows: Found episode with position = 0:" << QFileInfo(episodePath).fileName();
            }
        }
        
        qDebug() << "Operations_VP_Shows: Found" << candidateEpisodes.size() << "episodes with position = 0";
    }
    
    // Step 3: If still no candidates, use all episodes
    if (candidateEpisodes.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Step 3 - No episodes with position = 0, using all episodes";
        candidateEpisodes = allEpisodes;
        qDebug() << "Operations_VP_Shows: Using all" << candidateEpisodes.size() << "episodes as candidates";
    }
    
    // Select random episode from candidates
    if (candidateEpisodes.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No candidate episodes found";
        return QString();
    }
    
    // Use QRandomGenerator for random selection
    int randomIndex = QRandomGenerator::global()->bounded(candidateEpisodes.size());
    QString selectedEpisode = candidateEpisodes[randomIndex];
    
    qDebug() << "Operations_VP_Shows: Selected random episode (" << (randomIndex + 1) 
             << "/" << candidateEpisodes.size() << "):" << QFileInfo(selectedEpisode).fileName();
    
    return selectedEpisode;
}

void Operations_VP_Shows::autoplayNextEpisode()
{
    qDebug() << "Operations_VP_Shows: Autoplay triggered";
    qDebug() << "Operations_VP_Shows: Current state:";
    qDebug() << "Operations_VP_Shows:   - m_episodePlayer valid:" << (m_episodePlayer != nullptr);
    qDebug() << "Operations_VP_Shows:   - m_isAutoplayInProgress:" << m_isAutoplayInProgress;
    qDebug() << "Operations_VP_Shows:   - m_episodeWasNearCompletion:" << m_episodeWasNearCompletion;
    
    // Check if autoplay is enabled
    if (!m_currentShowSettings.autoplay) {
        qDebug() << "Operations_VP_Shows: Autoplay is disabled in settings";
        m_episodeWasNearCompletion = false;  // Reset flag
        return;
    }
    
    // Check if we're already processing an autoplay to prevent multiple triggers
    if (m_isAutoplayInProgress) {
        qDebug() << "Operations_VP_Shows: Autoplay already in progress, skipping";
        return;
    }
    
    // Check if we have a current episode path
    if (m_currentPlayingEpisodePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No current episode path stored";
        m_episodeWasNearCompletion = false;  // Reset flag
        return;
    }
    
    // Validate that we're in a valid state for autoplay
    if (!m_mainWindow) {
        qDebug() << "Critical-Operations_VP_Shows: MainWindow is null in autoplayNextEpisode";
        m_episodeWasNearCompletion = false;
        return;
    }
    
    // Find the next episode (random if enabled, sequential otherwise)
    QString nextEpisodePath;
    bool isRandomAutoplay = false;
    if (m_currentShowSettings.autoplayRandom) {
        nextEpisodePath = findRandomEpisode();
        isRandomAutoplay = true;
        qDebug() << "Operations_VP_Shows: Random episode autoplay enabled, selected episode:" << nextEpisodePath;
    } else {
        nextEpisodePath = findNextEpisode(m_currentPlayingEpisodePath);
        qDebug() << "Operations_VP_Shows: Sequential autoplay, next episode:" << nextEpisodePath;
    }
    
    // Store flag for use in decryptAndPlayEpisode
    m_isRandomAutoplay = isRandomAutoplay;
    
    if (nextEpisodePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No next episode available for autoplay";
        // Reset flags since we're not proceeding with autoplay
        m_isAutoplayInProgress = false;
        m_isRandomAutoplay = false;
        m_episodeWasNearCompletion = false;
        // No dialog shown - autoplay silently ends when no more episodes are available
        return;
    }
    
    // Original dialog code removed - autoplay ends silently
    
    qDebug() << "Operations_VP_Shows: Autoplaying next episode:" << nextEpisodePath;
    
    // Set the flag to prevent multiple autoplay triggers
    m_isAutoplayInProgress = true;
    
    // Reset the near-completion flag for consistency (not used for autoplay decision anymore)
    m_episodeWasNearCompletion = false;
    
    // Get episode name from the tree widget for display purposes
    QString episodeName;
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    
    // Find the episode item in the tree to get its display name
    for (int langIndex = 0; langIndex < treeWidget->topLevelItemCount(); ++langIndex) {
        QTreeWidgetItem* languageItem = treeWidget->topLevelItem(langIndex);
        for (int seasonIndex = 0; seasonIndex < languageItem->childCount(); ++seasonIndex) {
            QTreeWidgetItem* seasonItem = languageItem->child(seasonIndex);
            for (int epIndex = 0; epIndex < seasonItem->childCount(); ++epIndex) {
                QTreeWidgetItem* episodeItem = seasonItem->child(epIndex);
                if (episodeItem->data(0, Qt::UserRole).toString() == nextEpisodePath) {
                    episodeName = episodeItem->text(0);
                    break;
                }
            }
            if (!episodeName.isEmpty()) break;
        }
        if (!episodeName.isEmpty()) break;
    }
    
    if (episodeName.isEmpty()) {
        QFileInfo fileInfo(nextEpisodePath);
        episodeName = fileInfo.fileName();
    }
    
    // Store the pending autoplay information
    m_pendingAutoplayPath = nextEpisodePath;
    m_pendingAutoplayName = episodeName;
    m_pendingAutoplayIsRandom = isRandomAutoplay;
    
    qDebug() << "Operations_VP_Shows: Stored pending autoplay information:";
    qDebug() << "Operations_VP_Shows:   - Path:" << m_pendingAutoplayPath;
    qDebug() << "Operations_VP_Shows:   - Name:" << m_pendingAutoplayName;
    qDebug() << "Operations_VP_Shows:   - Is Random:" << m_pendingAutoplayIsRandom;
    
    // If we have a current player, connect to its destroyed signal and close it
    if (m_episodePlayer) {
        qDebug() << "Operations_VP_Shows: Connecting to player's destroyed signal for autoplay";
        
        // Connect to the destroyed signal (use unique connection to prevent duplicates)
        connect(m_episodePlayer.get(), &QObject::destroyed, 
                this, &Operations_VP_Shows::onPlayerDestroyedDuringAutoplay,
                Qt::UniqueConnection);
        
        // Stop playback tracking
        if (m_playbackTracker && m_playbackTracker->isTracking()) {
            qDebug() << "Operations_VP_Shows: Stopping playback tracking before closing player";
            m_playbackTracker->stopTracking();
        }
        
        // Force release the video file
        forceReleaseVideoFile();
        
        // Close the player - this will trigger the destroyed signal
        qDebug() << "Operations_VP_Shows: Closing current player to trigger autoplay";
        if (m_episodePlayer->isVisible()) {
            m_episodePlayer->close();
        }
        m_episodePlayer.reset();  // This will destroy the player and trigger the signal
    } else {
        // No current player, directly play the next episode
        qDebug() << "Operations_VP_Shows: No current player, directly playing next episode";
        decryptAndPlayEpisode(m_pendingAutoplayPath, m_pendingAutoplayName);
        
        // Clear pending autoplay info after playing
        m_pendingAutoplayPath.clear();
        m_pendingAutoplayName.clear();
        m_pendingAutoplayIsRandom = false;
    }
    
    qDebug() << "Operations_VP_Shows: Autoplay setup completed";
}

void Operations_VP_Shows::onPlayerDestroyedDuringAutoplay()
{
    qDebug() << "Operations_VP_Shows: Player destroyed signal received during autoplay";
    
    // Check if we have pending autoplay info
    if (m_pendingAutoplayPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No pending autoplay path, nothing to do";
        return;
    }
    
    // Check if we're still in autoplay mode
    if (!m_isAutoplayInProgress) {
        qDebug() << "Operations_VP_Shows: Autoplay was cancelled, clearing pending info";
        m_pendingAutoplayPath.clear();
        m_pendingAutoplayName.clear();
        m_pendingAutoplayIsRandom = false;
        return;
    }
    
    // Validate MainWindow is still valid
    if (!m_mainWindow) {
        qDebug() << "Critical-Operations_VP_Shows: MainWindow is null after player destruction";
        m_isAutoplayInProgress = false;
        m_isRandomAutoplay = false;
        m_episodeWasNearCompletion = false;
        m_pendingAutoplayPath.clear();
        m_pendingAutoplayName.clear();
        m_pendingAutoplayIsRandom = false;
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Player destroyed - proceeding with autoplay";
    qDebug() << "Operations_VP_Shows:   Next episode path:" << m_pendingAutoplayPath;
    qDebug() << "Operations_VP_Shows:   Episode name:" << m_pendingAutoplayName;
    qDebug() << "Operations_VP_Shows:   Is random:" << m_pendingAutoplayIsRandom;
    
    // Store the pending random flag in the member variable used by decryptAndPlayEpisode
    m_isRandomAutoplay = m_pendingAutoplayIsRandom;
    
    // Clean up temp file from previous playback
    cleanupTempFile();
    
    // Small delay to ensure everything is cleaned up
    QTimer::singleShot(100, this, [this]() {
        if (!m_pendingAutoplayPath.isEmpty() && m_isAutoplayInProgress) {
            // Play the next episode
            QString path = m_pendingAutoplayPath;
            QString name = m_pendingAutoplayName;
            
            // Clear pending info before playing
            m_pendingAutoplayPath.clear();
            m_pendingAutoplayName.clear();
            m_pendingAutoplayIsRandom = false;
            
            // Decrypt and play the next episode
            decryptAndPlayEpisode(path, name);
        }
    });
}

void Operations_VP_Shows::handleEpisodeNearCompletion(const QString& episodePath)
{
    qDebug() << "Operations_VP_Shows: Episode near completion:" << episodePath;
    
    // Get relative path for comparison
    QDir showDir(m_currentShowFolder);
    QString relativeEpisodePath = showDir.relativeFilePath(episodePath);
    QString currentRelativePath = showDir.relativeFilePath(m_currentPlayingEpisodePath);
    
    // Check if this is the currently playing episode
    if (relativeEpisodePath != currentRelativePath) {
        qDebug() << "Operations_VP_Shows: Episode path mismatch, not triggering autoplay";
        return;
    }
    
    // Check if autoplay is enabled
    if (!m_currentShowSettings.autoplay) {
        qDebug() << "Operations_VP_Shows: Autoplay is disabled, not proceeding";
        return;
    }
    
    // Check if we're already processing an autoplay
    if (m_isAutoplayInProgress) {
        qDebug() << "Operations_VP_Shows: Autoplay already in progress";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Preparing for autoplay...";
    
    // The actual autoplay will be triggered when the player stops
    // This is just a notification that we're near the end
}

void Operations_VP_Shows::performEpisodeExportWithWorker(const QStringList& episodePaths, const QString& exportPath, const QString& description, bool createFolderStructure)
{
    qDebug() << "Operations_VP_Shows: Preparing episode export with worker";
    qDebug() << "Operations_VP_Shows: Episodes to export:" << episodePaths.size();
    qDebug() << "Operations_VP_Shows: Create folder structure:" << createFolderStructure;
    
    // Get the show name
    QString showName;
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->label_VP_Shows_Display_Name) {
        showName = m_mainWindow->ui->label_VP_Shows_Display_Name->text();
    }
    if (showName.isEmpty()) {
        showName = "TV Show";
    }
    
    // Build the list of export file info
    QList<VP_ShowsExportWorker::ExportFileInfo> exportFiles;
    
    // Create metadata manager to read episode info
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Setup export directory based on createFolderStructure flag
    QDir exportDir(exportPath);
    QString baseExportPath = exportPath;  // Store the base export path
    
    if (createFolderStructure) {
        // Create show folder in export path when folder structure is requested
        QString showFolderName = showName;
        
        // Sanitize the show name for use as a folder name
        showFolderName.replace(QRegularExpression("[<>:\"|?*]"), "_");
    
        if (!exportDir.mkdir(showFolderName)) {
            // Folder might already exist, try to use it
            qDebug() << "Operations_VP_Shows: Show folder already exists or couldn't be created";
        }

        baseExportPath = exportDir.absoluteFilePath(showFolderName);
    }
    // When createFolderStructure is false, we export directly to the provided path

    QDir showExportDir(baseExportPath);

    // Process each episode
    for (const QString& episodePath : episodePaths) {
        // Read metadata to determine output path and filename
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(episodePath, metadata)) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from:" << episodePath;
            continue;
        }

        // Parse season and episode numbers
        int seasonNum = metadata.season.toInt();
        int episodeNum = metadata.episode.toInt();
        if (seasonNum <= 0 || episodeNum <= 0) {
            // Try to parse from filename as fallback
            VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum);
            if (seasonNum <= 0) seasonNum = 1;
        }

        QString outputFileName;
        QString outputFilePath;

        if (createFolderStructure) {
            // Create full folder structure (language/season)
            QString languageFolderName = QString("%1 %2").arg(metadata.language).arg(metadata.translation);
            languageFolderName.replace(QRegularExpression("[<>:\"|?*]"), "_");

            if (!showExportDir.exists(languageFolderName)) {
                if (!showExportDir.mkdir(languageFolderName)) {
                    qDebug() << "Operations_VP_Shows: Failed to create language folder:" << languageFolderName;
                    continue;
                }
            }

            QString languagePath = showExportDir.absoluteFilePath(languageFolderName);
            QDir languageDir(languagePath);

            // Check if using absolute numbering
            QString episodeFolderPath;
            if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
                // For absolute numbering, create "Episodes" folder instead of season folder
                QString episodesFolderName = "Episodes";
                if (!languageDir.exists(episodesFolderName)) {
                    if (!languageDir.mkdir(episodesFolderName)) {
                        qDebug() << "Operations_VP_Shows: Failed to create episodes folder:" << episodesFolderName;
                        continue;
                    }
                }
                episodeFolderPath = languageDir.absoluteFilePath(episodesFolderName);
            } else {
                // Traditional season folder structure
                QString seasonFolderName = QString("Season %1").arg(seasonNum, 2, 10, QChar('0'));
                if (!languageDir.exists(seasonFolderName)) {
                    if (!languageDir.mkdir(seasonFolderName)) {
                        qDebug() << "Operations_VP_Shows: Failed to create season folder:" << seasonFolderName;
                        continue;
                    }
                }
                episodeFolderPath = languageDir.absoluteFilePath(seasonFolderName);
            }

            // Generate output filename for folder structure
            if (episodeNum > 0) {
                if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
                    outputFileName = QString("%1_E%2")
                                   .arg(showName)
                                   .arg(episodeNum, 3, 10, QChar('0'));
                } else {
                    outputFileName = QString("%1_S%2E%3")
                                   .arg(showName)
                                   .arg(seasonNum, 2, 10, QChar('0'))
                                   .arg(episodeNum, 2, 10, QChar('0'));
                }

                if (!metadata.EPName.isEmpty()) {
                    outputFileName += "_" + metadata.EPName;
                }
            } else {
                QFileInfo fileInfo(metadata.filename);
                outputFileName = fileInfo.completeBaseName();
            }

            // Sanitize filename
            outputFileName.replace(QRegularExpression("[<>:\"|?*]"), "_");

            // Add extension
            QString originalExtension;
            if (!metadata.filename.isEmpty()) {
                QFileInfo originalFileInfo(metadata.filename);
                originalExtension = originalFileInfo.suffix();
            }
            if (!originalExtension.isEmpty()) {
                outputFileName += "." + originalExtension;
            } else {
                outputFileName += ".mp4";
            }

            outputFilePath = QDir(episodeFolderPath).absoluteFilePath(outputFileName);

        } else {
            // Export directly to the selected folder without creating subfolders
            if (episodeNum > 0) {
                if (metadata.isAbsoluteNumbering() || seasonNum == 0) {
                    outputFileName = QString("%1_E%2")
                                   .arg(showName)
                                   .arg(episodeNum, 3, 10, QChar('0'));
                } else {
                    outputFileName = QString("%1_S%2E%3")
                                   .arg(showName)
                                   .arg(seasonNum, 2, 10, QChar('0'))
                                   .arg(episodeNum, 2, 10, QChar('0'));
                }

                if (!metadata.EPName.isEmpty()) {
                    outputFileName += "_" + metadata.EPName;
                }
            } else {
                QFileInfo fileInfo(metadata.filename);
                outputFileName = fileInfo.completeBaseName();
            }

            // Sanitize filename
            outputFileName.replace(QRegularExpression("[<>:\"|?*]"), "_");

            // Add extension
            QString originalExtension;
            if (!metadata.filename.isEmpty()) {
                QFileInfo originalFileInfo(metadata.filename);
                originalExtension = originalFileInfo.suffix();
            }
            if (!originalExtension.isEmpty()) {
                outputFileName += "." + originalExtension;
            } else {
                outputFileName += ".mp4";
            }

            // Export directly to baseExportPath
            outputFilePath = QDir(baseExportPath).absoluteFilePath(outputFileName);
        }

        // Create export info
        VP_ShowsExportWorker::ExportFileInfo fileInfo;
        fileInfo.sourceFile = episodePath;
        fileInfo.targetFile = outputFilePath;
        fileInfo.displayName = outputFileName;
        fileInfo.fileSize = QFileInfo(episodePath).size();

        exportFiles.append(fileInfo);
    }
    
    
    if (exportFiles.isEmpty()) {
        QMessageBox::warning(m_mainWindow,
                           tr("Export Error"),
                           tr("No valid episodes found to export."));
        return;
    }
    
    // Create and show export progress dialog
    VP_ShowsExportProgressDialog* exportDialog = new VP_ShowsExportProgressDialog(m_mainWindow);
    
    // Connect completion signal
    connect(exportDialog, &VP_ShowsExportProgressDialog::exportComplete,
            this, [this, exportDialog, description](bool success, const QString& message,
                                                   const QStringList& successfulFiles,
                                                   const QStringList& failedFiles) {
        qDebug() << "Operations_VP_Shows: Episode export complete. Success:" << success;
        
        if (success) {
            // Success dialog removed - absence of error dialog indicates success
            // QMessageBox::information(m_mainWindow,
            //                        tr("Export Complete"),
            //                        tr("Successfully exported %1 episode%2.")
            //                        .arg(successfulFiles.size())
            //                        .arg(successfulFiles.size() > 1 ? "s" : ""));
        } else {
            QString detailedMessage = message;
            if (!failedFiles.isEmpty()) {
                detailedMessage += tr("\n\nFailed files: %1").arg(failedFiles.size());
            }
            QMessageBox::warning(m_mainWindow,
                               tr("Export Failed"),
                               detailedMessage);
        }
        
        // Clean up the dialog
        exportDialog->deleteLater();
    });
    
    // Start the export
    exportDialog->startExport(exportFiles, m_mainWindow->user_Key, m_mainWindow->user_Username, showName);
}

// =================
// HELPER FUNCTIONS
// =================

// Helper function to determine the watch state of an item (episode or category)
Operations_VP_Shows::WatchState Operations_VP_Shows::getItemWatchState(QTreeWidgetItem* item)
{
    if (!item || !m_watchHistory) {
        return WatchState::NotWatched;
    }

    // If it's an episode (no children)
    if (item->childCount() == 0) {
        QString videoPath = item->data(0, Qt::UserRole).toString();
        if (!videoPath.isEmpty()) {
            // Convert absolute path to relative path for watch history
            QDir showDir(m_currentShowFolder);
            QString relativePath = showDir.relativeFilePath(videoPath);

            if (m_watchHistory->isEpisodeCompleted(relativePath)) {
                return WatchState::Watched;
            }
        }
        return WatchState::NotWatched;
    }

    // It's a category (has children) - check all episodes under it
    int watchedCount = 0;
    int totalCount = 0;
    countWatchedEpisodes(item, watchedCount, totalCount);

    if (totalCount == 0) {
        return WatchState::NotWatched;
    } else if (watchedCount == 0) {
        return WatchState::NotWatched;
    } else if (watchedCount == totalCount) {
        return WatchState::Watched;
    } else {
        return WatchState::PartiallyWatched;
    }
}

// Helper function to count watched episodes under a tree item
void Operations_VP_Shows::countWatchedEpisodes(QTreeWidgetItem* item, int& watchedCount, int& totalCount)
{
    if (!item || !m_watchHistory) return;

    // If this is an episode (no children)
    if (item->childCount() == 0) {
        QString videoPath = item->data(0, Qt::UserRole).toString();
        if (!videoPath.isEmpty()) {
            totalCount++;

            // Convert absolute path to relative path for watch history
            QDir showDir(m_currentShowFolder);
            QString relativePath = showDir.relativeFilePath(videoPath);

            if (m_watchHistory->isEpisodeCompleted(relativePath)) {
                watchedCount++;
            }
        }
    } else {
        // This is a category, recursively count all child episodes
        for (int i = 0; i < item->childCount(); ++i) {
            countWatchedEpisodes(item->child(i), watchedCount, totalCount);
        }
    }
}

// Helper function to set watched state for a list of episode paths
void Operations_VP_Shows::setWatchedStateForEpisodes(const QStringList& episodePaths, bool watched)
{
    if (!m_watchHistory || episodePaths.isEmpty()) return;

    qDebug() << "Operations_VP_Shows: Setting" << episodePaths.size()
             << "episodes to watched state:" << watched;

    // Convert absolute paths to relative paths
    QDir showDir(m_currentShowFolder);
    QStringList relativePaths;
    relativePaths.reserve(episodePaths.size());
    
    for (const QString& absolutePath : episodePaths) {
        QString relativePath = showDir.relativeFilePath(absolutePath);
        if (!relativePath.isEmpty()) {
            relativePaths.append(relativePath);
        }
    }
    
    // Use batch operation for efficiency and safety
    m_watchHistory->batchSetEpisodesWatched(relativePaths, watched);
    
    // If marking as unwatched, also reset positions
    if (!watched) {
        for (const QString& relativePath : relativePaths) {
            m_watchHistory->resetEpisodePosition(relativePath);
        }
    }
    
    // Update last watched episode if necessary
    if (!watched) {
        QString currentLastWatched = m_watchHistory->getLastWatchedEpisode();
        if (relativePaths.contains(currentLastWatched)) {
            qDebug() << "Operations_VP_Shows: Last watched episode was marked unwatched, finding new one";
            
            // Find the next most recently watched episode
            QString newLastWatched;
            QDateTime latestTime;
            
            // Check all episodes in watch history to find the most recent one that's still watched
            for (const QString& videoFile : m_episodeFileMapping.values()) {
                QString relPath = showDir.relativeFilePath(videoFile);
                // Check if episode is completed (actually watched)
                if (!relativePaths.contains(relPath) && m_watchHistory->isEpisodeCompleted(relPath)) {
                    EpisodeWatchInfo info = m_watchHistory->getEpisodeWatchInfo(relPath);
                    if (info.lastWatched > latestTime) {
                        latestTime = info.lastWatched;
                        newLastWatched = relPath;
                    }
                }
            }
            
            // Update the last watched episode
            if (newLastWatched.isEmpty()) {
                m_watchHistory->clearLastWatchedEpisode();
                qDebug() << "Operations_VP_Shows: No other watched episodes found, cleared last watched";
            } else {
                m_watchHistory->setLastWatchedEpisode(newLastWatched);
                qDebug() << "Operations_VP_Shows: Updated last watched episode to:" << newLastWatched;
            }
        }
    }

    // Save the history with backup
    m_watchHistory->saveHistoryWithBackup();

    // Refresh the tree widget to show updated states
    refreshEpisodeTreeColors();
    
    // Update the Play/Resume button text
    updatePlayButtonText();
}

// Helper function to set watched state for all episodes under an item
void Operations_VP_Shows::setWatchedStateForItem(QTreeWidgetItem* item, bool watched)
{
    if (!item || !m_watchHistory) return;

    QStringList episodePaths;
    collectEpisodesFromTreeItem(item, episodePaths);

    qDebug() << "Operations_VP_Shows: Setting" << episodePaths.size()
             << "episodes to watched state:" << watched;

    // Convert absolute paths to relative paths and update watch history
    QDir showDir(m_currentShowFolder);
    for (const QString& absolutePath : episodePaths) {
        QString relativePath = showDir.relativeFilePath(absolutePath);
        
        if (watched) {
            // Mark as watched
            m_watchHistory->setEpisodeWatched(relativePath, true);
        } else {
            // Mark as unwatched AND reset playback position
            m_watchHistory->setEpisodeWatched(relativePath, false);
            m_watchHistory->resetEpisodePosition(relativePath);
            
            // If this was the last watched episode, update it
            if (m_watchHistory->getLastWatchedEpisode() == relativePath) {
                qDebug() << "Operations_VP_Shows: Clearing last watched episode as it was marked unwatched";
                
                // Find the next most recently watched episode
                QString newLastWatched;
                QDateTime latestTime;
                
                // Check all episodes in watch history to find the most recent one that's still watched
                for (const QString& videoFile : m_episodeFileMapping.values()) {
                    QString relPath = showDir.relativeFilePath(videoFile);
                    // Check if episode is completed (actually watched)
                    if (relPath != relativePath && m_watchHistory->isEpisodeCompleted(relPath)) {
                        EpisodeWatchInfo info = m_watchHistory->getEpisodeWatchInfo(relPath);
                        if (info.lastWatched > latestTime) {
                            latestTime = info.lastWatched;
                            newLastWatched = relPath;
                        }
                    }
                }
                
                // Update the last watched episode
                if (newLastWatched.isEmpty()) {
                    m_watchHistory->clearLastWatchedEpisode();
                    qDebug() << "Operations_VP_Shows: No other watched episodes found, cleared last watched";
                } else {
                    m_watchHistory->setLastWatchedEpisode(newLastWatched);
                    qDebug() << "Operations_VP_Shows: Updated last watched episode to:" << newLastWatched;
                }
            }
        }
    }

    // Save the history with backup after all updates
    if (!episodePaths.isEmpty()) {
        m_watchHistory->saveHistoryWithBackup();
    }

    // Refresh the tree widget to show updated states
    refreshEpisodeTreeColors();
    
    // Update the Play/Resume button text
    updatePlayButtonText();
}

// Helper function to refresh the tree widget colors based on watch states
void Operations_VP_Shows::refreshEpisodeTreeColors()
{
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        return;
    }

    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    QColor watchedColor(128, 128, 128); // Grey color for watched items

    // Process all top-level items (languages)
    for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
        refreshItemColors(treeWidget->topLevelItem(i), watchedColor);
    }
    
    // Expand to show the last watched episode location
    expandToLastWatchedEpisode();
    
    // Update favourite indicators in the tree
    updateFavouriteIndicators();
    
    // Update the Play button text after refreshing episode states
    updatePlayButtonText();
}

// Helper function to recursively refresh colors for an item and its children
void Operations_VP_Shows::refreshItemColors(QTreeWidgetItem* item, const QColor& watchedColor)
{
    if (!item || !m_watchHistory) return;

    // If this is an episode (no children)
    if (item->childCount() == 0) {
        QString videoPath = item->data(0, Qt::UserRole).toString();
        if (!videoPath.isEmpty()) {
            // Convert absolute path to relative path for watch history
            QDir showDir(m_currentShowFolder);
            QString relativePath = showDir.relativeFilePath(videoPath);

            if (m_watchHistory->isEpisodeCompleted(relativePath)) {
                item->setForeground(0, QBrush(watchedColor));
            } else {
                item->setForeground(0, QBrush()); // Reset to default color
            }
        }
    } else {
        // This is a category, process children first
        bool allWatched = true;
        bool hasEpisodes = false;

        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            refreshItemColors(child, watchedColor);

            // Check if this child or its descendants have unwatched episodes
            int watchedCount = 0;
            int totalCount = 0;
            countWatchedEpisodes(child, watchedCount, totalCount);

            if (totalCount > 0) {
                hasEpisodes = true;
                if (watchedCount < totalCount) {
                    allWatched = false;
                }
            }
        }

        // Set category color based on whether all episodes are watched
        if (hasEpisodes && allWatched) {
            item->setForeground(0, QBrush(watchedColor));
        } else {
            item->setForeground(0, QBrush()); // Reset to default color
        }
    }
}

// Helper function to determine which episode should be played
QTreeWidgetItem* Operations_VP_Shows::determineEpisodeToPlay()
{
    qDebug() << "Operations_VP_Shows: Determining episode to play";

    // Check if we have the tree widget and a current show folder
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Tree widget not available";
        return nullptr;
    }

    if (m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No current show folder set";
        return nullptr;
    }

    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;

    // Check if autoplay random is enabled - if so, select a random episode
    if (m_currentShowSettings.autoplayRandom) {
        qDebug() << "Operations_VP_Shows: Autoplay random is enabled, selecting random episode";
        
        // Use the existing findRandomEpisode function to get a random episode
        QString randomEpisodePath = findRandomEpisode();
        
        if (!randomEpisodePath.isEmpty()) {
            // Find this episode in the tree
            std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findEpisodeItem;
            findEpisodeItem = [&findEpisodeItem](QTreeWidgetItem* parent, const QString& episodePath) -> QTreeWidgetItem* {
                for (int i = 0; i < parent->childCount(); ++i) {
                    QTreeWidgetItem* child = parent->child(i);
                    
                    QString itemPath = child->data(0, Qt::UserRole).toString();
                    if (!itemPath.isEmpty()) {
                        QFileInfo itemInfo(itemPath);
                        QFileInfo episodeInfo(episodePath);
                        if (itemInfo.fileName() == episodeInfo.fileName()) {
                            return child;
                        }
                    }
                    
                    if (child->childCount() > 0) {
                        QTreeWidgetItem* found = findEpisodeItem(child, episodePath);
                        if (found) return found;
                    }
                }
                return nullptr;
            };
            
            for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                QTreeWidgetItem* found = findEpisodeItem(treeWidget->topLevelItem(i), randomEpisodePath);
                if (found) {
                    qDebug() << "Operations_VP_Shows: Found random episode in tree:" << found->text(0);
                    return found;
                }
            }
            
            qDebug() << "Operations_VP_Shows: Random episode not found in tree, path:" << randomEpisodePath;
        } else {
            qDebug() << "Operations_VP_Shows: No random episode could be selected";
        }
    }

    // Try to use watch history to find the last watched episode
    QString lastWatchedEpisode;

    if (m_watchHistory) {
        lastWatchedEpisode = m_watchHistory->getLastWatchedEpisode();
        qDebug() << "Operations_VP_Shows: Last watched episode from history:" << lastWatchedEpisode;
    }

    // If we have a last watched episode, try to find it
    if (!lastWatchedEpisode.isEmpty()) {
        // Check if this episode has a resume position
        qint64 resumePosition = m_watchHistory->getResumePosition(lastWatchedEpisode);
        if (resumePosition > 0) {
            qDebug() << "Operations_VP_Shows: Last watched episode has resume position" << resumePosition;

            // Check if the resume position is near the end (within COMPLETION_THRESHOLD_MS)
            // If so, find the next episode based on autoplay random setting
            bool isNearEnd = false;
            EpisodeWatchInfo watchInfo = m_watchHistory->getEpisodeWatchInfo(lastWatchedEpisode);

            if (watchInfo.totalDuration > 0) {
                qint64 remainingTime = watchInfo.totalDuration - resumePosition;
                if (remainingTime <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS) {
                    isNearEnd = true;
                    qDebug() << "Operations_VP_Shows: Resume position is near end (" << remainingTime
                             << "ms remaining of " << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS
                             << "ms threshold)";
                    
                    // If autoplay random is enabled, select a random episode instead of next
                    if (m_currentShowSettings.autoplayRandom) {
                        qDebug() << "Operations_VP_Shows: Autoplay random enabled, selecting random episode instead of next";
                        QString randomEpisodePath = findRandomEpisode();
                        
                        if (!randomEpisodePath.isEmpty()) {
                            // Find this episode in the tree
                            std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findEpisodeItem;
                            findEpisodeItem = [&findEpisodeItem](QTreeWidgetItem* parent, const QString& episodePath) -> QTreeWidgetItem* {
                                for (int i = 0; i < parent->childCount(); ++i) {
                                    QTreeWidgetItem* child = parent->child(i);
                                    
                                    QString itemPath = child->data(0, Qt::UserRole).toString();
                                    if (!itemPath.isEmpty()) {
                                        QFileInfo itemInfo(itemPath);
                                        QFileInfo episodeInfo(episodePath);
                                        if (itemInfo.fileName() == episodeInfo.fileName()) {
                                            return child;
                                        }
                                    }
                                    
                                    if (child->childCount() > 0) {
                                        QTreeWidgetItem* found = findEpisodeItem(child, episodePath);
                                        if (found) return found;
                                    }
                                }
                                return nullptr;
                            };
                            
                            for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                                QTreeWidgetItem* found = findEpisodeItem(treeWidget->topLevelItem(i), randomEpisodePath);
                                if (found) {
                                    qDebug() << "Operations_VP_Shows: Found random episode (near end case) in tree:" << found->text(0);
                                    return found;
                                }
                            }
                        }
                    } else {
                        qDebug() << "Operations_VP_Shows: Will find next episode in sequence";
                    }
                }
            }

            if (!isNearEnd) {
                // Not near end, so we can resume this episode
                // Function to recursively search for an episode by its file path
                std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findEpisodeItem;
                findEpisodeItem = [&findEpisodeItem](QTreeWidgetItem* parent, const QString& episodePath) -> QTreeWidgetItem* {
                    for (int i = 0; i < parent->childCount(); ++i) {
                        QTreeWidgetItem* child = parent->child(i);

                        // Check if this item has the episode path
                        QString itemPath = child->data(0, Qt::UserRole).toString();
                        if (!itemPath.isEmpty()) {
                            // Extract just the filename from both paths for comparison
                            QFileInfo itemInfo(itemPath);
                            QFileInfo episodeInfo(episodePath);
                            if (itemInfo.fileName() == episodeInfo.fileName()) {
                                return child;
                            }
                        }

                        // Recursively search children
                        if (child->childCount() > 0) {
                            QTreeWidgetItem* found = findEpisodeItem(child, episodePath);
                            if (found) return found;
                        }
                    }
                    return nullptr;
                };

                // Search through all top-level items
                for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                    QTreeWidgetItem* found = findEpisodeItem(treeWidget->topLevelItem(i), lastWatchedEpisode);
                    if (found) {
                        qDebug() << "Operations_VP_Shows: Found last watched episode in tree:" << found->text(0);
                        return found;
                    }
                }
            } else {
                // Near end, so find the next episode in sequence (not necessarily unwatched)
                qDebug() << "Operations_VP_Shows: Episode near end, looking for next episode in sequence";

                // Get all available episodes and find the next one in sequence
                QStringList allEpisodes = getAllAvailableEpisodes();
                
                // Find the current episode index and get the next one
                int currentIndex = -1;
                for (int i = 0; i < allEpisodes.size(); ++i) {
                    if (allEpisodes[i] == lastWatchedEpisode || 
                        QFileInfo(allEpisodes[i]).fileName() == QFileInfo(lastWatchedEpisode).fileName()) {
                        currentIndex = i;
                        break;
                    }
                }
                
                QString nextEpisode;
                if (currentIndex >= 0 && currentIndex < allEpisodes.size() - 1) {
                    nextEpisode = allEpisodes[currentIndex + 1];
                    qDebug() << "Operations_VP_Shows: Found next episode in sequence:" << nextEpisode;
                } else {
                    qDebug() << "Operations_VP_Shows: No next episode available (at end of list)";
                }

                if (!nextEpisode.isEmpty()) {

                    // Find this episode in the tree
                    std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findEpisodeItem;
                    findEpisodeItem = [&findEpisodeItem](QTreeWidgetItem* parent, const QString& episodePath) -> QTreeWidgetItem* {
                        for (int i = 0; i < parent->childCount(); ++i) {
                            QTreeWidgetItem* child = parent->child(i);

                            QString itemPath = child->data(0, Qt::UserRole).toString();
                            if (!itemPath.isEmpty()) {
                                QFileInfo itemInfo(itemPath);
                                QFileInfo episodeInfo(episodePath);
                                if (itemInfo.fileName() == episodeInfo.fileName()) {
                                    return child;
                                }
                            }

                            if (child->childCount() > 0) {
                                QTreeWidgetItem* found = findEpisodeItem(child, episodePath);
                                if (found) return found;
                            }
                        }
                        return nullptr;
                    };

                    for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                        QTreeWidgetItem* found = findEpisodeItem(treeWidget->topLevelItem(i), nextEpisode);
                        if (found) {
                            qDebug() << "Operations_VP_Shows: Found next episode in tree:" << found->text(0);
                            return found;
                        }
                    }
                }
            }
        }
    }

    qDebug() << "Operations_VP_Shows: No watch history found or no resumable episode";
    
    // Check if autoplay random is enabled when there's no watch history
    if (m_currentShowSettings.autoplayRandom) {
        qDebug() << "Operations_VP_Shows: Autoplay random enabled with no watch history, selecting random episode";
        QString randomEpisodePath = findRandomEpisode();
        
        if (!randomEpisodePath.isEmpty()) {
            // Find this episode in the tree
            std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findEpisodeItem;
            findEpisodeItem = [&findEpisodeItem](QTreeWidgetItem* parent, const QString& episodePath) -> QTreeWidgetItem* {
                for (int i = 0; i < parent->childCount(); ++i) {
                    QTreeWidgetItem* child = parent->child(i);
                    
                    QString itemPath = child->data(0, Qt::UserRole).toString();
                    if (!itemPath.isEmpty()) {
                        QFileInfo itemInfo(itemPath);
                        QFileInfo episodeInfo(episodePath);
                        if (itemInfo.fileName() == episodeInfo.fileName()) {
                            return child;
                        }
                    }
                    
                    if (child->childCount() > 0) {
                        QTreeWidgetItem* found = findEpisodeItem(child, episodePath);
                        if (found) return found;
                    }
                }
                return nullptr;
            };
            
            for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
                QTreeWidgetItem* found = findEpisodeItem(treeWidget->topLevelItem(i), randomEpisodePath);
                if (found) {
                    qDebug() << "Operations_VP_Shows: Found random episode (no history case) in tree:" << found->text(0);
                    return found;
                }
            }
        }
    }
    
    qDebug() << "Operations_VP_Shows: Looking for first episode";
    // If no watch history or episode not found, find the first episode
    // Skip Extra/Movies/OVA categories and prioritize Episode 1 or S01E01

    QTreeWidgetItem* firstEpisodeToPlay = nullptr;
    QTreeWidgetItem* fallbackEpisode = nullptr;

    // Search through all language versions
    for (int langIndex = 0; langIndex < treeWidget->topLevelItemCount(); ++langIndex) {
        QTreeWidgetItem* languageItem = treeWidget->topLevelItem(langIndex);

        // Search through all categories under this language
        for (int catIndex = 0; catIndex < languageItem->childCount(); ++catIndex) {
            QTreeWidgetItem* categoryItem = languageItem->child(catIndex);
            QString categoryText = categoryItem->text(0);

            // Skip special categories
            if (categoryText.startsWith("Extra") ||
                categoryText.startsWith("Movies") ||
                categoryText.startsWith("OVA") ||
                categoryText.contains("Error")) {
                qDebug() << "Operations_VP_Shows: Skipping category:" << categoryText;
                continue;
            }

            // Check if this is "Episodes" (absolute numbering) or "Season" category
            if (categoryText == tr("Episodes")) {
                // This is absolute numbering, look for Episode 1
                qDebug() << "Operations_VP_Shows: Found Episodes category (absolute numbering)";
                if (categoryItem->childCount() > 0) {
                    QTreeWidgetItem* firstEp = categoryItem->child(0);

                    // Check if this is Episode 1
                    QString epText = firstEp->text(0);
                    if (epText.contains("Episode 1") || epText.contains("Ep. 1") ||
                        epText.contains("E1 ") || epText == "1") {
                        qDebug() << "Operations_VP_Shows: Found Episode 1 in absolute numbering";
                        firstEpisodeToPlay = firstEp;
                        break;
                    }

                    // Store as fallback if we don't find Episode 1
                    if (!fallbackEpisode) {
                        fallbackEpisode = firstEp;
                        qDebug() << "Operations_VP_Shows: Storing first absolute episode as fallback:" << epText;
                    }
                }
            } else if (categoryText.startsWith(tr("Season"))) {
                // This is a season, check if it's Season 1
                if (categoryText == tr("Season 1") || categoryText == tr("Season %1").arg(1)) {
                    qDebug() << "Operations_VP_Shows: Found Season 1";
                    if (categoryItem->childCount() > 0) {
                        QTreeWidgetItem* firstEp = categoryItem->child(0);

                        // Check if this is Episode 1 of Season 1
                        QString epText = firstEp->text(0);
                        if (epText.contains("Episode 1") || epText.contains("Ep. 1") ||
                            epText.contains("E01") || epText.contains("E1 ")) {
                            qDebug() << "Operations_VP_Shows: Found S01E01";
                            firstEpisodeToPlay = firstEp;
                            break;
                        }

                        // Store as fallback if we don't find Episode 1
                        if (!fallbackEpisode) {
                            fallbackEpisode = firstEp;
                            qDebug() << "Operations_VP_Shows: Storing first episode of Season 1 as fallback:" << epText;
                        }
                    }
                } else if (!fallbackEpisode && categoryItem->childCount() > 0) {
                    // Store first episode of any other season as last resort fallback
                    fallbackEpisode = categoryItem->child(0);
                    qDebug() << "Operations_VP_Shows: Storing first episode of" << categoryText << "as last resort fallback";
                }
            }
        }

        // If we found the first episode to play, stop searching
        if (firstEpisodeToPlay) {
            break;
        }
    }

    // Return the episode we found
    QTreeWidgetItem* episodeToPlay = firstEpisodeToPlay ? firstEpisodeToPlay : fallbackEpisode;

    if (episodeToPlay) {
        qDebug() << "Operations_VP_Shows: Episode to play:" << episodeToPlay->text(0);
    } else {
        qDebug() << "Operations_VP_Shows: No episode found to play";
    }

    return episodeToPlay;
}

// Helper function to expand tree to show last watched episode
void Operations_VP_Shows::expandToLastWatchedEpisode()
{
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Expanding tree to show episode that would be played";
    
    // Use the same logic as Play button to determine which episode to expand to
    QTreeWidgetItem* episodeToExpand = determineEpisodeToPlay();
    
    if (!episodeToExpand) {
        qDebug() << "Operations_VP_Shows: No episode to expand to";
        return;
    }
    
    // Expand all parent items to make the episode visible
    QTreeWidgetItem* current = episodeToExpand->parent();
    while (current) {
        current->setExpanded(true);
        current = current->parent();
    }
    
    // Scroll to the episode
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    treeWidget->scrollToItem(episodeToExpand, QAbstractItemView::PositionAtCenter);
    
    qDebug() << "Operations_VP_Shows: Expanded tree to show episode:" << episodeToExpand->text(0);
}


// =================
// CONTEXT MENU ACTION
// =================

// Slot for handling the mark as watched/unwatched action
void Operations_VP_Shows::toggleWatchedStateFromContextMenu()
{
    if (!m_watchHistory) {
        qDebug() << "Operations_VP_Shows: Cannot toggle watched state - watch history not available";
        return;
    }

    // Get all selected items
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    QList<QTreeWidgetItem*> selectedItems = treeWidget->selectedItems();
    
    if (selectedItems.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No items selected for watch state toggle";
        return;
    }
    
    // Determine if we're marking as watched or unwatched
    // Check if all selected episodes are already watched (completed)
    bool allWatched = true;
    for (const QString& episodePath : m_contextMenuEpisodePaths) {
        QDir showDir(m_currentShowFolder);
        QString relativePath = showDir.relativeFilePath(episodePath);
        // Use isEpisodeCompleted instead of hasEpisodeBeenWatched
        // because hasEpisodeBeenWatched only checks if episode exists in history,
        // not if it's actually marked as watched/completed
        if (!m_watchHistory->isEpisodeCompleted(relativePath)) {
            allWatched = false;
            break;
        }
    }
    
    bool markAsWatched = !allWatched;
    
    qDebug() << "Operations_VP_Shows: Toggling watched state for" << m_contextMenuEpisodePaths.size() 
             << "episodes. Marking as watched:" << markAsWatched;

    // Apply the watch state to all selected items
    if (selectedItems.size() > 1) {
        // Multiple selection - treat like a category
        setWatchedStateForEpisodes(m_contextMenuEpisodePaths, markAsWatched);
    } else {
        // Single selection - use existing logic
        setWatchedStateForItem(m_contextMenuTreeItem, markAsWatched);
    }

    // Show feedback to user
    QString message;
    int episodeCount = m_contextMenuEpisodePaths.size();
    
    if (selectedItems.size() == 1 && m_contextMenuTreeItem->childCount() == 0) {
        // Single episode
        message = markAsWatched ?
                      tr("Episode \"%1\" marked as watched").arg(m_contextMenuTreeItem->text(0)) :
                      tr("Episode \"%1\" marked as unwatched").arg(m_contextMenuTreeItem->text(0));
    } else {
        // Multiple episodes or category
        message = markAsWatched ?
                      tr("Marked %1 episode%2 as watched").arg(episodeCount).arg(episodeCount > 1 ? "s" : "") :
                      tr("Marked %1 episode%2 as unwatched").arg(episodeCount).arg(episodeCount > 1 ? "s" : "");
    }

    // Optional: Show a status message (you can use statusBar if available)
    qDebug() << "Operations_VP_Shows:" << message;
}

// Slot for handling the mark as favourite/unfavourite action
void Operations_VP_Shows::toggleFavouriteStateFromContextMenu()
{
    if (!m_showFavourites) {
        qDebug() << "Operations_VP_Shows: Cannot toggle favourite state - favourites manager not available";
        return;
    }
    
    // Get all selected items
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    QList<QTreeWidgetItem*> selectedItems = treeWidget->selectedItems();
    
    if (selectedItems.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No items selected for favourite state toggle";
        return;
    }
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episode paths for favourite toggle";
        return;
    }
    
    // Determine if we're marking as favourite or unfavourite
    // Check if all selected episodes are already favourites
    bool allFavourites = true;
    for (const QString& episodePath : m_contextMenuEpisodePaths) {
        QDir showDir(m_currentShowFolder);
        QString relativePath = showDir.relativeFilePath(episodePath);
        
        if (!m_showFavourites->isEpisodeFavourite(relativePath)) {
            allFavourites = false;
            break;
        }
    }
    
    bool markAsFavourite = !allFavourites;
    
    qDebug() << "Operations_VP_Shows: Toggling favourite state for" << m_contextMenuEpisodePaths.size() 
             << "episodes. Marking as favourite:" << markAsFavourite;
    
    // Apply the favourite state to all selected episodes
    int successCount = 0;
    int failCount = 0;
    
    for (const QString& episodePath : m_contextMenuEpisodePaths) {
        QDir showDir(m_currentShowFolder);
        QString relativePath = showDir.relativeFilePath(episodePath);
        
        bool success = false;
        if (markAsFavourite) {
            success = m_showFavourites->addEpisodeToFavourites(relativePath);
        } else {
            success = m_showFavourites->removeEpisodeFromFavourites(relativePath);
        }
        
        if (success) {
            successCount++;
        } else {
            failCount++;
            qDebug() << "Operations_VP_Shows: Failed to toggle favourite for:" << relativePath;
        }
    }
    
    // Update visual indicators in the tree widget
    updateFavouriteIndicators();
    
    // Show feedback to user
    QString message;
    int episodeCount = m_contextMenuEpisodePaths.size();
    
    if (selectedItems.size() == 1 && m_contextMenuTreeItem && m_contextMenuTreeItem->childCount() == 0) {
        // Single episode
        message = markAsFavourite ?
                      tr("Episode \"%1\" marked as favourite").arg(m_contextMenuTreeItem->text(0)) :
                      tr("Episode \"%1\" removed from favourites").arg(m_contextMenuTreeItem->text(0));
    } else {
        // Multiple episodes or category
        if (failCount == 0) {
            message = markAsFavourite ?
                          tr("Marked %1 episode%2 as favourite").arg(episodeCount).arg(episodeCount > 1 ? "s" : "") :
                          tr("Removed %1 episode%2 from favourites").arg(episodeCount).arg(episodeCount > 1 ? "s" : "");
        } else {
            message = tr("Successfully updated %1 of %2 episodes").arg(successCount).arg(episodeCount);
        }
    }
    
    // Optional: Show a status message
    qDebug() << "Operations_VP_Shows:" << message;
    
    // If there were failures, show a warning
    if (failCount > 0) {
        QMessageBox::warning(m_mainWindow, tr("Partial Success"),
                           tr("%1\n\n%2 episode(s) could not be updated.").arg(message).arg(failCount));
    }
}

void Operations_VP_Shows::updateFavouriteIndicators()
{
    if (!m_showFavourites) {
        qDebug() << "Operations_VP_Shows: Cannot update favourite indicators - favourites manager not available";
        return;
    }
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Cannot update favourite indicators - tree widget not available";
        return;
    }
    
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    
    // Define the watched color to match what's used elsewhere
    QColor watchedColor(128, 128, 128); // Grey color for watched items
    
    // Iterate through all items in the tree
    for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* languageItem = treeWidget->topLevelItem(i);
        
        for (int j = 0; j < languageItem->childCount(); ++j) {
            QTreeWidgetItem* seasonItem = languageItem->child(j);
            
            for (int k = 0; k < seasonItem->childCount(); ++k) {
                QTreeWidgetItem* episodeItem = seasonItem->child(k);
                
                // Get the episode path from user data
                QString episodePath = episodeItem->data(0, Qt::UserRole).toString();
                if (!episodePath.isEmpty()) {
                    // Convert to relative path
                    QDir showDir(m_currentShowFolder);
                    QString relativePath = showDir.relativeFilePath(episodePath);
                    
                    // Get the original text without any existing indicators
                    QString originalText = episodeItem->text(0);
                    
                    // Remove any existing star indicators (both filled and outline)
                    originalText.remove(" ★");  // Remove filled star
                    originalText.remove(" ☆");  // Remove outline star
                    
                    // Check if this episode is watched
                    bool isWatched = false;
                    if (m_watchHistory) {
                        isWatched = m_watchHistory->isEpisodeCompleted(relativePath);
                    }
                    
                    // Check if this episode is a favourite
                    if (m_showFavourites->isEpisodeFavourite(relativePath)) {
                        // Add a filled star to indicate it's a favourite
                        episodeItem->setText(0, originalText + " ★");
                        
                        // Optional: Set a different color or font for favourites
                        QFont font = episodeItem->font(0);
                        font.setBold(true);
                        episodeItem->setFont(0, font);
                        
                        // Optional: Set a golden color for the star
                        episodeItem->setForeground(0, QBrush(QColor(255, 215, 0)));  // Gold color
                    } else {
                        // Reset to original text without star
                        episodeItem->setText(0, originalText);
                        
                        // Reset font
                        QFont font = episodeItem->font(0);
                        font.setBold(false);
                        episodeItem->setFont(0, font);
                        
                        // Preserve watched state color or reset to default
                        if (isWatched) {
                            episodeItem->setForeground(0, QBrush(watchedColor));  // Keep watched color
                        } else {
                            episodeItem->setForeground(0, QBrush());  // Reset to default color
                        }
                    }
                }
            }
        }
    }
    
    qDebug() << "Operations_VP_Shows: Updated favourite indicators in tree widget";
}


void Operations_VP_Shows::refreshShowPosterWithNotification()
{
    qDebug() << "Operations_VP_Shows: Refreshing show poster with notification check";

    // First reload the poster image
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->label_VP_Shows_Display_Image) {
        QPixmap showImage = loadShowImage(m_currentShowFolder);
        if (!showImage.isNull()) {
            QSize labelSize = m_mainWindow->ui->label_VP_Shows_Display_Image->size();
            QPixmap scaledImage = showImage.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_mainWindow->ui->label_VP_Shows_Display_Image->setPixmap(scaledImage);
        }
    }

    // Then check and display new episode notification
    if (VP_ShowsConfig::isTMDBEnabled() && m_currentShowSettings.useTMDB &&
        getShowIdAsInt(m_currentShowSettings.showId) > 0) {
        qDebug() << "Operations_VP_Shows: Checking for new episodes";
        checkAndDisplayNewEpisodes(m_currentShowFolder, getShowIdAsInt(m_currentShowSettings.showId));
    } else {
        // Clear any notification if TMDB is disabled
        displayNewEpisodeIndicator(false, 0);
    }
}
