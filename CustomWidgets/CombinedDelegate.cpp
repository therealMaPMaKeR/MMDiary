#include "CombinedDelegate.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QTextDocument>
#include "custom_QTextEditWidget.h"
#include "../Operations-Global/inputvalidation.h"
#include "../Operations-Global/operations_files.h"
#include "../Operations-Global/CryptoUtils.h"
#include "../mainwindow.h"

CombinedDelegate::CombinedDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , m_colorLength(5)
    , m_taskManagerLength(12)
    , m_textColor(QColor(255, 0, 0))
{
    // Connect our custom signal to our non-const slot
    connect(this, &CombinedDelegate::textCommitted, this, &CombinedDelegate::onEditorClosed);
}

void CombinedDelegate::setColorLength(int length) {
    m_colorLength = length;
}

void CombinedDelegate::setTextColor(const QColor &color) {
    m_textColor = color;
}

QWidget *CombinedDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (index.data(Qt::UserRole).toBool()) {
        return nullptr; // Don't create an editor if UserRole is true
    } else {
        custom_QTextEditWidget *editor = new custom_QTextEditWidget(parent);

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
    if (custom_QTextEditWidget *textEdit = qobject_cast<custom_QTextEditWidget*>(editor)) {
        textEdit->setPlainText(index.model()->data(index, Qt::EditRole).toString());

        // Set the font from the item
        QFont itemFont = index.data(Qt::FontRole).value<QFont>();
        textEdit->setFont(itemFont);

        // This will trigger adjustHeight via the textChanged signal
    }
}

void CombinedDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(editor)) {
        // Add input validation before setting the data
        QString text = textEdit->document()->toPlainText();
        InputValidation::ValidationResult result =
            InputValidation::validateInput(text, InputValidation::InputType::DiaryContent, 10000);

        if (result.isValid) {
            model->setData(index, text, Qt::EditRole);
        } else {
            qWarning() << "Input validation failed:" << result.errorMessage;
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
        qDebug() << "=== sizeHint called for image item";

        // For image items, we need a larger size hint
        // Try to get the image path and load it to determine size
        QString imagePath = index.data(Qt::UserRole+4).toString();
        if (!imagePath.isEmpty()) {
            // Load the image to get its actual dimensions
            QPixmap imagePixmap = loadImageForDisplay(imagePath);
            if (!imagePixmap.isNull()) {
                QSize imageSize = imagePixmap.size();
                int itemHeight = imageSize.height() + 30; // Image + 30px for text and padding
                int itemWidth = qMax(imageSize.width() + 20, 300); // Minimum 300px width

                qDebug() << "Calculated size hint for image:" << QSize(itemWidth, itemHeight);
                return QSize(itemWidth, itemHeight);
            }
        }

        // Fallback size for image items if we can't load the image
        qDebug() << "Using fallback size for image item";
        return QSize(400, 250);
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
    qDebug() << "=== paintImageItem called";
    qDebug() << "Option rect:" << option.rect;

    // Draw the selection background if item is selected
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
        qDebug() << "Drew selection background";
    }

    // Draw the background color for image items
    if (!(option.state & QStyle::State_Selected)) {
        painter->fillRect(option.rect, QBrush(QColor(248, 248, 255))); // Light blue background
        qDebug() << "Drew light blue background";
    }

    // Get the image path and load the image
    QString imagePath = index.data(Qt::UserRole+4).toString();
    qDebug() << "Image path from UserRole+4:" << imagePath;

    if (imagePath.isEmpty()) {
        qDebug() << "Image path is empty, falling back to default paint";
        // If no image path, just draw the text
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // Load and decrypt the image
    QPixmap imagePixmap = loadImageForDisplay(imagePath);
    qDebug() << "Loaded image pixmap - isNull:" << imagePixmap.isNull() << "size:" << imagePixmap.size();

    if (imagePixmap.isNull()) {
        qDebug() << "Image pixmap is null, drawing error text";
        // If image failed to load, draw error text
        painter->save();
        painter->setPen(Qt::red);
        painter->setFont(option.font);
        painter->drawText(option.rect, Qt::AlignCenter, "Image not found");
        painter->restore();
        return;
    }

    // Calculate image display area (leave space at bottom for text)
    QRect imageRect = option.rect;
    imageRect.setHeight(imageRect.height() - 20); // Reserve 20px for text at bottom
    qDebug() << "Image display rect:" << imageRect;

    // Scale image to fit while preserving aspect ratio
    QPixmap scaledPixmap = imagePixmap.scaled(imageRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    qDebug() << "Scaled pixmap size:" << scaledPixmap.size();

    // Center the image horizontally and vertically in the available space
    int x = imageRect.x() + (imageRect.width() - scaledPixmap.width()) / 2;
    int y = imageRect.y() + (imageRect.height() - scaledPixmap.height()) / 2;
    qDebug() << "Drawing image at position:" << x << y;

    // Draw the image
    painter->drawPixmap(x, y, scaledPixmap);
    qDebug() << "Drew pixmap";

    // Draw the caption text at the bottom
    QString text = index.data(Qt::DisplayRole).toString();
    qDebug() << "Caption text:" << text;

    if (!text.isEmpty()) {
        QRect textRect = option.rect;
        textRect.setTop(option.rect.bottom() - 20); // Bottom 20px for text
        qDebug() << "Text rect:" << textRect;

        painter->save();
        QFont captionFont = option.font;
        captionFont.setPointSize(8);
        captionFont.setItalic(true);
        painter->setFont(captionFont);
        painter->setPen(option.palette.text().color());
        painter->drawText(textRect, Qt::AlignCenter, text);
        painter->restore();
        qDebug() << "Drew caption text";
    }

    qDebug() << "=== paintImageItem finished";
}

QPixmap CombinedDelegate::loadImageForDisplay(const QString& imagePath) const
{
    qDebug() << "=== CombinedDelegate::loadImageForDisplay called for:" << imagePath;

    // Check if the encrypted image file exists
    if (!QFileInfo::exists(imagePath)) {
        qDebug() << "Image file does not exist in delegate";
        return QPixmap();
    }

    qDebug() << "Image file exists in delegate";

    try {
        // Read the encrypted binary data
        QFile encryptedFile(imagePath);
        if (!encryptedFile.open(QIODevice::ReadOnly)) {
            qDebug() << "Failed to open encrypted file in delegate";
            return QPixmap();
        }

        QByteArray encryptedData = encryptedFile.readAll();
        encryptedFile.close();

        qDebug() << "Read encrypted data in delegate, size:" << encryptedData.size();

        // We need access to the encryption key - get it from the parent widget
        QWidget* parentWidget = qobject_cast<QWidget*>(parent());
        if (!parentWidget) {
            qDebug() << "No parent widget found in delegate";
            return QPixmap();
        }

        // Find the main window to get the encryption key
        MainWindow* mainWindow = nullptr;
        QWidget* current = parentWidget;
        while (current && !mainWindow) {
            mainWindow = qobject_cast<MainWindow*>(current);
            current = current->parentWidget();
        }

        if (!mainWindow) {
            qDebug() << "No main window found in delegate";
            return QPixmap();
        }

        qDebug() << "Found main window in delegate, attempting decryption";

        // Decrypt using binary decryption
        QByteArray decryptedData = CryptoUtils::Encryption_DecryptBArray(
            mainWindow->user_Key, encryptedData);

        if (decryptedData.isEmpty()) {
            qDebug() << "Decryption failed in delegate";
            return QPixmap();
        }

        qDebug() << "Decryption successful in delegate, size:" << decryptedData.size();

        // Load image directly from binary data
        QPixmap pixmap;
        bool loadSuccess = pixmap.loadFromData(decryptedData);

        qDebug() << "Load from data in delegate - success:" << loadSuccess << "size:" << pixmap.size();

        return loadSuccess ? pixmap : QPixmap();

    } catch (const std::exception& e) {
        qDebug() << "Exception in delegate loadImageForDisplay:" << e.what();
        return QPixmap();
    } catch (...) {
        qDebug() << "Unknown exception in delegate loadImageForDisplay";
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
    // Validate text before triggering the signal
    InputValidation::ValidationResult result =
        InputValidation::validateInput(text, InputValidation::InputType::DiaryContent, 10000);

    if (result.isValid) {
        TextModificationsMade(text, itemIndex);
    } else {
        qWarning() << "Input validation failed on editor close:" << result.errorMessage;
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
