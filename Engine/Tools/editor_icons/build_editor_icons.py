#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Regenerates the editor's built-in icons from icons.py.

For every registered icon this writes <group>/<id>.svg (the artwork source) and <group>/<id>.png
(a 64 px raster of it) under Engine/Editor/EditorCore/assets/icons/. Both are committed: the build
embeds the PNGs (embed_editor_icons.py), and has no SVG renderer of its own.

The rasterizer is headless Chromium, so the PNG is exactly what the SVG looks like in a browser,
clip paths and gradients included. Files in the three group directories that no icon owns any
more are deleted, so a renamed icon does not leave its old file behind.

Usage:
    build_editor_icons.py [--chromium PATH] [--gallery OUT.html]
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import icons  # noqa: E402  (after the path tweak above)
from gallery import write_gallery  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]
ICON_ROOT = REPO_ROOT / "Engine" / "Editor" / "EditorCore" / "assets" / "icons"
GROUPS = (icons.NODES, icons.NODE_CATEGORIES, icons.CONTENT_TYPES)
RASTER_SIZE = 64
CHROMIUM_CANDIDATES = ("headless_shell", "chromium", "chromium-browser", "google-chrome")


def find_chromium(explicit):
    if explicit:
        return explicit
    if os.environ.get("UVE_CHROMIUM"):
        return os.environ["UVE_CHROMIUM"]
    for name in CHROMIUM_CANDIDATES:
        found = shutil.which(name)
        if found:
            return found
    return None


def rasterize(chromium, svg_path, png_path, work_dir):
    page = work_dir / "page.html"
    page.write_text(
        '<!doctype html><html><head><style>html,body{margin:0;background:transparent}img{display:block}'
        f'</style></head><body><img src="{svg_path.as_uri()}" width="{RASTER_SIZE}" height="{RASTER_SIZE}">'
        "</body></html>", encoding="utf-8")
    png_path.unlink(missing_ok=True)
    subprocess.run([chromium, "--headless", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                    "--force-device-scale-factor=1", "--force-color-profile=srgb",
                    "--default-background-color=00000000", f"--window-size={RASTER_SIZE},{RASTER_SIZE}",
                    f"--screenshot={png_path}", page.as_uri()],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not png_path.is_file():
        raise RuntimeError(f"{chromium} did not write {png_path}")


def main():
    parser = argparse.ArgumentParser(description="Regenerate the editor icon SVGs and PNGs")
    parser.add_argument("--chromium", help="Chromium or headless_shell binary (default: $UVE_CHROMIUM, then PATH)")
    parser.add_argument("--gallery", type=Path, help="also write a preview page of every icon here")
    args = parser.parse_args()

    chromium = find_chromium(args.chromium)
    if chromium is None:
        print("build_editor_icons.py: no Chromium found; pass --chromium or set UVE_CHROMIUM", file=sys.stderr)
        return 1

    owned = set()
    with tempfile.TemporaryDirectory() as work:
        for (group, icon_id) in icons.ICONS:
            directory = ICON_ROOT / group
            directory.mkdir(parents=True, exist_ok=True)
            svg_path = directory / f"{icon_id}.svg"
            png_path = directory / f"{icon_id}.png"
            svg_path.write_text(icons.build((group, icon_id)) + "\n", encoding="utf-8")
            rasterize(chromium, svg_path, png_path, Path(work))
            owned.update((svg_path, png_path))

    for group in GROUPS:
        for stale in (ICON_ROOT / group).glob("*"):
            if stale.suffix in (".svg", ".png") and stale not in owned:
                stale.unlink()
                print(f"removed {stale.relative_to(REPO_ROOT)}")

    if args.gallery:
        sections = {}
        for key, (title, section, _) in icons.ICONS.items():
            sections.setdefault(section, []).append((title, icons.build(key)))
        write_gallery(args.gallery, list(sections.items()))

    print(f"wrote {len(icons.ICONS)} icons under {ICON_ROOT.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
