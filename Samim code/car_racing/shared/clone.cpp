// A clone of your driving (see clone.h): the recordings, and backpropagation training.
#include "clone.h"
#include "gamesim.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

namespace sim {

// what an intent (-1..1, an output read as cloneIntent) or a recorded share counts as:
// steering / braking one way, or not
static int intentClass(float v) { return v > 0.33f ? 1 : v < -0.33f ? -1 : 0; }

// ---- The recordings ----------------------------------------------------------------------

void CloneBook::beginRace()
{
    ++raceCounter;
    // keep the newest MAX_RACES - 1 races (the new one is the MAX_RACES-th)
    std::vector<uint16_t> ids;
    for (const CloneSample &s : samples)
        if (ids.empty() || ids.back() != s.race) ids.push_back(s.race);
    if (int(ids.size()) >= MAX_RACES) {
        const std::vector<uint16_t> keep(ids.end() - (MAX_RACES - 1), ids.end());
        samples.erase(std::remove_if(samples.begin(), samples.end(), [&](const CloneSample &s) {
                          return std::find(keep.begin(), keep.end(), s.race) == keep.end(); }),
                      samples.end());
    }
}

void CloneRecorder::step(const GameSim &s, int idx, const PlayerInput &in, CloneBook &book)
{
    const float st = (in.right && !in.left) ? 1.0f : (in.left && !in.right) ? -1.0f : 0.0f;
    const float pd = in.up ? 1.0f : in.down ? -1.0f : 0.0f;
    if (tickNo++ % EVERY == 0) {                         // what it sees now (on the open road)
        float prev = 0.0f;
        for (float p : past) prev += p;
        prev = past.empty() ? 0.0f : 0.9f * prev / float(past.size());
        Pending p;
        if (s.cloneView(idx, prev, p.s.in)) { p.s.race = book.raceCounter; pending.push_back(p); }
    }
    for (Pending &p : pending) {                         // ... and what it does from now on
        p.s.steer += st; p.s.pedal += pd;
        if (in.nitro) p.s.keys |= KeyNitro;
        if (in.boost) p.s.keys |= KeyBoost;
        ++p.n;
    }
    for (size_t k = 0; k < pending.size();) {
        if (pending[k].n < AHEAD) { ++k; continue; }
        CloneSample done = pending[k].s;
        done.steer /= float(AHEAD); done.pedal /= float(AHEAD);
        book.samples.push_back(done);
        pending.erase(pending.begin() + long(k));
    }
    past.push_back(st);
    if (int(past.size()) > AHEAD) past.erase(past.begin());
}

void CloneBook::dropEmptyRace()
{
    const size_t n = size_t(std::count_if(samples.begin(), samples.end(), [&](const CloneSample &s) { return s.race == raceCounter; }));
    if (n < 300)                                         // (under 15 s of open road)
        samples.erase(std::remove_if(samples.begin(), samples.end(), [&](const CloneSample &s) { return s.race == raceCounter; }),
                      samples.end());
}

int CloneBook::races() const
{
    int n = 0;
    for (size_t i = 0; i < samples.size(); ++i)
        if (i == 0 || samples[i].race != samples[i - 1].race) ++n;
    return n;
}

// the file: "TPPCLON2", the race counter, the count, then each sample as 16-bit numbers
std::string CloneBook::bytes(int race) const
{
    std::string out;
    auto put = [&out](const void *p, size_t n) { out.append(static_cast<const char *>(p), n); };
    uint32_t n = 0;
    for (const CloneSample &s : samples) n += (race < 0 || s.race == race) ? 1u : 0u;
    put("TPPCLON2", 8);
    put(&raceCounter, sizeof raceCounter);
    put(&n, sizeof n);
    for (const CloneSample &s : samples) {
        if (race >= 0 && s.race != race) continue;
        int16_t q[BRAIN_IN];
        for (int i = 0; i < BRAIN_IN; ++i) q[i] = int16_t(std::lround(std::clamp(s.in[i], -4.0f, 4.0f) * 8000.0f));
        const int16_t sp[2] = {int16_t(std::lround(s.steer * 30000.0f)), int16_t(std::lround(s.pedal * 30000.0f))};
        put(q, sizeof q);
        put(sp, sizeof sp);
        put(&s.keys, 1);
        put(&s.race, sizeof s.race);
    }
    return out;
}

bool CloneBook::save(const std::string &path) const
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::string b = bytes();
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    return ok;
}

bool CloneBook::load(const std::string &path)
{
    samples.clear();
    raceCounter = 0;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char tag[8];
    uint32_t n = 0;
    bool ok = std::fread(tag, 1, 8, f) == 8 && std::equal(tag, tag + 8, "TPPCLON2")
              && std::fread(&raceCounter, sizeof raceCounter, 1, f) == 1 && std::fread(&n, sizeof n, 1, f) == 1 && n < 2000000u;
    for (uint32_t k = 0; ok && k < n; ++k) {
        int16_t q[BRAIN_IN], sp[2];
        CloneSample s;
        ok = std::fread(q, sizeof q, 1, f) == 1 && std::fread(sp, sizeof sp, 1, f) == 1 && std::fread(&s.keys, 1, 1, f) == 1
             && std::fread(&s.race, sizeof s.race, 1, f) == 1;
        s.steer = float(sp[0]) / 30000.0f; s.pedal = float(sp[1]) / 30000.0f;
        for (int i = 0; i < BRAIN_IN; ++i) s.in[i] = float(q[i]) / 8000.0f;
        if (ok) samples.push_back(s);
    }
    std::fclose(f);
    if (!ok) { samples.clear(); raceCounter = 0; }
    return ok;
}

// ---- Training ----------------------------------------------------------------------------

// the network's curve (as brain.cpp) and its slope
static inline float squash(float x)
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float squashSlope(float x)
{
    if (x <= -3.0f || x >= 3.0f) return 0.0f;
    const float x2 = x * x, d = 27.0f + 9.0f * x2;
    return ((27.0f + 3.0f * x2) * d - (27.0f + x2) * x2 * 18.0f) / (d * d);
}

// what each output should say for this sample, and how much each output counts
static void targetsFor(const CloneSample &c, float t[BRAIN_OUT], float w[BRAIN_OUT])
{
    t[OutSteer] = 0.9f * c.steer;                        // (cloneIntent reads it back)
    w[OutSteer] = 1.0f;
    t[OutPedal] = 0.9f * c.pedal;
    w[OutPedal] = 1.0f;
    t[OutNitro] = (c.keys & KeyNitro) ? 0.8f : -0.8f;    // (rare: a press counts ten times)
    w[OutNitro] = (c.keys & KeyNitro) ? 3.0f : 0.3f;
    t[OutBoost] = (c.keys & KeyBoost) ? 0.8f : -0.8f;
    w[OutBoost] = (c.keys & KeyBoost) ? 1.5f : 0.5f;
    t[OutRamps] = 0.0f;                                  // (nothing to copy: kept neutral)
    w[OutRamps] = 0.05f;
}

bool CloneTrainer::begin(const CloneBook &b, const Brain *start, int epochCount)
{
    *this = CloneTrainer();
    st.races = b.races();
    if (st.races < CloneBook::MIN_RACES) { st.races = 0; return false; }
    book = b;
    uint16_t newest = book.samples.back().race;
    for (int i = 0; i < int(book.samples.size()); ++i) (book.samples[size_t(i)].race == newest ? test : train).push_back(i);
    st.samples = int(book.samples.size());
    if (train.size() < 300 || test.empty()) { st.races = 0; return false; }   // (nothing learned: keep the old clone)

    rng.seed(20261101u);
    W.resize(BRAIN_SIZE); M.assign(BRAIN_SIZE, 0.0f); V.assign(BRAIN_SIZE, 0.0f); G.resize(BRAIN_SIZE);
    if (start) std::copy(start->w, start->w + BRAIN_SIZE, W.begin());
    else { std::normal_distribution<float> g(0.0f, 0.3f); for (float &x : W) x = g(rng); }
    epochs = epochCount;
    active = true;
    return true;
}

bool CloneTrainer::run(double budgetMs)
{
    if (!active) return true;
    const auto t0 = std::chrono::steady_clock::now();
    const int BATCH = 32;
    const float LR = 0.003f, B1 = 0.9f, B2 = 0.999f;
    float z[BRAIN_HID], h[BRAIN_HID], a[BRAIN_OUT], y[BRAIN_OUT], dA[BRAIN_OUT], dZ[BRAIN_HID], t[BRAIN_OUT], wt[BRAIN_OUT];
    const int OUT0 = BRAIN_HID * (BRAIN_IN + 1);         // where the output layer's numbers start
    for (; ep < epochs; ++ep, b0 = 0, shuffled = false) {
        if (!shuffled) { std::shuffle(train.begin(), train.end(), rng); shuffled = true; }
        for (; b0 < train.size(); b0 += BATCH) {
            if (budgetMs >= 0.0
                && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() >= budgetMs)
                return false;                            // (carries on from this batch next time)
            std::fill(G.begin(), G.end(), 0.0f);
            const size_t b1 = std::min(train.size(), b0 + BATCH);
            for (size_t bi = b0; bi < b1; ++bi) {
                const CloneSample &s = book.samples[size_t(train[bi])];
                // forward
                for (int j = 0; j < BRAIN_HID; ++j) {
                    const float *p = &W[size_t(j * (BRAIN_IN + 1))];
                    float acc = p[BRAIN_IN];
                    for (int i = 0; i < BRAIN_IN; ++i) acc += p[i] * s.in[i];
                    z[j] = acc; h[j] = squash(acc);
                }
                for (int o = 0; o < BRAIN_OUT; ++o) {
                    const float *p = &W[size_t(OUT0 + o * (BRAIN_HID + 1))];
                    float acc = p[BRAIN_HID];
                    for (int j = 0; j < BRAIN_HID; ++j) acc += p[j] * h[j];
                    a[o] = acc; y[o] = squash(acc);
                }
                // backward: squared error, weighted per output
                targetsFor(s, t, wt);
                for (int o = 0; o < BRAIN_OUT; ++o) dA[o] = 2.0f * wt[o] * (y[o] - t[o]) * squashSlope(a[o]);
                std::fill(dZ, dZ + BRAIN_HID, 0.0f);
                for (int o = 0; o < BRAIN_OUT; ++o) {
                    float *g = &G[size_t(OUT0 + o * (BRAIN_HID + 1))];
                    const float *p = &W[size_t(OUT0 + o * (BRAIN_HID + 1))];
                    for (int j = 0; j < BRAIN_HID; ++j) { g[j] += dA[o] * h[j]; dZ[j] += dA[o] * p[j]; }
                    g[BRAIN_HID] += dA[o];
                }
                for (int j = 0; j < BRAIN_HID; ++j) {
                    const float d = dZ[j] * squashSlope(z[j]);
                    float *g = &G[size_t(j * (BRAIN_IN + 1))];
                    for (int i = 0; i < BRAIN_IN; ++i) g[i] += d * s.in[i];
                    g[BRAIN_IN] += d;
                }
            }
            // Adam
            ++step;
            const float inv = 1.0f / float(b1 - b0);
            const float c1 = 1.0f - std::pow(B1, float(step)), c2 = 1.0f - std::pow(B2, float(step));
            for (int k = 0; k < BRAIN_SIZE; ++k) {
                const float g = G[size_t(k)] * inv;
                M[size_t(k)] = B1 * M[size_t(k)] + (1.0f - B1) * g;
                V[size_t(k)] = B2 * V[size_t(k)] + (1.0f - B2) * g * g;
                W[size_t(k)] -= LR * (M[size_t(k)] / c1) / (std::sqrt(V[size_t(k)] / c2) + 1e-7f);
            }
        }
    }
    std::copy(W.begin(), W.end(), out.w);
    active = false;

    // how often it means what you meant, on the race it never saw
    int sameSteer = 0, samePedal = 0;
    for (int i : test) {
        const CloneSample &s = book.samples[size_t(i)];
        float o[BRAIN_OUT];
        out.run(s.in, o);
        sameSteer += intentClass(cloneIntent(o[OutSteer])) == intentClass(s.steer);
        samePedal += intentClass(cloneIntent(o[OutPedal])) == intentClass(s.pedal);
    }
    st.agreeSteer = float(sameSteer) / float(test.size());
    st.agreePedal = float(samePedal) / float(test.size());
    book = CloneBook();                                  // (done: the copy and the numbers can go)
    train.clear(); test.clear(); W.clear(); M.clear(); V.clear(); G.clear();
    return true;
}

bool trainClone(const CloneBook &book, const Brain *start, Brain &out, CloneStats &st, int epochs)
{
    CloneTrainer t;
    const bool ok = t.begin(book, start, epochs);
    if (ok) t.run();
    st = t.st;
    if (ok) out = t.out;
    return ok;
}

// the clone's file: "TPPBRAN1", its stats, its weights
bool saveCloneBrain(const std::string &path, const Brain &b, const CloneStats &st)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite("TPPBRAN1", 1, 8, f);
    std::fwrite(&st, sizeof st, 1, f);
    std::fwrite(b.w, sizeof(float), BRAIN_SIZE, f);
    std::fclose(f);
    return true;
}

bool loadCloneBrain(const std::string &path, Brain &b, CloneStats &st)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char tag[8];
    const bool ok = std::fread(tag, 1, 8, f) == 8 && std::equal(tag, tag + 8, "TPPBRAN1") && std::fread(&st, sizeof st, 1, f) == 1
                    && std::fread(b.w, sizeof(float), BRAIN_SIZE, f) == size_t(BRAIN_SIZE);
    std::fclose(f);
    return ok;
}

} // namespace sim
