#include "loginscreen.h"
#include "ui_loginscreen.h"
#include "mainwindow.h"
#include "CryptoUtils.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include "sqlite-database-auth.h"
#include "constants.h"
#include "settings_default_usersettings.h"
#include <cstring> // For secure memory operations
#include <openssl/crypto.h> // For OPENSSL_cleanse

loginscreen::loginscreen(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::loginscreen)
{
    ui->setupUi(this);
    ui->label_ErrorDisplay->setText(""); // hide the default error message
    ui->lineEdit_Password->setEchoMode(QLineEdit::Password);
    ui->pushButton_Login->setChecked(true);
    ui->lineEdit_Username->setFocus();
    #ifdef QT_DEBUG
    this->setWindowTitle("MMDiary - DEBUG - UNSAFE - DO NOT USE");
    #else
    this->setWindowTitle("MMDiary - v" + Constants::AppVer);
    #endif
    //Prevent whitespaces inside username and password fields
    QRegularExpression noWhitespace("[^\\s]*"); // Matches any non-whitespace characters
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, this);
    ui->lineEdit_Password->setValidator(validator);
    ui->lineEdit_Username->setValidator(validator);

    DatabaseAuthManager& db = DatabaseAuthManager::instance();
    // Connect to the database
    if (!db.connect()) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        return;
    }

    // Initialize versioning system
    if (!db.initializeVersioning()) {
        qCritical() << "Failed to initialize versioning system:" << db.lastError();
        return;
    }

    // Apply all pending migrations
    if (!db.migrateAuthDatabase()) {
        qCritical() << "Failed to migrate database:" << db.lastError();
        return;
    }
}

loginscreen::~loginscreen()
{
    qDebug() << "loginscreen: Destructor - Clearing sensitive data from memory";
    
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
    
    // Clear username field
    if (ui && ui->lineEdit_Username) {
        ui->lineEdit_Username->clear();
        ui->lineEdit_Username->setText(QString());
    }
    
    delete ui;
}

//functions
bool loginscreen::isUserInputValid()
{
    // Clear any previous error message
    ui->label_ErrorDisplay->setText("");

    // Validate username
    InputValidation::ValidationResult usernameResult =
        InputValidation::validateInput(ui->lineEdit_Username->text(), InputValidation::InputType::Username);

    if (!usernameResult.isValid) {
        ui->label_ErrorDisplay->setText(usernameResult.errorMessage);
        return false;
    }


    // Validate password
    InputValidation::ValidationResult passwordResult =
        InputValidation::validateInput(ui->lineEdit_Password->text(), InputValidation::InputType::Password);

    if (!passwordResult.isValid) {
        ui->label_ErrorDisplay->setText(passwordResult.errorMessage);
        return false;
    }


    // If we made it here, both username and password are valid
    return true;
}


// slots


void loginscreen::on_pushButton_Login_clicked()
{
    if(isUserInputValid())
    {
        DatabaseAuthManager& db = DatabaseAuthManager::instance();
        // Connect to the database
        if (!db.connect()) {
            qCritical() << "Failed to connect to database:" << db.lastError();
            return;
        }
        if(db.GetUserData_String(ui->lineEdit_Username->text(),Constants::UserT_Index_Username) == Constants::ErrorMessage_INVUSER) // if we return INVALIDUSER, it means that the user does not already exist
        {
            ui->label_ErrorDisplay->setText("Account doesn't exist. Verify spelling or make a new account.");
            return;
        }
        else if(db.GetUserData_String(ui->lineEdit_Username->text(),Constants::UserT_Index_Username) == Constants::ErrorMessage_Default) // error accessing the database
        {
            ui->label_ErrorDisplay->setText("An Error occured trying to access the database.");
            return;
        }
        else if(!CryptoUtils::Hashing_CompareHash(db.GetUserData_String(ui->lineEdit_Username->text(),Constants::UserT_Index_Password), ui->lineEdit_Password->text())) // password is incorrect
        {
            ui->label_ErrorDisplay->setText("Incorrect Password.");
            return;
        }
        else if(CryptoUtils::Hashing_CompareHash(db.GetUserData_String(ui->lineEdit_Username->text(),Constants::UserT_Index_Password), ui->lineEdit_Password->text())) // password is correct
        {
            qDebug() << "loginscreen: Authentication successful, deriving encryption key";
            
            // Store password temporarily for key derivation
            QString tempPassword = ui->lineEdit_Password->text();
            
            // Immediately clear the UI password field
            ui->lineEdit_Password->clear();
            ui->lineEdit_Password->setText(QString());
            
            OperationsFiles::setUsername(ui->lineEdit_Username->text()); // set the username in OperationsFiles so that we may create temp files at the correct location
            
            // Derive encryption key
            QByteArray salt = db.GetUserData_ByteA(ui->lineEdit_Username->text(), Constants::UserT_Index_Salt);
            QByteArray derivedKey = CryptoUtils::Encryption_DeriveWithSalt(tempPassword, salt);
            QByteArray encryptedStoredKey = db.GetUserData_ByteA(ui->lineEdit_Username->text(), Constants::UserT_Index_EncryptionKey);
            QByteArray encryptionKey = CryptoUtils::Encryption_DecryptBArray(derivedKey, encryptedStoredKey);
            
            // Create SecureByteArray from the decrypted key
            SecureByteArray secureKey(encryptionKey);
            
            // Securely clear temporary password from memory using volatile pointer
            volatile QChar* pwData = const_cast<volatile QChar*>(tempPassword.constData());
            int pwLen = tempPassword.length();
            for (int i = 0; i < pwLen; ++i) {
                // Use const_cast to temporarily remove volatile for assignment
                const_cast<QChar*>(pwData)[i] = QChar('\0');
            }
            tempPassword.clear();
            
            // Securely clear intermediate key materials using OPENSSL_cleanse
            if (!derivedKey.isEmpty()) {
                OPENSSL_cleanse(derivedKey.data(), derivedKey.size());
                derivedKey.clear();
            }
            if (!salt.isEmpty()) {
                OPENSSL_cleanse(salt.data(), salt.size());
                salt.clear();
            }
            if (!encryptedStoredKey.isEmpty()) {
                OPENSSL_cleanse(encryptedStoredKey.data(), encryptedStoredKey.size());
                encryptedStoredKey.clear();
            }
            if (!encryptionKey.isEmpty()) {
                OPENSSL_cleanse(encryptionKey.data(), encryptionKey.size());
                encryptionKey.clear();
            }

            // Check if we need to delete backups (after password change)
            qDebug() << "loginscreen: Checking for scheduled backup deletion";
            if (!db.checkAndDeleteBackupsIfNeeded(ui->lineEdit_Username->text())) {
                qWarning() << "loginscreen: Failed to process backup deletion, but continuing with login";
            }

            MainWindow *mw =  new MainWindow(this->parentWidget());
            connect(this, &loginscreen::passDataMW_Signal, mw, &MainWindow::ReceiveDataLogin_Slot);
            // Transfer ownership of the key to MainWindow
            SecureByteArray* keyPtr = new SecureByteArray(std::move(secureKey));
            passDataMW_Signal(ui->lineEdit_Username->text(), keyPtr);
            
            // Note: ownership of keyPtr has been transferred to MainWindow
            
            mw->show();
            loggingIn = true;
            this->close();
        }
    }

}


void loginscreen::on_pushButton_NewAccount_clicked()
{
    if(isUserInputValid())

    {
        if(ui->lineEdit_Username->text().toLower() == "mmdiary.db")
        {
            ui->label_ErrorDisplay->setText("Username cannot be MMDiary.db");
            return;
        }
        DatabaseAuthManager& db = DatabaseAuthManager::instance();
        // Connect to the database
        if (!db.connect()) {
            qCritical() << "Failed to connect to database:" << db.lastError();
            return;
        }
        if(db.GetUserData_String(ui->lineEdit_Username->text(),Constants::UserT_Index_Username) == Constants::ErrorMessage_INVUSER) // if we return INVALIDUSER, it means that the user does not already exist, so we can create it.
        {
            OperationsFiles::setUsername(ui->lineEdit_Username->text()); // set the username in OperationsFiles so that we may create temp files at the correct location

            // Make sure we're connected
            if (!db.isConnected()) {
                if (!db.connect()) {
                    qCritical() << "Failed to connect to database:" << db.lastError();
                }
            }

            // Store password temporarily for processing
            QString tempPassword = ui->lineEdit_Password->text();
            QString tempUsername = ui->lineEdit_Username->text();
            
            // Immediately clear the UI password field
            ui->lineEdit_Password->clear();
            ui->lineEdit_Password->setText(QString());
            
            // Prepare user creation data
            QString hashedPassword = CryptoUtils::Hashing_HashPassword(tempPassword);

            QByteArray salt;
            QByteArray derivedKeyWithSalt = CryptoUtils::Encryption_DeriveKey(tempPassword, &salt);
            // Extract just the derivedKey part (remove the salt)
            QByteArray derivedKey = derivedKeyWithSalt.mid(salt.size());
            QByteArray encryptionKey = CryptoUtils::Encryption_GenerateKey(); // creates the encryption key
            QByteArray encryptedKey = CryptoUtils::Encryption_EncryptBArray(derivedKey, encryptionKey, tempUsername);
            
            // Securely clear temporary password from memory using volatile pointer
            volatile QChar* pwData = const_cast<volatile QChar*>(tempPassword.constData());
            int pwLen = tempPassword.length();
            for (int i = 0; i < pwLen; ++i) {
                // Use const_cast to temporarily remove volatile for assignment
                const_cast<QChar*>(pwData)[i] = QChar('\0');
            }
            tempPassword.clear();
            
            // Create SecureByteArray for the encryption key
            SecureByteArray secureEncryptionKey(encryptionKey);
            
            // Securely clear intermediate key materials after use
            bool createSuccess = db.CreateUser(tempUsername, hashedPassword, encryptedKey, salt, tempUsername);
            
            // Clear sensitive intermediate data using OPENSSL_cleanse
            if (!derivedKeyWithSalt.isEmpty()) {
                OPENSSL_cleanse(derivedKeyWithSalt.data(), derivedKeyWithSalt.size());
                derivedKeyWithSalt.clear();
            }
            if (!derivedKey.isEmpty()) {
                OPENSSL_cleanse(derivedKey.data(), derivedKey.size());
                derivedKey.clear();
            }
            if (!salt.isEmpty()) {
                OPENSSL_cleanse(salt.data(), salt.size());
                salt.clear();
            }
            if (!encryptedKey.isEmpty()) {
                OPENSSL_cleanse(encryptedKey.data(), encryptedKey.size());
                encryptedKey.clear();
            }
            if (!encryptionKey.isEmpty()) {
                OPENSSL_cleanse(encryptionKey.data(), encryptionKey.size());
                encryptionKey.clear();
            }

            // Create user using the new CreateUser method
            if (createSuccess) {
                qDebug() << "loginscreen: User created with ID:" << db.lastInsertId();

                // Use const reference conversion instead of .data() to prevent copy
                if(!Default_UserSettings::SetAllDefaults(tempUsername, secureEncryptionKey)) // set user defaults
                {
                    // SecureByteArray will clear automatically on destruction
                    this->close(); // if unable to do so, exit app
                }
                MainWindow *mw =  new MainWindow(this->parentWidget());
                connect(this, &loginscreen::passDataMW_Signal, mw, &MainWindow::ReceiveDataLogin_Slot);
                // Transfer ownership of the key to MainWindow
                SecureByteArray* keyPtr = new SecureByteArray(std::move(secureEncryptionKey));
                passDataMW_Signal(tempUsername, keyPtr);
                
                // Note: ownership of keyPtr has been transferred to MainWindow
                
                mw->show();
                loggingIn = true;
                this->close();
            } else {
                qWarning() << "loginscreen: Failed to create user:" << db.lastError();
                ui->label_ErrorDisplay->setText("Failed to create user account.");
                
                // SecureByteArray will clear automatically on destruction
            }
        }
        else // the user does exist
        {
            ui->label_ErrorDisplay->setText("User Already Exists. Choose a different username.");
        }
    }
}


void loginscreen::closeEvent(QCloseEvent *event)
{
    //This is a demo that shows how to use the encryption system and the fact that it works as intended.
    /*
    QString encryptedTest = "Potatoes";
    qDebug() << "Test Text: " << encryptedTest;
    //Retrieve the stored encryption key, decrypt it using a key derived from password and the stored salt. key is now the proper key used to decrypt diaries.
    QString key = CryptoUtils::Encryption_Decrypt(CryptoUtils::Encryption_DeriveWithSalt(ui->lineEdit_Password->text(),db.GetUserData_ByteA(ui->lineEdit_Username->text())),db.GetUserData_String(ui->lineEdit_Username->text(),"encryptionkey"));
    qDebug() << "The Decrypted Encryption Key: " << key;
    //encrypt using the key
    encryptedTest = CryptoUtils::Encryption_Encrypt(key,encryptedTest);
    qDebug() << "The Encrypted Text: " << encryptedTest;
    //decrypt using the key
    encryptedTest = CryptoUtils::Encryption_Decrypt(key,encryptedTest);
    qDebug() << "The Decrypted Text: " << encryptedTest;
    */
    if(!loggingIn){qApp->quit();}else{loggingIn = false;}
}
