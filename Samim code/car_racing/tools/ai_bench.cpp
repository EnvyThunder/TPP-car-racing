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
#include <algorithm>

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

    // Online CPU racers: levels 1..5 together on every map, against the reference driver
    // (car 0, standing in for a human). Each level starts from every grid slot once
    // (5 rotations x 5 maps = 25 races), so starting position doesn't decide the result.
    std::printf("\nOnline CPU levels: 25 races of 6 cars (reference driver + levels 1-5)\n");
    float rankSum[6] = {};
    int finished[6] = {};
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        for (int rot = 0; rot < 5; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.startRace(sim::GameSim::ModeOnline, 6);
            int levelOf[6] = {0};
            for (int lv = 1; lv <= 5; ++lv) levelOf[1 + (lv - 1 + rot) % 5] = lv;
            for (int i = 1; i < 6; ++i) s.getCars()[i].aiLevel = levelOf[i];
            for (int t = 0; t < 60 * 300 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[6];
                for (int i = 0; i < 6; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, i > 0);
                s.step(in, 6);
            }
            for (int i = 0; i < 6; ++i) {
                rankSum[levelOf[i]] += float(s.rankOf(i));
                finished[levelOf[i]] += s.getCars()[i].finished ? 1 : 0;
            }
        }
    }
    std::printf("%-14s %8s %8s %8s %8s %8s %8s\n", "", "ref", "lvl 1", "lvl 2", "lvl 3", "lvl 4", "lvl 5");
    std::printf("%-14s", "average place");
    for (int i = 0; i < 6; ++i) std::printf(" %8.2f", rankSum[i] / 25.0f);
    std::printf("\n%-14s", "finished");
    for (int i = 0; i < 6; ++i) std::printf(" %5d/25", finished[i]);
    std::printf("\n");

    // Full room: 8 cars on every map (3 reference drivers standing in for people + levels
    // 1-5), 3 grid rotations each. Counts crashes (a car hitting something, not every frame
    // it stays in contact) and how many cars reach the line before the race closes.
    std::printf("\nFull room: 15 races of 8 cars (3 reference drivers + levels 1-5)\n");
    std::printf("%-16s %10s %10s %14s\n", "map", "winner", "finished", "crashes/car");
    int allFinished = 0, allCrashes = 0;
    for (int m = 0; m < sim::NUM_MAPS; ++m) {
        float winSum = 0.0f;
        int fin = 0, crashes = 0;
        for (int rot = 0; rot < 3; ++rot) {
            sim::GameSim s;
            s.setMap(m);
            s.startRace(sim::GameSim::ModeOnline, 8);
            for (int i = 0; i < 8; ++i) s.getCars()[(i + rot * 3) % 8].aiLevel = i < 3 ? 0 : i - 2;
            bool wasCrashing[8] = {};
            for (int t = 0; t < 60 * 300 && s.getState() != sim::GameSim::Finished; ++t) {
                sim::PlayerInput in[8];
                for (int i = 0; i < 8; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, s.getCars()[i].aiLevel > 0);
                s.step(in, 8);
                bool crashing[8] = {};
                for (const sim::SimEvent &e : s.getEvents())
                    if (e.type == sim::SimEvent::Crash && e.carIndex >= 0 && e.carIndex < 8) crashing[e.carIndex] = true;
                for (int i = 0; i < 8; ++i) {
                    crashes += (crashing[i] && !wasCrashing[i]) ? 1 : 0;
                    wasCrashing[i] = crashing[i];
                }
            }
            float best = 999.0f;
            for (const sim::Car &c : s.getCars())
                if (c.finished) { ++fin; best = std::min(best, c.finishTime); }
            winSum += best;
        }
        std::printf("%-16s %9.2fs %7d/24 %14.1f\n", sim::MAPS[m].name, winSum / 3.0f, fin, crashes / 24.0f);
        allFinished += fin;
        allCrashes += crashes;
    }
    std::printf("%-16s %10s %6d/120 %14.1f\n", "ALL MAPS", "", allFinished, allCrashes / 120.0f);
    return 0;
}
