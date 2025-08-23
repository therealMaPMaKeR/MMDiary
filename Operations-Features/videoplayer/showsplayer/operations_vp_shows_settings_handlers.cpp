// This file contains the settings handling functions for Operations_VP_Shows
// These functions handle loading and saving show-specific settings

#include "operations_vp_shows.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "vp_shows_settings.h"
#include <QCheckBox>
#include <QDebug>

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
    
    qDebug() << "Operations_VP_Shows: Finished loading show settings - Autoplay:" << settings.autoplay
             << "SkipIntroOutro:" << settings.skipIntroOutro << "UseTMDB:" << settings.useTMDB;
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
