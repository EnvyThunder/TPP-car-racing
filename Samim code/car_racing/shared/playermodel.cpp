// The player model: one race's measurements -> traits; traits and habits kept per player.
// The weights below were set with tools/ai_bench ("profile": simulated players of known
// styles must come out with the right traits).
#include "playermodel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sim {

static float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

RaceTraits raceTraits(const RaceObs &o)
{
    RaceTraits t;
    const float km = o.metres / 1000.0f;
    t.valid = km >= 1.0f;
    if (km < 0.2f) return t;
    const float perKm = std::max(km, 3.0f);              // (rates over at least 3 km: a ram in the first metres isn't "1 a km")

    // skill: pace against the clean CPU reference, less a share for mistakes per km
    float pace = 0.0f;
    for (float r : o.sectorRatio) pace += std::clamp(r, 0.4f, 1.3f);
    pace = o.sectorRatio.empty() ? 0.85f : pace / float(o.sectorRatio.size());
    const float mistakes = (0.3f * o.rail + 0.4f * o.offroad + 1.0f * o.barrier + 0.3f * o.objects + 0.8f * o.traffic
                          + 0.5f * o.roughLandings + 1.2f * o.crashLandings + 1.5f * o.flewOff + 2.0f * o.wrecks) / perKm;
    t.skill = clamp01(clamp01((pace - 0.70f) / 0.28f) - std::min(0.35f, 0.12f * mistakes));

    // consistency: how much the same tenth of the lap varies from lap to lap
    float cvSum = 0.0f;
    int cvN = 0;
    for (int k = 0; k < PM_SECTORS; ++k) {
        float sum = 0.0f, sum2 = 0.0f;
        int n = 0;
        for (size_t i = 0; i < o.sectorRatio.size(); ++i)
            if (o.sectorIndex[i] == k) { sum += o.sectorRatio[i]; sum2 += o.sectorRatio[i] * o.sectorRatio[i]; ++n; }
        if (n < 2) continue;
        const float mean = sum / float(n), var = std::max(0.0f, sum2 / float(n) - mean * mean);
        cvSum += std::sqrt(var) / std::max(0.1f, mean);
        ++cvN;
    }
    if (cvN >= 3) {
        t.hasConsistency = true;
        t.consistency = clamp01(1.0f - (cvSum / float(cvN)) / 0.12f);
    }

    // aggression: rams (and revenge), blocking, close passes, tailgating - per km
    t.aggression = clamp01((1.0f * o.rams + 1.0f * o.revenge + 0.5f * o.blocks + 0.15f * o.closePasses
                          + 0.03f * float(o.tailgateTicks) / 60.0f) / perKm / 1.2f);

    // risk: side ramps taken (and the big ones, with nitro), long L boosts
    float boostRisk = 0.0f;
    if (o.boosts > 0) {
        const float avgHold = float(o.boostTicks) / float(o.boosts) / 60.0f;
        boostRisk = clamp01(avgHold / 5.0f + 0.4f * float(o.boostOver3) / float(o.boosts) + 0.8f * float(o.boostOver10) / float(o.boosts));
        boostRisk = clamp01(boostRisk + 0.15f * clamp01(float(o.boosts) / perKm / 0.6f));
    }
    if (o.rampsOffered > 0) {
        const float take = float(o.rampsTaken) / float(o.rampsOffered);
        const float big = o.rampsTaken ? float(o.bigRampsTaken) / float(o.rampsTaken) : 0.0f;
        const float nitro = o.rampsTaken ? float(o.nitroJumps) / float(o.rampsTaken) : 0.0f;
        t.risk = clamp01(0.45f * take + 0.15f * big + 0.10f * nitro + 0.30f * boostRisk);
    } else {
        t.risk = clamp01(0.2f + 0.6f * boostRisk);
    }
    return t;
}

// ---- live mood ----------------------------------------------------------------------
void moodDecay(RaceObs &o)
{
    o.tilt *= 0.999615f;                                 // half gone in 30 s (1800 ticks)
}

void moodMistake(RaceObs &o, int tick, float weight)
{
    o.mistakeStreak = tick - o.lastMistakeTick < 1200 ? o.mistakeStreak + 1 : 1;   // within 20 s: a streak
    o.lastMistakeTick = tick;
    o.tilt = clamp01(o.tilt + 0.025f * weight * (1.0f + 0.5f * float(std::min(o.mistakeStreak, 4))));
}

void moodDamage(RaceObs &o, float amount)
{
    o.tilt = clamp01(o.tilt + amount / 500.0f);
}

void moodWreck(RaceObs &o, int tick)
{
    o.tilt = clamp01(o.tilt + 0.35f + (tick - o.lastWreckTick < 3600 ? 0.25f : 0.0f));   // (a second wreck within a minute: more)
    o.lastWreckTick = tick;
}

void moodSecond(RaceObs &o, int rank, int cars, float gapAhead, float gapBehind)
{
    // places lost in the last 20 s add to the tilt
    const int old = o.rankAgo.back();
    for (int k = int(o.rankAgo.size()) - 1; k > 0; --k) o.rankAgo[size_t(k)] = o.rankAgo[size_t(k - 1)];
    o.rankAgo[0] = rank;
    if (old > 0 && rank > old) o.tilt = clamp01(o.tilt + 0.02f * float(rank - old));
    if (rank == 1) o.leadTicks += 60;
    // confidence: leading (more with a big gap), or near the front; tilt eats it
    float target;
    if (rank == 1) target = 0.65f + std::min(0.30f, gapBehind / 400.0f);
    else target = std::max(0.05f, 0.55f - 0.45f * float(rank - 1) / float(std::max(1, cars - 1)) - std::min(0.15f, gapAhead / 1000.0f));
    target -= 0.5f * o.tilt;
    o.confidence = clamp01(o.confidence + (target - o.confidence) * 0.1f);
}

// ---- the profile ----------------------------------------------------------------------
int PlayerProfile::attitude(int rosterId) const
{
    if (rosterId < 0 || rosterId >= PM_ROSTER) return AttitudeNone;
    const RivalRecord &r = rivals[size_t(rosterId)];
    if (r.ramsByYou >= 3 && r.ramsByYou > r.ramsOnYou + 1) return AttitudeGrudge;
    if (r.youAhead - r.theyAhead >= 2) return AttitudeDetermined;
    if (r.theyAhead - r.youAhead >= 3) return AttitudeCocky;
    return AttitudeNone;
}

int PlayerProfile::nemesis() const
{
    int best = -1, most = 1;                             // at least two close battles
    for (int k = 0; k < PM_ROSTER; ++k)
        if (rivals[size_t(k)].close > most) { most = rivals[size_t(k)].close; best = k; }
    return best;
}

float PlayerProfile::challengeAfter(float now, int place, int cars)
{
    float step = -0.015f;                                // 3rd, 4th ...: a little easier
    if (place <= 1) step = 0.06f;                        // you won: clearly tougher
    else if (place >= cars) step = -0.03f;               // last: a bit easier
    else if (place == 2) step = 0.025f;                  // 2nd: a little tougher
    return std::clamp(now + step, -0.4f, 1.0f);
}

void PlayerProfile::addRace(const RaceObs &o, const RaceSummary &s, const std::vector<RivalRace> &rivalsHere)
{
    if (s.cars >= 2) challenge = challengeAfter(challenge, s.place, s.cars);
    for (const RivalRace &rr : rivalsHere) {
        if (rr.rosterId < 0 || rr.rosterId >= PM_ROSTER) continue;
        RivalRecord &r = rivals[size_t(rr.rosterId)];
        ++r.races;
        r.ramsByYou += rr.ramsByYou;
        r.ramsOnYou += rr.ramsOnYou;
        (rr.youAhead ? r.youAhead : r.theyAhead) += 1;
        r.close += rr.close ? 1 : 0;
    }
    const RaceTraits rt = raceTraits(o);
    if (rt.valid) {
        const float w = races < 4 ? 1.0f / float(races + 1) : 0.2f;
        traits.skill += (rt.skill - traits.skill) * w;
        traits.aggression += (rt.aggression - traits.aggression) * w;
        traits.risk += (rt.risk - traits.risk) * w;
        if (rt.hasConsistency) traits.consistency += (rt.consistency - traits.consistency) * w;
        ++races;
        sure = 1.0f - 1.0f / (1.0f + 0.4f * float(races));
    }
    if (s.map >= 0 && s.map < PM_MAPS) {
        MapHabits &h = habits[size_t(s.map)];
        h.forkOffered += o.forkOffered; h.forkTaken += o.forkTaken;
        h.passLeft += o.passLeft; h.passRight += o.passRight;
        h.rampsOffered += o.rampsOffered; h.rampsTaken += o.rampsTaken;
        h.boosts += o.boosts;
        for (int k = 0; k < PM_BOOST_BINS; ++k) h.boostBins[size_t(k)] += o.boostBins[size_t(k)];
    }
    recent.insert(recent.begin(), s);
    if (recent.size() > 10) recent.resize(10);
}

std::vector<std::string> PlayerProfile::habitLines(const char *const mapNames[PM_MAPS]) const
{
    std::vector<std::string> out;
    char buf[128];
    int left = 0, right = 0, offered = 0, taken = 0;
    for (int m = 0; m < PM_MAPS; ++m) {
        const MapHabits &h = habits[size_t(m)];
        left += h.passLeft; right += h.passRight;
        offered += h.rampsOffered; taken += h.rampsTaken;
        if (h.forkOffered >= 3) {
            const int pct = 100 * h.forkTaken / h.forkOffered;
            if (pct >= 65) { std::snprintf(buf, sizeof buf, "TAKES THE SHORTCUT %d%% OF THE TIME ON %s", pct, mapNames[m]); out.push_back(buf); }
            else if (pct <= 20) { std::snprintf(buf, sizeof buf, "RARELY TAKES THE SHORTCUT ON %s", mapNames[m]); out.push_back(buf); }
        }
        if (h.boosts >= 4) {
            int best = 0;
            for (int k = 1; k < PM_BOOST_BINS; ++k) if (h.boostBins[size_t(k)] > h.boostBins[size_t(best)]) best = k;
            if (h.boostBins[size_t(best)] * 100 >= 35 * h.boosts) {
                std::snprintf(buf, sizeof buf, "BOOSTS AT %d%% OF THE LAP ON %s", best * 100 / PM_BOOST_BINS + 100 / PM_BOOST_BINS / 2, mapNames[m]);
                out.push_back(buf);
            }
        }
    }
    if (left + right >= 6) {
        const int pl = 100 * left / (left + right);
        if (pl >= 65) { std::snprintf(buf, sizeof buf, "OVERTAKES ON THE LEFT %d%% OF THE TIME", pl); out.push_back(buf); }
        else if (pl <= 35) { std::snprintf(buf, sizeof buf, "OVERTAKES ON THE RIGHT %d%% OF THE TIME", 100 - pl); out.push_back(buf); }
    }
    if (offered >= 5) {
        const int pct = 100 * taken / offered;
        if (pct >= 70) { std::snprintf(buf, sizeof buf, "TAKES %d%% OF THE SIDE RAMPS", pct); out.push_back(buf); }
        else if (pct <= 25) { std::snprintf(buf, sizeof buf, "AVOIDS SIDE RAMPS (TAKES %d%%)", pct); out.push_back(buf); }
    }
    if (races >= 2) {
        if (traits.aggression >= 0.6f) out.push_back("RAMS AND BLOCKS A LOT");
        else if (traits.aggression <= 0.15f) out.push_back("A CLEAN DRIVER: HARDLY EVER TOUCHES ANOTHER CAR");
        if (traits.risk >= 0.65f) out.push_back("LOVES RISK: BIG JUMPS AND LONG BOOSTS");
        else if (traits.risk <= 0.2f) out.push_back("PLAYS IT SAFE");
    }
    return out;
}

} // namespace sim
