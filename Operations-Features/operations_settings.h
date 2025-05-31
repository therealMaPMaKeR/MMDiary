#ifndef OPERATIONS_SETTINGS_H
#define OPERATIONS_SETTINGS_H

#include <QObject>
#include "../mainwindow.h"
#include "../Operations-Global/operations.h"
#include "../Operations-Global/inputvalidation.h"
#include "constants.h"
#include <QMessageBox>
#include <QMap>
#include "../Operations-Global/passwordvalidation.h"

class MainWindow;
class Operations_Settings : public QObject
{
    Q_OBJECT
private:
    MainWindow* m_mainWindow;

    // Maps to store setting names and descriptions
    QMap<QObject*, QString> m_settingNames;
    QMap<QObject*, QString> m_settingDescriptions;

    // Helper functions
    void UpdateButtonStates(const QString& settingsType);
    bool ValidateSettingsInput(const QString& settingsType);
    bool ValidatePassword(const QString& settingsType);
    QString getSettingsTypeFromTabObjectName(const QString& tabObjectName);
    QString getTabObjectNameByIndex(QTabWidget* tabWidget, int index);


    // Setting description functions
    void SetupSettingDescriptions();
    void DisplaySettingDescription(QObject* control);
    void ClearSettingDescription();

    // Event filter for hover events
    bool eventFilter(QObject* watched, QEvent* event) override;

public:
    explicit Operations_Settings(MainWindow* mainWindow);
    friend class MainWindow;
    void InitializeCustomCheckboxes();
    void LoadSettings(const QString& settingsType = Constants::DBSettings_Type_ALL);
    void SaveSettings(const QString& settingsType);
    void Slot_ButtonPressed(const QString button);
    void Slot_ValueChanged(const QString settingsType);
public slots:
    void onSettingsTabChanged(int newIndex);
    void onMainTabChanged(int newIndex);
private:
    bool hasUnsavedChanges(const QString& settingsType);
    bool handleUnsavedChanges(const QString& settingsType, int newTabIndex);
    int m_previousSettingsTabIndex;
    int m_previousMainTabIndex;
    // New private methods for dialog management
    void showHiddenItemsDialog(const QString& itemType, QString& settingValue, bool& hideItemsSetting);
    QStringList parseItemList(const QString& itemString);
    QString formatItemList(const QStringList& items);
private slots:
    void onHiddenCategoriesClicked();
    void onHiddenTagsClicked();
};

#endif // OPERATIONS_SETTINGS_H
