#include "raceclient.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <algorithm>

using namespace net;

RaceClient::RaceClient(QObject *parent)
    : QObject(parent)
{
    connect(&sock, &QTcpSocket::connected, this, &RaceClient::onConnected);
    connect(&sock, &QTcpSocket::readyRead, this, &RaceClient::onReadyRead);
    connect(&sock, &QTcpSocket::disconnected, this, &RaceClient::onDisconnected);
    connect(&sock, &QTcpSocket::errorOccurred, this, &RaceClient::onSocketError);
    connect(&udp, &QUdpSocket::readyRead, this, &RaceClient::onDiscoveryReply);
    connect(&discoverTimer, &QTimer::timeout, this, &RaceClient::onDiscoveryTimer);
    connect(&pingTimer, &QTimer::timeout, this, &RaceClient::onPingTimer);
    connectTimer.setSingleShot(true);
    connect(&connectTimer, &QTimer::timeout, this, &RaceClient::onConnectTimeout);
    pingClock.start();
    // Lag simulator for testing/demos: CAR_RACING_LAG_MS=150 delays every message
    // by half of that each way, like a slow internet connection.
    fakeLagMs = qEnvironmentVariableIntValue("CAR_RACING_LAG_MS");
}

RaceClient::~RaceClient()
{
    // cut the socket's signals before it is destroyed (it would report "disconnected")
    sock.disconnect(this);
    udp.disconnect(this);
    sock.abort();
}

void RaceClient::reset()
{
    discoverTimer.stop();
    connectTimer.stop();
    pingTimer.stop();
    udp.close();
    reader = FrameReader();
    code.clear();
    slot = -1;
    isHost = false;
    players.clear();
    pingMs = -1;
    joined = false;
    lagIn.clear();
    lagOut.clear();
}

void RaceClient::join(const QString &codeOrAddress, const QString &name, float r, float g, float b)
{
    leave();
    leaving = false;
    myName = name; myR = r; myG = g; myB = b; wantMap = 0;
    QString text = codeOrAddress.trimmed().toUpper();
    int at = text.indexOf('@');
    if (at >= 0) {                                       // direct: CODE@host[:port]
        wantCode = text.left(at);
        QString addr = text.mid(at + 1);
        quint16 port = GAME_PORT;
        int colon = addr.lastIndexOf(':');
        if (colon > 0) { port = quint16(addr.mid(colon + 1).toUInt()); addr = addr.left(colon); }
        connectTo(addr.toLower(), port);
        return;
    }
    wantCode = text;
    emit status("LOOKING FOR ROOM " + wantCode + " ON THIS NETWORK...");
    udp.bind(QHostAddress::AnyIPv4, 0);
    discoverTries = 0;
    onDiscoveryTimer();
    discoverTimer.start(300);
}

void RaceClient::create(const QString &host, quint16 port, const QString &name, float r, float g, float b, int map)
{
    leave();
    leaving = false;
    myName = name; myR = r; myG = g; myB = b; wantMap = map;
    wantCode.clear();
    connectTo(host, port);
}

void RaceClient::leave()
{
    leaving = true;
    reset();
    if (sock.state() != QAbstractSocket::UnconnectedState) {
        sock.disconnectFromHost();
        if (sock.state() != QAbstractSocket::UnconnectedState) sock.abort();
    }
}

void RaceClient::connectTo(const QString &host, quint16 port)
{
    discoverTimer.stop();
    connectHost = host;
    emit status(QString("CONNECTING TO %1...").arg(host.toUpper()));
    sock.connectToHost(host, port);
    connectTimer.start(6000);   // a firewall silently drops the connection: don't wait forever
}

void RaceClient::onConnectTimeout()
{
    if (sock.state() == QAbstractSocket::ConnectedState) return;
    leaving = true;
    sock.abort();
    emit failed("FOUND " + connectHost.toUpper() + " BUT IT DID NOT LET US IN|"
                "WINDOWS FIREWALL ON THE HOST PC IS BLOCKING THE GAME|"
                "ON THE HOST PC: RIGHT-CLICK ALLOW_FIREWALL.BAT > RUN AS ADMINISTRATOR");
}

// ---------------------------------------------------------------------------
//  Discovery: ask every network we are on "who has room CODE?"
// ---------------------------------------------------------------------------
void RaceClient::onDiscoveryTimer()
{
    if (++discoverTries > 14) {                          // ~4 s without an answer
        discoverTimer.stop();
        udp.close();
        emit failed("ROOM " + wantCode + " NOT FOUND ON THIS NETWORK|"
                    "1. HOST: RUN ALLOW_FIREWALL.BAT ONCE (RIGHT-CLICK > RUN AS ADMINISTRATOR)|"
                    "2. OR TYPE THE CODE@ADDRESS SHOWN IN THE HOST LOBBY|"
                    "3. STILL NOTHING? THIS WI-FI BLOCKS DEVICES - USE A PHONE HOTSPOT");
        return;
    }
    QByteArray ask = ("TURBO8?" + wantCode).toUtf8();
    const bool testNoBroadcast = qEnvironmentVariableIsSet("CAR_RACING_NO_BROADCAST");   // tests the sweep
    if (!testNoBroadcast) {
        udp.writeDatagram(ask, QHostAddress::Broadcast, DISCOVERY_PORT);
        udp.writeDatagram(ask, QHostAddress::LocalHost, DISCOVERY_PORT);
        for (const QNetworkAddressEntry &e : lanEntries())
            if (!e.broadcast().isNull()) udp.writeDatagram(ask, e.broadcast(), DISCOVERY_PORT);
    }
    // Many routers drop broadcasts between Wi-Fi devices. So after a moment we also ask
    // every address on our subnet directly (unicast gets through where broadcast doesn't).
    if (discoverTries == 2 || discoverTries == 6 || discoverTries == 10) {
        for (const QNetworkAddressEntry &e : lanEntries()) {
            // big networks (college Wi-Fi is often /16): ask the 1022 addresses nearest to us
            int prefix = std::max(e.prefixLength(), 22);
            if (prefix > 30) continue;
            quint32 ip = e.ip().toIPv4Address(), mask = 0xFFFFFFFFu << (32 - prefix);
            quint32 first = (ip & mask) + 1, last = (ip | ~mask) - 1;
            for (quint32 a = first; a <= last; ++a)
                udp.writeDatagram(ask, QHostAddress(a), DISCOVERY_PORT);
        }
    }
}

void RaceClient::onDiscoveryReply()
{
    while (udp.hasPendingDatagrams()) {
        QNetworkDatagram d = udp.receiveDatagram();
        QString msg = QString::fromUtf8(d.data());
        if (!msg.startsWith("TURBO8!") || !discoverTimer.isActive()) continue;
        QStringList parts = msg.mid(7).split(':');
        if (parts.size() != 2 || parts[0] != wantCode) continue;
        QHostAddress from(d.senderAddress().toIPv4Address());     // strip any IPv6 mapping
        udp.close();
        connectTo(from.toString(), quint16(parts[1].toUInt()));
        return;
    }
}

// ---------------------------------------------------------------------------
//  TCP connection
// ---------------------------------------------------------------------------
void RaceClient::onConnected()
{
    connectTimer.stop();
    sock.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    emit status("JOINING...");
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << PROTOCOL_VERSION << myName << myR << myG << myB << wantCode << qint8(wantMap);
    send(C_JOIN, out);
    pingTimer.start(1000);
}

void RaceClient::onReadyRead()
{
    reader.feed(sock.readAll());
    MsgType type;
    QByteArray payload;
    while (reader.next(type, payload)) {
        if (fakeLagMs > 0) {
            // lag simulator: each timer hands over the OLDEST waiting message, as timers due
            // at the same moment may fire in any order (a real TCP connection never reorders)
            lagIn.emplace_back(type, payload);
            QTimer::singleShot(fakeLagMs / 2, Qt::PreciseTimer, this, [this]() {
                if (lagIn.empty()) return;
                auto m = lagIn.front();
                lagIn.pop_front();
                dispatch(m.first, m.second);
            });
        } else {
            dispatch(type, payload);
        }
    }
}

void RaceClient::dispatch(MsgType type, const QByteArray &payload)
{
    {
        QDataStream in(payload);
        prepare(in);
        switch (type) {
        case S_WELCOME: {
            qint8 s; bool h;
            in >> s >> code >> h;
            slot = s; isHost = h; joined = true;
            emit welcomed();
            break;
        }
        case S_ERROR: {
            QString reason;
            in >> reason;
            leaving = true;
            emit failed(reason.toUpper());
            break;
        }
        case S_LOBBY: {
            qint8 map, me; bool host; quint8 n;
            in >> map >> me >> host >> n;
            lobbyMap = map; slot = me; isHost = host;
            players.clear();
            for (int i = 0; i < n; ++i) {
                LobbyPlayer p;
                qint8 lvl;
                in >> p.name >> p.r >> p.g >> p.b >> p.host >> lvl;
                p.cpuLevel = lvl;
                players.append(p);
            }
            emit lobbyChanged();
            break;
        }
        case S_RACE_START: {
            qint8 map, s; quint8 n;
            in >> map >> s >> n;
            QStringList names;
            for (int i = 0; i < n; ++i) { QString nm; in >> nm; names << nm; }
            quint32 world = 0;
            in >> world;
            slot = s;
            emit raceStarted(map, s, names, world);
            break;
        }
        case S_SNAPSHOT: {
            quint32 lastSeq;
            in >> lastSeq;
            emit snapshotArrived(lastSeq, payload.mid(4));
            break;
        }
        case S_PONG: {
            qint64 sent;
            in >> sent;
            pingMs = int(pingClock.elapsed() - sent);
            break;
        }
        case S_RACE_OVER:
            emit raceOver();
            break;
        default:
            break;
        }
    }
}

void RaceClient::onDisconnected()
{
    pingTimer.stop();
    if (!leaving) emit failed(joined ? "LOST CONNECTION TO THE HOST" : "COULD NOT JOIN");
}

void RaceClient::onSocketError(QAbstractSocket::SocketError)
{
    if (leaving || sock.state() == QAbstractSocket::ConnectedState) return;
    if (joined) return;
    connectTimer.stop();
    leaving = true;
    if (sock.error() == QAbstractSocket::ConnectionRefusedError)
        emit failed("NO GAME IS HOSTING AT " + connectHost.toUpper() + "|CHECK THE CODE@ADDRESS, OR ASK THE HOST TO HOST AGAIN");
    else if (sock.error() == QAbstractSocket::HostNotFoundError)
        emit failed("ADDRESS " + connectHost.toUpper() + " NOT FOUND|CHECK THE CODE@ADDRESS");
    else
        emit failed("COULD NOT CONNECT TO " + connectHost.toUpper() + "|" + sock.errorString().toUpper());
}

void RaceClient::onPingTimer()
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << qint64(pingClock.elapsed());
    send(C_PING, out);
}

void RaceClient::sendInput(quint32 seq, const sim::PlayerInput &in)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << seq << packInput(in);
    send(C_INPUT, out);
}

void RaceClient::requestStart() { send(C_START); }
void RaceClient::requestAddCpu() { send(C_ADD_BOT); }
void RaceClient::requestRemoveCpu() { send(C_REMOVE_BOT); }

void RaceClient::requestMap(int map)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << qint8(map);
    send(C_SET_MAP, out);
}

void RaceClient::send(MsgType type, const QByteArray &payload)
{
    if (sock.state() != QAbstractSocket::ConnectedState) return;
    QByteArray f = frame(type, payload);
    if (fakeLagMs > 0) {
        lagOut.push_back(f);                     // in order, like lagIn
        QTimer::singleShot(fakeLagMs / 2, Qt::PreciseTimer, this, [this]() {
            if (lagOut.empty()) return;
            QByteArray oldest = lagOut.front();
            lagOut.pop_front();
            if (sock.state() == QAbstractSocket::ConnectedState) sock.write(oldest);
        });
    } else {
        sock.write(f);
    }
}
