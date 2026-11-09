// Online multiplayer: the "ONLINE MULTIPLAYER (6P)" screens and the networked race.
//
// One player picks HOST A RACE: their game starts a RaceServer and shows a 4-letter
// room code. Friends pick JOIN WITH A CODE and type it; their game finds the host on
// the local network. The host's server runs the real race; every game sends only its
// key presses and draws the snapshots it gets back:
//   * my own car is predicted locally (no input lag) and corrected by each snapshot
//   * the other cars are drawn INTERP_DELAY_MS in the past, blended between snapshots
#include "glwidget.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "raceserver.h"
#include "raceclient.h"
#include "protocol.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#ifndef __EMSCRIPTEN__
#include <QNetworkInterface>
#endif
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include "gl1redirect.h"   // (the drawing goes through gl1shim: OpenGL ES 2 / WebGL)

namespace {

const float PI_F = 3.14159265f;

const char *ordinal(int r)
{
    static const char *S[8] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH"};
    return S[std::clamp(r, 1, 8) - 1];
}

void fillRect(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

void frameRect(float x0, float y0, float x1, float y1, float t)
{
    fillRect(x0, y0, x1, y0 + t); fillRect(x0, y1 - t, x1, y1);
    fillRect(x0, y0, x0 + t, y1); fillRect(x1 - t, y0, x1, y1);
}

float lerpAngle(float a, float b, float t)
{
    float d = b - a;
    while (d > PI_F) d -= 2.0f * PI_F;
    while (d < -PI_F) d += 2.0f * PI_F;
    return a + d * t;
}

} // namespace

// ===========================================================================
//  Joining, hosting, leaving
// ===========================================================================
QString GLWidget::savedName() const
{
    // the name you gave yourself in a lobby before (kept on this computer); empty = the
    // room names you CAR1, CAR2, ... Scripted tests never use or change it.
    if (!netTest.isEmpty()) return QString();
    if (!accountUser.isEmpty()) return accountUser;      // logged in: your username
    return net::cleanName(QSettings("TPP", "CarRacing").value("online/name").toString());
}

QString GLWidget::lanAddress()
{
    // the address friends on the same Wi-Fi/hotspot can reach (Wi-Fi adapter first)
    const auto entries = net::lanEntries();
    return entries.isEmpty() ? QString() : entries.first().ip().toString();
}

void GLWidget::openOnline()
{
#ifdef __EMSCRIPTEN__
    // Online races are for the desktop game (smooth over the internet; a browser is not): the
    // site's download page opens in a new tab and this game carries on
    EM_ASM({ window.open('/#download', '_blank'); });
    return;
#endif
    if (!net) {
        net = new RaceClient(this);
        netClock.start();
        connect(net, &RaceClient::status, this, [this](const QString &s) { netMessage = s; update(); });
        connect(net, &RaceClient::welcomed, this, [this]() { netScreen = NetScreen::Lobby; netMessage.clear(); });
        connect(net, &RaceClient::lobbyChanged, this, [this]() {
            if (netScreen == NetScreen::Busy) netScreen = NetScreen::Lobby;
        });
        connect(net, &RaceClient::raceStarted, this, [this](int map, int laps, int slot, const QStringList &names, quint32 world) {
            selectedMapPreset = map;
            simEngine.setMap(map);
            simEngine.setLaps(laps);
            simEngine.startRace(sim::GameSim::ModeOnline, int(names.size()));
            trackMismatch = (simEngine.worldChecksum() != world);
            std::printf("WORLD %s (host %08x)\n", simEngine.worldParts().c_str(), world); std::fflush(stdout);   // (diagnosis)
            mySlot = slot;
            racerNames = names;
            nameEditing = false;
            view1 = CarView();
            view2 = CarView();
            for (CarFx &f : carFx) f = CarFx();
            for (float &t : nitroBannerTimer) t = 0.0f;
            skidMarks.clear();
            particles.clear();
            netEvents.clear();
            history.clear();
            sentInputs.clear();
            presentCars.clear(); presVx.clear(); presVz.clear(); presVs.clear();
            inputSeq = 0;
            corrX = corrZ = 0.0f;
            netInterpTarget = netInterpMs = net::INTERP_DELAY_MS;
            netJitter = 0.0; netLastSnapAt = netLastCompose = -1;
            netRenderT = netLastComposeNs = -1.0;
            statStalls = statLate = 0;
            if (slot >= 0 && slot < int(simEngine.getCars().size())) predicted = predictedPrev = simEngine.getCars()[slot];
            keys.clear();
            simAccumulator = 0.0f;
            elapsedTimer.restart();
            netScreen = NetScreen::Race;
            beginOnlineStory();                          // the story scene first (the server waits)
        });
        connect(net, &RaceClient::storyEnded, this, [this]() { endOnlineStory(); });
        connect(net, &RaceClient::snapshotArrived, this, &GLWidget::onNetSnapshot);
        connect(net, &RaceClient::extrasArrived, this, [this](const QByteArray &body) {
            if (!isOnline()) return;
            QDataStream in(body);
            net::prepare(in);
            std::vector<sim::SimEvent> evs;
            simEngine.readExtras(in, evs);
            netEvents.insert(netEvents.end(), evs.begin(), evs.end());
        });
        connect(net, &RaceClient::raceOver, this, [this]() {
            if (story != StoryScreen::Off) endOnlineStory();
            netStoryHold = false;
            netScreen = NetScreen::Lobby;
            simEngine.setState(State::Menu);
            keys.clear();
        });
        connect(net, &RaceClient::failed, this, [this](const QString &why) {
            // leave on the next turn of the event loop (we are inside the client's own signal)
            QTimer::singleShot(0, this, [this, why]() { netLeave(why); });
        });
    }
    netScreen = NetScreen::Choose;
    netChoice = 0;
    netMessage.clear();
    joinText.clear();
}

// The internet race server runs beside the accounts API (docs/online/README.md): the same site,
// over a WebSocket at /race
QString GLWidget::raceUrl() const
{
    const QString env = qEnvironmentVariable("CAR_RACING_RACE");
    if (!env.isEmpty()) return env;
    QString base = apiBase();
    if (base.startsWith("https://")) base = "wss://" + base.mid(8);
    else if (base.startsWith("http://")) base = "ws://" + base.mid(7);
    return base + "/race";
}

void GLWidget::netHostInternet()
{
    float r, g, b;
    myCarColor(r, g, b);
    net->token = accountToken;                           // (logged in: the server knows you)
    netScreen = NetScreen::Busy;
    netMessage = "CREATING A ROOM ON THE INTERNET SERVER...";
    net->create(raceUrl(), 0, savedName(), r, g, b, selectedMapPreset, simEngine.getLaps());
}

void GLWidget::netHost()
{
#ifdef __EMSCRIPTEN__
    netMessage = "IN THE BROWSER, HOST AN ONLINE RACE";   // (a web page can't be a server)
    return;
#endif
    float r, g, b;
    myCarColor(r, g, b);
    // CAR_RACING_SERVER=host[:port] hosts the room on a dedicated server instead of this PC
    QString server = qEnvironmentVariable("CAR_RACING_SERVER");
    if (!server.isEmpty()) {
        quint16 port = net::GAME_PORT;
        int colon = server.lastIndexOf(':');
        if (colon > 0) { port = quint16(server.mid(colon + 1).toUInt()); server = server.left(colon); }
        netScreen = NetScreen::Busy;
        net->create(server, port, savedName(), r, g, b, selectedMapPreset, simEngine.getLaps());
        return;
    }
    if (!hostServer) {
        hostServer = new HostedServer;
        if (!hostServer->start()) {
            delete hostServer;
            hostServer = nullptr;
            netMessage = "COULD NOT START A SERVER ON THIS PC";
            return;
        }
    }
    netScreen = NetScreen::Busy;
    netMessage = "CREATING ROOM...";
    net->create("127.0.0.1", hostServer->port(), savedName(), r, g, b, selectedMapPreset, simEngine.getLaps());
}

void GLWidget::netJoin()
{
    if (joinText.trimmed().isEmpty()) { netMessage = "TYPE THE ROOM CODE FIRST"; return; }
    float r, g, b;
    myCarColor(r, g, b);
    netScreen = NetScreen::Busy;
    net->internetUrl = raceUrl();                        // (not on this Wi-Fi: the internet server is asked next)
    net->token = accountToken;
    net->join(joinText, savedName(), r, g, b);
}

void GLWidget::netLeave(const QString &message)
{
    if (net) net->leave();
    delete hostServer;                     // stops the server's thread
    hostServer = nullptr;
    if (isOnline() || simEngine.getGameMode() == sim::GameSim::ModeOnline) simEngine.setState(State::Menu);
    netScreen = NetScreen::Choose;
    netMessage = message;
    nameEditing = false;
    keys.clear();
}

// ===========================================================================
//  The networked race
// ===========================================================================
void GLWidget::onNetSnapshot(quint32 lastSeq, int queued, const QByteArray &body)
{
    if (!isOnline()) return;
    netQueued += (double(queued) - netQueued) * 0.1;
    {
        const qint64 t = netClock.elapsed();
        if (netLastSnapAt >= 0 && t - netLastSnapAt >= 150) ++statStalls;
        netLastSnapAt = t;
    }
    if (!netTest.isEmpty()) {                            // scripted test: how evenly snapshots arrive
        const qint64 t = netClock.elapsed();
        if (statLastSnap >= 0) {
            statSnapGapMax = std::max(statSnapGapMax, double(t - statLastSnap));
            if (t - statLastSnap > 50) ++statSnapLate;
        }
        statLastSnap = t;
        statSnapBytes += body.size();
    }
    QDataStream in(body);
    net::prepare(in);
    std::vector<sim::SimEvent> evs;
    simEngine.readSnapshot(in, evs);
    netEvents.insert(netEvents.end(), evs.begin(), evs.end());

    // Time the snapshots by the server's clock (its tick), not by when they got here: a
    // slow frame here or on the network delivers them unevenly, and the other cars would
    // stutter. serverClockAhead follows the quickest deliveries, and slowly gives way when
    // the network gets slower.
    const auto &cars = simEngine.getCars();
    const double serverMs = simEngine.getTick() * (1000.0 / 60.0);
    const double ahead = serverMs - double(netClock.elapsed());
    // how late this one is against the quickest: the buffer of the other cars follows it - a steady
    // connection shows them sooner, a shaky one a little later but without stops
    const double lateBy = history.empty() ? 0.0 : std::max(0.0, serverClockAhead - ahead);
    netJitter = std::max(lateBy, netJitter * 0.985);     // (peaks fade over about 4 s)
    static const bool buf250 = qEnvironmentVariableIsSet("CAR_RACING_NET_BUF250");   // (test: the bigger buffer)
    netInterpTarget = std::clamp(40.0 + netJitter * 1.1, 60.0, buf250 ? 250.0 : 150.0);   // (other cars no further in the past: clashes line up)
    static const bool fixedBuffer = qEnvironmentVariableIsSet("CAR_RACING_NET_FIXED");   // (test: the old fixed 80 ms)
    if (fixedBuffer) netInterpTarget = 80.0;
    if (lateBy > netInterpMs) ++statLate;
    if (history.empty() || ahead > serverClockAhead) serverClockAhead = ahead;
    else serverClockAhead -= 2.0;
    history.push_back({serverMs, cars});
    while (history.size() > 2 && history.front().t < serverMs - 1000.0) history.pop_front();
    if (mySlot < 0 || mySlot >= int(cars.size())) return;

    // Reconciliation: start from where the server says my car is, then re-apply the
    // inputs it hasn't processed yet. Where the car ends up is my new prediction.
    while (!sentInputs.empty() && sentInputs.front().seq <= lastSeq) sentInputs.pop_front();
    sim::Car p = cars[mySlot];
    p.aiTarget = predicted.aiTarget; p.aiReverse = predicted.aiReverse;     // test autopilot memory
    p.aiCheckTimer = predicted.aiCheckTimer; p.aiCheckProg = predicted.aiCheckProg;
    // While my car is replayed, the other cars move on as they did on the server (at their pace
    // between the last two snapshots) - frozen where this snapshot left them, my replayed car
    // would bump into them again at every step, and the next snapshot would pull it back
    // (the push and pull shook the screen near other cars)
    static const bool frozenReplay = qEnvironmentVariableIsSet("CAR_RACING_NET_FROZENREPLAY");   // (test: the old replay)
    static const bool bumpOn = !qEnvironmentVariableIsSet("CAR_RACING_NET_NOBUMP");   // (test: no bumps on my screen, as before)
    auto &live = simEngine.getCars();
    const std::vector<sim::Car> base = live;
    std::vector<float> vx(base.size(), 0.0f), vz(base.size(), 0.0f), vs(base.size(), 0.0f);
    if (!frozenReplay && history.size() >= 2) {
        const Sample &pa = history[history.size() - 2];
        const float ticks = float((history.back().t - pa.t) / (1000.0 / 60.0));
        if (ticks > 0.5f)
            for (size_t i = 0; i < base.size() && i < pa.cars.size(); ++i) {
                const sim::Car &o = pa.cars[i];
                if (int(i) == mySlot || o.path != base[i].path || base[i].wreck > 0) continue;
                const float dx = (base[i].x - o.x) / ticks, dz = (base[i].z - o.z) / ticks;
                if (dx * dx + dz * dz > 9.0f) continue;     // (a respawn, not a pace)
                vx[i] = dx; vz[i] = dz;
                vs[i] = (base[i].progress - o.progress) / ticks;
            }
    }
    int k = 0;
    for (const SentInput &si : sentInputs) {
        ++k;
        if (!frozenReplay)
            for (size_t i = 0; i < live.size(); ++i) {
                if (int(i) == mySlot || (vx[i] == 0.0f && vz[i] == 0.0f)) continue;
                live[i].x = base[i].x + vx[i] * float(k);
                live[i].z = base[i].z + vz[i] * float(k);
                live[i].s = simEngine.wrapS(base[i].s + vs[i] * float(k));
                live[i].hint = int(live[i].s);
            }
        simEngine.updateCar(p, si.in, mySlot);
        simEngine.predictCollisions(p);
        if (bumpOn)
            for (size_t i = 0; i < live.size(); ++i)
                if (int(i) != mySlot) simEngine.predictBump(p, live[i]);
    }
    presentCars = live;                                  // (where the others are now, by the same rule)
    presVx = vx; presVz = vz; presVs = vs;
    for (size_t i = 0; i < live.size() && i < base.size(); ++i)
        if (int(i) != mySlot) live[i] = base[i];
    simEngine.clearEvents();
    // hide corrections by blending them out over a few frames instead of jumping
    float ex = (predicted.x + corrX) - p.x, ez = (predicted.z + corrZ) - p.z;
    float err2 = ex * ex + ez * ez;
    ++statSnapshots;
    if (err2 > 0.25f) ++statSmallFix;
    statErrSum += std::sqrt(err2);
    if (err2 > 36.0f) ++statSnaps;                       // big: usually a crash the server saw first
    if (err2 < 225.0f) { corrX = ex; corrZ = ez; }       // up to 15 m: glide there over a few frames
    else               { corrX = corrZ = 0.0f; }         // further: jump (respawn-sized error)
    predictedPrev.x += p.x - predicted.x; predictedPrev.z += p.z - predicted.z; predictedPrev.y += p.y - predicted.y;
    predictedPrev.heading += p.heading - predicted.heading;   // (the step before moves with it)
    predicted = p;
}

void GLWidget::netUpdate(float realDt)
{
    const float DT = 1.0f / 60.0f;
    auto &cars = simEngine.getCars();
    // Time stretching: the server keeps a few of my inputs waiting so a late packet doesn't
    // stop my car there (more on a shaky connection). Too few waiting: this game runs up to 2%
    // faster for a moment; too many: up to 2% slower. Nobody can feel 2%.
    // (the server keeps a few of my inputs waiting - more on a shaky connection - so a late
    // packet doesn't stop my car there; this game always runs at exactly 60 steps a second:
    // stretching it added and dropped steps, a jerky camera)
    const int want = std::clamp(int(netJitter / 16.7) + 1, 1, 8);
    static const bool stretchOn = qEnvironmentVariableIsSet("CAR_RACING_NET_STRETCH");   // (test: the old time stretching)
    const double stretch = stretchOn ? 1.0 + std::clamp((double(want) - netQueued) * 0.005, -0.02, 0.02) : 1.0;
    simAccumulator += realDt * float(stretch);
    while (simAccumulator >= DT) {
        simAccumulator -= DT;
        sim::PlayerInput in;                             // online: arrows or WASD both drive
        in.up    = isDown(Qt::Key_Up) || isDown(Qt::Key_W);
        in.down  = isDown(Qt::Key_Down) || isDown(Qt::Key_S);
        in.left  = isDown(Qt::Key_Left) || isDown(Qt::Key_A);
        in.right = isDown(Qt::Key_Right) || isDown(Qt::Key_D);
        in.nitro = isDown(Qt::Key_Return) || isDown(Qt::Key_Enter) || isDown(Qt::Key_Space) || isDown(Qt::Key_N);
        in.boost = isDown(Qt::Key_L);
        if (!netTest.isEmpty() && mySlot < int(cars.size()))
            in = simEngine.computeAIInput(predicted, mySlot, false);   // scripted test: CPU drives
        if (!netTest.isEmpty() && qEnvironmentVariableIsSet("CAR_RACING_NETTEST_RAM") && mySlot < int(cars.size())) {
            // (test: drive into the nearest car ahead, as a player looking for a clash)
            int best = -1; float bestD = 60.0f;
            for (int i = 0; i < int(cars.size()); ++i) {
                if (i == mySlot || cars[i].wreck > 0) continue;
                const float dx = cars[i].x - predicted.x, dz = cars[i].z - predicted.z;
                const float fx = std::sin(predicted.heading), fz = -std::cos(predicted.heading);
                const float along = dx * fx + dz * fz, dist = std::sqrt(dx * dx + dz * dz);
                if (along > -1.0f && dist < bestD) { bestD = dist; best = i; }
            }
            if (best >= 0) {
                const float dx = cars[best].x - predicted.x, dz = cars[best].z - predicted.z;
                const float rx = std::cos(predicted.heading), rz = std::sin(predicted.heading);   // (the car's right)
                const float side = dx * rx + dz * rz;
                in.left = side < -0.3f; in.right = side > 0.3f; in.up = true; in.down = false;
            }
        }
        sentInputs.push_back({++inputSeq, in});
        while (sentInputs.size() > 180) sentInputs.pop_front();
        {                                                // this input and copies of the last ones
            QByteArray bits;
            const size_t from = sentInputs.size() > size_t(net::INPUT_COPIES) ? sentInputs.size() - net::INPUT_COPIES : 0;
            for (size_t k = from; k < sentInputs.size(); ++k) bits.append(char(net::packInput(sentInputs[k].in)));
            net->sendInput(inputSeq, bits, want);
        }

        if (mySlot < int(cars.size())) {                                             // prediction
            predictedPrev = predicted;
            simEngine.updateCar(predicted, in, mySlot);
            simEngine.predictCollisions(predicted);
            static const bool bumpLive = !qEnvironmentVariableIsSet("CAR_RACING_NET_NOBUMP");
            for (size_t i = 0; i < presentCars.size() && i < presVx.size(); ++i) {
                if (int(i) == mySlot) continue;
                sim::Car &o = presentCars[i];
                o.x += presVx[i]; o.z += presVz[i];
                o.s = simEngine.wrapS(o.s + presVs[i]); o.hint = int(o.s);
                if (bumpLive) simEngine.predictBump(predicted, o);
            }
        }
        for (sim::Traffic &t : simEngine.trafficMut()) {                             // traffic between snapshots
            t.s = simEngine.wrapS(t.s + t.speed);
            t.lane += (t.targetLane - t.lane) * 0.06f;
        }
        for (int i = 0; i < int(cars.size()) && i < sim::MAX_RACERS; ++i) updateCarFx(i, cars[i]);
    }
    simEngine.clearEvents();               // prediction makes its own effects; the server's are used
    for (const sim::SimEvent &e : netEvents)                 // (tests: clashes the server saw for my car)
        if (e.carIndex == mySlot) {
            if (e.type == sim::SimEvent::Crash) ++myCrashEvents;
            if (e.type == sim::SimEvent::Damage && e.count >= 0 && e.count < sim::MAX_RACERS) ++myClashes;
        }
    processSimEvents(netEvents);
    netEvents.clear();
    composeOnlineCars();
}

void GLWidget::composeOnlineCars()
{
    auto &cars = simEngine.getCars();
    if (history.empty()) return;
    const qint64 nowMs = netClock.elapsed();
    const float frameS = netLastCompose < 0 ? 0.016f : std::clamp(float(nowMs - netLastCompose) * 0.001f, 0.001f, 0.1f);
    netLastCompose = nowMs;
    // the buffer changes size slowly (more quickly when it must grow), so the cars don't visibly shift
    netInterpMs += std::clamp(netInterpTarget - netInterpMs, -15.0 * double(frameS), 60.0 * double(frameS));
    // The other cars' playback clock runs at real time, measured to the microsecond, and eases
    // towards where it should be (at most 5% faster or slower) instead of jumping each time the
    // estimate of the server's clock moves - a jump shows as all the other cars jerking at once.
    const double nowExact = double(netClock.nsecsElapsed()) * 1e-6;
    const double frameMs = netLastComposeNs < 0 ? 16.7 : std::clamp(nowExact - netLastComposeNs, 0.1, 100.0);
    netLastComposeNs = nowExact;
    const double targetT = nowExact + serverClockAhead - netInterpMs;
    static const bool jumpyClock = qEnvironmentVariableIsSet("CAR_RACING_NET_JUMPYCLOCK");   // (test: the old clock)
    if (jumpyClock || netRenderT < 0 || std::fabs(targetT - netRenderT) > 300.0) netRenderT = targetT;
    else netRenderT += frameMs + std::clamp((targetT - netRenderT) * 0.05, -0.05 * frameMs, 0.05 * frameMs);
    const double renderT = netRenderT;
    ++statComposed;
    if (renderT > history.back().t) ++statStarved;      // nothing newer to show

    // the two snapshots around renderT
    const Sample *a = &history.front(), *b = nullptr;
    for (const Sample &s : history) {
        if (s.t <= renderT) a = &s;
        else { b = &s; break; }
    }
    for (int i = 0; i < int(cars.size()); ++i) {
        if (i == mySlot || i >= int(a->cars.size())) continue;
        sim::Car c = a->cars[i];
        if (b && i < int(b->cars.size()) && b->t > a->t) {
            const sim::Car &n = b->cars[i];
            float f = std::clamp(float((renderT - a->t) / (b->t - a->t)), 0.0f, 1.0f);
            c.x += (n.x - c.x) * f;
            c.z += (n.z - c.z) * f;
            if (c.path != n.path) {                      // changed road between the snapshots
                c.path = n.path; c.s = n.s; c.d = n.d; c.lap = n.lap; c.progress = n.progress;
            } else if (c.path == 1) {                    // on the shortcut
                c.s += (n.s - c.s) * f;
                c.d += (n.d - c.d) * f;
                c.progress += (n.progress - c.progress) * f;
            } else {
                c.d += (n.d - c.d) * f;
                c.progress += (n.progress - c.progress) * f;   // the race order follows the distance raced
                if (simEngine.sprint()) {                // a sprint: metres past the start line
                    c.lap = 0;
                    c.s = c.progress + simEngine.startLine();
                } else {
                    c.lap = int(std::floor(c.progress / simEngine.lapLength()));
                    c.s = c.progress - float(c.lap) * simEngine.lapLength();
                }
            }
            c.hint = int(c.s);
            c.y += (n.y - c.y) * f;
            c.air = n.air;
            c.heading = lerpAngle(c.heading, n.heading, f);
            c.steerVis += (n.steerVis - c.steerVis) * f;
            c.roll += (n.roll - c.roll) * f;
            c.pitch += (n.pitch - c.pitch) * f;
            c.speed += (n.speed - c.speed) * f;
            c.wheelSpin = n.wheelSpin;
        }
        else if (a == &history.back() && history.size() >= 2 && renderT > a->t) {
            // Nothing newer yet (a late update): keep the car driving for up to 0.35 s. On the
            // ground it follows its line on the road - the road's bends turn it, so it doesn't
            // slide straight on into a wall - at the pace it had; in the air, straight on.
            const Sample &p = history[history.size() - 2];
            const float span = float(a->t - p.t);
            if (span > 0.0f && i < int(p.cars.size()) && c.wreck == 0 && p.cars[i].path == c.path) {
                const sim::Car &o = p.cars[i];
                static const bool longGuess = qEnvironmentVariableIsSet("CAR_RACING_NET_LONGGUESS");   // (test: the old 0.35 s)
                const float ahead = std::min(float(renderT - a->t), longGuess ? 350.0f : 100.0f);   // (short: a long guess snaps back)
                const float k = ahead / span;
                const float dx = c.x - o.x, dz = c.z - o.z;
                const float dp = c.progress - o.progress;
                if (dx * dx + dz * dz < 25.0f && dp > -1.0f && dp < 5.0f) {   // (not across a respawn)
                    if (c.air > 0 || c.path == 1 || simEngine.sprint()) {
                        c.x += dx * k;
                        c.z += dz * k;
                        c.y += (c.y - o.y) * k;
                    } else {
                        const float oldS = c.s;
                        c.progress += dp * k;
                        c.d += (c.d - o.d) * k * 0.5f;          // (drifting across: damped)
                        c.lap = int(std::floor(c.progress / simEngine.lapLength()));
                        c.s = c.progress - float(c.lap) * simEngine.lapLength();
                        float x0, z0, x1, z1, x2, z2, x3, z3;
                        simEngine.pointAt(c.s, c.d, x2, z2, 0);
                        c.x = x2; c.z = z2;
                        simEngine.pointAt(oldS, 0.0f, x0, z0, 0);
                        simEngine.pointAt(simEngine.wrapS(oldS + 2.0f), 0.0f, x1, z1, 0);
                        simEngine.pointAt(c.s, 0.0f, x3, z3, 0);
                        simEngine.pointAt(simEngine.wrapS(c.s + 2.0f), 0.0f, x2, z2, 0);
                        const float turn = std::atan2(x2 - x3, -(z2 - z3)) - std::atan2(x1 - x0, -(z1 - z0));
                        c.heading += std::remainder(turn, 6.2831853f);   // (the road's bend turns the car)
                        c.hint = int(c.s);
                    }
                }
            }
        }
        // near my car: drawn where it is now (as my car bumps it), blending to the smooth delayed
        // view further away - so a clash on screen is where the server has it
        static const bool nearNow = !qEnvironmentVariableIsSet("CAR_RACING_NET_NOBUMP");
        if (nearNow && i < int(presentCars.size()) && mySlot >= 0 && mySlot < int(presentCars.size()) && c.wreck == 0) {
            const sim::Car &pc = presentCars[i];
            const float dx = pc.x - predicted.x, dz = pc.z - predicted.z, dist = std::sqrt(dx * dx + dz * dz);
            float want[4] = {pc.x - c.x, pc.z - c.z, pc.d - c.d, simEngine.aheadOf(c.s, pc.s)};
            const bool ok = dist < 40.0f && want[0] * want[0] + want[1] * want[1] < 400.0f && pc.path == c.path;
            const float w = ok ? std::clamp((40.0f - dist) / 20.0f, 0.0f, 1.0f) : 0.0f;
            // the offset glides to where it should be over about 0.06 s (each snapshot moves the
            // "now" a little; jumping there would jerk the car)
            float *off = nearOff[std::min(i, 7)];
            const float g = 1.0f - std::exp(-frameS / 0.06f);
            for (int k = 0; k < 4; ++k) off[k] += (want[k] * w - off[k]) * g;
            c.x += off[0]; c.z += off[1]; c.d += off[2];
            c.s = simEngine.wrapS(c.s + off[3]); c.hint = int(c.s);
        }
        cars[i] = c;
    }
    if (mySlot >= 0 && mySlot < int(cars.size())) {
        sim::Car me = predicted;
        // drawn between its last two steps by the time since the last one: a frame with no step
        // (or two) no longer jerks it - the screen draws at 60-144 fps, the race runs 60 steps
        static const bool stepped = qEnvironmentVariableIsSet("CAR_RACING_NET_STEPPED");   // (test: drawn on the steps)
        if (!stepped && predictedPrev.path == predicted.path) {
            const float f = std::clamp(simAccumulator * 60.0f, 0.0f, 1.0f);
            const float dx = predicted.x - predictedPrev.x, dz = predicted.z - predictedPrev.z;
            if (dx * dx + dz * dz < 25.0f) {             // (not across a respawn)
                me.x = predictedPrev.x + dx * f;
                me.z = predictedPrev.z + dz * f;
                me.y = predictedPrev.y + (predicted.y - predictedPrev.y) * f;
                me.heading = lerpAngle(predictedPrev.heading, predicted.heading, f);
            }
        }
        me.x += corrX;
        me.z += corrZ;
        // the correction fades by time, not by frames; a big one takes a little longer, so it glides
        const float big = std::min(1.0f, std::sqrt(corrX * corrX + corrZ * corrZ) / 4.0f);
        const float fade = std::exp(-frameS / (0.09f + 0.13f * big));
        corrX *= fade;
        corrZ *= fade;
        const sim::Car &srv = history.back().cars[mySlot];   // facts only the server knows
        me.finished = srv.finished;
        me.finishTime = srv.finishTime;
        me.r = srv.r; me.g = srv.g; me.b = srv.b;
        cars[mySlot] = me;
    }
    // the shake meter: how much each car's step from one frame to the next changes (a car
    // moving smoothly has nearly the same step every frame; a jerk shows as a big change)
    // (by speed: distance over the frame's own time, so uneven frame times don't count as a jerk)
    static QElapsedTimer shakeClock;
    static qint64 shakeLastNs = -1;
    if (!shakeClock.isValid()) shakeClock.start();
    const qint64 ns = shakeClock.nsecsElapsed();
    const float fdt = shakeLastNs < 0 ? 0.016f : std::max(0.002f, float(ns - shakeLastNs) * 1e-9f);
    shakeLastNs = ns;
    if (!netTest.isEmpty() && simEngine.getState() == State::Racing) {
        ++shakeFrames;
        shakeCam += view1.shake;                         // (the camera's own crash shake, 0..1)
        if (mySlot >= 0 && mySlot < int(cars.size()))    // (my car drawn inside another: a clash that didn't happen on screen)
            for (int i = 0; i < int(cars.size()); ++i) {
                const sim::Car &m = cars[mySlot], &o = cars[i];
                if (i == mySlot || o.wreck > 0 || m.path != o.path) continue;
                if (std::fabs(m.d - o.d) < 1.5f && std::fabs(simEngine.aheadOf(o.s, m.s)) < 3.5f) { ++statOverlap; break; }
            }
        {
            const double camStep = std::remainder(double(view1.camHeading) - shakeLastCamH, 6.2831853) / fdt;   // (rad/s)
            shakeCamTurn += std::min(10.0, std::fabs(camStep - shakeLastCamStep));
            shakeLastCamStep = camStep; shakeLastCamH = view1.camHeading;
        }
        for (int i = 0; i < int(cars.size()) && i < 8; ++i) {
            const float x = cars[i].x, z = cars[i].z;
            if (shakeHave[i]) {
                const float sx = (x - shakeLast[i][0]) / fdt, sz = (z - shakeLast[i][1]) / fdt;   // (m/s)
                const float jx = sx - shakeStep[i][0], jz = sz - shakeStep[i][1];
                const double j = std::sqrt(double(jx * jx + jz * jz)) * 0.01;   // (x100 below: m/s)
                if (j < 0.6) {                           // (not a respawn)
                    if (i == mySlot) { shakeMe += j; shakeMax = std::max(shakeMax, j); }
                    else shakeOthers += j;
                }
                shakeStep[i][0] = sx; shakeStep[i][1] = sz;
            }
            shakeLast[i][0] = x; shakeLast[i][1] = z; shakeHave[i] = true;
        }
    }
}

// ===========================================================================
//  Keys on the online screens
// ===========================================================================
bool GLWidget::handleOnlineKey(QKeyEvent *event)
{
    const int k = event->key();
    const bool enter = (k == Qt::Key_Return || k == Qt::Key_Enter);
    switch (netScreen) {
    case NetScreen::Off:
        return false;
    case NetScreen::Choose:
        if (k == Qt::Key_Up || k == Qt::Key_W)        netChoice = (netChoice + 3) % 4;
        else if (k == Qt::Key_Down || k == Qt::Key_S) netChoice = (netChoice + 1) % 4;
        else if (k == Qt::Key_Escape)                 netScreen = NetScreen::Off;
        else if (enter || k == Qt::Key_Space) {
            if (netChoice == 0)      netHostInternet();
            else if (netChoice == 1) netHost();
            else if (netChoice == 2) { netScreen = NetScreen::EnterCode; joinText.clear(); netMessage.clear(); }
            else                     netScreen = NetScreen::Off;
        }
        break;
    case NetScreen::EnterCode:
        if (k == Qt::Key_Escape)         netScreen = NetScreen::Choose;
        else if (enter)                  netJoin();
        else if (k == Qt::Key_Backspace) joinText.chop(1);
        else {
            for (QChar c : event->text().toUpper())
                if ((c.isLetterOrNumber() && c.unicode() < 128) || c == '@' || c == '.' || c == ':')
                    if (joinText.size() < 40) joinText += c;
        }
        break;
    case NetScreen::Busy:
        if (k == Qt::Key_Escape) netLeave(QString());
        break;
    case NetScreen::Lobby:
        if (nameEditing) {                               // typing a new name for yourself
            if (k == Qt::Key_Escape) {
                nameEditing = false;
            } else if (enter) {
                const QString n = net::cleanName(nameText);
                if (!n.isEmpty()) {
                    net->requestName(n);
                    if (netTest.isEmpty()) QSettings("TPP", "CarRacing").setValue("online/name", n);   // next time too
                }
                nameEditing = false;
            } else if (k == Qt::Key_Backspace) {
                nameText.chop(1);
            } else {
                for (QChar c : event->text().toUpper())
                    if (((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.')
                        && nameText.size() < net::MAX_NAME)
                        nameText += c;
            }
            break;
        }
        if (k == Qt::Key_Escape) {
            netLeave("YOU LEFT THE ROOM");
        } else if (k == Qt::Key_N) {
            nameEditing = true;
            nameText = (net->slot >= 0 && net->slot < net->players.size()) ? net->players[net->slot].name : QString();
            netMessage.clear();
        } else if (k == Qt::Key_C) {
            if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(net->code);
            netMessage = "ROOM CODE COPIED - PASTE IT TO YOUR FRIENDS";
        } else if (net->isHost && (k == Qt::Key_Plus || k == Qt::Key_Equal || k == Qt::Key_B)) {
            if (net->players.size() < sim::MAX_RACERS) net->requestAddCpu();
            else netMessage = QString("ALL %1 SEATS ARE TAKEN").arg(sim::MAX_RACERS);
        } else if (net->isHost && (k == Qt::Key_Minus || k == Qt::Key_Underscore)) {
            net->requestRemoveCpu();
        } else if (net->isHost && enter) {
            net->requestStart();
        } else if (net->isHost && (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_A || k == Qt::Key_D)) {
            int dir = (k == Qt::Key_Left || k == Qt::Key_A) ? -1 : 1;
            net->requestMap((net->lobbyMap + dir + sim::NUM_MAPS) % sim::NUM_MAPS, net->lobbyLaps);
        }
        break;
    case NetScreen::Race:
        if (k == Qt::Key_Escape) {
            if (story != StoryScreen::Off || netStoryHold) return false;   // (the story scene: ESC skips it for everyone)
            netLeave("YOU LEFT THE RACE");
            return true;
        }
        if (k == Qt::Key_R || k == Qt::Key_P) return true;   // no restart / pause online
        return false;                                         // driving keys
    }
    update();
    return true;
}

// ===========================================================================
//  Drawing: online menu, lobby, results
// ===========================================================================
void GLWidget::drawOnlineScreen(int W, int H, float dpr)
{
    begin2D(W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
    fillRect(0, 0, W, H);

    const float ps = std::clamp(H / 420.0f, 1.3f, 1.9f);
    const float pw = std::min(W * 0.66f, 820.0f * dpr), ph = std::min(H * 0.86f, 620.0f * dpr);
    const float x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f, x1 = x0 + pw, y1 = y0 + ph;
    const float pad = 22.0f * dpr;
    const float tick = float(simEngine.getTick());
    const bool blink = (int(tick) / 25) % 2 == 0;

    glColor4f(0.04f, 0.07f, 0.13f, 0.94f);
    fillRect(x0, y0, x1, y1);
    glColor4f(0.92f, 0.35f, 1.0f, 0.85f);
    frameRect(x0, y0, x1, y1, 2.0f * dpr);

    auto centered = [&](const QString &s, float y, float size, float r, float g, float b) {
        QByteArray t = s.toUtf8();
        drawText(t.constData(), (W - textWidth(t.constData(), size)) * 0.5f, y, size, r, g, b);
    };
    auto left = [&](const QString &s, float x, float y, float size, float r, float g, float b) {
        QByteArray t = s.toUtf8();
        drawText(t.constData(), x, y, size, r, g, b);
    };
    // status/error messages may hold several lines separated by '|'
    auto message = [&](float yTop, float size, float r, float g, float b) {
        const QStringList lines = netMessage.split('|');
        for (int i = 0; i < lines.size(); ++i)
            centered(lines[i], yTop - i * (9.0f * size + 4.0f * dpr), size * (i == 0 ? 1.0f : 0.85f),
                     r, g, i == 0 ? b : std::min(1.0f, b + 0.4f));
    };

    float y = y1 - pad - 7.0f * ps * 1.5f;
    switch (netScreen) {
    case NetScreen::Choose: {
        centered("ONLINE MULTIPLAYER", y, ps * 1.5f, 1.0f, 1.0f, 1.0f);
        y -= 18.0f * dpr + 7.0f * ps;
        centered(QString("UP TO %1 RACERS - ONE HOSTS, EVERYONE ELSE JOINS WITH THE ROOM CODE").arg(sim::MAX_RACERS),
                 y, ps * 0.75f, 0.70f, 0.78f, 0.92f);
        y -= 40.0f * dpr;
        static const char *OPTS[4] = {"HOST AN ONLINE RACE", "HOST ON THIS WI-FI ONLY", "JOIN WITH A CODE", "BACK"};
        static const char *SUB[4] = {"FRIENDS ANYWHERE JOIN WITH YOUR CODE - ANY WI-FI OR MOBILE DATA",
                                     "ONLY FOR FRIENDS ON THE SAME WI-FI AS THIS PC",
                                     "TYPE THE CODE YOUR FRIEND GOT - WORKS FROM ANYWHERE", "MAIN MENU"};
        const float bw = pw * 0.62f, bh = 54.0f * dpr, bx = (W - bw) * 0.5f;
        for (int i = 0; i < 4; ++i) {
            float by1 = y, by0 = y - bh;
            bool sel = (netChoice == i);
            glColor4f(sel ? 0.40f : 0.06f, sel ? 0.12f : 0.10f, sel ? 0.50f : 0.18f, 0.95f);
            fillRect(bx, by0, bx + bw, by1);
            glColor4f(0.92f, 0.35f, 1.0f, sel ? 1.0f : 0.35f);
            frameRect(bx, by0, bx + bw, by1, (sel ? 2.0f : 1.0f) * dpr);
            left(OPTS[i], bx + 18.0f * dpr, by0 + bh * 0.55f, ps * 1.05f, 1.0f, 1.0f, 1.0f);
            left(SUB[i], bx + 18.0f * dpr, by0 + bh * 0.18f, ps * 0.62f, 0.75f, 0.70f, 0.90f);
            if (sel) left(">", bx + bw - 26.0f * dpr, by0 + bh * 0.4f, ps * 1.1f, 1.0f, 0.85f, 0.25f);
            y = by0 - 14.0f * dpr;
        }
        if (!netMessage.isEmpty()) message(y - 10.0f * dpr, ps * 0.8f, 1.0f, 0.55f, 0.35f);
        {
            QString ip = lanAddress();
            const QString mine = savedName().isEmpty() ? QString("CAR + A NUMBER ([N] IN THE LOBBY TO CHANGE)") : savedName();
            centered("YOUR NAME: " + mine + "   -   THIS PC: " + (ip.isEmpty() ? QString("NO NETWORK!") : ip),
                     y0 + pad + 16.0f * dpr, ps * 0.62f, 0.60f, 0.70f, 0.85f);
        }
        centered("[UP/DOWN] CHOOSE   [ENTER] OK   [ESC] BACK", y0 + pad, ps * 0.7f, 0.70f, 0.78f, 0.90f);
        break;
    }
    case NetScreen::EnterCode: {
        centered("JOIN A RACE", y, ps * 1.5f, 1.0f, 1.0f, 1.0f);
        y -= 40.0f * dpr;
        centered("TYPE THE ROOM CODE", y, ps * 0.9f, 0.75f, 0.80f, 0.95f);
        y -= 30.0f * dpr;
        const float bs = ps * 4.0f, bw = std::max(textWidth("WWWW_", bs), textWidth((joinText + "_").toUtf8().constData(), bs)) + 48.0f * dpr;
        const float bh = 7.0f * bs + 28.0f * dpr, bx = (W - bw) * 0.5f;
        glColor4f(0.02f, 0.03f, 0.06f, 1.0f);
        fillRect(bx, y - bh, bx + bw, y);
        glColor4f(1.0f, 0.82f, 0.25f, 0.9f);
        frameRect(bx, y - bh, bx + bw, y, 2.0f * dpr);
        QString shown = joinText + (blink ? "_" : " ");
        centered(shown, y - bh + 14.0f * dpr, bs, 1.0f, 0.88f, 0.30f);
        y -= bh + 26.0f * dpr;
        centered("4 LETTERS, FOR EXAMPLE K7Q2", y, ps * 0.7f, 0.65f, 0.72f, 0.85f);
        y -= 18.0f * dpr;
        centered("YOUR FRIEND CAN BE ANYWHERE - SAME WI-FI OR NOT", y, ps * 0.62f, 0.55f, 0.62f, 0.78f);
        if (!netMessage.isEmpty()) message(y - 30.0f * dpr, ps * 0.8f, 1.0f, 0.55f, 0.35f);
        centered("[ENTER] JOIN   [BACKSPACE] DELETE   [ESC] BACK", y0 + pad, ps * 0.7f, 0.70f, 0.78f, 0.90f);
        break;
    }
    case NetScreen::Busy:
        centered("ONLINE", y, ps * 1.5f, 1.0f, 1.0f, 1.0f);
        if (netMessage.isEmpty()) centered("CONNECTING...", (y0 + y1) * 0.5f, ps * 0.95f, 1.0f, 0.88f, 0.30f);
        else message((y0 + y1) * 0.5f, ps * 0.95f, 1.0f, 0.88f, 0.30f);
        centered("[ESC] CANCEL", y0 + pad, ps * 0.7f, 0.70f, 0.78f, 0.90f);
        break;
    case NetScreen::Lobby: {
        centered("RACE LOBBY", y, ps * 1.4f, 1.0f, 1.0f, 1.0f);
        if (net->pingMs >= 0)
            left(QString("PING %1 MS").arg(net->pingMs), x1 - pad - textWidth("PING 000 MS", ps * 0.6f), y,
                 ps * 0.6f, 0.45f, 1.0f, 0.55f);
        y -= 16.0f * dpr + 7.0f * ps;
        centered("ROOM CODE", y, ps * 0.8f, 0.75f, 0.80f, 0.95f);
        y -= 10.0f * dpr;
        const float cs = ps * 4.2f, cw = textWidth(net->code.toUtf8().constData(), cs) + 44.0f * dpr;
        const float ch = 7.0f * cs + 24.0f * dpr, cx = (W - cw) * 0.5f;
        glColor4f(0.02f, 0.03f, 0.06f, 1.0f);
        fillRect(cx, y - ch, cx + cw, y);
        glColor4f(1.0f, 0.82f, 0.25f, 0.95f);
        frameRect(cx, y - ch, cx + cw, y, 2.0f * dpr);
        centered(net->code, y - ch + 12.0f * dpr, cs, 1.0f, 0.88f, 0.30f);
        y -= ch + 20.0f * dpr;
        centered("SHARE THIS CODE: FRIENDS PICK ONLINE > JOIN WITH A CODE   [C] COPY", y, ps * 0.66f, 0.85f, 0.90f, 1.0f);
        if (net->isHost) {
            QString ip = lanAddress();
            if (!ip.isEmpty()) {
                y -= 16.0f * dpr;
                centered("IF THE CODE ALONE DOES NOT WORK, FRIENDS TYPE: " + net->code + "@" + ip, y, ps * 0.62f,
                         0.85f, 0.85f, 0.55f);
            }
            int people = 0;
            for (const net::LobbyPlayer &lp : net->players) people += lp.cpuLevel == 0 ? 1 : 0;
            if (hostServer && people < 2) {
                // shows whether friends' join attempts reach this PC at all
                y -= 16.0f * dpr;
                int seen = hostServer->server()->remoteAttempts();
                if (seen == 0)
                    centered("JOIN ATTEMPTS FROM OTHER PCS: 0  -  IF A FRIEND IS TRYING, RUN ALLOW_FIREWALL.BAT ON THIS PC",
                             y, ps * 0.6f, 1.0f, 0.60f, 0.35f);
                else
                    centered(QString("JOIN ATTEMPTS FROM OTHER PCS: %1  -  YOUR NETWORK IS FINE").arg(seen),
                             y, ps * 0.6f, 0.45f, 1.0f, 0.55f);
            }
        }
        y -= 30.0f * dpr;

        // player list
        int cpus = 0;
        for (const net::LobbyPlayer &lp : net->players) cpus += lp.cpuLevel > 0 ? 1 : 0;
        left(cpus ? QString("PLAYERS %1/%2  (%3 CPU)").arg(net->players.size()).arg(sim::MAX_RACERS).arg(cpus)
                  : QString("PLAYERS %1/%2").arg(net->players.size()).arg(sim::MAX_RACERS),
             x0 + pad, y, ps * 0.85f, 0.92f, 0.35f, 1.0f);
        const sim::MapDef &md = sim::MAPS[std::clamp(net->lobbyMap, 0, sim::NUM_MAPS - 1)];
        QString mapLine = QString("MAP: ") + md.name + (net->isHost ? " < >" : "")
                          + (sim::GameSim::isSprintMap(net->lobbyMap) ? QString("   SPRINT")
                                                                        : QString("   LAPS: %1").arg(sim::GameSim::LAPS));
        left(mapLine, x1 - pad - textWidth(mapLine.toUtf8().constData(), ps * 0.85f), y, ps * 0.85f, 0.35f, 0.90f, 1.0f);
        y -= 12.0f * dpr;
        const float rowH = std::min(30.0f * dpr, (y - (y0 + pad + 50.0f * dpr)) / float(sim::MAX_RACERS));
        for (int i = 0; i < sim::MAX_RACERS; ++i) {
            float ry1 = y - i * rowH, ry0 = ry1 - rowH + 3.0f * dpr;
            glColor4f(0.07f, 0.10f, 0.17f, 0.9f);
            fillRect(x0 + pad, ry0, x1 - pad, ry1);
            float ty = ry0 + (rowH - 3.0f * dpr - 7.0f * ps * 0.8f) * 0.5f;
            if (i < net->players.size()) {
                const net::LobbyPlayer &p = net->players[i];
                glColor4f(p.r, p.g, p.b, 1.0f);
                fillRect(x0 + pad + 8.0f * dpr, ry0 + 5.0f * dpr, x0 + pad + 8.0f * dpr + (rowH - 13.0f * dpr), ry1 - 8.0f * dpr);
                const float ts = ps * 0.8f;
                const float numX = x0 + pad + rowH + 4.0f * dpr;
                left(QString::number(i + 1), numX, ty, ts, 0.60f, 0.65f, 0.75f);
                // every name gets the same space (10 letters): longer ones are written smaller
                const float nameX = numX + textWidth("0  ", ts), nameW = textWidth("WWWWWWWWWW", ts);
                const bool editing = nameEditing && i == net->slot;
                const QByteArray nb = (editing ? nameText + (blink ? "_" : " ") : p.name).toUtf8();
                const float ns = fitSize(nb.constData(), ts, nameW);
                if (editing) {
                    glColor4f(1.0f, 0.82f, 0.25f, 0.22f);
                    fillRect(nameX - 4.0f * dpr, ry0 + 2.0f * dpr, nameX + nameW + 4.0f * dpr, ry1 - 2.0f * dpr);
                }
                drawText(nb.constData(), nameX, ty + 3.5f * (ts - ns), ns, editing ? 1.0f : (p.cpuLevel ? 0.75f : 1.0f),
                         editing ? 0.88f : (p.cpuLevel ? 0.85f : 1.0f), editing ? 0.30f : 1.0f);
                QStringList tags;
                if (p.host) tags << "HOST";
                if (i == net->slot) tags << "YOU";
                if (p.cpuLevel > 0) tags << QString("BOT (ADAPTS)");
                left(tags.join("  "), nameX + nameW + 16.0f * dpr, ty + 3.5f * ts * 0.2f, ts * 0.8f, 0.55f, 0.85f, 1.0f);

            } else {
                left(QString("%1  WAITING FOR PLAYER...").arg(i + 1), x0 + pad + rowH + 4.0f * dpr, ty, ps * 0.8f,
                     0.35f, 0.40f, 0.50f);
            }
        }
        if (!netMessage.isEmpty()) centered(netMessage, y0 + pad + 22.0f * dpr, ps * 0.66f, 0.45f, 1.0f, 0.55f);
        if (nameEditing)
            centered(QString("TYPE YOUR NAME (UP TO %1 LETTERS)   [ENTER] SAVE   [ESC] CANCEL").arg(net::MAX_NAME),
                     y0 + pad, ps * 0.75f, 1.0f, 0.88f, 0.30f);
        else if (net->isHost)
            centered(blink ? "[ENTER] START   [+]/[-] CPU   [N] NAME   [LEFT/RIGHT] MAP   [ESC] LEAVE"
                           : "                [+]/[-] CPU   [N] NAME   [LEFT/RIGHT] MAP   [ESC] LEAVE",
                     y0 + pad, ps * 0.75f, 0.45f, 1.0f, 0.55f);
        else
            centered("WAITING FOR THE HOST TO START...   [N] YOUR NAME   [ESC] LEAVE", y0 + pad, ps * 0.75f, 0.85f, 0.85f, 0.85f);
        break;
    }
    default:
        break;
    }
}

void GLWidget::drawOnlineResults(int W, int H, float dpr)
{
    begin2D(W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.02f, 0.04f, 0.07f, 0.82f);
    fillRect(0, 0, W, H);

    const float ps = std::clamp(H / 420.0f, 1.3f, 1.9f);
    const auto &cars = simEngine.getCars();
    const int n = int(cars.size());
    float y = H * 0.82f;
    auto centered = [&](const QString &s, float yy, float size, float r, float g, float b) {
        QByteArray t = s.toUtf8();
        drawText(t.constData(), (W - textWidth(t.constData(), size)) * 0.5f, yy, size, r, g, b);
    };
    centered("RACE RESULTS", y, ps * 2.2f, 1.0f, 0.88f, 0.25f);
    y -= 40.0f * dpr;
    for (int rank = 1; rank <= n; ++rank) {
        for (int i = 0; i < n; ++i) {
            if (simEngine.rankOf(i) != rank) continue;
            const sim::Car &c = cars[i];
            QString name = (i < racerNames.size()) ? racerNames[i] : QString("P%1").arg(i + 1);
            QString time = c.finished ? QString("%1:%2").arg(int(c.finishTime) / 60)
                                            .arg(c.finishTime - 60.0f * (int(c.finishTime) / 60), 5, 'f', 2, QChar('0'))
                                      : QString(c.out ? "OUT" : "DNF");
            bool me = (i == mySlot);
            const float ts = ps * 1.1f;
            const float rankW = textWidth("1ST  ", ts), nameW = textWidth("WWWWWWWWWW", ts);
            float rowW = rankW + nameW + textWidth("  0:00.00", ts) + 50.0f * dpr;
            float rx = (W - rowW) * 0.5f;
            glColor4f(c.r, c.g, c.b, 1.0f);
            fillRect(rx, y - 2.0f * dpr, rx + 14.0f * dpr, y + 7.0f * ts);
            const float cr = me ? 1.0f : 0.88f, cg = me ? 0.88f : 0.92f, cb = me ? 0.30f : 0.98f;
            float tx = rx + 26.0f * dpr;
            drawText(ordinal(rank), tx, y, ts, cr, cg, cb);
            const QByteArray nb = name.toUtf8();
            const float ns = fitSize(nb.constData(), ts, nameW);
            drawText(nb.constData(), tx + rankW, y + 3.5f * (ts - ns), ns, cr, cg, cb);
            const QByteArray tb = time.toUtf8();
            drawText(tb.constData(), tx + rankW + nameW + textWidth("  ", ts), y, ts, cr, cg, cb);
            y -= 9.0f * ps * 1.1f + 8.0f * dpr;
        }
    }
    centered("BACK TO THE LOBBY IN A FEW SECONDS", H * 0.10f, ps * 0.8f, 0.70f, 0.78f, 0.90f);
}

// ===========================================================================
//  Scripted online test (CAR_RACING_NETTEST=host:N or join:CODE[@addr:port])
// ===========================================================================
void GLWidget::runNetAutoShot()
{
    const int f = ++autoShotFrame;
    auto shot = [&](const QString &name) { saveScreenshot(autoShotDir + "/" + name + ".png"); };
    static int lobbyFrame = -1, raceFrame = -1;
    static bool codeWritten = false;
    const bool host = netTest.startsWith("host");

    if (f == 30) openOnline();
    if (host) {
        const int wanted = std::max(1, netTest.section(':', 1, 1).toInt());
        if (f == 45) shot("net_01_online_menu");
        const int cpus = netTest.section(':', 2, 2).toInt();
        static int inLobby = -1;                        // (CPUs are added once the lobby is there: over the internet it takes a moment)
        if (netScreen == NetScreen::Lobby && inLobby < 0) inLobby = std::max(f, 120);
        if (inLobby > 0 && f >= inLobby && f < inLobby + 10 * cpus && (f - inLobby) % 10 == 0) net->requestAddCpu();
        if (f == 330 && netScreen == NetScreen::Lobby) shot("net_02a_host_waiting");
        if (f == 50) {                                  // (CAR_RACING_NETTEST_INTERNET: on the internet server, CAR_RACING_RACE)
            if (qEnvironmentVariableIsSet("CAR_RACING_NETTEST_INTERNET")) netHostInternet();
            else netHost();
        }
        if (!codeWritten && net && !net->code.isEmpty()) {         // tell the test script the code
            QFile file(autoShotDir + "/room.txt");
            if (file.open(QIODevice::WriteOnly)) file.write(net->code.toUtf8());
            codeWritten = true;
        }
        if (netScreen == NetScreen::Lobby && lobbyFrame < 0 && net->players.size() >= wanted) lobbyFrame = f;
        if (lobbyFrame > 0 && f == lobbyFrame + 40) shot("net_02_host_lobby");
        if (lobbyFrame > 0 && f == lobbyFrame + 50) net->requestStart();
    } else {
        if (f == 45) { netScreen = NetScreen::EnterCode; joinText = netTest.mid(5); }
        if (f == 60) shot("net_03_join_code");
        if (f == 70) netJoin();
        if (netScreen == NetScreen::Lobby && lobbyFrame < 0) lobbyFrame = f;
        if (lobbyFrame > 0 && f == lobbyFrame + 30) shot("net_04_guest_lobby");
        if (f == 600 && lobbyFrame < 0 && !isOnline()) {      // the room was never found
            shot("net_03b_join_failed");
            QCoreApplication::quit();
        }
    }
    // CAR_RACING_NETTEST_NAME: rename in the lobby (checks long names fit their space)
    if (lobbyFrame > 0 && f == lobbyFrame + 12 && !qEnvironmentVariable("CAR_RACING_NETTEST_NAME").isEmpty())
        net->requestName(qEnvironmentVariable("CAR_RACING_NETTEST_NAME"));
    auto finish = [&]() {
        QFile stats(autoShotDir + "/net_stats.txt");        // GUI apps on Windows have no console
        if (stats.open(QIODevice::WriteOnly)) {
            stats.write(QString("snapshots %1, corrections >0.5 m: %2, large (>6 m, smoothed): %3, ping %4 ms, jumps landed %5\n")
                            .arg(statSnapshots).arg(statSmallFix).arg(statSnaps).arg(net ? net->pingMs : -1)
                            .arg(statLandings).toUtf8());
            stats.write(QString("camera: crash shake %1 on average, turn jerk %2 rad/s per frame; server saw %3 clashes with cars, %4 crash events for my car\n")
                            .arg(shakeCam / std::max(1, shakeFrames), 0, 'f', 3).arg(shakeCamTurn / std::max(1, shakeFrames), 0, 'f', 3)
                            .arg(myClashes).arg(myCrashEvents).toUtf8());
            stats.write(QString("my car drawn inside another car: %1 frames\n").arg(statOverlap).toUtf8());
            stats.write(QString("shake (speed change per frame, m/s): my car %1 (worst %2), other cars %3\n")
                            .arg(100.0 * shakeMe / std::max(1, shakeFrames), 0, 'f', 2).arg(100.0 * shakeMax, 0, 'f', 1)
                            .arg(100.0 * shakeOthers / std::max(1, shakeFrames * std::max(1, int(simEngine.getCars().size()) - 1)), 0, 'f', 2).toUtf8());
            stats.write(QString("average correction per snapshot %1 cm\n").arg(100.0 * statErrSum / std::max(1, statSnapshots), 0, 'f', 2).toUtf8());
            stats.write(QString("frames %1, slower than 25 ms: %2, drawing avg %3 ms max %4 ms\n")
                            .arg(statFrames).arg(statSlowFrames).arg(statPaintMs / std::max(1, statFrames), 0, 'f', 2)
                            .arg(statPaintMax, 0, 'f', 1).toUtf8());
            stats.write(QString("frames with nothing newer to show: %1 of %2, stalls %3, late %4, jitter %5 ms, buffer %6 ms\n")
                            .arg(statStarved).arg(statComposed).arg(statStalls).arg(statLate)
                            .arg(netJitter, 0, 'f', 0).arg(netInterpMs, 0, 'f', 0).toUtf8());
            stats.write(QString("snapshot gaps: longest %1 ms, over 50 ms: %2, average size %3 bytes\n")
                            .arg(statSnapGapMax, 0, 'f', 0).arg(statSnapLate)
                            .arg(statSnapBytes / std::max(1, statSnapshots), 0, 'f', 0).toUtf8());
            if (hostServer)
                stats.write(QString("server: longest pause between ticks %1 ms, ticks without a new input: %2, inputs dropped: %3\n")
                                .arg(hostServer->server()->statMaxGapMs.load()).arg(hostServer->server()->statStarved.load())
                                .arg(hostServer->server()->statDropped.load()).toUtf8());
        }
        QCoreApplication::quit();
    };
    if (isOnline()) {
        if (raceFrame < 0) raceFrame = f;
        if (f == raceFrame + 150) shot(host ? "net_04b_host_grid" : "net_04c_guest_grid");
        if (f == raceFrame + 400) shot(host ? "net_05_host_race" : "net_06_guest_race");
        // CAR_RACING_NETTEST_FRAMES: race longer than 1000 frames (e.g. to cross a jump)
        const int len = std::max(1000, qEnvironmentVariableIntValue("CAR_RACING_NETTEST_FRAMES"));
        if (f == raceFrame + len - 100) shot(host ? "net_07_host_race_later" : "net_08_guest_race_later");
        if (f == raceFrame + (host ? len : len - 60)) finish();  // the host (server) closes last
    } else if (raceFrame > 0) {
        finish();                                                 // the race ended for us early
    }
    if (f > 60 * 200) QCoreApplication::quit();                   // safety net
}
