#include "operations_passwordmanager.h"
#include "../CustomWidgets/CombinedDelegate.h"
#include "../Operations-Global/CryptoUtils.h"
#include "Operations-Global/operations_files.h"
#include "Operations-Global/operations.h"
#include "ui_mainwindow.h"
#include "ui_passwordmanager_addpassword.h"
#include "../constants.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <QDialog>
#include "Operations-Global/inputvalidation.h"

Operations_PasswordManager::Operations_PasswordManager(MainWindow* mainWindow)
    : m_mainWindow(mainWindow)
{
    connect(m_mainWindow->ui->listWidget_PWList, &QListWidget::itemClicked,
            this, &Operations_PasswordManager::on_PWListItemClicked);

    // Connect the context menu signals
    connect(m_mainWindow->ui->tableWidget_PWDisplay, &QWidget::customContextMenuRequested,
            this, &Operations_PasswordManager::showContextMenu_PWDisplay);
    connect(m_mainWindow->ui->listWidget_PWList, &QWidget::customContextMenuRequested,
            this, &Operations_PasswordManager::showContextMenu_PWList);

    // Connect double-click signal to copy text
    connect(m_mainWindow->ui->tableWidget_PWDisplay, &QTableWidget::itemDoubleClicked,
            this, &Operations_PasswordManager::onTableItemDoubleClicked);

    // Enable context menu policies
    m_mainWindow->ui->tableWidget_PWDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    m_mainWindow->ui->listWidget_PWList->setContextMenuPolicy(Qt::CustomContextMenu);

    // Initialize clipboard timer
    m_clipboardTimer = new QTimer(this);
    m_clipboardTimer->setSingleShot(true);
    connect(m_clipboardTimer, &QTimer::timeout, this, &Operations_PasswordManager::clearClipboard);
}

//-----------------Password Display-----------------//
void Operations_PasswordManager::SetupPWDisplay(QString sortingMethod)
{
    // Temporarily disable sorting to prevent unwanted sorting during setup
    m_mainWindow->ui->tableWidget_PWDisplay->setSortingEnabled(false);

    m_mainWindow->ui->tableWidget_PWDisplay->clear(); // first we clear the display
    m_mainWindow->ui->tableWidget_PWDisplay->setRowCount(0); // then, we remove all rows
    m_mainWindow->ui->tableWidget_PWDisplay->setColumnCount(3); // then, we set column count to 3 (updated from 2)

    // We set the value of the columns depending on the sorting method selected.
    if(sortingMethod == "Password")
    {
        m_mainWindow->ui->tableWidget_PWDisplay->setHorizontalHeaderLabels(QStringList() << "Password" << "Account" << "Service");
        m_mainWindow->ui->label_PWDisplayIND->setText("Passwords");
    }
    else if(sortingMethod == "Account")
    {
        m_mainWindow->ui->tableWidget_PWDisplay->setHorizontalHeaderLabels(QStringList() << "Account" << "Password" << "Service");
        m_mainWindow->ui->label_PWDisplayIND->setText("Accounts");
    }
    else if(sortingMethod == "Service")
    {
        m_mainWindow->ui->tableWidget_PWDisplay->setHorizontalHeaderLabels(QStringList() << "Service" << "Account" << "Password");
        m_mainWindow->ui->label_PWDisplayIND->setText("Services");
    }

    m_mainWindow->ui->tableWidget_PWDisplay->horizontalHeader()->setSectionsMovable(true); // we make the columns movable
    m_mainWindow->ui->tableWidget_PWDisplay->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); // we allow the user to resize the columns

    // Reset the sort indicator on the horizontal header to remove any previous sorting
    m_mainWindow->ui->tableWidget_PWDisplay->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);

    // Set default size for all three columns
    m_mainWindow->ui->tableWidget_PWDisplay->setColumnWidth(0, 200);
    m_mainWindow->ui->tableWidget_PWDisplay->setColumnWidth(1, 200);
    m_mainWindow->ui->tableWidget_PWDisplay->setColumnWidth(2, 200);

    // Re-enable sorting after setup is complete
    m_mainWindow->ui->tableWidget_PWDisplay->setSortingEnabled(true);

    // Set default sort on the second column (index 1) in ascending order
    //m_mainWindow->ui->tableWidget_PWDisplay->horizontalHeader()->setSortIndicator(1, Qt::AscendingOrder);
    //m_mainWindow->ui->tableWidget_PWDisplay->sortItems(1, Qt::AscendingOrder);

    m_mainWindow->ui->tableWidget_PWDisplay->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UpdatePasswordMasking();
}

void Operations_PasswordManager::SetupPWList(QString sortingMethod, bool applyMasking)
{
    m_mainWindow->ui->listWidget_PWList->clear();

    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Validate the file path using our centralized function
    if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
        qWarning() << "Password file failed validation check: " << passwordsFilePath;
        QMessageBox::warning(m_mainWindow, "Password File Error",
                             "The password file appears to be corrupted or tampered with.");
        return;
    }

    // Check if the passwords file exists
    if (!QFileInfo::exists(passwordsFilePath)) {
        // If the file doesn't exist, return without an error - there are just no passwords yet
        return;
    }

    // Read the encrypted file content
    QString fileContent;
    if (!OperationsFiles::readEncryptedFile(passwordsFilePath, m_mainWindow->user_Key, fileContent)) {
        qDebug() << "Failed to read passwords file: " << passwordsFilePath;
        return;
    }

    // Sets to store unique values
    QSet<QString> uniqueValues;

    // Parse the passwords file
    QTextStream fileStream(&fileContent);
    QString line;
    QString account, password, service;

    while (!fileStream.atEnd()) {
        line = fileStream.readLine();

        // Validate each line from the file
        InputValidation::ValidationResult lineResult =
            InputValidation::validateInput(line, InputValidation::InputType::PlainText);
        if (!lineResult.isValid) {
            qWarning() << "Invalid content in passwords file: " << lineResult.errorMessage;
            continue; // Skip this line but continue processing
        }

        if (line == "<Password>") {
            // Reset fields for a new password entry
            account = password = service = "";

            // Read the next lines until empty line or end of file
            while (!fileStream.atEnd()) {
                line = fileStream.readLine();

                // Validate each line
                InputValidation::ValidationResult entryResult =
                    InputValidation::validateInput(line, InputValidation::InputType::PlainText);
                if (!entryResult.isValid) {
                    qWarning() << "Invalid entry in passwords file: " << entryResult.errorMessage;
                    continue; // Skip this line but continue processing
                }

                if (line.isEmpty()) {
                    break; // End of this password entry
                }

                if (line.startsWith("Account: ")) {
                    account = line.mid(9); // Remove "Account: " prefix
                } else if (line.startsWith("Password: ")) {
                    password = line.mid(10); // Remove "Password: " prefix
                } else if (line.startsWith("Service: ")) {
                    service = line.mid(9); // Remove "Service: " prefix
                }
            }

            // Validate extracted data
            bool accountValid = !account.isEmpty() &&
                                InputValidation::validateInput(account, InputValidation::InputType::Line).isValid;
            bool passwordValid = !password.isEmpty() &&
                                 InputValidation::validateInput(password, InputValidation::InputType::Line).isValid;
            bool serviceValid = !service.isEmpty() &&
                                InputValidation::validateInput(service, InputValidation::InputType::Line).isValid;

            // Add the value to our set based on sorting method if all fields are valid
            if (accountValid && passwordValid && serviceValid) {
                if (sortingMethod == "Password") {
                    uniqueValues.insert(password);
                } else if (sortingMethod == "Account") {
                    uniqueValues.insert(account);
                } else if (sortingMethod == "Service") {
                    uniqueValues.insert(service);
                }
            }
        }
    }

    // Convert the set to a sorted list
    QStringList sortedValues = uniqueValues.values();
    std::sort(sortedValues.begin(), sortedValues.end());

    // Add the sorted values to the list widget
    foreach (const QString &value, sortedValues) {
        m_mainWindow->ui->listWidget_PWList->addItem(value);
    }
    if (applyMasking) {
        UpdatePasswordMasking();
    }
}

void Operations_PasswordManager::UpdatePWDisplayForSelection(const QString &selectedValue)
{
    // Get current sorting method
    QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Setup the display with the current sorting method
    SetupPWDisplay(sortingMethod);

    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Validate the password file using our centralized function
    if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
        qWarning() << "Password file failed validation check: " << passwordsFilePath;
        QMessageBox::warning(m_mainWindow, "Password File Error",
                             "The password file appears to be corrupted or tampered with.");
        return;
    }

    // Check if the passwords file exists
    if (!QFileInfo::exists(passwordsFilePath)) {
        return; // No passwords to display
    }

    // Read the encrypted file content
    QString fileContent;
    if (!OperationsFiles::readEncryptedFile(passwordsFilePath, m_mainWindow->user_Key, fileContent)) {
        qDebug() << "Failed to read passwords file: " << passwordsFilePath;
        return;
    }

    // Parse the passwords file and populate the table
    QTextStream fileStream(&fileContent);
    QString line;
    QString account, password, service;
    int row = 0;

    while (!fileStream.atEnd()) {
        line = fileStream.readLine();

        // Validate each line from the file
        InputValidation::ValidationResult lineResult =
            InputValidation::validateInput(line, InputValidation::InputType::PlainText);
        if (!lineResult.isValid) {
            qWarning() << "Invalid content in passwords file: " << lineResult.errorMessage;
            continue; // Skip this line but continue processing
        }

        if (line == "<Password>") {
            // Reset fields for a new password entry
            account = password = service = "";

            // Read the next lines until empty line or end of file
            while (!fileStream.atEnd()) {
                line = fileStream.readLine();

                // Validate each line
                InputValidation::ValidationResult entryResult =
                    InputValidation::validateInput(line, InputValidation::InputType::PlainText);
                if (!entryResult.isValid) {
                    qWarning() << "Invalid entry in passwords file: " << entryResult.errorMessage;
                    continue; // Skip this line but continue processing
                }

                if (line.isEmpty()) {
                    break; // End of this password entry
                }

                if (line.startsWith("Account: ")) {
                    account = line.mid(9); // Remove "Account: " prefix
                } else if (line.startsWith("Password: ")) {
                    password = line.mid(10); // Remove "Password: " prefix
                } else if (line.startsWith("Service: ")) {
                    service = line.mid(9); // Remove "Service: " prefix
                }
            }

            // Validate extracted data
            bool accountValid = !account.isEmpty() &&
                                InputValidation::validateInput(account, InputValidation::InputType::Line).isValid;
            bool passwordValid = !password.isEmpty() &&
                                 InputValidation::validateInput(password, InputValidation::InputType::Line).isValid;
            bool serviceValid = !service.isEmpty() &&
                                InputValidation::validateInput(service, InputValidation::InputType::Line).isValid;

            // Check if this entry matches our selection
            bool matchesSelection = false;

            if (sortingMethod == "Password" && password == selectedValue) {
                matchesSelection = true;
            } else if (sortingMethod == "Account" && account == selectedValue) {
                matchesSelection = true;
            } else if (sortingMethod == "Service" && service == selectedValue) {
                matchesSelection = true;
            }
            // Ensure consistent display of (None) values
            if (account.isEmpty()) {
                account = "(None)";
            }
            if (password.isEmpty()) {
                password = "(None)";
            }
            if (service.isEmpty()) {
                service = "(None)";
            }
            // If this entry matches and all fields are valid, add it to the table
            if (matchesSelection && accountValid && passwordValid && serviceValid) {
                m_mainWindow->ui->tableWidget_PWDisplay->insertRow(row);

                if (sortingMethod == "Password") {
                    // First column is Password (the sorting method)
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 0, new QTableWidgetItem(password));
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 1, new QTableWidgetItem(account));
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 2, new QTableWidgetItem(service));
                } else if (sortingMethod == "Account") {
                    // First column is Account (the sorting method)
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 0, new QTableWidgetItem(account));
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 1, new QTableWidgetItem(password));
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 2, new QTableWidgetItem(service));
                } else if (sortingMethod == "Service") {
                    // First column is Service (the sorting method)
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 0, new QTableWidgetItem(service));
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 1, new QTableWidgetItem(account));
                    m_mainWindow->ui->tableWidget_PWDisplay->setItem(row, 2, new QTableWidgetItem(password));
                }

                // Store the complete entry data as item data for reference
                // Use a role that won't conflict with display or edit roles (Qt::UserRole)
                QTableWidgetItem* firstItem = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0);
                firstItem->setData(Qt::UserRole, account); // Store account
                firstItem->setData(Qt::UserRole + 1, password); // Store password
                firstItem->setData(Qt::UserRole + 2, service); // Store service

                row++;
            }
        }
    }

    // Set default sort on the second column (index 1) in ascending order
    m_mainWindow->ui->tableWidget_PWDisplay->horizontalHeader()->setSortIndicator(1, Qt::AscendingOrder);
    m_mainWindow->ui->tableWidget_PWDisplay->sortItems(1, Qt::AscendingOrder);
    UpdatePasswordMasking();
}

void Operations_PasswordManager::AddPassword(QString account, QString password, QString service)
{
    // If account or service is empty, set to "(None)"
    if (account.isEmpty()) {
        account = "(None)";
    }

    if (service.isEmpty()) {
        service = "(None)";
    }

    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Ensure the directory exists
    if (!OperationsFiles::ensureDirectoryExists(passwordsDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                             "Could not create or access the passwords directory.");
        return;
    }

    // String to hold all passwords content
    QString passwordsContent;
    // Flag to check for duplicates
    bool duplicateFound = false;

    // If file exists, read it and check for duplicate entries
    if (QFileInfo::exists(passwordsFilePath)) {
        // Validate the password file
        if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
            qWarning() << "Password file failed validation check: " << passwordsFilePath;
            QMessageBox::warning(m_mainWindow, "Password File Error",
                                 "The existing password file appears to be corrupted or tampered with.");
            return;
        }

        // Read the encrypted file content
        if (!OperationsFiles::readEncryptedFile(passwordsFilePath, m_mainWindow->user_Key, passwordsContent)) {
            qDebug() << "Failed to read passwords file: " << passwordsFilePath;
            return;
        }

        // Parse file to check for duplicates
        QTextStream fileStream(&passwordsContent);
        QString line;
        bool inPasswordBlock = false;
        QString currentAccount, currentPassword, currentService;
        QString processedContent;

        while (!fileStream.atEnd()) {
            line = fileStream.readLine();

            // Validate each line
            InputValidation::ValidationResult lineResult =
                InputValidation::validateInput(line, InputValidation::InputType::PlainText);

            if (!lineResult.isValid) {
                qWarning() << "Invalid content in passwords file: " << lineResult.errorMessage;
                continue;
            }

            // Start of a password entry
            if (line == "<Password>") {
                inPasswordBlock = true;
                currentAccount = currentPassword = currentService = "";
                processedContent += line + "\n";  // Add this line to content
                continue;
            }

            // Process lines inside a password block
            if (inPasswordBlock) {
                processedContent += line + "\n";  // Add this line to content

                if (line.isEmpty()) {
                    // End of password block
                    inPasswordBlock = false;

                    // Check if this is a duplicate entry
                    if (currentAccount == account &&
                        currentPassword == password &&
                        currentService == service) {
                        duplicateFound = true;
                    }
                }
                else if (line.startsWith("Account: ")) {
                    currentAccount = line.mid(9);  // Remove "Account: " prefix
                }
                else if (line.startsWith("Password: ")) {
                    currentPassword = line.mid(10);  // Remove "Password: " prefix
                }
                else if (line.startsWith("Service: ")) {
                    currentService = line.mid(9);  // Remove "Service: " prefix
                }
            }
            else {
                // Outside a password block, just add to processed content
                processedContent += line + "\n";
            }
        }

        // Update the passwords content with processed content
        passwordsContent = processedContent;
    }

    // If duplicate found, return without adding new entry
    if (duplicateFound) {
        // Return silently as requested
        return;
    }

    // Format the new password entry
    QString newPasswordEntry = "<Password>\n"
                               "Account: " + account + "\n"
                                           "Password: " + password + "\n"
                                            "Service: " + service + "\n\n";

    // Add the new password entry to the content
    passwordsContent += newPasswordEntry;

    // Write the updated content to the encrypted file
    if (!OperationsFiles::writeEncryptedFile(passwordsFilePath, m_mainWindow->user_Key, passwordsContent)) {
        qDebug() << "Failed to write passwords file: " << passwordsFilePath;
        QMessageBox::warning(m_mainWindow, "Encryption Error",
                             "Failed to encrypt passwords file. Your passwords may not be secure.");
        return;
    }

    // Get current sorting method
    QString currentSortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Determine which value to look for in the list based on sorting method
    QString valueToFind;
    if (currentSortingMethod == "Password") {
        valueToFind = password;
    } else if (currentSortingMethod == "Account") {
        valueToFind = account;
    } else if (currentSortingMethod == "Service") {
        valueToFind = service;
    }

    // Update the password list
    SetupPWList(currentSortingMethod);

    // Find and select the newly added item in the list
    for (int i = 0; i < m_mainWindow->ui->listWidget_PWList->count(); ++i) {
        if (m_mainWindow->ui->listWidget_PWList->item(i)->text() == valueToFind) {
            m_mainWindow->ui->listWidget_PWList->setCurrentRow(i);
            // Simulate a click on the item to load the table
            on_PWListItemClicked(m_mainWindow->ui->listWidget_PWList->item(i));
            break;
        }
    }
}

bool Operations_PasswordManager::ModifyPassword(const QString &oldAccount, const QString &oldPassword, const QString &oldService, const QString &newAccount, const QString &newPassword, const QString &newService)
{
    // Create copies of the parameters that we can modify
    QString modifiedNewAccount = newAccount;
    QString modifiedNewService = newService;

    // If new account or service is empty, set to "(None)"
    if (modifiedNewAccount.isEmpty()) {
        modifiedNewAccount = "(None)";
    }

    if (modifiedNewService.isEmpty()) {
        modifiedNewService = "(None)";
    }

    // If old and new values are identical (using our modified values for comparison)
    if (oldAccount == modifiedNewAccount && oldPassword == newPassword && oldService == modifiedNewService) {
        return true;  // No changes needed
    }

    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Validate the password file
    if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
        qWarning() << "Password file failed validation check: " << passwordsFilePath;
        return false;
    }

    // Check if the passwords file exists
    if (!QFileInfo::exists(passwordsFilePath)) {
        return false;  // No passwords file to modify
    }

    // Use the processEncryptedFile function to modify the password file
    bool success = OperationsFiles::processEncryptedFile(passwordsFilePath, m_mainWindow->user_Key,
                                                         [&](QString& content) -> bool {
                                                             // Process the content to modify the password
                                                             QTextStream fileStream(&content);
                                                             QString line;
                                                             bool oldPasswordFound = false;
                                                             bool newPasswordExists = false;

                                                             // Temporary storage for file content without the old password
                                                             QString newFileContent;

                                                             // Variables to track current password entry
                                                             bool inPasswordBlock = false;
                                                             QString currentAccount, currentPassword, currentService;
                                                             QString currentBlock;

                                                             while (!fileStream.atEnd()) {
                                                                 line = fileStream.readLine();

                                                                 // Start of a password entry
                                                                 if (line == "<Password>") {
                                                                     inPasswordBlock = true;
                                                                     currentBlock = line + "\n";
                                                                     currentAccount = currentPassword = currentService = "";
                                                                     continue;
                                                                 }

                                                                 // End of a password entry (empty line)
                                                                 if (inPasswordBlock && line.isEmpty()) {
                                                                     inPasswordBlock = false;

                                                                     // Check if this is the password to be removed
                                                                     if (currentAccount == oldAccount && currentPassword == oldPassword && currentService == oldService) {
                                                                         oldPasswordFound = true;
                                                                         // Don't add this block to the new content
                                                                     }
                                                                     // Check if this matches the new password we want to add
                                                                     else if (currentAccount == modifiedNewAccount && currentPassword == newPassword && currentService == modifiedNewService) {
                                                                         newPasswordExists = true;
                                                                         // Still add this block to the new content
                                                                         newFileContent += currentBlock + "\n";
                                                                     }
                                                                     else {
                                                                         // Add this block to the new content
                                                                         newFileContent += currentBlock + "\n";
                                                                     }
                                                                     continue;
                                                                 }

                                                                 // Inside a password block
                                                                 if (inPasswordBlock) {
                                                                     currentBlock += line + "\n";

                                                                     if (line.startsWith("Account: ")) {
                                                                         currentAccount = line.mid(9);  // Remove "Account: " prefix
                                                                     } else if (line.startsWith("Password: ")) {
                                                                         currentPassword = line.mid(10);  // Remove "Password: " prefix
                                                                     } else if (line.startsWith("Service: ")) {
                                                                         currentService = line.mid(9);  // Remove "Service: " prefix
                                                                     }
                                                                 }
                                                                 else {
                                                                     // Outside a password block, just add to new content
                                                                     newFileContent += line + "\n";
                                                                 }
                                                             }

                                                             // If we didn't find the old password, something's wrong
                                                             if (!oldPasswordFound) {
                                                                 return false;
                                                             }

                                                             // If the new password doesn't already exist, add it
                                                             if (!newPasswordExists) {
                                                                 QString newPasswordEntry = "<Password>\n"
                                                                                            "Account: " + modifiedNewAccount + "\n"
                                                                                                                   "Password: " + newPassword + "\n"
                                                                                                            "Service: " + modifiedNewService + "\n\n";
                                                                 newFileContent += newPasswordEntry;
                                                             }

                                                             // Update the content
                                                             content = newFileContent;
                                                             return true;
                                                         }
                                                         );

    if (!success) {
        qDebug() << "Failed to modify password in file: " << passwordsFilePath;
        return false;
    }

    // Update the UI
    QString currentSortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Store the current selection
    QString currentListSelection;
    if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
        currentListSelection = m_mainWindow->ui->listWidget_PWList->currentItem()->text();
    }

    // Update the list
    SetupPWList(currentSortingMethod);

    // Check if the current selection still exists in the list
    bool selectionExists = false;
    for (int i = 0; i < m_mainWindow->ui->listWidget_PWList->count(); ++i) {
        if (m_mainWindow->ui->listWidget_PWList->item(i)->text() == currentListSelection) {
            m_mainWindow->ui->listWidget_PWList->setCurrentRow(i);
            selectionExists = true;
            break;
        }
    }

    if (selectionExists) {
        // Trigger a click on the current item to refresh the table
        if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
            on_PWListItemClicked(m_mainWindow->ui->listWidget_PWList->currentItem());
        }
    } else {
        // Reset the display
        SetupPWDisplay(currentSortingMethod);
    }

    return true;
}

bool Operations_PasswordManager::DeletePassword(const QString &account, const QString &password, const QString &service)
{
    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Validate the password file
    if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
        qWarning() << "Password file failed validation check: " << passwordsFilePath;
        return false;
    }

    // Check if the passwords file exists
    if (!QFileInfo::exists(passwordsFilePath)) {
        return false; // No passwords file to modify
    }

    // Define pattern to find the password entry
    QRegularExpression pattern(
        "<Password>\\s*"
        "Account: " + QRegularExpression::escape(account) + "\\s*"
                                                "Password: " + QRegularExpression::escape(password) + "\\s*"
                                                 "Service: " + QRegularExpression::escape(service) + "\\s*\\n"
        );

    // Use the processEncryptedFile function to modify the password file
    return OperationsFiles::processEncryptedFile(passwordsFilePath, m_mainWindow->user_Key,
                                                 [&pattern](QString& content) -> bool {
                                                     // Replace the password entry (including the <Password> tag) with an empty string
                                                     content.replace(pattern, "");
                                                     return true;
                                                 }
                                                 );
}

bool Operations_PasswordManager::DeleteAllAssociatedPasswords(const QString &value, const QString &field)
{
    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Validate the password file
    if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
        qWarning() << "Password file failed validation check: " << passwordsFilePath;
        return false;
    }

    // Check if the passwords file exists
    if (!QFileInfo::exists(passwordsFilePath)) {
        return false; // No passwords file to modify
    }

    // Use the processEncryptedFile function to modify the password file
    return OperationsFiles::processEncryptedFile(passwordsFilePath, m_mainWindow->user_Key,
                                                 [&value, &field](QString& content) -> bool {
                                                     // Process the content to remove all matching passwords
                                                     QTextStream fileStream(&content);
                                                     QString line;
                                                     QString newFileContent;
                                                     bool inPasswordBlock = false;
                                                     bool skipCurrentBlock = false;
                                                     QString currentAccount, currentPassword, currentService;
                                                     QString currentBlock;

                                                     while (!fileStream.atEnd()) {
                                                         line = fileStream.readLine();

                                                         // Start of a password entry
                                                         if (line == "<Password>") {
                                                             inPasswordBlock = true;
                                                             skipCurrentBlock = false;
                                                             currentBlock = line + "\n";
                                                             currentAccount = currentPassword = currentService = "";
                                                             continue;
                                                         }

                                                         // End of a password entry (empty line)
                                                         if (inPasswordBlock && line.isEmpty()) {
                                                             inPasswordBlock = false;

                                                             // Check if this block matches our criteria to delete
                                                             bool shouldDelete = false;

                                                             if (field == "Password" && currentPassword == value) {
                                                                 shouldDelete = true;
                                                             } else if (field == "Account" && currentAccount == value) {
                                                                 shouldDelete = true;
                                                             } else if (field == "Service" && currentService == value) {
                                                                 shouldDelete = true;
                                                             }

                                                             // Only add to new content if we're not deleting this block
                                                             if (!shouldDelete) {
                                                                 newFileContent += currentBlock + "\n";
                                                             }
                                                             continue;
                                                         }

                                                         // Inside a password block
                                                         if (inPasswordBlock) {
                                                             currentBlock += line + "\n";

                                                             if (line.startsWith("Account: ")) {
                                                                 currentAccount = line.mid(9);  // Remove "Account: " prefix
                                                             } else if (line.startsWith("Password: ")) {
                                                                 currentPassword = line.mid(10);  // Remove "Password: " prefix
                                                             } else if (line.startsWith("Service: ")) {
                                                                 currentService = line.mid(9);  // Remove "Service: " prefix
                                                             }
                                                         }
                                                         else {
                                                             // Outside a password block, just add to new content
                                                             newFileContent += line + "\n";
                                                         }
                                                     }

                                                     // Update the content
                                                     content = newFileContent;
                                                     return true;
                                                 }
                                                 );
}

//-------------Context Menu----------//
//----PWDisplay---//
void Operations_PasswordManager::showContextMenu_PWDisplay(const QPoint &pos)
{
    // Get the table widget item at the clicked position
    QTableWidgetItem *item = m_mainWindow->ui->tableWidget_PWDisplay->itemAt(pos);
    if (!item) {
        return; // No item at this position
    }

    // Create menu
    QMenu contextMenu(m_mainWindow);

    // Add menu actions
    QAction *deleteAction = contextMenu.addAction("Delete Password");
    QAction *editAction = contextMenu.addAction("Modify Password");
    QAction *copyAction = contextMenu.addAction("Copy to Clipboard");

    // Get the row of the clicked item
    int row = item->row();

    // Store row and column in QObject properties for use in slot functions
    deleteAction->setProperty("row", row);
    editAction->setProperty("row", row);
    copyAction->setProperty("row", row);
    copyAction->setProperty("column", item->column());

    // Connect actions to slots
    connect(deleteAction, &QAction::triggered, this, &Operations_PasswordManager::onDeletePasswordClicked);
    connect(editAction, &QAction::triggered, this, &Operations_PasswordManager::onEditPasswordClicked);
    connect(copyAction, &QAction::triggered, this, &Operations_PasswordManager::onCopyToClipboardClicked);

    // Show the menu at the cursor position
    contextMenu.exec(m_mainWindow->ui->tableWidget_PWDisplay->mapToGlobal(pos));
}

void Operations_PasswordManager::onDeletePasswordClicked()
{
    // Get the sender action
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    // Get the row from the action's property
    int row = action->property("row").toInt();

    // Get current sorting method
    QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Get account, password, and service based on the sorting method
    QString account, password, service;

    if (sortingMethod == "Password") {
        password = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0)->text();
        account = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 1)->text();
        service = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 2)->text();
    } else if (sortingMethod == "Account") {
        account = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0)->text();
        password = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 1)->text();
        service = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 2)->text();
    } else if (sortingMethod == "Service") {
        service = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0)->text();
        account = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 1)->text();
        password = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 2)->text();
    }

    // Create a detailed message showing the password details
    QString detailedMessage = "Are you sure you want to delete this password?\n\n"
                              "Account: " + account + "\n"
                                          "Password: " + password + "\n"
                                           "Service: " + service;

    // Confirm deletion with the user
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(m_mainWindow, "Confirm Deletion",
                                  detailedMessage,
                                  QMessageBox::Yes|QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Store the current selection from the list widget
        QString currentListSelection;
        if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
            currentListSelection = m_mainWindow->ui->listWidget_PWList->currentItem()->text();
        }

        // Delete the password
        if (DeletePassword(account, password, service)) {
            // Successfully deleted, update UI

            // Update list widget
            SetupPWList(sortingMethod);

            // Check if the current selection still exists in the list
            bool selectionExists = false;
            for (int i = 0; i < m_mainWindow->ui->listWidget_PWList->count(); ++i) {
                if (m_mainWindow->ui->listWidget_PWList->item(i)->text() == currentListSelection) {
                    m_mainWindow->ui->listWidget_PWList->setCurrentRow(i);
                    selectionExists = true;
                    break;
                }
            }

            if (selectionExists) {
                // Trigger a click on the current item to refresh the table
                if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
                    on_PWListItemClicked(m_mainWindow->ui->listWidget_PWList->currentItem());
                }
            } else {
                // Reset the display
                SetupPWDisplay(sortingMethod);
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Delete Failed",
                                  "Failed to delete the password. Please try again.");
        }
    }
}

void Operations_PasswordManager::onCopyToClipboardClicked()
{
    // Get the sender action
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    // Get the row and column from action properties
    int row = action->property("row").toInt();
    int column = action->property("column").toInt();

    // Get the text from the cell
    QTableWidgetItem *item = m_mainWindow->ui->tableWidget_PWDisplay->item(row, column);
    if (item) {
        // Get the text to copy
        QString textToCopy = item->text();

        // If this is a masked password, get the real password from user data
        if (m_mainWindow->setting_PWMan_HidePasswords && textToCopy == "••••••••") {
            textToCopy = item->data(Qt::UserRole + 10).toString();
        }

        // Copy to clipboard
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(textToCopy);

        // Determine if this is a password column based on the current sorting method
        bool isPasswordColumn = false;
        QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

        if ((sortingMethod == "Password" && column == 0) ||
            (sortingMethod == "Account" && column == 1) ||
            (sortingMethod == "Service" && column == 2)) {
            isPasswordColumn = true;
        }

        // Only start timer if we're copying a password
        if (isPasswordColumn) {
            startClipboardClearTimer();
        }

        // Display message (masking password if hide option is on)
        QString displayText = textToCopy;
        if (isPasswordColumn && m_mainWindow->setting_PWMan_HidePasswords) {
            displayText = "••••••••";
        }

        QString statusMessage = "Copied to clipboard: " + displayText;
        if (isPasswordColumn) {
            statusMessage += " | Clipboard will clear in 30 seconds.";
        }

        m_mainWindow->statusBar()->showMessage(statusMessage, 2000);
    }
}

void Operations_PasswordManager::onEditPasswordClicked()
{
    // Get the sender action
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    // Get the row from the action's property
    int row = action->property("row").toInt();

    // Get current sorting method
    QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Get account, password, and service based on the sorting method
    QString account, password, service;

    if (sortingMethod == "Password") {
        password = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0)->text();
        account = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 1)->text();
        service = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 2)->text();
    } else if (sortingMethod == "Account") {
        account = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0)->text();
        password = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 1)->text();
        service = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 2)->text();
    } else if (sortingMethod == "Service") {
        service = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 0)->text();
        account = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 1)->text();
        password = m_mainWindow->ui->tableWidget_PWDisplay->item(row, 2)->text();
    }

    // Create and show the edit dialog
    QDialog dialog;
    Ui::PasswordManager_AddPassword ui;
    ui.setupUi(&dialog);

    // Change button text from "Add Password" to "Modify Password"
    ui.pushButton_AddPW->setText("Modify Password");

    // Set dialog title
    dialog.setWindowTitle("Edit Password");

    // Pre-fill the form with existing data
    ui.lineEdit_AccountName->setText(account);
    ui.lineEdit_Password->setText(password);
    ui.lineEdit_Service->setText(service);

    // Clear error display
    ui.label_ErrorDisplay->clear();

    // Prevent whitespace in lineEdit_Password
    QRegularExpression noWhitespace("[^\\s]*"); // Matches any non-whitespace characters
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, &dialog);
    ui.lineEdit_Password->setValidator(validator);

    // Dialog Logic
    QObject::connect(ui.pushButton_AddPW, &QPushButton::clicked, [&]() // if modify button is pushed
                     {
                         // Check for forbidden string "<Password>" in any field
                         if (ui.lineEdit_AccountName->text() == "<Password>" ||
                             ui.lineEdit_Password->text() == "<Password>" ||
                             ui.lineEdit_Service->text() == "<Password>") {
                             ui.label_ErrorDisplay->setText("The text \"<Password>\" is not allowed in any field.");
                             return;
                         }

                         // Only validate account name if not empty (it's now optional)
                         if (!ui.lineEdit_AccountName->text().isEmpty()) {
                             InputValidation::ValidationResult usernameResult =
                                 InputValidation::validateInput(ui.lineEdit_AccountName->text(), InputValidation::InputType::Line);
                             if (!usernameResult.isValid) {
                                 ui.label_ErrorDisplay->setText(usernameResult.errorMessage);
                                 return;
                             }
                         }

                         // Only validate service name if not empty (still optional)
                         if (!ui.lineEdit_Service->text().isEmpty()) {
                             InputValidation::ValidationResult serviceResult =
                                 InputValidation::validateInput(ui.lineEdit_Service->text(), InputValidation::InputType::Line);
                             if (!serviceResult.isValid) {
                                 ui.label_ErrorDisplay->setText(serviceResult.errorMessage);
                                 return;
                             }
                         }

                         // Validate password
                         if (ui.lineEdit_Password->text().isEmpty()) {
                             ui.label_ErrorDisplay->setText("Password field is empty.");
                             return;
                         } else {
                             InputValidation::ValidationResult passwordResult =
                                 InputValidation::validateInput(ui.lineEdit_Password->text(), InputValidation::InputType::Line);
                             if (!passwordResult.isValid) {
                                 ui.label_ErrorDisplay->setText(passwordResult.errorMessage);
                                 return;
                             }
                         }

                         dialog.accept(); // Close dialog
                     });

    QObject::connect(ui.pushButton_Cancel, &QPushButton::clicked, [&]() // if cancel button is pushed
                     {
                         dialog.reject(); // Close dialog
                     });

    if (dialog.exec() == QDialog::Accepted) {
        // Get new values from dialog
        QString newAccount = ui.lineEdit_AccountName->text();
        QString newPassword = ui.lineEdit_Password->text();
        QString newService = ui.lineEdit_Service->text();

        // Modify the password
        ModifyPassword(account, password, service, newAccount, newPassword, newService);
    }
}

//----PWList---//
void Operations_PasswordManager::showContextMenu_PWList(const QPoint &pos)
{
    // Get the list widget item at the clicked position
    QListWidgetItem *item = m_mainWindow->ui->listWidget_PWList->itemAt(pos);
    if (!item) {
        return; // No item at this position
    }

    // Get the value and current sorting method
    QString selectedValue = item->text();
    QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Create menu
    QMenu contextMenu(m_mainWindow);

    // Create action with dynamic text based on sorting method
    QString actionText;
    if (sortingMethod == "Password") {
        actionText = "Delete All Passwords Associated with \"" + selectedValue + "\"";
    } else if (sortingMethod == "Account") {
        actionText = "Delete All Passwords Associated with \"" + selectedValue + "\"";
    } else if (sortingMethod == "Service") {
        actionText = "Delete All Passwords Associated with \"" + selectedValue + "\"";
    }

    QAction *deleteAction = contextMenu.addAction(actionText);

    // Store value and field in QObject properties for use in slot functions
    deleteAction->setProperty("value", selectedValue);
    deleteAction->setProperty("field", sortingMethod);

    // Connect action to slot
    connect(deleteAction, &QAction::triggered, this, &Operations_PasswordManager::onDeleteAllAssociatedPasswordsClicked);

    // Show the menu at the cursor position
    contextMenu.exec(m_mainWindow->ui->listWidget_PWList->mapToGlobal(pos));
}

void Operations_PasswordManager::onDeleteAllAssociatedPasswordsClicked()
{
    // Get the sender action
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    // Get the value and field from the action's properties
    QString value = action->property("value").toString();
    QString field = action->property("field").toString();

    // Show warning confirmation dialog
    QString warningMessage = "Warning: This will delete ALL passwords associated with this " +
                             field.toLower() + ".\n\n" +
                             "Are you sure you want to delete all passwords with " +
                             field + ": \"" + value + "\"?";

    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(m_mainWindow, "Confirm Multiple Deletion",
                                 warningMessage,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No); // Default to No for safety

    if (reply == QMessageBox::Yes) {
        // Delete all matching passwords
        if (DeleteAllAssociatedPasswords(value, field)) {
            // Update UI after deletion
            SetupPWList(field);
            // Clear the table as the selected item might no longer exist
            SetupPWDisplay(field);
        } else {
            QMessageBox::critical(m_mainWindow, "Delete Failed",
                                  "Failed to delete the passwords. Please try again.");
        }
    }
}

//------------SLOTS------------//
void Operations_PasswordManager::onTableItemDoubleClicked(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }

    int column = item->column();
    int row = item->row();

    // Get text to copy (use original password if this is a masked password)
    QString textToCopy = item->text();
    if (m_mainWindow->setting_PWMan_HidePasswords && textToCopy == "••••••••") {
        textToCopy = item->data(Qt::UserRole + 10).toString();
    }

    // Copy the text to clipboard
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(textToCopy);

    // Determine if this is a password column based on the current sorting method
    bool isPasswordColumn = false;
    QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    if ((sortingMethod == "Password" && column == 0) ||
        (sortingMethod == "Account" && column == 1) ||
        (sortingMethod == "Service" && column == 2)) {
        isPasswordColumn = true;
    }

    // Only start timer if we're copying a password
    if (isPasswordColumn) {
        startClipboardClearTimer();
    }

    // Display message (masking password if hide option is on)
    QString displayText = textToCopy;
    if (isPasswordColumn && m_mainWindow->setting_PWMan_HidePasswords) {
        displayText = "••••••••";
    }

    QString statusMessage = "Copied to clipboard: " + displayText;
    if (isPasswordColumn) {
        statusMessage += " | Clipboard will clear in 30 seconds.";
    }

    m_mainWindow->statusBar()->showMessage(statusMessage, 2000);
}
void Operations_PasswordManager::on_SortByChanged(QString currentText)
{
    SetupPWList(currentText);
    SetupPWDisplay(currentText);

    // Check if there are any items in the list
    if (m_mainWindow->ui->listWidget_PWList->count() > 0) {
        // Select the first item
        m_mainWindow->ui->listWidget_PWList->setCurrentRow(0);

        // Get the first item
        QListWidgetItem* firstItem = m_mainWindow->ui->listWidget_PWList->item(0);

        // Simulate a click on the first item to load its details
        if (firstItem) {
            on_PWListItemClicked(firstItem);
        }
    }
}

void Operations_PasswordManager::on_AddPasswordClicked()
{
    QDialog dialog;
    Ui::PasswordManager_AddPassword ui;
    ui.setupUi(&dialog);
    ui.label_ErrorDisplay->clear();
    // Prevent whitespace in lineEdit_Password
    QRegularExpression noWhitespace("[^\\s]*"); // Matches any non-whitespace characters
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, &dialog);
    ui.lineEdit_Password->setValidator(validator);
    //Dialog Logic
    QObject::connect(ui.pushButton_AddPW, &QPushButton::clicked, [&]() // if add pw button is pushed
                     {
                         // Check for forbidden string "<Password>" in any field
                         if (ui.lineEdit_AccountName->text() == "<Password>" ||
                             ui.lineEdit_Password->text() == "<Password>" ||
                             ui.lineEdit_Service->text() == "<Password>") {
                             ui.label_ErrorDisplay->setText("The text \"<Password>\" is not allowed in any field.");
                             return;
                         }

                         // Only validate account name if not empty (it's now optional)
                         if(!ui.lineEdit_AccountName->text().isEmpty()) {
                             InputValidation::ValidationResult usernameResult =
                                 InputValidation::validateInput(ui.lineEdit_AccountName->text(), InputValidation::InputType::Line);
                             if(!usernameResult.isValid) {
                                 ui.label_ErrorDisplay->setText(usernameResult.errorMessage);
                                 return;
                             }
                         }

                         // Only validate service name if not empty (still optional)
                         if(!ui.lineEdit_Service->text().isEmpty()) {
                             InputValidation::ValidationResult serviceResult =
                                 InputValidation::validateInput(ui.lineEdit_Service->text(), InputValidation::InputType::Line);
                             if(!serviceResult.isValid) {
                                 ui.label_ErrorDisplay->setText(serviceResult.errorMessage);
                                 return;
                             }
                         }

                         // Password is still required
                         if(ui.lineEdit_Password->text().isEmpty()) {
                             ui.label_ErrorDisplay->setText("Password field is empty.");
                             return;
                         }
                         else {
                             InputValidation::ValidationResult passwordResult =
                                 InputValidation::validateInput(ui.lineEdit_Password->text(), InputValidation::InputType::Line);
                             if(!passwordResult.isValid) {
                                 ui.label_ErrorDisplay->setText(passwordResult.errorMessage);
                                 return;
                             }
                         }

                         dialog.accept(); // Close dialog
                     });
    QObject::connect(ui.pushButton_Cancel, &QPushButton::clicked, [&]() // if cancel button is pushed
                     {
                         dialog.reject(); // Close dialog
                     });

    if (dialog.exec() == QDialog::Accepted) // execute dialog, and wait for the user to accept or cancel
    {
        AddPassword(ui.lineEdit_AccountName->text(), ui.lineEdit_Password->text(), ui.lineEdit_Service->text());
    }
    else
    {

    }
}

void Operations_PasswordManager::on_PWListItemClicked(QListWidgetItem *item)
{
    if (item) {
        SetupPWDisplay(m_mainWindow->ui->comboBox_PWSortBy->currentText());
        UpdatePWDisplayForSelection(item->text());
    }
}

//------------Settings Implementation------------//

void Operations_PasswordManager::UpdatePasswordMasking()
{
    // Block signals during our updates to prevent recursion
    m_mainWindow->ui->comboBox_PWSortBy->blockSignals(true);

    // Get current selection before making any changes
    QString currentSortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    // Step 1: Rebuild the combo box with the appropriate options
    // Save current items
    QStringList items;
    for (int i = 0; i < m_mainWindow->ui->comboBox_PWSortBy->count(); i++) {
        QString item = m_mainWindow->ui->comboBox_PWSortBy->itemText(i);
        if (item != "Password" || !m_mainWindow->setting_PWMan_HidePasswords) {
            items << item;
        }
    }

    // If we're not hiding passwords and "Password" isn't in the list, add it
    if (!m_mainWindow->setting_PWMan_HidePasswords && !items.contains("Password")) {
        items.prepend("Password");
    }

    // Rebuild the combo box
    m_mainWindow->ui->comboBox_PWSortBy->clear();
    foreach (const QString &item, items) {
        m_mainWindow->ui->comboBox_PWSortBy->addItem(item);
    }

    // Select the closest available sorting method to what was previously selected
    int methodIndex = m_mainWindow->ui->comboBox_PWSortBy->findText(currentSortingMethod);
    if (methodIndex < 0 && m_mainWindow->ui->comboBox_PWSortBy->count() > 0) {
        // If the previous sorting method is no longer available, default to first item
        methodIndex = 0;
        currentSortingMethod = m_mainWindow->ui->comboBox_PWSortBy->itemText(0);
    }

    if (methodIndex >= 0) {
        m_mainWindow->ui->comboBox_PWSortBy->setCurrentIndex(methodIndex);
    }

    // Re-enable signals
    m_mainWindow->ui->comboBox_PWSortBy->blockSignals(false);

    // Now update the password display
    // (Rest of the function is unchanged)
    int passwordColumn = -1;
    if (currentSortingMethod == "Password") {
        passwordColumn = 0;
    } else if (currentSortingMethod == "Account") {
        passwordColumn = 1;
    } else if (currentSortingMethod == "Service") {
        passwordColumn = 2;
    }

    // Only update the table if we have a valid password column
    if (passwordColumn >= 0) {
        int rowCount = m_mainWindow->ui->tableWidget_PWDisplay->rowCount();
        for (int row = 0; row < rowCount; ++row) {
            QTableWidgetItem* item = m_mainWindow->ui->tableWidget_PWDisplay->item(row, passwordColumn);
            if (item) {
                // Store the actual password in user data if it's not already there
                if (!item->data(Qt::UserRole + 10).isValid()) {
                    item->setData(Qt::UserRole + 10, item->text());
                }

                // Update display text based on masking setting
                if (m_mainWindow->setting_PWMan_HidePasswords) {
                    item->setText("••••••••");
                } else {
                    // Restore the original password from user data
                    item->setText(item->data(Qt::UserRole + 10).toString());
                }
            }
        }
    }

    // Update the list widget if needed
    if (currentSortingMethod == "Password") {
        int listCount = m_mainWindow->ui->listWidget_PWList->count();
        for (int i = 0; i < listCount; ++i) {
            QListWidgetItem* item = m_mainWindow->ui->listWidget_PWList->item(i);
            if (item) {
                // Store the actual password in user data if it's not already there
                if (!item->data(Qt::UserRole + 10).isValid()) {
                    item->setData(Qt::UserRole + 10, item->text());
                }

                // Update display text based on masking setting
                if (m_mainWindow->setting_PWMan_HidePasswords) {
                    item->setText("••••••••");
                } else {
                    // Restore the original password from user data
                    item->setText(item->data(Qt::UserRole + 10).toString());
                }
            }
        }
    }
    QTimer::singleShot(25, this, [=]() {SetupPWList(currentSortingMethod, false);});
}


//--- clear clipboard --//
void Operations_PasswordManager::startClipboardClearTimer()
{
    m_clipboardTimer->start(30000); // 30 seconds = 30000 milliseconds
}

void Operations_PasswordManager::clearClipboard()
{
    // Clear the clipboard
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->clear();

    // Show status message that clipboard was cleared
    m_mainWindow->statusBar()->showMessage("Clipboard has been cleared for security", 2000);
}
