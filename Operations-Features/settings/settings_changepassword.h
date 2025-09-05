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

    // Enum for backup deletion modes
    enum class BackupDeletionMode {
        None = 0,        // No deletion scheduled
        Immediate = 1,   // Delete on next login
        Delayed = 2      // Delete after 7 days
    };

private slots:
    void on_pushButton_Cancel_clicked();
    void on_pushButton_ChangePW_clicked();

private:
    Ui::ChangePassword *ui;

    // Store current user info
    QString m_username;
    QByteArray m_encryptionKey;

    // SECURITY: Secure cleanup of sensitive data
    void secureCleanup();

    // Validates user input
    bool validateUserInput();

    // Verifies that the current password is correct
    bool verifyCurrentPassword();

    // Shows backup deletion confirmation dialog
    BackupDeletionMode showBackupDeletionDialog();

    // Changes the password in the database
    bool changePassword(BackupDeletionMode backupMode);
};

#endif // CHANGEPASSWORD_H
