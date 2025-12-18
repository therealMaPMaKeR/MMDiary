#include "encrypteddata_progressdialogs.h"
#include <QApplication>

// ============================================================================
// EncryptionProgressDialog Implementation
// ============================================================================

EncryptionProgressDialog::EncryptionProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_overallProgress(nullptr)
    , m_fileProgress(nullptr)
    , m_statusLabel(nullptr)
    , m_fileCountLabel(nullptr)
    , m_cancelButton(nullptr)
    , m_cancelled(false)
{
    setupUI();
    setWindowTitle("Encryption Progress");
    setWindowModality(Qt::WindowModal);
    setFixedSize(400, 200);
}

void EncryptionProgressDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Preparing to encrypt files...", this);
    mainLayout->addWidget(m_statusLabel);
    
    // File count label
    m_fileCountLabel = new QLabel("Files: 0/0", this);
    mainLayout->addWidget(m_fileCountLabel);
    
    // Overall progress bar
    QLabel* overallLabel = new QLabel("Overall Progress:", this);
    mainLayout->addWidget(overallLabel);
    m_overallProgress = new QProgressBar(this);
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    mainLayout->addWidget(m_overallProgress);
    
    // File progress bar
    QLabel* fileLabel = new QLabel("Current File Progress:", this);
    mainLayout->addWidget(fileLabel);
    m_fileProgress = new QProgressBar(this);
    m_fileProgress->setRange(0, 100);
    m_fileProgress->setValue(0);
    mainLayout->addWidget(m_fileProgress);
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &EncryptionProgressDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

void EncryptionProgressDialog::setOverallProgress(int percentage)
{
    if (m_overallProgress) {
        m_overallProgress->setValue(percentage);
    }
}

void EncryptionProgressDialog::setFileProgress(int percentage)
{
    if (m_fileProgress) {
        m_fileProgress->setValue(percentage);
    }
}

void EncryptionProgressDialog::setStatusText(const QString& text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}

void EncryptionProgressDialog::setFileCountText(const QString& text)
{
    if (m_fileCountLabel) {
        m_fileCountLabel->setText(text);
    }
}

bool EncryptionProgressDialog::wasCancelled() const
{
    return m_cancelled;
}

void EncryptionProgressDialog::onCancelClicked()
{
    m_cancelled = true;
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");
    setStatusText("Cancelling operation...");
    
    if (onCancelCallback) {
        onCancelCallback();
    }
    
    emit cancelled();
}

void EncryptionProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_cancelled) {
        onCancelClicked();
    }
    event->accept();
}

void EncryptionProgressDialog::reject()
{
    if (!m_cancelled) {
        onCancelClicked();
    }
    QDialog::reject();
}

// ============================================================================
// BatchDecryptionProgressDialog Implementation
// ============================================================================

BatchDecryptionProgressDialog::BatchDecryptionProgressDialog(QWidget* parent)
    : QDialog(parent)
    , m_overallProgress(nullptr)
    , m_fileProgress(nullptr)
    , m_statusLabel(nullptr)
    , m_fileCountLabel(nullptr)
    , m_cancelButton(nullptr)
    , m_cancelled(false)
{
    setupUI();
    setWindowTitle("Batch Export Progress");
    setWindowModality(Qt::WindowModal);
    setFixedSize(400, 200);
}

void BatchDecryptionProgressDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Preparing to export files...", this);
    mainLayout->addWidget(m_statusLabel);
    
    // File count label
    m_fileCountLabel = new QLabel("Files: 0/0", this);
    mainLayout->addWidget(m_fileCountLabel);
    
    // Overall progress bar
    QLabel* overallLabel = new QLabel("Overall Progress:", this);
    mainLayout->addWidget(overallLabel);
    m_overallProgress = new QProgressBar(this);
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    mainLayout->addWidget(m_overallProgress);
    
    // File progress bar
    QLabel* fileLabel = new QLabel("Current File Progress:", this);
    mainLayout->addWidget(fileLabel);
    m_fileProgress = new QProgressBar(this);
    m_fileProgress->setRange(0, 100);
    m_fileProgress->setValue(0);
    mainLayout->addWidget(m_fileProgress);
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &BatchDecryptionProgressDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

void BatchDecryptionProgressDialog::setOverallProgress(int percentage)
{
    if (m_overallProgress) {
        m_overallProgress->setValue(percentage);
    }
}

void BatchDecryptionProgressDialog::setFileProgress(int percentage)
{
    if (m_fileProgress) {
        m_fileProgress->setValue(percentage);
    }
}

void BatchDecryptionProgressDialog::setStatusText(const QString& text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}

void BatchDecryptionProgressDialog::setFileCountText(const QString& text)
{
    if (m_fileCountLabel) {
        m_fileCountLabel->setText(text);
    }
}

bool BatchDecryptionProgressDialog::wasCancelled() const
{
    return m_cancelled;
}

void BatchDecryptionProgressDialog::onCancelClicked()
{
    m_cancelled = true;
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText("Cancelling...");
    setStatusText("Cancelling operation...");
    
    if (onCancelCallback) {
        onCancelCallback();
    }
    
    emit cancelled();
}

void BatchDecryptionProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_cancelled) {
        onCancelClicked();
    }
    event->accept();
}

void BatchDecryptionProgressDialog::reject()
{
    if (!m_cancelled) {
        onCancelClicked();
    }
    QDialog::reject();
}
