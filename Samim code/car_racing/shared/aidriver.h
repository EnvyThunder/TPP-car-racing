#ifndef AIDRIVER_H
#define AIDRIVER_H

// The CPU driver's mind (docs/cpu-psychology/README.md, milestone 2). There is no difficulty
// level and no speed help: every car has the same top speed. A CPU differs by
//   - skill (0..1): how precisely it drives - its line, its braking point, how early it sees
//     what is ahead, how cleanly it lands, how well it times nitro and boosts;
//   - personality: what it likes to do (attack, block, save it all for the end, show off on
//     the ramps, drive clean, hold a grudge, or be unpredictable);
//   - mood, which changes during the race (confident, pressured, shaken, revenge, desperate);
//   - tactics, picked ten times a second with weighted randomness and kept for a moment;
//   - human mistakes, more of them under pressure.
// GameSim::aiThink works out an AiView every tick; computeAIInput drives by it. All the
// randomness comes from hashes of the race's AI seed, the tick and the car, so the online
// server (the only one running the CPUs) is exact and repeatable.

#include <cstdint>
#include <array>
#include "roster.h"

namespace sim {

enum Persona { PersonaAttacker, PersonaBlocker, PersonaLateCharger, PersonaShowOff, PersonaClean,
               PersonaGrudge, PersonaWildCard, NUM_PERSONAS };
enum Mood { MoodCalm, MoodConfident, MoodPressured, MoodShaken, MoodRevenge, MoodDesperate, NUM_MOODS };
enum Tactic { TacticFollow, TacticPassLeft, TacticPassRight, TacticWait, TacticDefend, TacticAttack, NUM_TACTICS };
enum Mistake { MistakeNone, MistakeLateBrake, MistakeWide, MistakeLateSee, MistakeOverBoost };
// what the race director (director.cpp) has a CPU doing about the person it races
enum DirMode { DirNone, DirHunt, DirEase, DirSurprise, DirPressure, DirCounter, DirGiveRoom, NUM_DIR_MODES };
const char *directorModeShort(int m);

// What the director knows of a person from their profile (offline yours, online the server's)
struct PlayerHints {
    bool  known = false;
    float sure = 0.0f;             // how far the profile can be trusted
    float skill = 0.6f;            // the player model's skill
    float shortcut = 0.0f;         // share of this map's shortcut taken
    float passLeft = 0.5f;         // share of passes on the left
    float ramps = 0.5f;            // share of side ramps taken
    int   boostBin = -1;           // favourite boosting spot on this map (a twentieth of the lap; -1 none)
    float aggression = 0.3f, risk = 0.4f;
    std::array<int8_t, ROSTER_SIZE> attitude{};   // what each named driver thinks of them (Attitude)
    int   nemesis = -1;            // the named driver they battle most closely
};

struct PersonaDef {
    const char *name, *shortName;
    float aggression;     // wants to pass, accepts contact (0..1)
    float block;          // defends its line
    float rampLove;       // side ramps, twisted ones
    float boostLove;      // L boost: how often, how long
    float randomness;     // how loosely it picks its tactics (weighted-random "temperature")
    float grudge;         // turns to revenge when hit
    float mistakes;       // its share of mistakes (x)
    bool  lateCharger;    // saves nitro and boosts for the last third
};
const PersonaDef &personaDef(int p);
const char *moodName(int m);
const char *moodShort(int m);
const char *tacticShort(int t);
// the CPU precision that drives at the pace the player model measures as this skill
float cpuPrecisionFor(float measuredSkill);
// From the very first race the CPUs race you hard: a player the game doesn't know yet is taken
// for a quick one (NEW_PLAYER_SKILL: CPUs at about 93% of a clean lap's pace), and however
// slow a profile reads, they never drive below CPU_FLOOR (about 85%). They only get faster.
constexpr float NEW_PLAYER_SKILL = 0.93f;
constexpr float CPU_FLOOR = 0.62f;
// the CPUs' skill centre: from what the profile knows (sure 0..1) of the measured skill
float cpuCentreFor(float measuredSkill, float sure, float challenge);

// what the mind wants this tick (computeAIInput drives by it)
struct AiView {
    float skill = 1.0f;
    float lineErr = 0.0f;          // metres off the best line it drives
    float liftAt = 0.80f, brakeAt = 1.05f;   // corner speed: lift / brake at this share of the grip
    float horizonMul = 1.0f;       // how far ahead it sees hazards
    float rollSlack = 0.39f;       // how exactly it finishes a barrel roll (radians)
    float commit = 1.0f;           // share of top speed it holds on a straight
    int   rampChance = 50;         // % it takes a side ramp
    int   cutChance = 50;          // % it takes the shortcut
    int   passCar = -1, passSide = 0;   // overtaking this car, on this side (-1 left, +1 right)
    int   waitCar = -1;            // staying behind this car
    int   defendCar = -1;          // blocking this car (behind)
    int   attackCar = -1;          // driving into this car
    bool  saveNitro = false;       // keep the nitro for later
    int   boostHold = 90;          // how long it holds the L boost (ticks)
    int   boostEvery = 900;        // about how often it starts one (ticks)
    bool  boostAnyRoad = false;    // keeps holding into corners
    int   pressureCar = -1;        // tailgating this person (the director)
    int   coverSide = 0;           // defending: sit on this side of the person behind (-1 left, +1 right)
};

} // namespace sim

#endif // AIDRIVER_H
