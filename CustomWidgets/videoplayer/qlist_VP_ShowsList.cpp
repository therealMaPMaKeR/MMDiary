#include "qlist_VP_ShowsList.h"
#include <QApplication>
#include <QtMath>

qlist_VP_ShowsList::qlist_VP_ShowsList(QWidget *parent)
    : QListWidget(parent)
    , m_iconViewScrollMultiplier(1.0)  //
    , m_listViewScrollMultiplier(1.0)   // Normal speed for list view
{
    qDebug() << "qlist_VP_ShowsList: Custom TV shows list widget created";
    qDebug() << "qlist_VP_ShowsList: Icon view scroll multiplier:" << m_iconViewScrollMultiplier;
    qDebug() << "qlist_VP_ShowsList: List view scroll multiplier:" << m_listViewScrollMultiplier;
}

qlist_VP_ShowsList::~qlist_VP_ShowsList()
{
    qDebug() << "qlist_VP_ShowsList: Custom TV shows list widget destroyed";
}

void qlist_VP_ShowsList::setIconViewScrollMultiplier(double multiplier)
{
    if (multiplier > 0.0 && multiplier <= 50.0) {  // Reasonable limits
        m_iconViewScrollMultiplier = multiplier;
        qDebug() << "qlist_VP_ShowsList: Icon view scroll multiplier set to:" << m_iconViewScrollMultiplier;
    } else {
        qDebug() << "Critical-qlist_VP_ShowsList: Invalid scroll multiplier value:" << multiplier 
                 << "- must be between 0.0 and 50.0";
    }
}

bool qlist_VP_ShowsList::isIconViewMode() const
{
    // Check if we're in icon mode by looking at the view mode
    return viewMode() == QListView::IconMode;
}

void qlist_VP_ShowsList::wheelEvent(QWheelEvent *event)
{
    /*
    // Check if we're in icon view mode
    bool inIconView = isIconViewMode();
    
    // Determine which multiplier to use
    double scrollMultiplier = inIconView ? m_iconViewScrollMultiplier : m_listViewScrollMultiplier;
    
    qDebug() << "qlist_VP_ShowsList: Wheel event - Icon view:" << inIconView 
             << "- Using multiplier:" << scrollMultiplier;
    
    if (scrollMultiplier == 1.0) {
        // Normal scrolling speed - just pass to base class
        QListWidget::wheelEvent(event);
        return;
    }
    
    // Get the current vertical scroll bar
    QScrollBar* vScrollBar = verticalScrollBar();
    if (!vScrollBar) {
        qDebug() << "Critical-qlist_VP_ShowsList: No vertical scroll bar found";
        QListWidget::wheelEvent(event);
        return;
    }
    
    // Calculate the scroll delta
    QPoint numPixels = event->pixelDelta();
    QPoint numDegrees = event->angleDelta() / 8;
    
    int delta = 0;
    
    if (!numPixels.isNull()) {
        // Use pixel delta if available (high-precision trackpads)
        delta = numPixels.y();
        qDebug() << "qlist_VP_ShowsList: Using pixel delta:" << delta;
    } else if (!numDegrees.isNull()) {
        // Use angle delta (standard mouse wheels)
        // Usually, one wheel step is 15 degrees
        QPoint numSteps = numDegrees / 15;
        delta = numSteps.y() * 120;  // Convert back to standard wheel delta units
        qDebug() << "qlist_VP_ShowsList: Using angle delta, steps:" << numSteps.y() << "delta:" << delta;
    }
    
    if (delta != 0) {
        // Apply the multiplier to the scroll amount
        int amplifiedDelta = qRound(delta * scrollMultiplier);
        
        // Calculate the new scroll position
        int currentValue = vScrollBar->value();
        int newValue = currentValue - amplifiedDelta;  // Negative because wheel up = positive delta
        
        // Clamp to valid range
        newValue = qBound(vScrollBar->minimum(), newValue, vScrollBar->maximum());
        
        qDebug() << "qlist_VP_ShowsList: Scrolling from" << currentValue << "to" << newValue 
                 << "(delta:" << delta << "amplified:" << amplifiedDelta << ")";
        
        // Set the new scroll position
        vScrollBar->setValue(newValue);
        
        // Accept the event to prevent further processing
        event->accept();
    } else {
        // No scroll delta, pass to base class
        QListWidget::wheelEvent(event);
    }
    */
    QListWidget::wheelEvent(event); // I had a scrolling bug once, and I didnt rly try to debug it, I just tried to fix it. Now I don't know what happened and I realise this code is useless. Because it was a random bug.
}

void qlist_VP_ShowsList::mousePressEvent(QMouseEvent *event)
{
    // Get the item at the click position
    QListWidgetItem* item = itemAt(event->pos());
    
    // If no item was clicked (clicking on empty space)
    if (!item) {
        qDebug() << "qlist_VP_ShowsList: Clicked on empty space, clearing selection";
        
        // Clear the selection
        clearSelection();
        setCurrentItem(nullptr);
        
        // Emit the signal to notify listeners
        emit selectionCleared();
        
        // Don't call the base class implementation to prevent default behavior
        event->accept();
        return;
    }
    
    // If an item was clicked, use default behavior
    QListWidget::mousePressEvent(event);
}
