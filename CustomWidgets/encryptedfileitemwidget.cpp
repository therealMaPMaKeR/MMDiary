#include "encryptedfileitemwidget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QFontMetrics>

// Initialize static member
int EncryptedFileItemWidget::s_iconSize = 64;

EncryptedFileItemWidget::EncryptedFileItemWidget(QWidget *parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_iconLabel(nullptr)
    , m_filenameLabel(nullptr)
    , m_needsThumbnailLoad(true)
{
    setupUI();
}

EncryptedFileItemWidget::~EncryptedFileItemWidget()
{
    // Qt will handle cleanup of child widgets
}

void EncryptedFileItemWidget::setupUI()
{
    // Create layout
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(8);

    // Create icon label
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(s_iconSize, s_iconSize);
    m_iconLabel->setScaledContents(true);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet("border: 1px solid #555; background-color: #333;");

    // Create filename label
    m_filenameLabel = new QLabel(this);
    m_filenameLabel->setWordWrap(false);
    m_filenameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Add widgets to layout
    m_layout->addWidget(m_iconLabel, 0);
    m_layout->addWidget(m_filenameLabel, 1);

    setLayout(m_layout);
}

void EncryptedFileItemWidget::setFileInfo(const QString& originalFilename,
                                          const QString& encryptedFilePath,
                                          const QString& fileType)
{
    m_originalFilename = originalFilename;
    m_encryptedFilePath = encryptedFilePath;
    m_fileType = fileType;

    // Set filename with elision if too long
    QFontMetrics fm(m_filenameLabel->font());
    QString elidedText = fm.elidedText(originalFilename, Qt::ElideMiddle, 1000);
    m_filenameLabel->setText(elidedText);
    m_filenameLabel->setToolTip(originalFilename); // Full filename in tooltip
}

void EncryptedFileItemWidget::setIcon(const QPixmap& pixmap)
{
    if (!pixmap.isNull()) {
        QPixmap scaledPixmap = pixmap.scaled(s_iconSize, s_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_iconLabel->setPixmap(scaledPixmap);
    }
}

void EncryptedFileItemWidget::setIconSize(int size)
{
    s_iconSize = size;
    // Note: Existing widgets would need to be updated manually if size changes
}

void EncryptedFileItemWidget::updateIconSize()
{
    m_iconLabel->setFixedSize(s_iconSize, s_iconSize);
}
