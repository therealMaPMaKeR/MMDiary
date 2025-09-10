#include "qlist_DiaryTextDisplay.h"
#include "qtextedit_DiaryTextInput.h"
#include "../../constants.h"
#include "../../Operations-Global/SafeTimer.h"
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFont>
#include <QFontMetrics>
#include <QTextDocument>
#include <QListWidgetItem>
#include <QDebug>
#include "../../Operations-Global/inputvalidation.h" // Add this include

qlist_DiaryTextDisplay::qlist_DiaryTextDisplay(QWidget *parent)
    : QListWidget(parent)
    , m_inSizeUpdate(false)
    , m_inMouseEvent(false)
    , m_resizeTimer(nullptr)
{
    qDebug() << "qlist_DiaryTextDisplay: Constructor called";
    this->setParent(parent);
    this->show();
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // Enable drag & drop
    setAcceptDrops(true);

    // Use a delay to re-enable drag & drop in case something else disables it
    // Store the timer pointer to ensure proper cleanup
    m_dragDropTimer = new SafeTimer(this, "qlist_DiaryTextDisplay::dragDropTimer");
    m_dragDropTimer->setSingleShot(true);
    m_dragDropTimer->start(100, [this]() {
        if (this->isVisible()) {  // Only if widget still exists and is visible
            setAcceptDrops(true);
        }
    });
    
    // Create resize timer for coalescing rapid resize events
    m_resizeTimer = new SafeTimer(this, "qlist_DiaryTextDisplay::resizeTimer");
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(100); // 100ms delay to coalesce resize events
}

qlist_DiaryTextDisplay::~qlist_DiaryTextDisplay()
{
    qDebug() << "qlist_DiaryTextDisplay: Destructor called";
    
    // Stop any pending resize updates
    if (m_resizeTimer) {
        m_resizeTimer->stop();
        delete m_resizeTimer;
        m_resizeTimer = nullptr;
    }
    
    // Make sure the "finished" signal is emitted if we're destroyed during an update
    if (m_inSizeUpdate) {
        m_inSizeUpdate = false;
        emit sizeUpdateFinished();
    }
    
    // Clean up timer if it exists
    if (m_dragDropTimer) {
        m_dragDropTimer->stop();
        delete m_dragDropTimer;
        m_dragDropTimer = nullptr;
    }
    
    qDebug() << "qlist_DiaryTextDisplay: Destructor completed";
}

// Add this after the constructor
void qlist_DiaryTextDisplay::leaveEvent(QEvent *event)
{
    if (!m_inMouseEvent) {
        m_inMouseEvent = true;
        clearSelection();
        m_inMouseEvent = false;
    }
    QListWidget::leaveEvent(event);
}

void qlist_DiaryTextDisplay::enterEvent(QEnterEvent *event)
{
    // Handle enter event if needed in the future
    QListWidget::enterEvent(event);
}

void qlist_DiaryTextDisplay::selectLastItem()
{
    qDebug() << "qlist_DiaryTextDisplay: selectLastItem() called";
    if (count() > 0) {
        QListWidgetItem* lastItem = item(count() - 1);
        if (lastItem && (lastItem->flags() & Qt::ItemIsEnabled)) {
            setCurrentItem(lastItem);
        }
    }
}

void qlist_DiaryTextDisplay::wheelEvent(QWheelEvent *event)
{
    if (!event) {
        qWarning() << "qlist_DiaryTextDisplay: Null event in wheelEvent";
        return;
    }
    
    if (event->modifiers() & Qt::ControlModifier) {
        qDebug() << "qlist_DiaryTextDisplay: Wheel event with Ctrl modifier";
        
        // Prevent nested updates
        if (m_inSizeUpdate) {
            qDebug() << "qlist_DiaryTextDisplay: Already in size update, skipping";
            event->ignore();
            return;
        }
        
        m_inSizeUpdate = true;
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
            if (item) {  // Null check
                item->setFont(font);
            }
        }

        // Update sizes directly
        updateItemSizes();

        // Always signal we're done, regardless of what happened
        m_inSizeUpdate = false;
        emit sizeUpdateFinished();

        event->accept();
    } else {
        QListWidget::wheelEvent(event); // Pass normal wheel events
    }
}

void qlist_DiaryTextDisplay::resizeEvent(QResizeEvent *event)
{
    qDebug() << "qlist_DiaryTextDisplay: resizeEvent called";
    
    // Let the base class handle the resize immediately
    QListWidget::resizeEvent(event);
    
    // Defer the size update to coalesce rapid resize events
    // This prevents crashes from multiple rapid resizes
    if (m_resizeTimer && !m_inSizeUpdate) {
        m_resizeTimer->stop();
        m_resizeTimer->start([this]() {
            performDeferredSizeUpdate();
        });
    }
}

void qlist_DiaryTextDisplay::performDeferredSizeUpdate()
{
    qDebug() << "qlist_DiaryTextDisplay: performDeferredSizeUpdate called";
    
    // Check if widget is still valid
    if (!this->isVisible() || m_inSizeUpdate) {
        return;
    }
    
    m_inSizeUpdate = true;
    emit sizeUpdateStarted();
    
    // Perform the actual size update
    updateItemSizes();
    
    m_inSizeUpdate = false;
    emit sizeUpdateFinished();
}

bool qlist_DiaryTextDisplay::eventFilter(QObject *obj, QEvent *event)
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

void qlist_DiaryTextDisplay::keyPressEvent(QKeyEvent *event)
{
    // for some reason this is also needed for the event filter to work in main window
    // Empty implementation as per the provided source code
}

void qlist_DiaryTextDisplay::mousePressEvent(QMouseEvent *event)
{
    if (!event) {
        qWarning() << "qlist_DiaryTextDisplay: Null event in mousePressEvent";
        return;
    }
    
    if (event->button() == Qt::LeftButton) {
        // Store the click position for later use
        m_lastClickPos = event->pos();
    }

    // Call base class implementation
    QListWidget::mousePressEvent(event);
}

void qlist_DiaryTextDisplay::UpdateFontSize_Slot(int size, bool resize)
{
    qDebug() << "qlist_DiaryTextDisplay: UpdateFontSize_Slot called with size:" << size;
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
    if (QWidget* editor = this->findChild<qtextedit_DiaryTextInput*>()) {
        qtextedit_DiaryTextInput* textEdit = qobject_cast<qtextedit_DiaryTextInput*>(editor);
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

void qlist_DiaryTextDisplay::TextWasEdited(QString text, int itemIndex)
{
    qDebug() << "qlist_DiaryTextDisplay: TextWasEdited called for item:" << itemIndex;
    // Validate the edited text
    InputValidation::ValidationResult result =
        InputValidation::validateInput(text, InputValidation::InputType::DiaryContent, 10000);

    if (!result.isValid) {
        qWarning() << "qlist_DiaryTextDisplay: Text validation failed in TextWasEdited:" << result.errorMessage;
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

void qlist_DiaryTextDisplay::updateItemSizes()
{
    // Safety check for viewport
    if (!this->viewport()) {
        qWarning() << "qlist_DiaryTextDisplay: No viewport available in updateItemSizes";
        return;
    }
    
    QFont font = this->font();
    font.setPointSize(m_fontSize);

    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *item = this->item(i);
        if (!item) continue; // Skip null items

        // Check if this is an image item - if so, don't override its size hint
        bool isImageItem = item->data(Qt::UserRole+3).toBool();
        if (isImageItem) {
            // For image items, preserve the existing size hint
            // No font changes needed since there are no captions
            continue; // Skip size calculation for image items
        }

        // Get the item text
        QString text = item->text();
        // For items with colored text, we need special handling
        bool hasColoredText = item->data(Qt::UserRole+1).toBool();

        // Get viewport width with safety check
        int viewportWidth = this->viewport() ? this->viewport()->width() : 400;
        
        // Adjust item size hint based on current viewport width
        if (hasColoredText) {
            // For colored text items, create a text document to measure
            QTextDocument doc;
            doc.setDefaultFont(font);
            doc.setPlainText(text);
            // Calculate width of list widget viewport
            doc.setTextWidth(viewportWidth);
            QSize docSize = doc.size().toSize();
            item->setSizeHint(QSize(docSize.width(), docSize.height() + 0)); // Add padding
        } else {
            // For regular items, use font metrics
            QFontMetrics fm(font);
            QRect textRect = fm.boundingRect(0, 0, viewportWidth, 0,
                                             Qt::AlignLeft | Qt::TextWordWrap, text);
            item->setSizeHint(QSize(textRect.width() + 10, textRect.height() + 0)); // Add padding
        }
    }

    // Force layout update only if widget is visible
    if (this->isVisible()) {
        this->doItemsLayout();
    }
}

void qlist_DiaryTextDisplay::updateItemFonts()
{
    QFont font = this->font();
    font.setPointSize(m_fontSize);

    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *item = this->item(i);
        item->setFont(font);
    }

    // If an editor is currently open, update its font size too
    if (QWidget* editor = this->findChild<qtextedit_DiaryTextInput*>()) {
        qtextedit_DiaryTextInput* textEdit = qobject_cast<qtextedit_DiaryTextInput*>(editor);
        if (textEdit) {
            textEdit->updateFontSize(m_fontSize);
        }
    }

    // After updating fonts, update the item sizes
    updateItemSizes();
}

// Drag & Drop implementation
void qlist_DiaryTextDisplay::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        QStringList imagePaths;

        foreach(const QUrl &url, event->mimeData()->urls()) {
            QString filePath = url.toLocalFile();
            if (isImageFile(filePath)) {
                imagePaths.append(filePath);
            }
        }

        if (!imagePaths.isEmpty()) {
            event->acceptProposedAction();
            return;
        }
    }

    QListWidget::dragEnterEvent(event);
}

void qlist_DiaryTextDisplay::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QListWidget::dragMoveEvent(event);
    }
}

void qlist_DiaryTextDisplay::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        QStringList imagePaths;

        foreach(const QUrl &url, event->mimeData()->urls()) {
            QString filePath = url.toLocalFile();
            if (isImageFile(filePath)) {
                imagePaths.append(filePath);
            }
        }

        if (!imagePaths.isEmpty()) {
            emit imagesDropped(imagePaths);
            event->acceptProposedAction();
            return;
        }
    }

    QListWidget::dropEvent(event);
}

bool qlist_DiaryTextDisplay::isImageFile(const QString& filePath)
{
    if (!QFileInfo::exists(filePath)) {
        return false;
    }

    QStringList supportedFormats = getSupportedImageFormats();
    QString fileExtension = QFileInfo(filePath).suffix().toLower();

    return supportedFormats.contains(fileExtension);
}

QStringList qlist_DiaryTextDisplay::getSupportedImageFormats()
{
    return QStringList() << "png" << "jpg" << "jpeg" << "gif" << "bmp"
                         << "tiff" << "tif" << "webp" << "ico" << "svg";
}
