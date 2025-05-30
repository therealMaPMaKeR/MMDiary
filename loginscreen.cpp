#include "loginscreen.h"
#include "ui_loginscreen.h"
#include "mainwindow.h"
#include "Operations-Global/CryptoUtils.h"
#include "Operations-Global/inputvalidation.h"
#include "Operations-Global/operations_files.h"
#include "Operations-Global/sqlite-database-auth.h"
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
    this->setWindowTitle("MMDiary - v" + Constants::AppVer);

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

            // Prepare user creation data
            QString hashedPassword = CryptoUtils::Hashing_HashPassword(ui->lineEdit_Password->text());

            QByteArray salt;
            QByteArray derivedKeyWithSalt = CryptoUtils::Encryption_DeriveKey(ui->lineEdit_Password->text(), &salt);
            // Extract just the derivedKey part (remove the salt)
            QByteArray derivedKey = derivedKeyWithSalt.mid(salt.size());
            QByteArray encryptionKey = CryptoUtils::Encryption_GenerateKey(); // creates the encryption key
            QByteArray encryptedKey = CryptoUtils::Encryption_EncryptBArray(derivedKey, encryptionKey, ui->lineEdit_Username->text());

            // Create user using the new CreateUser method
            if (db.CreateUser(ui->lineEdit_Username->text(), hashedPassword, encryptedKey, salt, ui->lineEdit_Username->text())) {
                qDebug() << "User created with ID:" << db.lastInsertId();

                if(!Default_UserSettings::SetAllDefaults(ui->lineEdit_Username->text(), encryptionKey)) // set user defaults
                {
                    this->close(); // if unable to do so, exit app
                }
                MainWindow *mw =  new MainWindow(this->parentWidget());
                connect(this, &loginscreen::passDataMW_Signal, mw, &MainWindow::ReceiveDataLogin_Slot);
                passDataMW_Signal(ui->lineEdit_Username->text(), encryptionKey);
                mw->show();
                loggingIn = true;
                this->close();
            } else {
                qWarning() << "Failed to create user:" << db.lastError();
                ui->label_ErrorDisplay->setText("Failed to create user account.");
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
