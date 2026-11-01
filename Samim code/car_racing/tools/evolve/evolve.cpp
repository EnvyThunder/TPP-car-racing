// Breeds the CPU drivers' neural brains (shared/brain.h) by evolution in the headless race sim.
//
//   evolve train <species 0-3> <generations> [population] [threads]
//       a population of brains races on every map; the best survive, their children get
//       small random changes and mixes of two parents; repeat. Keeps going from the saved
//       population if there is one. Writes evolve_<species>.pop (all of them),
//       evolve_<species>.best (the best brain) and evolve_<species>.csv (the training curve).
//   evolve pack <out brains.inc>
//       builds shared/brains.inc from the four .best files.
//
// Each species is scored differently (scoreOf), so each learns its own way of driving:
//   RACER      distance in the time - pure pace
//   DAREDEVIL  pace, plus side ramps, air time and L boosting; crashes cost it less
//   BRUISER    pace, plus hits on other cars and places gained; it ignores racers ahead
//   SMOOTH     pace, minus jerky steering and time near the road edge; crashes cost it more
// and every species pays for wrecks, damage and the safety net taking over.
//
// Build (from the project folder):
//   g++ -O2 -std=c++17 -Ishared tools/evolve/evolve.cpp shared/gamesim.cpp shared/track.cpp
//       shared/circuits.cpp shared/playermodel.cpp shared/aidriver.cpp shared/director.cpp
//       shared/roster.cpp shared/brain.cpp -o evolve.exe -pthread
#include "gamesim.h"
#include "brain.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

using namespace sim;

static const int RACE_TICKS = 60 * 125;             // each test race: 125 s of driving - the whole 2 laps, last-lap extras too
static const int TRAIN_PERSONA[NUM_BRAINS] = {PersonaLateCharger, PersonaShowOff, PersonaAttacker, PersonaClean};

struct Genome { std::vector<float> w; double fit = 0.0, prog = 0.0; };

struct Tally {                                      // what one brain did in one race
    float progress = 0.0f, damage = 0.0f;
    int wrecks = 0, takeovers = 0, air = 0, boost = 0, jumps = 0, hits = 0, steerFlips = 0, edge = 0;
    int startRank = 0, endRank = 0, brainTicks = 0;
};

static double scoreOf(int species, const Tally &t)
{
    double f = t.progress;
    const double dmgCost = species == BrainSmooth ? 3.0 : (species == BrainDaredevil || species == BrainBruiser) ? 1.0 : 2.0;
    f -= dmgCost * t.damage + 400.0 * t.wrecks + 250.0 * t.takeovers;
    switch (species) {
    case BrainDaredevil: f += 0.8 * t.air + 40.0 * t.jumps + 0.25 * t.boost; break;
    case BrainBruiser:   f += 35.0 * t.hits + 40.0 * (t.startRank - t.endRank); break;
    case BrainSmooth:    f -= 0.6 * t.steerFlips + 0.3 * t.edge; break;
    default: break;
    }
    return f;
}

// one race: six brains of one species, on one map, with obstacles, traffic and ramps
static void race(int species, int map, uint32_t seed, const Genome *g[MAX_RACERS], Tally out[MAX_RACERS])
{
    GameSim s;
    s.setMap(map);
    s.setAiSeed(seed);
    s.setCpuSkill(0.85f);
    s.setDirector(false);
    s.startRace(GameSim::ModeOnline, MAX_RACERS);
    std::mt19937 rng(seed * 7u + 3u);
    for (int i = 0; i < MAX_RACERS; ++i) {
        Car &c = s.getCars()[i];
        c.aiLevel = 1;
        c.aiPersona = TRAIN_PERSONA[species];
        c.aiSkill = 0.55f + 0.45f * float(rng() % 1000u) / 1000.0f;    // (it must work for weaker drivers too)
        if (g[i]) s.setBrainOverride(i, reinterpret_cast<const Brain *>(g[i]->w.data()), species);
        else s.setBrains(false);                    // (the rules alone: the baseline)
    }
    bool prevL[MAX_RACERS] = {}, prevR[MAX_RACERS] = {};
    float prevHealth[MAX_RACERS];
    int prevWrecks[MAX_RACERS] = {};
    for (int i = 0; i < MAX_RACERS; ++i) prevHealth[i] = 100.0f;
    int ticks = 0, racingTicks = 0;
    while (racingTicks < RACE_TICKS && ticks < RACE_TICKS + 60 * 10) {
        ++ticks;
        PlayerInput in[MAX_RACERS];
        for (int i = 0; i < MAX_RACERS; ++i) in[i] = s.computeAIInput(s.getCars()[i], i, true);
        s.step(in, MAX_RACERS);
        if (s.getState() != GameSim::Racing) { s.clearEvents(); continue; }
        if (racingTicks == 60 * 3)
            for (int i = 0; i < MAX_RACERS; ++i) out[i].startRank = s.rankOf(i);
        ++racingTicks;
        for (const SimEvent &e : s.getEvents()) {
            if (e.carIndex < 0 || e.carIndex >= MAX_RACERS) continue;
            if (e.type == SimEvent::Stunt && e.count <= SimEvent::StuntSaved) ++out[e.carIndex].jumps;
        }
        s.clearEvents();
        for (int i = 0; i < MAX_RACERS; ++i) {
            const Car &c = s.getCars()[i];
            Tally &t = out[i];
            if (c.wreck == 0 && c.health < prevHealth[i]) t.damage += prevHealth[i] - c.health;
            prevHealth[i] = c.wreck ? 100.0f : c.health;
            if (c.wrecks > prevWrecks[i]) { t.wrecks += c.wrecks - prevWrecks[i]; prevWrecks[i] = c.wrecks; }
            if (c.air > 0) ++t.air;
            if (c.boosting) ++t.boost;
            if (c.aiBrainDriving) {
                ++t.brainTicks;
                if ((in[i].left && prevR[i]) || (in[i].right && prevL[i])) ++t.steerFlips;
                if (std::fabs(c.d) > 0.75f * s.roadHalfAt(c.s, c.path)) ++t.edge;
            }
            prevL[i] = in[i].left; prevR[i] = in[i].right;
        }
    }
    for (int i = 0; i < MAX_RACERS; ++i) {
        const Car &c = s.getCars()[i];
        out[i].progress = c.progress;
        out[i].takeovers = c.aiTakeovers;
        out[i].hits = c.crashesCaused;
        out[i].endRank = s.rankOf(i);
    }
}

static bool loadFloats(const std::string &path, std::vector<std::vector<float>> &rows)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int n = 0, size = 0;
    if (std::fread(&n, sizeof n, 1, f) != 1 || std::fread(&size, sizeof size, 1, f) != 1 || size != BRAIN_SIZE) { std::fclose(f); return false; }
    rows.assign(size_t(n), std::vector<float>(size_t(size)));
    for (auto &r : rows)
        if (std::fread(r.data(), sizeof(float), r.size(), f) != r.size()) { std::fclose(f); return false; }
    std::fclose(f);
    return true;
}

static void saveFloats(const std::string &path, const std::vector<std::vector<float>> &rows)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const int n = int(rows.size()), size = BRAIN_SIZE;
    std::fwrite(&n, sizeof n, 1, f);
    std::fwrite(&size, sizeof size, 1, f);
    for (const auto &r : rows) std::fwrite(r.data(), sizeof(float), r.size(), f);
    std::fclose(f);
}

static void train(int species, int gens, int pop, int threads)
{
    for (int m = 0; m < NUM_MAPS; ++m) GameSim::referenceSectors(m);   // (worked out once, before the threads)
    pop = std::max(12, pop / MAX_RACERS * MAX_RACERS);
    const std::string base = "evolve_" + std::to_string(species);
    std::mt19937 rng(12345u + uint32_t(species) * 977u);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    std::vector<Genome> P(static_cast<size_t>(pop));
    std::vector<std::vector<float>> saved;
    int startGen = 0;
    if (loadFloats(base + ".pop", saved) && !saved.empty()) {
        for (int i = 0; i < pop; ++i) P[size_t(i)].w = saved[size_t(i) % saved.size()];
        FILE *csv = std::fopen((base + ".csv").c_str(), "r");
        if (csv) { char line[256]; while (std::fgets(line, sizeof line, csv)) ++startGen; std::fclose(csv); startGen = std::max(0, startGen - 1); }
        std::printf("%s: carrying on from generation %d\n", brainName(species), startGen);
    } else {
        for (Genome &g : P) {
            g.w.resize(BRAIN_SIZE);
            for (float &x : g.w) x = gauss(rng) * 0.4f;
        }
        FILE *csv = std::fopen((base + ".csv").c_str(), "w");
        if (csv) { std::fprintf(csv, "generation,best,mean,progress_best,wrecks_mean,takeovers_mean,brain_share\n"); std::fclose(csv); }
    }

    const int racesPerMap = pop / MAX_RACERS;
    for (int gen = startGen; gen < startGen + gens; ++gen) {
        // every brain drives once on each map, in a random six each time
        std::vector<std::vector<int>> order(NUM_MAPS);
        for (int m = 0; m < NUM_MAPS; ++m) {
            order[size_t(m)].resize(size_t(pop));
            for (int i = 0; i < pop; ++i) order[size_t(m)][size_t(i)] = i;
            std::shuffle(order[size_t(m)].begin(), order[size_t(m)].end(), rng);
        }
        std::vector<Tally> tallies(size_t(pop) * NUM_MAPS);
        std::atomic<int> next(0);
        const int jobs = NUM_MAPS * racesPerMap;
        const uint32_t genSeed = uint32_t(gen) * 7919u + uint32_t(species) * 104729u + 17u;
        auto worker = [&]() {
            for (int j; (j = next++) < jobs;) {
                const int m = j / racesPerMap, r = j % racesPerMap;
                const Genome *g[MAX_RACERS];
                Tally t[MAX_RACERS];
                for (int i = 0; i < MAX_RACERS; ++i) g[i] = &P[size_t(order[size_t(m)][size_t(r * MAX_RACERS + i)])];
                race(species, m, genSeed + uint32_t(m) * 31u, g, t);
                for (int i = 0; i < MAX_RACERS; ++i) tallies[size_t(order[size_t(m)][size_t(r * MAX_RACERS + i)]) * NUM_MAPS + size_t(m)] = t[i];
            }
        };
        std::vector<std::thread> pool;
        for (int k = 0; k < threads; ++k) pool.emplace_back(worker);
        for (auto &th : pool) th.join();

        double mean = 0.0, wrecks = 0.0, takeovers = 0.0, share = 0.0;
        for (int i = 0; i < pop; ++i) {
            double f = 0.0, prog = 0.0;
            for (int m = 0; m < NUM_MAPS; ++m) {
                const Tally &t = tallies[size_t(i) * NUM_MAPS + size_t(m)];
                f += scoreOf(species, t);
                prog += t.progress;
                wrecks += t.wrecks; takeovers += t.takeovers; share += double(t.brainTicks) / RACE_TICKS;
            }
            P[size_t(i)].fit = f / NUM_MAPS;
            P[size_t(i)].prog = prog / NUM_MAPS;
            mean += P[size_t(i)].fit;
        }
        mean /= pop;
        std::sort(P.begin(), P.end(), [](const Genome &a, const Genome &b) { return a.fit > b.fit; });
        const double bestProg = P[0].prog;
        std::printf("%s gen %3d  best %7.1f (%6.0f m)  mean %7.1f  wrecks %.2f  takeovers %.2f  brain drives %.0f%%\n", brainName(species), gen,
                    P[0].fit, bestProg, mean, wrecks / (pop * NUM_MAPS), takeovers / (pop * NUM_MAPS), 100.0 * share / (pop * NUM_MAPS));
        std::fflush(stdout);
        FILE *csv = std::fopen((base + ".csv").c_str(), "a");
        if (csv) {
            std::fprintf(csv, "%d,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f\n", gen, P[0].fit, mean, bestProg, wrecks / (pop * NUM_MAPS),
                         takeovers / (pop * NUM_MAPS), share / (pop * NUM_MAPS));
            std::fclose(csv);
        }
        std::vector<std::vector<float>> rows;
        for (const Genome &g : P) rows.push_back(g.w);
        saveFloats(base + ".pop", rows);
        saveFloats(base + ".best", {P[0].w});

        // the next generation: the best 1/8 stay as they are; the rest are children of two
        // parents (each picked as the best of three at random), mixed and slightly changed
        const int elite = std::max(2, pop / 8);
        const float sigma = std::max(0.05f, 0.25f * std::pow(0.985f, float(gen)));
        std::vector<Genome> N(P.begin(), P.begin() + elite);
        auto pick = [&]() -> const Genome & {
            int b = int(rng() % uint32_t(pop));
            for (int k = 0; k < 2; ++k) { int c = int(rng() % uint32_t(pop)); if (P[size_t(c)].fit > P[size_t(b)].fit) b = c; }
            return P[size_t(b)];
        };
        while (int(N.size()) < pop) {
            const Genome &a = pick(), &b = pick();
            Genome child;
            child.w = a.w;
            if (rng() % 2u)
                for (int k = 0; k < BRAIN_SIZE; ++k) if (rng() % 2u) child.w[size_t(k)] = b.w[size_t(k)];
            for (float &x : child.w)
                if (rng() % 10u == 0u) x += gauss(rng) * sigma;
            N.push_back(std::move(child));
        }
        P.swap(N);
    }
}

static void pack(const char *outPath)
{
    FILE *f = std::fopen(outPath, "w");
    if (!f) { std::printf("can't write %s\n", outPath); return; }
    std::fprintf(f, "// Generated by tools/evolve (\"evolve pack\") - the trained weights of the four brains.\n");
    std::fprintf(f, "#define BRAINS_TRAINED 1\n");
    std::fprintf(f, "static const float BRAIN_WEIGHTS[NUM_BRAINS][BRAIN_SIZE] = {\n");
    for (int k = 0; k < NUM_BRAINS; ++k) {
        std::vector<std::vector<float>> rows;
        if (!loadFloats("evolve_" + std::to_string(k) + ".best", rows) || rows.empty()) {
            std::printf("no evolve_%d.best\n", k);
            std::fclose(f);
            return;
        }
        std::fprintf(f, "    { // %s\n", brainName(k));
        for (int i = 0; i < BRAIN_SIZE; ++i)
            std::fprintf(f, "%s%.6ff,%s", i % 8 == 0 ? "        " : " ", double(rows[0][size_t(i)]), (i % 8 == 7 || i == BRAIN_SIZE - 1) ? "\n" : "");
        std::fprintf(f, "    },\n");
    }
    std::fprintf(f, "};\n");
    std::fclose(f);
    std::printf("wrote %s\n", outPath);
}

// the rules alone (no brains) in the same test races, scored the same way; with a .best
// file, the best brain of that species in the same races, side by side
static void baseline(int species, int seeds)
{
    for (int m = 0; m < NUM_MAPS; ++m) GameSim::referenceSectors(m);
    std::vector<std::vector<float>> rows;
    Genome best;
    const bool haveBest = loadFloats("evolve_" + std::to_string(species) + ".best", rows) && !rows.empty();
    if (haveBest) best.w = rows[0];
    for (int who = 0; who < (haveBest ? 2 : 1); ++who) {
        double score = 0.0, prog = 0.0, wrecks = 0.0, take = 0.0, share = 0.0;
        int n = 0;
        for (int m = 0; m < NUM_MAPS; ++m)
            for (int sd = 0; sd < seeds; ++sd) {
                const Genome *g[MAX_RACERS];
                for (int i = 0; i < MAX_RACERS; ++i) g[i] = who ? &best : nullptr;
                Tally t[MAX_RACERS];
                race(species, m, 900001u + uint32_t(sd) * 131u + uint32_t(m) * 31u, g, t);
                for (int i = 0; i < MAX_RACERS; ++i) {
                    score += scoreOf(species, t[i]); prog += t[i].progress; wrecks += t[i].wrecks;
                    take += t[i].takeovers; share += double(t[i].brainTicks) / RACE_TICKS; ++n;
                }
            }
        std::printf("%-6s %-10s score %7.1f  distance %6.0f m  wrecks %.2f  takeovers %.2f  brain drives %.0f%%\n",
                    who ? "BRAIN" : "RULES", brainName(species), score / n, prog / n, wrecks / n, take / n, 100.0 * share / n);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "baseline") == 0) { baseline(std::atoi(argv[2]), argc > 3 ? std::atoi(argv[3]) : 4); return 0; }
    if (argc >= 4 && std::strcmp(argv[1], "train") == 0) {
        const int threads = argc > 5 ? std::atoi(argv[5]) : int(std::max(1u, std::thread::hardware_concurrency()));
        train(std::clamp(std::atoi(argv[2]), 0, NUM_BRAINS - 1), std::atoi(argv[3]), argc > 4 ? std::atoi(argv[4]) : 48, threads);
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "pack") == 0) { pack(argv[2]); return 0; }
    std::printf("evolve train <species 0-3> <generations> [population] [threads]\nevolve pack <out brains.inc>\n");
    return 1;
}
