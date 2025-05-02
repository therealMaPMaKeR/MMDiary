#include "loginscreen.h"
#include "ui_loginscreen.h"
#include "mainwindow.h"
#include "Operations-Global/CryptoUtils.h"
#include "Operations-Global/inputvalidation.h"
#include "Operations-Global/operations_files.h"
#include "constants.h"
#include "Operations-Global/default_usersettings.h"

loginscreen::loginscreen(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::loginscreen)
{
    ui->setupUi(this);
    ui->label_ErrorDisplay->setText(""); // hide the default error message
    ui->lineEdit_Password->setEchoMode(QLineEdit::Password);
    ui->pushButton_Login->setChecked(true);
    ui->lineEdit_Username->setFocus();
    this->setWindowTitle("MMDiary - V4.0.0");

    //Prevent whitespaces inside username and password fields
    QRegularExpression noWhitespace("[^\\s]*"); // Matches any non-whitespace characters
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, this);
    ui->lineEdit_Password->setValidator(validator);
    ui->lineEdit_Username->setValidator(validator);

    DatabaseManager& db = DatabaseManager::instance();
    // Connect to the database
    if (!db.connect(Constants::DBPath_User)) {
        qCritical() << "Failed to connect to database:" << db.lastError();
        return;
    }

    // Initialize versioning system
    if (!db.initializeVersioning()) {
        qCritical() << "Failed to initialize versioning system:" << db.lastError();
        return;
    }

    // Apply all pending migrations
    if (!db.migrateDatabase()) {
        qCritical() << "Failed to migrate database:" << db.lastError();
        return;
    }
}

loginscreen::~loginscreen()
{
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
        DatabaseManager& db = DatabaseManager::instance();
        // Connect to the database
        if (!db.connect(Constants::DBPath_User)) {
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
            OperationsFiles::setUsername(ui->lineEdit_Username->text()); // set the username in OperationsFiles so that we may create temp files at the correct location
            QByteArray encryptionKey = CryptoUtils::Encryption_DecryptBArray(CryptoUtils::Encryption_DeriveWithSalt(ui->lineEdit_Password->text(),db.GetUserData_ByteA(ui->lineEdit_Username->text(), Constants::UserT_Index_Salt)),db.GetUserData_ByteA(ui->lineEdit_Username->text(),Constants::UserT_Index_EncryptionKey));

            MainWindow *mw =  new MainWindow(this->parentWidget());
            connect(this, &loginscreen::passDataMW_Signal, mw, &MainWindow::ReceiveDataLogin_Slot);
            passDataMW_Signal(ui->lineEdit_Username->text(), encryptionKey);
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
        DatabaseManager& db = DatabaseManager::instance();
        // Connect to the database
        if (!db.connect(Constants::DBPath_User)) {
            qCritical() << "Failed to connect to database:" << db.lastError();
            return;
        }
        if(db.GetUserData_String(ui->lineEdit_Username->text(),Constants::UserT_Index_Username) == Constants::ErrorMessage_INVUSER) // if we return INVALIDUSER, it means that the user does not already exist, so we can create it.
        {
            OperationsFiles::setUsername(ui->lineEdit_Username->text()); // set the username in OperationsFiles so that we may create temp files at the correct location
            // Get database manager instance
            DatabaseManager& db = DatabaseManager::instance();
            // Make sure we're connected
            if (!db.isConnected()) {
                if (!db.connect(Constants::DBPath_User)) {
                    qCritical() << "Failed to connect to database:" << db.lastError();
                }
            }
            QMap<QString, QVariant> userData;
            //add user to database
            userData[Constants::UserT_Index_Username] = ui->lineEdit_Username->text(); // set user username
            userData[Constants::UserT_Index_Password] = CryptoUtils::Hashing_HashPassword(ui->lineEdit_Password->text()); // set user password
            QByteArray salt;
            QByteArray derivedKeyWithSalt = CryptoUtils::Encryption_DeriveKey(ui->lineEdit_Password->text(), &salt);
            // Extract just the derivedKey part (remove the salt)
            QByteArray derivedKey = derivedKeyWithSalt.mid(salt.size());
            QByteArray encryptionKey = CryptoUtils::Encryption_GenerateKey(); // creates the encryption key
            userData[Constants::UserT_Index_Salt] = salt; // store the salt value of the key we will use to encrypt the encryption key. This way, we don't need to store any sensitive information.
            userData[Constants::UserT_Index_EncryptionKey] = CryptoUtils::Encryption_EncryptBArray(derivedKey, encryptionKey,ui->lineEdit_Username->text());  //encrypts the key using a key derived from password.
            // set default setting values
            userData[Constants::UserT_Index_Displayname] = ui->lineEdit_Username->text(); // set the users display name as their username by default. they can change it later.
            userData[Constants::UserT_Index_Iterations] = "500000";
            if (db.insert("users", userData)) {
                qDebug() << "User added with ID:" << db.lastInsertId();
            } else {
                qWarning() << "Failed to add user:" << db.lastError();
            }
            if(!Default_UserSettings::SetAllDefaults(ui->lineEdit_Username->text())) // set user defaults
            {
                this->close(); // if unable to do so, exit app
            }
            MainWindow *mw =  new MainWindow(this->parentWidget());
            connect(this, &loginscreen::passDataMW_Signal, mw, &MainWindow::ReceiveDataLogin_Slot);
            passDataMW_Signal(ui->lineEdit_Username->text(), encryptionKey);
            mw->show();
            loggingIn = true;
            this->close();
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
