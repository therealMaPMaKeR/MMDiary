#include "passwordvalidation.h"
#include "ui_passwordvalidation.h"
#include "encryption/CryptoUtils.h"
#include "sqlite-database-auth.h"
#include "sqlite-database-settings.h"
#include <QMessageBox>
#include "../constants.h"
#include <cstring> // For secure memory operations

QMap<QString, QDateTime> PasswordValidation::s_lastValidationTimes;
const int MAX_GRACE_PERIOD_ENTRIES = 100; // Prevent unbounded growth

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
    qDebug() << "PasswordValidation: Destructor - Clearing sensitive data";
    
    // Securely clear password field before deletion
    if (ui && ui->lineEdit_Password) {
        QString clearText = ui->lineEdit_Password->text();
        if (!clearText.isEmpty()) {
            // Overwrite the QString's internal buffer
            QChar* data = clearText.data();
            int len = clearText.length();
            for (int i = 0; i < len; ++i) {
                data[i] = QChar('\0');
            }
            clearText.clear();
        }
        ui->lineEdit_Password->clear();
        ui->lineEdit_Password->setText(QString());
    }
    
    delete ui;
}

// ---------- Grace Period Implementation -----------//

// NEW: Public static method to get grace period setting from database
int PasswordValidation::getGracePeriodForUser(const QString& username)
{
    // Get database manager instance
    DatabaseSettingsManager& db = DatabaseSettingsManager::instance();

    // Try to connect (may already be connected)
    if (!db.isConnected()) {
        // We need the encryption key, but we don't have access to it here
        // For now, return 0 (no grace period) if database is not connected
        qWarning() << "Cannot get grace period: Settings database not connected";
        return 0;
    }

    QString gracePeriodStr = db.GetSettingsData_String(Constants::SettingsT_Index_ReqPWDelay);
    if (gracePeriodStr == Constants::ErrorMessage_Default) {
        qWarning() << "Failed to get grace period setting, using default";
        return 30; // Default value
    }

    bool ok;
    int gracePeriod = gracePeriodStr.toInt(&ok);
    if (!ok || gracePeriod < 0 || gracePeriod > 300) {
        qWarning() << "Invalid grace period value:" << gracePeriodStr;
        return 30; // Default value
    }

    return gracePeriod;
}

// NEW: Public static method to check if within grace period
bool PasswordValidation::isWithinGracePeriod(const QString& username, int gracePeriodSeconds)
{
    // If grace period is 0, always require password
    if (gracePeriodSeconds <= 0) {
        return false;
    }

    // Check if we have a recorded validation time for this user
    if (!s_lastValidationTimes.contains(username)) {
        return false;
    }

    QDateTime lastValidation = s_lastValidationTimes[username];
    QDateTime currentTime = QDateTime::currentDateTime();

    // Calculate seconds elapsed since last validation
    qint64 secondsElapsed = lastValidation.secsTo(currentTime);

    qDebug() << "Grace period check - Username:" << username
             << "Seconds elapsed:" << secondsElapsed
             << "Grace period:" << gracePeriodSeconds;

    // Return true if we're still within the grace period
    return (secondsElapsed < gracePeriodSeconds);
}

// NEW: Public static method to record successful validation
void PasswordValidation::recordSuccessfulValidation(const QString& username)
{
    // Clean up old entries if map is getting too large
    if (s_lastValidationTimes.size() >= MAX_GRACE_PERIOD_ENTRIES) {
        qDebug() << "PasswordValidation: Grace period map size limit reached, cleaning old entries";
        
        // Remove entries older than 1 hour
        QDateTime cutoffTime = QDateTime::currentDateTime().addSecs(-3600);
        QMutableMapIterator<QString, QDateTime> it(s_lastValidationTimes);
        while (it.hasNext()) {
            it.next();
            if (it.value() < cutoffTime) {
                it.remove();
            }
        }
        
        // If still too many, keep only the most recent half
        if (s_lastValidationTimes.size() >= MAX_GRACE_PERIOD_ENTRIES) {
            QList<QPair<QString, QDateTime>> entries;
            for (auto it = s_lastValidationTimes.begin(); it != s_lastValidationTimes.end(); ++it) {
                entries.append(qMakePair(it.key(), it.value()));
            }
            
            // Sort by time (newest first)
            std::sort(entries.begin(), entries.end(),
                     [](const QPair<QString, QDateTime>& a, const QPair<QString, QDateTime>& b) {
                         return a.second > b.second;
                     });
            
            // Keep only the newest half
            s_lastValidationTimes.clear();
            int keepCount = MAX_GRACE_PERIOD_ENTRIES / 2;
            for (int i = 0; i < keepCount && i < entries.size(); ++i) {
                s_lastValidationTimes[entries[i].first] = entries[i].second;
            }
        }
    }
    
    s_lastValidationTimes[username] = QDateTime::currentDateTime();
    qDebug() << "PasswordValidation: Recorded successful password validation for user:" << username;
}

void PasswordValidation::clearGracePeriod(const QString& username)
{
    if (username.isEmpty()) {
        // Clear all grace periods
        s_lastValidationTimes.clear();
        qDebug() << "Cleared all grace periods";
    } else {
        // Clear specific user's grace period
        s_lastValidationTimes.remove(username);
        qDebug() << "Cleared grace period for user:" << username;
    }
}

// ----  Core Implementation ---- //

void PasswordValidation::setOperationName(const QString& operationName)
{
    ui->label_OperationName->setText(operationName);
}

QString PasswordValidation::getPassword() const
{
    // Note: Caller is responsible for securely clearing the returned password
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
    // Get grace period setting
    int gracePeriodSeconds = getGracePeriodForUser(username);

    // Check if we're within grace period
    if (isWithinGracePeriod(username, gracePeriodSeconds)) {
        qDebug() << "Password validation skipped due to grace period for operation:" << operationName;
        return true;
    }

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
    DatabaseAuthManager& dbManager = DatabaseAuthManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        // Clear password before returning
        QChar* pwData = enteredPassword.data();
        int pwLen = enteredPassword.length();
        for (int i = 0; i < pwLen; ++i) {
            pwData[i] = QChar('\0');
        }
        enteredPassword.clear();
        
        QMessageBox::critical(parent, "Error", "Database connection failed.");
        return false;
    }

    // Get stored password hash for the username
    QString storedHash = dbManager.GetUserData_String(username, Constants::UserT_Index_Password);

    // Validate the password
    bool isValid = CryptoUtils::Hashing_CompareHash(storedHash, enteredPassword);
    
    // Securely clear the entered password from memory
    QChar* pwData = enteredPassword.data();
    int pwLen = enteredPassword.length();
    for (int i = 0; i < pwLen; ++i) {
        pwData[i] = QChar('\0');
    }
    enteredPassword.clear();

    if (!isValid) {
        QMessageBox::critical(parent, "Invalid Password", "The password you entered is incorrect.");
        return false;
    }

    // Record successful validation for grace period
    recordSuccessfulValidation(username);

    return true;
}

bool PasswordValidation::validatePasswordWithCustomCancel(QWidget* parent, const QString& operationName,
                                                          const QString& username, const QString& cancelButtonText)
{
    // Get grace period setting
    int gracePeriodSeconds = getGracePeriodForUser(username);

    // Check if we're within grace period
    if (isWithinGracePeriod(username, gracePeriodSeconds)) {
        qDebug() << "Password validation skipped due to grace period for operation:" << operationName;
        return true;
    }

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
    DatabaseAuthManager& dbManager = DatabaseAuthManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        // Clear password before returning
        QChar* pwData = enteredPassword.data();
        int pwLen = enteredPassword.length();
        for (int i = 0; i < pwLen; ++i) {
            pwData[i] = QChar('\0');
        }
        enteredPassword.clear();
        
        QMessageBox::critical(parent, "Error", "Database connection failed.");
        return false;
    }

    // Get stored password hash for the username
    QString storedHash = dbManager.GetUserData_String(username, Constants::UserT_Index_Password);

    // Validate the password
    bool isValid = CryptoUtils::Hashing_CompareHash(storedHash, enteredPassword);
    
    // Securely clear the entered password from memory
    QChar* pwData = enteredPassword.data();
    int pwLen = enteredPassword.length();
    for (int i = 0; i < pwLen; ++i) {
        pwData[i] = QChar('\0');
    }
    enteredPassword.clear();

    if (!isValid) {
        QMessageBox::critical(parent, "Invalid Password", "The password you entered is incorrect.");
        return false;
    }

    // Record successful validation for grace period
    recordSuccessfulValidation(username);

    return true;
}

// Overloaded Methods that accept Grace Period directly

bool PasswordValidation::validatePasswordForOperation(QWidget* parent, const QString& operationName,
                                                      const QString& username, int gracePeriodSeconds)
{
    // Check if we're within grace period
    if (isWithinGracePeriod(username, gracePeriodSeconds)) {
        qDebug() << "Password validation skipped due to grace period for operation:" << operationName;
        return true;
    }

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
    DatabaseAuthManager& dbManager = DatabaseAuthManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        // Clear password before returning
        QChar* pwData = enteredPassword.data();
        int pwLen = enteredPassword.length();
        for (int i = 0; i < pwLen; ++i) {
            pwData[i] = QChar('\0');
        }
        enteredPassword.clear();
        
        QMessageBox::critical(parent, "Error", "Database connection failed.");
        return false;
    }

    // Get stored password hash for the username
    QString storedHash = dbManager.GetUserData_String(username, Constants::UserT_Index_Password);

    // Validate the password
    bool isValid = CryptoUtils::Hashing_CompareHash(storedHash, enteredPassword);
    
    // Securely clear the entered password from memory
    QChar* pwData = enteredPassword.data();
    int pwLen = enteredPassword.length();
    for (int i = 0; i < pwLen; ++i) {
        pwData[i] = QChar('\0');
    }
    enteredPassword.clear();

    if (!isValid) {
        QMessageBox::critical(parent, "Invalid Password", "The password you entered is incorrect.");
        return false;
    }

    // Record successful validation for grace period
    recordSuccessfulValidation(username);

    return true;
}

bool PasswordValidation::validatePasswordWithCustomCancel(QWidget* parent, const QString& operationName,
                                                          const QString& username, const QString& cancelButtonText,
                                                          int gracePeriodSeconds)
{
    // Check if we're within grace period
    if (isWithinGracePeriod(username, gracePeriodSeconds)) {
        qDebug() << "Password validation skipped due to grace period for operation:" << operationName;
        return true;
    }

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
    DatabaseAuthManager& dbManager = DatabaseAuthManager::instance();

    // Ensure database connection
    if (!dbManager.isConnected()) {
        // Clear password before returning
        QChar* pwData = enteredPassword.data();
        int pwLen = enteredPassword.length();
        for (int i = 0; i < pwLen; ++i) {
            pwData[i] = QChar('\0');
        }
        enteredPassword.clear();
        
        QMessageBox::critical(parent, "Error", "Database connection failed.");
        return false;
    }

    // Get stored password hash for the username
    QString storedHash = dbManager.GetUserData_String(username, Constants::UserT_Index_Password);

    // Validate the password
    bool isValid = CryptoUtils::Hashing_CompareHash(storedHash, enteredPassword);
    
    // Securely clear the entered password from memory
    QChar* pwData = enteredPassword.data();
    int pwLen = enteredPassword.length();
    for (int i = 0; i < pwLen; ++i) {
        pwData[i] = QChar('\0');
    }
    enteredPassword.clear();

    if (!isValid) {
        QMessageBox::critical(parent, "Invalid Password", "The password you entered is incorrect.");
        return false;
    }

    // Record successful validation for grace period
    recordSuccessfulValidation(username);

    return true;
}
