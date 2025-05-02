#ifndef OPERATIONS_TASKLISTS_H
#define OPERATIONS_TASKLISTS_H

#include <QObject>
#include <QTime>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include <QMessageBox>
#include <queue>

class Operations_Diary;
class MainWindow;
class Operations_TaskLists : public QObject
{
    Q_OBJECT
private:
    MainWindow* m_mainWindow;
    Operations_Diary* m_diaryOps;
    // Helper function to compare time values
    bool compareTimeValues(int value1, const QString& unit1, int value2, const QString& unit2);

    // Task manipulation helper functions
    bool checkDuplicateTaskName(const QString& taskName, const QString& taskListFilePath, const QString& currentTaskId = QString());
    QString getCurrentSelectedTaskData();
    bool modifyTask(const QString& taskId, const QString& newTaskData);
    QString currentTaskToEdit;    // Stores the name of the task being edited
    QString currentTaskData;

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

    // Helper function to calculate due date for time-limited tasks
    QDateTime CalculateDueDate(const QDateTime& creationDate, int timeValue, const QString& timeUnit);

    // Helper function to calculate time left for time-limited tasks
    QString CalculateTimeLeft(const QDateTime& currentDateTime, const QDateTime& dueDateTime);

    // Helper function to calculate due date for recurrent tasks
    QDateTime CalculateRecurrentDueDate(const QDateTime& creationDateTime,
                                    const QTime& startTime,
                                    int frequencyValue,
                                    const QString& frequencyUnit,
                                    bool hasTimeLimit,
                                    int timeLimitValue,
                                    const QString& timeLimitUnit,
                                    bool calculateNext,
                                        const QDateTime& currentDateTime);

    void SetTaskStatus(bool checked, QListWidgetItem* item = nullptr);


    QTimer* m_reminderTimer;
    QMap<QString, QDateTime> m_lastNotifiedTasks; // Track last notification time for each task

    void CheckTaskReminders();
    bool ShouldShowTimeLimitReminder(const QDateTime& dueDateTime, const QDateTime& creationDateTime,
                                     int reminderFrequency, const QString& reminderUnit,
                                     const QString& taskId);
    bool ShouldShowRecurrentReminder(const QDateTime& dueDateTime, int reminderValue,
                                     const QString& reminderUnit, const QDateTime& currentDateTime,
                                     const QString& taskId);

    bool UpdateCongratMessageToNone(const QString& taskListName,
                                    const QString& taskName,
                                    const QString& taskType,
                                    int congratMessageIndex);


    QMap<QString, bool> m_overdueNotifiedTasks; // Tracks tasks that have received overdue notifications

    // Add this function declaration:
    bool UpdatePunitiveMessageToNone(const QString& taskListName,
                                     const QString& taskName,
                                     const QString& taskType,
                                     int punitiveMessageIndex);

    struct TaskDueInfo {
        QString taskId;          // Unique task identifier
        QString taskName;        // Name of the task
        QString taskListName;    // Name of the task list
        QDateTime dueDateTime;   // When the task is due
        QString punitiveType;    // Type of punitive message, if any

        // For sorting in the priority queue
        bool operator<(const TaskDueInfo& other) const {
            return dueDateTime > other.dueDateTime; // Reverse order for priority_queue
        }
    };

    // Priority queue for due tasks
    std::priority_queue<TaskDueInfo> m_dueTasksQueue;

    // Timer for precise timing
    QTimer* m_preciseTaskTimer;

    // Methods to manage the task queue
    void InitializeDueTasksQueue();
    void UpdateDueTasksQueue();
    void ScheduleNextDueTask();
    void ProcessDueTask(const TaskDueInfo& taskInfo);
    void AddTaskToDueQueue(const QString& taskListName, const QString& taskName,
                           const QDateTime& dueDateTime, const QString& punitiveType);
    void RemoveTaskFromDueQueue(const QString& taskId);

    QDateTime m_taskDueDateTime;
    QDateTime m_taskCreationDateTime;

    QTimer* m_timerUpdateTimeLeft;  // New timer for updating Time Left
    int m_timeLeftRow;              // Row of Time Left cell
    int m_timeLeftCol;              // Column of Time Left cell
    bool m_timeLeftVisible;         // Whether Time Left is currently visible
    QString m_currentTaskType;      // Track current task type
    QDateTime ParseFormattedDateTime(const QString& formattedDateTime);

    QString SafeCalculateTimeLeft(qint64 secondsLeft);

    // Task order management
    void SaveTaskOrder();
    bool SaveTasklistOrder();
    bool LoadTasklistOrder(QStringList& orderedTasklists);
    void HandleTaskDropEvent(QDropEvent* event);
    void HandleTaskListDropEvent(QDropEvent* event);
    void EnforceTaskOrder();
public slots:
    void updateTimeLeftCell();      // New slot to update the Time Left cell
public:
    ~Operations_TaskLists();
    explicit Operations_TaskLists(MainWindow* mainWindow, Operations_Diary* diaryOps);
    friend class MainWindow;

    void CreateNewTaskList();
    void CreateTaskListFile(const QString& listName);
    void LoadTasklists();
    void LoadIndividualTasklist(const QString& tasklistName, const QString& taskToSelect);
    void LoadTaskDetails(const QString& taskName);
    void ShowTaskMenu(bool editMode);
    void AddTask();
    void DeleteTask(const QString& taskId);
    void RenameTasklist(QListWidgetItem* item);

    QString FormatTimeDifference(qint64 seconds);

    // Context menu
    void showContextMenu_TaskListDisplay(const QPoint &pos);

    // Task addition functions
    void AddTaskSimple(QString taskName, bool logTask, QString cmess, QString description);
    void AddTaskTimeLimit(QString taskName, bool logTask, int value_TLimit,
                                                QString type_TLimit, QString cmess, QString pmess,
                                                bool reminder, int value_RFreq, QString type_RFreq,
                          QString description);
    void AddTaskRecurrent(QString taskName, bool logTask, int value_Freq,
                                                QString type_Freq, QTime startTime, bool timeLimit,
                                                int value_TLimit, QString type_TLimit, bool reminder,
                                                int value_Reminder, QString type_Reminder,
                                                QString description);

    // Task modification functions
    void ModifyTaskSimple(const QString& originalTaskName,
                                                QString taskName, bool logTask, QString cmess,
                          QString description);
    void ModifyTaskTimeLimit(const QString& originalTaskName,
                                                   QString taskName, bool logTask, int value_TLimit,
                                                   QString type_TLimit, QString cmess, QString pmess,
                                                   bool reminder, int value_RFreq, QString type_RFreq,
                                                   QString description);
    void ModifyTaskRecurrent(const QString& originalTaskName,
                                                   QString taskName, bool logTask, int value_Freq,
                                                   QString type_Freq, QTime startTime, bool timeLimit,
                                                   int value_TLimit, QString type_TLimit, bool reminder,
                                                   int value_Reminder, QString type_Reminder,
                                                   QString description);
    void SaveTaskDescription();

    // Check if all tasks in a tasklist are completed
    bool AreAllTasksCompleted(const QString& tasklistName);

    // Update the visual appearance of a tasklist based on completion status
    void UpdateTasklistAppearance(const QString& tasklistName);

    void UpdateTasklistsTextSize(int fontSize);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void showContextMenu_TaskListList(const QPoint &pos);
    void DeleteTaskList();
};

#endif // OPERATIONS_TASKLISTS_H
