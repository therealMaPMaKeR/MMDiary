#include "vp_shows_add_dialog.h"
#include "ui_vp_shows_add_dialog.h"
#include "inputvalidation.h"
#include <QMessageBox>
#include <QDebug>
#include <QRegularExpression>

VP_ShowsAddDialog::VP_ShowsAddDialog(const QString& folderName, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsAddDialog)
    , m_folderName(folderName)
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsAddDialog: Initializing dialog with folder name:" << folderName;
    
    // Set window title
    setWindowTitle(tr("Add TV Show"));
    
    // Pre-fill the show name with the folder name
    ui->lineEdit_ShowName->setText(folderName);
    
    // Pre-fill the language with English
    ui->lineEdit_Language->setText("English");
    
    // Set up the translation mode combo box
    ui->comboBox_TranslationMode->clear();
    ui->comboBox_TranslationMode->addItem("Dubbed");
    ui->comboBox_TranslationMode->addItem("Subbed");
    ui->comboBox_TranslationMode->setCurrentIndex(0); // Default to Dubbed
    
    // Connect signals
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &VP_ShowsAddDialog::on_buttonBox_accepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &VP_ShowsAddDialog::on_buttonBox_rejected);
    
    qDebug() << "VP_ShowsAddDialog: Dialog initialized successfully";
}

VP_ShowsAddDialog::~VP_ShowsAddDialog()
{
    delete ui;
}

QString VP_ShowsAddDialog::getShowName() const
{
    return ui->lineEdit_ShowName->text().trimmed();
}

QString VP_ShowsAddDialog::getLanguage() const
{
    return ui->lineEdit_Language->text().trimmed();
}

QString VP_ShowsAddDialog::getTranslationMode() const
{
    return ui->comboBox_TranslationMode->currentText();
}

bool VP_ShowsAddDialog::validateInputs()
{
    qDebug() << "VP_ShowsAddDialog: Validating inputs";
    
    // Validate show name
    QString showName = getShowName();
    if (!validateShowName(showName)) {
        return false;
    }
    
    // Validate language
    QString language = getLanguage();
    if (!validateLanguage(language)) {
        return false;
    }
    
    // Translation mode is from combo box, so it's always valid
    QString translationMode = getTranslationMode();
    if (translationMode.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Please select a translation mode."));
        return false;
    }
    
    qDebug() << "VP_ShowsAddDialog: All inputs valid - Show:" << showName 
             << "Language:" << language << "Translation:" << translationMode;
    
    return true;
}

bool VP_ShowsAddDialog::validateShowName(const QString& showName)
{
    if (showName.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Show name cannot be empty."));
        ui->lineEdit_ShowName->setFocus();
        return false;
    }
    
    // Use InputValidation for proper validation
    InputValidation::ValidationResult result = InputValidation::validateInput(
        showName, InputValidation::InputType::DisplayName, 100);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Invalid show name: %1").arg(result.errorMessage));
        ui->lineEdit_ShowName->setFocus();
        ui->lineEdit_ShowName->selectAll();
        return false;
    }
    
    qDebug() << "VP_ShowsAddDialog: Show name validated:" << showName;
    return true;
}

bool VP_ShowsAddDialog::validateLanguage(const QString& language)
{
    if (language.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Language cannot be empty."));
        ui->lineEdit_Language->setFocus();
        return false;
    }
    
    // Use InputValidation for proper validation
    InputValidation::ValidationResult result = InputValidation::validateInput(
        language, InputValidation::InputType::PlainText, 50);
    
    if (!result.isValid) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Invalid language: %1").arg(result.errorMessage));
        ui->lineEdit_Language->setFocus();
        ui->lineEdit_Language->selectAll();
        return false;
    }
    
    // Additional check: Language should only contain letters, spaces, and hyphens
    QRegularExpression languageRegex("^[a-zA-Z\\s\\-]+$");
    if (!languageRegex.match(language).hasMatch()) {
        QMessageBox::warning(this, tr("Invalid Input"), 
                           tr("Language can only contain letters, spaces, and hyphens."));
        ui->lineEdit_Language->setFocus();
        ui->lineEdit_Language->selectAll();
        return false;
    }
    
    qDebug() << "VP_ShowsAddDialog: Language validated:" << language;
    return true;
}

void VP_ShowsAddDialog::on_buttonBox_accepted()
{
    qDebug() << "VP_ShowsAddDialog: OK button clicked";
    
    if (validateInputs()) {
        qDebug() << "VP_ShowsAddDialog: Inputs validated, accepting dialog";
        accept();
    } else {
        qDebug() << "VP_ShowsAddDialog: Input validation failed";
        // Don't accept the dialog if validation fails
    }
}

void VP_ShowsAddDialog::on_buttonBox_rejected()
{
    qDebug() << "VP_ShowsAddDialog: Cancel button clicked";
    reject();
}

void VP_ShowsAddDialog::setShowNameReadOnly(bool readOnly)
{
    qDebug() << "VP_ShowsAddDialog: Setting show name read-only:" << readOnly;
    ui->lineEdit_ShowName->setReadOnly(readOnly);
    
    // If read-only, also change the style to indicate it's disabled
    if (readOnly) {
        ui->lineEdit_ShowName->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #404040; }");
    } else {
        ui->lineEdit_ShowName->setStyleSheet("");  // Reset to default style
    }
}
