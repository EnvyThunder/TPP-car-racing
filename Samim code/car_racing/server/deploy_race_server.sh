#!/bin/bash
# Puts the online race server on the AWS machine (docs/online/README.md), from the game's folder:
#   server/deploy_race_server.sh <user@host> <key.pem>
# It copies the server's sources (server/, net/, shared/), builds them there with Ubuntu's Qt 6
# (installed the first time) and runs turbo8_server as the service car-racing-race:
# WebSocket connections on 127.0.0.1:47810 (Caddy passes wss://<site>/race to them), at most 4
# rooms, players' tokens checked with the accounts API on 127.0.0.1:3000.
set -euo pipefail
HOST="${1:?usage: deploy_race_server.sh <user@host> <key.pem>}"
KEY="${2:?key file}"
SSH=(ssh -i "$KEY" -o StrictHostKeyChecking=accept-new "$HOST")
cd "$(dirname "$0")/.."
PKG=$(mktemp -t race-XXXX.tgz)
tar -czf "$PKG" server/server.pro server/main.cpp net shared
scp -i "$KEY" -o StrictHostKeyChecking=accept-new "$PKG" "$HOST:/tmp/race-server.tgz"
rm -f "$PKG"
"${SSH[@]}" bash -s <<'EOF'
set -euo pipefail
if ! command -v qmake6 >/dev/null; then
    sudo apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq qt6-base-dev qt6-base-dev-tools build-essential >/dev/null
fi
id racing >/dev/null 2>&1 || sudo useradd --system --home-dir /var/lib/car-racing --shell /usr/sbin/nologin racing
rm -rf /tmp/race-build && mkdir -p /tmp/race-build && tar -xzf /tmp/race-server.tgz -C /tmp/race-build
cd /tmp/race-build/server
qmake6 server.pro CONFIG+=release >/dev/null
make -j2 -s 2>&1 | grep -E "error" || true
sudo mkdir -p /opt/car-racing-race
sudo install -m 755 turbo8_server /opt/car-racing-race/turbo8_server
sudo tee /etc/systemd/system/car-racing-race.service >/dev/null <<'UNIT'
[Unit]
Description=TPP car racing online race server (WebSocket rooms)
After=network-online.target car-racing-api.service
Wants=network-online.target

[Service]
User=racing
Group=racing
ExecStart=/opt/car-racing-race/turbo8_server 47800 --ws 47810 --max-rooms 4 --api http://127.0.0.1:3000
Restart=always
RestartSec=2
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
MemoryMax=300M

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable car-racing-race >/dev/null 2>&1
sudo systemctl restart car-racing-race
rm -rf /tmp/race-build /tmp/race-server.tgz
sleep 2
systemctl is-active car-racing-race
journalctl -u car-racing-race -n 4 --no-pager -o cat
EOF
