#ifndef GL1SHIM_H
#define GL1SHIM_H

// The game's OpenGL 1 drawing on top of OpenGL ES 2 / WebGL (docs/browser/README.md, Phase D1).
// The renderer was written for the old fixed-function pipeline: glBegin/glEnd, the matrix
// stacks, fixed-function lighting and fog, display lists, GLU shapes. Browsers only have WebGL
// (which has none of that), so this small layer of our own does it with one shader and vertex
// buffers - on the desktop too, so both draw the same pixels. gl1redirect.h points the game's
// gl / glu calls here.
//
//   immediate mode   batches of vertices (quads and polygons become triangles); small batches
//                    with the same state are merged into one draw
//   matrices         modelview / projection stacks, glOrtho, gluPerspective, gluLookAt
//   lighting         light 0 (ambient, diffuse, specular; directional or positional), the
//                    global ambient, colour material, specular material and shininess
//   fog              linear, on the eye distance
//   textures         modulate; sphere-map texture coordinates (the cars' reflections)
//   display lists    recorded once, replayed from static vertex buffers
//   GLU              cylinders, discs, spheres
//   wide lines       drawn as thin quads (WebGL only draws 1-pixel lines)

#include <qopengl.h>                 // (GL types and constants only: no Qt keywords such as emit)

struct GLUquadric;

// what OpenGL ES headers don't have (the values of desktop OpenGL 1)
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_QUAD_STRIP
#define GL_QUAD_STRIP 0x0008
#endif
#ifndef GL_POLYGON
#define GL_POLYGON 0x0009
#endif
#ifndef GL_COMPILE
#define GL_COMPILE 0x1300
#endif
#ifndef GL_MODELVIEW
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#endif
#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING 0x0B50
#define GL_LIGHT0 0x4000
#define GL_NORMALIZE 0x0BA1
#define GL_COLOR_MATERIAL 0x0B57
#define GL_FOG 0x0B60
#endif
#ifndef GL_AMBIENT
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_POSITION 0x1203
#define GL_SHININESS 0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE 0x0B65
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_HINT 0x0C54
#endif
#ifndef GL_SMOOTH
#define GL_SMOOTH 0x1D01
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#endif
#ifndef GL_TEXTURE_GEN_S
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_GEN_MODE 0x2500
#define GL_SPHERE_MAP 0x2402
#define GL_S 0x2000
#define GL_T 0x2001
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif
#ifndef GL_ENABLE_BIT
#define GL_CURRENT_BIT 0x00000001
#define GL_ENABLE_BIT 0x00002000
#define GL_TEXTURE_BIT 0x00040000
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GLU_SMOOTH
#define GLU_SMOOTH 100000
#endif

namespace sgl {

void init();                                  // once, with the context current (initializeGL)
void flush();                                 // draw what is waiting
void endFrame();                              // the end of every frame (Qt may use OpenGL in between)
struct Stats { int draws = 0, vertices = 0, streamed = 0; };   // (streamed: vertices uploaded this frame)
Stats frameStats();                           // (since the last call)

// immediate mode
void begin(GLenum mode);
void end();
void vertex2f(float x, float y);
void vertex3f(float x, float y, float z);
void color3f(float r, float g, float b);
void color4f(float r, float g, float b, float a);
void color3fv(const float *c);
void color4fv(const float *c);
void normal3f(float x, float y, float z);
void texCoord2f(float s, float t);

// matrices
void matrixMode(GLenum mode);
void loadIdentity();
void pushMatrix();
void popMatrix();
void translatef(float x, float y, float z);
void rotatef(float deg, float x, float y, float z);
void scalef(float x, float y, float z);
void ortho(double l, double r, double b, double t, double n, double f);
void perspective(double fovy, double aspect, double zNear, double zFar);
void lookAt(double ex, double ey, double ez, double cx, double cy, double cz, double ux, double uy, double uz);
void getFloatv(GLenum what, float *out);

// state
void enable(GLenum cap);
void disable(GLenum cap);
void blendFunc(GLenum s, GLenum d);
void depthMask(GLboolean on);
void depthFunc(GLenum f);
void viewport(GLint x, GLint y, GLsizei w, GLsizei h);
void clear(GLbitfield mask);
void clearColor(float r, float g, float b, float a);
void lineWidth(float w);
void pointSize(float s);
void shadeModel(GLenum);
void hint(GLenum, GLenum);
void colorMaterial(GLenum face, GLenum mode);
void lightfv(GLenum light, GLenum what, const float *v);
void lightModelfv(GLenum what, const float *v);
void materialfv(GLenum face, GLenum what, const float *v);
void materialf(GLenum face, GLenum what, float v);
void fogi(GLenum what, GLint v);
void fogf(GLenum what, float v);
void fogfv(GLenum what, const float *v);
void texEnvi(GLenum, GLenum, GLint);
void texGeni(GLenum coord, GLenum what, GLint mode);
void pushAttrib(GLbitfield mask);
void popAttrib();

// textures
void genTextures(GLsizei n, GLuint *ids);
void deleteTextures(GLsizei n, const GLuint *ids);
void bindTexture(GLenum target, GLuint id);
void texParameteri(GLenum target, GLenum what, GLint v);
void texParameterf(GLenum target, GLenum what, float v);
void pixelStorei(GLenum what, GLint v);
void texImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data);
GLint build2DMipmaps(GLenum target, GLint internal, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data);
const GLubyte *getString(GLenum what);

// display lists
GLuint genLists(GLsizei n);
void deleteLists(GLuint first, GLsizei n);
void newList(GLuint id, GLenum mode);
void endList();
void callList(GLuint id);

// GLU shapes
GLUquadric *newQuadric();
void deleteQuadric(GLUquadric *q);
void quadricNormals(GLUquadric *q, GLenum normals);
void cylinder(GLUquadric *q, double base, double top, double height, GLint slices, GLint stacks);
void disk(GLUquadric *q, double inner, double outer, GLint slices, GLint loops);
void sphere(GLUquadric *q, double radius, GLint slices, GLint stacks);

} // namespace sgl

#endif // GL1SHIM_H
