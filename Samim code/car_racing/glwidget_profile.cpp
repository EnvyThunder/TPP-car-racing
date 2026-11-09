// The player profile in the game (docs/cpu-psychology/README.md): loading and saving it per
// player name, blending in each finished race, the MY PROFILE screen in the menu and the F3
// overlay with the live measurements during a race. The measuring itself is in the shared
// simulation (shared/playermodel.*), so the online server measures its players the same way.
#include "glwidget.h"
#include "raceclient.h"
#include "protocol.h"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "gl1redirect.h"   // (the drawing goes through gl1shim: OpenGL ES 2 / WebGL)

namespace {

// a profile as a small JSON file (Phase B uploads the same file to the player database)
QJsonObject toJson(const sim::PlayerProfile &p)
{
    QJsonObject o;
    o["version"] = 1;
    o["name"] = QString::fromStdString(p.name);
    o["races"] = p.races;
    o["sure"] = double(p.sure);
    o["challenge"] = double(p.challenge);
    QJsonObject t;
    t["skill"] = double(p.traits.skill);
    t["consistency"] = double(p.traits.consistency);
    t["aggression"] = double(p.traits.aggression);
    t["risk"] = double(p.traits.risk);
    o["traits"] = t;
    QJsonArray habits;
    for (int m = 0; m < sim::PM_MAPS; ++m) {
        const sim::MapHabits &h = p.habits[size_t(m)];
        QJsonObject j;
        j["map"] = m;
        j["forkOffered"] = h.forkOffered; j["forkTaken"] = h.forkTaken;
        j["passLeft"] = h.passLeft; j["passRight"] = h.passRight;
        j["rampsOffered"] = h.rampsOffered; j["rampsTaken"] = h.rampsTaken;
        j["boosts"] = h.boosts;
        QJsonArray bins;
        for (int v : h.boostBins) bins.append(v);
        j["boostBins"] = bins;
        habits.append(j);
    }
    o["habits"] = habits;
    QJsonArray recent;
    for (const sim::RaceSummary &r : p.recent) {
        QJsonObject j;
        j["map"] = r.map; j["place"] = r.place; j["cars"] = r.cars; j["wrecks"] = r.wrecks; j["rams"] = r.rams;
        j["bestLap"] = double(r.bestLap); j["km"] = double(r.km); j["eliminated"] = r.eliminated;
        recent.append(j);
    }
    o["recent"] = recent;
    QJsonArray rivals;
    for (int k = 0; k < sim::PM_ROSTER; ++k) {
        const sim::RivalRecord &r = p.rivals[size_t(k)];
        QJsonObject j;
        j["races"] = r.races; j["ramsByYou"] = r.ramsByYou; j["ramsOnYou"] = r.ramsOnYou;
        j["youAhead"] = r.youAhead; j["theyAhead"] = r.theyAhead; j["close"] = r.close;
        rivals.append(j);
    }
    o["rivals"] = rivals;
    return o;
}

bool fromJson(const QJsonObject &o, sim::PlayerProfile &p)
{
    if (o["version"].toInt() != 1 || !o["traits"].isObject()) return false;
    p.races = std::max(0, o["races"].toInt());
    p.sure = std::clamp(float(o["sure"].toDouble()), 0.0f, 1.0f);
    p.challenge = std::clamp(float(o["challenge"].toDouble()), -0.4f, 1.0f);   // (older files: 0)
    const QJsonObject t = o["traits"].toObject();
    auto trait = [&](const char *k, float def) { return std::clamp(float(t[k].toDouble(def)), 0.0f, 1.0f); };
    p.traits.skill = trait("skill", 0.5f);
    p.traits.consistency = trait("consistency", 0.5f);
    p.traits.aggression = trait("aggression", 0.3f);
    p.traits.risk = trait("risk", 0.4f);
    for (const QJsonValue &v : o["habits"].toArray()) {
        const QJsonObject j = v.toObject();
        const int m = j["map"].toInt(-1);
        if (m < 0 || m >= sim::PM_MAPS) continue;
        sim::MapHabits &h = p.habits[size_t(m)];
        h.forkOffered = j["forkOffered"].toInt(); h.forkTaken = j["forkTaken"].toInt();
        h.passLeft = j["passLeft"].toInt(); h.passRight = j["passRight"].toInt();
        h.rampsOffered = j["rampsOffered"].toInt(); h.rampsTaken = j["rampsTaken"].toInt();
        h.boosts = j["boosts"].toInt();
        const QJsonArray bins = j["boostBins"].toArray();
        for (int k = 0; k < sim::PM_BOOST_BINS && k < bins.size(); ++k) h.boostBins[size_t(k)] = bins[k].toInt();
    }
    for (const QJsonValue &v : o["recent"].toArray()) {
        const QJsonObject j = v.toObject();
        sim::RaceSummary r;
        r.map = j["map"].toInt(); r.place = j["place"].toInt(); r.cars = j["cars"].toInt();
        r.wrecks = j["wrecks"].toInt(); r.rams = j["rams"].toInt();
        r.bestLap = float(j["bestLap"].toDouble()); r.km = float(j["km"].toDouble());
        r.eliminated = j["eliminated"].toBool();
        if (p.recent.size() < 10) p.recent.push_back(r);
    }
    const QJsonArray rivals = o["rivals"].toArray();
    for (int k = 0; k < sim::PM_ROSTER && k < rivals.size(); ++k) {
        const QJsonObject j = rivals[k].toObject();
        sim::RivalRecord &r = p.rivals[size_t(k)];
        r.races = j["races"].toInt(); r.ramsByYou = j["ramsByYou"].toInt(); r.ramsOnYou = j["ramsOnYou"].toInt();
        r.youAhead = j["youAhead"].toInt(); r.theyAhead = j["theyAhead"].toInt(); r.close = j["close"].toInt();
    }
    return true;
}

} // namespace

QString profilePath(const std::string &name)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles";
    QDir().mkpath(dir);
    QString file = QString::fromStdString(name);
    file.replace(' ', '_');
    return dir + "/" + (file.isEmpty() ? QString("PLAYER") : file) + ".json";
}

// The skill the CPUs spread around: your profile's (trusted as far as it is sure; a new
// profile: an average driver), turned into the CPU precision that drives at that pace
float GLWidget::cpuSkillFor(const sim::PlayerProfile &p) const
{
    return sim::cpuCentreFor(p.traits.skill, p.sure, 0.0f);
}

// What the race director may know of a person: their skill, their habits on this map and in
// general, as far as the profile is sure
sim::PlayerHints GLWidget::hintsFor(const sim::PlayerProfile &p, int map)
{
    sim::PlayerHints h;
    if (p.races == 0) return h;
    h.known = true;
    h.sure = p.sure;
    h.skill = p.traits.skill;
    h.aggression = p.traits.aggression;
    h.risk = p.traits.risk;
    int left = 0, right = 0, offered = 0, taken = 0;
    for (const sim::MapHabits &mh : p.habits) { left += mh.passLeft; right += mh.passRight; offered += mh.rampsOffered; taken += mh.rampsTaken; }
    if (left + right >= 6) h.passLeft = float(left) / float(left + right);
    if (offered >= 5) h.ramps = float(taken) / float(offered);
    for (int k = 0; k < sim::ROSTER_SIZE; ++k) h.attitude[size_t(k)] = int8_t(p.attitude(k));
    h.nemesis = p.nemesis();
    if (map >= 0 && map < sim::PM_MAPS) {
        const sim::MapHabits &mh = p.habits[size_t(map)];
        if (mh.forkOffered >= 3) h.shortcut = float(mh.forkTaken) / float(mh.forkOffered);
        if (mh.boosts >= 4) {
            int best = 0;
            for (int k = 1; k < sim::PM_BOOST_BINS; ++k) if (mh.boostBins[size_t(k)] > mh.boostBins[size_t(best)]) best = k;
            if (mh.boostBins[size_t(best)] * 100 >= 35 * mh.boosts) h.boostBin = best;
        }
    }
    return h;
}

// Your name (the one you use online; PLAYER until you set one) - player 2 in split screen is PLAYER 2
std::string GLWidget::profileName(int player) const
{
    if (player == 1) return "PLAYER 2";
    if (!accountUser.isEmpty()) return accountUser.toStdString();   // logged in: your account's profile
    const QString n = net::cleanName(QSettings("TPP", "CarRacing").value("online/name").toString());
    return (n.isEmpty() ? QString("PLAYER") : n).toStdString();
}

sim::PlayerProfile GLWidget::loadProfile(const std::string &name) const
{
    sim::PlayerProfile p;
    p.name = name;
    QFile f(profilePath(name));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        sim::PlayerProfile read;
        read.name = name;
        if (doc.isObject() && fromJson(doc.object(), read)) p = read;   // (a damaged file: start afresh)
    }
    return p;
}

void GLWidget::saveProfile(const sim::PlayerProfile &p) const
{
    if (!autoShotDir.isEmpty() && p.name.rfind("TEST", 0) != 0) return;   // (scripted tests never touch your profile)
    QFile f(profilePath(p.name));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(toJson(p)).toJson(QJsonDocument::Indented));
}

// A race has finished (vs CPU or split screen): each person's race is blended into their
// profile. Online the server measures its players itself.
void GLWidget::profileAfterRace()
{
    if (isOnline()) return;
    const auto &cars = simEngine.getCars();
    const int people = simEngine.getGameMode() == sim::GameSim::ModeSplitScreen ? 2 : 1;
    for (int i = 0; i < people && i < int(cars.size()); ++i) {
        const sim::Car &c = cars[size_t(i)];
        const sim::RaceObs &o = simEngine.raceObs(i);
        sim::RaceSummary s;
        s.map = simEngine.getMap();
        s.place = simEngine.rankOf(i);
        s.cars = int(cars.size());
        s.wrecks = c.wrecks;
        s.rams = c.crashesCaused;
        s.bestLap = c.bestLap;
        s.km = o.metres / 1000.0f;
        s.eliminated = c.out;
        sim::PlayerProfile p = loadProfile(profileName(i));
        p.addRace(o, s, simEngine.rivalRaces(i));
        saveProfile(p);
        if (i == 0) cloneAfterRace();                     // your clone learns this race
        if (i == 0 && !accountToken.isEmpty()) {          // ... and the server gets it: profile, clone, this race
            accountPushAll();
            QJsonObject r;
            r["map"] = s.map;
            r["place"] = std::clamp(s.place, 1, 6);
            r["cars"] = std::clamp(s.cars, 1, 6);
            const float best = simEngine.sprint() ? (c.finished ? c.finishTime : 0.0f) : c.bestLap;   // (a sprint: its time)
            r["bestLap"] = double(std::clamp(best, 0.0f, 600.0f));
            r["wrecks"] = std::min(c.wrecks, 999);
            r["rams"] = std::min(c.crashesCaused, 999);
            r["km"] = double(std::clamp(s.km, 0.0f, 200.0f));
            r["skill"] = double(std::clamp(sim::raceTraits(o).skill, 0.0f, 1.0f));
            if (accountShare && cloneLastRace >= 0) {    // (you share your races for training: the recording too)
                const std::string b = cloneBook.bytes(cloneLastRace);
                r["recording"] = QString::fromLatin1(QByteArray(b.data(), qsizetype(b.size())).toBase64());
            }
            accountRaceUpload(r);
        }
        if (i == 0)                                      // your rival and the head-to-head, for the results
            for (int j = 0; j < int(cars.size()); ++j) {
                const sim::Car &r = cars[size_t(j)];
                if (r.dirRole != 1 || r.dirRivalOf != 0 || r.rosterId < 0) continue;
                const sim::RivalRecord &rec = p.rivals[size_t(r.rosterId)];
                static const char *ATT[4] = {"", "   GRUDGE", "   DETERMINED", "   COCKY"};
                char buf[96];
                snprintf(buf, sizeof buf, "YOUR RIVAL %s:  YOU %d - %d%s%s", sim::rosterDriver(r.rosterId).name, rec.youAhead, rec.theyAhead,
                         p.nemesis() == r.rosterId ? "   NEMESIS" : "", ATT[std::clamp(p.attitude(r.rosterId), 0, 3)]);
                rivalSummary = buf;
            }
    }
}

// MY PROFILE: your traits as bars, your habits in words, your last 10 races
void GLWidget::drawProfileScreen(int W, int H, float dpr)
{
    const sim::PlayerProfile &p = shownProfile;          // (read when the screen opened)
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    const float tps = ps * 1.1f, hps = ps * 2.0f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    rect(0, 0, float(W), float(H));
    const float bx0 = float(W) * 0.10f, bx1 = float(W) * 0.90f, by0 = float(H) * 0.07f, by1 = float(H) * 0.93f;
    glColor4f(0.03f, 0.05f, 0.10f, 0.95f);
    rect(bx0, by0, bx1, by1);
    glColor4f(0.35f, 0.85f, 1.0f, 0.95f);
    rect(bx0, by1 - 4.0f * dpr, bx1, by1);
    rect(bx0, by0, bx1, by0 + 2.0f * dpr);

    char buf[160];
    float y = by1 - 18.0f * dpr - 7.0f * hps;
    snprintf(buf, sizeof buf, "MY PROFILE: %s", p.name.c_str());
    drawText(buf, bx0 + 24.0f * dpr, y, hps, 0.35f, 0.85f, 1.0f);
    snprintf(buf, sizeof buf, "%d RACE%s MEASURED   PROFILE %d%% SURE", p.races, p.races == 1 ? "" : "S", int(std::lround(p.sure * 100.0f)));
    drawText(buf, bx1 - 24.0f * dpr - textWidth(buf, ps), y + 2.0f * ps, ps, 0.75f, 0.80f, 0.90f);
    {
        QByteArray acct = accountLine() + (accountUser.isEmpty() ? "   L: LOG IN / CREATE ACCOUNT" : "   L: LOG OUT");
        if (!accountUser.isEmpty()) acct += accountShare ? "   SHARING RACES (S)" : "   NOT SHARING RACES (S)";
        drawText(acct.constData(), bx1 - 24.0f * dpr - textWidth(acct.constData(), ps * 0.85f), y - 10.0f * dpr - 2.0f * ps, ps * 0.85f,
                 accountUser.isEmpty() ? 0.70f : 0.45f, accountUser.isEmpty() ? 0.75f : 0.95f, accountUser.isEmpty() ? 0.85f : 0.65f);
    }
    y -= 16.0f * dpr + 7.0f * tps;

    const float colW = (bx1 - bx0 - 72.0f * dpr) * 0.5f;
    const float lx = bx0 + 24.0f * dpr, rx = lx + colW + 24.0f * dpr;
    if (p.races == 0) {
        const char *t = "DRIVE A RACE (VS CPU OR SPLIT SCREEN) AND YOUR PROFILE STARTS HERE.";
        drawText(t, lx, y, tps, 0.95f, 0.95f, 0.98f);
        y -= 7.0f * tps + 14.0f * dpr;
    }
    // the traits: a bar each (0..1)
    struct Bar { const char *name, *low, *high; float v; float r, g, b; };
    const Bar bars[4] = {
        {"SKILL", "LEARNING", "EXPERT", p.traits.skill, 0.35f, 0.95f, 0.45f},
        {"CONSISTENCY", "UP AND DOWN", "LIKE CLOCKWORK", p.traits.consistency, 0.35f, 0.80f, 1.0f},
        {"AGGRESSION", "CLEAN", "RAMS AND BLOCKS", p.traits.aggression, 1.0f, 0.40f, 0.30f},
        {"RISK", "PLAYS IT SAFE", "DAREDEVIL", p.traits.risk, 1.0f, 0.75f, 0.25f},
    };
    float ly = y;
    for (const Bar &b : bars) {
        drawText(b.name, lx, ly, tps, 0.95f, 0.95f, 0.98f);
        const float barY = ly - 6.0f * dpr - 5.0f * ps, barX1 = lx + colW;
        glColor4f(0.15f, 0.18f, 0.24f, 1.0f);
        rect(lx, barY, barX1, barY + 4.0f * ps);
        glColor4f(b.r, b.g, b.b, 1.0f);
        rect(lx, barY, lx + (barX1 - lx) * std::clamp(b.v, 0.0f, 1.0f), barY + 4.0f * ps);
        drawText(b.low, lx, barY - 3.0f * dpr - 7.0f * ps * 0.8f, ps * 0.8f, 0.55f, 0.60f, 0.70f);
        drawText(b.high, barX1 - textWidth(b.high, ps * 0.8f), barY - 3.0f * dpr - 7.0f * ps * 0.8f, ps * 0.8f, 0.55f, 0.60f, 0.70f);
        snprintf(buf, sizeof buf, "%d", int(std::lround(b.v * 100.0f)));
        drawText(buf, barX1 - textWidth(buf, tps), ly, tps, b.r, b.g, b.b);
        ly -= 7.0f * tps + 34.0f * dpr + 9.0f * ps;
    }
    // your clone: how far it is trained, how well it copies you, and how to delete it
    drawText("YOUR CLONE", lx, ly, tps, 1.0f, 0.80f, 0.25f);
    ly -= 7.0f * tps + 8.0f * dpr;
    if (cloneReady)
        snprintf(buf, sizeof buf, "LEARNED FROM %d RACES - DOES WHAT YOU DO %d%% OF THE TIME", cloneStats.races,
                 int(std::lround(cloneStats.agree() * 100.0f)));
    else if (cloneTraining)
        snprintf(buf, sizeof buf, "TRAINING...");
    else
        snprintf(buf, sizeof buf, "DRIVE %d MORE VS CPU RACE%s AND IT LEARNS YOUR DRIVING",
                 sim::CloneBook::MIN_RACES - std::min(cloneBook.races(), sim::CloneBook::MIN_RACES),
                 sim::CloneBook::MIN_RACES - cloneBook.races() == 1 ? "" : "S");
    {
        float s = ps;
        while (s > ps * 0.6f && textWidth(buf, s) > colW) s *= 0.93f;
        drawText(buf, lx, ly, s, 0.95f, 0.95f, 0.98f);
        ly -= 7.0f * s + 8.0f * dpr;
    }
    if (cloneBook.races() > 0 || cloneReady) {
        const char *r = cloneResetArmed ? "PRESS R AGAIN TO DELETE YOUR CLONE AND RECORDINGS" : "R: RESET YOUR CLONE (ITS FILES STAY ON THIS PC)";
        drawText(r, lx, ly, ps * 0.85f, cloneResetArmed ? 1.0f : 0.55f, cloneResetArmed ? 0.45f : 0.60f, cloneResetArmed ? 0.35f : 0.70f);
    }
    // habits, in words
    const char *names[sim::PM_MAPS];
    for (int m = 0; m < sim::PM_MAPS; ++m) names[m] = sim::MAPS[std::min(m, sim::NUM_MAPS - 1)].name;
    const std::vector<std::string> habits = p.habitLines(names);
    float ry = y;
    drawText("HABITS", rx, ry, tps, 1.0f, 0.80f, 0.25f);
    ry -= 7.0f * tps + 8.0f * dpr;
    if (habits.empty()) {
        drawText("NONE YET - A FEW MORE RACES", rx, ry, ps, 0.70f, 0.75f, 0.85f);
        ry -= 7.0f * ps + 8.0f * dpr;
    }
    for (size_t k = 0; k < habits.size() && k < 6; ++k) {
        std::string line = habits[k];
        float s = ps;
        while (s > ps * 0.6f && textWidth(line.c_str(), s) > colW) s *= 0.92f;
        drawText(line.c_str(), rx, ry, s, 0.95f, 0.95f, 0.98f);
        ry -= 7.0f * ps + 8.0f * dpr;
    }
    // your rivals among the named drivers: the head-to-head, and what they think of you
    {
        std::vector<int> order;
        for (int k = 0; k < sim::PM_ROSTER; ++k) if (p.rivals[size_t(k)].races > 0) order.push_back(k);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return p.rivals[size_t(a)].races > p.rivals[size_t(b)].races; });
        if (!order.empty()) {
            ry -= 10.0f * dpr;
            drawText("RIVALS", rx, ry, tps, 1.0f, 0.80f, 0.25f);
            ry -= 7.0f * tps + 8.0f * dpr;
            static const char *ATT[4] = {"", " GRUDGE", " DETERMINED", " COCKY"};
            for (size_t k = 0; k < order.size() && k < 4; ++k) {
                const sim::RivalRecord &r = p.rivals[size_t(order[k])];
                snprintf(buf, sizeof buf, "%-10s YOU %d - %d  RAMS %d/%d%s%s", sim::rosterDriver(order[k]).name, r.youAhead, r.theyAhead,
                         r.ramsByYou, r.ramsOnYou, p.nemesis() == order[k] ? " NEMESIS" : "", ATT[std::clamp(p.attitude(order[k]), 0, 3)]);
                float s = ps * 0.9f;
                while (s > ps * 0.55f && textWidth(buf, s) > colW) s *= 0.92f;
                drawText(buf, rx, ry, s, 0.95f, 0.95f, 0.98f);
                ry -= 7.0f * s + 7.0f * dpr;
            }
        }
    }
    // the last races
    ry -= 10.0f * dpr;
    drawText("LAST RACES", rx, ry, tps, 1.0f, 0.80f, 0.25f);
    ry -= 7.0f * tps + 8.0f * dpr;
    static const char *ORD[6] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH"};
    for (const sim::RaceSummary &r : p.recent) {
        if (ry < by0 + 30.0f * dpr) break;
        char best[16] = "-";
        if (r.bestLap > 0.0f) snprintf(best, sizeof best, "%d:%04.1f", int(r.bestLap) / 60, r.bestLap - 60.0f * float(int(r.bestLap) / 60));
        snprintf(buf, sizeof buf, "%-16s %s/%d  WRECKS %d  RAMS %d  BEST %s", sim::MAPS[std::clamp(r.map, 0, sim::NUM_MAPS - 1)].name,
                 r.eliminated ? "OUT" : ORD[std::clamp(r.place, 1, 6) - 1], r.cars, r.wrecks, r.rams, best);
        float s = ps * 0.9f;
        while (s > ps * 0.55f && textWidth(buf, s) > colW) s *= 0.92f;
        drawText(buf, rx, ry, s, r.place == 1 && !r.eliminated ? 1.0f : 0.85f, r.place == 1 && !r.eliminated ? 0.88f : 0.88f,
                 r.place == 1 && !r.eliminated ? 0.40f : 0.95f);
        ry -= 7.0f * s + 7.0f * dpr;
    }
    const char *keys = "ANY KEY: CLOSE   (THE CPUS ADAPT TO THIS PROFILE)";
    drawText(keys, (bx0 + bx1 - textWidth(keys, ps * 0.9f)) * 0.5f, by0 + 12.0f * dpr, ps * 0.9f, 0.35f, 0.85f, 1.0f);
}

// F3 during a race: what the player model sees of you right now
void GLWidget::drawAiDebug(int W, int H, float dpr)
{
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const float ps = std::clamp(float(H) / 520.0f, 1.1f, 1.8f);
    const float w = 250.0f * ps, x0 = float(W) - w - 12.0f * dpr;
    float y = float(H) * 0.70f;
    const int people = isOnline() ? 1 : simEngine.getGameMode() == sim::GameSim::ModeSplitScreen ? 2 : 1;
    const int rows = isOnline() ? 3 : 9;
    glColor4f(0.02f, 0.04f, 0.08f, 0.82f);
    rect(x0, y - float(rows * people) * 9.0f * ps - 8.0f * dpr, float(W) - 12.0f * dpr, y + 9.0f * ps + 6.0f * dpr);
    char buf[96];
    for (int i = 0; i < people; ++i) {
        const int idx = isOnline() ? std::max(0, mySlot) : i;
        snprintf(buf, sizeof buf, "PLAYER MODEL: %s", profileName(isOnline() ? 0 : i).c_str());
        drawText(buf, x0 + 6.0f * dpr, y, ps, 0.35f, 0.85f, 1.0f);
        y -= 9.0f * ps;
        if (isOnline()) {
            drawText("ONLINE: MEASURED ON THE SERVER", x0 + 6.0f * dpr, y, ps, 0.80f, 0.80f, 0.85f);
            y -= 9.0f * ps * 2.0f;
            continue;
        }
        const sim::RaceObs &o = simEngine.raceObs(idx);
        const sim::RaceTraits t = sim::raceTraits(o);
        auto line = [&](const char *name, float v, float r, float g, float b) {
            drawText(name, x0 + 6.0f * dpr, y, ps, 0.85f, 0.88f, 0.95f);
            const float bx = x0 + 100.0f * ps, bw = w - 140.0f * ps;
            glColor4f(0.15f, 0.18f, 0.24f, 1.0f);
            rect(bx, y, bx + bw, y + 6.0f * ps);
            glColor4f(r, g, b, 1.0f);
            rect(bx, y, bx + bw * std::clamp(v, 0.0f, 1.0f), y + 6.0f * ps);
            snprintf(buf, sizeof buf, "%.2f", double(v));
            drawText(buf, bx + bw + 6.0f * dpr, y, ps, r, g, b);
            y -= 9.0f * ps;
        };
        line("SKILL", t.skill, 0.35f, 0.95f, 0.45f);
        line("AGGRESSION", t.aggression, 1.0f, 0.40f, 0.30f);
        line("RISK", t.risk, 1.0f, 0.75f, 0.25f);
        line("TILT", o.tilt, 1.0f, 0.30f, 0.60f);
        line("CONFIDENCE", o.confidence, 0.35f, 0.80f, 1.0f);
        snprintf(buf, sizeof buf, "%.1f KM  RAMS %d  BLOCKS %d  CLOSE %d", double(o.metres / 1000.0f), o.rams, o.blocks, o.closePasses);
        drawText(buf, x0 + 6.0f * dpr, y, ps * 0.9f, 0.70f, 0.75f, 0.85f);
        y -= 9.0f * ps;
        snprintf(buf, sizeof buf, "RAMPS %d/%d  BOOSTS %d  MISTAKES %d", o.rampsTaken, o.rampsOffered, o.boosts,
                 o.rail + o.offroad + o.barrier + o.objects + o.traffic + o.roughLandings + o.crashLandings + o.flewOff);
        drawText(buf, x0 + 6.0f * dpr, y, ps * 0.9f, 0.70f, 0.75f, 0.85f);
        y -= 9.0f * ps * 2.0f;
    }
    (void)dpr;
}

// F4: every CPU's mind - personality, mood (and who it is after), what it is doing, skill
void GLWidget::drawCpuMinds(int W, int H, float dpr)
{
    const auto &cars = simEngine.getCars();
    std::vector<int> cpus;
    for (int i = 0; i < int(cars.size()); ++i) if (cars[size_t(i)].aiLevel > 0 && cars[size_t(i)].aiPersona >= 0) cpus.push_back(i);
    if (cpus.empty()) return;
    begin2D(W, H);
    const float ps = std::clamp(float(H) / 560.0f, 1.0f, 1.7f);
    const float rowH = 9.0f * ps, w = 470.0f * ps, x0 = float(W) - w - 12.0f * dpr;
    float y = float(H) * 0.62f;
    glColor4f(0.02f, 0.04f, 0.08f, 0.82f);
    glBegin(GL_QUADS);
    const float y0 = y - rowH * float(cpus.size()) - 6.0f * dpr, y1 = y + rowH + 6.0f * dpr;
    glVertex2f(x0, y0); glVertex2f(float(W) - 12.0f * dpr, y0); glVertex2f(float(W) - 12.0f * dpr, y1); glVertex2f(x0, y1);
    glEnd();
    drawText("CPU MINDS (F4)", x0 + 6.0f * dpr, y, ps, 1.0f, 0.80f, 0.25f);
    y -= rowH;
    static const char *ORD[6] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH"};
    for (int i : cpus) {
        const sim::Car &c = cars[size_t(i)];
        char mood[48];
        if (c.aiMood == sim::MoodRevenge && c.aiMoodTarget >= 0)
            snprintf(mood, sizeof mood, "REVENGE>%s", carName(c.aiMoodTarget).constData());
        else
            snprintf(mood, sizeof mood, "%s", sim::moodName(c.aiMood));
        char role[40] = "";
        if (c.dirRole == 1 && c.dirRivalOf >= 0) snprintf(role, sizeof role, "RIVAL>%s ", carName(c.dirRivalOf).constData());
        if (c.dirMode != sim::DirNone) snprintf(role + std::strlen(role), sizeof role - std::strlen(role), "%s", sim::directorModeShort(c.dirMode));
        // its brain: the species, and who drives right now (NET the brain, RUL the rules in a
        // tricky moment, SAFE while the safety net has taken over)
        char brain[24] = "RULES";
        if (c.aiBrain >= 0)
            snprintf(brain, sizeof brain, "%.4s:%s", sim::brainName(c.aiBrain), c.aiBrainOff > 0 ? "SAFE" : c.aiBrainDriving ? "NET" : "RUL");
        char buf[180];
        snprintf(buf, sizeof buf, "%-6s %-12s %-10s %-14s %-6s %3d %-3s %s", carName(i).constData(), sim::personaDef(c.aiPersona).name, brain, mood,
                 sim::tacticShort(c.aiTactic), int(std::lround(c.aiSkill * 100.0f)), ORD[std::clamp(simEngine.rankOf(i), 1, 6) - 1], role);
        float s = ps;
        while (s > ps * 0.6f && textWidth(buf, s) > w - 12.0f * dpr) s *= 0.93f;
        drawText(buf, x0 + 6.0f * dpr, y, s, 0.90f, 0.92f, 0.98f);
        y -= rowH;
    }
}

// A named driver's line to you: a bubble under the top bar, edged in their car's colour
void GLWidget::drawTauntBubble(int vw, int vh, float ps)
{
    if (tauntUntil < simEngine.getTick() || tauntText.isEmpty()) return;
    const float s = ps * 1.15f, w = textWidth(tauntText.constData(), s) + 20.0f * ps, h = 7.0f * s + 10.0f * ps;
    const float x0 = (float(vw) - w) * 0.5f, y0 = float(vh) * 0.80f;
    const float fade = std::clamp(float(tauntUntil - simEngine.getTick()) / 30.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glColor4f(0.03f, 0.05f, 0.10f, 0.85f * fade);
    glVertex2f(x0, y0); glVertex2f(x0 + w, y0); glVertex2f(x0 + w, y0 + h); glVertex2f(x0, y0 + h);
    glColor4f(tauntCol[0], tauntCol[1], tauntCol[2], fade);
    glVertex2f(x0, y0); glVertex2f(x0 + 4.0f * ps, y0); glVertex2f(x0 + 4.0f * ps, y0 + h); glVertex2f(x0, y0 + h);
    glEnd();
    drawText(tauntText.constData(), x0 + 12.0f * ps, y0 + 5.0f * ps, s, 1.0f, 1.0f, 1.0f);
}

// On the results: your rival and the head-to-head
void GLWidget::drawRivalLine(int W, int H, float dpr)
{
    if (rivalSummary.isEmpty() && cloneSummary.isEmpty()) return;
    begin2D(W, H);
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    if (!cloneSummary.isEmpty()) {                      // what your clone learned (above the rival line)
        const QByteArray &t = cloneSummary;
        const float w = textWidth(t.constData(), ps) + 24.0f * dpr, x0 = (float(W) - w) * 0.5f;
        const float y0 = 54.0f * dpr + (rivalSummary.isEmpty() ? 0.0f : 7.0f * ps + 18.0f * dpr);
        glColor4f(0.03f, 0.05f, 0.10f, 0.90f);
        glBegin(GL_QUADS);
        glVertex2f(x0, y0); glVertex2f(x0 + w, y0); glVertex2f(x0 + w, y0 + 7.0f * ps + 12.0f * dpr); glVertex2f(x0, y0 + 7.0f * ps + 12.0f * dpr);
        glEnd();
        drawText(t.constData(), x0 + 12.0f * dpr, y0 + 6.0f * dpr, ps, 0.45f, 0.95f, 0.65f);
        if (rivalSummary.isEmpty()) return;
    }
    const float w = textWidth(rivalSummary.constData(), ps) + 24.0f * dpr, x0 = (float(W) - w) * 0.5f, y0 = 54.0f * dpr;
    glColor4f(0.03f, 0.05f, 0.10f, 0.90f);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x0 + w, y0); glVertex2f(x0 + w, y0 + 7.0f * ps + 12.0f * dpr); glVertex2f(x0, y0 + 7.0f * ps + 12.0f * dpr);
    glEnd();
    drawText(rivalSummary.constData(), x0 + 12.0f * dpr, y0 + 6.0f * dpr, ps, 1.0f, 0.80f, 0.30f);
}

// ---- The clone of your driving (shared/clone.h) ------------------------------------------
// Files next to your profile: <name>.clone (your recordings), <name>.brain (the clone)
static std::string clonePath(const std::string &name, const char *ext)
{
    QString p = profilePath(name);
    p.chop(5);                                           // (".json")
    return (p + ext).toStdString();
}
std::string clonePathFor(const std::string &name, const char *ext) { return clonePath(name, ext); }

void GLWidget::cloneLoad()
{
    const std::string name = cloneOwner();
    cloneBook.load(clonePath(name, ".clone"));
    cloneReady = sim::loadCloneBrain(clonePath(name, ".brain"), cloneBrain, cloneStats);
}

// vs CPU, offline: record you, and with a trained clone (and YOUR CLONE on) the last CPU is it
void GLWidget::cloneSetupRace(int mode)
{
    cloneRecording = false;
    cloneLastRace = -1;
    cloneSummary.clear();
    simEngine.setCloneBrain(nullptr);
    if (mode != sim::GameSim::ModeSingle || isOnline()) return;
    if (!cloneTraining) cloneLoad();                     // (this race's recording starts from what is saved)
    cloneBook.beginRace();
    cloneRec.reset();
    cloneRecording = autoShotDir.isEmpty() || !cloneName.empty();   // (scripted tests record only a test profile)
    auto &cars = simEngine.getCars();
    if (!cloneOn || !cloneReady || cars.size() < 2 || story != StoryScreen::Off) return;
    const size_t idx = cars.size() - 1;
    sim::Car &c = cars[idx];
    const sim::PlayerProfile p = loadProfile(cloneOwner());
    c.aiClone = true;
    c.rosterId = -1;
    c.aiPersona = p.traits.aggression > 0.45f ? sim::PersonaAttacker : p.traits.risk > 0.55f ? sim::PersonaShowOff : sim::PersonaClean;
    c.aiSkill = cpuSkillFor(p);                          // (the rules' moments at your pace, as sure as your profile; the clone itself is you)
    c.r = 0.55f * cars[0].r + 0.25f; c.g = 0.55f * cars[0].g + 0.25f; c.b = 0.55f * cars[0].b + 0.25f;   // your colour, paler
    if (idx < nameOverride.size()) nameOverride[idx] = "YOUR CLONE";
    simEngine.setCloneBrain(&cloneBrain);
}

// the race is over: keep its recording, and from 3 races on train the clone in the background
void GLWidget::cloneAfterRace()
{
    if (!cloneRecording) return;
    cloneRecording = false;
    cloneBook.dropEmptyRace();
    cloneLastRace = -1;
    for (const sim::CloneSample &cs : cloneBook.samples)
        if (cs.race == cloneBook.raceCounter) { cloneLastRace = cloneBook.raceCounter; break; }
    cloneBook.save(clonePath(cloneOwner(), ".clone"));
    if (cloneBook.races() < sim::CloneBook::MIN_RACES || cloneTraining) return;
    if (cloneThread.joinable()) cloneThread.join();
    cloneTraining = true;
    cloneSummary = "YOUR CLONE IS LEARNING THIS RACE...";
    const sim::CloneBook book = cloneBook;               // (the thread trains on its own copy)
#ifdef __EMSCRIPTEN__
    // (the browser has no threads: clonePoll trains it a few ms every frame)
    if (!cloneSlices.begin(book, sim::bundledBrain(sim::BrainRacer))) {
        cloneNewStats = cloneSlices.st;
        cloneDone = true;
    }
#else
    cloneThread = std::thread([this, book]() {
        sim::trainClone(book, sim::bundledBrain(sim::BrainRacer), cloneNew, cloneNewStats);
        cloneDone = true;
    });
#endif
}

void GLWidget::clonePoll()
{
#ifdef __EMSCRIPTEN__
    if (cloneSlices.busy()) {
        const bool racing = simEngine.getState() == State::Racing;   // (less while you drive)
        if (cloneSlices.run(racing ? 2.0 : 6.0)) {
            cloneNew = cloneSlices.out;
            cloneNewStats = cloneSlices.st;
            cloneDone = true;
        }
    }
#endif
    if (!cloneDone) return;
    if (cloneThread.joinable()) cloneThread.join();
    cloneDone = false;
    cloneTraining = false;
    if (cloneNewStats.races < sim::CloneBook::MIN_RACES) return;   // (training failed: keep the old one)
    cloneBrain = cloneNew;
    cloneStats = cloneNewStats;
    cloneReady = true;
    sim::saveCloneBrain(clonePath(cloneOwner(), ".brain"), cloneBrain, cloneStats);
    if (!accountToken.isEmpty() && cloneName.empty()) accountPushAll();   // (your account keeps it too)
    char buf[96];
    snprintf(buf, sizeof buf, "YOUR CLONE LEARNED FROM %d RACES - DOES WHAT YOU DO %d%% OF THE TIME", cloneStats.races,
             int(std::lround(cloneStats.agree() * 100.0f)));
    cloneSummary = buf;
}

void GLWidget::cloneReset()
{
    if (cloneTraining) return;                           // (not while it trains)
    const std::string name = cloneOwner();
    QFile::remove(QString::fromStdString(clonePath(name, ".clone")));
    QFile::remove(QString::fromStdString(clonePath(name, ".brain")));
    cloneBook = sim::CloneBook();
    cloneReady = false;
    cloneStats = sim::CloneStats();
}
