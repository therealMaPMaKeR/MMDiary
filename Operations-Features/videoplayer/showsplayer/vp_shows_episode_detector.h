#ifndef VP_SHOWS_EPISODE_DETECTOR_H
#define VP_SHOWS_EPISODE_DETECTOR_H

#include <QString>
#include <QStringList>
#include <QDate>
#include <QPointer>

class MainWindow;
class VP_ShowsTMDB;

class VP_ShowsEpisodeDetector
{
public:
    struct NewEpisodeInfo {
        bool hasNewEpisodes;
        int newEpisodeCount;
        QString latestNewEpisodeName;
        int latestSeason;
        int latestEpisode;
        QDate latestAirDate;
        
        NewEpisodeInfo() : hasNewEpisodes(false), newEpisodeCount(0), 
                          latestSeason(0), latestEpisode(0) {}
    };
    
    explicit VP_ShowsEpisodeDetector(QPointer<MainWindow> mainWindow);
    ~VP_ShowsEpisodeDetector();
    
    /**
     * Check if there are new episodes available for a show
     * @param showFolderPath Path to the show folder
     * @param tmdbShowId TMDB ID of the show
     * @return NewEpisodeInfo structure with details about new episodes
     */
    NewEpisodeInfo checkForNewEpisodes(const QString& showFolderPath, int tmdbShowId);
    
private:
    struct UserLatestEpisode {
        int season;
        int episode;
        int absoluteNumber;  // For absolute numbering shows
        bool isAbsoluteNumbering;
        
        UserLatestEpisode() : season(0), episode(0), absoluteNumber(0), isAbsoluteNumbering(false) {}
    };
    
    QPointer<MainWindow> m_mainWindow;
    
    /**
     * Get the latest episode the user has in their library
     * @param showFolderPath Path to the show folder
     * @return UserLatestEpisode structure with the latest episode info
     */
    UserLatestEpisode getLatestUserEpisode(const QString& showFolderPath);
    
    /**
     * Check if an air date is in the past (excluding today)
     * @param airDate The air date string in YYYY-MM-DD format
     * @return true if the date is in the past, false otherwise
     */
    bool isAirDateInPast(const QString& airDate);
    
    /**
     * Compare two episode positions to determine which is later
     * @param season1 First episode's season
     * @param episode1 First episode's episode number
     * @param season2 Second episode's season
     * @param episode2 Second episode's episode number
     * @return true if first episode is later than second
     */
    bool isEpisodeLater(int season1, int episode1, int season2, int episode2);
};

#endif // VP_SHOWS_EPISODE_DETECTOR_H
