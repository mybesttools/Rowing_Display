#pragma once

#include <cstddef>
#include <cstdint>

// Turns a photo into a scene background: the on-device version of
// scripts/background_tools.py (same waterline / sun guesses, crop, sky
// colours and Floyd-Steinberg dithering; resampling is a box filter instead
// of Lanczos). Pure maths on RGB888 buffers, no ESP-IDF, so it's covered by
// the native tests.
namespace background_convert {

constexpr int kOutW = 400;
constexpr int kOutH = 240;
constexpr float kHorizon = 0.333f; // waterline in the output, fraction of height

struct Image {
    const uint8_t* rgb; // RGB888, row-major, no padding
    int w;
    int h;
};

// Waterline of a calm-water photo, fraction of its height: the row the
// picture is most mirror-symmetric about, relative to how much detail there
// is (so flat sky doesn't win). Same as background_tools.estimate_horizon().
float estimateHorizon(const Image& img);

// Brightest part of the sky, fraction of the width.
float estimateSunX(const Image& img, float horizon);

struct Result {
    float sunX;           // output pixels
    uint32_t sunColor;    // 0xRRGGBB
    uint32_t rippleColor; // 0xRRGGBB
};

// Crops `img` so `horizon` lands at kHorizon, scales it to kOutW x kOutH,
// picks the glint/ripple colours from the sky and dithers to RGB565 (LVGL
// byte order) into `out` (kOutW * kOutH). `scratch` must hold
// kOutW * kOutH * 3 bytes.
Result convert(const Image& img, float horizon, float sunX, uint8_t* scratch, uint16_t* out);

} // namespace background_convert
