#ifndef VP_SHOWS_PROGRESSDIALOGS_H
#define VP_SHOWS_PROGRESSDIALOGS_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QPixmap>
#include "qtextedit.h"
#include "vp_shows_encryptionworkers.h"

// Thread cleanup utility class
class WorkerThreadCleanupHelper
{
public:
    // Standard cleanup pattern for worker threads
    // Returns true if cleanup was successful, false if termination was required
    template<typename WorkerType>
    static bool cleanupWorkerThread(WorkerType*& worker, QThread*& thread, const QString& className);
};

// Forward declarations
class VP_ShowsEncryptionWorker;
class VP_ShowsDecryptionWorker;
class VP_ShowsExportWorker;

// Progress dialog for encrypting TV show files
class VP_ShowsEncryptionProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsEncryptionProgressDialog(QWidget* parent = nullptr);
    ~VP_ShowsEncryptionProgressDialog();
    
    void startEncryption(const QStringList& sourceFiles,
                         const QStringList& targetFiles,
                         const QString& showName,
                         const QByteArray& encryptionKey,
                         const QString& username,
                         const QString& language = "English",
                         const QString& translation = "Dubbed",
                         bool useTMDB = true,
                         const QPixmap& customPoster = QPixmap(),
                         const QString& customDescription = QString(),
                         VP_ShowsEncryptionWorker::ParseMode parseMode = VP_ShowsEncryptionWorker::ParseFromFile,
                         int showId = 0);

signals:
    void encryptionComplete(bool success, const QString& message,
                           const QStringList& successfulFiles,
                           const QStringList& failedFiles);

private slots:
    void onProgressUpdated(int percentage);
    void onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);
    void onCurrentFileProgressUpdated(int percentage);
    void onEncryptionFinished(bool success, const QString& errorMessage,
                             const QStringList& successfulFiles,
                             const QStringList& failedFiles);
    void onCancelClicked();

private:
    void setupUI();
    void cleanup();
    
    QProgressBar* m_overallProgressBar;
    QProgressBar* m_fileProgressBar;
    QLabel* m_statusLabel;
    QLabel* m_fileLabel;
    QPushButton* m_cancelButton;
    
    QThread* m_workerThread;
    VP_ShowsEncryptionWorker* m_worker;
};

// Progress dialog for decrypting TV show files (for playback)
class VP_ShowsDecryptionProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsDecryptionProgressDialog(QWidget* parent = nullptr);
    ~VP_ShowsDecryptionProgressDialog();
    
    void startDecryption(const QString& sourceFile,
                        const QString& targetFile,
                        const QByteArray& encryptionKey,
                        const QString& username);

signals:
    void decryptionComplete(bool success, const QString& targetFile, const QString& message);

private slots:
    void onProgressUpdated(int percentage);
    void onDecryptionFinished(bool success, const QString& errorMessage);
    void onCancelClicked();

private:
    void setupUI();
    void cleanup();
    
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QPushButton* m_cancelButton;
    
    QThread* m_workerThread;
    VP_ShowsDecryptionWorker* m_worker;
    QString m_targetFile;
};

// Progress dialog for exporting entire TV shows
class VP_ShowsExportProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsExportProgressDialog(QWidget* parent = nullptr);
    ~VP_ShowsExportProgressDialog();
    
    void startExport(const QList<VP_ShowsExportWorker::ExportFileInfo>& files,
                     const QByteArray& encryptionKey,
                     const QString& username,
                     const QString& showName);

signals:
    void exportComplete(bool success, const QString& message,
                       const QStringList& successfulFiles,
                       const QStringList& failedFiles);

private slots:
    void onOverallProgressUpdated(int percentage);
    void onCurrentFileProgressUpdated(int percentage);
    void onFileProgressUpdate(int currentFile, int totalFiles, const QString& fileName);
    void onFileExportWarning(const QString& fileName, const QString& warningMessage);
    void onExportFinished(bool success, const QString& errorMessage,
                         const QStringList& successfulFiles,
                         const QStringList& failedFiles);
    void onCancelClicked();

private:
    void setupUI();
    void cleanup();
    
    QProgressBar* m_overallProgressBar;
    QProgressBar* m_currentFileProgressBar;
    QLabel* m_statusLabel;
    QLabel* m_fileLabel;
    QLabel* m_overallLabel;
    QLabel* m_warningLabel;
    QPushButton* m_cancelButton;
    
    QThread* m_workerThread;
    VP_ShowsExportWorker* m_worker;
    QString m_showName;
    QStringList m_warnings;
};

// TMDB Data Reacquisition Progress Dialog
class VP_ShowsTMDBReacquisitionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsTMDBReacquisitionDialog(QWidget* parent = nullptr);
    ~VP_ShowsTMDBReacquisitionDialog();
    
    void setTotalEpisodes(int total);
    void updateProgress(int current, const QString& episodeName);
    void setStatusMessage(const QString& message);
    void showRateLimitMessage(int retryInSeconds);
    
    bool wasCancelled() const { return m_cancelled; }
    
signals:
    void cancelRequested();
    
private slots:
    void onCancelClicked();
    
private:
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QLabel* m_currentItemLabel;
    QPushButton* m_cancelButton;
    QTextEdit* m_logWidget;
    
    bool m_cancelled;
    int m_totalEpisodes;
    int m_currentEpisode;
    
    void appendLog(const QString& message);
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
};

// Template implementation of cleanup helper
template<typename WorkerType>
bool WorkerThreadCleanupHelper::cleanupWorkerThread(WorkerType*& worker, QThread*& thread, const QString& className)
{
    bool cleanShutdown = true;
    
    // STEP 1: Disconnect all signals FIRST to prevent race conditions
    if (worker) {
        // Disconnect all signals from worker
        QObject::disconnect(worker, nullptr, nullptr, nullptr);
    }
    
    if (thread) {
        // Disconnect all signals from thread
        QObject::disconnect(thread, nullptr, nullptr, nullptr);
    }
    
    // STEP 2: Cancel the worker operation
    if (worker) {
        qDebug() << className << ": Cancelling worker operation";
        worker->cancel();
    }
    
    // STEP 3: Stop and clean up the thread
    if (thread) {
        if (thread->isRunning()) {
            qDebug() << className << ": Requesting thread quit";
            thread->quit();
            
            // Wait up to 10 seconds for graceful shutdown
            if (!thread->wait(10000)) {
                qDebug() << className << ": Thread didn't quit gracefully, forcing termination";
                thread->terminate();
                cleanShutdown = false;
                
                // Wait 2 more seconds after termination
                if (!thread->wait(2000)) {
                    qCritical() << className << ": ERROR - Failed to terminate thread!";
                }
            } else {
                qDebug() << className << ": Thread stopped gracefully";
            }
        }
    }
    
    // STEP 4: Clean up worker object
    if (worker) {
        // Check thread affinity before moving
        if (worker->thread() != QApplication::instance()->thread()) {
            // Move worker back to main thread for safe deletion
            worker->moveToThread(QApplication::instance()->thread());
        }
        
        // Delete the worker
        worker->deleteLater();
        worker = nullptr;
    }
    
    // STEP 5: Delete the thread object
    if (thread) {
        delete thread;
        thread = nullptr;
    }
    
    return cleanShutdown;
}

#endif // VP_SHOWS_PROGRESSDIALOGS_H
