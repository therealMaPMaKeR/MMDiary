#ifndef NONCECHECKER_H
#define NONCECHECKER_H

#include <QObject>
#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QMutex>
#include <QByteArray>
#include <QSet>
#include <QPair>
#include <QMap>

// Forward declarations
class MainWindow;

// Progress dialog for nonce checking
class NonceCheckProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NonceCheckProgressDialog(QWidget* parent = nullptr);
    
    void setFileProgress(int current, int total);
    void setOperationProgress(int current, int total);
    void setStatusText(const QString& text);
    bool wasCancelled() const { return m_cancelled; }
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
    
private slots:
    void onCancelClicked();
    
private:
    void setupUI();
    
    QLabel* m_statusLabel;
    QLabel* m_fileLabel;
    QProgressBar* m_fileProgress;
    QLabel* m_operationLabel;
    QProgressBar* m_operationProgress;
    QPushButton* m_cancelButton;
    bool m_cancelled;
    
signals:
    void cancelled();
};

// Worker class for nonce checking
class NonceCheckWorker : public QObject
{
    Q_OBJECT
    
public:
    struct NonceInfo {
        QString filePath;
        int chunkIndex;  // -1 for metadata, 0+ for file chunks
        QByteArray nonce;
        
        NonceInfo() : chunkIndex(-1) {}
        NonceInfo(const QString& path, int idx, const QByteArray& n) 
            : filePath(path), chunkIndex(idx), nonce(n) {}
    };
    
    struct DuplicateNonce {
        QByteArray nonce;
        QList<NonceInfo> occurrences;
    };
    
    NonceCheckWorker(const QString& username, const QByteArray& encryptionKey);
    ~NonceCheckWorker();
    
    QList<DuplicateNonce> getDuplicates() const { return m_duplicates; }
    int getTotalNoncesChecked() const { return m_totalNoncesChecked; }
    int getTotalFilesChecked() const { return m_totalFilesChecked; }
    
public slots:
    void doCheck();
    void cancel();
    
signals:
    void fileProgress(int current, int total);
    void operationProgress(int current, int total);
    void statusUpdate(const QString& text);
    void checkFinished(bool success, const QString& errorMessage = QString());
    
private:
    bool checkSingleFile(const QString& filePath);
    QStringList enumerateEncryptedFiles();
    
    QString m_username;
    QByteArray m_encryptionKey;
    bool m_cancelled;
    QMutex m_cancelMutex;
    
    // Track all nonces and their locations
    QMap<QByteArray, QList<NonceInfo>> m_nonceMap;
    QList<DuplicateNonce> m_duplicates;
    int m_totalNoncesChecked;
    int m_totalFilesChecked;
};

// Main nonce checker class
class NonceChecker : public QObject
{
    Q_OBJECT
    
public:
    explicit NonceChecker(MainWindow* mainWindow);
    ~NonceChecker();
    
    void performCheck();
    
private slots:
    void onFileProgress(int current, int total);
    void onOperationProgress(int current, int total);
    void onStatusUpdate(const QString& text);
    void onCheckFinished(bool success, const QString& errorMessage);
    void onCheckCancelled();
    
private:
    void showResultsDialog(const QList<NonceCheckWorker::DuplicateNonce>& duplicates, 
                           int totalNonces, int totalFiles);
    void handleReencryption(const QList<NonceCheckWorker::DuplicateNonce>& duplicates);
    
    MainWindow* m_mainWindow;
    NonceCheckProgressDialog* m_progressDialog;
    NonceCheckWorker* m_worker;
    QThread* m_workerThread;
};

#endif // NONCECHECKER_H
