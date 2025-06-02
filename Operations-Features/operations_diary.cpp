#include "operations_diary.h"
#include "../CustomWidgets/CombinedDelegate.h"
#include "../Operations-Global/CryptoUtils.h"
#include "Operations-Global/operations_files.h"
#include "Operations-Global/imageviewer.h"
#include "qimagereader.h"
#include "ui_mainwindow.h"
#include "../constants.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QMutexLocker>
#include <QPainter>
#include <QInputDialog>

Operations_Diary::Operations_Diary(MainWindow* mainWindow)
    : m_mainWindow(mainWindow)
{
    DiariesFilePath = "Data/" + m_mainWindow->user_Username + "/Diaries/";

    // Connect image handling signals
    connect(m_mainWindow->ui->DiaryTextInput, &custom_QTextEditWidget::imagesDropped,
            this, [this](const QStringList& imagePaths) {
                processAndAddImages(imagePaths, imagePaths.size() > 1);
            });

    connect(m_mainWindow->ui->DiaryTextInput, &custom_QTextEditWidget::imagesPasted,
            this, [this](const QStringList& imagePaths) {
                processAndAddImages(imagePaths, imagePaths.size() > 1);
            });
}

Operations_Diary::~Operations_Diary()
{
}

// Operational Functions

QList<QListWidgetItem*> Operations_Diary::getTextDisplayItems()
{
    QList<QListWidgetItem*> items;
    int count = m_mainWindow->ui->DiaryTextDisplay->count();
    for(int i=0;i<count;i++)
    {
        items.append(m_mainWindow->ui->DiaryTextDisplay->item(i));
    }
    return items;
}

QString Operations_Diary::GetDiaryDateStamp(QString date_time)
{
    // Validate the date string format
    InputValidation::ValidationResult result =
        InputValidation::validateInput(date_time, InputValidation::InputType::PlainText);
    if (!result.isValid) {
        qWarning() << "Invalid date string format:" << result.errorMessage;
        return "ERROR - Invalid date format";
    }

    QDate curDate(date_time.section('.',0,0).toInt(),date_time.section('.',1,1).toInt(),date_time.section('.',2,2).toInt());
    QString year = date_time.section('.',0,0);
    QString month = Operations::ConvertMonthtoText(date_time.section('.',1,1));
    QString dayoftheweek = Operations::GetDayOfWeek(curDate);
    QString day = date_time.section('.',2,2);
    if(day.toInt() < 10)
    {
        day = day.right(1); // if day is lower than 10, remove the first number which happens to be 0. so 09 becomes 9 for example.
    }
    QString datestamp = dayoftheweek + " the " + day + Operations::GetOrdinalSuffix(day.toInt()) + " of " + month + " " + year;
    return datestamp;
}

QString Operations_Diary::getDiaryFilePath(const QString& dateString)
{
    // Check if dateString contains "ERROR" and prevent creation
    if(!m_mainWindow->initFinished)
    {
        return QString();
    }

    // Validate the dateString
    InputValidation::ValidationResult result =
        InputValidation::validateInput(dateString, InputValidation::InputType::PlainText);
    if (!result.isValid) {
        qWarning() << "Invalid date string:" << result.errorMessage;
        return QString();
    }

    // Validate date format with regex
    QRegularExpression validDatePattern("^\\d{4}\\.\\d{2}\\.\\d{2}$");
    if (!validDatePattern.match(dateString).hasMatch()) {
        qWarning() << "Invalid date format:" << dateString;
        return QString();
    }

    if (dateString.contains("ERROR", Qt::CaseInsensitive)) {
        qDebug() << "Attempted to create a directory with 'ERROR' in the name: " << dateString;
        return QString(); // Return empty string to indicate error
    }

    // Parse the date parts
    QStringList dateParts = dateString.split('.');
    if (dateParts.size() != 3) {
        qDebug() << "Invalid date format: " << dateString;
        return QString();
    }

    QString year = dateParts[0];
    QString month = dateParts[1];
    QString day = dateParts[2];

    // Verify that year, month, and day are valid numeric values
    bool yearOk, monthOk, dayOk;
    int yearNum = year.toInt(&yearOk);
    int monthNum = month.toInt(&monthOk);
    int dayNum = day.toInt(&dayOk);

    if (!yearOk || !monthOk || !dayOk ||
        yearNum < 1900 || yearNum > 2100 ||
        monthNum < 1 || monthNum > 12 ||
        dayNum < 1 || dayNum > 31) {
        qDebug() << "Invalid date components:" << dateString;
        return QString();
    }

    // Build hierarchical path structure
    QString hierarchicalPath = DiariesFilePath + year + "/" + month + "/" + day;
    QString filePath = hierarchicalPath + "/" + dateString + ".txt";

    // Validate the constructed file path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Invalid file path:" << pathResult.errorMessage;
        return QString();
    }

    return filePath;
}

void Operations_Diary::ensureDiaryDirectoryExists(const QString& dateString)
{
    // Validate the dateString
    InputValidation::ValidationResult result =
        InputValidation::validateInput(dateString, InputValidation::InputType::PlainText);
    if (!result.isValid) {
        qWarning() << "Invalid date string for directory creation:" << result.errorMessage;
        return;
    }

    // First ensure the base Diaries directory exists
    if (!OperationsFiles::ensureDirectoryExists(DiariesFilePath)) {
        qWarning() << "Failed to create base diaries directory:" << DiariesFilePath;
        return;
    }

    // Only create directory if it's a valid date format (YYYY.MM.DD)
    QStringList dateParts = dateString.split('.');
    if (dateParts.size() != 3) {
        qDebug() << "Cannot create directory for invalid date format: " << dateString;
        return;
    }

    QString year = dateParts[0];
    QString month = dateParts[1];
    QString day = dateParts[2];

    // Use the hierarchical directory creation function
    QStringList pathComponents;
    pathComponents << year << month << day;

    OperationsFiles::createHierarchicalDirectory(pathComponents, DiariesFilePath);
}

QString Operations_Diary::FormatDateTime(const QDateTime& dateTime)
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

QString Operations_Diary::FindLastTimeStampType(int index)
{
    QList<QListWidgetItem*> items = getTextDisplayItems();
    int start_index;
    if (index == 0)
    {
        start_index = items.size() - 1;
    }
    else
    {
        if (index < 0 || index >= items.size())
        {
            qDebug() << "Invalid index:" << index;
            return "";
        }
        start_index = index;
    }
    for (int i = start_index; i >= 0; i--)
    {
        QString itemText = items.at(i)->text();
        if (itemText == Constants::Diary_TimeStampStart)
        {
            return Constants::Diary_TimeStampStart;
        }
        else if (itemText == Constants::Diary_TaskManagerStart)
        {
            return Constants::Diary_TaskManagerStart;
        }
    }
    return "";
}

// Diary Operations

void Operations_Diary::AddNewEntryToDisplay()
{
    // Validate the diary input text
    QString diaryText = m_mainWindow->ui->DiaryTextInput->toPlainText();
    InputValidation::ValidationResult result =
        InputValidation::validateInput(diaryText, InputValidation::InputType::DiaryContent, 100000);

    if (!result.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Diary Content",
                             result.errorMessage + "\nPlease edit your entry.");
        return;
    }

    //if the text has newlines, add markers so that when we load this later we can recreate the text block as a single item
    if(diaryText.contains("\n")) // if the text we input contains more than one line.
    {
        m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TextBlockStart); // add text block start marker
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setHidden(true); // hide text block start marker
        m_mainWindow->ui->DiaryTextDisplay->addItem(diaryText); // add text
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->flags() | Qt::ItemIsEditable); // make our new entry editable
        m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TextBlockEnd); // add text block end marker
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setHidden(true); // hide text block end marker
    }
    else
    {
        m_mainWindow->ui->DiaryTextDisplay->addItem(diaryText); // add text
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->flags() | Qt::ItemIsEditable); // make our new entry editable
    }
}

void Operations_Diary::InputNewEntry(QString DiaryFileName)
{
    // Validate the diary file name
    InputValidation::ValidationResult fileResult =
        InputValidation::validateInput(DiaryFileName, InputValidation::InputType::FilePath);
    if (!fileResult.isValid) {
        qWarning() << "Invalid diary file path:" << fileResult.errorMessage;
        return;
    }

    // Validate the diary input text
    QString diaryText = m_mainWindow->ui->DiaryTextInput->document()->toPlainText();
    InputValidation::ValidationResult contentResult =
        InputValidation::validateInput(diaryText, InputValidation::InputType::DiaryContent, 100000);

    if (!contentResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Diary Content",
                             contentResult.errorMessage + "\nPlease edit your entry.");
        return;
    }

    prevent_onDiaryTextDisplay_itemChanged = true; // variable used to prevent on_DiaryTextDisplay_itemChanged() from executing. this function is only for editing text
    // REMOVES THE SPACER WIDGET USED IN DESELECTING THE LAST ENTRY. WE DONT WANT TO SAVE IT IN THE DIARY FILE
    m_mainWindow->ui->DiaryTextDisplay->takeItem(getTextDisplayItems().length() - 1);
    //----------------------//
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("hh:mm"); // Format approprietly
    int currentTimeMinutes = formattedTime.section(":",0,0).toInt() * 60 + formattedTime.section(":",1,1).toInt(); // get the current time in minutes

    //for each \n, add 1 to entries without spacer counter. Makes it so text block don't bypass this counter. // this one is for when you add a textblock to an existing timestamp span
    for (QChar c : diaryText) {
        if (c == '\n') {
            cur_entriesNoSpacer++; // add to counter for each \n
        }
    }

    //if not enough time has passed since the last entry and we havnt reached the limit of entries without spacers in a row and textdisplay isnt empty
    if(lastTimeStamp_Hours * 60 + lastTimeStamp_Minutes > currentTimeMinutes - m_mainWindow->setting_Diary_TStampTimer && cur_entriesNoSpacer < m_mainWindow->setting_Diary_TStampCounter)
    {
        AddNewEntryToDisplay(); // Use the local function
        cur_entriesNoSpacer++; // add to the entries without spacer counter
    }
    else // if enough time has passed since the last timestamp or we have reached the limit of entries without a spacer or the diary file is empty
    {
        QString timestamp = m_mainWindow->user_Displayname + " at " + formattedTime;
        m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_Spacer); //Add spacer
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the spacer
        m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TimeStampStart); //Add timestamp start marker
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setHidden(true); // hide the timestamp marker
        m_mainWindow->ui->DiaryTextDisplay->addItem(timestamp); //Add timestamp
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setData(Qt::UserRole+1, true);
        font_TimeStamp.setFamily(timestamp); // define the value of the text that is going to have it's font changed.
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setFont(font_TimeStamp); // change the font of the last item in our list, which is our timestamp.
        m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the timestamp
        AddNewEntryToDisplay(); // Use the local function
        lastTimeStamp_Hours = formattedTime.section(":",0,0).toInt(); // update the hours value of our lastTimeStamp variable
        lastTimeStamp_Minutes = formattedTime.section(":",1,1).toInt(); // update the minutes value of our lastTimeStamp variable
        cur_entriesNoSpacer = 0; // Reset the entries without spacer counter
        //for each \n, add 1 to entries without spacer counter. Makes it so text block don't bypass this counter. // this one is for when you add a text block to a new timstamp span
        for (QChar c : diaryText) {
            if (c == '\n') {
                cur_entriesNoSpacer++; // add to counter for each \n
            }
        }
    }

    // Make sure directory exists
    QFileInfo fileInfo(DiaryFileName);
    QString dirPath = fileInfo.dir().path();
    QStringList pathComponents;

    // Extract directory components
    QString relativePath = dirPath;
    if (relativePath.startsWith(DiariesFilePath)) {
        relativePath = relativePath.mid(DiariesFilePath.length());
    }

    pathComponents = relativePath.split("/", Qt::SkipEmptyParts);
    OperationsFiles::createHierarchicalDirectory(pathComponents, DiariesFilePath);

    SaveDiary(DiaryFileName, false); // save todays diary
    //Add a spacer that is used only for one reason, being able to deselect the last entry of the display. IT IS NOT SAVED INTO OUR DIARY FILE
    m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_Spacer); //Add spacer
    m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setData(Qt::UserRole, true); // uses hidetext delegate to hide the spacer
    m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the spacer
    m_mainWindow->ui->DiaryTextDisplay->item(getTextDisplayItems().length() - 1)->setHidden(true);
    //------------------------------//
    prevent_onDiaryTextDisplay_itemChanged = false; // variable used to prevent on_DiaryTextDisplay_itemChanged() from executing. this function is only for editing text

    // Now select the newly added entry
    // Get the previous-to-last item (the actual entry, not the spacer)

    if (Operations::GetListItems(m_mainWindow->ui->DiaryTextDisplay).length() > 1) {
        m_mainWindow->ui->DiaryTextDisplay->setCurrentItem(m_mainWindow->ui->DiaryTextDisplay->item(Operations::GetListItems(m_mainWindow->ui->DiaryTextDisplay).length() - 2));
    }

    UpdateDelegate();
    UpdateFontSize(m_mainWindow->setting_Diary_TextSize, false);
    m_mainWindow->ui->DiaryTextDisplay->scrollToBottom(); // scroll to bottom of diary text display
    m_mainWindow->ui->DiaryTextInput->clear(); // Clear text input
}

void Operations_Diary::SaveDiary(QString DiaryFileName, bool previousDiary)
{
    QMutexLocker locker(&m_saveDiaryMutex);
    // Validate the diary file name
    InputValidation::ValidationResult fileResult =
        InputValidation::validateInput(DiaryFileName, InputValidation::InputType::FilePath);
    if (!fileResult.isValid) {
        qWarning() << "Invalid diary file path for save operation:" << fileResult.errorMessage;
        return;
    }

    // First ensure the directory exists
    QFileInfo fileInfo(DiaryFileName);
    QString dirPath = fileInfo.dir().path();
    QStringList pathComponents;

    // Extract directory components
    QString relativePath = dirPath;
    if (relativePath.startsWith(DiariesFilePath)) {
        relativePath = relativePath.mid(DiariesFilePath.length());
    }

    pathComponents = relativePath.split("/", Qt::SkipEmptyParts);
    OperationsFiles::createHierarchicalDirectory(pathComponents, DiariesFilePath);

    QList<QListWidgetItem *> items = getTextDisplayItems(); // get a list of all of our textdisplay items

    if(previousDiary) // if we are saving the previous diary, example: we just edited an entry in the previous diary display
    {
        foreach(QListWidgetItem *item, items) // for each line of text
        {
            if(items.indexOf(item) >= previousDiaryLineCounter) // if it's index is bigger or equal to the length of our previous diary display
            {
                items.removeAt(items.indexOf(item)); //remove the item from our list of text to save, so that we dont add todays diary content to our previous diary
            }
        }
    }
    else // otherwise we are saving todays diary
    {
        items.remove(0, previousDiaryLineCounter); // remove the previous diary text. Prevents addition of previous diary content to new one.
    }

    // Construct the text content
    QStringList diaryContent;
    foreach(QListWidgetItem *item, items)
    {
        bool isImageItem = item->data(Qt::UserRole+3).toBool();

        if (isImageItem) {
            // Reconstruct image markers for image items
            diaryContent.append(Constants::Diary_ImageStart);

            bool isMultiImage = item->data(Qt::UserRole+5).toBool();
            if (isMultiImage) {
                QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
                QStringList imageFilenames;
                foreach(const QString& path, imagePaths) {
                    imageFilenames.append(QFileInfo(path).fileName());
                }
                diaryContent.append(imageFilenames.join("|"));
            } else {
                QString imagePath = item->data(Qt::UserRole+4).toString();
                diaryContent.append(QFileInfo(imagePath).fileName());
            }

            diaryContent.append(Constants::Diary_ImageEnd);
        } else {
            // Regular text item - validate before saving
            QString itemText = item->text();
            InputValidation::ValidationResult contentResult =
                InputValidation::validateInput(itemText, InputValidation::InputType::DiaryContent, 100000);

            if (!contentResult.isValid) {
                qWarning() << "Invalid content in diary entry: " << contentResult.errorMessage;
                // You could choose to skip this entry, replace with sanitized text,
                // or continue anyway depending on your requirements
                // For now, we'll continue but log the issue
            }

            diaryContent.append(itemText);
        }
    }

    // Use the centralized file operation to write and encrypt the file
    bool success = OperationsFiles::writeEncryptedFileLines(DiaryFileName, m_mainWindow->user_Key, diaryContent);
    if (!success) {
        qDebug() << "Failed to save diary file: " << DiaryFileName;
    }
}

void Operations_Diary::LoadDiary(QString DiaryFileName)
{
    // Use a mutex to prevent loading while a save is in progress
    QMutexLocker locker(&m_saveDiaryMutex);

    // Validate the diary file name
    InputValidation::ValidationResult fileResult =
        InputValidation::validateInput(DiaryFileName, InputValidation::InputType::FilePath);
    if (!fileResult.isValid) {
        qWarning() << "Invalid diary file path for load operation:" << fileResult.errorMessage;
        return;
    }

    // Get the diary directory path for image loading (keep as relative path)
    QFileInfo diaryFileInfo(DiaryFileName);
    QString diaryDir = diaryFileInfo.dir().path();

    // Check if file exists before attempting to decrypt
    QFileInfo fileInfo(DiaryFileName);
    if (!fileInfo.exists()) {
        qWarning() << "Diary file does not exist:" << DiaryFileName;
        return;
    }

    // Validate file name format using a regular expression
    QString fileName = fileInfo.fileName();
    QRegularExpression validFilenamePattern("^\\d{4}\\.\\d{2}\\.\\d{2}\\.txt$");
    if (!validFilenamePattern.match(fileName).hasMatch()) {
        qWarning() << "Invalid diary file name format:" << fileName;
        return;
    }

    // Validate diary file integrity
    if (!OperationsFiles::validateFilePath(DiaryFileName, OperationsFiles::FileType::Diary, m_mainWindow->user_Key)) {
        qWarning() << "Diary file failed integrity check: " << DiaryFileName;
        QMessageBox::warning(m_mainWindow, "Diary File Error",
                             "The diary file appears to be corrupted or tampered with.");
        return;
    }

    m_mainWindow->ui->DiaryTextDisplay->clear(); // Clear the Diary Display Before Loading the New Diary File
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format appropriately
    QString todayDiaryPath = getDiaryFilePath(formattedTime);
    if (todayDiaryPath.isEmpty()) {
        // Handle the error case
        qDebug() << "Invalid diary path for date: " << formattedTime;
        return;
    }

    QList<QListWidgetItem *> items; // create our listwidgetitem list variable
    previousDiaryLineCounter = 0; // reset the previous diary line counter
    bool nextLine_isTimeStamp = false;
    bool nextLine_isTextBlock = false;
    bool nextLine_isImage = false;
    bool nextLine_isTaskManager = false;
    bool inTaskManagerSection = false; // Flag to track if we're in a Task Manager section

    QString textblock;
    textblock.clear();

    if(DiaryFileName == todayDiaryPath) // if we are opening today's diary
    {
        // Find the previous diary by traversing the directory structure
        QString prevDiaryPath = "";
        bool foundPrevDiary = false;

        // Extract the date from the file path
        QFileInfo fileInfo(DiaryFileName);
        QString fileName = fileInfo.fileName();
        QString dateString = fileName.left(fileName.lastIndexOf('.'));

        // Parse today's date
        QDate todayDate(
            dateString.section('.', 0, 0).toInt(),
            dateString.section('.', 1, 1).toInt(),
            dateString.section('.', 2, 2).toInt()
            );

        // Calculate yesterday's date
        QDate yesterdayDate = todayDate.addDays(-1);

        // Format yesterday's date as YYYY.MM.DD
        QString yesterdayString = QString("%1.%2.%3")
                                      .arg(yesterdayDate.year())
                                      .arg(yesterdayDate.month(), 2, 10, QChar('0'))
                                      .arg(yesterdayDate.day(), 2, 10, QChar('0'));

        // Check if yesterday's diary exists
        prevDiaryPath = getDiaryFilePath(yesterdayString);
        if (QFileInfo::exists(prevDiaryPath)) {
            foundPrevDiary = true;
        }
        else {
            // If yesterday's diary doesn't exist, find the most recent diary before today
            QList<QString> allDiaries;

            // Scan year folders
            QDir baseDir(DiariesFilePath);
            QStringList yearFolders = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            std::sort(yearFolders.begin(), yearFolders.end());

            foreach(const QString &yearFolder, yearFolders) {
                QDir yearDir(DiariesFilePath + yearFolder);
                QStringList monthFolders = yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                std::sort(monthFolders.begin(), monthFolders.end());

                foreach(const QString &monthFolder, monthFolders) {
                    QDir monthDir(DiariesFilePath + yearFolder + "/" + monthFolder);
                    QStringList dayFolders = monthDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    std::sort(dayFolders.begin(), dayFolders.end());

                    foreach(const QString &dayFolder, dayFolders) {
                        QString diaryDateString = yearFolder + "." + monthFolder + "." + dayFolder;
                        QString diaryPath = getDiaryFilePath(diaryDateString);

                        // Add to our list if it's not today's diary
                        if(diaryPath != todayDiaryPath) {
                            allDiaries.append(diaryPath);
                        }
                    }
                }
            }

            // Sort all diaries by date (since the path contains the date)
            std::sort(allDiaries.begin(), allDiaries.end());

            if(!allDiaries.isEmpty()) {
                // Get the most recent diary (last one in the sorted list)
                prevDiaryPath = allDiaries.last();
                foundPrevDiary = true;
            }
        }

        if(foundPrevDiary) {
            previous_DiaryFileName = prevDiaryPath;

            bool firstLineSetup = false;
            QStringList prevDiaryLines;

            // Read the previous diary file
            bool readSuccess = OperationsFiles::readEncryptedFileLines(
                previous_DiaryFileName, m_mainWindow->user_Key, prevDiaryLines);

            if (!readSuccess) {
                qDebug() << "Failed to read previous diary file: " << previous_DiaryFileName;
                return;
            }

            foreach(const QString& line, prevDiaryLines) {
                // Validate each line of content
                InputValidation::ValidationResult contentResult =
                    InputValidation::validateInput(line, InputValidation::InputType::DiaryContent, 100000);

                if (!contentResult.isValid) {
                    qWarning() << "Invalid content in previous diary entry during load: " << contentResult.errorMessage;
                    // Continue loading but log the issue
                }

                m_mainWindow->ui->DiaryTextDisplay->addItem(line); //add it to the text display
                previousDiaryLineCounter++; // counts how many lines of text are part of the previous diary view

                items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
                int lastindex = items.length() - 1; // use that list to get the last index of our listview diary text display
                if(!items.isEmpty()) //if items is not empty, otherwise the software would crash by returning an invalid index
                {
                    if(firstLineSetup == false) // setup the first line of the diary. It contains the diary file date.
                    {
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setTextAlignment(Qt::AlignCenter);
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled);
                        firstLineSetup = true;
                    }
                    if(line == Constants::Diary_TextBlockStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); //remove text block start marker
                        previousDiaryLineCounter--;
                        nextLine_isTextBlock = true; // this will remain true until we find a text block end marker
                    }
                    else if(line == Constants::Diary_TextBlockEnd)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); //remove text block end marker
                        previousDiaryLineCounter--; // remove 1 to previousdiarylinecounter because we removed a line
                        nextLine_isTextBlock = false; // the text block has been loaded completely, we will now add it to display
                        m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TextBlockStart); // add text block start marker
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true); // hide text block start marker
                        textblock = textblock.left(textblock.length() - 1); // remove the last characters in text block, that is the last unecessary \n
                        m_mainWindow->ui->DiaryTextDisplay->addItem(textblock); // add text block
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex+1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex+1)->flags() | Qt::ItemIsEditable); // make our new entry editable // +1 because we added 1 item since last calculating the last index
                        m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TextBlockEnd); // add text block end marker
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex+2)->setHidden(true); // hide text block end marker // +2 because we added 2 items since last calculating the last index
                        textblock.clear(); // clear the textblock
                        previousDiaryLineCounter = previousDiaryLineCounter + 3; // add 3 to previous... because we added 3 items

                        // Hide text block if it's part of a Task Manager section and setting is false
                        if (inTaskManagerSection && !m_mainWindow->setting_Diary_ShowTManLogs) {
                            m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                            m_mainWindow->ui->DiaryTextDisplay->item(lastindex+1)->setHidden(true);
                            m_mainWindow->ui->DiaryTextDisplay->item(lastindex+2)->setHidden(true);
                        }
                    }
                    else if(nextLine_isTextBlock == true)
                    {
                        textblock = textblock + line + "\n";
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); //remove the line from the display
                        previousDiaryLineCounter--; // remove 1 to previousdiarylinecounter because we removed a line
                    }
                    else if(line == Constants::Diary_Spacer)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled); //disable the spacers

                        // If we're in a Task Manager section, hiding ends at the spacer
                        if (inTaskManagerSection) {
                            inTaskManagerSection = false;
                        }
                    }
                    else if(line == Constants::Diary_TimeStampStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                        nextLine_isTimeStamp = true;
                    }
                    else if(nextLine_isTimeStamp == true)
                    {
                        font_TimeStamp.setFamily(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->text()); // get the value of the text that will have it's font changed
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFont(font_TimeStamp); // change the font of said text
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled); //disable the timestamps
                        nextLine_isTimeStamp = false;
                    }
                    else if(line == Constants::Diary_TaskManagerStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                        nextLine_isTaskManager = true;

                        // Start hiding Task Manager content if setting is false
                        if (!m_mainWindow->setting_Diary_ShowTManLogs) {
                            inTaskManagerSection = true;
                        }
                    }
                    else if(nextLine_isTaskManager == true)
                    {
                        font_TimeStamp.setFamily(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->text());
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFont(font_TimeStamp);
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled);
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setData(Qt::UserRole+1, true); // For coloring
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setData(Qt::UserRole+2, true); // Mark as Task Manager entry
                        nextLine_isTaskManager = false;

                        // Hide Task Manager timestamp if setting is false
                        if (!m_mainWindow->setting_Diary_ShowTManLogs) {
                            m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                        }
                    }
                    else if(line == Constants::Diary_ImageStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); // Remove the image start marker
                        nextLine_isImage = true;
                    }
                    else if(line == Constants::Diary_ImageEnd)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); // Remove the image end marker
                        nextLine_isImage = false;
                    }
                    else if(nextLine_isImage == true)
                    {
                        // Process the image filename(s) - could be single or multiple separated by |
                        QString imageData = line;
                        QStringList imageFilenames;

                        if (imageData.contains("|")) {
                            // Multiple images
                            imageFilenames = imageData.split("|", Qt::SkipEmptyParts);
                        } else {
                            // Single image
                            imageFilenames.append(imageData);
                        }

                        // Process all images in this entry
                        QStringList validImagePaths;
                        bool hasValidImages = false;

                        foreach(const QString& imageFilename, imageFilenames) {
                            QString imagePath = QDir::cleanPath(diaryDir + "/" + imageFilename);

                            // Try to load the image to verify it exists and is valid
                            QPixmap testPixmap = loadEncryptedImage(imagePath);
                            if (!testPixmap.isNull()) {
                                validImagePaths.append(imagePath);
                                hasValidImages = true;
                            } else {
                                qWarning() << "Failed to load image:" << imagePath;
                                markDiaryForCleanup = true;
                            }
                        }

                        if (hasValidImages) {
                            // Set up the item for image display
                            QString combinedPaths = validImagePaths.join("|");

                            // Create display text (empty since we removed captions)
                            setupImageItem(m_mainWindow->ui->DiaryTextDisplay->item(lastindex), combinedPaths, "");

                            // Calculate size based on number of images
                            int imageCount = validImagePaths.size();
                            if (imageCount == 1) {
                                // Single image - use original size calculation
                                QPixmap imagePixmap = loadEncryptedImage(validImagePaths.first());
                                if (!imagePixmap.isNull()) {
                                    QSize imageSize = imagePixmap.size();
                                    int itemHeight = imageSize.height() + 10; // Image + padding
                                    int itemWidth = qMax(imageSize.width() + 20, 300);
                                    m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setSizeHint(QSize(itemWidth, itemHeight));
                                }
                            } else {
                                // Multiple images - calculate grid size
                                const int THUMBNAIL_SIZE = 64;
                                const int MARGIN = 10;
                                const int SPACING = 5;

                                int availableWidth = m_mainWindow->ui->DiaryTextDisplay->viewport()->width() - (2 * MARGIN);
                                int imagesPerRow = availableWidth / (THUMBNAIL_SIZE + SPACING);
                                if (imagesPerRow < 1) imagesPerRow = 1;

                                int rows = (imageCount + imagesPerRow - 1) / imagesPerRow; // Ceiling division
                                int totalHeight = (rows * THUMBNAIL_SIZE) + ((rows - 1) * SPACING) + (2 * MARGIN);

                                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setSizeHint(QSize(availableWidth + (2 * MARGIN), totalHeight));
                            }

                            // Hide if part of Task Manager section and setting is false
                            if (inTaskManagerSection && !m_mainWindow->setting_Diary_ShowTManLogs) {
                                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                            }
                        } else {
                            // No valid images, remove the line and mark for cleanup
                            m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex);
                            markDiaryForCleanup = true;
                        }

                        // Note: nextLine_isImage remains true until we hit IMAGE_END
                    }
                    else // if line is a diary entry
                    {
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() | Qt::ItemIsEditable); //Make line editable.

                        // Hide entry if it's part of a Task Manager section and setting is false
                        if (inTaskManagerSection && !m_mainWindow->setting_Diary_ShowTManLogs) {
                            m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                        }
                    }
                }
            }

            // Extract previous diary date from file path
            QFileInfo prevFileInfo(previous_DiaryFileName);
            QString prevFileName = prevFileInfo.fileName();
            QString prevDateString = prevFileName.left(prevFileName.lastIndexOf('.'));

            // Parse dates to check if previous diary is yesterday's
            QDate prevDate(
                prevDateString.section('.', 0, 0).toInt(),
                prevDateString.section('.', 1, 1).toInt(),
                prevDateString.section('.', 2, 2).toInt()
                );

            // IF PREVIOUS DIARY IS NOT YESTERDAYS, DISABLE TEXT EDITING
            if (prevDate.addDays(1) == todayDate) {
                // Previous diary is yesterday's, do nothing (keep editable)
            } else {
                // Previous diary is not yesterday's
                foreach(QListWidgetItem *item, items) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                }
            }
        }
        else {
            previous_DiaryFileName = ""; // No previous diary found
        }
    }
    else {
        previous_DiaryFileName = ""; // Not loading today's diary
    }

    // Read the current diary file
    QStringList diaryLines;
    bool readSuccess = OperationsFiles::readEncryptedFileLines(
        DiaryFileName, m_mainWindow->user_Key, diaryLines);

    if (!readSuccess) {
        qDebug() << "Failed to read diary file: " << DiaryFileName;
        return;
    }

    cur_entriesNoSpacer = 0; // Reset the entries without spacer counter
    bool firstLineSetup = false;
    inTaskManagerSection = false; // Reset the Task Manager section flag

    foreach(const QString& line, diaryLines) {
        // Validate each line of content
        InputValidation::ValidationResult contentResult =
            InputValidation::validateInput(line, InputValidation::InputType::DiaryContent, 100000);

        if (!contentResult.isValid) {
            qWarning() << "Invalid content in diary entry during load: " << contentResult.errorMessage;
            // Continue loading but log the issue
        }

        m_mainWindow->ui->DiaryTextDisplay->addItem(line); //add it to the text display
        items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
        int lastindex = items.length() - 1; // use that list to get the last index of our listview diary text display
        if(!items.isEmpty()) //if items is not empty, otherwise the software would crash by returning an invalid index
        {
            if(firstLineSetup == false) // setup the first line of the diary. It contains the diary file date.
            {
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setTextAlignment(Qt::AlignCenter);
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled);
                if(DiaryFileName == todayDiaryPath) // if we are loading todays diary, save its datestamp to a variable and setfocus to text-input
                {
                    currentdiary_DateStamp = GetDiaryDateStamp(formattedTime);;
                    m_mainWindow->ui->DiaryTextInput->setFocus();
                }
                firstLineSetup = true;
            }

            if(line == Constants::Diary_TextBlockStart)
            {
                m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); //remove text block start marker
                nextLine_isTextBlock = true; // this will remain true until we find a text block end marker
            }
            else if(line == Constants::Diary_TextBlockEnd)
            {
                m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); //remove text block end marker
                nextLine_isTextBlock = false; // the text block has been loaded completely, we will now add it to display
                m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TextBlockStart); // add text block start marker
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true); // hide text block start marker
                textblock = textblock.left(textblock.length() - 1); // remove the last characters in text block, that is the last unecessary \n
                m_mainWindow->ui->DiaryTextDisplay->addItem(textblock); // add text block
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex+1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex+1)->flags() | Qt::ItemIsEditable); // make our new entry editable // +1 because we added 1 item since last calculating the last index
                m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_TextBlockEnd); // add text block end marker
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex+2)->setHidden(true); // hide text block end marker // +2 because we added 2 items since last calculating the last index
                textblock.clear(); // clear the textblock

                // Hide text block if it's part of a Task Manager section and setting is false
                if (inTaskManagerSection && !m_mainWindow->setting_Diary_ShowTManLogs) {
                    m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                    m_mainWindow->ui->DiaryTextDisplay->item(lastindex+1)->setHidden(true);
                    m_mainWindow->ui->DiaryTextDisplay->item(lastindex+2)->setHidden(true);
                }
            }
            else if(nextLine_isTextBlock == true)
            {
                textblock = textblock + line + "\n";
                m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); //remove the line from the display
            }
            else if (line == Constants::Diary_Spacer)
            {
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
                cur_entriesNoSpacer = 0; // Reset the entries without spacer counter
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled); //disable current line, preventing users from interacting with it

                // If we're in a Task Manager section, hiding ends at the spacer
                if (inTaskManagerSection) {
                    inTaskManagerSection = false;
                }
            }
            else if(line == Constants::Diary_TimeStampStart)
            {
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                nextLine_isTimeStamp = true;
            }
            else if(line == Constants::Diary_TaskManagerStart)
            {
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                nextLine_isTaskManager = true;

                // Start hiding Task Manager content if setting is false
                if (!m_mainWindow->setting_Diary_ShowTManLogs) {
                    inTaskManagerSection = true;
                }
            }
            else if(line == Constants::Diary_ImageStart)
            {
                m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); // Remove the image start marker
                nextLine_isImage = true;
            }
            else if(line == Constants::Diary_ImageEnd)
            {
                m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex); // Remove the image end marker
                nextLine_isImage = false;
            }
            else if(nextLine_isImage == true)
            {
                // Process the image filename(s) - could be single or multiple separated by |
                QString imageData = line;
                QStringList imageFilenames;

                if (imageData.contains("|")) {
                    // Multiple images
                    imageFilenames = imageData.split("|", Qt::SkipEmptyParts);
                } else {
                    // Single image
                    imageFilenames.append(imageData);
                }

                // Process all images in this entry
                QStringList validImagePaths;
                bool hasValidImages = false;

                foreach(const QString& imageFilename, imageFilenames) {
                    QString imagePath = QDir::cleanPath(diaryDir + "/" + imageFilename);

                    // Try to load the image to verify it exists and is valid
                    QPixmap testPixmap = loadEncryptedImage(imagePath);
                    if (!testPixmap.isNull()) {
                        validImagePaths.append(imagePath);
                        hasValidImages = true;
                    } else {
                        qWarning() << "Failed to load image:" << imagePath;
                        markDiaryForCleanup = true;
                    }
                }

                if (hasValidImages) {
                    // Set up the item for image display
                    QString combinedPaths = validImagePaths.join("|");

                    // Create display text (empty since we removed captions)
                    setupImageItem(m_mainWindow->ui->DiaryTextDisplay->item(lastindex), combinedPaths, "");

                    // Calculate size based on number of images
                    int imageCount = validImagePaths.size();
                    qDebug() << "DIARY-DEBUG7: About to calculate size for" << imageCount << "images";
                    if (imageCount == 1) {
                        // Single image - use original size calculation
                        QPixmap imagePixmap = loadEncryptedImage(validImagePaths.first());
                        if (!imagePixmap.isNull()) {
                            QSize imageSize = imagePixmap.size();
                            int itemHeight = imageSize.height() + 10; // Image + padding
                            int itemWidth = qMax(imageSize.width() + 20, 300);
                            m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setSizeHint(QSize(itemWidth, itemHeight));
                        }
                    } else {
                        // Multiple images - calculate grid size
                        const int THUMBNAIL_SIZE = 64;
                        const int MARGIN = 10;
                        const int SPACING = 5;

                        int availableWidth = m_mainWindow->ui->DiaryTextDisplay->viewport()->width() - (2 * MARGIN);
                        int imagesPerRow = availableWidth / (THUMBNAIL_SIZE + SPACING);
                        if (imagesPerRow < 1) imagesPerRow = 1;

                        int rows = (imageCount + imagesPerRow - 1) / imagesPerRow; // Ceiling division
                        int totalHeight = (rows * THUMBNAIL_SIZE) + ((rows - 1) * SPACING) + (2 * MARGIN);

                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setSizeHint(QSize(availableWidth + (2 * MARGIN), totalHeight));
                    }

                    // Hide if part of Task Manager section and setting is false
                    if (inTaskManagerSection && !m_mainWindow->setting_Diary_ShowTManLogs) {
                        m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                    }
                } else {
                    // No valid images, remove the line and mark for cleanup
                    m_mainWindow->ui->DiaryTextDisplay->takeItem(lastindex);
                    markDiaryForCleanup = true;
                }

                // Note: nextLine_isImage remains true until we hit IMAGE_END
            }
            else if(nextLine_isTimeStamp == false && nextLine_isTaskManager == false && nextLine_isImage == false) // NEXT LINE IS REGULAR DIARY
            {
                cur_entriesNoSpacer++; // add to the entries without spacer counter
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() | Qt::ItemIsEditable);

                // Hide entry if it's part of a Task Manager section and setting is false
                if (inTaskManagerSection && !m_mainWindow->setting_Diary_ShowTManLogs) {
                    m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                }
            }
            else if(nextLine_isTimeStamp == true)
            {
                font_TimeStamp.setFamily(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->text()); // get the value of the text that will have it's font changed
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFont(font_TimeStamp); // change the font of said text
                nextLine_isTimeStamp = false;
            }
            else if(nextLine_isTaskManager == true)
            {
                font_TimeStamp.setFamily(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->text());
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFont(font_TimeStamp);
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->flags() & ~Qt::ItemIsEnabled);
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setData(Qt::UserRole+1, true); // For coloring
                m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setData(Qt::UserRole+2, true); // Mark as Task Manager entry
                nextLine_isTaskManager = false;

                // Hide Task Manager timestamp if setting is false
                if (!m_mainWindow->setting_Diary_ShowTManLogs) {
                    m_mainWindow->ui->DiaryTextDisplay->item(lastindex)->setHidden(true);
                }
            }
        }
    }

    QList<QListWidgetItem*> templist = m_mainWindow->ui->DiaryTextDisplay->findItems(Constants::Diary_TimeStampStart, Qt::MatchStartsWith); // get a list of all timestamp locations
    if(!templist.isEmpty()) //if templist is not empty, otherwise the software would crash by returning an invalid index
    {
        QListWidgetItem* lastTimestampMarker = templist.last();
        int markerRow = m_mainWindow->ui->DiaryTextDisplay->row(lastTimestampMarker);
        QString temptext = m_mainWindow->ui->DiaryTextDisplay->item(markerRow + 1)->text();
        QString temptime = temptext.section(" at ",1,1); // remove the username from the text and keep only the time

        // Validate time format
        InputValidation::ValidationResult timeResult =
            InputValidation::validateInput(temptime, InputValidation::InputType::PlainText);

        if (timeResult.isValid) {
            QString temphours = temptime.section(":",0,0), tempminutes = temptime.section(":",1,1); // get the hours and minutes seperately
            lastTimeStamp_Hours = temphours.toInt(); // save the hours value of the last timestamp
            lastTimeStamp_Minutes = tempminutes.toInt(); // save the minutes value of the timestamp
        } else {
            qWarning() << "Invalid timestamp format detected: " << temptime;
            // Set default values
            lastTimeStamp_Hours = 0;
            lastTimeStamp_Minutes = 0;
        }

        foreach(QListWidgetItem *item, templist) // for each timestamp
        {
            m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->row(item)+1)->setFlags(item->flags() & ~Qt::ItemIsEnabled); // get the row of the timestamp marker, add 1 because the actual timestamp is next entry, make unselectable.
            m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->row(item)+1)->setData(Qt::UserRole+1, true);
        }

        // Extract the date from the diary file path
        QFileInfo fileInfo(DiaryFileName);
        QString fileName = fileInfo.fileName();
        QString datePart = fileName.left(fileName.lastIndexOf('.'));

        // Validate date format
        InputValidation::ValidationResult dateResult =
            InputValidation::validateInput(datePart, InputValidation::InputType::PlainText);
        if (!dateResult.isValid) {
            qWarning() << "Invalid date format in diary file path: " << dateResult.errorMessage;
        }

        if(datePart != formattedTime) // if we are not loading todays diary, will disable all lines
        {
            foreach(QListWidgetItem *item, getTextDisplayItems())
            {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled); //disable current line, preventing users from interacting with it
            }
        }
    }

    items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display. //Make a Widget List of all the text in the listview diary text display.
    if(m_mainWindow->ui->DiaryTextDisplay->item(items.length() -1)->text().contains(GetDiaryDateStamp(formattedTime))) //if last line is a datestamp, guarantee a timestamp on next entry
    {
        cur_entriesNoSpacer = 100000; // set absurd value to make sure that we will add a timestamp on our first entry, in this case, when an empty journal is loaded.
    }

    //add a spacer that is used only for one reason, being able to deselect the last entry of the display. IT IS NOT SAVED INTO OUR DIARY FILE
    m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_Spacer); //Add spacer
    items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display. //Make a Widget List of all the text in the listview diary text display.
    m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
    m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the spacer
    //------------------------------//

    // Extract the date from the diary file path
    fileInfo.setFile(DiaryFileName);
    fileName.clear();
    fileName = fileInfo.fileName();
    QString datePart = fileName.left(fileName.lastIndexOf('.'));

    if(datePart != formattedTime) // if we are not loading todays diary
    {
        cur_entriesNoSpacer = 100000; // set absurd value to make sure that we will add a timestamp on our first entry, say, when a new journal is created.
    }
    qDebug() << "DIARY-DEBUG6: === LoadDiary completed, checking all items ===";
    for (int i = 0; i < m_mainWindow->ui->DiaryTextDisplay->count(); i++) {
        QListWidgetItem* item = m_mainWindow->ui->DiaryTextDisplay->item(i);
        bool isImage = item->data(Qt::UserRole+3).toBool();
        if (isImage) {
            qDebug() << "DIARY-DEBUG6: Image item at row" << i;
            qDebug() << "DIARY-DEBUG6:  - Size hint:" << item->sizeHint();
            qDebug() << "DIARY-DEBUG6:  - Hidden:" << item->isHidden();
            qDebug() << "DIARY-DEBUG6:  - UserRole+4 (path):" << item->data(Qt::UserRole+4);
            qDebug() << "DIARY-DEBUG6:  - Item flags:" << item->flags();
            qDebug() << "DIARY-DEBUG6:  - Visual rect:" << m_mainWindow->ui->DiaryTextDisplay->visualItemRect(item);
        }
    }
    UpdateDisplayName();
    UpdateFontSize(m_mainWindow->setting_Diary_TextSize, true);
    QTimer::singleShot(50, this, &Operations_Diary::ScrollBottom); // scroll to bottom of diary text display // we use a very short delay otherwise it doesnt fully scroll down due to fontsize changes.
    // If we found broken image references, save the diary to clean them up
    if (markDiaryForCleanup) {
        qDebug() << "Cleaning up broken image references in diary:" << DiaryFileName;
        SaveDiary(DiaryFileName, DiaryFileName == previous_DiaryFileName);
        markDiaryForCleanup = false;
    }
}

void Operations_Diary::DeleteDiary(QString DiaryFileName)
{
    // Validate the diary file name
    InputValidation::ValidationResult fileResult =
        InputValidation::validateInput(DiaryFileName, InputValidation::InputType::FilePath);
    if (!fileResult.isValid) {
        qWarning() << "Invalid diary file path for delete operation:" << fileResult.errorMessage;
        QMessageBox::warning(m_mainWindow, "Invalid File Path",
                             "Cannot delete diary - invalid file path: " + fileResult.errorMessage);
        return;
    }

    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format appropriately
    QString todayDiaryPath = getDiaryFilePath(formattedTime);

    // Extract the diary date from the file path
    QFileInfo fileInfo(DiaryFileName);
    QString dayDirectoryPath = fileInfo.dir().absolutePath();

    // Extract date components from the path
    // The path format would be DiariesFilePath/YYYY/MM/DD/YYYY.MM.DD.txt
    QString fileName = fileInfo.fileName(); // Get YYYY.MM.DD.txt
    QString dateString = fileName.left(fileName.lastIndexOf('.')); // Remove .txt extension

    // Validate date string format
    InputValidation::ValidationResult dateResult =
        InputValidation::validateInput(dateString, InputValidation::InputType::PlainText);
    if (!dateResult.isValid) {
        qWarning() << "Invalid date format in file name:" << dateResult.errorMessage;
        QMessageBox::warning(m_mainWindow, "Invalid Date Format",
                             "Cannot delete diary - invalid date format in filename");
        return;
    }

    // Parse date components
    QString year = dateString.section('.',0,0);
    QString month = dateString.section('.',1,1);
    QString day = dateString.section('.',2,2);

    // Prepare hierarchy levels list for directory cleanup
    QStringList hierarchyLevels;
    hierarchyLevels << year << month << day;

    // Delete all files in the day directory
    QDir dayDir(dayDirectoryPath);
    if (!dayDir.exists()) {
        qWarning() << "Day directory does not exist:" << dayDirectoryPath;
        QMessageBox::warning(m_mainWindow, "Directory Error",
                             "The diary directory does not exist.");
        return;
    }

    // Get all files in the day directory
    QStringList filesInDay = dayDir.entryList(QDir::Files);
    bool allFilesDeleted = true;

    // Delete each file in the directory (no need for secure delete since content is encrypted)
    foreach(const QString& fileName, filesInDay) {
        QString filePath = QDir::cleanPath(dayDirectoryPath + "/" + fileName);
        QFile file(filePath);
        if (file.exists()) {
            bool fileDeleteSuccess = file.remove();
            if (!fileDeleteSuccess) {
                qWarning() << "Failed to delete file:" << filePath;
                allFilesDeleted = false;
            } else {
                qDebug() << "Successfully deleted file:" << filePath;
            }
        }
    }

    // Remove the now-empty day directory
    bool dirRemoveSuccess = dayDir.rmdir(dayDirectoryPath);
    if (!dirRemoveSuccess) {
        qWarning() << "Failed to remove day directory:" << dayDirectoryPath;
    } else {
        qDebug() << "Successfully removed day directory:" << dayDirectoryPath;
    }

    bool deleteSuccess = allFilesDeleted && dirRemoveSuccess;

    if (!deleteSuccess) {
        qWarning() << "Failed to completely delete diary and its contents: " << DiaryFileName;
        QMessageBox::warning(m_mainWindow, "Delete Error",
                             "Failed to completely delete the diary and its contents.");
        return;
    }

    // Clean up empty parent directories (month and year folders if they become empty)
    QString monthPath = QDir::cleanPath(DiariesFilePath + year + "/" + month);
    QDir monthDir(monthPath);
    if (monthDir.exists() && monthDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        bool monthRemoveSuccess = monthDir.rmdir(monthPath);
        if (monthRemoveSuccess) {
            qDebug() << "Removed empty month directory:" << monthPath;

            // Check if year directory is now empty
            QString yearPath = QDir::cleanPath(DiariesFilePath + year);
            QDir yearDir(yearPath);
            if (yearDir.exists() && yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
                bool yearRemoveSuccess = yearDir.rmdir(yearPath);
                if (yearRemoveSuccess) {
                    qDebug() << "Removed empty year directory:" << yearPath;
                }
            }
        }
    }

    if(DiaryFileName == current_DiaryFileName) // if we delete the currently loaded diary
    {
        // Check if this was the last diary for its year
        QDir yearDir(DiariesFilePath + year);
        bool isLastDiaryForYear = !yearDir.exists() || yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();

        if (isLastDiaryForYear) {
            // If we deleted the last diary for this year, we need to update the year list
            // and select the most recent year
            UpdateListYears();

            // Find the most recent year
            QStringList yearsList;
            for(int i = 0; i < m_mainWindow->ui->DiaryListYears->count(); i++) {
                yearsList.append(m_mainWindow->ui->DiaryListYears->itemText(i));
            }

            if (!yearsList.isEmpty()) {
                // Sort years in descending order to find the most recent
                std::sort(yearsList.begin(), yearsList.end(), std::greater<QString>());
                QString mostRecentYear = yearsList.first();

                // Set current year to the most recent
                int yearIndex = m_mainWindow->ui->DiaryListYears->findText(mostRecentYear);
                if (yearIndex >= 0) {
                    m_mainWindow->ui->DiaryListYears->setCurrentIndex(yearIndex);
                }
            }
        }
        DiaryLoader(); // execute diary loader
    }
    else if(DiaryFileName == previous_DiaryFileName && current_DiaryFileName == todayDiaryPath) //if we delete the previous diary and the current one is loaded
    {
        // Check if this was the last diary for its year
        QDir yearDir(DiariesFilePath + year);
        bool isLastDiaryForYear = !yearDir.exists() || yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();

        if (isLastDiaryForYear) {
            // If we deleted the last diary for this year, we need to update the year list
            UpdateListYears();
        }
        DiaryLoader(); // execute diary loader
    }
    else
    {
        // Check if this was the last diary for its year
        QDir yearDir(DiariesFilePath + year);
        bool isLastDiaryForYear = !yearDir.exists() || yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();

        if (isLastDiaryForYear) {
            // If we deleted the last diary for this year, we need to update the year list
            // and select the most recent year
            UpdateListYears();

            // Find the most recent year
            QStringList yearsList;
            for(int i = 0; i < m_mainWindow->ui->DiaryListYears->count(); i++) {
                yearsList.append(m_mainWindow->ui->DiaryListYears->itemText(i));
            }

            if (!yearsList.isEmpty()) {
                // Sort years in descending order to find the most recent
                std::sort(yearsList.begin(), yearsList.end(), std::greater<QString>());
                QString mostRecentYear = yearsList.first();

                // Set current year to the most recent
                int yearIndex = m_mainWindow->ui->DiaryListYears->findText(mostRecentYear);
                if (yearIndex >= 0) {
                    m_mainWindow->ui->DiaryListYears->setCurrentIndex(yearIndex);
                    // This will trigger the currentIndexChanged signal
                    // which will update the month and day lists
                } else {
                    UpdateDiarySorter(mostRecentYear, month, "NULL");
                }
            }
        } else {
            UpdateDiarySorter(year, month, "NULL");
        }
    }
}

void Operations_Diary::CreateNewDiary()
{
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format appropriately

    // Validate the date format
    InputValidation::ValidationResult dateResult =
        InputValidation::validateInput(formattedTime, InputValidation::InputType::PlainText);
    if (!dateResult.isValid) {
        qWarning() << "Invalid date format for new diary: " << dateResult.errorMessage;
        QMessageBox::warning(m_mainWindow, "Date Format Error",
                             "Cannot create diary - invalid date format");
        return;
    }

    // Create the directory structure for today's diary
    ensureDiaryDirectoryExists(formattedTime);

    QString diaryPath = getDiaryFilePath(formattedTime);
    if (diaryPath.isEmpty()) {
        // Handle the error case
        qDebug() << "Invalid diary path for date: " << formattedTime;
        QMessageBox::warning(m_mainWindow, "Path Error",
                             "Cannot create diary - failed to generate valid file path");
        return;
    }

    // Validate the file path
    InputValidation::ValidationResult pathResult =
        InputValidation::validateInput(diaryPath, InputValidation::InputType::FilePath);
    if (!pathResult.isValid) {
        qWarning() << "Invalid file path for new diary: " << pathResult.errorMessage;
        QMessageBox::warning(m_mainWindow, "Path Error",
                             "Cannot create diary - invalid file path: " + pathResult.errorMessage);
        return;
    }

    current_DiaryFileName = diaryPath; // Sets the current Diary to today's date

    // Prepare diary content
    QStringList diaryContent;
    currentdiary_DateStamp = GetDiaryDateStamp(formattedTime);
    diaryContent.append(currentdiary_DateStamp);

    // Use centralized file operations to write and encrypt the file
    bool writeSuccess = OperationsFiles::writeEncryptedFileLines(
        current_DiaryFileName, m_mainWindow->user_Key, diaryContent);

    if (!writeSuccess) {
        qDebug() << "Failed to create new diary file: " << current_DiaryFileName;
        QMessageBox::warning(m_mainWindow, "File Creation Error",
                             "Failed to create new diary file.");
        return;
    }

    cur_entriesNoSpacer = 100000; // set absurd value to make sure that we will add a timestamp on our first entry
    UpdateDiarySorter(formattedTime.section('.',0,0), formattedTime.section('.',1,1), formattedTime.section('.',2,2));
    m_mainWindow->ui->DiaryTextInput->setFocus();

    //add a spacer that is used only for one reason, being able to deselect the last entry of the display
    m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_Spacer); //Add spacer
    QList<QListWidgetItem *> items = getTextDisplayItems();
    m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
    m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the spacer

    m_mainWindow->ui->DiaryTextDisplay->scrollToBottom();
    DiaryLoader(); // Reload the diary to ensure everything is properly initialized
}

void Operations_Diary::DeleteEntry()
{
    qDebug() << "=== DeleteEntry called ===";

    QList<QListWidgetItem*> items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
    if(items.length() > 0 && m_mainWindow->ui->DiaryTextDisplay->currentRow() > 0) // if our display has at least 1 line and the current row is the first one. prevents SIGSEV crash
    {
        QListWidgetItem* currentItem = m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->currentRow());

        qDebug() << "Current row:" << m_mainWindow->ui->DiaryTextDisplay->currentRow();
        qDebug() << "Current item text:" << (currentItem ? currentItem->text() : "NULL");

        // Check if this is an image item and delete associated files
        if (currentItem && currentItem->data(Qt::UserRole+3).toBool()) {
            qDebug() << "Deleting image item";

            // This is an image item - delete the actual files
            QString diaryPath = current_DiaryFileName;
            if (m_mainWindow->ui->DiaryTextDisplay->currentRow() < previousDiaryLineCounter && previous_DiaryFileName != "") {
                diaryPath = previous_DiaryFileName; // Use previous diary path
            }

            QFileInfo diaryFileInfo(diaryPath);
            QString diaryDir = diaryFileInfo.dir().absolutePath();

            // Get image data and delete files
            bool isMultiImage = currentItem->data(Qt::UserRole+5).toBool();
            if (isMultiImage) {
                QStringList imagePaths = currentItem->data(Qt::UserRole+4).toStringList();
                QStringList imageFilenames;
                foreach(const QString& path, imagePaths) {
                    imageFilenames.append(QFileInfo(path).fileName());
                }
                deleteImageFiles(imageFilenames.join("|"), diaryDir);
                qDebug() << "Deleted multi-image files:" << imageFilenames;
            } else {
                QString imagePath = currentItem->data(Qt::UserRole+4).toString();
                QString imageFilename = QFileInfo(imagePath).fileName();
                deleteImageFiles(imageFilename, diaryDir);
                qDebug() << "Deleted single image file:" << imageFilename;
            }
        }

        if(m_mainWindow->ui->DiaryTextDisplay->currentRow() < previousDiaryLineCounter && previous_DiaryFileName != "") // if current row is within range of previous diary in the text display and a previous diary is loaded
        {
            qDebug() << "Deleting from previous diary";

            if (m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->currentRow())->text().contains("\n")) // if current row is a text block
            {
                m_mainWindow->ui->DiaryTextDisplay->takeItem(m_mainWindow->ui->DiaryTextDisplay->currentRow()-1); // remove the text block start marker
                m_mainWindow->ui->DiaryTextDisplay->takeItem(m_mainWindow->ui->DiaryTextDisplay->currentRow()+1); // remove the text block end marker
                previousDiaryLineCounter = previousDiaryLineCounter - 2; //update the previous diary line counter variable
            }
            m_mainWindow->ui->DiaryTextDisplay->takeItem(m_mainWindow->ui->DiaryTextDisplay->currentRow()); // remove the entry
            previousDiaryLineCounter--; //update the previous diary line counter variable
            remove_EmptyTimestamps(true); // scans the diary text display for timestamps that have no text attached to them, and removes them. true means we are editing the previous diary
            items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
            foreach(QListWidgetItem *item, items) // for each line of text
            {
                if(items.indexOf(item) >= previousDiaryLineCounter) // if it's index is bigger or equal to the length of our previous diary display
                {
                    items.removeAt(items.indexOf(item)); //remove the item from our list, we only want the previous diary in the item list
                }
            }
            if(items.length() == 1) // if previous diary length is 1(meaning its empty), delete the diary
            {
                DeleteDiary(previous_DiaryFileName);
            }
            else //otherwise, save it.
            {
                SaveDiary(previous_DiaryFileName, true); // Save the previous diary. true means are saving the previous diary
            }
        }
        else // if current row is in todays diary.
        {
            qDebug() << "Deleting from current diary";

            if (m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->currentRow())->text().contains("\n")) // if current row is a text block
            {
                m_mainWindow->ui->DiaryTextDisplay->takeItem(m_mainWindow->ui->DiaryTextDisplay->currentRow()-1); // remove the text block start marker
                m_mainWindow->ui->DiaryTextDisplay->takeItem(m_mainWindow->ui->DiaryTextDisplay->currentRow()+1); // remove the text block end marker
            }
            m_mainWindow->ui->DiaryTextDisplay->takeItem(m_mainWindow->ui->DiaryTextDisplay->currentRow()); // remove the entry altogether. Prevents empty lines from becoming spacers.
            prevent_onDiaryTextDisplay_itemChanged = true; // prevents infinite loop
            // REMOVES THE SPACER WIDGET USED IN DESELECTING THE LAST ENTRY. WE DONT WANT TO SAVE IT IN THE DIARY FILE
            QList<QListWidgetItem *> items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
            m_mainWindow->ui->DiaryTextDisplay->takeItem(items.length() - 1);
            //----------------------//
            remove_EmptyTimestamps(false); // scans the diary text display for timestamps that have no text attached to them, and removes them. false means we are editing todays diary
            items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
            items.remove(0, previousDiaryLineCounter); // remove the previous diary text. We want to check the length of the current diary
            if(items.length() == 2) // todays diary's length is 2, meaning it is empty
            {
                DeleteDiary(current_DiaryFileName); // delete it
            }
            else
            {
                SaveDiary(current_DiaryFileName, false); // Save the current diary. false means we are saving todays diary
            }
            //Add a spacer that is used only for one reason, being able to deselect the last entry of the display. IT IS NOT SAVED INTO OUR DIARY FILE
            m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_Spacer); //Add spacer
            items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
            m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
            m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the spacer
            //------------------------------//
            prevent_onDiaryTextDisplay_itemChanged = false; // prevents infinite loop
        }
    }

    qDebug() << "=== DeleteEntry completed ===";
}

// Diary Sorter

void Operations_Diary::UpdateDiarySorter(QString current_Year, QString current_Month, QString current_Day)
{
    //Initialize the Diary Sorting system. If a new file is created instead, initialization happens in the CreateNewDiary Function.
    currentdiary_Year = current_Year; // Set this BEFORE calling UpdateListYears()
    UpdateListYears(); // This will now use currentdiary_Year to set the selection

    UpdateListMonths(current_Year); // update month list based on current year
    currentdiary_Month = current_Month; //
    QList templist = m_mainWindow->ui->DiaryListMonths->findItems(Operations::ConvertMonthtoText(current_Month), Qt::MatchContains); // Make a list of all items that match the name of the current month (will only return one item because each month has a unique name)
    if (!templist.isEmpty()) {
        m_mainWindow->ui->DiaryListMonths->setCurrentItem(templist.at(0)); // Set the currently selected item in the months list to the month of the loaded diary. Since it can only find one match, the result will always be at index 0 of our templist
    }

    UpdateListDays(Operations::ConvertMonthtoText(current_Month)); // update the list of days based on current month
    if(current_Day != "NULL")
    {
        templist = m_mainWindow->ui->DiaryListDays->findItems(current_Day, Qt::MatchContains); // Make a list of all items that match the name of the current day (will only return one item because each day has a unique name)
        if (!templist.isEmpty()) {
            m_mainWindow->ui->DiaryListDays->setCurrentItem(templist.at(0)); // Set the currently selected item in the days list to the day of the currently selected diary. Since it can only find one match, the result will always be at index 0 of our templist
        }
    }
}

void Operations_Diary::UpdateListYears()
{
    QString year = ("");
    int textFound = -1; // The variable that will let us know if text has been found or not. -1 means none, anyother number represents the index at which it has been found

    // Get list of all year folders
    QDir baseDir(DiariesFilePath);
    QStringList yearFolders = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Clear the years list to rebuild it (except for the current selection)
    QString currentSelection = "";
    if (m_mainWindow->ui->DiaryListYears->count() > 0 && m_mainWindow->ui->DiaryListYears->currentIndex() >= 0) {
        currentSelection = m_mainWindow->ui->DiaryListYears->currentText();
    }

    // Check if currentdiary_Year is set - we want to prioritize this
    QString targetYear = currentdiary_Year.isEmpty() ? currentSelection : currentdiary_Year;

    // Clear the comboBox to rebuild it
    m_mainWindow->ui->DiaryListYears->clear();

    foreach(QString yearFolder, yearFolders) //for each year folder, update the year list if needed
    {
        // The folder name is already the year
        year = yearFolder;

        textFound = m_mainWindow->ui->DiaryListYears->findText(year); //returns -1 if it didnt find a match
        if(textFound == -1) //if it didnt find a match
        {
            m_mainWindow->ui->DiaryListYears->addItem(year); //add new year into the year list
        }
    }

    // Now set the selection to the target year (either currentdiary_Year or the previous selection)
    if (!targetYear.isEmpty()) {
        int index = m_mainWindow->ui->DiaryListYears->findText(targetYear);
        if (index >= 0) {
            m_mainWindow->ui->DiaryListYears->setCurrentIndex(index);
        } else if (m_mainWindow->ui->DiaryListYears->count() > 0) {
            // If target year is not found, select the most recent year
            QStringList years;
            for (int i = 0; i < m_mainWindow->ui->DiaryListYears->count(); i++) {
                years.append(m_mainWindow->ui->DiaryListYears->itemText(i));
            }
            std::sort(years.begin(), years.end(), std::greater<QString>());
            if (!years.isEmpty()) {
                int mostRecentIndex = m_mainWindow->ui->DiaryListYears->findText(years.first());
                if (mostRecentIndex >= 0) {
                    m_mainWindow->ui->DiaryListYears->setCurrentIndex(mostRecentIndex);
                }
            }
        }
    } else if (m_mainWindow->ui->DiaryListYears->count() > 0) {
        // If no target, select most recent year
        QStringList years;
        for (int i = 0; i < m_mainWindow->ui->DiaryListYears->count(); i++) {
            years.append(m_mainWindow->ui->DiaryListYears->itemText(i));
        }
        std::sort(years.begin(), years.end(), std::greater<QString>());
        if (!years.isEmpty()) {
            int mostRecentIndex = m_mainWindow->ui->DiaryListYears->findText(years.first());
            if (mostRecentIndex >= 0) {
                m_mainWindow->ui->DiaryListYears->setCurrentIndex(mostRecentIndex);
            }
        }
    }

    textFound = m_mainWindow->ui->DiaryListYears->findText(""); // Look for empty index, there will be one if it this is the first time this code runs
    if(textFound != -1)// if an empty index was found, remove it
    {
        m_mainWindow->ui->DiaryListYears->removeItem(textFound); // Remove empty index
    }
}

void Operations_Diary::UpdateListMonths(QString current_Year)
{
    QString month = ("");
    currentyear_DiaryList.clear(); // resets the variable

    // Check if year folder exists
    QDir yearDir(DiariesFilePath + current_Year);
    if (!yearDir.exists()) {
        return;
    }

    // Get list of all month folders for the selected year
    QStringList monthFolders = yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // For each month folder, find all day folders and construct date strings
    foreach(QString monthFolder, monthFolders)
    {
        QDir monthDir(DiariesFilePath + current_Year + "/" + monthFolder);
        QStringList dayFolders = monthDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        foreach(QString dayFolder, dayFolders)
        {
            // Construct the date string in format YYYY.MM.DD
            QString dateString = current_Year + "." + monthFolder + "." + dayFolder;
            currentyear_DiaryList.append(dateString); // Add to the list of diaries for this year
        }
    }

    m_mainWindow->ui->DiaryListMonths->clear(); // Clear the monthlist before repopulating

    // Set of months that have diary entries (to avoid duplicates)
    QSet<QString> processedMonths;

    foreach(QString dateString, currentyear_DiaryList)
    {
        month = dateString.section('.',1,1); // Get the month part

        // Convert numeric month to text
        QString monthText = Operations::ConvertMonthtoText(month);

        if (!processedMonths.contains(monthText)) {
            m_mainWindow->ui->DiaryListMonths->addItem(monthText);
            processedMonths.insert(monthText);
        }
    }
}

void Operations_Diary::UpdateListDays(QString current_Month)
{
    QString month = (""), day = ("");
    currentmonth_DiaryList.clear();

    // Convert text month to numeric format
    month = Operations::ConvertMonthtoInt(current_Month);

    // Filter diaries for the selected month
    foreach(QString dateString, currentyear_DiaryList)
    {
        if(dateString.section('.',1,1) == month)
        {
            currentmonth_DiaryList.append(dateString);
        }
    }

    m_mainWindow->ui->DiaryListDays->clear(); //Clear the days list before repopulating

    foreach(QString dateString, currentmonth_DiaryList)
    {
        // Extract the day part
        day = dateString.section('.',2,2);

        // Skip if not exactly two digits
        if (day.length() != 2 || !day.at(0).isDigit() || !day.at(1).isDigit()) {
            continue;
        }

        // Parse the date
        QDate currentFileDate(
            dateString.section('.',0,0).toInt(),
            dateString.section('.',1,1).toInt(),
            dateString.section('.',2,2).toInt()
            );

        QList itemFound = m_mainWindow->ui->DiaryListDays->findItems(day, Qt::MatchContains);
        if(itemFound.isEmpty())
        {
            m_mainWindow->ui->DiaryListDays->addItem(day + " - " + Operations::GetDayOfWeek(currentFileDate));
        }
    }
}

void Operations_Diary::DiaryLoader()
{
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format approprietly

    // Sets the current Diary to today's date
    QString diaryPath = getDiaryFilePath(formattedTime);
    if (diaryPath.isEmpty()) {
        // Handle the error case
        qDebug() << "Invalid diary path for date: " << formattedTime;
        return;
    }
    current_DiaryFileName = diaryPath;

    if(QFileInfo::exists(current_DiaryFileName)) // If today's diary file already exists, load it
    {
        LoadDiary(current_DiaryFileName);
        UpdateDiarySorter(formattedTime.section('.',0,0), formattedTime.section('.',1,1), formattedTime.section('.',2,2));
    }
    else
    {
        // In the hierarchical structure, we need to scan the year folders first
        QDir baseDir(DiariesFilePath);
        QStringList yearFolders = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        if(yearFolders.isEmpty()) // If no year folders exist, create a new diary
        {
            CreateNewDiary();
        }
        else // If year folders exist, find the most recent diary
        {
            // Sort year folders numerically
            std::sort(yearFolders.begin(), yearFolders.end());

            QString latestYear = yearFolders.last();
            QDir yearDir(DiariesFilePath + latestYear);
            QStringList monthFolders = yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

            if(monthFolders.isEmpty())
            {
                CreateNewDiary();
                return;
            }

            // Sort month folders numerically
            std::sort(monthFolders.begin(), monthFolders.end());

            QString latestMonth = monthFolders.last();
            QDir monthDir(DiariesFilePath + latestYear + "/" + latestMonth);
            QStringList dayFolders = monthDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

            if(dayFolders.isEmpty())
            {
                CreateNewDiary();
                return;
            }

            // Sort day folders numerically
            std::sort(dayFolders.begin(), dayFolders.end());

            QString latestDay = dayFolders.last();

            // Construct the date string in the format YYYY.MM.DD
            QString latestDateString = latestYear + "." + latestMonth + "." + latestDay;
            QString latestDiaryPath = getDiaryFilePath(latestDateString);

            if(QFileInfo::exists(latestDiaryPath))
            {
                LoadDiary(latestDiaryPath);
                UpdateDiarySorter(latestYear, latestMonth, latestDay);
            }
            else
            {
                // If the most recent diary file doesn't exist (unusual), create a new one
                CreateNewDiary();
            }
        }
    }

    // Scroll to the bottom of display when opening the software
    QList<QListWidgetItem *> items = getTextDisplayItems();
    m_mainWindow->ui->DiaryTextDisplay->setCurrentRow(items.length() - 1);
    UpdateDelegate();
}

//Context Menu

void Operations_Diary::OpenEditor() // Open the entry editor
{
    m_mainWindow->ui->DiaryTextDisplay->editItem(m_mainWindow->ui->DiaryTextDisplay->currentItem()); // open text editor
}

void Operations_Diary::DeleteDiaryFromListDays()
{
    // Extract the date from the current diary file name
    QFileInfo fileInfo(current_DiaryFileName);
    QString fileName = fileInfo.fileName();
    QString dateString = fileName.left(fileName.lastIndexOf('.')); // Remove .txt extension

    // Validate date string format
    InputValidation::ValidationResult result =
        InputValidation::validateInput(dateString, InputValidation::InputType::PlainText);
    if (!result.isValid) {
        qWarning() << "Invalid date format in file name:" << result.errorMessage;
        return;
    }

    // Get formatted date stamp
    QString formattedDate = GetDiaryDateStamp(dateString);

    // Show confirmation dialog
    QMessageBox::StandardButton reply = QMessageBox::question(
        m_mainWindow,
        "Confirm Deletion",
        "Are you sure you want to delete the diary entry for " + formattedDate + "?",
        QMessageBox::Yes | QMessageBox::No
        );

    // Only delete if user confirms
    if (reply == QMessageBox::Yes) {
        DeleteDiary(current_DiaryFileName);
    }
}

void Operations_Diary::CopyToClipboard()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_mainWindow->ui->DiaryTextDisplay->currentItem()->text());
}

void Operations_Diary::showContextMenu_TextDisplay(const QPoint &pos)
{
    if(!m_mainWindow->ui->DiaryTextDisplay->selectedItems().isEmpty()) // if we requested a context menu and we currently have an item selected
    {
        QListWidgetItem* selectedItem = m_mainWindow->ui->DiaryTextDisplay->selectedItems().first();

        // Store the context menu position for click detection
        m_lastContextMenuPos = pos;

        // Check if this is an image item
        bool isImageItem = selectedItem->data(Qt::UserRole+3).toBool();

        if (isImageItem) {
            // ENABLE CLICK DETECTION FOR MULTI-IMAGE SUPPORT
            m_clickedImageIndex = calculateClickedImageIndex(selectedItem, pos);

            bool isMultiImage = selectedItem->data(Qt::UserRole+5).toBool();

            // Show image context menu with click detection
            QMenu contextMenu(tr("Image menu"), m_mainWindow->ui->DiaryTextDisplay);

            if (isMultiImage) {
                // Multi-image - show different options based on click detection
                if (m_clickedImageIndex >= 0) {
                    // Specific image was clicked
                    QStringList imagePaths = selectedItem->data(Qt::UserRole+4).toStringList();
                    QString imageFileName = QFileInfo(imagePaths[m_clickedImageIndex]).fileName();

                    // Show specific image options
                    QAction *action1 = contextMenu.addAction(QString("Open: %1").arg(imageFileName));
                    QAction *action2 = contextMenu.addAction(QString("Copy Path: %1").arg(imageFileName));
                    QAction *action3 = contextMenu.addAction(QString("Delete: %1").arg(imageFileName));
                    contextMenu.addSeparator();
                    QAction *action4 = contextMenu.addAction("Open Any Image...");
                    QAction *action5 = contextMenu.addAction("Copy All Paths");
                    QAction *action6 = contextMenu.addAction("Delete All Images");

                    connect(action1, &QAction::triggered, this, [this, selectedItem]() {
                        handleSpecificImageClick(selectedItem, m_clickedImageIndex);
                    });
                    connect(action2, &QAction::triggered, this, [this, selectedItem]() {
                        copySpecificImagePath(selectedItem, m_clickedImageIndex);
                    });
                    connect(action3, &QAction::triggered, this, [this, selectedItem]() {
                        deleteSpecificImage(selectedItem, m_clickedImageIndex);
                    });
                    connect(action4, &QAction::triggered, this, [this, selectedItem]() {
                        handleImageClick(selectedItem); // This will show the selection dialog
                    });
                    connect(action5, &QAction::triggered, this, [this, selectedItem]() {
                        copyAllImagePaths(selectedItem);
                    });
                    connect(action6, &QAction::triggered, this, &Operations_Diary::DeleteEntry);
                } else {
                    // No specific image clicked, show general multi-image options
                    QAction *action1 = contextMenu.addAction("Select Image to Open...");
                    QAction *action2 = contextMenu.addAction("Copy All Paths");
                    QAction *action3 = contextMenu.addAction("Delete All Images");

                    connect(action1, &QAction::triggered, this, [this, selectedItem]() {
                        handleImageClick(selectedItem); // This will show the selection dialog
                    });
                    connect(action2, &QAction::triggered, this, [this, selectedItem]() {
                        copyAllImagePaths(selectedItem);
                    });
                    connect(action3, &QAction::triggered, this, &Operations_Diary::DeleteEntry);
                }
            } else {
                // Single image - always show single image options
                QString imagePath = selectedItem->data(Qt::UserRole+4).toString();
                QString imageFileName = QFileInfo(imagePath).fileName();

                QAction *action1 = contextMenu.addAction(QString("Open: %1").arg(imageFileName));
                QAction *action2 = contextMenu.addAction(QString("Copy Path: %1").arg(imageFileName));
                QAction *action3 = contextMenu.addAction(QString("Delete: %1").arg(imageFileName));

                connect(action1, &QAction::triggered, this, [this, selectedItem]() {
                    handleImageClick(selectedItem);
                });
                connect(action2, &QAction::triggered, this, [this, selectedItem]() {
                    QString imagePath = selectedItem->data(Qt::UserRole+4).toString();
                    QClipboard *clipboard = QGuiApplication::clipboard();
                    clipboard->setText(imagePath);
                });
                connect(action3, &QAction::triggered, this, &Operations_Diary::DeleteEntry);
            }

            // Use the widget's mapToGlobal with the original pos
            QPoint globalPos = m_mainWindow->ui->DiaryTextDisplay->mapToGlobal(pos);
            contextMenu.exec(globalPos);
            return;
        }

        // Regular text context menu (existing code)
        QMenu contextMenu(tr("Context menu"), m_mainWindow->ui->DiaryTextDisplay);
        contextMenu.installEventFilter(m_mainWindow);
        contextMenu.setAttribute(Qt::WA_DeleteOnClose);
        //create context menu actions
        QAction action1("Delete", m_mainWindow->ui->DiaryTextDisplay);
        QAction action2("Modify", m_mainWindow->ui->DiaryTextDisplay);
        QAction action3("Copy", m_mainWindow->ui->DiaryTextDisplay);
        //connect context menu signals
        connect(&action1, SIGNAL(triggered()), this, SLOT(DeleteEntry()));
        connect(&action2, SIGNAL(triggered()), this, SLOT(OpenEditor()));
        connect(&action3, SIGNAL(triggered()), this, SLOT(CopyToClipboard()));
        //build context menu
        contextMenu.addAction(&action3);
        if(m_mainWindow->setting_Diary_CanEditRecent == true)
        {
            contextMenu.addAction(&action2);
        }
        contextMenu.addAction(&action1);
        //Adjust context menu position
        QPoint newpos = pos;
        newpos.setX(pos.x() +175);
        newpos.setY(pos.y() +35);
        // Get the selected item and its index
        selectedItem = m_mainWindow->ui->DiaryTextDisplay->selectedItems().first();
        int selectedIndex = m_mainWindow->ui->DiaryTextDisplay->row(selectedItem);

        // Use the index in FindLastTimeStampType
        if (FindLastTimeStampType(selectedIndex) == Constants::Diary_TaskManagerStart)
        {
            action2.setEnabled(false);
        }
        contextMenu.exec(m_mainWindow->mapToGlobal(newpos));
    }
}

void Operations_Diary::showContextMenu_ListDays(const QPoint &pos)
{
    if(!m_mainWindow->ui->DiaryListDays->selectedItems().isEmpty()) // if we requested a context menu and we currently have an item selected
    {
        //Initialize context menu
        QMenu contextMenu(tr("Context menu"), m_mainWindow->ui->DiaryListDays);
        contextMenu.installEventFilter(m_mainWindow);
        contextMenu.setAttribute(Qt::WA_DeleteOnClose);
        //create context menu actions
        QAction action1("Delete", m_mainWindow->ui->DiaryListDays);
        //connect context menu signals
        connect(&action1, SIGNAL(triggered()), this, SLOT(DeleteDiaryFromListDays()));
        //build context menu
        contextMenu.addAction(&action1);
        //Use the actual position where user clicked
        QPoint globalPos = m_mainWindow->ui->DiaryListDays->mapToGlobal(pos);
        contextMenu.exec(globalPos);
    }
}

//Misc

void Operations_Diary::UpdateDelegate()
{
    CombinedDelegate *delegate = new CombinedDelegate(m_mainWindow);
    connect(delegate, &CombinedDelegate::TextModificationsMade, m_mainWindow->ui->DiaryTextDisplay, &custom_QListWidget::TextWasEdited);
    delegate->setColorLength(m_mainWindow->user_Displayname.length());  // Color first 5 characters
    delegate->setTextColor(QColor(m_mainWindow->user_nameColor));  // Use red color for the text
    m_mainWindow->ui->DiaryTextDisplay->setItemDelegate(delegate);
}

void Operations_Diary::ScrollBottom()
{
    m_mainWindow->ui->DiaryTextDisplay->scrollToBottom();
}

void Operations_Diary::UpdateDisplayName()
{
    QList<QListWidgetItem*>  templist = m_mainWindow->ui->DiaryTextDisplay->findItems(Constants::Diary_TimeStampStart, Qt::MatchStartsWith); // get a list of all timestamp locations
    if(!templist.isEmpty()) //if templist is not empty, otherwise the software would crash by returning an invalid index
    {
        foreach(QListWidgetItem *item, templist) // for each timestamp
        {
            if(m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->row(item)+1)->text().section(" at ",0,0) != m_mainWindow->user_Displayname) // if the display name of current time stamp isnt the same as the current display name
            {
                QString timestamp_Time = m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->row(item)+1)->text().section(" at ",1,1); // save the timestamp minus the display name and " at "
                m_mainWindow->ui->DiaryTextDisplay->item(m_mainWindow->ui->DiaryTextDisplay->row(item)+1)->setText(m_mainWindow->user_Displayname + " at " + timestamp_Time); // set text to new display name + " at " + saved timestamp
            }
        }
    }
}

void Operations_Diary::remove_EmptyTimestamps(bool previousDiary)
{
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format approprietly
    currentdiary_DateStamp = GetDiaryDateStamp(formattedTime);
    QList<QListWidgetItem *> items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display. // make a list of all our text display lines
    qDebug() << "we are attempting to remove empty timestamps";
    if(!items.isEmpty()) //if items is not empty, otherwise the software would crash by returning an invalid index
    {
        foreach(QListWidgetItem *item, items)
        {
            if(!previousDiary) // if we are editing todays diary
            {
                if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                {
                    qDebug() << "index of item: " << items.indexOf(item);
                    qDebug() << "items length: " << items.length()-2;
                    //qDebug() << "item at +2: " << items.at(items.indexOf(item)+2)->text();
                }
                if(items.indexOf(item)+2 <= items.length()-2 && items.at(items.indexOf(item)+2)->text() == Constants::Diary_Spacer && items.indexOf(item)-1 > 0) //if we find a timestamp && compare index of item to check with list length(prevents crashes)
                {
                    if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)-1); // remove the spacer that comes before the timestamp
                        items.removeAt(items.indexOf(item)-1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)+1); //remove the timestamp
                        items.removeAt(items.indexOf(item)+1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)); //remove the timestamp marker
                        items.removeAt(items.indexOf(item)); //update our itemlist
                    }

                }
                else if(items.indexOf(item) == items.length()-2) // if we find a timestamp that is at the very end of our diarydisplay
                {
                    if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)-1); // remove the spacer that comes before the timestamp
                        items.removeAt(items.indexOf(item)-1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)+1); //remove the timestamp
                        items.removeAt(items.indexOf(item)+1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)); //remove the timestamp marker
                        items.removeAt(items.indexOf(item)); //update our itemlist
                        cur_entriesNoSpacer = 100000; //absurb value to guarantee that we will add a spacer. Since we remove the last time stamp, it is guaranteed that we need one. unless you'd travel back in time ;)
                    }
                }
            }
            else // if we are not editing todays diary but rather the previous one
            {
                if(items.indexOf(item)+2 <= items.length()-2 && items.at(items.indexOf(item)+2)->text() == Constants::Diary_Spacer && items.indexOf(item)-1 > 0) //if we find a timestamp && compare index of item to check with list length(prevents crashes)
                {
                    if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)-1); // remove the spacer that comes before the timestamp
                        items.removeAt(items.indexOf(item)-1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)+1); //remove the timestamp
                        items.removeAt(items.indexOf(item)+1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)); //remove the timestamp marker
                        items.removeAt(items.indexOf(item)); //update our itemlist
                        previousDiaryLineCounter = previousDiaryLineCounter -3; //update the previousDiaryLineCounter variable because we removed 3 lines from the previous diary
                    }
                }
                else if(items.indexOf(item)+2 <= items.length()-2 && items.at(items.indexOf(item)+2)->text() == currentdiary_DateStamp) // if we find a spacer that is at the end of the previous diary display. uses datestamp detection
                {
                    if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                    {
                        qDebug() << "DEBUGITEM: " << item->text();
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)-1); // remove the spacer that comes before the timestamp
                        items.removeAt(items.indexOf(item)-1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)+1); //remove the timestamp
                        items.removeAt(items.indexOf(item)+1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)); //remove the timestamp marker
                        items.removeAt(items.indexOf(item)); //update our itemlist
                        previousDiaryLineCounter = previousDiaryLineCounter -3; //update the previousDiaryLineCounter variable because we removed 3 lines from the previous diary
                        cur_entriesNoSpacer = 100000; //absurb value to guarantee that we will add a spacer. Since we remove the last time stamp, it is guaranteed that we need one. unless you'd travel back in time ;)
                    }
                }
                else if(items.indexOf(item) == items.length()-3) // if we find a timestamp that is at the very end of our diarydisplay
                {
                    if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                    {
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)-1); // remove the spacer that comes before the timestamp
                        items.removeAt(items.indexOf(item)-1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)+1); //remove the timestamp
                        items.removeAt(items.indexOf(item)+1); //update our itemlist
                        m_mainWindow->ui->DiaryTextDisplay->takeItem(items.indexOf(item)); //remove the timestamp marker
                        items.removeAt(items.indexOf(item)); //update our itemlist
                        previousDiaryLineCounter = previousDiaryLineCounter -3; //update the previousDiaryLineCounter variable because we removed 3 lines from the previous diary
                        cur_entriesNoSpacer = 100000; //absurb value to guarantee that we will add a spacer. Since we remove the last time stamp, it is guaranteed that we need one. unless you'd travel back in time ;)
                    }
                }
                else if(item->text() == Constants::Diary_TimeStampStart || item->text() == Constants::Diary_TaskManagerStart)
                {
                    //qDebug() << items.at(items.indexOf(item)+2)->text();
                    //qDebug() << currentdiary_DateStamp;
                }
            }
        }
    }
}

void Operations_Diary::DeleteEmptyCurrentDayDiary()
{
    // Get today's date
    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("yyyy.MM.dd");

    // Get the path for today's diary file
    QString todayDiaryPath = getDiaryFilePath(formattedTime);
    if (todayDiaryPath.isEmpty()) {
        qDebug() << "Invalid diary path for current date:" << formattedTime;
        return;
    }

    // Check if today's diary exists
    if (!QFileInfo::exists(todayDiaryPath)) {
        qDebug() << "Today's diary does not exist, nothing to delete";
        return;
    }

    // Load today's diary to ensure we're working with the latest content
    current_DiaryFileName = todayDiaryPath;
    LoadDiary(todayDiaryPath);

    // Now check if the diary is empty
    QList<QListWidgetItem *> items = getTextDisplayItems();
    int currentDayItemsLength = items.length() - previousDiaryLineCounter;
    qDebug() << "Current day item length:" << currentDayItemsLength;

    // If current diary is empty (only has the date header), delete it
    if (currentDayItemsLength <= 2) {
        DeleteDiary(current_DiaryFileName);
    }
}

// Image handling

QString Operations_Diary::generateImageFilename(const QString& originalExtension, const QString& diaryDir)
{
    QDateTime currentDateTime = QDateTime::currentDateTime();

    // Format: 2025.6.1.13.18.45 (year.month.day.hour.minute.second)
    // Use single digits for month/day/hour/minute/second when possible
    QString baseFilename = QString("%1.%2.%3.%4.%5.%6")
                               .arg(currentDateTime.date().year())
                               .arg(currentDateTime.date().month())
                               .arg(currentDateTime.date().day())
                               .arg(currentDateTime.time().hour())
                               .arg(currentDateTime.time().minute())
                               .arg(currentDateTime.time().second());

    QString targetExtension = originalExtension.toLower();

    // Ensure we have a valid extension
    if (targetExtension.isEmpty()) {
        targetExtension = "png";
    }

    QString filename = baseFilename + "." + targetExtension;
    QString fullPath = QDir::cleanPath(diaryDir + "/" + filename);

    // Check for duplicates and add suffix if needed
    int suffix = 1;
    while (QFileInfo::exists(fullPath)) {
        filename = QString("%1(%2).%3").arg(baseFilename).arg(suffix).arg(targetExtension);
        fullPath = QDir::cleanPath(diaryDir + "/" + filename);
        suffix++;
    }

    return filename;
}

bool Operations_Diary::saveEncryptedImage(const QString& sourcePath, const QString& targetPath)
{
    // Validate input paths
    InputValidation::ValidationResult sourceResult =
        InputValidation::validateInput(sourcePath, InputValidation::InputType::ExternalFilePath);
    InputValidation::ValidationResult targetResult =
        InputValidation::validateInput(targetPath, InputValidation::InputType::FilePath);

    if (!sourceResult.isValid || !targetResult.isValid) {
        qWarning() << "Invalid file paths for image encryption";
        return false;
    }

    // Check if source file exists
    if (!QFileInfo::exists(sourcePath)) {
        qWarning() << "Source image file does not exist:" << sourcePath;
        return false;
    }

    // Read the binary image data
    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open source image file:" << sourcePath;
        return false;
    }

    QByteArray imageData = sourceFile.readAll();
    sourceFile.close();

    // Encrypt using binary encryption
    QByteArray encryptedData = CryptoUtils::Encryption_EncryptBArray(
        m_mainWindow->user_Key, imageData, m_mainWindow->user_Username);

    if (encryptedData.isEmpty()) {
        qWarning() << "Binary encryption failed for image:" << sourcePath;
        return false;
    }

    // Write encrypted data to target file
    QFile targetFile(targetPath);
    if (!targetFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open target file for writing:" << targetPath;
        return false;
    }

    qint64 bytesWritten = targetFile.write(encryptedData);
    targetFile.close();

    return (bytesWritten == encryptedData.size());
}

QPixmap Operations_Diary::generateThumbnail(const QString& imagePath, int maxSize)
{
    QPixmap originalPixmap(imagePath);
    if (originalPixmap.isNull()) {
        return QPixmap();
    }

    // Scale the pixmap to fit within maxSize while preserving aspect ratio
    QPixmap thumbnail = originalPixmap.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Create a square pixmap with padding
    QPixmap squareThumbnail(maxSize, maxSize);
    squareThumbnail.fill(Qt::transparent);

    QPainter painter(&squareThumbnail);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the thumbnail
    int x = (maxSize - thumbnail.width()) / 2;
    int y = (maxSize - thumbnail.height()) / 2;
    painter.drawPixmap(x, y, thumbnail);

    return squareThumbnail;
}

QSize Operations_Diary::getImageDimensions(const QString& imagePath)
{
    QImageReader reader(imagePath);
    return reader.size();
}

bool Operations_Diary::isImageOversized(const QSize& imageSize, int maxWidth, int maxHeight)
{
    return imageSize.width() > maxWidth || imageSize.height() > maxHeight;
}

void Operations_Diary::processAndAddImages(const QStringList& imagePaths, bool forceThumbnails)
{
    if (imagePaths.isEmpty()) {
        return;
    }

    // Get today's diary directory
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString formattedDate = currentDateTime.toString("yyyy.MM.dd");
    QString diaryPath = getDiaryFilePath(formattedDate);

    if (diaryPath.isEmpty()) {
        QMessageBox::warning(m_mainWindow, "Error", "Cannot determine diary file path.");
        return;
    }

    QFileInfo diaryFileInfo(diaryPath);
    QString diaryDir = diaryFileInfo.dir().absolutePath();

    // Ensure the diary directory exists
    ensureDiaryDirectoryExists(formattedDate);

    QStringList processedImages;
    QStringList failedImages;

    foreach(const QString& imagePath, imagePaths) {
        try {
            // Validate image file
            if (!QFileInfo::exists(imagePath)) {
                failedImages.append(imagePath + " (file not found)");
                continue;
            }

            // Get image dimensions
            QSize imageSize = getImageDimensions(imagePath);
            if (imageSize.isEmpty()) {
                failedImages.append(imagePath + " (invalid image)");
                continue;
            }

            // Force thumbnails for grouped images, or use existing logic for single images
            bool needsThumbnail = (imagePaths.size() > 1) || forceThumbnails ||
                                  isImageOversized(imageSize, MAX_IMAGE_WIDTH, MAX_IMAGE_HEIGHT);

            // Generate filename
            QString originalExtension = QFileInfo(imagePath).suffix();
            QString imageFilename = generateImageFilename(originalExtension, diaryDir);
            QString encryptedImagePath = QDir::cleanPath(diaryDir + "/" + imageFilename);

            // Encrypt and save the original image
            bool saveSuccess = saveEncryptedImage(imagePath, encryptedImagePath);
            if (!saveSuccess) {
                failedImages.append(imagePath + " (encryption failed)");
                continue;
            }

            QString displayFilename = imageFilename;

            // Generate thumbnail if needed
            if (needsThumbnail) {
                QPixmap thumbnail = generateThumbnail(imagePath, THUMBNAIL_SIZE);
                if (!thumbnail.isNull()) {
                    QString thumbnailFilename = QFileInfo(imageFilename).completeBaseName() + ".thumb";
                    QString thumbnailPath = QDir::cleanPath(diaryDir + "/" + thumbnailFilename);

                    // Save thumbnail as temporary file, then encrypt it
                    QString tempThumbnailPath = QDir::tempPath() + "/" + thumbnailFilename + ".png";
                    if (thumbnail.save(tempThumbnailPath, "PNG")) {
                        bool thumbnailSaveSuccess = saveEncryptedImage(tempThumbnailPath, thumbnailPath);
                        QFile::remove(tempThumbnailPath); // Clean up temp file

                        if (thumbnailSaveSuccess) {
                            displayFilename = thumbnailFilename;
                        }
                    }
                }
            }

            processedImages.append(displayFilename);

        } catch (const std::exception& e) {
            failedImages.append(imagePath + QString(" (error: %1)").arg(e.what()));
        } catch (...) {
            failedImages.append(imagePath + " (unknown error)");
        }
    }

    // Clean up temporary clipboard files
    foreach(const QString& imagePath, imagePaths) {
        if (imagePath.contains("clipboard_image_")) {
            QFile::remove(imagePath);
        }
    }

    // Add images to diary if any were processed successfully
    if (!processedImages.isEmpty()) {
        // Get current diary file path
        QString todayDiaryPath = getDiaryFilePath(formattedDate);

        // Check if we should group images
        bool shouldGroup = checkShouldGroupImages(todayDiaryPath);

        // Add images to diary
        addImagesToCurrentDiary(processedImages, todayDiaryPath, shouldGroup);

        // If user was typing, preserve their text and add it after images
        QString currentText = m_mainWindow->ui->DiaryTextInput->toPlainText().trimmed();
        if (!currentText.isEmpty()) {
            // Add the user's text as a regular entry
            InputNewEntry(todayDiaryPath);
        } else {
            // Clear the text input since we're not using it
            m_mainWindow->ui->DiaryTextInput->clear();
        }
    }

    // Show error message if any images failed
    if (!failedImages.isEmpty()) {
        QString errorMessage = "Failed to process the following images:\n\n";
        errorMessage += failedImages.join("\n");
        QMessageBox::warning(m_mainWindow, "Image Processing Errors", errorMessage);
    }
}

bool Operations_Diary::checkShouldGroupImages(const QString& diaryFilePath)
{
    qDebug() << "=== checkShouldGroupImages called ===";

    // Check timestamp condition first
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString formattedTime = currentDateTime.toString("hh:mm");
    int currentTimeMinutes = formattedTime.section(":",0,0).toInt() * 60 + formattedTime.section(":",1,1).toInt();

    bool needsTimestamp = true;
    if (lastTimeStamp_Hours * 60 + lastTimeStamp_Minutes > currentTimeMinutes - m_mainWindow->setting_Diary_TStampTimer &&
        cur_entriesNoSpacer < m_mainWindow->setting_Diary_TStampCounter) {
        needsTimestamp = false;
        qDebug() << "No timestamp needed - can potentially group";
    } else {
        qDebug() << "Timestamp needed - cannot group";
        return false; // Don't group if we need a new timestamp
    }

    // Check if last visible entry in the display is an image
    QList<QListWidgetItem*> items = getTextDisplayItems();
    if (items.isEmpty()) {
        qDebug() << "No items in display";
        return false;
    }

    // Look for the last non-spacer item (skip the last item which is always a spacer)
    for (int i = items.size() - 2; i >= 0; i--) { // -2 to skip the spacer
        QListWidgetItem* item = items[i];

        // Skip hidden items and spacers
        if (item->isHidden() || item->text() == Constants::Diary_Spacer) {
            continue;
        }

        // Check if this item is an image
        bool isImageItem = item->data(Qt::UserRole+3).toBool();
        qDebug() << "Last visible item at index" << i << "is image:" << isImageItem;

        if (isImageItem) {
            qDebug() << "Found image item - can group!";
            return true; // Can group with this image
        } else {
            qDebug() << "Found non-image item - cannot group";
            return false; // Hit a non-image item, can't group
        }
    }

    qDebug() << "No suitable item found for grouping";
    return false;
}

bool Operations_Diary::openImageWithViewer(const QString& imagePath)
{
    // Validate the image path
    InputValidation::ValidationResult result =
        InputValidation::validateInput(imagePath, InputValidation::InputType::FilePath);
    if (!result.isValid) {
        qWarning() << "Invalid image path for viewer:" << result.errorMessage;
        return false;
    }

    // Check if the encrypted image file exists
    if (!QFileInfo::exists(imagePath)) {
        QMessageBox::warning(m_mainWindow, "Error", "Image file not found: " + imagePath);
        return false;
    }

    try {
        // Load and decrypt the image
        QPixmap imagePixmap = loadEncryptedImage(imagePath);

        if (imagePixmap.isNull()) {
            QMessageBox::warning(m_mainWindow, "Error", "Failed to load image: " + imagePath);
            return false;
        }

        // Create new image viewer instance (non-modal, multiple instances allowed)
        ImageViewer* viewer = new ImageViewer(m_mainWindow);

        // Load the decrypted image into the viewer
        QString imageTitle = QFileInfo(imagePath).fileName();
        bool loadSuccess = viewer->loadImage(imagePixmap, imageTitle);

        if (!loadSuccess) {
            delete viewer;
            QMessageBox::warning(m_mainWindow, "Error", "Failed to display image in viewer.");
            return false;
        }

        // Show the viewer (non-modal)
        viewer->show();
        viewer->raise();
        viewer->activateWindow();

        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception in openImageWithViewer:" << e.what();
        QMessageBox::warning(m_mainWindow, "Error", "An error occurred while opening the image viewer.");
        return false;
    } catch (...) {
        qWarning() << "Unknown exception in openImageWithViewer";
        QMessageBox::warning(m_mainWindow, "Error", "An unknown error occurred while opening the image viewer.");
        return false;
    }
}

void Operations_Diary::cleanupBrokenImageReferences()
{
    // This will be called during diary loading to remove broken image references
    // Implementation depends on how we detect broken references during loading
}

bool Operations_Diary::loadAndDisplayImage(const QString& imagePath, const QString& imageFilename)
{
    // Check if the encrypted image file exists
    if (!QFileInfo::exists(imagePath)) {
        qWarning() << "Encrypted image file not found:" << imagePath;
        return false;
    }

    try {
        // Try to load the encrypted image
        QPixmap imagePixmap = loadEncryptedImage(imagePath);

        if (imagePixmap.isNull()) {
            qWarning() << "Failed to load encrypted image:" << imagePath;
            return false;
        }

        // Get the current item and set it up to display the image
        QList<QListWidgetItem*> items = getTextDisplayItems();
        if (!items.isEmpty()) {
            QListWidgetItem* item = items.last();

            // Create display text (shorter since we're showing the image)
            QString displayText = getImageDisplayText(imageFilename, imagePixmap.size());

            // Set up as image item FIRST
            setupImageItem(item, imagePath, displayText);

            // Calculate the size needed for the image + text
            QSize imageSize = imagePixmap.size();
            int itemHeight = imageSize.height() + 30; // Image + 30px for text and padding
            int itemWidth = qMax(imageSize.width() + 20, 300); // Minimum 300px width

            // Set the size hint AFTER setting up the image item
            item->setSizeHint(QSize(itemWidth, itemHeight));

            qDebug() << "Set size hint for image item:" << QSize(itemWidth, itemHeight);
        }

        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception while loading image:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception while loading image";
        return false;
    }
}

QPixmap Operations_Diary::loadEncryptedImage(const QString& encryptedImagePath)
{
    qDebug() << "=== loadEncryptedImage called for:" << encryptedImagePath;

    // Check if file exists first
    if (!QFileInfo::exists(encryptedImagePath)) {
        qWarning() << "Encrypted image file does not exist:" << encryptedImagePath;
        return QPixmap();
    }

    // Read the encrypted binary data
    QFile encryptedFile(encryptedImagePath);
    if (!encryptedFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open encrypted image file:" << encryptedImagePath;
        return QPixmap();
    }

    QByteArray encryptedData = encryptedFile.readAll();
    encryptedFile.close();

    qDebug() << "Read encrypted data, size:" << encryptedData.size();

    // Decrypt using binary decryption
    QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(
        m_mainWindow->user_Key, encryptedData);

    if (decryptedData.isEmpty()) {
        qWarning() << "Binary decryption failed for image:" << encryptedImagePath;
        return QPixmap();
    }

    qDebug() << "Decrypted data size:" << decryptedData.size();

    // Load image directly from binary data
    QPixmap pixmap;
    bool loadSuccess = pixmap.loadFromData(decryptedData);

    qDebug() << "Load from data success:" << loadSuccess << "Pixmap size:" << pixmap.size();

    return pixmap;
}

QString Operations_Diary::getImageDisplayText(const QString& imageFilename, const QSize& imageSize)
{
    // Create a shorter display text since we're showing the actual image
    QFileInfo fileInfo(imageFilename);
    QString baseName = fileInfo.completeBaseName(); // Get full name without extension
    QString extension = fileInfo.suffix().toUpper();

    // Check if this is a thumbnail
    bool isThumbnail = imageFilename.endsWith(".thumb");

    QString sizeText = QString("%1×%2").arg(imageSize.width()).arg(imageSize.height());

    if (isThumbnail) {
        return QString("%1 %2 (Click to view full image)").arg(baseName).arg(sizeText);
    } else {
        return QString("%1.%2 %3").arg(baseName).arg(extension).arg(sizeText);
    }
}

void Operations_Diary::setupImageItem(QListWidgetItem* item, const QString& imagePath, const QString& displayText)
{
    // Set up the item as a clickable image
    item->setText(""); // Remove caption completely
    item->setData(Qt::UserRole+3, true); // Mark as image

    // Check if this is multiple images (contains |)
    if (imagePath.contains("|")) {
        QStringList imagePaths = imagePath.split("|", Qt::SkipEmptyParts);
        item->setData(Qt::UserRole+4, imagePaths); // Store as QStringList
        item->setData(Qt::UserRole+5, true); // Mark as multi-image
    } else {
        item->setData(Qt::UserRole+4, imagePath); // Store single path as QString
        item->setData(Qt::UserRole+5, false); // Mark as single image
    }

    item->setFlags(item->flags() & ~Qt::ItemIsEditable); // Make non-editable but still selectable

    // No background color - use theme default
    // No font changes needed since we have no text
}

void Operations_Diary::handleImageClick(QListWidgetItem* item)
{
    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        return; // Not an image item
    }

    bool isMultiImage = item->data(Qt::UserRole+5).toBool();

    if (isMultiImage) {
        // For multi-image items, check if we have a stored clicked index from context menu
        if (m_clickedImageIndex >= 0) {
            QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
            if (m_clickedImageIndex < imagePaths.size()) {
                // Get the original image path instead of thumbnail
                QString diaryPath = current_DiaryFileName;
                QFileInfo diaryFileInfo(diaryPath);
                QString diaryDir = diaryFileInfo.dir().absolutePath();

                QString originalPath = getOriginalImagePath(imagePaths[m_clickedImageIndex], diaryDir);
                openImageWithViewer(originalPath);
                return;
            }
        }

        // Fallback to selection dialog if no valid clicked index
        QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();

        // Get original paths and their filenames for display
        QString diaryPath = current_DiaryFileName;
        QFileInfo diaryFileInfo(diaryPath);
        QString diaryDir = diaryFileInfo.dir().absolutePath();

        QStringList originalPaths = getOriginalImagePaths(imagePaths, diaryDir);
        QStringList imageFilenames;
        foreach(const QString& path, originalPaths) {
            imageFilenames.append(QFileInfo(path).fileName());
        }

        bool ok;
        QString selectedFilename = QInputDialog::getItem(
            m_mainWindow,
            "Select Image to Open",
            "Multiple images found. Select which image to open:",
            imageFilenames,
            0,
            false,
            &ok
            );

        if (ok && !selectedFilename.isEmpty()) {
            int index = imageFilenames.indexOf(selectedFilename);
            if (index >= 0 && index < originalPaths.size()) {
                openImageWithViewer(originalPaths[index]);
            }
        }
    } else {
        // Single image
        QString imagePath = item->data(Qt::UserRole+4).toString();

        if (imagePath.isEmpty()) {
            QMessageBox::warning(m_mainWindow, "Error", "Image path not found.");
            return;
        }

        // Get the original image path instead of thumbnail
        QString diaryPath = current_DiaryFileName;
        QFileInfo diaryFileInfo(diaryPath);
        QString diaryDir = diaryFileInfo.dir().absolutePath();

        QString originalPath = getOriginalImagePath(imagePath, diaryDir);
        openImageWithViewer(originalPath);
    }
}

void Operations_Diary::addImagesToCurrentDiary(const QStringList& imageFilenames, const QString& diaryFilePath, bool shouldGroup)
{
    qDebug() << "=== addImagesToCurrentDiary called ===";
    qDebug() << "imageFilenames:" << imageFilenames;
    qDebug() << "shouldGroup:" << shouldGroup;

    // Ensure the diary exists or create it
    if (!QFileInfo::exists(diaryFilePath)) {
        // Create new diary with just the date header
        QDateTime currentDateTime = QDateTime::currentDateTime();
        QString formattedDate = currentDateTime.toString("yyyy.MM.dd");
        QStringList diaryContent;
        diaryContent.append(GetDiaryDateStamp(formattedDate));

        bool writeSuccess = OperationsFiles::writeEncryptedFileLines(
            diaryFilePath, m_mainWindow->user_Key, diaryContent);

        if (!writeSuccess) {
            qWarning() << "Failed to create diary file for images";
            return;
        }
        qDebug() << "Created new diary file";
    }

    // Read current diary content
    QStringList diaryContent;
    bool readSuccess = OperationsFiles::readEncryptedFileLines(
        diaryFilePath, m_mainWindow->user_Key, diaryContent);

    if (!readSuccess) {
        qWarning() << "Failed to read diary file for adding images";
        return;
    }

    qDebug() << "DIARY-DEBUG9: Read diary content from file:";
    for (int i = 0; i < diaryContent.size(); i++) {
        qDebug() << "DIARY-DEBUG9: Line" << i << ":" << diaryContent[i];
    }

    qDebug() << "Read diary content, lines:" << diaryContent.size();

    if (shouldGroup && !diaryContent.isEmpty()) {
        qDebug() << "Attempting to group with existing image";

        // Find the last image entry and modify it
        for (int i = diaryContent.size() - 1; i >= 0; i--) {
            if (diaryContent[i] == Constants::Diary_ImageStart) {
                qDebug() << "Found IMAGE_START at line" << i;

                // Found start of last image entry, find the corresponding end
                for (int j = i + 1; j < diaryContent.size(); j++) {
                    if (diaryContent[j] == Constants::Diary_ImageEnd) {
                        qDebug() << "Found IMAGE_END at line" << j;
                        qDebug() << "Current image data:" << diaryContent[j - 1];

                        // Found the image data line (between start and end)
                        QString existingImages = diaryContent[j - 1];
                        QString newImages = imageFilenames.join("|");

                        // Combine existing and new images
                        diaryContent[j - 1] = existingImages + "|" + newImages;
                        qDebug() << "Updated image data:" << diaryContent[j - 1];

                        // Write the updated content back
                        bool writeSuccess = OperationsFiles::writeEncryptedFileLines(
                            diaryFilePath, m_mainWindow->user_Key, diaryContent);

                        if (writeSuccess) {
                            qDebug() << "Successfully grouped images in diary file";

                            // Reload the diary if it's currently displayed
                            if (current_DiaryFileName == diaryFilePath) {
                                qDebug() << "Reloading diary to show grouped images";
                                LoadDiary(diaryFilePath);
                            }
                        } else {
                            qWarning() << "Failed to write grouped images to diary";
                        }
                        return;
                    }
                }
                break; // Found start but no end, something's wrong
            }
        }
        qDebug() << "Could not find existing image entry to group with";
    }

    qDebug() << "Adding as new image entry (not grouping)";

    // Not grouping or no existing image found - add as new entry

    // Check if we need to add a timestamp
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString formattedTime = currentDateTime.toString("hh:mm");
    int currentTimeMinutes = formattedTime.section(":",0,0).toInt() * 60 + formattedTime.section(":",1,1).toInt();

    // Check if we need a timestamp (reuse existing logic)
    bool needsTimestamp = true;
    if (lastTimeStamp_Hours * 60 + lastTimeStamp_Minutes > currentTimeMinutes - m_mainWindow->setting_Diary_TStampTimer &&
        cur_entriesNoSpacer < m_mainWindow->setting_Diary_TStampCounter) {
        needsTimestamp = false;
    }

    if (needsTimestamp) {
        qDebug() << "Adding timestamp for new image entry";

        // Add timestamp
        QString timestamp = m_mainWindow->user_Displayname + " at " + formattedTime;
        diaryContent.append(Constants::Diary_Spacer);
        diaryContent.append(Constants::Diary_TimeStampStart);
        diaryContent.append(timestamp);

        // Update timestamp tracking
        lastTimeStamp_Hours = formattedTime.section(":",0,0).toInt();
        lastTimeStamp_Minutes = formattedTime.section(":",1,1).toInt();
        cur_entriesNoSpacer = 0;
    } else {
        qDebug() << "No timestamp needed for new image entry";
    }

    // Add image(s) as single or grouped entry
    diaryContent.append(Constants::Diary_ImageStart);
    if (imageFilenames.size() == 1) {
        diaryContent.append(imageFilenames.first());
        qDebug() << "Added single image:" << imageFilenames.first();
    } else {
        diaryContent.append(imageFilenames.join("|"));
        qDebug() << "Added grouped images:" << imageFilenames.join("|");
    }
    diaryContent.append(Constants::Diary_ImageEnd);

    cur_entriesNoSpacer++;

    qDebug() << "DIARY-DEBUG8: About to write diary content:";
    for (int i = 0; i < diaryContent.size(); i++) {
        qDebug() << "DIARY-DEBUG8: Line" << i << ":" << diaryContent[i];
    }

    // Write the updated content back to the diary
    bool writeSuccess = OperationsFiles::writeEncryptedFileLines(
        diaryFilePath, m_mainWindow->user_Key, diaryContent);

    if (!writeSuccess) {
        qWarning() << "Failed to write images to diary file";
        return;
    }

    qDebug() << "Successfully wrote image entry to diary";

    // Reload the diary if it's currently displayed
    if (current_DiaryFileName == diaryFilePath) {
        qDebug() << "Reloading diary to show new images";
        LoadDiary(diaryFilePath);
    }

    qDebug() << "=== addImagesToCurrentDiary completed ===";
}

void Operations_Diary::deleteImageFiles(const QString& imageData, const QString& diaryDir)
{
    QStringList imageFilenames;

    if (imageData.contains("|")) {
        // Multiple images
        imageFilenames = imageData.split("|", Qt::SkipEmptyParts);
    } else {
        // Single image
        imageFilenames.append(imageData);
    }

    foreach(const QString& imageFilename, imageFilenames) {
        QString imagePath = QDir::cleanPath(diaryDir + "/" + imageFilename);

        if (isThumbnailPath(imageFilename)) {
            // This is a thumbnail - delete both thumbnail and original

            // Delete the thumbnail
            if (QFileInfo::exists(imagePath)) {
                bool deleteSuccess = OperationsFiles::secureDelete(imagePath, 3, false);
                if (deleteSuccess) {
                    qDebug() << "Successfully deleted thumbnail file:" << imagePath;
                } else {
                    qWarning() << "Failed to delete thumbnail file:" << imagePath;
                }
            }

            // Find and delete the original image
            QString originalPath = getOriginalImagePath(imagePath, diaryDir);
            if (originalPath != imagePath && QFileInfo::exists(originalPath)) {
                bool originalDeleteSuccess = OperationsFiles::secureDelete(originalPath, 3, false);
                if (originalDeleteSuccess) {
                    qDebug() << "Successfully deleted original file:" << originalPath;
                } else {
                    qWarning() << "Failed to delete original file:" << originalPath;
                }
            }
        } else {
            // This is an original image - delete both original and any associated thumbnail

            // Delete the original image
            if (QFileInfo::exists(imagePath)) {
                bool deleteSuccess = OperationsFiles::secureDelete(imagePath, 3, false);
                if (deleteSuccess) {
                    qDebug() << "Successfully deleted original image file:" << imagePath;
                } else {
                    qWarning() << "Failed to delete original image file:" << imagePath;
                }
            }

            // Check if there's a corresponding thumbnail and delete it
            QString thumbnailFilename = QFileInfo(imageFilename).completeBaseName() + ".thumb";
            QString thumbnailPath = QDir::cleanPath(diaryDir + "/" + thumbnailFilename);
            if (QFileInfo::exists(thumbnailPath)) {
                bool thumbnailDeleteSuccess = OperationsFiles::secureDelete(thumbnailPath, 3, false);
                if (thumbnailDeleteSuccess) {
                    qDebug() << "Successfully deleted thumbnail file:" << thumbnailPath;
                } else {
                    qWarning() << "Failed to delete thumbnail file:" << thumbnailPath;
                }
            }
        }
    }
}

int Operations_Diary::calculateClickedImageIndex(QListWidgetItem* item, const QPoint& clickPos)
{
    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        return -1; // Not an image item
    }

    bool isMultiImage = item->data(Qt::UserRole+5).toBool();
    if (!isMultiImage) {
        return 0; // Single image, always index 0
    }

    QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
    int imageCount = qMin(imagePaths.size(), 10); // Cap at 10 images

    if (imageCount <= 1) {
        return 0;
    }

    // Get the item's rect in widget coordinates
    QRect itemRect = m_mainWindow->ui->DiaryTextDisplay->visualItemRect(item);

    // Convert click position to relative coordinates within the item
    QPoint relativePos = clickPos - itemRect.topLeft();

    // Grid layout constants (must match paintMultipleImages)
    const int THUMBNAIL_SIZE = 64;
    const int MARGIN = 10;
    const int SPACING = 5;

    // Calculate available width and images per row
    int availableWidth = itemRect.width() - (2 * MARGIN);
    int imagesPerRow = availableWidth / (THUMBNAIL_SIZE + SPACING);
    if (imagesPerRow < 1) imagesPerRow = 1;

    // Account for margins
    int clickX = relativePos.x() - MARGIN;
    int clickY = relativePos.y() - MARGIN;

    // Check if click is outside the image area
    if (clickX < 0 || clickY < 0) {
        return -1;
    }

    // Calculate grid position
    int col = clickX / (THUMBNAIL_SIZE + SPACING);
    int row = clickY / (THUMBNAIL_SIZE + SPACING);

    // Convert to image index
    int imageIndex = (row * imagesPerRow) + col;

    // Validate bounds
    if (imageIndex < 0 || imageIndex >= imageCount) {
        return -1;
    }

    // Additional check: ensure click is within the actual thumbnail area
    int thumbnailStartX = col * (THUMBNAIL_SIZE + SPACING);
    int thumbnailStartY = row * (THUMBNAIL_SIZE + SPACING);
    int thumbnailEndX = thumbnailStartX + THUMBNAIL_SIZE;
    int thumbnailEndY = thumbnailStartY + THUMBNAIL_SIZE;

    if (clickX >= thumbnailStartX && clickX <= thumbnailEndX &&
        clickY >= thumbnailStartY && clickY <= thumbnailEndY) {
        return imageIndex;
    }

    return -1; // Click was in spacing/margin area
}

void Operations_Diary::deleteSpecificImage(QListWidgetItem* item, int imageIndex)
{
    qDebug() << "=== deleteSpecificImage called ===";
    qDebug() << "imageIndex:" << imageIndex;

    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        qDebug() << "Not an image item, returning";
        return; // Not an image item
    }

    bool isMultiImage = item->data(Qt::UserRole+5).toBool();
    qDebug() << "isMultiImage:" << isMultiImage;

    if (!isMultiImage) {
        qDebug() << "Single image - calling DeleteEntry()";
        // Single image - just delete the whole entry
        DeleteEntry();
        return;
    }

    QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
    qDebug() << "Original imagePaths:" << imagePaths;

    if (imageIndex < 0 || imageIndex >= imagePaths.size()) {
        qDebug() << "Invalid imageIndex, returning";
        return; // Invalid index
    }

    // Get the diary directory for file deletion
    QString diaryPath = current_DiaryFileName;
    QFileInfo diaryFileInfo(diaryPath);
    QString diaryDir = diaryFileInfo.dir().absolutePath();
    qDebug() << "diaryDir:" << diaryDir;

    // Get the image filename to delete
    QString imagePathToDelete = imagePaths[imageIndex];
    QString imageFilename = QFileInfo(imagePathToDelete).fileName();
    qDebug() << "Deleting imageFilename:" << imageFilename;

    // Delete the actual file
    deleteImageFiles(imageFilename, diaryDir);

    // Remove from the list
    imagePaths.removeAt(imageIndex);
    qDebug() << "Remaining imagePaths after removal:" << imagePaths;

    if (imagePaths.size() == 1) {
        qDebug() << "Converting to single image";
        // Only one image left - convert back to single image
        item->setData(Qt::UserRole+4, imagePaths.first());
        item->setData(Qt::UserRole+5, false); // Mark as single image

        // Update size hint for single image
        QPixmap imagePixmap = loadEncryptedImage(imagePaths.first());
        if (!imagePixmap.isNull()) {
            QSize imageSize = imagePixmap.size();
            int itemHeight = imageSize.height() + 10;
            int itemWidth = qMax(imageSize.width() + 20, 300);
            item->setSizeHint(QSize(itemWidth, itemHeight));
            qDebug() << "Set single image size hint:" << QSize(itemWidth, itemHeight);
        } else {
            qDebug() << "Failed to load image for size calculation";
        }
    } else if (imagePaths.isEmpty()) {
        qDebug() << "No images left - calling DeleteEntry()";
        // No images left - delete the whole entry
        DeleteEntry();
        return;
    } else {
        qDebug() << "Multiple images still remain - updating data";
        // Multiple images still remain - update the data
        item->setData(Qt::UserRole+4, imagePaths);

        // Recalculate size hint for remaining images
        int imageCount = imagePaths.size();
        const int THUMBNAIL_SIZE = 64;
        const int MARGIN = 10;
        const int SPACING = 5;

        int availableWidth = m_mainWindow->ui->DiaryTextDisplay->viewport()->width() - (2 * MARGIN);
        int imagesPerRow = availableWidth / (THUMBNAIL_SIZE + SPACING);
        if (imagesPerRow < 1) imagesPerRow = 1;

        int rows = (imageCount + imagesPerRow - 1) / imagesPerRow;
        int totalHeight = (rows * THUMBNAIL_SIZE) + ((rows - 1) * SPACING) + (2 * MARGIN);

        item->setSizeHint(QSize(availableWidth + (2 * MARGIN), totalHeight));
        qDebug() << "Set multi-image size hint:" << QSize(availableWidth + (2 * MARGIN), totalHeight);
    }

    qDebug() << "About to call updateImageEntryInDiary";
    // Update the diary file
    updateImageEntryInDiary(item);

    qDebug() << "About to force repaint";
    // Force repaint
    m_mainWindow->ui->DiaryTextDisplay->update();

    qDebug() << "=== deleteSpecificImage completed ===";
}

QString Operations_Diary::removeImageFromData(const QString& imageData, int indexToRemove)
{
    QStringList imageFilenames;

    if (imageData.contains("|")) {
        imageFilenames = imageData.split("|", Qt::SkipEmptyParts);
    } else {
        imageFilenames.append(imageData);
    }

    if (indexToRemove >= 0 && indexToRemove < imageFilenames.size()) {
        imageFilenames.removeAt(indexToRemove);
    }

    if (imageFilenames.isEmpty()) {
        return "";
    } else if (imageFilenames.size() == 1) {
        return imageFilenames.first();
    } else {
        return imageFilenames.join("|");
    }
}

void Operations_Diary::updateImageEntryInDiary(QListWidgetItem* item)
{
    qDebug() << "=== updateImageEntryInDiary called ===";

    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        qDebug() << "Not an image item, returning";
        return; // Not an image item
    }

    // Read current diary content
    QStringList diaryContent;
    bool readSuccess = OperationsFiles::readEncryptedFileLines(
        current_DiaryFileName, m_mainWindow->user_Key, diaryContent);

    if (!readSuccess) {
        qWarning() << "Failed to read diary file for image update";
        return;
    }

    qDebug() << "Read diary content, lines:" << diaryContent.size();

    // Get the item row for debugging
    int itemRow = m_mainWindow->ui->DiaryTextDisplay->row(item);
    qDebug() << "Item row in display:" << itemRow;

    // Get updated image data
    bool isMultiImage = item->data(Qt::UserRole+5).toBool();
    QString newImageData;

    if (isMultiImage) {
        QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
        QStringList imageFilenames;
        foreach(const QString& path, imagePaths) {
            imageFilenames.append(QFileInfo(path).fileName());
        }
        newImageData = imageFilenames.join("|");
        qDebug() << "Multi-image data to save:" << newImageData;
    } else {
        QString imagePath = item->data(Qt::UserRole+4).toString();
        newImageData = QFileInfo(imagePath).fileName();
        qDebug() << "Single-image data to save:" << newImageData;
    }

    // Find and update the image entry in diary content
    bool foundAndUpdated = false;
    for (int i = 0; i < diaryContent.size() - 2; i++) { // -2 to ensure we can access i+2
        if (diaryContent[i] == Constants::Diary_ImageStart) {
            qDebug() << "Found IMAGE_START at line" << i;

            // Check if the next line after IMAGE_START is followed by IMAGE_END
            if (i + 2 < diaryContent.size() && diaryContent[i + 2] == Constants::Diary_ImageEnd) {
                qDebug() << "Found matching IMAGE_END at line" << (i + 2);
                qDebug() << "Current image data at line" << (i + 1) << ":" << diaryContent[i + 1];

                // Update the image data line
                diaryContent[i + 1] = newImageData;
                foundAndUpdated = true;
                qDebug() << "Updated image data to:" << newImageData;

                // For now, let's update the FIRST image entry we find
                // TODO: We should improve this to find the correct entry
                break;
            }
        }
    }

    if (!foundAndUpdated) {
        qWarning() << "Could not find image entry to update in diary file";

        // Let's debug what's in the diary content
        qDebug() << "=== Diary content debug ===";
        for (int i = 0; i < diaryContent.size(); i++) {
            qDebug() << "Line" << i << ":" << diaryContent[i];
        }
        qDebug() << "=== End diary content debug ===";
        return;
    }

    // Write back to file
    bool writeSuccess = OperationsFiles::writeEncryptedFileLines(
        current_DiaryFileName, m_mainWindow->user_Key, diaryContent);

    if (!writeSuccess) {
        qWarning() << "Failed to write updated diary file";
    } else {
        qDebug() << "Successfully wrote updated diary file";

        // Reload the diary to see the changes
        qDebug() << "Reloading diary to reflect changes";
        LoadDiary(current_DiaryFileName);
    }

    qDebug() << "=== updateImageEntryInDiary completed ===";
}

void Operations_Diary::handleSpecificImageClick(QListWidgetItem* item, int imageIndex)
{
    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        return; // Not an image item
    }

    bool isMultiImage = item->data(Qt::UserRole+5).toBool();

    if (isMultiImage) {
        QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
        if (imageIndex >= 0 && imageIndex < imagePaths.size()) {
            // Get the original image path instead of thumbnail
            QString diaryPath = current_DiaryFileName;
            QFileInfo diaryFileInfo(diaryPath);
            QString diaryDir = diaryFileInfo.dir().absolutePath();

            QString originalPath = getOriginalImagePath(imagePaths[imageIndex], diaryDir);
            openImageWithViewer(originalPath);
        }
    } else {
        QString imagePath = item->data(Qt::UserRole+4).toString();

        // Get the original image path instead of thumbnail
        QString diaryPath = current_DiaryFileName;
        QFileInfo diaryFileInfo(diaryPath);
        QString diaryDir = diaryFileInfo.dir().absolutePath();

        QString originalPath = getOriginalImagePath(imagePath, diaryDir);
        openImageWithViewer(originalPath);
    }
}

void Operations_Diary::copySpecificImagePath(QListWidgetItem* item, int imageIndex)
{
    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        return; // Not an image item
    }

    bool isMultiImage = item->data(Qt::UserRole+5).toBool();

    if (isMultiImage) {
        QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
        if (imageIndex >= 0 && imageIndex < imagePaths.size()) {
            // Get the original image path instead of thumbnail
            QString diaryPath = current_DiaryFileName;
            QFileInfo diaryFileInfo(diaryPath);
            QString diaryDir = diaryFileInfo.dir().absolutePath();

            QString originalPath = getOriginalImagePath(imagePaths[imageIndex], diaryDir);
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setText(originalPath);
        }
    } else {
        QString imagePath = item->data(Qt::UserRole+4).toString();

        // Get the original image path instead of thumbnail
        QString diaryPath = current_DiaryFileName;
        QFileInfo diaryFileInfo(diaryPath);
        QString diaryDir = diaryFileInfo.dir().absolutePath();

        QString originalPath = getOriginalImagePath(imagePath, diaryDir);
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(originalPath);
    }
}

void Operations_Diary::copyAllImagePaths(QListWidgetItem* item)
{
    if (!item || !item->data(Qt::UserRole+3).toBool()) {
        return; // Not an image item
    }

    bool isMultiImage = item->data(Qt::UserRole+5).toBool();

    QString diaryPath = current_DiaryFileName;
    QFileInfo diaryFileInfo(diaryPath);
    QString diaryDir = diaryFileInfo.dir().absolutePath();

    if (isMultiImage) {
        QStringList imagePaths = item->data(Qt::UserRole+4).toStringList();
        QStringList originalPaths = getOriginalImagePaths(imagePaths, diaryDir);
        QString allPaths = originalPaths.join("\n");
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(allPaths);
    } else {
        QString imagePath = item->data(Qt::UserRole+4).toString();
        QString originalPath = getOriginalImagePath(imagePath, diaryDir);
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(originalPath);
    }
}

bool Operations_Diary::isThumbnailPath(const QString& imagePath) const
{
    return imagePath.endsWith(".thumb");
}

QString Operations_Diary::getOriginalImagePath(const QString& thumbnailPath) const
{
    if (!isThumbnailPath(thumbnailPath)) {
        return thumbnailPath; // Already the original
    }

    // Get the directory from the thumbnail path
    QFileInfo fileInfo(thumbnailPath);
    QString diaryDir = fileInfo.dir().absolutePath();

    return getOriginalImagePath(thumbnailPath, diaryDir);
}

QString Operations_Diary::getOriginalImagePath(const QString& thumbnailPath, const QString& diaryDir) const
{
    if (!isThumbnailPath(thumbnailPath)) {
        return thumbnailPath; // Already the original
    }

    // Extract base name from thumbnail (remove .thumb extension)
    QFileInfo thumbnailInfo(thumbnailPath);
    QString baseName = thumbnailInfo.completeBaseName(); // This gets "2025.6.1.13.18.45" from "2025.6.1.13.18.45.thumb"

    // Look for files in the same directory with the same base name but different extension
    QDir dir(diaryDir);
    QStringList filters;
    filters << baseName + ".*";

    QStringList matchingFiles = dir.entryList(filters, QDir::Files);

    // Find the file that isn't the thumbnail
    foreach(const QString& fileName, matchingFiles) {
        if (!fileName.endsWith(".thumb")) {
            return QDir::cleanPath(diaryDir + "/" + fileName);
        }
    }

    // If no original found, return the thumbnail path as fallback
    qWarning() << "Original image not found for thumbnail:" << thumbnailPath;
    return thumbnailPath;
}

QStringList Operations_Diary::getOriginalImagePaths(const QStringList& imagePaths, const QString& diaryDir) const
{
    QStringList originalPaths;

    foreach(const QString& imagePath, imagePaths) {
        originalPaths.append(getOriginalImagePath(imagePath, diaryDir));
    }

    return originalPaths;
}

// SLOTS implementation

void Operations_Diary::on_DiaryTextInput_returnPressed()
{
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format approprietly
    QString todayDiaryPath = getDiaryFilePath(formattedTime); //Get the name of today's diary file.

    // Validate the diary input text
    QString diaryText = m_mainWindow->ui->DiaryTextInput->toPlainText();
    InputValidation::ValidationResult contentResult =
        InputValidation::validateInput(diaryText, InputValidation::InputType::DiaryContent, 10000);

    if (!contentResult.isValid) {
        QMessageBox::warning(m_mainWindow, "Invalid Diary Content",
                             contentResult.errorMessage + "\nPlease edit your entry.");
        return;
    }

    //Add new entry to diary
    if(!diaryText.isEmpty()) //if the text input isnt empty
    {
        if (current_DiaryFileName == todayDiaryPath) // if todays diary is currently loaded
        {
            InputNewEntry(current_DiaryFileName); // add text to todays diary
            // Select the newly added entry (the next-to-last item, since the last is the spacer)
            QList<QListWidgetItem*> items = getTextDisplayItems();
            if (items.length() > 1) {
                m_mainWindow->ui->DiaryTextDisplay->setCurrentItem(items.at(items.length() - 2));
            }
        }
        else if (!QFileInfo::exists(todayDiaryPath)) // else if todays diary isn't currently loaded and does not exist
        {
            current_DiaryFileName = todayDiaryPath;
            CreateNewDiary();
            cur_entriesNoSpacer = 100000; //set to absurb value to make sure a spacer/timestamp is added.
            InputNewEntry(current_DiaryFileName);
        }
        else //else if todays diary isnt currently loaded but does exist
        {
            // Force set current_DiaryFileName to today's diary
            current_DiaryFileName = todayDiaryPath;

            // Properly select today's year, month, and day in the UI
            m_mainWindow->ui->DiaryListYears->setCurrentIndex(m_mainWindow->ui->DiaryListYears->findText(formattedTime.section('.',0,0), Qt::MatchExactly));

            // Find today's month by exact match, not wildcard
            QList<QListWidgetItem*> monthItems = m_mainWindow->ui->DiaryListMonths->findItems(
                Operations::ConvertMonthtoText(formattedTime.section('.',1,1)),
                Qt::MatchContains);

            if (!monthItems.isEmpty()) {
                m_mainWindow->ui->DiaryListMonths->setCurrentItem(monthItems.at(0));
            }

            // Find today's day by exact match, not wildcard
            QList<QListWidgetItem*> dayItems = m_mainWindow->ui->DiaryListDays->findItems(
                formattedTime.section('.',2,2),
                Qt::MatchContains);

            if (!dayItems.isEmpty()) {
                m_mainWindow->ui->DiaryListDays->setCurrentItem(dayItems.at(0));
            }

            // Add entry to today's diary
            InputNewEntry(current_DiaryFileName);
        }
    }
    // as for preventing the user from typing in an older diary, this is handled in on_DiaryListDays_currentTextChanged().
}

void Operations_Diary::on_DiaryListYears_currentTextChanged(const QString &arg1)
{
    currentdiary_Year = arg1; // Sets the value of the currently selected year
    UpdateListMonths(arg1); // Updates the list of months

    // Find the most recent month in the selected year
    QList<QListWidgetItem*> monthItems = m_mainWindow->ui->DiaryListMonths->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard);
    if (!monthItems.isEmpty()) {
        // Create a list of months (as integers) for sorting
        QVector<int> monthNumbers;
        QMap<int, QListWidgetItem*> monthMap; // Map to associate month numbers with list items

        foreach(QListWidgetItem* item, monthItems) {
            QString monthName = item->text();
            QString monthNumStr = Operations::ConvertMonthtoInt(monthName);
            int monthNum = monthNumStr.toInt();
            monthNumbers.append(monthNum);
            monthMap[monthNum] = item;
        }

        // Sort months in descending order to find the most recent
        std::sort(monthNumbers.begin(), monthNumbers.end(), std::greater<int>());

        if (!monthNumbers.isEmpty()) {
            // Get the most recent month item
            QListWidgetItem* mostRecentMonth = monthMap[monthNumbers.first()];
            m_mainWindow->ui->DiaryListMonths->setCurrentItem(mostRecentMonth);
            // The month change will trigger on_DiaryListMonths_currentTextChanged
            // which will load the most recent day
        }
    }
}

void Operations_Diary::on_DiaryListMonths_currentTextChanged(const QString &currentText)
{
    currentdiary_Month = Operations::ConvertMonthtoInt(currentText); // Sets the value of the currently selected month as an Integer in a string
    UpdateListDays(currentText); // Updates the list of days.

    // Find the most recent day in the selected month
    QList<QListWidgetItem*> dayItems = m_mainWindow->ui->DiaryListDays->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard);
    if (!dayItems.isEmpty()) {
        // Create a list of days (as integers) for sorting
        QVector<int> dayNumbers;
        QMap<int, QListWidgetItem*> dayMap; // Map to associate day numbers with list items

        foreach(QListWidgetItem* item, dayItems) {
            // Extract just the day number from the item text (which might be "DD - DayOfWeek")
            QString dayText = item->text();
            QString dayNumStr = dayText.section(" - ", 0, 0).trimmed();
            int dayNum = dayNumStr.toInt();
            dayNumbers.append(dayNum);
            dayMap[dayNum] = item;
        }

        // Sort days in descending order to find the most recent
        std::sort(dayNumbers.begin(), dayNumbers.end(), std::greater<int>());

        if (!dayNumbers.isEmpty()) {
            // Get the most recent day item
            QListWidgetItem* mostRecentDay = dayMap[dayNumbers.first()];
            m_mainWindow->ui->DiaryListDays->setCurrentItem(mostRecentDay);
            // This will trigger on_DiaryListDays_currentTextChanged
            // which will load the diary for this day
        }
    }
}

void Operations_Diary::on_DiaryListDays_currentTextChanged(const QString &currentText)
{
    QDateTime date = QDateTime::currentDateTime(); // Get date and time
    QString formattedTime = date.toString("yyyy.MM.dd"); // Format appropriately
    QString todayDiaryPath = getDiaryFilePath(formattedTime);
    if (todayDiaryPath.isEmpty()) {
        // Handle the error case
        qDebug() << "Invalid diary path for date: " << formattedTime;
        return;
    }

    // Construct the diary date from the selected values
    QString diaryDate = currentdiary_Year + "." + currentdiary_Month + "." + currentText.left(2);
    QString diaryPath = getDiaryFilePath(diaryDate);
    if (diaryPath.isEmpty()) {
        // Handle the error case
        qDebug() << "Invalid diary path for date: " << diaryDate;
        return;
    }
    current_DiaryFileName = diaryPath; //gets the file path of the currently selected diary

    if(current_DiaryFileName == todayDiaryPath) // if currently selected diary is today's diary, allow the user to add new entries to the diary
    {
        m_mainWindow->ui->DiaryTextInput->setEnabled(true);
    }
    else
    {
        // Find the most recent diary
        QString latestDiaryPath = "";
        bool foundLatestDiary = false;

        // Scan directories to find the most recent diary
        QDir baseDir(DiariesFilePath);
        QStringList yearFolders = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(yearFolders.begin(), yearFolders.end());

        if (!yearFolders.isEmpty()) {
            QString latestYear = yearFolders.last();
            QDir yearDir(DiariesFilePath + latestYear);
            QStringList monthFolders = yearDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            std::sort(monthFolders.begin(), monthFolders.end());

            if (!monthFolders.isEmpty()) {
                QString latestMonth = monthFolders.last();
                QDir monthDir(DiariesFilePath + latestYear + "/" + latestMonth);
                QStringList dayFolders = monthDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                std::sort(dayFolders.begin(), dayFolders.end());

                if (!dayFolders.isEmpty()) {
                    QString latestDay = dayFolders.last();

                    // Construct the date string in the format YYYY.MM.DD
                    QString latestDateString = latestYear + "." + latestMonth + "." + latestDay;
                    latestDiaryPath = getDiaryFilePath(latestDateString);

                    if(QFileInfo::exists(latestDiaryPath)) {
                        foundLatestDiary = true;
                    }
                }
            }
        }

        if(foundLatestDiary && current_DiaryFileName == latestDiaryPath) // if the currently selected diary is the last file in our directory
        {
            m_mainWindow->ui->DiaryTextInput->setEnabled(true);
        }
        else // if currently selected diary is from a different day and there are at least 2 diary files
        {
            m_mainWindow->ui->DiaryTextInput->setEnabled(false);
        }
    }

    LoadDiary(current_DiaryFileName); // Load the currently selected Diary
}

void Operations_Diary::on_DiaryTextDisplay_itemChanged()
{
    if(!prevent_onDiaryTextDisplay_itemChanged && m_mainWindow->initFinished) // if we are not currently adding new text and init is complete
    {
        QList<QListWidgetItem*> items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
        if(items.length() > 0 && m_mainWindow->ui->DiaryTextDisplay->currentRow() > 0) // if our display has at least 1 line and the current row is the first one. prevents SIGSEV crash
        {
            // Get the current edited text
            QString editedText = "";
            if (m_mainWindow->ui->DiaryTextDisplay->currentItem()) {
                editedText = m_mainWindow->ui->DiaryTextDisplay->currentItem()->text();

                // Validate the edited text
                InputValidation::ValidationResult result =
                    InputValidation::validateInput(editedText, InputValidation::InputType::DiaryContent, 100000);

                if (!result.isValid) {
                    QMessageBox::warning(m_mainWindow, "Invalid Entry",
                                         "The text you entered contains invalid content: " + result.errorMessage);

                    // Reset to original text
                    m_mainWindow->ui->DiaryTextDisplay->currentItem()->setText(uneditedText);
                    return;
                }
            }

            if(m_mainWindow->ui->DiaryTextDisplay->currentRow() < previousDiaryLineCounter && previous_DiaryFileName != "") // if current row is within range of previous diary in the text display and a previous diary is loaded
            {
                if(editedText.isEmpty()) // if we erased all the text from the entry
                {
                    m_mainWindow->ui->DiaryTextDisplay->currentItem()->setText(uneditedText); // Restore the text to its original value.
                }
                SaveDiary(previous_DiaryFileName, true); // Save the previous diary with the newly modified text. true means we are saving the previous diary
            }
            else // if current row is in todays diary.
            {
                if(editedText.isEmpty()) // if we erased all the text from the entry
                {
                    m_mainWindow->ui->DiaryTextDisplay->currentItem()->setText(uneditedText); // Restore the text to its original value. uneditedText is saved in on_doubleclicked
                }
                prevent_onDiaryTextDisplay_itemChanged = true; // prevents infinite loop
                // REMOVES THE SPACER WIDGET USED IN DESELECTING THE LAST ENTRY. WE DONT WANT TO SAVE IT IN THE DIARY FILE
                QList<QListWidgetItem *> items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
                m_mainWindow->ui->DiaryTextDisplay->takeItem(items.length() - 1);
                //----------------------//
                SaveDiary(current_DiaryFileName, false); // Save the current diary with the newly modified text. false means we are saving todays diary
                //Add a spacer that is used only for one reason, being able to deselect the last entry of the display. IT IS NOT SAVED INTO OUR DIARY FILE
                m_mainWindow->ui->DiaryTextDisplay->addItem(Constants::Diary_Spacer); //Add spacer
                items = getTextDisplayItems(); // Update the list of all the text in the listview diary text display.
                m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setData(Qt::UserRole, true);// uses hidetext delegate to hide the spacer
                m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->setFlags(m_mainWindow->ui->DiaryTextDisplay->item(items.length() - 1)->flags() & ~Qt::ItemIsEnabled); // disable the spacer
                //------------------------------//
                prevent_onDiaryTextDisplay_itemChanged = false; // prevents infinite loop
            }
        }
    }
}

void Operations_Diary::on_DiaryTextDisplay_entered(const QModelIndex &index)
{
    if(m_mainWindow->initFinished)
    {
        if(index.flags() & Qt::ItemIsEnabled)
        {
            m_mainWindow->ui->DiaryTextDisplay->setCurrentIndex(index);
        }
        else
        {
            m_mainWindow->ui->DiaryTextDisplay->clearSelection();
        }
    }
}

void Operations_Diary::on_DiaryTextDisplay_clicked()
{
    m_mainWindow->ui->DiaryTextInput->setFocus();
}

//--------Task Logging--------//
void Operations_Diary::AddTaskLogEntry(QString taskType, QString taskName, QString taskListName, QString entryType, QDateTime dateTime, QString additionalInfo)
{
    // Make defensive copies of all input parameters
    QString safeTaskType = taskType;
    QString safeTaskName = taskName;
    QString safeTaskListName = taskListName;
    QString safeEntryType = entryType;
    QString safeAdditionalInfo = additionalInfo;
    QDateTime safeDateTime = dateTime.isValid() ? dateTime : QDateTime::currentDateTime();

    // Validate input parameters
    InputValidation::ValidationResult taskTypeResult =
        InputValidation::validateInput(safeTaskType, InputValidation::InputType::PlainText);
    InputValidation::ValidationResult taskNameResult =
        InputValidation::validateInput(safeTaskName, InputValidation::InputType::PlainText);
    InputValidation::ValidationResult entryTypeResult =
        InputValidation::validateInput(safeEntryType, InputValidation::InputType::PlainText);

    if (!taskTypeResult.isValid || !taskNameResult.isValid || !entryTypeResult.isValid) {
        qWarning() << "Invalid parameters for task log entry";
        return;
    }

    // Create a separate validation for additionalInfo if it's not empty
    if (!safeAdditionalInfo.isEmpty()) {
        InputValidation::ValidationResult additionalInfoResult =
            InputValidation::validateInput(safeAdditionalInfo, InputValidation::InputType::PlainText);
        if (!additionalInfoResult.isValid) {
            qWarning() << "Invalid additional info for task log entry";
            safeAdditionalInfo.clear(); // Clear if invalid to avoid using it
        }
    }

    // Format the datetime
    QString formattedDateTime = FormatDateTime(safeDateTime);

    // Construct the message based on task type and entry type
    QString message;

    // Build the message content with explicit scope to avoid lingering references
    {
        if (safeTaskType == "Simple") {
            if (safeEntryType == "Creation") {
                message = QString("Simple: %1 in %2 has been created on %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);
            }
            else if (safeEntryType == "Completion") {
                message = QString("Simple: %1 in %2 was completed on %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);

                // Add congratulatory message if provided
                if (!safeAdditionalInfo.isEmpty()) {
                    message += "\n" + safeAdditionalInfo;
                }
            }
        }
        else if (safeTaskType == "TimeLimit") {
            if (safeEntryType == "Creation") {
                message = QString("TimeLimit: %1 in %2 has been created on %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);
            }
            else if (safeEntryType == "Overdue") {
                message = QString("TimeLimit: %1 in %2 is now overdue %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);

                // Add punitive message if provided and valid
                if (!safeAdditionalInfo.isEmpty()) {
                    message += "\n" + safeAdditionalInfo;
                }
            }
            else if (safeEntryType == "CompletionOnTime") {
                message = QString("TimeLimit: %1 in %2 has been completed on time.").arg(safeTaskName).arg(safeTaskListName);

                // Add congratulatory message if provided
                if (!safeAdditionalInfo.isEmpty()) {
                    message += "\n" + safeAdditionalInfo;
                }
            }
            else if (safeEntryType == "CompletionLate") {
                message = QString("TimeLimit: %1 in %2 has been completed late by %3.").arg(safeTaskName).arg(safeTaskListName).arg(safeAdditionalInfo);
            }
        }
        else if (safeTaskType == "Recurrent") {
            if (safeEntryType == "Creation") {
                message = QString("Recurrent: %1 in %2 has been created on %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);
            }
            else if (safeEntryType == "Start") {
                if (safeAdditionalInfo.isEmpty()) {
                    message = QString("Recurrent: %1 in %2 needs to be completed by %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);
                } else {
                    message = QString("Recurrent: %1 in %2 was not completed last time. %1 needs to be completed by %3.").arg(safeTaskName).arg(safeTaskListName).arg(formattedDateTime);
                }
            }
            else if (safeEntryType == "CompletionOnTime") {
                message = QString("Recurrent: %1 in %2 has been completed on time. Next occurrence will be %3.").arg(safeTaskName).arg(safeTaskListName).arg(safeAdditionalInfo);
            }
            else if (safeEntryType == "CompletionLate") {
                // Make a defensive copy of additionalInfo before splitting
                QString timeDiff = safeAdditionalInfo.section('|', 0, 0);
                QString nextOccurrence = safeAdditionalInfo.section('|', 1, 1);
                message = QString("Recurrent: %1 in %2 has been completed late by %3. Next occurrence will be %4.").arg(safeTaskName).arg(safeTaskListName).arg(timeDiff).arg(nextOccurrence);
            }
        }
    }

    // If we didn't generate a valid message, don't log anything
    if (message.isEmpty()) {
        qWarning() << "Failed to generate message for task log entry";
        return;
    }

    // Get today's diary file path
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QString formattedDate = currentDateTime.toString("yyyy.MM.dd");
    QString todayDiaryPath = getDiaryFilePath(formattedDate);

    if (todayDiaryPath.isEmpty()) {
        qWarning() << "Failed to get diary file path for task log entry";
        return;
    }

    // Ensure today's diary directory exists
    ensureDiaryDirectoryExists(formattedDate);

    // Get current diary content or create new diary
    QStringList diaryContent;

    // Read existing content if diary exists
    if (QFileInfo::exists(todayDiaryPath)) {
        bool readSuccess = OperationsFiles::readEncryptedFileLines(
            todayDiaryPath, m_mainWindow->user_Key, diaryContent);

        if (!readSuccess) {
            qWarning() << "Failed to read diary file for task log entry";
            return;
        }
    } else {
        // This is a new diary, add the date header
        diaryContent.append(GetDiaryDateStamp(formattedDate));
    }

    // Format the timestamp
    QString formattedTime = currentDateTime.toString("hh:mm");

    // Check for existing Task Manager timestamps
    int currentTimeMinutes = formattedTime.section(":",0,0).toInt() * 60 + formattedTime.section(":",1,1).toInt();

    if (FindLastTimeStampType() == Constants::Diary_TaskManagerStart &&
        lastTimeStamp_Hours * 60 + lastTimeStamp_Minutes > currentTimeMinutes - m_mainWindow->setting_Diary_TStampTimer) {
        // Recent Task Manager timestamp exists, no need to add another
    } else {
        // Add a new Task Manager timestamp
        diaryContent.append(Constants::Diary_Spacer);
        diaryContent.append(Constants::Diary_TaskManagerStart);
        diaryContent.append("Task Manager at " + formattedTime);

        // Update timestamp tracking
        lastTimeStamp_Hours = formattedTime.section(":",0,0).toInt();
        lastTimeStamp_Minutes = formattedTime.section(":",1,1).toInt();
        cur_entriesNoSpacer = 10000; // Set to high value to ensure a regular timestamp if user types
    }

    // Add the message - handle multiline messages
    if (message.contains("\n")) {
        diaryContent.append(Constants::Diary_TextBlockStart);
        diaryContent.append(message);
        diaryContent.append(Constants::Diary_TextBlockEnd);
    } else {
        diaryContent.append(message);
    }

    // Write the updated content to the diary file
    bool writeSuccess = OperationsFiles::writeEncryptedFileLines(
        todayDiaryPath, m_mainWindow->user_Key, diaryContent);

    if (!writeSuccess) {
        qWarning() << "Failed to write task log entry to diary file";
        return;
    }

    // Reload today's diary if it's currently loaded
    if (!current_DiaryFileName.isEmpty() && current_DiaryFileName == todayDiaryPath) {
        LoadDiary(todayDiaryPath);
    }
}
