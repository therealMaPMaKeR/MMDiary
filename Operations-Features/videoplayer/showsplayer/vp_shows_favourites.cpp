#include "vp_shows_favourites.h"
#include "../../Operations-Global/operations_files.h"
#include "../../Operations-Global/inputvalidation.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

VP_ShowsFavourites::VP_ShowsFavourites(const QString& showFolderPath, 
                                      const QByteArray& encryptionKey,
                                      const QString& username)
    : m_showFolderPath(QDir(showFolderPath).absolutePath())  // Ensure absolute path
    , m_encryptionKey(encryptionKey)
    , m_username(username)
    , m_isDirty(false)
    , m_isLoaded(false)
{
    qDebug() << "VP_ShowsFavourites: ============================================";
    qDebug() << "VP_ShowsFavourites: Initializing favourites manager";
    qDebug() << "VP_ShowsFavourites: Show folder path (original):" << showFolderPath;
    qDebug() << "VP_ShowsFavourites: Show folder path (absolute):" << m_showFolderPath;
    qDebug() << "VP_ShowsFavourites: Username:" << username;
    qDebug() << "VP_ShowsFavourites: Encryption key present:" << !encryptionKey.isEmpty();
    qDebug() << "VP_ShowsFavourites: Encryption key length:" << encryptionKey.length();
    
    // Extract show name and generate obfuscated name
    QString showName = extractShowName();
    m_obfuscatedShowName = generateObfuscatedName(showName);
    
    // Build favourites file path
    QString fileName = QString("%1%2%3")
                      .arg(FAVOURITES_FILENAME_PREFIX)
                      .arg(m_obfuscatedShowName)
                      .arg(FAVOURITES_FILENAME_SUFFIX);
    m_favouritesFilePath = QDir(m_showFolderPath).absoluteFilePath(fileName);
    
    qDebug() << "VP_ShowsFavourites: Show name:" << showName;
    qDebug() << "VP_ShowsFavourites: Obfuscated name:" << m_obfuscatedShowName;
    qDebug() << "VP_ShowsFavourites: Favourites file path:" << m_favouritesFilePath;
}

VP_ShowsFavourites::~VP_ShowsFavourites()
{
    // Save any unsaved changes
    if (m_isDirty) {
        qDebug() << "VP_ShowsFavourites: Saving unsaved changes before destruction";
        saveFavourites();
    }
}

bool VP_ShowsFavourites::loadFavourites()
{
    qDebug() << "VP_ShowsFavourites: Loading favourites from:" << m_favouritesFilePath;
    qDebug() << "VP_ShowsFavourites: Current working directory:" << QDir::currentPath();
    
    // Check if file exists
    if (!QFile::exists(m_favouritesFilePath)) {
        qDebug() << "VP_ShowsFavourites: Favourites file does not exist, starting with empty list";
        m_favouriteEpisodes.clear();
        m_isLoaded = true;
        return true;  // Not an error - just no favourites yet
    }
    
    qDebug() << "VP_ShowsFavourites: Favourites file exists, attempting to read...";
    qDebug() << "VP_ShowsFavourites: File size:" << QFileInfo(m_favouritesFilePath).size() << "bytes";
    
    // Read encrypted file
    QString fileContent;
    if (!OperationsFiles::readEncryptedFile(m_favouritesFilePath, m_encryptionKey, fileContent)) {
        qDebug() << "VP_ShowsFavourites: Failed to read encrypted favourites file";
        qDebug() << "VP_ShowsFavourites: Encryption key length:" << m_encryptionKey.length();
        return false;
    }
    
    qDebug() << "VP_ShowsFavourites: Successfully read encrypted file, content length:" << fileContent.length();
    
    // Parse favourites from string
    if (!favouritesFromString(fileContent)) {
        qDebug() << "VP_ShowsFavourites: Failed to parse favourites data";
        return false;
    }
    
    qDebug() << "VP_ShowsFavourites: Successfully loaded" << m_favouriteEpisodes.size() << "favourite episodes";
    m_isLoaded = true;
    return true;
}

bool VP_ShowsFavourites::saveFavourites()
{
    qDebug() << "VP_ShowsFavourites: Saving favourites to:" << m_favouritesFilePath;
    qDebug() << "VP_ShowsFavourites: Number of favourites to save:" << m_favouriteEpisodes.size();
    
    // Ensure directory exists
    if (!ensureDirectoryExists()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure directory exists";
        return false;
    }
    
    // Convert favourites to string format
    QString content = favouritesToString();
    qDebug() << "VP_ShowsFavourites: Content length:" << content.length();
    
    // Write encrypted file
    if (!OperationsFiles::writeEncryptedFile(m_favouritesFilePath, m_encryptionKey, content)) {
        qDebug() << "VP_ShowsFavourites: Failed to write encrypted favourites file";
        return false;
    }
    
    // Verify file was created
    if (QFile::exists(m_favouritesFilePath)) {
        qDebug() << "VP_ShowsFavourites: File saved successfully, size:" << QFileInfo(m_favouritesFilePath).size() << "bytes";
    } else {
        qDebug() << "VP_ShowsFavourites: WARNING - File not found after write!";
        return false;
    }
    
    m_isDirty = false;
    qDebug() << "VP_ShowsFavourites: Successfully saved favourites";
    return true;
}

bool VP_ShowsFavourites::addEpisodeToFavourites(const QString& episodePath)
{
    qDebug() << "VP_ShowsFavourites: Adding episode to favourites:" << episodePath;
    
    // Validate episode path
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty()) {
        qDebug() << "VP_ShowsFavourites: Invalid episode path:" << episodePath;
        return false;
    }
    
    // Ensure favourites are loaded
    if (!ensureLoaded()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure favourites are loaded";
        return false;
    }
    
    // Check if already favourite
    if (m_favouriteEpisodes.contains(validPath)) {
        qDebug() << "VP_ShowsFavourites: Episode is already in favourites";
        return true;  // Already favourite, no need to add again
    }
    
    // Add to favourites
    m_favouriteEpisodes.append(validPath);
    m_isDirty = true;
    
    qDebug() << "VP_ShowsFavourites: Episode added to favourites. Total favourites:" << m_favouriteEpisodes.size();
    
    // Save immediately to ensure persistence
    return saveFavourites();
}

bool VP_ShowsFavourites::removeEpisodeFromFavourites(const QString& episodePath)
{
    qDebug() << "VP_ShowsFavourites: Removing episode from favourites:" << episodePath;
    
    // Validate episode path
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty()) {
        qDebug() << "VP_ShowsFavourites: Invalid episode path:" << episodePath;
        return false;
    }
    
    // Ensure favourites are loaded
    if (!ensureLoaded()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure favourites are loaded";
        return false;
    }
    
    // Remove from favourites
    int removedCount = m_favouriteEpisodes.removeAll(validPath);
    
    if (removedCount == 0) {
        qDebug() << "VP_ShowsFavourites: Episode was not in favourites";
        return true;  // Not an error - just wasn't favourite
    }
    
    m_isDirty = true;
    qDebug() << "VP_ShowsFavourites: Episode removed from favourites. Total favourites:" << m_favouriteEpisodes.size();
    
    // Save immediately to ensure persistence
    return saveFavourites();
}

bool VP_ShowsFavourites::toggleEpisodeFavourite(const QString& episodePath)
{
    qDebug() << "VP_ShowsFavourites: Toggling favourite status for episode:" << episodePath;
    
    // Check current status
    if (isEpisodeFavourite(episodePath)) {
        // Currently favourite, remove it
        bool success = removeEpisodeFromFavourites(episodePath);
        if (success) {
            qDebug() << "VP_ShowsFavourites: Episode removed from favourites";
            return false; // Now not favourite
        } else {
            qDebug() << "VP_ShowsFavourites: Failed to remove episode from favourites";
            return true; // Still favourite due to failure
        }
    } else {
        // Currently not favourite, add it
        bool success = addEpisodeToFavourites(episodePath);
        if (success) {
            qDebug() << "VP_ShowsFavourites: Episode added to favourites";
            return true; // Now favourite
        } else {
            qDebug() << "VP_ShowsFavourites: Failed to add episode to favourites";
            return false; // Still not favourite due to failure
        }
    }
}

bool VP_ShowsFavourites::isEpisodeFavourite(const QString& episodePath) const
{
    // Validate episode path
    QString validPath = validateEpisodePath(episodePath);
    if (validPath.isEmpty()) {
        return false;
    }
    
    // Ensure favourites are loaded (const_cast for lazy loading)
    if (!const_cast<VP_ShowsFavourites*>(this)->ensureLoaded()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure favourites are loaded during check";
        return false;
    }
    
    return m_favouriteEpisodes.contains(validPath);
}

QStringList VP_ShowsFavourites::getFavouriteEpisodes() const
{
    // Ensure favourites are loaded (const_cast for lazy loading)
    if (!const_cast<VP_ShowsFavourites*>(this)->ensureLoaded()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure favourites are loaded during get";
        return QStringList();
    }
    
    return m_favouriteEpisodes;
}

int VP_ShowsFavourites::getFavouriteCount() const
{
    // Ensure favourites are loaded (const_cast for lazy loading)
    if (!const_cast<VP_ShowsFavourites*>(this)->ensureLoaded()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure favourites are loaded during count";
        return 0;
    }
    
    return m_favouriteEpisodes.size();
}

bool VP_ShowsFavourites::clearAllFavourites()
{
    qDebug() << "VP_ShowsFavourites: Clearing all favourites";
    
    // Ensure favourites are loaded
    if (!ensureLoaded()) {
        qDebug() << "VP_ShowsFavourites: Failed to ensure favourites are loaded";
        return false;
    }
    
    int previousCount = m_favouriteEpisodes.size();
    m_favouriteEpisodes.clear();
    m_isDirty = true;
    
    qDebug() << "VP_ShowsFavourites: Cleared" << previousCount << "favourites";
    
    // Save the cleared state
    return saveFavourites();
}

bool VP_ShowsFavourites::favouritesFileExists() const
{
    return QFile::exists(m_favouritesFilePath);
}

QString VP_ShowsFavourites::getFavouritesFilePath() const
{
    return m_favouritesFilePath;
}

QString VP_ShowsFavourites::generateObfuscatedName(const QString& showName) const
{
    if (showName.isEmpty()) {
        qDebug() << "VP_ShowsFavourites: Empty show name, using fallback";
        return "unknown_show";
    }
    
    // Create a hash of the show name for obfuscation
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(showName.toUtf8());
    hash.addData(m_username.toUtf8());  // Add username for additional uniqueness
    
    // Take first 16 characters of hash for reasonable filename length
    QString hashString = hash.result().toHex().left(16);
    
    qDebug() << "VP_ShowsFavourites: Generated obfuscated name for '" << showName << "':" << hashString;
    return hashString;
}

QString VP_ShowsFavourites::validateEpisodePath(const QString& episodePath) const
{
    if (episodePath.isEmpty()) {
        return QString();
    }
    
    // Remove any leading/trailing whitespace
    QString cleanPath = episodePath.trimmed();
    
    // Ensure path doesn't contain directory traversal
    if (cleanPath.contains("../") || cleanPath.contains("..\\")) {
        qDebug() << "VP_ShowsFavourites: Path contains directory traversal:" << episodePath;
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

bool VP_ShowsFavourites::ensureLoaded()
{
    if (m_isLoaded) {
        return true;  // Already loaded
    }
    
    qDebug() << "VP_ShowsFavourites: Loading favourites on demand";
    return loadFavourites();
}

bool VP_ShowsFavourites::ensureDirectoryExists() const
{
    QFileInfo fileInfo(m_favouritesFilePath);
    QDir parentDir = fileInfo.dir();
    
    if (!parentDir.exists()) {
        qDebug() << "VP_ShowsFavourites: Parent directory does not exist, creating:" << parentDir.path();
        if (!parentDir.mkpath(".")) {
            qDebug() << "VP_ShowsFavourites: Failed to create parent directory";
            return false;
        }
    }
    
    return true;
}

QString VP_ShowsFavourites::extractShowName() const
{
    // Extract show name from folder path
    QFileInfo folderInfo(m_showFolderPath);
    QString folderName = folderInfo.fileName();
    
    // Parse show name from folder format: "ShowName_Language_Translation"
    QStringList parts = folderName.split('_');
    if (parts.size() >= 1) {
        QString showName = parts[0];
        qDebug() << "VP_ShowsFavourites: Extracted show name from folder:" << showName;
        return showName;
    }
    
    qDebug() << "VP_ShowsFavourites: Could not extract show name from folder, using full name:" << folderName;
    return folderName;
}

QString VP_ShowsFavourites::favouritesToString() const
{
    // Simple format: one episode path per line
    return m_favouriteEpisodes.join('\n');
}

bool VP_ShowsFavourites::favouritesFromString(const QString& data)
{
    // Clear current favourites
    m_favouriteEpisodes.clear();
    
    if (data.isEmpty()) {
        qDebug() << "VP_ShowsFavourites: Empty favourites data, starting with empty list";
        return true;
    }
    
    // Split by newlines and validate each path
    QStringList lines = data.split('\n', Qt::SkipEmptyParts);
    int validCount = 0;
    
    for (const QString& line : lines) {
        QString validPath = validateEpisodePath(line);
        if (!validPath.isEmpty()) {
            m_favouriteEpisodes.append(validPath);
            validCount++;
        } else {
            qDebug() << "VP_ShowsFavourites: Skipping invalid episode path:" << line;
        }
    }
    
    qDebug() << "VP_ShowsFavourites: Parsed" << validCount << "valid favourite episodes from" << lines.size() << "lines";
    return true;
}
