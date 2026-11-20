# Getting Started with UniVex Engine

UniVex Engine uses **UVBT** (`Engine/Tools/UVBT/uvbt.py`) as its single build system across Linux, Windows, and cross-compilation targets.

## Prerequisites

- **Linux (Ubuntu/Debian):** `g++` (C++23), `python3`, `ninja`, OpenGL/Vulkan dev headers (`libgl1-mesa-dev`, `libvulkan-dev`, `libx11-dev`, `libxrandr-dev`, `libxi-dev`, `libxcursor-dev`, `libxinerama-dev`).
- **Windows:** MinGW-w64 / GCC (C++23) or MSVC toolchain + `python3`.

## Quick Build Commands (UVBT)

```bash
# Build the full automated test suite (Debug)
python3 Engine/Tools/UVBT/uvbt.py build UniVexTests --config Debug

# Build the standalone Editor
python3 Engine/Tools/UVBT/uvbt.py build EditorApp --config Release

# Build a Shipping runtime binary (strips TRACE/INFO logs and assertions)
python3 Engine/Tools/UVBT/uvbt.py build RuntimeApp --config Shipping

# Enable strict warnings-as-errors
UVBT_WERROR=1 python3 Engine/Tools/UVBT/uvbt.py build UniVexTests --config Debug
```

## Running Tests & Tools

```bash
# Run headless automated tests (Linux with software GL / Vulkan lavapipe)
xvfb-run -a ./Binaries/Linux/Debug/UniVexTests

# Validate all built-in shaders and optionally cross-compile to SPIR-V
python3 Engine/Tools/compile_shaders.py --validate-sources

# Run VCS asset diff / 3-way merge tool
python3 Engine/Tools/uve_asset_diff.py diff path/a.uvmaterial path/b.uvmaterial

# Generate public API documentation (requires doxygen)
doxygen Doxyfile
```

## Module Layout

Every engine module under `Engine/Runtime/` and `Engine/Editor/` enforces a strict public/private split:
- `Expose/` — Public headers included by downstream modules and documented by `Doxyfile`.
- `Internal/` — Private implementation files (`*.cpp` and private headers).
- `Tests/` — Module unit tests automatically linked into `UniVexTests`.
- `<Module>.Build.cs` — Declarative module dependencies consumed by UVBT.
