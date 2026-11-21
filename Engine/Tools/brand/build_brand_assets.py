#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Regenerates the raster brand assets from Engine/Distribution/brand/univex-logo.svg.

The logo uses SVG filters (lighting, grain) that only a browser engine renders faithfully, so the
PNGs are rendered with Chromium (render_svg.cjs, Playwright) and committed; builds never render.
Outputs:

    Engine/Distribution/brand/png/univex-logo-<size>.png   application and file icons
    Engine/Distribution/brand/univex-logo.ico              Windows executable and file icon
    Engine/Runtime/Prism/assets/images/univex_logo.png     the mark PRISM draws in title bars

Needs Node with Playwright and Python with Pillow.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[3]
BRAND_DIR = REPO_ROOT / "Engine" / "Distribution" / "brand"
SOURCE_SVG = BRAND_DIR / "univex-logo.svg"
PNG_DIR = BRAND_DIR / "png"
ICO_FILE = BRAND_DIR / "univex-logo.ico"
PRISM_MARK = REPO_ROOT / "Engine" / "Runtime" / "Prism" / "assets" / "images" / "univex_logo.png"
RENDERER = Path(__file__).with_name("render_svg.cjs")
PNG_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
PRISM_MARK_SIZE = 256
NAME_PREFIX = "univex-logo"


def main() -> int:
    node = shutil.which("node")
    if node is None:
        print("build_brand_assets: node is required", file=sys.stderr)
        return 1
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        prefix = Path(scratch) / NAME_PREFIX
        subprocess.run([node, str(RENDERER), str(SOURCE_SVG), str(prefix), *map(str, PNG_SIZES)], check=True,
                       cwd=RENDERER.parent)
        for size in PNG_SIZES:
            shutil.copy2(f"{prefix}-{size}.png", PNG_DIR / f"{NAME_PREFIX}-{size}.png")
    largest = Image.open(PNG_DIR / f"{NAME_PREFIX}-{max(ICO_SIZES)}.png").convert("RGBA")
    largest.save(ICO_FILE, format="ICO", sizes=[(size, size) for size in ICO_SIZES])
    PRISM_MARK.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(PNG_DIR / f"{NAME_PREFIX}-{PRISM_MARK_SIZE}.png", PRISM_MARK)
    print(f"build_brand_assets: wrote {len(PNG_SIZES)} PNGs, {ICO_FILE.name} and {PRISM_MARK.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
