#ifndef VP_SHOWS_PROGRESSDIALOGS_H
#define VP_SHOWS_PROGRESSDIALOGS_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QThread>

// Forward declarations
class VP_ShowsEncryptionWorker;
class VP_ShowsDecryptionWorker;

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
                         const QString& username);

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

#endif // VP_SHOWS_PROGRESSDIALOGS_H
