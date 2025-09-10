#include "datastorage_field_definitions.h"
#include <QDebug>

DataStorage_FieldDefinitions::DataStorage_FieldDefinitions()
{
    qDebug() << "DataStorage_FieldDefinitions: Initializing field definitions";
    initializeAllFieldDefinitions();
    qDebug() << "DataStorage_FieldDefinitions: Initialized" << m_fieldRegistry.size() << "data types";
}

DataStorage_FieldDefinitions::~DataStorage_FieldDefinitions()
{
    qDebug() << "DataStorage_FieldDefinitions: Destructor called";
}

void DataStorage_FieldDefinitions::initializeAllFieldDefinitions()
{
    qDebug() << "DataStorage_FieldDefinitions: Initializing all field definitions";
    
    // Register all supported data types
    registerTVShowSettingsFields();
    // registerTaskListFields(); // Uncomment when implementing task lists
    
    // Add more registration method calls here as new data types are added:
    // registerMovieSettingsFields();
    // registerUserPreferencesFields();
    
    qDebug() << "DataStorage_FieldDefinitions: All field definitions initialized";
}

void DataStorage_FieldDefinitions::registerTVShowSettingsFields()
{
    qDebug() << "DataStorage_FieldDefinitions: Registering TV Show Settings fields";
    
    QList<FieldDefinition> tvShowFields;
    
    // Security: Limit total fields per data type to prevent memory issues
    tvShowFields.reserve(20); // Pre-allocate reasonable size
    
    // Define all TV show settings fields based on ShowSettings struct defaults
    // These definitions determine what fields exist in TV show settings files
    // and their default values when files are created or repaired.
    
    tvShowFields.append(FieldDefinition("showName", String, QString(""), true));
    tvShowFields.append(FieldDefinition("showId", String, QString("error"), true));  // TMDB show ID, "error" means not set
    tvShowFields.append(FieldDefinition("skipIntro", Boolean, false, true));
    tvShowFields.append(FieldDefinition("skipOutro", Boolean, false, true));
    tvShowFields.append(FieldDefinition("autoplay", Boolean, true, true));  // Default true to match ShowSettings
    tvShowFields.append(FieldDefinition("autoplayRandom", Boolean, false, true));  // Random episode autoplay
    tvShowFields.append(FieldDefinition("useTMDB", Boolean, true, true));  // Default to enabled
    tvShowFields.append(FieldDefinition("autoFullscreen", Boolean, true, true));  // Default true to match ShowSettings
    tvShowFields.append(FieldDefinition("displayFileNames", Boolean, false, true));  // Display file names instead of episode names
    
    m_fieldRegistry[TVShowSettings] = tvShowFields;
    
    qDebug() << "DataStorage_FieldDefinitions: Registered" << tvShowFields.size() 
             << "fields for TV Show Settings";
}

void DataStorage_FieldDefinitions::registerTaskListFields()
{
    qDebug() << "DataStorage_FieldDefinitions: Registering Task List fields (placeholder)";
    
    // Placeholder for future task list field definitions
    // When task lists are redone to use this system, implement the fields here
    
    QList<FieldDefinition> taskListFields;
    
    // Example fields (uncomment and modify when implementing):
    // taskListFields.append(FieldDefinition("taskName", String, QString(""), true));
    // taskListFields.append(FieldDefinition("completed", Boolean, false, true));
    // taskListFields.append(FieldDefinition("priority", Integer, 1, true));
    // taskListFields.append(FieldDefinition("dueDate", String, QString(""), false));
    
    // m_fieldRegistry[TaskLists] = taskListFields;
    
    qDebug() << "DataStorage_FieldDefinitions: Task List field registration complete (placeholder)";
}

QList<DataStorage_FieldDefinitions::FieldDefinition> DataStorage_FieldDefinitions::getFieldDefinitions(DataType dataType) const
{
    if (m_fieldRegistry.contains(dataType)) {
        return m_fieldRegistry[dataType];
    }
    
    qDebug() << "DataStorage_FieldDefinitions: No field definitions found for data type:" << static_cast<int>(dataType);
    return QList<FieldDefinition>();
}

bool DataStorage_FieldDefinitions::isDataTypeSupported(DataType dataType) const
{
    return m_fieldRegistry.contains(dataType);
}

QList<DataStorage_FieldDefinitions::DataType> DataStorage_FieldDefinitions::getSupportedDataTypes() const
{
    return m_fieldRegistry.keys();
}
