#ifndef VP_SHOWS_SETTINGS_DIALOG_H
#define VP_SHOWS_SETTINGS_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

class VP_ShowsSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsSettingsDialog(QWidget *parent = nullptr);
    ~VP_ShowsSettingsDialog();

private slots:
    void onSaveClicked();
    void onTestConnectionClicked();
    void onTMDBEnabledToggled(bool checked);

private:
    void setupUi();
    void loadSettings();
    
    // UI elements
    QCheckBox* m_enableTMDBCheckBox;
    QLineEdit* m_apiKeyEdit;
    QPushButton* m_testButton;
    QPushButton* m_saveButton;
    QPushButton* m_cancelButton;
};

#endif // VP_SHOWS_SETTINGS_DIALOG_H
