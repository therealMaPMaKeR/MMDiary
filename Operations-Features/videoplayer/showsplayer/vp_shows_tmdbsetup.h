#ifndef VP_SHOWS_TMDBSETUP_H
#define VP_SHOWS_TMDBSETUP_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

class VP_Shows_TMDBSetup : public QDialog
{
    Q_OBJECT

public:
    explicit VP_Shows_TMDBSetup(QWidget *parent = nullptr);
    ~VP_Shows_TMDBSetup();

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

#endif // VP_SHOWS_TMDBSETUP_H
