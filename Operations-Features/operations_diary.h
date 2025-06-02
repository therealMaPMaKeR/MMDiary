#ifndef OPERATIONS_DIARY_H
#define OPERATIONS_DIARY_H

#include <QObject>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include <QMessageBox>
#include <QMutex>
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

    QMutex m_saveDiaryMutex;

    // Image handling functions
    QString generateImageFilename(const QString& originalExtension, const QString& diaryDir);
    bool saveEncryptedImage(const QString& sourcePath, const QString& targetPath);
    QPixmap generateThumbnail(const QString& imagePath, int maxSize = 64);
    QSize getImageDimensions(const QString& imagePath);
    bool isImageOversized(const QSize& imageSize, int maxWidth = 400, int maxHeight = 300);
    void processAndAddImages(const QStringList& imagePaths, bool forceThumbnails = false);
    void addImageToDisplay(const QString& imagePath, bool isThumbnail);
    bool openImageWithViewer(const QString& imagePath);
    void cleanupBrokenImageReferences();

    // Constants for image handling
    static const int MAX_IMAGE_WIDTH = 0;
    static const int MAX_IMAGE_HEIGHT = 0;
    static const int THUMBNAIL_SIZE = 64;

    bool loadAndDisplayImage(const QString& imagePath, const QString& imageFilename);
    QPixmap loadEncryptedImage(const QString& encryptedImagePath);
    QString getImageDisplayText(const QString& imageFilename, const QSize& imageSize);
    void setupImageItem(QListWidgetItem* item, const QString& imagePath, const QString& displayText);
    bool markDiaryForCleanup = false; // Flag to indicate diary needs cleanup

    void handleImageClick(QListWidgetItem* item);
    void addImagesToCurrentDiary(const QStringList& imageFilenames, const QString& diaryFilePath, bool shouldGroup = false);
    bool checkShouldGroupImages(const QString& diaryFilePath);

    // Image click detection
    int m_clickedImageIndex = -1;  // Index of clicked image in multi-image items
    QPoint m_lastContextMenuPos;   // Position where context menu was requested

    // Helper functions for image deletion and click detection
    void deleteImageFiles(const QString& imageData, const QString& diaryDir);
    int calculateClickedImageIndex(QListWidgetItem* item, const QPoint& clickPos);
    void deleteSpecificImage(QListWidgetItem* item, int imageIndex);
    QString removeImageFromData(const QString& imageData, int indexToRemove);
    void updateImageEntryInDiary(QListWidgetItem* item, const QString& originalImageData);
    void handleSpecificImageClick(QListWidgetItem* item, int imageIndex);
    void copySpecificImagePath(QListWidgetItem* item, int imageIndex);
    void copyAllImagePaths(QListWidgetItem* item);

    // Helper functions for thumbnail/original image handling
    bool isThumbnailPath(const QString& imagePath) const;
    QString getOriginalImagePath(const QString& thumbnailPath) const;
    QString getOriginalImagePath(const QString& thumbnailPath, const QString& diaryDir) const;
    QStringList getOriginalImagePaths(const QStringList& imagePaths, const QString& diaryDir) const;

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
