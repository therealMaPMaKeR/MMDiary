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

private:
    QString lastValidText; // Add this member variable to store the last valid text

signals:
    void customSignal();
};
#endif // CUSTOM_QTEXTEDITWIDGET_H
