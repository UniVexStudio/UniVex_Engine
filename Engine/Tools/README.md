# Engine/Tools

Repo-local development and CI utilities (no longer a placeholder - each tool below is real).

## check_math_boundary.py

Enforces the two-math-libraries boundary documented in
`Engine/Editor/Viewport/Internal/integration/MathConversions.h`:

- `univex/math` (the Viewport's OpenGL-facing math kit) may only be included under
  `Engine/Editor/Viewport/`, `Engine/App/`, and `Engine/Editor/EditorApp/`.
- `uve/math` (the engine-wide math library) may only enter the Viewport through its
  engine-bridge layer, `Engine/Editor/Viewport/Internal/integration/`.

Runs standalone (`python3 Engine/Tools/check_math_boundary.py`) and as a step in
`.github/workflows/ci.yml`. Exit code 1 with a full violation list on failure.

## embed_file.py

Byte-embeds a binary file as a C++20 `inline constexpr std::array<std::uint8_t, N>` `.inc`,
in this repo's established 16-hex-bytes-per-line convention. Driven by `add_custom_command`
in `Engine/Runtime/UI/CMakeLists.txt` and `Engine/Editor/EditorCore/CMakeLists.txt` so font
byte-arrays are generated from single canonical `.ttf` sources at build time instead of being
committed as duplicate snapshots. Verified byte-identical against
the four hand-committed `.inc` files it replaced.
