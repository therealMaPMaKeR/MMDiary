#include "vp_shows_episode_detector.h"
#include "vp_shows_tmdb.h"
#include "vp_shows_config.h"
#include "vp_shows_metadata.h"
#include "../../../MainWindow.h"
#include <QDir>
#include <QDebug>

VP_ShowsEpisodeDetector::VP_ShowsEpisodeDetector(QPointer<MainWindow> mainWindow)
    : m_mainWindow(mainWindow)
{
    qDebug() << "VP_ShowsEpisodeDetector: Created episode detector";
}

VP_ShowsEpisodeDetector::~VP_ShowsEpisodeDetector()
{
    qDebug() << "VP_ShowsEpisodeDetector: Destroyed episode detector";
}

VP_ShowsEpisodeDetector::NewEpisodeInfo VP_ShowsEpisodeDetector::checkForNewEpisodes(
    const QString& showFolderPath, int tmdbShowId)
{
    NewEpisodeInfo result;
    
    if (!m_mainWindow) {
        qDebug() << "VP_ShowsEpisodeDetector: MainWindow pointer is null";
        return result;
    }
    
    // Check if TMDB is enabled
    if (!VP_ShowsConfig::isTMDBEnabled()) {
        qDebug() << "VP_ShowsEpisodeDetector: TMDB is disabled in settings";
        return result;
    }
    
    // Check if we have a valid TMDB ID
    if (tmdbShowId <= 0) {
        qDebug() << "VP_ShowsEpisodeDetector: Invalid TMDB ID:" << tmdbShowId;
        return result;
    }
    
    // Get the latest episode the user has
    UserLatestEpisode userLatest = getLatestUserEpisode(showFolderPath);
    
    if (userLatest.season == 0 && userLatest.episode == 0 && userLatest.absoluteNumber == 0) {
        qDebug() << "VP_ShowsEpisodeDetector: No episodes found in user library";
        return result;
    }
    
    qDebug() << "VP_ShowsEpisodeDetector: User's latest episode - Season:" << userLatest.season 
             << "Episode:" << userLatest.episode 
             << "Absolute:" << userLatest.absoluteNumber
             << "IsAbsolute:" << userLatest.isAbsoluteNumbering;
    
    // Create TMDB API instance
    VP_ShowsTMDB tmdbApi;
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "VP_ShowsEpisodeDetector: No TMDB API key available";
        return result;
    }
    tmdbApi.setApiKey(apiKey);
    
    // Get show info from TMDB
    VP_ShowsTMDB::ShowInfo showInfo;
    if (!tmdbApi.getShowById(tmdbShowId, showInfo)) {
        qDebug() << "VP_ShowsEpisodeDetector: Failed to get show info from TMDB";
        return result;
    }
    
    // Build episode map if show uses absolute numbering
    QMap<int, VP_ShowsTMDB::EpisodeMapping> episodeMap;
    if (userLatest.isAbsoluteNumbering) {
        episodeMap = tmdbApi.buildEpisodeMap(tmdbShowId);
        
        // Map the user's absolute episode to season/episode
        if (episodeMap.contains(userLatest.absoluteNumber)) {
            const VP_ShowsTMDB::EpisodeMapping& mapping = episodeMap[userLatest.absoluteNumber];
            userLatest.season = mapping.season;
            userLatest.episode = mapping.episode;
            qDebug() << "VP_ShowsEpisodeDetector: Mapped absolute episode" << userLatest.absoluteNumber
                     << "to S" << userLatest.season << "E" << userLatest.episode;
        }
    }
    
    // Check each season for new episodes
    for (int seasonNum : showInfo.seasonNumbers) {
        // Skip season 0 (specials)
        if (seasonNum == 0) {
            continue;
        }
        
        // Get all episodes for this season
        QList<VP_ShowsTMDB::EpisodeInfo> seasonEpisodes = tmdbApi.getSeasonEpisodes(tmdbShowId, seasonNum);
        
        for (const VP_ShowsTMDB::EpisodeInfo& episode : seasonEpisodes) {
            // Check if this episode is after the user's latest episode
            bool isNewEpisode = false;
            
            if (seasonNum > userLatest.season) {
                // This is a new season after the user's latest
                isNewEpisode = true;
            } else if (seasonNum == userLatest.season && episode.episodeNumber > userLatest.episode) {
                // Same season but later episode
                isNewEpisode = true;
            }
            
            if (isNewEpisode) {
                // Check if the air date is in the past (not today or future)
                if (isAirDateInPast(episode.airDate)) {
                    qDebug() << "VP_ShowsEpisodeDetector: Found new episode - S" << seasonNum 
                             << "E" << episode.episodeNumber 
                             << ":" << episode.episodeName
                             << "Air date:" << episode.airDate;
                    
                    result.hasNewEpisodes = true;
                    result.newEpisodeCount++;
                    
                    // Track the latest new episode
                    if (isEpisodeLater(seasonNum, episode.episodeNumber, 
                                      result.latestSeason, result.latestEpisode)) {
                        result.latestSeason = seasonNum;
                        result.latestEpisode = episode.episodeNumber;
                        result.latestNewEpisodeName = episode.episodeName;
                        result.latestAirDate = QDate::fromString(episode.airDate, "yyyy-MM-dd");
                    }
                } else {
                    qDebug() << "VP_ShowsEpisodeDetector: Episode S" << seasonNum 
                             << "E" << episode.episodeNumber 
                             << "air date is not in the past:" << episode.airDate;
                }
            }
        }
    }
    
    if (result.hasNewEpisodes) {
        qDebug() << "VP_ShowsEpisodeDetector: Found" << result.newEpisodeCount 
                 << "new episode(s). Latest: S" << result.latestSeason 
                 << "E" << result.latestEpisode;
    } else {
        qDebug() << "VP_ShowsEpisodeDetector: No new episodes available";
    }
    
    return result;
}

VP_ShowsEpisodeDetector::UserLatestEpisode VP_ShowsEpisodeDetector::getLatestUserEpisode(
    const QString& showFolderPath)
{
    UserLatestEpisode result;
    
    if (!m_mainWindow) {
        return result;
    }
    
    // Get all video files in the folder
    QDir showDir(showFolderPath);
    QStringList videoExtensions;
    videoExtensions << "*.mmvid";
    showDir.setNameFilters(videoExtensions);
    QStringList videoFiles = showDir.entryList(QDir::Files, QDir::Name);
    
    if (videoFiles.isEmpty()) {
        qDebug() << "VP_ShowsEpisodeDetector: No video files found in folder";
        return result;
    }
    
    // Create metadata manager
    VP_ShowsMetadata metadataManager(m_mainWindow->user_Key, m_mainWindow->user_Username);
    
    // Find the latest episode
    for (const QString& videoFile : videoFiles) {
        QString videoPath = showDir.absoluteFilePath(videoFile);
        VP_ShowsMetadata::ShowMetadata metadata;
        
        if (metadataManager.readMetadataFromFile(videoPath, metadata)) {
            // Skip non-regular content (Movies, OVAs, Extras)
            if (metadata.contentType != VP_ShowsMetadata::Regular) {
                continue;
            }
            
            // Parse season and episode numbers
            bool seasonOk = false;
            bool episodeOk = false;
            int season = metadata.season.toInt(&seasonOk);
            int episode = metadata.episode.toInt(&episodeOk);
            
            if (!seasonOk || !episodeOk) {
                continue;
            }
            
            // Check if this show uses absolute numbering
            bool isAbsolute = metadata.isAbsoluteNumbering();
            
            if (isAbsolute) {
                // For absolute numbering, track the highest episode number
                if (episode > result.absoluteNumber) {
                    result.absoluteNumber = episode;
                    result.season = season;  // Usually 0 for absolute
                    result.episode = episode;
                    result.isAbsoluteNumbering = true;
                }
            } else {
                // For regular numbering, compare season and episode
                if (isEpisodeLater(season, episode, result.season, result.episode)) {
                    result.season = season;
                    result.episode = episode;
                    result.isAbsoluteNumbering = false;
                }
            }
        }
    }
    
    qDebug() << "VP_ShowsEpisodeDetector: Latest user episode - S" << result.season 
             << "E" << result.episode;
    
    return result;
}

bool VP_ShowsEpisodeDetector::isAirDateInPast(const QString& airDate)
{
    if (airDate.isEmpty()) {
        return false;
    }
    
    QDate date = QDate::fromString(airDate, "yyyy-MM-dd");
    if (!date.isValid()) {
        qDebug() << "VP_ShowsEpisodeDetector: Invalid air date format:" << airDate;
        return false;
    }
    
    QDate today = QDate::currentDate();
    
    // Only return true if the date is strictly before today (not today or future)
    return date < today;
}

bool VP_ShowsEpisodeDetector::isEpisodeLater(int season1, int episode1, int season2, int episode2)
{
    if (season1 > season2) {
        return true;
    } else if (season1 == season2 && episode1 > episode2) {
        return true;
    }
    return false;
}
