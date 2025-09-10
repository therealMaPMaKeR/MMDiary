#include "vp_shows_tmdbsetup.h"
#include "vp_shows_config.h"
#include "vp_shows_tmdb.h"
#include "inputvalidation.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QDebug>

VP_Shows_TMDBSetup::VP_Shows_TMDBSetup(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    loadSettings();
}

VP_Shows_TMDBSetup::~VP_Shows_TMDBSetup()
{
    qDebug() << "VP_Shows_TMDBSetup: Destructor called";
}

void VP_Shows_TMDBSetup::setupUi()
{
    setWindowTitle("TMDB Integration Setup");
    setMinimumWidth(500);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // TMDB Settings Group
    QGroupBox* tmdbGroup = new QGroupBox("The Movie Database (TMDB) Integration", this);
    QVBoxLayout* tmdbLayout = new QVBoxLayout(tmdbGroup);
    
    // Enable checkbox
    m_enableTMDBCheckBox = new QCheckBox("Enable TMDB integration for automatic metadata", this);
    tmdbLayout->addWidget(m_enableTMDBCheckBox);
    
    // Info label
    QLabel* infoLabel = new QLabel(
        "TMDB provides show information, episode names, and images.\n"
        "API Key Configuration:\n"
        "• The API key is defined in tmdb_api_key.h\n"
        "• To change it: edit tmdb_api_key.h and rebuild the project\n"
        "• Get your key from https://www.themoviedb.org/settings/api",
        this
    );
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: #666; padding: 5px; }");
    tmdbLayout->addWidget(infoLabel);
    
    // API Key display (read-only)
    QHBoxLayout* apiKeyLayout = new QHBoxLayout();
    QLabel* apiKeyLabel = new QLabel("API Key Status:", this);
    apiKeyLabel->setMinimumWidth(100);
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setReadOnly(true);
    m_apiKeyEdit->setStyleSheet("QLineEdit { background-color: #f0f0f0; }");
    
    apiKeyLayout->addWidget(apiKeyLabel);
    apiKeyLayout->addWidget(m_apiKeyEdit);
    tmdbLayout->addLayout(apiKeyLayout);
    
    // Test connection button
    m_testButton = new QPushButton("Test Connection", this);
    tmdbLayout->addWidget(m_testButton);
    
    mainLayout->addWidget(tmdbGroup);
    
    // Spacer
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_saveButton = new QPushButton("Save", this);
    m_cancelButton = new QPushButton("Cancel", this);
    
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addWidget(m_cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(m_enableTMDBCheckBox, &QCheckBox::toggled, this, &VP_Shows_TMDBSetup::onTMDBEnabledToggled);
    connect(m_testButton, &QPushButton::clicked, this, &VP_Shows_TMDBSetup::onTestConnectionClicked);
    connect(m_saveButton, &QPushButton::clicked, this, &VP_Shows_TMDBSetup::onSaveClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    // Initial state
    onTMDBEnabledToggled(m_enableTMDBCheckBox->isChecked());
}

void VP_Shows_TMDBSetup::loadSettings()
{
    // Load TMDB enabled state
    bool tmdbEnabled = VP_ShowsConfig::isTMDBEnabled();
    m_enableTMDBCheckBox->setChecked(tmdbEnabled);
    
    // Display API key status
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (!apiKey.isEmpty()) {
        if (apiKey.startsWith("Bearer ")) {
            m_apiKeyEdit->setText("Bearer token configured (" + QString::number(apiKey.length()) + " chars)");
        } else {
            m_apiKeyEdit->setText("API key configured (" + QString::number(apiKey.length()) + " chars)");
        }
    } else {
        m_apiKeyEdit->setText("No API key found - edit tmdb_api_key.h and rebuild");
    }
    
    qDebug() << "VP_Shows_TMDBSetup: Settings loaded";
}

void VP_Shows_TMDBSetup::onSaveClicked()
{
    // Save only the enabled/disabled state
    bool tmdbEnabled = m_enableTMDBCheckBox->isChecked();
    VP_ShowsConfig::setTMDBEnabled(tmdbEnabled);
    
    if (tmdbEnabled && VP_ShowsConfig::getTMDBApiKey().isEmpty()) {
        // Warn if enabling without API key
        QMessageBox::information(this, "API Key Required",
                               "TMDB integration is enabled but no API key is configured.\n\n"
                               "To add an API key:\n"
                               "1. Copy tmdb_api_key_TEMPLATE.h to tmdb_api_key.h\n"
                               "2. Add your API key to the file\n"
                               "3. Rebuild the project");
    }
    
    QMessageBox::information(this, "Settings Saved", "TMDB settings have been saved.");
    accept();
}

void VP_Shows_TMDBSetup::onTestConnectionClicked()
{
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    
    if (apiKey.isEmpty()) {
        QMessageBox::warning(this, "No API Key", 
                           "No API key configured.\n\n"
                           "Please edit tmdb_api_key.h and rebuild the project.");
        return;
    }
    
    // Disable button during test
    m_testButton->setEnabled(false);
    m_testButton->setText("Testing...");
    
    // Create temporary TMDB instance for testing
    VP_ShowsTMDB testTMDB;
    testTMDB.setApiKey(apiKey);
    
    // Try to search for a known show as a test
    VP_ShowsTMDB::ShowInfo testShow;
    bool success = testTMDB.searchTVShow("Breaking Bad", testShow);
    
    // Re-enable button
    m_testButton->setEnabled(true);
    m_testButton->setText("Test Connection");
    
    if (success && testShow.tmdbId > 0) {
        QMessageBox::information(this, "Connection Successful",
                               QString("Successfully connected to TMDB!\n\n"
                                      "Test search found: %1").arg(testShow.showName));
        qDebug() << "VP_Shows_TMDBSetup: TMDB connection test successful";
    } else {
        QMessageBox::warning(this, "Connection Failed",
                           "Failed to connect to TMDB. Please check your API key and internet connection.");
        qDebug() << "VP_Shows_TMDBSetup: TMDB connection test failed";
    }
}

void VP_Shows_TMDBSetup::onTMDBEnabledToggled(bool checked)
{
    // API key display is always read-only
    m_testButton->setEnabled(checked && !VP_ShowsConfig::getTMDBApiKey().isEmpty());
    
    qDebug() << "VP_Shows_TMDBSetup: TMDB enabled toggled:" << checked;
}
