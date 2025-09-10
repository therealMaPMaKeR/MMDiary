#ifndef QTAB_MAIN_H
#define QTAB_MAIN_H

#include <QWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QSet>
#include <QMenu>
#include <QAction>
#include <QMap>
#include "../Operations-Global/SafeTimer.h"

class qtab_Main : public QTabWidget
{
    Q_OBJECT
public:
    explicit qtab_Main(QWidget *parent = nullptr);
    ~qtab_Main();

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
    
    // SECURITY: Track if validation is in progress to prevent race conditions
    bool isValidationInProgress() const { return m_isValidating; }
    void setValidationInProgress(bool inProgress);

signals:
    // Signal to request password validation
    void passwordValidationRequested(int targetTabIndex, int currentIndex);

    // Signal to check for unsaved changes
    void unsavedChangesCheckRequested(int targetTabIndex, int currentIndex);

public:
    // SECURITY: Intercept setCurrentIndex to validate all tab switches
    // Note: This hides the base class method, doesn't override (not virtual)
    void setCurrentIndex(int index);

protected:
    // Override event filter to catch tab bar clicks
    bool eventFilter(QObject *watched, QEvent *event) override;
    
    // SECURITY: Override keyPressEvent to intercept keyboard shortcuts
    void keyPressEvent(QKeyEvent *event) override;

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
    
    // SECURITY: Validation state tracking
    bool m_isValidating;
    bool m_isInitialized;  // Track if widget is fully initialized
    SafeTimer* m_validationTimer;
    int m_pendingTabIndex;
    
    // SECURITY: Clear grace period for hidden tabs
    void clearGracePeriodForHiddenTab(const QString& tabObjectName);
    
    // SECURITY: Validate programmatic tab switches
    bool validateProgrammaticSwitch(int targetIndex);
    
private slots:
    // SECURITY: Handle validation timeout
    void onValidationTimeout();
};

#endif // QTAB_MAIN_H
