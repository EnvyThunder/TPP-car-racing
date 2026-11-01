// OpenGL 1 on OpenGL ES 2 / WebGL: see gl1shim.h.
#include "gl1shim.h"

#include <QOpenGLFunctions>

#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

struct GLUquadric { bool smooth = true; };

namespace sgl {
namespace {

struct V { float p[3]; float n[3]; float c[4]; float t[2]; };
enum Prim { PTris, PLines, PPoints };

struct State {
    bool lighting = false, light0 = false, colorMat = false, fog = false, tex2d = false, texGenS = false, texGenT = false;
    bool depthTest = false, blend = false;
    float lAmb[4] = {0, 0, 0, 1}, lDif[4] = {1, 1, 1, 1}, lSpec[4] = {1, 1, 1, 1}, lPos[4] = {0, 0, 1, 0};   // (eye space)
    float gAmb[4] = {0.2f, 0.2f, 0.2f, 1};
    float mAmb[4] = {0.2f, 0.2f, 0.2f, 1}, mDif[4] = {0.8f, 0.8f, 0.8f, 1}, mSpec[4] = {0, 0, 0, 1};
    float shininess = 0.0f;
    float fogStart = 0.0f, fogEnd = 1.0f, fogCol[4] = {0, 0, 0, 0};
    GLuint tex = 0;
    float lineW = 1.0f, pointS = 1.0f;
};

struct Batch {
    Prim prim = PTris;
    std::vector<V> v;                // (kept for wide lines; else freed once uploaded)
    bool bakedColor = true;          // false: the list set no colour - the current colour at call time
    GLuint vbo = 0;
    int count = 0;
};
struct Op {
    int batch = -1;                  // >= 0: draw that batch; else run fn
    std::function<void()> fn;
};
struct List {
    std::vector<Op> ops;
    std::vector<Batch> batches;
};

struct Attrib {
    State st;
    float color[4];
};

QOpenGLFunctions *f = nullptr;
QOpenGLShaderProgram *prog = nullptr;
GLuint streamVbo = 0;
bool desktopGL = true;
State st;
float curColor[4] = {1, 1, 1, 1}, curNormal[3] = {0, 0, 1}, curUV[2] = {0, 0};
std::vector<QMatrix4x4> mvStack(1), projStack(1);
bool modeProj = false;
GLint vp[4] = {0, 0, 1, 1};

bool inBegin = false;
GLenum beginMode = 0;
bool colorInBegin = false;
std::vector<V> cur, conv;

Prim pendPrim = PTris;
std::vector<V> pend;

std::unordered_map<GLuint, List> lists;
GLuint nextList = 1;
List *rec = nullptr;
bool recColorSet = false;
std::vector<Attrib> attribStack;
Stats stats;

QMatrix4x4 &mv() { return mvStack.back(); }
QMatrix4x4 &proj() { return projStack.back(); }
QMatrix4x4 &curMatrix() { return modeProj ? projStack.back() : mvStack.back(); }

const char *VS = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec3 a_pos;
attribute vec3 a_nrm;
attribute vec4 a_col;
attribute vec2 a_uv;
uniform mat4 u_mv;
uniform mat4 u_proj;
uniform mat3 u_nm;
uniform bool u_lit;
uniform vec4 u_lpos;
uniform vec4 u_lamb;
uniform vec4 u_ldif;
uniform vec4 u_lspec;
uniform vec4 u_gamb;
uniform bool u_cmat;
uniform vec4 u_mamb;
uniform vec4 u_mdif;
uniform vec4 u_mspec;
uniform float u_shin;
uniform bool u_useConst;
uniform vec4 u_const;
uniform bool u_texgen;
uniform float u_psize;
varying vec4 v_col;
varying vec2 v_uv;
varying float v_fogz;
void main()
{
    vec4 ep = u_mv * vec4(a_pos, 1.0);
    gl_Position = u_proj * ep;
    gl_PointSize = u_psize;
    vec4 col = u_useConst ? u_const : a_col;
    vec3 n = u_nm * a_nrm;
    float nlen = length(n);
    n = nlen > 0.0 ? n / nlen : vec3(0.0, 0.0, 1.0);
    if (u_lit) {
        vec4 amb = u_cmat ? col : u_mamb;
        vec4 dif = u_cmat ? col : u_mdif;
        vec3 L = u_lpos.w == 0.0 ? normalize(u_lpos.xyz) : normalize(u_lpos.xyz - ep.xyz);
        float nl = max(dot(n, L), 0.0);
        vec3 c = u_gamb.rgb * amb.rgb + u_lamb.rgb * amb.rgb + nl * u_ldif.rgb * dif.rgb;
        if (nl > 0.0) {
            vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
            float s = u_shin > 0.0 ? pow(max(dot(n, H), 0.0001), u_shin) : 1.0;
            c += s * u_lspec.rgb * u_mspec.rgb;
        }
        col = vec4(clamp(c, 0.0, 1.0), dif.a);              // (fixed-function OpenGL clamps the lit colour)
    }
    v_col = col;
    if (u_texgen) {
        vec3 r = reflect(normalize(ep.xyz), n);
        float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
        v_uv = vec2(r.x / m + 0.5, r.y / m + 0.5);
    } else {
        v_uv = a_uv;
    }
    v_fogz = abs(ep.z);
}
)";

const char *FS = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform bool u_tex;
uniform sampler2D u_samp;
uniform bool u_fog;
uniform float u_fs;
uniform float u_fe;
uniform vec4 u_fc;
varying vec4 v_col;
varying vec2 v_uv;
varying float v_fogz;
void main()
{
    vec4 c = v_col;
    if (u_tex) c *= texture2D(u_samp, v_uv);
    if (u_fog) {
        float k = clamp((u_fe - v_fogz) / max(u_fe - u_fs, 0.0001), 0.0, 1.0);
        c.rgb = mix(u_fc.rgb, c.rgb, k);
    }
    gl_FragColor = c;
}
)";

struct Loc {
    int mv, proj, nm, lit, lpos, lamb, ldif, lspec, gamb, cmat, mamb, mdif, mspec, shin, useConst, cnst, texgen, psize;
    int tex, samp, fog, fs, fe, fc;
} loc;

// a uniform is only sent when its value changed (a frame has over a thousand draws)
float ucache[64][16];
bool uset[64];
bool progBound = false;
bool same(int l, const float *v, int n)
{
    if (l < 0 || l >= 64) return l < 0;
    if (uset[l] && std::memcmp(ucache[l], v, size_t(n) * sizeof(float)) == 0) return true;
    std::memcpy(ucache[l], v, size_t(n) * sizeof(float));
    uset[l] = true;
    return false;
}
void u1i(int l, int v) { const float fv = float(v); if (!same(l, &fv, 1)) prog->setUniformValue(l, v); }
void u1f(int l, float v) { if (!same(l, &v, 1)) prog->setUniformValue(l, v); }
void u4(int l, const float *v) { if (!same(l, v, 4)) prog->setUniformValue(l, QVector4D(v[0], v[1], v[2], v[3])); }
void um4(int l, const QMatrix4x4 &m) { if (!same(l, m.constData(), 16)) prog->setUniformValue(l, m); }
void um3(int l, const QMatrix3x3 &m) { if (!same(l, m.constData(), 9)) prog->setUniformValue(l, m); }

// set every uniform for a draw with these matrices (constColor: the list's vertices take this colour)
void setUniforms(const QMatrix4x4 &m, const QMatrix4x4 &p, const float *constColor, bool flat2d)
{
    if (!progBound) { prog->bind(); progBound = true; }
    um4(loc.mv, m);
    um4(loc.proj, p);
    const bool lit = st.lighting && st.light0 && !flat2d;
    const bool tex = st.tex2d && st.tex != 0 && !flat2d;
    const bool texgen = tex && st.texGenS && st.texGenT;
    if (lit || texgen) um3(loc.nm, m.normalMatrix());    // (the inverse is only worked out when it is used)
    u1i(loc.lit, lit ? 1 : 0);
    if (lit) {
        u4(loc.lpos, st.lPos);
        u4(loc.lamb, st.lAmb);
        u4(loc.ldif, st.lDif);
        u4(loc.lspec, st.lSpec);
        u4(loc.gamb, st.gAmb);
        u1i(loc.cmat, st.colorMat ? 1 : 0);
        u4(loc.mamb, st.mAmb);
        u4(loc.mdif, st.mDif);
        u4(loc.mspec, st.mSpec);
        u1f(loc.shin, st.shininess);
    }
    u1i(loc.useConst, constColor ? 1 : 0);
    if (constColor) u4(loc.cnst, constColor);
    u1i(loc.tex, tex ? 1 : 0);
    u1i(loc.texgen, texgen ? 1 : 0);
    u1i(loc.samp, 0);
    u1f(loc.psize, st.pointS);
    const bool fog = st.fog && !flat2d;
    u1i(loc.fog, fog ? 1 : 0);
    if (fog) {
        u1f(loc.fs, st.fogStart);
        u1f(loc.fe, st.fogEnd);
        u4(loc.fc, st.fogCol);
    }
}

void pointers()
{
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void *>(offsetof(V, p)));
    f->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void *>(offsetof(V, n)));
    f->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void *>(offsetof(V, c)));
    f->glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void *>(offsetof(V, t)));
    for (int i = 0; i < 4; ++i) f->glEnableVertexAttribArray(GLuint(i));
}

GLenum glPrim(Prim p) { return p == PTris ? GL_TRIANGLES : p == PLines ? GL_LINES : GL_POINTS; }

// wide lines: each segment becomes a quad of the width in pixels, in clip space
std::vector<V> wideLines(const V *v, int n, const QMatrix4x4 &m, const QMatrix4x4 &p, const float *constColor)
{
    std::vector<V> out;
    const QMatrix4x4 mvp = p * m;
    const float hw = st.lineW * 0.5f, sx = 2.0f / float(std::max(1, vp[2])), sy = 2.0f / float(std::max(1, vp[3]));
    for (int i = 0; i + 1 < n; i += 2) {
        QVector4D a = mvp * QVector4D(v[i].p[0], v[i].p[1], v[i].p[2], 1.0f);
        QVector4D b = mvp * QVector4D(v[i + 1].p[0], v[i + 1].p[1], v[i + 1].p[2], 1.0f);
        if (a.w() <= 1e-5f || b.w() <= 1e-5f) continue;
        const QVector3D na = a.toVector3DAffine(), nb = b.toVector3DAffine();
        float dx = (nb.x() - na.x()) / sx, dy = (nb.y() - na.y()) / sy;      // (in pixels)
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) { dx = 1.0f; dy = 0.0f; } else { dx /= len; dy /= len; }
        const float ox = -dy * hw * sx, oy = dx * hw * sy;
        V q[4] = {v[i], v[i], v[i + 1], v[i + 1]};
        const QVector3D pts[4] = {na + QVector3D(ox, oy, 0), na - QVector3D(ox, oy, 0), nb - QVector3D(ox, oy, 0), nb + QVector3D(ox, oy, 0)};
        for (int k = 0; k < 4; ++k) {
            q[k].p[0] = pts[k].x(); q[k].p[1] = pts[k].y(); q[k].p[2] = pts[k].z();
            if (constColor) std::memcpy(q[k].c, constColor, sizeof q[k].c);
        }
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k : idx) out.push_back(q[k]);
    }
    return out;
}

void drawStream(Prim prim, const V *v, int n, const float *constColor)
{
    if (n <= 0) return;
    if (prim == PLines && st.lineW > 1.25f) {                // (WebGL only draws 1-pixel lines)
        std::vector<V> q = wideLines(v, n, mv(), proj(), constColor);
        if (q.empty()) return;
        const QMatrix4x4 id;
        setUniforms(id, id, nullptr, true);
        f->glBindBuffer(GL_ARRAY_BUFFER, streamVbo);
        f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(q.size() * sizeof(V)), q.data(), GL_STREAM_DRAW);
        pointers();
        f->glDrawArrays(GL_TRIANGLES, 0, GLsizei(q.size()));
        ++stats.draws; stats.vertices += int(q.size()); stats.streamed += int(q.size());
        return;
    }
    setUniforms(mv(), proj(), constColor, false);
    f->glBindBuffer(GL_ARRAY_BUFFER, streamVbo);
    f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(size_t(n) * sizeof(V)), v, GL_STREAM_DRAW);
    pointers();
    f->glDrawArrays(glPrim(prim), 0, n);
    ++stats.draws; stats.vertices += n; stats.streamed += n;
}

void drawBatch(const Batch &b)
{
    const float *cc = b.bakedColor ? nullptr : curColor;
    if (b.prim == PLines && st.lineW > 1.25f) { drawStream(PLines, b.v.data(), int(b.v.size()), cc); return; }
    setUniforms(mv(), proj(), cc, false);
    f->glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    pointers();
    f->glDrawArrays(glPrim(b.prim), 0, b.count);
    ++stats.draws; stats.vertices += b.count;
}

// the vertices of a glBegin / glEnd as plain triangles, lines or points
Prim convert(GLenum mode, const std::vector<V> &in, std::vector<V> &out)
{
    out.clear();
    const int n = int(in.size());
    switch (mode) {
    case GL_TRIANGLES:
        out.assign(in.begin(), in.begin() + (n / 3) * 3);
        return PTris;
    case GL_TRIANGLE_STRIP:
        for (int i = 0; i + 2 < n; ++i) {
            if (i % 2 == 0) { out.push_back(in[i]); out.push_back(in[i + 1]); out.push_back(in[i + 2]); }
            else { out.push_back(in[i + 1]); out.push_back(in[i]); out.push_back(in[i + 2]); }
        }
        return PTris;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (int i = 1; i + 1 < n; ++i) { out.push_back(in[0]); out.push_back(in[i]); out.push_back(in[i + 1]); }
        return PTris;
    case GL_QUADS:
        for (int i = 0; i + 3 < n; i += 4) {
            out.push_back(in[i]); out.push_back(in[i + 1]); out.push_back(in[i + 2]);
            out.push_back(in[i]); out.push_back(in[i + 2]); out.push_back(in[i + 3]);
        }
        return PTris;
    case GL_QUAD_STRIP:
        for (int i = 0; i + 3 < n; i += 2) {
            out.push_back(in[i]); out.push_back(in[i + 1]); out.push_back(in[i + 3]);
            out.push_back(in[i]); out.push_back(in[i + 3]); out.push_back(in[i + 2]);
        }
        return PTris;
    case GL_LINES:
        out.assign(in.begin(), in.begin() + (n / 2) * 2);
        return PLines;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        for (int i = 0; i + 1 < n; ++i) { out.push_back(in[i]); out.push_back(in[i + 1]); }
        if (mode == GL_LINE_LOOP && n > 2) { out.push_back(in[n - 1]); out.push_back(in[0]); }
        return PLines;
    default:
        out = in;
        return PPoints;
    }
}

void vertex(float x, float y, float z)
{
    V v;
    v.p[0] = x; v.p[1] = y; v.p[2] = z;
    std::memcpy(v.n, curNormal, sizeof v.n);
    std::memcpy(v.c, curColor, sizeof v.c);
    std::memcpy(v.t, curUV, sizeof v.t);
    cur.push_back(v);
}

// a state call while a list is being recorded: kept for the replay (and not run now)
#define RECORD(...)                                                   \
    if (rec) { rec->ops.push_back(Op{-1, [=]() { __VA_ARGS__; }}); return; }

} // namespace

// ---- Set-up and frames -------------------------------------------------------------------------

void init()
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    f = ctx->functions();
    desktopGL = !ctx->isOpenGLES();
    delete prog;
    prog = new QOpenGLShaderProgram;
    prog->addShaderFromSourceCode(QOpenGLShader::Vertex, VS);
    prog->addShaderFromSourceCode(QOpenGLShader::Fragment, FS);
    prog->bindAttributeLocation("a_pos", 0);
    prog->bindAttributeLocation("a_nrm", 1);
    prog->bindAttributeLocation("a_col", 2);
    prog->bindAttributeLocation("a_uv", 3);
    if (!prog->link()) qWarning("gl1shim: shader: %s", qPrintable(prog->log()));
    const char *names[] = {"u_mv", "u_proj", "u_nm", "u_lit", "u_lpos", "u_lamb", "u_ldif", "u_lspec", "u_gamb", "u_cmat",
                           "u_mamb", "u_mdif", "u_mspec", "u_shin", "u_useConst", "u_const", "u_texgen", "u_psize",
                           "u_tex", "u_samp", "u_fog", "u_fs", "u_fe", "u_fc"};
    int *uni = &loc.mv;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) uni[i] = prog->uniformLocation(names[i]);
    if (!streamVbo) f->glGenBuffers(1, &streamVbo);
    std::memset(uset, 0, sizeof uset);
    progBound = false;
    if (desktopGL) f->glEnable(0x8642);                  // (GL_VERTEX_PROGRAM_POINT_SIZE: the shader sets point sizes)
}

void flush()
{
    if (pend.empty()) return;
    drawStream(pendPrim, pend.data(), int(pend.size()), nullptr);
    pend.clear();
}

void endFrame()
{
    flush();
    progBound = false;                                   // (Qt draws the widget with its own program after us)
    std::memset(uset, 0, sizeof uset);
}

Stats frameStats()
{
    Stats s = stats;
    stats = Stats();
    return s;
}

// ---- Immediate mode ----------------------------------------------------------------------------

void begin(GLenum mode)
{
    inBegin = true;
    beginMode = mode;
    colorInBegin = false;
    cur.clear();
}

void end()
{
    inBegin = false;
    const Prim prim = convert(beginMode, cur, conv);
    if (conv.empty()) return;
    if (rec) {                                           // into the list being recorded
        const bool baked = recColorSet || colorInBegin;
        if (!rec->ops.empty() && rec->ops.back().batch >= 0) {
            Batch &last = rec->batches[size_t(rec->ops.back().batch)];
            if (last.prim == prim && last.bakedColor == baked) { last.v.insert(last.v.end(), conv.begin(), conv.end()); return; }
        }
        Batch b;
        b.prim = prim;
        b.v = conv;
        b.bakedColor = baked;
        rec->batches.push_back(std::move(b));
        rec->ops.push_back(Op{int(rec->batches.size()) - 1, nullptr});
        return;
    }
    if (!pend.empty() && pendPrim != prim) flush();
    pendPrim = prim;
    pend.insert(pend.end(), conv.begin(), conv.end());
}

void vertex2f(float x, float y) { vertex(x, y, 0.0f); }
void vertex3f(float x, float y, float z) { vertex(x, y, z); }

void color4f(float r, float g, float b, float a)
{
    if (!inBegin && rec) { rec->ops.push_back(Op{-1, [=]() { color4f(r, g, b, a); }}); recColorSet = true; }
    if (inBegin) colorInBegin = true;
    curColor[0] = r; curColor[1] = g; curColor[2] = b; curColor[3] = a;
}
void color3f(float r, float g, float b) { color4f(r, g, b, 1.0f); }
void color3fv(const float *c) { color4f(c[0], c[1], c[2], 1.0f); }
void color4fv(const float *c) { color4f(c[0], c[1], c[2], c[3]); }

void normal3f(float x, float y, float z)
{
    if (!inBegin && rec) rec->ops.push_back(Op{-1, [=]() { normal3f(x, y, z); }});
    curNormal[0] = x; curNormal[1] = y; curNormal[2] = z;
}

void texCoord2f(float s, float t) { curUV[0] = s; curUV[1] = t; }

// ---- Matrices ----------------------------------------------------------------------------------

void matrixMode(GLenum mode) { RECORD(matrixMode(mode)) flush(); modeProj = mode == GL_PROJECTION; }
void loadIdentity() { RECORD(loadIdentity()) flush(); curMatrix().setToIdentity(); }
void pushMatrix()
{
    RECORD(pushMatrix())
    auto &s = modeProj ? projStack : mvStack;
    s.push_back(s.back());
}
void popMatrix()
{
    RECORD(popMatrix())
    flush();
    auto &s = modeProj ? projStack : mvStack;
    if (s.size() > 1) s.pop_back();
}
void translatef(float x, float y, float z) { RECORD(translatef(x, y, z)) flush(); curMatrix().translate(x, y, z); }
void rotatef(float deg, float x, float y, float z) { RECORD(rotatef(deg, x, y, z)) flush(); curMatrix().rotate(deg, x, y, z); }
void scalef(float x, float y, float z) { RECORD(scalef(x, y, z)) flush(); curMatrix().scale(x, y, z); }
void ortho(double l, double r, double b, double t, double n, double fa)
{
    RECORD(ortho(l, r, b, t, n, fa))
    flush();
    curMatrix().ortho(float(l), float(r), float(b), float(t), float(n), float(fa));
}
void perspective(double fovy, double aspect, double zNear, double zFar)
{
    flush();
    curMatrix().perspective(float(fovy), float(aspect), float(zNear), float(zFar));
}
void lookAt(double ex, double ey, double ez, double cx, double cy, double cz, double ux, double uy, double uz)
{
    flush();
    curMatrix().lookAt(QVector3D(float(ex), float(ey), float(ez)), QVector3D(float(cx), float(cy), float(cz)),
                       QVector3D(float(ux), float(uy), float(uz)));
}

void getFloatv(GLenum what, float *out)
{
    if (what == GL_MODELVIEW_MATRIX) { std::memcpy(out, mv().constData(), 16 * sizeof(float)); return; }
    if (what == GL_PROJECTION_MATRIX) { std::memcpy(out, proj().constData(), 16 * sizeof(float)); return; }
    if (what == GL_FOG_COLOR) { std::memcpy(out, st.fogCol, 4 * sizeof(float)); return; }
    f->glGetFloatv(what, out);
}

// ---- State -------------------------------------------------------------------------------------

static void setCap(GLenum cap, bool on)
{
    flush();
    switch (cap) {
    case GL_LIGHTING: st.lighting = on; break;
    case GL_LIGHT0: st.light0 = on; break;
    case GL_COLOR_MATERIAL: st.colorMat = on; break;
    case GL_FOG: st.fog = on; break;
    case GL_TEXTURE_2D: st.tex2d = on; break;
    case GL_TEXTURE_GEN_S: st.texGenS = on; break;
    case GL_TEXTURE_GEN_T: st.texGenT = on; break;
    case GL_NORMALIZE: case GL_MULTISAMPLE: break;       // (always normalised; multisampling comes from the surface)
    case GL_DEPTH_TEST: st.depthTest = on; on ? f->glEnable(cap) : f->glDisable(cap); break;
    case GL_BLEND: st.blend = on; on ? f->glEnable(cap) : f->glDisable(cap); break;
    default: on ? f->glEnable(cap) : f->glDisable(cap); break;
    }
}
void enable(GLenum cap) { RECORD(enable(cap)) setCap(cap, true); }
void disable(GLenum cap) { RECORD(disable(cap)) setCap(cap, false); }
void blendFunc(GLenum s, GLenum d) { RECORD(blendFunc(s, d)) flush(); f->glBlendFunc(s, d); }
void depthMask(GLboolean on) { RECORD(depthMask(on)) flush(); f->glDepthMask(on); }
void depthFunc(GLenum fn) { flush(); f->glDepthFunc(fn); }
void viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    flush();
    vp[0] = x; vp[1] = y; vp[2] = w; vp[3] = h;
    f->glViewport(x, y, w, h);
}
void clear(GLbitfield mask) { flush(); f->glClear(mask); }
void clearColor(float r, float g, float b, float a) { f->glClearColor(r, g, b, a); }
void lineWidth(float w) { RECORD(lineWidth(w)) flush(); st.lineW = w; }
void pointSize(float s) { RECORD(pointSize(s)) flush(); st.pointS = s; }
void shadeModel(GLenum) {}
void hint(GLenum, GLenum) {}
void colorMaterial(GLenum, GLenum) {}                    // (the game only uses ambient and diffuse)

void lightfv(GLenum light, GLenum what, const float *v)
{
    if (light != GL_LIGHT0) return;
    std::array<float, 4> a = {v[0], v[1], v[2], what == GL_POSITION || what == GL_AMBIENT || what == GL_DIFFUSE || what == GL_SPECULAR ? v[3] : 1.0f};
    RECORD(lightfv(light, what, a.data()))
    flush();
    if (what == GL_POSITION) {
        const QVector4D e = mv() * QVector4D(a[0], a[1], a[2], a[3]);   // (stored in eye space, as OpenGL does)
        st.lPos[0] = e.x(); st.lPos[1] = e.y(); st.lPos[2] = e.z(); st.lPos[3] = e.w();
    } else if (what == GL_AMBIENT) std::memcpy(st.lAmb, a.data(), sizeof st.lAmb);
    else if (what == GL_DIFFUSE) std::memcpy(st.lDif, a.data(), sizeof st.lDif);
    else if (what == GL_SPECULAR) std::memcpy(st.lSpec, a.data(), sizeof st.lSpec);
}

void lightModelfv(GLenum what, const float *v)
{
    if (what != GL_LIGHT_MODEL_AMBIENT) return;
    flush();
    std::memcpy(st.gAmb, v, sizeof st.gAmb);
}

void materialfv(GLenum, GLenum what, const float *v)
{
    std::array<float, 4> a = {v[0], what == GL_SHININESS ? 0.0f : v[1], what == GL_SHININESS ? 0.0f : v[2], what == GL_SHININESS ? 0.0f : v[3]};
    RECORD(materialfv(GL_FRONT_AND_BACK, what, a.data()))
    flush();
    if (what == GL_SPECULAR) std::memcpy(st.mSpec, a.data(), sizeof st.mSpec);
    else if (what == GL_AMBIENT) std::memcpy(st.mAmb, a.data(), sizeof st.mAmb);
    else if (what == GL_DIFFUSE) std::memcpy(st.mDif, a.data(), sizeof st.mDif);
    else if (what == GL_AMBIENT_AND_DIFFUSE) { std::memcpy(st.mAmb, a.data(), sizeof st.mAmb); std::memcpy(st.mDif, a.data(), sizeof st.mDif); }
    else if (what == GL_SHININESS) st.shininess = a[0];
}
void materialf(GLenum face, GLenum what, float v) { const float a[4] = {v, 0, 0, 0}; materialfv(face, what, a); }

void fogi(GLenum, GLint) {}                              // (linear, the only mode the game uses)
void fogf(GLenum what, float v)
{
    flush();
    if (what == GL_FOG_START) st.fogStart = v;
    else if (what == GL_FOG_END) st.fogEnd = v;
}
void fogfv(GLenum what, const float *v)
{
    if (what != GL_FOG_COLOR) return;
    std::array<float, 4> a = {v[0], v[1], v[2], v[3]};
    RECORD(fogfv(what, a.data()))
    flush();
    std::memcpy(st.fogCol, a.data(), sizeof st.fogCol);
}
void texEnvi(GLenum, GLenum, GLint) {}                   // (modulate, the only mode the game uses)
void texGeni(GLenum, GLenum, GLint) {}                   // (sphere map, the only mode the game uses)

void pushAttrib(GLbitfield)
{
    flush();
    Attrib a;
    a.st = st;
    std::memcpy(a.color, curColor, sizeof a.color);
    attribStack.push_back(a);
}

void popAttrib()
{
    if (attribStack.empty()) return;
    flush();
    const Attrib a = attribStack.back();
    attribStack.pop_back();
    if (a.st.depthTest != st.depthTest) setCap(GL_DEPTH_TEST, a.st.depthTest);
    if (a.st.blend != st.blend) setCap(GL_BLEND, a.st.blend);
    if (a.st.tex != st.tex) f->glBindTexture(GL_TEXTURE_2D, a.st.tex);
    st = a.st;
    std::memcpy(curColor, a.color, sizeof curColor);
}

// ---- Textures ----------------------------------------------------------------------------------

void genTextures(GLsizei n, GLuint *ids) { f->glGenTextures(n, ids); }
void deleteTextures(GLsizei n, const GLuint *ids)
{
    flush();
    for (GLsizei i = 0; i < n; ++i) if (ids[i] == st.tex) st.tex = 0;
    f->glDeleteTextures(n, ids);
}
void bindTexture(GLenum target, GLuint id) { RECORD(bindTexture(target, id)) flush(); st.tex = id; f->glBindTexture(target, id); }
void texParameteri(GLenum target, GLenum what, GLint v) { f->glTexParameteri(target, what, v == GL_CLAMP ? GL_CLAMP_TO_EDGE : v); }
void texParameterf(GLenum target, GLenum what, float v) { f->glTexParameterf(target, what, v); }
void pixelStorei(GLenum what, GLint v) { f->glPixelStorei(what, v); }
void texImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data)
{
    f->glTexImage2D(target, level, desktopGL ? internal : GLint(format), w, h, border, format, type, data);
}
// every level made here, each a 2x2 average of the one before (as GLU does): the same on an old
// desktop context and in WebGL, where generating them on the GPU is not always there
GLint build2DMipmaps(GLenum target, GLint internal, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data)
{
    const int ch = format == GL_RGBA ? 4 : 3;
    std::vector<unsigned char> img(static_cast<const unsigned char *>(data), static_cast<const unsigned char *>(data) + size_t(w) * size_t(h) * size_t(ch));
    texImage2D(target, 0, internal, w, h, 0, format, type, img.data());
    for (int level = 1; w > 1 || h > 1; ++level) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<unsigned char> next(size_t(nw) * size_t(nh) * size_t(ch));
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < ch; ++c) {
                    const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                    const int sum = img[size_t((y0 * w + x0) * ch + c)] + img[size_t((y0 * w + x1) * ch + c)]
                                  + img[size_t((y1 * w + x0) * ch + c)] + img[size_t((y1 * w + x1) * ch + c)];
                    next[size_t((y * nw + x) * ch + c)] = (unsigned char)((sum + 2) / 4);
                }
        img.swap(next);
        w = nw; h = nh;
        texImage2D(target, level, internal, w, h, 0, format, type, img.data());
    }
    return 0;
}
const GLubyte *getString(GLenum what) { return f->glGetString(what); }

// ---- Display lists -----------------------------------------------------------------------------

GLuint genLists(GLsizei n)
{
    const GLuint first = nextList;
    nextList += GLuint(n);
    return first;
}

void deleteLists(GLuint first, GLsizei n)
{
    for (GLuint id = first; id < first + GLuint(n); ++id) {
        auto it = lists.find(id);
        if (it == lists.end()) continue;
        for (Batch &b : it->second.batches) if (b.vbo) f->glDeleteBuffers(1, &b.vbo);
        lists.erase(it);
    }
}

void newList(GLuint id, GLenum)
{
    flush();
    deleteLists(id, 1);
    rec = &lists[id];
    recColorSet = false;
}

void endList()
{
    if (!rec) return;
    for (Batch &b : rec->batches) {
        b.count = int(b.v.size());
        f->glGenBuffers(1, &b.vbo);
        f->glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.v.size() * sizeof(V)), b.v.data(), GL_STATIC_DRAW);
        if (b.prim != PLines) { b.v.clear(); b.v.shrink_to_fit(); }   // (lines may need widening at draw time)
    }
    rec = nullptr;
}

void callList(GLuint id)
{
    RECORD(callList(id))
    auto it = lists.find(id);
    if (it == lists.end()) return;
    flush();
    const List &l = it->second;
    for (const Op &op : l.ops) {
        if (op.batch >= 0) drawBatch(l.batches[size_t(op.batch)]);
        else op.fn();
    }
}

// ---- GLU shapes (as GLU makes them: along +z, the angle measured from +y towards +x) -----------

GLUquadric *newQuadric() { return new GLUquadric; }
void deleteQuadric(GLUquadric *q) { delete q; }
void quadricNormals(GLUquadric *q, GLenum normals) { if (q) q->smooth = normals == GLU_SMOOTH; }

void cylinder(GLUquadric *, double base, double top, double height, GLint slices, GLint stacks)
{
    const float nz = height != 0.0 ? float((base - top) / height) : 0.0f;
    for (int j = 0; j < stacks; ++j) {
        const float z0 = float(height * j / stacks), z1 = float(height * (j + 1) / stacks);
        const float r0 = float(base + (top - base) * j / stacks), r1 = float(base + (top - base) * (j + 1) / stacks);
        begin(GL_QUAD_STRIP);
        for (int i = 0; i <= slices; ++i) {
            const float a = 2.0f * 3.14159265f * float(i % slices) / float(slices), s = std::sin(a), c = std::cos(a);
            const float len = std::sqrt(1.0f + nz * nz);
            normal3f(s / len, c / len, nz / len);
            vertex3f(r0 * s, r0 * c, z0);
            vertex3f(r1 * s, r1 * c, z1);
        }
        end();
    }
}

void disk(GLUquadric *, double inner, double outer, GLint slices, GLint loops)
{
    normal3f(0.0f, 0.0f, 1.0f);
    for (int j = 0; j < loops; ++j) {
        const float ra = float(inner + (outer - inner) * j / loops), rb = float(inner + (outer - inner) * (j + 1) / loops);
        begin(GL_QUAD_STRIP);
        for (int i = 0; i <= slices; ++i) {
            const float a = 2.0f * 3.14159265f * float(i % slices) / float(slices), s = std::sin(a), c = std::cos(a);
            vertex3f(rb * s, rb * c, 0.0f);
            vertex3f(ra * s, ra * c, 0.0f);
        }
        end();
    }
}

void sphere(GLUquadric *, double radius, GLint slices, GLint stacks)
{
    const float r = float(radius);
    for (int j = 0; j < stacks; ++j) {
        const float rho0 = 3.14159265f * float(j) / float(stacks), rho1 = 3.14159265f * float(j + 1) / float(stacks);
        begin(GL_QUAD_STRIP);
        for (int i = 0; i <= slices; ++i) {
            const float th = 2.0f * 3.14159265f * float(i % slices) / float(slices);
            const float x0 = -std::sin(th) * std::sin(rho0), y0 = std::cos(th) * std::sin(rho0), z0 = std::cos(rho0);
            const float x1 = -std::sin(th) * std::sin(rho1), y1 = std::cos(th) * std::sin(rho1), z1 = std::cos(rho1);
            normal3f(x0, y0, z0); vertex3f(x0 * r, y0 * r, z0 * r);
            normal3f(x1, y1, z1); vertex3f(x1 * r, y1 * r, z1 * r);
        }
        end();
    }
}

} // namespace sgl
