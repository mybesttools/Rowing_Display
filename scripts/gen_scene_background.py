"""Bakes the scene backgrounds: the first into the firmware, all onto the SD card.

Each image in BACKGROUNDS is cropped to the 5:3 panel aspect so its horizon
lands at HORIZON (fraction of output height, shared by all backgrounds so the
rower's camera never moves), scaled to 400x240 and Floyd-Steinberg dithered to
RGB565. rower_scene draws the boat on top and uses each background's sun
column and colours for the glints and ripples it paints on the water.

Images are Higgsfield generations of lakes without a foreground boat. The
first entry is built into the firmware (firmware/src/scene_background.{h,cpp})
as the default; the others are written as sd_card/backgrounds/<slug>.bin.
Copy that backgrounds folder to the root of a FAT32 SD card and pick one under
Settings > Background on the device.

.bin layout (little-endian): 56-byte header, then kWidth*kHeight RGB565 pixels
(LVGL native order, row-major). See sd_backgrounds.h.

    python3 scripts/gen_scene_background.py
"""
import struct
from pathlib import Path

import numpy as np
from PIL import Image

OUT_W, OUT_H = 400, 240
HORIZON = 0.333
ROOT = Path(__file__).resolve().parent.parent

# src_horizon / sun_x / center_x are fractions of the source image.
# glint: strength of the sun path on the water (0 = sun not in the picture).
# sun / ripple: optional 0xRRGGBB overrides for the colours sky_colours() picks.
BACKGROUNDS = [
    dict(slug="dusk_lake", name="Dusk lake", src="assets/scene_background.jpg",
         src_horizon=0.4727, sun_x=0.2936, glint=1.0, sun=0xFFE3A8, ripple=0xF2C9A0),
    dict(slug="pastel_dawn", name="Pastel dawn", src="assets/background-option-2.jpeg",
         src_horizon=0.495, sun_x=0.18, glint=0.6),
    dict(slug="golden_morning", name="Golden morning", src="assets/background-option-3.jpeg",
         src_horizon=0.48, sun_x=0.85, glint=1.0),
    dict(slug="orange_sunset", name="Orange sunset", src="assets/background-option-4.jpeg",
         src_horizon=0.545, sun_x=0.5, glint=0.8),
    dict(slug="cloud_mirror", name="Cloud mirror", src="assets/background-option-5.jpeg",
         src_horizon=0.555, sun_x=0.2, glint=0.6),
    dict(slug="summer_day", name="Summer day", src="assets/background-option-6.jpeg",
         src_horizon=0.445, sun_x=0.5, glint=0.0, center_x=0.4),
    dict(slug="alpine_sunrise", name="Alpine sunrise", src="assets/background-option-7.jpeg",
         src_horizon=0.375, sun_x=0.55, glint=1.0),
    dict(slug="blue_hour", name="Blue hour", src="assets/background-option-8.jpeg",
         src_horizon=0.495, sun_x=0.3, glint=0.3),
]


def crop(bg):
    """Returns the 400x240 RGB image and the sun column in output pixels."""
    im = Image.open(ROOT / bg["src"]).convert("RGB")
    sw, sh = im.size
    hy = bg["src_horizon"] * sh
    # Largest 5:3 crop that puts the horizon at HORIZON.
    crop_h = min(hy / HORIZON, (sh - hy) / (1.0 - HORIZON), sw * OUT_H / OUT_W)
    crop_w = crop_h * OUT_W / OUT_H
    top = hy - HORIZON * crop_h
    left = min(max(bg.get("center_x", 0.5) * sw - crop_w / 2, 0), sw - crop_w)
    im = im.resize((OUT_W, OUT_H), Image.LANCZOS, box=(left, top, left + crop_w, top + crop_h))
    return im, (bg["sun_x"] * sw - left) / crop_w * OUT_W


def sky_colours(im):
    """Sun glint and ripple colours from the brightest part of the sky."""
    sky = np.asarray(im, dtype=np.float32)[:int(HORIZON * OUT_H) - 4].reshape(-1, 3)
    # Brightest tenth of the sky: the glow the water would reflect (hills
    # and trees on the skyline are darker and drop out).
    lum = sky @ np.array([0.299, 0.587, 0.114])
    glow = sky[lum >= np.percentile(lum, 90)].mean(axis=0)
    ripple = glow * (242.0 / glow.max())
    sun = 0.6 * ripple + 0.4 * 255.0
    sun = sun * (255.0 / sun.max())
    return sun, ripple


def rgb(hex_colour):
    return np.array([(hex_colour >> 16) & 0xFF, (hex_colour >> 8) & 0xFF, hex_colour & 0xFF], dtype=np.float32)


def dither565(im):
    """Floyd-Steinberg to RGB565: hides banding in the sky gradient."""
    px = np.asarray(im, dtype=np.float32).copy()
    levels = np.array([31.0, 63.0, 31.0])
    out = np.zeros((OUT_H, OUT_W), dtype=np.uint16)
    for y in range(OUT_H):
        for x in range(OUT_W):
            old = np.clip(px[y, x], 0, 255)
            q = np.round(old / 255.0 * levels)
            err = old - q / levels * 255.0
            out[y, x] = (int(q[0]) << 11) | (int(q[1]) << 5) | int(q[2])
            if x + 1 < OUT_W:
                px[y, x + 1] += err * 7 / 16
            if y + 1 < OUT_H:
                if x > 0:
                    px[y + 1, x - 1] += err * 3 / 16
                px[y + 1, x] += err * 5 / 16
                if x + 1 < OUT_W:
                    px[y + 1, x + 1] += err * 1 / 16
    return out


def hex_rgb(c):
    return "0x" + "".join(f"{int(round(v)):02X}" for v in c)


src_dir = ROOT / "firmware" / "src"
sd_dir = ROOT / "sd_card" / "backgrounds"
sd_dir.mkdir(parents=True, exist_ok=True)

for i, bg in enumerate(BACKGROUNDS):
    im, sun_x = crop(bg)
    sun, ripple = sky_colours(im)
    sun = rgb(bg["sun"]) if "sun" in bg else sun
    ripple = rgb(bg["ripple"]) if "ripple" in bg else ripple
    pixels = dither565(im).flatten()
    sun_hex, ripple_hex = hex_rgb(sun), hex_rgb(ripple)
    print(f"{bg['slug']}: sun x {sun_x:.1f}px, sun {sun_hex}, ripple {ripple_hex}")

    if i != 0:
        header = struct.pack("<4sHHffII32s", b"RBG1", OUT_W, OUT_H, sun_x, bg["glint"],
                             int(sun_hex, 16), int(ripple_hex, 16), bg["name"].encode()[:31])
        (sd_dir / f"{bg['slug']}.bin").write_bytes(header + pixels.astype("<u2").tobytes())
        continue
    (src_dir / "scene_background.h").write_text(f"""#pragma once

#include <cstdint>

// Generated by scripts/gen_scene_background.py - do not edit.
// Static sky + water photo under the procedural rower (see rower_scene.cpp).
// More backgrounds can be loaded from the SD card (sd_backgrounds.h).
namespace scene_background {{

constexpr int kWidth = {OUT_W};
constexpr int kHeight = {OUT_H};
// Waterline row, in kWidth x kHeight pixels. Shared by every background.
constexpr float kHorizonY = {HORIZON * OUT_H:.2f}f;

struct Background {{
    const char* name;
    const uint16_t* pixels; // RGB565 (LVGL native byte order), row-major
    float sunX;             // sun column, in kWidth pixels
    float glint;            // sun path strength on the water, 0..1
    uint32_t sunColor;      // 0xRRGGBB
    uint32_t rippleColor;   // sky glow reflected on the ripple crests
}};

// Built-in background ({bg["name"]}).
extern const Background kDefault;

}} // namespace scene_background
""")
    lines = [
        "// Generated by scripts/gen_scene_background.py - do not edit.",
        '#include "scene_background.h"',
        "",
        "namespace scene_background {",
        "",
        "namespace {",
        "",
        "const uint16_t kPixels[kWidth * kHeight] = {",
    ]
    for j in range(0, len(pixels), 16):
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in pixels[j:j + 16]) + ",")
    lines += [
        "};",
        "",
        "} // namespace",
        "",
        f'const Background kDefault = {{"{bg["name"]}", kPixels, {sun_x:.2f}f, {bg["glint"]:.2f}f, '
        f"{sun_hex}, {ripple_hex}}};",
        "",
        "} // namespace scene_background",
        "",
    ]
    (src_dir / "scene_background.cpp").write_text("\n".join(lines))
