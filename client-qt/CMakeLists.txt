cmake_minimum_required(VERSION 3.16)
project(ggw_client_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)          # harmless here (no Q_OBJECT), kept for Qt Creator convention
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Works with Qt6 (preferred) or Qt5. Qt Creator picks this up directly.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_executable(ggw_client_qt main.cpp)
target_link_libraries(ggw_client_qt PRIVATE
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Network)
