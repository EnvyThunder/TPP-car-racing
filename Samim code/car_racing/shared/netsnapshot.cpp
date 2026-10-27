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
    out << quint8(state) << quint8(mode) << qint8(mapIndex) << qint32(tick) << raceTimer << qint8(winner);

    out << quint8(cars.size());
    for (const Car &c : cars) {
        quint8 flags = (c.nitroOn ? 1 : 0) | (c.isDrifting ? 2 : 0) | (c.finished ? 4 : 0);
        out << c.x << c.z << c.heading << c.speed << c.nitro << c.steerVis << c.wheelSpin
            << c.roll << c.pitch << c.driftAngle << c.topSpeed << c.finishTime << c.r << c.g << c.b
            << qint16(c.nitroTimer) << qint8(c.nitroCharges) << flags;
    }

    out << quint8(traffic.size());
    for (const Traffic &t : traffic)
        out << t.z << t.lane << t.targetLane << t.speed << quint8(t.parked ? 1 : 0);

    // only obstacles that moved since the last snapshot (TCP delivers every snapshot in order)
    std::vector<quint16> moved;
    for (size_t i = 0; i < obstacles.size(); ++i) {
        const Obstacle &o = obstacles[i];
        if (!o.knocked) continue;
        if (lastSent && i < lastSent->size()) {
            const Obstacle &p = (*lastSent)[i];
            if (p.knocked && p.x == o.x && p.y == o.y && p.z == o.z && p.rot == o.rot) continue;
        }
        moved.push_back(quint16(i));
    }
    out << quint16(moved.size());
    for (quint16 i : moved) {
        const Obstacle &o = obstacles[i];
        out << i << o.x << o.y << o.z << o.rot;
    }
    if (lastSent) *lastSent = obstacles;

    out << quint8(nitroPickups.size());
    for (const NitroPickup &np : nitroPickups) out << quint8(np.active ? 1 : 0);

    out << quint16(evs.size());
    for (const SimEvent &e : evs)
        out << quint8(e.type) << e.x << e.y << e.z << e.vx << e.vz << e.intensity
            << e.r << e.g << e.b << qint8(e.count) << qint8(e.carIndex) << quint8(e.highPitch ? 1 : 0);
}

void GameSim::readSnapshot(QDataStream &in, std::vector<SimEvent> &evOut)
{
    quint8 st, md; qint8 mi, win; qint32 tk;
    in >> st >> md >> mi >> tk >> raceTimer >> win;
    state = State(st); mode = GameMode(md); tick = tk; winner = win;
    if (mi != mapIndex) setMap(mi);

    quint8 n;
    in >> n;
    cars.resize(n);
    for (Car &c : cars) {
        qint16 nt; qint8 nc; quint8 flags;
        in >> c.x >> c.z >> c.heading >> c.speed >> c.nitro >> c.steerVis >> c.wheelSpin
           >> c.roll >> c.pitch >> c.driftAngle >> c.topSpeed >> c.finishTime >> c.r >> c.g >> c.b
           >> nt >> nc >> flags;
        c.nitroTimer = nt; c.nitroCharges = nc;
        c.nitroOn = flags & 1; c.isDrifting = flags & 2; c.finished = flags & 4;
    }

    in >> n;
    traffic.resize(n);
    for (Traffic &t : traffic) {
        quint8 parked;
        in >> t.z >> t.lane >> t.targetLane >> t.speed >> parked;
        t.parked = parked;
    }

    quint16 moved;
    in >> moved;
    for (int k = 0; k < moved; ++k) {
        quint16 i; float x, y, z, rot;
        in >> i >> x >> y >> z >> rot;
        if (i < obstacles.size()) {
            Obstacle &o = obstacles[i];
            o.knocked = true; o.x = x; o.y = y; o.z = z; o.rot = rot;
            o.vx = o.vy = o.vz = 0.0f;
        }
    }

    in >> n;
    for (int k = 0; k < n; ++k) {
        quint8 a;
        in >> a;
        if (k < int(nitroPickups.size())) nitroPickups[k].active = a;
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
