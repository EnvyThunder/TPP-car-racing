#ifndef GAMESIM_H
#define GAMESIM_H

#include "simtypes.h"
#include "track.h"
#include "playermodel.h"
#include "aidriver.h"
#include "brain.h"
#include <vector>
#include <random>

class QDataStream;

namespace sim {

class GameSim {
public:
    enum State { Menu, Countdown, Racing, Paused, Finished };
    enum GameMode { ModeSingle, ModeSplitScreen, ModeOnline };

    explicit GameSim(uint32_t seed = 2024);

    // racers = number of cars on the grid (split screen 2; vs CPU and online up to MAX_RACERS)
    void reset(uint32_t seed = 2024, int racers = 2);
    void startRace(GameMode mode, int racers = 2);
    // runs (online server, optional): how many inputs each car uses this tick - 0 while its
    // player's next input hasn't arrived (the car waits), 2 to catch up; extra = the second one
    void step(const PlayerInput inputs[], int inputCount, const int runs[] = nullptr,
              const PlayerInput extra[] = nullptr);

    // Race position (1 = leading) of car idx among all cars
    int rankOf(int idx) const;

    // Fingerprint of the track as built by reset(): obstacles, nitro cans, starting
    // traffic, scenery count. Online, every player's copy must match the host's.
    uint32_t worldChecksum() const;
    std::string worldParts() const;     // (diagnosis: the fingerprint part by part, to see where two systems differ)

    // ---- Network snapshots (server -> clients) ----
    // Only the parts that change during a race are sent; the track, props and the
    // starting traffic are rebuilt identically on every machine from the map + seed.
    // mem (server side, one per receiver) remembers what their earlier snapshots said: obstacle
    // updates go only for the ones that moved, each car's rare facts only now and then.
    struct SnapshotMemory {
        std::vector<Obstacle> obstacles;
        std::vector<uint32_t> slowHash;
        std::vector<int> slowAge;
        int count = 0;
    };
    // extras: obstacle changes and effects go there instead (the fast lane: they must not be lost)
    void writeSnapshot(QDataStream &out, const std::vector<SimEvent> &events, SnapshotMemory *mem,
                       QDataStream *extras = nullptr, int exact = -1) const;   // exact: the receiver's own car, unrounded
    void readSnapshot(QDataStream &in, std::vector<SimEvent> &eventsOut);
    void readExtras(QDataStream &in, std::vector<SimEvent> &eventsOut);

    uint64_t checksum() const;

    // ---- The circuit -------------------------------------------------------------
    // Everything on it is placed by s (metres along the lap, 0 = start/finish line) and
    // d (metres right of the centre line). World: x east, z south (the layout pictures of
    // tools/track_preview have north up, so they match what you see when driving).
    const Track &circuit() const { return track; }
    float lapLength() const { return lapLen; }                   // (a sprint: the whole road)
    int getLaps() const { return laps; }
    // every circuit race is 2 laps (the owner found 3 too long); n is ignored
    static constexpr int LAPS = 2;
    void setLaps(int) { laps = LAPS; }
    // the race ends this long after the 4th car (or every car, with fewer) has crossed the line
    static constexpr int FINISHERS = 4, FINISH_WAIT = 120;
    // A nitro can is back 1 s after it was taken, in the same place; each next car to take it
    // gets a longer boost: 2.0, 2.3, 2.5, 2.8 s, then 3.0 s for everyone after
    static constexpr int NITRO_BACK = 60;
    static int nitroLenFor(int take) { static const int LEN[5] = {120, 138, 150, 168, 180}; return LEN[std::clamp(take, 1, 5) - 1]; }
    // A jump: every launch off a ramp is a little stronger than it used to be, and each next
    // car over the same ramp is thrown a little higher, up to the 4th (the same after that)
    // Together with JUMP_CARRY a jump covers about 35% more road than it did: worth the risk.
    static float jumpMulFor(int jumper) { static const float MUL[4] = {1.25f, 1.32f, 1.39f, 1.46f}; return MUL[std::clamp(jumper, 1, 4) - 1]; }
    static constexpr float JUMP_CARRY = 1.16f;                    // in the air the car covers this much more ground
    static constexpr float STEER_RATE = 0.024f;                   // radians a tick at full lock (was 0.030)
    static constexpr float STEER_TAP  = 0.6f;                     // a key just pressed turns this much, full after a few ticks
    // A sprint (Sunset Valley, Coastal Highway) is one open road raced once from the start
    // line to the finish line: no laps, s clamped to the road's ends
    bool sprint() const { return track.sprint(); }
    static bool isSprintMap(int m) { return draftCircuits()[size_t((m % NUM_MAPS + NUM_MAPS) % NUM_MAPS)].sprint; }
    float startLine() const { return track.startS(); }           // s of the start line (a circuit: 0)
    float finishLine() const { return sprint() ? track.finishS() : 0.0f; }
    float raceDistance() const { return sprint() ? track.finishS() - track.startS() : float(laps) * lapLen; }
    // Every road function takes the road: 0 the main loop (s wraps round the lap), 1 the
    // shortcut (an open road from its fork to its merge, s clamped to its ends)
    float wrapS(float s) const;                                   // into [0, lap length) (a sprint: clamped)
    float aheadOf(float from, float to) const;                    // forward distance along the lap (+/- half a lap)
    void pointAt(float s, float d, float &x, float &z, int path = 0) const;   // world position
    float headingAt(float s, int path = 0) const;                 // road direction (car heading convention)
    float roadHalfAt(float s, int path = 0) const;                // half the road width there
    float heightAt(float s, float d, int path = 0) const;         // height of the road surface (banking and ramps included)

    // The shortcut (milestone 1d part B): leaves the main road at shortcutFrom(), joins it
    // again at shortcutTo(); its s runs from 0 to shortcutEnd()
    bool hasShortcut() const { return track.shortcut().size() > 2; }
    // The shortcut is closed to the leaders: the first SHORTCUT_CLOSED_TOP places can't take
    // it (in a race of that many cars or fewer, everyone but the last). It holds on every map
    // that has a shortcut, new ones too. Who is shut out is decided until 60 m before the fork
    // and then kept until 110 m past it (no gate slamming shut on a car already turning in);
    // a car already on the shortcut stays on it.
    static constexpr int SHORTCUT_CLOSED_TOP = 3;
    float toShortcut(const Car &c) const { return hasShortcut() && c.path == 0 ? aheadOf(c.s, shortcutFrom()) : 1e9f; }
    // where on the shortcut it has left the main road's asphalt (the closed sign stands there)
    float shortcutGateS() const;
    float shortcutFrom() const { return track.shortcutFromS(); }
    float shortcutTo() const { return track.shortcutToS(); }
    float shortcutEnd() const { return float(track.shortcut().size()) - 1.0f; }
    float progressOf(const Car &c) const;                         // laps x lap length + distance into the lap

    // Ramps (milestone 1c): the road rises over `run` metres to a lip `height` metres above
    // its normal profile, then drops back down `back` metres of steep slope
    struct Ramp { float lip = 0.0f, run = 30.0f, height = 0.0f, back = 0.0f; };
    static constexpr int EXTRA_RAMPS = 2;                         // found on each map's straights (setMap)
    const std::vector<Ramp> &ramps() const { return rampList; }
    bool nearRamp(float s, float before, float after) const;     // within that many metres of a lip

    // Side ramps (Asphalt-style kickers in one lane at the road's edge): the car flies high
    // and far, a twisted one (twist +/- 1) rolls it over in the air. More of them, higher and
    // more dangerous, in the last third of the road; on a circuit's last lap more still
    // (fromLap). The lane rises from s - run to the lip at s, then drops straight back down.
    struct SideRamp { float s = 0.0f, d = 0.0f, half = 2.2f, run = 16.0f, height = 1.4f, twist = 0.0f, danger = 1.0f; int fromLap = 0; };
    const std::vector<SideRamp> &sideRamps() const { return sideList; }
    bool present(int fromLap) const { return fromLap <= leadLap; }   // last-lap extras appear when the leader starts it
    float sideLift(float s, float d) const;                       // a side ramp's height above the road there
    float surfaceAt(float s, float d, int path = 0) const { return heightAt(s, d, path) + (path ? 0.0f : sideLift(s, d)); }
    bool overWater(const Car &c) const;                          // off the road here: water below (a river, the sea)
    bool overGorge(float s) const;                                // a bridge with no water: a deep drop
    float lastThird() const;                                      // s where the dangerous last third of the road begins
    bool nearJoin(float s) const;                                 // around the shortcut's fork or merge
    float gradeAt(float s, int path = 0) const;                   // rise per metre along the road
    float bankAt(float s, int path = 0) const;                    // sine of the banking (+ = right edge lower)
    // height of the road surface below (x, z), searching near sHint (things knocked off their place)
    float heightNear(float x, float z, float sHint, int path = 0) const;
    // the nearest point of the centre line to (x, z), searched near sHint: its s and d
    // (wide: if nothing is near the hint, search the whole road)
    void project(float x, float z, float sHint, float &s, float &d, int path = 0, bool wide = true) const;
    int sliceNear(float x, float z, float reach) const;           // nearest slice within reach, or -1
    void locate(Car &c) const;                                    // c.s, c.d, c.lap, c.progress from c.x, c.z
    float roadDistance(float x, float z, float reach) const;      // to the nearest centre line, if within reach (scenery)
    float seaLineX() const { return seaX; }                       // Coastal Highway: the sea is east of this
    // the temple: a landmark beside the start/finish straight, clear of every road
    void landmark(float &x, float &z, float &rotDeg) const { x = landX; z = landZ; rotDeg = landRot; }

    // Road width of this race (set by reset): 3 lanes for 2 cars, 5 for a pack
    float roadHalf() const { return halfWidth; }
    float railOffset() const { return halfWidth + 4.0f; }   // guard rails
    int laneCount() const { return lanes; }
    float laneX(int i) const { return (float(i) - 0.5f * float(lanes - 1)) * LANE_WIDTH; }   // lane centre from the road centre

    // Simulation states & accessors
    State getState() const { return state; }
    void setState(State s) { state = s; }
    GameMode getGameMode() const { return mode; }
    void setGameMode(GameMode m) { mode = m; }

    // the challenge (shared/playermodel.h): above 0 the CPUs also make fewer mistakes and take more
    // of the jumps, the shortcut and their boosts; at most a little sharper or duller around the skill
    void setCpuChallenge(float c) { cpuChallenge = std::clamp(c, -0.4f, 1.0f); }
    int getAiDifficulty() const { return aiDifficulty; }
    void setAiDifficulty(int d) { aiDifficulty = d; }

    // Map selection (takes effect at the next startRace / reset)
    int getMap() const { return mapIndex; }
    void setMap(int m);                                           // also builds the map's circuit
    const MapDef &mapDef() const { return MAPS[mapIndex]; }

    int getTick() const { return tick; }
    float getRaceTimer() const { return raceTimer; }
    int getWinner() const { return winner; }

    const std::vector<Car>& getCars() const { return cars; }
    std::vector<Car>& getCars() { return cars; }

    const std::vector<Traffic>& getTraffic() const { return traffic; }
    std::vector<Traffic>& trafficMut() { return traffic; }   // online clients move traffic between snapshots
    const std::vector<Obstacle>& getObstacles() const { return obstacles; }
    const std::vector<Prop>& getProps() const { return props; }
    const std::vector<NitroPickup>& getNitroPickups() const { return nitroPickups; }

    const std::vector<SimEvent>& getEvents() const { return events; }
    void clearEvents() { events.clear(); }

    void updateCar(Car &c, const PlayerInput &in, int carIndex);
    void updateAI(Car &c, int carIndex);
    // The CPU driver's decision for this tick. asRival = apply the difficulty level and
    // catch-up help against player 1; false = neutral driver at normal top speed.
    PlayerInput computeAIInput(Car &c, int carIndex, bool asRival);
    PlayerInput computeAIDrive(Car &c, int carIndex, bool asRival);   // (what it would do without looking for mines)
    void collideWorld(Car &c, int carIndex);
    // Client-side prediction only: bump into barriers and traffic like collideWorld does,
    // but without knocking anything over or making events (the server decides those).
    void predictCollisions(Car &c) const;
    // online: my car bumping another racer on my own screen at once (only my car moves, as
    // the server will move it - half the overlap, the speeds shared when one runs into the
    // other's back; the damage stays the server's). true: they touched
    bool predictBump(Car &me, const Car &other) const;
    void collideCars(Car &a, Car &b, int idxA, int idxB);
    bool knock(Obstacle &o, float x, float z, float fx, float fz, float speed, int carIndex);

    // ---- Crashes: health, blame, wrecks ----------------------------------------------
    // A wreck tumbles and slides to the verge for WRECK_SLIDE ticks, then waits; the car is
    // back WRECK_TIME ticks after the crash (WRECK_TIME_EJECT when the driver was thrown out
    // and has to run back), and drives through other cars for GHOST_TIME more.
    static constexpr int WRECK_SLIDE = 75;
    static constexpr int WRECK_TIME = 120;
    static constexpr int WRECK_TIME_EJECT = 180;
    static constexpr int GHOST_TIME = 90;
    int wreckTime(const Car &c) const { return c.ejected ? WRECK_TIME_EJECT : WRECK_TIME; }
    void forceWreck(int idx, bool eject);                         // story scenes and tests

    // ---- L boost, bounty -----------------------------------------------------------------
    // Holding L gives BOOST_GAIN more top speed for as long as it is held (not in split
    // screen). It costs health every tick, more the longer L has been held without a break:
    // BOOST_RATE[0] a second for the first 1.5 s, BOOST_RATE[1] from 1.5 to 3 s, BOOST_RATE[2]
    // from 3 to 5 s, and at 5 s the engine burns out: the car is wrecked (the owner's numbers).
    // There is no elimination (the owner found it too harsh): a wrecked car is always back
    // in the race 2-3 s later. When a car is wrecked, the car that did it the most damage
    // since its last wreck gets BOUNTY health.
    // every hit's damage is scaled by this (the owner asked for gentler crashes)
    static constexpr float DAMAGE_SCALE = 0.6f;
    // landings (jumps and flips) cost about a third of what they did (the owner asked for jumps
    // to be gentle); a clean landing or roll costs nothing
    static constexpr float LANDING_DAMAGE = 0.35f;
    // hitting obstacles and barriers costs this much more than it used to (the owner asked;
    // crashes between cars are unchanged), and there are more of them: gaps x OBSTACLE_GAP
    static constexpr float OBSTACLE_DAMAGE = 1.5f, OBSTACLE_GAP = 0.6f;
    // crashes into barriers, obstacles and other racers cost more the further ahead a car is:
    // x POS_DAMAGE_FRONT for the leader down to x POS_DAMAGE_BACK for the last car
    static constexpr float POS_DAMAGE_FRONT = 1.25f, POS_DAMAGE_BACK = 0.80f;
    float positionDamage(int idx) const;
    static constexpr float BOOST_GAIN = 0.22f;   // (the owner: 22%)
    static constexpr float BOOST_RATE[3] = {10.0f, 15.0f, 25.0f};   // health a second
    static constexpr int   BOOST_MID = 90, BOOST_HIGH = 180, BOOST_BURN = 300;   // 1.5 s, 3 s, 5 s (60 ticks a second)
    static constexpr float BOUNTY = 15.0f;
    bool boostAllowed() const { return mode != ModeSplitScreen; }
    static int boostStage(const Car &c) { return c.boostHold >= BOOST_HIGH ? 2 : c.boostHold >= BOOST_MID ? 1 : 0; }   // (BOOST_BURN: burnt out)
    // ---- The CPU driver's mind (shared/aidriver.*) --------------------------------------
    // The AI seed makes every race's personalities, skills and choices different (the
    // track itself always comes from reset's seed); the skill centre is the profile skill the
    // CPUs spread around (offline yours, online the room's people). Set both before startRace.
    void setAiSeed(uint32_t s);
    void setCpuSkill(float centre);
    float cpuSkill() const { return cpuSkillCentre; }
    AiView aiThink(Car &c, int idx, bool cpu);

    // ---- Evolved brains (shared/brain.h) -------------------------------------------------
    // On in the game; tests switch them off to compare with the rules alone. The trainer
    // (tools/evolve) gives each car the brain it is scoring (nullptr: its own species).
    void setBrains(bool on) { brainsOn = on; }
    bool brainsEnabled() const { return brainsOn; }
    void setBrainOverride(int car, const Brain *b, int kind) { if (car >= 0 && car < MAX_RACERS) { brainOver[car] = b; brainOverKind[car] = kind; } }
    // the brain's inputs for this car (tools and the F4 view; hazard = nearest hard hazard)
    void brainInputs(const Car &c, int idx, float hzDist, float hzMid, float out[BRAIN_IN]) const;
    // The clone of a person's driving (shared/clone.h): the brain cars with aiClone drive with
    // (nullptr: none). cloneView = what car idx sees, as a clone sees it (prevSteer: the
    // steering it pressed last, -0.8 / 0 / +0.8); true on the open road, where it is recorded
    void setCloneBrain(const Brain *b) { cloneBrain = b; }
    bool cloneView(int idx, float prevSteer, float out[BRAIN_IN]) const;

    // ---- The race director (shared/director.cpp) ----------------------------------------
    // The people's profiles (set before or after startRace; kept until changed), and a
    // switch for tests (it is always on in the game)
    void setPlayerHints(int car, const PlayerHints &h);
    void clearPlayerHints();
    void setDirector(bool on) { directorOn = on; }
    bool isPerson(int i) const;                                   // a car driven by a person
    // what happened between a person and each named driver this race (the rivals record)
    std::vector<RivalRace> rivalRaces(int person) const;
    static constexpr int TAUNT_GAP = 20 * 60;                     // at most one taunt every 20 s per person
    int leaderLap() const { return leadLap; }

    // ---- The player model (shared/playermodel.h) -------------------------------------
    // What each car did this race (every car is measured; the game keeps profiles for the
    // people). The clean reference: the CPU driver alone on the map with nothing on the road,
    // the time of each tenth of the lap (or of the sprint) in ticks - worked out once per map.
    const RaceObs &raceObs(int i) const { static const RaceObs none; return i >= 0 && i < int(obsList.size()) ? obsList[size_t(i)] : none; }
    static const std::array<float, PM_SECTORS> &referenceSectors(int map);

private:
    // takes health off; other = the other car or a SimEvent::Cause; impact = how hard (m per tick)
    void damage(Car &c, int idx, float amount, int other, bool atFault, float impact);
    void startWreck(Car &c, int idx, int cause, bool eject, bool takesLife = true);
    void healthGone(Car &c, int idx, int cause, bool eject);     // health ran out: wreck or elimination
    void landOutside(Car &c, int idx);
    int airTimeLeft(const Car &c) const;                         // ticks until a flying car lands                            // came down beyond the guard rail
    void placeSideRamps(uint32_t seed);
    void updateLeadLap();
    void updateShortcutGates();                                   // who the shortcut is closed to (each tick)
    void observeStep();                                           // the player model's per-tick measurements
    RaceObs *obsOf(int idx) { return idx >= 0 && idx < int(obsList.size()) ? &obsList[size_t(idx)] : nullptr; }
    std::vector<RaceObs> obsList;
    float pairRel[MAX_RACERS][MAX_RACERS] = {};                   // how far each car was ahead of each other one
    float dPrev[MAX_RACERS] = {};                                  // each car's d a quarter-second ago (blocking)
    void wreckStep(Car &c, int idx);                              // a wrecked car's tick
    int pairCool[MAX_RACERS][MAX_RACERS] = {};                    // ticks before two cars can hurt each other again

    bool hitBarrier(Car &c, const Obstacle &o) const;            // push out of a barrier, true on contact
    bool hitTraffic(Car &c, const Traffic &t) const;             // push out of a traffic car, true on contact
    void moveOnTrack(Car &c, float ds, float dd) const;          // nudge along / across the road
    void fly(Car &c, float along, int carIndex);                 // follow the road, take off, fly, land
    float rnd(float a, float b);

    State state = Menu;
    GameMode mode = ModeSingle;
    int aiDifficulty = 1;
    float cpuChallenge = 0.0f;
    int mapIndex = 0;
    float halfWidth = ROAD_HALF;
    int lanes = 3;
    int laps = LAPS;
    int topDoneTick = -1;                                         // when the 4th car finished

    Track track;
    float lapLen = 1.0f;
    float seaX = 1e9f;
    float landX = 0.0f, landZ = 0.0f, landRot = 0.0f;
    std::vector<float> bankSin;                                   // per slice
    std::vector<float> lift, liftGrade;                           // ramps: extra height and rise per slice
    std::vector<Ramp> rampList;
    std::vector<SideRamp> sideList;
    std::vector<int> sideJumps, mainJumps;                        // cars that have flown off each ramp this race
    std::vector<int16_t> sideAt;                                  // per slice: side ramp index + 1 (0: none)
    uint32_t raceSeed = 2024;                                     // the seed reset() was given
    uint32_t aiSeed = 1;                                          // this race's CPU minds (setAiSeed)
    float cpuSkillCentre = 0.6f;                                  // (setCpuSkill)
    void aiMeet(Car &c, int idx);                                 // personality and skill, the first time
    void aiMood(Car &c, int idx, const PersonaDef &P);
    void directorRoles();
    void directorStep();                                          // once a second
    std::array<PlayerHints, MAX_RACERS> hints{};
    std::array<bool, MAX_RACERS> surpriseUsed{};
    std::array<int, MAX_RACERS> tauntTick{}, tauntBy{};           // a person's last taunt: when, from whom
    void taunt(int speaker, int moment, int target);
    int attitudeOf(const Car &c, int person) const;               // what this named driver thinks of that person
    bool rolesSet = false;
    bool directorOn = true;
    bool brainsOn = true;
    const Brain *cloneBrain = nullptr;
    void hazardAhead(const Car &c, int idx, float &dist, float &mid) const;   // (the clone's view of the road ahead)
    const Brain *brainOver[MAX_RACERS] = {};
    int brainOverKind[MAX_RACERS] = {-1, -1, -1, -1, -1, -1};
    int leadLap = 0;                                              // the leader's lap (circuits; a sprint: 0)
    std::vector<float> cutMain;                                   // per shortcut slice: metres past the fork it counts as
    float forkReach = 0.0f, mergeReach = 0.0f;                    // main-road metres the shortcut runs close beside
    void nearestSlice(float x, float z, int hint, int &slice, float &t, int path = 0, bool wide = true) const;
    const std::vector<Slice> &road(int path) const { return path ? track.shortcut() : track.slices(); }
    void sliceAt(float s, int path, int &i, int &j, float &f) const;   // the slice s falls in, and towards the next
    // the road a car follows, `ahead` metres on: past the shortcut's end the main road; along
    // the shortcut once the fork is reached if the car (a CPU) plans to take it
    void alongRoad(const Car &c, float ahead, int &path, float &s) const;
    void aheadPoint(const Car &c, float ahead, float d, float &x, float &z) const;
    float headingAhead(const Car &c, float ahead) const;
    bool insideOtherRoad(const Car &c) const;                    // where a shortcut leaves or joins: inside the other road
    // slices by 32 m square for roadDistance()
    float gridX0 = 0, gridZ0 = 0;
    int gridW = 0, gridH = 0;
    std::vector<std::vector<int>> grid;

    int tick = 0;
    float raceTimer = 0.0f;
    int winner = 0;

    std::vector<Car> cars;
    std::vector<Traffic> traffic;
    std::vector<Obstacle> obstacles;
    std::vector<Prop> props;
    std::vector<NitroPickup> nitroPickups;
    std::vector<SimEvent> events;

    std::mt19937 rng;
};

} // namespace sim

#endif // GAMESIM_H
