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
#ifndef __EMSCRIPTEN__
#include <QNetworkInterface>
#else
#include <QHostAddress>
#include <QList>
#endif
#include <QString>
#include <QtEndian>
#include "simtypes.h"

namespace net {

#ifdef __EMSCRIPTEN__
// (a web page can't see the network adapters: in the browser there are no Wi-Fi rooms)
struct LanEntry {
    QHostAddress ip() const { return QHostAddress(); }
    QHostAddress broadcast() const { return QHostAddress(); }
    int prefixLength() const { return 32; }
};
inline QList<LanEntry> lanEntries() { return {}; }
#else
// Virtual adapters (WSL, Hyper-V, VMs, VPNs) are not the Wi-Fi friends are on
inline bool isVirtualAdapter(const QNetworkInterface &nif)
{
    static const char *VIRTUAL[] = {"vEthernet", "VirtualBox", "VMware", "WSL", "Hyper-V",
                                    "Bluetooth", "TAP", "Tailscale", "ZeroTier", "Npcap"};
    for (const char *v : VIRTUAL)
        if (nif.humanReadableName().contains(v, Qt::CaseInsensitive) || nif.name().contains(v, Qt::CaseInsensitive))
            return true;
    return false;
}

// Real, connected network adapters (Wi-Fi first) with an IPv4 address
inline QList<QNetworkAddressEntry> lanEntries()
{
    QList<QNetworkAddressEntry> wifi, other;
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        const auto f = nif.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack))
            continue;
        if (isVirtualAdapter(nif)) continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLinkLocal())
                (nif.type() == QNetworkInterface::Wifi ? wifi : other).append(e);
    }
    return wifi + other;
}
#endif

constexpr quint16 PROTOCOL_VERSION = 22;   // 2: same track everywhere + fingerprint; 3: CPU racers; 4: wide road;
                                           // 5: rooms of 6, names changed in the lobby; 6: circuits with laps;
                                           // 7: hills and banking; 8: jumps (car height in snapshots);
                                           // 9: tunnels, bridges, the raised highway; 10: shortcuts;
                                           // 11: the finished circuits (the track changed)
                                           // 12: Sunset Valley and Coastal Highway are sprints;
                                           // 13: crashes (health, wrecks, ghosting) in snapshots
                                           // 14: the L boost (input bit 32), side ramps, the story
                                           //     scene before each race (C_STORY / S_STORY_END)
                                           // 15: CPU minds (personality, mood, tactic, skill) in
                                           //     snapshots; no CPU levels; 2 laps; top-4 finish
                                           // 16: nitro cans back in 1 s with longer boosts per
                                           //     taker; stronger jumps per jumper (counters sent)
                                           // 17: the race director's mode and rivals in snapshots
                                           // 18: named drivers (roster) and taunts
                                           // 19: evolved brains (which one, who drives) in snapshots
                                           // 20: the shortcut is closed to the top 3 (flag in snapshots)
                                           // 21: WebSocket links (net/link.h); C_JOIN ends with an account token
                                           // 22: more ramps and obstacles (tyres, concrete), gentler landings
constexpr quint16 GAME_PORT        = 47800;   // TCP; a busy port makes the host try the next ones
constexpr quint16 DISCOVERY_PORT   = 47801;   // UDP
constexpr int     SNAPSHOT_EVERY   = 2;       // sim ticks per snapshot: 60 Hz sim -> 30 Hz snapshots
constexpr int     INTERP_DELAY_MS  = 80;      // other cars are drawn this far in the past (smoothness)

// Player names: up to MAX_NAME of the characters the game's pixel font can show
constexpr int MAX_NAME = 16;
inline QString cleanName(const QString &raw)
{
    QString out;
    for (QChar c : raw.toUpper())
        if (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.')
            && out.size() < MAX_NAME)
            out += c;
    return out.simplified();
}

enum MsgType : quint8 {
    // client -> server
    C_JOIN = 1,      // version, name, r, g, b, room code ("" = create a new room), map, laps
    C_INPUT,         // seq, input bits
    C_START,         // host: start the race
    C_SET_MAP,       // host: choose the map and the number of laps in the lobby
    C_PING,          // client clock in ms (echoed back to measure ping)
    C_ADD_BOT,       // host: add a CPU racer (random level 1-5) to a free seat
    C_REMOVE_BOT,    // host: remove the last CPU racer
    C_SET_NAME,      // new name for yourself (in the lobby)
    C_STORY,         // the race's story scene: 0 = I watched it, 1 = skip it for everyone
    // server -> client
    S_WELCOME = 64,  // your slot, room code, are you the host
    S_ERROR,         // reason; the server then closes the connection
    S_LOBBY,         // map, laps, players
    S_RACE_START,    // map, laps, your slot, racer names, track fingerprint
    S_SNAPSHOT,      // last input seq applied for you + world snapshot
    S_PONG,          // echoed client clock
    S_RACE_OVER,     // race finished: back to the lobby
    S_STORY_END      // the story scene is over for everyone (skipped or watched): the countdown starts
};

struct LobbyPlayer {
    QString name;
    float r = 1, g = 1, b = 1;
    bool host = false;
    int cpuLevel = 0;      // 0 = a person; 1..5 = CPU racer and its difficulty
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
    return quint8((in.up ? 1 : 0) | (in.down ? 2 : 0) | (in.left ? 4 : 0) | (in.right ? 8 : 0) | (in.nitro ? 16 : 0) | (in.boost ? 32 : 0));
}

inline sim::PlayerInput unpackInput(quint8 b)
{
    sim::PlayerInput in;
    in.up = b & 1; in.down = b & 2; in.left = b & 4; in.right = b & 8; in.nitro = b & 16; in.boost = b & 32;
    return in;
}

} // namespace net

#endif // PROTOCOL_H
