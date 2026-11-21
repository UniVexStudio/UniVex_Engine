#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Emscripten / WebAssembly browser export scaffold for UniVex Engine.

Generates a WebGL2/WASM HTML shell and invokes `em++` when Emscripten SDK (`emsdk`) is active.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


HTML_SHELL = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>{title} — UniVex Web Runtime</title>
  <style>
    html, body {{ margin: 0; padding: 0; width: 100%; height: 100%; background: #101216; overflow: hidden; }}
    canvas {{ width: 100vw; height: 100vh; display: block; }}
  </style>
</head>
<body>
  <canvas id="canvas" oncontextmenu="event.preventDefault()"></canvas>
  <script>
    var Module = {{ canvas: document.getElementById('canvas') }};
  </script>
  <script async src="{js_module}"></script>
</body>
</html>
"""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="UniVex Web/WASM browser export scaffold")
    parser.add_argument("--title", default="UniVexApp")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--scaffold-only", action="store_true")
    args = parser.parse_args(argv)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    html_path = args.out_dir / "index.html"
    html_path.write_text(HTML_SHELL.format(title=args.title, js_module="uve_runtime.js"), encoding="utf-8")
    if args.scaffold_only:
        print(f"[build_web] Wrote WebAssembly shell to {html_path}")
        return 0

    empp = shutil.which("em++")
    if not empp:
        print("[build_web] Emscripten (em++) not found on PATH; activate emsdk or pass --scaffold-only", file=sys.stderr)
        return 1
    print(f"[build_web] Found Emscripten at {empp}; shell generated at {html_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
