#include "operations_passwordmanager.h"
#include "CombinedDelegate.h"
#include "encryption/CryptoUtils.h"
#include "operations_files.h"
#include "operations.h"
#include "ui_mainwindow.h"
#include "ui_passwordmanager_addpassword.h"
#include "constants.h"
#include "../../Operations-Global/security/clipboard_security.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <QDialog>
#include <QRandomGenerator>
#include "inputvalidation.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Secure string clearing helper function
static void secureStringClear(QString& str) {
    if (str.isEmpty()) return;
    
    // Get the internal data
    QChar* data = str.data();
    int len = str.length();
    
#ifdef Q_OS_WIN
    // On Windows, use SecureZeroMemory to prevent compiler optimization
    // This ensures the memory is actually cleared
    SecureZeroMemory(data, len * sizeof(QChar));
#else
    // Fallback for non-Windows (though you specified Windows only)
    // Overwrite with zeros multiple times
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < len; ++i) {
            data[i] = QChar('\0');
        }
    }
#endif
    
    // Clear the string
    str.clear();
    
    // Force the string to release its memory
    str.squeeze();
}

// Helper function to convert SecureByteArray to QString for display/comparison
static QString secureToQString(const SecureByteArray& secure) {
    return QString::fromUtf8(secure.data(), secure.size());
}

// Helper function to convert QString to SecureByteArray
static SecureByteArray qStringToSecure(const QString& str) {
    QByteArray utf8 = str.toUtf8();
    SecureByteArray result;
    result.append(utf8);
    return result;
}

Operations_PasswordManager::Operations_PasswordManager(MainWindow* mainWindow)
    : m_mainWindow(mainWindow), m_currentLoadedValue(QString()), m_clipboardTimer(nullptr)
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

    // Connect search functionality
    connect(m_mainWindow->ui->lineEdit_PWSearch, &QLineEdit::textChanged,
            this, &Operations_PasswordManager::onSearchTextChanged);

    // Enable context menu policies
    m_mainWindow->ui->tableWidget_PWDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    m_mainWindow->ui->listWidget_PWList->setContextMenuPolicy(Qt::CustomContextMenu);

    // Initialize clipboard timer
    m_clipboardTimer = new SafeTimer(this, "Operations_PasswordManager");
    m_clipboardTimer->setSingleShot(true);

    // Initialize search placeholder text
    updateSearchPlaceholder();
}

Operations_PasswordManager::~Operations_PasswordManager()
{
    qDebug() << "Operations_PasswordManager: Destructor called - performing secure cleanup";
    
    // Stop and delete the clipboard timer
    if (m_clipboardTimer) {
        if (m_clipboardTimer->isActive()) {
            m_clipboardTimer->stop();
            // Clear clipboard immediately if timer was active
            clearClipboard();
        }
        delete m_clipboardTimer;
        m_clipboardTimer = nullptr;
    }
    
    // Clean up any cached passwords in widgets
    cleanupCachedPasswords();
    
    // Clear the current loaded value
    if (!m_currentLoadedValue.isEmpty()) {
        secureStringClear(m_currentLoadedValue);
    }
}

void Operations_PasswordManager::cleanupCachedPasswords()
{
    qDebug() << "Operations_PasswordManager: Cleaning up cached passwords";
    
    // Clean up passwords stored in table widget UserRole data
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->tableWidget_PWDisplay) {
        int rowCount = m_mainWindow->ui->tableWidget_PWDisplay->rowCount();
        int colCount = m_mainWindow->ui->tableWidget_PWDisplay->columnCount();
        
        for (int row = 0; row < rowCount; ++row) {
            for (int col = 0; col < colCount; ++col) {
                QTableWidgetItem* item = m_mainWindow->ui->tableWidget_PWDisplay->item(row, col);
                if (item) {
                    // Clear UserRole+10 (stored password)
                    if (item->data(Qt::UserRole + 10).isValid()) {
                        QString storedPassword = item->data(Qt::UserRole + 10).toString();
                        secureStringClear(storedPassword);
                        item->setData(Qt::UserRole + 10, QVariant());
                    }
                    // Clear UserRole+1 (password in metadata)
                    if (item->data(Qt::UserRole + 1).isValid()) {
                        QString storedPassword = item->data(Qt::UserRole + 1).toString();
                        secureStringClear(storedPassword);
                        item->setData(Qt::UserRole + 1, QVariant());
                    }
                }
            }
        }
    }
    
    // Clean up passwords stored in list widget UserRole data
    if (m_mainWindow && m_mainWindow->ui && m_mainWindow->ui->listWidget_PWList) {
        int listCount = m_mainWindow->ui->listWidget_PWList->count();
        
        for (int i = 0; i < listCount; ++i) {
            QListWidgetItem* item = m_mainWindow->ui->listWidget_PWList->item(i);
            if (item && item->data(Qt::UserRole + 10).isValid()) {
                QString storedPassword = item->data(Qt::UserRole + 10).toString();
                secureStringClear(storedPassword);
                item->setData(Qt::UserRole + 10, QVariant());
            }
        }
    }
    
    qDebug() << "Operations_PasswordManager: Cached passwords cleanup completed";
}

//-----------------Password Generation-----------------//
SecureByteArray Operations_PasswordManager::generatePassword()
{
    qDebug() << "Operations_PasswordManager: Generating password with length:" << m_passwordLength;
    
    // Character sets for password generation
    const QString lowercase = "abcdefghijklmnopqrstuvwxyz";
    const QString uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const QString numbers = "0123456789";
    const QString symbols = m_allowedSymbols;
    
    // Ensure we have at least the minimum required characters
    if (m_passwordLength < 4) {
        qWarning() << "Operations_PasswordManager: Password length too short, minimum is 4";
        return SecureByteArray();
    }
    
    SecureByteArray password;
    password.reserve(m_passwordLength);
    
    // Track how many of each type we've added
    int symbolCount = 0;
    int numberCount = 0;
    int uppercaseCount = 0;
    int lowercaseCount = 0;
    
    // Use cryptographically secure random generator
    QRandomGenerator* rng = QRandomGenerator::system();
    if (!rng) {
        qCritical() << "Operations_PasswordManager: Failed to get system random generator";
        return SecureByteArray();
    }
    
    // First, ensure we meet minimum requirements
    // Add one uppercase
    password.append(uppercase.at(rng->bounded(uppercase.length())).toLatin1());
    uppercaseCount++;
    
    // Add one number
    password.append(numbers.at(rng->bounded(numbers.length())).toLatin1());
    numberCount++;
    
    // Add one symbol
    password.append(symbols.at(rng->bounded(symbols.length())).toLatin1());
    symbolCount++;
    
    // Add one lowercase to ensure we have all types
    password.append(lowercase.at(rng->bounded(lowercase.length())).toLatin1());
    lowercaseCount++;
    
    // Calculate how many more characters we need
    int remainingChars = m_passwordLength - 4;
    
    // Calculate ideal distribution for remaining characters
    // We want roughly even distribution while respecting max symbols
    int remainingSymbols = qMin(m_maxSymbols - symbolCount, remainingChars / 4);
    int remainingNumbers = remainingChars / 3;
    int remainingUppercase = remainingChars / 3;
    int remainingLowercase = remainingChars - remainingSymbols - remainingNumbers - remainingUppercase;
    
    // Add remaining characters based on distribution
    for (int i = 0; i < remainingSymbols && password.size() < m_passwordLength; i++) {
        password.append(symbols.at(rng->bounded(symbols.length())).toLatin1());
        symbolCount++;
    }
    
    for (int i = 0; i < remainingNumbers && password.size() < m_passwordLength; i++) {
        password.append(numbers.at(rng->bounded(numbers.length())).toLatin1());
        numberCount++;
    }
    
    for (int i = 0; i < remainingUppercase && password.size() < m_passwordLength; i++) {
        password.append(uppercase.at(rng->bounded(uppercase.length())).toLatin1());
        uppercaseCount++;
    }
    
    // Fill the rest with lowercase
    while (password.size() < m_passwordLength) {
        password.append(lowercase.at(rng->bounded(lowercase.length())).toLatin1());
        lowercaseCount++;
    }
    
    // Shuffle the password to avoid predictable patterns
    SecureByteArray shuffledPassword;
    shuffledPassword.reserve(m_passwordLength);
    
    QList<char> chars;
    for (int i = 0; i < password.size(); i++) {
        chars.append(password[i]);
    }
    
    // Fisher-Yates shuffle for true randomness
    for (int i = chars.size() - 1; i > 0; i--) {
        int j = rng->bounded(i + 1);
        chars.swapItemsAt(i, j);
    }
    
    // Build the shuffled password while checking for consecutive characters
    for (int i = 0; i < chars.size(); i++) {
        shuffledPassword.append(chars[i]);
    }
    
    // Check for consecutive characters and fix them
    bool hasConsecutive = true;
    int maxAttempts = 100; // Prevent infinite loop
    int attempts = 0;
    
    while (hasConsecutive && attempts < maxAttempts) {
        hasConsecutive = false;
        
        for (int i = 0; i < shuffledPassword.size() - 1; i++) {
            if (shuffledPassword[i] == shuffledPassword[i + 1]) {
                hasConsecutive = true;
                
                // Swap with a random position that's not adjacent
                int swapPos;
                do {
                    swapPos = rng->bounded(shuffledPassword.size());
                } while (swapPos == i || swapPos == i + 1 || 
                         (swapPos > 0 && shuffledPassword[swapPos - 1] == shuffledPassword[i]) ||
                         (swapPos < shuffledPassword.size() - 1 && shuffledPassword[swapPos + 1] == shuffledPassword[i]));
                
                // Perform the swap
                char temp = shuffledPassword[i];
                QByteArray newData = shuffledPassword.data();
                newData[i] = newData[swapPos];
                newData[swapPos] = temp;
                shuffledPassword.setData(newData);
                
                break; // Check from beginning again
            }
        }
        
        attempts++;
    }
    
    if (attempts >= maxAttempts) {
        qWarning() << "Operations_PasswordManager: Could not eliminate all consecutive characters after" << maxAttempts << "attempts";
    }
    
    // Clear intermediate buffers before returning
    password.clear(); // SecureByteArray clears securely
    chars.clear(); // Clear the character list
    
    qDebug() << "Operations_PasswordManager: Generated password with"
             << "uppercase:" << uppercaseCount
             << "lowercase:" << lowercaseCount  
             << "numbers:" << numberCount
             << "symbols:" << symbolCount;
    
    return shuffledPassword;
}

//-----------------Password Display-----------------//
void Operations_PasswordManager::SetupPWDisplay(QString sortingMethod)
{
    // Temporarily disable sorting to prevent unwanted sorting during setup
    m_mainWindow->ui->tableWidget_PWDisplay->setSortingEnabled(false);

    // Use centralized cleanup function for cached passwords
    cleanupCachedPasswords();

    m_mainWindow->ui->tableWidget_PWDisplay->clear(); // first we clear the display
    m_mainWindow->ui->tableWidget_PWDisplay->setRowCount(0); // then, we remove all rows
    m_mainWindow->ui->tableWidget_PWDisplay->setColumnCount(3); // then, we set column count to 3 (updated from 2)

    // We set the value of the columns depending on the sorting method selected.
    if(sortingMethod == "Password")
    {
        m_mainWindow->ui->tableWidget_PWDisplay->setHorizontalHeaderLabels(QStringList() << "Password" << "Account" << "Service");
    }
    else if(sortingMethod == "Account")
    {
        m_mainWindow->ui->tableWidget_PWDisplay->setHorizontalHeaderLabels(QStringList() << "Account" << "Password" << "Service");
    }
    else if(sortingMethod == "Service")
    {
        m_mainWindow->ui->tableWidget_PWDisplay->setHorizontalHeaderLabels(QStringList() << "Service" << "Account" << "Password");
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
    // Store the currently selected item's text before clearing the list
    QString currentSelection;
    if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
        currentSelection = m_mainWindow->ui->listWidget_PWList->currentItem()->text();
    }

    // Use centralized cleanup function for cached passwords before clearing
    cleanupCachedPasswords();

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
    
    // Track passwords for secure cleanup
    QStringList passwordsToClean;

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
                    passwordsToClean.append(password); // Track for cleanup
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
            
            // Clear password for this entry after processing
            if (!password.isEmpty()) {
                secureStringClear(password);
            }
        }
    }
    
    // Final cleanup of any remaining passwords
    for (QString& pwd : passwordsToClean) {
        secureStringClear(pwd);
    }
    passwordsToClean.clear();

    // Convert the set to a sorted list with case-insensitive sorting
    QStringList sortedValues = uniqueValues.values();
    std::sort(sortedValues.begin(), sortedValues.end(), 
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    // Add the sorted values to the list widget
    foreach (const QString &value, sortedValues) {
        m_mainWindow->ui->listWidget_PWList->addItem(value);
    }
    // After populating the list, restore the selection if possible
    if (!currentSelection.isEmpty()) {
        for (int i = 0; i < m_mainWindow->ui->listWidget_PWList->count(); ++i) {
            if (m_mainWindow->ui->listWidget_PWList->item(i)->text() == currentSelection) {
                m_mainWindow->ui->listWidget_PWList->setCurrentRow(i);
                break;
            }
        }
    }

    if (applyMasking) {
        UpdatePasswordMasking();
    }
}

// New Function: Update Search Placeholder Text
void Operations_PasswordManager::updateSearchPlaceholder()
{
    qDebug() << "Operations_PasswordManager: Updating search placeholder text";
    
    QString currentSortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();
    QString placeholderText;
    
    if (currentSortingMethod == "Password") {
        placeholderText = "Search passwords...";
    } else if (currentSortingMethod == "Account") {
        placeholderText = "Search accounts...";
    } else if (currentSortingMethod == "Service") {
        placeholderText = "Search services...";
    } else {
        placeholderText = "Search...";
    }
    
    m_mainWindow->ui->lineEdit_PWSearch->setPlaceholderText(placeholderText);
    qDebug() << "Operations_PasswordManager: Placeholder text set to:" << placeholderText;
}

// New Function: Filter Password List Based on Search Text
void Operations_PasswordManager::filterPWList(const QString& searchText)
{
    qDebug() << "Operations_PasswordManager: Filtering list with search text:" << searchText;
    
    int firstVisibleIndex = -1;
    int visibleCount = 0;
    int totalCount = m_mainWindow->ui->listWidget_PWList->count();
    
    // If search text is empty, show all items and select first one
    if (searchText.isEmpty()) {
        for (int i = 0; i < totalCount; ++i) {
            QListWidgetItem* item = m_mainWindow->ui->listWidget_PWList->item(i);
            if (item) {
                item->setHidden(false);
                if (firstVisibleIndex == -1) {
                    firstVisibleIndex = i;
                }
            }
        }
        qDebug() << "Operations_PasswordManager: Search text empty, showing all items";
        
        // When clearing search, always select the first item
        if (firstVisibleIndex >= 0) {
            qDebug() << "Operations_PasswordManager: Search cleared, auto-selecting first item at index:" << firstVisibleIndex;
            m_mainWindow->ui->listWidget_PWList->setCurrentRow(firstVisibleIndex);
            
            // Trigger the item click to load its details
            QListWidgetItem* firstItem = m_mainWindow->ui->listWidget_PWList->item(firstVisibleIndex);
            if (firstItem) {
                on_PWListItemClicked(firstItem);
            }
        }
    }
    else {
        // Filter items based on search text (case-insensitive)
        for (int i = 0; i < totalCount; ++i) {
            QListWidgetItem* item = m_mainWindow->ui->listWidget_PWList->item(i);
            if (item) {
                bool matches = item->text().contains(searchText, Qt::CaseInsensitive);
                item->setHidden(!matches);
                if (matches) {
                    visibleCount++;
                    if (firstVisibleIndex == -1) {
                        firstVisibleIndex = i;
                    }
                }
            }
        }
        qDebug() << "Operations_PasswordManager: Filter applied. Visible items:" << visibleCount << "of" << totalCount;
        
        // Auto-select the first visible item if there is one and nothing is selected
        // or if the currently selected item is now hidden
        bool currentSelectionHidden = false;
        QListWidgetItem* currentItem = m_mainWindow->ui->listWidget_PWList->currentItem();
        if (currentItem && currentItem->isHidden()) {
            currentSelectionHidden = true;
        }
        
        if (firstVisibleIndex >= 0 && (!currentItem || currentSelectionHidden)) {
            qDebug() << "Operations_PasswordManager: Auto-selecting first visible item at index:" << firstVisibleIndex;
            m_mainWindow->ui->listWidget_PWList->setCurrentRow(firstVisibleIndex);
            
            // Trigger the item click to load its details
            QListWidgetItem* firstItem = m_mainWindow->ui->listWidget_PWList->item(firstVisibleIndex);
            if (firstItem) {
                on_PWListItemClicked(firstItem);
            }
        }
        else if (firstVisibleIndex == -1) {
            // No visible items, clear the display
            qDebug() << "Operations_PasswordManager: No visible items after filtering, clearing display";
            m_currentLoadedValue.clear();
            SetupPWDisplay(m_mainWindow->ui->comboBox_PWSortBy->currentText());
        }
    }
}

// New Slot: Handle Search Text Changes
void Operations_PasswordManager::onSearchTextChanged(const QString& text)
{
    qDebug() << "Operations_PasswordManager: Search text changed to:" << text;
    filterPWList(text);
}

// Helper Function: Preserve and Reapply Search Filter
void Operations_PasswordManager::preserveAndReapplySearchFilter()
{
    QString currentSearchText = m_mainWindow->ui->lineEdit_PWSearch->text();
    if (!currentSearchText.isEmpty()) {
        qDebug() << "Operations_PasswordManager: Reapplying preserved search filter:" << currentSearchText;
        filterPWList(currentSearchText);
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
    
    // Track all passwords for secure cleanup
    QStringList passwordsToClean;

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
                    passwordsToClean.append(password); // Track for cleanup
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
            
            // Clear password for this entry
            if (!password.isEmpty() && password != "(None)") {
                secureStringClear(password);
            }
        }
    }
    
    // Secure cleanup of all tracked passwords
    for (QString& pwd : passwordsToClean) {
        secureStringClear(pwd);
    }
    passwordsToClean.clear();

    // Set default sort on the second column (index 1) in ascending order
    m_mainWindow->ui->tableWidget_PWDisplay->horizontalHeader()->setSortIndicator(1, Qt::AscendingOrder);
    m_mainWindow->ui->tableWidget_PWDisplay->sortItems(1, Qt::AscendingOrder);
    UpdatePasswordMasking();
}

void Operations_PasswordManager::AddPassword(QString account, const SecureByteArray& password, QString service)
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
            
            // Clear the current password after checking
            if (!currentPassword.isEmpty()) {
                secureStringClear(currentPassword);
            }
                }
                else if (line.startsWith("Account: ")) {
                    currentAccount = line.mid(9);  // Remove "Account: " prefix
                }
                else if (line.startsWith("Password: ")) {
                    // Clear any previous password before assigning new one
                    if (!currentPassword.isEmpty()) {
                        secureStringClear(currentPassword);
                    }
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
    QString passwordStr = secureToQString(password);
    QString newPasswordEntry = "<Password>\n"
                               "Account: " + account + "\n"
                                           "Password: " + passwordStr + "\n"
                                            "Service: " + service + "\n\n";
    // Clear the temporary password string
    secureStringClear(passwordStr);

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
        valueToFind = secureToQString(password);
    } else if (currentSortingMethod == "Account") {
        valueToFind = account;
    } else if (currentSortingMethod == "Service") {
        valueToFind = service;
    }

    // Update the password list
    SetupPWList(currentSortingMethod);
    
    // Reapply search filter after rebuilding the list
    preserveAndReapplySearchFilter();

    // Find and select the newly added item in the list
    for (int i = 0; i < m_mainWindow->ui->listWidget_PWList->count(); ++i) {
        if (m_mainWindow->ui->listWidget_PWList->item(i)->text() == valueToFind) {
            m_mainWindow->ui->listWidget_PWList->setCurrentRow(i);
            // Simulate a click on the item to load the table
            on_PWListItemClicked(m_mainWindow->ui->listWidget_PWList->item(i));
            break;
        }
    }
    
    // Clear valueToFind if it was a password
    if (currentSortingMethod == "Password") {
        secureStringClear(valueToFind);
    }
}

bool Operations_PasswordManager::ModifyPassword(const QString &oldAccount, const SecureByteArray &oldPassword, const QString &oldService,
                        const QString &newAccount, const SecureByteArray &newPassword, const QString &newService)
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

    // Convert passwords to strings for comparison (will be cleared after)
    QString oldPasswordStr = secureToQString(oldPassword);
    QString newPasswordStr = secureToQString(newPassword);
    
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
                                                                     if (currentAccount == oldAccount && currentPassword == oldPasswordStr && currentService == oldService) {
                                                                         oldPasswordFound = true;
                                                                         // Don't add this block to the new content
                                                                     }
                                                                     // Check if this matches the new password we want to add
                                                                     else if (currentAccount == modifiedNewAccount && currentPassword == newPasswordStr && currentService == modifiedNewService) {
                                                                         newPasswordExists = true;
                                                                         // Still add this block to the new content
                                                                         newFileContent += currentBlock + "\n";
                                                                     }
                                                                     else {
                                                                         // Add this block to the new content
                                                                         newFileContent += currentBlock + "\n";
                                                                     }
                                                                     
                                                                     // Clear the current password after processing
                                                                     if (!currentPassword.isEmpty()) {
                                                                         secureStringClear(currentPassword);
                                                                     }
                                                                     continue;
                                                                 }

                                                                 // Inside a password block
                                                                 if (inPasswordBlock) {
                                                                     currentBlock += line + "\n";

                                                                     if (line.startsWith("Account: ")) {
                                                                         currentAccount = line.mid(9);  // Remove "Account: " prefix
                                                                     } else if (line.startsWith("Password: ")) {
                                                                         // Clear any previous password before assigning new one
                                                                         if (!currentPassword.isEmpty()) {
                                                                             secureStringClear(currentPassword);
                                                                         }
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
                                                                                                                   "Password: " + newPasswordStr + "\n"
                                                                                                            "Service: " + modifiedNewService + "\n\n";
                                                                 newFileContent += newPasswordEntry;
                                                             }

                                                             // Update the content
                                                             content = newFileContent;
                                                             return true;
                                                         }
                                                         );

    // Clear the temporary password strings
    secureStringClear(oldPasswordStr);
    secureStringClear(newPasswordStr);
    
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
    
    // Reapply search filter after rebuilding the list
    preserveAndReapplySearchFilter();

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
        // Clear the current loaded value to force refresh
        m_currentLoadedValue.clear();
        // Trigger a click on the current item to refresh the table
        if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
            on_PWListItemClicked(m_mainWindow->ui->listWidget_PWList->currentItem());
        }
    } else {
        // Clear the current loaded value and reset the display
        m_currentLoadedValue.clear();
        SetupPWDisplay(currentSortingMethod);
    }

    return true;
}

bool Operations_PasswordManager::DeletePassword(const QString &account, const SecureByteArray &password, const QString &service)
{
    // Convert password to string for comparison
    QString passwordStr = secureToQString(password);
    
    // Construct the passwords directory path
    QString passwordsDir = "Data/" + m_mainWindow->user_Username + "/Passwords/";
    QString passwordsFilePath = passwordsDir + "passwords.txt";

    // Validate the password file
    if (!OperationsFiles::validateFilePath(passwordsFilePath, OperationsFiles::FileType::Password, m_mainWindow->user_Key)) {
        qWarning() << "Password file failed validation check: " << passwordsFilePath;
        secureStringClear(passwordStr);
        return false;
    }

    // Check if the passwords file exists
    if (!QFileInfo::exists(passwordsFilePath)) {
        secureStringClear(passwordStr);
        return false; // No passwords file to modify
    }

    // Read the file to check how many passwords it contains
    QString fileContent;
    if (!OperationsFiles::readEncryptedFile(passwordsFilePath, m_mainWindow->user_Key, fileContent)) {
        qDebug() << "Failed to read passwords file: " << passwordsFilePath;
        secureStringClear(passwordStr);
        return false;
    }

    // Count password entries
    int passwordCount = fileContent.count("<Password>");

    // Check if this is the only entry
    QRegularExpression pattern(
        "<Password>\\s*"
        "Account: " + QRegularExpression::escape(account) + "\\s*"
                                                "Password: " + QRegularExpression::escape(passwordStr) + "\\s*"
                                                 "Service: " + QRegularExpression::escape(service) + "\\s*\\n"
        );

    bool isLastEntry = (passwordCount == 1 && pattern.match(fileContent).hasMatch());

    if (isLastEntry) {
        // Delete the entire file if this is the last entry
        QFile file(passwordsFilePath);
        bool result = file.remove();
        secureStringClear(passwordStr);
        return result;
    } else {
        // Otherwise, use the processEncryptedFile function to modify the password file
        bool result = OperationsFiles::processEncryptedFile(passwordsFilePath, m_mainWindow->user_Key,
                                                     [&pattern](QString& content) -> bool {
                                                         // Replace the password entry with an empty string
                                                         content.replace(pattern, "");
                                                         return true;
                                                     });
        secureStringClear(passwordStr);
        return result;
    }
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

    // Read the file to check how many passwords it contains
    QString fileContent;
    if (!OperationsFiles::readEncryptedFile(passwordsFilePath, m_mainWindow->user_Key, fileContent)) {
        qDebug() << "Failed to read passwords file: " << passwordsFilePath;
        return false;
    }

    // Count total password entries
    int passwordCount = fileContent.count("<Password>");

    // Count passwords matching our criteria
    int matchingCount = 0;
    QTextStream fileStream(&fileContent);
    QString line;
    bool inPasswordBlock = false;
    QString currentAccount, currentPassword, currentService;

    while (!fileStream.atEnd()) {
        line = fileStream.readLine();

        if (line == "<Password>") {
            inPasswordBlock = true;
            currentAccount = currentPassword = currentService = "";
            continue;
        }

        if (inPasswordBlock && line.isEmpty()) {
            inPasswordBlock = false;

            // Check if this block matches our criteria
            bool shouldDelete = false;

            if (field == "Password" && currentPassword == value) {
                shouldDelete = true;
            } else if (field == "Account" && currentAccount == value) {
                shouldDelete = true;
            } else if (field == "Service" && currentService == value) {
                shouldDelete = true;
            }

            if (shouldDelete) {
                matchingCount++;
            }
            
            // Clear the current password after checking
            if (!currentPassword.isEmpty()) {
                secureStringClear(currentPassword);
            }
            continue;
        }

        if (inPasswordBlock) {
            if (line.startsWith("Account: ")) {
                currentAccount = line.mid(9);  // Remove "Account: " prefix
            } else if (line.startsWith("Password: ")) {
                // Clear any previous password before assigning new one
                if (!currentPassword.isEmpty()) {
                    secureStringClear(currentPassword);
                }
                currentPassword = line.mid(10);  // Remove "Password: " prefix
            } else if (line.startsWith("Service: ")) {
                currentService = line.mid(9);  // Remove "Service: " prefix
            }
        }
    }

    // Check if we're deleting all passwords
    if (matchingCount == passwordCount) {
        // Delete the entire file
        QFile file(passwordsFilePath);
        return file.remove();
    } else {
        // Otherwise, use the processEncryptedFile function to modify the password file
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
                                                                 
                                                                 // Clear the current password after processing
                                                                 if (!currentPassword.isEmpty()) {
                                                                     secureStringClear(currentPassword);
                                                                 }
                                                                 continue;
                                                             }

                                                             // Inside a password block
                                                             if (inPasswordBlock) {
                                                                 currentBlock += line + "\n";

                                                                 if (line.startsWith("Account: ")) {
                                                                     currentAccount = line.mid(9);  // Remove "Account: " prefix
                                                                 } else if (line.startsWith("Password: ")) {
                                                                     // Clear any previous password before assigning new one
                                                                     if (!currentPassword.isEmpty()) {
                                                                         secureStringClear(currentPassword);
                                                                     }
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
    // Check if passwords are hidden
    if (m_mainWindow->setting_PWMan_HidePasswords) {
        QMessageBox::information(m_mainWindow, "Operation Not Allowed",
                                 "Cannot modify or delete passwords when the option 'Hide Passwords' is activated in the settings menu.");
        return;
    }

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

        // Convert password to SecureByteArray and delete
        SecureByteArray passwordSecure = qStringToSecure(password);
        // Clear the QString password immediately
        secureStringClear(password);
        
        if (DeletePassword(account, passwordSecure, service)) {
            // Successfully deleted, update UI

            // Update list widget
            SetupPWList(sortingMethod);
            
            // Reapply search filter after rebuilding the list
            preserveAndReapplySearchFilter();

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
                // Clear the current loaded value to force refresh
                m_currentLoadedValue.clear();
                // Trigger a click on the current item to refresh the table
                if (m_mainWindow->ui->listWidget_PWList->currentItem()) {
                    on_PWListItemClicked(m_mainWindow->ui->listWidget_PWList->currentItem());
                }
            } else {
                // Clear the current loaded value and reset the display
                m_currentLoadedValue.clear();
                SetupPWDisplay(sortingMethod);
            }
        } else {
            QMessageBox::critical(m_mainWindow, "Delete Failed",
                                  "Failed to delete the password. Please try again.");
        }
    } else {
        // User cancelled, password already cleared
        secureStringClear(password);
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

        // Determine if this is a password column based on the current sorting method
        bool isPasswordColumn = false;
        QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

        if ((sortingMethod == "Password" && column == 0) ||
            (sortingMethod == "Account" && column == 1) ||
            (sortingMethod == "Service" && column == 2)) {
            isPasswordColumn = true;
        }

        // Use secure clipboard for passwords
        ClipboardSecurity::ClipboardResult clipResult;
        if (isPasswordColumn) {
            // Check for clipboard monitors and warn user
            ClipboardSecurity::MonitorInfo monitorInfo = ClipboardSecurity::ClipboardSecurityManager::detectClipboardMonitors();
            if (monitorInfo.detected) {
                QMessageBox::StandardButton reply = QMessageBox::warning(
                    m_mainWindow, 
                    "Clipboard Monitor Detected",
                    QString("Warning: %1\n\nDo you want to continue copying the password?").arg(monitorInfo.warning),
                    QMessageBox::Yes | QMessageBox::No
                );
                
                if (reply == QMessageBox::No) {
                    return;
                }
            }
            
            // Copy password securely
            clipResult = ClipboardSecurity::ClipboardSecurityManager::copyPasswordSecure(textToCopy);
            if (clipResult.success) {
                startClipboardClearTimer();
            }
        } else {
            // Regular copy for non-password fields
            clipResult = ClipboardSecurity::ClipboardSecurityManager::copyTextSecure(textToCopy, ClipboardSecurity::SecurityLevel::Normal);
        }

        // Check if copy was successful
        if (!clipResult.success) {
            QMessageBox::warning(m_mainWindow, "Clipboard Error", 
                               QString("Failed to copy to clipboard: %1").arg(clipResult.errorMessage));
            return;
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
    // Check if passwords are hidden
    if (m_mainWindow->setting_PWMan_HidePasswords) {
        QMessageBox::information(m_mainWindow, "Operation Not Allowed",
                                 "Cannot modify or delete passwords when the option 'Hide Passwords' is activated in the settings menu.");
        return;
    }

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
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, ui.lineEdit_Password);
    ui.lineEdit_Password->setValidator(validator);
    
    // Connect the Generate Password button
    QObject::connect(ui.pushButton_GenPassword, &QPushButton::clicked, [&]()
    {
        qDebug() << "Operations_PasswordManager: Generate Password button clicked in Edit dialog";
        
        // Clear any existing password first
        QString oldPassword = ui.lineEdit_Password->text();
        if (!oldPassword.isEmpty()) {
            ui.lineEdit_Password->clear();
            secureStringClear(oldPassword);
        }
        
        SecureByteArray generatedPassword = generatePassword();
        if (!generatedPassword.isEmpty()) {
            QString generatedPasswordStr = secureToQString(generatedPassword);
            ui.lineEdit_Password->setText(generatedPasswordStr);
            ui.label_ErrorDisplay->setText("Password generated successfully!");
            ui.label_ErrorDisplay->setStyleSheet("color: green;");
            qDebug() << "Operations_PasswordManager: Password set in field";
            // Clear the generated password string from memory after setting
            secureStringClear(generatedPasswordStr);
        } else {
            ui.label_ErrorDisplay->setText("Failed to generate password.");
            ui.label_ErrorDisplay->setStyleSheet("color: red;");
            qDebug() << "Operations_PasswordManager: Password generation failed";
        }
    });

    // Dialog Logic
    QObject::connect(ui.pushButton_AddPW, &QPushButton::clicked, [&]() // if modify button is pushed
                     {
                         // Check for forbidden string "<Password>" in any field
                         if (ui.lineEdit_AccountName->text() == "<Password>" ||
                             ui.lineEdit_Password->text() == "<Password>" ||
                             ui.lineEdit_Service->text() == "<Password>") {
                             ui.label_ErrorDisplay->setText("The text \"<Password>\" is not allowed in any field.");
                             // Clear password field for security
                             QString tempPwd = ui.lineEdit_Password->text();
                             ui.lineEdit_Password->clear();
                             secureStringClear(tempPwd);
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
        QString newPasswordStr = ui.lineEdit_Password->text();
        QString newService = ui.lineEdit_Service->text();
        
        // Clear the dialog fields
        ui.lineEdit_Password->clear();
        ui.lineEdit_AccountName->clear();
        ui.lineEdit_Service->clear();
        
        // Convert passwords to SecureByteArray
        SecureByteArray oldPasswordSecure = qStringToSecure(password);
        SecureByteArray newPasswordSecure = qStringToSecure(newPasswordStr);
        
        // Clear the QString passwords
        secureStringClear(password);
        secureStringClear(newPasswordStr);

        // Modify the password
        ModifyPassword(account, oldPasswordSecure, service, newAccount, newPasswordSecure, newService);
    } else {
        // Clear the dialog fields even if cancelled
        ui.lineEdit_Password->clear();
        ui.lineEdit_AccountName->clear();
        ui.lineEdit_Service->clear();
        
        // Clear the local copy of password
        secureStringClear(password);
    }
}

//----PWList---//
void Operations_PasswordManager::showContextMenu_PWList(const QPoint &pos)
{
    // Don't show context menu if passwords are hidden
    if (m_mainWindow->setting_PWMan_HidePasswords) {
        return; // Silently return without showing menu
    }
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
            // Clear the current loaded value since we're deleting items
            m_currentLoadedValue.clear();
            
            // Update UI after deletion
            SetupPWList(field);
            
            // Reapply search filter after rebuilding the list
            preserveAndReapplySearchFilter();
            
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

    // Determine if this is a password column based on the current sorting method
    bool isPasswordColumn = false;
    QString sortingMethod = m_mainWindow->ui->comboBox_PWSortBy->currentText();

    if ((sortingMethod == "Password" && column == 0) ||
        (sortingMethod == "Account" && column == 1) ||
        (sortingMethod == "Service" && column == 2)) {
        isPasswordColumn = true;
    }

    // Use secure clipboard for passwords
    ClipboardSecurity::ClipboardResult clipResult;
    if (isPasswordColumn) {
        // Check for clipboard monitors and warn user
        ClipboardSecurity::MonitorInfo monitorInfo = ClipboardSecurity::ClipboardSecurityManager::detectClipboardMonitors();
        if (monitorInfo.detected) {
            QMessageBox::StandardButton reply = QMessageBox::warning(
                m_mainWindow, 
                "Clipboard Monitor Detected",
                QString("Warning: %1\n\nDo you want to continue copying the password?").arg(monitorInfo.warning),
                QMessageBox::Yes | QMessageBox::No
            );
            
            if (reply == QMessageBox::No) {
                return;
            }
        }
        
        // Copy password securely (excludes from Windows clipboard history)
        clipResult = ClipboardSecurity::ClipboardSecurityManager::copyPasswordSecure(textToCopy);
        if (clipResult.success) {
            startClipboardClearTimer();
        }
    } else {
        // Regular copy for non-password fields
        clipResult = ClipboardSecurity::ClipboardSecurityManager::copyTextSecure(textToCopy, ClipboardSecurity::SecurityLevel::Normal);
    }

    // Check if copy was successful
    if (!clipResult.success) {
        QMessageBox::warning(m_mainWindow, "Clipboard Error", 
                           QString("Failed to copy to clipboard: %1").arg(clipResult.errorMessage));
        return;
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
    qDebug() << "Operations_PasswordManager: Sorting method changed to:" << currentText;
    
    // Clear the currently loaded value since we're changing sorting method
    m_currentLoadedValue.clear();
    qDebug() << "Operations_PasswordManager: Cleared currently loaded value due to sorting change";
    
    // Store current search text before making changes
    QString preservedSearchText = m_mainWindow->ui->lineEdit_PWSearch->text();
    qDebug() << "Operations_PasswordManager: Preserving search text:" << preservedSearchText;
    
    // Update placeholder text for new sorting method
    updateSearchPlaceholder();
    
    // Update the password list and display
    SetupPWList(currentText);
    SetupPWDisplay(currentText);
    
    // Reapply the preserved search filter
    if (!preservedSearchText.isEmpty()) {
        m_mainWindow->ui->lineEdit_PWSearch->setText(preservedSearchText);
        filterPWList(preservedSearchText);
        qDebug() << "Operations_PasswordManager: Search filter reapplied after sorting change";
    }
    else {
        // No search filter active, select the first item if available
        if (m_mainWindow->ui->listWidget_PWList->count() > 0) {
            // Find the first non-hidden item
            int firstVisibleIndex = -1;
            for (int i = 0; i < m_mainWindow->ui->listWidget_PWList->count(); ++i) {
                QListWidgetItem* item = m_mainWindow->ui->listWidget_PWList->item(i);
                if (item && !item->isHidden()) {
                    firstVisibleIndex = i;
                    break;
                }
            }
            
            if (firstVisibleIndex >= 0) {
                qDebug() << "Operations_PasswordManager: Selecting first visible item at index:" << firstVisibleIndex;
                m_mainWindow->ui->listWidget_PWList->setCurrentRow(firstVisibleIndex);
                
                // Get the first visible item
                QListWidgetItem* firstItem = m_mainWindow->ui->listWidget_PWList->item(firstVisibleIndex);
                
                // Load its details
                if (firstItem) {
                    on_PWListItemClicked(firstItem);
                }
            }
        }
    }
    
    qDebug() << "Operations_PasswordManager: UI updated for new sorting method";
}

void Operations_PasswordManager::on_AddPasswordClicked()
{
    QDialog dialog;
    Ui::PasswordManager_AddPassword ui;
    ui.setupUi(&dialog);
    ui.label_ErrorDisplay->clear();
    // Prevent whitespace in lineEdit_Password
    QRegularExpression noWhitespace("[^\\s]*"); // Matches any non-whitespace characters
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(noWhitespace, ui.lineEdit_Password);
    ui.lineEdit_Password->setValidator(validator);
    
    // Connect the Generate Password button
    QObject::connect(ui.pushButton_GenPassword, &QPushButton::clicked, [&]()
    {
        qDebug() << "Operations_PasswordManager: Generate Password button clicked";
        
        // Clear any existing password first
        QString oldPassword = ui.lineEdit_Password->text();
        if (!oldPassword.isEmpty()) {
            ui.lineEdit_Password->clear();
            secureStringClear(oldPassword);
        }
        
        SecureByteArray generatedPassword = generatePassword();
        if (!generatedPassword.isEmpty()) {
            QString generatedPasswordStr = secureToQString(generatedPassword);
            ui.lineEdit_Password->setText(generatedPasswordStr);
            ui.label_ErrorDisplay->setText("Password generated successfully!");
            ui.label_ErrorDisplay->setStyleSheet("color: green;");
            qDebug() << "Operations_PasswordManager: Password set in field";
            // Clear the generated password string from memory after setting
            secureStringClear(generatedPasswordStr);
        } else {
            ui.label_ErrorDisplay->setText("Failed to generate password.");
            ui.label_ErrorDisplay->setStyleSheet("color: red;");
            qDebug() << "Operations_PasswordManager: Password generation failed";
        }
    });
    
    //Dialog Logic
    QObject::connect(ui.pushButton_AddPW, &QPushButton::clicked, [&]() // if add pw button is pushed
                     {
                         // Check for forbidden string "<Password>" in any field
                         if (ui.lineEdit_AccountName->text() == "<Password>" ||
                             ui.lineEdit_Password->text() == "<Password>" ||
                             ui.lineEdit_Service->text() == "<Password>") {
                             ui.label_ErrorDisplay->setText("The text \"<Password>\" is not allowed in any field.");
                             // Clear password field for security
                             QString tempPwd = ui.lineEdit_Password->text();
                             ui.lineEdit_Password->clear();
                             secureStringClear(tempPwd);
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
        // Store password temporarily
        QString tempPasswordStr = ui.lineEdit_Password->text();
        QString tempAccount = ui.lineEdit_AccountName->text();
        QString tempService = ui.lineEdit_Service->text();
        
        // Clear the dialog fields
        ui.lineEdit_Password->clear();
        ui.lineEdit_AccountName->clear();
        ui.lineEdit_Service->clear();
        
        // Convert password to SecureByteArray
        SecureByteArray tempPassword = qStringToSecure(tempPasswordStr);
        
        // Clear the QString password
        secureStringClear(tempPasswordStr);
        
        // Add the password
        AddPassword(tempAccount, tempPassword, tempService);
    }
    else
    {
        // Clear the dialog fields even if cancelled
        ui.lineEdit_Password->clear();
        ui.lineEdit_AccountName->clear();
        ui.lineEdit_Service->clear();
    }
}

void Operations_PasswordManager::on_PWListItemClicked(QListWidgetItem *item)
{
    if (item) {
        QString clickedValue = item->text();
        
        // Check if this item is already loaded to prevent unnecessary refreshes
        if (m_currentLoadedValue == clickedValue) {
            qDebug() << "Operations_PasswordManager: Item already loaded, skipping refresh:" << clickedValue;
            return;
        }
        
        qDebug() << "Operations_PasswordManager: List item clicked, loading:" << clickedValue;
        qDebug() << "Operations_PasswordManager: Previous loaded value was:" << m_currentLoadedValue;
        
        // Update the currently loaded value
        m_currentLoadedValue = clickedValue;
        
        // Store the current search text before updating display
        QString currentSearchText = m_mainWindow->ui->lineEdit_PWSearch->text();
        qDebug() << "Operations_PasswordManager: Current search text:" << currentSearchText;
        
        SetupPWDisplay(m_mainWindow->ui->comboBox_PWSortBy->currentText());
        UpdatePWDisplayForSelection(item->text());
        
        // Reapply the search filter if there was search text
        if (!currentSearchText.isEmpty()) {
            qDebug() << "Operations_PasswordManager: Reapplying search filter after click";
            filterPWList(currentSearchText);
        }
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
                    QString originalPassword = item->data(Qt::UserRole + 10).toString();
                    item->setText(originalPassword);
                    // Clear the stored password data when unmasking
                    item->setData(Qt::UserRole + 10, QVariant());
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
                    QString originalPassword = item->data(Qt::UserRole + 10).toString();
                    item->setText(originalPassword);
                    // Clear the stored password data when unmasking
                    item->setData(Qt::UserRole + 10, QVariant());
                }
            }
        }
    }
    SafeTimer::singleShot(25, this, [this, currentSortingMethod]() {
        SetupPWList(currentSortingMethod, false);
        preserveAndReapplySearchFilter();
    }, "Operations_PasswordManager");
}


//--- clear clipboard --//
void Operations_PasswordManager::startClipboardClearTimer()
{
    qDebug() << "Operations_PasswordManager: Starting clipboard clear timer (30 seconds)";

    if (m_clipboardTimer) {
        // Stop any existing timer
        if (m_clipboardTimer->isActive()) {
            m_clipboardTimer->stop();
        }

        // Start the timer with callback
        m_clipboardTimer->setInterval(30000); // 30 seconds
        m_clipboardTimer->start([this]() {
            clearClipboard();
        });
    }
}

void Operations_PasswordManager::clearClipboard()
{
    qDebug() << "Operations_PasswordManager: Clearing clipboard for security";

    // Use the enhanced secure clipboard clearing
    bool cleared = ClipboardSecurity::ClipboardSecurityManager::clearClipboardSecure();
    
    if (!cleared) {
        qWarning() << "Operations_PasswordManager: Failed to clear clipboard securely";
        // Fallback to basic clear
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->clear();
    }

    // Stop the timer if it's still active
    if (m_clipboardTimer && m_clipboardTimer->isActive()) {
        m_clipboardTimer->stop();
    }

    m_mainWindow->statusBar()->showMessage("Clipboard cleared for security.", 2000);
}
