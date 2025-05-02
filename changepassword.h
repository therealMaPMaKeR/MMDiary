#ifndef CHANGEPASSWORD_H
#define CHANGEPASSWORD_H

#include <QDialog>
#include <QByteArray>
#include <QString>

namespace Ui {
class ChangePassword;
}

class ChangePassword : public QDialog
{
    Q_OBJECT

public:
    explicit ChangePassword(QWidget *parent = nullptr);
    ~ChangePassword();

    // Initialize with current username and encryption key
    void initialize(const QString& username, const QByteArray& encryptionKey);

private slots:
    void on_pushButton_Cancel_clicked();
    void on_pushButton_ChangePW_clicked();

private:
    Ui::ChangePassword *ui;

    // Store current user info
    QString m_username;
    QByteArray m_encryptionKey;

    // Validates user input
    bool validateUserInput();

    // Verifies that the current password is correct
    bool verifyCurrentPassword();

    // Changes the password in the database
    bool changePassword();
};

#endif // CHANGEPASSWORD_H
