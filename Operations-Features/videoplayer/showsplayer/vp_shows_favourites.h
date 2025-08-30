#ifndef VP_SHOWS_FAVOURITES_H
#define VP_SHOWS_FAVOURITES_H

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <memory>

/**
 * @brief TV Show Favourites Manager
 * Handles loading, saving, and managing favourite episodes for TV shows
 * Stores favorites in an encrypted file in the show folder
 */
class VP_ShowsFavourites
{
public:
    // Constants
    static constexpr const char* FAVOURITES_FILENAME_PREFIX = "favourites_";
    static constexpr const char* FAVOURITES_FILENAME_SUFFIX = ".encrypted";
    
    /**
     * @brief Constructor
     * @param showFolderPath Path to the TV show folder
     * @param encryptionKey Encryption key for secure storage
     * @param username Current user's username
     */
    VP_ShowsFavourites(const QString& showFolderPath, 
                      const QByteArray& encryptionKey,
                      const QString& username);
    
    /**
     * @brief Destructor - ensures data is saved if needed
     */
    ~VP_ShowsFavourites();
    
    // Core functionality
    
    /**
     * @brief Load favourites from encrypted file
     * @return true if successfully loaded or file doesn't exist, false if error occurred
     */
    bool loadFavourites();
    
    /**
     * @brief Save favourites to encrypted file
     * @return true if successfully saved, false otherwise
     */
    bool saveFavourites();
    
    /**
     * @brief Add an episode to favourites
     * @param episodePath Relative path to the episode within the show folder
     * @return true if successfully added, false otherwise
     */
    bool addEpisodeToFavourites(const QString& episodePath);
    
    /**
     * @brief Remove an episode from favourites
     * @param episodePath Relative path to the episode within the show folder
     * @return true if successfully removed, false otherwise
     */
    bool removeEpisodeFromFavourites(const QString& episodePath);
    
    /**
     * @brief Toggle favourite status of an episode
     * @param episodePath Relative path to the episode within the show folder
     * @return true if episode is now favourite, false if no longer favourite
     */
    bool toggleEpisodeFavourite(const QString& episodePath);
    
    /**
     * @brief Check if an episode is marked as favourite
     * @param episodePath Relative path to the episode within the show folder
     * @return true if episode is favourite, false otherwise
     */
    bool isEpisodeFavourite(const QString& episodePath) const;
    
    /**
     * @brief Get all favourite episodes
     * @return List of episode paths that are marked as favourites
     */
    QStringList getFavouriteEpisodes() const;
    
    /**
     * @brief Get count of favourite episodes
     * @return Number of episodes marked as favourites
     */
    int getFavouriteCount() const;
    
    /**
     * @brief Clear all favourites
     * @return true if successfully cleared, false otherwise
     */
    bool clearAllFavourites();
    
    /**
     * @brief Check if favourites file exists
     * @return true if file exists, false otherwise
     */
    bool favouritesFileExists() const;
    
    /**
     * @brief Get the path to the favourites file
     * @return Full path to the encrypted favourites file
     */
    QString getFavouritesFilePath() const;

private:
    // Member variables
    QString m_showFolderPath;          // Path to the TV show folder
    QByteArray m_encryptionKey;        // Encryption key for file operations
    QString m_username;                // Current user's username
    QString m_favouritesFilePath;      // Full path to the favourites file
    QString m_obfuscatedShowName;      // Obfuscated show name for filename
    
    // Data storage
    QStringList m_favouriteEpisodes;   // List of favourite episode paths
    
    // State tracking
    bool m_isDirty;                    // Whether data needs to be saved
    bool m_isLoaded;                   // Whether favourites have been loaded
    
    // Helper methods
    
    /**
     * @brief Generate obfuscated filename for the show
     * @param showName Original show name
     * @return Obfuscated name suitable for filename
     */
    QString generateObfuscatedName(const QString& showName) const;
    
    /**
     * @brief Validate and sanitize episode path
     * @param episodePath Path to validate
     * @return Sanitized path, or empty string if invalid
     */
    QString validateEpisodePath(const QString& episodePath) const;
    
    /**
     * @brief Ensure favourites are loaded from file
     * @return true if loaded successfully, false otherwise
     */
    bool ensureLoaded();
    
    /**
     * @brief Create the favourites file directory if it doesn't exist
     * @return true if directory exists or was created successfully
     */
    bool ensureDirectoryExists() const;
    
    /**
     * @brief Extract show name from folder path for obfuscation
     * @return Show name extracted from folder structure
     */
    QString extractShowName() const;
    
    /**
     * @brief Convert favourites list to string format for storage
     * @return String representation of favourites data
     */
    QString favouritesToString() const;
    
    /**
     * @brief Parse favourites from string format
     * @param data String data from file
     * @return true if parsing was successful
     */
    bool favouritesFromString(const QString& data);
};

#endif // VP_SHOWS_FAVOURITES_H
