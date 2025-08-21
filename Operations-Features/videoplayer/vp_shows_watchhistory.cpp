#include "vp_shows_watchhistory.h"
#include "../../Operations-Global/operations_files.h"
#include "../../Operations-Global/inputvalidation.h"
#include "../../Operations-Global/encryption/CryptoUtils.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>

// EpisodeWatchInfo implementation

QJsonObject EpisodeWatchInfo::toJson() const {
    QJsonObject json;
    json["episodePath"] = episodePath;
    json["episodeIdentifier"] = episodeIdentifier;
    json["lastWatched"] = lastWatched.toString(Qt::ISODate);
    json["lastPosition"] = lastPosition;
    json["totalDuration"] = totalDuration;
    json["completed"] = completed;
    json["watchCount"] = watchCount;
    return json;
}

EpisodeWatchInfo EpisodeWatchInfo::fromJson(const QJsonObject& json) {
    EpisodeWatchInfo info;
    info.episodePath = json["episodePath"].toString();
    info.episodeIdentifier = json["episodeIdentifier"].toString();
    info.lastWatched = QDateTime::fromString(json["lastWatched"].toString(), Qt::ISODate);
    info.lastPosition = json["lastPosition"].toInteger();
    info.totalDuration = json["totalDuration"].toInteger();
    info.completed = json["completed"].toBool();
    info.watchCount = json["watchCount"].toInt();
    return info;
}

// TVShowSettings implementation

QJsonObject TVShowSettings::toJson() const {
    QJsonObject json;
    json["autoplayEnabled"] = autoplayEnabled;
    json["skipIntroEnabled"] = skipIntroEnabled;
    json["skipOutroEnabled"] = skipOutroEnabled;
    json["introSkipSeconds"] = introSkipSeconds;
    json["outroSkipSeconds"] = outroSkipSeconds;
    json["preferredLanguage"] = preferredLanguage;
    json["preferredTranslation"] = preferredTranslation;
    return json;
}

TVShowSettings TVShowSettings::fromJson(const QJsonObject& json) {
    TVShowSettings settings;
    settings.autoplayEnabled = json["autoplayEnabled"].toBool(true);
    settings.skipIntroEnabled = json["skipIntroEnabled"].toBool(false);
    settings.skipOutroEnabled = json["skipOutroEnabled"].toBool(false);
    settings.introSkipSeconds = json["introSkipSeconds"].toInt(0);
    settings.outroSkipSeconds = json["outroSkipSeconds"].toInt(0);
    settings.preferredLanguage = json["preferredLanguage"].toString("English");
    settings.preferredTranslation = json["preferredTranslation"].toString("Subbed");
    return settings;
}

// TVShowWatchData implementation

QJsonObject TVShowWatchData::toJson() const {
    QJsonObject json;
    json["showName"] = showName;
    json["lastWatchedEpisode"] = lastWatchedEpisode;
    json["lastWatchedTime"] = lastWatchedTime.toString(Qt::ISODate);
    json["settings"] = settings.toJson();
    
    // Convert watch history map to JSON object
    QJsonObject historyJson;
    for (auto it = watchHistory.begin(); it != watchHistory.end(); ++it) {
        historyJson[it.key()] = it.value().toJson();
    }
    json["watchHistory"] = historyJson;
    
    return json;
}

TVShowWatchData TVShowWatchData::fromJson(const QJsonObject& json) {
    TVShowWatchData data;
    data.showName = json["showName"].toString();
    data.lastWatchedEpisode = json["lastWatchedEpisode"].toString();
    data.lastWatchedTime = QDateTime::fromString(json["lastWatchedTime"].toString(), Qt::ISODate);
    data.settings = TVShowSettings::fromJson(json["settings"].toObject());
    
    // Convert JSON object to watch history map
    QJsonObject historyJson = json["watchHistory"].toObject();
    for (auto it = historyJson.begin(); it != historyJson.end(); ++it) {
        data.watchHistory[it.key()] = EpisodeWatchInfo::fromJson(it.value().toObject());
    }
    
    return data;
}

// VP_ShowsWatchHistory implementation

VP_ShowsWatchHistory::VP_ShowsWatchHistory(const QString& showFolderPath, 
                                         const QByteArray& encryptionKey,
                                         const QString& username)
    : m_showFolderPath(showFolderPath)
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_historyFilePath(showFolderPath + "/" + HISTORY_FILENAME)
    , m_watchData(std::make_unique<TVShowWatchData>())
    , m_isDirty(false)
{
    qDebug() << "VP_ShowsWatchHistory: Initializing watch history for show folder:" << showFolderPath;
    
    // Extract show name from folder path
    QFileInfo folderInfo(showFolderPath);
    QString folderName = folderInfo.fileName();
    
    // Parse show name from folder format: "ShowName_Language_Translation"
    QStringList parts = folderName.split('_');
    if (parts.size() >= 1) {
        m_watchData->showName = parts[0];
    }
    
    // Try to load existing history
    if (!loadHistory()) {
        qDebug() << "VP_ShowsWatchHistory: No existing history found, creating new data";
        initializeEmptyData();
    }
}

VP_ShowsWatchHistory::~VP_ShowsWatchHistory() {
    // Save any unsaved changes
    if (m_isDirty) {
        qDebug() << "VP_ShowsWatchHistory: Saving unsaved changes before destruction";
        saveHistory();
    }
}

bool VP_ShowsWatchHistory::loadHistory() {
    qDebug() << "VP_ShowsWatchHistory: Loading history from:" << m_historyFilePath;
    
    // Check if file exists
    if (!QFile::exists(m_historyFilePath)) {
        qDebug() << "VP_ShowsWatchHistory: History file does not exist";
        return false;
    }
    
    // Read encrypted file
    QString jsonContent;
    if (!OperationsFiles::readEncryptedFile(m_historyFilePath, m_encryptionKey, jsonContent)) {
        qDebug() << "VP_ShowsWatchHistory: Failed to read encrypted history file";
        return false;
    }
    
    // Parse JSON
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        qDebug() << "VP_ShowsWatchHistory: Invalid JSON in history file";
        return false;
    }
    
    // Load watch data
    try {
        *m_watchData = TVShowWatchData::fromJson(doc.object());
        qDebug() << "VP_ShowsWatchHistory: Successfully loaded history with" 
                 << m_watchData->watchHistory.size() << "episodes";
        return true;
    } catch (const std::exception& e) {
        qDebug() << "VP_ShowsWatchHistory: Exception loading history:" << e.what();
        return false;
    }
}

bool VP_ShowsWatchHistory::saveHistory() {
    qDebug() << "VP_ShowsWatchHistory: Saving history to:" << m_historyFilePath;
    
    // Convert watch data to JSON
    QJsonObject json = m_watchData->toJson();
    QJsonDocument doc(json);
    QString jsonContent = doc.toJson(QJsonDocument::Indented);
    
    // Validate JSON content is not empty
    if (jsonContent.isEmpty()) {
        qDebug() << "VP_ShowsWatchHistory: Empty JSON content, not saving";
        return false;
    }
    
    // Write encrypted file
    if (!OperationsFiles::writeEncryptedFile(m_historyFilePath, m_encryptionKey, jsonContent)) {
        qDebug() << "VP_ShowsWatchHistory: Failed to write encrypted history file";
        return false;
    }
    
    m_isDirty = false;
    qDebug() << "VP_ShowsWatchHistory: Successfully saved history";
    return true;
}

bool VP_ShowsWatchHistory::clearHistory() {
    qDebug() << "VP_ShowsWatchHistory: Clearing all watch history";
    
    // Reset watch data
    initializeEmptyData();
    m_isDirty = true;
    
    // Save the cleared data
    return saveHistory();
}

void VP_ShowsWatchHistory::updateWatchProgress(const QString& episodePath, 
                                              qint64 position, 
                                              qint64 duration,
                                              const QString& episodeIdentifier) {
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty()) {
        qDebug() << "VP_ShowsWatchHistory: Invalid episode path:" << episodePath;
        return;
    }
    
    qDebug() << "VP_ShowsWatchHistory: Updating progress for" << validPath 
             << "- Position:" << position << "Duration:" << duration;
    
    // Get or create episode info
    EpisodeWatchInfo& info = m_watchData->watchHistory[validPath];
    
    // Update info
    info.episodePath = validPath;
    info.lastPosition = position;
    info.totalDuration = duration;
    info.lastWatched = QDateTime::currentDateTime();
    
    // Set episode identifier
    if (!episodeIdentifier.isEmpty()) {
        info.episodeIdentifier = episodeIdentifier;
    } else if (info.episodeIdentifier.isEmpty()) {
        info.episodeIdentifier = parseEpisodeIdentifier(validPath);
    }
    
    // Increment watch count if this is the first update for this session
    if (info.watchCount == 0) {
        info.watchCount = 1;
    }
    
    // Check if episode is completed
    if (isNearEnd(position, duration)) {
        info.completed = true;
        qDebug() << "VP_ShowsWatchHistory: Episode marked as completed";
    }
    
    // Update show-level info
    m_watchData->lastWatchedEpisode = validPath;
    m_watchData->lastWatchedTime = QDateTime::currentDateTime();
    
    m_isDirty = true;
}

void VP_ShowsWatchHistory::markEpisodeCompleted(const QString& episodePath) {
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty()) {
        qDebug() << "VP_ShowsWatchHistory: Invalid episode path:" << episodePath;
        return;
    }
    
    qDebug() << "VP_ShowsWatchHistory: Marking episode as completed:" << validPath;
    
    EpisodeWatchInfo& info = m_watchData->watchHistory[validPath];
    info.completed = true;
    info.lastWatched = QDateTime::currentDateTime();
    
    m_isDirty = true;
}

EpisodeWatchInfo VP_ShowsWatchHistory::getEpisodeWatchInfo(const QString& episodePath) const {
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty() || !m_watchData->watchHistory.contains(validPath)) {
        return EpisodeWatchInfo();
    }
    
    return m_watchData->watchHistory[validPath];
}

bool VP_ShowsWatchHistory::hasEpisodeBeenWatched(const QString& episodePath) const {
    QString validPath = validateEpisodePath(episodePath);
    return !validPath.isEmpty() && m_watchData->watchHistory.contains(validPath);
}

bool VP_ShowsWatchHistory::isEpisodeCompleted(const QString& episodePath) const {
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty() || !m_watchData->watchHistory.contains(validPath)) {
        return false;
    }
    
    return m_watchData->watchHistory[validPath].completed;
}

QString VP_ShowsWatchHistory::getLastWatchedEpisode() const {
    return m_watchData->lastWatchedEpisode;
}

QString VP_ShowsWatchHistory::getNextUnwatchedEpisode(const QString& currentEpisodePath,
                                                     const QStringList& availableEpisodes) const {
    qDebug() << "VP_ShowsWatchHistory: Finding next unwatched episode after:" << currentEpisodePath;
    
    // Find current episode index
    int currentIndex = availableEpisodes.indexOf(currentEpisodePath);
    if (currentIndex == -1) {
        qDebug() << "VP_ShowsWatchHistory: Current episode not found in available episodes";
        // If current episode not found, start from beginning
        currentIndex = -1;
    }
    
    // Look for next unwatched episode
    for (int i = currentIndex + 1; i < availableEpisodes.size(); ++i) {
        const QString& episodePath = availableEpisodes[i];
        if (!isEpisodeCompleted(episodePath)) {
            qDebug() << "VP_ShowsWatchHistory: Found next unwatched episode:" << episodePath;
            return episodePath;
        }
    }
    
    qDebug() << "VP_ShowsWatchHistory: No unwatched episodes found after current";
    return QString();
}

qint64 VP_ShowsWatchHistory::getResumePosition(const QString& episodePath) const {
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty() || !m_watchData->watchHistory.contains(validPath)) {
        return 0;
    }
    
    const EpisodeWatchInfo& info = m_watchData->watchHistory[validPath];
    
    // If episode is completed or near end, start from beginning
    if (info.completed || isNearEnd(info.lastPosition, info.totalDuration)) {
        qDebug() << "VP_ShowsWatchHistory: Episode completed or near end, starting from beginning";
        return 0;
    }
    
    qDebug() << "VP_ShowsWatchHistory: Resume position for" << validPath << "is" << info.lastPosition;
    return info.lastPosition;
}

TVShowSettings VP_ShowsWatchHistory::getSettings() const {
    return m_watchData->settings;
}

void VP_ShowsWatchHistory::updateSettings(const TVShowSettings& settings) {
    qDebug() << "VP_ShowsWatchHistory: Updating show settings";
    m_watchData->settings = settings;
    m_isDirty = true;
}

bool VP_ShowsWatchHistory::isAutoplayEnabled() const {
    return m_watchData->settings.autoplayEnabled;
}

void VP_ShowsWatchHistory::setAutoplayEnabled(bool enabled) {
    qDebug() << "VP_ShowsWatchHistory: Setting autoplay to:" << enabled;
    m_watchData->settings.autoplayEnabled = enabled;
    m_isDirty = true;
}

QString VP_ShowsWatchHistory::getShowName() const {
    return m_watchData->showName;
}

void VP_ShowsWatchHistory::setShowName(const QString& showName) {
    m_watchData->showName = showName;
    m_isDirty = true;
}

qint64 VP_ShowsWatchHistory::getTotalWatchTime() const {
    qint64 totalTime = 0;
    for (const auto& info : m_watchData->watchHistory) {
        totalTime += info.lastPosition;
    }
    return totalTime;
}

int VP_ShowsWatchHistory::getWatchedEpisodeCount() const {
    return m_watchData->watchHistory.size();
}

int VP_ShowsWatchHistory::getCompletedEpisodeCount() const {
    int count = 0;
    for (const auto& info : m_watchData->watchHistory) {
        if (info.completed) {
            count++;
        }
    }
    return count;
}

void VP_ShowsWatchHistory::initializeEmptyData() {
    m_watchData = std::make_unique<TVShowWatchData>();
    
    // Set show name from folder
    QFileInfo folderInfo(m_showFolderPath);
    QString folderName = folderInfo.fileName();
    QStringList parts = folderName.split('_');
    if (parts.size() >= 1) {
        m_watchData->showName = parts[0];
    }
    
    // Initialize with default settings
    m_watchData->settings = TVShowSettings();
}

QString VP_ShowsWatchHistory::validateEpisodePath(const QString& episodePath) const {
    if (episodePath.isEmpty()) {
        return QString();
    }
    
    // Remove any leading/trailing whitespace
    QString cleanPath = episodePath.trimmed();
    
    // Ensure path doesn't contain directory traversal
    if (cleanPath.contains("../") || cleanPath.contains("..\\")) {
        qDebug() << "VP_ShowsWatchHistory: Path contains directory traversal:" << episodePath;
        return QString();
    }
    
    // Normalize path separators
    cleanPath = cleanPath.replace('\\', '/');
    
    // Remove leading slash if present
    if (cleanPath.startsWith('/')) {
        cleanPath = cleanPath.mid(1);
    }
    
    return cleanPath;
}

bool VP_ShowsWatchHistory::isNearEnd(qint64 position, qint64 duration) const {
    if (duration <= 0) {
        return false;
    }
    
    qint64 remaining = duration - position;
    return remaining <= COMPLETION_THRESHOLD_MS;
}

QString VP_ShowsWatchHistory::parseEpisodeIdentifier(const QString& episodePath) const {
    // Extract filename from path
    QFileInfo fileInfo(episodePath);
    QString filename = fileInfo.fileName();
    
    // Try to match common episode patterns
    // Pattern 1: SxxExx or sxxexx
    QRegularExpression pattern1("S(\\d+)E(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match1 = pattern1.match(filename);
    if (match1.hasMatch()) {
        int season = match1.captured(1).toInt();
        int episode = match1.captured(2).toInt();
        return QString("S%1E%2").arg(season, 2, 10, QChar('0')).arg(episode, 2, 10, QChar('0'));
    }
    
    // Pattern 2: Season x Episode x
    QRegularExpression pattern2("Season\\s*(\\d+).*Episode\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match2 = pattern2.match(filename);
    if (match2.hasMatch()) {
        int season = match2.captured(1).toInt();
        int episode = match2.captured(2).toInt();
        return QString("S%1E%2").arg(season, 2, 10, QChar('0')).arg(episode, 2, 10, QChar('0'));
    }
    
    // Pattern 3: Episode number only (xxx or Exxx)
    QRegularExpression pattern3("E?(\\d{2,3})", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match3 = pattern3.match(filename);
    if (match3.hasMatch()) {
        int episode = match3.captured(1).toInt();
        return QString("E%1").arg(episode, 3, 10, QChar('0'));
    }
    
    // If no pattern matches, return empty
    return QString();
}
