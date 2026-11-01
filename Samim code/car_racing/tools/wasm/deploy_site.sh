#!/bin/bash
# Puts the browser game on the AWS machine (docs/browser/README.md), from the game's folder, after
# tools/wasm/build_wasm.bat:
#   tools/wasm/deploy_site.sh <user@host> <key.pem> [build folder, default C:/Qt/tpp-wasm]
# Each file goes with a gzip copy beside it (Caddy sends that one: about 10 MB instead of 20). The
# new files replace the old ones in one step; the previous version stays in /var/www/tpp-game.old,
# so going back is "sudo mv /var/www/tpp-game.old /var/www/tpp-game" (after moving the new one away).
set -euo pipefail
HOST="${1:?usage: deploy_site.sh <user@host> <key.pem> [build folder]}"
KEY="${2:?key file}"
OUT="${3:-C:/Qt/tpp-wasm}"
SSH=(ssh -i "$KEY" -o StrictHostKeyChecking=accept-new "$HOST")
FILES=(index.html car_racing.js car_racing.wasm qtloader.js)
STAGE=$(mktemp -d)
for f in "${FILES[@]}"; do
    test -f "$OUT/$f" || { echo "missing $OUT/$f - run tools/wasm/build_wasm.bat"; exit 1; }
    cp "$OUT/$f" "$STAGE/$f"
    gzip -9 -k "$STAGE/$f"
done
PKG=$(mktemp -t site-XXXX.tgz)
tar -czf "$PKG" -C "$STAGE" .
rm -rf "$STAGE"
scp -i "$KEY" -o StrictHostKeyChecking=accept-new "$PKG" "$HOST:/tmp/tpp-site.tgz"
rm -f "$PKG"
"${SSH[@]}" bash -s <<'REMOTE'
set -euo pipefail
sudo rm -rf /var/www/tpp-game.new
sudo mkdir -p /var/www/tpp-game.new
sudo tar --no-same-owner -xzf /tmp/tpp-site.tgz -C /var/www/tpp-game.new
sudo chmod -R a+rX /var/www/tpp-game.new
if [ -d /var/www/tpp-game ]; then sudo rm -rf /var/www/tpp-game.old; sudo mv /var/www/tpp-game /var/www/tpp-game.old; fi
sudo mv /var/www/tpp-game.new /var/www/tpp-game
rm -f /tmp/tpp-site.tgz
ls -la /var/www/tpp-game
REMOTE
