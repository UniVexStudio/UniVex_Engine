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

Byte-embeds a binary file as a C++23 `inline constexpr std::array<std::uint8_t, N>` `.inc`,
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

## headless_gl_toolchain.sh

Builds a private desktop-GL + Vulkan toolchain into `~/toolchain` for a Linux host that has no
package manager, no display and no GPU - only gcc, python3/pip and HTTPS to github.com (codeload),
pypi.org and registry.npmjs.org. That is the sandbox this engine is developed in: `apt` is blocked,
and so are GitHub *release assets*, which is why nothing below is a prebuilt download from a
release page.

| Piece | Source | Why |
|---|---|---|
| cmake, ninja, meson, bison 3.8.2, flex 2.6.4, pkgconf | PyPI wheels (`bison-bin`, `flex-bin`, ...) | Debian would supply these; the wheels are prebuilt manylinux binaries. |
| GNU m4 1.4.18 | `tar-mirror/gnu-m4` (release-tarball git import) | bison and flex exec `m4` at runtime; ftp.gnu.org is unreachable. Two one-line glibc patches. |
| Mesa 25.0.7 - `libOSMesa.so.8`, softpipe, OpenGL 3.3 Core | `sailfishos-mirror/mesa` tag tarball | Headless GL with no X11/DRM/LLVM. 25.0 is the **last** Mesa with OSMesa (25.1 removed it). `libGL.so` is a symlink to it: OSMesa exports the whole desktop-GL ABI, so `find_package(OpenGL)` and the linker are happy. |
| GLFW 3.4, Null platform only | `glfw/glfw` tag tarball | One-line patch so `GLFW_ANY_PLATFORM` auto-selects Null (upstream refuses); contexts are then created through OSMesa, so `WindowManagerUVE`'s plain `glfwInit()` + `glfwCreateWindow()` work unchanged. |
| GLEW 2.2 in `GLEW_OSMESA` mode | `Perlmint/glew-cmake` (pre-generated `glew.c`) | The editor viewport's `glewInit()` resolves through `OSMesaGetProcAddress`. |
| SwiftShader Vulkan 1.3 CPU ICD + loader | `@sparticuz/chromium` npm tarball (`bin/swiftshader.tar.br`) | The only prebuilt Linux Vulkan implementation on a reachable host; it is Chromium's bundled copy. `VK_ICD_FILENAMES` points at it. |

The script is idempotent (each step checks its install artefact), writes `~/.univex_env`
and sources it from `~/.bashrc`. The env file carries more than PATH:

- `CMAKE_PREFIX_PATH`, `PKG_CONFIG_PATH`, `LD_LIBRARY_PATH`, `M4` - the usual prefix wiring.
- `C_INCLUDE_PATH` / `CPLUS_INCLUDE_PATH` / `LIBRARY_PATH` - a few test targets include
  `<GL/gl.h>` and `<GLFW/glfw3.h>` directly, relying on a system-wide install; this makes
  `~/toolchain` look like one without touching the CMake files.
- `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` - the SwiftShader ICD manifest.
- `MESA_GL_VERSION_OVERRIDE=4.6` + `MESA_GLSL_VERSION_OVERRIDE=460` - softpipe tops out at a
  real GL 3.3 Core because it has no tessellation, and `WindowDescUVE` defaults to a 4.6 Core
  context request. The override only changes the advertised version string: anything the
  engine needs from 3.3 (compute, SSBO, image load/store, indirect draws) is really there,
  while tessellation, `shader_draw_parameters`, `texture_barrier`, BC compression and MSAA
  beyond 1x remain absent and the few tests that need them `GTEST_SKIP`. Without the
  override every GL/Window fixture skips with "No display available".

Afterwards configure with the legacy `FindOpenGL` mode (there is no GLVND `libOpenGL`/`libGLX`
here, only the OSMesa-backed `libGL.so`); `UVE_HAS_DESKTOP_GL` turns on and every GL-gated
target - `uve_rhi_opengl`, the GLFW `WindowManagerUVE`, `univex_viewport_gl`, the editor -
compiles and runs headless:

```sh
source ~/.univex_env
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUVE_BUILD_TESTS=ON \
      -DCMAKE_PREFIX_PATH="$HOME/toolchain" -DOpenGL_GL_PREFERENCE=LEGACY \
      -DOPENGL_INCLUDE_DIR="$HOME/toolchain/include" -DOPENGL_gl_LIBRARY="$HOME/toolchain/lib/libGL.so"
cmake --build build -j2            # 2 vCPU / 3.8 GB RAM box: -j2 keeps the C++23 TUs from swapping
ctest --test-dir build --output-on-failure -j2
cd /tmp && "$OLDPWD/build/Engine/Editor/Viewport/univex_headless_capture" --out /tmp/frame.ppm --width 640 --height 400
```

(`univex_headless_capture` writes `frame.ppm` into the *current* directory when `--out` is
omitted and silently ignores unknown flags, so run it from `/tmp` - the repository root carries
a committed reference `frame.ppm`.)

### Screenshots of the real editor - no X11 needed

There is no Xvfb and no way to build one here (the X server needs libX11/libxcb, pixman,
libXfont2, libxkbfile, libxcvt and a Mesa rebuilt with GLX - most of them autotools-only,
hours of work, and it would only add real input events and multiple OS windows). It is also
unnecessary for pictures: under the Null platform the window *is* an RGBA buffer in memory,
so the full ImGui editor renders exactly as it would on screen, and `glfwSwapBuffers()` is the
one place every frame passes through. `headless_framedump.c` (built by the script into
`~/toolchain/lib/libuve_framedump.so`) hooks it via `LD_PRELOAD`, writes the default
framebuffer to a PPM on the frames you name, and can request a window close so programs
without a frame limit still exit cleanly:

```sh
source ~/.univex_env
mkdir -p /tmp/editor_run && cd /tmp/editor_run        # the editor writes Content/, .uvsettings, logs into cwd
UVE_FRAMEDUMP=/tmp/editor_run/editor.ppm UVE_FRAMEDUMP_FRAMES=12 \
  LD_PRELOAD="$HOME/toolchain/lib/libuve_framedump.so" "$REPO/build/Engine/App/uve_editor" --frames 12   # REPO = your checkout
python3 -c "from PIL import Image; Image.open('/tmp/editor_run/editor.ppm').save('/tmp/editor_run/editor.png')"
```

`UVE_FRAMEDUMP_FRAMES` takes `12`, `4,8,30` or `every:10` (several frames -> `editor_0012.ppm`
etc.); `UVE_FRAMEDUMP_CLOSE=N` closes the window after swap N (for `univex_editor`,
`univex_viewport_demo`, the runtime). Budget ~2 s per 1280x720 editor frame on softpipe; the
first dumped frame should be >= 3, so the panels have drawn through a full frame and the
viewport's render target has something in it. (It used to say "so ImGui's docking layout has
settled" - there is no dock layout to settle now: the frame computes its own rects on frame
one.) Two things the toolchain does to make this work, both in the script: `libGL.so.1` is a
tiny GLX shim over OSMesa (`glXGetProcAddressARB` -> `OSMesaGetProcAddress`) because Dear
ImGui's bundled loader refuses a `libGL` without it - the editor's UI silently never
initialised before that - and `libGL.so` is a GNU ld script that links both the shim and
`libOSMesa.so.8`. For *driving* the editor rather than just looking at it,
`uve_editor --bridge-stdio` already exposes the editor's commands over JSON-RPC without any
window.

Caveats: softpipe is a pure-CPU rasterizer (no LLVM JIT), so GL work is correct but slow -
fine for tests and frame captures, not for interactive use. It also has no robust buffer
access: a draw that fetches vertices past the end of its buffer reads garbage instead of the
zeros a GPU driver or llvmpipe would return, so geometry that looks fine in CI can explode into
random full-screen triangles here. Treat that as a bug report, not a renderer quirk - it is
exactly how the gizmo renderer's over-long `glDrawArrays` count was found. The GLFW Null platform has no
real monitors, input or vsync; `glfwSwapBuffers` is a no-op and the framebuffer lives in
client memory (read it back with `glReadPixels`). CI (`.github/workflows/ci.yml`) still runs
against real Mesa GL under Xvfb and lavapipe Vulkan - this toolchain is for the dev loop, not
a replacement for that check.
