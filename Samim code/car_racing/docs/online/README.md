# Online over the internet (Phase C)

Online races can now run on the game server on AWS instead of a player's PC, so friends
anywhere can race. They go over **WebSockets**, the only connection a browser can open, so the
browser version (Phase D) will join the same races. LAN play on one Wi-Fi works as before.

- **Address:** `wss://tppcarracing.duckdns.org/race` (AWS Mumbai) (Caddy does the TLS and passes it to the race
  server on 127.0.0.1:47810).
- **Free:** it runs on the same free-plan EC2 machine as the accounts API.

## C1: WebSocket support

- **`net/link.*`:** a small WebSocket layer of our own (RFC 6455). Qt WebSockets isn't part of
  this Qt install, so we didn't add a new module.
  - It covers the handshake, binary/ping/pong/close frames, fragmented messages, client masking
    and size limits.
  - The game's framed messages (`protocol.h`) ride binary WebSocket messages, so the race logic,
    snapshots and prediction are unchanged.
  - Plain TCP still goes through the same `Link`.
- **The race server** (`turbo8_server`):
  - `--ws PORT`: WebSocket connections, from 127.0.0.1 only;
  - `--max-rooms N`: "THE SERVER IS FULL – TRY AGAIN IN A FEW MINUTES";
  - at most 8 connections per address (the real one, from Caddy's X-Forwarded-For);
  - `--api URL`: a logged-in player's token is checked with the accounts API, they appear under
    their username, and the server sends their result to their account, so online times are
    server-measured on the leaderboard;
  - guests play as CAR1, CAR2… and don't go on the board.
- **In the game:**
  - ONLINE MULTIPLAYER has **HOST A RACE ON THIS PC** (Wi-Fi, as before), **HOST ON THE
    INTERNET SERVER** and **JOIN WITH A CODE**;
  - joining by code looks on the Wi-Fi first, then on the internet server, so one 4-letter code
    works for both;
  - `CODE@wss://host/race` works too.
- **Online version 21:** the join message carries the account token.

## C2: on AWS

- **Building and running:** `server/deploy_race_server.sh <user@host> <key.pem>` copies the
  sources, builds them on the server with Ubuntu's free Qt 6 (installed the first time) and runs
  the `car-racing-race` service with `--ws 47810 --max-rooms 4 --api http://127.0.0.1:3000`.
- **Routing:** the backend's Caddyfile sends `/race` to it.
- **SSH:** it is allowed only from the owner's current IP. When that changes (home and mobile
  IPs do), update the `car-racing-sg` rule to "My IP" before deploying.

## Checks

| Check | Result |
|---|---|
| bots over `ws://`, on this PC | full race, results, track fingerprint matches |
| Node's built-in WebSocket client (an independent implementation) | handshake accepted, the server's reply arrives as a game frame, clean close (1000); also **live over wss://** |
| 5 hosts against `--max-rooms 4` | 4 rooms, the 5th told "THE SERVER IS FULL" |
| a logged-in bot (account token) | shown as its username; its result reached its account and the leaderboard |
| two game windows over `ws://` | 0 large corrections; snapshot gaps the same as over TCP |
| **live:** bots over `wss://` to AWS | full race, fingerprint matches, ping 204 ms from India |
| **live:** server CPU while a room races | **about 1% of one core** (memory 284 MB used of 908) |
| **live:** two game windows through AWS | raced; 0 large corrections; host ping 203 ms |

**Notes:**

- **Bots at 200 ms ping:** the test bots finished behind the CPUs at that ping. The same happens
  over plain TCP with 200 ms of simulated lag, so it's the bots' simple driving with old
  positions, not the server.
- **The guest window's 565 ms:** in the two-window live test it measured a 565 ms ping (two
  windows starting on one PC). Worth watching in real play.
- **Room limit:** 4 rooms is cautious. With 1% CPU per room, it can go up later.

## Smoothness (no change to the protocol)

- The other cars' buffer follows the connection: 60-150 ms instead of a fixed 80 ms, from how late snapshots
  arrive. With nothing newer to show (a stalled update) a car keeps moving for up to 120 ms.
- Corrections of your own car fade by time, not by frames, and larger ones take a little longer.
- **F2** shows FPS, the slowest frame, graphics level, ping, jitter, buffer, stalls, late updates and corrections.
- Graphics are automatic (`gfxFrame` in glwidget.cpp): over about 22 ms a frame or 12% of frames above 33 ms, one
  level down (0-3: view distance, trees and houses, particles, skid marks, and in the browser the resolution);
  smooth for 20 s or more, one level up. The player never chooses. `?gfx=N` on the page holds a level for tests.
- Test with a poor connection (40 ms each way, up to 40 ms jitter, a 200 ms stall every 5 s, one short race):
  frames with nothing newer to show went from 22 to 13 of 939 against the old fixed buffer.

**The pull-backs (found with a 400 ms stall both ways every 5 s, like TCP resending a lost packet).** Inputs that waited
in a stall arrived together, the server kept only 0.2 s of them and dropped the rest, and each dropped input pulled the
player's car back about a metre. The server now keeps a whole second. Same test: 58 small and 12 large corrections before,
0 and 0 after (two runs). The browser's resolution step also never ran in online races; it does now.
