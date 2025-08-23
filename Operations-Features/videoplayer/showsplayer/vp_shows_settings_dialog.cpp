#include "vp_shows_settings_dialog.h"
#include "ui_vp_shows_settings_dialog.h"
#include <QDebug>

VP_ShowsSettingsDialog::VP_ShowsSettingsDialog(const QString& showName, const QString& showPath, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VP_ShowsSettingsDialog)
    , m_showName(showName)
    , m_showPath(showPath)
{
    ui->setupUi(this);
    
    qDebug() << "VP_ShowsSettingsDialog: Created dialog for show:" << showName;
    qDebug() << "VP_ShowsSettingsDialog: Show path:" << showPath;
    
    // Set window title to include show name
    setWindowTitle(QString("Settings - %1").arg(showName));
    
    // TODO: Load show-specific settings here
    
    // TODO: Connect UI signals to slots here
}

VP_ShowsSettingsDialog::~VP_ShowsSettingsDialog()
{
    qDebug() << "VP_ShowsSettingsDialog: Destructor called";
    delete ui;
}

// TODO: Implement slot functions as needed
