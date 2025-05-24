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
// Operations_EncryptedData Implementation
// ============================================================================

Operations_EncryptedData::Operations_EncryptedData(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_progressDialog(nullptr)
    , m_worker(nullptr)
    , m_workerThread(nullptr)
{
    // Connect selection changed signal to update button states
    connect(m_mainWindow->ui->listWidget_DataENC_FileList, &QListWidget::itemSelectionChanged,
            this, &Operations_EncryptedData::updateButtonStates);

    onSortTypeChanged("All");

    // Set initial button states (disabled since no files loaded yet)
    updateButtonStates();
}

Operations_EncryptedData::~Operations_EncryptedData()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        if (m_worker) {
            m_worker->cancel();
        }
        m_workerThread->quit();
        m_workerThread->wait(5000); // Wait up to 5 seconds
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }

    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_progressDialog) {
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
}

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


//Populate List Widget

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


//Buttons

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
