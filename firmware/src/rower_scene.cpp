#include "rower_scene.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace rower_scene {

namespace {

LGFX_Sprite* g_canvas = nullptr;

constexpr float kCamZ = -0.95f;
constexpr float kCamY = 0.98f;
constexpr float kPi = 3.14159265f;
constexpr float kTau = kPi * 2.0f;

struct RGB565 {
    uint16_t v;
};

RGB565 color565(LGFX_Sprite* c, uint8_t r, uint8_t g, uint8_t b) {
    return RGB565{c->color565(r, g, b)};
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

uint16_t lerpColor565(LGFX_Sprite* c, uint16_t c0, uint16_t c1, float t) {
    t = clamp01(t);
    const int r0 = (c0 >> 11) & 31;
    const int g0 = (c0 >> 5) & 63;
    const int b0 = c0 & 31;
    const int r1 = (c1 >> 11) & 31;
    const int g1 = (c1 >> 5) & 63;
    const int b1 = c1 & 31;
    const int r = static_cast<int>(lerp(static_cast<float>(r0), static_cast<float>(r1), t));
    const int g = static_cast<int>(lerp(static_cast<float>(g0), static_cast<float>(g1), t));
    const int b = static_cast<int>(lerp(static_cast<float>(b0), static_cast<float>(b1), t));
    return c->color565(static_cast<uint8_t>(r << 3), static_cast<uint8_t>(g << 2), static_cast<uint8_t>(b << 3));
}

struct Camera {
    int w = kWidth;
    int h = kHeight;
    float focal = 280.0f;
    float horizon = 28.0f;

    void project(float x, float y, float z, float& sx, float& sy, float& ppm) const {
        float zc = z - kCamZ;
        if (zc < 0.32f) {
            zc = 0.32f;
        }
        ppm = focal / zc;
        sx = w * 0.5f + x * ppm;
        sy = horizon + (kCamY - y) * ppm;
    }
};

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

bool ensureCanvas() {
    if (g_canvas != nullptr) {
        return true;
    }
    g_canvas = new LGFX_Sprite();
    g_canvas->setPsram(true);
    // Match LVGL LV_COLOR_FORMAT_RGB565 (RRRRRGGG GGGBBBBB). LGFX's default
    // rgb565_2Byte stores bytes swapped and produces neon banding when blitted.
    g_canvas->setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
    if (g_canvas->createSprite(kWidth, kHeight) == nullptr) {
        delete g_canvas;
        g_canvas = nullptr;
        printf("rower_scene: sprite alloc failed\n");
        return false;
    }
    return true;
}

void fillVerticalGradient(LGFX_Sprite* c, uint16_t top, uint16_t bottom) {
    for (int y = 0; y < kHeight; y++) {
        const float t = static_cast<float>(y) / static_cast<float>(kHeight - 1);
        const uint16_t col = lerpColor565(c, top, bottom, t);
        c->drawFastHLine(0, y, kWidth, col);
    }
}

float waterY(float x, float z, float t) {
    // Smaller amplitudes + higher spatial freq → finer ripples, fewer blotches.
    return 0.010f * std::sin(x * 1.55f + z * 0.95f + t * kTau)
         + 0.006f * std::sin(x * 3.1f - z * 1.7f + t * kTau * 1.6f)
         + 0.0035f * std::sin(x * 4.6f + z * 2.6f - t * kTau * 0.8f);
}

float hullHalfBeam(float z) {
    const float u = clamp01(z / 8.2f);
    if (u < 0.32f) {
        const float tt = u / 0.32f;
        return lerp(0.05f, 0.29f, std::sin(tt * kPi * 0.5f));
    }
    const float tt = (u - 0.32f) / 0.68f;
    return lerp(0.29f, 0.02f, std::pow(tt, 0.65f));
}

void drawThickLine(LGFX_Sprite* c, float x0, float y0, float x1, float y1, int width, uint16_t color) {
    width = width < 1 ? 1 : width;
    c->drawWideLine(x0, y0, x1, y1, width, color);
}

void fillPoly(LGFX_Sprite* c, const std::vector<Vec2>& pts, uint16_t color) {
    if (pts.size() < 3) {
        return;
    }
    const Vec2 anchor = pts[0];
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        c->fillTriangle(
            static_cast<int>(anchor.x), static_cast<int>(anchor.y),
            static_cast<int>(pts[i].x), static_cast<int>(pts[i].y),
            static_cast<int>(pts[i + 1].x), static_cast<int>(pts[i + 1].y),
            color
        );
    }
}

void poly3(LGFX_Sprite* c, const Camera& cam, const std::vector<Vec3>& pts, uint16_t color) {
    std::vector<Vec2> projected;
    projected.reserve(pts.size());
    for (const Vec3& p : pts) {
        float sx, sy, ppm;
        cam.project(p.x, p.y, p.z, sx, sy, ppm);
        (void)ppm;
        projected.push_back({sx, sy});
    }
    fillPoly(c, projected, color);
}

void line3(LGFX_Sprite* c, const Camera& cam, Vec3 a, Vec3 b, uint16_t color, float widthM) {
    float sx0, sy0, ppm0;
    float sx1, sy1, ppm1;
    cam.project(a.x, a.y, a.z, sx0, sy0, ppm0);
    cam.project(b.x, b.y, b.z, sx1, sy1, ppm1);
    const int width = static_cast<int>(0.5f * (ppm0 + ppm1) * widthM);
    drawThickLine(c, sx0, sy0, sx1, sy1, width < 1 ? 1 : width, color);
}

void ellipseAt(LGFX_Sprite* c, const Camera& cam, float x, float y, float z, float rx, float ry, uint16_t color) {
    float sx, sy, ppm;
    cam.project(x, y, z, sx, sy, ppm);
    const int w = static_cast<int>(rx * ppm * 2.0f);
    const int h = static_cast<int>(ry * ppm * 2.0f);
    if (w < 2 || h < 2) {
        return;
    }
    c->fillEllipse(static_cast<int>(sx), static_cast<int>(sy), w / 2, h / 2, color);
}

void drawSky(LGFX_Sprite* c, const Camera& cam) {
    const uint16_t sunCore = color565(c, 0xF4, 0xDC, 0xA0).v;
    const uint16_t hillFar = color565(c, 0x12, 0x2A, 0x36).v;
    const uint16_t hillNear = color565(c, 0x0C, 0x1C, 0x26).v;

    const int sunX = static_cast<int>(cam.w * 0.22f);
    const int sunY = static_cast<int>(cam.horizon - 12);
    c->fillCircle(sunX, sunY, 8, sunCore);

    for (int band = 0; band < 2; band++) {
        const float amp = band == 0 ? 17.0f : 12.0f;
        const float k = band == 0 ? 0.0055f : 0.0095f;
        const float phase = band == 0 ? 0.3f : 1.6f;
        const float yOff = band == 0 ? -9.0f : 0.0f;
        const uint16_t col = band == 0 ? hillFar : hillNear;
        std::vector<Vec2> pts;
        pts.push_back({0.0f, static_cast<float>(cam.h)});
        for (int x = 0; x <= cam.w; x += 6) {
            const float y = cam.horizon + yOff + amp * std::sin(x * k + phase) + 3.0f * std::sin(x * k * 2.3f + phase);
            pts.push_back({static_cast<float>(x), y});
        }
        pts.push_back({static_cast<float>(cam.w), static_cast<float>(cam.h)});
        fillPoly(c, pts, col);
    }
}

void drawWater(LGFX_Sprite* c, const Camera& cam, float t) {
    const uint16_t waterSurface = color565(c, 0x1B, 0x4E, 0x66).v;
    const uint16_t waterDeep = color565(c, 0x03, 0x08, 0x0C).v;
    const uint16_t foam = color565(c, 0xD0, 0xEA, 0xF0).v;

    const int waterTop = static_cast<int>(cam.horizon);
    for (int y = waterTop; y < cam.h; y++) {
        const float tt = static_cast<float>(y - waterTop) / static_cast<float>(cam.h - waterTop - 1);
        c->drawFastHLine(0, y, cam.w, lerpColor565(c, waterSurface, waterDeep, tt));
    }

    const float zs[] = {18, 14, 11, 8.5f, 6.5f, 5.0f, 3.8f, 2.9f, 2.2f, 1.7f, 1.25f, 0.95f, 0.72f, 0.55f, 0.42f};
    const int zCount = static_cast<int>(sizeof(zs) / sizeof(zs[0]));
    for (int i = 0; i < zCount - 1; i++) {
        const float z0 = zs[i];
        const float z1 = zs[i + 1];
        std::vector<Vec2> top;
        std::vector<Vec2> bot;
        for (int xi = 0; xi < 31; xi++) {
            const float x = xi * 0.7f - 10.5f;
            float sx0, sy0, ppm0;
            float sx1, sy1, ppm1;
            cam.project(x, waterY(x, z0, t), z0, sx0, sy0, ppm0);
            cam.project(x, waterY(x, z1, t), z1, sx1, sy1, ppm1);
            (void)ppm0;
            (void)ppm1;
            top.push_back({sx0, sy0});
            bot.push_back({sx1, sy1});
        }
        const float depth = static_cast<float>(i) / static_cast<float>(zCount - 2);
        // Soft band tint only — avoid bright foam fills that read as large blotches.
        const uint16_t band = lerpColor565(c, waterSurface, foam, 0.06f + 0.10f * (1.0f - depth));
        std::vector<Vec2> poly = top;
        for (int j = static_cast<int>(bot.size()) - 1; j >= 0; j--) {
            poly.push_back(bot[static_cast<size_t>(j)]);
        }
        fillPoly(c, poly, band);
        for (size_t j = 1; j < top.size(); j++) {
            drawThickLine(c, top[j - 1].x, top[j - 1].y, top[j].x, top[j].y, 1, foam);
        }
    }

    // Sparse small sparkles (was ppm*0.07 / min 3.5 → large unclean spots).
    for (int i = 0; i < 48; i++) {
        const float s1 = std::sin((i + 1) * 12.9898f) * 43758.5453f;
        const float s2 = std::sin((i + 1) * 78.233f) * 23421.631f;
        const float fx = s1 - std::floor(s1);
        const float fz = s2 - std::floor(s2);
        const float x = (fx - 0.5f) * 8.0f;
        const float z = 0.45f + fz * 9.5f;
        const float tw = 0.5f + 0.5f * std::sin(t * kTau * 1.15f + i * 0.63f);
        if (tw < 0.55f) {
            continue;
        }
        float sx, sy, ppm;
        cam.project(x, 0.02f, z, sx, sy, ppm);
        float r = ppm * 0.022f;
        if (r < 1.2f) {
            r = 1.2f;
        } else if (r > 3.0f) {
            r = 3.0f;
        }
        c->fillEllipse(static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(r), static_cast<int>(r * 0.5f), foam);
    }

    // Thin traveling glints (was ppm*0.16 / min 8 → big ovals).
    for (int i = 0; i < 10; i++) {
        const float u = std::fmod(i / 10.0f + t * 0.35f, 1.0f);
        const float x = (u - 0.5f) * 6.5f;
        const float z = 1.1f + 7.0f * std::fmod(i * 0.37f + t, 1.0f);
        float sx, sy, ppm;
        cam.project(x, 0.025f, z, sx, sy, ppm);
        float rw = ppm * 0.045f;
        float rh = ppm * 0.014f;
        if (rw < 2.0f) {
            rw = 2.0f;
        } else if (rw > 5.0f) {
            rw = 5.0f;
        }
        if (rh < 1.0f) {
            rh = 1.0f;
        } else if (rh > 2.0f) {
            rh = 2.0f;
        }
        c->fillEllipse(static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(rw), static_cast<int>(rh), foam);
    }
}

void drawWake(LGFX_Sprite* c, const Camera& cam, float t, float vigor) {
    const uint16_t foam = color565(c, 0xD0, 0xEA, 0xF0).v;
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < 6; i++) {
            const float u = std::fmod(t * 8.0f + i, 8.0f) / 8.0f;
            const float z = lerp(0.35f, -0.4f, u);
            const float x = side * (0.12f + u * 0.55f);
            const float y = 0.03f + 0.02f * std::sin(u * 8.0f + t * kTau);
            float sx, sy, ppm;
            cam.project(x, y, z, sx, sy, ppm);
            float rw = ppm * (0.035f + u * 0.06f);
            float rh = ppm * 0.015f;
            if (rw < 1.5f) {
                rw = 1.5f;
            } else if (rw > 4.0f) {
                rw = 4.0f;
            }
            if (rh < 1.0f) {
                rh = 1.0f;
            } else if (rh > 2.0f) {
                rh = 2.0f;
            }
            c->fillEllipse(static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(rw), static_cast<int>(rh), foam);
            (void)vigor;
        }
    }
}

void drawHull(LGFX_Sprite* c, const Camera& cam) {
    const uint16_t hullSide = color565(c, 0xC8, 0xD2, 0xDA).v;
    const uint16_t hullShadow = color565(c, 0x7A, 0x88, 0x96).v;
    const uint16_t deck = color565(c, 0xE4, 0xEA, 0xF0).v;
    const uint16_t accent = color565(c, 0x3D, 0xE8, 0xB8).v;
    const uint16_t cockpit = color565(c, 0x2A, 0x36, 0x42).v;
    const uint16_t rigger = color565(c, 0x4A, 0x56, 0x64).v;

    std::vector<Vec3> deckL, deckR, sideL, sideR;
    for (int i = 0; i < 24; i++) {
        const float z = i * 0.35f;
        const float hw = hullHalfBeam(z);
        const float deckY = 0.11f + 0.02f * (1.0f - z / 8.2f);
        const float keelY = -0.02f;
        deckL.push_back({-hw * 0.92f, deckY, z});
        deckR.push_back({hw * 0.92f, deckY, z});
        sideL.push_back({-hw, lerp(keelY, deckY, 0.35f), z});
        sideR.push_back({hw, lerp(keelY, deckY, 0.35f), z});
    }

    std::vector<Vec3> sidePoly = sideL;
    for (int i = static_cast<int>(deckL.size()) - 1; i >= 0; i--) {
        sidePoly.push_back(deckL[static_cast<size_t>(i)]);
    }
    poly3(c, cam, sidePoly, hullSide);

    std::vector<Vec3> sideRPoly = sideR;
    for (int i = static_cast<int>(deckR.size()) - 1; i >= 0; i--) {
        sideRPoly.push_back(deckR[static_cast<size_t>(i)]);
    }
    poly3(c, cam, sideRPoly, hullShadow);

    std::vector<Vec3> deckPoly = deckL;
    for (int i = static_cast<int>(deckR.size()) - 1; i >= 0; i--) {
        deckPoly.push_back(deckR[static_cast<size_t>(i)]);
    }
    poly3(c, cam, deckPoly, deck);

    line3(c, cam, deckL[1], deckL[deckL.size() - 4], accent, 0.012f);
    line3(c, cam, deckR[1], deckR[deckR.size() - 4], accent, 0.012f);

    poly3(
        c, cam,
        {
            {-0.10f, 0.105f, 1.05f},
            {0.10f, 0.105f, 1.05f},
            {0.09f, 0.10f, 2.55f},
            {-0.09f, 0.10f, 2.55f},
        },
        cockpit
    );
    line3(c, cam, {-0.055f, 0.112f, 1.15f}, {-0.05f, 0.108f, 2.45f}, rigger, 0.018f);
    line3(c, cam, {0.055f, 0.112f, 1.15f}, {0.05f, 0.108f, 2.45f}, rigger, 0.018f);
}

void drawOars(LGFX_Sprite* c, const Camera& cam, float drive, bool recovering) {
    const uint16_t wood = color565(c, 0xC4, 0x8A, 0x4A).v;
    const uint16_t woodDark = color565(c, 0x5A, 0x34, 0x18).v;
    const uint16_t rigger = color565(c, 0x4A, 0x56, 0x64).v;
    const uint16_t blade = color565(c, 0x2A, 0x34, 0x3C).v;

    const float bladeZ = lerp(4.05f, 0.72f, drive);
    const float bladeX = lerp(1.50f, 0.92f, drive);
    const float bladeY = recovering ? 0.32f : 0.02f;
    const float lockZ = 1.55f;
    const float lockY = 0.16f;
    const float handleZ = lerp(0.72f, 1.48f, drive);
    const float handleY = lerp(0.50f, 0.48f, drive);

    for (int side = -1; side <= 1; side += 2) {
        const Vec3 lock = {side * 0.40f, lockY, lockZ};
        const Vec3 bladePt = {side * bladeX, bladeY, bladeZ};
        const Vec3 handle = {side * 0.16f, handleY, handleZ};
        line3(c, cam, {side * 0.12f, 0.12f, lockZ}, lock, rigger, 0.03f);
        line3(c, cam, lock, {side * 0.40f, 0.02f, lockZ + 0.05f}, rigger, 0.02f);
        line3(c, cam, handle, lock, wood, 0.035f);
        line3(c, cam, lock, bladePt, recovering ? woodDark : wood, 0.04f);

        if (recovering) {
            poly3(
                c, cam,
                {
                    {bladePt.x - side * 0.04f, bladePt.y + 0.02f, bladePt.z - 0.08f},
                    {bladePt.x + side * 0.04f, bladePt.y + 0.02f, bladePt.z - 0.08f},
                    {bladePt.x + side * 0.05f, bladePt.y + 0.02f, bladePt.z + 0.22f},
                    {bladePt.x - side * 0.05f, bladePt.y + 0.02f, bladePt.z + 0.22f},
                },
                blade
            );
        } else {
            poly3(
                c, cam,
                {
                    {bladePt.x - side * 0.02f, bladePt.y + 0.07f, bladePt.z - 0.04f},
                    {bladePt.x + side * 0.02f, bladePt.y - 0.05f, bladePt.z - 0.04f},
                    {bladePt.x + side * 0.03f, bladePt.y - 0.05f, bladePt.z + 0.28f},
                    {bladePt.x - side * 0.03f, bladePt.y + 0.07f, bladePt.z + 0.28f},
                },
                blade
            );
        }
    }
}

void drawRower(LGFX_Sprite* c, const Camera& cam, float drive, bool recovering) {
    (void)recovering;
    const uint16_t seat = color565(c, 0x6A, 0x74, 0x80).v;
    const uint16_t kit = color565(c, 0x1A, 0x32, 0x42).v;
    const uint16_t kitLight = color565(c, 0x2E, 0x52, 0x64).v;
    const uint16_t skin = color565(c, 0xE4, 0xB6, 0x8C).v;
    const uint16_t skinShadow = color565(c, 0xB0, 0x7A, 0x54).v;
    const uint16_t hair = color565(c, 0x1C, 0x14, 0x10).v;
    const uint16_t woodDark = color565(c, 0x5A, 0x34, 0x18).v;
    // Rower faces bow (+Z). Camera sits at stern (−Z) and sees back/shoulders.

    const float zSeat = lerp(0.88f, 2.45f, drive);
    const Vec3 hip = {0.0f, 0.32f, zSeat};
    const float shZ = zSeat + lerp(-0.08f, 0.28f, drive);
    const float shY = lerp(0.82f, 0.54f, drive);
    const float headZ = shZ + lerp(-0.06f, 0.10f, drive);
    const float headY = shY + 0.24f;
    const float footZ = 2.70f;
    const float footY = 0.11f;
    const float kneeZ = lerp(zSeat + 0.10f, zSeat + 0.62f, drive);
    const float kneeY = lerp(0.58f, 0.22f, drive);

    poly3(
        c, cam,
        {
            {-0.09f, 0.14f, zSeat - 0.06f},
            {0.09f, 0.14f, zSeat - 0.06f},
            {0.09f, 0.14f, zSeat + 0.08f},
            {-0.09f, 0.14f, zSeat + 0.08f},
        },
        seat
    );

    for (int side = -1; side <= 1; side += 2) {
        const Vec3 hipS = {side * 0.08f, hip.y, hip.z};
        const Vec3 knee = {side * 0.13f, kneeY, kneeZ};
        const Vec3 foot = {side * 0.10f, footY, footZ};
        line3(c, cam, hipS, knee, kit, 0.10f);
        line3(c, cam, knee, foot, kit, 0.08f);
        ellipseAt(c, cam, knee.x, knee.y, knee.z, 0.08f, 0.07f, kitLight);
        ellipseAt(c, cam, foot.x, foot.y, foot.z, 0.06f, 0.03f, kitLight);
    }

    poly3(
        c, cam,
        {
            {-0.17f, hip.y + 0.02f, hip.z - 0.02f},
            {0.17f, hip.y + 0.02f, hip.z - 0.02f},
            {0.19f, shY, shZ - 0.02f},
            {-0.19f, shY, shZ - 0.02f},
        },
        kit
    );
    line3(c, cam, {0.0f, hip.y, hip.z - 0.02f}, {0.0f, shY, shZ - 0.02f}, kitLight, 0.04f);
    for (int side = -1; side <= 1; side += 2) {
        ellipseAt(c, cam, side * 0.17f, shY - 0.01f, shZ - 0.03f, 0.05f, 0.04f, kitLight);
    }

    // Back of head toward stern (−Z); no face/eyes toward camera.
    ellipseAt(c, cam, 0.0f, headY, headZ - 0.03f, 0.13f, 0.15f, skin);
    ellipseAt(c, cam, 0.0f, headY + 0.05f, headZ - 0.05f, 0.14f, 0.09f, hair);
    line3(c, cam, {0.0f, headY - 0.10f, headZ - 0.03f}, {0.0f, shY + 0.04f, shZ - 0.03f}, skinShadow, 0.035f);

    const float handleZ = lerp(zSeat - 0.48f, zSeat - 0.08f, drive);
    const float handleY = lerp(0.50f, 0.46f, drive);
    for (int side = -1; side <= 1; side += 2) {
        const Vec3 shoulder = {side * 0.16f, shY - 0.02f, shZ};
        const Vec3 handle = {side * 0.15f, handleY, handleZ};
        const float elbowZ = lerp(handleZ + 0.04f, (handleZ + shZ) * 0.5f, drive);
        const float elbowY = lerp(handleY - 0.02f, (handleY + shY) * 0.5f - 0.06f, drive);
        const Vec3 elbow = {side * 0.22f, elbowY, elbowZ};
        line3(c, cam, shoulder, elbow, skin, 0.06f);
        line3(c, cam, elbow, handle, skin, 0.055f);
        ellipseAt(c, cam, handle.x, handle.y, handle.z, 0.045f, 0.04f, skinShadow);
    }
    line3(c, cam, {-0.16f, handleY, handleZ}, {0.16f, handleY, handleZ}, woodDark, 0.03f);
}

void drawScene(LGFX_Sprite* c, float waterTime, float drive, bool recovering) {
    const Camera cam;
    const uint16_t skyTop = color565(c, 0x06, 0x0A, 0x12).v;
    const uint16_t skyHorizon = color565(c, 0x24, 0x48, 0x5C).v;
    const uint16_t hull = color565(c, 0xF2, 0xF5, 0xF8).v;

    fillVerticalGradient(c, skyTop, skyHorizon);
    drawSky(c, cam);
    drawWater(c, cam, waterTime);

    const float vigor = recovering ? 0.0f : (0.4f + 0.6f * std::sin(std::max(drive, 0.05f) * kPi));
    drawWake(c, cam, waterTime, vigor);

    if (drive < 0.65f) {
        drawOars(c, cam, drive, recovering);
        drawHull(c, cam);
        drawRower(c, cam, drive, recovering);
    } else {
        drawHull(c, cam);
        drawRower(c, cam, drive, recovering);
        drawOars(c, cam, drive, recovering);
    }

    poly3(
        c, cam,
        {
            {-0.04f, 0.08f, 0.05f},
            {0.04f, 0.08f, 0.05f},
            {0.02f, 0.02f, -0.05f},
            {-0.02f, 0.02f, -0.05f},
        },
        hull
    );
}

} // namespace

void draw(lv_draw_buf_t* dst, float drive, float waterTime, bool recovering) {
    if (dst == nullptr || dst->data == nullptr || !ensureCanvas()) {
        return;
    }

    drive = clamp01(drive);
    waterTime = std::fmod(waterTime, 1.0f);
    if (waterTime < 0.0f) {
        waterTime += 1.0f;
    }

    drawScene(g_canvas, waterTime, drive, recovering);

    const auto* src = static_cast<const uint8_t*>(g_canvas->getBuffer());
    if (src == nullptr) {
        return;
    }
    const size_t rowBytes = static_cast<size_t>(kWidth) * 2u;
    const size_t srcStride = g_canvas->bufferLength() / static_cast<uint32_t>(kHeight);
    const size_t dstStride = dst->header.stride;
    const int copyH = std::min(kHeight, static_cast<int>(dst->header.h));
    for (int y = 0; y < copyH; y++) {
        std::memcpy(
            dst->data + static_cast<size_t>(y) * dstStride,
            src + static_cast<size_t>(y) * srcStride,
            rowBytes
        );
    }
    lv_draw_buf_flush_cache(dst, nullptr);
}

} // namespace rower_scene
