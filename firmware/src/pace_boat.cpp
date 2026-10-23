#include "pace_boat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "rower_scene.h"

namespace pace_boat {

namespace {

// Present buffer pixels per scene frame pixel (panel_runtime's 2x blit).
constexpr float kScale = 2.0f;
// Lane offset. Beside our boat but inside the picture: at 4.5 m (fully
// clear of our blades) it sat past the right edge until it was ~3 m ahead.
// At 3 m its left blade can cross our right one when level; it's
// translucent, so that reads as two boats close together.
constexpr float kLaneX = 3.0f;
constexpr float kAlpha = 0.6f;

struct Canvas {
    uint16_t* px;
    int w;
    int h;
    int stride; // in pixels
};

struct P2 {
    float x;
    float y;
};

// Ghost white with a little blue, so it reads as "not a real boat".
constexpr int kR = 225;
constexpr int kG = 240;
constexpr int kB = 255;

void blendSpan(const Canvas& c, int y, int x0, int x1, float a) {
    if (y < 0 || y >= c.h) {
        return;
    }
    x0 = std::max(x0, 0);
    x1 = std::min(x1, c.w - 1);
    uint16_t* row = c.px + static_cast<size_t>(y) * c.stride;
    const int ia = static_cast<int>(a * 256.0f);
    for (int x = x0; x <= x1; x++) {
        const uint16_t d = row[x];
        const int r = (d >> 11) & 31;
        const int g = (d >> 5) & 63;
        const int b = d & 31;
        const int nr = r + (((kR >> 3) - r) * ia >> 8);
        const int ng = g + (((kG >> 2) - g) * ia >> 8);
        const int nb = b + (((kB >> 3) - b) * ia >> 8);
        row[x] = static_cast<uint16_t>((nr << 11) | (ng << 5) | nb);
    }
}

// Scanline fill of a convex polygon.
void fillConvex(const Canvas& c, const P2* p, int n, float a) {
    float minY = p[0].y;
    float maxY = p[0].y;
    for (int i = 1; i < n; i++) {
        minY = std::min(minY, p[i].y);
        maxY = std::max(maxY, p[i].y);
    }
    const int y0 = std::max(0, static_cast<int>(std::ceil(minY)));
    const int y1 = std::min(c.h - 1, static_cast<int>(std::floor(maxY)));
    for (int y = y0; y <= y1; y++) {
        const float fy = static_cast<float>(y);
        float xl = 1e9f;
        float xr = -1e9f;
        for (int i = 0; i < n; i++) {
            const P2& e0 = p[i];
            const P2& e1 = p[(i + 1) % n];
            if ((fy < std::min(e0.y, e1.y)) || (fy > std::max(e0.y, e1.y)) || e0.y == e1.y) {
                continue;
            }
            const float x = e0.x + (fy - e0.y) * (e1.x - e0.x) / (e1.y - e0.y);
            xl = std::min(xl, x);
            xr = std::max(xr, x);
        }
        if (xl <= xr) {
            blendSpan(c, y, static_cast<int>(std::lround(xl)), static_cast<int>(std::lround(xr)), a);
        }
    }
}

void fillEllipse(const Canvas& c, P2 center, float rx, float ry, float a) {
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    const int y0 = static_cast<int>(std::ceil(center.y - ry));
    const int y1 = static_cast<int>(std::floor(center.y + ry));
    for (int y = y0; y <= y1; y++) {
        const float t = (static_cast<float>(y) - center.y) / ry;
        const float half = rx * std::sqrt(std::max(0.0f, 1.0f - t * t));
        blendSpan(c, y, static_cast<int>(std::lround(center.x - half)), static_cast<int>(std::lround(center.x + half)), a);
    }
}

void thickLine(const Canvas& c, P2 p0, P2 p1, float width, float a) {
    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) {
        return;
    }
    const float nx = -dy / len * width * 0.5f;
    const float ny = dx / len * width * 0.5f;
    const P2 q[4] = {{p0.x + nx, p0.y + ny}, {p1.x + nx, p1.y + ny}, {p1.x - nx, p1.y - ny}, {p0.x - nx, p0.y - ny}};
    fillConvex(c, q, 4, a);
}

bool at(float x, float y, float z, P2& out, float* ppm = nullptr) {
    float sx = 0.0f;
    float sy = 0.0f;
    float scale = 0.0f;
    if (!rower_scene::project(x, y, z, sx, sy, scale)) {
        return false;
    }
    out = {sx * kScale, sy * kScale};
    if (ppm != nullptr) {
        *ppm = scale * kScale;
    }
    return true;
}

} // namespace

void draw(lv_draw_buf_t* frame, float gapMeters, float strokePhase) {
    if (frame == nullptr || frame->data == nullptr) {
        return;
    }
    const Canvas c{reinterpret_cast<uint16_t*>(frame->data), static_cast<int>(frame->header.w),
                   static_cast<int>(frame->header.h), static_cast<int>(frame->header.stride / sizeof(uint16_t))};
    const float seatZ = gapMeters + rower_scene::kRowerZ;
    P2 seat;
    float ppm = 0.0f;
    if (!at(kLaneX, 0.0f, seatZ, seat, &ppm)) {
        return;
    }

    // Hull: a long narrow wedge on the water, stern 1.6 m behind the seat.
    P2 hull[4];
    const float sternZ = seatZ - 1.6f;
    const float bowZ = seatZ + 6.4f;
    if (at(kLaneX - 0.22f, 0.1f, std::max(sternZ, seatZ - 0.6f), hull[0]) &&
        at(kLaneX + 0.22f, 0.1f, std::max(sternZ, seatZ - 0.6f), hull[1]) && at(kLaneX + 0.02f, 0.1f, bowZ, hull[2]) &&
        at(kLaneX - 0.02f, 0.1f, bowZ, hull[3])) {
        fillConvex(c, hull, 4, kAlpha * 0.8f);
    }

    // Oars: swing from the catch (blades toward the bow) to the finish.
    const float swing = std::sin(strokePhase * 6.2831853f);
    const float bladeZ = seatZ + 0.35f + 0.55f * swing;
    const float lineW = std::max(1.5f, 0.05f * ppm);
    for (int side = -1; side <= 1; side += 2) {
        P2 gate;
        P2 blade;
        if (at(kLaneX + 0.8f * side, 0.35f, seatZ + 0.35f, gate) && at(kLaneX + 2.35f * side, 0.0f, bladeZ, blade)) {
            P2 hands = gate;
            at(kLaneX + 0.25f * side, 0.6f, seatZ + 0.1f, hands);
            thickLine(c, hands, blade, lineW, kAlpha);
            fillEllipse(c, blade, 0.12f * ppm, 0.04f * ppm, kAlpha);
        }
    }

    // Rower: torso leaning with the stroke, then the head.
    const float lean = -0.15f * swing;
    P2 torso;
    P2 head;
    if (at(kLaneX, 0.7f, seatZ + lean, torso) && at(kLaneX, 1.12f, seatZ + lean * 1.4f, head)) {
        fillEllipse(c, torso, 0.2f * ppm, 0.3f * ppm, kAlpha);
        fillEllipse(c, head, 0.11f * ppm, 0.12f * ppm, kAlpha);
    }
}

} // namespace pace_boat
