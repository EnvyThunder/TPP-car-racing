#ifndef GAMESIM_H
#define GAMESIM_H

#include "simtypes.h"
#include <vector>
#include <random>

class QDataStream;

namespace sim {

class GameSim {
public:
    enum State { Menu, Countdown, Racing, Paused, Finished };
    enum GameMode { ModeSingle, ModeSplitScreen, ModeOnline };

    explicit GameSim(uint32_t seed = 2024);

    // racers = number of cars on the grid (2 for local modes, up to MAX_RACERS online)
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

    // ---- Network snapshots (server -> clients) ----
    // Only the parts that change during a race are sent; the track, props and the
    // starting traffic are rebuilt identically on every machine from the map + seed.
    // lastSentObstacles (server side) limits obstacle updates to the ones that moved.
    void writeSnapshot(QDataStream &out, const std::vector<SimEvent> &events,
                       std::vector<Obstacle> *lastSentObstacles) const;
    void readSnapshot(QDataStream &in, std::vector<SimEvent> &eventsOut);

    uint64_t checksum() const;

    float roadCenter(float z) const;
    float roadAngle(float z) const;

    // Road width of this race (set by reset): 3 lanes offline, 5 lanes online
    float roadHalf() const { return halfWidth; }
    float railOffset() const { return halfWidth + 4.0f; }   // guard rails
    int laneCount() const { return lanes; }
    float laneX(int i) const { return (float(i) - 0.5f * float(lanes - 1)) * LANE_WIDTH; }   // lane centre from the road centre

    // Simulation states & accessors
    State getState() const { return state; }
    void setState(State s) { state = s; }
    GameMode getGameMode() const { return mode; }
    void setGameMode(GameMode m) { mode = m; }

    int getAiDifficulty() const { return aiDifficulty; }
    void setAiDifficulty(int d) { aiDifficulty = d; }

    // Map selection (takes effect at the next startRace / reset)
    int getMap() const { return mapIndex; }
    void setMap(int m) { mapIndex = (m % NUM_MAPS + NUM_MAPS) % NUM_MAPS; }
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
    void collideWorld(Car &c, int carIndex);
    // Client-side prediction only: bump into barriers and traffic like collideWorld does,
    // but without knocking anything over or making events (the server decides those).
    void predictCollisions(Car &c) const;
    void collideCars(Car &a, Car &b, int idxA, int idxB);
    bool knock(Obstacle &o, float x, float z, float fx, float fz, float speed, int carIndex);

private:
    float rnd(float a, float b);

    State state = Menu;
    GameMode mode = ModeSingle;
    int aiDifficulty = 1;
    int mapIndex = 0;
    float halfWidth = ROAD_HALF;
    int lanes = 3;

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
