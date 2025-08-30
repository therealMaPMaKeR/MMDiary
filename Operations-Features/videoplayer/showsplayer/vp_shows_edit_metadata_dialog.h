#ifndef VP_SHOWS_EDIT_METADATA_DIALOG_H
#define VP_SHOWS_EDIT_METADATA_DIALOG_H

#include <QDialog>
#include <QByteArray>
#include <QString>
#include "vp_shows_metadata.h"

namespace Ui {
class VP_ShowsEditMetadataDialog;
}

class QComboBox;
class QLabel;

class VP_ShowsEditMetadataDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsEditMetadataDialog(const QString& videoFilePath, 
                                       const QByteArray& encryptionKey,
                                       const QString& username,
                                       bool repairMode = false,
                                       const QString& showName = QString(),
                                       QWidget *parent = nullptr);
    ~VP_ShowsEditMetadataDialog();

    // Get the modified metadata after dialog is accepted
    VP_ShowsMetadata::ShowMetadata getMetadata() const { return m_metadata; }
    
    // Check if metadata was actually modified
    bool wasModified() const { return m_wasModified; }
    
    // Check if TMDB re-acquisition was requested
    bool shouldReacquireTMDB() const { return m_shouldReacquireTMDB; }

public slots:
    // Override accept to validate and save metadata
    void accept() override;

private slots:
    // Handle content type changes
    void onContentTypeChanged(int index);
    
    // Handle dual display checkbox changes
    void onDualDisplayChanged(int state);
    
    // Handle episode image operations
    void onSelectImageClicked();
    void onRemoveImageClicked();
    
    // Handle field changes to track modifications
    void onFieldChanged();
    
    // Validate input fields
    bool validateInput();

private:
    // Load metadata from file
    bool loadMetadata();
    
    // Initialize empty metadata for repair mode
    void initializeEmptyMetadata();
    
    // Populate UI with metadata
    void populateUI();
    
    // Update metadata from UI
    void updateMetadataFromUI();
    
    // Update image preview
    void updateImagePreview();
    
    // Check if metadata has been modified
    void checkForModifications();
    
    // Format date for display
    QString formatDate(const QString& date) const;
    
private:
    Ui::VP_ShowsEditMetadataDialog *ui;
    
    QString m_videoFilePath;
    QByteArray m_encryptionKey;
    QString m_username;
    
    VP_ShowsMetadata::ShowMetadata m_metadata;
    VP_ShowsMetadata::ShowMetadata m_originalMetadata;
    
    bool m_wasModified;
    bool m_repairMode;  // Flag to indicate repair mode
    QString m_providedShowName;  // Show name provided for repair mode
    bool m_shouldReacquireTMDB;  // Flag to indicate if TMDB re-acquisition was requested
    
    // UI elements that need special handling
    QLabel* m_imagePreviewLabel;
    QComboBox* m_contentTypeCombo;
};

#endif // VP_SHOWS_EDIT_METADATA_DIALOG_H
