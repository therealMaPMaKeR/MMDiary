#include "operations_vp_shows.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "regularplayer/videoplayer.h"
#include "vp_shows_progressdialogs.h"
#include "vp_shows_metadata.h"
#include "vp_shows_settings_dialog.h"
#include "vp_shows_add_dialog.h"
#include "vp_shows_tmdb.h"
#include "operations_files.h"  // Add operations_files for secure file operations
#include "CryptoUtils.h"
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
#include <algorithm>

Operations_VP_Shows::Operations_VP_Shows(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_encryptionDialog(nullptr)
{
    qDebug() << "Operations_VP_Shows: Constructor called";
    
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
    
    // Connect double-click on shows list to display show details
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->listWidget_VP_List_List) {
        connect(m_mainWindow->ui->listWidget_VP_List_List, &QListWidget::itemDoubleClicked,
                this, &Operations_VP_Shows::onShowListItemDoubleClicked);
        qDebug() << "Operations_VP_Shows: Connected show list double-click handler";
        
        // Setup context menu for the list widget
        setupContextMenu();
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
    }
    
    // Load the TV shows list on initialization
    // We use a small delay to ensure the UI is fully initialized
    QTimer::singleShot(100, this, &Operations_VP_Shows::loadTVShowsList);
}

Operations_VP_Shows::~Operations_VP_Shows()
{
    qDebug() << "Operations_VP_Shows: Destructor called";
    
    // Force release and cleanup any playing video
    if (m_episodePlayer) {
        forceReleaseVideoFile();
        // Reset the player to ensure it's properly closed
        m_episodePlayer.reset();
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

void Operations_VP_Shows::testVideoPlayer()
{
    qDebug() << "Operations_VP_Shows: Testing video player";
    
    // Select a video file
    QString videoPath = selectVideoFile();
    
    if (videoPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Test cancelled - no file selected";
        return;
    }
    
    // Validate the video file
    if (!isValidVideoFile(videoPath)) {
        QMessageBox::warning(m_mainWindow, 
                           tr("Invalid Video File"),
                           tr("The selected file is not a valid video file."));
        return;
    }
    
    try {
        // Create video player if not exists
        if (!m_testVideoPlayer) {
            qDebug() << "Operations_VP_Shows: Creating new VideoPlayer instance";
            m_testVideoPlayer = std::make_unique<VideoPlayer>();
            
            // Connect error signal
            connect(m_testVideoPlayer.get(), &VideoPlayer::errorOccurred,
                    this, [this](const QString& error) {
                qDebug() << "Operations_VP_Shows: VideoPlayer error:" << error;
                QMessageBox::critical(m_mainWindow, 
                                    tr("Video Player Error"),
                                    error);
            });
        }
        
        // Load and play the video
        qDebug() << "Operations_VP_Shows: Loading video:" << videoPath;
        if (m_testVideoPlayer->loadVideo(videoPath)) {
            // Show the window first
            m_testVideoPlayer->show();
            
            // Raise and activate to ensure it's on top
            m_testVideoPlayer->raise();
            m_testVideoPlayer->activateWindow();
            
            // Start in fullscreen mode
            m_testVideoPlayer->startInFullScreen();
            
            // Add a small delay to ensure video widget is properly initialized
            QTimer::singleShot(100, [this]() {
                m_testVideoPlayer->play();
                qDebug() << "Operations_VP_Shows: Video loaded and playing in fullscreen";
            });
        } else {
            qDebug() << "Operations_VP_Shows: Failed to load video";
            QMessageBox::warning(m_mainWindow,
                               tr("Load Failed"),
                               tr("Failed to load the video file."));
        }
        
    } catch (const std::exception& e) {
        qDebug() << "Operations_VP_Shows: Exception caught:" << e.what();
        QMessageBox::critical(m_mainWindow,
                            tr("Video Player Error"),
                            tr("An error occurred: %1").arg(e.what()));
    }
}

void Operations_VP_Shows::on_pushButton_Debug_clicked()
{
    qDebug() << "Operations_VP_Shows: Debug button clicked";
    testVideoPlayer();
}

void Operations_VP_Shows::on_pushButton_VP_List_AddShow_clicked()
{
    qDebug() << "Operations_VP_Shows: Add Show button clicked";
    importTVShow();
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
    
    qDebug() << "Operations_VP_Shows: Show details - Name:" << showName 
             << "Language:" << language << "Translation:" << translationMode;
    
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
    
    // Start encryption with language and translation info
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, showName, encryptionKey, username, 
                                       language, translationMode);
}

QStringList Operations_VP_Shows::findVideoFiles(const QString& folderPath, bool recursive)
{
    QStringList videoFiles;
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", 
                                  "webm", "m4v", "mpg", "mpeg", "3gp"};
    
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
        videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                       << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
        
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
    // Generate a random filename with the original extension
    const QString chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const int nameLength = 32;
    
    QString randomName;
    for (int i = 0; i < nameLength; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        randomName.append(chars.at(index));
    }
    
    // Add extension if provided
    if (!extension.isEmpty()) {
        return randomName + "." + extension;
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
    
    if (success && !successfulFiles.isEmpty()) {
        // After successful encryption, check if we have TMDB data and save show description/image
        // The show folder path should be the directory of the first target file
        if (m_encryptionDialog) {
            // Get the target folder from the first successful encrypted file
            // Note: We need to get this info from somewhere - let's check if we can extract it
            // For now, we'll save the description after refreshing the list
            qDebug() << "Operations_VP_Shows: Import successful, TMDB data may have been saved";
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
        
        QMessageBox::information(m_mainWindow,
                               tr("Import Successful"),
                               successMessage);
        
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
                QListWidgetItem* item = new QListWidgetItem(metadata.showName);
                
                // Store the folder path as user data for later use (when playing videos)
                item->setData(Qt::UserRole, folderPath);
                
                // Store the mapping in RAM for quick access
                m_showFolderMapping[metadata.showName] = folderPath;
                
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

void Operations_VP_Shows::openSettings()
{
    qDebug() << "Operations_VP_Shows: Opening TV shows settings dialog";
    
    VP_ShowsSettingsDialog settingsDialog(m_mainWindow);
    
    // Show the dialog modally
    if (settingsDialog.exec() == QDialog::Accepted) {
        qDebug() << "Operations_VP_Shows: Settings saved";
        
        // Optionally refresh the shows list in case TMDB settings changed
        // This would be relevant if we display TMDB-sourced data in the list
        // For now, we just log that settings were saved
    } else {
        qDebug() << "Operations_VP_Shows: Settings dialog cancelled";
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
    
    // Store current show folder for later use
    m_currentShowFolder = showFolderPath;
    
    // Update the show name label
    if (m_mainWindow->ui->label_VP_Shows_Display_Name) {
        m_mainWindow->ui->label_VP_Shows_Display_Name->setText(showName);
    }
    
    // Load and display the show image
    if (m_mainWindow->ui->label_VP_Shows_Display_Image) {
        QPixmap showImage = loadShowImage(showFolderPath);
        
        if (!showImage.isNull()) {
            // Scale the image to fit the label (256x256)
            QPixmap scaledImage = showImage.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_mainWindow->ui->label_VP_Shows_Display_Image->setPixmap(scaledImage);
        } else {
            // Set a placeholder text if no image is available
            m_mainWindow->ui->label_VP_Shows_Display_Image->setText(tr("No Image Available"));
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
    
    // Switch to the display page
    if (m_mainWindow->ui->stackedWidget_VP_Shows) {
        m_mainWindow->ui->stackedWidget_VP_Shows->setCurrentIndex(1); // Switch to page_2 (display page)
        qDebug() << "Operations_VP_Shows: Switched to display page";
    }
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
    
    // Get all video files in the folder
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
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
    
    // Create metadata manager
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Map to organize episodes by language/translation, then season
    // Key: "Language Translation" (e.g., "English Dubbed"), Value: map of seasons
    QMap<QString, QMap<int, QList<QPair<int, QTreeWidgetItem*>>>> languageVersions;
    
    // Process each video file
    for (const QString& videoFile : videoFiles) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        
        // Read metadata from the file
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(videoPath, metadata)) {
            qDebug() << "Operations_VP_Shows: Failed to read metadata from:" << videoFile;
            continue;
        }
        
        // Parse season and episode numbers
        int seasonNum = metadata.season.toInt();
        int episodeNum = metadata.episode.toInt();
        
        // If season/episode numbers are invalid, try to parse from filename
        if (seasonNum == 0 || episodeNum == 0) {
            VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum);
        }
        
        // Default to season 1 if still no season number
        if (seasonNum == 0) {
            seasonNum = 1;
        }
        
        // Create language/translation key (e.g., "English Dubbed")
        QString languageKey = QString("%1 %2").arg(metadata.language).arg(metadata.translation);
        
        // Create episode item
        QTreeWidgetItem* episodeItem = new QTreeWidgetItem();
        
        // Format the episode name
        QString episodeName;
        if (!metadata.EPName.isEmpty()) {
            // Use TMDB episode name if available
            episodeName = QString("%1 - %2").arg(episodeNum).arg(metadata.EPName);
        } else {
            // Use original filename without extension as fallback
            QFileInfo fileInfo(metadata.filename);
            QString baseName = fileInfo.completeBaseName();
            episodeName = QString("%1 - %2").arg(episodeNum).arg(baseName);
        }
        
        episodeItem->setText(0, episodeName);
        
        // Store the file path in the item's data
        episodeItem->setData(0, Qt::UserRole, videoPath);
        
        // Create mapping key and store the mapping
        QString mappingKey = QString("%1_%2_S%3E%4")
            .arg(metadata.showName)
            .arg(languageKey)
            .arg(seasonNum, 2, 10, QChar('0'))
            .arg(episodeNum, 2, 10, QChar('0'));
        m_episodeFileMapping[mappingKey] = videoPath;
        
        // Add to language/season map
        languageVersions[languageKey][seasonNum].append(QPair<int, QTreeWidgetItem*>(episodeNum, episodeItem));
        
        qDebug() << "Operations_VP_Shows: Added episode" << languageKey 
                 << "S" << seasonNum << "E" << episodeNum << "-" << episodeName;
    }
    
    // Create language/translation items, then season items, then add episodes
    QList<QString> languageKeys = languageVersions.keys();
    std::sort(languageKeys.begin(), languageKeys.end());
    
    for (const QString& languageKey : languageKeys) {
        // Create language/translation item
        QTreeWidgetItem* languageItem = new QTreeWidgetItem();
        languageItem->setText(0, languageKey);
        
        // Get seasons for this language/translation
        QMap<int, QList<QPair<int, QTreeWidgetItem*>>>& seasons = languageVersions[languageKey];
        QList<int> seasonNumbers = seasons.keys();
        std::sort(seasonNumbers.begin(), seasonNumbers.end());
        
        for (int seasonNum : seasonNumbers) {
            // Create season item
            QTreeWidgetItem* seasonItem = new QTreeWidgetItem();
            seasonItem->setText(0, tr("Season %1").arg(seasonNum));
            
            // Sort episodes by episode number
            QList<QPair<int, QTreeWidgetItem*>>& episodes = seasons[seasonNum];
            std::sort(episodes.begin(), episodes.end(), 
                      [](const QPair<int, QTreeWidgetItem*>& a, const QPair<int, QTreeWidgetItem*>& b) {
                          return a.first < b.first;
                      });
            
            // Add episodes to season
            for (const auto& episode : episodes) {
                seasonItem->addChild(episode.second);
            }
            
            // Add season to language item
            languageItem->addChild(seasonItem);
            
            // Expand the season by default
            seasonItem->setExpanded(true);
        }
        
        // Add language item to tree widget
        m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList->addTopLevelItem(languageItem);
        
        // Expand the language item by default
        languageItem->setExpanded(true);
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
    
    // Clean up any existing temp file first
    cleanupTempFile();
    
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
    
    // Get the file extension from the encrypted file
    QFileInfo fileInfo(encryptedFilePath);
    QString extension = fileInfo.suffix().toLower();
    
    // Generate a random filename for the decrypted file
    QString randomName = generateRandomFileName(extension);
    QString decryptedFilePath = QDir(tempDecryptPath).absoluteFilePath(randomName);
    
    qDebug() << "Operations_VP_Shows: Decrypting to:" << decryptedFilePath;
    
    // Decrypt the file with metadata handling
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
        qDebug() << "Operations_VP_Shows: Creating new VideoPlayer instance for episode playback";
        m_episodePlayer = std::make_unique<VideoPlayer>();
        
        // Connect error signal
        connect(m_episodePlayer.get(), &VideoPlayer::errorOccurred,
                this, [this](const QString& error) {
            qDebug() << "Operations_VP_Shows: VideoPlayer error:" << error;
            QMessageBox::critical(m_mainWindow, 
                                tr("Video Player Error"),
                                error);
            // Clean up temp file on error
            cleanupTempFile();
        });
        
        // Connect playback state changed to clean up when stopped
        connect(m_episodePlayer.get(), &VideoPlayer::playbackStateChanged,
                this, [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState) {
                qDebug() << "Operations_VP_Shows: Playback stopped, scheduling cleanup";
                // Force the media player to release the file
                forceReleaseVideoFile();
                // Use a timer to delay cleanup to ensure file handle is fully released
                QTimer::singleShot(1000, this, &Operations_VP_Shows::cleanupTempFile);
            }
        });
        
        // Note: We can't connect to destroyed signal directly since unique_ptr manages the object
        // The cleanup will happen through playback state changes or when closing the window
    }
    
    // Load and play the video
    qDebug() << "Operations_VP_Shows: Loading decrypted video:" << decryptedFilePath;
    if (m_episodePlayer->loadVideo(decryptedFilePath)) {
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
        QTimer::singleShot(100, [this]() {
            if (m_episodePlayer) {
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
    
    QFile target(targetFile);
    if (!target.open(QIODevice::WriteOnly)) {
        qDebug() << "Operations_VP_Shows: Failed to open target file:" << target.errorString();
        source.close();
        return false;
    }
    
    // Create metadata manager to read the metadata
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Read and verify metadata (but don't write it to target)
    VP_ShowsMetadata::ShowMetadata metadata;
    if (!metadataManager.readFixedSizeEncryptedMetadata(&source, metadata)) {
        qDebug() << "Operations_VP_Shows: Failed to read metadata from encrypted file";
        source.close();
        target.close();
        target.remove();
        return false;
    }
    
    qDebug() << "Operations_VP_Shows: Read metadata - Show:" << metadata.showName 
             << "Episode:" << metadata.EPName;
    
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
    
    qDebug() << "Operations_VP_Shows: Successfully decrypted video to:" << targetFile;
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
        
        // Use operations_files secure delete for the temp file with 3 passes
        // Set allowExternalFiles to true since this is a temp file
        bool deleted = OperationsFiles::secureDelete(m_currentTempFile, 3, true);
        
        if (deleted) {
            qDebug() << "Operations_VP_Shows: Successfully deleted temp file";
        } else {
            qDebug() << "Operations_VP_Shows: Failed to secure delete temp file, trying regular delete";
            // Try regular delete as fallback
            if (QFile::remove(m_currentTempFile)) {
                qDebug() << "Operations_VP_Shows: Regular delete succeeded";
            } else {
                qDebug() << "Operations_VP_Shows: Regular delete also failed";
                // Schedule another attempt later
                QTimer::singleShot(2000, this, [this]() {
                    if (!m_currentTempFile.isEmpty() && QFile::exists(m_currentTempFile)) {
                        qDebug() << "Operations_VP_Shows: Retry deleting temp file";
                        QFile::remove(m_currentTempFile);
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
    
    // Check if we have the tree widget and a current show folder
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList) {
        qDebug() << "Operations_VP_Shows: Tree widget not available";
        return;
    }
    
    if (m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No current show folder set";
        return;
    }
    
    // TODO: In the future, we can implement watch history to resume from the last watched episode
    // For now, we'll just play the first episode of the first season of the first language version
    
    QTreeWidget* treeWidget = m_mainWindow->ui->treeWidget_VP_Shows_Display_EpisodeList;
    
    // Find the first episode (now it's Language->Season->Episode)
    if (treeWidget->topLevelItemCount() > 0) {
        QTreeWidgetItem* firstLanguage = treeWidget->topLevelItem(0);
        if (firstLanguage && firstLanguage->childCount() > 0) {
            QTreeWidgetItem* firstSeason = firstLanguage->child(0);
            if (firstSeason && firstSeason->childCount() > 0) {
                QTreeWidgetItem* firstEpisode = firstSeason->child(0);
                if (firstEpisode) {
                    // Simulate double-click on the first episode
                    onEpisodeDoubleClicked(firstEpisode, 0);
                } else {
                    qDebug() << "Operations_VP_Shows: No episodes found in first season";
                }
            } else {
                qDebug() << "Operations_VP_Shows: First season has no episodes";
            }
        } else {
            qDebug() << "Operations_VP_Shows: First language version has no seasons";
        }
    } else {
        qDebug() << "Operations_VP_Shows: No language versions found in tree widget";
        QMessageBox::information(m_mainWindow,
                               tr("No Episodes"),
                               tr("This show has no episodes to play."));
    }
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
    qDebug() << "Operations_VP_Shows: Add episodes to show:" << m_contextMenuShowName;
    
    if (m_contextMenuShowName.isEmpty() || m_contextMenuShowPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show selected for adding episodes";
        return;
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
    QDir showDir(m_contextMenuShowPath);
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
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
    VP_ShowsAddDialog addDialog(m_contextMenuShowName, m_mainWindow);
    
    // Set the show name field as read-only since we're adding to an existing show
    addDialog.setShowNameReadOnly(true);
    addDialog.setWindowTitle(tr("Add Episodes to %1").arg(m_contextMenuShowName));
    
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
                                                 m_contextMenuShowName, newLanguage, newTranslation);
    
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
    
    // Create and show progress dialog
    if (!m_encryptionDialog) {
        m_encryptionDialog = new VP_ShowsEncryptionProgressDialog(m_mainWindow);
        connect(m_encryptionDialog, &VP_ShowsEncryptionProgressDialog::encryptionComplete,
                this, &Operations_VP_Shows::onEncryptionComplete);
    }
    
    // Start encryption with the show name and metadata
    m_encryptionDialog->startEncryption(filesToImport, targetFiles, m_contextMenuShowName, 
                                       m_mainWindow->user_Key, m_mainWindow->user_Username, 
                                       newLanguage, newTranslation);
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
        
        // Use secure delete with 3 passes for each file
        if (!OperationsFiles::secureDelete(filePath, 3)) {
            qDebug() << "Operations_VP_Shows: Failed to securely delete file:" << file;
            // Try regular delete as fallback
            if (!QFile::remove(filePath)) {
                qDebug() << "Operations_VP_Shows: Failed to delete file:" << file;
                allDeleted = false;
            }
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
    
    QMessageBox::information(m_mainWindow,
                           tr("Show Deleted"),
                           tr("The show \"%1\" has been deleted from your library.").arg(m_contextMenuShowName));
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
    
    // Get all video files
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
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
        
        // Create season folder with zero-padded number
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
        
        QString seasonPath = languageDir.absoluteFilePath(seasonFolderName);
        
        // Generate output filename
        // episodeNum was already extracted above
        QString outputFileName;
        
        if (episodeNum > 0) {
            outputFileName = QString("%1_S%2E%3")
                           .arg(showName)
                           .arg(seasonNum, 2, 10, QChar('0'))
                           .arg(episodeNum, 2, 10, QChar('0'));
            
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
        
        QString outputFilePath = QDir(seasonPath).absoluteFilePath(outputFileName);
        
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
    
    // Get all video files
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.mpg" << "*.mpeg" << "*.3gp";
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
        
        // Create season folder path with zero-padded number
        QString seasonFolderName = QString("Season %1").arg(seasonNum, 2, 10, QChar('0'));
        if (!languageDir.exists(seasonFolderName)) {
            if (!languageDir.mkdir(seasonFolderName)) {
                qDebug() << "Operations_VP_Shows: Failed to create season folder:" << seasonFolderName;
                continue;
            }
        }
        
        QString seasonPath = languageDir.absoluteFilePath(seasonFolderName);
        
        // Generate output filename
        QString outputFileName;
        
        if (episodeNum > 0) {
            outputFileName = QString("%1_S%2E%3")
                           .arg(showName)
                           .arg(seasonNum, 2, 10, QChar('0'))
                           .arg(episodeNum, 2, 10, QChar('0'));
            
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
        
        QString outputFilePath = QDir(seasonPath).absoluteFilePath(outputFileName);
        
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
            QMessageBox::information(m_mainWindow,
                                   tr("Export Complete"),
                                   tr("The show \"%1\" has been successfully exported.").arg(showName));
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
