#include "changepassword.h"
#include "qvalidator.h"
#include "ui_changepassword.h"
#include "Operations-Global/CryptoUtils.h"
#include "Operations-Global/inputvalidation.h"
#include "Operations-Global/sqlite-database-handler.h"
#include "constants.h"
#include <QMessageBox>

ChangePassword::ChangePassword(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChangePassword)
{
    ui->setupUi(this);

    // Configure UI elements
    setWindowTitle("Change Password");
    ui->label_ErrorDisplay->setText(""); // Clear error message

    // Set password fields to password mode
    ui->lineEdit_CurPW->setEchoMode(QLineEdit::Password);
    ui->lineEdit_NewPW->setEchoMode(QLineEdit::Password);
    ui->lineEdit_ConfirmPW->setEchoMode(QLineEdit::Password);

    // Prevent whitespaces in password fields
    QRegularExpression noWhitespace("[^\\s]*"); // Matches any non-whitespace characters
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, this);
    ui->lineEdit_CurPW->setValidator(validator);
    ui->lineEdit_NewPW->setValidator(validator);
    ui->lineEdit_ConfirmPW->setValidator(validator);

    // Set focus to current password field
    ui->lineEdit_CurPW->setFocus();
}

ChangePassword::~ChangePassword()
{
    delete ui;
}

void ChangePassword::initialize(const QString& username, const QByteArray& encryptionKey)
{
    m_username = username;
    m_encryptionKey = encryptionKey;
}

void ChangePassword::on_pushButton_Cancel_clicked()
{
    reject(); // Close the dialog
}

void ChangePassword::on_pushButton_ChangePW_clicked()
{
    // Clear any previous error messages
    ui->label_ErrorDisplay->setText("");

    // Validate input and change password
    if (validateUserInput()) {
        if (verifyCurrentPassword()) {
            if (changePassword()) {
                QMessageBox::information(this, "Success", "Password changed successfully.");
                accept(); // Close dialog with success status
            } else {
                ui->label_ErrorDisplay->setText("Failed to update password in database.");
            }
        } else {
            ui->label_ErrorDisplay->setText("Current password is incorrect.");
        }
    }
}

bool ChangePassword::validateUserInput()
{
    // Check that all fields are filled
    if (ui->lineEdit_CurPW->text().isEmpty() ||
        ui->lineEdit_NewPW->text().isEmpty() ||
        ui->lineEdit_ConfirmPW->text().isEmpty()) {
        ui->label_ErrorDisplay->setText("All fields are required.");
        return false;
    }

    // Check that new password meets requirements
    InputValidation::ValidationResult newPwResult =
        InputValidation::validateInput(ui->lineEdit_NewPW->text(), InputValidation::InputType::Password);

    if (!newPwResult.isValid) {
        ui->label_ErrorDisplay->setText(newPwResult.errorMessage);
        return false;
    }

    // Check that new password and confirm password match
    if (ui->lineEdit_NewPW->text() != ui->lineEdit_ConfirmPW->text()) {
        ui->label_ErrorDisplay->setText("New password and confirmation do not match.");
        return false;
    }

    // Check that new password is different from current password
    if (ui->lineEdit_CurPW->text() == ui->lineEdit_NewPW->text()) {
        ui->label_ErrorDisplay->setText("New password must be different from current password.");
        return false;
    }

    return true;
}

bool ChangePassword::verifyCurrentPassword()
{
    DatabaseManager& db = DatabaseManager::instance();

    // Connect to the database
    if (!db.isConnected() && !db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        return false;
    }

    // Get the stored password hash
    QString storedHash = db.GetUserData_String(m_username, Constants::UserT_Index_Password);
    if (storedHash == Constants::ErrorMessage_Default || storedHash == Constants::ErrorMessage_INVUSER) {
        qCritical() << "Failed to retrieve password hash from database";
        return false;
    }

    // Verify the current password
    return CryptoUtils::Hashing_CompareHash(storedHash, ui->lineEdit_CurPW->text());
}

bool ChangePassword::changePassword()
{
    DatabaseManager& db = DatabaseManager::instance();

    // Connect to the database
    if (!db.isConnected() && !db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        return false;
    }

    // Start a transaction
    if (!db.beginTransaction()) {
        qCritical() << "Failed to begin database transaction:" << db.lastError();
        return false;
    }

    try {

        // 1. Generate new password hash
        QString newHashedPassword = CryptoUtils::Hashing_HashPassword(ui->lineEdit_NewPW->text());

        // 2. Get a new derived key and store the salt.
        QByteArray newSalt;
        QByteArray derivedKeyWithSalt = CryptoUtils::Encryption_DeriveKey(ui->lineEdit_NewPW->text(), &newSalt);

        // 3. Extract just the derivedKey part (remove the salt)
        QByteArray newDerivedKey = derivedKeyWithSalt.mid(newSalt.size());

        // 4. Re-encrypt the original encryption key with the new derived key
        QByteArray reEncryptedKey = CryptoUtils::Encryption_EncryptBArray(newDerivedKey, m_encryptionKey, m_username);

        // 5. Update the database with new values

        // Update password hash
        if (!db.UpdateUserData_TEXT(m_username, Constants::UserT_Index_Password, newHashedPassword)) {
            qCritical() << "Failed to update password hash:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // Update salt
        if (!db.UpdateUserData_BLOB(m_username, Constants::UserT_Index_Salt, newSalt)) {
            qCritical() << "Failed to update salt:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // Update encrypted key
        if (!db.UpdateUserData_BLOB(m_username, Constants::UserT_Index_EncryptionKey, reEncryptedKey)) {
            qCritical() << "Failed to update encryption key:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // Commit the transaction
        if (!db.commitTransaction()) {
            qCritical() << "Failed to commit transaction:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        qCritical() << "Exception during password change:" << e.what();
        db.rollbackTransaction();
        return false;
    }
}
