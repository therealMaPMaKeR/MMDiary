#ifndef ENCRYPTED_FILE_ITEM_WIDGET_H
#define ENCRYPTED_FILE_ITEM_WIDGET_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QPixmap>

class EncryptedFileItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EncryptedFileItemWidget(QWidget *parent = nullptr);
    ~EncryptedFileItemWidget();

    // Set the file information
    void setFileInfo(const QString& originalFilename, const QString& encryptedFilePath,
                     const QString& fileType);

    // Set the icon/thumbnail
    void setIcon(const QPixmap& pixmap);

    // Get file information
    QString getOriginalFilename() const { return m_originalFilename; }
    QString getEncryptedFilePath() const { return m_encryptedFilePath; }
    QString getFileType() const { return m_fileType; }

    // Icon size management
    static void setIconSize(int size);
    static int getIconSize() { return s_iconSize; }

    // Check if this item needs thumbnail loading
    bool needsThumbnailLoad() const { return m_needsThumbnailLoad; }
    void setThumbnailLoaded() { m_needsThumbnailLoad = false; }

private:
    void setupUI();
    void updateIconSize();

    // UI components
    QHBoxLayout* m_layout;
    QLabel* m_iconLabel;
    QLabel* m_filenameLabel;

    // File information
    QString m_originalFilename;
    QString m_encryptedFilePath;
    QString m_fileType;

    // State
    bool m_needsThumbnailLoad;

    // Static icon size (can be changed globally)
    static int s_iconSize;
};

#endif // ENCRYPTED_FILE_ITEM_WIDGET_H
