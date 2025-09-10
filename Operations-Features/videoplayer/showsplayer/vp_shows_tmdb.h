#ifndef VP_SHOWS_TMDB_H
#define VP_SHOWS_TMDB_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QMap>
#include <memory>

class VP_ShowsTMDB : public QObject
{
    Q_OBJECT

public:
    // Episode information structure
    struct EpisodeInfo {
        QString episodeName;
        QString overview;
        QString stillPath;  // Path to episode still image
        int seasonNumber;
        int episodeNumber;
        QString airDate;
        
        EpisodeInfo() : seasonNumber(0), episodeNumber(0) {}
    };
    
    // Show information structure
    struct ShowInfo {
        QString showName;
        QString overview;
        QString posterPath;  // Path to show poster
        QString backdropPath;
        QString firstAirDate;
        int tmdbId;
        QList<int> seasonNumbers;  // Available seasons
        
        ShowInfo() : tmdbId(0) {}
    };

    explicit VP_ShowsTMDB(QObject *parent = nullptr);
    ~VP_ShowsTMDB();
    
    // Set API key (should be stored securely in your app)
    void setApiKey(const QString& apiKey);
    
    // Search for TV show and get basic info (returns first result)
    bool searchTVShow(const QString& showName, ShowInfo& showInfo);
    
    // Search for TV shows and get multiple results
    QList<ShowInfo> searchTVShows(const QString& showName, int maxResults = 10);
    
    // Get show info by TMDB ID (more reliable than searching by name)
    bool getShowById(int tmdbId, ShowInfo& showInfo);
    
    // Get show poster path by TMDB ID
    QString getShowPosterById(int tmdbId);
    
    // Get show description by TMDB ID
    QString getShowDescriptionById(int tmdbId);
    
    // Get specific episode information
    bool getEpisodeInfo(int tmdbId, int season, int episode, EpisodeInfo& episodeInfo);
    
    // Structure to hold episode mapping for absolute numbering
    struct EpisodeMapping {
        int absoluteNumber;  // The absolute episode number (1, 2, 3, ...)
        int season;
        int episode;
        QString episodeName;
        QString airDate;     // Episode air date (YYYY-MM-DD format)
        
        EpisodeMapping() : absoluteNumber(0), season(0), episode(0) {}
        EpisodeMapping(int abs, int s, int e, const QString& name = QString())
            : absoluteNumber(abs), season(s), episode(e), episodeName(name) {}
    };
    
    // Fetch all episodes for a show and build absolute numbering map
    // Returns a map of absolute episode number -> season/episode info
    QMap<int, EpisodeMapping> buildEpisodeMap(int tmdbId);
    
    // Get all episodes for a specific season
    QList<EpisodeInfo> getSeasonEpisodes(int tmdbId, int seasonNumber);
    
    // Get Season 0 (specials) for a show
    QList<EpisodeInfo> getShowSpecials(int tmdbId);
    
    // Movie information structure
    struct MovieInfo {
        QString title;
        QString overview;
        QString releaseDate;
        QString posterPath;
        int tmdbId;
        
        MovieInfo() : tmdbId(0) {}
    };
    
    // Get movies related to a TV show
    QList<MovieInfo> getShowMovies(int tmdbId);
    
    // Get list of movie titles for a show (for content detection)
    QStringList getShowMovieTitles(int tmdbId);
    
    // Get list of OVA/special titles for a show (for content detection)
    QStringList getShowOvaTitles(int tmdbId);
    
    // Check if a show has only one season (excluding season 0/specials)
    // Pass the ShowInfo that was returned from searchTVShow
    static bool hasSingleSeason(const ShowInfo& showInfo);
    
    // Download image from TMDB to temporary file
    // Available sizes:
    // - Posters: w92, w154, w185, w342, w500, w780, original
    // - Stills (episode images): w92, w185, w300, original
    // - Backdrops: w300, w780, w1280, original
    bool downloadImage(const QString& imagePath, const QString& tempFilePath, bool isPoster = true);
    
    // Parse episode identifiers from filename
    static bool parseEpisodeFromFilename(const QString& filename, int& season, int& episode);
    
    // Parse episode from filename for single-season shows
    // Only looks for episode numbers, ignores season patterns
    static bool parseEpisodeForSingleSeasonShow(const QString& filename, int& episode);
    
    // Parse season/content type from folder name (episode still from filename)
    // Also detects content type overrides and absolute numbering from folder keywords
    static bool parseSeasonFromFolderName(const QString& folderName, const QString& filename, 
                                         int& season, int& episode,
                                         int& contentTypeOverride, bool& hasContentOverride);
    
    // Scale image to specified dimensions (for EPImage thumbnail)
    static QByteArray scaleImageToSize(const QByteArray& imageData, int width, int height);
    
    // Generate a proper temp file path in the app user temp directory
    // This ensures we use Data/username/temp instead of system temp
    static QString generateTempFilePath(const QString& prefix = "tmdb", const QString& extension = ".tmp");

private:
    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    QString m_apiKey;
    QString m_baseUrl;
    QString m_imageBaseUrl;
    
    // Helper methods
    QJsonObject makeApiRequest(const QString& endpoint);
    bool initializeConfiguration();
    QString sanitizeShowName(const QString& showName);
    
signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
};

#endif // VP_SHOWS_TMDB_H
