QT += core gui widgets opengl multimedia network
greaterThan(QT_MAJOR_VERSION, 5): QT += openglwidgets

CONFIG += c++17
TARGET = car_racing
TEMPLATE = app

# silence macOS "OpenGL is deprecated" warnings
DEFINES += GL_SILENCE_DEPRECATION

INCLUDEPATH += shared net

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    glwidget.cpp \
    carmodel.cpp \
    glwidget_online.cpp \
    net/raceserver.cpp \
    net/raceclient.cpp \
    shared/gamesim.cpp \
    shared/netsnapshot.cpp

HEADERS += \
    mainwindow.h \
    glwidget.h \
    carmodel.h \
    pixelfont.h \
    audioengine.h \
    shared/simtypes.h \
    shared/gamesim.h \
    net/protocol.h \
    net/raceserver.h \
    net/raceclient.h

macx:        LIBS += -framework OpenGL
unix:!macx:  LIBS += -lGL -lGLU
win32:       LIBS += -lopengl32 -lglu32
