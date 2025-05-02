#ifndef CUSTOM_QLISTWIDGET_H
#define CUSTOM_QLISTWIDGET_H

#include <QListWidget>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QEvent>

class custom_QTextEditWidget;

class custom_QListWidget : public QListWidget
{
    Q_OBJECT
public:
    custom_QListWidget(QWidget *parent = nullptr);
    ~custom_QListWidget();

    // Add a method to get the current font size
    int currentFontSize() const { return m_fontSize; }

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
    void keyPressEvent(QKeyEvent *event); // needed for event filter to work in main window because it makes the slot public
    void UpdateFontSize_Slot(int size, bool resize);
    void TextWasEdited(QString text, int itemIndex);

private:
    // Methods for updating items
    void updateItemSizes();
    void updateItemFonts();
    int m_fontSize = 10;
    bool m_inSizeUpdate = false;

signals:
    void sizeUpdateStarted();
    void sizeUpdateFinished();
};

#endif // CUSTOM_QLISTWIDGET_H
