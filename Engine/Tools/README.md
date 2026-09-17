# Engine/Tools

Repo-local development and CI utilities (no longer a placeholder - each tool below is real).

## check_math_boundary.py

Enforces the two-math-libraries boundary documented in `AUDIT.md` (section 6.1) and in
`Engine/Editor/Viewport/Internal/integration/MathConversions.h`:

- `univex/math` (the Viewport's OpenGL-facing math kit) may only be included under
  `Engine/Editor/Viewport/`, `Engine/App/`, and `Engine/Editor/EditorApp/`.
- `uve/math` (the engine-wide math library) may only enter the Viewport through its
  engine-bridge layer, `Engine/Editor/Viewport/Internal/integration/`.

Runs standalone (`python3 Engine/Tools/check_math_boundary.py`) and as a step in
`.github/workflows/ci.yml`. Exit code 1 with a full violation list on failure.
