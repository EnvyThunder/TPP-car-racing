#ifndef TRACK_H
#define TRACK_H

// Circuits (step 1 of docs/ROADMAP.md): a closed smooth curve through a map's control
// points, cut into 1 m slices. Everything on a circuit is placed by s (metres along the
// lap) and d (metres right of the centre line). Only + - * / and sqrt are used, so every
// platform builds exactly the same track.
//
// The game races on it (shared/gamesim.cpp); tools/track_preview draws the layouts from it.

#include <cstdint>
#include <vector>

namespace sim {

// Markers for the stretch of road from a control point to the next one
enum TrackFlag : uint16_t {
    TF_TUNNEL   = 1,    // walls and ceiling
    TF_BRIDGE   = 2,    // deck over water or a gorge
    TF_JUMP     = 4,    // ramp / crest the cars fly off
    TF_OVERPASS = 8,    // raised road that crosses over another part of the circuit
    TF_DIRT     = 16,   // rough surface (shortcuts)
};

struct CtrlPoint {
    float x = 0, z = 0;       // top view, metres (x east, z north)
    float y = 0;              // height, metres
    float bank = 0;           // sideways tilt in degrees (+ = right edge lower)
    float width = 1.0f;       // road width multiplier
    uint16_t flags = 0;       // TrackFlag bits for the stretch to the next point
};

struct ShortcutDef {
    int fromPt = -1, toPt = -1;    // main control points where it leaves and rejoins
    std::vector<CtrlPoint> via;    // its own points in between
};

struct CircuitDef {
    const char *name = "";
    const char *features = "";     // one-line description for the layout pictures
    std::vector<CtrlPoint> pts;    // closed loop; point 0 is the start/finish line
    ShortcutDef shortcut;
    int laps = 3;
    // A sprint is an open road raced once from start to finish: point 0 is where the road
    // begins (the grid stands behind the start), point 1 the start line, the second-to-last
    // point the finish line and the last point the end of the run-off.
    bool sprint = false;
    // its jump ramps' lip: this many degrees steeper than the road (set with tools/ai_bench so
    // every launch lands on the road)
    float rampLipDeg = 6.0f;
};

struct Slice {
    float x = 0, y = 0, z = 0;     // centre of the road
    float tx = 0, tz = 1;          // direction of travel (horizontal, unit length)
    float grade = 0;               // rise per metre
    float curvature = 0;           // 1 / radius in 1/m, + = turning left
    float bank = 0, width = 1.0f;
    uint16_t flags = 0;
};

class Track {
public:
    void build(const CircuitDef &def);

    float length() const { return float(main.size()); }        // metres per lap (a sprint: the whole road)
    bool sprint() const { return open; }                        // an open road raced once
    float startS() const { return open ? ptS[1] : 0.0f; }      // the start line
    float finishS() const { return open ? ptS[ptS.size() - 2] : 0.0f; }   // a sprint's finish line
    const std::vector<Slice> &slices() const { return main; }  // main loop, one per metre
    const std::vector<Slice> &shortcut() const { return cut; } // shortcut path, one per metre
    float pointS(int i) const { return ptS[i]; }               // s of control point i
    float shortcutFromS() const { return cutFromS; }
    float shortcutToS() const { return cutToS; }

    struct Report {
        float length = 0, minRadius = 0, minRadiusS = 0, maxGrade = 0, minY = 0, maxY = 0;
        float shortcutLength = 0, mainBetween = 0;     // shortcut vs the main road it skips
        float shortcutMinRadius = 0;                   // (minRadius covers both roads)
        int clashes = 0;                               // places where the road runs into itself
        float clashS = -1;                             // first one found
        int crossings = 0;                             // planned crossovers (one road above the other)
        float seam = 0;                                // gap between the last and first slice
    };
    // halfCorridor = half the road width plus the rails (the widest race type)
    Report check(float halfCorridor) const;

private:
    std::vector<Slice> main, cut;
    std::vector<float> ptS;
    float cutFromS = -1, cutToS = -1;
    bool open = false;
};

// The maps, circuits and sprints (shared/circuits.cpp)
const std::vector<CircuitDef> &draftCircuits();

} // namespace sim

#endif // TRACK_H
