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
done
# The game's files go in v/<build>/, named by their own fingerprint: the site tells browsers to
# keep that folder for good (a new build is a new folder), and to ask about index.html each time.
BUILD=$(cat "$OUT/car_racing.js" "$OUT/car_racing.wasm" "$OUT/qtloader.js" | sha1sum | cut -c1-12)
# The site: the front page (tools/wasm/site) at /, the desktop game's ZIP at /download/, and the
# browser game (offline play) at /play/
mkdir -p "$STAGE/play/v/$BUILD" "$STAGE/download" "$STAGE/img"
for f in car_racing.js car_racing.wasm qtloader.js; do cp "$OUT/$f" "$STAGE/play/v/$BUILD/$f"; done
sed "s/__BUILD__/$BUILD/g" "$OUT/index.html" > "$STAGE/play/index.html"
cp tools/wasm/site/index.html "$STAGE/index.html"
cp tools/wasm/site/img/*.jpg "$STAGE/img/"
ZIP=dist/TPP-Car-Racing-desktop.zip
test -f "$ZIP" || { echo "missing $ZIP - package the desktop game first"; exit 1; }
# never someone else's music on the site: only the game's own (sounds/README.md)
if unzip -l "$ZIP" | grep -qi "private_\|\.mp3"; then echo "$ZIP holds an MP3 - not uploading it"; exit 1; fi
cp "$ZIP" "$STAGE/download/"
for f in "$STAGE/index.html" "$STAGE/play/index.html" "$STAGE/play/v/$BUILD/"*; do
    gzip -9 -k "$f"
    if command -v brotli >/dev/null; then brotli -q 11 -k "$f"; fi   # (smaller than gzip, for browsers that take it)
done
echo "build $BUILD"
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
# keep play/v/<build>/ for good in browsers, ask about the rest; brotli where the browser takes it
if grep -q "@build path /v/\*" /etc/caddy/Caddyfile; then       # (the game moved to /play/)
    sudo cp /etc/caddy/Caddyfile /etc/caddy/Caddyfile.bak
    sudo sed -i "s|@build path /v/\*|@build path /play/v/*|" /etc/caddy/Caddyfile
    sudo bash -c "set -a; . /etc/caddy/site.env; caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile" >/dev/null 2>&1 && sudo systemctl reload caddy || { sudo cp /etc/caddy/Caddyfile.bak /etc/caddy/Caddyfile; echo "caddy config kept as it was"; }
fi
if ! grep -q "path /play/v/\*" /etc/caddy/Caddyfile; then
    sudo cp /etc/caddy/Caddyfile /etc/caddy/Caddyfile.bak
    sudo python3 - <<'PY'
p = "/etc/caddy/Caddyfile"
s = open(p).read()
s = s.replace('''		header Cache-Control "no-cache"
		file_server {
			precompressed gzip
		}''', '''		@build path /play/v/*
		header @build Cache-Control "public, max-age=31536000, immutable"
		header !@build Cache-Control "no-cache"
		file_server {
			precompressed br gzip
		}''')
open(p, "w").write(s)
PY
    sudo bash -c "set -a; . /etc/caddy/site.env; caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile" >/dev/null 2>&1 && sudo systemctl reload caddy || { sudo cp /etc/caddy/Caddyfile.bak /etc/caddy/Caddyfile; echo "caddy config kept as it was"; }
fi
ls -la /var/www/tpp-game
REMOTE
