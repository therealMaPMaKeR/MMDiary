#ifndef OPERATIONS_VP_SHOWS_H
#define OPERATIONS_VP_SHOWS_H

#include <QObject>
#include <QPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <memory>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QPixmap>
#include <QListWidgetItem>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QStorageInfo>
#include <QTimer>
#include <QKeyEvent>
#include "vp_shows_settings.h"
#include "vp_shows_metadata.h"
#include "vp_shows_episode_detector.h"
#include "../../../Operations-Global/ThreadSafeContainers.h"
#include "vp_shows_newepisode_checker.h"

// Forward declarations
class MainWindow;
class VP_Shows_Videoplayer;
class VP_ShowsEncryptionProgressDialog;
class VP_ShowsWatchHistory;
class VP_ShowsPlaybackTracker;
class VP_ShowsFavourites;
class VP_ShowsEditMultipleMetadataDialog;

class Operations_VP_Shows : public QObject
{
    Q_OBJECT

private:
    QPointer<MainWindow> m_mainWindow;  // Use QPointer for automatic null-setting on deletion
    bool m_blockSelectionChange = false;  // Track selection changes to enforce broken file restrictions
    std::unique_ptr<VP_Shows_Videoplayer> m_testVideoPlayer;  // For testing purposes
    std::unique_ptr<VP_Shows_Videoplayer> m_episodePlayer;    // For episode playback
    QPointer<VP_ShowsEncryptionProgressDialog> m_encryptionDialog;  // Use QPointer for safety
    std::unique_ptr<VP_ShowsWatchHistory> m_watchHistory;     // Direct watch history access (for non-playback queries)
    std::unique_ptr<VP_ShowsPlaybackTracker> m_playbackTracker; // Playback tracking and integration
    std::unique_ptr<VP_ShowsFavourites> m_showFavourites;     // Favourites management for current show
    std::unique_ptr<VP_ShowsEpisodeDetector> m_episodeDetector; // Episode detector for new episodes
    
    // Store mapping between show names and their folder paths - Thread-safe
    ThreadSafeMap<QString, QString> m_showFolderMapping;
    
    // Store mapping between episode display names and their file paths - Thread-safe
    // Key format: "ShowName_Season_Episode" -> filepath
    ThreadSafeMap<QString, QString> m_episodeFileMapping;
    
    // Current displayed show folder path
    QString m_currentShowFolder;
    
    // Current decrypted temp file path (for cleanup)
    QString m_currentTempFile;
    
    // Stores the actual decrypted file path with proper extension from decryptVideoWithMetadata
    QString m_lastDecryptedFilePath;
    
    // Flags for tracking import state
    bool m_isUpdatingExistingShow = false;
    int m_originalEpisodeCount = 0;
    int m_newEpisodeCount = 0;
    
    // View mode tracking
    bool m_isIconViewMode = false;
    ThreadSafeMap<QString, QPixmap> m_posterCache;  // Thread-safe cache for loaded posters
    
    // Search functionality
    QString m_currentSearchText;
    QTimer* m_searchDebounceTimer;
    void filterShowsList();
    
    // Helper functions
    QString selectVideoFile();
    bool isValidVideoFile(const QString& filePath);
    QStringList findVideoFiles(const QString& folderPath, bool recursive = true);
    
    // View mode helper functions
    QPixmap loadShowPoster(const QString& showFolderPath, const QSize& targetSize);
    void setupListViewMode();
    void setupIconViewMode();
    void refreshShowListItem(QListWidgetItem* item, const QString& showName, const QString& folderPath);
    
public:
    // Scroll speed configuration
    void setIconViewScrollMultiplier(double multiplier);
    double getIconViewScrollMultiplier() const;
    
private:
    QString generateRandomFileName(const QString& extension);
    bool createShowFolderStructure(QString& outputPath);
    
    // Duplicate handling functions
    bool checkForExistingShow(const QString& showName, const QString& language, 
                             const QString& translation, QString& existingFolder,
                             QStringList& existingEpisodes);
    QStringList filterNewEpisodes(const QStringList& candidateFiles, 
                                 const QStringList& existingEpisodes,
                                 const QString& showName,
                                 const QString& language,
                                 const QString& translation);

    enum class WatchState {
        NotWatched = 0,
        Watched = 1,
        PartiallyWatched = 2
    };

    // New episode checker for background checking
    std::unique_ptr<VP_ShowsNewEpisodeCheckerManager> m_episodeCheckerManager;

    // Update show poster in list with new episode badge
    void updateShowPosterInList(const QString& folderPath, int newEpisodeCount);

    // Start background checking for new episodes
    void startBackgroundEpisodeCheck();

    void refreshShowPosterWithNotification();

    // Helper function for drawing new episode badge
    void drawNewEpisodeBadge(QPainter& painter, const QSize& posterSize, int newEpisodeCount, bool compactMode = false);

    WatchState getItemWatchState(QTreeWidgetItem* item);
    void countWatchedEpisodes(QTreeWidgetItem* item, int& watchedCount, int& totalCount);
    void setWatchedStateForItem(QTreeWidgetItem* item, bool watched);
    void refreshEpisodeTreeColors();
    void refreshItemColors(QTreeWidgetItem* item, const QColor& watchedColor);
    void expandToLastWatchedEpisode();
    QTreeWidgetItem* determineEpisodeToPlay();




public:
    explicit Operations_VP_Shows(MainWindow* mainWindow);
    ~Operations_VP_Shows();
    
    // Friend class declaration
    friend class MainWindow;
    
    // Import TV show functionality
    void importTVShow();
    
    // Load and display TV shows in the list
    void loadTVShowsList();
    
    // Refresh the TV shows list (public version that can be called from outside)
    void refreshTVShowsList();
    
    // Open show-specific settings dialog
    void openShowSettings();
    
    // Save show description to encrypted file
    bool saveShowDescription(const QString& showFolderPath, const QString& description);
    
    // Load show description from encrypted file
    QString loadShowDescription(const QString& showFolderPath);
    
    // Save show image to encrypted file
    bool saveShowImage(const QString& showFolderPath, const QByteArray& imageData);
    
    // Load show image from encrypted file
    QPixmap loadShowImage(const QString& showFolderPath);
    
    // Add "NEW" indicator overlay to show poster
    QPixmap addNewEpisodeIndicator(const QPixmap& originalPoster, int newEpisodeCount, bool compactMode = true);
    
    // Display show details page
    void displayShowDetails(const QString& showName, const QString& folderPath = QString());
    
    // Update Play/Resume button text based on watch history
    void updatePlayButtonText();
    
    // Handle show list double-click
    void onShowListItemDoubleClicked(QListWidgetItem* item);
    
    // Load episodes for a show and populate tree widget
    void loadShowEpisodes(const QString& showFolderPath);
    
    // Handle episode double-click in tree widget
    void onEpisodeDoubleClicked(QTreeWidgetItem* item, int column);
    void onTreeSelectionChanged();
    void deleteBrokenVideosFromCategory();
    void repairBrokenVideo();
    void corruptVideoMetadata();  // Debug only
    
    // Decrypt and play episode
    void decryptAndPlayEpisode(const QString& encryptedFilePath, const QString& episodeName);
    
    // Clean up temporary decrypted file
    void cleanupTempFile();
    
    // Force media player to release video file
    void forceReleaseVideoFile();
    
    // Decrypt video file with metadata handling
    bool decryptVideoWithMetadata(const QString& sourceFile, const QString& targetFile);
    
    // Context menu for show list
    void setupContextMenu();
    void showContextMenu(const QPoint& pos);
    
    // Context menu for poster on display page
    void setupPosterContextMenu();
    void showPosterContextMenu(const QPoint& pos);
    
    // Context menu actions
    void addEpisodesToShow();
    void decryptAndExportShow();
    void deleteShow();
    void showInFileExplorer();  // Show TV show folder in File Explorer
    
    // Helper functions for export
    qint64 calculateShowSize(const QString& showFolderPath);
    qint64 estimateDecryptedSize(const QString& showFolderPath);
    bool exportShowEpisodes(const QString& showFolderPath, const QString& exportPath, const QString& showName);
    void performExportWithWorker(const QString& showFolderPath, const QString& exportPath, const QString& showName);
    
    // Currently selected show for context menu operations
    QString m_contextMenuShowName;
    QString m_contextMenuShowPath;
    
    // Episode tree widget context menu
    void setupEpisodeContextMenu();
    void showEpisodeContextMenu(const QPoint& pos);
    
    // Episode context menu actions
    void playEpisodeFromContextMenu();
    void decryptAndExportEpisodeFromContextMenu();
    void deleteEpisodeFromContextMenu();
    void editEpisodeMetadata();
    void editMultipleEpisodesMetadata();
    void toggleWatchedStateFromContextMenu();
    void toggleFavouriteStateFromContextMenu();
    void showEpisodesInFileExplorer();  // Show episodes in File Explorer
    
    // TMDB re-acquisition methods
    void reacquireTMDBForSingleEpisode(const QString& videoFilePath, const VP_ShowsMetadata::ShowMetadata& metadata);
    void reacquireTMDBForMultipleEpisodes(const QStringList& videoFilePaths);
    void reacquireTMDBForMultipleEpisodesWithMetadata(const QStringList& videoFilePaths, 
                                                       const QList<VP_ShowsMetadata::ShowMetadata>& metadataList,
                                                       VP_ShowsEditMultipleMetadataDialog* dialog);
    void reacquireTMDBFromContextMenu();  // Context menu handler for TMDB re-acquisition
    
    // Helper functions for episode operations
    void collectEpisodesFromTreeItem(QTreeWidgetItem* item, QStringList& episodePaths);
    void updateFavouriteIndicators();  // Update visual indicators for favourite episodes
    
    // Helper functions for broken file handling
    bool isItemBroken(QTreeWidgetItem* item) const;
    bool isBrokenCategory(QTreeWidgetItem* item) const;
    bool hasAnyBrokenItemInSelection(const QList<QTreeWidgetItem*>& items) const;
    bool hasAnyWorkingItemInSelection(const QList<QTreeWidgetItem*>& items) const;
    void enforceSelectionRestrictions();
    bool deleteEpisodesWithConfirmation(const QStringList& episodePaths, const QString& description);
    bool exportEpisodes(const QStringList& episodePaths, const QString& exportPath, const QString& showName);
    void setWatchedStateForEpisodes(const QStringList& episodePaths, bool watched);
    void performEpisodeExportWithWorker(const QStringList& episodePaths, const QString& exportPath, const QString& description, bool createFolderStructure = true);
    
    // Currently selected tree item for episode context menu
    QTreeWidgetItem* m_contextMenuTreeItem = nullptr;  // Initialize to nullptr for safety
    QString m_contextMenuEpisodePath;
    QStringList m_contextMenuEpisodePaths;
    
    // Helper function to clear context menu data
    void clearContextMenuData();
    
    // Helper to safely check tree item validity
    bool isTreeItemValid(QTreeWidgetItem* item) const;
    
    // Autoplay tracking
    QString m_currentPlayingEpisodePath;  // Path of currently playing episode
    bool m_isAutoplayInProgress = false;  // Flag to prevent multiple autoplay triggers
    bool m_episodeWasNearCompletion = false;  // Flag to track if episode reached near-end position
    bool m_forceStartFromBeginning = false;  // Flag to force starting from beginning when near end on direct play
    bool m_isRandomAutoplay = false;  // Flag to indicate this is random autoplay (for position reset)
    bool m_isDecrypting = false;  // Flag to track if we're currently decrypting an episode
    
    // Removed playback speed management - now handled globally in BaseVideoPlayer
    
    // Safe list/tree widget access helpers
    QListWidgetItem* safeGetListItem(QListWidget* widget, int index) const;
    QTreeWidgetItem* safeGetTreeItem(QTreeWidget* widget, int index) const;
    QListWidgetItem* safeTakeListItem(QListWidget* widget, int index);
    bool validateListWidget(QListWidget* widget) const;
    bool validateTreeWidget(QTreeWidget* widget) const;
    int safeGetListItemCount(QListWidget* widget) const;
    int safeGetTreeItemCount(QTreeWidget* widget) const;
    
    // Settings handling
    // Load settings for the current show (checkboxes)
    void loadShowSettings(const QString& showFolderPath);
    
    // Save settings for the current show when checkboxes change
    void saveShowSettings();
    
    // Current show settings
    VP_ShowsSettings::ShowSettings m_currentShowSettings;
    
    // Dialog settings stored for later use
    bool m_dialogUseTMDB;  // Whether to use TMDB (from add dialog)
    QString m_dialogShowName;  // Show name from add dialog
    int m_dialogShowId = 0;  // Store the selected TMDB show ID

    // Store the import output path for use in onEncryptionComplete
    QString m_currentImportOutputPath;
    
    // Store the original source folder selected by user for cleanup boundary
    // Only set when importing a complete show via folder selection (not for individual file imports)
    QString m_originalSourceFolderPath;
    
    // REMOVED - Checkbox handlers moved to settings dialog
    // void onSkipContentCheckboxChanged(int state);
    // void onAutoplayCheckboxChanged(int state);
    
    // Pending autoplay information for signal-based synchronization
    QString m_pendingAutoplayPath;
    QString m_pendingAutoplayName;
    bool m_pendingAutoplayIsRandom = false;
    
    // Pending context menu play information (for playing after closing existing player)
    QString m_pendingContextMenuEpisodePath;
    QString m_pendingContextMenuEpisodeName;
    
    // Autoplay functionality
    QStringList getAllAvailableEpisodes() const;  // Get all episodes in playback order
    QString findNextEpisode(const QString& currentEpisodePath) const;  // Find next episode to play
    QString findRandomEpisode();  // Find random episode for autoplay based on watch status
    void autoplayNextEpisode();  // Trigger autoplay of next episode
    void handleEpisodeNearCompletion(const QString& episodePath);  // Handle when episode is about to end
    
    // Cleanup functions
    void cleanupEmptyShowFolder(const QString& folderPath);  // Clean up a single empty show folder
    void cleanupIncompleteShowFolders();  // Clean up all incomplete show folders on startup
    
    // New episode detection
    void checkAndDisplayNewEpisodes(const QString& showFolderPath, int tmdbShowId);
    void displayNewEpisodeIndicator(bool hasNewEpisodes, int newEpisodeCount);
    
    // Track new episode status for current show
    bool m_currentShowHasNewEpisodes = false;
    int m_currentShowNewEpisodeCount = 0;
    
    // Helper function to determine and store the last available episode
    QString determineLastAvailableEpisode(const QStringList& allVideoFiles, VP_ShowsMetadata& metadataManager);
    void updateLastAvailableEpisode(const QString& showFolderPath);

protected:
    // Event filter for handling escape key on display page
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    // Slot for add show button
    void on_pushButton_VP_List_AddShow_clicked();
    
    // Slot for add episode button (standalone, no pre-selected show)
    void on_pushButton_VP_List_AddEpisode_clicked();
    
    // Slot for handling show list selection changes
    void onShowListSelectionChanged();
    
    // Slot for play/continue button
    void onPlayContinueClicked();
    
    // Slot for encryption completion
    void onEncryptionComplete(bool success, const QString& message,
                             const QStringList& successfulFiles,
                             const QStringList& failedFiles);
    
    // Slot for view mode change
    void onViewModeChanged(int index);
    
    // Slot for handling player destruction during autoplay
    void onPlayerDestroyedDuringAutoplay();

private slots:
    // Search functionality slots
    void onSearchTextChanged(const QString& text);
    void onSearchTimerTimeout();
    
    //void toggleWatchedStateFromContextMenu();

signals:
    void videoPlayerError(const QString& errorMessage);
};

#endif // OPERATIONS_VP_SHOWS_H
