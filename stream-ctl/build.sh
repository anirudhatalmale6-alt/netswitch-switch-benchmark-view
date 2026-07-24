#!/bin/sh
# Build the 6GGW stream delivery control tool. Zero dependencies, one file.
set -e
cd "$(dirname "$0")"

g++ -std=c++17 -O2 ggw_streamctl.cpp -o ggw_streamctl
echo "built ./ggw_streamctl"

# Windows (cross-compile from Linux with mingw, if installed):
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_streamctl.cpp -o ggw_streamctl.exe -static
  echo "built ./ggw_streamctl.exe (Windows)"
fi

# Windows natively with MSVC:
#   cl /std:c++17 /EHsc ggw_streamctl.cpp
