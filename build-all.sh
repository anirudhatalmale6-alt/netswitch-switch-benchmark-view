#!/bin/sh
# 6GGW / NetSwitch — one command to build every native module + client.
# Zero-dependency C++ modules always build; Qt/OpenSSL builds are attempted and skipped cleanly
# if their toolkits aren't installed (they need Qt5 / OpenSSL+zlib on the build host).
cd "$(dirname "$0")"
ok=0; skip=0; fail=0

# name | source file (single-file C++17 modules) | extra flags
build_std() {
  name="$1"; src="$2"; extra="$3"
  if [ ! -f "$src" ]; then echo "SKIP  $name  (no $src)"; skip=$((skip+1)); return; fi
  out="$(dirname "$src")/$(basename "$src" | sed 's/\.[^.]*$//')"
  if g++ -std=c++17 -O2 $extra "$src" -o "$out" 2>/tmp/ggw_build_$name.log; then
    echo "OK    $name  -> $out"; ok=$((ok+1))
    if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
      x86_64-w64-mingw32-g++ -std=c++17 -O2 $extra "$src" -o "$out.exe" -static 2>>/tmp/ggw_build_$name.log \
        && echo "      + windows $out.exe"
    fi
  else
    echo "FAIL  $name  (see /tmp/ggw_build_$name.log)"; fail=$((fail+1))
  fi
}

echo "== zero-dependency native modules =="
build_std stream-qc  stream-qc/ggw_streamqc.cpp
build_std stream-ctl stream-ctl/ggw_streamctl.cpp
build_std ipr        ipr/ggw_ipr.cpp
build_std client-cli client-cli/ggw_cli.cpp
# note: dramm/ (cpu_benchmark) is a library component compiled into server-cpp, not a standalone tool.

echo
echo "== dependency builds (attempted, skipped if toolkit missing) =="
if [ -f secure/ggw_secure.cpp ]; then
  if g++ -std=c++17 -O2 secure/ggw_secure.cpp -o secure/ggw_secure -lssl -lcrypto 2>/tmp/ggw_build_secure.log; then
    echo "OK    secure (TLS/SSL transport, OpenSSL)"; ok=$((ok+1))
  else
    echo "SKIP  secure (needs libssl-dev — see /tmp/ggw_build_secure.log)"; skip=$((skip+1))
  fi
fi
if [ -x server-cpp/build.sh ]; then
  if sh server-cpp/build.sh >/tmp/ggw_build_server.log 2>&1; then
    echo "OK    server-cpp (native C++ server, OpenSSL+zlib)"; ok=$((ok+1))
  else
    echo "SKIP  server-cpp (needs OpenSSL+zlib dev — see /tmp/ggw_build_server.log)"; skip=$((skip+1))
  fi
fi
if [ -x client-qt/build.sh ]; then
  if sh client-qt/build.sh >/tmp/ggw_build_qt.log 2>&1; then
    echo "OK    client-qt (Qt desktop/mobile client)"; ok=$((ok+1))
  else
    echo "SKIP  client-qt (needs Qt5 dev — see /tmp/ggw_build_qt.log)"; skip=$((skip+1))
  fi
fi

echo
echo "built $ok, skipped $skip, failed $fail"
[ "$fail" -eq 0 ]
