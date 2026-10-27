// Online multiplayer: the "ONLINE MULTIPLAYER (8P)" screens and the networked race.
//
// One player picks HOST A RACE: their game starts a RaceServer and shows a 4-letter
// room code. Friends pick JOIN WITH A CODE and type it; their game finds the host on
// the local network. The host's server runs the real race; every game sends only its
// key presses and draws the snapshots it gets back:
//   * my own car is predicted locally (no input lag) and corrected by each snapshot
//   * the other cars are drawn INTERP_DELAY_MS in the past, blended between snapshots
#include "glwidget.h"
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
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QNetworkInterface>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>

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
QString GLWidget::playerName() const
{
    QString n;
    for (QChar c : qEnvironmentVariable("USERNAME").toUpper())
        if (c.isLetterOrNumber() && c.unicode() < 128) n += c;
    return n.isEmpty() ? QString("PLAYER") : n.left(10);
}

QString GLWidget::lanAddress()
{
    // the address friends on the same Wi-Fi/hotspot can reach (Wi-Fi adapter first)
    const auto entries = net::lanEntries();
    return entries.isEmpty() ? QString() : entries.first().ip().toString();
}

void GLWidget::openOnline()
{
    if (!net) {
        net = new RaceClient(this);
        netClock.start();
        connect(net, &RaceClient::status, this, [this](const QString &s) { netMessage = s; update(); });
        connect(net, &RaceClient::welcomed, this, [this]() { netScreen = NetScreen::Lobby; netMessage.clear(); });
        connect(net, &RaceClient::lobbyChanged, this, [this]() {
            if (netScreen == NetScreen::Busy) netScreen = NetScreen::Lobby;
        });
        connect(net, &RaceClient::raceStarted, this, [this](int map, int slot, const QStringList &names) {
            selectedMapPreset = map;
            simEngine.setMap(map);
            simEngine.startRace(sim::GameSim::ModeOnline, int(names.size()));
            mySlot = slot;
            racerNames = names;
            view1 = CarView();
            view2 = CarView();
            for (CarFx &f : carFx) f = CarFx();
            for (float &t : nitroBannerTimer) t = 0.0f;
            skidMarks.clear();
            particles.clear();
            netEvents.clear();
            history.clear();
            sentInputs.clear();
            inputSeq = 0;
            corrX = corrZ = 0.0f;
            if (slot >= 0 && slot < int(simEngine.getCars().size())) predicted = simEngine.getCars()[slot];
            keys.clear();
            simAccumulator = 0.0f;
            elapsedTimer.restart();
            netScreen = NetScreen::Race;
        });
        connect(net, &RaceClient::snapshotArrived, this, &GLWidget::onNetSnapshot);
        connect(net, &RaceClient::raceOver, this, [this]() {
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

void GLWidget::netHost()
{
    float r, g, b;
    myCarColor(r, g, b);
    // CAR_RACING_SERVER=host[:port] hosts the room on a dedicated server instead of this PC
    QString server = qEnvironmentVariable("CAR_RACING_SERVER");
    if (!server.isEmpty()) {
        quint16 port = net::GAME_PORT;
        int colon = server.lastIndexOf(':');
        if (colon > 0) { port = quint16(server.mid(colon + 1).toUInt()); server = server.left(colon); }
        netScreen = NetScreen::Busy;
        net->create(server, port, playerName(), r, g, b, selectedMapPreset);
        return;
    }
    if (!hostServer) {
        hostServer = new RaceServer(this);
        if (!hostServer->start()) {
            delete hostServer;
            hostServer = nullptr;
            netMessage = "COULD NOT START A SERVER ON THIS PC";
            return;
        }
    }
    netScreen = NetScreen::Busy;
    netMessage = "CREATING ROOM...";
    net->create("127.0.0.1", hostServer->port(), playerName(), r, g, b, selectedMapPreset);
}

void GLWidget::netJoin()
{
    if (joinText.trimmed().isEmpty()) { netMessage = "TYPE THE ROOM CODE FIRST"; return; }
    float r, g, b;
    myCarColor(r, g, b);
    netScreen = NetScreen::Busy;
    net->join(joinText, playerName(), r, g, b);
}

void GLWidget::netLeave(const QString &message)
{
    if (net) net->leave();
    if (hostServer) { hostServer->deleteLater(); hostServer = nullptr; }
    if (isOnline() || simEngine.getGameMode() == sim::GameSim::ModeOnline) simEngine.setState(State::Menu);
    netScreen = NetScreen::Choose;
    netMessage = message;
    keys.clear();
}

// ===========================================================================
//  The networked race
// ===========================================================================
void GLWidget::onNetSnapshot(quint32 lastSeq, const QByteArray &body)
{
    if (!isOnline()) return;
    QDataStream in(body);
    net::prepare(in);
    std::vector<sim::SimEvent> evs;
    simEngine.readSnapshot(in, evs);
    netEvents.insert(netEvents.end(), evs.begin(), evs.end());

    const auto &cars = simEngine.getCars();
    const qint64 now = netClock.elapsed();
    history.push_back({now, cars});
    while (history.size() > 2 && history.front().t < now - 1000) history.pop_front();
    if (mySlot < 0 || mySlot >= int(cars.size())) return;

    // Reconciliation: start from where the server says my car is, then re-apply the
    // inputs it hasn't processed yet. Where the car ends up is my new prediction.
    while (!sentInputs.empty() && sentInputs.front().seq <= lastSeq) sentInputs.pop_front();
    sim::Car p = cars[mySlot];
    p.aiTarget = predicted.aiTarget; p.aiReverse = predicted.aiReverse;     // test autopilot memory
    p.aiCheckTimer = predicted.aiCheckTimer; p.aiCheckZ = predicted.aiCheckZ;
    for (const SentInput &si : sentInputs) {
        simEngine.updateCar(p, si.in, mySlot);
        simEngine.predictCollisions(p);
    }
    simEngine.clearEvents();
    // hide corrections by blending them out over a few frames instead of jumping
    float ex = (predicted.x + corrX) - p.x, ez = (predicted.z + corrZ) - p.z;
    float err2 = ex * ex + ez * ez;
    ++statSnapshots;
    if (err2 > 0.25f) ++statSmallFix;
    if (err2 > 36.0f) ++statSnaps;                       // big: usually a crash the server saw first
    if (err2 < 225.0f) { corrX = ex; corrZ = ez; }       // up to 15 m: glide there over a few frames
    else               { corrX = corrZ = 0.0f; }         // further: jump (respawn-sized error)
    predicted = p;
}

void GLWidget::netUpdate(float realDt)
{
    const float DT = 1.0f / 60.0f;
    auto &cars = simEngine.getCars();
    simAccumulator += realDt;
    while (simAccumulator >= DT) {
        simAccumulator -= DT;
        sim::PlayerInput in;                             // online: arrows or WASD both drive
        in.up    = isDown(Qt::Key_Up) || isDown(Qt::Key_W);
        in.down  = isDown(Qt::Key_Down) || isDown(Qt::Key_S);
        in.left  = isDown(Qt::Key_Left) || isDown(Qt::Key_A);
        in.right = isDown(Qt::Key_Right) || isDown(Qt::Key_D);
        in.nitro = isDown(Qt::Key_Return) || isDown(Qt::Key_Enter) || isDown(Qt::Key_Space) || isDown(Qt::Key_N);
        if (!netTest.isEmpty() && mySlot < int(cars.size()))
            in = simEngine.computeAIInput(predicted, mySlot, false);   // scripted test: CPU drives
        net->sendInput(++inputSeq, in);
        sentInputs.push_back({inputSeq, in});
        while (sentInputs.size() > 180) sentInputs.pop_front();

        if (mySlot < int(cars.size())) {                                             // prediction
            simEngine.updateCar(predicted, in, mySlot);
            simEngine.predictCollisions(predicted);
        }
        for (sim::Traffic &t : simEngine.trafficMut()) {                             // traffic between snapshots
            if (t.parked) continue;
            t.z -= t.speed;
            t.lane += (t.targetLane - t.lane) * 0.06f;
        }
        for (int i = 0; i < int(cars.size()) && i < sim::MAX_RACERS; ++i) updateCarFx(i, cars[i]);
    }
    simEngine.clearEvents();               // prediction makes its own effects; the server's are used
    processSimEvents(netEvents);
    netEvents.clear();
    composeOnlineCars();
}

void GLWidget::composeOnlineCars()
{
    auto &cars = simEngine.getCars();
    if (history.empty()) return;
    const qint64 renderT = netClock.elapsed() - net::INTERP_DELAY_MS;

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
            float f = std::clamp(float(renderT - a->t) / float(b->t - a->t), 0.0f, 1.0f);
            c.x += (n.x - c.x) * f;
            c.z += (n.z - c.z) * f;
            c.heading = lerpAngle(c.heading, n.heading, f);
            c.steerVis += (n.steerVis - c.steerVis) * f;
            c.roll += (n.roll - c.roll) * f;
            c.pitch += (n.pitch - c.pitch) * f;
            c.speed += (n.speed - c.speed) * f;
            c.wheelSpin = n.wheelSpin;
        }
        cars[i] = c;
    }
    if (mySlot >= 0 && mySlot < int(cars.size())) {
        sim::Car me = predicted;
        me.x += corrX;
        me.z += corrZ;
        corrX *= 0.85f;
        corrZ *= 0.85f;
        const sim::Car &srv = history.back().cars[mySlot];   // facts only the server knows
        me.finished = srv.finished;
        me.finishTime = srv.finishTime;
        me.r = srv.r; me.g = srv.g; me.b = srv.b;
        cars[mySlot] = me;
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
        if (k == Qt::Key_Up || k == Qt::Key_W)        netChoice = (netChoice + 2) % 3;
        else if (k == Qt::Key_Down || k == Qt::Key_S) netChoice = (netChoice + 1) % 3;
        else if (k == Qt::Key_Escape)                 netScreen = NetScreen::Off;
        else if (enter || k == Qt::Key_Space) {
            if (netChoice == 0)      netHost();
            else if (netChoice == 1) { netScreen = NetScreen::EnterCode; joinText.clear(); netMessage.clear(); }
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
        if (k == Qt::Key_Escape) {
            netLeave("YOU LEFT THE ROOM");
        } else if (k == Qt::Key_C) {
            if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(net->code);
            netMessage = "ROOM CODE COPIED - PASTE IT TO YOUR FRIENDS";
        } else if (net->isHost && enter) {
            net->requestStart();
        } else if (net->isHost && (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_A || k == Qt::Key_D)) {
            int dir = (k == Qt::Key_Left || k == Qt::Key_A) ? -1 : 1;
            net->requestMap((net->lobbyMap + dir + sim::NUM_MAPS) % sim::NUM_MAPS);
        }
        break;
    case NetScreen::Race:
        if (k == Qt::Key_Escape) { netLeave("YOU LEFT THE RACE"); return true; }
        if (k == Qt::Key_C) { view1.camMode = (view1.camMode + 1) % 3; return true; }
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
        centered("UP TO 8 PLAYERS - ONE HOSTS, EVERYONE ELSE JOINS WITH THE ROOM CODE", y, ps * 0.75f, 0.70f, 0.78f, 0.92f);
        y -= 40.0f * dpr;
        static const char *OPTS[3] = {"HOST A RACE", "JOIN WITH A CODE", "BACK"};
        static const char *SUB[3] = {"YOU GET A CODE TO SHARE", "TYPE THE CODE YOUR FRIEND GOT", "MAIN MENU"};
        const float bw = pw * 0.62f, bh = 54.0f * dpr, bx = (W - bw) * 0.5f;
        for (int i = 0; i < 3; ++i) {
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
            centered("YOUR NAME: " + playerName() + "   -   THIS PC: " + (ip.isEmpty() ? QString("NO NETWORK!") : ip),
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
        centered("DIFFERENT NETWORK? TYPE CODE@ADDRESS, FOR EXAMPLE K7Q2@203.0.113.5", y, ps * 0.62f, 0.55f, 0.62f, 0.78f);
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
            if (hostServer && net->players.size() < 2) {
                // shows whether friends' join attempts reach this PC at all
                y -= 16.0f * dpr;
                int seen = hostServer->remoteAttempts();
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
        left(QString("PLAYERS %1/8").arg(net->players.size()), x0 + pad, y, ps * 0.85f, 0.92f, 0.35f, 1.0f);
        const sim::MapDef &md = sim::MAPS[std::clamp(net->lobbyMap, 0, sim::NUM_MAPS - 1)];
        QString mapLine = QString("MAP: ") + md.name + (net->isHost ? "  < >" : "");
        left(mapLine, x1 - pad - textWidth(mapLine.toUtf8().constData(), ps * 0.85f), y, ps * 0.85f, 0.35f, 0.90f, 1.0f);
        y -= 12.0f * dpr;
        const float rowH = std::min(26.0f * dpr, (y - (y0 + pad + 50.0f * dpr)) / 8.0f);
        for (int i = 0; i < sim::MAX_RACERS; ++i) {
            float ry1 = y - i * rowH, ry0 = ry1 - rowH + 3.0f * dpr;
            glColor4f(0.07f, 0.10f, 0.17f, 0.9f);
            fillRect(x0 + pad, ry0, x1 - pad, ry1);
            float ty = ry0 + (rowH - 3.0f * dpr - 7.0f * ps * 0.8f) * 0.5f;
            if (i < net->players.size()) {
                const net::LobbyPlayer &p = net->players[i];
                glColor4f(p.r, p.g, p.b, 1.0f);
                fillRect(x0 + pad + 8.0f * dpr, ry0 + 5.0f * dpr, x0 + pad + 8.0f * dpr + (rowH - 13.0f * dpr), ry1 - 8.0f * dpr);
                QString line = QString("%1  %2").arg(i + 1).arg(p.name);
                if (p.host) line += "  (HOST)";
                if (i == net->slot) line += "  (YOU)";
                left(line, x0 + pad + rowH + 4.0f * dpr, ty, ps * 0.8f, 1.0f, 1.0f, 1.0f);
            } else {
                left(QString("%1  WAITING FOR PLAYER...").arg(i + 1), x0 + pad + rowH + 4.0f * dpr, ty, ps * 0.8f,
                     0.35f, 0.40f, 0.50f);
            }
        }
        if (!netMessage.isEmpty()) centered(netMessage, y0 + pad + 22.0f * dpr, ps * 0.66f, 0.45f, 1.0f, 0.55f);
        if (net->isHost)
            centered(blink ? "[ENTER] START RACE   [LEFT/RIGHT] MAP   [ESC] LEAVE" : "   [LEFT/RIGHT] MAP   [ESC] LEAVE",
                     y0 + pad, ps * 0.75f, 0.45f, 1.0f, 0.55f);
        else
            centered("WAITING FOR THE HOST TO START...   [ESC] LEAVE", y0 + pad, ps * 0.75f, 0.85f, 0.85f, 0.85f);
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
                                      : QString("DNF");
            QString line = QString("%1  %2  %3").arg(ordinal(rank), -4).arg(name, -12).arg(time);
            bool me = (i == mySlot);
            float rowW = textWidth("1ST   WWWWWWWWWWWW  0:00.00", ps * 1.1f) + 50.0f * dpr;
            float rx = (W - rowW) * 0.5f;
            glColor4f(c.r, c.g, c.b, 1.0f);
            fillRect(rx, y - 2.0f * dpr, rx + 14.0f * dpr, y + 7.0f * ps * 1.1f);
            QByteArray t = line.toUtf8();
            drawText(t.constData(), rx + 26.0f * dpr, y, ps * 1.1f, me ? 1.0f : 0.88f, me ? 0.88f : 0.92f, me ? 0.30f : 0.98f);
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
        if (f == 330 && netScreen == NetScreen::Lobby) shot("net_02a_host_waiting");
        if (f == 50) netHost();
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
    auto finish = [&]() {
        QFile stats(autoShotDir + "/net_stats.txt");        // GUI apps on Windows have no console
        if (stats.open(QIODevice::WriteOnly))
            stats.write(QString("snapshots %1, corrections >0.5 m: %2, large (>6 m, smoothed): %3, ping %4 ms\n")
                            .arg(statSnapshots).arg(statSmallFix).arg(statSnaps).arg(net ? net->pingMs : -1).toUtf8());
        QCoreApplication::quit();
    };
    if (isOnline()) {
        if (raceFrame < 0) raceFrame = f;
        if (f == raceFrame + 400) shot(host ? "net_05_host_race" : "net_06_guest_race");
        if (f == raceFrame + 900) shot(host ? "net_07_host_race_later" : "net_08_guest_race_later");
        if (f == raceFrame + (host ? 1000 : 940)) finish();      // the host (server) closes last
    } else if (raceFrame > 0) {
        finish();                                                 // the race ended for us early
    }
    if (f > 60 * 150) QCoreApplication::quit();                   // safety net
}
