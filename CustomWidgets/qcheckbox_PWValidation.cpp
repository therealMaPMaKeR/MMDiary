#include "qcheckbox_PWValidation.h"
#include "../Operations-Global/passwordvalidation.h"
#include <QDebug>

qcheckbox_PWValidation::qcheckbox_PWValidation(QWidget *parent)
    : QCheckBox(parent)
    , m_operationName("")
    , m_username("")
    , m_requireValidation(false)
    , m_validationMode(ValidateOnUncheck) // Default to validate when unchecking
    , m_hasDatabaseGetter(false)
    , m_hasGracePeriodGetter(false)
{
    qDebug() << "qcheckbox_PWValidation: Constructor called";
}

void qcheckbox_PWValidation::setGracePeriodGetter(const std::function<int()>& getter)
{
    qDebug() << "qcheckbox_PWValidation: setGracePeriodGetter called";
    m_gracePeriodGetter = getter;
    m_hasGracePeriodGetter = true;
}

void qcheckbox_PWValidation::setValidationInfo(const QString& operationName, const QString& username)
{
    qDebug() << "qcheckbox_PWValidation: setValidationInfo called for operation:" << operationName;
    m_operationName = operationName;
    m_username = username;
}

void qcheckbox_PWValidation::setRequireValidation(bool require)
{
    qDebug() << "qcheckbox_PWValidation: setRequireValidation called with require:" << require;
    m_requireValidation = require;
}

void qcheckbox_PWValidation::setValidationMode(ValidationMode mode)
{
    qDebug() << "qcheckbox_PWValidation: setValidationMode called with mode:" << mode;
    m_validationMode = mode;
}

void qcheckbox_PWValidation::setDatabaseValueGetter(const std::function<bool()>& getter)
{
    qDebug() << "qcheckbox_PWValidation: setDatabaseValueGetter called";
    m_databaseValueGetter = getter;
    m_hasDatabaseGetter = true;
}

void qcheckbox_PWValidation::nextCheckState()
{
    qDebug() << "qcheckbox_PWValidation: nextCheckState called";
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
        qDebug() << "qcheckbox_PWValidation: Password validation required for operation:" << m_operationName;
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
            qDebug() << "qcheckbox_PWValidation: Password validation failed for" << m_operationName;
            return;
        }
        qDebug() << "qcheckbox_PWValidation: Password validation passed for" << m_operationName;
    }

    // If validation passed or not required, proceed with normal state change
    QCheckBox::nextCheckState();
}
