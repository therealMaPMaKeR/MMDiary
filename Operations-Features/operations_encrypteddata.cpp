#include "operations_encrypteddata.h"
#include "../Operations-Global/CryptoUtils.h"
#include "../Operations-Global/operations_files.h"
#include "../constants.h"
#include "ui_mainwindow.h"
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QThread>
#include <QMutexLocker>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QRegularExpression>

// Windows-specific includes for file association checking
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <QSettings>
#endif

// ============================================================================
// EncryptionWorker Implementation
// ============================================================================

EncryptionWorker::EncryptionWorker(const QString& sourceFile, const QString& targetFile,
                                   const QByteArray& encryptionKey, const QString& username)
    : m_sourceFile(sourceFile)
    , m_targetFile(targetFile)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_cancelled(false)
{
}

void EncryptionWorker::doEncryption()
{
    try {
        QFile sourceFile(m_sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit encryptionFinished(false, "Failed to open source file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        qint64 processedSize = 0;

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(m_targetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit encryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(m_targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit encryptionFinished(false, "Failed to create target file");
            return;
        }

        // Write header: original filename length + filename
        QFileInfo sourceInfo(m_sourceFile);
        QString originalFilename = sourceInfo.fileName();
        QByteArray filenameBytes = originalFilename.toUtf8();

        // Write length and data
        quint32 filenameLength = static_cast<quint32>(filenameBytes.size());

        targetFile.write(reinterpret_cast<const char*>(&filenameLength), sizeof(filenameLength));
        targetFile.write(filenameBytes);

        // Encrypt and write file content in chunks
        const qint64 chunkSize = 1024 * 1024; // 1MB chunks
        QByteArray buffer;

        while (!sourceFile.atEnd()) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(m_targetFile); // Clean up partial file
                    emit encryptionFinished(false, "Operation was cancelled");
                    return;
                }
            }

            // Read chunk
            buffer = sourceFile.read(chunkSize);
            if (buffer.isEmpty()) {
                break;
            }

            // Encrypt chunk
            QByteArray encryptedChunk = CryptoUtils::Encryption_EncryptBArray(
                m_encryptionKey, buffer, m_username);

            if (encryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(m_targetFile); // Clean up partial file
                emit encryptionFinished(false, "Encryption failed for file chunk");
                return;
            }

            // Write encrypted chunk size and data
            quint32 chunkDataSize = static_cast<quint32>(encryptedChunk.size());
            targetFile.write(reinterpret_cast<const char*>(&chunkDataSize), sizeof(chunkDataSize));
            targetFile.write(encryptedChunk);

            processedSize += buffer.size();

            // Update progress
            int percentage = static_cast<int>((processedSize * 100) / totalSize);
            emit progressUpdated(percentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        sourceFile.close();
        targetFile.close();

        emit encryptionFinished(true);

    } catch (const std::exception& e) {
        emit encryptionFinished(false, QString("Encryption error: %1").arg(e.what()));
    } catch (...) {
        emit encryptionFinished(false, "Unknown encryption error occurred");
    }
}

void EncryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

// ============================================================================
// DecryptionWorker Implementation
// ============================================================================

DecryptionWorker::DecryptionWorker(const QString& sourceFile, const QString& targetFile,
                                   const QByteArray& encryptionKey)
    : m_sourceFile(sourceFile)
    , m_targetFile(targetFile)
    , m_encryptionKey(encryptionKey)
    , m_cancelled(false)
{
}

void DecryptionWorker::doDecryption()
{
    try {
        QFile sourceFile(m_sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit decryptionFinished(false, "Failed to open encrypted file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        qint64 processedSize = 0;

        // Read and skip the filename header (we don't need it for decryption)
        quint32 filenameLength = 0;
        if (sourceFile.read(reinterpret_cast<char*>(&filenameLength), sizeof(filenameLength)) != sizeof(filenameLength)) {
            emit decryptionFinished(false, "Failed to read filename length from encrypted file");
            return;
        }

        if (filenameLength == 0 || filenameLength > 1000) {
            emit decryptionFinished(false, "Invalid filename length in encrypted file");
            return;
        }

        // Skip the filename bytes
        if (sourceFile.read(filenameLength).size() != static_cast<int>(filenameLength)) {
            emit decryptionFinished(false, "Failed to skip filename in encrypted file");
            return;
        }

        processedSize += sizeof(filenameLength) + filenameLength;

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(m_targetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit decryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(m_targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit decryptionFinished(false, "Failed to create target file");
            return;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(m_targetFile); // Clean up partial file
                    emit decryptionFinished(false, "Operation was cancelled");
                    return;
                }
            }

            // Read chunk size
            quint32 chunkSize = 0;
            qint64 bytesRead = sourceFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (bytesRead == 0) {
                break; // End of file
            }
            if (bytesRead != sizeof(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read chunk size");
                return;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Invalid chunk size in encrypted file");
                return;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = sourceFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read complete encrypted chunk");
                return;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(
                m_encryptionKey, encryptedChunk);

            if (decryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Decryption failed for file chunk");
                return;
            }

            // Write decrypted chunk
            if (targetFile.write(decryptedChunk) != decryptedChunk.size()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to write decrypted data");
                return;
            }

            processedSize += sizeof(chunkSize) + chunkSize;

            // Update progress
            int percentage = static_cast<int>((processedSize * 100) / totalSize);
            emit progressUpdated(percentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        sourceFile.close();

        // Ensure all data is written to disk before closing
        targetFile.flush();
        targetFile.close();

// Set proper file permissions for reading
#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(m_targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(m_targetFile)) {
            emit decryptionFinished(false, "Target file was not created successfully");
            return;
        }

        qDebug() << "Temp decryption completed successfully:" << m_targetFile;
        emit decryptionFinished(true);

    } catch (const std::exception& e) {
        emit decryptionFinished(false, QString("Decryption error: %1").arg(e.what()));
    } catch (...) {
        emit decryptionFinished(false, "Unknown decryption error occurred");
    }
}

void DecryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

// ============================================================================
// TempDecryptionWorker Implementation
// ============================================================================

TempDecryptionWorker::TempDecryptionWorker(const QString& sourceFile, const QString& targetFile,
                                           const QByteArray& encryptionKey)
    : m_sourceFile(sourceFile)
    , m_targetFile(targetFile)
    , m_encryptionKey(encryptionKey)
    , m_cancelled(false)
{
}

void TempDecryptionWorker::doDecryption()
{
    try {
        QFile sourceFile(m_sourceFile);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            emit decryptionFinished(false, "Failed to open encrypted file for reading");
            return;
        }

        // Get file size for progress calculation
        qint64 totalSize = sourceFile.size();
        qint64 processedSize = 0;

        // Read and skip the filename header
        quint32 filenameLength = 0;
        if (sourceFile.read(reinterpret_cast<char*>(&filenameLength), sizeof(filenameLength)) != sizeof(filenameLength)) {
            emit decryptionFinished(false, "Failed to read filename length from encrypted file");
            return;
        }

        if (filenameLength == 0 || filenameLength > 1000) {
            emit decryptionFinished(false, "Invalid filename length in encrypted file");
            return;
        }

        // Skip the filename bytes
        if (sourceFile.read(filenameLength).size() != static_cast<int>(filenameLength)) {
            emit decryptionFinished(false, "Failed to skip filename in encrypted file");
            return;
        }

        processedSize += sizeof(filenameLength) + filenameLength;

        // Create target directory if it doesn't exist
        QFileInfo targetInfo(m_targetFile);
        QDir targetDir = targetInfo.dir();
        if (!targetDir.exists()) {
            if (!targetDir.mkpath(".")) {
                emit decryptionFinished(false, "Failed to create target directory");
                return;
            }
        }

        QFile targetFile(m_targetFile);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            emit decryptionFinished(false, "Failed to create target file");
            return;
        }

        // Decrypt file content chunk by chunk
        while (!sourceFile.atEnd()) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    targetFile.close();
                    QFile::remove(m_targetFile); // Clean up partial file
                    emit decryptionFinished(false, "Operation was cancelled");
                    return;
                }
            }

            // Read chunk size
            quint32 chunkSize = 0;
            qint64 bytesRead = sourceFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (bytesRead == 0) {
                break; // End of file
            }
            if (bytesRead != sizeof(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read chunk size");
                return;
            }

            if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Invalid chunk size in encrypted file");
                return;
            }

            // Read encrypted chunk data
            QByteArray encryptedChunk = sourceFile.read(chunkSize);
            if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to read complete encrypted chunk");
                return;
            }

            // Decrypt chunk
            QByteArray decryptedChunk = CryptoUtils::Encryption_DecryptBArray(
                m_encryptionKey, encryptedChunk);

            if (decryptedChunk.isEmpty()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Decryption failed for file chunk");
                return;
            }

            // Write decrypted chunk
            if (targetFile.write(decryptedChunk) != decryptedChunk.size()) {
                targetFile.close();
                QFile::remove(m_targetFile);
                emit decryptionFinished(false, "Failed to write decrypted data");
                return;
            }

            processedSize += sizeof(chunkSize) + chunkSize;

            // Update progress
            int percentage = static_cast<int>((processedSize * 100) / totalSize);
            emit progressUpdated(percentage);

            // Allow other threads to run
            QCoreApplication::processEvents();
        }

        sourceFile.close();

        // Ensure all data is written to disk before closing
        targetFile.flush();
        targetFile.close();

// Set proper file permissions for reading
#ifdef Q_OS_WIN
        // On Windows, ensure the file is not marked as read-only
        QFile::setPermissions(m_targetFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
#endif

        // Verify the file was created successfully
        if (!QFile::exists(m_targetFile)) {
            emit decryptionFinished(false, "Target file was not created successfully");
            return;
        }

        qDebug() << "Temp decryption completed successfully:" << m_targetFile;
        emit decryptionFinished(true);

    } catch (const std::exception& e) {
        emit decryptionFinished(false, QString("Decryption error: %1").arg(e.what()));
    } catch (...) {
        emit decryptionFinished(false, "Unknown decryption error occurred");
    }
}

void TempDecryptionWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
}

// ============================================================================
// Operations_EncryptedData Implementation
// ============================================================================

Operations_EncryptedData::Operations_EncryptedData(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_progressDialog(nullptr)
    , m_worker(nullptr)
    , m_workerThread(nullptr)
    , m_decryptWorker(nullptr)
    , m_decryptWorkerThread(nullptr)
    , m_tempDecryptWorker(nullptr)
    , m_tempDecryptWorkerThread(nullptr)
    , m_tempFileCleanupTimer(nullptr)
{
    // Connect selection changed signal to update button states
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::itemSelectionChanged,
            this, &Operations_EncryptedData::updateButtonStates);

    // Connect double-click signal
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::itemDoubleClicked,
            this, &Operations_EncryptedData::onFileListDoubleClicked);

    onSortTypeChanged("All");

    // Set initial button states (disabled since no files loaded yet)
    updateButtonStates();

    // Start temp file monitoring
    startTempFileMonitoring();

    // Clean up any orphaned temp files from previous sessions
    cleanupTempFiles();
}

Operations_EncryptedData::~Operations_EncryptedData()
{
    // Stop the cleanup timer
    if (m_tempFileCleanupTimer) {
        m_tempFileCleanupTimer->stop();
        m_tempFileCleanupTimer->deleteLater();
        m_tempFileCleanupTimer = nullptr;
    }

    // Handle encryption worker (existing code)
    if (m_workerThread && m_workerThread->isRunning()) {
        if (m_worker) {
            m_worker->cancel();
        }
        m_workerThread->quit();
        m_workerThread->wait(5000);
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }

    // Handle decryption worker
    if (m_decryptWorkerThread && m_decryptWorkerThread->isRunning()) {
        if (m_decryptWorker) {
            m_decryptWorker->cancel();
        }
        m_decryptWorkerThread->quit();
        m_decryptWorkerThread->wait(5000);
        if (m_decryptWorkerThread->isRunning()) {
            m_decryptWorkerThread->terminate();
            m_decryptWorkerThread->wait(1000);
        }
    }

    // Handle temp decryption worker
    if (m_tempDecryptWorkerThread && m_tempDecryptWorkerThread->isRunning()) {
        if (m_tempDecryptWorker) {
            m_tempDecryptWorker->cancel();
        }
        m_tempDecryptWorkerThread->quit();
        m_tempDecryptWorkerThread->wait(5000);
        if (m_tempDecryptWorkerThread->isRunning()) {
            m_tempDecryptWorkerThread->terminate();
            m_tempDecryptWorkerThread->wait(1000);
        }
    }

    // Clean up workers
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_decryptWorker) {
        m_decryptWorker->deleteLater();
        m_decryptWorker = nullptr;
    }

    if (m_decryptWorkerThread) {
        m_decryptWorkerThread->deleteLater();
        m_decryptWorkerThread = nullptr;
    }

    if (m_tempDecryptWorker) {
        m_tempDecryptWorker->deleteLater();
        m_tempDecryptWorker = nullptr;
    }

    if (m_tempDecryptWorkerThread) {
        m_tempDecryptWorkerThread->deleteLater();
        m_tempDecryptWorkerThread = nullptr;
    }

    if (m_progressDialog) {
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
}

// ============================================================================
// Double-click to open functionality
// ============================================================================

void Operations_EncryptedData::onFileListDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    // Get the encrypted file path and original filename
    QString encryptedFilePath = item->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get the original filename
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to extract original filename from encrypted file.");
        return;
    }

    // Extract file extension
    QFileInfo fileInfo(originalFilename);
    QString extension = fileInfo.suffix().toLower();

    if (extension.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "No File Extension",
                             "The file has no extension. Cannot determine default application.");
        return;
    }

    // Validate encryption key before proceeding with file opening
    qDebug() << "Validating encryption key for double-click open:" << encryptedFilePath;
    QByteArray encryptionKey = m_mainWindow->user_Key;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful for double-click open";

    // Check for default application
    QString defaultApp = checkDefaultApp(extension);
    QString appToUse;

    qDebug() << "File extension:" << extension;
    qDebug() << "Default app found:" << (defaultApp.isEmpty() ? "None" : defaultApp);

    if (defaultApp.isEmpty()) {
        // No default app - show dialog to select one
        AppChoice choice = showNoDefaultAppDialog();
        qDebug() << "No default app dialog choice (int):" << static_cast<int>(choice);
        qDebug() << "Comparing with SelectApp (" << static_cast<int>(AppChoice::SelectApp) << ")";

        if (choice == AppChoice::Cancel) {
            qDebug() << "User cancelled - no default app dialog";
            return;
        } else if (choice == AppChoice::SelectApp) {
            qDebug() << "User chose to select app - calling selectApplication()";
            appToUse = selectApplication();
            qDebug() << "selectApplication() returned:" << appToUse;
            if (appToUse.isEmpty()) {
                qDebug() << "User cancelled app selection";
                return; // User cancelled app selection
            }
        } else {
            qDebug() << "WARNING: Unexpected choice value in no default app dialog:" << static_cast<int>(choice);
        }
    } else {
        // Default app exists - show dialog with options
        AppChoice choice = showDefaultAppDialog(defaultApp);
        qDebug() << "Default app dialog choice (int):" << static_cast<int>(choice);
        qDebug() << "Comparing with Cancel (" << static_cast<int>(AppChoice::Cancel) << "), UseDefault (" << static_cast<int>(AppChoice::UseDefault) << "), SelectApp (" << static_cast<int>(AppChoice::SelectApp) << ")";

        if (choice == AppChoice::Cancel) {
            qDebug() << "User cancelled - default app dialog";
            return;
        } else if (choice == AppChoice::UseDefault) {
            appToUse = "default"; // Special marker for default app
            qDebug() << "User chose UseDefault - appToUse set to 'default'";
        } else if (choice == AppChoice::SelectApp) {
            qDebug() << "User chose to select app - calling selectApplication()";
            appToUse = selectApplication();
            qDebug() << "selectApplication() returned:" << appToUse;
            if (appToUse.isEmpty()) {
                qDebug() << "User cancelled app selection";
                return; // User cancelled app selection
            }
        } else {
            qDebug() << "WARNING: Unexpected choice value in default app dialog:" << static_cast<int>(choice);
        }
    }

    // Safety check - this should never happen but let's handle it
    if (appToUse.isEmpty()) {
        qDebug() << "ERROR: appToUse is still empty after dialog logic!";
        qDebug() << "This should not happen - showing fallback dialog";
        QMessageBox::critical(m_mainWindow, "Error",
                              "No application was selected to open the file.");
        return;
    }

    qDebug() << "Final appToUse value before proceeding:" << appToUse;

    // Create temp file path with obfuscated name
    QString tempFilePath = createTempFilePath(originalFilename);
    if (tempFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create temporary file path.");
        return;
    }

    // Store the app to open for later use after decryption
    m_pendingAppToOpen = appToUse;
    qDebug() << "Stored in m_pendingAppToOpen:" << m_pendingAppToOpen;

    qDebug() << "Starting temporary decryption with validated encryption key";

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting file for opening...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("Opening Encrypted File");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread for temp decryption
    m_tempDecryptWorkerThread = new QThread(this);
    m_tempDecryptWorker = new TempDecryptionWorker(encryptedFilePath, tempFilePath, encryptionKey);
    m_tempDecryptWorker->moveToThread(m_tempDecryptWorkerThread);

    // Connect signals
    connect(m_tempDecryptWorkerThread, &QThread::started, m_tempDecryptWorker, &TempDecryptionWorker::doDecryption);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::progressUpdated, this, &Operations_EncryptedData::onTempDecryptionProgress);
    connect(m_tempDecryptWorker, &TempDecryptionWorker::decryptionFinished, this, &Operations_EncryptedData::onTempDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onTempDecryptionCancelled);

    // Start decryption
    m_tempDecryptWorkerThread->start();
    m_progressDialog->exec();
}

QString Operations_EncryptedData::checkDefaultApp(const QString& extension)
{
#ifdef Q_OS_WIN
    // Check Windows registry for default application
    QSettings regSettings(QString("HKEY_CLASSES_ROOT\\.%1").arg(extension), QSettings::NativeFormat);
    QString fileType = regSettings.value(".", "").toString();

    if (fileType.isEmpty()) {
        return QString(); // No association found
    }

    QSettings appSettings(QString("HKEY_CLASSES_ROOT\\%1\\shell\\open\\command").arg(fileType), QSettings::NativeFormat);
    QString command = appSettings.value(".", "").toString();

    if (command.isEmpty()) {
        return QString(); // No command found
    }

    // Extract application name from command
    // Commands usually look like: "C:\Program Files\App\app.exe" "%1"
    QRegularExpression regex("\"([^\"]+)\"");
    QRegularExpressionMatch match = regex.match(command);
    if (match.hasMatch()) {
        QString appPath = match.captured(1);
        QFileInfo appInfo(appPath);
        return appInfo.baseName(); // Return just the application name
    } else {
        // Try to extract first word as app path
        QStringList parts = command.split(' ', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            QFileInfo appInfo(parts.first());
            return appInfo.baseName();
        }
    }
#endif
    return QString(); // Fallback for non-Windows or if detection fails
}

Operations_EncryptedData::AppChoice Operations_EncryptedData::showDefaultAppDialog(const QString& appName)
{
    qDebug() << "Showing default app dialog for app:" << appName;

    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Open Encrypted File");
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(QString("'%1' is set as default for this type of file.").arg(appName));
    msgBox.setInformativeText("Do you want to open it with the default app or select a specific one?");

    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* useDefaultButton = msgBox.addButton("Use Default", QMessageBox::AcceptRole);
    QPushButton* selectAppButton = msgBox.addButton("Select an App", QMessageBox::ActionRole);

    msgBox.setDefaultButton(useDefaultButton);
    int result = msgBox.exec();

    qDebug() << "Dialog exec result:" << result;
    qDebug() << "Clicked button text:" << (msgBox.clickedButton() ? msgBox.clickedButton()->text() : "nullptr");
    qDebug() << "Clicked button pointer:" << msgBox.clickedButton();
    qDebug() << "Cancel button pointer:" << cancelButton;
    qDebug() << "Use Default button pointer:" << useDefaultButton;
    qDebug() << "Select App button pointer:" << selectAppButton;

    AppChoice choice = AppChoice::Cancel; // Default

    if (msgBox.clickedButton() == cancelButton) {
        choice = AppChoice::Cancel;
        qDebug() << "User clicked Cancel button (pointer match)";
    } else if (msgBox.clickedButton() == useDefaultButton) {
        choice = AppChoice::UseDefault;
        qDebug() << "User clicked Use Default button (pointer match)";
    } else if (msgBox.clickedButton() == selectAppButton) {
        choice = AppChoice::SelectApp;
        qDebug() << "User clicked Select an App button (pointer match)";
    } else {
        qDebug() << "Button pointer comparison failed - using text fallback";
        // Fallback based on button text in case button comparison fails
        if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Use Default") {
            choice = AppChoice::UseDefault;
            qDebug() << "Fallback: Detected Use Default by text";
        } else if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Select an App") {
            choice = AppChoice::SelectApp;
            qDebug() << "Fallback: Detected Select an App by text";
        } else {
            choice = AppChoice::Cancel;
            qDebug() << "Fallback: Defaulting to Cancel";
        }
    }

    qDebug() << "Final default app dialog result:" << static_cast<int>(choice);
    return choice;
}

Operations_EncryptedData::AppChoice Operations_EncryptedData::showNoDefaultAppDialog()
{
    qDebug() << "Showing no default app dialog";

    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Open Encrypted File");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("No default app defined for this type of file.");

    QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* selectAppButton = msgBox.addButton("Select an App", QMessageBox::AcceptRole);

    msgBox.setDefaultButton(selectAppButton);
    int result = msgBox.exec();

    qDebug() << "Dialog exec result:" << result;
    qDebug() << "Clicked button text:" << (msgBox.clickedButton() ? msgBox.clickedButton()->text() : "nullptr");
    qDebug() << "Clicked button pointer:" << msgBox.clickedButton();
    qDebug() << "Cancel button pointer:" << cancelButton;
    qDebug() << "Select App button pointer:" << selectAppButton;

    AppChoice choice = AppChoice::Cancel; // Default

    if (msgBox.clickedButton() == cancelButton) {
        choice = AppChoice::Cancel;
        qDebug() << "User clicked Cancel button (pointer match)";
    } else if (msgBox.clickedButton() == selectAppButton) {
        choice = AppChoice::SelectApp;
        qDebug() << "User clicked Select an App button (pointer match)";
    } else {
        qDebug() << "Button pointer comparison failed - using text fallback";
        // Fallback based on button text
        if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Select an App") {
            choice = AppChoice::SelectApp;
            qDebug() << "Fallback: Detected Select an App by text";
        } else {
            choice = AppChoice::Cancel;
            qDebug() << "Fallback: Defaulting to Cancel";
        }
    }

    qDebug() << "Final no default app dialog result:" << static_cast<int>(choice);
    return choice;
}

QString Operations_EncryptedData::selectApplication()
{
    qDebug() << "Opening application selection dialog";

    QString appPath = QFileDialog::getOpenFileName(
        m_mainWindow,
        "Select Application",
        QString(),
        "Executable Files (*.exe);;All Files (*.*)"
        );

    qDebug() << "Application selection result:" << (appPath.isEmpty() ? "User cancelled" : appPath);

    // Validate the selected application path
    if (!appPath.isEmpty()) {
        QFileInfo appInfo(appPath);
        if (!appInfo.exists() || !appInfo.isExecutable()) {
            QMessageBox::warning(m_mainWindow, "Invalid Application",
                                 "The selected file is not a valid executable.");
            qDebug() << "Invalid application selected:" << appPath;
            return QString();
        }
    }

    return appPath;
}

QString Operations_EncryptedData::createTempFilePath(const QString& originalFilename)
{
    // Get the temp decrypt directory
    QString tempDir = getTempDecryptDir();

    // Ensure temp directory exists
    QDir dir(tempDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Failed to create temp decrypt directory:" << tempDir;
            return QString();
        }
    }

    // Extract extension from original filename
    QFileInfo fileInfo(originalFilename);
    QString extension = fileInfo.suffix();

    // Generate obfuscated filename with original extension
    QString obfuscatedName = generateRandomFilename();

    // Replace .mmenc extension with the original file extension
    if (!extension.isEmpty()) {
        obfuscatedName = obfuscatedName.replace(".mmenc", "." + extension);
    } else {
        obfuscatedName = obfuscatedName.replace(".mmenc", "");
    }

    // Ensure filename is unique in temp directory
    QString finalPath;
    int attempts = 0;
    const int maxAttempts = 100;

    do {
        if (attempts > 0) {
            // Add number suffix for uniqueness
            QString nameWithoutExt = QFileInfo(obfuscatedName).baseName();
            QString finalName = QString("%1_%2").arg(nameWithoutExt).arg(attempts);
            if (!extension.isEmpty()) {
                finalName += "." + extension;
            }
            finalPath = QDir(tempDir).absoluteFilePath(finalName);
        } else {
            finalPath = QDir(tempDir).absoluteFilePath(obfuscatedName);
        }

        attempts++;

        if (attempts > maxAttempts) {
            qWarning() << "Failed to generate unique temp filename after" << maxAttempts << "attempts";
            return QString();
        }

    } while (QFile::exists(finalPath));

    return finalPath;
}

void Operations_EncryptedData::openFileWithApp(const QString& tempFile, const QString& appPath)
{
    // Ensure file exists and is readable
    QFileInfo fileInfo(tempFile);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        QMessageBox::critical(m_mainWindow, "File Error",
                              "The temporary file could not be accessed.");
        return;
    }

    qDebug() << "Attempting to open file:" << tempFile;
    qDebug() << "File size:" << fileInfo.size() << "bytes";
    qDebug() << "Using app:" << (appPath == "default" ? "default system app" : appPath);

    // Safety check for empty app path
    if (appPath.isEmpty()) {
        qDebug() << "WARNING: Empty app path, falling back to default app";
        QMessageBox::StandardButton reply = QMessageBox::question(
            m_mainWindow,
            "No Application Selected",
            "No application was selected. Would you like to try opening with the system default application?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
            );

        if (reply != QMessageBox::Yes) {
            return;
        }

        // Fall back to default app
        QUrl fileUrl = QUrl::fromLocalFile(tempFile);

        // Add a small delay to ensure file is fully written
        QCoreApplication::processEvents();
        QThread::msleep(100);

        if (!QDesktopServices::openUrl(fileUrl)) {
            // Try alternative method on Windows
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << "start" << "" << QDir::toNativeSeparators(tempFile);
            if (QProcess::startDetached(command, args)) {
                qDebug() << "Opened file with Windows start command:" << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the default application.\n\nFile location: %1").arg(tempFile));
        } else {
            qDebug() << "Opened file with fallback default app:" << tempFile;
        }
        return;
    }

    if (appPath == "default") {
        // Use system default application
        QUrl fileUrl = QUrl::fromLocalFile(tempFile);

        // Add a small delay to ensure file is fully written
        QCoreApplication::processEvents();
        QThread::msleep(100);

        if (!QDesktopServices::openUrl(fileUrl)) {
            // Try alternative method on Windows
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << "start" << "" << QDir::toNativeSeparators(tempFile);
            if (QProcess::startDetached(command, args)) {
                qDebug() << "Opened file with fallback Windows start command:" << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the default application.\n\nFile location: %1").arg(tempFile));
        } else {
            qDebug() << "Opened file with default app:" << tempFile;
        }
    } else {
        // Use specific application
        QStringList arguments;
        arguments << QDir::toNativeSeparators(tempFile);

        // Set working directory to the app's directory
        QFileInfo appInfo(appPath);
        QString workingDir = appInfo.absolutePath();

        // Add a small delay to ensure file is fully written
        QCoreApplication::processEvents();
        QThread::msleep(100);

        qint64 pid = 0;
        bool success = QProcess::startDetached(appPath, arguments, workingDir, &pid);

        if (!success) {
            // Try with quoted path as a single argument
            QStringList quotedArgs;
            quotedArgs << QString("\"%1\"").arg(QDir::toNativeSeparators(tempFile));
            success = QProcess::startDetached(appPath, quotedArgs, workingDir, &pid);
        }

        if (!success) {
            // Try using cmd.exe on Windows
#ifdef Q_OS_WIN
            QString command = QString("cmd.exe");
            QStringList args;
            args << "/c" << QString("\"%1\" \"%2\"").arg(
                QDir::toNativeSeparators(appPath),
                QDir::toNativeSeparators(tempFile));

            if (QProcess::startDetached(command, args)) {
                qDebug() << "Opened file with Windows cmd command:" << appPath << tempFile;
                return;
            }
#endif
            QMessageBox::warning(m_mainWindow, "Failed to Open File",
                                 QString("Could not open the file with the selected application.\n\nApp: %1\nFile: %2\n\nTry opening the file manually from the temp folder.").arg(appPath, tempFile));
        } else {
            qDebug() << "Opened file with app:" << appPath << "file:" << tempFile << "PID:" << pid;
        }
    }
}

QString Operations_EncryptedData::getTempDecryptDir()
{
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_mainWindow->user_Username);
    QString tempPath = QDir(userPath).absoluteFilePath("Temp");
    return QDir(tempPath).absoluteFilePath("tempdecrypt");
}

// ============================================================================
// Temp file monitoring and cleanup
// ============================================================================

void Operations_EncryptedData::startTempFileMonitoring()
{
    if (!m_tempFileCleanupTimer) {
        m_tempFileCleanupTimer = new QTimer(this);
        connect(m_tempFileCleanupTimer, &QTimer::timeout, this, &Operations_EncryptedData::onCleanupTimerTimeout);
        m_tempFileCleanupTimer->start(60000); // 1 minute = 60000 ms
        qDebug() << "Started temp file cleanup timer with 1-minute interval";
    }
}

void Operations_EncryptedData::onCleanupTimerTimeout()
{
    cleanupTempFiles();
}

void Operations_EncryptedData::cleanupTempFiles()
{
    QString tempDir = getTempDecryptDir();
    QDir dir(tempDir);

    if (!dir.exists()) {
        return; // No temp directory exists yet
    }

    // Get all files in the temp directory
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    int filesDeleted = 0;
    for (const QFileInfo& fileInfo : fileList) {
        QString filePath = fileInfo.absoluteFilePath();

        // Check if file is in use
        if (!isFileInUse(filePath)) {
            // File is not in use, securely delete it
            if (OperationsFiles::secureDelete(filePath, 3)) {
                filesDeleted++;
                qDebug() << "Cleaned up temp file:" << filePath;
            } else {
                qWarning() << "Failed to clean up temp file:" << filePath;
            }
        } else {
            qDebug() << "Temp file still in use:" << filePath;
        }
    }

    if (filesDeleted > 0) {
        qDebug() << "Cleanup completed. Deleted" << filesDeleted << "temp files";
    }
}

bool Operations_EncryptedData::isFileInUse(const QString& filePath)
{
#ifdef Q_OS_WIN
    // On Windows, try to open the file with exclusive access
    // If it fails, the file is likely in use
    HANDLE fileHandle = CreateFileA(
        filePath.toLocal8Bit().constData(),
        GENERIC_READ | GENERIC_WRITE,
        0, // No sharing - exclusive access
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );

    if (fileHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED) {
            // File is in use
            return true;
        }
        // Other errors might indicate the file doesn't exist or other issues
        // For safety, consider it not in use so it can be cleaned up
        return false;
    } else {
        // Successfully opened with exclusive access, file is not in use
        CloseHandle(fileHandle);
        return false;
    }
#else
    // For non-Windows systems, this is a fallback
    // Simply try to open and close the file
    QFile file(filePath);
    if (file.open(QIODevice::ReadWrite)) {
        file.close();
        return false; // Not in use
    }
    return true; // Assume in use if we can't open it
#endif
}

// ============================================================================
// Temp decryption slots
// ============================================================================

void Operations_EncryptedData::onTempDecryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onTempDecryptionFinished(bool success, const QString& errorMessage)
{
    qDebug() << "=== onTempDecryptionFinished called ===";
    qDebug() << "Success:" << success;
    qDebug() << "m_pendingAppToOpen at start of onTempDecryptionFinished:" << m_pendingAppToOpen;

    // Store the app choice in a local variable immediately to prevent it from being cleared
    QString localAppToOpen = m_pendingAppToOpen;
    qDebug() << "Stored in localAppToOpen:" << localAppToOpen;

    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_tempDecryptWorkerThread) {
        m_tempDecryptWorkerThread->quit();
        m_tempDecryptWorkerThread->wait();
        m_tempDecryptWorkerThread->deleteLater();
        m_tempDecryptWorkerThread = nullptr;
    }

    if (m_tempDecryptWorker) {
        QString tempFilePath = m_tempDecryptWorker->m_targetFile;
        qDebug() << "Got tempFilePath:" << tempFilePath;
        qDebug() << "m_pendingAppToOpen after getting tempFilePath:" << m_pendingAppToOpen;
        qDebug() << "localAppToOpen after getting tempFilePath:" << localAppToOpen;

        if (success) {
            qDebug() << "m_pendingAppToOpen in success branch:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen in success branch:" << localAppToOpen;

            // Add a brief delay to ensure file system operations are complete
            QCoreApplication::processEvents();
            qDebug() << "m_pendingAppToOpen after processEvents:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen after processEvents:" << localAppToOpen;

            QThread::msleep(200);
            qDebug() << "m_pendingAppToOpen after msleep:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen after msleep:" << localAppToOpen;

            // Verify file exists and has content before trying to open
            QFileInfo fileInfo(tempFilePath);
            qDebug() << "Temp decryption finished. File:" << tempFilePath;
            qDebug() << "File exists:" << fileInfo.exists() << "Size:" << fileInfo.size() << "bytes";
            qDebug() << "Pending app JUST BEFORE openFileWithApp:" << m_pendingAppToOpen;
            qDebug() << "localAppToOpen JUST BEFORE openFileWithApp:" << localAppToOpen;

            if (!fileInfo.exists() || fileInfo.size() == 0) {
                QMessageBox::critical(m_mainWindow, "File Error",
                                      QString("The decrypted temporary file is missing or empty.\n\nExpected location: %1").arg(tempFilePath));
            } else {
                qDebug() << "About to call openFileWithApp with localAppToOpen:" << localAppToOpen;
                // Use the local variable instead of the member variable
                openFileWithApp(tempFilePath, localAppToOpen);
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Decryption Failed",
                                  "Failed to decrypt file for opening: " + errorMessage);

            // Clean up the partial temp file if it exists
            if (QFile::exists(tempFilePath)) {
                QFile::remove(tempFilePath);
            }
        }

        m_tempDecryptWorker->deleteLater();
        m_tempDecryptWorker = nullptr;
    }

    // Clear pending app only after we're done using it
    qDebug() << "About to clear m_pendingAppToOpen at end of function";
    m_pendingAppToOpen.clear();
    qDebug() << "Cleared m_pendingAppToOpen at end of function";
}

void Operations_EncryptedData::onTempDecryptionCancelled()
{
    qDebug() << "=== onTempDecryptionCancelled called ===";
    qDebug() << "m_pendingAppToOpen before cancel processing:" << m_pendingAppToOpen;

    if (m_tempDecryptWorker) {
        m_tempDecryptWorker->cancel();
    }

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }

    // Clear pending app
    qDebug() << "About to clear m_pendingAppToOpen in onTempDecryptionCancelled";
    m_pendingAppToOpen.clear();
    qDebug() << "Cleared m_pendingAppToOpen in onTempDecryptionCancelled";
}

// ============================================================================
// Existing functionality (encryption, regular decryption, etc.)
// ============================================================================

// [Insert all the existing methods from the original file here...]
// I'll continue with the rest of the existing methods in the next part

void Operations_EncryptedData::encryptSelectedFile()
{
    // Open file dialog to select a file for encryption
    QString filePath = QFileDialog::getOpenFileName(
        m_mainWindow,
        "Select File to Encrypt",
        QString(),
        "All Files (*.*)"
        );

    // Check if user selected a file (didn't cancel)
    if (filePath.isEmpty()) {
        qDebug() << "User cancelled file selection";
        return;
    }

    qDebug() << "Selected file for encryption:" << filePath;

    // Validate the file path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        filePath, InputValidation::InputType::ExternalFilePath, 1000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "The selected file path is invalid: " + result.errorMessage);
        return;
    }

    // Check if file exists and is readable
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        QMessageBox::warning(m_mainWindow, "File Access Error",
                             "The selected file cannot be read or does not exist.");
        return;
    }

    // Get username from mainwindow
    QString username = m_mainWindow->user_Username;
    QByteArray encryptionKey = m_mainWindow->user_Key;

    // Create target path
    QString targetPath = createTargetPath(filePath, username);
    if (targetPath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to create target path for encrypted file.");
        return;
    }

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Encrypting file...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("File Encryption");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread
    m_workerThread = new QThread(this);
    m_worker = new EncryptionWorker(filePath, targetPath, encryptionKey, username);
    m_worker->moveToThread(m_workerThread);

    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &EncryptionWorker::doEncryption);
    connect(m_worker, &EncryptionWorker::progressUpdated, this, &Operations_EncryptedData::onEncryptionProgress);
    connect(m_worker, &EncryptionWorker::encryptionFinished, this, &Operations_EncryptedData::onEncryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onEncryptionCancelled);

    // Start encryption
    m_workerThread->start();
    m_progressDialog->exec();
}

QString Operations_EncryptedData::determineFileType(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();

    // Video files
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp"};
    if (videoExtensions.contains(extension)) {
        return "Video";
    }

    // Image files
    QStringList imageExtensions = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "svg", "ico", "webp"};
    if (imageExtensions.contains(extension)) {
        return "Image";
    }

    // Audio files
    QStringList audioExtensions = {"mp3", "wav", "flac", "aac", "ogg", "wma", "m4a"};
    if (audioExtensions.contains(extension)) {
        return "Audio";
    }

    // Document files
    QStringList documentExtensions = {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf", "odt"};
    if (documentExtensions.contains(extension)) {
        return "Document";
    }

    // Archive files
    QStringList archiveExtensions = {"zip", "rar", "7z", "tar", "gz", "bz2"};
    if (archiveExtensions.contains(extension)) {
        return "Archive";
    }

    // Default to "Other" for unrecognized file types
    return "Other";
}

QString Operations_EncryptedData::generateRandomFilename()
{
    const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const int length = 32; // Generate a 32-character random string

    QString randomString;
    for (int i = 0; i < length; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        randomString.append(chars[index]);
    }

    return randomString + ".mmenc";
}

bool Operations_EncryptedData::checkFilenameExists(const QString& folderPath, const QString& filename)
{
    QDir dir(folderPath);
    return dir.exists(filename);
}

QString Operations_EncryptedData::createTargetPath(const QString& sourceFile, const QString& username)
{
    // Determine file type
    QString fileType = determineFileType(sourceFile);

    // Create folder path: Data/username/EncryptedData/FileType/
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");
    QString typePath = QDir(encDataPath).absoluteFilePath(fileType);

    // Ensure directories exist
    QDir typeDir(typePath);
    if (!typeDir.exists()) {
        if (!typeDir.mkpath(".")) {
            qWarning() << "Failed to create directory:" << typePath;
            return QString();
        }
    }

    // Generate unique filename
    QString filename;
    int attempts = 0;
    const int maxAttempts = 100;

    do {
        filename = generateRandomFilename();
        attempts++;

        if (attempts > maxAttempts) {
            qWarning() << "Failed to generate unique filename after" << maxAttempts << "attempts";
            return QString();
        }

    } while (checkFilenameExists(typePath, filename));

    return QDir(typePath).absoluteFilePath(filename);
}

void Operations_EncryptedData::showSuccessDialog(const QString& encryptedFile, const QString& originalFile)
{
    QMessageBox msgBox(m_mainWindow);
    msgBox.setWindowTitle("Encryption Complete");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("File encrypted successfully!");
    msgBox.setInformativeText("The file has been encrypted and saved securely.\n\n"
                              "Would you like to securely delete the original unencrypted file?");

    QPushButton* deleteButton = msgBox.addButton("Delete Original", QMessageBox::YesRole);
    QPushButton* keepButton = msgBox.addButton("Keep Original", QMessageBox::NoRole);
    msgBox.setDefaultButton(keepButton);

    msgBox.exec();

    // Check if the delete button was clicked by comparing button text
    if (msgBox.clickedButton() && msgBox.clickedButton()->text() == "Delete Original") {
        // Securely delete the original file
        bool deleted = OperationsFiles::secureDelete(originalFile, 3);

        if (deleted) {
            QMessageBox::information(m_mainWindow, "File Deleted",
                                     "The original file has been securely deleted.");
        } else {
            QMessageBox::warning(m_mainWindow, "Deletion Failed",
                                 "Failed to securely delete the original file. "
                                 "You may need to delete it manually.");
        }
    }
}

void Operations_EncryptedData::onEncryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onEncryptionFinished(bool success, const QString& errorMessage)
{
    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_worker) {
        QString originalFile = m_worker->m_sourceFile;
        QString encryptedFile = m_worker->m_targetFile;

        if (success) {
            // Determine the file type that was just encrypted
            QString fileType = determineFileType(originalFile);
            QString uiSortType = mapDirectoryToSortType(fileType);

            // Check if combo box is already on the correct type
            QString currentSortType = m_mainWindow->ui->comboBox_DataENC_SortType->currentText();

            if (currentSortType == uiSortType || currentSortType == "All") {
                // Already on the correct type or showing all files - just refresh the list
                populateEncryptedFilesList();
            } else {
                // Need to change combo box, which will automatically trigger list refresh
                int targetIndex = Operations::GetIndexFromText(uiSortType, m_mainWindow->ui->comboBox_DataENC_SortType);
                if (targetIndex != -1) {
                    m_mainWindow->ui->comboBox_DataENC_SortType->setCurrentIndex(targetIndex);
                    // This will automatically trigger onSortTypeChanged() which will repopulate the list
                } else {
                    // Fallback: manually populate the list if index lookup failed
                    qWarning() << "Failed to find combo box index for:" << uiSortType;
                    populateEncryptedFilesList();
                }
            }

            showSuccessDialog(encryptedFile, originalFile);
        } else {
            QMessageBox::critical(m_mainWindow, "Encryption Failed",
                                  "File encryption failed: " + errorMessage);
        }

        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

void Operations_EncryptedData::onEncryptionCancelled()
{
    if (m_worker) {
        m_worker->cancel();
    }

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }
}

void Operations_EncryptedData::decryptSelectedFile()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        QMessageBox::warning(m_mainWindow, "No Selection",
                             "Please select a file to decrypt.");
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get encryption key
    QByteArray encryptionKey = m_mainWindow->user_Key;

    // Validate encryption key before proceeding
    qDebug() << "Validating encryption key for file:" << encryptedFilePath;
    if (!InputValidation::validateEncryptionKey(encryptedFilePath, encryptionKey)) {
        QMessageBox::critical(m_mainWindow, "Invalid Encryption Key",
                              "The encryption key is invalid or the file is corrupted. "
                              "Please ensure you are using the correct user account.");
        return;
    }
    qDebug() << "Encryption key validation successful";

    // Get the original filename
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to extract original filename from encrypted file.");
        return;
    }

    // Open save dialog with original filename pre-filled
    QString suggestedPath = QDir::homePath() + "/" + originalFilename;
    QString targetPath = QFileDialog::getSaveFileName(
        m_mainWindow,
        "Save Decrypted File As",
        suggestedPath,
        "All Files (*.*)"
        );

    if (targetPath.isEmpty()) {
        qDebug() << "User cancelled file save dialog";
        return;
    }

    // Validate the target path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        targetPath, InputValidation::InputType::ExternalFilePath, 1000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "The selected save path is invalid: " + result.errorMessage);
        return;
    }

    // Check if target file already exists
    if (QFile::exists(targetPath)) {
        int ret = QMessageBox::question(m_mainWindow, "File Exists",
                                        "The target file already exists. Do you want to overwrite it?",
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }

    // Set up progress dialog
    m_progressDialog = new QProgressDialog("Decrypting file...", "Cancel", 0, 100, m_mainWindow);
    m_progressDialog->setWindowTitle("File Decryption");
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);

    // Set up worker thread
    m_decryptWorkerThread = new QThread(this);
    m_decryptWorker = new DecryptionWorker(encryptedFilePath, targetPath, encryptionKey);
    m_decryptWorker->moveToThread(m_decryptWorkerThread);

    // Connect signals
    connect(m_decryptWorkerThread, &QThread::started, m_decryptWorker, &DecryptionWorker::doDecryption);
    connect(m_decryptWorker, &DecryptionWorker::progressUpdated, this, &Operations_EncryptedData::onDecryptionProgress);
    connect(m_decryptWorker, &DecryptionWorker::decryptionFinished, this, &Operations_EncryptedData::onDecryptionFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, &Operations_EncryptedData::onDecryptionCancelled);

    // Start decryption
    m_decryptWorkerThread->start();
    m_progressDialog->exec();
}

void Operations_EncryptedData::onDecryptionProgress(int percentage)
{
    if (m_progressDialog) {
        m_progressDialog->setValue(percentage);
    }
}

void Operations_EncryptedData::onDecryptionFinished(bool success, const QString& errorMessage)
{
    if (m_progressDialog) {
        m_progressDialog->close();
    }

    if (m_decryptWorkerThread) {
        m_decryptWorkerThread->quit();
        m_decryptWorkerThread->wait();
        m_decryptWorkerThread->deleteLater();
        m_decryptWorkerThread = nullptr;
    }

    if (m_decryptWorker) {
        QString encryptedFile = m_decryptWorker->m_sourceFile;
        QString decryptedFile = m_decryptWorker->m_targetFile;

        if (success) {
            // Show success dialog and ask about deleting encrypted file
            QMessageBox msgBox(m_mainWindow);
            msgBox.setWindowTitle("Decryption Complete");
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setText("File decrypted successfully!");
            msgBox.setInformativeText("The file has been decrypted and saved.\n\n"
                                      "Would you like to delete the encrypted copy?");

            QPushButton* deleteButton = msgBox.addButton("Delete Encrypted Copy", QMessageBox::YesRole);
            QPushButton* keepButton = msgBox.addButton("Keep Encrypted Copy", QMessageBox::NoRole);
            msgBox.setDefaultButton(keepButton);

            msgBox.exec();

            // Check if the delete button was clicked
            if (msgBox.clickedButton() == deleteButton) {
                // Delete the encrypted file
                if (QFile::remove(encryptedFile)) {
                    // Refresh the list since we removed a file
                    populateEncryptedFilesList();
                    QMessageBox::information(m_mainWindow, "File Deleted",
                                             "The encrypted file has been deleted.");
                } else {
                    QMessageBox::warning(m_mainWindow, "Deletion Failed",
                                         "Failed to delete the encrypted file. "
                                         "You may need to delete it manually.");
                }
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Decryption Failed",
                                  "File decryption failed: " + errorMessage);
        }

        m_decryptWorker->deleteLater();
        m_decryptWorker = nullptr;
    }
}

void Operations_EncryptedData::onDecryptionCancelled()
{
    if (m_decryptWorker) {
        m_decryptWorker->cancel();
    }

    if (m_progressDialog) {
        m_progressDialog->setLabelText("Cancelling...");
        m_progressDialog->setCancelButton(nullptr); // Disable cancel button while cancelling
    }
}

void Operations_EncryptedData::deleteSelectedFile()
{
    // Get the currently selected item
    QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_DataENC_FileList->currentItem();
    if (!currentItem) {
        QMessageBox::warning(m_mainWindow, "No Selection",
                             "Please select a file to delete.");
        return;
    }

    // Get the encrypted file path from the item's user data
    QString encryptedFilePath = currentItem->data(Qt::UserRole).toString();
    if (encryptedFilePath.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Error",
                              "Failed to retrieve encrypted file path.");
        return;
    }

    // Verify the encrypted file still exists
    if (!QFile::exists(encryptedFilePath)) {
        QMessageBox::critical(m_mainWindow, "File Not Found",
                              "The encrypted file no longer exists.");
        populateEncryptedFilesList(); // Refresh the list
        return;
    }

    // Get the original filename for display in the confirmation dialog
    QString originalFilename = getOriginalFilename(encryptedFilePath);
    if (originalFilename.isEmpty()) {
        // If we can't get the original filename, use the encrypted filename
        QFileInfo fileInfo(encryptedFilePath);
        originalFilename = fileInfo.fileName();
    }

    // Show confirmation dialog
    int ret = QMessageBox::question(
        m_mainWindow,
        "Confirm Deletion",
        QString("Are you sure you want to delete '%1'?").arg(originalFilename),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No // Default to No for safety
        );

    if (ret != QMessageBox::Yes) {
        return; // User cancelled
    }

    // Delete the encrypted file (regular deletion is fine since it's encrypted)
    bool deleted = QFile::remove(encryptedFilePath);

    if (deleted) {
        // Refresh the list since we removed a file
        populateEncryptedFilesList();
        QMessageBox::information(m_mainWindow, "File Deleted",
                                 QString("'%1' has been deleted.").arg(originalFilename));
    } else {
        QMessageBox::critical(m_mainWindow, "Deletion Failed",
                              QString("Failed to delete '%1'. The file may be in use or you may not have sufficient permissions.").arg(originalFilename));
    }
}

QString Operations_EncryptedData::getOriginalFilename(const QString& encryptedFilePath)
{
    QFile file(encryptedFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open encrypted file:" << encryptedFilePath;
        return QString();
    }

    // Read the filename length (first 4 bytes)
    quint32 filenameLength = 0;
    if (file.read(reinterpret_cast<char*>(&filenameLength), sizeof(filenameLength)) != sizeof(filenameLength)) {
        qWarning() << "Failed to read filename length from:" << encryptedFilePath;
        return QString();
    }

    // Validate filename length (reasonable limits)
    if (filenameLength == 0 || filenameLength > 1000) {
        qWarning() << "Invalid filename length in encrypted file:" << filenameLength << encryptedFilePath;
        return QString();
    }

    // Read the original filename
    QByteArray filenameBytes = file.read(filenameLength);
    if (filenameBytes.size() != static_cast<int>(filenameLength)) {
        qWarning() << "Failed to read complete filename from:" << encryptedFilePath;
        return QString();
    }

    file.close();

    // Convert to QString
    QString originalFilename = QString::fromUtf8(filenameBytes);

    // Validate the filename
    InputValidation::ValidationResult result = InputValidation::validateInput(
        originalFilename, InputValidation::InputType::FileName, 255);

    if (!result.isValid) {
        qWarning() << "Invalid filename extracted from encrypted file:" << originalFilename;
        return QString();
    }

    return originalFilename;
}

QString Operations_EncryptedData::mapSortTypeToDirectory(const QString& sortType)
{
    if (sortType == "Text") {
        return "Document";
    } else if (sortType == "Image") {
        return "Image";
    } else if (sortType == "Audio") {
        return "Audio";
    } else if (sortType == "Video") {
        return "Video";
    } else if (sortType == "Archive") {
        return "Archive";
    } else if (sortType == "Other") {
        return "Other";
    } else if (sortType == "All") {
        return "All";
    }

    return "All"; // Default fallback
}

QString Operations_EncryptedData::mapDirectoryToSortType(const QString& directoryName)
{
    if (directoryName == "Document") {
        return "Text";
    } else if (directoryName == "Image") {
        return "Image";
    } else if (directoryName == "Audio") {
        return "Audio";
    } else if (directoryName == "Video") {
        return "Video";
    } else if (directoryName == "Archive") {
        return "Archive";
    } else if (directoryName == "Other") {
        return "Other";
    }

    return "All"; // Default fallback
}

void Operations_EncryptedData::populateEncryptedFilesList()
{
    // Clear the current list
    m_mainWindow->ui->listWidget_DataENC_FileList->clear();

    // Get current sort type from combo box
    QString currentSortType = m_mainWindow->ui->comboBox_DataENC_SortType->currentText();
    QString username = m_mainWindow->user_Username;

    // Build base path to encrypted data
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");

    QDir encDataDir(encDataPath);
    if (!encDataDir.exists()) {
        // No encrypted data directory exists yet
        qDebug() << "EncryptedData directory doesn't exist for user:" << username;
        return;
    }

    QStringList directoriesToScan;

    if (currentSortType == "All") {
        // Scan all subdirectories
        directoriesToScan << "Document" << "Image" << "Audio" << "Video" << "Archive" << "Other";
    } else {
        // Scan only the specific directory
        QString mappedDirectory = mapSortTypeToDirectory(currentSortType);
        directoriesToScan << mappedDirectory;
    }

    // Scan each directory for encrypted files
    for (const QString& dirName : directoriesToScan) {
        QString dirPath = QDir(encDataPath).absoluteFilePath(dirName);
        QDir dir(dirPath);

        if (!dir.exists()) {
            continue; // Skip non-existent directories
        }

        // Get all .mmenc files in this directory
        QStringList filters;
        filters << "*.mmenc";
        QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);

        for (const QFileInfo& fileInfo : fileList) {
            QString encryptedFilePath = fileInfo.absoluteFilePath();
            QString originalFilename = getOriginalFilename(encryptedFilePath);

            if (!originalFilename.isEmpty()) {
                // Create list item with original filename as display text
                QListWidgetItem* item = new QListWidgetItem(originalFilename);

                // Store the encrypted file path and directory type as user data
                item->setData(Qt::UserRole, encryptedFilePath);
                item->setData(Qt::UserRole + 1, dirName);

                // Add type prefix if showing "All" to help user identify file types
                if (currentSortType == "All") {
                    QString displayName = QString("[%1] %2").arg(
                        dirName == "Document" ? "Text" : dirName, originalFilename);
                    item->setText(displayName);
                }

                m_mainWindow->ui->listWidget_DataENC_FileList->addItem(item);
            } else {
                qWarning() << "Failed to extract filename from:" << encryptedFilePath;
            }
        }
    }

    // Update list widget to show results
    int itemCount = m_mainWindow->ui->listWidget_DataENC_FileList->count();
    qDebug() << "Populated encrypted files list with" << itemCount << "items for sort type:" << currentSortType;
    // Update button states after populating the list
    updateButtonStates();
}

void Operations_EncryptedData::onSortTypeChanged(const QString& sortType)
{
    Q_UNUSED(sortType)
    // Repopulate the list when sort type changes
    populateEncryptedFilesList();
}

void Operations_EncryptedData::updateButtonStates()
{
    // Check if any item is selected in the file list
    bool hasSelection = (m_mainWindow->ui->listWidget_DataENC_FileList->currentItem() != nullptr);

    // Style for disabled buttons (same as operations_settings.cpp)
    QString disabledStyle = "color: #888888; background-color: #444444;";
    QString enabledStyle = ""; // Default style

    // Update Decrypt button
    m_mainWindow->ui->pushButton_DataENC_Decrypt->setEnabled(hasSelection);
    m_mainWindow->ui->pushButton_DataENC_Decrypt->setStyleSheet(hasSelection ? enabledStyle : disabledStyle);

    // Update Delete File button
    m_mainWindow->ui->pushButton_DataENC_DeleteFile->setEnabled(hasSelection);
    m_mainWindow->ui->pushButton_DataENC_DeleteFile->setStyleSheet(hasSelection ? enabledStyle : disabledStyle);
}
