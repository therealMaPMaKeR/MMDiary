#ifndef VP_SHOWS_VIDEOPLAYER_H
#define VP_SHOWS_VIDEOPLAYER_H

#include "../BaseVideoPlayer.h"
#include "vp_shows_watchhistory.h"

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
    void handlePlaybackStateChanged(VP_VLCPlayer::PlayerState state) override;
    
    // Override initialization for show-specific settings
    void initializeFromPreviousSettings() override;

private:
    // Show-specific members only
    // Watch history management
    VP_ShowsWatchHistory* m_watchHistory;
    QString m_showPath;
    QString m_episodePath;
    QString m_episodeIdentifier;
    QTimer* m_progressSaveTimer;
    qint64 m_lastSavedPosition;
    bool m_hasStartedPlaying;
    
    // Helper methods
    bool shouldUpdateProgressForShows(qint64 currentPosition) const;
    void initializeWatchProgress();
    void finalizeWatchProgress();
    
public:
    // Static method to reset stored window settings (for when playing a different show)
    static void resetStoredWindowSettings();
};

#endif // VP_SHOWS_VIDEOPLAYER_H
