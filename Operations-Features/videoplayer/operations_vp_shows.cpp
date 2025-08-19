#include "operations_vp_shows.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "regularplayer/videoplayer.h"
#include "vp_shows_progressdialogs.h"
#include "vp_shows_metadata.h"
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QRandomGenerator>
#include <QStandardPaths>

Operations_VP_Shows::Operations_VP_Shows(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_encryptionDialog(nullptr)
{
    qDebug() << "Operations_VP_Shows: Constructor called";
    
    // Connect the Add Show button if it exists
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->pushButton_VP_List_AddShow) {
        connect(m_mainWindow->ui->pushButton_VP_List_AddShow, &QPushButton::clicked,
                this, &Operations_VP_Shows::on_pushButton_VP_List_AddShow_clicked);
        qDebug() << "Operations_VP_Shows: Connected Add Show button";
    }
}

Operations_VP_Shows::~Operations_VP_Shows()
{
    qDebug() << "Operations_VP_Shows: Destructor called";
    if (m_encryptionDialog) {
        delete m_encryptionDialog;
    }
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
    
    // Get the show name from the folder name
    QDir selectedDir(folderPath);
    QString showName = selectedDir.dirName();
    
    // Find all video files in the folder and subfolders
    QStringList videoFiles = findVideoFiles(folderPath);
    
    if (videoFiles.isEmpty()) {
        QMessageBox::warning(m_mainWindow,
                           tr("No Video Files Found"),
                           tr("The selected folder does not contain any compatible video files."));
        return;
    }
    
    qDebug() << "Operations_VP_Shows: Found" << videoFiles.size() << "video files";
    
    // Create the output folder structure
    QString outputPath;
    if (!createShowFolderStructure(outputPath)) {
        QMessageBox::critical(m_mainWindow,
                            tr("Folder Creation Failed"),
                            tr("Failed to create the necessary folder structure."));
        return;
    }
    
    // Generate random filenames for each video file
    QStringList targetFiles;
    for (const QString& sourceFile : videoFiles) {
        QFileInfo fileInfo(sourceFile);
        QString extension = fileInfo.suffix().toLower();
        QString randomName = generateRandomFileName(extension);
        QString targetFile = outputPath + "/" + randomName;
        targetFiles.append(targetFile);
    }
    
    // Get encryption key and username from mainwindow
    QByteArray encryptionKey = m_mainWindow->user_Key;
    QString username = m_mainWindow->user_Username;
    
    // Create and show progress dialog
    if (!m_encryptionDialog) {
        m_encryptionDialog = new VP_ShowsEncryptionProgressDialog(m_mainWindow);
        connect(m_encryptionDialog, &VP_ShowsEncryptionProgressDialog::encryptionComplete,
                this, &Operations_VP_Shows::onEncryptionComplete);
    }
    
    // Start encryption
    m_encryptionDialog->startEncryption(videoFiles, targetFiles, showName, encryptionKey, username);
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
    
    return randomName + "." + extension;
}

bool Operations_VP_Shows::createShowFolderStructure(QString& outputPath)
{
    // Get user data folder
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Failed to get app data location";
        return false;
    }
    
    // Create Videoplayer folder if it doesn't exist
    QDir dataDir(dataPath);
    if (!dataDir.exists("Videoplayer")) {
        if (!dataDir.mkdir("Videoplayer")) {
            qDebug() << "Operations_VP_Shows: Failed to create Videoplayer folder";
            return false;
        }
    }
    
    // Create Shows folder if it doesn't exist
    QDir videoplayerDir(dataPath + "/Videoplayer");
    if (!videoplayerDir.exists("Shows")) {
        if (!videoplayerDir.mkdir("Shows")) {
            qDebug() << "Operations_VP_Shows: Failed to create Shows folder";
            return false;
        }
    }
    
    // Generate a random folder name for this show
    QString showsPath = dataPath + "/Videoplayer/Shows";
    QString randomFolderName = generateRandomFileName("");
    randomFolderName = randomFolderName.left(randomFolderName.length() - 1); // Remove the dot
    
    QDir showsDir(showsPath);
    if (!showsDir.mkdir(randomFolderName)) {
        qDebug() << "Operations_VP_Shows: Failed to create show folder:" << randomFolderName;
        return false;
    }
    
    outputPath = showsPath + "/" + randomFolderName;
    qDebug() << "Operations_VP_Shows: Created output folder:" << outputPath;
    
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
    
    if (success) {
        QMessageBox::information(m_mainWindow,
                               tr("Import Successful"),
                               tr("TV show imported successfully!\n%1").arg(message));
        
        // TODO: Refresh the show list in the UI
    } else {
        QString detailedMessage = message;
        if (!failedFiles.isEmpty()) {
            detailedMessage += "\n\nFailed files:\n";
            for (const QString& file : failedFiles) {
                QFileInfo fileInfo(file);
                detailedMessage += "- " + fileInfo.fileName() + "\n";
            }
        }
        
        QMessageBox::warning(m_mainWindow,
                           tr("Import Partially Failed"),
                           detailedMessage);
    }
}
