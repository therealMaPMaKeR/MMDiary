#ifndef OPERATIONS_TASKLISTS_H
#define OPERATIONS_TASKLISTS_H

#include <QObject>
#include <QTime>
#include <QPointer>
#include "../../mainwindow.h"
#include "operations.h"
#include "inputvalidation.h"
#include <QMessageBox>

class MainWindow;
class Operations_TaskLists : public QObject
{
    Q_OBJECT
    
private:
    // SECURITY: Use QPointer for automatic null checking when MainWindow is destroyed
    QPointer<MainWindow> m_mainWindow;
    
    // Task manipulation helper functions
    bool checkDuplicateTaskName(const QString& taskName, const QString& taskListFilePath, const QString& currentTaskId = QString());
    QString currentTaskToEdit;    // Stores the name of the task being edited
    QString currentTaskData;       // Stores the data of the task being edited
    
    QTimer* m_descriptionSaveTimer;
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
    void EnforceTaskOrder();
    
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
    void ShowTaskMenu(bool editMode);
    void DeleteTask(const QString& taskName);
    
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
