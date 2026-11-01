# Draws every circuit from above plus its height profile as PNG pictures, and checks it
# (loop closed, no place where the road runs into itself, corners, slopes).
#   track_preview <output folder>
# The lap times come from the game's own simulation (shared/gamesim.cpp).
# Drawing is done pixel by pixel with our own raster code; Qt only saves the PNG files.
QT = core gui
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = track_preview
TEMPLATE = app

INCLUDEPATH += ../../shared ../..

SOURCES += \
    main.cpp \
    ../../shared/track.cpp \
    ../../shared/circuits.cpp \
    ../../shared/gamesim.cpp \
    ../../shared/playermodel.cpp \
    ../../shared/aidriver.cpp \
    ../../shared/brain.cpp \
    ../../shared/clone.cpp \
    ../../shared/director.cpp \
    ../../shared/roster.cpp

HEADERS += \
    ../../shared/track.h \
    ../../shared/gamesim.h \
    ../../shared/playermodel.h \
    ../../shared/aidriver.h \
    ../../shared/brain.h \
    ../../shared/clone.h \
    ../../pixelfont.h
