#ifndef RASTER_CANVAS_H
#define RASTER_CANVAS_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QKeyEvent>
#include <vector>
#include <unordered_set>
#include <cstdint>

class RasterCanvas : public QWidget {
    Q_OBJECT
public:
    explicit RasterCanvas(int width = 800, int height = 600, QWidget *parent = nullptr);
    ~RasterCanvas() override = default;

    int getCanvasWidth() const  { return canvasWidth; }
    int getCanvasHeight() const { return canvasHeight; }

    uint32_t* getBuffer() { return pixelBuffer.data(); }
    const uint32_t* getBuffer() const { return pixelBuffer.data(); }
    void clear(uint32_t color = 0xFF000000);

    bool isKeyPressed(int key) const;

signals:
    void keyPressedSignal(int key);
    void keyReleasedSignal(int key);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    int canvasWidth;
    int canvasHeight;
    std::vector<uint32_t> pixelBuffer;
    std::unordered_set<int> pressedKeys;
};

#endif // RASTER_CANVAS_H
