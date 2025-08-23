#ifndef VP_SHOWS_SETTINGS_DIALOG_H
#define VP_SHOWS_SETTINGS_DIALOG_H

#include <QDialog>
#include <QTimer>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include "vp_shows_tmdb.h"

namespace Ui {
class VP_ShowsSettingsDialog;
}

class VP_ShowsSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent = nullptr);
    ~VP_ShowsSettingsDialog();

private slots:
    // Autofill/Search functionality slots
    void onShowNameTextChanged(const QString& text);
    void onSearchTimerTimeout();
    void onSuggestionItemHovered();
    void onSuggestionItemClicked(QListWidgetItem* item);
    void onImageDownloadFinished(QNetworkReply* reply);
    
    // Other existing slots
    void onLineEditFocusOut();
    void hideSuggestions();

private:
    // UI setup and initialization
    void setupAutofillUI();
    void positionSuggestionsList();
    
    // Search and suggestion handling
    void performTMDBSearch(const QString& searchText);
    void displaySuggestions(const QList<VP_ShowsTMDB::ShowInfo>& shows);
    void clearSuggestions();
    
    // Image handling
    void downloadAndDisplayPoster(const QString& posterPath);
    void displayShowInfo(const VP_ShowsTMDB::ShowInfo& showInfo);
    
    // Event handling
    bool eventFilter(QObject* obj, QEvent* event) override;
    
private:
    Ui::VP_ShowsSettingsDialog *ui;
    QString m_showName;
    QString m_showPath;
    
    // Autofill/Search components
    QListWidget* m_suggestionsList;
    QTimer* m_searchTimer;
    std::unique_ptr<VP_ShowsTMDB> m_tmdbApi;
    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    
    // Current search state
    QString m_currentSearchText;
    QList<VP_ShowsTMDB::ShowInfo> m_currentSuggestions;
    
    // Image cache
    QMap<QString, QPixmap> m_posterCache;
    QString m_currentDownloadingPoster;
    
    // State tracking
    bool m_isShowingSuggestions;
    
    // Constants
    static constexpr int SEARCH_DELAY_MS = 500;  // Debounce delay for search
    static constexpr int MAX_SUGGESTIONS = 8;    // Maximum number of suggestions to show
    static constexpr int SUGGESTION_HEIGHT = 60; // Height of each suggestion item
};

#endif // VP_SHOWS_SETTINGS_DIALOG_H
