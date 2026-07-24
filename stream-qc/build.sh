#!/bin/sh
# Build the 6GGW stream quality-control tool. Zero dependencies, one file.
set -e
cd "$(dirname "$0")"

g++ -std=c++17 -O2 ggw_streamqc.cpp -o ggw_streamqc
echo "built ./ggw_streamqc"

# Windows (cross-compile from Linux with mingw, if installed):
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_streamqc.cpp -o ggw_streamqc.exe -static
  echo "built ./ggw_streamqc.exe (Windows)"
fi

# Windows natively with MSVC:
#   cl /std:c++17 /EHsc ggw_streamqc.cpp
