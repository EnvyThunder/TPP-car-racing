#include "raceclient.h"

#include <QNetworkDatagram>
#include <QHostInfo>
#include <QUrl>
#include <QtEndian>
#ifndef __EMSCRIPTEN__
#include <QNetworkInterface>
#endif
#include <algorithm>

using namespace net;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The browser's fast lane: a WebRTC data channel to the server, unordered and never resent
// (what UDP is to the desktop game). The offer, with all its candidates, goes over the
// WebSocket; the answer comes back the same way. Everything the page hears lands in
// tppRtcBuf and is handed to tpp_rtc_event (0 offer ready, 1 a message, 2 open, 3 closed).
static RaceClient *rtcClient = nullptr;
static unsigned char tppRtcBuf[65536];
extern "C" EMSCRIPTEN_KEEPALIVE void tpp_rtc_event(int kind, int size)
{
    if (rtcClient) rtcClient->rtcEvent(kind, tppRtcBuf, size);
}
EM_JS(void, tpp_rtc_start, (unsigned char *buf, int cap), {
    try {
        if (window.tppRtc) { try { window.tppRtc.pc.close(); } catch (e) {} }
        const pc = new RTCPeerConnection({ iceServers: [] });
        const dc = pc.createDataChannel("fast", { ordered: false, maxRetransmits: 0 });
        dc.binaryType = "arraybuffer";
        window.tppRtc = { pc: pc, dc: dc };
        const give = function (kind, bytes) {
            const n = Math.min(bytes.length, cap);
            HEAPU8.set(bytes.subarray(0, n), buf);
            _tpp_rtc_event(kind, n);
        };
        dc.onopen = function () { give(2, new Uint8Array(0)); };
        dc.onclose = function () { give(3, new Uint8Array(0)); };
        dc.onmessage = function (ev) { give(1, new Uint8Array(ev.data)); };
        let sent = false;
        const offer = function () {
            if (sent || !pc.localDescription) return;
            sent = true;
            give(0, new TextEncoder().encode(pc.localDescription.sdp));
        };
        pc.onicegatheringstatechange = function () { if (pc.iceGatheringState === "complete") offer(); };
        pc.createOffer().then(function (o) { return pc.setLocalDescription(o); })
          .then(function () { setTimeout(offer, 1500); });   // (gathering slow: send what there is)
    } catch (e) { console.log("fast lane unavailable: " + e); }
});
EM_JS(void, tpp_rtc_answer, (const char *sdp), {
    try { if (window.tppRtc) window.tppRtc.pc.setRemoteDescription({ type: "answer", sdp: UTF8ToString(sdp) }); }
    catch (e) { console.log("fast lane answer: " + e); }
});
EM_JS(void, tpp_rtc_send, (const unsigned char *p, int n), {
    try { const r = window.tppRtc; if (r && r.dc.readyState === "open") r.dc.send(HEAPU8.slice(p, p + n)); } catch (e) {}
});
EM_JS(void, tpp_rtc_stop, (), {
    try { if (window.tppRtc) window.tppRtc.pc.close(); } catch (e) {}
    window.tppRtc = null;
});
#endif

RaceClient::RaceClient(QObject *parent)
    : QObject(parent)
{
    connect(&udp, &QUdpSocket::readyRead, this, &RaceClient::onDiscoveryReply);
    connect(&discoverTimer, &QTimer::timeout, this, &RaceClient::onDiscoveryTimer);
    connect(&pingTimer, &QTimer::timeout, this, &RaceClient::onPingTimer);
    connectTimer.setSingleShot(true);
    connect(&connectTimer, &QTimer::timeout, this, &RaceClient::onConnectTimeout);
    pingClock.start();
    connect(&fastSock, &QUdpSocket::readyRead, this, &RaceClient::onFastRead);
    connect(&fastTimer, &QTimer::timeout, this, &RaceClient::onFastTimer);
    // Lag simulator for testing/demos: CAR_RACING_LAG_MS=150 delays every message
    // by half of that each way, like a slow internet connection.
    fakeLagMs = qEnvironmentVariableIntValue("CAR_RACING_LAG_MS");
}

RaceClient::~RaceClient()
{
    // cut the socket's signals before it is destroyed (it would report "disconnected")
    if (link) { link->disconnect(this); link->abort(); }
    udp.disconnect(this);
    fastSock.disconnect(this);
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
#ifdef __EMSCRIPTEN__
    if (rtcWeb) tpp_rtc_stop();
    rtcWeb = false;
#endif
    fastTimer.stop();
    fastOk = false;
    fastToken = 0;
    fastHost = QHostAddress();
    lastSnapTick = -1;
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
    const QString rest = at >= 0 ? codeOrAddress.trimmed().mid(at + 1) : QString();
    if (rest.startsWith("ws://", Qt::CaseInsensitive) || rest.startsWith("wss://", Qt::CaseInsensitive)) {   // CODE@wss://host/race
        wantCode = text.left(at);
        connectTo(rest, 0);
        return;
    }
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
#ifdef __EMSCRIPTEN__
    if (!internetUrl.isEmpty()) { connectTo(internetUrl, 0); return; }   // (a browser can't search the Wi-Fi)
#endif
    emit status("LOOKING FOR ROOM " + wantCode + " ON THIS NETWORK...");
    udp.bind(QHostAddress::AnyIPv4, 0);
    discoverTries = 0;
    onDiscoveryTimer();
    discoverTimer.start(300);
}

void RaceClient::create(const QString &host, quint16 port, const QString &name, float r, float g, float b, int map, int laps)
{
    leave();
    leaving = false;
    myName = name; myR = r; myG = g; myB = b; wantMap = map; wantLaps = laps;
    wantCode.clear();
    connectTo(host, port);
}

void RaceClient::leave()
{
    leaving = true;
    reset();
    if (link) {
        link->disconnect(this);
        link->close();
        link->deleteLater();
        link = nullptr;
    }
}

void RaceClient::connectTo(const QString &host, quint16 port)
{
    discoverTimer.stop();
    connectHost = host;
    if (link) { link->disconnect(this); link->abort(); link->deleteLater(); }
    link = new net::Link(this);
    connect(link, &net::Link::opened, this, &RaceClient::onConnected);
    connect(link, &net::Link::readyRead, this, &RaceClient::onReadyRead);
    connect(link, &net::Link::closed, this, &RaceClient::onDisconnected);
    connect(link, &net::Link::failed, this, &RaceClient::onLinkFailed);
    const bool web = host.startsWith("ws://", Qt::CaseInsensitive) || host.startsWith("wss://", Qt::CaseInsensitive);
    emit status(web ? QString("CONNECTING TO THE INTERNET SERVER...") : QString("CONNECTING TO %1...").arg(host.toUpper()));
    if (web) link->connectWs(QUrl(host));
    else link->connectTcp(host, port);
    connectTimer.start(web ? 12000 : 6000);   // a firewall silently drops the connection: don't wait forever
}

void RaceClient::onConnectTimeout()
{
    if (link && link->isOpen()) return;
    leaving = true;
    if (link) link->abort();
    if (connectHost.startsWith("ws", Qt::CaseInsensitive)) {
        emit failed("THE INTERNET SERVER DID NOT ANSWER|CHECK THE INTERNET CONNECTION, OR TRY AGAIN LATER");
        return;
    }
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
        if (!internetUrl.isEmpty()) {                    // not on this Wi-Fi: perhaps on the internet server
            emit status("ROOM " + wantCode + " IS NOT ON THIS NETWORK - TRYING THE INTERNET SERVER...");
            connectTo(internetUrl, 0);
            return;
        }
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
        for (const auto &e : lanEntries())
            if (!e.broadcast().isNull()) udp.writeDatagram(ask, e.broadcast(), DISCOVERY_PORT);
    }
    // Many routers drop broadcasts between Wi-Fi devices. So after a moment we also ask
    // every address on our subnet directly (unicast gets through where broadcast doesn't).
    if (discoverTries == 2 || discoverTries == 6 || discoverTries == 10) {
        for (const auto &e : lanEntries()) {
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
    emit status("JOINING...");
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << PROTOCOL_VERSION << myName << myR << myG << myB << wantCode << qint8(wantMap) << qint8(wantLaps) << token;
    send(C_JOIN, out);
    pingTimer.start(1000);
}

void RaceClient::onReadyRead()
{
    if (!link) return;
    reader.feed(link->take());
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
#ifdef __EMSCRIPTEN__
            if (!in.atEnd()) {                           // the fast lane: the page's WebRTC data channel
                quint16 port; quint64 token;
                in >> port >> token;
                if (!qEnvironmentVariableIsSet("CAR_RACING_NET_NOFAST")) {
                    fastToken = token;
                    fastOk = false;
                    rtcWeb = true;
                    rtcClient = this;
                    tpp_rtc_start(tppRtcBuf, int(sizeof tppRtcBuf));
                }
            }
#else
            if (!in.atEnd()) {                           // the fast lane: say hello on it until it answers
                quint16 port; quint64 token;
                in >> port >> token;
                const int testPort = qEnvironmentVariableIntValue("CAR_RACING_NET_FASTPORT");   // (tests: through a lag proxy)
                if (port && !qEnvironmentVariableIsSet("CAR_RACING_NET_NOFAST")) {
                    fastPort = testPort > 0 ? quint16(testPort) : port;
                    fastToken = token;
                    fastOk = false;
                    const QString host = connectHost.startsWith("ws", Qt::CaseInsensitive) ? QUrl(connectHost).host() : connectHost;
                    QHostInfo::lookupHost(host, this, [this, token](const QHostInfo &info) {
                        if (token != fastToken) return;
                        for (const QHostAddress &a : info.addresses())
                            if (a.protocol() == QAbstractSocket::IPv4Protocol) { fastHost = a; break; }
                        if (fastHost.isNull()) return;
                        if (fastSock.state() != QAbstractSocket::BoundState) fastSock.bind(QHostAddress::AnyIPv4, 0);
                        onFastTimer();
                        fastTimer.start(250);
                    });
                }
            }
#endif
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
            qint8 map, laps, me; bool host; quint8 n;
            in >> map >> laps >> me >> host >> n;
            lobbyMap = map; lobbyLaps = laps; slot = me; isHost = host;
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
            qint8 map, laps, s; quint8 n;
            in >> map >> laps >> s >> n;
            QStringList names;
            for (int i = 0; i < n; ++i) { QString nm; in >> nm; names << nm; }
            quint32 world = 0;
            in >> world;
            slot = s;
            lastSnapTick = -1;
            emit raceStarted(map, laps, s, names, world);
            if (autoStory) sendStory(false);
            break;
        }
        case S_SNAPSHOT:
            handleSnapshot(payload);
            break;
        case S_EXTRAS:
            emit extrasArrived(payload);
            break;
#ifdef __EMSCRIPTEN__
        case S_RTC_ANSWER: {
            QString sdp;
            in >> sdp;
            if (rtcWeb) tpp_rtc_answer(sdp.toUtf8().constData());
            break;
        }
#endif
        case S_PONG: {
            qint64 sent;
            in >> sent;
            pingMs = int(pingClock.elapsed() - sent);
            break;
        }
        case S_STORY_END:
            emit storyEnded();
            break;
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

void RaceClient::onLinkFailed()
{
    if (leaving || !link || link->isOpen()) return;
    if (joined) return;
    connectTimer.stop();
    leaving = true;
    if (connectHost.startsWith("ws", Qt::CaseInsensitive))
        emit failed("COULD NOT REACH THE INTERNET SERVER|" + link->errorText().toUpper());
    else if (link->failure() == net::Link::Refused)
        emit failed("NO GAME IS HOSTING AT " + connectHost.toUpper() + "|CHECK THE CODE@ADDRESS, OR ASK THE HOST TO HOST AGAIN");
    else if (link->failure() == net::Link::NotFound)
        emit failed("ADDRESS " + connectHost.toUpper() + " NOT FOUND|CHECK THE CODE@ADDRESS");
    else
        emit failed("COULD NOT CONNECT TO " + connectHost.toUpper() + "|" + link->errorText().toUpper());
}

void RaceClient::onPingTimer()
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << qint64(pingClock.elapsed());
    send(C_PING, out);
}

void RaceClient::sendStory(bool skipForAll)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << quint8(skipForAll ? 1 : 0);
    send(C_STORY, out);
}

void RaceClient::sendInput(quint32 seq, const QByteArray &bits, int want)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << seq << quint8(std::clamp(want, 1, 12)) << quint8(bits.size());
    out += bits;
    if (fastActive()) sendFast(F_INPUT, out);
    else send(C_INPUT, out);
}

void RaceClient::requestStart() { send(C_START); }
void RaceClient::requestAddCpu() { send(C_ADD_BOT); }
void RaceClient::requestRemoveCpu() { send(C_REMOVE_BOT); }

void RaceClient::requestName(const QString &name)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << name;
    send(C_SET_NAME, out);
}

void RaceClient::requestMap(int map, int laps)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    prepare(ds);
    ds << qint8(map) << qint8(laps);
    send(C_SET_MAP, out);
}

void RaceClient::send(MsgType type, const QByteArray &payload)
{
    if (!link || !link->isOpen()) return;
    QByteArray f = frame(type, payload);
    if (fakeLagMs > 0) {
        lagOut.push_back(f);                     // in order, like lagIn
        QTimer::singleShot(fakeLagMs / 2, Qt::PreciseTimer, this, [this]() {
            if (lagOut.empty()) return;
            QByteArray oldest = lagOut.front();
            lagOut.pop_front();
            if (link && link->isOpen()) link->send(oldest);
        });
    } else {
        link->send(f);
    }
}

// ---------------------------------------------------------------------------
//  The fast lane (net/protocol.h)
// ---------------------------------------------------------------------------
void RaceClient::handleSnapshot(const QByteArray &payload)
{
    if (payload.size() < 13) return;
    const qint32 tick = qFromBigEndian<qint32>(payload.constData() + 9);   // (after seq, queued, state, mode, map, laps)
    if (tick <= lastSnapTick) return;                // older than one already shown (the fast lane can reorder)
    lastSnapTick = tick;
    QDataStream in(payload);
    prepare(in);
    quint32 lastSeq; quint8 queued;
    in >> lastSeq >> queued;
    emit snapshotArrived(lastSeq, queued, payload.mid(5));
}

bool RaceClient::fastActive() const
{
    return fastOk && fastLastRx >= 0 && pingClock.elapsed() - fastLastRx < 3500;
}

void RaceClient::sendFast(quint8 type, const QByteArray &payload)
{
    QByteArray dg(9, 0);
    dg[0] = char(type);
    qToBigEndian<quint64>(fastToken, dg.data() + 1);
    dg += payload;
#ifdef __EMSCRIPTEN__
    if (rtcWeb) { tpp_rtc_send(reinterpret_cast<const unsigned char *>(dg.constData()), int(dg.size())); return; }
#endif
    fastSock.writeDatagram(dg, fastHost, fastPort);
}

void RaceClient::onFastTimer()
{
    bool web = false;
#ifdef __EMSCRIPTEN__
    web = rtcWeb;
#endif
    if (!link || !link->isOpen() || !fastToken || (fastHost.isNull() && !web)) { fastTimer.stop(); return; }
    if (fastOk && !fastActive()) fastOk = false;     // nothing back for a while: say hello again
    sendFast(F_HELLO, QByteArray());
    fastTimer.setInterval(fastOk ? 1000 : 250);
}

void RaceClient::onFastRead()
{
    while (fastSock.hasPendingDatagrams()) {
        const QNetworkDatagram d = fastSock.receiveDatagram(65536);
        const QByteArray data = d.data();
        if (data.size() < 9 || qFromBigEndian<quint64>(data.constData() + 1) != fastToken) continue;
        fastLastRx = pingClock.elapsed();
        if (quint8(data[0]) == F_HELLO_OK) fastOk = true;
        else if (quint8(data[0]) == F_SNAPSHOT && fastOk) handleSnapshot(data.mid(9));
    }
}

#ifdef __EMSCRIPTEN__
void RaceClient::rtcEvent(int kind, const unsigned char *data, int size)
{
    if (!rtcWeb) return;
    if (kind == 0) {                                     // the offer: to the server by the WebSocket
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);
        prepare(ds);
        ds << QString::fromUtf8(reinterpret_cast<const char *>(data), size);
        send(C_RTC_OFFER, out);
    } else if (kind == 1) {                              // a datagram
        QByteArray d(reinterpret_cast<const char *>(data), size);
        if (d.size() < 9 || qFromBigEndian<quint64>(d.constData() + 1) != fastToken) return;
        fastLastRx = pingClock.elapsed();
        if (quint8(d[0]) == F_HELLO_OK) fastOk = true;
        else if (quint8(d[0]) == F_SNAPSHOT && fastOk) handleSnapshot(d.mid(9));
    } else if (kind == 2) {                              // open: say hello on it (the answer proves both ways)
        onFastTimer();
        fastTimer.start(250);
    } else if (kind == 3) {
        fastOk = false;
    }
}
#endif
