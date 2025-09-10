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
    FilePath,        // File paths (restricted to Data directory)
    ExternalFilePath,// External file paths (not restricted to Data directory)
    DiaryContent,    // Content for diary entries
    ColorName,       // Color names (for your color settings)
    Line,
    TaskListName,
    CategoryTag,     // Categories and tags for encrypted files
    TVShowName       // TV show names (allows special characters like colons, apostrophes, etc.)
};

struct ValidationResult {
    bool isValid;
    QString errorMessage;
};

// File format validation structure
struct FileValidationResult {
    bool isValid;
    bool hasValidHeader;
    bool contentMatchesExtension;
    QString detectedMimeType;
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
bool validateEncryptionKey(const QString& filePath, const QByteArray& expectedEncryptionKey, bool useNewMetadataFormat);
// Validate diary file integrity
bool validateDiaryFile(const QString& filePath, const QByteArray& expectedEncryptionKey);
bool validateDiaryFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence);
// Validate password file integrity
bool validatePasswordFile(const QString& filePath, const QByteArray& expectedEncryptionKey);
bool validatePasswordFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence);
// Validate tasklist file integrity
bool validateTasklistFile(const QString& filePath, const QByteArray& expectedEncryptionKey);
bool validateTasklistFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence);

// New file format validation functions
FileValidationResult validateFileFormat(const QString& filePath);
bool isValidImageFile(const QString& filePath);
bool isValidVideoFile(const QString& filePath);
bool isValidAudioFile(const QString& filePath);
bool checkFileHeader(const QString& filePath, const QByteArray& expectedMagic, int offset = 0);
QString detectMimeType(const QString& filePath);
bool hasValidFileStructure(const QString& filePath, qint64 maxSize = -1);

} // namespace InputValidation

#endif // INPUTVALIDATION_H
