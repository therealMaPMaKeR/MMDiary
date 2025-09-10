#ifndef LOGINSCREEN_H
#define LOGINSCREEN_H

#include <QDialog>
#include <QSettings>
#include <QDebug>
#include "Operations-Global/encryption/SecureByteArray.h"

//Forward Declarations
class MainWindow;
//Headers Class
namespace Ui {
class loginscreen;
}

class loginscreen : public QDialog
{
    Q_OBJECT

public:
    explicit loginscreen(QWidget *parent = nullptr);
    ~loginscreen();

private slots:

    void on_pushButton_Login_clicked();

    void on_pushButton_NewAccount_clicked();


private:
    Ui::loginscreen *ui;
    QString getUserData(QString username, QString dataType);
    QByteArray getUserSalt(QString username);
    bool isUserInputValid();
    bool loggingIn = false;

protected:
    void closeEvent(QCloseEvent *event);
signals:
    // Note: Ownership of the SecureByteArray pointer is transferred to the receiver
    void passDataMW_Signal(QString username, SecureByteArray* key);
};

#endif // LOGINSCREEN_H
