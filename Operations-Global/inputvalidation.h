#ifndef INPUTVALIDATION_H
#define INPUTVALIDATION_H

#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QLineEdit>
#include <QTextEdit>
#include <QDir>
#include <QFileInfo>
#include <QByteArray>

namespace InputValidation {

enum class InputType {
    PlainText,       // Regular text with basic restrictions
    Username,        // Usernames with stricter character set
    Password,        // Password requirements
    DisplayName,     // Display names with some restrictions
    FileName,        // Valid file names
    FilePath,        // File paths
    DiaryContent,    // Content for diary entries
    ColorName,        // Color names (for your color settings)
    Line,
    TaskListName
};

struct ValidationResult {
    bool isValid;
    QString errorMessage;
};

// The common passwords deny list (defined in cpp file)
extern const QStringList commonPasswords;

// Main validation function
ValidationResult validateInput(const QString& input, InputType type, int maxLength = 1000);

// Helper functions for Qt widgets
bool validateLineEdit(QLineEdit* lineEdit, InputType type, int maxLength = 1000);
bool validateTextEdit(QTextEdit* textEdit, InputType type, int maxLength = 10000);

//Helper function to validate encryption key
bool validateEncryptionKey(const QString& filePath, const QByteArray& expectedEncryptionKey);
// Validate diary file integrity
bool validateDiaryFile(const QString& filePath, const QByteArray& expectedEncryptionKey);
bool validateDiaryFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence);
// Validate password file integrity
bool validatePasswordFile(const QString& filePath, const QByteArray& expectedEncryptionKey);
bool validatePasswordFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence);
// Validate tasklist file integrity
bool validateTasklistFile(const QString& filePath, const QByteArray& expectedEncryptionKey);
bool validateTasklistFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence);

} // namespace InputValidation

#endif // INPUTVALIDATION_H
