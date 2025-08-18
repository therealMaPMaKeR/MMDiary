#ifndef QLIST_TASKLISTDISPLAY_H
#define QLIST_TASKLISTDISPLAY_H

#include <QWidget>
#include <QListWidget>
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDebug>

class qlist_TasklistDisplay : public QListWidget
{
    Q_OBJECT

public:
    explicit qlist_TasklistDisplay(QWidget *parent = nullptr) : QListWidget(parent),
        m_checkboxWidth(25) {
        qDebug() << "qlist_TasklistDisplay: Constructor called";
        // Enable drag and drop by default
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
    }

    void setCheckboxWidth(int width) {
        qDebug() << "qlist_TasklistDisplay: setCheckboxWidth called with width:" << width;
        m_checkboxWidth = width;
    }

    int checkboxWidth() const {
        return m_checkboxWidth;
    }

signals:
    // Signal emitted when items are reordered through drag and drop
    void itemsReordered();

protected:
    void mousePressEvent(QMouseEvent *event) override {
        qDebug() << "qlist_TasklistDisplay: mousePressEvent called";
        // Store the clicked position and item for potential double-click check
        m_lastClickPos = event->pos();
        m_lastClickedItem = itemAt(m_lastClickPos);

        // Pass the event to base class
        QListWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        qDebug() << "qlist_TasklistDisplay: mouseDoubleClickEvent called";
        // Get the item at the current position
        QPoint pos = event->pos();
        QListWidgetItem* item = itemAt(pos);

        // If double-clicking on the same item as the first click
        if (item && item == m_lastClickedItem) {
            // Check if the click is in the checkbox area
            QRect rect = visualItemRect(item);
            QRect checkboxRect = rect;
            checkboxRect.setWidth(m_checkboxWidth);

            if (checkboxRect.contains(pos)) {
                qDebug() << "qlist_TasklistDisplay: Double-click detected in checkbox area, treating as single click";
                // Treat as a single click instead of a double-click
                // We simulate another mouse press event to toggle the checkbox
                QMouseEvent singleClick(QEvent::MouseButtonPress,
                                        pos,
                                        event->globalPosition(),
                                        event->button(),
                                        event->buttons(),
                                        event->modifiers());
                QListWidget::mousePressEvent(&singleClick);
                return; // Don't pass to base class double-click handler
            }
        }

        // For clicks outside the checkbox area, proceed with normal double-click
        QListWidget::mouseDoubleClickEvent(event);
    }

    // Override drop event to handle reordering
    void dropEvent(QDropEvent *event) override {
        qDebug() << "qlist_TasklistDisplay: dropEvent called";
        // Call the base class implementation to handle the actual drop
        QListWidget::dropEvent(event);

        // After the drop has been processed, emit our signal
        emit itemsReordered();

        // Accept the event
        event->acceptProposedAction();
    }

    // Additional drag and drop event handlers for more control if needed
    void dragEnterEvent(QDragEnterEvent *event) override {
        QListWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        QListWidget::dragMoveEvent(event);
    }

private:
    int m_checkboxWidth;  // Width of the checkbox detection area
    QPoint m_lastClickPos;  // Position of the last click
    QListWidgetItem* m_lastClickedItem = nullptr;  // Item of the last click
};

#endif // QLIST_TASKLISTDISPLAY_H
