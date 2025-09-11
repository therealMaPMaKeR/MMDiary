#ifndef VP_SHOWS_SETTINGS_DIALOG_H
#define VP_SHOWS_SETTINGS_DIALOG_H

#include <QDialog>
#include <QTimer>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include "vp_shows_tmdb.h"
#include "vp_shows_settings.h"

namespace Ui {
class VP_ShowsSettingsDialog;
}

class VP_ShowsSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent = nullptr);
    ~VP_ShowsSettingsDialog();

public slots:
    // Override accept to handle OK button
    void accept() override;
    
public:
    // Check if TMDB data was updated (for tree refresh)
    bool wasTMDBDataUpdated() const { return m_tmdbDataWasUpdated; }
    // Check if display file names setting was changed (for tree refresh)
    bool wasDisplayFileNamesChanged() const { return m_displayFileNamesChanged; }
    // Check if watch history was reset (for tree refresh)
    bool wasWatchHistoryReset() const { return m_watchHistoryWasReset; }

private slots:
    // Autofill/Search functionality slots
    void onShowNameTextChanged(const QString& text);
    void onSearchTimerTimeout();
    void onSuggestionItemHovered();
    void onSuggestionItemClicked(QListWidgetItem* item);
    void onImageDownloadFinished(QNetworkReply* reply);
    
    // Checkbox handler
    void onUseTMDBCheckboxToggled(bool checked);
    
    // Other existing slots
    void onLineEditFocusOut();
    void hideSuggestions(bool itemWasSelected = false);
    
    // Settings related slots
    void loadShowSettings();
    void saveShowSettings();
    void updateAllVideosMetadata();
    void updateShowDescription();
    void updateShowImage();
    
    // Button click handlers
    void onResetWatchHistoryClicked();
    void onUseCustomPosterClicked();
    void onUseCustomDescClicked();
    void onReacquireTMDBDataClicked();  // Handler for re-acquiring TMDB data

private:
    // UI setup and initialization
    void setupAutofillUI();
    void positionSuggestionsList();
    
    // Search and suggestion handling
    void performTMDBSearch(const QString& searchText);
    void displaySuggestions(const QList<VP_ShowsTMDB::ShowInfo>& shows);
    void clearSuggestions();
    
    // Image handling
    void downloadAndDisplayPoster(const QString& posterPath);
    void displayShowInfo(const VP_ShowsTMDB::ShowInfo& showInfo);
    
    // Event handling
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    // Load show metadata from encrypted files
    QString loadActualShowName();  // Get show name from video metadata
    void loadAndDisplayOriginalShowData();  // Load and display original poster/description
    
    // Settings management
    VP_ShowsSettings::ShowSettings m_currentSettings;
    
    // Store the selected show's TMDB ID
    int m_selectedShowId = 0;
    
private:
    Ui::VP_ShowsSettingsDialog *ui;
    QString m_showName;  // Actual decrypted show name (e.g., "Daria")
    QString m_showPath;  // Path to show folder
    QString m_originalShowName;  // Store the original show name
    QString m_originalDescription;  // Store the original show description
    QPixmap m_originalPoster;  // Store the original show poster
    
    // Autofill/Search components
    QListWidget* m_suggestionsList;
    QTimer* m_searchTimer;
    std::unique_ptr<VP_ShowsTMDB> m_tmdbApi;
    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    
    // Current search state
    QString m_currentSearchText;
    QList<VP_ShowsTMDB::ShowInfo> m_currentSuggestions;
    
    // Image cache with scaled posters
    struct CachedPoster {
        QPixmap scaledPixmap;  // Pre-scaled to label size
        QString posterPath;     // Original poster path (for cache key)
        qint64 sizeInBytes;     // Approximate memory size
    };
    
    QMap<QString, CachedPoster> m_posterCache;  // Key is poster path
    QList<QString> m_cacheAccessOrder;          // Track access order for LRU
    qint64 m_currentCacheSize;                  // Current cache size in bytes
    QString m_currentDownloadingPoster;
    
    // Cache management
    static constexpr qint64 MAX_CACHE_SIZE = 50 * 1024 * 1024;  // 50 MB limit
    static constexpr int MAX_CACHE_ITEMS = 20;                  // Maximum number of cached posters
    
    void addToCache(const QString& posterPath, const QPixmap& scaledPixmap);
    void enforeCacheLimits();
    qint64 estimatePixmapSize(const QPixmap& pixmap);
    
    // State tracking
    bool m_isShowingSuggestions;
    int m_hoveredItemIndex;  // Track hovered item index separately from selection
    bool m_itemJustSelected;  // Flag to prevent restoration after selection
    bool m_tmdbDataWasUpdated = false;  // Flag to track if TMDB data was successfully updated
    
    // TMDB reacquisition support
    struct VideoFileInfo {
        QString filePath;
        QString relativePath;
        QString episodeName;
        int season;
        int episode;
        QString language;
        QString translation;
    };
    QList<VideoFileInfo> collectVideoFiles();  // Collect all video files from the show
    bool updateVideoMetadataWithTMDB(const VideoFileInfo& videoInfo, const VP_ShowsTMDB::EpisodeInfo& episodeInfo);
    
    // Constants
    static constexpr int SEARCH_DELAY_MS = 500;  // Debounce delay for search
    static constexpr int MAX_SUGGESTIONS = 8;    // Maximum number of suggestions to show
    // Note: Suggestion item heights are now calculated dynamically based on actual content
    bool m_displayFileNamesChanged;   // Track if display file names setting changed
    bool m_watchHistoryWasReset = false;  // Track if watch history was reset
};

#endif // VP_SHOWS_SETTINGS_DIALOG_H
