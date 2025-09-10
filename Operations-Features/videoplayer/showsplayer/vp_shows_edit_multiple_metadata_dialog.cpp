#include "vp_shows_edit_multiple_metadata_dialog.h"
#include "ui_vp_shows_edit_multiple_metadata_dialog.h"
#include "vp_shows_metadata.h"
#include "vp_shows_settings.h"
#include "vp_shows_tmdb.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include <QDebug>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>

VP_ShowsEditMultipleMetadataDialog::VP_ShowsEditMultipleMetadataDialog(const QStringList& videoFilePaths,
                                                                       const QByteArray& encryptionKey,
                                                                       const QString& username,
                                                                       QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsEditMultipleMetadataDialog)
    , m_videoFilePaths(videoFilePaths)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_canEditSeason(false)
    , m_hasCommonLanguage(false)
    , m_hasCommonTranslation(false)
    , m_hasCommonContentType(false)
    , m_modifiedFileCount(0)
    , m_shouldReacquireTMDB(false)
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Created dialog for" << videoFilePaths.size() << "files";
    
    // Set window title with file count
    setWindowTitle(tr("Edit Metadata for %1 Files").arg(videoFilePaths.size()));
    
    // Load metadata from all files
    if (!loadAllMetadata()) {
        QMessageBox::critical(this, tr("Error"), 
                            tr("Failed to load metadata from one or more files."));
        reject();
        return;
    }
    
    // Analyze files to determine what can be edited
    analyzeSelectedFiles();
    
    // Populate UI with common values
    populateUI();
    
    // Connect signals for checkbox state changes
    connect(ui->checkBox_Language, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::onLanguageCheckChanged);
    connect(ui->checkBox_Translation, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::onTranslationCheckChanged);
    connect(ui->checkBox_ContentType, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::onContentTypeCheckChanged);
    connect(ui->checkBox_Season, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::onSeasonCheckChanged);
    
    // Connect signals for preview updates
    connect(ui->comboBox_Language, &QComboBox::currentTextChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->comboBox_Translation, &QComboBox::currentTextChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->comboBox_ContentType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->lineEdit_Season, &QLineEdit::textChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    
    // Connect clear checkboxes for preview updates
    connect(ui->checkBox_ClearEpisodeNames, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->checkBox_ClearEpisodeNumbers, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->checkBox_ClearEpisodeImages, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->checkBox_ClearEpisodeDescriptions, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->checkBox_ClearEpisodeAirDates, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    connect(ui->checkBox_ResetDisplayStatus, &QCheckBox::stateChanged,
            this, &VP_ShowsEditMultipleMetadataDialog::updatePreview);
    
    // Initial preview update
    updatePreview();
    
    // Initialize the Re-acquire TMDB checkbox based on show settings
    // Get the show folder path from the first video file path (all should be in same show)
    if (!m_videoFilePaths.isEmpty()) {
        QFileInfo fileInfo(m_videoFilePaths.first());
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
                qDebug() << "VP_ShowsEditMultipleMetadataDialog: Valid show ID found:" << showSettings.showId;
                qDebug() << "VP_ShowsEditMultipleMetadataDialog: Set Re-acquire TMDB checkbox to:" << showSettings.useTMDB;
            } else {
                // Disable the checkbox if show ID is invalid
                ui->checkBox_ReacquireTMDB->setChecked(false);
                ui->checkBox_ReacquireTMDB->setEnabled(false);
                ui->checkBox_ReacquireTMDB->setToolTip(tr("TMDB re-acquisition is not available for this show (no valid show ID)"));
                qDebug() << "VP_ShowsEditMultipleMetadataDialog: Invalid or missing show ID, disabling Re-acquire TMDB checkbox";
            }
        } else {
            // Default to disabled if settings can't be loaded
            ui->checkBox_ReacquireTMDB->setChecked(false);
            ui->checkBox_ReacquireTMDB->setEnabled(false);
            ui->checkBox_ReacquireTMDB->setToolTip(tr("TMDB re-acquisition is not available (cannot load show settings)"));
            qDebug() << "VP_ShowsEditMultipleMetadataDialog: Could not load show settings, disabling Re-acquire TMDB";
        }
    }
}

VP_ShowsEditMultipleMetadataDialog::~VP_ShowsEditMultipleMetadataDialog()
{
    delete ui;
}

bool VP_ShowsEditMultipleMetadataDialog::loadAllMetadata()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Loading metadata from all files";
    
    m_allMetadata.clear();
    VP_ShowsMetadata metadataManager(m_encryptionKey, m_username);
    
    for (const QString& filePath : m_videoFilePaths) {
        // Validate file path
        if (!OperationsFiles::isWithinAllowedDirectory(filePath, "Data")) {
            qDebug() << "VP_ShowsEditMultipleMetadataDialog: File path outside allowed directory:" << filePath;
            return false;
        }
        
        VP_ShowsMetadata::ShowMetadata metadata;
        if (!metadataManager.readMetadataFromFile(filePath, metadata)) {
            qDebug() << "VP_ShowsEditMultipleMetadataDialog: Failed to read metadata from:" << filePath;
            
            // Ask user if they want to skip this file and continue
            int result = QMessageBox::question(this, tr("Error Reading File"),
                                              tr("Failed to read metadata from:\n%1\n\nSkip this file and continue?")
                                              .arg(QFileInfo(filePath).fileName()),
                                              QMessageBox::Yes | QMessageBox::No);
            
            if (result == QMessageBox::No) {
                return false;
            }
            
            // Skip this file but continue with others
            continue;
        }
        
        m_allMetadata.append(metadata);
    }
    
    if (m_allMetadata.isEmpty()) {
        qDebug() << "VP_ShowsEditMultipleMetadataDialog: No metadata could be loaded";
        return false;
    }
    
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Loaded metadata from" << m_allMetadata.size() << "files";
    return true;
}

void VP_ShowsEditMultipleMetadataDialog::analyzeSelectedFiles()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Analyzing selected files";
    
    if (m_allMetadata.isEmpty()) {
        return;
    }
    
    // Check for common language
    m_commonLanguage = m_allMetadata.first().language;
    m_hasCommonLanguage = true;
    for (const auto& metadata : m_allMetadata) {
        if (metadata.language != m_commonLanguage) {
            m_hasCommonLanguage = false;
            m_commonLanguage.clear();
            break;
        }
    }
    
    // Check for common translation
    m_commonTranslation = m_allMetadata.first().translation;
    m_hasCommonTranslation = true;
    for (const auto& metadata : m_allMetadata) {
        if (metadata.translation != m_commonTranslation) {
            m_hasCommonTranslation = false;
            m_commonTranslation.clear();
            break;
        }
    }
    
    // Check for common content type
    m_commonContentType = m_allMetadata.first().contentType;
    m_hasCommonContentType = true;
    for (const auto& metadata : m_allMetadata) {
        if (metadata.contentType != m_commonContentType) {
            m_hasCommonContentType = false;
            break;
        }
    }
    
    // Check for common season (allows editing season if all are the same)
    m_commonSeason = m_allMetadata.first().season;
    m_canEditSeason = true;
    for (const auto& metadata : m_allMetadata) {
        if (metadata.season != m_commonSeason) {
            m_canEditSeason = false;
            m_commonSeason.clear();
            break;
        }
    }
    
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Analysis complete:";
    qDebug() << "  Common Language:" << m_hasCommonLanguage << "(" << m_commonLanguage << ")";
    qDebug() << "  Common Translation:" << m_hasCommonTranslation << "(" << m_commonTranslation << ")";
    qDebug() << "  Common Content Type:" << m_hasCommonContentType;
    qDebug() << "  Can Edit Season:" << m_canEditSeason << "(" << m_commonSeason << ")";
}

void VP_ShowsEditMultipleMetadataDialog::populateUI()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Populating UI";
    
    // Define styling constants
    const QString disabledLabelStyle = "QLabel { color: #888888; }";
    const QString disabledWidgetStyle = "QComboBox, QLineEdit { background-color: #f0f0f0; color: #888888; }";
    
    // Set file count label
    ui->label_FileCount->setText(tr("Editing %1 files").arg(m_allMetadata.size()));
    
    // Language field
    if (m_hasCommonLanguage) {
        ui->comboBox_Language->setCurrentText(m_commonLanguage);
        ui->label_LanguageStatus->setText(tr("(Current: %1)").arg(m_commonLanguage));
    } else {
        ui->label_LanguageStatus->setText(tr("(Mixed values)"));
    }
    ui->comboBox_Language->setEnabled(false);  // Initially disabled until checkbox is checked
    ui->comboBox_Language->setStyleSheet(disabledWidgetStyle);  // Apply disabled styling
    
    // Translation field
    if (m_hasCommonTranslation) {
        ui->comboBox_Translation->setCurrentText(m_commonTranslation);
        ui->label_TranslationStatus->setText(tr("(Current: %1)").arg(m_commonTranslation));
    } else {
        ui->label_TranslationStatus->setText(tr("(Mixed values)"));
    }
    ui->comboBox_Translation->setEnabled(false);
    ui->comboBox_Translation->setStyleSheet(disabledWidgetStyle);  // Apply disabled styling
    
    // Content Type field
    if (m_hasCommonContentType) {
        ui->comboBox_ContentType->setCurrentIndex(static_cast<int>(m_commonContentType));
        QString typeName;
        switch (m_commonContentType) {
            case VP_ShowsMetadata::Regular: typeName = tr("Regular Episode"); break;
            case VP_ShowsMetadata::Movie: typeName = tr("Movie"); break;
            case VP_ShowsMetadata::OVA: typeName = tr("OVA/OAD"); break;
            case VP_ShowsMetadata::Extra: typeName = tr("Extra/Special"); break;
        }
        ui->label_ContentTypeStatus->setText(tr("(Current: %1)").arg(typeName));
    } else {
        ui->label_ContentTypeStatus->setText(tr("(Mixed values)"));
    }
    ui->comboBox_ContentType->setEnabled(false);
    ui->comboBox_ContentType->setStyleSheet(disabledWidgetStyle);  // Apply disabled styling
    
    // Season field
    if (m_canEditSeason) {
        ui->lineEdit_Season->setText(m_commonSeason);
        ui->label_SeasonStatus->setText(tr("(Current: %1)").arg(
            m_commonSeason.isEmpty() ? tr("Absolute numbering") : m_commonSeason));
        ui->checkBox_Season->setEnabled(true);
        ui->lineEdit_Season->setEnabled(false);
        ui->lineEdit_Season->setStyleSheet(disabledWidgetStyle);  // Apply disabled styling
    } else {
        ui->label_SeasonStatus->setText(tr("(Different seasons - cannot edit)"));
        ui->label_SeasonStatus->setStyleSheet(disabledLabelStyle);  // Apply disabled label styling
        ui->checkBox_Season->setEnabled(false);
        ui->lineEdit_Season->setEnabled(false);
        ui->lineEdit_Season->setPlaceholderText(tr("Multiple seasons selected"));
        ui->lineEdit_Season->setStyleSheet(disabledWidgetStyle);  // Apply disabled styling
    }
    
    // Clear options are always available
    ui->checkBox_ClearEpisodeNames->setEnabled(true);
    ui->checkBox_ClearEpisodeNumbers->setEnabled(true);
    ui->checkBox_ClearEpisodeImages->setEnabled(true);
    ui->checkBox_ClearEpisodeDescriptions->setEnabled(true);
    ui->checkBox_ClearEpisodeAirDates->setEnabled(true);
    ui->checkBox_ResetDisplayStatus->setEnabled(true);
}

void VP_ShowsEditMultipleMetadataDialog::onLanguageCheckChanged(int state)
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Language checkbox changed to:" << (state == Qt::Checked);
    
    // Define styling constants
    const QString enabledStyle = "";
    const QString disabledWidgetStyle = "QComboBox, QLineEdit { background-color: #f0f0f0; color: #888888; }";
    
    bool isEnabled = (state == Qt::Checked);
    ui->comboBox_Language->setEnabled(isEnabled);
    
    // Apply styling based on enabled state
    ui->comboBox_Language->setStyleSheet(isEnabled ? enabledStyle : disabledWidgetStyle);
    
    if (!isEnabled) {
        // Reset to original value if unchecked
        if (m_hasCommonLanguage) {
            ui->comboBox_Language->setCurrentText(m_commonLanguage);
        }
    }
    updatePreview();
}

void VP_ShowsEditMultipleMetadataDialog::onTranslationCheckChanged(int state)
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Translation checkbox changed to:" << (state == Qt::Checked);
    
    // Define styling constants
    const QString enabledStyle = "";
    const QString disabledWidgetStyle = "QComboBox, QLineEdit { background-color: #f0f0f0; color: #888888; }";
    
    bool isEnabled = (state == Qt::Checked);
    ui->comboBox_Translation->setEnabled(isEnabled);
    
    // Apply styling based on enabled state
    ui->comboBox_Translation->setStyleSheet(isEnabled ? enabledStyle : disabledWidgetStyle);
    
    if (!isEnabled) {
        if (m_hasCommonTranslation) {
            ui->comboBox_Translation->setCurrentText(m_commonTranslation);
        }
    }
    updatePreview();
}

void VP_ShowsEditMultipleMetadataDialog::onContentTypeCheckChanged(int state)
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: ContentType checkbox changed to:" << (state == Qt::Checked);
    
    // Define styling constants
    const QString enabledStyle = "";
    const QString disabledWidgetStyle = "QComboBox, QLineEdit { background-color: #f0f0f0; color: #888888; }";
    
    bool isEnabled = (state == Qt::Checked);
    ui->comboBox_ContentType->setEnabled(isEnabled);
    
    // Apply styling based on enabled state
    ui->comboBox_ContentType->setStyleSheet(isEnabled ? enabledStyle : disabledWidgetStyle);
    
    if (!isEnabled) {
        if (m_hasCommonContentType) {
            ui->comboBox_ContentType->setCurrentIndex(static_cast<int>(m_commonContentType));
        }
    }
    updatePreview();
}

void VP_ShowsEditMultipleMetadataDialog::onSeasonCheckChanged(int state)
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Season checkbox changed to:" << (state == Qt::Checked);
    
    // Define styling constants
    const QString enabledStyle = "";
    const QString disabledWidgetStyle = "QComboBox, QLineEdit { background-color: #f0f0f0; color: #888888; }";
    
    bool isEnabled = (state == Qt::Checked && m_canEditSeason);
    ui->lineEdit_Season->setEnabled(isEnabled);
    
    // Apply styling based on enabled state
    ui->lineEdit_Season->setStyleSheet(isEnabled ? enabledStyle : disabledWidgetStyle);
    
    if (state != Qt::Checked) {
        if (m_canEditSeason) {
            ui->lineEdit_Season->setText(m_commonSeason);
        }
    }
    updatePreview();
}

void VP_ShowsEditMultipleMetadataDialog::updatePreview()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Updating preview";
    
    // Update the changes structure
    updateChangesFromUI();
    
    // Build preview text
    QStringList changesList;
    
    if (m_changes.changeLanguage) {
        changesList << tr("• Change Language to: %1").arg(m_changes.language);
    }
    
    if (m_changes.changeTranslation) {
        changesList << tr("• Change Translation to: %1").arg(m_changes.translation);
    }
    
    if (m_changes.changeContentType) {
        QString typeName;
        switch (m_changes.contentType) {
            case VP_ShowsMetadata::Regular: typeName = tr("Regular Episode"); break;
            case VP_ShowsMetadata::Movie: typeName = tr("Movie"); break;
            case VP_ShowsMetadata::OVA: typeName = tr("OVA/OAD"); break;
            case VP_ShowsMetadata::Extra: typeName = tr("Extra/Special"); break;
        }
        changesList << tr("• Change Content Type to: %1").arg(typeName);
    }
    
    if (m_changes.changeSeason) {
        changesList << tr("• Change Season to: %1").arg(
            m_changes.season.isEmpty() ? tr("Absolute numbering") : m_changes.season);
    }
    
    if (m_changes.clearEpisodeNames) {
        changesList << tr("• Clear Episode Names");
    }
    
    if (m_changes.clearEpisodeNumbers) {
        changesList << tr("• Clear Episode Numbers");
    }
    
    if (m_changes.clearEpisodeImages) {
        changesList << tr("• Clear Episode Images");
    }
    
    if (m_changes.clearEpisodeDescriptions) {
        changesList << tr("• Clear Episode Descriptions");
    }
    
    if (m_changes.clearEpisodeAirDates) {
        changesList << tr("• Clear Episode Air Dates");
    }
    
    if (m_changes.resetDisplayStatus) {
        changesList << tr("• Reset Display Status (dual display off)");
    }
    
    // Update preview text
    if (changesList.isEmpty()) {
        ui->textEdit_Preview->setPlainText(tr("No changes selected"));
        ui->textEdit_Preview->setStyleSheet("QTextEdit { color: gray; }");
    } else {
        ui->textEdit_Preview->setPlainText(tr("Changes to be applied:\n\n%1").arg(changesList.join("\n")));
        ui->textEdit_Preview->setStyleSheet("");  // Reset to default style
    }
}

void VP_ShowsEditMultipleMetadataDialog::updateChangesFromUI()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Updating changes from UI";
    
    // Reset changes
    m_changes = MetadataChanges();
    
    // Check which edit fields are enabled
    if (ui->checkBox_Language->isChecked()) {
        m_changes.changeLanguage = true;
        m_changes.language = ui->comboBox_Language->currentText();
    }
    
    if (ui->checkBox_Translation->isChecked()) {
        m_changes.changeTranslation = true;
        m_changes.translation = ui->comboBox_Translation->currentText();
    }
    
    if (ui->checkBox_ContentType->isChecked()) {
        m_changes.changeContentType = true;
        m_changes.contentType = static_cast<VP_ShowsMetadata::ContentType>(
            ui->comboBox_ContentType->currentIndex());
    }
    
    if (ui->checkBox_Season->isChecked() && m_canEditSeason) {
        m_changes.changeSeason = true;
        m_changes.season = ui->lineEdit_Season->text().trimmed();
    }
    
    // Check clear options
    m_changes.clearEpisodeNames = ui->checkBox_ClearEpisodeNames->isChecked();
    m_changes.clearEpisodeNumbers = ui->checkBox_ClearEpisodeNumbers->isChecked();
    m_changes.clearEpisodeImages = ui->checkBox_ClearEpisodeImages->isChecked();
    m_changes.clearEpisodeDescriptions = ui->checkBox_ClearEpisodeDescriptions->isChecked();
    m_changes.clearEpisodeAirDates = ui->checkBox_ClearEpisodeAirDates->isChecked();
    m_changes.resetDisplayStatus = ui->checkBox_ResetDisplayStatus->isChecked();
}

bool VP_ShowsEditMultipleMetadataDialog::validateInput()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Validating input";
    
    // Update changes from UI
    updateChangesFromUI();
    
    // Check if any changes are selected
    bool hasChanges = m_changes.changeLanguage || m_changes.changeTranslation ||
                     m_changes.changeContentType || m_changes.changeSeason ||
                     m_changes.clearEpisodeNames || m_changes.clearEpisodeNumbers ||
                     m_changes.clearEpisodeImages || m_changes.clearEpisodeDescriptions ||
                     m_changes.clearEpisodeAirDates || m_changes.resetDisplayStatus;
    
    if (!hasChanges) {
        QMessageBox::information(this, tr("No Changes"),
                               tr("No changes have been selected."));
        return false;
    }
    
    // Validate season if being changed
    if (m_changes.changeSeason && !m_changes.season.isEmpty()) {
        // Season should be a number or empty for absolute numbering
        bool ok;
        int seasonNum = m_changes.season.toInt(&ok);
        if (!ok || seasonNum < 0) {
            QMessageBox::warning(this, tr("Invalid Season"),
                                tr("Season must be a valid number (0 or greater) or empty for absolute numbering."));
            ui->lineEdit_Season->setFocus();
            return false;
        }
    }
    
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Validation successful";
    return true;
}

bool VP_ShowsEditMultipleMetadataDialog::applyChangesToFiles()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Applying changes to files";
    
    VP_ShowsMetadata metadataManager(m_encryptionKey, m_username);
    m_modifiedFileCount = 0;
    QStringList failedFiles;
    
    // Apply changes to each file
    for (int i = 0; i < m_videoFilePaths.size() && i < m_allMetadata.size(); ++i) {
        const QString& filePath = m_videoFilePaths[i];
        VP_ShowsMetadata::ShowMetadata metadata = m_allMetadata[i];
        
        qDebug() << "VP_ShowsEditMultipleMetadataDialog: Processing file:" << filePath;
        
        // Apply edit changes
        if (m_changes.changeLanguage) {
            metadata.language = m_changes.language;
        }
        
        if (m_changes.changeTranslation) {
            metadata.translation = m_changes.translation;
        }
        
        if (m_changes.changeContentType) {
            metadata.contentType = m_changes.contentType;
            
            // When changing to Regular Episode, ensure we have valid episode numbers
            if (m_changes.contentType == VP_ShowsMetadata::Regular) {
                // Check if episode number is empty or invalid
                bool hasValidEpisode = false;
                if (!metadata.episode.isEmpty()) {
                    bool ok;
                    int episodeNum = metadata.episode.toInt(&ok);
                    hasValidEpisode = ok && episodeNum > 0;
                }
                
                if (!hasValidEpisode) {
                    // Try to parse from filename
                    int seasonNum = 0, episodeNum = 0;
                    if (VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum)) {
                        if (episodeNum > 0) {
                            metadata.episode = QString::number(episodeNum);
                            // Only update season if:
                            // 1. User is NOT manually changing the season in this bulk edit
                            // 2. Current season is invalid
                            if (!m_changes.changeSeason && seasonNum > 0 && 
                                (metadata.season.isEmpty() || metadata.season.toInt() <= 0)) {
                                metadata.season = QString::number(seasonNum);
                                qDebug() << "VP_ShowsEditMultipleMetadataDialog: Also setting parsed season" << seasonNum;
                            }
                            qDebug() << "VP_ShowsEditMultipleMetadataDialog: Parsed episode" << episodeNum << "from filename for Regular Episode";
                        } else {
                            // Parsing gave invalid episode, mark as error
                            metadata.episode = "error";
                            if (!m_changes.changeSeason) {
                                metadata.season = "error";
                            }
                            qDebug() << "VP_ShowsEditMultipleMetadataDialog: Invalid episode parsed, marking as error";
                        }
                    } else {
                        // Could not parse from filename, mark as error so it appears in error category
                        metadata.episode = "error";
                        if (!m_changes.changeSeason) {
                            metadata.season = "error";
                        }
                        qDebug() << "VP_ShowsEditMultipleMetadataDialog: Could not parse episode for Regular Episode, marking as error";
                    }
                }
            }
        }
        
        if (m_changes.changeSeason) {
            metadata.season = m_changes.season;
        }
        
        // Apply clear operations
        if (m_changes.clearEpisodeNames) {
            metadata.EPName.clear();
        }
        
        if (m_changes.clearEpisodeNumbers) {
            metadata.episode.clear();
            
            // If we're changing to Regular Episode and clearing episode numbers,
            // try to parse from filename to avoid episodes disappearing from tree
            if (m_changes.changeContentType && m_changes.contentType == VP_ShowsMetadata::Regular) {
                int seasonNum = 0, episodeNum = 0;
                if (VP_ShowsTMDB::parseEpisodeFromFilename(metadata.filename, seasonNum, episodeNum)) {
                    if (episodeNum > 0) {
                        metadata.episode = QString::number(episodeNum);
                        // Only set parsed season if user is NOT manually changing season
                        if (!m_changes.changeSeason && seasonNum > 0 && 
                            (metadata.season.isEmpty() || metadata.season.toInt() <= 0)) {
                            metadata.season = QString::number(seasonNum);
                        }
                        qDebug() << "VP_ShowsEditMultipleMetadataDialog: Parsed episode" << episodeNum << "from filename after clear";
                    } else {
                        // Parsing failed, set to "error" to show in error category
                        metadata.episode = "error";
                        if (!m_changes.changeSeason) {
                            metadata.season = "error";
                        }
                        qDebug() << "VP_ShowsEditMultipleMetadataDialog: Could not parse episode from filename, marking as error";
                    }
                } else {
                    // Parsing failed, set to "error" to show in error category
                    metadata.episode = "error";
                    if (!m_changes.changeSeason) {
                        metadata.season = "error";
                    }
                    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Failed to parse episode from filename, marking as error";
                }
            }
        }
        
        if (m_changes.clearEpisodeImages) {
            metadata.EPImage.clear();
        }
        
        if (m_changes.clearEpisodeDescriptions) {
            metadata.EPDescription.clear();
        }
        
        if (m_changes.clearEpisodeAirDates) {
            metadata.airDate.clear();
        }
        
        if (m_changes.resetDisplayStatus) {
            metadata.isDualDisplay = false;
        }
        
        // Write updated metadata back to file
        if (!metadataManager.writeMetadataToFile(filePath, metadata)) {
            qDebug() << "VP_ShowsEditMultipleMetadataDialog: Failed to write metadata to:" << filePath;
            failedFiles << QFileInfo(filePath).fileName();
        } else {
            m_modifiedFileCount++;
        }
    }
    
    // Report any failures
    if (!failedFiles.isEmpty()) {
        QMessageBox::warning(this, tr("Some Files Failed"),
                           tr("Failed to update metadata for the following files:\n\n%1\n\n"
                              "%2 of %3 files were successfully updated.")
                           .arg(failedFiles.join("\n"))
                           .arg(m_modifiedFileCount)
                           .arg(m_videoFilePaths.size()));
        
        // Return true if at least some files were updated
        return m_modifiedFileCount > 0;
    }
    
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Successfully updated" << m_modifiedFileCount << "files";
    return true;
}

void VP_ShowsEditMultipleMetadataDialog::accept()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Accept clicked";
    
    // Validate input
    if (!validateInput()) {
        return;
    }
    
    // Confirm with user
    int result = QMessageBox::question(this, tr("Confirm Changes"),
                                      tr("Apply changes to %1 files?\n\nThis action cannot be undone.")
                                      .arg(m_allMetadata.size()),
                                      QMessageBox::Yes | QMessageBox::No);
    
    if (result != QMessageBox::Yes) {
        return;
    }
    
    // Store whether TMDB re-acquisition was requested
    m_shouldReacquireTMDB = ui->checkBox_ReacquireTMDB->isChecked();
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: TMDB re-acquisition requested:" << m_shouldReacquireTMDB;
    
    // If TMDB re-acquisition is NOT requested, apply changes and save now
    // If TMDB IS requested, we'll save after TMDB updates in the parent function
    if (!m_shouldReacquireTMDB) {
        // Apply changes
        if (!applyChangesToFiles()) {
            // If all files failed, don't accept the dialog
            if (m_modifiedFileCount == 0) {
                return;
            }
        }
        
        // Success message removed - lack of error dialog is sufficient
        // QMessageBox::information(this, tr("Success"),
        //                        tr("Successfully updated metadata for %1 files.")
        //                        .arg(m_modifiedFileCount));
    } else {
        // Update changes structure from UI so parent can use them
        updateChangesFromUI();
        qDebug() << "VP_ShowsEditMultipleMetadataDialog: Deferring file save until after TMDB processing";
    }
    
    // Call base class accept
    QDialog::accept();
}

bool VP_ShowsEditMultipleMetadataDialog::applyChangesAndSave()
{
    qDebug() << "VP_ShowsEditMultipleMetadataDialog: Applying changes and saving after TMDB processing";
    
    // Update changes structure from UI (in case it wasn't done yet)
    updateChangesFromUI();
    
    // Apply changes and save
    if (!applyChangesToFiles()) {
        if (m_modifiedFileCount == 0) {
            return false;
        }
    }
    
    // Success message removed - lack of error dialog is sufficient
    // QMessageBox::information(this, tr("Success"),
    //                        tr("Successfully updated metadata for %1 files.")
    //                        .arg(m_modifiedFileCount));
    
    return true;
}
