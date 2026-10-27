#ifndef SIMTYPES_H
#define SIMTYPES_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace sim {

// Constants
constexpr float PI          = 3.14159265f;
constexpr float ROAD_HALF   = 7.0f;               // Road width is 14 units (3 lanes)
constexpr float RAIL_OFFSET = ROAD_HALF + 4.0f;   // Guard rails
constexpr float TRACK_LEN   = 4000.0f;            // Finish line distance
constexpr float MAX_SPEED   = 1.10f;
constexpr float NITRO_SPEED = 1.65f;
constexpr float OFFROAD_MAX = 0.45f;
constexpr float LANES[3]    = {-4.67f, 0.0f, 4.67f};

// Input frame for a vehicle
struct PlayerInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool nitro = false;
};

// Physics state of a vehicle
struct Car {
    float x = 0.0f, z = 0.0f;     // World position (track runs towards -Z)
    float heading = 0.0f;         // Radians: 0 = facing -Z, + = turning right
    float speed = 0.0f;           // Units per tick
    float nitro = 0.0f;           // 0..1 nitro tank / timer display ratio
    bool  nitroOn = false;
    int   nitroTimer = 0;         // Remaining boost ticks (120 ticks = 2.0s)
    int   nitroCharges = 0;       // Available nitro charges (capped at 1, no stacking)
    float steerVis = 0.0f;        // Visual front-wheel steer angle
    float wheelSpin = 0.0f;       // Degrees
    float r = 1.0f, g = 0.0f, b = 0.0f;
    bool  finished = false;

    // Advanced vehicle dynamics & metrics
    float roll = 0.0f;            // Visual body roll when turning
    float pitch = 0.0f;           // Visual body pitch (acceleration/braking)
    float driftAngle = 0.0f;      // Drift slip angle
    bool  isDrifting = false;
    float topSpeed = 0.0f;        // Max speed reached
    float finishTime = 0.0f;      // Time in seconds to finish
    float speedScale = 1.0f;      // Top-speed multiplier (CPU difficulty / catch-up); players stay at 1

    // CPU driver memory
    float aiTarget = 0.0f;        // Sideways offset from the road centre the CPU is steering for
    int   aiReverse = 0;          // Remaining ticks of a reverse-out manoeuvre
    int   aiCheckTimer = 0;       // Progress watchdog: ticks since the last check
    float aiCheckZ = 0.0f;        // Position at the last check
};

// ---- Maps ----
// Each map has its own road shape and a scenery theme. The theme decides
// which props the sim places; the client also uses it for sky and lighting.
enum class MapTheme { Countryside, Desert, Snow, City, Coast };

struct MapDef {
    const char *name;
    const char *tagline;
    MapTheme theme;
    float amp[3], freq[3], phase[3]; // road centre line = sum of three sine waves
    int   trafficCount;
    float obstacleGapMin, obstacleGapMax; // spacing between obstacle patterns
    int   stars;                     // 1..3 difficulty shown in the menu
};

constexpr int NUM_MAPS = 5;
extern const MapDef MAPS[NUM_MAPS];

struct Traffic {
    float lane = 0.0f, targetLane = 0.0f;
    float z = 0.0f, speed = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    bool  truck = false;
    bool  parked = false;
};

struct Obstacle {
    enum Type { Cone, Barrel, Barrier };
    Type  type = Cone;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float hx = 0.0f, hz = 0.0f;   // Half extents (barriers only)
    float vx = 0.0f, vy = 0.0f, vz = 0.0f; // Velocity when knocked away
    float rot = 0.0f, spin = 0.0f;
    bool  knocked = false;
};

struct Prop {
    enum Type { House, Pine, RoundTree, Lamp, Torch,
                Cactus, Rock, SnowPine, Building, Palm, Lighthouse };
    Type  type = Pine;
    float x = 0.0f, z = 0.0f, rot = 0.0f, scale = 1.0f;
    int   palette = 0;
};

// Collectible Nitro canister on the track
struct NitroPickup {
    float x = 0.0f, y = 0.5f, z = 0.0f;
    bool  active = true;
};

// Simulation event for client audio / particle side-effects
struct SimEvent {
    enum Type {
        Crash,
        Sparks,
        Debris,
        NitroFlame,
        BrakeSmoke,
        DriftSmoke,
        Fanfare,
        CountdownBeep,
        NitroPickupEvent
    };

    Type  type = Crash;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float intensity = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    int   count = 0;
    int   carIndex = 0;
    bool  highPitch = false;
};

} // namespace sim

#endif // SIMTYPES_H
