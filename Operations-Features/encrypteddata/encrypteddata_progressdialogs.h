#ifndef ENCRYPTEDDATA_PROGRESSDIALOGS_H
#define ENCRYPTEDDATA_PROGRESSDIALOGS_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <functional>

class EncryptionProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EncryptionProgressDialog(QWidget* parent = nullptr);

    void setOverallProgress(int percentage);
    void setFileProgress(int percentage);
    void setStatusText(const QString& text);
    void setFileCountText(const QString& text);
    bool wasCancelled() const;

    std::function<void()> onCancelCallback;

private slots:
    void onCancelClicked();

private:
    void setupUI();

    QProgressBar* m_overallProgress;
    QProgressBar* m_fileProgress;
    QLabel* m_statusLabel;
    QLabel* m_fileCountLabel;
    QPushButton* m_cancelButton;
    bool m_cancelled;

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

signals:
    void cancelled();
};

// Batch decryption progress dialog
class BatchDecryptionProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchDecryptionProgressDialog(QWidget* parent = nullptr);

    void setOverallProgress(int percentage);
    void setFileProgress(int percentage);
    void setStatusText(const QString& text);
    void setFileCountText(const QString& text);
    bool wasCancelled() const;

    std::function<void()> onCancelCallback;

private slots:
    void onCancelClicked();

private:
    void setupUI();

    QProgressBar* m_overallProgress;
    QProgressBar* m_fileProgress;
    QLabel* m_statusLabel;
    QLabel* m_fileCountLabel;
    QPushButton* m_cancelButton;
    bool m_cancelled;

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

signals:
    void cancelled();
};

// Progress dialog class for secure deletion
class SecureDeletionProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SecureDeletionProgressDialog(QWidget* parent = nullptr);

    void setOverallProgress(int percentage);
    void setCurrentItem(const QString& itemName);
    void setStatusText(const QString& text);
    bool wasCancelled() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void onCancelClicked();

private:
    void setupUI();

    QProgressBar* m_overallProgress;
    QLabel* m_statusLabel;
    QLabel* m_currentItemLabel;
    QPushButton* m_cancelButton;
    bool m_cancelled;

signals:
    void cancelled();
};

#endif // ENCRYPTEDDATA_PROGRESSDIALOGS_H
