# qmake project — open directly in Qt Creator (Qt.com IDE) and build/deploy to
# Windows, Linux, macOS, and with the mobile kits to native iOS and Android.
QT       += widgets network
CONFIG   += c++17
TARGET    = ggw_client_qt
TEMPLATE  = app
SOURCES  += main.cpp

# Android: Qt Creator generates the Gradle project; nothing extra needed here.
# iOS:     Qt Creator generates the Xcode project; set your signing team in Qt Creator.
