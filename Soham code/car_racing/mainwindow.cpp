#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    glWidget = new GLWidget(this);
    setCentralWidget(glWidget);
    resize(1440, 810);
    setWindowTitle("Temple Run Racers  |  P2 (left): WASD + SPACE   |   P1 (right): ARROWS + ENTER   |   R = restart");
    glWidget->setFocus(); // keyboard goes to the game immediately
}

MainWindow::~MainWindow() {}
