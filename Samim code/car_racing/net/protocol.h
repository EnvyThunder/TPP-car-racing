#ifndef PROTOCOL_H
#define PROTOCOL_H

// Online multiplayer protocol shared by the game, the room server and the test bots.
//
//   * TCP (GAME_PORT): one connection per player, carrying lobby and race messages.
//     Every message is framed as [quint32 length][quint8 type][payload].
//   * UDP (DISCOVERY_PORT): a joiner broadcasts "TURBO8?CODE" on the local network;
//     the machine hosting room CODE answers "TURBO8!CODE:port". So players only
//     ever type the 4-letter room code - no IP addresses.
//
// The server is authoritative: clients send only their key presses, the server runs
// the race (sim::GameSim) and sends snapshots of the world back 30 times a second.

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>
#include <QtEndian>
#include "simtypes.h"

namespace net {

constexpr quint16 PROTOCOL_VERSION = 1;
constexpr quint16 GAME_PORT        = 47800;   // TCP; a busy port makes the host try the next ones
constexpr quint16 DISCOVERY_PORT   = 47801;   // UDP
constexpr int     SNAPSHOT_EVERY   = 2;       // sim ticks per snapshot: 60 Hz sim -> 30 Hz snapshots
constexpr int     INTERP_DELAY_MS  = 80;      // other cars are drawn this far in the past (smoothness)

enum MsgType : quint8 {
    // client -> server
    C_JOIN = 1,      // version, name, r, g, b, room code ("" = create a new room), map
    C_INPUT,         // seq, input bits
    C_START,         // host: start the race
    C_SET_MAP,       // host: choose the map in the lobby
    C_PING,          // client clock in ms (echoed back to measure ping)
    // server -> client
    S_WELCOME = 64,  // your slot, room code, are you the host
    S_ERROR,         // reason; the server then closes the connection
    S_LOBBY,         // map, players
    S_RACE_START,    // map, your slot, racer names
    S_SNAPSHOT,      // last input seq applied for you + world snapshot
    S_PONG,          // echoed client clock
    S_RACE_OVER      // race finished: back to the lobby
};

struct LobbyPlayer {
    QString name;
    float r = 1, g = 1, b = 1;
    bool host = false;
};

// Same settings on both ends: floats travel as 4 bytes
inline QDataStream &prepare(QDataStream &ds)
{
    ds.setVersion(QDataStream::Qt_6_0);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
    return ds;
}

inline QByteArray frame(MsgType type, const QByteArray &payload = QByteArray())
{
    QByteArray out(5, 0);
    qToBigEndian<quint32>(quint32(payload.size() + 1), out.data());
    out[4] = char(type);
    out += payload;
    return out;
}

// Turns the TCP byte stream back into whole messages
class FrameReader {
public:
    void feed(const QByteArray &bytes) { buf += bytes; }
    bool next(MsgType &type, QByteArray &payload)
    {
        if (buf.size() < 5) return false;
        quint32 len = qFromBigEndian<quint32>(buf.constData());
        if (len == 0 || len > (1u << 20)) { bad = true; buf.clear(); return false; }
        if (quint32(buf.size()) < 4 + len) return false;
        type = MsgType(quint8(buf[4]));
        payload = buf.mid(5, int(len) - 1);
        buf.remove(0, int(4 + len));
        return true;
    }
    bool broken() const { return bad; }
private:
    QByteArray buf;
    bool bad = false;
};

inline quint8 packInput(const sim::PlayerInput &in)
{
    return quint8((in.up ? 1 : 0) | (in.down ? 2 : 0) | (in.left ? 4 : 0) | (in.right ? 8 : 0) | (in.nitro ? 16 : 0));
}

inline sim::PlayerInput unpackInput(quint8 b)
{
    sim::PlayerInput in;
    in.up = b & 1; in.down = b & 2; in.left = b & 4; in.right = b & 8; in.nitro = b & 16;
    return in;
}

} // namespace net

#endif // PROTOCOL_H
