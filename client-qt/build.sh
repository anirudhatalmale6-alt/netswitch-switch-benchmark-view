#!/bin/sh
# Build the 6GGW Qt desktop client.
#
# Normal way (what your machine / Qt Creator will do) — with a full Qt install:
#     cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   or open ggw-client.pro / CMakeLists.txt in Qt Creator and press Run.
#
# One-liner with a system Qt5 (no cmake), for reference:
#     g++ -std=c++17 -fPIC main.cpp -o ggw_client_qt \
#         $(pkg-config --cflags --libs Qt5Widgets Qt5Network)
#
# This script uses cmake:
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
echo "built -> build/ggw_client_qt  (run it, or set GGW_SERVER=http://host:8090 first)"
