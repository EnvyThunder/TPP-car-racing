// The race director (docs/cpu-psychology/README.md, milestone 3): the CPUs react to the people
// in the race. Every person gets a rival CPU; once a second the director looks at each CPU
// and the person it is racing, and sets its levers - only behaviour, never speed beyond the
// top speed every car has:
//   effort      how much of the full top speed it commits to (+/- a few percent)
//   mistakes    how often it slips
//   risk        side ramps, how long and how often it boosts
//   tactics     hunting, pressuring (tailgating), covering a side, a surprise attack
// Excitement: the rival hunts a person who leads by a lot; CPUs ahead of a person who is far
// behind or tilted ease off a little (more mistakes, less effort); a confident person gets
// one surprise attack in the middle third; a timid one gets pressured from behind.
// Habits (from the person's profile, as far as it is sure): a CPU near them takes their
// favourite shortcut too, covers the side they like to pass on, takes the side ramp they
// like first, boosts just before their favourite boosting spot. A rammer gets room from the
// clean racers. The last half lap (a sprint: its last 15%) is pure - the director is off.
#include "gamesim.h"

#include <algorithm>
#include <cmath>

namespace sim {

void GameSim::setPlayerHints(int car, const PlayerHints &h)
{
    if (car >= 0 && car < MAX_RACERS) hints[size_t(car)] = h;
}

void GameSim::clearPlayerHints()
{
    for (PlayerHints &h : hints) h = PlayerHints();
}

// A named driver says something to a person: at most one line every 20 s per person, never
// twice in a row from the same driver (the start and the finish always get through)
void GameSim::taunt(int speaker, int moment, int target)
{
    if (speaker < 0 || speaker >= int(cars.size()) || target < 0 || target >= int(cars.size())) return;
    if ((cars[size_t(speaker)].rosterId < 0 && !cars[size_t(speaker)].aiClone) || !isPerson(target) || isPerson(speaker)) return;
    const bool always = moment == TauntStart || moment == TauntWin || moment == TauntLost;
    if (!always && (tick - tauntTick[size_t(target)] < TAUNT_GAP || tauntBy[size_t(target)] == speaker)) return;
    tauntTick[size_t(target)] = tick;
    tauntBy[size_t(target)] = speaker;
    uint32_t h = aiSeed * 2654435761u ^ uint32_t(tick) * 40503u ^ uint32_t(speaker + 3) * 2246822519u;
    h ^= h >> 13;
    SimEvent ev;
    ev.type = SimEvent::Taunt;
    ev.carIndex = speaker;
    ev.count = moment;
    ev.vx = float(target);
    ev.intensity = float(h & 1u);
    ev.x = cars[size_t(speaker)].x; ev.y = cars[size_t(speaker)].y + 3.0f; ev.z = cars[size_t(speaker)].z;
    events.push_back(ev);
}

int GameSim::attitudeOf(const Car &c, int person) const
{
    if (c.rosterId < 0 || person < 0 || person >= MAX_RACERS || !hints[size_t(person)].known) return AttitudeNone;
    return hints[size_t(person)].attitude[size_t(std::clamp(c.rosterId, 0, ROSTER_SIZE - 1))];
}

std::vector<RivalRace> GameSim::rivalRaces(int person) const
{
    std::vector<RivalRace> out;
    if (person < 0 || person >= int(cars.size())) return out;
    const Car &p = cars[size_t(person)];
    const RaceObs &po = raceObs(person);
    for (int j = 0; j < int(cars.size()); ++j) {
        const Car &c = cars[size_t(j)];
        if (j == person || c.rosterId < 0) continue;
        RivalRace r;
        r.rosterId = c.rosterId;
        r.ramsByYou = j < PM_CARS ? po.ramsOn[size_t(j)] : 0;
        r.ramsOnYou = person < PM_CARS ? raceObs(j).ramsOn[size_t(person)] : 0;
        r.youAhead = rankOf(person) < rankOf(j);
        r.close = (p.finished && c.finished) ? std::fabs(p.finishTime - c.finishTime) < 3.0f
                                             : std::abs(rankOf(person) - rankOf(j)) == 1 && std::fabs(p.progress - c.progress) < 100.0f;
        out.push_back(r);
    }
    return out;
}

bool GameSim::isPerson(int i) const
{
    if (i < 0 || i >= int(cars.size())) return false;
    return cars[size_t(i)].aiLevel == 0;
}

// Roles: each person (in grid order) gets the CPU closest to their skill as their rival
void GameSim::directorRoles()
{
    for (Car &c : cars) { c.dirRole = 0; c.dirRivalOf = -1; }
    for (int h = 0; h < int(cars.size()); ++h) {
        if (!isPerson(h)) continue;
        const float want = hints[size_t(h)].known ? cpuPrecisionFor(hints[size_t(h)].skill) : cpuSkillCentre;
        int best = -1;
        float bestD = 1e9f;
        for (int j = 0; j < int(cars.size()); ++j) {
            const Car &c = cars[size_t(j)];
            if (isPerson(j) || c.dirRole == 1) continue;
            // (the person's nemesis first, then a named driver determined to beat them)
            const float pref = c.rosterId >= 0 && c.rosterId == hints[size_t(h)].nemesis ? -10.0f
                             : attitudeOf(c, h) == AttitudeDetermined ? -5.0f : 0.0f;
            const float d = pref + std::fabs((c.aiSkill < 0.0f ? cpuSkillCentre : c.aiSkill) - want);
            if (d < bestD) { bestD = d; best = j; }
        }
        if (best >= 0) { cars[size_t(best)].dirRole = 1; cars[size_t(best)].dirRivalOf = h; }
    }
    rolesSet = true;
}

void GameSim::directorStep()
{
    if (!directorOn) return;
    if (!rolesSet) directorRoles();
    const float total = raceDistance();
    const float pureFrom = sprint() ? total * 0.85f : total - 0.5f * lapLen;   // the last half lap is pure
    for (int i = 0; i < int(cars.size()); ++i) {
        Car &c = cars[size_t(i)];
        if (isPerson(i)) continue;
        // neutral levers, then whatever applies
        c.dirEffort = 0.0f; c.dirMistake = 1.0f; c.dirRamp = 0; c.dirBoostHold = 0; c.dirBoostEvery = 0;
        c.dirMode = DirNone; c.dirTarget = -1; c.dirCover = 0; c.dirCut = -1; c.dirPressure = -1;
        if (c.wreck > 0 || c.finished || c.progress >= pureFrom) continue;
        // the person this CPU races: its own as a rival, else the nearest one on the road
        int h = c.dirRole == 1 ? c.dirRivalOf : -1;
        if (h < 0) {
            float nearest = 1e9f;
            for (int j = 0; j < int(cars.size()); ++j) {
                if (!isPerson(j)) continue;
                const float d = std::fabs(cars[size_t(j)].progress - c.progress);
                if (d < nearest) { nearest = d; h = j; }
            }
        }
        if (h < 0) continue;                             // no people in this race (tests)
        const Car &p = cars[size_t(h)];
        const RaceObs *po = obsOf(h);
        const PlayerHints &ph = hints[size_t(h)];
        const float trust = ph.known ? std::clamp(ph.sure, 0.0f, 1.0f) : 0.0f;
        const float gap = p.progress - c.progress;      // > 0: the person is ahead of this CPU
        const int pRank = rankOf(h);
        float pLead = 0.0f;                              // the person's lead, if leading
        if (pRank == 1) {
            pLead = 1e9f;
            for (int j = 0; j < int(cars.size()); ++j)
                if (j != h) pLead = std::min(pLead, p.progress - cars[size_t(j)].progress);
        }
        const float tilt = po ? po->tilt : 0.0f, conf = po ? po->confidence : 0.0f;
        c.dirTarget = h;

        // ---- excitement
        if (std::fabs(gap) < 15.0f) {
            // a close battle: let them race
        } else if (c.dirRole == 1 && pRank == 1 && pLead > (attitudeOf(c, h) == AttitudeDetermined ? 40.0f : 80.0f) && gap > 0.0f) {
            c.dirMode = DirHunt;                         // the rival goes after a person who leads clearly
            c.dirEffort = 0.05f;
            c.dirMistake = 0.7f;
            c.dirRamp = 25;
            c.dirBoostHold = 200;
            c.dirBoostEvery = 300;
        } else if (gap < -80.0f && (tilt > 0.30f || gap < -200.0f)) {
            c.dirMode = DirEase;                         // far ahead of a struggling person: ease off a little
            c.dirEffort = -0.06f;
            c.dirMistake = 1.6f;
        }
        // the whole field stays together: a CPU running away at the front eases a little, one
        // far behind the leader pushes a little (any car - this is what makes lead changes)
        if (c.dirMode == DirNone) {
            float leader = -1e9f, second = -1e9f;
            int leaderIdx = -1;
            for (int j = 0; j < int(cars.size()); ++j) {
                const float pr = cars[size_t(j)].progress;
                if (pr > leader) { second = leader; leader = pr; leaderIdx = j; }
                else if (pr > second) second = pr;
            }
            if (c.progress >= leader && leader - second > 120.0f) { c.dirMode = DirEase; c.dirEffort = -0.04f; c.dirMistake = 1.3f; }
            else if (leader - c.progress > 200.0f && !isPerson(leaderIdx)) { c.dirEffort = 0.04f; }   // (a person leading: only the rival hunts)
            // close behind any car: it pushes to get past (more overtakes, more lead changes)
            for (int j = 0; j < int(cars.size()) && c.dirMode == DirNone; ++j) {
                if (j == i || cars[size_t(j)].path != c.path || cars[size_t(j)].wreck > 0) continue;
                const float ahead = aheadOf(c.s, cars[size_t(j)].s);
                if (ahead > 5.0f && ahead < 30.0f) c.dirEffort = std::max(c.dirEffort, 0.04f);
            }
        }
        // a confident person: one surprise attack in the middle third, from a CPU close behind
        const bool middle = p.progress > total / 3.0f && p.progress < total * 2.0f / 3.0f;
        if (c.dirMode == DirNone && middle && conf > 0.6f && !surpriseUsed[size_t(h)] && gap > 0.0f && gap < 80.0f) {
            surpriseUsed[size_t(h)] = true;
            c.dirSurpriseUntil = tick + 12 * 60;
        }
        if (tick < c.dirSurpriseUntil) {
            c.dirMode = DirSurprise;
            c.dirEffort = 0.10f;
            c.dirMistake = 0.7f;
            c.dirBoostHold = 240;
            c.dirBoostEvery = 120;
            c.dirRamp = 30;
        }
        // a timid person (low risk and aggression): pressure from right behind
        if (c.dirMode == DirNone && trust > 0.3f && ph.risk < 0.3f && ph.aggression < 0.2f && gap > 3.0f && gap < 40.0f) {
            c.dirMode = DirPressure;
            c.dirPressure = h;
            c.dirEffort = 0.04f;
        }

        // what this named driver thinks of the person: determined pushes when near them, cocky
        // slips when they sit right behind
        if (attitudeOf(c, h) == AttitudeDetermined && std::fabs(gap) < 50.0f) c.dirEffort = std::max(c.dirEffort, 0.02f);
        if (attitudeOf(c, h) == AttitudeCocky && gap < -3.0f && gap > -20.0f) c.dirMistake *= 1.4f;

        // ---- countering habits (as far as the profile is sure)
        if (trust > 0.3f && std::fabs(gap) < 70.0f) {
            // the favourite shortcut: take it too
            if (ph.shortcut > 0.6f && hasShortcut()) c.dirCut = 1;
            // the favourite passing side: cover it when the person is right behind
            if (gap < -3.0f && gap > -25.0f && std::fabs(ph.passLeft - 0.5f) > 0.15f)
                c.dirCover = ph.passLeft > 0.5f ? -1 : 1;
            // a ramp lover: take the side ramps first when just ahead of them
            if (ph.ramps > 0.6f && gap < 0.0f) c.dirRamp = std::max(c.dirRamp, 50);
            // the favourite boosting spot: boost just before it when ahead of them
            if (ph.boostBin >= 0 && gap < 0.0f && gap > -60.0f) {
                const float f = sprint() ? std::clamp(c.progress / total, 0.0f, 0.999f) : wrapS(c.s) / lapLen;
                const int bin = int(f * PM_BOOST_BINS);
                if (bin == (ph.boostBin + PM_BOOST_BINS - 1) % PM_BOOST_BINS) { c.dirBoostEvery = 1; c.dirBoostHold = std::max(c.dirBoostHold, 120); }
            }
            if (c.dirCut >= 0 || c.dirCover != 0 || c.dirRamp >= 50 || c.dirBoostEvery == 1)
                if (c.dirMode == DirNone) c.dirMode = DirCounter;
        }
        // a rammer: the clean racers give room (they wait rather than pass close)
        if (trust > 0.3f && ph.aggression > 0.6f && c.aiPersona == PersonaClean && std::fabs(gap) < 40.0f) c.dirMode = DirGiveRoom;
    }
}

const char *directorModeShort(int m)
{
    static const char *N[NUM_DIR_MODES] = {"", "HUNT", "EASE", "SURPRISE", "PRESSURE", "COUNTER", "ROOM"};
    return N[std::clamp(m, 0, NUM_DIR_MODES - 1)];
}

} // namespace sim
