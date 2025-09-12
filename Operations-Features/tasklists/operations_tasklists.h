#ifndef OPERATIONS_TASKLISTS_H
#define OPERATIONS_TASKLISTS_H

#include <QObject>
#include <QTime>
#include <QPointer>
#include <QUuid>
#include <utility>  // For std::pair
#include "../../mainwindow.h"
#include "operations.h"
#include "inputvalidation.h"
#include <QMessageBox>
#include "../../Operations-Global/ThreadSafeContainers.h"
#include "../../Operations-Global/SafeTimer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include "../../CustomWidgets/tasklists/qtree_Tasklists_list.h"

class MainWindow;
class Operations_TaskLists : public QObject
{
    Q_OBJECT
    
private:
    // SECURITY: Use QPointer for automatic null checking when MainWindow is destroyed
    QPointer<MainWindow> m_mainWindow;
    
    // Metadata header structure for tasklist files
    struct TasklistMetadata {
        char magic[8];          // "TASKLIST" - 8 bytes
        char version[4];        // "0002" - 4 bytes (incremented for new format)
        char name[256];         // Tasklist name (null-padded) - 256 bytes
        char creationDate[32];  // ISO format date (null-padded) - 32 bytes
        char lastSelectedTask[128]; // Last selected task name (null-padded) - 128 bytes
        char reserved[84];      // Reserved for future use - 84 bytes
        // Total: 512 bytes
    };
    
    static constexpr int METADATA_SIZE = 512;
    static constexpr const char* TASKLIST_MAGIC = "TASKLIST";
    static constexpr const char* TASKLIST_VERSION = "0003";  // JSON format
    
    // Helper functions for metadata
    bool writeTasklistMetadata(const QString& filePath, const QString& tasklistName, const QByteArray& key);
    bool readTasklistMetadata(const QString& filePath, QString& tasklistName, const QByteArray& key);
    bool updateLastSelectedTask(const QString& tasklistName, const QString& taskName);
    QString generateTasklistFilename();
    QString findTasklistFileByName(const QString& tasklistName);
    ThreadSafeMap<QString, QString> m_tasklistNameToFile;  // Maps tasklist names to file paths
    
    // Thread-safe container for managing task order during reordering
    ThreadSafeList<std::pair<QListWidgetItem*, int>> m_taskOrderCache;
    
    // Task manipulation helper functions
    bool checkDuplicateTaskName(const QString& taskName, const QString& taskListFilePath, const QString& currentTaskId = QString());
    QString currentTaskToEdit;    // Stores the name of the task being edited
    QString currentTaskData;       // Stores the data of the task being edited
    QString currentTaskId;         // Stores the ID of the task being edited
    
    // JSON helper functions
    QJsonObject taskToJson(const QString& name, bool completed, const QString& completionDate,
                          const QString& creationDate, const QString& description, const QString& id);
    bool parseJsonTask(const QJsonObject& taskObj, QString& name, bool& completed, 
                      QString& completionDate, QString& creationDate, QString& description, QString& id);
    bool readTasklistJson(const QString& filePath, QJsonArray& tasks);
    bool writeTasklistJson(const QString& filePath, const QJsonArray& tasks);
    
    SafeTimer* m_descriptionSaveTimer;
    QString m_currentTaskName;
    QString m_lastSavedDescription; // To track description changes
    
    QString currentTaskListBeingRenamed;
    
    // For tracking the last clicked item (for delete key functionality)
    QWidget* m_lastClickedWidget;
    QListWidgetItem* m_lastClickedItem;
    
    // Methods for handling delete key functionality
    void onTaskListItemClicked(QListWidgetItem* item);
    void onTaskDisplayItemClicked(QListWidgetItem* item);
    void handleDeleteKeyPress();
    
    // Methods for handling double-click functionality
    void onTaskListItemDoubleClicked(QListWidgetItem* item);
    void onTaskDisplayItemDoubleClicked(QListWidgetItem* item);
    void EditSelectedTask();
    
    // Helper function to format datetime in the required format
    QString FormatDateTime(const QDateTime& dateTime);
    
    void SetTaskStatus(bool checked, QListWidgetItem* item = nullptr);
    
    // Task order management
    bool SaveTasklistOrder();
    bool LoadTasklistOrder(QStringList& orderedTasklists);
    void SaveTaskOrder();  // Save the current task display order
    void HandleTaskReorder();  // Handle drag-and-drop reordering with group detection
    void EnforceTaskOrder();
    
    // Category and tasklist settings management
    bool SaveTasklistSettings();
    bool LoadTasklistSettings();
    void CreateNewCategory();
    
    // Helper functions for tree widget operations
    QTreeWidgetItem* findTasklistItemInTree(const QString& tasklistName);
    QString getTasklistNameFromTreeItem(QTreeWidgetItem* item);
    
    // Safe container operations helpers
    QListWidgetItem* safeGetItem(QListWidget* widget, int index) const;
    QListWidgetItem* safeTakeItem(QListWidget* widget, int index);
    bool validateListWidget(QListWidget* widget) const;
    int safeGetItemCount(QListWidget* widget) const;
    
    // Custom file I/O that handles metadata properly
    bool readTasklistFileWithMetadata(const QString& filePath, QStringList& taskLines);
    bool writeTasklistFileWithMetadata(const QString& filePath, const QStringList& taskLines);
    
    // ========== TUNABLE PARAMETERS FOR TABLE HEIGHT ==========
    // These should match the values in UpdateTasklistsTextSize
    const int ROW_PADDING = 4;        // Padding around row text
    const int HEADER_PADDING = 6;     // Padding around header text
    const int EXTRA_PADDING = -1;      // Extra padding for borders/margins (-1 to hide the bottom outline of the row)
    const int MIN_TABLE_HEIGHT = 40;  // Minimum table height
    const int MAX_TABLE_HEIGHT = 90;  // Maximum table height
    // =========================================================

public:
    ~Operations_TaskLists();
    explicit Operations_TaskLists(MainWindow* mainWindow);
    friend class MainWindow;
    
    // Task list management
    void CreateNewTaskList();
    void CreateTaskListFile(const QString& listName);
    void LoadTasklists();
    void LoadIndividualTasklist(const QString& tasklistName, const QString& taskToSelect);
    void LoadTaskDetails(const QString& taskName);
    void DeleteTaskList();
    void RenameTasklist(QListWidgetItem* item);
    
    // Task operations
    void CreateNewTask();  // New function for immediate task creation
    void ShowTaskMenu(bool editMode);
    void DeleteTask(const QString& taskName);
    void RenameTask(QListWidgetItem* item);
    
    // Context menu
    void showContextMenu_TaskListDisplay(const QPoint &pos);
    void showContextMenu_TaskListList(const QPoint &pos);
    
    // Simple task functions
    void AddTaskSimple(QString taskName, QString description);
    void ModifyTaskSimple(const QString& originalTaskName, QString taskName, QString description);
    
    // Task description management
    void SaveTaskDescription();
    
    // Check if all tasks in a tasklist are completed
    bool AreAllTasksCompleted(const QString& tasklistName);
    
    // Update the visual appearance of a tasklist based on completion status
    void UpdateTasklistAppearance(const QString& tasklistName);
    
    // Update text size for task lists
    void UpdateTasklistsTextSize(int fontSize);
    
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif // OPERATIONS_TASKLISTS_H
