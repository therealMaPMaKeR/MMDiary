#include "CombinedDelegate.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QTextDocument>
#include "qtextedit_DiaryTextInput.h"
#include "inputvalidation.h"
#include "operations_files.h"
#include "CryptoUtils.h"
#include "mainwindow.h"

CombinedDelegate::CombinedDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , m_colorLength(5)
    , m_taskManagerLength(12)
    , m_textColor(QColor(255, 0, 0))
{
    qDebug() << "CombinedDelegate: Constructor called";
    // Connect our custom signal to our non-const slot
    connect(this, &CombinedDelegate::textCommitted, this, &CombinedDelegate::onEditorClosed);
}

void CombinedDelegate::setColorLength(int length) {
    qDebug() << "CombinedDelegate: setColorLength called with length:" << length;
    m_colorLength = length;
}

void CombinedDelegate::setTextColor(const QColor &color) {
    qDebug() << "CombinedDelegate: setTextColor called";
    m_textColor = color;
}

QWidget *CombinedDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    qDebug() << "CombinedDelegate: createEditor called for index:" << index.row();
    if (index.data(Qt::UserRole).toBool()) {
        return nullptr; // Don't create an editor if UserRole is true
    } else {
        qtextedit_DiaryTextInput *editor = new qtextedit_DiaryTextInput(parent);

        // Set the font from the item
        QFont itemFont = index.data(Qt::FontRole).value<QFont>();
        editor->setFont(itemFont);

        // Store the index as a property of the editor
        editor->setProperty("itemIndex", index.row());

        // Store connections to disconnect later
        auto textChangedConn = connect(editor, &QTextEdit::textChanged, this, &CombinedDelegate::editorTextChanged);
        auto commitDataConn = connect(this, &QAbstractItemDelegate::commitData, this, [this, editor, index]() {
            if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(editor)) {
                emit textCommitted(textEdit->toPlainText(), index.row());
            }
        });

        // Disconnect when editor closes
        connect(this, &QAbstractItemDelegate::closeEditor, this, [=]() {
            disconnect(textChangedConn);
            disconnect(commitDataConn);
        });

        return editor;
    }
}

void CombinedDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    qDebug() << "CombinedDelegate: setEditorData called for index:" << index.row();
    if (qtextedit_DiaryTextInput *textEdit = qobject_cast<qtextedit_DiaryTextInput*>(editor)) {
        textEdit->setPlainText(index.model()->data(index, Qt::EditRole).toString());

        // Set the font from the item
        QFont itemFont = index.data(Qt::FontRole).value<QFont>();
        textEdit->setFont(itemFont);

        // This will trigger adjustHeight via the textChanged signal
    }
}

void CombinedDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    qDebug() << "CombinedDelegate: setModelData called for index:" << index.row();
    if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(editor)) {
        // Add input validation before setting the data
        QString text = textEdit->document()->toPlainText();
        InputValidation::ValidationResult result =
            InputValidation::validateInput(text, InputValidation::InputType::DiaryContent, 10000);

        if (result.isValid) {
            model->setData(index, text, Qt::EditRole);
        } else {
            qWarning() << "CombinedDelegate: Input validation failed:" << result.errorMessage;
            // Do not set the data if validation fails
            // Instead, keep the previous value
            // We could also inform the user here about the validation failure
        }
    }
}

void CombinedDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    editor->setGeometry(option.rect);
    if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(editor)) {
        adjustEditorSize(textEdit);
    }
}

QSize CombinedDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    // First check if the item has a specific size hint set
    QVariant sizeHintVar = index.data(Qt::SizeHintRole);
    if (sizeHintVar.isValid()) {
        return qvariant_cast<QSize>(sizeHintVar);
    }

    // Check if this is an image item - handle it specially
    bool isImageItem = index.data(Qt::UserRole+3).toBool();
    if (isImageItem) {
        qDebug() << "CombinedDelegate: sizeHint called for image item";

        // Single image only now - calculate size based on actual image dimensions
        QString imagePath = index.data(Qt::UserRole+4).toString();

        // Try to get the actual display size for this image
        QSize imageSize = getActualImageDisplaySize(imagePath);

        const int MARGIN = 10;
        int totalHeight = imageSize.height() + (2 * MARGIN);
        int totalWidth = imageSize.width() + (2 * MARGIN);

        qDebug() << "CombinedDelegate: Calculated single image size:" << QSize(totalWidth, totalHeight);
        return QSize(totalWidth, totalHeight);
    }

    // Check if this is a colored text item
    bool shouldColorText = index.data(Qt::UserRole+1).toBool();

    if (!shouldColorText) {
        // For normal items, use default size
        return QStyledItemDelegate::sizeHint(option, index);
    }

    // For colored items, calculate proper size
    QSize size = QStyledItemDelegate::sizeHint(option, index);

    // Get the text
    QString text = index.data(Qt::DisplayRole).toString();
    if (!text.isEmpty()) {
        // Create a temporary document to calculate required height
        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setPlainText(text);

        // Set text width to the available width in the view
        int textWidth = option.rect.width();
        if (textWidth <= 0) {
            // If the rect width is not valid, use the current size hint width
            textWidth = size.width();
        }
        doc.setTextWidth(textWidth);

        // Ensure minimum height for the text plus some padding
        int textHeight = doc.size().height();
        size.setHeight(textHeight + 0);  // Add some padding
    }

    return size;
}

QSize CombinedDelegate::getActualImageDisplaySize(const QString& imagePath) const
{
    // Cache results to avoid repeated calculations - with size limit
    static QMap<QString, QSize> sizeCache;
    static const int MAX_CACHE_SIZE = 100;  // Maximum cache entries
    
    // Clear cache if it gets too large to prevent unbounded memory growth
    if (sizeCache.size() > MAX_CACHE_SIZE) {
        qDebug() << "CombinedDelegate: Clearing image size cache, was" << sizeCache.size() << "entries";
        sizeCache.clear();
    }

    if (sizeCache.contains(imagePath)) {
        return sizeCache[imagePath];
    }

    QSize resultSize;

    // Safer way to get the main window using stored pointer or application search
    MainWindow* mainWindow = nullptr;
    
    // Try to get from parent widget first
    QWidget* parentWidget = qobject_cast<QWidget*>(parent());
    if (parentWidget) {
        // Limit traversal depth to prevent infinite loops
        const int MAX_DEPTH = 10;
        int depth = 0;
        QWidget* current = parentWidget;
        
        while (current && !mainWindow && depth < MAX_DEPTH) {
            mainWindow = qobject_cast<MainWindow*>(current);
            if (!mainWindow) {
                current = current->parentWidget();
                depth++;
            }
        }
        
        if (depth >= MAX_DEPTH) {
            qWarning() << "CombinedDelegate: Max parent traversal depth reached";
        }
    }

    if (!mainWindow) {
        qDebug() << "CombinedDelegate: No main window found, using default size";
        resultSize = QSize(200, 150); // Reasonable default
        sizeCache[imagePath] = resultSize;
        return resultSize;
    }

    try {
        // Load the actual image to determine its display size
        QPixmap imagePixmap = loadImageForDisplay(imagePath);

        if (!imagePixmap.isNull()) {
            QSize originalSize = imagePixmap.size();

            // Apply the same sizing logic as the diary would use
            const int MAX_WIDTH = 400;
            const int MAX_HEIGHT = 300;
            const int MIN_SIZE = 64;

            // Check if image is very small
            if (originalSize.width() < MIN_SIZE && originalSize.height() < MIN_SIZE) {
                // Scale up to minimum size
                double scaleX = double(MIN_SIZE) / originalSize.width();
                double scaleY = double(MIN_SIZE) / originalSize.height();
                double scale = qMax(scaleX, scaleY);

                resultSize = QSize(
                    qRound(originalSize.width() * scale),
                    qRound(originalSize.height() * scale)
                    );
            }
            // Check if image is too large
            else if (originalSize.width() > MAX_WIDTH || originalSize.height() > MAX_HEIGHT) {
                // Scale down to fit within maximum dimensions
                resultSize = originalSize.scaled(QSize(MAX_WIDTH, MAX_HEIGHT), Qt::KeepAspectRatio);
            }
            // Image is within acceptable range
            else {
                resultSize = originalSize;
            }

            qDebug() << "CombinedDelegate: Actual image size" << originalSize << "display size" << resultSize;
        } else {
            qDebug() << "CombinedDelegate: Failed to load image, using default size";
            resultSize = QSize(64, 64); // Default thumbnail size
        }
    } catch (...) {
        qDebug() << "CombinedDelegate: Exception loading image, using default size";
        resultSize = QSize(64, 64);
    }

    sizeCache[imagePath] = resultSize;
    return resultSize;
}

void CombinedDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (index.data(Qt::UserRole).toBool()) {
        // Do not paint the text if the UserRole data is true
        return;
    }

    // Check if this is an image item
    bool isImageItem = index.data(Qt::UserRole+3).toBool();

    if (isImageItem) {
        // Custom painting for image items
        paintImageItem(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Check if this item should have colored text
    bool shouldColorText = index.data(Qt::UserRole+1).toBool();

    if (!shouldColorText) {
        // For normal items, use the default painting
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // Rest of existing colored text painting code...
    // Draw the selection background if item is selected
    if (opt.state & QStyle::State_Selected) {
        painter->fillRect(opt.rect, opt.palette.highlight());
    }

    // Get the text
    QString text = index.data(Qt::DisplayRole).toString();

    // Save painter state
    painter->save();

    // Calculate text rectangle with padding
    QRect textRect = opt.rect.adjusted(0, 0, -1, -1);

    // Check if this is a task manager entry
    bool isTaskManager = index.data(Qt::UserRole+2).toBool();

    // Use different color length based on whether it's a Task Manager entry
    int colorLength = isTaskManager ? 12 : m_colorLength;  // "Task Manager" is 12 characters

    if (text.isEmpty()) {
        // Nothing to draw
    } else if (text.length() <= colorLength) {
        // If text is shorter than color length, color the whole text
        painter->setPen(m_textColor);
        painter->setFont(opt.font); // Respect the item's font
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
    } else {
        // Create a text document for more control over text layout
        QTextDocument doc;
        doc.setDefaultFont(opt.font);
        doc.setTextWidth(textRect.width());

        // Create the formatted text with different colors
        QString htmlText = QString("<span style=\"font-weight: bold; font-family: Helvetica \"><span style=\"color: %1;\">%2</span>%3</span>")
                               .arg(m_textColor.name())
                               .arg(text.left(colorLength).toHtmlEscaped())
                               .arg(text.mid(colorLength).toHtmlEscaped());

        doc.setHtml(htmlText);

        // Draw the document
        painter->translate(textRect.topLeft());
        doc.drawContents(painter);
    }

    // Restore painter state
    painter->restore();
}

void CombinedDelegate::paintImageItem(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    qDebug() << "CombinedDelegate: paintImageItem called";
    // Draw the selection background if item is selected
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    // Single image handling only
    QString imagePath = index.data(Qt::UserRole+4).toString();
    qDebug() << "CombinedDelegate: Single image path:" << imagePath;

    if (!imagePath.isEmpty()) {
        qDebug() << "CombinedDelegate: Calling paintSingleImage";
        paintSingleImage(painter, option, index, imagePath);
    } else {
        qDebug() << "CombinedDelegate: No image path found for single image";
    }
}

void CombinedDelegate::paintSingleImage(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, const QString& imagePath) const {
    qDebug() << "CombinedDelegate: paintSingleImage called for path:" << imagePath;
    if (imagePath.isEmpty()) {
        qDebug() << "CombinedDelegate: Image path is empty, falling back to default paint";
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // Load and decrypt the image
    QPixmap imagePixmap = loadImageForDisplay(imagePath);
    if (imagePixmap.isNull()) {
        qDebug() << "CombinedDelegate: Image pixmap is null, drawing error text";
        painter->save();
        painter->setPen(Qt::red);
        painter->setFont(option.font);
        painter->drawText(option.rect, Qt::AlignLeft | Qt::AlignVCenter, "Image not found");
        painter->restore();
        return;
    }

    const int MARGIN = 10;

    // Left-align the image instead of centering
    int x = option.rect.x() + MARGIN; // Left alignment
    int y = option.rect.y() + MARGIN; // Top alignment with margin

    // Calculate available space for the image
    int availableWidth = option.rect.width() - (2 * MARGIN);
    int availableHeight = option.rect.height() - (2 * MARGIN);

    // Scale the image to fit the available space while preserving aspect ratio
    QPixmap scaledPixmap = imagePixmap.scaled(
        QSize(availableWidth, availableHeight),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
        );

    // Draw the image at left-aligned position
    painter->drawPixmap(x, y, scaledPixmap);

    qDebug() << "CombinedDelegate: Painted left-aligned image at" << QPoint(x, y) << "size" << scaledPixmap.size();
}

QPixmap CombinedDelegate::loadImageForDisplay(const QString& imagePath) const
{
    qDebug() << "CombinedDelegate: loadImageForDisplay called for:" << imagePath;

    // Check if the encrypted image file exists
    if (!QFileInfo::exists(imagePath)) {
        qDebug() << "CombinedDelegate: Image file does not exist";
        return QPixmap();
    }

    qDebug() << "CombinedDelegate: Image file exists";

    try {
        // Read the encrypted binary data
        QFile encryptedFile(imagePath);
        if (!encryptedFile.open(QIODevice::ReadOnly)) {
            qDebug() << "CombinedDelegate: Failed to open encrypted file";
            return QPixmap();
        }

        QByteArray encryptedData = encryptedFile.readAll();
        encryptedFile.close();

        qDebug() << "CombinedDelegate: Read encrypted data, size:" << encryptedData.size();

        // We need access to the encryption key - get it from the parent widget
        QWidget* parentWidget = qobject_cast<QWidget*>(parent());
        if (!parentWidget) {
            qDebug() << "CombinedDelegate: No parent widget found";
            return QPixmap();
        }

        // Find the main window to get the encryption key - with safety limits
        MainWindow* mainWindow = nullptr;
        const int MAX_DEPTH = 10;
        int depth = 0;
        QWidget* current = parentWidget;
        
        while (current && !mainWindow && depth < MAX_DEPTH) {
            mainWindow = qobject_cast<MainWindow*>(current);
            if (!mainWindow) {
                current = current->parentWidget();
                depth++;
            }
        }
        
        if (depth >= MAX_DEPTH) {
            qWarning() << "CombinedDelegate: Max parent traversal depth reached in loadImageForDisplay";
        }

        if (!mainWindow) {
            qDebug() << "CombinedDelegate: No main window found";
            return QPixmap();
        }

        qDebug() << "CombinedDelegate: Found main window, attempting decryption";

        // Decrypt using binary decryption
        QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(
            mainWindow->user_Key, encryptedData);

        if (decryptedData.isEmpty()) {
            qDebug() << "CombinedDelegate: Decryption failed";
            return QPixmap();
        }

        qDebug() << "CombinedDelegate: Decryption successful, size:" << decryptedData.size();

        // Load image directly from binary data
        QPixmap pixmap;
        bool loadSuccess = pixmap.loadFromData(decryptedData);

        qDebug() << "CombinedDelegate: Load from data - success:" << loadSuccess << "size:" << pixmap.size();

        return loadSuccess ? pixmap : QPixmap();

    } catch (const std::exception& e) {
        qDebug() << "CombinedDelegate: Exception in loadImageForDisplay:" << e.what();
        return QPixmap();
    } catch (...) {
        qDebug() << "CombinedDelegate: Unknown exception in loadImageForDisplay";
        return QPixmap();
    }
}

bool CombinedDelegate::eventFilter(QObject *object, QEvent *event) {
    if (QTextEdit *editor = qobject_cast<QTextEdit *>(object)) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

            if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
                keyEvent->modifiers() == Qt::ControlModifier) {
                emit commitData(editor);
                emit closeEditor(editor);
                return true;
            }
            if (keyEvent->key() == Qt::Key_Return && keyEvent->modifiers() == Qt::ShiftModifier) {
                return false;
            }
            if (keyEvent->key() == Qt::Key_Return) {
                emit commitData(editor);
                emit closeEditor(editor);
                return true;
            }
            if (keyEvent->key() == Qt::Key_Escape) {
                emit closeEditor(editor, QAbstractItemDelegate::NoHint);
                return true;
            }
        }
    }
    return QStyledItemDelegate::eventFilter(object, event);
}

void CombinedDelegate::editorTextChanged(void) {
    if (QTextEdit *editor = qobject_cast<QTextEdit *>(sender())) {
        adjustEditorSize(editor);
        if (auto view = qobject_cast<QAbstractItemView *>(editor->parentWidget()->parentWidget())) {
            view->viewport()->update();
            adjustListWidgetScroll(editor);
        }
    }
}

void CombinedDelegate::onEditorClosed(const QString &text, int itemIndex) {
    qDebug() << "CombinedDelegate: onEditorClosed called for item:" << itemIndex;
    // Validate text before triggering the signal
    InputValidation::ValidationResult result =
        InputValidation::validateInput(text, InputValidation::InputType::DiaryContent, 10000);

    if (result.isValid) {
        TextModificationsMade(text, itemIndex);
    } else {
        qWarning() << "CombinedDelegate: Input validation failed on editor close:" << result.errorMessage;
        // Don't process the edited text if validation fails
    }
}

void CombinedDelegate::adjustEditorSize(QTextEdit *editor) const {
    if (!editor) return;

    int availableWidth = editor->viewport()->width();

    QTextDocument doc;
    doc.setPlainText(editor->toPlainText());
    doc.setTextWidth(availableWidth);

    int requiredHeight = doc.size().height() + 4;

    editor->setFixedHeight(requiredHeight);
}

void CombinedDelegate::adjustListWidgetScroll(QTextEdit *editor) const {
    QListWidget *listWidget = qobject_cast<QListWidget *>(editor->parentWidget()->parentWidget());
    if (!listWidget) return;

    QModelIndex index = listWidget->currentIndex();
    if (!index.isValid()) return;

    listWidget->scrollToItem(listWidget->item(index.row()), QAbstractItemView::EnsureVisible);
}
