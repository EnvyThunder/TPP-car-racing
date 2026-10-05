cmake_minimum_required(VERSION 3.16)

project(TopViewRacing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 or fallback to Qt5
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)

add_executable(TopViewRacing
    main.cpp
    mainwindow.h
    mainwindow.cpp
    raster_canvas.h
    raster_canvas.cpp
    cg_math.h
    cg_math.cpp
    track_topview.h
    track_topview.cpp
    car_topview.h
    car_topview.cpp
    rival_topview.h
    rival_topview.cpp
    engine_topview.h
    engine_topview.cpp
)

target_link_libraries(TopViewRacing PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)
