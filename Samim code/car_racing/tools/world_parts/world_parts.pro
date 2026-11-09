# Prints each map's track fingerprint part by part (to compare the server, the game and the browser)
QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = world_parts
TEMPLATE = app
INCLUDEPATH += ../../shared
SOURCES += main.cpp \
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
