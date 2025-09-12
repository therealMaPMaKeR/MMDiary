#include "operations_tasklists.h"
#include "CombinedDelegate.h"
#include "encryption/CryptoUtils.h"
#include "operations.h"
#include "inputvalidation.h"
#include "ui_mainwindow.h"
#include "../../constants.h"
#include "ui_tasklists_addtask.h"
#include "../../CustomWidgets/tasklists/qlist_TasklistDisplay.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMap>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QHeaderView>
#include <QFontMetrics>
#include <QUuid>
#include <QInputDialog>
#include <utility>  // For std::pair and std::make_pair
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "operations_files.h"

// Security: Centralized helper functions for task data
namespace TaskDataSecurity {
// Note: escapeTaskField and unescapeTaskField functions removed - no longer needed with JSON format

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
}

// JSON helper functions for task data
QJsonObject Operations_TaskLists::taskToJson(const QString& name, bool completed, const QString& completionDate,
                                             const QString& creationDate, const QString& description, const QString& id) {
    QJsonObject task;
    task["id"] = id.isEmpty() ? QUuid::createUuid().toString() : id;
    task["name"] = name;
    task["completed"] = completed;
    task["completionDate"] = completionDate;
    task["creationDate"] = creationDate;
    task["description"] = description;
    return task;
}

bool Operations_TaskLists::parseJsonTask(const QJsonObject& taskObj, QString& name, bool& completed,
                                        QString& completionDate, QString& creationDate, QString& description, QString& id) {
    if (!taskObj.contains("name") || !taskObj.contains("id")) {
        return false;
    }
    
    id = taskObj["id"].toString();
    name = taskObj["name"].toString();
    completed = taskObj["completed"].toBool();
    completionDate = taskObj["completionDate"].toString();
    creationDate = taskObj["creationDate"].toString();
    description = taskObj["description"].toString();
    
    return true;
}

bool Operations_TaskLists::readTasklistJson(const QString& filePath, QJsonArray& tasks) {
    qDebug() << "Operations_TaskLists: Reading JSON tasks from:" << filePath;
    
    // Create temporary directory
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("read_json", tempDir);
    
    // Decrypt the file
    if (!CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, filePath, tempPath)) {
        qWarning() << "Operations_TaskLists: Failed to decrypt tasklist file";
        return false;
    }
    
    // Open the decrypted file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: Failed to open decrypted file";
        return false;
    }
    
    // Skip the metadata header (512 bytes)
    tempFile.seek(METADATA_SIZE);
    
    // Read the JSON content
    QByteArray jsonData = tempFile.readAll();
    tempFile.close();
    QFile::remove(tempPath);
    
    if (jsonData.trimmed().isEmpty()) {
        // Empty task list
        tasks = QJsonArray();
        return true;
    }
    
    // Parse JSON
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) {
        qWarning() << "Operations_TaskLists: Invalid JSON format";
        return false;
    }
    
    QJsonObject root = doc.object();
    if (!root.contains("tasks") || !root["tasks"].isArray()) {
        qWarning() << "Operations_TaskLists: Missing or invalid tasks array";
        return false;
    }
    
    tasks = root["tasks"].toArray();
    return true;
}

bool Operations_TaskLists::writeTasklistJson(const QString& filePath, const QJsonArray& tasks) {
    qDebug() << "Operations_TaskLists: Writing JSON tasks to:" << filePath;
    
    // First read the existing metadata to preserve it
    QString tasklistName;
    QString lastSelectedTask;
    
    // Read existing metadata
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString readTempPath = TaskDataSecurity::generateSecureTempFileName("read_metadata_json", tempDir);
    
    // Decrypt file to read metadata
    if (CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, filePath, readTempPath)) {
        QFile readFile(readTempPath);
        if (readFile.open(QIODevice::ReadOnly)) {
            QByteArray metadataBytes = readFile.read(METADATA_SIZE);
            if (metadataBytes.size() == METADATA_SIZE) {
                const TasklistMetadata* oldMetadata = reinterpret_cast<const TasklistMetadata*>(metadataBytes.constData());
                
                // Extract tasklist name
                char nameBuffer[257];
                memcpy(nameBuffer, oldMetadata->name, 256);
                nameBuffer[256] = '\0';
                tasklistName = QString::fromUtf8(nameBuffer);
                
                // Extract lastSelectedTask
                char taskBuffer[129];
                memcpy(taskBuffer, oldMetadata->lastSelectedTask, 128);
                taskBuffer[128] = '\0';
                lastSelectedTask = QString::fromUtf8(taskBuffer);
            }
            readFile.close();
        }
        QFile::remove(readTempPath);
    }
    
    if (tasklistName.isEmpty()) {
        qWarning() << "Operations_TaskLists: Failed to read existing metadata";
        return false;
    }
    
    // Create temporary file for writing
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("write_json", tempDir);
    QFile tempFile(tempPath);
    
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Operations_TaskLists: Failed to open temp file for writing";
        return false;
    }
    
    // Write metadata header
    TasklistMetadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    
    // Set magic and version
    strncpy(metadata.magic, TASKLIST_MAGIC, 8);
    strncpy(metadata.version, TASKLIST_VERSION, 4);
    
    // Set tasklist name
    QByteArray nameBytes = tasklistName.toUtf8();
    int nameToCopy = qMin(nameBytes.size(), 255);
    memcpy(metadata.name, nameBytes.constData(), nameToCopy);
    
    // Set creation date
    QString creationDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QByteArray dateBytes = creationDate.toUtf8();
    int dateToCopy = qMin(dateBytes.size(), 31);
    memcpy(metadata.creationDate, dateBytes.constData(), dateToCopy);
    
    // Preserve lastSelectedTask
    if (!lastSelectedTask.isEmpty()) {
        QByteArray taskBytes = lastSelectedTask.toUtf8();
        int taskToCopy = qMin(taskBytes.size(), 127);
        memcpy(metadata.lastSelectedTask, taskBytes.constData(), taskToCopy);
    }
    
    // Write metadata to temp file
    tempFile.write(reinterpret_cast<const char*>(&metadata), METADATA_SIZE);
    
    // Create JSON document
    QJsonObject root;
    root["version"] = 2;
    root["tasks"] = tasks;
    
    QJsonDocument doc(root);
    tempFile.write(doc.toJson(QJsonDocument::Compact));
    
    tempFile.close();
    
    // Encrypt the temp file to the final location
    bool success = CryptoUtils::Encryption_EncryptFile(m_mainWindow->user_Key, tempPath, filePath);
    
    // Clean up temp file
    QFile::remove(tempPath);
    
    if (!success) {
        qWarning() << "Operations_TaskLists: Failed to encrypt tasklist file";
    }
    
    return success;
}


Operations_TaskLists::Operations_TaskLists(MainWindow* mainWindow)
    : m_mainWindow(mainWindow)
    , m_tasklistNameToFile(100, "TasklistNameToFile")  // Initialize thread-safe map
    , m_taskOrderCache(100, "TaskOrderCache")  // Initialize with max size and debug name
    , m_lastClickedWidget(nullptr)
    , m_lastClickedItem(nullptr)
{
    qDebug() << "Operations_TaskLists: Initializing";

    m_mainWindow->ui->treeWidget_TaskList_List->setSortingEnabled(false);

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
    m_descriptionSaveTimer = new SafeTimer(this, "Operations_TaskLists::DescriptionSaveTimer");
    m_descriptionSaveTimer->setSingleShot(true);
    m_descriptionSaveTimer->setInterval(5000); // 5 seconds
    connect(m_descriptionSaveTimer, &SafeTimer::timeout, this, &Operations_TaskLists::SaveTaskDescription);

    // Install event filters
    m_mainWindow->ui->plainTextEdit_TaskDesc->installEventFilter(this);
    m_mainWindow->ui->listWidget_TaskListDisplay->installEventFilter(this);
    m_mainWindow->ui->tableWidget_TaskDetails->installEventFilter(this);

    // Connect the context menu signal for the task list list widget
    connect(m_mainWindow->ui->treeWidget_TaskList_List, &QWidget::customContextMenuRequested,
            this, &Operations_TaskLists::showContextMenu_TaskListList);

    // Enable context menu policy for the task list list widget
    m_mainWindow->ui->treeWidget_TaskList_List->setContextMenuPolicy(Qt::CustomContextMenu);

    // Install event filters for key press events
    m_mainWindow->ui->treeWidget_TaskList_List->installEventFilter(this);
    m_mainWindow->ui->listWidget_TaskListDisplay->installEventFilter(this);

    // Connect item clicked signals to track the last clicked item
    if (auto* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List)) {
        connect(treeWidget, &QTreeWidget::itemClicked,
                this, [this, treeWidget](QTreeWidgetItem* item, int) {
                    // Only handle clicks on tasklists, not categories
                    if (!treeWidget->isCategory(item)) {
                        QString tasklistName = getTasklistNameFromTreeItem(item);
                        if (!tasklistName.isEmpty()) {
                            LoadIndividualTasklist(tasklistName, "NULL");
                        }
                    }
                });
                
        // Connect double-click for rename
        connect(treeWidget, &QTreeWidget::itemDoubleClicked,
                this, [this, treeWidget](QTreeWidgetItem* item, int) {
                    // Only allow rename on tasklists for now, not categories
                    if (!treeWidget->isCategory(item)) {
                        // TODO: Implement rename for tree items
                    }
                });
    }
    
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemClicked,
            this, &Operations_TaskLists::onTaskDisplayItemClicked);
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemDoubleClicked,
            this, &Operations_TaskLists::onTaskDisplayItemDoubleClicked);

    // Connect item changed signal for checkbox handling
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemChanged,
            this, [this](QListWidgetItem* item) {
                if (item) {
                    // Skip dummy item
                    if (item->data(Qt::UserRole + 999).toBool()) return;
                    
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
    m_mainWindow->ui->treeWidget_TaskList_List->setDragEnabled(true);
    m_mainWindow->ui->treeWidget_TaskList_List->setAcceptDrops(true);
    m_mainWindow->ui->treeWidget_TaskList_List->setDropIndicatorShown(true);
    m_mainWindow->ui->treeWidget_TaskList_List->setDragDropMode(QAbstractItemView::InternalMove);

    // Connect drag and drop signals
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &qlist_TasklistDisplay::itemsReordered,
            this, [this]() {
                // Handle task reordering with smart group detection
                HandleTaskReorder();
            });

    // Tree widget handles its own reordering internally
    // Structure changes are saved via the structureChanged signal connected above

    // Connect the Create Category button
    connect(m_mainWindow->ui->pushButton_Tasklists_NewCategory, &QPushButton::clicked,
            this, &Operations_TaskLists::CreateNewCategory);
    
    // Connect tree widget structure changes to save settings
    if (auto* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List)) {
        connect(treeWidget, &qtree_Tasklists_list::structureChanged,
                this, &Operations_TaskLists::SaveTasklistSettings);
    }
    
    LoadTasklists();
}

// Helper function to generate unique tasklist filename
QString Operations_TaskLists::generateTasklistFilename()
{
    // Generate UUID-based filename
    QString uuid = QUuid::createUuid().toString();
    // Remove braces from UUID
    uuid = uuid.mid(1, uuid.length() - 2);
    return QString("tasklist_%1.txt").arg(uuid);
}

// Write encrypted metadata header and content to a tasklist file
bool Operations_TaskLists::writeTasklistMetadata(const QString& filePath, const QString& tasklistName, const QByteArray& key)
{
    qDebug() << "Operations_TaskLists: Writing metadata for tasklist:" << tasklistName;
    
    // Create metadata structure
    TasklistMetadata metadata;
    memset(&metadata, 0, sizeof(metadata));  // Clear all to zero
    
    // Set magic and version
    strncpy(metadata.magic, TASKLIST_MAGIC, 8);
    strncpy(metadata.version, TASKLIST_VERSION, 4);
    
    // Set tasklist name (truncate if necessary)
    QByteArray nameBytes = tasklistName.toUtf8();
    int nameToCopy = qMin(nameBytes.size(), 255);  // Leave room for null terminator
    memcpy(metadata.name, nameBytes.constData(), nameToCopy);
    
    // Set creation date
    QString creationDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QByteArray dateBytes = creationDate.toUtf8();
    int dateToCopy = qMin(dateBytes.size(), 31);  // Leave room for null terminator
    memcpy(metadata.creationDate, dateBytes.constData(), dateToCopy);
    
    // lastSelectedTask is left empty (all zeros) for new tasklists
    
    // Convert metadata to byte array
    QByteArray metadataBytes(reinterpret_cast<const char*>(&metadata), METADATA_SIZE);
    
    // Create temporary file for writing
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("metadata", tempDir);
    QFile tempFile(tempPath);
    
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Operations_TaskLists: Failed to open temp file for metadata";
        return false;
    }
    
    // Write metadata (will be encrypted as part of the file)
    tempFile.write(metadataBytes);
    tempFile.close();
    
    // Encrypt the temp file to the target location
    bool success = CryptoUtils::Encryption_EncryptFile(key, tempPath, filePath);
    
    // Clean up temp file
    QFile::remove(tempPath);
    
    if (success) {
        // Update the name-to-file mapping
        m_tasklistNameToFile.insert(tasklistName, filePath);
    }
    
    return success;
}

// Read and decrypt metadata header from a tasklist file  
bool Operations_TaskLists::readTasklistMetadata(const QString& filePath, QString& tasklistName, const QByteArray& key)
{
    qDebug() << "Operations_TaskLists: Reading metadata from:" << filePath;
    
    // Create temporary file for decryption
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("metadata_read", tempDir);
    
    // Decrypt the file to temp location
    if (!CryptoUtils::Encryption_DecryptFile(key, filePath, tempPath)) {
        qWarning() << "Operations_TaskLists: Failed to decrypt tasklist file for metadata";
        return false;
    }
    
    // Read the metadata from decrypted file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: Failed to open decrypted file for metadata";
        return false;
    }
    
    // Read metadata bytes
    QByteArray metadataBytes = tempFile.read(METADATA_SIZE);
    tempFile.close();
    QFile::remove(tempPath);
    
    if (metadataBytes.size() != METADATA_SIZE) {
        qWarning() << "Operations_TaskLists: Invalid metadata size:" << metadataBytes.size();
        return false;
    }
    
    // Parse metadata
    const TasklistMetadata* metadata = reinterpret_cast<const TasklistMetadata*>(metadataBytes.constData());
    
    // Verify magic
    if (strncmp(metadata->magic, TASKLIST_MAGIC, 8) != 0) {
        qWarning() << "Operations_TaskLists: Invalid magic in metadata";
        return false;
    }
    
    // Verify version
    if (strncmp(metadata->version, TASKLIST_VERSION, 4) != 0) {
        qWarning() << "Operations_TaskLists: Unsupported version in metadata";
        return false;
    }
    
    // Extract name (ensure null-terminated)
    char nameBuffer[257];
    memcpy(nameBuffer, metadata->name, 256);
    nameBuffer[256] = '\0';
    tasklistName = QString::fromUtf8(nameBuffer);
    
    // Update the name-to-file mapping
    m_tasklistNameToFile.insert(tasklistName, filePath);
    
    return true;
}

// Find tasklist file by name
QString Operations_TaskLists::findTasklistFileByName(const QString& tasklistName)
{
    // Check cache first
    if (m_tasklistNameToFile.contains(tasklistName)) {
        auto cachedPath = m_tasklistNameToFile.value(tasklistName);
        if (cachedPath.has_value()) {
            return cachedPath.value();
        }
    }
    
    // If not in cache, scan all tasklist files
    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";
    QDir dir(tasksListsPath);
    QStringList filters;
    filters << "tasklist_*.txt";
    QStringList tasklistFiles = dir.entryList(filters, QDir::Files);
    
    for (const QString& filename : tasklistFiles) {
        QString filePath = tasksListsPath + filename;
        QString name;
        if (readTasklistMetadata(filePath, name, m_mainWindow->user_Key)) {
            if (name == tasklistName) {
                return filePath;
            }
        }
    }
    
    return QString();  // Not found
}

Operations_TaskLists::~Operations_TaskLists()
{
    qDebug() << "Operations_TaskLists: Destructor called";

    // Clear pointer references first to prevent use during destruction
    m_lastClickedWidget = nullptr;
    m_lastClickedItem = nullptr;

    // Disconnect all signals to prevent callbacks during destruction
    this->disconnect();

    // Stop and delete the timer first
    if (m_descriptionSaveTimer) {
        m_descriptionSaveTimer->stop();
        m_descriptionSaveTimer->disconnect();
        delete m_descriptionSaveTimer;
        m_descriptionSaveTimer = nullptr;
    }

    // Remove event filters with proper null checks
    // Since this object is parented to MainWindow, MainWindow and its UI
    // should still be valid when this destructor runs
    if (m_mainWindow && m_mainWindow->ui) {
        // Remove event filters from each widget if it exists
        if (m_mainWindow->ui->plainTextEdit_TaskDesc) {
            m_mainWindow->ui->plainTextEdit_TaskDesc->removeEventFilter(this);
        }
        if (m_mainWindow->ui->listWidget_TaskListDisplay) {
            m_mainWindow->ui->listWidget_TaskListDisplay->removeEventFilter(this);
        }
        if (m_mainWindow->ui->tableWidget_TaskDetails) {
            m_mainWindow->ui->tableWidget_TaskDetails->removeEventFilter(this);
        }
        if (m_mainWindow->ui->treeWidget_TaskList_List) {
            m_mainWindow->ui->treeWidget_TaskList_List->removeEventFilter(this);
        }
    }

    // Clear any sensitive string data
    TaskDataSecurity::secureStringClear(currentTaskToEdit);
    TaskDataSecurity::secureStringClear(currentTaskData);
    TaskDataSecurity::secureStringClear(m_currentTaskName);
    TaskDataSecurity::secureStringClear(m_lastSavedDescription);
    TaskDataSecurity::secureStringClear(currentTaskListBeingRenamed);
}

//--------Safe Container Operations Helpers--------//
QListWidgetItem* Operations_TaskLists::safeGetItem(QListWidget* widget, int index) const
{
    if (!validateListWidget(widget)) {
        qWarning() << "Operations_TaskLists: Invalid widget in safeGetItem";
        return nullptr;
    }

    if (index < 0 || index >= widget->count()) {
        qWarning() << "Operations_TaskLists: Index out of bounds in safeGetItem:" << index << "count:" << widget->count();
        return nullptr;
    }

    return widget->item(index);
}

QListWidgetItem* Operations_TaskLists::safeTakeItem(QListWidget* widget, int index)
{
    if (!validateListWidget(widget)) {
        qWarning() << "Operations_TaskLists: Invalid widget in safeTakeItem";
        return nullptr;
    }

    if (index < 0 || index >= widget->count()) {
        qWarning() << "Operations_TaskLists: Index out of bounds in safeTakeItem:" << index << "count:" << widget->count();
        return nullptr;
    }

    QListWidgetItem* item = widget->takeItem(index);
    if (!item) {
        qWarning() << "Operations_TaskLists: takeItem returned null at index:" << index;
    }

    return item;
}

bool Operations_TaskLists::validateListWidget(QListWidget* widget) const
{
    if (!widget) {
        qWarning() << "Operations_TaskLists: Null widget pointer";
        return false;
    }

    // Additional validation: check if widget is still valid (not deleted)
    if (!m_mainWindow) {
        qWarning() << "Operations_TaskLists: MainWindow is null";
        return false;
    }

    return true;
}

int Operations_TaskLists::safeGetItemCount(QListWidget* widget) const
{
    if (!validateListWidget(widget)) {
        return 0;
    }

    return widget->count();
}

// Custom file I/O that properly handles the 512-byte metadata header
bool Operations_TaskLists::readTasklistFileWithMetadata(const QString& filePath, QStringList& taskLines)
{
    qDebug() << "Operations_TaskLists: Reading tasklist file with metadata:" << filePath;
    taskLines.clear();
    
    // Decrypt the file to a temporary location
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("read_tasks", tempDir);
    
    if (!CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, filePath, tempPath)) {
        qWarning() << "Operations_TaskLists: Failed to decrypt tasklist file";
        return false;
    }
    
    // Read the decrypted file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: Failed to open decrypted file";
        return false;
    }
    
    // Skip the metadata header (512 bytes)
    tempFile.seek(METADATA_SIZE);
    
    // Read all task lines
    QTextStream stream(&tempFile);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (!line.isEmpty()) {
            taskLines.append(line);
        }
    }
    
    tempFile.close();
    QFile::remove(tempPath);
    
    return true;
}

// Update the last selected task in the metadata
bool Operations_TaskLists::updateLastSelectedTask(const QString& tasklistName, const QString& taskName)
{
    qDebug() << "Operations_TaskLists: Updating last selected task for" << tasklistName << "to" << taskName;
    
    // Find the tasklist file
    QString filePath = findTasklistFileByName(tasklistName);
    if (filePath.isEmpty()) {
        qWarning() << "Operations_TaskLists: Could not find tasklist file for" << tasklistName;
        return false;
    }
    
    // Create temporary directory
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("update_selection", tempDir);
    
    // Decrypt the file
    if (!CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, filePath, tempPath)) {
        qWarning() << "Operations_TaskLists: Failed to decrypt tasklist file";
        return false;
    }
    
    // Open the decrypted file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadWrite)) {
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: Failed to open temp file";
        return false;
    }
    
    // Read the entire file
    QByteArray allContent = tempFile.readAll();
    
    // Check if file has metadata
    if (allContent.size() < METADATA_SIZE) {
        tempFile.close();
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: File too small for metadata";
        return false;
    }
    
    // Parse metadata
    TasklistMetadata metadata;
    memcpy(&metadata, allContent.constData(), METADATA_SIZE);
    
    // Verify magic and version
    if (strncmp(metadata.magic, TASKLIST_MAGIC, 8) != 0) {
        tempFile.close();
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: Invalid magic number";
        return false;
    }
    
    // Update the lastSelectedTask field
    memset(metadata.lastSelectedTask, 0, 128);  // Clear existing
    QByteArray taskBytes = taskName.toUtf8();
    int taskToCopy = qMin(taskBytes.size(), 127);  // Leave room for null terminator
    memcpy(metadata.lastSelectedTask, taskBytes.constData(), taskToCopy);
    
    // Write back the updated metadata and the rest of the content
    tempFile.seek(0);
    tempFile.write(reinterpret_cast<const char*>(&metadata), METADATA_SIZE);
    tempFile.write(allContent.mid(METADATA_SIZE));  // Write the rest unchanged
    tempFile.close();
    
    // Re-encrypt the file
    bool success = CryptoUtils::Encryption_EncryptFile(m_mainWindow->user_Key, tempPath, filePath);
    
    // Clean up temp file
    QFile::remove(tempPath);
    
    if (!success) {
        qWarning() << "Operations_TaskLists: Failed to re-encrypt tasklist file";
    }
    
    return success;
}

bool Operations_TaskLists::writeTasklistFileWithMetadata(const QString& filePath, const QStringList& taskLines)
{
    qDebug() << "Operations_TaskLists: Writing tasklist file with metadata:" << filePath;
    
    // First read the existing metadata to preserve it
    QString tasklistName;
    QString lastSelectedTask;
    
    // Read existing metadata to preserve fields
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString readTempPath = TaskDataSecurity::generateSecureTempFileName("read_metadata_for_write", tempDir);
    
    // Decrypt file to read metadata
    if (CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, filePath, readTempPath)) {
        QFile readFile(readTempPath);
        if (readFile.open(QIODevice::ReadOnly)) {
            QByteArray metadataBytes = readFile.read(METADATA_SIZE);
            if (metadataBytes.size() == METADATA_SIZE) {
                const TasklistMetadata* oldMetadata = reinterpret_cast<const TasklistMetadata*>(metadataBytes.constData());
                
                // Extract tasklist name
                char nameBuffer[257];
                memcpy(nameBuffer, oldMetadata->name, 256);
                nameBuffer[256] = '\0';
                tasklistName = QString::fromUtf8(nameBuffer);
                
                // Extract lastSelectedTask
                char taskBuffer[129];
                memcpy(taskBuffer, oldMetadata->lastSelectedTask, 128);
                taskBuffer[128] = '\0';
                lastSelectedTask = QString::fromUtf8(taskBuffer);
            }
            readFile.close();
        }
        QFile::remove(readTempPath);
    }
    
    if (tasklistName.isEmpty()) {
        qWarning() << "Operations_TaskLists: Failed to read existing metadata";
        return false;
    }
    
    // Create temporary file for writing
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("write_tasks", tempDir);
    QFile tempFile(tempPath);
    
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Operations_TaskLists: Failed to open temp file for writing";
        return false;
    }
    
    // Write metadata header
    TasklistMetadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    
    // Set magic and version
    strncpy(metadata.magic, TASKLIST_MAGIC, 8);
    strncpy(metadata.version, TASKLIST_VERSION, 4);
    
    // Set tasklist name
    QByteArray nameBytes = tasklistName.toUtf8();
    int nameToCopy = qMin(nameBytes.size(), 255);
    memcpy(metadata.name, nameBytes.constData(), nameToCopy);
    
    // Set creation date (we could preserve the original, but for now use current)
    QString creationDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QByteArray dateBytes = creationDate.toUtf8();
    int dateToCopy = qMin(dateBytes.size(), 31);
    memcpy(metadata.creationDate, dateBytes.constData(), dateToCopy);
    
    // Preserve lastSelectedTask
    if (!lastSelectedTask.isEmpty()) {
        QByteArray taskBytes = lastSelectedTask.toUtf8();
        int taskToCopy = qMin(taskBytes.size(), 127);
        memcpy(metadata.lastSelectedTask, taskBytes.constData(), taskToCopy);
    }
    
    // Write metadata to temp file
    tempFile.write(reinterpret_cast<const char*>(&metadata), METADATA_SIZE);
    
    // Write task lines
    for (const QString& line : taskLines) {
        if (!line.isEmpty()) {
            tempFile.write(line.toUtf8());
            tempFile.write("\n");
        }
    }
    
    tempFile.close();
    
    // Encrypt the temp file to the final location
    bool success = CryptoUtils::Encryption_EncryptFile(m_mainWindow->user_Key, tempPath, filePath);
    
    // Clean up temp file
    QFile::remove(tempPath);
    
    if (!success) {
        qWarning() << "Operations_TaskLists: Failed to encrypt tasklist file";
    }
    
    return success;
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
            if (watched == m_mainWindow->ui->treeWidget_TaskList_List ||
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
    // This function is no longer used with tree widget
    // Tree widget uses QTreeWidgetItem*, not QListWidgetItem*
    // Keeping for compatibility if needed
    m_lastClickedWidget = m_mainWindow->ui->treeWidget_TaskList_List;
    m_lastClickedItem = item;
}

void Operations_TaskLists::onTaskDisplayItemClicked(QListWidgetItem* item)
{
    m_lastClickedWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    m_lastClickedItem = item;
    
    // Update the last selected task in metadata if it's a valid task
    if (item && (item->flags() & Qt::ItemIsEnabled) && item->text() != "No tasks in this list"
        && !item->data(Qt::UserRole + 999).toBool()) {  // Skip dummy item
        // Get current tasklist name
        qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
        if (treeWidget) {
            QTreeWidgetItem* currentItem = treeWidget->currentItem();
            if (currentItem && !treeWidget->isCategory(currentItem)) {
                QString tasklistName = getTasklistNameFromTreeItem(currentItem);
                QString taskName = item->text();
                updateLastSelectedTask(tasklistName, taskName);
            }
        }
    }
}

void Operations_TaskLists::onTaskListItemDoubleClicked(QListWidgetItem* item)
{
    // This function is no longer used with tree widget
    // Tree widget uses QTreeWidgetItem* for double-click events
    // The rename functionality is now handled directly in the tree widget
    if (!item) return;
    
    // Legacy code - may be called from old code paths
    currentTaskListBeingRenamed = item->text();
    
    // Cannot directly rename tree items using list widget methods
    qWarning() << "Operations_TaskLists: onTaskListItemDoubleClicked called but tree widget is in use";
    return;
}

void Operations_TaskLists::onTaskDisplayItemDoubleClicked(QListWidgetItem* item)
{
    if (!item || (item->flags() & Qt::ItemIsEnabled) == 0) return;
    
    // Skip dummy item
    if (item->data(Qt::UserRole + 999).toBool()) return;

    if (!validateListWidget(m_mainWindow->ui->listWidget_TaskListDisplay)) {
        qWarning() << "Operations_TaskLists: Invalid task display widget";
        return;
    }

    QListWidget* listWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    // Validate item still exists in the list
    bool itemExists = false;
    int itemRow = -1;
    int listCount = safeGetItemCount(listWidget);
    for (int i = 0; i < listCount; ++i) {
        QListWidgetItem* currentItem = safeGetItem(listWidget, i);
        if (currentItem && currentItem == item) {
            itemExists = true;
            itemRow = i;
            break;
        }
    }

    if (!itemExists) return;

    // Store the original task name for renaming
    currentTaskToEdit = item->text();
    currentTaskData = item->data(Qt::UserRole).toString();  // Store the task ID
    m_currentTaskName = item->text();

    // Make the item editable and start editing
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    listWidget->editItem(item);

    // Use a single-shot connection to handle the edit completion
    QMetaObject::Connection* conn = new QMetaObject::Connection();
    *conn = connect(listWidget, &QListWidget::itemChanged, this,
            [this, listWidget, itemRow, conn](QListWidgetItem* changedItem) {
                // Validate the item at the row is the one that changed
                int currentCount = safeGetItemCount(listWidget);
                if (itemRow >= 0 && itemRow < currentCount) {
                    QListWidgetItem* itemAtRow = safeGetItem(listWidget, itemRow);
                    if (itemAtRow && itemAtRow == changedItem) {
                        disconnect(*conn);
                        delete conn;
                        RenameTask(changedItem);
                    }
                }
            });
}

void Operations_TaskLists::handleDeleteKeyPress()
{
    if (!m_lastClickedWidget || !m_lastClickedItem) return;

    // Validate that the widget still exists and contains the item
    QListWidget* listWidget = qobject_cast<QListWidget*>(m_lastClickedWidget);
    if (!listWidget) {
        m_lastClickedWidget = nullptr;
        m_lastClickedItem = nullptr;
        return;
    }

    // Find the item in the widget to ensure it's still valid
    bool itemStillExists = false;
    int listCount = safeGetItemCount(listWidget);
    for (int i = 0; i < listCount; ++i) {
        QListWidgetItem* currentItem = safeGetItem(listWidget, i);
        if (currentItem && currentItem == m_lastClickedItem) {
            itemStillExists = true;
            break;
        }
    }

    if (!itemStillExists) {
        m_lastClickedWidget = nullptr;
        m_lastClickedItem = nullptr;
        return;
    }

    // Now safe to check flags
    if ((m_lastClickedItem->flags() & Qt::ItemIsEnabled) == 0) return;

    if (m_lastClickedWidget == m_mainWindow->ui->treeWidget_TaskList_List) {
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

    // Store the original task info
    currentTaskToEdit = selectedItem->text();
    currentTaskData = selectedItem->data(Qt::UserRole).toString();  // Get task ID
    m_currentTaskName = selectedItem->text();

    // Enable inline editing
    selectedItem->setFlags(selectedItem->flags() | Qt::ItemIsEditable);
    taskListWidget->editItem(selectedItem);

    // Get the row for validation
    int itemRow = taskListWidget->row(selectedItem);

    // Use a single-shot connection to handle the edit completion
    QMetaObject::Connection* conn = new QMetaObject::Connection();
    *conn = connect(taskListWidget, &QListWidget::itemChanged, this,
            [this, taskListWidget, itemRow, conn](QListWidgetItem* changedItem) {
                // Validate the item at the row is the one that changed
                int currentCount = safeGetItemCount(taskListWidget);
                if (itemRow >= 0 && itemRow < currentCount) {
                    QListWidgetItem* itemAtRow = safeGetItem(taskListWidget, itemRow);
                    if (itemAtRow && itemAtRow == changedItem) {
                        disconnect(*conn);
                        delete conn;
                        RenameTask(changedItem);
                    }
                }
            });
}

//--------Task List Display Functions--------//
void Operations_TaskLists::LoadIndividualTasklist(const QString& tasklistName, const QString& taskToSelect)
{
    qDebug() << "Operations_TaskLists: Loading tasklist:" << tasklistName << "with task to select:" << taskToSelect;
    
    QString actualTaskToSelect = taskToSelect;  // Will be modified if we read from metadata

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
    
    // IMPORTANT: Ensure the task list is selected in the UI widget
    // This is crucial for LoadTaskDetails to work properly, especially during app startup
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (treeWidget) {
        QTreeWidgetItem* tasklistItem = treeWidget->findTasklist(tasklistName);
        if (tasklistItem) {
            treeWidget->setCurrentItem(tasklistItem);
            qDebug() << "Operations_TaskLists: Set current task list in UI to:" << tasklistName;
            // Process events to ensure the UI is updated before we continue
            // This is important during app startup when LoadTaskDetails needs currentItem()
            QApplication::processEvents();
        } else {
            qWarning() << "Operations_TaskLists: Task list not found in UI widget:" << tasklistName;
        }
    }

    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(tasklistName);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find task list: " + tasklistName);
        return;
    }
    
    // If taskToSelect is empty or "NULL", try to read lastSelectedTask from metadata
    if (actualTaskToSelect.isEmpty() || actualTaskToSelect == "NULL") {
        // Read metadata to get lastSelectedTask
        QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
        if (OperationsFiles::ensureDirectoryExists(tempDir)) {
            QString tempPath = TaskDataSecurity::generateSecureTempFileName("read_last_selected", tempDir);
            
            if (CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, taskListFilePath, tempPath)) {
                QFile tempFile(tempPath);
                if (tempFile.open(QIODevice::ReadOnly)) {
                    // Read metadata
                    QByteArray metadataBytes = tempFile.read(METADATA_SIZE);
                    if (metadataBytes.size() == METADATA_SIZE) {
                        const TasklistMetadata* metadata = reinterpret_cast<const TasklistMetadata*>(metadataBytes.constData());
                        
                        // Extract lastSelectedTask (ensure null-terminated)
                        char taskBuffer[129];
                        memcpy(taskBuffer, metadata->lastSelectedTask, 128);
                        taskBuffer[128] = '\0';
                        QString lastSelectedTask = QString::fromUtf8(taskBuffer);
                        
                        if (!lastSelectedTask.isEmpty()) {
                            actualTaskToSelect = lastSelectedTask;
                            qDebug() << "Operations_TaskLists: Using lastSelectedTask from metadata:" << actualTaskToSelect;
                        }
                    }
                    tempFile.close();
                }
                QFile::remove(tempPath);
            }
        }
    }

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

    // Decrypt the file to read tasks
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                            "Could not create temporary directory.");
        return;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("load_tasklist", tempDir);
    
    if (!CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, taskListFilePath, tempPath)) {
        QMessageBox::warning(m_mainWindow, "Read Error",
                            "Could not decrypt the task list file.");
        return;
    }
    
    // Read the decrypted file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        QFile::remove(tempPath);
        QMessageBox::warning(m_mainWindow, "Read Error",
                            "Could not open decrypted task list file.");
        return;
    }
    
    // Skip the metadata header (512 bytes)
    tempFile.seek(METADATA_SIZE);
    
    // Read all task lines
    QTextStream stream(&tempFile);
    QStringList taskLines;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (!line.isEmpty()) {
            taskLines.append(line);
        }
    }
    
    tempFile.close();
    QFile::remove(tempPath);

    // Set the task list label with the name
    m_mainWindow->ui->label_TaskListName->setText(tasklistName);

    // Read JSON tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        qWarning() << "Operations_TaskLists: Failed to read JSON tasks";
        // Continue with empty task list
    }

    // Process each task from JSON
    for (const QJsonValue& value : tasks) {
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        
        // Parse task fields
        QString taskId, taskName, completionDate, creationDate, description;
        bool isCompleted;
        
        if (!parseJsonTask(taskObj, taskName, isCompleted, completionDate, creationDate, description, taskId)) {
            qWarning() << "Operations_TaskLists: Skipping invalid task object";
            continue;
        }
        
        // Validate task name
        if (taskName.trimmed().isEmpty()) {
            qWarning() << "Operations_TaskLists: Skipping task with empty name";
            continue;
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

        // Store the task ID for reference
        item->setData(Qt::UserRole, taskId);
        item->setData(Qt::UserRole + 1, QJsonDocument(taskObj).toJson(QJsonDocument::Compact));

        // Add the item to the list widget
        taskDisplayWidget->addItem(item);
    }

    // ALWAYS add an invisible dummy item to prevent single-item checkbox bug
    // This item is hidden and non-interactive
    QListWidgetItem* dummyItem = new QListWidgetItem("");  // Empty text
    dummyItem->setFlags(Qt::NoItemFlags);  // Make it completely non-interactive
    dummyItem->setData(Qt::UserRole + 999, true);  // Mark it as dummy item
    dummyItem->setSizeHint(QSize(0, 0));  // Make it have zero size - truly invisible
    taskDisplayWidget->addItem(dummyItem);
    
    // If the list is empty (excluding our dummy), display a message
    if (taskDisplayWidget->count() == 1) {  // Only our dummy item
        QListWidgetItem* item = new QListWidgetItem("No tasks in this list");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        taskDisplayWidget->insertItem(0, item);  // Insert before dummy

        // Clear the table
        m_mainWindow->ui->tableWidget_TaskDetails->clear();
        m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
        m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);
    } else {
        // Enforce task order to put checked tasks at the top
        EnforceTaskOrder();
    }

    // After loading all tasks, select the appropriate task
    int taskToSelectIndex = -1;
    int displayCount = safeGetItemCount(taskDisplayWidget);
    int realTaskCount = 0;  // Count of real tasks (excluding dummy)
    int lastRealTaskIndex = -1;

    // Count real tasks and find the task to select
    for (int i = 0; i < displayCount; ++i) {
        QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
        if (!item) continue;
        
        // Skip dummy item
        if (item->data(Qt::UserRole + 999).toBool()) continue;
        
        realTaskCount++;
        lastRealTaskIndex = i;  // Track the last real task index
        
        if (!actualTaskToSelect.isEmpty() && actualTaskToSelect != "NULL" && item->text() == actualTaskToSelect) {
            taskToSelectIndex = i;
        }
    }

    // If we didn't find the specified task or none was specified, select the last real item
    if (taskToSelectIndex == -1 && lastRealTaskIndex >= 0) {
        taskToSelectIndex = lastRealTaskIndex;
    }

    // If we have a valid index, select that item
    if (taskToSelectIndex >= 0 && taskToSelectIndex < displayCount) {
        taskDisplayWidget->setCurrentRow(taskToSelectIndex);
        QListWidgetItem* selectedItem = safeGetItem(taskDisplayWidget, taskToSelectIndex);

        if (selectedItem && (selectedItem->flags() & Qt::ItemIsEnabled)) {
            m_currentTaskName = selectedItem->text();
            qDebug() << "Operations_TaskLists: Selected task from metadata/parameter:" << m_currentTaskName;
            qDebug() << "Operations_TaskLists: Calling LoadTaskDetails for task:" << m_currentTaskName;
            LoadTaskDetails(selectedItem->text());

            // Scroll to the selected item (typically the last item) when loading a tasklist
            // Cast to our custom widget type to use explicit scrolling
            if (auto* customWidget = qobject_cast<qlist_TasklistDisplay*>(taskDisplayWidget)) {
                customWidget->scrollToItemExplicitly(selectedItem);
            }
        } else {
            qDebug() << "Operations_TaskLists: WARNING - Selected item is null or disabled, not loading task details";
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
        qDebug() << "Operations_TaskLists: ERROR - Invalid task name:" << nameResult.errorMessage;
        QMessageBox::warning(m_mainWindow, "Invalid Task Name", nameResult.errorMessage);
        return;
    }

    // Get current task list
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qDebug() << "Operations_TaskLists: ERROR - Failed to cast tree widget";
        return;
    }
    
    QTreeWidgetItem* currentTaskListItem = treeWidget->currentItem();
    if (!currentTaskListItem || treeWidget->isCategory(currentTaskListItem)) {
        qDebug() << "Operations_TaskLists: ERROR - No task list selected in UI, cannot load task details";
        // During startup, suppress the warning dialog
        if (m_mainWindow->initFinished) {
            QMessageBox::warning(m_mainWindow, "No Task List Selected",
                                "Please select a task list first.");
        }
        return;
    }
    
    QString currentTaskList = getTasklistNameFromTreeItem(currentTaskListItem);
    if (currentTaskList.isEmpty()) {
        qDebug() << "Operations_TaskLists: ERROR - Could not get tasklist name from tree item";
        return;
    }
    qDebug() << "Operations_TaskLists: Current task list in UI:" << currentTaskList;

    // Find the tasklist file by name using the cache
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find task list: " + currentTaskList);
        return;
    }

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

    QString tempPath = TaskDataSecurity::generateSecureTempFileName("load_task_details", tempDir);

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


    // Ensure table height is appropriate for current font size
    QFont currentFont = taskDetailsTable->font();
    QFontMetrics fm(currentFont);
    int rowHeight = fm.height() + ROW_PADDING;
    int headerHeight = fm.height() + HEADER_PADDING;
    int totalHeight = headerHeight + rowHeight + EXTRA_PADDING;

    if (totalHeight < MIN_TABLE_HEIGHT) {
        totalHeight = MIN_TABLE_HEIGHT;
    }
    if (totalHeight > MAX_TABLE_HEIGHT) {
        totalHeight = MAX_TABLE_HEIGHT;
    }

    taskDetailsTable->setMinimumHeight(totalHeight);
    taskDetailsTable->setMaximumHeight(totalHeight);

    // Open the decrypted file
    QFile file(tempPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QFile::remove(tempPath);
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not open task list file for reading.");
        return;
    }

    file.close();
    
    // Read JSON tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        qWarning() << "Operations_TaskLists: Failed to read JSON tasks for details";
        return;
    }

    QString taskDescription = "";
    bool taskFound = false;
    QString taskId;
    bool isCompleted = false;
    QString completionDateStr;
    QString creationDateStr;

    // Find the task by name
    for (const QJsonValue& value : tasks) {
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        QString currentTaskName = taskObj["name"].toString();
        
        if (currentTaskName == taskName) {
            taskFound = true;
            
            // Parse task data
            taskId = taskObj["id"].toString();
            currentTaskId = taskId;  // Store for later use
            isCompleted = taskObj["completed"].toBool();
            completionDateStr = taskObj["completionDate"].toString();
            creationDateStr = taskObj["creationDate"].toString();
            taskDescription = taskObj["description"].toString();
            
            // Store the current task data as JSON for later use
            currentTaskData = QJsonDocument(taskObj).toJson(QJsonDocument::Compact);
            
            break;
        }
    }
    
    if (!taskFound) {
        qWarning() << "Operations_TaskLists: Could not find the specified task in the task list.";
        return;
    }

    // Format the task details
    QString completionStatus = isCompleted ? "Completed" : "Pending";
    QDateTime creationDateTime = QDateTime::fromString(creationDateStr, Qt::ISODate);
    QString formattedCreationDate = FormatDateTime(creationDateTime);

    // Configure table
    int columnCount = isCompleted ? 3 : 2;
    taskDetailsTable->setColumnCount(columnCount);

    QStringList headers;
    headers << "Status";

    if (isCompleted) {
        headers << "Completion Date" << "Creation Date";
    } else {
        headers << "Creation Date";
    }

    taskDetailsTable->setHorizontalHeaderLabels(headers);
    taskDetailsTable->insertRow(0);

    // Completion Status
    QTableWidgetItem* statusItem = new QTableWidgetItem(completionStatus);
    if (isCompleted) {
        statusItem->setForeground(Qt::green);
    }
    taskDetailsTable->setItem(0, 0, statusItem);

    // If task is completed, add completion date
    if (isCompleted) {
        QDateTime completionDateTime = QDateTime::fromString(completionDateStr, Qt::ISODate);
        QString formattedCompletionDate = FormatDateTime(completionDateTime);
        taskDetailsTable->setItem(0, 1, new QTableWidgetItem(formattedCompletionDate));

        // Creation Date
        taskDetailsTable->setItem(0, 2, new QTableWidgetItem(formattedCreationDate));
    } else {
        // Creation Date
        taskDetailsTable->setItem(0, 1, new QTableWidgetItem(formattedCreationDate));
    }

    // Resize columns to content
    taskDetailsTable->resizeColumnsToContents();

    // Resize rows to content for proper height
    taskDetailsTable->resizeRowsToContents();

    // Make the last column stretch
    int lastColumn = taskDetailsTable->columnCount() - 1;
    taskDetailsTable->horizontalHeader()->setSectionResizeMode(lastColumn, QHeaderView::Stretch);
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

    // Cast to qtree_Tasklists_list
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list";
        return;
    }
    
    treeWidget->clear();
    
    // Clear the cache
    m_tasklistNameToFile.clear();

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

    // Try to load the tasklist settings (categories and structure)
    bool settingsLoaded = LoadTasklistSettings();

    // Get all tasklist files (new format: tasklist_*.txt)
    QDir tasksListsDir(tasksListsPath);
    QStringList filters;
    filters << "tasklist_*.txt";
    QStringList tasklistFiles = tasksListsDir.entryList(filters, QDir::Files);

    // Map to store tasklist names and their file paths
    QMap<QString, QString> tasklistNameToPath;
    QStringList allTasklistNames;

    // Read metadata from each file to get tasklist names
    for (const QString& filename : tasklistFiles) {
        QString filePath = tasksListsPath + filename;
        QString tasklistName;
        
        // Skip TasklistOrder.txt if it exists
        if (filename.toLower() == "tasklistorder.txt") {
            continue;
        }
        
        // Read metadata to get the actual tasklist name
        if (!readTasklistMetadata(filePath, tasklistName, m_mainWindow->user_Key)) {
            qWarning() << "Operations_TaskLists: Failed to read metadata from" << filename;
            continue;
        }
        
        tasklistNameToPath[tasklistName] = filePath;
        allTasklistNames.append(tasklistName);
    }

    // If settings weren't loaded or incomplete, add all tasklists to Uncategorized
    if (!settingsLoaded) {
        // Ensure Uncategorized category exists
        treeWidget->getOrCreateCategory("Uncategorized");
        
        // Add all tasklists to Uncategorized
        for (const QString& tasklistName : allTasklistNames) {
            treeWidget->addTasklist(tasklistName, "Uncategorized");
        }
    } else {
        // Settings were loaded, now add any tasklists that aren't in the settings
        // Get all tasklists currently in the tree
        QStringList tasklistsInTree;
        QStringList categories = treeWidget->getAllCategories();
        for (const QString& category : categories) {
            QStringList tasklistsInCategory = treeWidget->getTasklistsInCategory(category);
            tasklistsInTree.append(tasklistsInCategory);
        }
        
        // Add any tasklists that aren't in the tree to Uncategorized
        for (const QString& tasklistName : allTasklistNames) {
            if (!tasklistsInTree.contains(tasklistName)) {
                treeWidget->addTasklist(tasklistName, "Uncategorized");
            }
        }
    }
    
    // Update appearance of all tasklists
    for (const QString& tasklistName : allTasklistNames) {
        UpdateTasklistAppearance(tasklistName);
    }

    // Select the first tasklist if any exist
    if (!allTasklistNames.isEmpty()) {
        // Find the first tasklist item in the tree and select it
        QTreeWidgetItem* firstTasklist = treeWidget->findTasklist(allTasklistNames.first());
        if (firstTasklist) {
            treeWidget->setCurrentItem(firstTasklist);
        }
    }

    // Don't automatically load a task list here - let LoadPersistentSettings handle it
    // This prevents the issue where task details aren't shown on app startup
    // because the task list gets loaded twice with different parameters
}

void Operations_TaskLists::CreateNewTaskList()
{
    qDebug() << "Operations_TaskLists: Creating new task list";

    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list";
        return;
    }

    // Get all existing tasklist names
    QStringList existingNames;
    QStringList categories = treeWidget->getAllCategories();
    for (const QString& category : categories) {
        QStringList tasklistsInCategory = treeWidget->getTasklistsInCategory(category);
        existingNames.append(tasklistsInCategory);
    }

    // Get a unique name for the new task list
    QString initialName = "New Task List";
    QString uniqueName = Operations::GetUniqueItemName(initialName, existingNames);

    // Add the new tasklist to Uncategorized category
    QTreeWidgetItem* newItem = treeWidget->addTasklist(uniqueName, "Uncategorized");
    if (!newItem) {
        qWarning() << "Operations_TaskLists: Failed to add new tasklist to tree";
        return;
    }

    // Create the task list directory and file
    CreateTaskListFile(uniqueName);

    // Select the new item
    treeWidget->setCurrentItem(newItem);
    
    // Save the updated settings
    SaveTasklistSettings();
    
    // TODO: Enable inline editing of the new tasklist name
    // For now, the user can use right-click rename later
    
    // Load the newly created tasklist
    LoadIndividualTasklist(uniqueName, "NULL");
}

void Operations_TaskLists::CreateTaskListFile(const QString& listName)
{
    qDebug() << "Operations_TaskLists: Creating task list file for:" << listName;

    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";
    
    // Generate UUID-based filename
    QString filename = generateTasklistFilename();
    QString taskListFilePath = tasksListsPath + filename;

    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid Path",
                            "Cannot create task list file: Invalid path");
        return;
    }

    // Ensure the Tasklists directory exists
    if (!OperationsFiles::ensureDirectoryExists(tasksListsPath)) {
        QMessageBox::warning(m_mainWindow, "Directory Creation Failed",
                            "Failed to create directory for task lists.");
        return;
    }

    // Create temporary file for writing metadata and initial content
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                            "Could not create temporary directory.");
        return;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("new_tasklist", tempDir);
    QFile tempFile(tempPath);
    
    if (!tempFile.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not create temporary file.");
        return;
    }
    
    // Create and write metadata
    TasklistMetadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    
    // Set magic and version
    strncpy(metadata.magic, TASKLIST_MAGIC, 8);
    strncpy(metadata.version, TASKLIST_VERSION, 4);
    
    // Set tasklist name
    QByteArray nameBytes = listName.toUtf8();
    int nameToCopy = qMin(nameBytes.size(), 255);
    memcpy(metadata.name, nameBytes.constData(), nameToCopy);
    
    // Set creation date
    QString creationDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QByteArray dateBytes = creationDate.toUtf8();
    int dateToCopy = qMin(dateBytes.size(), 31);
    memcpy(metadata.creationDate, dateBytes.constData(), dateToCopy);
    
    // Write metadata to temp file
    tempFile.write(reinterpret_cast<const char*>(&metadata), METADATA_SIZE);
    
    // Write initial JSON structure with empty tasks array
    QJsonObject root;
    root["version"] = 2;
    root["tasks"] = QJsonArray();  // Empty task array
    
    QJsonDocument doc(root);
    tempFile.write(doc.toJson(QJsonDocument::Compact));
    
    tempFile.close();
    
    // Encrypt the temp file to the final location
    if (!CryptoUtils::Encryption_EncryptFile(m_mainWindow->user_Key, tempPath, taskListFilePath)) {
        QFile::remove(tempPath);
        QMessageBox::warning(m_mainWindow, "File Creation Failed",
                            "Failed to create encrypted task list file.");
        return;
    }
    
    // Clean up temp file
    QFile::remove(tempPath);
    
    // Update the cache
    m_tasklistNameToFile.insert(listName, taskListFilePath);
    
    // Load the newly created tasklist
    LoadIndividualTasklist(listName, "NULL");
}

void Operations_TaskLists::CreateNewTask()
{
    qDebug() << "Operations_TaskLists: Creating new task with inline editing";

    // Get current task list
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget";
        return;
    }
    
    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }
    
    QString currentTaskList = getTasklistNameFromTreeItem(currentItem);

    // Find the tasklist file
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the selected task list.");
        return;
    }

    // Read existing tasks to check for duplicates
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        // If reading fails, start with empty array
        tasks = QJsonArray();
    }

    // Build list of existing task names
    QStringList existingNames;
    for (const QJsonValue& value : tasks) {
        if (!value.isObject()) continue;
        QJsonObject taskObj = value.toObject();
        QString taskName = taskObj["name"].toString();
        if (!taskName.isEmpty()) {
            existingNames.append(taskName);
        }
    }

    // Get a unique name for the new task
    QString initialName = "New Task";
    QString uniqueName = Operations::GetUniqueItemName(initialName, existingNames);

    // Create new task object with unique name
    QString taskId = QUuid::createUuid().toString();
    QString creationDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QJsonObject newTask = taskToJson(uniqueName, false, "", creationDate, "", taskId);
    
    // Add the new task to the array
    tasks.append(newTask);
    
    // Write back the updated tasks
    if (!writeTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not save the new task.");
        return;
    }

    // Reload the task list to show the new task
    LoadIndividualTasklist(currentTaskList, uniqueName);

    // Find the new task item in the display list and enable inline editing
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QListWidgetItem* newTaskItem = nullptr;
    
    int taskCount = safeGetItemCount(taskDisplayWidget);
    for (int i = 0; i < taskCount; ++i) {
        QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
        if (item && item->text() == uniqueName) {
            newTaskItem = item;
            break;
        }
    }

    if (!newTaskItem) {
        qWarning() << "Operations_TaskLists: Could not find newly created task item for editing";
        return;
    }

    // Store the task info for editing
    currentTaskToEdit = uniqueName;
    currentTaskData = taskId;
    m_currentTaskName = uniqueName;

    // Enable inline editing
    newTaskItem->setFlags(newTaskItem->flags() | Qt::ItemIsEditable);
    taskDisplayWidget->editItem(newTaskItem);

    // Get the row for validation
    int itemRow = taskDisplayWidget->row(newTaskItem);

    // Use a single-shot connection to handle the edit completion
    QMetaObject::Connection* conn = new QMetaObject::Connection();
    *conn = connect(taskDisplayWidget, &QListWidget::itemChanged, this,
            [this, taskDisplayWidget, itemRow, conn, uniqueName](QListWidgetItem* changedItem) {
                // Validate the item at the row is the one that changed
                int currentCount = safeGetItemCount(taskDisplayWidget);
                if (itemRow >= 0 && itemRow < currentCount) {
                    QListWidgetItem* itemAtRow = safeGetItem(taskDisplayWidget, itemRow);
                    if (itemAtRow && itemAtRow == changedItem) {
                        disconnect(*conn);
                        delete conn;
                        
                        // Handle the rename with automatic duplicate numbering
                        QString newName = changedItem->text().trimmed();
                        
                        // If the user cleared the name or left it as the unique name, just finish
                        if (newName.isEmpty() || newName == uniqueName) {
                            changedItem->setFlags(changedItem->flags() & ~Qt::ItemIsEditable);
                            return;
                        }
                        
                        // Get current task list to check for duplicates
                        qtree_Tasklists_list* treeWidget = m_mainWindow->ui->treeWidget_TaskList_List ? 
                            qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List) : nullptr;
                        if (!treeWidget) {
                            changedItem->setFlags(changedItem->flags() & ~Qt::ItemIsEditable);
                            return;
                        }
                        
                        QTreeWidgetItem* currentItem = treeWidget->currentItem();
                        if (!currentItem || treeWidget->isCategory(currentItem)) {
                            changedItem->setFlags(changedItem->flags() & ~Qt::ItemIsEditable);
                            return;
                        }
                        
                        QString currentTaskList = getTasklistNameFromTreeItem(currentItem);
                        QString taskListFilePath = findTasklistFileByName(currentTaskList);
                        if (taskListFilePath.isEmpty()) {
                            changedItem->setFlags(changedItem->flags() & ~Qt::ItemIsEditable);
                            return;
                        }
                        
                        // Read existing tasks
                        QJsonArray tasks;
                        if (!readTasklistJson(taskListFilePath, tasks)) {
                            changedItem->setFlags(changedItem->flags() & ~Qt::ItemIsEditable);
                            return;
                        }
                        
                        // Build list of existing names (excluding the current task)
                        QStringList existingNames;
                        for (const QJsonValue& value : tasks) {
                            if (!value.isObject()) continue;
                            QJsonObject taskObj = value.toObject();
                            QString taskName = taskObj["name"].toString();
                            QString taskId = taskObj["id"].toString();
                            // Exclude the current task being renamed
                            if (!taskName.isEmpty() && taskId != currentTaskData) {
                                existingNames.append(taskName);
                            }
                        }
                        
                        // Get unique name if there's a duplicate
                        QString finalName = Operations::GetUniqueItemName(newName, existingNames);
                        if (finalName != newName) {
                            changedItem->setText(finalName);
                        }
                        
                        // Now perform the actual rename
                        RenameTask(changedItem);
                    }
                }
            });
}

void Operations_TaskLists::DeleteTaskList()
{
    qDebug() << "Operations_TaskLists: Deleting task list";

    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list in DeleteTaskList";
        return;
    }

    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list to delete.");
        return;
    }

    QString taskListName = currentItem->text(0);

    // Confirm deletion
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(m_mainWindow, "Confirm Deletion",
                                  "Are you sure you want to delete the task list \"" + taskListName + "\"?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    // Find the tasklist file
    QString taskListFilePath = findTasklistFileByName(taskListName);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the task list file.");
        return;
    }

    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(m_mainWindow, "Invalid File",
                            "Task list file does not exist.");
        return;
    }

    // Delete the file
    if (!QFile::remove(taskListFilePath)) {
        QMessageBox::warning(m_mainWindow, "Delete Failed",
                            "Could not delete the task list file.");
        return;
    }
    
    // Remove from cache
    m_tasklistNameToFile.remove(taskListName);

    // Get parent category before deletion
    QTreeWidgetItem* parentCategory = currentItem->parent();
    
    // Remove the item from the tree
    if (parentCategory) {
        parentCategory->removeChild(currentItem);
    } else {
        // This shouldn't happen for tasklists, but handle it anyway
        int index = treeWidget->indexOfTopLevelItem(currentItem);
        if (index >= 0) {
            treeWidget->takeTopLevelItem(index);
        }
    }
    delete currentItem;

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
    // Try to select another tasklist in the same category
    if (parentCategory && parentCategory->childCount() > 0) {
        QTreeWidgetItem* nextItem = parentCategory->child(0);
        if (nextItem) {
            treeWidget->setCurrentItem(nextItem);
            QString nextTasklistName = nextItem->text(0);
            LoadIndividualTasklist(nextTasklistName, "NULL");
        }
    } else {
        // Try to find any other tasklist in any category
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem* category = treeWidget->topLevelItem(i);
            if (category && category->childCount() > 0) {
                QTreeWidgetItem* firstTasklist = category->child(0);
                if (firstTasklist) {
                    treeWidget->setCurrentItem(firstTasklist);
                    QString tasklistName = firstTasklist->text(0);
                    LoadIndividualTasklist(tasklistName, "NULL");
                    break;
                }
            }
        }
    }
    
    // Save the updated structure
    SaveTasklistSettings();
}

void Operations_TaskLists::RenameTasklist(QListWidgetItem* item)
{
    // This function is called with QListWidgetItem* but we use tree widget now
    // This should be refactored to use QTreeWidgetItem*
    qDebug() << "Operations_TaskLists: RenameTasklist called with QListWidgetItem - needs refactoring";
    
    if (!item) return;
    
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

    // Get existing names from tree widget
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in RenameTasklist";
        item->setText(originalName);
        return;
    }
    
    QStringList existingNames;
    QStringList categories = treeWidget->getAllCategories();
    for (const QString& category : categories) {
        QStringList tasklistsInCategory = treeWidget->getTasklistsInCategory(category);
        for (const QString& tasklistName : tasklistsInCategory) {
            if (tasklistName != originalName) {
                existingNames.append(tasklistName);
            }
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

    // Find the tasklist file
    QString taskListFilePath = findTasklistFileByName(originalName);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the task list file.");
        item->setText(originalName);
        return;
    }

    // Decrypt the file to update metadata
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                            "Could not create temporary directory.");
        item->setText(originalName);
        return;
    }

    QString tempPath = TaskDataSecurity::generateSecureTempFileName("rename", tempDir);

    // Decrypt the file
    if (!CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, taskListFilePath, tempPath)) {
        QMessageBox::warning(m_mainWindow, "Decryption Failed",
                            "Could not decrypt task list file.");
        item->setText(originalName);
        return;
    }

    // Read the file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadWrite)) {
        QFile::remove(tempPath);
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not open task list file.");
        item->setText(originalName);
        return;
    }

    // Read all content
    QByteArray allContent = tempFile.readAll();
    
    // Update metadata header with new name
    TasklistMetadata metadata;
    if (allContent.size() >= METADATA_SIZE) {
        memcpy(&metadata, allContent.constData(), METADATA_SIZE);
        
        // Update the name field
        memset(metadata.name, 0, 256);
        QByteArray nameBytes = newName.toUtf8();
        int nameToCopy = qMin(nameBytes.size(), 255);
        memcpy(metadata.name, nameBytes.constData(), nameToCopy);
        
        // Write back the updated metadata
        tempFile.seek(0);
        tempFile.write(reinterpret_cast<const char*>(&metadata), METADATA_SIZE);
        tempFile.write(allContent.mid(METADATA_SIZE));  // Write the rest of the content
    }
    
    tempFile.close();

    // Re-encrypt the file
    if (!CryptoUtils::Encryption_EncryptFile(m_mainWindow->user_Key, tempPath, taskListFilePath)) {
        QFile::remove(tempPath);
        QMessageBox::warning(m_mainWindow, "Encryption Failed",
                            "Could not save the renamed task list.");
        item->setText(originalName);
        return;
    }

    QFile::remove(tempPath);
    
    // Update cache
    m_tasklistNameToFile.remove(originalName);
    m_tasklistNameToFile.insert(newName, taskListFilePath);

    // Find and update the tree item
    QTreeWidgetItem* treeItem = treeWidget->findTasklist(originalName);
    if (treeItem) {
        treeItem->setText(0, newName);
        treeWidget->setCurrentItem(treeItem);
        LoadIndividualTasklist(newName, m_currentTaskName);
    }
    
    // Save the updated settings
    SaveTasklistSettings();
}

//--------Task Operations--------//
void Operations_TaskLists::ShowTaskMenu(bool editMode)
{
    qDebug() << "Operations_TaskLists: Showing task menu, editMode:" << editMode;

    // Get current task list
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in ShowTaskMenu";
        return;
    }
    
    QTreeWidgetItem* currentTaskListItem = treeWidget->currentItem();
    if (!currentTaskListItem || treeWidget->isCategory(currentTaskListItem)) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }

    QString currentTaskList = currentTaskListItem->text(0);

    // Create and configure the dialog
    QDialog dialog(m_mainWindow);
    Ui::Tasklists_AddTask ui;
    ui.setupUi(&dialog);

    // Set window title based on mode
    dialog.setWindowTitle(editMode ? "Edit Task" : "New Task");

    // If editing, populate the fields
    if (editMode) {
        // Parse the current task data
        // New format: TaskName|CompletionStatus|CompletionDate|CreationDate|DESC:Description
        QStringList parts = currentTaskData.split('|');
        if (parts.size() >= 1) {
            QString taskName = parts[0];  // TaskName is now at index 0
            taskName.replace("\\|", "|");
            ui.lineEdit_TaskName->setText(taskName);

            // Load description if available (now at index 4)
            if (parts.size() > 4 && parts[4].startsWith("DESC:")) {
                QString description = parts[4].mid(5);
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
    else {
        // Reload the task list to fix a bug with double click on checkbox when only one item is in the list
        LoadIndividualTasklist(currentTaskList, "NULL");
    }
}

void Operations_TaskLists::AddTaskSimple(QString taskName, QString description)
{
    qDebug() << "Operations_TaskLists: Adding task:" << taskName;

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
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in AddTaskSimple";
        return;
    }
    
    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }

    QString currentTaskList = currentItem->text(0);

    // Find the tasklist file
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the selected task list.");
        return;
    }

    // Check for duplicate task names and automatically append number if needed
    if (checkDuplicateTaskName(taskName, taskListFilePath)) {
        // Build list of existing task names
        QJsonArray existingTasks;
        if (!readTasklistJson(taskListFilePath, existingTasks)) {
            existingTasks = QJsonArray();
        }
        
        QStringList existingNames;
        for (const QJsonValue& value : existingTasks) {
            if (!value.isObject()) continue;
            QJsonObject taskObj = value.toObject();
            QString existingTaskName = taskObj["name"].toString();
            if (!existingTaskName.isEmpty()) {
                existingNames.append(existingTaskName);
            }
        }
        
        // Get unique name with number appended
        taskName = Operations::GetUniqueItemName(taskName, existingNames);
    }

    // Read existing tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        // If reading fails, start with empty array
        tasks = QJsonArray();
    }

    // Create new task object
    QString taskId = QUuid::createUuid().toString();
    QString creationDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QJsonObject newTask = taskToJson(taskName, false, "", creationDate, description, taskId);
    
    // Add the new task to the array
    tasks.append(newTask);
    
    // Write back the updated tasks
    if (!writeTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not save the updated task list.");
        return;
    }

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
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in ModifyTaskSimple";
        return;
    }
    
    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }

    QString currentTaskList = currentItem->text(0);

    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the selected task list.");
        return;
    }

    // Check for duplicate task names (if name changed) and automatically append number if needed
    if (originalTaskName != taskName && checkDuplicateTaskName(taskName, taskListFilePath, currentTaskId)) {
        // Build list of existing task names (excluding the current task)
        QJsonArray existingTasks;
        if (!readTasklistJson(taskListFilePath, existingTasks)) {
            QMessageBox::warning(m_mainWindow, "File Error",
                                "Could not read the task list file.");
            return;
        }
        
        QStringList existingNames;
        for (const QJsonValue& value : existingTasks) {
            if (!value.isObject()) continue;
            QJsonObject taskObj = value.toObject();
            QString existingTaskName = taskObj["name"].toString();
            QString existingTaskId = taskObj["id"].toString();
            // Exclude the current task being modified
            if (!existingTaskName.isEmpty() && existingTaskId != currentTaskId) {
                existingNames.append(existingTaskName);
            }
        }
        
        // Get unique name with number appended
        taskName = Operations::GetUniqueItemName(taskName, existingNames);
    }

    // Read existing tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not read the task list file.");
        return;
    }
    
    // Find and modify the task
    bool taskFound = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonValue value = tasks[i];
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        if (taskObj["name"].toString() == originalTaskName) {
            // Preserve existing fields but update name and description
            taskObj["name"] = taskName;
            taskObj["description"] = description;
            
            // Update the task in the array
            tasks[i] = taskObj;
            taskFound = true;
            break;
        }
    }
    
    if (!taskFound) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                            "Could not find the task to modify.");
        return;
    }
    
    // Write back the updated tasks
    if (!writeTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not save the modified task list.");
        return;
    }

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
    QListWidget* taskListWidget = m_mainWindow->ui->treeWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the selected task list.");
        return;
    }

    // Read existing tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not read the task list file.");
        return;
    }

    // Find and remove the task
    bool taskFound = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonValue value = tasks[i];
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        if (taskObj["name"].toString() == taskName) {
            tasks.removeAt(i);
            taskFound = true;
            break;
        }
    }

    if (!taskFound) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                            "Could not find the task to delete.");
        return;
    }

    // Write back the updated tasks
    if (!writeTasklistJson(taskListFilePath, tasks)) {
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

void Operations_TaskLists::RenameTask(QListWidgetItem* item)
{
    qDebug() << "Operations_TaskLists: Renaming task";

    Qt::ItemFlags originalFlags = item->flags();
    QString originalName = currentTaskToEdit;
    QString newName = item->text().trimmed();
    QString taskId = currentTaskData;  // This contains the task ID

    // Validate the new task name
    InputValidation::ValidationResult result =
        InputValidation::validateInput(newName, InputValidation::InputType::PlainText);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name", result.errorMessage);
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    if (newName.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Empty Task Name",
                            "Task name cannot be empty.");
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    // If name hasn't changed, just restore flags and return
    if (newName == originalName) {
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    // Get current task list from tree widget
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in RenameTask";
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }
    
    QTreeWidgetItem* currentTaskListItem = treeWidget->currentItem();
    if (!currentTaskListItem || treeWidget->isCategory(currentTaskListItem)) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                            "Please select a task list first.");
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    QString currentTaskList = currentTaskListItem->text(0);

    // Find the tasklist file
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Task List Not Found",
                            "Could not find the task list file.");
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    // Check for duplicate task names and automatically append number if needed
    if (checkDuplicateTaskName(newName, taskListFilePath, taskId)) {
        // Build list of existing task names (excluding the current task)
        QJsonArray tasks;
        if (!readTasklistJson(taskListFilePath, tasks)) {
            item->setText(originalName);
            item->setFlags(originalFlags & ~Qt::ItemIsEditable);
            return;
        }
        
        QStringList existingNames;
        for (const QJsonValue& value : tasks) {
            if (!value.isObject()) continue;
            QJsonObject taskObj = value.toObject();
            QString taskName = taskObj["name"].toString();
            QString existingTaskId = taskObj["id"].toString();
            // Exclude the current task being renamed
            if (!taskName.isEmpty() && existingTaskId != taskId) {
                existingNames.append(taskName);
            }
        }
        
        // Get unique name with number appended
        QString uniqueName = Operations::GetUniqueItemName(newName, existingNames);
        newName = uniqueName;
        item->setText(uniqueName);
    }

    // Read existing tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not read the task list file.");
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    // Find and update the task by ID
    bool taskFound = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonValue value = tasks[i];
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        // Find by ID for safety (in case there are duplicate names somehow)
        if (taskObj["id"].toString() == taskId) {
            // Update the task name
            taskObj["name"] = newName;
            tasks[i] = taskObj;
            taskFound = true;
            break;
        }
    }

    if (!taskFound) {
        // Fallback: try to find by name
        for (int i = 0; i < tasks.size(); ++i) {
            QJsonValue value = tasks[i];
            if (!value.isObject()) continue;
            
            QJsonObject taskObj = value.toObject();
            if (taskObj["name"].toString() == originalName) {
                // Update the task name
                taskObj["name"] = newName;
                tasks[i] = taskObj;
                taskFound = true;
                break;
            }
        }
    }

    if (!taskFound) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                            "Could not find the task to rename.");
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    // Write back the updated tasks
    if (!writeTasklistJson(taskListFilePath, tasks)) {
        QMessageBox::warning(m_mainWindow, "File Error",
                            "Could not save the renamed task.");
        item->setText(originalName);
        item->setFlags(originalFlags & ~Qt::ItemIsEditable);
        return;
    }

    // Update the current task name if this was the selected task
    if (m_currentTaskName == originalName) {
        m_currentTaskName = newName;
    }

    // Restore item flags (remove editable flag)
    item->setFlags(originalFlags & ~Qt::ItemIsEditable);

    // Reload task details if this is the currently selected task
    if (item == m_mainWindow->ui->listWidget_TaskListDisplay->currentItem()) {
        LoadTaskDetails(newName);
    }
}

void Operations_TaskLists::SetTaskStatus(bool checked, QListWidgetItem* item)
{
    qDebug() << "Operations_TaskLists: Setting task status, checked:" << checked;

    if (!item) {
        if (!validateListWidget(m_mainWindow->ui->listWidget_TaskListDisplay)) {
            qWarning() << "Operations_TaskLists: Invalid task display widget";
            return;
        }

        QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
        item = taskDisplayWidget->currentItem();

        if (!item) return;
    }

    QString taskName = item->text();
    QString taskData = item->data(Qt::UserRole).toString();

    // Get current task list
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in SetTaskStatus";
        return;
    }
    
    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) return;

    QString currentTaskList = currentItem->text(0);

    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        qWarning() << "Operations_TaskLists: Could not find task list file for SetTaskStatus";
        return;
    }

    // Read existing tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        qWarning() << "Operations_TaskLists: Failed to read tasks for status update";
        return;
    }
    
    // Find and update the task
    bool taskFound = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonValue value = tasks[i];
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        if (taskObj["name"].toString() == taskName) {
            // Update completion status and date
            taskObj["completed"] = checked;
            taskObj["completionDate"] = checked ? QDateTime::currentDateTime().toString(Qt::ISODate) : "";
            
            // Update the task in the array
            tasks[i] = taskObj;
            taskFound = true;
            break;
        }
    }
    
    if (!taskFound) {
        qWarning() << "Operations_TaskLists: Task not found for status update:" << taskName;
        return;
    }
    
    // Write back the updated tasks
    if (!writeTasklistJson(taskListFilePath, tasks)) {
        qWarning() << "Operations_TaskLists: Failed to write updated tasks";
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

void Operations_TaskLists::HandleTaskReorder()
{
    qDebug() << "Operations_TaskLists: Handling task reorder";

    if (!validateListWidget(m_mainWindow->ui->listWidget_TaskListDisplay)) {
        qWarning() << "Operations_TaskLists: Invalid task display widget";
        return;
    }

    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    // Check the current order and detect if any tasks moved between groups
    int itemCount = safeGetItemCount(taskDisplayWidget);
    if (itemCount == 0) return;

    // Check if the groups are properly separated
    bool needsReordering = false;
    int lastCompletedIndex = -1;

    for (int i = 0; i < itemCount; ++i) {
        QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
        if (!item) continue;

        // Skip dummy item
        if (item->data(Qt::UserRole + 999).toBool()) continue;
        
        // Skip disabled items
        if ((item->flags() & Qt::ItemIsEnabled) == 0) continue;

        if (item->checkState() == Qt::Checked) {
            // This is a completed task
            if (lastCompletedIndex != -1 && lastCompletedIndex < i - 1) {
                // There was a gap (pending task) between completed tasks
                needsReordering = true;
                break;
            }
            lastCompletedIndex = i;
        } else {
            // This is a pending task
            // If we haven't seen any completed tasks yet, that's fine
            // But if we have, and now we see a completed task later, that's bad
        }
    }

    // Also check if any completed tasks come after pending tasks
    if (!needsReordering) {
        bool inPendingSection = false;
        for (int i = 0; i < itemCount; ++i) {
            QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
            if (!item) continue;

            // Skip dummy item
            if (item->data(Qt::UserRole + 999).toBool()) continue;
            
            // Skip disabled items
            if ((item->flags() & Qt::ItemIsEnabled) == 0) continue;

            if (item->checkState() != Qt::Checked) {
                inPendingSection = true;
            } else if (inPendingSection) {
                // Found a completed task after we entered the pending section
                needsReordering = true;
                break;
            }
        }
    }

    if (needsReordering) {
        qDebug() << "Operations_TaskLists: Groups are mixed, enforcing proper order";
        // Groups are mixed, need to enforce proper ordering
        // This will also save the order
        EnforceTaskOrder();
    } else {
        qDebug() << "Operations_TaskLists: Groups are properly separated, saving order";
        // Groups are properly separated, just save the new order
        SaveTaskOrder();
    }
}

void Operations_TaskLists::SaveTaskOrder()
{
    qDebug() << "Operations_TaskLists: Saving task order";

    if (!validateListWidget(m_mainWindow->ui->listWidget_TaskListDisplay)) {
        qWarning() << "Operations_TaskLists: Invalid task display widget";
        return;
    }

    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    // Get current task list from tree widget
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in SaveTaskOrder";
        return;
    }
    
    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) {
        qDebug() << "Operations_TaskLists: No task list selected";
        return;
    }

    QString currentTaskList = currentItem->text(0);

    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        qWarning() << "Operations_TaskLists: Could not find task list file for SaveTaskOrder";
        return;
    }

    // Read existing JSON tasks
    QJsonArray existingTasks;
    if (!readTasklistJson(taskListFilePath, existingTasks)) {
        qWarning() << "Operations_TaskLists: Could not read task list JSON for reordering";
        return;
    }

    // Create a map of task names to their JSON objects
    QMap<QString, QJsonObject> taskMap;
    for (const QJsonValue& value : existingTasks) {
        if (!value.isObject()) continue;
        QJsonObject taskObj = value.toObject();
        QString taskName = taskObj["name"].toString();
        if (!taskName.isEmpty()) {
            taskMap[taskName] = taskObj;
        }
    }

    // Build new ordered task array based on display order
    QJsonArray reorderedTasks;
    
    int itemCount = safeGetItemCount(taskDisplayWidget);
    for (int i = 0; i < itemCount; ++i) {
        QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
        if (!item) continue;

        // Skip dummy item
        if (item->data(Qt::UserRole + 999).toBool()) continue;
        
        // Skip disabled items (like "No tasks in this list")
        if ((item->flags() & Qt::ItemIsEnabled) == 0) continue;

        QString taskName = item->text();
        if (taskMap.contains(taskName)) {
            // Update the completion status based on the checkbox state
            QJsonObject taskObj = taskMap[taskName];
            taskObj["completed"] = (item->checkState() == Qt::Checked);
            
            reorderedTasks.append(taskObj);
            taskMap.remove(taskName);  // Remove to track any missing tasks
        }
    }

    // Add any remaining tasks that weren't in the display (shouldn't happen, but safety check)
    for (const QJsonObject& taskObj : taskMap.values()) {
        reorderedTasks.append(taskObj);
    }

    // Write back the reordered tasks
    if (!writeTasklistJson(taskListFilePath, reorderedTasks)) {
        qWarning() << "Operations_TaskLists: Could not write reordered task list";
        return;
    }

    qDebug() << "Operations_TaskLists: Task order saved successfully";
}

void Operations_TaskLists::SaveTaskDescription()
{
    qDebug() << "Operations_TaskLists: Saving task description";

    if (m_currentTaskName.isEmpty()) return;

    QString newDescription = m_mainWindow->ui->plainTextEdit_TaskDesc->toPlainText();
    if (newDescription == m_lastSavedDescription) return;

    // Get current task list from tree widget
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in SaveTaskDescription";
        return;
    }
    
    QTreeWidgetItem* currentItem = treeWidget->currentItem();
    if (!currentItem || treeWidget->isCategory(currentItem)) {
        qDebug() << "Operations_TaskLists: No task list selected";
        return;
    }

    QString currentTaskList = currentItem->text(0);

    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(currentTaskList);
    if (taskListFilePath.isEmpty()) {
        qDebug() << "Operations_TaskLists: Could not find task list file for SaveTaskDescription";
        return;
    }

    // Read existing JSON tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        qWarning() << "Operations_TaskLists: Could not read task list JSON for saving description";
        return;
    }

    // Find and update the task by name
    bool taskFound = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonValue value = tasks[i];
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        if (taskObj["name"].toString() == m_currentTaskName) {
            // Update the description
            taskObj["description"] = newDescription;
            
            // Update the task in the array
            tasks[i] = taskObj;
            taskFound = true;
            break;
        }
    }
    
    if (!taskFound) {
        qWarning() << "Operations_TaskLists: Task not found for description update:" << m_currentTaskName;
        return;
    }
    
    // Write back the updated tasks
    if (writeTasklistJson(taskListFilePath, tasks)) {
        m_lastSavedDescription = newDescription;
        qDebug() << "Operations_TaskLists: Task description saved successfully";
    } else {
        qWarning() << "Operations_TaskLists: Failed to write updated task description";
    }
}

//--------Helper Functions--------//
bool Operations_TaskLists::checkDuplicateTaskName(const QString& taskName, const QString& taskListFilePath, const QString& currentTaskId)
{
    // Read the JSON tasks
    QJsonArray tasks;
    if (!readTasklistJson(taskListFilePath, tasks)) {
        return false;
    }
    
    // Check each task for duplicate name
    for (const QJsonValue& value : tasks) {
        if (!value.isObject()) continue;
        
        QJsonObject taskObj = value.toObject();
        QString existingName = taskObj["name"].toString();
        QString existingId = taskObj["id"].toString();
        
        // Check if this is a duplicate (same name but different ID)
        if (existingName == taskName && (currentTaskId.isEmpty() || existingId != currentTaskId)) {
            return true;  // Found a duplicate
        }
    }
    
    return false;  // No duplicate found
}

bool Operations_TaskLists::AreAllTasksCompleted(const QString& tasklistName)
{
    // Find the tasklist file by name
    QString taskListFilePath = findTasklistFileByName(tasklistName);
    if (taskListFilePath.isEmpty()) {
        qDebug() << "Operations_TaskLists: Could not find task list file for AreAllTasksCompleted";
        return false;
    }

    QStringList lines;
    if (!readTasklistFileWithMetadata(taskListFilePath, lines)) {
        return false;
    }

    int taskCount = 0;
    int completedCount = 0;

    // No header line in new format - start from index 0
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) continue;

        QStringList parts = lines[i].split('|');
        // New format: CompletionStatus is at index 1
        if (parts.size() >= 2) {
            taskCount++;
            if (parts[1] == "1") {
                completedCount++;
            }
        }
    }

    return (taskCount > 0 && taskCount == completedCount);
}

void Operations_TaskLists::UpdateTasklistAppearance(const QString& tasklistName)
{
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list in UpdateTasklistAppearance";
        return;
    }

    QTreeWidgetItem* item = treeWidget->findTasklist(tasklistName);
    if (!item) return;

    if (AreAllTasksCompleted(tasklistName)) {
        QFont font = item->font(0);
        font.setStrikeOut(true);
        item->setFont(0, font);
        item->setForeground(0, QColor(100, 100, 100));
    } else {
        QFont font = item->font(0);
        font.setStrikeOut(false);
        item->setFont(0, font);
        item->setForeground(0, QColor(255, 255, 255));
    }
}

void Operations_TaskLists::EnforceTaskOrder()
{
    qDebug() << "Operations_TaskLists: Enforcing task order";

    if (!validateListWidget(m_mainWindow->ui->listWidget_TaskListDisplay)) {
        qWarning() << "Operations_TaskLists: Invalid task display widget";
        return;
    }

    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    int itemCount = safeGetItemCount(taskDisplayWidget);
    if (itemCount <= 1) return;

    taskDisplayWidget->blockSignals(true);

    // Lists to maintain order within groups
    QList<QListWidgetItem*> completedItems;
    QList<QListWidgetItem*> pendingItems;
    QList<QListWidgetItem*> disabledItems;

    QListWidgetItem* currentItem = taskDisplayWidget->currentItem();
    QString currentItemText = currentItem ? currentItem->text() : "";

    // Collect items in their current order (preserves relative ordering)
    for (int i = 0; i < itemCount; ++i) {
        QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
        if (!item) continue;

        // Skip dummy item
        if (item->data(Qt::UserRole + 999).toBool()) continue;
        
        // Store pointer to the item (we'll take them all at once later)
        if ((item->flags() & Qt::ItemIsEnabled) == 0) {
            disabledItems.append(item);
        } else if (item->checkState() == Qt::Checked) {
            completedItems.append(item);
        } else {
            pendingItems.append(item);
        }
    }

    // Clear the widget and re-add items in the correct group order
    // Take all items first (in reverse to avoid index shifting)
    // But keep track of the dummy item to re-add it at the end
    QListWidgetItem* savedDummyItem = nullptr;
    for (int i = itemCount - 1; i >= 0; --i) {
        QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
        if (item && item->data(Qt::UserRole + 999).toBool()) {
            savedDummyItem = item;  // Save reference to dummy but don't take it yet
            continue;
        }
        safeTakeItem(taskDisplayWidget, i);
    }
    // Now take the dummy item
    if (savedDummyItem) {
        int dummyIndex = taskDisplayWidget->row(savedDummyItem);
        if (dummyIndex >= 0) {
            safeTakeItem(taskDisplayWidget, dummyIndex);
        }
    }

    // Add items back in the desired order: completed, pending, disabled
    // This preserves the relative order within each group
    for (QListWidgetItem* item : completedItems) {
        if (item) {
            taskDisplayWidget->addItem(item);
        }
    }
    for (QListWidgetItem* item : pendingItems) {
        if (item) {
            taskDisplayWidget->addItem(item);
        }
    }
    for (QListWidgetItem* item : disabledItems) {
        if (item) {
            taskDisplayWidget->addItem(item);
        }
    }
    
    // Re-add the dummy item at the end
    if (savedDummyItem) {
        taskDisplayWidget->addItem(savedDummyItem);
    }

    // Restore selection
    if (!currentItemText.isEmpty()) {
        int newCount = safeGetItemCount(taskDisplayWidget);
        for (int i = 0; i < newCount; ++i) {
            QListWidgetItem* item = safeGetItem(taskDisplayWidget, i);
            if (item && item->text() == currentItemText) {
                taskDisplayWidget->setCurrentItem(item);
                break;
            }
        }
    }

    taskDisplayWidget->blockSignals(false);

    // Save the new order after enforcing it
    SaveTaskOrder();
}

// Save the tasklist settings (categories and structure) to JSON file
bool Operations_TaskLists::SaveTasklistSettings()
{
    qDebug() << "Operations_TaskLists: Saving tasklist settings";
    
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list";
        return false;
    }
    
    // Get the JSON structure from the tree widget
    QJsonDocument doc = treeWidget->saveStructureToJson();
    
    // Save to file
    QString settingsFilePath = "Data/" + m_mainWindow->user_Username + "/Tasklists/tasklistsettings.txt";
    
    // Ensure directory exists
    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";
    if (!OperationsFiles::ensureDirectoryExists(tasksListsPath)) {
        qWarning() << "Operations_TaskLists: Failed to create Tasklists directory";
        return false;
    }
    
    // Create temp file
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("settings", tempDir);
    QFile tempFile(tempPath);
    
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Operations_TaskLists: Failed to open temp file for settings";
        return false;
    }
    
    // Write JSON to temp file
    tempFile.write(doc.toJson(QJsonDocument::Indented));
    tempFile.close();
    
    // Encrypt the temp file to the final location
    bool success = CryptoUtils::Encryption_EncryptFile(m_mainWindow->user_Key, tempPath, settingsFilePath);
    
    // Clean up temp file
    QFile::remove(tempPath);
    
    if (!success) {
        qWarning() << "Operations_TaskLists: Failed to encrypt settings file";
    }
    
    return success;
}

// Load the tasklist settings (categories and structure) from JSON file
bool Operations_TaskLists::LoadTasklistSettings()
{
    qDebug() << "Operations_TaskLists: Loading tasklist settings";
    
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list";
        return false;
    }
    
    QString settingsFilePath = "Data/" + m_mainWindow->user_Username + "/Tasklists/tasklistsettings.txt";
    
    // Check if file exists
    QFileInfo fileInfo(settingsFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qDebug() << "Operations_TaskLists: Settings file does not exist";
        return false;
    }
    
    // Create temp directory
    QString tempDir = "Data/" + m_mainWindow->user_Username + "/temp/";
    if (!OperationsFiles::ensureDirectoryExists(tempDir)) {
        qWarning() << "Operations_TaskLists: Failed to create temp directory";
        return false;
    }
    
    QString tempPath = TaskDataSecurity::generateSecureTempFileName("load_settings", tempDir);
    
    // Decrypt the file
    if (!CryptoUtils::Encryption_DecryptFile(m_mainWindow->user_Key, settingsFilePath, tempPath)) {
        qWarning() << "Operations_TaskLists: Failed to decrypt settings file";
        return false;
    }
    
    // Read the decrypted file
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        QFile::remove(tempPath);
        qWarning() << "Operations_TaskLists: Failed to open decrypted settings file";
        return false;
    }
    
    QByteArray jsonData = tempFile.readAll();
    tempFile.close();
    QFile::remove(tempPath);
    
    // Parse JSON
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Operations_TaskLists: Failed to parse settings JSON:" << error.errorString();
        return false;
    }
    
    // Load the structure into the tree widget
    return treeWidget->loadStructureFromJson(doc);
}

// Create a new category
void Operations_TaskLists::CreateNewCategory()
{
    qDebug() << "Operations_TaskLists: Creating new category";
    
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list";
        return;
    }
    
    // Get existing category names
    QStringList existingCategories = treeWidget->getAllCategories();
    
    // Get a unique name for the new category
    QString initialName = "New Category";
    QString uniqueName = Operations::GetUniqueItemName(initialName, existingCategories);
    
    // Add the category to the tree
    QTreeWidgetItem* categoryItem = treeWidget->addCategory(uniqueName);
    if (!categoryItem) {
        qWarning() << "Operations_TaskLists: Failed to create category";
        return;
    }
    
    // Expand the new category
    categoryItem->setExpanded(true);
    
    // Save the updated settings
    SaveTasklistSettings();
}

// Helper function to find a tasklist item in the tree
QTreeWidgetItem* Operations_TaskLists::findTasklistItemInTree(const QString& tasklistName)
{
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        return nullptr;
    }
    
    return treeWidget->findTasklist(tasklistName);
}

// Helper function to get tasklist name from tree item
QString Operations_TaskLists::getTasklistNameFromTreeItem(QTreeWidgetItem* item)
{
    if (!item) {
        return QString();
    }
    
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        return QString();
    }
    
    // If the item is a category, return empty
    if (treeWidget->isCategory(item)) {
        return QString();
    }
    
    // Return the tasklist name
    return item->text(0);
}

bool Operations_TaskLists::SaveTasklistOrder()
{
    qDebug() << "Operations_TaskLists: Saving tasklist order";

    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast to qtree_Tasklists_list in SaveTasklistOrder";
        return false;
    }

    // Get all tasklists from all categories
    QStringList tasklistOrder;
    for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* category = treeWidget->topLevelItem(i);
        if (category) {
            for (int j = 0; j < category->childCount(); ++j) {
                QTreeWidgetItem* tasklistItem = category->child(j);
                if (tasklistItem) {
                    tasklistOrder.append(tasklistItem->text(0));
                }
            }
        }
    }

    if (tasklistOrder.isEmpty()) return true;

    QString orderFilePath = "Data/" + m_mainWindow->user_Username + "/Tasklists/TasklistOrder.txt";

    QStringList content;
    content.append("# TasklistOrder");
    content.append(tasklistOrder);

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

    QFont font = m_mainWindow->ui->treeWidget_TaskList_List->font();
    font.setPointSize(fontSize);

    // Update list widgets
    m_mainWindow->ui->treeWidget_TaskList_List->setFont(font);
    m_mainWindow->ui->listWidget_TaskListDisplay->setFont(font);

    // Update checkbox hitbox width to scale with font size
    // Cast to custom widget type to access setCheckboxWidth
    if (auto* customTaskDisplay = qobject_cast<qlist_TasklistDisplay*>(m_mainWindow->ui->listWidget_TaskListDisplay)) {
        // Calculate checkbox width proportional to font size
        // Base calculation: fontSize * 2.5 gives good scaling
        // This gives us ~25px for 10pt font, ~50px for 20pt font
        int checkboxWidth = static_cast<int>(fontSize * 1.2);

        // Ensure minimum and maximum reasonable sizes
        if (checkboxWidth < 20) checkboxWidth = 20;  // Minimum clickable area
        if (checkboxWidth > 60) checkboxWidth = 60;  // Maximum reasonable size

        customTaskDisplay->setCheckboxWidth(checkboxWidth);
        qDebug() << "Operations_TaskLists: Updated checkbox width to:" << checkboxWidth << "for font size:" << fontSize;
    }

    // Update labels
    m_mainWindow->ui->label_TaskListName->setFont(font);
    m_mainWindow->ui->label_Tasks->setFont(font);
    m_mainWindow->ui->label_TaskDetails->setFont(font);

    // Update table widget
    m_mainWindow->ui->tableWidget_TaskDetails->setFont(font);

    // Update the headers font as well
    QHeaderView* horizontalHeader = m_mainWindow->ui->tableWidget_TaskDetails->horizontalHeader();
    if (horizontalHeader) {
        horizontalHeader->setFont(font);
    }
    QHeaderView* verticalHeader = m_mainWindow->ui->tableWidget_TaskDetails->verticalHeader();
    if (verticalHeader) {
        verticalHeader->setFont(font);
    }

    // Calculate appropriate height for the table widget based on font size
    QFontMetrics fm(font);
    int rowHeight = fm.height() + ROW_PADDING;
    int headerHeight = fm.height() + HEADER_PADDING;
    int totalHeight = headerHeight + rowHeight + EXTRA_PADDING;

    // Ensure minimum readable height
    if (totalHeight < MIN_TABLE_HEIGHT) {
        totalHeight = MIN_TABLE_HEIGHT;
    }
    // Cap maximum height to prevent excessive expansion
    if (totalHeight > MAX_TABLE_HEIGHT) {
        totalHeight = MAX_TABLE_HEIGHT;
    }

    // Update table widget's minimum and maximum height
    m_mainWindow->ui->tableWidget_TaskDetails->setMinimumHeight(totalHeight);
    m_mainWindow->ui->tableWidget_TaskDetails->setMaximumHeight(totalHeight);

    // Force the table to resize its rows
    if (m_mainWindow->ui->tableWidget_TaskDetails->rowCount() > 0) {
        m_mainWindow->ui->tableWidget_TaskDetails->resizeRowsToContents();

        // Resize columns horizontally based on font size
        QTableWidget* taskDetailsTable = m_mainWindow->ui->tableWidget_TaskDetails;
        int columnCount = taskDetailsTable->columnCount();

        if (columnCount > 0) {
            // Calculate width for Status column (first column)
            QString longestStatus = "Completed";  // Longest possible status text
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
            int statusWidth = fm.horizontalAdvance(longestStatus) + 20;  // Add padding
#else
            int statusWidth = fm.width(longestStatus) + 20;  // Add padding
#endif
            taskDetailsTable->setColumnWidth(0, statusWidth);

            // If task is completed, there's a Completion Date column (second column)
            if (columnCount == 3) {  // Completed task has 3 columns
                // Calculate width for Completion Date column
                // Use a sample date format to calculate width
                QString sampleDate = "Wednesday the 31st December 2025 at 23:59";
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
                int dateWidth = fm.horizontalAdvance(sampleDate) + 20;  // Add padding
#else
                int dateWidth = fm.width(sampleDate) + 20;  // Add padding
#endif
                taskDetailsTable->setColumnWidth(1, dateWidth);
            }

            // The last column (Creation Date) is already set to stretch
            // by the existing code in LoadTaskDetails
        }
    }

    // Update plain text edit
    m_mainWindow->ui->plainTextEdit_TaskDesc->setFont(font);
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

    // Store task data safely before capturing
    QString taskName;
    QString taskData;
    bool hasValidItem = false;

    if (item && (item->flags() & Qt::ItemIsEnabled)) {
        taskName = item->text();
        taskData = item->data(Qt::UserRole).toString();
        hasValidItem = true;
        editTaskAction->setEnabled(true);
        deleteTaskAction->setEnabled(true);
    } else {
        editTaskAction->setEnabled(false);
        deleteTaskAction->setEnabled(false);
    }

    connect(newTaskAction, &QAction::triggered, this, [this]() {
        CreateNewTask();
    });

    // Capture by value to avoid dangling pointer
    connect(editTaskAction, &QAction::triggered, this, [this, taskName, taskData, hasValidItem, taskListWidget, pos]() {
        if (hasValidItem) {
            // Find the item again to ensure it's still valid
            QListWidgetItem* itemToEdit = taskListWidget->itemAt(pos);
            if (itemToEdit && itemToEdit->text() == taskName) {
                // Store the original task info
                currentTaskToEdit = taskName;
                currentTaskData = itemToEdit->data(Qt::UserRole).toString();  // Get task ID
                m_currentTaskName = taskName;
                
                // Enable inline editing
                itemToEdit->setFlags(itemToEdit->flags() | Qt::ItemIsEditable);
                taskListWidget->editItem(itemToEdit);
                
                // Get the row for validation
                int itemRow = taskListWidget->row(itemToEdit);
                
                // Use a single-shot connection to handle the edit completion
                QMetaObject::Connection* conn = new QMetaObject::Connection();
                *conn = connect(taskListWidget, &QListWidget::itemChanged, this,
                        [this, taskListWidget, itemRow, conn](QListWidgetItem* changedItem) {
                            // Validate the item at the row is the one that changed
                            int currentCount = safeGetItemCount(taskListWidget);
                            if (itemRow >= 0 && itemRow < currentCount) {
                                QListWidgetItem* itemAtRow = safeGetItem(taskListWidget, itemRow);
                                if (itemAtRow && itemAtRow == changedItem) {
                                    disconnect(*conn);
                                    delete conn;
                                    RenameTask(changedItem);
                                }
                            }
                        });
            }
        }
    });

    connect(deleteTaskAction, &QAction::triggered, this, [this, taskName, hasValidItem]() {
        if (hasValidItem) {
            DeleteTask(taskName);
        }
    });

    contextMenu.exec(taskListWidget->mapToGlobal(pos));
}

void Operations_TaskLists::showContextMenu_TaskListList(const QPoint &pos)
{
    qDebug() << "Operations_TaskLists: Showing context menu for tasklist tree";
    
    // Cast to qtree_Tasklists_list
    qtree_Tasklists_list* treeWidget = qobject_cast<qtree_Tasklists_list*>(m_mainWindow->ui->treeWidget_TaskList_List);
    if (!treeWidget) {
        qWarning() << "Operations_TaskLists: Failed to cast tree widget in showContextMenu_TaskListList";
        return;
    }
    
    QTreeWidgetItem* item = treeWidget->itemAt(pos);

    QMenu contextMenu(m_mainWindow);

    // Determine what actions to show based on what was clicked
    bool isCategory = item ? treeWidget->isCategory(item) : false;
    bool hasItem = (item != nullptr);
    
    QAction* newCategoryAction = nullptr;
    QAction* newTaskListAction = nullptr;
    QAction* renameCategoryAction = nullptr;
    QAction* renameTaskListAction = nullptr;
    QAction* deleteCategoryAction = nullptr;
    QAction* deleteTaskListAction = nullptr;
    
    if (!hasItem || isCategory) {
        // Clicked on empty space or category
        newCategoryAction = contextMenu.addAction("New Category");
        newTaskListAction = contextMenu.addAction("New Tasklist");
        
        if (isCategory) {
            contextMenu.addSeparator();
            renameCategoryAction = contextMenu.addAction("Rename Category");
            deleteCategoryAction = contextMenu.addAction("Delete Category");
        }
    } else {
        // Clicked on a tasklist
        newTaskListAction = contextMenu.addAction("New Tasklist");
        renameTaskListAction = contextMenu.addAction("Rename Tasklist");
        deleteTaskListAction = contextMenu.addAction("Delete Tasklist");
    }

    // Store item data for use in lambdas
    QString itemText = item ? item->text(0) : QString();

    // Connect actions
    if (newCategoryAction) {
        connect(newCategoryAction, &QAction::triggered, this, &Operations_TaskLists::CreateNewCategory);
    }
    
    if (newTaskListAction) {
        connect(newTaskListAction, &QAction::triggered, this, &Operations_TaskLists::CreateNewTaskList);
    }
    
    if (renameCategoryAction) {
        connect(renameCategoryAction, &QAction::triggered, this, [this, treeWidget, item, itemText]() {
            if (!item || !treeWidget->isCategory(item)) return;
            
            // Get existing category names for validation
            QStringList existingCategories = treeWidget->getAllCategories();
            existingCategories.removeAll(itemText);  // Remove current name from list
            
            bool ok;
            QString newName = QInputDialog::getText(m_mainWindow, "Rename Category",
                                                   "Enter new category name:", QLineEdit::Normal,
                                                   itemText, &ok);
            if (ok && !newName.isEmpty() && newName != itemText) {
                // Validate the new name
                InputValidation::ValidationResult result =
                    InputValidation::validateInput(newName, InputValidation::InputType::TaskListName);
                
                if (!result.isValid) {
                    QMessageBox::warning(m_mainWindow, "Invalid Category Name", result.errorMessage);
                    return;
                }
                
                // Check for duplicates
                if (existingCategories.contains(newName)) {
                    QMessageBox::warning(m_mainWindow, "Duplicate Name",
                                       "A category with this name already exists.");
                    return;
                }
                
                // Rename the category
                item->setText(0, newName);
                
                // Save the updated settings
                SaveTasklistSettings();
            }
        });
    }
    
    if (renameTaskListAction) {
        connect(renameTaskListAction, &QAction::triggered, this, [this, treeWidget, item, itemText]() {
            if (!item || treeWidget->isCategory(item)) return;
            
            // Get existing tasklist names for validation
            QStringList existingNames;
            QStringList categories = treeWidget->getAllCategories();
            for (const QString& category : categories) {
                QStringList tasklistsInCategory = treeWidget->getTasklistsInCategory(category);
                for (const QString& tasklistName : tasklistsInCategory) {
                    if (tasklistName != itemText) {
                        existingNames.append(tasklistName);
                    }
                }
            }
            
            bool ok;
            QString newName = QInputDialog::getText(m_mainWindow, "Rename Tasklist",
                                                   "Enter new tasklist name:", QLineEdit::Normal,
                                                   itemText, &ok);
            if (ok && !newName.isEmpty() && newName != itemText) {
                // Store the original name for the rename operation
                currentTaskListBeingRenamed = itemText;
                
                // Create a temporary QListWidgetItem to pass to RenameTasklist
                // This is a workaround since RenameTasklist expects QListWidgetItem*
                QListWidgetItem tempItem(newName);
                RenameTasklist(&tempItem);
            }
        });
    }
    
    if (deleteCategoryAction) {
        connect(deleteCategoryAction, &QAction::triggered, this, [this, treeWidget, item, itemText]() {
            if (!item || !treeWidget->isCategory(item)) return;
            
            // Check if category has children
            if (item->childCount() > 0) {
                QMessageBox::StandardButton reply = QMessageBox::question(m_mainWindow, "Delete Category",
                    QString("The category '%1' contains %2 tasklist(s). "
                           "All tasklists will be moved to 'Uncategorized'. Continue?")
                           .arg(itemText).arg(item->childCount()),
                    QMessageBox::Yes | QMessageBox::No);
                
                if (reply != QMessageBox::Yes) return;
                
                // Move all children to Uncategorized
                while (item->childCount() > 0) {
                    QTreeWidgetItem* child = item->takeChild(0);
                    QString tasklistName = child->text(0);
                    delete child;
                    treeWidget->addTasklist(tasklistName, "Uncategorized");
                }
            } else {
                // Empty category, just confirm deletion
                QMessageBox::StandardButton reply = QMessageBox::question(m_mainWindow, "Delete Category",
                    QString("Are you sure you want to delete the category '%1'?").arg(itemText),
                    QMessageBox::Yes | QMessageBox::No);
                
                if (reply != QMessageBox::Yes) return;
            }
            
            // Delete the category
            int index = treeWidget->indexOfTopLevelItem(item);
            if (index >= 0) {
                delete treeWidget->takeTopLevelItem(index);
            }
            
            // Ensure Uncategorized still exists
            treeWidget->ensureUncategorizedExists();
            
            // Save the updated settings
            SaveTasklistSettings();
        });
    }
    
    if (deleteTaskListAction) {
        connect(deleteTaskListAction, &QAction::triggered, this, &Operations_TaskLists::DeleteTaskList);
    }

    contextMenu.exec(treeWidget->mapToGlobal(pos));
}
