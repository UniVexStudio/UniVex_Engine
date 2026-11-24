# Third-Party Notices

UniVex Engine and UniVex Hub include, link against, or bundle the third-party libraries, fonts, and runtime components listed below. Each component remains under its own license.

When you ship a game or application built with UniVex Engine, keep this file (or the applicable notices below) in your product documentation or credits.

## Libraries fetched by UVBT

The core libraries below are downloaded into `ThirdParty/` by UVBT (`Engine/Tools/UVBT/uvbt.py`). GLFW, GLEW, FreeType, and libjpeg-turbo are fetched by UVBT for Android or static/fallback Linux builds and linked from system or MSYS2 packages on standard desktop builds.

| Component | Version / Tag | License | Upstream |
| --- | --- | --- | --- |
| ufbx | `v0.23.0` | MIT / Public Domain | <https://github.com/ufbx/ufbx> |
| fmt | `11.0.2` | MIT | <https://github.com/fmtlib/fmt> |
| nlohmann/json | `v3.11.3` | MIT | <https://github.com/nlohmann/json> |
| miniaudio | `0.11.25` | MIT-0 / Public Domain | <https://github.com/mackron/miniaudio> |
| SPIRV-Reflect | `vulkan-sdk-1.4.321.0` | Apache-2.0 | <https://github.com/KhronosGroup/SPIRV-Reflect> |
| Vulkan-Headers | `vulkan-sdk-1.4.357.0` | Apache-2.0 / MIT | <https://github.com/KhronosGroup/Vulkan-Headers> |
| Basis Universal | `v2_50` | Apache-2.0 | <https://github.com/BinomialLLC/basis_universal> |
| GLFW | `3.4` | zlib/libpng | <https://github.com/glfw/glfw> |
| GLEW (`glew-cmake`) | `glew-cmake-2.2.0` | Modified BSD / MIT | <https://github.com/Perlmint/glew-cmake> |
| FreeType | `VER-2-13-3` | FreeType License (FTL) | <https://github.com/freetype/freetype> |
| libjpeg-turbo | `3.1.0` | IJG / Modified BSD / zlib | <https://github.com/libjpeg-turbo/libjpeg-turbo> |
| GoogleTest (tests only) | `v1.15.2` | BSD-3-Clause | <https://github.com/google/googletest> |

Required attributions for the libraries above:

- **FreeType (FTL):** Portions of this software are copyright © The FreeType Project (<https://www.freetype.org>). All rights reserved.
- **Independent JPEG Group (libjpeg-turbo):** This software is based in part on the work of the Independent JPEG Group.

## Vendored code and bundled fonts

| Component | Repository Path | License | License / Notice File |
| --- | --- | --- | --- |
| stb_truetype.h (v1.26) | `Engine/Runtime/UI/thirdparty/stb_truetype.h` | MIT / Public Domain | [Engine/Runtime/UI/assets/fonts/THIRD_PARTY_NOTICES.md](Engine/Runtime/UI/assets/fonts/THIRD_PARTY_NOTICES.md) |
| Liberation Sans (subset) | `Engine/Runtime/UI/assets/fonts/liberation-sans-subset.ttf` | SIL Open Font License 1.1 | [Engine/Runtime/UI/assets/fonts/THIRD_PARTY_NOTICES.md](Engine/Runtime/UI/assets/fonts/THIRD_PARTY_NOTICES.md) |
| Inter (Regular, Medium, SemiBold) | `Engine/Runtime/Prism/assets/fonts/Inter-*.otf` | SIL Open Font License 1.1 | [Engine/Runtime/Prism/assets/fonts/LICENSE-Inter.txt](Engine/Runtime/Prism/assets/fonts/LICENSE-Inter.txt) |
| DejaVu Sans Mono | `Engine/Runtime/Prism/assets/fonts/DejaVuSansMono.ttf` | Bitstream Vera / DejaVu License | [Engine/Runtime/Prism/assets/fonts/LICENSE-DejaVu.txt](Engine/Runtime/Prism/assets/fonts/LICENSE-DejaVu.txt) |

## System and toolchain runtime libraries

- **zlib** (<https://github.com/madler/zlib>): zlib license. Linked from the host system on Linux, the Android NDK on Android, and MSYS2 on Windows (and pinned to `v1.3.1` in `Engine/Tools/headless_gl_toolchain.sh` for headless OpenGL builds).
- **libcurl** (<https://curl.se/docs/copyright.html>): curl license (MIT/X derivative). Used on Linux for Hub and Editor HTTP requests.
- **MinGW-w64 / GCC UCRT64 runtime DLLs** (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`): GNU GPLv3 with the GCC Runtime Library Exception (<https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html>), which allows unrestricted linking and commercial distribution of compiled binaries, and MIT/BSD for `winpthreads`. Bundled in Windows packages.
- **Android NDK Native App Glue** (`android_native_app_glue`): Apache-2.0 (<https://developer.android.com/ndk>). Linked into Android builds.
