# Dedicated room server (no graphics). Players create and join rooms on it with
# a room code, exactly like the server built into "HOST A RACE".
QT = core network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = turbo8_server
TEMPLATE = app

INCLUDEPATH += ../shared ../net

# the browsers' fast lane (WebRTC data channels), where libdatachannel is installed
exists(/usr/local/include/rtc/rtc.hpp) {
    DEFINES += HAVE_RTC
    INCLUDEPATH += /usr/local/include
    LIBS += -L/usr/local/lib -ldatachannel
    QMAKE_LFLAGS += -Wl,-rpath,/usr/local/lib
}

SOURCES += \
    ../net/link.cpp \
    main.cpp \
    ../net/raceserver.cpp \
    ../shared/gamesim.cpp \
    ../shared/playermodel.cpp \
    ../shared/aidriver.cpp \
    ../shared/brain.cpp \
    ../shared/clone.cpp \
    ../shared/director.cpp \
    ../shared/roster.cpp \
    ../shared/track.cpp \
    ../shared/circuits.cpp \
    ../shared/netsnapshot.cpp

HEADERS += \
    ../net/link.h \
    ../net/raceserver.h \
    ../net/protocol.h \
    ../shared/gamesim.h \
    ../shared/playermodel.h \
    ../shared/aidriver.h \
    ../shared/brain.h \
    ../shared/clone.h \
    ../shared/track.h \
    ../shared/simtypes.h
