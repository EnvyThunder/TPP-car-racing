#ifndef RACECLIENT_H
#define RACECLIENT_H

// Connection from one player's game to a RaceServer (the host's game or the
// dedicated server). Finds rooms by code on the local network, keeps the lobby
// state and hands race snapshots to the game.

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>
#include <deque>
#include "protocol.h"
#include "link.h"

class RaceClient : public QObject
{
    Q_OBJECT
public:
    explicit RaceClient(QObject *parent = nullptr);
    ~RaceClient() override;

    // "K7Q2" = find the room on this network (then on the internet server, if one is set);
    // "K7Q2@host", "K7Q2@host:port" or "K7Q2@wss://host/race" = connect directly
    void join(const QString &codeOrAddress, const QString &name, float r, float g, float b);
    // Create a new room on the server at host:port (the host's own game uses 127.0.0.1), or at
    // a ws:// / wss:// URL (the internet server; port unused)
    void create(const QString &host, quint16 port, const QString &name, float r, float g, float b, int map, int laps);
    void leave();

    void sendInput(quint32 seq, const QByteArray &bits, int want);   // bits: the last inputs, oldest first
    void sendStory(bool skipForAll);           // the race's story scene: watched it / skip it for everyone
    bool autoStory = false;                    // (test bots: say "watched" as soon as a race starts)
    void requestStart();
    void requestAddCpu();      // host: add a CPU racer (random level 1-5)
    void requestRemoveCpu();   // host: remove the last CPU racer
    void requestName(const QString &name);   // change my name (in the lobby)
    void requestMap(int map, int laps);    // host: map and number of laps

    QString internetUrl;                       // the internet server (wss://...); empty: none
    QString token;                             // your account's session (sent when joining; empty: a guest)

    // Lobby / race state as last told by the server
    QString code;
    int slot = -1;
    bool isHost = false;
    int lobbyMap = 0;
    int lobbyLaps = 3;
    QList<net::LobbyPlayer> players;
    int pingMs = -1;
    bool fastActive() const;                   // inputs and snapshots go by the fast lane (UDP) now

signals:
    void status(const QString &text);
    void welcomed();
    void lobbyChanged();
    // world = the host's track fingerprint (GameSim::worldChecksum) to compare with ours
    void raceStarted(int map, int laps, int slot, const QStringList &names, quint32 world);
    void snapshotArrived(quint32 lastSeq, int queued, const QByteArray &body);
    void extrasArrived(const QByteArray &body);   // obstacle changes + effects for a fast-lane snapshot
    void raceOver();
    void storyEnded();                         // everyone may race: the countdown starts
    void failed(const QString &reason);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onLinkFailed();
    void onDiscoveryReply();
    void onDiscoveryTimer();
    void onPingTimer();
    void onConnectTimeout();
    void onFastRead();
    void onFastTimer();

private:
    void connectTo(const QString &host, quint16 port);
    void send(net::MsgType type, const QByteArray &payload = QByteArray());
    void dispatch(net::MsgType type, const QByteArray &payload);
    void reset();

    net::Link *link = nullptr;                 // the connection (TCP or WebSocket), made per connect
    QUdpSocket udp;
    QTimer discoverTimer, pingTimer, connectTimer;
    QElapsedTimer pingClock;
    net::FrameReader reader;
    int discoverTries = 0;
    bool leaving = false;
    bool joined = false;
    QString myName, wantCode;
    float myR = 1, myG = 1, myB = 1;
    int wantMap = 0, wantLaps = 3;
    int fakeLagMs = 0;
    // lag simulator queues: messages leave them in the order they came in
    std::deque<std::pair<net::MsgType, QByteArray>> lagIn;
    std::deque<QByteArray> lagOut;
    QString connectHost;
    // the fast lane (net/protocol.h)
    QUdpSocket fastSock;
    QTimer fastTimer;
    QHostAddress fastHost;
    quint16 fastPort = 0;
    quint64 fastToken = 0;
    bool fastOk = false;                       // the server answers on it
    qint64 fastLastRx = -1;
    qint32 lastSnapTick = -1;                  // (an older snapshot arriving late is thrown away)
    void sendFast(quint8 type, const QByteArray &payload);
    void handleSnapshot(const QByteArray &payload);
#ifdef __EMSCRIPTEN__
public:
    void rtcEvent(int kind, const unsigned char *data, int size);   // from the page's WebRTC (net/raceclient.cpp)
private:
    bool rtcWeb = false;                       // the fast lane is the browser's data channel
#endif
};

#endif // RACECLIENT_H
