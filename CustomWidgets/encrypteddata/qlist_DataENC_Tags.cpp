#include "qlist_DataENC_Tags.h"
#include <QApplication>

// ============================================================================
// Constructor & Destructor
// ============================================================================
qlist_DataENC_Tags::qlist_DataENC_Tags(QWidget *parent)
    : QListWidget(parent)
{
    qDebug() << "qlist_DataENC_Tags: Constructor called";
    
    // Set selection behavior to not change on clicks
    // We'll handle the checkbox state manually
    setSelectionMode(QAbstractItemView::SingleSelection);
}

qlist_DataENC_Tags::~qlist_DataENC_Tags()
{
    qDebug() << "qlist_DataENC_Tags: Destructor called";
    // Disconnect all signals to prevent callbacks during destruction
    this->disconnect();
}

// ============================================================================
// Event Handlers
// ============================================================================
void qlist_DataENC_Tags::mousePressEvent(QMouseEvent *event)
{
    if (!event) {
        qWarning() << "qlist_DataENC_Tags: Null event in mousePressEvent";
        return;
    }
    
    // Get the item at the click position
    QPoint pos = event->pos();
    QListWidgetItem* clickedItem = itemAt(pos);
    
    if (!clickedItem) {
        qDebug() << "qlist_DataENC_Tags: No item at click position";
        // Call base class to handle selection clearing
        QListWidget::mousePressEvent(event);
        return;
    }
    
    // Validate the item still exists
    if (!isItemValid(clickedItem)) {
        qWarning() << "qlist_DataENC_Tags: Clicked item is no longer valid";
        return;
    }
    
    // Check if the item has a checkbox
    if (!(clickedItem->flags() & Qt::ItemIsUserCheckable)) {
        qDebug() << "qlist_DataENC_Tags: Item does not have a checkbox";
        // Call base class for normal selection
        QListWidget::mousePressEvent(event);
        return;
    }
    
    Qt::MouseButton button = event->button();
    Qt::CheckState currentState = clickedItem->checkState();
    Qt::CheckState newState = currentState;
    
    qDebug() << "qlist_DataENC_Tags: Mouse button:" << button 
             << "Current state:" << currentState;
    
    // Handle different mouse buttons
    if (button == Qt::LeftButton) {
        // Left-click: Toggle the tag (check/uncheck)
        newState = (currentState == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
        qDebug() << "qlist_DataENC_Tags: Left-click detected - toggling tag from" 
                 << (currentState == Qt::Checked ? "checked" : "unchecked")
                 << "to" << (newState == Qt::Checked ? "checked" : "unchecked");
    }
    else if (button == Qt::RightButton) {
        // Right-click: Only uncheck the tag (don't check if already unchecked)
        if (currentState == Qt::Checked) {
            newState = Qt::Unchecked;
            qDebug() << "qlist_DataENC_Tags: Right-click detected - unchecking tag";
        } else {
            // Tag is already unchecked, do nothing
            qDebug() << "qlist_DataENC_Tags: Right-click on already unchecked tag - no action";
            // Select the item for visual feedback but don't change state
            setCurrentItem(clickedItem);
            return;
        }
    }
    else {
        // Other buttons: let base class handle
        QListWidget::mousePressEvent(event);
        return;
    }
    
    // Only change state if it's different
    if (newState != currentState) {
        clickedItem->setCheckState(newState);
        
        // Emit custom signal for state change
        emit tagCheckStateChanged(clickedItem);
        
        // Also emit the standard itemChanged signal for compatibility
        emit itemChanged(clickedItem);
        
        qDebug() << "qlist_DataENC_Tags: Tag" << clickedItem->text() 
                 << "state changed to" << (newState == Qt::Checked ? "checked" : "unchecked");
    } else {
        qDebug() << "qlist_DataENC_Tags: Tag already in desired state";
    }
    
    // Select the item (visual feedback)
    setCurrentItem(clickedItem);
    
    // Don't call base class mousePressEvent to prevent default checkbox toggle behavior
}

// ============================================================================
// Helper Functions
// ============================================================================
bool qlist_DataENC_Tags::isItemValid(QListWidgetItem* item) const
{
    if (!item) {
        return false;
    }
    
    // Check if the item exists in this list
    for (int i = 0; i < count(); ++i) {
        if (this->item(i) == item) {
            return true;
        }
    }
    
    return false;
}
