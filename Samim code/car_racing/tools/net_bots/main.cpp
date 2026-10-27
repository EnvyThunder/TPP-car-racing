// Test bots for online rooms: each bot is a normal network client (exactly what a
// player's game does) whose "keyboard" is the CPU driver from the simulation.
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <memory>
#include <vector>
#include "raceclient.h"
#include "gamesim.h"

struct Bot {
    RaceClient client;
    sim::GameSim sim;           // local copy of the race, rebuilt from snapshots
    sim::PlayerInput input;
    quint32 seq = 0;
    bool racing = false;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 2) {
        out << "usage: net_bots CODE[@host[:port]] [count]   - bots join an existing room\n"
               "       net_bots NEW@host[:port] [count] [map] - bot 1 creates a room, the rest join,\n"
               "                                              the race runs and the results are printed"
            << Qt::endl;
        return 1;
    }
    QString where = argv[1];
    const bool selfHosted = where.toUpper().startsWith("NEW");
    const int count = std::clamp((argc > 2) ? QString(argv[2]).toInt() : 7, 1, selfHosted ? 8 : 7);
    const int map = (argc > 3) ? QString(argv[3]).toInt() : 0;
    QString hostAddr = "127.0.0.1";
    quint16 hostPort = net::GAME_PORT;
    if (selfHosted && where.contains('@')) {
        QString a = where.mid(where.indexOf('@') + 1);
        int colon = a.lastIndexOf(':');
        if (colon > 0) { hostPort = quint16(a.mid(colon + 1).toUInt()); a = a.left(colon); }
        hostAddr = a;
    }

    static const float COL[8][3] = {{1.0f, 0.75f, 0.1f}, {0.2f, 0.85f, 0.35f}, {0.85f, 0.3f, 0.95f},
                                    {0.1f, 0.85f, 0.9f}, {0.95f, 0.5f, 0.15f}, {0.92f, 0.92f, 0.95f},
                                    {0.6f, 0.4f, 0.2f}, {0.9f, 0.15f, 0.2f}};
    std::vector<std::unique_ptr<Bot>> bots;
    for (int i = 0; i < count; ++i) {
        auto bot = std::make_unique<Bot>();
        Bot *b = bot.get();
        QObject::connect(&b->client, &RaceClient::failed, [&out, i](const QString &why) {
            out << "bot" << i + 1 << ": " << why << Qt::endl;
        });
        QObject::connect(&b->client, &RaceClient::welcomed, [&out, b, i]() {
            out << "bot" << i + 1 << " joined room " << b->client.code << " as slot " << b->client.slot << Qt::endl;
        });
        QObject::connect(&b->client, &RaceClient::raceStarted, [b, &out, i](int map, int, const QStringList &names, quint32 world) {
            b->sim.setMap(map);
            b->sim.startRace(sim::GameSim::ModeOnline, int(names.size()));
            if (b->sim.worldChecksum() != world)
                out << "bot" << i + 1 << ": TRACK DIFFERS FROM THE HOST!" << Qt::endl;
            else if (i == 0)
                out << "track fingerprint matches the host: " << Qt::hex << world << Qt::dec << Qt::endl;
            b->seq = 0;
            b->racing = true;
        });
        QObject::connect(&b->client, &RaceClient::raceOver, [b]() { b->racing = false; });
        QObject::connect(&b->client, &RaceClient::snapshotArrived, [b](quint32, const QByteArray &body) {
            QDataStream in(body);
            net::prepare(in);
            std::vector<sim::SimEvent> evs;
            b->sim.readSnapshot(in, evs);
            int s = b->client.slot;
            if (s >= 0 && s < int(b->sim.getCars().size()))
                b->input = b->sim.computeAIInput(b->sim.getCars()[s], s, false);
        });
        if (!selfHosted) {
            // stagger the joins a little, like real players
            QTimer::singleShot(150 * i, [b, where, i]() {
                b->client.join(where, QString("BOT%1").arg(i + 1), COL[i][0], COL[i][1], COL[i][2]);
            });
        }
        bots.push_back(std::move(bot));
    }

    if (selfHosted) {
        // bot 1 creates the room; once it has a code the others join it by that code
        Bot *h = bots[0].get();
        QObject::connect(&h->client, &RaceClient::welcomed, [&, h]() {
            out << "ROOM CODE " << h->client.code << Qt::endl;
            for (int i = 1; i < count; ++i) {
                Bot *b = bots[i].get();
                QString to = QString("%1@%2:%3").arg(h->client.code, hostAddr).arg(hostPort);
                QTimer::singleShot(150 * i, [b, to, i]() {
                    b->client.join(to, QString("BOT%1").arg(i + 1), COL[i][0], COL[i][1], COL[i][2]);
                });
            }
        });
        QObject::connect(&h->client, &RaceClient::lobbyChanged, [&, h]() {
            if (!h->racing && h->client.players.size() == count && h->client.isHost) {
                out << "all " << count << " in the lobby - starting" << Qt::endl;
                h->client.requestStart();
            }
        });
        QObject::connect(&h->client, &RaceClient::raceOver, [&, h]() {
            const auto &cars = h->sim.getCars();
            out << "RESULTS" << Qt::endl;
            for (int rank = 1; rank <= int(cars.size()); ++rank)
                for (int i = 0; i < int(cars.size()); ++i)
                    if (h->sim.rankOf(i) == rank)
                        out << "  " << rank << ". BOT" << i + 1 << "  "
                            << (cars[i].finished ? QString::number(cars[i].finishTime, 'f', 2) + " s" : QString("DNF")) << Qt::endl;
            out << "ping of bot 1: " << h->client.pingMs << " ms" << Qt::endl;
            QCoreApplication::quit();
        });
        h->client.create(hostAddr, hostPort, "BOT1", COL[0][0], COL[0][1], COL[0][2], map);
    }

    QTimer inputTimer;                                   // send keys at 60 Hz, like the game
    QObject::connect(&inputTimer, &QTimer::timeout, [&bots]() {
        for (auto &b : bots)
            if (b->racing) b->client.sendInput(++b->seq, b->input);
    });
    inputTimer.setTimerType(Qt::PreciseTimer);
    inputTimer.start(16);
    return app.exec();
}
