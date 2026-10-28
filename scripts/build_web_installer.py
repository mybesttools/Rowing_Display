"""Build the browser installer site and the release firmware images.

For each board/machine build below this runs `pio run`, merges bootloader,
partition table, boot_app0 and app into one image that flashes at 0x0, and
writes an ESP Web Tools manifest for it. Result, under --out:

    index.html, in-action.jpg     the installer page (web/index.html)
    manifest-<env>.json           one per build
    firmware/<env>.bin            merged images the manifests point at
    release/rowing-display-<version>-<env>.bin   the same, named for a release
    release-notes.md              this version's CHANGELOG section + how to flash

The GitHub workflow (.github/workflows/release.yml) runs this on a v* tag and
publishes the site to GitHub Pages and release/ to the GitHub release.
Locally:

    ~/.platformio/penv/bin/python scripts/build_web_installer.py
    python3 -m http.server -d .pio/web-installer 8080   # then open in Chrome

Needs esptool importable by the running Python (pip install esptool).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from firmware_version import firmware_version  # noqa: E402

# The page comes from this checkout; the firmware from --project, so the
# workflow can rebuild a tag made before this script existed.
HERE = Path(__file__).resolve().parent.parent
ROOT = HERE
APP_OFFSET = 0x10000  # app0 in the Arduino default partition tables
INSTALLER_URL = "https://mybesttools.github.io/Rowing_Display/"

BUILDS = [
    dict(env="esp32-s3-lcd5-kuipers", board='5" (ESP32-S3-Touch-LCD-5)', machine="Kuipers AquaRower"),
    dict(env="esp32-s3-lcd5", board='5" (ESP32-S3-Touch-LCD-5)', machine="Dunlop Air Rower 750"),
    dict(env="esp32-s3-devkitc-1-kuipers", board='7" (ESP32-S3-Touch-LCD-7)', machine="Kuipers AquaRower"),
    dict(env="esp32-s3-devkitc-1", board='7" (ESP32-S3-Touch-LCD-7)', machine="Dunlop Air Rower 750"),
]


def pio_cmd() -> list[str]:
    if os.environ.get("PIO"):
        return [os.environ["PIO"]]
    penv = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    return [str(penv)] if penv.exists() else ["pio"]


def flash_images(env: str) -> list[tuple[int, Path]]:
    out = subprocess.run([*pio_cmd(), "project", "metadata", "-e", env, "--json-output"],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout
    meta = json.loads(out[out.index("{"):])[env]
    images = [(int(i["offset"], 16), Path(i["path"])) for i in meta["extra"]["flash_images"]]
    images.append((APP_OFFSET, ROOT / ".pio" / "build" / env / "firmware.bin"))
    return sorted(images)


def merge(images: list[tuple[int, Path]], dest: Path) -> None:
    args = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin", "-o", str(dest),
            "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for offset, path in images:
        args += [hex(offset), str(path)]
    subprocess.run(args, cwd=ROOT, check=True)


def manifest(build: dict, version: str) -> dict:
    return {
        "name": f"Rowing Display, {build['board']}, {build['machine']}",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3",
                    "parts": [{"path": f"firmware/{build['env']}.bin", "offset": 0}]}],
    }


def release_notes() -> str:
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    m = re.search(rf"^## {re.escape(version)}\b.*?\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    changes = m.group(1).strip() + "\n\n" if m else ""
    return (changes + f"Flash it from the browser: {INSTALLER_URL}\n\n"
            "Or write the image for your board and machine at offset 0: "
            "`esptool.py --chip esp32s3 write_flash 0x0 rowing-display-<version>-<env>.bin`\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=HERE / ".pio" / "web-installer")
    ap.add_argument("--skip-build", action="store_true", help="reuse existing .pio/build output")
    ap.add_argument("--project", type=Path, default=HERE, help="repo checkout to build (default: this one)")
    args = ap.parse_args()
    global ROOT
    ROOT = args.project.resolve()

    version = firmware_version(ROOT)
    out: Path = args.out
    shutil.rmtree(out, ignore_errors=True)
    (out / "firmware").mkdir(parents=True)
    (out / "release").mkdir()

    for build in BUILDS:
        env = build["env"]
        # Ask for the layout before building: on a fresh checkout the build
        # adds firmware_version.cpp, which changes PlatformIO's project
        # checksum, and a metadata call after that wipes .pio/build.
        images = flash_images(env)
        if not args.skip_build:
            subprocess.run([*pio_cmd(), "run", "-e", env], cwd=ROOT, check=True)
        image = out / "firmware" / f"{env}.bin"
        merge(images, image)
        shutil.copy(image, out / "release" / f"rowing-display-{version}-{env}.bin")
        (out / f"manifest-{env}.json").write_text(json.dumps(manifest(build, version), indent=2) + "\n")

    page = (HERE / "web" / "index.html").read_text(encoding="utf-8")
    (out / "index.html").write_text(page.replace("{{VERSION}}", version), encoding="utf-8")
    shutil.copy(HERE / "hardware" / "in-action.jpg", out / "in-action.jpg")
    (out / "release-notes.md").write_text(release_notes(), encoding="utf-8")
    print(f"web installer {version} -> {out}")


if __name__ == "__main__":
    main()
