#ifndef VP_SHOWS_SETTINGS_DIALOG_H
#define VP_SHOWS_SETTINGS_DIALOG_H

#include <QDialog>

namespace Ui {
class VP_ShowsSettingsDialog;
}

class VP_ShowsSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent = nullptr);
    ~VP_ShowsSettingsDialog();

private slots:
    // Add your slot functions here as needed

private:
    Ui::VP_ShowsSettingsDialog *ui;
    QString m_showName;
    QString m_showPath;
};

#endif // VP_SHOWS_SETTINGS_DIALOG_H
