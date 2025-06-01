#ifndef CUSTOM_QTEXTEDITWIDGET_H
#define CUSTOM_QTEXTEDITWIDGET_H
// AI GENERATED
#include <QWidget>
#include <QTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMenu>
#include <QAbstractTextDocumentLayout>

// In custom_QTextEditWidget.h
class custom_QTextEditWidget : public QTextEdit
{
    Q_OBJECT
public:
    custom_QTextEditWidget(QWidget *parent = nullptr);

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
};
#endif // CUSTOM_QTEXTEDITWIDGET_H
