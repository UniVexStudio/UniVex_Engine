#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Regenerates the editor's built-in icons from icons.py.

For every registered icon this writes:
1. Standalone Outliner / Scene / Component SVGs and 64x64 PNGs under:
   - Engine/Editor/EditorCore/assets/icons/objects/
   - Engine/Editor/EditorCore/assets/icons/components/
   - Engine/Editor/EditorCore/assets/icons/object_categories/
   - Engine/Editor/EditorCore/assets/icons/content_types/
2. Studio Card ("Box na Parihaba" + auto 3D/2D/UI badge) SVGs and 64x64 PNGs under:
   - Engine/Editor/EditorCore/assets/icons/object_cards/
   - Engine/Editor/EditorCore/assets/icons/content_type_cards/

Runs a strict monochrome color audit on every generated SVG (exempting the preserved folder).
Fast batch rasterization via headless Edge/Chromium.
"""

import argparse
import base64
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import icons  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]
ICON_ROOT = REPO_ROOT / "Engine" / "Editor" / "EditorCore" / "assets" / "icons"
GROUPS = (icons.OBJECTS, icons.OBJECT_CATEGORIES, icons.CONTENT_TYPES, icons.COMPONENTS)
CARD_GROUPS = {
    icons.OBJECTS: "object_cards",
    icons.CONTENT_TYPES: "content_type_cards",
}
RASTER_SIZE = 64
CHROMIUM_CANDIDATES = (
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "headless_shell",
    "chromium",
    "chromium-browser",
    "google-chrome",
)

HEX_RE = re.compile(r"#([0-9a-fA-F]{6})\b")


def assert_monochrome_svg(svg_text, label):
    """Strictly verifies that every hex color in the SVG is pure neutral gray/white (R~=G~=B)."""
    if "folder" in label.lower():
        return
    for match in HEX_RE.finditer(svg_text):
        h = match.group(1)
        r, g, b = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        if max(r, g, b) - min(r, g, b) > 0.05:
            raise ValueError(f"Non-monochrome color #{h} detected in {label}!")


def find_chromium(explicit):
    if explicit:
        return explicit
    if os.environ.get("UVE_CHROMIUM"):
        return os.environ["UVE_CHROMIUM"]
    for name in CHROMIUM_CANDIDATES:
        if os.path.isfile(name):
            return name
        found = shutil.which(name)
        if found:
            return found
    return None


def clean_old_directories():
    for old_dir in ("sample_pbr", "ue5_monochrome_samples"):
        d = ICON_ROOT / old_dir
        if d.is_dir():
            shutil.rmtree(d)
            print(f"Removed legacy directory: {d.relative_to(REPO_ROOT)}")


def batch_rasterize(chromium, entries):
    """Rasterizes all SVG entries to 64x64 PNG in a single headless browser session via HTML5 canvas."""
    port = 8794
    items_to_render = [{"path": str(png_p), "svg": svg_text} for (_, _, _, png_p, svg_text) in entries]
    done_event = threading.Event()
    results = {}

    class BatchHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_GET(self):
            if self.path == "/":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                payload = json.dumps(items_to_render)
                html = f"""<!doctype html>
<html><body>
<canvas id="c" width="{RASTER_SIZE}" height="{RASTER_SIZE}"></canvas>
<script>
const items = {payload};
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d');

function renderItem(item) {{
    return new Promise((resolve) => {{
        const blob = new Blob([item.svg], {{type: 'image/svg+xml'}});
        const url = URL.createObjectURL(blob);
        const img = new Image();
        img.onload = () => {{
            ctx.clearRect(0, 0, {RASTER_SIZE}, {RASTER_SIZE});
            ctx.drawImage(img, 0, 0, {RASTER_SIZE}, {RASTER_SIZE});
            URL.revokeObjectURL(url);
            const dataUrl = canvas.toDataURL('image/png');
            resolve({{path: item.path, data: dataUrl}});
        }};
        img.onerror = () => {{
            URL.revokeObjectURL(url);
            resolve({{path: item.path, error: 'img load failed'}});
        }};
        img.src = url;
    }});
}}

async function run() {{
    const out = [];
    for (let i = 0; i < items.length; i++) {{
        out.push(await renderItem(items[i]));
    }}
    await fetch('/done', {{
        method: 'POST',
        headers: {{'Content-Type': 'application/json'}},
        body: JSON.stringify(out)
    }});
}}
run();
</script></body></html>"""
                self.wfile.write(html.encode("utf-8"))
            else:
                self.send_error(404)

        def do_POST(self):
            if self.path == "/done":
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length)
                results["items"] = json.loads(body.decode("utf-8"))
                self.send_response(200)
                self.end_headers()
                done_event.set()

    server = http.server.HTTPServer(("127.0.0.1", port), BatchHandler)
    server_thread = threading.Thread(target=server.serve_forever)
    server_thread.daemon = True
    server_thread.start()

    start_time = time.time()
    proc = subprocess.Popen([
        chromium, "--headless", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
        "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
        f"http://127.0.0.1:{port}/"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    done = done_event.wait(timeout=90)
    proc.kill()
    server.shutdown()
    elapsed = time.time() - start_time

    if not done:
        raise RuntimeError("Batch rasterization timed out after 90 seconds!")

    items = results.get("items", [])
    saved = 0
    errors = []
    for item in items:
        if "error" in item:
            errors.append(f"{item['path']}: {item['error']}")
        elif "data" in item and item["data"].startswith("data:image/png;base64,"):
            png_bytes = base64.b64decode(item["data"].split(",", 1)[1])
            p = Path(item["path"])
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(png_bytes)
            saved += 1

    if errors:
        raise RuntimeError(f"Encountered errors during rasterization: {errors[:5]}")
    print(f"Successfully generated all {saved} 64x64 PNG icons in {elapsed:.2f}s.")


def main():
    parser = argparse.ArgumentParser(description="Regenerate the editor icon SVGs (and optionally PNGs)")
    parser.add_argument("--chromium", help="Chromium/Edge binary")
    parser.add_argument("--svg-only", action="store_true", help="Write and verify all SVGs without rasterizing PNGs")
    args = parser.parse_args()

    clean_old_directories()

    svg_entries = []
    for (group, icon_id) in icons.ICONS:
        directory = ICON_ROOT / group
        directory.mkdir(parents=True, exist_ok=True)
        svg_path = directory / f"{icon_id}.svg"
        png_path = directory / f"{icon_id}.png"
        svg_text = icons.build((group, icon_id)) + "\n"
        assert_monochrome_svg(svg_text, f"{group}/{icon_id}.svg")
        svg_path.write_text(svg_text, encoding="utf-8")
        svg_entries.append((group, icon_id, svg_path, png_path, svg_text))

        if group in CARD_GROUPS:
            card_dir = ICON_ROOT / CARD_GROUPS[group]
            card_dir.mkdir(parents=True, exist_ok=True)
            card_svg_path = card_dir / f"{icon_id}.svg"
            card_png_path = card_dir / f"{icon_id}.png"
            card_svg_text = icons.build_card((group, icon_id)) + "\n"
            assert_monochrome_svg(card_svg_text, f"{CARD_GROUPS[group]}/{icon_id}.svg")
            card_svg_path.write_text(card_svg_text, encoding="utf-8")
            svg_entries.append((CARD_GROUPS[group], icon_id, card_svg_path, card_png_path, card_svg_text))

    print(f"Verified & wrote {len(svg_entries)} SVGs across {len(icons.ICONS)} registered definitions.")

    # Remove stale files
    all_target_dirs = list(GROUPS) + list(CARD_GROUPS.values())
    owned_paths = {entry[2] for entry in svg_entries} | {entry[3] for entry in svg_entries}
    removed_count = 0
    for target_dir in all_target_dirs:
        d = ICON_ROOT / target_dir
        if d.is_dir():
            for f in d.iterdir():
                if f.is_file() and f not in owned_paths:
                    f.unlink()
                    removed_count += 1
    if removed_count > 0:
        print(f"Cleaned up {removed_count} stale icon files.")

    if args.svg_only:
        return 0

    chromium = find_chromium(args.chromium)
    if chromium is None:
        print("build_editor_icons.py: no Chromium/Edge found for rasterization.", file=sys.stderr)
        return 1

    print(f"Fast-rasterizing {len(svg_entries)} icons to 64x64 PNG with {chromium}...")
    batch_rasterize(chromium, svg_entries)
    return 0


if __name__ == "__main__":
    sys.exit(main())
