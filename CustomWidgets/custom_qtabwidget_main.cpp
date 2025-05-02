#include "custom_qtabwidget_main.h"
#include <QEvent>
#include <QMouseEvent>

custom_QTabWidget_Main::custom_QTabWidget_Main(QWidget *parent)
    : QTabWidget{parent}
{
    // Install event filter on the tab bar
    tabBar()->installEventFilter(this);
}

void custom_QTabWidget_Main::setRequirePasswordForTab(int tabIndex, bool required)
{
    m_passwordProtectedTab = tabIndex;
    m_requirePassword = required;
}

void custom_QTabWidget_Main::setSettingsTabIndex(int tabIndex)
{
    m_settingsTabIndex = tabIndex;
}

bool custom_QTabWidget_Main::eventFilter(QObject *watched, QEvent *event)
{
    // Check if this is a mouse event on the tab bar
    if (watched == tabBar() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

        // Get the tab that was clicked
        int clickedTab = tabBar()->tabAt(mouseEvent->pos());

        // Get the current tab
        int currentTab = currentIndex();

        // First check: Are we trying to leave the settings tab?
        if (currentTab == m_settingsTabIndex && clickedTab != m_settingsTabIndex) {
            // Emit signal to check for unsaved changes
            emit unsavedChangesCheckRequested(clickedTab, currentTab);

            // Consume the event to prevent the automatic tab switch
            return true;
        }

        // Second check: Are we trying to access the password tab?
        if (clickedTab == m_passwordProtectedTab && m_requirePassword) {
            // Emit signal to request password validation
            emit passwordValidationRequested(clickedTab, currentTab);

            // Consume the event to prevent the automatic tab switch
            return true;
        }
    }

    // Pass event to parent class
    return QTabWidget::eventFilter(watched, event);
}
