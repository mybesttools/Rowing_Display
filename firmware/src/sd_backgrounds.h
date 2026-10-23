#pragma once

#include <cstdint>

#include "scene_background.h"

// Extra scene backgrounds from the SD card: /backgrounds/*.bin, written by
// scripts/gen_scene_background.py. Both Waveshare boards wire the card to SPI
// GPIO11/12/13 with chip select on CH422G EXIO4 (vendor 03_SD_Test).
namespace sd_backgrounds {

// File layout: this header, then kWidth*kHeight RGB565 pixels.
struct FileHeader {
    char magic[4]; // "RBG1"
    uint16_t width;
    uint16_t height;
    float sunX;
    float glint;
    uint32_t sunColor;
    uint32_t rippleColor;
    char name[32];
};
static_assert(sizeof(FileHeader) == 56, "matches gen_scene_background.py");

constexpr int kMaxFiles = 16;
constexpr int kFileNameLen = 32;

// Mounts the card, lists the backgrounds and, if `wanted` (a file name such
// as "golden_morning.bin") is among them, reads its pixels into `pixels`
// (kWidth*kHeight) and fills `out`. Unmounts before returning. Call once at
// boot while the CH422G is still in output mode: the 5" board's DI polling
// later switches it to input, which lets the card's chip select float high.
// `expander` is panel_runtime::ioExpanderBase().
bool begin(void* expander, const char* wanted, uint16_t* pixels, scene_background::Background& out);

// Backgrounds found by begin(), sorted by file name.
int count();
const char* fileName(int i);
const char* displayName(int i);

} // namespace sd_backgrounds
