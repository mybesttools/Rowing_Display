#include "rower_scene.h"

#include "scene_background.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rower_scene {

namespace {

// Frames are drawn at kSS× resolution into one scratch sprite, then box-
// filtered down to kWidth×kHeight with ordered dithering. That gives every
// edge (triangles and ellipses included, which LGFX doesn't anti-alias) a
// smooth ramp and hides RGB565 banding in the drawn shapes, at a
// boot-time-only cost. The sprite is freed by releaseCanvas() once the
// frame set is precomputed.
constexpr int kSS = 2;
constexpr int kCW = kWidth * kSS;
constexpr int kCH = kHeight * kSS;

constexpr float kPi = 3.14159265f;
constexpr float kTau = kPi * 2.0f;

LGFX_Sprite* g_canvas = nullptr;
const scene_background::Background* g_bg = &scene_background::kDefault;

// ---------------------------------------------------------------------------
// Small math/color helpers

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

float smoothstep(float e0, float e1, float x) {
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

float hash01(int i) {
    const float s = std::sin(static_cast<float>(i) * 12.9898f + 78.233f) * 43758.5453f;
    return s - std::floor(s);
}

struct Rgb {
    float r;
    float g;
    float b;
};

constexpr Rgb rgb(uint32_t hex) {
    return {static_cast<float>((hex >> 16) & 0xFF), static_cast<float>((hex >> 8) & 0xFF), static_cast<float>(hex & 0xFF)};
}

Rgb mix(Rgb a, Rgb b, float t) {
    t = clamp01(t);
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}

uint16_t pack(Rgb c) {
    const auto ch = [](float v) { return static_cast<uint32_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v)); };
    return static_cast<uint16_t>(((ch(c.r) & 0xF8) << 8) | ((ch(c.g) & 0xFC) << 3) | (ch(c.b) >> 3));
}

// Palette for everything drawn over the scene_background photo. The sun
// glints and ripple crests take their colours from the selected background.
constexpr Rgb kWaterMid = rgb(0x173A4C);
constexpr Rgb kWaterDeep = rgb(0x06131C);
constexpr Rgb kFoam = rgb(0xE6F4F8);
constexpr Rgb kHullLit = rgb(0xF4F7FA);
constexpr Rgb kHullShade = rgb(0xAEBAC6);
constexpr Rgb kHullDark = rgb(0x6F7D8A);
constexpr Rgb kCockpit = rgb(0x26313C);
constexpr Rgb kAccent = rgb(0x3DE8B8);
constexpr Rgb kMetal = rgb(0x55616D);
constexpr Rgb kShaft = rgb(0xE9EDF1);
constexpr Rgb kShaftShade = rgb(0xB3BCC5);
constexpr Rgb kGrip = rgb(0x2A2F36);
constexpr Rgb kKit = rgb(0x1C3A4E);
constexpr Rgb kKitShade = rgb(0x12283A);
constexpr Rgb kSkin = rgb(0xE2B48C);
constexpr Rgb kSkinShade = rgb(0xB58563);
constexpr Rgb kHair = rgb(0x221812);
constexpr Rgb kSeat = rgb(0x3A4450);

// ---------------------------------------------------------------------------
// Camera: sits behind and above the stern, looking toward the bow (+Z).
// Units are metres; y=0 is the waterline. All screen values are canvas
// pixels (already kSS-scaled).

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vec3 vlerp(Vec3 a, Vec3 b, float t) {
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)};
}

struct Pt {
    float x;
    float y;
};

struct Camera {
    float camZ = -2.1f;
    float camY = 1.45f;
    float focal = 300.0f * kSS;
    // Matches the waterline baked into scene_background.
    float horizon = scene_background::kHorizonY * kSS;

    Pt project(Vec3 p, float* ppmOut = nullptr) const {
        float zc = p.z - camZ;
        if (zc < 0.3f) {
            zc = 0.3f;
        }
        const float ppm = focal / zc;
        if (ppmOut != nullptr) {
            *ppmOut = ppm;
        }
        return {kCW * 0.5f + p.x * ppm, horizon + (camY - p.y) * ppm};
    }

    float ppmAt(float z) const {
        const float zc = std::max(z - camZ, 0.3f);
        return focal / zc;
    }
};

// ---------------------------------------------------------------------------
// Primitive wrappers (all coordinates in canvas pixels)

uint16_t* pixels() {
    return static_cast<uint16_t*>(g_canvas->getBuffer());
}

void blendPixel(int x, int y, Rgb c, float a) {
    if (x < 0 || y < 0 || x >= kCW || y >= kCH || a <= 0.0f) {
        return;
    }
    uint16_t& d = pixels()[static_cast<size_t>(y) * kCW + x];
    const Rgb dst = {static_cast<float>((d >> 11) & 31) * (255.0f / 31.0f), static_cast<float>((d >> 5) & 63) * (255.0f / 63.0f),
                     static_cast<float>(d & 31) * (255.0f / 31.0f)};
    d = pack(mix(dst, c, a));
}

// Anti-aliased horizontal stroke `width` px tall centred on y. Much cheaper
// than drawWideLine for the hundreds of ripple/glint strokes per frame.
void hstroke(float x0, float x1, float y, float width, Rgb col, float alpha) {
    const float top = y - width * 0.5f;
    const float bot = y + width * 0.5f;
    const int xs = static_cast<int>(std::floor(x0));
    const int xe = static_cast<int>(std::ceil(x1));
    for (int yi = static_cast<int>(std::floor(top)); yi < bot; yi++) {
        const float vcov = std::min(bot, static_cast<float>(yi + 1)) - std::max(top, static_cast<float>(yi));
        if (vcov <= 0.0f) {
            continue;
        }
        for (int x = xs; x < xe; x++) {
            const float hcov = std::min(x1, static_cast<float>(x + 1)) - std::max(x0, static_cast<float>(x));
            blendPixel(x, yi, col, alpha * vcov * clamp01(hcov));
        }
    }
}

void tri(Pt a, Pt b, Pt c, Rgb col) {
    g_canvas->fillTriangle(
        static_cast<int32_t>(std::lround(a.x)), static_cast<int32_t>(std::lround(a.y)), static_cast<int32_t>(std::lround(b.x)),
        static_cast<int32_t>(std::lround(b.y)), static_cast<int32_t>(std::lround(c.x)), static_cast<int32_t>(std::lround(c.y)), pack(col)
    );
}

// Convex polygon only (fan triangulation).
void convex(const Pt* pts, int n, Rgb col) {
    for (int i = 1; i + 1 < n; i++) {
        tri(pts[0], pts[i], pts[i + 1], col);
    }
}

void convex3(const Camera& cam, const Vec3* pts, int n, Rgb col) {
    Pt proj[12];
    n = std::min(n, 12);
    for (int i = 0; i < n; i++) {
        proj[i] = cam.project(pts[i]);
    }
    convex(proj, n, col);
}

// Anti-aliased capsule; `radius` in canvas pixels.
void capsule(Pt a, Pt b, float radius, Rgb col) {
    g_canvas->drawWideLine(
        static_cast<int32_t>(std::lround(a.x)), static_cast<int32_t>(std::lround(a.y)), static_cast<int32_t>(std::lround(b.x)),
        static_cast<int32_t>(std::lround(b.y)), std::max(radius, 0.6f), pack(col)
    );
}

// 3D capsule; `radiusM` in metres, scaled by perspective at each end.
void limb(const Camera& cam, Vec3 a, Vec3 b, float radiusM, Rgb col) {
    float ppmA = 0.0f;
    float ppmB = 0.0f;
    const Pt pa = cam.project(a, &ppmA);
    const Pt pb = cam.project(b, &ppmB);
    capsule(pa, pb, 0.5f * (ppmA + ppmB) * radiusM, col);
}

void ellipse(Pt c, float rx, float ry, Rgb col) {
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    g_canvas->fillEllipse(
        static_cast<int32_t>(std::lround(c.x)), static_cast<int32_t>(std::lround(c.y)), static_cast<int32_t>(std::lround(rx)),
        static_cast<int32_t>(std::lround(ry)), pack(col)
    );
}

void ball(const Camera& cam, Vec3 c, float rxM, float ryM, Rgb col) {
    float ppm = 0.0f;
    const Pt p = cam.project(c, &ppm);
    ellipse(p, rxM * ppm, ryM * ppm, col);
}

void drawWaterDetail(const Camera& cam, float t) {
    // Sun path: broken glints on the water below the sun.
    const float sunX = g_bg->sunX * kSS;
    const Rgb sun = rgb(g_bg->sunColor);
    const Rgb ripple = rgb(g_bg->rippleColor);
    for (int i = 0; i < 18; i++) {
        const float z = 2.5f + 22.0f * std::pow(static_cast<float>(i) / 18.0f, 1.6f);
        float ppm = 0.0f;
        const Pt p = cam.project({0.0f, 0.0f, z}, &ppm);
        const float wob = std::sin(t * kTau + static_cast<float>(i) * 1.7f);
        const float halfLen = (0.35f + 0.25f * hash01(i + 300)) * ppm * (0.6f + 0.4f * wob);
        const float x = sunX + (hash01(i + 500) - 0.5f) * 0.5f * ppm;
        const float fade = 1.0f - static_cast<float>(i) / 30.0f;
        if (halfLen > 1.0f && g_bg->glint > 0.0f) {
            hstroke(x - halfLen, x + halfLen, p.y, 1.4f * kSS, sun, 0.45f * fade * g_bg->glint);
        }
    }

    // Ripple crests: short strokes spread evenly over the screen rows (so
    // perspective thins them out naturally with distance). Each one only
    // shimmers in place with its own phase - no translation - so every
    // frame count loops seamlessly and there is no repeating tile pattern.
    constexpr int kRipples = 340;
    for (int i = 0; i < kRipples; i++) {
        const float sy = cam.horizon + 3.0f * kSS + hash01(i * 4 + 1) * (static_cast<float>(kCH) - cam.horizon);
        const float ppm = (sy - cam.horizon) / cam.camY;
        const float sx = hash01(i * 4 + 2) * kCW;
        const float phase = hash01(i * 4 + 3) * kTau;
        const float a = 0.5f + 0.5f * std::sin(t * kTau + phase);
        const float drift = 0.04f * ppm * std::sin(t * kTau + phase * 1.7f);
        const float halfLen = (0.08f + 0.20f * hash01(i * 4 + 4)) * ppm * (0.55f + 0.45f * a);
        const float width = std::max(1.0f, 0.020f * ppm);
        hstroke(sx + drift - halfLen, sx + drift + halfLen, sy, width, ripple, 0.10f + 0.32f * a);
    }
}

// ---------------------------------------------------------------------------
// Boat

float hullHalfBeam(float z) {
    // Stern at z=0 (towards the camera), bow at z=8.
    const float u = clamp01(z / 8.0f);
    if (u < 0.3f) {
        return lerp(0.10f, 0.23f, std::sin(u / 0.3f * kPi * 0.5f));
    }
    return lerp(0.23f, 0.015f, std::pow((u - 0.3f) / 0.7f, 0.8f));
}

constexpr float kDeckY = 0.12f;

void drawHullShadow(const Camera& cam) {
    // Dark reflection / shadow on the water, slightly wider than the hull.
    constexpr int kN = 14;
    for (int i = 0; i + 1 < kN; i++) {
        const float z0 = static_cast<float>(i) * 8.0f / (kN - 1);
        const float z1 = static_cast<float>(i + 1) * 8.0f / (kN - 1);
        const float w0 = hullHalfBeam(z0) * 1.55f;
        const float w1 = hullHalfBeam(z1) * 1.55f;
        const Vec3 q[4] = {{-w0, 0.0f, z0}, {w0, 0.0f, z0}, {w1, 0.0f, z1}, {-w1, 0.0f, z1}};
        convex3(cam, q, 4, mix(kWaterMid, kWaterDeep, 0.75f));
    }
}

void drawHull(const Camera& cam, float t) {
    constexpr int kN = 24;
    for (int i = kN - 2; i >= 0; i--) {
        const float z0 = static_cast<float>(i) * 8.0f / (kN - 1);
        const float z1 = static_cast<float>(i + 1) * 8.0f / (kN - 1);
        const float w0 = hullHalfBeam(z0);
        const float w1 = hullHalfBeam(z1);
        // Sides (left lit by the sun, right in shade), then the deck.
        const Vec3 left[4] = {{-w0, 0.0f, z0}, {-w0 * 0.92f, kDeckY, z0}, {-w1 * 0.92f, kDeckY, z1}, {-w1, 0.0f, z1}};
        const Vec3 right[4] = {{w0, 0.0f, z0}, {w0 * 0.92f, kDeckY, z0}, {w1 * 0.92f, kDeckY, z1}, {w1, 0.0f, z1}};
        const Vec3 deck[4] = {{-w0 * 0.92f, kDeckY, z0}, {w0 * 0.92f, kDeckY, z0}, {w1 * 0.92f, kDeckY, z1}, {-w1 * 0.92f, kDeckY, z1}};
        convex3(cam, left, 4, kHullShade);
        convex3(cam, right, 4, kHullDark);
        convex3(cam, deck, 4, kHullLit);
    }

    // Waterline foam along both sides, flickering with the water phase.
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < 18; i++) {
            const float z0 = 0.2f + static_cast<float>(i) * 0.42f;
            const float z1 = z0 + 0.28f;
            const float a = 0.5f + 0.5f * std::sin(t * kTau * 2.0f + static_cast<float>(i) * 1.3f + side);
            limb(cam, {side * hullHalfBeam(z0) * 1.04f, 0.005f, z0}, {side * hullHalfBeam(z1) * 1.04f, 0.005f, z1}, 0.012f,
                 mix(kWaterMid, kFoam, 0.35f + 0.4f * a));
        }
    }

    // Accent pin-stripes along the gunwales and the open cockpit.
    limb(cam, {-hullHalfBeam(0.3f) * 0.9f, kDeckY + 0.002f, 0.3f}, {-hullHalfBeam(6.5f) * 0.9f, kDeckY + 0.002f, 6.5f}, 0.008f, kAccent);
    limb(cam, {hullHalfBeam(0.3f) * 0.9f, kDeckY + 0.002f, 0.3f}, {hullHalfBeam(6.5f) * 0.9f, kDeckY + 0.002f, 6.5f}, 0.008f, kAccent);
    const Vec3 cockpit[4] = {{-0.15f, kDeckY + 0.003f, 0.95f}, {0.15f, kDeckY + 0.003f, 0.95f}, {0.13f, kDeckY + 0.003f, 2.55f},
                             {-0.13f, kDeckY + 0.003f, 2.55f}};
    convex3(cam, cockpit, 4, kCockpit);
    // Slide rails.
    limb(cam, {-0.07f, kDeckY + 0.02f, 1.0f}, {-0.07f, kDeckY + 0.02f, 2.1f}, 0.012f, kMetal);
    limb(cam, {0.07f, kDeckY + 0.02f, 1.0f}, {0.07f, kDeckY + 0.02f, 2.1f}, 0.012f, kMetal);
}

// ---------------------------------------------------------------------------
// Rower + oars. Stylised to match the stock console: seen from behind, the
// rower faces the bow (+Z). drive: 0 = catch (compressed, reaching forward),
// 1 = finish (legs flat, leaning back, handles at the ribs).

struct Pose {
    float seatZ;
    Vec3 hip;
    Vec3 knee[2];
    Vec3 foot[2];
    Vec3 shoulderC;
    Vec3 shoulder[2];
    Vec3 head;
    Vec3 elbow[2];
    Vec3 hand[2];
    Vec3 lock[2];
    Vec3 blade[2];
    Vec3 bladeDir[2];
    bool bladeInWater;
};

constexpr float kFootZ = 2.30f;
constexpr float kFootY = 0.20f;
constexpr float kThigh = 0.46f;
constexpr float kShin = 0.46f;
constexpr float kTorso = 0.58f;
constexpr float kLockZ = 1.95f;
constexpr float kLockX = 0.80f;
constexpr float kOutboard = 1.55f;

// Knee position by two-bone IK in the y/z plane, bending upward.
Vec3 kneeFor(Vec3 hip, Vec3 foot) {
    const float dz = foot.z - hip.z;
    const float dy = foot.y - hip.y;
    const float d = std::sqrt(dz * dz + dy * dy);
    const float half = std::min(d * 0.5f, kThigh * 0.999f);
    const float up = std::sqrt(std::max(kThigh * kThigh - half * half, 0.0f));
    // Perpendicular to hip->foot, pointing up.
    const float nz = -dy / d;
    const float ny = dz / d;
    return {lerp(hip.x, foot.x, 0.5f), hip.y + dy * 0.5f + ny * up, hip.z + dz * 0.5f + nz * up};
}

Pose solvePose(float drive, bool recovering) {
    // Stroke sequencing: legs, then body swing, then arms (reversed on the
    // recovery simply by running drive back from 1 to 0).
    const float legs = smoothstep(0.0f, 0.6f, drive);
    const float body = smoothstep(0.25f, 0.8f, drive);
    const float arms = smoothstep(0.55f, 1.0f, drive);

    Pose p{};
    p.seatZ = lerp(kFootZ - 0.44f, kFootZ - 0.90f, legs);
    p.hip = {0.0f, 0.34f, p.seatZ};
    for (int s = 0; s < 2; s++) {
        const float side = s == 0 ? -1.0f : 1.0f;
        p.foot[s] = {side * 0.12f, kFootY, kFootZ};
        p.knee[s] = kneeFor({side * 0.11f, p.hip.y, p.hip.z}, p.foot[s]);
        p.knee[s].x = side * 0.13f;
    }

    const float lean = lerp(0.45f, -0.35f, body);  // radians, + = toward bow
    const Vec3 up = {0.0f, std::cos(lean), std::sin(lean)};
    p.shoulderC = p.hip + up * kTorso;
    p.head = p.shoulderC + up * 0.22f;

    const Vec3 reachStraight = {0.0f, -0.20f, 0.60f};
    const Vec3 reachBent = {0.0f, -0.26f, 0.18f};
    const Vec3 reach = vlerp(reachStraight, reachBent, arms);
    for (int s = 0; s < 2; s++) {
        const float side = s == 0 ? -1.0f : 1.0f;
        p.shoulder[s] = p.shoulderC + Vec3{side * 0.23f, -0.03f, 0.0f};
        // Sculling grips overlap slightly; the left hand leads a touch.
        p.hand[s] = p.shoulderC + reach + Vec3{side * 0.11f, s == 0 ? 0.015f : -0.015f, s == 0 ? 0.03f : 0.0f};
        const Vec3 mid = vlerp(p.shoulder[s], p.hand[s], 0.5f);
        p.elbow[s] = mid + Vec3{side * (0.05f + 0.10f * arms), -0.06f * arms, -0.14f * arms};

        // Oar is straight through the gate: blade direction = handle->lock.
        // The gate height follows the same straight line, so the shaft
        // never shows a kink at the rigger.
        float dx = side * kLockX - p.hand[s].x;
        float dz = kLockZ - p.hand[s].z;
        const float inboard = std::sqrt(dx * dx + dz * dz);
        dx /= inboard;
        dz /= inboard;
        const float bladeY = recovering ? lerp(0.10f, 0.26f, std::sin(drive * kPi)) : -0.02f;
        const float gateY = lerp(p.hand[s].y, bladeY, inboard / (inboard + kOutboard));
        p.lock[s] = {side * kLockX, gateY, kLockZ};
        p.bladeDir[s] = {dx, (bladeY - p.hand[s].y) / (inboard + kOutboard), dz};
        p.blade[s] = {p.lock[s].x + dx * kOutboard, bladeY, p.lock[s].z + dz * kOutboard};
    }
    p.bladeInWater = !recovering;
    return p;
}

void drawOar(const Camera& cam, const Pose& p, int s, float t) {
    const float side = s == 0 ? -1.0f : 1.0f;
    const Rgb shaft = s == 0 ? kShaft : kShaftShade;
    const Vec3 dir = p.bladeDir[s];
    const Vec3 across = {-dir.z, 0.0f, dir.x};  // horizontal, perpendicular to shaft
    const Vec3 flat = {dir.x, 0.0f, dir.z};     // shaft direction, level

    // Rigger: two struts from the gunwale to the gate.
    limb(cam, {side * 0.17f, kDeckY, kLockZ - 0.25f}, p.lock[s], 0.012f, kMetal);
    limb(cam, {side * 0.17f, kDeckY, kLockZ + 0.25f}, p.lock[s], 0.012f, kMetal);

    // Shaft: handle -> gate -> blade neck.
    const Vec3 neck = p.blade[s] - dir * 0.42f;  // still on the shaft line
    limb(cam, p.hand[s], p.lock[s], 0.016f, shaft);
    limb(cam, p.lock[s], neck, 0.016f, shaft);
    limb(cam, p.hand[s], p.hand[s] + (p.lock[s] - p.hand[s]) * 0.18f, 0.022f, kGrip);

    // Blade: squared (vertical) in the water, feathered (flat) above it.
    const Vec3 tip = p.blade[s] + flat * 0.08f;
    if (p.bladeInWater) {
        const Vec3 q[4] = {neck + Vec3{0, 0.05f, 0}, tip + Vec3{0, 0.08f, 0}, tip + Vec3{0, -0.14f, 0}, neck + Vec3{0, -0.08f, 0}};
        convex3(cam, q, 4, mix(kAccent, kWaterDeep, 0.45f));
        const Vec3 above[4] = {neck + Vec3{0, 0.05f, 0}, tip + Vec3{0, 0.08f, 0}, tip + Vec3{0, 0.0f, 0}, neck + Vec3{0, 0.0f, 0}};
        convex3(cam, above, 4, kAccent);
        // Puddle / splash where the blade cuts the surface.
        for (int i = 0; i < 5; i++) {
            const float u = static_cast<float>(i) / 4.0f;
            const Vec3 c = vlerp(neck, tip, u);
            const float wob = 0.5f + 0.5f * std::sin(t * kTau * 3.0f + static_cast<float>(i) * 2.1f);
            ball(cam, {c.x, 0.0f, c.z}, 0.07f + 0.03f * wob, 0.018f, mix(rgb(g_bg->rippleColor), kFoam, 0.4f + 0.4f * wob));
        }
    } else {
        const Vec3 q[4] = {neck + across * 0.06f, tip + across * 0.11f, tip - across * 0.11f, neck - across * 0.06f};
        convex3(cam, q, 4, kAccent);
        // Faint drip rings on the water under a raised blade.
        ball(cam, {p.blade[s].x, 0.0f, p.blade[s].z}, 0.10f, 0.02f, mix(kWaterMid, rgb(g_bg->rippleColor), 0.35f));
    }
}

void drawRower(const Camera& cam, const Pose& p) {
    // Seat.
    const Vec3 seat[4] = {{-0.12f, 0.24f, p.seatZ - 0.10f}, {0.12f, 0.24f, p.seatZ - 0.10f}, {0.12f, 0.24f, p.seatZ + 0.12f},
                          {-0.12f, 0.24f, p.seatZ + 0.12f}};
    convex3(cam, seat, 4, kSeat);

    // Legs (furthest from the camera, mostly hidden behind the back).
    for (int s = 0; s < 2; s++) {
        const Rgb kit = s == 0 ? kKit : kKitShade;
        const Rgb skin = s == 0 ? kSkin : kSkinShade;
        const float side = s == 0 ? -1.0f : 1.0f;
        limb(cam, p.knee[s], p.foot[s], 0.050f, skin);
        limb(cam, {side * 0.11f, p.hip.y, p.hip.z}, p.knee[s], 0.075f, kit);
        ball(cam, p.foot[s] + Vec3{0, -0.02f, 0.04f}, 0.055f, 0.035f, kGrip);
    }

    // Arms (in front of the torso from the rower's point of view, so drawn
    // before it; only the parts that stick out past the back stay visible).
    for (int s = 0; s < 2; s++) {
        const Rgb skin = s == 0 ? kSkin : kSkinShade;
        limb(cam, p.hand[s], p.elbow[s], 0.045f, skin);
        limb(cam, p.elbow[s], p.shoulder[s], 0.055f, skin);
        ball(cam, p.hand[s], 0.045f, 0.040f, skin);
    }

    // Torso: tapered back panel with rounded shoulders and a lit/shaded split.
    const Vec3 waistL = p.hip + Vec3{-0.18f, 0.02f, -0.02f};
    const Vec3 waistR = p.hip + Vec3{0.18f, 0.02f, -0.02f};
    const Vec3 midC = vlerp(p.hip, p.shoulderC, 0.5f);
    const Vec3 back[4] = {waistL, waistR, p.shoulder[1] + Vec3{0.02f, 0, -0.03f}, p.shoulder[0] + Vec3{-0.02f, 0, -0.03f}};
    convex3(cam, back, 4, kKit);
    const Vec3 shadeHalf[4] = {midC + Vec3{0.0f, -0.29f * 0.5f, -0.03f}, waistR, p.shoulder[1] + Vec3{0.02f, 0, -0.03f},
                               p.shoulderC + Vec3{0.0f, 0, -0.03f}};
    convex3(cam, shadeHalf, 4, kKitShade);
    limb(cam, p.shoulder[0], p.shoulder[1], 0.07f, kKit);
    ball(cam, p.shoulder[0] + Vec3{0, 0, -0.03f}, 0.09f, 0.08f, kKit);
    ball(cam, p.shoulder[1] + Vec3{0, 0, -0.03f}, 0.09f, 0.08f, kKit);
    // Accent stripe down the spine.
    limb(cam, p.hip + Vec3{0, 0.06f, -0.06f}, p.shoulderC + Vec3{0, -0.04f, -0.06f}, 0.012f, kAccent);

    // Neck + head (back of the head toward the camera).
    limb(cam, p.shoulderC + Vec3{0, 0, -0.02f}, p.head + Vec3{0, -0.08f, -0.02f}, 0.045f, kSkinShade);
    ball(cam, p.head + Vec3{0, 0, -0.03f}, 0.115f, 0.13f, kSkin);
    ball(cam, p.head + Vec3{0, 0.03f, -0.05f}, 0.118f, 0.11f, kHair);
    // Cap brim edge catching the light.
    ball(cam, p.head + Vec3{-0.03f, 0.07f, -0.06f}, 0.05f, 0.025f, mix(kHair, rgb(g_bg->sunColor), 0.25f));
}

// Pixel-doubles the 400×240 photo into the canvas; downsampleInto() then
// returns it unchanged (2×2 identical samples), so its dithering survives.
void drawBackground() {
    static_assert(scene_background::kWidth * kSS == kCW && scene_background::kHeight * kSS == kCH, "background size");
    uint16_t* dst = pixels();
    for (int y = 0; y < scene_background::kHeight; y++) {
        const uint16_t* src = g_bg->pixels + static_cast<size_t>(y) * scene_background::kWidth;
        uint16_t* row = dst + static_cast<size_t>(y * kSS) * kCW;
        for (int x = 0; x < scene_background::kWidth; x++) {
            row[x * 2] = src[x];
            row[x * 2 + 1] = src[x];
        }
        std::memcpy(row + kCW, row, sizeof(uint16_t) * kCW);
    }
}

void drawScene(float waterTime, float drive, bool recovering) {
    const Camera cam;
    const Pose pose = solvePose(drive, recovering);

    drawBackground();
    drawWaterDetail(cam, waterTime);
    drawHullShadow(cam);

    // Blades far from the camera (finish side) sit behind the hull edge;
    // near ones (catch side) in front. Draw by depth.
    const bool oarsBehind = pose.blade[0].z > 2.2f;
    if (oarsBehind) {
        drawOar(cam, pose, 0, waterTime);
        drawOar(cam, pose, 1, waterTime);
    }
    drawHull(cam, waterTime);
    if (!oarsBehind) {
        drawOar(cam, pose, 0, waterTime);
        drawOar(cam, pose, 1, waterTime);
    }
    drawRower(cam, pose);
}

bool ensureCanvas() {
    if (g_canvas != nullptr) {
        return true;
    }
    g_canvas = new LGFX_Sprite();
    g_canvas->setPsram(true);
    // Match LVGL LV_COLOR_FORMAT_RGB565 (RRRRRGGG GGGBBBBB). LGFX's default
    // rgb565_2Byte stores bytes swapped and produces neon banding when blitted.
    g_canvas->setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
    if (g_canvas->createSprite(kCW, kCH) == nullptr) {
        delete g_canvas;
        g_canvas = nullptr;
        printf("rower_scene: sprite alloc failed\n");
        return false;
    }
    return true;
}

// 2×2 box filter + 4×4 ordered dither down to the kWidth×kHeight frame.
void downsampleInto(lv_draw_buf_t* dst) {
    static const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const uint16_t* src = pixels();
    const int outH = std::min(kHeight, static_cast<int>(dst->header.h));
    for (int y = 0; y < outH; y++) {
        const uint16_t* r0 = src + static_cast<size_t>(y * 2) * kCW;
        const uint16_t* r1 = r0 + kCW;
        auto* out = reinterpret_cast<uint16_t*>(dst->data + static_cast<size_t>(y) * dst->header.stride);
        for (int x = 0; x < kWidth; x++) {
            const uint16_t a = r0[x * 2];
            const uint16_t b = r0[x * 2 + 1];
            const uint16_t c = r1[x * 2];
            const uint16_t d = r1[x * 2 + 1];
            // Channel sums are 4x the average; (sum*4 + m) >> 4 == floor(avg + m/16).
            const uint32_t m = kBayer4[y & 3][x & 3];
            const uint32_t sr = ((a >> 11) & 31) + ((b >> 11) & 31) + ((c >> 11) & 31) + ((d >> 11) & 31);
            const uint32_t sg = ((a >> 5) & 63) + ((b >> 5) & 63) + ((c >> 5) & 63) + ((d >> 5) & 63);
            const uint32_t sb = (a & 31) + (b & 31) + (c & 31) + (d & 31);
            const uint32_t r = std::min<uint32_t>(31, (sr * 4 + m) >> 4);
            const uint32_t g = std::min<uint32_t>(63, (sg * 4 + m) >> 4);
            const uint32_t bl = std::min<uint32_t>(31, (sb * 4 + m) >> 4);
            out[x] = static_cast<uint16_t>((r << 11) | (g << 5) | bl);
        }
    }
}

} // namespace

void setBackground(const scene_background::Background& bg) {
    g_bg = &bg;
}

bool draw(lv_draw_buf_t* dst, float drive, float waterTime, bool recovering) {
    if (dst == nullptr || dst->data == nullptr || !ensureCanvas()) {
        return false;
    }

    drive = clamp01(drive);
    waterTime = std::fmod(waterTime, 1.0f);
    if (waterTime < 0.0f) {
        waterTime += 1.0f;
    }

    drawScene(waterTime, drive, recovering);
    downsampleInto(dst);
    lv_draw_buf_flush_cache(dst, nullptr);
    return true;
}

void releaseCanvas() {
    if (g_canvas == nullptr) {
        return;
    }
    g_canvas->deleteSprite();
    delete g_canvas;
    g_canvas = nullptr;
}

} // namespace rower_scene
