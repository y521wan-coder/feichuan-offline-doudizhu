# Dependencies.cmake - Find and configure third-party dependencies

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network Test)

# Qt auto-features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

qt_standard_project_setup()
