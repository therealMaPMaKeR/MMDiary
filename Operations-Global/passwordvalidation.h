#ifndef PASSWORDVALIDATION_H
#define PASSWORDVALIDATION_H

#include <QDialog>
#include <QString>

namespace Ui {
class PasswordValidation;
}

class PasswordValidation : public QDialog
{
    Q_OBJECT

public:
    explicit PasswordValidation(QWidget *parent = nullptr);
    ~PasswordValidation();

    static bool validatePasswordWithCustomCancel(QWidget* parent, const QString& operationName,
                                                 const QString& username, const QString& cancelButtonText);
    // Set the operation name to be displayed in the dialog
    void setOperationName(const QString& operationName);

    // Get the entered password
    QString getPassword() const;

    // Static method to validate password for an operation
    static bool validatePasswordForOperation(QWidget* parent, const QString& operationName, const QString& username);

private slots:
    void onProceedClicked();
    void onCancelClicked();

private:
    Ui::PasswordValidation *ui;
};

#endif // PASSWORDVALIDATION_H
