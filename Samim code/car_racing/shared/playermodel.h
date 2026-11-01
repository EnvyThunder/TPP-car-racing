#ifndef PLAYERMODEL_H
#define PLAYERMODEL_H

// The player model (docs/cpu-psychology/README.md): what each driver does in a race, turned
// into a profile - skill, consistency, aggression, risk, and live tilt and confidence - plus
// habits per map. GameSim fills a RaceObs for every car while it races (plain C++, so the
// online server measures its players exactly like your own game does offline); the game
// keeps a PlayerProfile per player name and blends each race into it.
//
// Milestone 1 only measures and shows the profile; the CPU drivers start using it later.

#include <array>
#include <string>
#include <vector>

namespace sim {

constexpr int PM_SECTORS = 10;       // a lap (or a sprint) is timed in tenths
constexpr int PM_BOOST_BINS = 20;    // where on the lap the L boost is used, in twentieths
constexpr int PM_MAPS = 16;          // habits are kept for this many maps (at least NUM_MAPS: room for new maps)
constexpr int PM_CARS = 6;           // (MAX_RACERS)
constexpr int PM_ROSTER = 12;        // (ROSTER_SIZE: the named CPU drivers)

// Everything one car did in one race. All counts are for this race only.
struct RaceObs {
    // how much it raced
    float metres = 0.0f;             // distance raced (forwards)
    int   ticks = 0;                 // ticks racing (not finished, not out)

    // pace: each finished tenth of a lap, the reference time over its own time (1 = as fast
    // as the CPU on a clean road; a tenth with a wreck in it doesn't count)
    std::vector<float> sectorRatio;
    std::vector<int>   sectorIndex;  // which tenth (0..9) each one was
    int   secGlobal = -1;            // lap x 10 + tenth the car is in (-1: not started)
    int   secStartTick = 0;
    bool  secSpoiled = false;        // a wreck in this tenth

    // mistakes
    int rail = 0, offroad = 0, barrier = 0, objects = 0, traffic = 0;
    int roughLandings = 0, crashLandings = 0, flewOff = 0, wrecks = 0;
    int offroadTicks = 0;
    bool offNow = false;             // off the asphalt right now (entering it counts as a mistake)

    // aggression
    int rams = 0;                    // hits on other racers that were its fault
    std::array<int, PM_CARS> ramsOn{};   // ... on each car
    int closePasses = 0;             // passed a car with less than 1.5 m between them
    int blocks = 0;                  // moved into the lane of a car right behind
    int tailgateTicks = 0;           // within 10 m behind another racer, in its lane
    int revenge = 0;                 // rammed a car within 10 s of being hit by it

    // risk
    int rampsOffered = 0, rampsTaken = 0, bigRampsTaken = 0, nitroJumps = 0;
    int landings = 0;                // side-ramp landings
    float landHurt = 0.0f;           // damage from them
    int boosts = 0, boostTicks = 0, boostLongest = 0, boostOver3 = 0, boostOver10 = 0;
    int boostHoldNow = 0;            // ticks of the boost being held right now

    // habits
    int forkOffered = 0, forkTaken = 0;
    int passLeft = 0, passRight = 0;
    std::array<int, PM_BOOST_BINS> boostBins{};

    // live mood (changes during the race)
    float tilt = 0.0f;               // 0..1: frustration - wrecks and mistakes close together
    float confidence = 0.3f;         // 0..1: leading, clean driving
    int   lastWreckTick = -1000000;
    int   lastMistakeTick = -1000000;
    int   mistakeStreak = 0;
    int   lastHitBy = -1, lastHitTick = -1000000;
    int   leadTicks = 0;
    std::array<int, 20> rankAgo{};   // its place each of the last 20 seconds (0: not yet)
};

// The slow traits, 0..1
struct Traits {
    float skill = 0.5f, consistency = 0.5f, aggression = 0.3f, risk = 0.4f;
};

// One race's traits (consistency only when there were laps to compare)
struct RaceTraits : Traits {
    bool hasConsistency = false;
    bool valid = false;              // raced far enough to say anything (at least 1 km)
};
RaceTraits raceTraits(const RaceObs &o);

// Live mood, every tick (GameSim calls these)
void moodDecay(RaceObs &o);                                   // tilt fades (half in 30 s)
void moodMistake(RaceObs &o, int tick, float weight = 1.0f);  // a mistake
void moodDamage(RaceObs &o, float amount);                    // health lost
void moodWreck(RaceObs &o, int tick);
void moodSecond(RaceObs &o, int rank, int cars, float gapAhead, float gapBehind);   // once a second

struct MapHabits {
    int forkOffered = 0, forkTaken = 0, passLeft = 0, passRight = 0, rampsOffered = 0, rampsTaken = 0;
    int boosts = 0;
    std::array<int, PM_BOOST_BINS> boostBins{};
};

// One race against one named driver (for the rivals record)
struct RivalRace {
    int rosterId = -1;
    int ramsByYou = 0, ramsOnYou = 0;
    bool youAhead = false;           // you finished (or were) ahead of them
    bool close = false;              // within 3 s (or 100 m) of each other at the end
};

// Everything between you and one named driver, over all races
struct RivalRecord {
    int races = 0, ramsByYou = 0, ramsOnYou = 0, youAhead = 0, theyAhead = 0, close = 0;
};
// What a named driver thinks of you, from the record
enum Attitude { AttitudeNone, AttitudeGrudge, AttitudeDetermined, AttitudeCocky };

struct RaceSummary {
    int map = 0, place = 0, cars = 0, wrecks = 0, rams = 0;
    float bestLap = 0.0f, km = 0.0f;
    bool eliminated = false;
};

struct PlayerProfile {
    std::string name;
    int races = 0;
    Traits traits;
    float sure = 0.0f;               // 0..1: how far the profile can be trusted (more races, surer)
    std::array<MapHabits, PM_MAPS> habits;
    std::vector<RaceSummary> recent; // newest first, at most 10
    std::array<RivalRecord, PM_ROSTER> rivals{};   // per named driver

    // a grudge (you rammed them a lot), determined (you keep beating them), cocky (they keep
    // beating you); the nemesis is the one with the most close battles (-1: none yet)
    int attitude(int rosterId) const;
    int nemesis() const;

    // blends one race in (the first races count more, so it learns quickly, then settles)
    void addRace(const RaceObs &o, const RaceSummary &s, const std::vector<RivalRace> &rivalsHere = {});
    // the habits as short sentences for the profile screen
    std::vector<std::string> habitLines(const char *const mapNames[PM_MAPS]) const;
};

} // namespace sim

#endif // PLAYERMODEL_H
