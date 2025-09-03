#include "datastorage_field_manager.h"
#include "operations_files.h"
#include "inputvalidation.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <utility> // For std::move

DataStorage_FieldManager::DataStorage_FieldManager(const QByteArray& encryptionKey, const QString& username)
    : m_encryptionKey(encryptionKey)
    , m_username(username)
{
    qDebug() << "DataStorage_FieldManager: Initialized with username:" << m_username;
    qDebug() << "DataStorage_FieldManager: Using field definitions with" 
             << m_fieldDefinitions.getSupportedDataTypes().size() << "supported data types";
}

DataStorage_FieldManager::~DataStorage_FieldManager()
{
    qDebug() << "DataStorage_FieldManager: Destructor called";
}



DataStorage_FieldManager::ValidationResult DataStorage_FieldManager::readAndValidateData(
    const QString& filePath, 
    DataType dataType,
    QMap<QString, QVariant>& data)
{
    qDebug() << "DataStorage_FieldManager: Reading and validating data from:" << filePath;
    
    ValidationResult result;
    data.clear();
    
    // Check if data type is supported
    if (!isDataTypeSupported(dataType)) {
        result.errorMessage = "Unsupported data type";
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Validate file path using InputValidation
    InputValidation::ValidationResult pathValidation = 
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!pathValidation.isValid) {
        result.errorMessage = "Invalid file path: " + pathValidation.errorMessage;
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Check if file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qDebug() << "DataStorage_FieldManager: Data file does not exist, creating with defaults";
        
        // Create data with default values only
        QMap<QString, QVariant> defaultData;
        ValidationResult defaultResult = validateAndFixFields(defaultData, dataType, data);
        
        // Write the default data to file
        if (writeValidatedData(filePath, dataType, data)) {
            result.success = true;
            result.wasModified = true;
            result.addedFields = defaultResult.addedFields;
            qDebug() << "DataStorage_FieldManager: Created new data file with defaults";
        } else {
            result.errorMessage = "Failed to create default data file";
            qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        }
        return result;
    }
    
    // Security check: validate file size before reading
    if (fileInfo.size() > DataStorageLimits::MAX_FILE_SIZE_BYTES) {
        result.errorMessage = QString("File size exceeds maximum allowed (%1 bytes)").arg(DataStorageLimits::MAX_FILE_SIZE_BYTES);
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Read the encrypted data file
    QString dataString;
    if (!OperationsFiles::readEncryptedFile(filePath, m_encryptionKey, dataString)) {
        result.errorMessage = "Failed to read encrypted data file";
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Security check: validate decrypted data size
    if (dataString.size() > DataStorageLimits::MAX_FILE_SIZE_BYTES) {
        result.errorMessage = QString("Decrypted data size exceeds maximum allowed (%1 bytes)").arg(DataStorageLimits::MAX_FILE_SIZE_BYTES);
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Parse the data
    QMap<QString, QVariant> parsedData;
    if (!parseDataString(dataString, parsedData)) {
        result.errorMessage = "Failed to parse data";
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Validate and fix the field structure
    ValidationResult validationResult = validateAndFixFields(parsedData, dataType, data);
    
    // If data was modified, write it back to the file
    if (validationResult.wasModified) {
        if (writeValidatedData(filePath, dataType, data)) {
            qDebug() << "DataStorage_FieldManager: Data file was repaired and saved";
        } else {
            result.errorMessage = "Data validation succeeded but failed to save repaired file";
            qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
            return result;
        }
    }
    
    // Copy validation results
    result.success = validationResult.success;
    result.wasModified = validationResult.wasModified;
    result.addedFields = validationResult.addedFields;
    result.removedFields = validationResult.removedFields;
    result.errorMessage = validationResult.errorMessage;
    
    if (result.success) {
        qDebug() << "DataStorage_FieldManager: Successfully loaded and validated data";
        if (result.wasModified) {
            qDebug() << "DataStorage_FieldManager: Added fields:" << result.addedFields.join(", ");
            qDebug() << "DataStorage_FieldManager: Removed fields:" << result.removedFields.join(", ");
        }
    }
    
    return result;
}

bool DataStorage_FieldManager::writeValidatedData(const QString& filePath, 
                                                DataType dataType,
                                                const QMap<QString, QVariant>& data)
{
    qDebug() << "DataStorage_FieldManager: Writing validated data to:" << filePath;
    
    // Check if data type is supported
    if (!isDataTypeSupported(dataType)) {
        qDebug() << "DataStorage_FieldManager: Unsupported data type for writing";
        return false;
    }
    
    // Security check: validate data size before processing
    if (!isDataSizeWithinLimits(data)) {
        qDebug() << "DataStorage_FieldManager: Data size exceeds security limits";
        return false;
    }
    
    // Validate file path
    InputValidation::ValidationResult pathValidation = 
        InputValidation::validateInput(filePath, InputValidation::InputType::FilePath);
    if (!pathValidation.isValid) {
        qDebug() << "DataStorage_FieldManager: Invalid file path:" << pathValidation.errorMessage;
        return false;
    }
    
    // Ensure parent directory exists
    QFileInfo fileInfo(filePath);
    QDir parentDir = fileInfo.dir();
    if (!parentDir.exists()) {
        if (!OperationsFiles::ensureDirectoryExists(parentDir.absolutePath())) {
            qDebug() << "DataStorage_FieldManager: Failed to create parent directory";
            return false;
        }
    }
    
    // Validate all data against field definitions
    QList<FieldDefinition> fieldDefs = getFieldDefinitions(dataType);
    for (auto it = data.begin(); it != data.end(); ++it) {
        bool fieldFound = false;
        for (const FieldDefinition& def : fieldDefs) {
            if (def.name == it.key()) {
                if (!validateFieldValue(it.key(), it.value(), def)) {
                    qDebug() << "DataStorage_FieldManager: Invalid field value for" << it.key();
                    return false;
                }
                fieldFound = true;
                break;
            }
        }
        
        if (!fieldFound) {
            qDebug() << "DataStorage_FieldManager: Unknown field in data:" << it.key();
            return false;
        }
    }
    
    // Serialize the data
    QString serializedData = serializeData(data);
    
    // Write the encrypted data file
    if (!OperationsFiles::writeEncryptedFile(filePath, m_encryptionKey, serializedData)) {
        qDebug() << "DataStorage_FieldManager: Failed to write encrypted data file";
        return false;
    }
    
    qDebug() << "DataStorage_FieldManager: Successfully wrote validated data";
    return true;
}

QList<DataStorage_FieldManager::FieldDefinition> DataStorage_FieldManager::getFieldDefinitions(DataType dataType) const
{
    return m_fieldDefinitions.getFieldDefinitions(dataType);
}

bool DataStorage_FieldManager::isDataTypeSupported(DataType dataType) const
{
    return m_fieldDefinitions.isDataTypeSupported(dataType);
}

bool DataStorage_FieldManager::parseDataString(const QString& dataString, 
                                            QMap<QString, QVariant>& parsedData,
                                            int maxFields)
{
    qDebug() << "DataStorage_FieldManager: Parsing data string";
    
    parsedData.clear();
    
    // Security check: limit total lines to process
    QStringList lines = dataString.split('\n', Qt::SkipEmptyParts);
    if (lines.size() > DataStorageLimits::MAX_LINES_PER_FILE) {
        qDebug() << "DataStorage_FieldManager: Too many lines in file (" << lines.size() 
                 << "), maximum allowed:" << DataStorageLimits::MAX_LINES_PER_FILE;
        return false;
    }
    
    int fieldCount = 0;
    for (const QString& line : lines) {
        // Security check: limit line length
        if (line.length() > DataStorageLimits::MAX_LINE_LENGTH) {
            qDebug() << "DataStorage_FieldManager: Line exceeds maximum length";
            continue;
        }
        
        QString trimmedLine = line.trimmed();
        
        // Skip empty lines and comments
        if (trimmedLine.isEmpty() || trimmedLine.startsWith('#')) {
            continue;
        }
        
        // Look for key=value format
        int equalsPos = trimmedLine.indexOf('=');
        if (equalsPos == -1) {
            qDebug() << "DataStorage_FieldManager: Invalid line format (no equals):" << trimmedLine;
            continue;
        }
        
        QString key = trimmedLine.left(equalsPos).trimmed();
        QString value = trimmedLine.mid(equalsPos + 1).trimmed();
        
        // Validate key
        InputValidation::ValidationResult keyValidation = 
            InputValidation::validateInput(key, InputValidation::InputType::PlainText, 100);
        if (!keyValidation.isValid) {
            qDebug() << "DataStorage_FieldManager: Invalid key:" << keyValidation.errorMessage;
            continue;
        }
        
        // Security check: limit field name length
        if (key.length() > DataStorageLimits::MAX_FIELD_NAME_LENGTH) {
            qDebug() << "DataStorage_FieldManager: Field name too long, skipping:" << key.left(50) << "...";
            continue;
        }
        
        // Security check: limit number of fields
        if (fieldCount >= maxFields) {
            qDebug() << "DataStorage_FieldManager: Maximum field limit reached (" << maxFields << "), stopping parse";
            break;
        }
        
        // Store as string for now - will be converted to proper type during validation
        parsedData[key] = value;
        fieldCount++;
    }
    
    qDebug() << "DataStorage_FieldManager: Parsed" << parsedData.size() << "data fields";
    return true;
}

QString DataStorage_FieldManager::serializeData(const QMap<QString, QVariant>& data,
                                               int sizeLimit) const
{
    qDebug() << "DataStorage_FieldManager: Serializing" << data.size() << "data fields";
    
    // Security check: validate data before serialization
    if (data.size() > DataStorageLimits::MAX_FIELDS_PER_FILE) {
        qDebug() << "DataStorage_FieldManager: Too many fields to serialize (" << data.size() << ")";
        return QString();
    }
    
    // Use QTextStream for memory-efficient string building
    QString dataString;
    dataString.reserve(data.size() * 100); // Pre-allocate estimated size
    QTextStream stream(&dataString);
    
    for (auto it = data.begin(); it != data.end(); ++it) {
        // Security check: validate key length
        if (it.key().length() > DataStorageLimits::MAX_FIELD_NAME_LENGTH) {
            qDebug() << "DataStorage_FieldManager: Skipping field with name too long:" << it.key().left(50) << "...";
            continue;
        }
        
        QString valueStr = convertFromType(it.value());
        
        // Security check: validate value length for strings
        if (valueStr.length() > DataStorageLimits::MAX_STRING_VALUE_LENGTH) {
            qDebug() << "DataStorage_FieldManager: Field value too long, truncating:" << it.key();
            valueStr = valueStr.left(DataStorageLimits::MAX_STRING_VALUE_LENGTH);
        }
        
        // Write to stream
        stream << it.key() << "=" << valueStr << "\n";
        
        // Security check: monitor total size
        if (dataString.size() > sizeLimit) {
            qDebug() << "DataStorage_FieldManager: Serialized data exceeds size limit";
            return QString();
        }
    }
    
    stream.flush();
    return dataString;
}

DataStorage_FieldManager::ValidationResult DataStorage_FieldManager::validateAndFixFields(
    const QMap<QString, QVariant>& currentData,
    DataType dataType,
    QMap<QString, QVariant>& validatedData)
{
    qDebug() << "DataStorage_FieldManager: Validating and fixing fields";
    
    ValidationResult result;
    validatedData.clear();
    
    // Security check: limit input data size
    if (currentData.size() > DataStorageLimits::MAX_FIELDS_PER_FILE * 2) {
        result.errorMessage = QString("Too many fields in current data (%1)").arg(currentData.size());
        qDebug() << "DataStorage_FieldManager:" << result.errorMessage;
        return result;
    }
    
    // Get field definitions for this data type
    QList<FieldDefinition> fieldDefs = getFieldDefinitions(dataType);
    if (fieldDefs.isEmpty()) {
        result.errorMessage = "No field definitions found for data type";
        return result;
    }
    
    // Step 1: Add all expected fields (with type conversion and defaults for missing ones)
    // Note: QMap doesn't support reserve, but it's self-balancing so performance is still good
    
    for (const FieldDefinition& def : fieldDefs) {
        if (currentData.contains(def.name)) {
            // Field exists - convert to proper type
            QString stringValue = currentData[def.name].toString();
            QVariant convertedValue = convertToType(stringValue, def.type, def.defaultValue);
            
            // Validate the converted value
            if (validateFieldValue(def.name, convertedValue, def)) {
                validatedData[def.name] = std::move(convertedValue);
            } else {
                // Use default if validation fails
                validatedData[def.name] = def.defaultValue;
                qDebug() << "DataStorage_FieldManager: Field value validation failed for" 
                         << def.name << ", using default";
            }
        } else if (def.required) {
            // Missing required field - add with default value
            validatedData[def.name] = def.defaultValue;
            result.addedFields.append(def.name);
            result.wasModified = true;
            qDebug() << "DataStorage_FieldManager: Added missing field:" << def.name;
        }
    }
    
    // Step 2: Remove any obsolete fields (fields in current but not in definition)
    for (auto it = currentData.begin(); it != currentData.end(); ++it) {
        bool fieldExpected = false;
        for (const FieldDefinition& def : fieldDefs) {
            if (def.name == it.key()) {
                fieldExpected = true;
                break;
            }
        }
        
        if (!fieldExpected) {
            result.removedFields.append(it.key());
            result.wasModified = true;
            qDebug() << "DataStorage_FieldManager: Removed obsolete field:" << it.key();
        }
    }
    
    result.success = true;
    qDebug() << "DataStorage_FieldManager: Field validation completed successfully";
    return result;
}

QVariant DataStorage_FieldManager::convertToType(const QString& value, FieldType expectedType, 
                                            const QVariant& defaultValue) const
{
    switch (expectedType) {
    case DataStorage_FieldDefinitions::String:
        return value;
        
    case DataStorage_FieldDefinitions::Boolean: {
        QString lowerValue = value.toLower();
        if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes" || lowerValue == "on") {
            return true;
        } else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no" || lowerValue == "off") {
            return false;
        } else {
            qDebug() << "DataStorage_FieldManager: Invalid boolean value:" << value << ", using default";
            return defaultValue;
        }
    }
    
    case DataStorage_FieldDefinitions::Integer: {
        bool ok;
        int intValue = value.toInt(&ok);
        if (ok) {
            return intValue;
        } else {
            qDebug() << "DataStorage_FieldManager: Invalid integer value:" << value << ", using default";
            return defaultValue;
        }
    }
    
    case DataStorage_FieldDefinitions::Double: {
        bool ok;
        double doubleValue = value.toDouble(&ok);
        if (ok) {
            return doubleValue;
        } else {
            qDebug() << "DataStorage_FieldManager: Invalid double value:" << value << ", using default";
            return defaultValue;
        }
    }
    
    default:
        qDebug() << "DataStorage_FieldManager: Unknown field type, returning as string";
        return value;
    }
}

QString DataStorage_FieldManager::convertFromType(const QVariant& value) const
{
    switch (value.type()) {
    case QVariant::Bool:
        return value.toBool() ? "true" : "false";
        
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
        return QString::number(value.toLongLong());
        
    case QVariant::Double:
        return QString::number(value.toDouble());
        
    case QVariant::String:
    default:
        return value.toString();
    }
}

bool DataStorage_FieldManager::validateFieldValue(const QString& fieldName, const QVariant& value, 
                                             const FieldDefinition& definition) const
{
    // Check type compatibility
    bool typeValid = false;
    
    switch (definition.type) {
    case DataStorage_FieldDefinitions::String:
        typeValid = value.canConvert<QString>();
        if (typeValid) {
            // Additional string validation
            QString strValue = value.toString();
            
            // Security check: limit string length
            if (strValue.length() > DataStorageLimits::MAX_STRING_VALUE_LENGTH) {
                qDebug() << "DataStorage_FieldManager: String value too long for field:" << fieldName;
                return false;
            }
            
            // Special validation for showName field
            if (fieldName == "showName") {
                // Allow empty show names (will be set later)
                if (!strValue.isEmpty()) {
                    InputValidation::ValidationResult validation = 
                        InputValidation::validateInput(strValue, InputValidation::InputType::TVShowName, 100);
                    typeValid = validation.isValid;
                }
            } else {
                // General string validation
                InputValidation::ValidationResult validation = 
                    InputValidation::validateInput(strValue, InputValidation::InputType::PlainText, 200);
                typeValid = validation.isValid;
            }
        }
        break;
        
    case DataStorage_FieldDefinitions::Boolean:
        typeValid = value.canConvert<bool>();
        break;
        
    case DataStorage_FieldDefinitions::Integer:
        typeValid = value.canConvert<int>();
        break;
        
    case DataStorage_FieldDefinitions::Double:
        typeValid = value.canConvert<double>();
        break;
    }
    
    if (!typeValid) {
        qDebug() << "DataStorage_FieldManager: Field value type validation failed for" 
                 << fieldName << "- expected type:" << definition.type;
    }
    
    return typeValid;
}

bool DataStorage_FieldManager::isDataSizeWithinLimits(const QMap<QString, QVariant>& data) const
{
    // Check number of fields
    if (data.size() > DataStorageLimits::MAX_FIELDS_PER_FILE) {
        qDebug() << "DataStorage_FieldManager: Too many fields:" << data.size();
        return false;
    }
    
    // Calculate and check total estimated size
    size_t estimatedSize = calculateDataSize(data);
    if (estimatedSize > static_cast<size_t>(DataStorageLimits::MAX_FILE_SIZE_BYTES)) {
        qDebug() << "DataStorage_FieldManager: Estimated data size too large:" << estimatedSize << "bytes";
        return false;
    }
    
    return true;
}

size_t DataStorage_FieldManager::calculateDataSize(const QMap<QString, QVariant>& data) const
{
    size_t totalSize = 0;
    
    for (auto it = data.begin(); it != data.end(); ++it) {
        // Add key size
        totalSize += it.key().toUtf8().size();
        
        // Add value size based on type
        switch (it.value().type()) {
        case QVariant::String:
            totalSize += it.value().toString().toUtf8().size();
            break;
        case QVariant::Bool:
            totalSize += 5; // "true" or "false"
            break;
        case QVariant::Int:
        case QVariant::UInt:
        case QVariant::LongLong:
        case QVariant::ULongLong:
            totalSize += 20; // Maximum digits for 64-bit integer
            break;
        case QVariant::Double:
            totalSize += 30; // Maximum digits for double precision
            break;
        default:
            totalSize += it.value().toString().toUtf8().size();
            break;
        }
        
        // Add overhead for "=" and newline
        totalSize += 2;
    }
    
    return totalSize;
}
