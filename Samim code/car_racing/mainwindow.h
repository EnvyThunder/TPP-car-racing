#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "raster_canvas.h"
#include "engine_topview.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    RasterCanvas *canvas;
    TopViewEngine *engine;
};

#endif // MAINWINDOW_H
