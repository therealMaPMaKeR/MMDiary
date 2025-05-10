#include "operations_tasklists.h"
#include "operations_diary.h"
#include "../CustomWidgets/CombinedDelegate.h"
#include "../Operations-Global/CryptoUtils.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include "ui_mainwindow.h"
#include "../constants.h"
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
#include "Operations-Global/operations_files.h"

Operations_TaskLists::Operations_TaskLists(MainWindow* mainWindow, Operations_Diary* diaryOps)
    : m_mainWindow(mainWindow)
    , m_diaryOps(diaryOps)
    , m_lastClickedWidget(nullptr)
    , m_lastClickedItem(nullptr)
    , m_timeLeftRow(-1)
    , m_timeLeftCol(-1)
    , m_timeLeftVisible(false)
    , m_currentTaskType("")
{
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

    // Connect text change signal
    /*connect(m_mainWindow->ui->plainTextEdit_TaskDesc, &QPlainTextEdit::textChanged,
            this, [this]() {
                m_descriptionSaveTimer->start(); // Restart timer on each text change
            });*/ // disconnected because we dont want this anymore, kept in case we want to reactivate this.

    // We'll need to install an event filter to catch focus loss
    m_mainWindow->ui->plainTextEdit_TaskDesc->installEventFilter(this);

    // Also connect to mouse click events on other widgets to save when user clicks elsewhere
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

    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &QListWidget::itemChanged,
            this, [this](QListWidgetItem* item) {
                if (item) {
                    // Only process the change if it's related to the checkbox state
                    // and not some other attribute of the item
                    bool checked = (item->checkState() == Qt::Checked);

                    // Temporarily block signals to avoid recursion
                    m_mainWindow->ui->listWidget_TaskListDisplay->blockSignals(true);

                    // Call our SetTaskStatus function with the specific item
                    SetTaskStatus(checked, item);

                    // Re-enable signals
                    m_mainWindow->ui->listWidget_TaskListDisplay->blockSignals(false);
                }
            });

    // Initialize the precise task timer
    m_preciseTaskTimer = new QTimer(this);
    connect(m_preciseTaskTimer, &QTimer::timeout, this, [this]() {
        if (!m_dueTasksQueue.empty()) {
            ProcessDueTask(m_dueTasksQueue.top());
        }
    });


    // Initialize the time left update timer
    m_timerUpdateTimeLeft = new QTimer(this);
    m_timerUpdateTimeLeft->setInterval(1000); // Update every second
    connect(m_timerUpdateTimeLeft, &QTimer::timeout, this, &Operations_TaskLists::updateTimeLeftCell);

    // Initialize the due tasks queue
    InitializeDueTasksQueue();

    // Schedule a periodic refresh of the queue to catch any external changes
    QTimer* queueRefreshTimer = new QTimer(this);
    queueRefreshTimer->setInterval(1800000); // 30 minutes
    connect(queueRefreshTimer, &QTimer::timeout, this, &Operations_TaskLists::UpdateDueTasksQueue);
    queueRefreshTimer->start();

    // Existing reminder timer code...
    m_reminderTimer = new QTimer(this);
    m_reminderTimer->setInterval(60000); // Check every minute
    connect(m_reminderTimer, &QTimer::timeout, this, &Operations_TaskLists::CheckTaskReminders);
    m_reminderTimer->start();

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

    // Install event filters to capture drop events
    connect(m_mainWindow->ui->listWidget_TaskListDisplay, &custom_QListWidget_Task::itemsReordered,
            this, [this]() {
                // Brief delay to ensure the drag and drop operation is complete
                QTimer::singleShot(0, this, &Operations_TaskLists::EnforceTaskOrder);
            });

    connect(m_mainWindow->ui->listWidget_TaskList_List, &custom_QListWidget_Task::itemsReordered,
            this, &Operations_TaskLists::SaveTasklistOrder);
    LoadTasklists();
}


Operations_TaskLists::~Operations_TaskLists()
{
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

    if (m_preciseTaskTimer) {
        m_preciseTaskTimer->stop();
        delete m_preciseTaskTimer;
        m_preciseTaskTimer = nullptr;
    }

    if (m_timerUpdateTimeLeft) {
        m_timerUpdateTimeLeft->stop();
        delete m_timerUpdateTimeLeft;
        m_timerUpdateTimeLeft = nullptr;
    }


    // Delete the timer
    if (m_descriptionSaveTimer) {
        m_descriptionSaveTimer->stop();
        delete m_descriptionSaveTimer;
        m_descriptionSaveTimer = nullptr;
    }

    if (m_reminderTimer) {
        m_reminderTimer->stop();
        delete m_reminderTimer;
        m_reminderTimer = nullptr;
    }
}

//--------Operational Functions--------//
QString Operations_TaskLists::FormatTimeDifference(qint64 seconds)
{
    if (seconds < 0) {
        return "Invalid time";
    }

    // Convert to appropriate time units
    if (seconds < 60) {
        // Less than a minute
        return QString("%1 seconds").arg(seconds);
    } else if (seconds < 3600) {
        // Less than an hour
        int minutes = seconds / 60;
        int remainingSeconds = seconds % 60;
        return QString("%1 minute%2 %3 second%4")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s")
            .arg(remainingSeconds)
            .arg(remainingSeconds == 1 ? "" : "s");
    } else if (seconds < 86400) {
        // Less than a day
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        return QString("%1 hour%2 %3 minute%4")
            .arg(hours)
            .arg(hours == 1 ? "" : "s")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s");
    } else if (seconds < 2592000) {
        // Less than 30 days (approximate month)
        int days = seconds / 86400;
        int hours = (seconds % 86400) / 3600;
        return QString("%1 day%2 %3 hour%4")
            .arg(days)
            .arg(days == 1 ? "" : "s")
            .arg(hours)
            .arg(hours == 1 ? "" : "s");
    } else if (seconds < 31536000) {
        // Less than a year
        int months = seconds / 2592000; // Using 30 days per month as approximation
        int days = (seconds % 2592000) / 86400;
        return QString("%1 month%2 %3 day%4")
            .arg(months)
            .arg(months == 1 ? "" : "s")
            .arg(days)
            .arg(days == 1 ? "" : "s");
    } else {
        // A year or more
        int years = seconds / 31536000; // Using 365 days per year as approximation
        int months = (seconds % 31536000) / 2592000;
        return QString("%1 year%2 %3 month%4")
            .arg(years)
            .arg(years == 1 ? "" : "s")
            .arg(months)
            .arg(months == 1 ? "" : "s");
    }
}

bool Operations_TaskLists::compareTimeValues(int value1, const QString& unit1, int value2, const QString& unit2) {
    // Conversion to minutes (as a common unit of measurement)
    QMap<QString, int> unitToMinutes;
    unitToMinutes["Minutes"] = 1;
    unitToMinutes["Minute"] = 1;
    unitToMinutes["Hours"] = 60;
    unitToMinutes["Hour"] = 60;
    unitToMinutes["Days"] = 60 * 24;
    unitToMinutes["Day"] = 60 * 24;
    unitToMinutes["Months"] = 60 * 24 * 30;  // Approximation
    unitToMinutes["Month"] = 60 * 24 * 30;   // Approximation
    unitToMinutes["Years"] = 60 * 24 * 365;  // Approximation
    unitToMinutes["Year"] = 60 * 24 * 365;   // Approximation

    // Convert both time values to minutes
    int minutes1 = value1 * unitToMinutes[unit1];
    int minutes2 = value2 * unitToMinutes[unit2];

    // Return true if first time value is shorter (smaller) than second
    return minutes1 < minutes2;
}

bool Operations_TaskLists::eventFilter(QObject* watched, QEvent* event)
{
    // Check for focus out event on the description text edit
    if (watched == m_mainWindow->ui->plainTextEdit_TaskDesc && event->type() == QEvent::FocusOut) {
        SaveTaskDescription();
        return false; // Continue with normal event processing
    }

    // Check for key press events
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // Handle Enter/Shift+Enter for task description text edit
        if (watched == m_mainWindow->ui->plainTextEdit_TaskDesc && keyEvent->key() == Qt::Key_Return) {
            // Check if Shift key is pressed
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                // Shift+Enter: Insert newline (let the default handling work)
                return false; // Continue with normal event processing
            } else {
                // Enter without Shift: Save description and set focus to task list display
                SaveTaskDescription();
                m_mainWindow->ui->listWidget_TaskListDisplay->setFocus();
                return true; // We've handled the event
            }
        }

        // Check if the event's source is one of our list widgets and the key is Delete
        if (keyEvent->key() == Qt::Key_Delete) {
            if (watched == m_mainWindow->ui->listWidget_TaskList_List ||
                watched == m_mainWindow->ui->listWidget_TaskListDisplay) {
                handleDeleteKeyPress();
                return true; // We've handled the event
            }
        }
    }

    // Mouse press events on other widgets to save when clicking elsewhere
    if (event->type() == QEvent::MouseButtonPress &&
        watched != m_mainWindow->ui->plainTextEdit_TaskDesc &&
        m_mainWindow->ui->plainTextEdit_TaskDesc->hasFocus()) {
        SaveTaskDescription();
        return false; // Continue with normal event processing
    }

    // Pass the event to the base class
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

void Operations_TaskLists::EditSelectedTask()
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QListWidgetItem* selectedItem = taskListWidget->currentItem();

    if (!selectedItem || (selectedItem->flags() & Qt::ItemIsEnabled) == 0) {
        return; // No valid item selected
    }

    // Store the task information
    currentTaskToEdit = selectedItem->text();
    currentTaskData = selectedItem->data(Qt::UserRole).toString();

    // Update m_currentTaskName
    m_currentTaskName = selectedItem->text();

    // Show dialog for editing task
    ShowTaskMenu(true);
}

void Operations_TaskLists::handleDeleteKeyPress()
{
    if (!m_lastClickedWidget || !m_lastClickedItem) {
        return; // No item selected
    }

    // Check if the item is a placeholder or disabled
    if ((m_lastClickedItem->flags() & Qt::ItemIsEnabled) == 0) {
        return; // Skip placeholder items
    }

    if (m_lastClickedWidget == m_mainWindow->ui->listWidget_TaskList_List) {
        // Delete selected task list
        DeleteTaskList();
    } else if (m_lastClickedWidget == m_mainWindow->ui->listWidget_TaskListDisplay) {
        // Delete selected task
        DeleteTask(m_lastClickedItem->text());
    }
}

QDateTime Operations_TaskLists::CalculateDueDate(const QDateTime& creationDate, int timeValue, const QString& timeUnit)
{
    if (!creationDate.isValid() || timeValue <= 0) {
        return QDateTime(); // Invalid DateTime
    }

    QDateTime dueDate = creationDate;

    // Add the appropriate amount of time based on the unit
    if (timeUnit == "Minutes") {
        dueDate = dueDate.addSecs(timeValue * 60);
    } else if (timeUnit == "Hours") {
        dueDate = dueDate.addSecs(timeValue * 3600);
    } else if (timeUnit == "Days") {
        dueDate = dueDate.addDays(timeValue);
    } else if (timeUnit == "Months") {
        dueDate = dueDate.addMonths(timeValue);
    } else if (timeUnit == "Years") {
        dueDate = dueDate.addYears(timeValue);
    }

    return dueDate;
}

QString Operations_TaskLists::CalculateTimeLeft(const QDateTime& currentDateTime, const QDateTime& dueDateTime)
{
    if (!dueDateTime.isValid() || !currentDateTime.isValid()) {
        return "Unknown";
    }

    // Calculate the time difference in seconds
    qint64 secondsLeft = currentDateTime.secsTo(dueDateTime);

    // If negative, the task is overdue
    if (secondsLeft < 0) {
        return "Overdue";
    }

    // Convert to appropriate time units
    if (secondsLeft < 60) {
        // Less than a minute
        return QString("%1 seconds").arg(secondsLeft);
    } else if (secondsLeft < 3600) {
        // Less than an hour
        int minutes = secondsLeft / 60;
        int seconds = secondsLeft % 60;
        return QString("%1 minute%2 %3 second%4")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s")
            .arg(seconds)
            .arg(seconds == 1 ? "" : "s");
    } else if (secondsLeft < 86400) {
        // Less than a day
        int hours = secondsLeft / 3600;
        int minutes = (secondsLeft % 3600) / 60;
        return QString("%1 hour%2 %3 minute%4")
            .arg(hours)
            .arg(hours == 1 ? "" : "s")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s");
    } else if (secondsLeft < 2592000) {
        // Less than 30 days (approximate month)
        int days = secondsLeft / 86400;
        int hours = (secondsLeft % 86400) / 3600;
        return QString("%1 day%2 %3 hour%4")
            .arg(days)
            .arg(days == 1 ? "" : "s")
            .arg(hours)
            .arg(hours == 1 ? "" : "s");
    } else if (secondsLeft < 31536000) {
        // Less than a year
        int months = secondsLeft / 2592000; // Using 30 days per month as approximation
        int days = (secondsLeft % 2592000) / 86400;
        return QString("%1 month%2 %3 day%4")
            .arg(months)
            .arg(months == 1 ? "" : "s")
            .arg(days)
            .arg(days == 1 ? "" : "s");
    } else {
        // A year or more
        int years = secondsLeft / 31536000; // Using 365 days per year as approximation
        int months = (secondsLeft % 31536000) / 2592000;
        return QString("%1 year%2 %3 month%4")
            .arg(years)
            .arg(years == 1 ? "" : "s")
            .arg(months)
            .arg(months == 1 ? "" : "s");
    }
}

QString Operations_TaskLists::SafeCalculateTimeLeft(qint64 secondsLeft)
{
    // Make sure we don't process negative seconds
    if (secondsLeft < 0) {
        return "Overdue";
    }

    // Convert to appropriate time units
    if (secondsLeft < 60) {
        // Less than a minute
        return QString("%1 seconds").arg(secondsLeft);
    } else if (secondsLeft < 3600) {
        // Less than an hour
        int minutes = secondsLeft / 60;
        int seconds = secondsLeft % 60;
        return QString("%1 minute%2 %3 second%4")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s")
            .arg(seconds)
            .arg(seconds == 1 ? "" : "s");
    } else if (secondsLeft < 86400) {
        // Less than a day
        int hours = secondsLeft / 3600;
        int minutes = (secondsLeft % 3600) / 60;
        return QString("%1 hour%2 %3 minute%4")
            .arg(hours)
            .arg(hours == 1 ? "" : "s")
            .arg(minutes)
            .arg(minutes == 1 ? "" : "s");
    } else if (secondsLeft < 2592000) {
        // Less than 30 days (approximate month)
        int days = secondsLeft / 86400;
        int hours = (secondsLeft % 86400) / 3600;
        return QString("%1 day%2 %3 hour%4")
            .arg(days)
            .arg(days == 1 ? "" : "s")
            .arg(hours)
            .arg(hours == 1 ? "" : "s");
    } else if (secondsLeft < 31536000) {
        // Less than a year
        int months = secondsLeft / 2592000; // Using 30 days per month as approximation
        int days = (secondsLeft % 2592000) / 86400;
        return QString("%1 month%2 %3 day%4")
            .arg(months)
            .arg(months == 1 ? "" : "s")
            .arg(days)
            .arg(days == 1 ? "" : "s");
    } else {
        // A year or more
        int years = secondsLeft / 31536000; // Using 365 days per year as approximation
        int months = (secondsLeft % 31536000) / 2592000;
        return QString("%1 year%2 %3 month%4")
            .arg(years)
            .arg(years == 1 ? "" : "s")
            .arg(months)
            .arg(months == 1 ? "" : "s");
    }
}

QDateTime Operations_TaskLists::CalculateRecurrentDueDate(const QDateTime& creationDateTime,
                                                          const QTime& startTime,
                                                          int frequencyValue,
                                                          const QString& frequencyUnit,
                                                          bool hasTimeLimit,
                                                          int timeLimitValue,
                                                          const QString& timeLimitUnit,
                                                          bool calculateNext,
                                                          const QDateTime& currentDateTime)
{
    if (!creationDateTime.isValid() || !startTime.isValid() || frequencyValue <= 0) {
        return QDateTime(); // Invalid DateTime
    }

    // Base start date time is the creation date with the specified start time
    QDateTime baseDateTime = creationDateTime.date().startOfDay();
    baseDateTime.setTime(startTime);

    // Calculate how many frequency periods have passed since creation
    qint64 secondsElapsed = baseDateTime.secsTo(currentDateTime);
    qint64 frequencyInSeconds = 0;

    // Convert frequency to seconds
    if (frequencyUnit == "Minutes") {
        frequencyInSeconds = frequencyValue * 60;
    } else if (frequencyUnit == "Hours") {
        frequencyInSeconds = frequencyValue * 3600;
    } else if (frequencyUnit == "Days") {
        frequencyInSeconds = frequencyValue * 86400;
    } else if (frequencyUnit == "Months") {
        // Approximation using 30 days per month
        frequencyInSeconds = frequencyValue * 86400 * 30;
    } else if (frequencyUnit == "Years") {
        // Approximation using 365 days per year
        frequencyInSeconds = frequencyValue * 86400 * 365;
    }

    // Calculate how many full periods have passed
    qint64 periodsElapsed = 0;
    if (frequencyInSeconds > 0) {
        periodsElapsed = secondsElapsed / frequencyInSeconds;
    }

    // If we're calculating the next occurrence and the current time is past the current due date
    if (calculateNext) {
        periodsElapsed++; // Advance to the next period
    }

    // Calculate the next occurrence's base time
    QDateTime nextOccurrenceBase;

    // Using direct date operations for more accuracy with months and years
    if (frequencyUnit == "Minutes") {
        nextOccurrenceBase = baseDateTime.addSecs(periodsElapsed * frequencyValue * 60);
    } else if (frequencyUnit == "Hours") {
        nextOccurrenceBase = baseDateTime.addSecs(periodsElapsed * frequencyValue * 3600);
    } else if (frequencyUnit == "Days") {
        nextOccurrenceBase = baseDateTime.addDays(periodsElapsed * frequencyValue);
    } else if (frequencyUnit == "Months") {
        nextOccurrenceBase = baseDateTime.addMonths(periodsElapsed * frequencyValue);
    } else if (frequencyUnit == "Years") {
        nextOccurrenceBase = baseDateTime.addYears(periodsElapsed * frequencyValue);
    }

    // Calculate due date based on time limit or frequency
    QDateTime dueDateTime;

    if (hasTimeLimit && timeLimitValue > 0) {
        // Due date is the next occurrence base time plus the time limit
        if (timeLimitUnit == "Minutes") {
            dueDateTime = nextOccurrenceBase.addSecs(timeLimitValue * 60);
        } else if (timeLimitUnit == "Hours") {
            dueDateTime = nextOccurrenceBase.addSecs(timeLimitValue * 3600);
        } else if (timeLimitUnit == "Days") {
            dueDateTime = nextOccurrenceBase.addDays(timeLimitValue);
        } else if (timeLimitUnit == "Months") {
            dueDateTime = nextOccurrenceBase.addMonths(timeLimitValue);
        } else if (timeLimitUnit == "Years") {
            dueDateTime = nextOccurrenceBase.addYears(timeLimitValue);
        }
    } else {
        // If no time limit, due date is the next occurrence time plus one frequency period
        if (frequencyUnit == "Minutes") {
            dueDateTime = nextOccurrenceBase.addSecs(frequencyValue * 60);
        } else if (frequencyUnit == "Hours") {
            dueDateTime = nextOccurrenceBase.addSecs(frequencyValue * 3600);
        } else if (frequencyUnit == "Days") {
            dueDateTime = nextOccurrenceBase.addDays(frequencyValue);
        } else if (frequencyUnit == "Months") {
            dueDateTime = nextOccurrenceBase.addMonths(frequencyValue);
        } else if (frequencyUnit == "Years") {
            dueDateTime = nextOccurrenceBase.addYears(frequencyValue);
        }
    }

    return dueDateTime;
}

//-----------Tasklist Display-----------//
void Operations_TaskLists::LoadIndividualTasklist(const QString& tasklistName, const QString& taskToSelect)
{
    m_mainWindow->ui->plainTextEdit_TaskDesc->clear();
    // Clear the task display list first
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    taskDisplayWidget->clear();

    // Validate the task list name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(tasklistName, InputValidation::InputType::TaskListName);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task List Name",
                             nameResult.errorMessage);
        return;
    }

    // Sanitize the task list name for file operations
    QString sanitizedName = tasklistName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

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

    // The first line is the date header - skip it for processing tasks
    if (taskLines.isEmpty()) {
        return;
    }

    QString dateHeader = taskLines.first();
    // Set the task list label with the name
    m_mainWindow->ui->label_TaskListName->setText(tasklistName);

    // Process each line in the file (skip header line)
    for (int i = 1; i < taskLines.size(); i++) {
        QString line = taskLines[i];

        // Skip empty lines
        if (line.isEmpty()) {
            continue;
        }

        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');

        // Basic sanity check - ensure we have at least the task type and name
        if (parts.size() < 2) {
            qWarning() << "Invalid task format in file - not enough fields";
            continue;
        }

        QString taskType = parts[0];
        QString taskName = parts[1];

        // Unescape any escaped pipe characters in the task name
        taskName.replace("\\|", "|");

        // Check if the task is completed (field index 3)
        bool isCompleted = false;
        if (parts.size() > 3 && (parts[3] == "1" || parts[3] == "2")) {
            isCompleted = true;
        }

        // Create a list widget item for the task
        QListWidgetItem* item = new QListWidgetItem(taskName);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(isCompleted ? Qt::Checked : Qt::Unchecked);

        // Apply visual formatting based on completion status
        if (isCompleted) {
            // Apply strikethrough and grey out for completed tasks
            QFont font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
            item->setForeground(QColor(100, 100, 100)); // Grey color
        } else {
            // Normal appearance for pending tasks
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
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);  // Make it non-selectable
        taskDisplayWidget->addItem(item);
        // Clear the table
        m_mainWindow->ui->tableWidget_TaskDetails->clear();
        m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
        m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);
    }

    // After loading all tasks, select the appropriate task
    int taskToSelectIndex = -1;

    // If a specific task was requested to be selected
    if (!taskToSelect.isEmpty()) {
        // Find the index of the task with the given name
        for (int i = 0; i < taskDisplayWidget->count(); ++i) {
            QListWidgetItem* item = taskDisplayWidget->item(i);
            if (item->text() == taskToSelect) {
                taskToSelectIndex = i;
                break;
            }
        }
    }

    // If we didn't find the specified task or none was specified,
    // and there are items in the list, select the last item
    if (taskToSelectIndex == -1 && taskDisplayWidget->count() > 0) {
        taskToSelectIndex = taskDisplayWidget->count() - 1;
    }

    // If we have a valid index, select that item
    if (taskToSelectIndex >= 0 && taskToSelectIndex < taskDisplayWidget->count()) {
        taskDisplayWidget->setCurrentRow(taskToSelectIndex);
        QListWidgetItem* selectedItem = taskDisplayWidget->item(taskToSelectIndex);

        // Load the task details for the selected item
        if (selectedItem && (selectedItem->flags() & Qt::ItemIsEnabled)) {
            m_currentTaskName = selectedItem->text();
            LoadTaskDetails(selectedItem->text());
        }
    }

    // Update the appearance of the tasklist in the list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QList<QListWidgetItem*> items = taskListWidget->findItems(tasklistName, Qt::MatchExactly);
    if (!items.isEmpty()) {
        UpdateTasklistAppearance(tasklistName);
    }
}

void Operations_TaskLists::showContextMenu_TaskListDisplay(const QPoint &pos)
{
    // Get the list widget item at the clicked position
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QListWidgetItem* item = taskListWidget->itemAt(pos);

    // Create menu
    QMenu contextMenu(m_mainWindow);

    // Add menu actions
    QAction* newTaskAction = contextMenu.addAction("New Task");

    // Only enable edit and delete if an item is clicked
    QAction* editTaskAction = contextMenu.addAction("Edit Task");
    QAction* deleteTaskAction = contextMenu.addAction("Delete Task");

    // Disable edit and delete options if no item is selected or if it's a placeholder item
    if (!item || (item->flags() & Qt::ItemIsEnabled) == 0) {
        editTaskAction->setEnabled(false);
        deleteTaskAction->setEnabled(false);
    }

    // Connect actions to slots
    connect(newTaskAction, &QAction::triggered, this, [this]() {
        ShowTaskMenu(false); // Show dialog for new task
    });

    connect(editTaskAction, &QAction::triggered, this, [this, item]() {
        if (item) {
            // Store the task name to edit
            currentTaskToEdit = item->text();
            currentTaskData = item->data(Qt::UserRole).toString();

            // Set the current task name to the one we're editing
            m_currentTaskName = item->text();

            ShowTaskMenu(true); // Show dialog for editing task
        }
    });

    connect(deleteTaskAction, &QAction::triggered, this, [this, item]() {
        if (item) {
            DeleteTask(item->text());
        }
    });

    // Show the menu at the cursor position
    contextMenu.exec(taskListWidget->mapToGlobal(pos));
}

void Operations_TaskLists::LoadTaskDetails(const QString& taskName)
{
    // Stop the timer if it was running
    m_timerUpdateTimeLeft->stop();

    // Reset tracking variables
    m_timeLeftRow = -1;
    m_timeLeftCol = -1;
    m_timeLeftVisible = false;
    m_currentTaskType = "";

    // Reset task date variables
    m_taskDueDateTime = QDateTime();
    m_taskCreationDateTime = QDateTime();

    QDateTime currentDateTime = QDateTime::currentDateTime();
    // Store the current task name for future use
    m_currentTaskName = taskName;
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
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

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

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
    // Temporary path for decrypted file
    QString tempPath = taskListFilePath + ".temp";

    // Decrypt the file to a temporary location
    bool decrypted = CryptoUtils::Encryption_DecryptFile(
        m_mainWindow->user_Key, taskListFilePath, tempPath);

    if (!decrypted) {
        QMessageBox::warning(m_mainWindow, "Decryption Failed",
                             "Could not decrypt task list file.");
        return;
    }

    // Set up the table widget
    QTableWidget* taskDetailsTable = m_mainWindow->ui->tableWidget_TaskDetails;

    // Clear the table
    taskDetailsTable->clear();
    taskDetailsTable->setRowCount(0);
    taskDetailsTable->setColumnCount(0);

    // Hide vertical header (row numbers)
    taskDetailsTable->verticalHeader()->setVisible(false);

    // Disable editing
    taskDetailsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Make the table read-only
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
    in.readLine(); // Skip the header line

    QString taskDescription = ""; // For storing task description
    bool taskFound = false;

    // Process each line in the file to find the task
    while (!in.atEnd() && !taskFound) {
        QString line = in.readLine();

        // Skip empty lines
        if (line.isEmpty()) {
            continue;
        }

        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');

        // Basic sanity check - ensure we have at least the task type and name
        if (parts.size() < 2) {
            continue;
        }

        QString taskType = parts[0];
        QString currentTaskName = parts[1];

        // Unescape any escaped pipe characters in the task name
        currentTaskName.replace("\\|", "|");

        // Check if this is the task we're looking for
        if (currentTaskName == taskName) {
            taskFound = true;
            // Set the current task type right when we find the task
            m_currentTaskType = taskType;
            // Store the full task data line
            currentTaskData = line;

            // Check for task description
            // This would be added as an additional field in the task data
            // For example, it could be the last field in the pipe-separated string
            if (parts.size() > 0) {
                for (int i = 0; i < parts.size(); i++) {
                    if (parts[i].startsWith("DESC:")) {
                        taskDescription = parts[i].mid(5); // Remove "DESC:" prefix
                        taskDescription.replace("\\|", "|"); // Unescape pipes
                        taskDescription.replace("\\n", "\n"); // Unescape newlines
                        taskDescription.replace("\\r", "\r"); // Unescape carriage returns
                        break;
                    }
                }
            }

            // Get completion status
            bool isCompleted = (parts.size() > 3 && (parts[3] == "1" || parts[3] == "2"));
            bool isLateCompleted = (parts.size() > 3 && parts[3] == "2");

            // Get log to diary
            QString logToDiary = (parts.size() > 2 && parts[2] == "1") ? "Yes" : "No";

            // Get creation date
            QString creationDate = (parts.size() > 5) ? parts[5] : "Unknown";
            // Parse creation date directly from file
            m_taskCreationDateTime = QDateTime::fromString(creationDate, Qt::ISODate);
            // Format creation date for display
            QString formattedCreationDate = FormatDateTime(m_taskCreationDateTime);

            // Configure table based on task type
            if (taskType == "Simple") {
                // Rest of Simple task handling code...
                // Determine how many columns we need based on completion status
                bool isCompleted = (parts.size() > 3 && parts[3] == "1");
                bool isLateCompleted = (parts.size() > 3 && parts[3] == "2");
                QString completionStatus;

                if (isLateCompleted) {
                    completionStatus = "Late Completion";
                } else if (isCompleted) {
                    completionStatus = "Completed";
                } else {
                    completionStatus = "Pending";
                }

                // MODIFIED: Reduced column count by 1 (removed Log to Diary column)
                int columnCount = isCompleted ? 5 : 3; // Was 6 and 4

                taskDetailsTable->setColumnCount(columnCount);

                QStringList headers;
                headers << "Task Type" << "Status";

                // Add completion-related columns only if the task is completed
                if (isCompleted) {
                    headers << "Completion Time" << "Creation Date" << "Completion Date";
                } else {
                    headers << "Creation Date";
                }

                // REMOVED: Log to Diary column
                taskDetailsTable->setHorizontalHeaderLabels(headers);

                taskDetailsTable->insertRow(0);

                // Task Type
                taskDetailsTable->setItem(0, 0, new QTableWidgetItem("Simple"));

                // Completion Status - with color
                QTableWidgetItem* statusItem = new QTableWidgetItem(completionStatus);
                if (isLateCompleted) {
                    statusItem->setForeground(Qt::yellow);
                } else if (isCompleted) {
                    statusItem->setForeground(Qt::green);
                }
                taskDetailsTable->setItem(0, 1, statusItem);

                // Calculate column index for remaining fields (depends on whether task is completed)
                int creationDateColIndex = isCompleted ? 3 : 2;

                // If task is completed, add completion time and date
                if (isCompleted) {
                    // Get completion date
                    QString completionDateStr = (parts.size() > 4) ? parts[4] : "";
                    QDateTime completionDateTime = QDateTime::fromString(completionDateStr, Qt::ISODate);

                    // Calculate time difference between creation and completion
                    QDateTime creationDateTime = QDateTime::fromString(creationDate, Qt::ISODate);
                    QString completionTimeStr = "Unknown";

                    if (creationDateTime.isValid() && completionDateTime.isValid()) {
                        qint64 secondsElapsed = creationDateTime.secsTo(completionDateTime);
                        completionTimeStr = FormatTimeDifference(secondsElapsed);
                    }

                    // Completion Time
                    taskDetailsTable->setItem(0, 2, new QTableWidgetItem(completionTimeStr));

                    // Completion Date
                    QString formattedCompletionDate = FormatDateTime(completionDateTime);
                    taskDetailsTable->setItem(0, 4, new QTableWidgetItem(formattedCompletionDate));
                }

                // Creation Date - formatted
                taskDetailsTable->setItem(0, creationDateColIndex, new QTableWidgetItem(formattedCreationDate));

                // REMOVED: Log to Diary column
            }

            // For TimeLimit tasks section:
            else if (taskType == "TimeLimit") {
                // Determine how many columns we need based on whether task is completed
                bool isCompleted = (parts.size() > 3 && (parts[3] == "1" || parts[3] == "2"));
                bool isLateCompleted = (parts.size() > 3 && parts[3] == "2");
                bool reminderEnabled = (parts.size() > 10 && parts[10] == "1");

                // Calculate Due Date based on creation date and time limit
                m_taskDueDateTime = CalculateDueDate(m_taskCreationDateTime, parts[6].toInt(), parts[7]);

                // Determine if the task is overdue (only for pending tasks)
                bool isOverdue = false;
                if (!isCompleted && currentDateTime > m_taskDueDateTime) {
                    isOverdue = true;
                }

                // Set status text based on task status
                QString completionStatus;
                if (isLateCompleted) {
                    completionStatus = "Late Completion";
                } else if (isCompleted) {
                    completionStatus = "Completed";
                } else if (isOverdue) {
                    completionStatus = "Overdue";
                } else {
                    completionStatus = "Pending";
                }

                // MODIFIED: Base column count includes: Task Type, Status, Time Left, Time Limit, Due Date
                int baseColumnCount = reminderEnabled ? 6 : 5; // Was 7 and 6
                // Add columns for completion info if completed
                int columnCount = isCompleted ? baseColumnCount + 2 : baseColumnCount;
                // REMOVED: No longer add 1 for Log to Diary

                taskDetailsTable->setColumnCount(columnCount);

                QStringList headers;
                headers << "Task Type" << "Status" << "Time Left" << "Time Limit" << "Due Date";

                if (reminderEnabled) {
                    headers << "Reminder Frequency";
                }

                // Add completion-related columns only if the task is completed
                if (isCompleted) {
                    headers << "Completion Time" << "Creation Date" << "Completion Date";
                } else {
                    headers << "Creation Date";
                }

                // REMOVED: Log to Diary column
                taskDetailsTable->setHorizontalHeaderLabels(headers);

                taskDetailsTable->insertRow(0);

                // Task Type
                taskDetailsTable->setItem(0, 0, new QTableWidgetItem("Time Limit"));

                // Completion Status with appropriate color
                QTableWidgetItem* statusItem = new QTableWidgetItem(completionStatus);
                if (isLateCompleted) {
                    statusItem->setForeground(Qt::yellow);
                } else if (isCompleted) {
                    statusItem->setForeground(Qt::green);
                } else if (isOverdue) {
                    statusItem->setForeground(Qt::red);
                }
                taskDetailsTable->setItem(0, 1, statusItem);

                // Calculate Time Left with appropriate formatting
                QString timeLeft;
                QTableWidgetItem* timeLeftItem;

                if (isCompleted) {
                    // Get completion date
                    QString completionDateStr = (parts.size() > 4) ? parts[4] : "";
                    QDateTime completionDateTime = QDateTime::fromString(completionDateStr, Qt::ISODate);

                    if (completionDateTime.isValid() && m_taskDueDateTime.isValid()) {
                        // Calculate time difference between due date and completion date
                        qint64 secondsLeft = m_taskDueDateTime.secsTo(completionDateTime);

                        if (secondsLeft > 0) {
                            // Task was completed after due date (overdue)
                            // Format as negative time
                            timeLeft = "-" + FormatTimeDifference(secondsLeft);
                        } else {
                            // Task was completed before or at due date
                            timeLeft = FormatTimeDifference(-secondsLeft);
                        }
                    } else {
                        timeLeft = "Unknown";
                    }

                    timeLeftItem = new QTableWidgetItem(timeLeft);
                    // Apply color based on status
                    if (isLateCompleted) {
                        timeLeftItem->setForeground(Qt::yellow);
                    } else {
                        timeLeftItem->setForeground(Qt::green);
                    }
                } else if (isOverdue) {
                    // For overdue tasks, show the time beyond deadline
                    qint64 secondsOverdue = m_taskDueDateTime.secsTo(currentDateTime);
                    timeLeft = "-" + FormatTimeDifference(secondsOverdue);
                    timeLeftItem = new QTableWidgetItem(timeLeft);
                    timeLeftItem->setForeground(Qt::red);
                } else {
                    // For pending tasks, show current time left
                    timeLeft = CalculateTimeLeft(currentDateTime, m_taskDueDateTime);
                    timeLeftItem = new QTableWidgetItem(timeLeft);
                    // Use white color for pending tasks (default)
                }

                taskDetailsTable->setItem(0, 2, timeLeftItem);

                // Time Limit (index 6 and 7)
                QString timeLimit = "Not set";
                if (parts.size() > 7) {
                    timeLimit = parts[6] + " " + parts[7];
                }
                taskDetailsTable->setItem(0, 3, new QTableWidgetItem(timeLimit));

                // Due Date
                QString formattedDueDate = FormatDateTime(m_taskDueDateTime);
                taskDetailsTable->setItem(0, 4, new QTableWidgetItem(formattedDueDate));

                // Calculate column indices - depends on whether reminder is enabled and task is completed
                int reminderColIndex = 5;
                int completionTimeColIndex = reminderEnabled ? 6 : 5;
                int creationDateColIndex = reminderEnabled ? (isCompleted ? 7 : 6) : (isCompleted ? 6 : 5);
                int completionDateColIndex = reminderEnabled ? 8 : 7;
                // REMOVED: logDiaryColIndex is no longer needed

                // Reminder Frequency (if enabled)
                if (reminderEnabled && parts.size() > 12) {
                    QString reminderFreq = parts[11] + " " + parts[12];
                    taskDetailsTable->setItem(0, reminderColIndex, new QTableWidgetItem(reminderFreq));
                }

                // If task is completed, add completion time and date
                if (isCompleted) {
                    // Get completion date
                    QString completionDateStr = (parts.size() > 4) ? parts[4] : "";
                    QDateTime completionDateTime = QDateTime::fromString(completionDateStr, Qt::ISODate);

                    // Calculate time difference between creation and completion
                    QString completionTimeStr = "Unknown";

                    if (m_taskCreationDateTime.isValid() && completionDateTime.isValid()) {
                        qint64 secondsElapsed = m_taskCreationDateTime.secsTo(completionDateTime);
                        completionTimeStr = FormatTimeDifference(secondsElapsed);
                    }

                    // Completion Time
                    taskDetailsTable->setItem(0, completionTimeColIndex, new QTableWidgetItem(completionTimeStr));

                    // Completion Date
                    QString formattedCompletionDate = FormatDateTime(completionDateTime);
                    taskDetailsTable->setItem(0, completionDateColIndex, new QTableWidgetItem(formattedCompletionDate));
                }

                // Creation Date (formatted)
                taskDetailsTable->setItem(0, creationDateColIndex, new QTableWidgetItem(formattedCreationDate));

                // REMOVED: Log to Diary column
            }
            else if (taskType == "Recurrent") {
                // Determine if task is completed and if reminder is enabled
                bool isCompleted = (parts.size() > 3 && (parts[3] == "1" || parts[3] == "2"));
                bool isLateCompleted = (parts.size() > 3 && parts[3] == "2");
                bool reminderEnabled = (parts.size() > 12 && parts[12] == "1");

                // Set completion status with appropriate formatting
                QString completionStatus;
                if (isLateCompleted) {
                    completionStatus = "Late Completion";
                } else if (isCompleted) {
                    completionStatus = "Completed";
                } else {
                    completionStatus = "Pending";
                }

                // Get frequency info
                int frequencyValue = 0;
                QString frequencyUnit = "";
                if (parts.size() > 7) {
                    frequencyValue = parts[6].toInt();
                    frequencyUnit = parts[7];
                }

                // Get start time
                QTime startTime;
                if (parts.size() > 8) {
                    startTime = QTime::fromString(parts[8], "hh:mm:ss");
                }

                // Get time limit info if available
                bool hasTimeLimit = (parts.size() > 9 && parts[9] == "1");
                int timeLimitValue = 0;
                QString timeLimitUnit = "";
                if (hasTimeLimit && parts.size() > 11) {
                    timeLimitValue = parts[10].toInt();
                    timeLimitUnit = parts[11];
                }

                // Calculate Due Date or Next Due Date based on completion status
                if (isCompleted) {
                    m_taskDueDateTime = CalculateRecurrentDueDate(m_taskCreationDateTime, startTime,
                                                        frequencyValue, frequencyUnit,
                                                        hasTimeLimit, timeLimitValue, timeLimitUnit,
                                                        true, currentDateTime);
                } else {
                    m_taskDueDateTime = CalculateRecurrentDueDate(m_taskCreationDateTime, startTime,
                                                        frequencyValue, frequencyUnit,
                                                        hasTimeLimit, timeLimitValue, timeLimitUnit,
                                                        false, currentDateTime);

                    // Check if the task is overdue (only relevant for pending tasks)
                    bool isOverdue = currentDateTime > m_taskDueDateTime;
                    if (isOverdue) {
                        completionStatus = "Overdue";
                    }
                }

                // MODIFIED: Default columns: Task Type, Status, Frequency, Due/Next Due Date, Creation Date
                // +1 for Reminder Time if enabled, +1 for Time Left if pending and not overdue
                // -1 for removing Log to Diary
                int baseColumns = 5; // Was 6
                bool showTimeLeft = !isCompleted && completionStatus != "Overdue";

                // Calculate final column count
                int columnCount = baseColumns;
                if (reminderEnabled) columnCount++;
                if (showTimeLeft) columnCount++;

                taskDetailsTable->setColumnCount(columnCount);

                // Create headers
                QStringList headers;
                headers << "Task Type" << "Status" << "Frequency";

                // Add Time Left column only for pending, non-overdue tasks
                if (showTimeLeft) {
                    headers << "Time Left";
                }

                // Add appropriate due date header
                if (isCompleted) {
                    headers << "Next Due Date";
                } else {
                    headers << "Due Date";
                }

                // Add reminder header if enabled
                if (reminderEnabled) {
                    headers << "Reminder Time";
                }

                // Add creation date header
                headers << "Creation Date";

                // REMOVED: Log to Diary header

                taskDetailsTable->setHorizontalHeaderLabels(headers);
                taskDetailsTable->insertRow(0);

                // Task Type
                taskDetailsTable->setItem(0, 0, new QTableWidgetItem("Recurrent"));

                // Completion Status with color
                QTableWidgetItem* statusItem = new QTableWidgetItem(completionStatus);
                if (isLateCompleted) {
                    statusItem->setForeground(Qt::yellow);
                } else if (isCompleted) {
                    statusItem->setForeground(Qt::green);
                } else if (completionStatus == "Overdue") {
                    statusItem->setForeground(Qt::red);
                }
                taskDetailsTable->setItem(0, 1, statusItem);

                // Frequency (index 6 and 7)
                QString frequency = "Not set";
                if (parts.size() > 7) {
                    frequency = QString("%1 %2").arg(frequencyValue).arg(frequencyUnit);
                }
                taskDetailsTable->setItem(0, 2, new QTableWidgetItem(frequency));

                // Time Left - only add if not overdue and not completed
                int dueDateColIndex = 3; // Default column index for Due Date

                if (showTimeLeft) {
                    // Calculate Time Left
                    QString timeLeft = CalculateTimeLeft(currentDateTime, m_taskDueDateTime);
                    taskDetailsTable->setItem(0, 3, new QTableWidgetItem(timeLeft));
                    dueDateColIndex = 4; // Due Date moved to column 4
                }

                // Due Date or Next Due Date
                QTableWidgetItem* dueDateItem;
                if (m_taskDueDateTime.isValid()) {
                    // Check if overdue for non-completed tasks
                    if (!isCompleted && completionStatus == "Overdue") {
                        dueDateItem = new QTableWidgetItem("Overdue");
                        dueDateItem->setForeground(Qt::red);
                    } else {
                        dueDateItem = new QTableWidgetItem(FormatDateTime(m_taskDueDateTime));
                    }
                } else {
                    dueDateItem = new QTableWidgetItem("Not set");
                }
                taskDetailsTable->setItem(0, dueDateColIndex, dueDateItem);

                // Column index for remaining fields (depends on Time Left and reminder)
                int reminderColIndex = dueDateColIndex + 1;
                int creationDateColIndex = reminderEnabled ? reminderColIndex + 1 : reminderColIndex;
                // REMOVED: logDiaryColIndex is no longer needed

                // Reminder Time (if enabled)
                if (reminderEnabled && parts.size() > 14) {
                    QString reminderTime = parts[13] + " " + parts[14];
                    if (parts.size() > 14) {
                        reminderTime = parts[13] + " " + parts[14] + " before due date";
                    }
                    taskDetailsTable->setItem(0, reminderColIndex, new QTableWidgetItem(reminderTime));
                }

                // Creation Date (formatted)
                taskDetailsTable->setItem(0, creationDateColIndex, new QTableWidgetItem(formattedCreationDate));

                // REMOVED: Log to Diary column
            }

            // Resize columns to content
            taskDetailsTable->resizeColumnsToContents();

            // Break the loop since we found the task
            break;
        }
    }

    file.close();
    QFile::remove(tempPath); // Clean up temporary file

    if (!taskFound) {
        qDebug() << "Could not find the specified task in the task list.";
        return;
    }

    // After setting up the task details table, find and store the Time Left cell position
    taskDetailsTable = m_mainWindow->ui->tableWidget_TaskDetails;

    // Find the Time Left column
    int timeLeftCol = -1;
    for (int col = 0; col < taskDetailsTable->columnCount(); ++col) {
        QTableWidgetItem* headerItem = taskDetailsTable->horizontalHeaderItem(col);
        if (headerItem && headerItem->text() == "Time Left") {
            timeLeftCol = col;
            break;
        }
    }

    // If we found a Time Left column
    if (timeLeftCol >= 0 && taskDetailsTable->rowCount() > 0) {
        m_timeLeftRow = 0; // Assuming task details are always in the first row
        m_timeLeftCol = timeLeftCol;
        m_timeLeftVisible = true;

        // Get the time left cell
        QTableWidgetItem* timeLeftItem = taskDetailsTable->item(m_timeLeftRow, m_timeLeftCol);

        // Only start the timer if there's a valid time left (not "Overdue" or other special state)
        if (timeLeftItem && !timeLeftItem->text().contains("Overdue")) {
            // Make sure timer is stopped before restarting
            if (m_timerUpdateTimeLeft->isActive()) {
                m_timerUpdateTimeLeft->stop();
            }

            // Start the timer
            m_timerUpdateTimeLeft->start();

            // Immediately update once to ensure display is current
            updateTimeLeftCell();
        }
    }


    // Set the task description if found
    if (m_mainWindow->ui->plainTextEdit_TaskDesc) {
        m_mainWindow->ui->plainTextEdit_TaskDesc->setPlainText(taskDescription);
        m_lastSavedDescription = m_mainWindow->ui->plainTextEdit_TaskDesc->toPlainText();
    }

    QTextCursor cursor = m_mainWindow->ui->plainTextEdit_TaskDesc->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_mainWindow->ui->plainTextEdit_TaskDesc->setTextCursor(cursor);

    int itemIndex = 0;
    for (int i = 0; i < m_mainWindow->ui->listWidget_TaskListDisplay->count(); ++i) {
        QListWidgetItem* item = m_mainWindow->ui->listWidget_TaskListDisplay->item(i);
        if (item->text() == taskName) {
            itemIndex = i;
        }
    }

    m_mainWindow->ui->listWidget_TaskListDisplay->setCurrentItem(m_mainWindow->ui->listWidget_TaskListDisplay->item(itemIndex)); // select the task we are loading

    int lastColumn = m_mainWindow->ui->tableWidget_TaskDetails->columnCount() - 1;
    m_mainWindow->ui->tableWidget_TaskDetails->horizontalHeader()->setSectionResizeMode(lastColumn, QHeaderView::Stretch);
}

void Operations_TaskLists::onTaskDisplayItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    // Check if the item is a placeholder or disabled
    if ((item->flags() & Qt::ItemIsEnabled) == 0) {
        return; // Skip placeholder items
    }

    // Store the task name to edit
    currentTaskToEdit = item->text();
    currentTaskData = item->data(Qt::UserRole).toString();

    // Set the current task name to the one we're editing
    m_currentTaskName = item->text();

    // Show dialog for editing task
    ShowTaskMenu(true);
}

QString Operations_TaskLists::FormatDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid()) {
        return "Unknown";
    }

    QDate date = dateTime.date();
    QTime time = dateTime.time();

    // Get day of week (e.g., "Tuesday")
    QString dayOfWeek = Operations::GetDayOfWeek(date);

    // Get day of month (e.g., 22)
    int day = date.day();

    // Get ordinal suffix (e.g., "nd" for 22nd)
    QString ordinalSuffix = Operations::GetOrdinalSuffix(day);

    // Get month name (e.g., "April")
    QString month = date.toString("MMMM");

    // Get year (e.g., 2025)
    int year = date.year();

    // Get time (e.g., "18:08")
    QString timeString = time.toString("HH:mm");

    // Format: "Tuesday the 22nd April 2025 at 18:08"
    return QString("%1 the %2%3 %4 %5 at %6")
        .arg(dayOfWeek)
        .arg(day)
        .arg(ordinalSuffix)
        .arg(month)
        .arg(year)
        .arg(timeString);
}

void Operations_TaskLists::updateTimeLeftCell()
{
    // If there's no time left cell visible, do nothing
    if (!m_timeLeftVisible || m_timeLeftRow < 0 || m_timeLeftCol < 0) {
        m_timerUpdateTimeLeft->stop();
        return;
    }

    QTableWidget* taskDetailsTable = m_mainWindow->ui->tableWidget_TaskDetails;

    // Make sure the table exists and has the correct dimensions
    if (!taskDetailsTable || taskDetailsTable->rowCount() <= m_timeLeftRow ||
        taskDetailsTable->columnCount() <= m_timeLeftCol) {
        m_timerUpdateTimeLeft->stop();
        return;
    }

    // Get task status - Status column is typically at index 1
    QTableWidgetItem* statusItem = taskDetailsTable->item(m_timeLeftRow, 1);
    if (!statusItem) {
        m_timerUpdateTimeLeft->stop();
        return; // No status item found
    }

    QString status = statusItem->text();

    // Don't update if completed or overdue
    if (status == "Completed" || status == "Late Completion" || status == "Overdue") {
        m_timerUpdateTimeLeft->stop();
        return;
    }

    // Make sure the time left cell actually exists before we try to update it
    QTableWidgetItem* timeLeftItem = taskDetailsTable->item(m_timeLeftRow, m_timeLeftCol);
    if (!timeLeftItem) {
        m_timerUpdateTimeLeft->stop();
        return;
    }

    // Get current date-time
    QDateTime currentDateTime = QDateTime::currentDateTime();

    // Extract the task data we need
    QString taskType = m_currentTaskType;
    if (taskType.isEmpty()) {
        m_timerUpdateTimeLeft->stop();
        return;
    }

    // This is where we'll store the calculated time left
    QString newTimeLeft;

    // Calculate time left based on task type
    if (taskType == "TimeLimit") {
        // Use stored due date instead of parsing from table
        if (!m_taskDueDateTime.isValid()) {
            m_timerUpdateTimeLeft->stop();
            return; // Invalid due date
        }

        // Safe calculation of time left
        qint64 secondsLeft = currentDateTime.secsTo(m_taskDueDateTime);

        // Handle zero or negative time - just display "Overdue" rather than crashing
        // Changed from secondsLeft < 0 to secondsLeft <= 0 to catch the exact 0 case
        if (secondsLeft <= 0) {
            // Set time left display to "Overdue"
            newTimeLeft = "Overdue";

            // Update the display
            timeLeftItem->setText(newTimeLeft);
            timeLeftItem->setForeground(Qt::red);

            // Update the status cell to show Overdue
            statusItem->setText("Overdue");
            statusItem->setForeground(Qt::red);

            // Stop the timer to prevent further updates
            m_timerUpdateTimeLeft->stop();
            return;
        } else {
            // Calculate time left text for positive seconds remaining
            newTimeLeft = SafeCalculateTimeLeft(secondsLeft);
        }
    }
    else if (taskType == "Recurrent") {
        // Use stored due date instead of parsing from table
        if (!m_taskDueDateTime.isValid()) {
            m_timerUpdateTimeLeft->stop();
            return; // Invalid due date
        }

        // Safe calculation of time left
        qint64 secondsLeft = currentDateTime.secsTo(m_taskDueDateTime);

        // Handle zero or negative time - just display "Overdue" rather than crashing
        // Changed from secondsLeft < 0 to secondsLeft <= 0 to catch the exact 0 case
        if (secondsLeft <= 0) {
            // Set time left display to "Overdue"
            newTimeLeft = "Overdue";

            // Update the display
            timeLeftItem->setText(newTimeLeft);
            timeLeftItem->setForeground(Qt::red);

            // Update the status cell to show Overdue
            statusItem->setText("Overdue");
            statusItem->setForeground(Qt::red);

            // Stop the timer to prevent further updates
            m_timerUpdateTimeLeft->stop();
            return;
        } else {
            // Calculate time left text for positive seconds remaining
            newTimeLeft = SafeCalculateTimeLeft(secondsLeft);
        }
    }

    // Update the time left cell safely
    if (!newTimeLeft.isEmpty()) {
        timeLeftItem->setText(newTimeLeft);
    }
}

QDateTime Operations_TaskLists::ParseFormattedDateTime(const QString& formattedDateTime)
{
    // The format is: "Tuesday the 22nd April 2025 at 18:08"
    // But we need to also handle seconds if present (e.g. "Tuesday the 22nd April 2025 at 18:08:30")
    QRegularExpression regex("(\\w+) the (\\d+)\\w+ (\\w+) (\\d{4}) at (\\d{1,2}):(\\d{2})(?::(\\d{2}))?");
    QRegularExpressionMatch match = regex.match(formattedDateTime);

    if (!match.hasMatch()) {
        qDebug() << "Failed to match datetime format:" << formattedDateTime;
        return QDateTime(); // Invalid format, return an invalid QDateTime
    }

    // Extract the components with more precise control
    QString dayOfWeek = match.captured(1);
    int day = match.captured(2).toInt();
    QString monthName = match.captured(3);
    int year = match.captured(4).toInt();
    int hour = match.captured(5).toInt();
    int minute = match.captured(6).toInt();
    int seconds = match.lastCapturedIndex() >= 7 ? match.captured(7).toInt() : 0; // Get seconds if present

    // Convert month name to month number more precisely
    QStringList monthNames = {"January", "February", "March", "April", "May", "June",
                             "July", "August", "September", "October", "November", "December"};
    int month = monthNames.indexOf(monthName) + 1;

    if (month <= 0) {
        // Try alternative spelling for March
        if (monthName == "Mars") {
            month = 3;
        } else {
            qDebug() << "Failed to parse month:" << monthName;
            return QDateTime();
        }
    }

    // Create the date and time
    QDate date(year, month, day);
    QTime time(hour, minute, seconds); // Now including seconds

    if (!date.isValid() || !time.isValid()) {
        qDebug() << "Invalid date or time components:" << year << month << day << hour << minute << seconds;
        return QDateTime();
    }

    return QDateTime(date, time);
}
//-----------Tasklists List-----------//
void Operations_TaskLists::CreateNewTaskList()
{
    m_mainWindow->ui->listWidget_TaskList_List->setSortingEnabled(false);

    // Get the list widget
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

    // Create the task list directory and file with the initial unique name
    // This ensures the task list is created even if the user doesn't edit it
    CreateTaskListFile(uniqueName);

    // Make the item editable and select it
    taskListWidget->setCurrentItem(newItem);
    newItem->setFlags(newItem->flags() | Qt::ItemIsEditable);
    taskListWidget->editItem(newItem);

    // Connect to itemChanged signal once to handle the edit completion
    QObject::connect(taskListWidget, &QListWidget::itemChanged, this,
                     [this, taskListWidget, newItem, uniqueName](QListWidgetItem* changedItem) {
                         if (changedItem == newItem) {
                             // Disconnect to avoid processing future edits with this lambda
                             QObject::disconnect(taskListWidget, &QListWidget::itemChanged, this, nullptr);

                             // If the user didn't change the name, we're already done
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
                                 // Reset to the original unique name
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

                             // Use Operations::GetUniqueItemName to ensure a unique name
                             QString newUniqueName = Operations::GetUniqueItemName(listName, existingNames);
                             if (newUniqueName != listName) {
                                 changedItem->setText(newUniqueName);
                                 listName = newUniqueName;
                             }

                             // We need to handle the case where the user changes the name
                             // after we've already created the task list

                             // Create a new task list file with the new name
                             CreateTaskListFile(listName);

                             // Delete the old task list file (with the original unique name)
                             // First, sanitize the old task list name for file operations
                             QString oldSanitizedName = uniqueName;
                             oldSanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

                             // Construct file paths for the old task list
                             QString oldTaskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + oldSanitizedName + "/";
                             QString oldTaskListFilePath = oldTaskListDir + oldSanitizedName + ".txt";

                             // Check if the old file exists
                             QFileInfo fileInfo(oldTaskListFilePath);
                             if (fileInfo.exists() && fileInfo.isFile()) {
                                 // Delete the file
                                 QFile file(oldTaskListFilePath);
                                 file.remove();

                                 // Delete the directory
                                 QDir dir(oldTaskListDir);
                                 dir.removeRecursively();
                             }

                             // Reload the task lists to show the changes
                             //LoadTasklists();
                             //taskListWidget->setCurrentItem(newItem);
                            emit taskListWidget->itemClicked(newItem);

                         }
                     }
                     );
        //emit taskListWidget->itemClicked(taskListWidget->item(taskListWidget->count() - 1));
}

void Operations_TaskLists::CreateTaskListFile(const QString& listName)
{
    // Sanitize the task list name for file operations
    QString sanitizedName = listName;
    // Replace any remaining invalid characters with underscores for file system safety
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

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

    // Format the date: "Monday the 21st of April 2025"
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

void Operations_TaskLists::LoadTasklists()
{
    // Clear the list widget first
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    taskListWidget->clear();
    m_mainWindow->ui->listWidget_TaskList_List->setSortingEnabled(false);

    // Construct the path to the user's Tasklists directory
    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";

    // Validate the path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(tasksListsPath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Invalid tasklists path:" << pathResult.errorMessage;
        return;
    }

    // Check if the directory exists, create it if not
    if (!OperationsFiles::ensureDirectoryExists(tasksListsPath)) {
        qWarning() << "Failed to create Tasklists directory";
        return;
    }

    // Try to load the saved task list order
    QStringList orderedTasklists;
    bool hasOrderFile = LoadTasklistOrder(orderedTasklists);

    // Get all subdirectories (each should represent a task list)
    QDir tasksListsDir(tasksListsPath);
    QStringList taskListDirs = tasksListsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Structure to hold task list info for sorting
    struct TaskListInfo {
        QString name;
        QDateTime creationDate;
        QString displayName;
        int order; // New field for explicit ordering
    };

    QList<TaskListInfo> taskLists;
    QSet<QString> orderedNames; // To track which tasklists are in the order file

    // First pass: Process the tasklists that have a saved order
    if (hasOrderFile) {
        for (int i = 0; i < orderedTasklists.size(); ++i) {
            QString taskListName = orderedTasklists[i];
            QString sanitizedName = taskListName;
            sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

            // Check if this tasklist still exists
            if (taskListDirs.contains(sanitizedName)) {
                QString taskListPath = tasksListsPath + sanitizedName + "/";
                QString taskListFilePath = taskListPath + sanitizedName + ".txt";

                // Validate the file path and check if file exists
                QFileInfo fileInfo(taskListFilePath);
                if (fileInfo.exists() && fileInfo.isFile() &&
                    OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {

                    // Get creation date
                    QDateTime creationDate = fileInfo.birthTime();
                    if (!creationDate.isValid()) {
                        creationDate = fileInfo.lastModified();
                    }

                    // Add to our list with the explicit order
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
        // Skip if already processed
        if (orderedNames.contains(taskListDirName)) {
            continue;
        }

        QString taskListPath = tasksListsPath + taskListDirName + "/";
        QString taskListFilePath = taskListPath + taskListDirName + ".txt";

        // Validate file path and check if file exists
        QFileInfo fileInfo(taskListFilePath);
        if (fileInfo.exists() && fileInfo.isFile() &&
            OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {

            // Get creation date
            QDateTime creationDate = fileInfo.birthTime();
            if (!creationDate.isValid()) {
                creationDate = fileInfo.lastModified();
            }

            // Add to our list with a large order value (to come after ordered items)
            TaskListInfo taskListInfo;
            taskListInfo.name = taskListDirName;
            taskListInfo.creationDate = creationDate;
            taskListInfo.displayName = taskListDirName; // Assuming sanitized name equals display name for unordered items
            taskListInfo.order = orderedTasklists.size() + 1000; // Large value to ensure ordered come first

            taskLists.append(taskListInfo);
        }
    }

    // Sort task lists - first by order, then by creation date for those with same order
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
        // Store the actual task list name as item data for later use
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

void Operations_TaskLists::showContextMenu_TaskListList(const QPoint &pos)
{
    // Get the list widget item at the clicked position
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QListWidgetItem* item = taskListWidget->itemAt(pos);

    // Create menu
    QMenu contextMenu(m_mainWindow);

    // Add menu actions
    QAction* newTaskListAction = contextMenu.addAction("New Tasklist");

    // Add rename option - should be placed between New and Delete
    QAction* renameTaskListAction = contextMenu.addAction("Rename Tasklist");

    // Only enable rename and delete if an item is clicked
    QAction* deleteTaskListAction = contextMenu.addAction("Delete Tasklist");

    // Disable rename and delete options if no item is selected
    if (!item) {
        renameTaskListAction->setEnabled(false);
        deleteTaskListAction->setEnabled(false);
    }

    // Connect actions to slots
    connect(newTaskListAction, &QAction::triggered, this, &Operations_TaskLists::CreateNewTaskList);

    // Connect the rename action to make the item editable
    connect(renameTaskListAction, &QAction::triggered, this, [this, item, taskListWidget]() {
        if (item) {
            // Store the original name before editing
            currentTaskListBeingRenamed = item->text();

            // Make the item editable and start editing
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            taskListWidget->editItem(item);

            // Connect to itemChanged signal to handle the edit completion
            connect(taskListWidget, &QListWidget::itemChanged, this,
                    [this, taskListWidget, item](QListWidgetItem* changedItem) {
                        if (changedItem == item) {
                            // Disconnect to avoid processing future edits with this lambda
                            disconnect(taskListWidget, &QListWidget::itemChanged, this, nullptr);

                            // Call our rename function
                            RenameTasklist(item);
                        }
                    });
        }
    });

    connect(deleteTaskListAction, &QAction::triggered, this, &Operations_TaskLists::DeleteTaskList);

    // Show the menu at the cursor position
    contextMenu.exec(taskListWidget->mapToGlobal(pos));
}

void Operations_TaskLists::RenameTasklist(QListWidgetItem* item)
{
    // Store the original flags before removing ItemIsEditable
    Qt::ItemFlags originalFlags = item->flags();

    //item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    // Get the current and new task list names
    QString originalName = currentTaskListBeingRenamed;
    QString newName = item->text().trimmed();

    // Validate the new task list name
    InputValidation::ValidationResult result =
        InputValidation::validateInput(newName, InputValidation::InputType::TaskListName);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task List Name",
                             result.errorMessage);
        // Revert to the original name
        item->setText(originalName);
        return;
    }

    // Check if the name already exists in the list widget
    QStringList existingNames;
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    for (int i = 0; i < taskListWidget->count(); ++i) {
        QListWidgetItem* existingItem = taskListWidget->item(i);
        if (existingItem != item) {
            existingNames.append(existingItem->text());
        }
    }

    // If the name exists, get a unique name
    if (existingNames.contains(newName)) {
        QString uniqueName = Operations::GetUniqueItemName(newName, existingNames);
        newName = uniqueName;
        item->setText(uniqueName);
    }

    // If the name didn't change, no need to proceed
    if (newName == originalName) {
        return;
    }

    // Sanitize the original and new task list names for file operations
    QString originalSanitizedName = originalName;
    originalSanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    QString newSanitizedName = newName;
    newSanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString originalTaskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + originalSanitizedName + "/";
    QString originalTaskListFilePath = originalTaskListDir + originalSanitizedName + ".txt";

    QString newTaskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + newSanitizedName + "/";
    QString newTaskListFilePath = newTaskListDir + newSanitizedName + ".txt";

    // Validate the file paths
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
                            "Could not validate the task list file. It may be corrupted or tampered with.");
        item->setText(originalName);
        return;
    }
    // Create the new directory if it doesn't exist
    QDir newDir(newTaskListDir);
    if (!newDir.exists()) {
        if (!newDir.mkpath(".")) {
            QMessageBox::warning(m_mainWindow, "Directory Creation Failed",
                                 "Failed to create directory for renamed task list.");
            item->setText(originalName);
            return;
        }
    }

    // Temporary path for decrypted file
    QString tempPath = originalTaskListFilePath + ".temp";

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

    // Clean up temporary file
    QFile::remove(tempPath);

    if (!encrypted) {
        QMessageBox::warning(m_mainWindow, "Encryption Failed",
                             "Could not encrypt the task list file.");
        item->setText(originalName);
        return;
    }

    // Delete the original file and directory
    QFile originalFile(originalTaskListFilePath);
    if (!originalFile.remove()) {
        QMessageBox::warning(m_mainWindow, "File Deletion Failed",
                             "Could not delete the original task list file.");
        // Continue anyway, as the rename was successful
    }

    // Remove the original directory if empty
    QDir originalDir(originalTaskListDir);
    if (originalDir.exists() && originalDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        originalDir.removeRecursively();
    }

    // Update the current selected item
    taskListWidget->setCurrentItem(item);
    // Reload the task list to show the changes
    LoadIndividualTasklist(newName,m_currentTaskName);
    item->setFlags(originalFlags);
}

void Operations_TaskLists::DeleteTaskList()
{
    // Get the current selected task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QListWidgetItem* currentItem = taskListWidget->currentItem();

    if (!currentItem) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select a task list to delete.");
        return;
    }

    QString taskListName = currentItem->text();

    // Confirm deletion with the user
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(m_mainWindow, "Confirm Deletion",
                                  "Are you sure you want to delete the task list \"" + taskListName + "\"?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return; // User canceled the operation
    }

    // Sanitize the task list name for file operations
    QString sanitizedName = taskListName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Create QStringList for hierarchical directory components for the deleteFileAndCleanEmptyDirs function
    QStringList hierarchyLevels;
    hierarchyLevels << "Data" << m_mainWindow->user_Username << "Tasklists" << sanitizedName;

    // Base path for the deleteFileAndCleanEmptyDirs function
    QString basePath = "Data/";

    // Validate file path and check if it exists
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile() ||
        !OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File",
                             "Task list file does not exist or cannot be accessed.");
        return;
    }

    // First, try to use the existing function to delete the file
    bool fileDeleted = OperationsFiles::deleteFileAndCleanEmptyDirs(taskListFilePath, hierarchyLevels, basePath);

    if (!fileDeleted) {
        QMessageBox::warning(m_mainWindow, "Delete Failed",
                             "Could not delete the task list file.");
        return;
    }

    // Regardless of whether the above function cleaned up directories, check the directory
    QDir dir(taskListDir);
    if (dir.exists()) {
        // Check if the directory is empty
        QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        if (entries.isEmpty()) {
            // Directory is empty, remove it
            if (!dir.removeRecursively()) {
                // Just log the warning but continue - the file was deleted successfully
                qWarning() << "Failed to remove empty directory:" << taskListDir;
            }
        } else {
            // Directory is not empty, log what files remain
            qWarning() << "Directory not empty after file deletion:" << taskListDir;
            qWarning() << "Remaining files:" << entries;
        }
    }

    // Store the index of the item to be deleted
    int currentIndex = taskListWidget->row(currentItem);

    // Remove the item from the list widget
    delete taskListWidget->takeItem(taskListWidget->row(currentItem));

    // Clear the task display list
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    taskDisplayWidget->clear();

    // Clear task details
    m_mainWindow->ui->tableWidget_TaskDetails->clear();
    m_mainWindow->ui->tableWidget_TaskDetails->setRowCount(0);
    m_mainWindow->ui->tableWidget_TaskDetails->setColumnCount(0);

    // Clear task description
    m_mainWindow->ui->plainTextEdit_TaskDesc->clear();

    // Clear current task list label
    m_mainWindow->ui->label_TaskListName->clear();

    // Clear the last clicked item reference
    m_lastClickedItem = nullptr;
    m_lastClickedWidget = nullptr;

    // If there are remaining task lists, select one
    if (taskListWidget->count() > 0) {
        // If the deleted item was the last one, select the new last item
        int newIndex = (currentIndex >= taskListWidget->count()) ?
                           taskListWidget->count() - 1 : currentIndex;

        // Select and "click" the item at the new index
        taskListWidget->setCurrentRow(newIndex);
        QListWidgetItem* newCurrentItem = taskListWidget->item(newIndex);
        if (newCurrentItem) {
            // Simulate a click by calling our click handler
            onTaskListItemClicked(newCurrentItem);
            // Also emit the itemClicked signal to ensure any connected slots are called
            emit taskListWidget->itemClicked(newCurrentItem);
        }
    }
}

void Operations_TaskLists::onTaskListItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    // Store the original name before editing
    currentTaskListBeingRenamed = item->text();

    // Make the item editable and start editing
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_mainWindow->ui->listWidget_TaskList_List->editItem(item);

    // Connect to itemChanged signal to handle the edit completion
    connect(m_mainWindow->ui->listWidget_TaskList_List, &QListWidget::itemChanged, this,
            [this, item](QListWidgetItem* changedItem) {
                if (changedItem == item) {
                    // Disconnect to avoid processing future edits with this lambda
                    disconnect(m_mainWindow->ui->listWidget_TaskList_List, &QListWidget::itemChanged, this, nullptr);

                    // Call our rename function
                    RenameTasklist(item);
                }
            });
}

bool Operations_TaskLists::AreAllTasksCompleted(const QString& tasklistName)
{
    // Validate the task list name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(tasklistName, InputValidation::InputType::TaskListName);
    if (!nameResult.isValid) {
        qWarning() << "Invalid task list name when checking completion status:" << nameResult.errorMessage;
        return false;
    }

    // Sanitize the task list name for file operations
    QString sanitizedName = tasklistName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        qWarning() << "Invalid file path when checking tasklist completion";
        return false;
    }

    // Check if the file exists
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "Task list file does not exist when checking completion";
        return false;
    }

    // Read the task list file
    QStringList taskLines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, taskLines)) {
        qWarning() << "Failed to read task list file when checking completion";
        return false;
    }

    bool hasAnyTasks = false;
    bool allTasksCompleted = true;

    // Process each line in the file to check task status (skip header line)
    for (int i = 1; i < taskLines.size(); i++) {
        QString line = taskLines[i];

        // Skip empty lines
        if (line.isEmpty()) {
            continue;
        }

        // Parse the task data (pipe-separated values)
        QStringList parts = line.split('|');

        // Basic sanity check - ensure we have at least the task type and name
        if (parts.size() < 2) {
            continue;
        }

        // We found at least one task
        hasAnyTasks = true;

        // Check if this task is completed (field index 3)
        bool isCompleted = false;
        if (parts.size() > 3) {
            isCompleted = (parts[3] == "1" || parts[3] == "2");
        }

        // If we find any task that is not completed, the whole list is not completed
        if (!isCompleted) {
            allTasksCompleted = false;
            break;
        }
    }

    // If there are no tasks, return false (don't strikethrough empty tasklists)
    if (!hasAnyTasks) {
        return false;
    }

    return allTasksCompleted;
}

void Operations_TaskLists::UpdateTasklistAppearance(const QString& tasklistName)
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;

    // Find the item with the given name
    QList<QListWidgetItem*> items = taskListWidget->findItems(tasklistName, Qt::MatchExactly);
    if (items.isEmpty()) {
        return;
    }

    QListWidgetItem* item = items.first();
    bool allCompleted = AreAllTasksCompleted(tasklistName);

    if (allCompleted) {
        // Apply strikethrough and grey out
        QFont font = item->font();
        font.setStrikeOut(true);
        item->setFont(font);
        item->setForeground(QColor(100, 100, 100)); // Grey color
    } else {
        // Restore normal appearance
        QFont font = item->font();
        font.setStrikeOut(false);
        item->setFont(font);
        item->setForeground(QColor(255, 255, 255)); // White color
    }
}

//----------Task Manager-------//
void Operations_TaskLists::ShowTaskMenu(bool editMode)
{
    // Create dialog
    QDialog dialog;
    Ui::tasklists_addtask ui;
    ui.setupUi(&dialog);
    //Hide recurrent task because we are disabling them until I fix or rework the entire tasklist system.
    ui.radioButton_TaskRecurrent->setHidden(true);
    ui.label_RecurrentTask->setHidden(true);
    //hide log task to diary temporarily until I decide what to do
    ui.groupBox_LogTask->setHidden(true);
    // Set the window title
    dialog.setWindowTitle(editMode ? "Edit Task" : "Add Task");

    // Define style sheets for disabled and enabled states
    const QString disabledLabelStyle = "color: rgb(100, 100, 100);"; // Darker grey text only for labels
    const QString disabledWidgetStyle = "color: rgb(100, 100, 100); background-color: rgb(60, 60, 60);"; // For input widgets
    const QString enabledStyle = ""; // Default style (empty string)

    // Variables to store original task data if editing
    QString originalTaskName;
    QString taskType;
    bool originalLogTask = false;
    QString taskDescription = ""; // Initialize task description

    // Get current task list for checking duplicate names
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QStringList existingTaskNames;

    if (taskListWidget->currentItem() != nullptr) {
        QString currentTaskList = taskListWidget->currentItem()->text();
        ui.label_TaskListName->setText(currentTaskList);
        // Get all existing task names
        QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
        for (int i = 0; i < taskDisplayWidget->count(); ++i) {
            QListWidgetItem* item = taskDisplayWidget->item(i);
            // Skip placeholder or disabled items
            if ((item->flags() & Qt::ItemIsEnabled) == 0) {
                continue;
            }
            existingTaskNames.append(item->text());
        }
        LoadIndividualTasklist(currentTaskList,m_currentTaskName);
    }
    else
    {
        QMessageBox::warning(m_mainWindow,
            "Warning",
            "Need to open a tasklist first.",
            QMessageBox::Ok
            );
        return;
    }

    // In edit mode, parse the current task data and set up UI accordingly
    if (editMode && !currentTaskData.isEmpty()) {
        QStringList parts = currentTaskData.split('|');

        if (parts.size() >= 2) {
            taskType = parts[0];
            originalTaskName = parts[1];
            originalTaskName.replace("\\|", "|"); // Unescape pipe characters

            // Set task name
            ui.lineEdit_TaskName->setText(originalTaskName);

            // Set log to diary checkbox (should be at index 2)
            if (parts.size() > 2) {
                originalLogTask = (parts[2] == "1");
                ui.checkBox_TaskLogDiary->setChecked(originalLogTask);
            }

            // Extract task description if it exists
            // The description is stored at the end with a "DESC:" prefix
            for (int i = 0; i < parts.size(); i++) {
                if (parts[i].startsWith("DESC:")) {
                    taskDescription = parts[i].mid(5); // Remove "DESC:" prefix
                    taskDescription.replace("\\|", "|"); // Unescape pipes
                    taskDescription.replace("\\n", "\n"); // Unescape newlines
                    taskDescription.replace("\\r", "\r"); // Unescape carriage returns

                    // Validate the task description
                    InputValidation::ValidationResult descResult =
                        InputValidation::validateInput(taskDescription, InputValidation::InputType::PlainText);
                    if (!descResult.isValid) {
                        QMessageBox::warning(m_mainWindow, "Invalid Task Description",
                                             descResult.errorMessage);
                        taskDescription = ""; // Reset to empty string if invalid
                    }
                    break;
                }
            }
            ui.plainTextEdit_TaskDesc->setPlainText(taskDescription);
            // Select the appropriate task type radio button and set up UI
            if (taskType == "Simple") {
                ui.radioButton_TaskSimple->setChecked(true);
                ui.stackedWidget->setCurrentIndex(0);

                // Set congratulatory message if available (index 6 for simple tasks)
                if (parts.size() > 6) {
                    QString cmess = parts[6];
                    cmess.replace("\\|", "|");
                    int index = ui.comboBox_Simple_CMess->findText(cmess);
                    if (index >= 0) {
                        ui.comboBox_Simple_CMess->setCurrentIndex(index);
                    }
                }
            }
            else if (taskType == "TimeLimit") {
                ui.radioButton_TaskTimed->setChecked(true);
                ui.stackedWidget->setCurrentIndex(1);

                // Set time limit value and type (index 6 and 7)
                if (parts.size() > 7) {
                    ui.spinBox_Timed_TLimit->setValue(parts[6].toInt());
                    int typeIndex = ui.comboBox_Timed_TLimit->findText(parts[7]);
                    if (typeIndex >= 0) {
                        ui.comboBox_Timed_TLimit->setCurrentIndex(typeIndex);
                    }
                }

                // Set congratulatory message (index 8)
                if (parts.size() > 8) {
                    QString cmess = parts[8];
                    cmess.replace("\\|", "|");
                    int index = ui.comboBox_Timed_CMess->findText(cmess);
                    if (index >= 0) {
                        ui.comboBox_Timed_CMess->setCurrentIndex(index);
                    }
                }

                // Set punitive message (index 9)
                if (parts.size() > 9) {
                    QString pmess = parts[9];
                    pmess.replace("\\|", "|");
                    int index = ui.comboBox_Timed_PMess->findText(pmess);
                    if (index >= 0) {
                        ui.comboBox_Timed_PMess->setCurrentIndex(index);
                    }
                }

                // Set reminder enabled (index 10)
                if (parts.size() > 10) {
                    bool reminderEnabled = (parts[10] == "1");
                    ui.checkBox_Timed_Reminder->setChecked(reminderEnabled);

                    // Set reminder frequency and type if enabled (index 11 and 12)
                    if (reminderEnabled && parts.size() > 12) {
                        ui.spinBox_Timed_RFreq->setValue(parts[11].toInt());
                        int typeIndex = ui.comboBox_Timed_RFreq->findText(parts[12]);
                        if (typeIndex >= 0) {
                            ui.comboBox_Timed_RFreq->setCurrentIndex(typeIndex);
                        }
                    }
                }
            }
            else if (taskType == "Recurrent") {
                ui.radioButton_TaskRecurrent->setChecked(true);
                ui.stackedWidget->setCurrentIndex(2);

                // Set frequency value and type (index 6 and 7)
                if (parts.size() > 7) {
                    ui.spinBox_Rec_Freq->setValue(parts[6].toInt());
                    int typeIndex = ui.comboBox_Rec_Freq->findText(parts[7]);
                    if (typeIndex >= 0) {
                        ui.comboBox_Rec_Freq->setCurrentIndex(typeIndex);
                    }
                }

                // Set start time (index 8)
                if (parts.size() > 8) {
                    QTime startTime = QTime::fromString(parts[8], "hh:mm:ss");
                    if (startTime.isValid()) {
                        ui.timeEdit_Rec_Start->setTime(startTime);
                    }
                }

                // Set time limit enabled (index 9)
                if (parts.size() > 9) {
                    bool timeLimitEnabled = (parts[9] == "1");
                    ui.checkBox_Rec_TLimit->setChecked(timeLimitEnabled);

                    // Set time limit value and type if enabled (index 10 and 11)
                    if (timeLimitEnabled && parts.size() > 11) {
                        ui.spinBox_Rec_TLimit->setValue(parts[10].toInt());
                        int typeIndex = ui.comboBox_Rec_TLimit->findText(parts[11]);
                        if (typeIndex >= 0) {
                            ui.comboBox_Rec_TLimit->setCurrentIndex(typeIndex);
                        }
                    }
                }

                // Set reminder enabled (index 12)
                if (parts.size() > 12) {
                    bool reminderEnabled = (parts[12] == "1");
                    ui.checkBox_Rec_Reminder->setChecked(reminderEnabled);

                    // Set reminder value and type if enabled (index 13 and 14)
                    if (reminderEnabled && parts.size() > 14) {
                        ui.spinBox_Rec_Reminder->setValue(parts[13].toInt());
                        int typeIndex = ui.comboBox_Rec_Reminder->findText(parts[14]);
                        if (typeIndex >= 0) {
                            ui.comboBox_Rec_Reminder->setCurrentIndex(typeIndex);
                        }
                    }
                }
            }
        }
    } else {
        // Default for new task - using user settings

        // Set log to diary checkbox based on user settings
        ui.checkBox_TaskLogDiary->setChecked(m_mainWindow->setting_TLists_LogToDiary);

        // Set default CMess/PMess for all task types (regardless of which one is active)
        // This ensures the values are already set if the user switches task types

        // Set CMess for Simple tasks
        int simpleCMessIndex = ui.comboBox_Simple_CMess->findText(m_mainWindow->setting_TLists_CMess);
        if (simpleCMessIndex >= 0) {
            ui.comboBox_Simple_CMess->setCurrentIndex(simpleCMessIndex);
        }

        // Set CMess and PMess for TimeLimit tasks
        int timedCMessIndex = ui.comboBox_Timed_CMess->findText(m_mainWindow->setting_TLists_CMess);
        if (timedCMessIndex >= 0) {
            ui.comboBox_Timed_CMess->setCurrentIndex(timedCMessIndex);
        }

        int timedPMessIndex = ui.comboBox_Timed_PMess->findText(m_mainWindow->setting_TLists_PMess);
        if (timedPMessIndex >= 0) {
            ui.comboBox_Timed_PMess->setCurrentIndex(timedPMessIndex);
        }

        // Now select the appropriate task type based on user settings
        QString taskType = m_mainWindow->setting_TLists_TaskType;
        qDebug() << "Default task type from settings:" << taskType;

        if (taskType == "Time Limit" || taskType == "TimeLimit") {
            qDebug() << "Setting Time Limit task type";
            ui.radioButton_TaskTimed->setChecked(true);
            ui.stackedWidget->setCurrentIndex(1);
            ui.lineEdit_TaskName->setPlaceholderText("Time Limit Task");
        }
        else if (taskType == "Recurrent") {
            qDebug() << "Setting Recurrent task type";
            ui.radioButton_TaskRecurrent->setChecked(true);
            ui.stackedWidget->setCurrentIndex(2);
            ui.lineEdit_TaskName->setPlaceholderText("Recurrent Task");
        }
        else { // Default to Simple if setting is not recognized or is "Simple"
            qDebug() << "Setting Simple task type (default)";
            ui.radioButton_TaskSimple->setChecked(true);
            ui.stackedWidget->setCurrentIndex(0);
            ui.lineEdit_TaskName->setPlaceholderText("Simple Task");
        }
    }

    // Connect radio buttons to change the stacked widget page and update placeholder text
    connect(ui.radioButton_TaskSimple, &QRadioButton::toggled, [&](bool checked) {
        if (checked) {
            ui.stackedWidget->setCurrentIndex(0); // Simple Task page
            ui.lineEdit_TaskName->setPlaceholderText("Simple Task");
        }
    });

    connect(ui.radioButton_TaskTimed, &QRadioButton::toggled, [&](bool checked) {
        if (checked) {
            ui.stackedWidget->setCurrentIndex(1); // Time Limit Task page
            ui.lineEdit_TaskName->setPlaceholderText("Time Limit Task");
        }
    });

    connect(ui.radioButton_TaskRecurrent, &QRadioButton::toggled, [&](bool checked) {
        if (checked) {
            ui.stackedWidget->setCurrentIndex(2); // Recurrent Task page
            ui.lineEdit_TaskName->setPlaceholderText("Recurrent Task");
        }
    });

    // Time Limit Task page - Reminder checkbox controls
    // Initial state - disable reminder frequency controls if checkbox is unchecked
    bool reminderEnabled = ui.checkBox_Timed_Reminder->isChecked();
    ui.label_Timed_RFreq->setEnabled(reminderEnabled);
    ui.spinBox_Timed_RFreq->setEnabled(reminderEnabled);
    ui.comboBox_Timed_RFreq->setEnabled(reminderEnabled);

    // Apply style based on state - different styles for labels and widgets
    ui.label_Timed_RFreq->setStyleSheet(reminderEnabled ? enabledStyle : disabledLabelStyle);
    ui.spinBox_Timed_RFreq->setStyleSheet(reminderEnabled ? enabledStyle : disabledWidgetStyle);
    ui.comboBox_Timed_RFreq->setStyleSheet(reminderEnabled ? enabledStyle : disabledWidgetStyle);

    // Connect the reminder checkbox to enable/disable and change style of related controls
    connect(ui.checkBox_Timed_Reminder, &QCheckBox::toggled,
            [&, enabledStyle, disabledLabelStyle, disabledWidgetStyle](bool checked) {
                ui.label_Timed_RFreq->setEnabled(checked);
                ui.spinBox_Timed_RFreq->setEnabled(checked);
                ui.comboBox_Timed_RFreq->setEnabled(checked);

                // Update styles based on checked state - different styles for labels and widgets
                ui.label_Timed_RFreq->setStyleSheet(checked ? enabledStyle : disabledLabelStyle);
                ui.spinBox_Timed_RFreq->setStyleSheet(checked ? enabledStyle : disabledWidgetStyle);
                ui.comboBox_Timed_RFreq->setStyleSheet(checked ? enabledStyle : disabledWidgetStyle);
            });

    // Recurrent Task page - Time Limit checkbox controls
    // Initial state - disable time limit controls if checkbox is unchecked
    bool timeLimitEnabled = ui.checkBox_Rec_TLimit->isChecked();
    ui.label_Rec_TLimit->setEnabled(timeLimitEnabled);
    ui.spinBox_Rec_TLimit->setEnabled(timeLimitEnabled);
    ui.comboBox_Rec_TLimit->setEnabled(timeLimitEnabled);

    // Apply style based on state - different styles for labels and widgets
    ui.label_Rec_TLimit->setStyleSheet(timeLimitEnabled ? enabledStyle : disabledLabelStyle);
    ui.spinBox_Rec_TLimit->setStyleSheet(timeLimitEnabled ? enabledStyle : disabledWidgetStyle);
    ui.comboBox_Rec_TLimit->setStyleSheet(timeLimitEnabled ? enabledStyle : disabledWidgetStyle);

    // Connect the time limit checkbox to enable/disable and change style of related controls
    connect(ui.checkBox_Rec_TLimit, &QCheckBox::toggled,
            [&, enabledStyle, disabledLabelStyle, disabledWidgetStyle](bool checked) {
                ui.label_Rec_TLimit->setEnabled(checked);
                ui.spinBox_Rec_TLimit->setEnabled(checked);
                ui.comboBox_Rec_TLimit->setEnabled(checked);

                // Update styles based on checked state - different styles for labels and widgets
                ui.label_Rec_TLimit->setStyleSheet(checked ? enabledStyle : disabledLabelStyle);
                ui.spinBox_Rec_TLimit->setStyleSheet(checked ? enabledStyle : disabledWidgetStyle);
                ui.comboBox_Rec_TLimit->setStyleSheet(checked ? enabledStyle : disabledWidgetStyle);
            });

    // Recurrent Task page - Reminder checkbox controls
    // Initial state - disable reminder controls if checkbox is unchecked
    bool recReminderEnabled = ui.checkBox_Rec_Reminder->isChecked();
    ui.label_Rec_Reminder->setEnabled(recReminderEnabled);
    ui.label_Rec_ReminderBefore->setEnabled(recReminderEnabled);
    ui.spinBox_Rec_Reminder->setEnabled(recReminderEnabled);
    ui.comboBox_Rec_Reminder->setEnabled(recReminderEnabled);

    // Apply style based on state - different styles for labels and widgets
    ui.label_Rec_Reminder->setStyleSheet(recReminderEnabled ? enabledStyle : disabledLabelStyle);
    ui.label_Rec_ReminderBefore->setStyleSheet(recReminderEnabled ? enabledStyle : disabledLabelStyle);
    ui.spinBox_Rec_Reminder->setStyleSheet(recReminderEnabled ? enabledStyle : disabledWidgetStyle);
    ui.comboBox_Rec_Reminder->setStyleSheet(recReminderEnabled ? enabledStyle : disabledWidgetStyle);

    // Connect the reminder checkbox to enable/disable and change style of related controls
    connect(ui.checkBox_Rec_Reminder, &QCheckBox::toggled,
            [&, enabledStyle, disabledLabelStyle, disabledWidgetStyle](bool checked) {
                ui.label_Rec_Reminder->setEnabled(checked);
                ui.label_Rec_ReminderBefore->setEnabled(checked);
                ui.spinBox_Rec_Reminder->setEnabled(checked);
                ui.comboBox_Rec_Reminder->setEnabled(checked);

                // Update styles based on checked state - different styles for labels and widgets
                ui.label_Rec_Reminder->setStyleSheet(checked ? enabledStyle : disabledLabelStyle);
                ui.label_Rec_ReminderBefore->setStyleSheet(checked ? enabledStyle : disabledLabelStyle);
                ui.spinBox_Rec_Reminder->setStyleSheet(checked ? enabledStyle : disabledWidgetStyle);
                ui.comboBox_Rec_Reminder->setStyleSheet(checked ? enabledStyle : disabledWidgetStyle);
            });

    // Connect Save and Exit button
    connect(ui.pushButton_SaveExit, &QPushButton::clicked, [&]() {
        // Get task name or use placeholder if empty
        QString taskName = ui.lineEdit_TaskName->text().trimmed();
        if (taskName.isEmpty()) {
            taskName = ui.lineEdit_TaskName->placeholderText();
        }

        // Check for duplicate task names (unless it's the same as original in edit mode)
        if (!taskName.isEmpty() && existingTaskNames.contains(taskName) &&
            (!editMode || (editMode && taskName != originalTaskName))) {
            QMessageBox::warning(&dialog, "Duplicate Task Name",
                                 "A task with this name already exists. Please choose a different name.");
            return;
        }
        // Get the description from the plainTextEdit widget
        QString taskDescription = ui.plainTextEdit_TaskDesc->toPlainText();

        // Validate the description
        InputValidation::ValidationResult descResult =
            InputValidation::validateInput(taskDescription, InputValidation::InputType::PlainText);
        if (!descResult.isValid) {
            QMessageBox::warning(&dialog, "Invalid Description",
                                 descResult.errorMessage);
            return;
        }
        bool logTask = ui.checkBox_TaskLogDiary->isChecked();

        if (ui.radioButton_TaskSimple->isChecked()) {
            // Simple Task - no time validation needed
            QString cmess = ui.comboBox_Simple_CMess->currentText();

            if (editMode) {
                ModifyTaskSimple(originalTaskName, taskName, logTask, cmess, taskDescription);
            } else {
                AddTaskSimple(taskName, logTask, cmess, taskDescription);
            }
            dialog.accept();
        }
        else if (ui.radioButton_TaskTimed->isChecked()) {
            // Time Limit Task
            int value_TLimit = ui.spinBox_Timed_TLimit->value();
            QString type_TLimit = ui.comboBox_Timed_TLimit->currentText();
            QString cmess = ui.comboBox_Timed_CMess->currentText();
            QString pmess = ui.comboBox_Timed_PMess->currentText();
            bool reminder = ui.checkBox_Timed_Reminder->isChecked();

            // Validation for Time Limit Task
            bool validationPassed = true;

            // If reminder is enabled, check that reminder frequency is shorter than time limit
            if (reminder) {
                int value_RFreq = ui.spinBox_Timed_RFreq->value();
                QString type_RFreq = ui.comboBox_Timed_RFreq->currentText();

                // Only perform validation if both values are > 0
                if (value_TLimit > 0 && value_RFreq > 0) {
                    // Check if reminder frequency is shorter than time limit
                    if (!compareTimeValues(value_RFreq, type_RFreq, value_TLimit, type_TLimit)) {
                        QMessageBox::warning(&dialog, "Invalid Time Values",
                                             "Reminder frequency must be shorter than Time Limit.");
                        validationPassed = false;
                    }
                }

                // If validation passed, proceed with adding/modifying the task
                if (validationPassed) {
                    if (editMode) {
                        ModifyTaskTimeLimit(originalTaskName, taskName, logTask, value_TLimit, type_TLimit,
                                            cmess, pmess, reminder, value_RFreq, type_RFreq, taskDescription);
                    } else {
                        AddTaskTimeLimit(taskName, logTask, value_TLimit, type_TLimit,
                                         cmess, pmess, reminder, value_RFreq, type_RFreq, taskDescription);
                    }
                    dialog.accept();
                }
            } else {
                // If reminder is not enabled, no need for time validation
                if (editMode) {
                    ModifyTaskTimeLimit(originalTaskName, taskName, logTask, value_TLimit, type_TLimit,
                                        cmess, pmess, reminder, 0, "", taskDescription);
                } else {
                    AddTaskTimeLimit(taskName, logTask, value_TLimit, type_TLimit,
                                     cmess, pmess, reminder, 0, "", taskDescription);
                }
                dialog.accept();
            }
        }
        else if (ui.radioButton_TaskRecurrent->isChecked()) {
            // Recurrent Task
            int value_Freq = ui.spinBox_Rec_Freq->value();
            QString type_Freq = ui.comboBox_Rec_Freq->currentText();
            QTime startTime = ui.timeEdit_Rec_Start->time();
            bool timeLimit = ui.checkBox_Rec_TLimit->isChecked();
            bool reminder = ui.checkBox_Rec_Reminder->isChecked();

            // Variables for validation
            bool validationPassed = true;

            // Values for time limit and reminder (only use if enabled)
            int value_TLimit = 0;
            QString type_TLimit = "";
            int value_Reminder = 0;
            QString type_Reminder = "";

            // Check time limit validation if enabled
            if (timeLimit) {
                value_TLimit = ui.spinBox_Rec_TLimit->value();
                type_TLimit = ui.comboBox_Rec_TLimit->currentText();

                // Only perform validation if both values are > 0
                if (value_Freq > 0 && value_TLimit > 0) {
                    // Check if time limit is shorter than frequency
                    if (!compareTimeValues(value_TLimit, type_TLimit, value_Freq, type_Freq)) {
                        QMessageBox::warning(&dialog, "Invalid Time Values",
                                             "Time limit must be shorter than Task Frequency.");
                        validationPassed = false;
                    }
                }
            }

            // Check reminder validation if enabled
            if (reminder && validationPassed) {
                value_Reminder = ui.spinBox_Rec_Reminder->value();
                type_Reminder = ui.comboBox_Rec_Reminder->currentText();

                // Only perform validation if both values are > 0
                if (value_Freq > 0 && value_Reminder > 0) {
                    // Check if reminder time is shorter than frequency
                    if (!compareTimeValues(value_Reminder, type_Reminder, value_Freq, type_Freq)) {
                        QMessageBox::warning(&dialog, "Invalid Time Values",
                                             "Reminder time must be shorter than Task Frequency.");
                        validationPassed = false;
                    }
                }
            }

            // If all validations passed, add or modify the task
            if (validationPassed) {
                if (editMode) {
                    ModifyTaskRecurrent(originalTaskName, taskName, logTask, value_Freq, type_Freq,
                                        startTime, timeLimit, value_TLimit, type_TLimit,
                                        reminder, value_Reminder, type_Reminder, taskDescription);
                } else {
                    AddTaskRecurrent(taskName, logTask, value_Freq, type_Freq,
                                     startTime, timeLimit, value_TLimit, type_TLimit,
                                     reminder, value_Reminder, type_Reminder, taskDescription);
                }
                dialog.accept();
            }
        }
    });

    // Connect Exit without Saving button
    connect(ui.pushButton_ExitNOSave, &QPushButton::clicked, [&]() {
        dialog.reject();
    });

    // Show the dialog (modal)
    dialog.exec();
}

void Operations_TaskLists::DeleteTask(const QString& taskName)
{
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Confirm deletion with the user
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(m_mainWindow, "Confirm Deletion",
                                  "Are you sure you want to delete the task \"" + taskName + "\"?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return; // User canceled the operation
    }

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate file path and check if file exists
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile() ||
        !OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File",
                             "Task list file does not exist or cannot be accessed.");
        return;
    }

    // Use the removeTaskEntry function to remove the task
    if (!OperationsFiles::removeTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName)) {
        QMessageBox::warning(m_mainWindow, "Deletion Failed",
                             "Failed to delete the task from the file.");
        return;
    }

    // Remove from due queue if it's a TimeLimit task
    if (m_currentTaskType == "TimeLimit") {
        QString taskId = currentTaskList + "::" + taskName;
        RemoveTaskFromDueQueue(taskId);
    }

    // Find the task in the display list
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QList<QListWidgetItem*> items = taskDisplayWidget->findItems(taskName, Qt::MatchExactly);
    int currentIndex = -1;

    if (!items.isEmpty()) {
        currentIndex = taskDisplayWidget->row(items.first());
    }

    // Clear the last clicked item reference if it was the item being deleted
    if (m_lastClickedWidget == taskDisplayWidget && m_lastClickedItem &&
        m_lastClickedItem->text() == taskName) {
        m_lastClickedItem = nullptr;
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the task list to show the changes
    LoadIndividualTasklist(currentTaskList, taskName);

    // Select an item in the task display widget if there are any tasks
    if (taskDisplayWidget->count() > 0 && !taskDisplayWidget->item(0)->text().startsWith("No tasks")) {
        // If the deleted item was the last one or beyond the current count, select the last item
        int newIndex = (currentIndex >= taskDisplayWidget->count() || currentIndex < 0) ?
                           taskDisplayWidget->count() - 1 : currentIndex;

        // Select and "click" the item at the new index
        taskDisplayWidget->setCurrentRow(newIndex);
        QListWidgetItem* newCurrentItem = taskDisplayWidget->item(newIndex);
        if (newCurrentItem) {
            // Simulate a click by calling our click handler
            onTaskDisplayItemClicked(newCurrentItem);
            // Also emit the itemClicked signal to ensure any connected slots are called
            emit taskDisplayWidget->itemClicked(newCurrentItem);
        }
    }
}

void Operations_TaskLists::AddTaskSimple(QString taskName, bool logTask, QString cmess, QString description)
{
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
        return;
    }

    // Validate completion message
    InputValidation::ValidationResult cmessResult =
        InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);
    if (!cmessResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Completion Message",
                             cmessResult.errorMessage);
        return;
    }

    // Validate description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
        return;
    }

    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select or create a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Ensure the directory exists
    if (!OperationsFiles::ensureDirectoryExists(taskListDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                             "Could not create or access task list directory.");
        return;
    }

    // Get current date-time for creation timestamp
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString creationDate = currentDateTime.toString(Qt::ISODate);

    // Escape any pipe characters and newlines in strings to preserve the format
    QString safeTaskName = taskName.replace("|", "\\|");
    QString safeCmess = cmess.replace("|", "\\|");
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Create the task entry format
    QString taskEntry = QString("Simple|%1|%2|0||%3|%4|DESC:%5")
                            .arg(safeTaskName)
                            .arg(logTask ? "1" : "0")
                            .arg(creationDate)
                            .arg(safeCmess)
                            .arg(safeDescription);

    // Add the task to the task list file
    if (!OperationsFiles::addTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Add Task Failed",
                             "Failed to add the task to the task list file.");
        return;
    }

    // Log to diary if enabled
    if (logTask && m_diaryOps) {
        m_diaryOps->AddTaskLogEntry("Simple", taskName, currentTaskList, "Creation", currentDateTime);
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the current task list
    LoadIndividualTasklist(currentTaskList, taskName);
}

void Operations_TaskLists::AddTaskTimeLimit(QString taskName, bool logTask, int value_TLimit,
                                            QString type_TLimit, QString cmess, QString pmess,
                                            bool reminder, int value_RFreq, QString type_RFreq,
                                            QString description)
{
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
        return;
    }

    // Validate time limit value
    if (value_TLimit <= 0) {
        QMessageBox::warning(m_mainWindow, "Invalid Time Limit",
                             "Time limit must be greater than zero.");
        return;
    }

    // Validate time limit unit
    QStringList validTimeUnits = {"Minutes", "Hours", "Days", "Months", "Years"};
    if (!validTimeUnits.contains(type_TLimit)) {
        QMessageBox::warning(m_mainWindow, "Invalid Time Unit",
                             "The time unit is not valid.");
        return;
    }

    // Validate completion message
    InputValidation::ValidationResult cmessResult =
        InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);
    if (!cmessResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Completion Message",
                             cmessResult.errorMessage);
        return;
    }

    // Validate past-due message
    InputValidation::ValidationResult pmessResult =
        InputValidation::validateInput(pmess, InputValidation::InputType::PlainText);
    if (!pmessResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Past-Due Message",
                             pmessResult.errorMessage);
        return;
    }

    // Validate description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
        return;
    }

    // Validate reminder frequency if enabled
    if (reminder) {
        if (value_RFreq <= 0) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Frequency",
                                 "Reminder frequency must be greater than zero.");
            return;
        }

        if (!validTimeUnits.contains(type_RFreq)) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Time Unit",
                                 "The reminder time unit is not valid.");
            return;
        }

        // Ensure reminder frequency is shorter than time limit
        if (!compareTimeValues(value_RFreq, type_RFreq, value_TLimit, type_TLimit)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Values",
                                 "Reminder frequency must be shorter than Time Limit.");
            return;
        }
    }

    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select or create a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Ensure the directory exists
    if (!OperationsFiles::ensureDirectoryExists(taskListDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                             "Could not create or access task list directory.");
        return;
    }

    // Get current date-time for creation timestamp
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString creationDate = currentDateTime.toString(Qt::ISODate);

    // Create a new task entry - escape any pipe characters
    QString safeTaskName = taskName.replace("|", "\\|");
    QString safeCmess = cmess.replace("|", "\\|");
    QString safePmess = pmess.replace("|", "\\|");
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Format: TimeLimit|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|TimeLimitValue|...
    QString taskEntry = QString("TimeLimit|%1|%2|0||%3|%4|%5|%6|%7|%8|%9|%10|DESC:%11")
                            .arg(safeTaskName)
                            .arg(logTask ? "1" : "0")
                            .arg(creationDate)
                            .arg(value_TLimit)
                            .arg(type_TLimit)
                            .arg(safeCmess)
                            .arg(safePmess)
                            .arg(reminder ? "1" : "0")
                            .arg(reminder ? QString::number(value_RFreq) : "")
                            .arg(reminder ? type_RFreq : "")
                            .arg(safeDescription);

    // Add the task to the tasklist file
    if (!OperationsFiles::addTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Add Task Failed",
                             "Failed to add the task to the task list file.");
        return;
    }

    // Log to diary if enabled
    if (logTask && m_diaryOps) {
        m_diaryOps->AddTaskLogEntry("TimeLimit", taskName, currentTaskList, "Creation", currentDateTime);
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the current task list
    LoadIndividualTasklist(currentTaskList, taskName);

    // Calculate due date and add to queue
    QDateTime dueDateTime = CalculateDueDate(currentDateTime, value_TLimit, type_TLimit);
    AddTaskToDueQueue(currentTaskList, taskName, dueDateTime, pmess);
}

void Operations_TaskLists::AddTaskRecurrent(QString taskName, bool logTask, int value_Freq,
                                            QString type_Freq, QTime startTime, bool timeLimit,
                                            int value_TLimit, QString type_TLimit, bool reminder,
                                            int value_Reminder, QString type_Reminder,
                                            QString description)
{
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
        return;
    }

    // Validate frequency value
    if (value_Freq <= 0) {
        QMessageBox::warning(m_mainWindow, "Invalid Frequency",
                             "Frequency value must be greater than zero.");
        return;
    }

    // Validate frequency unit
    QStringList validTimeUnits = {"Minutes", "Hours", "Days", "Months", "Years"};
    if (!validTimeUnits.contains(type_Freq)) {
        QMessageBox::warning(m_mainWindow, "Invalid Frequency Unit",
                             "The frequency unit is not valid.");
        return;
    }

    // Validate description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
        return;
    }

    // Validate time limit if enabled
    if (timeLimit) {
        if (value_TLimit <= 0) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Limit",
                                 "Time limit must be greater than zero.");
            return;
        }

        if (!validTimeUnits.contains(type_TLimit)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Limit Unit",
                                 "The time limit unit is not valid.");
            return;
        }

        // Ensure time limit is shorter than frequency
        if (!compareTimeValues(value_TLimit, type_TLimit, value_Freq, type_Freq)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Values",
                                 "Time limit must be shorter than Task Frequency.");
            return;
        }
    }

    // Validate reminder if enabled
    if (reminder) {
        if (value_Reminder <= 0) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Value",
                                 "Reminder value must be greater than zero.");
            return;
        }

        if (!validTimeUnits.contains(type_Reminder)) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Unit",
                                 "The reminder unit is not valid.");
            return;
        }

        // Ensure reminder time is shorter than frequency
        if (!compareTimeValues(value_Reminder, type_Reminder, value_Freq, type_Freq)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Values",
                                 "Reminder time must be shorter than Task Frequency.");
            return;
        }
    }

    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select or create a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Ensure the directory exists
    if (!OperationsFiles::ensureDirectoryExists(taskListDir)) {
        QMessageBox::warning(m_mainWindow, "Directory Error",
                             "Could not create or access task list directory.");
        return;
    }

    // Get current date-time for creation timestamp
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString creationDate = currentDateTime.toString(Qt::ISODate);

    // Calculate the next due date based on frequency
    QDateTime nextDueDateTime = currentDateTime;
    if (type_Freq == "Minutes") {
        nextDueDateTime = nextDueDateTime.addSecs(value_Freq * 60);
    } else if (type_Freq == "Hours") {
        nextDueDateTime = nextDueDateTime.addSecs(value_Freq * 3600);
    } else if (type_Freq == "Days") {
        nextDueDateTime = nextDueDateTime.addDays(value_Freq);
    } else if (type_Freq == "Months") {
        nextDueDateTime = nextDueDateTime.addMonths(value_Freq);
    } else if (type_Freq == "Years") {
        nextDueDateTime = nextDueDateTime.addYears(value_Freq);
    }

    // Set the time component of the next due date
    nextDueDateTime.setTime(startTime);
    QString nextDueDate = nextDueDateTime.toString(Qt::ISODate);

    // Create a new task entry - escape any pipe characters
    QString safeTaskName = taskName.replace("|", "\\|");
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Format: Recurrent|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|FreqValue|...
    QString taskEntry = QString("Recurrent|%1|%2|0||%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|DESC:%14")
                            .arg(safeTaskName)
                            .arg(logTask ? "1" : "0")
                            .arg(creationDate)
                            .arg(value_Freq)
                            .arg(type_Freq)
                            .arg(startTime.toString("hh:mm:ss"))
                            .arg(timeLimit ? "1" : "0")
                            .arg(timeLimit ? QString::number(value_TLimit) : "")
                            .arg(timeLimit ? type_TLimit : "")
                            .arg(reminder ? "1" : "0")
                            .arg(reminder ? QString::number(value_Reminder) : "")
                            .arg(reminder ? type_Reminder : "")
                            .arg(nextDueDate)
                            .arg(safeDescription);

    // Add the task to the tasklist file
    if (!OperationsFiles::addTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Add Task Failed",
                             "Failed to add the task to the task list file.");
        return;
    }

    // Log to diary if enabled
    if (logTask && m_diaryOps) {
        m_diaryOps->AddTaskLogEntry("Recurrent", taskName, currentTaskList, "Creation", currentDateTime);

        // Also log the first start notification
        QDateTime nextDueDateTime = CalculateRecurrentDueDate(
            QDateTime::fromString(creationDate, Qt::ISODate), startTime,
            value_Freq, type_Freq, timeLimit, value_TLimit, type_TLimit,
            false, currentDateTime);

        m_diaryOps->AddTaskLogEntry("Recurrent", taskName, currentTaskList, "Start", nextDueDateTime);
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the current task list
    LoadIndividualTasklist(currentTaskList, taskName);
}

void Operations_TaskLists::ModifyTaskSimple(const QString& originalTaskName,
                                            QString taskName, bool logTask, QString cmess,
                                            QString description)
{
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
        return;
    }

    // Validate completion message
    InputValidation::ValidationResult cmessResult =
        InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);
    if (!cmessResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Completion Message",
                             cmessResult.errorMessage);
        return;
    }

    // Validate description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
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

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Find the task in the file
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, originalTaskName, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                             "Could not find the task to modify.");
        return;
    }

    // Parse the existing task entry
    QStringList parts = taskEntry.split('|');
    if (parts.size() < 2 || parts[0] != "Simple") {
        QMessageBox::warning(m_mainWindow, "Invalid Task Format",
                             "The task format is not valid for a Simple task.");
        return;
    }

    // Create a new task entry - escape any pipe characters in strings
    QString safeTaskName = taskName.replace("|", "\\|");
    QString safeCmess = cmess.replace("|", "\\|");
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Preserve the creation date if it exists (should be at index 5)
    QString creationDate = (parts.size() > 5) ? parts[5] : QDateTime::currentDateTime().toString(Qt::ISODate);

    // Format: Simple|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|CongratulationMessage|DESC:Description
    QString newTaskEntry = QString("Simple|%1|%2|%3|%4|%5|%6|DESC:%7")
                               .arg(safeTaskName)
                               .arg(logTask ? "1" : "0")
                               .arg(parts.size() > 3 ? parts[3] : "0") // Preserve completed status
                               .arg(parts.size() > 4 ? parts[4] : "") // Preserve completion date
                               .arg(creationDate)
                               .arg(safeCmess)
                               .arg(safeDescription);

    // Modify the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, originalTaskName, newTaskEntry)) {
        QMessageBox::warning(m_mainWindow, "Modification Failed",
                             "Failed to modify the task in the task list file.");
        return;
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the task list to show the changes
    LoadIndividualTasklist(currentTaskList, taskName);
}

void Operations_TaskLists::ModifyTaskTimeLimit(const QString& originalTaskName,
                                               QString taskName, bool logTask, int value_TLimit,
                                               QString type_TLimit, QString cmess, QString pmess,
                                               bool reminder, int value_RFreq, QString type_RFreq,
                                               QString description)
{
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
        return;
    }

    // Validate time limit value
    if (value_TLimit <= 0) {
        QMessageBox::warning(m_mainWindow, "Invalid Time Limit",
                             "Time limit must be greater than zero.");
        return;
    }

    // Validate time limit unit
    QStringList validTimeUnits = {"Minutes", "Hours", "Days", "Months", "Years"};
    if (!validTimeUnits.contains(type_TLimit)) {
        QMessageBox::warning(m_mainWindow, "Invalid Time Unit",
                             "The time unit is not valid.");
        return;
    }

    // Validate completion message
    InputValidation::ValidationResult cmessResult =
        InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);
    if (!cmessResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Completion Message",
                             cmessResult.errorMessage);
        return;
    }

    // Validate past-due message
    InputValidation::ValidationResult pmessResult =
        InputValidation::validateInput(pmess, InputValidation::InputType::PlainText);
    if (!pmessResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Past-Due Message",
                             pmessResult.errorMessage);
        return;
    }

    // Validate description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
        return;
    }

    // Validate reminder frequency if enabled
    if (reminder) {
        if (value_RFreq <= 0) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Frequency",
                                 "Reminder frequency must be greater than zero.");
            return;
        }

        if (!validTimeUnits.contains(type_RFreq)) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Time Unit",
                                 "The reminder time unit is not valid.");
            return;
        }

        // Ensure reminder frequency is shorter than time limit
        if (!compareTimeValues(value_RFreq, type_RFreq, value_TLimit, type_TLimit)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Values",
                                 "Reminder frequency must be shorter than Time Limit.");
            return;
        }
    }

    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Find the task in the file
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, originalTaskName, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                             "Could not find the task to modify.");
        return;
    }

    // Parse the existing task entry
    QStringList parts = taskEntry.split('|');
    if (parts.size() < 2 || parts[0] != "TimeLimit") {
        QMessageBox::warning(m_mainWindow, "Invalid Task Format",
                             "The task format is not valid for a TimeLimit task.");
        return;
    }

    // Create a new task entry - escape any pipe characters in strings
    QString safeTaskName = taskName.replace("|", "\\|");
    QString safeCmess = cmess.replace("|", "\\|");
    QString safePmess = pmess.replace("|", "\\|");
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Preserve the creation date if it exists (should be at index 5)
    QString creationDate = (parts.size() > 5) ? parts[5] : QDateTime::currentDateTime().toString(Qt::ISODate);

    // Format: TimeLimit|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|TimeLimitValue|...
    QString newTaskEntry = QString("TimeLimit|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|DESC:%13")
                               .arg(safeTaskName)
                               .arg(logTask ? "1" : "0")
                               .arg(parts.size() > 3 ? parts[3] : "0") // Preserve completed status
                               .arg(parts.size() > 4 ? parts[4] : "") // Preserve completion date
                               .arg(creationDate)
                               .arg(value_TLimit)
                               .arg(type_TLimit)
                               .arg(safeCmess)
                               .arg(safePmess)
                               .arg(reminder ? "1" : "0")
                               .arg(reminder ? QString::number(value_RFreq) : "")
                               .arg(reminder ? type_RFreq : "")
                               .arg(safeDescription);

    // Modify the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, originalTaskName, newTaskEntry)) {
        QMessageBox::warning(m_mainWindow, "Modification Failed",
                             "Failed to modify the task in the task list file.");
        return;
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the task list to show the changes
    LoadIndividualTasklist(currentTaskList, taskName);

    // Remove old task from queue and add updated task
    QString oldTaskId = currentTaskList + "::" + originalTaskName;
    RemoveTaskFromDueQueue(oldTaskId);

    // Get the creation date for due date calculation
    QDateTime taskCreationDateTime = QDateTime::fromString(creationDate, Qt::ISODate);

    // Calculate new due date using the preserved creation date
    QDateTime dueDateTime = CalculateDueDate(taskCreationDateTime, value_TLimit, type_TLimit);

    // Add updated task to queue
    AddTaskToDueQueue(currentTaskList, taskName, dueDateTime, pmess);
}

void Operations_TaskLists::ModifyTaskRecurrent(const QString& originalTaskName,
                                               QString taskName, bool logTask, int value_Freq,
                                               QString type_Freq, QTime startTime, bool timeLimit,
                                               int value_TLimit, QString type_TLimit, bool reminder,
                                               int value_Reminder, QString type_Reminder,
                                               QString description)
{
    // Validate task name
    InputValidation::ValidationResult nameResult =
        InputValidation::validateInput(taskName, InputValidation::InputType::PlainText);
    if (!nameResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Name",
                             nameResult.errorMessage);
        return;
    }

    // Validate frequency value
    if (value_Freq <= 0) {
        QMessageBox::warning(m_mainWindow, "Invalid Frequency",
                             "Frequency value must be greater than zero.");
        return;
    }

    // Validate frequency unit
    QStringList validTimeUnits = {"Minutes", "Hours", "Days", "Months", "Years"};
    if (!validTimeUnits.contains(type_Freq)) {
        QMessageBox::warning(m_mainWindow, "Invalid Frequency Unit",
                             "The frequency unit is not valid.");
        return;
    }

    // Validate description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
        return;
    }

    // Validate time limit if enabled
    if (timeLimit) {
        if (value_TLimit <= 0) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Limit",
                                 "Time limit must be greater than zero.");
            return;
        }

        if (!validTimeUnits.contains(type_TLimit)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Limit Unit",
                                 "The time limit unit is not valid.");
            return;
        }

        // Ensure time limit is shorter than frequency
        if (!compareTimeValues(value_TLimit, type_TLimit, value_Freq, type_Freq)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Values",
                                 "Time limit must be shorter than Task Frequency.");
            return;
        }
    }

    // Validate reminder if enabled
    if (reminder) {
        if (value_Reminder <= 0) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Value",
                                 "Reminder value must be greater than zero.");
            return;
        }

        if (!validTimeUnits.contains(type_Reminder)) {
            QMessageBox::warning(m_mainWindow, "Invalid Reminder Unit",
                                 "The reminder unit is not valid.");
            return;
        }

        // Ensure reminder time is shorter than frequency
        if (!compareTimeValues(value_Reminder, type_Reminder, value_Freq, type_Freq)) {
            QMessageBox::warning(m_mainWindow, "Invalid Time Values",
                                 "Reminder time must be shorter than Task Frequency.");
            return;
        }
    }

    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select a task list first.");
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Find the task in the file
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, originalTaskName, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                             "Could not find the task to modify.");
        return;
    }

    // Parse the existing task entry
    QStringList parts = taskEntry.split('|');
    if (parts.size() < 2 || parts[0] != "Recurrent") {
        QMessageBox::warning(m_mainWindow, "Invalid Task Format",
                             "The task format is not valid for a Recurrent task.");
        return;
    }

    // Calculate the next due date based on frequency
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QDateTime nextDueDateTime = currentDateTime;
    if (type_Freq == "Minutes") {
        nextDueDateTime = nextDueDateTime.addSecs(value_Freq * 60);
    } else if (type_Freq == "Hours") {
        nextDueDateTime = nextDueDateTime.addSecs(value_Freq * 3600);
    } else if (type_Freq == "Days") {
        nextDueDateTime = nextDueDateTime.addDays(value_Freq);
    } else if (type_Freq == "Months") {
        nextDueDateTime = nextDueDateTime.addMonths(value_Freq);
    } else if (type_Freq == "Years") {
        nextDueDateTime = nextDueDateTime.addYears(value_Freq);
    }

    // Set the time component of the next due date
    nextDueDateTime.setTime(startTime);
    QString nextDueDate = nextDueDateTime.toString(Qt::ISODate);

    // Create a new task entry - escape any pipe characters in strings
    QString safeTaskName = taskName.replace("|", "\\|");
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Preserve the creation date if it exists (should be at index 5)
    QString creationDate = (parts.size() > 5) ? parts[5] : QDateTime::currentDateTime().toString(Qt::ISODate);

    // Format: Recurrent|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|FreqValue|...
    QString newTaskEntry = QString("Recurrent|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|DESC:%16")
                               .arg(safeTaskName)
                               .arg(logTask ? "1" : "0")
                               .arg(parts.size() > 3 ? parts[3] : "0") // Preserve completed status
                               .arg(parts.size() > 4 ? parts[4] : "") // Preserve completion date
                               .arg(creationDate)
                               .arg(value_Freq)
                               .arg(type_Freq)
                               .arg(startTime.toString("hh:mm:ss"))
                               .arg(timeLimit ? "1" : "0")
                               .arg(timeLimit ? QString::number(value_TLimit) : "")
                               .arg(timeLimit ? type_TLimit : "")
                               .arg(reminder ? "1" : "0")
                               .arg(reminder ? QString::number(value_Reminder) : "")
                               .arg(reminder ? type_Reminder : "")
                               .arg(nextDueDate)
                               .arg(safeDescription);

    // Modify the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, originalTaskName, newTaskEntry)) {
        QMessageBox::warning(m_mainWindow, "Modification Failed",
                             "Failed to modify the task in the task list file.");
        return;
    }

    // Update the appearance of the tasklist
    UpdateTasklistAppearance(currentTaskList);

    // Reload the task list to show the changes
    LoadIndividualTasklist(currentTaskList, taskName);
}

void Operations_TaskLists::SaveTaskDescription()
{
    // Stop the timer if it's running
    if (m_descriptionSaveTimer->isActive()) {
        m_descriptionSaveTimer->stop();
    }

    // Get the task description from the text widget
    QString description = m_mainWindow->ui->plainTextEdit_TaskDesc->toPlainText();

    // Skip saving if the description hasn't changed since last save
    // and we're working with the same task
    if (description == m_lastSavedDescription &&
        m_mainWindow->ui->listWidget_TaskListDisplay->currentItem() &&
        m_mainWindow->ui->listWidget_TaskListDisplay->currentItem()->text() == m_currentTaskName) {
        return;
    }

    // Update the last saved description
    m_lastSavedDescription = description;

    // Validate the description
    InputValidation::ValidationResult descResult =
        InputValidation::validateInput(description, InputValidation::InputType::PlainText);
    if (!descResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Description",
                             descResult.errorMessage);
        return;
    }

    // Get current task list and task
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    if (taskListWidget->currentItem() == nullptr ||
        taskDisplayWidget->currentItem() == nullptr ||
        (taskDisplayWidget->currentItem()->flags() & Qt::ItemIsEnabled) == 0) {
        // No task list selected, no task selected, or disabled placeholder item
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();
    QString currentTaskName = taskDisplayWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Find the task in the file
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, currentTaskName, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                             "Could not find the task to modify.");
        return;
    }

    // Create a safe copy of the description
    QString safeDescription = description;
    safeDescription.replace("|", "\\|"); // Escape pipes
    safeDescription.replace("\n", "\\n"); // Escape newlines
    safeDescription.replace("\r", "\\r"); // Escape carriage returns

    // Parse the task data
    QStringList parts = taskEntry.split('|');
    bool descriptionUpdated = false;

    // Find and update any existing description
    for (int i = 0; i < parts.size(); i++) {
        if (parts[i].startsWith("DESC:")) {
            parts[i] = "DESC:" + safeDescription;
            descriptionUpdated = true;
            break;
        }
    }

    // If no description field found, add one
    if (!descriptionUpdated) {
        parts.append("DESC:" + safeDescription);
    }

    // Create the modified task entry
    QString newTaskEntry = parts.join("|");

    // Update the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, currentTaskName, newTaskEntry)) {
        QMessageBox::warning(m_mainWindow, "Description Save Failed",
                             "Failed to save the task description.");
        return;
    }

    // Update the task data in the list item
    QListWidgetItem* currentItem = taskDisplayWidget->currentItem();
    if (currentItem) {
        currentItem->setData(Qt::UserRole, newTaskEntry);
    }

    m_mainWindow->statusBar()->showMessage("Description saved.", 2000);
}

void Operations_TaskLists::SetTaskStatus(bool checked, QListWidgetItem* item)
{
    // Get the task display widget
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    // If no item was provided, use the currently selected one
    if (!item) {
        item = taskDisplayWidget->currentItem();
    }

    if (!item || (item->flags() & Qt::ItemIsEnabled) == 0) {
        // No valid item selected or it's a placeholder item
        return;
    }

    // Get the task name and data
    QString taskName = item->text();
    QString taskData = item->data(Qt::UserRole).toString();

    // Variables to track congratulatory message
    bool shouldShowCongratMessage = false;
    QString congratMessage;
    QString taskListName;
    QString taskType;
    int congratMessageIndex = -1; // Different index for different task types

    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        QMessageBox::warning(m_mainWindow, "No Task List Selected",
                             "Please select a task list first.");
        return;
    }

    taskListName = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = taskListName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate and check the file
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Could not access task list file.");
        return;
    }

    // Find the task in the file
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName, taskEntry)) {
        QMessageBox::warning(m_mainWindow, "Task Not Found",
                             "Could not find the task to modify.");
        return;
    }

    // Parse the task data
    QStringList parts = taskEntry.split('|');
    if (parts.size() < 2) {
        QMessageBox::warning(m_mainWindow, "Invalid Task Format",
                             "The task format is not valid.");
        return;
    }

    taskType = parts[0];

    // Get current date-time for completion timestamp
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString completionDate = currentDateTime.toString(Qt::ISODate);

    // Extract logTask value from the task data
    bool logTask = false;
    if (parts.size() > 2) {
        logTask = (parts[2] == "1");
    }

    // Different handling based on task type
    if (taskType == "Simple") {
        // Format: Simple|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|CongratulationMessage|DESC:Description
        if (checked) {
            // Check if we should show a congratulatory message
            if (parts.size() > 6) {
                QString cmess = parts[6];
                cmess.replace("\\|", "|"); // Unescape pipes

                // Validate the congratulatory message
                InputValidation::ValidationResult cmessResult =
                    InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);

                if (cmessResult.isValid && cmess != "None") {
                    shouldShowCongratMessage = true;
                    congratMessage = cmess;
                    congratMessageIndex = 6;
                }
            }

            // Set task as completed
            parts[3] = "1"; // Completed status
            parts[4] = completionDate; // Set completion date
        } else {
            // Set task as pending
            parts[3] = "0"; // Pending status
            parts[4] = ""; // Clear completion date
        }

        if (checked && logTask && m_diaryOps) {
            // Get congratulatory message if applicable
            QString congratMessage = "";
            if (parts.size() > 6) {
                QString cmess = parts[6];
                cmess.replace("\\|", "|"); // Unescape pipes

                // Validate the congratulatory message
                InputValidation::ValidationResult cmessResult =
                    InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);

                if (cmessResult.isValid && cmess != "None") {
                    // Determine category based on the message value
                    Constants::CPUNCategory category = Constants::CPUNCategory::None;

                    if (cmess == "Simple") {
                        category = Constants::CPUNCategory::Simple;
                    } else if (cmess == "Advanced") {
                        category = Constants::CPUNCategory::Advanced;
                    } else if (cmess == "Intense") {
                        category = Constants::CPUNCategory::Intense;
                    } else if (cmess == "Extreme") {
                        category = Constants::CPUNCategory::Extreme;
                    } else {
                        // Use the specific message directly
                        congratMessage = cmess;
                    }

                    if (category != Constants::CPUNCategory::None) {
                        congratMessage = Constants::GetCPUNMessage(Constants::CPUNType::Congrat, category);
                    }
                }
            }

            m_diaryOps->AddTaskLogEntry("Simple", taskName, taskListName, "Completion",
                                       QDateTime::currentDateTime(), congratMessage);
        }
    }
    else if (taskType == "TimeLimit") {
        // Format: TimeLimit|TaskName|LogToDiary|Completed|CompletionDate|CreationDate|TimeLimitValue|...
        if (checked) {
            // Check if the task is overdue
            bool isOverdue = false;

            // Calculate Due Date based on creation date and time limit
            QDateTime creationDateTime = QDateTime::fromString(parts[5], Qt::ISODate);
            int timeLimitValue = parts[6].toInt();
            QString timeLimitUnit = parts[7];

            QDateTime dueDateTime = CalculateDueDate(creationDateTime, timeLimitValue, timeLimitUnit);

            // Check if current time is past due date
            isOverdue = currentDateTime > dueDateTime;

            // Check if we should show a congratulatory message (only if not overdue)
            if (!isOverdue && parts.size() > 8) {
                QString cmess = parts[8];
                cmess.replace("\\|", "|"); // Unescape pipes

                // Validate the congratulatory message
                InputValidation::ValidationResult cmessResult =
                    InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);

                if (cmessResult.isValid && cmess != "None") {
                    shouldShowCongratMessage = true;
                    congratMessage = cmess;
                    congratMessageIndex = 8;
                }
            }

            if (isOverdue) {
                parts[3] = "2"; // Late Completion status (using "2" to differentiate)
            } else {
                parts[3] = "1"; // Completed status
            }

            // Set completion date regardless of whether it's overdue or not
            parts[4] = completionDate;

            // Log completion if checked and log to diary is enabled
            if (checked && logTask && m_diaryOps) {
                if (isOverdue) {
                    // Task was completed late
                    QDateTime completionDateTime = QDateTime::fromString(completionDate, Qt::ISODate);
                    qint64 secondsLate = dueDateTime.secsTo(completionDateTime);
                    QString timeDifference = FormatTimeDifference(secondsLate);

                    m_diaryOps->AddTaskLogEntry("TimeLimit", taskName, taskListName, "CompletionLate",
                                               QDateTime(), timeDifference);
                } else {
                    // Task was completed on time
                    QString congratMessage = "";
                    if (parts.size() > 8) {
                        QString cmess = parts[8];
                        cmess.replace("\\|", "|"); // Unescape pipes

                        // Validate the congratulatory message
                        InputValidation::ValidationResult cmessResult =
                            InputValidation::validateInput(cmess, InputValidation::InputType::PlainText);

                        if (cmessResult.isValid && cmess != "None") {
                            // Determine category based on the message value
                            Constants::CPUNCategory category = Constants::CPUNCategory::None;

                            if (cmess == "Simple") {
                                category = Constants::CPUNCategory::Simple;
                            } else if (cmess == "Advanced") {
                                category = Constants::CPUNCategory::Advanced;
                            } else if (cmess == "Intense") {
                                category = Constants::CPUNCategory::Intense;
                            } else if (cmess == "Extreme") {
                                category = Constants::CPUNCategory::Extreme;
                            } else {
                                // Use the specific message directly
                                congratMessage = cmess;
                            }

                            if (category != Constants::CPUNCategory::None) {
                                congratMessage = Constants::GetCPUNMessage(Constants::CPUNType::Congrat, category);
                            }
                        }
                    }

                    m_diaryOps->AddTaskLogEntry("TimeLimit", taskName, taskListName, "CompletionOnTime",
                                               QDateTime(), congratMessage);
                }
            }
        } else {
            // Set task as pending
            parts[3] = "0"; // Pending status
            parts[4] = ""; // Clear completion date
        }
    }
    else if (taskType == "Recurrent") {
        // Recurrent tasks don't have congratulatory messages as per requirements
        if (checked) {
            // Check if the task is overdue
            bool isOverdue = false;

            // Calculate Due Date based on recurrent parameters
            QDateTime creationDateTime = QDateTime::fromString(parts[5], Qt::ISODate);
            int frequencyValue = parts[6].toInt();
            QString frequencyUnit = parts[7];
            QTime startTime = QTime::fromString(parts[8], "hh:mm:ss");

            // Get time limit info if available
            bool hasTimeLimit = (parts.size() > 9 && parts[9] == "1");
            int timeLimitValue = 0;
            QString timeLimitUnit = "";
            if (hasTimeLimit && parts.size() > 11) {
                timeLimitValue = parts[10].toInt();
                timeLimitUnit = parts[11];
            }

            // Calculate Due Date
            QDateTime currentDateTime = QDateTime::currentDateTime();
            QDateTime dueDateTime = CalculateRecurrentDueDate(
                creationDateTime, startTime, frequencyValue, frequencyUnit,
                hasTimeLimit, timeLimitValue, timeLimitUnit, false, currentDateTime);

            // Check if current time is past due date
            isOverdue = currentDateTime > dueDateTime;

            if (isOverdue) {
                parts[3] = "2"; // Late Completion status
            } else {
                parts[3] = "1"; // Completed status
            }

            // Calculate next due date
            QDateTime nextDueDateTime = CalculateRecurrentDueDate(
                creationDateTime, startTime, frequencyValue, frequencyUnit,
                hasTimeLimit, timeLimitValue, timeLimitUnit, true, currentDateTime);

            // Log completion if checked and log to diary is enabled
            if (checked && logTask && m_diaryOps) {
                // Format next occurrence date for the message
                QString nextDueTimeString = FormatDateTime(nextDueDateTime);

                // Check if completed on time or late (only if time limit is set)
                if (hasTimeLimit && currentDateTime > dueDateTime) {
                    // Task was completed late
                    qint64 secondsLate = dueDateTime.secsTo(currentDateTime);
                    QString timeDifference = FormatTimeDifference(secondsLate);

                    // Combine time difference and next occurrence with a separator
                    QString additionalInfo = timeDifference + "|" + nextDueTimeString;

                    m_diaryOps->AddTaskLogEntry("Recurrent", taskName, taskListName, "CompletionLate",
                                               QDateTime(), additionalInfo);
                } else {
                    // Task was completed on time
                    m_diaryOps->AddTaskLogEntry("Recurrent", taskName, taskListName, "CompletionOnTime",
                                               QDateTime(), nextDueTimeString);
                }

                // Log the start notification for the next occurrence
                bool notCompletedLastTime = false; // We'd need to track this separately

                m_diaryOps->AddTaskLogEntry("Recurrent", taskName, taskListName, "Start",
                                           nextDueDateTime,
                                           notCompletedLastTime ? "NotCompletedLastTime" : "");
            }

            // In recurrent tasks, NextDueDate is typically at position 15
            // Ensure we have enough elements in the array
            while (parts.size() < 16) {
                parts.append("");
            }

            // Set the next due date at the correct position
            parts[15] = nextDueDateTime.toString(Qt::ISODate);
        } else {
            // Set task as pending
            parts[3] = "0"; // Pending status
        }
    }

    // Join the parts back together to form the updated task entry
    QString updatedTaskEntry = parts.join("|");

    // Modify the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName, updatedTaskEntry)) {
        QMessageBox::warning(m_mainWindow, "Status Update Failed",
                             "Failed to update the task status.");
        return;
    }

    // Update the current item's appearance based on checked state
    if (checked) {
        // Apply strikethrough and grey out
        QFont font = item->font();
        font.setStrikeOut(true);
        item->setFont(font);
        item->setForeground(QColor(100, 100, 100)); // Grey color
    } else {
        // Restore normal appearance
        QFont font = item->font();
        font.setStrikeOut(false);
        item->setFont(font);
        item->setForeground(QColor(255, 255, 255)); // White color
    }

    // Set the item's checkbox state
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);

    // Update the item's data
    item->setData(Qt::UserRole, updatedTaskEntry);

    if (checked && taskType == "TimeLimit") {
        QString taskId = taskListName + "::" + taskName;
        RemoveTaskFromDueQueue(taskId);
    }

    // Display congratulatory message if appropriate
    if (shouldShowCongratMessage && checked) {
        // Get the appropriate message using the Constants namespace function
        Constants::CPUNCategory category = Constants::CPUNCategory::None;

        // Determine category based on the congratMessage value
        if (congratMessage == "Simple") {
            category = Constants::CPUNCategory::Simple;
        } else if (congratMessage == "Advanced") {
            category = Constants::CPUNCategory::Advanced;
        } else if (congratMessage == "Intense") {
            category = Constants::CPUNCategory::Intense;
        } else if (congratMessage == "Extreme") {
            category = Constants::CPUNCategory::Extreme;
        } else {
            // If we have a specific message text, use it directly
            QMessageBox::information(m_mainWindow, "Congratulations!",
                                    congratMessage);
        }

        if (category != Constants::CPUNCategory::None) {
            QString message = Constants::GetCPUNMessage(Constants::CPUNType::Congrat, category);
            QMessageBox::information(m_mainWindow, "Congratulations!", message);
        }

        // Update the congratulatory message to "None" to prevent repeat messages
        if (UpdateCongratMessageToNone(taskListName, taskName, taskType, congratMessageIndex)) {
            // Update in-memory representation of task data
            QStringList parts = taskData.split('|');
            if (parts.size() > congratMessageIndex) {
                parts[congratMessageIndex] = "None";
                QString updatedTaskData = parts.join('|');

                // Update item's data
                item->setData(Qt::UserRole, updatedTaskData);

                // Update currentTaskData if this is the task being edited
                if (taskName == currentTaskToEdit) {
                    currentTaskData = updatedTaskData;
                }
            }
        }
    }

    // Reorder tasks with completed tasks at the top
    EnforceTaskOrder();

    // Update the task details view
    LoadTaskDetails(taskName);

    if (taskListWidget->currentItem() != nullptr) {
        QString currentTaskList = taskListWidget->currentItem()->text();
        UpdateTasklistAppearance(currentTaskList);
    }
}

//---------------Reminder Feature--------------//
void Operations_TaskLists::CheckTaskReminders() // also checks for overdue tasks
{
    // Get current date-time
    QDateTime currentDateTime = QDateTime::currentDateTime();
    qDebug() << "Checking task reminders at:" << currentDateTime.toString();

    // Get list of all task lists
    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";
    QDir tasksListsDir(tasksListsPath);

    if (!tasksListsDir.exists()) {
        qDebug() << "Task lists directory doesn't exist:" << tasksListsPath;
        return;
    }

    // Get all subdirectories (each represents a task list)
    QStringList taskListDirs = tasksListsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    qDebug() << "Found" << taskListDirs.size() << "task lists to check";

    for (const QString& taskListDirName : taskListDirs) {
        QString taskListPath = tasksListsPath + taskListDirName + "/";
        QString taskListFilePath = taskListPath + taskListDirName + ".txt";

        qDebug() << "Checking task list:" << taskListFilePath;

        // Check if the file exists
        QFileInfo fileInfo(taskListFilePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            qDebug() << "Task list file doesn't exist:" << taskListFilePath;
            continue;
        }

        // Validate the tasklist file
        if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
            qDebug() << "Invalid task list file during reminder check:" << taskListFilePath;
            continue; // Skip to the next tasklist
        }

        // Read the task list file
        QStringList taskLines;
        if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, taskLines)) {
            qDebug() << "Failed to read task list file during reminder check";
            continue;
        }

        int taskCount = 0;
        // Skip the header line (first line) and process each task
        for (int i = 1; i < taskLines.size(); i++) {
            QString line = taskLines[i];

            // Skip empty lines
            if (line.isEmpty()) {
                continue;
            }

            taskCount++;

            // Parse the task data
            QStringList parts = line.split('|');

            // Debug task data
            qDebug() << "Processing task:" << taskCount << "with" << parts.size() << "fields";

            // Ensure we have the minimum required fields
            if (parts.size() < 2) {
                qDebug() << "Task has insufficient fields, skipping";
                continue;
            }

            QString taskType = parts[0];
            QString taskName = parts[1];
            taskName.replace("\\|", "|"); // Unescape pipes

            qDebug() << "Task type:" << taskType << "Task name:" << taskName;

            // Create a unique ID for this task
            QString taskId = taskListDirName + "::" + taskName;

            // Skip completed tasks
            bool isCompleted = (parts.size() > 3 && (parts[3] == "1" || parts[3] == "2"));
            if (isCompleted) {
                qDebug() << "Task is completed, skipping";
                continue;
            }

            // Process according to task type
            if (taskType == "TimeLimit") {
                // Create a unique ID for this task
                QString taskId = taskListDirName + "::" + taskName;

                // Skip if this task is already handled by the precise timer system
                // Use a temporary copy of the queue to check
                std::priority_queue<TaskDueInfo> tempQueue = m_dueTasksQueue;
                bool inQueue = false;

                while (!tempQueue.empty()) {
                    if (tempQueue.top().taskId == taskId) {
                        inQueue = true;
                        break;
                    }
                    tempQueue.pop();
                }

                if (inQueue) {
                    qDebug() << "Task already in precise queue, skipping in reminder check:" << taskName;
                    continue;
                }

                // Process TimeLimit task
                qDebug() << "Processing TimeLimit task with" << parts.size() << "fields";

                // Print all task data for debugging
                for (int i = 0; i < parts.size(); i++) {
                    qDebug() << "Field" << i << ":" << parts[i];
                }

                // TimeLimit task should have reminder flag at index 10
                bool reminderEnabled = false;
                if (parts.size() > 10) {
                    reminderEnabled = (parts[10] == "1");
                    qDebug() << "Reminder enabled:" << reminderEnabled;
                }

                // Get task details for due date calculation
                QDateTime creationDateTime = QDateTime::fromString(parts[5], Qt::ISODate);
                int timeLimitValue = parts[6].toInt();
                QString timeLimitUnit = parts[7];

                // Calculate due date
                QDateTime dueDateTime = CalculateDueDate(creationDateTime, timeLimitValue, timeLimitUnit);

                // Check if task is overdue
                bool isOverdue = currentDateTime > dueDateTime;

                // Skip overdue notification for TimeLimit tasks - This is now handled by the more precise timer system
                // Instead, just add it to the due queue if not already there and not overdue
                if (!isOverdue) {
                    // If not in queue and not overdue, add to due tasks queue with appropriate message
                    QString punitiveMessage = "None";
                    if (parts.size() > 9) {
                        punitiveMessage = parts[9];
                    }

                    // Add to queue (will be ignored if already exists)
                    AddTaskToDueQueue(taskListDirName, taskName, dueDateTime, punitiveMessage);
                }

                // Only process reminders if the task is not overdue
                if (!isOverdue && reminderEnabled) {
                    // Get reminder frequency information
                    int reminderFrequency = parts[11].toInt();
                    QString reminderUnit = parts[12];

                    // Pass creationDateTime to the function
                    if (ShouldShowTimeLimitReminder(dueDateTime, creationDateTime, reminderFrequency, reminderUnit, taskId)) {
                        qDebug() << "REMINDER DUE for time limit task:" << taskName;

                        // Calculate time remaining until due
                        QString timeRemaining = CalculateTimeLeft(currentDateTime, dueDateTime);

                        // Format due time
                        QString dueTime = FormatDateTime(dueDateTime);

                        // Show notification if tray icon exists
                        if (m_mainWindow && m_mainWindow->trayIcon && m_mainWindow->setting_TLists_Notif) {
                            m_mainWindow->trayIcon->showMessage(
                                "Time Limit Reminder",
                                taskName + " is due in " + timeRemaining + ". It needs to be completed by " + dueTime,
                                QSystemTrayIcon::Warning,
                                5000
                                );
                            qDebug() << "Notification sent for time limit task:" << taskName;

                            // Record the time we showed this notification
                            m_lastNotifiedTasks[taskId] = QDateTime::currentDateTime();
                        } else {
                            qDebug() << "Cannot show notification - tray icon is null";
                        }
                    } else {
                        qDebug() << "Not time to show reminder for time limit task:" << taskName;
                    }
                }
            }
            else if (taskType == "Recurrent") {
                // Process Recurrent task
                qDebug() << "Processing Recurrent task with" << parts.size() << "fields";

                // Print all task data for debugging
                for (int i = 0; i < parts.size(); i++) {
                    qDebug() << "Field" << i << ":" << parts[i];
                }

                // Recurrent task should have reminder flag at index 12
                bool reminderEnabled = false;
                if (parts.size() > 12) {
                    reminderEnabled = (parts[12] == "1");
                    qDebug() << "Reminder enabled:" << reminderEnabled;
                }

                if (reminderEnabled) {
                    // Get creation date and calculate next due date
                    QDateTime creationDateTime = QDateTime::fromString(parts[5], Qt::ISODate);
                    int frequencyValue = parts[6].toInt();
                    QString frequencyUnit = parts[7];
                    QTime startTime = QTime::fromString(parts[8], "hh:mm:ss");

                    qDebug() << "Creation date:" << creationDateTime.toString();
                    qDebug() << "Frequency:" << frequencyValue << frequencyUnit;
                    qDebug() << "Start time:" << startTime.toString();

                    // Get time limit info if available
                    bool hasTimeLimit = (parts.size() > 9 && parts[9] == "1");
                    int timeLimitValue = 0;
                    QString timeLimitUnit = "";
                    if (hasTimeLimit && parts.size() > 11) {
                        timeLimitValue = parts[10].toInt();
                        timeLimitUnit = parts[11];
                        qDebug() << "Time limit:" << timeLimitValue << timeLimitUnit;
                    }

                    // Calculate due date
                    QDateTime dueDateTime = CalculateRecurrentDueDate(
                        creationDateTime, startTime, frequencyValue, frequencyUnit,
                        hasTimeLimit, timeLimitValue, timeLimitUnit, false, currentDateTime);
                    qDebug() << "Due date:" << dueDateTime.toString();

                    // Check if the task is overdue
                    bool isOverdue = currentDateTime > dueDateTime;
                    if (isOverdue) {
                        qDebug() << "Recurrent task is overdue, skipping reminder";
                        continue;
                    }

                    // Get reminder information
                    int reminderValue = 0;
                    QString reminderUnit = "";

                    if (parts.size() > 14) {
                        reminderValue = parts[13].toInt();
                        reminderUnit = parts[14];
                        qDebug() << "Reminder:" << reminderValue << reminderUnit << "before due date";
                    } else {
                        qDebug() << "Missing reminder value/unit fields";
                        continue;
                    }

                    // For recurrent tasks, the reminder is X time before due date (only once)
                    if (ShouldShowRecurrentReminder(dueDateTime, reminderValue, reminderUnit, currentDateTime, taskId)) {
                        qDebug() << "REMINDER DUE for recurrent task:" << taskName;

                        // Calculate time remaining until due
                        QString timeRemaining = CalculateTimeLeft(currentDateTime, dueDateTime);

                        // Format due time
                        QString dueTime = FormatDateTime(dueDateTime);

                        // Show notification if tray icon exists
                        if (m_mainWindow && m_mainWindow->trayIcon && m_mainWindow->setting_TLists_Notif) {
                            m_mainWindow->trayIcon->showMessage(
                                "Recurrent Task Reminder",
                                taskName + " is due in " + timeRemaining + ". It needs to be completed by " + dueTime,
                                QSystemTrayIcon::Warning,
                                5000
                                );
                            qDebug() << "Notification sent for recurrent task:" << taskName;

                            // Record the time we showed this notification with the due date to avoid repeats
                            QString cycleId = taskId + "::" + dueDateTime.toString(Qt::ISODate);
                            m_lastNotifiedTasks[cycleId] = QDateTime::currentDateTime();
                        } else {
                            qDebug() << "Cannot show notification - tray icon is null";
                        }
                    } else {
                        qDebug() << "Not time to show reminder for recurrent task:" << taskName;
                    }
                }
            }
        }

        qDebug() << "Processed" << taskCount << "tasks in task list";
    }

    // Clean up old notifications (older than 24 hours)
    QDateTime cleanupThreshold = currentDateTime.addDays(-1);
    QMutableMapIterator<QString, QDateTime> it(m_lastNotifiedTasks);
    while (it.hasNext()) {
        it.next();
        if (it.value() < cleanupThreshold) {
            it.remove();
        }
    }

    // Clear overdue notifications for completed tasks (check weekly)
    static int cleanupCounter = 0;
    cleanupCounter++;

    if (cleanupCounter >= 10080) { // Weekly cleanup (60 min * 24 hours * 7 days = 10080 minutes)
        cleanupCounter = 0;
        m_overdueNotifiedTasks.clear();
        qDebug() << "Cleared overdue notification history during weekly cleanup";
    }
}

bool Operations_TaskLists::ShouldShowTimeLimitReminder(const QDateTime& dueDateTime,
                                                       const QDateTime& creationDateTime,
                                                       int reminderFrequency,
                                                       const QString& reminderUnit,
                                                       const QString& taskId)
{
    QDateTime currentDateTime = QDateTime::currentDateTime();

    // Don't show reminders for past-due tasks
    if (currentDateTime > dueDateTime) {
        qDebug() << "Task is past due, not showing reminder";
        return false;
    }

    // If this is the first check for this task, calculate when reminders should be shown
    // based on the creation date and frequency
    if (!m_lastNotifiedTasks.contains(taskId)) {
        qDebug() << "First check for this task";

        // Calculate time intervals in seconds based on the reminder unit and frequency
        qint64 frequencySeconds = 0;
        if (reminderUnit == "Minutes") {
            frequencySeconds = reminderFrequency * 60;
        } else if (reminderUnit == "Hours") {
            frequencySeconds = reminderFrequency * 3600;
        } else if (reminderUnit == "Days") {
            frequencySeconds = reminderFrequency * 86400;
        } else if (reminderUnit == "Months") {
            // Approximate a month as 30 days
            frequencySeconds = reminderFrequency * 86400 * 30;
        } else if (reminderUnit == "Years") {
            // Approximate a year as 365 days
            frequencySeconds = reminderFrequency * 86400 * 365;
        }

        if (frequencySeconds <= 0) {
            qDebug() << "Invalid reminder frequency";
            return false;
        }

        // Calculate seconds since task creation
        qint64 secondsSinceCreation = creationDateTime.secsTo(currentDateTime);
        qDebug() << "Seconds since task creation:" << secondsSinceCreation;

        // Calculate how many reminder periods have passed
        qint64 periodsPassed = secondsSinceCreation / frequencySeconds;
        qDebug() << "Reminder periods passed:" << periodsPassed;

        // Calculate when the next reminder should be shown
        QDateTime nextReminderTime;

        if (periodsPassed == 0) {
            // No periods have passed, show the first reminder now
            qDebug() << "First period, showing reminder now";
            return true;
        } else {
            // Calculate the next reminder time based on the number of periods passed
            // This ensures we align with the proper reminder schedule

            // More accurate calculation of next reminder time, handling different units properly
            nextReminderTime = creationDateTime;

            if (reminderUnit == "Minutes") {
                nextReminderTime = nextReminderTime.addSecs((periodsPassed + 1) * reminderFrequency * 60);
            } else if (reminderUnit == "Hours") {
                nextReminderTime = nextReminderTime.addSecs((periodsPassed + 1) * reminderFrequency * 3600);
            } else if (reminderUnit == "Days") {
                nextReminderTime = nextReminderTime.addDays((periodsPassed + 1) * reminderFrequency);
            } else if (reminderUnit == "Months") {
                nextReminderTime = nextReminderTime.addMonths((periodsPassed + 1) * reminderFrequency);
            } else if (reminderUnit == "Years") {
                nextReminderTime = nextReminderTime.addYears((periodsPassed + 1) * reminderFrequency);
            }

            qDebug() << "Next calculated reminder time:" << nextReminderTime.toString();

            // Check if we're within 1 minute of the calculated next reminder time
            qint64 secondsDiff = qAbs(currentDateTime.secsTo(nextReminderTime));
            if (secondsDiff < 60) {
                qDebug() << "Within threshold of next reminder time";
                return true;
            } else {
                // Calculate the previous reminder time
                QDateTime previousReminderTime = creationDateTime;

                if (reminderUnit == "Minutes") {
                    previousReminderTime = previousReminderTime.addSecs(periodsPassed * reminderFrequency * 60);
                } else if (reminderUnit == "Hours") {
                    previousReminderTime = previousReminderTime.addSecs(periodsPassed * reminderFrequency * 3600);
                } else if (reminderUnit == "Days") {
                    previousReminderTime = previousReminderTime.addDays(periodsPassed * reminderFrequency);
                } else if (reminderUnit == "Months") {
                    previousReminderTime = previousReminderTime.addMonths(periodsPassed * reminderFrequency);
                } else if (reminderUnit == "Years") {
                    previousReminderTime = previousReminderTime.addYears(periodsPassed * reminderFrequency);
                }

                // If the most recent reminder time was within the last minute, show it
                qint64 secondsSincePrevious = previousReminderTime.secsTo(currentDateTime);
                if (secondsSincePrevious >= 0 && secondsSincePrevious < 60) {
                    qDebug() << "Within threshold of previous reminder time";
                    return true;
                }

                qDebug() << "Not time for a reminder yet";
                return false;
            }
        }
    }

    // For subsequent checks, use the last notification time
    QDateTime lastNotificationTime = m_lastNotifiedTasks[taskId];
    qDebug() << "Last notification was at:" << lastNotificationTime.toString();

    // Calculate when the next notification should be
    QDateTime nextNotificationTime = lastNotificationTime;

    if (reminderUnit == "Minutes") {
        nextNotificationTime = nextNotificationTime.addSecs(reminderFrequency * 60);
    } else if (reminderUnit == "Hours") {
        nextNotificationTime = nextNotificationTime.addSecs(reminderFrequency * 3600);
    } else if (reminderUnit == "Days") {
        nextNotificationTime = nextNotificationTime.addDays(reminderFrequency);
    } else if (reminderUnit == "Months") {
        nextNotificationTime = nextNotificationTime.addMonths(reminderFrequency);
    } else if (reminderUnit == "Years") {
        nextNotificationTime = nextNotificationTime.addYears(reminderFrequency);
    }

    qDebug() << "Next notification scheduled for:" << nextNotificationTime.toString();

    // Show a notification if we've reached or passed the next notification time
    // Use a 1-minute window to avoid missing notifications
    qint64 secondsDiff = currentDateTime.secsTo(nextNotificationTime);
    return (secondsDiff <= 0 && secondsDiff > -60);
}

bool Operations_TaskLists::ShouldShowRecurrentReminder(const QDateTime& dueDateTime, int reminderValue,
                                                       const QString& reminderUnit, const QDateTime& currentDateTime,
                                                       const QString& taskId)
{
    if (!dueDateTime.isValid() || reminderValue <= 0) {
        qDebug() << "Invalid due date or reminder value";
        return false;
    }

    // Calculate when the reminder should trigger (reminder time before due date)
    QDateTime reminderDateTime = dueDateTime;

    // Subtract the reminder time from the due date
    if (reminderUnit == "Minutes") {
        reminderDateTime = reminderDateTime.addSecs(-reminderValue * 60);
    } else if (reminderUnit == "Hours") {
        reminderDateTime = reminderDateTime.addSecs(-reminderValue * 3600);
    } else if (reminderUnit == "Days") {
        reminderDateTime = reminderDateTime.addDays(-reminderValue);
    } else if (reminderUnit == "Months") {
        reminderDateTime = reminderDateTime.addMonths(-reminderValue);
    } else if (reminderUnit == "Years") {
        reminderDateTime = reminderDateTime.addYears(-reminderValue);
    }

    qDebug() << "Current time:" << currentDateTime.toString();
    qDebug() << "Reminder time:" << reminderDateTime.toString();
    qDebug() << "Due time:" << dueDateTime.toString();

    // Task is past due, don't show a reminder
    if (currentDateTime > dueDateTime) {
        qDebug() << "Task is past due";
        return false;
    }

    // For recurrent tasks, we need to track if we've already shown a reminder for this cycle
    // Create a unique ID using the task ID and the due date
    QString cycleId = taskId + "::" + dueDateTime.toString(Qt::ISODate);

    // If we've already shown a notification for this cycle, don't show another
    if (m_lastNotifiedTasks.contains(cycleId)) {
        qDebug() << "Already shown a reminder for this cycle";
        return false;
    }

    // Check if we're within the reminder window
    // Current time should be at or after the reminder time, but before the due time
    return (currentDateTime >= reminderDateTime && currentDateTime < dueDateTime);
}


//------------CPUN Message Feature ---------//
bool Operations_TaskLists::UpdateCongratMessageToNone(const QString& taskListName,
                                                   const QString& taskName,
                                                   const QString& taskType,
                                                   int congratMessageIndex)
{
    // Skip if invalid index
    if (congratMessageIndex < 0) {
        return false;
    }

    // Sanitize the task list name for file operations
    QString sanitizedName = taskListName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        qWarning() << "Invalid file path when updating congrat message";
        return false;
    }

    // Find the task entry
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName, taskEntry)) {
        qWarning() << "Task not found when updating congrat message";
        return false;
    }

    // Parse the task data
    QStringList parts = taskEntry.split('|');

    // Check that the task type matches
    if (parts.isEmpty() || parts[0] != taskType) {
        qWarning() << "Task type mismatch when updating congrat message";
        return false;
    }

    // Make sure the array is large enough
    while (parts.size() <= congratMessageIndex) {
        parts.append("");
    }

    // Set congratulatory message to "None"
    parts[congratMessageIndex] = "None";

    // Create the updated task entry
    QString updatedTaskEntry = parts.join("|");

    // Modify the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName, updatedTaskEntry)) {
        qWarning() << "Failed to modify task when updating congrat message";
        return false;
    }

    return true;
}

bool Operations_TaskLists::UpdatePunitiveMessageToNone(const QString& taskListName,
                                                     const QString& taskName,
                                                     const QString& taskType,
                                                     int punitiveMessageIndex)
{
    // Skip if invalid index
    if (punitiveMessageIndex < 0) {
        return false;
    }

    // Sanitize the task list name for file operations
    QString sanitizedName = taskListName;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        qWarning() << "Invalid file path when updating punitive message";
        return false;
    }

    // Find the task entry
    QString taskEntry;
    if (!OperationsFiles::findTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName, taskEntry)) {
        qWarning() << "Task not found when updating punitive message";
        return false;
    }

    // Parse the task data
    QStringList parts = taskEntry.split('|');

    // Check that the task type matches
    if (parts.isEmpty() || parts[0] != taskType) {
        qWarning() << "Task type mismatch when updating punitive message";
        return false;
    }

    // Make sure the array is large enough
    while (parts.size() <= punitiveMessageIndex) {
        parts.append("");
    }

    // Set punitive message to "None"
    parts[punitiveMessageIndex] = "None";

    // Create the updated task entry
    QString updatedTaskEntry = parts.join("|");

    // Modify the task entry
    if (!OperationsFiles::modifyTaskEntry(taskListFilePath, m_mainWindow->user_Key, taskName, updatedTaskEntry)) {
        qWarning() << "Failed to modify task when updating punitive message";
        return false;
    }

    return true;
}

void Operations_TaskLists::InitializeDueTasksQueue()
{
    // Clear the queue
    while (!m_dueTasksQueue.empty()) {
        m_dueTasksQueue.pop();
    }

    // Scan all task lists and add due tasks to the queue
    QString tasksListsPath = "Data/" + m_mainWindow->user_Username + "/Tasklists/";
    QDir tasksListsDir(tasksListsPath);

    if (!tasksListsDir.exists()) {
        qDebug() << "Task lists directory doesn't exist:" << tasksListsPath;
        return;
    }

    // Get all task list directories
    QStringList taskListDirs = tasksListsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    qDebug() << "Found" << taskListDirs.size() << "task lists to scan for due tasks";

    // Scan each task list
    for (const QString& taskListDirName : taskListDirs) {
        QString taskListPath = tasksListsPath + taskListDirName + "/";
        QString taskListFilePath = taskListPath + taskListDirName + ".txt";

        // Check if file exists
        QFileInfo fileInfo(taskListFilePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }

        // Validate the tasklist file for security
        if (!InputValidation::validateTasklistFile(taskListFilePath, m_mainWindow->user_Key)) {
            qDebug() << "Invalid task list file during due tasks queue initialization:" << taskListFilePath;
            continue; // Skip to the next tasklist
        }
        // Decrypt and read the file
        QString tempPath = taskListFilePath + ".temp";
        bool decrypted = CryptoUtils::Encryption_DecryptFile(
            m_mainWindow->user_Key, taskListFilePath, tempPath);

        if (!decrypted) {
            continue;
        }

        QFile file(tempPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QFile::remove(tempPath);
            continue;
        }

        QTextStream in(&file);
        in.readLine(); // Skip header line

        // Process each task
        while (!in.atEnd()) {
            QString line = in.readLine();

            // Skip empty lines
            if (line.isEmpty()) {
                continue;
            }

            // Parse the task data
            QStringList parts = line.split('|');

            // Ensure minimum required fields
            if (parts.size() < 2) {
                continue;
            }

            QString taskType = parts[0];
            QString taskName = parts[1];
            taskName.replace("\\|", "|"); // Unescape pipes

            // Skip completed tasks
            bool isCompleted = (parts.size() > 3 && (parts[3] == "1" || parts[3] == "2"));
            if (isCompleted) {
                continue;
            }

            // Process time limit tasks
            if (taskType == "TimeLimit") {
                // Get task details for due date calculation
                QDateTime creationDateTime = QDateTime::fromString(parts[5], Qt::ISODate);
                int timeLimitValue = parts[6].toInt();
                QString timeLimitUnit = parts[7];

                // Calculate due date
                QDateTime dueDateTime = CalculateDueDate(creationDateTime, timeLimitValue, timeLimitUnit);

                // Get punitive message type
                QString punitiveType = "None";
                if (parts.size() > 9) {
                    punitiveType = parts[9];
                }

                // Check if due date is in the future and add to queue
                QDateTime currentDateTime = QDateTime::currentDateTime();
                if (dueDateTime > currentDateTime) {
                    AddTaskToDueQueue(taskListDirName, taskName, dueDateTime, punitiveType);
                }
            }
        }

        file.close();
        QFile::remove(tempPath);
    }

    // Schedule the next due task
    ScheduleNextDueTask();
}

// Add a task to the due tasks queue
void Operations_TaskLists::AddTaskToDueQueue(const QString& taskListName, const QString& taskName,
                                            const QDateTime& dueDateTime, const QString& punitiveType)
{
    // Create unique task ID
    QString taskId = taskListName + "::" + taskName;

    // Check if task is already overdue notified
    if (m_overdueNotifiedTasks.value(taskId, false)) {
        return;
    }

    // Create task info structure
    TaskDueInfo taskInfo;
    taskInfo.taskId = taskId;
    taskInfo.taskName = taskName;
    taskInfo.taskListName = taskListName;
    taskInfo.dueDateTime = dueDateTime;
    taskInfo.punitiveType = punitiveType;

    // Add to priority queue
    m_dueTasksQueue.push(taskInfo);

    qDebug() << "Added task to due queue:" << taskName << "due at" << dueDateTime.toString();

    // Reschedule timer if this is now the earliest due task
    if (m_dueTasksQueue.top().taskId == taskId) {
        ScheduleNextDueTask();
    }
}

// Remove a task from the due queue (when completed or deleted)
void Operations_TaskLists::RemoveTaskFromDueQueue(const QString& taskId)
{
    // Since we can't easily remove from a std::priority_queue, we'll rebuild it
    std::priority_queue<TaskDueInfo> newQueue;

    while (!m_dueTasksQueue.empty()) {
        TaskDueInfo task = m_dueTasksQueue.top();
        m_dueTasksQueue.pop();

        if (task.taskId != taskId) {
            newQueue.push(task);
        }
    }

    // Replace the queue
    m_dueTasksQueue = newQueue;

    // Reschedule timer for the next task
    ScheduleNextDueTask();
}

// Schedule the timer for the next due task
void Operations_TaskLists::ScheduleNextDueTask()
{
    // Stop the current timer if it's running
    if (m_preciseTaskTimer->isActive()) {
        m_preciseTaskTimer->stop();
    }

    // If the queue is empty, nothing to schedule
    if (m_dueTasksQueue.empty()) {
        qDebug() << "No tasks in due queue, timer not scheduled";
        return;
    }

    // Get the next due task
    TaskDueInfo nextTask = m_dueTasksQueue.top();

    // Calculate milliseconds until the due time
    QDateTime currentDateTime = QDateTime::currentDateTime();
    qint64 msUntilDue = currentDateTime.msecsTo(nextTask.dueDateTime);

    // If the task is already overdue, process it immediately
    if (msUntilDue <= 0) {
        ProcessDueTask(nextTask);
        return;
    }

    // Schedule the timer
    qDebug() << "Scheduling timer for task:" << nextTask.taskName << "in" << msUntilDue << "ms";
    m_preciseTaskTimer->setSingleShot(true);
    m_preciseTaskTimer->start(msUntilDue);
}

// Process a task that has become due
void Operations_TaskLists::ProcessDueTask(const TaskDueInfo& taskInfo)
{
    qDebug() << "***** STARTING ProcessDueTask *****";
    qDebug() << "TaskId:" << taskInfo.taskId;
    qDebug() << "TaskName:" << taskInfo.taskName;
    qDebug() << "TaskListName:" << taskInfo.taskListName;
    qDebug() << "DueDateTime:" << taskInfo.dueDateTime.toString(Qt::ISODate);
    qDebug() << "PunitiveType:" << taskInfo.punitiveType;

    // Make local copies of all needed data BEFORE popping from queue
    QString localTaskId = taskInfo.taskId;
    QString localTaskName = taskInfo.taskName;
    QString localTaskListName = taskInfo.taskListName;
    QDateTime localDueDateTime = taskInfo.dueDateTime;
    QString localPunitiveType = taskInfo.punitiveType;

    qDebug() << "Made local copies of task data";

    // Now remove task from queue
    qDebug() << "Before popping from queue";
    m_dueTasksQueue.pop();
    qDebug() << "After popping from queue";

    // Mark task as notified for overdue
    qDebug() << "Before marking task as notified";
    m_overdueNotifiedTasks[localTaskId] = true;
    qDebug() << "After marking task as notified";

    // Default message
    qDebug() << "Creating overdueMessage";
    QString overdueMessage = "Failed to complete " + localTaskName + " in time";
    qDebug() << "OverdueMessage created:" << overdueMessage;

    // Get punitive message if set
    qDebug() << "Creating punitiveMessage";
    QString punitiveMessage = "";
    if (localPunitiveType != "None") {
        qDebug() << "PunitiveType is not None";
        // Determine category based on the punitiveMessage value
        Constants::CPUNCategory category = Constants::CPUNCategory::None;

        if (localPunitiveType == "Simple") {
            category = Constants::CPUNCategory::Simple;
        } else if (localPunitiveType == "Advanced") {
            category = Constants::CPUNCategory::Advanced;
        } else if (localPunitiveType == "Intense") {
            category = Constants::CPUNCategory::Intense;
        } else if (localPunitiveType == "Extreme") {
            category = Constants::CPUNCategory::Extreme;
        } else {
            // If we have a specific message text, use it directly
            punitiveMessage = localPunitiveType;
        }

        if (category != Constants::CPUNCategory::None) {
            qDebug() << "Getting CPUN message for category:" << static_cast<int>(category);
            punitiveMessage = Constants::GetCPUNMessage(Constants::CPUNType::Punish, category);
        }
        qDebug() << "PunitiveMessage created:" << punitiveMessage;

        // Disable the punitive message to prevent it from showing again
        qDebug() << "Before updating punitive message to None";
        try {
            UpdatePunitiveMessageToNone(localTaskListName, localTaskName, "TimeLimit", 9);
        } catch (const std::exception& e) {
            qDebug() << "Exception in UpdatePunitiveMessageToNone:" << e.what();
        } catch (...) {
            qDebug() << "Unknown exception in UpdatePunitiveMessageToNone";
        }
        qDebug() << "After updating punitive message to None";
    }

    // Combine messages if punitive message is available
    qDebug() << "Before combining messages";
    if (!punitiveMessage.isEmpty()) {
        overdueMessage += ". " + punitiveMessage;
    }
    qDebug() << "Final overdueMessage:" << overdueMessage;

    // Show notification if tray icon exists
    qDebug() << "Before showing notification";
    if (m_mainWindow && m_mainWindow->trayIcon && m_mainWindow->setting_TLists_Notif) {
        m_mainWindow->trayIcon->showMessage(
            "Task Overdue",
            overdueMessage,
            QSystemTrayIcon::Critical,
            5000
        );
        qDebug() << "Notification sent for task:" << localTaskName;
    } else {
        qDebug() << "Cannot show notification - tray icon is null";
    }

    qDebug() << "Preparing to check if Log to Diary is enabled";

    // Check if Log to Diary is enabled for this task and log to diary if it is
    bool logTask = false;
    bool taskFileFound = false;

    try {
        // Sanitize the task list name for file operations
        qDebug() << "Before sanitizing taskListName";
        QString sanitizedName = localTaskListName;
        sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        qDebug() << "Sanitized name:" << sanitizedName;

        QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
        qDebug() << "TaskListDir:" << taskListDir;

        // Construct file paths with proper sanitization
        QString taskListFilePath = taskListDir + sanitizedName + ".txt";
        qDebug() << "TaskListFilePath:" << taskListFilePath;

        // Check if file exists
        qDebug() << "Before checking if file exists";
        QFileInfo fileInfo(taskListFilePath);
        taskFileFound = fileInfo.exists() && fileInfo.isFile();
        qDebug() << "File exists:" << taskFileFound;

        if (taskFileFound) {
            // Validate the tasklist file for security
            if (!InputValidation::validateTasklistFile(taskListFilePath, m_mainWindow->user_Key)) {
                qDebug() << "Invalid task list file when processing due task:" << taskListFilePath;
                return; // Skip this task
            }
            // Temporary path for decrypted file with unique identifier
            qDebug() << "Creating unique temp path";
            QString uniqueId = QString::number(QDateTime::currentDateTime().toMSecsSinceEpoch());
            QString tempPath = taskListFilePath + ".temp." + uniqueId;
            qDebug() << "TempPath:" << tempPath;

            // Decrypt the file to check Log to Diary setting
            qDebug() << "Before decrypting file";
            bool decrypted = false;
            try {
                decrypted = CryptoUtils::Encryption_DecryptFile(
                    m_mainWindow->user_Key, taskListFilePath, tempPath);
            } catch (const std::exception& e) {
                qDebug() << "Exception during decryption:" << e.what();
            } catch (...) {
                qDebug() << "Unknown exception during decryption";
            }
            qDebug() << "File decrypted:" << decrypted;

            if (decrypted) {
                // Open the file and find the task
                qDebug() << "Before opening temp file";
                QFile file(tempPath);
                bool fileOpened = file.open(QIODevice::ReadOnly | QIODevice::Text);
                qDebug() << "File opened:" << fileOpened;

                if (fileOpened) {
                    QTextStream in(&file);
                    in.readLine(); // Skip header line
                    qDebug() << "Skipped header line";

                    bool taskFound = false;
                    qDebug() << "Searching for task in file";

                    while (!in.atEnd()) {
                        QString line = in.readLine();

                        // Skip empty lines
                        if (line.isEmpty()) {
                            continue;
                        }

                        // Parse the task data (pipe-separated values)
                        QStringList parts = line.split('|');

                        // Basic check - ensure we have at least task type and name
                        if (parts.size() < 2) {
                            continue;
                        }

                        QString taskType = parts[0];
                        QString currentTaskName = parts[1];
                        currentTaskName.replace("\\|", "|"); // Unescape pipes

                        qDebug() << "Checking task:" << currentTaskName << "Type:" << taskType;

                        // If this is the task we're looking for
                        if (currentTaskName == localTaskName && taskType == "TimeLimit") {
                            taskFound = true;
                            qDebug() << "Task found in file";

                            // Check if Log to Diary is enabled
                            if (parts.size() > 2) {
                                logTask = (parts[2] == "1");
                                qDebug() << "Log to Diary enabled:" << logTask;
                            }

                            break;
                        }
                    }

                    file.close();
                    qDebug() << "Temp file closed";

                    // Log to diary if enabled and task found
                    if (taskFound && logTask) {
                        qDebug() << "*** CRITICAL POINT: Before logging to diary ***";

                        // Make absolutely sure all parameters are valid
                        QString safeTaskName = localTaskName;
                        QString safeMsg = punitiveMessage;
                        QDateTime safeDueTime = localDueDateTime.isValid() ?
                                               localDueDateTime : QDateTime::currentDateTime();

                        qDebug() << "SafeTaskName:" << safeTaskName;
                        qDebug() << "SafeMsg:" << safeMsg;
                        qDebug() << "SafeDueTime:" << safeDueTime.toString(Qt::ISODate);

                        if (m_diaryOps) {
                            qDebug() << "m_diaryOps is valid";
                            try {
                                qDebug() << "Calling AddTaskLogEntry";
                                // CRITICAL CHANGE: Pass the actual due date instead of an empty QDateTime
                                m_diaryOps->AddTaskLogEntry("TimeLimit", safeTaskName, localTaskListName, "Overdue",
                                                         safeDueTime, safeMsg);
                                qDebug() << "AddTaskLogEntry completed successfully";
                            } catch (const std::exception& e) {
                                qDebug() << "Exception during diary logging:" << e.what();
                            } catch (...) {
                                qDebug() << "Unknown exception during diary logging";
                            }
                        } else {
                            qDebug() << "m_diaryOps is NULL";
                        }
                        qDebug() << "*** CRITICAL POINT: After logging to diary ***";
                    }
                }

                // Clean up temporary file
                qDebug() << "Before removing temp file";
                QFile::remove(tempPath);
                qDebug() << "After removing temp file";
            }
        }
    } catch (const std::exception& e) {
        qDebug() << "Exception in log to diary section:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception in log to diary section";
    }

    // Schedule the next task
    qDebug() << "Before scheduling next task";
    ScheduleNextDueTask();
    qDebug() << "After scheduling next task";

    qDebug() << "***** FINISHED ProcessDueTask *****";
}

// Update the queue when tasks change
void Operations_TaskLists::UpdateDueTasksQueue()
{
    // We'll just reinitialize the queue for simplicity
    InitializeDueTasksQueue();
}



//---------Task/Tasklit Order Management----------//

// This function handles saving tasks in their new order after drag and drop
void Operations_TaskLists::SaveTaskOrder()
{
    // Get current task list
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;
    if (taskListWidget->currentItem() == nullptr) {
        qWarning() << "No task list selected when trying to save task order";
        return;
    }

    QString currentTaskList = taskListWidget->currentItem()->text();

    // Sanitize the task list name for file operations
    QString sanitizedName = currentTaskList;
    sanitizedName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    // Construct file paths
    QString taskListDir = "Data/" + m_mainWindow->user_Username + "/Tasklists/" + sanitizedName + "/";
    QString taskListFilePath = taskListDir + sanitizedName + ".txt";

    // Validate the file path
    if (!OperationsFiles::validateFilePath(taskListFilePath, OperationsFiles::FileType::TaskList, m_mainWindow->user_Key)) {
        qWarning() << "Invalid file path when saving task order";
        return;
    }

    // Check if the file exists
    QFileInfo fileInfo(taskListFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "Task list file does not exist when saving task order";
        return;
    }

    // Read the task list file
    QStringList taskLines;
    if (!OperationsFiles::readTasklistFile(taskListFilePath, m_mainWindow->user_Key, taskLines)) {
        qWarning() << "Failed to read task list file when saving task order";
        return;
    }

    // Make sure we have at least the header line
    if (taskLines.isEmpty()) {
        qWarning() << "Empty task list file when saving task order";
        return;
    }

    // Get the header line and keep it
    QString headerLine = taskLines.first();

    // Get the tasks in their current order from the list widget
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;
    QMap<QString, QString> taskMap; // Map task names to their data

    // First, check if there are disabled placeholder items
    bool hasPlaceholders = false;
    for (int i = 0; i < taskDisplayWidget->count(); ++i) {
        QListWidgetItem* item = taskDisplayWidget->item(i);
        if ((item->flags() & Qt::ItemIsEnabled) == 0) {
            hasPlaceholders = true;
            break;
        }
    }

    // If there are placeholders, don't save the order
    if (hasPlaceholders) {
        return;
    }

    // Create a map of task names to their data lines from the file (skip header)
    for (int i = 1; i < taskLines.size(); ++i) {
        QString line = taskLines[i];
        if (line.isEmpty()) {
            continue;
        }

        QStringList parts = line.split('|');
        if (parts.size() < 2) {
            continue;
        }

        QString taskName = parts[1];
        taskName.replace("\\|", "|"); // Unescape pipes

        taskMap[taskName] = line;
    }

    // Create a new list with tasks in the new order
    QStringList newTaskLines;
    newTaskLines.append(headerLine); // Keep the header

    // Add tasks in the new order
    for (int i = 0; i < taskDisplayWidget->count(); ++i) {
        QListWidgetItem* item = taskDisplayWidget->item(i);

        // Skip placeholder or disabled items
        if ((item->flags() & Qt::ItemIsEnabled) == 0) {
            continue;
        }

        QString taskName = item->text();
        if (taskMap.contains(taskName)) {
            newTaskLines.append(taskMap[taskName]);
        }
    }

    // Write the updated task list back to the file
    if (!OperationsFiles::writeTasklistFile(taskListFilePath, m_mainWindow->user_Key, newTaskLines)) {
        qWarning() << "Failed to write task list file when saving task order";
    }
}

// Handler for task drop events
void Operations_TaskLists::HandleTaskDropEvent(QDropEvent* event)
{
    // Save the new task order
    SaveTaskOrder();

    // Accept the event
    event->acceptProposedAction();
}

// Save the current order of tasklists to the config file
bool Operations_TaskLists::SaveTasklistOrder()
{
    QListWidget* taskListWidget = m_mainWindow->ui->listWidget_TaskList_List;

    // If there are no tasklists, don't save an empty file
    if (taskListWidget->count() == 0) {
        return false;
    }

    // Create the directory structure if it doesn't exist
    QString settingsDir = "Data/" + m_mainWindow->user_Username + "/Settings/Tasklists/";
    if (!OperationsFiles::ensureDirectoryExists(settingsDir)) {
        qWarning() << "Failed to create directory for tasklist order file";
        return false;
    }

    // Construct the file path
    QString orderFilePath = settingsDir + "TasklistOrder.txt";

    // Only validate if the file already exists
    QFileInfo fileInfo(orderFilePath);
    if (fileInfo.exists() && !OperationsFiles::validateFilePath(orderFilePath, OperationsFiles::FileType::Generic, m_mainWindow->user_Key)) {
        qWarning() << "Invalid tasklist order file path";
        return false;
    }

    // Create content for the file
    QStringList contentLines;
    contentLines.append("# TasklistOrder - Do not edit manually");

    // Add all tasklist names
    for (int i = 0; i < taskListWidget->count(); ++i) {
        QListWidgetItem* item = taskListWidget->item(i);
        QString tasklistName = item->text();

        // Validate each tasklist name
        InputValidation::ValidationResult nameResult =
            InputValidation::validateInput(tasklistName, InputValidation::InputType::TaskListName);
        if (nameResult.isValid) {
            // Add valid tasklist names to the content
            contentLines.append(tasklistName);
        }
    }

    // Write the content to the file
    if (!OperationsFiles::writeEncryptedFileLines(orderFilePath, m_mainWindow->user_Key, contentLines)) {
        qWarning() << "Failed to write tasklist order file";
        return false;
    }

    return true;
}

// Load the tasklist order from the config file
bool Operations_TaskLists::LoadTasklistOrder(QStringList& orderedTasklists)
{
    // Clear the output list
    orderedTasklists.clear();

    // Construct the file path
    QString settingsDir = "Data/" + m_mainWindow->user_Username + "/Settings/Tasklists/";
    QString orderFilePath = settingsDir + "TasklistOrder.txt";

    // Check if the file exists
    QFileInfo fileInfo(orderFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        // Not an error - just means we've never saved an order before
        return false;
    }

    // Validate the file path
    if (!OperationsFiles::validateFilePath(orderFilePath, OperationsFiles::FileType::Generic, m_mainWindow->user_Key)) {
        qWarning() << "Invalid tasklist order file path";
        return false;
    }

    // Read the file content
    QStringList contentLines;
    if (!OperationsFiles::readEncryptedFileLines(orderFilePath, m_mainWindow->user_Key, contentLines)) {
        qWarning() << "Failed to read tasklist order file";
        return false;
    }

    // Need at least the header line
    if (contentLines.isEmpty()) {
        qWarning() << "Empty tasklist order file";
        return false;
    }

    // Check for valid header
    QString headerLine = contentLines.first();
    if (!headerLine.startsWith("# TasklistOrder")) {
        qWarning() << "Invalid tasklist order file format";
        return false;
    }

    // Process all tasklist names (skip the header)
    for (int i = 1; i < contentLines.size(); ++i) {
        QString line = contentLines[i].trimmed();

        // Skip empty lines
        if (line.isEmpty()) {
            continue;
        }

        // Validate each tasklist name
        InputValidation::ValidationResult nameResult =
            InputValidation::validateInput(line, InputValidation::InputType::TaskListName);
        if (nameResult.isValid) {
            orderedTasklists.append(line);
        }
    }

    return !orderedTasklists.isEmpty();
}

// Handler for tasklist drop events
void Operations_TaskLists::HandleTaskListDropEvent(QDropEvent* event)
{
    // Save the new tasklist order
    SaveTasklistOrder();

    // Accept the event
    event->acceptProposedAction();
}


void Operations_TaskLists::EnforceTaskOrder()
{
    QListWidget* taskDisplayWidget = m_mainWindow->ui->listWidget_TaskListDisplay;

    // Skip if there are no items or only one item
    if (taskDisplayWidget->count() <= 1) {
        return;
    }

    // Temporarily block signals to avoid recursion
    taskDisplayWidget->blockSignals(true);

    // Collect all items
    QList<QListWidgetItem*> completedItems;
    QList<QListWidgetItem*> pendingItems;
    QList<QListWidgetItem*> disabledItems;

    // Remember the currently selected item
    QListWidgetItem* currentItem = taskDisplayWidget->currentItem();
    QString currentItemText = currentItem ? currentItem->text() : "";

    // Take all items out of the list
    for (int i = taskDisplayWidget->count() - 1; i >= 0; i--) {
        QListWidgetItem* item = taskDisplayWidget->takeItem(i);

        if (!item) {
            continue;
        }

        // Handle disabled/placeholder items
        if ((item->flags() & Qt::ItemIsEnabled) == 0) {
            disabledItems.prepend(item);
            continue;
        }

        if (item->checkState() == Qt::Checked) {
            completedItems.prepend(item);
        } else {
            pendingItems.prepend(item);
        }
    }

    // Add completed items first (at the top)
    for (QListWidgetItem* item : completedItems) {
        taskDisplayWidget->addItem(item);
    }

    // Then add pending items
    for (QListWidgetItem* item : pendingItems) {
        taskDisplayWidget->addItem(item);
    }

    // Finally add any disabled/placeholder items
    for (QListWidgetItem* item : disabledItems) {
        taskDisplayWidget->addItem(item);
    }

    // Re-enable signals
    taskDisplayWidget->blockSignals(false);

    // Restore the selection if possible
    if (!currentItemText.isEmpty()) {
        for (int i = 0; i < taskDisplayWidget->count(); i++) {
            QListWidgetItem* item = taskDisplayWidget->item(i);
            if (item && item->text() == currentItemText) {
                taskDisplayWidget->setCurrentItem(item);
                break;
            }
        }
    }

    // Save the new task order
    SaveTaskOrder();
}


//-------misc-------//
void Operations_TaskLists::UpdateTasklistsTextSize(int fontSize)
{
    if (!m_mainWindow || !m_mainWindow->ui)
        return;

    // Create font with the specified size
    QFont font;
    font.setPointSize(fontSize);

    // Apply to all text widgets in the Tasklists tab
    m_mainWindow->ui->listWidget_TaskList_List->setFont(font);
    m_mainWindow->ui->listWidget_TaskListDisplay->setFont(font);
    m_mainWindow->ui->tableWidget_TaskDetails->setFont(font);
    m_mainWindow->ui->plainTextEdit_TaskDesc->setFont(font);
    m_mainWindow->ui->label_TaskListName->setFont(font);
    //m_mainWindow->ui->pushButton_NewTaskList->setFont(font);
    //m_mainWindow->ui->pushButton_AddTask->setFont(font);

    // Apply to table headers
    QHeaderView* hHeader = m_mainWindow->ui->tableWidget_TaskDetails->horizontalHeader();
    if (hHeader) {
        hHeader->setFont(font);
    }

    QHeaderView* vHeader = m_mainWindow->ui->tableWidget_TaskDetails->verticalHeader();
    if (vHeader) {
        vHeader->setFont(font);
    }

    // ===== Table height scaling with your provided parameters =====
    const int baseFontSize = 10;
    const int baseTableHeight = 50;
    const int minimumHeight = 50;
    const int linearFactor = 1;
    const double exponentialFactor = 0.23;

    int fontSizeDelta = fontSize - baseFontSize;
    double fontSizeRatio = static_cast<double>(fontSize) / baseFontSize;

    int linearComponent = fontSizeDelta * linearFactor;
    int exponentialComponent = static_cast<int>(baseTableHeight * (fontSizeRatio - 1.0) * exponentialFactor);

    int calculatedHeight = baseTableHeight + linearComponent + exponentialComponent;
    int scaledHeight = std::max(minimumHeight, calculatedHeight);

    m_mainWindow->ui->tableWidget_TaskDetails->setFixedHeight(scaledHeight);

    // ===== Checkbox scaling in the task list display =====
    // Scale the checkbox detection area in our custom list widget
    if (auto* customList = dynamic_cast<custom_QListWidget_Task*>(m_mainWindow->ui->listWidget_TaskListDisplay)) {
        // Determine appropriate checkbox width based on font size
        // Base checkbox width is 25 at font size 10
        int scaledCheckboxWidth = static_cast<int>(25 * (fontSizeRatio));

        // Set the checkbox detection width in our custom widget
        customList->setCheckboxWidth(scaledCheckboxWidth);
    }

    // Scale the visual appearance of checkboxes using stylesheets
    // Base size is usually around 13px for checkboxes at font size 10
    int checkboxSize = static_cast<int>(13 + (fontSize - baseFontSize) * 0.3);

    // Create a stylesheet for the checkboxes
    QString styleSheet = QString(
        "QListWidget::indicator {"
        "    width: %1px;"
        "    height: %1px;"
        "}"
    ).arg(checkboxSize);

    // Apply the stylesheet to the list widget
    m_mainWindow->ui->listWidget_TaskListDisplay->setStyleSheet(styleSheet);

    // Resize columns to fit content
    if (m_mainWindow->ui->tableWidget_TaskDetails->rowCount() > 0) {
        m_mainWindow->ui->tableWidget_TaskDetails->resizeColumnsToContents();
    }

    // Force a redraw of the widgets
    m_mainWindow->ui->listWidget_TaskList_List->update();
    m_mainWindow->ui->listWidget_TaskListDisplay->update();
    m_mainWindow->ui->tableWidget_TaskDetails->update();
    m_mainWindow->ui->plainTextEdit_TaskDesc->update();
}
