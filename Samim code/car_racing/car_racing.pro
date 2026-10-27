QT += core gui widgets opengl multimedia
greaterThan(QT_MAJOR_VERSION, 5): QT += openglwidgets

CONFIG += c++17
TARGET = car_racing
TEMPLATE = app

# silence macOS "OpenGL is deprecated" warnings
DEFINES += GL_SILENCE_DEPRECATION

INCLUDEPATH += shared

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    glwidget.cpp \
    carmodel.cpp \
    shared/gamesim.cpp

HEADERS += \
    mainwindow.h \
    glwidget.h \
    carmodel.h \
    pixelfont.h \
    audioengine.h \
    shared/simtypes.h \
    shared/gamesim.h

macx:        LIBS += -framework OpenGL
unix:!macx:  LIBS += -lGL -lGLU
win32:       LIBS += -lopengl32 -lglu32
