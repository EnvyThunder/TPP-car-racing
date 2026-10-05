#include "raster_canvas.h"
#include <algorithm>

RasterCanvas::RasterCanvas(int width, int height, QWidget *parent)
    : QWidget(parent),
      canvasWidth(width),
      canvasHeight(height),
      pixelBuffer(width * height, 0xFF000000) {

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
}

void RasterCanvas::clear(uint32_t color) {
    std::fill(pixelBuffer.begin(), pixelBuffer.end(), color);
}

bool RasterCanvas::isKeyPressed(int key) const {
    return pressedKeys.find(key) != pressedKeys.end();
}

void RasterCanvas::paintEvent(QPaintEvent * /*event*/) {
    if (canvasWidth <= 0 || canvasHeight <= 0 || pixelBuffer.empty()) return;
    QPainter painter(this);
    QImage image(reinterpret_cast<const uchar*>(pixelBuffer.data()),
                 canvasWidth, canvasHeight,
                 canvasWidth * sizeof(uint32_t),
                 QImage::Format_ARGB32_Premultiplied);

    painter.drawImage(rect(), image);
}

void RasterCanvas::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    int newW = width();
    int newH = height();
    if (newW > 0 && newH > 0 && (newW != canvasWidth || newH != canvasHeight)) {
        canvasWidth = newW;
        canvasHeight = newH;
        pixelBuffer.assign(canvasWidth * canvasHeight, 0xFF000000);
    }
}

void RasterCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F11) {
        if (window()->isFullScreen()) {
            window()->showNormal();
        } else {
            window()->showFullScreen();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && window()->isFullScreen()) {
        window()->showNormal();
        event->accept();
        return;
    }
    if (!event->isAutoRepeat()) {
        pressedKeys.insert(event->key());
        emit keyPressedSignal(event->key());
    }
}

void RasterCanvas::keyReleaseEvent(QKeyEvent *event) {
    if (!event->isAutoRepeat()) {
        pressedKeys.erase(event->key());
        emit keyReleasedSignal(event->key());
    }
}
