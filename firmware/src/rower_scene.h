#pragma once

#include "lvgl.h"
#include "scene_background.h"

// Procedural stern-view rowing scene (see scratchpad/watergen/gen_stroke_phases.py
// for the Python reference). Drawn with LovyanGFX into an off-screen canvas,
// precomputed at boot, then blitted through panel_runtime's 2× present path.
namespace rower_scene {

constexpr int kWidth = 400;
constexpr int kHeight = 240;
constexpr int kIdleWaterFrameCount = 10;
constexpr int kStrokeFrameCount = 16;

// Draw one frame into `dst` (400×240 RGB565). Rendered at 2× and
// downsampled, so edges and gradients come out anti-aliased.
//   drive: 0 = catch pose, 1 = finish pose (matches gen_stroke_phases.py).
//   waterTime: 0..1 loops the water ripples / glints.
//   recovering: feather oars and soften wake during recovery.
// False if the scratch canvas couldn't be allocated (dst untouched).
bool draw(lv_draw_buf_t* dst, float drive, float waterTime, bool recovering);

// Background later draw()s use (scene_background::kDefault until set). Must
// stay valid, pixels included, until the last draw().
void setBackground(const scene_background::Background& bg);

// Frees the scratch canvas draw() renders into. Call once all frames are
// precomputed; the next draw() re-creates it.
void releaseCanvas();

} // namespace rower_scene
