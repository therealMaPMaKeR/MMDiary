#include "operations_vp_shows.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "regularplayer/videoplayer.h"
#include <QDebug>
#include <QFileInfo>
#include <QTimer>

Operations_VP_Shows::Operations_VP_Shows(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    qDebug() << "Operations_VP_Shows: Constructor called";
    
    // TODO: Connect the debug button when it's added to the UI
    // connect(m_mainWindow->ui->pushButton_Debug, &QPushButton::clicked,
    //         this, &Operations_VP_Shows::on_pushButton_Debug_clicked);
}

Operations_VP_Shows::~Operations_VP_Shows()
{
    qDebug() << "Operations_VP_Shows: Destructor called";
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
