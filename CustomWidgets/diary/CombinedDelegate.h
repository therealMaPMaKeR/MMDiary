#ifndef COMBINEDDELEGATE_H
#define COMBINEDDELEGATE_H

#include <QStyledItemDelegate>
#include <QColor>
#include <QSize>
#include <QTextEdit>

// Forward Declarations
class qtextedit_DiaryTextInput;
class qlist_DiaryTextDisplay;
class QPainter;
class QWidget;
class QStyleOptionViewItem;
class QModelIndex;
class QAbstractItemModel;
class QEvent;
class QObject;
class QListWidget;

class CombinedDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    CombinedDelegate(QObject *parent = nullptr);

    // Add setters for colored text properties
    void setColorLength(int length);
    void setTextColor(const QColor &color);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool eventFilter(QObject *object, QEvent *event) override;

private slots:
    void editorTextChanged(void);
    void onEditorClosed(const QString &text, int itemIndex);

private:
    // private variables
    int m_colorLength;     // Number of characters to color
    int m_taskManagerLength;  // Length of "Task Manager" text
    QColor m_textColor;    // Color for text characters

    // private functions
    void adjustEditorSize(QTextEdit *editor) const;
    void adjustListWidgetScroll(QTextEdit *editor) const;

    void paintImageItem(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QPixmap loadImageForDisplay(const QString& imagePath) const;

    // Image painting helper methods (simplified for single images only)
    void paintSingleImage(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, const QString& imagePath) const;
    // REMOVED: paintMultipleImages - no longer needed

    QSize getActualImageDisplaySize(const QString& imagePath) const;
signals:
    void TextModificationsMade(QString text, int itemIndex);
    // Signal to include both text and item index
    void textCommitted(const QString &text, int itemIndex) const;
};

#endif // COMBINEDDELEGATE_H
