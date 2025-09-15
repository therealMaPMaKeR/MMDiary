#include "vp_shows_newepisode_checker.h"
#include "vp_shows_episode_detector.h"
#include "vp_shows_settings.h"
#include "vp_shows_config.h"
#include "vp_shows_tmdb.h"
#include "../../../MainWindow.h"
#include "../../../Operations-Global/SafeTimer.h"
#include <QThread>
#include <QDebug>
#include <QDate>
#include <QApplication>
#include <QStatusBar>

// ============================================================================
// VP_ShowsNewEpisodeChecker Implementation
// ============================================================================

VP_ShowsNewEpisodeChecker::VP_ShowsNewEpisodeChecker(QPointer<MainWindow> mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_cancelled(0)
    , m_isRunning(0)
    , m_episodeDetector(nullptr)
    , m_totalShows(0)
    , m_showsChecked(0)
    , m_showsWithNewEpisodes(0)
    , m_showsSkipped(0)
    , m_rateLimitRetries(0)
    , m_isOnVideoPlayerTabCallback(nullptr)
{
    qDebug() << "VP_ShowsNewEpisodeChecker: Constructor called in thread" << QThread::currentThreadId();
    
    // Create episode detector
    if (m_mainWindow) {
        m_episodeDetector = new VP_ShowsEpisodeDetector(m_mainWindow);
    }
}

VP_ShowsNewEpisodeChecker::~VP_ShowsNewEpisodeChecker()
{
    qDebug() << "VP_ShowsNewEpisodeChecker: Destructor called in thread" << QThread::currentThreadId();
    
    // Cancel any ongoing operation
    cancel();
    
    // Clean up episode detector
    if (m_episodeDetector) {
        delete m_episodeDetector;
        m_episodeDetector = nullptr;
    }
}

void VP_ShowsNewEpisodeChecker::setShowsList(const QList<ShowInfo>& shows)
{
    qDebug() << "VP_ShowsNewEpisodeChecker: Setting shows list with" << shows.size() << "shows";
    
    // Clear existing list
    m_showsList.clear();
    
    // Add all shows to the thread-safe list
    for (const ShowInfo& show : shows) {
        m_showsList.append(show);
    }
    
    m_totalShows = shows.size();
}

void VP_ShowsNewEpisodeChecker::cancel()
{
    qDebug() << "VP_ShowsNewEpisodeChecker: Cancellation requested";
    m_cancelled = 1;
}

bool VP_ShowsNewEpisodeChecker::isRunning() const
{
    return m_isRunning == 1;
}

void VP_ShowsNewEpisodeChecker::startChecking()
{
    qDebug() << "VP_ShowsNewEpisodeChecker: Starting episode checking in thread" << QThread::currentThreadId();
    
    // Check if already running
    if (m_isRunning.fetchAndStoreOrdered(1) == 1) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Already running, ignoring start request";
        return;
    }
    
    // Reset statistics
    m_showsChecked = 0;
    m_showsWithNewEpisodes = 0;
    m_showsSkipped = 0;
    m_rateLimitRetries = 0;
    m_cancelled = 0;
    
    // Check if we have shows to check
    int showCount = m_showsList.size();
    if (showCount == 0) {
        qDebug() << "VP_ShowsNewEpisodeChecker: No shows to check";
        m_isRunning = 0;
        emit checkingFinished(0, 0);
        return;
    }
    
    qDebug() << "VP_ShowsNewEpisodeChecker: Checking" << showCount << "shows for new episodes";
    emit statusMessage(QString("Checking %1 shows for new episodes...").arg(showCount));
    
    // Process each show
    int currentIndex = 0;
    m_showsList.safeIterate([this, &currentIndex](const ShowInfo& show) {
        // Check if cancelled
        if (m_cancelled == 1) {
            qDebug() << "VP_ShowsNewEpisodeChecker: Operation cancelled";
            return;
        }
        
        currentIndex++;
        
        // Update progress
        emit progressUpdated(currentIndex, m_totalShows, show.showName);
        updateStatusBar(QString("Checking for new episodes: %1 (%2/%3)")
                       .arg(show.showName)
                       .arg(currentIndex)
                       .arg(m_totalShows));
        
        // Check if we should check this show
        if (!shouldCheckShow(show)) {
            qDebug() << "VP_ShowsNewEpisodeChecker: Skipping show" << show.showName;
            m_showsSkipped++;
            return;
        }
        
        // Check for new episodes
        if (checkShowForNewEpisodes(show)) {
            m_showsWithNewEpisodes++;
        }
        
        m_showsChecked++;
        
        // Small delay to avoid hammering the API
        if (currentIndex < m_totalShows) {
            QThread::msleep(100);
        }
    });
    
    // Clear status bar message
    clearStatusBar();
    
    // Mark as not running
    m_isRunning = 0;
    
    // Emit completion signal
    qDebug() << "VP_ShowsNewEpisodeChecker: Checking completed. Checked:" << m_showsChecked
             << "Shows with new episodes:" << m_showsWithNewEpisodes
             << "Skipped:" << m_showsSkipped;
    
    emit checkingFinished(m_showsChecked, m_showsWithNewEpisodes);
}

bool VP_ShowsNewEpisodeChecker::shouldCheckShow(const ShowInfo& show) const
{
    // Skip if TMDB is disabled globally
    if (!VP_ShowsConfig::isTMDBEnabled()) {
        qDebug() << "VP_ShowsNewEpisodeChecker: TMDB disabled globally, skipping" << show.showName;
        return false;
    }
    
    // Skip if show doesn't use TMDB
    if (!show.useTMDB) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Show doesn't use TMDB, skipping" << show.showName;
        return false;
    }
    
    // Skip if show doesn't display new episode notifications
    if (!show.displayNewEpNotif) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Show has notifications disabled, skipping" << show.showName;
        return false;
    }
    
    // Skip if show has invalid TMDB ID
    if (show.tmdbId <= 0) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Invalid TMDB ID, skipping" << show.showName;
        return false;
    }
    
    // IMPORTANT: Skip if show already has NewEPCount > 0 (to save resources)
    if (show.currentNewEPCount > 0) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Show already has" << show.currentNewEPCount 
                 << "new episodes cached, skipping" << show.showName;
        return false;
    }
    
    return true;
}

bool VP_ShowsNewEpisodeChecker::checkShowForNewEpisodes(const ShowInfo& show)
{
    qDebug() << "VP_ShowsNewEpisodeChecker: Checking show" << show.showName 
             << "for new episodes (TMDB ID:" << show.tmdbId << ")";
    
    if (!m_episodeDetector) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Episode detector is null";
        return false;
    }
    
    // Use the episode detector to check for new episodes
    VP_ShowsEpisodeDetector::NewEpisodeInfo newEpisodeInfo;
    int retryCount = 0;
    bool success = false;
    
    while (retryCount <= 5 && !success && m_cancelled == 0) {
        // Try to check for new episodes
        newEpisodeInfo = m_episodeDetector->checkForNewEpisodes(show.folderPath, show.tmdbId);
        
        // Check if we have a valid result (if the count is 0, it might be rate limited)
        // We consider it successful if we got a response (even if no new episodes)
        // Rate limiting typically results in the detector returning empty results
        if (retryCount == 0 || newEpisodeInfo.newEpisodeCount > 0) {
            // First attempt or found episodes - consider it successful
            success = true;
            m_rateLimitRetries = 0;  // Reset rate limit counter on success
        } else {
            // Might be rate limited, retry
            retryCount++;
            m_rateLimitRetries++;
            
            if (m_rateLimitRetries > MAX_RATE_LIMIT_RETRIES) {
                qDebug() << "VP_ShowsNewEpisodeChecker: Max rate limit retries exceeded, giving up";
                emit statusMessage("Too many rate limit retries, stopping check");
                return false;
            }
            
            qDebug() << "VP_ShowsNewEpisodeChecker: Possible rate limit, retry" << retryCount 
                     << "in 2 seconds";
            
            // Emit rate limit signal
            emit rateLimitHit(2);
            updateStatusBar(QString("Rate limited, retrying in 2 seconds... (%1/%2)")
                           .arg(retryCount)
                           .arg(5));
            
            // Wait 2 seconds before retry
            for (int i = 0; i < 20 && m_cancelled == 0; i++) {
                QThread::msleep(100);
                QApplication::processEvents();
            }
        }
    }
    
    if (!success) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Failed to check show after retries";
        return false;
    }
    
    // Update settings if new episodes were found
    if (newEpisodeInfo.hasNewEpisodes) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Found" << newEpisodeInfo.newEpisodeCount 
                 << "new episodes for" << show.showName;
        
        // Update show settings with the new episode count
        updateShowSettings(show.folderPath, newEpisodeInfo.newEpisodeCount);
        
        // Emit signal to update the UI
        emit newEpisodesFound(show.folderPath, newEpisodeInfo.newEpisodeCount);
        
        return true;
    } else {
        qDebug() << "VP_ShowsNewEpisodeChecker: No new episodes for" << show.showName;
        
        // Update the check date even if no new episodes
        updateShowSettings(show.folderPath, 0);
    }
    
    return false;
}

void VP_ShowsNewEpisodeChecker::updateShowSettings(const QString& folderPath, int newEpisodeCount)
{
    if (!m_mainWindow) {
        qDebug() << "VP_ShowsNewEpisodeChecker: MainWindow is null, cannot update settings";
        return;
    }
    
    // Create settings manager
    VP_ShowsSettings settingsManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Load current settings
    VP_ShowsSettings::ShowSettings settings;
    if (!settingsManager.loadShowSettings(folderPath, settings)) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Failed to load settings for" << folderPath;
        return;
    }
    
    // Update the new episode check date and count
    QString todayStr = QDate::currentDate().toString(Qt::ISODate);
    settings.NewEPCheckDate = todayStr;
    settings.NewAvailableEPCount = newEpisodeCount;
    
    // Save updated settings
    if (!settingsManager.saveShowSettings(folderPath, settings)) {
        qDebug() << "VP_ShowsNewEpisodeChecker: Failed to save settings for" << folderPath;
        return;
    }
    
    qDebug() << "VP_ShowsNewEpisodeChecker: Updated settings for" << folderPath
             << "- Check date:" << todayStr << "New episode count:" << newEpisodeCount;
}

bool VP_ShowsNewEpisodeChecker::isOnVideoPlayerTab() const
{
    // Use the callback if provided
    if (m_isOnVideoPlayerTabCallback) {
        return m_isOnVideoPlayerTabCallback();
    }
    
    // Default to false if no callback
    return false;
}

void VP_ShowsNewEpisodeChecker::updateStatusBar(const QString& message)
{
    // Only update status bar if we're on the video player tab
    if (!isOnVideoPlayerTab()) {
        return;
    }
    
    if (m_mainWindow && m_mainWindow->statusBar()) {
        m_mainWindow->statusBar()->showMessage(message);
    }
}

void VP_ShowsNewEpisodeChecker::clearStatusBar()
{
    // Only clear if we're on the video player tab
    if (!isOnVideoPlayerTab()) {
        return;
    }
    
    if (m_mainWindow && m_mainWindow->statusBar()) {
        m_mainWindow->statusBar()->clearMessage();
    }
}

// ============================================================================
// VP_ShowsNewEpisodeCheckerManager Implementation
// ============================================================================

VP_ShowsNewEpisodeCheckerManager::VP_ShowsNewEpisodeCheckerManager(QPointer<MainWindow> mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_worker(nullptr)
    , m_workerThread(nullptr)
    , m_statusBarTimer(nullptr)
    , m_tabCheckCallback(nullptr)
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Constructor called";
    
    // Create status bar update timer
    m_statusBarTimer = new SafeTimer(this, "VP_ShowsNewEpisodeCheckerManager");
    m_statusBarTimer->setInterval(100);  // Update every 100ms
}

VP_ShowsNewEpisodeCheckerManager::~VP_ShowsNewEpisodeCheckerManager()
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Destructor called";
    cleanup();
}

void VP_ShowsNewEpisodeCheckerManager::startChecking(const QList<VP_ShowsNewEpisodeChecker::ShowInfo>& shows)
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Starting episode checking for" << shows.size() << "shows";
    
    // Clean up any previous operation
    cleanup();
    
    // Create worker and thread
    m_worker = new VP_ShowsNewEpisodeChecker(m_mainWindow);
    m_workerThread = new QThread();
    
    // Pass the tab check callback to the worker
    if (m_tabCheckCallback) {
        m_worker->setTabCheckCallback(m_tabCheckCallback);
    }
    
    // Set shows list
    m_worker->setShowsList(shows);
    
    // Move worker to thread
    m_worker->moveToThread(m_workerThread);
    
    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &VP_ShowsNewEpisodeChecker::startChecking);
    // Don't use deleteLater on thread - we manage deletion in cleanup()
    
    connect(m_worker, &VP_ShowsNewEpisodeChecker::progressUpdated,
            this, &VP_ShowsNewEpisodeCheckerManager::onProgressUpdated);
    
    connect(m_worker, &VP_ShowsNewEpisodeChecker::statusMessage,
            this, &VP_ShowsNewEpisodeCheckerManager::onStatusMessage);
    
    connect(m_worker, &VP_ShowsNewEpisodeChecker::newEpisodesFound,
            this, &VP_ShowsNewEpisodeCheckerManager::newEpisodesFound);
    
    connect(m_worker, &VP_ShowsNewEpisodeChecker::checkingFinished,
            this, &VP_ShowsNewEpisodeCheckerManager::onCheckingFinished);
    
    connect(m_worker, &VP_ShowsNewEpisodeChecker::rateLimitHit,
            this, &VP_ShowsNewEpisodeCheckerManager::onRateLimitHit);
    
    // Connect cleanup
    connect(m_worker, &VP_ShowsNewEpisodeChecker::checkingFinished,
            m_workerThread, &QThread::quit);
    
    // Start status bar timer with callback
    m_statusBarTimer->start([this]() {
        if (m_mainWindow && m_mainWindow->statusBar() && !m_lastStatusMessage.isEmpty()) {
            // Only update if on video player tab
            if (m_tabCheckCallback && m_tabCheckCallback()) {
                m_mainWindow->statusBar()->showMessage(m_lastStatusMessage);
            }
        }
    });
    
    // Start the thread
    m_workerThread->start();
}

void VP_ShowsNewEpisodeCheckerManager::cancelChecking()
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Cancelling episode checking";
    
    if (m_worker) {
        m_worker->cancel();
    }
    
    cleanup();
}

bool VP_ShowsNewEpisodeCheckerManager::isChecking() const
{
    return m_worker && m_worker->isRunning();
}

void VP_ShowsNewEpisodeCheckerManager::onProgressUpdated(int current, int total, const QString& showName)
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Progress" << current << "/" << total 
             << "- Checking:" << showName;
    
    m_lastStatusMessage = QString("Checking for new episodes: %1 (%2/%3)")
                         .arg(showName)
                         .arg(current)
                         .arg(total);
}

void VP_ShowsNewEpisodeCheckerManager::onStatusMessage(const QString& message)
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Status:" << message;
    m_lastStatusMessage = message;
}

void VP_ShowsNewEpisodeCheckerManager::onRateLimitHit(int retryInSeconds)
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Rate limit hit, retrying in" << retryInSeconds << "seconds";
    
    m_lastStatusMessage = QString("Rate limited. Retrying in %1 seconds...").arg(retryInSeconds);
}

void VP_ShowsNewEpisodeCheckerManager::onCheckingFinished(int showsChecked, int showsWithNewEpisodes)
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Checking finished. Checked:" << showsChecked
             << "Shows with new episodes:" << showsWithNewEpisodes;
    
    // Stop status bar timer
    if (m_statusBarTimer) {
        m_statusBarTimer->stop();
    }
    
    // Clear status bar
    m_lastStatusMessage.clear();
    if (m_mainWindow && m_mainWindow->statusBar()) {
        // Only clear if on video player tab
        if (m_tabCheckCallback && m_tabCheckCallback()) {
            m_mainWindow->statusBar()->clearMessage();
        }
    }
    
    // Emit completion signal
    emit checkingFinished(showsChecked, showsWithNewEpisodes);
    
    // Schedule cleanup using SafeTimer for safety
    SafeTimer::singleShot(1000, this, [this]() {
        cleanup();
    }, "VP_ShowsNewEpisodeCheckerManager::CleanupTimer");
}

void VP_ShowsNewEpisodeCheckerManager::cleanup()
{
    qDebug() << "VP_ShowsNewEpisodeCheckerManager: Cleaning up worker thread";
    
    // Stop status bar timer
    if (m_statusBarTimer) {
        m_statusBarTimer->stop();
    }
    
    // Clean up worker thread
    if (m_workerThread) {
        // First disconnect all signals to prevent any race conditions
        disconnect(m_workerThread, nullptr, nullptr, nullptr);
        
        // Check if thread is still valid and running
        if (m_workerThread->isRunning()) {
            qDebug() << "VP_ShowsNewEpisodeCheckerManager: Thread is running, requesting quit";
            m_workerThread->quit();
            if (!m_workerThread->wait(5000)) {
                qWarning() << "VP_ShowsNewEpisodeCheckerManager: Worker thread didn't quit, terminating";
                m_workerThread->terminate();
                m_workerThread->wait(2000);
            }
        }
        
        // Now safe to delete
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    
    // Clean up worker
    if (m_worker) {
        // Disconnect all signals from worker first
        disconnect(m_worker, nullptr, nullptr, nullptr);
        m_worker->deleteLater();
        m_worker = nullptr;
    }
}
