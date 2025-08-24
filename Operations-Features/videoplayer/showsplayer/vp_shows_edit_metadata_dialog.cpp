#include "vp_shows_edit_metadata_dialog.h"
#include "ui_vp_shows_edit_metadata_dialog.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <QPixmap>
#include <QBuffer>
#include <QDate>
#include <QImageReader>

VP_ShowsEditMetadataDialog::VP_ShowsEditMetadataDialog(const QString& videoFilePath,
                                                       const QByteArray& encryptionKey,
                                                       const QString& username,
                                                       QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsEditMetadataDialog)
    , m_videoFilePath(videoFilePath)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_wasModified(false)
    , m_imagePreviewLabel(nullptr)
    , m_contentTypeCombo(nullptr)
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsEditMetadataDialog: Created dialog for file:" << videoFilePath;
    
    // Store references to special UI elements
    m_imagePreviewLabel = ui->label_ImagePreview;
    m_contentTypeCombo = ui->comboBox_ContentType;
    
    // Set file path in read-only label
    ui->label_FilePathValue->setText(videoFilePath);
    
    // Load metadata from file
    if (!loadMetadata()) {
        QMessageBox::critical(this, tr("Error"), 
                            tr("Failed to load metadata from file."));
        reject();
        return;
    }
    
    // Populate UI with loaded metadata
    populateUI();
    
    // Connect signals for change tracking
    connect(ui->lineEdit_Filename, &QLineEdit::textChanged, 
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->lineEdit_ShowName, &QLineEdit::textChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->lineEdit_Season, &QLineEdit::textChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->lineEdit_Episode, &QLineEdit::textChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->lineEdit_EPName, &QLineEdit::textChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->comboBox_Language, &QComboBox::currentTextChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->comboBox_Translation, &QComboBox::currentTextChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->comboBox_ContentType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VP_ShowsEditMetadataDialog::onContentTypeChanged);
    connect(ui->dateEdit_AirDate, &QDateEdit::dateChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    connect(ui->checkBox_DualDisplay, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMetadataDialog::onFieldChanged);
    
    // Connect image buttons
    connect(ui->pushButton_SelectImage, &QPushButton::clicked,
            this, &VP_ShowsEditMetadataDialog::onSelectImageClicked);
    connect(ui->pushButton_RemoveImage, &QPushButton::clicked,
            this, &VP_ShowsEditMetadataDialog::onRemoveImageClicked);
    
    // Update remove image button state
    ui->pushButton_RemoveImage->setEnabled(!m_metadata.EPImage.isEmpty());
}

VP_ShowsEditMetadataDialog::~VP_ShowsEditMetadataDialog()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Destructor called";
    delete ui;
}

bool VP_ShowsEditMetadataDialog::loadMetadata()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Loading metadata from file";
    
    // Validate file path using operations_files
    if (!OperationsFiles::isWithinAllowedDirectory(m_videoFilePath, "Data")) {
        qDebug() << "VP_ShowsEditMetadataDialog: File path outside allowed directory";
        return false;
    }
    
    // Create metadata manager
    VP_ShowsMetadata metadataManager(m_encryptionKey, m_username);
    
    // Read metadata from file
    if (!metadataManager.readMetadataFromFile(m_videoFilePath, m_metadata)) {
        qDebug() << "VP_ShowsEditMetadataDialog: Failed to read metadata from file";
        return false;
    }
    
    // Store original metadata for comparison
    m_originalMetadata = m_metadata;
    
    qDebug() << "VP_ShowsEditMetadataDialog: Metadata loaded successfully";
    qDebug() << "VP_ShowsEditMetadataDialog:   Show:" << m_metadata.showName;
    qDebug() << "VP_ShowsEditMetadataDialog:   Episode:" << m_metadata.EPName;
    qDebug() << "VP_ShowsEditMetadataDialog:   Language:" << m_metadata.language;
    qDebug() << "VP_ShowsEditMetadataDialog:   Translation:" << m_metadata.translation;
    
    return true;
}

void VP_ShowsEditMetadataDialog::populateUI()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Populating UI with metadata";
    
    // Basic information
    ui->lineEdit_Filename->setText(m_metadata.filename);
    ui->lineEdit_ShowName->setText(m_metadata.showName);
    ui->lineEdit_Season->setText(m_metadata.season);
    ui->lineEdit_Episode->setText(m_metadata.episode);
    ui->lineEdit_EPName->setText(m_metadata.EPName);
    
    // Language settings
    ui->comboBox_Language->setCurrentText(m_metadata.language);
    ui->comboBox_Translation->setCurrentText(m_metadata.translation);
    
    // Content settings
    ui->comboBox_ContentType->setCurrentIndex(static_cast<int>(m_metadata.contentType));
    ui->checkBox_DualDisplay->setChecked(m_metadata.isDualDisplay);
    
    // Air date
    if (!m_metadata.airDate.isEmpty()) {
        QDate date = QDate::fromString(m_metadata.airDate, "yyyy-MM-dd");
        if (date.isValid()) {
            ui->dateEdit_AirDate->setDate(date);
        } else {
            ui->dateEdit_AirDate->setDate(QDate::currentDate());
        }
    } else {
        ui->dateEdit_AirDate->setDate(QDate::currentDate());
    }
    
    // Episode image
    updateImagePreview();
    
    // Read-only information
    if (m_metadata.encryptionDateTime.isValid()) {
        ui->label_EncryptionDateValue->setText(
            m_metadata.encryptionDateTime.toString("yyyy-MM-dd hh:mm:ss"));
    } else {
        ui->label_EncryptionDateValue->setText(tr("Unknown"));
    }
}

void VP_ShowsEditMetadataDialog::updateImagePreview()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Updating image preview";
    
    if (m_metadata.EPImage.isEmpty()) {
        m_imagePreviewLabel->setText(tr("No Image"));
        m_imagePreviewLabel->setPixmap(QPixmap());
        ui->pushButton_RemoveImage->setEnabled(false);
    } else {
        QPixmap pixmap;
        if (pixmap.loadFromData(m_metadata.EPImage)) {
            // Scale to fit label while maintaining aspect ratio
            QPixmap scaled = pixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_imagePreviewLabel->setPixmap(scaled);
            ui->pushButton_RemoveImage->setEnabled(true);
            
            qDebug() << "VP_ShowsEditMetadataDialog: Image loaded, size:" << m_metadata.EPImage.size() << "bytes";
        } else {
            m_imagePreviewLabel->setText(tr("Invalid Image"));
            qDebug() << "VP_ShowsEditMetadataDialog: Failed to load image from data";
        }
    }
}

void VP_ShowsEditMetadataDialog::onContentTypeChanged(int index)
{
    qDebug() << "VP_ShowsEditMetadataDialog: Content type changed to index:" << index;
    
    // Update dual display checkbox visibility/relevance based on content type
    // Dual display is mainly relevant for special content types (Movie, OVA, Extra)
    bool isSpecialContent = (index != 0); // Not Regular Episode
    
    if (!isSpecialContent) {
        ui->checkBox_DualDisplay->setChecked(false);
        ui->checkBox_DualDisplay->setToolTip(tr("Only applicable for special content types"));
    } else {
        ui->checkBox_DualDisplay->setToolTip(tr("Show this content in both regular episodes and its special category"));
    }
    
    onFieldChanged();
}

void VP_ShowsEditMetadataDialog::onSelectImageClicked()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Select image clicked";
    
    QString filter = tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)");
    QString imagePath = QFileDialog::getOpenFileName(this, tr("Select Episode Image"), 
                                                     QString(), filter);
    
    if (imagePath.isEmpty()) {
        return;
    }
    
    // Validate file path
    InputValidation::ValidationResult result = InputValidation::validateInput(
        imagePath, InputValidation::InputType::ExternalFilePath, 1000);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Invalid File"), 
                           tr("Selected file is invalid: %1").arg(result.errorMessage));
        return;
    }
    
    // Load and resize image
    QImageReader reader(imagePath);
    reader.setScaledSize(QSize(128, 128));
    QImage image = reader.read();
    
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Error"), 
                           tr("Failed to load image from file."));
        return;
    }
    
    // Convert to byte array (PNG format for consistency)
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    
    // Check size limit (32KB max)
    if (imageData.size() > VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
        // Try to compress more with JPEG
        imageData.clear();
        buffer.seek(0);
        image.save(&buffer, "JPEG", 80);
        
        if (imageData.size() > VP_ShowsMetadata::MAX_EP_IMAGE_SIZE) {
            QMessageBox::warning(this, tr("Image Too Large"), 
                               tr("The image is too large. Maximum size is 32KB."));
            return;
        }
    }
    
    // Update metadata and UI
    m_metadata.EPImage = imageData;
    updateImagePreview();
    onFieldChanged();
    
    qDebug() << "VP_ShowsEditMetadataDialog: Image selected and loaded, size:" << imageData.size() << "bytes";
}

void VP_ShowsEditMetadataDialog::onRemoveImageClicked()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Remove image clicked";
    
    m_metadata.EPImage.clear();
    updateImagePreview();
    onFieldChanged();
}

void VP_ShowsEditMetadataDialog::onFieldChanged()
{
    // Mark that a field has changed (actual comparison done in checkForModifications)
    // We don't set m_wasModified here as we want to compare actual values
    qDebug() << "VP_ShowsEditMetadataDialog: Field changed, will check for modifications on accept";
}

bool VP_ShowsEditMetadataDialog::validateInput()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Validating input fields";
    
    // Validate filename
    QString filename = ui->lineEdit_Filename->text().trimmed();
    if (filename.isEmpty()) {
        QMessageBox::warning(this, tr("Validation Error"), 
                           tr("Filename cannot be empty."));
        ui->lineEdit_Filename->setFocus();
        return false;
    }
    
    InputValidation::ValidationResult result = InputValidation::validateInput(
        filename, InputValidation::InputType::FileName);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Validation Error"), 
                           tr("Invalid filename: %1").arg(result.errorMessage));
        ui->lineEdit_Filename->setFocus();
        return false;
    }
    
    // Validate show name
    QString showName = ui->lineEdit_ShowName->text().trimmed();
    if (showName.isEmpty()) {
        QMessageBox::warning(this, tr("Validation Error"), 
                           tr("Show name cannot be empty."));
        ui->lineEdit_ShowName->setFocus();
        return false;
    }
    
    if (!VP_ShowsMetadata::isValidShowName(showName)) {
        QMessageBox::warning(this, tr("Validation Error"), 
                           tr("Show name is too long (maximum 100 characters)."));
        ui->lineEdit_ShowName->setFocus();
        return false;
    }
    
    // Validate episode number if provided
    QString episode = ui->lineEdit_Episode->text().trimmed();
    if (!episode.isEmpty()) {
        if (episode.length() > VP_ShowsMetadata::MAX_EPISODE_LENGTH) {
            QMessageBox::warning(this, tr("Validation Error"), 
                               tr("Episode number is too long (maximum 100 characters)."));
            ui->lineEdit_Episode->setFocus();
            return false;
        }
    }
    
    // Validate episode name if provided
    QString epName = ui->lineEdit_EPName->text().trimmed();
    if (!epName.isEmpty()) {
        if (epName.length() > VP_ShowsMetadata::MAX_EP_NAME_LENGTH) {
            QMessageBox::warning(this, tr("Validation Error"), 
                               tr("Episode name is too long (maximum 200 characters)."));
            ui->lineEdit_EPName->setFocus();
            return false;
        }
    }
    
    qDebug() << "VP_ShowsEditMetadataDialog: Validation successful";
    return true;
}

void VP_ShowsEditMetadataDialog::updateMetadataFromUI()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Updating metadata from UI";
    
    // Basic information
    m_metadata.filename = ui->lineEdit_Filename->text().trimmed();
    m_metadata.showName = ui->lineEdit_ShowName->text().trimmed();
    m_metadata.season = ui->lineEdit_Season->text().trimmed();
    m_metadata.episode = ui->lineEdit_Episode->text().trimmed();
    m_metadata.EPName = ui->lineEdit_EPName->text().trimmed();
    
    // Language settings
    m_metadata.language = ui->comboBox_Language->currentText();
    m_metadata.translation = ui->comboBox_Translation->currentText();
    
    // Content settings
    m_metadata.contentType = static_cast<VP_ShowsMetadata::ContentType>(
        ui->comboBox_ContentType->currentIndex());
    m_metadata.isDualDisplay = ui->checkBox_DualDisplay->isChecked();
    
    // Air date
    m_metadata.airDate = ui->dateEdit_AirDate->date().toString("yyyy-MM-dd");
    
    // Note: EPImage is already updated by the image selection/removal functions
    // Note: encryptionDateTime is read-only and not updated
}

void VP_ShowsEditMetadataDialog::checkForModifications()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Checking for modifications";
    
    // Update metadata from UI first
    updateMetadataFromUI();
    
    // Compare with original metadata
    m_wasModified = false;
    
    if (m_metadata.filename != m_originalMetadata.filename) {
        qDebug() << "VP_ShowsEditMetadataDialog: Filename changed";
        m_wasModified = true;
    }
    if (m_metadata.showName != m_originalMetadata.showName) {
        qDebug() << "VP_ShowsEditMetadataDialog: Show name changed";
        m_wasModified = true;
    }
    if (m_metadata.season != m_originalMetadata.season) {
        qDebug() << "VP_ShowsEditMetadataDialog: Season changed";
        m_wasModified = true;
    }
    if (m_metadata.episode != m_originalMetadata.episode) {
        qDebug() << "VP_ShowsEditMetadataDialog: Episode changed";
        m_wasModified = true;
    }
    if (m_metadata.EPName != m_originalMetadata.EPName) {
        qDebug() << "VP_ShowsEditMetadataDialog: Episode name changed";
        m_wasModified = true;
    }
    if (m_metadata.language != m_originalMetadata.language) {
        qDebug() << "VP_ShowsEditMetadataDialog: Language changed";
        m_wasModified = true;
    }
    if (m_metadata.translation != m_originalMetadata.translation) {
        qDebug() << "VP_ShowsEditMetadataDialog: Translation changed";
        m_wasModified = true;
    }
    if (m_metadata.contentType != m_originalMetadata.contentType) {
        qDebug() << "VP_ShowsEditMetadataDialog: Content type changed";
        m_wasModified = true;
    }
    if (m_metadata.isDualDisplay != m_originalMetadata.isDualDisplay) {
        qDebug() << "VP_ShowsEditMetadataDialog: Dual display changed";
        m_wasModified = true;
    }
    if (m_metadata.airDate != m_originalMetadata.airDate) {
        qDebug() << "VP_ShowsEditMetadataDialog: Air date changed";
        m_wasModified = true;
    }
    if (m_metadata.EPImage != m_originalMetadata.EPImage) {
        qDebug() << "VP_ShowsEditMetadataDialog: Episode image changed";
        m_wasModified = true;
    }
    
    qDebug() << "VP_ShowsEditMetadataDialog: Modifications detected:" << m_wasModified;
}

void VP_ShowsEditMetadataDialog::accept()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Accept clicked";
    
    // Validate input first
    if (!validateInput()) {
        return;
    }
    
    // Check for modifications
    checkForModifications();
    
    if (m_wasModified) {
        qDebug() << "VP_ShowsEditMetadataDialog: Saving modified metadata";
        
        // Update metadata from UI
        updateMetadataFromUI();
        
        // Create metadata manager
        VP_ShowsMetadata metadataManager(m_encryptionKey, m_username);
        
        // Write metadata back to file
        if (!metadataManager.writeMetadataToFile(m_videoFilePath, m_metadata)) {
            QMessageBox::critical(this, tr("Error"), 
                                tr("Failed to save metadata to file."));
            return;
        }
        
        qDebug() << "VP_ShowsEditMetadataDialog: Metadata saved successfully";
    } else {
        qDebug() << "VP_ShowsEditMetadataDialog: No modifications detected, nothing to save";
    }
    
    // Call base class accept
    QDialog::accept();
}

QString VP_ShowsEditMetadataDialog::formatDate(const QString& date) const
{
    if (date.isEmpty()) {
        return tr("Unknown");
    }
    
    QDate qdate = QDate::fromString(date, "yyyy-MM-dd");
    if (qdate.isValid()) {
        return qdate.toString("MMMM d, yyyy");
    }
    
    return date;
}
