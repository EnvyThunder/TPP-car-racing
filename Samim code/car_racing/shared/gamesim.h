#ifndef GAMESIM_H
#define GAMESIM_H

#include "simtypes.h"
#include <vector>
#include <random>

namespace sim {

class GameSim {
public:
    enum State { Menu, Countdown, Racing, Paused, Finished };
    enum GameMode { ModeSingle, ModeSplitScreen };

    explicit GameSim(uint32_t seed = 2024);

    void reset(uint32_t seed = 2024);
    void startRace(GameMode mode);
    void step(const PlayerInput inputs[], int inputCount);

    uint64_t checksum() const;

    float roadCenter(float z) const;
    float roadAngle(float z) const;

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
    void collideCars(Car &a, Car &b, int idxA, int idxB);
    bool knock(Obstacle &o, float x, float z, float fx, float fz, float speed, int carIndex);

private:
    float rnd(float a, float b);

    State state = Menu;
    GameMode mode = ModeSingle;
    int aiDifficulty = 1;
    int mapIndex = 0;

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
