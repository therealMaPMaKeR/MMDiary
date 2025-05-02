#ifndef OPERATIONS_TEXTSMANAGER_H
#define OPERATIONS_TEXTSMANAGER_H

#include <QObject>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include <QMessageBox>

class MainWindow;
class Operations_TextsManager : public QObject
{
    Q_OBJECT
private:
    MainWindow* m_mainWindow;
public:
    explicit Operations_TextsManager(MainWindow* mainWindow);
    friend class MainWindow;

};

#endif // OPERATIONS_TEXTSMANAGER_H
