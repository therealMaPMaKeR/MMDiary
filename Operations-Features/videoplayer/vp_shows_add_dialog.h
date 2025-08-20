#ifndef VP_SHOWS_ADD_DIALOG_H
#define VP_SHOWS_ADD_DIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class VP_ShowsAddDialog; }
QT_END_NAMESPACE

class VP_ShowsAddDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VP_ShowsAddDialog(const QString& folderName, QWidget *parent = nullptr);
    ~VP_ShowsAddDialog();
    
    // Getters for the dialog values
    QString getShowName() const;
    QString getLanguage() const;
    QString getTranslationMode() const;
    
    // Validation
    bool validateInputs();

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    
private:
    Ui::VP_ShowsAddDialog *ui;
    QString m_folderName;
    
    // Helper function to validate and sanitize input
    bool validateShowName(const QString& showName);
    bool validateLanguage(const QString& language);
};

#endif // VP_SHOWS_ADD_DIALOG_H
