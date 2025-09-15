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
    QListWidget::wheelEvent(event);
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
