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
    main.cpp \
    ../../net/raceclient.cpp \
    ../../shared/gamesim.cpp \
    ../../shared/netsnapshot.cpp

HEADERS += \
    ../../net/raceclient.h \
    ../../net/protocol.h \
    ../../shared/gamesim.h \
    ../../shared/simtypes.h
