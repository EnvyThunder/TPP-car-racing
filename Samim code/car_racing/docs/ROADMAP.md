# Roadmap: from a 4 km sprint to an arcade racer

Agreed plan for growing the game into a full arcade racer in the spirit of games like
Asphalt Legends: adventurous circuits, jumps, wrecks and takedowns, bots that feel
alive, a proper lobby and real sound. Planned in October 2026; nothing in this document is
built yet unless a step says so.

The starting point is the version tagged **`v1-online`**: 5 straight 4 km maps, vs CPU,
split screen, and online races with CPU racers (see `docs/online-multiplayer/README.md`).
Since then every race holds **at most 6 cars** (vs CPU: you + 1-5 CPUs; online rooms:
6 racers, people and CPUs together), names are changed in the lobby (default CAR1,
CAR2, ...), and long names are drawn smaller to fit a fixed space.

## Rules for every step

- **Raster graphics only** (course requirement). Everything is drawn as pixels: OpenGL
  rasterising our triangles, textures we generate pixel by pixel, and our own pixel font.
  No SVG, vector shapes, QPainter paths or TrueType text.
- **Our own game.** We take genre ideas only, never another game's tracks, cars, names,
  music or look.
- **Same race for everyone online.** The host runs the real race; tracks are built
  identically on Windows and Mac (basic maths only, our own sin/cos where needed) and
  checked with a track fingerprint. The online version number goes up after each step.
- **1P and split screen keep working** after every step.
- **Proof before play:** headless test races (`tools/ai_bench.cpp`), a 6-car online race
  on one PC, screenshots, frame-time checks.
- **One milestone at a time:** design agreed in chat, build only after the go-ahead, local
  commit, the owner checks it, push to GitHub only when asked.

## Order

| Step | Content | Size |
|---|---|---|
| 1 | New circuit track system | Large (the foundation) |
| 2 | Action driving: nitro bar, drifting, wrecks, takedowns, tricks | Large |
| 3 | New bots: skill instead of speed, personalities, director, memory | Medium-large |
| 4 | Menus, profile, garage, online lobby, sound | Medium |
| 5 | Game modes | Medium |

Tracks come first because jumps, shortcuts and laps need them, and the bots should learn
the final tracks and use the step 2 moves from day one.

---

## Step 1: circuits

Every map becomes a **closed circuit of about 3-4 km, raced 1-5 laps (3 by default)**,
with hills, banked corners, tunnels, bridges, ramps with jumps and one shortcut per map.
The old straight maps are replaced.

**How a circuit is described**
- About 30-40 control points per map (position, height, banking, width, markers such as
  tunnel, bridge, ramp, shortcut). A smooth closed curve runs through them and is cut into
  1 m slices; each slice knows its 3D position, direction, sideways direction with banking,
  width and markers.
- Everything is placed by **s** (metres along the lap) and **d** (metres left/right of the
  centre). Road width follows the race type: 3 lanes for 2 cars, 5 for a pack (online,
  or vs CPU with 2+ CPUs).
- Each shortcut is its own short curve that leaves and rejoins the main road; its distance
  maps onto the main road so positions stay fair.

**Driving on it**
- Cars still move freely; each tick their s and d are found by searching near the previous
  position (fast, and never confuses two parts of the track that cross).
- Uphill slows a little, downhill speeds up. At a ramp lip or sharp crest the car becomes
  airborne (gravity, no steering, nitro still works); hard landings cost speed and shake the
  camera. No deadly gaps in step 1. Cars at different heights (bridge over road) never
  collide.

**Laps and positions**
- Start/finish line at s = 0 with the grid behind it (rows of up to 4). A lap counts when
  the car crosses the line forwards; crossing it backwards takes the lap away again. (The
  planned checkpoints turned out unnecessary: the car's s is found by a search near where it
  was, so it can only reach the line by driving the lap.)
- Position = laps done + distance into the lap; finished cars by finish time.
- HUD: real "LAP 2/3", lap time, best lap, position (replacing the fake lap number).
- Knocked-over cones and barrels return when no car is near; nitro cans reappear; traffic
  drives around the loop.

**Drawing**
- Road built once into stored chunks of about 100 m, drawn only when near and in front.
- Ground as a height map that blends into the road (visual only); tunnels with walls,
  ceiling, lights and darker lighting; bridges with pillars and water or a gorge below.
- Camera follows height and slope; minimap shows the whole loop and the shortcut;
  start/finish gate at the line.

**The circuits (our own designs)**

| Map | Lap | Main features |
|---|---|---|
| Sunset Valley (easy) | ~3.2 km | Rolling hills, crest jump, wooden bridge over a river, village chicane. Shortcut: farm dirt road |
| Canyon Run (medium) | ~3.6 km | Fast canyon-floor sweepers, tunnel through a mesa, banked hairpin climbing a plateau, ramp jump over a dry riverbed. Shortcut: slot canyon |
| Alpine Pass (hard) | ~3.4 km | Switchbacks with ~60 m of climb, avalanche gallery tunnel, stone bridge over a gorge, ski-jump crest. Shortcut: steep forest track |
| Neon City (hard) | ~3.3 km | Right-angle street corners, raised highway crossing over the street below, underground tunnel, ramp jump across a junction. Shortcut: car park |
| Coastal Highway (fast) | ~4.0 km | Long sea-side straights, banked cliff-top curves, harbour bridge, lighthouse tunnel. Shortcut: along the beach |

Layouts are approved from top-view and height-profile pictures (`tools/track_preview`)
before they are built in 3D.

**Milestones:** 1a flat loops (track system, s/d everywhere, laps, HUD, minimap) -
1b height (hills, banking, ground, slope physics, camera) - 1c jumps - 1d tunnels,
bridges, crossover, shortcuts - 1e the 5 finished circuits.

**Done: 1a to 1e** (see `docs/circuits/README.md`). The five finished circuits are raced
with their hills, banked corners, jump ramps, tunnels, bridges, Neon City's raised highway
and their shortcuts. Changed afterwards by the owner: Sunset Valley and Coastal Highway are
now 8.2 km sprints on the first game's road (no laps, small obstacles, steady traffic), and
the menu shows a big map card with the CPU level. Changed from the plan above, with the owner's agreement: a third of
the normal steering works in the air (so jumps can be longer and still land on curving
roads), and gravity is twice real gravity.

**Checks:** loops close; the road never runs into itself except at planned crossovers;
corner radius, slope and banking within limits; converting position to s/d and back gives
the same point; the CPU finishes every lap on every map at both widths; online corrections
stay low across jumps; frames at least as fast as before.

---

## Step 2: action driving

**Controls:** drift = Shift (split screen: player 1 Right Shift, player 2 Left Shift);
double-tap drift = 360 spin. Nitro stays on Enter/Space (split screen: Enter or / and Space).

**Nitro bar, earned by driving well:** drift (up to ~10%/s), near miss (+5%), slipstream
(small top-speed gain + ~3%/s), overtake (+4%), airtime (~8%/s), barrel roll (+20%),
360 spin (+25%), takedown (+30%), nitro can (+40%, now a bonus refill).

| Boost | How | Effect |
|---|---|---|
| Normal | Tap nitro | ~+25% top speed for ~1.5 s, uses a quarter of the bar |
| Perfect | Tap again while the marker is in the target zone | ~+35%, chainable |
| Shockwave | Double-tap with a full bar | ~+50% for ~2.5 s, empties the bar, knocks cars it touches |

**Drifting:** hold drift while steering: sharper turn, the back slides out, slight speed
loss; counter-steer or release to straighten.

**Air:** twisted ramps give barrel rolls (designed to finish before landing); nitro in the
air extends jumps; bad landings wreck.

**Wrecks:** hard hits at sharp angles, takedowns and bad landings wreck the car: ~2 s of
tumbling (sparks, pieces, smoke, orbiting camera, "WRECKED"), then back on the road at the
same place in a free lane at about half speed, then ~1.5 s as a blinking ghost. Rails and
glancing touches are still only bumps.

**Takedowns:** your front into a rival's side or rear with enough speed difference and
angle; the 360 spin and shockwave wreck any car they touch. Small traffic is knocked
flying (you lose ~20% speed); hitting a truck head-on wrecks you. Takedowns on by default,
with a switch; online feed of who took down whom.

**Feeling of speed:** wider view at speed, camera shake, speed streaks, coloured screen
edges for perfect and shockwave boosts, pop-ups (DRIFT, NEAR MISS, PERFECT, TAKEDOWN,
BARREL ROLL). Slow motion on your takedowns in single player only.

**Online:** the drift key is one more input bit; tap timing is judged per input tick on the
server (every input is used exactly once), so it is fair under lag. While your car is
wrecked your game shows the server's state, then prediction resumes.

**Milestones:** 2a drift + nitro bar - 2b wrecks - 2c takedowns - 2d 360 spin and barrel
rolls - 2e speed feel, pop-ups, sounds, tuning.

**Done ahead of the rest of step 2, asked for by the owner and the professor** (see
`docs/crashes-and-story/README.md`): wrecks come from a health bar on every car (over its
name plate and in the corner), and the professor's fair-play rule replaces takedowns: the
car that rams takes 85% of the damage, the car it hits 15%. A wreck tumbles and sometimes
throws its block-figure driver out, who runs back to the car; the car is back in 2-3 s and
ghosts for 1.5 s. Also new: block-figure drivers in every car and STORY MODE (a prologue,
five chapters against the Brick Barons crew, an ending).

**Targets:** ~1-2 wrecks per car per race, a few takedowns per race, every car finishes,
the bar fills about twice per lap for a good driver, no extra corrections online.

---

## Step 3: bots

**Core rule:** bots press the same keys in the same car physics. The level-based top-speed
bonus is removed; a bot wins only by driving better. Everything is offline (no AI service).

| Part | Rate | What it does |
|---|---|---|
| Senses | every tick | Sees cars and obstacles 0.1-0.4 s late with a little distance error; knows the track |
| Hands | 60/s | Follows a racing line and speed profile precomputed per circuit; skill = line precision, corner speed, brake accuracy, drift and perfect-boost timing |
| Tactics | 10/s | Scores moves (follow line, overtake left/right, defend, slipstream, shortcut, takedown, shockwave, spin, use/save nitro) with personality, mood and seeded randomness; commits for a moment |
| Personality and form | per race | Attacker, Late charger, Blocker, Wild card, Clean racer + a "form of the day" so skill ranges overlap |
| Mood | on events | Calm, Confident, Under pressure, Shaken, Revenge |
| Director | 1/s | Keeps a fight near every human; changes behaviour only; last lap is pure driving; never picks the winner |

- **Named bot roster** (~12 invented drivers) instead of CPU1-CPU7.
- **Memory** (saved per player name): skill rating from lap pace, last 10 results, streak,
  rival, habits (overtaking side, shortcut use, takedowns).
- **Choices:** Rookie / Pro / Legend (overlapping skill ranges, shown as 1-5 dots),
  Adaptive (matches the humans), Fair (director off). vs CPU: 1-5 bots (6 cars at most).
  Optional offline radio taunts (switchable). F3 debug view of each bot's mood and plan.
- **Online:** the host's server runs every bot; the director treats every human the same.

**Proof:** test drivers (novice, average, good, expert) in 1000+ headless races on all
circuits with 6 cars. Targets: win chance Rookie ~70%, Pro ~45%, Legend ~25%, Adaptive
45-50% at every level; never 0% or 100%; at least 3 lead changes per race on average;
at least half the races finish within 2 s of a rival; no bot wins more than 30%; no bot
ever faster than the physics allows.

**Milestones:** 3a fair hands and racing line - 3b personality, form, moods, mistakes,
tactics - 3c director, Fair switch, up to 5 bots - 3d roster, memory, Adaptive, lobby
choices, debug view - 3e tuning against the targets and playtest - 3f optional neural
network "hands" trained by evolution.

---

## Step 4: menus, lobby and sound

Found in `v1-online`: only the background music plays (all effect sounds in
`audioengine.h` are empty, mono, ~250 ms delay); "LOBBY & PROFILE" in the menu does
nothing; the profile card shows a made-up name.

- **Profile page:** editable name (also used online), rank title, races/wins/podiums,
  best lap per circuit, rival and record against them, takedown and near-miss totals, last
  10 results as a bar chart.
- **Garage:** car picker plus paint and rim colours (saved, used online). **Map cards** with
  the circuit shape, length, laps and feature icons. **Settings:** volumes, controls,
  takedowns, taunts, draw distance, full screen. Fades and slides between screens.
- **Online lobby:** every racer's car on a stage in the garage scene with name plate and
  ready light; room code; ping bars; bot dots and personality; host panel (map, laps,
  difficulty, bots, takedowns, taunts, later the mode); ready with R (starts when everyone
  is ready, or the host forces a start after 10 s); quick chat with ready-made messages.
- **Results:** podium for the top 3 with fireworks, table of times, best laps and
  takedowns, rank change, rematch.
- **Sound engine:** stereo, ~50 ms delay, own thread, separate volumes. Everything is
  synthesised by our own code (no downloaded audio): engine through 6 virtual gears,
  other cars with distance, stereo side and pass-by pitch drop, tyres and surfaces, wind,
  tunnel echo, bridge rumble, nitro per boost type and shockwave boom, layered crashes,
  event jingles, menu sounds, music per map that intensifies on the last lap and in battles.

**Milestones:** 4a sound engine and driving sounds - 4b music, menu sounds, settings -
4c profile, garage, map cards, menu cleanup - 4d online lobby - 4e results podium and
rematch. Sounds are rendered to WAV files for approval first; sound uses under 5% of one
core and is heard within ~60 ms of a key press.

---

## Step 5: game modes

| Mode | Rules | Winner |
|---|---|---|
| Race | Laps and positions | First across the line |
| Ghost race | Racers pass through each other (see-through); traffic and obstacles stay | First across the line |
| Time Attack | No bots or traffic; race your saved best-lap ghost; split times | Best lap; medals at ~3/6/10% from the racing-line time. Online: everyone as ghosts, best lap wins |
| Elimination | Last car out every 30 s (host: 20-45 s) until one is left; red warning and ticking | Last car left |
| Takedown race | 3 minutes; takedown +100, near miss +10, airtime and drifting a little, wrecked -50 | Most points |
| Cup | 3 circuits in a row, points 10-8-6-5-4-3-2-1, standings between races, podium | Most points |

Bots adapt per mode; the director stays out of the last 10 s before each knockout and the
last 30 s of a takedown race. Profile keeps stats per mode. One rule set per mode;
ghosts stored at ~30 samples per second (~100 KB per lap).

**Milestones:** 5a mode system + Ghost race + mode picker - 5b Time Attack - 5c Elimination
- 5d Takedown race - 5e Cup and guide.

**Checks:** every mode ends correctly; rankings, scores and ties are right; exactly one
car out per elimination round; ghosts retrace a lap within a few centimetres; online
results match on every screen.

---

## Progress after the plan

- Crashes, block-figure drivers, story: `docs/crashes-and-story/README.md`.
- Side ramps and skill-based landings, more danger in the last third and on the final lap,
  the hold-L boost, elimination after two wrecks within 12.8 km, the bounty, reckless CPUs,
  River Serpent (a sixth map), the story inside every mode, place and health in the corner,
  online version 14: `docs/jumps-boost-story/README.md`.

