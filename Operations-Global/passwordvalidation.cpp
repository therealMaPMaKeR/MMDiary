#include "passwordvalidation.h"
#include "ui_passwordvalidation.h"
#include "CryptoUtils.h"
#include "sqlite-database-handler.h"
#include <QMessageBox>
#include "../constants.h"

PasswordValidation::PasswordValidation(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PasswordValidation)
{
    ui->setupUi(this);
    setWindowTitle("Password Validation");

    // Make password field use password mode (show dots instead of characters)
    ui->lineEdit_Password->setEchoMode(QLineEdit::Password);

    // Connect signals and slots
    connect(ui->pushButton_Proceed, &QPushButton::clicked, this, &PasswordValidation::onProceedClicked);
    connect(ui->pushButton_Cancel, &QPushButton::clicked, this, &PasswordValidation::onCancelClicked);

    // Set focus to password field
    ui->lineEdit_Password->setFocus();
}

PasswordValidation::~PasswordValidation()
{
    delete ui;
}

void PasswordValidation::setOperationName(const QString& operationName)
{
    ui->label_OperationName->setText(operationName);
}

QString PasswordValidation::getPassword() const
{
    return ui->lineEdit_Password->text();
}

void PasswordValidation::onProceedClicked()
{
    // If password field is empty, show warning
    if (ui->lineEdit_Password->text().isEmpty()) {
        QMessageBox::warning(this, "Missing Password", "Please enter your password.");
        return;
    }

    // Otherwise accept the dialog
    accept();
}

void PasswordValidation::onCancelClicked()
{
    // Reject the dialog
    reject();
}

bool PasswordValidation::validatePasswordForOperation(QWidget* parent, const QString& operationName, const QString& username)
{
    // Create and configure the dialog
    PasswordValidation dialog(parent);
    dialog.setOperationName(operationName);

    // Show the dialog and wait for user input
    if (dialog.exec() != QDialog::Accepted) {
        return false; // User cancelled
    }

    // Get the entered password
    QString enteredPassword = dialog.getPassword();

    // Get database manager instance
    DatabaseManager& dbManager = DatabaseManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        QMessageBox::critical(parent, "Error", "Database connection failed.");
        return false;
    }

    // Get stored password hash for the username
    QString storedHash = dbManager.GetUserData_String(username, Constants::UserT_Index_Password);

    // Validate the password
    bool isValid = CryptoUtils::Hashing_CompareHash(storedHash, enteredPassword);

    if (!isValid) {
        QMessageBox::critical(parent, "Invalid Password", "The password you entered is incorrect.");
    }

    return isValid;
}

bool PasswordValidation::validatePasswordWithCustomCancel(QWidget* parent, const QString& operationName,
                                                          const QString& username, const QString& cancelButtonText)
{
    // Create and configure the dialog
    PasswordValidation dialog(parent);
    dialog.setOperationName(operationName);

    // Set custom text for cancel button
    dialog.ui->pushButton_Cancel->setText(cancelButtonText);

    // Show the dialog and wait for user input
    if (dialog.exec() != QDialog::Accepted) {
        return false; // User cancelled
    }

    // Get the entered password
    QString enteredPassword = dialog.getPassword();

    // Get database manager instance
    DatabaseManager& dbManager = DatabaseManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        QMessageBox::critical(parent, "Error", "Database connection failed.");
        return false;
    }

    // Get stored password hash for the username
    QString storedHash = dbManager.GetUserData_String(username, Constants::UserT_Index_Password);

    // Validate the password
    bool isValid = CryptoUtils::Hashing_CompareHash(storedHash, enteredPassword);

    if (!isValid) {
        QMessageBox::critical(parent, "Invalid Password", "The password you entered is incorrect.");
    }

    return isValid;
}
