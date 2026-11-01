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
#include <QThread>
#include <atomic>
#include <deque>
#include "gamesim.h"
#include "protocol.h"
#include "link.h"

class QTcpSocket;
class QNetworkAccessManager;

class RaceServer : public QObject
{
    Q_OBJECT
public:
    explicit RaceServer(QObject *parent = nullptr);
    ~RaceServer() override;

    // Listens on port (or the next free one of the 10 after it). Returns false if none was free.
    bool start(quint16 port = net::GAME_PORT);
    quint16 port() const { return tcp.serverPort(); }
    // The internet server (docs/online/README.md): WebSocket connections on this port too
    // (127.0.0.1 only: on AWS Caddy passes wss://<site>/race to it); at most maxRooms rooms
    // (0: no limit) and MAX_PER_ADDRESS connections from one address; apiBase = the accounts
    // API on the same machine (players' tokens are checked there, and their results go up)
    bool startWs(quint16 wsPort, bool anyAddress = false);
    quint16 wsPort() const;
    int maxRooms = 0;
    QString apiBase;
    static constexpr int MAX_PER_ADDRESS = 8;
    int roomCount() const { return rooms.size(); }
    // Join attempts that reached this PC from other machines (room searches + connections).
    // Stays 0 while a friend is trying to join => a firewall or the Wi-Fi is blocking them.
    int remoteAttempts() const { return remoteSeen; }
    // measurements for the scripted online test (read from the game's thread)
    std::atomic<int> statMaxGapMs{0}, statStarved{0}, statDropped{0};

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
        net::Link *sock = nullptr;       // null once disconnected (car stays in the race)
        quint64 id = 0;
        QString token, account;          // a logged-in player: their session, and the username it proved
        net::FrameReader reader;
        QString name;
        float r = 1, g = 1, b = 1;
        std::deque<std::pair<quint32, sim::PlayerInput>> inputs;   // queued, each used exactly once
        float budget = 0.0f;             // inputs this player may still use (stops "fast forward")
        quint32 lastSeq = 0;             // seq of the input applied most recently
        int cpuLevel = 0;                // CPU racer (driven by the server) with difficulty 1..5
        bool isCpu() const { return cpuLevel > 0; }
    };
    struct Room {
        QString code;
        int map = 0;
        int laps = 3;
        bool racing = false;
        int overTicks = 0;               // ticks since the race finished
        QList<Player *> players;         // index == car slot during a race
        Player *host = nullptr;
        sim::GameSim sim;
        std::vector<sim::SimEvent> pendingEvents;
        std::vector<sim::Obstacle> lastSentObstacles;
        int steps = 0;
        // the story scene before the race: the race waits until every person in the room has
        // watched it, one of them skips it for everyone, or STORY_MAX ticks have passed
        bool storyHold = false;
        int storyTicks = 0;
        QList<Player *> storyWatched;
    };
    static constexpr int STORY_MAX = 75 * 60;
    // the player model: each person's profile, built by the server from their races here
    // (a profile sent by a player's own game is never trusted)
    QHash<QString, sim::PlayerProfile> profiles;
    void endStory(Room *room);

    void handle(Player *p, net::MsgType type, const QByteArray &payload);
    void join(Player *p, QDataStream &in);
    void startRace(Room *room);
    void endRace(Room *room);
    void removePlayer(Player *p);
    // a name nobody else in the room has (wanted + a number if it is taken); an empty
    // wanted name gives the default CAR1, CAR2, ...
    QString uniqueName(Room *room, const QString &wanted, const Player *self) const;
    void addCpu(Room *room);
    void removeCpu(Room *room);
    void pickFreeColour(Room *room, Player *p);
    void sendLobby(Room *room);
    void send(Player *p, net::MsgType type, const QByteArray &payload = QByteArray());
    void fail(Player *p, const QString &reason);
    QString newCode() const;

    QTcpServer tcp;
    QTcpServer ws;
    QUdpSocket udp;
    QNetworkAccessManager *nam = nullptr;
    quint64 nextId = 0;
    QHash<QString, int> perAddress;
    void accept(QTcpSocket *sock, bool webSocket);
    void checkAccount(Player *p);
    void uploadResult(Player *p, const QJsonObject &race);
    Player *playerById(quint64 id) const;
    QTimer timer;
    QElapsedTimer clock;
    double accumulator = 0.0;
    std::atomic<int> remoteSeen{0};
    QHash<QString, Room *> rooms;
    QHash<net::Link *, Player *> bySocket;
    QHash<Player *, Room *> roomOf;
};

// The room server of a player who hosts, on a thread of its own: its 60 Hz race and
// the snapshots keep a steady beat even while the host's game is busy drawing a frame.
class HostedServer
{
public:
    ~HostedServer();                     // stops the server, then its thread
    bool start();                        // false: no free port on this PC
    quint16 port() const { return listenPort; }
    const RaceServer *server() const { return srv; }   // only its thread-safe counters
private:
    QThread thread;
    RaceServer *srv = nullptr;           // lives on `thread`
    quint16 listenPort = 0;
};

#endif // RACESERVER_H
