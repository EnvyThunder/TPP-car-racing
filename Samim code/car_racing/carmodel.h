#ifndef CARMODEL_H
#define CARMODEL_H

// Sculpted sports-car model for the legacy (fixed-function) OpenGL renderer.
//
// The body is not made of boxes: it is "lofted". We describe the car with a few
// smooth profile curves along its length (width, hood/deck line, shoulder line,
// sill line with wheel-arch cut-outs) and sweep a rounded cross-section along
// them. The result is compiled once into display lists, so drawing a car each
// frame costs only a few glCallList() calls.
//
// Car-local axes: +X right, +Y up, front of the car towards -Z (same as the sim).

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

namespace carmodel {

// Wheel layout shared by the body (arch cut-outs) and GLWidget::drawCar
constexpr float WHEEL_R       = 0.36f;
constexpr float WHEEL_X       = 0.98f;    // tyre centre from the car's centreline
constexpr float WHEEL_Z_FRONT = -1.38f;
constexpr float WHEEL_Z_REAR  =  1.38f;
constexpr float NOSE_Z        = -2.25f;
constexpr float TAIL_Z        =  2.25f;

struct Lists {
    GLuint body  = 0;   // painted panels: no colour calls inside, caller sets the paint colour
    GLuint glass = 0;   // greenhouse glass: no colour calls inside
    GLuint trim  = 0;   // black plastics, grille, wheel wells, exhausts, lamp housings (own colours)
};

// Build the display lists. Needs a current GL context.
Lists build();
void  destroy(Lists &l);

// Colours of the world the car reflects (each map has its own sky).
struct EnvColors {
    float horizon[3], mid[3], top[3];   // sky from horizon to zenith
    float ground[3];                    // what is below the horizon
    float sun[3];                       // sun (or moon) colour
    float sunHeight;                    // sun elevation, roughly sin(angle)
};

// Sphere-map texture (sky above, ground below) for paint/glass reflections.
GLuint buildEnvMap(const EnvColors &e);

// Draws `list` again as an additive sphere-mapped reflection ("clear coat").
void reflectionPass(GLuint list, GLuint envTex, float strength);

// Additive-blend helpers for glows (lamps, nitro, headlight pools). They also make
// the fog black so glowing things don't turn fog-coloured in the distance.
void beginGlow();
void endGlow();

// Draws a patch that sits exactly on the body surface (lamps, decals).
// u = position along the car (NOSE_Z..TAIL_Z), s = position around the
// cross-section (0 = left sill, 0.5 = roof/hood centre, 1 = right sill).
void bodyPatch(float u0, float u1, float s0, float s1, float offset, int nu = 6, int ns = 4);

// Height of the body's centreline (hood / deck, ignoring the cabin) at length u.
float bodyTop(float u);

// Rear wing on struts (wing in the current colour, struts/end plates black).
void drawWing();

// Head/tail lights and their glow halos. brake = 0..1 brightens the tail lights.
// Call with lighting disabled.
void drawLamps(float brake);

} // namespace carmodel

#endif // CARMODEL_H
