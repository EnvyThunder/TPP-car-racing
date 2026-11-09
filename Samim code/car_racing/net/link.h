#ifndef LINK_H
#define LINK_H

// A connection that carries the game's framed messages (protocol.h) between a RaceClient and a
// RaceServer, over plain TCP (the same Wi-Fi) or a WebSocket (RFC 6455: the internet server,
// and later the browser version - browsers can only open WebSockets). Over a WebSocket every
// send() is one binary message; what arrives is handed on as one byte stream, so the
// FrameReader on top works the same either way. wss:// runs the WebSocket over TLS (on the
// AWS server Caddy does the TLS, and the race server sees plain ws:// from 127.0.0.1).
//
// Our own small implementation (no extra Qt module): the handshake, binary / ping / pong /
// close frames, fragmented messages, client masking, and size limits.

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QUrl>

class QTcpSocket;

namespace net {

class Link : public QObject
{
    Q_OBJECT
public:
    explicit Link(QObject *parent = nullptr);
    ~Link() override;

    // server side: an accepted socket (this link takes it over); a WebSocket first answers the
    // HTTP upgrade, then opened() follows
    void acceptTcp(QTcpSocket *s);
    void acceptWs(QTcpSocket *s);
    // client side: "host" + port (plain TCP), or a ws:// / wss:// URL
    void connectTcp(const QString &host, quint16 port);
    void connectWs(const QUrl &url);

    void send(const QByteArray &bytes);
    QByteArray take();                   // what has arrived since the last take()
    void close();                        // goes once what was sent is out
    void abort();
    bool isOpen() const { return open; }
    qint64 backlog() const;              // bytes sent but not out yet (a slow connection backs up)
    bool isWebSocket() const { return mode != Tcp; }
    bool isLoopback() const;
    QString clientAddress() const;       // the player's address (behind Caddy: X-Forwarded-For)
    QString errorText() const { return lastError; }
    enum Failure { NoFailure, Refused, NotFound, Other };
    Failure failure() const { return failKind; }

    static constexpr int MAX_MESSAGE = (1 << 20) + 64;   // (a frame the game never needs)
#ifdef __EMSCRIPTEN__
    // the browser: its own WebSocket does the work (emscripten/websocket.h); its callbacks
    // land here (0 open, 1 a binary message, 2 closed, 3 an error)
    void webEvent(int kind, const unsigned char *data, int size);
#endif

signals:
    void opened();                       // connected (and the WebSocket handshake done)
    void readyRead();
    void closed();                       // the other side went, or the connection broke
    void failed();                       // could not connect (errorText() says why)

private:
    enum Mode { Tcp, WsServer, WsClient };
    void attach(QTcpSocket *s);
    void onReadyRead();
    void onConnected();
    void serverHandshake();
    void clientHandshake();
    void parseFrames();
    void sendFrame(int opcode, const QByteArray &payload);
    void protocolError(const QString &why);

    QTcpSocket *sock = nullptr;
    Mode mode = Tcp;
    bool open = false, wasOpen = false, handshaken = false, closing = false;
    QByteArray raw;                      // bytes not yet understood (handshake or a part frame)
    QByteArray received;                 // game bytes for take()
    QByteArray message;                  // a fragmented message so far
    QByteArray wsKey;                    // client: the key it sent
    QUrl url;
    QString forwardedFor, lastError;
    Failure failKind = NoFailure;
#ifdef __EMSCRIPTEN__
    int ws = 0;                          // the browser's WebSocket
    QByteArray urlText;
#endif
};

} // namespace net

#endif // LINK_H
