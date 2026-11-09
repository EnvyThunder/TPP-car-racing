// Network snapshots of the race (server -> clients). Kept apart from gamesim.cpp so
// the simulation itself stays plain C++ (QtCore is only needed here for QDataStream).
//
// Kept small for phone connections (about half the size of a plain copy): positions stay
// full floats; angles, speeds and other values that only need three digits go as half
// floats; the facts that rarely change (lap times, colours, the CPU's mind) go for each car
// every 8th snapshot, and in every snapshot for a moment after they change.
#include "gamesim.h"
#include <QDataStream>
#include <QIODevice>
#include <QHash>
#include <QtCore/qfloat16.h>
#include <cmath>

namespace sim {

namespace {
inline qfloat16 h(float v) { return qfloat16(v); }
inline float rd(QDataStream &in) { qfloat16 v; in >> v; return float(v); }
constexpr float TWO_PI = 6.2831853f;
}

// ---------------------------------------------------------------------------
//  Network snapshot
// ---------------------------------------------------------------------------
void GameSim::writeSnapshot(QDataStream &out, const std::vector<SimEvent> &evs, SnapshotMemory *mem, QDataStream *extras, int exact) const
{
    out << quint8(state) << quint8(mode) << qint8(mapIndex) << qint8(laps) << qint32(tick) << raceTimer << qint8(winner);

    const int count = mem ? mem->count++ : 0;
    if (mem && mem->slowHash.size() != cars.size()) { mem->slowHash.assign(cars.size(), 0); mem->slowAge.assign(cars.size(), 0); }
    out << quint8(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) {
        const Car &c = cars[i];
        // the rare facts, ready to compare with the last ones
        QByteArray slow;
        {
            QDataStream sd(&slow, QIODevice::WriteOnly);
            sd.setVersion(out.version());
            sd.setFloatingPointPrecision(QDataStream::SinglePrecision);
            sd << c.lapStart << c.bestLap << c.topSpeed << c.finishTime
               << quint8(std::clamp(int(c.r * 255.0f + 0.5f), 0, 255)) << quint8(std::clamp(int(c.g * 255.0f + 0.5f), 0, 255))
               << quint8(std::clamp(int(c.b * 255.0f + 0.5f), 0, 255))
               << qint8(c.nitroCharges) << qint16(c.nitroLen) << qint16(std::min(c.calm, 30000))
               << quint8(c.wrecks) << quint8(c.crashesCaused) << c.lastWreckProg << quint8(c.bounties)
               << qint8(c.aiPersona) << quint8(c.aiMood) << quint8(c.aiTactic) << quint8(std::clamp(int(c.aiSkill * 255.0f), 0, 255))
               << quint8(c.dirMode) << qint8(c.dirRole ? c.dirRivalOf : -1) << qint8(c.rosterId)
               << quint8((c.aiBrain + 1) | (c.aiBrainDriving ? 16 : 0) | (c.aiBrainOff > 0 ? 32 : 0));
        }
        bool sendSlow = true;
        if (mem) {
            const quint32 hs = quint32(qHash(slow));
            if (hs != mem->slowHash[i]) { mem->slowHash[i] = hs; mem->slowAge[i] = 0; }
            sendSlow = mem->slowAge[i]++ < 6 || (count + int(i)) % 8 == 0;
        }
        const bool wreckInfo = c.wreck > 0 || c.ejected;
        // the receiver's own car exactly: their game replays their latest keys from it, and any
        // rounding would come back as a small correction 30 times a second (a shaking screen)
        const bool full = int(i) == exact;
        auto f = [&](float v) { if (full) out << v; else out << h(v); };
        const quint8 flags = (c.nitroOn ? 1 : 0) | (c.isDrifting ? 2 : 0) | (c.finished ? 4 : 0) | (c.ejected ? 8 : 0)
                           | (sendSlow ? 16 : 0) | (wreckInfo ? 32 : 0) | (full ? 64 : 0);
        const quint8 lf = (c.out ? 4 : 0) | (c.lifeWreck ? 8 : 0) | (c.outside ? 16 : 0) | (c.boosting ? 32 : 0) | (c.cutClosed ? 64 : 0);
        out << flags << c.x << c.z << c.s << c.y;
        f(c.d);
        out << qint8(c.path) << qint8(c.lap);
        f(c.vy);
        out << qint16(c.air);
        f(full ? c.heading : std::remainder(c.heading, TWO_PI)); f(c.speed); f(c.nitro); f(c.steerVis); f(std::fmod(c.wheelSpin, 360.0f));
        f(c.roll); f(c.pitch); f(c.driftAngle);
        out << lf;
        f(c.health);
        out << qint16(c.wreck) << quint8(c.ghost) << qint16(c.nitroTimer) << qint16(std::min(c.boostHold, 30000)) << c.barrel;
        f(c.barrelRate); f(c.spinRate); f(c.jumpDanger);
        out << c.outS;
        if (wreckInfo) { out << c.wreckX << c.wreckZ; f(c.wreckVx); f(c.wreckVz); f(c.wreckSpin); }
        if (sendSlow) out.writeRawData(slow.constData(), int(slow.size()));
    }

    out << quint8(traffic.size());
    for (const Traffic &t : traffic)
        out << t.s << h(t.lane) << h(t.targetLane) << h(t.speed);

    out << quint8(nitroPickups.size());
    for (const NitroPickup &np : nitroPickups) out << quint8(np.active ? 1 : 0) << quint8(std::min(np.takes, 255));
    out << quint8(sideJumps.size());
    for (int v : sideJumps) out << quint8(std::min(v, 255));
    out << quint8(mainJumps.size());
    for (int v : mainJumps) out << quint8(std::min(v, 255));

    // the extras: in the snapshot, or apart (then the snapshot says none)
    if (extras) out << quint16(0) << quint16(0);
    QDataStream &x = extras ? *extras : out;

    // only obstacles that moved (or were put back) since this player's last snapshot
    std::vector<Obstacle> *lastSent = mem ? &mem->obstacles : nullptr;
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
    x << quint16(moved.size());
    for (quint16 i : moved) {
        const Obstacle &o = obstacles[i];
        x << i << quint8(o.knocked ? 1 : 0) << o.x << o.y << o.z << h(std::remainder(o.rot, TWO_PI));
    }
    if (lastSent) *lastSent = obstacles;

    x << quint16(evs.size());
    for (const SimEvent &e : evs)
        x << quint8(e.type) << e.x << e.y << e.z << h(e.vx) << h(e.vz) << h(e.intensity)
            << quint8(std::clamp(int(e.r * 255.0f + 0.5f), 0, 255)) << quint8(std::clamp(int(e.g * 255.0f + 0.5f), 0, 255))
            << quint8(std::clamp(int(e.b * 255.0f + 0.5f), 0, 255))
            << qint8(e.count) << qint8(e.carIndex) << quint8(e.highPitch ? 1 : 0);
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
    cars.resize(n);                      // (the rare facts stay from the last snapshot that had them)
    for (Car &c : cars) {
        qint8 path, lap; qint16 air, wreck, nt, hold; quint8 flags, lf, ghost;
        in >> flags >> c.x >> c.z >> c.s >> c.y;
        const bool full = flags & 64;                    // (my own car: exact)
        auto f = [&]() { if (full) { float v; in >> v; return v; } return rd(in); };
        c.d = f();
        in >> path >> lap;
        c.vy = f();
        in >> air;
        c.heading = f(); c.speed = f(); c.nitro = f(); c.steerVis = f(); c.wheelSpin = f();
        c.roll = f(); c.pitch = f(); c.driftAngle = f();
        in >> lf;
        c.health = f();
        in >> wreck >> ghost >> nt >> hold >> c.barrel;
        c.barrelRate = f(); c.spinRate = f(); c.jumpDanger = f();
        in >> c.outS;
        if (flags & 32) {
            in >> c.wreckX >> c.wreckZ;
            c.wreckVx = f(); c.wreckVz = f(); c.wreckSpin = f();
        }
        if (flags & 16) {
            quint8 r, g, b, wrecks, caused, bounties, mood, tactic, skill, dirMode, brain;
            qint8 nc, persona, rivalOf, roster; qint16 nlen, calm;
            in >> c.lapStart >> c.bestLap >> c.topSpeed >> c.finishTime >> r >> g >> b
               >> nc >> nlen >> calm >> wrecks >> caused >> c.lastWreckProg >> bounties
               >> persona >> mood >> tactic >> skill >> dirMode >> rivalOf >> roster >> brain;
            c.r = r / 255.0f; c.g = g / 255.0f; c.b = b / 255.0f;
            c.nitroCharges = nc; c.nitroLen = nlen; c.calm = calm; c.wrecks = wrecks; c.crashesCaused = caused;
            c.bounties = bounties;
            c.aiPersona = persona; c.aiMood = mood; c.aiTactic = tactic; c.aiSkill = float(skill) / 255.0f;
            c.dirMode = dirMode; c.dirRole = rivalOf >= 0 ? 1 : 0; c.dirRivalOf = rivalOf; c.rosterId = roster;
            c.aiBrain = int(brain & 15) - 1; c.aiBrainDriving = brain & 16; c.aiBrainOff = (brain & 32) ? 1 : 0;
        }
        c.out = lf & 4; c.lifeWreck = lf & 8; c.outside = lf & 16; c.boosting = lf & 32; c.cutClosed = lf & 64;
        c.boostHold = hold;
        c.nitroTimer = nt;
        c.nitroOn = flags & 1; c.isDrifting = flags & 2; c.finished = flags & 4; c.ejected = flags & 8;
        c.wreck = wreck; c.ghost = ghost;
        c.path = path; c.lap = lap; c.hint = int(c.s); c.progress = progressOf(c);
        c.air = air;
    }

    updateLeadLap();

    in >> n;
    traffic.resize(n);
    for (Traffic &t : traffic) { in >> t.s; t.lane = rd(in); t.targetLane = rd(in); t.speed = rd(in); }

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
    readExtras(in, evOut);
}

void GameSim::readExtras(QDataStream &in, std::vector<SimEvent> &evOut)
{
    quint16 moved;
    in >> moved;
    for (int k = 0; k < moved; ++k) {
        quint16 i; quint8 down; float x, y, z;
        in >> i >> down >> x >> y >> z;
        const float rot = rd(in);
        if (i < obstacles.size()) {
            Obstacle &o = obstacles[i];
            o.knocked = down; o.x = x; o.y = y; o.z = z; o.rot = rot;
            o.vx = o.vy = o.vz = 0.0f;
        }
    }

    quint16 ne;
    in >> ne;
    for (int k = 0; k < ne; ++k) {
        SimEvent e; quint8 type, hp, r, g, b; qint8 cnt, ci;
        in >> type >> e.x >> e.y >> e.z;
        e.vx = rd(in); e.vz = rd(in); e.intensity = rd(in);
        in >> r >> g >> b >> cnt >> ci >> hp;
        e.r = r / 255.0f; e.g = g / 255.0f; e.b = b / 255.0f;
        e.type = SimEvent::Type(type); e.count = cnt; e.carIndex = ci; e.highPitch = hp;
        evOut.push_back(e);
    }
}

} // namespace sim
