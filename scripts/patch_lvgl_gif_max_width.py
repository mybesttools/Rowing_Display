"""Make LVGL 9.5's AnimatedGIF decoder honor LV_GIF_MAX_WIDTH.

Mirrors lvgl/lvgl#9956 (merged to master June 2026, not in v9.5.0).
Idempotent: safe to run on an already-patched tree.
"""

from __future__ import annotations

from pathlib import Path

OLD = "#define MAX_WIDTH 480\n"
NEW = (
    "#ifndef LV_GIF_MAX_WIDTH\n"
    "#define LV_GIF_MAX_WIDTH 480\n"
    "#endif\n"
    "#define MAX_WIDTH LV_GIF_MAX_WIDTH\n"
)


def patch_file(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    if "MAX_WIDTH LV_GIF_MAX_WIDTH" in text:
        return "already"
    if OLD not in text:
        return "skip"
    path.write_text(text.replace(OLD, NEW, 1), encoding="utf-8")
    return "patched"


def collect_roots() -> list[Path]:
    roots: list[Path] = []
    pio_env = globals().get("env")
    if pio_env is not None:
        roots.append(Path(str(pio_env["PROJECT_LIBDEPS_DIR"])))
        project = Path(str(pio_env["PROJECT_DIR"]))
        roots.append(project / ".pio" / "libdeps")
        roots.append(project / "firmware" / ".pio" / "libdeps")
    else:
        here = Path(__file__).resolve().parents[1]
        roots.append(here / ".pio" / "libdeps")
        roots.append(here / "firmware" / ".pio" / "libdeps")

    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        key = str(root)
        if key in seen:
            continue
        seen.add(key)
        unique.append(root)
    return unique


def patch_all() -> None:
    found = False
    for root in collect_roots():
        if not root.exists():
            continue
        for path in root.rglob("lvgl/src/libs/gif/AnimatedGIF.h"):
            found = True
            result = patch_file(path)
            print(f"patch_lvgl_gif_max_width: {result} {path}")
    if not found:
        print("patch_lvgl_gif_max_width: no AnimatedGIF.h yet (libs not installed)")


try:
    Import("env")
except NameError:
    pass

patch_all()
