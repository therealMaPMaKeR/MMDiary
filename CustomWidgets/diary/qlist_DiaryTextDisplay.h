#ifndef QLIST_DIARYTEXTDISPLAY_H
#define QLIST_DIARYTEXTDISPLAY_H

#include <QListWidget>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>

class SafeTimer;

class qtextedit_DiaryTextInput;

class qlist_DiaryTextDisplay : public QListWidget
{
    Q_OBJECT
public:
    qlist_DiaryTextDisplay(QWidget *parent = nullptr);
    ~qlist_DiaryTextDisplay();

    // Add a method to get the current font size
    int currentFontSize() const { return m_fontSize; }

    // Add method to select the last item
    void selectLastItem();

    QPoint getLastClickPos() const { return m_lastClickPos; }

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

    // Add event handlers for mouse events
    void leaveEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

    // Add drag & drop event handlers
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

public slots:
    void keyPressEvent(QKeyEvent *event); // needed for event filter to work in main window because it makes the slot public
    void UpdateFontSize_Slot(int size, bool resize);
    void TextWasEdited(QString text, int itemIndex);
    
private slots:
    void performDeferredSizeUpdate();  // Slot for deferred size updates after resize

private:
    // Methods for updating items
    void updateItemSizes();
    void updateItemFonts();
    int m_fontSize = 10;
    bool m_inSizeUpdate = false;
    bool m_inMouseEvent = false; // New flag to prevent recursive events

    QPoint m_lastClickPos;
    
    // Resource management timers
    SafeTimer* m_dragDropTimer = nullptr;     // Timer for drag&drop re-enable
    SafeTimer* m_resizeTimer = nullptr;       // Timer for coalescing resize events

    // Helper methods for drag & drop
    bool isImageFile(const QString& filePath);
    QStringList getSupportedImageFormats();

signals:
    void sizeUpdateStarted();
    void sizeUpdateFinished();

    // Add signal for image dropping
    void imagesDropped(const QStringList& imagePaths);
};

#endif // QLIST_DIARYTEXTDISPLAY_H
