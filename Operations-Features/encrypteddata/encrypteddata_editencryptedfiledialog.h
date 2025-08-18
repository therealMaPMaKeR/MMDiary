#ifndef EDITENCRYPTEDFILEDIALOG_H
#define EDITENCRYPTEDFILEDIALOG_H

#include <QDialog>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include "encrypteddata_encryptedfilemetadata.h"

namespace Ui {
class EditEncryptedFileDialog;
}

class EditEncryptedFileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditEncryptedFileDialog(QWidget *parent = nullptr);
    ~EditEncryptedFileDialog();

    // Initialize the dialog with file information
    void initialize(const QString& encryptedFilePath, const QByteArray& encryptionKey, const QString& username);

private slots:
    void on_pushButton_Save_clicked();
    void on_pushButton_Cancel_clicked();

private:
    Ui::EditEncryptedFileDialog *ui;

    // Store current file info
    QString m_encryptedFilePath;
    QByteArray m_encryptionKey;
    QString m_username;
    EncryptedFileMetadata::FileMetadata m_originalMetadata;

    // NEW: Store the original file extension separately
    QString m_originalExtension;

    // Metadata manager
    EncryptedFileMetadata* m_metadataManager;

    // Validation methods
    bool validateAllInputs();
    bool validateFilename(const QString& filename);
    bool validateCategory(const QString& category);
    bool validateTags(const QString& tagsString, QStringList& parsedTags);

    // Helper methods
    void loadCurrentMetadata();
    void populateFields();
    bool saveMetadata();

    // NEW: Helper method to split filename and extension
    void splitFilenameAndExtension(const QString& fullFilename, QString& baseName, QString& extension);
};

#endif // EDITENCRYPTEDFILEDIALOG_H
