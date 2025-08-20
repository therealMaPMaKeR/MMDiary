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
            qDebug() << "VP_ShowsTMDB: Rate limit exceeded. Please wait before making more requests.";
        } else if (httpStatusCode == 401) {
            qDebug() << "VP_ShowsTMDB: Authentication failed. Please check your API key.";
        } else {
            qDebug() << "VP_ShowsTMDB: Network error (" << httpStatusCode << "):" << reply->errorString();
        }
        
        reply->deleteLater();
        return QJsonObject();
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "VP_ShowsTMDB: Invalid JSON response";
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

bool VP_ShowsTMDB::getEpisodeInfo(int tmdbId, int season, int episode, EpisodeInfo& episodeInfo)
{
    if (tmdbId <= 0 || season <= 0 || episode <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid parameters for episode info";
        return false;
    }
    
    QString endpoint = QString("/tv/%1/season/%2/episode/%3")
                      .arg(tmdbId)
                      .arg(season)
                      .arg(episode);
    
    QJsonObject response = makeApiRequest(endpoint);
    
    if (response.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get episode info for S" << season << "E" << episode;
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
            OperationsFiles::secureDelete(actualTempPath, 1, false);
        }
        return false;
    }
    
    qint64 written = tempFile.write(imageData);
    tempFile.close();
    
    if (written != imageData.size()) {
        qDebug() << "VP_ShowsTMDB: Failed to write complete image data";
        if (!tempFilePath.isEmpty() && tempFilePtr) {
            // If we created the temp file, clean it up
            OperationsFiles::secureDelete(actualTempPath, 1, false);
        }
        return false;
    }
    
    // If a specific temp path was requested and we created our own temp file,
    // move the file to the requested location
    if (!tempFilePath.isEmpty() && tempFilePtr) {
        QFile::remove(tempFilePath);  // Remove target if it exists
        if (!QFile::rename(actualTempPath, tempFilePath)) {
            qDebug() << "VP_ShowsTMDB: Failed to move temp file to requested location";
            OperationsFiles::secureDelete(actualTempPath, 1, false);
            return false;
        }
        qDebug() << "VP_ShowsTMDB: Successfully downloaded image to:" << tempFilePath;
    } else {
        qDebug() << "VP_ShowsTMDB: Successfully downloaded image to:" << actualTempPath;
    }
    
    return true;
}

bool VP_ShowsTMDB::parseEpisodeFromFilename(const QString& filename, int& season, int& episode)
{
    season = 0;
    episode = 0;
    
    if (filename.isEmpty()) {
        return false;
    }
    
    // First try patterns with both season and episode
    QList<QRegularExpression> seasonPatterns = {
        // S##E## or s##e## (most common)
        QRegularExpression("S(\\d{1,2})E(\\d{1,3})", QRegularExpression::CaseInsensitiveOption),
        
        // ##x## format
        QRegularExpression("(\\d{1,2})x(\\d{1,3})", QRegularExpression::CaseInsensitiveOption),
        
        // S## E## with space
        QRegularExpression("S(\\d{1,2})\\s+E(\\d{1,3})", QRegularExpression::CaseInsensitiveOption),
        
        // Season # Episode #
        QRegularExpression("Season\\s+(\\d{1,2})\\s+Episode\\s+(\\d{1,3})", QRegularExpression::CaseInsensitiveOption),
        
        // [#x##] format
        QRegularExpression("\\[(\\d{1,2})x(\\d{1,3})\\]", QRegularExpression::CaseInsensitiveOption),
        
        // S##.E## with dot
        QRegularExpression("S(\\d{1,2})\\.E(\\d{1,3})", QRegularExpression::CaseInsensitiveOption),
        
        // ### format (1st digit = season, last 2 = episode, e.g., 101 = S01E01)
        QRegularExpression("(?:^|[^\\d])(\\d)(\\d{2})(?:[^\\d]|$)"),
        
        // #### format (first 2 digits = season, last 2 = episode, e.g., 0101 = S01E01)
        QRegularExpression("(?:^|[^\\d])(\\d{2})(\\d{2})(?:[^\\d]|$)")
    };
    
    // Try season patterns first
    for (const auto& pattern : seasonPatterns) {
        QRegularExpressionMatch match = pattern.match(filename);
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
    
    // If no season pattern matched, try absolute numbering patterns
    QList<QRegularExpression> absolutePatterns = {
        // E### or Ep### or Episode ### (common for anime)
        QRegularExpression("(?:E|Ep|Episode)\\s*(\\d{1,4})", QRegularExpression::CaseInsensitiveOption),
        
        // #### format at the beginning or surrounded by non-digits (e.g., "001.mkv" or "Show_001")
        QRegularExpression("(?:^|[^\\d])(\\d{3,4})(?:[^\\d]|$)"),
        
        // - ### - format (common in anime releases)
        QRegularExpression("\\s-\\s*(\\d{1,4})\\s*-\\s"),
        
        // [###] format
        QRegularExpression("\\[(\\d{1,4})\\]"),
        
        // (#) or (###) format
        QRegularExpression("\\((\\d{1,4})\\)")
    };
    
    for (const auto& pattern : absolutePatterns) {
        QRegularExpressionMatch match = pattern.match(filename);
        if (match.hasMatch()) {
            episode = match.captured(1).toInt();
            
            // For absolute numbering, set season to 0
            if (episode > 0 && episode <= 9999) {
                season = 0;  // Indicates absolute numbering
                qDebug() << "VP_ShowsTMDB: Parsed absolute episode from filename:" << filename 
                        << "-> Episode" << episode;
                return true;
            }
        }
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

QMap<int, VP_ShowsTMDB::EpisodeMapping> VP_ShowsTMDB::buildEpisodeMap(int tmdbId)
{
    QMap<int, EpisodeMapping> episodeMap;
    
    if (tmdbId <= 0) {
        qDebug() << "VP_ShowsTMDB: Invalid TMDB ID for building episode map";
        return episodeMap;
    }
    
    // First, get show details to get list of seasons
    QString showEndpoint = QString("/tv/%1").arg(tmdbId);
    QJsonObject showDetails = makeApiRequest(showEndpoint);
    
    if (showDetails.isEmpty()) {
        qDebug() << "VP_ShowsTMDB: Failed to get show details for building episode map";
        return episodeMap;
    }
    
    QJsonArray seasons = showDetails["seasons"].toArray();
    int absoluteNumber = 1;
    
    // Process each season
    for (const QJsonValue& seasonValue : seasons) {
        QJsonObject seasonObj = seasonValue.toObject();
        int seasonNumber = seasonObj["season_number"].toInt();
        
        // Skip season 0 (specials) for absolute numbering
        if (seasonNumber == 0) {
            qDebug() << "VP_ShowsTMDB: Skipping season 0 (specials) for absolute numbering";
            continue;
        }
        
        // Get all episodes for this season
        // Add a small delay to avoid rate limiting (TMDB allows 40 requests/10 seconds)
        if (seasonNumber > 1) {
            QThread::msleep(250);  // 250ms delay between season fetches
        }
        QList<EpisodeInfo> seasonEpisodes = getSeasonEpisodes(tmdbId, seasonNumber);
        
        // Add each episode to the map with its absolute number
        for (const EpisodeInfo& episode : seasonEpisodes) {
            EpisodeMapping mapping(absoluteNumber, 
                                  episode.seasonNumber, 
                                  episode.episodeNumber, 
                                  episode.episodeName);
            episodeMap[absoluteNumber] = mapping;
            absoluteNumber++;
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
                     << ":" << it.value().episodeName;
        }
    }
    
    return episodeMap;
}
