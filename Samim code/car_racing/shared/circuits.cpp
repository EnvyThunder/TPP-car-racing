// The maps (step 1 of docs/ROADMAP.md): four circuits raced in laps and four sprints (Sunset
// Valley, Coastal Highway, Desert Zigzag, Ghat Road) raced once from start to finish. tools/track_preview
// draws each one from above with its height profile and measures it in the game's simulation.
// Top view in metres: x east, z north; y = height. Point 0 is the start/finish line; it
// stands on a straight (the points just before and after it lie on one line).
#include "track.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

CtrlPoint P(float x, float z, float y, uint16_t flags = 0, float bank = 0.0f, float width = 1.0f)
{
    CtrlPoint p;
    p.x = x; p.z = z; p.y = y; p.flags = flags; p.bank = bank; p.width = width;
    return p;
}

// shortcuts: narrower, rough surface
CtrlPoint D(float x, float z, float y) { return P(x, z, y, TF_DIRT, 0.0f, 0.5f); }

CircuitDef canyonRun()
{
    const float K = 0.96f;      // whole layout a little smaller
    auto C = [K](float x, float z, float y, uint16_t f = 0, float bank = 0.0f) { return P(x * K, z * K, y, f, bank); };
    auto CD = [K](float x, float z, float y) { return D(x * K, z * K, y); };
    CircuitDef c;
    c.name = "CANYON RUN";
    c.features = "MEDIUM. FAST SWEEPERS ON THE CANYON FLOOR, A BANKED HAIRPIN CLIMBING TO A PLATEAU, A TUNNEL THROUGH A MESA, A RAMP JUMP OVER A DRY RIVERBED. SHORTCUT: SLOT CANYON.";
    c.pts = {
        C(110, 0, 0), C(220, 0, 0), C(330, 0, 0), C(600, -40, 0), C(880, 20, 1),
        C(1150, -30, 2), C(1380, 40, 4), C(1485, 195, 8, 0, -8), C(1410, 338, 14, 0, -8), C(1230, 360, 20),
        C(1030, 330, 24), C(800, 360, 25, TF_TUNNEL), C(560, 350, 25), C(330, 400, 22), C(120, 360, 16, TF_JUMP),
        C(-60, 270, 8), C(-150, 150, 4), C(-120, 40, 1), C(-50, 0, 0), C(30, 0, 0),
    };
    c.shortcut.fromPt = 6;
    c.shortcut.toPt = 9;
    c.shortcut.via = {CD(1400, 110, 6), CD(1370, 200, 11), CD(1310, 285, 15)};
    c.rampLipDeg = 9.0f;
    return c;
}

CircuitDef alpinePass()
{
    const float K = 1.08f;      // whole layout a little bigger
    auto A = [K](float x, float z, float y, uint16_t f = 0, float bank = 0.0f) { return P(x * K, z * K, y, f, bank); };
    auto AD = [K](float x, float z, float y) { return D(x * K, z * K, y); };
    CircuitDef c;
    c.name = "ALPINE PASS";
    c.features = "HARD. SWITCHBACKS CLIMBING 60 M, AN AVALANCHE GALLERY TUNNEL, A STONE BRIDGE OVER A GORGE, A SKI-JUMP CREST. SHORTCUT: STEEP FOREST TRACK.";
    c.pts = {
        A(140, 0, 0), A(240, 0, 1), A(330, 4, 3), A(440, 55, 8), A(475, 115, 13),
        A(455, 175, 17), A(380, 195, 21), A(230, 230, 27), A(190, 285, 31), A(215, 340, 34),
        A(300, 365, 38), A(520, 400, 47), A(700, 480, 55), A(760, 620, 60, TF_TUNNEL), A(650, 740, 58),
        A(480, 760, 52, TF_BRIDGE), A(330, 730, 45), A(180, 650, 36, TF_JUMP), A(60, 540, 24), A(-60, 400, 14),
        A(-100, 250, 6), A(-85, 110, 2), A(-20, 0, 0), A(60, 0, 0),
    };
    c.shortcut.fromPt = 12;
    c.shortcut.toPt = 15;
    c.shortcut.via = {AD(722, 548, 57), AD(680, 640, 57), AD(580, 715, 55)};
    c.rampLipDeg = 7.0f;
    return c;
}

// A right-angle city corner: an even arc of radius r between two straight streets.
// (inx, inz) / (outx, outz) = direction of the street before / after the corner.
void corner(std::vector<CtrlPoint> &pts, float cx, float cz, float inx, float inz, float outx, float outz,
            float y, uint16_t flags = 0, float r = 30.0f, float bank = 0.0f)
{
    const float k = 0.2929f * r;    // the arc's middle is r (1 - 1/sqrt 2) in from the corner
    pts.push_back(P(cx - inx * 2.2f * r, cz - inz * 2.2f * r, y, flags));
    pts.push_back(P(cx - inx * r, cz - inz * r, y, flags, bank));
    pts.push_back(P(cx + (outx - inx) * k, cz + (outz - inz) * k, y, flags, bank));
    pts.push_back(P(cx + outx * r, cz + outz * r, y, flags, bank));
    pts.push_back(P(cx + outx * 2.2f * r, cz + outz * 2.2f * r, y, flags));
}

CircuitDef neonCity()
{
    CircuitDef c;
    c.name = "NEON CITY";
    c.features = "HARD. RIGHT-ANGLE STREET CORNERS, A RAISED HIGHWAY THAT CROSSES OVER THE START STREET, AN UNDERGROUND TUNNEL, A RAMP JUMP ACROSS A JUNCTION. SHORTCUT: THROUGH A CAR PARK.";
    const uint16_t UP = TF_OVERPASS;
    std::vector<CtrlPoint> &p = c.pts;
    p.push_back(P(150, 0, 0));
    p.push_back(P(400, 0, 0));                         // level where the highway crosses
    p.push_back(P(560, 0, 0));                         // street dips into the underpass
    corner(p, 800, 0, 1, 0, 0, 1, -4, TF_TUNNEL);
    corner(p, 800, 300, 0, 1, -1, 0, -4, TF_TUNNEL);
    p.back().flags = 0;                                // out of the tunnel
    corner(p, 600, 300, -1, 0, 0, 1, -1);
    corner(p, 600, 600, 0, 1, -1, 0, 0);
    p.push_back(P(470, 600, 0, TF_JUMP));              // ramp across the junction
    corner(p, 300, 600, -1, 0, 0, -1, 0, 0, 30.0f, -5.0f);   // banked on-ramp
    p.back().flags = UP;                               // up onto the raised highway...
    p.push_back(P(300, 470, 0.5f, UP));
    p.push_back(P(300, 160, 9, UP));                   // ...over the start street at 9 m
    p.push_back(P(300, -120, 9, UP));
    corner(p, 300, -350, 0, -1, -1, 0, 0);
    p.push_back(P(100, -350, 0));                      // car park entrance
    const int carParkIn = int(p.size()) - 1;
    corner(p, -100, -350, -1, 0, 0, 1, 0);
    p.push_back(P(-100, -150, 0));                     // car park exit
    const int carParkOut = int(p.size()) - 1;
    corner(p, -100, 0, 0, 1, 1, 0, 0);
    // the car park lane: one wide sweep across the corner, leaving and joining at a shallow angle
    c.shortcut.fromPt = carParkIn;
    c.shortcut.toPt = carParkOut;
    c.shortcut.via = {D(55, -326, 0), D(-42, -238, 0), D(-76, -205, 0)};
    c.rampLipDeg = 9.0f;
    return c;
}

// The long one: a 5 km lap that crosses its river twice on low bridges, climbs to a banked
// hairpin, winds once right round a spiral that climbs over its own entry, and comes home down
// long hills to a second hairpin before the start. Its straights carry most of the side ramps.
CircuitDef riverSerpent()
{
    CircuitDef c;
    c.name = "RIVER SERPENT";
    c.features = "HARD. A 5 KM LAP: TWO RIVER BRIDGES, A BANKED HAIRPIN CLIMBING THE RIDGE, A 360 SPIRAL THAT CROSSES OVER ITSELF, LONG HILLS, A SECOND HAIRPIN, A RAMP JUMP.";
    const uint16_t UP = TF_OVERPASS, BR = TF_BRIDGE;
    c.pts = {
        P(100, 0, 0), P(300, 0, 1), P(550, 0, 6), P(820, -20, 8), P(1060, -5, 4),
        P(1200, 10, 2, BR), P(1420, 10, 2),                               // over the river
        P(1640, 60, 6), P(1800, 200, 11),
        P(1840, 320, 14, 0, -8), P(1818, 373, 16, 0, -8), P(1765, 395, 17, 0, -8),   // the hairpin, banked
        P(1712, 373, 18, 0, -8), P(1690, 320, 18, 0, -8),
        P(1680, 235, 17), P(1600, 180, 13), P(1500, 172, 8), P(1450, 172, 4),
        P(1420, 172, 2, BR), P(1200, 172, 2),                             // back over the river
        P(1110, 172, 3), P(1000, 176, 5),
        P(900, 215, 7, 0, -4), P(860, 300, 9, 0, -4), P(930, 372, 11, 0, -4), P(1030, 345, 13, 0, -4),   // the spiral...
        P(1052, 255, 14, UP), P(1035, 130, 14),                           // ...over its own entry
        P(965, 80, 13), P(800, 70, 12), P(560, 82, 10, TF_JUMP), P(320, 100, 7),
        P(110, 140, 5), P(-90, 128, 3),
        P(-230, 128, 1, 0, 6), P(-277, 109, 0, 0, 6), P(-296, 62, 0, 0, 6),   // the second hairpin
        P(-277, 15, 0, 0, 6), P(-230, -3, 0, 0, 6),
        P(-130, 0, 0), P(0, 0, 0),
    };
    c.rampLipDeg = 6.0f;
    return c;
}

// ---- Sprints: one long road from start to finish ----
// The road of the very first version of the game: a straight with gentle S-bends (its
// roadCenter: 18 sin(0.012 d) + 8 sin(0.031 d + 1.3) metres to the side, eased in after the
// start and out before the finish), here 8.2 km long with the map's hills and structures.
// Point 0 is where the road begins (150 m before the start line), the last point the end of
// the 300 m run-off past the finish.

struct SprintMark { float d, y; uint16_t flags; };   // a height (and the stretch's markers) d metres along

// north: the road runs north (else east); heights follow a smooth curve through the marks;
// straightAt: a place kept straight (a jump), easing back into the bends 120-420 m away
CircuitDef sprintRoad(const char *name, const char *features, bool north, const std::vector<SprintMark> &marks,
                      float straightAt = -1.0f)
{
    CircuitDef c;
    c.name = name;
    c.features = features;
    c.sprint = true;
    c.laps = 1;
    const float START = 150.0f, FINISH = START + 8200.0f, END = FINISH + 300.0f;
    // a smooth curve through the heights (cubic Hermite, each mark's slope from its neighbours)
    auto slope = [&](size_t k) {
        const SprintMark &p = marks[k > 0 ? k - 1 : k], &q = marks[k + 1 < marks.size() ? k + 1 : k];
        return (q.y - p.y) / std::max(1.0f, q.d - p.d);
    };
    auto height = [&](float d, uint16_t &flags) {
        size_t k = 0;
        while (k + 2 < marks.size() && marks[k + 1].d <= d) ++k;
        const SprintMark &a = marks[k], &b = marks[k + 1];
        flags = a.flags;
        const float len = std::max(1.0f, b.d - a.d), f = std::clamp((d - a.d) / len, 0.0f, 1.0f);
        const float f2 = f * f, f3 = f2 * f;
        return (2 * f3 - 3 * f2 + 1) * a.y + (f3 - 2 * f2 + f) * len * slope(k)
             + (-2 * f3 + 3 * f2) * b.y + (f3 - f2) * len * slope(k + 1);
    };
    auto wiggle = [&](float d) {
        const float r = d - START;                                      // the first game's d
        const float ramp = std::clamp((r - 80.0f) / 200.0f, 0.0f, 1.0f);
        const float endRamp = std::clamp((FINISH - START + 60.0f - r) / 200.0f, 0.0f, 1.0f);
        float k = ramp * endRamp;
        if (straightAt >= 0.0f) {
            const float u = std::clamp((std::fabs(d - straightAt) - 120.0f) / 300.0f, 0.0f, 1.0f);
            k *= u * u * (3.0f - 2.0f * u);
        }
        return k * (18.0f * std::sin(r * 0.012f) + 8.0f * std::sin(r * 0.031f + 1.3f));
    };
    std::vector<float> ds = {0.0f};
    for (float d = START; d < END - 1.0f; d += 25.0f) ds.push_back(d);
    ds.push_back(END);
    for (float d : ds) {
        uint16_t flags = 0;
        const float y = height(d, flags);
        const float w = wiggle(d);
        c.pts.push_back(north ? P(-w, d, y, flags) : P(d, w, y, flags));
    }
    return c;
}

CircuitDef sunsetSprint()
{
    // the crest jump's lip is the point at 3400 m (a marker flags the stretch from a point on)
    CircuitDef c = sprintRoad("SUNSET VALLEY",
                      "EASY SPRINT. THE FIRST GAME'S ROAD THROUGH ROLLING FARMLAND, A CREST JUMP, A WOODEN BRIDGE OVER THE RIVER, A VILLAGE. STEADY TRAFFIC.",
                      false,
                      {{0, 0, 0}, {550, 0, 0}, {1050, 2, 0}, {1650, 6, 0}, {2300, 12, 0}, {2900, 18, 0},
                       {3400, 25, TF_JUMP}, {3425, 24.6f, 0}, {3700, 19, 0}, {4000, 12, 0}, {4600, 5, 0},
                       {5000, 2, TF_BRIDGE}, {5200, 2, 0}, {5800, 8, 0}, {6450, 14, 0}, {7050, 10, 0},
                       {7550, 5, 0}, {8000, 2, 0}, {8350, 0, 0}, {8650, 0, 0}},
                      3400.0f);
    c.rampLipDeg = 5.0f;
    return c;
}

CircuitDef coastalSprint()
{
    CircuitDef c = sprintRoad("COASTAL HIGHWAY",
                      "FAST SPRINT. THE FIRST GAME'S ROAD NORTH WITH THE SEA ALONGSIDE, A CLIFF-TOP STRETCH, A TUNNEL UNDER THE LIGHTHOUSE, A HARBOUR BRIDGE. STEADY TRAFFIC.",
                      true,
                      {{0, 2, 0}, {550, 3, 0}, {1050, 4, 0}, {1650, 6, 0}, {2250, 10, 0}, {2750, 20, 0},
                       {3150, 28, 0}, {3650, 32, 0}, {4050, 30, TF_TUNNEL}, {4350, 22, 0}, {4750, 12, 0},
                       {5300, 5, 0}, {5700, 3, TF_BRIDGE}, {6000, 3, 0}, {6500, 4, 0}, {7100, 6, 0},
                       {7600, 4, 0}, {8000, 3, 0}, {8350, 2, 0}, {8650, 2, 0}});
    c.rampLipDeg = 0.0f;                                 // (no jumps)
    return c;
}


// ---- Hairpin sprints: a mountain road of stacked switchbacks ----
// Rows of road stacked up a slope, each joined to the next by a hairpin. The first row starts at
// (x, z) heading east (dir +1) or west (-1); each next row lies `gap` metres further north and runs
// the other way. A row climbs `climb` metres, a hairpin `turnClimb`; `wave` bends the rows a
// little (as such roads follow the hillside), `slant` tilts them (the row's far end that many
// metres further north). Returns the height at the end.
float switchbacks(std::vector<CtrlPoint> &p, float x, float z, float y, float dir, float rowLen, int rows,
                  float gap, float climb, float turnClimb, float wave, float slant)
{
    const float r = gap * 0.5f;
    const int steps = std::max(2, int(rowLen / 70.0f));
    for (int k = 0; k < rows; ++k) {
        const float d = (k % 2 == 0) ? dir : -dir;
        // the row's start: the west or east end, depending on its direction
        const float startX = (d == dir) ? x : x + dir * rowLen;
        const float baseZ = z + float(k) * gap;
        for (int i = (k == 0 ? 0 : 1); i <= steps; ++i) {
            const float f = float(i) / float(steps);
            const float px = startX + d * rowLen * f;
            const float tilt = slant * ((d == dir) ? f : 1.0f - f);   // (every row parallel)
            const float pz = baseZ + tilt + wave * std::sin(f * 3.14159265f * 2.0f) * (k % 2 ? -1.0f : 1.0f);
            p.push_back(P(px, pz, y + climb * f));
        }
        y += climb;
        if (k + 1 == rows) break;
        // the hairpin: half a circle north, round its centre beside the row's end
        const float ex = startX + d * rowLen, ez = baseZ + ((d == dir) ? slant : 0.0f);
        const float bank = (d > 0) ? -8.0f : 8.0f;       // (left turn: the left edge lower)
        for (int a = 1; a <= 3; ++a) {
            const float th = -1.5707963f + 0.7853982f * float(a);
            p.push_back(P(ex + d * r * std::cos(th), ez + r + r * std::sin(th), y + turnClimb * float(a) / 4.0f, 0, bank));
        }
        y += turnClimb;
    }
    return y;
}

// DESERT ZIGZAG: across the desert to the foot of a ridge, up nine stacked switchbacks, through
// a mesa at the top and down a canyon to the finish
CircuitDef desertZigzag()
{
    CircuitDef c;
    c.name = "DESERT ZIGZAG";
    c.features = "HARD SPRINT. ACROSS THE DESERT WITH A JUMP OVER A DRY WASH, NINE STACKED HAIRPINS UP THE RIDGE, A TUNNEL THROUGH THE MESA AT THE TOP, A BRIDGE OVER A CANYON, A CREST JUMP. SHORTCUT: A DRY RIVERBED.";
    c.sprint = true;
    c.laps = 1;
    std::vector<CtrlPoint> &p = c.pts;
    p = {P(0, 0, 0), P(150, 0, 0), P(300, 0, 0), P(450, 0, 0), P(700, -30, 1), P(950, 15, 2),
         P(1150, 0, 3), P(1250, 0, 3, TF_JUMP), P(1400, 0, 3), P(1550, 10, 3), P(1700, 60, 4), P(1810, 140, 4)};
    float y = switchbacks(p, 1880, 190, 4, 1.0f, 420.0f, 9, 80.0f, 8.0f, 1.0f, 5.0f, 0.0f);
    const float zt = 190 + 8 * 80.0f;                   // the top row
    p.push_back(P(2420, zt + 5, y));
    p.push_back(P(2560, zt + 10, y, TF_TUNNEL));         // through the mesa
    p.push_back(P(2760, zt + 10, y - 2));
    p.push_back(P(2920, zt - 15, y - 6));
    p.push_back(P(3060, zt - 90, y - 13));
    p.push_back(P(3160, zt - 210, y - 21));
    p.push_back(P(3210, zt - 340, y - 28, TF_BRIDGE));   // over the canyon
    p.push_back(P(3240, zt - 470, y - 32));
    p.push_back(P(3290, zt - 600, y - 37));
    p.push_back(P(3330, zt - 720, y - 41));
    const int cutFrom = int(p.size());
    p.push_back(P(3420, zt - 850, y - 45));
    p.push_back(P(3470, zt - 1040, y - 47));            // round the rock...
    p.push_back(P(3560, zt - 1180, y - 49));
    p.push_back(P(3700, zt - 1220, y - 51));
    p.push_back(P(3830, zt - 1150, y - 52));
    p.push_back(P(3880, zt - 1010, y - 53));
    p.push_back(P(3930, zt - 880, y - 54));
    const int cutTo = int(p.size());
    p.push_back(P(3990, zt - 780, y - 55));
    p.push_back(P(4030, zt - 700, y - 58));
    p.push_back(P(4150, zt - 560, y - 62));
    p.push_back(P(4350, zt - 540, y - 64, TF_JUMP));     // the crest
    p.push_back(P(4500, zt - 540, y - 65));
    p.push_back(P(4750, zt - 550, y - 67));
    p.push_back(P(5000, zt - 560, y - 69));               // the finish line
    p.push_back(P(5300, zt - 560, y - 69));               // run-off
    c.shortcut.fromPt = cutFrom;
    c.shortcut.toPt = cutTo;
    c.shortcut.via = {D(3540, zt - 885, y - 48), D(3680, zt - 900, y - 51), D(3810, zt - 880, y - 53)};
    c.rampLipDeg = 7.0f;
    return c;
}

// GHAT ROAD: up a valley through the jungle and over its river, eight hairpins up the
// hillside, a spiral on the cliff that climbs over its own road, a tunnel through the rock and
// along the misty ridge to the finish
CircuitDef ghatRoad()
{
    CircuitDef c;
    c.name = "GHAT ROAD";
    c.features = "HARD SPRINT. A JUNGLE VALLEY AND A RIVER BRIDGE, EIGHT HAIRPINS UP THE HILLSIDE, A SPIRAL ON THE CLIFF THAT CROSSES OVER ITSELF, A TUNNEL THROUGH THE ROCK, A RAMP JUMP ON THE RIDGE. SHORTCUT: THROUGH A TEA ESTATE.";
    c.sprint = true;
    c.laps = 1;
    std::vector<CtrlPoint> &p = c.pts;
    p = {P(0, 0, 0), P(0, 150, 0), P(0, 300, 0), P(0, 450, 0), P(25, 700, 1), P(-30, 950, 2),
         P(-10, 1150, 2, TF_BRIDGE), P(0, 1330, 2),      // over the river
         P(10, 1480, 3), P(30, 1590, 4)};
    float y = switchbacks(p, 70, 1660, 4, 1.0f, 330.0f, 8, 76.0f, 11.0f, 1.5f, 6.0f, 0.0f);
    const float zt = 1660 + 7 * 76.0f;                   // the top row, heading west
    // the spiral: right round, climbing, over its own entry (River Serpent's, moved here)
    const float sx = -1250.0f, sz = zt - 176.0f;
    p.push_back(P(-90, zt, y));
    p.push_back(P(1110 + sx, 172 + sz, y + 0.5f));
    p.push_back(P(1000 + sx, 176 + sz, y + 1));
    p.push_back(P(900 + sx, 215 + sz, y + 3, 0, -4));
    p.push_back(P(860 + sx, 300 + sz, y + 5, 0, -4));
    p.push_back(P(930 + sx, 372 + sz, y + 7, 0, -4));
    p.push_back(P(1030 + sx, 345 + sz, y + 9, 0, -4));
    p.push_back(P(1052 + sx, 255 + sz, y + 10, TF_OVERPASS));
    p.push_back(P(1035 + sx, 130 + sz, y + 10));
    p.push_back(P(965 + sx, 80 + sz, y + 10));
    p.push_back(P(800 + sx, 70 + sz, y + 10));
    const float ye = y + 10, ze = 70 + sz;
    p.push_back(P(-650, ze - 10, ye, TF_TUNNEL));         // through the rock
    p.push_back(P(-850, ze - 20, ye));
    p.push_back(P(-1050, ze - 50, ye - 2));
    p.push_back(P(-1250, ze - 100, ye - 4));
    p.push_back(P(-1400, ze - 140, ye - 6));
    p.push_back(P(-1500, ze - 160, ye - 7, TF_JUMP));     // the ramp on the ridge
    p.push_back(P(-1650, ze - 185, ye - 8));
    p.push_back(P(-1800, ze - 200, ye - 10));
    const int cutFrom = int(p.size());
    p.push_back(P(-2000, ze - 230, ye - 12));
    p.push_back(P(-2200, ze - 300, ye - 13));            // round the tea estate...
    p.push_back(P(-2330, ze - 470, ye - 15));
    p.push_back(P(-2260, ze - 650, ye - 17));
    const int cutTo = int(p.size());
    p.push_back(P(-2080, ze - 700, ye - 18));
    p.push_back(P(-1950, ze - 720, ye - 20));
    p.push_back(P(-1780, ze - 735, ye - 22));
    p.push_back(P(-1600, ze - 745, ye - 23));
    p.push_back(P(-1400, ze - 750, ye - 24));             // the finish line
    p.push_back(P(-1100, ze - 750, ye - 24));             // run-off
    c.shortcut.fromPt = cutFrom;
    c.shortcut.toPt = cutTo;
    c.shortcut.via = {D(-2110, ze - 320, ye - 13), D(-2150, ze - 450, ye - 15), D(-2150, ze - 580, ye - 17)};
    c.rampLipDeg = 7.0f;
    return c;
}

} // namespace

const std::vector<CircuitDef> &draftCircuits()
{
    static const std::vector<CircuitDef> all = {sunsetSprint(), canyonRun(), alpinePass(), neonCity(), coastalSprint(),
                                                  riverSerpent(), desertZigzag(), ghatRoad()};
    return all;
}

} // namespace sim
