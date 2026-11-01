#ifndef BRAIN_H
#define BRAIN_H

// Evolved neural brains for the CPU drivers (docs/cpu-psychology/README.md, milestone 5).
// A brain is a small neural network - BRAIN_IN inputs, one hidden layer of BRAIN_HID
// neurons, BRAIN_OUT outputs - whose weights were bred by evolution in the headless race
// sim (tools/evolve): no internet and no AI service, just numbers we trained ourselves and
// built into the game (shared/brains.inc).
//
// It drives the open road: steering, throttle and brakes, nitro, the L boost, and how keen
// it is on side ramps. The rule-based driver (GameSim::computeAIInput) keeps the tricky
// moments - rows of obstacles close ahead, cars alongside, battles, lining up with a ramp,
// flying, backing out - and takes over for 3 s whenever a brain leaves the road or points
// the wrong way (the safety net). Four species were trained with different scoring, so each
// drives in its own way; every personality uses one of them.

namespace sim {

enum BrainKind { BrainRacer, BrainDaredevil, BrainBruiser, BrainSmooth, NUM_BRAINS,
                 BrainClone = NUM_BRAINS };   // (a clone of a person's driving: shared/clone.h)

constexpr int BRAIN_IN = 28, BRAIN_HID = 16, BRAIN_OUT = 5;
constexpr int BRAIN_SIZE = BRAIN_IN * BRAIN_HID + BRAIN_HID + BRAIN_HID * BRAIN_OUT + BRAIN_OUT;
// outputs
enum BrainOut { OutSteer, OutPedal, OutNitro, OutBoost, OutRamps };

struct Brain {
    float w[BRAIN_SIZE];
    // the same numbers in give the same numbers out on every run (only plain float maths)
    void run(const float in[BRAIN_IN], float out[BRAIN_OUT]) const;
};

const char *brainName(int kind);        // "RACER" ... "CLONE"
int brainForPersona(int persona);       // the species each personality drives with
const Brain *bundledBrain(int kind);    // the trained brain built into the game (nullptr: none)

} // namespace sim

#endif // BRAIN_H
