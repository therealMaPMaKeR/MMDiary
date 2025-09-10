#ifndef QLIST_VP_SHOWSLIST_H
#define QLIST_VP_SHOWSLIST_H

#include <QListWidget>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QScrollBar>

class qlist_VP_ShowsList : public QListWidget
{
    Q_OBJECT

public:
    explicit qlist_VP_ShowsList(QWidget *parent = nullptr);
    ~qlist_VP_ShowsList();

    // Set the scroll speed multiplier for icon view mode
    void setIconViewScrollMultiplier(double multiplier);
    double getIconViewScrollMultiplier() const { return m_iconViewScrollMultiplier; }

    // Check if we're in icon view mode
    bool isIconViewMode() const;

protected:
    // Override wheel event to handle custom scrolling speed
    void wheelEvent(QWheelEvent *event) override;
    
    // Override mouse press event to handle clicking on empty space
    void mousePressEvent(QMouseEvent *event) override;

signals:
    // Signal emitted when selection is cleared by clicking on empty space
    void selectionCleared();

private:
    double m_iconViewScrollMultiplier;  // Multiplier for scroll speed in icon view mode
    double m_listViewScrollMultiplier;  // Multiplier for scroll speed in list view mode (usually 1.0)
};

#endif // QLIST_VP_SHOWSLIST_H
