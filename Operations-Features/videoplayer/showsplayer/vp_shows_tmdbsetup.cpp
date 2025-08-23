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
        "You need either:\n"
        "• An API key (v3 auth) from https://www.themoviedb.org/settings/api\n"
        "• An API Read Access Token (Bearer token) for enhanced security",
        this
    );
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: #666; padding: 5px; }");
    tmdbLayout->addWidget(infoLabel);
    
    // API Key input
    QHBoxLayout* apiKeyLayout = new QHBoxLayout();
    QLabel* apiKeyLabel = new QLabel("API Key:", this);
    apiKeyLabel->setMinimumWidth(60);
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setPlaceholderText("Enter your TMDB API key or Bearer token");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    
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
    
    // Load API key (if exists)
    QString apiKey = VP_ShowsConfig::getTMDBApiKey();
    if (!apiKey.isEmpty()) {
        m_apiKeyEdit->setText(apiKey);
    }
    
    qDebug() << "VP_Shows_TMDBSetup: Settings loaded";
}

void VP_Shows_TMDBSetup::onSaveClicked()
{
    // Validate and save settings
    bool tmdbEnabled = m_enableTMDBCheckBox->isChecked();
    VP_ShowsConfig::setTMDBEnabled(tmdbEnabled);
    
    if (tmdbEnabled) {
        QString apiKey = m_apiKeyEdit->text().trimmed();
        
        if (!apiKey.isEmpty()) {
            // Validate API key format (basic check)
            // Bearer tokens are longer than regular API keys (can be 200+ characters)
            int maxLength = apiKey.startsWith("Bearer ") || apiKey.length() > 100 ? 512 : 256;
            
            InputValidation::ValidationResult validationResult = 
                InputValidation::validateInput(apiKey, InputValidation::InputType::PlainText, maxLength);
            
            if (!validationResult.isValid) {
                QMessageBox::warning(this, "Invalid API Key", 
                                   QString("The API key/token is invalid: %1").arg(validationResult.errorMessage));
                return;
            }
            
            VP_ShowsConfig::setTMDBApiKey(apiKey);
            qDebug() << "VP_Shows_TMDBSetup: API key saved";
        } else if (VP_ShowsConfig::getTMDBApiKey().isEmpty()) {
            // Warn if enabling without API key
            QMessageBox::information(this, "API Key Required",
                                   "TMDB integration is enabled but no API key is set. "
                                   "Metadata retrieval will not work until you provide a valid API key.");
        }
    }
    
    QMessageBox::information(this, "Settings Saved", "TMDB settings have been saved.");
    accept();
}

void VP_Shows_TMDBSetup::onTestConnectionClicked()
{
    QString apiKey = m_apiKeyEdit->text().trimmed();
    
    if (apiKey.isEmpty()) {
        QMessageBox::warning(this, "No API Key", "Please enter an API key to test.");
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
    m_apiKeyEdit->setEnabled(checked);
    m_testButton->setEnabled(checked);
    
    if (!checked) {
        m_apiKeyEdit->setStyleSheet("QLineEdit { background-color: #f0f0f0; }");
    } else {
        m_apiKeyEdit->setStyleSheet("");
    }
    
    qDebug() << "VP_Shows_TMDBSetup: TMDB enabled toggled:" << checked;
}
