#ifndef DATASTORAGE_FIELD_MANAGER_H
#define DATASTORAGE_FIELD_MANAGER_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QMap>
#include <QByteArray>
#include "datastorage_field_definitions.h"

/**
 * @brief Generic data storage field manager for handling versioned data files
 * 
 * This class provides a unified way to manage structured data files across different features
 * with automatic field validation, missing field addition, and obsolete field removal.
 * It uses a field registry approach where each feature defines its expected fields
 * with types and default values.
 * 
 * Can be used for settings files, task lists, or any other structured data storage needs.
 */
class DataStorage_FieldManager
{
public:
    // Type aliases for easier access to definitions
    using FieldType = DataStorage_FieldDefinitions::FieldType;
    using FieldDefinition = DataStorage_FieldDefinitions::FieldDefinition;
    using DataType = DataStorage_FieldDefinitions::DataType;

    /**
     * @brief Result of data validation/repair operation
     */
    struct ValidationResult {
        bool success;
        bool wasModified;           // True if file was changed during validation
        QStringList addedFields;    // Fields that were added
        QStringList removedFields;  // Fields that were removed
        QString errorMessage;
        
        ValidationResult() : success(false), wasModified(false) {}
    };

    /**
     * @brief Constructor
     * @param encryptionKey Encryption key for file operations
     * @param username Username for encrypted file operations
     */
    DataStorage_FieldManager(const QByteArray& encryptionKey, const QString& username);
    
    /**
     * @brief Destructor
     */
    ~DataStorage_FieldManager();

    /**
     * @brief Read and validate data file, automatically fixing field issues
     * @param filePath Path to the data file
     * @param dataType Type of data this file contains
     * @param data Output map of field name -> value
     * @return ValidationResult containing operation details
     */
    ValidationResult readAndValidateData(const QString& filePath, 
                                        DataType dataType,
                                        QMap<QString, QVariant>& data);

    /**
     * @brief Write data file with all required fields
     * @param filePath Path to the data file
     * @param dataType Type of data this file contains
     * @param data Map of field name -> value to write
     * @return True if successful, false otherwise
     */
    bool writeValidatedData(const QString& filePath, 
                           DataType dataType,
                           const QMap<QString, QVariant>& data);

    /**
     * @brief Get the expected field definitions for a data type
     * @param dataType The data type to get definitions for
     * @return List of field definitions, empty if data type not supported
     */
    QList<FieldDefinition> getFieldDefinitions(DataType dataType) const;

    /**
     * @brief Check if a data type is supported
     * @param dataType The data type to check
     * @return True if supported, false otherwise
     */
    bool isDataTypeSupported(DataType dataType) const;

private:
    QByteArray m_encryptionKey;
    QString m_username;
    
    // Field definitions instance for accessing field configurations
    DataStorage_FieldDefinitions m_fieldDefinitions;
    
    /**
     * @brief Parse data from string format (key=value\n)
     * @param dataString Raw data string
     * @param parsedData Output map of parsed data
     * @return True if parsing was successful
     */
    bool parseDataString(const QString& dataString, 
                        QMap<QString, QVariant>& parsedData);
    
    /**
     * @brief Serialize data to string format (key=value\n)
     * @param data Map of data to serialize
     * @return Serialized data string
     */
    QString serializeData(const QMap<QString, QVariant>& data) const;
    
    /**
     * @brief Validate and fix field structure according to data type definition
     * @param currentData Current data from file
     * @param dataType Data type to validate against
     * @param validatedData Output validated and fixed data
     * @return ValidationResult with details about what was changed
     */
    ValidationResult validateAndFixFields(const QMap<QString, QVariant>& currentData,
                                        DataType dataType,
                                        QMap<QString, QVariant>& validatedData);
    
    /**
     * @brief Convert string value to appropriate QVariant type
     * @param value String value to convert
     * @param expectedType Expected field type
     * @param defaultValue Default value to use if conversion fails
     * @return Converted QVariant value
     */
    QVariant convertToType(const QString& value, FieldType expectedType, 
                          const QVariant& defaultValue) const;
    
    /**
     * @brief Convert QVariant value to string for serialization
     * @param value QVariant value to convert
     * @return String representation
     */
    QString convertFromType(const QVariant& value) const;
    
    /**
     * @brief Validate individual field value
     * @param fieldName Name of the field
     * @param value Value to validate
     * @param definition Field definition to validate against
     * @return True if valid, false otherwise
     */
    bool validateFieldValue(const QString& fieldName, const QVariant& value, 
                           const FieldDefinition& definition) const;
};

#endif // DATASTORAGE_FIELD_MANAGER_H
