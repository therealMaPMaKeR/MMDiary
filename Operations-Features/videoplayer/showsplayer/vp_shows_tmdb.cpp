#include "vp_shows_tmdb.h"
#include "../../Operations-Global/operations_files.h"
#include "inputvalidation.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QBuffer>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QDir>
#include <QThread>

// Helper function to detect if a string contains a resolution pattern
static bool containsResolutionPattern(const QString& str) {
    static const QList<QRegularExpression> patterns = {
        QRegularExpression("\\b(240|360|480|720|1080|1440|2160|4320)p\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\.(240|360|480|720|1080|1440|2160|4320)p", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\b(4K|8K|UHD|FHD|HD|SD)\\b", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : patterns) {
        if (pattern.match(str).hasMatch()) {
            return true;
        }
    }
    return false;
}

VP_ShowsTMDB::VP_ShowsTMDB(QObject *parent)
    : QObject(parent)
    , m_networkManager(std::make_unique<QNetworkAccessManager>(this))
    , m_baseUrl("https://api.themoviedb.org/3")
    , m_imageBaseUrl("https://image.tmdb.org/t/p")
{
    qDebug() << "VP_ShowsTMDB: Constructor called";
}

VP_ShowsTMDB::~VP_ShowsTMDB()
{
    qDebug() << "VP_ShowsTMDB: Destructor called";
}

void VP_ShowsTMDB::setApiKey(const QString& apiKey)
{
    m_apiKey = apiKey;
    qDebug() << "VP_ShowsTMDB: API key set";
    
    // Initialize configuration when API key is set
    if (!m_apiKey.isEmpty()) {
        initializeConfiguration();
    }
}

bool VP_ShowsTMDB::initializeConfiguration()
{
    if (m_apiKey.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Cannot initialize configuration without API key";
        return false;
    }
    
    QString endpoint = "/configuration";
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get configuration";
        return false;
    }
    
    QJsonObject images = response["images"].toObject();
    QString secureBaseUrl = images["secure_base_url"].toString();
    QString baseUrl = images["base_url"].toString();
    
    // Prefer secure URL if available, fallback to regular base URL
    if (!secureBaseUrl.isEmpty()) {
        m_imageBaseUrl = secureBaseUrl;
        qDebug() << "VP_ShowsTMDB: Using secure image base URL:" << m_imageBaseUrl;
    } else if (!baseUrl.isEmpty()) {
        m_imageBaseUrl = baseUrl;
        qDebug() << "VP_ShowsTMDB: Using regular image base URL:" << m_imageBaseUrl;
    } else {
        // Fallback to default if configuration fails
        m_imageBaseUrl = "https://image.tmdb.org/t/p";
        qDebug() << "VP_ShowsTMDB: Using default image base URL:" << m_imageBaseUrl;
    }
    
    return true;
}

QString VP_ShowsTMDB::sanitizeShowName(const QString& showName)
{
    // Remove common patterns that might interfere with search
    QString sanitized = showName;
    
    // Remove year patterns like (2023) or [2023]
    sanitized.remove(QRegularExpression("\\s*[\\(\\[]\\d{4}[\\)\\]]\\s*"));
    
    // Remove quality indicators
    sanitized.remove(QRegularExpression("\\s*(1080p|720p|480p|2160p|4K|HD|SD)", QRegularExpression::CaseInsensitiveOption));
    
    // Remove file extensions if present
    sanitized.remove(QRegularExpression("\\.(mkv|mp4|avi|mov|wmv|flv|webm)$", QRegularExpression::CaseInsensitiveOption));
    
    // Trim whitespace
    sanitized = sanitized.trimmed();
    
    qDebug() << "VP_ShowsTMDB: Sanitized show name from" << showName << "to" << sanitized;
    return sanitized;
}

QJsonObject VP_ShowsTMDB::makeApiRequest(const QString& endpoint)
{
    if (m_apiKey.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: API key not set";
        return QJsonObject();
    }
    
    QUrl url(m_baseUrl + endpoint);
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    
    // Support both API key and Bearer token authentication
    if (m_apiKey.startsWith("Bearer ") || m_apiKey.length() > 100) {
        // If it looks like a Bearer token (long string), use Authorization header
        QString bearerToken = m_apiKey.startsWith("Bearer ") ? m_apiKey : "Bearer " + m_apiKey;
        request.setRawHeader("Authorization", bearerToken.toUtf8());
    } else {
        // Use traditional API key as query parameter
        QUrlQuery query(url.query());
        query.addQueryItem("api_key", m_apiKey);
        url.setQuery(query);
    }
    
    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        if (httpStatusCode == 429) {
            // Rate limit exceeded
            qDebug() << "VP_ShowsTMDB: Rate limit exceeded for endpoint:" << endpoint << "- Please wait before making more requests.";
        } else if (httpStatusCode == 401) {
            qDebug() << "VP_ShowsTMDB: Authentication failed for endpoint:" << endpoint << "- Please check your API key.";
        } else if (httpStatusCode == 404) {
            qDebug() << "VP_ShowsTMDB: Episode not found (404) for endpoint:" << endpoint;
        } else {
            qDebug() << "VP_ShowsTMDB: Network error (" << httpStatusCode << ") for endpoint:" << endpoint << "- Error:" << reply->errorString();
        }
        
        reply->deleteLater();
        return QJsonObject();
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "VP_ShowsTMDB: Invalid JSON response for endpoint:" << endpoint;
        qDebug() << "VP_ShowsTMDB: Raw response data:" << data.left(500); // Log first 500 chars of response
        return QJsonObject();
    }
    
    return doc.object();
}

bool VP_ShowsTMDB::searchTVShow(const QString& showName, ShowInfo& showInfo)
{
    if (showName.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Empty show name provided";
        return false;
    }
    
    QString sanitizedName = sanitizeShowName(showName);
    
    // Validate input
    InputValidation::ValidationResult validationResult = 
        InputValidation::validateInput(sanitizedName, InputValidation::InputType::PlainText, 100);
    
    if (!validationResult.isValid) {
        qDebug() << "VP_ShowsTMDB: Invalid show name after sanitization:" << validationResult.errorMessage;
        return false;
    }
    
    QString endpoint = "/search/tv";
    QUrl url(m_baseUrl + endpoint);
    QUrlQuery query;
    query.addQueryItem("query", sanitizedName);
    
    // Support both API key and Bearer token authentication
    if (m_apiKey.startsWith("Bearer ") || m_apiKey.length() > 100) {
        // Use Bearer token in header, don't add API key to query
        url.setQuery(query);
    } else {
        // Add API key to query parameters
        query.addQueryItem("api_key", m_apiKey);
        url.setQuery(query);
    }
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // Add Bearer token to header if applicable
    if (m_apiKey.startsWith("Bearer ") || m_apiKey.length() > 100) {
        QString bearerToken = m_apiKey.startsWith("Bearer ") ? m_apiKey : "Bearer " + m_apiKey;
        request.setRawHeader("Authorization", bearerToken.toUtf8());
    }
    
    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        if (httpStatusCode == 429) {
            qDebug() << "VP_ShowsTMDB: Rate limit exceeded. Please wait before searching again.";
        } else if (httpStatusCode == 401) {
            qDebug() << "VP_ShowsTMDB: Authentication failed. Please check your API key.";
        } else {
            qDebug() << "VP_ShowsTMDB: Search request failed (" << httpStatusCode << "):" << reply->errorString();
        }
        
        reply->deleteLater();
        return false;
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "VP_ShowsTMDB: Invalid search response";
        return false;
    }
    
    QJsonObject response = doc.object();
    QJsonArray results = response["results"].toArray();
    
    if (results.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: No results found for" << sanitizedName;
        return false;
    }
    
    // Get the first result (most relevant)
    QJsonObject firstResult = results[0].toObject();
    
    showInfo.tmdbId = firstResult["id"].toInt();
    showInfo.showName = firstResult["name"].toString();
    showInfo.overview = firstResult["overview"].toString();
    showInfo.posterPath = firstResult["poster_path"].toString();
    showInfo.backdropPath = firstResult["backdrop_path"].toString();
    showInfo.firstAirDate = firstResult["first_air_date"].toString();
    
    qDebug() << "VP_ShowsTMDB: Found show:" << showInfo.showName << "ID:" << showInfo.tmdbId;
    
    // Get season information
    QString seasonsEndpoint = QString("/tv/%1").arg(showInfo.tmdbId);
    QJsonObject showDetails = makeApiRequest(seasonsEndpoint);
    
    if (!showDetails.isEmpty()) {
        QJsonArray seasons = showDetails["seasons"].toArray();
        for (const QJsonValue& season : seasons) {
            QJsonObject seasonObj = season.toObject();
            int seasonNumber = seasonObj["season_number"].toInt();
            if (seasonNumber > 0) {  // Skip season 0 (specials)
                showInfo.seasonNumbers.append(seasonNumber);
            }
        }
        qDebug() << "VP_ShowsTMDB: Found" << showInfo.seasonNumbers.size() << "seasons";
    }
    
    return true;
}

QList<VP_ShowsTMDB::ShowInfo> VP_ShowsTMDB::searchTVShows(const QString& showName, int maxResults)
{
    QList<ShowInfo> shows;
    
    if (showName.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Empty show name provided for multi-search";
        return shows;
    }
    
    QString sanitizedName = sanitizeShowName(showName);
    
    // Validate input
    InputValidation::ValidationResult validationResult = 
        InputValidation::validateInput(sanitizedName, InputValidation::InputType::PlainText, 100);
    
    if (!validationResult.isValid) {
        qDebug() << "VP_ShowsTMDB: Invalid show name after sanitization:" << validationResult.errorMessage;
        return shows;
    }
    
    QString endpoint = "/search/tv";
    QUrl url(m_baseUrl + endpoint);
    QUrlQuery query;
    query.addQueryItem("query", sanitizedName);
    
    // Support both API key and Bearer token authentication
    if (m_apiKey.startsWith("Bearer ") || m_apiKey.length() > 100) {
        // Use Bearer token in header, don't add API key to query
        url.setQuery(query);
    } else {
        // Add API key to query parameters
        query.addQueryItem("api_key", m_apiKey);
        url.setQuery(query);
    }
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // Add Bearer token to header if applicable
    if (m_apiKey.startsWith("Bearer ") || m_apiKey.length() > 100) {
        QString bearerToken = m_apiKey.startsWith("Bearer ") ? m_apiKey : "Bearer " + m_apiKey;
        request.setRawHeader("Authorization", bearerToken.toUtf8());
    }
    
    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        if (httpStatusCode == 429) {
            qDebug() << "VP_ShowsTMDB: Rate limit exceeded. Please wait before searching again.";
        } else if (httpStatusCode == 401) {
            qDebug() << "VP_ShowsTMDB: Authentication failed. Please check your API key.";
        } else {
            qDebug() << "VP_ShowsTMDB: Search request failed (" << httpStatusCode << "):" << reply->errorString();
        }
        
        reply->deleteLater();
        return shows;
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "VP_ShowsTMDB: Invalid search response";
        return shows;
    }
    
    QJsonObject response = doc.object();
    QJsonArray results = response["results"].toArray();
    
    if (results.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: No results found for" << sanitizedName;
        return shows;
    }
    
    // Process up to maxResults shows
    int count = qMin(results.size(), maxResults);
    for (int i = 0; i < count; ++i) {
        QJsonObject showObj = results[i].toObject();
        
        ShowInfo show;
        show.tmdbId = showObj["id"].toInt();
        show.showName = showObj["name"].toString();
        show.overview = showObj["overview"].toString();
        show.posterPath = showObj["poster_path"].toString();
        show.backdropPath = showObj["backdrop_path"].toString();
        show.firstAirDate = showObj["first_air_date"].toString();
        
        shows.append(show);
        
        qDebug() << "VP_ShowsTMDB: Found show #" << (i+1) << ":" << show.showName << "ID:" << show.tmdbId;
    }
    
    qDebug() << "VP_ShowsTMDB: Found total of" << shows.size() << "shows for search:" << sanitizedName;
    
    return shows;
}

bool VP_ShowsTMDB::getEpisodeInfo(int tmdbId, int season, int episode, EpisodeInfo& episodeInfo)
{
    if (tmdbId <= 0 || season <= 0 || episode <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid parameters for episode info - tmdbId:" << tmdbId << "season:" << season << "episode:" << episode;
        return false;
    }
    
    QString endpoint = QString("/tv/%1/season/%2/episode/%3")
                      .arg(tmdbId)
                      .arg(season)
                      .arg(episode);
    
    qDebug() << "VP_ShowsTMDB: Requesting episode info from endpoint:" << endpoint;
    
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get episode info for S" << season << "E" << episode << "- Empty response from endpoint:" << endpoint;
        return false;
    }
    
    episodeInfo.episodeName = response["name"].toString();
    episodeInfo.overview = response["overview"].toString();
    episodeInfo.stillPath = response["still_path"].toString();
    episodeInfo.seasonNumber = response["season_number"].toInt();
    episodeInfo.episodeNumber = response["episode_number"].toInt();
    episodeInfo.airDate = response["air_date"].toString();
    
    qDebug() << "VP_ShowsTMDB: Got episode info:" << episodeInfo.episodeName;
    
    return true;
}

bool VP_ShowsTMDB::hasSingleSeason(const ShowInfo& showInfo)
{
    // Check if the show has only one regular season (excluding season 0/specials)
    // Count non-zero seasons from the ShowInfo that was populated during searchTVShow
    
    int regularSeasonCount = 0;
    for (int seasonNum : showInfo.seasonNumbers) {
        if (seasonNum > 0) {  // Exclude season 0 (specials)
            regularSeasonCount++;
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Show '" << showInfo.showName << "' has" << regularSeasonCount << "regular season(s)";
    return regularSeasonCount == 1;
}

bool VP_ShowsTMDB::parseSeasonFromFolderName(const QString& folderName, const QString& filename, 
                                            int& season, int& episode,
                                            int& contentTypeOverride, bool& hasContentOverride)
{
    season = 0;
    episode = 0;
    contentTypeOverride = 0;  // Default to Regular (0)
    hasContentOverride = false;
    
    if (folderName.isEmpty() || filename.isEmpty()) {
        return false;
    }
    
    qDebug() << "VP_ShowsTMDB: Parsing season from folder:" << folderName;
    qDebug() << "VP_ShowsTMDB: Parsing episode from file:" << filename;
    
    // STEP 1: Check folder name for content type keywords FIRST
    // This should happen regardless of whether we can parse episode numbers
    QString lowerFolderName = folderName.toLower();
    
    // Check for movie/film keywords
    if (lowerFolderName.contains("movie") || lowerFolderName.contains("film")) {
        contentTypeOverride = 1;  // VP_ShowsMetadata::Movie = 1
        hasContentOverride = true;
        qDebug() << "VP_ShowsTMDB: Folder name contains movie/film - overriding content type to Movie";
    }
    // Check for OVA keywords
    else if (lowerFolderName.contains("ova") || lowerFolderName.contains("oad")) {
        contentTypeOverride = 2;  // VP_ShowsMetadata::OVA = 2
        hasContentOverride = true;
        qDebug() << "VP_ShowsTMDB: Folder name contains OVA/OAD - overriding content type to OVA";
    }
    // Check for extra/special keywords
    else if (lowerFolderName.contains("extra") || lowerFolderName.contains("special") || 
             lowerFolderName.contains("bonus")) {
        contentTypeOverride = 3;  // VP_ShowsMetadata::Extra = 3
        hasContentOverride = true;
        qDebug() << "VP_ShowsTMDB: Folder name contains extra/special/bonus - overriding content type to Extra";
    }
    
    // If we have a content type override for non-episodic content (Movie, OVA, Extra),
    // force season and episode to be empty (0) and skip filename parsing
    if (hasContentOverride) {
        season = 0;
        episode = 0;
        qDebug() << "VP_ShowsTMDB: Content type override detected - forcing empty season/episode values";
        qDebug() << "VP_ShowsTMDB: Skipping filename parsing for non-episodic content";
        return true;  // Return success with content type override and empty season/episode
    }
    
    // STEP 2: Parse both season and episode from filename using existing complex logic
    // Only do this for regular episodes (no content type override)
    bool parsedFromFile = parseEpisodeFromFilename(filename, season, episode);
    
    if (!parsedFromFile) {
        qDebug() << "VP_ShowsTMDB: Failed to parse episode from filename";
        return false;
    }
    
    qDebug() << "VP_ShowsTMDB: Filename parsing gave us S" << season << "E" << episode;
    
    // STEP 3: Check for absolute numbering indicator ("episode" keyword)
    if (lowerFolderName.contains("episode")) {
        season = 0;  // Use 0 to indicate absolute numbering
        qDebug() << "VP_ShowsTMDB: Folder name contains 'episode' - using absolute numbering (season=0)";
    } else {
        // STEP 4: Try to find season patterns in folder name (existing logic)
        QString cleanedFolderName = folderName;
        
        // Remove common folder prefixes/suffixes
        cleanedFolderName.replace(QRegularExpression("\\[.*?\\]"), " ");  // Remove bracketed content
        cleanedFolderName.replace(QRegularExpression("\\(.*?\\)"), " ");  // Remove parentheses content
        cleanedFolderName = cleanedFolderName.simplified();
        
        // Try to find season patterns in folder name
        QList<QRegularExpression> seasonPatterns = {
            // "Season 1" or "Season 01" format
            QRegularExpression("\\bSeason\\s+(\\d{1,2})\\b", QRegularExpression::CaseInsensitiveOption),
            // "S01" or "S1" format
            QRegularExpression("\\bS(\\d{1,2})\\b", QRegularExpression::CaseInsensitiveOption),
            // "Season.1" or "Season_1" format
            QRegularExpression("\\bSeason[\\._](\\d{1,2})\\b", QRegularExpression::CaseInsensitiveOption),
            // "1st Season", "2nd Season", etc.
            QRegularExpression("\\b(\\d{1,2})(?:st|nd|rd|th)\\s+Season\\b", QRegularExpression::CaseInsensitiveOption),
            // Just a number at the end of folder name (like "Show Name 2")
            QRegularExpression("\\s+(\\d{1,2})$")
        };
        
        bool seasonFoundInFolder = false;
        int folderSeason = 0;
        
        for (const auto& pattern : seasonPatterns) {
            QRegularExpressionMatch match = pattern.match(cleanedFolderName);
            if (match.hasMatch()) {
                folderSeason = match.captured(1).toInt();
                if (folderSeason > 0 && folderSeason <= 99) {
                    seasonFoundInFolder = true;
                    qDebug() << "VP_ShowsTMDB: Found season" << folderSeason << "in folder name";
                    break;
                }
            }
        }
        
        // Override season with folder value if found, otherwise keep the one from filename
        if (seasonFoundInFolder) {
            season = folderSeason;
            qDebug() << "VP_ShowsTMDB: Using season from folder:" << season;
        } else {
            qDebug() << "VP_ShowsTMDB: No season found in folder name, keeping season from filename:" << season;
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Final result - S" << season << "E" << episode;
    if (hasContentOverride) {
        qDebug() << "VP_ShowsTMDB: Content type override:" << contentTypeOverride;
    }
    return true;  // Return true as long as we got valid episode from filename
}

bool VP_ShowsTMDB::parseEpisodeForSingleSeasonShow(const QString& filename, int& episode)
{
    episode = 0;
    
    if (filename.isEmpty()) {
        return false;
    }
    
    qDebug() << "VP_ShowsTMDB: Parsing single-season show filename:" << filename;
    
    // Pre-process the filename to remove common patterns that might interfere
    QString cleanedFilename = filename;
    
    // STEP 1: Remove file extension
    int lastDot = cleanedFilename.lastIndexOf('.');
    if (lastDot > 0 && cleanedFilename.length() - lastDot <= 5) {
        cleanedFilename = cleanedFilename.left(lastDot);
    }
    
    // STEP 2: Remove bracketed content that contains codec/quality info
    QList<QRegularExpression> bracketPatterns = {
        // Remove any brackets containing resolution indicators
        QRegularExpression("\\[[^\\]]*(?:720p|1080p|480p|360p|240p|1440p|2160p|4320p|BD|DVD|WEB|HDTV|BluRay|BRRip|WEBRip|FLAC|AAC|AC3|DTS|x264|x265|H264|H265|HEVC)[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption),
        // Remove brackets with hex-like codes (6+ alphanumeric characters)
        QRegularExpression("\\[[A-F0-9]{6,}\\]", QRegularExpression::CaseInsensitiveOption),
        // Remove brackets containing file sizes or bitrates
        QRegularExpression("\\[[^\\]]*(?:\\d+(?:MB|GB|KB|kbps|Kbps))[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption),
        // Remove brackets containing only numbers and dots (like release versions)
        QRegularExpression("\\[[\\d\\.]+\\]")
    };
    
    for (const auto& pattern : bracketPatterns) {
        cleanedFilename.replace(pattern, " ");
    }
    
    // STEP 3: Remove timestamp patterns
    QRegularExpression timestampPattern("\\d{1,2}\\.\\d{2}\\.\\d{2}\\s*-\\s*\\d{1,2}\\.\\d{2}\\.\\d{2}");
    cleanedFilename.replace(timestampPattern, " ");
    
    // STEP 4: Remove parentheses containing codec/quality info
    QList<QRegularExpression> parenPatterns = {
        QRegularExpression("\\([^\\)]*(?:720p|1080p|480p|BD|DVD|WEB|FLAC|AAC|AC3|x264|x265)[^\\)]*\\)", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\(\\d+(?:MB|GB|KB)\\)", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : parenPatterns) {
        cleanedFilename.replace(pattern, " ");
    }
    
    // STEP 5: Remove resolution patterns
    QList<QRegularExpression> resolutionPatterns = {
        QRegularExpression("\\b(240|360|480|720|1080|1440|2160|4320)p\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("[\\.-](240|360|480|720|1080|1440|2160|4320)p", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\b(4K|8K|UHD|FHD|HD|SD)\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\b\\d{3,4}x\\d{3,4}\\b"),
        QRegularExpression("\\b(x264|x265|h264|h265|HEVC|AVC|VP9|VP8|AV1)\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\b(FLAC|AAC|AC3|DTS|MP3|OGG|WMA|DDP|TrueHD|Atmos)\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\b(RARBG|YIFY|FGT|PSA|AMZN|NF|WEB-DL|WEBRip|BDRip|BluRay|HDTV|DVDRip)\\b", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : resolutionPatterns) {
        cleanedFilename.replace(pattern, " ");
    }
    
    // STEP 6: Remove multiple spaces and trim
    cleanedFilename = cleanedFilename.simplified();
    
    qDebug() << "VP_ShowsTMDB: Cleaned filename for single-season parsing:" << cleanedFilename;
    
    // For single-season shows, look for episode numbers ONLY (no season patterns)
    // Priority order for episode detection:
    
    // 1. Explicit "Episode X" patterns
    QList<QRegularExpression> explicitEpisodePatterns = {
        // "Episode 36" or "Ep 36" or "E 36" with word boundaries
        QRegularExpression("\\bEpisode\\s+(\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bEp\\.?\\s+(\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        // "- Episode 36 -" format (common in anime)
        QRegularExpression("\\s-\\s*Episode\\s+(\\d{1,4})\\s*-", QRegularExpression::CaseInsensitiveOption),
        // "Episode.36" or "Episode_36" formats
        QRegularExpression("\\bEpisode[\\._ ](\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        // "Ep.36" or "Ep_36" formats
        QRegularExpression("\\bEp[\\._ ](\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        // "#36" format (common in some releases)
        QRegularExpression("#(\\d{1,4})\\b"),
        // Just "E36" or "E 36" (without season)
        QRegularExpression("\\bE\\s*(\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : explicitEpisodePatterns) {
        QRegularExpressionMatch match = pattern.match(cleanedFilename);
        if (match.hasMatch()) {
            episode = match.captured(1).toInt();
            if (episode > 0 && episode <= 9999) {
                qDebug() << "VP_ShowsTMDB: Single-season parse - found episode" << episode << "from explicit pattern";
                return true;
            }
        }
    }
    
    // 2. Number patterns with dashes (common in anime)
    QList<QRegularExpression> dashPatterns = {
        // "ShowName - 18" or "Show - 18 - Title"
        QRegularExpression("\\s-\\s+(\\d{1,4})(?:\\s|$|-)"),
        // "- 18 -" format when surrounded by dashes
        QRegularExpression("\\s-\\s+(\\d{1,4})\\s+-\\s+"),
        // Just a dash before number at end
        QRegularExpression("-\\s*(\\d{1,4})$")
    };
    
    for (const auto& pattern : dashPatterns) {
        QRegularExpressionMatch match = pattern.match(cleanedFilename);
        if (match.hasMatch()) {
            episode = match.captured(1).toInt();
            if (episode > 0 && episode <= 9999) {
                qDebug() << "VP_ShowsTMDB: Single-season parse - found episode" << episode << "from dash pattern";
                return true;
            }
        }
    }
    
    // 3. Bracketed or parenthesized numbers (less common but still valid)
    QList<QRegularExpression> bracketPatterns2 = {
        // [18] or (18) format
        QRegularExpression("\\[(\\d{1,4})\\]"),
        QRegularExpression("\\((\\d{1,4})\\)")
    };
    
    for (const auto& pattern : bracketPatterns2) {
        QRegularExpressionMatch match = pattern.match(cleanedFilename);
        if (match.hasMatch()) {
            episode = match.captured(1).toInt();
            // Be more strict with bracketed numbers to avoid false positives
            if (episode > 0 && episode <= 999) {
                qDebug() << "VP_ShowsTMDB: Single-season parse - found episode" << episode << "from bracket pattern";
                return true;
            }
        }
    }
    
    // MODIFIED: Instead of looking for standalone numbers,
    // we now use the full parser and override the season
    // This prevents mistaking season numbers for episode numbers (e.g., S02E05)
    
    // Use the full parser to get both season and episode
    int parsedSeason = 0;
    int parsedEpisode = 0;
    bool fullParseResult = parseEpisodeFromFilename(filename, parsedSeason, parsedEpisode);
    
    if (fullParseResult && parsedEpisode > 0) {
        episode = parsedEpisode;
        qDebug() << "VP_ShowsTMDB: Single-season parse - found episode" << episode 
                << "(original parse gave S" << parsedSeason << "E" << parsedEpisode << ")";
        return true;
    }
    
    qDebug() << "VP_ShowsTMDB: Single-season parse - no episode number found";
    return false;
}

bool VP_ShowsTMDB::downloadImage(const QString& imagePath, const QString& tempFilePath, bool isPoster)
{
    if (imagePath.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Empty image path";
        return false;
    }
    
    // Create a temporary file in the app user temp directory if no path provided
    QString actualTempPath = tempFilePath;
    std::unique_ptr<QTemporaryFile> tempFilePtr;
    
    if (actualTempPath.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Creating temp file in app user directory";
        QString templateStr = "tmdb_image_XXXXXX";
        if (isPoster) {
            templateStr = "tmdb_poster_XXXXXX";
        } else {
            templateStr = "tmdb_still_XXXXXX";
        }
        
        tempFilePtr = OperationsFiles::createTempFile(templateStr, false);
        if (!tempFilePtr) {
            qDebug() << "VP_ShowsTMDB: Failed to create temp file";
            return false;
        }
        
        actualTempPath = tempFilePtr->fileName();
        tempFilePtr->close();  // Close but keep the file
        qDebug() << "VP_ShowsTMDB: Created temp file at:" << actualTempPath;
    }
    
    // Determine size based on image type
    QString size = isPoster ? "w500" : "w300";  // w500 for posters, w300 for episode stills
    
    QString fullUrl = m_imageBaseUrl + "/" + size + imagePath;
    qDebug() << "VP_ShowsTMDB: Downloading image from:" << fullUrl;
    
    QNetworkRequest request((QUrl(fullUrl)));
    
    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::downloadProgress, this, &VP_ShowsTMDB::downloadProgress);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "VP_ShowsTMDB: Image download failed:" << reply->errorString();
        reply->deleteLater();
        return false;
    }
    
    QByteArray imageData = reply->readAll();
    reply->deleteLater();
    
    if (imageData.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Downloaded image data is empty";
        return false;
    }
    
    // Write to temp file using secure file operations
    QFile tempFile(actualTempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qDebug() << "VP_ShowsTMDB: Failed to open temp file for writing";
        if (!tempFilePath.isEmpty() && tempFilePtr) {
            // If we created the temp file, clean it up
            QFile::remove(actualTempPath);
        }
        return false;
    }
    
    qint64 written = tempFile.write(imageData);
    tempFile.close();
    
    if (written != imageData.size()) {
        qDebug() << "VP_ShowsTMDB: Failed to write complete image data";
        if (!tempFilePath.isEmpty() && tempFilePtr) {
            // If we created the temp file, clean it up
            QFile::remove(actualTempPath);
        }
        return false;
    }
    
    // If a specific temp path was requested and we created our own temp file,
    // move the file to the requested location
    if (!tempFilePath.isEmpty() && tempFilePtr) {
        // Delete temp file if it exists
        if (QFile::exists(tempFilePath)) {
            if (!QFile::remove(tempFilePath)) {
                qDebug() << "VP_ShowsTMDB: Failed to delete existing temp file:" << tempFilePath;
            }
        }
        if (!QFile::rename(actualTempPath, tempFilePath)) {
            qDebug() << "VP_ShowsTMDB: Failed to move temp file to requested location";
            QFile::remove(actualTempPath);
            return false;
        }
        qDebug() << "VP_ShowsTMDB: Successfully downloaded image to:" << tempFilePath;
    } else {
        qDebug() << "VP_ShowsTMDB: Successfully downloaded image to:" << actualTempPath;
    }
    
    return true;
}

/**
 * Parse episode information from a filename using a priority-based approach.
 * 
 * Priority order:
 * 1. Explicit "Episode X" patterns (for absolute numbering like anime)
 * 2. Standard SxxExx patterns (for traditional TV shows)
 * 3. Numeric patterns (e.g., 101 for S01E01)
 * 4. Other absolute numbering patterns
 * 
 * The function pre-processes the filename to remove resolution patterns
 * (720p, 1080p, etc.) to avoid false matches.
 * 
 * Examples:
 * - "Naruto - Episode 36 - Team 10.720p.mp4" -> Episode 36 (absolute)
 * - "Show S01E05.mkv" -> Season 1, Episode 5
 * - "Series 1x05.avi" -> Season 1, Episode 5
 * - "Anime - 036 - Title.mp4" -> Episode 36 (absolute)
 * 
 * @param filename The filename to parse
 * @param season Output: season number (0 for absolute numbering)
 * @param episode Output: episode number
 * @return true if parsing succeeded, false otherwise
 */
bool VP_ShowsTMDB::parseEpisodeFromFilename(const QString& filename, int& season, int& episode)
{
    season = 0;
    episode = 0;
    
    if (filename.isEmpty()) {
        return false;
    }
    
    qDebug() << "VP_ShowsTMDB: Original filename for parsing:" << filename;
    
    // Pre-process the filename to remove common patterns that might interfere
    QString cleanedFilename = filename;
    
    // STEP 1: Remove file extension
    int lastDot = cleanedFilename.lastIndexOf('.');
    if (lastDot > 0 && cleanedFilename.length() - lastDot <= 5) {
        cleanedFilename = cleanedFilename.left(lastDot);
    }
    
    // STEP 1.5: Extract potential episode numbers from brackets/parentheses BEFORE removing them
    // These will be used as LOW PRIORITY fallbacks
    QList<int> bracketedNumbers;
    QList<int> parenthesizedNumbers;
    
    // Extract numbers from brackets [##]
    QRegularExpression bracketNumberPattern("\\[(\\d{1,3})\\]");
    QRegularExpressionMatchIterator bracketIt = bracketNumberPattern.globalMatch(cleanedFilename);
    while (bracketIt.hasNext()) {
        QRegularExpressionMatch match = bracketIt.next();
        int num = match.captured(1).toInt();
        if (num > 0 && num <= 999) {
            bracketedNumbers.append(num);
            qDebug() << "VP_ShowsTMDB: Found bracketed number:" << num;
        }
    }
    
    // Extract numbers from parentheses (##)
    QRegularExpression parenNumberPattern("\\((\\d{1,3})\\)");
    QRegularExpressionMatchIterator parenIt = parenNumberPattern.globalMatch(cleanedFilename);
    while (parenIt.hasNext()) {
        QRegularExpressionMatch match = parenIt.next();
        int num = match.captured(1).toInt();
        if (num > 0 && num <= 999) {
            parenthesizedNumbers.append(num);
            qDebug() << "VP_ShowsTMDB: Found parenthesized number:" << num;
        }
    }
    
    // STEP 2: Remove bracketed content that contains codec/quality info
    // This removes things like [BD 1080p FLAC], [F1242A6], [720p], etc.
    QList<QRegularExpression> bracketPatterns = {
        // Remove any brackets containing resolution indicators
        QRegularExpression("\\[[^\\]]*(?:720p|1080p|480p|360p|240p|1440p|2160p|4320p|BD|DVD|WEB|HDTV|BluRay|BRRip|WEBRip|FLAC|AAC|AC3|DTS|x264|x265|H264|H265|HEVC)[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption),
        // Remove brackets with hex-like codes (6+ alphanumeric characters)
        QRegularExpression("\\[[A-F0-9]{6,}\\]", QRegularExpression::CaseInsensitiveOption),
        // Remove brackets containing file sizes or bitrates
        QRegularExpression("\\[[^\\]]*(?:\\d+(?:MB|GB|KB|kbps|Kbps))[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption),
        // Remove brackets containing only numbers and dots (like release versions)
        QRegularExpression("\\[[\\d\\.]+\\]")
    };
    
    for (const auto& pattern : bracketPatterns) {
        cleanedFilename.replace(pattern, " ");
    }
    
    // STEP 3: Remove timestamp patterns (like 0.00.07-0.21.05)
    QRegularExpression timestampPattern("\\d{1,2}\\.\\d{2}\\.\\d{2}\\s*-\\s*\\d{1,2}\\.\\d{2}\\.\\d{2}");
    cleanedFilename.replace(timestampPattern, " ");
    
    // STEP 4: Remove parentheses containing codec/quality info
    QList<QRegularExpression> parenPatterns = {
        QRegularExpression("\\([^\\)]*(?:720p|1080p|480p|BD|DVD|WEB|FLAC|AAC|AC3|x264|x265)[^\\)]*\\)", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\(\\d+(?:MB|GB|KB)\\)", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : parenPatterns) {
        cleanedFilename.replace(pattern, " ");
    }
    
    // STEP 5: Enhanced resolution removal - look for 'p' and check preceding numbers
    // This handles cases like s01e01720p where 720p is directly attached
    QRegularExpression resolutionPPattern("(240|360|480|720|1080|1440|2160|4320)p", QRegularExpression::CaseInsensitiveOption);
    cleanedFilename.replace(resolutionPPattern, " ");
    
    // Now remove other resolution patterns
    QList<QRegularExpression> resolutionPatterns = {
        // Standard resolutions with 'p' suffix
        QRegularExpression("\\b(240|360|480|720|1080|1440|2160|4320)p\\b", QRegularExpression::CaseInsensitiveOption),
        // With dots/dashes before resolution (like .720p or -720p)
        QRegularExpression("[\\.-](240|360|480|720|1080|1440|2160|4320)p", QRegularExpression::CaseInsensitiveOption),
        // 4K, 8K variations
        QRegularExpression("\\b(4K|8K|UHD|FHD|HD|SD)\\b", QRegularExpression::CaseInsensitiveOption),
        // Resolution with x (like 1920x1080)
        QRegularExpression("\\b\\d{3,4}x\\d{3,4}\\b"),
        // Video formats and codecs that might contain numbers
        QRegularExpression("\\b(x264|x265|h264|h265|HEVC|AVC|VP9|VP8|AV1)\\b", QRegularExpression::CaseInsensitiveOption),
        // Audio formats
        QRegularExpression("\\b(FLAC|AAC|AC3|DTS|MP3|OGG|WMA|DDP|TrueHD|Atmos)\\b", QRegularExpression::CaseInsensitiveOption),
        // Release groups and sources
        QRegularExpression("\\b(RARBG|YIFY|FGT|PSA|AMZN|NF|WEB-DL|WEBRip|BDRip|BluRay|HDTV|DVDRip)\\b", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : resolutionPatterns) {
        cleanedFilename.replace(pattern, " ");
    }
    
    // STEP 6: Remove multiple spaces and trim
    cleanedFilename = cleanedFilename.simplified();
    
    qDebug() << "VP_ShowsTMDB: Cleaned filename:" << cleanedFilename;
    
    // PRIORITY 1: Try patterns with BOTH season and episode FIRST
    // These take highest priority to avoid false positives from episode-only patterns
    QList<QRegularExpression> seasonPatterns = {
        // S##E## or s##e## (most common) - allow underscore or word boundary before
        QRegularExpression("(?:^|[\\W_])S(\\d{1,2})E(\\d{1,3})(?:$|[\\W_])", QRegularExpression::CaseInsensitiveOption),
        
        // ##x## format - allow underscore or word boundary
        QRegularExpression("(?:^|[\\W_])(\\d{1,2})x(\\d{1,3})(?:$|[\\W_])", QRegularExpression::CaseInsensitiveOption),
        
        // S## E## with space
        QRegularExpression("(?:^|[\\W_])S(\\d{1,2})\\s+E(\\d{1,3})(?:$|[\\W_])", QRegularExpression::CaseInsensitiveOption),
        
        // Season # Episode #
        QRegularExpression("\\bSeason\\s+(\\d{1,2})\\s+Episode\\s+(\\d{1,3})\\b", QRegularExpression::CaseInsensitiveOption),
        
        // S##.E## with dot
        QRegularExpression("(?:^|[\\W_])S(\\d{1,2})\\.E(\\d{1,3})(?:$|[\\W_])", QRegularExpression::CaseInsensitiveOption),
        
        // S##_E## with underscore
        QRegularExpression("(?:^|[\\W_])S(\\d{1,2})_E(\\d{1,3})(?:$|[\\W_])", QRegularExpression::CaseInsensitiveOption),
        
        // Season.#.Episode.#
        QRegularExpression("\\bSeason\\.(\\d{1,2})\\.Episode\\.(\\d{1,3})\\b", QRegularExpression::CaseInsensitiveOption),
        
        // S# - E# format with dash
        QRegularExpression("(?:^|[\\W_])S(\\d{1,2})\\s*-\\s*E(\\d{1,3})(?:$|[\\W_])", QRegularExpression::CaseInsensitiveOption)
    };
    
    // Try season patterns on the cleaned filename
    for (const auto& pattern : seasonPatterns) {
        QRegularExpressionMatch match = pattern.match(cleanedFilename);
        if (match.hasMatch()) {
            season = match.captured(1).toInt();
            episode = match.captured(2).toInt();
            
            // Validate the extracted values
            if (season > 0 && season <= 99 && episode > 0 && episode <= 999) {
                qDebug() << "VP_ShowsTMDB: Parsed from filename:" << filename 
                        << "-> S" << season << "E" << episode;
                return true;
            }
        }
    }
    
    // PRIORITY 2: Check for explicit "Episode" patterns (for absolute numbering)
    // Only check these AFTER trying to find season+episode patterns
    QList<QRegularExpression> explicitEpisodePatterns = {
        // "Episode 36" or "Ep 36" or "E 36" with word boundaries
        QRegularExpression("\\bEpisode\\s+(\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bEp\\.?\\s+(\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        // "- Episode 36 -" format (common in anime)
        QRegularExpression("\\s-\\s*Episode\\s+(\\d{1,4})\\s*-", QRegularExpression::CaseInsensitiveOption),
        // "Episode.36" or "Episode_36" formats
        QRegularExpression("\\bEpisode[\\._](\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        // "Ep.36" or "Ep_36" formats
        QRegularExpression("\\bEp[\\._](\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption),
        // "#36" format (common in some releases)
        QRegularExpression("#(\\d{1,4})\\b"),
        // Underscore patterns like _06_ or _46_ (common in some releases)
        QRegularExpression("_(\\d{1,3})_"),
        // "- 36 -" format when surrounded by dashes (common in anime)
        QRegularExpression("\\s-\\s+(\\d{1,4})\\s+-\\s+"),
        // "ShowName - 36" format but avoid show names that ARE numbers (like "86")
        // Use negative lookbehind to ensure it's not just a number at the start
        QRegularExpression("[a-zA-Z]\\w*\\s*-\\s+(\\d{1,4})(?:\\s|$)"),
        // "Part 36" format
        QRegularExpression("\\bPart\\s+(\\d{1,4})\\b", QRegularExpression::CaseInsensitiveOption)
    };
    
    for (const auto& pattern : explicitEpisodePatterns) {
        QRegularExpressionMatch match = pattern.match(cleanedFilename);
        if (match.hasMatch()) {
            episode = match.captured(1).toInt();
            if (episode > 0 && episode <= 9999) {
                season = 0;  // Use 0 to indicate absolute numbering
                qDebug() << "VP_ShowsTMDB: Parsed absolute episode from explicit pattern:" << filename 
                        << "-> Episode" << episode;
                return true;
            }
        }
    }
    
    // PRIORITY 3: Try numeric patterns that might indicate season and episode
    // These patterns are VERY restricted to avoid false matches
    // Only use these if the filename doesn't contain many other numbers
    
    // Count how many number sequences are in the cleaned filename
    QRegularExpression numberSequence("\\b\\d+\\b");
    int numberCount = 0;
    QRegularExpressionMatchIterator it = numberSequence.globalMatch(cleanedFilename);
    while (it.hasNext()) {
        it.next();
        numberCount++;
    }
    
    // Only try numeric patterns if there aren't too many numbers in the filename
    // This helps avoid matching timestamps, file sizes, etc.
    if (numberCount <= 3) {
        QList<QRegularExpression> numericSeasonPatterns = {
            // ### format (1st digit = season, last 2 = episode, e.g., 101 = S01E01)
            // Must be at the beginning or after common separators
            QRegularExpression("(?:^|\\s|_|-)(\\d)(\\d{2})(?:\\s|_|-|$)"),
            
            // #### format (first 2 digits = season, last 2 = episode, e.g., 0101 = S01E01)
            QRegularExpression("(?:^|\\s|_|-)(\\d{2})(\\d{2})(?:\\s|_|-|$)")
        };
        
        for (const auto& pattern : numericSeasonPatterns) {
            QRegularExpressionMatch match = pattern.match(cleanedFilename);
            if (match.hasMatch()) {
                int testSeason = match.captured(1).toInt();
                int testEpisode = match.captured(2).toInt();
                
                // Very strict validation for numeric patterns
                // Season must be 1-20, episode must be 1-99
                // Also check that it's not a year (19xx or 20xx)
                if (testSeason > 0 && testSeason <= 20 && 
                    testEpisode > 0 && testEpisode <= 99 &&
                    !(testSeason == 19 || testSeason == 20)) {
                    season = testSeason;
                    episode = testEpisode;
                    qDebug() << "VP_ShowsTMDB: Parsed from numeric pattern:" << filename 
                            << "-> S" << season << "E" << episode;
                    return true;
                }
            }
        }
    }
    
    // PRIORITY 4: If no season pattern matched, try other absolute numbering patterns
    QList<QRegularExpression> absolutePatterns = {
        // - ### - format (common in anime releases) but avoid if it's at the very start (show name)
        QRegularExpression("\\S+\\s+-\\s+(\\d{1,4})\\s*-\\s"),
        
        // ### or #### format at the beginning or surrounded by non-digits
        // Only as last resort to avoid false matches
        QRegularExpression("(?:^|[^\\d])(\\d{3,4})(?:[^\\d]|$)")
    };
    
    for (const auto& pattern : absolutePatterns) {
        QRegularExpressionMatch match = pattern.match(cleanedFilename);
        if (match.hasMatch()) {
            episode = match.captured(1).toInt();
            
            // For absolute numbering, set season to 0
            if (episode > 0 && episode <= 9999) {
                season = 0;  // Indicates absolute numbering
                qDebug() << "VP_ShowsTMDB: Parsed absolute episode from pattern:" << filename 
                        << "-> Episode" << episode;
                return true;
            }
        }
    }
    
    // PRIORITY 5 (LOWEST): Use bracketed/parenthesized numbers as last resort
    // Only if we found no other patterns
    if (!bracketedNumbers.isEmpty()) {
        episode = bracketedNumbers.first();
        season = 0;  // Absolute numbering
        qDebug() << "VP_ShowsTMDB: Using bracketed number as last resort:" << filename 
                << "-> Episode" << episode;
        return true;
    }
    
    if (!parenthesizedNumbers.isEmpty()) {
        episode = parenthesizedNumbers.first();
        season = 0;  // Absolute numbering
        qDebug() << "VP_ShowsTMDB: Using parenthesized number as last resort:" << filename 
                << "-> Episode" << episode;
        return true;
    }
    
    qDebug() << "VP_ShowsTMDB: Could not parse episode info from filename:" << filename;
    return false;
}

QByteArray VP_ShowsTMDB::scaleImageToSize(const QByteArray& imageData, int width, int height)
{
    if (imageData.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Empty image data provided for scaling";
        return QByteArray();
    }
    
    QImage image;
    if (!image.loadFromData(imageData)) {
        qDebug() << "VP_ShowsTMDB: Failed to load image from data";
        return QByteArray();
    }
    
    // Scale the image while maintaining aspect ratio
    QImage scaledImage = image.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // Create a square image with padding if needed
    if (scaledImage.width() != width || scaledImage.height() != height) {
        QImage finalImage(width, height, QImage::Format_ARGB32);
        finalImage.fill(Qt::black);  // Black background for padding
        
        // Center the scaled image
        int x = (width - scaledImage.width()) / 2;
        int y = (height - scaledImage.height()) / 2;
        
        QPainter painter(&finalImage);
        painter.drawImage(x, y, scaledImage);
        painter.end();
        
        scaledImage = finalImage;
    }
    
    // Convert back to byte array (JPEG format for smaller size)
    QByteArray scaledData;
    QBuffer buffer(&scaledData);
    buffer.open(QIODevice::WriteOnly);
    scaledImage.save(&buffer, "JPEG", 85);  // 85% quality
    
    qDebug() << "VP_ShowsTMDB: Scaled image from" << imageData.size() 
            << "bytes to" << scaledData.size() << "bytes";
    
    return scaledData;
}

QString VP_ShowsTMDB::generateTempFilePath(const QString& prefix, const QString& extension)
{
    // Create a temp file using the operations_files secure temp file creation
    // This ensures we use Data/username/temp instead of system temp
    // Note: The XXXXXX pattern in QTemporaryFile generates cryptographically random filenames
    QString templateStr = prefix + "_XXXXXX";
    if (!extension.isEmpty() && !extension.startsWith('.')) {
        templateStr += "." + extension;
    } else if (!extension.isEmpty()) {
        templateStr += extension;
    }
    
    auto tempFile = OperationsFiles::createTempFile(templateStr, false);
    if (!tempFile) {
        qDebug() << "VP_ShowsTMDB: Failed to create temp file with template:" << templateStr;
        return QString();
    }
    
    QString tempPath = tempFile->fileName();
    tempFile->close();  // Close but keep the file on disk
    
    qDebug() << "VP_ShowsTMDB: Generated temp file path:" << tempPath;
    return tempPath;
}

QList<VP_ShowsTMDB::EpisodeInfo> VP_ShowsTMDB::getSeasonEpisodes(int tmdbId, int seasonNumber)
{
    QList<EpisodeInfo> episodes;
    
    if (tmdbId <= 0 || seasonNumber < 0) {
        qDebug() << "VP_ShowsTMDB: Invalid parameters for getting season episodes";
        return episodes;
    }
    
    QString endpoint = QString("/tv/%1/season/%2").arg(tmdbId).arg(seasonNumber);
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get season" << seasonNumber << "episodes for show ID" << tmdbId;
        return episodes;
    }
    
    QJsonArray episodesArray = response["episodes"].toArray();
    
    for (const QJsonValue& value : episodesArray) {
        QJsonObject episodeObj = value.toObject();
        
        EpisodeInfo episode;
        episode.episodeName = episodeObj["name"].toString();
        episode.overview = episodeObj["overview"].toString();
        episode.stillPath = episodeObj["still_path"].toString();
        episode.seasonNumber = episodeObj["season_number"].toInt();
        episode.episodeNumber = episodeObj["episode_number"].toInt();
        episode.airDate = episodeObj["air_date"].toString();
        
        episodes.append(episode);
    }
    
    qDebug() << "VP_ShowsTMDB: Retrieved" << episodes.size() << "episodes for season" << seasonNumber;
    return episodes;
}

QList<VP_ShowsTMDB::EpisodeInfo> VP_ShowsTMDB::getShowSpecials(int tmdbId)
{
    // Season 0 contains specials in TMDB
    return getSeasonEpisodes(tmdbId, 0);
}

QList<VP_ShowsTMDB::MovieInfo> VP_ShowsTMDB::getShowMovies(int tmdbId)
{
    QList<MovieInfo> movies;
    
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for getting movies";
        return movies;
    }
    
    // Search for movies related to the show
    // TMDB doesn't have a direct endpoint for TV show movies, 
    // so we use the search endpoint with the show name
    QString showEndpoint = QString("/tv/%1").arg(tmdbId);
    QJsonObject showDetails = makeApiRequest(showEndpoint);
    
    if (showDetails.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get show details for movie search";
        return movies;
    }
    
    QString showName = showDetails["name"].toString();
    if (showName.isEmpty()) {
        return movies;
    }
    
    // Search for movies with the show name
    QString searchEndpoint = QString("/search/movie?query=%1").arg(QUrl::toPercentEncoding(showName));
    QJsonObject searchResults = makeApiRequest(searchEndpoint);
    
    if (searchResults.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to search for movies";
        return movies;
    }
    
    QJsonArray results = searchResults["results"].toArray();
    
    // Filter results to find movies that are likely related to the show
    for (const QJsonValue& value : results) {
        QJsonObject movieObj = value.toObject();
        QString movieTitle = movieObj["title"].toString();
        
        // Check if movie title contains the show name
        if (movieTitle.contains(showName, Qt::CaseInsensitive)) {
            MovieInfo movie;
            movie.tmdbId = movieObj["id"].toInt();
            movie.title = movieTitle;
            movie.overview = movieObj["overview"].toString();
            movie.releaseDate = movieObj["release_date"].toString();
            movie.posterPath = movieObj["poster_path"].toString();
            
            movies.append(movie);
            qDebug() << "VP_ShowsTMDB: Found related movie:" << movie.title;
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Found" << movies.size() << "movies related to show";
    return movies;
}

QStringList VP_ShowsTMDB::getShowMovieTitles(int tmdbId)
{
    QStringList titles;
    QList<MovieInfo> movies = getShowMovies(tmdbId);
    
    for (const MovieInfo& movie : movies) {
        titles.append(movie.title);
    }
    
    return titles;
}

QStringList VP_ShowsTMDB::getShowOvaTitles(int tmdbId)
{
    QStringList titles;
    
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for getting OVA titles";
        return titles;
    }
    
    // Get Season 0 (specials) which often contains OVAs
    QList<EpisodeInfo> specials = getShowSpecials(tmdbId);
    
    for (const EpisodeInfo& special : specials) {
        if (!special.episodeName.isEmpty()) {
            // Check if this special is likely an OVA based on its name
            QString lowerName = special.episodeName.toLower();
            if (lowerName.contains("ova") || lowerName.contains("oad") || 
                lowerName.contains("original") || lowerName.contains("special")) {
                titles.append(special.episodeName);
                qDebug() << "VP_ShowsTMDB: Found OVA/Special title:" << special.episodeName;
            }
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Found" << titles.size() << "OVA/special titles";
    return titles;
}

bool VP_ShowsTMDB::getShowById(int tmdbId, ShowInfo& showInfo)
{
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for getShowById:" << tmdbId;
        return false;
    }
    
    // Get show details directly by ID
    QString endpoint = QString("/tv/%1").arg(tmdbId);
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get show details for ID:" << tmdbId;
        return false;
    }
    
    // Populate ShowInfo from response
    showInfo.tmdbId = response["id"].toInt();
    showInfo.showName = response["name"].toString();
    showInfo.overview = response["overview"].toString();
    showInfo.posterPath = response["poster_path"].toString();
    showInfo.backdropPath = response["backdrop_path"].toString();
    showInfo.firstAirDate = response["first_air_date"].toString();
    
    // Get season information
    QJsonArray seasons = response["seasons"].toArray();
    for (const QJsonValue& season : seasons) {
        QJsonObject seasonObj = season.toObject();
        int seasonNumber = seasonObj["season_number"].toInt();
        if (seasonNumber > 0) {  // Skip season 0 (specials)
            showInfo.seasonNumbers.append(seasonNumber);
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Got show by ID:" << showInfo.showName << "with" << showInfo.seasonNumbers.size() << "seasons";
    return true;
}

QString VP_ShowsTMDB::getShowPosterById(int tmdbId)
{
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for getShowPosterById:" << tmdbId;
        return QString();
    }
    
    // Get show details to extract poster path
    QString endpoint = QString("/tv/%1").arg(tmdbId);
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get show details for poster, ID:" << tmdbId;
        return QString();
    }
    
    QString posterPath = response["poster_path"].toString();
    qDebug() << "VP_ShowsTMDB: Got poster path for show ID" << tmdbId << ":" << posterPath;
    return posterPath;
}

QString VP_ShowsTMDB::getShowDescriptionById(int tmdbId)
{
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for getShowDescriptionById:" << tmdbId;
        return QString();
    }
    
    // Get show details to extract description
    QString endpoint = QString("/tv/%1").arg(tmdbId);
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get show details for description, ID:" << tmdbId;
        return QString();
    }
    
    QString overview = response["overview"].toString();
    qDebug() << "VP_ShowsTMDB: Got description for show ID" << tmdbId;
    return overview;
}

QMap<int, VP_ShowsTMDB::EpisodeMapping> VP_ShowsTMDB::buildEpisodeMap(int tmdbId)
{
    QMap<int, EpisodeMapping> episodeMap;
    
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for building episode map";
        return episodeMap;
    }
    
    // Optimized: Get show details with append_to_response for more data in one call
    // This gets basic show info + season count in a single API call
    QString showEndpoint = QString("/tv/%1").arg(tmdbId);
    QJsonObject showDetails = makeApiRequest(showEndpoint);
    
    if (showDetails.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get show details for building episode map";
        return episodeMap;
    }
    
    QJsonArray seasons = showDetails["seasons"].toArray();
    int absoluteNumber = 1;
    
    // Optimization: Batch season requests efficiently
    // Group seasons to minimize API calls while respecting rate limits
    QList<int> seasonNumbers;
    for (const QJsonValue& seasonValue : seasons) {
        QJsonObject seasonObj = seasonValue.toObject();
        int seasonNumber = seasonObj["season_number"].toInt();
        int episodeCount = seasonObj["episode_count"].toInt();
        
        // Skip season 0 (specials) for absolute numbering
        if (seasonNumber == 0) {
            qDebug() << "VP_ShowsTMDB: Skipping season 0 (specials) for absolute numbering";
            continue;
        }
        
        // Only fetch seasons that have episodes
        if (episodeCount > 0) {
            seasonNumbers.append(seasonNumber);
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Will fetch" << seasonNumbers.size() << "seasons for show ID" << tmdbId;
    
    // Process seasons with optimized rate limiting
    // Strategy: Process in batches with smart delays
    const int BATCH_SIZE = 5;  // Process 5 seasons at a time
    const int BATCH_DELAY = 1500; // 1.5 seconds between batches
    const int REQUEST_DELAY = 100; // 100ms between requests within a batch
    
    for (int i = 0; i < seasonNumbers.size(); i += BATCH_SIZE) {
        // Add delay between batches (except for first batch)
        if (i > 0) {
            QThread::msleep(BATCH_DELAY);
            qDebug() << "VP_ShowsTMDB: Waiting between batches to respect rate limits";
        }
        
        // Process current batch
        int batchEnd = qMin(i + BATCH_SIZE, seasonNumbers.size());
        for (int j = i; j < batchEnd; ++j) {
            int seasonNumber = seasonNumbers[j];
            
            // Small delay between requests in the same batch (except first)
            if (j > i) {
                QThread::msleep(REQUEST_DELAY);
            }
            
            QList<EpisodeInfo> seasonEpisodes = getSeasonEpisodes(tmdbId, seasonNumber);
            
            // Add each episode to the map with its absolute number
            for (const EpisodeInfo& episode : seasonEpisodes) {
                EpisodeMapping mapping(absoluteNumber, 
                                      episode.seasonNumber, 
                                      episode.episodeNumber, 
                                      episode.episodeName);
                mapping.airDate = episode.airDate;  // Store airDate in mapping
                episodeMap[absoluteNumber] = mapping;
                absoluteNumber++;
            }
            
            qDebug() << "VP_ShowsTMDB: Fetched season" << seasonNumber 
                     << "(" << seasonEpisodes.size() << "episodes)";
        }
    }
    
    qDebug() << "VP_ShowsTMDB: Built episode map with" << episodeMap.size() << "episodes for show ID" << tmdbId;
    
    // Log the first few mappings for debugging
    if (!episodeMap.isEmpty()) {
        auto it = episodeMap.begin();
        for (int i = 0; i < 5 && it != episodeMap.end(); ++i, ++it) {
            qDebug() << "VP_ShowsTMDB: Episode" << it.key() 
                     << "-> S" << it.value().season 
                     << "E" << it.value().episode 
                     << ":" << it.value().episodeName
                     << "Air date:" << it.value().airDate;
        }
    }
    
    return episodeMap;
}
