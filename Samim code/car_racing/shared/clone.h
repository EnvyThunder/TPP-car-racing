#ifndef CLONE_H
#define CLONE_H

// A clone of your driving (docs/cpu-psychology/README.md, milestone 6). While you race
// offline the game writes down, 20 times a second on the open road, what you saw (the same
// inputs a brain gets, shared/brain.h) and what you did. After a race a network of the
// brains' shape is trained on it with backpropagation (supervised learning: "given what you
// saw, do what you did"), starting from the RACER brain. A CPU can then drive with it inside
// the same brain-and-rules hybrid as the evolved brains. Everything stays on your PC: the
// recordings and the clone are files next to your profile.
//
// People (and the rule-based driver) tap the steering keys on and off many times a second,
// which no network can copy tap by tap. So what it learns is the intent: how much you steer
// and accelerate over the next CloneRecorder::AHEAD ticks (-1..1). Driving, the clone turns
// that back into taps at the same rate (GameSim::computeAIInput, error diffusion).

#include "brain.h"
#include "simtypes.h"

#include <cstdint>
#include <string>
#include <random>
#include <vector>

namespace sim {

class GameSim;

enum CloneKey : uint8_t { KeyNitro = 1, KeyBoost = 2 };

struct CloneSample {
    float in[BRAIN_IN];
    float steer = 0.0f;          // -1 (all left) .. +1 (all right): the share of the next ticks steering
    float pedal = 0.0f;          // +1 throttle all the time .. -1 brake all the time
    uint8_t keys = 0;            // nitro / boost pressed in those ticks
    uint16_t race = 0;           // which recorded race it came from
};

// Your recordings: the last MAX_RACES races
struct CloneBook {
    static constexpr int MAX_RACES = 20;
    static constexpr int MIN_RACES = 3;     // a clone is trained from this many races on
    std::vector<CloneSample> samples;
    uint16_t raceCounter = 0;
    void beginRace();                       // a new race starts (drops the oldest past MAX_RACES)
    void dropEmptyRace();                   // the race just ended had too little open road: forget it
    int races() const;
    bool save(const std::string &path) const;
    std::string bytes(int race = -1) const;     // the file's contents (race >= 0: that race only - an upload)
    bool load(const std::string &path);
};

// Records one car into a book: call step() every tick with the keys about to be applied
// (before GameSim::step). Every EVERY ticks on the open road it notes what the car sees; the
// sample is finished AHEAD ticks later with what the driver did in between.
class CloneRecorder {
public:
    static constexpr int EVERY = 3, AHEAD = 8;
    void reset() { pending.clear(); past.clear(); tickNo = 0; }
    void step(const GameSim &s, int idx, const PlayerInput &in, CloneBook &book);
private:
    struct Pending { CloneSample s; int n = 0; };
    std::vector<Pending> pending;
    std::vector<float> past;                 // the last AHEAD ticks' steering (what it sees of itself)
    int tickNo = 0;
};

struct CloneStats {
    int races = 0, samples = 0;
    float agreeSteer = 0.0f;     // on the newest race (held out): share of moments with the same steering intent
    float agreePedal = 0.0f;     // ... the same throttle / coast / brake intent
    float agree() const { return 0.5f * (agreeSteer + agreePedal); }
};

// Trains a clone (from `start`, or from small random numbers without one), holding the
// newest race out to measure how often it means what you meant. Same data, same result.
bool trainClone(const CloneBook &book, const Brain *start, Brain &out, CloneStats &st, int epochs = 20);

// The same training a little at a time: the browser has no threads, and training a long history
// in one go stops the page for many seconds. run() works for about budgetMs (below 0: to the
// end) and says whether it is finished; slices or not, the result is exactly the same.
class CloneTrainer {
public:
    bool begin(const CloneBook &book, const Brain *start, int epochs = 20);   // false: too little to learn from
    bool run(double budgetMs = -1.0);
    bool busy() const { return active; }
    Brain out{};
    CloneStats st;
private:
    CloneBook book;
    std::vector<int> train, test;
    std::mt19937 rng;
    std::vector<float> W, M, V, G;
    int epochs = 0, ep = 0, step = 0;
    size_t b0 = 0;
    bool shuffled = false, active = false;
};

bool saveCloneBrain(const std::string &path, const Brain &b, const CloneStats &st);
bool loadCloneBrain(const std::string &path, Brain &b, CloneStats &st);

// a clone's outputs read as intents (-1..1), as it was trained
inline float cloneIntent(float out) { return out / 0.9f; }

} // namespace sim

#endif // CLONE_H
