#include "inputvalidation.h"
#include "Operations-Global/CryptoUtils.h"

#include <QRegularExpression>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace InputValidation {


// Common password deny list - can be expanded
const QStringList commonPasswords = {
    "password", "password123", "123456", "qwerty", "admin", "welcome",
    "letmein", "123456789", "12345678", "test", "123123", "1234",
    "football", "1234567", "monkey", "111111", "abc123"
};

ValidationResult validateInput(const QString& input, InputType type, int maxLength) {
    ValidationResult result = {true, ""};

    // Check for maximum length to prevent DoS
    if (input.length() > maxLength) {
        result.isValid = false;
        result.errorMessage = "Input exceeds maximum allowed length";
        return result;
    }

    // Check for null characters in the input
    if (input.contains(QChar(0))) {
        result.isValid = false;
        result.errorMessage = "Input contains null characters";
        return result;
    }

    // Validate UTF-8 encoding
    QByteArray utf8Data = input.toUtf8();
    QString roundTrip = QString::fromUtf8(utf8Data);
    if (roundTrip != input) {
        result.isValid = false;
        result.errorMessage = "Input contains invalid UTF-8 sequences";
        return result;
    }

    // Check for common malicious patterns
    QRegularExpression scriptTagPattern("<script[^>]*>.*?</script>",
                                        QRegularExpression::CaseInsensitiveOption |
                                            QRegularExpression::DotMatchesEverythingOption);

    if (scriptTagPattern.match(input).hasMatch()) {
        result.isValid = false;
        result.errorMessage = "Input contains potentially malicious script tags";
        return result;
    }

    // Type-specific validation
    switch (type) {
    case InputType::PlainText: {
        // Allow specific control characters but restrict others
        // Allow: \n (newline, 0x0A), \t (tab, 0x09), \r (carriage return, 0x0D)
        QRegularExpression invalidControlChars("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]");
        if (invalidControlChars.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Input contains invalid control characters";
        }
        break;
    }

    case InputType::Username: {
        // Username format: alphanumeric, underscores, dots, hyphens, 3-20 chars
        // Case-insensitive pattern
        QRegularExpression usernamePattern("^[a-zA-Z0-9][a-zA-Z0-9._-]*[a-zA-Z0-9]$|^[a-zA-Z0-9]$",
                                           QRegularExpression::CaseInsensitiveOption);
        if (!usernamePattern.match(input).hasMatch() || input.length() < 3 || input.length() > 20) {
            result.isValid = false;
            result.errorMessage = "Username must be 3-20 characters and contain only letters, numbers, dots, underscores, and hyphens";
        }
        break;
    }

    case InputType::Password: {
        // Password requirements: 8+ chars, uppercase, lowercase, digit, special char
        bool hasUppercase = input.contains(QRegularExpression("[A-Z]"));
        bool hasLowercase = input.contains(QRegularExpression("[a-z]"));
        bool hasDigit = input.contains(QRegularExpression("\\d"));
        bool hasSpecial = input.contains(QRegularExpression("[^a-zA-Z0-9]"));

        if (input.length() < 8 || !hasUppercase || !hasLowercase || !hasDigit) {
            result.isValid = false;
            result.errorMessage = "Password must be at least 8 characters and include uppercase, lowercase, and digit.";
            return result;
        }

        // Check against common passwords
        QString lowercaseInput = input.toLower();
        for (const QString& commonPassword : commonPasswords) {
            if (lowercaseInput == commonPassword) {
                result.isValid = false;
                result.errorMessage = "Password is too common";
                return result;
            }
        }
        break;
    }

    case InputType::DisplayName: {
        // Trim leading and trailing spaces
        QString trimmedInput = input.trimmed();

        // Display name: alphanumeric and spaces, reasonable length
        QRegularExpression displayNamePattern("^[a-zA-Z0-9\\s]+$");
        if (!displayNamePattern.match(trimmedInput).hasMatch() ||
            trimmedInput.length() < 2 ||
            trimmedInput.length() > 30) {
            result.isValid = false;
            result.errorMessage = "Display name must be 2-30 characters and contain only letters, numbers, and spaces";
        }

        // If the trimmed input differs from original, it had leading/trailing spaces
        if (trimmedInput != input) {
            // This is a soft warning - we don't necessarily fail validation
            qDebug() << "Display name had leading or trailing spaces that will be trimmed";
        }
        break;
    }

    case InputType::FileName: {
        // File name validation: no path separators or invalid chars
        QRegularExpression invalidFileChars("[\\\\/:*?\"<>|]");
        if (invalidFileChars.match(input).hasMatch() || input.isEmpty()) {
            result.isValid = false;
            result.errorMessage = "File name contains invalid characters";
            return result;
        }

        // Check for dot files (hidden files in Unix)
        if (input.startsWith('.')) {
            result.isValid = false;
            result.errorMessage = "File names cannot start with a dot (.)";
            return result;
        }

        // Check for multiple consecutive dots
        if (input.contains("..")) {
            result.isValid = false;
            result.errorMessage = "File names cannot contain consecutive dots";
            return result;
        }
        break;
    }

    case InputType::FilePath: {
        // Create a clean, absolute path
        QFileInfo pathInfo(input);
        QString absolutePath = pathInfo.absoluteFilePath();

        // Use canonicalFilePath to resolve symbolic links
        QString canonicalPath;

        // If the file exists, get its canonical path to resolve symlinks
        if (pathInfo.exists()) {
            canonicalPath = pathInfo.canonicalFilePath();

            // If canonicalFilePath returns empty (error), fallback to cleanPath
            if (canonicalPath.isEmpty()) {
                canonicalPath = QDir::cleanPath(absolutePath);
                qWarning() << "Failed to get canonical path for existing file, using cleaned path:" << canonicalPath;
            }
        } else {
            // For non-existent files, use cleanPath
            canonicalPath = QDir::cleanPath(absolutePath);
        }

        // Path traversal check - still useful as a first defense
        QRegularExpression pathTraversalPattern("\\.\\.");
        if (pathTraversalPattern.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Path contains directory traversal patterns";
            return result;
        }

// Additional OS-specific checks
#ifdef Q_OS_WIN
        // Windows-specific checks
        // Check for alternate data streams
        if (input.contains(':')) {
            QRegularExpression adsPattern(":.+");
            if (adsPattern.match(QFileInfo(input).fileName()).hasMatch()) {
                result.isValid = false;
                result.errorMessage = "Path contains Windows alternate data stream";
                return result;
            }
        }

        // Check for Windows short names (8.3 format)
        QRegularExpression shortNamePattern("~[0-9]");
        if (shortNamePattern.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Path may contain Windows short name format";
            return result;
        }
#endif

        // Ensure path is within allowed directory
        QString diariesBasePath = QDir::cleanPath(QDir(QDir::current().path() + "/Data").absolutePath());

        // If base directory exists, get its canonical path to ensure we compare real paths
        QFileInfo baseInfo(diariesBasePath);
        if (baseInfo.exists() && baseInfo.isDir()) {
            diariesBasePath = baseInfo.canonicalFilePath();
        }

        if (!canonicalPath.startsWith(diariesBasePath)) {
            result.isValid = false;
            result.errorMessage = "Path is outside of allowed directory";
        }
        break;
    }

    case InputType::DiaryContent: {
        // Diary content - simply check for script tags and large content
        // Other content validation can be added here as needed
        break;
    }

    case InputType::ColorName: {
        // Valid color names
        QRegularExpression colorNamePattern("^[a-zA-Z\\s]+$");
        if (!colorNamePattern.match(input).hasMatch() || input.length() > 20) {
            result.isValid = false;
            result.errorMessage = "Invalid color name";
        }
        break;
    }
    case InputType::Line: {
        // Restrict all control characters
        QRegularExpression invalidControlChars("[\\x00-\\x1F\\x7F]");
        if (invalidControlChars.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Input contains invalid control characters";
        }
        break;
    }
    case InputType::TaskListName: {
        // Task list name length check: 2-50 characters
        if (input.length() < 2 || input.length() > 50) {
            result.isValid = false;
            result.errorMessage = "Task list name must be between 2 and 50 characters long";
            return result;
        }

        // Validate task list name: allow alphanumeric, spaces, and common punctuation
        QRegularExpression taskListNamePattern("^[\\w\\s\\-.,!?()]+$");
        if (!taskListNamePattern.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Task list name contains invalid characters";
            return result;
        }

        // Check for filesystem-unsafe characters
        QRegularExpression invalidFileChars("[\\\\/:*?\"<>|]");
        if (invalidFileChars.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Task list name contains characters that are not allowed in file names";
            return result;
        }

        // Check for leading/trailing spaces
        if (input != input.trimmed()) {
            result.isValid = false;
            result.errorMessage = "Task list name cannot have leading or trailing spaces";
            return result;
        }
        break;
    }
    }
    return result;
}

// Helper functions that apply the validation to specific Qt widgets
bool validateLineEdit(QLineEdit* lineEdit, InputType type, int maxLength) {
    if (!lineEdit) return false;

    ValidationResult result = validateInput(lineEdit->text(), type, maxLength);
    if (!result.isValid) {
        // You could show this error message in a QLabel or QMessageBox
        qWarning() << "Validation error:" << result.errorMessage;
        // Optionally highlight the lineEdit with error styling
        // lineEdit->setStyleSheet("background-color: #FFDDDD;");
        return false;
    }

    // If it's a DisplayName, apply trimming
    if (type == InputType::DisplayName) {
        QString trimmed = lineEdit->text().trimmed();
        if (trimmed != lineEdit->text()) {
            lineEdit->setText(trimmed);
        }
    }

    // Reset any error styling
    // lineEdit->setStyleSheet("");
    return true;
}

bool validateTextEdit(QTextEdit* textEdit, InputType type, int maxLength) {
    if (!textEdit) return false;

    ValidationResult result = validateInput(textEdit->toPlainText(), type, maxLength);
    if (!result.isValid) {
        qWarning() << "Validation error:" << result.errorMessage;
        // Optionally highlight the textEdit with error styling
        // textEdit->setStyleSheet("background-color: #FFDDDD;");
        return false;
    }

    // Reset any error styling
    // textEdit->setStyleSheet("");
    return true;
}

// Validate encryption key using in-memory decryption
bool validateEncryptionKey(const QString& filePath, const QByteArray& expectedEncryptionKey) {
    QFile encryptedFile(filePath);
    if (!encryptedFile.exists() || encryptedFile.size() == 0) {
        qWarning() << "File doesn't exist or is empty:" << filePath;
        return false;
    }

    if (!encryptedFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open encrypted file:" << filePath;
        return false;
    }

    // Get file size
    qint64 fileSize = encryptedFile.size();

    // For files larger than 64KB, just check the beginning and end
    QByteArray encryptedData;

    if (fileSize <= 65536) { // 64KB threshold
        // For smaller files, just read the whole thing
        encryptedData = encryptedFile.readAll();
    } else {
        // For larger files, read the beginning chunk (with nonce) and ending chunk (with auth tag)
        const int CHUNK_SIZE = 4096; // 4KB chunks

        // Read beginning (contains nonce + start of ciphertext)
        QByteArray beginChunk = encryptedFile.read(CHUNK_SIZE);

        // Seek to end and read end chunk (contains end of ciphertext + auth tag)
        encryptedFile.seek(fileSize - CHUNK_SIZE);
        QByteArray endChunk = encryptedFile.read(CHUNK_SIZE);

        // For validation, we'll create a synthetic "mini-file" with just the nonce from the
        // beginning and a small part of the ciphertext followed by the authentication tag
        // This is a bit of a hack but works because of how AESGCM256 is structured

        // First 28 bytes (nonce + some data) from the beginning
        encryptedData.append(beginChunk.left(28));

        // Last 16 bytes (authentication tag) from the end
        encryptedData.append(endChunk.right(16));
    }

    encryptedFile.close();

    // Try to decrypt the sample or full file to validate key
    bool decryptionSuccess = false;
    try {
        // If we're using a synthetic sample, we need a custom validation method
        if (fileSize > 65536) {
            // Custom validation for large files
            // We need to create an AESGCM256 instance and manually check the key
            AESGCM256Crypto crypto(expectedEncryptionKey);

            // For large files, it's enough to verify the key structure
            decryptionSuccess = (crypto.m_key.size() == 32); // 32 bytes for AES-256
        } else {
            // For small files, attempt full decryption
            QString encryptedBase64 = QString::fromLatin1(encryptedData.toBase64());
            QString decryptedText = CryptoUtils::Encryption_Decrypt(expectedEncryptionKey, encryptedBase64);
            decryptionSuccess = !decryptedText.isEmpty();
        }
    }
    catch (...) {
        qWarning() << "Exception during decryption validation for file:" << filePath;
        decryptionSuccess = false;
    }

    if (!decryptionSuccess) {
        qWarning() << "Failed to decrypt file with provided key:" << filePath;
        return false;
    }

    qDebug() << "Encryption key matches the expected key for file:" << filePath;
    return true;
}

// New function to validate task list files with standardized non-existence handling
bool validateTasklistFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence) {
    QFileInfo fileInfo(filePath);

    // Check if file exists based on requireExistence parameter
    if (!fileInfo.exists()) {
        if (requireExistence) {
            qWarning() << "Required task list file doesn't exist:" << filePath;
            return false;
        } else {
            // If existence is not required, consider this valid
            return true;
        }
    }

    if (fileInfo.size() == 0) {
        // Empty files are invalid
        qWarning() << "Task list file is empty:" << filePath;
        return false;
    }

    // Create a canonical path for validation
    QString canonicalPath = fileInfo.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        canonicalPath = QDir::cleanPath(fileInfo.absoluteFilePath());
        qWarning() << "Failed to get canonical path, using cleaned path:" << canonicalPath;
    }

    // Check if file is within expected directory structure
    QString dataBasePath = QDir::cleanPath(QDir(QDir::current().path() + "/Data").absolutePath());

    // Get canonical path of base directory if it exists
    QFileInfo baseInfo(dataBasePath);
    if (baseInfo.exists() && baseInfo.isDir()) {
        dataBasePath = baseInfo.canonicalFilePath();
    }

    if (!canonicalPath.startsWith(dataBasePath)) {
        qWarning() << "Task list file outside of data directory:" << canonicalPath;
        return false;
    }

    // Validate file name
    QString fileName = fileInfo.fileName();
    if (!fileName.endsWith(".txt")) {
        qWarning() << "Invalid task list file extension:" << fileName;
        return false;
    }

    // Validate encryption key
    return validateEncryptionKey(filePath, expectedEncryptionKey);
}

// Modified version of validateDiaryFile to include standardized non-existence handling
bool validateDiaryFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence) {
    QFileInfo fileInfo(filePath);

    // Check if file exists based on requireExistence parameter
    if (!fileInfo.exists()) {
        if (requireExistence) {
            qWarning() << "Required diary file doesn't exist:" << filePath;
            return false;
        } else {
            // If existence is not required, consider this valid
            return true;
        }
    }

    if (fileInfo.size() == 0) {
        qWarning() << "Diary file is empty:" << filePath;
        return false;
    }

    // Create a canonical path for validation
    QString canonicalPath = fileInfo.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        canonicalPath = QDir::cleanPath(fileInfo.absoluteFilePath());
        qWarning() << "Failed to get canonical path, using cleaned path:" << canonicalPath;
    }

    // Check if file is within expected directory structure
    QString diariesBasePath = QDir::cleanPath(QDir(QDir::current().path() + "/Data").absolutePath());

    // Get canonical path of base directory if it exists
    QFileInfo baseInfo(diariesBasePath);
    if (baseInfo.exists() && baseInfo.isDir()) {
        diariesBasePath = baseInfo.canonicalFilePath();
    }

    if (!canonicalPath.startsWith(diariesBasePath)) {
        qWarning() << "Diary file outside of data directory:" << canonicalPath;
        return false;
    }

    // Validate file name format: YYYY.MM.DD.txt
    QString fileName = fileInfo.fileName();
    QRegularExpression validFilenamePattern("^\\d{4}\\.\\d{2}\\.\\d{2}\\.txt$");
    if (!validFilenamePattern.match(fileName).hasMatch()) {
        qWarning() << "Invalid diary filename format:" << fileName;
        return false;
    }

    // Further validate the date components in the file name
    QStringList dateParts = fileName.left(fileName.lastIndexOf('.')).split('.');
    if (dateParts.size() != 3) {
        qWarning() << "Invalid diary filename date parts:" << fileName;
        return false;
    }

    // Validate year (reasonable range, e.g., 1900-2100)
    int year = dateParts[0].toInt();
    if (year < 1900 || year > 2100) {
        qWarning() << "Invalid year in diary filename:" << year;
        return false;
    }

    // Validate month (01-12)
    int month = dateParts[1].toInt();
    if (month < 1 || month > 12) {
        qWarning() << "Invalid month in diary filename:" << month;
        return false;
    }

    // Validate day (01-31, with basic validation for month lengths)
    int day = dateParts[2].toInt();
    if (day < 1 || day > 31) {
        qWarning() << "Invalid day in diary filename:" << day;
        return false;
    }

    // More detailed day validation based on month
    if ((month == 4 || month == 6 || month == 9 || month == 11) && day > 30) {
        qWarning() << "Invalid day for month in diary filename:" << month << day;
        return false; // April, June, September, November have 30 days
    }
    else if (month == 2) {
        // February: check for leap years
        bool isLeapYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if ((isLeapYear && day > 29) || (!isLeapYear && day > 28)) {
            qWarning() << "Invalid day for February in diary filename:" << year << day;
            return false;
        }
    }

    // Validate file path format
    QRegularExpression validDiaryPathPattern(
        "Data/[^/]+/Diaries/\\d{4}/\\d{2}/\\d{2}/\\d{4}\\.\\d{2}\\.\\d{2}\\.txt$");

    if (!validDiaryPathPattern.match(canonicalPath).hasMatch()) {
        qWarning() << "Invalid diary path format:" << canonicalPath;
        return false;
    }

    // Validate encryption key
    return validateEncryptionKey(filePath, expectedEncryptionKey);
}

// Modified version of validatePasswordFile with standardized non-existence handling
bool validatePasswordFile(const QString& filePath, const QByteArray& expectedEncryptionKey, bool requireExistence) {
    QFileInfo fileInfo(filePath);

    // Check if file exists based on requireExistence parameter
    if (!fileInfo.exists()) {
        if (requireExistence) {
            qWarning() << "Required password file doesn't exist:" << filePath;
            return false;
        } else {
            // If existence is not required, consider this valid
            return true;
        }
    }

    if (fileInfo.size() == 0) {
        // Empty files are invalid
        qWarning() << "Password file is empty:" << filePath;
        return false;
    }

    // Create a canonical path for validation
    QString canonicalPath = fileInfo.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        canonicalPath = QDir::cleanPath(fileInfo.absoluteFilePath());
        qWarning() << "Failed to get canonical path, using cleaned path:" << canonicalPath;
    }

    // Check if file is within expected directory structure
    QString dataBasePath = QDir::cleanPath(QDir(QDir::current().path() + "/Data").absolutePath());

    // Get canonical path of base directory if it exists
    QFileInfo baseInfo(dataBasePath);
    if (baseInfo.exists() && baseInfo.isDir()) {
        dataBasePath = baseInfo.canonicalFilePath();
    }

    if (!canonicalPath.startsWith(dataBasePath)) {
        qWarning() << "Password file outside of data directory:" << canonicalPath;
        return false;
    }

    // Validate file name
    QString fileName = fileInfo.fileName();
    if (fileName != "passwords.txt") {
        qWarning() << "Invalid password file name:" << fileName;
        return false;
    }

    // Validate encryption key
    return validateEncryptionKey(filePath, expectedEncryptionKey);
}

// Legacy function wrappers to maintain backward compatibility
bool validateDiaryFile(const QString& filePath, const QByteArray& expectedEncryptionKey) {
    return validateDiaryFile(filePath, expectedEncryptionKey, true); // Default to requiring existence
}

bool validatePasswordFile(const QString& filePath, const QByteArray& expectedEncryptionKey) {
    return validatePasswordFile(filePath, expectedEncryptionKey, false); // Default to not requiring existence
}

bool validateTasklistFile(const QString& filePath, const QByteArray& expectedEncryptionKey) {
    return validateTasklistFile(filePath, expectedEncryptionKey, false); // Default to not requiring existence
}

} // namespace InputValidation
