#ifndef OPERATIONS_PASSWORDMANAGER_H
#define OPERATIONS_PASSWORDMANAGER_H

#include <QObject>
#include <QPointer>
#include "../../mainwindow.h"
#include "operations.h"
#include "inputvalidation.h"
#include "encryption/SecureByteArray.h"
#include "../Operations-Global/SafeTimer.h"
#include "../Operations-Global/security/clipboard_security.h"
#include <QMessageBox>

class MainWindow;
class Operations_PasswordManager : public QObject
{
    Q_OBJECT
private:
    // SECURITY: Use QPointer for automatic null checking when MainWindow is destroyed
    QPointer<MainWindow> m_mainWindow;
public:
    explicit Operations_PasswordManager(MainWindow* mainWindow);
    ~Operations_PasswordManager(); // Destructor for secure cleanup
    friend class MainWindow;


    void UpdatePasswordMasking();
private:
    //functions
    void SetupPWDisplay(QString sortingMethod);
    void SetupPWList(QString sortingMethod, bool applyMasking = true);
    void AddPassword(QString account, const SecureByteArray& password, QString service);
    void UpdatePWDisplayForSelection(const QString &selectedValue);
    bool DeletePassword(const QString &account, const SecureByteArray& password, const QString &service);
    bool ModifyPassword(const QString &oldAccount, const SecureByteArray& oldPassword, const QString &oldService,
                        const QString &newAccount, const SecureByteArray& newPassword, const QString &newService);
    bool DeleteAllAssociatedPasswords(const QString &value, const QString &field);

    // Search functionality
    void updateSearchPlaceholder();
    void filterPWList(const QString& searchText);
    void preserveAndReapplySearchFilter();
    
    // Currently loaded password tracking
    QString m_currentLoadedValue;  // The value from the list that's currently loaded in the display
    
    // Secure cleanup helper
    void cleanupCachedPasswords();

    SafeTimer* m_clipboardTimer = nullptr; // Timer for clearing clipboard
    SafeTimer* m_pasteDelayTimer = nullptr; // Timer for delay after paste detection
    ClipboardSecurity::ClipboardMonitor* m_clipboardMonitor = nullptr; // Monitor for paste/overwrite detection
    bool m_clipboardClearPending = false; // Track if clipboard clear is pending
    QString m_copiedPasswordHash; // Hash of the copied password for comparison
    
    void startClipboardClearTimer();
    void clearClipboard();
    void setupClipboardMonitoring(const QString& password);
    void stopClipboardMonitoring();
    void onClipboardPasteDetected();
    void onClipboardOverwritten();

    // Function to show context menu
    void showContextMenu_PWDisplay(const QPoint &pos);
    void showContextMenu_PWList(const QPoint &pos);
    
    // Password generation
    SecureByteArray generatePassword();
    
    // Password generation configuration (future menu settings)
    int m_passwordLength = 12;
    QString m_allowedSymbols = "!@#$%&*";
    int m_maxSymbols = 3;
public slots:
    void on_SortByChanged(QString currentText);
    void on_AddPasswordClicked();
    void on_PWListItemClicked(QListWidgetItem *item);

    void onTableItemDoubleClicked(QTableWidgetItem *item);

    // Search functionality slot
    void onSearchTextChanged(const QString& text);

    // Context menu action slots
    void onDeletePasswordClicked();
    void onEditPasswordClicked();
    void onCopyToClipboardClicked();
    void onDeleteAllAssociatedPasswordsClicked();
};

#endif // OPERATIONS_PASSWORDMANAGER_H
