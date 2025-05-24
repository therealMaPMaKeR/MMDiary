#ifndef OPERATIONS_ENCRYPTEDDATA_H
#define OPERATIONS_ENCRYPTEDDATA_H

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

#endif // OPERATIONS_ENCRYPTEDDATA_H
