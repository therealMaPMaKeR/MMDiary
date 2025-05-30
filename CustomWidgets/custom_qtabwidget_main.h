#ifndef CUSTOM_QTABWIDGET_MAIN_H
#define CUSTOM_QTABWIDGET_MAIN_H

#include <QWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QSet>

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

signals:
    // Signal to request password validation
    void passwordValidationRequested(int targetTabIndex, int currentIndex);

    // Signal to check for unsaved changes
    void unsavedChangesCheckRequested(int targetTabIndex, int currentIndex);

protected:
    // Override event filter to catch tab bar clicks
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QSet<QString> m_passwordProtectedTabs; // Set of tab object names that require password
    QString m_settingsTabObjectName;       // Settings tab object name (default to "tab_Settings")
};

#endif // CUSTOM_QTABWIDGET_MAIN_H
