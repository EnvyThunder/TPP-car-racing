// turbo8_server [port] [--ws PORT] [--max-rooms N] [--api URL]
// Dedicated room server: run it on any PC (or a cloud machine) and players host
// and join rooms on it. On the same network they still only need the room code;
// over the internet they type CODE@server-address.
// The internet server on AWS (docs/online/README.md) runs it with --ws 47810 (WebSocket
// connections from 127.0.0.1, passed on by Caddy from wss://<site>/race), --max-rooms 4 and
// --api http://127.0.0.1:3000 (logged-in players: their tokens are checked, results go up).
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QTextStream>
#include "raceserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    quint16 port = net::GAME_PORT, wsPort = 0;
    RaceServer server;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--ws" && i + 1 < args.size()) wsPort = quint16(args[++i].toUInt());
        else if (args[i] == "--max-rooms" && i + 1 < args.size()) server.maxRooms = args[++i].toInt();
        else if (args[i] == "--api" && i + 1 < args.size()) server.apiBase = args[++i];
        else if (args[i] == "--rtc" && i + 1 < args.size()) server.rtcPort = quint16(args[++i].toUInt());
        else if (args[i] == "--public-ip" && i + 1 < args.size()) server.publicIp = args[++i];
        else port = quint16(args[i].toUInt());
    }
    QObject::connect(&server, &RaceServer::log, [&out](const QString &line) { out << line << Qt::endl; });
    if (!server.start(port)) return 1;
    if (wsPort && !server.startWs(wsPort)) return 1;

    out << "Addresses of this machine:" << Qt::endl;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
            out << "  " << a.toString() << Qt::endl;
    out << "Players: ONLINE -> HOST / JOIN. From another network join with CODE@<address>." << Qt::endl;
    return app.exec();
}
