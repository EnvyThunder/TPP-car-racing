#include "cg_math.h"
#include <cmath>
#include <algorithm>
#include <cstring>

Mat2D Mat2D::identity() {
    Mat2D res;
    res.m[0][0] = 1.0; res.m[0][1] = 0.0; res.m[0][2] = 0.0;
    res.m[1][0] = 0.0; res.m[1][1] = 1.0; res.m[1][2] = 0.0;
    return res;
}

Mat2D Mat2D::translation(double tx, double ty) {
    Mat2D res;
    res.m[0][0] = 1.0; res.m[0][1] = 0.0; res.m[0][2] = tx;
    res.m[1][0] = 0.0; res.m[1][1] = 1.0; res.m[1][2] = ty;
    return res;
}

Mat2D Mat2D::rotation(double angleRad) {
    Mat2D res;
    double c = std::cos(angleRad);
    double s = std::sin(angleRad);
    res.m[0][0] = c;   res.m[0][1] = -s;  res.m[0][2] = 0.0;
    res.m[1][0] = s;   res.m[1][1] = c;   res.m[1][2] = 0.0;
    return res;
}

Mat2D Mat2D::scale(double sx, double sy) {
    Mat2D res;
    res.m[0][0] = sx;  res.m[0][1] = 0.0; res.m[0][2] = 0.0;
    res.m[1][0] = 0.0; res.m[1][1] = sy;  res.m[1][2] = 0.0;
    return res;
}

Mat2D Mat2D::multiply(const Mat2D &a, const Mat2D &b) {
    Mat2D res;
    res.m[0][0] = a.m[0][0] * b.m[0][0] + a.m[0][1] * b.m[1][0];
    res.m[0][1] = a.m[0][0] * b.m[0][1] + a.m[0][1] * b.m[1][1];
    res.m[0][2] = a.m[0][0] * b.m[0][2] + a.m[0][1] * b.m[1][2] + a.m[0][2];

    res.m[1][0] = a.m[1][0] * b.m[0][0] + a.m[1][1] * b.m[1][0];
    res.m[1][1] = a.m[1][0] * b.m[0][1] + a.m[1][1] * b.m[1][1];
    res.m[1][2] = a.m[1][0] * b.m[0][2] + a.m[1][1] * b.m[1][2] + a.m[1][2];
    return res;
}

Vec2D Mat2D::transform(const Vec2D &p) const {
    return Vec2D(
        m[0][0] * p.x + m[0][1] * p.y + m[0][2],
        m[1][0] * p.x + m[1][1] * p.y + m[1][2]
    );
}

Vec2D Mat2D::transform(double x, double y) const {
    return Vec2D(
        m[0][0] * x + m[0][1] * y + m[0][2],
        m[1][0] * x + m[1][1] * y + m[1][2]
    );
}

void putPixelSafe(uint32_t *fb, int fbW, int fbH, int x, int y, uint32_t col) {
    if (x >= 0 && x < fbW && y >= 0 && y < fbH) {
        fb[y * fbW + x] = col;
    }
}

void blendPixelSafe(uint32_t *fb, int fbW, int fbH, int x, int y, uint32_t col) {
    if (x >= 0 && x < fbW && y >= 0 && y < fbH) {
        int idx = y * fbW + x;
        fb[idx] = blendRgba(fb[idx], col);
    }
}

void fillScanline(uint32_t *fb, int fbW, int fbH, int y, int x0, int x1, uint32_t col) {
    if (y < 0 || y >= fbH) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(fbW - 1, x1);
    if (x0 > x1) return;

    uint32_t *row = fb + (y * fbW);
    for (int x = x0; x <= x1; x++) {
        row[x] = col;
    }
}

void fillScanlineBlend(uint32_t *fb, int fbW, int fbH, int y, int x0, int x1, uint32_t col) {
    if (y < 0 || y >= fbH) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(fbW - 1, x1);
    if (x0 > x1) return;

    uint32_t *row = fb + (y * fbW);
    for (int x = x0; x <= x1; x++) {
        row[x] = blendRgba(row[x], col);
    }
}

void drawBresenhamLine(uint32_t *fb, int fbW, int fbH, int x0, int y0, int x1, int y1, uint32_t col, int thickness) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int halfThick = thickness / 2;

    while (true) {
        if (thickness <= 1) {
            putPixelSafe(fb, fbW, fbH, x0, y0, col);
        } else {
            for (int ty = -halfThick; ty <= halfThick; ty++) {
                for (int tx = -halfThick; tx <= halfThick; tx++) {
                    putPixelSafe(fb, fbW, fbH, x0 + tx, y0 + ty, col);
                }
            }
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void drawMidpointCircle(uint32_t *fb, int fbW, int fbH, int cx, int cy, int radius, uint32_t col, int thickness) {
    int x = 0;
    int y = radius;
    int d = 1 - radius;

    auto plotOctants = [&](int px, int py) {
        if (thickness <= 1) {
            putPixelSafe(fb, fbW, fbH, cx + px, cy + py, col);
            putPixelSafe(fb, fbW, fbH, cx - px, cy + py, col);
            putPixelSafe(fb, fbW, fbH, cx + px, cy - py, col);
            putPixelSafe(fb, fbW, fbH, cx - px, cy - py, col);
            putPixelSafe(fb, fbW, fbH, cx + py, cy + px, col);
            putPixelSafe(fb, fbW, fbH, cx - py, cy + px, col);
            putPixelSafe(fb, fbW, fbH, cx + py, cy - px, col);
            putPixelSafe(fb, fbW, fbH, cx - py, cy - px, col);
        } else {
            for (int t = -thickness/2; t <= thickness/2; t++) {
                putPixelSafe(fb, fbW, fbH, cx + px, cy + py + t, col);
                putPixelSafe(fb, fbW, fbH, cx - px, cy + py + t, col);
                putPixelSafe(fb, fbW, fbH, cx + px, cy - py + t, col);
                putPixelSafe(fb, fbW, fbH, cx - px, cy - py + t, col);
                putPixelSafe(fb, fbW, fbH, cx + py + t, cy + px, col);
                putPixelSafe(fb, fbW, fbH, cx - py + t, cy + px, col);
                putPixelSafe(fb, fbW, fbH, cx + py + t, cy - px, col);
                putPixelSafe(fb, fbW, fbH, cx - py + t, cy - px, col);
            }
        }
    };

    plotOctants(x, y);
    while (x < y) {
        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
        plotOctants(x, y);
    }
}

void fillMidpointCircle(uint32_t *fb, int fbW, int fbH, int cx, int cy, int radius, uint32_t col) {
    int x = 0;
    int y = radius;
    int d = 1 - radius;

    bool blend = (getAlpha(col) < 255);

    auto drawLines = [&](int px, int py) {
        if (blend) {
            fillScanlineBlend(fb, fbW, fbH, cy + py, cx - px, cx + px, col);
            if (py != 0) fillScanlineBlend(fb, fbW, fbH, cy - py, cx - px, cx + px, col);
            fillScanlineBlend(fb, fbW, fbH, cy + px, cx - py, cx + py, col);
            if (px != 0) fillScanlineBlend(fb, fbW, fbH, cy - px, cx - py, cx + py, col);
        } else {
            fillScanline(fb, fbW, fbH, cy + py, cx - px, cx + px, col);
            if (py != 0) fillScanline(fb, fbW, fbH, cy - py, cx - px, cx + px, col);
            fillScanline(fb, fbW, fbH, cy + px, cx - py, cx + py, col);
            if (px != 0) fillScanline(fb, fbW, fbH, cy - px, cx - py, cx + py, col);
        }
    };

    drawLines(x, y);
    while (x < y) {
        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
        drawLines(x, y);
    }
}

void drawMidpointEllipse(uint32_t *fb, int fbW, int fbH, int cx, int cy, int rx, int ry, uint32_t col) {
    if (rx <= 0 || ry <= 0) return;
    long long rx2 = static_cast<long long>(rx) * rx;
    long long ry2 = static_cast<long long>(ry) * ry;
    long long twoRx2 = 2 * rx2;
    long long twoRy2 = 2 * ry2;

    long long x = 0;
    long long y = ry;
    long long px = 0;
    long long py = twoRx2 * y;

    auto plotQuadrants = [&](int qx, int qy) {
        putPixelSafe(fb, fbW, fbH, cx + qx, cy + qy, col);
        putPixelSafe(fb, fbW, fbH, cx - qx, cy + qy, col);
        putPixelSafe(fb, fbW, fbH, cx + qx, cy - qy, col);
        putPixelSafe(fb, fbW, fbH, cx - qx, cy - qy, col);
    };

    // Region 1
    double p = ry2 - (rx2 * ry) + (0.25 * rx2);
    while (px < py) {
        plotQuadrants(static_cast<int>(x), static_cast<int>(y));
        x++;
        px += twoRy2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= twoRx2;
            p += ry2 + px - py;
        }
    }

    // Region 2
    p = ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        plotQuadrants(static_cast<int>(x), static_cast<int>(y));
        y--;
        py -= twoRx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
        }
    }
}

void fillMidpointEllipse(uint32_t *fb, int fbW, int fbH, int cx, int cy, int rx, int ry, uint32_t col) {
    if (rx <= 0 || ry <= 0) return;
    bool blend = (getAlpha(col) < 255);

    for (int y = -ry; y <= ry; y++) {
        double factor = 1.0 - (static_cast<double>(y * y) / (ry * ry));
        if (factor < 0.0) continue;
        int dx = static_cast<int>(rx * std::sqrt(factor));
        if (blend) {
            fillScanlineBlend(fb, fbW, fbH, cy + y, cx - dx, cx + dx, col);
        } else {
            fillScanline(fb, fbW, fbH, cy + y, cx - dx, cx + dx, col);
        }
    }
}

void fillTriangle(uint32_t *fb, int fbW, int fbH, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col) {
    // Sort vertices by Y ascending
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y0 > y2) { std::swap(x0, x2); std::swap(y0, y2); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }

    int totalHeight = y2 - y0;
    if (totalHeight == 0) return;

    for (int y = y0; y <= y2; y++) {
        bool secondHalf = (y > y1) || (y1 == y0);
        int segmentHeight = secondHalf ? (y2 - y1) : (y1 - y0);
        if (segmentHeight == 0) continue;

        double alpha = static_cast<double>(y - y0) / totalHeight;
        double beta  = static_cast<double>(y - (secondHalf ? y1 : y0)) / segmentHeight;

        int ax = static_cast<int>(x0 + (x2 - x0) * alpha);
        int bx = secondHalf ? static_cast<int>(x1 + (x2 - x1) * beta)
                            : static_cast<int>(x0 + (x1 - x0) * beta);

        fillScanline(fb, fbW, fbH, y, ax, bx, col);
    }
}

void fillTriangleBlend(uint32_t *fb, int fbW, int fbH, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col) {
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y0 > y2) { std::swap(x0, x2); std::swap(y0, y2); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }

    int totalHeight = y2 - y0;
    if (totalHeight == 0) return;

    for (int y = y0; y <= y2; y++) {
        bool secondHalf = (y > y1) || (y1 == y0);
        int segmentHeight = secondHalf ? (y2 - y1) : (y1 - y0);
        if (segmentHeight == 0) continue;

        double alpha = static_cast<double>(y - y0) / totalHeight;
        double beta  = static_cast<double>(y - (secondHalf ? y1 : y0)) / segmentHeight;

        int ax = static_cast<int>(x0 + (x2 - x0) * alpha);
        int bx = secondHalf ? static_cast<int>(x1 + (x2 - x1) * beta)
                            : static_cast<int>(x0 + (x1 - x0) * beta);

        fillScanlineBlend(fb, fbW, fbH, y, ax, bx, col);
    }
}

void fillConvexPolygon(uint32_t *fb, int fbW, int fbH, const std::vector<Vec2D> &pts, uint32_t col) {
    if (pts.size() < 3) return;
    // Fan triangulation from vertex 0
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        fillTriangle(fb, fbW, fbH,
                     static_cast<int>(std::round(pts[0].x)),
                     static_cast<int>(std::round(pts[0].y)),
                     static_cast<int>(std::round(pts[i].x)),
                     static_cast<int>(std::round(pts[i].y)),
                     static_cast<int>(std::round(pts[i+1].x)),
                     static_cast<int>(std::round(pts[i+1].y)),
                     col);
    }
}

void fillConvexPolygonBlend(uint32_t *fb, int fbW, int fbH, const std::vector<Vec2D> &pts, uint32_t col) {
    if (pts.size() < 3) return;
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        fillTriangleBlend(fb, fbW, fbH,
                          static_cast<int>(std::round(pts[0].x)),
                          static_cast<int>(std::round(pts[0].y)),
                          static_cast<int>(std::round(pts[i].x)),
                          static_cast<int>(std::round(pts[i].y)),
                          static_cast<int>(std::round(pts[i+1].x)),
                          static_cast<int>(std::round(pts[i+1].y)),
                          col);
    }
}

void drawPolygonOutline(uint32_t *fb, int fbW, int fbH, const std::vector<Vec2D> &pts, uint32_t col, int thickness) {
    if (pts.size() < 2) return;
    for (size_t i = 0; i < pts.size(); i++) {
        size_t next = (i + 1) % pts.size();
        drawBresenhamLine(fb, fbW, fbH,
                          static_cast<int>(std::round(pts[i].x)),
                          static_cast<int>(std::round(pts[i].y)),
                          static_cast<int>(std::round(pts[next].x)),
                          static_cast<int>(std::round(pts[next].y)),
                          col, thickness);
    }
}

void drawHeadlightCone(uint32_t *fb, int fbW, int fbH, const Vec2D &apex, const Vec2D &dir, double angleRad, double length, uint32_t innerCol, uint32_t outerCol) {
    Vec2D forward = dir.normalized();
    double halfAngle = angleRad / 2.0;

    int steps = 18;
    for (int i = 0; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;
        double a = -halfAngle + t * angleRad;
        Vec2D rayDir = forward.rotated(a);

        // Alpha blended ray with distance attenuation
        for (int d = 6; d <= static_cast<int>(length); d += 2) {
            double distT = static_cast<double>(d) / length;
            uint32_t col = lerpRgba(innerCol, outerCol, distT);
            int px = static_cast<int>(apex.x + rayDir.x * d);
            int py = static_cast<int>(apex.y + rayDir.y * d);
            blendPixelSafe(fb, fbW, fbH, px, py, col);
            blendPixelSafe(fb, fbW, fbH, px + 1, py, col);
        }
    }
}

// 5x7 High-Energy Arcade Bitmap Font
static const uint8_t ARCADE_FONT_5X7[96][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // 42 '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // 87 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
    {0x03, 0x04, 0x78, 0x04, 0x03}, // 89 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91 '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93 ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}  // 96 '`'
};

void drawArcadeText(uint32_t *fb, int fbW, int fbH, int x, int y, const std::string &text, uint32_t col, int scale, bool shadow) {
    if (scale < 1) scale = 1;

    auto renderGlyph = [&](char ch, int gx, int gy, uint32_t c) {
        char upper = (ch >= 'a' && ch <= 'z') ? (ch - 'a' + 'A') : ch;
        int idx = upper - 32;
        if (idx < 0 || idx >= 96) idx = 0;

        for (int colIdx = 0; colIdx < 5; colIdx++) {
            uint8_t bits = ARCADE_FONT_5X7[idx][colIdx];
            for (int rowIdx = 0; rowIdx < 7; rowIdx++) {
                if (bits & (1 << rowIdx)) {
                    int px = gx + colIdx * scale;
                    int py = gy + rowIdx * scale;
                    for (int dy = 0; dy < scale; dy++) {
                        fillScanline(fb, fbW, fbH, py + dy, px, px + scale - 1, c);
                    }
                }
            }
        }
    };

    if (shadow) {
        int curX = x + scale;
        int curY = y + scale;
        uint32_t shadowCol = rgba(10, 15, 26, 200);
        for (char ch : text) {
            renderGlyph(ch, curX, curY, shadowCol);
            curX += 6 * scale;
        }
    }

    int curX = x;
    for (char ch : text) {
        renderGlyph(ch, curX, y, col);
        curX += 6 * scale;
    }
}
