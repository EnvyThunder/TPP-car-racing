#include "gamesim.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace sim {

// Road shapes: amplitude (m), frequency (rad/m), phase for three sine waves.
// Tightest bend must stay above the car's turning circle at full speed
// (~37 m), so even ALPINE PASS is drivable flat-out by a perfect driver.
const MapDef MAPS[NUM_MAPS] = {
    {"SUNSET VALLEY",   "ROLLING COUNTRYSIDE AT GOLDEN HOUR", MapTheme::Countryside,
     {18.0f, 8.0f, 0.0f}, {0.012f, 0.031f, 0.0f}, {0.0f, 1.3f, 0.0f}, 16, 95.0f, 130.0f, 1},
    {"CANYON RUN",      "FAST SWEEPERS THROUGH RED ROCK DESERT", MapTheme::Desert,
     {26.0f, 9.0f, 3.0f}, {0.008f, 0.027f, 0.050f}, {0.5f, 2.0f, 0.7f}, 10, 105.0f, 145.0f, 2},
    {"ALPINE PASS",     "TIGHT SNOWY SWITCHBACKS - BRAKE EARLY", MapTheme::Snow,
     {14.0f, 7.0f, 0.0f}, {0.022f, 0.045f, 0.0f}, {0.0f, 1.0f, 0.0f}, 8, 110.0f, 150.0f, 3},
    {"NEON CITY",       "NIGHT STREETS - CHICANES - TRAFFIC", MapTheme::City,
     {12.0f, 5.0f, 2.5f}, {0.014f, 0.038f, 0.065f}, {0.3f, 2.4f, 1.1f}, 22, 95.0f, 125.0f, 3},
    {"COASTAL HIGHWAY", "FLAT-OUT OCEAN ROAD LINED WITH PALMS", MapTheme::Coast,
     {22.0f, 6.0f, 0.0f}, {0.010f, 0.024f, 0.0f}, {0.9f, 0.2f, 0.0f}, 14, 100.0f, 140.0f, 1},
};

GameSim::GameSim(uint32_t seed)
    : rng(seed)
{
    reset(seed);
}

float GameSim::rnd(float a, float b)
{
    return std::uniform_real_distribution<float>(a, b)(rng);
}

float GameSim::roadCenter(float z) const
{
    float d = -z;
    float ramp    = std::clamp((d - 80.0f) / 200.0f, 0.0f, 1.0f);
    float endRamp = std::clamp((TRACK_LEN + 60.0f - d) / 200.0f, 0.0f, 1.0f);
    const MapDef &m = MAPS[mapIndex];
    float s = 0.0f;
    for (int i = 0; i < 3; ++i)
        if (m.amp[i] != 0.0f) s += m.amp[i] * std::sin(d * m.freq[i] + m.phase[i]);
    return ramp * endRamp * s;
}

float GameSim::roadAngle(float z) const
{
    return std::atan2(roadCenter(z - 1.0f) - roadCenter(z), 1.0f);
}

void GameSim::startRace(GameMode m, int racers)
{
    mode = m;
    reset(2024, racers);
    state = Countdown;
}

void GameSim::reset(uint32_t seed, int racers)
{
    rng.seed(seed);
    cars.clear();

    // Starting grid: two columns, rows 9 m apart behind the start line.
    // Slots 0 and 1 are the original P1 (red, right) and P2 (blue, left).
    static const float GRID_COL[MAX_RACERS][3] = {
        {0.92f, 0.14f, 0.16f}, {0.15f, 0.42f, 0.98f}, {1.00f, 0.75f, 0.10f}, {0.20f, 0.85f, 0.35f},
        {0.85f, 0.30f, 0.95f}, {0.10f, 0.85f, 0.90f}, {0.95f, 0.50f, 0.15f}, {0.92f, 0.92f, 0.95f}};
    racers = std::clamp(racers, 1, MAX_RACERS);
    for (int i = 0; i < racers; ++i) {
        Car c;
        c.x = (i % 2 == 0) ? 3.0f : -3.0f;
        c.z = float(i / 2) * 9.0f;
        c.r = GRID_COL[i][0]; c.g = GRID_COL[i][1]; c.b = GRID_COL[i][2];
        cars.push_back(c);
    }

    tick = 0;
    raceTimer = 0.0f;
    winner = 0;
    traffic.clear();
    obstacles.clear();
    props.clear();
    nitroPickups.clear();
    events.clear();

    std::mt19937 gen(seed);
    auto R = [&gen](float a, float b) { return std::uniform_real_distribution<float>(a, b)(gen); };

    // ---- Collectible Nitro Pickups ----
    // Fewer, strategic nitro canisters spaced across the 4,000m track (approx 7-8 total)
    for (float d = 360.0f; d < TRACK_LEN - 220.0f; d += R(440.0f, 580.0f)) {
        NitroPickup np{};
        np.z = -d;
        int lane = int(R(0, 3)) % 3;
        np.x = roadCenter(np.z) + LANES[lane];
        np.y = 0.5f;
        np.active = true;
        nitroPickups.push_back(np);
    }

    auto addRound = [&](Obstacle::Type t, float d, float off) {
        Obstacle o{}; o.type = t; o.z = -d; o.x = roadCenter(o.z) + off;
        obstacles.push_back(o);
    };
    auto addBarrier = [&](float d, float off) {
        Obstacle o{}; o.type = Obstacle::Barrier; o.z = -d; o.x = roadCenter(o.z) + off;
        o.hx = 2.2f; o.hz = 0.6f;
        obstacles.push_back(o);
    };

    const MapDef &map = MAPS[mapIndex];

    // ---- Obstacle patterns ----
    for (float d = 240.0f; d < TRACK_LEN - 220.0f; d += R(map.obstacleGapMin, map.obstacleGapMax)) {
        int pattern = int(R(0.0f, 4.0f)) % 4;
        if (pattern == 0) { // cone slalom
            for (int i = 0; i < 6; ++i) addRound(Obstacle::Cone, d + i * 11.0f, (i % 2) ? 3.5f : -3.5f);
        } else if (pattern == 1) { // zig-zag barrier walls
            int open1 = int(R(0, 3)) % 3;
            int open2 = (open1 + 1 + int(R(0, 2)) % 2) % 3;
            for (int l = 0; l < 3; ++l) if (l != open1) addBarrier(d, LANES[l]);
            for (int l = 0; l < 3; ++l) if (l != open2) addBarrier(d + 38.0f, LANES[l]);
        } else if (pattern == 2) { // barrels
            for (int i = 0; i < 3; ++i) addRound(Obstacle::Barrel, d + R(0, 18), R(-5.5f, 5.5f));
        } else { // cone wall with one gap
            int gap = int(R(0, 3)) % 3;
            for (float x = -6.0f; x <= 6.01f; x += 1.5f)
                if (std::fabs(x - LANES[gap]) > 2.4f) addRound(Obstacle::Cone, d, x);
        }
    }

    // ---- Scenery (depends on the map's theme) ----
    auto addProp = [&](Prop::Type t, float d, float side, float off, float scale, int palette, float rot) {
        Prop p{}; p.type = t; p.z = -d; p.scale = scale; p.palette = palette; p.rot = rot;
        p.x = roadCenter(p.z) + side * (RAIL_OFFSET + off);
        props.push_back(p);
    };
    auto addLamps = [&](float spacing, bool bothSides) {
        int alt = 0;
        for (float d = 30.0f; d < TRACK_LEN - 220.0f; d += spacing, ++alt) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                if (!bothSides && side != ((alt % 2) ? 1.0f : -1.0f)) continue;
                Prop p{}; p.type = Prop::Lamp; p.z = -d; p.scale = 1.0f;
                p.x = roadCenter(p.z) + side * (ROAD_HALF + 2.2f);
                p.rot = (side < 0) ? 90.0f : -90.0f;
                props.push_back(p);
            }
        }
    };

    switch (map.theme) {
    case MapTheme::Countryside: {
        // houses + forest
        float lastHouse[2] = {-100.0f, -100.0f};
        for (float d = 20.0f; d < TRACK_LEN + 40.0f; d += R(12.0f, 20.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                if (R(0, 1) < 0.85f) {
                    Prop p{}; p.z = -d + R(-3, 3); p.scale = R(0.85f, 1.2f); p.palette = int(R(0, 4)) % 4;
                    float r = R(0, 1);
                    if (r < 0.4f && d - lastHouse[s] > 16.0f && d < TRACK_LEN - 220.0f) {
                        p.type = Prop::House; lastHouse[s] = d;
                        p.x = roadCenter(p.z) + side * (RAIL_OFFSET + 8.0f + R(0, 6));
                        p.rot = (side < 0) ? 90.0f : -90.0f;
                    } else {
                        p.type = (r < 0.72f) ? Prop::Pine : Prop::RoundTree;
                        float off = (d - lastHouse[s] < 10.0f) ? R(20, 30) : R(3, 25);
                        p.x = roadCenter(p.z) + side * (RAIL_OFFSET + off);
                        p.rot = R(0, 360);
                    }
                    props.push_back(p);
                }
                Prop b{}; // background forest row
                b.type = (R(0, 1) < 0.6f) ? Prop::Pine : Prop::RoundTree;
                b.z = -d + R(-6, 6);
                b.x = roadCenter(b.z) + side * (RAIL_OFFSET + 32.0f + R(0, 45));
                b.scale = R(1.0f, 1.6f); b.rot = R(0, 360);
                props.push_back(b);
            }
        }
        addLamps(50.0f, false);
        break;
    }
    case MapTheme::Desert:
        // cacti and boulders by the road, huge red mesas further out
        for (float d = 20.0f; d < TRACK_LEN + 40.0f; d += R(14.0f, 24.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                if (R(0, 1) < 0.75f) {
                    bool cactus = R(0, 1) < 0.6f;
                    addProp(cactus ? Prop::Cactus : Prop::Rock, d + R(-4, 4), side, R(3, 24),
                            cactus ? R(0.8f, 1.4f) : R(0.6f, 1.5f), int(R(0, 3)) % 3, R(0, 360));
                }
                if (R(0, 1) < 0.45f) {                   // mesa: radius is 6 m x scale, keep it off the road
                    float sc = R(4.0f, 9.0f);
                    addProp(Prop::Rock, d + R(-8, 8), side, 6.0f * sc + R(6, 50), sc, 0, R(0, 360));
                }
            }
        }
        break;
    case MapTheme::Snow:
        // dense snowy forest with the odd mountain chalet and rocks poking out of the snow
        for (float d = 20.0f; d < TRACK_LEN + 40.0f; d += R(9.0f, 15.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                float r = R(0, 1);
                if (r < 0.08f && d < TRACK_LEN - 220.0f)
                    addProp(Prop::House, d, side, R(9, 14), R(0.9f, 1.1f), 4, (side < 0) ? 90.0f : -90.0f);
                else if (r < 0.20f)
                    addProp(Prop::Rock, d + R(-3, 3), side, R(3, 18), R(0.7f, 1.6f), 3, R(0, 360));
                else if (r < 0.92f)
                    addProp(Prop::SnowPine, d + R(-3, 3), side, R(3, 22), R(0.9f, 1.4f), 0, R(0, 360));
                addProp(Prop::SnowPine, d + R(-6, 6), side, R(26, 70), R(1.1f, 1.8f), 0, R(0, 360));
            }
        }
        break;
    case MapTheme::City:
        // two rows of towers lining the street, street lights on both sides
        for (int s = 0; s < 2; ++s) {
            float side = (s == 0) ? -1.0f : 1.0f;
            for (float d = 15.0f; d < TRACK_LEN + 60.0f; d += R(17.0f, 23.0f))
                addProp(Prop::Building, d, side, R(7, 10), R(0.7f, 2.4f), int(R(0, 5)) % 5, 0.0f);
            for (float d = 25.0f; d < TRACK_LEN + 60.0f; d += R(20.0f, 30.0f))
                addProp(Prop::Building, d, side, R(32, 55), R(1.8f, 4.5f), int(R(0, 5)) % 5, 0.0f);
        }
        addLamps(28.0f, true);
        break;
    case MapTheme::Coast:
        // sea on the right (+x): palms on the beach, lighthouses on the rocks;
        // palms, beach houses and trees inland on the left
        for (float d = 20.0f; d < TRACK_LEN + 40.0f; d += R(13.0f, 22.0f)) {
            if (R(0, 1) < 0.7f) addProp(Prop::Palm, d + R(-4, 4), 1.0f, R(1.5f, 7.0f), R(0.85f, 1.2f), 0, R(0, 360));
            float r = R(0, 1);
            if (r < 0.18f && d < TRACK_LEN - 220.0f)
                addProp(Prop::House, d, -1.0f, R(8, 13), R(0.85f, 1.05f), int(R(0, 4)) % 4, 90.0f);
            else if (r < 0.70f)
                addProp(Prop::Palm, d + R(-4, 4), -1.0f, R(2, 20), R(0.85f, 1.25f), 0, R(0, 360));
            else
                addProp(Prop::RoundTree, d + R(-4, 4), -1.0f, R(4, 26), R(0.9f, 1.3f), 0, R(0, 360));
            addProp(R(0, 1) < 0.5f ? Prop::RoundTree : Prop::Rock, d + R(-6, 6), -1.0f, R(32, 80),
                    R(1.2f, 2.2f), 1, R(0, 360));
        }
        addProp(Prop::Lighthouse, 1250.0f, 1.0f, 26.0f, 1.0f, 0, 0.0f);
        addProp(Prop::Lighthouse, 2850.0f, 1.0f, 30.0f, 1.0f, 0, 0.0f);
        addLamps(60.0f, false);
        break;
    }

    // Torches
    for (float d = TRACK_LEN - 220.0f; d < TRACK_LEN + 30.0f; d += 20.0f) {
        for (int s = 0; s < 2; ++s) {
            float side = (s == 0) ? -1.0f : 1.0f;
            Prop p{}; p.type = Prop::Torch; p.z = -d; p.scale = 1.0f; p.palette = s; p.rot = 0;
            p.x = roadCenter(p.z) + side * (ROAD_HALF + 2.0f);
            props.push_back(p);
        }
    }

    // ---- Traffic ----
    const float pal[6][3] = {{0.95f, 0.95f, 0.95f}, {0.65f, 0.67f, 0.70f}, {0.20f, 0.65f, 0.30f},
                             {1.00f, 0.55f, 0.10f}, {0.55f, 0.30f, 0.75f}, {0.98f, 0.85f, 0.15f}};
    const float trafficGap = (TRACK_LEN - 400.0f) / float(map.trafficCount);
    for (int i = 0; i < map.trafficCount; ++i) {
        Traffic t{};
        t.z = -(180.0f + i * trafficGap + R(0, trafficGap * 0.35f));
        t.lane = t.targetLane = LANES[int(R(0, 3)) % 3];
        t.speed = R(0.35f, 0.62f);
        int k = int(R(0, 6)) % 6;
        t.r = pal[k][0]; t.g = pal[k][1]; t.b = pal[k][2];
        t.truck = R(0, 1) < 0.25f;
        t.parked = false;
        traffic.push_back(t);
    }
}

void GameSim::step(const PlayerInput inputs[], int inputCount)
{
    events.clear();
    ++tick;

    if (state == Menu || state == Paused) {
        return;
    }

    if (state == Countdown) {
        if (tick == 1 || tick == 60 || tick == 120) {
            SimEvent ev;
            ev.type = SimEvent::CountdownBeep;
            ev.highPitch = false;
            events.push_back(ev);
        }
        if (tick >= 180) {
            state = Racing;
            raceTimer = 0.0f;
            SimEvent ev;
            ev.type = SimEvent::CountdownBeep;
            ev.highPitch = true;
            events.push_back(ev);
        }
    }

    if (state == Racing) {
        raceTimer += 1.0f / 60.0f;
    }

    // Update cars: car 1 is the CPU in single-player, every other car follows its input
    for (size_t i = 0; i < cars.size(); ++i) {
        if (mode == ModeSingle && i == 1) {
            updateAI(cars[i], int(i));
        } else {
            PlayerInput in = (int(i) < inputCount) ? inputs[i] : PlayerInput{};
            updateCar(cars[i], in, int(i));
        }
    }

    // Traffic AI
    float lead = cars.empty() ? 0.0f : cars[0].z;
    float back = cars.empty() ? 0.0f : cars[0].z;
    for (const auto &c : cars) {
        lead = std::min(lead, c.z);
        back = std::max(back, c.z);
    }

    for (Traffic &t : traffic) {
        if (t.parked) continue;
        t.z -= t.speed;

        for (const Obstacle &o : obstacles) {
            if (o.type != Obstacle::Barrier || o.z > t.z || o.z < t.z - 25.0f) continue;
            float c = roadCenter(o.z);
            if (std::fabs(c + t.targetLane - o.x) < o.hx + 1.2f) {
                for (int l = 0; l < 3; ++l) {
                    bool blocked = false;
                    for (const Obstacle &o2 : obstacles)
                        if (o2.type == Obstacle::Barrier && std::fabs(o2.z - o.z) < 1.0f &&
                            std::fabs(c + LANES[l] - o2.x) < o2.hx + 1.2f) { blocked = true; break; }
                    if (!blocked) { t.targetLane = LANES[l]; break; }
                }
            }
        }
        t.lane += (t.targetLane - t.lane) * 0.06f;

        float tx = roadCenter(t.z) + t.lane, ta = roadAngle(t.z);
        for (Obstacle &o : obstacles)
            if (std::fabs(o.z - t.z) < 6.0f) knock(o, tx, t.z, std::sin(ta), -std::cos(ta), t.speed, -1);

        if (t.z > back + 40.0f || t.z < -TRACK_LEN + 40.0f) {
            float nz = lead - 260.0f - rnd(0.0f, 160.0f);
            if (nz < -TRACK_LEN + 80.0f) { t.parked = true; continue; }
            t.z = nz;
            t.lane = t.targetLane = LANES[int(rnd(0, 3)) % 3];
            t.speed = rnd(0.35f, 0.62f);
        }
    }

    // Knocked-over obstacles physics
    for (Obstacle &o : obstacles) {
        if (!o.knocked) continue;
        if (o.y <= 0.0f && std::fabs(o.vy) < 0.01f && std::fabs(o.vx) + std::fabs(o.vz) < 0.01f) continue;
        o.x += o.vx; o.y += o.vy; o.z += o.vz;
        o.vy -= 0.025f;
        o.rot += o.spin;
        if (o.y < 0.0f) { o.y = 0.0f; o.vy = -o.vy * 0.35f; o.vx *= 0.6f; o.vz *= 0.6f; o.spin *= 0.6f; }
    }

    // Collisions
    for (size_t i = 0; i < cars.size(); ++i) {
        collideWorld(cars[i], int(i));
    }
    for (size_t i = 0; i < cars.size(); ++i)
        for (size_t j = i + 1; j < cars.size(); ++j)
            collideCars(cars[i], cars[j], int(i), int(j));

    // Finish line
    if (state == Racing) {
        for (size_t i = 0; i < cars.size(); ++i) {
            if (!cars[i].finished && cars[i].z <= -TRACK_LEN) {
                cars[i].finished = true;
                cars[i].finishTime = raceTimer;
                if (!winner) {
                    winner = int(i + 1);
                    SimEvent ev;
                    ev.type = SimEvent::Fanfare;
                    events.push_back(ev);
                }
            }
        }
        if (winner != 0) {
            bool allDone = true;
            for (const auto &c : cars) {
                if (!c.finished) {
                    allDone = false;
                    break;
                }
            }
            // Transition to Finished state if all cars finished, or a few seconds after the
            // winner finishes (online gets longer so everyone can reach the line)
            float winnerFinishTime = cars[winner - 1].finishTime;
            float grace = (mode == ModeOnline) ? 20.0f : 3.0f;
            if (allDone || (raceTimer - winnerFinishTime >= grace)) {
                state = Finished;
            }
        }
    }
}

void GameSim::updateCar(Car &c, const PlayerInput &in, int carIndex)
{
    bool up = in.up;
    bool down = in.down;
    bool left = in.left;
    bool right = in.right;
    bool nitroKey = in.nitro;

    if (state != Racing) { up = down = nitroKey = false; }
    if (state == Countdown) { left = right = false; }

    float offset = c.x - roadCenter(c.z);
    bool offroad = std::fabs(offset) > ROAD_HALF;

    // Trigger nitro when player/AI presses nitro button and has an available charge
    if (nitroKey && c.nitroCharges > 0 && c.nitroTimer == 0 && !offroad) {
        c.nitroCharges = 0;
        c.nitroTimer = 120; // Exactly 2 seconds @ 60 ticks/sec
        c.nitroOn = true;
    }

    // 2-second active nitro countdown & boost
    if (c.nitroTimer > 0) {
        c.nitroOn = true;
        c.nitroTimer--;
        c.nitro = float(c.nitroTimer) / 120.0f; // Fractional display for HUD meter

        // Rapid surge towards NITRO_SPEED (1.65f)
        c.speed = std::min(NITRO_SPEED, c.speed + 0.035f);

        // Exhaust nitro flame particles
        float fx = std::sin(c.heading), fz = -std::cos(c.heading);
        float rx = std::cos(c.heading), rz = std::sin(c.heading);
        for (float side : {-0.7f, 0.7f}) {
            SimEvent ev;
            ev.type = SimEvent::NitroFlame;
            ev.x = c.x - fx * 2.2f + rx * side;
            ev.y = 0.42f;
            ev.z = c.z - fz * 2.2f + rz * side;
            ev.vx = -fx;
            ev.vz = -fz;
            ev.carIndex = carIndex;
            events.push_back(ev);
        }
    } else {
        c.nitroOn = false;
        c.nitro = (c.nitroCharges > 0) ? 1.0f : 0.0f;
        // After 2 seconds, smoothly return to normal top speed
        if (c.speed > MAX_SPEED * c.speedScale) {
            c.speed = std::max(MAX_SPEED * c.speedScale, c.speed - 0.016f);
        }
    }

    // speedScale is 1 for human players; the CPU uses it for difficulty and catch-up
    const float topSpeed = MAX_SPEED * c.speedScale;
    float maxS = c.nitroOn ? NITRO_SPEED : topSpeed;
    if (offroad) maxS = OFFROAD_MAX;

    if (up) {
        if (!c.nitroOn) {
            c.speed += 0.016f * c.speedScale * (1.0f - 0.5f * c.speed / topSpeed);
        }
    } else if (down) {
        if (c.speed > 0.05f) {
            c.speed -= 0.038f;
            if (c.speed > 0.35f) {
                float fx = std::sin(c.heading), fz = -std::cos(c.heading);
                float rx = std::cos(c.heading), rz = std::sin(c.heading);
                for (float s : {-0.9f, 0.9f}) {
                    SimEvent ev;
                    ev.type = SimEvent::BrakeSmoke;
                    ev.x = c.x - fx * 1.5f + rx * s;
                    ev.y = 0.15f;
                    ev.z = c.z - fz * 1.5f + rz * s;
                    ev.carIndex = carIndex;
                    events.push_back(ev);
                }
            }
        } else {
            c.speed -= 0.008f;
        }
    } else {
        if (!c.nitroOn) {
            c.speed *= (state == Finished ? 0.97f : 0.99f);
        }
    }

    if (c.speed > maxS) c.speed = std::max(maxS, c.speed - 0.025f);
    if (c.speed < -0.35f) c.speed = -0.35f;

    float steer = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    float grip  = std::clamp(std::fabs(c.speed) / 0.35f, 0.0f, 1.0f);
    c.heading  += steer * 0.030f * grip * (c.speed >= 0.0f ? 1.0f : -1.0f);
    c.steerVis += (steer * 0.45f - c.steerVis) * 0.25f;

    bool turningHard = std::fabs(steer) > 0.45f && c.speed > 0.45f;
    c.isDrifting = turningHard || (down && turningHard);
    if (c.isDrifting) {
        c.driftAngle += (steer * 0.32f - c.driftAngle) * 0.18f;
        float fx = std::sin(c.heading), fz = -std::cos(c.heading);
        float rx = std::cos(c.heading), rz = std::sin(c.heading);
        for (float s : {-0.9f, 0.9f}) {
            SimEvent ev;
            ev.type = SimEvent::DriftSmoke;
            ev.x = c.x - fx * 1.5f + rx * s;
            ev.y = 0.12f;
            ev.z = c.z - fz * 1.5f + rz * s;
            ev.carIndex = carIndex;
            events.push_back(ev);
        }
    } else {
        c.driftAngle *= 0.85f;
    }

    c.roll += (-steer * 4.6f * std::clamp(c.speed / MAX_SPEED, 0.0f, 1.0f) - c.roll) * 0.22f;
    float targetPitch = 0.0f;
    if (down && c.speed > 0.1f) targetPitch = 2.4f;
    else if (c.nitroOn) targetPitch = -2.0f;
    else if (up) targetPitch = -1.0f;
    c.pitch += (targetPitch - c.pitch) * 0.20f;

    float moveAngle = c.heading + c.driftAngle * 0.45f;
    c.x += std::sin(moveAngle) * c.speed;
    c.z -= std::cos(moveAngle) * c.speed;

    c.topSpeed = std::max(c.topSpeed, c.speed);
    c.wheelSpin = std::fmod(c.wheelSpin + c.speed * 136.0f, 360.0f);

    // Guard rails
    offset = c.x - roadCenter(c.z);
    float lim = RAIL_OFFSET - 1.0f;
    if (std::fabs(offset) > lim) {
        c.x = roadCenter(c.z) + (offset > 0 ? lim : -lim);
        c.speed *= 0.82f;
        c.heading += (roadAngle(c.z) - c.heading) * 0.35f;

        SimEvent spk;
        spk.type = SimEvent::Sparks;
        spk.x = c.x; spk.y = 0.4f; spk.z = c.z;
        spk.count = 8;
        spk.carIndex = carIndex;
        events.push_back(spk);

        SimEvent cr;
        cr.type = SimEvent::Crash;
        cr.intensity = 0.8f;
        cr.carIndex = carIndex;
        events.push_back(cr);
    }
}

void GameSim::updateAI(Car &c, int carIndex)
{
    PlayerInput in = computeAIInput(c, carIndex, true);
    updateCar(c, in, carIndex);
}

PlayerInput GameSim::computeAIInput(Car &c, int carIndex, bool asRival)
{
    PlayerInput in;

    // ---- 1. Pace: difficulty + catch-up ("rubber band") ----------------------------
    // Easy / Medium / Hard change the CPU's top speed a little; when the CPU falls
    // behind player 1 it gets extra pace so races stay close, and on Easy/Medium it
    // eases off when it is far ahead.
    const int diff = std::clamp(aiDifficulty, 0, 2);
    const float playerZ = cars.empty() ? c.z : cars[0].z;
    const float gap = c.z - playerZ;                     // > 0: CPU is behind player 1
    if (asRival) {
        const float base[3]     = {0.95f, 1.00f, 1.04f};
        const float maxBonus[3] = {0.04f, 0.09f, 0.14f};
        float scale = base[diff];
        if (gap > 0.0f) {
            scale += std::min(maxBonus[diff], gap * 0.0015f);
        } else if (diff == 0) {
            scale -= std::min(0.10f, -gap * 0.0015f);
        } else if (diff == 1) {
            scale -= std::min(0.05f, std::max(0.0f, -gap - 40.0f) * 0.001f);
        }
        c.speedScale = scale;
    } else {
        c.speedScale = 1.0f;
    }

    if (state != Racing) {
        c.aiReverse = 0; c.aiCheckTimer = 0; c.aiCheckZ = c.z;
        c.aiTarget = c.x - roadCenter(c.z);
        return in;
    }

    // ---- 2. Progress watchdog: if we barely moved in 1.5 s we are wedged -> back out
    // (paused while backing out, and restarted afterwards, so it can't loop)
    if (c.aiReverse == 0 && ++c.aiCheckTimer >= 90) {
        if (c.aiCheckZ - c.z < 10.0f) c.aiReverse = 50;
        c.aiCheckTimer = 0;
        c.aiCheckZ = c.z;
    }

    // ---- 3. Hazards ahead --------------------------------------------------------
    // dist = metres ahead of us; [x0, x1] = band of road it blocks, measured from the
    // road centre at the hazard's own position (so bends don't shift it).
    struct Hazard { float dist, x0, x1; bool soft; };   // soft = cones: avoid, but never brake for them
    std::vector<Hazard> hz;
    const float horizon = 30.0f + std::max(0.0f, c.speed) * 75.0f;
    auto addHazard = [&](float dist, float offMin, float offMax, bool soft = false) {
        if (dist < -2.0f || dist > horizon) return;
        hz.push_back({std::max(0.0f, dist), offMin, offMax, soft});
    };
    const float CAR_HALF = 1.15f;                        // car half width + a little margin
    for (const Obstacle &o : obstacles) {
        if (o.knocked) continue;
        float off = o.x - roadCenter(o.z), dist = c.z - o.z;
        float r = (o.type == Obstacle::Barrier) ? o.hx : (o.type == Obstacle::Barrel ? 0.7f : 0.45f);
        addHazard(dist, off - r - CAR_HALF, off + r + CAR_HALF, o.type == Obstacle::Cone);
    }
    for (const Traffic &t : traffic) {
        if (t.parked || t.speed > c.speed + 0.05f) continue;   // only traffic we are catching
        // a car changing lanes blocks both the lane it is leaving and the one it is entering
        addHazard(c.z - t.z - 4.0f, std::min(t.lane, t.targetLane) - 2.1f, std::max(t.lane, t.targetLane) + 2.1f);
    }
    for (size_t j = 0; j < cars.size(); ++j) {           // other racers: overtake, don't ram
        if (int(j) == carIndex) continue;
        const Car &o = cars[j];
        if (o.speed > c.speed + 0.05f) continue;
        float off = o.x - roadCenter(o.z);
        addHazard(c.z - o.z - 4.0f, off - 2.2f, off + 2.2f);
    }
    std::sort(hz.begin(), hz.end(), [](const Hazard &a, const Hazard &b) { return a.dist < b.dist; });

    // ---- 4. Find the gap ---------------------------------------------------------
    // Take the next "row" of hazards (everything within 6 m of the nearest one),
    // work out the free gaps across the road, and aim for the nearest point of the
    // best gap - preferring gaps that line up with the row after it.
    typedef std::pair<float, float> Gap;
    const float EDGE = ROAD_HALF - 1.0f;
    const float myOff = c.x - roadCenter(c.z);
    auto freeGaps = [&](size_t first, size_t last, std::vector<Gap> &gaps, bool hardOnly = false) {
        gaps.assign(1, Gap(-EDGE, EDGE));
        for (size_t k = first; k < last; ++k) {
            if (hardOnly && hz[k].soft) continue;
            std::vector<Gap> next;
            for (const Gap &g : gaps) {
                if (hz[k].x1 <= g.first || hz[k].x0 >= g.second) { next.push_back(g); continue; }
                if (hz[k].x0 > g.first)  next.push_back(Gap(g.first, hz[k].x0));
                if (hz[k].x1 < g.second) next.push_back(Gap(hz[k].x1, g.second));
            }
            gaps.swap(next);
        }
    };
    float target = std::clamp(c.aiTarget, -EDGE, EDGE);
    float rowDist = 1e9f;
    size_t rowEnd = 0;
    bool rowBlocked = false;
    if (!hz.empty()) {
        rowDist = hz[0].dist;
        while (rowEnd < hz.size() && hz[rowEnd].dist <= rowDist + 6.0f) ++rowEnd;
        std::vector<Gap> gaps, gaps2;
        freeGaps(0, rowEnd, gaps);
        if (gaps.empty()) freeGaps(0, rowEnd, gaps, true);   // no clean gap: just go through the cones
        size_t row2End = rowEnd;
        bool haveRow2 = rowEnd < hz.size() && hz[rowEnd].dist < rowDist + 50.0f;
        if (haveRow2) {
            while (row2End < hz.size() && hz[row2End].dist <= hz[rowEnd].dist + 6.0f) ++row2End;
            freeGaps(rowEnd, row2End, gaps2);
        }
        if (gaps.empty()) {
            rowBlocked = true;                           // e.g. traffic filling the only gap: wait
        } else {
            float bestScore = -1e9f;
            for (const Gap &g : gaps) {
                float w = g.second - g.first, margin = std::min(0.5f, w * 0.5f);
                float p = std::clamp(myOff, g.first + margin, g.second - margin);
                float sc = -std::fabs(p - myOff) + 0.4f * std::min(w, 4.0f);
                if (haveRow2 && !gaps2.empty()) {
                    float move2 = 1e9f;
                    for (const Gap &g2 : gaps2) move2 = std::min(move2, std::fabs(std::clamp(p, g2.first, g2.second) - p));
                    sc -= 0.5f * move2;
                }
                if (sc > bestScore) { bestScore = sc; target = p; }
            }
        }
    }
    if (rowDist > 45.0f) {                               // nothing close: go and grab a nitro can
        for (const NitroPickup &np : nitroPickups) {
            float dist = c.z - np.z, off = np.x - roadCenter(np.z);
            if (np.active && dist > 0.0f && dist < 60.0f && std::fabs(off) < EDGE) { target = off; break; }
        }
    }
    c.aiTarget = target;

    // ---- 5. Steering: pure pursuit towards a point on the target line ------------
    const float spd = std::max(0.2f, std::fabs(c.speed));
    const float move = std::fabs(target - myOff);
    // aim closer (sharper steering) while moving across, further (smoother) when settled
    const float look = (move > 1.0f) ? std::clamp(spd * 11.0f, 6.0f, 14.0f) : std::clamp(spd * 17.0f, 9.0f, 24.0f);
    const float tz = c.z - look;
    const float tx = roadCenter(tz) + target;
    float desired = std::atan2(tx - c.x, c.z - tz);
    float moveAngle = c.heading + c.driftAngle * 0.45f * 0.6f;   // the car slides a little when steering
    float err = desired - moveAngle;
    while (err > PI) err -= 2.0f * PI;
    while (err < -PI) err += 2.0f * PI;

    if (c.aiReverse > 0) {
        // back straight out, lining the car up with the road (reversing at an angle
        // would slide us sideways and undo the progress we made towards the gap);
        // steering works the other way round in reverse
        if (--c.aiReverse == 0) { c.aiCheckTimer = 0; c.aiCheckZ = c.z; }
        float alignErr = roadAngle(c.z) - c.heading;
        in.down = true;
        in.left  = alignErr >  0.03f;
        in.right = alignErr < -0.03f;
        return in;
    }
    in.right = err > 0.012f;
    in.left  = err < -0.012f;

    // ---- 6. Throttle and brakes ---------------------------------------------------
    in.up = true;
    // a) bends: compare how fast the road turns with how fast the car can turn
    float turnNeed = std::fabs(roadAngle(c.z - 50.0f) - roadAngle(c.z - 20.0f)) / 30.0f;
    float turnMax = 0.030f / spd * 1.15f;                 // rad per metre at this speed (+drift)
    if (turnNeed > turnMax * 0.80f) in.up = false;
    if (turnNeed > turnMax * 1.05f && c.speed > 0.5f) in.down = true;
    // b) the next row of hazards
    if (!hz.empty()) {
        bool inBand = false;
        for (size_t k = 0; k < rowEnd; ++k)
            if (!hz[k].soft && myOff > hz[k].x0 && myOff < hz[k].x1) inBand = true;
        // distance needed to slide across into the gap: the slower we go, the tighter
        // the car can turn, so a sideways move takes less road
        float distNeeded = 10.0f * spd + move * (4.0f + 6.0f * spd);
        if (rowBlocked) {
            if (rowDist < spd * 30.0f && c.speed > 0.25f) { in.down = true; in.up = false; }
        } else if (inBand && distNeeded > rowDist - 1.0f && c.speed > 0.22f) {
            in.down = true; in.up = false;               // too fast to make it into the gap
        }
    }
    if (std::fabs(err) > 0.45f && c.speed > 0.6f) in.up = false;   // badly off line: lift

    // ---- 7. Nitro: on a clear, straight bit of road ------------------------------
    if (c.nitroCharges > 0 && c.nitroTimer == 0) {
        bool straight = std::fabs(roadAngle(c.z - 120.0f) - roadAngle(c.z)) < 0.12f && turnNeed < turnMax * 0.4f;
        bool clear = true;
        for (const Hazard &h : hz)
            if (!h.soft && h.dist < 110.0f && target > h.x0 && target < h.x1) clear = false;
        bool wantIt = !asRival || diff == 2 || gap > -10.0f || c.z < -TRACK_LEN + 900.0f;
        if (diff == 0 && gap < 0.0f) wantIt = false;      // Easy never uses nitro to run away
        if (straight && clear && wantIt && std::fabs(err) < 0.1f) in.nitro = true;
    }

    return in;
}

bool GameSim::knock(Obstacle &o, float x, float z, float fx, float fz, float speed, int carIndex)
{
    if (o.type == Obstacle::Barrier || o.knocked) return false;
    float r = (o.type == Obstacle::Cone ? 0.45f : 0.7f) + 1.0f;
    float dx = o.x - x, dz = o.z - z;
    if (dx * dx + dz * dz > r * r) return false;
    float d = std::sqrt(dx * dx + dz * dz) + 1e-4f;
    float power = std::max(std::fabs(speed), 0.2f);
    o.knocked = true;
    o.vx = dx / d * 0.25f + fx * power * 0.9f;
    o.vz = dz / d * 0.25f + fz * power * 0.9f;
    o.vy = 0.25f + power * 0.3f;
    o.spin = 12.0f + power * 20.0f;

    SimEvent deb;
    deb.type = SimEvent::Debris;
    deb.x = o.x; deb.y = (o.type == Obstacle::Cone ? 0.4f : 0.6f); deb.z = o.z;
    if (o.type == Obstacle::Cone) {
        deb.r = 0.95f; deb.g = 0.45f; deb.b = 0.05f; deb.count = 6;
    } else {
        deb.r = 0.85f; deb.g = 0.1f; deb.b = 0.1f; deb.count = 8;
    }
    deb.carIndex = carIndex;
    events.push_back(deb);

    SimEvent cr;
    cr.type = SimEvent::Crash;
    cr.intensity = 0.6f;
    cr.carIndex = carIndex;
    events.push_back(cr);

    return true;
}

void GameSim::collideWorld(Car &c, int carIndex)
{
    float fx = std::sin(c.heading), fz = -std::cos(c.heading);

    for (Obstacle &o : obstacles) {
        if (std::fabs(o.z - c.z) > 8.0f) continue;
        if (o.type == Obstacle::Barrier) {
            for (float off : {1.3f, -1.3f}) {
                float px = c.x + fx * off, pz = c.z + fz * off;
                float qx = std::clamp(px, o.x - o.hx, o.x + o.hx);
                float qz = std::clamp(pz, o.z - o.hz, o.z + o.hz);
                float dx = px - qx, dz = pz - qz, d2 = dx * dx + dz * dz;
                const float RAD = 1.0f;
                if (d2 < RAD * RAD) {
                    float d = std::sqrt(d2), nx, nz, pen;
                    if (d < 1e-4f) { nx = 0; nz = 1; pen = RAD + (o.z + o.hz - pz); }
                    else           { nx = dx / d; nz = dz / d; pen = RAD - d; }
                    c.x += nx * pen; c.z += nz * pen;
                    if (c.speed > 0.15f) {
                        SimEvent spk;
                        spk.type = SimEvent::Sparks;
                        spk.x = c.x; spk.y = 0.5f; spk.z = c.z;
                        spk.count = 8;
                        spk.carIndex = carIndex;
                        events.push_back(spk);

                        SimEvent cr;
                        cr.type = SimEvent::Crash;
                        cr.intensity = 0.85f;
                        cr.carIndex = carIndex;
                        events.push_back(cr);
                    }
                    c.speed *= 0.35f;
                }
            }
        } else if (knock(o, c.x, c.z, fx, fz, c.speed, carIndex)) {
            c.speed *= (o.type == Obstacle::Cone) ? 0.85f : 0.55f;
        }
    }

    for (const Traffic &t : traffic) {
        if (t.parked) continue;
        float tx = roadCenter(t.z) + t.lane;
        float dx = c.x - tx, dz = c.z - t.z;
        const float halfW = 1.9f;
        float halfL = t.truck ? 5.6f : 4.2f;
        if (std::fabs(dx) < halfW && std::fabs(dz) < halfL) {
            float px = halfW - std::fabs(dx), pz = halfL - std::fabs(dz);
            if (px < pz)      { c.x += (dx > 0) ? px : -px; c.speed *= 0.93f; }
            else if (dz > 0)  { c.z += pz; c.speed = std::min(c.speed, t.speed * 0.5f); }
            else              { c.z -= pz; }

            SimEvent spk;
            spk.type = SimEvent::Sparks;
            spk.x = (c.x + tx) * 0.5f; spk.y = 0.5f; spk.z = c.z;
            spk.count = 8;
            spk.carIndex = carIndex;
            events.push_back(spk);

            SimEvent cr;
            cr.type = SimEvent::Crash;
            cr.intensity = t.truck ? 1.0f : 0.75f;
            cr.carIndex = carIndex;
            events.push_back(cr);
        }
    }

    // Check collectible nitro pickups on track
    for (NitroPickup &np : nitroPickups) {
        if (!np.active) continue;
        float dx = c.x - np.x;
        float dz = c.z - np.z;
        if (dx * dx + dz * dz < 2.5f * 2.5f) {
            np.active = false;
            // Award nitro charge (capped at 1, no extra boost stacking)
            if (c.nitroCharges == 0) {
                c.nitroCharges = 1;
                if (c.nitroTimer == 0) {
                    c.nitro = 1.0f;
                }
            }
            SimEvent ev;
            ev.type = SimEvent::NitroPickupEvent;
            ev.x = np.x;
            ev.y = np.y;
            ev.z = np.z;
            ev.carIndex = carIndex;
            events.push_back(ev);
        }
    }
}

void GameSim::predictCollisions(Car &c) const
{
    float fx = std::sin(c.heading), fz = -std::cos(c.heading);
    for (const Obstacle &o : obstacles) {
        if (o.type != Obstacle::Barrier || std::fabs(o.z - c.z) > 8.0f) continue;
        for (float off : {1.3f, -1.3f}) {
            float px = c.x + fx * off, pz = c.z + fz * off;
            float qx = std::clamp(px, o.x - o.hx, o.x + o.hx);
            float qz = std::clamp(pz, o.z - o.hz, o.z + o.hz);
            float dx = px - qx, dz = pz - qz, d2 = dx * dx + dz * dz;
            const float RAD = 1.0f;
            if (d2 < RAD * RAD) {
                float d = std::sqrt(d2), nx, nz, pen;
                if (d < 1e-4f) { nx = 0; nz = 1; pen = RAD + (o.z + o.hz - pz); }
                else           { nx = dx / d; nz = dz / d; pen = RAD - d; }
                c.x += nx * pen; c.z += nz * pen;
                c.speed *= 0.35f;
            }
        }
    }
    for (const Traffic &t : traffic) {
        if (t.parked) continue;
        float tx = roadCenter(t.z) + t.lane;
        float dx = c.x - tx, dz = c.z - t.z;
        const float halfW = 1.9f;
        float halfL = t.truck ? 5.6f : 4.2f;
        if (std::fabs(dx) < halfW && std::fabs(dz) < halfL) {
            float px = halfW - std::fabs(dx), pz = halfL - std::fabs(dz);
            if (px < pz)     { c.x += (dx > 0) ? px : -px; c.speed *= 0.93f; }
            else if (dz > 0) { c.z += pz; c.speed = std::min(c.speed, t.speed * 0.5f); }
            else             { c.z -= pz; }
        }
    }
}

void GameSim::collideCars(Car &a, Car &b, int idxA, int idxB)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    if (std::fabs(dx) < 1.9f && std::fabs(dz) < 4.2f) {
        float px = 1.9f - std::fabs(dx), pz = 4.2f - std::fabs(dz);
        if (px < pz) {
            float s = ((dx > 0) ? 1.0f : -1.0f) * px * 0.5f;
            a.x += s; b.x -= s;
        } else {
            float s = ((dz > 0) ? 1.0f : -1.0f) * pz * 0.5f;
            a.z += s; b.z -= s;
            float avg = (a.speed + b.speed) * 0.5f * 0.95f;
            a.speed = b.speed = avg;
        }

        SimEvent spk;
        spk.type = SimEvent::Sparks;
        spk.x = (a.x + b.x) * 0.5f; spk.y = 0.45f; spk.z = (a.z + b.z) * 0.5f;
        spk.count = 10;
        spk.carIndex = idxA;
        events.push_back(spk);

        SimEvent cr;
        cr.type = SimEvent::Crash;
        cr.intensity = 0.9f;
        cr.carIndex = idxA;
        events.push_back(cr);

        SimEvent cr2;
        cr2.type = SimEvent::Crash;
        cr2.intensity = 0.9f;
        cr2.carIndex = idxB;
        events.push_back(cr2);
    }
}

int GameSim::rankOf(int idx) const
{
    if (idx < 0 || idx >= int(cars.size())) return 1;
    const Car &me = cars[idx];
    int rank = 1;
    for (int j = 0; j < int(cars.size()); ++j) {
        if (j == idx) continue;
        const Car &o = cars[j];
        bool ahead;
        if (o.finished && me.finished) ahead = o.finishTime < me.finishTime;
        else if (o.finished != me.finished) ahead = o.finished;
        else ahead = o.z < me.z;                          // track runs towards -Z
        if (ahead) ++rank;
    }
    return rank;
}

uint64_t GameSim::checksum() const
{
    // FNV-1a 64-bit hash
    uint64_t hash = 14695981039346656037ULL;
    auto hashBytes = [&hash](const void *data, size_t len) {
        const uint8_t *ptr = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= ptr[i];
            hash *= 1099511628211ULL;
        }
    };

    int32_t t = tick;
    hashBytes(&t, sizeof(t));

    for (const auto &c : cars) {
        float vals[] = { c.x, c.z, c.heading, c.speed, c.nitro, float(c.nitroTimer), float(c.nitroCharges) };
        hashBytes(vals, sizeof(vals));
    }
    for (const auto &tr : traffic) {
        float vals[] = { tr.z, tr.lane, tr.speed };
        hashBytes(vals, sizeof(vals));
    }
    for (const auto &np : nitroPickups) {
        bool act = np.active;
        hashBytes(&act, sizeof(act));
    }

    return hash;
}

} // namespace sim
