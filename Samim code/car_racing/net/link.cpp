// The game's connections: plain TCP or WebSocket (see link.h).
#ifndef __EMSCRIPTEN__
#include "link.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>
#include <QtEndian>
#include <cstring>

namespace net {

static const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static QByteArray acceptKey(const QByteArray &key)
{
    return QCryptographicHash::hash(key + WS_GUID, QCryptographicHash::Sha1).toBase64();
}

Link::Link(QObject *parent) : QObject(parent) {}

Link::~Link()
{
    if (sock) { sock->disconnect(this); sock->abort(); delete sock; }
}

void Link::attach(QTcpSocket *s)
{
    sock = s;
    sock->setParent(this);
    connect(sock, &QTcpSocket::readyRead, this, &Link::onReadyRead);
    connect(sock, &QTcpSocket::disconnected, this, [this]() {

        open = false;
        if (wasOpen || mode == WsServer) emit closed();
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        if (open || sock->state() == QAbstractSocket::ConnectedState) return;   // (a running connection: disconnected() reports it)
        failKind = e == QAbstractSocket::ConnectionRefusedError ? Refused : e == QAbstractSocket::HostNotFoundError ? NotFound : Other;
        lastError = sock->errorString();
        emit failed();
    });
}

// ---- Server side ---------------------------------------------------------------------------

void Link::acceptTcp(QTcpSocket *s)
{
    mode = Tcp;
    attach(s);
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    open = wasOpen = true;
}

void Link::acceptWs(QTcpSocket *s)
{
    mode = WsServer;
    attach(s);
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    if (sock->bytesAvailable()) onReadyRead();
}

void Link::serverHandshake()
{
    const int end = raw.indexOf("\r\n\r\n");
    if (end < 0) { if (raw.size() > 8192) protocolError("handshake too long"); return; }
    const QList<QByteArray> lines = raw.left(end).split('\n');
    raw.remove(0, end + 4);
    QByteArray key, upgrade;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const int c = line.indexOf(':');
        if (c <= 0) continue;
        const QByteArray name = line.left(c).trimmed().toLower(), value = line.mid(c + 1).trimmed();
        if (name == "sec-websocket-key") key = value;
        else if (name == "upgrade") upgrade = value.toLower();
        else if (name == "x-forwarded-for") forwardedFor = QString::fromLatin1(value.split(',').first().trimmed());
    }
    if (lines.isEmpty() || !lines[0].startsWith("GET ") || upgrade != "websocket" || key.isEmpty()) {
        sock->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        protocolError("not a WebSocket request");
        return;
    }
    sock->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                + acceptKey(key) + "\r\n\r\n");
    handshaken = open = wasOpen = true;
    emit opened();
}

// ---- Client side ---------------------------------------------------------------------------

void Link::connectTcp(const QString &host, quint16 port)
{
    mode = Tcp;
    attach(new QTcpSocket(this));
    connect(sock, &QTcpSocket::connected, this, [this]() {
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        open = wasOpen = true;
        emit opened();
    });
    sock->connectToHost(host, port);
}

void Link::connectWs(const QUrl &u)
{
    mode = WsClient;
    url = u;
    const bool tls = u.scheme().toLower() == "wss";
    if (tls) {
        auto *ssl = new QSslSocket(this);
        attach(ssl);
        connect(ssl, &QSslSocket::encrypted, this, &Link::onConnected);
        connect(ssl, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errs) {
            lastError = errs.isEmpty() ? QString("TLS error") : errs.first().errorString();   // (never ignored)
        });
        ssl->connectToHostEncrypted(u.host(), quint16(u.port(443)));
    } else {
        attach(new QTcpSocket(this));
        connect(sock, &QTcpSocket::connected, this, &Link::onConnected);
        sock->connectToHost(u.host(), quint16(u.port(80)));
    }
}

void Link::onConnected()
{
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    QByteArray k(16, '\0');
    for (char &c : k) c = char(QRandomGenerator::global()->bounded(256));
    wsKey = k.toBase64();
    const QByteArray path = url.path().isEmpty() ? QByteArray("/") : url.path(QUrl::FullyEncoded).toLatin1();
    const QByteArray host = url.host().toLatin1() + (url.port() > 0 ? ":" + QByteArray::number(url.port()) : QByteArray());
    sock->write("GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: " + wsKey + "\r\nSec-WebSocket-Version: 13\r\n\r\n");
}

void Link::clientHandshake()
{
    const int end = raw.indexOf("\r\n\r\n");
    if (end < 0) { if (raw.size() > 8192) protocolError("handshake too long"); return; }
    const QList<QByteArray> lines = raw.left(end).split('\n');
    raw.remove(0, end + 4);
    QByteArray acceptValue;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const int c = line.indexOf(':');
        if (c > 0 && line.left(c).trimmed().toLower() == "sec-websocket-accept") acceptValue = line.mid(c + 1).trimmed();
    }
    if (lines.isEmpty() || !lines[0].contains(" 101") || acceptValue != acceptKey(wsKey)) {
        failKind = Other;
        lastError = lines.isEmpty() ? QString("no answer") : QString::fromLatin1(lines[0].trimmed());
        sock->abort();
        emit failed();
        return;
    }
    handshaken = open = wasOpen = true;
    emit opened();
}

// ---- Both ----------------------------------------------------------------------------------

void Link::onReadyRead()
{
    if (mode == Tcp) {
        received += sock->readAll();
        emit readyRead();
        return;
    }
    raw += sock->readAll();
    if (!handshaken) {
        if (mode == WsServer) serverHandshake(); else clientHandshake();
        if (!handshaken) return;
    }
    parseFrames();
}

void Link::parseFrames()
{
    bool got = false;
    while (raw.size() >= 2) {
        const quint8 b0 = quint8(raw[0]), b1 = quint8(raw[1]);
        const bool fin = b0 & 0x80, masked = b1 & 0x80;
        const int opcode = b0 & 0x0F;
        quint64 len = b1 & 0x7F;
        int at = 2;
        if (len == 126) {
            if (raw.size() < 4) break;
            len = qFromBigEndian<quint16>(raw.constData() + 2);
            at = 4;
        } else if (len == 127) {
            if (raw.size() < 10) break;
            len = qFromBigEndian<quint64>(raw.constData() + 2);
            at = 10;
        }
        if (len > quint64(MAX_MESSAGE)) { protocolError("message too big"); return; }
        if (masked != (mode == WsServer)) { protocolError("wrong masking"); return; }   // (clients mask, servers don't)
        const int need = at + (masked ? 4 : 0) + int(len);
        if (raw.size() < need) break;
        QByteArray payload = raw.mid(at + (masked ? 4 : 0), int(len));
        if (masked) {
            const char *m = raw.constData() + at;
            for (int i = 0; i < payload.size(); ++i) payload[i] = char(payload[i] ^ m[i % 4]);
        }
        raw.remove(0, need);
        switch (opcode) {
        case 0x0: case 0x1: case 0x2:                    // continuation, text, binary
            if (opcode != 0 && !message.isEmpty()) { protocolError("interleaved message"); return; }
            message += payload;
            if (message.size() > MAX_MESSAGE) { protocolError("message too big"); return; }
            if (fin) { received += message; message.clear(); got = true; }
            break;
        case 0x8:                                        // close: answer it and go (what came before it first)
            if (got) emit readyRead();
            if (!closing) sendFrame(0x8, payload.left(2));
            closing = true;
            sock->disconnectFromHost();
            return;
        case 0x9: sendFrame(0xA, payload); break;        // ping -> pong
        case 0xA: break;                                 // pong
        default: protocolError("unknown frame"); return;
        }
    }
    if (got) emit readyRead();
}

void Link::sendFrame(int opcode, const QByteArray &payload)
{
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) return;
    QByteArray f;
    f.reserve(payload.size() + 14);
    f.append(char(0x80 | opcode));
    const bool mask = mode == WsClient;
    const quint8 mbit = mask ? 0x80 : 0x00;
    const qsizetype n = payload.size();
    if (n < 126) {
        f.append(char(mbit | quint8(n)));
    } else if (n < 65536) {
        f.append(char(mbit | 126));
        char l[2]; qToBigEndian<quint16>(quint16(n), l); f.append(l, 2);
    } else {
        f.append(char(mbit | 127));
        char l[8]; qToBigEndian<quint64>(quint64(n), l); f.append(l, 8);
    }
    if (mask) {
        char m[4];
        const quint32 r = QRandomGenerator::global()->generate();
        std::memcpy(m, &r, 4);
        f.append(m, 4);
        QByteArray p = payload;
        for (qsizetype i = 0; i < p.size(); ++i) p[i] = char(p[i] ^ m[i % 4]);
        f.append(p);
    } else {
        f.append(payload);
    }
    sock->write(f);
}

void Link::send(const QByteArray &bytes)
{
    if (!open || !sock) return;
    if (mode == Tcp) sock->write(bytes);
    else sendFrame(0x2, bytes);
}

QByteArray Link::take()
{
    QByteArray out;
    out.swap(received);
    return out;
}

void Link::close()
{
    if (!sock) return;
    if (mode != Tcp && open && !closing) { closing = true; sendFrame(0x8, QByteArray::fromHex("03e8")); }   // (1000: normal)
    sock->flush();
    sock->disconnectFromHost();
}

void Link::abort()
{
    open = false;
    if (sock) sock->abort();
}

bool Link::isLoopback() const
{
    return sock && sock->peerAddress().isLoopback() && forwardedFor.isEmpty();
}

QString Link::clientAddress() const
{
    if (!forwardedFor.isEmpty()) return forwardedFor;
    return sock ? sock->peerAddress().toString() : QString();
}

void Link::protocolError(const QString &why)
{
    lastError = why;
    open = false;
    if (sock) sock->disconnectFromHost();
}

} // namespace net

#else   // ---- the browser: its own WebSocket (a web page has no TCP sockets) ------------------

#include "link.h"

#include <QMetaObject>
#include <emscripten/websocket.h>

namespace net {

Link::Link(QObject *parent) : QObject(parent) {}

Link::~Link()
{
    if (ws > 0) { emscripten_websocket_close(ws, 1000, ""); emscripten_websocket_delete(ws); }
}

void Link::acceptTcp(QTcpSocket *) {}
void Link::acceptWs(QTcpSocket *) {}

void Link::connectTcp(const QString &, quint16)
{
    failKind = Other;
    lastError = "a browser can only use WebSockets";
    QMetaObject::invokeMethod(this, [this]() { emit failed(); }, Qt::QueuedConnection);
}

static EM_BOOL webOpen(int, const EmscriptenWebSocketOpenEvent *, void *l) { static_cast<Link *>(l)->webEvent(0, nullptr, 0); return EM_TRUE; }
static EM_BOOL webMessage(int, const EmscriptenWebSocketMessageEvent *e, void *l)
{
    if (!e->isText) static_cast<Link *>(l)->webEvent(1, e->data, int(e->numBytes));
    return EM_TRUE;
}
static EM_BOOL webClose(int, const EmscriptenWebSocketCloseEvent *, void *l) { static_cast<Link *>(l)->webEvent(2, nullptr, 0); return EM_TRUE; }
static EM_BOOL webError(int, const EmscriptenWebSocketErrorEvent *, void *l) { static_cast<Link *>(l)->webEvent(3, nullptr, 0); return EM_TRUE; }

void Link::connectWs(const QUrl &u)
{
    mode = WsClient;
    url = u;
    urlText = u.toString().toUtf8();
    EmscriptenWebSocketCreateAttributes attr;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = urlText.constData();
    attr.createOnMainThread = EM_TRUE;
    ws = emscripten_websocket_new(&attr);
    if (ws <= 0) {
        failKind = Other;
        lastError = "the browser refused the WebSocket";
        QMetaObject::invokeMethod(this, [this]() { emit failed(); }, Qt::QueuedConnection);
        return;
    }
    emscripten_websocket_set_onopen_callback(ws, this, webOpen);
    emscripten_websocket_set_onmessage_callback(ws, this, webMessage);
    emscripten_websocket_set_onclose_callback(ws, this, webClose);
    emscripten_websocket_set_onerror_callback(ws, this, webError);
}

void Link::webEvent(int kind, const unsigned char *data, int size)
{
    switch (kind) {
    case 0:
        open = wasOpen = handshaken = true;
        emit opened();
        break;
    case 1:
        received.append(reinterpret_cast<const char *>(data), size);
        emit readyRead();
        break;
    case 2:
        open = false;
        if (wasOpen) { emit closed(); break; }
        failKind = Other;
        if (lastError.isEmpty()) lastError = "could not connect";
        emit failed();
        break;
    default:
        lastError = "connection error";
        break;
    }
}

void Link::send(const QByteArray &bytes)
{
    if (!open || ws <= 0) return;
    emscripten_websocket_send_binary(ws, const_cast<char *>(bytes.constData()), uint32_t(bytes.size()));
}

QByteArray Link::take()
{
    QByteArray out;
    out.swap(received);
    return out;
}

void Link::close()
{
    if (ws > 0 && open) emscripten_websocket_close(ws, 1000, "");
}

void Link::abort()
{
    open = false;
    if (ws > 0) emscripten_websocket_close(ws, 1000, "");
}

bool Link::isLoopback() const { return false; }
QString Link::clientAddress() const { return QString(); }

} // namespace net
#endif
