#ifndef CG_MATH_H
#define CG_MATH_H

#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

// Fast RGBA utility (0xAARRGGBB)
inline constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(b);
}

inline constexpr uint8_t getRed(uint32_t c)   { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline constexpr uint8_t getGreen(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline constexpr uint8_t getBlue(uint32_t c)  { return static_cast<uint8_t>(c & 0xFF); }
inline constexpr uint8_t getAlpha(uint32_t c) { return static_cast<uint8_t>((c >> 24) & 0xFF); }

inline uint32_t blendRgba(uint32_t bg, uint32_t fg) {
    uint32_t a = getAlpha(fg);
    if (a == 255) return fg;
    if (a == 0) return bg;
    uint32_t invA = 255 - a;
    uint32_t r = (getRed(fg) * a + getRed(bg) * invA) / 255;
    uint32_t g = (getGreen(fg) * a + getGreen(bg) * invA) / 255;
    uint32_t b = (getBlue(fg) * a + getBlue(bg) * invA) / 255;
    return rgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255);
}

inline uint32_t lerpRgba(uint32_t c1, uint32_t c2, double t) {
    t = std::max(0.0, std::min(1.0, t));
    uint8_t r = static_cast<uint8_t>(getRed(c1) + t * (getRed(c2) - getRed(c1)));
    uint8_t g = static_cast<uint8_t>(getGreen(c1) + t * (getGreen(c2) - getGreen(c1)));
    uint8_t b = static_cast<uint8_t>(getBlue(c1) + t * (getBlue(c2) - getBlue(c1)));
    uint8_t a = static_cast<uint8_t>(getAlpha(c1) + t * (getAlpha(c2) - getAlpha(c1)));
    return rgba(r, g, b, a);
}

inline uint32_t scaleBrightness(uint32_t col, double factor) {
    uint8_t r = static_cast<uint8_t>(std::min(255.0, getRed(col) * factor));
    uint8_t g = static_cast<uint8_t>(std::min(255.0, getGreen(col) * factor));
    uint8_t b = static_cast<uint8_t>(std::min(255.0, getBlue(col) * factor));
    return rgba(r, g, b, getAlpha(col));
}

// 2D Vector Structure
struct Vec2D {
    double x;
    double y;

    Vec2D() : x(0.0), y(0.0) {}
    Vec2D(double _x, double _y) : x(_x), y(_y) {}

    Vec2D operator-() const { return Vec2D(-x, -y); }
    Vec2D operator+(const Vec2D &o) const { return Vec2D(x + o.x, y + o.y); }
    Vec2D operator-(const Vec2D &o) const { return Vec2D(x - o.x, y - o.y); }
    Vec2D operator*(double s) const { return Vec2D(x * s, y * s); }
    Vec2D operator/(double s) const { return Vec2D(x / s, y / s); }

    double length() const { return std::sqrt(x * x + y * y); }
    double lengthSquared() const { return x * x + y * y; }

    Vec2D normalized() const {
        double l = length();
        if (l < 1e-6) return Vec2D(0.0, 0.0);
        return Vec2D(x / l, y / l);
    }

    double dot(const Vec2D &o) const { return x * o.x + y * o.y; }
    double cross(const Vec2D &o) const { return x * o.y - y * o.x; }

    Vec2D rotated(double rad) const {
        double cosA = std::cos(rad);
        double sinA = std::sin(rad);
        return Vec2D(x * cosA - y * sinA, x * sinA + y * cosA);
    }
};

// 2D Affine Transformation Matrix (3x3 homogeneous coordinates)
// [ m[0][0]  m[0][1]  m[0][2] ]
// [ m[1][0]  m[1][1]  m[1][2] ]
// [    0        0        1    ]
struct Mat2D {
    double m[2][3];

    static Mat2D identity();
    static Mat2D translation(double tx, double ty);
    static Mat2D rotation(double angleRad);
    static Mat2D scale(double sx, double sy);
    static Mat2D multiply(const Mat2D &a, const Mat2D &b);

    Vec2D transform(const Vec2D &p) const;
    Vec2D transform(double x, double y) const;
};

// Core Computer Graphics Algorithms
void putPixelSafe(uint32_t *fb, int fbW, int fbH, int x, int y, uint32_t col);
void blendPixelSafe(uint32_t *fb, int fbW, int fbH, int x, int y, uint32_t col);
void fillScanline(uint32_t *fb, int fbW, int fbH, int y, int x0, int x1, uint32_t col);
void fillScanlineBlend(uint32_t *fb, int fbW, int fbH, int y, int x0, int x1, uint32_t col);

// Bresenham's Line Algorithm with Thickness
void drawBresenhamLine(uint32_t *fb, int fbW, int fbH, int x0, int y0, int x1, int y1, uint32_t col, int thickness = 1);

// Midpoint Circle & Ellipse Algorithms
void drawMidpointCircle(uint32_t *fb, int fbW, int fbH, int cx, int cy, int radius, uint32_t col, int thickness = 1);
void fillMidpointCircle(uint32_t *fb, int fbW, int fbH, int cx, int cy, int radius, uint32_t col);
void drawMidpointEllipse(uint32_t *fb, int fbW, int fbH, int cx, int cy, int rx, int ry, uint32_t col);
void fillMidpointEllipse(uint32_t *fb, int fbW, int fbH, int cx, int cy, int rx, int ry, uint32_t col);

// Triangle & Convex Polygon Rasterization (Scanline)
void fillTriangle(uint32_t *fb, int fbW, int fbH, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col);
void fillTriangleBlend(uint32_t *fb, int fbW, int fbH, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col);
void fillConvexPolygon(uint32_t *fb, int fbW, int fbH, const std::vector<Vec2D> &pts, uint32_t col);
void fillConvexPolygonBlend(uint32_t *fb, int fbW, int fbH, const std::vector<Vec2D> &pts, uint32_t col);
void drawPolygonOutline(uint32_t *fb, int fbW, int fbH, const std::vector<Vec2D> &pts, uint32_t col, int thickness = 1);

// Headlight Beam Projection (Alpha Blended Light Fan)
void drawHeadlightCone(uint32_t *fb, int fbW, int fbH, const Vec2D &apex, const Vec2D &dir, double angleRad, double length, uint32_t innerCol, uint32_t outerCol);

// Arcade Typography
void drawArcadeText(uint32_t *fb, int fbW, int fbH, int x, int y, const std::string &text, uint32_t col, int scale = 1, bool shadow = true);

#endif // CG_MATH_H
