#include "vp_shows_progressdialogs.h"
#include "qtimer.h"
#include "vp_shows_encryptionworkers.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QMessageBox>

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
                                                       const QString& translation)
{
    qDebug() << "VP_ShowsEncryptionProgressDialog: Starting encryption for" << sourceFiles.size() << "files";
    
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
    m_worker = new VP_ShowsEncryptionWorker(sourceFiles, targetFiles, showName, encryptionKey, username, language, translation);
    
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
    QTimer::singleShot(success ? 1000 : 2000, this, &QDialog::accept);
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
    
    // First, check if we have a worker to cancel
    if (m_worker) {
        m_worker->cancel();
    }
    
    // Handle thread cleanup
    if (m_workerThread) {
        // Disconnect all signals to prevent any further processing
        if (m_worker) {
            m_worker->disconnect();
        }
        m_workerThread->disconnect();
        
        // Only try to stop the thread if it's still valid and running
        if (m_workerThread->isRunning()) {
            qDebug() << "VP_ShowsEncryptionProgressDialog: Requesting thread quit";
            m_workerThread->quit();
            
            // Wait for thread to finish
            if (!m_workerThread->wait(5000)) {
                qDebug() << "VP_ShowsEncryptionProgressDialog: Thread didn't quit in time, terminating";
                m_workerThread->terminate();
                m_workerThread->wait();
            }
        }
        
        // Delete the worker and thread objects
        if (m_worker) {
            delete m_worker;
            m_worker = nullptr;
        }
        
        delete m_workerThread;
        m_workerThread = nullptr;
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
        QTimer::singleShot(2000, this, &QDialog::reject);
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
    
    // First, check if we have a worker to cancel
    if (m_worker) {
        m_worker->cancel();
    }
    
    // Handle thread cleanup
    if (m_workerThread) {
        // Disconnect all signals to prevent any further processing
        if (m_worker) {
            m_worker->disconnect();
        }
        m_workerThread->disconnect();
        
        // Only try to stop the thread if it's still valid and running
        if (m_workerThread->isRunning()) {
            qDebug() << "VP_ShowsDecryptionProgressDialog: Requesting thread quit";
            m_workerThread->quit();
            
            // Wait for thread to finish
            if (!m_workerThread->wait(5000)) {
                qDebug() << "VP_ShowsDecryptionProgressDialog: Thread didn't quit in time, terminating";
                m_workerThread->terminate();
                m_workerThread->wait();
            }
        }
        
        // Delete the worker and thread objects
        if (m_worker) {
            delete m_worker;
            m_worker = nullptr;
        }
        
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    
    qDebug() << "VP_ShowsDecryptionProgressDialog: cleanup() completed";
}

// Include QTimer for delayed close
#include <QTimer>

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
    
    // Clean up any previous operation
    cleanup();
    
    // Reset UI
    m_overallProgressBar->setValue(0);
    m_currentFileProgressBar->setValue(0);
    m_statusLabel->setText(QString("Exporting %1 (%2 files)").arg(showName).arg(files.size()));
    m_fileLabel->setText("");
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

void VP_ShowsExportProgressDialog::onExportFinished(bool success, const QString& errorMessage,
                                                   const QStringList& successfulFiles,
                                                   const QStringList& failedFiles)
{
    qDebug() << "VP_ShowsExportProgressDialog: Export finished. Success:" << success;
    
    m_cancelButton->setEnabled(false);
    
    if (success) {
        m_statusLabel->setText(QString("Export of %1 completed successfully!").arg(m_showName));
        m_overallProgressBar->setValue(100);
        m_currentFileProgressBar->setValue(100);
    } else {
        m_statusLabel->setText("Export failed: " + errorMessage);
    }
    
    // Emit completion signal
    emit exportComplete(success, errorMessage, successfulFiles, failedFiles);
    
    // Close dialog after a short delay
    QTimer::singleShot(success ? 1500 : 3000, this, &QDialog::accept);
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
    
    // First, check if we have a worker to cancel
    if (m_worker) {
        m_worker->cancel();
    }
    
    // Handle thread cleanup
    if (m_workerThread) {
        // Disconnect all signals to prevent any further processing
        if (m_worker) {
            m_worker->disconnect();
        }
        m_workerThread->disconnect();
        
        // Only try to stop the thread if it's still valid and running
        if (m_workerThread->isRunning()) {
            qDebug() << "VP_ShowsExportProgressDialog: Requesting thread quit";
            m_workerThread->quit();
            
            // Wait for thread to finish
            if (!m_workerThread->wait(5000)) {
                qDebug() << "VP_ShowsExportProgressDialog: Thread didn't quit in time, terminating";
                m_workerThread->terminate();
                m_workerThread->wait();
            }
        }
        
        // Delete the worker and thread objects
        if (m_worker) {
            delete m_worker;
            m_worker = nullptr;
        }
        
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    
    qDebug() << "VP_ShowsExportProgressDialog: cleanup() completed";
}
