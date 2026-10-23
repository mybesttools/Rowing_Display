"""Shared image code for scene backgrounds (gen_scene_background.py, make_background.py).

A background is a 400x240 RGB565 picture whose waterline sits at HORIZON of
its height: rower_scene's camera assumes that, so every background keeps the
rower in the same place. The SD card file is a 56-byte header (see
firmware/src/sd_backgrounds.h) followed by the pixels.
"""
import struct

import numpy as np
from PIL import Image, ImageDraw

OUT_W, OUT_H = 400, 240
HORIZON = 0.333
MAX_NAME = 31          # header name field, NUL-terminated
MAX_FILE_NAME = 31     # firmware's kFileNameLen, including ".bin"
MAX_SD_FILES = 16      # firmware's kMaxFiles


def crop(im, src_horizon, sun_x, center_x=0.5):
    """Largest 5:3 crop of `im` that puts src_horizon (fraction of its height)
    at HORIZON, scaled to OUT_W x OUT_H. Returns the image and the sun column
    (sun_x, a fraction of the source width) in output pixels."""
    im = im.convert("RGB")
    sw, sh = im.size
    hy = src_horizon * sh
    crop_h = min(hy / HORIZON, (sh - hy) / (1.0 - HORIZON), sw * OUT_H / OUT_W)
    crop_w = crop_h * OUT_W / OUT_H
    top = hy - HORIZON * crop_h
    left = min(max(center_x * sw - crop_w / 2, 0), sw - crop_w)
    out = im.resize((OUT_W, OUT_H), Image.LANCZOS, box=(left, top, left + crop_w, top + crop_h))
    return out, (sun_x * sw - left) / crop_w * OUT_W


def estimate_horizon(im):
    """Guesses the waterline (fraction of height) of a calm-water photo: the
    row the picture is most mirror-symmetric about, relative to how much
    detail there is (so flat sky doesn't win). Check it in the preview."""
    a = np.asarray(im.convert("L").resize((200, max(1, round(200 * im.height / im.width)))), dtype=np.float32)
    h = a.shape[0]
    k = max(2, int(h * 0.12))
    best = (float("inf"), h // 2)
    for y in range(max(k, int(h * 0.25)), min(h - k - 1, int(h * 0.75))):
        up = a[y - k:y][::-1]
        down = a[y + 1:y + 1 + k]
        mirror = np.mean(np.abs(up - down))
        detail = np.mean(np.abs(up - up.mean(axis=0))) + np.mean(np.abs(down - down.mean(axis=0)))
        score = mirror / (detail + 10.0)
        if score < best[0]:
            best = (score, y)
    return best[1] / h


def estimate_sun_x(im, src_horizon):
    """Brightest part of the sky, as a fraction of the width."""
    a = np.asarray(im.convert("L").resize((200, max(1, round(200 * im.height / im.width)))), dtype=np.float32)
    sky = a[:max(1, int(src_horizon * a.shape[0]) - 2)]
    columns = np.convolve(np.percentile(sky, 90, axis=0), np.ones(15) / 15, mode="same")
    return float(np.argmax(columns)) / a.shape[1]


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


def hex_rgb(c):
    return "0x" + "".join(f"{int(round(v)):02X}" for v in c)


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


def sd_file_bytes(name, sun_x, glint, sun, ripple, pixels):
    """The .bin the firmware reads from the SD card's /backgrounds folder."""
    header = struct.pack("<4sHHffII32s", b"RBG1", OUT_W, OUT_H, sun_x, glint,
                         int(hex_rgb(sun), 16), int(hex_rgb(ripple), 16), name.encode()[:MAX_NAME])
    return header + pixels.flatten().astype("<u2").tobytes()


def preview(pixels, sun_x, glint, name):
    """800x480 picture of the panel: the dithered background, the waterline
    the rower sits on, the sun column and the bands the metric tiles cover."""
    a = pixels.astype(np.uint32)
    im = Image.fromarray(np.stack([((a >> 11) & 31) * 255 // 31, ((a >> 5) & 63) * 255 // 63,
                                   (a & 31) * 255 // 31], -1).astype(np.uint8))
    im = im.resize((OUT_W * 2, OUT_H * 2), Image.NEAREST).convert("RGBA")
    overlay = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    # Session screen: 16px padding, 96px top bar, 88px bottom bar.
    d.rectangle([16, 16, 783, 111], fill=(10, 20, 40, 110))
    d.rectangle([16, 376, 783, 463], fill=(10, 20, 40, 110))
    hy = HORIZON * OUT_H * 2
    for x in range(0, OUT_W * 2, 16):
        d.line([(x, hy), (x + 8, hy)], fill=(255, 60, 60, 255), width=2)
    if glint > 0:
        sx = sun_x * 2
        d.line([(sx, hy + 6), (sx, OUT_H * 2 - 100)], fill=(255, 220, 80, 200), width=2)
    # Where the boat and rower sit.
    d.polygon([(400, hy + 30), (430, 470), (370, 470)], outline=(255, 255, 255, 220), width=2)
    d.text((24, 120), f"{name}  (red: waterline, yellow: sun glints, white: boat, dark: metric tiles)",
           fill=(255, 255, 255, 255))
    return Image.alpha_composite(im, overlay).convert("RGB")
