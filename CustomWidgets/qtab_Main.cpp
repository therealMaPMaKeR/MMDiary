#include "qtab_Main.h"
#include "../Operations-Global/operations.h"
#include <QEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QDebug>

qtab_Main::qtab_Main(QWidget *parent)
    : QTabWidget{parent}, m_settingsTabObjectName("tab_Settings"), m_tabVisibilityMenu(nullptr)
{
    qDebug() << "qtab_Main: Constructor called";
    // Install event filter on the tab bar
    tabBar()->installEventFilter(this);

    // Initialize tab mappings
    initializeTabMappings();

    // Create the context menu
    createTabVisibilityMenu();
}

void qtab_Main::initializeTabMappings()
{
    qDebug() << "qtab_Main: initializeTabMappings called";
    // Map tab object names to user-friendly display names
    m_tabObjectNameToDisplayName["tab_Diaries"] = "Diaries";
    m_tabObjectNameToDisplayName["tab_Tasklists"] = "Task Lists";
    m_tabObjectNameToDisplayName["tab_Passwords"] = "Passwords";
    m_tabObjectNameToDisplayName["tab_DataEncryption"] = "Encrypted Data";
    m_tabObjectNameToDisplayName["tab_Settings"] = "Settings";
    m_tabObjectNameToDisplayName["tab_VideoPlayer"] = "Video Player";
}

int qtab_Main::countVisibleTabs() const
{
    int visibleCount = 0;
    for (int i = 0; i < count(); ++i) {
        if (isTabVisible(i)) {
            visibleCount++;
        }
    }
    qDebug() << "qtab_Main: countVisibleTabs returning:" << visibleCount;
    return visibleCount;
}

void qtab_Main::createTabVisibilityMenu()
{
    qDebug() << "qtab_Main: createTabVisibilityMenu called";
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

            connect(action, &QAction::triggered, this, &qtab_Main::onTabVisibilityToggled);

            m_tabVisibilityMenu->addAction(action);
            m_tabVisibilityActions[objectName] = action;
        }
    }
}

void qtab_Main::showTabVisibilityContextMenu(const QPoint& position)
{
    qDebug() << "qtab_Main: showTabVisibilityContextMenu called";
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

void qtab_Main::updateTabVisibilityMenuStates()
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

void qtab_Main::onTabVisibilityToggled()
{
    qDebug() << "qtab_Main: onTabVisibilityToggled called";
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    QString objectName = action->data().toString();
    bool shouldBeVisible = action->isChecked();

    qDebug() << "qtab_Main: Toggling visibility for tab:" << objectName << "to:" << shouldBeVisible;

    // SAFEGUARD: Prevent hiding the last visible tab
    if (!shouldBeVisible) {
        int visibleCount = countVisibleTabs();
        if (visibleCount <= 1) {
            // Silently prevent hiding the last tab by unchecking the action
            action->setChecked(true);
            qDebug() << "qtab_Main: Prevented hiding last visible tab";
            return;
        }

        // NEW: Before hiding any tab, switch to settings tab for security
        int settingsTabIndex = getTabIndexByObjectName(m_settingsTabObjectName);
        if (settingsTabIndex >= 0) {
            // Ensure settings tab is visible first
            setTabVisible(settingsTabIndex, true);
            // Switch to settings tab before hiding the requested tab
            setCurrentIndex(settingsTabIndex);
            qDebug() << "qtab_Main: Switched to settings tab before hiding tab:" << objectName;
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

void qtab_Main::setTabVisibleByObjectName(const QString& tabObjectName, bool visible)
{
    qDebug() << "qtab_Main: setTabVisibleByObjectName called for:" << tabObjectName << "visible:" << visible;
    // NEW: Prevent hiding the settings tab
    if (tabObjectName == m_settingsTabObjectName && !visible) {
        qDebug() << "qtab_Main: Attempt to hide settings tab blocked - settings tab cannot be hidden";
        return;
    }

    int tabIndex = getTabIndexByObjectName(tabObjectName);
    if (tabIndex >= 0) {
        setTabVisible(tabIndex, visible);
    }
}

bool qtab_Main::isTabVisibleByObjectName(const QString& tabObjectName) const
{
    int tabIndex = getTabIndexByObjectName(tabObjectName);
    if (tabIndex >= 0) {
        return isTabVisible(tabIndex);
    }
    return false;
}

int qtab_Main::getTabIndexByObjectName(const QString& objectName) const
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

QString qtab_Main::getTabObjectNameByIndex(int index) const
{
    if (index >= 0 && index < count()) {
        QWidget* tabPage = widget(index);
        if (tabPage) {
            return tabPage->objectName();
        }
    }
    return QString();
}

void qtab_Main::setRequirePasswordForTab(const QString& tabObjectName, bool required)
{
    qDebug() << "qtab_Main: setRequirePasswordForTab called for:" << tabObjectName << "required:" << required;
    if (required) {
        m_passwordProtectedTabs.insert(tabObjectName);
    } else {
        m_passwordProtectedTabs.remove(tabObjectName);
    }
}

void qtab_Main::setSettingsTabObjectName(const QString& tabObjectName)
{
    qDebug() << "qtab_Main: setSettingsTabObjectName called with:" << tabObjectName;
    m_settingsTabObjectName = tabObjectName;
}

// NEW: Method to ensure settings tab is always visible
void qtab_Main::ensureSettingsTabVisible()
{
    qDebug() << "qtab_Main: ensureSettingsTabVisible called";
    int settingsTabIndex = getTabIndexByObjectName(m_settingsTabObjectName);
    if (settingsTabIndex >= 0) {
        setTabVisible(settingsTabIndex, true);
        qDebug() << "qtab_Main: Ensured settings tab is visible";
    }
}

bool qtab_Main::eventFilter(QObject *watched, QEvent *event)
{
    // Check if this is a mouse event on the tab bar
    if (watched == tabBar()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

            // Handle right-click for context menu
            if (mouseEvent->button() == Qt::RightButton) {
                qDebug() << "qtab_Main: Right-click detected on tab bar";
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

                qDebug() << "qtab_Main: Tab click from" << currentTab << "to" << clickedTab;

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
                    qDebug() << "qtab_Main: Leaving settings tab, checking for unsaved changes";
                    needsValidation = true;
                    emit unsavedChangesCheckRequested(clickedTab, currentTab);
                }
                // Second check: Are we trying to access a password-protected tab?
                else if (m_passwordProtectedTabs.contains(clickedTabObjectName)) {
                    qDebug() << "qtab_Main: Accessing password-protected tab, requesting validation";
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

void qtab_Main::attemptTabSwitch(int targetTabIndex)
{
    qDebug() << "qtab_Main: attemptTabSwitch called for index:" << targetTabIndex;
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
        qDebug() << "qtab_Main: Leaving settings tab during tab switch, checking for unsaved changes";
        // Emit signal to check for unsaved changes
        emit unsavedChangesCheckRequested(targetTabIndex, currentTab);
        return; // Let the unsaved changes handler deal with the actual tab switch
    }

    // Second check: Are we trying to access a password-protected tab?
    if (m_passwordProtectedTabs.contains(targetTabObjectName)) {
        qDebug() << "qtab_Main: Accessing password-protected tab during tab switch, requesting validation";
        // Emit signal to request password validation
        emit passwordValidationRequested(targetTabIndex, currentTab);
        return; // Let the password validation handler deal with the actual tab switch
    }

    // No validation needed, switch directly
    qDebug() << "qtab_Main: No validation needed, switching to tab:" << targetTabIndex;
    setCurrentIndex(targetTabIndex);
}

void qtab_Main::moveTab(int fromIndex, int toIndex) {
    qDebug() << "qtab_Main: moveTab called from" << fromIndex << "to" << toIndex;
    tabBar()->moveTab(fromIndex, toIndex);
}
