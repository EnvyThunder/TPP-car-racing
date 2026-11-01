#ifndef SIMTYPES_H
#define SIMTYPES_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace sim {

// Constants
constexpr float PI          = 3.14159265f;
// Road: 3 lanes (14 units) for 2 cars. A pack (online rooms, or vs CPU with 2+ CPUs)
// gets 5 lanes (23.3 units). A race's own width is GameSim::roadHalf(). Every map is a
// circuit (shared/track.h, shared/circuits.cpp) raced over several laps.
constexpr float ROAD_HALF   = 7.0f;               // Half width of the 3-lane road
constexpr float LANE_WIDTH  = 4.67f;
constexpr int   ONLINE_LANES = 5;
constexpr float MAX_SPEED   = 1.10f;
constexpr float NITRO_SPEED = 1.65f;
constexpr float OFFROAD_MAX = 0.45f;
constexpr float GRAVITY     = 2.0f * 9.8f / 3600.0f;   // m per tick per tick: twice real gravity (quick, high arcs)
constexpr int   MAX_RACERS  = 6;                  // Every race holds up to 6 cars (online and vs CPU)

// Input frame for a vehicle
struct PlayerInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool nitro = false;
    bool boost = false;           // L held: more speed, paid for with health - more the longer it is held (not in split screen)
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
    int   nitroLen = 120;         // how long the charge it holds lasts (ticks: later takers of a can get more)
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
    float speedScale = 1.0f;      // (always 1: no car gets speed help any more)
    int   aiLevel = 0;            // > 0: a CPU racer (0: a person, or the test drivers)

    // CPU driver memory
    float aiTarget = 0.0f;        // Sideways offset from the road centre the CPU is steering for
    int   aiReverse = 0;          // Remaining ticks of a reverse-out manoeuvre
    int   aiCheckTimer = 0;       // Progress watchdog: ticks since the last check
    float aiCheckProg = 0.0f;     // Race progress at the last check

    // Where the car is on the circuit (GameSim::locate keeps these up to date)
    float s = 0.0f;               // metres along the lap from the start/finish line
    float d = 0.0f;               // metres right of the centre line
    int   lap = -1;               // laps started (-1 on the grid, behind the line)
    float progress = 0.0f;        // lap * lap length + s: how far the car has raced
    int   path = 0;               // the road it is on: 0 the main road, 1 the shortcut (s, d on that road)
    bool  cutClosed = false;      // the shortcut is closed to it: one of the leaders (GameSim::SHORTCUT_CLOSED_TOP)
    bool  cutPlan = false;        // CPU: take the shortcut this lap ...
    int   cutPlanLap = -99;       // ... decided on this lap
    float y = 0.0f;               // height of the car (on the road, or flying)
    float vy = 0.0f;              // vertical speed, m per tick (climbing the road, or flying)
    int   air = 0;                // ticks in the air (0 = on the road)
    float lapStart = 0.0f;        // race time when this lap began (< 0: went back over the line)
    float bestLap = 0.0f;         // fastest whole lap so far in seconds (0: none yet)
    int   hint = 0;               // slice it was near last time (where the search starts)

    // Crashes (GameSim::damage): every hit takes health off - most of it from the car that
    // drove into the other - and at 0 the car is wrecked: it tumbles, sometimes throws its
    // driver out, and is back in the race 2-3 s later, passing through other cars for 1.5 s
    float health = 100.0f;        // 0..100
    int   wreck = 0;              // ticks since the car was wrecked (0 = driving)
    bool  ejected = false;        // this wreck threw the driver out: they run back to the car
    float wreckX = 0.0f, wreckZ = 0.0f;     // where the wreck began ...
    float wreckVx = 0.0f, wreckVz = 0.0f;   // ... and how fast it was going then (metres per tick)
    float wreckSpin = 0.0f;       // how the wreck tumbles: +/- 1 or 2 rolls (look only)
    int   ghost = 0;              // ticks left of driving through other cars after coming back
    int   calm = 999;             // ticks since the last damage (the car mends itself after 4 s)
    int   wrecks = 0;             // this race: times wrecked
    int   crashesCaused = 0;      // this race: hits on other cars that were this car's fault

    // Holding L boosts, costing health faster the longer it is held. (There is no elimination:
    // `out` stays false; it is kept so old screens and snapshots still read it.)
    bool  out = false;            // (never set)
    float lastWreckProg = -1e9f;  // race progress where its health last ran out
    bool  lifeWreck = false;      // this wreck took all the health (back with 100)
    int   boostHold = 0;          // ticks L has been held (let go, it runs down twice as fast)
    bool  boosting = false;       // boosting this tick
    float dmgFrom[MAX_RACERS] = {};   // damage taken from each car since the last wreck (bounty)
    int   bounties = 0;           // this race: +15% bounties won

    // Jumps (GameSim::fly): side ramps send the car up high; a twisted one rolls it. Up / Down
    // in the air speed up or slow down the roll; how straight and level it lands decides the damage.
    float barrel = 0.0f;          // roll in the air, radians (0 = upright)
    float barrelRate = 0.0f;      // radians per tick
    float spinRate = 0.0f;        // flat spin from hitting a ramp at an angle (heading per tick)
    float jumpDanger = 0.0f;      // 0: an ordinary jump; else the side ramp's danger (1 normal, 1.5 near the finish)
    bool  outside = false;        // flew over the guard rail
    float outS = 0.0f;            // where it left the road

    // The CPU driver's mind (shared/aidriver.*): skill, personality, mood, tactics, mistakes
    float aiSkill = -1.0f;        // 0..1 precision (-1: not met yet - set the first time it thinks)
    int   aiPersona = -1;         // Persona (-1: not met yet)
    int   aiMood = 0;             // Mood
    int   aiMoodTarget = -1, aiMoodUntil = 0;   // revenge: on this car, until
    int   aiRevengeHit = -1;      // the hit it last decided about
    int   aiTactic = 0, aiTacticCar = -1, aiTacticUntil = 0;
    int   aiMistake = 0, aiMistakeUntil = 0, aiMistakeSide = 1;
    int   aiPressure = 0;         // ticks with a car sitting close behind
    float aiLineErr = 0.0f, aiLineGoal = 0.0f;   // metres off the best line, and where that is going
    int   aiLineUntil = 0;
    // The race director's levers on this CPU (shared/director.cpp)
    int   dirRole = 0;            // 1: a person's rival
    int   dirRivalOf = -1;        // ... whose
    int   dirMode = 0;            // DirMode
    int   dirTarget = -1;         // the person it is racing
    float dirEffort = 0.0f;       // + / - share of top speed it commits to
    float dirMistake = 1.0f;      // x its mistakes
    int   dirRamp = 0;            // + % side ramps
    int   dirBoostHold = 0, dirBoostEvery = 0;   // L boost: at least this long, about this often (0: its own way)
    int   dirCover = 0;           // cover this passing side of the person behind
    int   dirCut = -1;            // 1: take the shortcut too
    int   dirPressure = -1;       // tailgate this person
    int   dirSurpriseUntil = 0;   // a surprise attack until this tick
    int   rosterId = -1;          // a named driver (shared/roster.h), -1: none
    // Its evolved brain (shared/brain.h): which species, and who drove this tick
    int   aiBrain = -1;           // BrainKind (-1: the rules alone)
    bool  aiBrainDriving = false; // the brain drove this tick (else the rules)
    int   aiBrainOff = 0;         // ticks left of the safety net (the rules took over)
    int   aiBrainBad = 0;         // ticks the brain has been off the road or pointing wrong
    float aiBrainSteer = 0.0f;    // its last steering output (it sees it next tick)
    float aiBrainRamp = 0.0f;     // how keen it is on side ramps (a slow average of its output)
    int   aiBrainTicks = 0, aiRulesTicks = 0, aiTakeovers = 0;   // this race (tests, F4)
    bool  aiClone = false;        // drives with the clone of a person's driving (GameSim::setCloneBrain)
    float aiCloneSteer = 0.0f, aiClonePedal = 0.0f;   // its intents turned into key taps (error diffusion)
};

// ---- Maps ----
// Each map has its own circuit (shared/circuits.cpp) and a scenery theme. The theme
// decides which props the sim places; the client also uses it for sky and lighting.
enum class MapTheme { Countryside, Desert, Snow, City, Coast };

struct MapDef {
    const char *name;
    const char *tagline;
    MapTheme theme;
    int   trafficCount;
    float obstacleGapMin, obstacleGapMax; // spacing between obstacle patterns
    int   stars;                     // 1..3 difficulty shown in the menu
};

constexpr int NUM_MAPS = 6;
extern const MapDef MAPS[NUM_MAPS];

// Road traffic drives round the circuit: s along the lap, lane = metres right of the centre
struct Traffic {
    float lane = 0.0f, targetLane = 0.0f;
    float s = 0.0f, speed = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    bool  truck = false;
};

struct Obstacle {
    enum Type { Cone, Barrel, Barrier, Tyres, Concrete };   // (tyres: a stack you knock away; concrete: a block that never moves)
    Type  type = Cone;
    bool block() const { return type == Barrier || type == Concrete; }   // stays put, the car is pushed out
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float hx = 0.0f, hz = 0.0f;   // Half extents (barriers only)
    float vx = 0.0f, vy = 0.0f, vz = 0.0f; // Velocity when knocked away
    float rot = 0.0f, spin = 0.0f;
    bool  knocked = false;
    float s = 0.0f, d = 0.0f;     // where it was put on the circuit (barriers stay there)
    int   downTicks = 0;          // ticks since it was knocked over (it is put back later)
    int   fromLap = 0;            // only there from this lap on (more on the last lap)
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
    float s = 0.0f, d = 0.0f;     // where it is on the circuit
    int   downTicks = 0;          // ticks since it was picked up (it comes back 1 s later)
    int   takes = 0;              // how many cars took it this race (each next one gets a longer boost)
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
        NitroPickupEvent,
        Landing,          // intensity = how hard, m/s against the slope; count = ticks in the air
        DirtDust,         // dust from a car on a dirt shortcut
        Damage,           // carIndex lost intensity health; count = who or what (a car, or a Cause);
                          // highPitch = it was this car's fault
        Wrecked,          // carIndex is wrecked; count = the car whose fault it was (-1: none);
                          // highPitch = the driver was thrown out
        Respawn,          // carIndex is back in the race
        Eliminated,       // carIndex is out: wrecked twice in a short time; count = what took it (as Damage)
        Bounty,           // carIndex won +15% for the most damage to car `count`, which was wrecked
        Stunt,            // carIndex landed a jump: count = Stunt kind, intensity = damage taken
        Splash,           // a car came down off the road: count 0 water, 1 ground, 2 a gorge
        Boost,            // carIndex started an L boost
        Taunt             // carIndex (a named driver) says something to car vx: count = TauntMoment, intensity = variant
    };
    enum StuntKind { StuntClean = 0, StuntRoll = 1, StuntSaved = 2, StuntRough = 3, StuntCrash = 4 };
    // what a car hit, for Damage events (count), besides another car's index
    enum Cause { ByNone = -1, ByTraffic = -2, ByBarrier = -3, ByRail = -4, ByLanding = -5, ByObject = -6,
                 ByWater = -7, ByOffroad = -8, ByRampSide = -9, ByBoost = -10 };

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
