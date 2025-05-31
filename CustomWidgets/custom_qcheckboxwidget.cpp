#include "custom_qcheckboxwidget.h"
#include "../Operations-Global/passwordvalidation.h"
#include <QDebug>

custom_QCheckboxWidget::custom_QCheckboxWidget(QWidget *parent)
    : QCheckBox(parent)
    , m_operationName("")
    , m_username("")
    , m_requireValidation(false)
    , m_validationMode(ValidateOnUncheck) // Default to validate when unchecking
    , m_hasDatabaseGetter(false)
    , m_hasGracePeriodGetter(false)
{
}

void custom_QCheckboxWidget::setGracePeriodGetter(const std::function<int()>& getter)
{
    m_gracePeriodGetter = getter;
    m_hasGracePeriodGetter = true;
}

void custom_QCheckboxWidget::setValidationInfo(const QString& operationName, const QString& username)
{
    m_operationName = operationName;
    m_username = username;
}

void custom_QCheckboxWidget::setRequireValidation(bool require)
{
    m_requireValidation = require;
}

void custom_QCheckboxWidget::setValidationMode(ValidationMode mode)
{
    m_validationMode = mode;
}

void custom_QCheckboxWidget::setDatabaseValueGetter(const std::function<bool()>& getter)
{
    m_databaseValueGetter = getter;
    m_hasDatabaseGetter = true;
}

void custom_QCheckboxWidget::nextCheckState()
{
    // Determine if validation is needed based on current state and validation mode
    bool needsValidation = false;
    bool dbValue = false;

    // Get the database value if available
    if (m_hasDatabaseGetter) {
        dbValue = m_databaseValueGetter();
    }

    if (m_requireValidation) {
        if (m_validationMode == ValidateOnBoth) {
            // Always validate when ValidationMode is ValidateOnBoth
            needsValidation = true;
        }
        else if (m_validationMode == ValidateOnUncheck && isChecked()) {
            // Validating on uncheck and currently checked (will be unchecked)
            // Only validate if the database value is TRUE (checked)
            needsValidation = m_hasDatabaseGetter ? dbValue : true;
        }
        else if (m_validationMode == ValidateOnCheck && !isChecked()) {
            // Validating on check and currently unchecked (will be checked)
            // Only validate if the database value is FALSE (unchecked)
            needsValidation = m_hasDatabaseGetter ? !dbValue : true;
        }
    }

    if (needsValidation) {
        // Get grace period if available
        int gracePeriod = m_hasGracePeriodGetter ? m_gracePeriodGetter() : 0;

        // Call the appropriate password validation method
        bool validationPassed;
        if (gracePeriod > 0) {
            validationPassed = PasswordValidation::validatePasswordForOperation(
                this->parentWidget(), m_operationName, m_username, gracePeriod);
        } else {
            validationPassed = PasswordValidation::validatePasswordForOperation(
                this->parentWidget(), m_operationName, m_username);
        }

        if (!validationPassed) {
            // If validation failed, don't change the state
            qDebug() << "Password validation failed for" << m_operationName;
            return;
        }
    }

    // If validation passed or not required, proceed with normal state change
    QCheckBox::nextCheckState();
}
