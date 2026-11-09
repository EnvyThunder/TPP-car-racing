#include "gamesim.h"
#include "clone.h"

// Keep the maths identical on every machine: clang (Mac, especially Apple Silicon) would
// otherwise fuse a * b + c into one FMA instruction that rounds differently from Windows.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif
#include <cmath>
#include <algorithm>
#include <vector>

namespace sim {

// Look, traffic and obstacle spacing of each map; the road is the map's circuit
// (shared/circuits.cpp).
const MapDef MAPS[NUM_MAPS] = {
    {"SUNSET VALLEY",   "ROLLING COUNTRYSIDE AT GOLDEN HOUR", MapTheme::Countryside, 16, 95.0f, 130.0f, 1},
    {"CANYON RUN",      "FAST SWEEPERS THROUGH RED ROCK DESERT", MapTheme::Desert, 10, 105.0f, 145.0f, 2},
    {"ALPINE PASS",     "TIGHT SNOWY SWITCHBACKS - BRAKE EARLY", MapTheme::Snow, 8, 110.0f, 150.0f, 3},
    {"NEON CITY",       "NIGHT STREETS - SQUARE CORNERS - TRAFFIC", MapTheme::City, 22, 95.0f, 125.0f, 3},
    {"COASTAL HIGHWAY", "FLAT-OUT OCEAN ROAD LINED WITH PALMS", MapTheme::Coast, 14, 100.0f, 140.0f, 1},
    {"RIVER SERPENT",   "HAIRPINS, A SPIRAL, BRIDGES OVER THE RIVER", MapTheme::Countryside, 12, 100.0f, 140.0f, 3},
    {"DESERT ZIGZAG",   "NINE STACKED HAIRPINS UP A DESERT RIDGE", MapTheme::Desert, 10, 105.0f, 145.0f, 3},
    {"GHAT ROAD",       "JUNGLE HAIRPINS AND A CLIFF SPIRAL IN THE MIST", MapTheme::Jungle, 14, 100.0f, 140.0f, 3},
};

GameSim::GameSim(uint32_t seed)
    : rng(seed)
{
    setMap(0);
    reset(seed);
}

// A random float in [a, b) that is identical on every platform. std::mt19937 itself is
// fully specified by the C++ standard, but std::uniform_real_distribution is not: Windows
// (MinGW) and Mac (Clang) return different values. Every player's game rebuilds the track
// (obstacles, nitro cans, traffic, scenery) from the same seed, so it must match exactly.
static float uniformFloat(std::mt19937 &g, float a, float b)
{
    return a + (b - a) * float(double(g()) * (1.0 / 4294967296.0));
}

float GameSim::rnd(float a, float b)
{
    return uniformFloat(rng, a, b);
}

static float wrapAngle(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

// ---------------------------------------------------------------------------
//  The circuit
// ---------------------------------------------------------------------------
void GameSim::setMap(int m)
{
    static_assert(NUM_MAPS <= PM_MAPS, "the player model keeps habits for PM_MAPS maps: raise PM_MAPS");
    mapIndex = (m % NUM_MAPS + NUM_MAPS) % NUM_MAPS;
    // The whole layout: hills, banking, ramps, the markers of tunnels, bridges and the raised
    // highway (drawn by the game) and the shortcut
    const CircuitDef &def = draftCircuits()[size_t(mapIndex)];
    std::vector<int> jumpPts;
    for (size_t i = 0; i < def.pts.size(); ++i)
        if (def.pts[i].flags & TF_JUMP) jumpPts.push_back(int(i));
    track.build(def);
    lapLen = float(track.slices().size());
    sideList.clear();                                    // (reset places the side ramps)
    sideAt.clear();

    // Ramps at the jump points (milestone 1c). The lip is this many degrees steeper than
    // the road (set with tools/ai_bench so every launch lands on the road): the ramp rises
    // h = run x tan(angle) / 2 on a curve that steepens to the lip, then drops back at 20%.
    rampList.clear();
    for (int i : jumpPts) {
        const float a = def.rampLipDeg * PI / 180.0f, tanA = a * (1.0f + a * a / 3.0f);
        Ramp r;
        r.lip = track.pointS(i);
        r.height = r.run * tanA * 0.5f;
        r.back = r.height / 0.2f;
        rampList.push_back(r);
    }
    // More jumps on every map (the owner asked for them): up to EXTRA_RAMPS more, found on the
    // track itself - so a new map gets them too. A spot needs a straight, flat, open road from
    // 40 m before the lip to 180 m after it (the car lands on the road), 500 m from any other
    // ramp, 300 m from the start and finish and 250 m from the shortcut's fork and merge.
    // (straight enough: under 0.3 rad of turn over the 150 m after the lip and no bend tighter
    // than 120 m; flat: under 6% and changing less than 3%)
    {
        const auto &sl = track.slices();
        const float n = float(sl.size());
        auto dist = [n](float a, float b) { float d = std::fabs(a - b); return std::min(d, n - d); };
        const float first = track.sprint() ? track.startS() + 300.0f : 300.0f;
        const float last = track.sprint() ? track.finishS() - 300.0f : n - 300.0f;
        int added = 0;
        for (float s = first; s < last && added < EXTRA_RAMPS; s += 10.0f) {
            bool ok = true;
            for (const Ramp &o : rampList) if (dist(s, o.lip) < 500.0f) { ok = false; break; }
            if (ok && track.shortcut().size() > 2)
                ok = dist(s, track.shortcutFromS()) > 250.0f && dist(s, track.shortcutToS()) > 250.0f;
            if (ok && !track.sprint()) ok = dist(s, 0.0f) > 300.0f;
            const float g0 = sl[size_t(s) % sl.size()].grade;
            float turn = 0.0f;
            for (int k = -40; ok && k <= 180; k += 2) {
                const Slice &q = sl[size_t(int(s) + k + int(n)) % sl.size()];
                if (k >= 0 && k <= 150) turn += 2.0f * q.curvature;
                ok = q.flags == 0 && std::fabs(q.curvature) < 1.0f / 120.0f && std::fabs(q.grade) < 0.06f
                     && std::fabs(q.grade - g0) < 0.03f && std::fabs(q.bank) < 0.08f && std::fabs(turn) < 0.3f;
            }
            if (!ok) continue;
            const float a = def.rampLipDeg * PI / 180.0f, tanA = a * (1.0f + a * a / 3.0f);
            Ramp r;
            r.lip = s;
            r.height = r.run * tanA * 0.5f;
            r.back = r.height / 0.2f;
            rampList.push_back(r);
            ++added;
        }
        std::sort(rampList.begin(), rampList.end(), [](const Ramp &x, const Ramp &y) { return x.lip < y.lip; });
    }
    lift.assign(track.slices().size(), 0.0f);
    liftGrade.assign(track.slices().size(), 0.0f);
    for (const Ramp &r : rampList)
        for (int k = int(-r.run) - 1; k <= int(r.back) + 1; ++k) {
            const size_t i = size_t(wrapS(r.lip + float(k)));
            const float u = (float(k) + r.run) / r.run;            // 0..1 up the ramp
            if (k <= 0 && u >= 0.0f) {
                lift[i] = r.height * u * u;
                liftGrade[i] = 2.0f * r.height * u / r.run;
            } else if (k > 0 && float(k) <= r.back) {
                lift[i] = r.height * (1.0f - float(k) / r.back);
                liftGrade[i] = -0.2f;
            }
        }

    // Where each metre of the shortcut counts along the main road (race progress). Near the
    // fork and the merge: its nearest point on the main road, while that is within 30 m (so
    // never a far part of a bend); in between evenly; never going backwards.
    cutMain.clear();
    forkReach = mergeReach = 0.0f;
    const auto &CUT = track.shortcut();
    if (CUT.size() > 2) {
        const int n = int(CUT.size());
        const float from = track.shortcutFromS(), skipped = track.shortcutToS() - from;
        cutMain.assign(size_t(n), -1.0f);
        // (searched over the whole stretch it skips: where the two roads run side by side for
        // a long way, a shortcut metre is not a main-road metre, so a guess can be far out)
        auto mainAt = [&](int k) {
            float best = 30.0f * 30.0f, at = -1.0f;
            for (int j = 0; j <= int(skipped); ++j) {
                float cx, cz;
                pointAt(from + float(j), 0.0f, cx, cz);
                const float dx = CUT[size_t(k)].x - cx, dz = -CUT[size_t(k)].z - cz;
                if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; at = float(j); }
            }
            return at;
        };
        int a = 0, b = n - 1;
        cutMain[0] = 0.0f;
        while (a + 1 < n && (cutMain[size_t(a + 1)] = mainAt(a + 1)) >= 0.0f) ++a;
        cutMain[size_t(n - 1)] = skipped;
        while (b - 1 > a && (cutMain[size_t(b - 1)] = mainAt(b - 1)) >= 0.0f) --b;
        // how far the two roads run side by side (a shallow fork overlaps a long way): no
        // obstacles there, as a car on the shortcut could meet them
        forkReach = cutMain[size_t(a)];
        mergeReach = skipped - cutMain[size_t(b)];
        for (int k = a + 1; k < b; ++k)
            cutMain[size_t(k)] = cutMain[size_t(a)] + (cutMain[size_t(b)] - cutMain[size_t(a)]) * float(k - a) / float(b - a);
        for (int k = 1; k < n; ++k) cutMain[size_t(k)] = std::max(cutMain[size_t(k)], cutMain[size_t(k - 1)]);
    }

    // sine of each slice's banking, by its series (the same on every system)
    bankSin.clear();
    for (const Slice &sl : track.slices()) {
        const float r = sl.bank * PI / 180.0f, r2 = r * r;
        bankSin.push_back(r * (1.0f - r2 / 6.0f * (1.0f - r2 / 20.0f)));
    }

    // slices of both roads by 32 m square, for roadDistance() (shortcut slice k is stored
    // as -1 - k); the sea line of Coastal Highway
    const auto &S = track.slices();
    const auto &CS = track.shortcut();
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (const auto *R : {&S, &CS})
        for (const Slice &sl : *R) {
            x0 = std::min(x0, sl.x); x1 = std::max(x1, sl.x);
            z0 = std::min(z0, -sl.z); z1 = std::max(z1, -sl.z);
        }
    gridX0 = x0 - 64.0f; gridZ0 = z0 - 64.0f;
    gridW = int((x1 - x0 + 128.0f) / 32.0f) + 1;
    gridH = int((z1 - z0 + 128.0f) / 32.0f) + 1;
    grid.assign(size_t(gridW * gridH), std::vector<int>());
    for (int i = 0; i < int(S.size()); ++i) {
        int gx = int((S[i].x - gridX0) / 32.0f), gz = int((-S[i].z - gridZ0) / 32.0f);
        grid[size_t(gz * gridW + gx)].push_back(i);
    }
    for (int k = 0; k < int(CS.size()); ++k) {
        int gx = int((CS[k].x - gridX0) / 32.0f), gz = int((-CS[k].z - gridZ0) / 32.0f);
        grid[size_t(gz * gridW + gx)].push_back(-1 - k);
    }
    seaX = (MAPS[mapIndex].theme == MapTheme::Coast) ? x1 + 70.0f : 1e9f;

    // the temple stands 150 m beside the road, as near the start as it fits, with its
    // steps facing the road
    landX = landZ = landRot = 0.0f;
    for (float s = 0.0f; s < lapLen; s += 10.0f) {
        bool found = false;
        for (float side : {1.0f, -1.0f}) {
            float x, z;
            pointAt(s, side * 150.0f, x, z);
            if (roadDistance(x, z, 140.0f) < 140.0f || x > seaX - 90.0f) continue;
            const Slice &sl = S[size_t(s)];
            landX = x; landZ = z;
            landRot = std::atan2(-side * sl.tz, -side * sl.tx) * 180.0f / PI;
            found = true;
            break;
        }
        if (found) break;
    }
}

float GameSim::wrapS(float s) const
{
    if (sprint()) return std::clamp(s, 0.0f, lapLen - 1.001f);   // an open road ends
    s = std::fmod(s, lapLen);
    return s < 0.0f ? s + lapLen : s;
}

float GameSim::aheadOf(float from, float to) const
{
    if (sprint()) return to - from;
    const float d = wrapS(to - from);
    return d > 0.5f * lapLen ? d - lapLen : d;
}

void GameSim::sliceAt(float s, int path, int &i, int &j, float &f) const
{
    if (path == 0) {
        const int n = int(lapLen);
        s = wrapS(s);
        i = std::min(int(s), n - 1);
        j = sprint() ? std::min(i + 1, n - 1) : (i + 1) % n;
    } else {
        const int n = int(track.shortcut().size());
        s = std::clamp(s, 0.0f, float(n - 1));
        i = std::min(int(s), n - 2);
        j = i + 1;
    }
    f = std::clamp(s - float(i), 0.0f, 1.0f);
}

// the layout's x east / z north becomes the world's x / -z; to the driver's right is (tz, tx)
void GameSim::pointAt(float s, float d, float &x, float &z, int path) const
{
    const auto &S = road(path);
    int i, j;
    float f;
    sliceAt(s, path, i, j, f);
    const Slice &a = S[size_t(i)], &b = S[size_t(j)];
    float tx = a.tx + (b.tx - a.tx) * f, tz = a.tz + (b.tz - a.tz) * f;
    const float len = std::sqrt(tx * tx + tz * tz);
    tx /= len; tz /= len;
    x = a.x + (b.x - a.x) * f + tz * d;
    z = -(a.z + (b.z - a.z) * f) + tx * d;
}

float GameSim::headingAt(float s, int path) const
{
    int i, j;
    float f;
    sliceAt(s, path, i, j, f);
    const Slice &a = road(path)[size_t(i)];
    return std::atan2(a.tx, a.tz);                     // facing (sin h, -cos h) = (tx, -tz)
}

// around the shortcut's fork (150 m before, at least 50 m after: as far as the two roads run
// side by side, plus 30 m) and merge (the same the other way round)
bool GameSim::nearJoin(float s) const
{
    if (!hasShortcut()) return false;
    const float toFork = aheadOf(s, shortcutFrom()), toMerge = aheadOf(s, shortcutTo());
    const float afterFork = std::max(50.0f, forkReach + 30.0f), beforeMerge = std::max(50.0f, mergeReach + 30.0f);
    return (toFork <= 150.0f && toFork >= -afterFork) || (toMerge <= beforeMerge && toMerge >= -150.0f);
}

bool GameSim::nearRamp(float s, float before, float after) const
{
    for (const Ramp &r : rampList) {
        const float ahead = aheadOf(s, r.lip);                  // > 0: the lip is still ahead
        if (ahead <= before && ahead >= -after) return true;
    }
    return false;
}

// (the shortcut has no banking and no ramps)
float GameSim::heightAt(float s, float d, int path) const
{
    const auto &S = road(path);
    int i, j;
    float f;
    sliceAt(s, path, i, j, f);
    if (path) return S[size_t(i)].y + (S[size_t(j)].y - S[size_t(i)].y) * f;
    const float y = S[size_t(i)].y + lift[size_t(i)]
                  + (S[size_t(j)].y + lift[size_t(j)] - S[size_t(i)].y - lift[size_t(i)]) * f;
    const float b = bankSin[size_t(i)] + (bankSin[size_t(j)] - bankSin[size_t(i)]) * f;
    return y - d * b;
}

float GameSim::sideLift(float s, float d) const
{
    if (sideAt.empty()) return 0.0f;
    const int k = sideAt[size_t(std::clamp(int(wrapS(s)), 0, int(sideAt.size()) - 1))] - 1;
    if (k < 0) return 0.0f;
    const SideRamp &r = sideList[size_t(k)];
    const float dd = d - r.d;
    if (!present(r.fromLap) || std::fabs(dd) > r.half) return 0.0f;
    const float u = 1.0f - aheadOf(s, r.s) / r.run;         // 0 at the foot .. 1 at the lip
    if (u < 0.0f || u > 1.0f) return 0.0f;
    return r.height * u * u * (1.0f + 0.4f * r.twist * dd / r.half);   // twisted: one side higher
}

// Off the road here: a river under Sunset Valley's bridges, the harbour under Coastal
// Highway's, the sea east of the coast road
bool GameSim::overWater(const Car &c) const
{
    if (c.x > seaX - 30.0f) return true;
    const MapTheme t = MAPS[mapIndex].theme;
    if (t != MapTheme::Countryside && t != MapTheme::Coast && t != MapTheme::Jungle) return false;
    const int i = std::clamp(int(wrapS(c.outS)), 0, int(lapLen) - 1);
    return track.slices()[size_t(i)].flags & TF_BRIDGE;
}

bool GameSim::overGorge(float s) const
{
    const MapTheme t = MAPS[mapIndex].theme;
    if (t == MapTheme::Countryside || t == MapTheme::Coast || t == MapTheme::Jungle) return false;
    return track.slices()[size_t(std::clamp(int(wrapS(s)), 0, int(lapLen) - 1))].flags & TF_BRIDGE;
}

float GameSim::lastThird() const
{
    return sprint() ? startLine() + (finishLine() - startLine()) * 2.0f / 3.0f : lapLen * 2.0f / 3.0f;
}

void GameSim::updateLeadLap()
{
    leadLap = 0;
    if (sprint()) return;
    for (const Car &c : cars) if (!c.out) leadLap = std::max(leadLap, c.lap);
}

// Side ramps: in the first two thirds of the road one about every 600 m, in the last third
// one every 230-320 m, higher (more air, harder landings: danger 1.5) and twisted more often;
// a circuit's last lap gets as many again in its last third. Never in or near a tunnel, under
// another road, at a big ramp, the shortcut's ends or the start, nor on top of obstacles or
// nitro cans; each one leaves 260 m of open sky to fly through.
void GameSim::placeSideRamps(uint32_t seed)
{
    sideList.clear();
    sideAt.assign(size_t(lapLen), 0);
    std::mt19937 g(seed * 2654435761u + 977u);
    auto R = [&g](float a, float b) { return uniformFloat(g, a, b); };
    const auto &S = track.slices();
    const float s0 = startLine(), third = lastThird();
    const float sEnd = sprint() ? finishLine() - 200.0f : lapLen - 220.0f;
    auto overhead = [&](int k) {                         // another part of the road above this one
        const float x = S[size_t(k)].x, z = -S[size_t(k)].z;
        const int gx = int((x - gridX0) / 32.0f), gz = int((z - gridZ0) / 32.0f);
        for (int cz = gz - 1; cz <= gz + 1; ++cz)
            for (int cx = gx - 1; cx <= gx + 1; ++cx) {
                if (cx < 0 || cz < 0 || cx >= gridW || cz >= gridH) continue;
                for (int j : grid[size_t(cz * gridW + cx)]) {
                    if (j < 0 || std::fabs(aheadOf(float(k), float(j))) < 300.0f) continue;
                    const float dx = S[size_t(j)].x - x, dz = -S[size_t(j)].z - z;
                    if (dx * dx + dz * dz < 22.0f * 22.0f && S[size_t(j)].y > S[size_t(k)].y + 3.0f) return true;
                }
            }
        return false;
    };
    auto fits = [&](float s, float run) {
        if (s - run < s0 + 300.0f || s > sEnd) return false;
        if (nearRamp(s, 320.0f, 150.0f) || nearJoin(s - run) || nearJoin(s + 200.0f)) return false;
        for (float k = s - run - 20.0f; k <= s + 260.0f; k += 4.0f) {
            const int i = int(wrapS(k));
            if (S[size_t(i)].flags & (TF_TUNNEL | TF_OVERPASS)) return false;
            if (int(k) % 8 == 0 && overhead(i)) return false;
        }
        for (const SideRamp &o : sideList) if (std::fabs(aheadOf(o.s, s)) < 80.0f) return false;
        // the road may bend after it - that is the driver's job in the air - but not more
        // than 0.7 rad over the first 170 m (no flying straight off into a hairpin)
        for (float k = 10.0f; k <= 170.0f; k += 10.0f)
            if (std::fabs(wrapAngle(headingAt(s + k) - headingAt(s))) > 0.7f) return false;
        for (const NitroPickup &n : nitroPickups) { const float a = aheadOf(s, n.s); if (a > -run - 30.0f && a < 30.0f) return false; }
        return true;
    };
    auto add = [&](float s, int fromLap) -> float {     // returns where the ramp went
        const bool late = s >= third;
        SideRamp r;
        r.run = 16.0f;
        float at = s;                                    // (no room here: try a little further on)
        while (at < s + 240.0f && !fits(at, r.run)) at += 20.0f;
        if (at >= s + 240.0f) return s + 240.0f;
        r.s = at;
        const float side = R(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
        r.half = 2.2f;
        r.d = side * (halfWidth - 2.4f);
        r.height = late ? R(1.8f, 2.1f) : R(1.3f, 1.6f);
        r.danger = late ? 1.5f : 1.0f;
        if (R(0.0f, 1.0f) < (late ? 0.55f : 0.30f)) r.twist = R(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
        r.fromLap = fromLap;
        sideList.push_back(r);
        return r.s;
    };
    // (about twice as many as before - the owner asked for more jumps)
    for (float s = s0 + 450.0f; s < sEnd;) { s = add(s, 0); s += s < third ? R(260.0f, 350.0f) : R(150.0f, 200.0f); }
    if (!sprint() && laps > 1)
        for (float s = third + 90.0f; s < sEnd;) { s = add(s, laps - 1); s += R(150.0f, 200.0f); }
    for (size_t k = 0; k < sideList.size(); ++k)
        for (int m = 0; m <= int(sideList[k].run) + 1; ++m)
            sideAt[size_t(wrapS(sideList[k].s - float(m)))] = int16_t(k + 1);
}

float GameSim::gradeAt(float s, int path) const
{
    const auto &S = road(path);
    int i, j;
    float f;
    sliceAt(s, path, i, j, f);
    if (path) return S[size_t(i)].grade + (S[size_t(j)].grade - S[size_t(i)].grade) * f;
    return S[size_t(i)].grade + liftGrade[size_t(i)]
         + (S[size_t(j)].grade + liftGrade[size_t(j)] - S[size_t(i)].grade - liftGrade[size_t(i)]) * f;
}

float GameSim::bankAt(float s, int path) const
{
    if (path) return 0.0f;
    int i, j;
    float f;
    sliceAt(s, 0, i, j, f);
    return bankSin[size_t(i)] + (bankSin[size_t(j)] - bankSin[size_t(i)]) * f;
}

void GameSim::project(float x, float z, float sHint, float &s, float &d, int path, bool wide) const
{
    const int hint = path ? int(std::clamp(sHint, 0.0f, shortcutEnd())) : int(wrapS(sHint));
    int i;
    float t;
    nearestSlice(x, z, hint, i, t, path, wide);
    s = float(i) + t;
    float cx, cz;
    pointAt(s, 0.0f, cx, cz, path);
    const Slice &sl = road(path)[size_t(i)];
    d = (x - cx) * sl.tz + (z - cz) * sl.tx;
}

float GameSim::heightNear(float x, float z, float sHint, int path) const
{
    float s, d;
    project(x, z, sHint, s, d, path);
    return heightAt(s, d, path);
}

int GameSim::sliceNear(float x, float z, float reach) const
{
    const auto &S = track.slices();
    const int gx = int(std::floor((x - gridX0) / 32.0f)), gz = int(std::floor((z - gridZ0) / 32.0f));
    const int rings = int(reach / 32.0f) + 1;
    float best = reach * reach;
    int found = -1;
    for (int dz = -rings; dz <= rings; ++dz)
        for (int dx = -rings; dx <= rings; ++dx) {
            const int cx = gx + dx, cz = gz + dz;
            if (cx < 0 || cz < 0 || cx >= gridW || cz >= gridH) continue;
            for (int i : grid[size_t(cz * gridW + cx)]) {
                if (i < 0) continue;                     // (the shortcut)
                const float ex = S[size_t(i)].x - x, ez = -S[size_t(i)].z - z, d2 = ex * ex + ez * ez;
                if (d2 < best) { best = d2; found = i; }
            }
        }
    return found;
}

float GameSim::roadHalfAt(float s, int path) const
{
    int i, j;
    float f;
    sliceAt(s, path, i, j, f);
    return halfWidth * road(path)[size_t(i)].width;
}

// The nearest point of the centre line to (x, z): slice and fraction to the next one.
// Searched near the hint (so a crossing road far along the lap is never picked by
// mistake), the whole road only if nothing near is within 60 m.
void GameSim::nearestSlice(float x, float z, int hint, int &slice, float &t, int path, bool wide) const
{
    const auto &S = road(path);
    const int n = int(S.size());
    int bi = std::clamp(hint, 0, n - 1);
    float bt = 0.0f, bd2 = 1e30f;
    const bool open = path != 0 || sprint();             // an open road: no segment from its end back to its start
    auto search = [&](int from, int to) {
        if (open) { from = std::max(from, 0); to = std::min(to, n - 2); }
        for (int k = from; k <= to; ++k) {
            const int i = open ? k : ((k % n) + n) % n, j = open ? k + 1 : (i + 1) % n;
            const float ax = S[size_t(i)].x, az = -S[size_t(i)].z;
            const float ex = S[size_t(j)].x - ax, ez = -S[size_t(j)].z - az, l2 = ex * ex + ez * ez;
            const float u = l2 > 0.0f ? std::clamp(((x - ax) * ex + (z - az) * ez) / l2, 0.0f, 1.0f) : 0.0f;
            const float px = ax + ex * u - x, pz = az + ez * u - z, d2 = px * px + pz * pz;
            if (d2 < bd2) { bd2 = d2; bi = i; bt = u; }
        }
    };
    search(hint - 40, hint + 40);
    if (wide && bd2 > 60.0f * 60.0f) search(0, n - 1);
    slice = bi;
    t = bt;
}

// Race progress: laps x lap length + distance into the lap. On the shortcut it is where
// that metre counts along the main road (the cutMain table, see setMap): positions stay
// fair and nothing jumps when a car changes road.
float GameSim::progressOf(const Car &c) const
{
    if (sprint()) return c.s - startLine();               // a sprint: metres past the start line
    const float lap = float(c.lap) * lapLen;
    if (c.path == 0) return lap + c.s;
    int i, j;
    float f;
    sliceAt(c.s, 1, i, j, f);
    return lap + shortcutFrom() + cutMain[size_t(i)] + (cutMain[size_t(j)] - cutMain[size_t(i)]) * f;
}

// Find where the car is: the nearest point of its road's centre line. Where the shortcut
// leaves or joins the main road the car may cross onto the other road (the nearer centre
// line wins, by half a metre so it doesn't flicker; past the shortcut's ends it is back on
// the main road). Crossing the start/finish line forwards starts a lap, backwards takes
// one off.
void GameSim::locate(Car &c) const
{
    float sNew, dNew;
    project(c.x, c.z, float(c.hint), sNew, dNew, c.path);
    if (hasShortcut()) {
        const float end = shortcutEnd();
        float os = 0.0f, od = 0.0f;
        int other = -1;
        // a car changes road once it has left its own road's asphalt and the other road's
        // centre line is nearer (by half a metre). The search on the other road starts as many
        // metres from its end as the car is from that end, and its answer must lead back to
        // the car (a search that ran out of range would give the wrong place).
        auto backToCar = [&](float s, float d, int path) {
            float x, z;
            pointAt(s, d, x, z, path);
            return (x - c.x) * (x - c.x) + (z - c.z) * (z - c.z) < 1.5f * 1.5f;
        };
        if (c.path == 0) {
            const float toFork = aheadOf(sNew, shortcutFrom()), toMerge = aheadOf(sNew, shortcutTo());
            if (c.cutClosed) {}                          // (closed to the leaders: they stay on the main road)
            else if (toFork < 5.0f && toFork > -100.0f)  { project(c.x, c.z, std::max(0.0f, -toFork), os, od, 1); other = 1; }
            else if (toMerge < 100.0f && toMerge > -5.0f) { project(c.x, c.z, end - std::max(0.0f, toMerge), os, od, 1); other = 1; }
            if (other == 1 && (os < 0.5f || os > end - 0.5f || std::fabs(dNew) < roadHalfAt(sNew) - 1.0f
                               || std::fabs(od) + 0.5f >= std::fabs(dNew) || !backToCar(os, od, 1))) other = -1;
        } else if (sNew < 100.0f || sNew > end - 100.0f) {
            project(c.x, c.z, sNew < 100.0f ? shortcutFrom() + sNew : shortcutTo() - (end - sNew), os, od, 0);
            if ((std::fabs(dNew) > roadHalfAt(sNew, 1) - 0.5f && std::fabs(od) + 0.5f < std::fabs(dNew) && backToCar(os, od, 0))
                || sNew <= 0.01f || sNew >= end - 0.01f) other = 0;
        }
        if (other >= 0) {                                // onto the other road (no line crossed)
            c.path = other;
            c.s = os;
            c.d = od;
            c.hint = int(os);
            c.progress = progressOf(c);
            return;
        }
    }
    if (sprint()) {
        // the road's ends: the car stops there (the start of the run-up, the end of the run-off)
        const float lo = 4.0f, hi = lapLen - 8.0f;
        if (sNew < lo || sNew > hi) {
            sNew = std::clamp(sNew, lo, hi);
            pointAt(sNew, dNew, c.x, c.z);
            c.speed = 0.0f;
        }
    } else if (c.path == 0) {
        if (c.s > 0.75f * lapLen && sNew < 0.25f * lapLen) {
            ++c.lap;
            if (c.lap >= 1 && c.lapStart >= 0.0f) {      // a whole lap done: its time
                const float t = raceTimer - c.lapStart;
                if (c.bestLap <= 0.0f || t < c.bestLap) c.bestLap = t;
            }
            c.lapStart = raceTimer;
        } else if (c.s < 0.25f * lapLen && sNew > 0.75f * lapLen) {
            --c.lap;
            c.lapStart = -1.0f;                          // this lap doesn't count for a lap time
        }
    }
    c.s = sNew;
    c.d = dNew;
    c.hint = int(sNew);
    c.progress = progressOf(c);
}

bool GameSim::insideOtherRoad(const Car &c) const
{
    if (!hasShortcut()) return false;
    float os, od;
    if (c.path == 0) {
        const float toFork = aheadOf(c.s, shortcutFrom()), toMerge = aheadOf(c.s, shortcutTo());
        if (toFork < 5.0f && toFork > -100.0f && c.cutClosed) return false;   // (closed: the rail runs across the fork)
        if (toFork < 5.0f && toFork > -100.0f)        project(c.x, c.z, 0.0f, os, od, 1);
        else if (toMerge < 100.0f && toMerge > -5.0f) project(c.x, c.z, shortcutEnd(), os, od, 1);
        else return false;
        return std::fabs(od) < roadHalfAt(os, 1) + 3.0f;
    }
    if (c.s > 100.0f && c.s < shortcutEnd() - 100.0f) return false;
    project(c.x, c.z, c.s < 100.0f ? shortcutFrom() : shortcutTo(), os, od, 0);
    return std::fabs(od) < roadHalfAt(os) + 3.0f;
}

void GameSim::alongRoad(const Car &c, float ahead, int &path, float &s) const
{
    path = c.path;
    s = c.s + ahead;
    if (!hasShortcut()) return;
    const float end = shortcutEnd();
    if (c.path == 1) {
        if (s > end) { path = 0; s = shortcutTo() + (s - end); }
    } else if (c.cutPlan && c.cutPlanLap == c.lap) {
        // still on the main road but taking the shortcut: from the fork on (and for its first
        // 100 m, until the car counts as on it) follow the shortcut
        const float toFork = aheadOf(c.s, shortcutFrom());
        if (toFork > -100.0f && ahead > toFork) { path = 1; s = std::min(ahead - toFork, end); }
    }
}

void GameSim::aheadPoint(const Car &c, float ahead, float d, float &x, float &z) const
{
    int path;
    float s;
    alongRoad(c, ahead, path, s);
    if (path == 1) d = std::clamp(d, 1.0f - roadHalfAt(s, 1), roadHalfAt(s, 1) - 1.0f);
    pointAt(s, d, x, z, path);
}

float GameSim::headingAhead(const Car &c, float ahead) const
{
    int path;
    float s;
    alongRoad(c, ahead, path, s);
    return headingAt(s, path);
}

// Only the squares that can hold a slice within `reach` are searched: a road further away
// than that is reported as far (1e9). Both roads count.
float GameSim::roadDistance(float x, float z, float reach) const
{
    const auto &S = track.slices();
    const auto &CS = track.shortcut();
    const int gx = int(std::floor((x - gridX0) / 32.0f)), gz = int(std::floor((z - gridZ0) / 32.0f));
    const int rings = int(reach / 32.0f) + 1;
    float best = 1e18f;
    for (int dz = -rings; dz <= rings; ++dz)
        for (int dx = -rings; dx <= rings; ++dx) {
            const int cx = gx + dx, cz = gz + dz;
            if (cx < 0 || cz < 0 || cx >= gridW || cz >= gridH) continue;
            for (int i : grid[size_t(cz * gridW + cx)]) {
                const Slice &sl = i >= 0 ? S[size_t(i)] : CS[size_t(-1 - i)];
                const float ex = sl.x - x, ez = -sl.z - z;
                best = std::min(best, ex * ex + ez * ez);
            }
        }
    return std::sqrt(best);
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
    raceSeed = seed;
    cars.clear();

    // Online rooms and every vs CPU race (like an online room) race side by side:
    // a 5-lane road instead of 3, a wider grid, simpler obstacles and less traffic
    const bool pack = (mode == ModeOnline) || (mode == ModeSingle && racers >= 2);
    lanes = pack ? ONLINE_LANES : 3;
    halfWidth = ROAD_HALF + float(lanes - 3) * 0.5f * LANE_WIDTH;
    const float L = lapLen;

    // Starting grid: behind the start/finish line, two columns (four for a pack), rows 9 m
    // apart. Slots 0 and 1 are the original P1 (red, right) and P2 (blue, left).
    static const float GRID_COL[MAX_RACERS][3] = {
        {0.92f, 0.14f, 0.16f}, {0.15f, 0.42f, 0.98f}, {1.00f, 0.75f, 0.10f}, {0.20f, 0.85f, 0.35f},
        {0.85f, 0.30f, 0.95f}, {0.10f, 0.85f, 0.90f}};
    racers = std::clamp(racers, 1, MAX_RACERS);
    for (int i = 0; i < racers; ++i) {
        Car c;
        float back;
        if (pack) {
            static const float COL_D[4] = {2.5f, -2.5f, 7.5f, -7.5f};
            c.d = COL_D[i % 4];
            back = 10.0f + float(i / 4) * 9.0f;
        } else {
            c.d = (i % 2 == 0) ? 3.0f : -3.0f;
            back = 10.0f + float(i / 2) * 9.0f;
        }
        c.s = sprint() ? startLine() - back : wrapS(-back);
        c.lap = sprint() ? 0 : -1;                       // (a sprint has no laps)
        c.progress = -back;
        c.aiCheckProg = c.progress;
        c.hint = int(c.s);
        pointAt(c.s, c.d, c.x, c.z);
        c.y = heightAt(c.s, c.d);
        c.vy = 0.0f;
        c.air = 0;
        c.heading = headingAt(c.s);
        c.aiTarget = c.d;
        c.r = GRID_COL[i][0]; c.g = GRID_COL[i][1]; c.b = GRID_COL[i][2];
        cars.push_back(c);
    }

    tick = 0;
    raceTimer = 0.0f;
    winner = 0;
    topDoneTick = -1;
    for (auto &row : pairCool) for (int &v : row) v = 0;
    traffic.clear();
    obstacles.clear();
    props.clear();
    nitroPickups.clear();
    events.clear();

    std::mt19937 gen(seed);
    auto R = [&gen](float a, float b) { return uniformFloat(gen, a, b); };
    const float roadRot = 180.0f / PI;                           // heading (radians) -> rotation (degrees)

    // On a sprint the race runs from the start line to the finish line; on a circuit round
    // the whole lap from s = 0
    const float s0 = startLine(), sEnd = sprint() ? finishLine() + 220.0f : L;

    // ---- Collectible Nitro Pickups ----
    // Strategic nitro canisters around the lap, about every 400 m
    for (float d = s0 + 360.0f; d < sEnd - 220.0f; d += R(330.0f, 450.0f)) {
        if (nearRamp(d, 150.0f, 150.0f) || nearJoin(d)) continue;   // the ramps, fork and merge stay clear
        NitroPickup np{};
        int lane = int(R(0, float(lanes))) % lanes;
        np.s = d;
        np.d = laneX(lane);
        pointAt(np.s, np.d, np.x, np.z);
        np.y = 0.5f;
        np.active = true;
        nitroPickups.push_back(np);
    }

    int fromLap = 0;                                     // obstacles from this lap on (see below)
    auto addRound = [&](Obstacle::Type t, float d, float off) {
        Obstacle o{}; o.type = t; o.s = wrapS(d); o.d = off; o.fromLap = fromLap;
        pointAt(o.s, o.d, o.x, o.z);
        obstacles.push_back(o);
    };
    auto addBarrier = [&](float d, float off, Obstacle::Type t = Obstacle::Barrier) {
        Obstacle o{}; o.type = t; o.s = wrapS(d); o.d = off; o.fromLap = fromLap;
        pointAt(o.s, o.d, o.x, o.z);
        o.hx = t == Obstacle::Concrete ? 1.9f : 2.2f; o.hz = t == Obstacle::Concrete ? 0.8f : 0.6f;
        o.rot = -headingAt(o.s) * roadRot;                       // lies across the road
        obstacles.push_back(o);
    };

    const MapDef &map = MAPS[mapIndex];
    // more obstacles towards the finish: 40% closer together in the last third of the road,
    // and on a circuit's last lap a second set there (fromLap)
    const float third = lastThird();
    auto closer = [&](float d) { return d >= third ? 0.6f : 1.0f; };

    // ---- Obstacle patterns ----
    // The whole pattern (from its start d to d + len) stays clear of the ramps (and the side
    // ramps, placed first), the fork and the merge.
    placeSideRamps(seed);
    auto nearSide = [&](float d, float len) {
        for (const SideRamp &r : sideList) {
            const float a = aheadOf(r.s, d);              // > 0: past the lip
            if (a + len > -r.run - 45.0f && a < 60.0f) return true;
        }
        return false;
    };
    // (and of the obstacles already there: the last lap's set must never close the road
    // together with the first one)
    auto nearObstacle = [&](float d, float len) {
        for (const Obstacle &o : obstacles) {
            const float a = aheadOf(d, o.s);
            if (a > -45.0f && a < len + 45.0f) return true;
        }
        return false;
    };
    auto keepClear = [&](float d, float len, float afterRamp) {
        return nearRamp(d, 150.0f, afterRamp) || nearRamp(d + len, 150.0f, afterRamp) || nearJoin(d) || nearJoin(d + len)
            || nearSide(d, len) || nearObstacle(d, len);
    };
    // no room at d: try a little further on (up to 150 m) rather than skip the whole gap
    auto slideClear = [&](float &d, float len, float afterRamp) {
        for (float t = d; t < d + 150.0f; t += 15.0f)
            if (!keepClear(t, len, afterRamp)) { d = t; return true; }
        return false;
    };
    // A pack of cars, and every sprint: only small things that close one lane at most (no
    // barrier walls, no cone walls) and more room between them. A sprint also has short
    // blocks: one barrier across one lane.
    const bool small = pack || sprint();
    const bool extraLap = !sprint() && laps > 1;
    for (int pass = 0; pass < (extraLap ? 2 : 1); ++pass) {
    fromLap = pass ? laps - 1 : 0;
    for (float d = pass ? third + 60.0f : s0 + 300.0f; small && d < sEnd - 220.0f;
         d += R(map.obstacleGapMin, map.obstacleGapMax) * 1.6f * OBSTACLE_GAP * closer(d)) {
        if (!slideClear(d, 14.0f, 150.0f)) continue;
        float x = laneX(int(R(0.0f, float(lanes))) % lanes);
        const float r = R(0.0f, 1.0f);
        if (r < 0.36f) {                                 // three cones in a row along one lane
            for (int i = 0; i < 3; ++i) addRound(Obstacle::Cone, d + i * 7.0f, x);
        } else if (r < 0.58f) {                          // two barrels side by side
            addRound(Obstacle::Barrel, d, x - 1.0f);
            addRound(Obstacle::Barrel, d, x + 1.0f);
        } else if (r < 0.80f) {                          // two tyre stacks side by side
            addRound(Obstacle::Tyres, d, x - 1.0f);
            addRound(Obstacle::Tyres, d, x + 1.0f);
        } else if (r < 0.92f) {                          // a concrete block: one lane closed
            addBarrier(d, x, Obstacle::Concrete);
        } else {                                         // a short block: one lane closed
            addBarrier(d, x);
        }
    }
    for (float d = pass ? third + 60.0f : s0 + 240.0f; !small && d < sEnd - 220.0f;
         d += R(map.obstacleGapMin, map.obstacleGapMax) * OBSTACLE_GAP * closer(d)) {
        if (!slideClear(d, 55.0f, 200.0f)) continue;  // (the longest pattern: the cone slalom)
        int pattern = int(R(0.0f, 6.0f)) % 6;
        if (pattern == 4) {                              // a road block: concrete across all lanes but one
            const int open = int(R(0, float(lanes))) % lanes;
            for (int l = 0; l < lanes; ++l) if (l != open) addBarrier(d + 12.0f, laneX(l), Obstacle::Concrete);
            for (int l = 0; l < lanes; ++l) if (l != open) addRound(Obstacle::Cone, d, laneX(l));   // (a warning first)
            continue;
        }
        if (pattern == 5) {                              // a tyre-stack chicane
            for (int i = 0; i < 4; ++i) addRound(Obstacle::Tyres, d + i * 14.0f, (i % 2) ? 3.2f : -3.2f);
            continue;
        }
        if (pattern == 0) { // cone slalom
            for (int i = 0; i < 6; ++i) addRound(Obstacle::Cone, d + i * 11.0f, (i % 2) ? 3.5f : -3.5f);
        } else if (pattern == 1) { // zig-zag barrier walls
            int open1 = int(R(0, 3)) % 3;
            int open2 = (open1 + 1 + int(R(0, 2)) % 2) % 3;
            for (int l = 0; l < 3; ++l) if (l != open1) addBarrier(d, laneX(l));
            for (int l = 0; l < 3; ++l) if (l != open2) addBarrier(d + 38.0f, laneX(l));
        } else if (pattern == 2) { // barrels
            for (int i = 0; i < 3; ++i) {               // (one draw at a time: in a call the compilers order them differently)
                const float off = R(-5.5f, 5.5f), along = R(0, 18);
                addRound(Obstacle::Barrel, d + along, off);
            }
        } else { // cone wall with one gap
            int gap = int(R(0, 3)) % 3;
            for (float x = -6.0f; x <= 6.01f; x += 1.5f)
                if (std::fabs(x - laneX(gap)) > 2.4f) addRound(Obstacle::Cone, d, x);
        }
    }
    }
    fromLap = 0;
    std::mt19937 rx(uint32_t(seed) ^ 0x5DEECE66u);
    auto Rx = [&](float a, float b) { return a + (b - a) * float(rx() % 100000u) / 100000.0f; };
    // The death traps: a mine in one lane under a red skull you can see from far away. 0 health at
    // once - drive around it or jump over it. A few on each map, one more in the last third;
    // never soon after the start or a bend, or near anything else.
    {
        auto addMine = [&](float from) {
            for (float t = from; t < from + 400.0f && t < sEnd - 300.0f; t += 15.0f) {
                if (keepClear(t, 10.0f, 220.0f) || std::fabs(wrapAngle(headingAt(t) - headingAt(t - 90.0f))) > 0.55f) continue;
                addRound(Obstacle::Mine, t, laneX(int(rx() % uint32_t(lanes))));
                obstacles.back().rot = -headingAt(obstacles.back().s) * roadRot;
                return;
            }
        };
        const int mines = std::clamp(int((sEnd - s0) / 1500.0f) + 2, 2, 4);
        for (int i = 0; i < mines; ++i)
            addMine(s0 + 400.0f + (sEnd - s0 - 800.0f) * (float(i) + 0.3f) / float(mines) + Rx(0.0f, 80.0f));
        if (sEnd - third > 500.0f) addMine(third + Rx(80.0f, sEnd - third - 400.0f));
    }
    // Stones and more road blocks, on every map: always a lane open, clear of the ramps, the fork
    // and the merge (keepClear), closer together in the last third. A pack of cars or a sprint:
    // one lane closed at most, like its other obstacles.
    int extraBlocks = 0;
    for (float d = s0 + 260.0f; d < sEnd - 220.0f; d += Rx(150.0f, 230.0f) * closer(d)) {
        if (!slideClear(d, 30.0f, 180.0f)) continue;
        const int open = int(rx() % uint32_t(lanes));
        const bool rocks = (extraBlocks++ % 5) < 3;          // (three of five: every map gets its stones)
        for (int l = 0; l < lanes; ++l) {
            if (l == open || (small && l != (open + 1) % lanes)) continue;
            if (rocks) {                                 // a rockfall: a few stones across the lane
                for (int i = 0; i < 2; ++i) {           // (one draw at a time, as above)
                    const float off = Rx(-1.2f, 1.2f), along = Rx(0.0f, 5.0f);
                    addRound(Obstacle::Stone, d + float(i) * 9.0f + along, laneX(l) + off);
                }
            } else {                                     // a road block, with cones as the warning
                addRound(Obstacle::Cone, d, laneX(l));
                addBarrier(d + 12.0f, laneX(l), Obstacle::Concrete);
            }
        }
    }
    leadLap = 0;
    obsList.assign(cars.size(), RaceObs());
    rolesSet = false;
    surpriseUsed.fill(false);
    tauntTick.fill(-1000000);
    tauntBy.fill(-1);
    sideJumps.assign(sideList.size(), 0);
    mainJumps.assign(rampList.size(), 0);
    for (auto &row : pairRel) for (float &v : row) v = 1e9f;   // (unknown until the first tick)
    referenceSectors(mapIndex);

    // ---- Scenery (depends on the map's theme) ----
    // A prop is only kept when no road comes closer than its own clearance (circuits pass
    // near themselves), and never in the sea.
    auto keep = [&](float x, float z, float clearance) {
        const float tx = x - landX, tz = z - landZ;
        if (x >= seaX - 10.0f || tx * tx + tz * tz <= 75.0f * 75.0f) return false;
        if (roadDistance(x, z, halfWidth + clearance) < halfWidth + clearance) return false;
        const int k = sliceNear(x, z, 220.0f);         // not in the valley under a bridge
        return k < 0 || !(track.slices()[size_t(k)].flags & TF_BRIDGE);
    };
    // side: -1 left, +1 right; off: metres beyond the guard rail; facing: rotation relative
    // to the road (e.g. houses turn their front to it)
    auto addProp = [&](Prop::Type t, float d, float side, float off, float scale, int palette, float rot,
                       float radius, bool faceRoad = false) {
        Prop p{}; p.type = t; p.scale = scale; p.palette = palette;
        pointAt(d, side * (railOffset() + off), p.x, p.z);
        p.rot = faceRoad ? -headingAt(d) * roadRot + rot : rot;
        if (keep(p.x, p.z, 4.0f + radius)) props.push_back(p);
    };
    auto addLamps = [&](float spacing, bool bothSides) {
        int alt = 0;
        for (float d = 30.0f; d < L - 220.0f; d += spacing, ++alt) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                if (!bothSides && side != ((alt % 2) ? 1.0f : -1.0f)) continue;
                Prop p{}; p.type = Prop::Lamp; p.scale = 1.0f;
                pointAt(d, side * (halfWidth + 2.2f), p.x, p.z);
                p.rot = -headingAt(d) * roadRot + ((side < 0) ? 90.0f : -90.0f);
                if (keep(p.x, p.z, 1.8f)) props.push_back(p);
            }
        }
    };

    switch (map.theme) {
    case MapTheme::Countryside: {
        // houses + forest
        float lastHouse[2] = {-100.0f, -100.0f};
        for (float d = 20.0f; d < L; d += R(12.0f, 20.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                if (R(0, 1) < 0.85f) {
                    const float dd = d + R(-3, 3), scale = R(0.85f, 1.2f);
                    const int palette = int(R(0, 4)) % 4;
                    float r = R(0, 1);
                    if (r < 0.4f && d - lastHouse[s] > 16.0f && d < L - 220.0f) {
                        lastHouse[s] = d;
                        addProp(Prop::House, dd, side, 8.0f + R(0, 6), scale, palette, (side < 0) ? 90.0f : -90.0f, 8.0f, true);
                    } else {
                        float off = (d - lastHouse[s] < 10.0f) ? R(20, 30) : R(3, 25);
                        addProp((r < 0.72f) ? Prop::Pine : Prop::RoundTree, dd, side, off, scale, palette, R(0, 360), 3.0f);
                    }
                }
                // background forest row
                const Prop::Type bt = (R(0, 1) < 0.6f) ? Prop::Pine : Prop::RoundTree;
                const float bd = d + R(-6, 6), boff = 32.0f + R(0, 45);
                const float bscale = R(1.0f, 1.6f), brot = R(0, 360);
                addProp(bt, bd, side, boff, bscale, 0, brot, 4.0f);
            }
        }
        addLamps(50.0f, false);
        break;
    }
    case MapTheme::Desert:
        // cacti and boulders by the road, huge red mesas further out
        for (float d = 20.0f; d < L; d += R(14.0f, 24.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                if (R(0, 1) < 0.75f) {
                    bool cactus = R(0, 1) < 0.6f;
                    const float dd = d + R(-4, 4), off = R(3, 24);
                    const float scale = cactus ? R(0.8f, 1.4f) : R(0.6f, 1.5f);
                    const int palette = int(R(0, 3)) % 3;
                    addProp(cactus ? Prop::Cactus : Prop::Rock, dd, side, off, scale, palette, R(0, 360), 2.0f * scale);
                }
                if (R(0, 1) < 0.45f) {                   // mesa: radius is 6 m x scale, keep it off the road
                    float sc = R(4.0f, 9.0f);
                    const float dd = d + R(-8, 8), off = 6.0f * sc + R(6, 50);
                    addProp(Prop::Rock, dd, side, off, sc, 0, R(0, 360), 6.0f * sc);
                }
            }
        }
        break;
    case MapTheme::Snow:
        // dense snowy forest with the odd mountain chalet and rocks poking out of the snow
        for (float d = 20.0f; d < L; d += R(9.0f, 15.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                float r = R(0, 1);
                if (r < 0.08f && d < L - 220.0f) {
                    const float off = R(9, 14), scale = R(0.9f, 1.1f);
                    addProp(Prop::House, d, side, off, scale, 4, (side < 0) ? 90.0f : -90.0f, 8.0f, true);
                } else if (r < 0.20f) {
                    const float dd = d + R(-3, 3), off = R(3, 18), scale = R(0.7f, 1.6f);
                    addProp(Prop::Rock, dd, side, off, scale, 3, R(0, 360), 2.0f * scale);
                } else if (r < 0.92f) {
                    const float dd = d + R(-3, 3), off = R(3, 22), scale = R(0.9f, 1.4f);
                    addProp(Prop::SnowPine, dd, side, off, scale, 0, R(0, 360), 3.0f);
                }
                const float dd = d + R(-6, 6), off = R(26, 70), scale = R(1.1f, 1.8f);
                addProp(Prop::SnowPine, dd, side, off, scale, 0, R(0, 360), 4.0f);
            }
        }
        break;
    case MapTheme::City:
        // two rows of towers lining the street, street lights on both sides
        for (int s = 0; s < 2; ++s) {
            float side = (s == 0) ? -1.0f : 1.0f;
            for (float d = 15.0f; d < L; d += R(17.0f, 23.0f)) {
                const float off = R(7, 10), scale = R(0.7f, 2.4f);
                const int palette = int(R(0, 5)) % 5;
                addProp(Prop::Building, d, side, off, scale, palette, 0.0f, 7.0f, true);
            }
            for (float d = 25.0f; d < L; d += R(20.0f, 30.0f)) {
                const float off = R(32, 55), scale = R(1.8f, 4.5f);
                const int palette = int(R(0, 5)) % 5;
                addProp(Prop::Building, d, side, off, scale, palette, 0.0f, 12.0f, true);
            }
        }
        addLamps(28.0f, true);
        break;
    case MapTheme::Coast: {
        // palms, beach houses and trees along the road; lighthouses on the beach (east of
        // the circuit)
        for (float d = 20.0f; d < L; d += R(13.0f, 22.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                float r = R(0, 1);
                if (r < 0.15f && d < L - 220.0f) {
                    const float off = R(8, 13), scale = R(0.85f, 1.05f);
                    const int palette = int(R(0, 4)) % 4;
                    addProp(Prop::House, d, side, off, scale, palette, (side < 0) ? 90.0f : -90.0f, 8.0f, true);
                } else if (r < 0.70f) {
                    const float dd = d + R(-4, 4), off = R(2, 20), scale = R(0.85f, 1.25f);
                    addProp(Prop::Palm, dd, side, off, scale, 0, R(0, 360), 2.0f);
                } else {
                    const float dd = d + R(-4, 4), off = R(4, 26), scale = R(0.9f, 1.3f);
                    addProp(Prop::RoundTree, dd, side, off, scale, 0, R(0, 360), 3.0f);
                }
            }
        }
        int t0 = -1, t1 = -1;                            // the tunnel under the headland
        for (int i = 0; i < int(L); ++i)
            if (track.slices()[size_t(i)].flags & TF_TUNNEL) { if (t0 < 0) t0 = i; t1 = i; }
        if (t0 >= 0) {
            const float mid = 0.5f * float(t0 + t1);
            float ax, az, bx, bz;
            pointAt(mid, 40.0f, ax, az);
            pointAt(mid, -40.0f, bx, bz);
            Prop p{}; p.type = Prop::Lighthouse; p.scale = 1.0f;
            pointAt(mid, (ax > bx ? 1.0f : -1.0f) * (railOffset() + 30.0f), p.x, p.z);   // on the sea side
            props.push_back(p);
        }
        {
            float x, z;
            pointAt(L * 0.7f, 0.0f, x, z);
            Prop p{}; p.type = Prop::Lighthouse; p.scale = 1.0f;
            p.x = seaX - 22.0f; p.z = z;
            props.push_back(p);
        }
        addLamps(60.0f, false);
        break;
    }
    case MapTheme::Jungle:
        // thick rainforest right up to the road: broad trees, coconut palms, mossy rocks, the odd
        // roadside house; tall trees behind
        for (float d = 20.0f; d < L; d += R(8.0f, 13.0f)) {
            for (int s = 0; s < 2; ++s) {
                float side = (s == 0) ? -1.0f : 1.0f;
                float r = R(0, 1);
                if (r < 0.05f && d < L - 220.0f) {
                    const float off = R(9, 13), scale = R(0.8f, 1.0f);
                    addProp(Prop::House, d, side, off, scale, int(R(0, 4)) % 4, (side < 0) ? 90.0f : -90.0f, 8.0f, true);
                } else if (r < 0.55f) {
                    const float dd = d + R(-3, 3), off = R(2.5f, 16), scale = R(1.0f, 1.6f);
                    addProp(Prop::RoundTree, dd, side, off, scale, 0, R(0, 360), 3.0f);
                } else if (r < 0.82f) {
                    const float dd = d + R(-3, 3), off = R(2.5f, 18), scale = R(0.9f, 1.4f);
                    addProp(Prop::Palm, dd, side, off, scale, 0, R(0, 360), 2.0f);
                } else if (r < 0.92f) {
                    const float dd = d + R(-3, 3), off = R(3, 14), scale = R(0.6f, 1.3f);
                    addProp(Prop::Rock, dd, side, off, scale, 3, R(0, 360), 2.0f * scale);
                }
                const float dd = d + R(-5, 5), off = R(20, 60), scale = R(1.4f, 2.2f);
                addProp(R(0, 1) < 0.7f ? Prop::RoundTree : Prop::Pine, dd, side, off, scale, 0, R(0, 360), 4.0f);
            }
        }
        break;
    }

    // Torches lining the run to the start/finish line (a sprint: to the start line)
    for (float d = std::max(0.0f, (sprint() ? s0 : L) - 220.0f); d < (sprint() ? s0 : L) + 30.0f; d += 20.0f) {
        for (int s = 0; s < 2; ++s) {
            float side = (s == 0) ? -1.0f : 1.0f;
            Prop p{}; p.type = Prop::Torch; p.scale = 1.0f; p.palette = s; p.rot = 0;
            pointAt(d, side * (halfWidth + 2.0f), p.x, p.z);
            if (keep(p.x, p.z, 1.6f)) props.push_back(p);
        }
    }

    // ---- Traffic: spread round the lap, ahead of the grid ----
    const float pal[6][3] = {{0.95f, 0.95f, 0.95f}, {0.65f, 0.67f, 0.70f}, {0.20f, 0.65f, 0.30f},
                             {1.00f, 0.55f, 0.10f}, {0.55f, 0.30f, 0.75f}, {0.98f, 0.85f, 0.15f}};
    // a sprint: one car or truck every 200 m, cars faster than trucks, each at its own
    // steady speed (a pack too: the road is a highway full of traffic)
    const float raceLen = sprint() ? finishLine() - s0 : L;
    const int trafficCount = sprint() ? int(raceLen / 200.0f) : pack ? map.trafficCount / 2 : map.trafficCount;
    const float trafficGap = (raceLen - 400.0f) / float(trafficCount);
    for (int i = 0; i < trafficCount; ++i) {
        Traffic t{};
        t.s = s0 + 180.0f + i * trafficGap + R(0, trafficGap * 0.35f);
        t.lane = t.targetLane = laneX(int(R(0, float(lanes))) % lanes);
        t.speed = R(0.35f, 0.62f);
        int k = int(R(0, 6)) % 6;
        t.r = pal[k][0]; t.g = pal[k][1]; t.b = pal[k][2];
        t.truck = R(0, 1) < 0.25f;
        if (sprint()) t.speed = t.truck ? 0.35f + 0.10f * (t.speed - 0.35f) / 0.27f : 0.48f + 0.14f * (t.speed - 0.35f) / 0.27f;
        traffic.push_back(t);
    }
}

void GameSim::step(const PlayerInput inputs[], int inputCount, const int runs[], const PlayerInput extra[])
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
    updateLeadLap();
    if (state == Racing && tick % 60 == 0) directorStep();   // the race director, once a second
    if (state == Racing && tick == 270)                  // 1.5 s in: each person's rival says hello
        for (int j = 0; j < int(cars.size()); ++j)
            if (cars[size_t(j)].dirRole == 1 && cars[size_t(j)].dirRivalOf >= 0) taunt(j, TauntStart, cars[size_t(j)].dirRivalOf);

    // Update cars: in vs CPU every car after yours is a CPU, every other car follows its input
    updateShortcutGates();                               // (the leaders: the shortcut is closed to them)
    for (size_t i = 0; i < cars.size(); ++i) {
        if (mode == ModeSingle && i >= 1) {
            updateAI(cars[i], int(i));
        } else {
            PlayerInput in = (int(i) < inputCount) ? inputs[i] : PlayerInput{};
            const int r = (runs && int(i) < inputCount) ? runs[i] : 1;
            if (r >= 1) updateCar(cars[i], in, int(i));
            if (r >= 2 && extra) updateCar(cars[i], extra[i], int(i));
        }
    }

    // Traffic drives round the loop (a sprint: on along the road; off the far end it comes
    // back 400 m behind the last racer, out of sight)
    float lastCar = lapLen;
    for (const Car &c : cars) lastCar = std::min(lastCar, c.s);
    auto laneFree = [&](const Traffic &me, float lane) {
        for (const Traffic &o : traffic)
            if (&o != &me && std::fabs(o.targetLane - lane) < 1.0f && std::fabs(aheadOf(me.s, o.s)) < 30.0f) return false;
        return true;
    };
    for (size_t ti = 0; ti < traffic.size(); ++ti) {
        Traffic &t = traffic[ti];
        if (sprint()) {
            t.s += t.speed;
            if (t.s > finishLine() + 200.0f) { t.s = std::max(20.0f, lastCar - 400.0f); t.lane = t.targetLane; }
            // catching slower traffic in its lane: pass it in a clear lane next door, or wait
            // behind it; otherwise now and then (about every 30 s) move over when it is clear
            const Traffic *slow = nullptr;
            for (const Traffic &o : traffic) {
                const float ahead = o.s - t.s;
                if (&o != &t && std::fabs(o.lane - t.targetLane) < 1.0f && ahead > 0.0f && ahead < 25.0f && o.speed < t.speed)
                    slow = &o;
            }
            const uint32_t h = (uint32_t(tick) * 2654435761u) ^ (uint32_t(ti + 1) * 40503u);
            const bool wander = (h % 1800u) == 0u;
            if ((slow || wander) && std::fabs(t.lane - t.targetLane) < 0.2f) {
                const int cur = int(std::lround((t.targetLane / LANE_WIDTH) + 0.5f * float(lanes - 1)));
                const int side = (h >> 11) & 1u ? 1 : -1;
                bool moved = false;
                for (int k : {cur + side, cur - side})
                    if (k >= 0 && k < lanes && laneFree(t, laneX(k))) { t.targetLane = laneX(k); moved = true; break; }
                if (!moved && slow) t.s = std::min(t.s, slow->s - 12.0f);   // nowhere to go: stay behind it
            }
        } else {
            t.s = wrapS(t.s + t.speed);
        }
        for (const Obstacle &o : obstacles) {             // change lane before a barrier wall
            if (!o.block()) continue;
            const float ahead = aheadOf(t.s, o.s);
            if (ahead < 0.0f || ahead > 25.0f) continue;
            if (std::fabs(t.targetLane - o.d) < o.hx + 1.2f) {
                for (int l = 0; l < lanes; ++l) {
                    bool blocked = false;
                    for (const Obstacle &o2 : obstacles)
                        if (o2.block() && std::fabs(aheadOf(o.s, o2.s)) < 1.0f &&
                            std::fabs(laneX(l) - o2.d) < o2.hx + 1.2f) { blocked = true; break; }
                    if (!blocked) { t.targetLane = laneX(l); break; }
                }
            }
        }
        t.lane += (t.targetLane - t.lane) * 0.06f;

        float tx, tz;
        pointAt(t.s, t.lane, tx, tz);
        const float ta = headingAt(t.s);
        for (Obstacle &o : obstacles)
            if (present(o.fromLap) && std::fabs(aheadOf(t.s, o.s)) < 6.0f) knock(o, tx, tz, std::sin(ta), -std::cos(ta), t.speed, -1);
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

    // Every lap has something to dodge and to collect: knocked-over cones and barrels are
    // put back after 20 s and picked-up nitro cans come back after 15 s - only while no car
    // is near, so nothing appears right in front of a driver
    auto carNear = [&](float x, float z) {
        for (const Car &c : cars) {
            const float dx = c.x - x, dz = c.z - z;
            if (dx * dx + dz * dz < 60.0f * 60.0f) return true;
        }
        return false;
    };
    for (Obstacle &o : obstacles) {
        if (!o.knocked || ++o.downTicks < 20 * 60) continue;
        float hx, hz;
        pointAt(o.s, o.d, hx, hz);
        if (carNear(hx, hz) || carNear(o.x, o.z)) continue;
        o.knocked = false;
        o.downTicks = 0;
        o.x = hx; o.y = 0.0f; o.z = hz;
        o.vx = o.vy = o.vz = 0.0f;
        o.rot = o.spin = 0.0f;
    }
    for (NitroPickup &np : nitroPickups) {
        if (np.active || ++np.downTicks < NITRO_BACK) continue;   // back in the same place 1 s later
        np.active = true;
        np.downTicks = 0;
    }

    // Collisions
    for (auto &row : pairCool) for (int &v : row) if (v > 0) --v;
    for (size_t i = 0; i < cars.size(); ++i) {
        collideWorld(cars[i], int(i));
    }
    for (size_t i = 0; i < cars.size(); ++i)
        for (size_t j = i + 1; j < cars.size(); ++j)
            collideCars(cars[i], cars[j], int(i), int(j));

    if (state == Racing) observeStep();

    // Finish line
    if (state == Racing) {
        for (size_t i = 0; i < cars.size(); ++i) {
            if (!cars[i].finished && cars[i].progress >= raceDistance()) {
                cars[i].finished = true;
                cars[i].finishTime = raceTimer;
                // a named driver wins: it tells every person; a person beats their rival home
                if (!winner && !isPerson(int(i)))
                    for (int j = 0; j < int(cars.size()); ++j) if (isPerson(j)) taunt(int(i), TauntWin, j);
                if (isPerson(int(i)))
                    for (int j = 0; j < int(cars.size()); ++j)
                        if (cars[size_t(j)].dirRole == 1 && cars[size_t(j)].dirRivalOf == int(i) && !cars[size_t(j)].finished)
                            taunt(j, TauntLost, int(i));
                if (!winner) {
                    winner = int(i + 1);
                    SimEvent ev;
                    ev.type = SimEvent::Fanfare;
                    events.push_back(ev);
                }
            }
        }
        // The race is over once the top 4 are home (every car, with fewer than 4), two seconds
        // after the 4th crossed the line - whoever is still out on the road doesn't finish
        int done = 0;
        for (const auto &c : cars) if (c.finished) ++done;
        if (done >= std::min(FINISHERS, int(cars.size()))) {
            if (topDoneTick < 0) topDoneTick = tick;
            if (tick - topDoneTick >= FINISH_WAIT) state = Finished;
        }
    }
}

// The player model's measurements that need every car: the tenths of the lap and their
// times, the L boost, passes (close ones, and on which side), tailgating, blocking, and once
// a second the place (mood)
void GameSim::observeStep()
{
    const int n = int(std::min(cars.size(), obsList.size()));
    const float secLen = (sprint() ? raceDistance() : lapLen) / float(PM_SECTORS);
    const auto &ref = referenceSectors(mapIndex);
    for (int i = 0; i < n; ++i) {
        const Car &c = cars[size_t(i)];
        RaceObs &o = obsList[size_t(i)];
        moodDecay(o);
        if (c.finished || c.out) continue;
        // the L boost: how long each press is held, and where on the lap it starts
        if (c.boosting) {
            if (o.boostHoldNow == 0) {
                ++o.boosts;
                const float f = sprint() ? std::clamp(c.progress / raceDistance(), 0.0f, 0.999f) : wrapS(c.s) / lapLen;
                ++o.boostBins[size_t(std::clamp(int(f * PM_BOOST_BINS), 0, PM_BOOST_BINS - 1))];
            }
            ++o.boostHoldNow;
            ++o.boostTicks;
        } else if (o.boostHoldNow > 0) {
            o.boostLongest = std::max(o.boostLongest, o.boostHoldNow);
            if (o.boostHoldNow >= BOOST_MID) ++o.boostOver3;
            if (o.boostHoldNow >= BOOST_HIGH) ++o.boostOver10;
            o.boostHoldNow = 0;
        }
        // the tenths of the lap: each finished one against the reference
        if (c.wreck > 0) o.secSpoiled = true;
        if (c.progress >= 0.0f) {
            const int g = int(c.progress / secLen);
            if (o.secGlobal < 0 || g < o.secGlobal || g > o.secGlobal + 1) {
                o.secGlobal = g; o.secStartTick = tick; o.secSpoiled = false;
            } else if (g == o.secGlobal + 1) {
                const int k = o.secGlobal % PM_SECTORS;
                const int took = tick - o.secStartTick;
                if (!o.secSpoiled && took > 0 && ref[size_t(k)] > 0.0f) {
                    o.sectorRatio.push_back(ref[size_t(k)] / float(took));
                    o.sectorIndex.push_back(k);
                }
                o.secGlobal = g; o.secStartTick = tick; o.secSpoiled = false;
            }
        }
    }
    // passes, tailgating (every tick) and blocking (every quarter second)
    const bool blockCheck = tick % 15 == 0;
    for (int i = 0; i < n; ++i) {
        const Car &a = cars[size_t(i)];
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const Car &b = cars[size_t(j)];
            const bool live = a.path == b.path && a.wreck == 0 && b.wreck == 0 && a.ghost == 0 && b.ghost == 0 && !a.out && !b.out;
            const float rel = live ? aheadOf(b.s, a.s) : 1e9f;   // > 0: a is ahead of b
            const float prev = pairRel[i][j];
            pairRel[i][j] = rel;
            if (!live || prev > 1e8f || a.finished) continue;
            RaceObs &o = obsList[size_t(i)];
            const float lat = a.d - b.d;
            if (prev < 0.0f && rel >= 0.0f && rel < 10.0f && std::fabs(lat) < 8.0f) {   // a has just passed b
                if (std::fabs(lat) < 3.4f) ++o.closePasses;
                if (lat > 0.0f) ++o.passRight; else ++o.passLeft;
                if (!isPerson(i) && isPerson(j)) taunt(i, TauntPassedYou, j);
                else if (isPerson(i) && !isPerson(j)) taunt(j, TauntYouPassed, i);
            }
            if (rel > -10.0f && rel < -1.0f && std::fabs(lat) < 2.0f) ++o.tailgateTicks;
            if (blockCheck && rel > 3.0f && rel < 20.0f) {
                const float before = std::fabs(dPrev[i] - dPrev[j]), now = std::fabs(lat);
                if (before > 2.2f && now < 1.4f && std::fabs(a.d - dPrev[i]) > std::fabs(b.d - dPrev[j])) ++o.blocks;
            }
        }
    }
    if (blockCheck) for (int i = 0; i < n; ++i) dPrev[i] = cars[size_t(i)].d;
    // once a second: place and gaps (mood)
    if (tick % 60 == 0) {
        for (int i = 0; i < n; ++i) {
            const int rank = rankOf(i);
            float ahead = 0.0f, behind = 1e9f;
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                const int rj = rankOf(j);
                if (rj == rank - 1) ahead = cars[size_t(j)].progress - cars[size_t(i)].progress;
                if (rj == rank + 1) behind = cars[size_t(i)].progress - cars[size_t(j)].progress;
            }
            moodSecond(obsList[size_t(i)], rank, n, std::max(0.0f, ahead), behind > 1e8f ? 0.0f : std::max(0.0f, behind));
        }
    }
}

const std::array<float, PM_SECTORS> &GameSim::referenceSectors(int map)
{
    static std::array<std::array<float, PM_SECTORS>, NUM_MAPS> cache{};
    static bool done[NUM_MAPS] = {};
    static bool busy = false;
    const int m = (map % NUM_MAPS + NUM_MAPS) % NUM_MAPS;
    if (done[m] || busy) return cache[size_t(m)];   // (busy: the reference race itself is being run)
    busy = true;
    GameSim g;
    g.setMap(m);
    g.setLaps(1);
    g.startRace(ModeSingle, 1);
    g.state = Racing;
    g.traffic.clear();
    g.obstacles.clear();
    g.nitroPickups.clear();
    g.sideList.clear();
    std::fill(g.sideAt.begin(), g.sideAt.end(), int16_t(0));
    Car &c = g.cars[0];
    const float oneLap = g.sprint() ? g.raceDistance() : g.lapLength();   // (one lap, however long the race)
    const float secLen = oneLap / float(PM_SECTORS);
    int prev = -1, start = 0;
    bool lapDone = false;
    for (int t = 0; t < 60 * 400 && !lapDone; ++t) {
        PlayerInput in = g.computeAIInput(c, 0, false);
        in.boost = false;
        g.step(&in, 1);
        g.clearEvents();
        const int sec = c.progress >= 0.0f && c.progress < oneLap ? std::min(PM_SECTORS - 1, int(c.progress / secLen)) : -1;
        if (sec != prev) {
            if (prev >= 0 && sec == prev + 1) cache[size_t(m)][size_t(prev)] = float(t - start);
            prev = sec;
            start = t;
        }
        lapDone = g.sprint() ? c.finished : c.progress >= oneLap;
        if (lapDone && prev >= 0) cache[size_t(m)][size_t(prev)] = float(t - start);
    }
    float sum = 0.0f;
    int k = 0;
    for (float v : cache[size_t(m)]) if (v > 0.0f) { sum += v; ++k; }
    for (float &v : cache[size_t(m)]) if (v <= 0.0f) v = k ? sum / float(k) : 600.0f;
    done[m] = true;
    busy = false;
    return cache[size_t(m)];
}

void GameSim::updateCar(Car &c, const PlayerInput &in, int carIndex)
{
    if (c.ghost > 0) --c.ghost;
    if (c.wreck > 0) { wreckStep(c, carIndex); return; }
    c.calm = std::min(c.calm + 1, 100000);
    if (c.calm > 240 && c.health < 100.0f && state == Racing)       // 4 s without a hit: mends itself, 3 a second
        c.health = std::min(100.0f, c.health + 0.05f);
    const float px = c.x, pz = c.z;                      // (a side ramp's side pushes the car back here)
    const float sPrev = c.s, progPrev = c.progress;
    const int pathPrev = c.path;

    bool up = in.up;
    bool down = in.down;
    bool left = in.left;
    bool right = in.right;
    bool nitroKey = in.nitro;

    if (state != Racing) { up = down = nitroKey = false; }
    if (sprint() && c.finished) {                        // a sprint: past the finish, brake to a stop
        up = nitroKey = false;
        down = c.speed > 0.05f;
    }
    if (state == Countdown) { left = right = false; }
    const bool flying = c.air > 0;
    const bool rollSlower = down;                       // in the air the brake slows the roll
    if (flying) up = down = false;                       // in the air: nothing to grip

    bool offroad = !flying && std::fabs(c.d) > roadHalfAt(c.s, c.path);
    const bool dirt = c.path == 1;                       // the shortcut: rough and slower

    // Hills: climbing costs top speed and pull, going down gives a little. slope = rise per
    // metre in the direction the car faces (+ = uphill), along = how much the car points
    // along the road (+1 the right way, -1 backwards)
    const auto &R = road(c.path);
    const Slice &here = R[size_t(std::clamp(int(c.s), 0, int(R.size()) - 1))];
    const float along = std::sin(c.heading) * here.tx + std::cos(c.heading) * here.tz;
    const float slope = gradeAt(c.s, c.path) * along;
    const float climb = slope * (c.speed >= 0.0f ? 1.0f : -1.0f);        // + = going uphill
    const float hill = climb > 0.0f ? 1.0f - 1.1f * climb : 1.0f - 0.8f * climb;
    const float pull = std::clamp(1.0f - 2.0f * climb, 0.7f, 1.3f);
    // speedScale is 1 for human players; the CPU uses it for difficulty and catch-up
    float topSpeed = MAX_SPEED * c.speedScale * hill * (dirt ? 0.93f : 1.0f);

    // L boost: more top speed and pull while L is held, paid for with health every tick -
    // gently at first, faster after 3 s, and after 10 s the engine burns out
    const bool wasBoosting = c.boosting;
    c.boosting = in.boost && boostAllowed() && state == Racing && !c.finished && !c.out;
    if (c.boosting) {
        ++c.boostHold;
        if (!wasBoosting) {
            SimEvent ev;
            ev.type = SimEvent::Boost;
            ev.carIndex = carIndex;
            ev.x = c.x; ev.y = c.y + 1.0f; ev.z = c.z;
            events.push_back(ev);
        }
        c.health -= BOOST_RATE[boostStage(c)] / 60.0f;
        c.calm = 0;                                      // (no mending while boosting)
        if (c.health <= 0.0f) {
            for (int k = 0; k < 2; ++k) {                // the engine blows
                SimEvent cr;
                cr.type = k ? SimEvent::Crash : SimEvent::Sparks;
                cr.x = c.x; cr.y = c.y + 0.6f; cr.z = c.z;
                cr.count = 14; cr.intensity = 1.0f;
                cr.carIndex = carIndex;
                events.push_back(cr);
            }
            SimEvent ev;
            ev.type = SimEvent::Damage;
            ev.carIndex = carIndex;
            ev.intensity = 1.0f;
            ev.count = SimEvent::ByBoost;
            ev.highPitch = true;
            ev.x = c.x; ev.y = c.y + 2.9f; ev.z = c.z;
            events.push_back(ev);
            c.boosting = false;
            healthGone(c, carIndex, SimEvent::ByBoost, false);
            return;
        }
    } else {
        c.boostHold = std::max(0, c.boostHold - 2);
    }
    const bool boosting = c.boosting;
    if (boosting) topSpeed *= 1.0f + BOOST_GAIN;

    // Trigger nitro when player/AI presses nitro button and has an available charge
    if (nitroKey && c.nitroCharges > 0 && c.nitroTimer == 0 && !offroad) {
        c.nitroCharges = 0;
        c.nitroTimer = c.nitroLen;                       // 2.0-3.0 s: how many cars took that can before
        c.nitroOn = true;
    }

    // 2-second active nitro countdown & boost
    if (c.nitroTimer > 0) {
        c.nitroOn = true;
        c.nitroTimer--;
        c.nitro = float(c.nitroTimer) / float(std::max(1, c.nitroLen)); // Fractional display for HUD meter

        // Rapid surge towards NITRO_SPEED (1.65f)
        c.speed = std::min(NITRO_SPEED, c.speed + 0.035f);

        // Exhaust nitro flame particles
        float fx = std::sin(c.heading), fz = -std::cos(c.heading);
        float rx = std::cos(c.heading), rz = std::sin(c.heading);
        for (float side : {-0.7f, 0.7f}) {
            SimEvent ev;
            ev.type = SimEvent::NitroFlame;
            ev.x = c.x - fx * 2.2f + rx * side;
            ev.y = c.y + 0.42f;
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
        if (c.speed > topSpeed) {
            c.speed = std::max(topSpeed, c.speed - 0.016f);
        }
    }

    float maxS = c.nitroOn ? NITRO_SPEED : topSpeed;
    if (offroad) maxS = OFFROAD_MAX;

    if (up) {
        if (!c.nitroOn) {
            c.speed += (0.016f + (boosting ? 0.010f : 0.0f)) * c.speedScale * pull * (1.0f - 0.5f * c.speed / topSpeed);
        }
        if (boosting && (tick + carIndex) % 2 == 0) {     // a small blue flame
            SimEvent ev;
            ev.type = SimEvent::NitroFlame;
            ev.x = c.x - std::sin(c.heading) * 2.2f;
            ev.y = c.y + 0.42f;
            ev.z = c.z + std::cos(c.heading) * 2.2f;
            ev.vx = -std::sin(c.heading); ev.vz = std::cos(c.heading);
            ev.intensity = 0.5f;
            ev.carIndex = carIndex;
            events.push_back(ev);
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
                    ev.y = c.y + 0.15f;
                    ev.z = c.z - fz * 1.5f + rz * s;
                    ev.carIndex = carIndex;
                    events.push_back(ev);
                }
            }
        } else {
            c.speed -= 0.008f;
        }
    } else {
        if (!c.nitroOn && flying) {
            c.speed *= 0.999f;                           // air drag only
        } else if (!c.nitroOn) {
            c.speed *= (state == Finished ? 0.97f : 0.99f);
            if (state == Racing) c.speed -= 0.03f * slope;   // rolling: the hill pulls a little
        }
    }

    if (c.speed > maxS) c.speed = std::max(maxS, c.speed - 0.025f);
    if (c.speed < -0.35f) c.speed = -0.35f;

    float steer = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    float grip  = std::clamp(std::fabs(c.speed) / 0.35f, 0.0f, 1.0f);
    // banked corners: turning towards the low side grips better (+20% at 8 degrees)
    const float dir = c.speed >= 0.0f ? 1.0f : -1.0f;
    const float bank = bankAt(c.s, c.path) * (along >= 0.0f ? 1.0f : -1.0f);   // + = the car's right side is lower
    const float bankGrip = steer * dir * bank > 0.0f ? 1.0f + 1.5f * std::fabs(bank) : 1.0f;
    // in the air about half of normal steering lines the car up for the landing
    // (a tap makes a small correction: full lock comes after a few ticks of holding the key)
    const float held = steer * c.steerVis > 0.0f ? std::min(1.0f, std::fabs(c.steerVis) / 0.45f) : 0.0f;
    c.heading  += steer * STEER_RATE * (STEER_TAP + (1.0f - STEER_TAP) * held) * grip * dir * (flying ? 0.55f : bankGrip) * (dirt ? 0.9f : 1.0f);
    if (flying) {
        // a flat spin keeps turning the car until steered out of; a twisted ramp's roll goes
        // on at its rate, the brake slowing it (never backwards)
        c.heading += c.spinRate;
        c.spinRate *= 0.995f;
        if (c.barrelRate != 0.0f) {
            const float sgn = c.barrelRate > 0.0f ? 1.0f : -1.0f;
            if (rollSlower) c.barrelRate = sgn * std::max(0.0f, std::fabs(c.barrelRate) - 0.0008f);
            if (c.barrelRate == 0.0f) c.barrelRate = sgn * 1e-6f;
            c.barrel += c.barrelRate;
        }
    }
    if (dirt && !flying && c.speed > 0.3f && (tick + carIndex) % 4 == 0) {
        SimEvent ev;                                     // dust behind the car
        ev.type = SimEvent::DirtDust;
        ev.x = c.x - std::sin(c.heading) * 2.0f;
        ev.y = c.y + 0.3f;
        ev.z = c.z + std::cos(c.heading) * 2.0f;
        ev.carIndex = carIndex;
        events.push_back(ev);
    }
    c.steerVis += (steer * 0.45f - c.steerVis) * 0.25f;

    bool turningHard = !flying && std::fabs(steer) > 0.45f && c.speed > 0.45f;
    c.isDrifting = turningHard || (down && turningHard);
    if (c.isDrifting) {
        c.driftAngle += (steer * 0.32f - c.driftAngle) * 0.18f;
        float fx = std::sin(c.heading), fz = -std::cos(c.heading);
        float rx = std::cos(c.heading), rz = std::sin(c.heading);
        for (float s : {-0.9f, 0.9f}) {
            SimEvent ev;
            ev.type = SimEvent::DriftSmoke;
            ev.x = c.x - fx * 1.5f + rx * s;
            ev.y = c.y + 0.12f;
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
    const float carry = flying ? JUMP_CARRY : 1.0f;      // (a jump covers more road)
    c.x += std::sin(moveAngle) * c.speed * carry;
    c.z -= std::cos(moveAngle) * c.speed * carry;

    c.topSpeed = std::max(c.topSpeed, c.speed);
    c.wheelSpin = std::fmod(c.wheelSpin + c.speed * 136.0f, 360.0f);

    // Where the car is now; a side ramp's side stops a car coming at it from beside or behind
    locate(c);
    if (c.air == 0 && !c.outside && sideLift(c.s, c.d) > c.y - heightAt(c.s, c.d, c.path) + 0.45f) {
        c.x = px; c.z = pz;
        locate(c);
        const float v0 = std::fabs(c.speed);
        damage(c, carIndex, 14.0f * v0 + 1.0f, SimEvent::ByRampSide, true, v0);
        c.speed *= 0.7f;
        SimEvent spk;
        spk.type = SimEvent::Sparks;
        spk.x = c.x; spk.y = c.y + 0.5f; spk.z = c.z;
        spk.count = 8;
        spk.carIndex = carIndex;
        events.push_back(spk);
    }
    fly(c, along, carIndex);
    if (c.wreck > 0) return;                             // (came down off the road)
    // guard rails stop the car - unless it flies over them
    const float lim = roadHalfAt(c.s, c.path) + 4.0f - 1.0f;
    if (std::fabs(c.d) > lim && !insideOtherRoad(c) && c.air > 0
        && (c.outside || c.y > heightAt(c.s, c.d > 0.0f ? lim : -lim, c.path) + 1.1f)) {
        if (!c.outside) { c.outside = true; c.outS = c.s; }
        if (std::fabs(c.d) > lim + 45.0f || std::fabs(aheadOf(c.outS, c.s)) > 250.0f) landOutside(c, carIndex);
    } else if (std::fabs(c.d) > lim && !insideOtherRoad(c)) {   // (where the shortcut leaves or joins, both are open)
        // how fast the car meets the rail: a glancing scrape costs little, head-on a lot
        const float into = std::fabs(std::sin(wrapAngle(c.heading - headingAt(c.s, c.path)))) * std::fabs(c.speed);
        if (c.calm > 20) {
            damage(c, carIndex, 30.0f * into + 0.5f, SimEvent::ByRail, true, into);
            if (RaceObs *ob = obsOf(carIndex)) { ++ob->rail; moodMistake(*ob, tick, 0.5f); }
        }
        moveOnTrack(c, 0.0f, (c.d > 0 ? lim : -lim) - c.d);
        c.speed *= 0.82f;
        c.heading += wrapAngle(headingAt(c.s, c.path) - c.heading) * 0.35f;

        SimEvent spk;
        spk.type = SimEvent::Sparks;
        spk.x = c.x; spk.y = c.y + 0.4f; spk.z = c.z;
        spk.count = 8;
        spk.carIndex = carIndex;
        events.push_back(spk);

        SimEvent cr;
        cr.type = SimEvent::Crash;
        cr.intensity = 0.8f;
        cr.carIndex = carIndex;
        events.push_back(cr);
    }

    // the player model: how far it raced, off the asphalt, side ramps it passed, the shortcut
    if (RaceObs *ob = obsOf(carIndex); ob && state == Racing && !c.finished && !c.out) {
        ++ob->ticks;
        const float moved = c.progress - progPrev;
        if (moved > 0.0f && moved < 5.0f) ob->metres += moved;
        const bool off = c.air == 0 && std::fabs(c.d) > roadHalfAt(c.s, c.path);
        if (off) ++ob->offroadTicks;
        if (off && !ob->offNow) { ++ob->offroad; moodMistake(*ob, tick, 0.4f); }
        ob->offNow = off;
        if (c.path == 0 && pathPrev == 0)
            for (const SideRamp &r : sideList)
                if (present(r.fromLap) && aheadOf(sPrev, r.s) > 0.0f && aheadOf(c.s, r.s) <= 0.0f && aheadOf(c.s, r.s) > -50.0f)
                    ++ob->rampsOffered;
        if (hasShortcut()) {
            if (pathPrev == 0 && c.path == 1) { ++ob->forkTaken; ++ob->forkOffered; }
            else if (c.path == 0 && pathPrev == 0 && !c.cutClosed) {   // (closed to it: not offered)
                const float mark = shortcutFrom() + 40.0f;            // (past the fork on the main road)
                if (aheadOf(sPrev, mark) > 0.0f && aheadOf(c.s, mark) <= 0.0f && aheadOf(c.s, mark) > -50.0f) ++ob->forkOffered;
            }
        }
    }
}

// The car follows the road while it can. When the road falls away faster than gravity can
// pull the car down (a ramp's lip, a sharp crest) it takes off and flies; it lands when it
// comes down to the road again, harder the steeper it meets the slope there.
void GameSim::fly(Car &c, float along, int carIndex)
{
    const float road = surfaceAt(c.s, c.d, c.path);
    if (c.air == 0) {
        // the road a tick behind and a tick ahead along the car's path (its shape, so a push
        // from a collision never looks like a launch)
        const float ds = c.speed * along;
        const float behind = surfaceAt(c.s - ds, c.d, c.path), ahead = surfaceAt(c.s + ds, c.d, c.path);
        c.y = road;
        c.vy = road - behind;                            // climbing or dropping with the road
        if (ahead - road < c.vy - GRAVITY - 0.005f && std::fabs(c.speed) > 0.1f) {
            c.air = 1;                                   // the road drops away faster than gravity: take off
            c.vy -= GRAVITY;
            c.barrel = c.barrelRate = c.spinRate = c.jumpDanger = 0.0f;
            if (c.path == 0 && c.vy > 0.0f) {            // off a ramp (not a crest): the jump's strength
                int *count = nullptr;
                const int ks = sideAt.empty() ? -1 : sideAt[size_t(std::clamp(int(wrapS(c.s)), 0, int(sideAt.size()) - 1))] - 1;
                if (ks >= 0 && ks < int(sideJumps.size()) && sideLift(c.s, c.d) > 0.3f) count = &sideJumps[size_t(ks)];
                for (size_t k = 0; !count && k < rampList.size() && k < mainJumps.size(); ++k) {
                    const float past = aheadOf(rampList[k].lip, c.s);
                    if (past > -3.0f && past < 6.0f) count = &mainJumps[k];
                }
                if (count) { ++*count; c.vy *= jumpMulFor(*count); }
            }
            const int k = c.path == 0 && !sideAt.empty() ? sideAt[size_t(std::clamp(int(wrapS(c.s)), 0, int(sideAt.size()) - 1))] - 1 : -1;
            if (k >= 0 && sideLift(c.s, c.d) > 0.3f) {   // off a side ramp
                const SideRamp &r = sideList[size_t(k)];
                c.jumpDanger = r.danger;
                if (RaceObs *ob = obsOf(carIndex)) {
                    ++ob->rampsTaken;
                    if (r.twist != 0.0f || r.danger > 1.0f) ++ob->bigRampsTaken;
                    if (c.nitroOn) ++ob->nitroJumps;
                }
                // a twisted ramp rolls the car towards its low side: 15% more than one whole
                // roll in the time the car will be in the air (worked out along the road
                // ahead), so the driver brakes the roll to land upright. A short hop (too slow
                // at the ramp) only tips the car - and lands it on its side unless braked.
                if (r.twist != 0.0f) {
                    const int t = airTimeLeft(c);
                    c.barrelRate = t > 40 ? -r.twist * 1.15f * 2.0f * PI / float(t) : -r.twist * 0.25f * PI / float(std::max(t, 10));
                }
                // hitting it at an angle sends the car into a flat spin the same way
                const float a = wrapAngle(c.heading - headingAt(c.s, c.path));
                if (std::fabs(a) > 0.10f) c.spinRate = a * 0.012f;
            }
        }
        return;
    }
    ++c.air;
    c.y += c.vy;
    c.vy -= GRAVITY;
    if (c.outside) {                                     // beyond the rail: the ground (or water) lies lower
        const float edge = heightAt(c.s, c.d > 0.0f ? roadHalfAt(c.s, c.path) + 3.0f : -roadHalfAt(c.s, c.path) - 3.0f, c.path);
        const float below = overWater(c) ? 5.0f : overGorge(c.outS) ? 25.0f : 0.4f;
        if (c.y <= edge - below) landOutside(c, carIndex);
        return;
    }
    if (c.y > road) return;

    // Landing: how fast the car meets the slope under it (m/s), and how the driver brought it
    // down - pointing along the road, the roll finished, on the asphalt. Off a side ramp all
    // of it counts (times the ramp's danger); a clean landing costs next to nothing.
    const float roadClimb = gradeAt(c.s, c.path) * along * c.speed;   // the road's own climb per tick
    const float impact = (roadClimb - c.vy) * 60.0f;
    const float off = std::fabs(wrapAngle(c.heading - headingAt(c.s, c.path)));
    const float rollErr = std::fabs(wrapAngle(c.barrel));
    const bool sideJump = c.jumpDanger > 0.0f, rolled = c.barrelRate != 0.0f, spun = std::fabs(c.spinRate) > 0.0005f;
    const float rollBad = std::clamp((rollErr - 0.30f) / 1.3f, 0.0f, 1.0f);
    float lose = impact > 19.0f ? 0.15f : impact > 14.0f ? 0.05f : 0.0f;      // (limits raised with the longer jumps)
    float hurt = impact > 19.0f ? 10.0f : impact > 14.0f ? 4.0f : 0.0f;
    if (sideJump) {
        // (a side ramp throws the car high: coming down from there is expected, only a very
        // hard landing - full nitro off a high ramp - costs extra)
        lose = impact > 23.0f ? 0.1f : 0.0f;
        hurt = 1.0f + 0.5f * std::max(0.0f, impact - 23.0f) + 15.0f * std::clamp((off - 0.10f) / 0.45f, 0.0f, 1.0f)
             + 25.0f * rollBad + (std::fabs(c.d) > roadHalfAt(c.s, c.path) ? 4.0f : 0.0f);
        hurt *= c.jumpDanger;
        lose += 0.3f * rollBad;
    } else {
        hurt += 20.0f * std::clamp((off - 0.20f) / 0.5f, 0.0f, 1.0f);
    }
    const bool crash = rollErr > 2.3f || (off > 1.1f && off < PI - 1.1f);   // (a roll ~130 deg short, or sideways)
    if (off > 0.44f && off < PI - 0.44f) lose += 0.15f;
    c.speed *= 1.0f - std::min(lose, 0.8f);
    if (sideJump || c.air > 45) {
        SimEvent st;
        st.type = SimEvent::Stunt;
        st.carIndex = carIndex;
        st.x = c.x; st.y = road + 2.0f; st.z = c.z;
        st.intensity = hurt;
        st.count = crash ? SimEvent::StuntCrash
                 : hurt > 12.0f ? SimEvent::StuntRough
                 : hurt <= 3.0f * std::max(1.0f, c.jumpDanger) ? (rolled ? SimEvent::StuntRoll : spun ? SimEvent::StuntSaved : SimEvent::StuntClean)
                 : SimEvent::StuntRough;
        events.push_back(st);
        if (st.count == SimEvent::StuntRoll && state == Racing) { c.nitroCharges = 1; c.nitroLen = 120; }   // a clean roll fills the nitro
        if (RaceObs *ob = obsOf(carIndex); ob && sideJump && state == Racing) {
            ++ob->landings;
            ob->landHurt += hurt * DAMAGE_SCALE;
            if (st.count == SimEvent::StuntRough) { ++ob->roughLandings; moodMistake(*ob, tick, 0.5f); }
            if (st.count == SimEvent::StuntCrash) { ++ob->crashLandings; moodMistake(*ob, tick, 1.2f); }
        }
    }
    c.barrel = c.barrelRate = c.spinRate = c.jumpDanger = 0.0f;
    if (hurt * LANDING_DAMAGE >= 0.5f) damage(c, carIndex, hurt * LANDING_DAMAGE, SimEvent::ByLanding, true, impact / 60.0f);
    if (crash && c.wreck == 0 && state == Racing) startWreck(c, carIndex, SimEvent::ByLanding, rollErr > 2.6f, false);

    SimEvent ev;
    ev.type = SimEvent::Landing;
    ev.x = c.x; ev.y = road; ev.z = c.z;
    ev.intensity = impact;
    ev.count = std::min(c.air, 127);
    ev.carIndex = carIndex;
    events.push_back(ev);
    if (impact > 16.0f) {                                // a hard landing
        SimEvent spk;
        spk.type = SimEvent::Sparks;
        spk.x = c.x; spk.y = road + 0.3f; spk.z = c.z;
        spk.count = 12;
        spk.carIndex = carIndex;
        events.push_back(spk);
        SimEvent cr;
        cr.type = SimEvent::Crash;
        cr.intensity = std::min(1.0f, impact / 20.0f);
        cr.carIndex = carIndex;
        events.push_back(cr);
    }
    if (c.wreck > 0) return;
    c.y = road;
    c.vy = roadClimb;
    c.air = 0;
}

// Ticks until a flying car comes down on the road ahead (followed along the road)
int GameSim::airTimeLeft(const Car &c) const
{
    float y = c.y, vy = c.vy, sAt = c.s;
    int t = 0;
    do { y += vy; vy -= GRAVITY; sAt += c.speed * JUMP_CARRY; ++t; } while (t < 400 && y > surfaceAt(sAt, c.d, c.path));
    return t;
}

// The car came down beyond the guard rail: into the water, onto the ground below, or down a
// gorge. It costs a lot, and the car is put back at the roadside where it left the road, as
// after a wreck (its health is only all gone if the damage takes it).
void GameSim::landOutside(Car &c, int idx)
{
    const int kind = overWater(c) ? 0 : overGorge(c.outS) ? 2 : 1;
    if (RaceObs *ob = obsOf(idx); ob && state == Racing) { ++ob->flewOff; moodMistake(*ob, tick, 1.5f); }
    SimEvent sp;
    sp.type = SimEvent::Splash;
    sp.count = kind;
    sp.carIndex = idx;
    sp.x = c.x; sp.y = c.y; sp.z = c.z;
    sp.intensity = c.jumpDanger;                         // (0: not off a side ramp)
    events.push_back(sp);
    const float side = c.d >= 0.0f ? 1.0f : -1.0f;
    const float danger = std::max(1.0f, c.jumpDanger);
    c.outside = false;
    c.path = 0;
    pointAt(c.outS, side * (roadHalfAt(c.outS) - 1.6f), c.x, c.z);
    c.hint = int(c.outS);
    locate(c);
    c.y = heightAt(c.s, c.d);
    c.vy = 0.0f;
    c.air = 0;
    c.heading = headingAt(c.s);
    c.speed = 0.0f;
    c.barrel = c.barrelRate = c.spinRate = c.jumpDanger = 0.0f;
    const int cause = kind == 0 ? SimEvent::ByWater : SimEvent::ByOffroad;
    damage(c, idx, (kind == 0 ? 30.0f : kind == 2 ? 40.0f : 20.0f) * danger, cause, true, 0.3f);
    if (c.wreck == 0 && state == Racing) startWreck(c, idx, cause, false, false);
}

float GameSim::shortcutGateS() const
{
    if (!hasShortcut()) return 0.0f;
    for (float s = 2.0f; s < std::min(200.0f, shortcutEnd()); s += 2.0f) {
        float x, z, ms, md;
        pointAt(s, 0.0f, x, z, 1);
        project(x, z, shortcutFrom() + s, ms, md, 0);
        if (std::fabs(md) > roadHalfAt(ms) + roadHalfAt(s, 1) + 1.5f) return s;
    }
    return 8.0f;
}

void GameSim::updateShortcutGates()
{
    if (!hasShortcut()) return;
    const int closedTop = std::min(SHORTCUT_CLOSED_TOP, int(cars.size()) - 1);
    for (int i = 0; i < int(cars.size()); ++i) {
        Car &c = cars[size_t(i)];
        if (c.path != 0) continue;                       // (on the shortcut: it carries on)
        const float toFork = aheadOf(c.s, shortcutFrom());
        if (toFork < 60.0f && toFork > -110.0f) continue;    // (decided: kept past the fork)
        c.cutClosed = state == Racing && rankOf(i) <= closedTop;
    }
}

void GameSim::updateAI(Car &c, int carIndex)
{
    PlayerInput in = computeAIInput(c, carIndex, true);
    updateCar(c, in, carIndex);
}

// A CPU sees a death trap in its lane and steers out of the way in good time - only about one
// time in twelve does it miss it (a hash of the mine, the car and the lap: the same on every computer).
PlayerInput GameSim::computeAIInput(Car &c, int carIndex, bool asRival)
{
    PlayerInput in = computeAIDrive(c, carIndex, asRival);
    if (c.wreck > 0 || c.air > 0 || c.path != 0) return in;
    const float reach = 25.0f + std::fabs(c.speed) * 75.0f;
    for (const Obstacle &o : obstacles) {
        if (o.type != Obstacle::Mine || o.knocked || !present(o.fromLap)) continue;
        const float ahead = aheadOf(o.s, c.s);
        if (ahead < 0.0f || ahead > reach || std::fabs(c.d - o.d) > 3.4f) continue;
        const uint32_t h = (uint32_t(o.s) * 2654435761u) ^ (uint32_t(carIndex + 1) * 2246822519u) ^ (uint32_t(c.lap + 1) * 3266489917u);
        if ((h >> 8) % 12u == 0u) continue;
        const float edge = roadHalfAt(c.s, 0) - 1.6f;
        const float away = c.d >= o.d ? (c.d + 1.0f < edge ? 1.0f : -1.0f) : (c.d - 1.0f > -edge ? -1.0f : 1.0f);
        const float off = wrapAngle(c.heading - headingAt(c.s, 0));
        in.left = in.right = false;
        if (std::fabs(off) > 0.16f) (off > 0.0f ? in.left : in.right) = true;   // (not sideways: back along the road)
        else (away > 0.0f ? in.right : in.left) = true;
        break;
    }
    return in;
}

PlayerInput GameSim::computeAIDrive(Car &c, int carIndex, bool asRival)
{
    PlayerInput in;
    if (c.wreck > 0) {                                   // wrecked: wait to be back in the race
        c.aiCheckTimer = 0; c.aiCheckProg = c.progress; c.aiReverse = 0;
        return in;
    }

    // ---- 1. The mind (shared/aidriver.cpp): skill, personality, mood, tactics, mistakes.
    // No speed help: every car has the same top speed (the old difficulty levels and the
    // catch-up "rubber band" are gone); asRival = false is the neutral reference driver.
    c.speedScale = 1.0f;
    AiView v = aiThink(c, carIndex, asRival);
    float playerProg = c.progress;                       // the leading human driver
    bool haveHuman = false;
    for (int j = 0; j < int(cars.size()); ++j) {
        if (j == carIndex || cars[j].aiLevel > 0) continue;
        if (!haveHuman || cars[j].progress > playerProg) { playerProg = cars[j].progress; haveHuman = true; }
    }
    const float gap = playerProg - c.progress;           // > 0: behind the people (nitro and boost habits)
    if (!asRival) v.boostEvery = (gap > 0.0f || raceDistance() - c.progress < 1500.0f) ? 600 : 1500;

    if (state != Racing) {
        c.aiReverse = 0; c.aiCheckTimer = 0; c.aiCheckProg = c.progress;
        c.aiTarget = c.d;
        return in;
    }

    // ---- 2. Progress watchdog: if we barely moved in 1.5 s we are wedged -> back out
    // (paused while backing out, and restarted afterwards, so it can't loop)
    if (c.aiReverse == 0 && ++c.aiCheckTimer >= 90) {
        if (c.progress - c.aiCheckProg < 10.0f) c.aiReverse = 50;
        c.aiCheckTimer = 0;
        c.aiCheckProg = c.progress;
    }

    // A shortcut: decided once a lap, 300 m before the fork, whether to take it (better and
    // bolder drivers more often; a hash of the race, car and lap, the same on every computer)
    if (hasShortcut() && c.path == 0) {
        const float toFork = aheadOf(c.s, shortcutFrom());
        if (toFork > 0.0f && toFork < 300.0f && c.cutPlanLap != c.lap) {
            const uint32_t h = (uint32_t(carIndex + 1) * 2654435761u) ^ (uint32_t(c.lap + 7) * 40503u)
                             ^ (uint32_t(mapIndex + 3) * 2246822519u) ^ (asRival ? aiSeed * 97u : 0u);
            c.cutPlan = int((h >> 7) % 100u) < v.cutChance;
            c.cutPlanLap = c.lap;
        }
        if (c.cutClosed) c.cutPlan = false;              // (closed to the leaders)
    }

    // ---- 3. Hazards ahead --------------------------------------------------------
    // dist = metres ahead of us along the lap; [x0, x1] = band of road it blocks, measured
    // from the road centre at the hazard's own place (d), so bends don't shift it.
    struct Hazard { float dist, x0, x1; bool soft, racer; };   // soft = cones: avoid, but never brake for them
    std::vector<Hazard> hz;
    const float horizon = 30.0f + std::max(0.0f, c.speed) * 75.0f * v.horizonMul;   // (weaker drivers see things later)
    auto addHazard = [&](float dist, float offMin, float offMax, bool soft = false, bool racer = false) {
        if (dist < -2.0f || dist > horizon) return;
        hz.push_back({std::max(0.0f, dist), offMin, offMax, soft, racer});
    };
    const float CAR_HALF = 1.15f;                        // car half width + a little margin
    // In the air: steering lines the car up with the road (below); the brake slows the roll
    // so it lands upright - aiming at the nearest whole roll for the time left in the air,
    // less exactly the lower the level
    bool rollBrake = false;
    if (c.air > 0 && c.barrelRate != 0.0f) {
        const float tLeft = float(airTimeLeft(c));
        const float sgn = c.barrelRate > 0.0f ? 1.0f : -1.0f;
        const float done = c.barrel * sgn, endAt = done + std::fabs(c.barrelRate) * tLeft;
        const float aim = std::max(done, std::floor(endAt / (2.0f * PI) + 0.25f) * 2.0f * PI);
        const float slack = v.rollSlack;
        rollBrake = endAt > aim + slack;
    }

    // the shortcut has no obstacles or traffic, but on its last 150 m the main road past the
    // merge counts (the shortcut joins it in the middle)
    const bool nearMerge = c.path == 1 && c.s > shortcutEnd() - 150.0f;
    auto distTo = [&](float s) {
        return c.path == 0 ? aheadOf(c.s, s) : (shortcutEnd() - c.s) + aheadOf(shortcutTo(), s);
    };
    // Side ramps: each CPU decides (a hash; its personality, skill and mood set the chance,
    // less with low health or on a red ramp) whether to take a ramp; it lines up with one it
    // takes, the others are in the way
    int takeRamp = -1;
    float takeDist = 0.0f;
    for (size_t k = 0; k < sideList.size() && c.path == 0; ++k) {
        const SideRamp &r = sideList[k];
        const float dist = aheadOf(c.s, r.s - r.run);
        if (!present(r.fromLap) || dist < -r.run - 1.0f || dist > horizon + 60.0f) continue;
        const uint32_t h = (uint32_t(carIndex + 1) * 2246822519u) ^ (uint32_t(k + 11) * 2654435761u) ^ (uint32_t(c.lap + 3) * 40503u)
                         ^ (asRival ? aiSeed * 131u : 0u);
        const int chance = v.rampChance - (c.health < 50.0f ? 20 : 0) - (r.danger > 1.0f ? 10 : 0)
                         + (c.aiBrain >= 0 ? int(25.0f * c.aiBrainRamp) : 0);   // (its brain's taste for ramps)
        const bool aligned = std::fabs(c.d - r.d) < r.half - 0.6f;
        if (dist < 0.0f ? aligned : int((h >> 9) % 100u) < chance) { takeRamp = int(k); takeDist = dist; continue; }
        addHazard(std::max(0.0f, dist), r.d - r.half - CAR_HALF, r.d + r.half + CAR_HALF);
    }
    for (const Obstacle &o : obstacles) {
        if (o.knocked || !present(o.fromLap) || (c.path != 0 && !nearMerge)) continue;
        float off = o.d, dist = distTo(o.s);
        float r = o.block() ? o.hx : o.type == Obstacle::Cone ? 0.45f : o.type == Obstacle::Mine ? 1.5f : o.type == Obstacle::Stone ? 0.9f : 0.7f;
        addHazard(dist, off - r - CAR_HALF, off + r + CAR_HALF, o.type == Obstacle::Cone);
    }
    // Traffic and racers alongside us (overlapping lengthwise) go into `beside`: they limit
    // where we may steer right now, so we don't scrape along them, without hiding the
    // hazards further ahead.
    std::vector<Hazard> beside;
    for (const Traffic &t : traffic) {
        if (c.path != 0 && !nearMerge) break;            // (traffic stays on the main road)
        // only traffic we are catching - or right in front of us: after bumping into it we
        // are slower than it for a moment, and must not drive straight back into it
        const float halfL = t.truck ? 5.6f : 4.2f;
        const float dist = distTo(t.s) - halfL;         // to its back bumper (< 0: alongside)
        if (dist < -2.0f * halfL || (t.speed > c.speed + 0.05f && dist > 12.0f)) continue;
        // a car changing lanes blocks both the lane it is leaving and the one it is entering
        const float x0 = std::min(t.lane, t.targetLane) - 2.1f, x1 = std::max(t.lane, t.targetLane) + 2.1f;
        if (dist < 0.0f) {
            beside.push_back({0.0f, x0, x1, false, false});
        } else {
            addHazard(dist, x0, x1);
        }
    }
    for (size_t j = 0; j < cars.size(); ++j) {           // other racers: overtake, don't ram
        if (int(j) == carIndex || cars[j].path != c.path) continue;   // (the other road: collisions only)
        const Car &o = cars[j];
        const float ahead = aheadOf(c.s, o.s);
        if (std::fabs(ahead) < 4.2f) {
            beside.push_back({0.0f, o.d - 2.2f, o.d + 2.2f, false, true});
            continue;
        }
        if (o.speed > c.speed + 0.05f) continue;
        if (int(j) == v.attackCar) continue;            // attacking it: it is no hazard, it is the target
        addHazard(ahead - 4.0f, o.d - 2.2f, o.d + 2.2f, false, true);
    }
    std::sort(hz.begin(), hz.end(), [](const Hazard &a, const Hazard &b) { return a.dist < b.dist; });

    // ---- 4. Find the gap ---------------------------------------------------------
    // Take the next "row" of hazards (everything within 6 m of the nearest one),
    // work out the free gaps across the road, and aim for the nearest point of the
    // best gap - preferring gaps that line up with the row after it.
    typedef std::pair<float, float> Gap;
    const float EDGE = roadHalfAt(c.s, c.path) - 1.0f;
    const float myOff = c.d;
    auto cut = [](std::vector<Gap> &gaps, const Hazard &h) {
        std::vector<Gap> next;
        for (const Gap &g : gaps) {
            if (h.x1 <= g.first || h.x0 >= g.second) { next.push_back(g); continue; }
            if (h.x0 > g.first)  next.push_back(Gap(g.first, h.x0));
            if (h.x1 < g.second) next.push_back(Gap(h.x1, g.second));
        }
        gaps.swap(next);
    };
    // the next row also has to be reachable past whatever is alongside us now
    auto freeGaps = [&](size_t first, size_t last, std::vector<Gap> &gaps, bool nextRow, bool hardOnly = false) {
        gaps.assign(1, Gap(-EDGE, EDGE));
        if (nextRow)
            for (const Hazard &h : beside) cut(gaps, h);
        for (size_t k = first; k < last; ++k)
            if (!hardOnly || !hz[k].soft) cut(gaps, hz[k]);
    };
    float target = std::clamp(c.aiTarget, -EDGE, EDGE);
    float rowDist = 1e9f;
    size_t rowEnd = 0;
    bool rowBlocked = false;
    if (!hz.empty() || !beside.empty()) {
        if (!hz.empty()) rowDist = hz[0].dist;
        while (rowEnd < hz.size() && hz[rowEnd].dist <= rowDist + 6.0f) ++rowEnd;
        std::vector<Gap> gaps, gaps2;
        freeGaps(0, rowEnd, gaps, true);
        if (gaps.empty()) freeGaps(0, rowEnd, gaps, true, true);   // no clean gap: just go through the cones
        size_t row2End = rowEnd;
        bool haveRow2 = rowEnd < hz.size() && hz[rowEnd].dist < rowDist + 50.0f;
        if (haveRow2) {
            while (row2End < hz.size() && hz[row2End].dist <= hz[rowEnd].dist + 6.0f) ++row2End;
            freeGaps(rowEnd, row2End, gaps2, false);
        }
        if (gaps.empty()) {
            rowBlocked = true;                           // e.g. traffic filling the only gap: wait
        } else {
            float bestScore = -1e9f;
            // slow (crawling up to a barrier, or backing out of one): aim well inside the gap,
            // not just past its edge, or the car keeps touching the same barrier
            const float edgeMargin = std::fabs(c.speed) < 0.45f ? 1.5f : 0.5f;
            for (const Gap &g : gaps) {
                float w = g.second - g.first, margin = std::min(edgeMargin, w * 0.5f);
                float p = std::clamp(myOff, g.first + margin, g.second - margin);
                float sc = -std::fabs(p - myOff) + 0.4f * std::min(w, 4.0f);
                if (v.passCar >= 0 && (p - cars[size_t(v.passCar)].d) * float(v.passSide) > 1.0f) sc += 1.5f;   // the side it chose to pass on
                if (haveRow2 && !gaps2.empty()) {
                    float move2 = 1e9f;
                    for (const Gap &g2 : gaps2) move2 = std::min(move2, std::fabs(std::clamp(p, g2.first, g2.second) - p));
                    sc -= 0.5f * move2;
                }
                if (sc > bestScore) { bestScore = sc; target = p; }
            }
        }
    }
    // the mind's wishes on an open road: drive into the car it attacks, sit in the lane of the
    // car it defends against, and its own line (a weaker driver wanders off the best one)
    if (c.air == 0 && rowDist > 30.0f && beside.empty()) {
        if (v.attackCar >= 0) target = std::clamp(cars[size_t(v.attackCar)].d, -EDGE, EDGE);
        else if (v.defendCar >= 0) target = std::clamp(cars[size_t(v.defendCar)].d + 1.2f * float(v.coverSide), -EDGE + 1.0f, EDGE - 1.0f);
        else if (v.pressureCar >= 0) target = std::clamp(cars[size_t(v.pressureCar)].d, -EDGE + 1.0f, EDGE - 1.0f);   // right behind them
    }
    if (c.air == 0 && rowDist > 25.0f) target = std::clamp(target + v.lineErr, -EDGE, EDGE);
    if (c.air > 0) {
        target = std::clamp(myOff, -EDGE + 2.5f, EDGE - 2.5f);   // flying: come down on the road, pointing along it
    } else if (takeRamp >= 0 && takeDist < 70.0f && rowDist > takeDist + 2.0f) {
        // line up with the side ramp (the last 45 m before it are kept clear of obstacles;
        // anything nearer than the ramp is dodged first)
        const SideRamp &r = sideList[size_t(takeRamp)];
        target = std::clamp(r.d, r.d - r.half + 1.0f, r.d + r.half - 1.0f);
    } else if (c.path == 0 && nearRamp(c.s, 120.0f, 10.0f) && rowDist > 30.0f) {
        target = std::clamp(target, -2.5f, 2.5f);         // a ramp ahead (and nothing close): line up near the middle
    } else if (c.path == 0 && rowDist > 45.0f && beside.empty()) {   // nothing close: go and grab a nitro can
        for (const NitroPickup &np : nitroPickups) {
            float dist = aheadOf(c.s, np.s), off = np.d;
            if (np.active && dist > 0.0f && dist < 60.0f && std::fabs(off) < EDGE) { target = off; break; }
        }
    }
    c.aiTarget = target;

    // ---- 4b. The evolved brain (shared/brain.h) drives the open road ------------------
    // The rules keep the tricky moments: a hazard close ahead (a bruiser ignores other
    // racers), anything alongside, a battle the mind picked, lining up with a ramp, flying,
    // backing out - and the safety net: off the road or pointing wrong for 1 s (or badly
    // wrong at once), and the rules drive for 3 s.
    const Brain *brain = nullptr;
    int brainKind = -1;
    if (asRival && brainsOn && c.aiLevel > 0) {
        if (brainOver[carIndex]) { brain = brainOver[carIndex]; brainKind = brainOverKind[carIndex]; }
        else if (c.aiClone && cloneBrain) { brain = cloneBrain; brainKind = BrainClone; }
        else { brainKind = brainForPersona(c.aiPersona); brain = bundledBrain(brainKind); }
    }
    c.aiBrain = brain ? brainKind : -1;
    c.aiBrainDriving = false;
    if (brain) {
        if (c.aiBrainOff > 0) --c.aiBrainOff;
        if (c.aiReverse > 0) c.aiBrainOff = std::max(c.aiBrainOff, 120);   // backing out: the rules finish the job (2 s more)
        float hzDist = 1e9f, hzMid = 0.0f;
        for (const Hazard &h : hz)                       // (sorted, nearest first; cones count too - they hurt)
            if (!(brainKind == BrainBruiser && h.racer)) { hzDist = h.dist; hzMid = 0.5f * (h.x0 + h.x1); break; }
        // (the rules take over while they still have room to slide across into the gap they
        // picked: the same distance their brakes allow for, plus a margin)
        const float spdB = std::max(0.0f, c.speed);
        const float roomNeeded = 10.0f * spdB + std::fabs(target - c.d) * (4.0f + 6.0f * spdB) + 20.0f + 25.0f * spdB;
        const bool tricky = c.aiReverse > 0 || c.air > 0 || !beside.empty() || hzDist < roomNeeded
                         || (takeRamp >= 0 && takeDist < 80.0f) || v.attackCar >= 0 || v.defendCar >= 0 || v.pressureCar >= 0
                         || (c.path == 0 && nearRamp(c.s, 50.0f, 10.0f));
        if (!tricky && c.aiBrainOff == 0) {
            // (a clone sees the road ahead as it was recorded: GameSim::cloneView)
            const bool clone = brainKind == BrainClone;
            float bi[BRAIN_IN], bo[BRAIN_OUT];
            if (clone) cloneView(carIndex, c.aiBrainSteer, bi);
            else brainInputs(c, carIndex, hzDist, hzMid, bi);
            brain->run(bi, bo);
            // its skill: a weaker driver reacts later (its steering follows the brain more
            // slowly) and its line wanders a little (a clone already drives as well as its person)
            const float sk = clone ? 1.0f : std::clamp(c.aiSkill, 0.0f, 1.0f);
            uint32_t hh = (uint32_t(tick / 15) * 2654435761u) ^ (uint32_t(carIndex + 1) * 2246822519u) ^ (aiSeed * 3266489917u);
            hh ^= hh >> 15; hh *= 2246822519u; hh ^= hh >> 13; hh *= 3266489917u; hh ^= hh >> 16;
            const float wobble = (float(hh >> 8) / 16777216.0f - 0.5f) * (1.0f - sk) * 0.6f;
            if (!clone) {
                c.aiBrainSteer += (bo[OutSteer] - c.aiBrainSteer) * (0.35f + 0.65f * sk);
                const float steer = c.aiBrainSteer + wobble;
                in.right = steer > 0.15f;
                in.left  = steer < -0.15f;
                in.up = bo[OutPedal] > 0.0f;
                in.down = bo[OutPedal] < -0.5f && c.speed > 0.3f;
            } else {
                // a clone learned how much its person steers and accelerates (shared/clone.h):
                // tap the keys that often - each intent adds up, and a tap uses one up
                c.aiCloneSteer = std::clamp(c.aiCloneSteer + cloneIntent(bo[OutSteer]), -1.5f, 1.5f);
                if (c.aiCloneSteer > 0.5f) { in.right = true; c.aiCloneSteer -= 1.0f; }
                else if (c.aiCloneSteer < -0.5f) { in.left = true; c.aiCloneSteer += 1.0f; }
                c.aiClonePedal = std::clamp(c.aiClonePedal + cloneIntent(bo[OutPedal]), -1.5f, 1.5f);
                if (c.aiClonePedal > 0.5f) { in.up = true; c.aiClonePedal -= 1.0f; }
                else if (c.aiClonePedal < -0.5f) { in.down = c.speed > 0.3f; c.aiClonePedal += 1.0f; }
                // what it sees of its own steering: the share of the last ticks (as recorded)
                c.aiBrainSteer += (0.9f * (in.right ? 1.0f : in.left ? -1.0f : 0.0f) - c.aiBrainSteer) / float(CloneRecorder::AHEAD);
            }
            // the mind still decides how hard it commits, whom it waits behind, when nitro is saved;
            // and its skill limits its corner speed like the rules' (exactly theirs at skill 0,
            // loose enough at skill 1 that the brain's own line decides)
            if (!clone && !c.nitroOn && !c.boosting && c.speed > MAX_SPEED * (v.commit - 0.10f * (1.0f - sk))) in.up = false;
            if (!clone) {
                const float need = std::fabs(wrapAngle(headingAhead(c, 50.0f) - headingAhead(c, 20.0f))) / 30.0f;
                const float grip = 0.030f / std::max(0.2f, spdB) * 1.15f;
                if (need > grip * (v.liftAt + 0.30f * sk)) in.up = false;
                if (need > grip * (v.brakeAt + 0.30f * sk) && c.speed > 0.5f) { in.down = true; in.up = false; }
            }
            if (v.waitCar >= 0) {
                const Car &o = cars[size_t(v.waitCar)];
                if (aheadOf(c.s, o.s) < 14.0f && c.speed > o.speed) in.up = false;
            }
            // nitro can't be braked off, so only with clear road well ahead (the rules then have
            // room when they take over) and no ramp coming (too fast off a ramp flies past the
            // road); the L boost likewise, and only with health to spare
            const bool rampSoon = (takeRamp >= 0 && takeDist < 250.0f) || (c.path == 0 && nearRamp(c.s, 250.0f, 10.0f));
            in.nitro = bo[OutNitro] > 0.3f && c.nitroCharges > 0 && c.nitroTimer == 0 && !v.saveNitro && hzDist > 150.0f && !rampSoon;
            if (boostAllowed() && bo[OutBoost] > 0.3f && c.health > 60.0f && hzDist > 100.0f && !rampSoon)
                in.boost = c.boosting ? c.boostHold < 170 : (c.boostHold == 0 && c.nitroTimer == 0 && c.health > 80.0f);
            c.aiBrainRamp += (bo[OutRamps] - c.aiBrainRamp) * 0.01f;
            // the safety net
            const float headErr = std::fabs(wrapAngle(c.heading - headingAhead(c, 0.0f)));
            const bool off = std::fabs(c.d) > roadHalfAt(c.s, c.path) - 0.3f;
            if (off || headErr > 0.9f || c.speed < 0.1f) ++c.aiBrainBad; else c.aiBrainBad = 0;
            if (c.aiBrainBad > 60 || headErr > 1.6f) { c.aiBrainOff = 180; c.aiBrainBad = 0; ++c.aiTakeovers; }
            c.aiBrainDriving = true;
            ++c.aiBrainTicks;
            c.aiTarget = std::clamp(c.d, -EDGE, EDGE);   // (the rules pick up from where the car is)
            return in;
        }
        ++c.aiRulesTicks;
        c.aiBrainSteer = 0.0f;
        c.aiBrainBad = 0;
    }

    // ---- 5. Steering: pure pursuit towards a point on the target line ------------
    const float spd = std::max(0.2f, std::fabs(c.speed));
    const float move = std::fabs(target - myOff);
    // aim closer (sharper steering) while moving across, further (smoother) when settled
    const float look = (move > 1.0f) ? std::clamp(spd * 11.0f, 6.0f, 14.0f) : std::clamp(spd * 17.0f, 9.0f, 24.0f);
    float tx, tz;
    aheadPoint(c, look, target, tx, tz);
    float desired = std::atan2(tx - c.x, c.z - tz);
    float moveAngle = c.heading + c.driftAngle * 0.45f * 0.6f;   // the car slides a little when steering
    float err = desired - moveAngle;
    while (err > PI) err -= 2.0f * PI;
    while (err < -PI) err += 2.0f * PI;

    if (c.aiReverse > 0) {
        // back straight out, lining the car up with the road (reversing at an angle
        // would slide us sideways and undo the progress we made towards the gap);
        // steering works the other way round in reverse
        if (--c.aiReverse == 0) { c.aiCheckTimer = 0; c.aiCheckProg = c.progress; }
        float alignErr = wrapAngle(headingAhead(c, 0.0f) - c.heading);
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
    float turnNeed = std::fabs(wrapAngle(headingAhead(c, 50.0f) - headingAhead(c, 20.0f))) / 30.0f;
    float turnMax = 0.030f / spd * 1.15f;                 // rad per metre at this speed (+drift)
    const float bendDir = wrapAngle(headingAhead(c, 50.0f) - headingAhead(c, 20.0f));
    const float bankAhead = c.path == 0 ? bankAt(c.s + 35.0f) : 0.0f;
    if (bendDir * bankAhead > 0.0f) turnMax *= 1.0f + 1.5f * std::fabs(bankAhead);   // a banked corner
    if (turnNeed > turnMax * v.liftAt) in.up = false;    // (skill sets the braking point; mood and mistakes move it)
    if (turnNeed > turnMax * v.brakeAt && c.speed > 0.5f) in.down = true;
    // a share of top speed on the straights (its commitment), and waiting behind a car
    if (!c.nitroOn && !c.boosting && c.speed > MAX_SPEED * v.commit) in.up = false;
    if (v.waitCar >= 0) {
        const Car &o = cars[size_t(v.waitCar)];
        if (aheadOf(c.s, o.s) < 14.0f && c.speed > o.speed) in.up = false;
    }
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
        bool straight = std::fabs(wrapAngle(headingAhead(c, 120.0f) - headingAhead(c, 0.0f))) < 0.12f && turnNeed < turnMax * 0.4f;
        bool clear = true;
        for (const Hazard &h : hz)
            if (!h.soft && h.dist < 110.0f && target > h.x0 && target < h.x1) clear = false;
        const bool wantIt = !v.saveNitro;                // (a late charger keeps it for the last third)
        if (straight && clear && wantIt && std::fabs(err) < 0.1f) in.nitro = true;
    }
    if (c.air > 0) { in.up = false; in.down = rollBrake; }
    // ---- 8. L boost: now and then on a straight, with plenty of health, held for 1-2.5 s
    // (never into the costly part after 3 s; more often behind, and near the finish)
    const bool straightOn = c.air == 0 && std::fabs(err) < 0.12f
                         && std::fabs(wrapAngle(headingAhead(c, 80.0f) - headingAhead(c, 0.0f))) < 0.10f;
    // (how long and how often: its personality and mood; a show-off or a wild card keeps
    // holding into the corners, and a wild card sometimes past 10 s - its engine burns out)
    if (boostAllowed() && (v.boostAnyRoad ? c.boosting || straightOn : straightOn && c.health > 40.0f)) {
        const uint32_t h = (uint32_t(tick) * 2654435761u) ^ (uint32_t(carIndex + 5) * 2246822519u) ^ (asRival ? aiSeed : 0u);
        if (c.boosting) {
            in.boost = c.boostHold < v.boostHold;
        } else if (c.boostHold == 0 && c.nitroTimer == 0 && (v.boostAnyRoad || c.health > 70.0f)) {
            in.boost = (h >> 10) % uint32_t(std::max(1, v.boostEvery)) == 0u;
        }
    }

    return in;
}

// What a brain sees (shared/brain.h): its speed and where it is on the road, how the road
// bends and leans ahead, the nearest racers ahead and behind, its nitro, boost and health,
// how much of the race is left, the nearest hard hazard and side ramp, its last steering
void GameSim::brainInputs(const Car &c, int idx, float hzDist, float hzMid, float o[BRAIN_IN]) const
{
    const float h0 = headingAhead(c, 0.0f);
    const float half = roadHalfAt(c.s, c.path);
    o[0] = c.speed / MAX_SPEED;
    o[1] = std::clamp(wrapAngle(c.heading - h0) * 2.0f, -3.0f, 3.0f);
    o[2] = std::clamp(c.d / half, -2.0f, 2.0f);
    static const float AT[5] = {10.0f, 25.0f, 50.0f, 80.0f, 120.0f}, K[5] = {3.0f, 2.0f, 1.5f, 1.0f, 0.7f};
    for (int k = 0; k < 5; ++k) o[3 + k] = std::clamp(wrapAngle(headingAhead(c, AT[k]) - h0) * K[k], -3.0f, 3.0f);
    o[8] = (c.path == 0 ? bankAt(c.s + 35.0f) : 0.0f) * 4.0f;
    o[9] = std::clamp(gradeAt(c.s, c.path) * 10.0f, -2.0f, 2.0f);
    o[10] = half / 12.0f;
    float aD = 1e9f, aOff = 0.0f, aSp = 0.0f, bD = 1e9f, bOff = 0.0f;
    for (int j = 0; j < int(cars.size()); ++j) {
        const Car &q = cars[size_t(j)];
        if (j == idx || q.path != c.path || q.wreck > 0) continue;
        const float a = aheadOf(c.s, q.s);
        if (a > 0.0f && a < aD) { aD = a; aOff = q.d; aSp = q.speed; }
        if (a <= 0.0f && -a < bD) { bD = -a; bOff = q.d; }
    }
    const bool A = aD < 60.0f, B = bD < 30.0f;
    o[11] = A ? 1.0f - aD / 60.0f : 0.0f;
    o[12] = A ? std::clamp((aOff - c.d) / 6.0f, -2.0f, 2.0f) : 0.0f;
    o[13] = A ? std::clamp((aSp - c.speed) * 3.0f, -2.0f, 2.0f) : 0.0f;
    o[14] = B ? 1.0f - bD / 30.0f : 0.0f;
    o[15] = B ? std::clamp((bOff - c.d) / 6.0f, -2.0f, 2.0f) : 0.0f;
    o[16] = (c.nitroCharges > 0 && c.nitroTimer == 0) ? 1.0f : 0.0f;
    o[17] = c.nitroOn ? 1.0f : 0.0f;
    o[18] = std::min(float(c.boostHold) / 180.0f, 2.0f);
    o[19] = c.health / 100.0f;
    o[20] = std::clamp(1.0f - c.progress / std::max(1.0f, raceDistance()), 0.0f, 1.0f);
    o[21] = hzDist < 150.0f ? 1.0f - hzDist / 150.0f : 0.0f;
    o[22] = hzDist < 150.0f ? std::clamp((hzMid - c.d) / 6.0f, -2.0f, 2.0f) : 0.0f;
    float rD = 1e9f, rOff = 0.0f;
    for (const SideRamp &r : sideList) {
        if (c.path != 0 || !present(r.fromLap)) continue;
        const float dist = aheadOf(c.s, r.s - r.run);
        if (dist > 0.0f && dist < rD) { rD = dist; rOff = r.d; }
    }
    o[23] = rD < 150.0f ? 1.0f - rD / 150.0f : 0.0f;
    o[24] = rD < 150.0f ? std::clamp((rOff - c.d) / 6.0f, -2.0f, 2.0f) : 0.0f;
    o[25] = c.aiBrainSteer;
    o[26] = std::clamp(c.driftAngle * 3.0f, -2.0f, 2.0f);
    o[27] = 1.0f;
}

// The nearest thing in the way ahead, as the clone sees it: obstacles, traffic we are catching
// (or right in front), slower racers - the same things the rules dodge, found without the
// rules' mind, so a person's recording and the clone's driving see them alike
void GameSim::hazardAhead(const Car &c, int idx, float &dist, float &mid) const
{
    dist = 1e9f; mid = 0.0f;
    auto consider = [&](float a, float d) { if (a > -2.0f && a < 150.0f && a < dist) { dist = std::max(0.0f, a); mid = d; } };
    if (c.path == 0) {
        for (const Obstacle &o : obstacles)
            if (!o.knocked && present(o.fromLap)) consider(aheadOf(c.s, o.s), o.d);
        for (const Traffic &t : traffic) {
            const float halfL = t.truck ? 5.6f : 4.2f, a = aheadOf(c.s, t.s) - halfL;
            if (t.speed > c.speed + 0.05f && a > 12.0f) continue;
            if (a > -halfL) consider(a, 0.5f * (t.lane + t.targetLane));
        }
    }
    for (int j = 0; j < int(cars.size()); ++j) {
        const Car &o = cars[size_t(j)];
        if (j == idx || o.path != c.path || o.wreck > 0) continue;
        const float a = aheadOf(c.s, o.s);
        if (a >= 4.2f && o.speed <= c.speed + 0.05f) consider(a - 4.0f, o.d);
    }
}

bool GameSim::cloneView(int idx, float prevSteer, float out[BRAIN_IN]) const
{
    if (idx < 0 || idx >= int(cars.size())) return false;
    const Car &c = cars[size_t(idx)];
    float hzDist, hzMid;
    hazardAhead(c, idx, hzDist, hzMid);
    brainInputs(c, idx, hzDist, hzMid, out);
    out[25] = prevSteer;
    if (state != Racing || c.wreck > 0 || c.air > 0 || c.finished) return false;
    for (int j = 0; j < int(cars.size()); ++j)           // something alongside: the rules' moment
        if (j != idx && cars[size_t(j)].path == c.path && std::fabs(aheadOf(c.s, cars[size_t(j)].s)) < 4.2f) return false;
    return hzDist > 20.0f + 55.0f * std::max(0.0f, c.speed);
}

bool GameSim::knock(Obstacle &o, float x, float z, float fx, float fz, float speed, int carIndex)
{
    if (o.block() || o.knocked) return false;
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
    deb.x = o.x; deb.y = heightAt(o.s, o.d) + (o.type == Obstacle::Cone ? 0.4f : 0.6f); deb.z = o.z;
    if (o.type == Obstacle::Cone) {
        deb.r = 0.95f; deb.g = 0.45f; deb.b = 0.05f; deb.count = 6;
    } else if (o.type == Obstacle::Stone) {
        deb.r = 0.45f; deb.g = 0.43f; deb.b = 0.40f; deb.count = 8;
    } else if (o.type == Obstacle::Mine) {
        deb.r = 1.0f; deb.g = 0.45f; deb.b = 0.05f; deb.count = 24;
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

// Nudge a car along (ds) and across (dd) the road, then find it again
void GameSim::moveOnTrack(Car &c, float ds, float dd) const
{
    float x0, z0, x1, z1;
    pointAt(c.s, c.d, x0, z0, c.path);
    pointAt(c.s + ds, c.d + dd, x1, z1, c.path);
    c.x += x1 - x0;
    c.z += z1 - z0;
    locate(c);
}

// A barrier is a box lying across the road; the car's nose and tail are two round probes.
// Pushes the car out and slows it; true on contact.
bool GameSim::hitBarrier(Car &c, const Obstacle &o) const
{
    const float h = -o.rot * PI / 180.0f;               // the road's heading where it stands
    const float rx = std::cos(h), rz = std::sin(h), fx = std::sin(h), fz = -std::cos(h);
    const float cfx = std::sin(c.heading), cfz = -std::cos(c.heading);
    bool hit = false;
    for (float off : {1.3f, -1.3f}) {
        const float px = c.x + cfx * off - o.x, pz = c.z + cfz * off - o.z;
        const float lx = px * rx + pz * rz, lz = px * fx + pz * fz;     // in the barrier's own frame
        const float qx = std::clamp(lx, -o.hx, o.hx), qz = std::clamp(lz, -o.hz, o.hz);
        const float dx = lx - qx, dz = lz - qz, d2 = dx * dx + dz * dz;
        const float RAD = 1.0f;
        if (d2 < RAD * RAD) {
            float d = std::sqrt(d2), nx, nz, pen;
            if (d < 1e-4f) { nx = 0; nz = -1; pen = RAD + (lz + o.hz); }   // inside: back down the road
            else           { nx = dx / d; nz = dz / d; pen = RAD - d; }
            c.x += (nx * rx + nz * fx) * pen;
            c.z += (nx * rz + nz * fz) * pen;
            c.speed *= 0.35f;
            hit = true;
        }
    }
    if (hit) locate(c);
    return hit;
}

// A traffic car is a box on the road: across = lane, along = s. Pushes the car out; true
// on contact. Running into its back drops you to half its speed.
bool GameSim::hitTraffic(Car &c, const Traffic &t) const
{
    const float halfW = 1.9f, halfL = t.truck ? 5.6f : 4.2f;
    const float lx = c.d - t.lane, lz = aheadOf(t.s, c.s);
    if (std::fabs(lx) >= halfW || std::fabs(lz) >= halfL) return false;
    const float px = halfW - std::fabs(lx), pz = halfL - std::fabs(lz);
    if (px < pz)       { moveOnTrack(c, 0.0f, lx > 0 ? px : -px); c.speed *= 0.93f; }
    else if (lz < 0.0f) { moveOnTrack(c, -pz, 0.0f); c.speed = std::min(c.speed, t.speed * 0.5f); }
    else               { moveOnTrack(c, pz, 0.0f); }
    return true;
}

void GameSim::collideWorld(Car &c, int carIndex)
{
    if (c.wreck > 0) return;                              // a wreck slides through everything
    if (c.y - heightAt(c.s, c.d, c.path) > 1.0f) return;   // flying over everything on the road
    float fx = std::sin(c.heading), fz = -std::cos(c.heading);

    // things on another level (Neon City's raised highway over the start street) are out of reach
    auto otherLevel = [&](float s, float d) { return std::fabs(heightAt(s, d) - c.y) > 3.0f; };
    for (Obstacle &o : obstacles) {
        const float ox = o.x - c.x, oz = o.z - c.z;
        if (ox * ox + oz * oz > 64.0f || !present(o.fromLap) || otherLevel(o.s, o.d)) continue;
        if (o.block()) {
            const bool fast = c.speed > 0.15f;
            const float v0 = std::fabs(c.speed);
            if (hitBarrier(c, o) && fast) {
                if (c.calm > 10)
                    damage(c, carIndex, (o.type == Obstacle::Concrete ? 50.0f : 45.0f) * OBSTACLE_DAMAGE * v0, SimEvent::ByBarrier, true, v0);
                SimEvent spk;
                spk.type = SimEvent::Sparks;
                spk.x = c.x; spk.y = c.y + 0.5f; spk.z = c.z;
                spk.count = 8;
                spk.carIndex = carIndex;
                events.push_back(spk);

                SimEvent cr;
                cr.type = SimEvent::Crash;
                cr.intensity = 0.85f;
                cr.carIndex = carIndex;
                events.push_back(cr);
            }
        } else if ((o.type == Obstacle::Mine || o.type == Obstacle::Stone) && c.air > 0
                   && c.y > heightAt(o.s, o.d) + 1.0f) {
            continue;                                    // (jumped over it)
        } else if (o.type == Obstacle::Mine) {
            if (c.wreck == 0 && c.ghost == 0 && knock(o, c.x, c.z, fx, fz, c.speed, carIndex)) {
                // the whole of its health (damage() scales it: the sum is worked out backwards), and the driver is thrown out
                damage(c, carIndex, (c.health + 1.0f) / (DAMAGE_SCALE * positionDamage(carIndex)), SimEvent::ByObject, true, 1.0f);
                SimEvent spk;
                spk.type = SimEvent::Sparks;
                spk.x = c.x; spk.y = c.y + 0.5f; spk.z = c.z;
                spk.count = 24;
                spk.carIndex = carIndex;
                events.push_back(spk);
            }
        } else if (knock(o, c.x, c.z, fx, fz, c.speed, carIndex)) {
            const float v0 = std::fabs(c.speed);
            if (o.type == Obstacle::Stone) {             // a heavy rock: it costs health and a lot of speed
                damage(c, carIndex, 11.0f * OBSTACLE_DAMAGE * v0, SimEvent::ByObject, true, v0);
                c.speed *= 0.5f;
                continue;
            }
            const float hit = o.type == Obstacle::Cone ? 1.5f : o.type == Obstacle::Tyres ? 7.0f : 9.0f;
            damage(c, carIndex, hit * OBSTACLE_DAMAGE * v0, SimEvent::ByObject, true, v0);
            c.speed *= (o.type == Obstacle::Cone) ? 0.85f : (o.type == Obstacle::Tyres) ? 0.65f : 0.55f;
        }
    }

    for (const Traffic &t : traffic) {
        if (c.path != 0) continue;                        // (traffic stays on the main road)
        // how fast the car runs into it: into its back by the difference in speed, from the
        // side by how fast it moves across; a traffic car changing lanes into you is at fault
        const float lz = aheadOf(t.s, c.s), lx = c.d - t.lane;
        const float halfL = t.truck ? 5.6f : 4.2f;
        const float across = std::fabs(std::sin(wrapAngle(c.heading - headingAt(c.s)))) * std::fabs(c.speed);
        const float closing = lz < -halfL + 1.2f ? std::max(0.0f, c.speed - t.speed) : 0.06f + across;
        const bool theirFault = std::fabs(t.targetLane - t.lane) > 0.3f && (t.targetLane - t.lane) * lx > 0.0f;
        if (!hitTraffic(c, t)) continue;
        if (c.calm > 20)
            damage(c, carIndex, (80.0f * closing + 4.0f) * (theirFault ? 0.3f : 1.0f), SimEvent::ByTraffic, !theirFault, closing);
        float tx, tz;
        pointAt(t.s, t.lane, tx, tz);

        SimEvent spk;
        spk.type = SimEvent::Sparks;
        spk.x = (c.x + tx) * 0.5f; spk.y = c.y + 0.5f; spk.z = (c.z + tz) * 0.5f;
        spk.count = 8;
        spk.carIndex = carIndex;
        events.push_back(spk);

        SimEvent cr;
        cr.type = SimEvent::Crash;
        cr.intensity = t.truck ? 1.0f : 0.75f;
        cr.carIndex = carIndex;
        events.push_back(cr);
    }

    // Check collectible nitro pickups on track
    for (NitroPickup &np : nitroPickups) {
        if (!np.active) continue;
        float dx = c.x - np.x;
        float dz = c.z - np.z;
        // (a car that already holds a charge drives through it and leaves it for the others)
        if (dx * dx + dz * dz < 2.5f * 2.5f && !otherLevel(np.s, np.d) && c.nitroCharges == 0) {
            np.active = false;
            np.downTicks = 0;
            ++np.takes;
            c.nitroCharges = 1;
            c.nitroLen = nitroLenFor(np.takes);
            if (c.nitroTimer == 0) c.nitro = 1.0f;
            SimEvent ev;
            ev.type = SimEvent::NitroPickupEvent;
            ev.x = np.x;
            ev.y = heightAt(np.s, np.d) + np.y;
            ev.z = np.z;
            ev.carIndex = carIndex;
            events.push_back(ev);
        }
    }
}

void GameSim::predictCollisions(Car &c) const
{
    if (c.y - heightAt(c.s, c.d, c.path) > 1.0f) return;
    for (const Obstacle &o : obstacles) {
        const float ox = o.x - c.x, oz = o.z - c.z;
        if (o.block() && present(o.fromLap) && ox * ox + oz * oz <= 64.0f) hitBarrier(c, o);
    }
    if (c.path == 0)
        for (const Traffic &t : traffic) hitTraffic(c, t);
}

bool GameSim::predictBump(Car &a, const Car &b) const
{
    if (a.wreck > 0 || b.wreck > 0 || a.ghost > 0 || b.ghost > 0) return false;
    if (std::fabs(a.y - b.y) > 1.4f) return false;
    if (a.path != b.path) {
        const float dx = b.x - a.x, dz = b.z - a.z, d2 = dx * dx + dz * dz;
        if (d2 >= 2.4f * 2.4f || d2 < 1e-6f) return false;
        const float d = std::sqrt(d2), push = (2.4f - d) * 0.5f;
        a.x -= dx / d * push; a.z -= dz / d * push;
        locate(a);
        return true;
    }
    const float lx = a.d - b.d, lz = aheadOf(b.s, a.s);
    if (std::fabs(lx) >= 1.9f || std::fabs(lz) >= 4.2f) return false;
    const float px = 1.9f - std::fabs(lx), pz = 4.2f - std::fabs(lz);
    if (px < pz) {
        moveOnTrack(a, 0.0f, ((lx > 0) ? 1.0f : -1.0f) * px * 0.5f);
    } else {
        moveOnTrack(a, ((lz > 0) ? 1.0f : -1.0f) * pz * 0.5f, 0.0f);
        a.speed = (a.speed + b.speed) * 0.5f * 0.95f;
    }
    return true;
}

// Two racers: a box each, across = d, along = s (so it works whichever way the road runs).
// On different roads (where the shortcut leaves or joins) they are two circles in the world.
void GameSim::collideCars(Car &a, Car &b, int idxA, int idxB)
{
    if (a.wreck > 0 || b.wreck > 0 || a.ghost > 0 || b.ghost > 0) return;   // wrecks and ghosts pass through
    if (std::fabs(a.y - b.y) > 1.4f) return;            // one flies over the other
    const float sa = a.speed, sb = b.speed;              // (before the collision changes them)
    if (a.path != b.path) {
        const float dx = b.x - a.x, dz = b.z - a.z, d2 = dx * dx + dz * dz;
        if (d2 >= 2.4f * 2.4f || d2 < 1e-6f) return;
        const float d = std::sqrt(d2), push = (2.4f - d) * 0.5f;
        a.x -= dx / d * push; a.z -= dz / d * push;
        b.x += dx / d * push; b.z += dz / d * push;
        locate(a);
        locate(b);
    } else {
        const float lx = a.d - b.d, lz = aheadOf(b.s, a.s);
        if (std::fabs(lx) >= 1.9f || std::fabs(lz) >= 4.2f) return;
        float px = 1.9f - std::fabs(lx), pz = 4.2f - std::fabs(lz);
        if (px < pz) {
            float s = ((lx > 0) ? 1.0f : -1.0f) * px * 0.5f;
            moveOnTrack(a, 0.0f, s);
            moveOnTrack(b, 0.0f, -s);
        } else {
            float s = ((lz > 0) ? 1.0f : -1.0f) * pz * 0.5f;
            moveOnTrack(a, s, 0.0f);
            moveOnTrack(b, -s, 0.0f);
            float avg = (a.speed + b.speed) * 0.5f * 0.95f;
            a.speed = b.speed = avg;
        }
    }

    // Damage, by how fast the two cars closed in on each other. Blame: how fast each one moved
    // towards the other (driving into its back, or steering into its side). The car at fault
    // takes 85% of the damage, the car it hit 15%; when both drove into each other, half each.
    // While they stay in contact it can hurt again every half second.
    if (pairCool[idxA][idxB] == 0) {
        pairCool[idxA][idxB] = 30;
        float nx = b.x - a.x, nz = b.z - a.z;
        const float nl = std::sqrt(nx * nx + nz * nz);
        if (nl > 1e-4f) {
            nx /= nl; nz /= nl;
            const float ha = a.heading + a.driftAngle * 0.45f, hb = b.heading + b.driftAngle * 0.45f;
            const float pa = sa * (std::sin(ha) * nx - std::cos(ha) * nz);     // a moving towards b
            const float pb = -sb * (std::sin(hb) * nx - std::cos(hb) * nz);    // b moving towards a
            const float closing = pa + pb;
            if (closing > 0.03f) {
                const float total = 110.0f * closing + 4.0f;
                const float fa = std::clamp(0.5f + 0.75f * (pa - pb) / closing, 0.0f, 1.0f);   // a's share of the blame
                if (state == Racing && fa > 0.6f && !a.finished) ++a.crashesCaused;
                if (state == Racing && fa < 0.4f && !b.finished) ++b.crashesCaused;
                damage(a, idxA, total * (0.15f + 0.70f * fa), idxB, fa > 0.6f, closing);
                damage(b, idxB, total * (0.15f + 0.70f * (1.0f - fa)), idxA, fa < 0.4f, closing);
            }
        }
    }

    SimEvent spk;
    spk.type = SimEvent::Sparks;
    spk.x = (a.x + b.x) * 0.5f; spk.y = 0.5f * (a.y + b.y) + 0.45f; spk.z = (a.z + b.z) * 0.5f;
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

// ---------------------------------------------------------------------------
//  Crashes: health, wrecks and coming back
// ---------------------------------------------------------------------------
float GameSim::positionDamage(int idx) const
{
    const int n = int(cars.size());
    if (n < 2 || state != Racing) return 1.0f;
    const float behind = float(rankOf(idx) - 1) / float(n - 1);   // 0 the leader .. 1 the last car
    return POS_DAMAGE_FRONT + (POS_DAMAGE_BACK - POS_DAMAGE_FRONT) * behind;
}

void GameSim::damage(Car &c, int idx, float amount, int other, bool atFault, float impact)
{
    amount *= DAMAGE_SCALE;                              // (every hit, one dial)
    // crashes cost the front runners more and the back markers less (barriers, obstacles,
    // other racers): from +25% for the leader to -20% for the last car
    if (other >= 0 || other == SimEvent::ByBarrier || other == SimEvent::ByObject || other == SimEvent::ByRampSide)
        amount *= positionDamage(idx);
    if (state != Racing || c.wreck > 0 || c.ghost > 0 || c.finished || amount < 0.5f) return;
    c.health -= amount;
    c.calm = 0;
    if (other >= 0 && other < MAX_RACERS) c.dmgFrom[other] += amount;
    if (RaceObs *ob = obsOf(idx)) {
        moodDamage(*ob, amount);
        if (other == SimEvent::ByBarrier) { ++ob->barrier; moodMistake(*ob, tick, 1.0f); }
        else if (other == SimEvent::ByObject || other == SimEvent::ByRampSide) { ++ob->objects; moodMistake(*ob, tick, 0.4f); }
        else if (other == SimEvent::ByTraffic && atFault) { ++ob->traffic; moodMistake(*ob, tick, 0.8f); }
        else if (other >= 0 && atFault) {                // it drove into another racer
            ++ob->rams;
            if (other < PM_CARS) ++ob->ramsOn[size_t(other)];
            if (isPerson(idx) && !isPerson(other)) taunt(other, TauntYouRammed, idx);
            else if (!isPerson(idx) && isPerson(other)) taunt(idx, TauntRammedYou, other);
            if (other == ob->lastHitBy && tick - ob->lastHitTick < 600) ++ob->revenge;
        } else if (other >= 0) {
            ob->lastHitBy = other;
            ob->lastHitTick = tick;
        }
    }
    SimEvent ev;
    ev.type = SimEvent::Damage;
    ev.carIndex = idx;
    ev.intensity = amount;
    ev.count = other;
    ev.highPitch = atFault;
    ev.x = c.x; ev.y = c.y + 2.9f; ev.z = c.z;
    events.push_back(ev);
    if (c.health > 0.0f) return;
    // wrecked: the driver is thrown out by a big hit, otherwise about every other time
    // (a hash of the tick and the car, so it is the same on every computer)
    const uint32_t h = (uint32_t(tick) * 2654435761u) ^ (uint32_t(idx + 1) * 2246822519u);
    healthGone(c, idx, other >= 0 && !atFault ? other : (other < -1 ? other : SimEvent::ByNone),
               impact > 0.45f || (h >> 8) % 100u < 45u);
}

// Health ran out. The car that did it the most damage since its last wreck gets the bounty;
// the car is wrecked and comes back with full health 2-3 s later.
void GameSim::healthGone(Car &c, int idx, int cause, bool eject)
{
    int best = -1;
    float most = 9.99f;
    for (int j = 0; j < int(cars.size()) && j < MAX_RACERS; ++j)
        if (j != idx && c.dmgFrom[j] > most && !cars[size_t(j)].out && cars[size_t(j)].wreck == 0) { most = c.dmgFrom[j]; best = j; }
    for (float &v : c.dmgFrom) v = 0.0f;
    if (best >= 0) {
        Car &b = cars[size_t(best)];
        b.health = std::min(100.0f, b.health + BOUNTY);
        ++b.bounties;
        SimEvent ev;
        ev.type = SimEvent::Bounty;
        ev.carIndex = best;
        ev.count = idx;
        ev.x = b.x; ev.y = b.y + 3.2f; ev.z = b.z;
        events.push_back(ev);
    }
    c.lastWreckProg = c.progress;
    startWreck(c, idx, cause, eject, true);
}

void GameSim::startWreck(Car &c, int idx, int cause, bool eject, bool takesLife)
{
    const uint32_t h = (uint32_t(tick) * 40503u) ^ (uint32_t(idx + 7) * 2654435761u);
    if (takesLife) c.health = 0.0f;
    c.lifeWreck = takesLife;
    if (RaceObs *ob = obsOf(idx); ob && state == Racing) { ++ob->wrecks; ob->secSpoiled = true; moodWreck(*ob, tick); }
    if (isPerson(idx) && state == Racing) {              // the nearest named driver has something to say
        int best = -1;
        float nearest = 150.0f;
        for (int j = 0; j < int(cars.size()); ++j) {
            if (isPerson(j) || cars[size_t(j)].rosterId < 0) continue;
            const float d = std::fabs(cars[size_t(j)].progress - c.progress) - (cars[size_t(j)].dirRivalOf == idx ? 100.0f : 0.0f);
            if (d < nearest) { nearest = d; best = j; }
        }
        if (best >= 0) taunt(best, TauntYouWrecked, idx);
    }
    c.boosting = false;
    c.boostHold = 0;
    c.outside = false;
    c.barrel = c.barrelRate = c.spinRate = c.jumpDanger = 0.0f;
    c.wreck = 1;
    ++c.wrecks;
    c.ejected = eject;
    c.wreckX = c.x; c.wreckZ = c.z;
    c.wreckVx = std::sin(c.heading) * c.speed;
    c.wreckVz = -std::cos(c.heading) * c.speed;
    c.wreckSpin = ((h >> 3) & 1u ? 1.0f : -1.0f) * float(1 + (h >> 5) % 2u);
    c.speed = 0.0f;
    c.nitroTimer = 0; c.nitroOn = false; c.nitroCharges = 0; c.nitro = 0.0f;   // a wreck loses its nitro
    SimEvent ev;
    ev.type = SimEvent::Wrecked;
    ev.carIndex = idx;
    ev.count = cause;
    ev.highPitch = eject;
    ev.x = c.x; ev.y = c.y + 0.6f; ev.z = c.z;
    events.push_back(ev);
    SimEvent deb;
    deb.type = SimEvent::Debris;
    deb.x = c.x; deb.y = c.y + 0.7f; deb.z = c.z;
    deb.r = c.r; deb.g = c.g; deb.b = c.b; deb.count = 18;
    deb.carIndex = idx;
    events.push_back(deb);
    SimEvent cr;
    cr.type = SimEvent::Crash;
    cr.intensity = 1.0f;
    cr.carIndex = idx;
    events.push_back(cr);
}

void GameSim::forceWreck(int idx, bool eject)
{
    if (idx < 0 || idx >= int(cars.size()) || cars[size_t(idx)].wreck > 0) return;
    startWreck(cars[size_t(idx)], idx, SimEvent::ByNone, eject, false);
}

// The wreck tumbles on and slides to the side of the road (out of the racing line), stops,
// and after its time is back in a lane near where it stopped, ghosting for a moment.
// Only the car's own state is used, so client prediction gives the same result.
void GameSim::wreckStep(Car &c, int idx)
{
    const int t = c.out ? std::min(c.wreck + 1, 30000) : ++c.wreck;
    c.wreck = t;
    if (t <= WRECK_SLIDE) {
        const float f = std::pow(0.955f, float(t));      // sliding, slower and slower
        c.x += c.wreckVx * f;
        c.z += c.wreckVz * f;
        locate(c);
        const float edge = std::max(0.0f, roadHalfAt(c.s, c.path) - 1.6f);
        moveOnTrack(c, 0.0f, ((c.d >= 0.0f ? edge : -edge) - c.d) * 0.03f);
        c.y = heightAt(c.s, c.d, c.path);
        c.vy = 0.0f;
        c.air = 0;
    }
    if (t == WRECK_SLIDE) c.heading = headingAt(c.s, c.path);   // it ends upright, along the road
    c.speed = 0.0f;
    c.roll = c.pitch = c.driftAngle = c.steerVis = 0.0f;
    c.isDrifting = false;
    c.nitroOn = false;
    if (t >= wreckTime(c) && !c.out) {                   // (an eliminated car stays where it is)
        const float half = std::max(0.0f, roadHalfAt(c.s, c.path) - 2.0f);
        moveOnTrack(c, 0.0f, std::clamp(c.d, -half, half) - c.d);
        c.heading = headingAt(c.s, c.path);
        c.y = heightAt(c.s, c.d, c.path);
        c.wreck = 0;
        c.ejected = false;
        if (c.lifeWreck) c.health = 100.0f;              // (a wreck off a jump keeps what health was left)
        c.lifeWreck = false;
        c.ghost = GHOST_TIME;
        c.calm = 0;
        c.speed = 0.3f;                                  // a rolling start
        c.aiCheckTimer = 0; c.aiCheckProg = c.progress; c.aiReverse = 0;
        SimEvent ev;
        ev.type = SimEvent::Respawn;
        ev.carIndex = idx;
        ev.x = c.x; ev.y = c.y; ev.z = c.z;
        events.push_back(ev);
    }
}

std::string GameSim::worldParts() const
{
    auto q = [](float v) { return long(std::lround(v * 10.0f)); };
    struct H { uint32_t h = 2166136261u; void mix(long v) { h = (h ^ uint32_t(v)) * 16777619u; } };
    H heights, obst, nitro, traf, side, lapsFrom;
    for (size_t i = 0; i < track.slices().size(); i += 5) heights.mix(q(heightAt(float(i), 0.0f)));
    for (const Obstacle &o : obstacles) { obst.mix(o.type); obst.mix(q(o.x)); obst.mix(q(o.z)); obst.mix(q(o.hx)); lapsFrom.mix(o.fromLap); }
    for (const NitroPickup &n : nitroPickups) { nitro.mix(q(n.x)); nitro.mix(q(n.z)); }
    for (const Traffic &t : traffic) { traf.mix(q(t.s)); traf.mix(q(t.lane)); traf.mix(t.truck ? 1 : 0); }
    for (const SideRamp &r : sideList) { side.mix(q(r.s)); side.mix(q(r.d)); side.mix(q(r.height)); side.mix(long(r.twist)); side.mix(r.fromLap); }
    char buf[400];
    std::snprintf(buf, sizeof buf, "map %d lanes %d slices %zu cut %zu | heights %08x | obstacles %zu %08x | nitro %zu %08x | traffic %zu %08x | props %zu | side %zu %08x | fromLap %08x | total %08x",
                  mapIndex, lanes, track.slices().size(), track.shortcut().size(), heights.h, obstacles.size(), obst.h,
                  nitroPickups.size(), nitro.h, traffic.size(), traf.h, props.size(), sideList.size(), side.h, lapsFrom.h, worldChecksum());
    return buf;
}

uint32_t GameSim::worldChecksum() const
{
    // positions rounded to 10 cm, so tiny maths-library differences between systems don't matter
    uint32_t h = 2166136261u;
    auto mix = [&h](long v) { h = (h ^ uint32_t(v)) * 16777619u; };
    auto q = [](float v) { return long(std::lround(v * 10.0f)); };
    mix(mapIndex);
    mix(lanes);
    mix(long(track.slices().size()));
    mix(long(track.shortcut().size()));
    for (size_t i = 0; i < track.slices().size(); i += 5) mix(q(heightAt(float(i), 0.0f)));
    mix(long(obstacles.size()));
    for (const Obstacle &o : obstacles) { mix(o.type); mix(q(o.x)); mix(q(o.z)); mix(q(o.hx)); }
    mix(long(nitroPickups.size()));
    for (const NitroPickup &n : nitroPickups) { mix(q(n.x)); mix(q(n.z)); }
    mix(long(traffic.size()));
    for (const Traffic &t : traffic) { mix(q(t.s)); mix(q(t.lane)); mix(t.truck ? 1 : 0); }
    mix(long(props.size()));
    mix(long(sideList.size()));
    for (const SideRamp &r : sideList) { mix(q(r.s)); mix(q(r.d)); mix(q(r.height)); mix(long(r.twist)); mix(r.fromLap); }
    for (const Obstacle &o : obstacles) mix(o.fromLap);
    return h;
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
        if (o.out != me.out) ahead = me.out;                  // eliminated cars come last
        else if (o.finished && me.finished) ahead = o.finishTime < me.finishTime;
        else if (o.finished != me.finished) ahead = o.finished;
        else ahead = o.progress > me.progress;            // laps and distance raced
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
        float vals[] = { tr.s, tr.lane, tr.speed };
        hashBytes(vals, sizeof(vals));
    }
    for (const auto &np : nitroPickups) {
        bool act = np.active;
        hashBytes(&act, sizeof(act));
    }

    return hash;
}

} // namespace sim
