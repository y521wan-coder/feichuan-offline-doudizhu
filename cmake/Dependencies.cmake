# Dependencies.cmake - Find and configure third-party dependencies

option(FPDZ_WINDOWS7_COMPAT "Build the Windows 7 compatible Qt 5 client" OFF)
if(FPDZ_WINDOWS7_COMPAT)
    set(FPDZ_QT_MAJOR 5)
    find_package(Qt5 5.15.2 REQUIRED COMPONENTS Core Widgets Network WebSockets Test)
    if(MSVC)
        add_compile_options(/utf-8 /Zc:char8_t-)
        add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
else()
    set(FPDZ_QT_MAJOR 6)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network WebSockets Test)
endif()
set(FPDZ_QT_PACKAGE "Qt${FPDZ_QT_MAJOR}")

# Qt auto-features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT FPDZ_WINDOWS7_COMPAT)
    qt_standard_project_setup()
endif()
