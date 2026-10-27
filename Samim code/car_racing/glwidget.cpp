#include "glwidget.h"
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
#include <QFileInfo>
#include <QDateTime>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ===========================================================================
//  CONSTANTS
// ===========================================================================
namespace {

const float PI          = 3.14159265f;
const float TRACK_LEN   = 4000.0f;            // finish line distance
const float MAX_SPEED   = 1.10f;
const float NITRO_SPEED = 1.65f;
const float OFFROAD_MAX = 0.45f;
const float VIEW_DIST   = 340.0f;             // draw distance (fog end)
const float TEMPLE_DIST = 400.0f;             // temple "backdrop" distance

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

const ThemeLook THEMES[5] = {
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

static const MenuItem MENU_ITEMS[7] = {
    {"PLAY VS CPU",            true},
    {"SPLIT SCREEN (2P)",       false},
    {"ONLINE MULTIPLAYER (8P)", true},
    {"CHOOSE CAR",              false},
    {"CHOOSE MAP",              false},
    {"LOBBY & PROFILE",         true},
    {"CPU DIFFICULTY",          false}
};
static const int NUM_MENU_ITEMS = 7;

} // namespace

// ===========================================================================
//  CONSTRUCTION
// ===========================================================================
GLWidget::GLWidget(QWidget *parent)
    : QOpenGLWidget(parent), rng(std::random_device{}())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(800, 450);
    audio = new AudioEngine(this);
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

    elapsedTimer.start();
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &GLWidget::updateGame);
    timer->start(16); // ~60 ticks per second
}

GLWidget::~GLWidget()
{
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


// Menu pickers: 3 = car, 4 = map, 6 = CPU difficulty. Returns false for other items.
bool GLWidget::cycleMenuOption(int item, int dir)
{
    if (item == 3) {
        selectedCarPreset = (selectedCarPreset + dir + NUM_CAR_PRESETS) % NUM_CAR_PRESETS;
    } else if (item == 4) {
        selectedMapPreset = (selectedMapPreset + dir + sim::NUM_MAPS) % sim::NUM_MAPS;
        simEngine.setMap(selectedMapPreset);
    } else if (item == 6) {
        simEngine.setAiDifficulty((simEngine.getAiDifficulty() + dir + 3) % 3);
    } else {
        return false;
    }
    return true;
}

void GLWidget::startRace(GameMode mode)
{
    simEngine.setMap(selectedMapPreset);
    simEngine.startRace(mode);
    if (!simEngine.getCars().empty()) {
        simEngine.getCars()[0].r = CAR_PRESETS[selectedCarPreset].r;
        simEngine.getCars()[0].g = CAR_PRESETS[selectedCarPreset].g;
        simEngine.getCars()[0].b = CAR_PRESETS[selectedCarPreset].b;
    }
    view1 = CarView();
    view2 = CarView();
    for (CarFx &f : carFx) f = CarFx();
    skidMarks.clear();
    for (float &t : nitroBannerTimer) t = 0.0f;
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
    if (particles.size() > 300) return;
    Particle p;
    p.type = t;
    p.x = x; p.y = y; p.z = z;
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
            if (p.y < 0.05f) { p.y = 0.05f; p.vy = -p.vy * 0.4f; p.vx *= 0.6f; p.vz *= 0.6f; }
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
            skidMarks.push_back({fx.lastX[w], fx.lastZ[w], wx, wz, c.isDrifting ? 0.60f : 0.45f});
        }
        fx.lastX[w] = wx;
        fx.lastZ[w] = wz;
    }
    fx.marking = mark;
    if (skidMarks.size() > 1600) skidMarks.erase(skidMarks.begin(), skidMarks.begin() + 200);
}

void GLWidget::drawSkidMarks(float camZ)
{
    if (skidMarks.empty()) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const SkidMark &m : skidMarks) {
        if (m.z0 > camZ + 30.0f || m.z0 < camZ - VIEW_DIST) continue;
        float dx = m.x1 - m.x0, dz = m.z1 - m.z0, l = std::sqrt(dx * dx + dz * dz);
        if (l < 1e-4f) continue;
        float nx = -dz / l * 0.13f, nz = dx / l * 0.13f;     // half tyre width
        glColor4f(0.04f, 0.04f, 0.045f, m.alpha);
        glVertex3f(m.x0 - nx, 0.035f, m.z0 - nz); glVertex3f(m.x0 + nx, 0.035f, m.z0 + nz);
        glVertex3f(m.x1 + nx, 0.035f, m.z1 + nz); glVertex3f(m.x1 - nx, 0.035f, m.z1 - nz);
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

    if (f == 40)  { menuSelection = 4; cycleMenuOption(4, 1); }      // highlight the map picker
    if (f == 90)  shot("01_menu_map_picker");
    if (f == 95)  cycleMenuOption(4, -1);

    // one race per map: countdown (3 s), then ~3 s of flat-out driving, then a screenshot
    const int PER_MAP = 420, FIRST = 120;
    int m = (f - FIRST) / PER_MAP, t = (f - FIRST) % PER_MAP;
    if (f >= FIRST && m < sim::NUM_MAPS) {
        if (t == 0) { selectedMapPreset = m; startRace(sim::GameSim::ModeSingle); }
        keys.insert(Qt::Key_Up);
        if (t == 390) shot(QString("map%1_%2").arg(m + 1).arg(QString(sim::MAPS[m].name).replace(' ', '_').toLower()));
    }
    const int SPLIT = FIRST + sim::NUM_MAPS * PER_MAP;
    if (f == SPLIT) { keys.clear(); selectedMapPreset = 3; startRace(sim::GameSim::ModeSplitScreen); }
    if (f > SPLIT) { keys.insert(Qt::Key_Up); keys.insert(Qt::Key_W); }
    if (f == SPLIT + 380) shot("split_screen_city");
    if (f == SPLIT + 400) QCoreApplication::quit();
}

void GLWidget::updateGame()
{
    if (!autoShotDir.isEmpty()) runAutoShot();

    float realDt = float(elapsedTimer.nsecsElapsed() * 1e-9);   // not whole ms: they lose ~3% a frame
    elapsedTimer.restart();
    if (realDt <= 0.0f) realDt = 0.016f;
    if (realDt > 0.25f) realDt = 0.25f;

    for (int i = 0; i < sim::MAX_RACERS; ++i) {
        if (nitroBannerTimer[i] > 0.0f) {
            nitroBannerTimer[i] = std::max(0.0f, nitroBannerTimer[i] - realDt);
        }
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
    if (isOnline()) netUpdate(realDt);          // online: the server runs the race (glwidget_online.cpp)
    else simAccumulator += realDt;

    while (!isOnline() && simAccumulator >= FIXED_DT) {
        simAccumulator -= FIXED_DT;

        // Prepare inputs
        sim::PlayerInput inputs[2];
        inputs[0].up = isDown(Qt::Key_Up);
        inputs[0].down = isDown(Qt::Key_Down);
        inputs[0].left = isDown(Qt::Key_Left);
        inputs[0].right = isDown(Qt::Key_Right);
        inputs[0].nitro = isDown(Qt::Key_Return) || isDown(Qt::Key_Enter) || isDown(Qt::Key_Slash) || isDown(Qt::Key_N);

        inputs[1].up = isDown(Qt::Key_W);
        inputs[1].down = isDown(Qt::Key_S);
        inputs[1].left = isDown(Qt::Key_A);
        inputs[1].right = isDown(Qt::Key_D);
        inputs[1].nitro = isDown(Qt::Key_Space);

        simEngine.step(inputs, 2);
        processSimEvents(simEngine.getEvents());
        simEngine.clearEvents();
        for (int i = 0; i < 2 && i < int(simEngine.getCars().size()); ++i)
            updateCarFx(i, simEngine.getCars()[i]);
    }

    const auto &cars = simEngine.getCars();
    float step = realDt * 60.0f;

    auto updateCarView = [&](CarView &v, const sim::Car &c) {
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
        audio->update(std::fabs(me.speed) / sim::MAX_SPEED, me.isDrifting, me.nitroOn);
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
    btnH = std::clamp(availH / 7.6f, 38.0f * dpr, 46.0f * dpr);
    btnGap = std::clamp((availH - 7.0f * btnH) / 6.0f, 6.0f * dpr, 10.0f * dpr);
    ps = std::clamp(H / 420.0f, 1.30f, 1.75f);
}
} // namespace

void GLWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    int k = event->key();

    if (netScreen != NetScreen::Off && k != Qt::Key_F12 && handleOnlineKey(event)) return;

    // Universal restart: Pressing 'R' restarts race immediately from any gameplay state
    if (k == Qt::Key_R) {
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

    if (simEngine.getState() == State::Menu) {
        const int MENU_COUNT = 7;
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
            menuSelection = 0;
            startRace(sim::GameSim::ModeSingle);
            return;
        } else if (k == Qt::Key_2) {
            menuSelection = 1;
            startRace(sim::GameSim::ModeSplitScreen);
            return;
        } else if (k >= Qt::Key_3 && k <= Qt::Key_7) {
            menuSelection = k - Qt::Key_1;
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_M) {
            if (audio) audio->toggleMute();
            update();
            return;
        } else if (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_A || k == Qt::Key_D) {
            int dir = (k == Qt::Key_Left || k == Qt::Key_A) ? -1 : 1;
            if (cycleMenuOption(menuSelection, dir) && audio) audio->playCountdownBeep(false);
            update();
            return;
        } else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (menuSelection == 0) {
                startRace(sim::GameSim::ModeSingle);
            } else if (menuSelection == 1) {
                startRace(sim::GameSim::ModeSplitScreen);
            } else if (menuSelection == 2) {
                openOnline();
            } else {
                cycleMenuOption(menuSelection, 1);
            }
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        }
    } else if (simEngine.getState() == State::Paused) {
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
            return;
        } else if (k == Qt::Key_C) {
            view1.camMode = (view1.camMode + 1) % 3;
            return;
        } else if (k == Qt::Key_M) {
            if (audio) audio->toggleMute();
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
    QOpenGLWidget::focusOutEvent(event);
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (simEngine.getState() == State::Menu && activeModal == ModalNone && netScreen == NetScreen::Off) {
        float dpr = float(devicePixelRatioF());
        float mx = float(event->position().x()) * dpr;
        float my = float(height() - event->position().y()) * dpr;
        float W = float(width()) * dpr, H = float(height()) * dpr;
        float btnX, btnW, btnH, btnGap, listTop, ps;
        computeMenuMetrics(W, H, dpr, btnX, btnW, btnH, btnGap, listTop, ps);

        for (int i = 0; i < 7; ++i) {
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
    QOpenGLWidget::mouseMoveEvent(event);
}

void GLWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (netScreen != NetScreen::Off) return;     // online screens are keyboard-driven
    if (simEngine.getState() == State::Menu) {
        if (activeModal != ModalNone) {
            activeModal = ModalNone;
            if (audio) audio->playCountdownBeep(false);
            update();
            return;
        }

        float dpr = float(devicePixelRatioF());
        float mx = float(event->position().x()) * dpr;
        float my = float(height() - event->position().y()) * dpr;
        float W = float(width()) * dpr, H = float(height()) * dpr;
        float btnX, btnW, btnH, btnGap, listTop, ps;
        computeMenuMetrics(W, H, dpr, btnX, btnW, btnH, btnGap, listTop, ps);

        for (int i = 0; i < 7; ++i) {
            float by0 = listTop - (i + 1) * btnH - i * btnGap;
            float by1 = by0 + btnH;
            float bx0 = btnX;
            float bx1 = btnX + btnW;

            if (mx >= bx0 && mx <= bx1 && my >= by0 && my <= by1) {
                menuSelection = i;
                if (i == 0) {
                    startRace(sim::GameSim::ModeSingle);
                } else if (i == 1) {
                    startRace(sim::GameSim::ModeSplitScreen);
                } else if (i == 2) {
                    openOnline();
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
    QOpenGLWidget::mousePressEvent(event);
}


// ===========================================================================
//  OPENGL SETUP + PROCEDURAL PIXEL TEXTURES
// ===========================================================================
void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();

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
    const float dpr = float(devicePixelRatioF());
    const int W = int(width() * dpr), H = int(height() * dpr);

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

    if (simEngine.getState() == State::Menu) {
        drawShowcaseScene(W, H);
        glViewport(0, 0, W, H);
        drawMainMenu(W, H, dpr);
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

    if (simEngine.getState() == State::Paused) {
        glViewport(0, 0, W, H);
        drawPauseOverlay(W, H, dpr);
    } else if (simEngine.getState() == State::Finished) {
        glViewport(0, 0, W, H);
        drawFinishOverlay(W, H, dpr);
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

    // =======================================================================
    // 1. INDOOR GARAGE ARCHITECTURE
    // =======================================================================
    // A. Dark Polished Showroom Floor (Reflective epoxy tile look)
    glDisable(GL_LIGHTING);
    glColor3f(0.045f, 0.055f, 0.075f);
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
    float platR = 3.6f;
    float platH = 0.08f;

    // Dark metallic disc surface
    glColor3f(0.10f, 0.12f, 0.16f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(focusX, platH, focusZ);
    for (int i = 0; i <= 36; ++i) {
        float a = i * 2.0f * PI / 36.0f;
        glVertex3f(focusX + std::cos(a) * platR, platH, focusZ + std::sin(a) * platR);
    }
    glEnd();

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
    float carHeading = -24.0f * PI / 180.0f;

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
    float carR = CAR_PRESETS[selectedCarPreset].r, carG = CAR_PRESETS[selectedCarPreset].g, carB = CAR_PRESETS[selectedCarPreset].b;
    drawCar(focusX, focusZ, carHeading, 0.0f, 0.0f, 0.12f, 0.0f, carR, carG, carB, false, "");

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
}


void GLWidget::renderView(const sim::Car &me, const CarView &meView,
                    const sim::Car &other, const CarView &otherView,
                    int playerNum, int vx, int vy, int vw, int vh)
{
    (void)otherView;
    glViewport(vx, vy, vw, vh);

    // Per-map sky, fog, sunlight and reflections
    const ThemeLook &th = THEMES[themeIndex()];
    nightMode = th.night;
    ensureEnvMap(themeIndex());
    GLfloat fogCol[4] = {th.fog[0], th.fog[1], th.fog[2], 1.0f};
    glFogfv(GL_FOG_COLOR, fogCol);
    glFogf(GL_FOG_START, th.fogStart);
    glFogf(GL_FOG_END, th.fogEnd);
    GLfloat amb[4] = {th.ambient[0], th.ambient[1], th.ambient[2], 1.0f};
    GLfloat dif[4] = {th.diffuse[0], th.diffuse[1], th.diffuse[2], 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);

    drawSky(vw, vh);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(meView.fov, double(vw) / double(std::max(vh, 1)), 0.5, 800.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    if (meView.camMode == 0) {
        // Low chase camera that drops back as speed builds up
        float fx = std::sin(meView.camHeading), fz = -std::cos(meView.camHeading);
        float jx = std::sin(simEngine.getTick() * 1.9f) * meView.shake * 0.25f;
        float jy = std::cos(simEngine.getTick() * 2.3f) * meView.shake * 0.20f;
        float d = meView.camDist;
        float ex = me.x - fx * d + jx, ey = 1.9f + 0.12f * d + jy, ez = me.z - fz * d;
        gluLookAt(ex, ey, ez, me.x + fx * 6.0f, 1.0f, me.z + fz * 6.0f, 0, 1, 0);
    } else if (meView.camMode == 1) {
        float fx = std::sin(me.heading), fz = -std::cos(me.heading);
        float jx = std::sin(simEngine.getTick() * 2.5f) * meView.shake * 0.15f;
        float jy = std::cos(simEngine.getTick() * 2.8f) * meView.shake * 0.12f;
        float ex = me.x + fx * 0.4f + jx, ey = 1.35f + jy, ez = me.z + fz * 0.4f;
        gluLookAt(ex, ey, ez, me.x + fx * 25.0f, 1.1f, me.z + fz * 25.0f, 0, 1, 0);
    } else {
        float fx = std::sin(me.heading), fz = -std::cos(me.heading);
        float ex = me.x + fx * 1.8f, ey = 0.55f, ez = me.z + fz * 1.8f;
        gluLookAt(ex, ey, ez, me.x + fx * 35.0f, 0.45f, me.z + fz * 35.0f, 0, 1, 0);
    }

    GLfloat lp[] = {0.45f, 1.0f, 0.65f, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lp);

    glEnable(GL_DEPTH_TEST);

    glDisable(GL_LIGHTING); glDisable(GL_FOG);
    float camRefX = me.x, camRefZ = me.z;
    drawSunMountainsClouds(camRefX, camRefZ);
    glEnable(GL_LIGHTING);
    drawTemple(camRefX, camRefZ);

    glEnable(GL_FOG);
    drawGround(camRefX, camRefZ);
    if (simEngine.mapDef().theme == sim::MapTheme::Coast) drawSea(me.z);
    drawRoad(me.z);
    drawSkidMarks(me.z);
    // the gates (the finish one has a whole scene around it) only when they can be seen
    if (me.z > -60.0f) drawArch(-6.0f, false);
    const float toFinish = me.z + sim::TRACK_LEN;
    if (toFinish < VIEW_DIST + 120.0f && toFinish > -250.0f) drawArch(-sim::TRACK_LEN, true);
    drawProps(me.z);
    drawObstacles(me.z);
    drawNitroPickups(me.z);

    // shadows
    glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
    for (const sim::Traffic &t : simEngine.getTraffic()) {
        if (t.parked || t.z > me.z + 25.0f || t.z < me.z - VIEW_DIST) continue;
        drawShadow(roadCenter(t.z) + t.lane, t.z, roadAngle(t.z), t.truck ? 1.25f : 1.1f, t.truck ? 3.6f : 2.3f);
    }
    const auto &allCars = simEngine.getCars();
    const int meIdx = (&me >= allCars.data() && &me < allCars.data() + allCars.size()) ? int(&me - allCars.data()) : 0;
    for (int i = 0; i < int(allCars.size()); ++i) {
        if (i == meIdx && meView.camMode != 0) continue;     // no own shadow from the hood camera
        drawShadow(allCars[i].x, allCars[i].z, allCars[i].heading, 1.1f, 2.3f);
    }
    glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);

    // traffic
    for (const sim::Traffic &t : simEngine.getTraffic()) {
        if (t.parked || t.z > me.z + 25.0f || t.z < me.z - VIEW_DIST) continue;
        float tx = roadCenter(t.z) + t.lane, ta = roadAngle(t.z);
        float spin = std::fmod(float(simEngine.getTick()) * t.speed * 136.0f, 360.0f);
        if (t.truck) drawTruck(tx, t.z, ta, spin, t.r, t.g, t.b);
        else         drawCar(tx, t.z, ta, 0.0f, 0.0f, 0.0f, spin, t.r, t.g, t.b, false);
    }

    // Name plate above each car: "1"/"2" in split screen, "CPU" in single player, names online
    // (your own car online: "YOU/3" = you are 3rd, the same position as in the corner)
    auto badgeFor = [&](int i) -> QByteArray {
        if (isOnline()) {
            if (i == meIdx) return "YOU/" + QByteArray::number(simEngine.rankOf(i));
            return (i < racerNames.size()) ? racerNames[i].left(8).toUtf8() : QByteArray::number(i + 1);
        }
        if (simEngine.getGameMode() == sim::GameSim::ModeSingle && i == 1) return "CPU";
        return QByteArray::number(i + 1);
    };
    for (int i = 0; i < int(allCars.size()); ++i) {
        if (i == meIdx && meView.camMode != 0) continue;
        const sim::Car &c = allCars[i];
        QByteArray badge = badgeFor(i);
        drawCar(c.x, c.z, c.heading, c.roll, c.pitch, c.steerVis, c.wheelSpin, c.r, c.g, c.b, c.nitroOn,
                badge.constData(), (i < sim::MAX_RACERS) ? carFx[i].brakeGlow : 0.0f);
    }

    drawParticles();

    glDisable(GL_FOG); glDisable(GL_LIGHTING);

    begin2D(vw, vh);
    drawHUD(me, other, playerNum, vw, vh);
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
    Q_UNUSED(ex);
    float tz = -TRACK_LEN - TEMPLE_DIST;
    if (tz < ez - VIEW_DIST - 120.0f || tz > ez + 40.0f) return;
    float tx = roadCenter(-TRACK_LEN);

    glPushMatrix();
    glTranslatef(tx, 0.0f, tz);

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

    const float rad = VIEW_DIST + 30.0f;
    const int N = 36;
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, 1, 0);
    tv(ex, -0.05f, ez, ex, ez);
    for (int i = 0; i <= N; ++i) {
        float a = i * 2.0f * PI / N;
        float gx = ex + std::sin(a) * rad, gz = ez - std::cos(a) * rad;
        tv(gx, -0.05f, gz, gx, gz);
    }
    glEnd();
    g_ts = 0.25f;
}

void GLWidget::drawRoad(float camZ)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texAsphalt);
    g_ts = 0.125f;   // one asphalt tile every 8 m
    glColor3f(1, 1, 1);

    const float seg = 2.0f;
    float startZ = std::min(40.0f, std::ceil((camZ + 30.0f) / seg) * seg);   // incl. the grid behind the line
    float endZ   = std::max(-TRACK_LEN - 30.0f, camZ - VIEW_DIST);

    // 1. Asphalt Road Surface
    glBegin(GL_QUAD_STRIP);
    glNormal3f(0, 1, 0);
    for (float z = startZ; z >= endZ; z -= seg) {
        float c = roadCenter(z);
        tv(c - roadHalf(), 0.0f, z, -roadHalf(), z);
        tv(c + roadHalf(), 0.0f, z,  roadHalf(), z);
    }
    glEnd();

    glDisable(GL_TEXTURE_2D);

    // 2. Red & White Curbs (Kerbs) - locked to fixed world coordinates
    for (float side : {-1.0f, 1.0f}) {
        glBegin(GL_QUAD_STRIP);
        glNormal3f(0, 1, 0);
        for (float z = startZ; z >= endZ; z -= seg) {
            float c = roadCenter(z), x0 = c + side * roadHalf(), x1 = x0 + side * 0.7f;
            int k = int(std::floor((-z + 0.001f) / 4.0f));
            glColor3fv((k % 2 == 0) ? THEMES[themeIndex()].kerbA : THEMES[themeIndex()].kerbB);
            glVertex3f(x0, 0.02f, z); glVertex3f(x1, 0.02f, z);
        }
        glEnd();
    }

    // 3. Continuous Outer Boundary Lines (Solid White)
    glColor3f(0.95f, 0.95f, 0.95f);
    for (float side : {-1.0f, 1.0f}) {
        glBegin(GL_QUAD_STRIP);
        glNormal3f(0, 1, 0);
        for (float z = startZ; z >= endZ; z -= seg) {
            float c = roadCenter(z);
            float x = c + side * (roadHalf() - 0.40f);
            glVertex3f(x - 0.12f, 0.025f, z);
            glVertex3f(x + 0.12f, 0.025f, z);
        }
        glEnd();
    }

    // 4. Center Dashed Lane Divider Lines (Strictly Fixed in World Space)
    const float dashPeriod = 8.0f;
    const float dashLen = 4.0f;
    float dashStartZ = std::min(40.0f, std::ceil((camZ + 30.0f) / dashPeriod) * dashPeriod);
    float dashEndZ   = std::max(-TRACK_LEN, camZ - VIEW_DIST);

    glColor3f(0.96f, 0.94f, 0.88f);
    for (int l = 1; l < simEngine.laneCount(); ++l) {
        float off = simEngine.laneX(l) - 0.5f * sim::LANE_WIDTH;
        for (float z = dashStartZ; z >= dashEndZ; z -= dashPeriod) {
            float zNear = z;
            float zFar  = z - dashLen;
            if (zFar < -TRACK_LEN) zFar = -TRACK_LEN;
            float cNear = roadCenter(zNear);
            float cFar  = roadCenter(zFar);
            glBegin(GL_QUADS);
            glNormal3f(0, 1, 0);
            glVertex3f(cNear + off - 0.14f, 0.03f, zNear);
            glVertex3f(cNear + off + 0.14f, 0.03f, zNear);
            glVertex3f(cFar  + off + 0.14f, 0.03f, zFar);
            glVertex3f(cFar  + off - 0.14f, 0.03f, zFar);
            glEnd();
        }
    }

    // 5. Guard Rails and Posts (Posts strictly locked to fixed 12m intervals)
    const float postPeriod = 12.0f;
    float postStartZ = std::min(40.0f, std::ceil((camZ + 30.0f) / postPeriod) * postPeriod);
    for (float side : {-1.0f, 1.0f}) {
        glColor3f(0.70f, 0.72f, 0.76f);
        for (float z = postStartZ; z >= endZ; z -= postPeriod) {
            float c = roadCenter(z);
            boxAt(c + side * railOffset(), 0.55f, z, 0.10f, 0.55f, 0.10f);
        }
        glColor3f(0.82f, 0.84f, 0.88f);
        for (float ry : {0.50f, 0.95f}) {
            glBegin(GL_QUAD_STRIP);
            for (float z = startZ; z >= endZ; z -= seg) {
                float c = roadCenter(z), x = c + side * railOffset();
                glNormal3f(-side, 0, 0);
                glVertex3f(x, ry - 0.12f, z);
                glVertex3f(x, ry + 0.12f, z);
            }
            glEnd();
        }
    }
}

void GLWidget::drawArch(float z, bool finish)
{
    float c = roadCenter(z);
    glPushMatrix();
    glTranslatef(c, 0, z);

    if (!finish) {
        // Simple start line arch
        glColor3f(0.20f, 0.22f, 0.26f);
        boxAt(-roadHalf() - 1.0f, 3.5f, 0, 0.5f, 3.5f, 0.5f);
        boxAt( roadHalf() + 1.0f, 3.5f, 0, 0.5f, 3.5f, 0.5f);
        boxAt(0, 7.2f, 0, roadHalf() + 1.5f, 0.4f, 0.5f);
        glColor3f(0.12f, 0.14f, 0.18f);
        boxAt(0, 6.2f, 0, 6.0f, 0.9f, 0.08f);
        glDisable(GL_LIGHTING);
        glColor3f(0.2f, 1.0f, 0.4f);
        drawText3D("START", 0, 5.7f, 0.12f, 0.14f);
        glEnable(GL_LIGHTING);
    } else {
        // ===================================================================
        // GRAND ARCADE FINISH ARCH GANTRY & SCENIC FINISH ZONE
        // ===================================================================
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
        drawText3D("FINISH", 0, 6.65f, 0.51f, 0.20f);

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
        drawText3D("WINNER!", -3.3f, 5.02f, 0.36f, 0.068f);

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
        drawText3D("CONGRATULATIONS!", 3.3f, 5.06f, 0.36f, 0.036f);

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

        // Dark tire rubber skid trails leading up to the checkered line
        glColor4f(0.02f, 0.02f, 0.02f, 0.65f);
        for (int l = 0; l < simEngine.laneCount(); ++l) {
            const float lx = simEngine.laneX(l);
            for (float s : {-0.8f, 0.8f}) {
                glBegin(GL_QUADS);
                glVertex3f(lx + s - 0.22f, 0.045f, 24.0f);
                glVertex3f(lx + s + 0.22f, 0.045f, 24.0f);
                glVertex3f(lx + s + 0.22f, 0.045f, -6.0f);
                glVertex3f(lx + s - 0.22f, 0.045f, -6.0f);
                glEnd();
            }
        }

        // Red & white racing rumble curbs (curbing) flanking finish straight
        for (float side : {-1.0f, 1.0f}) {
            for (float rz = 45.0f; rz >= -45.0f; rz -= 3.0f) {
                int curbStep = int(std::floor((rz + 100.0f) / 3.0f));
                if (curbStep % 2 == 0) glColor3f(0.92f, 0.16f, 0.16f);
                else                   glColor3f(0.98f, 0.98f, 0.98f);
                float rx0 = side * roadHalf();
                float rx1 = side * (roadHalf() + 0.95f);
                glBegin(GL_QUADS);
                glVertex3f(rx0, 0.035f, rz);
                glVertex3f(rx1, 0.035f, rz);
                glVertex3f(rx1, 0.035f, rz - 3.0f);
                glVertex3f(rx0, 0.035f, rz - 3.0f);
                glEnd();
            }
        }

        // 8. Roadside Sponsor Billboards
        for (float sbz : { 28.0f, 62.0f, 96.0f }) {
            for (float side : { -1.0f, 1.0f }) {
                float bx = side * (railOffset() + 3.2f);
                glColor3f(0.25f, 0.25f, 0.28f);
                boxAt(bx, 1.5f, sbz, 0.15f, 1.5f, 0.15f);
                if (int(sbz) % 2 == 0) glColor3f(0.10f, 0.25f, 0.70f); // Blue
                else                   glColor3f(0.85f, 0.15f, 0.15f); // Red
                boxAt(bx, 3.2f, sbz, 0.12f, 1.0f, 4.5f);
                glDisable(GL_LIGHTING);
                glColor3f(1.0f, 0.9f, 0.2f);
                glPushMatrix();
                glTranslatef(bx - side * 0.15f, 3.0f, sbz);
                glRotatef(side > 0 ? -90.0f : 90.0f, 0, 1, 0);
                drawText3D(int(sbz) % 2 == 0 ? "ARCADE RACING" : "TURBO SPEED", 0, 0, 0, 0.14f);
                glPopMatrix();
                glEnable(GL_LIGHTING);
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
            glPushMatrix();
            glTranslatef(p.x, 0.0f, p.z);
            glRotatef(p.rot, 0, 1, 0);
            glScalef(p.scale, p.scale, p.scale);
            drawPalmTree();
            glPopMatrix();
        }

        // B. Rolling Lush Green Scenery Hills on both flanks
        for (float hz : { 80.0f, 40.0f, 0.0f, -40.0f, -80.0f }) {
            // Left flank rolling hills
            glColor3f(0.16f, 0.54f, 0.20f);
            boxAt(-roadHalf() - 18.0f, 3.5f, hz, 10.0f, 3.5f, 18.0f);
            glColor3f(0.14f, 0.48f, 0.18f);
            boxAt(-roadHalf() - 24.0f, 6.0f, hz + 5.0f, 8.0f, 6.0f, 14.0f);

            // Right flank rolling coastal mounds
            glColor3f(0.18f, 0.56f, 0.22f);
            boxAt( roadHalf() + 17.0f, 3.0f, hz, 9.0f, 3.0f, 18.0f);
        }

        // C. Coastal Rocky Cliff Formations overlooking the sea (Right side)
        glColor3f(0.42f, 0.40f, 0.38f);
        for (float rz : { 30.0f, -10.0f, -50.0f, -90.0f }) {
            boxAt(roadHalf() + 26.0f, 4.5f, rz, 7.0f, 4.5f, 16.0f);
            glColor3f(0.36f, 0.34f, 0.32f);
            boxAt(roadHalf() + 29.0f, 8.0f, rz - 6.0f, 6.0f, 8.0f, 12.0f);
        }

        // D. Scenic Lighthouse on the coastal cliff in the distance past the finish line
        glPushMatrix();
        glTranslatef(roadHalf() + 32.0f, 0.0f, -95.0f);
        drawLighthouse();
        glPopMatrix();
        }

        // E. Red & White Chevron Direction Signs on the bend past the finish line
        for (float cz : { -32.0f, -62.0f, -92.0f }) {
            float cx = roadHalf() + 2.2f;
            // Sign post
            glColor3f(0.30f, 0.32f, 0.35f);
            boxAt(cx, 1.3f, cz, 0.08f, 1.3f, 0.08f);
            // Red board
            glColor3f(0.85f, 0.15f, 0.15f);
            boxAt(cx, 2.2f, cz, 0.06f, 0.55f, 1.4f);
            // White chevron arrows ">>>"
            glDisable(GL_LIGHTING);
            glColor3f(0.98f, 0.98f, 0.98f);
            glPushMatrix();
            glTranslatef(cx - 0.08f, 2.05f, cz);
            glRotatef(-90.0f, 0, 1, 0);
            drawText3D(">>>", 0, 0, 0, 0.06f);
            glPopMatrix();
            glEnable(GL_LIGHTING);
        }
    }
    glPopMatrix();
}

// ===========================================================================
//  SCENERY PROPS
// ===========================================================================
void GLWidget::drawProps(float camZ)
{
    for (const sim::Prop &p : simEngine.getProps()) {
        if (p.z > camZ + 30.0f || p.z < camZ - VIEW_DIST) continue;
        glPushMatrix();
        glTranslatef(p.x, 0.0f, p.z);
        glRotatef(p.rot, 0, 1, 0);
        if (p.type == sim::Prop::Building) {           // scale = height only, footprint stays
            drawBuilding(p.scale, p.palette, int(-p.z));
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

void GLWidget::drawSea(float camZ)
{
    // Beach, surf and ocean to the right of the coastal road (follows the road's bends)
    const float seg = 8.0f;
    float startZ = std::ceil((camZ + 40.0f) / seg) * seg, endZ = camZ - VIEW_DIST - 20.0f;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    struct Band { float x0, x1; float c0[4], c1[4]; float y; };
    const float wave = 0.6f * std::sin(simEngine.getTick() * 0.03f);
    const Band bands[4] = {
        {railOffset() + 1.0f,  railOffset() + 15.0f, {0.86f, 0.78f, 0.58f, 1.0f}, {0.80f, 0.72f, 0.52f, 1.0f}, -0.02f},
        {railOffset() + 15.0f + wave, railOffset() + 16.2f + wave, {0.95f, 0.97f, 1.0f, 0.9f}, {0.75f, 0.92f, 0.95f, 0.9f}, 0.01f},
        {railOffset() + 16.0f, railOffset() + 45.0f, {0.24f, 0.66f, 0.72f, 1.0f}, {0.08f, 0.36f, 0.55f, 1.0f}, 0.0f},
        {railOffset() + 45.0f, railOffset() + 700.0f, {0.08f, 0.36f, 0.55f, 1.0f}, {0.04f, 0.22f, 0.42f, 1.0f}, 0.0f},
    };
    for (const Band &b : bands) {
        glBegin(GL_QUAD_STRIP);
        for (float z = startZ; z >= endZ; z -= seg) {
            float c = roadCenter(z);
            glColor4fv(b.c0); glVertex3f(c + b.x0, b.y, z);
            glColor4fv(b.c1); glVertex3f(c + b.x1, b.y, z);
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
void GLWidget::drawObstacles(float camZ)
{
    for (const sim::Obstacle &o : simEngine.getObstacles()) {
        if (o.z > camZ + 30.0f || o.z < camZ - VIEW_DIST) continue;
        glPushMatrix();
        glTranslatef(o.x, o.y, o.z);
        glRotatef(o.rot, 0, 1, 0);

        if (o.type == sim::Obstacle::Cone) {
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

void GLWidget::drawNitroPickups(float camZ)
{
    float tickF = float(simEngine.getTick());
    for (const sim::NitroPickup &p : simEngine.getNitroPickups()) {
        if (!p.active) continue;
        if (p.z > camZ + 30.0f || p.z < camZ - VIEW_DIST) continue;

        glPushMatrix();
        float hoverY = 0.55f + 0.12f * std::sin(tickF * 0.08f + p.z * 0.1f);
        glTranslatef(p.x, hoverY, p.z);
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
void GLWidget::drawShadow(float x, float z, float heading, float hw, float hl)
{
    const int N = 28;
    auto pt = [&](int i, float k) {
        float t = i * 2.0f * PI / N, c = std::cos(t), s = std::sin(t);
        float px = hw * k * std::copysign(std::sqrt(std::fabs(c)), c);
        float pz = hl * k * std::copysign(std::sqrt(std::fabs(s)), s);
        glVertex3f(px, 0, pz);
    };
    glPushMatrix();
    glTranslatef(x, 0.045f, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0, 0, 0, 0.55f);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= N; ++i) pt(i, 0.80f);
    glEnd();
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= N; ++i) {
        glColor4f(0, 0, 0, 0.55f); pt(i, 0.80f);
        glColor4f(0, 0, 0, 0.0f);  pt(i, 1.25f);
    }
    glEnd();
    glPopMatrix();
}

void GLWidget::drawCar(float x, float z, float heading, float roll, float pitch,
                       float steer, float spin, float r, float g, float b, bool nitro,
                       const char *badge, float brake)
{
    using namespace carmodel;
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);

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

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, glassSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 90.0f);
    glColor3f(0.05f, 0.06f, 0.08f);
    glCallList(carGlassList);
    reflectionPass(carGlassList, texEnv, 0.38f);

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

    // Overhead floating vehicle badge (e.g. [ 1 ], [ CPU ], [ 2 ])
    if (badge && *badge) {
        float bLen = float(std::strlen(badge));
        float hw = std::max(0.35f, bLen * 0.16f + 0.12f);
        float hh = 0.24f;
        float by = 2.25f;

        // Dark background plate
        glColor4f(0.05f, 0.07f, 0.12f, 0.92f);
        boxAt(0, by, 0, hw, hh, 0.04f);

        // Glowing border (Orange for CPU, Cyan for Player)
        bool isCpu = (std::strcmp(badge, "CPU") == 0);
        if (isCpu) glColor3f(1.0f, 0.45f, 0.20f);
        else       glColor3f(0.20f, 0.90f, 1.0f);
        float th = 0.02f;
        boxAt(0, by + hh, 0.045f, hw, th, 0.005f);
        boxAt(0, by - hh, 0.045f, hw, th, 0.005f);
        boxAt(-hw, by, 0.045f, th, hh, 0.005f);
        boxAt( hw, by, 0.045f, th, hh, 0.005f);

        // Double-sided text
        glColor3f(1.0f, 1.0f, 1.0f);
        drawText3D(badge, 0, by - 0.10f, 0.055f, 0.038f);
        glPushMatrix();
        glRotatef(180.0f, 0, 1, 0);
        drawText3D(badge, 0, by - 0.10f, 0.055f, 0.038f);
        glPopMatrix();

        // Small indicator triangle beneath the badge pointing down towards roof
        glBegin(GL_TRIANGLES);
        glVertex3f(-0.08f, by - hh, 0.045f);
        glVertex3f( 0.08f, by - hh, 0.045f);
        glVertex3f( 0.00f, by - hh - 0.12f, 0.045f);
        glEnd();
    }
    glEnable(GL_LIGHTING);

    glPopMatrix();
}

void GLWidget::drawTruck(float x, float z, float heading, float spin, float r, float g, float b)
{
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);

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
    static const char *ORD[8] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH"};
    const auto &hudCars = simEngine.getCars();
    const int myIdx = (&me >= hudCars.data() && &me < hudCars.data() + hudCars.size()) ? int(&me - hudCars.data()) : 0;
    const char *posStr = ORD[std::clamp(simEngine.rankOf(myIdx), 1, 8) - 1];
    char pTagBuf[8];
    snprintf(pTagBuf, sizeof pTagBuf, "P%d", playerNum);
    const char *pTag = pTagBuf;

    float prog = std::clamp((0.0f - me.z) / sim::TRACK_LEN, 0.0f, 1.0f);
    int lapNum = std::min(3, 1 + int(prog * 2.99f));
    int remDist = int(std::max(0.0f, sim::TRACK_LEN + me.z));

    char statusBuf[64];
    snprintf(statusBuf, sizeof statusBuf, "%s %s  •  LAP %d/3  •  %dM", pTag, posStr, lapNum, remDist);

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

    // Warnings / messages (Centered)
    bool offroad = std::fabs(me.x - roadCenter(me.z)) > roadHalf();
    if (offroad && simEngine.getState() == State::Racing && (simEngine.getTick() / 15) % 2) {
        float s = ps * 1.6f;
        drawText("OFF ROAD", (vw - textWidth("OFF ROAD", s)) * 0.5f, vh * 0.30f, s, 1.0f, 0.55f, 0.1f);
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
    } else if (simEngine.getWinner() != 0) {
        int winner = simEngine.getWinner();
        float s = ps * 3.5f;
        if (winner == playerNum) {
            const char *winMsg = "YOU WIN!";
            drawText(winMsg, (vw - textWidth(winMsg, s)) * 0.5f, vh * 0.54f, s, 0.3f, 1.0f, 0.4f);
        } else if (me.finished) {
            const char *finMsg = "FINISHED!";
            drawText(finMsg, (vw - textWidth(finMsg, s)) * 0.5f, vh * 0.54f, s, 1.0f, 0.85f, 0.2f);
        } else {
            QByteArray online = (winner - 1 < racerNames.size() ? racerNames[winner - 1] : QString("P%1").arg(winner)).toUtf8() + " WINS!";
            const char *loseMsg = isOnline() ? online.constData()
                                : (simEngine.getGameMode() == sim::GameSim::ModeSingle) ? "CPU WINS!" : "OPPONENT WINS!";
            drawText(loseMsg, (vw - textWidth(loseMsg, s)) * 0.5f, vh * 0.54f, s, 1.0f, 0.35f, 0.35f);
        }
        const char *rstMsg = isOnline() ? "WAITING FOR THE OTHERS..." : "PRESS [R] TO RESTART";
        float rstPs = ps * 1.10f;
        drawText(rstMsg, (vw - textWidth(rstMsg, rstPs)) * 0.5f, vh * 0.44f, rstPs, 0.9f, 0.9f, 0.9f);
    }
}

void GLWidget::drawMinimap(int W, int H, float dpr)
{
    const float mw = 125.0f * dpr;
    const float mh = std::min(250.0f * dpr, H * 0.44f);
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

    const float pad = 12.0f * dpr;
    const float top = y0 + mh - 7 * ps - 14 * dpr, bottom = y0 + pad;
    const float kx = (mw - 2 * pad) / 80.0f;
    const float ky = (top - bottom) / (sim::TRACK_LEN + 120.0f);
    auto mx = [&](float wx) { return x0 + mw * 0.5f + wx * kx; };
    auto my = [&](float wz) { return std::clamp(bottom + (-wz) * ky, bottom, top); };

    float rw = std::max(2.5f * dpr, roadHalf() * kx);
    glColor4f(0.60f, 0.60f, 0.66f, 1.0f);
    glBegin(GL_QUAD_STRIP);
    for (float d = 0; d <= sim::TRACK_LEN + 40.0f; d += 20.0f) {
        float c = roadCenter(-d);
        glVertex2f(mx(c) - rw, my(-d)); glVertex2f(mx(c) + rw, my(-d));
    }
    glEnd();

    glColor4f(1, 1, 1, 1);
    rect(mx(0) - rw - 2 * dpr, my(0) - dpr, mx(0) + rw + 2 * dpr, my(0) + dpr);

    float fy = my(-sim::TRACK_LEN), sq = 2.0f * dpr;
    for (int i = 0; i < 6; ++i)
        for (int row = 0; row < 2; ++row) {
            if ((i + row) % 2) glColor4f(0, 0, 0, 1); else glColor4f(1, 1, 1, 1);
            float xx = mx(0) - 3 * sq + i * sq, yy = fy + row * sq;
            rect(xx, yy, xx + sq, yy + sq);
        }

    float ty = my(-sim::TRACK_LEN - 110.0f);
    glColor4f(0.95f, 0.78f, 0.30f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(mx(0) - 8 * dpr, ty - 5 * dpr); glVertex2f(mx(0) + 8 * dpr, ty - 5 * dpr);
    glVertex2f(mx(0), ty + 5 * dpr);
    glEnd();

    glColor4f(0.85f, 0.85f, 0.85f, 0.75f);
    for (const sim::Traffic &t : simEngine.getTraffic()) {
        if (t.parked) continue;
        float px = mx(roadCenter(t.z) + t.lane), py = my(t.z), s = 1.5f * dpr;
        rect(px - s, py - s, px + s, py + s);
    }

    auto dot = [&](const sim::Car &c, const char *lbl) {
        float px = mx(c.x), py = my(c.z), s = 4.0f * dpr, o = 1.5f * dpr;
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
        if (simEngine.getGameMode() == sim::GameSim::ModeSingle && i == 1) snprintf(lbl, sizeof lbl, "CPU");
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
    drawText("USER: RACER_JAX", ptx, pCardY + pCardH - 14.0f * dpr, pPs * 1.02f, 1.0f, 1.0f, 1.0f);
    drawText("RANK: PLATINUM II (Lv. 64) , 12,450 XP", ptx, pCardY + pCardH - 27.0f * dpr, pPs * 0.84f, 1.0f, 0.82f, 0.25f);

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
    drawText("SERVER: EUROPE 1 (ONLINE)", ptx + 11.0f * dpr, pCardY + 8.0f * dpr, pPs * 0.82f, 0.35f, 1.0f, 0.55f);

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
                      menuSelection == 3 ? "  < >" : "");
        drawText(carLine, panelX + 16.0f * dpr, headerTop - 25.0f * dpr, ps * 0.80f, 1.0f, 0.65f, 0.15f);
    }

    // Thin divider line
    glColor4f(0.20f, 0.45f, 0.70f, 0.45f);
    rect(panelX + 16.0f * dpr, listTop + 6.0f * dpr, panelX + panelW - 16.0f * dpr, listTop + 7.5f * dpr);

    // 4. THE 7 MENU PILL BUTTONS
    float textPs = ps * 0.95f;
    for (int i = 0; i < NUM_MENU_ITEMS; ++i) {
        float by0 = listTop - (i + 1) * btnH - i * btnGap;
        float by1 = by0 + btnH;
        float bx0 = btnX;
        float bx1 = btnX + btnW;
        bool sel = (menuSelection == i);
        const MenuItem &item = MENU_ITEMS[i];

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
        drawText(item.title, bx0 + 36.0f * dpr, textY, textPs,
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


    // 6b. MAP + CPU CARD: which map, what it looks like, how hard the CPU is
    {
        static const float ACCENT[5][3] = {{1.0f, 0.62f, 0.20f}, {1.0f, 0.78f, 0.30f}, {0.60f, 0.86f, 1.0f},
                                           {0.92f, 0.35f, 1.0f}, {0.20f, 0.86f, 1.0f}};
        const int m = selectedMapPreset;
        const sim::MapDef &md = sim::MAPS[m];
        const float *ac = ACCENT[int(md.theme)];
        const bool selMap = (menuSelection == 4), selCpu = (menuSelection == 6);
        float cx0 = specX0, cw = specW, ch = 150.0f * dpr;
        float cy0 = m2Y0 + m2H + 12.0f * dpr;
        float cps = ps * 0.80f;

        glColor4f(0.04f, 0.07f, 0.13f, 0.90f);
        drawRoundedRect(cx0, cy0, cx0 + cw, cy0 + ch, 12.0f * dpr);
        glColor4f(ac[0], ac[1], ac[2], selMap ? 0.95f * pulse : 0.60f);
        drawRoundedBorder(cx0, cy0, cx0 + cw, cy0 + ch, 12.0f * dpr, (selMap ? 2.0f : 1.2f) * dpr);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "MAP %d/%d", m + 1, sim::NUM_MAPS);
        drawText(buf, cx0 + 12.0f * dpr, cy0 + ch - 16.0f * dpr, cps * 0.95f, ac[0], ac[1], ac[2]);
        if (selMap && (simEngine.getTick() / 20) % 2 == 0) {
            const char *hint = "< LEFT / RIGHT >";
            drawText(hint, cx0 + cw - 12.0f * dpr - textWidth(hint, cps * 0.8f), cy0 + ch - 16.0f * dpr, cps * 0.8f, 1.0f, 1.0f, 1.0f);
        }
        drawText(md.name, cx0 + 12.0f * dpr, cy0 + ch - 36.0f * dpr, cps * 1.25f, 1.0f, 1.0f, 1.0f);
        drawText(md.tagline, cx0 + 12.0f * dpr, cy0 + ch - 50.0f * dpr, cps * 0.66f, 0.62f, 0.72f, 0.85f);

        // track preview: the road's centre line from start (left) to finish (right)
        float px0 = cx0 + 14.0f * dpr, px1 = cx0 + cw - 14.0f * dpr, pyc = cy0 + 52.0f * dpr;
        float k = 18.0f * dpr / 34.0f;
        glColor4f(0.20f, 0.28f, 0.40f, 0.5f);
        rect(px0, pyc - 0.5f * dpr, px1, pyc + 0.5f * dpr);
        glLineWidth(2.0f * dpr);
        glColor4f(ac[0], ac[1], ac[2], 1.0f);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= 160; ++i) {
            float d = sim::TRACK_LEN * float(i) / 160.0f;
            glVertex2f(px0 + (px1 - px0) * float(i) / 160.0f, pyc + simEngine.roadCenter(-d) * k);
        }
        glEnd();
        glLineWidth(1.0f);
        glColor4f(0.3f, 1.0f, 0.5f, 1.0f);
        rect(px0 - 2.5f * dpr, pyc - 2.5f * dpr, px0 + 2.5f * dpr, pyc + 2.5f * dpr);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        rect(px1 - 2.5f * dpr, pyc - 2.5f * dpr, px1 + 2.5f * dpr, pyc + 2.5f * dpr);

        // difficulty pips + CPU level
        drawText("DIFFICULTY", cx0 + 12.0f * dpr, cy0 + 12.0f * dpr, cps * 0.75f, 0.70f, 0.78f, 0.90f);
        float dx = cx0 + 12.0f * dpr + textWidth("DIFFICULTY", cps * 0.75f) + 8.0f * dpr;
        for (int i = 0; i < 3; ++i) {
            if (i < md.stars) glColor4f(ac[0], ac[1], ac[2], 1.0f);
            else              glColor4f(0.25f, 0.30f, 0.40f, 1.0f);
            rect(dx + i * 10.0f * dpr, cy0 + 12.0f * dpr, dx + i * 10.0f * dpr + 7.0f * dpr, cy0 + 19.0f * dpr);
        }
        static const char *CPU_NAMES[3] = {"CPU: EASY", "CPU: MEDIUM", "CPU: HARD"};
        static const float CPU_COL[3][3] = {{0.35f, 1.0f, 0.5f}, {1.0f, 0.82f, 0.25f}, {1.0f, 0.35f, 0.30f}};
        int dl = std::clamp(simEngine.getAiDifficulty(), 0, 2);
        const char *cpuTxt = CPU_NAMES[dl];
        float cpuPs = cps * (selCpu ? 0.95f : 0.80f);
        drawText(cpuTxt, cx0 + cw - 12.0f * dpr - textWidth(cpuTxt, cpuPs), cy0 + 12.0f * dpr, cpuPs,
                 CPU_COL[dl][0], CPU_COL[dl][1], CPU_COL[dl][2]);
    }

    // 7. BOTTOM NAVIGATION BAR / HINTS
    float footY = 14.0f * dpr;
    const char *footHints = "[UP/DOWN] NAVIGATE   [LEFT/RIGHT] CHANGE CAR / MAP / CPU   [ENTER] SELECT";
    float footPs = ps * 0.75f;
    drawText(footHints, 28.0f * dpr, footY, footPs, 0.65f, 0.78f, 0.90f);

    if (menuSelection <= 1 && ((simEngine.getTick() / 25) % 2 == 0)) {
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

    const float ps = std::max(2.5f, H / 180.0f * dpr);
    const float cx = W * 0.5f, cy = H * 0.5f;

    float titlePs = ps * 2.5f;
    const char *title = "GAME PAUSED";
    drawText(title, cx - textWidth(title, titlePs) * 0.5f, cy + ps * 25, titlePs, 1.0f, 0.85f, 0.2f);

    float itemPs = ps * 1.4f;
    const char *o1 = "[ESC] / [P]  RESUME RACE";
    drawText(o1, cx - textWidth(o1, itemPs) * 0.5f, cy + ps * 8, itemPs, 1.0f, 1.0f, 1.0f);

    const char *o2 = "[R]          RESTART RACE";
    drawText(o2, cx - textWidth(o2, itemPs) * 0.5f, cy - ps * 2, itemPs, 1.0f, 1.0f, 1.0f);

    const char *o3 = "[M]          MAIN MENU";
    drawText(o3, cx - textWidth(o3, itemPs) * 0.5f, cy - ps * 12, itemPs, 0.9f, 0.7f, 0.7f);
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
    static sim::Car dummyCar;
    const sim::Car &c1 = (cars.size() > 0) ? cars[0] : dummyCar;
    const sim::Car &c2 = (cars.size() > 1) ? cars[1] : dummyCar;
    int winner = simEngine.getWinner();
    bool isSingle = (simEngine.getGameMode() == sim::GameSim::ModeSingle);

    // Data for Winner and Runner-up
    const sim::Car &cWin = (winner == 1) ? c1 : c2;
    const sim::Car &cLoss = (winner == 1) ? c2 : c1;
    const char *nameWin = (winner == 1) ? (isSingle ? "PLAYER 1 (YOU)" : "PLAYER 1") : (isSingle ? "CPU BOT" : "PLAYER 2");
    const char *nameLoss = (winner == 1) ? (isSingle ? "CPU BOT" : "PLAYER 2") : (isSingle ? "PLAYER 1 (YOU)" : "PLAYER 1");

    int winM = int(cWin.finishTime) / 60;
    float winS = cWin.finishTime - winM * 60;
    int topSpeedWin = int(cWin.topSpeed / sim::MAX_SPEED * 240.0f);
    int topSpeedLoss = int(cLoss.topSpeed / sim::MAX_SPEED * 240.0f);

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
    if (isSingle) {
        if (winner == 1) snprintf(upBuf, sizeof upBuf, "🏁  1ST PLACE: PLAYER 1 (YOU) - WINNER!  🏁");
        else             snprintf(upBuf, sizeof upBuf, "🏁  1ST PLACE: CPU BOT - CHAMPION!  🏁");
    } else {
        snprintf(upBuf, sizeof upBuf, "🏁  1ST PLACE: PLAYER %d - CHAMPION!  🏁", winner);
    }
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
    snprintf(timeTxt, sizeof timeTxt, "TIME %02d:%04.1f", winM, winS);
    snprintf(lapTxt,  sizeof lapTxt,  "LAP 3 / 3");
    snprintf(posTxt,  sizeof posTxt,  winner == 1 ? "POSITION 1ST" : "POSITION 2ND");
    snprintf(spdTxt,  sizeof spdTxt,  "TOP %d KM/H", topSpeedWin);

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
    // 6. DETAILED LEADERBOARD BREAKDOWN (Clean, Generous Spacing)
    // =======================================================================
    const float lb_h = 76.0f * s;
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

    // Table Header Bar
    float colPs = 1.05f * s;
    glColor4f(0.12f, 0.16f, 0.24f, 0.95f);
    rect(bx0 + 2.0f * s, lb_y1 - 18.0f * s, bx1 - 2.0f * s, lb_y1 - 2.0f * s);
    drawText("RANK   RACER                 TIME         TOP SPEED   STATUS",
             bx0 + 12.0f * s, lb_y1 - 14.5f * s, colPs, 0.65f, 0.78f, 0.90f);

    // Row 1: Winner (Gold highlight card)
    float r1_y0 = lb_y1 - 44.0f * s;
    float r1_y1 = lb_y1 - 21.0f * s;
    glColor4f(0.20f, 0.17f, 0.05f, 0.75f);
    rect(bx0 + 4.0f * s, r1_y0, bx1 - 4.0f * s, r1_y1);
    // Gold indicator bar on left
    glColor4f(1.0f, 0.85f, 0.20f, 1.0f);
    rect(bx0 + 4.0f * s, r1_y0, bx0 + 9.0f * s, r1_y1);
    char r1Buf[96];
    snprintf(r1Buf, sizeof r1Buf, " 1ST   %-18s %02d:%04.1f      %3d KM/H    WINNER", nameWin, winM, winS, topSpeedWin);
    drawText(r1Buf, bx0 + 14.0f * s, r1_y0 + 6.0f * s, colPs, 1.0f, 0.92f, 0.45f);

    // Row 2: Runner-up (Silver highlight card)
    float r2_y0 = lb_y0 + 5.0f * s;
    float r2_y1 = r1_y0 - 4.0f * s;
    glColor4f(0.09f, 0.13f, 0.20f, 0.75f);
    rect(bx0 + 4.0f * s, r2_y0, bx1 - 4.0f * s, r2_y1);
    // Silver indicator bar on left
    glColor4f(0.60f, 0.75f, 0.90f, 1.0f);
    rect(bx0 + 4.0f * s, r2_y0, bx0 + 9.0f * s, r2_y1);
    char r2Buf[96];
    if (cLoss.finished) {
        float diff = std::max(0.0f, cLoss.finishTime - cWin.finishTime);
        snprintf(r2Buf, sizeof r2Buf, " 2ND   %-18s +%-4.1f SEC     %3d KM/H    RUNNER-UP", nameLoss, diff, topSpeedLoss);
    } else {
        float remM = std::max(0.0f, sim::TRACK_LEN + cLoss.z);
        snprintf(r2Buf, sizeof r2Buf, " 2ND   %-18s DNF (+%3dM)    %3d KM/H    RUNNER-UP", nameLoss, int(remM), topSpeedLoss);
    }
    drawText(r2Buf, bx0 + 14.0f * s, r2_y0 + 6.0f * s, colPs, 0.75f, 0.85f, 0.95f);

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
