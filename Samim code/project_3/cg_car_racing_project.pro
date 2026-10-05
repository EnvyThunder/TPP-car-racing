QT += core gui widgets

CONFIG += c++17 release

TARGET = TopViewRacing
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    raster_canvas.cpp \
    cg_math.cpp \
    track_topview.cpp \
    car_topview.cpp \
    rival_topview.cpp \
    engine_topview.cpp

HEADERS += \
    mainwindow.h \
    raster_canvas.h \
    cg_math.h \
    track_topview.h \
    car_topview.h \
    rival_topview.h \
    engine_topview.h
