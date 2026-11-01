#ifdef __EMSCRIPTEN__
#include "glwidget.h"
#else
#include "mainwindow.h"
#endif
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    // Legacy (compatibility) OpenGL 2.1 context with depth buffer + 4x MSAA
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
#ifdef __EMSCRIPTEN__
    // the browser: WebGL 2 (OpenGL ES 3); gl1shim draws the same as on the desktop
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(3, 0);
#else
    fmt.setSamples(4);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
#endif
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
#ifdef __EMSCRIPTEN__
    GLWidget w;                                          // (the browser: the game fills the page)
    w.setTitle("TPP Car Racing");
    w.showFullScreen();
#else
    MainWindow w;
    w.show();
#endif
    return a.exec();
}
