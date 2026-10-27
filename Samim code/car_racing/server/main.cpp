// turbo8_server [port]
// Dedicated room server: run it on any PC (or a cloud machine) and players host
// and join rooms on it. On the same network they still only need the room code;
// over the internet they type CODE@server-address.
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QTextStream>
#include "raceserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    quint16 port = net::GAME_PORT;
    if (argc > 1) port = quint16(QString(argv[1]).toUInt());

    RaceServer server;
    QObject::connect(&server, &RaceServer::log, [&out](const QString &line) { out << line << Qt::endl; });
    if (!server.start(port)) return 1;

    out << "Addresses of this machine:" << Qt::endl;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
            out << "  " << a.toString() << Qt::endl;
    out << "Players: ONLINE -> HOST / JOIN. From another network join with CODE@<address>." << Qt::endl;
    return app.exec();
}
