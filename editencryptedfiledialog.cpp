#include "editencryptedfiledialog.h"
#include "qpushbutton.h"
#include "ui_editencryptedfiledialog.h"
#include "Operations-Global/inputvalidation.h"
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>

EditEncryptedFileDialog::EditEncryptedFileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EditEncryptedFileDialog)
    , m_metadataManager(nullptr)
{
    ui->setupUi(this);
    
    // Set window properties
    setWindowModality(Qt::WindowModal);
    setFixedSize(size()); // Prevent resizing
    
    // Connect signals
    connect(ui->pushButton_Save, &QPushButton::clicked, this, &EditEncryptedFileDialog::on_pushButton_Save_clicked);
    connect(ui->pushButton_Cancel, &QPushButton::clicked, this, &EditEncryptedFileDialog::on_pushButton_Cancel_clicked);
}

EditEncryptedFileDialog::~EditEncryptedFileDialog()
{
    if (m_metadataManager) {
        delete m_metadataManager;
        m_metadataManager = nullptr;
    }
    delete ui;
}

void EditEncryptedFileDialog::initialize(const QString& encryptedFilePath, const QByteArray& encryptionKey, const QString& username)
{
    m_encryptedFilePath = encryptedFilePath;
    m_encryptionKey = encryptionKey;
    m_username = username;
    
    // Clean up any existing metadata manager
    if (m_metadataManager) {
        delete m_metadataManager;
    }
    
    // Create metadata manager
    m_metadataManager = new EncryptedFileMetadata(encryptionKey, username);
    
    // Load and display current metadata
    loadCurrentMetadata();
    populateFields();
}

void EditEncryptedFileDialog::loadCurrentMetadata()
{
    if (!m_metadataManager) {
        qWarning() << "Metadata manager not initialized";
        return;
    }
    
    // Check if file has new metadata format
    if (m_metadataManager->hasNewFormat(m_encryptedFilePath)) {
        // Load metadata from file
        if (!m_metadataManager->readMetadataFromFile(m_encryptedFilePath, m_originalMetadata)) {
            qWarning() << "Failed to read metadata from file:" << m_encryptedFilePath;
            
            // Fallback: try to get just the filename
            QString filename = m_metadataManager->getFilenameFromFile(m_encryptedFilePath);
            if (!filename.isEmpty()) {
                m_originalMetadata = EncryptedFileMetadata::FileMetadata(filename);
            } else {
                // Last resort: use the encrypted filename
                QFileInfo fileInfo(m_encryptedFilePath);
                m_originalMetadata = EncryptedFileMetadata::FileMetadata(fileInfo.fileName());
            }
        }
    } else {
        // Old format file - try to extract filename only
        QString filename = m_metadataManager->getFilenameFromFile(m_encryptedFilePath);
        if (!filename.isEmpty()) {
            m_originalMetadata = EncryptedFileMetadata::FileMetadata(filename);
        } else {
            // Fallback to encrypted filename
            QFileInfo fileInfo(m_encryptedFilePath);
            m_originalMetadata = EncryptedFileMetadata::FileMetadata(fileInfo.fileName());
        }
    }
    
    qDebug() << "Loaded metadata - Filename:" << m_originalMetadata.filename 
             << "Category:" << m_originalMetadata.category 
             << "Tags:" << m_originalMetadata.tags;
}

void EditEncryptedFileDialog::populateFields()
{
    // Populate filename field
    ui->lineEdit_Filename->setText(m_originalMetadata.filename);
    
    // Populate category field
    ui->lineEdit_Category->setText(m_originalMetadata.category);
    
    // Populate tags field (join with semicolons)
    QString tagsString = m_originalMetadata.tags.join(";");
    ui->lineEdit_Tags->setText(tagsString);
    
    // Set focus to filename field
    ui->lineEdit_Filename->setFocus();
    ui->lineEdit_Filename->selectAll();
}

bool EditEncryptedFileDialog::validateAllInputs()
{
    QString filename = ui->lineEdit_Filename->text().trimmed();
    QString category = ui->lineEdit_Category->text().trimmed();
    QString tagsString = ui->lineEdit_Tags->text().trimmed();
    
    // Validate filename
    if (!validateFilename(filename)) {
        ui->lineEdit_Filename->setFocus();
        return false;
    }
    
    // Validate category
    if (!validateCategory(category)) {
        ui->lineEdit_Category->setFocus();
        return false;
    }
    
    // Validate tags
    QStringList parsedTags;
    if (!validateTags(tagsString, parsedTags)) {
        ui->lineEdit_Tags->setFocus();
        return false;
    }
    
    return true;
}

bool EditEncryptedFileDialog::validateFilename(const QString& filename)
{
    if (filename.isEmpty()) {
        QMessageBox::warning(this, "Invalid Filename", "Filename cannot be empty.");
        return false;
    }
    
    // Use existing filename validation
    InputValidation::ValidationResult result = InputValidation::validateInput(
        filename, InputValidation::InputType::FileName, 255);
    
    if (!result.isValid) {
        QMessageBox::warning(this, "Invalid Filename", 
                             "Invalid filename: " + result.errorMessage);
        return false;
    }
    
    return true;
}

bool EditEncryptedFileDialog::validateCategory(const QString& category)
{
    // Empty category is allowed
    if (category.isEmpty()) {
        return true;
    }
    
    // Check length
    if (category.length() > EncryptedFileMetadata::MAX_CATEGORY_LENGTH) {
        QMessageBox::warning(this, "Invalid Category", 
                             QString("Category too long. Maximum %1 characters allowed.")
                             .arg(EncryptedFileMetadata::MAX_CATEGORY_LENGTH));
        return false;
    }
    
    // Check for invalid characters - allow only alphanumeric, spaces, and basic punctuation
    QRegularExpression validCharsPattern("^[a-zA-Z0-9\\s\\-_.,!?()]+$");
    if (!validCharsPattern.match(category).hasMatch()) {
        QMessageBox::warning(this, "Invalid Category", 
                             "Category contains invalid characters. Only letters, numbers, spaces, and basic punctuation are allowed.");
        return false;
    }
    
    // Check for leading/trailing spaces
    if (category != category.trimmed()) {
        QMessageBox::warning(this, "Invalid Category", 
                             "Category cannot have leading or trailing spaces.");
        return false;
    }
    
    // Check for multiple consecutive spaces
    if (category.contains("  ")) {
        QMessageBox::warning(this, "Invalid Category", 
                             "Category cannot contain multiple consecutive spaces.");
        return false;
    }
    
    return true;
}

bool EditEncryptedFileDialog::validateTags(const QString& tagsString, QStringList& parsedTags)
{
    parsedTags.clear();
    
    // Empty tags string is allowed
    if (tagsString.isEmpty()) {
        return true;
    }
    
    // Split by semicolon
    QStringList rawTags = tagsString.split(';', Qt::SkipEmptyParts);
    
    // Check tag count
    if (rawTags.size() > EncryptedFileMetadata::MAX_TAGS) {
        QMessageBox::warning(this, "Too Many Tags", 
                             QString("Too many tags. Maximum %1 tags allowed, but %2 were provided.")
                             .arg(EncryptedFileMetadata::MAX_TAGS).arg(rawTags.size()));
        return false;
    }
    
    // Validate each tag
    for (const QString& rawTag : rawTags) {
        QString tag = rawTag.trimmed();
        
        // Skip empty tags
        if (tag.isEmpty()) {
            continue;
        }
        
        // Check tag length
        if (tag.length() > EncryptedFileMetadata::MAX_TAG_LENGTH) {
            QMessageBox::warning(this, "Invalid Tag", 
                                 QString("Tag '%1' is too long. Maximum %2 characters allowed per tag.")
                                 .arg(tag).arg(EncryptedFileMetadata::MAX_TAG_LENGTH));
            return false;
        }
        
        // Use the static validation method
        if (!EncryptedFileMetadata::isValidTag(tag)) {
            QMessageBox::warning(this, "Invalid Tag", 
                                 QString("Tag '%1' contains invalid characters. Only letters, numbers, spaces, and basic punctuation are allowed.")
                                 .arg(tag));
            return false;
        }
        
        // Check for duplicates
        if (parsedTags.contains(tag, Qt::CaseInsensitive)) {
            QMessageBox::warning(this, "Duplicate Tag", 
                                 QString("Duplicate tag found: '%1'. Each tag should be unique.")
                                 .arg(tag));
            return false;
        }
        
        parsedTags.append(tag);
    }
    
    return true;
}

bool EditEncryptedFileDialog::saveMetadata()
{
    if (!m_metadataManager) {
        qWarning() << "Metadata manager not initialized";
        return false;
    }
    
    // Get validated input values
    QString filename = ui->lineEdit_Filename->text().trimmed();
    QString category = ui->lineEdit_Category->text().trimmed();
    QString tagsString = ui->lineEdit_Tags->text().trimmed();
    
    // Parse tags
    QStringList tags;
    if (!validateTags(tagsString, tags)) {
        return false; // Validation should have shown error message
    }
    
    // Create new metadata
    EncryptedFileMetadata::FileMetadata newMetadata(filename, category, tags);
    
    // Check if metadata actually changed
    bool hasChanges = (newMetadata.filename != m_originalMetadata.filename ||
                       newMetadata.category != m_originalMetadata.category ||
                       newMetadata.tags != m_originalMetadata.tags);
    
    if (!hasChanges) {
        qDebug() << "No changes detected, skipping save";
        return true; // No changes, but that's OK
    }
    
    // Save metadata to file
    if (!m_metadataManager->updateMetadataInFile(m_encryptedFilePath, newMetadata)) {
        QMessageBox::critical(this, "Save Failed", 
                              "Failed to save metadata to file. The file may be in use or corrupted.");
        return false;
    }
    
    qDebug() << "Successfully saved metadata changes";
    return true;
}

void EditEncryptedFileDialog::on_pushButton_Save_clicked()
{
    // Validate all inputs
    if (!validateAllInputs()) {
        return; // Validation failed, error message already shown
    }
    
    // Save metadata
    if (saveMetadata()) {
        accept(); // Close dialog with success
    }
    // If save failed, stay in dialog (error message already shown)
}

void EditEncryptedFileDialog::on_pushButton_Cancel_clicked()
{
    reject(); // Close dialog without saving
}
