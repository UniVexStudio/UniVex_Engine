#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Enforces the two-math-libraries boundary (2026-09-17 audit finding / AUDIT.md section 6.1).

Two math libraries coexist deliberately:

  * univex::math  - Engine/Editor/Viewport's own OpenGL-facing kit (Expose/univex/math):
                    column-major Mat4, OpenGL clip-space projections. Must stay inside the
                    Viewport and the two app targets that legitimately drive a viewport.
  * UVE::Math     - Engine/Runtime/Core/Math's engine-wide kit (row-major, Vulkan-style
                    depth). Must never leak into the Viewport's engine-agnostic core/gl
                    libraries - only the viewport ENGINE-BRIDGE (Internal/integration) may
                    see it, and only through integration/MathConversions.

Exits non-zero and prints every violation it finds. Run from anywhere:
    python3 Engine/Tools/check_math_boundary.py [repo_root]
"""

import re
import sys
from pathlib import Path

SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".c", ".cc", ".cxx", ".inl"}
SKIP_DIRS = {".git", "build", "node_modules", ".vs", "out"}

# Directories allowed to #include "univex/math": the Viewport module itself plus the two app
# layer targets that wire a viewport into a real process. Everything else is engine land and
# must use UVE::Math exclusively.
UNIVEX_MATH_ALLOWED_PREFIXES = (
    "Engine/Editor/Viewport/",
    "Engine/App/",
    "Engine/Editor/EditorApp/",
)

# Inside the Viewport itself, only the engine-bridge layer may #include "uve/math" - and the
# intent is that all such crossings go through integration/MathConversions.h.
UVE_MATH_ALLOWED_IN_VIEWPORT_PREFIX = "Engine/Editor/Viewport/Internal/integration/"

UNIVEX_MATH_INCLUDE = re.compile(r'#\s*include\s*[<"]univex/math/')
UVE_MATH_INCLUDE = re.compile(r'#\s*include\s*[<"]uve/math/')


def iter_source_files(repo_root: Path):
    for path in repo_root.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_EXTENSIONS:
            continue
        if SKIP_DIRS & set(path.parts):
            continue
        yield path


def main() -> int:
    repo_root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    violations = []
    for path in iter_source_files(repo_root):
        rel = path.relative_to(repo_root).as_posix()
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        has_univex_math = UNIVEX_MATH_INCLUDE.search(text)
        has_uve_math = UVE_MATH_INCLUDE.search(text)
        if has_univex_math and not rel.startswith(UNIVEX_MATH_ALLOWED_PREFIXES):
            violations.append(
                f"{rel}: includes univex/math outside the boundary.\n"
                f"    univex::math is the Viewport's private OpenGL-facing math kit; engine code must use\n"
                f"    UVE::Math (Engine/Runtime/Core/Math). Allowed: {', '.join(UNIVEX_MATH_ALLOWED_PREFIXES)}"
            )
        if (
            has_uve_math
            and rel.startswith("Engine/Editor/Viewport/")
            and not rel.startswith(UVE_MATH_ALLOWED_IN_VIEWPORT_PREFIX)
        ):
            violations.append(
                f"{rel}: includes uve/math inside the Viewport outside the engine-bridge layer.\n"
                f"    The Viewport's core/gl libraries stay engine-agnostic; cross-library data may only move\n"
                f"    through {UVE_MATH_ALLOWED_IN_VIEWPORT_PREFIX} (see integration/MathConversions.h)."
            )
    if violations:
        print("math boundary check FAILED:")
        for violation in violations:
            print(f"\n  {violation}")
        return 1
    print("math boundary check passed (AUDIT.md section 6.1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
