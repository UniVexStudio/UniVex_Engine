# PRISM

PRISM is UniVex's own retained-mode UI framework (C++23). It provides the widget tree, two-pass layout, popups,
docking, window chrome, the data views (tree, table, log, property grid), the editor tools (asset browser, curve,
colour and code editors, transform gizmo), and the blueprint graph editor with its compiler and VM.

## Layout
- `Expose/prism/` - public headers (`#include "prism/Application.h"`, ...)
- `Internal/` - implementation; `Internal/gl/` vendored Khronos GL headers for the OpenGL backend;
  `Internal/windows/uiautomationcore.def` - MinGW import library source for the UI Automation bridge
- `assets/` - fonts (Inter, DejaVu Sans Mono; licenses included), themes, locale tables.
  UVBT copies them next to every binary that links PRISM as `prism_assets/`.
- `tests/` - unit, fuzz and golden visual regression tests (`tests/golden/*.sig`)

## Build (UVBT)
Requires FreeType (Debian/Ubuntu `libfreetype-dev`, MSYS2 `mingw-w64-ucrt-x86_64-freetype`).

    ./uvbt build UniVexPrismTests Linux Release
    ./.uvbt/bin/Linux/Release/uve_prism_tests Engine/Runtime/Prism/assets

`PRISM_UPDATE_GOLDEN=1` re-records the goldens after an intentional visual change.

## Hosting
The engine owns the window and loop: `Application::initHosted(...)`, forward input with `host*`, call `tick()`, and
render through `GlBackend::create(loader, {}, false)`; composite `GlBackend::colorTexture()` over the engine's
viewport textures placed under `EngineView` (Hole mode) rectangles.
