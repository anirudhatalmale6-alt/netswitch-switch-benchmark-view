#!/bin/sh
# 6GGW / NetSwitch — short-term ($0, self-signed) certificates for the switch.
# Generates:
#   * ec256   — a 256-bit ECDSA (prime256v1) cert  ← the modern "256-bit SSL certificate"
#   * rsa1467 — an RSA key of exactly 1467 bits (your spec) + cert
#   * rsa2048 — an RSA 2048-bit key + cert (the safe standard, for comparison)
# All are self-signed (cost $0) and short-term (7 days). CN/SAN = localhost + 127.0.0.1 so a client
# can fully verify the chain and the hostname during the TLS handshake.
set -e
cd "$(dirname "$0")"
mkdir -p certs
DAYS=7
SUBJ="/C=FI/O=6GGW NetSwitch/CN=localhost"
SAN="subjectAltName=DNS:localhost,IP:127.0.0.1"

echo "== ec256 — 256-bit ECDSA (prime256v1), short-term self-signed =="
openssl ecparam -name prime256v1 -genkey -noout -out certs/ec256.key
openssl req -x509 -new -key certs/ec256.key -days "$DAYS" -subj "$SUBJ" \
  -addext "$SAN" -out certs/ec256.crt

echo "== rsa1467 — RSA 1467-bit (your spec), short-term self-signed =="
openssl genrsa -out certs/rsa1467.key 1467 2>/dev/null
openssl req -x509 -new -key certs/rsa1467.key -days "$DAYS" -subj "$SUBJ" \
  -addext "$SAN" -out certs/rsa1467.crt

echo "== rsa2048 — RSA 2048-bit (standard), short-term self-signed =="
openssl genrsa -out certs/rsa2048.key 2048 2>/dev/null
openssl req -x509 -new -key certs/rsa2048.key -days "$DAYS" -subj "$SUBJ" \
  -addext "$SAN" -out certs/rsa2048.crt

echo
echo "== summary =="
for n in ec256 rsa1467 rsa2048; do
  keybits=$(openssl pkey -in certs/$n.key -text -noout 2>/dev/null | grep -oiE '\(([0-9]+) bit' | head -1 | tr -dc 0-9)
  alg=$(openssl x509 -in certs/$n.crt -noout -text | grep -m1 'Public Key Algorithm' | sed 's/.*: //')
  exp=$(openssl x509 -in certs/$n.crt -noout -enddate | sed 's/notAfter=//')
  fp=$(openssl x509 -in certs/$n.crt -noout -fingerprint -sha256 | sed 's/.*=//')
  printf "  %-8s  %-24s  %4s-bit  expires %s\n" "$n" "$alg" "${keybits:-?}" "$exp"
  printf "            sha256 %s\n" "$fp"
done
echo
echo "wrote certs/  (self-signed, \$0, ${DAYS}-day). Use with: ./ggw_secure --selftest --cert certs/ec256.crt --key certs/ec256.key"
