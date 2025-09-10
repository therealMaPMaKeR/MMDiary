#include "vp_shows_edit_metadata_dialog.h"
#include "ui_vp_shows_edit_metadata_dialog.h"
#include "vp_shows_settings.h"
#include "vp_shows_tmdb.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <QPixmap>
#include <QBuffer>
#include <QDate>
#include <QImageReader>
#include <QFileInfo>

VP_ShowsEditMetadataDialog::VP_ShowsEditMetadataDialog(const QString& videoFilePath,
                                                       const QByteArray& encryptionKey,
                                                       const QString& username,
                                                       bool repairMode,
                                                       const QString& showName,
                                                       QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsEditMetadataDialog)
    , m_videoFilePath(videoFilePath)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_wasModified(false)
    , m_repairMode(repairMode)
    , m_providedShowName(showName)
    , m_shouldReacquireTMDB(false)
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
    
    // In repair mode, initialize with empty metadata
    if (m_repairMode) {
        qDebug() << "VP_ShowsEditMetadataDialog: Repair mode - initializing empty metadata";
        
        // Set window title to indicate repair mode
        setWindowTitle(tr("Repair Video Metadata"));
        
        // Initialize metadata with empty values
        initializeEmptyMetadata();
        
        // Show a message to the user about repair mode
        QMessageBox::information(this, tr("Repair Mode"),
                               tr("The metadata header is corrupted.\n\n"
                                  "Please enter the correct information to recreate the metadata."
                                  "\n\nThe video content itself is intact."));
    } else {
        // Normal mode - load metadata from file
        if (!loadMetadata()) {
            QMessageBox::critical(this, tr("Error"), 
                                tr("Failed to load metadata from file."));
            reject();
            return;
        }
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
    connect(ui->textEdit_EPDescription, &QTextEdit::textChanged,
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
            this, &VP_ShowsEditMetadataDialog::onDualDisplayChanged);
    
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

void VP_ShowsEditMetadataDialog::initializeEmptyMetadata()
{
    qDebug() << "VP_ShowsEditMetadataDialog: Initializing empty metadata for repair mode";
    
    // Initialize metadata with empty/default values
    m_metadata = VP_ShowsMetadata::ShowMetadata();
    
    // Use the provided show name if available
    QString showName;
    if (!m_providedShowName.isEmpty()) {
        showName = m_providedShowName;
        qDebug() << "VP_ShowsEditMetadataDialog: Using provided show name:" << showName;
    } else {
        // Fallback: try to extract from the file path (though this will be obfuscated)
        QFileInfo fileInfo(m_videoFilePath);
        QDir parentDir = fileInfo.dir();
        if (parentDir.exists()) {
            showName = parentDir.dirName();
            qDebug() << "VP_ShowsEditMetadataDialog: Warning - Using obfuscated folder name as show name:" << showName;
        }
    }
    
    // Set some default values
    m_metadata.showName = showName;
    
    QFileInfo fileInfo(m_videoFilePath);
    m_metadata.filename = fileInfo.fileName();
    
    // Remove .mmvid extension from filename if present
    if (m_metadata.filename.endsWith(".mmvid", Qt::CaseInsensitive)) {
        m_metadata.filename = m_metadata.filename.left(m_metadata.filename.length() - 6);
    }
    
    // Set default language and translation
    m_metadata.language = "Japanese";  // Common default for anime content
    m_metadata.translation = "English";
    
    // Set default content type
    m_metadata.contentType = VP_ShowsMetadata::Regular;
    m_metadata.isDualDisplay = false;
    
    // Set today's date as default air date
    m_metadata.airDate = QDate::currentDate().toString("yyyy-MM-dd");
    
    // EPImage remains empty (no thumbnail)
    m_metadata.EPImage = QByteArray();
    
    // Store as original for comparison (all changes will be considered modifications)
    m_originalMetadata = m_metadata;
    
    // In repair mode, always consider the metadata as modified
    m_wasModified = true;
    
    qDebug() << "VP_ShowsEditMetadataDialog: Empty metadata initialized with show name:" << showName;
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
    
    // Read metadata from file (the manager now handles locking internally)
    if (!metadataManager.readMetadataFromFile(m_videoFilePath, m_metadata)) {
        qDebug() << "VP_ShowsEditMetadataDialog: Failed to read metadata from file";
        
        // Check if it failed due to lock timeout  
        // The lock is handled inside readMetadataFromFile now
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
    
    // Show name should always be read-only (determined from folder structure)
    ui->lineEdit_ShowName->setReadOnly(true);
    ui->lineEdit_ShowName->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");  // Visual indication with dark grey text
    ui->lineEdit_Season->setText(m_metadata.season);
    ui->lineEdit_Episode->setText(m_metadata.episode);
    ui->lineEdit_EPName->setText(m_metadata.EPName);
    ui->textEdit_EPDescription->setPlainText(m_metadata.EPDescription);
    
    // Language settings
    ui->comboBox_Language->setCurrentText(m_metadata.language);
    ui->comboBox_Translation->setCurrentText(m_metadata.translation);
    
    // Content settings
    ui->comboBox_ContentType->setCurrentIndex(static_cast<int>(m_metadata.contentType));
    ui->checkBox_DualDisplay->setChecked(m_metadata.isDualDisplay);
    
    // Apply proper state to season/episode fields based on content type and dual display
    bool isSpecialContent = (m_metadata.contentType != VP_ShowsMetadata::Regular);
    if (isSpecialContent) {
        ui->checkBox_DualDisplay->setEnabled(true);
        
        // If it's special content without dual display, disable season/episode fields
        if (!m_metadata.isDualDisplay) {
            ui->lineEdit_Season->setEnabled(false);
            ui->lineEdit_Episode->setEnabled(false);
            ui->lineEdit_Season->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
            ui->lineEdit_Episode->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
        }
    } else {
        // Regular episodes don't use dual display
        ui->checkBox_DualDisplay->setEnabled(false);
    }
    
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
    
    // Initialize the Re-acquire TMDB checkbox based on show settings
    // Get the show folder path from the video file path
    QFileInfo fileInfo(m_videoFilePath);
    QString showFolderPath = fileInfo.absolutePath();
    
    // Load show settings to get UseTMDB setting and show ID
    VP_ShowsSettings settingsManager(m_encryptionKey, m_username);
    VP_ShowsSettings::ShowSettings showSettings;
    if (settingsManager.loadShowSettings(showFolderPath, showSettings)) {
        // Check if show ID is valid
        bool hasValidShowId = !showSettings.showId.isEmpty() && showSettings.showId != "error";
        
        if (hasValidShowId) {
            // Set checkbox default state based on UseTMDB setting
            ui->checkBox_ReacquireTMDB->setChecked(showSettings.useTMDB);
            ui->checkBox_ReacquireTMDB->setEnabled(true);
            qDebug() << "VP_ShowsEditMetadataDialog: Valid show ID found:" << showSettings.showId;
            qDebug() << "VP_ShowsEditMetadataDialog: Set Re-acquire TMDB checkbox to:" << showSettings.useTMDB;
        } else {
            // Disable the checkbox if show ID is invalid
            ui->checkBox_ReacquireTMDB->setChecked(false);
            ui->checkBox_ReacquireTMDB->setEnabled(false);
            ui->checkBox_ReacquireTMDB->setToolTip(tr("TMDB re-acquisition is not available for this show (no valid show ID)"));
            qDebug() << "VP_ShowsEditMetadataDialog: Invalid or missing show ID, disabling Re-acquire TMDB checkbox";
        }
    } else {
        // Default to disabled if settings can't be loaded
        ui->checkBox_ReacquireTMDB->setChecked(false);
        ui->checkBox_ReacquireTMDB->setEnabled(false);
        ui->checkBox_ReacquireTMDB->setToolTip(tr("TMDB re-acquisition is not available (cannot load show settings)"));
        qDebug() << "VP_ShowsEditMetadataDialog: Could not load show settings, disabling Re-acquire TMDB";
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
        // Regular episodes don't need dual display option
        ui->checkBox_DualDisplay->setChecked(false);
        ui->checkBox_DualDisplay->setEnabled(false);
        ui->checkBox_DualDisplay->setToolTip(tr("Only applicable for special content types"));
        
        // Re-enable season and episode fields for regular episodes
        ui->lineEdit_Season->setEnabled(true);
        ui->lineEdit_Episode->setEnabled(true);
        ui->lineEdit_Season->setStyleSheet("");  // Reset style
        ui->lineEdit_Episode->setStyleSheet("");  // Reset style
        
        // If fields are empty (switching from special content), try to parse from filename
        bool needsParsing = false;
        if (ui->lineEdit_Episode->text().isEmpty() || ui->lineEdit_Episode->text().toInt() <= 0) {
            needsParsing = true;
        }
        
        if (needsParsing) {
            // Try to parse from filename
            int seasonNum = 0, episodeNum = 0;
            QString filename = ui->lineEdit_Filename->text();
            if (!filename.isEmpty() && VP_ShowsTMDB::parseEpisodeFromFilename(filename, seasonNum, episodeNum)) {
                if (episodeNum > 0) {
                    ui->lineEdit_Episode->setText(QString::number(episodeNum));
                    if (seasonNum > 0 && ui->lineEdit_Season->text().isEmpty()) {
                        ui->lineEdit_Season->setText(QString::number(seasonNum));
                    }
                    qDebug() << "VP_ShowsEditMetadataDialog: Parsed episode" << episodeNum << "from filename for Regular Episode";
                } else {
                    // Parsing failed, mark as error
                    ui->lineEdit_Episode->setText("error");
                    ui->lineEdit_Season->setText("error");
                    qDebug() << "VP_ShowsEditMetadataDialog: Could not parse valid episode from filename, marking as error";
                }
            } else {
                // Could not parse, mark as error
                ui->lineEdit_Episode->setText("error");
                ui->lineEdit_Season->setText("error");
                qDebug() << "VP_ShowsEditMetadataDialog: Failed to parse episode from filename, marking as error";
            }
        }
        
        // Ensure we have valid season if episode is valid
        if (ui->lineEdit_Season->text().isEmpty() && ui->lineEdit_Episode->text() != "error") {
            ui->lineEdit_Season->setText("1");
        }
    } else {
        // Movies, OVAs, and Extras can use dual display
        ui->checkBox_DualDisplay->setEnabled(true);
        ui->checkBox_DualDisplay->setToolTip(tr("Show this content in both regular episodes and its special category"));
        
        // If dual display is NOT checked, clear and disable season/episode fields
        // This ensures special content appears in the correct category
        if (!ui->checkBox_DualDisplay->isChecked()) {
            ui->lineEdit_Season->clear();
            ui->lineEdit_Episode->clear();
            ui->lineEdit_Season->setEnabled(false);
            ui->lineEdit_Episode->setEnabled(false);
            ui->lineEdit_Season->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
            ui->lineEdit_Episode->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
        } else {
            // If dual display IS checked, keep season/episode enabled
            ui->lineEdit_Season->setEnabled(true);
            ui->lineEdit_Episode->setEnabled(true);
            ui->lineEdit_Season->setStyleSheet("");
            ui->lineEdit_Episode->setStyleSheet("");
        }
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

void VP_ShowsEditMetadataDialog::onDualDisplayChanged(int state)
{
    qDebug() << "VP_ShowsEditMetadataDialog: Dual display changed to:" << (state == Qt::Checked);
    
    // Only handle this for special content types (Movies, OVAs, Extras)
    int contentTypeIndex = ui->comboBox_ContentType->currentIndex();
    bool isSpecialContent = (contentTypeIndex != 0); // Not Regular Episode
    
    if (isSpecialContent) {
        if (state == Qt::Checked) {
            // If dual display is checked, enable season/episode fields
            ui->lineEdit_Season->setEnabled(true);
            ui->lineEdit_Episode->setEnabled(true);
            ui->lineEdit_Season->setStyleSheet("");
            ui->lineEdit_Episode->setStyleSheet("");
            
            // If fields are empty, set defaults
            if (ui->lineEdit_Season->text().isEmpty()) {
                ui->lineEdit_Season->setText("1");
            }
            if (ui->lineEdit_Episode->text().isEmpty()) {
                ui->lineEdit_Episode->setText("1");
            }
        } else {
            // If dual display is unchecked, clear and disable season/episode fields
            ui->lineEdit_Season->clear();
            ui->lineEdit_Episode->clear();
            ui->lineEdit_Season->setEnabled(false);
            ui->lineEdit_Episode->setEnabled(false);
            ui->lineEdit_Season->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
            ui->lineEdit_Episode->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
        }
    }
    
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
        filename, InputValidation::InputType::FileName, 255);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Validation Error"), 
                           tr("Invalid filename: %1").arg(result.errorMessage));
        ui->lineEdit_Filename->setFocus();
        return false;
    }
    
    // Validate season if provided and enabled
    if (ui->lineEdit_Season->isEnabled()) {
        QString season = ui->lineEdit_Season->text().trimmed();
        if (!season.isEmpty()) {
            // Season should be numeric or alphanumeric (e.g., "1", "2A", "Special")
            InputValidation::ValidationResult seasonResult = InputValidation::validateInput(
                season, InputValidation::InputType::PlainText, 50);
            
            if (!seasonResult.isValid) {
                QMessageBox::warning(this, tr("Validation Error"), 
                                   tr("Invalid season: %1").arg(seasonResult.errorMessage));
                ui->lineEdit_Season->setFocus();
                return false;
            }
        }
    }
    
    // Validate episode number if provided and enabled
    if (ui->lineEdit_Episode->isEnabled()) {
        QString episode = ui->lineEdit_Episode->text().trimmed();
        if (!episode.isEmpty()) {
            // Episode can be numeric or alphanumeric (e.g., "1", "12.5", "OVA1")
            InputValidation::ValidationResult episodeResult = InputValidation::validateInput(
                episode, InputValidation::InputType::PlainText, VP_ShowsMetadata::MAX_EPISODE_LENGTH);
            
            if (!episodeResult.isValid) {
                QMessageBox::warning(this, tr("Validation Error"), 
                                   tr("Invalid episode number: %1").arg(episodeResult.errorMessage));
                ui->lineEdit_Episode->setFocus();
                return false;
            }
        }
    }
    
    // Validate episode name if provided
    QString epName = ui->lineEdit_EPName->text().trimmed();
    if (!epName.isEmpty()) {
        // Use TVShowName validation type since episode names need the same special characters
        InputValidation::ValidationResult epNameResult = InputValidation::validateInput(
            epName, InputValidation::InputType::TVShowName, VP_ShowsMetadata::MAX_EP_NAME_LENGTH);
        
        if (!epNameResult.isValid) {
            QMessageBox::warning(this, tr("Validation Error"), 
                               tr("Invalid episode name: %1").arg(epNameResult.errorMessage));
            ui->lineEdit_EPName->setFocus();
            return false;
        }
    }
    
    // Validate episode description if provided
    QString epDescription = ui->textEdit_EPDescription->toPlainText().trimmed();
    if (!epDescription.isEmpty()) {
        // Use PlainText validation with appropriate length limit
        InputValidation::ValidationResult descResult = InputValidation::validateInput(
            epDescription, InputValidation::InputType::PlainText, VP_ShowsMetadata::MAX_EP_DESCRIPTION_LENGTH);
        
        if (!descResult.isValid) {
            QMessageBox::warning(this, tr("Validation Error"), 
                               tr("Invalid episode description: %1").arg(descResult.errorMessage));
            ui->textEdit_EPDescription->setFocus();
            return false;
        }
    }
    
    // Validate language
    QString language = ui->comboBox_Language->currentText();
    if (!language.isEmpty()) {
        InputValidation::ValidationResult langResult = InputValidation::validateInput(
            language, InputValidation::InputType::PlainText, 50);
        
        if (!langResult.isValid) {
            QMessageBox::warning(this, tr("Validation Error"), 
                               tr("Invalid language: %1").arg(langResult.errorMessage));
            ui->comboBox_Language->setFocus();
            return false;
        }
    }
    
    // Validate translation
    QString translation = ui->comboBox_Translation->currentText();
    if (!translation.isEmpty()) {
        InputValidation::ValidationResult transResult = InputValidation::validateInput(
            translation, InputValidation::InputType::PlainText, 50);
        
        if (!transResult.isValid) {
            QMessageBox::warning(this, tr("Validation Error"), 
                               tr("Invalid translation: %1").arg(transResult.errorMessage));
            ui->comboBox_Translation->setFocus();
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
    
    // Show name is always read-only (determined from folder structure), keep the original value
    m_metadata.showName = m_originalMetadata.showName;
    m_metadata.season = ui->lineEdit_Season->text().trimmed();
    m_metadata.episode = ui->lineEdit_Episode->text().trimmed();
    m_metadata.EPName = ui->lineEdit_EPName->text().trimmed();
    m_metadata.EPDescription = ui->textEdit_EPDescription->toPlainText().trimmed();
    
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
    // Show name comparison skipped since it's read-only
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
    if (m_metadata.EPDescription != m_originalMetadata.EPDescription) {
        qDebug() << "VP_ShowsEditMetadataDialog: Episode description changed";
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
    
    // In repair mode, always save (recreate metadata)
    // In normal mode, check for modifications first
    if (!m_repairMode) {
        checkForModifications();
    }
    
    // Store whether TMDB re-acquisition was requested
    m_shouldReacquireTMDB = ui->checkBox_ReacquireTMDB->isChecked();
    qDebug() << "VP_ShowsEditMetadataDialog: TMDB re-acquisition requested:" << m_shouldReacquireTMDB;
    
    if (m_wasModified || m_repairMode) {
        qDebug() << "VP_ShowsEditMetadataDialog: Saving metadata" << (m_repairMode ? "(repair mode)" : "(normal mode)");
        
        // Update metadata from UI
        updateMetadataFromUI();
        
        // Create metadata manager
        VP_ShowsMetadata metadataManager(m_encryptionKey, m_username);
        
        // Write metadata back to file
        if (!metadataManager.writeMetadataToFile(m_videoFilePath, m_metadata)) {
            QString errorMsg = m_repairMode ? 
                tr("Failed to recreate metadata header.") :
                tr("Failed to save metadata to file.");
            QMessageBox::critical(this, tr("Error"), errorMsg);
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
