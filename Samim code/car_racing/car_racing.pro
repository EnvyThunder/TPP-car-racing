QT += core gui widgets opengl multimedia network
greaterThan(QT_MAJOR_VERSION, 5): QT += openglwidgets

CONFIG += c++17
TARGET = car_racing
TEMPLATE = app

# silence macOS "OpenGL is deprecated" warnings
DEFINES += GL_SILENCE_DEPRECATION

INCLUDEPATH += shared net

SOURCES += \
    soundclips.cpp \
    gl1shim.cpp \
    net/link.cpp \
    main.cpp \
    mainwindow.cpp \
    glwidget.cpp \
    carmodel.cpp \
    glwidget_online.cpp \
    glwidget_story.cpp \
    glwidget_profile.cpp \
    glwidget_account.cpp \
    net/raceserver.cpp \
    net/raceclient.cpp \
    shared/gamesim.cpp \
    shared/playermodel.cpp \
    shared/aidriver.cpp \
    shared/brain.cpp \
    shared/clone.cpp \
    shared/director.cpp \
    shared/roster.cpp \
    shared/track.cpp \
    shared/circuits.cpp \
    shared/netsnapshot.cpp

HEADERS += \
    soundclips.h \
    gl1shim.h \
    gl1redirect.h \
    net/link.h \
    mainwindow.h \
    glwidget.h \
    carmodel.h \
    pixelfont.h \
    audioengine.h \
    shared/simtypes.h \
    shared/gamesim.h \
    shared/playermodel.h \
    shared/aidriver.h \
    shared/brain.h \
    shared/clone.h \
    shared/track.h \
    net/protocol.h \
    net/raceserver.h \
    net/raceclient.h

macx:        LIBS += -framework OpenGL
unix:!macx:!wasm: LIBS += -lGL -lGLU
win32:       LIBS += -lopengl32 -lglu32

# The browser version (docs/browser/README.md): Qt for WebAssembly. No Qt Multimedia there - the
# browser's Web Audio plays the synthesizer (audioweb.cpp); the browser's WebSocket carries online
# races (net/link.cpp, emscripten/websocket.h).
wasm {
    QT -= multimedia
    SOURCES += audioweb.cpp
    SOURCES -= mainwindow.cpp
    HEADERS -= mainwindow.h
    LIBS += -lwebsocket.js -lidbfs.js       # (idbfs: saved files in IndexedDB, tools/wasm/index.html)
}

# the recorded sounds (sounds/README.md)
RESOURCES += sounds.qrc
