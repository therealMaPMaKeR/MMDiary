// This file contains the settings handling functions for Operations_VP_Shows
// These functions handle loading and saving show-specific settings

#include "operations_vp_shows.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "vp_shows_settings.h"
#include <QCheckBox>
#include <QDebug>
#include <QLineEdit>
#include <QTimer>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <vector>

// Windows-specific includes for file explorer
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

void Operations_VP_Shows::loadShowSettings(const QString& showFolderPath)
{
    qDebug() << "Operations_VP_Shows: Loading show settings from folder:" << showFolderPath;
    
    // Check if we have the required UI elements
    if (!m_mainWindow || !m_mainWindow->ui) {
        qDebug() << "Operations_VP_Shows: UI elements not available for loading settings";
        return;
    }
    
    // Create settings manager
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Load the settings for this show
    VP_ShowsSettings::ShowSettings settings;
    if (!settingsManager.loadShowSettings(showFolderPath, settings)) {
        qDebug() << "Operations_VP_Shows: Failed to load show settings, using defaults";
        // loadShowSettings returns true even when file doesn't exist (uses defaults)
        // So this block only executes on actual errors
    }
    
    // Store the loaded settings
    m_currentShowSettings = settings;
    
    qDebug() << "Operations_VP_Shows: Finished loading show settings - Autoplay:" << settings.autoplay
             << "AutoplayRandom:" << settings.autoplayRandom
             << "SkipIntro:" << settings.skipIntro << "SkipOutro:" << settings.skipOutro << "UseTMDB:" << settings.useTMDB
             << "DisplayFileNames:" << settings.displayFileNames;
    
    // Checkboxes have been moved to the settings dialog
    // The settings are now only used internally for autoplay and skip intro/outro functionality
    
    /* REMOVED - Checkboxes moved to settings dialog
    if (m_mainWindow->ui->checkBox_VP_Shows_Display_SkipContent) {
        bool oldBlockState = m_mainWindow->ui->checkBox_VP_Shows_Display_SkipContent->blockSignals(true);
        m_mainWindow->ui->checkBox_VP_Shows_Display_SkipContent->setChecked(settings.skipIntroOutro);
        m_mainWindow->ui->checkBox_VP_Shows_Display_SkipContent->blockSignals(oldBlockState);
        qDebug() << "Operations_VP_Shows: Set skip intro/outro checkbox to:" << settings.skipIntroOutro;
    }
    
    if (m_mainWindow->ui->checkBox_VP_Shows_Display_Autoplay) {
        bool oldBlockState = m_mainWindow->ui->checkBox_VP_Shows_Display_Autoplay->blockSignals(true);
        m_mainWindow->ui->checkBox_VP_Shows_Display_Autoplay->setChecked(settings.autoplay);
        m_mainWindow->ui->checkBox_VP_Shows_Display_Autoplay->blockSignals(oldBlockState);
        qDebug() << "Operations_VP_Shows: Set autoplay checkbox to:" << settings.autoplay;
    }
    */
}
// ============================================================================
// Search Functionality Implementation
// ============================================================================

void Operations_VP_Shows::onSearchTextChanged(const QString& text)
{
    qDebug() << "Operations_VP_Shows: Search text changed to:" << text;
    
    // Store the current search text
    m_currentSearchText = text.trimmed();
    
    // Reset and start the debounce timer
    m_searchDebounceTimer->stop();
    m_searchDebounceTimer->start();
    
    qDebug() << "Operations_VP_Shows: Debounce timer started";
}

void Operations_VP_Shows::onSearchTimerTimeout()
{
    qDebug() << "Operations_VP_Shows: Search timer timeout, performing search";
    qDebug() << "Operations_VP_Shows: Search text:" << m_currentSearchText;
    
    filterShowsList();
}

void Operations_VP_Shows::filterShowsList()
{
    qDebug() << "Operations_VP_Shows: Filtering shows list with search text:" << m_currentSearchText;
    
    if (!m_mainWindow || !m_mainWindow->ui || !m_mainWindow->ui->listWidget_VP_List_List) {
        qDebug() << "Operations_VP_Shows: List widget not available for filtering";
        return;
    }
    
    QListWidget* listWidget = m_mainWindow->ui->listWidget_VP_List_List;
    int totalItems = listWidget->count();
    int visibleItems = 0;
    int hiddenItems = 0;
    
    // If search text is empty, show all items
    if (m_currentSearchText.isEmpty()) {
        qDebug() << "Operations_VP_Shows: Search text is empty, showing all items";
        for (int i = 0; i < totalItems; ++i) {
            QListWidgetItem* item = listWidget->item(i);
            if (item) {
                item->setHidden(false);
                visibleItems++;
            }
        }
        qDebug() << "Operations_VP_Shows: All" << visibleItems << "items are now visible";
        return;
    }
    
    // Perform case-insensitive search
    for (int i = 0; i < totalItems; ++i) {
        QListWidgetItem* item = listWidget->item(i);
        if (!item) {
            continue;
        }
        
        QString showName = item->text();
        
        // Check if the show name contains the search text (case-insensitive)
        bool matches = showName.contains(m_currentSearchText, Qt::CaseInsensitive);
        
        // Show or hide the item based on the match
        item->setHidden(!matches);
        
        if (matches) {
            visibleItems++;
        } else {
            hiddenItems++;
        }
    }
    
    qDebug() << "Operations_VP_Shows: Filter complete - Visible:" << visibleItems 
             << "Hidden:" << hiddenItems << "Total:" << totalItems;
    
    // If no items match, you could optionally show a message
    if (visibleItems == 0 && totalItems > 0) {
        qDebug() << "Operations_VP_Shows: No shows match the search criteria";
    }
}

void Operations_VP_Shows::saveShowSettings()
{
    qDebug() << "Operations_VP_Shows: Saving show settings";
    
    // Check if we have a current show folder
    if (m_currentShowFolder.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No current show folder, cannot save settings";
        return;
    }
    
    // Create settings manager
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Save the current settings
    if (!settingsManager.saveShowSettings(m_currentShowFolder, m_currentShowSettings)) {
        qDebug() << "Operations_VP_Shows: Failed to save show settings";
        // Optionally show an error message to the user
        // QMessageBox::warning(m_mainWindow, tr("Settings Error"), 
        //                     tr("Failed to save show settings."));
    } else {
        qDebug() << "Operations_VP_Shows: Show settings saved successfully";
    }
}

// REMOVED - Checkbox handlers moved to settings dialog
// Settings are now updated only from the VP_ShowsSettingsDialog
/*
void Operations_VP_Shows::onSkipContentCheckboxChanged(int state)
{
    qDebug() << "Operations_VP_Shows: Skip intro/outro checkbox changed to state:" << state;
    
    // Update the current settings
    m_currentShowSettings.skipIntroOutro = (state == Qt::Checked);
    
    // Save the settings immediately
    saveShowSettings();
    
    qDebug() << "Operations_VP_Shows: Skip intro/outro setting updated to:" << m_currentShowSettings.skipIntroOutro;
}

void Operations_VP_Shows::onAutoplayCheckboxChanged(int state)
{
    qDebug() << "Operations_VP_Shows: Autoplay checkbox changed to state:" << state;
    
    // Update the current settings
    m_currentShowSettings.autoplay = (state == Qt::Checked);
    
    // Save the settings immediately
    saveShowSettings();
    
    qDebug() << "Operations_VP_Shows: Autoplay setting updated to:" << m_currentShowSettings.autoplay;
}
*/

// ============================================================================
// File Explorer Functions
// ============================================================================

void Operations_VP_Shows::showInFileExplorer()
{
    qDebug() << "Operations_VP_Shows: Show in File Explorer triggered for TV show";
    
    // Check if we have a valid show path
    if (m_contextMenuShowPath.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No show path available";
        return;
    }
    
    // Verify the folder still exists
    if (!QDir(m_contextMenuShowPath).exists()) {
        QMessageBox::critical(m_mainWindow, "Folder Not Found",
                            "The TV show folder no longer exists.");
        refreshTVShowsList(); // Refresh the list
        return;
    }
    
#ifdef Q_OS_WIN
    // Windows-specific implementation to open Explorer and select the folder
    QString nativePath = QDir::toNativeSeparators(m_contextMenuShowPath);
    bool explorerOpened = false;
    
    // Method 1: Try using Windows Shell API (most reliable)
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(result) || result == S_FALSE) {  // S_FALSE means already initialized
        // Convert QString to wide string for Windows API
        std::wstring wPath = nativePath.toStdWString();
        
        // Parse the file path to get an ITEMIDLIST
        LPITEMIDLIST pidl = ILCreateFromPathW(wPath.c_str());
        if (pidl) {
            // Open Explorer and select the folder
            HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            if (SUCCEEDED(hr)) {
                qDebug() << "Operations_VP_Shows: Successfully opened Explorer with folder selected:" << m_contextMenuShowPath;
                explorerOpened = true;
            } else {
                qWarning() << "Operations_VP_Shows: SHOpenFolderAndSelectItems failed with HRESULT:" << hr;
            }
            ILFree(pidl);
        } else {
            qWarning() << "Operations_VP_Shows: Failed to create ITEMIDLIST from path";
        }
        
        if (result != S_FALSE) {
            CoUninitialize();
        }
    }
    
    // Method 2: Fallback to explorer.exe command if Shell API fails
    if (!explorerOpened) {
        QString explorerCommand = "explorer.exe";
        QStringList args;
        args << "/select," + nativePath;
        
        if (QProcess::startDetached(explorerCommand, args)) {
            qDebug() << "Operations_VP_Shows: Opened Explorer with /select command for:" << m_contextMenuShowPath;
        } else {
            QMessageBox::warning(m_mainWindow, "Failed to Open Explorer",
                                "Could not open File Explorer to show the folder.\n\n" +
                                m_contextMenuShowPath);
        }
    }
#else
    // Non-Windows fallback: just open the containing folder
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_contextMenuShowPath))) {
        QMessageBox::warning(m_mainWindow, "Failed to Open Folder",
                            "Could not open the folder.\n\n" +
                            m_contextMenuShowPath);
    }
#endif
}

void Operations_VP_Shows::showEpisodesInFileExplorer()
{
    qDebug() << "Operations_VP_Shows: Show in File Explorer triggered for episodes";
    qDebug() << "Operations_VP_Shows: Number of episode paths:" << m_contextMenuEpisodePaths.size();
    
    if (m_contextMenuEpisodePaths.isEmpty()) {
        qDebug() << "Operations_VP_Shows: No episode paths available";
        return;
    }
    
    // Verify that files still exist and collect valid paths
    QStringList validPaths;
    for (const QString& episodePath : m_contextMenuEpisodePaths) {
        if (QFile::exists(episodePath)) {
            validPaths.append(episodePath);
        } else {
            qDebug() << "Operations_VP_Shows: Episode file no longer exists:" << episodePath;
        }
    }
    
    if (validPaths.isEmpty()) {
        QMessageBox::critical(m_mainWindow, "Files Not Found",
                            "The selected episode files no longer exist.");
        loadShowEpisodes(m_currentShowFolder); // Refresh the episode list
        return;
    }
    
#ifdef Q_OS_WIN
    // Windows-specific implementation to open Explorer and select files
    bool explorerOpened = false;
    
    // Method 1: Try using Windows Shell API (supports multiple file selection)
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(result) || result == S_FALSE) {  // S_FALSE means already initialized
        
        if (validPaths.size() == 1) {
            // Single file - use simple approach
            QString filePath = validPaths.first();
            QString nativePath = QDir::toNativeSeparators(filePath);
            std::wstring wPath = nativePath.toStdWString();
            
            LPITEMIDLIST pidl = ILCreateFromPathW(wPath.c_str());
            if (pidl) {
                HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                if (SUCCEEDED(hr)) {
                    qDebug() << "Operations_VP_Shows: Successfully opened Explorer with file selected:" << filePath;
                    explorerOpened = true;
                } else {
                    qWarning() << "Operations_VP_Shows: SHOpenFolderAndSelectItems failed with HRESULT:" << hr;
                }
                ILFree(pidl);
            }
        } else {
            // Multiple files - properly select all of them
            // First, verify all files are in the same folder
            QFileInfo firstFileInfo(validPaths.first());
            QString folderPath = firstFileInfo.absolutePath();
            bool allInSameFolder = true;
            
            for (const QString& path : validPaths) {
                QFileInfo fileInfo(path);
                if (fileInfo.absolutePath() != folderPath) {
                    allInSameFolder = false;
                    break;
                }
            }
            
            if (allInSameFolder) {
                // Convert folder path to PIDL
                QString nativeFolderPath = QDir::toNativeSeparators(folderPath);
                std::wstring wFolderPath = nativeFolderPath.toStdWString();
                LPITEMIDLIST pidlFolder = ILCreateFromPathW(wFolderPath.c_str());
                
                if (pidlFolder) {
                    // Create array of child PIDLs for the files
                    std::vector<LPITEMIDLIST> filePidls;
                    filePidls.reserve(validPaths.size());
                    
                    // Get PIDLs for all files
                    for (const QString& filePath : validPaths) {
                        QString nativePath = QDir::toNativeSeparators(filePath);
                        std::wstring wPath = nativePath.toStdWString();
                        LPITEMIDLIST pidlFile = ILCreateFromPathW(wPath.c_str());
                        if (pidlFile) {
                            filePidls.push_back(pidlFile);
                        }
                    }
                    
                    if (!filePidls.empty()) {
                        // Create array of relative PIDLs (child items)
                        std::vector<LPCITEMIDLIST> relativePidls;
                        relativePidls.reserve(filePidls.size());
                        
                        for (LPITEMIDLIST filePidl : filePidls) {
                            // Get the last part of the PIDL (the file name)
                            LPCITEMIDLIST relativePidl = ILFindLastID(filePidl);
                            if (relativePidl) {
                                relativePidls.push_back(relativePidl);
                            }
                        }
                        
                        // Open Explorer and select all the files
                        HRESULT hr = SHOpenFolderAndSelectItems(
                            pidlFolder, 
                            static_cast<UINT>(relativePidls.size()),
                            relativePidls.data(),
                            0
                        );
                        
                        if (SUCCEEDED(hr)) {
                            qDebug() << "Operations_VP_Shows: Successfully opened Explorer with" 
                                     << filePidls.size() << "files selected";
                            explorerOpened = true;
                        } else {
                            qWarning() << "Operations_VP_Shows: SHOpenFolderAndSelectItems failed for multiple files with HRESULT:" << hr;
                        }
                        
                        // Clean up file PIDLs
                        for (LPITEMIDLIST pidl : filePidls) {
                            ILFree(pidl);
                        }
                    }
                    
                    ILFree(pidlFolder);
                }
            } else {
                // Files are in different folders - just open the first file's folder
                qDebug() << "Operations_VP_Shows: Selected files are in different folders, opening first file's folder";
                QString nativePath = QDir::toNativeSeparators(folderPath);
                QString explorerCommand = "explorer.exe";
                QStringList args;
                args << nativePath;
                
                if (QProcess::startDetached(explorerCommand, args)) {
                    qDebug() << "Operations_VP_Shows: Opened Explorer showing folder:" << folderPath;
                    explorerOpened = true;
                }
            }
        }
        
        if (result != S_FALSE) {
            CoUninitialize();
        }
    }
    
    // Method 2: Fallback to explorer.exe command if Shell API fails
    if (!explorerOpened) {
        if (validPaths.size() == 1) {
            // Single file - use /select parameter
            QString filePath = validPaths.first();
            QString nativePath = QDir::toNativeSeparators(filePath);
            QString explorerCommand = "explorer.exe";
            QStringList args;
            args << "/select," + nativePath;
            
            if (QProcess::startDetached(explorerCommand, args)) {
                qDebug() << "Operations_VP_Shows: Opened Explorer with /select command for:" << filePath;
            } else {
                QMessageBox::warning(m_mainWindow, "Failed to Open Explorer",
                                    "Could not open File Explorer to show the file.\n\n" +
                                    filePath);
            }
        } else {
            // Multiple files - open the folder
            QFileInfo fileInfo(validPaths.first());
            QString folderPath = fileInfo.absolutePath();
            QString nativePath = QDir::toNativeSeparators(folderPath);
            QString explorerCommand = "explorer.exe";
            QStringList args;
            args << nativePath;
            
            if (QProcess::startDetached(explorerCommand, args)) {
                qDebug() << "Operations_VP_Shows: Opened Explorer showing folder for multiple files:" << folderPath;
            } else {
                QMessageBox::warning(m_mainWindow, "Failed to Open Explorer",
                                    "Could not open File Explorer to show the folder.\n\n" +
                                    folderPath);
            }
        }
    }
#else
    // Non-Windows fallback: open the containing folder
    QFileInfo fileInfo(validPaths.first());
    QString folderPath = fileInfo.absolutePath();
    
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath))) {
        QMessageBox::warning(m_mainWindow, "Failed to Open Folder",
                            "Could not open the folder containing the episodes.\n\n" +
                            folderPath);
    }
#endif
}
