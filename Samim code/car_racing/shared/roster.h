#ifndef ROSTER_H
#define ROSTER_H

// The named CPU drivers (docs/cpu-psychology/README.md, milestone 4): twelve characters of
// our own, each with a personality (aidriver.h), a car and helmet colour, a home map where
// they are a little sharper and a small lean in skill - and short lines they say to a person
// at the right moments (taunts: pre-written, nothing is generated).

namespace sim {

constexpr int ROSTER_SIZE = 12;

struct RosterDriver {
    const char *name;        // at most 10 letters (it fits a name plate)
    int   persona;           // Persona
    float car[3], helmet[3];
    int   homeMap;           // a little sharper here (+0.05 skill)
    float skillLean;         // -0.06 .. +0.06 around the race's skill centre
};
const RosterDriver &rosterDriver(int i);
int rosterByName(const char *name);   // -1: not a roster driver

// when a driver says something to a person
enum TauntMoment { TauntStart, TauntPassedYou, TauntYouPassed, TauntYouRammed, TauntRammedYou,
                   TauntYouWrecked, TauntRevenge, TauntWin, TauntLost, NUM_TAUNT_MOMENTS };
// the line (two of each moment per personality; variant picks one)
const char *tauntLine(int rosterId, int moment, int variant);
// your clone's lines (shared/clone.h): it talks like someone who learned from you
const char *cloneTauntLine(int moment, int variant);

} // namespace sim

#endif // ROSTER_H
