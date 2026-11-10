#include "glwidget.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>

#include <emscripten/html5.h>
#endif
QString profilePath(const std::string &name);   // (glwidget_profile.cpp)
#include "pixelfont.h"
#include "audioengine.h"
#include "carmodel.h"
#include "raceserver.h"
#include "raceclient.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#include <QFocusEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QFileInfo>
#include <QDateTime>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "gl1redirect.h"   // (the drawing goes through gl1shim: OpenGL ES 2 / WebGL)

// ===========================================================================
//  CONSTANTS
// ===========================================================================
namespace {

const float PI          = 3.14159265f;
const float MAX_SPEED   = 1.10f;
const float NITRO_SPEED = 1.65f;
const float OFFROAD_MAX = 0.45f;
float VIEW_DIST         = 340.0f;             // draw distance (fog end): less on a slow computer (gfxSet)
// Automatic graphics, level 0 (everything) to 3: what each level draws. Only the picture changes:
// the race itself is the same for everyone. (The resolution step is the browser's: see gfxScale.)
const float GFX_VIEW[4]     = {340.0f, 290.0f, 240.0f, 190.0f};
const float GFX_SCALE[4]    = {1.0f, 0.85f, 0.70f, 0.55f};
const int   GFX_PROPS[4]    = {1, 1, 2, 3};            // every n-th tree and house
const int   GFX_PARTICLES[4] = {700, 400, 220, 120};
const int   GFX_SKIDS[4]    = {1600, 900, 500, 250};

// Sunset palette
const float SKY_TOP[3] = {0.20f, 0.22f, 0.45f};
const float SKY_MID[3] = {0.86f, 0.52f, 0.58f};
const float SKY_HOR[3] = {0.99f, 0.70f, 0.47f};
const float FOG_COL[4] = {0.94f, 0.72f, 0.55f, 1.0f};

// ---- Per-map look (indexed by sim::MapTheme) ----
struct ThemeLook {
    float skyTop[3], skyMid[3], skyHor[3], fog[3];
    float fogStart, fogEnd;
    float ambient[3], diffuse[3];
    float farTop[3], farBot[3], nearTop[3], nearBot[3];
    float mtnScale;
    float sunCore[3], sunEdge[3], sunGlow[4];
    float sunSize, sunHeight;
    float cloud[4];                  // alpha 0 = no clouds
    float groundTint[3];
    float kerbA[3], kerbB[3];
    float envGround[3];              // ground colour seen in car reflections
    bool  night;
};

const ThemeLook THEMES[6] = {
    // Countryside: golden-hour sunset (the original look)
    {{0.20f, 0.22f, 0.45f}, {0.86f, 0.52f, 0.58f}, {0.99f, 0.70f, 0.47f}, {0.94f, 0.72f, 0.55f}, 110.0f, 340.0f,
     {0.42f, 0.40f, 0.48f}, {0.95f, 0.88f, 0.78f},
     {0.56f, 0.42f, 0.62f}, {0.93f, 0.68f, 0.58f}, {0.30f, 0.42f, 0.45f}, {0.86f, 0.66f, 0.56f}, 1.0f,
     {1.0f, 0.98f, 0.85f}, {1.0f, 0.90f, 0.62f}, {1.0f, 0.85f, 0.55f, 0.55f}, 38.0f, 0.16f,
     {1.0f, 0.92f, 0.88f, 0.55f}, {0.85f, 0.90f, 0.80f},
     {0.92f, 0.20f, 0.15f}, {0.95f, 0.95f, 0.95f}, {0.30f, 0.30f, 0.28f}, false},
    // Desert: hot afternoon, red mesas
    {{0.22f, 0.42f, 0.74f}, {0.60f, 0.72f, 0.86f}, {0.99f, 0.86f, 0.64f}, {0.96f, 0.84f, 0.66f}, 120.0f, 340.0f,
     {0.50f, 0.44f, 0.38f}, {1.0f, 0.92f, 0.76f},
     {0.78f, 0.46f, 0.32f}, {0.96f, 0.78f, 0.58f}, {0.66f, 0.33f, 0.22f}, {0.92f, 0.70f, 0.50f}, 0.9f,
     {1.0f, 1.0f, 0.92f}, {1.0f, 0.96f, 0.78f}, {1.0f, 0.92f, 0.65f, 0.45f}, 30.0f, 0.34f,
     {1.0f, 0.97f, 0.93f, 0.30f}, {1.0f, 1.0f, 1.0f},
     {0.92f, 0.20f, 0.15f}, {0.95f, 0.95f, 0.95f}, {0.55f, 0.45f, 0.32f}, false},
    // Snow: cold clear morning, tall white Alps
    {{0.28f, 0.46f, 0.78f}, {0.62f, 0.76f, 0.92f}, {0.90f, 0.94f, 0.98f}, {0.86f, 0.90f, 0.95f}, 90.0f, 320.0f,
     {0.56f, 0.58f, 0.66f}, {0.95f, 0.95f, 1.0f},
     {0.95f, 0.96f, 1.0f}, {0.68f, 0.74f, 0.86f}, {0.84f, 0.88f, 0.95f}, {0.55f, 0.62f, 0.74f}, 1.7f,
     {1.0f, 1.0f, 0.96f}, {1.0f, 1.0f, 0.92f}, {1.0f, 1.0f, 0.95f, 0.40f}, 28.0f, 0.30f,
     {1.0f, 1.0f, 1.0f, 0.60f}, {1.0f, 1.0f, 1.0f},
     {0.92f, 0.20f, 0.15f}, {0.95f, 0.95f, 0.95f}, {0.75f, 0.78f, 0.85f}, false},
    // City: night, moon, skyline, lit windows and street lights
    {{0.02f, 0.02f, 0.07f}, {0.08f, 0.05f, 0.18f}, {0.30f, 0.13f, 0.36f}, {0.12f, 0.08f, 0.20f}, 70.0f, 330.0f,
     {0.20f, 0.20f, 0.30f}, {0.40f, 0.42f, 0.62f},
     {0.10f, 0.07f, 0.18f}, {0.14f, 0.09f, 0.22f}, {0.05f, 0.04f, 0.10f}, {0.10f, 0.07f, 0.18f}, 1.0f,
     {0.96f, 0.96f, 1.0f}, {0.85f, 0.88f, 1.0f}, {0.55f, 0.55f, 1.0f, 0.25f}, 14.0f, 0.40f,
     {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
     {0.95f, 0.75f, 0.10f}, {0.12f, 0.12f, 0.14f}, {0.05f, 0.05f, 0.07f}, true},
    // Coast: bright midday, sea on the right
    {{0.14f, 0.38f, 0.82f}, {0.45f, 0.68f, 0.95f}, {0.82f, 0.91f, 0.98f}, {0.80f, 0.89f, 0.97f}, 130.0f, 340.0f,
     {0.50f, 0.50f, 0.55f}, {1.0f, 0.97f, 0.90f},
     {0.46f, 0.60f, 0.56f}, {0.72f, 0.84f, 0.88f}, {0.26f, 0.46f, 0.30f}, {0.62f, 0.76f, 0.72f}, 0.6f,
     {1.0f, 1.0f, 0.95f}, {1.0f, 0.98f, 0.85f}, {1.0f, 1.0f, 0.90f, 0.45f}, 26.0f, 0.45f,
     {1.0f, 1.0f, 1.0f, 0.70f}, {0.95f, 1.0f, 0.85f},
     {0.92f, 0.20f, 0.15f}, {0.95f, 0.95f, 0.95f}, {0.25f, 0.35f, 0.45f}, false},
    // Jungle: a misty morning in the hills, clouds in the valleys, deep green slopes
    {{0.45f, 0.55f, 0.62f}, {0.70f, 0.78f, 0.80f}, {0.86f, 0.90f, 0.88f}, {0.80f, 0.86f, 0.84f}, 60.0f, 290.0f,
     {0.45f, 0.52f, 0.46f}, {0.86f, 0.92f, 0.82f},
     {0.40f, 0.54f, 0.48f}, {0.72f, 0.80f, 0.78f}, {0.14f, 0.34f, 0.18f}, {0.52f, 0.66f, 0.58f}, 1.5f,
     {1.0f, 1.0f, 0.95f}, {1.0f, 0.98f, 0.90f}, {1.0f, 1.0f, 0.95f, 0.30f}, 24.0f, 0.38f,
     {1.0f, 1.0f, 1.0f, 0.80f}, {0.60f, 0.84f, 0.56f},
     {0.92f, 0.20f, 0.15f}, {0.95f, 0.95f, 0.95f}, {0.20f, 0.32f, 0.20f}, false},
};

float g_ts = 0.25f;   // texture-coordinate scale used by box()

struct V3 { float x, y, z; };

// Integer hash -> [0,1]. Used to generate the pixel textures procedurally.
float hash2(int x, int y, int seed)
{
    unsigned int h = unsigned(x) * 374761393u + unsigned(y) * 668265263u + unsigned(seed) * 982451653u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return float(h & 0xFFFFu) / 65535.0f;
}

// Tileable value noise in [0,1]: random values on a grid, smoothly interpolated.
float vnoise(float x, float y, int period, int seed)
{
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    auto h = [&](int ix, int iy) {
        return hash2(((ix % period) + period) % period, ((iy % period) + period) % period, seed);
    };
    float a = h(x0, y0), b = h(x0 + 1, y0), c = h(x0, y0 + 1), d = h(x0 + 1, y0 + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// Fractal noise: several octaves of value noise (large blotches + fine detail), still tileable.
// u, v in [0,1) across the texture.
float fbm(float u, float v, int basePeriod, int octaves, int seed)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int p = basePeriod;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * vnoise(u * p, v * p, p, seed + o * 17);
        norm += amp;
        amp *= 0.5f;
        p *= 2;
    }
    return sum / norm;
}

inline void tv(float x, float y, float z, float u, float v)
{
    glTexCoord2f(u * g_ts, v * g_ts);
    glVertex3f(x, y, z);
}

// Axis-aligned box centred at origin with normals + texture coords.
void box(float hx, float hy, float hz)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    tv(-hx, -hy, hz, -hx, -hy); tv(hx, -hy, hz, hx, -hy); tv(hx, hy, hz, hx, hy); tv(-hx, hy, hz, -hx, hy);
    glNormal3f(0, 0, -1);
    tv(hx, -hy, -hz, hx, -hy); tv(-hx, -hy, -hz, -hx, -hy); tv(-hx, hy, -hz, -hx, hy); tv(hx, hy, -hz, hx, hy);
    glNormal3f(0, 1, 0);
    tv(-hx, hy, hz, -hx, hz); tv(hx, hy, hz, hx, hz); tv(hx, hy, -hz, hx, -hz); tv(-hx, hy, -hz, -hx, -hz);
    glNormal3f(0, -1, 0);
    tv(-hx, -hy, -hz, -hx, -hz); tv(hx, -hy, -hz, hx, -hz); tv(hx, -hy, hz, hx, hz); tv(-hx, -hy, hz, -hx, hz);
    glNormal3f(1, 0, 0);
    tv(hx, -hy, hz, hz, -hy); tv(hx, -hy, -hz, -hz, -hy); tv(hx, hy, -hz, -hz, hy); tv(hx, hy, hz, hz, hy);
    glNormal3f(-1, 0, 0);
    tv(-hx, -hy, -hz, -hz, -hy); tv(-hx, -hy, hz, hz, -hy); tv(-hx, hy, hz, hz, hy); tv(-hx, hy, -hz, -hz, hy);
    glEnd();
}

void boxAt(float x, float y, float z, float hx, float hy, float hz)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    box(hx, hy, hz);
    glPopMatrix();
}

// Emit a polygon with an automatically computed outward-facing normal.
void emitN(const V3 *p, int n, V3 ctr)
{
    float ux = p[1].x - p[0].x, uy = p[1].y - p[0].y, uz = p[1].z - p[0].z;
    float vx = p[2].x - p[0].x, vy = p[2].y - p[0].y, vz = p[2].z - p[0].z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float mx = 0, my = 0, mz = 0;
    for (int i = 0; i < n; ++i) { mx += p[i].x; my += p[i].y; mz += p[i].z; }
    mx = mx / n - ctr.x; my = my / n - ctr.y; mz = mz / n - ctr.z;
    if (nx * mx + ny * my + nz * mz < 0) { nx = -nx; ny = -ny; nz = -nz; }
    glNormal3f(nx, ny, nz);
    for (int i = 0; i < n; ++i) glVertex3f(p[i].x, p[i].y, p[i].z);
}
void quadN(V3 a, V3 b, V3 c, V3 d, V3 ctr) { V3 p[4] = {a, b, c, d}; emitN(p, 4, ctr); }
void triN(V3 a, V3 b, V3 c, V3 ctr)        { V3 p[3] = {a, b, c};    emitN(p, 3, ctr); }

// Pixel-font text placed in the 3D world (facing +Z, towards the drivers).
void drawText3D(const char *s, float cx, float y, float z, float ps)
{
    float w = float(std::strlen(s)) * 6.0f * ps - ps;
    float x0 = cx - w * 0.5f;
    glBegin(GL_QUADS);
    for (const char *p = s; *p; ++p, x0 += 6.0f * ps) {
        const unsigned char *rows = pixelGlyph(*p);
        if (!rows) continue;
        for (int ry = 0; ry < 7; ++ry)
            for (int rx = 0; rx < 5; ++rx)
                if (rows[ry] & (0x10 >> rx)) {
                    float qx = x0 + rx * ps, qy = y + (6 - ry) * ps;
                    glVertex3f(qx, qy, z); glVertex3f(qx + ps, qy, z);
                    glVertex3f(qx + ps, qy + ps, z); glVertex3f(qx, qy + ps, z);
                }
    }
    glEnd();
}

struct CarPreset {
    const char *model;
    const char *trim;
    float r, g, b;
    float topSpeedKmh;
    float topSpeedScore;
    float accelSec;
    float accelScore;
    float nitroKmh;
    float nitroScore;
    const char *handlingDesc;
    float handlingScore;
};

static const CarPreset CAR_PRESETS[] = {
    {"NISSAN GT-R R35",  "Custom Tuned",       0.88f, 0.40f, 0.10f, 385.0f, 9.2f, 2.9f, 9.5f, 310.0f, 9.0f, "Agile",   8.7f},
    {"CYBER BOLT GT",    "Electric Hypercar",  0.15f, 0.88f, 1.00f, 395.0f, 9.6f, 2.5f, 9.8f, 325.0f, 9.5f, "Sharp",   9.2f},
    {"SOLAR VIPER RS",   "V10 Track Monster",  1.00f, 0.82f, 0.15f, 380.0f, 9.0f, 3.1f, 9.2f, 315.0f, 9.1f, "Drift",   8.4f},
    {"EMERALD PHANTOM",  "Aero Light-Special", 0.15f, 0.90f, 0.35f, 375.0f, 8.8f, 3.0f, 9.1f, 305.0f, 8.8f, "Grip",    9.6f},
    {"APEX CRIMSON 911", "Twin-Turbo Classic", 0.95f, 0.12f, 0.18f, 388.0f, 9.4f, 2.8f, 9.6f, 312.0f, 9.2f, "Precise", 8.9f}
};
static const int NUM_CAR_PRESETS = sizeof(CAR_PRESETS) / sizeof(CAR_PRESETS[0]);

struct MenuItem {
    const char *title;
    bool isOrange;
};

// the menu's items, top to bottom
// (the story is no menu item: it plays inside every mode, before each race)
// (no LAPS item: every circuit race is 2 laps)
enum MenuIdx { M_VSCPU, M_SPLIT, M_ONLINE, M_CAR, M_MAP, M_CPUS, M_PROFILE, M_TAUNTS, M_SOUND, M_CLONE, M_BOARD, M_ACCOUNT };
static const MenuItem MENU_ITEMS[12] = {
    {"PLAY VS CPU",            true},
    {"SPLIT SCREEN (2P)",       false},
#ifdef __EMSCRIPTEN__
    {"ONLINE: DESKTOP GAME",    true},     // (the browser: online races need the desktop game - opens its download page)
#else
    {"ONLINE MULTIPLAYER (6P)", true},
#endif
    {"CHOOSE CAR",              false},
    {"CHOOSE MAP",              true},
    {"CPU RACERS",              false},    // title shows the count (drawMainMenu)
    {"MY PROFILE",              true},     // (the CPUs adapt to it: no difficulty to choose)
    {"TAUNTS",                  false},    // title shows ON / OFF (drawMainMenu)
    {"SOUND",                   true},     // title shows ON / OFF (also [M] in a race)
    {"YOUR CLONE",              false},    // title shows ON / OFF and how well it copies you
    {"LEADERBOARD",             true},     // the best laps from every player's account
    {"LOG OUT",                 false}     // title: LOG OUT (NAME) or LOG IN (drawMainMenu); opens the log-in screen
};
static const int NUM_MENU_ITEMS = 12;

} // namespace

// How each map builds its tunnels and bridges (milestone 1d)
struct StructureLook {
    float tunnelH;        // ceiling above the road
    float hill;           // ground over a tunnel, above the road
    float depth;          // valley under a bridge, below the deck
    float darkness;       // how much darker it is deep in a tunnel
    int   water;          // under a bridge: 0 none, 1 a river 5 m below the deck, 2 the sea
    bool  oneSided;       // an avalanche gallery: open (columns) on the valley side
    float wall[3], parapet[3], deck[3];
};

const StructureLook &structureLook(sim::MapTheme t)
{
    static const StructureLook LOOKS[6] = {
        // Sunset Valley: a wooden bridge over a river
        {6.5f, 12.0f, 6.0f, 0.55f, 1, false, {0.62f, 0.58f, 0.52f}, {0.48f, 0.32f, 0.18f}, {0.55f, 0.52f, 0.48f}},
        // Canyon Run: a tunnel through a sandstone mesa
        {6.5f, 15.0f, 6.0f, 0.55f, 0, false, {0.74f, 0.50f, 0.34f}, {0.62f, 0.46f, 0.32f}, {0.60f, 0.52f, 0.44f}},
        // Alpine Pass: an avalanche gallery, a stone bridge over a gorge
        {6.5f, 22.0f, 25.0f, 0.30f, 0, true, {0.70f, 0.70f, 0.72f}, {0.60f, 0.60f, 0.62f}, {0.62f, 0.62f, 0.64f}},
        // Neon City: a street tunnel with its roof at street level, the raised highway
        {4.5f, 4.75f, 0.0f, 0.35f, 0, false, {0.50f, 0.52f, 0.56f}, {0.55f, 0.57f, 0.62f}, {0.52f, 0.54f, 0.58f}},
        // Coastal Highway: a tunnel under the headland, a steel bridge over the harbour
        {6.5f, 14.0f, 6.0f, 0.55f, 2, false, {0.66f, 0.64f, 0.60f}, {0.42f, 0.50f, 0.60f}, {0.56f, 0.58f, 0.62f}},
        // Ghat Road: a tunnel through the cliff, a concrete bridge over the jungle river
        {6.5f, 18.0f, 8.0f, 0.55f, 1, false, {0.48f, 0.50f, 0.44f}, {0.58f, 0.58f, 0.54f}, {0.52f, 0.52f, 0.50f}},
    };
    return LOOKS[int(t)];
}

// ===========================================================================
//  CONSTRUCTION
// ===========================================================================
#ifdef __EMSCRIPTEN__
GLWidget::GLWidget(GLParent *parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent), rng(std::random_device{}())
{
#else
GLWidget::GLWidget(GLParent *parent)
    : QOpenGLWidget(parent), rng(std::random_device{}())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
#endif
    tauntsOn = QSettings("TPP", "CarRacing").value("taunts", true).toBool();   // (the TAUNTS menu item)
    cloneOn = QSettings("TPP", "CarRacing").value("clone", true).toBool();     // (the YOUR CLONE menu item)
    accountLoad();                                       // (logged in? then the profile and clone are your account's)
    cloneLoad();
#ifndef __EMSCRIPTEN__
    setMinimumSize(800, 450);
#endif
    audio = new AudioEngine(this);
    soundOn = QSettings("TPP", "CarRacing").value("sound", true).toBool();   // (the SOUND menu item)
    if (!soundOn) audio->setMuted(true);   // (never unmuted here: silent test runs stay silent)
    simEngine.setState(State::Menu);
    menuSelection = 0;
    activeModal = ModalNone;
    selectedCarPreset = 0;
    selectedMapPreset = 0;
    simEngine.setAiDifficulty(1);
    simEngine.reset(2024);
    if (!simEngine.getCars().empty()) {
        simEngine.getCars()[0].r = CAR_PRESETS[0].r;
        simEngine.getCars()[0].g = CAR_PRESETS[0].g;
        simEngine.getCars()[0].b = CAR_PRESETS[0].b;
    }

    autoShotDir = qEnvironmentVariable("CAR_RACING_AUTOSHOT");
    netTest = qEnvironmentVariable("CAR_RACING_NETTEST");
    gfxSet(QSettings("TPP", "CarRacing").value("gfx/level", 0).toInt());   // (where this computer was last time)
#ifdef __EMSCRIPTEN__
    {   // ?gfx=N on the page holds a level (tests)
        const int forced = EM_ASM_INT({ const v = new URLSearchParams(window.location.search).get("gfx"); return v === null ? -1 : parseInt(v); });
        if (forced >= 0) { gfxSet(forced); gfxFixed = true; }
    }
#endif

    elapsedTimer.start();
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &GLWidget::updateGame);
    timer->start(16); // ~60 ticks per second
}

GLWidget::~GLWidget()
{
    if (cloneThread.joinable()) cloneThread.join();      // (a clone still training: let it finish)
    // shut the network down while this widget is still whole (no signals into a dying widget)
    if (net) { net->disconnect(this); delete net; net = nullptr; }
    delete hostServer;
    hostServer = nullptr;
    if (quad) {
        makeCurrent();
        gluDeleteQuadric(quad);
        GLuint t[] = {texGrass, texAsphalt, texStone, texPlaster, texRoof, texEnv, texSoft, texSand, texSnow, texPavement};
        glDeleteTextures(10, t);
        carmodel::Lists lists{carBodyList, carGlassList, carTrimList};
        carmodel::destroy(lists);
        for (GLuint list : modelLists) glDeleteLists(list, 1);
        doneCurrent();
    }
    delete audio;
}

float GLWidget::rnd(float a, float b)
{
    return std::uniform_real_distribution<float>(a, b)(rng);
}


// Menu pickers: 3 = car, 4 = map, 5 = number of CPU racers, 6 = CPU difficulty.
// Returns false for other items.
// The LOG OUT / LOG IN menu item: logged in - log out (this PC's own profile and clone come
// back); then the log-in screen, where someone else can log in or carry on offline
void GLWidget::menuAccount()
{
    const bool wasIn = !accountUser.isEmpty();
    if (wasIn) { accountLogout(); acctUser.clear(); }
    acctStatusBad = false;
    openAccountScreen(false);
    if (wasIn) acctStatus = "YOU LOGGED OUT - LOG IN, OR PLAY OFFLINE";
}

// Sound on or off (the SOUND menu item, [M] in the menu and in a race), remembered
void GLWidget::toggleSound()
{
    soundOn = !soundOn;
    if (audio) audio->setMuted(!soundOn);
    QSettings("TPP", "CarRacing").setValue("sound", soundOn);
}

bool GLWidget::cycleMenuOption(int item, int dir)
{
    if (item == M_CPUS) {
        const int most = sim::MAX_RACERS - 1;
        cpuCount = 1 + (cpuCount - 1 + dir + most) % most;
    } else if (item == M_TAUNTS) {
        tauntsOn = !tauntsOn;
        QSettings("TPP", "CarRacing").setValue("taunts", tauntsOn);
    } else if (item == M_SOUND) {
        toggleSound();
    } else if (item == M_CLONE) {
        cloneOn = !cloneOn;
        QSettings("TPP", "CarRacing").setValue("clone", cloneOn);
    } else if (item == M_CAR) {
        selectedCarPreset = (selectedCarPreset + dir + NUM_CAR_PRESETS) % NUM_CAR_PRESETS;
    } else if (item == M_MAP) {
        selectedMapPreset = (selectedMapPreset + dir + sim::NUM_MAPS) % sim::NUM_MAPS;
        simEngine.setMap(selectedMapPreset);
    } else {
        return false;
    }
    return true;
}

void GLWidget::startRace(GameMode mode)
{
    simEngine.setMap(selectedMapPreset);
    // the CPUs: new personalities and choices every race, their skill spread around yours
    simEngine.setAiSeed(uint32_t(rng()));
    {
        const sim::PlayerProfile me = loadProfile(profileName(0));
        simEngine.setCpuChallenge(me.challenge);
        simEngine.setCpuSkill(sim::cpuCentreFor(me.traits.skill, me.sure, me.challenge));
    }
    // what the race director knows of you (and of player 2 in split screen)
    simEngine.clearPlayerHints();
    simEngine.setPlayerHints(0, hintsFor(loadProfile(profileName(0)), selectedMapPreset));
    if (mode == sim::GameSim::ModeSplitScreen) simEngine.setPlayerHints(1, hintsFor(loadProfile(profileName(1)), selectedMapPreset));
    simEngine.startRace(mode, mode == sim::GameSim::ModeSingle ? 1 + cpuCount : 2);
    auto &cars = simEngine.getCars();
    if (!cars.empty()) {
        cars[0].r = CAR_PRESETS[selectedCarPreset].r;
        cars[0].g = CAR_PRESETS[selectedCarPreset].g;
        cars[0].b = CAR_PRESETS[selectedCarPreset].b;
    }
    if (mode == sim::GameSim::ModeSingle) {
        // Every CPU is the CPU driver with its own mind (personality, mood, skill around yours:
        // shared/aidriver.cpp), on the wide road, whether there is 1 CPU or 5. Every CPU gets a
        // colour that doesn't look like yours or another CPU's.
        static const float PALETTE[8][3] = {
            {0.15f, 0.42f, 0.98f}, {1.00f, 0.75f, 0.10f}, {0.20f, 0.85f, 0.35f}, {0.85f, 0.30f, 0.95f},
            {0.10f, 0.85f, 0.90f}, {0.95f, 0.50f, 0.15f}, {0.92f, 0.92f, 0.95f}, {0.92f, 0.14f, 0.16f}};
        std::vector<int> used;
        for (size_t i = 1; i < cars.size(); ++i) {
            cars[i].aiLevel = 1;                         // (a CPU)
            for (int k = 0; k < 8; ++k) {
                const float *c = PALETTE[k];
                const bool likeMine = std::fabs(c[0] - cars[0].r) + std::fabs(c[1] - cars[0].g) + std::fabs(c[2] - cars[0].b) < 0.45f;
                if (likeMine || std::find(used.begin(), used.end(), k) != used.end()) continue;
                cars[i].r = c[0]; cars[i].g = c[1]; cars[i].b = c[2];
                used.push_back(k);
                break;
            }
        }
    }
    view1 = CarView();
    view2 = CarView();
    for (CarFx &f : carFx) f = CarFx();
    skidMarks.clear();
    popups.clear();
    for (auto &q : carMsgs) q.clear();
    feed.clear();
    // vs CPU: the CPUs are named drivers from the roster (shared/roster.cpp), each different
    if (mode == sim::GameSim::ModeSingle) {
        int order[sim::ROSTER_SIZE];
        for (int k = 0; k < sim::ROSTER_SIZE; ++k) order[k] = k;
        for (int k = sim::ROSTER_SIZE - 1; k > 0; --k) std::swap(order[k], order[int(rng() % unsigned(k + 1))]);
        for (size_t i = 1; i < cars.size(); ++i) {
            const sim::RosterDriver &R = sim::rosterDriver(order[(i - 1) % sim::ROSTER_SIZE]);
            cars[i].rosterId = order[(i - 1) % sim::ROSTER_SIZE];
            cars[i].aiPersona = R.persona;
            cars[i].r = R.car[0]; cars[i].g = R.car[1]; cars[i].b = R.car[2];
        }
    }
    rivalSummary.clear();
    tauntUntil = -1;
    if (story != StoryScreen::Off) storyDressCars();
    else {
        nameOverride.assign(cars.size(), QByteArray());
        for (size_t i = 0; i < cars.size(); ++i)
            if (cars[i].rosterId >= 0) nameOverride[i] = sim::rosterDriver(cars[i].rosterId).name;
    }
    cloneSetupRace(mode);                               // your clone in the race, and recording you
    for (float &t : nitroBannerTimer) t = 0.0f;
    profileSaved = false;
    elapsedTimer.restart();
}


// ===========================================================================
//  PARTICLES
// ===========================================================================
void GLWidget::spawnParticle(Particle::Type t, float x, float y, float z,
                             float vx, float vy, float vz,
                             float r, float g, float b, float a,
                             float size, float life, float decay)
{
    if (int(particles.size()) > GFX_PARTICLES[gfxLevel]) return;
    Particle p;
    p.type = t;
    p.x = x; p.y = y; p.z = z;
    p.floor = y - 0.45f;                                 // effects start about 0.45 m above the road
    p.vx = vx; p.vy = vy; p.vz = vz;
    p.r = r; p.g = g; p.b = b; p.a = a;
    p.size = size; p.life = life; p.decay = decay;
    particles.push_back(p);
}

void GLWidget::spawnSparks(float x, float y, float z, int count)
{
    for (int i = 0; i < count; ++i) {
        float angle = rnd(0.0f, 6.283f);
        float spd = rnd(0.05f, 0.22f);
        spawnParticle(Particle::Spark, x, y, z,
                      std::cos(angle) * spd, rnd(0.08f, 0.25f), std::sin(angle) * spd,
                      1.0f, rnd(0.6f, 0.95f), 0.1f, 1.0f,
                      rnd(0.08f, 0.16f), 1.0f, rnd(2.5f, 4.0f));
    }
}

void GLWidget::spawnDebris(float x, float y, float z, float r, float g, float b, int count)
{
    for (int i = 0; i < count; ++i) {
        float angle = rnd(0.0f, 6.283f);
        float spd = rnd(0.03f, 0.16f);
        spawnParticle(Particle::Dust, x, y, z,
                      std::cos(angle) * spd, rnd(0.05f, 0.18f), std::sin(angle) * spd,
                      r, g, b, 0.9f,
                      rnd(0.12f, 0.25f), 1.0f, rnd(1.5f, 2.5f));
    }
}

void GLWidget::updateParticles(float dt)
{
    for (size_t i = 0; i < particles.size(); ) {
        Particle &p = particles[i];
        p.life -= p.decay * dt;
        if (p.life <= 0.0f) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        p.x += p.vx * dt * 60.0f;
        p.y += p.vy * dt * 60.0f;
        p.z += p.vz * dt * 60.0f;
        if (p.type == Particle::Spark) {
            p.vy -= 0.012f * dt * 60.0f;
            if (p.y < p.floor + 0.05f) { p.y = p.floor + 0.05f; p.vy = -p.vy * 0.4f; p.vx *= 0.6f; p.vz *= 0.6f; }
        } else if (p.type == Particle::Smoke || p.type == Particle::Dust) {
            p.size += 1.1f * dt;   // puffs billow outwards as they age
            p.a = p.life * 0.6f;
        } else if (p.type == Particle::Flame) {
            p.size *= (1.0f - 0.4f * dt);
        }
        ++i;
    }
}

void GLWidget::drawParticles()
{
    if (particles.empty()) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);

    // Camera-facing soft sprites: take the camera's right/up axes from the modelview matrix
    GLfloat m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    const float Rx = m[0], Ry = m[4], Rz = m[8], Ux = m[1], Uy = m[5], Uz = m[9];
    auto sprite = [&](const Particle &p, float s) {
        glTexCoord2f(0, 0); glVertex3f(p.x + (-Rx - Ux) * s, p.y + (-Ry - Uy) * s, p.z + (-Rz - Uz) * s);
        glTexCoord2f(1, 0); glVertex3f(p.x + ( Rx - Ux) * s, p.y + ( Ry - Uy) * s, p.z + ( Rz - Uz) * s);
        glTexCoord2f(1, 1); glVertex3f(p.x + ( Rx + Ux) * s, p.y + ( Ry + Uy) * s, p.z + ( Rz + Uz) * s);
        glTexCoord2f(0, 1); glVertex3f(p.x + (-Rx + Ux) * s, p.y + (-Ry + Uy) * s, p.z + (-Rz + Uz) * s);
    };
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texSoft);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // flames & sparks glow (additive)
    carmodel::beginGlow();
    glBegin(GL_QUADS);
    for (const Particle &p : particles) {
        if (p.type == Particle::Smoke || p.type == Particle::Dust) continue;
        glColor4f(p.r, p.g, p.b, p.life);
        sprite(p, p.size * 1.6f);
    }
    glEnd();
    carmodel::endGlow();

    // smoke & dust: soft translucent puffs
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const Particle &p : particles) {
        if (p.type != Particle::Smoke && p.type != Particle::Dust) continue;
        glColor4f(p.r * 0.80f, p.g * 0.80f, p.b * 0.82f, p.a * 0.35f);
        // keep the whole puff above the road, otherwise the ground cuts it with a hard edge
        Particle lifted = p;
        lifted.y = std::max(p.y, p.size * 2.2f * 0.9f);
        sprite(lifted, p.size * 2.2f);
    }
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LIGHTING);
}

// ===========================================================================
//  BRAKE LIGHTS + SKID MARKS (client-side visuals derived from sim state)
// ===========================================================================
void GLWidget::updateCarFx(int idx, const sim::Car &c)
{
    CarFx &fx = carFx[idx];
    // Braking = slowing down faster than rolling resistance alone would (works for CPU too)
    bool braking = (fx.prevSpeed - c.speed) > 0.02f && c.speed > 0.05f;
    fx.prevSpeed = c.speed;
    fx.brakeGlow += ((braking ? 1.0f : 0.0f) - fx.brakeGlow) * (braking ? 0.5f : 0.15f);

    // Rear tyres lay rubber on the asphalt while drifting or braking hard
    bool mark = simEngine.getState() == State::Racing && (c.isDrifting || (braking && c.speed > 0.4f));
    float fx0 = std::sin(c.heading), fz0 = -std::cos(c.heading);
    float rx = std::cos(c.heading), rz = std::sin(c.heading);
    for (int w = 0; w < 2; ++w) {
        float side = (w == 0) ? -1.0f : 1.0f;
        float wx = c.x + rx * side * carmodel::WHEEL_X - fx0 * carmodel::WHEEL_Z_REAR;
        float wz = c.z + rz * side * carmodel::WHEEL_X - fz0 * carmodel::WHEEL_Z_REAR;
        if (mark && fx.marking) {
            float dx = wx - fx.lastX[w], dz = wz - fx.lastZ[w];
            if (dx * dx + dz * dz < 0.25f) continue;          // wait for 0.5 m of travel
        }
        const float wy = mark ? simEngine.heightNear(wx, wz, c.s, c.path) : c.y;
        if (mark && fx.marking)
            skidMarks.push_back({fx.lastX[w], fx.lastZ[w], wx, wz, c.isDrifting ? 0.60f : 0.45f, fx.lastY[w], wy});
        fx.lastX[w] = wx;
        fx.lastZ[w] = wz;
        fx.lastY[w] = wy;
    }
    fx.marking = mark;
    if (int(skidMarks.size()) > GFX_SKIDS[gfxLevel])
        skidMarks.erase(skidMarks.begin(), skidMarks.begin() + std::min<int>(200, int(skidMarks.size())));

    // a wreck burns and smokes; a badly damaged car smokes from the bonnet
    const int tk = simEngine.getTick();
    if (c.wreck > 0) {
        if ((tk + idx) % 2 == 0)
            spawnParticle(Particle::Smoke, c.x + rnd(-0.6f, 0.6f), c.y + 0.9f, c.z + rnd(-0.6f, 0.6f),
                          rnd(-0.02f, 0.02f), rnd(0.04f, 0.08f), rnd(-0.02f, 0.02f),
                          0.18f, 0.18f, 0.20f, 0.7f, rnd(0.5f, 0.9f), 1.0f, 0.9f);
        if (c.wreck < 90 && (tk + idx) % 3 == 0)
            spawnParticle(Particle::Flame, c.x + rnd(-0.5f, 0.5f), c.y + 0.7f, c.z + rnd(-0.5f, 0.5f),
                          rnd(-0.02f, 0.02f), rnd(0.03f, 0.07f), rnd(-0.02f, 0.02f),
                          1.0f, rnd(0.45f, 0.75f), 0.15f, 1.0f, rnd(0.35f, 0.6f), 1.0f, 4.0f);
    } else if (c.health < 40.0f && (tk + idx) % (c.health < 20.0f ? 2 : 4) == 0) {
        const float hx = c.x + fx0 * 1.7f, hz = c.z + fz0 * 1.7f;
        const float grey = c.health < 20.0f ? 0.15f : 0.55f;
        spawnParticle(Particle::Smoke, hx + rnd(-0.3f, 0.3f), c.y + 0.85f, hz + rnd(-0.3f, 0.3f),
                      -fx0 * 0.05f + rnd(-0.01f, 0.01f), rnd(0.03f, 0.06f), -fz0 * 0.05f + rnd(-0.01f, 0.01f),
                      grey, grey, grey + 0.03f, 0.55f, rnd(0.25f, 0.45f), 1.0f, 1.6f);
    }
}

void GLWidget::drawSkidMarks()
{
    if (skidMarks.empty()) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const SkidMark &m : skidMarks) {
        if (!inView(m.x0, m.z0)) continue;
        float dx = m.x1 - m.x0, dz = m.z1 - m.z0, l = std::sqrt(dx * dx + dz * dz);
        if (l < 1e-4f) continue;
        float nx = -dz / l * 0.13f, nz = dx / l * 0.13f;     // half tyre width
        glColor4f(0.04f, 0.04f, 0.045f, m.alpha);
        glVertex3f(m.x0 - nx, m.y0 + 0.035f, m.z0 - nz); glVertex3f(m.x0 + nx, m.y0 + 0.035f, m.z0 + nz);
        glVertex3f(m.x1 + nx, m.y1 + 0.035f, m.z1 + nz); glVertex3f(m.x1 - nx, m.y1 + 0.035f, m.z1 - nz);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void GLWidget::processSimEvents(const std::vector<sim::SimEvent> &events)
{
    const auto &cars = simEngine.getCars();
    for (const auto &ev : events) {
        switch (ev.type) {
        case sim::SimEvent::Crash:
            if (audio) audio->playCrash(ev.intensity);
            if (ev.carIndex == localCar()) {
                view1.shake = std::max(view1.shake, ev.intensity * 0.8f);
            } else if (ev.carIndex == 1 && !isOnline()) {
                view2.shake = std::max(view2.shake, ev.intensity * 0.8f);
            }
            break;
        case sim::SimEvent::Sparks:
            spawnSparks(ev.x, ev.y, ev.z, ev.count > 0 ? ev.count : 8);
            break;
        case sim::SimEvent::Debris:
            spawnDebris(ev.x, ev.y, ev.z, ev.r, ev.g, ev.b, ev.count > 0 ? ev.count : 8);
            break;
        case sim::SimEvent::NitroFlame: {
            float fx = (ev.carIndex >= 0 && ev.carIndex < int(cars.size())) ? std::sin(cars[ev.carIndex].heading) : ev.vx;
            float fz = (ev.carIndex >= 0 && ev.carIndex < int(cars.size())) ? -std::cos(cars[ev.carIndex].heading) : ev.vz;
            spawnParticle(Particle::Flame, ev.x, ev.y, ev.z,
                          -fx * 0.3f + rnd(-0.03f, 0.03f), rnd(0.02f, 0.08f), -fz * 0.3f + rnd(-0.03f, 0.03f),
                          0.2f, rnd(0.7f, 1.0f), 1.0f, 1.0f,
                          rnd(0.2f, 0.35f), 1.0f, 8.0f);
            if (rnd(0.0f, 1.0f) < 0.4f) {
                spawnParticle(Particle::Spark, ev.x, ev.y, ev.z,
                              -fx * 0.4f + rnd(-0.06f, 0.06f), rnd(0.04f, 0.15f), -fz * 0.4f + rnd(-0.06f, 0.06f),
                              1.0f, rnd(0.5f, 0.9f), 0.1f, 1.0f,
                              rnd(0.08f, 0.14f), 1.0f, 4.0f);
            }
            break;
        }
        case sim::SimEvent::BrakeSmoke:
            spawnParticle(Particle::Smoke, ev.x, ev.y, ev.z,
                          rnd(-0.02f, 0.02f), rnd(0.03f, 0.07f), rnd(-0.02f, 0.02f),
                          0.85f, 0.85f, 0.88f, 0.4f,
                          rnd(0.2f, 0.3f), 1.0f, 2.5f);
            break;
        case sim::SimEvent::DriftSmoke:
            spawnParticle(Particle::Smoke, ev.x, ev.y, ev.z,
                          rnd(-0.02f, 0.02f), rnd(0.03f, 0.08f), rnd(-0.02f, 0.02f),
                          0.85f, 0.85f, 0.88f, 0.5f,
                          rnd(0.2f, 0.35f), 1.0f, 2.0f);
            break;
        case sim::SimEvent::CountdownBeep:
            if (audio) audio->playCountdownBeep(ev.highPitch);
            break;
        case sim::SimEvent::Fanfare:
            if (audio) audio->playFanfare();
            break;
        case sim::SimEvent::NitroPickupEvent:
            if (audio) audio->playNitroPickup();
            spawnSparks(ev.x, ev.y, ev.z, 12);
            for (int k = 0; k < 10; ++k) {
                spawnParticle(Particle::Spark, ev.x, ev.y, ev.z,
                              rnd(-0.12f, 0.12f), rnd(0.08f, 0.25f), rnd(-0.12f, 0.12f),
                              0.15f, 0.85f, 1.0f, 1.0f,
                              rnd(0.2f, 0.35f), 1.0f, 2.5f);
            }
            if (ev.carIndex >= 0 && ev.carIndex < sim::MAX_RACERS) {
                nitroBannerTimer[ev.carIndex] = 2.4f;
            }
            break;
        case sim::SimEvent::DirtDust:
            spawnParticle(Particle::Smoke, ev.x + rnd(-0.6f, 0.6f), ev.y, ev.z + rnd(-0.6f, 0.6f),
                          rnd(-0.03f, 0.03f), rnd(0.01f, 0.03f), rnd(-0.03f, 0.03f),
                          0.62f, 0.52f, 0.38f, 0.45f, rnd(0.6f, 1.0f), 1.0f, 1.3f);
            break;
        case sim::SimEvent::Damage: {
            const int i = ev.carIndex, o = ev.count;
            if (i < 0 || i >= sim::MAX_RACERS || ev.intensity < 1.5f) break;
            const int amount = int(std::lround(ev.intensity));
            DamagePopup p;
            p.car = i; p.amount = amount; p.fault = ev.highPitch; p.born = simEngine.getTick();
            popups.push_back(p);
            if (!isLocalCar(i) || (ev.intensity < 2.5f && o != sim::SimEvent::ByBoost)) break;
            if (o == sim::SimEvent::ByLanding && stuntTick[i] == simEngine.getTick()) break;   // (said with the landing)
            QByteArray txt;
            float r = 1.0f, g = 0.86f, b = 0.30f;                // yellow: it happened to you
            if (o >= 0) {
                if (ev.highPitch) { txt = "YOU RAMMED " + carName(o); r = 1.0f; g = 0.35f; b = 0.25f; }
                else              txt = carName(o) + " HIT YOU";
            } else if (o == sim::SimEvent::ByTraffic) {
                txt = ev.highPitch ? "YOU HIT TRAFFIC" : "TRAFFIC HIT YOU";
                if (ev.highPitch) { r = 1.0f; g = 0.35f; b = 0.25f; }
            } else if (o == sim::SimEvent::ByBarrier) {
                txt = "HIT A BARRIER"; r = 1.0f; g = 0.35f; b = 0.25f;
            } else if (o == sim::SimEvent::ByRail) {
                txt = "SCRAPED THE RAIL";
            } else if (o == sim::SimEvent::ByLanding) {
                txt = ev.intensity >= 12.0f ? "BAD LANDING" : "LANDING";
                if (ev.intensity >= 12.0f) { r = 1.0f; g = 0.35f; b = 0.25f; }
            } else if (o == sim::SimEvent::ByWater) {
                txt = "INTO THE RIVER"; r = 1.0f; g = 0.35f; b = 0.25f;
            } else if (o == sim::SimEvent::ByOffroad) {
                txt = "FLEW OFF THE ROAD"; r = 1.0f; g = 0.35f; b = 0.25f;
            } else if (o == sim::SimEvent::ByBoost) {
                txt = "ENGINE BURNT OUT - TOO MUCH BOOST"; r = 1.0f; g = 0.35f; b = 0.25f;
            } else if (o == sim::SimEvent::ByRampSide) {
                txt = "HIT THE RAMP'S SIDE"; r = 1.0f; g = 0.35f; b = 0.25f;
            } else {
                txt = "HIT A BARREL";
            }
            pushMsg(carMsgs[i], txt + "  -" + QByteArray::number(amount), r, g, b, 100);
            if (i == localCar()) view1.shake = std::max(view1.shake, std::min(1.0f, ev.intensity / 40.0f));
            else if (i == 1 && !isOnline()) view2.shake = std::max(view2.shake, std::min(1.0f, ev.intensity / 40.0f));
            break;
        }
        case sim::SimEvent::Wrecked: {
            if (audio) { audio->playCrash(1.0f); audio->playCrash(0.9f); }
            spawnSparks(ev.x, ev.y, ev.z, 30);
            for (int k = 0; k < 24; ++k)                         // a burst of smoke and fire
                spawnParticle(k % 3 ? Particle::Smoke : Particle::Flame, ev.x + rnd(-1.2f, 1.2f), ev.y + rnd(0.0f, 0.8f),
                              ev.z + rnd(-1.2f, 1.2f), rnd(-0.06f, 0.06f), rnd(0.03f, 0.12f), rnd(-0.06f, 0.06f),
                              k % 3 ? 0.30f : 1.0f, k % 3 ? 0.30f : 0.55f, k % 3 ? 0.32f : 0.15f, 0.8f,
                              rnd(0.5f, 1.1f), 1.0f, rnd(0.8f, 1.4f));
            const int i = ev.carIndex;
            if (i < 0 || i >= sim::MAX_RACERS) break;
            QByteArray line = carName(i) + (isLocalCar(i) && carName(i) == "YOU" ? " WERE WRECKED" : " WRECKED");
            if (ev.count >= 0) line += " BY " + carName(ev.count);
            pushMsg(feed, line, isLocalCar(i) ? 1.0f : 0.95f, isLocalCar(i) ? 0.40f : 0.75f, isLocalCar(i) ? 0.30f : 0.40f, 300);
            if (i == localCar()) view1.shake = 1.0f;
            else if (i == 1 && !isOnline()) view2.shake = 1.0f;
            break;
        }
        case sim::SimEvent::Respawn: {
            for (int k = 0; k < 16; ++k) {                       // a ring of sparkles round the car
                const float a = float(k) * PI / 8.0f;
                spawnParticle(Particle::Spark, ev.x + std::cos(a) * 1.6f, ev.y + 0.4f, ev.z + std::sin(a) * 1.6f,
                              std::cos(a) * 0.03f, rnd(0.05f, 0.12f), std::sin(a) * 0.03f,
                              0.4f, 1.0f, 0.6f, 1.0f, rnd(0.15f, 0.25f), 1.0f, 2.0f);
            }
            if (ev.carIndex >= 0 && ev.carIndex < sim::MAX_RACERS && isLocalCar(ev.carIndex))
                pushMsg(carMsgs[ev.carIndex], "BACK IN THE RACE!", 0.40f, 1.0f, 0.55f, 90);
            break;
        }
        case sim::SimEvent::Stunt: {
            // how a jump was landed (side ramps): a word for it over your car, a nitro refill
            // for a clean barrel roll
            const int i = ev.carIndex;
            if (i < 0 || i >= sim::MAX_RACERS || !isLocalCar(i)) break;
            static const char *WORD[5] = {"PERFECT LANDING", "BARREL ROLL! NITRO FULL", "SPIN SAVED", "ROUGH LANDING", "CRASH LANDING!"};
            static const float COL[5][3] = {{0.35f, 1.0f, 0.55f}, {0.35f, 0.95f, 1.0f}, {0.35f, 1.0f, 0.55f},
                                            {1.0f, 0.75f, 0.25f}, {1.0f, 0.30f, 0.25f}};
            const int k = std::clamp(ev.count, 0, 4);
            QByteArray w = WORD[k];
            if (ev.intensity >= 1.5f) w += "  -" + QByteArray::number(int(std::lround(ev.intensity)));
            pushMsg(carMsgs[i], w, COL[k][0], COL[k][1], COL[k][2], 110);
            stuntTick[i] = simEngine.getTick();          // (its damage line is this one)
            if (k <= 2) ++statLandings;
            break;
        }
        case sim::SimEvent::Splash: {
            // came down off the road: a fountain of water, or a cloud of earth
            if (audio) audio->playCrash(0.7f);
            const bool water = ev.count == 0;
            for (int k = 0; k < 40; ++k)
                spawnParticle(Particle::Smoke, ev.x + rnd(-2.0f, 2.0f), ev.y + rnd(0.0f, 0.6f), ev.z + rnd(-2.0f, 2.0f),
                              rnd(-0.08f, 0.08f), rnd(0.10f, 0.32f), rnd(-0.08f, 0.08f),
                              water ? 0.75f : 0.55f, water ? 0.88f : 0.45f, water ? 1.0f : 0.32f, 0.85f,
                              rnd(0.6f, 1.3f), 1.0f, rnd(1.0f, 1.8f));
            if (ev.carIndex >= 0 && ev.carIndex < sim::MAX_RACERS)
                pushMsg(feed, carName(ev.carIndex) + (water ? " FELL IN THE RIVER" : ev.count == 2 ? " FELL INTO THE GORGE" : " FLEW OFF THE ROAD"),
                        0.55f, 0.85f, 1.0f, 240);
            break;
        }
        case sim::SimEvent::Bounty: {
            const int i = ev.carIndex, v = ev.count;
            if (i < 0 || i >= sim::MAX_RACERS || v < 0 || v >= sim::MAX_RACERS) break;
            pushMsg(feed, carName(i) + (carName(i) == "YOU" ? " GOT A BOUNTY +15 FOR " : " BOUNTY +15 FOR ") + carName(v), 1.0f, 0.85f, 0.25f, 240);
            if (isLocalCar(i)) pushMsg(carMsgs[i], "BOUNTY! +15 HP (MOST DAMAGE TO " + carName(v) + ")", 1.0f, 0.85f, 0.25f, 150);
            if (audio) audio->playNitroPickup();
            break;
        }
        case sim::SimEvent::Eliminated: {
            const int i = ev.carIndex;
            if (i < 0 || i >= sim::MAX_RACERS) break;
            pushMsg(feed, carName(i) + (carName(i) == "YOU" ? " WERE ELIMINATED" : " ELIMINATED"), 1.0f, 0.30f, 0.25f, 400);
            break;
        }
        case sim::SimEvent::Taunt: {
            // a named driver says something to a person - shown to that person only
            const int sp = ev.carIndex, target = int(ev.vx);
            if (!tauntsOn || sp < 0 || sp >= int(simEngine.getCars().size()) || !isLocalCar(target)) break;
            const sim::Car &c = simEngine.getCars()[size_t(sp)];
            if (c.rosterId < 0 && !c.aiClone) break;
            tauntText = carName(sp) + ": " + (c.aiClone ? sim::cloneTauntLine(ev.count, int(ev.intensity))
                                                       : sim::tauntLine(c.rosterId, ev.count, int(ev.intensity)));
            tauntCol[0] = c.r; tauntCol[1] = c.g; tauntCol[2] = c.b;
            tauntUntil = simEngine.getTick() + 180;
            pushMsg(feed, tauntText, 0.85f, 0.88f, 0.95f, 240);
            break;
        }
        case sim::SimEvent::Boost:
            if (ev.carIndex >= 0 && ev.carIndex < sim::MAX_RACERS && isLocalCar(ev.carIndex) && audio) audio->playNitroPickup();
            break;
        case sim::SimEvent::Landing: {
            // a jump ends: dust, a shake as hard as the landing, "BIG AIR" after a long flight
            const float hard = std::clamp((ev.intensity - 6.0f) / 14.0f, 0.0f, 1.0f);
            if (ev.carIndex == localCar()) {
                view1.shake = std::max(view1.shake, hard * 0.8f);
                ++statLandings;
            }
            else if (ev.carIndex == 1 && !isOnline()) view2.shake = std::max(view2.shake, hard * 0.8f);
            for (int k = 0; k < 6 + int(hard * 14.0f); ++k)
                spawnParticle(Particle::Smoke, ev.x + rnd(-1.4f, 1.4f), ev.y + 0.3f, ev.z + rnd(-1.4f, 1.4f),
                              rnd(-0.04f, 0.04f), rnd(0.01f, 0.04f), rnd(-0.04f, 0.04f),
                              0.62f, 0.57f, 0.50f, 0.55f, rnd(0.7f, 1.2f), 1.0f, 1.1f);
            if (ev.carIndex >= 0 && ev.carIndex < sim::MAX_RACERS && ev.count >= 30) {
                carFx[ev.carIndex].bigAir = float(ev.count) / 60.0f;
                carFx[ev.carIndex].bigAirUntil = simEngine.getTick() + 100;
            }
            break;
        }
        }
    }
}

void GLWidget::saveScreenshot(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (grabFramebuffer().save(path)) qDebug() << "[Screenshot] saved" << path;
    else                              qDebug() << "[Screenshot] FAILED" << path;
}

// Scripted capture used to make images for the project report:
// garage menu with the map picker -> a short race on every map -> split screen -> quit.
void GLWidget::myCarColor(float &r, float &g, float &b) const
{
    r = CAR_PRESETS[selectedCarPreset].r;
    g = CAR_PRESETS[selectedCarPreset].g;
    b = CAR_PRESETS[selectedCarPreset].b;
}

void GLWidget::runAutoShot()
{
    if (!netTest.isEmpty()) { runNetAutoShot(); return; }
    const int f = ++autoShotFrame;
    auto shot = [&](const QString &name) { saveScreenshot(autoShotDir + "/" + name + ".png"); };

    // CAR_RACING_AUTOSHOT_STORY: the menu, every line of the prologue (with its live crash)
    // and of chapter 1's scene, its rules card, then the start of the vs CPU race
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_STORY")) {
        static int next = 60, n = 0;
        if (f == 40) shot("story_00_menu");               // (no STORY MODE item: it is inside every mode)
        if (f == 56) { cpuCount = 5; storyMode = sim::GameSim::ModeSingle; storyBeginScene(0, 0); next = f + 100; }
        if (f >= next && story == StoryScreen::Scene && storyRules == 1) {   // the rules card before the race
            shot(QString("story_%1_ch%2_rules").arg(++n, 2, 10, QChar('0')).arg(storyChapter));
            storyRules = 2;
            storyEndScene();
            next = f + 30;
        } else if (f >= next && story == StoryScreen::Scene) {
            shot(QString("story_%1_ch%2_line%3").arg(++n, 2, 10, QChar('0')).arg(storyChapter).arg(storyLine));
            storyNextLine();                             // (shows the whole line)
            storyNextLine();                             // the next line
            const bool live = storyChapter == 0 && storyLine == 3;
            next = f + (live ? 160 : 100);
            if (live) n += 0;
        }
        if (story == StoryScreen::Scene && storyChapter == 0 && storyLine == 3 && f == next - 60)
            shot(QString("story_%1_ch0_live_crash").arg(++n, 2, 10, QChar('0')));
        if (story == StoryScreen::Race && f >= next) {
            static int raceAt = -1;
            if (raceAt < 0) raceAt = f;
            if (f == raceAt + 150) shot("story_90_race_goal");
            if (f == raceAt + 160) QCoreApplication::quit();
        }
        if (f > 60 * 90) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_CRASH: crashes - health bars over damaged cars, the drivers close up,
    // a wreck that throws your driver out (seen from the side, frame by frame through the
    // flight, the fall, the run back and the hop in), then a wreck from the chase camera
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_CRASH")) {
        static int wreckAt = -1, shotMask = 0, second = -1;
        static float wy = 0, wfx = 0, wfz = 0;
        auto &cars = simEngine.getCars();
        if (f == 10) { cpuCount = 4; selectedMapPreset = 1; startRace(sim::GameSim::ModeSingle); autoDrive = true; }
        if (cars.empty()) return;
        sim::Car &me = cars[0];
        if (f == 400 && cars.size() > 3) { cars[1].health = 55.0f; cars[2].health = 28.0f; cars[3].health = 12.0f; }
        if (f == 440) shot("crash_health_bars");
        if (f >= 450 && f < 480) {                       // beside your car, looking at the driver
            const float fx = std::sin(me.heading), fz = -std::cos(me.heading), rx = -fz, rz = fx;
            camOverride = true;
            camEye[0] = me.x - rx * 1.4f + fx * 3.6f; camEye[1] = me.y + 1.9f; camEye[2] = me.z - rz * 1.4f + fz * 3.6f;
            camAt[0] = me.x - rx * 0.34f; camAt[1] = me.y + 0.95f; camAt[2] = me.z - rz * 0.34f;
        }
        if (f == 478) shot("crash_driver_closeup");
        if (f == 490) {
            camOverride = false;
            wy = me.y; wfx = std::sin(me.heading); wfz = -std::cos(me.heading);
            simEngine.forceWreck(0, true);
            wreckAt = f;
        }
        if (wreckAt > 0 && second < 0) {                 // a camera at the side, following the car and its driver
            const float rx = -wfz, rz = wfx;
            float tx = me.x, tz = me.z, ty = me.y + 1.0f;
            EjectedDriver ed;
            if (ejectedDriver(me, ed)) { tx = 0.5f * (tx + ed.x); tz = 0.5f * (tz + ed.z); ty = std::max(ty, 0.5f * (ty + ed.y + 0.6f)); }
            camOverride = true;
            camEye[0] = tx - rx * 11.0f - wfx * 4.0f; camEye[1] = wy + 3.0f; camEye[2] = tz - rz * 11.0f - wfz * 4.0f;
            camAt[0] = tx; camAt[1] = ty; camAt[2] = tz;
            static const int AT[8] = {6, 20, 40, 65, 100, 130, 155, 172};
            for (int k = 0; k < 8; ++k)
                if (!(shotMask & (1 << k)) && me.wreck >= AT[k]) {
                    shotMask |= 1 << k;
                    shot(QString("crash_eject_%1_t%2").arg(k + 1).arg(me.wreck));
                }
            if (me.wreck == 0 && me.ghost > 0 && !(shotMask & 256)) { shotMask |= 256; shot("crash_back_ghost"); }
            if (me.wreck == 0 && me.ghost == 0 && (shotMask & 256)) { camOverride = false; second = f + 120; }
        }
        if (second > 0 && f == second) simEngine.forceWreck(0, false);
        if (second > 0 && f == second + 20) shot("crash_chase_wrecked");
        if (second > 0 && f == second + 70) shot("crash_chase_settled");
        if (second > 0 && f == second + 200) QCoreApplication::quit();
        if (f > 60 * 120) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_RAMPS: River Serpent - a side ramp and a twisted one from beside and
    // behind (run-up, in the air, landed), holding L (the boost line: 2 s, 5 s, 11 s), the
    // LAST CHANCE line after a wreck, being eliminated, and the story's rules cards
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_RAMPS")) {
        static int phase = 0, at = 0, mask = 0;
        auto &cars = simEngine.getCars();
        auto putBefore = [&](const sim::GameSim::SideRamp &r, float back) {
            sim::Car &me = cars[0];
            me.path = 0; me.s = simEngine.wrapS(r.s - back); me.d = r.d; me.hint = int(me.s);
            simEngine.pointAt(me.s, me.d, me.x, me.z);
            me.y = simEngine.heightAt(me.s, me.d); me.heading = simEngine.headingAt(me.s);
            me.speed = 1.1f; me.air = 0; me.vy = 0.0f;
            simEngine.locate(me);
            autoLaneS = r.s; autoLaneD = r.d;
        };
        auto findRamp = [&](bool twist) -> int {
            const auto &R = simEngine.sideRamps();
            for (int k = 0; k < int(R.size()); ++k) if ((R[size_t(k)].twist != 0.0f) == twist && R[size_t(k)].fromLap == 0) return k;
            return -1;
        };
        if (f == 10) { cpuCount = 3; selectedMapPreset = 5; startRace(sim::GameSim::ModeSingle); autoDrive = true; }
        if (cars.empty()) return;
        sim::Car &me = cars[0];
        if (f == 20) {                                   // a still of a ramp from beside the road
            const int k = findRamp(false);
            if (k >= 0) {
                const auto &r = simEngine.sideRamps()[size_t(k)];
                float x, z, ax, az;
                simEngine.pointAt(r.s - 30.0f, -r.d * 0.2f, x, z);
                simEngine.pointAt(r.s - 8.0f, r.d, ax, az);
                camOverride = true;
                camEye[0] = x; camEye[1] = simEngine.heightAt(r.s, 0.0f) + 4.0f; camEye[2] = z;
                camAt[0] = ax; camAt[1] = simEngine.heightAt(r.s, 0.0f) + 0.8f; camAt[2] = az;
            }
        }
        if (f == 40) shot("ramp_01_side_ramp");
        if (f == 44) {
            const int k = findRamp(true);
            if (k >= 0) {
                const auto &r = simEngine.sideRamps()[size_t(k)];
                float x, z, ax, az;
                simEngine.pointAt(r.s - 34.0f, -r.d * 0.2f, x, z);
                simEngine.pointAt(r.s - 6.0f, r.d, ax, az);
                camEye[0] = x; camEye[1] = simEngine.heightAt(r.s, 0.0f) + 5.0f; camEye[2] = z;
                camAt[0] = ax; camAt[1] = simEngine.heightAt(r.s, 0.0f) + 0.8f; camAt[2] = az;
            }
        }
        if (f == 64) { shot("ramp_02_twisted_ramp"); camOverride = false; }
        // a run at each kind once the race is on
        for (int kind = 0; kind < 2; ++kind) {
            const int f0 = 260 + kind * 420;
            if (f == f0) { const int k = findRamp(kind == 1); if (k >= 0) putBefore(simEngine.sideRamps()[size_t(k)], 110.0f); mask = 0; at = 0; }
            if (f > f0 && f < f0 + 400) {
                if (me.air > 0 && !(mask & 1) && me.air > 8)  { mask |= 1; shot(QString("ramp_%1_air_takeoff").arg(kind ? "twist" : "side")); }
                if (me.air > 0 && !(mask & 2) && me.air > 32) { mask |= 2; shot(QString("ramp_%1_air_high").arg(kind ? "twist" : "side")); }
                if (me.air > 0 && !(mask & 8) && me.air > 50) { mask |= 8; shot(QString("ramp_%1_air_late").arg(kind ? "twist" : "side")); }
                if (me.air == 0 && (mask & 2) && !(mask & 4)) { mask |= 4; at = f; }
                if ((mask & 4) && f == at + 12) { shot(QString("ramp_%1_landed").arg(kind ? "twist" : "side")); autoLaneS = -1.0f; }
            }
        }
        // holding L on River Serpent's long start straight
        if (f == 1120) { me.s = simEngine.wrapS(150.0f); me.d = 0.0f; me.hint = int(me.s); simEngine.pointAt(me.s, me.d, me.x, me.z);
                         me.heading = simEngine.headingAt(me.s); me.health = 100.0f; me.speed = 1.0f; simEngine.locate(me); keys.insert(Qt::Key_L); }
        if (f == 1120 + 120) shot("boost_2s");
        if (f == 1120 + 300) shot("boost_5s");
        if (f == 1120 + 660) shot("boost_11s_overheat");
        if (f == 1120 + 700) keys.remove(Qt::Key_L);
        if (f == 1120 + 760) shot("boost_after");
        if (f == 1950) { me.lastWreckProg = me.progress - 2500.0f; }
        if (f == 1980) shot("last_chance");
        if (f == 2000) { me.lastWreckProg = me.progress - 100.0f; me.health = 3.0f; }
        if (f == 2001) keys.insert(Qt::Key_L);
        if (me.out && !(mask & 64)) { mask |= 64; at = f; }
        if ((mask & 64) && f == at + 60) { shot("eliminated"); keys.remove(Qt::Key_L); }
        if ((mask & 64) && f == at + 70) { phase = 1; openStory(); storyUnlocked = 5; storyPick = 0; storyRules = 1; }
        if (phase == 1) {
            const int t = f - at - 70;
            if (t > 0 && t % 30 == 0 && t / 30 <= 6) { shot(QString("rules_%1").arg(t / 30)); storyPick = t / 30; }
            if (t == 30 * 7) QCoreApplication::quit();
        }
        if (f > 60 * 120) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_BOARD: with CAR_RACING_API pointing at a test server - the account
    // TESTB3AA (sharing races) races Canyon Run once; the race and its recording go up; then
    // MY PROFILE and the LEADERBOARD
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_BOARD")) {
        static int phase = 0, at = 0;
        auto step = [&](int next) { phase = next; at = f; };
        if (f == 5) { accountForget(); cloneName = "TESTB3AA"; openAccountScreen(true); acctUser = "TESTB3AA";
                      acctPass = acctPass2 = "test-pass-b3"; acctShare = true; accountSubmit(); step(1); }
        if (phase == 1 && syncState == SyncState::Synced) { cpuCount = 2; selectedMapPreset = 1; simEngine.setLaps(1);
                                                            startRace(sim::GameSim::ModeSingle); autoDrive = true; step(2); }
        if (phase == 2 && simEngine.getState() == State::Finished && f > at + 120) step(3);
        if (phase == 3 && f == at + 240) { simEngine.setState(State::Menu); autoDrive = false; shownProfile = loadProfile(profileName(0)); profileOpen = true; }
        if (phase == 3 && f == at + 260) { shot("board_profile"); profileOpen = false; openLeaderboard(1); step(4); }
        if (phase == 4 && !boardBusy && f > at + 20) {
            shot("board");
            std::printf("BOARD TEST: %d rows, first %s %.1f, share %d, status '%s'\n", int(boardRows.size()),
                        boardRows.empty() ? "-" : boardRows[0].user.toLatin1().constData(), boardRows.empty() ? 0.0 : double(boardRows[0].best),
                        int(accountShare), boardStatus.toLatin1().constData());
            std::fflush(stdout);
            QCoreApplication::quit();
        }
        if (f > 60 * 240) { std::printf("BOARD TEST: timed out in phase %d\n", phase); std::fflush(stdout); QCoreApplication::quit(); }
        return;
    }

    // CAR_RACING_AUTOSHOT_OBST: every map, the car standing 30 m before its first concrete block
    // and its first tyre stack (the obstacles the owner asked for), and before its last big ramp
    // (CAR_RACING_AUTOSHOT_OBST)
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_OBST")) {
        static const char *KIND[] = {"mine", "stone", "ramp"};
        const int k = (f - 10) / 60, map = k / 3, kind = k % 3, at = (f - 10) % 60;
        if (map >= sim::NUM_MAPS) { QCoreApplication::quit(); return; }
        if (f < 10) return;
        if (at == 0) {
            keys.clear();
            cpuCount = 1;
            selectedMapPreset = map;
            startRace(sim::GameSim::ModeSingle);
            simEngine.setState(State::Racing);
            float target = -1.0f;
            int lap = 0;
            for (const sim::Obstacle &o : simEngine.getObstacles())
                if (((kind == 0 && o.type == sim::Obstacle::Mine) || (kind == 1 && o.type == sim::Obstacle::Stone))) { target = o.s; lap = o.fromLap; break; }
            if (kind == 2 && !simEngine.ramps().empty()) target = simEngine.ramps().back().lip;
            if (target < 0.0f) return;
            for (int i = 0; i < int(simEngine.getCars().size()); ++i) {
                sim::Car &c = simEngine.getCars()[i];
                c.s = simEngine.wrapS(target - (i == 0 ? 32.0f : 400.0f));
                c.d = 0.0f; c.lap = lap; c.hint = int(c.s); c.speed = 0.0f;
                simEngine.pointAt(c.s, c.d, c.x, c.z);
                c.y = simEngine.heightAt(c.s, c.d);
                c.heading = simEngine.headingAt(c.s);
                simEngine.locate(c);
                c.aiCheckProg = c.progress;
            }
        }
        if (at == 50) shot(QString("obst_map%1_%2").arg(map + 1).arg(KIND[kind]));
        return;
    }

    // CAR_RACING_AUTOSHOT_GATE: Canyon Run, you leading two CPUs: the shortcut is closed to you -
    // the barrier across it and the HUD line, just before the fork
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_GATE")) {
        if (f == 10) { cpuCount = 2; selectedMapPreset = 1; simEngine.setLaps(1); startRace(sim::GameSim::ModeSingle); autoDrive = true; }
        if (f > 60 * 8 && simEngine.getState() == State::Racing && !simEngine.getCars().empty()) {
            const sim::Car &me = simEngine.getCars()[0];
            const float to = simEngine.toShortcut(me);
            if (to < 55.0f && to > 30.0f) {
                std::printf("GATE: place %d, closed %d\n", simEngine.rankOf(0), int(me.cutClosed));
                std::fflush(stdout);
                shot("shortcut_closed");
                QCoreApplication::quit();
            }
        }
        if (f > 60 * 150) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_ACCOUNT: with CAR_RACING_API pointing at a test server - create the
    // account TESTB2AA, race once (the profile goes up), log out, a wrong password, then log in
    // again with this PC's profile gone (it comes back down from the server). Its own settings
    // (acctApp): your real login is never touched.
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_ACCOUNT")) {
        static int phase = 0, at = 0;
        auto step = [&](int next) { phase = next; at = f; };
        if (f == 5) { accountForget(); openAccountScreen(true); acctUser = "TESTB2AA"; acctPass = acctPass2 = "test-pass-b2"; acctShare = false; acctRow = 4; }
        if (f == 20) shot("account_create");
        if (f == 25) { accountSubmit(); step(1); }
        if (phase == 1 && syncState == SyncState::Synced) { menuSelection = M_PROFILE; step(2); }
        if (phase == 2 && f == at + 20) shot("account_menu");
        if (phase == 2 && f == at + 25) { shownProfile = loadProfile(profileName(0)); profileOpen = true; }
        if (phase == 2 && f == at + 40) { shot("account_profile_new"); profileOpen = false; cpuCount = 2; selectedMapPreset = 1;
                                          simEngine.setLaps(1); startRace(sim::GameSim::ModeSingle); autoDrive = true; step(3); }
        if (phase == 3 && simEngine.getState() == State::Finished && syncState == SyncState::Synced && f > at + 120) step(4);
        if (phase == 4 && f == at + 30) { simEngine.setState(State::Menu); autoDrive = false; accountLogout();
                                          QFile::remove(profilePath("TESTB2AA")); openAccountScreen(false);
                                          acctUser = "TESTB2AA"; acctPass = "wrong-password"; acctRow = 2; accountSubmit(); step(5); }
        if (phase == 5 && !acctBusy && f > at + 10) { shot("account_wrong"); acctPass = "test-pass-b2"; accountSubmit(); step(6); }
        if (phase == 6 && syncState == SyncState::Synced && f > at + 10) { shownProfile = loadProfile(profileName(0)); profileOpen = true; step(7); }
        if (phase == 7 && f == at + 20) {
            shot("account_profile_synced");
            std::printf("ACCOUNT TEST: user %s, profile races after download %d, sync %d\n", accountUser.toLatin1().constData(),
                        shownProfile.races, int(syncState));
            std::fflush(stdout);
            QCoreApplication::quit();
        }
        if (f > 60 * 240) { std::printf("ACCOUNT TEST: timed out in phase %d (%s)\n", phase, acctStatus.toLatin1().constData()); std::fflush(stdout); QCoreApplication::quit(); }
        return;
    }

    // CAR_RACING_AUTOSHOT_CLONE: the TEST_CLONE profile's clone (tools/ai_bench clonefiles
    // into the profiles folder first): the menu item, MY PROFILE, a race with the clone in it
    // and F4 on, and the results once this race (recorded) has trained the clone again
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_CLONE")) {
        static int doneAt = -1;
        if (f == 5) { cloneName = "TEST_CLONE"; cloneLoad(); menuSelection = M_CLONE; }
        if (f == 20) shot("clone_menu");
        if (f == 30) { shownProfile = loadProfile(cloneName); profileOpen = true; }
        if (f == 45) shot("clone_profile");
        if (f == 50) { profileOpen = false; cpuCount = 5; selectedMapPreset = 1; simEngine.setLaps(1); startRace(sim::GameSim::ModeSingle);
                       autoDrive = true; showCpuMinds = true; }
        if (f > 60 * 8 && simEngine.getState() == State::Racing && !simEngine.getCars().empty()) {
            static bool shotRace = false;                // when the clone itself drives, close to us
            const auto &cs = simEngine.getCars();
            const sim::Car &cl = cs.back();
            if (!shotRace && cl.aiClone && cl.aiBrainDriving && std::fabs(simEngine.aheadOf(cs[0].s, cl.s)) < 40.0f) { shotRace = true; shot("clone_race_f4"); }
            if (!shotRace && f == 60 * 40) { shotRace = true; shot("clone_race_f4"); }
        }
        if (simEngine.getState() == State::Finished && !cloneTraining && cloneSummary.startsWith("YOUR CLONE LEARNED") && doneAt < 0) doneAt = f;
        if (doneAt >= 0 && f == doneAt + 60) { shot("clone_results"); QCoreApplication::quit(); }
        if (f > 60 * 200) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_PROFILE: a vs CPU race on River Serpent with F3 on (the player
    // model's live view), then the MY PROFILE screen built from that race and two made-up ones
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_PROFILE")) {
        static int doneAt = -1;
        if (f == 10) { cpuCount = 5; selectedMapPreset = 5; simEngine.setLaps(1); startRace(sim::GameSim::ModeSingle); autoDrive = true; showAiDebug = true; }
        if (f == 60 * 5) { showAiDebug = false; showCpuMinds = true; }
        if (f >= 60 * 6 && f <= 60 * 6 + 20 && !simEngine.getCars().empty()) {   // looking back at the pack
            const sim::Car &me = simEngine.getCars()[0];
            const float fx = std::sin(me.heading), fz = -std::cos(me.heading);
            camOverride = true;
            camEye[0] = me.x + fx * 10.0f; camEye[1] = me.y + 4.5f; camEye[2] = me.z + fz * 10.0f;
            camAt[0] = me.x - fx * 30.0f; camAt[1] = me.y + 1.0f; camAt[2] = me.z - fz * 30.0f;
        }
        if (f == 60 * 6 + 20) shot("cpu_minds_f4");
        if (f == 60 * 7) { showCpuMinds = false; showAiDebug = true; camOverride = false; }
        if (f == 60 * 40) shot("profile_f3_overlay");
        static bool tauntShot = false;
        if (!tauntShot && tauntUntil > simEngine.getTick() + 150 && simEngine.getState() == State::Racing) {
            tauntShot = true;
            shot("taunt_bubble");
        }
        if (simEngine.getState() == State::Finished && doneAt < 0) doneAt = f;
        if (doneAt > 0 && f == doneAt + 20) shot("results_rival_line");
        if (doneAt > 0 && f == doneAt + 30) {
            showAiDebug = false;
            sim::PlayerProfile p;
            p.name = profileName(0);
            for (int k = 0; k < 3; ++k) {
                const sim::Car &c = simEngine.getCars()[0];
                sim::RaceSummary r;
                r.map = k == 0 ? simEngine.getMap() : (k == 1 ? 1 : 3);
                r.place = k == 0 ? simEngine.rankOf(0) : 2 + k; r.cars = 6; r.wrecks = k == 2 ? 1 : c.wrecks;
                r.rams = c.crashesCaused; r.bestLap = c.bestLap; r.km = simEngine.raceObs(0).metres / 1000.0f;
                p.addRace(simEngine.raceObs(0), r, simEngine.rivalRaces(0));
            }
            {                                            // saved and read back: the same profile?
                sim::PlayerProfile t = p;
                t.name = "TEST_ROUNDTRIP";
                saveProfile(t);
                const sim::PlayerProfile back = loadProfile(t.name);
                const bool same = back.races == t.races && std::fabs(back.traits.skill - t.traits.skill) < 1e-4f
                    && std::fabs(back.traits.risk - t.traits.risk) < 1e-4f && back.recent.size() == t.recent.size()
                    && back.habits[size_t(t.recent[0].map)].rampsOffered == t.habits[size_t(t.recent[0].map)].rampsOffered;
                QFile out(autoShotDir + "/roundtrip.txt");
                if (out.open(QIODevice::WriteOnly)) out.write(same ? "ROUNDTRIP OK\n" : "ROUNDTRIP DIFFERENT\n");
                QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles/TEST_ROUNDTRIP.json");
            }
            shownProfile = p;
            simEngine.setState(State::Menu);
            profileOpen = true;
        }
        if (doneAt > 0 && f == doneAt + 60) shot("profile_screen");
        if ((doneAt > 0 && f == doneAt + 70) || f > 60 * 200) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_LOGOUT: the LOG OUT menu item (a pretend account in the test settings)
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_LOGOUT")) {
        if (f == 5) { accountScreen = AccountScreen::Off; accountUser = "TESTUSR1"; accountToken = "not-a-real-token"; menuSelection = M_ACCOUNT; }
        if (f == 30) shot("logout_1_menu");
        if (f == 35) menuAccount();
        if (f == 55) shot("logout_2_login_screen");
        if (f == 60) { accountScreen = AccountScreen::Off; menuSelection = M_ACCOUNT; }
        if (f == 80) shot("logout_3_menu_logged_out");
        if (f == 85) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_NEWMAPS: Desert Zigzag and Ghat Road, the CPU logic driving your car
    // among 5 CPUs: pictures along each road, then the menu's map card
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_NEWMAPS")) {
        static int map = 6, startF = 10;
        static const int AT[6] = {300, 1500, 2700, 3900, 5400, 7200};
        const int t = f - startF;
        if (t == 0) { cpuCount = 5; selectedMapPreset = map; startRace(sim::GameSim::ModeSingle); autoDrive = true; }
        for (int k = 0; k < 6; ++k)
            if (t == AT[k]) shot(QString("newmap_%1_%2").arg(QString(sim::MAPS[map].name).replace(' ', '_').toLower()).arg(k));
        if (t > 0 && (t == 7300 || simEngine.getState() == State::Finished)) {
            if (map == 6) { map = 7; startF = f + 5; }
            else if (t < 100000) { simEngine.setState(State::Menu); selectedMapPreset = 7; startF = 1 << 30; }
        }
        if (startF == (1 << 30)) { static int menuF = f; if (f == menuF + 40) shot("newmap_menu_card"); if (f == menuF + 50) QCoreApplication::quit(); }
        return;
    }

    // CAR_RACING_AUTOSHOT_UI: the pause screen (two choices), the online menu, and the online
    // winner strip (shown here offline: you stand at the start while the CPUs finish)
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_UI")) {
        static int wonAt = -1;
        if (f == 10) { cpuCount = 3; selectedMapPreset = 3; startRace(sim::GameSim::ModeSingle); simEngine.setLaps(1); autoDrive = true; }
        if (f == 400) { simEngine.setState(State::Paused); pauseChoice = 0; }
        if (f == 410) shot("ui_pause");
        if (f == 412) pauseChoice = 2;
        if (f == 420) shot("ui_pause_menu_choice");
        if (f == 425) { simEngine.setState(State::Racing); elapsedTimer.restart(); autoDrive = false; uiTestOnlineBanner = true; }
        if (wonAt < 0 && f > 425 && simEngine.getWinner() != 0) wonAt = f;
        if (wonAt > 0 && f == wonAt + 30) shot("ui_online_winner_strip");
        if (wonAt > 0 && f == wonAt + 35) { simEngine.setState(State::Menu); openOnline(); }
        if (wonAt > 0 && f == wonAt + 60) shot("ui_online_menu");
        if ((wonAt > 0 && f == wonAt + 65) || f > 60 * 300) QCoreApplication::quit();
        return;
    }

    // CAR_RACING_AUTOSHOT_FINISH: one whole vs CPU race against 5 CPUs, with the CPU logic
    // driving your car too, then a picture of the results screen
    if (qEnvironmentVariableIsSet("CAR_RACING_AUTOSHOT_FINISH")) {
        static int doneAt = -1;
        if (f == 10) { cpuCount = 5; selectedMapPreset = 4; startRace(sim::GameSim::ModeSingle); autoDrive = true; }
        if (f == 600) shot("race_5_cpus");
        static int lapShots = 0;                         // the new-lap banners: LAP 2/3, FINAL LAP
        const sim::Car &me = simEngine.getCars()[0];
        if (lapShots < 2 && me.lap == lapShots + 1 && me.s > 40.0f) shot(++lapShots == 1 ? "lap_2_banner" : "final_lap_banner");
        if (simEngine.getState() == State::Finished && doneAt < 0) doneAt = f;
        if (doneAt > 0 && f == doneAt + 60) shot("finish_screen");
        if ((doneAt > 0 && f == doneAt + 70) || f > 60 * 260) QCoreApplication::quit();
        return;
    }

    if (f == 20)  accountScreen = AccountScreen::Off;   // (no log-in box over the menu shots)
    if (f == 70)  shot("00_menu_showroom");
    if (f == 40)  { menuSelection = M_MAP; cycleMenuOption(M_MAP, 1); }   // highlight the map picker
    if (f == 90)  shot("01_menu_map_picker");
    if (f == 95)  cycleMenuOption(M_MAP, -1);
    // the big map card with other maps and CPU levels (back to the defaults afterwards)
    if (f == 98)  { menuSelection = M_PROFILE; cycleMenuOption(M_MAP, -1); cycleMenuOption(M_PROFILE, 1); }   // Coastal Highway, HARD
    if (f == 104) shot("01_menu_coastal_hard");
    if (f == 106) { cycleMenuOption(M_MAP, 2); cycleMenuOption(M_PROFILE, 1); }                            // Canyon Run, EASY
    if (f == 112) shot("01_menu_canyon_easy");
    if (f == 114) { cycleMenuOption(M_MAP, -1); cycleMenuOption(M_PROFILE, 1); menuSelection = M_VSCPU; }  // back: Sunset, MEDIUM

    // one race per map (the CPU logic drives your car): the grid facing the start/finish
    // gantry during the countdown, then a screenshot after ~4 s of racing
    const int PER_MAP = 460, FIRST = 120;
    int m = (f - FIRST) / PER_MAP, t = (f - FIRST) % PER_MAP;
    const QString mapName = (m < sim::NUM_MAPS) ? QString(sim::MAPS[m].name).replace(' ', '_').toLower() : QString();
    if (f >= FIRST && m < sim::NUM_MAPS) {
        if (t == 0) { selectedMapPreset = m; startRace(sim::GameSim::ModeSingle); autoDrive = true; }
        if (t == 150) shot(QString("map%1_%2_grid").arg(m + 1).arg(mapName));
        if (t == 440) shot(QString("map%1_%2").arg(m + 1).arg(mapName));
    }
    const int SPLIT = FIRST + sim::NUM_MAPS * PER_MAP;
    if (f == SPLIT) { keys.clear(); selectedMapPreset = 3; startRace(sim::GameSim::ModeSplitScreen); autoDrive = true; }
    if (f == SPLIT + 420) shot("split_screen_city");

    // Places worth a look, your car put there during the countdown: Neon City's flat crossing
    // (a bridge from milestone 1d on) from both roads, and the hills and banked corners
    struct Spot { int map; float s; const char *name; int path = 0; };
    static const Spot SPOTS[] = {
        {3, 105.0f, "neon_crossing_main"}, {3, 2245.0f, "neon_crossing_other"},
        {0, 3300.0f, "hill_sunset_climb"}, {1, 1375.0f, "hill_canyon_banked"},
        {2, 651.0f, "hill_alpine_switchbacks"},
        {2, 1500.0f, "hill_alpine_summit"}, {2, 2251.0f, "hill_alpine_descent"},
        {3, 520.0f, "hill_neon_cutting"}, {4, 3500.0f, "hill_coastal_cliffs"},
        {0, 5050.0f, "struct_sunset_bridge"}, {0, 5190.0f, "struct_sunset_on_bridge"},
        {1, 2163.0f, "struct_canyon_tunnel"}, {1, 2323.0f, "struct_canyon_in_tunnel"},
        {2, 1551.0f, "struct_alpine_gallery"}, {2, 1691.0f, "struct_alpine_in_gallery"},
        {2, 1920.0f, "struct_alpine_bridge"}, {3, 750.0f, "struct_neon_in_tunnel"},
        {3, 2150.0f, "struct_neon_highway"}, {4, 4080.0f, "struct_coastal_tunnel"},
        {4, 5790.0f, "struct_coastal_bridge"},
        {1, 1139.0f, "cut_canyon_fork"}, {1, 150.0f, "cut_canyon_on", 1},
        {2, 120.0f, "cut_alpine_on", 1}, {3, 120.0f, "cut_neon_on", 1},
        {0, 110.0f, "start_sunset"}, {1, 3510.0f, "start_canyon"}, {2, 3280.0f, "start_alpine"},
        {4, 110.0f, "start_coastal"}, {0, 8760.0f, "finish_sunset"},
        {4, 8775.0f, "finish_coastal"}, {4, 1500.0f, "sprint_coastal_traffic"},
    };
    const int SPOT0 = SPLIT + 440, NSPOTS = int(sizeof(SPOTS) / sizeof(SPOTS[0]));
    for (int k = 0; k < NSPOTS; ++k) {
        if (f == SPOT0 + k * 40) {
            keys.clear();
            autoDrive = false;
            selectedMapPreset = SPOTS[k].map;
            startRace(sim::GameSim::ModeSingle);
            sim::Car &c = simEngine.getCars()[0];
            c.path = SPOTS[k].path;
            c.s = SPOTS[k].s;
            c.d = 0.0f; c.lap = 0; c.hint = int(c.s);
            simEngine.pointAt(c.s, c.d, c.x, c.z, c.path);
            c.y = simEngine.heightAt(c.s, c.d, c.path);
            c.heading = simEngine.headingAt(c.s, c.path);
        }
        if (f == SPOT0 + k * 40 + 30) shot(SPOTS[k].name);
    }
    // Every jump in a real run-up: your car (driven by the CPU logic) 160 m before the lip,
    // a picture mid-air and one just after landing
    struct JumpShot { int map; bool nitro; const char *name; };
    static const JumpShot JUMPS[] = {
        {0, false, "jump_sunset"}, {1, false, "jump_canyon"}, {2, false, "jump_alpine"},
        {3, false, "jump_neon"}, {3, true, "jump_neon_nitro"},
    };
    const int JUMP0 = SPOT0 + NSPOTS * 40, NJUMPS = int(sizeof(JUMPS) / sizeof(JUMPS[0]));
    static int jumpState = 0, landedAt = -1;             // 0 run-up, 3 ramp shot taken, 1 flying shot taken, 2 done
    for (int k = 0; k < NJUMPS; ++k) {
        const int f0 = JUMP0 + k * 360;
        if (f == f0) {
            keys.clear();
            selectedMapPreset = JUMPS[k].map;
            startRace(sim::GameSim::ModeSingle);
            simEngine.setState(State::Racing);
            sim::Car &c = simEngine.getCars()[0];
            c.s = simEngine.wrapS(simEngine.ramps()[0].lip - 160.0f);
            c.d = 0.0f; c.lap = 0; c.hint = int(c.s);
            simEngine.pointAt(c.s, c.d, c.x, c.z);
            c.y = simEngine.heightAt(c.s, c.d);
            c.heading = simEngine.headingAt(c.s);
            simEngine.locate(c);
            c.aiCheckProg = c.progress;
            c.speed = JUMPS[k].nitro ? sim::NITRO_SPEED : sim::MAX_SPEED;
            if (JUMPS[k].nitro) { c.nitroTimer = 150; c.nitroCharges = 0; }
            autoDrive = true;
            jumpState = 0;
            landedAt = -1;
        }
        if (f > f0 && f < f0 + 360) {
            const sim::Car &c = simEngine.getCars()[0];
            const float toLip = simEngine.aheadOf(c.s, simEngine.ramps()[0].lip);
            if (jumpState == 0 && toLip < 45.0f && toLip > 0.0f) { shot(QString(JUMPS[k].name) + "_ramp"); jumpState = 3; }
            if (jumpState == 3 && c.air >= 28) { shot(QString(JUMPS[k].name) + "_air"); jumpState = 1; }
            if (jumpState == 1 && c.air == 0 && landedAt < 0) landedAt = f;
            if (jumpState == 1 && landedAt > 0 && f == landedAt + 12) { shot(QString(JUMPS[k].name) + "_landed"); jumpState = 2; }
        }
    }
    if (f == JUMP0 + NJUMPS * 360) QCoreApplication::quit();
}

// Automatic graphics: every 2 s of driving, look at how the frames went. Too slow: one level down
// at once. Smooth for a while: try one level up (and wait twice as long if that was too much).
// The player never chooses: the game stays smooth and gives up looks for it.
void GLWidget::gfxSet(int level)
{
    gfxLevel = std::clamp(level, 0, 3);
    VIEW_DIST = GFX_VIEW[gfxLevel];
    QSettings("TPP", "CarRacing").setValue("gfx/level", gfxLevel);
}

void GLWidget::gfxFrame(float rawDt)
{
    if (rawDt <= 0.0f || rawDt > 0.25f) return;          // (a hidden tab, not a slow frame)
    const float ms = rawDt * 1000.0f;
    meterTime += rawDt;
    ++meterFrames;
    meterWorstNow = std::max(meterWorstNow, ms);
    if (meterTime >= 1.0f) {
        meterFps = float(meterFrames) / meterTime;
        meterWorst = meterWorstNow;
        meterTime = 0.0f; meterFrames = 0; meterWorstNow = 0.0f;
    }
    if (gfxFixed || !autoShotDir.isEmpty() || !netTest.isEmpty()) return;
    const State st = simEngine.getState();
    if (story != StoryScreen::Scene && st != State::Racing && st != State::Countdown) return;   // (menus cost little)
    gfxSum += rawDt;
    ++gfxCount;
    if (ms > 33.0f) ++gfxSlow;
    gfxSinceRaise += rawDt;
    if (gfxSum < 2.0f) return;
    const float avg = gfxSum * 1000.0f / float(gfxCount), slow = float(gfxSlow) / float(gfxCount);
    gfxSum = 0.0f; gfxCount = 0; gfxSlow = 0;
    if ((avg > 22.0f || slow > 0.12f)) {
        if (gfxSinceRaise < 12.0f) gfxPatience = std::min(320.0f, gfxPatience * 2.0f);   // (that raise was too much)
        gfxCalm = 0.0f;
        if (gfxLevel < 3) gfxSet(gfxLevel + 1);
    } else if (avg < 18.5f && slow < 0.02f && gfxLevel > 0) {
        gfxCalm += 2.0f;
        if (gfxCalm >= gfxPatience) { gfxCalm = 0.0f; gfxSinceRaise = 0.0f; gfxSet(gfxLevel - 1); }
    } else {
        gfxCalm = 0.0f;
    }
}

// F2: the meter that says why a race feels slow (the frames here, or the connection)
bool GLWidget::netShaky() const
{
    if (!isOnline() || !net || netScreen != NetScreen::Race || netLastSnapAt < 0) return false;
    return netJitter > 160.0 || netClock.elapsed() - netLastSnapAt > 400;
}

void GLWidget::drawMeter(int W, int H, float dpr)
{
    glViewport(0, 0, W, H);
    begin2D(W, H);
    const float ps = std::clamp(float(H) / dpr / 420.0f, 1.3f, 2.4f) * dpr * 0.8f;
    if (!meterOn) {                                      // only the warning: the connection, not the game
        const char *msg = "UNSTABLE CONNECTION";
        const float w = textWidth(msg, ps), x = (float(W) - w) * 0.5f, y = float(H) - 20.0f * dpr - 7.0f * ps;   // (top centre)
        const float pulse = 0.75f + 0.25f * std::sin(float(netClock.elapsed()) * 0.008f);
        glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
        glBegin(GL_QUADS);
        glVertex2f(x - 8.0f * dpr, y - 5.0f * dpr); glVertex2f(x + w + 8.0f * dpr, y - 5.0f * dpr);
        glVertex2f(x + w + 8.0f * dpr, y + 7.0f * ps + 5.0f * dpr); glVertex2f(x - 8.0f * dpr, y + 7.0f * ps + 5.0f * dpr);
        glEnd();
        drawText(msg, x, y, ps, 1.0f, 0.55f * pulse, 0.2f * pulse);
        return;
    }
    char lines[3][128];
    snprintf(lines[0], sizeof lines[0], "%.0f FPS  SLOWEST %.0f MS  GRAPHICS %d OF 3", double(meterFps), double(meterWorst), gfxLevel);
    int n = 1;
    if (isOnline() && net) {
        snprintf(lines[1], sizeof lines[1], "%s  PING %d MS  JITTER %.0f MS  BUFFER %.0f MS", net->fastActive() ? "UDP" : "TCP", net->pingMs, netJitter, netInterpMs);
        snprintf(lines[2], sizeof lines[2], "STALLS %d  LATE %d  CORRECTIONS %d SMALL %d BIG", statStalls, statLate, statSmallFix, statSnaps);
        n = 3;
    }
    const float lh = 7.0f * ps * 1.5f;
    float w = 0.0f;
    for (int i = 0; i < n; ++i) w = std::max(w, textWidth(lines[i], ps));
    const float x = 10.0f * dpr, y1 = float(H) * 0.82f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.65f);
    glBegin(GL_QUADS);
    glVertex2f(x - 6.0f * dpr, y1 - float(n) * lh - 6.0f * dpr); glVertex2f(x + w + 6.0f * dpr, y1 - float(n) * lh - 6.0f * dpr);
    glVertex2f(x + w + 6.0f * dpr, y1 + 6.0f * dpr); glVertex2f(x - 6.0f * dpr, y1 + 6.0f * dpr);
    glEnd();
    for (int i = 0; i < n; ++i) drawText(lines[i], x, y1 - float(i + 1) * lh + 2.0f * dpr, ps, 0.6f, 1.0f, 0.7f);
}

void GLWidget::updateGame()
{
    clonePoll();                                         // (a clone finished training)
    if (!autoShotDir.isEmpty()) runAutoShot();
#ifdef __EMSCRIPTEN__
    storyTestStep();
    EM_ASM({ window.tppBeat = performance.now(); });     // (alive: the page's watchdog, tools/wasm/index.html)
#endif
    if (audio) {                                         // the music for what is on screen
        const bool inRace = (isOnline() || netScreen == NetScreen::Off) && story != StoryScreen::Chapters
                            && story != StoryScreen::Scene && simEngine.getState() != State::Menu
                            && !(netScreen != NetScreen::Off && !isOnline());
        // the race sound from GO until you cross the line (in split screen until both have);
        // the music on every other screen - the countdown, a pause and the results included
        bool driving = false;
        if (inRace && simEngine.getState() == State::Racing) {
            const auto &cs = simEngine.getCars();
            for (int i = 0; i < int(cs.size()); ++i)
                if (isLocalCar(i) && !cs[i].finished) driving = true;
        }
        audio->setMusic(driving ? 1 : !inRace ? 0 : simEngine.getState() == State::Finished ? 2 : 0);
        static int lastSound = -1;                       // (CAR_RACING_AUDIO_TRACE: each change, for tests)
        if (int(driving) != lastSound && qEnvironmentVariableIsSet("CAR_RACING_AUDIO_TRACE")) {
            static const char *ST[] = {"menu", "countdown", "racing", "paused", "finished"};
            std::printf("AUDIO: %s (%s)\n", driving ? "race" : "music", ST[int(simEngine.getState())]);
            std::fflush(stdout);
        }
        lastSound = int(driving);
    }

    float realDt = float(elapsedTimer.nsecsElapsed() * 1e-9);   // not whole ms: they lose ~3% a frame
    elapsedTimer.restart();
    if (realDt <= 0.0f) realDt = 0.016f;
    gfxFrame(realDt);
    if (realDt > 0.25f) realDt = 0.25f;

    for (int i = 0; i < sim::MAX_RACERS; ++i) {
        if (nitroBannerTimer[i] > 0.0f) {
            nitroBannerTimer[i] = std::max(0.0f, nitroBannerTimer[i] - realDt);
        }
    }

    if (story == StoryScreen::Chapters) {
        storyTime += realDt;
        if (audio) audio->update(0.04f, false, false);
        update();
        return;
    }
    if (story == StoryScreen::Scene) {
        storyUpdate(realDt);
        updateParticles(realDt);
        if (audio) audio->update(0.02f, false, false);
        update();
        return;
    }
    if (isOnline() && netStoryHold) {                    // watched it: waiting for the others (or a skip)
        if (audio) audio->update(0.02f, false, false);
        update();
        return;
    }

    if (simEngine.getState() == State::Menu) {
        if (audio) audio->update(0.04f, false, false);
        update();
        return;
    }

    if (simEngine.getState() == State::Paused) {
        if (audio) audio->update(0.0f, false, false);
        update();
        return;
    }

    const float FIXED_DT = 1.0f / 60.0f;
    if (isOnline() && !netTest.isEmpty()) { ++statFrames; if (realDt > 0.025f) ++statSlowFrames; }
    {
        const int now = simEngine.getTick();
        popups.erase(std::remove_if(popups.begin(), popups.end(), [now](const DamagePopup &p) { return now - p.born > 80; }),
                     popups.end());
    }
    // your car wrecked against CPUs: the first moments of the crash in slow motion
    const auto &simCars = simEngine.getCars();
    const bool slowMo = !isOnline() && simEngine.getGameMode() == sim::GameSim::ModeSingle && !simCars.empty()
                        && simCars[0].wreck > 0 && simCars[0].wreck < 50 && autoShotDir.isEmpty();
    if (isOnline()) netUpdate(realDt);          // online: the server runs the race (glwidget_online.cpp)
    else simAccumulator += slowMo ? realDt * 0.35f : realDt;

    while (!isOnline() && simAccumulator >= FIXED_DT) {
        simAccumulator -= FIXED_DT;

        // Prepare inputs
        sim::PlayerInput inputs[2];
        inputs[0].up = isDown(Qt::Key_Up);
        inputs[0].down = isDown(Qt::Key_Down);
        inputs[0].left = isDown(Qt::Key_Left);
        inputs[0].right = isDown(Qt::Key_Right);
        inputs[0].nitro = isDown(Qt::Key_Return) || isDown(Qt::Key_Enter) || isDown(Qt::Key_Slash) || isDown(Qt::Key_N);
        inputs[0].boost = isDown(Qt::Key_L) && simEngine.boostAllowed();   // (no L boost in split screen)

        inputs[1].up = isDown(Qt::Key_W);
        inputs[1].down = isDown(Qt::Key_S);
        inputs[1].left = isDown(Qt::Key_A);
        inputs[1].right = isDown(Qt::Key_D);
        inputs[1].nitro = isDown(Qt::Key_Space);
        if (autoDrive) {                                 // scripted test
            const sim::Car &me = simEngine.getCars()[0];
            inputs[0] = simEngine.computeAIInput(simEngine.getCars()[0], 0, false);
            inputs[0].boost = isDown(Qt::Key_L);
            if (autoLaneS > 0.0f && me.air == 0 && simEngine.aheadOf(me.s, autoLaneS) > 0.0f) {   // hold the ramp's lane
                const float e = autoLaneD - me.d + 3.0f * std::sin(simEngine.headingAt(me.s) - me.heading);
                inputs[0].left = e < -0.25f; inputs[0].right = e > 0.25f;
            }
        }
        if (autoDrive && simEngine.getCars().size() > 1 && simEngine.getGameMode() == sim::GameSim::ModeSplitScreen)
            inputs[1] = simEngine.computeAIInput(simEngine.getCars()[1], 1, false);

        if (cloneRecording) cloneRec.step(simEngine, 0, inputs[0], cloneBook);   // (what you see and do: your clone learns it)
        simEngine.step(inputs, 2);
        processSimEvents(simEngine.getEvents());
        simEngine.clearEvents();
        if (simEngine.getState() == State::Finished && !profileSaved) { profileSaved = true; profileAfterRace(); }
        for (int i = 0; i < sim::MAX_RACERS && i < int(simEngine.getCars().size()); ++i)
            updateCarFx(i, simEngine.getCars()[i]);
    }

    const auto &cars = simEngine.getCars();
    float step = realDt * 60.0f;

    auto updateCarView = [&](CarView &v, const sim::Car &c) {
        if (v.snap) { v.camHeading = c.heading; v.camY = c.y; v.snap = false; }   // a new race: look along the grid
        // in the air the camera rises slowly, so you see the car soar (it catches up after landing)
        v.camY += (c.y - v.camY) * std::min(1.0f, (c.air > 0 ? 0.04f : 0.15f) * step);
        v.camHeading += (c.heading - v.camHeading) * (0.10f * step);
        if (v.shake > 0.0f) v.shake = std::max(0.0f, v.shake - 0.05f * step);
        float targetFov = 64.0f + 12.0f * std::clamp(c.speed / sim::MAX_SPEED, 0.0f, 1.5f) + (c.nitroOn ? 10.0f : 0.0f);
        v.fov += (targetFov - v.fov) * (0.08f * step);
        float targetDist = 6.6f + 1.8f * std::clamp(c.speed / sim::MAX_SPEED, 0.0f, 1.5f);
        v.camDist += (targetDist - v.camDist) * (0.05f * step);
    };

    if (int(cars.size()) > localCar()) updateCarView(view1, cars[localCar()]);
    if (cars.size() > 1 && !isOnline()) updateCarView(view2, cars[1]);

    updateParticles(realDt);

    if (audio && int(cars.size()) > localCar()) {
        const sim::Car &me = cars[localCar()];
        const sim::Car *loud = &me;                      // (split screen: the faster player's engine)
        if (cars.size() > 1 && !isOnline() && std::fabs(cars[1].speed) > std::fabs(me.speed)) loud = &cars[1];
        const bool p2 = loud != &me;
        const bool gas = p2 ? isDown(Qt::Key_W) : (isDown(Qt::Key_Up) || (isOnline() && isDown(Qt::Key_W)));
        const bool brake = p2 ? isDown(Qt::Key_S) : (isDown(Qt::Key_Down) || (isOnline() && isDown(Qt::Key_S)));
        const bool driving = loud->wreck == 0 && simEngine.getState() == State::Racing;
        audio->drive(driving ? std::fabs(loud->speed) / sim::MAX_SPEED : 0.0f, driving && (gas || autoDrive), driving && brake,
                     driving && loud->air > 0, driving && loud->nitroOn);
    }

    update();
}

// ===========================================================================
//  INPUT
// ===========================================================================
namespace {
inline void computeMenuMetrics(float W, float H, float dpr,
                               float &btnX, float &btnW, float &btnH, float &btnGap, float &listTop, float &ps)
{
    float panelX = 28.0f * dpr;
    float panelW = std::clamp(320.0f * dpr, 280.0f, W * 0.30f);
    float panelY0 = 24.0f * dpr;
    float panelY1 = H - 86.0f * dpr;

    float headerH = 48.0f * dpr;
    listTop = panelY1 - headerH - 8.0f * dpr;

    float btnPadX = 14.0f * dpr;
    btnX = panelX + btnPadX;
    btnW = panelW - 2.0f * btnPadX;

    float availH = listTop - panelY0 - 12.0f * dpr;
    const float N = float(NUM_MENU_ITEMS);
    btnH = std::clamp(availH / (N + 0.9f), 32.0f * dpr, 46.0f * dpr);
    btnGap = std::clamp((availH - N * btnH) / (N - 1.0f), 5.0f * dpr, 10.0f * dpr);
    ps = std::clamp(H / 420.0f, 1.30f, 1.75f);
}
} // namespace

void GLWidget::keyPressEvent(QKeyEvent *event)
{
    if (simEngine.getState() == State::Menu && handleAccountKey(event)) return;   // (typing a name or password)
    if (event->isAutoRepeat()) return;
    int k = event->key();

    if (k == Qt::Key_F2) { meterOn = !meterOn; update(); return; }
    if (netScreen != NetScreen::Off && k != Qt::Key_F12 && handleOnlineKey(event)) return;
    if ((story != StoryScreen::Off || (isOnline() && netStoryHold)) && k != Qt::Key_F12 && handleStoryKey(event)) return;

    // Universal restart: Pressing 'R' restarts race immediately from any gameplay state
    if (k == Qt::Key_R && !(simEngine.getState() == State::Menu && profileOpen)) {   // (MY PROFILE uses R)
        startRace(simEngine.getGameMode());
        return;
    }

    // Screenshot: F12 saves the current frame to screenshots/ next to the executable
    if (k == Qt::Key_F12) {
        QString dir = QCoreApplication::applicationDirPath() + "/screenshots";
        saveScreenshot(dir + "/shot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png");
        return;
    }

    // Determinism test: Pressing 'F9' logs current sim tick and state checksum
    if (k == Qt::Key_F9) {
        qDebug() << "[GameSim Checksum] Tick:" << simEngine.getTick()
                 << "Hash:" << QString::asprintf("0x%016llX", (unsigned long long)simEngine.checksum());
        return;
    }

    if (k == Qt::Key_F3 && simEngine.getState() != State::Menu) { showAiDebug = !showAiDebug; update(); return; }
    if (k == Qt::Key_F4 && simEngine.getState() != State::Menu) { showCpuMinds = !showCpuMinds; update(); return; }
    if (simEngine.getState() == State::Menu && profileOpen && k == Qt::Key_R) {   // MY PROFILE: R twice deletes your clone
        if (cloneResetArmed) { cloneReset(); cloneResetArmed = false; } else cloneResetArmed = true;
        update();
        return;
    }
    if (simEngine.getState() == State::Menu && boardOpen) {                       // LEADERBOARD: left / right a map
        if (k == Qt::Key_Left || k == Qt::Key_A) openLeaderboard(boardMap - 1);
        else if (k == Qt::Key_Right || k == Qt::Key_D) openLeaderboard(boardMap + 1);
        else boardOpen = false;
        update();
        return;
    }
    if (simEngine.getState() == State::Menu && profileOpen && k == Qt::Key_S && !accountUser.isEmpty()) {   // MY PROFILE: share races
        accountSetShare(!accountShare);
        update();
        return;
    }
    if (simEngine.getState() == State::Menu && profileOpen && k == Qt::Key_L) {   // MY PROFILE: log in or out
        if (accountUser.isEmpty()) openAccountScreen(false);
        else { accountLogout(); shownProfile = loadProfile(profileName(0)); }
        update();
        return;
    }
    if (simEngine.getState() == State::Menu && profileOpen) { profileOpen = false; cloneResetArmed = false; update(); return; }
    if (simEngine.getState() == State::Menu) {
        const int MENU_COUNT = NUM_MENU_ITEMS;
        if (k == Qt::Key_Up || k == Qt::Key_W) {
            menuSelection = (menuSelection + MENU_COUNT - 1) % MENU_COUNT;
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_Down || k == Qt::Key_S) {
            menuSelection = (menuSelection + 1) % MENU_COUNT;
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_1) {
            menuSelection = M_VSCPU;
            startRace(sim::GameSim::ModeSingle);
            return;
        } else if (k == Qt::Key_2) {
            menuSelection = M_SPLIT;
            startRace(sim::GameSim::ModeSplitScreen);
            return;
        } else if (k >= Qt::Key_3 && k <= Qt::Key_8) {
            menuSelection = k - Qt::Key_3 + M_ONLINE;
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_M) {
            toggleSound();
            update();
            return;
        } else if (k == Qt::Key_Plus || k == Qt::Key_Equal || k == Qt::Key_Minus || k == Qt::Key_Underscore) {
            const int dir = (k == Qt::Key_Minus || k == Qt::Key_Underscore) ? -1 : 1;
            cpuCount = std::clamp(cpuCount + dir, 1, sim::MAX_RACERS - 1);    // [+]/[-]: CPU racers
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_A || k == Qt::Key_D) {
            int dir = (k == Qt::Key_Left || k == Qt::Key_A) ? -1 : 1;
            if (cycleMenuOption(menuSelection, dir) && audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (menuSelection == M_VSCPU) {
                beginRaceWithStory(sim::GameSim::ModeSingle);
            } else if (menuSelection == M_SPLIT) {
                beginRaceWithStory(sim::GameSim::ModeSplitScreen);
            } else if (menuSelection == M_ONLINE) {
                openOnline();
            } else if (menuSelection == M_PROFILE) {
                shownProfile = loadProfile(profileName(0));
                profileOpen = true;
            } else if (menuSelection == M_BOARD) {
                openLeaderboard(selectedMapPreset);
            } else if (menuSelection == M_ACCOUNT) {
                menuAccount();
            } else {
                cycleMenuOption(menuSelection, 1);
            }
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        }
    } else if (simEngine.getState() == State::Paused) {
        if (k == Qt::Key_Up || k == Qt::Key_W)   { pauseChoice = (pauseChoice + 2) % 3; return; }
        if (k == Qt::Key_Down || k == Qt::Key_S) { pauseChoice = (pauseChoice + 1) % 3; return; }
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            if (pauseChoice == 1) { startRace(simEngine.getGameMode()); return; }
            k = pauseChoice == 0 ? Qt::Key_P : Qt::Key_M;   // (resume / main menu below)
        }
        if (k == Qt::Key_Escape || k == Qt::Key_P) {
            simEngine.setState(State::Racing);
            elapsedTimer.restart();
            return;
        } else if (k == Qt::Key_M) {
            simEngine.setState(State::Menu);
            activeModal = ModalNone;
            return;
        }
    } else if (simEngine.getState() == State::Finished) {
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            startRace(simEngine.getGameMode());
            return;
        } else if (k == Qt::Key_M || k == Qt::Key_Escape) {
            simEngine.setState(State::Menu);
            activeModal = ModalNone;
            return;
        }
    } else if (simEngine.getState() == State::Racing) {
        if (k == Qt::Key_Escape || k == Qt::Key_P) {
            simEngine.setState(State::Paused);
            pauseChoice = 0;
            return;
        } else if (k == Qt::Key_M) {
            toggleSound();
            return;
        }
    }

    keys.insert(k);
}

void GLWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    keys.remove(event->key());
}

void GLWidget::focusOutEvent(QFocusEvent *event)
{
    keys.clear();
    GLBase::focusOutEvent(event);
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (simEngine.getState() == State::Menu && activeModal == ModalNone && netScreen == NetScreen::Off) {
        float dpr = pixelRatio();
        float mx = float(event->position().x()) * dpr;
        float my = logicalHeight() * dpr - float(event->position().y()) * dpr;
        float W = float(width()) * dpr, H = float(height()) * dpr;
        float btnX, btnW, btnH, btnGap, listTop, ps;
        computeMenuMetrics(W, H, dpr, btnX, btnW, btnH, btnGap, listTop, ps);

        for (int i = 0; i < NUM_MENU_ITEMS; ++i) {
            float by0 = listTop - (i + 1) * btnH - i * btnGap;
            float by1 = by0 + btnH;
            float bx0 = btnX;
            float bx1 = btnX + btnW;

            if (mx >= bx0 && mx <= bx1 && my >= by0 && my <= by1) {
                if (menuSelection != i) {
                    menuSelection = i;
                    if (audio) audio->playCountdownBeep(false);
                    update();
                }
                break;
            }
        }
    }
    GLBase::mouseMoveEvent(event);
}

void GLWidget::mousePressEvent(QMouseEvent *event)
{
#ifdef __EMSCRIPTEN__
    requestActivate();
#else
    setFocus();
#endif
    if (netScreen != NetScreen::Off) return;     // online screens are keyboard-driven
    if (simEngine.getState() == State::Menu && accountScreen != AccountScreen::Off) {
        const float d = pixelRatio();
        accountClick(float(event->position().x()) * d, (logicalHeight() - float(event->position().y())) * d);
        return;
    }
    if (simEngine.getState() == State::Menu && profileOpen) { profileOpen = false; update(); return; }
    if (simEngine.getState() == State::Menu && boardOpen) { boardOpen = false; update(); return; }
    if (simEngine.getState() == State::Menu) {
        if (activeModal != ModalNone) {
            activeModal = ModalNone;
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        }

        float dpr = pixelRatio();
        float mx = float(event->position().x()) * dpr;
        float my = logicalHeight() * dpr - float(event->position().y()) * dpr;
        float W = float(width()) * dpr, H = float(height()) * dpr;
        float btnX, btnW, btnH, btnGap, listTop, ps;
        computeMenuMetrics(W, H, dpr, btnX, btnW, btnH, btnGap, listTop, ps);

        for (int i = 0; i < NUM_MENU_ITEMS; ++i) {
            float by0 = listTop - (i + 1) * btnH - i * btnGap;
            float by1 = by0 + btnH;
            float bx0 = btnX;
            float bx1 = btnX + btnW;

            if (mx >= bx0 && mx <= bx1 && my >= by0 && my <= by1) {
                menuSelection = i;
                if (i == M_VSCPU) {
                    beginRaceWithStory(sim::GameSim::ModeSingle);
                } else if (i == M_SPLIT) {
                    beginRaceWithStory(sim::GameSim::ModeSplitScreen);
                } else if (i == M_ONLINE) {
                    openOnline();
                } else if (i == M_PROFILE) {
                    shownProfile = loadProfile(profileName(0));
                    profileOpen = true;
                } else if (i == M_BOARD) {
                    openLeaderboard(selectedMapPreset);
                } else if (i == M_ACCOUNT) {
                    menuAccount();
                } else {
                    cycleMenuOption(i, 1);
                }
                if (audio) audio->playCountdownBeep(false);
                update();
                return;
            }
        }
    } else if (simEngine.getState() == State::Finished) {
        startRace(simEngine.getGameMode());
    }
    GLBase::mousePressEvent(event);
}


// ===========================================================================
//  OPENGL SETUP + PROCEDURAL PIXEL TEXTURES
// ===========================================================================
void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    sgl::init();                                         // (the OpenGL 1 drawing on OpenGL ES 2 / WebGL)

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    glEnable(GL_LIGHT0);
    GLfloat amb[] = {0.42f, 0.40f, 0.48f, 1.0f};
    GLfloat dif[] = {0.95f, 0.88f, 0.78f, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    GLfloat gamb[] = {0.12f, 0.12f, 0.14f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gamb);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 110.0f);
    glFogf(GL_FOG_END, VIEW_DIST);
    glFogfv(GL_FOG_COLOR, FOG_COL);
    glHint(GL_FOG_HINT, GL_NICEST);

#ifdef GL_MULTISAMPLE
    glEnable(GL_MULTISAMPLE);
#endif

    quad = gluNewQuadric();
    gluQuadricNormals(quad, GLU_SMOOTH);

    buildTextures();

    // Sculpted car body (display lists) + reflection map for glossy paint and glass
    carmodel::Lists lists = carmodel::build();
    carBodyList = lists.body;
    carGlassList = lists.glass;
    carTrimList = lists.trim;
    ensureEnvMap(0);
}

// Rebuild the reflection map when the sky changes (different map, or back to the garage)
void GLWidget::ensureEnvMap(int theme)
{
    if (theme == envTheme && texEnv) return;
    const ThemeLook &t = THEMES[theme];
    carmodel::EnvColors e;
    for (int k = 0; k < 3; ++k) {
        e.horizon[k] = t.skyHor[k]; e.mid[k] = t.skyMid[k]; e.top[k] = t.skyTop[k];
        e.ground[k] = t.envGround[k]; e.sun[k] = t.sunCore[k] * (t.night ? 0.6f : 1.0f);
    }
    e.sunHeight = t.sunHeight;
    if (texEnv) glDeleteTextures(1, &texEnv);
    texEnv = carmodel::buildEnvMap(e);
    envTheme = theme;
}

GLuint GLWidget::makeTexture(int w, int h, const std::vector<unsigned char> &rgb)
{
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // Smooth (trilinear) filtering instead of blocky nearest-pixel sampling
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // Anisotropic filtering keeps the road sharp at grazing angles (if the GPU supports it)
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (ext && std::strstr(ext, "GL_EXT_texture_filter_anisotropic")) {
        const GLenum MAX_ANISOTROPY = 0x84FF, TEX_ANISOTROPY = 0x84FE;
        GLfloat maxAniso = 1.0f;
        glGetFloatv(MAX_ANISOTROPY, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D, TEX_ANISOTROPY, std::min(8.0f, maxAniso));
    }
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return id;
}

void GLWidget::buildTextures()
{
    auto put = [](std::vector<unsigned char> &img, int w, int x, int y, float r, float g, float b) {
        int i = (y * w + x) * 3;
        img[i]     = (unsigned char)std::clamp(int(r), 0, 255);
        img[i + 1] = (unsigned char)std::clamp(int(g), 0, 255);
        img[i + 2] = (unsigned char)std::clamp(int(b), 0, 255);
    };

    // grass: mottled natural greens with drier yellowish patches and the odd flower
    {
        const int N = 128; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float u = float(x) / N, v = float(y) / N;
            float big = fbm(u, v, 2, 4, 40), mid = fbm(u, v, 8, 3, 41), blade = hash2(x, y, 42);
            float dry = std::clamp((fbm(u, v, 3, 3, 43) - 0.55f) * 4.0f, 0.0f, 1.0f);
            float r = 58 + big * 28 + blade * 14 + dry * 40;
            float g = 92 + big * 34 + mid * 20 + blade * 18 + dry * 12;
            float b = 38 + big * 12 + blade * 8 + dry * 6;
            float f = hash2(x, y, 44);
            if (f > 0.9975f)      { r = 235; g = 225; b = 150; }
            else if (f > 0.9955f) { r = 225; g = 170; b = 200; }
            put(img, N, x, y, r, g, b);
        }
        texGrass = makeTexture(N, N, img);
    }
    // asphalt: worn patches + fine grain + light aggregate stones and dark pits
    {
        const int N = 256; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float u = float(x) / N, v = float(y) / N;
            float g = 62 + fbm(u, v, 4, 5, 30) * 34 + (hash2(x, y, 31) - 0.5f) * 26;
            float st = hash2(x, y, 32);
            if (st > 0.985f)      g += 38;
            else if (st < 0.015f) g -= 22;
            put(img, N, x, y, g * 0.98f, g, g * 1.04f);
        }
        texAsphalt = makeTexture(N, N, img);
    }
    // stone
    {
        const int N = 32; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            int row = y / 8, off = (row % 2) * 8, bx = (x + off) / 16;
            float r, g, b;
            if (y % 8 == 0 || (x + off) % 16 == 0) { r = 70; g = 72; b = 60; }
            else {
                float base = 150 + hash2(bx, row, 5) * 35, nz = hash2(x, y, 6) * 18;
                r = base + nz - 10; g = base + nz; b = base + nz - 25;
                if (hash2(x, y, 7) < 0.12f && (y % 8) > 5) { r = 95; g = 135; b = 70; }
            }
            put(img, N, x, y, r, g, b);
        }
        texStone = makeTexture(N, N, img);
    }
    // plaster
    {
        const int N = 32; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float v = 228 + hash2(x, y, 8) * 27;
            if (hash2(x, y, 9) < 0.03f) v -= 40;
            put(img, N, x, y, v, v - 4, v - 12);
        }
        texPlaster = makeTexture(N, N, img);
    }
    // roof
    {
        const int N = 32; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            int row = y / 8, ty = y % 8;
            float s = 255 - ty * 12 + (hash2(x, y, 10) - 0.5f) * 20;
            if ((x + (row % 2) * 4) % 8 == 0) s -= 60;
            if (ty == 0) s = 120;
            put(img, N, x, y, s, s, s);
        }
        texRoof = makeTexture(N, N, img);
    }
    // sand: warm dunes with wind ripples
    {
        const int N = 128; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float u = float(x) / N, v = float(y) / N;
            float big = fbm(u, v, 2, 4, 50), grain = hash2(x, y, 51);
            float ripple = 0.5f + 0.5f * std::sin((v * 14.0f + fbm(u, v, 4, 2, 52) * 3.0f) * 2.0f * PI);
            float k = 0.82f + big * 0.22f + ripple * 0.06f + (grain - 0.5f) * 0.08f;
            put(img, N, x, y, 214 * k, 178 * k, 128 * k);
        }
        texSand = makeTexture(N, N, img);
    }
    // snow: bright, slightly blue in the hollows, with a little sparkle
    {
        const int N = 128; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float u = float(x) / N, v = float(y) / N;
            float k = 0.86f + fbm(u, v, 3, 4, 60) * 0.14f;
            float r = 232 * k, g = 238 * k, b = 250 * k;
            if (hash2(x, y, 61) > 0.996f) { r = 255; g = 255; b = 255; }
            put(img, N, x, y, r, g, b);
        }
        texSnow = makeTexture(N, N, img);
    }
    // city pavement: concrete slabs with joints and stains
    {
        const int N = 128; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float u = float(x) / N, v = float(y) / N;
            float g = 74 + fbm(u, v, 4, 4, 70) * 26 + (hash2(x, y, 71) - 0.5f) * 10;
            if (x % 32 == 0 || y % 32 == 0) g *= 0.65f;
            put(img, N, x, y, g, g, g * 1.04f);
        }
        texPavement = makeTexture(N, N, img);
    }
    // soft round sprite: white, alpha fading smoothly from the centre to the edge
    {
        const int N = 64; std::vector<unsigned char> img(N * N * 4);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float dx = (x + 0.5f) / N * 2.0f - 1.0f, dy = (y + 0.5f) / N * 2.0f - 1.0f;
            float a = std::max(0.0f, 1.0f - (dx * dx + dy * dy));
            int i = (y * N + x) * 4;
            img[i] = img[i + 1] = img[i + 2] = 255;
            img[i + 3] = (unsigned char)(a * a * 255.0f);
        }
        glGenTextures(1, &texSoft);
        glBindTexture(GL_TEXTURE_2D, texSoft);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, N, N, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    }
}

void GLWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w); Q_UNUSED(h);
}

// ===========================================================================
//  FRAME RENDERING
// ===========================================================================
void GLWidget::paintGL()
{
    paintFrame();
    if (meterOn || netShaky()) drawMeter(frameW, frameH, pixelRatio());
    sgl::endFrame();                                     // (what is still waiting in the last batch)
    static const bool glStats = qEnvironmentVariableIsSet("CAR_RACING_GLSTATS");   // draws and vertices a frame
    if (glStats) {
        static int frames = 0;
        static long draws = 0, verts = 0, streamed = 0;
        const sgl::Stats st = sgl::frameStats();
        draws += st.draws; verts += st.vertices; streamed += st.streamed;
        if (++frames == 240) {
            std::printf("GLSTATS: %ld draws, %ld vertices (%ld uploaded) a frame\n", draws / frames, verts / frames, streamed / frames);
            std::fflush(stdout);
            frames = 0; draws = verts = streamed = 0;
        }
    }
}

// The pixel ratio, and the window's height in its own (logical) pixels. In the browser both come
// from the page: after a zoom or a move to another screen Qt's can be stale, and the menus would
// be drawn too big.
float GLWidget::pixelRatio() const
{
#ifdef __EMSCRIPTEN__
    return float(emscripten_get_device_pixel_ratio()) * gfxScale;
#else
    return float(devicePixelRatio());
#endif
}

float GLWidget::logicalHeight() const
{
#ifdef __EMSCRIPTEN__
    if (frameH > 0) return float(frameH) / pixelRatio();    // (the last frame drawn)
#endif
    return float(height());
}

void GLWidget::paintFrame()
{
    QElapsedTimer paintClock;                                   // scripted online test: drawing time
    paintClock.start();
    struct PaintStat {
        GLWidget *w; QElapsedTimer &c;
        ~PaintStat() {
            if (w->netTest.isEmpty() || !w->isOnline()) return;
            double ms = c.nsecsElapsed() * 1e-6;
            w->statPaintMs += ms;
            w->statPaintMax = std::max(w->statPaintMax, ms);
        }
    } paintStat{this, paintClock};
#ifdef __EMSCRIPTEN__
    {   // automatic graphics: while driving the canvas may hold fewer pixels (menus always have them all)
        const bool driving = story == StoryScreen::Scene
                             || (simEngine.getState() != State::Menu && (netScreen == NetScreen::Off || netScreen == NetScreen::Race));
        gfxScale = driving ? GFX_SCALE[gfxLevel] : 1.0f;
        EM_ASM({
            let c = window.tppCanvas;
            if (!c || !c.isConnected) {
                const host = document.querySelector("#screen > div");
                c = window.tppCanvas = host && host.shadowRoot ? host.shadowRoot.querySelector("canvas") : null;
            }
            if (!c || !c.clientWidth) return;
            if ($0 < 1 || window.tppScaled) {
                const w = Math.round(c.clientWidth * devicePixelRatio * $0); const h = Math.round(c.clientHeight * devicePixelRatio * $0);
                if (c.width != w || c.height != h) { c.width = w; c.height = h; }
                window.tppScaled = $0 < 1;
            }
        }, double(gfxScale));
    }
#endif
    const float dpr = pixelRatio();
    int W = int(width() * dpr), H = int(height() * dpr);
#ifdef __EMSCRIPTEN__
    {   // the browser: draw at the canvas's real size (Qt's window size can be stale, see pixelRatio)
        int bw = 0, bh = 0;
        emscripten_webgl_get_drawing_buffer_size(emscripten_webgl_get_current_context(), &bw, &bh);
        if (bw > 0 && bh > 0) { W = bw; H = bh; }
    }
#endif
    frameW = W; frameH = H;

    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto &cars = simEngine.getCars();
    static sim::Car dummyCar;
    const sim::Car &c1 = (cars.size() > 0) ? cars[0] : dummyCar;
    const sim::Car &c2 = (cars.size() > 1) ? cars[1] : dummyCar;

    if (netScreen != NetScreen::Off && !isOnline()) {         // online menu / lobby over the garage
        drawShowcaseScene(W, H);
        glViewport(0, 0, W, H);
        drawOnlineScreen(W, H, dpr);
        return;
    }

    if (story == StoryScreen::Chapters) {                     // story mode: the chapter list
        drawShowcaseScene(W, H);
        glViewport(0, 0, W, H);
        drawStoryChapters(W, H, dpr);
        if (storyRules == 1) drawStoryRules(W, H, dpr, storyPick);
        return;
    }
    if (story == StoryScreen::Scene) {                        // story mode: a scene at the grid
        renderView(c1, view1, c2, view2, 1, 0, 0, W, H);
        glViewport(0, 0, W, H);
        drawStoryOverlay(W, H, dpr);
        return;
    }

    if (simEngine.getState() == State::Menu) {
        drawShowcaseScene(W, H);
        glViewport(0, 0, W, H);
        drawMainMenu(W, H, dpr);
        if (profileOpen) drawProfileScreen(W, H, dpr);
        if (boardOpen) drawLeaderboard(W, H, dpr);
        if (accountScreen != AccountScreen::Off) drawAccountScreen(W, H, dpr);
        return;
    }

    if (isOnline()) {
        if (mySlot >= 0 && mySlot < int(cars.size())) {
            renderView(cars[mySlot], view1, cars[mySlot], view2, mySlot + 1, 0, 0, W, H);
            glViewport(0, 0, W, H);
            drawMinimap(W, H, dpr);
        }
        if (trackMismatch && (simEngine.getTick() / 30) % 2 == 0) {
            begin2D(W, H);
            const char *warn = "TRACK DIFFERS FROM THE HOST - BOTH PLAYERS NEED THE SAME GAME VERSION";
            float ps = 1.6f * dpr;
            drawText(warn, (W - textWidth(warn, ps)) * 0.5f, H * 0.80f, ps, 1.0f, 0.3f, 0.3f);
        }
        if (simEngine.getState() == State::Finished) {
            glViewport(0, 0, W, H);
            drawOnlineResults(W, H, dpr);
        }
        return;
    }

    if (simEngine.getGameMode() == sim::GameSim::ModeSingle) {
        renderView(c1, view1, c2, view2, 1, 0, 0, W, H);
        glViewport(0, 0, W, H);
        drawMinimap(W, H, dpr);
        if (story == StoryScreen::Race && simEngine.getState() != State::Finished) drawStoryRaceInfo(W, H, dpr);
        if (isOnline() && netStoryHold && story == StoryScreen::Off) drawNetStoryWait(W, H, dpr);
    } else {
        const int half = W / 2;
        renderView(c2, view2, c1, view1, 2, 0, 0, half, H);
        renderView(c1, view1, c2, view2, 1, half, 0, W - half, H);

        glViewport(0, 0, W, H);
        begin2D(W, H);
        glColor4f(0.05f, 0.05f, 0.08f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(half - 3 * dpr, 0); glVertex2f(half + 3 * dpr, 0);
        glVertex2f(half + 3 * dpr, H); glVertex2f(half - 3 * dpr, H);
        glEnd();
        drawMinimap(W, H, dpr);
    }
    if (showAiDebug && !hideHud) drawAiDebug(W, H, dpr);
    if (showCpuMinds && !hideHud) drawCpuMinds(W, H, dpr);

    if (simEngine.getState() == State::Paused) {
        glViewport(0, 0, W, H);
        drawPauseOverlay(W, H, dpr);
    } else if (simEngine.getState() == State::Finished) {
        glViewport(0, 0, W, H);
        drawFinishOverlay(W, H, dpr);
        drawRivalLine(W, H, dpr);
        if (story == StoryScreen::Race) drawStoryRaceInfo(W, H, dpr);   // goal met or not, and how to go on
    }
}

void GLWidget::begin2D(int w, int h)
{
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
}

void GLWidget::drawShowcaseScene(int W, int H)
{
    glViewport(0, 0, W, H);
    ensureEnvMap(0);   // the garage car reflects the default warm sky

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(46.0, double(W) / double(std::max(H, 1)), 0.5, 400.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    // Turntable center in showroom
    float focusX = 1.35f, focusZ = -2.2f;

    // Dramatic sporty low eye-level camera framing the car center-right
    float t = float(simEngine.getTick());
    float camPan = std::sin(t * 0.005f) * 0.25f;
    float ex = focusX - 3.8f + camPan;
    float ey = 1.48f + std::cos(t * 0.007f) * 0.06f;
    float ez = focusZ + 6.6f;
    gluLookAt(ex, ey, ez, focusX - 0.2f, 0.72f, focusZ, 0, 1, 0);

    // Dark moody garage lighting
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    GLfloat lp[] = {focusX + 2.5f, 6.5f, focusZ + 2.5f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lp);

    GLfloat lDiff[] = {0.85f, 0.90f, 0.95f, 1.0f};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lDiff);

    GLfloat lAmb[] = {0.20f, 0.24f, 0.32f, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, lAmb);

    // The car turns slowly on its turntable (one turn in about half a minute)
    const float carHeading = -24.0f * PI / 180.0f + t * 0.0035f;
    const float carR = CAR_PRESETS[selectedCarPreset].r, carG = CAR_PRESETS[selectedCarPreset].g, carB = CAR_PRESETS[selectedCarPreset].b;
    const float platR = 3.6f, platH = 0.08f;

    // 0. Its reflection in the glossy floor: the car drawn upside down under the turntable;
    // the floor and the turntable go over it, partly see-through
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_LIGHTING);                              // (first a solid dark floor under it all, not kept in depth)
    glDepthMask(GL_FALSE);
    glColor3f(0.03f, 0.035f, 0.05f);
    glBegin(GL_QUADS);
    glVertex3f(-35.0f, 0.0f, -25.0f); glVertex3f(35.0f, 0.0f, -25.0f);
    glVertex3f(35.0f, 0.0f, 25.0f);   glVertex3f(-35.0f, 0.0f, 25.0f);
    glEnd();
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(0.0f, 2.0f * platH, 0.0f);
    glScalef(1.0f, -1.0f, 1.0f);
    glFrontFace(GL_CW);
    drawCar(focusX, focusZ, carHeading, 0.0f, 0.0f, 0.12f, 0.0f, carR, carG, carB, false, "");
    glFrontFace(GL_CCW);
    glPopMatrix();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // =======================================================================
    // 1. INDOOR GARAGE ARCHITECTURE
    // =======================================================================
    // A. Dark Polished Showroom Floor (Reflective epoxy tile look)
    glDisable(GL_LIGHTING);
    glColor4f(0.045f, 0.055f, 0.075f, 0.68f);           // (the reflection shows through)
    glBegin(GL_QUADS);
    glVertex3f(-35.0f, 0.0f, -25.0f);
    glVertex3f( 35.0f, 0.0f, -25.0f);
    glVertex3f( 35.0f, 0.0f,  25.0f);
    glVertex3f(-35.0f, 0.0f,  25.0f);
    glEnd();

    // Subtle dark glossy floor grid lines
    glColor4f(0.09f, 0.12f, 0.18f, 0.50f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (float gx = -30.0f; gx <= 30.0f; gx += 3.5f) {
        glVertex3f(gx, 0.005f, -25.0f); glVertex3f(gx, 0.005f, 25.0f);
    }
    for (float gz = -25.0f; gz <= 25.0f; gz += 3.5f) {
        glVertex3f(-30.0f, 0.005f, gz); glVertex3f(30.0f, 0.005f, gz);
    }
    glEnd();

    glEnable(GL_LIGHTING);

    // B. Garage Back Wall (Dark slate industrial wall)
    float wallZ = -13.5f;
    glColor3f(0.07f, 0.085f, 0.11f);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glVertex3f(-32.0f, 0.0f, wallZ);
    glVertex3f( 32.0f, 0.0f, wallZ);
    glVertex3f( 32.0f, 10.5f, wallZ);
    glVertex3f(-32.0f, 10.5f, wallZ);
    glEnd();

    // Structural Steel Columns
    glColor3f(0.11f, 0.13f, 0.17f);
    for (float colX : {-12.0f, -5.0f, 2.0f, 9.0f, 16.0f}) {
        boxAt(colX, 5.0f, wallZ + 0.35f, 0.45f, 5.0f, 0.35f);
    }

    // Horizontal Steel Support Beams
    glColor3f(0.13f, 0.15f, 0.20f);
    boxAt(0.0f, 5.2f, wallZ + 0.25f, 32.0f, 0.15f, 0.25f);
    boxAt(0.0f, 9.5f, wallZ + 0.25f, 32.0f, 0.20f, 0.25f);

    // C. 2nd-Floor Mezzanine Catwalk / Industrial Walkway
    float catY = 5.2f;
    float catZ = wallZ + 1.2f;
    glColor3f(0.09f, 0.11f, 0.14f);
    boxAt(0.0f, catY, catZ, 32.0f, 0.12f, 1.2f);

    // Catwalk Railing (horizontal bars & vertical posts)
    glColor3f(0.18f, 0.21f, 0.26f);
    boxAt(0.0f, catY + 1.25f, catZ + 1.15f, 32.0f, 0.035f, 0.035f);
    boxAt(0.0f, catY + 0.65f, catZ + 1.15f, 32.0f, 0.025f, 0.025f);
    for (float rx = -28.0f; rx <= 28.0f; rx += 3.2f) {
        boxAt(rx, catY + 0.65f, catZ + 1.15f, 0.04f, 0.65f, 0.04f);
    }

    // D. Garage Roller Shutter Door (Right Side)
    float doorX = 8.5f, doorW = 4.2f, doorH = 2.4f;
    glColor3f(0.12f, 0.14f, 0.18f);
    boxAt(doorX, doorH, wallZ + 0.10f, doorW, doorH, 0.05f);
    glDisable(GL_LIGHTING);
    glColor3f(0.06f, 0.07f, 0.09f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (float sy = 0.4f; sy < doorH * 2.0f; sy += 0.35f) {
        glVertex3f(doorX - doorW + 0.1f, sy, wallZ + 0.16f);
        glVertex3f(doorX + doorW - 0.1f, sy, wallZ + 0.16f);
    }
    glEnd();
    glLineWidth(1.0f);

    // E. Overhead Ceiling LED Tube Fixtures (Glowing Cyan & White Strips)
    float pulse = 0.85f + 0.15f * std::sin(t * 0.12f);
    glColor4f(0.10f, 0.90f, 1.0f, 0.95f * pulse);
    boxAt(2.5f, catY + 2.8f, catZ - 0.2f, 2.8f, 0.05f, 0.05f);
    boxAt(-7.5f, catY + 2.8f, catZ - 0.2f, 2.8f, 0.05f, 0.05f);

    glColor4f(0.85f, 0.95f, 1.0f, 0.90f);
    boxAt(focusX + 1.0f, 7.8f, focusZ + 1.5f, 3.5f, 0.06f, 0.08f);

    // F. Glowing Neon Wall Signs on Garage Wall
    // 1. "NEON DRIFT" Cyan Neon Sign (matching Image 2)
    glPushMatrix();
    glTranslatef(1.2f, catY + 1.6f, wallZ + 0.15f);
    glColor4f(0.0f, 0.92f, 1.0f, 0.95f * pulse);
    drawText3D("NEON", -1.8f, 0.45f, 0.05f, 0.14f);
    drawText3D("DRIFT", -2.1f, -0.45f, 0.05f, 0.14f);
    glLineWidth(2.0f);
    glColor4f(0.0f, 0.85f, 1.0f, 0.75f * pulse);
    glBegin(GL_LINE_LOOP);
    glVertex3f(-2.5f, -0.9f, 0.04f); glVertex3f(2.2f, -0.9f, 0.04f);
    glVertex3f( 2.2f,  1.1f, 0.04f); glVertex3f(-2.5f,  1.1f, 0.04f);
    glEnd();
    glLineWidth(1.0f);
    glPopMatrix();

    // 2. "GARAGE 01" Sign on Wall
    glPushMatrix();
    glTranslatef(doorX, catY + 1.2f, wallZ + 0.15f);
    glColor4f(1.0f, 0.55f, 0.10f, 0.90f);
    drawText3D("GARAGE", -1.4f, 0.35f, 0.04f, 0.10f);
    drawText3D("01", -0.5f, -0.35f, 0.04f, 0.14f);
    glPopMatrix();

    // =======================================================================
    // 2. CIRCULAR SHOWROOM TURNTABLE PEDESTAL
    // =======================================================================
    // Dark metallic disc surface (polished: the reflection shows through), turning with the car
    glColor4f(0.10f, 0.12f, 0.16f, 0.66f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(focusX, platH, focusZ);
    for (int i = 0; i <= 36; ++i) {
        float a = i * 2.0f * PI / 36.0f;
        glVertex3f(focusX + std::cos(a) * platR, platH, focusZ + std::sin(a) * platR);
    }
    glEnd();
    glDisable(GL_LIGHTING);
    glColor4f(0.30f, 0.36f, 0.45f, 0.35f);               // its seams, turning with it
    glBegin(GL_LINES);
    for (int i = 0; i < 12; ++i) {
        const float a = carHeading + float(i) * PI / 6.0f;
        glVertex3f(focusX + std::cos(a) * 0.6f, platH + 0.006f, focusZ + std::sin(a) * 0.6f);
        glVertex3f(focusX + std::cos(a) * (platR - 0.3f), platH + 0.006f, focusZ + std::sin(a) * (platR - 0.3f));
    }
    glEnd();
    glEnable(GL_LIGHTING);

    // Metallic side rim
    glColor3f(0.06f, 0.07f, 0.10f);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 36; ++i) {
        float a = i * 2.0f * PI / 36.0f;
        float rx = std::cos(a) * platR, rz = std::sin(a) * platR;
        glVertex3f(focusX + rx, 0.005f, focusZ + rz);
        glVertex3f(focusX + rx, platH,  focusZ + rz);
    }
    glEnd();

    // Dual-Color Neon Glowing Ring (Cyan on Left, Amber/Orange on Right, exactly like Image 2!)
    glLineWidth(3.0f);
    glDepthMask(GL_FALSE);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 36; ++i) {
        float a = i * 2.0f * PI / 36.0f;
        float cosA = std::cos(a);
        if (cosA < 0.0f) {
            glColor4f(0.05f, 0.92f, 1.0f, 0.95f * pulse);
        } else {
            glColor4f(1.0f, 0.52f, 0.05f, 0.95f * pulse);
        }
        glVertex3f(focusX + std::cos(a) * (platR - 0.15f), platH + 0.01f, focusZ + std::sin(a) * (platR - 0.15f));
    }
    glEnd();
    glLineWidth(1.0f);

    // Floor Neon Underglow Glow Ring around Pedestal (soft diffuse glow)
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 36; ++i) {
        float a = i * 2.0f * PI / 36.0f;
        float cosA = std::cos(a);
        float cr = (cosA < 0.0f) ? 0.05f : 1.0f;
        float cg = (cosA < 0.0f) ? 0.90f : 0.50f;
        float cb = (cosA < 0.0f) ? 1.0f  : 0.05f;

        glColor4f(cr, cg, cb, 0.40f * pulse);
        glVertex3f(focusX + std::cos(a) * (platR - 0.1f), 0.01f, focusZ + std::sin(a) * (platR - 0.1f));
        glColor4f(cr, cg, cb, 0.0f);
        glVertex3f(focusX + std::cos(a) * (platR + 1.2f), 0.01f, focusZ + std::sin(a) * (platR + 1.2f));
    }
    glEnd();

    // =======================================================================
    // 3. HERO SPORTS CAR (Angled 3/4 front, no overhead badge)
    // =======================================================================
    drawShadow(focusX, focusZ, carHeading, 1.15f, 2.3f);

    // Under-car neon lighting (Cyan on front/left, Orange on rear/right, matching Image 2!)
    glColor4f(0.05f, 0.88f, 1.0f, 0.50f * pulse);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(focusX - 0.4f, platH + 0.02f, focusZ + 0.4f);
    for (int i = 0; i <= 16; ++i) {
        float a = i * 2.0f * PI / 16.0f;
        glVertex3f(focusX - 0.4f + std::cos(a) * 1.5f, platH + 0.02f, focusZ + 0.4f + std::sin(a) * 1.8f);
    }
    glEnd();

    glColor4f(1.0f, 0.50f, 0.05f, 0.50f * pulse);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(focusX + 0.4f, platH + 0.02f, focusZ - 0.4f);
    for (int i = 0; i <= 16; ++i) {
        float a = i * 2.0f * PI / 16.0f;
        glVertex3f(focusX + 0.4f + std::cos(a) * 1.5f, platH + 0.02f, focusZ - 0.4f + std::sin(a) * 1.8f);
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);

    // Render hero car in the colour of the selected car preset (no overhead badge)
    drawCar(focusX, focusZ, carHeading, 0.0f, 0.0f, 0.12f, 0.0f, carR, carG, carB, false, "");

    // 4. Studio spotlights: soft beams of light from the ceiling onto the turntable, and the
    // pool of light they make on it (added light: brighter where they cross)
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    const float lampX[2] = {focusX - 2.2f, focusX + 2.6f}, lampZ[2] = {focusZ + 1.2f, focusZ - 0.6f};
    for (int l = 0; l < 2; ++l) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 0.95f, 0.85f, 0.045f);
        glVertex3f(lampX[l], 7.7f, lampZ[l]);
        for (int i = 0; i <= 32; ++i) {
            const float a = float(i) * 2.0f * PI / 32.0f;
            glColor4f(1.0f, 0.95f, 0.85f, 0.0f);
            glVertex3f(focusX + std::cos(a) * 2.7f, platH + 0.02f, focusZ + std::sin(a) * 2.7f);
        }
        glEnd();
        glColor4f(1.0f, 0.97f, 0.90f, 0.9f);              // the lamp itself
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(lampX[l], 7.68f, lampZ[l]);
        for (int i = 0; i <= 16; ++i) {
            const float a = float(i) * 2.0f * PI / 16.0f;
            glVertex3f(lampX[l] + std::cos(a) * 0.22f, 7.68f, lampZ[l] + std::sin(a) * 0.22f);
        }
        glEnd();
    }
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f, 0.95f, 0.85f, 0.10f);
    glVertex3f(focusX, platH + 0.015f, focusZ);
    for (int i = 0; i <= 36; ++i) {
        const float a = float(i) * 2.0f * PI / 36.0f;
        glColor4f(1.0f, 0.95f, 0.85f, 0.0f);
        glVertex3f(focusX + std::cos(a) * 3.0f, platH + 0.015f, focusZ + std::sin(a) * 3.0f);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
}


void GLWidget::renderView(const sim::Car &me, const CarView &meView,
                    const sim::Car &other, const CarView &otherView,
                    int playerNum, int vx, int vy, int vw, int vh)
{
    (void)otherView;
    glViewport(vx, vy, vw, vh);
    ensureTrackGeometry();

    // Per-map sky, fog, sunlight and reflections; darker inside a tunnel (fading in and out
    // over about 30 m at the portals)
    const ThemeLook &th = THEMES[themeIndex()];
    nightMode = th.night;
    ensureEnvMap(themeIndex());
    glFogf(GL_FOG_START, std::min(110.0f, VIEW_DIST * 0.33f));
    glFogf(GL_FOG_END, VIEW_DIST);
    const float lit = 1.0f - (me.path ? 0.0f : tunnelShade(me.s)) * structureLook(simEngine.mapDef().theme).darkness;
    GLfloat fogCol[4] = {th.fog[0] * lit, th.fog[1] * lit, th.fog[2] * lit, 1.0f};
    glFogfv(GL_FOG_COLOR, fogCol);
    glFogf(GL_FOG_START, th.fogStart);
    glFogf(GL_FOG_END, th.fogEnd);
    GLfloat amb[4] = {th.ambient[0] * lit, th.ambient[1] * lit, th.ambient[2] * lit, 1.0f};
    GLfloat dif[4] = {th.diffuse[0] * lit, th.diffuse[1] * lit, th.diffuse[2] * lit, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);

    drawSky(vw, vh);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(meView.fov, double(vw) / double(std::max(vh, 1)), 0.5, 800.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    // On hills the cameras look along the slope: the point they look at sits on the road
    // ahead. The chase camera's height follows the car smoothly and never dips below the
    // road behind.
    if (camOverride) {
        gluLookAt(camEye[0], camEye[1], camEye[2], camAt[0], camAt[1], camAt[2], 0, 1, 0);
    } else {
        // Low chase camera that drops back as speed builds up
        float fx = std::sin(meView.camHeading), fz = -std::cos(meView.camHeading);
        float jx = std::sin(simEngine.getTick() * 1.9f) * meView.shake * 0.25f;
        float jy = std::cos(simEngine.getTick() * 2.3f) * meView.shake * 0.20f;
        float d = meView.camDist;
        float ex = me.x - fx * d + jx, ez = me.z - fz * d;
        float ey = std::max(meView.camY + 1.9f + 0.12f * d, simEngine.heightNear(ex, ez, me.s, me.path) + 1.2f) + jy;
        float lx = me.x + fx * 6.0f, lz = me.z + fz * 6.0f;
        float ly = me.air > 0 ? me.y + 1.0f : simEngine.heightNear(lx, lz, me.s, me.path) + 1.0f;
        EjectedDriver ed;
        if (me.wreck > 0 && ejectedDriver(me, ed) && me.wreck < sim::GameSim::WRECK_TIME_EJECT - 40) {
            // your driver flew out: the camera pulls back and up and keeps both in view
            lx = 0.5f * (me.x + ed.x); lz = 0.5f * (me.z + ed.z); ly = std::max(ly, ed.y);
            ex -= fx * 5.0f; ez -= fz * 5.0f; ey += 3.0f;
        }
        gluLookAt(ex, ey, ez, lx, ly, lz, 0, 1, 0);
    }

    GLfloat lp[] = {0.45f, 1.0f, 0.65f, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lp);

    glEnable(GL_DEPTH_TEST);

    // what this view can see (culling of the road, scenery and traffic)
    const float lookH = camOverride ? std::atan2(camAt[0] - camEye[0], -(camAt[2] - camEye[2]))
                      : meView.camHeading;
    viewX = camOverride ? camEye[0] : me.x;
    viewZ = camOverride ? camEye[2] : me.z;
    viewFx = std::sin(lookH); viewFz = -std::cos(lookH);

    glDisable(GL_LIGHTING); glDisable(GL_FOG);
    float camRefX = me.x, camRefZ = me.z;
    glPushMatrix();
    glTranslatef(0.0f, meView.camY, 0.0f);               // the backdrop rises with the car
    drawSunMountainsClouds(camRefX, camRefZ);
    glPopMatrix();
    glEnable(GL_LIGHTING);
    drawTemple(camRefX, camRefZ);

    glEnable(GL_FOG);
    drawGround(camRefX, camRefZ);
    if (simEngine.mapDef().theme == sim::MapTheme::Coast) drawSea();
    drawWater();
    drawRoad();
    drawRamps();
    drawSkidMarks();
    // the start/finish gantry (it has a whole scene around it) only when it can be seen
    // (a sprint: a START gantry at the start line and a FINISH gantry at the finish line)
    float gateX, gateZ;
    simEngine.pointAt(simEngine.startLine(), 0.0f, gateX, gateZ);
    if (inView(gateX, gateZ, 120.0f)) drawArch(simEngine.startLine(), simEngine.sprint() ? "START" : "FINISH");
    if (simEngine.sprint()) {
        simEngine.pointAt(simEngine.finishLine(), 0.0f, gateX, gateZ);
        if (inView(gateX, gateZ, 120.0f)) drawArch(simEngine.finishLine());
        drawRoadEnds();
    }
    drawProps();
    drawObstacles();
    drawShortcutGate();
    drawNitroPickups();

    // shadows
    glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
    for (const sim::Traffic &t : simEngine.getTraffic()) {
        float tx, tz;
        simEngine.pointAt(t.s, t.lane, tx, tz);
        if (!inView(tx, tz, 6.0f)) continue;
        const float ta = simEngine.headingAt(t.s);
        drawShadow(tx, tz, ta, t.truck ? 1.25f : 1.1f, t.truck ? 3.6f : 2.3f, poseOn(t.s, t.lane, ta));
    }
    const auto &allCars = simEngine.getCars();
    const int meIdx = (&me >= allCars.data() && &me < allCars.data() + allCars.size()) ? int(&me - allCars.data()) : 0;
    for (int i = 0; i < int(allCars.size()); ++i) {
        const sim::Car &c = allCars[i];
        const float above = std::max(0.0f, c.y - simEngine.heightAt(c.s, c.d, c.path));   // flying: smaller, fainter
        const float k = 1.0f / (1.0f + 0.08f * above);
        drawShadow(c.x, c.z, c.heading, 1.1f * k, 2.3f * k, poseOn(c.s, c.d, c.heading, c.path), 1.0f / (1.0f + 0.3f * above));
    }
    glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);

    // traffic
    for (const sim::Traffic &t : simEngine.getTraffic()) {
        float tx, tz;
        simEngine.pointAt(t.s, t.lane, tx, tz);
        if (!inView(tx, tz, 6.0f)) continue;
        const float ta = simEngine.headingAt(t.s);
        const RoadPose pose = poseOn(t.s, t.lane, ta);
        float spin = std::fmod(float(simEngine.getTick()) * t.speed * 136.0f, 360.0f);
        FigureLook tl;                                   // everyday drivers: no helmet, plain clothes
        tl.helmetOn = false;
        tl.suit[0] = 0.25f + 0.3f * t.b; tl.suit[1] = 0.30f + 0.2f * t.r; tl.suit[2] = 0.45f + 0.2f * t.g;
        if (t.truck) drawTruck(tx, tz, ta, spin, t.r, t.g, t.b, pose);
        else         drawCar(tx, tz, ta, 0.0f, 0.0f, 0.0f, spin, t.r, t.g, t.b, false, nullptr, 0.0f, pose, -1.0f, &tl);
    }

    // Name plate above each car: "1"/"2" in split screen, "CPU" in single player, names online
    // (your own car online and against CPUs: "YOU/3" = you are 3rd, as in the corner)
    auto badgeFor = [&](int i) -> QByteArray {
        if (isOnline()) {
            if (i == meIdx) return "YOU/" + QByteArray::number(simEngine.rankOf(i));
            return carName(i);
        }
        if (simEngine.getGameMode() == sim::GameSim::ModeSingle) {
            if (i == 0) return "YOU/" + QByteArray::number(simEngine.rankOf(0));
            return carName(i);
        }
        return QByteArray::number(i + 1);
    };
    for (int i = 0; i < int(allCars.size()); ++i) {
        const sim::Car &c = allCars[i];
        QByteArray badge = badgeFor(i);
        if (showCpuMinds && c.aiLevel > 0 && c.aiPersona >= 0) {   // F4: what this CPU is thinking
            badge = carName(i) + " " + sim::personaDef(c.aiPersona).shortName + " " + sim::moodShort(c.aiMood) + " "
                  + sim::tacticShort(c.aiTactic) + " " + QByteArray::number(int(std::lround(c.aiSkill * 100.0f)));
        }
        if (i == meIdx) badge = "";                      // your own car: no plate (health and place are in the corner)
        if (c.wreck > 0) { drawWreck(c, i, badge.constData()); continue; }
        if (c.ghost > 0 && (simEngine.getTick() / 4) % 2 == 0) continue;   // just back: blinking, passes through cars
        const FigureLook dl = driverLook(i, c);
        const bool out = std::find(driverOut.begin(), driverOut.end(), i) != driverOut.end();
        const RoadPose pose = carPose(c);
        if (c.barrel != 0.0f) {                          // a barrel roll: the whole car turns round its middle
            glPushMatrix();
            glTranslatef(c.x, pose.y + 0.7f, c.z);
            glRotatef(c.barrel * 180.0f / PI, std::sin(c.heading), 0.0f, -std::cos(c.heading));
            glTranslatef(-c.x, -pose.y - 0.7f, -c.z);
        }
        drawCar(c.x, c.z, c.heading, c.roll, c.pitch, c.steerVis, c.wheelSpin, c.r, c.g, c.b, c.nitroOn,
                hideHud ? nullptr : badge.constData(), (i < sim::MAX_RACERS) ? carFx[i].brakeGlow : 0.0f, pose,
                c.health, out ? nullptr : &dl);
        if (c.barrel != 0.0f) glPopMatrix();
    }
    for (const Actor &a : actors) {                      // story scenes: the characters standing at the grid
        if (!inView(a.x, a.z, 3.0f)) continue;
        glPushMatrix();
        glTranslatef(a.x, a.y, a.z);
        glRotatef(-a.heading * 180.0f / PI, 0, 1, 0);
        drawBlockFigure(a.look, a.pose);
        glPopMatrix();
    }
    // drivers thrown out of their wrecks
    for (int i = 0; i < int(allCars.size()); ++i) {
        EjectedDriver ed;
        if (!ejectedDriver(allCars[i], ed) || !inView(ed.x, ed.z, 3.0f)) continue;
        const FigureLook look = [&] { FigureLook l = driverLook(i, allCars[i]); l.face = ed.face; return l; }();
        glPushMatrix();
        glTranslatef(ed.x, ed.y, ed.z);
        glRotatef(-ed.heading * 180.0f / PI, 0, 1, 0);
        glTranslatef(0.0f, 0.35f, 0.0f);                 // tumble round the middle of the figure
        glRotatef(-ed.rotX, 1, 0, 0);
        glRotatef(ed.rotZ, 0, 0, 1);
        glTranslatef(0.0f, -0.35f, 0.0f);
        drawBlockFigure(look, ed.pose);
        glPopMatrix();
    }
    // damage numbers rising from the cars that were hit (red: it was that car's fault)
    glDisable(GL_LIGHTING);
    for (const DamagePopup &p : popups) {
        const int age = simEngine.getTick() - p.born;
        if (age < 0 || age > 70 || p.car >= int(allCars.size())) continue;
        const sim::Car &c = allCars[size_t(p.car)];
        if (!inView(c.x, c.z, 4.0f)) continue;
        glPushMatrix();
        glTranslatef(c.x, c.y + 3.05f + float(age) * 0.018f, c.z);
        glRotatef(-lookH * 180.0f / PI, 0, 1, 0);
        const float a = std::clamp(float(70 - age) / 25.0f, 0.0f, 1.0f);
        char buf[16];
        snprintf(buf, sizeof buf, "-%d", p.amount);
        if (p.fault) glColor4f(1.0f, 0.28f, 0.22f, a);
        else         glColor4f(1.0f, 0.86f, 0.30f, a);
        drawText3D(buf, 0.0f, 0.0f, 0.0f, 0.075f);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);

    drawParticles();

    glDisable(GL_FOG); glDisable(GL_LIGHTING);

    begin2D(vw, vh);
    if (!hideHud) drawHUD(me, other, playerNum, vw, vh);
}


// ===========================================================================
//  BACKDROP: sky, sun, clouds, mountains, temple
// ===========================================================================
void GLWidget::drawSky(int vw, int vh)
{
    begin2D(vw, vh);
    float h1 = vh * 0.42f, h2 = vh * 0.68f;
    glBegin(GL_QUADS);
    const ThemeLook &th = THEMES[themeIndex()];
    glColor3fv(th.skyHor); glVertex2f(0, 0);  glVertex2f(vw, 0);  glVertex2f(vw, h1); glVertex2f(0, h1);
    glColor3fv(th.skyHor); glVertex2f(0, h1); glVertex2f(vw, h1);
    glColor3fv(th.skyMid); glVertex2f(vw, h2); glVertex2f(0, h2);
    glColor3fv(th.skyMid); glVertex2f(0, h2); glVertex2f(vw, h2);
    glColor3fv(th.skyTop); glVertex2f(vw, vh); glVertex2f(0, vh);
    glEnd();
}

void GLWidget::drawSunMountainsClouds(float ex, float ez)
{
    const ThemeLook &th = THEMES[themeIndex()];
    glDepthMask(GL_FALSE);

    // Night sky: a dome of stars (fixed pattern from a hash, so they don't flicker)
    if (th.night) {
        glPointSize(2.0f);
        glBegin(GL_POINTS);
        for (int i = 0; i < 420; ++i) {
            float az = hash2(i, 1, 77) * 2.0f * PI, el = 0.08f + hash2(i, 2, 77) * 1.3f;
            float b = 0.45f + 0.55f * hash2(i, 3, 77);
            glColor4f(b, b, b * 1.05f, b);
            glVertex3f(ex + std::sin(az) * std::cos(el) * 650.0f, std::sin(el) * 650.0f, ez - std::cos(az) * std::cos(el) * 650.0f);
        }
        glEnd();
        glPointSize(1.0f);
    }

    float dx = 0.28f, dy = th.sunHeight, dz = -1.0f;
    float L = std::sqrt(dx * dx + dy * dy + dz * dz); dx /= L; dy /= L; dz /= L;
    float cx = ex + dx * 640.0f, cy = dy * 640.0f, cz = ez + dz * 640.0f;
    float rx = -dz, rz = dx; float rl = std::sqrt(rx * rx + rz * rz); rx /= rl; rz /= rl;
    float ux = -dy * rz, uy = rz * dx - rx * dz, uz = dy * rx;

    const float glowR = th.sunSize * 3.4f;
    glBegin(GL_TRIANGLE_FAN);
    glColor4fv(th.sunGlow); glVertex3f(cx, cy, cz);
    glColor4f(th.sunGlow[0], th.sunGlow[1], th.sunGlow[2], 0.0f);
    for (int i = 0; i <= 32; ++i) {
        float a = i * 2.0f * PI / 32.0f, c = std::cos(a) * glowR, s = std::sin(a) * glowR;
        glVertex3f(cx + rx * c + ux * s, cy + uy * s, cz + rz * c + uz * s);
    }
    glEnd();

    // Smooth sun disc: bright core, softened limb
    auto sunPt = [&](float a, float rad) {
        float c = std::cos(a) * rad, s = std::sin(a) * rad;
        glVertex3f(cx + rx * c + ux * s, cy + uy * s, cz + rz * c + uz * s);
    };
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(th.sunCore[0], th.sunCore[1], th.sunCore[2], 1.0f);
    glVertex3f(cx, cy, cz);
    glColor4f(th.sunEdge[0], th.sunEdge[1], th.sunEdge[2], 1.0f);
    for (int i = 0; i <= 48; ++i) sunPt(i * 2.0f * PI / 48.0f, th.sunSize);
    glEnd();
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 48; ++i) {
        float a = i * 2.0f * PI / 48.0f;
        glColor4f(th.sunEdge[0], th.sunEdge[1], th.sunEdge[2], 1.0f); sunPt(a, th.sunSize);
        glColor4f(th.sunEdge[0], th.sunEdge[1], th.sunEdge[2], 0.0f); sunPt(a, th.sunSize * 1.24f);
    }
    glEnd();
    glDepthMask(GL_TRUE);

    auto ring = [&](float rad, int n, float base, float a1, float a2, float a3, float seed,
                    const float top[3], const float bot[3]) {
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= n; ++i) {
            float a = i * 2.0f * PI / n;
            float h = base + a1 * std::sin(a * 7 + seed) + a2 * std::sin(a * 13 + seed * 2) + a3 * std::sin(a * 23 + seed * 3);
            if (i % 2) h *= 0.62f;
            h *= th.mtnScale;
            float x = ex + std::sin(a) * rad, z = ez - std::cos(a) * rad;
            glColor3fv(bot); glVertex3f(x, -20.0f, z);
            glColor3fv(top); glVertex3f(x, h, z);
        }
        glEnd();
    };
    if (th.night) {
        // City skyline: flat-topped towers of random height with a few lit windows
        for (int layer = 0; layer < 2; ++layer) {
            const float rad = layer ? 520.0f : 600.0f;
            const float *top = layer ? th.nearTop : th.farTop, *bot = layer ? th.nearBot : th.farBot;
            const int n = layer ? 150 : 110;
            for (int i = 0; i < n; ++i) {
                float a0 = i * 2.0f * PI / n, a1 = (i + 1) * 2.0f * PI / n;
                float h = (layer ? 18.0f : 30.0f) + hash2(i, layer, 91) * (layer ? 45.0f : 70.0f);
                float x0 = ex + std::sin(a0) * rad, z0 = ez - std::cos(a0) * rad;
                float x1 = ex + std::sin(a1) * rad, z1 = ez - std::cos(a1) * rad;
                glBegin(GL_QUADS);
                glColor3fv(bot); glVertex3f(x0, -20.0f, z0); glVertex3f(x1, -20.0f, z1);
                glColor3fv(top); glVertex3f(x1, h, z1); glVertex3f(x0, h, z0);
                glEnd();
                glBegin(GL_QUADS);
                for (int w = 0; w < 6; ++w) {
                    if (hash2(i * 7 + w, layer, 92) < 0.55f) continue;
                    float f = 0.15f + 0.7f * hash2(i * 7 + w, layer, 93), wy = 4.0f + hash2(i * 7 + w, layer, 94) * (h - 8.0f);
                    float wx = x0 + (x1 - x0) * f, wz = z0 + (z1 - z0) * f;
                    float sx = (x1 - x0) * 0.04f, sz = (z1 - z0) * 0.04f;
                    glColor4f(1.0f, 0.82f, 0.45f, 0.8f);
                    glVertex3f(wx - sx, wy - 1.2f, wz - sz); glVertex3f(wx + sx, wy - 1.2f, wz + sz);
                    glVertex3f(wx + sx, wy + 1.2f, wz + sz); glVertex3f(wx - sx, wy + 1.2f, wz - sz);
                }
                glEnd();
            }
        }
    } else {
        ring(600.0f, 72, 70.0f, 18.0f, 12.0f, 8.0f, 0.7f, th.farTop, th.farBot);
        ring(520.0f, 90, 38.0f, 12.0f, 9.0f, 6.0f, 2.1f, th.nearTop, th.nearBot);
    }
    if (th.cloud[3] <= 0.0f) return;

    // Soft clouds: a few overlapping translucent puffs, each turned to face the camera
    glDepthMask(GL_FALSE);
    for (int i = 0; i < 12; ++i) {
        float a = i * 0.523f + 0.3f + simEngine.getTick() * 0.00004f;
        float dist = 480.0f + (i % 3) * 35.0f;
        float kx = ex + std::sin(a) * dist, kz = ez - std::cos(a) * dist;
        float ky = 45.0f + (i % 4) * 8.0f;
        float sx = std::cos(a), sz = std::sin(a);            // sideways, across the view direction
        float w = 24.0f + (i % 3) * 10.0f;
        for (int p = 0; p < 5; ++p) {
            float off = (p - 2) * w * 0.36f + ((i * 7 + p * 3) % 5 - 2) * 2.0f;
            float py = ky + ((p == 1 || p == 2 || p == 3) ? 3.5f : 0.0f) + (p == 2 ? 3.0f : 0.0f);
            float pr = w * ((p == 2) ? 0.55f : (p == 0 || p == 4) ? 0.32f : 0.42f);
            float px0 = kx + sx * off, pz0 = kz + sz * off;
            glBegin(GL_TRIANGLE_FAN);
            glColor4fv(th.cloud);
            glVertex3f(px0, py, pz0);
            glColor4f(th.cloud[0], th.cloud[1] * 0.92f, th.cloud[2] * 0.9f, 0.0f);
            for (int k = 0; k <= 20; ++k) {
                float t = k * 2.0f * PI / 20.0f, c = std::cos(t) * pr, s = std::sin(t) * pr * 0.42f;
                glVertex3f(px0 + sx * c, py + s, pz0 + sz * c);
            }
            glEnd();
        }
    }
    glDepthMask(GL_TRUE);
}

void GLWidget::drawTemple(float ex, float ez)
{
    float tx, tz, rot;
    simEngine.landmark(tx, tz, rot);
    const float dx = tx - ex, dz = tz - ez, reach = VIEW_DIST + 120.0f;
    if (dx * dx + dz * dz > reach * reach) return;

    float base = groundAt(tx, tz);                       // the lowest ground under it
    for (float ox : {-45.0f, 45.0f})
        for (float oz : {-45.0f, 45.0f}) base = std::min(base, groundAt(tx + ox, tz + oz));
    glPushMatrix();
    glTranslatef(tx, base - 0.3f, tz);
    glRotatef(rot, 0, 1, 0);                             // the steps face the road

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texStone);
    glColor3f(1, 1, 1);
    g_ts = 0.15f;

    const float plW[4] = {42.0f, 36.0f, 30.0f, 24.0f};
    const float plH[4] = { 2.4f,  2.2f,  2.0f,  1.8f};
    float curY = 0;
    for (int i = 0; i < 4; ++i) {
        boxAt(0, curY + plH[i] * 0.5f, 0, plW[i], plH[i] * 0.5f, plW[i]);
        curY += plH[i];
    }
    for (int s = 0; s < 18; ++s) {
        float sw = 6.0f, sh = 0.5f, sl = 0.9f;
        boxAt(0, s * sh + sh * 0.5f, plW[0] + (18 - s) * sl - 1.0f, sw, sh * 0.5f, sl * 0.5f);
    }
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.85f, 0.85f, 0.82f);
    const float colR = plW[3] - 2.5f;
    for (int i = 0; i < 16; ++i) {
        float a = i * 2.0f * PI / 16.0f;
        float cx = std::cos(a) * colR, cz = std::sin(a) * colR;
        glPushMatrix();
        glTranslatef(cx, curY, cz);
        glRotatef(-90, 1, 0, 0);
        gluCylinder(quad, 1.3, 1.3, 13.0, 10, 1);
        glPopMatrix();
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texStone);
    glColor3f(0.95f, 0.95f, 0.95f);
    boxAt(0, curY + 6.5f, 0, colR - 2.5f, 6.5f, colR - 2.5f);
    curY += 13.0f;

    glBindTexture(GL_TEXTURE_2D, texRoof);
    g_ts = 0.35f;
    glColor3f(0.78f, 0.22f, 0.18f);
    const float rfW[3] = {32.0f, 25.0f, 18.0f};
    for (int i = 0; i < 3; ++i) {
        boxAt(0, curY + 0.8f, 0, rfW[i], 0.8f, rfW[i]);
        boxAt(0, curY + 2.2f, 0, rfW[i] - 3.0f, 0.7f, rfW[i] - 3.0f);
        curY += 3.5f;
        boxAt(0, curY + 3.0f, 0, rfW[i] - 4.5f, 3.0f, rfW[i] - 4.5f);
        curY += 6.0f;
    }
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.95f, 0.82f, 0.25f);
    glPushMatrix();
    glTranslatef(0, curY, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 2.2, 0.0, 14.0, 10, 1);
    glPopMatrix();

    glPopMatrix();
    g_ts = 0.25f;
}

// ===========================================================================
//  GROUND, ROAD, ARCHES
// ===========================================================================
void GLWidget::ensureTrackGeometry()
{
    if (stretchKey != trackKey()) findStretches();
    if (roadKey != trackKey()) buildRoad();
    if (groundKey != trackKey()) buildGround();
}

void GLWidget::findStretches()
{
    stretchKey = trackKey();
    stretches.clear();
    if (simEngine.hasShortcut()) {
        float x, z, s, d;
        simEngine.pointAt(std::min(40.0f, simEngine.shortcutEnd()), 0.0f, x, z, 1);
        simEngine.project(x, z, simEngine.shortcutFrom() + 40.0f, s, d, 0, false);
        forkSide = d >= 0.0f ? 1.0f : -1.0f;
        simEngine.pointAt(std::max(0.0f, simEngine.shortcutEnd() - 40.0f), 0.0f, x, z, 1);
        simEngine.project(x, z, simEngine.shortcutTo() - 40.0f, s, d, 0, false);
        mergeSide = d >= 0.0f ? 1.0f : -1.0f;
    }
    const auto &S = simEngine.circuit().slices();
    const int n = int(S.size());
    auto kindAt = [&](int i) {
        const uint16_t f = S[size_t(i)].flags;
        if (f & sim::TF_TUNNEL) return 1;
        if (f & sim::TF_BRIDGE) return 2;
        if ((f & sim::TF_OVERPASS) && simEngine.heightAt(float(i), 0.0f) > 1.0f) return 3;   // its raised part
        return 0;
    };
    for (int i = 0; i < n;) {
        const int k = kindAt(i);
        if (!k) { ++i; continue; }
        Stretch st;
        st.a = i;
        st.kind = k;
        while (i < n && kindAt(i) == k) ++i;
        st.b = i;
        if (k == 1) {
            // the mountain side of a tunnel is where the other roads around are higher
            const int mid = (st.a + st.b) / 2;
            const sim::Slice &m = S[size_t(mid)];
            float sum[2] = {0.0f, 0.0f}, w[2] = {0.0f, 0.0f};
            for (int j = 0; j < n; j += 4) {
                if (std::min(std::abs(j - mid), n - std::abs(j - mid)) < 250) continue;
                const float dx = S[size_t(j)].x - m.x, dz = m.z - S[size_t(j)].z, d2 = dx * dx + dz * dz;
                if (d2 > 300.0f * 300.0f) continue;
                const int right = dx * m.tz + dz * m.tx > 0.0f ? 1 : 0;
                sum[right] += S[size_t(j)].y / (d2 + 400.0f);
                w[right] += 1.0f / (d2 + 400.0f);
            }
            const float hr = w[1] > 0.0f ? sum[1] / w[1] : -1e9f, hl = w[0] > 0.0f ? sum[0] / w[0] : -1e9f;
            st.side = hr >= hl ? 1.0f : -1.0f;
        }
        stretches.push_back(st);
    }
}

float GLWidget::tunnelShade(float s) const
{
    float shade = 0.0f;
    for (const Stretch &st : stretches) {
        if (st.kind != 1) continue;
        const float inside = std::min(s - float(st.a), float(st.b) - s);   // metres past the nearer portal
        shade = std::max(shade, std::clamp((inside + 10.0f) / 30.0f, 0.0f, 1.0f));
    }
    return shade;
}

// A flat water plane under a bridge over a river or the harbour: it shows wherever the
// ground dips below it (the valley under the bridge)
void GLWidget::drawWater()
{
    const StructureLook &lk = structureLook(simEngine.mapDef().theme);
    if (!lk.water) return;
    for (const Stretch &st : stretches) {
        if (st.kind != 2) continue;
        const float mid = 0.5f * float(st.a + st.b);
        float cx, cz;
        simEngine.pointAt(mid, 0.0f, cx, cz);
        if (!inView(cx, cz, 250.0f)) continue;
        const float y = lk.water == 1 ? simEngine.heightAt(mid, 0.0f) - 5.0f : 0.0f;
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glBegin(GL_QUADS);
        const float R = 240.0f, step = 40.0f;           // small squares, so the fog shades them
        for (float x = cx - R; x < cx + R; x += step)
            for (float z = cz - R; z < cz + R; z += step) {
                glColor4f(0.16f, 0.40f, 0.52f, 0.92f);
                glVertex3f(x, y, z); glVertex3f(x + step, y, z);
                glVertex3f(x + step, y, z + step); glVertex3f(x, y, z + step);
            }
        glEnd();
        glEnable(GL_LIGHTING);
    }
}

// The ground is an 8 m grid around the whole circuit. Up to 12 m past the guard rails it
// lies just under the road's surface (banking included); further out it blends into the
// height of the roads around, so slopes, cuttings and embankments form between parts of
// the circuit at different heights, and then into gentle hills of our own noise. Coastal
// Highway's ground drops to a beach and under the sea; around the temple it is level.
void GLWidget::buildGround()
{
    for (const GroundChunk &c : groundChunks) glDeleteLists(c.list, 1);
    groundChunks.clear();
    groundKey = trackKey();

    const auto &S = simEngine.circuit().slices();
    const int n = int(S.size());
    const float CELL = 8.0f, REACH = 160.0f, BIN = 64.0f, MARGIN = 400.0f;
    const float rail = railOffset(), band = rail + 12.0f;
    const StructureLook &look = structureLook(simEngine.mapDef().theme);
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, mean = 0.0f;
    for (const sim::Slice &sl : S) {
        x0 = std::min(x0, sl.x); x1 = std::max(x1, sl.x);
        z0 = std::min(z0, -sl.z); z1 = std::max(z1, -sl.z);
        mean += sl.y;
    }
    mean /= float(n);
    groundX0 = x0 - MARGIN; groundZ0 = z0 - MARGIN;
    groundW = int((x1 - x0 + 2.0f * MARGIN) / CELL) + 2;
    groundD = int((z1 - z0 + 2.0f * MARGIN) / CELL) + 2;

    // every 4th slice, sorted into 64 m squares
    const int bw = int(float(groundW) * CELL / BIN) + 1, bd = int(float(groundD) * CELL / BIN) + 1;
    std::vector<std::vector<int>> bins(size_t(bw * bd));
    std::vector<char> raised(size_t(n), 0);              // the raised highway stands on pillars
    for (const Stretch &st : stretches)
        if (st.kind == 3)
            for (int k = st.a; k < st.b; ++k) raised[size_t(k)] = 1;
    const auto &CS = simEngine.circuit().shortcut();      // (its slice k is stored as -1 - k)
    for (int k = 0; k < int(CS.size()); k += 4) {
        const int bx = int((CS[size_t(k)].x - groundX0) / BIN), bz = int((-CS[size_t(k)].z - groundZ0) / BIN);
        bins[size_t(bz * bw + bx)].push_back(-1 - k);
    }
    for (int k = 0; k < n; k += 4) {
        if (raised[size_t(k)]) continue;
        const int bx = int((S[size_t(k)].x - groundX0) / BIN), bz = int((-S[size_t(k)].z - groundZ0) / BIN);
        bins[size_t(bz * bw + bx)].push_back(k);
    }

    const sim::MapTheme theme = simEngine.mapDef().theme;
    const float amp = theme == sim::MapTheme::Snow ? 22.0f : theme == sim::MapTheme::Jungle ? 16.0f : theme == sim::MapTheme::Countryside ? 9.0f
                    : theme == sim::MapTheme::Coast ? 7.0f : theme == sim::MapTheme::Desert ? 6.0f : 0.0f;
    const float seaX = simEngine.seaLineX();
    auto smooth = [](float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };

    groundH.assign(size_t(groundW * groundD), mean);
    std::vector<float> nearDist(groundH.size(), 1e9f);
    for (int j = 0; j < groundD; ++j)
        for (int i = 0; i < groundW; ++i) {
            const float x = groundX0 + float(i) * CELL, z = groundZ0 + float(j) * CELL;
            // the nearest road, and the roads' heights around blended by distance
            float wsum = 1.0f / (4.0f * REACH * REACH), hsum = mean * wsum, best = 1e18f, cutBest = 1e18f, mainBest = 1e18f;
            int nearest = -1, nearestMain = -1, nearestOnCut = -1;
            bool nearestCut = false;
            const int bx = int((x - groundX0) / BIN), bz = int((z - groundZ0) / BIN), rings = int(REACH / BIN) + 1;
            for (int dz = -rings; dz <= rings; ++dz)
                for (int dx = -rings; dx <= rings; ++dx) {
                    const int cx = bx + dx, cz = bz + dz;
                    if (cx < 0 || cz < 0 || cx >= bw || cz >= bd) continue;
                    for (int e : bins[size_t(cz * bw + cx)]) {
                        const bool onCut = e < 0;
                        const int k = onCut ? -1 - e : e;
                        const sim::Slice &sl = onCut ? CS[size_t(k)] : S[size_t(k)];
                        const float ex = sl.x - x, ez = -sl.z - z, d2 = ex * ex + ez * ez;
                        if (d2 < best) { best = d2; nearest = k; nearestCut = onCut; }
                        if (onCut && d2 < cutBest) { cutBest = d2; nearestOnCut = k; }
                        if (!onCut && d2 < mainBest) { mainBest = d2; nearestMain = k; }
                        if (d2 < REACH * REACH) {
                            const float f = 1.0f - std::sqrt(d2) / REACH, w = f * f / (d2 + 400.0f);
                            wsum += w;
                            hsum += w * sl.y;
                        }
                    }
                }
            const float dist = std::sqrt(best);
            const float hills = amp * (2.0f * fbm(x / 2048.0f, z / 2048.0f, 8, 3, 911) - 1.0f);
            const float around = hsum / wsum + hills * smooth((dist - band) / 120.0f);
            float h = around;
            if (nearest >= 0 && dist < band + 40.0f) {
                // (where the shortcut leaves or joins, both roads are near: under the lower one)
                const float near2 = (band + 40.0f) * (band + 40.0f);
                float road = 1e9f;
                if (nearestMain >= 0 && mainBest < near2) {
                    road = simEngine.heightNear(x, z, float(nearestMain));
                    if (simEngine.nearRamp(float(nearestMain), 48.0f, 30.0f)) road -= 0.6f;   // ramps bend too sharply for the grid
                }
                if (nearestOnCut >= 0 && cutBest < near2) road = std::min(road, simEngine.heightNear(x, z, float(nearestOnCut), 1));
                road -= 0.15f;
                h = road + (around - road) * smooth((dist - band) / 40.0f);
            }
            if (x > seaX - 90.0f) {                      // beach, then under the sea
                const float beach = x < seaX - 8.0f ? 0.2f : std::max(-2.5f, 0.2f - (x - seaX + 8.0f) * 0.4f);
                h += (beach - h) * smooth((x - seaX + 90.0f) / 40.0f) * smooth((dist - band) / 10.0f);
            }
            if (nearest >= 0 && !nearestCut && cutBest > (band + 10.0f) * (band + 10.0f)) {
                // a hill over a tunnel and a valley under a bridge (never next to the shortcut). The hill starts past the band
                // that follows the road (so the grid never slopes up across the road); the tunnel
                // has its own walls and roof, and its portal face hides the gap
                const sim::Slice &sl = S[size_t(nearest)];
                const float side = (x - sl.x) * sl.tz + (z + sl.z) * sl.tx;   // metres right of that slice
                const float road = simEngine.heightAt(float(nearest), 0.0f);
                for (const Stretch &st : stretches) {
                    const float out = std::max({float(st.a - nearest), float(nearest - st.b), 0.0f});
                    if (st.kind == 1 && out < 30.0f && std::fabs(side) > band
                        && (!look.oneSided || side * st.side > 0.0f)) {
                        const float across = 1.0f - smooth((std::fabs(side) - band - 8.0f) / 70.0f);
                        h = std::max(h, road + look.hill * across * (1.0f - smooth(out / 30.0f)));
                    } else if (st.kind == 2 && out == 0.0f) {
                        const float fromEnd = float(std::min(nearest - st.a, st.b - nearest));
                        h = std::min(h, road - look.depth * smooth((fromEnd - 8.0f) / 25.0f)
                                        + std::max(0.0f, std::fabs(side) - 200.0f) * 0.3f);
                    }
                }
            }
            groundH[size_t(j * groundW + i)] = h;
            nearDist[size_t(j * groundW + i)] = dist;
        }

    // a level square round the temple
    float lx, lz, lrot;
    simEngine.landmark(lx, lz, lrot);
    const int li = std::clamp(int((lx - groundX0) / CELL + 0.5f), 0, groundW - 1);
    const int lj = std::clamp(int((lz - groundZ0) / CELL + 0.5f), 0, groundD - 1);
    const float level = groundH[size_t(lj * groundW + li)];
    for (int j = 0; j < groundD; ++j)
        for (int i = 0; i < groundW; ++i) {
            const float dx = groundX0 + float(i) * CELL - lx, dz = groundZ0 + float(j) * CELL - lz;
            const float t = smooth((std::sqrt(dx * dx + dz * dz) - 70.0f) / 40.0f);
            float &h = groundH[size_t(j * groundW + i)];
            if (nearDist[size_t(j * groundW + i)] > band) h = level + (h - level) * t;
        }
    groundMin = *std::min_element(groundH.begin(), groundH.end());

    // 64 m display lists; each row of cells is a strip (cells split from (i, j+1) to (i+1, j),
    // which groundAt() follows)
    auto H = [&](int i, int j) {
        return groundH[size_t(std::clamp(j, 0, groundD - 1) * groundW + std::clamp(i, 0, groundW - 1))];
    };
    const float oldTs = g_ts;
    g_ts = 0.08f;
    const int CH = 8;
    for (int cj = 0; cj + 1 < groundD; cj += CH)
        for (int ci = 0; ci + 1 < groundW; ci += CH) {
            GroundChunk gc;
            gc.list = glGenLists(1);
            gc.x = groundX0 + (float(ci) + 0.5f * CH) * CELL;
            gc.z = groundZ0 + (float(cj) + 0.5f * CH) * CELL;
            glNewList(gc.list, GL_COMPILE);
            for (int j = cj; j < std::min(cj + CH, groundD - 1); ++j) {
                glBegin(GL_TRIANGLE_STRIP);
                for (int i = ci; i <= std::min(ci + CH, groundW - 1); ++i)
                    for (int k : {j, j + 1}) {
                        const float nx = (H(i - 1, k) - H(i + 1, k)) / (2.0f * CELL);
                        const float nz = (H(i, k - 1) - H(i, k + 1)) / (2.0f * CELL);
                        const float len = std::sqrt(nx * nx + 1.0f + nz * nz);
                        glNormal3f(nx / len, 1.0f / len, nz / len);
                        const float x = groundX0 + float(i) * CELL, z = groundZ0 + float(k) * CELL;
                        tv(x, H(i, k), z, x, z);
                    }
                glEnd();
            }
            glEndList();
            groundChunks.push_back(gc);
        }
    g_ts = oldTs;

    // check: the ground must never cover the road
    int covered = 0;
    for (int k = 0; k < n; k += 2)
        for (float d : {-roadHalf(), -0.5f * roadHalf(), 0.0f, 0.5f * roadHalf(), roadHalf()}) {
            float x, z;
            simEngine.pointAt(float(k), d, x, z);
            if (groundAt(x, z) > simEngine.heightAt(float(k), d) - 0.02f && ++covered <= 6)
                qDebug() << "[Ground] covers the road at s" << k << "d" << d << "by" << groundAt(x, z) - simEngine.heightAt(float(k), d);
        }
    for (int k = 0; k + 1 < int(CS.size()); k += 2)          // and the shortcut
        for (float d : {-1.0f, 0.0f, 1.0f}) {
            const float dd = d * simEngine.roadHalfAt(float(k), 1);
            float x, z;
            simEngine.pointAt(float(k), dd, x, z, 1);
            if (groundAt(x, z) > simEngine.heightAt(float(k), dd, 1) - 0.04f && ++covered <= 6)
                qDebug() << "[Ground] covers the shortcut at s" << k << "d" << dd;
        }
    qDebug() << "[Ground]" << simEngine.mapDef().name << groundW << "x" << groundD << "points,"
             << groundChunks.size() << "pieces, road covered at" << covered << "of" << (n / 2 + n % 2) * 5 << "checks";
}

float GLWidget::groundAt(float x, float z) const
{
    if (groundH.empty()) return 0.0f;
    const float CELL = 8.0f;
    const float u = std::clamp((x - groundX0) / CELL, 0.0f, float(groundW - 1) - 0.001f);
    const float v = std::clamp((z - groundZ0) / CELL, 0.0f, float(groundD - 1) - 0.001f);
    const int i = int(u), j = int(v);
    const float fx = u - float(i), fz = v - float(j);
    auto H = [&](int a, int b) { return groundH[size_t(b * groundW + a)]; };
    if (fx + fz <= 1.0f)                                // the triangle (i, j), (i+1, j), (i, j+1)
        return H(i, j) + (H(i + 1, j) - H(i, j)) * fx + (H(i, j + 1) - H(i, j)) * fz;
    return H(i + 1, j + 1) + (H(i, j + 1) - H(i + 1, j + 1)) * (1.0f - fx) + (H(i + 1, j) - H(i + 1, j + 1)) * (1.0f - fz);
}

void GLWidget::drawGround(float ex, float ez)
{
    const ThemeLook &th = THEMES[themeIndex()];
    GLuint tex = texGrass;
    switch (simEngine.mapDef().theme) {
    case sim::MapTheme::Desert: tex = texSand; break;
    case sim::MapTheme::Snow:   tex = texSnow; break;
    case sim::MapTheme::City:   tex = texPavement; break;
    default: break;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    g_ts = 0.08f;
    glColor3fv(th.groundTint);

    // a flat skirt below the lowest ground, in case the view reaches past the height map
    const float rad = VIEW_DIST + 30.0f, fy = groundMin - 3.0f;
    const int N = 36;
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, 1, 0);
    tv(ex, fy, ez, ex, ez);
    for (int i = 0; i <= N; ++i) {
        float a = i * 2.0f * PI / N;
        float gx = ex + std::sin(a) * rad, gz = ez - std::cos(a) * rad;
        tv(gx, fy, gz, gx, gz);
    }
    glEnd();
    for (const GroundChunk &c : groundChunks)
        if (inView(c.x, c.z, 46.0f)) glCallList(c.list);
    g_ts = 0.25f;
}

// A racer's pose: standing on the road, or flying (nose along the flight path, wings level)
RoadPose GLWidget::carPose(const sim::Car &c) const
{
    RoadPose p = poseOn(c.s, c.d, c.heading, c.path);
    p.y = c.y;
    if (c.air > 0) {
        p.pitch = std::atan2(c.vy, std::max(0.05f, std::fabs(c.speed))) * 180.0f / PI;
        p.roll = 0.0f;
    }
    return p;
}

// Jump ramps: yellow and black bands up the ramp, a steel lip, and JUMP boards on both
// sides 80 m before it; a SHORTCUT board 80 m before the fork
void GLWidget::drawRamps()
{
    if (simEngine.hasShortcut()) {
        const float s = simEngine.wrapS(simEngine.shortcutFrom() - 80.0f);
        float bx, bz;
        simEngine.pointAt(s, 0.0f, bx, bz);
        if (inView(bx, bz, 40.0f)) {
            glPushMatrix();
            placeOnRoad(s, forkSide * (railOffset() + 2.0f));
            glColor3f(0.30f, 0.32f, 0.35f);
            boxAt(0, 1.3f, 0, 0.09f, 1.3f, 0.09f);
            glColor3f(0.20f, 0.45f, 0.25f);
            boxAt(0, 3.0f, 0, 2.3f, 0.65f, 0.06f);
            glDisable(GL_LIGHTING);
            glColor3f(0.95f, 0.95f, 0.90f);
            drawText3D(forkSide > 0 ? "SHORTCUT >" : "< SHORTCUT", 0, 2.70f, 0.07f, 0.075f);
            glEnable(GL_LIGHTING);
            glPopMatrix();
        }
    }
    // Side ramps: a pink kicker in one lane with white chevrons pointing up it (a twisted one
    // purple and orange, one side higher; a dangerous one near the finish with red edges),
    // its sides and the drop at its lip shaded darker
    for (const sim::GameSim::SideRamp &r : simEngine.sideRamps()) {
        if (!simEngine.present(r.fromLap)) continue;
        float lx, lz;
        simEngine.pointAt(r.s, r.d, lx, lz);
        if (!inView(lx, lz, 40.0f)) continue;
        auto P = [&](float s, float d, float up, float &x, float &y, float &z) {
            simEngine.pointAt(s, d, x, z);
            y = simEngine.heightAt(s, d) + (up > 0.0f ? simEngine.sideLift(s, d) : 0.0f) + 0.03f;
        };
        auto V = [&](float s, float d, float up) { float x, y, z; P(s, d, up, x, y, z); glVertex3f(x, y, z); };
        const bool twist = r.twist != 0.0f, late = r.danger > 1.0f;
        const float d0 = r.d - r.half + 0.02f, d1 = r.d + r.half - 0.02f;
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glBegin(GL_QUADS);
        const int N = 8;
        for (int k = 0; k < N; ++k) {                    // the top, in stripes up the slope
            const float s0 = r.s - r.run + r.run * float(k) / N, s1 = r.s - r.run + r.run * float(k + 1) / N - 0.01f;
            const float shade = 0.80f + 0.20f * float(k) / N;
            if (twist) { if (k % 2) glColor3f(0.55f * shade, 0.20f * shade, 0.85f * shade); else glColor3f(1.0f * shade, 0.55f * shade, 0.15f * shade); }
            else       { if (k % 2) glColor3f(0.95f * shade, 0.25f * shade, 0.65f * shade); else glColor3f(1.0f * shade, 0.45f * shade, 0.80f * shade); }
            for (int j = 0; j < 2; ++j) {
                const float a = j ? r.d : d0, b = j ? d1 : r.d;
                V(s0, a, 1); V(s0, b, 1); V(s1, b, 1); V(s1, a, 1);
            }
        }
        for (int j = 0; j < 2; ++j) {                    // its sides: from the road up to the top
            const float d = j ? d1 : d0;
            glColor3f(late ? 0.75f : 0.55f, late ? 0.12f : 0.15f, late ? 0.15f : 0.40f);
            for (int k = 0; k < N; ++k) {
                const float s0 = r.s - r.run + r.run * float(k) / N, s1 = r.s - r.run + r.run * float(k + 1) / N;
                V(s0, d, 0); V(s1, d, 0); V(s1, d, 1); V(s0, d, 1);
            }
        }
        glColor3f(0.35f, 0.10f, 0.28f);                  // the drop at the lip
        for (int j = 0; j < 4; ++j) {
            const float a = d0 + (d1 - d0) * float(j) / 4.0f, b = d0 + (d1 - d0) * float(j + 1) / 4.0f;
            V(r.s - 0.02f, a, 1); V(r.s - 0.02f, b, 1); V(r.s - 0.02f, b, 0); V(r.s - 0.02f, a, 0);
        }
        glEnd();
        glBegin(GL_TRIANGLES);                           // white chevrons pointing up the ramp
        glColor3f(1.0f, 1.0f, 1.0f);
        for (int k = 1; k <= 3; ++k) {
            const float sc = r.s - r.run + r.run * float(k) / 4.0f;
            float ax, ay, az, bx, by, bz, cx, cy, cz;
            P(sc + 1.4f, r.d, 1, ax, ay, az); P(sc, r.d - 0.9f, 1, bx, by, bz); P(sc, r.d + 0.9f, 1, cx, cy, cz);
            glVertex3f(ax, ay + 0.02f, az); glVertex3f(bx, by + 0.02f, bz); glVertex3f(cx, cy + 0.02f, cz);
        }
        glEnd();
        if (late) {                                      // red edges: a dangerous ramp
            glLineWidth(3.0f);
            glBegin(GL_LINES);
            glColor3f(1.0f, 0.15f, 0.10f);
            for (float d : {d0, d1}) { V(r.s - r.run, d, 1); V(r.s, d, 1); }
            V(r.s, d0, 1); V(r.s, d1, 1);
            glEnd();
            glLineWidth(1.0f);
        }
        glEnable(GL_LIGHTING);
    }
    for (const sim::GameSim::Ramp &r : simEngine.ramps()) {
        float lx, lz;
        simEngine.pointAt(r.lip, 0.0f, lx, lz);
        if (!inView(lx, lz, 120.0f)) continue;
        const float half = roadHalf();
        auto V = [&](float s, float d, float y) {        // y: above the road's surface
            float x, z;
            simEngine.pointAt(s, d, x, z);
            glVertex3f(x, simEngine.heightAt(s, d) + y, z);
        };
        glDisable(GL_TEXTURE_2D);
        glNormal3f(0, 1, 0);
        glBegin(GL_QUADS);
        for (float k = -r.run; k < -0.6f; k += 2.0f) {
            if (int((k + r.run) / 2.0f) % 2) glColor3f(0.10f, 0.10f, 0.10f);
            else                              glColor3f(1.00f, 0.80f, 0.10f);
            const float s0 = r.lip + k, s1 = std::min(r.lip - 0.6f, s0 + 2.0f);
            V(s0, -half, 0.04f); V(s0, half, 0.04f); V(s1, half, 0.04f); V(s1, -half, 0.04f);
        }
        glColor3f(0.72f, 0.74f, 0.78f);                  // the steel lip
        V(r.lip - 0.6f, -half, 0.06f); V(r.lip - 0.6f, half, 0.06f); V(r.lip, half, 0.06f); V(r.lip, -half, 0.06f);
        glEnd();
        for (float side : {-1.0f, 1.0f}) {
            glPushMatrix();
            placeOnRoad(r.lip - 80.0f, side * (railOffset() + 2.0f));
            glColor3f(0.30f, 0.32f, 0.35f);
            boxAt(0, 1.3f, 0, 0.09f, 1.3f, 0.09f);
            glColor3f(1.0f, 0.80f, 0.10f);
            boxAt(0, 3.0f, 0, 1.7f, 0.65f, 0.06f);
            glDisable(GL_LIGHTING);
            glColor3f(0.08f, 0.08f, 0.08f);
            drawText3D("JUMP", 0, 2.62f, 0.07f, 0.11f);
            glEnable(GL_LIGHTING);
            glPopMatrix();
        }
    }
}

RoadPose GLWidget::poseOn(float s, float d, float heading, int path) const
{
    RoadPose p;
    p.y = simEngine.heightAt(s, d, path);
    const auto &R = path ? simEngine.circuit().shortcut() : simEngine.circuit().slices();
    const sim::Slice &sl = R[size_t(std::clamp(int(path ? s : simEngine.wrapS(s)), 0, int(R.size()) - 1))];
    const float g = simEngine.gradeAt(s, path), b = simEngine.bankAt(s, path);   // rise per metre along, drop per metre to the right
    const float sh = std::sin(heading), ch = std::cos(heading);
    const float fwdAlong = sh * sl.tx + ch * sl.tz, fwdRight = sh * sl.tz - ch * sl.tx;      // car's forward on the road
    const float rightAlong = ch * sl.tx - sh * sl.tz, rightRight = ch * sl.tz + sh * sl.tx;  // car's right on the road
    p.pitch = std::atan(g * fwdAlong - b * fwdRight) * 180.0f / PI;
    p.roll = std::atan(g * rightAlong - b * rightRight) * 180.0f / PI;
    return p;
}

bool GLWidget::inView(float x, float z, float radius) const
{
    const float dx = x - viewX, dz = z - viewZ, reach = VIEW_DIST + radius;
    return dx * dx + dz * dz < reach * reach && dx * viewFx + dz * viewFz > -30.0f - radius;
}

void GLWidget::placeOnRoad(float s, float d) const
{
    float x, z;
    simEngine.pointAt(s, d, x, z);
    // on the road and its verge: the road's height; further out: the ground's
    const float y = std::fabs(d) <= railOffset() + 1.0f ? simEngine.heightAt(s, d) : groundAt(x, z);
    glTranslatef(x, y, z);
    glRotatef(-simEngine.headingAt(s) * 180.0f / PI, 0, 1, 0);   // local -Z = along the road, +X = right
}

// The whole circuit is recorded once per map and road width, in display lists of about
// 50 m each: the asphalt (textured), then kerbs, lines, guard rails and posts.
void GLWidget::buildRoad()
{
    for (const RoadChunk &c : roadChunks) glDeleteLists(c.surface, 2);
    roadChunks.clear();
    roadKey = trackKey();

    const auto &S = simEngine.circuit().slices();
    const int n = int(S.size());
    const float half = roadHalf(), rail = railOffset();
    const ThemeLook &th = THEMES[themeIndex()];

    // Where another part of the circuit passes close by (Neon City's street under the raised
    // highway): no pillars there
    std::vector<char> cross(size_t(n), 0);
    const float near2 = (rail + 4.0f) * (rail + 4.0f), coarse2 = (rail + 8.0f) * (rail + 8.0f);
    auto dist2 = [&](int i, int j) {
        const float dx = S[size_t(i)].x - S[size_t(j)].x, dz = S[size_t(i)].z - S[size_t(j)].z;
        return dx * dx + dz * dz;
    };
    for (int i = 0; i < n; ++i)
        for (int j0 = 0; j0 < n; j0 += 4) {               // every 4th slice first, then its neighbours
            if (dist2(i, j0) > coarse2) continue;
            for (int j = std::max(0, j0 - 3); j <= std::min(n - 1, j0 + 3); ++j) {
                const int di = std::min(std::abs(i - j), n - std::abs(i - j));
                if (di >= 120 && dist2(i, j) < near2) cross[size_t(i)] = 1;
            }
        }
    auto at = [&](int i) { return size_t(i % n); };
    std::vector<int> stretchOf(size_t(n), -1);           // which tunnel / bridge a slice is in
    for (size_t k = 0; k < stretches.size(); ++k)
        for (int i = stretches[k].a; i < stretches[k].b; ++i) stretchOf[at(i)] = int(k);
    auto kind = [&](int i) { return stretchOf[at(i)] < 0 ? 0 : stretches[size_t(stretchOf[at(i)])].kind; };
    // where the shortcut leaves or joins, no main-road rail stands across it (per side)
    const bool cut = simEngine.hasShortcut();
    const float cutRail = cut ? simEngine.roadHalfAt(1.0f, 1) + 4.0f : 0.0f;
    std::vector<char> railGap[2] = {std::vector<char>(size_t(n), 0), std::vector<char>(size_t(n), 0)};
    if (cut)
        for (int e = 0; e < 2; ++e) {
            const float join = e == 0 ? simEngine.shortcutFrom() : simEngine.shortcutTo();
            for (int k = -160; k <= 160; ++k) {
                const float s = simEngine.wrapS(join + float(k));
                for (int side = 0; side < 2; ++side) {
                    float x, z, os, od;
                    simEngine.pointAt(s, (side ? 1.0f : -1.0f) * rail, x, z);
                    simEngine.project(x, z, e == 0 ? 0.0f : simEngine.shortcutEnd(), os, od, 1, false);
                    if (std::fabs(od) < cutRail + 1.0f && os > 0.5f && os < simEngine.shortcutEnd() - 0.5f)
                        railGap[side][at(int(s))] = 1;
                }
            }
        }
    const StructureLook &lk = structureLook(simEngine.mapDef().theme);
    auto P = [&](float s, float d, float y) {                 // y: above the road's surface
        float x, z;
        simEngine.pointAt(s, d, x, z);
        glVertex3f(x, simEngine.heightAt(s, d) + y, z);
    };

    const float oldTs = g_ts;
    g_ts = 0.125f;                                       // one asphalt tile every 8 m
    const int CH = 50;
    const int nEnd = simEngine.sprint() ? n - 1 : n;     // a sprint is an open road: no slice after the last
    for (int c0 = 0; c0 < nEnd; c0 += CH) {
        const int c1 = std::min(c0 + CH, nEnd);          // slices c0..c1 (on a circuit slice n is slice 0 again)
        RoadChunk rc;
        rc.surface = glGenLists(2);
        rc.marks = rc.surface + 1;
        simEngine.pointAt(0.5f * float(c0 + c1), 0.0f, rc.x, rc.z);

        // 1. Asphalt
        glNewList(rc.surface, GL_COMPILE);
        glBegin(GL_QUAD_STRIP);
        for (int i = c0; i <= c1; ++i) {
            const sim::Slice &sl = S[at(i)];
            const float g = sl.grade, b = simEngine.bankAt(float(i));
            glNormal3f(-g * sl.tx + b * sl.tz, 1.0f, g * sl.tz + b * sl.tx);   // tilted with slope and banking
            for (float d : {-half, half}) {
                float x, z;
                simEngine.pointAt(float(i), d, x, z);
                tv(x, simEngine.heightAt(float(i), d), z, d, float(i));
            }
        }
        glEnd();
        glEndList();

        glNewList(rc.marks, GL_COMPILE);
        glNormal3f(0, 1, 0);
        // 2. Red & white kerbs, 4 m each
        glBegin(GL_QUADS);
        for (float side : {-1.0f, 1.0f})
            for (int i = c0; i < c1; ++i) {
                glColor3fv(((i / 4) % 2 == 0) ? th.kerbA : th.kerbB);
                P(i, side * half, 0.02f); P(i, side * (half + 0.7f), 0.02f);
                P(i + 1, side * (half + 0.7f), 0.02f); P(i + 1, side * half, 0.02f);
            }
        // 3. Solid white edge lines
        glColor3f(0.95f, 0.95f, 0.95f);
        for (float side : {-1.0f, 1.0f})
            for (int i = c0; i < c1; ++i) {
                const float d = side * (half - 0.40f);
                P(i, d - 0.12f, 0.025f); P(i, d + 0.12f, 0.025f); P(i + 1, d + 0.12f, 0.025f); P(i + 1, d - 0.12f, 0.025f);
            }
        // 4. Dashed lane lines: 4 m line, 4 m gap
        glColor3f(0.96f, 0.94f, 0.88f);
        for (int l = 1; l < simEngine.laneCount(); ++l) {
            const float off = simEngine.laneX(l) - 0.5f * sim::LANE_WIDTH;
            for (int i = (c0 + 7) / 8 * 8; i < c1; i += 8) {
                P(i, off - 0.14f, 0.03f); P(i, off + 0.14f, 0.03f);
                P(i + 4, off + 0.14f, 0.03f); P(i + 4, off - 0.14f, 0.03f);
            }
        }
        glEnd();
        // 5. Guard rails, posts every 12 m
        for (float side : {-1.0f, 1.0f}) {
            glColor3f(0.70f, 0.72f, 0.76f);
            for (int i = (c0 + 11) / 12 * 12; i < c1; i += 12) {
                if (kind(i) || railGap[side > 0][at(i)]) continue;   // tunnel walls and bridge parapets instead
                float x, z;
                simEngine.pointAt(float(i), side * rail, x, z);
                boxAt(x, simEngine.heightAt(float(i), side * rail) + 0.55f, z, 0.10f, 0.55f, 0.10f);
            }
            glColor3f(0.82f, 0.84f, 0.88f);
            for (float ry : {0.50f, 0.95f}) {
                bool open = false;
                for (int i = c0; i <= c1; ++i) {
                    if (kind(i) || railGap[side > 0][at(i)]) {
                        if (open) { glEnd(); open = false; }
                        continue;
                    }
                    if (!open) { glBegin(GL_QUAD_STRIP); open = true; }
                    const sim::Slice &sl = S[at(i)];
                    glNormal3f(-side * sl.tz, 0, -side * sl.tx);  // facing the road
                    float x, z;
                    simEngine.pointAt(float(i), side * rail, x, z);
                    const float h = simEngine.heightAt(float(i), side * rail);
                    glVertex3f(x, h + ry - 0.12f, z);
                    glVertex3f(x, h + ry + 0.12f, z);
                }
                if (open) glEnd();
            }
        }

        // 6. Tunnels: walls on the rail lines (an avalanche gallery has columns on its valley
        //    side), a ceiling with lights, and a portal face at both ends
        auto strip = [&](int k, const std::function<void(int)> &put) {    // a quad strip over slices of kind k
            bool open = false;
            for (int i = c0; i <= c1; ++i) {
                if (kind(i) != k) {
                    if (open) { glEnd(); open = false; }
                    continue;
                }
                if (!open) { glBegin(GL_QUAD_STRIP); open = true; }
                put(i);
            }
            if (open) glEnd();
        };
        auto rightOf = [&](int i, float side) {          // the normal facing out of the road on that side
            const sim::Slice &sl = S[at(i)];
            glNormal3f(side * sl.tz, 0.0f, side * sl.tx);
        };
        glColor3fv(lk.wall);
        for (float side : {-1.0f, 1.0f}) {
            bool openSide = false;                       // the valley side of a gallery
            for (const Stretch &st : stretches)
                if (st.kind == 1 && lk.oneSided && side * st.side < 0.0f) openSide = true;
            if (openSide) {
                for (int i = (c0 + 7) / 8 * 8; i < c1; i += 8) {
                    if (kind(i) != 1) continue;
                    float x, z;
                    simEngine.pointAt(float(i), side * rail, x, z);
                    boxAt(x, simEngine.heightAt(float(i), side * rail) + 0.5f * lk.tunnelH, z, 0.35f, 0.5f * lk.tunnelH, 0.35f);
                }
                continue;
            }
            strip(1, [&](int i) { rightOf(i, -side); P(i, side * rail, 0.0f); P(i, side * rail, lk.tunnelH); });
        }
        strip(1, [&](int i) { glNormal3f(0, -1, 0); P(i, -rail, lk.tunnelH); P(i, rail, lk.tunnelH); });
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 0.90f, 0.62f);
        glBegin(GL_QUADS);
        for (int i = (c0 + 14) / 15 * 15; i < c1; i += 15) {
            if (kind(i) != 1) continue;
            P(i, -0.7f, lk.tunnelH - 0.05f); P(i, 0.7f, lk.tunnelH - 0.05f);
            P(i + 2, 0.7f, lk.tunnelH - 0.05f); P(i + 2, -0.7f, lk.tunnelH - 0.05f);
        }
        glEnd();
        glEnable(GL_LIGHTING);
        for (const Stretch &st : stretches) {
            if (st.kind != 1) continue;
            for (int end : {st.a, st.b}) {
                if (end < c0 || end >= c1) continue;
                const float s = float(end), facing = end == st.a ? -1.0f : 1.0f;
                const sim::Slice &sl = S[at(end)];
                const float W = rail + 16.0f, top = std::max(lk.tunnelH + 0.8f, lk.hill + 1.0f);
                glColor3fv(lk.wall);
                glNormal3f(facing * sl.tx, 0.0f, -facing * sl.tz);
                glBegin(GL_QUADS);
                P(s, -W, -0.6f); P(s, -rail, -0.6f); P(s, -rail, top); P(s, -W, top);
                P(s, rail, -0.6f); P(s, W, -0.6f); P(s, W, top); P(s, rail, top);
                P(s, -rail, lk.tunnelH); P(s, rail, lk.tunnelH); P(s, rail, top); P(s, -rail, top);
                glEnd();
            }
        }

        // 7. Bridges and the raised highway: a deck from rail to rail, parapets instead of
        //    guard rails, and pillars every 28 m (never on a road underneath)
        for (int k : {2, 3}) {
            glColor3fv(lk.deck);
            for (float side : {-1.0f, 1.0f})                 // the deck beside the asphalt
                strip(k, [&](int i) { glNormal3f(0, 1, 0); P(i, side * half, -0.01f); P(i, side * (rail + 0.4f), -0.01f); });
            strip(k, [&](int i) { glNormal3f(0, -1, 0); P(i, -(rail + 0.4f), -1.5f); P(i, rail + 0.4f, -1.5f); });
            glColor3fv(k == 3 ? lk.deck : lk.parapet);
            for (float side : {-1.0f, 1.0f}) {
                strip(k, [&](int i) { rightOf(i, -side); P(i, side * rail, 0.0f); P(i, side * rail, 1.1f); });
                strip(k, [&](int i) { glNormal3f(0, 1, 0); P(i, side * rail, 1.1f); P(i, side * (rail + 0.4f), 1.1f); });
                strip(k, [&](int i) { rightOf(i, side); P(i, side * (rail + 0.4f), -1.5f); P(i, side * (rail + 0.4f), 1.1f); });
            }
        }
        glColor3fv(lk.deck);
        for (int i = (c0 + 27) / 28 * 28; i < c1; i += 28) {
            if (!kind(i) || kind(i) == 1 || cross[at(i)]) continue;
            const Stretch &st = stretches[size_t(stretchOf[at(i)])];
            if (i < st.a + 10 || i > st.b - 10) continue;
            float x, z;
            simEngine.pointAt(float(i), 0.0f, x, z);
            glPushMatrix();
            glTranslatef(x, simEngine.heightAt(float(i), 0.0f) - 1.5f - 30.0f, z);
            glRotatef(-simEngine.headingAt(float(i)) * 180.0f / PI, 0, 1, 0);
            box(2.2f, 30.0f, 1.3f);
            glPopMatrix();
        }
        glEndList();
        roadChunks.push_back(rc);
    }

    // 8. The shortcut: a dirt road at half width, 2 cm under the main road where the two
    //    overlap, with guard rails except where it runs inside the main road's
    if (cut) {
        const auto &CS = simEngine.circuit().shortcut();
        const int m = int(CS.size());
        std::vector<char> inMain[2] = {std::vector<char>(size_t(m), 0), std::vector<char>(size_t(m), 0)};
        for (int i = 0; i < m; ++i)
            for (int side = 0; side < 2; ++side) {
                float x, z, ms, md;
                simEngine.pointAt(float(i), (side ? 1.0f : -1.0f) * cutRail, x, z, 1);
                simEngine.project(x, z, i < m / 2 ? simEngine.shortcutFrom() + float(i) : simEngine.shortcutTo() - float(m - i), ms, md, 0, false);
                inMain[side][size_t(i)] = std::fabs(md) < rail + 1.0f;
            }
        for (int c0 = 0; c0 < m - 1; c0 += CH) {
            const int c1 = std::min(c0 + CH, m - 1);
            RoadChunk rc;
            rc.surface = glGenLists(2);
            rc.marks = rc.surface + 1;
            rc.dirt = true;
            simEngine.pointAt(0.5f * float(c0 + c1), 0.0f, rc.x, rc.z, 1);
            glNewList(rc.surface, GL_COMPILE);
            glBegin(GL_QUAD_STRIP);
            glNormal3f(0, 1, 0);
            for (int i = c0; i <= c1; ++i) {
                const float h = simEngine.roadHalfAt(float(i), 1);
                for (float d : {-h, h}) {
                    float x, z;
                    simEngine.pointAt(float(i), d, x, z, 1);
                    tv(x, simEngine.heightAt(float(i), d, 1) - 0.02f, z, d, float(i));
                }
            }
            glEnd();
            glEndList();
            glNewList(rc.marks, GL_COMPILE);
            for (float side : {-1.0f, 1.0f}) {
                const int sd = side > 0;
                glColor3f(0.55f, 0.42f, 0.30f);          // wooden posts every 6 m
                for (int i = (c0 + 5) / 6 * 6; i < c1; i += 6) {
                    if (inMain[sd][size_t(i)]) continue;
                    float x, z;
                    simEngine.pointAt(float(i), side * cutRail, x, z, 1);
                    boxAt(x, simEngine.heightAt(float(i), side * cutRail, 1) + 0.5f, z, 0.09f, 0.5f, 0.09f);
                }
                glColor3f(0.62f, 0.50f, 0.36f);
                bool open = false;
                for (int i = c0; i <= c1; ++i) {
                    if (inMain[sd][size_t(i)]) {
                        if (open) { glEnd(); open = false; }
                        continue;
                    }
                    if (!open) { glBegin(GL_QUAD_STRIP); open = true; }
                    const sim::Slice &sl = CS[size_t(i)];
                    glNormal3f(-side * sl.tz, 0, -side * sl.tx);
                    float x, z;
                    simEngine.pointAt(float(i), side * cutRail, x, z, 1);
                    const float y = simEngine.heightAt(float(i), side * cutRail, 1);
                    glVertex3f(x, y + 0.65f, z);
                    glVertex3f(x, y + 0.85f, z);
                }
                if (open) glEnd();
            }
            glEndList();
            roadChunks.push_back(rc);
        }
    }
    g_ts = oldTs;
}

void GLWidget::drawRoad()
{
    ensureTrackGeometry();
    std::vector<const RoadChunk *> seen;
    for (const RoadChunk &c : roadChunks)
        if (inView(c.x, c.z, 45.0f)) seen.push_back(&c);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texAsphalt);
    glColor3f(1, 1, 1);
    for (const RoadChunk *c : seen) if (!c->dirt) glCallList(c->surface);
    glBindTexture(GL_TEXTURE_2D, texSand);              // the shortcut: packed dirt
    glColor3f(0.78f, 0.62f, 0.45f);
    for (const RoadChunk *c : seen) if (c->dirt) glCallList(c->surface);
    glDisable(GL_TEXTURE_2D);
    for (const RoadChunk *c : seen) glCallList(c->marks);
}

// A sprint's road ends: a row of red and white barriers across the road and its verges
void GLWidget::drawRoadEnds()
{
    const float len = simEngine.lapLength();
    for (float s : {2.0f, len - 4.0f}) {
        float x, z;
        simEngine.pointAt(s, 0.0f, x, z);
        if (!inView(x, z, 40.0f)) continue;
        glPushMatrix();
        placeOnRoad(s, 0.0f);
        const float w = railOffset() + 3.0f;
        int k = 0;
        for (float d = -w; d <= w + 0.01f; d += 2.0f, ++k) {
            if (k % 2) glColor3f(0.95f, 0.95f, 0.95f); else glColor3f(0.90f, 0.12f, 0.10f);
            boxAt(d, 0.6f, 0.0f, 1.0f, 0.6f, 0.4f);
        }
        glPopMatrix();
    }
}

void GLWidget::drawArch(float s0, const char *word)
{
    // ===================================================================
    // GRAND ARCADE START/FINISH GANTRY & SCENIC FINISH ZONE
    // ===================================================================
    glPushMatrix();
    placeOnRoad(s0, 0.0f);
    float archW = roadHalf() + 2.5f;

    // 1. Heavy dark metallic vertical pillars with industrial lattice trusses
    glColor3f(0.13f, 0.15f, 0.18f);
    // Base pedestals
    boxAt(-archW, 0.6f, 0, 1.25f, 0.6f, 1.25f);
    boxAt( archW, 0.6f, 0, 1.25f, 0.6f, 1.25f);
    // Vertical column uprights
    boxAt(-archW - 0.55f, 4.2f, 0, 0.28f, 4.2f, 0.35f);
    boxAt(-archW + 0.55f, 4.2f, 0, 0.28f, 4.2f, 0.35f);
    boxAt( archW - 0.55f, 4.2f, 0, 0.28f, 4.2f, 0.35f);
    boxAt( archW + 0.55f, 4.2f, 0, 0.28f, 4.2f, 0.35f);

    // Diagonal truss cross-bars between column posts
    glColor3f(0.20f, 0.23f, 0.28f);
    for (float py = 1.5f; py < 8.0f; py += 1.6f) {
        for (float side : {-1.0f, 1.0f}) {
            float px = side * archW;
            boxAt(px, py, 0, 0.60f, 0.10f, 0.25f);
        }
    }

    // 2. Angled truss shoulders
    for (float side : {-1.0f, 1.0f}) {
        glPushMatrix();
        glTranslatef(side * (archW - 1.2f), 7.6f, 0);
        glRotatef(side * -35.0f, 0, 0, 1);
        glColor3f(0.15f, 0.17f, 0.21f);
        boxAt(0, 0, 0, 0.70f, 1.6f, 0.55f);
        glPopMatrix();
    }

    // 3. Spanning horizontal crossbeam double girder
    glColor3f(0.12f, 0.14f, 0.17f);
    boxAt(0, 8.40f, 0, archW + 0.8f, 0.32f, 0.55f); // Lower girder
    boxAt(0, 9.45f, 0, archW + 0.8f, 0.22f, 0.55f); // Upper girder
    // Lattice connector struts between girders
    glColor3f(0.22f, 0.26f, 0.32f);
    for (float gx = -archW; gx <= archW; gx += 2.0f) {
        boxAt(gx, 8.92f, 0, 0.10f, 0.35f, 0.40f);
    }

    // 4. Stadium Floodlight Units atop the gantry crossbeam
    const float floodX[] = {-6.2f, -3.1f, 0.0f, 3.1f, 6.2f};
    for (float fx : floodX) {
        // Swivel mounting bracket
        glColor3f(0.22f, 0.24f, 0.28f);
        boxAt(fx, 9.80f, 0.0f, 0.09f, 0.15f, 0.09f);
        // Floodlight dark metal casing
        glColor3f(0.12f, 0.13f, 0.16f);
        boxAt(fx, 10.25f, 0.0f, 0.46f, 0.34f, 0.22f);
        // Glowing warm stadium lamp face facing +Z
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 0.96f, 0.78f);
        boxAt(fx, 10.25f, 0.23f, 0.40f, 0.28f, 0.02f);
        glEnable(GL_LIGHTING);
    }

    // 5. Large Illuminated FINISH Signboard Box
    glColor3f(0.08f, 0.09f, 0.12f);
    boxAt(0, 7.35f, 0.22f, 7.0f, 1.45f, 0.26f);

    // Pulsing electric cyan neon border framing
    glDisable(GL_LIGHTING);
    float pulse = 0.85f + 0.15f * std::sin(float(simEngine.getTick()) * 0.25f);
    glColor3f(0.15f, 0.90f * pulse, 1.0f * pulse); // Electric Cyan neon
    boxAt(0, 8.75f, 0.49f, 6.95f, 0.06f, 0.02f);  // Top rim
    boxAt(0, 5.95f, 0.49f, 6.95f, 0.06f, 0.02f);  // Bottom rim
    boxAt(-6.95f, 7.35f, 0.49f, 0.06f, 1.40f, 0.02f); // Left rim
    boxAt( 6.95f, 7.35f, 0.49f, 0.06f, 1.40f, 0.02f); // Right rim

    // Flanking Checkered Flag Panels on Left & Right sides of the sign
    for (float side : {-1.0f, 1.0f}) {
        float startChkX = (side < 0) ? -6.7f : 4.1f;
        float chkCellW = 0.65f, chkCellH = 0.55f;
        for (int cr = 0; cr < 4; ++cr) {
            for (int cc = 0; cc < 4; ++cc) {
                if ((cr + cc) % 2 == 0) glColor3f(0.98f, 0.98f, 0.98f);
                else                   glColor3f(0.04f, 0.04f, 0.04f);
                float cx0 = startChkX + cc * chkCellW;
                float cy0 = 6.25f + cr * chkCellH;
                glBegin(GL_QUADS);
                glVertex3f(cx0, cy0, 0.49f);
                glVertex3f(cx0 + chkCellW, cy0, 0.49f);
                glVertex3f(cx0 + chkCellW, cy0 + chkCellH, 0.49f);
                glVertex3f(cx0, cy0 + chkCellH, 0.49f);
                glEnd();
            }
        }
    }

    // Top Header Badge: "GRAND PRIX"
    glColor3f(0.10f, 0.11f, 0.14f);
    boxAt(0, 8.88f, 0.26f, 1.9f, 0.22f, 0.06f);
    glColor3f(1.0f, 0.88f, 0.25f);
    drawText3D("GRAND PRIX", 0, 8.78f, 0.35f, 0.038f);

    // Bright bold glowing golden-yellow "FINISH" block letters
    glColor3f(1.0f, 0.94f, 0.18f); // Golden yellow neon
    drawText3D(word, 0, 6.65f, 0.51f, 0.20f);

    // 6. Dual Lower Digital Sub-Panels (Hanging Banners)
    // Left hanging panel: "WINNER!"
    glColor3f(0.20f, 0.22f, 0.26f);
    boxAt(-4.3f, 5.70f, 0.22f, 0.05f, 0.25f, 0.05f); // Hanger bracket
    boxAt(-2.3f, 5.70f, 0.22f, 0.05f, 0.25f, 0.05f);
    glColor3f(0.08f, 0.09f, 0.12f);
    boxAt(-3.3f, 5.15f, 0.22f, 2.2f, 0.38f, 0.12f);
    // Glowing amber border
    glColor3f(1.0f * pulse, 0.85f * pulse, 0.15f);
    boxAt(-3.3f, 5.50f, 0.35f, 2.15f, 0.04f, 0.02f);
    boxAt(-3.3f, 4.80f, 0.35f, 2.15f, 0.04f, 0.02f);
    boxAt(-5.45f, 5.15f, 0.35f, 0.04f, 0.35f, 0.02f);
    boxAt(-1.15f, 5.15f, 0.35f, 0.04f, 0.35f, 0.02f);
    glColor3f(1.0f, 0.88f, 0.15f);
    const bool start = std::strcmp(word, "START") == 0;   // a sprint's start gantry wishes luck instead
    drawText3D(start ? "GO GO GO" : "WINNER!", -3.3f, 5.02f, 0.36f, start ? 0.052f : 0.068f);

    // Right hanging panel: "CONGRATULATIONS!"
    glColor3f(0.20f, 0.22f, 0.26f);
    boxAt( 2.0f, 5.70f, 0.22f, 0.05f, 0.25f, 0.05f); // Hanger bracket
    boxAt( 4.6f, 5.70f, 0.22f, 0.05f, 0.25f, 0.05f);
    glColor3f(0.08f, 0.09f, 0.12f);
    boxAt( 3.3f, 5.15f, 0.22f, 2.7f, 0.38f, 0.12f);
    // Glowing cyan border
    glColor3f(0.20f, 0.90f * pulse, 1.0f * pulse);
    boxAt( 3.3f, 5.50f, 0.35f, 2.65f, 0.04f, 0.02f);
    boxAt( 3.3f, 4.80f, 0.35f, 2.65f, 0.04f, 0.02f);
    boxAt( 0.65f, 5.15f, 0.35f, 0.04f, 0.35f, 0.02f);
    boxAt( 5.95f, 5.15f, 0.35f, 0.04f, 0.35f, 0.02f);
    glColor3f(0.95f, 0.98f, 1.0f);
    drawText3D(start ? "GOOD LUCK!" : "CONGRATULATIONS!", 3.3f, 5.06f, 0.36f, start ? 0.050f : 0.036f);

    // Dark electrical conduits connecting to side pillars
    glColor3f(0.12f, 0.12f, 0.14f);
    boxAt(-6.2f, 5.15f, 0.18f, 1.0f, 0.05f, 0.05f);
    boxAt( 6.8f, 5.15f, 0.18f, 0.9f, 0.05f, 0.05f);

    glEnable(GL_LIGHTING);

    // 7. Multi-row high-contrast checkered finish line on the asphalt
    const int n = 20;
    float sq = (roadHalf() * 2.0f) / n;
    for (int i = 0; i < n; ++i) {
        for (int row = 0; row < 4; ++row) {
            if ((i + row) % 2) glColor3f(0.03f, 0.03f, 0.03f);
            else               glColor3f(0.98f, 0.98f, 0.98f);
            float x0 = -roadHalf() + i * sq, z0 = -row * sq * 0.9f;
            glBegin(GL_QUADS);
            glVertex3f(x0, 0.04f, z0);
            glVertex3f(x0 + sq, 0.04f, z0);
            glVertex3f(x0 + sq, 0.04f, z0 - sq * 0.9f);
            glVertex3f(x0, 0.04f, z0 - sq * 0.9f);
            glEnd();
        }
    }
    glPopMatrix();

    // The rest follows the road's bends: each piece stands at its own place, lz metres
    // before the line (negative: after it) and lx metres right of the centre line. A piece
    // that would stand on another part of the circuit is left out.
    auto at = [&](float lz, float lx, const std::function<void()> &draw) {
        float x, z;
        simEngine.pointAt(s0 - lz, lx, x, z);
        if (simEngine.roadDistance(x, z, std::fabs(lx)) < std::fabs(lx) - 3.0f) return;
        glPushMatrix();
        placeOnRoad(s0 - lz, lx);
        draw();
        glPopMatrix();
    };
    auto rv = [&](float lz, float lx, float y) {               // y: above the road's surface
        float x, z;
        simEngine.pointAt(s0 - lz, lx, x, z);
        glVertex3f(x, simEngine.heightAt(s0 - lz, lx) + y, z);
    };

    // Dark tire rubber skid trails leading up to the checkered line
    glColor4f(0.02f, 0.02f, 0.02f, 0.65f);
    for (int l = 0; l < simEngine.laneCount(); ++l) {
        for (float sd : {-0.8f, 0.8f}) {
            const float lx = simEngine.laneX(l) + sd;
            glBegin(GL_QUAD_STRIP);
            for (float lz = 24.0f; lz >= -6.0f; lz -= 3.0f) {
                rv(lz, lx - 0.22f, 0.045f);
                rv(lz, lx + 0.22f, 0.045f);
            }
            glEnd();
        }
    }

    // Red & white racing rumble curbs (curbing) flanking finish straight
    glBegin(GL_QUADS);
    for (float side : {-1.0f, 1.0f}) {
        for (float rz = 45.0f; rz >= -45.0f; rz -= 3.0f) {
            int curbStep = int(std::floor((rz + 100.0f) / 3.0f));
            if (curbStep % 2 == 0) glColor3f(0.92f, 0.16f, 0.16f);
            else                   glColor3f(0.98f, 0.98f, 0.98f);
            float rx0 = side * roadHalf();
            float rx1 = side * (roadHalf() + 0.95f);
            rv(rz, rx0, 0.035f); rv(rz, rx1, 0.035f); rv(rz - 3.0f, rx1, 0.035f); rv(rz - 3.0f, rx0, 0.035f);
        }
    }
    glEnd();

    // 8. Roadside Sponsor Billboards
    for (float sbz : { 28.0f, 62.0f, 96.0f }) {
        for (float side : { -1.0f, 1.0f }) {
            at(sbz, side * (railOffset() + 3.2f), [&]() {
                glColor3f(0.25f, 0.25f, 0.28f);
                boxAt(0, 1.5f, 0, 0.15f, 1.5f, 0.15f);
                if (int(sbz) % 2 == 0) glColor3f(0.10f, 0.25f, 0.70f); // Blue
                else                   glColor3f(0.85f, 0.15f, 0.15f); // Red
                boxAt(0, 3.2f, 0, 0.12f, 1.0f, 4.5f);
                glDisable(GL_LIGHTING);
                glColor3f(1.0f, 0.9f, 0.2f);
                glTranslatef(-side * 0.15f, 3.0f, 0);
                glRotatef(side > 0 ? -90.0f : 90.0f, 0, 1, 0);
                drawText3D(int(sbz) % 2 == 0 ? "ARCADE RACING" : "TURBO SPEED", 0, 0, 0, 0.14f);
                glEnable(GL_LIGHTING);
            });
        }
    }

    // 9. LUSH SCENIC NATURAL LANDSCAPE (Spectators removed, replaced with coastal & mountain scenery)
    // (only on the green maps; desert, snow and city keep their own scenery to the end)
    const sim::MapTheme finTheme = simEngine.mapDef().theme;
    if (finTheme == sim::MapTheme::Countryside || finTheme == sim::MapTheme::Coast) {
        // A. Tropical Palm Trees along both sides of track
        const struct PalmPos { float x, z, scale, rot; } palms[] = {
            { -roadHalf() - 4.5f,  20.0f, 1.05f,   35.0f },
            { -roadHalf() - 7.2f,  45.0f, 0.95f,  120.0f },
            { -roadHalf() - 5.0f,  75.0f, 1.15f,  210.0f },
            { -roadHalf() - 4.2f, -18.0f, 1.00f,   75.0f },
            { -roadHalf() - 6.8f, -48.0f, 1.10f,  160.0f },
            { -roadHalf() - 8.5f, -80.0f, 1.20f,  290.0f },

            {  roadHalf() + 4.5f,  18.0f, 1.00f,  -40.0f },
            {  roadHalf() + 6.8f,  42.0f, 1.10f, -130.0f },
            {  roadHalf() + 5.2f,  70.0f, 0.90f,  -85.0f },
            {  roadHalf() + 4.8f, -22.0f, 1.05f,  -60.0f },
            {  roadHalf() + 7.5f, -54.0f, 1.15f, -190.0f },
            {  roadHalf() + 9.0f, -85.0f, 1.25f,  -20.0f }
        };
        for (const auto &p : palms) {
            at(p.z, p.x, [&]() {
                glRotatef(p.rot, 0, 1, 0);
                glScalef(p.scale, p.scale, p.scale);
                drawPalmTree();
            });
        }

        // B. Rolling Lush Green Scenery Hills on both flanks
        for (float hz : { 80.0f, 40.0f, 0.0f, -40.0f, -80.0f }) {
            // Left flank rolling hills
            at(hz, -roadHalf() - 18.0f, [&]() { glColor3f(0.16f, 0.54f, 0.20f); boxAt(0, 3.5f, 0, 10.0f, 3.5f, 18.0f); });
            at(hz + 5.0f, -roadHalf() - 24.0f, [&]() { glColor3f(0.14f, 0.48f, 0.18f); boxAt(0, 6.0f, 0, 8.0f, 6.0f, 14.0f); });
            // Right flank rolling coastal mounds
            at(hz, roadHalf() + 17.0f, [&]() { glColor3f(0.18f, 0.56f, 0.22f); boxAt(0, 3.0f, 0, 9.0f, 3.0f, 18.0f); });
        }

        // C. Coastal Rocky Cliff Formations overlooking the sea (Right side)
        for (float rz : { 30.0f, -10.0f, -50.0f, -90.0f }) {
            at(rz, roadHalf() + 26.0f, [&]() { glColor3f(0.42f, 0.40f, 0.38f); boxAt(0, 4.5f, 0, 7.0f, 4.5f, 16.0f); });
            at(rz - 6.0f, roadHalf() + 29.0f, [&]() { glColor3f(0.36f, 0.34f, 0.32f); boxAt(0, 8.0f, 0, 6.0f, 8.0f, 12.0f); });
        }

        // D. Scenic Lighthouse on the coastal cliff in the distance past the finish line
        at(-95.0f, roadHalf() + 32.0f, [&]() { drawLighthouse(); });
    }

    // E. Red & White Chevron Direction Signs past the finish line
    for (float cz : { -32.0f, -62.0f, -92.0f }) {
        at(cz, roadHalf() + 2.2f, [&]() {
            // Sign post
            glColor3f(0.30f, 0.32f, 0.35f);
            boxAt(0, 1.3f, 0, 0.08f, 1.3f, 0.08f);
            // Red board
            glColor3f(0.85f, 0.15f, 0.15f);
            boxAt(0, 2.2f, 0, 0.06f, 0.55f, 1.4f);
            // White chevron arrows ">>>"
            glDisable(GL_LIGHTING);
            glColor3f(0.98f, 0.98f, 0.98f);
            glTranslatef(-0.08f, 2.05f, 0);
            glRotatef(-90.0f, 0, 1, 0);
            drawText3D(">>>", 0, 0, 0, 0.06f);
            glEnable(GL_LIGHTING);
        });
    }
}

// ===========================================================================
//  SCENERY PROPS
// ===========================================================================
void GLWidget::drawProps()
{
    const auto &props = simEngine.getProps();
    if (propBaseKey != groundKey || propBase.size() != props.size()) {
        // the lowest and highest ground under each prop (corners of its footprint)
        propBase.assign(props.size(), PropBase());
        for (size_t i = 0; i < props.size(); ++i) {
            const sim::Prop &p = props[i];
            const float r = p.type == sim::Prop::House || p.type == sim::Prop::Building ? 6.0f * std::max(1.0f, p.type == sim::Prop::House ? p.scale : 1.0f)
                          : p.type == sim::Prop::Rock ? (p.scale > 3.0f ? 4.2f : 1.4f) * p.scale
                          : p.type == sim::Prop::Lighthouse ? 3.0f : 0.0f;
            float lo = groundAt(p.x, p.z), hi = lo;
            if (r > 0.0f)
                for (float ox : {-r, r})
                    for (float oz : {-r, r}) {
                        const float h = groundAt(p.x + ox, p.z + oz);
                        lo = std::min(lo, h);
                        hi = std::max(hi, h);
                    }
            propBase[i] = {lo, hi};
        }
        propBaseKey = groundKey;
    }
    for (size_t pi = 0; pi < props.size(); ++pi) {
        const sim::Prop &p = props[pi];
        if (GFX_PROPS[gfxLevel] > 1 && pi % size_t(GFX_PROPS[gfxLevel]) != 0
            && p.type != sim::Prop::Lighthouse && p.type != sim::Prop::Lamp && p.type != sim::Prop::Torch) continue;
        if (!inView(p.x, p.z, p.type == sim::Prop::Rock ? 6.0f * p.scale : 12.0f)) continue;
        const bool built = p.type == sim::Prop::House || p.type == sim::Prop::Building;
        const PropBase &pb = propBase[pi];
        glPushMatrix();
        glTranslatef(p.x, built ? pb.top : pb.base - 0.15f, p.z);   // houses on their highest corner
        glRotatef(p.rot, 0, 1, 0);
        if (built && pb.top - pb.base > 0.05f) {          // a stone foundation down to the lowest
            const float hx = p.type == sim::Prop::House ? 5.6f * p.scale : 6.2f;
            const float hz = p.type == sim::Prop::House ? 4.1f * p.scale : 5.2f;
            const float depth = pb.top - pb.base + 0.4f;
            glColor3f(0.52f, 0.50f, 0.47f);
            boxAt(0, 0.05f - depth * 0.5f, 0, hx, depth * 0.5f, hz);
        }
        if (p.type == sim::Prop::Building) {           // scale = height only, footprint stays
            drawBuilding(p.scale, p.palette, int(std::fabs(p.x) + std::fabs(p.z)));
            glPopMatrix();
            continue;
        }
        glScalef(p.scale, p.scale, p.scale);
        // the models that never change are recorded once and replayed (much cheaper)
        const int key = int(p.type) * 100;
        switch (p.type) {
        case sim::Prop::House:      drawCachedModel(key + p.palette, [&]() { drawHouse(p.palette); }); break;
        case sim::Prop::Pine:       drawCachedModel(key, [&]() { drawPine(); }); break;
        case sim::Prop::RoundTree:  drawCachedModel(key, [&]() { drawRoundTree(); }); break;
        case sim::Prop::Lamp:       drawLamp(); break;
        case sim::Prop::Torch:      drawTorch(p.palette, float(simEngine.getTick()) * 0.08f); break;
        case sim::Prop::Cactus:     drawCachedModel(key, [&]() { drawCactus(); }); break;
        case sim::Prop::Rock: {
            const bool mesa = p.scale > 3.0f;
            drawCachedModel(key + p.palette * 2 + (mesa ? 1 : 0), [&]() { drawRock(p.palette, mesa); });
            break;
        }
        case sim::Prop::SnowPine:   drawCachedModel(key, [&]() { drawPine(true); }); break;
        case sim::Prop::Palm:       drawCachedModel(key, [&]() { drawPalmTree(); }); break;
        case sim::Prop::Lighthouse: drawCachedModel(key, [&]() { drawLighthouse(); }); break;
        case sim::Prop::Building:   break;
        }
        glPopMatrix();
    }
}

void GLWidget::drawHouse(int palette)
{
    const float pals[5][3] = {{0.92f, 0.88f, 0.80f}, {0.85f, 0.65f, 0.55f},
                              {0.75f, 0.80f, 0.75f}, {0.90f, 0.78f, 0.62f},
                              {0.58f, 0.40f, 0.25f}};                        // 4: wooden chalet
    const float roofs[5][3] = {{0.65f, 0.22f, 0.18f}, {0.30f, 0.35f, 0.40f},
                               {0.55f, 0.30f, 0.20f}, {0.25f, 0.45f, 0.35f},
                               {0.95f, 0.96f, 0.99f}};                       // 4: snow on the roof

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texPlaster);
    g_ts = 0.35f;
    glColor3fv(pals[palette]);
    boxAt(0, 3.2f, 0, 5.5f, 3.2f, 4.0f);

    glBindTexture(GL_TEXTURE_2D, texRoof);
    g_ts = 0.5f;
    glColor3fv(roofs[palette]);
    V3 c = {0, 8.0f, 0};
    V3 e0 = {-6.2f, 6.4f,  4.6f}, e1 = { 6.2f, 6.4f,  4.6f};
    V3 e2 = { 6.2f, 6.4f, -4.6f}, e3 = {-6.2f, 6.4f, -4.6f};
    V3 r0 = {-5.5f, 9.8f,  0.0f}, r1 = { 5.5f, 9.8f,  0.0f};

    glBegin(GL_QUADS);
    quadN(e0, e1, r1, r0, c);
    quadN(e2, e3, r0, r1, c);
    glEnd();
    glBegin(GL_TRIANGLES);
    triN(e3, e0, r0, c);
    triN(e1, e2, r1, c);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glColor3f(0.35f, 0.22f, 0.12f);
    boxAt(0, 1.8f, 4.05f, 0.9f, 1.8f, 0.05f);
    glColor3f(0.95f, 0.90f, 0.65f);
    for (float wx : {-3.2f, 3.2f}) {
        boxAt(wx, 3.4f, 4.05f, 0.9f, 0.9f, 0.05f);
        boxAt(wx, 3.4f, -4.05f, 0.9f, 0.9f, 0.05f);
    }
    glColor3f(0.55f, 0.52f, 0.50f);
    boxAt(3.5f, 8.5f, -1.5f, 0.6f, 2.2f, 0.6f);
    g_ts = 0.25f;
}

void GLWidget::drawPine(bool snow)
{
    glColor3f(0.32f, 0.22f, 0.14f);
    glPushMatrix();
    glTranslatef(0, 0, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 0.45, 0.25, 4.5, 8, 1);
    glPopMatrix();

    const float tiers[4][3] = {{3.2f, 3.2f, 0.72f}, {5.6f, 2.6f, 0.82f},
                               {7.8f, 2.0f, 0.92f}, {9.6f, 1.3f, 1.05f}};
    for (int i = 0; i < 4; ++i) {
        float y = tiers[i][0], r = tiers[i][1], f = tiers[i][2];
        glColor3f(0.12f * f, 0.38f * f, 0.18f * f);
        if (snow) glColor3f(0.10f * f, 0.28f * f, 0.20f * f);
        glPushMatrix();
        glTranslatef(0, y, 0);
        glRotatef(-90, 1, 0, 0);
        gluCylinder(quad, r, 0.0, 2.6, 8, 1);
        gluDisk(quad, 0.0, r, 8, 1);
        if (snow) {                                        // snow lying on each tier
            glColor3f(0.93f, 0.95f, 0.99f);
            glTranslatef(0, 0, 1.0f);
            gluCylinder(quad, r * 0.66f, 0.0, 1.65, 8, 1);
        }
        glPopMatrix();
    }
}

void GLWidget::drawCactus()
{
    // Saguaro: trunk with two arms, rounded tops
    auto limb = [&](float x, float y, float len, float r) {
        glPushMatrix(); glTranslatef(x, y, 0); glRotatef(-90, 1, 0, 0);
        gluCylinder(quad, r, r * 0.92f, len, 10, 1); glPopMatrix();
        glPushMatrix(); glTranslatef(x, y + len, 0); gluSphere(quad, r * 0.92f, 10, 6); glPopMatrix();
    };
    glColor3f(0.24f, 0.47f, 0.25f);
    limb(0.0f, 0.0f, 4.3f, 0.38f);
    for (float side : {1.0f, -1.0f}) {
        float y = (side > 0) ? 1.7f : 2.5f, len = (side > 0) ? 0.85f : 0.65f;
        glPushMatrix(); glTranslatef(0, y, 0); glRotatef(side * 90.0f, 0, 1, 0);
        gluCylinder(quad, 0.24, 0.24, len, 8, 1); glPopMatrix();
        glColor3f(0.22f, 0.44f, 0.24f);
        limb(side * len, y - 0.05f, (side > 0) ? 1.6f : 1.2f, 0.25f);
        glColor3f(0.24f, 0.47f, 0.25f);
    }
}

void GLWidget::drawRock(int palette, bool mesa)
{
    const float pal[4][3] = {{0.62f, 0.34f, 0.22f}, {0.45f, 0.44f, 0.42f},
                             {0.74f, 0.58f, 0.40f}, {0.46f, 0.47f, 0.50f}};
    if (mesa) {
        // flat-topped desert butte with layered rock bands
        const float bands[3][3] = {{0.66f, 0.34f, 0.22f}, {0.78f, 0.46f, 0.30f}, {0.70f, 0.40f, 0.26f}};
        float y = 0.0f, r = 6.0f;
        for (int b = 0; b < 3; ++b) {
            glColor3fv(bands[b]);
            glPushMatrix(); glTranslatef(0, y, 0); glRotatef(-90, 1, 0, 0);
            gluCylinder(quad, r, r - 0.4f, 2.6, 14, 1); glPopMatrix();
            y += 2.6f; r -= 0.4f;
        }
        glPushMatrix(); glTranslatef(0, y, 0); glRotatef(-90, 1, 0, 0); gluDisk(quad, 0.0, r, 14, 1); glPopMatrix();
        return;
    }
    glColor3fv(pal[palette & 3]);
    glPushMatrix(); glTranslatef(0, 0.35f, 0); glScalef(1.6f, 0.9f, 1.25f); gluSphere(quad, 1.0, 8, 6); glPopMatrix();
    glPushMatrix(); glTranslatef(1.1f, 0.2f, 0.5f); glScalef(0.8f, 0.6f, 0.7f); gluSphere(quad, 1.0, 7, 5); glPopMatrix();
    if (palette == 3) {                                    // snow cap
        glColor3f(0.94f, 0.96f, 0.99f);
        glPushMatrix(); glTranslatef(0, 0.8f, 0); glScalef(1.3f, 0.42f, 1.0f); gluSphere(quad, 1.0, 8, 5); glPopMatrix();
    }
}

void GLWidget::drawBuilding(float heightScale, int palette, int seed)
{
    // City tower: facade + roof block + a grid of lit and dark windows (+ the odd neon strip)
    const float pal[5][3] = {{0.22f, 0.24f, 0.30f}, {0.30f, 0.26f, 0.24f}, {0.18f, 0.22f, 0.26f},
                             {0.26f, 0.28f, 0.34f}, {0.32f, 0.30f, 0.36f}};
    const float w = 4.5f + float(seed % 3) * 0.8f, d = 5.0f, h = 10.0f + 16.0f * heightScale;
    const float *col = pal[palette % 5];
    glColor3fv(col);
    boxAt(0, h * 0.5f, 0, w, h * 0.5f, d);
    glColor3f(col[0] * 0.7f, col[1] * 0.7f, col[2] * 0.7f);
    boxAt(0, h + 0.6f, 0, w * 0.55f, 0.6f, d * 0.55f);

    glDisable(GL_LIGHTING);
    glBegin(GL_QUADS);
    int wi = 0;
    for (float y = 2.6f; y < h - 1.2f; y += 2.6f) {
        for (int face = 0; face < 4; ++face) {
            float span = (face < 2) ? d : w;
            for (float t = -span + 1.2f; t < span - 0.8f; t += 2.0f, ++wi) {
                if (hash2(seed, wi, 101) < 0.35f) continue;   // dark window: facade shows through
                if (hash2(seed, wi, 102) < 0.75f) glColor3f(1.0f, 0.82f, 0.45f);
                else                              glColor3f(0.55f, 0.78f, 1.0f);
                float x0, x1, z0, z1;
                if (face < 2) { float fx = (face == 0 ? w : -w) * 1.002f; x0 = x1 = fx; z0 = t; z1 = t + 1.0f; }
                else          { float fz = (face == 2 ? d : -d) * 1.002f; z0 = z1 = fz; x0 = t; x1 = t + 1.0f; }
                glVertex3f(x0, y, z0); glVertex3f(x1, y, z1); glVertex3f(x1, y + 1.3f, z1); glVertex3f(x0, y + 1.3f, z0);
            }
        }
    }
    glEnd();
    if (hash2(seed, 7, 103) > 0.6f) {                          // vertical neon sign on one corner
        bool pink = hash2(seed, 8, 103) > 0.5f;
        glColor3f(pink ? 1.0f : 0.2f, pink ? 0.25f : 0.95f, pink ? 0.75f : 1.0f);
        boxAt(w + 0.15f, h * 0.45f, d - 0.6f, 0.12f, h * 0.25f, 0.35f);
    }
    glEnable(GL_LIGHTING);
}

void GLWidget::drawSea()
{
    // Beach, surf and ocean east of the coastal circuit: the shore runs north-south
    const float sx = simEngine.seaLineX();
    if (viewX + VIEW_DIST < sx - 50.0f) return;          // too far inland to see it
    const float seg = 8.0f;
    const float startZ = std::floor((viewZ - VIEW_DIST - 20.0f) / seg) * seg, endZ = viewZ + VIEW_DIST + 20.0f;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    struct Band { float x0, x1; float c0[4], c1[4]; float y; };
    const float wave = 0.6f * std::sin(simEngine.getTick() * 0.03f);
    const Band bands[4] = {
        {sx - 50.0f, sx - 8.0f, {0.86f, 0.78f, 0.58f, 1.0f}, {0.80f, 0.72f, 0.52f, 1.0f}, 0.32f},
        {sx - 8.0f + wave, sx - 6.8f + wave, {0.95f, 0.97f, 1.0f, 0.9f}, {0.75f, 0.92f, 0.95f, 0.9f}, 0.34f},
        {sx - 7.0f, sx + 22.0f, {0.24f, 0.66f, 0.72f, 1.0f}, {0.08f, 0.36f, 0.55f, 1.0f}, 0.0f},
        {sx + 22.0f, sx + 700.0f, {0.08f, 0.36f, 0.55f, 1.0f}, {0.04f, 0.22f, 0.42f, 1.0f}, 0.0f},
    };
    for (const Band &b : bands) {
        glBegin(GL_QUAD_STRIP);
        for (float z = startZ; z <= endZ; z += seg) {
            glColor4fv(b.c0); glVertex3f(b.x0, b.y, z);
            glColor4fv(b.c1); glVertex3f(b.x1, b.y, z);
        }
        glEnd();
    }
    glEnable(GL_LIGHTING);
}

void GLWidget::drawRoundTree()
{
    glColor3f(0.35f, 0.24f, 0.16f);
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 0.55, 0.35, 4.0, 8, 1);
    glPopMatrix();

    const float b[4][4] = {{0, 5.2f, 0, 2.6f}, {-1.3f, 4.4f, 0.4f, 1.9f},
                           {1.4f, 4.6f, -0.3f, 1.8f}, {0.2f, 6.2f, -0.4f, 2.0f}};
    const float f[4] = {0.85f, 0.72f, 0.78f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        glColor3f(0.22f * f[i], 0.52f * f[i], 0.18f * f[i]);
        glPushMatrix();
        glTranslatef(b[i][0], b[i][1], b[i][2]);
        gluSphere(quad, b[i][3], 9, 7);
        glPopMatrix();
    }
}

void GLWidget::drawPalmTree()
{
    glDisable(GL_TEXTURE_2D);

    // 1. Segmented curved trunk with natural bark ridges
    float curX = 0.0f, curY = 0.0f, curZ = 0.0f;
    float r = 0.50f;
    for (int i = 0; i < 7; ++i) {
        float h = 1.45f;
        float nextR = r * 0.92f;
        if (i % 2 == 0) glColor3f(0.46f, 0.32f, 0.20f);
        else            glColor3f(0.38f, 0.26f, 0.16f);

        glPushMatrix();
        glTranslatef(curX, curY, curZ);
        glRotatef(-90.0f, 1, 0, 0);
        gluCylinder(quad, r, nextR, h, 8, 1);
        gluDisk(quad, 0, r, 8, 1);
        glPopMatrix();

        curY += h;
        curX += 0.14f;
        curZ += 0.04f;
        r = nextR;
    }

    // 2. Coconuts cluster beneath tree crown
    glColor3f(0.30f, 0.18f, 0.10f);
    for (int c = 0; c < 4; ++c) {
        float ang = float(c) * (2.0f * PI / 4.0f) + 0.35f;
        glPushMatrix();
        glTranslatef(curX + std::cos(ang) * 0.35f, curY - 0.20f, curZ + std::sin(ang) * 0.35f);
        gluSphere(quad, 0.26, 6, 6);
        glPopMatrix();
    }

    // 3. Radiating lush palm fronds (drooping green leaf blades)
    const int leafCount = 8;
    for (int l = 0; l < leafCount; ++l) {
        float baseAng = float(l) * (2.0f * PI / float(leafCount));

        glPushMatrix();
        glTranslatef(curX, curY, curZ);
        glRotatef(baseAng * 180.0f / PI, 0, 1, 0);
        glRotatef(24.0f + 6.0f * std::sin(float(l)), 1, 0, 0);

        float leafLen = 4.4f;
        float leafW = 0.85f;

        glBegin(GL_TRIANGLE_FAN);
        glColor3f(0.22f, 0.60f, 0.20f);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glColor3f(0.14f, 0.70f, 0.24f);
        glVertex3f(-leafW * 0.5f, -0.35f, leafLen * 0.35f);
        glColor3f(0.12f, 0.62f, 0.19f);
        glVertex3f(-leafW * 0.55f, -0.90f, leafLen * 0.70f);
        glColor3f(0.09f, 0.52f, 0.16f);
        glVertex3f(0.0f, -1.50f, leafLen);
        glColor3f(0.13f, 0.64f, 0.20f);
        glVertex3f(leafW * 0.55f, -0.90f, leafLen * 0.70f);
        glColor3f(0.16f, 0.72f, 0.26f);
        glVertex3f(leafW * 0.5f, -0.35f, leafLen * 0.35f);
        glEnd();

        glPopMatrix();
    }
}

void GLWidget::drawLighthouse()
{
    glDisable(GL_TEXTURE_2D);

    // Rocky foundation base
    glColor3f(0.38f, 0.36f, 0.34f);
    boxAt(0, 4.0f, 0, 8.0f, 4.0f, 8.0f);
    boxAt(0, 7.5f, 0, 6.5f, 3.5f, 6.5f);

    // Tapered tower with alternating red & white rings
    float baseH = 11.0f;
    float botR = 3.6f;
    float topR = 2.1f;
    const int rings = 6;
    float ringH = 3.0f;
    for (int r = 0; r < rings; ++r) {
        float rY = baseH + r * ringH;
        float rBot = botR - (botR - topR) * (float(r) / float(rings));
        float rTop = botR - (botR - topR) * (float(r + 1) / float(rings));
        if (r % 2 == 0) glColor3f(0.96f, 0.96f, 0.96f);
        else            glColor3f(0.88f, 0.16f, 0.16f);
        glPushMatrix();
        glTranslatef(0, rY, 0);
        glRotatef(-90, 1, 0, 0);
        gluCylinder(quad, rBot, rTop, ringH, 12, 1);
        gluDisk(quad, 0, rBot, 12, 1);
        glPopMatrix();
    }

    float totalH = baseH + rings * ringH;
    // Observation deck gallery & railing
    glColor3f(0.18f, 0.20f, 0.24f);
    glPushMatrix();
    glTranslatef(0, totalH, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 2.7, 2.7, 0.5, 12, 1);
    gluDisk(quad, 0, 2.7, 12, 1);
    glPopMatrix();

    // Lantern room with glowing yellow beacon
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 0.95f, 0.40f, 0.95f);
    glPushMatrix();
    glTranslatef(0, totalH + 0.5f, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 1.7, 1.7, 2.0, 12, 1);
    glPopMatrix();

    // Conical roof cap
    glColor3f(0.12f, 0.14f, 0.16f);
    glPushMatrix();
    glTranslatef(0, totalH + 2.5f, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 1.9, 0.0, 1.6, 12, 1);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void GLWidget::drawLamp()
{
    glColor3f(0.22f, 0.24f, 0.26f);
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 0.16, 0.10, 6.5, 8, 1);
    glPopMatrix();
    boxAt(0.6f, 6.4f, 0, 0.6f, 0.08f, 0.08f);

    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.95f, 0.65f);
    boxAt(1.1f, 6.2f, 0, 0.22f, 0.18f, 0.22f);
    if (nightMode) {                                   // pool of warm light on the road below
        carmodel::beginGlow();
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 0.82f, 0.50f, 0.30f);
        glVertex3f(2.0f, 0.06f, 0.0f);
        glColor4f(1.0f, 0.82f, 0.50f, 0.0f);
        for (int i = 0; i <= 20; ++i) {
            float a = i * 2.0f * PI / 20.0f;
            glVertex3f(2.0f + std::cos(a) * 5.5f, 0.06f, std::sin(a) * 5.5f);
        }
        glEnd();
        carmodel::endGlow();
    }
    glEnable(GL_LIGHTING);
}

void GLWidget::drawTorch(int palette, float phase)
{
    glColor3f(0.25f, 0.25f, 0.28f);
    boxAt(0, 1.5f, 0, 0.35f, 1.5f, 0.35f);
    boxAt(0, 3.1f, 0, 0.7f, 0.15f, 0.7f);

    glDisable(GL_LIGHTING);
    float f = 0.85f + 0.30f * std::sin(phase + (palette ? 1.5f : 0.0f));
    if (palette == 0) glColor3f(1.0f, 0.45f * f, 0.1f);
    else              glColor3f(0.2f, 0.75f * f, 1.0f);
    glPushMatrix();
    glTranslatef(0, 3.2f, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 0.28, 0.0, 0.8 * f, 8, 1);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

// ===========================================================================
//  OBSTACLES
// ===========================================================================
void GLWidget::drawObstacles()
{
    for (const sim::Obstacle &o : simEngine.getObstacles()) {
        if (!inView(o.x, o.z, 3.0f) || !simEngine.present(o.fromLap)) continue;
        glPushMatrix();
        // o.y is the height above the road; a knocked-over one may lie away from its place
        const float base = o.knocked ? simEngine.heightNear(o.x, o.z, o.s) : simEngine.heightAt(o.s, o.d);
        glTranslatef(o.x, base + o.y, o.z);
        glRotatef(o.rot, 0, 1, 0);
        if (o.block() && !o.knocked)                      // lies across a banked road
            glRotatef(-std::asin(simEngine.bankAt(o.s)) * 180.0f / PI, 0, 0, 1);

        if (o.type == sim::Obstacle::Mine) {
            if (o.knocked) { glPopMatrix(); continue; }  // (it went off: it is back a little later)
            // red cross on the road, the black mine, its blinking light
            glDisable(GL_LIGHTING);
            glColor3f(0.85f, 0.08f, 0.06f);
            for (float a : {45.0f, -45.0f}) {
                glPushMatrix();
                glRotatef(a, 0, 1, 0);
                boxAt(0, 0.04f, 0, 2.6f, 0.02f, 0.28f);
                glPopMatrix();
            }
            glColor3f(0.05f, 0.05f, 0.06f);
            boxAt(0, 0.22f, 0, 0.95f, 0.18f, 0.95f);
            const bool blink = simEngine.getTick() % 30 < 15;
            glColor3f(blink ? 1.0f : 0.4f, blink ? 0.12f : 0.03f, 0.03f);
            boxAt(0, 0.5f, 0, 0.32f, 0.12f, 0.32f);
            // the sign: a post and a board with the skull, seen from far away (pixels, like the game's text)
            glColor3f(0.15f, 0.15f, 0.16f);
            boxAt(0, 2.2f, 0, 0.09f, 2.2f, 0.09f);
            static const char *const SKULL[7] = {".XXXXX.", "XXXXXXX", "X..X..X", "X..X..X", "XXX.XXX", ".XXXXX.", ".X.X.X."};
            const float px = 0.28f, top = 6.2f;
            glColor3f(0.85f, 0.08f, 0.06f);
            boxAt(0, top - 3.5f * px, 0, 4.5f * px, 4.5f * px, 0.05f);
            glColor3f(0.04f, 0.04f, 0.05f);
            boxAt(0, top - 3.5f * px, 0, 3.8f * px, 3.8f * px, 0.08f);
            glColor3f(1.0f, 1.0f, 0.95f);
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 7; ++col)
                    if (SKULL[row][col] == 'X')
                        boxAt((float(col) - 3.0f) * px * 1.0f, top - float(row) * px, 0, px * 0.5f, px * 0.5f, 0.10f);
            glEnable(GL_LIGHTING);
        } else if (o.type == sim::Obstacle::Stone) {      // a rock off the hillside: never two quite alike
            const int look = int(std::fabs(o.d) * 7.0f + o.s) % 4;
            glRotatef(float(look) * 67.0f, 0, 1, 0);
            const float k = 0.9f + 0.08f * float(look);
            glScalef(k, k, k);
            glColor3f(0.42f + 0.03f * float(look), 0.40f + 0.02f * float(look), 0.37f);
            boxAt(0, 0.45f, 0, 0.85f, 0.45f, 0.70f);
            glColor3f(0.52f, 0.50f, 0.46f);
            boxAt(0.15f, 1.00f, -0.08f, 0.55f, 0.20f, 0.46f);
            glColor3f(0.33f, 0.31f, 0.29f);
            boxAt(-0.55f, 0.22f, 0.35f, 0.42f, 0.22f, 0.38f);
        } else if (o.type == sim::Obstacle::Cone) {
            glColor3f(0.98f, 0.42f, 0.05f);
            boxAt(0, 0.04f, 0, 0.45f, 0.04f, 0.45f);
            glPushMatrix();
            glRotatef(-90, 1, 0, 0);
            gluCylinder(quad, 0.42, 0.05, 1.1, 10, 1);
            glPopMatrix();
            glColor3f(0.95f, 0.95f, 0.95f);
            boxAt(0, 0.6f, 0, 0.23f, 0.08f, 0.23f);
        } else if (o.type == sim::Obstacle::Barrel) {
            glColor3f(0.85f, 0.15f, 0.15f);
            glPushMatrix();
            glRotatef(-90, 1, 0, 0);
            gluCylinder(quad, 0.6, 0.6, 1.3, 12, 1);
            glPushMatrix(); glTranslatef(0, 0, 1.3f); gluDisk(quad, 0, 0.6, 12, 1); glPopMatrix();
            glPopMatrix();
            glColor3f(0.95f, 0.95f, 0.95f);
            for (float by : {0.4f, 0.9f}) boxAt(0, by, 0, 0.62f, 0.06f, 0.62f);
        } else if (o.type == sim::Obstacle::Tyres) {      // three black tyres, a white band on the middle one
            glPushMatrix();
            glRotatef(-90, 1, 0, 0);
            for (int k = 0; k < 3; ++k) {
                glColor3f(0.08f, 0.08f, 0.09f);
                glPushMatrix(); glTranslatef(0, 0, 0.42f * k);
                gluCylinder(quad, 0.65, 0.65, 0.40, 14, 1);
                glPushMatrix(); glTranslatef(0, 0, 0.40f); gluDisk(quad, 0.28, 0.65, 14, 1); glPopMatrix();
                glPopMatrix();
            }
            glPopMatrix();
            glColor3f(0.95f, 0.95f, 0.95f);
            boxAt(0, 0.63f, 0, 0.67f, 0.05f, 0.67f);
        } else if (o.type == sim::Obstacle::Concrete) {   // a grey road block, red and white on top
            glColor3f(0.62f, 0.62f, 0.60f);
            boxAt(0, 0.55f, 0, o.hx, 0.55f, o.hz);
            glColor3f(0.45f, 0.45f, 0.44f);
            boxAt(0, 0.06f, 0, o.hx + 0.12f, 0.06f, o.hz + 0.12f);
            for (float bx = -o.hx + 0.35f; bx < o.hx; bx += 0.7f) {
                const int k = int((bx + o.hx) / 0.7f);
                if (k % 2) glColor3f(0.90f, 0.12f, 0.10f); else glColor3f(0.96f, 0.96f, 0.96f);
                boxAt(bx, 1.12f, 0, 0.35f, 0.04f, o.hz + 0.01f);
            }
        } else {
            glColor3f(0.95f, 0.50f, 0.08f);
            boxAt(0, 0.5f, 0, o.hx, 0.5f, o.hz);
            glColor3f(0.12f, 0.12f, 0.15f);
            boxAt(0, 0.08f, 0, o.hx + 0.15f, 0.08f, o.hz + 0.15f);
            glColor3f(0.95f, 0.95f, 0.95f);
            for (float bx = -o.hx + 0.6f; bx < o.hx; bx += 1.2f) {
                glPushMatrix();
                glTranslatef(bx, 0.5f, o.hz + 0.01f);
                glRotatef(45, 0, 0, 1);
                box(0.12f, 0.55f, 0.005f);
                glPopMatrix();
            }
        }
        glPopMatrix();
    }
}

// While the shortcut is closed to your car (you are in the top 3) a red and white barrier
// stands across its mouth, a few metres in
void GLWidget::drawShortcutGate()
{
    const auto &cars = simEngine.getCars();
    const int me = localCar();
    if (!simEngine.hasShortcut() || me < 0 || me >= int(cars.size()) || !cars[size_t(me)].cutClosed || cars[size_t(me)].path != 0) return;
    const float at = simEngine.shortcutGateS();          // where it has left the main road
    float gx, gz;
    simEngine.pointAt(at, 0.0f, gx, gz, 1);
    if (!inView(gx, gz, 12.0f)) return;
    const float half = simEngine.roadHalfAt(at, 1) + 0.5f;
    glPushMatrix();
    glTranslatef(gx, simEngine.heightAt(at, 0.0f, 1), gz);
    glRotatef(-simEngine.headingAt(at, 1) * 180.0f / PI, 0, 1, 0);   // local -Z = along the road, +X = across it
    for (float side : {-half, half}) {                   // posts
        glColor3f(0.85f, 0.85f, 0.88f);
        boxAt(side, 0.8f, 0, 0.15f, 0.8f, 0.15f);
    }
    const int bars = int(half * 2.0f / 1.2f) + 1;
    for (int k = 0; k < bars; ++k) {                     // the board, striped
        const float x0 = -half + float(k) * (2.0f * half / float(bars));
        if (k % 2) glColor3f(0.95f, 0.95f, 0.95f); else glColor3f(0.90f, 0.12f, 0.10f);
        boxAt(x0 + half / float(bars), 1.15f, 0, half / float(bars), 0.32f, 0.08f);
    }
    glColor3f(0.90f, 0.12f, 0.10f);                      // a sign: a red disc with a white bar
    glPushMatrix();
    glTranslatef(0, 2.3f, 0.1f);
    gluDisk(quad, 0, 0.7, 20, 1);
    glColor3f(0.97f, 0.97f, 0.97f);
    boxAt(0, 0, 0.02f, 0.45f, 0.12f, 0.01f);
    glPopMatrix();
    glPopMatrix();
}

void GLWidget::drawNitroCanister()
{
    // Main nitro tank (metallic electric cyan)
    glColor3f(0.12f, 0.75f, 1.0f);
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    glTranslatef(0, 0, -0.4f);
    gluCylinder(quad, 0.34, 0.34, 0.82, 14, 1);
    gluDisk(quad, 0, 0.34, 14, 1);
    glPushMatrix();
    glTranslatef(0, 0, 0.82f);
    gluDisk(quad, 0, 0.34, 14, 1);
    glPopMatrix();
    glPopMatrix();

    // White label stripe around middle
    glColor3f(0.95f, 0.95f, 0.95f);
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    glTranslatef(0, 0, -0.1f);
    gluCylinder(quad, 0.35, 0.35, 0.22, 14, 1);
    glPopMatrix();

    // Metallic neck & nozzle valve
    glColor3f(0.85f, 0.86f, 0.90f);
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    glTranslatef(0, 0, 0.42f);
    gluCylinder(quad, 0.14, 0.10, 0.20, 10, 1);
    gluDisk(quad, 0, 0.10, 10, 1);
    glPopMatrix();

    // Brass valve knob
    glColor3f(0.96f, 0.80f, 0.25f);
    boxAt(0, 0.68f, 0, 0.12f, 0.05f, 0.12f);

    // Glowing neon aura ring around canister
    glDisable(GL_LIGHTING);
    float glow = 0.6f + 0.4f * std::sin(float(simEngine.getTick()) * 0.18f);
    glColor4f(0.25f, 0.95f, 1.0f, glow);
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    gluDisk(quad, 0.36, 0.50, 14, 1);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void GLWidget::drawNitroPickups()
{
    float tickF = float(simEngine.getTick());
    for (const sim::NitroPickup &p : simEngine.getNitroPickups()) {
        if (!p.active) continue;
        if (!inView(p.x, p.z, 2.0f)) continue;

        glPushMatrix();
        float hoverY = 0.55f + 0.12f * std::sin(tickF * 0.08f + p.z * 0.1f);
        glTranslatef(p.x, simEngine.heightAt(p.s, p.d) + hoverY, p.z);
        glRotatef(tickF * 2.8f, 0, 1, 0);
        drawNitroCanister();
        glPopMatrix();

        // Ground glow disk on the road surface
        glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
        float pulse = 0.38f + 0.20f * std::sin(tickF * 0.10f + p.z);
        glColor4f(0.12f, 0.85f, 1.0f, pulse);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(p.x, 0.04f, p.z);
        for (int i = 0; i <= 16; ++i) {
            float a = i * 2.0f * PI / 16.0f;
            glVertex3f(p.x + std::cos(a) * 1.15f, 0.04f, p.z + std::sin(a) * 1.15f);
        }
        glEnd();
        glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);
    }
}

// ===========================================================================
//  VEHICLES
// ===========================================================================
// Alloy wheel: tyre with sidewalls, polished rim lip, 5 twin spokes, brake disc
// and a red caliper. `side` (-1 left, +1 right) keeps the rim face on the outside.
void GLWidget::drawWheel(float x, float y, float z, float steerDeg, float spin, float side, float radius)
{
    const float R = radius, W = radius * 0.78f, rimR = radius * 0.70f;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(-steerDeg, 0, 1, 0);
    if (side < 0.0f) glRotatef(180.0f, 0, 1, 0);   // outer face always along local +X

    // brake caliper: steers with the wheel but does not spin
    glColor3f(0.80f, 0.12f, 0.08f);
    glPushMatrix();
    glRotatef(-35.0f, 1, 0, 0);
    boxAt(W * 0.5f - 0.075f, rimR * 0.58f, 0.0f, 0.03f, 0.07f, 0.11f);
    glPopMatrix();

    glRotatef(side < 0.0f ? spin : -spin, 1, 0, 0);  // mirrored frame on the left needs the opposite sign
    drawCachedModel(100000 + int(R * 1000.0f), [&]() {   // the spinning part, recorded once per size
        glPushMatrix();
        glRotatef(90, 0, 1, 0);                         // cylinder axis (local Z) -> outward X
        glTranslatef(0, 0, -W * 0.5f);

        glColor3f(0.07f, 0.07f, 0.075f);                // tread + inner sidewall
        gluCylinder(quad, R, R, W, 24, 1);
        glPushMatrix(); glRotatef(180.0f, 0, 1, 0); gluDisk(quad, 0.0, R, 24, 1); glPopMatrix();

        glTranslatef(0, 0, W);
        glColor3f(0.11f, 0.11f, 0.12f);                 // outer sidewall
        gluDisk(quad, rimR, R, 24, 1);
        glColor3f(0.82f, 0.83f, 0.86f);                 // polished rim lip
        gluDisk(quad, rimR * 0.88f, rimR, 24, 1);

        glTranslatef(0, 0, -0.06f);                     // dished centre
        glColor3f(0.32f, 0.32f, 0.34f);                 // brake disc seen between the spokes
        gluDisk(quad, 0.0, rimR * 0.88f, 20, 1);
        glColor3f(0.78f, 0.79f, 0.83f);
        for (int k = 0; k < 5; ++k) {
            for (float off : {-8.0f, 8.0f}) {
                glPushMatrix();
                glRotatef(k * 72.0f + off, 0, 0, 1);
                boxAt(0, rimR * 0.48f, 0.035f, 0.022f, rimR * 0.42f, 0.02f);
                glPopMatrix();
            }
        }
        glColor3f(0.20f, 0.20f, 0.22f);                 // centre hub
        glPushMatrix(); glTranslatef(0, 0, 0.056f); gluDisk(quad, 0.0, rimR * 0.20f, 12, 1); glPopMatrix();
        glPopMatrix();
    });
    glPopMatrix();
}

// Records a model that never changes into a display list the first time, then replays it.
void GLWidget::drawCachedModel(int key, const std::function<void()> &draw)
{
    auto it = modelLists.constFind(key);
    if (it == modelLists.constEnd()) {
        GLuint list = glGenLists(1);
        glNewList(list, GL_COMPILE);
        draw();
        glEndList();
        it = modelLists.insert(key, list);
    }
    glCallList(it.value());
}

// Soft contact shadow: dark rounded core that fades out towards the edges.
void GLWidget::drawShadow(float x, float z, float heading, float hw, float hl, const RoadPose &pose, float fade)
{
    const int N = 28;
    auto pt = [&](int i, float k) {
        float t = i * 2.0f * PI / N, c = std::cos(t), s = std::sin(t);
        float px = hw * k * std::copysign(std::sqrt(std::fabs(c)), c);
        float pz = hl * k * std::copysign(std::sqrt(std::fabs(s)), s);
        glVertex3f(px, 0, pz);
    };
    glPushMatrix();
    glTranslatef(x, pose.y + 0.045f, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);
    glRotatef(pose.pitch, 1, 0, 0);
    glRotatef(pose.roll, 0, 0, 1);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0, 0, 0, 0.55f * fade);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= N; ++i) pt(i, 0.80f);
    glEnd();
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= N; ++i) {
        glColor4f(0, 0, 0, 0.55f * fade); pt(i, 0.80f);
        glColor4f(0, 0, 0, 0.0f);  pt(i, 1.25f);
    }
    glEnd();
    glPopMatrix();
}

void GLWidget::drawCar(float x, float z, float heading, float roll, float pitch,
                       float steer, float spin, float r, float g, float b, bool nitro,
                       const char *badge, float brake, const RoadPose &pose, float health,
                       const FigureLook *driver)
{
    using namespace carmodel;
    glPushMatrix();
    glTranslatef(x, pose.y, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);
    glRotatef(pose.pitch, 1, 0, 0);                      // standing on the slope and banking
    glRotatef(pose.roll, 0, 0, 1);

    // Wheels stay planted on the road; only the body rolls and pitches on its springs.
    float stDeg = steer * (180.0f / PI);
    drawWheel(-WHEEL_X, WHEEL_R, WHEEL_Z_FRONT, stDeg, spin, -1.0f, WHEEL_R);
    drawWheel( WHEEL_X, WHEEL_R, WHEEL_Z_FRONT, stDeg, spin,  1.0f, WHEEL_R);
    drawWheel(-WHEEL_X, WHEEL_R, WHEEL_Z_REAR,  0.0f,  spin, -1.0f, WHEEL_R);
    drawWheel( WHEEL_X, WHEEL_R, WHEEL_Z_REAR,  0.0f,  spin,  1.0f, WHEEL_R);

    glPushMatrix();
    glTranslatef(0, WHEEL_R, 0);
    glRotatef(roll, 0, 0, 1);
    glRotatef(pitch, 1, 0, 0);
    glTranslatef(0, -WHEEL_R, 0);

    // Glossy paint: specular highlight + a clear-coat reflection of the sky
    const GLfloat paintSpec[4] = {0.55f, 0.55f, 0.55f, 1.0f};
    const GLfloat glassSpec[4] = {0.90f, 0.90f, 0.90f, 1.0f};
    const GLfloat noSpec[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, paintSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.0f);
    glColor3f(r, g, b);
    glCallList(carBodyList);
    if (badge) drawWing();                 // racers get a rear wing, road traffic doesn't
    reflectionPass(carBodyList, texEnv, 0.22f);

    // the driver, sitting on the left behind the wheel and looking where the car steers.
    // A dark cabin first, then the glass (not hiding what is behind it), then the driver -
    // so the figure shows clearly through the tinted glass - then the glass's reflection.
    if (driver) {
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, noSpec);
        glColor3f(0.09f, 0.09f, 0.11f);                  // the cabin: tub, seats, dashboard
        boxAt(0.0f, 0.66f, 0.15f, 0.68f, 0.26f, 0.92f);
        boxAt(-0.34f, 0.85f, 0.62f, 0.21f, 0.30f, 0.07f);
        boxAt( 0.34f, 0.85f, 0.62f, 0.21f, 0.30f, 0.07f);
        boxAt(0.0f, 0.86f, -0.62f, 0.66f, 0.07f, 0.16f);
    }
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, glassSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 90.0f);
    if (driver) glDepthMask(GL_FALSE);
    glColor4f(0.05f, 0.06f, 0.08f, driver ? 0.45f : 1.0f);
    glCallList(carGlassList);
    if (driver) {
        glDepthMask(GL_TRUE);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, paintSpec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 30.0f);
        glPushMatrix();
        glTranslatef(-0.34f, 0.20f, 0.18f);              // a toy figure: big head, filling the window
        glScalef(1.25f, 1.25f, 1.25f);
        FigurePose sit;
        sit.hip = 82.0f;
        sit.armL = sit.armR = 58.0f;
        sit.headTurn = -steer * 35.0f;
        drawBlockFigure(*driver, sit);
        glPopMatrix();
        glColor3f(0.08f, 0.08f, 0.09f);                  // steering wheel
        boxAt(-0.34f, 0.86f, -0.20f, 0.12f, 0.018f, 0.018f);
        boxAt(-0.34f, 0.74f, -0.20f, 0.12f, 0.018f, 0.018f);
        boxAt(-0.45f, 0.80f, -0.20f, 0.018f, 0.06f, 0.018f);
        boxAt(-0.23f, 0.80f, -0.20f, 0.018f, 0.06f, 0.018f);
    }
    reflectionPass(carGlassList, texEnv, driver ? 0.20f : 0.38f);

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, noSpec);
    glCallList(carTrimList);

    glDisable(GL_LIGHTING);
    drawLamps(brake);
    if (nitro) {
        float fl = 0.7f + 0.3f * std::sin(simEngine.getTick() * 1.7f);
        beginGlow();
        for (float sx : {-0.48f, 0.48f}) {
            glPushMatrix();
            glTranslatef(sx, 0.28f, TAIL_Z + 0.06f);
            glColor4f(0.35f, 0.65f, 1.0f, 0.80f);
            gluCylinder(quad, 0.10, 0.0, 1.0 + 0.6 * fl, 10, 1);
            glColor4f(1.0f, 1.0f, 1.0f, 0.90f);
            gluCylinder(quad, 0.05, 0.0, 0.5 + 0.3 * fl, 10, 1);
            glPopMatrix();
        }
        endGlow();
    }
    glPopMatrix();   // end of sprung body

    // Pools of headlight on the asphalt ahead of the car
    beginGlow();
    for (float sx : {-0.9f, 0.9f}) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 0.93f, 0.78f, 0.12f);
        glVertex3f(sx, 0.04f, -6.0f);
        glColor4f(1.0f, 0.93f, 0.78f, 0.0f);
        for (int i = 0; i <= 20; ++i) {
            float t = i * 2.0f * PI / 20.0f;
            glVertex3f(sx + std::cos(t) * 1.6f, 0.04f, -6.0f + std::sin(t) * 3.6f);
        }
        glEnd();
    }
    endGlow();

    // Overhead floating vehicle badge (e.g. [ 1 ], [ CPU ], [ 2 ]) and the car's health
    if (badge && *badge) {
        glRotatef(-pose.roll, 0, 0, 1);                  // the plate stays upright on slopes and banking
        glRotatef(-pose.pitch, 1, 0, 0);
        drawPlate(badge, health);
    }
    glEnable(GL_LIGHTING);

    glPopMatrix();
}

// Name plate over a car (current matrix: the car's place, facing along it, upright) with the
// car's health above it: ten blocks, green / yellow / red, blinking when nearly wrecked.
void GLWidget::drawPlate(const char *badge, float health)
{
    float bLen = float(std::strlen(badge));
    float hw = std::max(0.35f, bLen * 0.16f + 0.12f);
    float letter = 0.038f;
    const float MAX_HW = 1.40f;                       // long names: smaller letters, same plate
    if (hw > MAX_HW) { letter *= (MAX_HW - 0.12f) / (bLen * 0.16f); hw = MAX_HW; }
    float hh = 0.24f;
    float by = 2.25f;

    // Dark background plate
    glColor4f(0.05f, 0.07f, 0.12f, 0.92f);
    boxAt(0, by, 0, hw, hh, 0.04f);

    // Glowing border (Orange for CPU, Cyan for Player)
    bool isCpu = (std::strncmp(badge, "CPU", 3) == 0);
    if (isCpu) glColor3f(1.0f, 0.45f, 0.20f);
    else       glColor3f(0.20f, 0.90f, 1.0f);
    float th = 0.02f;
    boxAt(0, by + hh, 0.045f, hw, th, 0.005f);
    boxAt(0, by - hh, 0.045f, hw, th, 0.005f);
    boxAt(-hw, by, 0.045f, th, hh, 0.005f);
    boxAt( hw, by, 0.045f, th, hh, 0.005f);

    // Double-sided text
    glColor3f(1.0f, 1.0f, 1.0f);
    const float ly = by - 0.10f * letter / 0.038f;
    drawText3D(badge, 0, ly, 0.055f, letter);
    glPushMatrix();
    glRotatef(180.0f, 0, 1, 0);
    drawText3D(badge, 0, ly, 0.055f, letter);
    glPopMatrix();

    // Small indicator triangle beneath the badge pointing down towards roof
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.08f, by - hh, 0.045f);
    glVertex3f( 0.08f, by - hh, 0.045f);
    glVertex3f( 0.00f, by - hh - 0.12f, 0.045f);
    glEnd();

    if (health >= 0.0f) {
        const float hy = by + hh + 0.13f, bh = 0.055f, w = hw;
        glColor4f(0.05f, 0.07f, 0.12f, 0.92f);
        boxAt(0, hy, 0, w + 0.03f, bh + 0.03f, 0.03f);
        const float k = std::clamp(health / 100.0f, 0.0f, 1.0f);
        const bool blink = health < 25.0f && (simEngine.getTick() / 8) % 2 == 0;
        if (health > 60.0f)      glColor3f(0.30f, 0.95f, 0.40f);
        else if (health > 30.0f) glColor3f(1.00f, 0.80f, 0.20f);
        else                     glColor3f(blink ? 1.0f : 0.75f, blink ? 0.45f : 0.12f, 0.10f);
        for (int i = 0; i < 10; ++i) {                   // ten blocks of 10
            const float f = std::clamp(k * 10.0f - float(i), 0.0f, 1.0f);
            if (f <= 0.0f) break;
            const float x0 = -w + (2.0f * w) * float(i) / 10.0f + 0.012f;
            const float x1 = x0 + ((2.0f * w) / 10.0f - 0.024f) * f;
            boxAt(0.5f * (x0 + x1), hy, 0.0f, 0.5f * (x1 - x0), bh, 0.035f);
        }
    }
}

// ===========================================================================
//  BLOCK-FIGURE DRIVERS AND WRECKS
// ===========================================================================
namespace {
// a box narrower at the bottom than at the top (the figure's chest): y0..y1, depth +/- hd
void taperBox(float bw, float tw, float hd, float y0, float y1)
{
    const V3 c{0.0f, 0.5f * (y0 + y1), 0.0f};
    const V3 b0{-bw, y0, -hd}, b1{bw, y0, -hd}, b2{bw, y0, hd}, b3{-bw, y0, hd};
    const V3 t0{-tw, y1, -hd}, t1{tw, y1, -hd}, t2{tw, y1, hd}, t3{-tw, y1, hd};
    glBegin(GL_QUADS);
    quadN(b0, b1, t1, t0, c); quadN(b1, b2, t2, t1, c); quadN(b2, b3, t3, t2, c); quadN(b3, b0, t0, t3, c);
    quadN(t0, t1, t2, t3, c); quadN(b3, b2, b1, b0, c);
    glEnd();
}
} // namespace

// The figure stands on its hip joint (origin), faces -Z like the cars, about 1.2 m tall:
// legs below the hip, then belt, chest, arms on the shoulders, neck and a square head.
void GLWidget::drawBlockFigure(const FigureLook &L, const FigurePose &P)
{
    auto col = [](const float *c, float k = 1.0f) { glColor3f(c[0] * k, c[1] * k, c[2] * k); };
    for (int side : {-1, 1}) {                            // legs, hinged at the hip
        glPushMatrix();
        glTranslatef(float(side) * 0.075f, 0.0f, 0.0f);
        glRotatef(P.hip + float(side) * P.legSwing, 1, 0, 0);
        col(L.suit, 0.78f);
        boxAt(0.0f, -0.20f, 0.0f, 0.064f, 0.20f, 0.080f);
        glColor3f(0.12f, 0.12f, 0.14f);                   // boots
        boxAt(0.0f, -0.42f, -0.025f, 0.066f, 0.030f, 0.105f);
        glPopMatrix();
    }
    glColor3f(0.15f, 0.15f, 0.17f);                       // belt
    boxAt(0.0f, 0.045f, 0.0f, 0.150f, 0.045f, 0.088f);
    col(L.suit);
    taperBox(0.142f, 0.195f, 0.090f, 0.09f, 0.46f);       // chest
    col(L.trim);
    boxAt(0.0f, 0.28f, -0.092f, 0.030f, 0.17f, 0.004f);   // stripe down the front
    for (int side : {-1, 1}) {                            // arms: + = forward, 180 = up
        glPushMatrix();
        glTranslatef(float(side) * 0.215f, 0.425f, 0.0f);
        glRotatef(side < 0 ? P.armL : P.armR, 1, 0, 0);
        glRotatef(float(side) * 7.0f, 0, 0, 1);
        col(L.suit);
        boxAt(0.0f, -0.14f, 0.0f, 0.050f, 0.15f, 0.056f);
        col(L.skin);                                      // the C-shaped hand
        boxAt(0.0f, -0.325f, -0.028f, 0.040f, 0.036f, 0.016f);
        boxAt(0.0f, -0.325f, 0.030f, 0.040f, 0.036f, 0.014f);
        boxAt(0.0f, -0.352f, 0.0f, 0.040f, 0.010f, 0.044f);
        glPopMatrix();
    }
    glPushMatrix();
    glRotatef(P.headTurn, 0, 1, 0);
    col(L.skin);
    boxAt(0.0f, 0.485f, 0.0f, 0.052f, 0.025f, 0.052f);    // neck
    boxAt(0.0f, 0.615f, 0.0f, 0.118f, 0.108f, 0.108f);    // head
    glColor3f(0.08f, 0.07f, 0.06f);                       // face: pixel eyes and mouth
    const float fz = -0.110f;
    boxAt(-0.042f, 0.640f, fz, 0.013f, 0.019f, 0.003f);
    boxAt( 0.042f, 0.640f, fz, 0.013f, 0.019f, 0.003f);
    switch (L.face) {
    case 1:                                               // grin
        boxAt(0.0f, 0.578f, fz, 0.048f, 0.013f, 0.003f);
        glColor3f(0.98f, 0.98f, 0.95f);
        boxAt(0.0f, 0.582f, fz - 0.001f, 0.040f, 0.005f, 0.003f);
        break;
    case 2:                                               // frown, with angry brows
        boxAt(0.0f, 0.575f, fz, 0.030f, 0.007f, 0.003f);
        boxAt(-0.036f, 0.566f, fz, 0.008f, 0.008f, 0.003f);
        boxAt( 0.036f, 0.566f, fz, 0.008f, 0.008f, 0.003f);
        boxAt(-0.045f, 0.672f, fz, 0.022f, 0.006f, 0.003f);
        boxAt( 0.045f, 0.672f, fz, 0.022f, 0.006f, 0.003f);
        break;
    case 3:                                               // surprised: "O"
        boxAt(0.0f, 0.572f, fz, 0.018f, 0.022f, 0.003f);
        break;
    default:                                              // smile
        boxAt(0.0f, 0.572f, fz, 0.030f, 0.007f, 0.003f);
        boxAt(-0.036f, 0.582f, fz, 0.008f, 0.008f, 0.003f);
        boxAt( 0.036f, 0.582f, fz, 0.008f, 0.008f, 0.003f);
        break;
    }
    if (L.helmetOn) {                                     // open-face helmet, visor flipped up
        col(L.helmet);
        boxAt(0.0f, 0.745f, 0.008f, 0.133f, 0.030f, 0.125f);
        boxAt(0.0f, 0.625f, 0.098f, 0.133f, 0.095f, 0.028f);
        boxAt(-0.123f, 0.630f, 0.0f, 0.012f, 0.095f, 0.115f);
        boxAt( 0.123f, 0.630f, 0.0f, 0.012f, 0.095f, 0.115f);
        col(L.trim);
        boxAt(0.0f, 0.777f, 0.008f, 0.026f, 0.004f, 0.120f);
        glColor3f(0.10f, 0.12f, 0.18f);
        boxAt(0.0f, 0.737f, -0.128f, 0.112f, 0.014f, 0.018f);
    } else {                                              // hair
        col(L.hair);
        boxAt(0.0f, 0.738f, 0.010f, 0.124f, 0.030f, 0.116f);
        boxAt(0.0f, 0.660f, 0.095f, 0.124f, 0.060f, 0.020f);
    }
    glPopMatrix();
}

// Each racer's driver: overalls in the car's colour, a helmet that stands out from it;
// skin tones vary from car to car
FigureLook GLWidget::driverLook(int carIdx, const sim::Car &c) const
{
    static const float SKIN[4][3] = {{1.00f, 0.80f, 0.25f}, {0.93f, 0.72f, 0.55f}, {0.62f, 0.42f, 0.28f},
                                     {0.98f, 0.86f, 0.72f}};
    FigureLook L;
    L.suit[0] = c.r; L.suit[1] = c.g; L.suit[2] = c.b;
    const float bright = 0.30f * c.r + 0.59f * c.g + 0.11f * c.b;
    const float hel = bright > 0.55f ? 0.12f : 0.95f;
    L.helmet[0] = L.helmet[1] = L.helmet[2] = hel;
    L.trim[0] = L.trim[1] = L.trim[2] = 1.0f - hel * 0.8f;
    if (c.rosterId >= 0) {                               // a named driver's own helmet
        const sim::RosterDriver &R = sim::rosterDriver(c.rosterId);
        L.helmet[0] = R.helmet[0]; L.helmet[1] = R.helmet[1]; L.helmet[2] = R.helmet[2];
    }
    if (isLocalCar(carIdx)) { L.helmet[0] = 1.0f; L.helmet[1] = 0.78f; L.helmet[2] = 0.15f; }   // players: gold helmet
    const float *sk = SKIN[(carIdx * 7 + 1) % 4];
    L.skin[0] = sk[0]; L.skin[1] = sk[1]; L.skin[2] = sk[2];
    return L;
}

bool GLWidget::isLocalCar(int i) const
{
    if (isOnline()) return i == mySlot;
    if (simEngine.getGameMode() == sim::GameSim::ModeSplitScreen) return i == 0 || i == 1;
    return i == 0;
}

QByteArray GLWidget::carName(int i) const
{
    if (i >= 0 && i < int(nameOverride.size()) && !nameOverride[size_t(i)].isEmpty()) return nameOverride[size_t(i)];
    if (isOnline()) return i >= 0 && i < racerNames.size() ? racerNames[i].toUtf8() : "CAR" + QByteArray::number(i + 1);
    if (simEngine.getGameMode() == sim::GameSim::ModeSingle)
        return i == 0 ? QByteArray("YOU") : simEngine.getCars().size() > 2 ? "CPU" + QByteArray::number(i) : QByteArray("CPU");
    return "P" + QByteArray::number(i + 1);
}

void GLWidget::pushMsg(std::deque<HudMsg> &q, const QByteArray &text, float r, float g, float b, int ticks)
{
    HudMsg m;
    m.text = text; m.r = r; m.g = g; m.b = b;
    m.until = simEngine.getTick() + ticks;
    q.push_back(m);
    while (q.size() > 4) q.pop_front();
}

// The road (inside the guard rails) or the ground (beyond them) under a point
float GLWidget::floorUnder(float x, float z, const sim::Car &ref) const
{
    float s, d;
    simEngine.project(x, z, ref.s, s, d, ref.path, false);
    if (std::fabs(d) <= railOffset() + 1.0f) return simEngine.heightAt(s, d, ref.path);
    return groundAt(x, z);
}

// A thrown-out driver, worked out from where and how fast the wreck began (the same on
// every computer, so online players see the same flight): out through the windscreen 4 ticks
// after the crash, tumbling through the air, one bounce, a slide; lies still, gets up, runs
// back to the car (which has stopped by then) and hops in just before it is back in the race.
bool GLWidget::ejectedDriver(const sim::Car &c, EjectedDriver &o) const
{
    const int T0 = 4, W = sim::GameSim::WRECK_TIME_EJECT;
    const int t = c.wreck;
    if (!c.ejected || t < T0 || t >= W) return false;
    const float sp = std::sqrt(c.wreckVx * c.wreckVx + c.wreckVz * c.wreckVz);
    const float fx = sp > 0.01f ? c.wreckVx / sp : std::sin(c.heading), fz = sp > 0.01f ? c.wreckVz / sp : -std::cos(c.heading);
    const float rx = -fz, rz = fx;                        // the car's right
    const float side = c.wreckSpin >= 0.0f ? 1.0f : -1.0f;
    float x = c.wreckX + fx * 0.4f, z = c.wreckZ + fz * 0.4f;
    float y = floorUnder(x, z, c) + 1.0f;
    float vx = c.wreckVx * 0.36f + rx * 0.05f * side, vz = c.wreckVz * 0.36f + rz * 0.05f * side, vy = 0.17f;
    int bounces = 0, landed = -1, k = 0;
    const int steps = t - T0;
    for (; k < steps; ++k) {
        if (bounces < 2) {
            x += vx; z += vz; y += vy; vy -= sim::GRAVITY;
            const float fl = floorUnder(x, z, c);
            if (y <= fl) {
                y = fl;
                if (++bounces == 1) { vy = -vy * 0.35f; vx *= 0.5f; vz *= 0.5f; }
                else { vy = 0.0f; landed = k; }
            }
        } else {                                          // sliding to a stop
            x += vx; z += vz; vx *= 0.85f; vz *= 0.85f;
            y = floorUnder(x, z, c);
        }
    }
    o.face = 3;
    o.heading = std::atan2(fx, -fz);
    o.pose = FigurePose();
    if (bounces < 2) {                                    // flying and tumbling
        o.x = x; o.y = y; o.z = z;
        o.rotX = 13.0f * float(steps);
        o.rotZ = 5.0f * float(steps) * side;
        o.pose.armL = o.pose.armR = 165.0f + 10.0f * std::sin(float(steps) * 0.5f);
        o.pose.legSwing = 30.0f * std::sin(float(steps) * 0.4f);
        return true;
    }
    // on the ground: lie still, get up, run back to the car's door, hop in
    const int free = landed + T0;                         // the tick the slide began
    const int lieEnd = std::min(free + 30, W - 70), upEnd = lieEnd + 14, runEnd = W - 16;
    const float doorX = c.x - std::cos(c.heading) * 1.6f, doorZ = c.z - std::sin(c.heading) * 1.6f;
    o.rotZ = 0.0f;
    if (t < lieEnd) {
        o.x = x; o.y = y + 0.10f; o.z = z;
        o.rotX = 90.0f;                                   // face down
        o.pose.armL = o.pose.armR = 150.0f;
        o.face = 3;
    } else if (t < upEnd) {
        const float u = float(t - lieEnd) / float(upEnd - lieEnd);
        o.x = x; o.y = y + 0.10f + 0.50f * u; o.z = z;
        o.rotX = 90.0f * (1.0f - u);
        o.pose.armL = o.pose.armR = 150.0f * (1.0f - u);
        o.face = 0;
    } else if (t < runEnd) {                              // run to the car
        const float u = float(t - upEnd) / float(std::max(1, runEnd - upEnd));
        const float e = u * u * (3.0f - 2.0f * u);
        o.x = x + (doorX - x) * e; o.z = z + (doorZ - z) * e;
        o.y = floorUnder(o.x, o.z, c) + 0.62f + 0.06f * std::fabs(std::sin(float(t) * 0.55f));
        o.heading = std::atan2(doorX - x, -(doorZ - z));
        o.rotX = 8.0f;                                    // leaning into the run
        o.pose.legSwing = 45.0f * std::sin(float(t) * 0.55f);
        o.pose.armL = 50.0f * std::sin(float(t) * 0.55f);
        o.pose.armR = -o.pose.armL;
        o.face = 1;
    } else {                                              // hop in
        const float u = float(t - runEnd) / float(W - runEnd);
        o.x = doorX + (c.x - doorX) * u; o.z = doorZ + (c.z - doorZ) * u;
        o.y = floorUnder(c.x, c.z, c) + 0.62f + 1.1f * std::sin(u * PI);
        o.heading = c.heading;
        o.rotX = 0.0f;
        o.pose.hip = 80.0f * u;
        o.pose.armL = o.pose.armR = 170.0f;
        o.face = 1;
    }
    return true;
}

// A wreck: tumbles 1-2 rolls with a couple of hops while it slides, then sits smoking on the
// verge until it is back in the race. The driver stays in, or flies out (ejectedDriver).
void GLWidget::drawWreck(const sim::Car &c, int idx, const char *badge)
{
    const int t = c.wreck;
    const float u = std::min(1.0f, float(t) / 60.0f);
    const float ease = 1.0f - (1.0f - u) * (1.0f - u);
    const float rollDeg = c.wreckSpin * 360.0f * ease;
    const float hop = t < 60 ? 1.5f * std::fabs(std::sin(float(t) * PI / 28.0f)) * (1.0f - u) : 0.0f;
    const RoadPose pose = carPose(c);
    glPushMatrix();
    glTranslatef(c.x, pose.y + hop + 0.75f, c.z);
    glRotatef(-c.heading * 180.0f / PI, 0, 1, 0);
    glRotatef(rollDeg, 0, 0, 1);
    glRotatef(12.0f * std::sin(float(t) * 0.21f) * (1.0f - u), 1, 0, 0);
    glTranslatef(0.0f, -0.75f, 0.0f);
    const FigureLook dl = driverLook(idx, c);
    const bool driverIn = !c.ejected || t < 4;
    RoadPose flat;
    drawCar(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, c.wheelSpin, c.r * 0.72f, c.g * 0.72f, c.b * 0.72f, false,
            nullptr, 0.0f, flat, -1.0f, driverIn ? &dl : nullptr);
    glPopMatrix();
    if (badge && *badge) {                                // the plate stays over the wreck, upright
        glPushMatrix();
        glTranslatef(c.x, pose.y + hop, c.z);
        glRotatef(-c.heading * 180.0f / PI, 0, 1, 0);
        glDisable(GL_LIGHTING);
        drawPlate(badge, 0.0f);
        glEnable(GL_LIGHTING);
        glPopMatrix();
    }
}

void GLWidget::drawTruck(float x, float z, float heading, float spin, float r, float g, float b,
                        const RoadPose &pose)
{
    glPushMatrix();
    glTranslatef(x, pose.y, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);
    glRotatef(pose.pitch, 1, 0, 0);
    glRotatef(pose.roll, 0, 0, 1);

    for (float wz : {-2.1f, 0.9f, 2.2f}) {
        drawWheel(-1.25f, 0.48f, wz, 0.0f, spin, -1.0f, 0.48f);
        drawWheel( 1.25f, 0.48f, wz, 0.0f, spin,  1.0f, 0.48f);
    }

    glColor3f(r, g, b);
    boxAt(0, 1.45f, -2.0f, 1.15f, 0.95f, 1.1f);
    glColor3f(0.70f, 0.85f, 0.95f);
    boxAt(0, 1.75f, -3.12f, 0.95f, 0.45f, 0.02f);
    boxAt(-1.17f, 1.75f, -2.1f, 0.02f, 0.45f, 0.75f);
    boxAt( 1.17f, 1.75f, -2.1f, 0.02f, 0.45f, 0.75f);

    glColor3f(0.88f, 0.89f, 0.92f);
    boxAt(0, 1.85f, 0.95f, 1.20f, 1.35f, 2.35f);

    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 0.85f);
    boxAt(-0.75f, 0.75f, -3.12f, 0.25f, 0.12f, 0.02f);
    boxAt( 0.75f, 0.75f, -3.12f, 0.25f, 0.12f, 0.02f);
    glColor3f(1.0f, 0.10f, 0.10f);
    boxAt(-0.85f, 0.85f,  3.32f, 0.25f, 0.12f, 0.02f);
    boxAt( 0.85f, 0.85f,  3.32f, 0.25f, 0.12f, 0.02f);
    glEnable(GL_LIGHTING);

    glPopMatrix();
}

// ===========================================================================
//  HUD & 2D OVERLAYS
// ===========================================================================
float GLWidget::textWidth(const char *s, float ps) const
{
    int n = int(std::strlen(s));
    return (n > 0) ? float(n) * 6.0f * ps - ps : 0.0f;
}

float GLWidget::fitSize(const char *s, float ps, float maxWidth) const
{
    const float w = textWidth(s, ps);
    return (w > maxWidth && w > 0.0f) ? ps * maxWidth / w : ps;
}

void GLWidget::drawText(const char *s, float x, float y, float ps, float r, float g, float b)
{
    float cx = x;
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    for (const char *p = s; *p; ++p, cx += 6.0f * ps) {
        const unsigned char *rows = pixelGlyph(*p);
        if (!rows) continue;
        for (int ry = 0; ry < 7; ++ry)
            for (int rx = 0; rx < 5; ++rx)
                if (rows[ry] & (0x10 >> rx)) {
                    float qx = cx + rx * ps, qy = y + (6 - ry) * ps;
                    glVertex2f(qx, qy); glVertex2f(qx + ps, qy);
                    glVertex2f(qx + ps, qy + ps); glVertex2f(qx, qy + ps);
                }
    }
    glEnd();
}

void GLWidget::drawHUD(const sim::Car &me, const sim::Car &other, int playerNum, int vw, int vh)
{
    // Proportional, compact scale adapted safely to split-screen or single player
    const float ps = std::clamp(std::min(float(vw) / 280.0f, float(vh) / 200.0f), 1.6f, 2.3f);
    const float m = ps * 3.5f;
    const float bThk = std::max(1.0f, ps * 0.5f);
    char buf[64];

    auto rect = [](float x0, float y0, float x1, float y1) {
        glBegin(GL_QUADS);
        glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
        glEnd();
    };

    // =======================================================================
    // 1. TOP-LEFT: COMPACT RACE STATUS BADGE (P1/P2, Position, Lap, Distance)
    // =======================================================================
    (void)other;
    static const char *ORD[sim::MAX_RACERS] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH"};
    const auto &hudCars = simEngine.getCars();
    const int myIdx = (&me >= hudCars.data() && &me < hudCars.data() + hudCars.size()) ? int(&me - hudCars.data()) : 0;
    const char *posStr = ORD[std::clamp(simEngine.rankOf(myIdx), 1, sim::MAX_RACERS) - 1];
    char pTagBuf[8];
    snprintf(pTagBuf, sizeof pTagBuf, "P%d", playerNum);
    const char *pTag = pTagBuf;

    const int laps = simEngine.getLaps();
    int lapNum = std::clamp(me.lap + 1, 1, laps);
    int remDist = int(std::max(0.0f, simEngine.raceDistance() - me.progress));

    char statusBuf[64];
    if (simEngine.sprint()) snprintf(statusBuf, sizeof statusBuf, "%s %s  •  SPRINT  •  %dM", pTag, posStr, remDist);
    else                    snprintf(statusBuf, sizeof statusBuf, "%s %s  •  LAP %d/%d  •  %dM", pTag, posStr, lapNum, laps, remDist);

    float statPs = ps * 1.15f;
    float statW = textWidth(statusBuf, statPs) + ps * 6.0f;
    float statH = 7 * statPs + ps * 5.0f;
    float statX = m;
    float statY = vh - m - statH;

    // Translucent dark background pill
    glColor4f(0.04f, 0.06f, 0.10f, 0.65f);
    rect(statX, statY, statX + statW, statY + statH);

    // Subtle border
    if (simEngine.rankOf(myIdx) == 1) glColor4f(1.0f, 0.85f, 0.20f, 0.85f);   // gold border when leading
    else              glColor4f(0.30f, 0.75f, 0.95f, 0.75f);
    rect(statX, statY, statX + statW, statY + bThk);
    rect(statX, statY + statH - bThk, statX + statW, statY + statH);
    rect(statX, statY, statX + bThk, statY + statH);
    rect(statX + statW - bThk, statY, statX + statW, statY + statH);

    // Text rendering
    drawText(statusBuf, statX + ps * 3.0f, statY + ps * 2.5f, statPs, 1.0f, 1.0f, 1.0f);

    // Your place and health along the top, in the open space right of the race badge (your
    // car has no name plate): POSITION 3RD / 6, then ten blocks of health (green / yellow /
    // red, blinking when nearly gone)
    {
        const float rowH = 7.0f * ps + ps * 4.0f, rowW = std::max(statW, 150.0f * ps / 1.6f);   // the lines at the top left
        const float hps = std::min(statPs, ps * 1.15f);
        const float topX0 = statX + statW + 10.0f * ps;
        const float topX1 = std::max(topX0 + 170.0f * hps, vw * 0.72f);   // (clear of the map on the right)
        const float boxH = statH, by0 = statY;            // as tall as the badge, beside it
        char posBuf[24];
        snprintf(posBuf, sizeof posBuf, "POSITION %s / %d", posStr, int(hudCars.size()));
        const float posW = textWidth("POSITION 6TH / 6", hps) + hps * 8.0f;
        glColor4f(0.04f, 0.06f, 0.10f, 0.75f);
        rect(topX0, by0, topX0 + posW, by0 + boxH);
        const bool lead = simEngine.rankOf(myIdx) == 1;
        drawText(posBuf, topX0 + hps * 3.0f, by0 + (boxH - 7.0f * hps) * 0.5f, hps, 1.0f, lead ? 0.85f : 1.0f, lead ? 0.25f : 1.0f);
        const float hx0 = topX0 + posW + 6.0f * ps, hy0 = by0, hy1 = by0 + boxH, hpW = topX1 - hx0;
        const float ps = hps;                            // (the health line in the top bar's size)
        glColor4f(0.04f, 0.06f, 0.10f, 0.75f);
        rect(hx0, hy0, hx0 + hpW, hy1);
        const float hp = me.wreck > 0 ? 0.0f : me.health;
        const bool blink = hp < 25.0f && (simEngine.getTick() / 8) % 2 == 0;
        drawText("HP", hx0 + ps * 3.0f, hy0 + (boxH - 7.0f * ps) * 0.5f, ps, 0.80f, 0.86f, 0.95f);
        char hpBuf[16];
        if (me.wreck > 0) snprintf(hpBuf, sizeof hpBuf, "WRECKED");
        else              snprintf(hpBuf, sizeof hpBuf, "%d", int(std::ceil(hp)));
        const float numW = textWidth("WRECKED", ps);
        drawText(hpBuf, hx0 + hpW - ps * 3.0f - textWidth(hpBuf, ps), hy0 + (boxH - 7.0f * ps) * 0.5f, ps,
                 me.wreck > 0 ? 1.0f : 0.95f, me.wreck > 0 ? 0.35f : 0.95f, me.wreck > 0 ? 0.30f : 0.95f);
        const float bx0 = hx0 + ps * 3.0f + textWidth("HP ", ps), bx1 = hx0 + hpW - ps * 5.0f - numW;
        const float segW = (bx1 - bx0) / 10.0f;
        for (int i = 0; i < 10; ++i) {
            const float f = std::clamp(hp / 10.0f - float(i), 0.0f, 1.0f);
            glColor4f(0.15f, 0.18f, 0.24f, 0.9f);
            rect(bx0 + i * segW + ps * 0.5f, hy0 + ps * 2.0f, bx0 + (i + 1) * segW - ps * 0.5f, hy1 - ps * 2.0f);
            if (f <= 0.0f) continue;
            if (hp > 60.0f)      glColor4f(0.30f, 0.95f, 0.40f, 1.0f);
            else if (hp > 30.0f) glColor4f(1.00f, 0.80f, 0.20f, 1.0f);
            else                 glColor4f(blink ? 1.0f : 0.80f, blink ? 0.45f : 0.15f, 0.10f, 1.0f);
            rect(bx0 + i * segW + ps * 0.5f, hy0 + ps * 2.0f, bx0 + i * segW + ps * 0.5f + (segW - ps) * f, hy1 - ps * 2.0f);
        }
        // at the top left, under the lap time: the L boost (not in split screen) - how long it
        // has been held and what it costs now (green, yellow after 3 s, red and blinking from
        // 10 s); then, after a wreck, the distance in which another one would eliminate you
        {
        const float ps = hps / 0.85f, hpH = rowH, hpW = rowW, hx0 = statX;
        float ly1 = statY - 7.0f * ps * 0.85f - ps * 6.0f;
        if (simEngine.boostAllowed() && me.wreck == 0) {
            const float ly0 = ly1 - hpH;
            glColor4f(0.04f, 0.06f, 0.10f, 0.65f);
            rect(hx0, ly0, hx0 + hpW, ly1);
            char lBuf[40];
            const int stage = sim::GameSim::boostStage(me);
            float lr = 0.35f, lg = 0.90f, lb = 1.0f;
            if (me.boosting) {
                if (stage == 2) snprintf(lBuf, sizeof lBuf, "OVERHEAT! -%d HP/S  %.1fS", int(sim::GameSim::BOOST_RATE[2]), me.boostHold / 60.0f);
                else snprintf(lBuf, sizeof lBuf, "BOOST %.1fS -%g HP/S", me.boostHold / 60.0f, double(sim::GameSim::BOOST_RATE[stage]));
                const bool on = (simEngine.getTick() / 6) % 2 == 0;
                if (stage == 0)      { lr = 0.40f; lg = 1.0f; lb = 0.55f; }
                else if (stage == 1) { lr = 1.0f; lg = 0.85f; lb = 0.25f; }
                else                 { lr = 1.0f; lg = on ? 0.30f : 0.10f; lb = on ? 0.25f : 0.10f; }
                const float f = std::min(1.0f, me.boostHold / float(sim::GameSim::BOOST_BURN));   // a heat bar under the text: full = burnt out
                glColor4f(lr, lg, lb, 0.35f);
                rect(hx0, ly0, hx0 + hpW * f, ly0 + ps * 1.5f);
            } else if (me.boostHold > 0) {
                snprintf(lBuf, sizeof lBuf, "HOLD L: BOOST (COOLING)"); lr = lg = lb = 0.60f;
            } else {
                snprintf(lBuf, sizeof lBuf, "HOLD L: BOOST");
            }
            drawText(lBuf, hx0 + ps * 3.0f, ly0 + ps * 2.0f, ps, lr, lg, lb);
            ly1 = ly0 - ps * 2.0f;
        }
        // the shortcut ahead: closed while you are in the top 3 (GameSim::SHORTCUT_CLOSED_TOP)
        if (me.wreck == 0 && simEngine.toShortcut(me) < 300.0f && simEngine.toShortcut(me) > -40.0f) {
            const float ly0 = ly1 - hpH;
            glColor4f(0.04f, 0.06f, 0.10f, 0.65f);
            rect(hx0, ly0, hx0 + hpW, ly1);
            char sBuf[48];
            if (me.cutClosed) snprintf(sBuf, sizeof sBuf, "SHORTCUT CLOSED: TOP %d", sim::GameSim::SHORTCUT_CLOSED_TOP);
            else snprintf(sBuf, sizeof sBuf, "SHORTCUT OPEN");
            drawText(sBuf, hx0 + ps * 3.0f, ly0 + ps * 2.0f, ps, me.cutClosed ? 1.0f : 0.40f, me.cutClosed ? 0.40f : 1.0f,
                     me.cutClosed ? 0.30f : 0.55f);
            ly1 = ly0 - ps * 2.0f;
        }
        }
    }

    // this lap's time and the best lap, under the badge
    if (simEngine.getState() != State::Countdown) {
        auto mmss = [](float t, char *buf, size_t n) {
            const int m = int(t) / 60;
            snprintf(buf, n, "%d:%04.1f", m, t - float(m) * 60.0f);
        };
        char nowBuf[16], bestBuf[16], lapLine[48];
        const bool timing = me.lap >= 0 && me.lapStart >= 0.0f && !me.finished;
        if (timing) mmss(simEngine.getRaceTimer() - me.lapStart, nowBuf, sizeof nowBuf);
        else        snprintf(nowBuf, sizeof nowBuf, "-:--.-");
        if (me.bestLap > 0.0f) mmss(me.bestLap, bestBuf, sizeof bestBuf);
        else                   snprintf(bestBuf, sizeof bestBuf, "-:--.-");
        snprintf(lapLine, sizeof lapLine, "LAP TIME %s   BEST %s", nowBuf, bestBuf);
        if (simEngine.sprint()) {                        // a sprint: the race time so far
            mmss(me.finished ? me.finishTime : simEngine.getRaceTimer(), nowBuf, sizeof nowBuf);
            snprintf(lapLine, sizeof lapLine, "RACE TIME %s", nowBuf);
        }
        const float lps = ps * 0.85f;
        drawText(lapLine, statX + ps * 3.0f, statY - 7.0f * lps - ps * 3.0f, lps, 0.80f, 0.90f, 1.0f);
    }

    // =======================================================================
    // 2. BOTTOM-LEFT: RETRO-FUTURISTIC ARCADE SPEEDOMETER & NITRO GAUGE
    // =======================================================================
    // Compact scale: small & unobtrusive, leaving the road fully visible
    static const unsigned char SEVEN_SEG_DIGITS[10] = {
        0x3F, // 0: A B C D E F
        0x06, // 1: B C
        0x5B, // 2: A B D E G
        0x4F, // 3: A B C D G
        0x66, // 4: B C F G
        0x6D, // 5: A C D F G
        0x7D, // 6: A C D E F G
        0x07, // 7: A B C
        0x7F, // 8: A B C D E F G
        0x6F  // 9: A B C D F G
    };

    float s = std::clamp(std::min(float(vw) / 480.0f, float(vh) / 360.0f), 0.70f, 0.95f);
    float cardW = 124.0f * s;
    float cardH = 74.0f * s;
    float cardX = m;
    float cardY = m;

    // Draw dark rounded card background (navy slate)
    auto drawCardPoly = [](float x, float y, float w, float h, float r) {
        const int segs = 4;
        glColor4f(0.04f, 0.07f, 0.12f, 0.88f);
        glBegin(GL_POLYGON);
        for (int i = 0; i <= segs; ++i) {
            float a = PI + (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + r + std::cos(a) * r, y + r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = 1.5f * PI + (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + w - r + std::cos(a) * r, y + r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + w - r + std::cos(a) * r, y + h - r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = 0.5f * PI + (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + r + std::cos(a) * r, y + h - r + std::sin(a) * r);
        }
        glEnd();
    };
    drawCardPoly(cardX, cardY, cardW, cardH, 6.0f * s);

    // Glowing electric cyan outer bezel border
    auto drawCardBorder = [this](float x, float y, float w, float h, float r, float cr, float cg, float cb, float ca, float lw) {
        glLineWidth(lw);
        glColor4f(cr, cg, cb, ca);
        glBegin(GL_LINE_LOOP);
        const int segs = 4;
        for (int i = 0; i <= segs; ++i) {
            float a = PI + (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + r + std::cos(a) * r, y + r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = 1.5f * PI + (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + w - r + std::cos(a) * r, y + r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + w - r + std::cos(a) * r, y + h - r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = 0.5f * PI + (PI * 0.5f) * (float(i) / segs);
            glVertex2f(x + r + std::cos(a) * r, y + h - r + std::sin(a) * r);
        }
        glEnd();
        glLineWidth(1.0f);
    };
    // Soft outer neon glow
    drawCardBorder(cardX, cardY, cardW, cardH, 6.0f * s, 0.0f, 0.82f, 1.0f, 0.28f, 2.5f);
    // Crisp neon cyan border
    drawCardBorder(cardX, cardY, cardW, cardH, 6.0f * s, 0.25f, 0.95f, 1.0f, 0.95f, 1.2f);

    // Faint subtle high-tech cockpit grid lines
    glColor4f(0.12f, 0.22f, 0.35f, 0.16f);
    glBegin(GL_LINES);
    for (float gy = cardY + 16.0f * s; gy < cardY + cardH - 10.0f * s; gy += 14.0f * s) {
        glVertex2f(cardX + 6.0f * s, gy); glVertex2f(cardX + cardW - 6.0f * s, gy);
    }
    for (float gx = cardX + 24.0f * s; gx < cardX + cardW - 10.0f * s; gx += 24.0f * s) {
        glVertex2f(gx, cardY + 12.0f * s); glVertex2f(gx, cardY + cardH - 12.0f * s);
    }
    glEnd();

    // Top Header: Nitro / Boost Status (Left) & "ARCADE GP" (Right)
    const char *boostKey = (playerNum == 1) ? "ENTER" : "SPACE";
    char topBuf[32];
    float tr = 0.40f, tg = 0.55f, tb = 0.65f;
    if (me.nitroOn) {
        float secs = std::max(0.0f, float(me.nitroTimer) / 60.0f);
        snprintf(topBuf, sizeof topBuf, "⚡ BOOST %.1fS", secs);
        tr = 1.0f; tg = 0.88f; tb = 0.20f;
    } else if (me.nitroCharges > 0 || me.nitro > 0.01f) {
        snprintf(topBuf, sizeof topBuf, "⚡ NITRO [%s]", boostKey);
        tr = 0.25f; tg = 0.95f; tb = 1.0f;
    } else {
        snprintf(topBuf, sizeof topBuf, "NITRO: EMPTY");
    }
    float topPs = s * 0.82f;
    drawText(topBuf, cardX + 7.0f * s, cardY + cardH - 11.0f * s, topPs, tr, tg, tb);
    drawText("ARCADE GP", cardX + cardW - 37.0f * s, cardY + cardH - 11.0f * s, topPs, 0.35f, 0.88f, 1.0f);

    // Digital 7-Segment Speed Display
    int kmh = std::clamp(int(std::fabs(me.speed) / sim::NITRO_SPEED * 240.0f + 0.5f), 0, 260);
    int d100 = (kmh / 100) % 10;
    int d10  = (kmh / 10) % 10;
    int d1   = kmh % 10;

    float digX = cardX + 16.0f * s;
    float digY = cardY + 34.0f * s;
    float dw = 11.5f * s;
    float dh = 22.0f * s;
    float dt = 2.4f * s;
    float dsl = 2.2f * s;
    float dgap = 3.5f * s;

    // 7-segment digit drawing helper
    auto draw7Seg = [&](float x, float y, int digit, bool showDim) {
        unsigned char mask = (digit >= 0 && digit <= 9) ? SEVEN_SEG_DIGITS[digit] : 0x00;
        float midY = y + dh * 0.5f;

        auto quad = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, bool on) {
            if (on) {
                glColor4f(1.0f, 1.0f, 1.0f, 0.98f);
            } else if (showDim) {
                glColor4f(0.12f, 0.18f, 0.28f, 0.30f);
            } else {
                return;
            }
            glBegin(GL_QUADS);
            glVertex2f(x0, y0); glVertex2f(x1, y1);
            glVertex2f(x2, y2); glVertex2f(x3, y3);
            glEnd();
        };

        // Seg A (top)
        quad(x + dt * 0.8f + dsl * (1.0f - dt/dh), y + dh - dt,
             x + dw - dt * 0.8f + dsl * (1.0f - dt/dh), y + dh - dt,
             x + dw - dt * 0.3f + dsl, y + dh,
             x + dt * 0.3f + dsl, y + dh,
             mask & 0x01);

        // Seg B (top-right)
        quad(x + dw - dt + dsl * 0.5f, midY + dt * 0.35f,
             x + dw + dsl * 0.5f, midY + dt * 0.35f,
             x + dw + dsl * (1.0f - dt * 0.35f/dh), y + dh - dt * 0.35f,
             x + dw - dt + dsl * (1.0f - dt * 0.35f/dh), y + dh - dt * 0.35f,
             mask & 0x02);

        // Seg C (bottom-right)
        quad(x + dw - dt + dsl * (dt * 0.35f/dh), y + dt * 0.35f,
             x + dw + dsl * (dt * 0.35f/dh), y + dt * 0.35f,
             x + dw + dsl * 0.5f, midY - dt * 0.35f,
             x + dw - dt + dsl * 0.5f, midY - dt * 0.35f,
             mask & 0x04);

        // Seg D (bottom)
        quad(x + dt * 0.3f, y,
             x + dw - dt * 0.3f, y,
             x + dw - dt * 0.8f + dsl * (dt/dh), y + dt,
             x + dt * 0.8f + dsl * (dt/dh), y + dt,
             mask & 0x08);

        // Seg E (bottom-left)
        quad(x + dsl * (dt * 0.35f/dh), y + dt * 0.35f,
             x + dt + dsl * (dt * 0.35f/dh), y + dt * 0.35f,
             x + dt + dsl * 0.5f, midY - dt * 0.35f,
             x + dsl * 0.5f, midY - dt * 0.35f,
             mask & 0x10);

        // Seg F (top-left)
        quad(x + dsl * 0.5f, midY + dt * 0.35f,
             x + dt + dsl * 0.5f, midY + dt * 0.35f,
             x + dt + dsl * (1.0f - dt * 0.35f/dh), y + dh - dt * 0.35f,
             x + dsl * (1.0f - dt * 0.35f/dh), y + dh - dt * 0.35f,
             mask & 0x20);

        // Seg G (middle)
        quad(x + dt * 0.8f + dsl * 0.5f, midY - dt * 0.45f,
             x + dw - dt * 0.8f + dsl * 0.5f, midY - dt * 0.45f,
             x + dw - dt * 0.8f + dsl * 0.5f, midY + dt * 0.45f,
             x + dt * 0.8f + dsl * 0.5f, midY + dt * 0.45f,
             mask & 0x40);
    };

    // Hundreds digit (dim 8 when < 100)
    draw7Seg(digX, digY, (kmh >= 100 ? d100 : -1), true);
    // Tens digit (dim 8 when < 10)
    draw7Seg(digX + dw + dgap, digY, (kmh >= 10 ? d10 : -1), true);
    // Ones digit (always lit)
    draw7Seg(digX + (dw + dgap) * 2.0f, digY, d1, true);

    // Orange "KM/H" Pill Badge to the right of speed digits
    float badgeX = digX + (dw + dgap) * 3.0f + 3.0f * s;
    float badgeY = digY + 6.0f * s;
    float badgeW = 25.0f * s;
    float badgeH = 12.0f * s;

    // Orange badge background
    glColor4f(1.0f, 0.58f, 0.05f, 0.95f);
    rect(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
    // Amber top highlight border
    glColor4f(1.0f, 0.80f, 0.25f, 0.95f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(badgeX, badgeY); glVertex2f(badgeX + badgeW, badgeY);
    glVertex2f(badgeX + badgeW, badgeY + badgeH); glVertex2f(badgeX, badgeY + badgeH);
    glEnd();

    // Dark bold slanted "KM/H" text
    drawText("KM/H", badgeX + 2.2f * s, badgeY + 2.5f * s, s * 0.88f, 0.12f, 0.06f, 0.02f);

    // Curved "Smile" LED Arc Gauge underneath
    float arcX0 = cardX + 11.0f * s;
    float arcX1 = cardX + cardW - 11.0f * s;
    float arcSpan = arcX1 - arcX0;
    const int numTicks = 24;
    float tickLen = 5.4f * s;
    float tickThk = 1.7f * s;
    float spFrac = std::clamp(std::fabs(me.speed) / sim::NITRO_SPEED, 0.0f, 1.0f);

    struct ArcPoint { float tx, ty, bx, by; };
    ArcPoint pts[numTicks];

    for (int i = 0; i < numTicks; ++i) {
        float u = float(i) / float(numTicks - 1);
        float c = 2.0f * (u - 0.5f); // -1.0 to +1.0
        float txPos = arcX0 + u * arcSpan;
        float tyPos = (cardY + 16.0f * s) + (10.5f * s) * (c * c);

        float dy_dx = (21.0f * s * c) / arcSpan;
        float angle = std::atan(dy_dx);
        float nx = -std::sin(angle), ny = std::cos(angle);
        float txDir = std::cos(angle), tyDir = std::sin(angle);

        // Store contour points
        pts[i].tx = txPos + nx * (tickLen * 0.5f + 1.2f * s);
        pts[i].ty = tyPos + ny * (tickLen * 0.5f + 1.2f * s);
        pts[i].bx = txPos - nx * (tickLen * 0.5f + 1.2f * s);
        pts[i].by = tyPos - ny * (tickLen * 0.5f + 1.2f * s);

        // Color based on speed position (Cyan -> Lime -> Amber -> Red)
        float rCol, gCol, bCol;
        if (u < 0.28f) {
            rCol = 0.00f; gCol = 0.90f; bCol = 1.00f; // Cyan
        } else if (u < 0.62f) {
            rCol = 0.22f; gCol = 0.98f; bCol = 0.25f; // Lime
        } else if (u < 0.84f) {
            rCol = 1.00f; gCol = 0.82f; bCol = 0.12f; // Amber
        } else {
            rCol = 1.00f; gCol = 0.18f; bCol = 0.24f; // Red
        }

        bool isLit = (u <= spFrac + (0.5f / float(numTicks - 1)));
        if (isLit) {
            glColor4f(rCol, gCol, bCol, 0.95f);
        } else {
            glColor4f(0.10f, 0.16f, 0.24f, 0.35f);
        }

        // Draw segmented tick quad
        float hx = nx * (tickLen * 0.5f), hy = ny * (tickLen * 0.5f);
        float wx = txDir * (tickThk * 0.5f), wy = tyDir * (tickThk * 0.5f);
        glBegin(GL_QUADS);
        glVertex2f(txPos - hx - wx, tyPos - hy - wy);
        glVertex2f(txPos - hx + wx, tyPos - hy + wy);
        glVertex2f(txPos + hx + wx, tyPos + hy + wy);
        glVertex2f(txPos + hx - wx, tyPos + hy - wy);
        glEnd();
    }

    // Outer glowing cyan contour capsule around the LED smile arc
    glLineWidth(1.2f);
    glColor4f(0.20f, 0.92f, 1.0f, 0.80f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < numTicks; ++i) glVertex2f(pts[i].tx, pts[i].ty);
    for (int i = numTicks - 1; i >= 0; --i) glVertex2f(pts[i].bx, pts[i].by);
    glVertex2f(pts[0].tx, pts[0].ty);
    glEnd();
    glLineWidth(1.0f);

    // Scale reference labels
    drawText("0", arcX0 - 1.0f * s, cardY + 16.5f * s, s * 0.80f, 0.70f, 0.82f, 0.95f);
    drawText("120", arcX0 + 0.5f * arcSpan - 7.5f * s, cardY + 7.0f * s, s * 0.80f, 0.70f, 0.82f, 0.95f);
    drawText("240+", arcX1 - 13.5f * s, cardY + 16.5f * s, s * 0.80f, 0.70f, 0.82f, 0.95f);
    drawText("ARCADE GP", cardX + cardW - 37.0f * s, cardY + 4.2f * s, s * 0.65f, 0.25f, 0.40f, 0.55f);

    // Drift Indicator (floats right above the speedometer card)
    if (me.isDrifting) {
        const char *driftTxt = "★ DRIFT ★";
        float driftPs = s * 0.95f;
        drawText(driftTxt, cardX + (cardW - textWidth(driftTxt, driftPs)) * 0.5f, cardY + cardH + 4.0f * s, driftPs, 1.0f, 0.88f, 0.20f);
    }

    // Nitro collection notification (brief pop-up above speedometer card)
    int carIdx = playerNum - 1;
    if (carIdx >= 0 && carIdx < sim::MAX_RACERS && nitroBannerTimer[carIdx] > 0.0f) {
        const char *alertTxt = (playerNum == 1) ? "+1 NITRO! [ENTER]" : "+1 NITRO! [SPACE]";
        float aPs = s * 1.05f;
        float ax = cardX;
        float ay = cardY + cardH + (me.isDrifting ? 14.0f * s : 4.0f * s);
        drawText(alertTxt, ax, ay, aPs, 0.30f, 1.0f, 0.90f);
    }

    // =======================================================================
    // 3. SLEEK 1-LINE "⚡ NITRO BOOST" POPUP CAPSULE (Active during 2.0s boost)
    // =======================================================================
    if (me.nitroOn && me.nitroTimer > 0) {
        float bScale = std::clamp(std::min(float(vw) / 500.0f, float(vh) / 380.0f), 0.72f, 1.0f);
        float txtPs = bScale * 1.30f;
        float boltW = 9.0f * bScale;
        float boltH = 13.0f * bScale;
        float gap = 5.5f * bScale;
        float nitroW = textWidth("NITRO", txtPs);
        float spaceW = 6.0f * txtPs;
        float boostW = textWidth("BOOST", txtPs);
        float padX = 11.0f * bScale;

        float pillW = padX * 2.0f + boltW + gap + nitroW + spaceW + boostW;
        float pillH = 22.0f * bScale;
        float pillX = (vw - pillW) * 0.5f;
        float pillY = vh * 0.73f;
        float pillR = pillH * 0.5f;

        float alpha = std::clamp(float(me.nitroTimer) / 10.0f, 0.0f, 1.0f);
        float pulse = 0.92f + 0.08f * std::sin(float(me.nitroTimer) * 0.25f);

        // Dark capsule background
        const int segs = 8;
        glColor4f(0.04f, 0.07f, 0.12f, 0.90f * alpha);
        glBegin(GL_POLYGON);
        for (int i = 0; i <= segs; ++i) {
            float a = 0.5f * PI + PI * (float(i) / segs);
            glVertex2f(pillX + pillR + std::cos(a) * pillR, pillY + pillR + std::sin(a) * pillR);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = -0.5f * PI + PI * (float(i) / segs);
            glVertex2f(pillX + pillW - pillR + std::cos(a) * pillR, pillY + pillR + std::sin(a) * pillR);
        }
        glEnd();

        // Glowing electric cyan capsule outline
        auto drawPillBorder = [&](float cr, float cg, float cb, float ca, float lw) {
            glLineWidth(lw);
            glColor4f(cr, cg, cb, ca);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i <= segs; ++i) {
                float a = 0.5f * PI + PI * (float(i) / segs);
                glVertex2f(pillX + pillR + std::cos(a) * pillR, pillY + pillR + std::sin(a) * pillR);
            }
            for (int i = 0; i <= segs; ++i) {
                float a = -0.5f * PI + PI * (float(i) / segs);
                glVertex2f(pillX + pillW - pillR + std::cos(a) * pillR, pillY + pillR + std::sin(a) * pillR);
            }
            glEnd();
            glLineWidth(1.0f);
        };
        // Outer soft glow
        drawPillBorder(0.0f, 0.85f, 1.0f, 0.32f * alpha * pulse, 3.0f);
        // Inner crisp cyan line
        drawPillBorder(0.20f, 0.95f, 1.0f, 0.95f * alpha, 1.4f);

        // Golden yellow lightning bolt icon
        float bx = pillX + padX;
        float by = pillY + (pillH - boltH) * 0.5f;
        glColor4f(1.0f, 0.88f, 0.22f, alpha * pulse);
        glBegin(GL_TRIANGLES);
        glVertex2f(bx + boltW * 0.65f, by + boltH);
        glVertex2f(bx + boltW * 0.15f, by + boltH * 0.45f);
        glVertex2f(bx + boltW * 0.55f, by + boltH * 0.45f);

        glVertex2f(bx + boltW * 0.45f, by + boltH * 0.55f);
        glVertex2f(bx + boltW * 0.30f, by);
        glVertex2f(bx + boltW * 0.85f, by + boltH * 0.55f);
        glEnd();

        glBegin(GL_QUADS);
        glVertex2f(bx + boltW * 0.25f, by + boltH * 0.45f);
        glVertex2f(bx + boltW * 0.55f, by + boltH * 0.45f);
        glVertex2f(bx + boltW * 0.75f, by + boltH * 0.55f);
        glVertex2f(bx + boltW * 0.45f, by + boltH * 0.55f);
        glEnd();

        // Text: "NITRO" (cyan) and "BOOST" (golden-yellow)
        float tx = bx + boltW + gap;
        float ty = pillY + (pillH - 7.0f * txtPs) * 0.5f;
        drawText("NITRO", tx, ty, txtPs, 0.20f * pulse, 0.92f * pulse, 1.0f);
        tx += nitroW + spaceW;
        drawText("BOOST", tx, ty, txtPs, 1.0f, 0.90f * pulse, 0.30f * pulse);
    }

    // Crashes: what happened to your car (a few lines, newest at the bottom), WRECKED with
    // the time until you are back, and everyone's wrecks on the right
    if (myIdx < sim::MAX_RACERS) {
        std::deque<HudMsg> &q = carMsgs[myIdx];
        while (!q.empty() && q.front().until < simEngine.getTick()) q.pop_front();
        float my = vh * 0.20f;
        const float mps = ps * 1.25f;
        for (auto it = q.rbegin(); it != q.rend(); ++it, my += 9.0f * mps) {
            const char *t = it->text.constData();
            drawText(t, (vw - textWidth(t, mps)) * 0.5f, my, mps, it->r, it->g, it->b);
        }
    }
    drawTauntBubble(vw, vh, ps);
    if (me.wreck > 0) {
        const float s1 = ps * 4.0f, s2 = ps * 1.8f;          // (above the car and its name plate)
        drawText("WRECKED!", (vw - textWidth("WRECKED!", s1)) * 0.5f, vh * 0.76f, s1, 1.0f, 0.30f, 0.25f);
        if (me.ejected) {
            const char *t = "YOUR DRIVER FLEW OUT!";
            drawText(t, (vw - textWidth(t, s2)) * 0.5f, vh * 0.71f, s2, 1.0f, 0.85f, 0.35f);
        }
        char back[48];
        snprintf(back, sizeof back, "BACK IN %d", (simEngine.wreckTime(me) - me.wreck + 59) / 60);
        drawText(back, (vw - textWidth(back, s2)) * 0.5f, vh * (me.ejected ? 0.665f : 0.71f), s2, 0.95f, 0.95f, 0.95f);
    }
    {
        while (!feed.empty() && feed.front().until < simEngine.getTick()) feed.pop_front();
        float fy = vh * 0.56f;
        const float fps = ps * 1.0f;
        for (auto it = feed.rbegin(); it != feed.rend(); ++it, fy -= 9.0f * fps + ps * 3.0f) {
            const char *t = it->text.constData();
            const float tw = textWidth(t, fps);
            glColor4f(0.04f, 0.06f, 0.10f, 0.60f);
            rect(vw - m - tw - ps * 6.0f, fy - ps * 2.0f, vw - m, fy + 7.0f * fps + ps * 2.0f);
            drawText(t, vw - m - tw - ps * 3.0f, fy, fps, it->r, it->g, it->b);
        }
    }

    // Warnings / messages (Centered)
    bool offroad = std::fabs(me.d) > simEngine.roadHalfAt(me.s);
    if (offroad && simEngine.getState() == State::Racing && (simEngine.getTick() / 15) % 2) {
        float s = ps * 1.6f;
        drawText("OFF ROAD", (vw - textWidth("OFF ROAD", s)) * 0.5f, vh * 0.30f, s, 1.0f, 0.55f, 0.1f);
    }
    // a new lap: "LAP 2/3" or "FINAL LAP" for the first 120 m after the line
    if (simEngine.getState() == State::Racing && !me.finished && me.lap >= 1 && me.lap < laps && me.s < 120.0f) {
        char lapBuf[24];
        if (me.lap + 1 == laps) {
            snprintf(lapBuf, sizeof lapBuf, "FINAL LAP");
            const char *w = "MORE OBSTACLES AND RAMPS AHEAD!";
            drawText(w, (vw - textWidth(w, ps * 1.4f)) * 0.5f, vh * 0.62f, ps * 1.4f, 1.0f, 0.55f, 0.30f);
        }
        else                    snprintf(lapBuf, sizeof lapBuf, "LAP %d/%d", me.lap + 1, laps);
        float s = ps * 2.2f;
        drawText(lapBuf, (vw - textWidth(lapBuf, s)) * 0.5f, vh * 0.66f, s, 1.0f, 0.85f, 0.25f);
    }
    if (me.path == 1 && me.s < 80.0f && (simEngine.getTick() / 12) % 2) {   // just onto the shortcut
        float s = ps * 2.0f;
        drawText("SHORTCUT", (vw - textWidth("SHORTCUT", s)) * 0.5f, vh * 0.58f, s, 0.95f, 0.78f, 0.45f);
    }
    // a long flight: "BIG AIR 1.2 S" for a moment after landing
    if (myIdx < sim::MAX_RACERS && carFx[myIdx].bigAirUntil > simEngine.getTick()) {
        char airBuf[24];
        snprintf(airBuf, sizeof airBuf, "BIG AIR %.1f S", carFx[myIdx].bigAir);
        float s = ps * 2.0f;
        drawText(airBuf, (vw - textWidth(airBuf, s)) * 0.5f, vh * 0.58f, s, 0.35f, 0.95f, 1.0f);
    }
    if (simEngine.getState() == State::Countdown) {
        snprintf(buf, sizeof buf, "%d", 3 - simEngine.getTick() / 60);
        float s = ps * 6.0f;
        drawText(buf, (vw - textWidth(buf, s)) * 0.5f, vh * 0.52f, s, 1.0f, 0.85f, 0.2f);
        const char *hint = (simEngine.getGameMode() == sim::GameSim::ModeSingle) ? "ARROWS + ENTER NITRO"
                         : isOnline() ? "ARROWS/WASD + ENTER/SPACE NITRO"
                         : (playerNum == 2 ? "WASD + SPACE NITRO" : "ARROWS + ENTER NITRO");
        float hs = ps * 1.15f;
        drawText(hint, (vw - textWidth(hint, hs)) * 0.5f, vh * 0.42f, hs, 1.0f, 1.0f, 1.0f);
    } else if (simEngine.getState() == State::Racing && simEngine.getRaceTimer() < 1.0f) {
        float s = ps * 6.0f;
        drawText("GO!", (vw - textWidth("GO!", s)) * 0.5f, vh * 0.52f, s, 0.3f, 1.0f, 0.4f);
    } else if (simEngine.getWinner() != 0 && !isOnline() && !uiTestOnlineBanner && simEngine.getGameMode() == sim::GameSim::ModeSingle
               && !me.finished) {
        // a CPU won, but your race goes on until you cross the line
        const int winner = simEngine.getWinner();
        QByteArray msg = (hudCars.size() > 2 ? "CPU" + QByteArray::number(winner - 1) : QByteArray("CPU"))
                         + " WON - FINISH YOUR RACE!";
        float s = ps * 1.5f;
        drawText(msg.constData(), (vw - textWidth(msg.constData(), s)) * 0.5f, vh * 0.74f, s, 1.0f, 0.75f, 0.30f);
    } else if (simEngine.getWinner() != 0 && (isOnline() || uiTestOnlineBanner)) {
        // online: a small strip at the top - the others are still driving
        const int winner = simEngine.getWinner();
        QByteArray msg;
        float r = 1.0f, g = 0.45f, b = 0.40f;
        if (winner == playerNum)  { msg = "YOU WIN!"; r = 0.35f; g = 1.0f; b = 0.45f; }
        else if (me.finished)     { msg = QByteArray("FINISHED ") + posStr; r = 1.0f; g = 0.85f; b = 0.25f; }
        else msg = (winner - 1 < racerNames.size() ? racerNames[winner - 1] : QString("P%1").arg(winner)).toUtf8() + " WINS - FINISH YOUR RACE!";
        if (me.finished) msg += "   -   WAITING FOR THE OTHERS...";
        const float s = fitSize(msg.constData(), ps * 1.05f, vw * 0.7f);
        const float tw = textWidth(msg.constData(), s), th = 7.0f * s, y = vh * 0.72f;   // (under the drivers' chat line)
        glColor4f(0.03f, 0.05f, 0.10f, 0.72f);
        glBegin(GL_QUADS);
        glVertex2f((vw - tw) * 0.5f - th * 0.8f, y - th * 0.6f); glVertex2f((vw + tw) * 0.5f + th * 0.8f, y - th * 0.6f);
        glVertex2f((vw + tw) * 0.5f + th * 0.8f, y + th * 1.6f); glVertex2f((vw - tw) * 0.5f - th * 0.8f, y + th * 1.6f);
        glEnd();
        drawText(msg.constData(), (vw - tw) * 0.5f, y, s, r, g, b);
    } else if (simEngine.getWinner() != 0) {
        int winner = simEngine.getWinner();
        float s = ps * 3.5f;
        if (winner == playerNum) {
            const char *winMsg = "YOU WIN!";
            drawText(winMsg, (vw - textWidth(winMsg, s)) * 0.5f, vh * 0.54f, s, 0.3f, 1.0f, 0.4f);
        } else if (me.finished) {
            char finMsg[32];
            snprintf(finMsg, sizeof finMsg, "FINISHED %s!", posStr);
            drawText(finMsg, (vw - textWidth(finMsg, s)) * 0.5f, vh * 0.54f, s, 1.0f, 0.85f, 0.2f);
        } else {
            QByteArray online = (winner - 1 < racerNames.size() ? racerNames[winner - 1] : QString("P%1").arg(winner)).toUtf8() + " WINS!";
            const char *loseMsg = isOnline() ? online.constData()
                                : (simEngine.getGameMode() == sim::GameSim::ModeSingle) ? "CPU WINS!" : "OPPONENT WINS!";
            const float ls = fitSize(loseMsg, s, vw * 0.9f);
            drawText(loseMsg, (vw - textWidth(loseMsg, ls)) * 0.5f, vh * 0.54f, ls, 1.0f, 0.35f, 0.35f);
        }
        const char *rstMsg = isOnline() ? "WAITING FOR THE OTHERS..." : "PRESS [R] TO RESTART";
        float rstPs = ps * 1.10f;
        drawText(rstMsg, (vw - textWidth(rstMsg, rstPs)) * 0.5f, vh * 0.44f, rstPs, 0.9f, 0.9f, 0.9f);
    }
}

void GLWidget::drawMinimap(int W, int H, float dpr)
{
    const float mw = 150.0f * dpr;
    const float mh = std::min(172.0f * dpr, H * 0.44f);
    const float margin = 14.0f * dpr;
    const float topOffset = 14.0f * dpr;
    const float x0 = W - margin - mw, y0 = H - topOffset - mh;

    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };

    glColor4f(0.05f, 0.06f, 0.10f, 0.62f);
    rect(x0, y0, x0 + mw, y0 + mh);
    float bw = 2.0f * dpr;
    glColor4f(1.0f, 0.85f, 0.6f, 0.9f);
    rect(x0, y0, x0 + mw, y0 + bw); rect(x0, y0 + mh - bw, x0 + mw, y0 + mh);
    rect(x0, y0, x0 + bw, y0 + mh); rect(x0 + mw - bw, y0, x0 + mw, y0 + mh);

    float ps = 1.6f * dpr;
    drawText("MAP", x0 + (mw - textWidth("MAP", ps)) * 0.5f, y0 + mh - 7 * ps - 6 * dpr, ps, 1.0f, 0.9f, 0.7f);

    // the whole circuit, north up, as big as it fits
    const float pad = 10.0f * dpr;
    const float top = y0 + mh - 7 * ps - 12 * dpr, bottom = y0 + pad, left = x0 + pad, right = x0 + mw - pad;
    const auto &S = simEngine.circuit().slices();
    const int n = int(S.size());
    float ax = 1e9f, bx = -1e9f, az = 1e9f, bz = -1e9f;
    for (const sim::Slice &sl : S) {
        ax = std::min(ax, sl.x); bx = std::max(bx, sl.x);
        az = std::min(az, sl.z); bz = std::max(bz, sl.z);
    }
    const float k = std::min((right - left) / std::max(1.0f, bx - ax), (top - bottom) / std::max(1.0f, bz - az));
    const float ox = 0.5f * (left + right) - 0.5f * (ax + bx) * k, oy = 0.5f * (bottom + top) - 0.5f * (az + bz) * k;
    auto mx = [&](float wx) { return ox + wx * k; };
    auto my = [&](float wz) { return oy - wz * k; };     // world z points south

    float rw = std::max(1.5f * dpr, roadHalf() * k);
    glColor4f(0.60f, 0.60f, 0.66f, 1.0f);
    glBegin(GL_QUAD_STRIP);
    const int last = simEngine.sprint() ? n - 1 : n;     // (a loop ends where it began)
    for (int i = 0;; i = std::min(i + 10, last)) {
        const sim::Slice &sl = S[size_t(i % n)];
        const float px = ox + sl.x * k, py = oy + sl.z * k;
        glVertex2f(px - sl.tz * rw, py + sl.tx * rw);
        glVertex2f(px + sl.tz * rw, py - sl.tx * rw);
        if (i == last) break;
    }
    glEnd();

    // start/finish line (a sprint: the start line and the finish line)
    for (float lineS : {simEngine.startLine(), simEngine.sprint() ? simEngine.finishLine() : -1.0f}) {
        if (lineS < 0.0f) continue;
        const sim::Slice &sl = S[size_t(lineS)];
        const float px = ox + sl.x * k, py = oy + sl.z * k, w = rw + 2.5f * dpr, t = 1.2f * dpr;
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
        glVertex2f(px - sl.tz * w - sl.tx * t, py + sl.tx * w - sl.tz * t);
        glVertex2f(px + sl.tz * w - sl.tx * t, py - sl.tx * w - sl.tz * t);
        glVertex2f(px + sl.tz * w + sl.tx * t, py - sl.tx * w + sl.tz * t);
        glVertex2f(px - sl.tz * w + sl.tx * t, py + sl.tx * w + sl.tz * t);
        glEnd();
    }

    if (simEngine.hasShortcut()) {                       // the shortcut, thinner and sandy
        const auto &CS = simEngine.circuit().shortcut();
        glColor4f(0.85f, 0.70f, 0.48f, 1.0f);
        glLineWidth(2.0f * dpr);
        glBegin(GL_LINE_STRIP);
        for (size_t i = 0; i < CS.size(); i += 5) glVertex2f(ox + CS[i].x * k, oy + CS[i].z * k);
        glVertex2f(ox + CS.back().x * k, oy + CS.back().z * k);
        glEnd();
        glLineWidth(1.0f);
    }
    glColor4f(1.0f, 0.80f, 0.10f, 1.0f);                 // the jump ramps
    for (const sim::GameSim::Ramp &r : simEngine.ramps()) {
        float rx, rz;
        simEngine.pointAt(r.lip, 0.0f, rx, rz);
        rect(mx(rx) - 2.0f * dpr, my(rz) - 2.0f * dpr, mx(rx) + 2.0f * dpr, my(rz) + 2.0f * dpr);
    }

    // the temple
    float lx, lz, lrot;
    simEngine.landmark(lx, lz, lrot);
    const float ty = my(lz);
    glColor4f(0.95f, 0.78f, 0.30f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(mx(lx) - 5 * dpr, ty - 4 * dpr); glVertex2f(mx(lx) + 5 * dpr, ty - 4 * dpr);
    glVertex2f(mx(lx), ty + 4 * dpr);
    glEnd();

    glColor4f(0.85f, 0.85f, 0.85f, 0.75f);
    for (const sim::Traffic &t : simEngine.getTraffic()) {
        float tx, tz;
        simEngine.pointAt(t.s, t.lane, tx, tz);
        float px = mx(tx), py = my(tz), s = 1.3f * dpr;
        rect(px - s, py - s, px + s, py + s);
    }

    auto dot = [&](const sim::Car &c, const char *lbl) {
        float px = mx(c.x), py = my(c.z), s = 3.5f * dpr, o = 1.5f * dpr;
        glColor4f(1, 1, 1, 1);
        rect(px - s - o, py - s - o, px + s + o, py + s + o);
        glColor4f(c.r, c.g, c.b, 1);
        rect(px - s, py - s, px + s, py + s);
        drawText(lbl, px + s + 4 * dpr, py - 3.5f * ps, ps, std::min(1.0f, c.r + 0.3f),
                 std::min(1.0f, c.g + 0.3f), std::min(1.0f, c.b + 0.3f));
    };

    const auto &cars = simEngine.getCars();
    const int me = localCar();
    for (int i = int(cars.size()) - 1; i >= 0; --i) {       // my own dot last, so it is on top
        if (i == me && (isOnline() || simEngine.getGameMode() == sim::GameSim::ModeSingle)) continue;
        char lbl[8];
        if (simEngine.getGameMode() == sim::GameSim::ModeSingle && i >= 1) {
            if (cars.size() > 2) snprintf(lbl, sizeof lbl, "CPU%d", i);
            else                 snprintf(lbl, sizeof lbl, "CPU");
        }
        else snprintf(lbl, sizeof lbl, "P%d", i + 1);
        dot(cars[i], lbl);
    }
    if (me < int(cars.size()) && (isOnline() || simEngine.getGameMode() == sim::GameSim::ModeSingle))
        dot(cars[me], "YOU");
}

void GLWidget::drawMainMenu(int W, int H, float dpr)
{
    begin2D(W, H);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS);
        glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d);
        glEnd();
    };

    auto drawRoundedRect = [](float x0, float y0, float x1, float y1, float r, int segs = 8) {
        r = std::min(r, std::min((x1 - x0) * 0.5f, (y1 - y0) * 0.5f));
        glBegin(GL_POLYGON);
        for (int i = 0; i <= segs; ++i) {
            float a = -0.5f * PI + (0.5f * PI * i) / float(segs);
            glVertex2f(x1 - r + std::cos(a) * r, y0 + r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = (0.5f * PI * i) / float(segs);
            glVertex2f(x1 - r + std::cos(a) * r, y1 - r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = 0.5f * PI + (0.5f * PI * i) / float(segs);
            glVertex2f(x0 + r + std::cos(a) * r, y1 - r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = PI + (0.5f * PI * i) / float(segs);
            glVertex2f(x0 + r + std::cos(a) * r, y0 + r + std::sin(a) * r);
        }
        glEnd();
    };

    auto drawRoundedBorder = [&](float x0, float y0, float x1, float y1, float r, float lw, int segs = 8) {
        r = std::min(r, std::min((x1 - x0) * 0.5f, (y1 - y0) * 0.5f));
        glLineWidth(lw);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i <= segs; ++i) {
            float a = -0.5f * PI + (0.5f * PI * i) / float(segs);
            glVertex2f(x1 - r + std::cos(a) * r, y0 + r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = (0.5f * PI * i) / float(segs);
            glVertex2f(x1 - r + std::cos(a) * r, y1 - r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = 0.5f * PI + (0.5f * PI * i) / float(segs);
            glVertex2f(x0 + r + std::cos(a) * r, y1 - r + std::sin(a) * r);
        }
        for (int i = 0; i <= segs; ++i) {
            float a = PI + (0.5f * PI * i) / float(segs);
            glVertex2f(x0 + r + std::cos(a) * r, y0 + r + std::sin(a) * r);
        }
        glEnd();
        glLineWidth(1.0f);
    };

    auto drawGradientBar = [&](float bx0, float by0, float bw, float bh, float frac,
                               float r0, float g0, float b0, float r1, float g1, float b1) {
        // Slot background
        glColor4f(0.06f, 0.10f, 0.18f, 0.85f);
        drawRoundedRect(bx0, by0, bx0 + bw, by0 + bh, bh * 0.5f, 4);
        glColor4f(0.18f, 0.30f, 0.45f, 0.40f);
        drawRoundedBorder(bx0, by0, bx0 + bw, by0 + bh, bh * 0.5f, 1.0f * dpr, 4);

        // Smooth gradient-interpolated progress
        float fillW = std::clamp(bw * frac, bh, bw);
        const int numSegs = 20;
        float segW = fillW / float(numSegs);
        glBegin(GL_QUAD_STRIP);
        for (int s = 0; s <= numSegs; ++s) {
            float u = float(s) / float(numSegs);
            float cr = r0 + (r1 - r0) * u;
            float cg = g0 + (g1 - g0) * u;
            float cb = b0 + (b1 - b0) * u;
            glColor4f(cr, cg, cb, 0.95f);
            float sx = bx0 + s * segW;
            glVertex2f(sx, by0 + 1.0f * dpr);
            glVertex2f(sx, by0 + bh - 1.0f * dpr);
        }
        glEnd();
    };

    auto drawMenuIcon = [&](int idx, float cx, float cy, float sz, float cr, float cg, float cb, float ca) {
        glColor4f(cr, cg, cb, ca);
        if (idx == 0) {
            // Arcade D-Pad cross
            float w = sz * 0.36f;
            rect(cx - sz, cy - w, cx + sz, cy + w);
            rect(cx - w, cy - sz, cx + w, cy + sz);
        } else if (idx == 1) {
            // Split screen dual panels
            float gap = sz * 0.22f;
            rect(cx - sz, cy - sz * 0.72f, cx - gap, cy + sz * 0.72f);
            rect(cx + gap, cy - sz * 0.72f, cx + sz, cy + sz * 0.72f);
        } else if (idx == 2) {
            // Online globe / signal
            glLineWidth(1.4f * dpr);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 16; ++i) {
                float a = i * 2.0f * PI / 16.0f;
                glVertex2f(cx + std::cos(a) * sz, cy + std::sin(a) * sz);
            }
            glEnd();
            glBegin(GL_LINES);
            glVertex2f(cx - sz, cy); glVertex2f(cx + sz, cy);
            glVertex2f(cx, cy - sz); glVertex2f(cx, cy + sz);
            glEnd();
            glLineWidth(1.0f);
        } else if (idx == 3) {
            // Sports car silhouette
            glBegin(GL_POLYGON);
            glVertex2f(cx - sz * 0.95f, cy - sz * 0.40f);
            glVertex2f(cx - sz * 0.50f, cy + sz * 0.05f);
            glVertex2f(cx - sz * 0.15f, cy + sz * 0.50f);
            glVertex2f(cx + sz * 0.40f, cy + sz * 0.50f);
            glVertex2f(cx + sz * 0.75f, cy + sz * 0.10f);
            glVertex2f(cx + sz * 0.95f, cy - sz * 0.40f);
            glEnd();
            // Wheels
            glColor4f(0.1f, 0.15f, 0.22f, 1.0f);
            rect(cx - sz * 0.70f, cy - sz * 0.65f, cx - sz * 0.35f, cy - sz * 0.30f);
            rect(cx + sz * 0.35f, cy - sz * 0.65f, cx + sz * 0.70f, cy - sz * 0.30f);
        } else if (idx == 4) {
            // Map checkpoint pin
            glBegin(GL_TRIANGLES);
            glVertex2f(cx - sz * 0.65f, cy + sz * 0.15f);
            glVertex2f(cx + sz * 0.65f, cy + sz * 0.15f);
            glVertex2f(cx, cy - sz * 0.85f);
            glEnd();
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(cx, cy + sz * 0.25f);
            for (int i = 0; i <= 12; ++i) {
                float a = i * 2.0f * PI / 12.0f;
                glVertex2f(cx + std::cos(a) * (sz * 0.65f), cy + sz * 0.25f + std::sin(a) * (sz * 0.65f));
            }
            glEnd();
            // Inner hollow dot
            glColor4f(0.04f, 0.07f, 0.12f, 1.0f);
            rect(cx - sz * 0.22f, cy + sz * 0.10f, cx + sz * 0.22f, cy + sz * 0.40f);
        } else if (idx == 5) {
            // Trophy
            glBegin(GL_POLYGON);
            glVertex2f(cx - sz * 0.75f, cy + sz * 0.70f);
            glVertex2f(cx + sz * 0.75f, cy + sz * 0.70f);
            glVertex2f(cx + sz * 0.45f, cy);
            glVertex2f(cx + sz * 0.20f, cy - sz * 0.40f);
            glVertex2f(cx - sz * 0.20f, cy - sz * 0.40f);
            glVertex2f(cx - sz * 0.45f, cy);
            glEnd();
            rect(cx - sz * 0.50f, cy - sz * 0.75f, cx + sz * 0.50f, cy - sz * 0.50f);
        } else {
            // Settings Cog
            glLineWidth(1.5f * dpr);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 12; ++i) {
                float a = i * 2.0f * PI / 12.0f;
                glVertex2f(cx + std::cos(a) * sz * 0.8f, cy + std::sin(a) * sz * 0.8f);
            }
            glEnd();
            glBegin(GL_LINES);
            glVertex2f(cx - sz, cy); glVertex2f(cx + sz, cy);
            glVertex2f(cx, cy - sz); glVertex2f(cx, cy + sz);
            glVertex2f(cx - sz * 0.7f, cy - sz * 0.7f); glVertex2f(cx + sz * 0.7f, cy + sz * 0.7f);
            glVertex2f(cx - sz * 0.7f, cy + sz * 0.7f); glVertex2f(cx + sz * 0.7f, cy - sz * 0.7f);
            glEnd();
            glLineWidth(1.0f);
        }
    };

    auto drawSpecIcon = [&](int idx, float cx, float cy, float sz) {
        if (idx == 0) {
            // Speedometer gauge arc
            glColor4f(0.25f, 0.85f, 1.0f, 0.95f);
            glLineWidth(1.4f * dpr);
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i <= 10; ++i) {
                float a = 0.8f * PI - (1.1f * PI * i) / 10.0f;
                glVertex2f(cx + std::cos(a) * sz, cy + std::sin(a) * sz);
            }
            glEnd();
            // Needle
            glBegin(GL_LINES);
            glVertex2f(cx, cy - sz * 0.2f);
            glVertex2f(cx + sz * 0.65f, cy + sz * 0.65f);
            glEnd();
            glLineWidth(1.0f);
        } else if (idx == 1) {
            // Stopwatch / 0-100 chart
            glColor4f(1.0f, 0.75f, 0.20f, 0.95f);
            glLineWidth(1.3f * dpr);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 12; ++i) {
                float a = i * 2.0f * PI / 12.0f;
                glVertex2f(cx + std::cos(a) * sz * 0.85f, cy + std::sin(a) * sz * 0.85f);
            }
            glEnd();
            glBegin(GL_LINES);
            glVertex2f(cx, cy); glVertex2f(cx, cy + sz * 0.65f);
            glVertex2f(cx, cy); glVertex2f(cx + sz * 0.5f, cy);
            glEnd();
            glLineWidth(1.0f);
        } else if (idx == 2) {
            // Nitro bolt
            glColor4f(1.0f, 0.88f, 0.22f, 0.95f);
            glBegin(GL_TRIANGLES);
            glVertex2f(cx + sz * 0.4f, cy + sz * 0.9f);
            glVertex2f(cx - sz * 0.6f, cy - sz * 0.1f);
            glVertex2f(cx + sz * 0.1f, cy - sz * 0.1f);
            glVertex2f(cx - sz * 0.1f, cy + sz * 0.1f);
            glVertex2f(cx - sz * 0.4f, cy - sz * 0.9f);
            glVertex2f(cx + sz * 0.6f, cy + sz * 0.1f);
            glEnd();
        } else {
            // Steering wheel
            glColor4f(0.20f, 0.90f, 1.0f, 0.95f);
            glLineWidth(1.4f * dpr);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 14; ++i) {
                float a = i * 2.0f * PI / 14.0f;
                glVertex2f(cx + std::cos(a) * sz * 0.9f, cy + std::sin(a) * sz * 0.9f);
            }
            glEnd();
            glBegin(GL_LINES);
            glVertex2f(cx - sz * 0.9f, cy); glVertex2f(cx + sz * 0.9f, cy);
            glVertex2f(cx, cy); glVertex2f(cx, cy - sz * 0.9f);
            glEnd();
            glLineWidth(1.0f);
        }
    };

    float btnX, btnW, btnH, btnGap, listTop, ps;
    computeMenuMetrics(W, H, dpr, btnX, btnW, btnH, btnGap, listTop, ps);

    float tickF = float(simEngine.getTick());
    float pulse = 0.85f + 0.15f * std::sin(tickF * 0.14f);

    float panelX = 28.0f * dpr;
    float panelW = std::clamp(320.0f * dpr, 280.0f, W * 0.30f);
    float panelY0 = 24.0f * dpr;
    float panelY1 = H - 86.0f * dpr;

    // 1. TOP-LEFT PROFILE BADGE (USER: RACER_JAX)
    float pCardW = panelW;
    float pCardH = 54.0f * dpr;
    float pCardX = panelX;
    float pCardY = H - 18.0f * dpr - pCardH;

    glColor4f(0.04f, 0.07f, 0.13f, 0.90f);
    drawRoundedRect(pCardX, pCardY, pCardX + pCardW, pCardY + pCardH, 10.0f * dpr);
    glColor4f(0.18f, 0.82f, 1.0f, 0.65f);
    drawRoundedBorder(pCardX, pCardY, pCardX + pCardW, pCardY + pCardH, 10.0f * dpr, 1.2f * dpr);

    // Avatar Box
    float asz = 38.0f * dpr;
    float ax0 = pCardX + 8.0f * dpr, ay0 = pCardY + (pCardH - asz) * 0.5f;
    glColor4f(0.08f, 0.14f, 0.22f, 0.95f);
    drawRoundedRect(ax0, ay0, ax0 + asz, ay0 + asz, 6.0f * dpr);
    glColor4f(0.25f, 0.80f, 1.0f, 0.55f);
    drawRoundedBorder(ax0, ay0, ax0 + asz, ay0 + asz, 6.0f * dpr, 1.0f * dpr);

    // Avatar Head + Torso Silhouette
    float acx = ax0 + asz * 0.5f, acy = ay0 + asz * 0.5f;
    glColor4f(0.40f, 0.85f, 1.0f, 0.85f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(acx, acy + 4.0f * dpr);
    for (int i = 0; i <= 10; ++i) {
        float a = i * 2.0f * PI / 10.0f;
        glVertex2f(acx + std::cos(a) * 5.5f * dpr, acy + 4.0f * dpr + std::sin(a) * 5.5f * dpr);
    }
    glEnd();
    glBegin(GL_POLYGON);
    glVertex2f(acx - 10.0f * dpr, ay0 + 4.0f * dpr);
    glVertex2f(acx - 6.0f * dpr, acy - 1.0f * dpr);
    glVertex2f(acx + 6.0f * dpr, acy - 1.0f * dpr);
    glVertex2f(acx + 10.0f * dpr, ay0 + 4.0f * dpr);
    glEnd();

    // User Info Text beside avatar
    float ptx = ax0 + asz + 10.0f * dpr;
    float pPs = ps * 0.80f;
    {
        const QByteArray who = "USER: " + QByteArray::fromStdString(profileName(0));
        drawText(who.constData(), ptx, pCardY + pCardH - 14.0f * dpr, pPs * 1.02f, 1.0f, 1.0f, 1.0f);
        const QByteArray acct = accountUser.isEmpty() ? QByteArray("OFFLINE - LOG IN FROM MY PROFILE")
                              : syncState == SyncState::Synced ? QByteArray("ACCOUNT SYNCED") : syncState == SyncState::Pending
                              ? QByteArray("ACCOUNT: NOT SYNCED YET") : QByteArray("ACCOUNT: SYNCING...");
        drawText(acct.constData(), ptx, pCardY + pCardH - 27.0f * dpr, pPs * 0.84f, 1.0f, 0.82f, 0.25f);
    }

    // Glowing green server status LED
    float dotX = ptx + 3.0f * dpr, dotY = pCardY + 11.0f * dpr, dotR = 2.8f * dpr;
    glColor4f(0.20f, 1.0f, 0.40f, 0.95f * pulse);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(dotX, dotY);
    for (int i = 0; i <= 12; ++i) {
        float a = i * 2.0f * PI / 12.0f;
        glVertex2f(dotX + std::cos(a) * dotR, dotY + std::sin(a) * dotR);
    }
    glEnd();
    drawText("SERVER: ASIA - MUMBAI (ONLINE)", ptx + 11.0f * dpr, pCardY + 8.0f * dpr, pPs * 0.82f, 0.35f, 1.0f, 0.55f);

    // 2. TOP-RIGHT GARAGE STATUS CARD (GARAGE 01)
    float gCardW = std::clamp(260.0f * dpr, 220.0f, W * 0.26f);
    float gCardH = 48.0f * dpr;
    float gCardX = W - 28.0f * dpr - gCardW;
    float gCardY = H - 18.0f * dpr - gCardH;

    glColor4f(0.04f, 0.07f, 0.13f, 0.90f);
    drawRoundedRect(gCardX, gCardY, gCardX + gCardW, gCardY + gCardH, 10.0f * dpr);
    glColor4f(1.0f, 0.55f, 0.15f, 0.70f);
    drawRoundedBorder(gCardX, gCardY, gCardX + gCardW, gCardY + gCardH, 10.0f * dpr, 1.2f * dpr);

    drawText("GARAGE 01", gCardX + 12.0f * dpr, gCardY + gCardH - 16.0f * dpr, pPs * 1.05f, 1.0f, 1.0f, 1.0f);
    drawText("TUNED GT-R R35 [CUSTOM]", gCardX + 12.0f * dpr, gCardY + 9.0f * dpr, pPs * 0.84f, 1.0f, 0.68f, 0.20f);

    // 3. LEFT UNIFIED NAVIGATION CONTAINER CARD
    glColor4f(0.04f, 0.07f, 0.12f, 0.86f);
    drawRoundedRect(panelX, panelY0, panelX + panelW, panelY1, 16.0f * dpr);
    glColor4f(0.18f, 0.65f, 0.95f, 0.55f);
    drawRoundedBorder(panelX, panelY0, panelX + panelW, panelY1, 16.0f * dpr, 1.5f * dpr);

    // Header inside container
    float headerTop = panelY1 - 10.0f * dpr;
    {
        char carLine[64];
        std::snprintf(carLine, sizeof(carLine), "CAR: %s", CAR_PRESETS[selectedCarPreset].model);
        drawText(carLine, panelX + 16.0f * dpr, headerTop - 12.0f * dpr, ps * 0.92f, 1.0f, 1.0f, 1.0f);
        std::snprintf(carLine, sizeof(carLine), "(%s)%s", CAR_PRESETS[selectedCarPreset].trim,
                      menuSelection == M_CAR ? "  < >" : "");
        drawText(carLine, panelX + 16.0f * dpr, headerTop - 25.0f * dpr, ps * 0.80f, 1.0f, 0.65f, 0.15f);
    }

    // Thin divider line
    glColor4f(0.20f, 0.45f, 0.70f, 0.45f);
    rect(panelX + 16.0f * dpr, listTop + 6.0f * dpr, panelX + panelW - 16.0f * dpr, listTop + 7.5f * dpr);

    // 4. THE 8 MENU PILL BUTTONS
    float textPs = ps * 0.95f;
    for (int i = 0; i < NUM_MENU_ITEMS; ++i) {
        float by0 = listTop - (i + 1) * btnH - i * btnGap;
        float by1 = by0 + btnH;
        float bx0 = btnX;
        float bx1 = btnX + btnW;
        bool sel = (menuSelection == i);
        const MenuItem &item = MENU_ITEMS[i];
        char cpuTitle[40];
        const char *title = item.title;
        if (i == M_CPUS) {                              // CPU RACERS: 3  < >
            std::snprintf(cpuTitle, sizeof cpuTitle, "CPU RACERS: %d%s", cpuCount, sel ? "  < >" : "");
            title = cpuTitle;
        } else if (i == M_TAUNTS) {                     // TAUNTS: ON  < >
            std::snprintf(cpuTitle, sizeof cpuTitle, "TAUNTS: %s%s", tauntsOn ? "ON" : "OFF", sel ? "  < >" : "");
            title = cpuTitle;
        } else if (i == M_ACCOUNT) {                    // LOG OUT (SSK12345)  /  LOG IN
            if (accountUser.isEmpty()) std::snprintf(cpuTitle, sizeof cpuTitle, "LOG IN");
            else std::snprintf(cpuTitle, sizeof cpuTitle, "LOG OUT (%s)", accountUser.toLatin1().constData());
            title = cpuTitle;
        } else if (i == M_SOUND) {                      // SOUND: ON  < >
            std::snprintf(cpuTitle, sizeof cpuTitle, "SOUND: %s%s", soundOn ? "ON" : "OFF", sel ? "  < >" : "");
            title = cpuTitle;
        } else if (i == M_CLONE) {                      // YOUR CLONE: ON (81%)  < >   /  ... 2/3 RACES
            if (cloneReady)
                std::snprintf(cpuTitle, sizeof cpuTitle, "YOUR CLONE: %s (%d%%)%s", cloneOn ? "ON" : "OFF",
                              int(std::lround(cloneStats.agree() * 100.0f)), sel ? "  < >" : "");
            else
                std::snprintf(cpuTitle, sizeof cpuTitle, "YOUR CLONE: %d/%d RACES%s", std::min(cloneBook.races(), sim::CloneBook::MIN_RACES),
                              sim::CloneBook::MIN_RACES, sel ? "  < >" : "");
            title = cpuTitle;
        }

        float cr = item.isOrange ? 1.00f : 0.15f;
        float cg = item.isOrange ? 0.48f : 0.85f;
        float cb = item.isOrange ? 0.08f : 1.00f;
        float btnR = 10.0f * dpr;

        if (sel) {
            // Selected fill
            if (item.isOrange) {
                glColor4f(0.58f, 0.22f, 0.04f, 0.95f);
            } else {
                glColor4f(0.05f, 0.28f, 0.46f, 0.95f);
            }
            drawRoundedRect(bx0, by0, bx1, by1, btnR);

            // Double glowing neon border
            glColor4f(cr, cg, cb, 0.40f * pulse);
            drawRoundedBorder(bx0, by0, bx1, by1, btnR, 3.2f * dpr);
            glColor4f(cr * pulse, cg * pulse, cb * pulse, 1.0f);
            drawRoundedBorder(bx0, by0, bx1, by1, btnR, 1.6f * dpr);

            // Left accent vertical pill strip
            glColor4f(cr, cg, cb, 1.0f);
            drawRoundedRect(bx0 + 3.0f * dpr, by0 + 5.0f * dpr, bx0 + 7.5f * dpr, by1 - 5.0f * dpr, 2.0f * dpr);

            // Animated right indicator arrow
            float sway = std::sin(tickF * 0.20f) * 2.0f * dpr;
            float textY = by0 + (btnH - 7.0f * textPs) * 0.5f;
            drawText(">", bx1 - 16.0f * dpr + sway, textY, textPs * 1.05f, 1.0f, 0.90f, 0.25f);
        } else {
            // Unselected fill
            glColor4f(0.05f, 0.08f, 0.14f, 0.75f);
            drawRoundedRect(bx0, by0, bx1, by1, btnR);

            // Subtle border matching button's theme color
            glColor4f(cr * 0.85f, cg * 0.85f, cb * 0.85f, 0.45f);
            drawRoundedBorder(bx0, by0, bx1, by1, btnR, 1.0f * dpr);
        }

        // Left vector icon
        drawMenuIcon(i, bx0 + 20.0f * dpr, by0 + btnH * 0.5f, 7.5f * dpr, cr, cg, cb, sel ? 1.0f : 0.75f);

        // Text title
        float textY = by0 + (btnH - 7.0f * textPs) * 0.5f;
        drawText(title, bx0 + 36.0f * dpr, textY, textPs,
                 sel ? 1.0f : 0.88f, sel ? 1.0f : 0.92f, sel ? 1.0f : 0.96f);
    }

    // 5. MID-RIGHT FLOATING SECONDARY SPEC CARD
    float specW = std::clamp(300.0f * dpr, 260.0f, W * 0.29f);
    float specH = std::clamp(200.0f * dpr, 170.0f, H * 0.35f);
    float specX0 = W - 28.0f * dpr - specW;
    float specY0 = 24.0f * dpr;

    float m2H = 76.0f * dpr;
    float m2Y0 = specY0 + specH + 12.0f * dpr;
    glColor4f(0.04f, 0.07f, 0.13f, 0.88f);
    drawRoundedRect(specX0, m2Y0, specX0 + specW, m2Y0 + m2H, 12.0f * dpr);
    glColor4f(1.0f, 0.60f, 0.15f, 0.60f);
    drawRoundedBorder(specX0, m2Y0, specX0 + specW, m2Y0 + m2H, 12.0f * dpr, 1.2f * dpr);

    float m2Ps = ps * 0.76f;
    drawText("PERFORMANCE METRICS", specX0 + 12.0f * dpr, m2Y0 + m2H - 14.0f * dpr, m2Ps * 0.95f, 1.0f, 0.80f, 0.20f);
    glColor4f(0.30f, 0.45f, 0.60f, 0.40f);
    rect(specX0 + 12.0f * dpr, m2Y0 + m2H - 18.0f * dpr, specX0 + specW - 12.0f * dpr, m2Y0 + m2H - 17.0f * dpr);

    drawText("HORSEPOWER: 600 BHP", specX0 + 12.0f * dpr, m2Y0 + 36.0f * dpr, m2Ps * 0.82f, 0.85f, 0.90f, 0.95f);
    drawGradientBar(specX0 + 12.0f * dpr, m2Y0 + 29.0f * dpr, specW - 24.0f * dpr, 4.0f * dpr, 0.90f,
                    1.0f, 0.50f, 0.10f, 0.20f, 0.85f, 1.0f);

    drawText("WEIGHT: 1,480 KG  |  AERO: HIGH", specX0 + 12.0f * dpr, m2Y0 + 14.0f * dpr, m2Ps * 0.82f, 0.85f, 0.90f, 0.95f);
    drawGradientBar(specX0 + 12.0f * dpr, m2Y0 + 7.0f * dpr, specW - 24.0f * dpr, 4.0f * dpr, 0.82f,
                    0.20f, 0.85f, 1.0f, 1.0f, 0.50f, 0.10f);

    // 6. BOTTOM-RIGHT DETAILED SPEC CARD
    glColor4f(0.04f, 0.07f, 0.13f, 0.88f);
    drawRoundedRect(specX0, specY0, specX0 + specW, specY0 + specH, 14.0f * dpr);
    glColor4f(0.22f, 0.82f, 1.0f, 0.70f);
    drawRoundedBorder(specX0, specY0, specX0 + specW, specY0 + specH, 14.0f * dpr, 1.4f * dpr);

    // Spec Card Header
    float specPs = ps * 0.82f;
    drawText("SPECIFICATIONS", specX0 + 12.0f * dpr, specY0 + specH - 16.0f * dpr, specPs * 0.95f, 0.25f, 0.90f, 1.0f);
    const char *specTag = "[R35 NISMO]";
    float tagW = textWidth(specTag, specPs * 0.82f);
    drawText(specTag, specX0 + specW - 12.0f * dpr - tagW, specY0 + specH - 16.0f * dpr, specPs * 0.82f, 1.0f, 0.82f, 0.25f);

    // Divider line
    glColor4f(0.20f, 0.50f, 0.80f, 0.50f);
    rect(specX0 + 12.0f * dpr, specY0 + specH - 22.0f * dpr, specX0 + specW - 12.0f * dpr, specY0 + specH - 21.0f * dpr);

    // 4 Dynamic Spec Rows with Orange-to-Cyan Smooth Gradient Bars
    struct SpecRow {
        const char *label;
        const char *val;
        float frac;
    };
    const CarPreset &cp = CAR_PRESETS[selectedCarPreset];
    char sv[4][32];
    std::snprintf(sv[0], 32, "%d KM/H", int(cp.topSpeedKmh));
    std::snprintf(sv[1], 32, "%.1f S", cp.accelSec);
    std::snprintf(sv[2], 32, "%d KM/H", int(cp.nitroKmh));
    std::snprintf(sv[3], 32, "%s %.1f/10", cp.handlingDesc, cp.handlingScore);
    SpecRow specRows[4] = {
        {"TOP SPEED",   sv[0], cp.topSpeedScore / 10.0f},
        {"0-100 ACCEL", sv[1], cp.accelScore / 10.0f},
        {"NITRO BOOST", sv[2], cp.nitroScore / 10.0f},
        {"HANDLING",    sv[3], cp.handlingScore / 10.0f}
    };

    float rowTop = specY0 + specH - 38.0f * dpr;
    float rowGap = 28.0f * dpr;
    for (int k = 0; k < 4; ++k) {
        float ly = rowTop - k * rowGap;
        // Vector spec icon
        drawSpecIcon(k, specX0 + 20.0f * dpr, ly + 4.0f * dpr, 6.0f * dpr);

        // Label
        drawText(specRows[k].label, specX0 + 32.0f * dpr, ly, specPs * 0.85f, 0.88f, 0.92f, 0.96f);

        // Value text (right aligned)
        float vw = textWidth(specRows[k].val, specPs * 0.85f);
        drawText(specRows[k].val, specX0 + specW - 12.0f * dpr - vw, ly, specPs * 0.85f, 1.0f, 0.82f, 0.20f);

        // Gradient progress bar
        drawGradientBar(specX0 + 12.0f * dpr, ly - 9.0f * dpr, specW - 24.0f * dpr, 5.5f * dpr, specRows[k].frac,
                        1.0f, 0.45f, 0.05f, 0.15f, 0.85f, 1.0f);
    }

    // Sub-footer tags inside spec card
    float subTagY = specY0 + 8.0f * dpr;
    float subTagPs = specPs * 0.70f;
    drawText("TIRES: SEMI-SLICK COMPETITION", specX0 + 12.0f * dpr, subTagY + 8.0f * dpr, subTagPs, 0.50f, 0.65f, 0.80f);
    drawText("DRIVETRAIN: ATTESA E-TS AWD", specX0 + 12.0f * dpr, subTagY, subTagPs, 0.50f, 0.65f, 0.80f);


    // 6b. BIG MAP + CPU CARD in the open space above the car: which map, what it looks like,
    // how hard the CPU is - large enough to read at a glance
    {
        static const float ACCENT[6][3] = {{1.0f, 0.62f, 0.20f}, {1.0f, 0.78f, 0.30f}, {0.60f, 0.86f, 1.0f},
                                           {0.92f, 0.35f, 1.0f}, {0.20f, 0.86f, 1.0f}, {0.35f, 0.95f, 0.50f}};
        const int m = selectedMapPreset;
        const sim::MapDef &md = sim::MAPS[m];
        const float *ac = ACCENT[int(md.theme)];
        const bool selMap = (menuSelection == M_MAP), selCpu = (menuSelection == M_PROFILE), selLaps = false;
        const float cx0 = panelX + panelW + 24.0f * dpr, cx1 = W - 28.0f * dpr;
        const float cy1 = gCardY - 12.0f * dpr;
        const float cy0 = std::max(cy1 - H * 0.33f, m2Y0 + m2H + 12.0f * dpr);
        const float cw = cx1 - cx0, ch = cy1 - cy0, pad = 16.0f * dpr;

        glColor4f(0.04f, 0.07f, 0.13f, 0.90f);
        drawRoundedRect(cx0, cy0, cx1, cy1, 14.0f * dpr);
        glColor4f(ac[0], ac[1], ac[2], selMap ? 0.95f * pulse : 0.65f);
        drawRoundedBorder(cx0, cy0, cx1, cy1, 14.0f * dpr, (selMap ? 2.4f : 1.4f) * dpr);

        // left: the whole road from above (north up), as big as the card allows; the start in
        // green, the shortcut as a thinner sandy line
        const float boxW = std::min(ch - 2.0f * pad, cw * 0.38f);
        {
            const auto &S = simEngine.circuit().slices();
            const auto &C = simEngine.circuit().shortcut();
            const int n = int(S.size());
            float ax = 1e9f, bx = -1e9f, az = 1e9f, bz = -1e9f;
            for (const sim::Slice &sl : S) {
                ax = std::min(ax, sl.x); bx = std::max(bx, sl.x);
                az = std::min(az, sl.z); bz = std::max(bz, sl.z);
            }
            const float bx0 = cx0 + pad, bx1 = bx0 + boxW, by0 = cy0 + pad, by1 = cy1 - pad;
            glColor4f(0.02f, 0.04f, 0.08f, 0.85f);
            drawRoundedRect(bx0, by0, bx1, by1, 10.0f * dpr);
            const float in = 12.0f * dpr;
            const float k = std::min((bx1 - bx0 - 2.0f * in) / std::max(1.0f, bx - ax),
                                     (by1 - by0 - 2.0f * in) / std::max(1.0f, bz - az));
            const float ox = 0.5f * (bx0 + bx1) - 0.5f * (ax + bx) * k, oy = 0.5f * (by0 + by1) - 0.5f * (az + bz) * k;
            glLineWidth(5.0f * dpr);
            glColor4f(ac[0], ac[1], ac[2], 1.0f);
            glBegin(simEngine.sprint() ? GL_LINE_STRIP : GL_LINE_LOOP);
            for (int i = 0; i < n; i += 6) glVertex2f(ox + S[size_t(i)].x * k, oy + S[size_t(i)].z * k);
            glVertex2f(ox + S[size_t(n - 1)].x * k, oy + S[size_t(n - 1)].z * k);
            glEnd();
            if (!C.empty()) {
                glLineWidth(2.5f * dpr);
                glColor4f(0.85f, 0.65f, 0.35f, 1.0f);
                glBegin(GL_LINE_STRIP);
                for (size_t i = 0; i < C.size(); i += 6) glVertex2f(ox + C[i].x * k, oy + C[i].z * k);
                glVertex2f(ox + C.back().x * k, oy + C.back().z * k);
                glEnd();
            }
            glLineWidth(1.0f);
            const float r = 5.0f * dpr;
            const sim::Slice &st = S[size_t(simEngine.startLine())];
            glColor4f(0.3f, 1.0f, 0.5f, 1.0f);                   // the start in green
            rect(ox + st.x * k - r, oy + st.z * k - r, ox + st.x * k + r, oy + st.z * k + r);
            if (simEngine.sprint()) {                            // a sprint's finish in white
                const sim::Slice &fi = S[size_t(simEngine.finishLine())];
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                rect(ox + fi.x * k - r, oy + fi.z * k - r, ox + fi.x * k + r, oy + fi.z * k + r);
            }
        }

        // right: big text, top to bottom
        const float tx = cx0 + pad + boxW + 22.0f * dpr, tw = cx1 - pad - tx;
        float y = cy1 - pad - 7.0f * ps;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "MAP %d/%d", m + 1, sim::NUM_MAPS);
        drawText(buf, tx, y, ps, ac[0], ac[1], ac[2]);
        if (selMap && (simEngine.getTick() / 20) % 2 == 0) {
            const char *hint = "< LEFT / RIGHT >";
            drawText(hint, cx1 - pad - textWidth(hint, ps * 0.9f), y, ps * 0.9f, 1.0f, 1.0f, 1.0f);
        }
        float namePs = ps * 3.0f;                         // the map name, as large as fits
        while (namePs > ps * 1.2f && textWidth(md.name, namePs) > tw) namePs *= 0.92f;
        y -= 7.0f * namePs + 10.0f * dpr;
        drawText(md.name, tx, y, namePs, 1.0f, 1.0f, 1.0f);
        y -= 7.0f * ps * 0.85f + 9.0f * dpr;
        drawText(md.tagline, tx, y, ps * 0.85f, 0.62f, 0.72f, 0.85f);

        // the lower rows sit on the bottom of the card: race length, CPU level, map difficulty
        // (no difficulty to choose: the CPUs adapt to your profile)
        static const char *CPU_NAMES[1] = {"ADAPTIVE"};
        static const float CPU_COL[1][3] = {{0.35f, 0.85f, 1.0f}};
        const int dl = 0;
        const float cpuPs = ps * (selCpu ? 2.8f : 2.6f), lenPs = ps * (selLaps ? 1.6f : 1.5f);
        const float lenY = cy0 + pad + 4.0f * dpr;
        const float cpuY = lenY + 7.0f * lenPs + 30.0f * dpr;

        // map difficulty: big blocks
        y = cpuY + 7.0f * cpuPs + 26.0f * dpr;
        drawText("MAP", tx, y, ps * 1.2f, 0.70f, 0.78f, 0.90f);
        const float blk = 8.4f * ps, bxs = tx + textWidth("MAP", ps * 1.2f) + 12.0f * dpr;
        for (int i = 0; i < 3; ++i) {
            if (i < md.stars) glColor4f(ac[0], ac[1], ac[2], 1.0f);
            else              glColor4f(0.25f, 0.30f, 0.40f, 1.0f);
            rect(bxs + i * (blk + 6.0f * dpr), y, bxs + i * (blk + 6.0f * dpr) + blk, y + blk);
        }

        // CPU difficulty: the biggest line, in its own colour (green / yellow / red)
        y = cpuY;
        drawText("CPU", tx, y, cpuPs, 0.85f, 0.90f, 0.95f);
        const float lx = tx + textWidth("CPU ", cpuPs);
        const float pillW = textWidth(CPU_NAMES[dl], cpuPs) + 20.0f * dpr;
        glColor4f(CPU_COL[dl][0] * 0.30f, CPU_COL[dl][1] * 0.30f, CPU_COL[dl][2] * 0.30f, 0.95f);
        drawRoundedRect(lx, y - 7.0f * dpr, lx + pillW, y + 7.0f * cpuPs + 7.0f * dpr, 8.0f * dpr);
        glColor4f(CPU_COL[dl][0], CPU_COL[dl][1], CPU_COL[dl][2], selCpu ? pulse : 0.85f);
        drawRoundedBorder(lx, y - 7.0f * dpr, lx + pillW, y + 7.0f * cpuPs + 7.0f * dpr, 8.0f * dpr, 2.0f * dpr);
        drawText(CPU_NAMES[dl], lx + 10.0f * dpr, y, cpuPs, CPU_COL[dl][0], CPU_COL[dl][1], CPU_COL[dl][2]);
        std::snprintf(buf, sizeof buf, "%d CPU%s", cpuCount, cpuCount > 1 ? "S" : "");
        drawText(buf, lx + pillW + 16.0f * dpr, y + 7.0f * (cpuPs - ps * 1.6f) * 0.5f, ps * 1.6f, 0.85f, 0.90f, 0.95f);

        // race length
        y = lenY;
        if (simEngine.sprint())
            std::snprintf(buf, sizeof buf, "SPRINT %.1f KM, NO LAPS", simEngine.raceDistance() / 1000.0f);
        else
            std::snprintf(buf, sizeof buf, "%d LAP%s X %.1f KM", simEngine.getLaps(), simEngine.getLaps() > 1 ? "S" : "",
                          simEngine.lapLength() / 1000.0f);
        drawText(buf, tx, y, lenPs, selLaps ? 1.0f : 0.80f, selLaps ? 0.85f : 0.86f,
                 selLaps ? 0.30f : 0.95f);
    }

    // 7. BOTTOM NAVIGATION BAR / HINTS
    float footY = 14.0f * dpr;
    const char *footHints = "[UP/DOWN] NAVIGATE   [LEFT/RIGHT] CHANGE CAR / MAP / CPUS / ON-OFF   [+/-] CPU RACERS   [ENTER] SELECT";
    float footPs = ps * 0.75f;
    drawText(footHints, 28.0f * dpr, footY, footPs, 0.65f, 0.78f, 0.90f);

    if (menuSelection <= M_VSCPU && ((simEngine.getTick() / 25) % 2 == 0)) {
        const char *readyPrompt = "PRESS [ENTER] TO START";
        float prW = textWidth(readyPrompt, footPs * 1.05f);
        drawText(readyPrompt, W - 28.0f * dpr - prW, footY, footPs * 1.05f, 0.30f, 1.0f, 0.50f);
    }

    if (activeModal != ModalNone) {
        drawMenuModal(W, H, dpr);
    }
}

void GLWidget::drawMenuModal(int /*W*/, int /*H*/, float /*dpr*/)
{
    // Popups non-functional for now as requested
}

void GLWidget::drawPauseOverlay(int W, int H, float dpr)
{
    begin2D(W, H);
    glColor4f(0.0f, 0.0f, 0.0f, 0.65f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(W, 0); glVertex2f(W, H); glVertex2f(0, H);
    glEnd();

    auto box = [](float x0, float y0, float x1, float y1) {
        glBegin(GL_QUADS); glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1); glEnd();
    };
    auto frame = [&](float x0, float y0, float x1, float y1, float t) {
        box(x0, y0, x1, y0 + t); box(x0, y1 - t, x1, y1); box(x0, y0, x0 + t, y1); box(x1 - t, y0, x1, y1);
    };
    const float u = std::clamp(std::min(W / 900.0f, H / 600.0f), 0.6f, 1.6f);   // (layout unit, in pixels)
    const float ps = 2.4f * u;
    const float pw = std::min(W * 0.9f, 560.0f * u), ph = 372.0f * u;
    const float x0 = (W - pw) * 0.5f, x1 = x0 + pw, y0 = (H - ph) * 0.5f - 18.0f * u, y1 = y0 + ph;   // (under the drivers' chat line)
    auto centred = [&](const char *t, float y, float s, float r, float g, float b) {
        drawText(t, (W - textWidth(t, s)) * 0.5f, y, s, r, g, b);
    };

    glColor4f(0.04f, 0.07f, 0.13f, 0.95f);
    box(x0, y0, x1, y1);
    glColor4f(1.0f, 0.55f, 0.12f, 1.0f);                  // (an orange stripe on top, the game's colour)
    box(x0, y1 - 6.0f * u, x1, y1);
    glColor4f(0.92f, 0.35f, 1.0f, 0.75f);
    frame(x0, y0, x1, y1, 2.0f * u);

    float y = y1 - 30.0f * u - 7.0f * ps * 1.6f;
    centred("PAUSED", y, ps * 1.6f, 1.0f, 0.85f, 0.25f);
    {   // the race you are in
        const sim::MapDef &md = simEngine.mapDef();
        char info[96];
        if (simEngine.sprint()) std::snprintf(info, sizeof info, "%s  -  SPRINT", md.name);
        else std::snprintf(info, sizeof info, "%s  -  LAP %d / %d", md.name,
                           std::clamp(simEngine.getCars().empty() ? 1 : simEngine.getCars()[0].lap + 1, 1, simEngine.getLaps()), simEngine.getLaps());
        y -= 26.0f * u;
        centred(info, y, ps * 0.62f, 0.70f, 0.78f, 0.92f);
    }
    y -= 30.0f * u;

    static const char *OPT[3] = {"RESUME RACE", "RESTART RACE", "MAIN MENU"};
    static const char *KEY[3] = {"ESC", "R", "M"};
    static const char *SUB[3] = {"BACK TO THE ROAD", "FROM THE START LINE, SAME MAP", "LEAVE THIS RACE"};
    const float bw = pw * 0.78f, bh = 56.0f * u, bx = (W - bw) * 0.5f;
    for (int i = 0; i < 3; ++i) {
        const bool sel = pauseChoice == i;
        const float by1 = y, by0 = y - bh;
        glColor4f(sel ? 0.40f : 0.06f, sel ? 0.12f : 0.10f, sel ? 0.50f : 0.18f, 0.95f);
        box(bx, by0, bx + bw, by1);
        glColor4f(0.92f, 0.35f, 1.0f, sel ? 1.0f : 0.35f);
        frame(bx, by0, bx + bw, by1, (sel ? 2.0f : 1.0f) * u);
        // the key, in a little chip
        const float ks = ps * 0.7f, kw = textWidth("ESC", ks) + 14.0f * u, kh = 7.0f * ks + 12.0f * u;
        const float kx = bx + 16.0f * u, ky = by0 + (bh - kh) * 0.5f;
        glColor4f(0.02f, 0.03f, 0.06f, 1.0f);
        box(kx, ky, kx + kw, ky + kh);
        glColor4f(1.0f, 0.82f, 0.25f, 0.9f);
        frame(kx, ky, kx + kw, ky + kh, 1.5f * u);
        drawText(KEY[i], kx + (kw - textWidth(KEY[i], ks)) * 0.5f, ky + 6.0f * u, ks, 1.0f, 0.88f, 0.30f);
        const float tx = kx + kw + 16.0f * u;
        const float tr = i == 2 ? 1.0f : 1.0f, tg = i == 2 ? 0.75f : 1.0f, tb = i == 2 ? 0.75f : 1.0f;
        drawText(OPT[i], tx, by0 + bh * 0.48f, ps * 0.95f, tr, tg, tb);
        drawText(SUB[i], tx, by0 + bh * 0.18f, ps * 0.55f, 0.75f, 0.70f, 0.90f);
        if (sel) drawText(">", bx + bw - 26.0f * u, by0 + bh * 0.38f, ps * 1.0f, 1.0f, 0.85f, 0.25f);
        y = by0 - 12.0f * u;
    }
    centred("[UP/DOWN] CHOOSE   [ENTER] OK   [ESC] RESUME", y0 + 16.0f * u, ps * 0.6f, 0.70f, 0.78f, 0.90f);
}

void GLWidget::drawFinishOverlay(int W, int H, float dpr)
{
    begin2D(W, H);
    // Darkened cinematic backdrop: dims 3D track and any HUD cleanly
    glColor4f(0.02f, 0.04f, 0.07f, 0.88f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(W, 0); glVertex2f(W, H); glVertex2f(0, H);
    glEnd();

    auto rect = [](float x0, float y0, float x1, float y1) {
        glBegin(GL_QUADS);
        glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
        glEnd();
    };

    // Responsive scaling strictly clamped so everything fits perfectly
    const float s = std::clamp(std::min(float(W) / 800.0f, float(H) / 540.0f), 0.72f, 1.25f) * dpr;
    const float cx = W * 0.5f, cy = H * 0.5f;

    const auto &cars = simEngine.getCars();
    const int n = int(cars.size());
    if (n == 0) return;
    int winner = simEngine.getWinner();
    bool isSingle = (simEngine.getGameMode() == sim::GameSim::ModeSingle);
    static const char *ORD[sim::MAX_RACERS] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH"};

    // names on the results: YOU and CPU1..CPU5 in vs CPU, PLAYER 1/2 in split screen
    auto racerName = [&](int i) -> std::string {
        if (i >= 0 && i < int(nameOverride.size()) && !nameOverride[size_t(i)].isEmpty())   // named drivers, story characters
            return nameOverride[size_t(i)].toStdString() + (cars[size_t(i)].dirRole == 1 && cars[size_t(i)].dirRivalOf == 0 ? " (RIVAL)" : "");
        if (isSingle) return i == 0 ? std::string("YOU") : (n > 2 ? "CPU" + std::to_string(i) : std::string("CPU"));
        return "PLAYER " + std::to_string(i + 1);
    };
    std::vector<int> order(n);                     // car indices from 1st to last
    for (int i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return simEngine.rankOf(a) < simEngine.rankOf(b); });
    const sim::Car &cWin = cars[order[0]];
    // the ticker shows your numbers in vs CPU, the winner's in split screen
    const int focus = isSingle ? 0 : std::clamp(winner - 1, 0, n - 1);
    const sim::Car &cFocus = cars[focus];
    auto kmh = [](const sim::Car &c) { return int(c.topSpeed / sim::MAX_SPEED * 240.0f); };

    // Gantry total boundary
    const float gw = std::min(float(W) * 0.94f, 660.0f * s);
    const float gh = std::min(float(H) * 0.92f, 470.0f * s);
    const float gx0 = cx - gw * 0.5f, gx1 = cx + gw * 0.5f;
    const float gy0 = cy - gh * 0.5f, gy1 = cy + gh * 0.5f;

    // =======================================================================
    // 1. INDUSTRIAL STEEL TRUSS GANTRY (Pillars & Crossbeams from Photo 2)
    // =======================================================================
    const float pw = 34.0f * s; // pillar width
    const float topBeamH = 36.0f * s;

    // Pillar Bases & Columns
    for (float px : {gx0, gx1 - pw}) {
        // Base plate
        glColor4f(0.12f, 0.14f, 0.18f, 1.0f);
        rect(px, gy0, px + pw, gy1);
        // Vertical outer steel upright girders
        glColor4f(0.20f, 0.23f, 0.28f, 1.0f);
        rect(px, gy0, px + 5.0f * s, gy1);
        rect(px + pw - 5.0f * s, gy0, px + pw, gy1);
        // Cross-lattice bracing X patterns
        glColor4f(0.26f, 0.30f, 0.36f, 1.0f);
        for (float y = gy0 + 20.0f * s; y < gy1 - 30.0f * s; y += 32.0f * s) {
            rect(px + 4.0f * s, y, px + pw - 4.0f * s, y + 3.0f * s);
        }
    }

    // Top Overhead Spanning Truss Girders
    float tBeamY0 = gy1 - topBeamH;
    glColor4f(0.13f, 0.15f, 0.19f, 1.0f);
    rect(gx0 + pw, tBeamY0, gx1 - pw, gy1);
    // Upper & Lower Horizontal Steel Flanges
    glColor4f(0.22f, 0.25f, 0.30f, 1.0f);
    rect(gx0, gy1 - 6.0f * s, gx1, gy1);
    rect(gx0, tBeamY0, gx1, tBeamY0 + 5.0f * s);
    // Vertical Struts in top girder
    for (float tx = gx0 + pw + 30.0f * s; tx < gx1 - pw - 20.0f * s; tx += 48.0f * s) {
        rect(tx - 2.5f * s, tBeamY0 + 5.0f * s, tx + 2.5f * s, gy1 - 6.0f * s);
    }

    // =======================================================================
    // 2. 5 STADIUM FLOODLIGHTS ATOP GANTRY (Photo 2)
    // =======================================================================
    const float flSpacing = 52.0f * s;
    for (int i = -2; i <= 2; ++i) {
        float flx = cx + i * flSpacing;
        float fly = gy1 - 3.0f * s;
        // Mounting swivel bracket
        glColor4f(0.18f, 0.20f, 0.24f, 1.0f);
        rect(flx - 2.5f * s, fly - 6.0f * s, flx + 2.5f * s, fly);
        // Lamp housing box
        glColor4f(0.12f, 0.14f, 0.18f, 1.0f);
        rect(flx - 13.0f * s, fly, flx + 13.0f * s, fly + 15.0f * s);
        // Lamp glowing reflector face (warm golden-white)
        glColor4f(1.0f, 0.94f, 0.65f, 1.0f);
        rect(flx - 10.0f * s, fly + 2.0f * s, flx + 10.0f * s, fly + 13.0f * s);
        // Internal bulb grid bars
        glColor4f(0.85f, 0.72f, 0.35f, 0.85f);
        rect(flx - 1.0f * s, fly + 2.0f * s, flx + 1.0f * s, fly + 13.0f * s);
        rect(flx - 10.0f * s, fly + 7.0f * s, flx + 10.0f * s, fly + 8.5f * s);
    }

    // =======================================================================
    // 3. MAIN OVERHEAD ELECTRONIC SCOREBOARD (Photo 2 Centerpiece)
    // =======================================================================
    const float bw = gw - pw * 2.0f - 24.0f * s;
    const float bh = 95.0f * s;
    const float bx0 = cx - bw * 0.5f, bx1 = cx + bw * 0.5f;
    const float by1 = tBeamY0 - 8.0f * s;
    const float by0 = by1 - bh;

    // Dark outer chassis frame
    glColor4f(0.07f, 0.09f, 0.13f, 0.96f);
    rect(bx0, by0, bx1, by1);

    // Inner dark dot-matrix LED screen
    glColor4f(0.02f, 0.04f, 0.07f, 1.0f);
    rect(bx0 + 6.0f * s, by0 + 6.0f * s, bx1 - 6.0f * s, by1 - 6.0f * s);

    // Glowing Cyan Neon Tube Border (Photo 2 signature feature)
    // Outer diffuse cyan glow
    glColor4f(0.0f, 0.85f, 1.0f, 0.35f);
    rect(bx0 + 4.0f * s, by0 + 4.0f * s, bx1 - 4.0f * s, by0 + 8.5f * s);
    rect(bx0 + 4.0f * s, by1 - 8.5f * s, bx1 - 4.0f * s, by1 - 4.0f * s);
    rect(bx0 + 4.0f * s, by0 + 4.0f * s, bx0 + 8.5f * s, by1 - 4.0f * s);
    rect(bx1 - 8.5f * s, by0 + 4.0f * s, bx1 - 4.0f * s, by1 - 4.0f * s);
    // Inner electric cyan sharp neon line
    glColor4f(0.35f, 1.0f, 1.0f, 0.95f);
    rect(bx0 + 5.5f * s, by0 + 5.5f * s, bx1 - 5.5f * s, by0 + 7.0f * s);
    rect(bx0 + 5.5f * s, by1 - 7.0f * s, bx1 - 5.5f * s, by1 - 5.5f * s);
    rect(bx0 + 5.5f * s, by0 + 5.5f * s, bx0 + 7.0f * s, by1 - 5.5f * s);
    rect(bx1 - 7.0f * s, by0 + 5.5f * s, bx1 - 5.5f * s, by1 - 5.5f * s);

    // Top Mounted Header Tab: "GRAND PRIX" (Photo 2)
    const float hw = 130.0f * s, hh = 16.0f * s;
    const float hx0 = cx - hw * 0.5f, hx1 = cx + hw * 0.5f;
    const float hy0 = by1 - 8.0f * s, hy1 = by1 + hh - 8.0f * s;
    glColor4f(0.06f, 0.08f, 0.12f, 1.0f);
    rect(hx0, hy0, hx1, hy1);
    glColor4f(1.0f, 0.82f, 0.20f, 0.95f);
    rect(hx0, hy0, hx1, hy0 + 1.5f * s);
    rect(hx0, hy1 - 1.5f * s, hx1, hy1);
    rect(hx0, hy0, hx0 + 1.5f * s, hy1);
    rect(hx1 - 1.5f * s, hy0, hx1, hy1);
    const char *gpHeader = "GRAND PRIX";
    float gpHPs = 1.25f * s;
    drawText(gpHeader, cx - textWidth(gpHeader, gpHPs) * 0.5f, hy0 + 3.5f * s, gpHPs, 1.0f, 0.85f, 0.20f);

    // Subtitle inside board: "ROUND 1  •  RACE RESULTS"
    const char *subBoard = "ROUND 1  •  CHAMPIONSHIP";
    float subPs = 1.15f * s;
    drawText(subBoard, cx - textWidth(subBoard, subPs) * 0.5f, by1 - 22.0f * s, subPs, 0.30f, 0.90f, 1.0f);

    // Main Big Glowing Yellow LED Display: "FINISH" (Photo 2)
    const char *mainText = "FINISH";
    float finPs = 4.2f * s;
    float finY = by0 + 20.0f * s;
    // Ambient LED drop shadow
    drawText(mainText, cx - textWidth(mainText, finPs) * 0.5f + 1.5f * s, finY - 1.5f * s, finPs, 0.55f, 0.35f, 0.0f);
    // Glowing amber-yellow face
    drawText(mainText, cx - textWidth(mainText, finPs) * 0.5f, finY, finPs, 1.0f, 0.88f, 0.15f);

    // =======================================================================
    // 4. UPPER HORIZONTAL LED TICKER RIBBON (Photo 2)
    // =======================================================================
    const float th1 = 28.0f * s;
    const float ty1_top = by0 - 8.0f * s;
    const float ty1_bot = ty1_top - th1;

    // Dark chassis
    glColor4f(0.06f, 0.08f, 0.12f, 0.95f);
    rect(bx0, ty1_bot, bx1, ty1_top);
    glColor4f(0.25f, 0.35f, 0.45f, 0.80f);
    rect(bx0, ty1_bot, bx1, ty1_bot + 1.2f * s);
    rect(bx0, ty1_top - 1.2f * s, bx1, ty1_top);
    rect(bx0, ty1_bot, bx0 + 1.2f * s, ty1_top);
    rect(bx1 - 1.2f * s, ty1_bot, bx1, ty1_top);

    // Vertical mounting bracket links from main board
    for (float lx : {bx0 + 28.0f * s, bx1 - 28.0f * s}) {
        glColor4f(0.18f, 0.20f, 0.25f, 1.0f);
        rect(lx - 2.0f * s, ty1_top, lx + 2.0f * s, by0);
    }

    // Upper Ticker Text: Winner announcement with checkered flag motif
    char upBuf[64];
    if (isSingle && order[0] == 0) snprintf(upBuf, sizeof upBuf, "1ST PLACE: YOU - WINNER!");
    else                           snprintf(upBuf, sizeof upBuf, "1ST PLACE: %s - WINNER!", racerName(order[0]).c_str());
    float upPs = 1.15f * s;
    drawText(upBuf, cx - textWidth(upBuf, upPs) * 0.5f, ty1_bot + 8.5f * s, upPs, 1.0f, 0.88f, 0.25f);

    // =======================================================================
    // 5. LOWER HORIZONTAL LED TICKER RIBBON (Photo 2 Telemetry & Stats)
    // =======================================================================
    const float th2 = 28.0f * s;
    const float ty2_top = ty1_bot - 6.0f * s;
    const float ty2_bot = ty2_top - th2;

    glColor4f(0.06f, 0.08f, 0.12f, 0.95f);
    rect(bx0, ty2_bot, bx1, ty2_top);
    glColor4f(0.25f, 0.35f, 0.45f, 0.80f);
    rect(bx0, ty2_bot, bx1, ty2_bot + 1.2f * s);
    rect(bx0, ty2_top - 1.2f * s, bx1, ty2_top);
    rect(bx0, ty2_bot, bx0 + 1.2f * s, ty2_top);
    rect(bx1 - 1.2f * s, ty2_bot, bx1, ty2_top);

    // Bracket links between tickers
    for (float lx : {bx0 + 28.0f * s, bx1 - 28.0f * s}) {
        glColor4f(0.18f, 0.20f, 0.25f, 1.0f);
        rect(lx - 2.0f * s, ty2_top, lx + 2.0f * s, ty1_bot);
    }

    // Formatted LED telemetry matching Photo 2: "TIME 01:24:58   LAP 3/3   POSITION 1ST   TOP 240 KM/H"
    char timeTxt[32], lapTxt[32], posTxt[32], spdTxt[32];
    if (cFocus.finished) {
        int fm = int(cFocus.finishTime) / 60;
        snprintf(timeTxt, sizeof timeTxt, "TIME %02d:%04.1f", fm, cFocus.finishTime - fm * 60);
    } else {
        snprintf(timeTxt, sizeof timeTxt, "TIME DNF");
    }
    snprintf(lapTxt,  sizeof lapTxt,  "RACERS %d", n);
    snprintf(posTxt,  sizeof posTxt,  "POSITION %s", ORD[std::clamp(simEngine.rankOf(focus), 1, sim::MAX_RACERS) - 1]);
    snprintf(spdTxt,  sizeof spdTxt,  "TOP %d KM/H", kmh(cFocus));

    float t2Ps = 1.12f * s;
    float segW1 = textWidth(timeTxt, t2Ps);
    float segW2 = textWidth(lapTxt, t2Ps);
    float segW3 = textWidth(posTxt, t2Ps);
    float segW4 = textWidth(spdTxt, t2Ps);
    float totalTxtW = segW1 + segW2 + segW3 + segW4 + 36.0f * s;
    float startX = cx - totalTxtW * 0.5f;

    // Render telemetry with distinctive LED colors (Yellow & Cyan like Photo 2)
    float curX = startX;
    drawText(timeTxt, curX, ty2_bot + 8.5f * s, t2Ps, 1.0f, 0.85f, 0.25f);
    curX += segW1 + 12.0f * s;
    drawText(lapTxt,  curX, ty2_bot + 8.5f * s, t2Ps, 0.25f, 0.90f, 1.0f);
    curX += segW2 + 12.0f * s;
    drawText(posTxt,  curX, ty2_bot + 8.5f * s, t2Ps, 0.25f, 0.90f, 1.0f);
    curX += segW3 + 12.0f * s;
    drawText(spdTxt,  curX, ty2_bot + 8.5f * s, t2Ps, 1.0f, 0.85f, 0.25f);

    // =======================================================================
    // 6. LEADERBOARD: every car from 1st to last
    // =======================================================================
    const float rowH = 21.0f * s;
    const float lb_h = 22.0f * s + n * rowH;
    const float lb_y1 = ty2_bot - 8.0f * s;
    const float lb_y0 = lb_y1 - lb_h;

    // Table background panel
    glColor4f(0.08f, 0.10f, 0.16f, 0.92f);
    rect(bx0, lb_y0, bx1, lb_y1);
    glColor4f(0.18f, 0.26f, 0.38f, 0.70f);
    rect(bx0, lb_y0, bx1, lb_y0 + 1.0f * s);
    rect(bx0, lb_y1 - 1.0f * s, bx1, lb_y1);
    rect(bx0, lb_y0, bx0 + 1.0f * s, lb_y1);
    rect(bx1 - 1.0f * s, lb_y0, bx1, lb_y1);

    // Header bar and columns
    float colPs = 1.05f * s;
    glColor4f(0.12f, 0.16f, 0.24f, 0.95f);
    rect(bx0 + 2.0f * s, lb_y1 - 18.0f * s, bx1 - 2.0f * s, lb_y1 - 2.0f * s);
    const float cRank = bx0 + 16.0f * s, cName = bx0 + 62.0f * s, cTime = bx0 + 0.36f * bw;
    const float cBest = bx0 + 0.49f * bw, cTop = bx0 + 0.60f * bw, cWreck = bx0 + 0.72f * bw;
    const float cRam = bx0 + 0.80f * bw, cStat = bx0 + 0.88f * bw;
    const float hy = lb_y1 - 14.5f * s;
    drawText("RANK", cRank, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("RACER", cName, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("TIME", cTime, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("BEST LAP", cBest, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("TOP SPEED", cTop, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("WRECKS", cWreck, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("RAMS", cRam, hy, colPs, 0.65f, 0.78f, 0.90f);
    drawText("STATUS", cStat, hy, colPs, 0.65f, 0.78f, 0.90f);

    for (int r = 0; r < n; ++r) {
        const int i = order[r];
        const sim::Car &c = cars[i];
        const bool mine = isSingle && i == 0;
        const float ry1 = lb_y1 - 21.0f * s - r * rowH, ry0 = ry1 - rowH + 3.0f * s;
        if (r == 0)    glColor4f(0.20f, 0.17f, 0.05f, 0.75f);    // gold card for the winner
        else if (mine) glColor4f(0.05f, 0.18f, 0.24f, 0.80f);    // your row stands out
        else           glColor4f(0.09f, 0.13f, 0.20f, 0.75f);
        rect(bx0 + 4.0f * s, ry0, bx1 - 4.0f * s, ry1);
        glColor4f(c.r, c.g, c.b, 1.0f);                           // each car's colour on the left
        rect(bx0 + 4.0f * s, ry0, bx0 + 9.0f * s, ry1);

        char timeBuf[32], bestBuf[16], topBuf[16];
        if (c.finished && r == 0) {
            int m = int(c.finishTime) / 60;
            snprintf(timeBuf, sizeof timeBuf, "%02d:%04.1f", m, c.finishTime - m * 60);
        } else if (c.finished) {
            snprintf(timeBuf, sizeof timeBuf, "+%.1f SEC", std::max(0.0f, c.finishTime - cWin.finishTime));
        } else {
            snprintf(timeBuf, sizeof timeBuf, c.out ? "OUT +%dM" : "DNF +%dM", int(std::max(0.0f, simEngine.raceDistance() - c.progress)));
        }
        if (c.bestLap > 0.0f) {
            const int m = int(c.bestLap) / 60;
            snprintf(bestBuf, sizeof bestBuf, "%d:%04.1f", m, c.bestLap - float(m) * 60.0f);
        } else {
            snprintf(bestBuf, sizeof bestBuf, "-");
        }
        snprintf(topBuf, sizeof topBuf, "%d KM/H", kmh(c));
        const char *status = c.out ? "ELIMINATED" : !c.finished ? "DNF" : r == 0 ? "WINNER" : r < 3 ? "PODIUM" : "FINISHED";
        const float tr = r == 0 ? 1.0f : mine ? 0.55f : 0.78f, tg = r == 0 ? 0.92f : mine ? 0.95f : 0.86f;
        const float tb = r == 0 ? 0.45f : mine ? 1.0f : 0.95f;
        const float ty = ry0 + (rowH - 3.0f * s - 7.0f * colPs) * 0.5f;
        drawText(ORD[r], cRank, ty, colPs, tr, tg, tb);
        const std::string nm = racerName(i);
        const float ns = fitSize(nm.c_str(), colPs, cTime - cName - 10.0f * s);
        drawText(nm.c_str(), cName, ty + 3.5f * (colPs - ns), ns, tr, tg, tb);
        drawText(timeBuf, cTime, ty, colPs, tr, tg, tb);
        drawText(bestBuf, cBest, ty, colPs, tr, tg, tb);
        drawText(topBuf, cTop, ty, colPs, tr, tg, tb);
        char wreckBuf[8], ramBuf[8];
        snprintf(wreckBuf, sizeof wreckBuf, "%d", c.wrecks);
        snprintf(ramBuf, sizeof ramBuf, "%d", c.crashesCaused);
        drawText(wreckBuf, cWreck, ty, colPs, tr, tg, tb);
        drawText(ramBuf, cRam, ty, colPs, c.crashesCaused ? 1.0f : tr, c.crashesCaused ? 0.45f : tg, c.crashesCaused ? 0.35f : tb);
        drawText(status, cStat, ty, colPs, tr, tg, tb);
    }

    // =======================================================================
    // 7. INTERACTIVE ACTION BUTTONS (Gantry Base)
    // =======================================================================
    float btnY = gy0 + 10.0f * s;
    float btnH = 24.0f * s;
    float btnPs = 1.15f * s;

    // Button 1: [ENTER / R] RACE AGAIN (Glowing arcade green with pulse)
    const char *b1 = "[ENTER / R]  RACE AGAIN";
    float b1W = textWidth(b1, btnPs) + 20.0f * s;
    float b1X = cx - b1W - 10.0f * s;
    float pulse = 0.85f + 0.15f * std::sin(float(simEngine.getTick()) * 0.20f);
    glColor4f(0.10f, 0.32f, 0.14f, 0.90f);
    rect(b1X, btnY, b1X + b1W, btnY + btnH);
    glColor4f(0.25f, 0.95f, 0.35f, pulse);
    rect(b1X, btnY, b1X + b1W, btnY + 1.5f * s);
    rect(b1X, btnY + btnH - 1.5f * s, b1X + b1W, btnY + btnH);
    rect(b1X, btnY, b1X + 1.5f * s, btnY + btnH);
    rect(b1X + b1W - 1.5f * s, btnY, b1X + b1W, btnY + btnH);
    drawText(b1, b1X + 10.0f * s, btnY + 6.5f * s, btnPs, 0.40f, 1.0f, 0.50f);

    // Button 2: [M] MAIN MENU (Arcade blue / cyan)
    const char *b2 = "[M]  MAIN MENU";
    float b2W = textWidth(b2, btnPs) + 20.0f * s;
    float b2X = cx + 10.0f * s;
    glColor4f(0.10f, 0.18f, 0.30f, 0.90f);
    rect(b2X, btnY, b2X + b2W, btnY + btnH);
    glColor4f(0.35f, 0.75f, 1.0f, 0.85f);
    rect(b2X, btnY, b2X + b2W, btnY + 1.5f * s);
    rect(b2X, btnY + btnH - 1.5f * s, b2X + b2W, btnY + btnH);
    rect(b2X, btnY, b2X + 1.5f * s, btnY + btnH);
    rect(b2X + b2W - 1.5f * s, btnY, b2X + b2W, btnY + btnH);
    drawText(b2, b2X + 10.0f * s, btnY + 6.5f * s, btnPs, 0.65f, 0.85f, 1.0f);
}
