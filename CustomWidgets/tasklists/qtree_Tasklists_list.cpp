#include "qtree_Tasklists_list.h"
#include <QHeaderView>
#include <QDrag>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include "../../Operations-Global/inputvalidation.h"

qtree_Tasklists_list::qtree_Tasklists_list(QWidget *parent)
    : QTreeWidget(parent)
    , m_draggedItem(nullptr)
    , m_dragDropEnabled(true)
    , m_lastSelectedItem(nullptr)
{
    qDebug() << "qtree_Tasklists_list: Constructor called";
    setupWidget();
}

qtree_Tasklists_list::~qtree_Tasklists_list()
{
    qDebug() << "qtree_Tasklists_list: Destructor called";
    // Disconnect all signals to prevent callbacks during destruction
    this->disconnect();
    // Clear tracked item pointers
    m_lastSelectedItem = nullptr;
    m_draggedItem = nullptr;
}

void qtree_Tasklists_list::setupWidget()
{
    qDebug() << "qtree_Tasklists_list: Setting up widget";
    
    // Set up the tree widget
    setHeaderHidden(true);
    setRootIsDecorated(true);
    setIndentation(20);
    
    // Enable drag and drop
    setDragDropEnabled(true);
    
    // Set selection mode
    setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Enable context menu
    setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Create context menu actions
    m_actionRename = new QAction("Rename", this);
    m_actionDelete = new QAction("Delete", this);
    m_actionNewCategory = new QAction("New Category", this);
    m_actionNewTasklist = new QAction("New Tasklist", this);
    
    // Connect selection changed
    connect(this, &QTreeWidget::itemSelectionChanged, this, [this]() {
        QList<QTreeWidgetItem*> selected = selectedItems();
        if (!selected.isEmpty()) {
            QTreeWidgetItem* item = selected.first();
            m_lastSelectedItem = item;
            
            if (isCategory(item)) {
                emit categorySelected(item->text(0));
            } else if (isTasklist(item)) {
                emit tasklistSelected(item->text(0));
            }
        }
    });
}

void qtree_Tasklists_list::setDragDropEnabled(bool enabled)
{
    m_dragDropEnabled = enabled;
    
    if (enabled) {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
    } else {
        setDragEnabled(false);
        setAcceptDrops(false);
        setDropIndicatorShown(false);
        setDragDropMode(QAbstractItemView::NoDragDrop);
    }
}

QStringList qtree_Tasklists_list::getAllTasklists() const
{
    QStringList allTasklists;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* categoryItem = topLevelItem(i);
        if (categoryItem && isCategory(const_cast<qtree_Tasklists_list*>(this)->topLevelItem(i))) {
            for (int j = 0; j < categoryItem->childCount(); ++j) {
                QTreeWidgetItem* tasklistItem = categoryItem->child(j);
                if (tasklistItem) {
                    allTasklists.append(tasklistItem->text(0));
                }
            }
        }
    }
    return allTasklists;
}

QTreeWidgetItem* qtree_Tasklists_list::addCategory(const QString& categoryName)
{
    qDebug() << "qtree_Tasklists_list: Adding category:" << categoryName;
    
    // Validate category name
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(categoryName, InputValidation::InputType::PlainText);
    if (!result.isValid) {
        qWarning() << "qtree_Tasklists_list: Invalid category name:" << result.errorMessage;
        return nullptr;
    }
    
    // Check if category already exists
    if (findCategory(categoryName)) {
        qDebug() << "qtree_Tasklists_list: Category already exists:" << categoryName;
        return findCategory(categoryName);
    }
    
    // Create new category item
    QTreeWidgetItem* categoryItem = new QTreeWidgetItem(this);
    categoryItem->setText(0, categoryName);
    categoryItem->setData(0, CategoryRole, true);
    categoryItem->setExpanded(true);
    
    // Set appearance for category
    QFont font = categoryItem->font(0);
    font.setBold(true);
    categoryItem->setFont(0, font);
    categoryItem->setForeground(0, QColor(180, 180, 180));
    
    // Add to categories list
    m_categories.append(categoryName);
    
    emit structureChanged();
    return categoryItem;
}

QTreeWidgetItem* qtree_Tasklists_list::findCategory(const QString& categoryName)
{
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (item && isCategory(item) && item->text(0) == categoryName) {
            return item;
        }
    }
    return nullptr;
}

QTreeWidgetItem* qtree_Tasklists_list::getOrCreateCategory(const QString& categoryName)
{
    QTreeWidgetItem* category = findCategory(categoryName);
    if (!category) {
        category = addCategory(categoryName);
    }
    return category;
}

void qtree_Tasklists_list::removeCategory(const QString& categoryName)
{
    qDebug() << "qtree_Tasklists_list: Removing category:" << categoryName;
    
    QTreeWidgetItem* categoryItem = findCategory(categoryName);
    if (!categoryItem) {
        return;
    }
    
    // All tasklists in this category will be deleted by the caller
    // Just clear the children here
    while (categoryItem->childCount() > 0) {
        QTreeWidgetItem* child = categoryItem->takeChild(0);
        if (child) {
            delete child;
        }
    }
    
    // Clear tracked pointers before removing
    clearTrackedItem(categoryItem);
    
    // Remove the category
    int index = indexOfTopLevelItem(categoryItem);
    if (index >= 0) {
        delete takeTopLevelItem(index);
    }
    
    // Remove from categories list
    m_categories.removeOne(categoryName);
    
    emit structureChanged();
}

bool qtree_Tasklists_list::renameCategory(const QString& oldName, const QString& newName)
{
    qDebug() << "qtree_Tasklists_list: Renaming category from" << oldName << "to" << newName;
    
    // Validate new name
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(newName, InputValidation::InputType::PlainText);
    if (!result.isValid) {
        qWarning() << "qtree_Tasklists_list: Invalid category name:" << result.errorMessage;
        return false;
    }
    
    // Check if new name already exists
    if (findCategory(newName)) {
        qWarning() << "qtree_Tasklists_list: Category already exists:" << newName;
        return false;
    }
    
    QTreeWidgetItem* categoryItem = findCategory(oldName);
    if (!categoryItem) {
        return false;
    }
    
    categoryItem->setText(0, newName);
    
    // Update categories list
    m_categories.removeOne(oldName);
    m_categories.append(newName);
    
    emit categoryRenamed(oldName, newName);
    emit structureChanged();
    return true;
}

QTreeWidgetItem* qtree_Tasklists_list::addTasklist(const QString& tasklistName, const QString& categoryName)
{
    qDebug() << "qtree_Tasklists_list: Adding tasklist:" << tasklistName << "to category:" << categoryName;
    
    // Validate tasklist name
    InputValidation::ValidationResult result = 
        InputValidation::validateInput(tasklistName, InputValidation::InputType::TaskListName);
    if (!result.isValid) {
        qWarning() << "qtree_Tasklists_list: Invalid tasklist name:" << result.errorMessage;
        return nullptr;
    }
    
    // Check if tasklist already exists
    if (findTasklist(tasklistName)) {
        qDebug() << "qtree_Tasklists_list: Tasklist already exists:" << tasklistName;
        return findTasklist(tasklistName);
    }
    
    // Category is required - must exist
    QTreeWidgetItem* categoryItem = findCategory(categoryName);
    if (!categoryItem) {
        qWarning() << "qtree_Tasklists_list: Category does not exist:" << categoryName;
        return nullptr;
    }
    
    // Create tasklist item
    QTreeWidgetItem* tasklistItem = new QTreeWidgetItem(categoryItem);
    tasklistItem->setText(0, tasklistName);
    tasklistItem->setData(0, CategoryRole, false);
    
    // Set appearance for tasklist
    tasklistItem->setForeground(0, QColor(255, 255, 255));
    
    emit structureChanged();
    return tasklistItem;
}

void qtree_Tasklists_list::moveTasklistToCategory(const QString& tasklistName, const QString& categoryName)
{
    qDebug() << "qtree_Tasklists_list: Moving tasklist:" << tasklistName << "to category:" << categoryName;
    
    QTreeWidgetItem* tasklistItem = findTasklist(tasklistName);
    if (!tasklistItem) {
        qWarning() << "qtree_Tasklists_list: Tasklist not found:" << tasklistName;
        return;
    }
    
    // Get current category
    QTreeWidgetItem* currentCategory = tasklistItem->parent();
    if (!currentCategory) {
        qWarning() << "qtree_Tasklists_list: Tasklist has no parent category";
        return;
    }
    QString oldCategoryName = currentCategory->text(0);
    
    // If moving to the same category, do nothing
    if (oldCategoryName == categoryName) {
        return;
    }
    
    // Get or create new category
    QTreeWidgetItem* newCategory = getOrCreateCategory(categoryName);
    if (!newCategory) {
        return;
    }
    
    // Take the item from current parent (removes but keeps alive)
    int index = currentCategory->indexOfChild(tasklistItem);
    QTreeWidgetItem* takenItem = currentCategory->takeChild(index);
    
    if (!takenItem) {
        qWarning() << "qtree_Tasklists_list: Failed to take item from parent";
        return;
    }
    
    // Add to new category
    newCategory->addChild(takenItem);
    
    // Ensure new category is expanded
    newCategory->setExpanded(true);
    
    // Update the display
    this->update();
    
    emit tasklistMoved(tasklistName, oldCategoryName, categoryName);
    emit structureChanged();
}

QTreeWidgetItem* qtree_Tasklists_list::findTasklist(const QString& tasklistName)
{
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* categoryItem = topLevelItem(i);
        if (categoryItem && isCategory(categoryItem)) {
            for (int j = 0; j < categoryItem->childCount(); ++j) {
                QTreeWidgetItem* tasklistItem = categoryItem->child(j);
                if (tasklistItem && tasklistItem->text(0) == tasklistName) {
                    return tasklistItem;
                }
            }
        }
    }
    return nullptr;
}

QString qtree_Tasklists_list::getTasklistCategory(const QString& tasklistName)
{
    QTreeWidgetItem* tasklistItem = findTasklist(tasklistName);
    if (tasklistItem && tasklistItem->parent()) {
        return tasklistItem->parent()->text(0);
    }
    return QString(); // Empty string if no category (shouldn't happen)
}

QStringList qtree_Tasklists_list::getAllCategories() const
{
    QStringList categories;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (item && isCategory(const_cast<qtree_Tasklists_list*>(this)->topLevelItem(i))) {
            categories.append(item->text(0));
        }
    }
    return categories;
}

QStringList qtree_Tasklists_list::getTasklistsInCategory(const QString& categoryName) const
{
    QStringList tasklists;
    QTreeWidgetItem* categoryItem = const_cast<qtree_Tasklists_list*>(this)->findCategory(categoryName);
    if (categoryItem) {
        for (int i = 0; i < categoryItem->childCount(); ++i) {
            QTreeWidgetItem* tasklistItem = categoryItem->child(i);
            if (tasklistItem) {
                tasklists.append(tasklistItem->text(0));
            }
        }
    }
    return tasklists;
}

QJsonDocument qtree_Tasklists_list::saveStructureToJson() const
{
    qDebug() << "qtree_Tasklists_list: Saving structure to JSON";
    
    QJsonArray categoriesArray;
    
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* categoryItem = topLevelItem(i);
        if (categoryItem && isCategory(const_cast<qtree_Tasklists_list*>(this)->topLevelItem(i))) {
            QJsonObject categoryObj;
            categoryObj["name"] = categoryItem->text(0);
            
            QJsonArray tasklistsArray;
            for (int j = 0; j < categoryItem->childCount(); ++j) {
                QTreeWidgetItem* tasklistItem = categoryItem->child(j);
                if (tasklistItem) {
                    tasklistsArray.append(tasklistItem->text(0));
                }
            }
            categoryObj["tasklists"] = tasklistsArray;
            
            categoriesArray.append(categoryObj);
        }
    }
    
    QJsonObject root;
    root["categories"] = categoriesArray;
    
    return QJsonDocument(root);
}

bool qtree_Tasklists_list::loadStructureFromJson(const QJsonDocument& doc)
{
    qDebug() << "qtree_Tasklists_list: Loading structure from JSON";
    
    if (!doc.isObject()) {
        qWarning() << "qtree_Tasklists_list: Invalid JSON document";
        return false;
    }
    
    QJsonObject root = doc.object();
    if (!root.contains("categories") || !root["categories"].isArray()) {
        qWarning() << "qtree_Tasklists_list: Missing or invalid categories array";
        return false;
    }
    
    // Clear tracked pointers before clearing the tree
    m_draggedItem = nullptr;
    m_lastSelectedItem = nullptr;
    
    // Clear current structure
    clear();
    m_categories.clear();
    
    // Load categories and tasklists
    QJsonArray categoriesArray = root["categories"].toArray();
    
    for (const QJsonValue& value : categoriesArray) {
        if (!value.isObject()) continue;
        
        QJsonObject categoryObj = value.toObject();
        QString categoryName = categoryObj["name"].toString();
        
        if (categoryName.isEmpty()) continue;
        
        // Create category
        QTreeWidgetItem* categoryItem = addCategory(categoryName);
        if (!categoryItem) continue;
        
        // Add tasklists to category
        if (categoryObj.contains("tasklists") && categoryObj["tasklists"].isArray()) {
            QJsonArray tasklistsArray = categoryObj["tasklists"].toArray();
            for (const QJsonValue& tasklistValue : tasklistsArray) {
                QString tasklistName = tasklistValue.toString();
                if (!tasklistName.isEmpty()) {
                    addTasklist(tasklistName, categoryName);
                }
            }
        }
    }
    
    emit structureChanged();
    return true;
}

void qtree_Tasklists_list::setItemAsCategory(QTreeWidgetItem* item, bool isCategory)
{
    if (item) {
        item->setData(0, CategoryRole, isCategory);
        
        if (isCategory) {
            // Set appearance for category
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setForeground(0, QColor(180, 180, 180));
        } else {
            // Set appearance for tasklist
            QFont font = item->font(0);
            font.setBold(false);
            item->setFont(0, font);
            item->setForeground(0, QColor(255, 255, 255));
        }
    }
}

bool qtree_Tasklists_list::isCategory(QTreeWidgetItem* item) const
{
    if (!item) return false;
    return item->data(0, CategoryRole).toBool();
}

bool qtree_Tasklists_list::isTasklist(QTreeWidgetItem* item) const
{
    if (!item) return false;
    return !isCategory(item) && item->parent() != nullptr;
}

QString qtree_Tasklists_list::getTasklistDisplayName(QTreeWidgetItem* item) const
{
    if (item && isTasklist(item)) {
        return item->text(0);
    }
    return QString();
}

void qtree_Tasklists_list::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_dragDropEnabled) {
        event->ignore();
        return;
    }
    
    if (event->mimeData()->hasFormat(MIME_TYPE_TASKLIST) || 
        event->mimeData()->hasFormat(MIME_TYPE_CATEGORY) ||
        event->mimeData()->hasFormat("application/x-task-data")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void qtree_Tasklists_list::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_dragDropEnabled) {
        event->ignore();
        return;
    }
    
    QTreeWidgetItem* item = itemAt(event->position().toPoint());
    
    // Handle category dragging
    if (event->mimeData()->hasFormat(MIME_TYPE_CATEGORY)) {
        // Get the dragged category name from mime data
        QByteArray encodedData = event->mimeData()->data(MIME_TYPE_CATEGORY);
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        QString draggedCategoryName;
        stream >> draggedCategoryName;
        
        QTreeWidgetItem* draggedCategory = findCategory(draggedCategoryName);
        
        // Categories can be reordered at the root level
        if (!item || isCategory(item)) {
            // Either dropping at empty space or between categories
            event->acceptProposedAction();
            setDropIndicatorShown(true);
        } else if (isTasklist(item)) {
            // Check if this tasklist belongs to the dragged category
            QTreeWidgetItem* parentCategory = item->parent();
            if (parentCategory && parentCategory == draggedCategory) {
                // Can't drop a category on its own tasklists
                event->ignore();
                setDropIndicatorShown(false);
            } else {
                // Allow dropping on tasklists - will insert after their parent category
                event->acceptProposedAction();
                setDropIndicatorShown(true);
            }
        } else {
            event->ignore();
            setDropIndicatorShown(false);
        }
        return;
    }
    
    // Handle external task dragging
    if (event->mimeData()->hasFormat("application/x-task-data")) {
        if (!item) {
            event->ignore();
            return;
        }
        
        // Can only drop tasks on tasklists, not categories
        if (isTasklist(item)) {
            event->acceptProposedAction();
            setDropIndicatorShown(true);
        } else if (isCategory(item)) {
            // Visual feedback that categories don't accept tasks directly
            event->ignore();
            setDropIndicatorShown(false);
        } else {
            event->ignore();
            setDropIndicatorShown(false);
        }
        return;
    }
    
    // Handle tasklist dragging
    if (event->mimeData()->hasFormat(MIME_TYPE_TASKLIST)) {
        if (!item) {
            event->ignore();
            return;
        }
        
        if (isCategory(item)) {
            // Can drop on category
            event->acceptProposedAction();
            setDropIndicatorShown(true);
        } else if (isTasklist(item)) {
            // Can drop on tasklist for reordering within same category
            event->acceptProposedAction();
            setDropIndicatorShown(true);
            
            // Provide visual feedback for drop position
            QRect itemRect = visualItemRect(item);
            QPoint dropPos = event->position().toPoint();
            
            // Update drop indicator position based on where we're hovering
            if (dropPos.y() < itemRect.center().y()) {
                // Hovering over upper half - will insert before
                setDropIndicatorShown(true);
            } else {
                // Hovering over lower half - will insert after
                setDropIndicatorShown(true);
            }
        } else {
            event->ignore();
            setDropIndicatorShown(false);
        }
    } else {
        event->ignore();
        setDropIndicatorShown(false);
    }
}

void qtree_Tasklists_list::dropEvent(QDropEvent *event)
{
    if (!m_dragDropEnabled) {
        event->ignore();
        return;
    }
    
    const QMimeData* mimeData = event->mimeData();
    
    // Handle external task drop
    if (mimeData->hasFormat("application/x-task-data")) {
        QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
        
        if (!targetItem || !isTasklist(targetItem)) {
            event->ignore();
            return;
        }
        
        // Get the target tasklist name
        QString targetTasklist = targetItem->text(0);
        
        // Parse the task data
        QByteArray taskData = mimeData->data("application/x-task-data");
        QJsonDocument doc = QJsonDocument::fromJson(taskData);
        
        if (!doc.isObject()) {
            event->ignore();
            return;
        }
        
        QJsonObject taskJson = doc.object();
        QString taskName = taskJson["name"].toString();
        QString taskId = taskJson["taskId"].toString();
        bool completed = taskJson["completed"].toBool();
        
        // Emit signal to handle the task transfer
        emit taskDroppedOnTasklist(taskName, taskId, targetTasklist, taskJson);
        
        event->acceptProposedAction();
        return;
    }
    
    // Handle category reordering
    if (mimeData->hasFormat(MIME_TYPE_CATEGORY)) {
        QByteArray encodedData = mimeData->data(MIME_TYPE_CATEGORY);
        QDataStream stream(&encodedData, QIODevice::ReadOnly);
        QString categoryName;
        stream >> categoryName;
        
        QTreeWidgetItem* categoryItem = findCategory(categoryName);
        if (!categoryItem) {
            event->ignore();
            return;
        }
        
        // Find target position
        QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
        int targetIndex = -1;
        
        if (!targetItem) {
            // Dropping at the end of the list
            targetIndex = topLevelItemCount();
        } else if (isCategory(targetItem)) {
            // Dropping on or near another category
            targetIndex = indexOfTopLevelItem(targetItem);
            
            // Check if we're dropping below the target
            QRect itemRect = visualItemRect(targetItem);
            QPoint dropPos = event->position().toPoint();
            if (dropPos.y() > itemRect.center().y()) {
                targetIndex++;
            }
        } else if (isTasklist(targetItem)) {
            // Dropping on a tasklist - insert after its parent category
            QTreeWidgetItem* parentCategory = targetItem->parent();
            if (!parentCategory) {
                event->ignore();
                return;
            }
            
            // Don't allow dropping a category on its own tasklists
            if (parentCategory == categoryItem) {
                event->ignore();
                return;
            }
            
            // Insert after the parent category
            targetIndex = indexOfTopLevelItem(parentCategory) + 1;
        } else {
            // Invalid drop target
            event->ignore();
            return;
        }
        
        // Get current index
        int currentIndex = indexOfTopLevelItem(categoryItem);
        if (currentIndex < 0) {
            event->ignore();
            return;
        }
        
        // Save the expanded state before moving
        bool wasExpanded = categoryItem->isExpanded();
        
        // Adjust target index if moving down
        if (currentIndex < targetIndex) {
            targetIndex--;
        }
        
        // Don't move if it's the same position
        if (currentIndex == targetIndex) {
            return;
        }
        
        // Take the item from its current position
        QTreeWidgetItem* takenItem = takeTopLevelItem(currentIndex);
        if (!takenItem) {
            event->ignore();
            return;
        }
        
        // Insert at the new position
        insertTopLevelItem(targetIndex, takenItem);
        
        // Restore the expanded state
        takenItem->setExpanded(wasExpanded);
        
        // Select the moved category
        setCurrentItem(takenItem);
        
        emit structureChanged();
        return;
    }
    
    // Handle tasklist moving (existing code)
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }
    
    if (!mimeData->hasFormat(MIME_TYPE_TASKLIST)) {
        event->ignore();
        return;
    }
    
    // Get the dragged tasklist name
    QByteArray encodedData = mimeData->data(MIME_TYPE_TASKLIST);
    QDataStream stream(&encodedData, QIODevice::ReadOnly);
    QString tasklistName;
    stream >> tasklistName;
    
    QTreeWidgetItem* tasklistItem = findTasklist(tasklistName);
    if (!tasklistItem) {
        event->ignore();
        return;
    }
    
    // Determine target category and position
    QTreeWidgetItem* targetCategory = nullptr;
    int targetIndex = -1;
    
    if (isCategory(targetItem)) {
        // Dropping on a category - add to end
        targetCategory = targetItem;
        targetIndex = targetCategory->childCount();
    } else if (isTasklist(targetItem)) {
        // Dropping on a tasklist - insert at that position
        targetCategory = targetItem->parent();
        if (targetCategory) {
            targetIndex = targetCategory->indexOfChild(targetItem);
            
            // Determine if we're dropping above or below the target item
            // Get the visual rectangle of the target item
            QRect itemRect = visualItemRect(targetItem);
            QPoint dropPos = event->position().toPoint();
            
            // If dropping in the lower half of the item, insert after it
            if (dropPos.y() > itemRect.center().y()) {
                targetIndex++;
            }
        }
    }
    
    if (!targetCategory) {
        event->ignore();
        return;
    }
    
    // Get current parent
    QTreeWidgetItem* currentParent = tasklistItem->parent();
    if (!currentParent) {
        // Tasklist must have a parent category
        qWarning() << "qtree_Tasklists_list: Tasklist has no parent category, cannot drop";
        event->ignore();
        return;
    }
    
    QString oldCategoryName = currentParent->text(0);
    QString newCategoryName = targetCategory->text(0);
    
    // If moving within the same category, adjust the target index
    if (currentParent == targetCategory) {
        int currentIndex = currentParent->indexOfChild(tasklistItem);
        if (currentIndex < targetIndex) {
            // Moving down - adjust index since we'll remove the item first
            targetIndex--;
        }
    }
    
    // Remove from current position
    currentParent->removeChild(tasklistItem);
    
    // Insert at new position
    if (targetIndex >= 0 && targetIndex <= targetCategory->childCount()) {
        targetCategory->insertChild(targetIndex, tasklistItem);
    } else {
        targetCategory->addChild(tasklistItem);
    }
    
    // Expand the target category to show the moved item
    targetCategory->setExpanded(true);
    
    // Select the moved item
    this->setCurrentItem(tasklistItem);
    
    // Notify about the move
    if (oldCategoryName != newCategoryName) {
        emit tasklistMoved(tasklistName, oldCategoryName, newCategoryName);
    }
    
    emit structureChanged();
    //event->acceptProposedAction();
}

void qtree_Tasklists_list::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    m_draggedItem = nullptr;
}

void qtree_Tasklists_list::mousePressEvent(QMouseEvent *event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    
    if (item && event->button() == Qt::LeftButton && m_dragDropEnabled) {
        // Allow dragging both tasklists and categories
        if (isTasklist(item) || isCategory(item)) {
            m_draggedItem = item;
        } else {
            m_draggedItem = nullptr;  // Clear if neither
        }
    } else {
        m_draggedItem = nullptr;  // Clear on other buttons or conditions
    }
    
    QTreeWidget::mousePressEvent(event);
}

void qtree_Tasklists_list::mouseDoubleClickEvent(QMouseEvent *event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    
    if (item) {
        emit itemDoubleClicked(item);
    }
    
    QTreeWidget::mouseDoubleClickEvent(event);
}

void qtree_Tasklists_list::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);
    
    if (!selected.isEmpty()) {
        QModelIndex index = selected.indexes().first();
        QTreeWidgetItem* item = itemFromIndex(index);
        
        if (item) {
            m_lastSelectedItem = item;
            
            if (isCategory(item)) {
                emit categorySelected(item->text(0));
            } else if (isTasklist(item)) {
                emit tasklistSelected(item->text(0));
            }
        }
    }
}

QMimeData* qtree_Tasklists_list::mimeData(const QList<QTreeWidgetItem*>& items) const
{
    if (items.isEmpty()) {
        return nullptr;
    }
    
    QTreeWidgetItem* item = items.first();
    QMimeData* mimeData = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    
    if (isCategory(item)) {
        // Store category name
        stream << item->text(0);
        mimeData->setData(MIME_TYPE_CATEGORY, encodedData);
    } else if (isTasklist(item)) {
        // Store tasklist name
        stream << item->text(0);
        mimeData->setData(MIME_TYPE_TASKLIST, encodedData);
    } else {
        delete mimeData;
        return nullptr;
    }
    
    return mimeData;
}

QStringList qtree_Tasklists_list::mimeTypes() const
{
    QStringList types;
    types << MIME_TYPE_TASKLIST;
    types << MIME_TYPE_CATEGORY;
    return types;
}

bool qtree_Tasklists_list::dropMimeData(QTreeWidgetItem *parent, int index, const QMimeData *data, Qt::DropAction action)
{
    Q_UNUSED(parent);
    Q_UNUSED(index);
    Q_UNUSED(action);
    
    // We handle drops in dropEvent instead
    return false;
}

void qtree_Tasklists_list::contextMenuEvent(QContextMenuEvent *event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    
    // Emit signal for external handling
    emit contextMenuRequested(event->pos());
    
    // You can also handle it internally if needed
    QTreeWidget::contextMenuEvent(event);
}

QTreeWidgetItem* qtree_Tasklists_list::getCategoryItem(QTreeWidgetItem* item) const
{
    if (!item) return nullptr;
    
    if (isCategory(item)) {
        return item;
    } else if (item->parent() && isCategory(item->parent())) {
        return item->parent();
    }
    
    return nullptr;
}

void qtree_Tasklists_list::updateItemAppearance(QTreeWidgetItem* item)
{
    if (!item) return;
    
    if (isCategory(item)) {
        // Set appearance for category
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setForeground(0, QColor(180, 180, 180));
    } else {
        // Set appearance for tasklist
        QFont font = item->font(0);
        font.setBold(false);
        item->setFont(0, font);
        item->setForeground(0, QColor(255, 255, 255));
    }
}

bool qtree_Tasklists_list::canDropOn(QTreeWidgetItem* item, const QMimeData* data) const
{
    if (!data) return false;
    
    if (data->hasFormat(MIME_TYPE_CATEGORY)) {
        // Categories can be dropped at root level, on other categories, 
        // or on tasklists (which will place them after the tasklist's parent category)
        return !item || isCategory(item) || isTasklist(item);
    }
    
    if (data->hasFormat(MIME_TYPE_TASKLIST)) {
        // Can drop tasklists on categories or other tasklists (for reordering)
        return item && (isCategory(item) || isTasklist(item));
    }
    
    return false;
}

void qtree_Tasklists_list::clearTrackedItem(QTreeWidgetItem* item)
{
    if (!item) return;
    
    // Clear tracked pointers if they match the item being removed
    if (m_draggedItem == item) {
        m_draggedItem = nullptr;
    }
    
    if (m_lastSelectedItem == item) {
        m_lastSelectedItem = nullptr;
    }
    
    // Also check children recursively
    for (int i = 0; i < item->childCount(); ++i) {
        clearTrackedItem(item->child(i));
    }
}
