cmake_minimum_required(VERSION 3.20)
project(ldirstat VERSION 0.5.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(LDIRSTAT_INSTALL_COMPONENT App)
if(NOT DEFINED LDIRSTAT_RELEASE_DATE)
    string(TIMESTAMP LDIRSTAT_RELEASE_DATE "%Y-%m-%d" UTC)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging")
set(LDIRSTAT_METAINFO_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/packaging/io.github.siim_eng.ldirstat.desktop.metainfo.xml")
configure_file(packaging/linux/ldirstat.metainfo.xml.in
               "${LDIRSTAT_METAINFO_FILE}"
               @ONLY)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Core library (no Qt).
add_library(ldirstat_core STATIC
    src/core/scanner.cpp
    src/core/flamegraph.cpp
    src/core/modifiedtimehistogram.cpp
    src/core/treemap.cpp
    src/core/filesystem.cpp
)
target_include_directories(ldirstat_core PUBLIC src/core)
target_link_libraries(ldirstat_core PRIVATE pthread)

# UI library (Qt).
add_library(ldirstat_ui STATIC
    src/ui/mainwindow.cpp
    src/ui/mainwindowbuilder.cpp
    src/ui/dirlistview.cpp
    src/ui/dirlistcolumn.cpp
    src/ui/filecategorystatsdialog.cpp
    src/ui/graphwidget.cpp
    src/ui/flamegraphwidget.cpp
    src/ui/modifiedtimehistogramdialog.cpp
    src/ui/rangeslider.cpp
    src/ui/treemapwidget.cpp
    src/ui/scanprogresswidget.cpp
    src/ui/welcomewidget.cpp
)
target_include_directories(ldirstat_ui PUBLIC src/ui)
target_compile_definitions(ldirstat_ui PUBLIC LDIRSTAT_APP_VERSION=\"${PROJECT_VERSION}\")
target_link_libraries(ldirstat_ui PUBLIC ldirstat_core Qt6::Widgets)

# Main application.
add_executable(LDirStat src/app/main.cpp src/app/resources.qrc)
target_link_libraries(LDirStat PRIVATE ldirstat_ui)

install(TARGETS LDirStat
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

install(FILES packaging/linux/ldirstat.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

install(FILES "${LDIRSTAT_METAINFO_FILE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

install(FILES src/icons/app.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME ldirstat.svg
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

install(FILES packaging/linux/ldirstat.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

install(FILES LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ldirstat
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

install(FILES README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        COMPONENT ${LDIRSTAT_INSTALL_COMPONENT})

# Benchmarks (no Qt).
add_executable(scandirs bench/scandirs.cpp)
target_link_libraries(scandirs PRIVATE ldirstat_core)

add_executable(scanmounts bench/scanmounts.cpp)
target_link_libraries(scanmounts PRIVATE ldirstat_core)

add_executable(gentree bench/gentree.cpp)
target_link_libraries(gentree PRIVATE ldirstat_core)

# Static analysis: cmake -B build -DENABLE_CPPCHECK=ON
option(ENABLE_CPPCHECK "Run cppcheck during build" OFF)
if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_CXX_CPPCHECK
            ${CPPCHECK_EXE}
            --std=c++20
            --enable=warning,style,performance,portability
            --suppressions-list=${CMAKE_SOURCE_DIR}/.cppcheck-suppressions
            --library=qt
            -Dslots=
            -Dsignals=public
            -Demit=
            -DQ_OBJECT=
            --inline-suppr
            --quiet
            --error-exitcode=0
        )
        message(STATUS "cppcheck enabled: ${CPPCHECK_EXE}")
    else()
        message(WARNING "ENABLE_CPPCHECK is ON but cppcheck not found")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(filecategorizer_tests tests/filecategorizer_tests.cpp)
    target_include_directories(filecategorizer_tests PRIVATE third_party)
    target_link_libraries(filecategorizer_tests PRIVATE ldirstat_core)
    add_test(NAME filecategorizer_tests COMMAND filecategorizer_tests)

    add_executable(flamegraph_tests tests/flamegraph_tests.cpp)
    target_include_directories(flamegraph_tests PRIVATE third_party)
    target_link_libraries(flamegraph_tests PRIVATE ldirstat_core)
    add_test(NAME flamegraph_tests COMMAND flamegraph_tests)

    add_executable(scanner_tests tests/scanner_tests.cpp)
    target_include_directories(scanner_tests PRIVATE third_party)
    target_link_libraries(scanner_tests PRIVATE ldirstat_core)
    add_test(NAME scanner_tests COMMAND scanner_tests)

    add_executable(modifiedtimehistogram_tests tests/modifiedtimehistogram_tests.cpp)
    target_include_directories(modifiedtimehistogram_tests PRIVATE third_party)
    target_link_libraries(modifiedtimehistogram_tests PRIVATE ldirstat_core)
    add_test(NAME modifiedtimehistogram_tests COMMAND modifiedtimehistogram_tests)
endif()
