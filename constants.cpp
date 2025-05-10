#include "constants.h"
#include <QRandomGenerator>
#include <QStringList>

namespace Constants
{
// Global
const QString AppVer = "4.0.8";
// Error Messages
const QString ErrorMessage_Default = "ERROR";
const QString ErrorMessage_INVUSER = "ERROR - INVALID USER";
// Settings Buttons
const QString SettingsButton_SaveGlobal = "SaveGlobal";
const QString SettingsButton_CancelGlobal = "CancelGlobal";
const QString SettingsButton_ResetGlobal = "ResetGlobal";
const QString SettingsButton_SaveDiary = "SaveDiary";
const QString SettingsButton_CancelDiary = "CancelDiary";
const QString SettingsButton_ResetDiary = "ResetDiary";
const QString SettingsButton_SaveTasklists = "SaveTasklists";
const QString SettingsButton_CancelTasklists = "CancelTasklists";
const QString SettingsButton_ResetTasklists = "ResetTasklists";
const QString SettingsButton_SavePWManager = "SavePWManager";
const QString SettingsButton_CancelPWManager = "CancelPWManager";
const QString SettingsButton_ResetPWManager = "ResetPWManager";
// Data Types
const QString DataType_QString = "QString";
const QString DataType_QByteArray = "QByteArray";
const QString DataType_INT = "INT";
const QString DataType_BOOL = "BOOL";
// Settings Types
const QString DBSettings_Type_ALL = "ALL";
const QString DBSettings_Type_Global = "Global";
const QString DBSettings_Type_Diary = "Diary";
const QString DBSettings_Type_Tasklists = "Tasklists";
const QString DBSettings_Type_PWManager = "PWManager";
// User Database
const QString DBPath_User = "Data/MMDiary.db";
// User Database Table Indexes - User Info
const QString UserT_Index_Username = "username";
const QString UserT_Index_Password = "password";
const QString UserT_Index_EncryptionKey = "encryptionkey";
const QString UserT_Index_Salt = "salt";
const QString UserT_Index_Iterations = "iterations";
// User Database Table Indexes - Global Settings
const QString UserT_Index_Displayname = "displayname";
const QString UserT_Index_DisplaynameColor = "displaynamecolor";
const QString UserT_Index_MinToTray = "MinToTray";
const QString UserT_Index_AskPWAfterMinToTray = "AskPWAfterMinToTray";
// User Database Table Indexes - Diary Settings
const QString UserT_Index_Diary_TextSize = "Diary_TextSize";
const QString UserT_Index_Diary_TStampTimer = "Diary_TStampTimer";
const QString UserT_Index_Diary_TStampCounter = "Diary_TStampCounter";
const QString UserT_Index_Diary_CanEditRecent = "Diary_CanEditRecent";
const QString UserT_Index_Diary_ShowTManLogs = "Diary_ShowTManLogs";
// User Database Table Indexes - Tasklist Settings
const QString UserT_Index_TLists_LogToDiary = "TLists_LogToDiary";
const QString UserT_Index_TLists_TaskType = "TLists_TaskType";
const QString UserT_Index_TLists_CMess = "TLists_CMess";
const QString UserT_Index_TLists_PMess = "TLists_PMess";
const QString UserT_Index_TLists_Notif = "TLists_Notif";
const QString UserT_Index_TLists_TextSize = "TLists_TextSize";
// User Database Table Indexes - Password Manager Settings
const QString UserT_Index_PWMan_DefSortingMethod = "PWMan_DefSortingMethod";
const QString UserT_Index_PWMan_ReqPassword = "PWMan_ReqPassword";
const QString UserT_Index_PWMan_HidePasswords = "PWMan_HidePasswords";
// Diary Text Codes
const QString Diary_Spacer = "<!spacer!>";
const QString Diary_TextBlockStart = "<!TextBlockStart!>";
const QString Diary_TextBlockEnd = "<!TextBlockEnd!>";
const QString Diary_TimeStampStart = "<!TimeStampStart!>";
const QString Diary_TaskManagerStart = "<!TaskManagerStart!>";
//Diary
const QString TASK_MANAGER_TEXT = "Task Manager";


QString GetCPUNMessage(CPUNType type, CPUNCategory category)
{
    if(type == CPUNType::Congrat)
    {
        // Arrays of task-oriented congratulatory messages for each category
        static const QStringList simpleMessages = {
            "Task completed.",
            "Item checked off.",
            "Task done.",
            "Complete.",
            "Finished task.",
            "Task accomplished.",
            "Item complete.",
            "Done and dusted.",
            "Completed successfully.",
            "Task finished.",
            "Item done.",
            "Task cleared.",
            "One less thing to do.",
            "Progress made.",
            "Moving forward.",
            "Task handled.",
            "Item wrapped up.",
            "Completed item.",
            "Task taken care of.",
            "Item completed."
        };

        static const QStringList advancedMessages = {
            "Great job completing that task!",
            "Well done on finishing this item!",
            "Nice work checking that off your list!",
            "You're making excellent progress!",
            "That's another task well handled!",
            "You're getting things done efficiently!",
            "Great progress on your to-do list!",
            "You're being really productive today!",
            "Excellent work on completing that task!",
            "You're moving through your tasks nicely!",
            "Good momentum on your task list!",
            "You're making solid progress today!",
            "That's how you get things done!",
            "Another task expertly completed!",
            "You're checking items off like a pro!",
            "Great job staying on top of your tasks!",
            "You're really accomplishing things today!",
            "That's good task management!",
            "You're being very effective today!",
            "Your productivity is impressive!"
        };

        static const QStringList intenseMessages = {
            "Spectacular job finishing that task!",
            "Outstanding work on your task completion!",
            "You're absolutely crushing your to-do list!",
            "Your productivity is through the roof!",
            "Remarkable efficiency in completing tasks!",
            "You're a task-completing machine today!",
            "Incredibly impressive progress on your tasks!",
            "Your task management skills are exceptional!",
            "You're demolishing that to-do list!",
            "Phenomenal job knocking out those tasks!",
            "You're making task completion look easy!",
            "That's serious productivity in action!",
            "Your task completion rate is off the charts!",
            "You're in the zone with your productivity!",
            "That's how a champion handles their tasks!",
            "Your task management is absolutely stellar!",
            "You're showing extraordinary productivity!",
            "That's masterful task completion!",
            "You're powering through your task list!",
            "Your productivity level is inspiring!"
        };

        static const QStringList extremeMessages = {
            "You are the supreme overlord of task completion! Nothing can stand in your way!",
            "Your task-destroying powers have reached mythical status! Productivity gods bow to you!",
            "The universe itself is in awe of your legendary task management abilities!",
            "Your to-do list trembles in fear knowing you'll obliterate every task with godlike efficiency!",
            "You've transcended ordinary productivity! Your task completion skills defy human limitations!",
            "Mountains of tasks crumble before your unstoppable productivity prowess!",
            "Your task management excellence has altered the space-time continuum of productivity!",
            "You are the Mozart of task completion - creating symphonies of productivity that will echo through eternity!",
            "Historians will study your unprecedented task-conquering abilities for generations to come!",
            "Your productivity superpowers make mere mortals question their existence!",
            "Task lists around the world speak your name in hushed, reverent tones!",
            "You've unlocked productivity achievements that scientists thought were theoretically impossible!",
            "Your task-crushing abilities have reached such heights that productivity apps crash in your presence!",
            "Your to-do list management deserves its own Nobel Prize category!",
            "The task you just completed was so masterfully executed that productivity experts are weeping with joy!",
            "You don't just complete tasks - you revolutionize the very concept of accomplishment!",
            "Task completion is your superpower, and you're saving the world one checklist at a time!",
            "Your productivity aura is so powerful it completes neighboring tasks automatically!",
            "You've shattered all known records in the task completion hall of fame!",
            "Your task management brilliance is so blinding I need special glasses just to witness it!"
        };

        // Select and return a random message from the appropriate category
        switch (category) {
        case CPUNCategory::None:
            return QString();

        case CPUNCategory::Simple:
            if (!simpleMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(simpleMessages.size());
                return simpleMessages[randomIndex];
            }
            break;

        case CPUNCategory::Advanced:
            if (!advancedMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(advancedMessages.size());
                return advancedMessages[randomIndex];
            }
            break;

        case CPUNCategory::Intense:
            if (!intenseMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(intenseMessages.size());
                return intenseMessages[randomIndex];
            }
            break;

        case CPUNCategory::Extreme:
            if (!extremeMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(extremeMessages.size());
                return extremeMessages[randomIndex];
            }
            break;

        default:
            break;
        }
    }
    else if(type == CPUNType::Punish)
    {
        // Arrays of task-focused punitive messages for missed deadlines
        static const QStringList simpleMessages = {
            "Task not completed yet.",
            "This task is still pending.",
            "Task remains unfinished.",
            "Still on your to-do list.",
            "Deadline missed.",
            "Task needs attention.",
            "Item not completed.",
            "Task left undone.",
            "Completion deadline passed.",
            "This task was delayed.",
            "Task still waiting.",
            "Item not crossed off yet.",
            "Task still pending completion.",
            "This task needs finishing.",
            "Task still requires attention.",
            "Unfinished business.",
            "This task was overlooked.",
            "Task postponed.",
            "To-do item missed.",
            "Task completion delayed."
        };

        static const QStringList advancedMessages = {
            "You missed this task's deadline.",
            "This task should have been completed by now.",
            "Your timeline for this task has elapsed.",
            "You've fallen behind on this task.",
            "This task is now overdue.",
            "You didn't finish this task on time.",
            "This task was not completed as scheduled.",
            "You've let this task slip past its deadline.",
            "Task completion failure noted.",
            "You missed your window to complete this.",
            "This task is now behind schedule.",
            "You didn't meet the deadline for this task.",
            "Task timeline not respected.",
            "You've procrastinated on this task.",
            "This task has been neglected.",
            "You failed to prioritize this task.",
            "Deadline violation on this task.",
            "You've dropped the ball on this one.",
            "This task needed more of your attention.",
            "You didn't follow through on this task."
        };

        static const QStringList intenseMessages = {
            "Serious failure to meet this task's deadline.",
            "Your procrastination has caught up with this task.",
            "This task has been severely neglected.",
            "Major deadline missed on an important task.",
            "You've completely failed to manage this task.",
            "Your task management has broken down here.",
            "Critical failure in your time management for this task.",
            "You've significantly underperformed on this deadline.",
            "This represents a serious lapse in your productivity.",
            "You've let yourself down with this missed deadline.",
            "Your accountability on this task has been poor.",
            "This is a considerable failure in your task management.",
            "You've shown a concerning lack of follow-through here.",
            "Your commitment to this task was clearly inadequate.",
            "This deadline failure reflects poorly on your reliability.",
            "You've demonstrated disappointing time management here.",
            "Your focus and discipline failed you on this task.",
            "This task exposes a significant weakness in your execution.",
            "You've missed a critical opportunity by not completing this.",
            "Your performance on this task timeline is unacceptable."
        };

        static const QStringList extremeMessages = {
            "What are you doing ? You miserable piece of shit. Can't even complete one single task.",
            "Pathetic, why do you even bother. Just stop trying at this point.",
            "I knew you would fail. You can't do shit. Stop wasting my time.",
            "Well well well, another task that you fail to accomplish on time, to no ones surprise.",
            "Seriously though stop pretending. You'll never reach your goals.",
            "What is wrong with you, come on man, what a shame.",
            "Your dad was right, you'll never amount to nothing.",
            "I've seen young children with more discipline than you, pathetic.",
            "Can you even do anything right ? I don't think so",
            "You couldn't complete a task if it meant saving your life. Disgraceful.",
            "You are so irresponsible, I bet your mom has to do everything for you",
            "If you were a tool you would break on first use. So unreliable.",
            "Stop trying already you good for nothing loser.",
            "Hahaha . . ahh you thought you could actually do it ? funny!",
            "I don't know what's craziest, the fact that you thought you could finish this task or the fact that you were'nt aborted.",
            "You are easily the least motivated person I ever met.",
            "You had one job. One simple fucking job. You are hopeless.",
            "Please tell me you don't have kids. I would feel so bad for them if you did.",
            "Insert hurtful message here. That's how little I care about you, not even gonna bother.",
            "What kind of idiot would fail such an easy . . oh wait, nevermind."
        };

        // Select and return a random message from the appropriate category
        switch (category) {
        case CPUNCategory::None:
            return QString();

        case CPUNCategory::Simple:
            if (!simpleMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(simpleMessages.size());
                return simpleMessages[randomIndex];
            }
            break;

        case CPUNCategory::Advanced:
            if (!advancedMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(advancedMessages.size());
                return advancedMessages[randomIndex];
            }
            break;

        case CPUNCategory::Intense:
            if (!intenseMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(intenseMessages.size());
                return intenseMessages[randomIndex];
            }
            break;

        case CPUNCategory::Extreme:
            if (!extremeMessages.isEmpty()) {
                int randomIndex = QRandomGenerator::global()->bounded(extremeMessages.size());
                return extremeMessages[randomIndex];
            }
            break;

        default:
            break;
        }
    }
    return QString();
}

}
