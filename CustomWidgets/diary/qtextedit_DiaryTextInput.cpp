#include "qtextedit_DiaryTextInput.h"
#include <QTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QKeyEvent>
#include <QDebug>
#include "../../Operations-Global/inputvalidation.h"
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QImageReader>

qtextedit_DiaryTextInput::qtextedit_DiaryTextInput(QWidget *parent): QTextEdit(parent) {
    qDebug() << "qtextedit_DiaryTextInput: Constructor called";
    // Force plain text mode - no rich text allowed
    setAcceptRichText(false);

    // Connect to textChanged signal to adjust height automatically when content changes
    connect(this, &QTextEdit::textChanged, this, &qtextedit_DiaryTextInput::adjustHeight);
    // Connect to document layout changes which happen during font changes
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, &qtextedit_DiaryTextInput::adjustHeight);
    this->setParent(parent);
    this->show();
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // Add validation when text changes
    connect(this, &QTextEdit::textChanged, this, &qtextedit_DiaryTextInput::validateText);
}

void qtextedit_DiaryTextInput::validateText() {
    QString currentText = this->toPlainText();
    InputValidation::ValidationResult result =
        InputValidation::validateInput(currentText, InputValidation::InputType::DiaryContent, 10000);

    if (!result.isValid) {
        qWarning() << "qtextedit_DiaryTextInput: Text validation warning:" << result.errorMessage;

        // Block the invalid input by restoring the previous valid text
        // Store the cursor position
        int cursorPosition = textCursor().position();

        // Disconnect the textChanged signal to avoid recursion
        disconnect(this, &QTextEdit::textChanged, this, &qtextedit_DiaryTextInput::validateText);

        // Set text to the last valid text
        setPlainText(lastValidText);

        // Try to restore cursor position if possible
        QTextCursor cursor = textCursor();
        cursor.setPosition(qMin(cursorPosition, lastValidText.length()));
        setTextCursor(cursor);

        // Reconnect the textChanged signal
        connect(this, &QTextEdit::textChanged, this, &qtextedit_DiaryTextInput::validateText);
    } else {
        // Update the last valid text
        lastValidText = currentText;
    }
}

void qtextedit_DiaryTextInput::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "qtextedit_DiaryTextInput: keyPressEvent called with key:" << event->key();
    adjustHeight();
    if (event->key() == Qt::Key_Return && event->modifiers() == Qt::ShiftModifier) {
        this->insertPlainText("\n");
    }
    else if (event->key() == Qt::Key_Return) {
        // Validate before emitting signal
        QString currentText = this->toPlainText();
        InputValidation::ValidationResult result =
            InputValidation::validateInput(currentText, InputValidation::InputType::DiaryContent, 10000);

        if (result.isValid) {
            emit customSignal();
        } else {
            qWarning() << "qtextedit_DiaryTextInput: Text validation failed on return press:" << result.errorMessage;
            // Do not emit the signal if validation fails
        }
    }
    else {
        // For other keys, let's pre-validate to see if this key would produce invalid input
        // We'll handle this in validateText() after the key is processed
        QTextEdit::keyPressEvent(event);
    }
}

void qtextedit_DiaryTextInput::UpdateFontSizeTrigger(int size, bool zoom) // need to pass zoom for the signal connect even though we dont use it
{
    qDebug() << "qtextedit_DiaryTextInput: UpdateFontSizeTrigger called with size:" << size;
    updateFontSize(size);
}

void qtextedit_DiaryTextInput::updateFontSize(int size) {
    qDebug() << "qtextedit_DiaryTextInput: updateFontSize called with size:" << size;
    QFont font = this->font();
    font.setPointSize(size);
    setFont(font);
    // After setting font, readjust height
    adjustHeight();
}

void qtextedit_DiaryTextInput::adjustHeight() {
    // Calculate the required height based on the content with current font metrics
    QTextDocument* doc = document();
    // Force layout update with current width
    doc->setTextWidth(viewport()->width());
    // Get the size based on current font and content
    int requiredHeight = doc->size().height() + frameWidth() * 2; // Add frame width for accurate measurement
    // If there's a vertical scrollbar, adjust for its presence
    if (verticalScrollBar()->isVisible()) {
        requiredHeight += verticalScrollBar()->height();
    }
    // Add some padding to ensure text isn't cut off
    requiredHeight += 4;
    // Set the new height, but only if it's different from the current height
    if (height() != requiredHeight) {
        setFixedHeight(requiredHeight);
    }
}

void qtextedit_DiaryTextInput::resizeEvent(QResizeEvent *event) {
    qDebug() << "qtextedit_DiaryTextInput: resizeEvent called";
    QTextEdit::resizeEvent(event);
    // After any resize, adjust the height based on content
    adjustHeight();
}

void qtextedit_DiaryTextInput::changeEvent(QEvent *event) {
    if (event->type() == QEvent::FontChange) {
        qDebug() << "qtextedit_DiaryTextInput: Font change event";
        // When font changes, adjust height
        adjustHeight();
    }
    QTextEdit::changeEvent(event);
}


//-- Copy/Paste --//
void qtextedit_DiaryTextInput::insertFromMimeData(const QMimeData *source)
{
    qDebug() << "qtextedit_DiaryTextInput: insertFromMimeData called";
    // Check if the mime data contains images
    if (source->hasImage()) {
        // Handle pasted images
        QVariant imageData = source->imageData();
        if (imageData.isValid()) {
            QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
            if (!pixmap.isNull()) {
                // Create a temporary file for the pasted image
                QString tempDir = QDir::tempPath();
                QString tempFileName = QString("clipboard_image_%1.png")
                                           .arg(QDateTime::currentDateTime().toString("yyyy.MM.dd_hh.mm.ss"));
                QString tempFilePath = QDir::cleanPath(tempDir + "/" + tempFileName);

                if (pixmap.save(tempFilePath, "PNG")) {
                    QStringList imagePaths;
                    imagePaths.append(tempFilePath);
                    emit imagesPasted(imagePaths);
                    return;
                }
            }
        }
    }

    // If the source has HTML or rich text, use the plain text version instead
    if (source->hasText()) {
        // Get only plain text and insert it
        QString plainText = source->text();

        // Create a new QMimeData with only plain text
        QMimeData *plainMimeData = new QMimeData;
        plainMimeData->setText(plainText);

        // Call the parent method with our plain text mime data
        QTextEdit::insertFromMimeData(plainMimeData);

        // Clean up
        delete plainMimeData;
    } else {
        // Fall back to default behavior for non-text content
        QTextEdit::insertFromMimeData(source);
    }
}


//--  Import Image to Diary --//
void qtextedit_DiaryTextInput::dragEnterEvent(QDragEnterEvent *event)
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

    QTextEdit::dragEnterEvent(event);
}

void qtextedit_DiaryTextInput::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QTextEdit::dragMoveEvent(event);
    }
}

void qtextedit_DiaryTextInput::dropEvent(QDropEvent *event)
{
    qDebug() << "qtextedit_DiaryTextInput: dropEvent called";
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

    QTextEdit::dropEvent(event);
}

bool qtextedit_DiaryTextInput::isImageFile(const QString& filePath)
{
    if (!QFileInfo::exists(filePath)) {
        return false;
    }

    QStringList supportedFormats = getSupportedImageFormats();
    QString fileExtension = QFileInfo(filePath).suffix().toLower();

    return supportedFormats.contains(fileExtension);
}

QStringList qtextedit_DiaryTextInput::getSupportedImageFormats()
{
    return QStringList() << "png" << "jpg" << "jpeg" << "gif" << "bmp"
                         << "tiff" << "tif" << "webp" << "ico" << "svg";
}
