#ifndef CUSTOM_QTABWIDGET_MAIN_H
#define CUSTOM_QTABWIDGET_MAIN_H

#include <QWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QSet>
#include <QMenu>
#include <QAction>
#include <QMap>

class custom_QTabWidget_Main : public QTabWidget
{
    Q_OBJECT
public:
    explicit custom_QTabWidget_Main(QWidget *parent = nullptr);

    // Allow setting whether password validation is required for specific tabs by object name
    void setRequirePasswordForTab(const QString& tabObjectName, bool required);

    // Add a method to set the settings tab object name
    void setSettingsTabObjectName(const QString& tabObjectName);

    void moveTab(int fromIndex, int toIndex);

    // Tab visibility methods
    void setTabVisibleByObjectName(const QString& tabObjectName, bool visible);
    bool isTabVisibleByObjectName(const QString& tabObjectName) const;

    // NEW: Method to ensure settings tab is always visible
    void ensureSettingsTabVisible();

signals:
    // Signal to request password validation
    void passwordValidationRequested(int targetTabIndex, int currentIndex);

    // Signal to check for unsaved changes
    void unsavedChangesCheckRequested(int targetTabIndex, int currentIndex);

protected:
    // Override event filter to catch tab bar clicks
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // Context menu action handlers
    void onTabVisibilityToggled();

private:
    QSet<QString> m_passwordProtectedTabs; // Set of tab object names that require password
    QString m_settingsTabObjectName;       // Settings tab object name (default to "tab_Settings")

    // Context menu functionality
    void showTabVisibilityContextMenu(const QPoint& position);
    void createTabVisibilityMenu();
    void updateTabVisibilityMenuStates();

    // Tab visibility tracking
    QMap<QString, QString> m_tabObjectNameToDisplayName; // Maps object names to user-friendly names
    QMenu* m_tabVisibilityMenu;
    QMap<QString, QAction*> m_tabVisibilityActions; // Maps object names to menu actions

    // Helper methods
    int getTabIndexByObjectName(const QString& objectName) const;
    QString getTabObjectNameByIndex(int index) const;
    void initializeTabMappings();
    void attemptTabSwitch(int targetTabIndex);

    // NEW: Helper method to count visible tabs
    int countVisibleTabs() const;
};

#endif // CUSTOM_QTABWIDGET_MAIN_H
