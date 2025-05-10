#include "custom_QTextEditWidget.h"
#include <QTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QKeyEvent>
#include <QDebug>
#include "../Operations-Global/inputvalidation.h"
#include <QMimeData>

custom_QTextEditWidget::custom_QTextEditWidget(QWidget *parent): QTextEdit(parent) {
    // Force plain text mode - no rich text allowed
    setAcceptRichText(false);

    // Connect to textChanged signal to adjust height automatically when content changes
    connect(this, &QTextEdit::textChanged, this, &custom_QTextEditWidget::adjustHeight);
    // Connect to document layout changes which happen during font changes
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, &custom_QTextEditWidget::adjustHeight);
    this->setParent(parent);
    this->show();
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // Add validation when text changes
    connect(this, &QTextEdit::textChanged, this, &custom_QTextEditWidget::validateText);
}

void custom_QTextEditWidget::validateText() {
    QString currentText = this->toPlainText();
    InputValidation::ValidationResult result =
        InputValidation::validateInput(currentText, InputValidation::InputType::DiaryContent, 10000);

    if (!result.isValid) {
        qWarning() << "Text validation warning:" << result.errorMessage;

        // Block the invalid input by restoring the previous valid text
        // Store the cursor position
        int cursorPosition = textCursor().position();

        // Disconnect the textChanged signal to avoid recursion
        disconnect(this, &QTextEdit::textChanged, this, &custom_QTextEditWidget::validateText);

        // Set text to the last valid text
        setPlainText(lastValidText);

        // Try to restore cursor position if possible
        QTextCursor cursor = textCursor();
        cursor.setPosition(qMin(cursorPosition, lastValidText.length()));
        setTextCursor(cursor);

        // Reconnect the textChanged signal
        connect(this, &QTextEdit::textChanged, this, &custom_QTextEditWidget::validateText);
    } else {
        // Update the last valid text
        lastValidText = currentText;
    }
}

void custom_QTextEditWidget::keyPressEvent(QKeyEvent *event)
{
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
            qWarning() << "Text validation failed on return press:" << result.errorMessage;
            // Do not emit the signal if validation fails
        }
    }
    else {
        // For other keys, let's pre-validate to see if this key would produce invalid input
        // We'll handle this in validateText() after the key is processed
        QTextEdit::keyPressEvent(event);
    }
}

void custom_QTextEditWidget::UpdateFontSizeTrigger(int size, bool zoom) // need to pass zoom for the signal connect even though we dont use it
{
    updateFontSize(size);
}

void custom_QTextEditWidget::updateFontSize(int size) {
    QFont font = this->font();
    font.setPointSize(size);
    setFont(font);
    // After setting font, readjust height
    adjustHeight();
}

void custom_QTextEditWidget::adjustHeight() {
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

void custom_QTextEditWidget::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    // After any resize, adjust the height based on content
    adjustHeight();
}

void custom_QTextEditWidget::changeEvent(QEvent *event) {
    if (event->type() == QEvent::FontChange) {
        // When font changes, adjust height
        adjustHeight();
    }
    QTextEdit::changeEvent(event);
}


void custom_QTextEditWidget::insertFromMimeData(const QMimeData *source) {
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
