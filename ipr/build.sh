#!/bin/sh
# Build the 6GGW IPR compositor. Zero dependencies, one file.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 ggw_ipr.cpp -o ggw_ipr
echo "built ./ggw_ipr"
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_ipr.cpp -o ggw_ipr.exe -static
  echo "built ./ggw_ipr.exe (Windows)"
fi
