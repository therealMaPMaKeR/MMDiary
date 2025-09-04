#include "imageviewer.h"
#include "ui_imageviewer.h"
#include "inputvalidation.h"
#include "operations_files.h"
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
#include <QDebug>
#include <QImageReader>
#include <QBuffer>
#include <cmath>

// Constants
const double ImageViewer::ZOOM_STEP = 1.25;
const double ImageViewer::MIN_ZOOM_FACTOR = 0.1;
const double ImageViewer::MAX_ZOOM_FACTOR = 10.0;

// Security limits for image loading
const qint64 MAX_IMAGE_FILE_SIZE = 100 * 1024 * 1024; // 100MB max file size
const int MAX_IMAGE_DIMENSION = 10000; // Max width or height in pixels
const int MAX_GIF_FILE_SIZE = 50 * 1024 * 1024; // 50MB max for animated images
const qint64 MAX_PIXEL_COUNT = 100000000; // 100 million pixels max (e.g., 10000x10000)
const int MAX_GIF_FRAMES = 1000; // Maximum frames in animated images

ImageViewer::ImageViewer(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImageViewer),
    m_movie(nullptr),
    m_isAnimated(false),
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

    // Get references to UI components (these will be created in the .ui file)
    m_imageLabel = ui->label_Image;
    m_scrollArea = ui->scrollArea_Image;

    // Configure image label
    if (m_imageLabel) {
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setScaledContents(false);
        m_imageLabel->setMinimumSize(1, 1);
        m_imageLabel->setMouseTracking(true); // Enable mouse tracking for cursor changes
    }

    // Configure scroll area (don't call setWidget - already set up in .ui file)
    if (m_scrollArea) {
        m_scrollArea->setWidgetResizable(false); // Important: let us control the widget size
        m_scrollArea->setAlignment(Qt::AlignCenter);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setMouseTracking(true); // Enable mouse tracking

        // Remove the image label from the layout and set it directly as scroll area widget
        if (m_imageLabel) {
            m_imageLabel->setParent(nullptr); // Remove from layout
            m_scrollArea->setWidget(m_imageLabel); // Set directly as scroll area widget
            m_imageLabel->installEventFilter(this); // Install event filter for mouse handling
        }

        // Install event filter on scroll area as well
        m_scrollArea->installEventFilter(this);
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
    cleanupMovie();
    delete ui;
}

bool ImageViewer::loadImage(const QString& imagePath)
{
    qDebug() << "ImageViewer: Loading image:" << imagePath;
    qDebug() << "ImageViewer: File exists:" << QFile::exists(imagePath);

    // Security: Validate file path using InputValidation
    InputValidation::ValidationResult pathResult = 
        InputValidation::validateInput(imagePath, InputValidation::InputType::ExternalFilePath, 1000);
    if (!pathResult.isValid) {
        qWarning() << "ImageViewer: Invalid image path:" << pathResult.errorMessage;
        QMessageBox::warning(this, "Security Error", "Invalid file path: " + pathResult.errorMessage);
        return false;
    }

    // Security: Check file size before loading
    QFileInfo fileInfo(imagePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "ImageViewer: File does not exist or is not a regular file:" << imagePath;
        QMessageBox::warning(this, "Error", "File does not exist or is not accessible");
        return false;
    }

    qint64 fileSize = fileInfo.size();
    qDebug() << "ImageViewer: File size:" << fileSize << "bytes";
    
    // Security: Enforce file size limits
    if (fileSize > MAX_IMAGE_FILE_SIZE) {
        qWarning() << "ImageViewer: Image file too large:" << fileSize << "bytes (max:" << MAX_IMAGE_FILE_SIZE << ")";
        QMessageBox::warning(this, "Security Error", 
            QString("Image file is too large. Maximum size is %1 MB").arg(MAX_IMAGE_FILE_SIZE / (1024*1024)));
        return false;
    }

    // Clean up any existing movie
    cleanupMovie();

    // Check if this is an animated image file
    bool isAnimated = isAnimatedImageFile(imagePath);
    qDebug() << "ImageViewer: Is animated file:" << isAnimated;

    if (isAnimated) {
        qDebug() << "ImageViewer: Detected as animated image (GIF)";
        
        // Security: Additional size check for animated images
        if (fileSize > MAX_GIF_FILE_SIZE) {
            qWarning() << "ImageViewer: Animated image file too large:" << fileSize << "bytes (max:" << MAX_GIF_FILE_SIZE << ")";
            QMessageBox::warning(this, "Security Error", 
                QString("Animated image file is too large. Maximum size is %1 MB").arg(MAX_GIF_FILE_SIZE / (1024*1024)));
            return false;
        }

        // Load as animated image
        setupMovie(imagePath);
        if (!m_movie || !m_movie->isValid()) {
            qDebug() << "ImageViewer: Failed to create valid QMovie";
            QMessageBox::warning(this, "Error", "Could not load animated image: " + imagePath);
            return false;
        }

        // Security: Check frame count
        int frameCount = m_movie->frameCount();
        if (frameCount > MAX_GIF_FRAMES) {
            qWarning() << "ImageViewer: Too many frames in animated image:" << frameCount << "(max:" << MAX_GIF_FRAMES << ")";
            cleanupMovie();
            QMessageBox::warning(this, "Security Error", 
                QString("Animated image has too many frames. Maximum is %1").arg(MAX_GIF_FRAMES));
            return false;
        }

        qDebug() << "ImageViewer: QMovie created successfully. Frame count:" << frameCount;
        qDebug() << "ImageViewer: Movie state:" << m_movie->state();
        qDebug() << "ImageViewer: Original movie size:" << m_originalMovieSize;
        
        // Security: Validate movie dimensions
        if (m_originalMovieSize.width() > MAX_IMAGE_DIMENSION || 
            m_originalMovieSize.height() > MAX_IMAGE_DIMENSION) {
            qWarning() << "ImageViewer: Animated image dimensions too large:" << m_originalMovieSize;
            cleanupMovie();
            QMessageBox::warning(this, "Security Error", 
                QString("Image dimensions exceed maximum allowed (%1x%1 pixels)").arg(MAX_IMAGE_DIMENSION));
            return false;
        }

        m_isAnimated = true;
        m_originalPixmap = QPixmap(); // Clear static image
    } else {
        qDebug() << "ImageViewer: Loading as static image";

        // Security: Use QImageReader for better control and validation
        QImageReader reader(imagePath);
        
        // Get image size without loading the full image
        QSize imageSize = reader.size();
        qDebug() << "ImageViewer: Image dimensions:" << imageSize;
        
        // Security: Validate image dimensions before loading
        if (!imageSize.isValid()) {
            qWarning() << "ImageViewer: Invalid image dimensions";
            QMessageBox::warning(this, "Error", "Invalid image format or corrupted file");
            return false;
        }
        
        if (imageSize.width() > MAX_IMAGE_DIMENSION || imageSize.height() > MAX_IMAGE_DIMENSION) {
            qWarning() << "ImageViewer: Image dimensions too large:" << imageSize;
            QMessageBox::warning(this, "Security Error", 
                QString("Image dimensions exceed maximum allowed (%1x%1 pixels)").arg(MAX_IMAGE_DIMENSION));
            return false;
        }
        
        // Security: Check total pixel count to prevent memory exhaustion
        qint64 pixelCount = static_cast<qint64>(imageSize.width()) * static_cast<qint64>(imageSize.height());
        if (pixelCount > MAX_PIXEL_COUNT) {
            qWarning() << "ImageViewer: Total pixel count too large:" << pixelCount;
            QMessageBox::warning(this, "Security Error", "Image resolution is too high");
            return false;
        }
        
        // Load the image with size constraints
        QImage image = reader.read();
        if (image.isNull()) {
            qWarning() << "ImageViewer: Failed to load image:" << reader.errorString();
            QMessageBox::warning(this, "Error", "Could not load image: " + reader.errorString());
            return false;
        }
        
        // Convert to pixmap
        QPixmap pixmap = QPixmap::fromImage(image);
        if (pixmap.isNull()) {
            qWarning() << "ImageViewer: Failed to convert image to pixmap";
            QMessageBox::warning(this, "Error", "Could not process image");
            return false;
        }
        
        qDebug() << "ImageViewer: Static image loaded successfully. Size:" << pixmap.size();
        m_originalPixmap = pixmap;
        m_isAnimated = false;
    }

    m_imagePath = imagePath;

    // Set window title (reuse existing fileInfo variable)
    setWindowTitle(QString("Image Viewer - %1").arg(fileInfo.fileName()));

    // Reset zoom settings
    m_zoomFactor = 1.0;
    m_fitToWindowMode = false;
    m_firstShow = true;
    m_dragging = false; // Reset drag state

    calculateMinZoomFactor();
    updateImage();
    updateZoomInfo();

    qDebug() << "ImageViewer: Load completed. m_isAnimated:" << m_isAnimated;
    return true;
}

bool ImageViewer::loadImage(const QPixmap& pixmap, const QString& title)
{
    qDebug() << "ImageViewer: Loading QPixmap with title:" << title;
    qDebug() << "ImageViewer: Pixmap size:" << pixmap.size() << "isNull:" << pixmap.isNull();

    // Clean up any existing movie
    cleanupMovie();

    if (pixmap.isNull()) {
        qWarning() << "ImageViewer: Null pixmap provided";
        QMessageBox::warning(this, "Error", "Invalid image data");
        return false;
    }
    
    // Security: Validate pixmap dimensions
    QSize pixmapSize = pixmap.size();
    if (pixmapSize.width() > MAX_IMAGE_DIMENSION || pixmapSize.height() > MAX_IMAGE_DIMENSION) {
        qWarning() << "ImageViewer: Pixmap dimensions too large:" << pixmapSize;
        QMessageBox::warning(this, "Security Error", 
            QString("Image dimensions exceed maximum allowed (%1x%1 pixels)").arg(MAX_IMAGE_DIMENSION));
        return false;
    }
    
    // Security: Check total pixel count
    qint64 pixelCount = static_cast<qint64>(pixmapSize.width()) * static_cast<qint64>(pixmapSize.height());
    if (pixelCount > MAX_PIXEL_COUNT) {
        qWarning() << "ImageViewer: Pixmap pixel count too large:" << pixelCount;
        QMessageBox::warning(this, "Security Error", "Image resolution is too high");
        return false;
    }

    m_originalPixmap = pixmap;
    m_imagePath.clear();
    m_isAnimated = false;

    qDebug() << "ImageViewer: Set as static image (QPixmap overload)";

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
    if (!hasImage()) return;

    if (m_zoomFactor < m_maxZoomFactor) {
        m_fitToWindowMode = false;
        setZoomFactor(m_zoomFactor * ZOOM_STEP);
    }
}

void ImageViewer::zoomOut()
{
    if (!hasImage()) return;

    if (m_zoomFactor > m_minZoomFactor) {
        m_fitToWindowMode = false;
        setZoomFactor(m_zoomFactor / ZOOM_STEP);
    }
}

void ImageViewer::fitToWindow()
{
    if (!hasImage()) return;

    m_fitToWindowMode = true;
    m_fitToWindowTimer->start(); // Start grace period for auto-updating
    calculateMinZoomFactor();
    setZoomFactor(m_minZoomFactor);
}

void ImageViewer::actualSize()
{
    if (!hasImage()) return;

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
    if (m_isAnimated && m_movie) {
        return m_originalMovieSize;
    } else {
        return m_originalPixmap.size();
    }
}

bool ImageViewer::hasImage() const
{
    bool hasStatic = !m_originalPixmap.isNull();
    bool hasAnimated = (m_isAnimated && m_movie && m_movie->isValid());

    qDebug() << "hasImage: hasStatic=" << hasStatic << ", hasAnimated=" << hasAnimated;

    return hasStatic || hasAnimated;
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (!hasImage()) {
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

    if (m_fitToWindowMode && hasImage()) {
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
    if (m_firstShow && hasImage()) {
        m_firstShow = false;

        QSize imageSize = getOriginalImageSize();
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
    if (!hasImage() || !m_imageLabel) {
        qDebug() << "ImageViewer: updateImage - No image or no label";
        return;
    }

    qDebug() << "ImageViewer: updateImage - isAnimated=" << m_isAnimated << ", zoomFactor=" << m_zoomFactor;

    if (m_isAnimated && m_movie) {
        qDebug() << "ImageViewer: Updating animated image";

        // For animated images, just handle scaling
        QSize originalSize = m_originalMovieSize;
        if (qAbs(m_zoomFactor - 1.0) >= 0.001) {
            // Security: Calculate scaled size with bounds checking
            qint64 newWidth = static_cast<qint64>(originalSize.width() * m_zoomFactor);
            qint64 newHeight = static_cast<qint64>(originalSize.height() * m_zoomFactor);
            
            // Security: Prevent integer overflow and excessive memory usage
            if (newWidth > MAX_IMAGE_DIMENSION || newHeight > MAX_IMAGE_DIMENSION) {
                qWarning() << "ImageViewer: Scaled dimensions would exceed maximum:" << newWidth << "x" << newHeight;
                // Clamp to maximum dimensions
                double scaleRatio = qMin(
                    static_cast<double>(MAX_IMAGE_DIMENSION) / originalSize.width(),
                    static_cast<double>(MAX_IMAGE_DIMENSION) / originalSize.height()
                );
                newWidth = static_cast<qint64>(originalSize.width() * scaleRatio);
                newHeight = static_cast<qint64>(originalSize.height() * scaleRatio);
            }
            
            QSize scaledSize = QSize(static_cast<int>(newWidth), static_cast<int>(newHeight));
            qDebug() << "ImageViewer: Scaling movie from" << originalSize << "to" << scaledSize;
            m_movie->setScaledSize(scaledSize);
        } else {
            // Use original size
            qDebug() << "ImageViewer: Using original movie size:" << originalSize;
            m_movie->setScaledSize(originalSize);
        }

        // Update label size
        QSize currentSize = m_movie->scaledSize().isEmpty() ? originalSize : m_movie->scaledSize();
        m_imageLabel->resize(currentSize);
        qDebug() << "ImageViewer: Set label size to:" << currentSize;

        // Ensure movie is still running
        if (m_movie->state() != QMovie::Running) {
            qDebug() << "ImageViewer: Movie not running, starting it. Current state:" << m_movie->state();
            m_movie->start();
        }
    } else {
        qDebug() << "ImageViewer: Updating static image";

        // Handle static image
        // Clear any movie from the label first
        m_imageLabel->setMovie(nullptr);

        if (qAbs(m_zoomFactor - 1.0) < 0.001) {
            // Use original size
            m_scaledPixmap = m_originalPixmap;
        } else {
            // Security: Calculate scaled size with bounds checking
            QSize originalSize = m_originalPixmap.size();
            qint64 newWidth = static_cast<qint64>(originalSize.width() * m_zoomFactor);
            qint64 newHeight = static_cast<qint64>(originalSize.height() * m_zoomFactor);
            
            // Security: Prevent integer overflow and excessive memory usage
            if (newWidth > MAX_IMAGE_DIMENSION || newHeight > MAX_IMAGE_DIMENSION) {
                qWarning() << "ImageViewer: Scaled dimensions would exceed maximum:" << newWidth << "x" << newHeight;
                // Clamp to maximum dimensions
                double scaleRatio = qMin(
                    static_cast<double>(MAX_IMAGE_DIMENSION) / originalSize.width(),
                    static_cast<double>(MAX_IMAGE_DIMENSION) / originalSize.height()
                );
                newWidth = static_cast<qint64>(originalSize.width() * scaleRatio);
                newHeight = static_cast<qint64>(originalSize.height() * scaleRatio);
            }
            
            // Security: Check memory requirements before scaling (rough estimate: 4 bytes per pixel)
            qint64 estimatedMemory = newWidth * newHeight * 4;
            const qint64 MAX_MEMORY = 500 * 1024 * 1024; // 500MB max for scaled image
            if (estimatedMemory > MAX_MEMORY) {
                qWarning() << "ImageViewer: Scaled image would use too much memory:" << estimatedMemory << "bytes";
                return;
            }
            
            QSize scaledSize = QSize(static_cast<int>(newWidth), static_cast<int>(newHeight));
            m_scaledPixmap = m_originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_imageLabel->setPixmap(m_scaledPixmap);
        m_imageLabel->resize(m_scaledPixmap.size());
    }
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
    if (!hasImage() || !m_scrollArea) {
        m_minZoomFactor = MIN_ZOOM_FACTOR;
        return;
    }

    QSize imageSize = getOriginalImageSize();
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

bool ImageViewer::eventFilter(QObject *obj, QEvent *event)
{
    // Handle mouse events only for the image label and scroll area
    if ((obj == m_imageLabel || obj == m_scrollArea) && hasImage()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && canDragImage()) {
                m_dragging = true;
                m_lastDragPos = mouseEvent->globalPosition().toPoint();
                if (m_imageLabel) {
                    m_imageLabel->setCursor(Qt::ClosedHandCursor);
                }
                return true; // Event handled
            }
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

            if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton)) {
                // Handle dragging
                QPoint currentPos = mouseEvent->globalPosition().toPoint();
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
                return true; // Event handled
            } else {
                // Handle cursor changes on hover
                if (canDragImage()) {
                    if (m_imageLabel) {
                        m_imageLabel->setCursor(Qt::OpenHandCursor);
                    }
                } else {
                    if (m_imageLabel) {
                        m_imageLabel->setCursor(Qt::ArrowCursor);
                    }
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_dragging) {
                m_dragging = false;
                if (canDragImage()) {
                    if (m_imageLabel) {
                        m_imageLabel->setCursor(Qt::OpenHandCursor);
                    }
                } else {
                    if (m_imageLabel) {
                        m_imageLabel->setCursor(Qt::ArrowCursor);
                    }
                }
                return true; // Event handled
            }
            break;
        }
        case QEvent::Leave: {
            // Reset cursor when leaving the image area
            if (m_imageLabel && !m_dragging) {
                m_imageLabel->setCursor(Qt::ArrowCursor);
            }
            break;
        }
        case QEvent::Enter: {
            // Set appropriate cursor when entering the image area
            if (m_imageLabel && !m_dragging) {
                if (canDragImage()) {
                    m_imageLabel->setCursor(Qt::OpenHandCursor);
                } else {
                    m_imageLabel->setCursor(Qt::ArrowCursor);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    return QDialog::eventFilter(obj, event);
}

bool ImageViewer::canDragImage() const
{
    if (!m_scrollArea || !m_imageLabel) {
        return false;
    }

    QSize viewportSize = m_scrollArea->viewport()->size();
    QSize imageSize = getCurrentImageSize();

    // Can drag if image is larger than viewport in either dimension
    return (imageSize.width() > viewportSize.width() ||
            imageSize.height() > viewportSize.height());
}

void ImageViewer::updateCursor()
{
    // This method is now primarily used for cleanup
    // The actual cursor management is handled in the event filter
    if (!m_dragging && m_imageLabel) {
        if (canDragImage()) {
            m_imageLabel->setCursor(Qt::OpenHandCursor);
        } else {
            m_imageLabel->setCursor(Qt::ArrowCursor);
        }
    }
}

bool ImageViewer::isAnimatedImageFile(const QString& filePath) const
{
    qDebug() << "ImageViewer: Checking if animated:" << filePath;
    
    // Security: Validate the file path first
    if (filePath.isEmpty() || filePath.contains(QChar(0))) {
        qWarning() << "ImageViewer: Invalid file path for animated check";
        return false;
    }
    
    QString lowerPath = filePath.toLower();
    qDebug() << "ImageViewer: Lowercase path:" << lowerPath;

    // Check for GIF files
    bool isGif = lowerPath.endsWith(".gif");
    qDebug() << "ImageViewer: Is GIF?" << isGif;

    if (isGif) {
        return true;
    }

    // You could add other animated formats here if needed
    // if (lowerPath.endsWith(".webp") || lowerPath.endsWith(".apng")) {
    //     return true;
    // }

    return false;
}

void ImageViewer::cleanupMovie()
{
    if (m_movie) {
        qDebug() << "ImageViewer: Cleaning up movie. Current state:" << m_movie->state();

        // Stop the movie if it's running
        if (m_movie->state() == QMovie::Running) {
            m_movie->stop();
        }

        // Remove movie from label if it's set
        if (m_imageLabel && m_imageLabel->movie() == m_movie) {
            m_imageLabel->setMovie(nullptr);
        }

        // Delete the movie object
        delete m_movie;
        m_movie = nullptr;
        qDebug() << "ImageViewer: Movie cleaned up";
    }
    m_isAnimated = false;
    m_originalMovieSize = QSize();
}

void ImageViewer::setupMovie(const QString& filePath)
{
    qDebug() << "ImageViewer: Setting up QMovie for:" << filePath;

    cleanupMovie();
    
    // Security: Validate file path again
    InputValidation::ValidationResult pathResult = 
        InputValidation::validateInput(filePath, InputValidation::InputType::ExternalFilePath, 1000);
    if (!pathResult.isValid) {
        qWarning() << "ImageViewer: Invalid path for movie setup:" << pathResult.errorMessage;
        return;
    }

    // Create the movie
    m_movie = new QMovie(filePath, QByteArray(), this);

    if (!m_movie->isValid()) {
        qDebug() << "ImageViewer: QMovie is not valid for file:" << filePath;
        delete m_movie;
        m_movie = nullptr;
        return;
    }

    qDebug() << "ImageViewer: QMovie is valid. Frame count:" << m_movie->frameCount();
    qDebug() << "ImageViewer: Movie format:" << m_movie->format();

    // Connect to finished signal to loop the animation
    connect(m_movie, &QMovie::finished, m_movie, &QMovie::start);

    // Get the original size
    if (m_movie->frameCount() > 0) {
        m_movie->jumpToFrame(0);
        QPixmap firstFrame = m_movie->currentPixmap();
        
        // Security: Validate first frame dimensions
        if (!firstFrame.isNull()) {
            QSize frameSize = firstFrame.size();
            if (frameSize.width() > MAX_IMAGE_DIMENSION || frameSize.height() > MAX_IMAGE_DIMENSION) {
                qWarning() << "ImageViewer: First frame dimensions too large:" << frameSize;
                delete m_movie;
                m_movie = nullptr;
                return;
            }
            m_originalMovieSize = frameSize;
            qDebug() << "ImageViewer: Got size from first frame:" << m_originalMovieSize;
        }
    }

    // Fallback if we couldn't get size
    if (m_originalMovieSize.isEmpty()) {
        m_originalMovieSize = QSize(300, 300); // Default size
        qDebug() << "ImageViewer: Using default size:" << m_originalMovieSize;
    }

    // Set movie to label and start it
    if (m_imageLabel) {
        qDebug() << "ImageViewer: Setting movie to label";
        m_imageLabel->setMovie(m_movie);
        qDebug() << "ImageViewer: Starting movie";
        m_movie->start();
        qDebug() << "ImageViewer: Movie state after start:" << m_movie->state();
    } else {
        qDebug() << "ImageViewer: ERROR: m_imageLabel is null!";
    }
}

QSize ImageViewer::getCurrentImageSize() const
{
    if (m_isAnimated && m_movie) {
        return m_movie->scaledSize().isEmpty() ? m_originalMovieSize : m_movie->scaledSize();
    } else {
        return m_scaledPixmap.isNull() ? m_originalPixmap.size() : m_scaledPixmap.size();
    }
}

// Security-enhanced thumbnail generation function
QPixmap ImageViewer::createSecureThumbnail(const QString& imagePath, const QSize& maxSize)
{
    qDebug() << "ImageViewer: Creating secure thumbnail for:" << imagePath << "max size:" << maxSize;
    
    // Security: Validate input parameters
    if (imagePath.isEmpty() || !maxSize.isValid()) {
        qWarning() << "ImageViewer: Invalid parameters for thumbnail creation";
        return QPixmap();
    }
    
    // Security: Validate file path
    InputValidation::ValidationResult pathResult = 
        InputValidation::validateInput(imagePath, InputValidation::InputType::ExternalFilePath, 1000);
    if (!pathResult.isValid) {
        qWarning() << "ImageViewer: Invalid image path for thumbnail:" << pathResult.errorMessage;
        return QPixmap();
    }
    
    // Security: Check file existence and size
    QFileInfo fileInfo(imagePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "ImageViewer: File does not exist for thumbnail:" << imagePath;
        return QPixmap();
    }
    
    qint64 fileSize = fileInfo.size();
    // Use smaller limit for thumbnails
    const qint64 MAX_THUMBNAIL_FILE_SIZE = 20 * 1024 * 1024; // 20MB max for thumbnail source
    if (fileSize > MAX_THUMBNAIL_FILE_SIZE) {
        qWarning() << "ImageViewer: Source file too large for thumbnail:" << fileSize;
        return QPixmap();
    }
    
    // Security: Limit thumbnail dimensions
    const int MAX_THUMBNAIL_DIMENSION = 512; // Maximum thumbnail size
    int targetWidth = qMin(maxSize.width(), MAX_THUMBNAIL_DIMENSION);
    int targetHeight = qMin(maxSize.height(), MAX_THUMBNAIL_DIMENSION);
    QSize constrainedSize(targetWidth, targetHeight);
    
    // Use QImageReader for efficient thumbnail generation
    QImageReader reader(imagePath);
    
    // Get original dimensions without loading full image
    QSize originalSize = reader.size();
    if (!originalSize.isValid()) {
        qWarning() << "ImageViewer: Cannot read image dimensions for thumbnail";
        return QPixmap();
    }
    
    // Security: Check original dimensions
    if (originalSize.width() > MAX_IMAGE_DIMENSION || originalSize.height() > MAX_IMAGE_DIMENSION) {
        qWarning() << "ImageViewer: Source image dimensions too large for thumbnail:" << originalSize;
        return QPixmap();
    }
    
    // Calculate scaled size maintaining aspect ratio
    QSize scaledSize = originalSize.scaled(constrainedSize, Qt::KeepAspectRatio);
    
    // Set the reader to scale while loading (more memory efficient)
    reader.setScaledSize(scaledSize);
    
    // Security: Set quality for reasonable file size
    reader.setQuality(85);
    
    // Load the scaled image
    QImage thumbnail = reader.read();
    if (thumbnail.isNull()) {
        qWarning() << "ImageViewer: Failed to create thumbnail:" << reader.errorString();
        return QPixmap();
    }
    
    // Security: Final check on thumbnail dimensions
    if (thumbnail.width() > MAX_THUMBNAIL_DIMENSION || thumbnail.height() > MAX_THUMBNAIL_DIMENSION) {
        qWarning() << "ImageViewer: Generated thumbnail exceeds size limits";
        return QPixmap();
    }
    
    qDebug() << "ImageViewer: Thumbnail created successfully, size:" << thumbnail.size();
    return QPixmap::fromImage(thumbnail);
}

// Security-enhanced thumbnail generation from QPixmap
QPixmap ImageViewer::createSecureThumbnailFromPixmap(const QPixmap& sourcePixmap, const QSize& maxSize)
{
    qDebug() << "ImageViewer: Creating secure thumbnail from pixmap, max size:" << maxSize;
    
    // Security: Validate input parameters
    if (sourcePixmap.isNull() || !maxSize.isValid()) {
        qWarning() << "ImageViewer: Invalid parameters for thumbnail creation from pixmap";
        return QPixmap();
    }
    
    // Security: Check source dimensions
    QSize sourceSize = sourcePixmap.size();
    if (sourceSize.width() > MAX_IMAGE_DIMENSION || sourceSize.height() > MAX_IMAGE_DIMENSION) {
        qWarning() << "ImageViewer: Source pixmap too large for thumbnail:" << sourceSize;
        return QPixmap();
    }
    
    // Security: Limit thumbnail dimensions
    const int MAX_THUMBNAIL_DIMENSION = 512;
    int targetWidth = qMin(maxSize.width(), MAX_THUMBNAIL_DIMENSION);
    int targetHeight = qMin(maxSize.height(), MAX_THUMBNAIL_DIMENSION);
    QSize constrainedSize(targetWidth, targetHeight);
    
    // Calculate scaled size maintaining aspect ratio
    QSize scaledSize = sourceSize.scaled(constrainedSize, Qt::KeepAspectRatio);
    
    // Security: Check memory requirements (rough estimate: 4 bytes per pixel)
    qint64 estimatedMemory = static_cast<qint64>(scaledSize.width()) * static_cast<qint64>(scaledSize.height()) * 4;
    const qint64 MAX_THUMBNAIL_MEMORY = 10 * 1024 * 1024; // 10MB max for thumbnail
    if (estimatedMemory > MAX_THUMBNAIL_MEMORY) {
        qWarning() << "ImageViewer: Thumbnail would use too much memory:" << estimatedMemory;
        return QPixmap();
    }
    
    // Create the thumbnail
    QPixmap thumbnail = sourcePixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    if (thumbnail.isNull()) {
        qWarning() << "ImageViewer: Failed to create thumbnail from pixmap";
        return QPixmap();
    }
    
    qDebug() << "ImageViewer: Thumbnail created successfully from pixmap, size:" << thumbnail.size();
    return thumbnail;
}
