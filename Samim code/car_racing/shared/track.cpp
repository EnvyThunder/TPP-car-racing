#include "track.h"

// Same maths on every machine (see gamesim.cpp)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif
#include <algorithm>
#include <cmath>

namespace sim {

namespace {

struct V3 { double x, y, z; };

V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(double k, V3 a) { return {k * a.x, k * a.y, k * a.z}; }
double dist(V3 a, V3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
V3 toV3(const CtrlPoint &p) { return {p.x, p.y, p.z}; }

// Centripetal Catmull-Rom between p1 and p2 (Barry-Goldman form): passes through every
// control point and never loops or overshoots in tight corners.
struct Segment {
    V3 p[4];
    double t[4];
    Segment(V3 a, V3 b, V3 c, V3 d) : p{a, b, c, d} {
        t[0] = 0.0;
        for (int i = 1; i < 4; ++i)
            t[i] = t[i - 1] + std::max(1e-6, std::sqrt(dist(p[i - 1], p[i])));   // |chord|^0.5
    }
    V3 at(double u) const {                       // u in [0,1] from p1 to p2
        const double t0 = t[0], t1 = t[1], t2 = t[2], t3 = t[3];
        const double tt = t1 + (t2 - t1) * u;
        V3 a1 = ((t1 - tt) / (t1 - t0)) * p[0] + ((tt - t0) / (t1 - t0)) * p[1];
        V3 a2 = ((t2 - tt) / (t2 - t1)) * p[1] + ((tt - t1) / (t2 - t1)) * p[2];
        V3 a3 = ((t3 - tt) / (t3 - t2)) * p[2] + ((tt - t2) / (t3 - t2)) * p[3];
        V3 b1 = ((t2 - tt) / (t2 - t0)) * a1 + ((tt - t0) / (t2 - t0)) * a2;
        V3 b2 = ((t3 - tt) / (t3 - t1)) * a2 + ((tt - t1) / (t3 - t1)) * a3;
        return ((t2 - tt) / (t2 - t1)) * b1 + ((tt - t1) / (t2 - t1)) * b2;
    }
};

struct Dense { V3 pos; double s; int seg; double frac; };

// Fine samples along consecutive segments, with the running length
std::vector<Dense> sampleDense(const std::vector<Segment> &segs, std::vector<double> *segStartS)
{
    std::vector<Dense> out;
    double s = 0.0;
    for (int k = 0; k < int(segs.size()); ++k) {
        if (segStartS) segStartS->push_back(s);
        const int steps = std::max(16, int(dist(segs[k].p[1], segs[k].p[2]) / 0.25));
        // the arc length of this segment, to report how far along it each sample is
        std::vector<V3> pts(steps + 1);
        for (int i = 0; i <= steps; ++i) pts[i] = segs[k].at(double(i) / steps);
        double segLen = 0.0;
        for (int i = 1; i <= steps; ++i) segLen += dist(pts[i - 1], pts[i]);
        double acc = 0.0;
        for (int i = (k == 0 ? 0 : 1); i <= steps; ++i) {
            if (i > 0) acc += dist(pts[i - 1], pts[i]);
            out.push_back({pts[i], s + acc, k, segLen > 0 ? acc / segLen : 0.0});
        }
        s += segLen;
    }
    return out;
}

double smooth(double f) { return f * f * (3.0 - 2.0 * f); }

// Even slices ~1 m apart, with direction, grade and curvature from the neighbours
std::vector<Slice> resample(const std::vector<Dense> &d, bool closed,
                            const std::vector<CtrlPoint> &attrFrom, const std::vector<CtrlPoint> &attrTo)
{
    const double total = d.back().s;
    const int n = std::max(2, int(std::lround(total)));
    const double step = closed ? total / n : total / (n - 1);
    std::vector<Slice> out(n);
    size_t j = 0;
    for (int i = 0; i < n; ++i) {
        const double s = i * step;
        while (j + 2 < d.size() && d[j + 1].s < s) ++j;
        const Dense &a = d[j], &b = d[j + 1];
        const double f = (b.s > a.s) ? std::clamp((s - a.s) / (b.s - a.s), 0.0, 1.0) : 0.0;
        Slice &sl = out[i];
        sl.x = float(a.pos.x + (b.pos.x - a.pos.x) * f);
        sl.y = float(a.pos.y + (b.pos.y - a.pos.y) * f);
        sl.z = float(a.pos.z + (b.pos.z - a.pos.z) * f);
        const int seg = a.seg;
        // b may already be the next segment's first sample: that is this segment's end
        const double u = smooth(a.frac + ((b.seg == a.seg ? b.frac : 1.0) - a.frac) * f);
        const CtrlPoint &p = attrFrom[seg], &q = attrTo[seg];
        sl.bank = float(p.bank + (q.bank - p.bank) * u);
        sl.width = float(p.width + (q.width - p.width) * u);
        sl.flags = p.flags;
    }
    auto idx = [&](int i) { return closed ? (i + n) % n : std::clamp(i, 0, n - 1); };
    for (int i = 0; i < n; ++i) {
        const Slice &a = out[idx(i - 1)], &b = out[idx(i + 1)];
        double dx = b.x - a.x, dz = b.z - a.z, len = std::sqrt(dx * dx + dz * dz);
        out[i].tx = float(len > 0 ? dx / len : 0.0);
        out[i].tz = float(len > 0 ? dz / len : 1.0);
        const double span = (idx(i + 1) - idx(i - 1) + n) % n * step;
        out[i].grade = float(span > 0 ? (b.y - a.y) / span : 0.0);
    }
    for (int i = 0; i < n; ++i) {
        const Slice &a = out[idx(i - 1)], &b = out[idx(i + 1)];
        const double cross = double(a.tx) * b.tz - double(a.tz) * b.tx;   // + = turning left
        const double span = (idx(i + 1) - idx(i - 1) + n) % n * step;
        out[i].curvature = float(span > 0 ? cross / span : 0.0);
    }
    return out;
}

} // namespace

void Track::build(const CircuitDef &def)
{
    const std::vector<CtrlPoint> &P = def.pts;
    const int n = int(P.size());
    open = def.sprint;
    // a loop wraps round; an open road gets a mirrored point beyond each end, so it starts
    // and ends heading straight on
    auto at = [&](int i) {
        if (!open) return P[(i % n + n) % n];
        if (i < 0) { CtrlPoint q = P[0]; q.x = 2 * P[0].x - P[1].x; q.z = 2 * P[0].z - P[1].z; q.y = 2 * P[0].y - P[1].y; return q; }
        if (i >= n) { CtrlPoint q = P[n - 1]; q.x = 2 * P[n - 1].x - P[n - 2].x; q.z = 2 * P[n - 1].z - P[n - 2].z;
                      q.y = 2 * P[n - 1].y - P[n - 2].y; return q; }
        return P[i];
    };

    // segment k runs from point k to point k+1
    std::vector<Segment> segs;
    std::vector<CtrlPoint> from, to;
    for (int k = 0; k < (open ? n - 1 : n); ++k) {
        segs.emplace_back(toV3(at(k - 1)), toV3(at(k)), toV3(at(k + 1)), toV3(at(k + 2)));
        from.push_back(at(k));
        to.push_back(at(k + 1));
    }
    std::vector<double> segS;
    std::vector<Dense> dense = sampleDense(segs, &segS);
    main = resample(dense, !open, from, to);
    const double scale = double(open ? main.size() - 1 : main.size()) / dense.back().s;   // slices are ~1 m apart
    ptS.clear();
    for (double s : segS) ptS.push_back(float(s * scale));
    if (open) ptS.push_back(float(main.size() - 1));             // the last point: the end of the road

    // shortcut: an open curve from one main point, through its own points, to another
    cut.clear();
    cutFromS = cutToS = -1;
    const ShortcutDef &sc = def.shortcut;
    if (sc.fromPt >= 0 && sc.toPt >= 0 && !sc.via.empty()) {
        std::vector<CtrlPoint> Q;
        Q.push_back(at(sc.fromPt - 1));
        Q.push_back(at(sc.fromPt));
        for (const CtrlPoint &v : sc.via) Q.push_back(v);
        Q.push_back(at(sc.toPt));
        Q.push_back(at(sc.toPt + 1));
        std::vector<Segment> cs;
        std::vector<CtrlPoint> cf, ct;
        for (int k = 1; k + 2 < int(Q.size()); ++k) {
            cs.emplace_back(toV3(Q[k - 1]), toV3(Q[k]), toV3(Q[k + 1]), toV3(Q[k + 2]));
            // the shortcut's own look (width, surface) all the way, junctions included
            const CtrlPoint &look = sc.via.front();
            CtrlPoint a = look, b = look;
            a.bank = b.bank = 0;
            cf.push_back(a);
            ct.push_back(b);
        }
        cut = resample(sampleDense(cs, nullptr), false, cf, ct);
        cutFromS = ptS[(sc.fromPt % n + n) % n];
        cutToS = ptS[(sc.toPt % n + n) % n];
    }
}

Track::Report Track::check(float halfCorridor) const
{
    Report r;
    const int n = int(main.size());
    r.length = float(n);
    if (n < 3) return r;
    const float dsx = main[0].x - main[n - 1].x, dsz = main[0].z - main[n - 1].z;
    r.seam = open ? 0.0f : std::sqrt(dsx * dsx + dsz * dsz);       // (an open road has no seam)

    float maxCurv = 0;
    r.minY = r.maxY = main[0].y;
    for (int i = 0; i < n; ++i) {
        const Slice &s = main[i];
        if (std::fabs(s.curvature) > maxCurv) { maxCurv = std::fabs(s.curvature); r.minRadiusS = float(i); }
        r.maxGrade = std::max(r.maxGrade, std::fabs(s.grade));
        r.minY = std::min(r.minY, s.y);
        r.maxY = std::max(r.maxY, s.y);
    }
    r.minRadius = maxCurv > 0 ? 1.0f / maxCurv : 1e9f;

    // Does the road run into itself? Two slices far apart along the lap must not overlap
    // (rails included) unless one passes clearly above the other.
    const float CLEARANCE = 7.0f;
    auto overlap = [&](const Slice &a, const Slice &b, bool &above) {
        const float dx = a.x - b.x, dz = a.z - b.z;
        const float need = halfCorridor * (a.width + b.width);
        if (dx * dx + dz * dz >= need * need) return false;
        above = std::fabs(a.y - b.y) >= CLEARANCE;
        return true;
    };
    bool prevClash = false, prevCross = false;
    for (int i = 0; i < n; ++i) {
        bool clash = false, cross = false;
        for (int j = 0; j < n; ++j) {
            const int sep = open ? std::abs(i - j) : std::min(std::abs(i - j), n - std::abs(i - j));
            if (sep < 80) continue;
            bool above = false;
            if (overlap(main[i], main[j], above)) (above ? cross : clash) = true;
        }
        if (clash && !prevClash) { ++r.clashes; if (r.clashS < 0) r.clashS = float(i); }
        if (cross && !prevCross) ++r.crossings;
        prevClash = clash;
        prevCross = cross;
    }
    r.crossings /= 2;                         // each crossover is seen from both roads

    if (!cut.empty()) {
        const int m = int(cut.size());
        r.shortcutLength = float(m);
        r.mainBetween = std::fmod(cutToS - cutFromS + float(n), float(n));
        float cutCurv = 0;
        for (const Slice &s : cut) {
            cutCurv = std::max(cutCurv, std::fabs(s.curvature));
            r.maxGrade = std::max(r.maxGrade, std::fabs(s.grade));
        }
        r.shortcutMinRadius = cutCurv > 0 ? 1.0f / cutCurv : 1e9f;
        r.minRadius = std::min(r.minRadius, r.shortcutMinRadius);
        // where the shortcut splits off and joins again the two roads naturally share
        // space (a fork), so the main road near those two places doesn't count
        auto nearFork = [&](int j) {
            auto gap = [&](float s) { float g = std::fabs(float(j) - s); return std::min(g, float(n) - g); };
            return gap(cutFromS) < 120.0f || gap(cutToS) < 120.0f;
        };
        bool prev = false;
        for (int k = 0; k < m; ++k) {
            bool clash = false;
            for (int j = 0; j < n; ++j) {
                bool above = false;
                if (!nearFork(j) && overlap(cut[k], main[j], above) && !above) clash = true;
            }
            if (clash && !prev) { ++r.clashes; if (r.clashS < 0) r.clashS = -float(k) - 1; }
            prev = clash;
        }
    }
    return r;
}

} // namespace sim
