#include "operations_vp_shows.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "VP_Shows_Videoplayer.h"
#include "vp_shows_progressdialogs.h"
#include "vp_shows_metadata.h"
#include "vp_shows_settings_dialog.h"
#include "vp_shows_add_dialog.h"
#include "vp_shows_edit_metadata_dialog.h"
#include "vp_shows_tmdb.h"
#include "vp_shows_settings.h"  // Add show settings
#include "inputvalidation.cpp"
#include "operations_files.h"  // Add operations_files for secure file operations
#include "CryptoUtils.h"
#include "vp_shows_watchhistory.h"  // Core watch history data management
#include "vp_shows_playback_tracker.h"  // Playback tracking integration
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
#include <QMediaPlayer>
#include <QCoreApplication>
#include <QProgressDialog>
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
#include <algorithm>
#include <functional>

Operations_VP_Shows::Operations_VP_Shows(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_encryptionDialog(nullptr)
    , m_watchHistory(nullptr)
    , m_playbackTracker(nullptr)
    , m_searchDebounceTimer(nullptr)
    , m_isAutoplayInProgress(false)
    , m_episodeWasNearCompletion(false)
{
    qDebug() << "Operations_VP_Shows: Constructor called";
    qDebug() << "Operations_VP_Shows: Autoplay system initialized";
    qDebug() << "Operations_VP_Shows: === CONFIGURATION ===";
    qDebug() << "Operations_VP_Shows:   COMPLETION_THRESHOLD_MS:" << VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS << "ms (" << (VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS/1000) << "seconds)";
    qDebug() << "Operations_VP_Shows:   SAVE_INTERVAL_SECONDS:" << VP_ShowsWatchHistory::SAVE_INTERVAL_SECONDS << "seconds";
    qDebug() << "Operations_VP_Shows:   Initial near-completion flag:" << m_episodeWasNearCompletion;
    
    // Set username for operations_files functions
    if (m_mainWindow && !m_mainWindow->user_Username.isEmpty()) {
        OperationsFiles::setUsername(m_mainWindow->user_Username);
        qDebug() << "Operations_VP_Shows: Set username for operations_files:" << m_mainWindow->user_Username;
    }
    
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
    }
    
    // Connect double-click on shows list to display show details
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->listWidget_VP_List_List) {
        connect(m_mainWindow->ui->listWidget_VP_List_List, &QListWidget::itemDoubleClicked,
                this, &Operations_VP_Shows::onShowListItemDoubleClicked);
        qDebug() << "Operations_VP_Shows: Connected show list double-click handler";
        
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
    
    // Load the TV shows list on initialization
    // We use a small delay to ensure the UI is fully initialized
    QTimer::singleShot(100, this, &Operations_VP_Shows::loadTVShowsList);
}

Operations_VP_Shows::~Operations_VP_Shows()
{
    qDebug() << "Operations_VP_Shows: Destructor called";
    
    // Stop playback tracking if active
    if (m_playbackTracker) {
        qDebug() << "Operations_VP_Shows: Stopping playback tracking in destructor";
        m_playbackTracker->stopTracking();
    }
    
    // Force release and cleanup any playing video
    if (m_episodePlayer) {
        forceReleaseVideoFile();
        // Reset the player to ensure it's properly closed
        m_episodePlayer.reset();
    }
    
    // Clean up playback tracker and watch history
    if (m_playbackTracker) {
        m_playbackTracker.reset();
    }
    if (m_watchHistory) {
        m_watchHistory.reset();
    }
    
    if (m_encryptionDialog) {
        delete m_encryptionDialog;
    }
    
    // Clean up temp file if it exists
    cleanupTempFile();
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

void Operations_VP_Shows::on_pushButton_VP_List_AddEpisode_clicked()
{
    qDebug() << "Operations_VP_Shows: Add Episode button clicked";
    
    // Open file dialog to select video files for episodes
    QString filter = "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg *.3gp);;All Files (*.*)";
    
    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        m_mainWindow,
        tr("Select Episode Video Files"),
        QDir::homePath(),
        filter
    );
    
    if (selectedFiles.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No files selected for adding episodes";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Selected" << selectedFiles.size() << "files for episodes";
    
    // Show the add dialog with empty show name field
    VP_ShowsAddDialog addDialog("", m_mainWindow);  // Pass empty string for show name
    addDialog.setWindowTitle(tr("Add Episodes to Library"));
    
    if (addDialog.exec() != QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Add episodes dialog cancelled";
        return;
    }
    
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
    
    // Check if this show already exists with the same language and translation mode
    QString existingShowFolder;
    QStringList existingEpisodes;
    bool showExists = checkForExistingShow(showName, language, translationMode, existingShowFolder, existingEpisodes);
    
    QString outputPath;
    QStringList filesToImport;
    QStringList targetFiles;
    
    if (showExists) {
        qDebug() << "Operations_VP_Shows: Show already exists, checking for new episodes";
        qDebug() << "Operations_VP_Shows: Existing show folder:" << existingShowFolder;
        qDebug() << "Operations_VP_Shows: Existing episodes count:" << existingEpisodes.size();
        
        // Filter out episodes we already have
        filesToImport = filterNewEpisodes(selectedFiles, existingEpisodes, showName, language, translationMode);
        
        if (filesToImport.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No new episodes to import";
            QMessageBox::information(m_mainWindow,
                                   tr("No New Episodes"),
                                   tr("All selected episodes already exist in \"%1\" with the specified language and translation.").arg(showName));
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Found" << filesToImport.size() << "new episodes to import";
        outputPath = existingShowFolder; // Use existing show folder
        m_currentImportOutputPath = outputPath; // Store for use in onEncryptionComplete
    } else {
        qDebug() << "Operations_VP_Shows: This is a new show, importing all episodes";
        filesToImport = selectedFiles;
        
        // Create the output folder structure using secure operations_files functions
        if (!createShowFolderStructure(outputPath)) {
            QMessageBox::critical(m_mainWindow,
                                tr("Folder Creation Failed"),
                                tr("Failed to create the necessary folder structure. Please check permissions and try again."));
            return;
        }
        m_currentImportOutputPath = outputPath; // Store for use in onEncryptionComplete
    }
    
    // Generate random filenames for each video file to import
    for (const QString& sourceFile : filesToImport) {
        QFileInfo fileInfo(sourceFile);
        QString extension = fileInfo.suffix().toLower();
        QString randomName = generateRandomFileName(extension);
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
    
    // Store whether this is an update or new import for the completion message
    m_isUpdatingExistingShow = showExists;
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
    
    // Get playback settings from the dialog
    bool autoplay = addDialog.isAutoplayEnabled();
    bool skipIntro = addDialog.isSkipIntroEnabled();
    bool skipOutro = addDialog.isSkipOutroEnabled();
    
    // Store dialog settings for use in onEncryptionComplete
    m_dialogAutoplay = autoplay;
    m_dialogSkipIntro = skipIntro;
    m_dialogSkipOutro = skipOutro;
    m_dialogUseTMDB = useTMDB;
    
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
    
    // Start encryption with language and translation info
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, encryptionKey, username, 
                                       language, translationMode, useTMDB, customPoster, customDescription);
}

void Operations_VP_Shows::importTVShow()
{
    qDebug() << "Operations_VP_Shows: Starting TV show import";
    
    // Ensure username is set for operations_files
    if (!m_mainWindow->user_Username.isEmpty()) {
        OperationsFiles::setUsername(m_mainWindow->user_Username);
    }
    
    // Open folder dialog to select TV show folder
    QString folderPath = QFileDialog::getExistingDirectory(
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
    
    // Get the folder name to pre-populate the dialog
    QDir selectedDir(folderPath);
    QString folderName = selectedDir.dirName();
    
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
    m_dialogAutoplay = autoplay;
    m_dialogSkipIntro = skipIntro;
    m_dialogSkipOutro = skipOutro;
    m_dialogUseTMDB = useTMDB;
    
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
    
    // Find all video files in the folder and subfolders
    QStringList videoFiles = findVideoFiles(folderPath);
    
    if (videoFiles.isEmpty()) {
        QMessageBox::warning(m_mainWindow,
                           tr("No Video Files Found"),
                           tr("The selected folder does not contain any compatible video files."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Found" << videoFiles.size() << "video files";
    
    // Check if this show already exists with the same language and translation mode
    QString existingShowFolder;
    QStringList existingEpisodes;
    bool showExists = checkForExistingShow(showName, language, translationMode, existingShowFolder, existingEpisodes);
    
    QString outputPath;
    QStringList filesToImport;
    QStringList targetFiles;
    
    if (showExists) {
        qDebug() << "Operations_VP_Shows: Show already exists, checking for new episodes";
        qDebug() << "Operations_VP_Shows: Existing show folder:" << existingShowFolder;
        qDebug() << "Operations_VP_Shows: Existing episodes count:" << existingEpisodes.size();
        
        // Filter out episodes we already have
        filesToImport = filterNewEpisodes(videoFiles, existingEpisodes, showName, language, translationMode);
        
        if (filesToImport.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No new episodes to import";
            QMessageBox::warning(m_mainWindow,
                               tr("No New Episodes"),
                               tr("Could not import \"%1\". This show is already in your library and the folder you selected contains no new episodes.").arg(showName));
            return;
        }
        
        qDebug() << "Operations_VP_Shows: Found" << filesToImport.size() << "new episodes to import";
        outputPath = existingShowFolder; // Use existing show folder
    } else {
        qDebug() << "Operations_VP_Shows: This is a new show, importing all episodes";
        filesToImport = videoFiles;
        
        // Create the output folder structure using secure operations_files functions
        if (!createShowFolderStructure(outputPath)) {
            QMessageBox::critical(m_mainWindow,
                                tr("Folder Creation Failed"),
                                tr("Failed to create the necessary folder structure. Please check permissions and try again."));
            return;
        }
    }
    
    // Generate random filenames for each video file to import
    for (const QString& sourceFile : filesToImport) {
        QFileInfo fileInfo(sourceFile);
        QString extension = fileInfo.suffix().toLower();
        QString randomName = generateRandomFileName(extension);
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
    
    // Store whether this is an update or new import for the completion message
    m_isUpdatingExistingShow = showExists;
    m_originalEpisodeCount = videoFiles.size();
    m_newEpisodeCount = filesToImport.size();
    
    // Create and show progress dialog
    if (!m_encryptionDialog) {
        m_encryptionDialog = new VP_ShowsEncryptionProgressDialog(m_mainWindow);
        connect(m_encryptionDialog, &VP_ShowsEncryptionProgressDialog::encryptionComplete,
                this, &Operations_VP_Shows::onEncryptionComplete);
    }
    
    // Start encryption with language and translation info, including custom data if not using TMDB
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, encryptionKey, username, 
                                       language, translationMode, useTMDB, customPoster, customDescription);
}

QStringList Operations_VP_Shows::findVideoFiles(const QString& folderPath, bool recursive)
{
    QStringList videoFiles;
    QStringList videoExtensions = {"mmvid"}; // Custom extension for encrypted video files
    
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
                videoFiles.append(filePath);
                qDebug() << "Operations_VP_Shows: Found video file:" << fileInfo.fileName();
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
    Q_UNUSED(extension); // We'll always use .mmvid now for encrypted videos
    
    // Generate a random filename with the custom .mmvid extension
    const QString chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const int nameLength = 32;
    
    QString randomName;
    for (int i = 0; i < nameLength; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        randomName.append(chars.at(index));
    }
    
    // Always add .mmvid extension for encrypted video files
    return randomName + ".mmvid";
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
    
    // Generate a random folder name for this specific show
    QString randomFolderName = generateRandomFileName("");
    randomFolderName = randomFolderName.left(randomFolderName.length() - 1); // Remove the dot
    
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
                
                // Create settings with values from dialog (stored when dialog was accepted)
                VP_ShowsSettings::ShowSettings settings;
                settings.autoplay = m_dialogAutoplay;
                settings.skipIntro = m_dialogSkipIntro;
                settings.skipOutro = m_dialogSkipOutro;
                settings.useTMDB = m_dialogUseTMDB;
                
                // Save the settings file
                if (settingsManager.saveShowSettings(showFolderPath, settings)) {
                    qDebug() << "Operations_VP_Shows: Settings file created/updated successfully";
                } else {
                    qDebug() << "Operations_VP_Shows: Failed to create/update settings file";
                }
            }
        }
        
        QString successMessage;
        if (m_isUpdatingExistingShow) {
            // This was an update to an existing show
            int addedCount = successfulFiles.size();
            successMessage = tr("Successfully added %1 new episode(s) to your library.").arg(addedCount);
            
            if (failedFiles.size() > 0) {
                successMessage += tr("\n\nNote: %1 episode(s) failed to import.").arg(failedFiles.size());
            }
        } else {
            // This was a new show import
            successMessage = tr("TV show imported successfully!\n%1").arg(message);
        }
        
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
        
        // If we're currently displaying a show and added episodes to it, refresh the episode list
        if (m_isUpdatingExistingShow && !m_currentShowFolder.isEmpty()) {
            loadShowEpisodes(m_currentShowFolder);
        }
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
        if (successfulFiles.isEmpty() && !failedFiles.isEmpty() && !m_isUpdatingExistingShow) {
            qDebug() << "Operations_VP_Shows: All files failed for new show, cleaning up created folders";
            // The encrypted files that failed would have been cleaned up by the encryption worker
            // We just need to log this for debugging purposes
        }
    }
    
    // Reset the flags
    m_isUpdatingExistingShow = false;
    m_originalEpisodeCount = 0;
    m_newEpisodeCount = 0;
}

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
    
    // Create metadata manager for reading show metadata
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Process each show folder
    for (const QString& folderName : showFolders) {
        QString folderPath = showsDir.absoluteFilePath(folderName);
        QDir showFolder(folderPath);
        
        // Find the first video file in this folder to read its metadata
        QStringList videoExtensions;
        videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                       << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
        
        showFolder.setNameFilters(videoExtensions);
        QStringList videoFiles = showFolder.entryList(QDir::Files);
        
        if (videoFiles.isEmpty()) {
            qDebug() << "Operations_VP_Shows: No video files found in folder:" << folderName;
            continue;
        }
        
        // Try to read metadata from the first video file
        QString firstVideoPath = showFolder.absoluteFilePath(videoFiles.first());
        VP_ShowsMetadata::ShowMetadata metadata;
        
        if (metadataManager.readMetadataFromFile(firstVideoPath, metadata)) {
            // Successfully read metadata, check if it has the Show field
            if (!metadata.showName.isEmpty()) {
                qDebug() << "Operations_VP_Shows: Found show:" << metadata.showName;
                
                // Add the show name to the list widget
                QListWidgetItem* item = new QListWidgetItem();
                
                // Store the folder path as user data for later use (when playing videos)
                item->setData(Qt::UserRole, folderPath);
                
                // Store the mapping in RAM for quick access
                m_showFolderMapping[metadata.showName] = folderPath;
                
                // Set up the item based on current view mode
                refreshShowListItem(item, metadata.showName, folderPath);
                
                m_mainWindow->ui->listWidget_VP_List_List->addItem(item);
            } else {
                qDebug() << "Operations_VP_Shows: Show name is empty in metadata for folder:" << folderName;
            }
        } else {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from file:" << firstVideoPath;
        }
    }
    
    // Sort the list alphabetically
    m_mainWindow->ui->listWidget_VP_List_List->sortItems(Qt::AscendingOrder);
    
    qDebug() << "Operations_VP_Shows: Finished loading shows. Total shows:" 
             << m_mainWindow->ui->listWidget_VP_List_List->count();
}

void Operations_VP_Shows::refreshTVShowsList()
{
    qDebug() << "Operations_VP_Shows: Refreshing TV shows list";
    
    // Simply call loadTVShowsList to reload the entire list
    loadTVShowsList();
}

void Operations_VP_Shows::onViewModeChanged(int index)
{
    qDebug() << "Operations_VP_Shows: View mode changed to index:" << index;
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        qDebug() << "Operations_VP_Shows: UI elements not available for view mode change";
        return;
    }
    
    // Index 0 = List view, Index 1 = Icon view
    m_isIconViewMode = (index == 1);
    
    if (m_isIconViewMode) {
        qDebug() << "Operations_VP_Shows: Switching to Icon view mode";
        setupIconViewMode();
    } else {
        qDebug() << "Operations_VP_Shows: Switching to List view mode";
        setupListViewMode();
    }
    
    // Refresh all items with the new view mode
    for (int i = 0; i < m_mainWindow->ui->listWidget_VP_List_List->count(); ++i) {
        QListWidgetItem* item = m_mainWindow->ui->listWidget_VP_List_List->item(i);
        if (item) {
            QString folderPath = item->data(Qt::UserRole).toString();
            QString showName = item->text();
            
            // Find the actual show name if we have it in the mapping
            for (auto it = m_showFolderMapping.begin(); it != m_showFolderMapping.end(); ++it) {
                if (it.value() == folderPath) {
                    showName = it.key();
                    break;
                }
            }
            
            refreshShowListItem(item, showName, folderPath);
        }
    }
}

void Operations_VP_Shows::setupListViewMode()
{
    qDebug() << "Operations_VP_Shows: Setting up List view mode";
    
    QListWidget* listWidget = m_mainWindow->ui->listWidget_VP_List_List;
    
    // Set to list mode
    listWidget->setViewMode(QListView::ListMode);
    listWidget->setResizeMode(QListView::Fixed);
    listWidget->setSpacing(2);
    listWidget->setUniformItemSizes(false);
    
    // Reset icon size to default for list mode
    listWidget->setIconSize(QSize(16, 16));
    
    // Set layout mode
    listWidget->setFlow(QListView::TopToBottom);
    listWidget->setWrapping(false);
    
    qDebug() << "Operations_VP_Shows: List view mode configured";
}

void Operations_VP_Shows::setupIconViewMode()
{
    qDebug() << "Operations_VP_Shows: Setting up Icon view mode";
    
    QListWidget* listWidget = m_mainWindow->ui->listWidget_VP_List_List;
    
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
    
    qDebug() << "Operations_VP_Shows: Icon view mode configured";
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
    }
}

QPixmap Operations_VP_Shows::loadShowPoster(const QString& showFolderPath, const QSize& targetSize)
{
    qDebug() << "Operations_VP_Shows: Loading poster for show folder:" << showFolderPath;
    
    // Check cache first
    if (m_posterCache.contains(showFolderPath)) {
        qDebug() << "Operations_VP_Shows: Found poster in cache";
        return m_posterCache[showFolderPath];
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
    
    // Cache the scaled poster
    m_posterCache[showFolderPath] = scaledPoster;
    
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
    
    // Show the dialog modally
    if (settingsDialog.exec() == QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Show settings saved";
        
        // Reload show settings to apply any changes
        loadShowSettings(m_currentShowFolder);
        
        // Update the show name display if it changed
        // We need to reload the metadata to get the updated show name
        VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
        
        // Find any video file to get the updated show name
        QDir showDir(m_currentShowFolder);
        QStringList videoExtensions;
        videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                       << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
        showDir.setNameFilters(videoExtensions);
        QStringList videoFiles = showDir.entryList(QDir::Files);
        
        if (!videoFiles.isEmpty()) {
            QString firstVideoPath = showDir.absoluteFilePath(videoFiles.first());
            VP_ShowsMetadata::ShowMetadata metadata;
            
            if (metadataManager.readMetadataFromFile(firstVideoPath, metadata)) {
                // Update the show name display
                if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
                    m_mainWindow->ui->label_VP_Shows_Display_Name->setText(metadata.showName);
                    qDebug() << "Operations_VP_Shows: Updated show name display to:" << metadata.showName;
                }
            }
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

void Operations_VP_Shows::displayShowDetails(const QString& showName)
{
    qDebug() << "Operations_VP_Shows: Displaying details for show:" << showName;
    
    // Check if we have the required UI elements
    if (!m_mainWindow || !m_mainWindow->ui) {
        qDebug() << "Operations_VP_Shows: UI elements not available";
        return;
    }
    
    // Get the folder path for this show
    if (!m_showFolderMapping.contains(showName)) {
        qDebug() << "Operations_VP_Shows: Show not found in mapping:" << showName;
        QMessageBox::warning(m_mainWindow, tr("Show Not Found"), 
                           tr("Could not find the folder for this show. Please refresh the list."));
        return;
    }
    
    QString showFolderPath = m_showFolderMapping[showName];
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
    
    // Store current show folder for later use (after watch history initialization)
    m_currentShowFolder = showFolderPath;

    // Update the show name label
    if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
        m_mainWindow->ui->label_VP_Shows_Display_Name->setText(showName);
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
            m_mainWindow->ui->label_VP_Shows_Display_Image->setPixmap(scaledImage);
            
            qDebug() << "Operations_VP_Shows: Scaled image from" << showImage.size() << "to" << scaledImage.size();
        } else {
            // Set a placeholder text if no image is available
            m_mainWindow->ui->label_VP_Shows_Display_Image->setText(tr("No Image Available"));
            qDebug() << "Operations_VP_Shows: No image available for show, displaying placeholder text";
        }
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
    
    // Load and display the episode list
    loadShowEpisodes(showFolderPath);
    
    // Load show-specific settings and update checkboxes
    loadShowSettings(showFolderPath);
    
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
                
                // Iterate through all episodes to check for resume positions
                for (int i = 0; i < tree->topLevelItemCount() && !hasResumePosition; ++i) {
                    QTreeWidgetItem* seasonItem = tree->topLevelItem(i);
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
    qDebug() << "Operations_VP_Shows: Double-clicked on show:" << showName;
    
    // Display the show details
    displayShowDetails(showName);
}

void Operations_VP_Shows::loadShowEpisodes(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Loading episodes from folder:" << showFolderPath;
    
    // Check if we have the tree widget
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Tree widget not available";
        return;
    }
    
    // Clear the tree widget and episode mapping
    m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->clear();
    m_episodeFileMapping.clear();
    
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
    
    // Process each video file
    for (const QString& videoFile : videoFiles) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        
        // Read metadata from the file
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(videoPath, metadata)) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from:" << videoFile;
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
            QString baseName = fileInfo.completeBaseName();
            QString errorName = QString("[ERROR] %1").arg(baseName);
            
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
            
            // Format the name based on content type
            QString itemName;
            QFileInfo fileInfo(metadata.filename);
            QString baseName = fileInfo.completeBaseName();
            
            if (!metadata.EPName.isEmpty()) {
                itemName = metadata.EPName;
            } else {
                itemName = baseName;
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
        
        // If we still don't have a valid episode number, skip regular episode processing
        if (episodeNum == 0) {
            qDebug() << "Operations_VP_Shows: No valid episode number found for:" << metadata.filename;
            continue;
        }
        
        // Default to season 1 if still no season number but we have an episode number
        if (seasonNum == 0) {
            seasonNum = 1;
        }
        
        // Create episode item
        QTreeWidgetItem* episodeItem = new QTreeWidgetItem();
        
        // Format the episode name based on numbering type
        QString episodeName;
        if (metadata.isAbsoluteNumbering()) {
            // For absolute numbering, just use episode number without season
            if (!metadata.EPName.isEmpty()) {
                episodeName = QString("Episode %1 - %2").arg(episodeNum).arg(metadata.EPName);
            } else {
                QFileInfo fileInfo(metadata.filename);
                QString baseName = fileInfo.completeBaseName();
                episodeName = QString("Episode %1 - %2").arg(episodeNum).arg(baseName);
            }
        } else {
            // Traditional season/episode numbering
            if (!metadata.EPName.isEmpty()) {
                episodeName = QString("%1 - %2").arg(episodeNum).arg(metadata.EPName);
            } else {
                QFileInfo fileInfo(metadata.filename);
                QString baseName = fileInfo.completeBaseName();
                episodeName = QString("%1 - %2").arg(episodeNum).arg(baseName);
            }
        }
        
        episodeItem->setText(0, episodeName);
        
        // Store the file path in the item's data
        episodeItem->setData(0, Qt::UserRole, videoPath);
        
        // Check if this episode has been watched and apply grey color if it has
        if (historyLoaded) {
            // Get relative path for watch history check
            QString relativeEpisodePath = showDir.relativeFilePath(videoPath);
            if (m_watchHistory->isEpisodeCompleted(relativeEpisodePath)) {
                episodeItem->setForeground(0, QBrush(watchedColor));
                qDebug() << "Operations_VP_Shows: Episode marked as watched:" << episodeName;
            }
        }
        
        // Create mapping key and store the mapping
        QString mappingKey = QString("%1_%2_S%3E%4")
            .arg(metadata.showName)
            .arg(languageKey)
            .arg(seasonNum, 2, 10, QChar('0'))
            .arg(episodeNum, 2, 10, QChar('0'));
        m_episodeFileMapping[mappingKey] = videoPath;
        
        // Add to language/season map
        // For absolute numbering, we'll use season 0 as a marker
        if (metadata.isAbsoluteNumbering()) {
            languageVersions[languageKey][0].append(QPair<int, QTreeWidgetItem*>(episodeNum, episodeItem));
        } else {
            languageVersions[languageKey][seasonNum].append(QPair<int, QTreeWidgetItem*>(episodeNum, episodeItem));
        }
        
        qDebug() << "Operations_VP_Shows: Added episode" << languageKey 
                 << "S" << seasonNum << "E" << episodeNum << "-" << episodeName;
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
}

void Operations_VP_Shows::onEpisodeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    
    if (!item) {
        qDebug() << "Operations_VP_Shows: Double-clicked item is null";
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
    
    // Decrypt and play the episode
    decryptAndPlayEpisode(videoPath, episodeName);
}

void Operations_VP_Shows::decryptAndPlayEpisode(const QString& encryptedFilePath, const QString& episodeName)
{
    qDebug() << "Operations_VP_Shows: Starting decryption and playback for:" << episodeName;
    
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
    
    // Note: We no longer use the m_episodeWasNearCompletion flag for autoplay decisions
    // Autoplay is now determined by checking the actual playback position when the player closes
    // This prevents the bug where seeking away from near-end would still trigger autoplay
    qDebug() << "Operations_VP_Shows: Using position-based autoplay (not flag-based)";
    m_episodeWasNearCompletion = false;  // Reset for consistency, though not used for autoplay anymore
    
    // Clean up any existing temp file first
    cleanupTempFile();
    
    // Reset any existing playback tracker before creating new one
    if (m_playbackTracker) {
        m_playbackTracker.reset();
    }
    // Don't reset m_watchHistory - it should persist for the context menu functionality
    // m_watchHistory is initialized in displayShowDetails and should remain available
    
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
            
            // NOTE: We no longer need to connect the episodeNearCompletion signal to set a flag
            // The autoplay decision is now made based on the actual position when the player closes
            // This prevents the bug where seeking away from the end would still trigger autoplay
            
            /* REMOVED - Old flag-based approach that caused autoplay bug
            QMetaObject::Connection connection = connect(m_playbackTracker.get(), &VP_ShowsPlaybackTracker::episodeNearCompletion,
                    this, [this](const QString& episodePath) {
                qDebug() << "Operations_VP_Shows: *** NEAR COMPLETION SIGNAL RECEIVED ***";
                qDebug() << "Operations_VP_Shows: Episode near completion signal received for:" << episodePath;
                qDebug() << "Operations_VP_Shows: Current flag value before setting:" << m_episodeWasNearCompletion;
                qDebug() << "Operations_VP_Shows: Setting m_episodeWasNearCompletion flag to true";
                m_episodeWasNearCompletion = true;
                qDebug() << "Operations_VP_Shows: Flag is now:" << m_episodeWasNearCompletion;
                qDebug() << "Operations_VP_Shows: Signal handler completed";
            });
            */
            
            qDebug() << "Operations_VP_Shows: Using position-based autoplay detection instead of flag-based approach";
            
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
        QMessageBox::critical(m_mainWindow, 
                            tr("Playback Error"),
                            tr("Failed to create temporary directory."));
        return;
    }
    
    if (!OperationsFiles::ensureDirectoryExists(tempDecryptPath)) {
        qDebug() << "Operations_VP_Shows: Failed to create tempdecrypt directory";
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
        QMessageBox::critical(m_mainWindow, 
                            tr("Decryption Error"),
                            tr("Failed to decrypt the video file. The file may be corrupted or the encryption key may be incorrect."));
        return;
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
        connect(m_episodePlayer.get(), &VP_Shows_Videoplayer::errorOccurred,
                this, [this](const QString& error) {
            qDebug() << "Operations_VP_Shows: VP_Shows_Videoplayer error:" << error;
            QMessageBox::critical(m_mainWindow, 
                                tr("Video Player Error"),
                                error);
            // Clean up temp file on error
            cleanupTempFile();
        });
        
        // Connect playback state changed to clean up when stopped
        connect(m_episodePlayer.get(), &VP_Shows_Videoplayer::playbackStateChanged,
                this, [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState) {
                qDebug() << "Operations_VP_Shows: Playback stopped, scheduling cleanup";
                
                // Get the current position and duration BEFORE stopping the tracker
                qint64 currentPosition = 0;
                qint64 duration = 0;
                bool shouldAutoplay = false;
                
                if (m_episodePlayer) {
                    currentPosition = m_episodePlayer->position();
                    duration = m_episodePlayer->duration();
                    qDebug() << "Operations_VP_Shows: Final position:" << currentPosition << "ms, Duration:" << duration << "ms";
                    
                    // Check if we're actually near completion RIGHT NOW (not based on old flag)
                    if (duration > 0) {
                        qint64 remaining = duration - currentPosition;
                        shouldAutoplay = (remaining <= VP_ShowsWatchHistory::COMPLETION_THRESHOLD_MS && remaining >= 0);
                        qDebug() << "Operations_VP_Shows: Remaining time:" << remaining << "ms";
                        qDebug() << "Operations_VP_Shows: Should autoplay based on current position:" << shouldAutoplay;
                    }
                }
                
                // Stop playback tracking to save final position
                if (m_playbackTracker) {
                    qDebug() << "Operations_VP_Shows: Stopping playback tracking";
                    m_playbackTracker->stopTracking();
                }
                
                // Trigger autoplay if enabled AND currently at near completion position
                qDebug() << "Operations_VP_Shows: Checking autoplay conditions:";
                qDebug() << "Operations_VP_Shows:   - Autoplay enabled:" << m_currentShowSettings.autoplay;
                qDebug() << "Operations_VP_Shows:   - Autoplay not in progress:" << !m_isAutoplayInProgress;
                qDebug() << "Operations_VP_Shows:   - Should autoplay (based on current position):" << shouldAutoplay;
                
                if (m_currentShowSettings.autoplay && !m_isAutoplayInProgress && shouldAutoplay) {
                    qDebug() << "Operations_VP_Shows: All conditions met - triggering autoplay";
                    autoplayNextEpisode();
                } else if (m_currentShowSettings.autoplay && !shouldAutoplay) {
                    qDebug() << "Operations_VP_Shows: Autoplay enabled but playback position not near completion, skipping autoplay";
                } else {
                    qDebug() << "Operations_VP_Shows: Autoplay conditions not met, skipping";
                }
                
                // Force the media player to release the file
                forceReleaseVideoFile();
                // Use a timer to delay cleanup to ensure file handle is fully released
                QTimer::singleShot(1000, this, &Operations_VP_Shows::cleanupTempFile);
                
                // Refresh the episode list to update watched status colors
                if (!m_currentShowFolder.isEmpty()) {
                    QTimer::singleShot(1500, this, [this]() {
                        qDebug() << "Operations_VP_Shows: Refreshing episode list after playback";
                        
                        // Reload watch history to get the updated watch states
                        if (m_watchHistory) {
                            qDebug() << "Operations_VP_Shows: Reloading watch history for updated states";
                            if (!m_watchHistory->loadHistory()) {
                                qDebug() << "Operations_VP_Shows: Failed to reload watch history";
                            }
                        }
                        
                        // Now refresh the episode list with updated watch states
                        loadShowEpisodes(m_currentShowFolder);
                        
                        // Update the Play/Resume button text after refreshing
                        updatePlayButtonText();
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
    if (m_episodePlayer->loadVideo(m_lastDecryptedFilePath)) {
        // Show the window first
        m_episodePlayer->show();
        
        // Set window title to episode name
        m_episodePlayer->setWindowTitle(tr("Playing: %1").arg(episodeName));
        
        // Raise and activate to ensure it's on top
        m_episodePlayer->raise();
        m_episodePlayer->activateWindow();
        
        // Start in fullscreen mode
        m_episodePlayer->startInFullScreen();
        
        // Add a small delay to ensure video widget is properly initialized
        QTimer::singleShot(100, [this, relativeEpisodePath]() {
            if (m_episodePlayer) {
                // Start tracking with the playback tracker
                if (m_playbackTracker && !relativeEpisodePath.isEmpty()) {
                    qDebug() << "Operations_VP_Shows: Starting playback tracking for episode";
                    m_playbackTracker->startTracking(relativeEpisodePath, m_episodePlayer.get());
                    
                    // Get resume position and seek if needed
                    qint64 resumePosition = m_playbackTracker->getResumePosition(relativeEpisodePath);
                    if (resumePosition > 1000) { // Only resume if more than 1 second
                        qDebug() << "Operations_VP_Shows: Resuming playback from:" << resumePosition << "ms";
                        m_episodePlayer->setPosition(resumePosition);
                        
                        // Add a small delay to ensure the slider updates properly during autoplay resume
                        QTimer::singleShot(50, [this, resumePosition]() {
                            if (m_episodePlayer) {
                                qDebug() << "Operations_VP_Shows: Forcing slider update for autoplay resume";
                                m_episodePlayer->forceUpdateSliderPosition(resumePosition);
                            }
                        });
                    }
                }
                
                m_episodePlayer->play();
                qDebug() << "Operations_VP_Shows: Episode playing in fullscreen";
            }
        });
    } else {
        qDebug() << "Operations_VP_Shows: Failed to load decrypted video";
        QMessageBox::warning(m_mainWindow,
                           tr("Load Failed"),
                           tr("Failed to load the decrypted video file."));
        // Clean up temp file if loading failed
        cleanupTempFile();
    }
}

bool Operations_VP_Shows::decryptVideoWithMetadata(const QString& sourceFile, const QString& targetFile)
{
    qDebug() << "Operations_VP_Shows: Decrypting video with metadata from:" << sourceFile;
    
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
            target.remove();
            return false;
        }
        
        // Read encrypted chunk
        QByteArray encryptedChunk = source.read(chunkSize);
        if (encryptedChunk.size() != chunkSize) {
            qDebug() << "Operations_VP_Shows: Failed to read complete chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Decrypt chunk
        QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(m_mainWindow->user_Key, encryptedChunk);
        if (decryptedChunk.isEmpty()) {
            qDebug() << "Operations_VP_Shows: Failed to decrypt chunk";
            source.close();
            target.close();
            target.remove();
            return false;
        }
        
        // Write decrypted chunk
        qint64 written = target.write(decryptedChunk);
        if (written != decryptedChunk.size()) {
            qDebug() << "Operations_VP_Shows: Failed to write decrypted chunk";
            source.close();
            target.close();
            target.remove();
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
        // Clear the media by loading an empty path
        m_episodePlayer->loadVideo("");
        // Process events to ensure the release takes effect
        QCoreApplication::processEvents();
    }
}

void Operations_VP_Shows::onPlayContinueClicked()
{
    qDebug() << "Operations_VP_Shows: Play/Continue button clicked";
    
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

void Operations_VP_Shows::showContextMenu(const QPoint& pos)
{
    qDebug() << "Operations_VP_Shows: Context menu requested";
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        return;
    }
    
    // Get the item at the position
    QListWidgetItem* item = m_mainWindow->ui->listWidget_VP_List_List->itemAt(pos);
    
    if (!item) {
        qDebug() << "Operations_VP_Shows: No item at context menu position";
        return;
    }
    
    // Clear any previous context menu data
    m_contextMenuEpisodePaths.clear();
    m_contextMenuEpisodePath.clear();
    
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
    
    // Show the menu at the cursor position
    contextMenu->exec(m_mainWindow->ui->listWidget_VP_List_List->mapToGlobal(pos));
    
    // Clean up
    contextMenu->deleteLater();
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
    
    // Open file dialog to select video files
    QString filter = "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.mpg *.mpeg *.3gp);;All Files (*.*)";
    
    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        m_mainWindow,
        tr("Select Video Files to Add"),
        QDir::homePath(),
        filter
    );
    
    if (selectedFiles.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No files selected for adding";
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Selected" << selectedFiles.size() << "files to add";
    
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
        QString extension = fileInfo.suffix().toLower();
        QString randomName = generateRandomFileName(extension);
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
    
    // Store dialog settings for use in onEncryptionComplete
    m_dialogAutoplay = autoplay;
    m_dialogSkipIntro = skipIntro;
    m_dialogSkipOutro = skipOutro;
    m_dialogUseTMDB = useTMDB;
    
    qDebug() << "Operations_VP_Shows: Dialog settings - Autoplay:" << autoplay << "SkipIntro:" << skipIntro << "SkipOutro:" << skipOutro;
    
    // Start encryption with the show name and metadata
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, 
                                       m_mainWindow->user_Key, m_mainWindow->user_Username, 
                                       newLanguage, newTranslation, useTMDB, customPoster, customDescription);
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
        return;
    }
    
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
        return;
    }
    
    // Count seasons and episodes
    QDir showDir(m_contextMenuShowPath);
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
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
        return;
    }
    
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
        return;
    }
    
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
        return;
    }
    
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
        } else {
            qDebug() << "Operations_VP_Shows: Show folder deleted successfully";
        }
    } else {
        QMessageBox::warning(m_mainWindow,
                           tr("Deletion Error"),
                           tr("Some files could not be deleted. The show may be partially removed."));
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
        
        // Add extension
        QFileInfo sourceInfo(sourceFilePath);
        outputFileName += "." + sourceInfo.suffix();
        
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
        
        // Add extension
        QFileInfo sourceInfo(sourceFilePath);
        outputFileName += "." + sourceInfo.suffix();
        
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
    
    qDebug() << "Operations_VP_Shows: Episode context menu setup complete with multi-selection enabled";
}

void Operations_VP_Shows::showEpisodeContextMenu(const QPoint& pos)
{
    qDebug() << "Operations_VP_Shows: Episode context menu requested";
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
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

        // Add a separator after the mark watched action
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
    
    // Add Episodes action - show for categories or mixed selection, but not for pure episode selection
    if (hasCategories || (isMultiSelection && itemType != "episodes")) {
        QAction* addEpisodesAction = contextMenu->addAction(tr("Add Episodes"));
        connect(addEpisodesAction, &QAction::triggered, this, &Operations_VP_Shows::addEpisodesToShow);
    }
    
    // Decrypt and Export action
    QAction* exportAction;
    if (itemType == "episode" && !isMultiSelection) {
        exportAction = contextMenu->addAction(tr("Decrypt and Export"));
    } else {
        int episodeCount = m_contextMenuEpisodePaths.size();
        exportAction = contextMenu->addAction(tr("Decrypt and Export (%1 episode%2)").arg(episodeCount).arg(episodeCount > 1 ? "s" : ""));
    }
    connect(exportAction, &QAction::triggered, this, &Operations_VP_Shows::decryptAndExportEpisodeFromContextMenu);
    
    // Edit metadata action - only for single episode
    if (itemType == "episode" && !isMultiSelection) {
        contextMenu->addSeparator();
        QAction* editMetadataAction = contextMenu->addAction(tr("Edit metadata"));
        connect(editMetadataAction, &QAction::triggered, this, &Operations_VP_Shows::editEpisodeMetadata);
        contextMenu->addSeparator();
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

void Operations_VP_Shows::playEpisodeFromContextMenu()
{
    qDebug() << "Operations_VP_Shows: Play episode from context menu";
    
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
    
    // Perform the export
    performEpisodeExportWithWorker(m_contextMenuEpisodePaths, exportPath, description);
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
    
    // Store the current episode item for later expansion
    QTreeWidgetItem* currentEpisodeItem = m_contextMenuTreeItem;
    QString originalEpisodeText = currentEpisodeItem ? currentEpisodeItem->text(0) : QString();
    
    // Create and show the edit metadata dialog
    // The dialog will handle reading the current metadata internally
    VP_ShowsEditMetadataDialog dialog(videoFilePath, m_mainWindow->user_Key, m_mainWindow->user_Username, m_mainWindow);
    
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
        
        QMessageBox::information(m_mainWindow, tr("Success"),
                               tr("Metadata has been successfully updated."));
    } else {
        qDebug() << "Operations_VP_Shows: User cancelled metadata editing";
    }
}

void Operations_VP_Shows::deleteEpisodeFromContextMenu()
{
    qDebug() << "Operations_VP_Shows: Delete episodes from context menu";
    qDebug() << "Operations_VP_Shows: Episodes to delete:" << m_contextMenuEpisodePaths.size();
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episodes to delete";
        return;
    }
    
    // Build description for deletion
    QString description;
    if (m_contextMenuTreeItem) {
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
            videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                           << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
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
    
    // Iterate through all language versions (top-level items)
    for (int langIndex = 0; langIndex < treeWidget->topLevelItemCount(); ++langIndex) {
        QTreeWidgetItem* languageItem = treeWidget->topLevelItem(langIndex);
        
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

void Operations_VP_Shows::autoplayNextEpisode()
{
    qDebug() << "Operations_VP_Shows: Autoplay triggered";
    
    // Check if autoplay is enabled
    if (!m_currentShowSettings.autoplay) {
        qDebug() << "Operations_VP_Shows: Autoplay is disabled in settings";
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
        return;
    }
    
    // Find the next episode
    QString nextEpisodePath = findNextEpisode(m_currentPlayingEpisodePath);
    
    if (nextEpisodePath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No next episode available for autoplay";
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
    
    // Small delay to ensure the previous player is fully closed
    QTimer::singleShot(500, this, [this, nextEpisodePath, episodeName]() {
        // Decrypt and play the next episode
        decryptAndPlayEpisode(nextEpisodePath, episodeName);
        
        // Reset the autoplay flag
        m_isAutoplayInProgress = false;
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

void Operations_VP_Shows::performEpisodeExportWithWorker(const QStringList& episodePaths, const QString& exportPath, const QString& description)
{
    qDebug() << "Operations_VP_Shows: Preparing episode export with worker";
    qDebug() << "Operations_VP_Shows: Episodes to export:" << episodePaths.size();
    
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
        
        // Add extension
        QFileInfo sourceInfo(episodePath);
        outputFileName += "." + sourceInfo.suffix();
        
        QString outputFilePath = QDir(episodeFolderPath).absoluteFilePath(outputFileName);
        
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
    
    // Try to use watch history to find the last watched episode
    QString lastWatchedEpisode;
    
    if (m_watchHistory) {
        lastWatchedEpisode = m_watchHistory->getLastWatchedEpisode();
        qDebug() << "Operations_VP_Shows: Last watched episode from history:" << lastWatchedEpisode;
    }
    
    // If we have a last watched episode, try to find it
    if (!lastWatchedEpisode.isEmpty()) {
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
                qDebug() << "Operations_VP_Shows: Found last watched episode in tree";
                return found;
            }
        }
        
        qDebug() << "Operations_VP_Shows: Could not find last watched episode in tree widget";
    }
    
    qDebug() << "Operations_VP_Shows: No watch history found, looking for first episode";
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
