#ifndef OPERATIONS_PASSWORDMANAGER_H
#define OPERATIONS_PASSWORDMANAGER_H

#include <QObject>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include <QMessageBox>

class MainWindow;
class Operations_PasswordManager : public QObject
{
    Q_OBJECT
private:
    MainWindow* m_mainWindow;
public:
    explicit Operations_PasswordManager(MainWindow* mainWindow);
    friend class MainWindow;


    void UpdatePasswordMasking();
private:
    //functions
    void SetupPWDisplay(QString sortingMethod);
    void SetupPWList(QString sortingMethod, bool applyMasking = true);
    void AddPassword(QString account, QString password, QString service);
    void UpdatePWDisplayForSelection(const QString &selectedValue);
    bool DeletePassword(const QString &account, const QString &password, const QString &service);
    bool ModifyPassword(const QString &oldAccount, const QString &oldPassword, const QString &oldService,const QString &newAccount, const QString &newPassword, const QString &newService);
    bool DeleteAllAssociatedPasswords(const QString &value, const QString &field);

    // Search functionality
    void updateSearchPlaceholder();
    void filterPWList(const QString& searchText);
    void preserveAndReapplySearchFilter();
    
    // Currently loaded password tracking
    QString m_currentLoadedValue;  // The value from the list that's currently loaded in the display

    QTimer* m_clipboardTimer; // Timer for clearing clipboard
    void startClipboardClearTimer();
    void clearClipboard();

    // Function to show context menu
    void showContextMenu_PWDisplay(const QPoint &pos);
    void showContextMenu_PWList(const QPoint &pos);
    
    // Password generation
    QString generatePassword();
    
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
