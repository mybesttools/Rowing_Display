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

// Projects a point in the scene (metres: x right, y up from the water, z
// ahead of our boat's stern) to kWidth x kHeight frame pixels, with the
// scale there in pixels per metre. False if it's too close to or behind the
// camera to draw.
bool project(float x, float y, float z, float& sx, float& sy, float& pixelsPerMetre);

// Seat position (z, metres) of our rower mid-stroke; a boat level with ours
// has its rower at the same z.
constexpr float kRowerZ = 1.6f;

// Frees the scratch canvas draw() renders into. Call once all frames are
// precomputed; the next draw() re-creates it.
void releaseCanvas();

} // namespace rower_scene
