#ifndef RACESERVER_H
#define RACESERVER_H

// Authoritative room server. Runs inside the host's game ("HOST A RACE") or on its
// own as the dedicated server (server/). Each room has a 4-letter code, up to 8
// players, a lobby, and its own sim::GameSim that is the single source of truth.

#include <QObject>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <deque>
#include "gamesim.h"
#include "protocol.h"

class QTcpSocket;

class RaceServer : public QObject
{
    Q_OBJECT
public:
    explicit RaceServer(QObject *parent = nullptr);
    ~RaceServer() override;

    // Listens on port (or the next free one of the 10 after it). Returns false if none was free.
    bool start(quint16 port = net::GAME_PORT);
    quint16 port() const { return tcp.serverPort(); }
    int roomCount() const { return rooms.size(); }

signals:
    void log(const QString &line);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onDiscovery();
    void onTick();

private:
    struct Player {
        QTcpSocket *sock = nullptr;      // null once disconnected (car stays in the race)
        net::FrameReader reader;
        QString name;
        float r = 1, g = 1, b = 1;
        std::deque<std::pair<quint32, sim::PlayerInput>> inputs;   // queued, one used per tick
        sim::PlayerInput current;
        quint32 lastSeq = 0;             // seq of the input applied most recently
    };
    struct Room {
        QString code;
        int map = 0;
        bool racing = false;
        int overTicks = 0;               // ticks since the race finished
        QList<Player *> players;         // index == car slot during a race
        Player *host = nullptr;
        sim::GameSim sim;
        std::vector<sim::SimEvent> pendingEvents;
        std::vector<sim::Obstacle> lastSentObstacles;
        int steps = 0;
    };

    void handle(Player *p, net::MsgType type, const QByteArray &payload);
    void join(Player *p, QDataStream &in);
    void startRace(Room *room);
    void endRace(Room *room);
    void removePlayer(Player *p);
    void sendLobby(Room *room);
    void send(Player *p, net::MsgType type, const QByteArray &payload = QByteArray());
    void fail(Player *p, const QString &reason);
    QString newCode() const;

    QTcpServer tcp;
    QUdpSocket udp;
    QTimer timer;
    QElapsedTimer clock;
    double accumulator = 0.0;
    QHash<QString, Room *> rooms;
    QHash<QTcpSocket *, Player *> bySocket;
    QHash<Player *, Room *> roomOf;
};

#endif // RACESERVER_H
