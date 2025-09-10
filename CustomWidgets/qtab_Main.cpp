#include "qtab_Main.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/passwordvalidation.h"
#include <QEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QDebug>
#include <QKeyEvent>

qtab_Main::qtab_Main(QWidget *parent)
    : QTabWidget{parent}, 
      m_settingsTabObjectName("tab_Settings"), 
      m_tabVisibilityMenu(nullptr),
      m_isValidating(false),
      m_isInitialized(false),  // Not initialized yet
      m_validationTimer(nullptr),
      m_pendingTabIndex(-1)
{
    qDebug() << "qtab_Main: Constructor called";
    
    // Install event filter on the tab bar (check if tabBar exists)
    if (tabBar()) {
        tabBar()->installEventFilter(this);
    } else {
        qWarning() << "qtab_Main: tabBar() is null in constructor";
    }

    // Initialize tab mappings
    initializeTabMappings();

    // Create the context menu
    createTabVisibilityMenu();
    
    // SECURITY: Setup validation timer for race condition prevention
    m_validationTimer = new SafeTimer(this, "qtab_Main");
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(100); // 100ms timeout for validation
    // Note: SafeTimer requires callback to be provided when calling start()
    
    // SECURITY: Set focus policy to capture keyboard events
    setFocusPolicy(Qt::StrongFocus);
    
    // Defer marking as initialized until after a short delay
    // This ensures parent widgets are fully constructed
    SafeTimer::singleShot(0, this, [this]() {
        m_isInitialized = true;
        qDebug() << "qtab_Main: Initialization complete";
    }, "qtab_Main_Init");
}

qtab_Main::~qtab_Main()
{
    qDebug() << "qtab_Main: Destructor called";
    
    // Clean up timer (SafeTimer handles cleanup automatically, but stop it first)
    if (m_validationTimer) {
        m_validationTimer->stop();
        delete m_validationTimer;
        m_validationTimer = nullptr;
    }
    
    // Clean up menu
    if (m_tabVisibilityMenu) {
        delete m_tabVisibilityMenu;
        m_tabVisibilityMenu = nullptr;
    }
}

void qtab_Main::initializeTabMappings()
{
    qDebug() << "qtab_Main: initializeTabMappings called";
    // Map tab object names to user-friendly display names
    m_tabObjectNameToDisplayName["tab_Diaries"] = "Diaries";
    m_tabObjectNameToDisplayName["tab_Tasklists"] = "Task lists";
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
            // Switch to settings tab before hiding the requested tab (safely)
            SafeTimer::singleShot(0, this, [this, settingsTabIndex]() {
                if (m_isInitialized && settingsTabIndex < count()) {
                    QTabWidget::setCurrentIndex(settingsTabIndex);
                }
            }, "qtab_Main_HideSwitch");
            qDebug() << "qtab_Main: Switched to settings tab before hiding tab:" << objectName;
        }
        
        // SECURITY: Clear grace period for password-protected tabs when hiding them
        if (m_passwordProtectedTabs.contains(objectName)) {
            clearGracePeriodForHiddenTab(objectName);
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

void qtab_Main::setValidationInProgress(bool inProgress)
{
    qDebug() << "qtab_Main: setValidationInProgress called with:" << inProgress;
    m_isValidating = inProgress;
    
    // If clearing validation, also clear any pending validation
    if (!inProgress) {
        m_pendingTabIndex = -1;
        if (m_validationTimer) {
            m_validationTimer->stop();
        }
    }
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

                // SECURITY: Additional safety check for rapid clicking
                if (m_isValidating) {
                    qDebug() << "qtab_Main: Already validating, ignoring click";
                    return true;
                }

                // First check: Are we trying to leave the settings tab?
                if (currentTabObjectName == m_settingsTabObjectName && clickedTabObjectName != m_settingsTabObjectName) {
                    qDebug() << "qtab_Main: Leaving settings tab, checking for unsaved changes";
                    needsValidation = true;
                    // CRITICAL FIX: Set validation flag BEFORE emitting signal
                    m_isValidating = true;
                    emit unsavedChangesCheckRequested(clickedTab, currentTab);
                }
                // Second check: Are we trying to access a password-protected tab?
                else if (m_passwordProtectedTabs.contains(clickedTabObjectName)) {
                    qDebug() << "qtab_Main: Accessing password-protected tab, requesting validation";
                    needsValidation = true;
                    // CRITICAL FIX: Set validation flag BEFORE emitting signal
                    m_isValidating = true;
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
    
    // Safety check
    if (!m_isInitialized || targetTabIndex < 0 || targetTabIndex >= count()) {
        qWarning() << "qtab_Main: Invalid state or index in attemptTabSwitch";
        return;
    }
    
    // SECURITY: Prevent race conditions - if validation is already in progress, queue this request
    if (m_isValidating) {
        qDebug() << "qtab_Main: Validation already in progress, queueing request";
        m_pendingTabIndex = targetTabIndex;
        if (m_validationTimer) {
            m_validationTimer->stop();
            // SafeTimer requires a callback when starting
            m_validationTimer->start([this]() {
                this->onValidationTimeout();
            });
        }
        return;
    }
    
    // Get the current tab
    int currentTab = currentIndex();

    // Don't switch if we're already on the target tab
    if (currentTab == targetTabIndex) {
        return;
    }
    
    // SECURITY: Mark validation as in progress
    m_isValidating = true;

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
        // m_isValidating is already set above, just emit the signal
        emit unsavedChangesCheckRequested(targetTabIndex, currentTab);
        // Note: MainWindow handler MUST call setValidationInProgress(false) when done
        return; // Let the unsaved changes handler deal with the actual tab switch
    }

    // Second check: Are we trying to access a password-protected tab?
    if (m_passwordProtectedTabs.contains(targetTabObjectName)) {
        qDebug() << "qtab_Main: Accessing password-protected tab during tab switch, requesting validation";
        // Emit signal to request password validation  
        // m_isValidating is already set above, just emit the signal
        emit passwordValidationRequested(targetTabIndex, currentTab);
        // Note: MainWindow handler MUST call setValidationInProgress(false) when done
        return; // Let the password validation handler deal with the actual tab switch
    }

    // No validation needed, switch directly
    qDebug() << "qtab_Main: No validation needed, switching to tab:" << targetTabIndex;
    
    // SECURITY: Use SafeTimer to defer the actual switch slightly to avoid stack overflow
    SafeTimer::singleShot(0, this, [this, targetTabIndex]() {
        if (m_isInitialized && targetTabIndex < count()) {
            QTabWidget::setCurrentIndex(targetTabIndex);
        }
    }, "qtab_Main_Switch");
    
    m_isValidating = false;
}

void qtab_Main::moveTab(int fromIndex, int toIndex) {
    qDebug() << "qtab_Main: moveTab called from" << fromIndex << "to" << toIndex;
    
    // SECURITY: Check if we're moving a password-protected tab
    QString movedTabObjectName = getTabObjectNameByIndex(fromIndex);
    if (m_passwordProtectedTabs.contains(movedTabObjectName)) {
        qDebug() << "qtab_Main: Moving password-protected tab, validation may be required on next access";
    }
    
    tabBar()->moveTab(fromIndex, toIndex);
}

// SECURITY: Override keyPressEvent to intercept keyboard shortcuts
void qtab_Main::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "qtab_Main: keyPressEvent called with key:" << event->key() << "modifiers:" << event->modifiers();
    
    // SECURITY: Prevent processing during validation
    if (m_isValidating) {
        qDebug() << "qtab_Main: Validation in progress, ignoring keyboard shortcut";
        event->ignore();
        return;
    }
    
    // Check for tab switching shortcuts
    bool isTabSwitchShortcut = false;
    int targetIndex = -1;
    
    // Ctrl+Tab / Ctrl+Shift+Tab
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Tab) {
        isTabSwitchShortcut = true;
        targetIndex = (currentIndex() + 1) % count();
        // Find next visible tab
        while (!isTabVisible(targetIndex) && targetIndex != currentIndex()) {
            targetIndex = (targetIndex + 1) % count();
        }
    } else if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_Tab) {
        isTabSwitchShortcut = true;
        targetIndex = currentIndex() - 1;
        if (targetIndex < 0) targetIndex = count() - 1;
        // Find previous visible tab
        while (!isTabVisible(targetIndex) && targetIndex != currentIndex()) {
            targetIndex--;
            if (targetIndex < 0) targetIndex = count() - 1;
        }
    }
    // Alt+1 through Alt+9
    else if (event->modifiers() == Qt::AltModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        int tabNumber = event->key() - Qt::Key_1;
        if (tabNumber < count() && isTabVisible(tabNumber)) {
            isTabSwitchShortcut = true;
            targetIndex = tabNumber;
        }
    }
    
    if (isTabSwitchShortcut && targetIndex >= 0 && targetIndex != currentIndex()) {
        qDebug() << "qtab_Main: Keyboard shortcut detected for tab switch to index:" << targetIndex;
        event->accept();
        
        // Use attemptTabSwitch which includes validation
        attemptTabSwitch(targetIndex);
        return;
    }
    
    // Let parent handle other key events
    QTabWidget::keyPressEvent(event);
}

// SECURITY: Intercept setCurrentIndex to validate ALL tab switches
// This method hides (not overrides) the base class method to add validation
void qtab_Main::setCurrentIndex(int index)
{
    // Bypass our validation entirely if not ready
    // This prevents crashes during initialization
    if (!m_isInitialized) {
        QTabWidget::setCurrentIndex(index);
        return;
    }
    
    // Safety check for invalid index
    if (index < 0 || index >= count()) {
        QTabWidget::setCurrentIndex(index);
        return;
    }
    
    // Don't validate if parent window is not visible yet (still initializing)
    QWidget* parent = parentWidget();
    if (!parent || !parent->isVisible()) {
        QTabWidget::setCurrentIndex(index);
        return;
    }
    
    // Check if this is called from within our own code (to avoid infinite loops)
    if (m_isValidating) {
        qDebug() << "qtab_Main: setCurrentIndex bypassing validation (already validating) for index:" << index;
        QTabWidget::setCurrentIndex(index);
        return;
    }
    
    // Now safe to log
    qDebug() << "qtab_Main: setCurrentIndex called for index:" << index;
    
    // Validate the programmatic switch
    if (validateProgrammaticSwitch(index)) {
        QTabWidget::setCurrentIndex(index);
    }
    // If validation fails, stay on current tab
}

// SECURITY: Validate programmatic tab switches
bool qtab_Main::validateProgrammaticSwitch(int targetIndex)
{
    qDebug() << "qtab_Main: validateProgrammaticSwitch called for index:" << targetIndex;
    
    // SECURITY: Check if parent still exists (SafeTimer pattern)
    if (!parent()) {
        qWarning() << "qtab_Main: Parent destroyed during validation";
        return false;
    }
    
    // Safety checks
    if (targetIndex < 0 || targetIndex >= count()) {
        qWarning() << "qtab_Main: Invalid target index in validateProgrammaticSwitch:" << targetIndex;
        return false;
    }
    
    int currentTab = currentIndex();
    if (currentTab == targetIndex) {
        return true; // No change needed
    }
    
    // More safety checks
    if (currentTab < 0 || currentTab >= count()) {
        qWarning() << "qtab_Main: Invalid current index in validateProgrammaticSwitch:" << currentTab;
        return true; // Allow the switch if current state is invalid
    }
    
    QString targetTabObjectName = getTabObjectNameByIndex(targetIndex);
    QString currentTabObjectName = getTabObjectNameByIndex(currentTab);
    
    // Check if we're leaving settings tab (unsaved changes check)
    if (currentTabObjectName == m_settingsTabObjectName && targetTabObjectName != m_settingsTabObjectName) {
        qDebug() << "qtab_Main: Programmatic switch from settings tab, emitting unsaved changes check";
        // CRITICAL FIX: Set validation flag BEFORE emitting signal to prevent recursion
        m_isValidating = true;
        emit unsavedChangesCheckRequested(targetIndex, currentTab);
        // Note: MainWindow handler MUST call setValidationInProgress(false) when done
        return false; // Let the signal handler deal with it
    }
    
    // Check if target tab is password-protected
    if (m_passwordProtectedTabs.contains(targetTabObjectName)) {
        qDebug() << "qtab_Main: Programmatic switch to password-protected tab, emitting validation request";
        // CRITICAL FIX: Set validation flag BEFORE emitting signal to prevent recursion
        m_isValidating = true;
        emit passwordValidationRequested(targetIndex, currentTab);
        // Note: MainWindow handler MUST call setValidationInProgress(false) when done
        return false; // Let the signal handler deal with it
    }
    
    return true; // Validation passed
}

// SECURITY: Clear grace period for hidden tabs
void qtab_Main::clearGracePeriodForHiddenTab(const QString& tabObjectName)
{
    qDebug() << "qtab_Main: Clearing grace period for hidden tab:" << tabObjectName;
    
    // We need to get the username somehow - this might need to be passed from MainWindow
    // For now, we'll clear all grace periods as a security measure
    PasswordValidation::clearGracePeriod();
}

// SECURITY: Handle validation timeout
void qtab_Main::onValidationTimeout()
{
    qDebug() << "qtab_Main: Validation timeout, processing pending tab switch";
    
    // Safety check - verify we still exist and are valid
    if (!m_isInitialized) {
        qWarning() << "qtab_Main: Not initialized in timeout handler";
        m_isValidating = false;
        m_pendingTabIndex = -1;
        return;
    }
    
    // Check if we have a pending tab switch
    if (m_pendingTabIndex >= 0 && m_pendingTabIndex < count() && m_pendingTabIndex != currentIndex()) {
        int pendingIndex = m_pendingTabIndex;
        m_pendingTabIndex = -1;
        m_isValidating = false; // Reset before attempting switch
        
        // Attempt the pending tab switch
        attemptTabSwitch(pendingIndex);
    } else {
        // No valid pending switch, just reset state
        m_isValidating = false;
        m_pendingTabIndex = -1;
    }
}
