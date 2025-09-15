#ifndef VP_SHOWS_NEWEPISODE_CHECKER_H
#define VP_SHOWS_NEWEPISODE_CHECKER_H

#include <QObject>
#include <QThread>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QAtomicInt>
#include <functional>
#include "../../../Operations-Global/ThreadSafeContainers.h"

class MainWindow;
class VP_ShowsEpisodeDetector;
class VP_ShowsSettings;
class SafeTimer;

/**
 * @brief Worker class for checking new episodes in the background
 * 
 * This class runs in a separate thread and checks for new episodes
 * for all shows in the user's library. It skips shows that already
 * have NewEPCount > 0 to save resources.
 */
class VP_ShowsNewEpisodeChecker : public QObject
{
    Q_OBJECT
    
public:
    struct ShowInfo {
        QString showName;
        QString folderPath;
        int tmdbId;
        bool useTMDB;
        bool displayNewEpNotif;
        int currentNewEPCount;  // Current cached count
        
        ShowInfo() : tmdbId(0), useTMDB(false), displayNewEpNotif(true), currentNewEPCount(0) {}
    };
    
    explicit VP_ShowsNewEpisodeChecker(QPointer<MainWindow> mainWindow, QObject* parent = nullptr);
    ~VP_ShowsNewEpisodeChecker();
    
    // Set callback to check if on video player tab
    void setTabCheckCallback(std::function<bool()> callback) { m_isOnVideoPlayerTabCallback = callback; }
    
    // Set the list of shows to check
    void setShowsList(const QList<ShowInfo>& shows);
    
    // Cancel the checking operation
    void cancel();
    
    // Check if the checker is currently running
    bool isRunning() const;
    
public slots:
    // Start checking for new episodes
    void startChecking();
    
signals:
    // Progress updates
    void progressUpdated(int current, int total, const QString& showName);
    void statusMessage(const QString& message);
    
    // New episodes found for a show
    void newEpisodesFound(const QString& folderPath, int newEpisodeCount);
    
    // Checking completed
    void checkingFinished(int showsChecked, int showsWithNewEpisodes);
    
    // Rate limit hit
    void rateLimitHit(int retryInSeconds);
    
private:
    // Thread-safe members
    QPointer<MainWindow> m_mainWindow;
    ThreadSafeList<ShowInfo> m_showsList;
    QAtomicInt m_cancelled;
    QAtomicInt m_isRunning;
    
    // Mutex for protecting shared state
    mutable QMutex m_stateMutex;
    
    // Episode detector
    VP_ShowsEpisodeDetector* m_episodeDetector;
    
    // Statistics
    int m_totalShows;
    int m_showsChecked;
    int m_showsWithNewEpisodes;
    int m_showsSkipped;
    
    // Rate limiting
    int m_rateLimitRetries;
    static const int MAX_RATE_LIMIT_RETRIES = 30;  // Max 60 seconds of retrying (30 * 2 seconds)
    static const int RATE_LIMIT_WAIT_MS = 2000;    // 2 seconds between retries
    
    // Helper methods
    bool shouldCheckShow(const ShowInfo& show) const;
    bool checkShowForNewEpisodes(const ShowInfo& show);
    void updateShowSettings(const QString& folderPath, int newEpisodeCount);
    bool isOnVideoPlayerTab() const;
    void updateStatusBar(const QString& message);
    void clearStatusBar();
    
    // Callback to check if on video player tab
    std::function<bool()> m_isOnVideoPlayerTabCallback;
};

/**
 * @brief Manager class that handles the worker thread for episode checking
 * 
 * This class manages the lifecycle of the worker thread and provides
 * a clean interface for the main application.
 */
class VP_ShowsNewEpisodeCheckerManager : public QObject
{
    Q_OBJECT
    
public:
    explicit VP_ShowsNewEpisodeCheckerManager(QPointer<MainWindow> mainWindow, QObject* parent = nullptr);
    ~VP_ShowsNewEpisodeCheckerManager();
    
    // Set callback to check if on video player tab
    void setTabCheckCallback(std::function<bool()> callback) { m_tabCheckCallback = callback; }
    
    // Start checking for new episodes
    void startChecking(const QList<VP_ShowsNewEpisodeChecker::ShowInfo>& shows);
    
    // Cancel any ongoing checking
    void cancelChecking();
    
    // Check if currently checking
    bool isChecking() const;
    
signals:
    // Forward signals from worker
    void newEpisodesFound(const QString& folderPath, int newEpisodeCount);
    void checkingFinished(int showsChecked, int showsWithNewEpisodes);
    
private slots:
    void onProgressUpdated(int current, int total, const QString& showName);
    void onStatusMessage(const QString& message);
    void onRateLimitHit(int retryInSeconds);
    void onCheckingFinished(int showsChecked, int showsWithNewEpisodes);
    
private:
    void cleanup();
    
    QPointer<MainWindow> m_mainWindow;
    VP_ShowsNewEpisodeChecker* m_worker;
    QThread* m_workerThread;
    
    // Timer for status bar updates
    SafeTimer* m_statusBarTimer;
    QString m_lastStatusMessage;
    
    // Callback to check if on video player tab
    std::function<bool()> m_tabCheckCallback;
};

#endif // VP_SHOWS_NEWEPISODE_CHECKER_H
