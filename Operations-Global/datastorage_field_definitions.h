#ifndef DATASTORAGE_FIELD_DEFINITIONS_H
#define DATASTORAGE_FIELD_DEFINITIONS_H

#include <QString>
#include <QVariant>
#include <QMap>
#include <QList>

/**
 * @brief Centralized definitions for data storage field management
 * 
 * This class contains all field definitions and data type configurations
 * used by the DataStorage_FieldManager. When you need to modify fields
 * for any file that uses the field management system, this is the only
 * file that should need modification.
 * 
 * Adding new data types or modifying existing field definitions should
 * be done here to keep all field management logic centralized.
 */
class DataStorage_FieldDefinitions
{
public:
    /**
     * @brief Supported field data types
     */
    enum FieldType {
        String,
        Boolean,
        Integer,
        Double
    };

    /**
     * @brief Data type identifiers for different features
     * 
     * Add new data types here when extending the system to new features.
     * Each data type should have corresponding field definitions registered
     * in the initializeAllFieldDefinitions() method.
     */
    enum DataType {
        TVShowSettings,
        TaskLists  // Future use for when you redo tasklists
        // Add more data types as needed
    };

    /**
     * @brief Field definition structure
     */
    struct FieldDefinition {
        QString name;
        FieldType type;
        QVariant defaultValue;
        bool required;  // If true, field must exist (will be added if missing)
        
        FieldDefinition() : type(String), required(true) {}
        
        FieldDefinition(const QString& fieldName, FieldType fieldType, 
                       const QVariant& defaultVal, bool isRequired = true)
            : name(fieldName), type(fieldType), defaultValue(defaultVal), required(isRequired) {}
    };

    /**
     * @brief Constructor - initializes all field definitions
     */
    DataStorage_FieldDefinitions();
    
    /**
     * @brief Destructor
     */
    ~DataStorage_FieldDefinitions();

    /**
     * @brief Get the field definitions for a specific data type
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

    /**
     * @brief Get all registered data types
     * @return List of all registered data types
     */
    QList<DataType> getSupportedDataTypes() const;

private:
    // Field registry - maps data types to their field definitions
    QMap<DataType, QList<FieldDefinition>> m_fieldRegistry;
    
    /**
     * @brief Initialize all field definitions for all supported data types
     * 
     * This method calls all the individual registration methods.
     * When adding new data types, add the corresponding registration
     * method call here.
     */
    void initializeAllFieldDefinitions();
    
    /**
     * @brief Register TV show settings fields
     * 
     * Defines all fields used in TV show settings files.
     * Modify this method to change TV show settings field structure.
     */
    void registerTVShowSettingsFields();
    
    /**
     * @brief Register task list fields (for future use)
     * 
     * Placeholder for when task lists are redone using this system.
     * Currently not implemented.
     */
    void registerTaskListFields();
    
    // Add more field registration methods here as needed:
    // void registerMovieSettingsFields();
    // void registerUserPreferencesFields();
    // etc.
};

#endif // DATASTORAGE_FIELD_DEFINITIONS_H
