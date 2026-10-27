#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    // Legacy (compatibility) OpenGL 2.1 context with depth buffer + 4x MSAA
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
