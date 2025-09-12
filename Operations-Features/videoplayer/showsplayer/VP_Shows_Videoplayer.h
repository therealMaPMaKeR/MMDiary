#ifndef VP_SHOWS_VIDEOPLAYER_H
#define VP_SHOWS_VIDEOPLAYER_H

#include "../BaseVideoPlayer.h"
#include "vp_shows_watchhistory.h"
#include "../../Operations-Global/SafeTimer.h"

class VP_Shows_Videoplayer : public BaseVideoPlayer
{
    Q_OBJECT

public:
    explicit VP_Shows_Videoplayer(QWidget *parent = nullptr);
    ~VP_Shows_Videoplayer();

    // Override video control functions that need show-specific behavior
    void play() override;
    void pause() override;
    void stop() override;
    
    // Watch history integration (show-specific)
    void setWatchHistoryManager(VP_ShowsWatchHistory* watchHistory);
    void setEpisodeInfo(const QString& showPath, const QString& episodePath, const QString& episodeIdentifier = QString());

// No need to redeclare signals - they're inherited from BaseVideoPlayer

private slots:
    void saveWatchProgress();  // Show-specific periodic save of watch progress
protected:
    // Override event handlers if show-specific behavior is needed
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;  // Handle window state changes
    void keyPressEvent(QKeyEvent *event) override;  // Handle Ctrl+Left/Right for shows
    void handlePlaybackStateChanged(VP_VLCPlayer::PlayerState state) override;
    void handleVideoFinished() override;  // Override to preserve autoplay

private:
    // Show-specific members only
    // Watch history management
    VP_ShowsWatchHistory* m_watchHistory;
    QString m_showPath;
    QString m_episodePath;
    QString m_episodeIdentifier;
    SafeTimer* m_progressSaveTimer;
    qint64 m_lastSavedPosition;
    bool m_hasStartedPlaying;
    
    // Window state restoration flags for autoplay
    bool m_shouldRestoreFullscreen = false;
    bool m_shouldRestoreMaximized = false;
    bool m_shouldRestoreMinimized = false;
    SafeTimer* m_minimizeTimer = nullptr;  // Timer for delayed minimize
    bool m_hasBeenMinimized = false;    // Track if we've already minimized
    
    // Helper methods
    bool shouldUpdateProgressForShows(qint64 currentPosition) const;
    void initializeWatchProgress();
    void finalizeWatchProgress();
    
public:
    // Static method to reset stored window settings (for when playing a different show)
    static void resetStoredWindowSettings();
};

#endif // VP_SHOWS_VIDEOPLAYER_H
