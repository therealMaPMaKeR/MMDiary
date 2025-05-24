#ifndef OPERATIONS_ENCRYPTEDDATA_H
#define OPERATIONS_ENCRYPTEDDATA_H

#include <QObject>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QThread>
#include <QMutex>
#include <QPushButton>
#include <QAbstractButton>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"

class MainWindow;

// Worker class for encryption in separate thread
class EncryptionWorker : public QObject
{
    Q_OBJECT

public:
    EncryptionWorker(const QString& sourceFile, const QString& targetFile,
                     const QByteArray& encryptionKey, const QString& username);

    QString m_sourceFile;
    QString m_targetFile;

public slots:
    void doEncryption();

signals:
    void progressUpdated(int percentage);
    void encryptionFinished(bool success, const QString& errorMessage = QString());

private:
    QByteArray m_encryptionKey;
    QString m_username;
    bool m_cancelled;
    QMutex m_cancelMutex;

public:
    void cancel();
};

class Operations_EncryptedData : public QObject
{
    Q_OBJECT

private:
    MainWindow* m_mainWindow;

    // Helper functions
    QString determineFileType(const QString& filePath);
    QString generateRandomFilename();
    bool checkFilenameExists(const QString& folderPath, const QString& filename);
    QString createTargetPath(const QString& sourceFile, const QString& username);
    void showSuccessDialog(const QString& encryptedFile, const QString& originalFile);

    // Progress dialog
    QProgressDialog* m_progressDialog;
    EncryptionWorker* m_worker;
    QThread* m_workerThread;

public:
    explicit Operations_EncryptedData(MainWindow* mainWindow);
    ~Operations_EncryptedData();
    friend class MainWindow;

    // Main encryption function
    void encryptSelectedFile();

private slots:
    void onEncryptionProgress(int percentage);
    void onEncryptionFinished(bool success, const QString& errorMessage = QString());
    void onEncryptionCancelled();
};

#endif // OPERATIONS_ENCRYPTEDDATA_H
