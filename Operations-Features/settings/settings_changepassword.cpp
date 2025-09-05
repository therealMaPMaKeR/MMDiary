#include "settings_changepassword.h"
#include "qvalidator.h"
#include "ui_settings_changepassword.h"
#include "CryptoUtils.h"
#include "inputvalidation.h"
#include "sqlite-database-auth.h"
#include "constants.h"
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include <algorithm>  // For std::fill

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
    // SECURITY: Parent is set immediately, ensuring proper cleanup
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, this);
    Q_ASSERT(validator->parent() == this); // Verify parent is set
    ui->lineEdit_CurPW->setValidator(validator);
    ui->lineEdit_NewPW->setValidator(validator);
    ui->lineEdit_ConfirmPW->setValidator(validator);

    // Set focus to current password field
    ui->lineEdit_CurPW->setFocus();
}

ChangePassword::~ChangePassword()
{
    // SECURITY: Secure cleanup of sensitive data before destruction
    secureCleanup();
    
    // SECURITY: Null check before deletion
    if (ui) {
        delete ui;
        ui = nullptr;
    }
}

void ChangePassword::initialize(const QString& username, const QByteArray& encryptionKey)
{
    m_username = username;
    m_encryptionKey = encryptionKey;
}

void ChangePassword::on_pushButton_Cancel_clicked()
{
    // SECURITY: Clean up sensitive data before closing
    secureCleanup();
    reject(); // Close the dialog
}

void ChangePassword::on_pushButton_ChangePW_clicked()
{
    // Clear any previous error messages
    ui->label_ErrorDisplay->setText("");

    // Validate input and change password
    if (validateUserInput()) {
        if (verifyCurrentPassword()) {
            // Show backup deletion dialog
            BackupDeletionMode backupMode = showBackupDeletionDialog();
            
            // User cancelled the dialog
            if (backupMode == BackupDeletionMode::None) {
                return;
            }
            
            if (changePassword(backupMode)) {
                QMessageBox::information(this, "Success", "Password changed successfully.");
                // SECURITY: Clean up sensitive data before closing
                secureCleanup();
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
    DatabaseAuthManager& db = DatabaseAuthManager::instance();

    // Connect to the database
    if (!db.isConnected() && !db.connect()) {
        qCritical() << "ChangePassword: Failed to connect to database:" << db.lastError();
        return false;
    }

    // Get the stored password hash
    QString storedHash = db.GetUserData_String(m_username, Constants::UserT_Index_Password);
    if (storedHash == Constants::ErrorMessage_Default || storedHash == Constants::ErrorMessage_INVUSER) {
        qCritical() << "ChangePassword: Failed to retrieve password hash from database";
        return false;
    }

    // Verify the current password
    return CryptoUtils::Hashing_CompareHash(storedHash, ui->lineEdit_CurPW->text());
}

ChangePassword::BackupDeletionMode ChangePassword::showBackupDeletionDialog()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("Backup Deletion Policy");
    msgBox.setText("When would you like to delete old backups?");
    msgBox.setInformativeText("Old backups can still be accessed with your old password. \n"
                             "For security, it's recommended to delete them after changing your password.");
    
    // Create custom buttons
    QPushButton *immediateButton = msgBox.addButton("Delete on next login", QMessageBox::ActionRole);
    QPushButton *delayedButton = msgBox.addButton("Delete in 7 days (Recommended)", QMessageBox::ActionRole);
    QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);
    
    // Set delayed as default button
    msgBox.setDefaultButton(delayedButton);
    
    msgBox.exec();
    
    if (msgBox.clickedButton() == immediateButton) {
        qDebug() << "ChangePassword: User selected immediate backup deletion";
        return BackupDeletionMode::Immediate;
    } else if (msgBox.clickedButton() == delayedButton) {
        qDebug() << "ChangePassword: User selected delayed backup deletion (7 days)";
        return BackupDeletionMode::Delayed;
    } else {
        qDebug() << "ChangePassword: User cancelled backup deletion dialog";
        return BackupDeletionMode::None;
    }
}

bool ChangePassword::changePassword(BackupDeletionMode backupMode)
{
    DatabaseAuthManager& db = DatabaseAuthManager::instance();

    // Connect to the database
    if (!db.isConnected() && !db.connect()) {
        qCritical() << "ChangePassword: Failed to connect to database:" << db.lastError();
        return false;
    }

    // Start a transaction
    if (!db.beginTransaction()) {
        qCritical() << "ChangePassword: Failed to begin database transaction:" << db.lastError();
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
            qCritical() << "ChangePassword: Failed to update password hash:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // Update salt
        if (!db.UpdateUserData_BLOB(m_username, Constants::UserT_Index_Salt, newSalt)) {
            qCritical() << "ChangePassword: Failed to update salt:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // Update encrypted key
        if (!db.UpdateUserData_BLOB(m_username, Constants::UserT_Index_EncryptionKey, reEncryptedKey)) {
            qCritical() << "ChangePassword: Failed to update encryption key:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // 6. Store backup deletion mode and date
        QString modeStr = QString::number(static_cast<int>(backupMode));
        if (!db.UpdateUserData_TEXT(m_username, Constants::UserT_Index_BackupDeletionMode, modeStr)) {
            qCritical() << "ChangePassword: Failed to update backup deletion mode:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        // Calculate deletion date based on mode
        QString deletionDate;
        if (backupMode == BackupDeletionMode::Immediate) {
            // Set to current date for immediate deletion on next login
            deletionDate = QDateTime::currentDateTime().toString(Qt::ISODate);
        } else if (backupMode == BackupDeletionMode::Delayed) {
            // Set to 7 days from now
            deletionDate = QDateTime::currentDateTime().addDays(7).toString(Qt::ISODate);
        }

        if (!deletionDate.isEmpty()) {
            if (!db.UpdateUserData_TEXT(m_username, Constants::UserT_Index_BackupDeletionDate, deletionDate)) {
                qCritical() << "ChangePassword: Failed to update backup deletion date:" << db.lastError();
                db.rollbackTransaction();
                return false;
            }
        }

        // Commit the transaction
        if (!db.commitTransaction()) {
            qCritical() << "ChangePassword: Failed to commit transaction:" << db.lastError();
            db.rollbackTransaction();
            return false;
        }

        qDebug() << "ChangePassword: Password changed successfully with backup mode:" << modeStr;
        return true;
    } catch (const std::exception& e) {
        qCritical() << "ChangePassword: Exception during password change:" << e.what();
        db.rollbackTransaction();
        return false;
    }
}

void ChangePassword::secureCleanup()
{
    // SECURITY: Securely clear sensitive data from memory
    if (!m_encryptionKey.isEmpty()) {
        // Overwrite the memory with zeros before clearing
        std::fill(m_encryptionKey.begin(), m_encryptionKey.end(), 0);
        m_encryptionKey.clear();
    }
    
    // Clear password fields
    if (ui) {
        if (ui->lineEdit_CurPW) {
            ui->lineEdit_CurPW->setText(QString());
        }
        if (ui->lineEdit_NewPW) {
            ui->lineEdit_NewPW->setText(QString());
        }
        if (ui->lineEdit_ConfirmPW) {
            ui->lineEdit_ConfirmPW->setText(QString());
        }
    }
}
