#ifndef VP_SHOWS_EDIT_MULTIPLE_METADATA_DIALOG_H
#define VP_SHOWS_EDIT_MULTIPLE_METADATA_DIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include "vp_shows_metadata.h"

namespace Ui {
class VP_ShowsEditMultipleMetadataDialog;
}

class VP_ShowsEditMultipleMetadataDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsEditMultipleMetadataDialog(const QStringList& videoFilePaths,
                                                const QByteArray& encryptionKey,
                                                const QString& username,
                                                QWidget *parent = nullptr);
    ~VP_ShowsEditMultipleMetadataDialog();

    // Structure to hold the changes to be applied
    struct MetadataChanges {
        // Fields that can be edited
        bool changeLanguage = false;
        QString language;
        
        bool changeTranslation = false;
        QString translation;
        
        bool changeContentType = false;
        VP_ShowsMetadata::ContentType contentType;
        
        bool changeSeason = false;
        QString season;
        
        // Fields that can be cleared
        bool clearEpisodeNames = false;
        bool clearEpisodeNumbers = false;
        bool clearEpisodeImages = false;
        bool clearEpisodeDescriptions = false;
        bool clearEpisodeAirDates = false;
        bool resetDisplayStatus = false;  // Reset isDualDisplay to false
    };

    // Get the changes to be applied after dialog is accepted
    MetadataChanges getMetadataChanges() const { return m_changes; }
    
    // Get count of files that will be modified
    int getModifiedFileCount() const { return m_modifiedFileCount; }

public slots:
    // Override accept to validate and prepare changes
    void accept() override;

private slots:
    // Handle checkbox state changes for edit fields
    void onLanguageCheckChanged(int state);
    void onTranslationCheckChanged(int state);
    void onContentTypeCheckChanged(int state);
    void onSeasonCheckChanged(int state);
    
    // Validate input fields
    bool validateInput();
    
    // Update the preview of changes
    void updatePreview();

private:
    // Analyze selected files to determine what can be edited
    void analyzeSelectedFiles();
    
    // Load metadata from all selected files
    bool loadAllMetadata();
    
    // Check if all files have the same value for a field
    bool checkFieldConsistency(const QString& fieldName);
    
    // Populate UI with common values where applicable
    void populateUI();
    
    // Update changes structure from UI
    void updateChangesFromUI();
    
    // Apply changes to all selected files
    bool applyChangesToFiles();
    
private:
    Ui::VP_ShowsEditMultipleMetadataDialog *ui;
    
    QStringList m_videoFilePaths;
    QByteArray m_encryptionKey;
    QString m_username;
    
    // Store metadata for all selected files
    QList<VP_ShowsMetadata::ShowMetadata> m_allMetadata;
    
    // Track what fields can be edited
    bool m_canEditSeason;  // True if all files are from same season
    QString m_commonSeason;  // The common season if applicable
    
    // Common values (if all files have the same value)
    QString m_commonLanguage;
    QString m_commonTranslation;
    VP_ShowsMetadata::ContentType m_commonContentType;
    bool m_hasCommonLanguage;
    bool m_hasCommonTranslation;
    bool m_hasCommonContentType;
    
    // Changes to be applied
    MetadataChanges m_changes;
    
    // Count of files that will be modified
    int m_modifiedFileCount;
};

#endif // VP_SHOWS_EDIT_MULTIPLE_METADATA_DIALOG_H
