#include "encryptedfileitemwidget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QFontMetrics>
#include <QDebug>

// Initialize static member
int EncryptedFileItemWidget::s_iconSize = 64;

EncryptedFileItemWidget::EncryptedFileItemWidget(QWidget *parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_iconLabel(nullptr)
    , m_filenameLabel(nullptr)
    , m_needsThumbnailLoad(true)
{
    qDebug() << "EncryptedFileItemWidget: Constructor called";
    setupUI();
}

EncryptedFileItemWidget::~EncryptedFileItemWidget()
{
    qDebug() << "EncryptedFileItemWidget: Destructor called";
    // Qt will handle cleanup of child widgets
}

void EncryptedFileItemWidget::setupUI()
{
    qDebug() << "EncryptedFileItemWidget: setupUI called";
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
                                          const QString& fileType,
                                          const QStringList& tags)
{
    qDebug() << "EncryptedFileItemWidget: setFileInfo called for file:" << originalFilename;
    m_originalFilename = originalFilename;
    m_encryptedFilePath = encryptedFilePath;
    m_fileType = fileType;
    m_tags = tags;

    // Set filename with elision if too long
    QFontMetrics fm(m_filenameLabel->font());
    QString elidedText = fm.elidedText(originalFilename, Qt::ElideMiddle, 1000);
    m_filenameLabel->setText(elidedText);
    
    // Build tooltip with filename and tags
    QString tooltip = originalFilename;
    
    // Add tags to tooltip if they exist
    if (!tags.isEmpty()) {
        tooltip += "\n\nTags: " + tags.join(", ");
    }
    
    m_filenameLabel->setToolTip(tooltip);
    
    // Also set tooltip on the entire widget for better coverage
    this->setToolTip(tooltip);
    
    qDebug() << "EncryptedFileItemWidget: Set tooltip for" << originalFilename 
             << "with" << tags.size() << "tags";
}

void EncryptedFileItemWidget::setIcon(const QPixmap& pixmap)
{
    qDebug() << "EncryptedFileItemWidget: setIcon called with pixmap size:" << pixmap.size();
    if (!pixmap.isNull()) {
        QPixmap scaledPixmap = pixmap.scaled(s_iconSize, s_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_iconLabel->setPixmap(scaledPixmap);
    }
}

void EncryptedFileItemWidget::setIconSize(int size)
{
    qDebug() << "EncryptedFileItemWidget: setIconSize called with size:" << size;
    s_iconSize = size;
    // Note: Existing widgets would need to be updated manually if size changes
}

void EncryptedFileItemWidget::updateIconSize()
{
    qDebug() << "EncryptedFileItemWidget: updateIconSize called";
    m_iconLabel->setFixedSize(s_iconSize, s_iconSize);
}
