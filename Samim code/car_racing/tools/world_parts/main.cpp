// Prints each map's track fingerprint part by part (shared/gamesim.cpp worldParts), to compare
// systems: the Linux server, the Windows game and the browser build must build the same track.
#include "gamesim.h"
#include <cstdio>
int main()
{
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        sim::GameSim s;
        s.setMap(m);
        s.setLaps(2);
        s.startRace(sim::GameSim::ModeOnline, 6);
        std::printf("%s\n", s.worldParts().c_str());
    }
    return 0;
}
