// ============================================================================
// Search Functionality Implementation
// Add these implementations to operations_encrypteddata.cpp
// ============================================================================

#include "operations_encrypteddata.h"

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

// Check if a file matches the search criteria
// MODIFIED: Now also searches in tags along with filename
bool Operations_EncryptedData::matchesSearchCriteria(const QString& filename, const QString& searchText)
{
    // If search text is empty, all files match
    if (searchText.isEmpty()) {
        return true;
    }
    
    // Check if the filename contains the search text (case-insensitive)
    if (filename.contains(searchText, Qt::CaseInsensitive)) {
        qDebug() << "Operations_EncryptedData: File matches search (filename):" << filename;
        return true;
    }
    
    // NEW: Also check tags for the search text
    // We need to get the file metadata to check tags
    // Since this function is called from updateFileListDisplay where we iterate through files,
    // we need to modify the approach slightly
    
    // Note: This function signature needs to be updated to also receive metadata
    // For now, returning based on filename only
    return false;
}

// IMPROVED VERSION: This version takes metadata as a parameter to check tags
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
    
    // NEW: Check if any tag contains the search text (case-insensitive)
    for (const QString& tag : metadata.tags) {
        if (tag.contains(searchText, Qt::CaseInsensitive)) {
            qDebug() << "Operations_EncryptedData: File matches search (tag):" << metadata.filename 
                     << "matching tag:" << tag;
            return true;
        }
    }
    
    // No match found in filename or tags
    return false;
}
