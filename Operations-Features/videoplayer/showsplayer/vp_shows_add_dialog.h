#ifndef VP_SHOWS_ADD_DIALOG_H
#define VP_SHOWS_ADD_DIALOG_H

#include <QDialog>
#include <QString>
#include <QTimer>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include "vp_shows_tmdb.h"

QT_BEGIN_NAMESPACE
namespace Ui { class VP_ShowsAddDialog; }
QT_END_NAMESPACE

class VP_ShowsAddDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsAddDialog(const QString& folderName, QWidget *parent = nullptr);
    ~VP_ShowsAddDialog();
    
    // Getters for the dialog values
    QString getShowName() const;
    QString getLanguage() const;
    QString getTranslationMode() const;
    
    // Getters for custom poster and description
    QPixmap getCustomPoster() const;
    QString getCustomDescription() const;
    bool hasCustomPoster() const;
    bool hasCustomDescription() const;
    bool isUsingTMDB() const;  // Check if TMDB checkbox is checked
    
    // Getters for playback settings
    bool isAutoplayEnabled() const;
    bool isSkipIntroEnabled() const;
    bool isSkipOutroEnabled() const;
    
    // Get the selected show's TMDB ID (0 if not selected from TMDB)
    int getSelectedShowId() const { return m_selectedShowId; }
    
    // Getter for parsing mode
    enum ParseMode {
        ParseFromFolder = 0,
        ParseFromFile = 1
    };
    ParseMode getParseMode() const;
    
    // Load settings from existing show
    void loadShowSettings(const QString& showPath, const QByteArray& encryptionKey, const QString& username);
    
    // Validation
    bool validateInputs();
    
    // Set show name as read-only (for adding episodes to existing show)
    void setShowNameReadOnly(bool readOnly);
    
    // Initialize for adding episodes to existing show with path
    void initializeForExistingShow(const QString& showPath, const QByteArray& encryptionKey, const QString& username);

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    
    // TMDB autofill/search functionality slots
    void onUseTMDBCheckboxToggled(bool checked);
    void onShowNameTextChanged(const QString& text);
    void onSearchTimerTimeout();
    void onExistingShowCheckTimeout();  // New slot for debounced existing show check
    void onSuggestionItemClicked(QListWidgetItem* item);
    void onImageDownloadFinished(QNetworkReply* reply);
    void hideSuggestions(bool itemWasSelected = false);
    
    // Custom poster and description slots
    void onUseCustomPosterClicked();
    void onUseCustomDescriptionClicked();
    
private:
    Ui::VP_ShowsAddDialog *ui;
    QString m_folderName;
    QString m_originalDescription;  // Store original description
    QPixmap m_originalPoster;  // Store original poster
    
    // Parent widget for accessing operations
    QWidget* m_parentWidget;
    
    // Helper function to validate and sanitize input
    bool validateShowName(const QString& showName);
    bool validateLanguage(const QString& language);
    
    // Helper functions for existing show mode
    void loadExistingShowData(const QString& showPath, const QByteArray& encryptionKey, const QString& username);
    bool m_isAddingToExistingShow;  // Track if we're adding to an existing show
    
    // Check for existing show when name changes
    void checkForExistingShow(const QString& showName);
    bool m_isCheckingExistingShow;  // Flag to prevent recursive checks
    QString m_lastCheckedShowName;  // Cache to avoid redundant checks
    bool m_hasTMDBData;  // Track if we have TMDB data loaded
    
    // Timer for debouncing existing show checks
    QTimer* m_existingShowCheckTimer;
    QString m_pendingShowNameCheck;  // Store the show name to check after debounce
    
    // Settings from existing show
    bool m_settingsLoaded;
    bool m_existingAutoplay;
    bool m_existingSkipIntro;
    bool m_existingSkipOutro;
    bool m_existingUseTMDB;
    
    // Store the selected show's TMDB ID
    int m_selectedShowId = 0;
    bool m_userSelectedFromDropdown = false;  // Track if user explicitly selected from dropdown
    
    // TMDB autofill functionality
    void setupAutofillUI();
    void positionSuggestionsList();
    void performTMDBSearch(const QString& searchText);
    void displaySuggestions(const QList<VP_ShowsTMDB::ShowInfo>& shows);
    void clearSuggestions();
    void downloadAndDisplayPoster(const QString& posterPath);
    void displayShowInfo(const VP_ShowsTMDB::ShowInfo& showInfo);
    
    // Event handling
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    // TMDB components
    QListWidget* m_suggestionsList;
    QTimer* m_searchTimer;
    std::unique_ptr<VP_ShowsTMDB> m_tmdbApi;
    std::unique_ptr<QNetworkAccessManager> m_networkManager;  // Kept for potential future use
    
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
    
    // Constants
    static constexpr int SEARCH_DELAY_MS = 500;  // Debounce delay for search
    static constexpr int MAX_SUGGESTIONS = 8;    // Maximum number of suggestions to show
    
    // Custom poster and description storage
    QPixmap m_customPoster;
    QString m_customDescription;
    bool m_hasCustomDescription = false;
};

#endif // VP_SHOWS_ADD_DIALOG_H
