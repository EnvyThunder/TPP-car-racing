#include "raster_canvas.h"
#include <algorithm>

RasterCanvas::RasterCanvas(int width, int height, QWidget *parent)
    : QWidget(parent),
      canvasWidth(width),
      canvasHeight(height),
      pixelBuffer(width * height, 0xFF000000) {

    setFixedSize(width, height);
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
    QPainter painter(this);
    QImage image(reinterpret_cast<const uchar*>(pixelBuffer.data()),
                 canvasWidth, canvasHeight,
                 canvasWidth * sizeof(uint32_t),
                 QImage::Format_ARGB32_Premultiplied);

    painter.drawImage(0, 0, image);
}

void RasterCanvas::keyPressEvent(QKeyEvent *event) {
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
