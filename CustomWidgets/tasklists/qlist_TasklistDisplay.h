#ifndef QLIST_TASKLISTDISPLAY_H
#define QLIST_TASKLISTDISPLAY_H

#include <QWidget>
#include <QListWidget>
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDebug>
#include <QPointer>

class qlist_TasklistDisplay : public QListWidget
{
    Q_OBJECT

public:
    explicit qlist_TasklistDisplay(QWidget *parent = nullptr) : QListWidget(parent),
        m_checkboxWidth(25),
        m_lastClickedItem(nullptr),
        m_lastClickedRow(-1) {
        qDebug() << "qlist_TasklistDisplay: Constructor called";
        // Enable drag and drop by default
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        
        // Connect to model signals to handle item deletion
        connect(this->model(), &QAbstractItemModel::rowsAboutToBeRemoved,
                this, &qlist_TasklistDisplay::handleRowsAboutToBeRemoved);
    }
    
    ~qlist_TasklistDisplay() {
        qDebug() << "qlist_TasklistDisplay: Destructor called";
        // Clear tracked item pointer
        m_lastClickedItem = nullptr;
        m_lastClickedRow = -1;
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
        if (!event) {
            qWarning() << "qlist_TasklistDisplay: Null event in mousePressEvent";
            return;
        }
        
        qDebug() << "qlist_TasklistDisplay: mousePressEvent called";
        // Store the clicked position and item for potential double-click check
        m_lastClickPos = event->pos();
        
        QListWidgetItem* clickedItem = itemAt(m_lastClickPos);
        if (clickedItem) {
            m_lastClickedItem = clickedItem;
            m_lastClickedRow = row(clickedItem);  // Store row index as backup
        } else {
            m_lastClickedItem = nullptr;
            m_lastClickedRow = -1;
        }

        // Pass the event to base class
        QListWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (!event) {
            qWarning() << "qlist_TasklistDisplay: Null event in mouseDoubleClickEvent";
            return;
        }
        
        qDebug() << "qlist_TasklistDisplay: mouseDoubleClickEvent called";
        // Get the item at the current position
        QPoint pos = event->pos();
        QListWidgetItem* item = itemAt(pos);

        // Validate that the item still exists and matches what we clicked
        bool isSameItem = false;
        if (item && m_lastClickedItem) {
            // Check if it's the same item by comparing both pointer and row
            if (item == m_lastClickedItem || 
                (m_lastClickedRow >= 0 && m_lastClickedRow < count() && 
                 this->item(m_lastClickedRow) == item)) {
                isSameItem = true;
            }
        }
        
        // If double-clicking on the same item as the first click
        if (item && isSameItem) {
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

private slots:
    void handleRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last) {
        Q_UNUSED(parent);
        // Check if our tracked item is being removed
        if (m_lastClickedRow >= first && m_lastClickedRow <= last) {
            qDebug() << "qlist_TasklistDisplay: Tracked item is being removed, clearing reference";
            m_lastClickedItem = nullptr;
            m_lastClickedRow = -1;
        }
    }

private:
    int m_checkboxWidth;  // Width of the checkbox detection area
    QPoint m_lastClickPos;  // Position of the last click
    QListWidgetItem* m_lastClickedItem;  // Item of the last click (raw pointer, validated before use)
    int m_lastClickedRow;  // Row index of last clicked item (for validation)
};

#endif // QLIST_TASKLISTDISPLAY_H
