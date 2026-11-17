"""Assemble the GitHub Pages site: web installer + OTA update files.

Run after `pio run -e <env>` for each environment (the release workflow does
this). Layout of the output directory:

    index.html                      the web installer (installer/index.html)
    firmware/latest.json            {"version": ..., "<env>": "<env>/firmware.bin"}
                                    read by the displays' update check
    firmware/<env>/manifest.json    ESP Web Tools manifest for one build
    firmware/<env>/*.bin            bootloader, partitions, boot_app0, firmware

Usage:
    python3 scripts/build_site.py --version 1.2.0 --out site esp32-s3-devkitc-1 esp32-s3-lcd5 ...
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Shown in the installer while flashing; the page has its own labels.
NAMES = {
    "esp32-s3-devkitc-1": 'Rowing Display, 7" board, Dunlop preset',
    "esp32-s3-devkitc-1-kuipers": 'Rowing Display, 7" board, Kuipers preset',
    "esp32-s3-lcd5": 'Rowing Display, 5" board, Dunlop preset',
    "esp32-s3-lcd5-kuipers": 'Rowing Display, 5" board, Kuipers preset',
}


def add_env(env_name: str, version: str, firmware_dir: Path) -> str:
    build_dir = Path(os.environ.get("PLATFORMIO_BUILD_DIR", ROOT / ".pio" / "build")) / env_name
    parts_file = build_dir / "flash_parts.json"
    if not parts_file.exists():
        raise SystemExit(f"{parts_file} missing: run `pio run -e {env_name}` first")
    out_dir = firmware_dir / env_name
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_parts = []
    for part in json.loads(parts_file.read_text()):
        src = Path(part["path"])
        if not src.exists():
            raise SystemExit(f"{src} missing (listed in {parts_file})")
        shutil.copyfile(src, out_dir / src.name)
        manifest_parts.append({"path": src.name, "offset": part["offset"]})
    app = manifest_parts[-1]["path"]  # flash_parts.py lists the app last
    manifest = {
        "name": NAMES.get(env_name, f"Rowing Display ({env_name})"),
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3", "parts": manifest_parts}],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return f"{env_name}/{app}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help='release version, e.g. "1.2.0" (no leading v)')
    parser.add_argument("--out", default="site", help="output directory (replaced)")
    parser.add_argument("envs", nargs="+", help="PlatformIO environments to publish")
    args = parser.parse_args()

    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    firmware_dir = out / "firmware"
    firmware_dir.mkdir(parents=True)
    shutil.copyfile(ROOT / "installer" / "index.html", out / "index.html")

    latest = {"version": args.version.lstrip("vV")}
    for env_name in args.envs:
        latest[env_name] = add_env(env_name, latest["version"], firmware_dir)
    (firmware_dir / "latest.json").write_text(json.dumps(latest, indent=2) + "\n")
    print(json.dumps(latest, indent=2))


if __name__ == "__main__":
    main()
