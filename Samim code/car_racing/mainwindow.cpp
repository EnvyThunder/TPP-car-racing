#include "mainwindow.h"
#include <QKeyEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      canvas(new RasterCanvas(800, 600, this)),
      engine(new TopViewEngine(canvas, this)) {

    setWindowTitle("APEX TOP-VIEW RACING - NATIVE QT CPU RASTER (Project 3)");
    setCentralWidget(canvas);

    // Allow window to be resized and maximized
    resize(800, 600);
    setMinimumSize(400, 300);

    engine->start();
    canvas->setFocus();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F11) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
