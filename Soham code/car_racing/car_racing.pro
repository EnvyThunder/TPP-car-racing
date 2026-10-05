QT += core gui widgets opengl
greaterThan(QT_MAJOR_VERSION, 5): QT += openglwidgets

CONFIG += c++17
TARGET = car_racing
TEMPLATE = app

# silence macOS "OpenGL is deprecated" warnings
DEFINES += GL_SILENCE_DEPRECATION

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    glwidget.cpp

HEADERS += \
    mainwindow.h \
    glwidget.h \
    pixelfont.h

macx:        LIBS += -framework OpenGL
unix:!macx:  LIBS += -lGL -lGLU
win32:       LIBS += -lopengl32 -lglu32
