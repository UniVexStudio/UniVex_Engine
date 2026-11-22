#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Regenerates the raster brand assets from Engine/Distribution/brand/univex-logo.svg.

The logo uses SVG filters (lighting, grain) that only a browser engine renders faithfully, so the
PNGs are rendered with Chromium (render_svg.cjs, Playwright) and committed; builds never render.
Outputs:

    Engine/Distribution/brand/png/univex-logo-<size>.png   application and file icons
    Engine/Distribution/brand/univex-logo.ico              Windows executable and file icon
    Engine/Runtime/Prism/assets/images/univex_logo.png     the mark PRISM draws in title bars
    Engine/App/Android/res/                                the Android launcher icon: a PNG per screen
                                                           density, and an adaptive icon (background
                                                           colour + foreground layer) for Android 8+

Needs Node with Playwright and Python with Pillow. `--android` only rebuilds the Android icons from
the committed PNGs (Pillow alone).
"""

from __future__ import annotations

import argparse
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

# Android launcher icon (Engine/App/Android/res, compiled into every APK by build_apk.py).
ANDROID_RES_DIR = REPO_ROOT / "Engine" / "App" / "Android" / "res"
ANDROID_ICON_NAME = "ic_launcher"
ANDROID_FOREGROUND_NAME = "ic_launcher_foreground"
ANDROID_BACKGROUND_COLOR_NAME = "ic_launcher_background"
# Density bucket -> scale over the 160 dpi baseline.
ANDROID_DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
# A legacy launcher icon is 48dp; an adaptive icon's layers are 108dp, of which launchers show the
# middle 72dp (masked to the device's shape) and keep the middle 66dp always visible.
ANDROID_LEGACY_ICON_DP = 48
ANDROID_ADAPTIVE_LAYER_DP = 108
ANDROID_ADAPTIVE_VISIBLE_DP = 72
# The logo is a disc on a transparent square: its rim (this fraction of the radius, outermost) gives
# the adaptive background colour, so the masked shape continues the disc's edge.
ANDROID_RIM_FRACTION = 0.06
ANDROID_RIM_MIN_ALPHA = 250


def logo_rim_color(logo: Image.Image) -> tuple[int, int, int]:
    """Average colour of the logo disc's opaque outer rim."""
    rgba = logo.convert("RGBA")
    size = rgba.width
    centre = (size - 1) / 2
    outer = size / 2
    inner = outer * (1 - ANDROID_RIM_FRACTION)
    total = [0, 0, 0]
    count = 0
    for y in range(size):
        for x in range(size):
            distance = ((x - centre) ** 2 + (y - centre) ** 2) ** 0.5
            if inner <= distance <= outer:
                r, g, b, a = rgba.getpixel((x, y))
                if a >= ANDROID_RIM_MIN_ALPHA:
                    total = [total[0] + r, total[1] + g, total[2] + b]
                    count += 1
    if count == 0:
        raise RuntimeError("the logo has no opaque rim to take the icon background from")
    return tuple(round(channel / count) for channel in total)


def write_android_icons() -> None:
    """Writes the launcher icon for every density and the adaptive icon resources."""
    source = Image.open(PNG_DIR / f"{NAME_PREFIX}-{max(PNG_SIZES)}.png").convert("RGBA")
    for density, scale in ANDROID_DENSITIES.items():
        folder = ANDROID_RES_DIR / f"mipmap-{density}"
        folder.mkdir(parents=True, exist_ok=True)
        legacy = round(ANDROID_LEGACY_ICON_DP * scale)
        source.resize((legacy, legacy), Image.Resampling.LANCZOS).save(folder / f"{ANDROID_ICON_NAME}.png")
        # Foreground layer: the disc fills the visible 72dp, centred on the 108dp layer.
        layer = round(ANDROID_ADAPTIVE_LAYER_DP * scale)
        disc = round(ANDROID_ADAPTIVE_VISIBLE_DP * scale)
        foreground = Image.new("RGBA", (layer, layer), (0, 0, 0, 0))
        offset = (layer - disc) // 2
        foreground.alpha_composite(source.resize((disc, disc), Image.Resampling.LANCZOS), (offset, offset))
        foreground.save(folder / f"{ANDROID_FOREGROUND_NAME}.png")
    red, green, blue = logo_rim_color(source)
    values = ANDROID_RES_DIR / "values"
    values.mkdir(parents=True, exist_ok=True)
    (values / f"{ANDROID_BACKGROUND_COLOR_NAME}.xml").write_text(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n"
        f"    <color name=\"{ANDROID_BACKGROUND_COLOR_NAME}\">#{red:02X}{green:02X}{blue:02X}</color>\n"
        "</resources>\n", encoding="utf-8")
    adaptive = ANDROID_RES_DIR / "mipmap-anydpi-v26"
    adaptive.mkdir(parents=True, exist_ok=True)
    (adaptive / f"{ANDROID_ICON_NAME}.xml").write_text(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<adaptive-icon xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
        f"    <background android:drawable=\"@color/{ANDROID_BACKGROUND_COLOR_NAME}\"/>\n"
        f"    <foreground android:drawable=\"@mipmap/{ANDROID_FOREGROUND_NAME}\"/>\n"
        "</adaptive-icon>\n", encoding="utf-8")
    print(f"build_brand_assets: wrote the Android launcher icon for {len(ANDROID_DENSITIES)} densities")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--android", action="store_true", help="only rebuild the Android icons from the PNGs")
    if parser.parse_args().android:
        write_android_icons()
        return 0
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
    write_android_icons()
    return 0


if __name__ == "__main__":
    sys.exit(main())
