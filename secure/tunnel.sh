#!/bin/sh
# 6GGW / NetSwitch — SSH tunnelling for the switch over Wifi6 / 5G.
#
# Why a tunnel: on 5G (and most Wifi) the mobile side sits behind carrier NAT / CGNAT, so it has no
# inbound port. SSH gives us an authenticated, encrypted path through that NAT with no open firewall
# hole. The switch's TLS channel (ggw_secure) then runs *inside* the tunnel — defence in depth.
#
# Two directions, pick by where the switch runs:
#
#  A) Switch on a reachable SERVER, terminal on mobile  ->  FORWARD tunnel (from the phone):
#       ssh -i keys/ggw_ed25519 -N -L 8443:localhost:8443 switch@SERVER_HOST
#     then the terminal connects TLS to localhost:8443:
#       ./ggw_secure --client --host 127.0.0.1 --port 8443 --ca certs/ec256.crt
#
#  B) Switch on MOBILE (behind CGNAT), reachable via a small RELAY  ->  REVERSE tunnel (from switch):
#       ssh -i keys/ggw_ed25519 -N -R 8443:localhost:8443 relay@RELAY_HOST
#     now anyone who can reach RELAY_HOST:8443 lands on the switch's TLS listener.
#
# Keep-alive flags for flaky mobile links: add  -o ServerAliveInterval=15 -o ServerAliveCountMax=3
# (this is what autossh automates; plain ssh + these flags is enough for a demo).
set -e
cd "$(dirname "$0")"
mkdir -p keys

echo "== generating SSH keys for the switch <-> terminal =="
[ -f keys/ggw_ed25519 ]     || ssh-keygen -t ed25519 -N '' -C '6ggw-switch' -f keys/ggw_ed25519 >/dev/null
[ -f keys/ggw_rsa ]         || ssh-keygen -t rsa -b 4096 -N '' -C '6ggw-switch-rsa' -f keys/ggw_rsa >/dev/null
echo "  ed25519: $(ssh-keygen -lf keys/ggw_ed25519.pub)"
echo "  rsa4096: $(ssh-keygen -lf keys/ggw_rsa.pub)"

echo
echo "== install on the peer =="
echo "  Add keys/ggw_ed25519.pub to the server's ~/.ssh/authorized_keys (or the relay's)."
echo "  The phone/terminal keeps the private key keys/ggw_ed25519 (mode 600)."

echo
echo "== ready-to-run commands =="
KEEPALIVE="-o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes"
echo "  FORWARD (terminal on mobile, switch on server):"
echo "    ssh -i keys/ggw_ed25519 -N $KEEPALIVE -L 8443:localhost:8443 switch@SERVER_HOST"
echo "  REVERSE (switch on mobile, via relay):"
echo "    ssh -i keys/ggw_ed25519 -N $KEEPALIVE -R 8443:localhost:8443 relay@RELAY_HOST"

echo
echo "== syntax-validating the tunnel options with the local ssh (no connection made) =="
ssh -G -L 8443:localhost:8443 -o ServerAliveInterval=15 dummy-host >/dev/null && echo "  forward -L options: valid"
ssh -G -R 8443:localhost:8443 -o ExitOnForwardFailure=yes dummy-host >/dev/null && echo "  reverse -R options: valid"
echo
echo "wrote keys/  — the TLS channel (ggw_secure) runs inside whichever tunnel you pick."
