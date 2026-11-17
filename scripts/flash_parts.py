"""After a build, list the images `pio run -t upload` would flash, and where.

PlatformIO post-script. Writes <build dir>/flash_parts.json, which
scripts/build_site.py turns into the web installer's manifests, so the
installer writes exactly what a USB upload from PlatformIO writes.
"""

from __future__ import annotations

import json
from pathlib import Path

Import("env")  # noqa: F821 (provided by PlatformIO)


def write_parts(target, source, env) -> None:  # noqa: ARG001
    parts = [
        {"offset": int(env.subst(offset), 16), "path": env.subst(path)}
        for offset, path in env.get("FLASH_EXTRA_IMAGES", [])
    ]
    parts.append(
        {
            "offset": int(env.subst("$ESP32_APP_OFFSET"), 16),
            "path": str(Path(env.subst("$BUILD_DIR")) / env.subst("${PROGNAME}.bin")),
        }
    )
    out = Path(env.subst("$BUILD_DIR")) / "flash_parts.json"
    out.write_text(json.dumps(parts, indent=2) + "\n")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_parts)  # noqa: F821
