#!/bin/sh
# Build the 6GGW C++ server. Two ways — pick either.
set -e
cd "$(dirname "$0")"

# 1) one-liner with g++ (no cmake needed):
#    g++ -std=c++17 -O2 ggw_server.cpp -o ggw_server -lssl -lcrypto -lz -lpthread

# 2) cmake (what this script does):
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cp build/ggw_server ./ggw_server
echo "built ./ggw_server  — run it with:  ./ggw_server   (PORT=8090 by default)"
