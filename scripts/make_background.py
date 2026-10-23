"""Turns your own photos into scene backgrounds for the display's SD card.

Works best with a landscape photo of calm water and no boat in the
foreground: the display draws its own rower in the lower middle.

    python3 scripts/make_background.py my_lake.jpg
    python3 scripts/make_background.py my_lake.jpg --name "Home lake" --horizon 0.52
    python3 scripts/make_background.py *.jpg --out /Volumes/ROWER/backgrounds

For each photo this writes <name>.bin to --out (default sd_card/backgrounds)
and a preview PNG to background_previews/. Check the preview: the red dashed
line must sit on the waterline (where the far shore meets the water). If it
doesn't, rerun with --horizon. Then copy the .bin files into a "backgrounds"
folder at the root of a FAT32 SD card, put the card in the display, and pick
the background under Settings > DISPLAY > Background (the display restarts
to load it). The display lists up to 16 backgrounds.

Needs Python 3 with numpy and Pillow (pip3 install numpy pillow).
"""
import argparse
import re
import sys
from pathlib import Path

from PIL import Image, UnidentifiedImageError

sys.path.insert(0, str(Path(__file__).resolve().parent))
from background_tools import (MAX_FILE_NAME, MAX_NAME, MAX_SD_FILES, crop, dither565,  # noqa: E402
                              estimate_horizon, estimate_sun_x, preview, sd_file_bytes, sky_colours)

ROOT = Path(__file__).resolve().parent.parent


def fraction(text):
    v = float(text)
    if not 0.0 <= v <= 1.0:
        raise argparse.ArgumentTypeError("must be between 0 and 1")
    return v


def slug_for(name):
    slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") or "background"
    return slug[:MAX_FILE_NAME - len(".bin")]


ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("photos", nargs="+", type=Path, help="JPEG/PNG/... photos")
ap.add_argument("--name", help="name shown on the display (one photo only; default: the file name)")
ap.add_argument("--horizon", type=fraction,
                help="waterline height in the photo, 0 = top, 1 = bottom (default: detected)")
ap.add_argument("--sun-x", type=fraction,
                help="sun position across the photo, 0 = left, 1 = right (default: brightest sky)")
ap.add_argument("--glint", type=fraction, default=0.6,
                help="strength of the sun's reflection on the water, 0 = none (default 0.6)")
ap.add_argument("--center-x", type=fraction, default=0.5,
                help="which part of a wide photo to keep, 0 = left edge, 1 = right edge (default 0.5)")
ap.add_argument("--out", type=Path, default=ROOT / "sd_card" / "backgrounds",
                help="folder for the .bin files (default: sd_card/backgrounds)")
args = ap.parse_args()

if args.name and len(args.photos) > 1:
    ap.error("--name only works with a single photo")

args.out.mkdir(parents=True, exist_ok=True)
preview_dir = ROOT / "background_previews"
preview_dir.mkdir(exist_ok=True)

for path in args.photos:
    try:
        src = Image.open(path)
        src.load()
    except (OSError, UnidentifiedImageError) as e:
        print(f"{path}: can't read ({e}), skipped")
        continue
    name = (args.name or path.stem.replace("_", " ").replace("-", " ").strip())[:MAX_NAME]
    horizon = args.horizon if args.horizon is not None else estimate_horizon(src)
    sun_x = args.sun_x if args.sun_x is not None else estimate_sun_x(src, horizon)

    im, sun_px = crop(src, horizon, sun_x, args.center_x)
    sun, ripple = sky_colours(im)
    pixels = dither565(im)

    slug = slug_for(name)
    out = args.out / f"{slug}.bin"
    out.write_bytes(sd_file_bytes(name, sun_px, args.glint, sun, ripple, pixels))
    pv = preview_dir / f"{slug}.png"
    preview(pixels, sun_px, args.glint, name).save(pv)

    how = "given" if args.horizon is not None else "detected"
    print(f"{path.name} -> {out}")
    print(f"    name \"{name}\", waterline {horizon:.3f} ({how}), sun {sun_x:.2f}, glint {args.glint:.1f}")
    print(f"    preview: {pv}")

count = len(list(args.out.glob("*.bin")))
if count > MAX_SD_FILES:
    print(f"note: {args.out} has {count} backgrounds; the display only lists the first {MAX_SD_FILES} (by file name)")
