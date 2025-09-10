#ifndef QLIST_DATAENC_TAGS_H
#define QLIST_DATAENC_TAGS_H

#include <QListWidget>
#include <QMouseEvent>
#include <QListWidgetItem>
#include <QDebug>
#include <QPointer>

/**
 * @brief Custom QListWidget for Encrypted Data tags
 * 
 * This widget handles mouse clicks on tags:
 * - Left-click: Toggles the tag (check/uncheck)
 * - Right-click: Unchecks the tag (only if checked)
 */
class qlist_DataENC_Tags : public QListWidget
{
    Q_OBJECT

public:
    explicit qlist_DataENC_Tags(QWidget *parent = nullptr);
    ~qlist_DataENC_Tags();

protected:
    /**
     * @brief Override mouse press event to handle left/right clicks
     * @param event Mouse event
     */
    void mousePressEvent(QMouseEvent *event) override;

signals:
    /**
     * @brief Emitted when a tag's check state changes
     * @param item The item that changed
     */
    void tagCheckStateChanged(QListWidgetItem* item);

private:
    /**
     * @brief Check if an item is valid (still exists in the list)
     * @param item Item to validate
     * @return True if valid, false otherwise
     */
    bool isItemValid(QListWidgetItem* item) const;
};

#endif // QLIST_DATAENC_TAGS_H
