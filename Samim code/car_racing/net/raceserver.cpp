#include "raceserver.h"

#include <QTcpSocket>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

using namespace net;

RaceServer::RaceServer(QObject *parent)
    : QObject(parent)
{
    connect(&tcp, &QTcpServer::newConnection, this, &RaceServer::onNewConnection);
    connect(&udp, &QUdpSocket::readyRead, this, &RaceServer::onDiscovery);
    connect(&timer, &QTimer::timeout, this, &RaceServer::onTick);
}

RaceServer::~RaceServer()
{
    // Close the sockets first, with their signals cut: a socket being destroyed reports
    // "disconnected", which must not reach this half-destroyed server.
    timer.stop();
    tcp.close();
    for (QTcpSocket *s : bySocket.keys()) {
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
    clock.start();
    timer.setTimerType(Qt::PreciseTimer);
    timer.start(5);
    emit log(QString("Game server listening on TCP port %1").arg(tcp.serverPort()));
    return true;
}

// ---------------------------------------------------------------------------
//  Connections and messages
// ---------------------------------------------------------------------------
void RaceServer::onNewConnection()
{
    while (QTcpSocket *sock = tcp.nextPendingConnection()) {
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);   // no Nagle delay on tiny packets
        if (!sock->peerAddress().isLoopback()) ++remoteSeen;
        Player *p = new Player;
        p->sock = sock;
        bySocket.insert(sock, p);
        connect(sock, &QTcpSocket::readyRead, this, &RaceServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &RaceServer::onDisconnected);
    }
}

void RaceServer::onReadyRead()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    Player *p = bySocket.value(sock);
    if (!p) return;
    p->reader.feed(sock->readAll());
    MsgType type;
    QByteArray payload;
    while (p->sock && p->reader.next(type, payload)) handle(p, type, payload);
    if (p->reader.broken()) fail(p, "Bad data from client");
}

void RaceServer::onDisconnected()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    Player *p = bySocket.take(sock);
    sock->deleteLater();
    if (!p) return;
    p->sock = nullptr;
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
    case C_INPUT: {
        quint32 seq; quint8 bits;
        in >> seq >> bits;
        if (room && room->racing) p->inputs.emplace_back(seq, unpackInput(bits));
        break;
    }
    case C_START:
        if (room && room->host == p && !room->racing) startRace(room);
        break;
    case C_SET_MAP: {
        qint8 m;
        in >> m;
        if (room && room->host == p && !room->racing) {
            room->map = std::clamp(int(m), 0, sim::NUM_MAPS - 1);
            sendLobby(room);
        }
        break;
    }
    case C_PING:
        send(p, S_PONG, payload);
        break;
    default:
        break;
    }
}

void RaceServer::join(Player *p, QDataStream &in)
{
    quint16 version; QString name, code; float r, g, b; qint8 map;
    in >> version >> name >> r >> g >> b >> code >> map;
    if (version != PROTOCOL_VERSION) { fail(p, "Different game version - update the game"); return; }
    p->name = name.trimmed().left(10).toUpper();
    if (p->name.isEmpty()) p->name = "PLAYER";
    p->r = r; p->g = g; p->b = b;

    Room *room = nullptr;
    code = code.trimmed().toUpper();
    if (!code.isEmpty()) {
        // Same name or same car colour as someone already in the room? Make it unique,
        // so everyone can tell the cars apart on track and on the minimap.
        if (Room *r0 = rooms.value(code)) {
            QString base = p->name;
            for (int n = 2; ; ++n) {
                bool taken = false;
                for (Player *q : r0->players) taken = taken || q->name == p->name;
                if (!taken) break;
                p->name = base.left(9) + QString::number(n);
            }
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
        room = new Room;
        room->code = newCode();
        room->map = std::clamp(int(map), 0, sim::NUM_MAPS - 1);
        room->host = p;
        rooms.insert(room->code, room);
        emit log(QString("Room %1 created by %2").arg(room->code, p->name));
    } else {
        room = rooms.value(code);
        if (!room)                                   { fail(p, "No room with code " + code); return; }
        if (room->racing)                            { fail(p, "Race in progress - join after it ends"); return; }
        if (room->players.size() >= sim::MAX_RACERS) { fail(p, "Room is full (8/8)"); return; }
        emit log(QString("%1 joined room %2").arg(p->name, room->code));
    }
    room->players.append(p);
    roomOf.insert(p, room);

    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << qint8(room->players.size() - 1) << room->code << bool(room->host == p);
    send(p, S_WELCOME, out);
    sendLobby(room);
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

void RaceServer::sendLobby(Room *room)
{
    for (int me = 0; me < room->players.size(); ++me) {
        Player *to = room->players[me];
        if (!to->sock) continue;
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        prepare(ds);
        ds << qint8(room->map) << qint8(me) << bool(room->host == to) << quint8(room->players.size());
        for (Player *q : room->players)
            ds << q->name << q->r << q->g << q->b << bool(room->host == q);
        send(to, S_LOBBY, out);
    }
}

// ---------------------------------------------------------------------------
//  Racing
// ---------------------------------------------------------------------------
void RaceServer::startRace(Room *room)
{
    const int n = room->players.size();
    room->sim.setMap(room->map);
    room->sim.startRace(sim::GameSim::ModeOnline, n);
    auto &cars = room->sim.getCars();
    for (int i = 0; i < n; ++i) {
        Player *p = room->players[i];
        cars[i].r = p->r; cars[i].g = p->g; cars[i].b = p->b;
        p->inputs.clear();
        p->current = sim::PlayerInput{};
        p->lastSeq = 0;
    }
    room->racing = true;
    room->overTicks = 0;
    room->steps = 0;
    room->pendingEvents.clear();
    room->lastSentObstacles.clear();
    emit log(QString("Room %1: race started on %2 with %3 players").arg(room->code, sim::MAPS[room->map].name).arg(n));

    for (int i = 0; i < n; ++i) {
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        prepare(ds);
        ds << qint8(room->map) << qint8(i) << quint8(n);
        for (Player *q : room->players) ds << q->name;
        send(room->players[i], S_RACE_START, out);
    }
}

void RaceServer::endRace(Room *room)
{
    room->racing = false;
    for (int i = room->players.size() - 1; i >= 0; --i) {  // drop players who left during the race
        Player *p = room->players[i];
        if (!p->sock) { roomOf.remove(p); room->players.removeAt(i); delete p; }
    }
    for (Player *p : room->players) send(p, S_RACE_OVER);
    sendLobby(room);
}

void RaceServer::onTick()
{
    const float DT = 1.0f / 60.0f;
    accumulator += clock.restart() * 0.001;
    if (accumulator > 0.25) accumulator = 0.25;              // don't spiral after a stall
    while (accumulator >= DT) {
        accumulator -= DT;
        const auto roomList = rooms.values();
        for (Room *room : roomList) {
            if (!room->racing) continue;
            const int n = room->players.size();
            sim::PlayerInput inputs[sim::MAX_RACERS];
            for (int i = 0; i < n; ++i) {
                Player *p = room->players[i];
                if (!p->sock) { inputs[i] = sim::PlayerInput{}; continue; }
                while (p->inputs.size() > 6) {                 // client ran ahead: skip stale inputs
                    p->lastSeq = p->inputs.front().first;
                    p->inputs.pop_front();
                }
                if (!p->inputs.empty()) {                      // one input per tick, like the client
                    p->current = p->inputs.front().second;
                    p->lastSeq = p->inputs.front().first;
                    p->inputs.pop_front();
                }
                inputs[i] = p->current;
            }
            room->sim.step(inputs, n);
            const auto &evs = room->sim.getEvents();
            if (room->pendingEvents.size() < 300)
                room->pendingEvents.insert(room->pendingEvents.end(), evs.begin(), evs.end());

            if (++room->steps % SNAPSHOT_EVERY == 0) {
                QByteArray body;
                QDataStream ds(&body, QIODevice::WriteOnly);
                prepare(ds);
                room->sim.writeSnapshot(ds, room->pendingEvents, &room->lastSentObstacles);
                room->pendingEvents.clear();
                for (Player *p : room->players) {
                    if (!p->sock) continue;
                    QByteArray out;
                    QDataStream hs(&out, QIODevice::WriteOnly);
                    prepare(hs);
                    hs << quint32(p->lastSeq);
                    send(p, S_SNAPSHOT, out + body);
                }
            }
            // results stay on screen for a few seconds, then everyone is back in the lobby
            if (room->sim.getState() == sim::GameSim::Finished && ++room->overTicks > 60 * 7) endRace(room);
        }
    }
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
    if (p && p->sock && p->sock->state() == QAbstractSocket::ConnectedState)
        p->sock->write(frame(type, payload));
}

void RaceServer::fail(Player *p, const QString &reason)
{
    if (!p->sock) return;
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << reason;
    send(p, S_ERROR, out);
    p->sock->flush();
    p->sock->disconnectFromHost();
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
