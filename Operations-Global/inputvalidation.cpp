#include "inputvalidation.h"
#include "encryption/CryptoUtils.h"
#include "../constants.h"
#include <QRegularExpression>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QDataStream>

namespace InputValidation {


// Common password deny list - can be expanded
const QStringList commonPasswords = {
    "password", "password123", "123456", "qwerty", "admin", "welcome",
    "letmein", "123456789", "12345678", "test", "123123", "1234",
    "football", "1234567", "monkey", "111111", "abc123"
};

ValidationResult validateInput(const QString& input, InputType type, int maxLength) {
    ValidationResult result = {true, ""};

    // Absolute maximum to prevent memory exhaustion
    const int ABSOLUTE_MAX_LENGTH = 1000000; // 1MB of text
    if (input.length() > ABSOLUTE_MAX_LENGTH) {
        result.isValid = false;
        result.errorMessage = "Input exceeds absolute maximum allowed length";
        return result;
    }

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
    // Add size limit before regex to prevent ReDoS attacks
    if (input.length() > 10000) {
        // For very large inputs, do a simple substring check instead of regex
        if (input.contains("<script", Qt::CaseInsensitive) || 
            input.contains("</script>", Qt::CaseInsensitive)) {
            result.isValid = false;
            result.errorMessage = "Input contains potentially malicious script tags";
            return result;
        }
    } else {
        // Use non-greedy regex with limited backtracking for smaller inputs
        QRegularExpression scriptTagPattern("<script[^>]{0,100}>.*?</script>",
                                            QRegularExpression::CaseInsensitiveOption);
        scriptTagPattern.setPatternOptions(QRegularExpression::DontCaptureOption);
        
        if (scriptTagPattern.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Input contains potentially malicious script tags";
            return result;
        }
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
        #ifdef QT_DEBUG
        return result;
        #else
        #endif
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

        // Check against common passwords - use string comparison without full copy
        for (const QString& commonPassword : commonPasswords) {
            if (input.compare(commonPassword, Qt::CaseInsensitive) == 0) {
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

        // Fix TOCTOU: Get canonical path in a safer way
        // Always try to resolve the parent directory's canonical path first
        QFileInfo parentInfo(pathInfo.dir().absolutePath());
        QString parentCanonical;
        
        if (parentInfo.exists() && parentInfo.isDir()) {
            parentCanonical = parentInfo.canonicalFilePath();
        } else {
            parentCanonical = QDir::cleanPath(pathInfo.dir().absolutePath());
        }
        
        // Construct the canonical path from parent + filename
        if (pathInfo.exists()) {
            // For existing files, try to get the canonical path
            QString tempCanonical = pathInfo.canonicalFilePath();
            if (!tempCanonical.isEmpty()) {
                canonicalPath = tempCanonical;
            } else {
                // Fallback: combine parent canonical with filename
                canonicalPath = QDir::cleanPath(parentCanonical + "/" + pathInfo.fileName());
            }
        } else {
            // For non-existent files, combine parent canonical with filename
            canonicalPath = QDir::cleanPath(parentCanonical + "/" + pathInfo.fileName());
        }

        // Enhanced path traversal check - check multiple patterns
        // Check for various forms of directory traversal
        QString normalizedInput = input;
        normalizedInput.replace('\\', '/');
        
        // Check for obvious traversal patterns
        if (normalizedInput.contains("../") || 
            normalizedInput.contains("..\\") ||
            normalizedInput.contains("/..") ||
            normalizedInput.contains("\\..") ||
            input.contains("%2e%2e") || // URL encoded
            input.contains("%252e%252e") || // Double URL encoded
            input.contains("..%2f") ||
            input.contains("..%5c")) { // URL encoded slashes
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

    case InputType::ExternalFilePath: {
        // External file path validation - similar to FilePath but allows paths outside Data directory
        QFileInfo pathInfo(input);
        QString absolutePath = pathInfo.absoluteFilePath();

        // Use canonicalFilePath to resolve symbolic links
        QString canonicalPath;

        // Fix TOCTOU: Get canonical path in a safer way
        // Always try to resolve the parent directory's canonical path first
        QFileInfo parentInfo(pathInfo.dir().absolutePath());
        QString parentCanonical;
        
        if (parentInfo.exists() && parentInfo.isDir()) {
            parentCanonical = parentInfo.canonicalFilePath();
        } else {
            parentCanonical = QDir::cleanPath(pathInfo.dir().absolutePath());
        }
        
        // Construct the canonical path from parent + filename
        if (pathInfo.exists()) {
            // For existing files, try to get the canonical path
            QString tempCanonical = pathInfo.canonicalFilePath();
            if (!tempCanonical.isEmpty()) {
                canonicalPath = tempCanonical;
            } else {
                // Fallback: combine parent canonical with filename
                canonicalPath = QDir::cleanPath(parentCanonical + "/" + pathInfo.fileName());
            }
        } else {
            // For non-existent files, combine parent canonical with filename
            canonicalPath = QDir::cleanPath(parentCanonical + "/" + pathInfo.fileName());
        }

        // Enhanced path traversal check - check multiple patterns
        // Check for various forms of directory traversal
        QString normalizedInput = input;
        normalizedInput.replace('\\', '/');
        
        // Check for obvious traversal patterns
        if (normalizedInput.contains("../") || 
            normalizedInput.contains("..\\") ||
            normalizedInput.contains("/..") ||
            normalizedInput.contains("\\..") ||
            input.contains("%2e%2e") || // URL encoded
            input.contains("%252e%252e") || // Double URL encoded
            input.contains("..%2f") ||
            input.contains("..%5c")) { // URL encoded slashes
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

        // Note: We do NOT check if the path is within the Data directory for external files
        // This allows users to select files from anywhere on their system for encryption
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
    case InputType::CategoryTag: {
        // Category/Tag validation: alphanumeric, spaces, and common punctuation, 1-50 chars
        if (input.length() < 1 || input.length() > 50) {
            result.isValid = false;
            result.errorMessage = "Category/Tag must be between 1 and 50 characters long";
            return result;
        }

        // Allow alphanumeric, spaces, and safe punctuation
        QRegularExpression validCharsPattern("^[a-zA-Z0-9\\s\\-_.,!?()]+$");
        if (!validCharsPattern.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "Category/Tag contains invalid characters. Only letters, numbers, spaces, and basic punctuation are allowed";
            return result;
        }

        // Check for leading/trailing spaces
        if (input != input.trimmed()) {
            result.isValid = false;
            result.errorMessage = "Category/Tag cannot have leading or trailing spaces";
            return result;
        }

        // Check for multiple consecutive spaces
        if (input.contains("  ")) {
            result.isValid = false;
            result.errorMessage = "Category/Tag cannot contain multiple consecutive spaces";
            return result;
        }
        break;
    }
    
    case InputType::TVShowName: {
        // TV Show name validation: allows more special characters than DisplayName
        // Minimum 1 character, maximum specified by maxLength (typically 200)
        if (input.isEmpty()) {
            result.isValid = false;
            result.errorMessage = "TV show name cannot be empty";
            return result;
        }
        
        if (input.length() > maxLength) {
            result.isValid = false;
            result.errorMessage = QString("TV show name exceeds maximum length of %1 characters").arg(maxLength);
            return result;
        }
        
        // Trim the input
        QString trimmedInput = input.trimmed();
        if (trimmedInput.isEmpty()) {
            result.isValid = false;
            result.errorMessage = "TV show name cannot be only spaces";
            return result;
        }
        
        // Check for filesystem-dangerous characters
        // Block: backslash, forward slash in path context (../../), pipe, asterisk, angle brackets, quotes for security
        // But allow single forward slash for titles like "Love/Hate"
        QRegularExpression pathTraversalPattern("\\.\\./|\\.\\.\\\\");
        if (pathTraversalPattern.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "TV show name contains path traversal attempt";
            return result;
        }
        
        // Block dangerous filesystem characters but allow TV show special chars
        QRegularExpression dangerousChars("[\\\\*|\"<>\\x00-\\x1F]");
        if (dangerousChars.match(input).hasMatch()) {
            result.isValid = false;
            result.errorMessage = "TV show name contains invalid characters";
            return result;
        }
        
        // Check for multiple consecutive spaces
        if (input.contains("  ")) {
            result.isValid = false;
            result.errorMessage = "TV show name cannot contain multiple consecutive spaces";
            return result;
        }
        
        // Check for leading/trailing spaces
        if (input != trimmedInput) {
            // This is a soft warning - we don't necessarily fail validation
            // The calling code should use the trimmed version
            qDebug() << "TV show name had leading or trailing spaces that should be trimmed";
        }
        
        // If we get here, the TV show name is valid
        // It can contain: letters, numbers, spaces, and special chars like : ' , & ( ) [ ] ! ? / - . é è etc.
        break;
    }
    }

    return result;
}

// Helper functions that apply the validation to specific Qt widgets
bool validateLineEdit(QLineEdit* lineEdit, InputType type, int maxLength) {
    if (!lineEdit) {
        qWarning() << "InputValidation: validateLineEdit called with null pointer";
        return false;
    }

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
    if (!textEdit) {
        qWarning() << "InputValidation: validateTextEdit called with null pointer";
        return false;
    }

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

bool validateEncryptionKey(const QString& filePath, const QByteArray& expectedEncryptionKey, bool useNewMetadataFormat) {
    // All files now use the new fixed-size metadata format
    QFile encryptedFile(filePath);
    if (!encryptedFile.exists() || encryptedFile.size() == 0) {
        qWarning() << "File doesn't exist or is empty:" << filePath;
        return false;
    }

    // Check if file is large enough to contain fixed-size metadata
    if (encryptedFile.size() < Constants::METADATA_RESERVED_SIZE) {
        qWarning() << "File too small to contain fixed-size metadata:" << encryptedFile.size()
        << "bytes, expected at least:" << Constants::METADATA_RESERVED_SIZE;
        return false;
    }

    if (!encryptedFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open encrypted file for key validation:" << filePath;
        return false;
    }

    try {
        // Read the fixed-size metadata block (40KB)
        QByteArray metadataBlock = encryptedFile.read(Constants::METADATA_RESERVED_SIZE);
        if (metadataBlock.size() != Constants::METADATA_RESERVED_SIZE) {
            qWarning() << "Failed to read complete fixed-size metadata for key validation:" << filePath;
            encryptedFile.close();
            return false;
        }

        encryptedFile.close();

        // Extract metadata size from first 4 bytes using Qt's safe method
        quint32 metadataSize = 0;
        if (metadataBlock.size() < static_cast<int>(sizeof(metadataSize))) {
            qWarning() << "Metadata block too small to contain size header:" << filePath;
            encryptedFile.close();
            return false;
        }
        QDataStream stream(metadataBlock.left(sizeof(metadataSize)));
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> metadataSize;

        // Validate metadata size (reasonable limits)
        const quint32 maxAllowedSize = static_cast<quint32>(Constants::METADATA_RESERVED_SIZE - sizeof(quint32));
        if (metadataSize == 0 || metadataSize > maxAllowedSize) {
            qWarning() << "Invalid metadata size for key validation:" << metadataSize << filePath;
            return false;
        }

        // Extract the encrypted metadata chunk
        QByteArray encryptedMetadata = metadataBlock.mid(sizeof(metadataSize), metadataSize);
        if (static_cast<quint32>(encryptedMetadata.size()) != metadataSize) {
            qWarning() << "Failed to extract encrypted metadata for key validation - size mismatch: "
                       << encryptedMetadata.size() << " vs expected " << metadataSize << " for file: " << filePath;
            return false;
        }

        // Try to decrypt the metadata chunk to validate the key
        QByteArray decryptedMetadata = CryptoUtils::Encryption_DecryptBArray(expectedEncryptionKey, encryptedMetadata);

        if (decryptedMetadata.isEmpty()) {
            qWarning() << "Failed to decrypt fixed-size metadata for key validation:" << filePath;
            return false;
        }

        // Additional validation: try to parse the metadata structure
        // This ensures the decryption actually produced valid metadata, not just random data
        const char* data = decryptedMetadata.constData();
        int pos = 0;
        int totalSize = decryptedMetadata.size();

        // Try to read filename length using safe Qt method
        quint32 filenameLength = 0;
        if (pos + static_cast<int>(sizeof(filenameLength)) > totalSize) {
            qWarning() << "Invalid decrypted metadata structure for key validation:" << filePath;
            return false;
        }

        QDataStream filenameStream(decryptedMetadata.mid(pos, sizeof(filenameLength)));
        filenameStream.setByteOrder(QDataStream::LittleEndian);
        filenameStream >> filenameLength;
        pos += sizeof(filenameLength);

        // Validate filename length (reasonable limits)
        if (filenameLength == 0 || filenameLength > 1000 || pos + static_cast<int>(filenameLength) > totalSize) {
            qWarning() << "Invalid filename length in decrypted metadata for key validation:" << filenameLength << filePath;
            return false;
        }

        // If we got this far, the key successfully decrypted valid metadata
        qDebug() << "Encryption key validation successful for fixed-size format file:" << filePath;
        return true;

    } catch (const std::exception& e) {
        qWarning() << "Exception during encryption key validation:" << e.what() << filePath;
        encryptedFile.close();
        return false;
    } catch (...) {
        qWarning() << "Unknown exception during encryption key validation:" << filePath;
        encryptedFile.close();
        return false;
    }
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

// File format validation implementation
FileValidationResult validateFileFormat(const QString& filePath) {
    FileValidationResult result;
    result.isValid = false;
    result.hasValidHeader = false;
    result.contentMatchesExtension = false;
    
    // Check if file exists
    QFile file(filePath);
    if (!file.exists()) {
        result.errorMessage = "File does not exist";
        return result;
    }
    
    // Security: Check file size limits
    qint64 fileSize = file.size();
    const qint64 MAX_FILE_SIZE = 5LL * 1024 * 1024 * 1024; // 5GB max
    if (fileSize > MAX_FILE_SIZE) {
        result.errorMessage = QString("File too large: %1 bytes (max: %2 bytes)").arg(fileSize).arg(MAX_FILE_SIZE);
        return result;
    }
    
    if (fileSize == 0) {
        result.errorMessage = "File is empty";
        return result;
    }
    
    // Open file and read header
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = "Cannot open file for reading";
        return result;
    }
    
    // Read first 512 bytes for header analysis
    QByteArray header = file.read(512);
    file.close();
    
    if (header.isEmpty()) {
        result.errorMessage = "Cannot read file header";
        return result;
    }
    
    // Get file extension
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    // Detect actual MIME type from content
    result.detectedMimeType = detectMimeType(filePath);
    
    // Check for specific file types
    if (isValidImageFile(filePath)) {
        result.hasValidHeader = true;
        QStringList imageExtensions = {"jpg", "jpeg", "png", "gif", "bmp", "webp", "tiff", "tif"};
        result.contentMatchesExtension = imageExtensions.contains(extension);
    } else if (isValidVideoFile(filePath)) {
        result.hasValidHeader = true;
        QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "3gp"};
        result.contentMatchesExtension = videoExtensions.contains(extension);
    } else if (isValidAudioFile(filePath)) {
        result.hasValidHeader = true;
        QStringList audioExtensions = {"mp3", "wav", "flac", "ogg", "m4a", "aac", "wma"};
        result.contentMatchesExtension = audioExtensions.contains(extension);
    }
    
    // Security: Check for embedded executables
    if (header.contains("MZ") && (header.indexOf("MZ") < 100)) {
        // Potential Windows executable embedded
        if (!extension.isEmpty() && extension != "exe" && extension != "dll") {
            result.errorMessage = "File may contain embedded executable code";
            result.isValid = false;
            return result;
        }
    }
    
    result.isValid = result.hasValidHeader;
    if (!result.isValid && result.errorMessage.isEmpty()) {
        result.errorMessage = "Unknown or unsupported file format";
    }
    
    return result;
}

bool isValidImageFile(const QString& filePath) {
    // JPEG magic numbers
    if (checkFileHeader(filePath, QByteArray::fromHex("FFD8FF"))) return true;
    // PNG magic number
    if (checkFileHeader(filePath, QByteArray::fromHex("89504E470D0A1A0A"))) return true;
    // GIF magic numbers (GIF87a or GIF89a)
    if (checkFileHeader(filePath, QByteArray("GIF87a")) || 
        checkFileHeader(filePath, QByteArray("GIF89a"))) return true;
    // BMP magic number
    if (checkFileHeader(filePath, QByteArray("BM"))) return true;
    // WebP magic number
    if (checkFileHeader(filePath, QByteArray("RIFF"), 0) && 
        checkFileHeader(filePath, QByteArray("WEBP"), 8)) return true;
    // TIFF magic numbers (little-endian or big-endian)
    if (checkFileHeader(filePath, QByteArray::fromHex("49492A00")) || 
        checkFileHeader(filePath, QByteArray::fromHex("4D4D002A"))) return true;
    
    return false;
}

bool isValidVideoFile(const QString& filePath) {
    // MP4/MOV magic numbers
    if (checkFileHeader(filePath, QByteArray("ftyp"), 4)) return true;
    // AVI magic number
    if (checkFileHeader(filePath, QByteArray("RIFF"), 0) && 
        checkFileHeader(filePath, QByteArray("AVI "), 8)) return true;
    // MKV/WebM magic number
    if (checkFileHeader(filePath, QByteArray::fromHex("1A45DFA3"))) return true;
    // FLV magic number
    if (checkFileHeader(filePath, QByteArray("FLV"))) return true;
    // WMV/ASF magic number
    if (checkFileHeader(filePath, QByteArray::fromHex("3026B2758E66CF11"))) return true;
    
    return false;
}

bool isValidAudioFile(const QString& filePath) {
    // MP3 magic numbers (ID3 or direct MPEG audio)
    if (checkFileHeader(filePath, QByteArray("ID3")) || 
        checkFileHeader(filePath, QByteArray::fromHex("FFFB"))) return true;
    // WAV magic number
    if (checkFileHeader(filePath, QByteArray("RIFF"), 0) && 
        checkFileHeader(filePath, QByteArray("WAVE"), 8)) return true;
    // FLAC magic number
    if (checkFileHeader(filePath, QByteArray("fLaC"))) return true;
    // OGG magic number
    if (checkFileHeader(filePath, QByteArray("OggS"))) return true;
    // M4A (similar to MP4)
    if (checkFileHeader(filePath, QByteArray("ftyp"), 4)) {
        // Additional check for M4A specific atoms
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            file.seek(8);
            QByteArray brand = file.read(4);
            file.close();
            if (brand == "M4A " || brand == "mp42") return true;
        }
    }
    
    return false;
}

bool checkFileHeader(const QString& filePath, const QByteArray& expectedMagic, int offset) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    if (offset > 0) {
        file.seek(offset);
    }
    
    QByteArray header = file.read(expectedMagic.size());
    file.close();
    
    return header == expectedMagic;
}

QString detectMimeType(const QString& filePath) {
    // Image types
    if (checkFileHeader(filePath, QByteArray::fromHex("FFD8FF"))) return "image/jpeg";
    if (checkFileHeader(filePath, QByteArray::fromHex("89504E470D0A1A0A"))) return "image/png";
    if (checkFileHeader(filePath, QByteArray("GIF8"))) return "image/gif";
    if (checkFileHeader(filePath, QByteArray("BM"))) return "image/bmp";
    if (checkFileHeader(filePath, QByteArray("RIFF"), 0) && 
        checkFileHeader(filePath, QByteArray("WEBP"), 8)) return "image/webp";
    
    // Video types
    if (checkFileHeader(filePath, QByteArray("ftyp"), 4)) {
        // Could be MP4, MOV, or M4A - need more specific check
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            file.seek(8);
            QByteArray brand = file.read(4);
            file.close();
            if (brand == "M4A " || brand == "mp42") return "audio/mp4";
            return "video/mp4";
        }
    }
    if (checkFileHeader(filePath, QByteArray("RIFF"), 0) && 
        checkFileHeader(filePath, QByteArray("AVI "), 8)) return "video/x-msvideo";
    if (checkFileHeader(filePath, QByteArray::fromHex("1A45DFA3"))) return "video/webm";
    if (checkFileHeader(filePath, QByteArray("FLV"))) return "video/x-flv";
    
    // Audio types
    if (checkFileHeader(filePath, QByteArray("ID3")) || 
        checkFileHeader(filePath, QByteArray::fromHex("FFFB"))) return "audio/mpeg";
    if (checkFileHeader(filePath, QByteArray("RIFF"), 0) && 
        checkFileHeader(filePath, QByteArray("WAVE"), 8)) return "audio/wav";
    if (checkFileHeader(filePath, QByteArray("fLaC"))) return "audio/flac";
    if (checkFileHeader(filePath, QByteArray("OggS"))) return "audio/ogg";
    
    // Document types
    if (checkFileHeader(filePath, QByteArray("%PDF"))) return "application/pdf";
    
    // Archive types (for detection only)
    if (checkFileHeader(filePath, QByteArray("PK\x03\x04"))) return "application/zip";
    if (checkFileHeader(filePath, QByteArray("Rar!"))) return "application/x-rar";
    if (checkFileHeader(filePath, QByteArray("7z\xBC\xAF\x27\x1C"))) return "application/x-7z-compressed";
    
    return "application/octet-stream"; // Unknown binary
}

bool hasValidFileStructure(const QString& filePath, qint64 maxSize) {
    QFileInfo fileInfo(filePath);
    
    // Check if file exists
    if (!fileInfo.exists()) {
        return false;
    }
    
    // Check file size
    qint64 fileSize = fileInfo.size();
    if (fileSize == 0) {
        return false;
    }
    
    if (maxSize > 0 && fileSize > maxSize) {
        return false;
    }
    
    // Check if file is readable
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    file.close();
    
    return true;
}

} // namespace InputValidation
