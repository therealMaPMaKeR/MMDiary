#include "custom_qtabwidget_main.h"
#include "../Operations-Global/operations.h"
#include <QEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>

custom_QTabWidget_Main::custom_QTabWidget_Main(QWidget *parent)
    : QTabWidget{parent}, m_settingsTabObjectName("tab_Settings"), m_tabVisibilityMenu(nullptr)
{
    // Install event filter on the tab bar
    tabBar()->installEventFilter(this);

    // Initialize tab mappings
    initializeTabMappings();

    // Create the context menu
    createTabVisibilityMenu();
}

void custom_QTabWidget_Main::initializeTabMappings()
{
    // Map tab object names to user-friendly display names
    m_tabObjectNameToDisplayName["tab_Diaries"] = "Diaries";
    m_tabObjectNameToDisplayName["tab_Tasklists"] = "Task Lists";
    m_tabObjectNameToDisplayName["tab_Passwords"] = "Passwords";
    m_tabObjectNameToDisplayName["tab_DataEncryption"] = "Encrypted Data";
    m_tabObjectNameToDisplayName["tab_Settings"] = "Settings";
}

int custom_QTabWidget_Main::countVisibleTabs() const
{
    int visibleCount = 0;
    for (int i = 0; i < count(); ++i) {
        if (isTabVisible(i)) {
            visibleCount++;
        }
    }
    return visibleCount;
}

void custom_QTabWidget_Main::createTabVisibilityMenu()
{
    // Only create the menu if it doesn't exist
    if (!m_tabVisibilityMenu) {
        m_tabVisibilityMenu = new QMenu("Tab Visibility", this);
    }

    // Clear existing actions and menu items
    m_tabVisibilityMenu->clear();
    m_tabVisibilityActions.clear();

    // Count visible tabs first
    int visibleTabCount = countVisibleTabs();

    // Create actions for each tab in their actual display order
    for (int i = 0; i < count(); ++i) {
        QWidget* tabWidget = widget(i);
        if (!tabWidget) {
            continue;
        }

        const QString& objectName = tabWidget->objectName();

        // NEW: Skip settings tab - it should not be hideable
        if (objectName == m_settingsTabObjectName) {
            continue;
        }

        // Get display name from our mapping, or use object name as fallback
        QString displayName = m_tabObjectNameToDisplayName.value(objectName, objectName);

        // Only show tabs in the context menu if:
        // 1. There's more than one visible tab, OR
        // 2. This specific tab is currently hidden (so user can show it)
        bool isCurrentTabVisible = isTabVisible(i);
        bool shouldShowInMenu = (visibleTabCount > 1) || !isCurrentTabVisible;

        if (shouldShowInMenu) {
            QAction* action = new QAction(displayName, this); // Use 'this' as parent, not the menu
            action->setCheckable(true);
            action->setChecked(isCurrentTabVisible); // Set based on current visibility
            action->setData(objectName); // Store object name in action data

            connect(action, &QAction::triggered, this, &custom_QTabWidget_Main::onTabVisibilityToggled);

            m_tabVisibilityMenu->addAction(action);
            m_tabVisibilityActions[objectName] = action;
        }
    }
}

void custom_QTabWidget_Main::showTabVisibilityContextMenu(const QPoint& position)
{
    if (!m_tabVisibilityMenu) {
        return;
    }

    // Recreate menu to ensure correct order after any tab moves and visibility changes
    createTabVisibilityMenu();

    // Update menu states before showing
    updateTabVisibilityMenuStates();

    // Only show the menu if there are actions to show
    if (!m_tabVisibilityMenu->isEmpty()) {
        // Show context menu at the global position
        QPoint globalPos = tabBar()->mapToGlobal(position);
        m_tabVisibilityMenu->exec(globalPos);
    }
}

void custom_QTabWidget_Main::updateTabVisibilityMenuStates()
{
    // Update checkbox states based on current tab visibility
    // Iterate through tabs in actual order to get correct visibility state
    for (int i = 0; i < count(); ++i) {
        QWidget* tabWidget = widget(i);
        if (!tabWidget) {
            continue;
        }

        const QString& objectName = tabWidget->objectName();

        // Update action if it exists (settings tab won't have an action)
        if (m_tabVisibilityActions.contains(objectName)) {
            QAction* action = m_tabVisibilityActions[objectName];
            bool isVisible = isTabVisible(i);
            action->setChecked(isVisible);
        }
    }
}

void custom_QTabWidget_Main::onTabVisibilityToggled()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    QString objectName = action->data().toString();
    bool shouldBeVisible = action->isChecked();

    // SAFEGUARD: Prevent hiding the last visible tab
    if (!shouldBeVisible) {
        int visibleCount = countVisibleTabs();
        if (visibleCount <= 1) {
            // Silently prevent hiding the last tab by unchecking the action
            action->setChecked(true);
            return;
        }

        // NEW: Before hiding any tab, switch to settings tab for security
        int settingsTabIndex = getTabIndexByObjectName(m_settingsTabObjectName);
        if (settingsTabIndex >= 0) {
            // Ensure settings tab is visible first
            setTabVisible(settingsTabIndex, true);
            // Switch to settings tab before hiding the requested tab
            setCurrentIndex(settingsTabIndex);
            qDebug() << "Switched to settings tab before hiding tab:" << objectName;
        }
    }

    // Make the tab visible/hidden
    setTabVisibleByObjectName(objectName, shouldBeVisible);

    // If making a tab visible, automatically switch to it (with validation)
    if (shouldBeVisible) {
        int targetTabIndex = getTabIndexByObjectName(objectName);
        if (targetTabIndex >= 0) {
            attemptTabSwitch(targetTabIndex);
        }
    }
}

void custom_QTabWidget_Main::setTabVisibleByObjectName(const QString& tabObjectName, bool visible)
{
    // NEW: Prevent hiding the settings tab
    if (tabObjectName == m_settingsTabObjectName && !visible) {
        qDebug() << "Attempt to hide settings tab blocked - settings tab cannot be hidden";
        return;
    }

    int tabIndex = getTabIndexByObjectName(tabObjectName);
    if (tabIndex >= 0) {
        setTabVisible(tabIndex, visible);
    }
}

bool custom_QTabWidget_Main::isTabVisibleByObjectName(const QString& tabObjectName) const
{
    int tabIndex = getTabIndexByObjectName(tabObjectName);
    if (tabIndex >= 0) {
        return isTabVisible(tabIndex);
    }
    return false;
}

int custom_QTabWidget_Main::getTabIndexByObjectName(const QString& objectName) const
{
    // Loop through all tabs to find the one with matching object name
    for (int i = 0; i < count(); ++i) {
        QWidget* tabPage = widget(i);
        if (tabPage && tabPage->objectName() == objectName) {
            return i;
        }
    }
    return -1; // Not found
}

QString custom_QTabWidget_Main::getTabObjectNameByIndex(int index) const
{
    if (index >= 0 && index < count()) {
        QWidget* tabPage = widget(index);
        if (tabPage) {
            return tabPage->objectName();
        }
    }
    return QString();
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

// NEW: Method to ensure settings tab is always visible
void custom_QTabWidget_Main::ensureSettingsTabVisible()
{
    int settingsTabIndex = getTabIndexByObjectName(m_settingsTabObjectName);
    if (settingsTabIndex >= 0) {
        setTabVisible(settingsTabIndex, true);
        qDebug() << "Ensured settings tab is visible";
    }
}

bool custom_QTabWidget_Main::eventFilter(QObject *watched, QEvent *event)
{
    // Check if this is a mouse event on the tab bar
    if (watched == tabBar()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

            // Handle right-click for context menu
            if (mouseEvent->button() == Qt::RightButton) {
                showTabVisibilityContextMenu(mouseEvent->pos());
                return true; // Consume the event
            }

            // Handle left-click for tab switching (existing logic)
            if (mouseEvent->button() == Qt::LeftButton) {
                // Get the tab that was clicked
                int clickedTab = tabBar()->tabAt(mouseEvent->pos());
                if (clickedTab == -1) {
                    // Click was not on a tab
                    return QTabWidget::eventFilter(watched, event);
                }

                // Get the current tab
                int currentTab = currentIndex();

                // Don't interfere if clicking the same tab (allows for drag operations)
                if (clickedTab == currentTab) {
                    return QTabWidget::eventFilter(watched, event);
                }

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

                // Check if validation is needed
                bool needsValidation = false;

                // First check: Are we trying to leave the settings tab?
                if (currentTabObjectName == m_settingsTabObjectName && clickedTabObjectName != m_settingsTabObjectName) {
                    needsValidation = true;
                    emit unsavedChangesCheckRequested(clickedTab, currentTab);
                }
                // Second check: Are we trying to access a password-protected tab?
                else if (m_passwordProtectedTabs.contains(clickedTabObjectName)) {
                    needsValidation = true;
                    emit passwordValidationRequested(clickedTab, currentTab);
                }

                // Only consume the event if validation was needed
                if (needsValidation) {
                    return true; // Prevent automatic tab switch, let validation handle it
                }

                // No validation needed, let Qt handle normally (including drag operations)
                return QTabWidget::eventFilter(watched, event);
            }
        }
    }

    // Pass event to parent class
    return QTabWidget::eventFilter(watched, event);
}

void custom_QTabWidget_Main::attemptTabSwitch(int targetTabIndex)
{
    // Get the current tab
    int currentTab = currentIndex();

    // Don't switch if we're already on the target tab
    if (currentTab == targetTabIndex) {
        return;
    }

    // Get tab object names by finding the widget at each index
    QString targetTabObjectName;
    QString currentTabObjectName;

    QWidget* targetWidget = widget(targetTabIndex);
    QWidget* currentWidget = widget(currentTab);

    if (targetWidget) {
        targetTabObjectName = targetWidget->objectName();
    }
    if (currentWidget) {
        currentTabObjectName = currentWidget->objectName();
    }

    // First check: Are we trying to leave the settings tab?
    if (currentTabObjectName == m_settingsTabObjectName && targetTabObjectName != m_settingsTabObjectName) {
        // Emit signal to check for unsaved changes
        emit unsavedChangesCheckRequested(targetTabIndex, currentTab);
        return; // Let the unsaved changes handler deal with the actual tab switch
    }

    // Second check: Are we trying to access a password-protected tab?
    if (m_passwordProtectedTabs.contains(targetTabObjectName)) {
        // Emit signal to request password validation
        emit passwordValidationRequested(targetTabIndex, currentTab);
        return; // Let the password validation handler deal with the actual tab switch
    }

    // No validation needed, switch directly
    setCurrentIndex(targetTabIndex);
}

void custom_QTabWidget_Main::moveTab(int fromIndex, int toIndex) {
    tabBar()->moveTab(fromIndex, toIndex);
}
