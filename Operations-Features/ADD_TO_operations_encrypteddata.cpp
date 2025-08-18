// ============================================================================
// INSTRUCTIONS FOR IMPLEMENTING TAG SEARCH IN operations_encrypteddata.cpp
// ============================================================================

/*
This file contains the implementation to make the search bar also filter for tags.
To implement this feature, you need to:

1. The header file (operations_encrypteddata.h) has already been updated to include 
   the new function declaration.

2. The updateFileListDisplay() function has already been updated to use the new 
   matchesSearchCriteriaWithTags function.

3. Now you need to add the implementation of the search functions to 
   operations_encrypteddata.cpp. 

Add the following code to the END of operations_encrypteddata.cpp file 
(or in an appropriate section with other search-related implementations):
*/

// ============================================================================
// Search Functionality Implementation
// ============================================================================

// Search text changed slot - triggers debounced search
void Operations_EncryptedData::onSearchTextChanged()
{
    qDebug() << "Operations_EncryptedData: Search text changed";
    
    // Get the current search text
    m_currentSearchText = m_mainWindow->ui->lineEdit_DataENC_SearchBar->text().trimmed();
    
    qDebug() << "Operations_EncryptedData: New search text:" << m_currentSearchText;
    
    // Reset and start the debounce timer
    m_searchDebounceTimer->stop();
    m_searchDebounceTimer->start();
}

// Clear search functionality
void Operations_EncryptedData::clearSearch()
{
    qDebug() << "Operations_EncryptedData: Clearing search";
    
    // Clear the search bar text
    m_mainWindow->ui->lineEdit_DataENC_SearchBar->clear();
    
    // Clear the stored search text
    m_currentSearchText.clear();
    
    // Stop any pending search timer
    m_searchDebounceTimer->stop();
    
    // Update the display immediately
    updateFileListDisplay();
}

// Check if a file matches the search criteria (filename only - legacy function)
bool Operations_EncryptedData::matchesSearchCriteria(const QString& filename, const QString& searchText)
{
    // If search text is empty, all files match
    if (searchText.isEmpty()) {
        return true;
    }
    
    // Check if the filename contains the search text (case-insensitive)
    return filename.contains(searchText, Qt::CaseInsensitive);
}

// Check if a file matches the search criteria (includes both filename and tags)
bool Operations_EncryptedData::matchesSearchCriteriaWithTags(
    const EncryptedFileMetadata::FileMetadata& metadata, 
    const QString& searchText)
{
    // If search text is empty, all files match
    if (searchText.isEmpty()) {
        return true;
    }
    
    // Check if the filename contains the search text (case-insensitive)
    if (metadata.filename.contains(searchText, Qt::CaseInsensitive)) {
        qDebug() << "Operations_EncryptedData: File matches search (filename):" << metadata.filename;
        return true;
    }
    
    // Check if any tag contains the search text (case-insensitive)
    for (const QString& tag : metadata.tags) {
        if (tag.contains(searchText, Qt::CaseInsensitive)) {
            qDebug() << "Operations_EncryptedData: File matches search (tag):" << metadata.filename 
                     << "matching tag:" << tag;
            return true;
        }
    }
    
    // Optional: Also check if the category contains the search text
    // Uncomment the following block if you want to search in categories as well
    /*
    if (metadata.category.contains(searchText, Qt::CaseInsensitive)) {
        qDebug() << "Operations_EncryptedData: File matches search (category):" << metadata.filename 
                 << "matching category:" << metadata.category;
        return true;
    }
    */
    
    // No match found in filename or tags
    return false;
}

// ============================================================================
// END OF SEARCH FUNCTIONALITY IMPLEMENTATION
// ============================================================================
