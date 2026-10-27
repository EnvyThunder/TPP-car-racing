#ifndef RACECLIENT_H
#define RACECLIENT_H

// Connection from one player's game to a RaceServer (the host's game or the
// dedicated server). Finds rooms by code on the local network, keeps the lobby
// state and hands race snapshots to the game.

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>
#include "protocol.h"

class RaceClient : public QObject
{
    Q_OBJECT
public:
    explicit RaceClient(QObject *parent = nullptr);
    ~RaceClient() override;

    // "K7Q2" = find the room on this network; "K7Q2@host" or "K7Q2@host:port" = connect directly
    void join(const QString &codeOrAddress, const QString &name, float r, float g, float b);
    // Create a new room on the server at host:port (the host's own game uses 127.0.0.1)
    void create(const QString &host, quint16 port, const QString &name, float r, float g, float b, int map);
    void leave();

    void sendInput(quint32 seq, const sim::PlayerInput &in);
    void requestStart();
    void requestMap(int map);

    // Lobby / race state as last told by the server
    QString code;
    int slot = -1;
    bool isHost = false;
    int lobbyMap = 0;
    QList<net::LobbyPlayer> players;
    int pingMs = -1;

signals:
    void status(const QString &text);
    void welcomed();
    void lobbyChanged();
    void raceStarted(int map, int slot, const QStringList &names);
    void snapshotArrived(quint32 lastSeq, const QByteArray &body);
    void raceOver();
    void failed(const QString &reason);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError);
    void onDiscoveryReply();
    void onDiscoveryTimer();
    void onPingTimer();

private:
    void connectTo(const QString &host, quint16 port);
    void send(net::MsgType type, const QByteArray &payload = QByteArray());
    void dispatch(net::MsgType type, const QByteArray &payload);
    void reset();

    QTcpSocket sock;
    QUdpSocket udp;
    QTimer discoverTimer, pingTimer;
    QElapsedTimer pingClock;
    net::FrameReader reader;
    int discoverTries = 0;
    bool leaving = false;
    bool joined = false;
    QString myName, wantCode;
    float myR = 1, myG = 1, myB = 1;
    int wantMap = 0;
    int fakeLagMs = 0;
};

#endif // RACECLIENT_H
