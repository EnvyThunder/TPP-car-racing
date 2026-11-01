// Network snapshots of the race (server -> clients). Kept apart from gamesim.cpp so
// the simulation itself stays plain C++ (QtCore is only needed here for QDataStream).
#include "gamesim.h"
#include <QDataStream>

namespace sim {

// ---------------------------------------------------------------------------
//  Network snapshot
// ---------------------------------------------------------------------------
void GameSim::writeSnapshot(QDataStream &out, const std::vector<SimEvent> &evs,
                            std::vector<Obstacle> *lastSent) const
{
    out << quint8(state) << quint8(mode) << qint8(mapIndex) << qint8(laps) << qint32(tick) << raceTimer << qint8(winner);

    out << quint8(cars.size());
    for (const Car &c : cars) {
        quint8 flags = (c.nitroOn ? 1 : 0) | (c.isDrifting ? 2 : 0) | (c.finished ? 4 : 0) | (c.ejected ? 8 : 0);
        out << c.x << c.z << qint8(c.path) << c.s << c.d << qint8(c.lap) << c.lapStart << c.bestLap << c.y << c.vy << qint16(c.air)
            << c.heading << c.speed << c.nitro << c.steerVis << c.wheelSpin
            << c.roll << c.pitch << c.driftAngle << c.topSpeed << c.finishTime << c.r << c.g << c.b
            << qint16(c.nitroTimer) << qint8(c.nitroCharges) << qint16(c.nitroLen) << flags
            << c.health << qint16(c.wreck) << quint8(c.ghost) << qint16(std::min(c.calm, 30000))
            << c.wreckX << c.wreckZ << c.wreckVx << c.wreckVz << c.wreckSpin
            << quint8(c.wrecks) << quint8(c.crashesCaused)
            << quint8((c.out ? 4 : 0) | (c.lifeWreck ? 8 : 0) | (c.outside ? 16 : 0) | (c.boosting ? 32 : 0) | (c.cutClosed ? 64 : 0)) << c.lastWreckProg
            << qint16(std::min(c.boostHold, 30000)) << quint8(c.bounties) << c.barrel << c.barrelRate << c.spinRate << c.jumpDanger << c.outS
            << qint8(c.aiPersona) << quint8(c.aiMood) << quint8(c.aiTactic) << quint8(std::clamp(int(c.aiSkill * 255.0f), 0, 255))
            << quint8(c.dirMode) << qint8(c.dirRole ? c.dirRivalOf : -1) << qint8(c.rosterId)
            << quint8((c.aiBrain + 1) | (c.aiBrainDriving ? 16 : 0) | (c.aiBrainOff > 0 ? 32 : 0));
    }

    out << quint8(traffic.size());
    for (const Traffic &t : traffic)
        out << t.s << t.lane << t.targetLane << t.speed;

    // only obstacles that moved (or were put back) since the last snapshot (TCP delivers
    // every snapshot in order)
    std::vector<quint16> moved;
    for (size_t i = 0; i < obstacles.size(); ++i) {
        const Obstacle &o = obstacles[i];
        if (lastSent && i < lastSent->size()) {
            const Obstacle &p = (*lastSent)[i];
            if (p.knocked == o.knocked && p.x == o.x && p.y == o.y && p.z == o.z && p.rot == o.rot) continue;
        } else if (!o.knocked) {
            continue;
        }
        moved.push_back(quint16(i));
    }
    out << quint16(moved.size());
    for (quint16 i : moved) {
        const Obstacle &o = obstacles[i];
        out << i << quint8(o.knocked ? 1 : 0) << o.x << o.y << o.z << o.rot;
    }
    if (lastSent) *lastSent = obstacles;

    out << quint8(nitroPickups.size());
    for (const NitroPickup &np : nitroPickups) out << quint8(np.active ? 1 : 0) << quint8(std::min(np.takes, 255));
    out << quint8(sideJumps.size());
    for (int v : sideJumps) out << quint8(std::min(v, 255));
    out << quint8(mainJumps.size());
    for (int v : mainJumps) out << quint8(std::min(v, 255));

    out << quint16(evs.size());
    for (const SimEvent &e : evs)
        out << quint8(e.type) << e.x << e.y << e.z << e.vx << e.vz << e.intensity
            << e.r << e.g << e.b << qint8(e.count) << qint8(e.carIndex) << quint8(e.highPitch ? 1 : 0);
}

void GameSim::readSnapshot(QDataStream &in, std::vector<SimEvent> &evOut)
{
    quint8 st, md; qint8 mi, lp, win; qint32 tk;
    in >> st >> md >> mi >> lp >> tk >> raceTimer >> win;
    state = State(st); mode = GameMode(md); tick = tk; winner = win;
    if (mi != mapIndex) setMap(mi);
    setLaps(lp);

    quint8 n;
    in >> n;
    cars.resize(n);
    for (Car &c : cars) {
        qint16 nt, air, wreck, calm, nlen; qint8 nc, lap, path; quint8 flags, ghost, wrecks, caused;
        in >> c.x >> c.z >> path >> c.s >> c.d >> lap >> c.lapStart >> c.bestLap >> c.y >> c.vy >> air >> c.heading >> c.speed >> c.nitro >> c.steerVis >> c.wheelSpin
           >> c.roll >> c.pitch >> c.driftAngle >> c.topSpeed >> c.finishTime >> c.r >> c.g >> c.b
           >> nt >> nc >> nlen >> flags
           >> c.health >> wreck >> ghost >> calm >> c.wreckX >> c.wreckZ >> c.wreckVx >> c.wreckVz >> c.wreckSpin
           >> wrecks >> caused;
        quint8 lf, bounties; qint16 hold;
        in >> lf >> c.lastWreckProg >> hold >> bounties >> c.barrel >> c.barrelRate >> c.spinRate >> c.jumpDanger >> c.outS;
        qint8 persona, rivalOf, roster; quint8 mood, tactic, skill, dirMode;
        quint8 brain;
        in >> persona >> mood >> tactic >> skill >> dirMode >> rivalOf >> roster >> brain;
        c.aiBrain = int(brain & 15) - 1; c.aiBrainDriving = brain & 16; c.aiBrainOff = (brain & 32) ? 1 : 0;
        c.rosterId = roster;
        c.dirMode = dirMode; c.dirRole = rivalOf >= 0 ? 1 : 0; c.dirRivalOf = rivalOf;
        c.aiPersona = persona; c.aiMood = mood; c.aiTactic = tactic; c.aiSkill = float(skill) / 255.0f;
        c.out = lf & 4; c.lifeWreck = lf & 8; c.outside = lf & 16; c.boosting = lf & 32; c.cutClosed = lf & 64;
        c.boostHold = hold; c.bounties = bounties;
        c.nitroTimer = nt; c.nitroCharges = nc; c.nitroLen = nlen;
        c.nitroOn = flags & 1; c.isDrifting = flags & 2; c.finished = flags & 4; c.ejected = flags & 8;
        c.wreck = wreck; c.ghost = ghost; c.calm = calm; c.wrecks = wrecks; c.crashesCaused = caused;
        c.path = path; c.lap = lap; c.hint = int(c.s); c.progress = progressOf(c);
        c.air = air;
    }

    updateLeadLap();

    in >> n;
    traffic.resize(n);
    for (Traffic &t : traffic) in >> t.s >> t.lane >> t.targetLane >> t.speed;

    quint16 moved;
    in >> moved;
    for (int k = 0; k < moved; ++k) {
        quint16 i; quint8 down; float x, y, z, rot;
        in >> i >> down >> x >> y >> z >> rot;
        if (i < obstacles.size()) {
            Obstacle &o = obstacles[i];
            o.knocked = down; o.x = x; o.y = y; o.z = z; o.rot = rot;
            o.vx = o.vy = o.vz = 0.0f;
        }
    }

    in >> n;
    for (int k = 0; k < n; ++k) {
        quint8 a, takes;
        in >> a >> takes;
        if (k < int(nitroPickups.size())) { nitroPickups[k].active = a; nitroPickups[k].takes = takes; }
    }
    for (std::vector<int> *jumps : {&sideJumps, &mainJumps}) {
        in >> n;
        jumps->resize(n);
        for (int &v : *jumps) { quint8 x; in >> x; v = x; }
    }

    quint16 ne;
    in >> ne;
    for (int k = 0; k < ne; ++k) {
        SimEvent e; quint8 type, hp; qint8 cnt, ci;
        in >> type >> e.x >> e.y >> e.z >> e.vx >> e.vz >> e.intensity >> e.r >> e.g >> e.b >> cnt >> ci >> hp;
        e.type = SimEvent::Type(type); e.count = cnt; e.carIndex = ci; e.highPitch = hp;
        evOut.push_back(e);
    }
}

} // namespace sim
