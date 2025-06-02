#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QDialog>
#include <QPixmap>
#include <QLabel>
#include <QScrollArea>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QTimer>

namespace Ui {
class ImageViewer;
}

class ImageViewer : public QDialog
{
    Q_OBJECT

public:
    explicit ImageViewer(QWidget *parent = nullptr);
    ~ImageViewer();

    // Main functionality
    bool loadImage(const QString& imagePath);
    bool loadImage(const QPixmap& pixmap, const QString& title = "");

    // Zoom controls
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void actualSize();

    // Zoom factor management
    void setZoomFactor(double factor);
    double getZoomFactor() const;

    // Image info
    QSize getOriginalImageSize() const;
    bool hasImage() const;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void on_pushButton_ZoomIn_clicked();
    void on_pushButton_ZoomOut_clicked();
    void on_pushButton_FitToWindow_clicked();
    void on_pushButton_ActualSize_clicked();

private:
    Ui::ImageViewer *ui;

    // Image data
    QPixmap m_originalPixmap;
    QPixmap m_scaledPixmap;
    QString m_imagePath;

    // Zoom functionality
    double m_zoomFactor;
    double m_minZoomFactor;
    double m_maxZoomFactor;
    bool m_fitToWindowMode;
    bool m_firstShow;
    QTimer* m_fitToWindowTimer;

    // UI components (will be set up via .ui file)
    QLabel* m_imageLabel;
    QScrollArea* m_scrollArea;

    // Drag scrolling functionality
    bool m_dragging;
    QPoint m_lastDragPos;
    QCursor m_originalCursor;

    // Helper methods
    void updateImage();
    void updateZoomInfo();
    void centerImage();
    void calculateMinZoomFactor();
    void adjustScrollBar(QScrollBar* scrollBar, double factor);
    bool canDragImage() const;
    void updateCursor();

    // Constants
    static const double ZOOM_STEP;
    static const double MIN_ZOOM_FACTOR;
    static const double MAX_ZOOM_FACTOR;
};

#endif // IMAGEVIEWER_H
