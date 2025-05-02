#ifndef CUSTOM_QTABWIDGET_MAIN_H
#define CUSTOM_QTABWIDGET_MAIN_H

#include <QWidget>
#include <QTabWidget>
#include <QTabBar>

class custom_QTabWidget_Main : public QTabWidget
{
    Q_OBJECT
public:
    explicit custom_QTabWidget_Main(QWidget *parent = nullptr);

    // Allow setting whether password validation is required
    void setRequirePasswordForTab(int tabIndex, bool required);

    // Add a method to set the settings tab index
    void setSettingsTabIndex(int tabIndex);

signals:
    // Signal to request password validation
    void passwordValidationRequested(int targetTabIndex, int currentIndex);

    // Signal to check for unsaved changes
    void unsavedChangesCheckRequested(int targetTabIndex, int currentIndex);

protected:
    // Override event filter to catch tab bar clicks
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int m_passwordProtectedTab = 2; // Default to tab index 2 (Passwords)
    bool m_requirePassword = true;  // Whether password validation is required
    int m_settingsTabIndex = 3;     // Settings tab index (default to 3)
};

#endif // CUSTOM_QTABWIDGET_MAIN_H
