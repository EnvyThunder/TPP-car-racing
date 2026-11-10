// The CPU driver's mind: skill, personality, mood, tactics and mistakes (see aidriver.h).
// GameSim::computeAIInput (gamesim.cpp) still does the driving itself - finding the gaps,
// steering, braking for bends - and asks aiThink what this driver wants.
#include "gamesim.h"

#include <algorithm>
#include <cmath>

namespace sim {

const PersonaDef &personaDef(int p)
{
    //                            name            short  aggr  block ramps boost random grudge mistakes late
    static const PersonaDef DEFS[NUM_PERSONAS] = {
        {"ATTACKER",     "ATK", 0.85f, 0.30f, 0.50f, 0.50f, 0.30f, 0.60f, 1.10f, false},
        {"BLOCKER",      "BLK", 0.40f, 0.95f, 0.30f, 0.30f, 0.25f, 0.40f, 0.90f, false},
        {"LATE CHARGER", "LTC", 0.50f, 0.30f, 0.40f, 0.70f, 0.20f, 0.20f, 0.90f, true},
        {"SHOW-OFF",     "SHO", 0.45f, 0.20f, 1.00f, 0.95f, 0.40f, 0.20f, 1.20f, false},
        {"CLEAN RACER",  "CLN", 0.20f, 0.15f, 0.25f, 0.20f, 0.15f, 0.00f, 0.70f, false},
        {"GRUDGE",       "GRD", 0.55f, 0.50f, 0.40f, 0.40f, 0.25f, 1.00f, 1.00f, false},
        {"WILD CARD",    "WLD", 0.60f, 0.40f, 0.70f, 0.70f, 1.00f, 0.50f, 1.60f, false},
    };
    return DEFS[std::clamp(p, 0, NUM_PERSONAS - 1)];
}

const char *moodName(int m)
{
    static const char *N[NUM_MOODS] = {"CALM", "CONFIDENT", "PRESSURED", "SHAKEN", "REVENGE", "DESPERATE"};
    return N[std::clamp(m, 0, NUM_MOODS - 1)];
}

const char *moodShort(int m)
{
    static const char *N[NUM_MOODS] = {"CALM", "CONF", "PRES", "SHKN", "REV", "DESP"};
    return N[std::clamp(m, 0, NUM_MOODS - 1)];
}

const char *tacticShort(int t)
{
    static const char *N[NUM_TACTICS] = {"LINE", "PASS-L", "PASS-R", "WAIT", "DEFEND", "ATTACK"};
    return N[std::clamp(t, 0, NUM_TACTICS - 1)];
}

// a number from 0 to 1 from the race's AI seed, the car, the tick and a "what for" salt
static float aiRand(uint32_t seed, int idx, int tick, uint32_t salt)
{
    uint32_t h = seed * 2654435761u ^ (uint32_t(idx + 1) * 2246822519u) ^ (uint32_t(tick) * 40503u) ^ (salt * 3266489917u);
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return float(h >> 8) / 16777216.0f;
}

// The CPU precision whose driving the player model measures as this skill (tools/ai_bench
// "calib": six CPUs at each precision, every map) - so CPUs centred on your profile drive at
// your pace
float cpuPrecisionFor(float measuredSkill)
{
    static const float MEASURED[11] = {0.19f, 0.27f, 0.41f, 0.52f, 0.67f, 0.71f, 0.81f, 0.90f, 0.94f, 0.955f, 0.97f};   // (with the evolved brains)
    if (measuredSkill <= MEASURED[0]) return 0.0f;
    for (int k = 1; k < 11; ++k)
        if (measuredSkill <= MEASURED[k])
            return 0.1f * (float(k - 1) + (measuredSkill - MEASURED[k - 1]) / (MEASURED[k] - MEASURED[k - 1]));
    return 1.0f;
}

void GameSim::setAiSeed(uint32_t s) { aiSeed = s ? s : 1u; }
float cpuCentreFor(float measuredSkill, float sure, float challenge)
{
    const float known = NEW_PLAYER_SKILL * (1.0f - sure) + measuredSkill * sure;
    return std::max(CPU_FLOOR, cpuPrecisionFor(known) + 0.5f * challenge);
}

void GameSim::setCpuSkill(float centre) { cpuSkillCentre = std::clamp(centre, 0.0f, 1.0f); }
// (the challenge moves the centre itself: GLWidget::startRace and the race server add it)

// A CPU gets its personality and skill the first time it thinks in a race: the personality
// from the race's AI seed; the skill spread around the centre (the player's profile: offline
// yours, online the room's people), plus its form of the day
void GameSim::aiMeet(Car &c, int idx)
{
    if (c.aiPersona < 0) c.aiPersona = std::min(NUM_PERSONAS - 1, int(aiRand(aiSeed, idx, 0, 11) * float(NUM_PERSONAS)));
    if (c.aiSkill < 0.0f && c.rosterId >= 0) {         // a named driver: its own lean, sharper on its home map
        const RosterDriver &R = rosterDriver(c.rosterId);
        const float spread = R.skillLean + (aiRand(aiSeed, idx, 0, 12) - 0.5f) * 0.16f + (R.homeMap == mapIndex ? 0.05f : 0.0f);
        const float form = (aiRand(aiSeed, idx, 0, 13) - 0.5f) * 0.10f;
        c.aiSkill = std::clamp(cpuSkillCentre + spread + form, 0.0f, 1.0f);
    }
    if (c.aiSkill < 0.0f) {
        const float spread = (aiRand(aiSeed, idx, 0, 12) - 0.5f) * 0.36f;   // -0.18..+0.18
        const float form = (aiRand(aiSeed, idx, 0, 13) - 0.5f) * 0.10f;     // a good or bad day
        c.aiSkill = std::clamp(cpuSkillCentre + spread + form, 0.0f, 1.0f);
    }
}

// The mood, every quarter second: shaken after a wreck, revenge on the car that hit it (its
// grudge decides how often), desperate when behind in the last third, pressured with a car
// sitting behind it, confident when leading clearly - else calm
void GameSim::aiMood(Car &c, int idx, const PersonaDef &P)
{
    const RaceObs *ob = obsOf(idx);
    const bool lastThird = c.progress > raceDistance() * 2.0f / 3.0f;
    const int rank = rankOf(idx);
    float leader = c.progress, behindGap = 1e9f;
    for (int j = 0; j < int(cars.size()); ++j) {
        if (j == idx) continue;
        leader = std::max(leader, cars[size_t(j)].progress);
        const float g = c.progress - cars[size_t(j)].progress;
        if (g > 0.0f) behindGap = std::min(behindGap, g);
    }
    // a new hit: revenge or not (decided once per hit)
    if (ob && ob->lastHitBy >= 0 && ob->lastHitTick != c.aiRevengeHit && tick - ob->lastHitTick < 120) {
        c.aiRevengeHit = ob->lastHitTick;
        const float want = attitudeOf(c, ob->lastHitBy) == AttitudeGrudge ? 1.0f       // (a grudge: always)
                         : 0.15f + 0.75f * P.grudge + 0.2f * P.aggression;
        if (aiRand(aiSeed, idx, ob->lastHitTick, 21) < want) {
            c.aiMoodTarget = ob->lastHitBy; c.aiMoodUntil = tick + 1800;
            taunt(idx, TauntRevenge, ob->lastHitBy);
        }
    }
    int m = MoodCalm;
    if (ob && tick - ob->lastWreckTick < 1200) m = MoodShaken;
    else if (c.aiMoodTarget >= 0 && tick < c.aiMoodUntil && c.aiMoodTarget < int(cars.size())) m = MoodRevenge;
    else if (lastThird && rank > 2 && leader - c.progress > 80.0f) m = MoodDesperate;
    else if (c.aiPressure > 240) m = MoodPressured;
    else if (rank == 1 && behindGap > 120.0f) m = MoodConfident;
    if (m != MoodRevenge && tick >= c.aiMoodUntil) c.aiMoodTarget = -1;
    c.aiMood = m;
}

AiView GameSim::aiThink(Car &c, int idx, bool cpu)
{
    AiView v;
    if (!cpu) {                                          // the neutral reference driver (tests, autodrive)
        const uint32_t hb = uint32_t(idx) * 37u + uint32_t(c.lap) * 11u + uint32_t(c.wrecks) * 101u;
        v.boostHold = 30 + int(hb % 60u);   // 0.5-1.5 s: the cheap part
        return v;
    }
    aiMeet(c, idx);
    const PersonaDef &P = personaDef(c.aiPersona);
    const float s = c.aiSkill;
    const bool racing = state == Racing && !c.finished && c.wreck == 0;
    const bool lastThird = c.progress > raceDistance() * 2.0f / 3.0f;
    const float EDGE = roadHalfAt(c.s, c.path) - 1.0f;

    // a car sitting close behind: pressure builds (and fades twice as fast)
    bool tailed = false;
    for (int j = 0; j < int(cars.size()); ++j) {
        if (j == idx || cars[size_t(j)].path != c.path || cars[size_t(j)].wreck > 0) continue;
        const float behind = aheadOf(cars[size_t(j)].s, c.s);
        if (behind > 0.0f && behind < 12.0f) tailed = true;
    }
    c.aiPressure = tailed ? std::min(c.aiPressure + 1, 1200) : std::max(0, c.aiPressure - 2);
    if (racing && (tick + idx) % 15 == 0) aiMood(c, idx, P);
    const int mood = c.aiMood;

    // the line it drives wanders: a new aim every 2-4 s, up to 1.8 m off for a weak driver
    if (racing && tick >= c.aiLineUntil) {
        c.aiLineGoal = (aiRand(aiSeed, idx, tick, 31) - 0.5f) * 2.0f * 2.4f * (1.0f - s);
        c.aiLineUntil = tick + 120 + int(aiRand(aiSeed, idx, tick, 32) * 120.0f);
    }
    c.aiLineErr += (c.aiLineGoal - c.aiLineErr) * 0.02f;

    // human mistakes, once a second a small chance (more under pressure, fewer when confident)
    if (racing && (tick + idx) % 60 == 0 && tick >= c.aiMistakeUntil) {
        static const float MOOD_MUL[NUM_MOODS] = {1.0f, 0.7f, 1.8f, 1.3f, 1.2f, 1.5f};
        const float bold = std::max(0.0f, cpuChallenge);
        const float p = (0.045f * (1.0f - s) + 0.004f) * P.mistakes * MOOD_MUL[mood] * c.dirMistake * (1.0f - 0.8f * bold);   // (the director's lever too)
        if (aiRand(aiSeed, idx, tick, 41) < p) {
            const float k = aiRand(aiSeed, idx, tick, 42);
            c.aiMistake = c.boosting && k < 0.25f ? MistakeOverBoost : k < 0.55f ? MistakeLateBrake : k < 0.85f ? MistakeWide : MistakeLateSee;
            c.aiMistakeUntil = tick + 60 + int(aiRand(aiSeed, idx, tick, 43) * 60.0f);
            c.aiMistakeSide = aiRand(aiSeed, idx, tick, 44) < 0.5f ? -1 : 1;
        }
    }
    if (tick >= c.aiMistakeUntil) c.aiMistake = MistakeNone;

    // what its mood does to its aggression
    float aggr = P.aggression;
    if (mood == MoodRevenge) aggr += 0.3f;
    if (mood == MoodDesperate) aggr += 0.3f;
    if (mood == MoodShaken) aggr -= 0.4f;
    if (mood == MoodConfident) aggr -= 0.1f;
    if (c.dirMode == DirSurprise || c.dirMode == DirHunt) aggr += 0.3f;
    if (c.dirMode == DirGiveRoom) aggr -= 0.4f;
    aggr = std::clamp(aggr, 0.0f, 1.0f);

    // tactics: ten times a second, if the last choice has run out (or its car is gone)
    auto carOk = [&](int j) { return j >= 0 && j < int(cars.size()) && j != idx && cars[size_t(j)].wreck == 0 && cars[size_t(j)].path == c.path; };
    if (racing && (tick >= c.aiTacticUntil || (c.aiTacticCar >= 0 && !carOk(c.aiTacticCar)))) {
        int ahead = -1, behind = -1;
        float aBest = 40.0f, bBest = 15.0f;
        for (int j = 0; j < int(cars.size()); ++j) {
            if (!carOk(j) || cars[size_t(j)].ghost > 0) continue;
            const float a = aheadOf(c.s, cars[size_t(j)].s);
            if (a > 0.0f && a < aBest) { aBest = a; ahead = j; }
            if (a < 0.0f && -a < bBest) { bBest = -a; behind = j; }
        }
        if (mood == MoodRevenge && carOk(c.aiMoodTarget)) {   // revenge: the target, if it is near ahead
            const float a = aheadOf(c.s, cars[size_t(c.aiMoodTarget)].s);
            if (a > 0.0f && a < 60.0f) ahead = c.aiMoodTarget;
        }
        float u[NUM_TACTICS];
        for (float &x : u) x = -1e9f;
        u[TacticFollow] = 1.0f;
        if (ahead >= 0) {
            const Car &A = cars[size_t(ahead)];
            const float roomL = A.d + EDGE, roomR = EDGE - A.d;   // road beside it on each side
            u[TacticPassLeft] = 0.7f + 0.9f * aggr + (roomL < 3.0f ? -3.0f : 0.0f);
            u[TacticPassRight] = 0.7f + 0.9f * aggr + (roomR < 3.0f ? -3.0f : 0.0f);
            u[TacticWait] = 0.5f + 0.8f * (1.0f - aggr) + (mood == MoodShaken ? 0.8f : 0.0f) + (c.aiPersona == PersonaClean ? 0.5f : 0.0f);
            u[TacticAttack] = -1.2f + 1.6f * aggr + (ahead == c.aiMoodTarget ? 2.5f : 0.0f) + (mood == MoodDesperate ? 0.7f : 0.0f)
                            + (attitudeOf(c, ahead) == AttitudeGrudge ? 0.8f : 0.0f)
                            + (c.aiPersona == PersonaClean ? -3.0f : 0.0f) + (c.aiPersona == PersonaWildCard ? 0.4f : 0.0f);
        }
        if (behind >= 0)
            u[TacticDefend] = 0.2f + 1.3f * P.block + (mood == MoodPressured ? 0.6f : 0.0f) + (lastThird ? 0.3f : 0.0f)
                            + (attitudeOf(c, behind) == AttitudeGrudge ? 0.8f : 0.0f);
        // weighted-random pick (softmax): a loose personality picks the second-best more often
        const float T = 0.20f + 0.8f * P.randomness;
        float best = -1e9f;
        for (float x : u) best = std::max(best, x);
        float w[NUM_TACTICS], sum = 0.0f;
        for (int k = 0; k < NUM_TACTICS; ++k) { w[k] = u[k] < -1e8f ? 0.0f : std::exp((u[k] - best) / T); sum += w[k]; }
        float r = aiRand(aiSeed, idx, tick, 51) * sum;
        int pick = TacticFollow;
        for (int k = 0; k < NUM_TACTICS; ++k) { if (r < w[k]) { pick = k; break; } r -= w[k]; }
        c.aiTactic = pick;
        c.aiTacticCar = pick == TacticDefend ? behind : (pick == TacticFollow ? -1 : ahead);
        c.aiTacticUntil = tick + 60 + int(aiRand(aiSeed, idx, tick, 52) * 120.0f);   // keeps it 1-3 s
    }

    // ---- the view: precision from skill, then mood and mistakes on top
    v.skill = s;
    v.lineErr = std::clamp(c.aiLineErr + (c.aiMistake == MistakeWide ? 2.5f * float(c.aiMistakeSide) : 0.0f), -EDGE + 1.0f, EDGE - 1.0f);
    v.liftAt = 0.80f + (s - 0.7f) * 0.40f;
    v.brakeAt = 1.05f + (s - 0.7f) * 0.45f;
    if (mood == MoodShaken) { v.liftAt -= 0.05f; v.brakeAt -= 0.05f; }
    if (mood == MoodDesperate) { v.liftAt += 0.05f; v.brakeAt += 0.05f; }
    if (c.aiMistake == MistakeLateBrake) { v.liftAt += 0.30f; v.brakeAt += 0.40f; }
    v.horizonMul = (0.6f + 0.4f * s) * (c.aiMistake == MistakeLateSee ? 0.4f : 1.0f);
    v.rollSlack = 0.15f + 0.6f * (1.0f - s) + (mood == MoodDesperate ? 0.1f : 0.0f);
    v.commit = std::min(1.0f, 0.60f + 0.40f * s + (mood == MoodConfident ? 0.02f : 0.0f) + (mood == MoodDesperate ? 0.03f : 0.0f)
                              + (mood == MoodRevenge ? 0.02f : 0.0f) - (mood == MoodShaken ? 0.05f : 0.0f) + c.dirEffort);
    v.rampChance = std::clamp(int(25.0f + 55.0f * P.rampLove + 15.0f * s) + (mood == MoodDesperate ? 25 : 0) - (mood == MoodShaken ? 30 : 0), 5, 97);
    const float bold = std::max(0.0f, cpuChallenge);
    v.rampChance = std::clamp(v.rampChance + c.dirRamp + int(30.0f * bold), 5, 97);
    v.cutChance = c.dirCut == 1 ? 95 : std::clamp(int(25.0f + 50.0f * s + 20.0f * P.aggression + 30.0f * bold) + (mood == MoodDesperate ? 20 : 0), 5, 95);
    if (carOk(c.aiTacticCar)) {
        switch (c.aiTactic) {
        case TacticPassLeft:  v.passCar = c.aiTacticCar; v.passSide = -1; break;
        case TacticPassRight: v.passCar = c.aiTacticCar; v.passSide = 1; break;
        case TacticWait:      v.waitCar = c.aiTacticCar; break;
        case TacticDefend:    v.defendCar = c.aiTacticCar; break;
        case TacticAttack:    v.attackCar = c.aiTacticCar; break;
        default: break;
        }
    }
    // nitro and the L boost: a late charger keeps both for the last third
    v.saveNitro = P.lateCharger && !lastThird && bold < 0.5f;
    const int presses = obsOf(idx) ? obsOf(idx)->boosts : 0;   // (each press has its own length)
    const float b = aiRand(aiSeed, idx, presses, 61);
    // (GameSim::BOOST_RATE: 10 health a second, 15 after 1.5 s, 25 after 3 s, burnt out at 5 s)
    v.boostHold = 30 + int(90.0f * P.boostLove * (0.5f + b));                     // 0.5 to about 2.7 s
    if (c.aiPersona == PersonaShowOff) v.boostHold = std::max(v.boostHold, 120 + int(b * 90.0f));   // 2-3.5 s
    if (c.aiPersona == PersonaWildCard && b < 0.15f) v.boostHold = 310 + int(b * 200.0f);   // sometimes far too long: burns out
    if (mood == MoodDesperate) v.boostHold = std::max(v.boostHold, 200);             // 3.3 s
    if (c.aiMistake == MistakeOverBoost) v.boostHold = 320;                          // a mistake: burns out
    v.boostEvery = mood == MoodDesperate ? 300 : std::max(300, int((1500.0f - 1000.0f * P.boostLove) * (1.0f - 0.6f * bold)));
    if (P.lateCharger && !lastThird && bold < 0.5f) v.boostEvery = 1000000;
    v.boostAnyRoad = P.boostLove > 0.8f || c.aiPersona == PersonaWildCard || mood == MoodDesperate;
    // the director's wishes on top
    if (c.dirBoostEvery > 0) {
        v.boostEvery = c.dirBoostEvery;
        v.boostHold = std::max(v.boostHold, c.dirBoostHold);
        v.boostAnyRoad = v.boostAnyRoad || c.dirMode == DirHunt || c.dirMode == DirSurprise;
        v.saveNitro = false;
    }
    if (c.dirPressure >= 0 && c.dirPressure < int(cars.size())) v.pressureCar = c.dirPressure;
    if (c.dirCover != 0 && c.dirTarget >= 0 && c.dirTarget < int(cars.size())) {   // cover the person behind
        v.defendCar = c.dirTarget;
        v.coverSide = c.dirCover;
        v.passCar = v.waitCar = v.attackCar = -1;
    }
    return v;
}

} // namespace sim
