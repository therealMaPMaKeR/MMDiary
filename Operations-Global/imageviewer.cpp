#include "imageviewer.h"
#include "ui_imageviewer.h"
#include <QScrollBar>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QShortcut>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>
#include <cmath>

// Constants
const double ImageViewer::ZOOM_STEP = 1.25;
const double ImageViewer::MIN_ZOOM_FACTOR = 0.1;
const double ImageViewer::MAX_ZOOM_FACTOR = 10.0;

ImageViewer::ImageViewer(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImageViewer),
    m_zoomFactor(1.0),
    m_minZoomFactor(MIN_ZOOM_FACTOR),
    m_maxZoomFactor(MAX_ZOOM_FACTOR),
    m_fitToWindowMode(false),
    m_firstShow(true),
    m_imageLabel(nullptr),
    m_scrollArea(nullptr),
    m_dragging(false)
{
    ui->setupUi(this);

    // Set window properties for non-modal behavior
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_DeleteOnClose); // Auto-delete when closed

    // Set default window size
    resize(800, 600);
    setMinimumSize(400, 300);

    // Initialize fit-to-window timer
    m_fitToWindowTimer = new QTimer(this);
    m_fitToWindowTimer->setSingleShot(true);
    m_fitToWindowTimer->setInterval(1000); // 1 second grace period

    // Store original cursor
    m_originalCursor = cursor();

    // Set window properties for non-modal behavior
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_DeleteOnClose); // Auto-delete when closed

    // Set default window size
    resize(800, 600);
    setMinimumSize(400, 300);

    // Get references to UI components (these will be created in the .ui file)
    m_imageLabel = ui->label_Image;
    m_scrollArea = ui->scrollArea_Image;

    // Configure image label
    if (m_imageLabel) {
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setScaledContents(false);
        m_imageLabel->setMinimumSize(1, 1);
    }

    // Configure scroll area (don't call setWidget - already set up in .ui file)
    if (m_scrollArea) {
        m_scrollArea->setWidgetResizable(false); // Important: let us control the widget size
        m_scrollArea->setAlignment(Qt::AlignCenter);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }

    // Set up keyboard shortcuts
    QShortcut* zoomInShortcut = new QShortcut(QKeySequence::ZoomIn, this);
    QShortcut* zoomOutShortcut = new QShortcut(QKeySequence::ZoomOut, this);
    QShortcut* actualSizeShortcut = new QShortcut(QKeySequence("Ctrl+0"), this);
    QShortcut* fitToWindowShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);

    connect(zoomInShortcut, &QShortcut::activated, this, &ImageViewer::zoomIn);
    connect(zoomOutShortcut, &QShortcut::activated, this, &ImageViewer::zoomOut);
    connect(actualSizeShortcut, &QShortcut::activated, this, &ImageViewer::actualSize);
    connect(fitToWindowShortcut, &QShortcut::activated, this, &ImageViewer::fitToWindow);

    // Update UI state
    updateZoomInfo();
}

ImageViewer::~ImageViewer()
{
    delete ui;
}

bool ImageViewer::loadImage(const QString& imagePath)
{
    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "Error", "Could not load image: " + imagePath);
        return false;
    }

    m_imagePath = imagePath;
    m_originalPixmap = pixmap;

    // Set window title
    QFileInfo fileInfo(imagePath);
    setWindowTitle(QString("Image Viewer - %1").arg(fileInfo.fileName()));

    // Reset zoom settings
    m_zoomFactor = 1.0;
    m_fitToWindowMode = false;
    m_firstShow = true;
    m_dragging = false; // Reset drag state

    calculateMinZoomFactor();
    updateImage();
    updateZoomInfo();

    return true;
}

bool ImageViewer::loadImage(const QPixmap& pixmap, const QString& title)
{
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "Error", "Invalid image data");
        return false;
    }

    m_originalPixmap = pixmap;
    m_imagePath.clear();

    // Set window title
    if (title.isEmpty()) {
        setWindowTitle("Image Viewer");
    } else {
        setWindowTitle(QString("Image Viewer - %1").arg(title));
    }

    // Reset zoom settings
    m_zoomFactor = 1.0;
    m_fitToWindowMode = false;
    m_firstShow = true;
    m_dragging = false; // Reset drag state

    calculateMinZoomFactor();
    updateImage();
    updateZoomInfo();

    return true;
}

void ImageViewer::zoomIn()
{
    if (m_originalPixmap.isNull()) return;

    if (m_zoomFactor < m_maxZoomFactor) {
        m_fitToWindowMode = false;
        setZoomFactor(m_zoomFactor * ZOOM_STEP);
    }
}

void ImageViewer::zoomOut()
{
    if (m_originalPixmap.isNull()) return;

    if (m_zoomFactor > m_minZoomFactor) {
        m_fitToWindowMode = false;
        setZoomFactor(m_zoomFactor / ZOOM_STEP);
    }
}

void ImageViewer::fitToWindow()
{
    if (m_originalPixmap.isNull()) return;

    m_fitToWindowMode = true;
    m_fitToWindowTimer->start(); // Start grace period for auto-updating
    calculateMinZoomFactor();
    setZoomFactor(m_minZoomFactor);
}

void ImageViewer::actualSize()
{
    if (m_originalPixmap.isNull()) return;

    m_fitToWindowMode = false;
    setZoomFactor(1.0);
}

void ImageViewer::setZoomFactor(double factor)
{
    factor = qBound(m_minZoomFactor, factor, m_maxZoomFactor);

    if (qAbs(factor - m_zoomFactor) < 0.001) return;

    m_zoomFactor = factor;
    updateImage();
    updateZoomInfo();
}

double ImageViewer::getZoomFactor() const
{
    return m_zoomFactor;
}

QSize ImageViewer::getOriginalImageSize() const
{
    return m_originalPixmap.size();
}

bool ImageViewer::hasImage() const
{
    return !m_originalPixmap.isNull();
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (m_originalPixmap.isNull()) {
        QDialog::wheelEvent(event);
        return;
    }

    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom with Ctrl + wheel
        int numDegrees = event->angleDelta().y() / 8;
        int numSteps = numDegrees / 15;

        if (numSteps > 0) {
            zoomIn();
        } else if (numSteps < 0) {
            zoomOut();
        }

        event->accept();
    } else {
        // Normal scrolling
        QDialog::wheelEvent(event);
    }
}

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }

    QDialog::keyPressEvent(event);
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);

    if (m_fitToWindowMode && !m_originalPixmap.isNull()) {
        // Only auto-update if we're in the grace period after fit-to-window was activated
        // or if this is the first show
        if (m_firstShow || m_fitToWindowTimer->isActive()) {
            calculateMinZoomFactor();
            setZoomFactor(m_minZoomFactor);
        } else {
            // Grace period expired - user is manually resizing, deactivate fit-to-window
            m_fitToWindowMode = false;
            updateZoomInfo();
        }
    }
}

void ImageViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // Auto-fit to window on first show if image is larger than dialog
    if (m_firstShow && !m_originalPixmap.isNull()) {
        m_firstShow = false;

        QSize imageSize = m_originalPixmap.size();
        QSize availableSize = m_scrollArea->viewport()->size();

        // Check if image is larger than the available space
        if (imageSize.width() > availableSize.width() ||
            imageSize.height() > availableSize.height()) {
            fitToWindow(); // This will start the timer
        } else {
            actualSize();
        }
    }
}

void ImageViewer::updateImage()
{
    if (m_originalPixmap.isNull() || !m_imageLabel) return;

    if (qAbs(m_zoomFactor - 1.0) < 0.001) {
        // Use original size
        m_scaledPixmap = m_originalPixmap;
    } else {
        // Scale the image
        QSize scaledSize = m_originalPixmap.size() * m_zoomFactor;
        m_scaledPixmap = m_originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    m_imageLabel->setPixmap(m_scaledPixmap);
    m_imageLabel->resize(m_scaledPixmap.size());

    // Update cursor based on whether the image can be dragged
    updateCursor();
}

void ImageViewer::updateZoomInfo()
{
    if (!ui->label_ZoomInfo) return;

    int percentage = static_cast<int>(m_zoomFactor * 100);
    QString zoomText = QString("%1%").arg(percentage);

    if (m_fitToWindowMode) {
        zoomText += " (Fit)";
    }

    ui->label_ZoomInfo->setText(zoomText);

    // Update button states
    if (ui->pushButton_ZoomIn) {
        ui->pushButton_ZoomIn->setEnabled(m_zoomFactor < m_maxZoomFactor);
    }
    if (ui->pushButton_ZoomOut) {
        ui->pushButton_ZoomOut->setEnabled(m_zoomFactor > m_minZoomFactor);
    }
}

void ImageViewer::calculateMinZoomFactor()
{
    if (m_originalPixmap.isNull() || !m_scrollArea) {
        m_minZoomFactor = MIN_ZOOM_FACTOR;
        return;
    }

    QSize imageSize = m_originalPixmap.size();
    QSize availableSize = m_scrollArea->viewport()->size();

    if (imageSize.isEmpty() || availableSize.isEmpty()) {
        m_minZoomFactor = MIN_ZOOM_FACTOR;
        return;
    }

    double scaleX = static_cast<double>(availableSize.width()) / imageSize.width();
    double scaleY = static_cast<double>(availableSize.height()) / imageSize.height();

    m_minZoomFactor = qMin(scaleX, scaleY);
    m_minZoomFactor = qMax(m_minZoomFactor, MIN_ZOOM_FACTOR);
}

void ImageViewer::on_pushButton_ZoomIn_clicked()
{
    zoomIn();
}

void ImageViewer::on_pushButton_ZoomOut_clicked()
{
    zoomOut();
}

void ImageViewer::on_pushButton_FitToWindow_clicked()
{
    fitToWindow();
}

void ImageViewer::on_pushButton_ActualSize_clicked()
{
    actualSize();
}

void ImageViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && canDragImage()) {
        m_dragging = true;
        m_lastDragPos = event->globalPosition().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QDialog::mousePressEvent(event);
    }
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint currentPos = event->globalPosition().toPoint();
        QPoint delta = currentPos - m_lastDragPos;

        // Scroll the scroll area by the delta
        if (m_scrollArea) {
            QScrollBar* hScrollBar = m_scrollArea->horizontalScrollBar();
            QScrollBar* vScrollBar = m_scrollArea->verticalScrollBar();

            if (hScrollBar) {
                hScrollBar->setValue(hScrollBar->value() - delta.x());
            }
            if (vScrollBar) {
                vScrollBar->setValue(vScrollBar->value() - delta.y());
            }
        }

        m_lastDragPos = currentPos;
        event->accept();
    } else {
        QDialog::mouseMoveEvent(event);
    }
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        updateCursor();
        event->accept();
    } else {
        QDialog::mouseReleaseEvent(event);
    }
}

bool ImageViewer::canDragImage() const
{
    if (!m_scrollArea || !m_imageLabel || m_scaledPixmap.isNull()) {
        return false;
    }

    QSize viewportSize = m_scrollArea->viewport()->size();
    QSize imageSize = m_scaledPixmap.size();

    // Can drag if image is larger than viewport in either dimension
    return (imageSize.width() > viewportSize.width() ||
            imageSize.height() > viewportSize.height());
}

void ImageViewer::updateCursor()
{
    if (m_dragging) {
        setCursor(Qt::ClosedHandCursor);
    } else if (canDragImage()) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(m_originalCursor);
    }
}
