# Test bots: join a room by code and race with the CPU driver.
#   net_bots K7Q2 7            (find room K7Q2 on this network, add 7 bots)
#   net_bots K7Q2@127.0.0.1 3
QT = core network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = net_bots
TEMPLATE = app

INCLUDEPATH += ../../shared ../../net

SOURCES += \
    ../../net/link.cpp \
    main.cpp \
    ../../net/raceclient.cpp \
    ../../shared/gamesim.cpp \
    ../../shared/playermodel.cpp \
    ../../shared/aidriver.cpp \
    ../../shared/brain.cpp \
    ../../shared/clone.cpp \
    ../../shared/director.cpp \
    ../../shared/roster.cpp \
    ../../shared/track.cpp \
    ../../shared/circuits.cpp \
    ../../shared/netsnapshot.cpp

HEADERS += \
    ../../net/link.h \
    ../../net/raceclient.h \
    ../../net/protocol.h \
    ../../shared/gamesim.h \
    ../../shared/playermodel.h \
    ../../shared/aidriver.h \
    ../../shared/brain.h \
    ../../shared/clone.h \
    ../../shared/track.h \
    ../../shared/simtypes.h
