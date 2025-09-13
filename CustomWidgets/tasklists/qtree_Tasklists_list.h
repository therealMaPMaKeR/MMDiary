#ifndef QTREE_TASKLISTS_LIST_H
#define QTREE_TASKLISTS_LIST_H

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QDebug>
#include <QPointer>
#include <QMenu>
#include <QAction>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../../Operations-Global/ThreadSafeContainers.h"
#include "../../Operations-Global/SafeTimer.h"

class qtree_Tasklists_list : public QTreeWidget
{
    Q_OBJECT

public:
    explicit qtree_Tasklists_list(QWidget *parent = nullptr);
    ~qtree_Tasklists_list();

    // Category management
    QTreeWidgetItem* addCategory(const QString& categoryName);
    QTreeWidgetItem* findCategory(const QString& categoryName);
    QTreeWidgetItem* getOrCreateCategory(const QString& categoryName);
    void removeCategory(const QString& categoryName);
    bool renameCategory(const QString& oldName, const QString& newName);
    
    // Tasklist management
    QTreeWidgetItem* addTasklist(const QString& tasklistName, const QString& categoryName);
    void moveTasklistToCategory(const QString& tasklistName, const QString& categoryName);
    QTreeWidgetItem* findTasklist(const QString& tasklistName);
    QString getTasklistCategory(const QString& tasklistName);
    
    // Get all categories
    QStringList getAllCategories() const;
    
    // Get tasklists in a category
    QStringList getTasklistsInCategory(const QString& categoryName) const;
    
    // Save/Load structure
    QJsonDocument saveStructureToJson() const;
    bool loadStructureFromJson(const QJsonDocument& doc);
    
    // Set whether an item is a category (used during initialization)
    void setItemAsCategory(QTreeWidgetItem* item, bool isCategory = true);
    bool isCategory(QTreeWidgetItem* item) const;
    
    // Get the display name for a tasklist (without category)
    QString getTasklistDisplayName(QTreeWidgetItem* item) const;
    
    // Enable/disable drag and drop
    void setDragDropEnabled(bool enabled);

    // Get all tasklists (for deletion confirmation)
    QStringList getAllTasklists() const;

signals:
    // Emitted when the structure changes (categories or tasklist order)
    void structureChanged();
    
    // Emitted when a tasklist is selected
    void tasklistSelected(const QString& tasklistName);
    
    // Emitted when a category is selected
    void categorySelected(const QString& categoryName);
    
    // Emitted when a tasklist is moved to a different category
    void tasklistMoved(const QString& tasklistName, const QString& oldCategory, const QString& newCategory);
    
    // Emitted when a category is renamed
    void categoryRenamed(const QString& oldName, const QString& newName);
    
    // Emitted when an item is double-clicked for renaming
    void itemDoubleClicked(QTreeWidgetItem* item);
    
    // Context menu requested
    void contextMenuRequested(const QPoint& pos);

protected:
    // Override drag and drop events
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    
    // Override mouse events
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    
    // Override selection
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    
    // Create mime data for drag operation
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    
    // Supported mime types
    QStringList mimeTypes() const override;
    
    // Can we drop on this item?
    bool dropMimeData(QTreeWidgetItem *parent, int index, const QMimeData *data, Qt::DropAction action) override;
    
    // Context menu
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // Helper functions
    void setupWidget();
    bool isTasklist(QTreeWidgetItem* item) const;
    QTreeWidgetItem* getCategoryItem(QTreeWidgetItem* item) const;
    void updateItemAppearance(QTreeWidgetItem* item);
    bool canDropOn(QTreeWidgetItem* item, const QMimeData* data) const;
    void clearTrackedItem(QTreeWidgetItem* item);  // Clear tracked pointers when item is deleted
    
    // Track categories using thread-safe container
    ThreadSafeList<QString> m_categories;
    
    // Store which items are categories (using UserRole data)
    static constexpr int CategoryRole = Qt::UserRole + 1;
    static constexpr int TasklistPathRole = Qt::UserRole + 2;
    
    // Drag and drop state
    // Note: QTreeWidgetItem doesn't inherit QObject, so we use raw pointers with careful cleanup
    QTreeWidgetItem* m_draggedItem;
    bool m_dragDropEnabled;
    
    // Last selected item tracking
    // Note: QTreeWidgetItem doesn't inherit QObject, so we use raw pointers with careful cleanup
    QTreeWidgetItem* m_lastSelectedItem;
    
    // Context menu actions
    QAction* m_actionRename;
    QAction* m_actionDelete;
    QAction* m_actionNewCategory;
    QAction* m_actionNewTasklist;
    
    // Constants
    static constexpr const char* MIME_TYPE_TASKLIST = "application/x-tasklist-item";
    static constexpr const char* MIME_TYPE_CATEGORY = "application/x-category-item";
};

#endif // QTREE_TASKLISTS_LIST_H
