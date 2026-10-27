# Dedicated room server (no graphics). Players create and join rooms on it with
# a room code, exactly like the server built into "HOST A RACE".
QT = core network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = turbo8_server
TEMPLATE = app

INCLUDEPATH += ../shared ../net

SOURCES += \
    main.cpp \
    ../net/raceserver.cpp \
    ../shared/gamesim.cpp \
    ../shared/netsnapshot.cpp

HEADERS += \
    ../net/raceserver.h \
    ../net/protocol.h \
    ../shared/gamesim.h \
    ../shared/simtypes.h
