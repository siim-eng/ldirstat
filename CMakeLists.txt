cmake_minimum_required(VERSION 3.20)
project(ldirstat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Core library (no Qt).
add_library(ldirstat_core STATIC
    src/core/scanner.cpp
    src/core/flamegraph.cpp
    src/core/filesystem.cpp
)
target_include_directories(ldirstat_core PUBLIC src/core)
target_link_libraries(ldirstat_core PRIVATE pthread)

# UI library (Qt).
add_library(ldirstat_ui STATIC
    src/ui/mainwindow.cpp
    src/ui/mainwindowbuilder.cpp
    src/ui/dirtreeview.cpp
    src/ui/filelistview.cpp
    src/ui/flamegraphwidget.cpp
    src/ui/scanprogresswidget.cpp
    src/ui/welcomewidget.cpp
)
target_include_directories(ldirstat_ui PUBLIC src/ui)
target_link_libraries(ldirstat_ui PUBLIC ldirstat_core Qt6::Widgets)

# Main application.
add_executable(LDirStat src/app/main.cpp src/app/resources.qrc)
target_link_libraries(LDirStat PRIVATE ldirstat_ui)

# Benchmarks (no Qt).
add_executable(scandirs bench/scandirs.cpp)
target_link_libraries(scandirs PRIVATE ldirstat_core)

add_executable(scanmounts bench/scanmounts.cpp)
target_link_libraries(scanmounts PRIVATE ldirstat_core)
