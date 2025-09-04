#include "encrypteddata_editencryptedfiledialog.h"
#include "qpushbutton.h"
#include "ui_encrypteddata_editencryptedfiledialog.h"
#include "inputvalidation.h"
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

void EditEncryptedFileDialog::splitFilenameAndExtension(const QString& fullFilename, QString& baseName, QString& extension)
{
    QFileInfo fileInfo(fullFilename);

    // Get the complete base name (filename without the last extension only)
    // This preserves dots in the filename except for the final extension
    baseName = fileInfo.completeBaseName();

    // Get the extension (including the dot)
    QString suffix = fileInfo.suffix();
    if (!suffix.isEmpty()) {
        extension = "." + suffix;
    } else {
        extension.clear();
    }

    qDebug() << "Split filename:" << fullFilename << "-> Base:" << baseName << "Extension:" << extension;
}

void EditEncryptedFileDialog::loadCurrentMetadata()
{
    qDebug() << "Loading current metadata for editing, preserving encryption datetime";

    // Clear any existing metadata
    m_originalMetadata = EncryptedFileMetadata::FileMetadata();

    if (!m_metadataManager) {
        qWarning() << "Metadata manager not initialized";
        return;
    }

    // Try to read the current metadata (this will include encryption datetime if available)
    if (!m_metadataManager->readMetadataFromFile(m_encryptedFilePath, m_originalMetadata)) {
        qWarning() << "Failed to read metadata from file:" << m_encryptedFilePath;
        qWarning() << "File may have corrupted metadata - creating minimal metadata for editing";

        // Create minimal metadata with just the filename extracted from encrypted filename
        QFileInfo fileInfo(m_encryptedFilePath);
        QString fullFileName = fileInfo.fileName(); // e.g., "randomstring.jpg.mmenc"

        if (fullFileName.endsWith(".mmenc", Qt::CaseInsensitive)) {
            m_originalMetadata.filename = fullFileName.left(fullFileName.length() - 6);
        } else {
            m_originalMetadata.filename = fileInfo.baseName();
        }

        // Note: encryptionDateTime will remain invalid for corrupted files
        qDebug() << "Created minimal metadata with filename:" << m_originalMetadata.filename;
    } else {
        qDebug() << "Successfully loaded metadata:";
        qDebug() << "  Filename:" << m_originalMetadata.filename;
        qDebug() << "  Category:" << m_originalMetadata.category;
        qDebug() << "  Tags:" << m_originalMetadata.tags.join(", ");
        qDebug() << "  Has thumbnail:" << (!m_originalMetadata.thumbnailData.isEmpty());
        qDebug() << "  Has encryption date:" << m_originalMetadata.hasEncryptionDateTime();

        if (m_originalMetadata.hasEncryptionDateTime()) {
            qDebug() << "  Encryption date:" << m_originalMetadata.encryptionDateTime.toString();
        }
    }

    // Split the filename into base name and extension
    QString baseName;
    splitFilenameAndExtension(m_originalMetadata.filename, baseName, m_originalExtension);

    qDebug() << "Loaded metadata - Full filename:" << m_originalMetadata.filename
             << "Base name:" << baseName
             << "Extension:" << m_originalExtension
             << "Category:" << m_originalMetadata.category
             << "Tags:" << m_originalMetadata.tags;
}

void EditEncryptedFileDialog::populateFields()
{
    // Split filename and only show the base name (without extension)
    QString baseName;
    QString extension; // We already have m_originalExtension, but this is for clarity
    splitFilenameAndExtension(m_originalMetadata.filename, baseName, extension);

    // Populate filename field with only the base name (no extension)
    ui->lineEdit_Filename->setText(baseName);

    // Populate category field
    ui->lineEdit_Category->setText(m_originalMetadata.category);

    // Populate tags field (join with semicolons)
    QString tagsString = m_originalMetadata.tags.join(";");
    ui->lineEdit_Tags->setText(tagsString);

    // Set focus to filename field
    ui->lineEdit_Filename->setFocus();
    ui->lineEdit_Filename->selectAll();

    // Update window title to show the extension and optionally encryption date
    QString windowTitle = QString("Edit File: %1%2").arg(baseName, m_originalExtension);

    // NEW: Optionally show encryption date in window title or status
    if (m_originalMetadata.hasEncryptionDateTime()) {
        QString encryptionDateStr = m_originalMetadata.encryptionDateTime.toString("MMM dd, yyyy hh:mm");
        windowTitle += QString(" (Encrypted: %1)").arg(encryptionDateStr);
        qDebug() << "Displaying encryption date in edit dialog:" << encryptionDateStr;
    } else {
        windowTitle += " (Legacy file)";
        qDebug() << "No encryption date available for display";
    }

    setWindowTitle(windowTitle);
}

bool EditEncryptedFileDialog::validateAllInputs()
{
    QString filename = ui->lineEdit_Filename->text().trimmed();
    QString category = ui->lineEdit_Category->text().trimmed();
    QString tagsString = ui->lineEdit_Tags->text().trimmed();

    // Validate filename (now just the base name)
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

    // Create the full filename for validation (base name + extension)
    QString fullFilename = filename + m_originalExtension;

    // Use existing filename validation on the full filename
    InputValidation::ValidationResult result = InputValidation::validateInput(
        fullFilename, InputValidation::InputType::FileName, 255);

    if (!result.isValid) {
        QMessageBox::warning(this, "Invalid Filename",
                             "Invalid filename: " + result.errorMessage);
        return false;
    }

    // Additional validation: ensure the base name itself doesn't contain problematic characters
    // (even though the full validation above should catch these)
    QRegularExpression invalidChars("[\\\\/:*?\"<>|]");
    if (invalidChars.match(filename).hasMatch()) {
        QMessageBox::warning(this, "Invalid Filename",
                             "Filename contains invalid characters (\\/:*?\"<>|).");
        return false;
    }

    // Check for leading/trailing dots or spaces
    if (filename.startsWith('.') || filename.endsWith('.') ||
        filename.startsWith(' ') || filename.endsWith(' ')) {
        QMessageBox::warning(this, "Invalid Filename",
                             "Filename cannot start or end with dots or spaces.");
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

    // Use InputValidation for consistent validation
    InputValidation::ValidationResult result = InputValidation::validateInput(
        category, InputValidation::InputType::CategoryTag, EncryptedFileMetadata::MAX_CATEGORY_LENGTH);

    if (!result.isValid) {
        QMessageBox::warning(this, "Invalid Category", result.errorMessage);
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

        // Use InputValidation for consistent validation
        InputValidation::ValidationResult tagResult = InputValidation::validateInput(
            tag, InputValidation::InputType::CategoryTag, EncryptedFileMetadata::MAX_TAG_LENGTH);
        
        if (!tagResult.isValid) {
            QMessageBox::warning(this, "Invalid Tag",
                                 QString("Tag '%1' is invalid: %2")
                                     .arg(tag).arg(tagResult.errorMessage));
            return false;
        }

        // Check for duplicates
        if (parsedTags.contains(tag, Qt::CaseInsensitive)) {
            QMessageBox::warning(this, "Invalid Tag",
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
    qDebug() << "Saving metadata with encryption datetime preservation";

    if (!m_metadataManager) {
        qWarning() << "Metadata manager not initialized";
        return false;
    }

    // Get validated input values
    QString baseName = ui->lineEdit_Filename->text().trimmed();
    QString category = ui->lineEdit_Category->text().trimmed();
    QString tagsString = ui->lineEdit_Tags->text().trimmed();

    // Reconstruct the full filename with the original extension
    QString fullFilename = baseName + m_originalExtension;

    qDebug() << "Reconstructing filename - Base:" << baseName
             << "Extension:" << m_originalExtension
             << "Full:" << fullFilename;

    // Parse tags
    QStringList tags;
    if (!validateTags(tagsString, tags)) {
        return false; // Validation should have shown error message
    }

    // UPDATED: Create new metadata structure with all preserved data
    EncryptedFileMetadata::FileMetadata newMetadata;
    newMetadata.filename = fullFilename;
    newMetadata.category = category;
    newMetadata.tags = tags;

    // CRITICAL: Preserve existing thumbnail data
    newMetadata.thumbnailData = m_originalMetadata.thumbnailData;

    // CRITICAL: Preserve existing encryption datetime if it exists
    if (m_originalMetadata.hasEncryptionDateTime()) {
        newMetadata.encryptionDateTime = m_originalMetadata.encryptionDateTime;
        qDebug() << "Preserved encryption datetime during edit:" << newMetadata.encryptionDateTime.toString();
    } else {
        qDebug() << "No encryption datetime to preserve (older file format or corrupted metadata)";
        // Leave encryptionDateTime invalid for files that don't have it
    }

    // Check if metadata actually changed (excluding thumbnail and datetime since we're preserving them)
    bool hasChanges = (newMetadata.filename != m_originalMetadata.filename ||
                       newMetadata.category != m_originalMetadata.category ||
                       newMetadata.tags != m_originalMetadata.tags);

    if (!hasChanges) {
        qDebug() << "No changes detected, skipping save";
        return true; // No changes, but that's OK
    }

    qDebug() << "Saving metadata changes (preserving thumbnail and encryption datetime):"
             << "Old filename:" << m_originalMetadata.filename << "-> New:" << newMetadata.filename
             << "Old category:" << m_originalMetadata.category << "-> New:" << newMetadata.category
             << "Old tags:" << m_originalMetadata.tags << "-> New:" << newMetadata.tags
             << "Thumbnail preserved:" << (!newMetadata.thumbnailData.isEmpty()) << "bytes:" << newMetadata.thumbnailData.size()
             << "Encryption date preserved:" << newMetadata.hasEncryptionDateTime();

    // Save metadata to file
    if (!m_metadataManager->updateMetadataInFile(m_encryptedFilePath, newMetadata)) {
        QMessageBox::critical(this, "Save Failed",
                              "Failed to save metadata to file. The file may be in use or corrupted.");
        return false;
    }

    qDebug() << "Successfully saved metadata changes with preserved thumbnail and encryption datetime";
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
