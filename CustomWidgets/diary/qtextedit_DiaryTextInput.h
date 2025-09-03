#ifndef QTEXTEDIT_DIARYTEXTINPUT_H
#define QTEXTEDIT_DIARYTEXTINPUT_H
// AI GENERATED
#include <QWidget>
#include <QTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMenu>
#include <QAbstractTextDocumentLayout>

// In qtextedit_DiaryTextInput.h
class qtextedit_DiaryTextInput : public QTextEdit
{
    Q_OBJECT
public:
    qtextedit_DiaryTextInput(QWidget *parent = nullptr);

public slots:
    void keyPressEvent(QKeyEvent *event) override;
    void UpdateFontSizeTrigger(int size, bool zoom);
    void updateFontSize(int size);

private slots:
    void adjustHeight();
    void validateText();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QString lastValidText; // Add this member variable to store the last valid text
    void insertFromMimeData(const QMimeData *source) override;

    bool isImageFile(const QString& filePath);
    QStringList getSupportedImageFormats();

signals:
    void customSignal();

    void imagesDropped(const QStringList& imagePaths);
    void imagesPasted(const QStringList& imagePaths);
    
    // SECURITY FIX: New signal to handle clipboard images without temp files
    void clipboardImageReceived(const QImage& image, const QString& format);
};
#endif // QTEXTEDIT_DIARYTEXTINPUT_H
