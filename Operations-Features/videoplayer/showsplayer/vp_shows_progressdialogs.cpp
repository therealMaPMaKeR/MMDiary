#include "vp_shows_progressdialogs.h"
#include "../../Operations-Global/SafeTimer.h"
#include "vp_shows_encryptionworkers.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QMessageBox>
#include <QApplication>
#include <QTextEdit>
#include <QDateTime>
#include <QCloseEvent>

//---------------- VP_ShowsEncryptionProgressDialog ----------------//

VP_ShowsEncryptionProgressDialog::VP_ShowsEncryptionProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: Constructor called";
    setupUI();
}

VP_ShowsEncryptionProgressDialog::~VP_ShowsEncryptionProgressDialog()
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: Destructor called";
    cleanup();
}

void VP_ShowsEncryptionProgressDialog::setupUI()
{
    setWindowTitle("Importing TV Show");
    setModal(true);
    setMinimumWidth(400);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Preparing to import files...", this);
    mainLayout->addWidget(m_statusLabel);
    
    // File label
    m_fileLabel = new QLabel("", this);
    mainLayout->addWidget(m_fileLabel);
    
    // Overall progress bar
    QLabel* overallLabel = new QLabel("Overall Progress:", this);
    mainLayout->addWidget(overallLabel);
    
    m_overallProgressBar = new QProgressBar(this);
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(0);
    mainLayout->addWidget(m_overallProgressBar);
    
    // File progress bar
    QLabel* fileProgressLabel = new QLabel("Current File Progress:", this);
    mainLayout->addWidget(fileProgressLabel);
    
    m_fileProgressBar = new QProgressBar(this);
    m_fileProgressBar->setRange(0, 100);
    m_fileProgressBar->setValue(0);
    mainLayout->addWidget(m_fileProgressBar);
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &VP_ShowsEncryptionProgressDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    setLayout(mainLayout);
}

void VP_ShowsEncryptionProgressDialog::startEncryption(const QStringList& sourceFiles,
                                                       const QStringList& targetFiles,
                                                       const QString& showName,
                                                       const QByteArray& encryptionKey,
                                                       const QString& username,
                                                       const QString& language,
                                                       const QString& translation,
                                                       bool useTMDB,
                                                       const QPixmap& customPoster,
                                                       const QString& customDescription,
                                                       VP_ShowsEncryptionWorker::ParseMode parseMode,
                                                       int showId)
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: Starting encryption for" << sourceFiles.size() << "files";
    qDebug() << "VP_ShowsEncryptionProgressDialog: Using TMDB:" << useTMDB;
    qDebug() << "VP_ShowsEncryptionProgressDialog: Has custom poster:" << !customPoster.isNull();
    qDebug() << "VP_ShowsEncryptionProgressDialog: Has custom description:" << !customDescription.isEmpty();
    qDebug() << "VP_ShowsEncryptionProgressDialog: Parse mode:" << (parseMode == VP_ShowsEncryptionWorker::ParseFromFolder ? "Folder" : "File");
    qDebug() << "VP_ShowsEncryptionProgressDialog: Show ID:" << showId;
    
    // Clean up any previous operation
    cleanup();
    
    // Reset UI
    m_overallProgressBar->setValue(0);
    m_fileProgressBar->setValue(0);
    m_statusLabel->setText(QString("Importing %1 files for show: %2").arg(sourceFiles.size()).arg(showName));
    m_fileLabel->setText("");
    m_cancelButton->setEnabled(true);
    
    // Create worker and thread
    m_workerThread = new QThread();
    m_worker = new VP_ShowsEncryptionWorker(sourceFiles, targetFiles, showName, encryptionKey, username, 
                                           language, translation, useTMDB, customPoster, customDescription, parseMode, showId);
    
    // Move worker to thread
    m_worker->moveToThread(m_workerThread);
    
    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &VP_ShowsEncryptionWorker::doEncryption);
    
    connect(m_worker, &VP_ShowsEncryptionWorker::progressUpdated,
            this, &VP_ShowsEncryptionProgressDialog::onProgressUpdated);
    
    connect(m_worker, &VP_ShowsEncryptionWorker::fileProgressUpdate,
            this, &VP_ShowsEncryptionProgressDialog::onFileProgressUpdate);
    
    connect(m_worker, &VP_ShowsEncryptionWorker::currentFileProgressUpdated,
            this, &VP_ShowsEncryptionProgressDialog::onCurrentFileProgressUpdated);
    
    connect(m_worker, &VP_ShowsEncryptionWorker::encryptionFinished,
            this, &VP_ShowsEncryptionProgressDialog::onEncryptionFinished);
    
    connect(m_worker, &VP_ShowsEncryptionWorker::encryptionFinished,
            m_workerThread, &QThread::quit);
    
    // Don't use deleteLater - we'll handle cleanup manually in cleanup()
    // This prevents use-after-free issues
    
    // Start the thread
    m_workerThread->start();
    
    // Show the dialog
    show();
}

void VP_ShowsEncryptionProgressDialog::onProgressUpdated(int percentage)
{
    m_overallProgressBar->setValue(percentage);
}

void VP_ShowsEncryptionProgressDialog::onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName)
{
    m_fileLabel->setText(QString("File %1 of %2: %3").arg(currentFile).arg(totalFiles).arg(fileName));
    m_fileProgressBar->setValue(0); // Reset for new file
}

void VP_ShowsEncryptionProgressDialog::onCurrentFileProgressUpdated(int percentage)
{
    m_fileProgressBar->setValue(percentage);
}

void VP_ShowsEncryptionProgressDialog::onEncryptionFinished(bool success, const QString& errorMessage,
                                                            const QStringList& successfulFiles,
                                                            const QStringList& failedFiles)
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: Encryption finished. Success:" << success;
    
    m_cancelButton->setEnabled(false);
    
    if (success) {
        m_statusLabel->setText("Import completed successfully!");
        m_overallProgressBar->setValue(100);
        m_fileProgressBar->setValue(100);
    } else {
        m_statusLabel->setText("Import failed: " + errorMessage);
    }
    
    // Emit completion signal
    emit encryptionComplete(success, errorMessage, successfulFiles, failedFiles);
    
    // Close dialog after a short delay
    SafeTimer::singleShot(success ? 1000 : 2000, this, [this]() { accept(); }, "VP_ShowsEncryptionProgressDialog");
}

void VP_ShowsEncryptionProgressDialog::onCancelClicked()
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: Cancel clicked";
    
    m_cancelButton->setEnabled(false);
    m_statusLabel->setText("Cancelling...");
    
    if (m_worker) {
        m_worker->cancel();
    }
}

void VP_ShowsEncryptionProgressDialog::cleanup()
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: cleanup() called";
    
    // Use the standardized cleanup helper
    bool cleanShutdown = WorkerThreadCleanupHelper::cleanupWorkerThread(
        m_worker, m_workerThread, "VP_ShowsEncryptionProgressDialog");
    
    if (!cleanShutdown) {
        qWarning() << "VP_ShowsEncryptionProgressDialog: Had to force terminate thread during cleanup";
    }
    
    qDebug() << "VP_ShowsEncryptionProgressDialog: cleanup() completed";
}

//---------------- VP_ShowsDecryptionProgressDialog ----------------//

VP_ShowsDecryptionProgressDialog::VP_ShowsDecryptionProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
    qDebug() << "VP_ShowsDecryptionProgressDialog: Constructor called";
    setupUI();
}

VP_ShowsDecryptionProgressDialog::~VP_ShowsDecryptionProgressDialog()
{
    qDebug() << "VP_ShowsDecryptionProgressDialog: Destructor called";
    cleanup();
}

void VP_ShowsDecryptionProgressDialog::setupUI()
{
    setWindowTitle("Preparing Video");
    setModal(true);
    setMinimumWidth(350);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Decrypting video file...", this);
    mainLayout->addWidget(m_statusLabel);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &VP_ShowsDecryptionProgressDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    setLayout(mainLayout);
}

void VP_ShowsDecryptionProgressDialog::startDecryption(const QString& sourceFile,
                                                       const QString& targetFile,
                                                       const QByteArray& encryptionKey,
                                                       const QString& username)
{
    qDebug() << "VP_ShowsDecryptionProgressDialog: Starting decryption";
    
    m_targetFile = targetFile;
    
    // Clean up any previous operation
    cleanup();
    
    // Reset UI
    m_progressBar->setValue(0);
    m_statusLabel->setText("Decrypting video file...");
    m_cancelButton->setEnabled(true);
    
    // Create worker and thread
    m_workerThread = new QThread();
    m_worker = new VP_ShowsDecryptionWorker(sourceFile, targetFile, encryptionKey, username);
    
    // Move worker to thread
    m_worker->moveToThread(m_workerThread);
    
    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &VP_ShowsDecryptionWorker::doDecryption);
    
    connect(m_worker, &VP_ShowsDecryptionWorker::progressUpdated,
            this, &VP_ShowsDecryptionProgressDialog::onProgressUpdated);
    
    connect(m_worker, &VP_ShowsDecryptionWorker::decryptionFinished,
            this, &VP_ShowsDecryptionProgressDialog::onDecryptionFinished);
    
    connect(m_worker, &VP_ShowsDecryptionWorker::decryptionFinished,
            m_workerThread, &QThread::quit);
    
    // Don't use deleteLater - we'll handle cleanup manually in cleanup()
    // This prevents use-after-free issues
    
    // Start the thread
    m_workerThread->start();
    
    // Show the dialog
    show();
}

void VP_ShowsDecryptionProgressDialog::onProgressUpdated(int percentage)
{
    m_progressBar->setValue(percentage);
}

void VP_ShowsDecryptionProgressDialog::onDecryptionFinished(bool success, const QString& errorMessage)
{
    qDebug() << "VP_ShowsDecryptionProgressDialog: Decryption finished. Success:" << success;
    
    m_cancelButton->setEnabled(false);
    
    if (success) {
        m_statusLabel->setText("Video ready!");
        m_progressBar->setValue(100);
    } else {
        m_statusLabel->setText("Failed: " + errorMessage);
    }
    
    // Emit completion signal
    emit decryptionComplete(success, m_targetFile, errorMessage);
    
    // Close dialog
    if (success) {
        accept();
    } else {
        SafeTimer::singleShot(2000, this, [this]() { reject(); }, "VP_ShowsDecryptionProgressDialog");
    }
}

void VP_ShowsDecryptionProgressDialog::onCancelClicked()
{
    qDebug() << "VP_ShowsDecryptionProgressDialog: Cancel clicked";
    
    m_cancelButton->setEnabled(false);
    m_statusLabel->setText("Cancelling...");
    
    if (m_worker) {
        m_worker->cancel();
    }
}

void VP_ShowsDecryptionProgressDialog::cleanup()
{
    qDebug() << "VP_ShowsDecryptionProgressDialog: cleanup() called";
    
    // Use the standardized cleanup helper
    bool cleanShutdown = WorkerThreadCleanupHelper::cleanupWorkerThread(
        m_worker, m_workerThread, "VP_ShowsDecryptionProgressDialog");
    
    if (!cleanShutdown) {
        qWarning() << "VP_ShowsDecryptionProgressDialog: Had to force terminate thread during cleanup";
    }
    
    qDebug() << "VP_ShowsDecryptionProgressDialog: cleanup() completed";
}

// SafeTimer is already included for delayed close

//---------------- VP_ShowsExportProgressDialog ----------------//

VP_ShowsExportProgressDialog::VP_ShowsExportProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
    qDebug() << "VP_ShowsExportProgressDialog: Constructor called";
    setupUI();
}

VP_ShowsExportProgressDialog::~VP_ShowsExportProgressDialog()
{
    qDebug() << "VP_ShowsExportProgressDialog: Destructor called";
    cleanup();
}

void VP_ShowsExportProgressDialog::setupUI()
{
    setWindowTitle("Exporting TV Show");
    setModal(true);
    setMinimumWidth(450);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Preparing to export files...", this);
    mainLayout->addWidget(m_statusLabel);
    
    // File label
    m_fileLabel = new QLabel("", this);
    mainLayout->addWidget(m_fileLabel);
    
    // Warning label (initially hidden)
    m_warningLabel = new QLabel("", this);
    m_warningLabel->setStyleSheet("QLabel { color: #FF8800; }"); // Orange color for warnings
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    mainLayout->addWidget(m_warningLabel);
    
    mainLayout->addSpacing(10);
    
    // Overall progress
    m_overallLabel = new QLabel("Overall Progress:", this);
    mainLayout->addWidget(m_overallLabel);
    
    m_overallProgressBar = new QProgressBar(this);
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setTextVisible(true);
    mainLayout->addWidget(m_overallProgressBar);
    
    mainLayout->addSpacing(10);
    
    // Current file progress
    QLabel* fileProgressLabel = new QLabel("Current File Progress:", this);
    mainLayout->addWidget(fileProgressLabel);
    
    m_currentFileProgressBar = new QProgressBar(this);
    m_currentFileProgressBar->setRange(0, 100);
    m_currentFileProgressBar->setValue(0);
    m_currentFileProgressBar->setTextVisible(true);
    mainLayout->addWidget(m_currentFileProgressBar);
    
    mainLayout->addSpacing(10);
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &VP_ShowsExportProgressDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    setLayout(mainLayout);
}

void VP_ShowsExportProgressDialog::startExport(const QList<VP_ShowsExportWorker::ExportFileInfo>& files,
                                              const QByteArray& encryptionKey,
                                              const QString& username,
                                              const QString& showName)
{
    qDebug() << "VP_ShowsExportProgressDialog: Starting export for" << files.size() << "files";
    
    m_showName = showName;
    m_warnings.clear();
    
    // Clean up any previous operation
    cleanup();
    
    // Reset UI
    m_overallProgressBar->setValue(0);
    m_currentFileProgressBar->setValue(0);
    m_statusLabel->setText(QString("Exporting %1 (%2 files)").arg(showName).arg(files.size()));
    m_statusLabel->setStyleSheet("");  // Reset any custom styling
    m_fileLabel->setText("");
    m_warningLabel->setText("");
    m_warningLabel->setVisible(false);
    m_cancelButton->setEnabled(true);
    
    // Create worker and thread
    m_workerThread = new QThread();
    m_worker = new VP_ShowsExportWorker(files, encryptionKey, username);
    
    // Move worker to thread
    m_worker->moveToThread(m_workerThread);
    
    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &VP_ShowsExportWorker::doExport);
    
    connect(m_worker, &VP_ShowsExportWorker::overallProgressUpdated,
            this, &VP_ShowsExportProgressDialog::onOverallProgressUpdated);
    
    connect(m_worker, &VP_ShowsExportWorker::currentFileProgressUpdated,
            this, &VP_ShowsExportProgressDialog::onCurrentFileProgressUpdated);
    
    connect(m_worker, &VP_ShowsExportWorker::fileProgressUpdate,
            this, &VP_ShowsExportProgressDialog::onFileProgressUpdate);
    
    connect(m_worker, &VP_ShowsExportWorker::fileExportWarning,
            this, &VP_ShowsExportProgressDialog::onFileExportWarning);
    
    connect(m_worker, &VP_ShowsExportWorker::exportFinished,
            this, &VP_ShowsExportProgressDialog::onExportFinished);
    
    connect(m_worker, &VP_ShowsExportWorker::exportFinished,
            m_workerThread, &QThread::quit);
    
    // Don't use deleteLater - we'll handle cleanup manually in cleanup()
    // This prevents use-after-free issues
    
    // Start the thread
    m_workerThread->start();
    
    // Show the dialog
    show();
}

void VP_ShowsExportProgressDialog::onOverallProgressUpdated(int percentage)
{
    m_overallProgressBar->setValue(percentage);
}

void VP_ShowsExportProgressDialog::onCurrentFileProgressUpdated(int percentage)
{
    m_currentFileProgressBar->setValue(percentage);
}

void VP_ShowsExportProgressDialog::onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName)
{
    m_fileLabel->setText(QString("File %1 of %2: %3").arg(currentFile).arg(totalFiles).arg(fileName));
    m_currentFileProgressBar->setValue(0); // Reset for new file
}

void VP_ShowsExportProgressDialog::onFileExportWarning(const QString& fileName, const QString& warningMessage)
{
    qDebug() << "VP_ShowsExportProgressDialog: Warning for file" << fileName << ":" << warningMessage;
    
    // Add warning to list
    m_warnings.append(fileName);
    
    // Update the warning label with count
    if (m_warnings.size() == 1) {
        m_warningLabel->setText(QString("⚠ 1 file skipped (already exists in target folder)"));
    } else {
        m_warningLabel->setText(QString("⚠ %1 files skipped (already exist in target folder)").arg(m_warnings.size()));
    }
    m_warningLabel->setVisible(true);
}

void VP_ShowsExportProgressDialog::onExportFinished(bool success, const QString& errorMessage,
                                                   const QStringList& successfulFiles,
                                                   const QStringList& failedFiles)
{
    qDebug() << "VP_ShowsExportProgressDialog: Export finished. Success:" << success;
    
    m_cancelButton->setEnabled(false);
    
    // Check if the error message indicates all files were skipped
    bool allFilesSkipped = errorMessage.contains("All") && errorMessage.contains("already exist");
    
    if (success) {
        m_statusLabel->setText(errorMessage);  // The error message contains the success summary
        m_overallProgressBar->setValue(100);
        m_currentFileProgressBar->setValue(100);
        
        // Update warning label if files were skipped
        if (!m_warnings.isEmpty()) {
            if (m_warnings.size() == 1) {
                m_warningLabel->setText(QString("⚠ 1 file was not exported (already exists in target folder)"));
            } else {
                m_warningLabel->setText(QString("⚠ %1 files were not exported (already exist in target folder)").arg(m_warnings.size()));
            }
            m_warningLabel->setVisible(true);
        }
    } else if (allFilesSkipped) {
        // Special case: all files were skipped due to duplicates
        m_statusLabel->setText(errorMessage);
        m_statusLabel->setStyleSheet("QLabel { color: #FF8800; }");  // Orange for warning
        m_overallProgressBar->setValue(100);  // Set to 100 since we checked all files
        m_currentFileProgressBar->setValue(100);
        
        // Warning label already shows the count during processing
        if (!m_warnings.isEmpty()) {
            m_warningLabel->setText(QString("⚠ No files exported - all %1 files already exist in the target folder").arg(m_warnings.size()));
            m_warningLabel->setVisible(true);
        }
    } else {
        // Export failed for other reasons
        m_statusLabel->setText(errorMessage);
        m_statusLabel->setStyleSheet("QLabel { color: #FF0000; }");  // Red for error
    }
    
    // Emit completion signal
    emit exportComplete(success, errorMessage, successfulFiles, failedFiles);
    
    // Close dialog after a delay
    // Longer delay if there were warnings or if all files were skipped
    int delay = 1500;
    if (!m_warnings.isEmpty() || allFilesSkipped) {
        delay = 4000;  // Give more time to read the warning
    } else if (!success) {
        delay = 3000;
    }
    
    SafeTimer::singleShot(delay, this, [this]() { accept(); }, "VP_ShowsExportProgressDialog");
}

void VP_ShowsExportProgressDialog::onCancelClicked()
{
    qDebug() << "VP_ShowsExportProgressDialog: Cancel clicked";
    
    m_cancelButton->setEnabled(false);
    m_statusLabel->setText("Cancelling export...");
    
    if (m_worker) {
        m_worker->cancel();
    }
}

void VP_ShowsExportProgressDialog::cleanup()
{
    qDebug() << "VP_ShowsExportProgressDialog: cleanup() called";
    
    // Use the standardized cleanup helper
    bool cleanShutdown = WorkerThreadCleanupHelper::cleanupWorkerThread(
        m_worker, m_workerThread, "VP_ShowsExportProgressDialog");
    
    if (!cleanShutdown) {
        qWarning() << "VP_ShowsExportProgressDialog: Had to force terminate thread during cleanup";
    }
    
    qDebug() << "VP_ShowsExportProgressDialog: cleanup() completed";
}

//---------------- VP_ShowsTMDBReacquisitionDialog ----------------//

VP_ShowsTMDBReacquisitionDialog::VP_ShowsTMDBReacquisitionDialog(QWidget* parent)
    : QDialog(parent)
    , m_cancelled(false)
    , m_totalEpisodes(0)
    , m_currentEpisode(0)
{
    qDebug() << "VP_ShowsTMDBReacquisitionDialog: Constructor called";
    
    setWindowTitle("Re-acquiring TMDB Data");
    setModal(true);
    setMinimumWidth(500);
    setMinimumHeight(400);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Preparing to fetch TMDB data...", this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);
    
    // Current item label
    m_currentItemLabel = new QLabel("", this);
    m_currentItemLabel->setWordWrap(true);
    mainLayout->addWidget(m_currentItemLabel);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);
    
    // Log widget (collapsible)
    QLabel* logLabel = new QLabel("Operation Log:", this);
    mainLayout->addWidget(logLabel);
    
    m_logWidget = new QTextEdit(this);
    m_logWidget->setReadOnly(true);
    m_logWidget->setMaximumHeight(150);
    m_logWidget->setStyleSheet("QTextEdit { background-color: #2b2b2b; color: #ffffff; font-family: monospace; }");
    mainLayout->addWidget(m_logWidget);
    
    // Spacer
    mainLayout->addStretch();
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &VP_ShowsTMDBReacquisitionDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

VP_ShowsTMDBReacquisitionDialog::~VP_ShowsTMDBReacquisitionDialog()
{
    qDebug() << "VP_ShowsTMDBReacquisitionDialog: Destructor called";
}

void VP_ShowsTMDBReacquisitionDialog::setTotalEpisodes(int total)
{
    m_totalEpisodes = total;
    m_currentEpisode = 0;
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(0);
    
    appendLog(QString("Total episodes to process: %1").arg(total));
}

void VP_ShowsTMDBReacquisitionDialog::updateProgress(int current, const QString& episodeName)
{
    m_currentEpisode = current;
    m_progressBar->setValue(current);
    
    int percentage = (m_totalEpisodes > 0) ? (current * 100 / m_totalEpisodes) : 0;
    m_statusLabel->setText(QString("Processing: %1/%2 (%3%)").arg(current).arg(m_totalEpisodes).arg(percentage));
    m_currentItemLabel->setText(QString("Current: %1").arg(episodeName));
    
    appendLog(QString("[%1/%2] Processing: %3").arg(current).arg(m_totalEpisodes).arg(episodeName));
}

void VP_ShowsTMDBReacquisitionDialog::setStatusMessage(const QString& message)
{
    m_statusLabel->setText(message);
    appendLog(message);
}

void VP_ShowsTMDBReacquisitionDialog::showRateLimitMessage(int retryInSeconds)
{
    QString message = QString("Rate limit reached. Retrying in %1 seconds...").arg(retryInSeconds);
    m_currentItemLabel->setText(message);
    m_currentItemLabel->setStyleSheet("QLabel { color: #ff9900; }");
    appendLog(QString("[RATE LIMIT] %1").arg(message));
    
    // Reset style after a moment
    SafeTimer::singleShot(retryInSeconds * 1000, this, [this]() {
        m_currentItemLabel->setStyleSheet("");
    }, "VP_ShowsTMDBReacquisitionDialog");
}

void VP_ShowsTMDBReacquisitionDialog::onCancelClicked()
{
    qDebug() << "VP_ShowsTMDBReacquisitionDialog: Cancel requested";
    
    int result = QMessageBox::question(this,
                                      "Cancel Operation",
                                      "Are you sure you want to cancel the TMDB data acquisition?\n\n"
                                      "Progress will be lost and you'll need to start over.",
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);
    
    if (result == QMessageBox::Yes) {
        m_cancelled = true;
        appendLog("[CANCELLED] Operation cancelled by user");
        emit cancelRequested();
        reject();
    }
}

void VP_ShowsTMDBReacquisitionDialog::appendLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logWidget->append(QString("[%1] %2").arg(timestamp).arg(message));
}

void VP_ShowsTMDBReacquisitionDialog::closeEvent(QCloseEvent* event)
{
    qDebug() << "VP_ShowsTMDBReacquisitionDialog: closeEvent - dialog being closed";
    
    if (!m_cancelled) {
        m_cancelled = true;
        appendLog("[CANCELLED] Operation cancelled by closing dialog");
        emit cancelRequested();
    }
    
    event->accept();
}

void VP_ShowsTMDBReacquisitionDialog::reject()
{
    qDebug() << "VP_ShowsTMDBReacquisitionDialog: reject - dialog being rejected";
    
    if (!m_cancelled) {
        m_cancelled = true;
        appendLog("[CANCELLED] Operation cancelled");
        emit cancelRequested();
    }
    
    QDialog::reject();
}
