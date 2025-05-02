#ifndef OPERATIONS_DIARY_H
#define OPERATIONS_DIARY_H

#include <QObject>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include <QMessageBox>
class MainWindow;
class Operations_Diary : public QObject
{
    Q_OBJECT
private:
    MainWindow* m_mainWindow;
    QString current_DiaryFileName, previous_DiaryFileName, currentdiary_Year, currentdiary_Month, DiariesFilePath = "Diaries/", currentdiary_DateStamp;
    int lastTimeStamp_Hours, lastTimeStamp_Minutes, entrySpacer_Delay = 5, entriesNoSpacerLimit = 5, cur_entriesNoSpacer, previousDiaryLineCounter;
    QStringList DiariesList, currentyear_DiaryList, currentmonth_DiaryList;
    QFont font_TimeStamp;
    QString uneditedText;
    bool prevent_onDiaryTextDisplay_itemChanged;

public:
    ~Operations_Diary();
    explicit Operations_Diary(MainWindow* mainWindow);
    friend class MainWindow;
    // Moved functions
    void InputNewEntry(QString DiaryFileName);
    void AddNewEntryToDisplay();
    QList<QListWidgetItem*> getTextDisplayItems();
    void DiaryLoader();
    void CreateNewDiary();
    void LoadDiary(QString DiaryFileName);
    void SaveDiary(QString DiaryFileName, bool previousDiary);
    void DeleteDiary(QString DiaryFileName);
    QString GetDiaryDateStamp(QString date_time);
    void remove_EmptyTimestamps(bool previousDiary);
    void UpdateDiarySorter(QString current_Year, QString current_Month, QString current_Day);
    void UpdateListYears();
    void UpdateListMonths(QString current_Year);
    void UpdateListDays(QString current_Month);
    QString getDiaryFilePath(const QString& dateString);
    void UpdateDisplayName();
    void showContextMenu_TextDisplay(const QPoint &pos);
    void showContextMenu_ListDays(const QPoint &pos);
    void ScrollBottom();
    void UpdateDelegate();
    void ensureDiaryDirectoryExists(const QString& dateString);
    void AddTaskLogEntry(QString taskType, QString taskName, QString taskListName, QString entryType, QDateTime dateTime = QDateTime(), QString additionalInfo = "");
    QString FormatDateTime(const QDateTime& dateTime);
    QString FindLastTimeStampType(int index = 0);

public slots:
    void DeleteEmptyCurrentDayDiary();
    void OpenEditor();
    void DeleteDiaryFromListDays();
    void DeleteEntry();
    void CopyToClipboard();
    void on_DiaryTextInput_returnPressed();
    void on_DiaryListYears_currentTextChanged(const QString &arg1);
    void on_DiaryListMonths_currentTextChanged(const QString &currentText);
    void on_DiaryListDays_currentTextChanged(const QString &currentText);
    void on_DiaryTextDisplay_itemChanged();
    void on_DiaryTextDisplay_entered(const QModelIndex &index);
    void on_DiaryTextDisplay_clicked();

signals:
    void UpdateFontSize(int size, bool resize);
};

#endif // OPERATIONS_DIARY_H
