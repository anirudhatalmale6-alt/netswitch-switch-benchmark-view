#!/bin/sh
# Build the 6GGW secure-transport TLS demo. Needs OpenSSL dev (libssl-dev).
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 ggw_secure.cpp -o ggw_secure -lssl -lcrypto
echo "built ./ggw_secure"
