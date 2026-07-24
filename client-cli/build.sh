#!/bin/sh
# Build the 6GGW CLI client. Zero dependencies, one file.
set -e
cd "$(dirname "$0")"

# Linux / macOS:
g++ -std=c++17 -O2 ggw_cli.cpp -o ggw_cli
echo "built ./ggw_cli"

# Windows (cross-compile from Linux with mingw, if installed):
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_cli.cpp -o ggw_cli.exe -lws2_32 -static
  echo "built ./ggw_cli.exe (Windows)"
fi

# Windows natively with MSVC:
#   cl /std:c++17 /EHsc ggw_cli.cpp ws2_32.lib
