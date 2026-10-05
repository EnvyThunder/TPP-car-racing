#include "glwidget.h"
#include "pixelfont.h"

#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#include <QFocusEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ===========================================================================
//  CONSTANTS
// ===========================================================================
namespace {

const float PI          = 3.14159265f;
const float ROAD_HALF   = 7.0f;               // road is 14 units wide (3 lanes)
const float RAIL_OFFSET = ROAD_HALF + 4.0f;   // guard rails
const float TRACK_LEN   = 4000.0f;            // finish line distance
const float MAX_SPEED   = 1.10f;
const float NITRO_SPEED = 1.65f;
const float OFFROAD_MAX = 0.45f;
const float VIEW_DIST   = 340.0f;             // draw distance (fog end)
const float TEMPLE_DIST = 400.0f;             // temple "backdrop" distance
const float LANES[3]    = {-4.67f, 0.0f, 4.67f};

// Sunset palette
const float SKY_TOP[3] = {0.20f, 0.22f, 0.45f};
const float SKY_MID[3] = {0.86f, 0.52f, 0.58f};
const float SKY_HOR[3] = {0.99f, 0.70f, 0.47f};
const float FOG_COL[4] = {0.94f, 0.72f, 0.55f, 1.0f};

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

} // namespace

// ===========================================================================
//  CONSTRUCTION
// ===========================================================================
GLWidget::GLWidget(QWidget *parent)
    : QOpenGLWidget(parent), rng(std::random_device{}())
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(800, 450);
    resetRace();

    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &GLWidget::updateGame);
    timer->start(16); // ~60 ticks per second
}

GLWidget::~GLWidget()
{
    if (quad) {
        makeCurrent();
        gluDeleteQuadric(quad);
        GLuint t[] = {texGrass, texAsphalt, texStone, texPlaster, texRoof};
        glDeleteTextures(5, t);
        doneCurrent();
    }
}

float GLWidget::rnd(float a, float b)
{
    return std::uniform_real_distribution<float>(a, b)(rng);
}

// Zig-zag track: sum of sines, straight at the start and before the temple.
float GLWidget::roadCenter(float z) const
{
    float d = -z;
    float ramp    = std::clamp((d - 80.0f) / 200.0f, 0.0f, 1.0f);
    float endRamp = std::clamp((TRACK_LEN + 60.0f - d) / 200.0f, 0.0f, 1.0f);
    return ramp * endRamp * (18.0f * std::sin(d * 0.012f) + 8.0f * std::sin(d * 0.031f + 1.3f));
}

float GLWidget::roadAngle(float z) const
{
    return std::atan2(roadCenter(z - 1.0f) - roadCenter(z), 1.0f);
}

// ===========================================================================
//  WORLD GENERATION
// ===========================================================================
void GLWidget::resetRace()
{
    p1 = Car(); p1.x =  3.0f; p1.r = 0.92f; p1.g = 0.14f; p1.b = 0.16f;  // red
    p2 = Car(); p2.x = -3.0f; p2.r = 0.15f; p2.g = 0.42f; p2.b = 0.98f;  // blue
    state = Countdown; tick = 0; raceTick = 0; winner = 0;
    traffic.clear(); obstacles.clear(); props.clear();

    std::mt19937 gen(2024); // fixed seed -> same track layout every race
    auto R = [&gen](float a, float b) { return std::uniform_real_distribution<float>(a, b)(gen); };

    auto addRound = [&](Obstacle::Type t, float d, float off) {
        Obstacle o{}; o.type = t; o.z = -d; o.x = roadCenter(o.z) + off;
        obstacles.push_back(o);
    };
    auto addBarrier = [&](float d, float off) {
        Obstacle o{}; o.type = Obstacle::Barrier; o.z = -d; o.x = roadCenter(o.z) + off;
        o.hx = 2.2f; o.hz = 0.6f;
        obstacles.push_back(o);
    };

    // ---- Obstacle patterns ----
    for (float d = 240.0f; d < TRACK_LEN - 220.0f; d += R(95.0f, 130.0f)) {
        int pattern = int(R(0.0f, 4.0f)) % 4;
        if (pattern == 0) {                         // cone slalom
            for (int i = 0; i < 6; ++i) addRound(Obstacle::Cone, d + i * 11.0f, (i % 2) ? 3.5f : -3.5f);
        } else if (pattern == 1) {                  // zig-zag barrier walls
            int open1 = int(R(0, 3)) % 3;
            int open2 = (open1 + 1 + int(R(0, 2)) % 2) % 3;
            for (int l = 0; l < 3; ++l) if (l != open1) addBarrier(d, LANES[l]);
            for (int l = 0; l < 3; ++l) if (l != open2) addBarrier(d + 38.0f, LANES[l]);
        } else if (pattern == 2) {                  // barrels
            for (int i = 0; i < 3; ++i) addRound(Obstacle::Barrel, d + R(0, 18), R(-5.5f, 5.5f));
        } else {                                    // cone wall with one gap
            int gap = int(R(0, 3)) % 3;
            for (float x = -6.0f; x <= 6.01f; x += 1.5f)
                if (std::fabs(x - LANES[gap]) > 2.4f) addRound(Obstacle::Cone, d, x);
        }
    }

    // ---- Scenery: houses + forest ----
    float lastHouse[2] = {-100.0f, -100.0f};
    for (float d = 20.0f; d < TRACK_LEN + 40.0f; d += R(12.0f, 20.0f)) {
        for (int s = 0; s < 2; ++s) {
            float side = (s == 0) ? -1.0f : 1.0f;
            if (R(0, 1) < 0.85f) {
                Prop p{}; p.z = -d + R(-3, 3); p.scale = R(0.85f, 1.2f); p.palette = int(R(0, 4)) % 4;
                float r = R(0, 1);
                if (r < 0.4f && d - lastHouse[s] > 16.0f && d < TRACK_LEN - 220.0f) {
                    p.type = Prop::House; lastHouse[s] = d;
                    p.x = roadCenter(p.z) + side * (RAIL_OFFSET + 8.0f + R(0, 6));
                    p.rot = (side < 0) ? 90.0f : -90.0f;   // face the road
                } else {
                    p.type = (r < 0.72f) ? Prop::Pine : Prop::RoundTree;
                    float off = (d - lastHouse[s] < 10.0f) ? R(20, 30) : R(3, 25);
                    p.x = roadCenter(p.z) + side * (RAIL_OFFSET + off);
                    p.rot = R(0, 360);
                }
                props.push_back(p);
            }
            Prop b{}; // background forest row
            b.type = (R(0, 1) < 0.6f) ? Prop::Pine : Prop::RoundTree;
            b.z = -d + R(-6, 6);
            b.x = roadCenter(b.z) + side * (RAIL_OFFSET + 32.0f + R(0, 45));
            b.scale = R(1.0f, 1.6f); b.rot = R(0, 360);
            props.push_back(b);
        }
    }
    // street lamps
    int alt = 0;
    for (float d = 30.0f; d < TRACK_LEN - 220.0f; d += 50.0f, ++alt) {
        float side = (alt % 2) ? 1.0f : -1.0f;
        Prop p{}; p.type = Prop::Lamp; p.z = -d; p.scale = 1.0f;
        p.x = roadCenter(p.z) + side * (ROAD_HALF + 2.2f);
        p.rot = (side < 0) ? 90.0f : -90.0f;
        props.push_back(p);
    }
    // fire & water torches leading to the temple
    for (float d = TRACK_LEN - 220.0f; d < TRACK_LEN + 30.0f; d += 20.0f) {
        for (int s = 0; s < 2; ++s) {
            float side = (s == 0) ? -1.0f : 1.0f;
            Prop p{}; p.type = Prop::Torch; p.z = -d; p.scale = 1.0f; p.palette = s; p.rot = 0;
            p.x = roadCenter(p.z) + side * (ROAD_HALF + 2.0f);
            props.push_back(p);
        }
    }

    // ---- Traffic ----
    const float pal[6][3] = {{0.95f, 0.95f, 0.95f}, {0.65f, 0.67f, 0.70f}, {0.20f, 0.65f, 0.30f},
                             {1.00f, 0.55f, 0.10f}, {0.55f, 0.30f, 0.75f}, {0.98f, 0.85f, 0.15f}};
    for (int i = 0; i < 16; ++i) {
        Traffic t{};
        t.z = -(180.0f + i * 230.0f + R(0, 80));
        t.lane = t.targetLane = LANES[int(R(0, 3)) % 3];
        t.speed = R(0.35f, 0.62f);
        int k = int(R(0, 6)) % 6;
        t.r = pal[k][0]; t.g = pal[k][1]; t.b = pal[k][2];
        t.truck = R(0, 1) < 0.25f;
        t.parked = false;
        traffic.push_back(t);
    }
}

// ===========================================================================
//  GAME LOOP
// ===========================================================================
void GLWidget::updateGame()
{
    ++tick;
    if (state == Countdown && tick >= 180) { state = Racing; raceTick = 0; }
    if (state == Racing) ++raceTick;

    // P1 = arrows (right screen), P2 = WASD (left screen)
    updateCar(p1, isDown(Qt::Key_Up), isDown(Qt::Key_Down), isDown(Qt::Key_Left), isDown(Qt::Key_Right),
              isDown(Qt::Key_Return) || isDown(Qt::Key_Enter) || isDown(Qt::Key_Slash));
    updateCar(p2, isDown(Qt::Key_W), isDown(Qt::Key_S), isDown(Qt::Key_A), isDown(Qt::Key_D),
              isDown(Qt::Key_Space));

    // ---- traffic AI ----
    float lead = std::min(p1.z, p2.z), back = std::max(p1.z, p2.z);
    for (Traffic &t : traffic) {
        if (t.parked) continue;
        t.z -= t.speed;

        // change lane if a barrier is coming up in our lane
        for (const Obstacle &o : obstacles) {
            if (o.type != Obstacle::Barrier || o.z > t.z || o.z < t.z - 25.0f) continue;
            float c = roadCenter(o.z);
            if (std::fabs(c + t.targetLane - o.x) < o.hx + 1.2f) {
                for (int l = 0; l < 3; ++l) {
                    bool blocked = false;
                    for (const Obstacle &o2 : obstacles)
                        if (o2.type == Obstacle::Barrier && std::fabs(o2.z - o.z) < 1.0f &&
                            std::fabs(c + LANES[l] - o2.x) < o2.hx + 1.2f) { blocked = true; break; }
                    if (!blocked) { t.targetLane = LANES[l]; break; }
                }
            }
        }
        t.lane += (t.targetLane - t.lane) * 0.06f;

        float tx = roadCenter(t.z) + t.lane, ta = roadAngle(t.z);
        for (Obstacle &o : obstacles)
            if (std::fabs(o.z - t.z) < 6.0f) knock(o, tx, t.z, std::sin(ta), -std::cos(ta), t.speed);

        // recycle traffic that fell behind
        if (t.z > back + 40.0f || t.z < -TRACK_LEN + 40.0f) {
            float nz = lead - 260.0f - rnd(0.0f, 160.0f);
            if (nz < -TRACK_LEN + 80.0f) { t.parked = true; continue; }
            t.z = nz;
            t.lane = t.targetLane = LANES[int(rnd(0, 3)) % 3];
            t.speed = rnd(0.35f, 0.62f);
        }
    }

    // ---- knocked-over cones / barrels physics ----
    for (Obstacle &o : obstacles) {
        if (!o.knocked) continue;
        if (o.y <= 0.0f && std::fabs(o.vy) < 0.01f && std::fabs(o.vx) + std::fabs(o.vz) < 0.01f) continue;
        o.x += o.vx; o.y += o.vy; o.z += o.vz;
        o.vy -= 0.025f;
        o.rot += o.spin;
        if (o.y < 0.0f) { o.y = 0.0f; o.vy = -o.vy * 0.35f; o.vx *= 0.6f; o.vz *= 0.6f; o.spin *= 0.6f; }
    }

    collideWorld(p1);
    collideWorld(p2);
    collideCars(p1, p2);

    // ---- finish line ----
    if (state == Racing) {
        if (!p1.finished && p1.z <= -TRACK_LEN) { p1.finished = true; if (!winner) winner = 1; }
        if (!p2.finished && p2.z <= -TRACK_LEN) { p2.finished = true; if (!winner) winner = 2; }
        if (winner) state = Finished;
    }

    update(); // schedule paintGL
}

void GLWidget::updateCar(Car &c, bool up, bool down, bool left, bool right, bool nitroKey)
{
    if (state != Racing) { up = down = nitroKey = false; }
    if (state == Countdown) { left = right = false; }

    float offset = c.x - roadCenter(c.z);
    bool offroad = std::fabs(offset) > ROAD_HALF;

    c.nitroOn = nitroKey && up && c.nitro > 0.01f && !offroad;
    float maxS = c.nitroOn ? NITRO_SPEED : MAX_SPEED;
    if (offroad) maxS = OFFROAD_MAX;

    if (c.nitroOn) { c.nitro = std::max(0.0f, c.nitro - 0.006f); c.speed += 0.03f; }
    else           { c.nitro = std::min(1.0f, c.nitro + 0.0010f); }

    if (up)        c.speed += 0.016f * (1.0f - 0.5f * c.speed / MAX_SPEED);
    else if (down) c.speed -= (c.speed > 0.0f) ? 0.035f : 0.008f;   // brake, then slow reverse
    else           c.speed *= (state == Finished ? 0.97f : 0.99f);  // coast

    if (c.speed > maxS) c.speed = std::max(maxS, c.speed - 0.025f);
    if (c.speed < -0.3f) c.speed = -0.3f;

    // steering (needs some speed, inverted in reverse)
    float steer = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    float grip  = std::clamp(std::fabs(c.speed) / 0.35f, 0.0f, 1.0f);
    c.heading  += steer * 0.030f * grip * (c.speed >= 0.0f ? 1.0f : -1.0f);
    c.steerVis += (steer * 0.45f - c.steerVis) * 0.25f;

    c.x += std::sin(c.heading) * c.speed;
    c.z -= std::cos(c.heading) * c.speed;

    c.wheelSpin  = std::fmod(c.wheelSpin + c.speed * 136.0f, 360.0f);
    c.camHeading += (c.heading - c.camHeading) * 0.10f;
    if (c.shake > 0.0f) c.shake = std::max(0.0f, c.shake - 0.05f);

    float targetFov = 64.0f + 12.0f * std::clamp(c.speed / MAX_SPEED, 0.0f, 1.5f) + (c.nitroOn ? 10.0f : 0.0f);
    c.fov += (targetFov - c.fov) * 0.08f;

    // guard rails
    offset = c.x - roadCenter(c.z);
    float lim = RAIL_OFFSET - 1.0f;
    if (std::fabs(offset) > lim) {
        c.x = roadCenter(c.z) + (offset > 0 ? lim : -lim);
        c.speed *= 0.85f;
        c.heading += (roadAngle(c.z) - c.heading) * 0.3f;
        c.shake = std::max(c.shake, 0.5f);
    }
}

bool GLWidget::knock(Obstacle &o, float x, float z, float fx, float fz, float speed)
{
    if (o.type == Obstacle::Barrier || o.knocked) return false;
    float r = (o.type == Obstacle::Cone ? 0.45f : 0.7f) + 1.0f;
    float dx = o.x - x, dz = o.z - z;
    if (dx * dx + dz * dz > r * r) return false;
    float d = std::sqrt(dx * dx + dz * dz) + 1e-4f;
    float power = std::max(std::fabs(speed), 0.2f);
    o.knocked = true;
    o.vx = dx / d * 0.25f + fx * power * 0.9f;
    o.vz = dz / d * 0.25f + fz * power * 0.9f;
    o.vy = 0.25f + power * 0.3f;
    o.spin = 12.0f + power * 20.0f;
    return true;
}

void GLWidget::collideWorld(Car &c)
{
    float fx = std::sin(c.heading), fz = -std::cos(c.heading);

    for (Obstacle &o : obstacles) {
        if (std::fabs(o.z - c.z) > 8.0f) continue;
        if (o.type == Obstacle::Barrier) {
            // car = two circles (front + back) vs. box
            for (float off : {1.3f, -1.3f}) {
                float px = c.x + fx * off, pz = c.z + fz * off;
                float qx = std::clamp(px, o.x - o.hx, o.x + o.hx);
                float qz = std::clamp(pz, o.z - o.hz, o.z + o.hz);
                float dx = px - qx, dz = pz - qz, d2 = dx * dx + dz * dz;
                const float RAD = 1.0f;
                if (d2 < RAD * RAD) {
                    float d = std::sqrt(d2), nx, nz, pen;
                    if (d < 1e-4f) { nx = 0; nz = 1; pen = RAD + (o.z + o.hz - pz); }
                    else           { nx = dx / d; nz = dz / d; pen = RAD - d; }
                    c.x += nx * pen; c.z += nz * pen;
                    if (c.speed > 0.15f) c.shake = 1.0f;
                    c.speed *= 0.35f;
                }
            }
        } else if (knock(o, c.x, c.z, fx, fz, c.speed)) {
            c.speed *= (o.type == Obstacle::Cone) ? 0.85f : 0.55f;
            c.shake = std::max(c.shake, (o.type == Obstacle::Cone) ? 0.35f : 0.8f);
        }
    }

    for (const Traffic &t : traffic) {
        if (t.parked) continue;
        float tx = roadCenter(t.z) + t.lane;
        float dx = c.x - tx, dz = c.z - t.z;
        const float halfW = 1.9f;
        float halfL = t.truck ? 5.6f : 4.2f;
        if (std::fabs(dx) < halfW && std::fabs(dz) < halfL) {
            float px = halfW - std::fabs(dx), pz = halfL - std::fabs(dz);
            if (px < pz)      { c.x += (dx > 0) ? px : -px; c.speed *= 0.93f; }
            else if (dz > 0)  { c.z += pz; c.speed = std::min(c.speed, t.speed * 0.5f); } // rear-ended it
            else              { c.z -= pz; }
            c.shake = std::max(c.shake, 0.8f);
        }
    }
}

void GLWidget::collideCars(Car &a, Car &b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    if (std::fabs(dx) < 1.9f && std::fabs(dz) < 4.2f) {
        float px = 1.9f - std::fabs(dx), pz = 4.2f - std::fabs(dz);
        if (px < pz) {
            float s = ((dx > 0) ? 1.0f : -1.0f) * px * 0.5f;
            a.x += s; b.x -= s;
        } else {
            float s = ((dz > 0) ? 1.0f : -1.0f) * pz * 0.5f;
            a.z += s; b.z -= s;
            float avg = (a.speed + b.speed) * 0.5f * 0.95f;
            a.speed = b.speed = avg;
        }
        a.shake = std::max(a.shake, 0.6f);
        b.shake = std::max(b.shake, 0.6f);
    }
}

// ===========================================================================
//  INPUT
// ===========================================================================
void GLWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;          // macOS sends repeats -> ignore
    keys.insert(event->key());
    if (event->key() == Qt::Key_R) resetRace();
    if (event->key() == Qt::Key_Escape) window()->close();
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

void GLWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
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
}

GLuint GLWidget::makeTexture(int w, int h, const std::vector<unsigned char> &rgb)
{
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);               // crisp pixels
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR); // no shimmer far away
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

    // --- grass with blades and tiny flowers ---
    {
        const int N = 64; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float n = hash2(x, y, 1), m = hash2(x, y, 2);
            float r = 78 + n * 30, g = 140 + n * 40, b = 62 + n * 20;
            if (m < 0.08f)       { r = 55;  g = 110; b = 45;  }
            else if (m > 0.992f) { r = 245; g = 225; b = 90;  }
            else if (m > 0.985f) { r = 240; g = 150; b = 190; }
            put(img, N, x, y, r, g, b);
        }
        texGrass = makeTexture(N, N, img);
    }
    // --- asphalt ---
    {
        const int N = 64; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float n = hash2(x, y, 3), m = hash2(x, y, 4);
            float g = 58 + n * 22;
            if (m < 0.04f) g = 105; else if (m > 0.985f) g = 35;
            put(img, N, x, y, g, g, g + 6);
        }
        texAsphalt = makeTexture(N, N, img);
    }
    // --- mossy temple stone bricks ---
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
    // --- plaster (tinted per house) ---
    {
        const int N = 32; std::vector<unsigned char> img(N * N * 3);
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float v = 228 + hash2(x, y, 8) * 27;
            if (hash2(x, y, 9) < 0.03f) v -= 40;
            put(img, N, x, y, v, v - 4, v - 12);
        }
        texPlaster = makeTexture(N, N, img);
    }
    // --- roof tiles (tinted per house) ---
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
}

void GLWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w); Q_UNUSED(h); // projection is set per split-screen view in paintGL
}

// ===========================================================================
//  FRAME
// ===========================================================================
void GLWidget::paintGL()
{
    const float dpr = float(devicePixelRatioF());   // Retina support
    const int W = int(width() * dpr), H = int(height() * dpr);

    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const int half = W / 2;
    renderView(p2, p1, 2, 0, 0, half, H);         // LEFT  = WASD player
    renderView(p1, p2, 1, half, 0, W - half, H);  // RIGHT = Arrow player

    // overlay: divider + minimap
    glViewport(0, 0, W, H);
    begin2D(W, H);
    glColor4f(0.05f, 0.05f, 0.08f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(half - 3 * dpr, 0); glVertex2f(half + 3 * dpr, 0);
    glVertex2f(half + 3 * dpr, H); glVertex2f(half - 3 * dpr, H);
    glEnd();
    drawMinimap(W, H, dpr);
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

void GLWidget::renderView(const Car &me, const Car &other, int playerNum, int vx, int vy, int vw, int vh)
{
    glViewport(vx, vy, vw, vh);
    drawSky(vw, vh);

    // ---- chase camera ----
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(me.fov, double(vw) / double(std::max(vh, 1)), 0.5, 800.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    float fx = std::sin(me.camHeading), fz = -std::cos(me.camHeading);
    float jx = std::sin(tick * 1.9f) * me.shake * 0.25f;
    float jy = std::cos(tick * 2.3f) * me.shake * 0.20f;
    float ex = me.x - fx * 8.5f + jx, ey = 3.4f + jy, ez = me.z - fz * 8.5f;
    gluLookAt(ex, ey, ez, me.x + fx * 6.0f, 1.0f, me.z + fz * 6.0f, 0, 1, 0);

    GLfloat lp[] = {0.45f, 1.0f, 0.65f, 0.0f};  // directional sunlight
    glLightfv(GL_LIGHT0, GL_POSITION, lp);

    glEnable(GL_DEPTH_TEST);

    // ---- far backdrop (no fog) ----
    glDisable(GL_LIGHTING); glDisable(GL_FOG);
    drawSunMountainsClouds(ex, ez);
    glEnable(GL_LIGHTING);
    drawTemple(ex, ez);

    // ---- world ----
    glEnable(GL_FOG);
    drawGround(ex, ez);
    drawRoad(me.z);
    drawArch(-6.0f, false);
    drawArch(-TRACK_LEN, true);
    drawProps(me.z);
    drawObstacles(me.z);

    // shadows
    glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
    for (const Traffic &t : traffic) {
        if (t.parked || t.z > me.z + 25.0f || t.z < me.z - VIEW_DIST) continue;
        drawShadow(roadCenter(t.z) + t.lane, t.z, roadAngle(t.z), t.truck ? 1.25f : 1.1f, t.truck ? 3.6f : 2.3f);
    }
    drawShadow(p1.x, p1.z, p1.heading, 1.1f, 2.3f);
    drawShadow(p2.x, p2.z, p2.heading, 1.1f, 2.3f);
    glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);

    // traffic
    for (const Traffic &t : traffic) {
        if (t.parked || t.z > me.z + 25.0f || t.z < me.z - VIEW_DIST) continue;
        float tx = roadCenter(t.z) + t.lane, ta = roadAngle(t.z);
        float spin = std::fmod(float(tick) * t.speed * 136.0f, 360.0f);
        if (t.truck) drawTruck(tx, t.z, ta, spin, t.r, t.g, t.b);
        else         drawCar(tx, t.z, ta, 0.0f, spin, t.r, t.g, t.b, false);
    }
    // both players are visible in both screens
    drawCar(p1.x, p1.z, p1.heading, p1.steerVis, p1.wheelSpin, p1.r, p1.g, p1.b, p1.nitroOn);
    drawCar(p2.x, p2.z, p2.heading, p2.steerVis, p2.wheelSpin, p2.r, p2.g, p2.b, p2.nitroOn);

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
    glColor3fv(SKY_HOR); glVertex2f(0, 0);  glVertex2f(vw, 0);  glVertex2f(vw, h1); glVertex2f(0, h1);
    glColor3fv(SKY_HOR); glVertex2f(0, h1); glVertex2f(vw, h1);
    glColor3fv(SKY_MID); glVertex2f(vw, h2); glVertex2f(0, h2);
    glColor3fv(SKY_MID); glVertex2f(0, h2); glVertex2f(vw, h2);
    glColor3fv(SKY_TOP); glVertex2f(vw, vh); glVertex2f(0, vh);
    glEnd();
}

void GLWidget::drawSunMountainsClouds(float ex, float ez)
{
    // ---- pixel sun (blocky disc, Jump-King style) ----
    glDepthMask(GL_FALSE);
    float dx = 0.28f, dy = 0.16f, dz = -1.0f;
    float L = std::sqrt(dx * dx + dy * dy + dz * dz); dx /= L; dy /= L; dz /= L;
    float cx = ex + dx * 640.0f, cy = dy * 640.0f, cz = ez + dz * 640.0f;
    float rx = -dz, rz = dx; float rl = std::sqrt(rx * rx + rz * rz); rx /= rl; rz /= rl; // right
    float ux = -dy * rz, uy = rz * dx - rx * dz, uz = dy * rx;                            // up
    // glow
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f, 0.85f, 0.55f, 0.55f); glVertex3f(cx, cy, cz);
    glColor4f(1.0f, 0.70f, 0.50f, 0.0f);
    for (int i = 0; i <= 32; ++i) {
        float a = i * 2.0f * PI / 32.0f, c = std::cos(a) * 130.0f, s = std::sin(a) * 130.0f;
        glVertex3f(cx + rx * c + ux * s, cy + uy * s, cz + rz * c + uz * s);
    }
    glEnd();
    // blocky disc made of pixel rows
    const float px = 5.0f; const int R = 8;
    glColor4f(1.0f, 0.93f, 0.62f, 1.0f);
    glBegin(GL_QUADS);
    for (int row = -R; row < R; ++row) {
        float yc = row + 0.5f;
        int hw = int(std::floor(std::sqrt(float(R * R) - yc * yc) + 0.5f));
        float y0 = row * px, y1 = (row + 1) * px, x0 = -hw * px, x1 = hw * px;
        glVertex3f(cx + rx * x0 + ux * y0, cy + uy * y0, cz + rz * x0 + uz * y0);
        glVertex3f(cx + rx * x1 + ux * y0, cy + uy * y0, cz + rz * x1 + uz * y0);
        glVertex3f(cx + rx * x1 + ux * y1, cy + uy * y1, cz + rz * x1 + uz * y1);
        glVertex3f(cx + rx * x0 + ux * y1, cy + uy * y1, cz + rz * x0 + uz * y1);
    }
    glEnd();
    glDepthMask(GL_TRUE);

    // ---- mountain rings that follow the camera (always on the horizon) ----
    auto ring = [&](float rad, int n, float base, float a1, float a2, float a3, float seed,
                    const float top[3], const float bot[3]) {
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= n; ++i) {
            float a = i * 2.0f * PI / n;
            float h = base + a1 * std::sin(a * 7 + seed) + a2 * std::sin(a * 13 + seed * 2) + a3 * std::sin(a * 23 + seed * 3);
            if (i % 2) h *= 0.62f; // jagged peaks
            float x = ex + std::sin(a) * rad, z = ez - std::cos(a) * rad;
            glColor3fv(bot); glVertex3f(x, -20.0f, z);
            glColor3fv(top); glVertex3f(x, h, z);
        }
        glEnd();
    };
    const float farTop[3]  = {0.56f, 0.42f, 0.62f}, farBot[3]  = {0.93f, 0.68f, 0.58f};
    const float nearTop[3] = {0.30f, 0.42f, 0.45f}, nearBot[3] = {0.86f, 0.66f, 0.56f};
    ring(600.0f, 72, 70.0f, 18.0f, 12.0f, 8.0f, 0.7f, farTop, farBot);
    ring(520.0f, 90, 38.0f, 12.0f, 9.0f, 6.0f, 2.1f, nearTop, nearBot);

    // ---- blocky pixel clouds ----
    for (int i = 0; i < 12; ++i) {
        float a = i * 0.523f + 0.3f + tick * 0.00004f;
        float h = 135.0f + 30.0f * std::sin(i * 2.1f);
        float x = ex + std::sin(a) * 480.0f, z = ez - std::cos(a) * 480.0f;
        float s = 0.8f + 0.4f * hash2(i, 1, 11);
        glColor4f(0.98f, 0.80f, 0.78f, 0.95f);
        boxAt(x, h - 4 * s, z, 22 * s, 4 * s, 7 * s);
        glColor4f(1.0f, 0.93f, 0.90f, 0.95f);
        boxAt(x - 6 * s, h + 3 * s, z, 12 * s, 4 * s, 6 * s);
        boxAt(x + 8 * s, h + 1 * s, z, 9 * s, 4 * s, 6 * s);
    }
}

void GLWidget::drawTemple(float ex, float ez)
{
    const float tz = -TRACK_LEN - 110.0f, tx = roadCenter(tz);
    float dx = tx - ex, dz = tz - ez, dist = std::sqrt(dx * dx + dz * dz);
    float px = tx, pz = tz, s = 1.0f;
    if (dist > TEMPLE_DIST) {                 // keep it on the horizon as a backdrop
        float k = TEMPLE_DIST / dist;
        px = ex + dx * k; pz = ez + dz * k;
        s = std::max(k, 0.42f);
    }

    glPushMatrix();
    glTranslatef(px, 0, pz);
    glScalef(s, s, s);

    // stepped pyramid
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texStone);
    g_ts = 0.18f;
    for (int i = 0; i < 5; ++i) {
        float hw = 62.0f - i * 11.0f, hd = 42.0f - i * 7.0f;
        if (i % 2) glColor3f(0.70f, 0.80f, 0.62f); else glColor3f(0.86f, 0.86f, 0.74f);
        boxAt(0, i * 10.0f + 5.0f, 0, hw, 5.0f, hd);
    }
    // stairs
    glColor3f(0.66f, 0.64f, 0.58f);
    for (int k = 0; k < 25; ++k) {
        float zf = 46.0f - k * 1.12f;
        boxAt(0, k * 2.0f + 1.0f, zf - 5.0f, 7.0f, 1.0f, 5.0f);
    }
    // shrine back wall + towers
    glColor3f(0.82f, 0.82f, 0.72f);
    boxAt(0, 56.5f, -6.0f, 15.0f, 6.5f, 2.0f);
    for (int sd = -1; sd <= 1; sd += 2) boxAt(sd * 72.0f, 22.0f, 0, 7.0f, 22.0f, 7.0f);
    glDisable(GL_TEXTURE_2D);
    g_ts = 0.25f;

    // columns
    glColor3f(0.92f, 0.90f, 0.82f);
    for (float cxp : {-13.0f, -6.5f, 6.5f, 13.0f}) {
        glPushMatrix();
        glTranslatef(cxp, 50.0f, 9.0f);
        glRotatef(-90, 1, 0, 0);
        gluCylinder(quad, 1.3, 1.3, 13.0, 10, 1);
        glPopMatrix();
    }
    // roof slab + golden pediment
    glColor3f(0.80f, 0.78f, 0.70f);
    boxAt(0, 64.2f, 1.0f, 17.0f, 1.2f, 11.0f);
    glColor3f(0.95f, 0.75f, 0.30f);
    V3 C = {0, 67.0f, 1.0f};
    glBegin(GL_TRIANGLES);
    triN({-17, 65.4f, 12}, {17, 65.4f, 12}, {0, 71.5f, 12}, C);
    triN({-17, 65.4f, -10}, {17, 65.4f, -10}, {0, 71.5f, -10}, C);
    glEnd();
    glBegin(GL_QUADS);
    quadN({-17, 65.4f, 12}, {0, 71.5f, 12}, {0, 71.5f, -10}, {-17, 65.4f, -10}, C);
    quadN({17, 65.4f, 12}, {0, 71.5f, 12}, {0, 71.5f, -10}, {17, 65.4f, -10}, C);
    glEnd();
    // tower caps
    glColor3f(0.25f, 0.45f, 0.35f);
    for (int sd = -1; sd <= 1; sd += 2) {
        float x = sd * 72.0f;
        boxAt(x, 45.0f, 0, 8.0f, 1.0f, 8.0f);
    }
    // hanging vines
    glColor3f(0.22f, 0.48f, 0.20f);
    for (int i = 0; i < 24; ++i) {
        float x = -58.0f + i * 5.0f;
        if (std::fabs(x) < 8.5f) continue;
        float len = 3.0f + 5.0f * hash2(i, 7, 9);
        boxAt(x, 10.0f - len * 0.5f, 42.15f, 0.35f, len * 0.5f, 0.15f);
        float len2 = 2.0f + 4.0f * hash2(i, 8, 9);
        if (std::fabs(x) < 50.0f) boxAt(x + 2.0f, 20.0f - len2 * 0.5f, 35.15f, 0.35f, len2 * 0.5f, 0.15f);
    }

    // glowing gem + fire/water flames (unlit = emissive look)
    glDisable(GL_LIGHTING);
    float pulse = 0.5f + 0.5f * std::sin(tick * 0.05f);
    glPushMatrix();
    glTranslatef(0, 79.0f, 1.0f);
    glRotatef(tick * 1.5f, 0, 1, 0);
    for (int layer = 0; layer < 2; ++layer) {
        float g = layer == 0 ? 3.0f : 5.5f, alpha = layer == 0 ? 1.0f : 0.3f;
        glColor4f(1.0f - 0.7f * pulse, 0.5f + 0.35f * pulse, 0.15f + 0.85f * pulse, alpha);
        V3 eq[4] = {{g, 0, 0}, {0, 0, g}, {-g, 0, 0}, {0, 0, -g}};
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 4; ++i) {
            V3 a = eq[i], b = eq[(i + 1) % 4];
            glVertex3f(0, g * 1.4f, 0); glVertex3f(a.x, a.y, a.z); glVertex3f(b.x, b.y, b.z);
            glVertex3f(0, -g * 1.4f, 0); glVertex3f(a.x, a.y, a.z); glVertex3f(b.x, b.y, b.z);
        }
        glEnd();
    }
    glPopMatrix();
    for (int sd = -1; sd <= 1; sd += 2) {
        float f = 1.0f + 0.15f * std::sin(tick * 0.35f + sd);
        glPushMatrix();
        glTranslatef(sd * 72.0f, 46.0f, 0);
        glRotatef(-90, 1, 0, 0);
        if (sd < 0) glColor4f(1.0f, 0.42f, 0.10f, 0.9f); else glColor4f(0.25f, 0.60f, 1.0f, 0.9f);
        gluCylinder(quad, 5.0, 0.0, 12.0 * f, 8, 1);
        if (sd < 0) glColor4f(1.0f, 0.85f, 0.30f, 1.0f); else glColor4f(0.75f, 0.95f, 1.0f, 1.0f);
        gluCylinder(quad, 2.8, 0.0, 7.0 * f, 8, 1);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

// ===========================================================================
//  GROUND + ROAD
// ===========================================================================
void GLWidget::drawGround(float ex, float ez)
{
    const float RNG = 450.0f, C = 25.0f, T = 1.0f / 8.0f;
    float x0 = std::floor((ex - RNG) / C) * C, z0 = std::floor((ez - RNG) / C) * C;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texGrass);
    glColor3f(1, 1, 1);
    glNormal3f(0, 1, 0);
    glBegin(GL_QUADS);
    for (float x = x0; x < ex + RNG; x += C)
        for (float z = z0; z < ez + RNG; z += C) {
            glTexCoord2f(x * T, z * T);           glVertex3f(x, -0.05f, z);
            glTexCoord2f((x + C) * T, z * T);     glVertex3f(x + C, -0.05f, z);
            glTexCoord2f((x + C) * T, (z + C) * T); glVertex3f(x + C, -0.05f, z + C);
            glTexCoord2f(x * T, (z + C) * T);     glVertex3f(x, -0.05f, z + C);
        }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void GLWidget::drawRoad(float camZ)
{
    const float STEP = 3.0f;
    const float endZ = -TRACK_LEN - 40.0f;
    float zStart = std::ceil((camZ + 30.0f) / STEP) * STEP;
    float zEnd = std::max(camZ - VIEW_DIST, endZ);

    // ---- asphalt ----
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texAsphalt);
    glColor3f(1, 1, 1);
    glNormal3f(0, 1, 0);
    const float T = 0.25f;
    glBegin(GL_QUADS);
    for (float z = zStart; z > zEnd; z -= STEP) {
        float z2 = z - STEP, c1 = roadCenter(z), c2 = roadCenter(z2);
        glTexCoord2f(-ROAD_HALF * T, -z * T);  glVertex3f(c1 - ROAD_HALF, 0, z);
        glTexCoord2f( ROAD_HALF * T, -z * T);  glVertex3f(c1 + ROAD_HALF, 0, z);
        glTexCoord2f( ROAD_HALF * T, -z2 * T); glVertex3f(c2 + ROAD_HALF, 0, z2);
        glTexCoord2f(-ROAD_HALF * T, -z2 * T); glVertex3f(c2 - ROAD_HALF, 0, z2);
    }
    glEnd();

    // ---- temple plaza ----
    if (camZ - VIEW_DIST < endZ) {
        glBindTexture(GL_TEXTURE_2D, texStone);
        glColor3f(0.85f, 0.82f, 0.72f);
        const float PT = 0.12f;
        glBegin(GL_QUADS);
        for (float z = endZ; z > endZ - 150.0f; z -= 10.0f)
            for (float x = -80.0f; x < 80.0f; x += 10.0f) {
                glTexCoord2f(x * PT, z * PT);                 glVertex3f(x, 0.0f, z);
                glTexCoord2f((x + 10) * PT, z * PT);          glVertex3f(x + 10, 0.0f, z);
                glTexCoord2f((x + 10) * PT, (z - 10) * PT);   glVertex3f(x + 10, 0.0f, z - 10);
                glTexCoord2f(x * PT, (z - 10) * PT);          glVertex3f(x, 0.0f, z - 10);
            }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);

    // ---- curbs, edge lines, lane dashes ----
    glBegin(GL_QUADS);
    for (float z = zStart; z > zEnd; z -= STEP) {
        float z2 = z - STEP, c1 = roadCenter(z), c2 = roadCenter(z2);
        int idx = int(std::floor(-z / STEP));
        if (idx & 1) glColor3f(0.85f, 0.12f, 0.12f); else glColor3f(0.95f, 0.95f, 0.95f);
        for (float side : {-1.0f, 1.0f}) {
            float a = ROAD_HALF, b = ROAD_HALF + 0.9f;
            glVertex3f(c1 + side * a, 0.02f, z);  glVertex3f(c1 + side * b, 0.02f, z);
            glVertex3f(c2 + side * b, 0.02f, z2); glVertex3f(c2 + side * a, 0.02f, z2);
        }
        glColor3f(0.95f, 0.95f, 0.92f);
        for (float side : {-1.0f, 1.0f}) {
            float a = ROAD_HALF - 0.45f, b = ROAD_HALF - 0.30f;
            glVertex3f(c1 + side * a, 0.02f, z);  glVertex3f(c1 + side * b, 0.02f, z);
            glVertex3f(c2 + side * b, 0.02f, z2); glVertex3f(c2 + side * a, 0.02f, z2);
        }
        if (((idx % 4) + 4) % 4 < 2) {
            for (float lx : {-ROAD_HALF / 3.0f, ROAD_HALF / 3.0f}) {
                glVertex3f(c1 + lx - 0.08f, 0.03f, z);  glVertex3f(c1 + lx + 0.08f, 0.03f, z);
                glVertex3f(c2 + lx + 0.08f, 0.03f, z2); glVertex3f(c2 + lx - 0.08f, 0.03f, z2);
            }
        }
    }
    glEnd();

    // ---- checkered start / finish lines ----
    auto checker = [&](float z0) {
        if (z0 > camZ + 30.0f || z0 < camZ - VIEW_DIST) return;
        float c = roadCenter(z0);
        glBegin(GL_QUADS);
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < 14; ++i) {
                if ((i + row) % 2 == 0) glColor3f(0.95f, 0.95f, 0.95f); else glColor3f(0.06f, 0.06f, 0.06f);
                float x0 = c - ROAD_HALF + i, zz = z0 - row;
                glVertex3f(x0, 0.035f, zz); glVertex3f(x0 + 1, 0.035f, zz);
                glVertex3f(x0 + 1, 0.035f, zz - 1); glVertex3f(x0, 0.035f, zz - 1);
            }
        glEnd();
    };
    checker(-6.0f);
    checker(-TRACK_LEN);

    // ---- guard rails ----
    float railEnd = std::max(zEnd, -TRACK_LEN - 20.0f);
    glColor3f(0.75f, 0.77f, 0.82f);
    glBegin(GL_QUADS);
    for (float z = zStart; z > railEnd; z -= STEP) {
        float z2 = z - STEP, c1 = roadCenter(z), c2 = roadCenter(z2);
        for (float side : {-1.0f, 1.0f}) {
            float x1 = c1 + side * RAIL_OFFSET, x2 = c2 + side * RAIL_OFFSET;
            glNormal3f(-side, 0, 0);
            glVertex3f(x1, 0.55f, z); glVertex3f(x2, 0.55f, z2);
            glVertex3f(x2, 0.95f, z2); glVertex3f(x1, 0.95f, z);
        }
    }
    glEnd();
    glColor3f(0.35f, 0.36f, 0.40f);
    for (float z = zStart; z > railEnd; z -= STEP * 2.0f) {
        float c = roadCenter(z);
        boxAt(c - RAIL_OFFSET, 0.5f, z, 0.08f, 0.5f, 0.08f);
        boxAt(c + RAIL_OFFSET, 0.5f, z, 0.08f, 0.5f, 0.08f);
    }
}

void GLWidget::drawArch(float z, bool finish)
{
    // drawn even when slightly out of fog range so the finish is visible on approach
    if (z > p1.z + 40.0f && z > p2.z + 40.0f) return;
    float c = roadCenter(z), px = ROAD_HALF + 1.6f;
    glColor3f(0.30f, 0.30f, 0.36f);
    boxAt(c - px, 3.75f, z, 0.5f, 3.75f, 0.5f);
    boxAt(c + px, 3.75f, z, 0.5f, 3.75f, 0.5f);
    if (finish) {
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < 18; ++i) {
                if ((i + row) % 2 == 0) glColor3f(0.95f, 0.95f, 0.95f); else glColor3f(0.06f, 0.06f, 0.06f);
                boxAt(c - 9.0f + i + 0.5f, 7.0f + row, z, 0.5f, 0.5f, 0.3f);
            }
    } else {
        glColor3f(0.85f, 0.15f, 0.15f);
        boxAt(c, 7.5f, z, 9.0f, 1.0f, 0.3f);
    }
    glDisable(GL_LIGHTING);
    if (finish) { glColor3f(1.0f, 0.82f, 0.25f); drawText3D("FINISH", c, 8.4f, z + 0.32f, 0.3f); }
    else        { glColor3f(1.0f, 1.0f, 1.0f);   drawText3D("START",  c, 6.95f, z + 0.32f, 0.16f); }
    glEnable(GL_LIGHTING);
}

// ===========================================================================
//  SCENERY PROPS
// ===========================================================================
void GLWidget::drawProps(float camZ)
{
    for (const Prop &p : props) {
        if (p.z > camZ + 25.0f || p.z < camZ - VIEW_DIST) continue;
        glPushMatrix();
        glTranslatef(p.x, 0, p.z);
        glRotatef(p.rot, 0, 1, 0);
        glScalef(p.scale, p.scale, p.scale);
        switch (p.type) {
        case Prop::House:     drawHouse(p.palette); break;
        case Prop::Pine:      drawPine(); break;
        case Prop::RoundTree: drawRoundTree(); break;
        case Prop::Lamp:      drawLamp(); break;
        case Prop::Torch:     drawTorch(p.palette, p.z * 0.37f); break;
        }
        glPopMatrix();
    }
}

void GLWidget::drawHouse(int pal)
{
    static const float walls[4][3] = {{0.97f, 0.88f, 0.72f}, {0.93f, 0.67f, 0.58f},
                                      {0.70f, 0.82f, 0.93f}, {0.74f, 0.90f, 0.76f}};
    static const float roofs[4][3] = {{0.78f, 0.26f, 0.20f}, {0.22f, 0.46f, 0.52f},
                                      {0.52f, 0.32f, 0.22f}, {0.47f, 0.32f, 0.58f}};
    const float hw = 3.2f, hd = 2.8f, h = 4.2f, rh = 2.6f, ov = 0.4f;
    const float yE = h - rh * ov / hd; // eave height at overhang

    // walls (plaster texture tinted)
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texPlaster);
    g_ts = 0.3f;
    glColor3fv(walls[pal]);
    boxAt(0, h * 0.5f, 0, hw, h * 0.5f, hd);
    glBegin(GL_TRIANGLES);
    for (float sx : {hw, -hw}) {
        glNormal3f(sx > 0 ? 1.0f : -1.0f, 0, 0);
        tv(sx, h, hd, hd, h); tv(sx, h, -hd, -hd, h); tv(sx, h + rh, 0, 0, h + rh);
    }
    glEnd();

    // roof (tile texture tinted)
    glBindTexture(GL_TEXTURE_2D, texRoof);
    g_ts = 0.35f;
    glColor3fv(roofs[pal]);
    float slope = std::sqrt((hd + ov) * (hd + ov) + (h + rh - yE) * (h + rh - yE));
    glBegin(GL_QUADS);
    glNormal3f(0, hd, rh);
    tv(-hw - ov, yE, hd + ov, -hw - ov, 0); tv(hw + ov, yE, hd + ov, hw + ov, 0);
    tv(hw + ov, h + rh, 0, hw + ov, slope); tv(-hw - ov, h + rh, 0, -hw - ov, slope);
    glNormal3f(0, hd, -rh);
    tv(-hw - ov, yE, -hd - ov, -hw - ov, 0); tv(hw + ov, yE, -hd - ov, hw + ov, 0);
    tv(hw + ov, h + rh, 0, hw + ov, slope); tv(-hw - ov, h + rh, 0, -hw - ov, slope);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    g_ts = 0.25f;

    // door, chimney
    glColor3f(0.42f, 0.26f, 0.16f); boxAt(0, 1.1f, hd + 0.02f, 0.6f, 1.1f, 0.06f);
    glColor3f(0.95f, 0.80f, 0.30f); boxAt(0.35f, 1.1f, hd + 0.1f, 0.06f, 0.06f, 0.03f);
    glColor3f(0.55f, 0.33f, 0.28f); boxAt(1.7f, h + rh * 0.55f, -1.0f, 0.35f, 1.1f, 0.35f);
    glColor3f(0.45f, 0.60f, 0.30f); boxAt(0, 0.12f, hd + 1.4f, 1.6f, 0.12f, 0.5f); // little hedge

    // warm glowing windows
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.86f, 0.45f);
    boxAt(-1.9f, 2.5f, hd + 0.03f, 0.6f, 0.55f, 0.04f);
    boxAt( 1.9f, 2.5f, hd + 0.03f, 0.6f, 0.55f, 0.04f);
    boxAt( hw + 0.03f, 2.5f, 0, 0.04f, 0.55f, 0.6f);
    boxAt(-hw - 0.03f, 2.5f, 0, 0.04f, 0.55f, 0.6f);
    glColor3f(0.30f, 0.20f, 0.15f);
    for (float wx : {-1.9f, 1.9f}) {
        boxAt(wx, 2.5f, hd + 0.08f, 0.04f, 0.55f, 0.02f);
        boxAt(wx, 2.5f, hd + 0.08f, 0.6f, 0.04f, 0.02f);
    }
    glEnable(GL_LIGHTING);
}

void GLWidget::drawPine()
{
    glColor3f(0.42f, 0.28f, 0.16f);
    boxAt(0, 0.8f, 0, 0.25f, 0.8f, 0.25f);
    for (int i = 0; i < 3; ++i) {
        glPushMatrix();
        glTranslatef(0, 1.2f + i * 1.35f, 0);
        glRotatef(-90, 1, 0, 0);
        glColor3f(0.13f + 0.03f * i, 0.40f + 0.05f * i, 0.24f);
        double r = 2.3 - i * 0.55;
        gluCylinder(quad, r, 0.0, 2.6, 8, 1);
        gluDisk(quad, 0.0, r, 8, 1);
        glPopMatrix();
    }
}

void GLWidget::drawRoundTree()
{
    glColor3f(0.45f, 0.30f, 0.18f);
    boxAt(0, 1.3f, 0, 0.3f, 1.3f, 0.3f);
    glColor3f(0.36f, 0.62f, 0.26f);
    const float blobs[3][4] = {{0, 3.6f, 0, 2.0f}, {0.9f, 3.0f, 0.5f, 1.3f}, {-0.8f, 3.2f, -0.4f, 1.4f}};
    for (const auto &b : blobs) {
        glPushMatrix();
        glTranslatef(b[0], b[1], b[2]);
        gluSphere(quad, b[3], 9, 7);
        glPopMatrix();
    }
}

void GLWidget::drawLamp()
{
    glColor3f(0.20f, 0.20f, 0.25f);
    boxAt(0, 3.0f, 0, 0.1f, 3.0f, 0.1f);
    boxAt(0, 5.9f, 0.8f, 0.06f, 0.06f, 0.8f);
    boxAt(0, 5.8f, 1.5f, 0.25f, 0.12f, 0.35f);
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.92f, 0.60f);
    boxAt(0, 5.66f, 1.5f, 0.18f, 0.05f, 0.25f);
    glEnable(GL_LIGHTING);
}

void GLWidget::drawTorch(int pal, float phase)
{
    glColor3f(0.55f, 0.55f, 0.50f);
    boxAt(0, 0.9f, 0, 0.5f, 0.9f, 0.5f);
    glColor3f(0.35f, 0.33f, 0.30f);
    boxAt(0, 1.9f, 0, 0.6f, 0.15f, 0.6f);
    glDisable(GL_LIGHTING);
    float f = 1.0f + 0.18f * std::sin(tick * 0.4f + phase);
    glPushMatrix();
    glTranslatef(0, 2.0f, 0);
    glRotatef(-90, 1, 0, 0);
    if (pal == 0) glColor4f(1.0f, 0.85f, 0.30f, 1.0f); else glColor4f(0.75f, 0.95f, 1.0f, 1.0f);
    gluCylinder(quad, 0.28, 0.0, 0.8 * f, 8, 1);
    if (pal == 0) glColor4f(1.0f, 0.40f, 0.10f, 0.75f); else glColor4f(0.20f, 0.55f, 1.0f, 0.75f);
    gluCylinder(quad, 0.48, 0.0, 1.3 * f, 8, 1);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

// ===========================================================================
//  OBSTACLES
// ===========================================================================
void GLWidget::drawObstacles(float camZ)
{
    for (const Obstacle &o : obstacles) {
        if (o.z > camZ + 25.0f || o.z < camZ - VIEW_DIST) continue;
        glPushMatrix();
        glTranslatef(o.x, o.y, o.z);
        if (o.knocked) glRotatef(o.rot, 1, 0, 0.4f);

        if (o.type == Obstacle::Cone) {
            glColor3f(0.1f, 0.1f, 0.1f);
            boxAt(0, 0.04f, 0, 0.5f, 0.04f, 0.5f);
            glPushMatrix(); glRotatef(-90, 1, 0, 0);
            glColor3f(1.0f, 0.45f, 0.05f);
            gluCylinder(quad, 0.42, 0.05, 1.1, 10, 1);
            glPopMatrix();
            glPushMatrix(); glTranslatef(0, 0.45f, 0); glRotatef(-90, 1, 0, 0);
            glColor3f(1, 1, 1);
            gluCylinder(quad, 0.275, 0.215, 0.18, 10, 1);
            glPopMatrix();
        } else if (o.type == Obstacle::Barrel) {
            glPushMatrix(); glRotatef(-90, 1, 0, 0);
            glColor3f(0.85f, 0.28f, 0.08f);
            gluCylinder(quad, 0.6, 0.6, 1.3, 12, 1);
            glPushMatrix(); glTranslatef(0, 0, 1.3f); gluDisk(quad, 0, 0.6, 12, 1); glPopMatrix();
            glColor3f(1.0f, 0.85f, 0.10f);
            for (float rz : {0.3f, 0.9f}) {
                glPushMatrix(); glTranslatef(0, 0, rz);
                gluCylinder(quad, 0.62, 0.62, 0.1, 12, 1);
                glPopMatrix();
            }
            glPopMatrix();
        } else { // red/white striped barrier
            glColor3f(0.55f, 0.55f, 0.55f);
            boxAt(0, 0.1f, 0, o.hx + 0.05f, 0.1f, o.hz + 0.1f);
            int n = std::max(1, int(o.hx * 2.0f / 0.55f));
            float seg = o.hx * 2.0f / n;
            for (int k = 0; k < n; ++k) {
                if (k % 2) glColor3f(0.95f, 0.95f, 0.95f); else glColor3f(0.88f, 0.12f, 0.10f);
                boxAt(-o.hx + seg * (k + 0.5f), 0.7f, 0, seg * 0.5f, 0.5f, o.hz);
            }
            glDisable(GL_LIGHTING);
            bool blink = (tick / 20) % 2 == 0;
            glColor3f(blink ? 1.0f : 0.4f, blink ? 0.75f : 0.3f, 0.1f);
            boxAt(-o.hx * 0.7f, 1.3f, 0, 0.15f, 0.1f, 0.15f);
            boxAt( o.hx * 0.7f, 1.3f, 0, 0.15f, 0.1f, 0.15f);
            glEnable(GL_LIGHTING);
        }
        glPopMatrix();
    }
}

// ===========================================================================
//  VEHICLES
// ===========================================================================
void GLWidget::drawWheel(float x, float y, float z, float steerDeg, float spin)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(-steerDeg, 0, 1, 0);
    glRotatef(-spin, 1, 0, 0);
    glRotatef(90, 0, 1, 0);           // cylinder axis -> X
    glTranslatef(0, 0, -0.17f);
    glColor3f(0.08f, 0.08f, 0.09f);
    gluCylinder(quad, 0.42, 0.42, 0.34, 14, 1);
    glColor3f(0.70f, 0.70f, 0.75f);
    gluDisk(quad, 0, 0.42, 14, 1);
    glPushMatrix(); glTranslatef(0, 0, 0.34f); gluDisk(quad, 0, 0.42, 14, 1); glPopMatrix();
    glColor3f(0.25f, 0.25f, 0.28f);   // spokes (make rotation visible)
    for (float sz : {-0.01f, 0.35f}) {
        boxAt(0, 0, sz, 0.36f, 0.05f, 0.01f);
        boxAt(0, 0, sz, 0.05f, 0.36f, 0.01f);
    }
    glPopMatrix();
}

void GLWidget::drawShadow(float x, float z, float heading, float hw, float hl)
{
    glPushMatrix();
    glTranslatef(x, 0.045f, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);
    glColor4f(0, 0, 0, 0.38f);
    glBegin(GL_QUADS);
    glVertex3f(-hw, 0, -hl); glVertex3f(hw, 0, -hl); glVertex3f(hw, 0, hl); glVertex3f(-hw, 0, hl);
    glEnd();
    glPopMatrix();
}

void GLWidget::drawCar(float x, float z, float heading, float steer, float spin,
                       float r, float g, float b, bool nitro)
{
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);   // model faces -Z

    // body
    glColor3f(r, g, b);
    boxAt(0, 0.62f, 0, 0.95f, 0.32f, 2.1f);
    // bumpers + side skirts
    glColor3f(r * 0.55f, g * 0.55f, b * 0.55f);
    boxAt(0, 0.40f, -2.16f, 0.95f, 0.13f, 0.1f);
    boxAt(0, 0.40f,  2.16f, 0.95f, 0.13f, 0.1f);
    glColor3f(0.08f, 0.08f, 0.10f);
    boxAt(0, 0.32f, 0, 0.97f, 0.05f, 1.55f);
    // racing stripe
    glColor3f(0.96f, 0.96f, 0.96f);
    boxAt(0, 0.945f, -0.2f, 0.18f, 0.012f, 1.9f);

    // tinted glass cabin (trapezoid)
    V3 C = {0, 1.0f, 0.2f};
    V3 t0 = {-0.68f, 1.42f, -0.35f}, t1 = {0.68f, 1.42f, -0.35f}, t2 = {0.68f, 1.42f, 0.75f}, t3 = {-0.68f, 1.42f, 0.75f};
    V3 b0 = {-0.90f, 0.94f, -1.05f}, b1 = {0.90f, 0.94f, -1.05f}, b2 = {0.90f, 0.94f, 1.45f}, b3 = {-0.90f, 0.94f, 1.45f};
    glColor3f(0.10f, 0.13f, 0.20f);
    glBegin(GL_QUADS);
    quadN(b0, b1, t1, t0, C);
    quadN(b3, b2, t2, t3, C);
    quadN(b0, t0, t3, b3, C);
    quadN(b1, t1, t2, b2, C);
    glEnd();
    glColor3f(r, g, b);
    boxAt(0, 1.44f, 0.2f, 0.68f, 0.025f, 0.55f);       // roof
    boxAt(-1.0f, 1.05f, -0.8f, 0.1f, 0.06f, 0.1f);     // mirrors
    boxAt( 1.0f, 1.05f, -0.8f, 0.1f, 0.06f, 0.1f);

    // spoiler
    glColor3f(0.1f, 0.1f, 0.12f);
    boxAt(-0.6f, 1.12f, 1.85f, 0.06f, 0.2f, 0.08f);
    boxAt( 0.6f, 1.12f, 1.85f, 0.06f, 0.2f, 0.08f);
    glColor3f(r * 0.8f, g * 0.8f, b * 0.8f);
    boxAt(0, 1.36f, 1.9f, 0.98f, 0.04f, 0.25f);

    // lights (unlit -> glowing)
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 0.85f);
    boxAt(-0.62f, 0.70f, -2.11f, 0.22f, 0.08f, 0.02f);
    boxAt( 0.62f, 0.70f, -2.11f, 0.22f, 0.08f, 0.02f);
    glColor3f(1.0f, 0.10f, 0.10f);
    boxAt(-0.62f, 0.75f, 2.11f, 0.25f, 0.07f, 0.02f);
    boxAt( 0.62f, 0.75f, 2.11f, 0.25f, 0.07f, 0.02f);
    if (nitro) {
        float fl = 0.7f + 0.3f * std::sin(tick * 1.7f);
        for (float sx : {-0.4f, 0.4f}) {
            glPushMatrix();
            glTranslatef(sx, 0.45f, 2.2f);
            glColor4f(0.35f, 0.70f, 1.0f, 0.85f);
            gluCylinder(quad, 0.20, 0.0, 1.0 + 0.6 * fl, 8, 1);
            glColor4f(1.0f, 1.0f, 1.0f, 0.95f);
            gluCylinder(quad, 0.10, 0.0, 0.5 + 0.3 * fl, 8, 1);
            glPopMatrix();
        }
    }
    glEnable(GL_LIGHTING);

    // wheels
    float sd = steer * 180.0f / PI;
    drawWheel(-0.92f, 0.42f, -1.35f, sd, spin);
    drawWheel( 0.92f, 0.42f, -1.35f, sd, spin);
    drawWheel(-0.92f, 0.42f,  1.35f, 0, spin);
    drawWheel( 0.92f, 0.42f,  1.35f, 0, spin);

    glPopMatrix();
}

void GLWidget::drawTruck(float x, float z, float heading, float spin, float r, float g, float b)
{
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(-heading * 180.0f / PI, 0, 1, 0);

    glColor3f(0.15f, 0.15f, 0.17f);
    boxAt(0, 0.45f, -0.1f, 1.0f, 0.15f, 3.4f);              // chassis
    glColor3f(r, g, b);
    boxAt(0, 1.25f, -2.7f, 1.05f, 0.95f, 0.85f);            // cab
    glColor3f(0.10f, 0.13f, 0.20f);
    boxAt(0, 1.6f, -3.56f, 0.9f, 0.4f, 0.02f);              // windshield
    glColor3f(0.92f, 0.92f, 0.90f);
    boxAt(0, 1.85f, 0.75f, 1.15f, 1.45f, 2.6f);             // cargo box
    glColor3f(r, g, b);
    boxAt(0, 1.3f, 0.75f, 1.17f, 0.15f, 2.62f);             // stripe

    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 0.85f);
    boxAt(-0.7f, 0.8f, -3.56f, 0.2f, 0.1f, 0.02f);
    boxAt( 0.7f, 0.8f, -3.56f, 0.2f, 0.1f, 0.02f);
    glColor3f(1.0f, 0.1f, 0.1f);
    boxAt(-0.9f, 0.7f, 3.36f, 0.15f, 0.12f, 0.02f);
    boxAt( 0.9f, 0.7f, 3.36f, 0.15f, 0.12f, 0.02f);
    glEnable(GL_LIGHTING);

    for (float wz : {-2.7f, 0.0f, 2.4f}) {
        drawWheel(-1.0f, 0.42f, wz, 0, spin);
        drawWheel( 1.0f, 0.42f, wz, 0, spin);
    }
    glPopMatrix();
}

// ===========================================================================
//  HUD (pixel font, raster bars)
// ===========================================================================
float GLWidget::textWidth(const char *s, float ps) const
{
    size_t n = std::strlen(s);
    return n ? (float(n) * 6.0f - 1.0f) * ps : 0.0f;
}

void GLWidget::drawText(const char *s, float x, float y, float ps, float r, float g, float b)
{
    for (int pass = 0; pass < 2; ++pass) {          // pass 0 = drop shadow
        float ox = pass == 0 ? ps * 0.5f : 0.0f, oy = pass == 0 ? -ps * 0.5f : 0.0f;
        if (pass == 0) glColor4f(0, 0, 0, 0.55f); else glColor4f(r, g, b, 1.0f);
        glBegin(GL_QUADS);
        float cx = x + ox;
        for (const char *p = s; *p; ++p, cx += 6.0f * ps) {
            const unsigned char *rows = pixelGlyph(*p);
            if (!rows) continue;
            for (int ry = 0; ry < 7; ++ry)
                for (int rx = 0; rx < 5; ++rx)
                    if (rows[ry] & (0x10 >> rx)) {
                        float qx = cx + rx * ps, qy = y + oy + (6 - ry) * ps;
                        glVertex2f(qx, qy); glVertex2f(qx + ps, qy);
                        glVertex2f(qx + ps, qy + ps); glVertex2f(qx, qy + ps);
                    }
        }
        glEnd();
    }
}

void GLWidget::drawHUD(const Car &me, const Car &other, int playerNum, int vw, int vh)
{
    const float ps = std::max(2.0f, vh / 150.0f);
    const float m = ps * 4.0f;
    char buf[64];

    auto rect = [](float x0, float y0, float x1, float y1) {
        glBegin(GL_QUADS);
        glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
        glEnd();
    };

    // ---- player tag + race position ----
    const char *tag = playerNum == 1 ? "P1" : "P2";
    float tagPs = ps * 2.0f;
    glColor4f(0, 0, 0, 0.35f);
    rect(m * 0.5f, vh - m * 0.5f - 7 * tagPs - m, m * 1.5f + textWidth("P1 2ND", tagPs) + m, vh - m * 0.5f);
    drawText(tag, m, vh - m - 7 * tagPs, tagPs,
             std::min(1.0f, me.r + 0.25f), std::min(1.0f, me.g + 0.25f), std::min(1.0f, me.b + 0.25f));
    bool first = me.z <= other.z;
    drawText(first ? "1ST" : "2ND", m + textWidth("P1 ", tagPs), vh - m - 7 * tagPs, tagPs,
             first ? 1.0f : 0.8f, first ? 0.85f : 0.8f, first ? 0.2f : 0.85f);

    // distance remaining
    snprintf(buf, sizeof buf, "%d M", int(std::max(0.0f, TRACK_LEN + me.z)));
    drawText(buf, m, vh - m * 2.0f - 7 * tagPs - 7 * ps, ps, 1.0f, 0.95f, 0.85f);

    // ---- speedometer ----
    int kmh = int(std::fabs(me.speed) / MAX_SPEED * 240.0f);
    snprintf(buf, sizeof buf, "%d", kmh);
    float big = ps * 3.0f;
    float kw = textWidth("KMH", ps);
    drawText(buf, vw - m - kw - ps * 2 - textWidth(buf, big), m, big, 1, 1, 1);
    drawText("KMH", vw - m - kw, m, ps, 1.0f, 0.9f, 0.6f);
    const int segs = 20;
    float bw = ps * 1.6f, bh = ps * 2.5f, gap = ps * 0.5f;
    float bx = vw - m - segs * (bw + gap), by = m + big * 7 + ps * 2;
    float frac = std::clamp(std::fabs(me.speed) / NITRO_SPEED, 0.0f, 1.0f);
    for (int i = 0; i < segs; ++i) {
        float t = float(i) / (segs - 1);
        if (i < int(frac * segs + 0.5f)) glColor4f(std::min(1.0f, 2 * t), std::min(1.0f, 2 * (1 - t)), 0.1f, 1.0f);
        else glColor4f(0, 0, 0, 0.3f);
        float x0 = bx + i * (bw + gap);
        rect(x0, by, x0 + bw, by + bh);
    }

    // ---- nitro gauge ----
    const int nsegs = 12;
    float nbw = ps * 2.2f;
    for (int i = 0; i < nsegs; ++i) {
        bool lit = i < int(me.nitro * nsegs + 0.5f);
        if (lit) {
            if (me.nitroOn && (tick / 4) % 2) glColor4f(1, 1, 1, 1);
            else glColor4f(0.25f, 0.8f, 1.0f, 1.0f);
        } else glColor4f(0, 0, 0, 0.3f);
        float x0 = m + i * (nbw + gap);
        rect(x0, m, x0 + nbw, m + bh);
    }
    drawText("NITRO", m, m + bh + ps * 1.5f, ps, 0.5f, 0.9f, 1.0f);

    // ---- warnings / race messages ----
    bool offroad = std::fabs(me.x - roadCenter(me.z)) > ROAD_HALF;
    if (offroad && state == Racing && (tick / 15) % 2) {
        float s = ps * 2.0f;
        drawText("OFF ROAD", (vw - textWidth("OFF ROAD", s)) * 0.5f, vh * 0.30f, s, 1.0f, 0.55f, 0.1f);
    }
    if (state == Countdown) {
        snprintf(buf, sizeof buf, "%d", 3 - tick / 60);
        float s = ps * 8.0f;
        drawText(buf, (vw - textWidth(buf, s)) * 0.5f, vh * 0.55f, s, 1.0f, 0.85f, 0.2f);
        const char *hint = playerNum == 2 ? "W A S D - SPACE NITRO" : "ARROWS - ENTER NITRO";
        float hs = ps * 1.4f;
        drawText(hint, (vw - textWidth(hint, hs)) * 0.5f, vh * 0.40f, hs, 1, 1, 1);
    } else if (state == Racing && raceTick < 60) {
        float s = ps * 8.0f;
        drawText("GO!", (vw - textWidth("GO!", s)) * 0.5f, vh * 0.55f, s, 0.3f, 1.0f, 0.4f);
    }
    if (state == Finished) {
        const Car &w = winner == 1 ? p1 : p2;
        snprintf(buf, sizeof buf, "P%d WINS!", winner);
        float s = ps * 4.5f;
        glColor4f(0, 0, 0, 0.45f);
        rect(0, vh * 0.42f, vw, vh * 0.42f + s * 7 + ps * 14);
        drawText(buf, (vw - textWidth(buf, s)) * 0.5f, vh * 0.42f + ps * 10, s,
                 std::min(1.0f, w.r + 0.3f), std::min(1.0f, w.g + 0.3f), std::min(1.0f, w.b + 0.3f));
        const char *rs = "PRESS R TO RESTART";
        drawText(rs, (vw - textWidth(rs, ps * 1.4f)) * 0.5f, vh * 0.42f + ps * 2, ps * 1.4f, 1, 1, 1);
    }
}

// ===========================================================================
//  MINIMAP (top-right): whole track, both players, traffic
// ===========================================================================
void GLWidget::drawMinimap(int W, int H, float dpr)
{
    const float mw = 150.0f * dpr;
    const float mh = std::min(320.0f * dpr, H * 0.55f);
    const float margin = 14.0f * dpr;
    const float x0 = W - margin - mw, y0 = H - margin - mh;

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
    const float kx = (mw - 2 * pad) / 80.0f;                  // +-40 world units across
    const float ky = (top - bottom) / (TRACK_LEN + 120.0f);
    auto mx = [&](float wx) { return x0 + mw * 0.5f + wx * kx; };
    auto my = [&](float wz) { return std::clamp(bottom + (-wz) * ky, bottom, top); };

    // road ribbon
    float rw = std::max(2.5f * dpr, ROAD_HALF * kx);
    glColor4f(0.60f, 0.60f, 0.66f, 1.0f);
    glBegin(GL_QUAD_STRIP);
    for (float d = 0; d <= TRACK_LEN + 40.0f; d += 20.0f) {
        float c = roadCenter(-d);
        glVertex2f(mx(c) - rw, my(-d)); glVertex2f(mx(c) + rw, my(-d));
    }
    glEnd();
    // start line
    glColor4f(1, 1, 1, 1);
    rect(mx(0) - rw - 2 * dpr, my(0) - dpr, mx(0) + rw + 2 * dpr, my(0) + dpr);
    // finish checker
    float fy = my(-TRACK_LEN), sq = 2.0f * dpr;
    for (int i = 0; i < 6; ++i)
        for (int row = 0; row < 2; ++row) {
            if ((i + row) % 2) glColor4f(0, 0, 0, 1); else glColor4f(1, 1, 1, 1);
            float xx = mx(0) - 3 * sq + i * sq, yy = fy + row * sq;
            rect(xx, yy, xx + sq, yy + sq);
        }
    // temple icon
    float ty = my(-TRACK_LEN - 110.0f);
    glColor4f(0.95f, 0.78f, 0.30f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(mx(0) - 8 * dpr, ty - 5 * dpr); glVertex2f(mx(0) + 8 * dpr, ty - 5 * dpr); glVertex2f(mx(0), ty + 5 * dpr);
    glEnd();

    // traffic dots
    glColor4f(0.85f, 0.85f, 0.85f, 0.75f);
    for (const Traffic &t : traffic) {
        if (t.parked) continue;
        float px = mx(roadCenter(t.z) + t.lane), py = my(t.z), s = 1.5f * dpr;
        rect(px - s, py - s, px + s, py + s);
    }
    // players
    auto dot = [&](const Car &c, const char *lbl) {
        float px = mx(c.x), py = my(c.z), s = 4.0f * dpr, o = 1.5f * dpr;
        glColor4f(1, 1, 1, 1);
        rect(px - s - o, py - s - o, px + s + o, py + s + o);
        glColor4f(c.r, c.g, c.b, 1);
        rect(px - s, py - s, px + s, py + s);
        drawText(lbl, px + s + 4 * dpr, py - 3.5f * ps, ps, std::min(1.0f, c.r + 0.3f),
                 std::min(1.0f, c.g + 0.3f), std::min(1.0f, c.b + 0.3f));
    };
    dot(p2, "P2");
    dot(p1, "P1");
}
