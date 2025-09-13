#include "qlist_TasklistDisplay.h"
#include <QDrag>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPainter>

void qlist_TasklistDisplay::startDrag(Qt::DropActions supportedActions)
{
    qDebug() << "qlist_TasklistDisplay: startDrag called";
    
    QList<QListWidgetItem*> items = selectedItems();
    if (items.isEmpty()) {
        return;
    }
    
    QMimeData* data = mimeData(items);
    if (!data) {
        return;
    }
    
    QDrag* drag = new QDrag(this);
    drag->setMimeData(data);
    
    // Set a pixmap for the drag (optional)
    if (!items.isEmpty()) {
        QListWidgetItem* firstItem = items.first();
        // Create a simple text pixmap
        QPixmap pixmap(200, 30);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setPen(Qt::white);
        painter.drawText(pixmap.rect(), Qt::AlignLeft | Qt::AlignVCenter, firstItem->text());
        drag->setPixmap(pixmap);
    }
    
    // Emit signal that external drag has started
    if (!items.isEmpty()) {
        QListWidgetItem* item = items.first();
        QString taskName = item->text();
        QString taskData = item->data(Qt::UserRole).toString();
        emit taskExternalDragStarted(taskName, taskData);
    }
    
    // Start the drag with move and copy actions
    Qt::DropAction dropAction = drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
    
    qDebug() << "qlist_TasklistDisplay: Drag completed with action:" << dropAction;
}

QMimeData* qlist_TasklistDisplay::mimeData(const QList<QListWidgetItem*>& items) const
{
    if (items.isEmpty()) {
        return nullptr;
    }
    
    QMimeData* mimeData = new QMimeData();
    
    // Add task-specific MIME data
    QListWidgetItem* item = items.first();  // For now, only support single task drag
    
    // Create JSON object with task data
    QJsonObject taskJson;
    taskJson["name"] = item->text();
    taskJson["taskId"] = item->data(Qt::UserRole).toString();  // Task ID if available
    taskJson["completed"] = (item->checkState() == Qt::Checked);
    
    // Add any other data stored in the item
    // UserRole + 1 might contain additional task data
    if (!item->data(Qt::UserRole + 1).toString().isEmpty()) {
        taskJson["additionalData"] = item->data(Qt::UserRole + 1).toString();
    }
    
    // Convert to JSON document
    QJsonDocument doc(taskJson);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    // Set custom MIME type for task data
    mimeData->setData("application/x-task-data", jsonData);
    
    // Also set text for compatibility
    mimeData->setText(item->text());
    
    // Call base class to preserve internal drag behavior
    QMimeData* baseMimeData = QListWidget::mimeData(items);
    if (baseMimeData) {
        // Copy formats from base mime data
        foreach(const QString& format, baseMimeData->formats()) {
            if (!mimeData->hasFormat(format)) {
                mimeData->setData(format, baseMimeData->data(format));
            }
        }
        delete baseMimeData;
    }
    
    return mimeData;
}
