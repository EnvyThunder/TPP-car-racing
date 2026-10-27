// Headless CPU-driver benchmark (no Qt, no graphics).
// Build:  g++ -O2 -std=c++17 -Ishared tools/ai_bench.cpp shared/gamesim.cpp -o ai_bench
//
// Races the CPU (car 1) against a reference driver in car 0 and prints finish
// times, crashes and the winner for every map and difficulty. The reference
// driver is the CPU's own driving logic at normal speed with no catch-up help,
// which stands in for a good human player.
// Pass "solo" to leave car 0 parked on the grid and time the CPU on its own.
#include "gamesim.h"
#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    const bool solo = argc > 1 && std::strcmp(argv[1], "solo") == 0;
    const char *diffName[3] = {"EASY", "MEDIUM", "HARD"};
    std::printf("%s\n", solo ? "CPU alone (car 0 parked)" : "CPU vs reference driver");
    std::printf("%-16s %-7s %9s %9s %8s %8s  %s\n", "map", "cpu", "cpu time", "ref time", "cpu hit", "ref hit", "winner");
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        for (int d = 0; d < 3; ++d) {
            sim::GameSim s;
            s.setMap(m);
            s.setAiDifficulty(d);
            s.startRace(sim::GameSim::ModeSingle);
            int hit[2] = {0, 0};
            for (int t = 0; t < 60 * 240 && !s.getCars()[1].finished && !(s.getState() == sim::GameSim::Finished); ++t) {
                sim::PlayerInput in[2];
                if (!solo) in[0] = s.computeAIInput(s.getCars()[0], 0, false);
                s.step(in, 2);
                bool crashed[2] = {false, false};
                for (const sim::SimEvent &e : s.getEvents())
                    if (e.type == sim::SimEvent::Crash && e.carIndex >= 0 && e.carIndex < 2) crashed[e.carIndex] = true;
                for (int i = 0; i < 2; ++i) hit[i] += crashed[i] ? 1 : 0;
            }
            const auto &c = s.getCars();
            auto tm = [](const sim::Car &car) { return car.finished ? car.finishTime : 999.0f; };
            std::printf("%-16s %-7s %8.2fs %8.2fs %8d %8d  %s\n", sim::MAPS[m].name, diffName[d],
                        tm(c[1]), tm(c[0]), hit[1], hit[0], s.getWinner() == 2 ? "CPU" : (s.getWinner() == 1 ? "player" : "-"));
        }
    }
    return 0;
}
