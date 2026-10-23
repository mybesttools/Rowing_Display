#pragma once

#include "lvgl.h"

// Ghost boat for the pace-boat workout: a translucent single sculler in the
// lane to our right, rowing the target split. Drawn on top of the
// precomputed scene each animation tick, into the 800x480 present buffer.
namespace pace_boat {

// gapMeters: how far the pace boat is ahead of us (negative: behind; it
// leaves the picture a couple of metres behind). strokePhase 0..1 moves
// its oars.
void draw(lv_draw_buf_t* frame, float gapMeters, float strokePhase);

} // namespace pace_boat
