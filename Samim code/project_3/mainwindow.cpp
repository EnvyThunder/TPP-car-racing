#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      canvas(new RasterCanvas(800, 600, this)),
      engine(new TopViewEngine(canvas, this)) {

    setWindowTitle("APEX TOP-VIEW RACING - NATIVE QT CPU RASTER (Project 3)");
    setCentralWidget(canvas);
    setFixedSize(800, 600);

    engine->start();
    canvas->setFocus();
}
