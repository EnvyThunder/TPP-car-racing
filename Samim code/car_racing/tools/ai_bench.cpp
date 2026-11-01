// Headless CPU-driver benchmark (no Qt, no graphics).
// Build:  g++ -O2 -std=c++17 -Ishared tools/ai_bench.cpp shared/gamesim.cpp shared/track.cpp
//         shared/circuits.cpp -o ai_bench
//
// A reference driver stands in for a good human player: the CPU's own driving logic at
// normal speed with no catch-up help.
//  1. vs CPU as the menu offers it: the reference driver against 1, 3 or 5 CPUs at each
//     difficulty (each CPU a random level of that
//     difficulty, as in the game)
//  2. online CPU levels 1-5 against each other and the reference driver
//  3. a full room of 6: finishes and crashes per map
//  4. crashes: wrecks, drivers thrown out and where the damage comes from, whole races
//  5. jumps: every ramp launched at speeds from slow to full nitro, from the middle and both
//     edges of the road, at both road widths, with the CPU's steering and with none
//  "./ai_bench crash" runs only part 4, "./ai_bench jumps" only part 5.
#include "gamesim.h"
#include "clone.h"
#include "brain.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>
#include <cstdlib>
#include <vector>

// One launch: the car starts 120 m before the lip at speed v (1.65 = nitro), d metres from
// the middle, and holds the throttle. The CPU's steering drives it to the lip; in the air
// it keeps steering (steer) or lets go (!steer).
struct Flight { bool flew = false; float air = 0, dist = 0, impact = 0, edge = 0; };
static Flight launch(int map, bool pack, const sim::GameSim::Ramp &r, float v, float d, bool steer)
{
    sim::GameSim s;
    s.setMap(map);
    s.startRace(pack ? sim::GameSim::ModeOnline : sim::GameSim::ModeSingle, 1);
    s.setState(sim::GameSim::Racing);
    s.trafficMut().clear();
    sim::Car &c = s.getCars()[0];
    c.s = s.wrapS(r.lip - 120.0f);
    c.d = d;
    c.lap = 0;
    c.hint = int(c.s);
    s.pointAt(c.s, c.d, c.x, c.z);
    c.heading = s.headingAt(c.s);
    c.y = s.heightAt(c.s, c.d);
    s.locate(c);
    c.aiCheckProg = c.progress;                          // (its "am I stuck?" check starts here)
    c.aiCheckTimer = 0;
    const bool nitro = v > sim::NITRO_SPEED - 0.01f;
    const float scale = nitro ? 1.0f : v / sim::MAX_SPEED;
    c.speed = v;
    if (nitro) { c.nitroTimer = 2000; c.nitroCharges = 0; }
    Flight f;
    float s0 = 0.0f;
    for (int t = 0; t < 900; ++t) {
        sim::PlayerInput in = s.computeAIInput(c, 0, false);
        in.up = true; in.down = false; in.nitro = false;
        c.speedScale = scale;
        if (!steer && c.air > 0) in.left = in.right = false;
        const int airBefore = c.air;
        s.step(&in, 1);
        if (airBefore == 0 && c.air > 0) { f.flew = true; s0 = c.progress; }
        for (const sim::SimEvent &e : s.getEvents())
            if (e.type == sim::SimEvent::Landing && f.flew) {
                f.air = e.count / 60.0f;
                f.impact = e.intensity;
                f.dist = c.progress - s0;
                f.edge = std::fabs(c.d) - s.roadHalfAt(c.s);   // > 0: landed off the asphalt
                return f;
            }
        s.clearEvents();
    }
    return f;
}

static void jumpTest()
{
    std::printf("Jumps: launch speed -> airtime (s), distance (m), landing impact (m/s; 12-16 medium, >16 hard),\n"
                "and the worst landing place: metres past the asphalt edge (<= 0: on the asphalt)\n");
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        sim::GameSim probe;
        probe.setMap(m);
        for (const sim::GameSim::Ramp &r : probe.ramps()) {
            std::printf("%s, lip at %.0f m, %.1f m high\n", sim::MAPS[m].name, r.lip, r.height);
            std::printf("  %-5s %-5s %6s %7s %7s %12s %12s\n", "road", "speed", "air", "dist", "impact", "edge steer", "edge none");
            for (int pack : {0, 1}) {
                for (float v : {0.4f, 0.7f, 1.0f, 1.1f, 1.3f, sim::NITRO_SPEED}) {
                    float air = 0, dist = 0, impact = 0, edgeSteer = -99, edgeNone = -99;
                    int flew = 0;
                    sim::GameSim w;
                    w.setMap(m);
                    w.startRace(pack ? sim::GameSim::ModeOnline : sim::GameSim::ModeSingle, 1);
                    const float half = w.roadHalf() - 2.0f;
                    for (float d : {-half, 0.0f, half})
                        for (bool steer : {true, false}) {
                            const Flight f = launch(m, pack, r, v, d, steer);
                            if (!f.flew) continue;
                            ++flew;
                            air = std::max(air, f.air);
                            dist = std::max(dist, f.dist);
                            impact = std::max(impact, f.impact);
                            (steer ? edgeSteer : edgeNone) = std::max(steer ? edgeSteer : edgeNone, f.edge);
                        }
                    if (!flew) { std::printf("  %-5s %5.2f   rolls over the ramp\n", pack ? "wide" : "3-ln", v); continue; }
                    std::printf("  %-5s %5.2f %6.2f %7.0f %7.1f %12.1f %12.1f\n", pack ? "wide" : "3-ln", v, air, dist, impact,
                                edgeSteer, edgeNone);
                }
            }
        }
    }
}

// Crashes: full rooms of 6 (2 reference drivers + levels 2-5) over a whole race (3 laps of
// a circuit, a sprint once), 3 grid rotations per map. How often cars are wrecked, how often
// the driver is thrown out, and where the damage comes from (a car's own fault in a hit on
// another car, being hit, traffic, barriers, guard rails, hard landings, cones and barrels).
static void crashTest()
{
    const int N = sim::MAX_RACERS;
    std::printf("Crashes: 15 whole races of %d cars (3 laps / a sprint); per car and race\n", N);
    std::printf("%-16s %7s %7s %8s | damage: %6s %5s %7s %7s %5s %5s %5s %9s\n", "map", "wrecks", "thrown", "at-fault",
                "rammed", "hit", "traffic", "barrier", "rail", "land", "cones", "finished");
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        int wrecks = 0, thrown = 0, fault = 0, fin = 0;
        float dmg[7] = {};
        for (int rot = 0; rot < 3; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.setLaps(3);
            s.startRace(sim::GameSim::ModeOnline, N);
            for (int i = 0; i < N; ++i) s.getCars()[(i + rot * 2) % N].aiLevel = i < 2 ? 0 : i;
            for (int t = 0; t < 60 * 420 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                for (int i = 0; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, s.getCars()[i].aiLevel > 0);
                s.step(in, N);
                for (const sim::SimEvent &e : s.getEvents()) {
                    if (e.type == sim::SimEvent::Wrecked) { ++wrecks; thrown += e.highPitch ? 1 : 0; }
                    if (e.type != sim::SimEvent::Damage) continue;
                    const int k = e.count >= 0 ? (e.highPitch ? 0 : 1) : e.count == sim::SimEvent::ByTraffic ? 2
                                : e.count == sim::SimEvent::ByBarrier ? 3 : e.count == sim::SimEvent::ByRail ? 4
                                : e.count == sim::SimEvent::ByLanding ? 5 : 6;
                    dmg[k] += e.intensity;
                }
                s.clearEvents();
            }
            for (const sim::Car &c : s.getCars()) { fault += c.crashesCaused; fin += c.finished ? 1 : 0; }
        }
        const float races = 3.0f * N;
        std::printf("%-16s %7.2f %6.0f%% %8.2f | %14.1f %5.1f %7.1f %7.1f %5.1f %5.1f %5.1f %6d/%d\n", sim::MAPS[m].name,
                    wrecks / races, wrecks ? 100.0f * thrown / wrecks : 0.0f, fault / races,
                    dmg[0] / races, dmg[1] / races, dmg[2] / races, dmg[3] / races, dmg[4] / races, dmg[5] / races,
                    dmg[6] / races, fin, 3 * N);
    }
    std::printf("\n");
}

// Side ramps, the L boost and elimination: whole races of 6 CPU and reference drivers. How
// many are eliminated (wrecked twice in 40 s); side jumps and how they landed; falls off the
// road (side ramps / all); bounties; boosts.
static void livesTest(int laps)
{
    const int N = sim::MAX_RACERS;
    std::printf("Ramps, boost, elimination: 3 races a map of %d cars (%d laps / a sprint); totals over the 18 cars\n", N, laps);
    std::printf("%-16s %5s %4s %5s %5s | %5s %5s %5s %5s %5s %5s | %6s %5s %5s %5s %6s\n", "map", "fin", "out", "wrkd", "ramps",
                "jumps", "clean", "roll", "saved", "rough", "crash", "splash", "bnty", "boost", "wreck", "jumpDmg");
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        int fin = 0, out = 0, life2 = 0, kinds[5] = {}, splash = 0, bounty = 0, boosts = 0, wrecks = 0, ramps = 0, finLife1 = 0, sideSplash = 0;
        float jumpDmg = 0.0f;
        for (int rot = 0; rot < 3; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.setLaps(laps);
            s.startRace(sim::GameSim::ModeOnline, N);
            ramps = int(s.sideRamps().size());
            for (int i = 0; i < N; ++i) s.getCars()[(i + rot * 2) % N].aiLevel = i < 2 ? 0 : i;
            for (int t = 0; t < 60 * 600 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                for (int i = 0; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, s.getCars()[i].aiLevel > 0);
                s.step(in, N);
                for (const sim::SimEvent &e : s.getEvents()) {
                    if (e.type == sim::SimEvent::Stunt) { ++kinds[std::clamp(e.count, 0, 4)]; jumpDmg += e.intensity; }
                    if (e.type == sim::SimEvent::Splash) { ++splash; if (e.intensity > 0.0f) ++sideSplash; }
                    if (e.type == sim::SimEvent::Bounty) ++bounty;
                    if (e.type == sim::SimEvent::Boost) ++boosts;
                    if (e.type == sim::SimEvent::Wrecked) ++wrecks;
                }
                s.clearEvents();
            }
            for (const sim::Car &c : s.getCars()) {
                fin += c.finished; out += c.out; life2 += c.wrecks > 0;
            }
        }
        const int jumps = kinds[0] + kinds[1] + kinds[2] + kinds[3] + kinds[4];
        std::printf("%-16s %5d %4d %5d %5d | %5d %5d %5d %5d %5d %5d | %3d/%-2d %5d %5d %5d %6.1f%s\n", sim::MAPS[m].name, fin, out, life2, ramps,
                    jumps, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], splash, sideSplash, bounty, boosts, wrecks,
                    jumps ? jumpDmg / jumps : 0.0f, "");
    }
    std::printf("\n");
}

// The elimination distance: how far an average driver (the reference driver and CPU levels
// 2-4, crashes and all) goes per 70 health of damage. Whole races of 6 on every map.
static void damageRate()
{
    const int N = sim::MAX_RACERS;
    double dmgAll = 0.0, distAll = 0.0;
    std::printf("Damage per km, average drivers (reference + levels 2-4), 3 races a map\n");
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        double dmg = 0.0, dist = 0.0;
        for (int rot = 0; rot < 3; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.setLaps(3);
            s.startRace(sim::GameSim::ModeOnline, N);
            static const int LV[6] = {0, 2, 3, 4, 3, 2};
            for (int i = 0; i < N; ++i) s.getCars()[(i + rot) % N].aiLevel = LV[i];
            std::vector<float> start(N);
            for (int i = 0; i < N; ++i) start[i] = s.getCars()[i].progress;
            for (int t = 0; t < 60 * 600 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                for (int i = 0; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, s.getCars()[i].aiLevel > 0);
                s.step(in, N);
                for (const sim::SimEvent &e : s.getEvents())
                    if (e.type == sim::SimEvent::Damage) dmg += e.intensity;
                s.clearEvents();
            }
            for (int i = 0; i < N; ++i) dist += std::max(0.0f, s.getCars()[i].progress - start[i]);
        }
        std::printf("%-16s %6.1f per km\n", sim::MAPS[m].name, 1000.0 * dmg / dist);
        dmgAll += dmg; distAll += dist;
    }
    const double perKm = 1000.0 * dmgAll / distAll;
    std::printf("ALL MAPS         %6.1f per km -> 70 health every %.0f m\n\n", perKm, 70.0 * 1000.0 / perKm);
}

// The player model test: simulated players of known styles race against 5 CPUs; each must
// come out of the player model with the right traits.
//   expert  - the CPU driver at full pace, clean
//   timid   - slow (lifts off at 80% of top speed), never boosts, keeps off the ramps
//   rammer  - steers at the car in front whenever one is close
//   risky   - every side ramp, nitro into them, holds L for 5-7 s on every straight
//   tilter  - drives like the expert until its first wreck, then gets wilder after each one
// For every style: the race traits (skill, consistency, aggression, risk) and the highest
// tilt; and a profile built from its races.
enum Style { Expert, Timid, Rammer, Risky, Tilter, NUM_STYLES };
static const char *STYLE_NAME[NUM_STYLES] = {"expert", "timid", "rammer", "risky", "tilter"};

static sim::PlayerInput styleInput(sim::GameSim &s, int style, int t, int &boostLeft, uint32_t &seed)
{
    sim::Car &c = s.getCars()[0];
    sim::PlayerInput in = s.computeAIInput(c, 0, false);
    in.boost = false;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.0f; };
    switch (style) {
    case Timid:
        if (c.speed > 0.80f * sim::MAX_SPEED) in.up = false;
        in.nitro = false;
        for (const auto &r : s.sideRamps()) {                // keep away from the ramp lanes
            const float dist = s.aheadOf(c.s, r.s);
            if (dist > 0.0f && dist < 90.0f && std::fabs(c.d - r.d) < r.half + 2.0f) {
                const bool goLeft = r.d > 0.0f;
                in.left = goLeft; in.right = !goLeft;
            }
        }
        break;
    case Rammer:
        for (size_t j = 1; j < s.getCars().size(); ++j) {   // a car just ahead: drive into it
            const sim::Car &o = s.getCars()[j];
            const float ahead = s.aheadOf(c.s, o.s);
            if (o.wreck == 0 && o.path == c.path && ahead > 0.0f && ahead < 30.0f) {
                const float e = o.d - c.d + 2.0f * std::sin(s.headingAt(c.s) - c.heading);
                in.left = e < -0.2f; in.right = e > 0.2f; in.up = true; in.down = false;
                break;
            }
        }
        break;
    case Risky:
        for (const auto &r : s.sideRamps()) {               // every side ramp, nitro into it
            if (!s.present(r.fromLap)) continue;
            const float dist = s.aheadOf(c.s, r.s);
            if (dist > 0.0f && dist < 80.0f && c.air == 0) {
                const float e = r.d - c.d + 3.0f * std::sin(s.headingAt(c.s) - c.heading);
                in.left = e < -0.25f; in.right = e > 0.25f; in.up = true; in.down = false;
                if (dist < 30.0f && c.nitroCharges > 0) in.nitro = true;
                break;
            }
        }
        if (boostLeft == 0 && c.air == 0 && t % 600 == 0) boostLeft = 300 + int(rnd() * 120.0f);
        if (boostLeft > 0) { in.boost = true; --boostLeft; }
        break;
    case Tilter:
        if (c.wrecks > 0 && c.wreck == 0 && rnd() < 0.04f * float(c.wrecks)) {   // jerks the wheel, more after each wreck
            const bool l = rnd() < 0.5f;
            in.left = l; in.right = !l;
        }
        if (t % 1500 == 700 && c.wrecks == 0 && c.wreck == 0) s.forceWreck(0, false);   // (its first wreck: it has to start somewhere)
        break;
    default:
        break;
    }
    return in;
}

static void profileTest()
{
    const int N = sim::MAX_RACERS;
    static const int MAPS[3] = {1, 5, 4};                   // Canyon Run, River Serpent, Coastal Highway
    std::printf("Player model: simulated players of known styles, 3 maps each (6-car races)\n");
    std::printf("%-8s %-16s %5s %5s %5s %5s %5s | %5s %5s %5s %5s %4s %4s\n", "style", "map", "skill", "cons", "aggr", "risk", "tilt avg/max",
                "rams", "block", "close", "ramps", "bst", ">3s");
    for (int st = 0; st < NUM_STYLES; ++st) {
        sim::PlayerProfile prof;
        prof.name = STYLE_NAME[st];
        for (int mi = 0; mi < 3; ++mi) {
            const int m = MAPS[mi];
            sim::GameSim s;
            s.setMap(m);
            s.setLaps(3);
            s.setCpuSkill(0.95f);                            // (CPUs at the test players' pace, as the game would set them)
            s.setAiSeed(uint32_t(st * 7 + mi + 1));
            s.startRace(sim::GameSim::ModeOnline, N);
            for (int i = 1; i < N; ++i) s.getCars()[i].aiLevel = 1 + (i + mi) % 5;
            int boostLeft = 0;
            uint32_t seed = 77u + uint32_t(st * 31 + mi);
            float maxTilt = 0.0f, sumTilt = 0.0f;
            int nTilt = 0;
            for (int t = 0; t < 60 * 600 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                in[0] = styleInput(s, st, t, boostLeft, seed);
                for (int i = 1; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, true);
                s.step(in, N);
                s.clearEvents();
                maxTilt = std::max(maxTilt, s.raceObs(0).tilt);
                sumTilt += s.raceObs(0).tilt; ++nTilt;
                if (s.getCars()[0].finished || s.getCars()[0].out) break;
            }
            const sim::RaceObs &o = s.raceObs(0);
            const sim::RaceTraits rt = sim::raceTraits(o);
            sim::RaceSummary sum;
            sum.map = m; sum.place = s.rankOf(0); sum.cars = N; sum.wrecks = s.getCars()[0].wrecks; sum.km = o.metres / 1000.0f;
            prof.addRace(o, sum);
            char cons[8] = "    -";
            if (rt.hasConsistency) std::snprintf(cons, sizeof cons, "%5.2f", rt.consistency);
            std::printf("%-8s %-16s %5.2f %5s %5.2f %5.2f %4.2f/%4.2f | %5d %5d %5d %2d/%-2d %4d %4d\n", STYLE_NAME[st], sim::MAPS[m].name,
                        rt.skill, cons, rt.aggression, rt.risk, nTilt ? sumTilt / float(nTilt) : 0.0f, maxTilt,
                        o.rams, o.blocks, o.closePasses, o.rampsTaken, o.rampsOffered, o.boosts, o.boostOver3);
        }
        std::printf("%-8s PROFILE          %5.2f %5.2f %5.2f %5.2f        sure %.2f\n", STYLE_NAME[st], prof.traits.skill,
                    prof.traits.consistency, prof.traits.aggression, prof.traits.risk, prof.sure);
    }
    std::printf("\n");
}

// ---- CPU psychology milestone 2 ------------------------------------------------------
// calib: what the player model measures (its "skill", pace against the clean reference) for
//        CPUs of each precision - the table the game uses to turn your profile into the CPUs'
//        skill centre
// fair:  test drivers of known skill race 5 CPUs centred on them, every map, new minds every
//        race: their wins and places, lead changes, close finishes, winners spread over the
//        CPUs; and what each personality does (measured by the player model)
static bool gTestPerson = false, gDirector = true;   // (fair: the test driver is a person; director on/off)
static bool gBrains = true;                          // (evolved brains on/off: "nobrains" before a mode)
static void cpuRace(int map, uint32_t seed, float centre, int testPersona, float testSkill,
                    std::vector<sim::RaceObs> &obs, std::vector<int> &ranks, std::vector<int> &personas,
                    int &leadChanges, float &gap12, std::vector<float> *skills = nullptr)
{
    const int N = sim::MAX_RACERS;
    sim::GameSim s;
    s.setMap(map);
    s.setAiSeed(seed);
    s.setCpuSkill(centre);
    s.setDirector(gDirector);
    s.setBrains(gBrains);
    s.startRace(sim::GameSim::ModeOnline, N);
    for (int i = 0; i < N; ++i) s.getCars()[i].aiLevel = 1;
    if (testPersona >= 0) { s.getCars()[0].aiPersona = testPersona; s.getCars()[0].aiSkill = testSkill;
                            if (gTestPerson) s.getCars()[0].aiLevel = 0; }
    int leader = -1;
    leadChanges = 0;
    for (int t = 0; t < 60 * 600 && s.getState() != sim::GameSim::Finished; ++t) {
        sim::PlayerInput in[sim::MAX_RACERS];
        for (int i = 0; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, true);
        s.step(in, N);
        s.clearEvents();
        if (t % 60 == 0 && s.getState() == sim::GameSim::Racing && t > 60 * 8) {   // (after the start's shuffle)
            int l = 0;
            for (int i = 0; i < N; ++i) if (s.rankOf(i) == 1) l = i;
            if (leader >= 0 && l != leader) ++leadChanges;
            leader = l;
        }
    }
    obs.clear(); ranks.clear(); personas.clear();
    if (skills) skills->clear();
    float t1 = 0.0f, t2 = 0.0f;
    for (int i = 0; i < N; ++i) {
        const sim::Car &c = s.getCars()[i];
        obs.push_back(s.raceObs(i));
        ranks.push_back(s.rankOf(i));
        personas.push_back(c.aiPersona);
        if (skills) skills->push_back(c.aiSkill);
        if (s.rankOf(i) == 1) t1 = c.finishTime;
        if (s.rankOf(i) == 2) t2 = c.finished ? c.finishTime : t1 + 99.0f;
    }
    gap12 = t2 - t1;
}

static void calibTest()
{
    std::printf("Calibration: CPU precision -> measured skill (6 CPUs all at one precision, 6 maps, 2 seeds)\n");
    for (float sk = 0.0f; sk <= 1.001f; sk += 0.1f) {
        float sum = 0.0f;
        int n = 0;
        for (int m = 0; m < sim::NUM_MAPS; ++m)
            for (uint32_t seed = 1; seed <= 2; ++seed) {
                std::vector<sim::RaceObs> obs; std::vector<int> ranks, personas; int lc; float g;
                // every car at this precision: the spread is switched off by setting each skill
                sim::GameSim probe;
                (void)probe;
                cpuRace(m, seed * 7919u + uint32_t(sk * 100.0f), sk, -1, 0.0f, obs, ranks, personas, lc, g);
                for (const sim::RaceObs &o : obs) { const sim::RaceTraits t = sim::raceTraits(o); if (t.valid) { sum += t.skill; ++n; } }
            }
        std::printf("  precision centre %.1f -> measured skill %.2f\n", sk, n ? sum / float(n) : 0.0f);
    }
    std::printf("\n");
}

static void fairTest(int seeds)
{
    const int N = sim::MAX_RACERS;
    static const float SKILLS[4] = {0.35f, 0.55f, 0.75f, 0.95f};
    static const char *NAMES[4] = {"beginner", "average", "good", "expert"};
    std::printf("Fairness and variety: a test driver (clean, fixed skill%s) against 5 CPUs centred on it, director %s,\n",
                gTestPerson ? ", a person" : "", gDirector ? "ON" : "OFF");
    std::printf("6 maps x %d races, new CPU minds every race\n", seeds);
    std::printf("%-9s %5s %6s %7s | %5s %6s %8s %s\n", "driver", "wins", "place", "podium", "leadch", "close", "winners", "(wins by grid slot 1-5)");
    float pAgg[sim::NUM_PERSONAS] = {}, pRisk[sim::NUM_PERSONAS] = {}, pRams[sim::NUM_PERSONAS] = {}, pBlocks[sim::NUM_PERSONAS] = {};
    float pPlace[sim::NUM_PERSONAS] = {}, pBoost[sim::NUM_PERSONAS] = {};
    int pN[sim::NUM_PERSONAS] = {};
    for (int k = 0; k < 4; ++k) {
        int wins = 0, podium = 0, races = 0, close = 0, lcSum = 0;
        float placeSum = 0.0f;
        int slotWins[6] = {};
        for (int m = 0; m < sim::NUM_MAPS; ++m)
            for (int sd = 0; sd < seeds; ++sd) {
                std::vector<sim::RaceObs> obs; std::vector<int> ranks, personas; int lc; float g;
                cpuRace(m, uint32_t(1000 * k + 37 * m + sd + 1), SKILLS[k], sim::PersonaClean, SKILLS[k], obs, ranks, personas, lc, g);
                ++races;
                wins += ranks[0] == 1;
                podium += ranks[0] <= 3;
                placeSum += float(ranks[0]);
                lcSum += lc;
                close += g < 2.0f;
                for (int i = 0; i < N; ++i) if (ranks[size_t(i)] == 1) ++slotWins[i];
                for (int i = 1; i < N; ++i) {
                    const int p = std::clamp(personas[size_t(i)], 0, sim::NUM_PERSONAS - 1);
                    const sim::RaceTraits t = sim::raceTraits(obs[size_t(i)]);
                    pAgg[p] += t.aggression; pRisk[p] += t.risk; pRams[p] += float(obs[size_t(i)].rams);
                    pBlocks[p] += float(obs[size_t(i)].blocks); pPlace[p] += float(ranks[size_t(i)]);
                    pBoost[p] += float(obs[size_t(i)].boosts);
                    ++pN[p];
                }
            }
        int distinct = 0;
        for (int i = 0; i < N; ++i) distinct += slotWins[i] > 0;
        std::printf("%-9s %4.0f%% %6.2f %6.0f%% | %5.1f %5.0f%% %8d  (%d %d %d %d %d)\n", NAMES[k], 100.0f * float(wins) / float(races),
                    placeSum / float(races), 100.0f * float(podium) / float(races), float(lcSum) / float(races),
                    100.0f * float(close) / float(races), distinct, slotWins[1], slotWins[2], slotWins[3], slotWins[4], slotWins[5]);
    }
    std::printf("\nPersonalities, as the player model measures the CPUs (per car and race)\n");
    std::printf("%-13s %5s %5s %5s %6s %6s %5s\n", "personality", "aggr", "risk", "rams", "blocks", "boosts", "place");
    for (int p = 0; p < sim::NUM_PERSONAS; ++p) {
        if (!pN[p]) continue;
        const float n = float(pN[p]);
        std::printf("%-13s %5.2f %5.2f %5.2f %6.2f %6.2f %5.2f\n", sim::personaDef(p).name, pAgg[p] / n, pRisk[p] / n, pRams[p] / n,
                    pBlocks[p] / n, pBoost[p] / n, pPlace[p] / n);
    }
    std::printf("\n");
}

// ---- CPU psychology milestone 3: habits countered ---------------------------------------
// A test driver with a known habit - always takes the shortcut, always passes on the left -
// races 5 CPUs; once with a profile that tells the director about the habit, once without.
// Shortcut: how often a CPU is on the shortcut with the driver (within 60 m). Passing: how
// often the driver gets past a car, and on which side.
static void habitTest(int seeds)
{
    const int N = sim::MAX_RACERS;
    std::printf("Habits countered: a test driver with a habit, with and without its profile known\n");
    std::printf("%-22s %-8s %8s %8s %8s %8s %10s\n", "habit", "profile", "races", "contest", "passes", "left%", "leftCover");
    struct HabitCase { const char *name; bool shortcut; bool left; };
    static const HabitCase CASES[2] = {{"always the shortcut", true, false}, {"passes on the left", false, true}};
    static const int CUT_MAPS[3] = {1, 2, 3};            // the maps with a shortcut
    for (const HabitCase &hc : CASES) {
        for (int known = 0; known < 2; ++known) {
            int races = 0, contest = 0, passes = 0, left = 0;
            long coverTicks = 0, aheadTicks = 0;              // a CPU just ahead: sitting on the driver's left?
            for (int mi = 0; mi < (hc.shortcut ? 3 : sim::NUM_MAPS); ++mi)
                for (int sd = 0; sd < seeds; ++sd) {
                    const int m = hc.shortcut ? CUT_MAPS[mi] : mi;
                    sim::GameSim s;
                    s.setMap(m);
                    s.setAiSeed(uint32_t(5000 + 31 * m + sd));
                    s.setCpuSkill(0.75f);
                    sim::PlayerHints h;
                    if (known) {
                        h.known = true; h.sure = 0.8f; h.skill = 0.71f;
                        if (hc.shortcut) h.shortcut = 1.0f;
                        if (hc.left) h.passLeft = 1.0f;
                    }
                    s.clearPlayerHints();
                    s.setPlayerHints(0, h);
                    s.startRace(sim::GameSim::ModeOnline, N);
                    for (int i = 0; i < N; ++i) s.getCars()[i].aiLevel = i == 0 ? 0 : 1;
                    s.getCars()[0].aiPersona = sim::PersonaClean;
                    s.getCars()[0].aiSkill = 0.75f;
                    bool contested = false;
                    for (int t = 0; t < 60 * 600 && s.getState() != sim::GameSim::Finished; ++t) {
                        sim::PlayerInput in[sim::MAX_RACERS];
                        for (int i = 0; i < N; ++i) {
                            sim::Car &c = s.getCars()[i];
                            if (i == 0 && hc.shortcut) { c.cutPlan = true; c.cutPlanLap = c.lap; }   // the habit
                            if (i == 0 && hc.left && c.aiTactic == sim::TacticPassRight) c.aiTactic = sim::TacticPassLeft;   // (always left)
                            in[i] = s.computeAIInput(c, i, true);
                        }
                        s.step(in, N);
                        s.clearEvents();
                        const sim::Car &me = s.getCars()[0];
                        for (int j = 1; j < N; ++j) {
                            const sim::Car &o = s.getCars()[j];
                            const float a = s.aheadOf(me.s, o.s);
                            if (o.path == me.path && a > 3.0f && a < 25.0f) { ++aheadTicks; coverTicks += o.d < me.d - 0.3f; }
                        }
                        if (me.path == 1)
                            for (int j = 1; j < N; ++j)
                                if (s.getCars()[j].path == 1 && std::fabs(s.getCars()[j].s - me.s) < 60.0f) contested = true;
                    }
                    ++races;
                    contest += contested;
                    const sim::RaceObs &o = s.raceObs(0);
                    passes += o.passLeft + o.passRight;
                    left += o.passLeft;
                }
            std::printf("%-22s %-8s %8d %7.0f%% %8.2f %7.0f%% %9.0f%%\n", hc.name, known ? "KNOWN" : "unknown", races,
                        hc.shortcut ? 100.0f * float(contest) / float(races) : 0.0f, float(passes) / float(races),
                        passes ? 100.0f * float(left) / float(passes) : 0.0f,
                        aheadTicks ? 100.0f * float(coverTicks) / float(aheadTicks) : 0.0f);
        }
    }
    std::printf("\n");
}

// ---- CPU psychology milestone 4: named drivers, memory, taunts ---------------------------
// A person (the clean test driver) against 5 named drivers. roster: every driver gets races
// and wins about as its skill lean suggests. taunts: how many lines a race, the 20 s gap.
// memory: the same races with a grudge (or determination) from one named driver - how often
// it goes after the person, and how often a determined one is picked as the rival.
static sim::GameSim *namedRace(int map, uint32_t seed, const sim::PlayerHints &h, int firstRoster)
{
    const int N = sim::MAX_RACERS;
    sim::GameSim *s = new sim::GameSim;
    s->setMap(map);
    s->setAiSeed(seed);
    s->setCpuSkill(0.7f);
    s->clearPlayerHints();
    s->setPlayerHints(0, h);
    s->startRace(sim::GameSim::ModeOnline, N);
    for (int i = 0; i < N; ++i) {
        sim::Car &c = s->getCars()[i];
        c.aiLevel = i == 0 ? 0 : 1;
        if (i > 0) { c.rosterId = (firstRoster + i - 1) % sim::ROSTER_SIZE; c.aiPersona = sim::rosterDriver(c.rosterId).persona; }
    }
    s->getCars()[0].aiPersona = sim::PersonaClean;
    s->getCars()[0].aiSkill = 0.7f;
    return s;
}

static void namedTest(int seeds)
{
    const int N = sim::MAX_RACERS;
    int appear[sim::ROSTER_SIZE] = {}, wins[sim::ROSTER_SIZE] = {};
    int races = 0, taunts = 0, moments[sim::NUM_TAUNT_MOMENTS] = {}, tooClose = 0;
    // roster and taunts
    for (int m = 0; m < sim::NUM_MAPS; ++m)
        for (int sd = 0; sd < seeds; ++sd) {
            sim::GameSim *s = namedRace(m, uint32_t(700 + 13 * m + sd), sim::PlayerHints(), (m * 5 + sd * 7) % sim::ROSTER_SIZE);
            int lastTick = -100000;
            for (int t = 0; t < 60 * 600 && s->getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                for (int i = 0; i < N; ++i) in[i] = s->computeAIInput(s->getCars()[i], i, true);
                s->step(in, N);
                for (const sim::SimEvent &e : s->getEvents()) {
                    if (e.type != sim::SimEvent::Taunt) continue;
                    ++taunts;
                    ++moments[std::clamp(e.count, 0, sim::NUM_TAUNT_MOMENTS - 1)];
                    const bool always = e.count == sim::TauntStart || e.count == sim::TauntWin || e.count == sim::TauntLost;
                    if (!always) { if (t - lastTick < sim::GameSim::TAUNT_GAP) ++tooClose; lastTick = t; }
                }
                s->clearEvents();
            }
            ++races;
            for (int i = 1; i < N; ++i) {
                const int r = s->getCars()[i].rosterId;
                ++appear[r];
                if (s->rankOf(i) == 1) ++wins[r];
            }
            delete s;
        }
    std::printf("Named drivers: %d races (a person against 5 named drivers)\n", races);
    for (int r = 0; r < sim::ROSTER_SIZE; ++r)
        std::printf("  %-10s %-12s lean %+.2f  races %3d  wins %3d (%2.0f%%)\n", sim::rosterDriver(r).name,
                    sim::personaDef(sim::rosterDriver(r).persona).name, sim::rosterDriver(r).skillLean, appear[r], wins[r],
                    appear[r] ? 100.0f * float(wins[r]) / float(appear[r]) : 0.0f);
    std::printf("Taunts: %.1f a race; under the 20 s gap: %d. By moment:", float(taunts) / float(races), tooClose);
    static const char *MN[sim::NUM_TAUNT_MOMENTS] = {"start", "passedYou", "youPassed", "youRammed", "rammedYou", "youWrecked", "revenge", "win", "lost"};
    for (int k = 0; k < sim::NUM_TAUNT_MOMENTS; ++k) std::printf(" %s %d", MN[k], moments[k]);
    std::printf("\n\n");

    // memory: a grudge from the driver in car 1, then determination from it
    std::printf("Memory: the named driver in car 1 holds a grudge / is determined against the person\n");
    std::printf("%-12s %8s %12s %12s\n", "attitude", "races", "goesAfter%", "rivalPicked");
    for (int att = 0; att < 3; ++att) {
        long after = 0, near = 0;
        int rival = 0, n = 0;
        for (int m = 0; m < sim::NUM_MAPS; ++m)
            for (int sd = 0; sd < seeds; ++sd) {
                sim::PlayerHints h;
                h.known = true; h.sure = 0.8f; h.skill = 0.66f;
                const int firstRoster = (m + sd) % sim::ROSTER_SIZE;
                if (att == 1) h.attitude[size_t(firstRoster)] = sim::AttitudeGrudge;
                if (att == 2) h.attitude[size_t(firstRoster)] = sim::AttitudeDetermined;
                sim::GameSim *s = namedRace(m, uint32_t(900 + 17 * m + sd), h, firstRoster);
                for (int t = 0; t < 60 * 600 && s->getState() != sim::GameSim::Finished; ++t) {
                    sim::PlayerInput in[sim::MAX_RACERS];
                    for (int i = 0; i < N; ++i) in[i] = s->computeAIInput(s->getCars()[i], i, true);
                    s->step(in, N);
                    s->clearEvents();
                    const sim::Car &c = s->getCars()[1], &me = s->getCars()[0];
                    if (std::fabs(c.progress - me.progress) < 40.0f && c.wreck == 0) {
                        ++near;
                        if ((c.aiTactic == sim::TacticAttack && c.aiTacticCar == 0) || (c.aiTactic == sim::TacticDefend && c.aiTacticCar == 0)
                            || (c.aiMood == sim::MoodRevenge && c.aiMoodTarget == 0)) ++after;
                    }
                    if (t == 60 * 10) rival += s->getCars()[1].dirRole == 1 && s->getCars()[1].dirRivalOf == 0;
                }
                ++n;
                delete s;
            }
        static const char *AN[3] = {"none", "grudge", "determined"};
        std::printf("%-12s %8d %11.0f%% %8d/%d\n", AN[att], n, near ? 100.0f * float(after) / float(near) : 0.0f, rival, n);
    }
    std::printf("\n");
}

// ---- CPU psychology milestone 5: evolved neural brains -----------------------------------
// Whole races (2 laps, obstacles, traffic, ramps) of six CPUs that all drive one way: the
// rules alone, or each of the four bundled brains. Same maps, seeds, skills and personality.
//   pace:   finish time, distance at 120 s, wrecks
//   style:  side ramps taken, air time, L boost, steering flips a minute, hits on others,
//           time near the road edge
//   safety: share of the time the brain drives, safety-net takeovers a minute, stuck
//           (under 10 m in 5 s while racing)
struct BrainRow {
    double finish = 0, dist120 = 0, wrecks = 0, jumps = 0, air = 0, boost = 0, flips = 0, hits = 0, edge = 0;
    double brainShare = 0, takeovers = 0, stuck = 0, minutes = 0, damage = 0;
    int finishers = 0, cars = 0;
};

static BrainRow brainRaces(int kind, int seeds, float skill)
{
    static const int PERSONA[sim::NUM_BRAINS] = {sim::PersonaLateCharger, sim::PersonaShowOff, sim::PersonaAttacker, sim::PersonaClean};
    const int N = sim::MAX_RACERS;
    BrainRow R;
    for (int m = 0; m < sim::NUM_MAPS; ++m)
        for (int sd = 0; sd < seeds; ++sd) {
            sim::GameSim s;
            s.setMap(m);
            s.setAiSeed(uint32_t(4242 + 101 * m + sd));
            s.setCpuSkill(skill);
            s.setDirector(false);
            s.startRace(sim::GameSim::ModeOnline, N);
            const int persona = kind >= 0 ? PERSONA[kind] : PERSONA[sd % sim::NUM_BRAINS];
            for (int i = 0; i < N; ++i) {
                sim::Car &c = s.getCars()[i];
                c.aiLevel = 1;
                c.aiPersona = persona;
                c.aiSkill = std::clamp(skill + 0.04f * float(i - 2), 0.0f, 1.0f);
                if (kind >= 0) s.setBrainOverride(i, sim::bundledBrain(kind), kind);
            }
            if (kind < 0) s.setBrains(false);
            bool pl[sim::MAX_RACERS] = {}, pr[sim::MAX_RACERS] = {};
            float lastProg[sim::MAX_RACERS] = {}, hp[sim::MAX_RACERS];
            for (int i = 0; i < N; ++i) hp[i] = 100.0f;
            int racing = 0;
            for (int t = 0; t < 60 * 400 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                for (int i = 0; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, true);
                s.step(in, N);
                for (const sim::SimEvent &e : s.getEvents())
                    if (e.type == sim::SimEvent::Stunt && e.count <= sim::SimEvent::StuntSaved) R.jumps += 1;
                s.clearEvents();
                if (s.getState() != sim::GameSim::Racing) continue;
                ++racing;
                for (int i = 0; i < N; ++i) {
                    const sim::Car &c = s.getCars()[i];
                    if (c.finished) continue;
                    if (c.wreck == 0 && c.health < hp[i]) R.damage += hp[i] - c.health;
                    hp[i] = c.wreck ? 100.0f : c.health;
                    if (c.air > 0) R.air += 1;
                    if (c.boosting) R.boost += 1;
                    if ((in[i].left && pr[i]) || (in[i].right && pl[i])) R.flips += 1;
                    pl[i] = in[i].left; pr[i] = in[i].right;
                    if (c.air == 0 && std::fabs(c.d) > 0.75f * s.roadHalfAt(c.s, c.path)) R.edge += 1;
                    if (racing % 300 == 0) {
                        if (c.wreck == 0 && racing > 600 && c.progress - lastProg[i] < 10.0f) R.stuck += 1;
                        lastProg[i] = c.progress;
                    }
                    R.minutes += 1.0 / 3600.0;
                }
                if (racing == 60 * 120)
                    for (int i = 0; i < N; ++i) R.dist120 += s.getCars()[i].progress;
            }
            for (int i = 0; i < N; ++i) {
                const sim::Car &c = s.getCars()[i];
                ++R.cars;
                if (c.finished) { ++R.finishers; R.finish += c.finishTime; }
                R.wrecks += c.wrecks;
                R.hits += c.crashesCaused;
                R.takeovers += c.aiTakeovers;
                const int all = c.aiBrainTicks + c.aiRulesTicks;
                R.brainShare += all ? double(c.aiBrainTicks) / all : 0.0;
            }
        }
    return R;
}

static void brainTest(int seeds)
{
    std::printf("Evolved brains: six CPUs driving the same way, whole races, 6 maps x %d seeds, skill 0.80\n", seeds);
    std::printf("%-10s %7s %7s %6s | %5s %6s %6s %6s %5s %5s %6s | %6s %6s %5s\n", "driver", "finish", "dist2m", "wreck",
                "jumps", "air/m", "boost%", "flip/m", "hits", "edge%", "dmg/m", "brain%", "take/m", "stuck");
    for (int k = -1; k < sim::NUM_BRAINS; ++k) {
        if (k >= 0 && !sim::bundledBrain(k)) { std::printf("%-10s (not trained)\n", sim::brainName(k)); continue; }
        const BrainRow R = brainRaces(k, seeds, 0.80f);
        const double cars = std::max(1, R.cars), mins = std::max(1e-6, R.minutes);
        std::printf("%-10s %6.1fs %6.0fm %6.2f | %5.2f %6.1f %5.1f%% %6.1f %5.2f %4.1f%% %6.1f | %5.0f%% %6.2f %5.0f\n",
                    k < 0 ? "RULES" : sim::brainName(k), R.finishers ? R.finish / R.finishers : 0.0, R.dist120 / cars, R.wrecks / cars,
                    R.jumps / cars, R.air / 60.0 / mins, 100.0 * R.boost / 60.0 / 60.0 / mins, R.flips / mins, R.hits / cars,
                    100.0 * R.edge / 60.0 / 60.0 / mins, R.damage / mins, 100.0 * R.brainShare / cars, R.takeovers / mins, R.stuck);
    }
    std::printf("(finish: mean time of the cars that finished; dist2m: metres after 120 s; jumps: clean side-ramp landings a car;\n"
                " air/m: seconds in the air a minute; flip/m: left-right steering flips a minute; edge%%: time near the road edge;\n"
                " brain%%: time the brain drives, the rest is the rules' tricky moments; take/m: safety-net takeovers a minute)\n\n");
}

// ---- CPU psychology milestone 6: a clone of a driver -------------------------------------
// Three test drivers stand in for people: the rules at skill 0.4 (a clean racer), the rules at
// skill 0.9 (an attacker), and the DAREDEVIL brain. Each races 7 times against 5 CPUs and is
// recorded exactly as the game records you (GameSim::cloneView, 20 times a second on the open
// road); a clone is trained (the newest race held out: agreement), then the original and its
// clone each race the same 6 new races. Their driving should match - and differ between drivers.
struct CloneRow {
    double place = 0, finish = 0, skill = 0, aggr = 0, risk = 0, boost = 0, ramps = 0, offroad = 0, wrecks = 0, brain = 0, take = 0;
    int races = 0, finished = 0;
};

static void cloneSetup(sim::GameSim &s, int map, uint32_t seed, int who, bool clone, const sim::Brain *cb)
{
    static const int PERSONA[3] = {sim::PersonaClean, sim::PersonaAttacker, sim::PersonaShowOff};
    static const float SKILL[3] = {0.4f, 0.9f, 0.8f};
    s.setMap(map);
    s.setAiSeed(seed);
    s.setCpuSkill(SKILL[who]);
    s.setDirector(false);
    s.startRace(sim::GameSim::ModeSingle, sim::MAX_RACERS);
    for (int i = 1; i < sim::MAX_RACERS; ++i) s.getCars()[i].aiLevel = 1;
    sim::Car &c = s.getCars()[0];
    c.aiPersona = PERSONA[who];
    c.aiSkill = SKILL[who];
    c.aiLevel = (clone || who == 2) ? 1 : 0;             // (a rules test driver counts as a person)
    if (clone) { c.aiClone = true; s.setCloneBrain(cb); }
    else if (who == 2) s.setBrainOverride(0, sim::bundledBrain(sim::BrainDaredevil), sim::BrainDaredevil);
}

static void cloneRun(sim::GameSim &s, sim::CloneBook *book)
{
    sim::CloneRecorder rec;
    for (int t = 0; t < 60 * 400 && s.getState() != sim::GameSim::Finished; ++t) {
        sim::PlayerInput in[sim::MAX_RACERS];
        for (int i = 0; i < sim::MAX_RACERS; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, true);
        if (book) rec.step(s, 0, in[0], *book);
        s.step(in, sim::MAX_RACERS);
        s.clearEvents();
    }
}

static void cloneTally(const sim::GameSim &s, CloneRow &R)
{
    const sim::Car &c = s.getCars()[0];
    const sim::RaceObs &o = s.raceObs(0);
    const sim::RaceTraits t = sim::raceTraits(o);
    ++R.races;
    R.place += s.rankOf(0);
    if (c.finished) { ++R.finished; R.finish += c.finishTime; }
    R.skill += t.skill; R.aggr += t.aggression; R.risk += t.risk;
    R.boost += o.ticks ? double(o.boostTicks) / o.ticks : 0.0;
    R.ramps += o.rampsOffered ? double(o.rampsTaken) / o.rampsOffered : 0.0;
    R.offroad += o.ticks ? double(o.offroadTicks) / o.ticks : 0.0;
    R.wrecks += c.wrecks;
    const int all = c.aiBrainTicks + c.aiRulesTicks;
    R.brain += all ? double(c.aiBrainTicks) / all : 0.0;
    R.take += c.aiTakeovers;
}

static void cloneTest()
{
    static const char *NAMES[3] = {"rules 0.4 (clean)", "rules 0.9 (attacker)", "DAREDEVIL brain"};
    std::printf("Clones: each test driver recorded for 7 races, a clone trained, then both race the same 6 new races\n");
    std::printf("%-22s %-6s %6s %5s %7s | %5s %5s %5s %6s %6s %5s %6s | %6s %5s\n", "driver", "", "agree", "place", "finish",
                "skill", "aggr", "risk", "boost%", "ramps%", "off%", "wrecks", "brain%", "take");
    for (int who = 0; who < 3; ++who) {
        sim::CloneBook book;
        for (int r = 0; r < 7; ++r) {
            book.beginRace();
            sim::GameSim s;
            cloneSetup(s, r % sim::NUM_MAPS, uint32_t(5100 + 31 * r + 7 * who), who, false, nullptr);
            cloneRun(s, &book);
            book.dropEmptyRace();
        }
        sim::Brain clone;
        sim::CloneStats st;
        const bool ok = sim::trainClone(book, sim::bundledBrain(sim::BrainRacer), clone, st);
        if (!ok) { std::printf("%-22s training failed (%d races, %d samples)\n", NAMES[who], st.races, st.samples); continue; }
        CloneRow R[2];
        for (int k = 0; k < 2; ++k)
            for (int m = 0; m < sim::NUM_MAPS; ++m) {
                sim::GameSim s;
                cloneSetup(s, m, uint32_t(8800 + 13 * m + who), who, k == 1, &clone);
                cloneRun(s, nullptr);
                cloneTally(s, R[k]);
            }
        for (int k = 0; k < 2; ++k) {
            const CloneRow &r = R[k];
            const double n = std::max(1, r.races);
            char agree[16] = "";
            if (k == 1) std::snprintf(agree, sizeof agree, "%.0f%%", 100.0 * st.agree());
            std::printf("%-22s %-6s %6s %5.2f %6.1fs | %5.2f %5.2f %5.2f %5.1f%% %5.0f%% %4.1f%% %6.2f | %5.0f%% %5.2f\n",
                        k ? "" : NAMES[who], k ? "CLONE" : "ORIG", agree, r.place / n, r.finished ? r.finish / r.finished : 0.0,
                        r.skill / n, r.aggr / n, r.risk / n, 100.0 * r.boost / n, 100.0 * r.ramps / n, 100.0 * r.offroad / n,
                        r.wrecks / n, 100.0 * r.brain / n, r.take / n);
        }
        std::printf("%-22s (%d races, %d samples; held-out race: steering %.0f%%, throttle/brake %.0f%%)\n", "", st.races, st.samples,
                    100.0 * st.agreeSteer, 100.0 * st.agreePedal);
    }
    std::printf("(agree: moments on a race it never saw where the clone means the same steering / pedal;\n"
                " skill, aggr, risk: the player model's measures; brain%%: time the clone itself drives)\n\n");
}

// clonefiles <folder>: the rules-0.9 test driver recorded for 7 races and its clone, saved
// as <folder>/TEST_CLONE.clone and .brain (the game's CAR_RACING_AUTOSHOT_CLONE test)
static void cloneFiles(const char *dir)
{
    sim::CloneBook book;
    for (int r = 0; r < 7; ++r) {
        book.beginRace();
        sim::GameSim s;
        cloneSetup(s, r % sim::NUM_MAPS, uint32_t(6100 + 31 * r), 1, false, nullptr);
        cloneRun(s, &book);
        book.dropEmptyRace();
    }
    sim::Brain clone;
    sim::CloneStats st;
    const bool ok = sim::trainClone(book, sim::bundledBrain(sim::BrainRacer), clone, st);
    const std::string base = std::string(dir) + "/TEST_CLONE";
    std::printf("%s: %d races, %d samples, agrees %.0f%%\n", ok && book.save(base + ".clone") && sim::saveCloneBrain(base + ".brain", clone, st)
                ? "saved" : "FAILED", st.races, st.samples, 100.0 * st.agree());
}

int main(int argc, char **argv)
{
    if (argc > 2 && std::strcmp(argv[1], "clonefiles") == 0) { cloneFiles(argv[2]); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "clone") == 0) { cloneTest(); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "nobrains") == 0) { gBrains = false; ++argv; --argc; }
    if (argc > 1 && std::strcmp(argv[1], "brains") == 0) { brainTest(argc > 2 ? std::atoi(argv[2]) : 2); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "named") == 0) { namedTest(argc > 2 ? std::atoi(argv[2]) : 4); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "habits") == 0) { habitTest(argc > 2 ? std::atoi(argv[2]) : 4); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "excite") == 0) {
        const int seeds = argc > 2 ? std::atoi(argv[2]) : 4;
        gTestPerson = true; gDirector = false; fairTest(seeds);
        gDirector = true; fairTest(seeds);
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "calib") == 0) { calibTest(); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "fair") == 0) { fairTest(argc > 2 ? std::atoi(argv[2]) : 4); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "profile") == 0) { profileTest(); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "damage") == 0) { damageRate(); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "lives") == 0) { livesTest(argc > 2 ? std::atoi(argv[2]) : 3); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "jumps") == 0) { jumpTest(); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "crash") == 0) { crashTest(); return 0; }
    const char *diffName[3] = {"EASY", "MEDIUM", "HARD"};
    static const int BAND[3][2] = {{1, 2}, {2, 3}, {3, 4}};   // CPU levels per difficulty (glwidget.cpp)
    std::printf("vs CPU: the reference driver (you) against 1, 3 or 5 CPUs, 5 maps x 6 races\n");
    std::printf("%-6s %-8s %11s %9s\n", "cpus", "level", "your place", "your wins");
    for (int cpus : {1, 3, 5}) {
        for (int d = 0; d < 3; ++d) {
            std::mt19937 pick(1000u + unsigned(d * 10 + cpus));
            float placeSum = 0.0f;
            int wins = 0, races = 0;
            for (int m = 0; m < sim::NUM_MAPS; ++m) {
                for (int k = 0; k < 6; ++k) {
                    sim::GameSim s;
                    s.setMap(m);
                    s.setLaps(1);                             // one lap of each circuit
                    s.startRace(sim::GameSim::ModeSingle, 1 + cpus);
                    for (int i = 1; i <= cpus; ++i)
                        s.getCars()[i].aiLevel = BAND[d][0] + int(pick() % unsigned(BAND[d][1] - BAND[d][0] + 1));
                    for (int t = 0; t < 60 * 300 && s.getState() != sim::GameSim::Finished; ++t) {
                        sim::PlayerInput in[1];
                        in[0] = s.computeAIInput(s.getCars()[0], 0, false);
                        s.step(in, 1);                       // the CPUs drive themselves
                    }
                    const int place = s.rankOf(0);
                    placeSum += float(place);
                    wins += place == 1 ? 1 : 0;
                    ++races;
                }
            }
            std::printf("%-6d %-8s %11.2f %6d/%d\n", cpus, diffName[d], placeSum / races, wins, races);
        }
    }

    // Online CPU racers: levels 1..5 together on every map, against the reference driver
    // (car 0, standing in for a human). Each level starts from every grid slot once
    // (5 rotations x 5 maps = 25 races), so starting position doesn't decide the result.
    std::printf("\nOnline CPU levels: 25 races of 6 cars (reference driver + levels 1-5)\n");
    float rankSum[6] = {};
    int finished[6] = {};
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        for (int rot = 0; rot < 5; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.setLaps(1);                             // one lap of each circuit
            s.startRace(sim::GameSim::ModeOnline, 6);
            int levelOf[6] = {0};
            for (int lv = 1; lv <= 5; ++lv) levelOf[1 + (lv - 1 + rot) % 5] = lv;
            for (int i = 1; i < 6; ++i) s.getCars()[i].aiLevel = levelOf[i];
            for (int t = 0; t < 60 * 300 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[6];
                for (int i = 0; i < 6; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, i > 0);
                s.step(in, 6);
            }
            for (int i = 0; i < 6; ++i) {
                rankSum[levelOf[i]] += float(s.rankOf(i));
                finished[levelOf[i]] += s.getCars()[i].finished ? 1 : 0;
            }
        }
    }
    std::printf("%-14s %8s %8s %8s %8s %8s %8s\n", "", "ref", "lvl 1", "lvl 2", "lvl 3", "lvl 4", "lvl 5");
    std::printf("%-14s", "average place");
    for (int i = 0; i < 6; ++i) std::printf(" %8.2f", rankSum[i] / 25.0f);
    std::printf("\n%-14s", "finished");
    for (int i = 0; i < 6; ++i) std::printf(" %5d/25", finished[i]);
    std::printf("\n");

    // Full room: 6 cars on every map (2 reference drivers standing in for people + levels
    // 2-5), 3 grid rotations each. Counts crashes (a car hitting something, not every frame
    // it stays in contact), how many cars reach the line before the race closes, how many
    // took the shortcut, and the biggest change of a car's race progress in one tick (it
    // must never jump where the shortcut leaves or joins).
    const int N = sim::MAX_RACERS;
    std::printf("\nFull room: 15 races of %d cars (2 reference drivers + levels 2-5)\n", N);
    std::printf("%-16s %10s %10s %14s %10s %10s\n", "map", "winner", "finished", "crashes/car", "shortcut", "max step");
    int allFinished = 0, allCrashes = 0;
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        float winSum = 0.0f;
        int fin = 0, crashes = 0, cut = 0;
        float maxStep = 0.0f;
        for (int rot = 0; rot < 3; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.setLaps(1);                             // one lap of each circuit
            s.startRace(sim::GameSim::ModeOnline, N);
            for (int i = 0; i < N; ++i) s.getCars()[(i + rot * 2) % N].aiLevel = i < 2 ? 0 : i;
            bool wasCrashing[sim::MAX_RACERS] = {}, tookCut[sim::MAX_RACERS] = {};
            float prev[sim::MAX_RACERS];
            for (int i = 0; i < N; ++i) prev[i] = s.getCars()[i].progress;
            for (int t = 0; t < 60 * 300 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[sim::MAX_RACERS];
                for (int i = 0; i < N; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, s.getCars()[i].aiLevel > 0);
                s.step(in, N);
                bool crashing[sim::MAX_RACERS] = {};
                for (const sim::SimEvent &e : s.getEvents())
                    if (e.type == sim::SimEvent::Crash && e.carIndex >= 0 && e.carIndex < N) crashing[e.carIndex] = true;
                for (int i = 0; i < N; ++i) {
                    crashes += (crashing[i] && !wasCrashing[i]) ? 1 : 0;
                    wasCrashing[i] = crashing[i];
                    const sim::Car &c = s.getCars()[i];
                    if (c.path == 1) tookCut[i] = true;
                    if (s.getState() == sim::GameSim::Racing)
                        maxStep = std::max(maxStep, std::fabs(c.progress - prev[i]));
                    prev[i] = c.progress;
                }
            }
            for (int i = 0; i < N; ++i) cut += tookCut[i] ? 1 : 0;
            float best = 999.0f;
            for (const sim::Car &c : s.getCars())
                if (c.finished) { ++fin; best = std::min(best, c.finishTime); }
            winSum += best;
        }
        std::printf("%-16s %9.2fs %7d/%d %14.1f %7d/%d %8.2f m\n", sim::MAPS[m].name, winSum / 3.0f, fin, 3 * N,
                    crashes / (3.0f * N), cut, 3 * N, maxStep);
        allFinished += fin;
        allCrashes += crashes;
    }
    std::printf("%-16s %10s %6d/%d %14.1f\n", "ALL MAPS", "", allFinished, 15 * N, allCrashes / (15.0f * N));
    std::printf("\n");
    crashTest();
    jumpTest();
    return 0;
}
