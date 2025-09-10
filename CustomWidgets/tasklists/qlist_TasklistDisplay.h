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
#include <QUuid>

class qlist_TasklistDisplay : public QListWidget
{
    Q_OBJECT

public:
    explicit qlist_TasklistDisplay(QWidget *parent = nullptr) : QListWidget(parent),
        m_checkboxWidth(25),
        m_lastClickedItem(nullptr),
        m_lastClickedRow(-1),
        m_lastClickedItemId() {
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
    
    // Public method to explicitly scroll to an item when needed
    void scrollToItemExplicitly(QListWidgetItem* item) {
        if (item) {
            // Use the base class scrollTo directly to bypass our override
            QListWidget::scrollTo(indexFromItem(item));
        }
    }
    
    ~qlist_TasklistDisplay() {
        qDebug() << "qlist_TasklistDisplay: Destructor called";
        // Disconnect all signals to prevent callbacks during destruction
        this->disconnect();
        // Clear tracked item pointer
        m_lastClickedItem = nullptr;
        m_lastClickedRow = -1;
        m_lastClickedItemId.clear();
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
    // Override scrollTo to disable automatic scrolling to selected items
    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override {
        Q_UNUSED(index);
        Q_UNUSED(hint);
        // Do nothing - this disables auto-scrolling when selecting items
        // Users can still manually scroll using scrollbar or mouse wheel
    }
    
    void mousePressEvent(QMouseEvent *event) override {
        if (!event) {
            qWarning() << "qlist_TasklistDisplay: Null event in mousePressEvent";
            return;
        }
        
        qDebug() << "qlist_TasklistDisplay: mousePressEvent called at pos:" << event->pos();
        // Store the clicked position and item for potential double-click check
        m_lastClickPos = event->pos();
        
        QListWidgetItem* clickedItem = itemAt(m_lastClickPos);
        if (clickedItem) {
            m_lastClickedItem = clickedItem;
            m_lastClickedRow = row(clickedItem);  // Store row index as backup
            m_lastClickedItemId = QUuid::createUuid().toString();  // Generate unique ID
            clickedItem->setData(Qt::UserRole + 100, m_lastClickedItemId);  // Store ID in item
            
            // Check if the item has a checkbox
            if (clickedItem->flags() & Qt::ItemIsUserCheckable) {
                QRect rect = visualItemRect(clickedItem);
                
                // IMPORTANT: Check if click is within Qt's default checkbox area FIRST
                // Qt's checkbox is typically in the first 16-20 pixels
                QRect qtCheckboxRect = rect;
                qtCheckboxRect.setWidth(20);
                
                // If click is in Qt's default checkbox area, we need to handle it specially
                if (qtCheckboxRect.contains(m_lastClickPos)) {
                    // Check if it's also in our extended checkbox area
                    QRect customCheckboxRect = rect;
                    customCheckboxRect.setWidth(m_checkboxWidth);
                    
                    if (customCheckboxRect.contains(m_lastClickPos)) {
                        qDebug() << "qlist_TasklistDisplay: Click in custom checkbox area - toggling state";
                        // Toggle the checkbox state manually
                        clickedItem->setCheckState(
                            clickedItem->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                        
                        // Emit itemChanged signal so Operations_TaskLists can handle the change
                        emit itemChanged(clickedItem);
                    }
                    
                    // Select the item but don't let Qt handle the checkbox toggle
                    setCurrentItem(clickedItem);
                    // CRITICAL: Don't pass to base class - this prevents Qt from toggling the checkbox
                    return;
                } else {
                    // Check if click is in our extended checkbox area (beyond Qt's default)
                    QRect customCheckboxRect = rect;
                    customCheckboxRect.setWidth(m_checkboxWidth);
                    
                    if (customCheckboxRect.contains(m_lastClickPos)) {
                        qDebug() << "qlist_TasklistDisplay: Click in extended checkbox area - toggling state";
                        // Toggle the checkbox state manually
                        clickedItem->setCheckState(
                            clickedItem->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                        
                        // Emit itemChanged signal so Operations_TaskLists can handle the change
                        emit itemChanged(clickedItem);
                        
                        // Select the item
                        setCurrentItem(clickedItem);
                        return; // Don't pass to base class
                    }
                }
            }
        } else {
            m_lastClickedItem = nullptr;
            m_lastClickedRow = -1;
            m_lastClickedItemId.clear();
        }

        // Pass to base class for normal selection handling
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
        if (item && m_lastClickedItem && isItemValid(item)) {
            // Check if it's the same item using multiple validation methods
            bool pointerMatch = (item == m_lastClickedItem);
            bool rowMatch = (m_lastClickedRow >= 0 && m_lastClickedRow < count() && 
                            this->item(m_lastClickedRow) == item);
            bool idMatch = (!m_lastClickedItemId.isEmpty() && 
                           item->data(Qt::UserRole + 100).toString() == m_lastClickedItemId);
            
            isSameItem = (pointerMatch || rowMatch) && idMatch;
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
            m_lastClickedItemId.clear();
        }
    }

private:
    int m_checkboxWidth;  // Width of the checkbox detection area
    QPoint m_lastClickPos;  // Position of the last click
    QListWidgetItem* m_lastClickedItem;  // Pointer to last clicked item (validated before use)
    int m_lastClickedRow;  // Row index of last clicked item (for validation)
    QString m_lastClickedItemId;  // Unique ID for additional validation
    
    // Helper method to validate item still exists
    bool isItemValid(QListWidgetItem* item) const {
        if (!item) return false;
        for (int i = 0; i < count(); ++i) {
            if (this->item(i) == item) {
                return true;
            }
        }
        return false;
    }
};

#endif // QLIST_TASKLISTDISPLAY_H
