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
build_std dramm      dramm/ggw_dramm.cpp
build_std diskbench  diskbench/ggw_diskbench.cpp
build_std cputimer   cputimer/ggw_cputimer.cpp
build_std radiosig   cputimer/ggw_radiosig.cpp
# note: cputimer is header-only (cpu_mhz_timer.h) + a demo; the header is the shared
#       MHz timebase meant to be #included across modules. radiosig runs the client's
#       radio-parameter reduction on that timebase. Both build Linux + Windows.
# thermal has two real backends in one file: Linux/Android (sysfs, plain g++) and
# Windows PC (WMI, needs -lwbemuuid -lole32 -loleaut32). So it's NOT build_std —
# the mingw build links the WMI libs, the native build doesn't. See thermal/README.md.
if [ -f thermal/ggw_thermal.cpp ]; then
  if g++ -std=c++17 -O2 thermal/ggw_thermal.cpp -o thermal/ggw_thermal 2>/tmp/ggw_build_thermal.log; then
    echo "OK    thermal (Linux/Android sysfs) -> thermal/ggw_thermal"; ok=$((ok+1))
    if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
      x86_64-w64-mingw32-g++ -std=c++17 -O2 thermal/ggw_thermal.cpp -o thermal/ggw_thermal.exe \
        -static -lwbemuuid -lole32 -loleaut32 2>>/tmp/ggw_build_thermal.log \
        && echo "      + windows (WMI) thermal/ggw_thermal.exe"
    fi
  else
    echo "FAIL  thermal (see /tmp/ggw_build_thermal.log)"; fail=$((fail+1))
  fi
fi
# note: dramm/cpu_benchmark.{cpp,h} is the library unit compiled into server-cpp; ggw_dramm.cpp is the
#       standalone tight-kernel benchmark (its CUDA-C twin ggw_dramm.cu needs nvcc — built on the GPU box).
#       diskbench is the portable disk-I/O companion (DiskSpd-aligned), builds Linux + Windows.

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
# NetSwitch (Microsoft SQL Server) client — Windows-only (uses windows.h + the ODBC subsystem),
# so it builds only with mingw, not native g++. The NSIS installer is built if makensis is present.
if [ -f netswitch-sql/netswitch_sql.cpp ]; then
  if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    if x86_64-w64-mingw32-g++ -std=c++17 -O2 netswitch-sql/netswitch_sql.cpp \
         -o netswitch-sql/netswitch_sql.exe -static -lodbc32 2>/tmp/ggw_build_nsql.log; then
      echo "OK    netswitch-sql (Windows SQL Server client, ODBC) -> netswitch-sql/netswitch_sql.exe"; ok=$((ok+1))
      if command -v makensis >/dev/null 2>&1; then
        if ( cd netswitch-sql && makensis installer.nsi >/tmp/ggw_build_nsql_nsis.log 2>&1 ); then
          echo "      + installer netswitch-sql/NetSwitch-SQL-Setup.exe"
        else
          echo "      (installer skipped — see /tmp/ggw_build_nsql_nsis.log)"
        fi
      fi
    else
      echo "FAIL  netswitch-sql (see /tmp/ggw_build_nsql.log)"; fail=$((fail+1))
    fi
  else
    echo "SKIP  netswitch-sql (needs mingw x86_64-w64-mingw32-g++)"; skip=$((skip+1))
  fi
fi

echo
echo "built $ok, skipped $skip, failed $fail"
[ "$fail" -eq 0 ]
