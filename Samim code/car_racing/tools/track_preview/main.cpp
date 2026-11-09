// Layout pictures for the circuits: the road from above (coloured by height) and the
// height along the lap, plus the numbers that matter and the automatic checks.
// Everything is drawn into a pixel buffer with our own raster routines (scan-line
// polygon fill, Bresenham lines, filled circles, the game's 5x7 pixel font).
// The lap times come from the game's own simulation (shared/gamesim.cpp): one car alone,
// driven by the CPU's driving logic at normal speed, no traffic.
#include "track.h"
#include "gamesim.h"
#include "pixelfont.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
//  A tiny raster canvas
// ---------------------------------------------------------------------------
struct Canvas {
    int w, h;
    std::vector<uint32_t> px;
    int clipX0 = 0, clipY0 = 0, clipX1 = 1 << 30, clipY1 = 1 << 30;   // drawing stays inside this box
    Canvas(int w_, int h_, uint32_t bg) : w(w_), h(h_), px(size_t(w_) * h_, bg) {}

    void clip(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
    void put(int x, int y, uint32_t c, float a = 1.0f) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        if (x < clipX0 || y < clipY0 || x >= clipX1 || y >= clipY1) return;
        uint32_t &d = px[size_t(y) * w + x];
        if (a >= 1.0f) { d = c; return; }
        auto mix = [a](uint32_t s, uint32_t t, int sh) {
            float v = float((s >> sh) & 255) * a + float((t >> sh) & 255) * (1.0f - a);
            return uint32_t(std::clamp(v, 0.0f, 255.0f)) << sh;
        };
        d = 0xFF000000u | mix(c, d, 16) | mix(c, d, 8) | mix(c, d, 0);
    }
    void rect(int x0, int y0, int x1, int y1, uint32_t c, float a = 1.0f) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) put(x, y, c, a);
    }
    // Bresenham line
    void line(int x0, int y0, int x1, int y1, uint32_t c, float a = 1.0f) {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            put(x0, y0, c, a);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    // Scan-line fill of any simple polygon (even-odd rule), sampled at pixel centres
    void polygon(const std::vector<float> &xs, const std::vector<float> &ys, uint32_t c, float a = 1.0f) {
        const int n = int(xs.size());
        float ymin = ys[0], ymax = ys[0];
        for (float y : ys) { ymin = std::min(ymin, y); ymax = std::max(ymax, y); }
        std::vector<float> cross;
        for (int y = std::max(0, int(std::floor(ymin))); y <= std::min(h - 1, int(std::ceil(ymax))); ++y) {
            const float yc = y + 0.5f;
            cross.clear();
            for (int i = 0; i < n; ++i) {
                float ya = ys[i], yb = ys[(i + 1) % n];
                if ((ya <= yc && yb > yc) || (yb <= yc && ya > yc))
                    cross.push_back(xs[i] + (yc - ya) / (yb - ya) * (xs[(i + 1) % n] - xs[i]));
            }
            std::sort(cross.begin(), cross.end());
            for (size_t k = 0; k + 1 < cross.size(); k += 2)
                for (int x = int(std::ceil(cross[k] - 0.5f)); x <= int(std::floor(cross[k + 1] - 0.5f)); ++x) put(x, y, c, a);
        }
    }
    void circle(int cx, int cy, int r, uint32_t c, float a = 1.0f) {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r) put(cx + x, cy + y, c, a);
    }
    // the game's 5x7 pixel font, scaled by k
    int text(int x, int y, const std::string &s, uint32_t c, int k = 2) {
        for (char ch : s) {
            if (const unsigned char *rows = pixelGlyph(ch))
                for (int ry = 0; ry < 7; ++ry)
                    for (int rx = 0; rx < 5; ++rx)
                        if (rows[ry] & (0x10 >> rx)) rect(x + rx * k, y + ry * k, x + rx * k + k, y + ry * k + k, c);
            x += 6 * k;
        }
        return x;
    }
    // with a dark shadow, readable on any ground colour
    void label(int x, int y, const std::string &s, uint32_t c, int k = 2) {
        text(x + k, y + k, s, 0xFF000000u, k);
        text(x, y, s, c, k);
    }
    static int textWidth(const std::string &s, int k) { return int(s.size()) * 6 * k - k; }

    bool save(const QString &path) const {
        QImage img(reinterpret_cast<const uchar *>(px.data()), w, h, w * 4, QImage::Format_RGB32);
        return img.save(path);
    }
};

uint32_t rgb(int r, int g, int b) { return 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b); }

// height colour ramp: low = deep blue, then teal, green, yellow, high = red
uint32_t heightColour(float t)
{
    static const int ramp[5][3] = {{40, 70, 170}, {30, 160, 170}, {70, 175, 70}, {225, 200, 60}, {210, 70, 50}};
    t = std::clamp(t, 0.0f, 1.0f) * 4.0f;
    int i = std::min(3, int(t));
    float f = t - i;
    return rgb(int(ramp[i][0] + (ramp[i + 1][0] - ramp[i][0]) * f), int(ramp[i][1] + (ramp[i + 1][1] - ramp[i][1]) * f),
               int(ramp[i][2] + (ramp[i + 1][2] - ramp[i][2]) * f));
}

std::string num(float v, int decimals = 0)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, double(v));
    return b;
}

// ---------------------------------------------------------------------------
//  One layout picture
// ---------------------------------------------------------------------------
const float MIN_RADIUS = 18.0f;    // our car still makes the turn after braking hard

// One lap alone in the game's simulation, no traffic (cut: take the shortcut or not). With
// seed 0 a clean lap without cones or barrels; with another seed the obstacles that seed
// puts on the road (the game always uses seed 2024, so its obstacles move whenever a layout
// changes: the pictures average 40 seeds). Also the longest flight off a jump (seconds in
// the air, metres travelled).
float lapAlone(int map, bool cut, uint32_t seed, float *air = nullptr, float *flight = nullptr)
{
    sim::GameSim s;
    s.setMap(map);
    s.setLaps(1);
    s.startRace(sim::GameSim::ModeSingle, 1);
    if (seed) s.reset(seed, 1);
    s.setState(sim::GameSim::Racing);
    s.trafficMut().clear();
    // (the game has no setter for the obstacles; the tool takes them away for a clean lap)
    if (!seed) const_cast<std::vector<sim::Obstacle> &>(s.getObstacles()).clear();
    sim::Car &c = s.getCars()[0];
    float fx = 0.0f, fz = 0.0f;
    for (int t = 0; t < 60 * 200 && !c.finished; ++t) {
        c.cutPlan = cut;
        c.cutPlanLap = c.lap;
        sim::PlayerInput in = s.computeAIInput(c, 0, false);
        const int airBefore = c.air;
        s.step(&in, 1);
        if (airBefore == 0 && c.air > 0) { fx = c.x; fz = c.z; }
        for (const sim::SimEvent &e : s.getEvents())
            if (e.type == sim::SimEvent::Landing && air && e.count / 60.0f > *air) {
                *air = e.count / 60.0f;
                *flight = std::sqrt((c.x - fx) * (c.x - fx) + (c.z - fz) * (c.z - fz));
            }
        s.clearEvents();
    }
    return c.finished ? c.finishTime : -1.0f;
}

// Lap times: clean, and the average of 40 obstacle seeds (the game's own seed 2024 and 39
// others), with and without the shortcut. A lap where the car got stuck (not finished in
// 200 s) is left out of the average and counted.
struct Measured { float clean = 0, lapMain = 0, lapCut = 0, air = 0, flight = 0; int stuck = 0; };

Measured measure(int map, bool hasShortcut)
{
    Measured m;
    m.clean = lapAlone(map, false, 0, &m.air, &m.flight);
    int nMain = 0, nCut = 0;
    for (uint32_t k = 0; k < 40; ++k) {
        const uint32_t seed = k == 0 ? 2024u : k * 7919u;
        const float a = lapAlone(map, false, seed);
        if (a > 0.0f) { m.lapMain += a; ++nMain; } else ++m.stuck;
        if (!hasShortcut) continue;
        const float b = lapAlone(map, true, seed);
        if (b > 0.0f) { m.lapCut += b; ++nCut; } else ++m.stuck;
    }
    m.lapMain /= float(std::max(nMain, 1));
    m.lapCut = hasShortcut ? m.lapCut / float(std::max(nCut, 1)) : m.lapMain;
    return m;
}

// The straight the start/finish line stands on (corners wider than 500 m count as straight):
// metres of it behind and ahead of the line
void startStraight(const std::vector<sim::Slice> &S, int &behind, int &ahead, int at = 0)
{
    const int n = int(S.size());
    auto straight = [&](int i) { return std::fabs(S[size_t((i % n + n) % n)].curvature) < 1.0f / 500.0f; };
    behind = ahead = 0;
    while (ahead < n && straight(at + ahead)) ++ahead;
    while (behind < at + n && straight(at - 1 - behind)) ++behind;
    if (at > 0) behind = std::min(behind, at);              // (a sprint: the road begins there)
}



// The angle (degrees) between the two roads where the shortcut leaves and where it joins
float angleBetween(const sim::Slice &a, const sim::Slice &b)
{
    const float dot = std::clamp(a.tx * b.tx + a.tz * b.tz, -1.0f, 1.0f);
    return std::acos(dot) * 180.0f / 3.14159265f;
}

void render(const sim::CircuitDef &def, const sim::Track &tr, const sim::Track::Report &rep, const Measured &ms,
            float halfOnline, uint32_t groundCol, bool coast, const QString &path)
{
    const int W = 1800, H = 1400;
    Canvas cv(W, H, rgb(18, 22, 30));
    const auto &S = tr.slices();
    const auto &C = tr.shortcut();
    const int n = int(S.size());

    // ---- top view area ----
    const int vx0 = 20, vy0 = 20, vw = 1300, vh = 1000;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto *list : {&S, &C})
        for (const sim::Slice &s : *list) {
            minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
            minZ = std::min(minZ, s.z); maxZ = std::max(maxZ, s.z);
        }
    const float pad = 60.0f;
    const float k = std::min(vw / (maxX - minX + 2 * pad), vh / (maxZ - minZ + 2 * pad));   // pixels per metre
    const float ox = vx0 + (vw - (maxX - minX) * k) * 0.5f, oz = vy0 + (vh - (maxZ - minZ) * k) * 0.5f;
    auto PX = [&](float x) { return ox + (x - minX) * k; };
    auto PY = [&](float z) { return oz + (maxZ - z) * k; };        // north is up
    cv.rect(vx0, vy0, vx0 + vw, vy0 + vh, groundCol);
    cv.clip(vx0, vy0, vx0 + vw, vy0 + vh);

    const float yLo = rep.minY, yHi = std::max(rep.maxY, rep.minY + 1.0f);
    auto edges = [&](const sim::Slice &s, float half, float &lx, float &ly, float &rx, float &ry) {
        const float hw = half * s.width;
        lx = PX(s.x - s.tz * hw); ly = PY(s.z + s.tx * hw);        // left = (-tz, tx)
        rx = PX(s.x + s.tz * hw); ry = PY(s.z - s.tx * hw);
    };
    const bool open = tr.sprint();
    const int s0 = int(tr.startS()), s1 = open ? int(tr.finishS()) : n;
    const float half = std::max(halfOnline, 3.5f / k);              // the wide (online) road, at least 7 px
    auto ribbon = [&](const std::vector<sim::Slice> &L, bool closed, bool overpassPass) {
        const int m = int(L.size());
        for (int i = 0; i + (closed ? 0 : 1) < m; ++i) {
            const sim::Slice &a = L[i], &b = L[(i + 1) % m];
            const bool over = (a.flags & sim::TF_OVERPASS) != 0;
            if (over != overpassPass) continue;
            float alx, aly, arx, ary, blx, bly, brx, bry;
            edges(a, half, alx, aly, arx, ary);
            edges(b, half, blx, bly, brx, bry);
            uint32_t col = (a.flags & sim::TF_DIRT) ? rgb(150, 105, 60) : heightColour((a.y - yLo) / (yHi - yLo));
            if (overpassPass) {                                          // shadow under the raised deck
                std::vector<float> sx{alx + 6, arx + 6, brx + 6, blx + 6}, sy{aly + 6, ary + 6, bry + 6, bly + 6};
                cv.polygon(sx, sy, rgb(0, 0, 0), 0.35f);
            }
            std::vector<float> xs{alx, arx, brx, blx}, ys{aly, ary, bry, bly};
            cv.polygon(xs, ys, col);
            if (a.flags & sim::TF_TUNNEL) cv.polygon(xs, ys, rgb(10, 10, 14), 0.55f);
            cv.line(int(alx), int(aly), int(blx), int(bly), rgb(15, 15, 18));
            cv.line(int(arx), int(ary), int(brx), int(bry), rgb(15, 15, 18));
            if ((a.flags & sim::TF_TUNNEL) && i % 6 < 3) {               // dashed tunnel walls
                cv.line(int(alx), int(aly), int(blx), int(bly), rgb(230, 230, 235));
                cv.line(int(arx), int(ary), int(brx), int(bry), rgb(230, 230, 235));
            }
        }
    };
    if (coast) {                                                     // the sea lies east of the coast road
        float east = -1e9f;
        for (const sim::Slice &s : S) east = std::max(east, s.x);
        cv.rect(int(PX(east + 70.0f)), vy0, vx0 + vw, vy0 + vh, rgb(40, 110, 170));
    }
    // a river (or a gorge, or the harbour) crossing under the middle of each bridge
    for (int i = 0; i < n; ++i) {
        if (!(S[i].flags & sim::TF_BRIDGE) || (S[(i + n - 1) % n].flags & sim::TF_BRIDGE)) continue;
        int e = i;
        while ((S[(e + 1) % n].flags & sim::TF_BRIDGE) && e - i < n) ++e;
        const sim::Slice &mid = S[((i + e) / 2) % n];
        const float runHalf = std::max(20.0f, float(e - i) * 0.35f), reach = 110.0f;
        const float rxv = mid.tz, rzv = -mid.tx;                     // across the road
        const float UV[4][2] = {{-reach, -runHalf}, {reach, -runHalf}, {reach, runHalf}, {-reach, runHalf}};
        std::vector<float> xs, ys;
        for (const auto &uv : UV) {
            xs.push_back(PX(mid.x + rxv * uv[0] + mid.tx * uv[1]));
            ys.push_back(PY(mid.z + rzv * uv[0] + mid.tz * uv[1]));
        }
        cv.polygon(xs, ys, rgb(60, 130, 210));
    }
    ribbon(S, !open, false);
    if (!C.empty()) ribbon(C, false, false);
    ribbon(S, !open, true);

    // jump markers, start line, direction arrows, distance ticks, control points
    for (int i = 0; i < n; ++i) {
        const sim::Slice &s = S[i];
        const bool jumpStart = (s.flags & sim::TF_JUMP) && !(S[(i + n - 1) % n].flags & sim::TF_JUMP);
        if (jumpStart) {
            const int cx = int(PX(s.x)), cy = int(PY(s.z));
            std::vector<float> tx{float(cx - 14), float(cx + 14), float(cx)}, ty{float(cy + 12), float(cy + 12), float(cy - 14)};
            cv.polygon(tx, ty, rgb(255, 150, 20));
            cv.label(cx - 22, cy - 36, "JUMP", rgb(255, 170, 40), 2);
        }
        if (i % 250 == 125) {                                        // direction of travel
            const float ax = PX(s.x), ay = PY(s.z), fx = s.tx, fy = -s.tz, rx = -fy, ry = fx;
            std::vector<float> tx{ax + fx * 12, ax - fx * 8 + rx * 8, ax - fx * 8 - rx * 8};
            std::vector<float> ty{ay + fy * 12, ay - fy * 8 + ry * 8, ay - fy * 8 - ry * 8};
            cv.polygon(tx, ty, rgb(255, 255, 255), 0.9f);
        }
        const int tick = open ? 1000 : 500, from = i - s0;           // (a sprint: metres from the start line)
        if (from > 0 && from % tick == 0 && i < s1) {
            const int cx = int(PX(s.x)), cy = int(PY(s.z));
            cv.circle(cx, cy, 4, rgb(255, 255, 255));
            cv.label(cx + 8, cy + 6, num(float(from)) + "M", rgb(235, 235, 240), 2);
        }
    }
    for (int which = 0; which < (open ? 2 : 1); ++which) {          // checkered start (and finish) line
        const sim::Slice &s = S[size_t(which == 0 ? s0 : s1)];
        float lx, ly, rx, ry;
        edges(s, std::max(half, 8.0f / k), lx, ly, rx, ry);
        for (int t = 0; t <= 12; ++t) {
            float f = t / 12.0f;
            cv.circle(int(lx + (rx - lx) * f), int(ly + (ry - ly) * f), 3, (t % 2) ? rgb(0, 0, 0) : rgb(255, 255, 255));
        }
        cv.label(int(rx) + 10, int(ry) - 7, which == 0 ? "START" : "FINISH", rgb(255, 255, 255), 2);
    }
    for (size_t i = 0; i < def.pts.size(); ++i) {                    // control points (for editing)
        const int cx = int(PX(def.pts[i].x)), cy = int(PY(def.pts[i].z));
        cv.circle(cx, cy, 3, rgb(20, 20, 20));
        cv.text(cx + 5, cy + 4, num(float(i)), rgb(25, 25, 30), 1);
    }
    for (const sim::CtrlPoint &p : def.shortcut.via) {
        const int cx = int(PX(p.x)), cy = int(PY(p.z));
        cv.circle(cx, cy, 3, rgb(90, 50, 20));
    }
    {   // scale bar: 200 m
        const int bx = vx0 + 20, by = vy0 + vh - 30, len = int(200.0f * k);
        cv.rect(bx + 2, by + 2, bx + len + 2, by + 8, rgb(0, 0, 0));
        cv.rect(bx, by, bx + len, by + 6, rgb(255, 255, 255));
        cv.label(bx, by - 20, "200 M", rgb(255, 255, 255), 2);
    }
    cv.clip(0, 0, W, H);

    // ---- height profile ----
    const int px0 = 20, py0 = 1050, pw = 1760, ph = 330;
    cv.rect(px0, py0, px0 + pw, py0 + ph, rgb(28, 34, 46));
    const float pyLo = yLo - 5.0f, pyHi = yHi + 8.0f;
    auto PPX = [&](float s) { return px0 + 10 + s / float(n) * (pw - 20); };
    auto PPY = [&](float y) { return py0 + ph - 30 - (y - pyLo) / (pyHi - pyLo) * (ph - 60); };
    for (int i = 0; i < n; ++i) {                                    // feature bands
        const sim::Slice &s = S[i];
        int x = int(PPX(float(i)));
        if (s.flags & sim::TF_TUNNEL)   cv.line(x, py0 + 8, x, py0 + 22, rgb(150, 150, 160));
        if (s.flags & sim::TF_BRIDGE)   cv.line(x, py0 + 8, x, py0 + 22, rgb(60, 130, 210));
        if (s.flags & sim::TF_OVERPASS) cv.line(x, py0 + 8, x, py0 + 22, rgb(170, 90, 200));
        if (s.flags & sim::TF_JUMP)     cv.line(x, py0 + 8, x, py0 + 22, rgb(255, 150, 20));
    }
    for (int i = 0; i + 1 < n; ++i) {                                // ground under the line
        std::vector<float> xs{PPX(float(i)), PPX(float(i + 1)), PPX(float(i + 1)), PPX(float(i))};
        std::vector<float> ys{PPY(S[i].y), PPY(S[i + 1].y), float(py0 + ph - 30), float(py0 + ph - 30)};
        cv.polygon(xs, ys, heightColour((S[i].y - yLo) / (yHi - yLo)), 0.55f);
        cv.line(int(PPX(float(i))), int(PPY(S[i].y)), int(PPX(float(i + 1))), int(PPY(S[i + 1].y)), rgb(255, 255, 255));
    }
    if (!C.empty()) {                                                // shortcut, squeezed onto the stretch it skips
        const int m = int(C.size());
        const float span = rep.mainBetween;
        for (int j = 0; j + 1 < m; j += 2) {
            float s0 = tr.shortcutFromS() + span * j / (m - 1), s1 = tr.shortcutFromS() + span * (j + 1) / (m - 1);
            s0 = std::fmod(s0, float(n)); s1 = std::fmod(s1, float(n));
            if (s1 < s0) continue;
            if ((j / 6) % 2 == 0)
                cv.line(int(PPX(s0)), int(PPY(C[j].y)), int(PPX(s1)), int(PPY(C[j + 1].y)), rgb(200, 140, 70));
        }
    }
    for (int sm = 0; s0 + sm <= s1; sm += open ? 1000 : 500) {
        int x = int(PPX(float(s0 + sm)));
        cv.line(x, py0 + ph - 30, x, py0 + ph - 24, rgb(200, 200, 210));
        cv.text(x - 10, py0 + ph - 20, num(float(sm)), rgb(200, 200, 210), 1);
    }
    if (open)
        for (int which = 0; which < 2; ++which) {                    // start and finish on the profile
            const int x = int(PPX(float(which == 0 ? s0 : s1)));
            cv.line(x, py0 + 30, x, py0 + ph - 30, rgb(255, 255, 255));
            cv.text(x + 4, py0 + ph - 48, which == 0 ? "START" : "FINISH", rgb(255, 255, 255), 1);
        }
    cv.text(px0 + 12, py0 + 30, std::string(open ? "HEIGHT ALONG THE ROAD:  " : "HEIGHT ALONG THE LAP:  ") + num(rep.minY) +
            " M TO " + num(rep.maxY) + " M", rgb(235, 235, 240), 2);

    // ---- info panel ----
    int ix = 1340, iy = 30;
    auto line = [&](const std::string &s, uint32_t c = rgb(235, 235, 240), int sz = 2) {
        cv.text(ix, iy, s, c, sz);
        iy += 9 * sz + 6;
    };
    line(def.name, rgb(255, 210, 80), 3);
    iy += 6;
    {   // wrap the feature line
        std::string f = def.features, cur;
        std::vector<std::string> words;
        size_t st = 0;
        while (st < f.size()) {
            size_t e = f.find(' ', st);
            if (e == std::string::npos) e = f.size();
            words.push_back(f.substr(st, e - st));
            st = e + 1;
        }
        cur.clear();
        for (const std::string &w : words) {
            std::string tryS = cur.empty() ? w : cur + " " + w;
            if (Canvas::textWidth(tryS, 2) > 430) { line(cur, rgb(190, 200, 215)); cur = w; }
            else cur = tryS;
        }
        if (!cur.empty()) line(cur, rgb(190, 200, 215));
    }
    iy += 10;
    if (open) {
        const float len = float(s1 - s0);
        line("SPRINT: " + num(len / 1000.0f, 1) + " KM, START TO FINISH");
        line("NO LAPS: RACED ONCE");
        line("RACE ALONE: " + num(ms.lapMain, 1) + " S (CLEAN " + num(ms.clean, 1) + " S)", rgb(120, 230, 120));
        if (ms.air > 0.0f)
            line("LONGEST JUMP: " + num(ms.air, 1) + " S, " + num(ms.flight) + " M", rgb(120, 230, 120));
        line("BENDS: ALL WIDER THAN R " + num(rep.minRadius) + " M");
        line("SMALL OBSTACLES ONLY,", rgb(190, 200, 215));
        line("  ABOUT ONE EVERY 180 M", rgb(190, 200, 215));
        line(num(std::floor(len / 200.0f)) + " CARS AND TRUCKS,", rgb(190, 200, 215));
        line("  KEEPING THEIR LANES", rgb(190, 200, 215));
    } else {
        line("LAP: " + num(rep.length) + " M");
        line("RACE: " + num(float(def.laps)) + " LAPS = " + num(rep.length * def.laps / 1000.0f, 1) + " KM");
        line("LAP ALONE: " + num(ms.lapMain, 1) + " S (CLEAN " + num(ms.clean, 1) + " S)", rgb(120, 230, 120));
        line("RACE OF " + num(float(def.laps)) + " LAPS: ABOUT " + num(ms.lapMain * def.laps / 60.0f, 1) + " MIN", rgb(120, 230, 120));
        if (ms.air > 0.0f)
            line("LONGEST JUMP: " + num(ms.air, 1) + " S, " + num(ms.flight) + " M", rgb(120, 230, 120));
    }
    if (!open) {
        int behind, ahead;
        startStraight(tr.slices(), behind, ahead, s0);
        line("START STRAIGHT: " + num(float(behind)) + " M + " + num(float(ahead)) + " M",
             behind + ahead >= 150 && behind >= 50 ? rgb(235, 235, 240) : rgb(255, 90, 80));
    }
    line("CLIMB: " + num(rep.maxY - rep.minY) + " M");
    line("STEEPEST: " + num(rep.maxGrade * 100.0f, 1) + " %");
    line("TIGHTEST CORNER: R " + num(rep.minRadius) + " M");
    if (rep.shortcutLength > 0) {
        const auto &M = tr.slices(), &K = tr.shortcut();
        const float fork = angleBetween(K.front(), M[size_t(tr.shortcutFromS())]);
        const float merge = angleBetween(K.back(), M[size_t(tr.shortcutToS()) % M.size()]);
        line("SHORTCUT: " + num(rep.shortcutLength) + " M");
        line("  LEAVES AT " + num(fork) + " DEG, JOINS AT " + num(merge) + " DEG");
        line("  SKIPS " + num(rep.mainBetween) + " M OF ROAD");
        line("  SAVES " + num(rep.mainBetween - rep.shortcutLength) + " M = " + num(ms.lapMain - ms.lapCut, 1) + " S",
             rgb(120, 230, 120));
    }
    iy += 10;
    line("CHECKS", rgb(255, 210, 80));
    if (!open) line(std::string("LOOP CLOSED: ") + (rep.seam < 1.6f ? "YES" : "NO"), rep.seam < 1.6f ? rgb(120, 230, 120) : rgb(255, 90, 80));
    line(std::string("ROAD HITS ITSELF: ") + (rep.clashes == 0 ? "NO" : num(float(rep.clashes)) + " PLACES"),
         rep.clashes == 0 ? rgb(120, 230, 120) : rgb(255, 90, 80));
    line("CROSSOVERS: " + num(float(rep.crossings)));
    line(std::string("CORNERS DRIVABLE: ") + (rep.minRadius >= MIN_RADIUS ? "YES" : "TOO TIGHT"),
         rep.minRadius >= MIN_RADIUS ? rgb(120, 230, 120) : rgb(255, 90, 80));
    iy += 14;
    line("KEY", rgb(255, 210, 80));
    for (int t = 0; t <= 4; ++t) cv.rect(ix + t * 40, iy, ix + t * 40 + 40, iy + 16, heightColour(t / 4.0f));
    iy += 22;
    line("ROAD COLOUR = HEIGHT, LOW TO HIGH", rgb(190, 200, 215), 1);
    cv.rect(ix, iy, ix + 30, iy + 14, rgb(150, 105, 60)); cv.text(ix + 40, iy + 1, "SHORTCUT (DIRT)", rgb(190, 200, 215), 2); iy += 24;
    cv.rect(ix, iy, ix + 30, iy + 14, rgb(50, 50, 55)); cv.text(ix + 40, iy + 1, "TUNNEL", rgb(190, 200, 215), 2); iy += 24;
    cv.rect(ix, iy, ix + 30, iy + 14, rgb(60, 130, 210)); cv.text(ix + 40, iy + 1, "BRIDGE / WATER", rgb(190, 200, 215), 2); iy += 24;
    cv.rect(ix, iy, ix + 30, iy + 14, rgb(170, 90, 200)); cv.text(ix + 40, iy + 1, "RAISED ROAD (CROSSOVER)", rgb(190, 200, 215), 2); iy += 24;
    cv.rect(ix, iy, ix + 30, iy + 14, rgb(255, 150, 20)); cv.text(ix + 40, iy + 1, "JUMP", rgb(190, 200, 215), 2); iy += 24;
    line("ROAD DRAWN AT ONLINE WIDTH (5 LANES)", rgb(150, 160, 175), 1);
    if (open) {
        line("GREEN: THE CPU DRIVER ALONE IN THE GAME SIMULATION, NO TRAFFIC,", rgb(150, 160, 175), 1);
        line("AVERAGE OF 40 SETS OF OBSTACLES (CLEAN: NONE)", rgb(150, 160, 175), 1);
    } else {
        line("GREEN: THE CPU DRIVER ALONE IN THE GAME SIMULATION,", rgb(150, 160, 175), 1);
        line("AVERAGE OF 40 SETS OF CONES (CLEAN: NO CONES)", rgb(150, 160, 175), 1);
    }

    if (!cv.save(path)) std::fprintf(stderr, "could not save %s\n", qPrintable(path));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString("layouts");
    QDir().mkpath(outDir);

    // online road: 5 lanes of 4.67 m; rails 4 m beyond the edge
    const float halfRoad = 7.0f + 4.67f, halfCorridor = halfRoad + 4.0f;
    static const uint32_t GROUND[6] = {rgb(92, 140, 70), rgb(196, 150, 100), rgb(225, 232, 240), rgb(40, 44, 60), rgb(110, 160, 90), rgb(46, 110, 60)};   // (by theme)

    int bad = 0;
    const auto &circuits = sim::draftCircuits();
    for (size_t i = 0; i < circuits.size(); ++i) {
        const sim::CircuitDef &def = circuits[i];
        sim::Track tr;
        tr.build(def);
        const sim::Track::Report r = tr.check(halfCorridor);
        std::printf("%-16s lap %5.0f m  climb %4.0f m  steepest %4.1f%%  tightest R %4.0f m (main: at s %4.0f, shortcut R %4.0f)  "
                    "shortcut %4.0f m vs %4.0f m  clashes %d  crossings %d  seam %.2f\n",
                    def.name, r.length, r.maxY - r.minY, r.maxGrade * 100.0f, r.minRadius, r.minRadiusS,
                    r.shortcutMinRadius, r.shortcutLength, r.mainBetween, r.clashes, r.crossings, r.seam);
        bad += r.clashes > 0 || r.minRadius < MIN_RADIUS || r.seam >= 1.6f;
        QString name = QString("%1_%2.png").arg(i + 1).arg(QString(def.name).toLower().replace(' ', '_'));
        const Measured ms = measure(int(i), !def.shortcut.via.empty());
        int behind, ahead;
        startStraight(tr.slices(), behind, ahead);
        const auto &M = tr.slices(), &K = tr.shortcut();
        std::printf("%-16s start straight %d m behind + %d m ahead; shortcut leaves at %.0f deg, joins at %.0f deg; "
                    "height %.0f to %.0f m\n", def.name, behind, ahead,
                    K.empty() ? 0.0f : angleBetween(K.front(), M[size_t(tr.shortcutFromS())]),
                    K.empty() ? 0.0f : angleBetween(K.back(), M[size_t(tr.shortcutToS()) % M.size()]), r.minY, r.maxY);
        std::printf("%-16s lap alone %.1f s (clean %.1f s), with the shortcut %.1f s (saves %.2f s), "
                    "longest jump %.2f s / %.0f m, stuck %d\n", def.name, ms.lapMain, ms.clean, ms.lapCut,
                    ms.lapMain - ms.lapCut, ms.air, ms.flight, ms.stuck);
        render(def, tr, r, ms, halfRoad, GROUND[int(sim::MAPS[i].theme)], sim::MAPS[i].theme == sim::MapTheme::Coast, QDir(outDir).filePath(name));
    }
    std::printf("%d circuit(s) with problems\n", bad);
    return bad ? 1 : 0;
}
