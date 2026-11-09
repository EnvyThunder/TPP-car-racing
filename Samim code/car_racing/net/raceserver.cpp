#include "raceserver.h"
#include <QtEndian>
#ifdef HAVE_RTC
#include <rtc/rtc.hpp>
#include <memory>
// A browser's WebRTC connection. libdatachannel calls back on its own threads: everything it
// says is passed to the server's thread by the player's id (the player may be gone by then).
struct RtcPeer {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    std::atomic<bool> open{false};
};
#else
struct RtcPeer {};
#endif

#include <QTcpSocket>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

using namespace net;

RaceServer::RaceServer(QObject *parent)
    : QObject(parent), tcp(this), ws(this), udp(this), fast(this), timer(this)   // children: they move threads with it
{
    connect(&fast, &QUdpSocket::readyRead, this, &RaceServer::onFast);
    upClock.start();
    connect(&tcp, &QTcpServer::newConnection, this, &RaceServer::onNewConnection);
    connect(&ws, &QTcpServer::newConnection, this, &RaceServer::onNewConnection);
    connect(&udp, &QUdpSocket::readyRead, this, &RaceServer::onDiscovery);
    connect(&timer, &QTimer::timeout, this, &RaceServer::onTick);
}

RaceServer::~RaceServer()
{
    // Close the sockets first, with their signals cut: a socket being destroyed reports
    // "disconnected", which must not reach this half-destroyed server.
    timer.stop();
    tcp.close();
    ws.close();
    for (net::Link *s : bySocket.keys()) {
        s->disconnect(this);
        s->abort();
        delete s;
    }
    bySocket.clear();
    for (Room *room : rooms) {
        qDeleteAll(room->players);
        delete room;
    }
}

bool RaceServer::start(quint16 port)
{
    bool ok = false;
    for (quint16 p = port; p < port + 10 && !ok; ++p)
        ok = tcp.listen(QHostAddress::AnyIPv4, p);
    if (!ok) {
        emit log("Could not open a TCP port for the game server");
        return false;
    }
    // Several servers on one PC can all answer discovery requests for their own rooms
    if (!udp.bind(QHostAddress::AnyIPv4, DISCOVERY_PORT, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
        emit log("Room discovery (UDP) unavailable - players can still join with CODE@address");
    if (!fast.bind(QHostAddress::AnyIPv4, quint16(tcp.serverPort() + 2)) && !fast.bind(QHostAddress::AnyIPv4, 0))
        emit log("Fast lane (UDP) unavailable - everything goes over the connection");
    else
        emit log(QString("Fast lane on UDP port %1").arg(fast.localPort()));
    clock.start();
    timer.setTimerType(Qt::PreciseTimer);
    timer.start(5);
    emit log(QString("Game server listening on TCP port %1").arg(tcp.serverPort()));
    return true;
}

// ---------------------------------------------------------------------------
//  Connections and messages
// ---------------------------------------------------------------------------
bool RaceServer::startWs(quint16 wsPort, bool anyAddress)
{
    if (!ws.listen(anyAddress ? QHostAddress::AnyIPv4 : QHostAddress::LocalHost, wsPort)) {
        emit log(QString("Could not open WebSocket port %1").arg(wsPort));
        return false;
    }
    if (!timer.isActive()) { clock.start(); timer.setTimerType(Qt::PreciseTimer); timer.start(5); }
    emit log(QString("WebSocket connections on %1:%2").arg(anyAddress ? "all addresses" : "127.0.0.1").arg(ws.serverPort()));
    return true;
}

quint16 RaceServer::wsPort() const { return ws.serverPort(); }

void RaceServer::onNewConnection()
{
    while (QTcpSocket *sock = tcp.nextPendingConnection()) accept(sock, false);
    while (QTcpSocket *sock = ws.nextPendingConnection()) accept(sock, true);
}

// a new connection: a TCP one is ready at once; a WebSocket after its handshake, when its
// address is known (behind Caddy: the player's, from X-Forwarded-For) - one address may
// hold MAX_PER_ADDRESS connections
void RaceServer::accept(QTcpSocket *sock, bool webSocket)
{
    auto *link = new net::Link(this);
    Player *p = new Player;
    p->id = ++nextId;
    p->sock = link;
    bySocket.insert(link, p);
    connect(link, &net::Link::readyRead, this, &RaceServer::onReadyRead);
    connect(link, &net::Link::closed, this, &RaceServer::onDisconnected);
    if (!webSocket) {
        link->acceptTcp(sock);
        if (!link->isLoopback()) ++remoteSeen;
        return;
    }
    connect(link, &net::Link::opened, this, [this, link]() {
        const QString addr = link->clientAddress();
        if (perAddress.value(addr) >= MAX_PER_ADDRESS) { link->close(); return; }
        perAddress[addr] += 1;
        link->setProperty("counted", addr);
    });
    link->acceptWs(sock);
}

void RaceServer::onReadyRead()
{
    auto *link = qobject_cast<net::Link *>(sender());
    Player *p = bySocket.value(link);
    if (!p) return;
    p->reader.feed(link->take());
    MsgType type;
    QByteArray payload;
    while (p->sock && p->reader.next(type, payload)) handle(p, type, payload);
    if (p->reader.broken()) fail(p, "Bad data from client");
}

void RaceServer::onDisconnected()
{
    auto *link = qobject_cast<net::Link *>(sender());
    if (!link) return;
    const QString counted = link->property("counted").toString();
    if (!counted.isEmpty() && --perAddress[counted] <= 0) perAddress.remove(counted);
    Player *p = bySocket.take(link);
    link->deleteLater();
    if (!p) return;
    p->sock = nullptr;
    closeRtc(p);
    removePlayer(p);
}

void RaceServer::handle(Player *p, MsgType type, const QByteArray &payload)
{
    QDataStream in(payload);
    prepare(in);
    Room *room = roomOf.value(p);
    switch (type) {
    case C_JOIN:
        if (!room) join(p, in);
        break;
    case C_INPUT: {                                      // the newest input and copies of the ones before it
        quint32 seq; quint8 want, count;
        in >> seq >> want >> count;
        if (!room || !room->racing || count == 0 || count > 32) break;
        p->want = std::clamp(int(want), 1, 12);
        for (int k = 0; k < count; ++k) {
            quint8 bits;
            in >> bits;
            if (in.status() != QDataStream::Ok) break;
            const quint32 s = seq - quint32(count - 1 - k);
            if (s > seq || s <= p->lastQueued) continue;  // (had it already)
            p->inputs.emplace_back(s, unpackInput(bits));
            p->lastQueued = s;
        }
        break;
    }
    case C_START:
        if (room && room->host == p && !room->racing) startRace(room);
        break;
    case C_SET_MAP: {
        qint8 m, l;
        in >> m >> l;
        if (room && room->host == p && !room->racing) {
            room->map = std::clamp(int(m), 0, sim::NUM_MAPS - 1);
            room->laps = std::clamp(int(l), 1, 5);
            sendLobby(room);
        }
        break;
    }
    case C_STORY: {
        quint8 skip;
        in >> skip;
        if (!room || !room->racing || !room->storyHold) break;
        if (skip) { endStory(room); break; }             // one person skips: it ends for everyone
        if (!room->storyWatched.contains(p)) room->storyWatched.append(p);
        bool all = true;
        for (Player *q : room->players)
            if (q->sock && !q->isCpu() && !room->storyWatched.contains(q)) all = false;
        if (all) endStory(room);
        break;
    }
    case C_PING:
        send(p, S_PONG, payload);
        break;
    case C_RTC_OFFER: {
        QString sdp;
        in >> sdp;
        if (room && sdp.size() < 20000) rtcOffer(p, sdp);
        break;
    }
    case C_ADD_BOT:
        if (room && room->host == p && !room->racing) addCpu(room);
        break;
    case C_REMOVE_BOT:
        if (room && room->host == p && !room->racing) removeCpu(room);
        break;
    case C_SET_NAME: {
        QString name;
        in >> name;
        if (room && !room->racing && !cleanName(name).isEmpty()) {
            const QString old = p->name;
            p->name = uniqueName(room, cleanName(name), p);
            emit log(QString("Room %1: %2 is now %3").arg(room->code, old, p->name));
            sendLobby(room);
        }
        break;
    }
    default:
        break;
    }
}

void RaceServer::join(Player *p, QDataStream &in)
{
    quint16 version; QString name, code, token; float r, g, b; qint8 map, laps;
    in >> version >> name >> r >> g >> b >> code >> map >> laps;
    if (version != PROTOCOL_VERSION) { fail(p, "Different game version - update the game"); return; }
    in >> token;                                     // (empty: a guest; else checked with the accounts API)
    if (token.size() <= 100) p->token = token;
    p->name = cleanName(name);                      // may be empty: a CAR name is given below
    p->r = r; p->g = g; p->b = b;

    Room *room = nullptr;
    code = code.trimmed().toUpper();
    if (!code.isEmpty()) {
        // Same name or same car colour as someone already in the room? Make it unique,
        // so everyone can tell the cars apart on track and on the minimap.
        if (Room *r0 = rooms.value(code)) {
            p->name = uniqueName(r0, p->name, p);
            static const float PALETTE[8][3] = {
                {0.92f, 0.14f, 0.16f}, {0.15f, 0.42f, 0.98f}, {1.00f, 0.75f, 0.10f}, {0.20f, 0.85f, 0.35f},
                {0.85f, 0.30f, 0.95f}, {0.10f, 0.85f, 0.90f}, {0.95f, 0.50f, 0.15f}, {0.92f, 0.92f, 0.95f}};
            auto clash = [&](float cr, float cg, float cb) {
                for (Player *q : r0->players)
                    if (std::fabs(q->r - cr) + std::fabs(q->g - cg) + std::fabs(q->b - cb) < 0.35f) return true;
                return false;
            };
            for (int k = 0; k < 8 && clash(p->r, p->g, p->b); ++k) {
                p->r = PALETTE[k][0]; p->g = PALETTE[k][1]; p->b = PALETTE[k][2];
            }
        }
    }
    if (code.isEmpty()) {                                  // create a new room
        if (maxRooms > 0 && rooms.size() >= maxRooms) { fail(p, "The server is full - try again in a few minutes"); return; }
        if (p->name.isEmpty()) p->name = "CAR1";
        room = new Room;
        room->code = newCode();
        room->map = std::clamp(int(map), 0, sim::NUM_MAPS - 1);
        room->laps = std::clamp(int(laps), 1, 5);
        room->host = p;
        rooms.insert(room->code, room);
        emit log(QString("Room %1 created by %2").arg(room->code, p->name));
    } else {
        room = rooms.value(code);
        if (!room)                                   { fail(p, "No room with code " + code); return; }
        if (room->racing)                            { fail(p, "Race in progress - join after it ends"); return; }
        if (room->players.size() >= sim::MAX_RACERS) {
            // a CPU racer gives its seat to a person
            bool hasCpu = false;
            for (Player *q : room->players) hasCpu = hasCpu || q->isCpu();
            if (!hasCpu) { fail(p, QString("Room is full (%1/%1)").arg(sim::MAX_RACERS)); return; }
            removeCpu(room);
        }
        emit log(QString("%1 joined room %2").arg(p->name, room->code));
    }
    room->players.append(p);
    roomOf.insert(p, room);

    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    p->fastToken = QRandomGenerator::global()->generate64() | 1;
    ds << qint8(room->players.size() - 1) << room->code << bool(room->host == p)
       << quint16(fast.state() == QAbstractSocket::BoundState ? fast.localPort() : 0) << quint64(p->fastToken);
    send(p, S_WELCOME, out);
    sendLobby(room);
    checkAccount(p);
}

void RaceServer::removePlayer(Player *p)
{
    Room *room = roomOf.value(p);
    if (!room) { delete p; return; }
    emit log(QString("%1 left room %2").arg(p->name, room->code));
    if (!room->racing) {                                   // in a race the car stays on track
        room->players.removeAll(p);
        roomOf.remove(p);
        delete p;
    }
    bool anyone = false;
    for (Player *q : room->players) anyone = anyone || q->sock;
    if (!anyone) {                                         // empty room: close it
        emit log(QString("Room %1 closed").arg(room->code));
        rooms.remove(room->code);
        for (Player *q : room->players) { roomOf.remove(q); delete q; }
        delete room;
        return;
    }
    if (!room->host || !room->host->sock || !room->players.contains(room->host)) {
        for (Player *q : room->players) if (q->sock) { room->host = q; break; }
    }
    if (!room->racing) sendLobby(room);
}

QString RaceServer::uniqueName(Room *room, const QString &wanted, const Player *self) const
{
    auto taken = [&](const QString &n) {
        for (const Player *q : room->players) if (q != self && q->name == n) return true;
        return false;
    };
    // default names CAR1, CAR2, ...: also when someone types one of those themselves,
    // so two players never end up as CAR2 and CAR22
    bool isDefault = wanted.isEmpty();
    if (wanted.startsWith("CAR") && wanted.size() > 3) {
        isDefault = true;
        for (int i = 3; i < wanted.size(); ++i) isDefault = isDefault && wanted[i].isDigit();
    }
    if (isDefault) {
        if (!wanted.isEmpty() && !taken(wanted)) return wanted;
        for (int k = 1; ; ++k)
            if (!taken(QString("CAR%1").arg(k))) return QString("CAR%1").arg(k);
    }
    if (!taken(wanted)) return wanted;
    for (int k = 2; ; ++k) {
        QString n = wanted.left(MAX_NAME - 2) + QString::number(k);
        if (!taken(n)) return n;
    }
}

void RaceServer::sendLobby(Room *room)
{
    for (int me = 0; me < room->players.size(); ++me) {
        Player *to = room->players[me];
        if (!to->sock) continue;
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        prepare(ds);
        ds << qint8(room->map) << qint8(room->laps) << qint8(me) << bool(room->host == to) << quint8(room->players.size());
        for (Player *q : room->players)
            ds << q->name << q->r << q->g << q->b << bool(room->host == q) << qint8(q->cpuLevel);
        send(to, S_LOBBY, out);
    }
}

// ---------------------------------------------------------------------------
//  CPU racers: seats filled by the server's own CPU drivers
// ---------------------------------------------------------------------------
void RaceServer::pickFreeColour(Room *room, Player *p)
{
    static const float PALETTE[8][3] = {
        {0.92f, 0.14f, 0.16f}, {0.15f, 0.42f, 0.98f}, {1.00f, 0.75f, 0.10f}, {0.20f, 0.85f, 0.35f},
        {0.85f, 0.30f, 0.95f}, {0.10f, 0.85f, 0.90f}, {0.95f, 0.50f, 0.15f}, {0.92f, 0.92f, 0.95f}};
    for (const auto &c : PALETTE) {
        bool used = false;
        for (Player *q : room->players)
            used = used || (q != p && std::fabs(q->r - c[0]) + std::fabs(q->g - c[1]) + std::fabs(q->b - c[2]) < 0.35f);
        if (!used) { p->r = c[0]; p->g = c[1]; p->b = c[2]; return; }
    }
}

void RaceServer::addCpu(Room *room)
{
    if (room->players.size() >= sim::MAX_RACERS) return;
    Player *p = new Player;
    p->cpuLevel = 1;                                              // (a CPU: no level - its mind adapts)
    // a named driver from the roster that isn't in the room yet (CPU1, CPU2, ... once they run out)
    const int start = int(QRandomGenerator::global()->bounded(sim::ROSTER_SIZE));
    for (int k = 0; k < sim::ROSTER_SIZE && p->name.isEmpty(); ++k) {
        const QString name = sim::rosterDriver((start + k) % sim::ROSTER_SIZE).name;
        bool taken = false;
        for (Player *q : room->players) taken = taken || q->name == name;
        if (!taken) p->name = name;
    }
    for (int n = 1; p->name.isEmpty(); ++n) {
        QString name = QString("CPU%1").arg(n);
        bool taken = false;
        for (Player *q : room->players) taken = taken || q->name == name;
        if (!taken) p->name = name;
    }
    room->players.append(p);
    pickFreeColour(room, p);
    if (const int r = sim::rosterByName(p->name.toUtf8().constData()); r >= 0) {   // the driver's own colours
        p->r = sim::rosterDriver(r).car[0]; p->g = sim::rosterDriver(r).car[1]; p->b = sim::rosterDriver(r).car[2];
    }
    roomOf.insert(p, room);
    emit log(QString("Room %1: %2 added").arg(room->code, p->name));
    sendLobby(room);
}

void RaceServer::removeCpu(Room *room)
{
    for (int i = room->players.size() - 1; i >= 0; --i) {
        Player *p = room->players[i];
        if (!p->isCpu()) continue;
        room->players.removeAt(i);
        roomOf.remove(p);
        emit log(QString("Room %1: %2 removed").arg(room->code, p->name));
        delete p;
        sendLobby(room);
        return;
    }
}

// ---------------------------------------------------------------------------
//  Racing
// ---------------------------------------------------------------------------
void RaceServer::startRace(Room *room)
{
    const int n = room->players.size();
    room->sim.setMap(room->map);
    room->sim.setLaps(room->laps);
    // the CPUs: new minds every race, their skill spread around the people in the room (what
    // the server has measured of them; someone new counts as an average driver)
    float skill = 0.0f, challenge = 0.0f;
    int people = 0;
    for (Player *p : room->players) {
        if (p->isCpu()) continue;
        const sim::PlayerProfile &prof = profiles[p->name];
        skill += sim::NEW_PLAYER_SKILL * (1.0f - prof.sure) + prof.traits.skill * prof.sure;
        challenge += prof.challenge;
        ++people;
    }
    challenge = people ? challenge / float(people) : 0.0f;   // (the room's people together)
    room->sim.setCpuChallenge(challenge);
    room->sim.setCpuSkill(sim::cpuCentreFor(people ? skill / float(people) : sim::NEW_PLAYER_SKILL, 1.0f, challenge));   // (skill: already blended)
    room->sim.setAiSeed(QRandomGenerator::global()->generate());
    room->sim.clearPlayerHints();                        // what the race director knows of each person
    for (int i = 0; i < room->players.size(); ++i) {
        Player *p = room->players[i];
        if (p->isCpu()) continue;
        const sim::PlayerProfile &prof = profiles[p->name];
        sim::PlayerHints h;
        if (prof.races > 0) {
            h.known = true; h.sure = prof.sure; h.skill = prof.traits.skill;
            h.aggression = prof.traits.aggression; h.risk = prof.traits.risk;
            const sim::MapHabits &mh = prof.habits[size_t(std::clamp(room->map, 0, sim::PM_MAPS - 1))];
            if (mh.forkOffered >= 3) h.shortcut = float(mh.forkTaken) / float(mh.forkOffered);
            int left = 0, right = 0;
            for (const sim::MapHabits &m : prof.habits) { left += m.passLeft; right += m.passRight; }
            if (left + right >= 6) h.passLeft = float(left) / float(left + right);
            for (int k = 0; k < sim::ROSTER_SIZE; ++k) h.attitude[size_t(k)] = int8_t(prof.attitude(k));
            h.nemesis = prof.nemesis();
        }
        room->sim.setPlayerHints(i, h);
    }
    room->sim.startRace(sim::GameSim::ModeOnline, n);
    auto &cars = room->sim.getCars();
    for (int i = 0; i < n; ++i) {
        Player *p = room->players[i];
        cars[i].r = p->r; cars[i].g = p->g; cars[i].b = p->b;
        cars[i].aiLevel = p->cpuLevel;
        if (p->isCpu()) {                                // a named driver: its personality
            cars[i].rosterId = sim::rosterByName(p->name.toUtf8().constData());
            if (cars[i].rosterId >= 0) cars[i].aiPersona = sim::rosterDriver(cars[i].rosterId).persona;
        }
        p->inputs.clear();
        p->budget = 0.0f;
        p->lastSeq = p->lastQueued = 0;
        p->want = 2;
        p->events.clear();
        p->mem = sim::GameSim::SnapshotMemory();
        p->skipped = 0;
    }
    room->racing = true;
    room->storyHold = true;                              // first the story scene (endStory)
    room->storyTicks = 0;
    room->storyWatched.clear();
    room->overTicks = 0;
    room->steps = 0;
    emit log(QString("Room %1: race started on %2 (%3 laps) with %4 players")
                 .arg(room->code, sim::MAPS[room->map].name).arg(room->laps).arg(n));

    for (int i = 0; i < n; ++i) {
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        prepare(ds);
        ds << qint8(room->map) << qint8(room->laps) << qint8(i) << quint8(n);
        for (Player *q : room->players) ds << q->name;
        ds << quint32(room->sim.worldChecksum());
        send(room->players[i], S_RACE_START, out);
    }
}

void RaceServer::endStory(Room *room)
{
    if (!room->storyHold) return;
    room->storyHold = false;
    for (Player *p : room->players) if (p->sock) send(p, S_STORY_END);
}

void RaceServer::endRace(Room *room)
{
    room->racing = false;
    const auto &cars = room->sim.getCars();
    for (int i = 0; i < room->players.size() && i < int(cars.size()); ++i) {   // each person's race into their profile
        Player *p = room->players[i];
        if (p->isCpu()) continue;
        sim::RaceSummary s;
        s.map = room->map; s.place = room->sim.rankOf(i); s.cars = int(cars.size());
        s.wrecks = cars[size_t(i)].wrecks; s.rams = cars[size_t(i)].crashesCaused; s.bestLap = cars[size_t(i)].bestLap;
        s.km = room->sim.raceObs(i).metres / 1000.0f; s.eliminated = cars[size_t(i)].out;
        if (!p->account.isEmpty()) {                     // logged in: the result goes to their account
            const sim::Car &c = cars[size_t(i)];
            QJsonObject r;
            r["map"] = room->map;
            r["place"] = std::clamp(s.place, 1, 6);
            r["cars"] = std::clamp(s.cars, 1, 6);
            const float best = room->sim.sprint() ? (c.finished ? c.finishTime : 0.0f) : c.bestLap;
            r["bestLap"] = double(std::clamp(best, 0.0f, 600.0f));
            r["wrecks"] = std::min(c.wrecks, 999);
            r["rams"] = std::min(c.crashesCaused, 999);
            r["km"] = double(std::clamp(s.km, 0.0f, 200.0f));
            r["skill"] = double(std::clamp(sim::raceTraits(room->sim.raceObs(i)).skill, 0.0f, 1.0f));
            uploadResult(p, r);
        }
        sim::PlayerProfile &prof = profiles[p->name];
        prof.name = p->name.toStdString();
        prof.addRace(room->sim.raceObs(i), s, room->sim.rivalRaces(i));
        emit log(QString("Profile %1: skill %2 aggression %3 risk %4 (%5 races)").arg(p->name)
                     .arg(double(prof.traits.skill), 0, 'f', 2).arg(double(prof.traits.aggression), 0, 'f', 2)
                     .arg(double(prof.traits.risk), 0, 'f', 2).arg(prof.races));
    }
    for (int i = room->players.size() - 1; i >= 0; --i) {  // drop players who left during the race
        Player *p = room->players[i];
        if (!p->sock && !p->isCpu()) { roomOf.remove(p); room->players.removeAt(i); delete p; }
    }
    for (Player *p : room->players) send(p, S_RACE_OVER);
    sendLobby(room);
}

void RaceServer::onTick()
{
    const float DT = 1.0f / 60.0f;
    // in nanoseconds: whole milliseconds would drop the fraction on every call (it runs every
    // ~5 ms), and the race would run up to 10% slower than real time - slower than the
    // players' games send their inputs
    const qint64 gapNs = clock.nsecsElapsed();
    clock.restart();
    const qint64 gapMs = gapNs / 1000000;
    accumulator += gapNs * 1e-9;
    if (accumulator > 0.25) accumulator = 0.25;              // don't spiral after a stall
    while (accumulator >= DT) {
        accumulator -= DT;
        const auto roomList = rooms.values();
        for (Room *room : roomList) {
            if (!room->racing) continue;
            if (room->storyHold) {                         // the story scene: the grid waits
                if (++room->storyTicks > STORY_MAX) endStory(room);
                for (Player *p : room->players) { p->inputs.clear(); p->budget = 0.0f; }
                continue;
            }
            if (room->sim.getState() == sim::GameSim::Racing && gapMs > statMaxGapMs) statMaxGapMs = int(gapMs);
            const int n = room->players.size();
            sim::PlayerInput inputs[sim::MAX_RACERS], extra[sim::MAX_RACERS];
            int runs[sim::MAX_RACERS];
            for (int i = 0; i < n; ++i) {
                Player *p = room->players[i];
                runs[i] = 1;
                if (p->isCpu()) {                              // CPU racer: the server drives it
                    inputs[i] = room->sim.computeAIInput(room->sim.getCars()[i], i, true);
                    continue;
                }
                if (!p->sock) { inputs[i] = sim::PlayerInput{}; continue; }
                // Every input a player sends moves their car exactly once, as it did in their own
                // game's prediction - so their car never has to be pulled back. Input not here
                // yet (a slow frame on their side): the car waits this tick. Inputs piling up:
                // two in one tick until it has caught up. The budget (a little over one input
                // per tick, for clocks that run slightly differently) stops a game going faster.
                // After a stall on the way here (TCP resends a lost packet: often 200-500 ms) the
                // waiting inputs arrive together. They must all be used: each one dropped pulls
                // the player's car back about a metre on their screen. So the budget and the queue
                // hold a whole second of them.
                static const bool deepQueue = qEnvironmentVariableIsSet("CAR_RACING_NET_DEEPQUEUE");   // (test: keep the inputs the game asks for waiting)
                p->budget = std::min(p->budget + 1.02f, 60.0f);
                runs[i] = 0;
                while (runs[i] < 2 && !p->inputs.empty() && p->budget >= 1.0f &&
                       (runs[i] == 0 || p->inputs.size() > (deepQueue ? size_t(p->want) + 2 : size_t(2)))) {   // (as before: your car on the server stays close to your screen)
                    (runs[i] == 0 ? inputs[i] : extra[i]) = p->inputs.front().second;
                    p->lastSeq = p->inputs.front().first;
                    p->inputs.pop_front();
                    p->budget -= 1.0f;
                    ++runs[i];
                }
                if (runs[i] == 0 && room->sim.getState() == sim::GameSim::Racing) ++statStarved;
                while (p->inputs.size() > 60) {                // a whole second behind: drop the oldest
                    p->lastSeq = p->inputs.front().first;
                    p->inputs.pop_front();
                    ++statDropped;
                }
            }
            room->sim.step(inputs, n, runs, extra);
            const auto &evs = room->sim.getEvents();
            for (Player *p : room->players)
                if (p->sock && p->events.size() < 300) p->events.insert(p->events.end(), evs.begin(), evs.end());

            if (++room->steps % SNAPSHOT_EVERY == 0) {
                static const bool rounded = qEnvironmentVariableIsSet("CAR_RACING_NET_ROUNDED");   // (tests: the old rounded own car)
                for (Player *p : room->players) {
                    if (!p->sock) continue;
                    // their connection has backed up (a stall on the way): hold the next snapshots
                    // rather than pile up ones that would all arrive late together. Each player's
                    // snapshot carries their own effects and obstacle changes since their last one.
                    // The fast lane while their inputs come by it: the snapshot as one datagram,
                    // what must not be lost (obstacle changes, effects) on the connection beside it.
                    const bool viaFast = p->fastInputAt >= 0 && upClock.elapsed() - p->fastInputAt < 1000
#ifdef HAVE_RTC
                                         && (!p->rtc || p->rtc->open)
#endif
                                         ;
                    if (!viaFast && p->sock->backlog() > SNAPSHOT_BACKLOG && p->skipped < 15) { ++p->skipped; ++statSkipped; continue; }
                    p->skipped = 0;
                    QByteArray out, extras;
                    QDataStream ds(&out, QIODevice::WriteOnly), xs(&extras, QIODevice::WriteOnly);
                    prepare(ds);
                    prepare(xs);
                    ds << quint32(p->lastSeq) << quint8(std::min<size_t>(p->inputs.size(), 255));
                    room->sim.writeSnapshot(ds, p->events, &p->mem, viaFast ? &xs : nullptr, rounded ? -1 : int(room->players.indexOf(p)));
                    p->events.clear();
                    if (viaFast) {
                        QByteArray dg(9, 0);
                        dg[0] = char(F_SNAPSHOT);
                        qToBigEndian<quint64>(p->fastToken, dg.data() + 1);
                        sendFastTo(p, dg + out);
                        if (extras.size() > 4) send(p, S_EXTRAS, extras);   // (4: nothing moved, no effects)
                    } else {
                        send(p, S_SNAPSHOT, out);
                    }
                }
            }
            // results stay on screen for a few seconds, then everyone is back in the lobby
            if (room->sim.getState() == sim::GameSim::Finished && ++room->overTicks > 60 * 7) endRace(room);
        }
    }
}

// ---------------------------------------------------------------------------
//  The fast lane (net/protocol.h): datagrams carry the sender's token; the address they come
//  from is where that player's snapshots go (a phone's address can change mid-race)
// ---------------------------------------------------------------------------
void RaceServer::onFast()
{
    while (fast.hasPendingDatagrams()) {
        const QNetworkDatagram d = fast.receiveDatagram(2048);
        const QByteArray data = d.data();
        if (data.size() < 9) continue;
        const quint64 token = qFromBigEndian<quint64>(data.constData() + 1);
        Player *p = nullptr;
        for (auto it = roomOf.constBegin(); it != roomOf.constEnd() && !p; ++it)
            if (it.key()->fastToken == token && it.key()->sock) p = it.key();
        if (!p || p->rtc) continue;
        p->fastAddr = d.senderAddress();
        p->fastPort = quint16(d.senderPort());
        fastPacket(p, data);
    }
}

// a fast-lane datagram from a player (by UDP, or a browser's data channel)
void RaceServer::fastPacket(Player *p, const QByteArray &data)
{
    const quint8 type = quint8(data[0]);
    if (type == F_HELLO) {
        QByteArray ok(data.left(9));
        ok[0] = char(F_HELLO_OK);
        sendFastTo(p, ok);
    } else if (type == F_INPUT) {
        p->fastInputAt = upClock.elapsed();
        handle(p, C_INPUT, data.mid(9));
    }
}

void RaceServer::sendFastTo(Player *p, const QByteArray &dg)
{
#ifdef HAVE_RTC
    if (p->rtc) {
        if (p->rtc->open && p->rtc->dc) {
            try { p->rtc->dc->send(reinterpret_cast<const std::byte *>(dg.constData()), size_t(dg.size())); } catch (...) {}
        }
        return;
    }
#endif
    if (!p->fastAddr.isNull()) fast.writeDatagram(dg, p->fastAddr, p->fastPort);
}

// A browser's offer: answer it, with this machine's public address in place of a private one
void RaceServer::rtcOffer(Player *p, const QString &sdp)
{
#ifdef HAVE_RTC
    if (!rtcPort) return;
    closeRtc(p);
    p->rtc = new RtcPeer;
    rtc::Configuration cfg;
    cfg.portRangeBegin = cfg.portRangeEnd = rtcPort;
    cfg.enableIceUdpMux = true;                          // (every browser on the one port)
    try {
        auto pc = std::make_shared<rtc::PeerConnection>(cfg);
        p->rtc->pc = pc;
        const quint64 id = p->id;
        std::weak_ptr<rtc::PeerConnection> weak = pc;
        const QString pub = publicIp;
        pc->onGatheringStateChange([this, id, weak, pub](rtc::PeerConnection::GatheringState st) {
            if (st != rtc::PeerConnection::GatheringState::Complete) return;
            auto pcs = weak.lock();
            if (!pcs) return;
            const auto desc = pcs->localDescription();
            if (!desc) return;
            QStringList lines = QString::fromStdString(std::string(*desc)).split("\r\n");
            for (QString &l : lines) {
                if (!l.startsWith("a=candidate:") || pub.isEmpty()) continue;
                QStringList f = l.split(' ');
                if (f.size() > 5) {
                    const QString ip = f[4];
                    if (ip.startsWith("10.") || ip.startsWith("192.168.") || ip.startsWith("172.")) { f[4] = pub; l = f.join(' '); }
                }
            }
            const QString answer = lines.join("\r\n");
            QMetaObject::invokeMethod(this, [this, id, answer]() {
                Player *q = playerById(id);
                if (!q || !q->rtc) return;
                QByteArray out;
                QDataStream ds(&out, QIODevice::WriteOnly);
                net::prepare(ds);
                ds << answer;
                send(q, S_RTC_ANSWER, out);
            }, Qt::QueuedConnection);
        });
        pc->onDataChannel([this, id](std::shared_ptr<rtc::DataChannel> dc) {
            QMetaObject::invokeMethod(this, [this, id, dc]() {
                Player *q = playerById(id);
                if (!q || !q->rtc) return;
                q->rtc->dc = dc;
                if (dc->isOpen()) q->rtc->open = true;
            }, Qt::QueuedConnection);
            dc->onOpen([this, id]() {
                QMetaObject::invokeMethod(this, [this, id]() {
                    Player *q = playerById(id);
                    if (q && q->rtc) q->rtc->open = true;
                }, Qt::QueuedConnection);
            });
            dc->onClosed([this, id]() {
                QMetaObject::invokeMethod(this, [this, id]() {
                    Player *q = playerById(id);
                    if (q && q->rtc) q->rtc->open = false;
                }, Qt::QueuedConnection);
            });
            dc->onMessage([this, id](rtc::message_variant m) {
                if (!std::holds_alternative<rtc::binary>(m)) return;
                const rtc::binary &b = std::get<rtc::binary>(m);
                if (b.size() < 9 || b.size() > 4096) return;
                QByteArray data(reinterpret_cast<const char *>(b.data()), int(b.size()));
                QMetaObject::invokeMethod(this, [this, id, data]() {
                    Player *q = playerById(id);
                    if (!q || !q->rtc || qFromBigEndian<quint64>(data.constData() + 1) != q->fastToken) return;
                    fastPacket(q, data);
                }, Qt::QueuedConnection);
            });
        });
        pc->setRemoteDescription(rtc::Description(sdp.toStdString(), "offer"));
    } catch (const std::exception &e) {
        emit log(QString("WebRTC offer failed: %1").arg(e.what()));
        closeRtc(p);
    }
#else
    Q_UNUSED(p); Q_UNUSED(sdp);
#endif
}

void RaceServer::closeRtc(Player *p)
{
    if (!p->rtc) return;
#ifdef HAVE_RTC
    RtcPeer *r = p->rtc;
    p->rtc = nullptr;
    try { if (r->pc) r->pc->close(); } catch (...) {}
    delete r;
#else
    delete p->rtc;
    p->rtc = nullptr;
#endif
}

// ---------------------------------------------------------------------------
//  Discovery: "who has room CODE?" on the local network
// ---------------------------------------------------------------------------
void RaceServer::onDiscovery()
{
    while (udp.hasPendingDatagrams()) {
        QNetworkDatagram d = udp.receiveDatagram();
        QString msg = QString::fromUtf8(d.data());
        if (!msg.startsWith("TURBO8?")) continue;
        if (!d.senderAddress().isLoopback()) ++remoteSeen;
        QString code = msg.mid(7).trimmed().toUpper();
        if (!rooms.contains(code)) continue;
        QByteArray reply = QString("TURBO8!%1:%2").arg(code).arg(tcp.serverPort()).toUtf8();
        udp.writeDatagram(reply, d.senderAddress(), quint16(d.senderPort()));
    }
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
void RaceServer::send(Player *p, MsgType type, const QByteArray &payload)
{
    if (p && p->sock && p->sock->isOpen())
        p->sock->send(frame(type, payload));
}

// ---------------------------------------------------------------------------
//  Accounts (the internet server): a token is checked with the accounts API on the same
//  machine; the username it proves becomes the player's name, and their results go up
// ---------------------------------------------------------------------------
RaceServer::Player *RaceServer::playerById(quint64 id) const
{
    for (auto it = roomOf.constBegin(); it != roomOf.constEnd(); ++it)
        if (it.key()->id == id) return it.key();
    return nullptr;
}

void RaceServer::checkAccount(Player *p)
{
    if (p->token.isEmpty() || apiBase.isEmpty()) return;
    if (!nam) nam = new QNetworkAccessManager(this);
    QNetworkRequest rq(QUrl(apiBase + "/api/me"));
    rq.setRawHeader("Authorization", "Bearer " + p->token.toUtf8());
    rq.setTransferTimeout(8000);
    QNetworkReply *r = nam->get(rq);
    const quint64 id = p->id;
    connect(r, &QNetworkReply::finished, this, [this, r, id]() {
        r->deleteLater();
        Player *q = playerById(id);
        if (!q) return;
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString user = QJsonDocument::fromJson(r->readAll()).object()["username"].toString();
        if (status != 200 || user.isEmpty()) { q->token.clear(); return; }   // (stays a guest)
        Room *room = roomOf.value(q);
        q->account = user;
        q->name = room ? uniqueName(room, user, q) : user;
        emit log(QString("%1 is logged in").arg(user));
        if (room && !room->racing) sendLobby(room);
    });
}

void RaceServer::uploadResult(Player *p, const QJsonObject &race)
{
    if (p->token.isEmpty() || apiBase.isEmpty()) return;
    if (!nam) nam = new QNetworkAccessManager(this);
    QNetworkRequest rq(QUrl(apiBase + "/api/races"));
    rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    rq.setRawHeader("Authorization", "Bearer " + p->token.toUtf8());
    rq.setTransferTimeout(8000);
    QNetworkReply *r = nam->post(rq, QJsonDocument(race).toJson(QJsonDocument::Compact));
    const QString who = p->account;
    connect(r, &QNetworkReply::finished, this, [this, r, who]() {
        r->deleteLater();
        emit log(QString("Result of %1 sent to their account (%2)").arg(who).arg(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
    });
}

void RaceServer::fail(Player *p, const QString &reason)
{
    if (!p->sock) return;
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << reason;
    send(p, S_ERROR, out);
    p->sock->close();
}

QString RaceServer::newCode() const
{
    static const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O or 1/I mix-ups
    QString code;
    do {
        code.clear();
        for (int i = 0; i < 4; ++i) code += QChar(ALPHABET[QRandomGenerator::global()->bounded(32)]);
    } while (rooms.contains(code));
    return code;
}

// ---------------------------------------------------------------------------
//  HostedServer: the host's room server on its own thread
// ---------------------------------------------------------------------------
bool HostedServer::start()
{
    srv = new RaceServer;
    srv->moveToThread(&thread);
    thread.setObjectName("race server");
    thread.start(QThread::HighPriority);
    bool ok = false;
    QMetaObject::invokeMethod(srv, [this, &ok]() {     // sockets are opened on the server's thread
        ok = srv->start();
        listenPort = srv->port();
    }, Qt::BlockingQueuedConnection);
    return ok;
}

HostedServer::~HostedServer()
{
    if (srv) QMetaObject::invokeMethod(srv, [s = srv]() { delete s; }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
}
