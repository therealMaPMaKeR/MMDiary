#include "noncechecker.h"
#include "../../mainwindow.h"
#include "../../constants.h"
#include "CryptoUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QCloseEvent>
#include <QCoreApplication>

// ============================================================================
// NonceCheckProgressDialog Implementation
// ============================================================================

NonceCheckProgressDialog::NonceCheckProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_cancelled(false)
{
    setupUI();
}

void NonceCheckProgressDialog::setupUI()
{
    setWindowTitle("Nonce Integrity Check");
    setModal(true);
    setFixedSize(400, 250);
    
    // Create main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Status label
    m_statusLabel = new QLabel("Verifying nonce integrity...", this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);
    
    // File progress section
    m_fileLabel = new QLabel("File (0/0)", this);
    mainLayout->addWidget(m_fileLabel);
    
    m_fileProgress = new QProgressBar(this);
    m_fileProgress->setRange(0, 100);
    m_fileProgress->setValue(0);
    mainLayout->addWidget(m_fileProgress);
    
    // Operation progress section
    m_operationLabel = new QLabel("Operation (0/0)", this);
    mainLayout->addWidget(m_operationLabel);
    
    m_operationProgress = new QProgressBar(this);
    m_operationProgress->setRange(0, 100);
    m_operationProgress->setValue(0);
    mainLayout->addWidget(m_operationProgress);
    
    // Add spacer
    mainLayout->addStretch();
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel nonce integrity check", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &NonceCheckProgressDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

void NonceCheckProgressDialog::setFileProgress(int current, int total)
{
    m_fileLabel->setText(QString("File (%1/%2)").arg(current).arg(total));
    if (total > 0) {
        int percentage = (current * 100) / total;
        m_fileProgress->setValue(percentage);
    }
}

void NonceCheckProgressDialog::setOperationProgress(int current, int total)
{
    m_operationLabel->setText(QString("Operation (%1/%2)").arg(current).arg(total));
    if (total > 0) {
        int percentage = (current * 100) / total;
        m_operationProgress->setValue(percentage);
    }
}

void NonceCheckProgressDialog::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

void NonceCheckProgressDialog::onCancelClicked()
{
    qDebug() << "NonceCheckProgressDialog: Cancel button clicked";
    m_cancelled = true;
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");
    emit cancelled();
}

void NonceCheckProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_cancelled) {
        onCancelClicked();
    }
    event->accept();
}

void NonceCheckProgressDialog::reject()
{
    if (!m_cancelled) {
        onCancelClicked();
    }
}

// ============================================================================
// NonceCheckWorker Implementation
// ============================================================================

NonceCheckWorker::NonceCheckWorker(const QString& username, const QByteArray& encryptionKey)
    : m_username(username)
    , m_encryptionKey(encryptionKey)
    , m_cancelled(false)
    , m_totalNoncesChecked(0)
    , m_totalFilesChecked(0)
{
}

NonceCheckWorker::~NonceCheckWorker()
{
}

QStringList NonceCheckWorker::enumerateEncryptedFiles()
{
    QStringList encryptedFiles;
    
    // Build path to encrypted data directory
    QString basePath = QDir::current().absoluteFilePath("Data");
    QString userPath = QDir(basePath).absoluteFilePath(m_username);
    QString encDataPath = QDir(userPath).absoluteFilePath("EncryptedData");
    
    // Check if encrypted data directory exists
    QDir encDataDir(encDataPath);
    if (!encDataDir.exists()) {
        qDebug() << "NonceCheckWorker: EncryptedData directory does not exist";
        return encryptedFiles;
    }
    
    // List all subdirectories (file type categories)
    QStringList categories = encDataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& category : categories) {
        QString categoryPath = QDir(encDataPath).absoluteFilePath(category);
        QDir categoryDir(categoryPath);
        
        // List all .mmenc files in this category
        QStringList filters;
        filters << "*.mmenc";
        QFileInfoList fileList = categoryDir.entryInfoList(filters, QDir::Files);
        
        for (const QFileInfo& fileInfo : fileList) {
            encryptedFiles.append(fileInfo.absoluteFilePath());
        }
    }
    
    qDebug() << "NonceCheckWorker: Found" << encryptedFiles.size() << "encrypted files";
    return encryptedFiles;
}

bool NonceCheckWorker::checkSingleFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "NonceCheckWorker: Failed to open file:" << filePath;
        return false;
    }
    
    qint64 fileSize = file.size();
    qint64 processedSize = 0;
    
    // Track operations for this file
    QList<NonceInfo> fileNonces;
    int totalOperations = 0;
    int currentOperation = 0;
    
    // First, calculate total operations by doing a quick scan
    {
        QFile scanFile(filePath);
        if (scanFile.open(QIODevice::ReadOnly)) {
            // Skip metadata
            scanFile.seek(Constants::METADATA_RESERVED_SIZE);
            totalOperations = 1; // Metadata counts as one operation
            
            // Count chunks
            while (!scanFile.atEnd()) {
                quint32 chunkSize = 0;
                qint64 bytesRead = scanFile.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
                if (bytesRead != sizeof(chunkSize)) break;
                if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) break; // Invalid chunk
                
                scanFile.seek(scanFile.pos() + chunkSize);
                totalOperations++;
            }
            scanFile.close();
        }
    }
    
    emit operationProgress(0, totalOperations);
    
    // Read and analyze metadata block (40KB)
    QByteArray metadataBlock = file.read(Constants::METADATA_RESERVED_SIZE);
    if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
        qWarning() << "NonceCheckWorker: Invalid metadata size in file:" << filePath;
        file.close();
        return false;
    }
    
    processedSize += Constants::METADATA_RESERVED_SIZE;
    
    // Extract nonce from encrypted metadata
    // The metadata is encrypted with AES-GCM, so it has format: nonce (12 bytes) + ciphertext + tag (16 bytes)
    // Skip the size header (4 bytes) and extract the nonce from the encrypted portion
    if (metadataBlock.size() >= 4 + 12) { // At least size header + nonce
        quint32 encryptedSize = 0;
        memcpy(&encryptedSize, metadataBlock.constData(), sizeof(encryptedSize));
        
        if (encryptedSize > 0 && encryptedSize < static_cast<quint32>(metadataBlock.size() - 4)) {
            // Extract nonce from encrypted metadata (starts after the 4-byte size header)
            QByteArray metadataNonce = metadataBlock.mid(4, 12);
            
            NonceInfo nonceInfo(filePath, -1, metadataNonce); // -1 indicates metadata
            fileNonces.append(nonceInfo);
            
            // Add to global map
            m_nonceMap[metadataNonce].append(nonceInfo);
            m_totalNoncesChecked++;
        }
    }
    
    currentOperation++;
    emit operationProgress(currentOperation, totalOperations);
    
    // Process file chunks
    int chunkIndex = 0;
    while (!file.atEnd()) {
        // Check for cancellation
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_cancelled) {
                file.close();
                return false;
            }
        }
        
        // Read chunk size
        quint32 chunkSize = 0;
        qint64 bytesRead = file.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
        if (bytesRead == 0) {
            break; // End of file
        }
        if (bytesRead != sizeof(chunkSize)) {
            qWarning() << "NonceCheckWorker: Failed to read chunk size in file:" << filePath;
            break;
        }
        
        processedSize += sizeof(chunkSize);
        
        // Validate chunk size
        if (chunkSize == 0 || chunkSize > 10 * 1024 * 1024) { // Max 10MB per chunk
            qWarning() << "NonceCheckWorker: Invalid chunk size" << chunkSize << "in file:" << filePath;
            break;
        }
        
        // Read encrypted chunk
        QByteArray encryptedChunk = file.read(chunkSize);
        if (encryptedChunk.size() != static_cast<int>(chunkSize)) {
            qWarning() << "NonceCheckWorker: Failed to read complete chunk in file:" << filePath;
            break;
        }
        
        processedSize += chunkSize;
        
        // Extract nonce (first 12 bytes of encrypted chunk)
        if (encryptedChunk.size() >= 12) {
            QByteArray chunkNonce = encryptedChunk.left(12);
            
            NonceInfo nonceInfo(filePath, chunkIndex, chunkNonce);
            fileNonces.append(nonceInfo);
            
            // Add to global map
            m_nonceMap[chunkNonce].append(nonceInfo);
            m_totalNoncesChecked++;
        }
        
        chunkIndex++;
        currentOperation++;
        emit operationProgress(currentOperation, totalOperations);
        
        // Allow other threads to run
        QCoreApplication::processEvents();
    }
    
    file.close();
    
    // Final progress update for this file
    emit operationProgress(totalOperations, totalOperations);
    
    return true;
}

void NonceCheckWorker::doCheck()
{
    try {
        emit statusUpdate("Enumerating encrypted files...");
        
        // Get list of all encrypted files
        QStringList encryptedFiles = enumerateEncryptedFiles();
        
        if (encryptedFiles.isEmpty()) {
            emit checkFinished(true, "No encrypted files found to check.");
            return;
        }
        
        int totalFiles = encryptedFiles.size();
        int currentFile = 0;
        
        emit fileProgress(0, totalFiles);
        emit statusUpdate("Checking nonce integrity...");
        
        // Process each file
        for (const QString& filePath : encryptedFiles) {
            // Check for cancellation
            {
                QMutexLocker locker(&m_cancelMutex);
                if (m_cancelled) {
                    emit checkFinished(false, "Check cancelled by user.");
                    return;
                }
            }
            
            currentFile++;
            emit fileProgress(currentFile, totalFiles);
            
            // Update status without showing filename
            emit statusUpdate(QString("Checking file %1 of %2...").arg(currentFile).arg(totalFiles));
            
            if (!checkSingleFile(filePath)) {
                // File check failed (either error or cancelled)
                if (m_cancelled) {
                    emit checkFinished(false, "Check cancelled by user.");
                    return;
                }
                // Continue with next file even if one fails
            } else {
                m_totalFilesChecked++;
            }
        }
        
        // Analyze results for duplicates
        emit statusUpdate("Analyzing results...");
        
        for (auto it = m_nonceMap.begin(); it != m_nonceMap.end(); ++it) {
            if (it.value().size() > 1) {
                // Found a duplicate nonce
                DuplicateNonce duplicate;
                duplicate.nonce = it.key();
                duplicate.occurrences = it.value();
                m_duplicates.append(duplicate);
                
                qWarning() << "NonceCheckWorker: Found duplicate nonce used" << it.value().size() << "times";
            }
        }
        
        if (m_duplicates.isEmpty()) {
            emit checkFinished(true, "No duplicate nonces found.");
        } else {
            emit checkFinished(true, QString("Found %1 duplicate nonces.").arg(m_duplicates.size()));
        }
        
    } catch (const std::exception& e) {
        emit checkFinished(false, QString("Error during nonce check: %1").arg(e.what()));
    } catch (...) {
        emit checkFinished(false, "Unknown error during nonce check.");
    }
}

void NonceCheckWorker::cancel()
{
    QMutexLocker locker(&m_cancelMutex);
    m_cancelled = true;
    qDebug() << "NonceCheckWorker: Cancel requested";
}

// ============================================================================
// NonceChecker Implementation
// ============================================================================

NonceChecker::NonceChecker(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_progressDialog(nullptr)
    , m_worker(nullptr)
    , m_workerThread(nullptr)
{
}

NonceChecker::~NonceChecker()
{
    // Clean up worker thread if still running
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

void NonceChecker::performCheck()
{
    qDebug() << "NonceChecker: Starting nonce integrity check";
    
    // Create progress dialog
    m_progressDialog = new NonceCheckProgressDialog(m_mainWindow);
    
    // Create worker and thread
    m_workerThread = new QThread(this);
    m_worker = new NonceCheckWorker(m_mainWindow->user_Username, m_mainWindow->user_Key);
    m_worker->moveToThread(m_workerThread);
    
    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &NonceCheckWorker::doCheck);
    connect(m_worker, &NonceCheckWorker::fileProgress, this, &NonceChecker::onFileProgress);
    connect(m_worker, &NonceCheckWorker::operationProgress, this, &NonceChecker::onOperationProgress);
    connect(m_worker, &NonceCheckWorker::statusUpdate, this, &NonceChecker::onStatusUpdate);
    connect(m_worker, &NonceCheckWorker::checkFinished, this, &NonceChecker::onCheckFinished);
    connect(m_progressDialog, &NonceCheckProgressDialog::cancelled, this, &NonceChecker::onCheckCancelled);
    
    // Start the check
    m_workerThread->start();
    m_progressDialog->exec();
}

void NonceChecker::onFileProgress(int current, int total)
{
    if (m_progressDialog) {
        m_progressDialog->setFileProgress(current, total);
    }
}

void NonceChecker::onOperationProgress(int current, int total)
{
    if (m_progressDialog) {
        m_progressDialog->setOperationProgress(current, total);
    }
}

void NonceChecker::onStatusUpdate(const QString& text)
{
    if (m_progressDialog) {
        m_progressDialog->setStatusText(text);
    }
}

void NonceChecker::onCheckFinished(bool success, const QString& errorMessage)
{
    qDebug() << "NonceChecker: Check finished - Success:" << success << "Message:" << errorMessage;
    
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
    
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
    
    if (m_worker) {
        if (success) {
            QList<NonceCheckWorker::DuplicateNonce> duplicates = m_worker->getDuplicates();
            int totalNonces = m_worker->getTotalNoncesChecked();
            int totalFiles = m_worker->getTotalFilesChecked();
            
            showResultsDialog(duplicates, totalNonces, totalFiles);
        } else {
            if (errorMessage != "Check cancelled by user.") {
                QMessageBox::critical(m_mainWindow, "Nonce Check Failed", errorMessage);
            }
        }
        
        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

void NonceChecker::onCheckCancelled()
{
    qDebug() << "NonceChecker: Cancel requested";
    if (m_worker) {
        m_worker->cancel();
    }
}

void NonceChecker::showResultsDialog(const QList<NonceCheckWorker::DuplicateNonce>& duplicates, 
                                     int totalNonces, int totalFiles)
{
    if (duplicates.isEmpty()) {
        // No duplicates found - show success dialog
        QMessageBox::information(m_mainWindow, "Nonce Integrity Check Complete",
                                 QString("No duplicate nonces found!\n\n"
                                        "Files checked: %1\n"
                                        "Total encryption operations checked: %2\n\n"
                                        "Your encrypted files are secure - no nonce reuse detected.")
                                     .arg(totalFiles)
                                     .arg(totalNonces));
    } else {
        // Duplicates found - show critical warning
        int totalDuplicateOperations = 0;
        for (const auto& dup : duplicates) {
            totalDuplicateOperations += dup.occurrences.size();
        }
        
        QMessageBox msgBox(m_mainWindow);
        msgBox.setWindowTitle("Critical Security Issue Detected");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText(QString("Nonce reuse has been detected in %1 encryption operations!")
                      .arg(totalDuplicateOperations));
        
        QString details = QString("Files checked: %1\n"
                                 "Total encryption operations: %2\n"
                                 "Duplicate nonces found: %3\n"
                                 "Affected operations: %4\n\n"
                                 "It is imperative that you re-encrypt the affected files to maintain security.\n\n"
                                 "Would you like to do it now?")
                         .arg(totalFiles)
                         .arg(totalNonces)
                         .arg(duplicates.size())
                         .arg(totalDuplicateOperations);
        
        msgBox.setInformativeText(details);
        
        QPushButton* reencryptButton = msgBox.addButton("Re-encrypt Now", QMessageBox::AcceptRole);
        QPushButton* laterButton = msgBox.addButton("Later", QMessageBox::RejectRole);
        
        msgBox.setDefaultButton(reencryptButton);
        msgBox.exec();
        
        if (msgBox.clickedButton() == reencryptButton) {
            handleReencryption(duplicates);
        } else {
            QMessageBox::warning(m_mainWindow, "Security Warning",
                                "Please re-encrypt your files as soon as possible to ensure data security.\n\n"
                                "You can use the 'Change Password' feature to re-encrypt all files with new nonces.");
        }
    }
}

void NonceChecker::handleReencryption(const QList<NonceCheckWorker::DuplicateNonce>& duplicates)
{
    // For now, inform the user that re-encryption will be implemented in the future
    QMessageBox::information(m_mainWindow, "Re-encryption",
                           "Automatic re-encryption will be implemented in a future update.\n\n"
                           "For now, you can:\n"
                           "1. Use the 'Change Password' feature to re-encrypt all files\n"
                           "2. Manually decrypt and re-encrypt affected files\n\n"
                           "The affected files have been logged for your reference.");
    
    // Log affected files for user reference
    qWarning() << "NonceChecker: Files requiring re-encryption due to nonce reuse:";
    QSet<QString> affectedFiles;
    for (const auto& dup : duplicates) {
        for (const auto& occurrence : dup.occurrences) {
            affectedFiles.insert(occurrence.filePath);
            qWarning() << "  File:" << occurrence.filePath 
                      << "Chunk:" << (occurrence.chunkIndex == -1 ? "metadata" : QString::number(occurrence.chunkIndex));
        }
    }
    qWarning() << "Total affected files:" << affectedFiles.size();
}
