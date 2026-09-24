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

## editor_icons/

The editor's built-in icons: one per scene node type, one per node palette category and one per
content browser type, drawn in one shared style (solid 3D shapes lit from the upper left, one
colour family per category).

- `kit.py` - the drawing primitives (boxes, spheres, cylinders, figures, tiles).
- `icons.py` - every icon, registered by group and id. A node's id is its registry typeId, a
  category's is its name in lower case, a content type's is its label in lower case.
- `build_editor_icons.py` - writes `<group>/<id>.svg` and a 64 px `<id>.png` for each icon under
  `Engine/Editor/EditorCore/assets/icons/`, rasterizing with headless Chromium
  (`--chromium PATH` or `$UVE_CHROMIUM`). `--gallery page.html` also writes a preview sheet.
  Run it after changing an icon and commit both files; stale files are removed.
- `embed_editor_icons.py` - run by the EditorCore build to embed the committed PNGs as one table.
  The build never needs a browser.

The editor's tests fail if a node type, palette category or content browser type has no icon, so
adding one of those means adding its icon here.
