#ifndef PASSWORDVALIDATION_H
#define PASSWORDVALIDATION_H

#include <QDialog>
#include <QString>
#include <QDateTime>
#include <QMap>

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

    static void clearGracePeriod(const QString& username = QString());

    // Overloaded methods that accept grace period directly
    static bool validatePasswordForOperation(QWidget* parent, const QString& operationName,
                                             const QString& username, int gracePeriodSeconds);
    static bool validatePasswordWithCustomCancel(QWidget* parent, const QString& operationName,
                                                 const QString& username, const QString& cancelButtonText,
                                                 int gracePeriodSeconds);

    // NEW: Public grace period management methods
    static void recordSuccessfulValidation(const QString& username);
    static bool isWithinGracePeriod(const QString& username, int gracePeriodSeconds);
    static int getGracePeriodForUser(const QString& username);

private slots:
    void onProceedClicked();
    void onCancelClicked();

private:
    Ui::PasswordValidation *ui;

    // Grace period functionality - static to persist across instances
    static QMap<QString, QDateTime> s_lastValidationTimes; // username -> last validation time
};

#endif // PASSWORDVALIDATION_H
