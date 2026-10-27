#include "carmodel.h"

#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#include <algorithm>
#include <cmath>
#include <vector>

namespace carmodel {
namespace {

const float PI = 3.14159265f;
const float ARCH_R = 0.47f;   // wheel-arch opening radius

struct V3 { float x, y, z; };
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(V3 a)
{
    float l = std::sqrt(dot(a, a));
    return l > 1e-8f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 1, 0};
}

inline float sq(float v) { return v * v; }
inline float gauss(float u, float c, float w) { float d = (u - c) / w; return std::exp(-d * d); }

// ---------------------------------------------------------------------------
//  Profile curves along the car (u runs from NOSE_Z to TAIL_Z)
// ---------------------------------------------------------------------------

// Half width seen from above: rounded nose and tail, flared fenders over the
// wheels and a slightly pinched waist at the doors ("coke-bottle" shape).
float halfWidth(float u)
{
    float w = 1.00f;
    if (u < -1.85f) w -= 0.17f * sq((-1.85f - u) / 0.40f);
    if (u >  1.95f) w -= 0.07f * sq((u - 1.95f) / 0.30f);
    w += 0.13f * gauss(u, WHEEL_Z_FRONT, 0.55f);
    w += 0.16f * gauss(u, WHEEL_Z_REAR, 0.62f);
    w -= 0.03f * gauss(u, -0.10f, 0.45f);
    return w;
}

// Centreline height of the hood / doors / rear deck (the cabin sits on top).
float topLine(float u)
{
    if (u < -0.90f) {                              // hood curving down into a rounded nose
        float t = (-0.90f - u) / 1.35f;
        return 0.46f + 0.36f * std::pow(std::max(0.0f, 1.0f - t * t), 0.65f);
    }
    if (u < 1.05f) return 0.82f + 0.07f * (u + 0.90f) / 1.95f;   // wedge-shaped beltline
    float t = (u - 1.05f) / 1.20f;                               // rear deck, kicks down at the tail
    return 0.89f + 0.02f * t - 0.12f * t * t * t;
}

// The "shoulder" (widest point of the section) rises over both wheels.
float shoulderLine(float u)
{
    float h = 0.62f + 0.11f * gauss(u, WHEEL_Z_FRONT, 0.50f) + 0.12f * gauss(u, WHEEL_Z_REAR, 0.55f);
    return std::min(h, topLine(u) - 0.07f);
}

// Bottom edge of the body side; it jumps up into a semicircle over each wheel.
float sillLine(float u)
{
    float y = 0.20f;
    for (float wc : {WHEEL_Z_FRONT, WHEEL_Z_REAR}) {
        float d = u - wc;
        if (std::fabs(d) < ARCH_R) y = std::max(y, WHEEL_R + std::sqrt(ARCH_R * ARCH_R - d * d));
    }
    return std::min(y, shoulderLine(u) - 0.03f);
}

// Cross-section parameter s: 0 = left sill, 0.5 = centre line, 1 = right sill.
// side = -1 / +1, q = 0 at the sill .. 1 at the centre line.
inline void splitS(float s, float &side, float &q)
{
    if (s < 0.5f) { side = -1.0f; q = s * 2.0f; }
    else          { side =  1.0f; q = (1.0f - s) * 2.0f; }
}

// Point on the lower body (everything below the glass).
V3 bodyPoint(float u, float s)
{
    float side, q;
    splitS(s, side, q);
    const float W = halfWidth(u), yt = topLine(u), yh = shoulderLine(u), ys = sillLine(u);
    const float QS = 0.28f;                        // part of the section used by the side panel
    float x, y;
    if (q < QS) {                                  // side panel: tucks in slightly towards the sill
        float t = q / QS;
        x = W * (0.93f + 0.07f * std::sin(t * PI * 0.5f));
        y = ys + (yh - ys) * t;
    } else {                                       // rounded top: superellipse from shoulder to centre
        float a = (q - QS) / (1.0f - QS) * PI * 0.5f;
        // max(): cos(90deg) is a tiny negative number in float, and pow(negative) = NaN
        x = W * std::pow(std::max(0.0f, std::cos(a)), 2.0f / 2.8f);
        y = yh + (yt - yh) * std::pow(std::sin(a), 2.0f / 2.8f);
        // fender crests: the edges over the wheels sit higher than the hood/deck centre
        float crest = 0.06f * gauss(u, WHEEL_Z_FRONT, 0.42f) + 0.05f * gauss(u, WHEEL_Z_REAR, 0.50f);
        y += crest * sq(std::sin(PI * std::pow(x / W, 1.6f)));
    }
    return {side * x, y, u};
}

// ---------------------------------------------------------------------------
//  Greenhouse (glass cabin)
// ---------------------------------------------------------------------------
const float CAB_U0 = -0.92f;      // windscreen base
const float CAB_U1 =  1.10f;      // rear-glass base
const float CAB_EDGE_Q = 0.58f;   // where on the body section the glass meets the paint

float roofLine(float u)
{
    const float ROOF = 1.27f, UW = -0.10f, UR = 0.40f;
    float b0 = topLine(CAB_U0) - 0.04f, b1 = topLine(CAB_U1) - 0.04f;
    if (u <= UW) {                                  // raked windscreen
        float t = std::clamp((u - CAB_U0) / (UW - CAB_U0), 0.0f, 1.0f);
        return b0 + (ROOF - b0) * std::sin(t * PI * 0.5f);
    }
    if (u <= UR) {                                  // roof
        float t = (u - UW) / (UR - UW);
        return ROOF - 0.025f * t * t;
    }
    float t = std::clamp((u - UR) / (CAB_U1 - UR), 0.0f, 1.0f);   // fastback rear glass
    return b1 + (ROOF - 0.025f - b1) * (1.0f - std::pow(t, 1.3f));
}

V3 cabinPoint(float u, float s)
{
    float side, q;
    splitS(s, side, q);
    float uc = std::clamp(u, CAB_U0, CAB_U1);
    V3 e = bodyPoint(uc, CAB_EDGE_Q * 0.5f);       // left-side point where glass meets paint
    float xb = -e.x - 0.01f, yb = e.y - 0.02f;     // tucked just under the paint
    float h = std::max(0.0f, roofLine(uc) - yb);
    float mid = 0.5f * (CAB_U0 + CAB_U1), half = 0.5f * (CAB_U1 - CAB_U0);
    float taper = 1.0f - 0.22f * std::pow(std::fabs(uc - mid) / half, 3.0f);   // narrower at both ends
    float xt = xb * 0.78f * taper;                  // roof narrower than the base (tumblehome)
    const float QS = 0.45f;
    float x, y;
    if (q < QS) {                                   // side window
        float t = q / QS;
        x = xb + (xt - xb) * t;
        y = yb + h * 0.72f * t;
    } else {                                        // roof / windscreen / rear glass
        float a = (q - QS) / (1.0f - QS) * PI * 0.5f;
        x = xt * std::pow(std::max(0.0f, std::cos(a)), 2.0f / 3.2f);
        y = yb + h * 0.72f + h * 0.28f * std::pow(std::sin(a), 2.0f / 3.2f);
    }
    return {side * x, y, u};
}

// ---------------------------------------------------------------------------
//  Mesh emission
// ---------------------------------------------------------------------------
typedef V3 (*SurfFn)(float, float);

// Surface normal by finite differences, flipped to point away from the car's core.
V3 surfNormal(SurfFn f, float u, float s, float ycen)
{
    const float eu = 0.0004f, es = 0.002f;
    V3 du = f(u + eu, s) - f(u - eu, s);
    V3 ds = f(u, std::min(1.0f, s + es)) - f(u, std::max(0.0f, s - es));
    V3 n = normalize(cross(du, ds));
    V3 p = f(u, s);
    if (dot(n, V3{p.x, p.y - ycen, 0.5f * p.z}) < 0.0f) n = {-n.x, -n.y, -n.z};
    return n;
}

void emit(SurfFn f, float u, float s, float offset, float ycen)
{
    V3 n = surfNormal(f, u, s, ycen), p = f(u, s);
    glNormal3f(n.x, n.y, n.z);
    glVertex3f(p.x + n.x * offset, p.y + n.y * offset, p.z + n.z * offset);
}

// Sweep the cross-section through the given stations along the car.
void loft(SurfFn f, const std::vector<float> &us, int ns, float ycen)
{
    for (size_t i = 0; i + 1 < us.size(); ++i) {
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= ns; ++j) {
            float s = float(j) / float(ns);
            emit(f, us[i], s, 0.0f, ycen);
            emit(f, us[i + 1], s, 0.0f, ycen);
        }
        glEnd();
    }
}

// Rectangular patch in (u, s) parameter space, lifted `offset` off the surface.
void surface(SurfFn f, float u0, float u1, float s0, float s1, int nu, int ns, float offset, float ycen)
{
    for (int i = 0; i < nu; ++i) {
        float ua = u0 + (u1 - u0) * float(i) / float(nu);
        float ub = u0 + (u1 - u0) * float(i + 1) / float(nu);
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= ns; ++j) {
            float s = s0 + (s1 - s0) * float(j) / float(ns);
            emit(f, ua, s, offset, ycen);
            emit(f, ub, s, offset, ycen);
        }
        glEnd();
    }
}

// Flat end face (front bumper / rear fascia) closing the loft.
void cap(float u, float nz)
{
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, 0, nz);
    glVertex3f(0, 0.5f * (sillLine(u) + topLine(u)), u);
    for (int j = 0; j <= 48; ++j) { V3 p = bodyPoint(u, float(j) / 48.0f); glVertex3f(p.x, p.y, p.z); }
    V3 p0 = bodyPoint(u, 0.0f);
    glVertex3f(p0.x, p0.y, p0.z);
    glEnd();
}

void box(float cx, float cy, float cz, float hx, float hy, float hz)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  glVertex3f(cx - hx, cy - hy, cz + hz); glVertex3f(cx + hx, cy - hy, cz + hz); glVertex3f(cx + hx, cy + hy, cz + hz); glVertex3f(cx - hx, cy + hy, cz + hz);
    glNormal3f(0, 0, -1); glVertex3f(cx + hx, cy - hy, cz - hz); glVertex3f(cx - hx, cy - hy, cz - hz); glVertex3f(cx - hx, cy + hy, cz - hz); glVertex3f(cx + hx, cy + hy, cz - hz);
    glNormal3f(0, 1, 0);  glVertex3f(cx - hx, cy + hy, cz + hz); glVertex3f(cx + hx, cy + hy, cz + hz); glVertex3f(cx + hx, cy + hy, cz - hz); glVertex3f(cx - hx, cy + hy, cz - hz);
    glNormal3f(0, -1, 0); glVertex3f(cx - hx, cy - hy, cz - hz); glVertex3f(cx + hx, cy - hy, cz - hz); glVertex3f(cx + hx, cy - hy, cz + hz); glVertex3f(cx - hx, cy - hy, cz + hz);
    glNormal3f(1, 0, 0);  glVertex3f(cx + hx, cy - hy, cz + hz); glVertex3f(cx + hx, cy - hy, cz - hz); glVertex3f(cx + hx, cy + hy, cz - hz); glVertex3f(cx + hx, cy + hy, cz + hz);
    glNormal3f(-1, 0, 0); glVertex3f(cx - hx, cy - hy, cz - hz); glVertex3f(cx - hx, cy - hy, cz + hz); glVertex3f(cx - hx, cy + hy, cz + hz); glVertex3f(cx - hx, cy + hy, cz - hz);
    glEnd();
}

// Flat quad facing +Z or -Z (lamps and grilles on the end faces).
void faceQuad(float x0, float x1, float y0, float y1, float z, float nz)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, nz);
    glVertex3f(x0, y0, z); glVertex3f(x1, y0, z); glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
    glEnd();
}

// Soft round glow facing +Z / -Z, used for lamp halos (additive blending).
void halo(float x, float y, float z, float rad, float r, float g, float b, float a)
{
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(r, g, b, a);
    glVertex3f(x, y, z);
    glColor4f(r, g, b, 0.0f);
    for (int i = 0; i <= 20; ++i) {
        float t = float(i) * 2.0f * PI / 20.0f;
        glVertex3f(x + std::cos(t) * rad, y + std::sin(t) * rad * 0.6f, z);
    }
    glEnd();
}

// Dark wheel well: an inner wall plus the arch "tunnel" so you can't see into the shell.
void wheelWell(float wc, float side)
{
    const float xi = side * (WHEEL_X - 0.20f);
    const int N = 24;
    glColor3f(0.03f, 0.03f, 0.035f);
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(side, 0, 0);
    glVertex3f(xi, 0.35f, wc);
    for (int k = 0; k <= N; ++k) {
        float z = wc - ARCH_R + 2.0f * ARCH_R * float(k) / float(N);
        glVertex3f(xi, sillLine(z), z);
    }
    glVertex3f(xi, 0.20f, wc + ARCH_R);
    glVertex3f(xi, 0.20f, wc - ARCH_R);
    glVertex3f(xi, sillLine(wc - ARCH_R), wc - ARCH_R);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (int k = 0; k <= N; ++k) {
        float z = wc - ARCH_R + 2.0f * ARCH_R * float(k) / float(N);
        float y = sillLine(z);
        V3 n = normalize(V3{0, WHEEL_R - y, wc - z});   // faces the tyre
        glNormal3f(n.x, n.y, n.z);
        glVertex3f(xi, y, z);
        glVertex3f(side * halfWidth(z), y, z);
    }
    glEnd();
}

GLfloat g_savedFog[4] = {0, 0, 0, 1};

// Lamp lenses, recorded by build(): they sit on the curved body, which is slow to work
// out, and every car on screen needs them every frame
GLuint g_frontLamps = 0;   // headlights + running lights, with their colours
GLuint g_tailLamps  = 0;   // tail lights, no colour (it changes with braking)

} // namespace

// ===========================================================================
//  Public API
// ===========================================================================
float bodyTop(float u) { return topLine(u); }

void bodyPatch(float u0, float u1, float s0, float s1, float offset, int nu, int ns)
{
    surface(bodyPoint, u0, u1, s0, s1, nu, ns, offset, 0.45f);
}

Lists build()
{
    Lists L;

    // Stations along the car: every 2.5 cm, plus exact points on both sides of
    // each wheel-arch edge so the arch cut-outs come out crisp.
    std::vector<float> us;
    const float archEdges[4] = {WHEEL_Z_FRONT - ARCH_R, WHEEL_Z_FRONT + ARCH_R,
                                WHEEL_Z_REAR - ARCH_R,  WHEEL_Z_REAR + ARCH_R};
    for (int i = 0; ; ++i) {
        float u = NOSE_Z + 0.025f * float(i);
        if (u > TAIL_Z - 1e-4f) break;
        bool nearEdge = false;
        for (float e : archEdges) if (std::fabs(u - e) < 0.003f) nearEdge = true;
        if (!nearEdge) us.push_back(u);
    }
    us.push_back(TAIL_Z);
    for (float e : archEdges) { us.push_back(e - 0.0005f); us.push_back(e + 0.0005f); }
    std::sort(us.begin(), us.end());

    // ---- painted body (colour supplied by the caller) ----
    L.body = glGenLists(1);
    glNewList(L.body, GL_COMPILE);
    loft(bodyPoint, us, 48, 0.45f);
    cap(NOSE_Z, -1.0f);
    cap(TAIL_Z,  1.0f);
    // body-coloured A/C pillars and roof panel laid over the glass
    surface(cabinPoint, CAB_U0, CAB_U1, 0.20f, 0.26f, 40, 2, 0.006f, 0.6f);
    surface(cabinPoint, CAB_U0, CAB_U1, 0.74f, 0.80f, 40, 2, 0.006f, 0.6f);
    surface(cabinPoint, -0.04f, 0.66f, 0.26f, 0.74f, 14, 12, 0.006f, 0.6f);
    // door mirrors
    for (float side : {-1.0f, 1.0f}) {
        V3 e = bodyPoint(-0.70f, CAB_EDGE_Q * 0.5f);
        box(side * (-e.x + 0.10f), e.y + 0.10f, -0.70f, 0.07f, 0.045f, 0.05f);
        box(side * (-e.x + 0.03f), e.y + 0.06f, -0.68f, 0.04f, 0.012f, 0.02f);
    }
    glEndList();

    // ---- glass ----
    L.glass = glGenLists(1);
    glNewList(L.glass, GL_COMPILE);
    surface(cabinPoint, CAB_U0, CAB_U1, 0.0f, 1.0f, 64, 24, 0.0f, 0.6f);
    glEndList();

    // ---- trim: everything with its own fixed colour ----
    GLUquadric *q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    L.trim = glGenLists(1);
    glNewList(L.trim, GL_COMPILE);

    for (float side : {-1.0f, 1.0f}) {
        wheelWell(WHEEL_Z_FRONT, side);
        wheelWell(WHEEL_Z_REAR, side);
    }

    // underbody between the wheel wells
    glColor3f(0.03f, 0.03f, 0.035f);
    glBegin(GL_QUADS);
    glNormal3f(0, -1, 0);
    glVertex3f(-0.76f, 0.205f, NOSE_Z + 0.05f); glVertex3f(0.76f, 0.205f, NOSE_Z + 0.05f);
    glVertex3f( 0.76f, 0.205f, TAIL_Z - 0.05f); glVertex3f(-0.76f, 0.205f, TAIL_Z - 0.05f);
    glEnd();

    // black side skirts, door shut-lines, handles, side intakes, hood vents
    glColor3f(0.04f, 0.04f, 0.045f);
    bodyPatch(-0.90f, 0.90f, 0.000f, 0.030f, 0.004f, 24, 1);
    bodyPatch(-0.90f, 0.90f, 0.970f, 1.000f, 0.004f, 24, 1);
    bodyPatch( 0.62f, 0.88f, 0.050f, 0.120f, 0.004f, 6, 3);
    bodyPatch( 0.62f, 0.88f, 0.880f, 0.950f, 0.004f, 6, 3);
    bodyPatch(-1.58f, -1.28f, 0.400f, 0.440f, 0.004f, 4, 2);
    bodyPatch(-1.58f, -1.28f, 0.560f, 0.600f, 0.004f, 4, 2);
    glColor3f(0.07f, 0.07f, 0.08f);
    for (float u : {-0.82f, 0.50f}) {
        bodyPatch(u, u + 0.010f, 0.030f, 0.275f, 0.004f, 1, 8);
        bodyPatch(u, u + 0.010f, 0.725f, 0.970f, 0.004f, 1, 8);
    }
    glColor3f(0.55f, 0.56f, 0.60f);
    bodyPatch(0.28f, 0.40f, 0.205f, 0.215f, 0.005f, 2, 1);
    bodyPatch(0.28f, 0.40f, 0.785f, 0.795f, 0.005f, 2, 1);

    // lamp housings (the lit lenses are drawn per frame by drawLamps)
    glColor3f(0.10f, 0.11f, 0.12f);
    bodyPatch(-2.19f, -1.85f, 0.145f, 0.255f, 0.005f, 6, 3);
    bodyPatch(-2.19f, -1.85f, 0.745f, 0.855f, 0.005f, 6, 3);
    glColor3f(0.16f, 0.02f, 0.02f);
    bodyPatch(2.00f, TAIL_Z, 0.115f, 0.195f, 0.005f, 5, 2);
    bodyPatch(2.00f, TAIL_Z, 0.805f, 0.885f, 0.005f, 5, 2);
    faceQuad(-0.86f, 0.86f, 0.615f, 0.705f, TAIL_Z + 0.004f, 1.0f);

    // front: grille, side intakes, splitter
    glColor3f(0.03f, 0.03f, 0.035f);
    faceQuad(-0.52f, 0.52f, 0.23f, 0.38f, NOSE_Z - 0.004f, -1.0f);
    faceQuad(-0.78f, -0.58f, 0.23f, 0.33f, NOSE_Z - 0.004f, -1.0f);
    faceQuad( 0.58f,  0.78f, 0.23f, 0.33f, NOSE_Z - 0.004f, -1.0f);
    glColor3f(0.30f, 0.31f, 0.33f);
    for (float y : {0.27f, 0.31f, 0.35f}) faceQuad(-0.50f, 0.50f, y, y + 0.008f, NOSE_Z - 0.006f, -1.0f);
    glColor3f(0.05f, 0.05f, 0.06f);
    box(0.0f, 0.19f, NOSE_Z + 0.06f, 0.80f, 0.015f, 0.12f);

    // rear: diffuser with fins, twin exhausts, number plate
    glColor3f(0.04f, 0.04f, 0.045f);
    box(0.0f, 0.27f, TAIL_Z - 0.04f, 0.80f, 0.07f, 0.06f);
    for (float fx : {-0.30f, -0.10f, 0.10f, 0.30f}) box(fx, 0.25f, TAIL_Z - 0.02f, 0.012f, 0.05f, 0.07f);
    for (float ex : {-0.48f, 0.48f}) {
        glPushMatrix();
        glTranslatef(ex, 0.28f, TAIL_Z - 0.10f);
        glColor3f(0.72f, 0.73f, 0.76f);
        gluCylinder(q, 0.058, 0.058, 0.17, 14, 1);
        glTranslatef(0, 0, 0.16f);
        glColor3f(0.02f, 0.02f, 0.02f);
        gluDisk(q, 0.0, 0.048, 14, 1);
        glPopMatrix();
    }
    glColor3f(0.93f, 0.93f, 0.88f);
    faceQuad(-0.26f, 0.26f, 0.42f, 0.53f, TAIL_Z + 0.004f, 1.0f);
    glColor3f(0.15f, 0.15f, 0.18f);
    faceQuad(-0.22f, 0.22f, 0.455f, 0.495f, TAIL_Z + 0.006f, 1.0f);

    glEndList();
    gluDeleteQuadric(q);

    g_frontLamps = glGenLists(1);
    glNewList(g_frontLamps, GL_COMPILE);
    glColor3f(1.0f, 0.97f, 0.88f);                         // headlights
    bodyPatch(-2.16f, -1.88f, 0.155f, 0.245f, 0.010f, 5, 3);
    bodyPatch(-2.16f, -1.88f, 0.755f, 0.845f, 0.010f, 5, 3);
    glColor3f(0.70f, 0.88f, 1.0f);                         // daytime running light strips
    bodyPatch(-2.17f, -1.96f, 0.100f, 0.116f, 0.010f, 4, 1);
    bodyPatch(-2.17f, -1.96f, 0.884f, 0.900f, 0.010f, 4, 1);
    glEndList();

    g_tailLamps = glGenLists(1);
    glNewList(g_tailLamps, GL_COMPILE);
    faceQuad(-0.80f, 0.80f, 0.635f, 0.685f, TAIL_Z + 0.008f, 1.0f);
    bodyPatch(2.03f, 2.24f, 0.125f, 0.185f, 0.010f, 4, 2);
    bodyPatch(2.03f, 2.24f, 0.815f, 0.875f, 0.010f, 4, 2);
    glEndList();
    return L;
}

void destroy(Lists &l)
{
    for (GLuint *id : {&l.body, &l.glass, &l.trim, &g_frontLamps, &g_tailLamps}) {
        if (*id) glDeleteLists(*id, 1);
        *id = 0;
    }
}

GLuint buildEnvMap(const EnvColors &e)
{
    // Sphere map: texel (u,v) stores what a mirror facing that way would reflect.
    const int N = 128;
    std::vector<unsigned char> img(N * N * 3);
    const V3 sun = normalize(V3{0.28f, e.sunHeight, -1.0f});
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N * 2.0f - 1.0f, v = (y + 0.5f) / N * 2.0f - 1.0f;
            float d = u * u + v * v;
            if (d > 1.0f) { float k = 1.0f / std::sqrt(d); u *= k; v *= k; d = 1.0f; }
            float nz = std::sqrt(std::max(0.0f, 1.0f - d));
            V3 R = {2.0f * nz * u, 2.0f * nz * v, 2.0f * nz * nz - 1.0f};
            float col[3];
            if (R.y >= 0.0f) {                       // sky: horizon -> mid -> top
                float t = std::min(1.0f, R.y / 0.30f), t2 = std::clamp((R.y - 0.30f) / 0.70f, 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k)
                    col[k] = e.horizon[k] + (e.mid[k] - e.horizon[k]) * t + (e.top[k] - e.mid[k]) * t2;
            } else {                                 // ground, darker straight down
                float t = std::min(1.0f, -R.y / 0.40f);
                for (int k = 0; k < 3; ++k) col[k] = e.ground[k] * (1.0f - 0.65f * t);
            }
            float band = std::exp(-sq(R.y / 0.06f));  // bright horizon line
            float cs = std::max(0.0f, dot(R, sun));   // the sun and its glare
            float spot = std::pow(cs, 200.0f) * 1.5f, glare = std::pow(cs, 12.0f) * 0.4f;
            for (int k = 0; k < 3; ++k) col[k] += 0.3f * band * e.horizon[k] + (spot + glare) * e.sun[k];
            float r = col[0], g = col[1], b = col[2];
            int i = (y * N + x) * 3;
            img[i]     = (unsigned char)std::clamp(int(r * 255.0f), 0, 255);
            img[i + 1] = (unsigned char)std::clamp(int(g * 255.0f), 0, 255);
            img[i + 2] = (unsigned char)std::clamp(int(b * 255.0f), 0, 255);
        }
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, N, N, 0, GL_RGB, GL_UNSIGNED_BYTE, img.data());
    return id;
}

void beginGlow()
{
    glGetFloatv(GL_FOG_COLOR, g_savedFog);
    const GLfloat black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glFogfv(GL_FOG_COLOR, black);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
}

void endGlow()
{
    glFogfv(GL_FOG_COLOR, g_savedFog);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

void reflectionPass(GLuint list, GLuint envTex, float strength)
{
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, envTex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    beginGlow();
    glColor4f(1.0f, 1.0f, 1.0f, strength);
    glCallList(list);
    endGlow();
    glPopAttrib();
}

void drawWing()
{
    // Rear wing on two struts; the wing itself uses the caller's paint colour.
    float deck = topLine(2.02f);
    glPushMatrix();
    glTranslatef(0.0f, deck + 0.27f, 2.04f);
    glRotatef(6.0f, 1, 0, 0);
    box(0.0f, 0.0f, 0.0f, 0.94f, 0.022f, 0.17f);
    glColor3f(0.05f, 0.05f, 0.06f);
    box(-0.94f, -0.02f, 0.0f, 0.010f, 0.055f, 0.18f);
    box( 0.94f, -0.02f, 0.0f, 0.010f, 0.055f, 0.18f);
    glPopMatrix();
    box(-0.52f, deck + 0.13f, 2.00f, 0.022f, 0.14f, 0.05f);
    box( 0.52f, deck + 0.13f, 2.00f, 0.022f, 0.14f, 0.05f);
}

void drawLamps(float brake)
{
    // Unlit (emissive-looking) lenses; caller disables lighting.
    glCallList(g_frontLamps);                              // headlights + running lights

    float k = 0.45f + 0.55f * brake;                       // tail lights: brighter when braking
    glColor3f(k, 0.04f + 0.12f * brake, 0.03f);
    glCallList(g_tailLamps);

    beginGlow();
    for (float sx : {-0.72f, 0.72f}) halo(sx, 0.59f, NOSE_Z - 0.04f, 0.38f, 1.0f, 0.95f, 0.80f, 0.45f);
    for (float sx : {-0.62f, 0.62f}) halo(sx, 0.66f, TAIL_Z + 0.04f, 0.30f + 0.25f * brake, 1.0f, 0.10f, 0.05f, 0.25f + 0.45f * brake);
    endGlow();
}

} // namespace carmodel
