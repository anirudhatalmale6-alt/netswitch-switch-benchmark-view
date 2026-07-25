# 6GGW / NetSwitch — secure transport (SSL/TLS + SSH tunnelling + RSA/ECDSA)

The switch and the mobile terminal talk over a real, verified TLS channel, optionally wrapped in an
SSH tunnel so it works through 5G / Wifi carrier NAT. Everything here is $0 (self-signed, short-term
certs) and verified end to end on this box.

## Pieces

| file | what it is |
|------|-----------|
| `gen_certs.sh` | makes the short-term ($0, 7-day) self-signed certs: EC-256, RSA-1467, RSA-2048 |
| `ggw_secure.cpp` | C++17 TLS server + client over OpenSSL — the code that folds into server-cpp + the client |
| `tunnel.sh` | generates SSH keys + the exact forward/reverse tunnel commands for mobile over 5G/Wifi6 |

## Certificates (short-term, $0, self-signed)

```
./gen_certs.sh
```
Produces three key/cert pairs in `certs/`:

- **ec256** — a **256-bit ECDSA** (prime256v1) cert. This is the "256-bit SSL certificate": modern,
  small, fast on mobile, ~equivalent to RSA-3072 in strength. **Recommended default.**
- **rsa2048** — RSA 2048-bit. The safe RSA standard; works everywhere.
- **rsa1467** — RSA at exactly 1467 bits (your spec). See the honest note below.

### Honest note on RSA 1467-bit

The 1467-bit key generates fine, but modern TLS **refuses it at the default security level** — OpenSSL
rejects any RSA key under 2048 bits ("ee key too small"). It only completes a handshake if you drop the
security level (`--seclevel 0`), which is **insecure** and I don't recommend shipping it. So:

- want a genuinely 256-bit cert → use **ec256** (it really is 256-bit, and it's strong);
- want RSA → use **rsa2048** (the floor TLS accepts).

I kept `rsa1467` in the generator so you can see this yourself, but the two I'd ship are ec256 / rsa2048.

## TLS channel

```
g++ -std=c++17 -O2 ggw_secure.cpp -o ggw_secure -lssl -lcrypto

# one-process handshake + full verify:
./ggw_secure --selftest --cert certs/ec256.crt --key certs/ec256.key

# real two-process (server = switch, client = mobile terminal):
./ggw_secure --server --port 8443 --cert certs/ec256.crt --key certs/ec256.key      # terminal 1
./ggw_secure --client --host 127.0.0.1 --port 8443 --ca certs/ec256.crt              # terminal 2
```
Negotiates **TLS 1.3 (TLS_AES_256_GCM_SHA384)**, requires + verifies the peer cert, checks the
hostname, then does an encrypted request/response. `--seclevel N` overrides the OpenSSL security level.

## SSH tunnelling (through 5G / Wifi carrier NAT)

```
./tunnel.sh
```
Generates an ed25519 + RSA-4096 keypair and prints the ready-to-run commands:

- **Forward** (terminal on mobile, switch on a server): `ssh -N -L 8443:localhost:8443 switch@SERVER`
- **Reverse** (switch on mobile behind CGNAT, via a relay): `ssh -N -R 8443:localhost:8443 relay@RELAY`

The TLS channel runs *inside* the tunnel — defence in depth. Keep-alive flags are included for flaky
mobile links (what autossh automates; plain ssh + those flags is enough).

## Wifi6 / 5G

TLS and SSH are transport-agnostic — they need only an IP route, so the same channel runs unchanged on
Wifi6 or 5G. The only thing 5G changes is NAT reachability, which the SSH tunnel solves.

## How it plugs into the switch

`ggw_secure.cpp` is the reference for wiring TLS into `server-cpp` (an `SSL_CTX` on the API listener
loads the cert/key) and into the Qt/CLI clients (load the cert as CA, verify, connect). Same OpenSSL
calls, same certs.
