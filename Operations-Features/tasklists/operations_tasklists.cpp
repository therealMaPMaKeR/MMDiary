#include "operations_tasklists.h"
#include "CombinedDelegate.h"
#include "encryption/CryptoUtils.h"
#include "operations.h"
#include "inputvalidation.h"
#include "ui_mainwindow.h"
#include "../../constants.h"
#include "ui_tasklists_addtask.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMap>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "operations_files.h"

// Security: Centralized escaping functions for task data
namespace TaskDataSecurity {
    QString escapeTaskField(const QString& input) {
        QString escaped = input;
        // Escape in specific order to prevent double-escaping
        escaped.replace("\\", "\\\\");  // Backslash first
        escaped.replace("|", "\\|");      // Then pipe
        escaped.replace("\n", "\\n");    // Newline
        escaped.replace("\r", "\\r");    // Carriage return
        escaped.replace("\t", "\\t");    // Tab
        escaped.replace("\0", "");        // Remove null bytes
        return escaped;
    }
    
    QString unescapeTaskField(const QString& input) {
        QString unescaped = input;
        // Unescape in reverse order
        unescaped.replace("\\t", "\t");
        unescaped.replace("\\r", "\r");
        unescaped.replace("\\n", "\n");
        unescaped.replace("\\|", "|");
        unescaped.replace("\\\\", "\\");
        return unescaped;
    }
    
    QString sanitizeFileName(const QString& input) {
        QString sanitized = input;
        // Remove null bytes first
        sanitized.remove(QChar('\0'));
        // Remove control characters (0x00-0x1F, 0x7F)
        sanitized.remove(QRegularExpression("[\\x00-\\x1F\\x7F]"));
        // Replace dangerous path characters
        sanitized.replace(QRegularExpression("[\\\\/:*?\"<>|\\.\\.]"), "_");
        // Remove leading/trailing dots and spaces (Windows specific)
        sanitized = sanitized.trimmed();
        sanitized.remove(QRegularExpression("^\\.+|\\s+$|\\.$"));
        // Limit length to prevent path overflow
        if (sanitized.length() > 200) {
            sanitized = sanitized.left(200);
        }
        // If empty after sanitization, use default
        if (sanitized.isEmpty()) {
            sanitized = "unnamed_list";
        }
        return sanitized;
    }
    
    QString generateSecureTempFileName(const QString& baseName, const QString& tempDir) {
        // Generate unique temporary file name with random component
        QDateTime now = QDateTime::currentDateTime();
        qint64 timestamp = now.toMSecsSinceEpoch();
        quint32 randomValue = QRandomGenerator::global()->generate();
        
        QString sanitizedBase = sanitizeFileName(baseName);
        QString tempFileName = QString("%1_%2_%3_temp.txt")
            .arg(sanitizedBase)
            .arg(timestamp)
            .arg(randomValue, 8, 16, QChar('0')); // 8-digit hex
        
        return QDir(tempDir).absoluteFilePath(tempFileName);
    }
    
    // Security: Clear sensitive string data from memory
    void secureStringClear(QString& str) {
        if (str.isEmpty()) return;
        
        // Get the internal data
        // Note: This works for non-shared QString instances
        QChar* data = str.data();
        int len = str.length();
        
#ifdef Q_OS_WIN
        // On Windows, use SecureZeroMemory to prevent compiler optimization
        // This ensures the memory is actually cleared
        SecureZeroMemory(data, len * sizeof(QChar));
#else
        // Fallback for non-Windows (though you specified Windows only)
        // Overwrite with zeros
        for (int i = 0; i < len; ++i) {
            data[i] = QChar('\0');
        }
        
        // Force multiple overwrites to prevent compiler optimization
        for (int i = 0; i < len; ++i) {
            data[i] = QChar(0x00);
        }
#endif
        
        // Clear the string
        str.clear();
        
        // Force the string to release its memory
        str.squeeze();
    }
    
    // Security: Clear QStringList data from memory
    void secureStringListClear(QStringList& list) {
        for (QString& str : list) {
            secureStringClear(str);
        }
        list.clear();
    }
    
    // Security: Validate task data structure
    bool validateTaskDataStructure(const QStringList& parts) {
        // Minimum required fields: Type|Name|Status|CompletionDate|CreationDate
        if (parts.size() < 5) {
            qWarning() << "Operations_TaskLists: Invalid task data - insufficient fields:" << parts.size();
            return false;
        }
        
        // Validate task type
        if (parts[0] != "Simple") {
            qWarning() << "Operations_TaskLists: Invalid task type:" << parts[0];
            return false;
        }
        
        // Validate task name (should not be empty after unescaping)
        QString taskName = unescapeTaskField(parts[1]);
        if (taskName.trimmed().isEmpty()) {
            qWarning() << "Operations_TaskLists: Invalid task name - empty or whitespace only";
            return false;
        }
        
        // Validate task name length
        if (taskName.length() > 255) {
            qWarning() << "Operations_TaskLists: Task name too long:" << taskName.length();
            return false;
        }
        
        // Validate status field (should be "0" or "1")
        if (parts[2] != "0" && parts[2] != "1") {
            qWarning() << "Operations_TaskLists: Invalid task status:" << parts[2];
            return false;
        }
        
        // If completed, should have a completion date
        if (parts[2] == "1" && parts[3].isEmpty()) {
            qWarning() << "Operations_TaskLists: Completed task missing completion date";
            // Not a critical error, allow to continue
        }
        
        // Validate description if present
        if (parts.size() > 5 && parts[5].startsWith("DESC:")) {
            QString description = unescapeTaskField(parts[5].mid(5));
            if (description.length() > 10000) {
                qWarning() << "Operations_TaskLists: Task description too long:" << description.length();
                return false;
            }
        }
        
        return true;
    }
}

Operations_TaskLists::Operations_TaskLists(MainWindow* mainWindow)
    : m_mainWindow(mainWindow)
    , m_lastClickedWidget(nullptr)
    , m_lastClickedItem(nullptr)
{
    qDebug() << "Operations_TaskLists: Initializing";
    
    m_mainWindow->ui->listWidget_TaskList_List->setSortingEnabled(false);
    
    // Clear the table
    m_mainWindow->ui->tableWidget_TaskDetails->clear();
    m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
    m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);
    
    // Connect the context menu signal for the task list display
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QWidget::customContextMenuRequested,
            this, &Operations_TaskLists::showContextMenu_TaskListDisplay);
    
    // Enable context menu policy
    m_mainWindow->ui->listWidget_TaskListDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Set up description save timer
    m_descriptionSaveTimer = new QTimer(this);
    m_descriptionSaveTimer->setSingleShot(true);
    m_descriptionSaveTimer->setInterval(5000); // 5 seconds
    connect(m_descriptionSaveTimer, &QTimer::timeout, this, &Operations_TaskLists::SaveTaskDescription);
    
    // Install event filters
    m_mainWindow->ui->plainTextEdit_TaskDesc->installEventFilter(this);
    m_mainWindow->ui->listWidget_TaskListDisplay->installEventFilter(this);
    m_mainWindow->ui->tableWidget_TaskDetails->installEventFilter(this);
    
    // Connect the context menu signal for the task list list widget
    connect(m_mainWindow->ui->listWidget_TaskList_List, &QWidget::customContextMenuRequested,
            this, &Operations_TaskLists::showContextMenu_TaskListList);
    
    // Enable context menu policy for the task list list widget
    m_mainWindow->ui->listWidget_TaskList_List->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Install event filters for key press events
    m_mainWindow->ui->listWidget_TaskList_List->installEventFilter(this);
    m_mainWindow->ui->listWidget_TaskListDisplay->installEventFilter(this);
    
    // Connect item clicked signals to track the last clicked item
    connect(m_mainWindow->ui->listWidget_TaskList_List, &QListWidget::itemClicked,
            this, &Operations_TaskLists::onTaskListItemClicked);
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemClicked,
            this, &Operations_TaskLists::onTaskDisplayItemClicked);
    
    // Connect double-click signals
    connect(m_mainWindow->ui->listWidget_TaskList_List, &QListWidget::itemDoubleClicked,
            this, &Operations_TaskLists::onTaskListItemDoubleClicked);
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemDoubleClicked,
            this, &Operations_TaskLists::onTaskDisplayItemDoubleClicked);
    
    // Connect item changed signal for checkbox handling
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemChanged,
            this, [this](QListWidgetItem* item) {
                if (item) {
                    bool checked = (item->checkState() == Qt::Checked);
                    m_mainWindow->ui->listWidget_TaskListDisplay->blockSignals(true);
                    SetTaskStatus(checked, item);
                    m_mainWindow->ui->listWidget_TaskListDisplay->blockSignals(false);
                }
            });
    
    // Enable drag and drop for task display list widget
    m_mainWindow->ui->listWidget_TaskListDisplay->setDragEnabled(true);
    m_mainWindow->ui->listWidget_TaskListDisplay->setAcceptDrops(true);
    m_mainWindow->ui->listWidget_TaskListDisplay->setDropIndicatorShown(true);
    m_mainWindow->ui->listWidget_TaskListDisplay->setDragDropMode(QAbstractItemView::InternalMove);
    
    // Enable drag and drop for task list widget
    m_mainWindow->ui->listWidget_TaskList_List->setDragEnabled(true);
    m_mainWindow->ui->listWidget_TaskList_List->setAcceptDrops(true);
    m_mainWindow->ui->listWidget_TaskList_List->setDropIndicatorShown(true);
    m_mainWindow->ui->listWidget_TaskList_List->setDragDropMode(QAbstractItemView::InternalMove);
    
    // Connect drag and drop signals
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &qlist_TasklistDisplay::itemsReordered,
            this, [this]() {
                QTimer::singleShot(0, this, &Operations_TaskLists::EnforceTaskOrder);
            });
    
    connect(m_mainWindow->ui->listWidget_TaskList_List, &qlist_TasklistDisplay::itemsReordered,
            this, &Operations_TaskLists::SaveTasklistOrder);
    
    LoadTasklists();
}

Operations_TaskLists::~Operations_TaskLists()
{
    qDebug() << "Operations_TaskLists: Destructor called";
    
    // Remove event filters
    if (m_mainWindow && m_mainWindow->ui) {
        if (m_mainWindow->ui->plainTextEdit_TaskDesc) {
            m_mainWindow->ui->plainTextEdit_TaskDesc->removeEventFilter(this);
        }
        if (m_mainWindow->ui->listWidget_TaskListDisplay) {
            m_mainWindow->ui->listWidget_TaskListDisplay->removeEventFilter(this);
        }
        if (m_mainWindow->ui->tableWidget_TaskDetails) {
            m_mainWindow->ui->tableWidget_TaskDetails->removeEventFilter(this);
        }
        if (m_mainWindow->ui->listWidget_TaskList_List) {
            m_mainWindow->ui->listWidget_TaskList_List->removeEventFilter(this);
        }
    }
    
    // Delete the timer
    if (m_descriptionSaveTimer) {
        m_descriptionSaveTimer->stop();
        delete m_descriptionSaveTimer;
        m_descriptionSaveTimer = nullptr;
    }
}

//--------Event Filters and Helpers--------//
bool Operations_TaskLists::eventFilter(QObject* watched, QEvent* event)
{
    // Check for focus out event on the description text edit
    if (watched == m_mainWindow->ui->plainTextEdit_TaskDesc && event->type() == QEvent::FocusOut) {
        SaveTaskDescription();
        return false;
    }
    
    // Check for key press events
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // Handle Enter/Shift+Enter for task description text edit
        if (watched == m_mainWindow->ui->plainTextEdit_TaskDesc && keyEvent->key() == Qt::Key_Return) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                // Shift+Enter: Insert newline
                return false;
            } else {
                // Enter without Shift: Save description and set focus to task list display
                SaveTaskDescription();
                m_mainWindow->ui->listWidget_TaskListDisplay->setFocus();
                return true;
            }
        }
        
        // Check if the key is Delete
        if (keyEvent->key() == Qt::Key_Delete) {
            if (watched == m_mainWindow->ui->listWidget_TaskList_List ||
                watched == m_mainWindow->ui->listWidget_TaskListDisplay) {
                handleDeleteKeyPress();
                return true;
            }
        }
    }
    
    // Mouse press events on other widgets to save when clicking elsewhere
    if (event->type() == QEvent::MouseButtonPress &&
        watched != m_mainWindow->ui->plainTextEdit_TaskDesc &&
        m_mainWindow->ui->plainTextEdit_TaskDesc->hasFocus()) {
        SaveTaskDescription();
        return false;
    }
    
    return QObject::eventFilter(watched, event);
}

void Operations_TaskLists::onTaskListItemClicked(QListWidgetItem* item)
{
    m_lastClickedWidget = m_mainWindow->ui->listWidget_TaskList_List;
    m_lastClickedItem = item;
}

void Operations_TaskLists::onTaskDisplayItemClicked(QListWidgetItem* item)
{
    m_lastClickedWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    m_lastClickedItem = item;
}

void Operations_TaskLists::onTaskListItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    
    currentTaskListBeingRenamed = item->text();
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_mainWindow->ui->listWidget_TaskList_List->editItem(item);
    
    connect(m_mainWindow->ui->listWidget_TaskList_List, &QListWidget::itemChanged, this,
            [this, item](QListWidgetItem* changedItem) {
                if (changedItem == item) {
                    disconnect(m_mainWindow->ui->listWidget_TaskList_List, &QListWidget::itemChanged, this, nullptr);
                    RenameTasklist(item);
                }
            });
}

void Operations_TaskLists::onTaskDisplayItemDoubleClicked(QListWidgetItem* item)
{
    if (!item || (item->flags() & Qt::ItemIsEnabled) == 0) return;
    
    currentTaskToEdit = item->text();
    currentTaskData = item->data(Qt::UserRole).toString();
    m_currentTaskName = item->text();
    
    ShowTaskMenu(true);
}

void Operations_TaskLists::handleDeleteKeyPress()
{
    if (!m_lastClickedWidget || !m_lastClickedItem) return;
    if ((m_lastClickedItem->flags() & Qt::ItemIsEnabled) == 0) return;
    
    if (m_lastClickedWidget == m_mainWindow->ui->listWidget_TaskList_List) {
        DeleteTaskList();
    } else if (m_lastClickedWidget == m_mainWindow->ui->listWidget_TaskListDisplay) {
        DeleteTask(m_lastClickedItem->text());
    }
}

void Operations_TaskLists::EditSelectedTask()
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QListWidgetItem* selectedItem = taskListWidget->currentItem();
    
    if (!selectedItem || (selectedItem->flags() & Qt::ItemIsEnabled) == 0) return;
    
    currentTaskToEdit = selectedItem->text();
    currentTaskData = selectedItem->data(Qt::UserRole).toString();
    m_currentTaskName = selectedItem->text();
    
    ShowTaskMenu(true);
}

//--------Task List Display Functions--------//
void Operations_TaskLists::LoadIndividualTasklist(const QString& tasklistName, const QString& taskToSelect)
{
    qDebug() << "Operations_TaskLists: Loading tasklist:" << tasklistName;
    
    m_mainWindow->ui->plainTextEdit_TaskDesc->clear();
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    taskDisplayWidget->clear();
    
    // Validate the task list name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(tasklistName, InputValidation::InputType::TaskListName);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task List Name", nameResult.errorMessage);
        return;
    }
    
    // Security: Use centralized sanitization for file operations
    QString sanitizedName = TaskDataSecurity::sanitizeFileName(tasklistName);
    
    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                            "Could not access task list file: Invalid path or file format");
        return;
    }
    
    // Check if the file exists
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(m_mainWindow, "File Not Found",
                            "Task list file does not exist: " + taskListFilePath);
        return;
    }
    
    // Read the task list file
    QStringList taskLines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, taskLines)) {
        QMessageBox::warning(m_mainWindow, "Read Error",
                            "Could not read the task list file. It may be corrupted or tampered with.");
        return;
    }
    
    if (taskLines.isEmpty()) return;
    
    // Set the task list label with the name
    m_mainWindow->ui->label_TaskListName->setText(tasklistName);
    
    // Process each line in the file (skip header line)
    for (int i = 1; i < taskLines.size(); i++) {
        QString line = taskLines[i];
        if (line.isEmpty()) continue;
        
        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');
        
        // Security: Validate task data structure
        if (!TaskDataSecurity::validateTaskDataStructure(parts)) {
            qWarning() << "Operations_TaskLists: Skipping invalid task entry";
            continue;
        }
        
        QString taskType = parts[0];
        QString taskName = parts[1];
        
        // Only process Simple tasks
        if (taskType != "Simple") {
            qWarning() << "Operations_TaskLists: Skipping non-Simple task type:" << taskType;
            continue;
        }
        
        // Security: Use centralized unescaping for task name
        taskName = TaskDataSecurity::unescapeTaskField(taskName);
        
        // Check if the task is completed (field index 2)
        bool isCompleted = false;
        if (parts.size() > 2 && parts[2] == "1") {
            isCompleted = true;
        }
        
        // Create a list widget item for the task
        QListWidgetItem* item = new QListWidgetItem(taskName);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(isCompleted ? Qt::Checked : Qt::Unchecked);
        
        // Apply visual formatting based on completion status
        if (isCompleted) {
            QFont font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
            item->setForeground(QColor(100, 100, 100)); // Grey color
        } else {
            QFont font = item->font();
            font.setStrikeOut(false);
            item->setFont(font);
            item->setForeground(QColor(255, 255, 255)); // White color
        }
        
        // Store the original task data as item data for reference
        item->setData(Qt::UserRole, line);
        
        // Add the item to the list widget
        taskDisplayWidget->addItem(item);
    }
    
    // If the list is empty, display a message
    if (taskDisplayWidget->count() == 0) {
        QListWidgetItem* item = new QListWidgetItem("No tasks in this list");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        taskDisplayWidget->addItem(item);
        
        // Clear the table
        m_mainWindow->ui->tableWidget_TaskDetails->clear();
        m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
        m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);
    }
    
    // After loading all tasks, select the appropriate task
    int taskToSelectIndex = -1;
    
    if (!taskToSelect.isEmpty()) {
        for (int i = 0; i < taskDisplayWidget->count(); ++i) {
            QListWidgetItem* item = taskDisplayWidget->item(i);
            if (item->text() == taskToSelect) {
                taskToSelectIndex = i;
                break;
            }
        }
    }
    
    // If we didn't find the specified task or none was specified, select the last item
    if (taskToSelectIndex == -1 && taskDisplayWidget->count() > 0) {
        taskToSelectIndex = taskDisplayWidget->count() - 1;
    }
    
    // If we have a valid index, select that item
    if (taskToSelectIndex >= 0 && taskToSelectIndex < taskDisplayWidget->count()) {
        taskDisplayWidget->setCurrentRow(taskToSelectIndex);
        QListWidgetItem* selectedItem = taskDisplayWidget->item(taskToSelectIndex);
        
        if (selectedItem && (selectedItem->flags() & Qt::ItemIsEnabled)) {
            m_currentTaskName = selectedItem->text();
            LoadTaskDetails(selectedItem->text());
        }
    }
    
    // Update the appearance of the tasklist in the list
    UpdateTasklistAppearance(tasklistName);
}

void Operations_TaskLists::LoadTaskDetails(const QString& taskName)
{
    qDebug() << "Operations_TaskLists: Loading task details for:" << taskName;
    
    m_currentTaskName = taskName;
    
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name", nameResult.errorMessage);
        return;
    }
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Security: Use centralized sanitization for file operations
    QString sanitizedName = TaskDataSecurity::sanitizeFileName(currentTaskList);
    
    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Validate file path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(taskListFilePath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                            "Could not access task list file: " + pathResult.errorMessage);
        return;
    }
    
    // Check if the file exists
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(m_mainWindow, "File Not Found",
                            "Task list file does not exist.");
        return;
    }
    
    // Validate the tasklist file for security
    if (!InputValidation::validateTasklistFile(taskListFilePath, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid Task List File",
                            "Could not validate the task list file. It may be corrupted or tampered with.");
        return;
    }
    
    // Security: Use secure temporary file generation
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                            "Could not create temporary directory.");
        return;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName(sanitizedName, tempDir);
    
    // Create a scope guard for temp file cleanup
    class TempFileGuard {
    public:
        TempFileGuard(const QString& path) : m_path(path) {}
        ~TempFileGuard() { QFile::remove(m_path); }
    private:
        QString m_path;
    };
    
    // Decrypt the file to a temporary location
    bool decrypted = CryptoUtils::Encryption_DecryptFile(
        m_mainWindow->user_Key, taskListFilePath, tempPath);
    
    if (!decrypted) {
        QFile::remove(tempPath); // Immediate cleanup on failure
        QMessageBox::warning(m_mainWindow, "Decryption Failed",
                            "Could not decrypt task list file.");
        return;
    }
    
    TempFileGuard tempGuard(tempPath); // Ensures cleanup on scope exit
    
    // Set up the table widget
    QTableWidget* taskDetailsTable = m_mainWindow->ui->tableWidget_TaskDetails;
    taskDetailsTable->clear();
    taskDetailsTable->setRowCount(0);
    taskDetailsTable->setColumnCount(0);
    taskDetailsTable->verticalHeader()->setVisible(false);
    taskDetailsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskDetailsTable->setFocusPolicy(Qt::NoFocus);
    taskDetailsTable->setSelectionMode(QAbstractItemView::NoSelection);
    
    // Open the decrypted file
    QFile file(tempPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QFile::remove(tempPath);
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not open task list file for reading.");
        return;
    }
    
    // Read the file content
    QTextStream in(&file);
    QString headerLine = in.readLine(); // Skip the header line
    TaskDataSecurity::secureStringClear(headerLine); // Clear header from memory
    
    QString taskDescription = "";
    bool taskFound = false;
    
    // Process each line in the file to find the task
    while (!in.atEnd() && !taskFound) {
        QString line = in.readLine();
        if (line.isEmpty()) continue;
        
        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');
        
        // Basic sanity check
        if (parts.size() < 2) continue;
        
        QString taskType = parts[0];
        QString currentTaskName = parts[1];
        
        // Unescape any escaped pipe characters in the task name
        currentTaskName.replace("\\|", "|");
        
        // Check if this is the task we're looking for
        if (currentTaskName == taskName) {
            taskFound = true;
            currentTaskData = line;
            
            // Check for task description (should be at index 5)
            if (parts.size() > 5 && parts[5].startsWith("DESC:")) {
                taskDescription = parts[5].mid(5); // Remove "DESC:" prefix
                // Security: Use centralized unescaping
                taskDescription = TaskDataSecurity::unescapeTaskField(taskDescription);
            }
            
            // Get completion status
            bool isCompleted = (parts.size() > 2 && parts[2] == "1");
            QString completionStatus = isCompleted ? "Completed" : "Pending";
            
            // Get creation date
            QString creationDate = (parts.size() > 4) ? parts[4] : "Unknown";
            QDateTime creationDateTime = QDateTime::fromString(creationDate, Qt::ISODate);
            QString formattedCreationDate = FormatDateTime(creationDateTime);
            
            // Configure table for Simple task
            int columnCount = isCompleted ? 4 : 3;
            
            taskDetailsTable->setColumnCount(columnCount);
            
            QStringList headers;
            headers << "Task Type" << "Status";
            
            if (isCompleted) {
                headers << "Completion Date" << "Creation Date";
            } else {
                headers << "Creation Date";
            }
            
            taskDetailsTable->setHorizontalHeaderLabels(headers);
            taskDetailsTable->insertRow(0);
            
            // Task Type
            taskDetailsTable->setItem(0, 0, new QTableWidgetItem("Simple"));
            
            // Completion Status
            QTableWidgetItem* statusItem = new QTableWidgetItem(completionStatus);
            if (isCompleted) {
                statusItem->setForeground(Qt::green);
            }
            taskDetailsTable->setItem(0, 1, statusItem);
            
            // If task is completed, add completion date
            if (isCompleted) {
                QString completionDateStr = (parts.size() > 3) ? parts[3] : "";
                QDateTime completionDateTime = QDateTime::fromString(completionDateStr, Qt::ISODate);
                QString formattedCompletionDate = FormatDateTime(completionDateTime);
                taskDetailsTable->setItem(0, 2, new QTableWidgetItem(formattedCompletionDate));
                
                // Creation Date
                taskDetailsTable->setItem(0, 3, new QTableWidgetItem(formattedCreationDate));
            } else {
                // Creation Date
                taskDetailsTable->setItem(0, 2, new QTableWidgetItem(formattedCreationDate));
            }
            
            // Resize columns to content
            taskDetailsTable->resizeColumnsToContents();
            
            // Make the last column stretch
            int lastColumn = taskDetailsTable->columnCount() - 1;
            taskDetailsTable->horizontalHeader()->setSectionResizeMode(lastColumn, QHeaderView::Stretch);
            
            break;
        }
    }
    
    file.close();
    // tempPath cleanup is handled by TempFileGuard
    
    if (!taskFound) {
        qWarning() << "Operations_TaskLists: Could not find the specified task in the task list.";
        return;
    }
    
    // Set the task description
    if (m_mainWindow->ui->plainTextEdit_TaskDesc) {
        m_mainWindow->ui->plainTextEdit_TaskDesc->setPlainText(taskDescription);
        m_lastSavedDescription = m_mainWindow->ui->plainTextEdit_TaskDesc->toPlainText();
    }
    
    QTextCursor cursor = m_mainWindow->ui->plainTextEdit_TaskDesc->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_mainWindow->ui->plainTextEdit_TaskDesc->setTextCursor(cursor);
    
    // Select the current task in the list
    for (int i = 0; i < m_mainWindow->ui->listWidget_TaskListDisplay->count(); ++i) {
        QListWidgetItem* item = m_mainWindow->ui->listWidget_TaskListDisplay->item(i);
        if (item->text() == taskName) {
            m_mainWindow->ui->listWidget_TaskListDisplay->setCurrentItem(item);
            break;
        }
    }
}

QString Operations_TaskLists::FormatDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid()) {
        return "Unknown";
    }
    
    QDate date = dateTime.date();
    QTime time = dateTime.time();
    
    QString dayOfWeek = Operations::GetDayOfWeek(date);
    int day = date.day();
    QString ordinalSuffix = Operations::GetOrdinalSuffix(day);
    QString month = date.toString("MMMM");
    int year = date.year();
    QString timeString = time.toString("HH:mm");
    
    return QString("%1 the %2%3 %4 %5 at %6")
        .arg(dayOfWeek)
        .arg(day)
        .arg(ordinalSuffix)
        .arg(month)
        .arg(year)
        .arg(timeString);
}

//--------Task List Management Functions--------//
void Operations_TaskLists::LoadTasklists()
{
    qDebug() << "Operations_TaskLists: Loading tasklists";
    
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    taskListWidget->clear();
    taskListWidget->setSortingEnabled(false);
    
    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";
    
    // Validate the path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(tasksListsPath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Operations_TaskLists: Invalid tasklists path:" << pathResult.errorMessage;
        return;
    }
    
    // Check if the directory exists, create it if not
    if (!OperationsFiles::ensureDirectoryExists(tasksListsPath)) {
        qWarning() << "Operations_TaskLists: Failed to create Tasklists directory";
        return;
    }
    
    // Try to load the saved task list order
    QStringList orderedTasklists;
    bool hasOrderFile = LoadTasklistOrder(orderedTasklists);
    
    // Get all subdirectories
    QDir tasksListsDir(tasksListsPath);
    QStringList taskListDirs = tasksListsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    struct TaskListInfo {
        QString name;
        QDateTime creationDate;
        QString displayName;
        int order;
    };
    
    QList<TaskListInfo> taskLists;
    QSet<QString> orderedNames;
    
    // First pass: Process the tasklists that have a saved order
    if (hasOrderFile) {
        for (int i = 0; i < orderedTasklists.size(); ++i) {
            QString taskListName = orderedTasklists[i];
            // Security: Use centralized sanitization
            QString sanitizedName = TaskDataSecurity::sanitizeFileName(taskListName);
            
            if (taskListDirs.contains(sanitizedName)) {
                QString taskListPath = tasksListsPath + sanitizedName + "/";
                QString taskListFilePath = taskListPath + sanitizedName + ".txt";
                
                QFileInfo fileInfo(taskListFilePath);
                if (fileInfo.exists() && fileInfo.isFile() &&
                    OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
                    
                    QDateTime creationDate = fileInfo.birthTime();
                    if (!creationDate.isValid()) {
                        creationDate = fileInfo.lastModified();
                    }
                    
                    TaskListInfo taskListInfo;
                    taskListInfo.name = sanitizedName;
                    taskListInfo.creationDate = creationDate;
                    taskListInfo.displayName = taskListName;
                    taskListInfo.order = i;
                    
                    taskLists.append(taskListInfo);
                    orderedNames.insert(sanitizedName);
                }
            }
        }
    }
    
    // Second pass: Process any remaining tasklists not in the order file
    for (const QString& taskListDirName : taskListDirs) {
        if (orderedNames.contains(taskListDirName)) continue;
        
        QString taskListPath = tasksListsPath + taskListDirName + "/";
        QString taskListFilePath = taskListPath + taskListDirName + ".txt";
        
        QFileInfo fileInfo(taskListFilePath);
        if (fileInfo.exists() && fileInfo.isFile() &&
            OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
            
            QDateTime creationDate = fileInfo.birthTime();
            if (!creationDate.isValid()) {
                creationDate = fileInfo.lastModified();
            }
            
            TaskListInfo taskListInfo;
            taskListInfo.name = taskListDirName;
            taskListInfo.creationDate = creationDate;
            taskListInfo.displayName = taskListDirName;
            taskListInfo.order = orderedTasklists.size() + 1000;
            
            taskLists.append(taskListInfo);
        }
    }
    
    // Sort task lists
    std::sort(taskLists.begin(), taskLists.end(),
              [](const TaskListInfo& a, const TaskListInfo& b) {
                  if (a.order != b.order) {
                      return a.order < b.order;
                  }
                  return a.creationDate < b.creationDate;
              });
    
    // Add the sorted task lists to the list widget
    for (const TaskListInfo& taskList : taskLists) {
        QListWidgetItem* item = new QListWidgetItem(taskList.displayName);
        item->setData(Qt::UserRole, taskList.name);
        taskListWidget->addItem(item);
    }
    
    for (int i = 0; i < taskListWidget->count(); ++i) {
        UpdateTasklistAppearance(taskListWidget->item(i)->text());
    }
    
    // Select the first task list if any exist
    if (taskListWidget->count() > 0) {
        taskListWidget->setCurrentRow(0);
    }
    
    taskListWidget->setSortingEnabled(false);
    
    // Load the selected tasklist if there is one
    if (taskListWidget->currentItem()) {
        LoadIndividualTasklist(taskListWidget->currentItem()->text(), m_currentTaskName);
    }
}

void Operations_TaskLists::CreateNewTaskList()
{
    qDebug() << "Operations_TaskLists: Creating new task list";
    
    m_mainWindow->ui->listWidget_TaskList_List->setSortingEnabled(false);
    
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    
    // Check for existing task lists with the name "New Task List"
    QStringList existingNames;
    for (int i = 0; i < taskListWidget->count(); ++i) {
        QListWidgetItem* item = taskListWidget->item(i);
        existingNames.append(item->text());
    }
    
    // Get a unique name for the new task list
    QString initialName = "New Task List";
    QString uniqueName = Operations::GetUniqueItemName(initialName, existingNames);
    
    // Add a new item to the list widget with the unique name
    QListWidgetItem* newItem = new QListWidgetItem(uniqueName);
    taskListWidget->addItem(newItem);
    
    // Create the task list directory and file
    CreateTaskListFile(uniqueName);
    
    // Make the item editable and select it
    taskListWidget->setCurrentItem(newItem);
    newItem->setFlags(newItem->flags() | Qt::ItemIsEditable);
    taskListWidget->editItem(newItem);
    
    // Connect to itemChanged signal once to handle the edit completion
    QObject::connect(taskListWidget, &QListWidget::itemChanged, this,
                    [this, taskListWidget, newItem, uniqueName](QListWidgetItem* changedItem) {
                        if (changedItem == newItem) {
                            QObject::disconnect(taskListWidget, &QListWidget::itemChanged, this, nullptr);
                            
                            if (changedItem->text() == uniqueName) {
                                return;
                            }
                            
                            // Validate the task list name
                            QString listName = changedItem->text().trimmed();
                            InputValidation::ValidationResult result =
                                InputValidation::validateInput(listName, InputValidation::InputType::TaskListName);
                            
                            if (!result.isValid) {
                                QMessageBox::warning(m_mainWindow, "Invalid Task List Name",
                                                    result.errorMessage);
                                changedItem->setText(uniqueName);
                                return;
                            }
                            
                            // Get all existing task list names for uniqueness check
                            QStringList existingNames;
                            for (int i = 0; i < taskListWidget->count(); ++i) {
                                QListWidgetItem* item = taskListWidget->item(i);
                                if (item != changedItem) {
                                    existingNames.append(item->text());
                                }
                            }
                            
                            // Ensure unique name
                            QString newUniqueName = Operations::GetUniqueItemName(listName, existingNames);
                            if (newUniqueName != listName) {
                                changedItem->setText(newUniqueName);
                                listName = newUniqueName;
                            }
                            
                            // Create a new task list file with the new name
                            CreateTaskListFile(listName);
                            
                            // Delete the old task list file
                            // Security: Use centralized sanitization
                            QString oldSanitizedName = TaskDataSecurity::sanitizeFileName(uniqueName);
                            
                            QString oldTaskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + oldSanitizedName + "/";
                            QString oldTaskListFilePath = oldTaskListDir + oldSanitizedName + ".txt";
                            
                            QFileInfo fileInfo(oldTaskListFilePath);
                            if (fileInfo.exists() && fileInfo.isFile()) {
                                QFile file(oldTaskListFilePath);
                                file.remove();
                                QDir dir(oldTaskListDir);
                                dir.removeRecursively();
                            }
                            
                            emit taskListWidget->itemClicked(newItem);
                        }
                    });
}

void Operations_TaskLists::CreateTaskListFile(const QString& listName)
{
    qDebug() << "Operations_TaskLists: Creating task list file for:" << listName;
    
    // Security: Use centralized sanitization
    QString sanitizedName = TaskDataSecurity::sanitizeFileName(listName);
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid Path",
                            "Cannot create task list file: Invalid path");
        return;
    }
    
    // Create the directory if it doesn't exist
    if (!OperationsFiles::ensureDirectoryExists(taskListDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Creation Failed",
                            "Failed to create directory for task list.");
        return;
    }
    
    // Get the current date for the header
    QDate currentDate = QDate::currentDate();
    QString dayOfWeek = Operations::GetDayOfWeek(currentDate);
    int day = currentDate.day();
    QString month = currentDate.toString("MMMM");
    int year = currentDate.year();
    QString ordinalSuffix = Operations::GetOrdinalSuffix(day);
    
    QString dateString = QString("%1 the %2%3 of %4 %5")
                            .arg(dayOfWeek)
                            .arg(day)
                            .arg(ordinalSuffix)
                            .arg(month)
                            .arg(year);
    
    // Create the initial content with just the header
    QStringList initialContent;
    initialContent.append(dateString);
    
    // Create a new tasklist file with the header
    if (!OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, initialContent)) {
        QMessageBox::warning(m_mainWindow, "File Creation Failed",
                            "Failed to create task list file.");
        return;
    }
    
    // Load the newly created tasklist
    LoadIndividualTasklist(listName, "NULL");
}

void Operations_TaskLists::DeleteTaskList()
{
    qDebug() << "Operations_TaskLists: Deleting task list";
    
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QListWidgetItem* currentItem = taskListWidget->currentItem();
    
    if (!currentItem) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list to delete.");
        return;
    }
    
    QString taskListName = currentItem->text();
    
    // Confirm deletion
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(m_mainWindow, "Confirm Deletion",
                                  "Are you sure you want to delete the task list \"" + taskListName + "\"?",
                                  QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Security: Use centralized sanitization
    QString sanitizedName = TaskDataSecurity::sanitizeFileName(taskListName);
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    QStringList hierarchyLevels;
    hierarchyLevels << "Data" << m_mainWindow->user_Username << "Tasklists" << sanitizedName;
    
    QString basePath = "Data/";
    
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile() ||
        !OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File",
                            "Task list file does not exist or cannot be accessed.");
        return;
    }
    
    bool fileDeleted = OperationsFiles::deleteFileAndCleanEmptyDirs(taskListFilePath, hierarchyLevels, basePath);
    
    if (!fileDeleted) {
        QMessageBox::warning(m_mainWindow, "Delete Failed",
                            "Could not delete the task list file.");
        return;
    }
    
    // Check and clean up directory
    QDir dir(taskListDir);
    if (dir.exists()) {
        QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        if (entries.isEmpty()) {
            dir.removeRecursively();
        }
    }
    
    int currentIndex = taskListWidget->row(currentItem);
    delete taskListWidget->takeItem(taskListWidget->row(currentItem));
    
    // Clear UI elements
    m_mainWindow->ui->listWidget_TaskListDisplay->clear();
    m_mainWindow->ui->tableWidget_TaskDetails->clear();
    m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
    m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);
    m_mainWindow->ui->plainTextEdit_TaskDesc->clear();
    m_mainWindow->ui->label_TaskListName->clear();
    
    m_lastClickedItem = nullptr;
    m_lastClickedWidget = nullptr;
    
    // Select another task list if available
    if (taskListWidget->count() > 0) {
        int newIndex = (currentIndex >= taskListWidget->count()) ?
                           taskListWidget->count() - 1 : currentIndex;
        
        taskListWidget->setCurrentRow(newIndex);
        QListWidgetItem* newCurrentItem = taskListWidget->item(newIndex);
        if (newCurrentItem) {
            onTaskListItemClicked(newCurrentItem);
            emit taskListWidget->itemClicked(newCurrentItem);
        }
    }
}

void Operations_TaskLists::RenameTasklist(QListWidgetItem* item)
{
    qDebug() << "Operations_TaskLists: Renaming tasklist";
    
    Qt::ItemFlags originalFlags = item->flags();
    
    QString originalName = currentTaskListBeingRenamed;
    QString newName = item->text().trimmed();
    
    // Validate the new task list name
    InputValidation::ValidationResult result =
        InputValidation::validateInput(newName, InputValidation::InputType::TaskListName);
    
    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task List Name", result.errorMessage);
        item->setText(originalName);
        return;
    }
    
    // Check if the name already exists
    QStringList existingNames;
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    for (int i = 0; i < taskListWidget->count(); ++i) {
        QListWidgetItem* existingItem = taskListWidget->item(i);
        if (existingItem != item) {
            existingNames.append(existingItem->text());
        }
    }
    
    if (existingNames.contains(newName)) {
        QString uniqueName = Operations::GetUniqueItemName(newName, existingNames);
        newName = uniqueName;
        item->setText(uniqueName);
    }
    
    if (newName == originalName) {
        return;
    }
    
    // Security: Use centralized sanitization
    QString originalSanitizedName = TaskDataSecurity::sanitizeFileName(originalName);
    QString newSanitizedName = TaskDataSecurity::sanitizeFileName(newName);
    
    QString originalTaskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + originalSanitizedName + "/";
    QString originalTaskListFilePath = originalTaskListDir + originalSanitizedName + ".txt";
    
    QString newTaskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + newSanitizedName + "/";
    QString newTaskListFilePath = newTaskListDir + newSanitizedName + ".txt";
    
    // Validate paths
    InputValidation::ValidationResult originalPathResult =
        InputValidation::validateInput(originalTaskListFilePath, InputValidation::InputType::FilePath);
    
    InputValidation::ValidationResult newPathResult =
        InputValidation::validateInput(newTaskListFilePath, InputValidation::InputType::FilePath);
    
    if (!originalPathResult.isValid || !newPathResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                            "Could not access task list file.");
        item->setText(originalName);
        return;
    }
    
    // Check if the original file exists
    QFileInfo fileInfo(originalTaskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(m_mainWindow, "File Not Found",
                            "Original task list file does not exist.");
        item->setText(originalName);
        return;
    }
    
    // Validate the tasklist file for security
    if (!InputValidation::validateTasklistFile(originalTaskListFilePath, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid Task List File",
                            "Could not validate the task list file.");
        item->setText(originalName);
        return;
    }
    
    // Create the new directory
    QDir newDir(newTaskListDir);
    if (!newDir.exists()) {
        if (!newDir.mkpath(".")) {
            QMessageBox::warning(m_mainWindow, "Directory Creation Failed",
                                "Failed to create directory for renamed task list.");
            item->setText(originalName);
            return;
        }
    }
    
    // Security: Use secure temporary file generation
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                            "Could not create temporary directory.");
        item->setText(originalName);
        return;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName(originalSanitizedName, tempDir);
    
    // Decrypt the file to a temporary location
    bool decrypted = CryptoUtils::Encryption_DecryptFile(
        m_mainWindow->user_Key, originalTaskListFilePath, tempPath);
    
    if (!decrypted) {
        QMessageBox::warning(m_mainWindow, "Decryption Failed",
                            "Could not decrypt task list file.");
        item->setText(originalName);
        return;
    }
    
    // Re-encrypt the file to the new location
    bool encrypted = CryptoUtils::Encryption_EncryptFile(
        m_mainWindow->user_Key, tempPath, newTaskListFilePath);
    
    QFile::remove(tempPath);
    
    if (!encrypted) {
        QMessageBox::warning(m_mainWindow, "Encryption Failed",
                            "Could not encrypt the task list file.");
        item->setText(originalName);
        return;
    }
    
    // Delete the original file and directory
    QFile originalFile(originalTaskListFilePath);
    originalFile.remove();
    
    QDir originalDir(originalTaskListDir);
    if (originalDir.exists() && originalDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        originalDir.removeRecursively();
    }
    
    taskListWidget->setCurrentItem(item);
    LoadIndividualTasklist(newName, m_currentTaskName);
    item->setFlags(originalFlags);
}

//--------Task Operations--------//
void Operations_TaskLists::ShowTaskMenu(bool editMode)
{
    qDebug() << "Operations_TaskLists: Showing task menu, editMode:" << editMode;
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Create and configure the dialog
    QDialog dialog(m_mainWindow);
    Ui::Tasklists_AddTask ui;
    ui.setupUi(&dialog);
    
    // Set window title based on mode
    dialog.setWindowTitle(editMode ? "Edit Task" : "New Task");
    
    // If editing, populate the fields
    if (editMode) {
        // Parse the current task data
        QStringList parts = currentTaskData.split('|');
        if (parts.size() >= 2) {
            QString taskName = parts[1];
            taskName.replace("\\|", "|");
            ui.lineEdit_TaskName->setText(taskName);
            
            // Load description if available
            if (parts.size() > 5 && parts[5].startsWith("DESC:")) {
                QString description = parts[5].mid(5);
                description.replace("\\|", "|");
                description.replace("\\n", "\n");
                description.replace("\\r", "\r");
                ui.plainTextEdit_Simple_Desc->setPlainText(description);
            }
        }
    }
    
    // Show the dialog
    if (dialog.exec() == QDialog::Accepted) {
        QString taskName = ui.lineEdit_TaskName->text().trimmed();
        
        // Validate task name
        InputValidation::ValidationResult result =
            InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
        
        if (!result.isValid) {
            QMessageBox::warning(m_mainWindow, "Invalid Task Name", result.errorMessage);
            return;
        }
        
        if (taskName.isEmpty()) {
            QMessageBox::warning(m_mainWindow, "Empty Task Name",
                                "Please enter a task name.");
            return;
        }
        
        QString description = ui.plainTextEdit_Simple_Desc->toPlainText();
        
        if (editMode) {
            ModifyTaskSimple(currentTaskToEdit, taskName, description);
        } else {
            AddTaskSimple(taskName, description);
        }
    }
}

void Operations_TaskLists::AddTaskSimple(QString taskName, QString description)
{
    qDebug() << "Operations_TaskLists: Adding simple task:" << taskName;
    
    // Validate inputs
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name", nameResult.errorMessage);
        return;
    }
    
    // Security: Enforce length limits
    if (taskName.length() > 255) {
        QMessageBox::warning(m_mainWindow, "Task Name Too Long",
                            "Task name must be less than 255 characters.");
        return;
    }
    
    if (description.length() > 10000) {
        QMessageBox::warning(m_mainWindow, "Description Too Long",
                            "Task description must be less than 10,000 characters.");
        return;
    }
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Security: Use centralized sanitization
    QString sanitizedName = TaskDataSecurity::sanitizeFileName(currentTaskList);
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Check for duplicate task names
    if (checkDuplicateTaskName(taskName, taskListFilePath)) {
        QMessageBox::warning(m_mainWindow, "Duplicate Task Name",
                            "A task with this name already exists in the current task list.");
        return;
    }
    
    // Security: Use centralized escaping for task data
    QString escapedTaskName = TaskDataSecurity::escapeTaskField(taskName);
    QString escapedDescription = TaskDataSecurity::escapeTaskField(description);
    
    // Create the task data line with escaped values
    QString taskData = QString("Simple|%1|0||%2|DESC:%3")
        .arg(escapedTaskName)
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .arg(escapedDescription);
    
    // Read the existing file content
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not read the task list file.");
        return;
    }
    
    // Add the new task
    lines.append(taskData);
    
    // Write back the file
    if (!OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not write to the task list file.");
        return;
    }
    
    // Unescape task name for display
    taskName.replace("\\|", "|");
    
    // Reload the task list to show the new task
    LoadIndividualTasklist(currentTaskList, taskName);
}

void Operations_TaskLists::ModifyTaskSimple(const QString& originalTaskName, QString taskName, QString description)
{
    qDebug() << "Operations_TaskLists: Modifying simple task:" << originalTaskName << "to" << taskName;
    
    // Validate inputs
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name", nameResult.errorMessage);
        return;
    }
    
    // Security: Enforce length limits
    if (taskName.length() > 255) {
        QMessageBox::warning(m_mainWindow, "Task Name Too Long",
                            "Task name must be less than 255 characters.");
        return;
    }
    
    if (description.length() > 10000) {
        QMessageBox::warning(m_mainWindow, "Description Too Long",
                            "Task description must be less than 10,000 characters.");
        return;
    }
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Sanitize the task list name
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Check for duplicate task names (if name changed)
    if (originalTaskName != taskName && checkDuplicateTaskName(taskName, taskListFilePath)) {
        QMessageBox::warning(m_mainWindow, "Duplicate Task Name",
                            "A task with this name already exists in the current task list.");
        return;
    }
    
    // Read the existing file content
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not read the task list file.");
        return;
    }
    
    // Find and modify the task
    bool taskFound = false;
    QString originalTaskNameEscaped = originalTaskName;
    originalTaskNameEscaped.replace("|", "\\|");
    
    for (int i = 1; i < lines.size(); ++i) {
        QStringList parts = lines[i].split('|');
        if (parts.size() >= 2 && parts[1] == originalTaskNameEscaped) {
            taskFound = true;
            
            // Preserve existing completion status and date
            QString completionStatus = (parts.size() > 2) ? parts[2] : "0";
            QString completionDate = (parts.size() > 3) ? parts[3] : "";
            QString creationDate = (parts.size() > 4) ? parts[4] : QDateTime::currentDateTime().toString(Qt::ISODate);
            
            // Escape special characters
            taskName.replace("|", "\\|");
            description.replace("|", "\\|");
            description.replace("\n", "\\n");
            description.replace("\r", "\\r");
            
            // Update the task data
            lines[i] = QString("Simple|%1|%2|%3|%4|DESC:%5")
                .arg(taskName)
                .arg(completionStatus)
                .arg(completionDate)
                .arg(creationDate)
                .arg(description);
            break;
        }
    }
    
    if (!taskFound) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                            "Could not find the task to modify.");
        return;
    }
    
    // Write back the file
    if (!OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not write to the task list file.");
        return;
    }
    
    // Unescape task name for display
    taskName.replace("\\|", "|");
    
    // Reload the task list to show the changes
    LoadIndividualTasklist(currentTaskList, taskName);
}

void Operations_TaskLists::DeleteTask(const QString& taskName)
{
    qDebug() << "Operations_TaskLists: Deleting task:" << taskName;
    
    // Confirm deletion
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(m_mainWindow, "Confirm Deletion",
                                  "Are you sure you want to delete the task \"" + taskName + "\"?",
                                  QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Sanitize the task list name
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Read the existing file content
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not read the task list file.");
        return;
    }
    
    // Find and remove the task
    bool taskFound = false;
    QString taskNameEscaped = taskName;
    taskNameEscaped.replace("|", "\\|");
    
    for (int i = 1; i < lines.size(); ++i) {
        QStringList parts = lines[i].split('|');
        if (parts.size() >= 2 && parts[1] == taskNameEscaped) {
            taskFound = true;
            lines.removeAt(i);
            break;
        }
    }
    
    if (!taskFound) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                            "Could not find the task to delete.");
        return;
    }
    
    // Write back the file
    if (!OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not write to the task list file.");
        return;
    }
    
    // Clear UI elements if this was the selected task
    if (m_currentTaskName == taskName) {
        m_mainWindow->ui->tableWidget_TaskDetails->clear();
        m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
        m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);
        m_mainWindow->ui->plainTextEdit_TaskDesc->clear();
        m_currentTaskName = "";
    }
    
    // Reload the task list
    LoadIndividualTasklist(currentTaskList, "");
}

void Operations_TaskLists::SetTaskStatus(bool checked, QListWidgetItem* item)
{
    qDebug() << "Operations_TaskLists: Setting task status, checked:" << checked;
    
    if (!item) {
        QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
        item = taskDisplayWidget->currentItem();
        
        if (!item) return;
    }
    
    QString taskName = item->text();
    QString taskData = item->data(Qt::UserRole).toString();
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) return;
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Sanitize the task list name
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Read the existing file content
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        return;
    }
    
    // Find and update the task
    QString taskNameEscaped = taskName;
    taskNameEscaped.replace("|", "\\|");
    
    for (int i = 1; i < lines.size(); ++i) {
        QStringList parts = lines[i].split('|');
        if (parts.size() >= 2 && parts[1] == taskNameEscaped) {
            // Update completion status and date
            if (parts.size() > 2) {
                parts[2] = checked ? "1" : "0";
            }
            if (parts.size() > 3) {
                parts[3] = checked ? QDateTime::currentDateTime().toString(Qt::ISODate) : "";
            }
            
            lines[i] = parts.join('|');
            break;
        }
    }
    
    // Write back the file
    if (!OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        return;
    }
    
    // Update visual appearance
    QFont font = item->font();
    font.setStrikeOut(checked);
    item->setFont(font);
    item->setForeground(checked ? QColor(100, 100, 100) : QColor(255, 255, 255));
    
    // Reorder tasks
    EnforceTaskOrder();
    
    // Update task details view
    LoadTaskDetails(taskName);
    
    // Update tasklist appearance
    UpdateTasklistAppearance(currentTaskList);
}

void Operations_TaskLists::SaveTaskDescription()
{
    qDebug() << "Operations_TaskLists: Saving task description";
    
    if (m_currentTaskName.isEmpty()) return;
    
    QString newDescription = m_mainWindow->ui->plainTextEdit_TaskDesc->toPlainText();
    if (newDescription == m_lastSavedDescription) return;
    
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) return;
    
    QString currentTaskList = taskListWidget->currentItem()->text();
    
    // Sanitize the task list name
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    // Read the existing file content
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        return;
    }
    
    // Find and update the task
    QString taskNameEscaped = m_currentTaskName;
    taskNameEscaped.replace("|", "\\|");
    
    for (int i = 1; i < lines.size(); ++i) {
        QStringList parts = lines[i].split('|');
        if (parts.size() >= 2 && parts[1] == taskNameEscaped) {
            // Escape special characters in description
            QString escapedDescription = newDescription;
            escapedDescription.replace("|", "\\|");
            escapedDescription.replace("\n", "\\n");
            escapedDescription.replace("\r", "\\r");
            
            // Update or add description field
            bool descriptionFound = false;
            for (int j = 5; j < parts.size(); ++j) {
                if (parts[j].startsWith("DESC:")) {
                    parts[j] = "DESC:" + escapedDescription;
                    descriptionFound = true;
                    break;
                }
            }
            
            if (!descriptionFound) {
                // Ensure we have at least 5 fields before adding description
                while (parts.size() < 5) {
                    parts.append("");
                }
                parts.append("DESC:" + escapedDescription);
            }
            
            lines[i] = parts.join('|');
            break;
        }
    }
    
    // Write back the file
    if (OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        m_lastSavedDescription = newDescription;
    }
}

//--------Helper Functions--------//
bool Operations_TaskLists::checkDuplicateTaskName(const QString& taskName, const QString& taskListFilePath, const QString& currentTaskId)
{
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        return false;
    }
    
    QString taskNameEscaped = taskName;
    taskNameEscaped.replace("|", "\\|");
    
    for (int i = 1; i < lines.size(); ++i) {
        QStringList parts = lines[i].split('|');
        if (parts.size() >= 2 && parts[1] == taskNameEscaped) {
            if (currentTaskId.isEmpty() || parts[1] != currentTaskId) {
                return true;
            }
        }
    }
    
    return false;
}

bool Operations_TaskLists::AreAllTasksCompleted(const QString& tasklistName)
{
    QString sanitizedName = tasklistName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";
    
    QStringList lines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, lines)) {
        return false;
    }
    
    int taskCount = 0;
    int completedCount = 0;
    
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) continue;
        
        QStringList parts = lines[i].split('|');
        if (parts.size() >= 3) {
            taskCount++;
            if (parts[2] == "1") {
                completedCount++;
            }
        }
    }
    
    return (taskCount > 0 && taskCount == completedCount);
}

void Operations_TaskLists::UpdateTasklistAppearance(const QString& tasklistName)
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    
    QList<QListWidgetItem*> items = taskListWidget->findItems(tasklistName, Qt::MatchExactly);
    if (items.isEmpty()) return;
    
    QListWidgetItem* item = items.first();
    
    if (AreAllTasksCompleted(tasklistName)) {
        QFont font = item->font();
        font.setStrikeOut(true);
        item->setFont(font);
        item->setForeground(QColor(100, 100, 100));
    } else {
        QFont font = item->font();
        font.setStrikeOut(false);
        item->setFont(font);
        item->setForeground(QColor(255, 255, 255));
    }
}

void Operations_TaskLists::EnforceTaskOrder()
{
    qDebug() << "Operations_TaskLists: Enforcing task order";
    
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    
    if (taskDisplayWidget->count() <= 1) return;
    
    taskDisplayWidget->blockSignals(true);
    
    QList<QListWidgetItem*> completedItems;
    QList<QListWidgetItem*> pendingItems;
    QList<QListWidgetItem*> disabledItems;
    
    QListWidgetItem* currentItem = taskDisplayWidget->currentItem();
    QString currentItemText = currentItem ? currentItem->text() : "";
    
    // Categorize items
    for (int i = 0; i < taskDisplayWidget->count(); ++i) {
        QListWidgetItem* item = taskDisplayWidget->takeItem(0);
        if (!item) continue;
        
        if ((item->flags() & Qt::ItemIsEnabled) == 0) {
            disabledItems.append(item);
        } else if (item->checkState() == Qt::Checked) {
            completedItems.append(item);
        } else {
            pendingItems.append(item);
        }
    }
    
    // Add items back in order
    for (QListWidgetItem* item : completedItems) {
        taskDisplayWidget->addItem(item);
    }
    for (QListWidgetItem* item : pendingItems) {
        taskDisplayWidget->addItem(item);
    }
    for (QListWidgetItem* item : disabledItems) {
        taskDisplayWidget->addItem(item);
    }
    
    // Restore selection
    if (!currentItemText.isEmpty()) {
        for (int i = 0; i < taskDisplayWidget->count(); ++i) {
            QListWidgetItem* item = taskDisplayWidget->item(i);
            if (item && item->text() == currentItemText) {
                taskDisplayWidget->setCurrentItem(item);
                break;
            }
        }
    }
    
    taskDisplayWidget->blockSignals(false);
}

bool Operations_TaskLists::SaveTasklistOrder()
{
    qDebug() << "Operations_TaskLists: Saving tasklist order";
    
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    
    if (taskListWidget->count() == 0) return true;
    
    QString orderFilePath = "Data/" + m_mainWindow->user_Username + "/Tasklists/TasklistOrder.txt";
    
    QStringList content;
    content.append("# TasklistOrder");
    
    for (int i = 0; i < taskListWidget->count(); ++i) {
        QListWidgetItem* item = taskListWidget->item(i);
        if (item) {
            content.append(item->text());
        }
    }
    
    return OperationsFiles::writeEncryptedFileLines(orderFilePath, m_mainWindow->user_Key, content);
}

bool Operations_TaskLists::LoadTasklistOrder(QStringList& orderedTasklists)
{
    QString orderFilePath = "Data/" + m_mainWindow->user_Username + "/Tasklists/TasklistOrder.txt";
    
    QFileInfo fileInfo(orderFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }
    
    if (!OperationsFiles::validateFilePath(orderFilePath, OperationsFiles::FileType::Generic, m_mainWindow->user_Key)) {
        qWarning() << "Operations_TaskLists: Invalid tasklist order file path";
        return false;
    }
    
    QStringList contentLines;
    if (!OperationsFiles::readEncryptedFileLines(orderFilePath, m_mainWindow->user_Key, contentLines)) {
        qWarning() << "Operations_TaskLists: Failed to read tasklist order file";
        return false;
    }
    
    if (contentLines.isEmpty()) {
        qWarning() << "Operations_TaskLists: Empty tasklist order file";
        return false;
    }
    
    QString headerLine = contentLines.first();
    if (!headerLine.startsWith("# TasklistOrder")) {
        qWarning() << "Operations_TaskLists: Invalid tasklist order file format";
        return false;
    }
    
    for (int i = 1; i < contentLines.size(); ++i) {
        QString line = contentLines[i].trimmed();
        if (line.isEmpty()) continue;
        
        InputValidation::ValidationResult nameResult =
            InputValidation::validateInput(line, InputValidation::InputType::TaskListName);
        if (nameResult.isValid) {
            orderedTasklists.append(line);
        }
    }
    
    return !orderedTasklists.isEmpty();
}

void Operations_TaskLists::UpdateTasklistsTextSize(int fontSize)
{
    qDebug() << "Operations_TaskLists: Updating text size to:" << fontSize;
    
    QFont font = m_mainWindow->ui->listWidget_TaskList_List->font();
    font.setPointSize(fontSize);
    m_mainWindow->ui->listWidget_TaskList_List->setFont(font);
    m_mainWindow->ui->listWidget_TaskListDisplay->setFont(font);
}

//--------Context Menu Functions--------//
void Operations_TaskLists::showContextMenu_TaskListDisplay(const QPoint &pos)
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QListWidgetItem* item = taskListWidget->itemAt(pos);
    
    QMenu contextMenu(m_mainWindow);
    
    QAction* newTaskAction = contextMenu.addAction("New Task");
    QAction* editTaskAction = contextMenu.addAction("Edit Task");
    QAction* deleteTaskAction = contextMenu.addAction("Delete Task");
    
    if (!item || (item->flags() & Qt::ItemIsEnabled) == 0) {
        editTaskAction->setEnabled(false);
        deleteTaskAction->setEnabled(false);
    }
    
    connect(newTaskAction, &QAction::triggered, this, [this]() {
        ShowTaskMenu(false);
    });
    
    connect(editTaskAction, &QAction::triggered, this, [this, item]() {
        if (item) {
            currentTaskToEdit = item->text();
            currentTaskData = item->data(Qt::UserRole).toString();
            m_currentTaskName = item->text();
            ShowTaskMenu(true);
        }
    });
    
    connect(deleteTaskAction, &QAction::triggered, this, [this, item]() {
        if (item) {
            DeleteTask(item->text());
        }
    });
    
    contextMenu.exec(taskListWidget->mapToGlobal(pos));
}

void Operations_TaskLists::showContextMenu_TaskListList(const QPoint &pos)
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QListWidgetItem* item = taskListWidget->itemAt(pos);
    
    QMenu contextMenu(m_mainWindow);
    
    QAction* newTaskListAction = contextMenu.addAction("New Tasklist");
    QAction* renameTaskListAction = contextMenu.addAction("Rename Tasklist");
    QAction* deleteTaskListAction = contextMenu.addAction("Delete Tasklist");
    
    if (!item) {
        renameTaskListAction->setEnabled(false);
        deleteTaskListAction->setEnabled(false);
    }
    
    connect(newTaskListAction, &QAction::triggered, this, &Operations_TaskLists::CreateNewTaskList);
    
    connect(renameTaskListAction, &QAction::triggered, this, [this, item, taskListWidget]() {
        if (item) {
            currentTaskListBeingRenamed = item->text();
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            taskListWidget->editItem(item);
            
            connect(taskListWidget, &QListWidget::itemChanged, this,
                    [this, taskListWidget, item](QListWidgetItem* changedItem) {
                        if (changedItem == item) {
                            disconnect(taskListWidget, &QListWidget::itemChanged, this, nullptr);
                            RenameTasklist(item);
                        }
                    });
        }
    });
    
    connect(deleteTaskListAction, &QAction::triggered, this, &Operations_TaskLists::DeleteTaskList);
    
    contextMenu.exec(taskListWidget->mapToGlobal(pos));
}
