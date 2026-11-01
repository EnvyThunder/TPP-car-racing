// Your account on the game server (car-racing-backend, docs/accounts/README.md): the LOG IN /
// CREATE ACCOUNT / PLAY OFFLINE screen, and keeping your profile and clone in step with the
// server. The game plays fully offline without it. Only the session token is kept on this PC
// (QSettings) - never the password.
#include "glwidget.h"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include "gl1redirect.h"   // (the drawing goes through gl1shim: OpenGL ES 2 / WebGL)

QString profilePath(const std::string &name);                    // (glwidget_profile.cpp)
std::string clonePathFor(const std::string &name, const char *ext);

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The page the game runs in: its own site (https://...), or ?api=... - only a server on this
// computer (localhost), so no link can send your login anywhere else. Empty on a test page.
EM_JS(char *, tpp_page_api, (), {
    var out = "";
    try {
        var asked = new URLSearchParams(location.search).get("api") || "";
        var local = function (u) { try { var h = new URL(u).hostname; return h === "localhost" || h === "127.0.0.1"; } catch (e) { return false; } };
        if (asked && local(asked)) { out = asked; while (out.endsWith("/")) out = out.slice(0, -1); }
        else if (location.protocol === "https:" && !local(location.href)) out = location.origin;
    } catch (e) {}
    return stringToNewUTF8(out);
});
#endif

// The server: the live one on AWS; CAR_RACING_API (or the setting api/base) points elsewhere
// (a server on this PC for tests). In the browser: the site the page came from.
QString GLWidget::apiBase() const
{
    const QString env = qEnvironmentVariable("CAR_RACING_API");
    if (!env.isEmpty()) return env;
#ifdef __EMSCRIPTEN__
    char *page = tpp_page_api();
    const QString fromPage = QString::fromUtf8(page);
    free(page);
    if (!fromPage.isEmpty()) return fromPage;
#endif
    return QSettings("TPP", acctApp()).value("api/base", "https://13-49-109-8.sslip.io").toString();
}

// scripted tests keep their account apart from yours
QString GLWidget::acctApp() const { return autoShotDir.isEmpty() ? "CarRacing" : "CarRacingTest"; }

void GLWidget::apiCall(const char *method, const QString &path, const QJsonObject &body,
                       std::function<void(int, const QJsonObject &)> done)
{
    if (!api) api = new QNetworkAccessManager(this);
    QNetworkRequest rq(QUrl(apiBase() + path));
    rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    rq.setTransferTimeout(15000);
    if (!accountToken.isEmpty()) rq.setRawHeader("Authorization", "Bearer " + accountToken.toUtf8());
    const QByteArray data = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *r = api->sendCustomRequest(rq, method, data);
    connect(r, &QNetworkReply::finished, this, [r, done]() {
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();   // 0: no answer
        const QJsonObject json = QJsonDocument::fromJson(r->readAll()).object();
        r->deleteLater();
        if (done) done(status, json);
    });
}

// ---- Who you are ---------------------------------------------------------------------------

void GLWidget::accountLoad()
{
    QSettings s("TPP", acctApp());
    accountUser = s.value("account/username").toString();
    accountToken = s.value("account/token").toString();
    if (accountToken.isEmpty()) {
        accountUser.clear();
        // first start without an account: the screen comes up (not in scripted tests)
        if (autoShotDir.isEmpty() && netTest.isEmpty()) openAccountScreen(false);
        return;
    }
    syncState = SyncState::Syncing;
    apiCall("GET", "/api/me", {}, [this](int status, const QJsonObject &j) {
        if (status == 200) {
            accountShare = j["shareRaces"].toBool();
            if (QSettings("TPP", acctApp()).value("account/pending").toBool()) accountPushAll();
            else syncState = SyncState::Synced;
            accountFlushRaces();                         // (races played while offline)
        } else if (status == 401) {                      // the session ran out (30 days) or was ended
            accountForget();
            acctStatus = "YOUR SESSION ENDED - PLEASE LOG IN AGAIN";
            acctStatusBad = true;
        } else {
            syncState = SyncState::Pending;              // offline: it syncs when the server answers again
        }
        update();
    });
}

void GLWidget::accountForget()
{
    QSettings s("TPP", acctApp());
    s.remove("account/token");
    s.remove("account/username");
    s.remove("account/pending");
    accountUser.clear();
    accountToken.clear();
    syncState = SyncState::Off;
    cloneLoad();                                         // (back to this PC's own profile and clone)
}

void GLWidget::accountLoggedIn(const QString &user, const QString &token, bool share)
{
    accountUser = user;
    accountToken = token;
    accountShare = share;
    QSettings s("TPP", acctApp());
    s.setValue("account/username", user);
    s.setValue("account/token", token);
    acctPass.fill(QChar(0)); acctPass.clear();           // (the password goes no further)
    acctPass2.fill(QChar(0)); acctPass2.clear();
    accountScreen = AccountScreen::Off;
    cloneLoad();                                         // (your account's clone, if this PC has one)
    accountSyncAfterLogin();
}

void GLWidget::accountLogout()
{
    if (accountToken.isEmpty()) return;
    apiCall("POST", "/api/logout", {}, nullptr);         // (ends the session on the server too)
    accountForget();
}

// ---- Keeping your profile and clone in step ------------------------------------------------
// On login: whichever side has more races wins (the other is kept as a backup file on this
// PC). After every race and every new clone: upload. If the server can't be reached it is
// marked pending and sent at the next start or race.

static QJsonObject readJsonFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void GLWidget::accountSyncAfterLogin()
{
    syncState = SyncState::Syncing;
    const std::string name = accountUser.toStdString();
    apiCall("GET", "/api/profile", {}, [this, name](int status, const QJsonObject &j) {
        if (status != 200) { accountSyncFailed(); return; }
        const int local = loadProfile(name).races, remote = j["races"].toInt();
        const QString path = profilePath(name);
        if (remote > local && j["profile"].isObject()) {
            if (local > 0) QFile::copy(path, QString(path).replace(".json", ".local-backup.json"));
            QFile f(path);
            if ((autoShotDir.isEmpty() || name.rfind("TEST", 0) == 0) && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(QJsonDocument(j["profile"].toObject()).toJson(QJsonDocument::Indented));
        }
        apiCall("GET", "/api/clone", {}, [this, name, local, remote](int st2, const QJsonObject &c) {
            if (st2 != 200) { accountSyncFailed(); return; }
            sim::Brain b; sim::CloneStats cs;
            const bool have = sim::loadCloneBrain(clonePathFor(name, ".brain"), b, cs);
            const int cloneLocal = have ? cs.races : 0, cloneRemote = c["data"].isString() ? c["races"].toInt() : 0;
            if (cloneRemote > cloneLocal) {
                QFile f(QString::fromStdString(clonePathFor(name, ".brain")));
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::fromBase64(c["data"].toString().toLatin1()));
                f.close();
                cloneLoad();
            }
            // what this PC has more of goes up
            accountFlushRaces();
            if (local > remote || cloneLocal > cloneRemote) accountPushAll();
            else { syncState = SyncState::Synced; QSettings("TPP", acctApp()).remove("account/pending"); update(); }
        });
    });
}

void GLWidget::accountPushAll()
{
    if (accountToken.isEmpty()) return;
    syncState = SyncState::Syncing;
    const std::string name = accountUser.toStdString();
    const QJsonObject profile = readJsonFile(profilePath(name));
    auto pushClone = [this, name]() {
        sim::Brain b; sim::CloneStats cs;
        if (!sim::loadCloneBrain(clonePathFor(name, ".brain"), b, cs)) { accountSyncDone(); return; }
        QFile f(QString::fromStdString(clonePathFor(name, ".brain")));
        if (!f.open(QIODevice::ReadOnly)) { accountSyncDone(); return; }
        QJsonObject body;
        body["data"] = QString::fromLatin1(f.readAll().toBase64());
        body["races"] = cs.races;
        body["agree"] = double(std::clamp(cs.agree(), 0.0f, 1.0f));
        apiCall("PUT", "/api/clone", body, [this](int st, const QJsonObject &) { if (st == 200) accountSyncDone(); else accountSyncFailed(); });
    };
    if (profile.isEmpty()) { pushClone(); return; }
    QJsonObject body;
    body["profile"] = profile;
    apiCall("PUT", "/api/profile", body, [this, pushClone](int st, const QJsonObject &) {
        if (st == 200) pushClone(); else accountSyncFailed();
    });
}

void GLWidget::accountSyncDone()
{
    syncState = SyncState::Synced;
    QSettings("TPP", acctApp()).remove("account/pending");
    update();
}

void GLWidget::accountSyncFailed()
{
    syncState = SyncState::Pending;
    QSettings("TPP", acctApp()).setValue("account/pending", true);
    update();
}

// ---- The screen ----------------------------------------------------------------------------
// rows: the fields, then the buttons (CREATE adds CONFIRM PASSWORD and SHARE MY RACES)
enum AcctRow { RowUser, RowPass, RowPass2, RowShare, RowGo, RowSwitch, RowOffline };

static std::vector<int> acctRows(bool create)
{
    if (create) return {RowUser, RowPass, RowPass2, RowShare, RowGo, RowSwitch, RowOffline};
    return {RowUser, RowPass, RowGo, RowSwitch, RowOffline};
}

void GLWidget::openAccountScreen(bool create)
{
    accountScreen = create ? AccountScreen::Create : AccountScreen::Login;
    acctRow = 0;
    acctPass.clear(); acctPass2.clear();
    if (!acctStatusBad) acctStatus.clear();
    update();
}

void GLWidget::accountSubmit()
{
    if (acctBusy) return;
    const bool create = accountScreen == AccountScreen::Create;
    const QString user = acctUser.toUpper();
    acctStatusBad = true;
    if (user.size() != 8) { acctStatus = "THE USERNAME IS EXACTLY 8 LETTERS OR DIGITS"; acctRow = 0; return; }
    if (acctPass.size() < 8) { acctStatus = "THE PASSWORD NEEDS AT LEAST 8 CHARACTERS"; acctRow = 1; return; }
    if (create && acctPass != acctPass2) { acctStatus = "THE TWO PASSWORDS ARE DIFFERENT"; acctRow = 2; return; }
    QJsonObject body;
    body["username"] = user;
    body["password"] = acctPass;
    if (create) body["shareRaces"] = acctShare;
    acctBusy = true;
    acctStatusBad = false;
    acctStatus = create ? "CREATING YOUR ACCOUNT..." : "LOGGING IN...";
    accountToken.clear();                                // (no old token goes with a login)
    apiCall("POST", create ? "/api/register" : "/api/login", body, [this, create](int status, const QJsonObject &j) {
        acctBusy = false;
        if ((status == 200 || status == 201) && j["token"].isString()) {
            acctStatus.clear();
            acctStatusBad = false;
            accountLoggedIn(j["username"].toString(), j["token"].toString(), j["shareRaces"].toBool());
            return;
        }
        acctStatusBad = true;
        if (status == 0) acctStatus = "CAN'T REACH THE SERVER - CHECK THE INTERNET, OR PLAY OFFLINE";
        else if (status == 409) acctStatus = "THAT USERNAME IS TAKEN - TRY ANOTHER";
        else if (status == 401) acctStatus = "WRONG USERNAME OR PASSWORD";
        else if (status == 429) acctStatus = QString("TOO MANY TRIES - WAIT %1 S").arg(j["retryAfter"].toInt());
        else acctStatus = j["error"].toString().toUpper();
        if (!create) acctPass.clear();
        update();
    });
    update();
}

bool GLWidget::handleAccountKey(QKeyEvent *event)
{
    if (accountScreen == AccountScreen::Off) return false;
    const int k = event->key();
    const bool create = accountScreen == AccountScreen::Create;
    const std::vector<int> rows = acctRows(create);
    acctRow = std::clamp(acctRow, 0, int(rows.size()) - 1);
    const int row = rows[size_t(acctRow)];
    QString *field = row == RowUser ? &acctUser : row == RowPass ? &acctPass : row == RowPass2 ? &acctPass2 : nullptr;
    if (event->isAutoRepeat() && k != Qt::Key_Backspace) return true;
    auto activate = [&](int r) {
        if (r == RowGo) accountSubmit();
        else if (r == RowSwitch) { acctStatusBad = false; openAccountScreen(!create); }
        else if (r == RowOffline) { accountScreen = AccountScreen::Off; acctStatusBad = false; acctStatus.clear(); }
        else if (r == RowShare) acctShare = !acctShare;
        else acctRow = std::min(acctRow + 1, int(rows.size()) - 1);
    };
    if (k == Qt::Key_Escape) { accountScreen = AccountScreen::Off; acctStatusBad = false; acctStatus.clear(); }
    else if (k == Qt::Key_Up || k == Qt::Key_Backtab) acctRow = (acctRow + int(rows.size()) - 1) % int(rows.size());
    else if (k == Qt::Key_Down || k == Qt::Key_Tab) acctRow = (acctRow + 1) % int(rows.size());
    else if (k == Qt::Key_Return || k == Qt::Key_Enter) {
        if (field && (row == RowPass || row == RowPass2) && acctRow + 1 < int(rows.size()) && rows[size_t(acctRow + 1)] == RowGo) accountSubmit();
        else activate(row);
    }
    else if (k == Qt::Key_Backspace && field) { if (!field->isEmpty()) field->chop(1); }
    else if (field) {
        for (const QChar ch : event->text()) {
            if (row == RowUser) {
                if (ch.isLetterOrNumber() && ch.unicode() < 128 && field->size() < 8) field->append(ch.toUpper());
            } else if (ch.unicode() >= 32 && ch.unicode() < 127 && field->size() < 128) {
                field->append(ch);
            }
        }
    }
    else if (k == Qt::Key_Space) activate(row);
    update();
    return true;
}

bool GLWidget::accountClick(float mx, float my)
{
    if (accountScreen == AccountScreen::Off) return false;
    const std::vector<int> rows = acctRows(accountScreen == AccountScreen::Create);
    for (const AcctHit &h : acctHits) {
        if (mx < h.x0 || mx > h.x1 || my < h.y0 || my > h.y1) continue;
        const auto it = std::find(rows.begin(), rows.end(), h.row);
        if (it == rows.end()) continue;
        acctRow = int(it - rows.begin());
        if (h.row >= RowShare) {                         // a button (or the box): press it
            QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
            const int keep = h.row;
            if (keep == RowShare) acctShare = !acctShare;
            else handleAccountKey(&space);
        }
        update();
        return true;
    }
    return true;                                         // (the screen takes every click)
}

void GLWidget::drawAccountScreen(int W, int H, float dpr)
{
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const bool create = accountScreen == AccountScreen::Create;
    const std::vector<int> rows = acctRows(create);
    const int sel = rows[size_t(std::clamp(acctRow, 0, int(rows.size()) - 1))];
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f), tps = ps * 1.1f, hps = ps * 2.0f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.65f);
    rect(0, 0, float(W), float(H));
    const float bw = std::min(float(W) * 0.62f, 620.0f * ps), bx0 = (float(W) - bw) * 0.5f, bx1 = bx0 + bw;
    const float by1 = float(H) * 0.90f, by0 = float(H) * 0.10f;
    glColor4f(0.03f, 0.05f, 0.10f, 0.96f);
    rect(bx0, by0, bx1, by1);
    glColor4f(0.35f, 0.85f, 1.0f, 0.95f);
    rect(bx0, by1 - 4.0f * dpr, bx1, by1);
    rect(bx0, by0, bx1, by0 + 2.0f * dpr);
    acctHits.clear();

    const float lx = bx0 + 28.0f * dpr, fw = bw - 56.0f * dpr;
    float y = by1 - 22.0f * dpr - 7.0f * hps;
    drawText(create ? "CREATE ACCOUNT" : "LOG IN", lx, y, hps, 0.35f, 0.85f, 1.0f);
    y -= 10.0f * dpr + 7.0f * ps;
    drawText(create ? "YOUR PROFILE AND CLONE ARE KEPT ON THE GAME SERVER AND FOLLOW YOU TO ANY PC"
                    : "LOG IN TO KEEP YOUR PROFILE AND CLONE ON THE GAME SERVER", lx, y, ps * 0.85f, 0.70f, 0.75f, 0.85f);
    y -= 22.0f * dpr;

    auto field = [&](int row, const char *label, const QString &value, bool secret, const char *hint) {
        drawText(label, lx, y - 7.0f * ps, ps, 0.85f, 0.88f, 0.95f);
        y -= 7.0f * ps + 8.0f * dpr;
        const float fh = 7.0f * tps + 14.0f * dpr, f0 = y - fh;
        glColor4f(sel == row ? 0.10f : 0.07f, sel == row ? 0.18f : 0.10f, sel == row ? 0.30f : 0.16f, 1.0f);
        rect(lx, f0, lx + fw, y);
        if (sel == row) { glColor4f(0.35f, 0.85f, 1.0f, 1.0f); rect(lx, f0, lx + 3.0f * dpr, y); }
        QByteArray shown = secret ? QByteArray(value.size(), '*') : value.toLatin1();
        if (sel == row && (int(elapsedTimer.elapsed() / 500) % 2 == 0)) shown += '_';
        if (shown.isEmpty()) drawText(hint, lx + 10.0f * dpr, f0 + 7.0f * dpr, tps, 0.40f, 0.45f, 0.55f);
        else drawText(shown.constData(), lx + 10.0f * dpr, f0 + 7.0f * dpr, tps, 1.0f, 1.0f, 1.0f);
        acctHits.push_back({lx, f0, lx + fw, y, row});
        y = f0 - 12.0f * dpr;
    };
    field(RowUser, "USERNAME (EXACTLY 8 LETTERS OR DIGITS)", acctUser, false, "E.G. SPEEDY01");
#ifdef __EMSCRIPTEN__
    // (typed into the game itself: the browser's password manager can't fill it in)
    field(RowPass, "PASSWORD (AT LEAST 8 CHARACTERS)", acctPass, true, "TYPE IT HERE - THE BROWSER CAN'T FILL IT IN");
#else
    field(RowPass, "PASSWORD (AT LEAST 8 CHARACTERS)", acctPass, true, "");
#endif
    if (create) {
        field(RowPass2, "PASSWORD AGAIN", acctPass2, true, "");
        const float s = 7.0f * ps;
        glColor4f(sel == RowShare ? 0.35f : 0.25f, sel == RowShare ? 0.85f : 0.45f, 1.0f, 1.0f);
        rect(lx, y - s - 2.0f * dpr, lx + s + 4.0f * dpr, y + 2.0f * dpr);
        glColor4f(0.03f, 0.05f, 0.10f, 1.0f);
        rect(lx + 2.0f * dpr, y - s, lx + s + 2.0f * dpr, y);
        if (acctShare) { glColor4f(0.35f, 1.0f, 0.55f, 1.0f); rect(lx + 4.0f * dpr, y - s + 2.0f * dpr, lx + s, y - 2.0f * dpr); }
        drawText("SHARE MY RACES FOR TRAINING (OPTIONAL - YOUR CLONE RECORDINGS HELP TRAIN THE CPUS)", lx + s + 12.0f * dpr, y - s,
                 ps * 0.8f, sel == RowShare ? 1.0f : 0.80f, sel == RowShare ? 1.0f : 0.85f, sel == RowShare ? 1.0f : 0.92f);
        acctHits.push_back({lx, y - s - 4.0f * dpr, lx + fw, y + 4.0f * dpr, RowShare});
        y -= s + 18.0f * dpr;
    }
    // buttons
    auto button = [&](int row, const char *text, float r, float g, float b) {
        const float bh = 7.0f * tps + 16.0f * dpr, b0 = y - bh;
        glColor4f(r * (sel == row ? 0.75f : 0.35f), g * (sel == row ? 0.75f : 0.35f), b * (sel == row ? 0.75f : 0.35f), 1.0f);
        rect(lx, b0, lx + fw, y);
        if (sel == row) { glColor4f(1.0f, 1.0f, 1.0f, 0.9f); rect(lx, b0, lx + 3.0f * dpr, y); }
        drawText(text, lx + (fw - textWidth(text, tps)) * 0.5f, b0 + 8.0f * dpr, tps, 1.0f, 1.0f, 1.0f);
        acctHits.push_back({lx, b0, lx + fw, y, row});
        y = b0 - 8.0f * dpr;
    };
    button(RowGo, acctBusy ? "PLEASE WAIT..." : create ? "CREATE ACCOUNT" : "LOG IN", 0.2f, 0.85f, 0.4f);
    button(RowSwitch, create ? "HAVE AN ACCOUNT? LOG IN" : "NEW HERE? CREATE AN ACCOUNT", 0.25f, 0.6f, 1.0f);
    button(RowOffline, "PLAY OFFLINE", 0.6f, 0.6f, 0.65f);
    if (!acctStatus.isEmpty()) {
        const QByteArray t = acctStatus.toLatin1();
        float s = ps;
        while (s > ps * 0.6f && textWidth(t.constData(), s) > fw) s *= 0.93f;
        drawText(t.constData(), lx, y - 7.0f * s - 4.0f * dpr, s, acctStatusBad ? 1.0f : 0.5f, acctStatusBad ? 0.45f : 0.95f,
                 acctStatusBad ? 0.35f : 0.65f);
    }
    const char *keys = "UP/DOWN OR TAB: MOVE   ENTER: CHOOSE   ESC: PLAY OFFLINE";
    drawText(keys, (bx0 + bx1 - textWidth(keys, ps * 0.85f)) * 0.5f, by0 + 12.0f * dpr, ps * 0.85f, 0.35f, 0.85f, 1.0f);
}

// a line for MY PROFILE and the menu
QByteArray GLWidget::accountLine() const
{
    if (accountUser.isEmpty()) return "NOT LOGGED IN - PLAYING OFFLINE";
    const char *st = syncState == SyncState::Synced ? "SYNCED" : syncState == SyncState::Syncing ? "SYNCING..."
                   : syncState == SyncState::Pending ? "NOT SYNCED YET (OFFLINE)" : "";
    return ("LOGGED IN AS " + accountUser + " - " + st).toLatin1();
}

// ---- Races up (B3) -------------------------------------------------------------------------
// Each race's summary (and its clone recording, if you share races) is added to a small queue
// file next to your profile, then sent oldest first. Offline it waits there (the newest 50).

static QString pendingRacesPath(const std::string &name)
{
    QString p = profilePath(name);
    p.chop(5);
    return p + ".pending-races.json";
}

static QJsonArray readQueue(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).array() : QJsonArray();
}

static void writeQueue(const QString &path, const QJsonArray &q)
{
    if (q.isEmpty()) { QFile::remove(path); return; }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(q).toJson(QJsonDocument::Compact));
}

void GLWidget::accountRaceUpload(const QJsonObject &race)
{
    if (accountUser.isEmpty()) return;
    const QString path = pendingRacesPath(accountUser.toStdString());
    QJsonArray q = readQueue(path);
    q.append(race);
    while (q.size() > 50) q.removeFirst();
    writeQueue(path, q);
    accountFlushRaces();
}

void GLWidget::accountFlushRaces()
{
    if (accountToken.isEmpty() || raceFlushing) return;
    const QString path = pendingRacesPath(accountUser.toStdString());
    const QJsonArray q = readQueue(path);
    if (q.isEmpty()) return;
    raceFlushing = true;
    apiCall("POST", "/api/races", q.first().toObject(), [this, path](int status, const QJsonObject &) {
        raceFlushing = false;
        if (status == 201 || status == 400) {            // sent (or refused for good): next one
            QJsonArray rest = readQueue(path);
            if (!rest.isEmpty()) rest.removeFirst();
            writeQueue(path, rest);
            accountFlushRaces();
        }                                                // (else: offline - it waits)
    });
}

void GLWidget::accountSetShare(bool on)
{
    if (accountToken.isEmpty()) return;
    QJsonObject body;
    body["shareRaces"] = on;
    apiCall("PATCH", "/api/me", body, [this](int status, const QJsonObject &j) {
        if (status == 200) accountShare = j["shareRaces"].toBool();
        update();
    });
}

// ---- The leaderboard -----------------------------------------------------------------------
// The 10 best laps on a map (on a sprint: the best race times) from every player's account

void GLWidget::openLeaderboard(int map)
{
    boardOpen = true;
    boardMap = (map % sim::NUM_MAPS + sim::NUM_MAPS) % sim::NUM_MAPS;
    boardRows.clear();
    boardBusy = true;
    boardStatus = "LOADING...";
    const int asked = boardMap;
    apiCall("GET", QString("/api/leaderboard?map=%1").arg(boardMap), {}, [this, asked](int status, const QJsonObject &j) {
        if (asked != boardMap) return;                   // (a newer map was asked for)
        boardBusy = false;
        if (status != 200) { boardStatus = "CAN'T REACH THE SERVER - TRY AGAIN LATER"; update(); return; }
        boardRows.clear();
        for (const QJsonValue v : j["best"].toArray()) {
            const QJsonObject r = v.toObject();
            boardRows.push_back({r["rank"].toInt(), r["username"].toString(), float(r["bestLap"].toDouble()), r["races"].toInt()});
        }
        boardStatus = boardRows.empty() ? "NO TIMES YET - LOG IN AND RACE THIS MAP TO BE THE FIRST" : "";
        update();
    });
    update();
}

void GLWidget::drawLeaderboard(int W, int H, float dpr)
{
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f), tps = ps * 1.1f, hps = ps * 2.0f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.60f);
    rect(0, 0, float(W), float(H));
    const float bw = std::min(float(W) * 0.62f, 620.0f * ps), bx0 = (float(W) - bw) * 0.5f, bx1 = bx0 + bw;
    const float by1 = float(H) * 0.90f, by0 = float(H) * 0.10f;
    glColor4f(0.03f, 0.05f, 0.10f, 0.96f);
    rect(bx0, by0, bx1, by1);
    glColor4f(1.0f, 0.80f, 0.25f, 0.95f);
    rect(bx0, by1 - 4.0f * dpr, bx1, by1);
    rect(bx0, by0, bx1, by0 + 2.0f * dpr);

    const bool sprintMap = sim::GameSim::isSprintMap(boardMap);
    const float lx = bx0 + 28.0f * dpr, fw = bw - 56.0f * dpr;
    float y = by1 - 22.0f * dpr - 7.0f * hps;
    drawText("LEADERBOARD", lx, y, hps, 1.0f, 0.80f, 0.25f);
    char buf[160];
    snprintf(buf, sizeof buf, "< %s >", sim::MAPS[boardMap].name);
    drawText(buf, bx1 - 28.0f * dpr - textWidth(buf, tps), y + 4.0f * dpr, tps, 1.0f, 1.0f, 1.0f);
    y -= 10.0f * dpr + 7.0f * ps;
    drawText(sprintMap ? "THE BEST RACE TIMES FROM EVERY PLAYER'S ACCOUNT" : "THE BEST LAPS FROM EVERY PLAYER'S ACCOUNT",
             lx, y, ps * 0.85f, 0.70f, 0.75f, 0.85f);
    y -= 24.0f * dpr;
    drawText("PLACE", lx, y, ps, 0.55f, 0.60f, 0.70f);
    drawText("PLAYER", lx + fw * 0.18f, y, ps, 0.55f, 0.60f, 0.70f);
    drawText(sprintMap ? "TIME" : "BEST LAP", lx + fw * 0.58f, y, ps, 0.55f, 0.60f, 0.70f);
    drawText("RACES", lx + fw * 0.84f, y, ps, 0.55f, 0.60f, 0.70f);
    y -= 10.0f * dpr + 7.0f * tps;
    for (const BoardRow &r : boardRows) {
        const bool mine = !accountUser.isEmpty() && r.user == accountUser;
        if (mine) { glColor4f(0.35f, 0.28f, 0.05f, 0.9f); rect(lx - 8.0f * dpr, y - 6.0f * dpr, lx + fw + 8.0f * dpr, y + 7.0f * tps + 4.0f * dpr); }
        const float cr = r.rank == 1 ? 1.0f : 0.92f, cg = r.rank == 1 ? 0.85f : 0.94f, cb = r.rank == 1 ? 0.30f : 0.98f;
        snprintf(buf, sizeof buf, "%d", r.rank);
        drawText(buf, lx, y, tps, cr, cg, cb);
        drawText(r.user.toLatin1().constData(), lx + fw * 0.18f, y, tps, cr, cg, cb);
        const int mm = int(r.best) / 60;
        snprintf(buf, sizeof buf, "%d:%04.1f", mm, double(r.best - 60.0f * float(mm)));
        drawText(buf, lx + fw * 0.58f, y, tps, cr, cg, cb);
        snprintf(buf, sizeof buf, "%d", r.races);
        drawText(buf, lx + fw * 0.84f, y, tps, cr, cg, cb);
        y -= 7.0f * tps + 12.0f * dpr;
    }
    if (!boardStatus.isEmpty())
        drawText(boardStatus.toLatin1().constData(), lx, y - 4.0f * dpr, ps, boardBusy ? 0.70f : 1.0f, boardBusy ? 0.75f : 0.60f, boardBusy ? 0.85f : 0.40f);
    const QByteArray you = accountUser.isEmpty() ? QByteArray("LOG IN (MY PROFILE, L) TO PUT YOUR TIMES ON THE BOARD")
                                                 : QByteArray("YOUR TIMES GO UP AFTER EVERY RACE AS ") + accountUser.toLatin1();
    drawText(you.constData(), lx, by0 + 30.0f * dpr, ps * 0.85f, 0.70f, 0.75f, 0.85f);
    const char *keys = "LEFT/RIGHT: MAP   ANY OTHER KEY: CLOSE";
    drawText(keys, (bx0 + bx1 - textWidth(keys, ps * 0.85f)) * 0.5f, by0 + 12.0f * dpr, ps * 0.85f, 1.0f, 0.80f, 0.25f);
}
