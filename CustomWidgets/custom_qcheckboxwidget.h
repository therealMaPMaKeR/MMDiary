#ifndef CUSTOM_QCHECKBOXWIDGET_H
#define CUSTOM_QCHECKBOXWIDGET_H

#include <QCheckBox>
#include <functional>

class custom_QCheckboxWidget : public QCheckBox
{
    Q_OBJECT

public:
    // Enum to specify when validation should occur
    enum ValidationMode {
        ValidateOnUncheck, // Validate when reducing security
        ValidateOnCheck,   // Validate when increasing security
        ValidateOnBoth     // Validate in both directions
    };

    explicit custom_QCheckboxWidget(QWidget *parent = nullptr);

    // Set the operation name and username for validation
    void setValidationInfo(const QString& operationName, const QString& username);

    // Enable/disable validation requirement
    void setRequireValidation(bool require);

    // Set validation mode (check, uncheck, or both)
    void setValidationMode(ValidationMode mode);

    // Set a function that returns the current database value for this setting
    void setDatabaseValueGetter(const std::function<bool()>& getter);

    // Method to set a function that returns the current grace period
    void setGracePeriodGetter(const std::function<int()>& getter);

protected:
    // Override nextCheckState to validate password before changing state
    void nextCheckState() override;

private:
    QString m_operationName;
    QString m_username;
    bool m_requireValidation;
    ValidationMode m_validationMode;
    std::function<bool()> m_databaseValueGetter;
    bool m_hasDatabaseGetter;

    std::function<int()> m_gracePeriodGetter;
    bool m_hasGracePeriodGetter;
};

#endif // CUSTOM_QCHECKBOXWIDGET_H
