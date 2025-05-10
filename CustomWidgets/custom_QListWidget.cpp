#include "custom_QListWidget.h"
#include "custom_QTextEditWidget.h"
#include "../constants.h"
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFont>
#include <QFontMetrics>
#include <QTextDocument>
#include <QListWidgetItem>
#include <QDebug>
#include "../Operations-Global/inputvalidation.h" // Add this include

custom_QListWidget::custom_QListWidget(QWidget *parent)
    : QListWidget(parent), m_inSizeUpdate(false)
{
    this->setParent(parent);
    this->show();
    this->setContextMenuPolicy(Qt::CustomContextMenu);
}

custom_QListWidget::~custom_QListWidget()
{
    // Make sure the "finished" signal is emitted if we're destroyed during an update
    if (m_inSizeUpdate) {
        emit sizeUpdateFinished();
    }
}

// Add this after the constructor
void custom_QListWidget::leaveEvent(QEvent *event)
{
    if (!m_inMouseEvent) {
        m_inMouseEvent = true;
        clearSelection();
        m_inMouseEvent = false;
    }
    QListWidget::leaveEvent(event);
}

void custom_QListWidget::enterEvent(QEnterEvent *event)
{
    // Handle enter event if needed in the future
    QListWidget::enterEvent(event);
}

void custom_QListWidget::selectLastItem()
{
    if (count() > 0) {
        QListWidgetItem* lastItem = item(count() - 1);
        if (lastItem && (lastItem->flags() & Qt::ItemIsEnabled)) {
            setCurrentItem(lastItem);
        }
    }
}
void custom_QListWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // Signal we're starting a size update
        emit sizeUpdateStarted();

        int delta = event->angleDelta().y();
        if (delta > 0)
        {
            m_fontSize += 2; // Zoom in
            if (m_fontSize > 30) m_fontSize = 30; // Maximum font size
        } else
        {
            m_fontSize -= 2; // Zoom out
            if (m_fontSize < 10) m_fontSize = 10; // Minimum font size
        }

        // Use direct update instead of calling updateItemFonts to avoid nested updates
        QFont font = this->font();
        font.setPointSize(m_fontSize);

        // Update font for all items directly
        for (int i = 0; i < count(); ++i) {
            QListWidgetItem *item = this->item(i);
            item->setFont(font);
        }

        // Update sizes directly
        updateItemSizes();

        // Always signal we're done, regardless of what happened
        emit sizeUpdateFinished();

        event->accept();
    } else {
        QListWidget::wheelEvent(event); // Pass normal wheel events
    }
}

void custom_QListWidget::resizeEvent(QResizeEvent *event)
{
    // Signal we're starting a size update
    emit sizeUpdateStarted();

    // Let the base class handle the resize
    QListWidget::resizeEvent(event);

    // Do size updates
    updateItemSizes();

    // Always signal we're done, regardless of what happened
    emit sizeUpdateFinished();
}

bool custom_QListWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Scroll || event->type() == QEvent::KeyPress) {
        // Allow mouse wheel events
        if (event->type() == QEvent::Wheel) {
            return QObject::eventFilter(obj, event);
        }
        // Block other scroll events
        return true; // Consume the event
    }
    return QObject::eventFilter(obj, event);
}

void custom_QListWidget::keyPressEvent(QKeyEvent *event)
{
    // for some reason this is also needed for the event filter to work in main window
    // Empty implementation as per the provided source code
}

void custom_QListWidget::UpdateFontSize_Slot(int size, bool resize)
{
    // Only emit if we're not already in a size update
    bool wasInSizeUpdate = m_inSizeUpdate;

    if (!wasInSizeUpdate) {
        m_inSizeUpdate = true;
        emit sizeUpdateStarted();
    }

    if(resize)
    {
        m_fontSize = size; // if resize is true, set the zoom level to default font size.
    }

    // Update the font size for all items
    QFont font = this->font();
    font.setPointSize(m_fontSize);

    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *item = this->item(i);
        item->setFont(font);
    }

    // If an editor is currently open, update its font size too
    if (QWidget* editor = this->findChild<custom_QTextEditWidget*>()) {
        custom_QTextEditWidget* textEdit = qobject_cast<custom_QTextEditWidget*>(editor);
        if (textEdit) {
            textEdit->updateFontSize(m_fontSize);
        }
    }

    // Now update sizes
    updateItemSizes();

    // Signal end of size update only if we started one
    if (!wasInSizeUpdate && m_inSizeUpdate) {
        m_inSizeUpdate = false;
        emit sizeUpdateFinished();
    }
}

void custom_QListWidget::TextWasEdited(QString text, int itemIndex)
{
    // Validate the edited text
    InputValidation::ValidationResult result =
        InputValidation::validateInput(text, InputValidation::InputType::DiaryContent, 10000);

    if (!result.isValid) {
        qWarning() << "Text validation failed in TextWasEdited:" << result.errorMessage;
        // Do not process the edited text
        return;
    }

    // Process validated text
    int count = 0;
    for (QChar c : text) {
        if (c == '\n') {
            count++;
        }
    }

    // Rest of the existing logic...
    if(count > 0 && this->item(itemIndex - 1)->text() != Constants::Diary_TextBlockStart) {
        // Code for adding text block markers
    }
    else if(count == 0 && this->item(itemIndex - 1)->text() == Constants::Diary_TextBlockStart) {
        // Code for removing text block markers
    }

    // Only emit if we're not already in a size update
    bool wasInSizeUpdate = m_inSizeUpdate;

    if (!wasInSizeUpdate) {
        m_inSizeUpdate = true;
        emit sizeUpdateStarted();
    }

    updateItemFonts();

    // Signal end of size update only if we started one
    if (!wasInSizeUpdate && m_inSizeUpdate) {
        m_inSizeUpdate = false;
        emit sizeUpdateFinished();
    }
}

void custom_QListWidget::updateItemSizes()
{
    QFont font = this->font();
    font.setPointSize(m_fontSize);

    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *item = this->item(i);
        // Get the item text
        QString text = item->text();
        // For items with colored text, we need special handling
        bool hasColoredText = item->data(Qt::UserRole+1).toBool();

        // Adjust item size hint based on current viewport width
        if (hasColoredText) {
            // For colored text items, create a text document to measure
            QTextDocument doc;
            doc.setDefaultFont(font);
            doc.setPlainText(text);
            // Calculate width of list widget viewport
            int viewportWidth = this->viewport()->width() - 0; // Add margin
            doc.setTextWidth(viewportWidth);
            QSize docSize = doc.size().toSize();
            item->setSizeHint(QSize(docSize.width(), docSize.height() + 0)); // Add padding
        } else {
            // For regular items, use font metrics
            QFontMetrics fm(font);
            QRect textRect = fm.boundingRect(0, 0, this->viewport()->width() - 0, 0,
                                             Qt::AlignLeft | Qt::TextWordWrap, text);
            item->setSizeHint(QSize(textRect.width() + 10, textRect.height() + 0)); // Add padding
        }
    }

    // Force layout update
    this->doItemsLayout();
}

void custom_QListWidget::updateItemFonts()
{
    QFont font = this->font();
    font.setPointSize(m_fontSize);

    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *item = this->item(i);
        item->setFont(font);
    }

    // If an editor is currently open, update its font size too
    if (QWidget* editor = this->findChild<custom_QTextEditWidget*>()) {
        custom_QTextEditWidget* textEdit = qobject_cast<custom_QTextEditWidget*>(editor);
        if (textEdit) {
            textEdit->updateFontSize(m_fontSize);
        }
    }

    // After updating fonts, update the item sizes
    updateItemSizes();
}
