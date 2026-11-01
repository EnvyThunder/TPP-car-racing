// The evolved brains (see brain.h): the network, and the trained weights from brains.inc.
#include "brain.h"
#include "aidriver.h"

#include <algorithm>

namespace sim {

// tanh, as a rational curve (close to it, cheap, and the same everywhere)
static inline float squash(float x)
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void Brain::run(const float in[BRAIN_IN], float out[BRAIN_OUT]) const
{
    const float *p = w;
    float hid[BRAIN_HID];
    for (int h = 0; h < BRAIN_HID; ++h) {
        float a = 0.0f;
        for (int i = 0; i < BRAIN_IN; ++i) a += p[i] * in[i];
        hid[h] = squash(a + p[BRAIN_IN]);
        p += BRAIN_IN + 1;
    }
    for (int o = 0; o < BRAIN_OUT; ++o) {
        float a = 0.0f;
        for (int h = 0; h < BRAIN_HID; ++h) a += p[h] * hid[h];
        out[o] = squash(a + p[BRAIN_HID]);
        p += BRAIN_HID + 1;
    }
}

const char *brainName(int kind)
{
    static const char *N[NUM_BRAINS + 1] = {"RACER", "DAREDEVIL", "BRUISER", "SMOOTH", "CLONE"};
    return kind >= 0 && kind <= BrainClone ? N[kind] : "RULES";
}

int brainForPersona(int persona)
{
    switch (persona) {
    case PersonaAttacker:    return BrainBruiser;
    case PersonaGrudge:      return BrainBruiser;
    case PersonaShowOff:     return BrainDaredevil;
    case PersonaWildCard:    return BrainDaredevil;
    case PersonaClean:       return BrainSmooth;
    case PersonaBlocker:     return BrainSmooth;
    default:                 return BrainRacer;      // the late charger
    }
}

#include "brains.inc"   // BRAINS_TRAINED, BRAIN_WEIGHTS[NUM_BRAINS][BRAIN_SIZE] (tools/evolve writes it)

const Brain *bundledBrain(int kind)
{
#if BRAINS_TRAINED
    static_assert(sizeof(Brain) == sizeof(float) * BRAIN_SIZE, "a Brain is just its weights");
    if (kind < 0 || kind >= NUM_BRAINS) return nullptr;
    return reinterpret_cast<const Brain *>(BRAIN_WEIGHTS[kind]);
#else
    (void)kind;
    return nullptr;
#endif
}

} // namespace sim
