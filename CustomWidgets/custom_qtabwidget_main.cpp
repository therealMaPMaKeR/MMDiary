#include "custom_qtabwidget_main.h"
#include "../Operations-Global/operations.h"
#include <QEvent>
#include <QMouseEvent>

custom_QTabWidget_Main::custom_QTabWidget_Main(QWidget *parent)
    : QTabWidget{parent}, m_settingsTabObjectName("tab_Settings")
{
    // Install event filter on the tab bar
    tabBar()->installEventFilter(this);
}

void custom_QTabWidget_Main::setRequirePasswordForTab(const QString& tabObjectName, bool required)
{
    if (required) {
        m_passwordProtectedTabs.insert(tabObjectName);
    } else {
        m_passwordProtectedTabs.remove(tabObjectName);
    }
}

void custom_QTabWidget_Main::setSettingsTabObjectName(const QString& tabObjectName)
{
    m_settingsTabObjectName = tabObjectName;
}

bool custom_QTabWidget_Main::eventFilter(QObject *watched, QEvent *event)
{
    // Check if this is a mouse event on the tab bar
    if (watched == tabBar() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

        // Get the tab that was clicked
        int clickedTab = tabBar()->tabAt(mouseEvent->pos());
        if (clickedTab == -1) {
            // Click was not on a tab
            return QTabWidget::eventFilter(watched, event);
        }

        // Get the current tab
        int currentTab = currentIndex();

        // Get tab object names by finding the widget at each index
        QString clickedTabObjectName;
        QString currentTabObjectName;

        QWidget* clickedWidget = widget(clickedTab);
        QWidget* currentWidget = widget(currentTab);

        if (clickedWidget) {
            clickedTabObjectName = clickedWidget->objectName();
        }
        if (currentWidget) {
            currentTabObjectName = currentWidget->objectName();
        }

        // First check: Are we trying to leave the settings tab?
        if (currentTabObjectName == m_settingsTabObjectName && clickedTabObjectName != m_settingsTabObjectName) {
            // Emit signal to check for unsaved changes
            emit unsavedChangesCheckRequested(clickedTab, currentTab);

            // Consume the event to prevent the automatic tab switch
            return true;
        }

        // Second check: Are we trying to access a password-protected tab?
        if (m_passwordProtectedTabs.contains(clickedTabObjectName)) {
            // Emit signal to request password validation
            emit passwordValidationRequested(clickedTab, currentTab);

            // Consume the event to prevent the automatic tab switch
            return true;
        }
    }

    // Pass event to parent class
    return QTabWidget::eventFilter(watched, event);
}

void custom_QTabWidget_Main::moveTab(int fromIndex, int toIndex) {
    tabBar()->moveTab(fromIndex, toIndex);
}
