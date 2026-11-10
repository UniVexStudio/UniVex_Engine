# UniVex Engine

<div align="center">

**A modern, high-performance, modular 2D and 3D real-time game and simulation engine engineered in C++23.**

[![C++23](https://img.shields.io/badge/Language-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Build System](https://img.shields.io/badge/Build%20System-UVBT-green.svg)](#univex-build-tool-uvbt)
[![Platforms](https://img.shields.io/badge/Platforms-Linux%20%7C%20Windows%20%7C%20Android-orange.svg)](#supported-platforms)
[![Graphics Backends](https://img.shields.io/badge/RHI-Vulkan%20%7C%20OpenGL-red.svg)](#rendering-hardware-interface-rhi)
[![Tests](https://img.shields.io/badge/Tests-5%2C200%2B%20Passing-brightgreen.svg)](#testing--verification)
[![License](https://img.shields.io/badge/License-Proprietary%20%2F%20UniVex%20Studios-lightgrey.svg)](#license)

</div>

---

## Overview

**UniVex Engine** is an extensible, production-grade real-time engine built from the ground up to power next-generation interactive applications, simulation, and games. Designed with a modular architecture similar to Unreal Engine's subsystem decoupling, UniVex Engine combines low-level data-oriented performance with high-level developer ergonomics.

The engine features its own specialized C#-driven modular build system (**UniVex Build Tool / UVBT**), a unified **Rendering Hardware Interface (RHI)** supporting modern Vulkan and OpenGL backends, an offline **asset cooking pipeline**, native **gameplay scripting (UVScript)**, and a fully dockable **interactive editor workspace**.

---

## Key Features & Architecture

```
                                  +-----------------------+
                                  |     UniVex Editor     |
                                  |   (Docking / Viewport)|
                                  +-----------+-----------+
                                              |
                   +--------------------------+--------------------------+
                   |                                                     |
       +-----------v-----------+                             +-----------v-----------+
       |   Scene & ECS Graph   |                             |   Asset & Cooker DB   |
       | (Transforms, Lighting)|                             | (uve_pack / Streaming)|
       +-----------+-----------+                             +-----------+-----------+
                   |                                                     |
       +-----------v-----------------------------------------------------v-----------+
       |                           UniVex Engine Core Runtime                        |
       |  Physics  *  Audio (Miniaudio)  *  Animation & Retarget  *  UVScript VM     |
       +--------------------------------------+--------------------------------------+
                                              |
       +--------------------------------------v--------------------------------------+
       |                       Rendering Hardware Interface (RHI)                    |
       |                   [ Vulkan Backend ]    [ OpenGL Backend ]                  |
       +--------------------------------------+--------------------------------------+
                                              |
       +--------------------------------------v--------------------------------------+
       |                         Platform & Window Subsystem                         |
       |                         (Linux / Windows / Android)                         |
       +-----------------------------------------------------------------------------+
```

### 1. UniVex Build Tool (UVBT)
* **C# Module Rules**: Every subsystem and plugin defines its dependencies, include paths, definitions, and system libraries via clean `.Build.cs` and `.Target.cs` specifications.
* **Deterministic Build Graph**: UVBT automatically parses module hierarchies, tracks dependency order, hashes sources for instant incremental builds, and links with whole-archive unit test auto-registration.
* **Multi-Target Output**: Easily build Editor (`UniVexEditor`), Standalone Runtime (`UniVexRuntime`), Asset Packer (`UniVexPack`), Asset Workload Benchmark (`UniVexAssetWorkload`), and Test Suites (`UniVexTests`).

### 2. Rendering Hardware Interface (RHI)
* **Multi-Backend Architecture**: Modern RHI abstracting both **Vulkan** (SPIR-V pipelines, compute dispatch, descriptor sets) and **modern OpenGL** contexts.
* **Advanced Pipeline Features**:
  * Depth prepass and cascaded shadow maps (CSM).
  * Compute shader storage buffers and image stores.
  * SPIR-V reflection (`spirv-reflect`) for automated uniform, push-constant, and descriptor layout matching.
  * Texture 2D arrays, 3D volumes, cubemaps, and sRGB color space precision.
  * Monotonic occlusion queries and GPU timestamp profiling.

### 3. Entity-Component-System (ECS) & Scene Graph
* **Data-Oriented Hierarchy**: High-density component allocations with spatial transform hierarchies, entity lifetime tracking, and tick scheduling.
* **Lighting & Environment**: Directional lights, point lights, spot lights, ambient irradiance, and environment maps.
* **Cameras & Viewports**: Perspective and orthographic projections, spring arms, depth sorting, and multi-viewport composition.

### 4. Animation & Humanoid Retargeting
* **Skeletal Animation**: Forward kinematic evaluation, blend spaces, and state machines.
* **Humanoid Retargeting**: Cross-skeleton motion retargeting with standardized bone mappings and resting poses (`humanoid_reference.json`).

### 5. Physics & Spatial Simulation
* **Rigid Body Dynamics**: 3D and 2D collision detection, velocity integration, and constraint solving.
* **Raycasting & Spatial Queries**: Accelerated bounding-volume hierarchy (BVH) queries for sweep tests, line-of-sight checks, and character collision.

### 6. Scripting Runtime (UVScript)
* **Native Toolchain (`uvsc`)**: Compiles typed `.uvs` scripts into native/bytecode runtime structures with reflection against engine host objects (`.uvhost`).
* **Hot Reloading & Coroutines**: Built-in support for time-slicing, execution yields, and live gameplay updates.

### 7. Asset Management & Cooker
* **High-Efficiency Compression**: Native Basis Universal transcoder (`.ktx2`) with full GPU-compressed texture streaming (ASTC, BC7, ETC).
* **FBX & Model Pipeline**: High-performance skeletal and static mesh ingestion powered by `ufbx`.
* **Deterministic Distribution**: Standalone project cooker (`uve_pack`) packages assets, shaders, and scenes into optimized binary archives (`.uvassetdb`).

### 8. UniVex Editor Workspace
* **Dockable Workspace**: Powered by Dear ImGui with custom high-DPI font rasterization and vector icon fonts.
* **Live Tooling**: Viewport gizmos, Scene Outliner, Property Inspector, Asset Browser, Console/Log Viewer, and single-click Play-in-Editor (PIE) simulation.

---

## Repository Structure

```
.
├── Engine/
│   ├── Editor/          # UniVex Editor workspace, panels, viewport, tools
│   │   ├── EditorCore/  # Docking, layout, widgets, inspector, themes
│   │   ├── EditorApp/   # Editor executable entrypoint and loop
│   │   └── Viewport/    # Scene viewport, camera controls, gizmos
│   ├── Runtime/         # Core game engine runtime
│   │   ├── Animation/   # Skeletal animation, blending, state machines
│   │   ├── Asset/       # Texture, mesh, audio, and scene asset pipelines
│   │   ├── Audio/       # Miniaudio-based cross-platform audio engine
│   │   ├── Component/   # ECS components (Render, Physics, Camera, etc.)
│   │   ├── Engine/      # Engine loop, initialization, frame orchestrator
│   │   ├── Input/       # Input mapping, keyboard, mouse, gamepad
│   │   ├── Math/        # SIMD-accelerated vectors, matrices, quaternions
│   │   ├── Physics/     # 2D and 3D collision detection and physics dynamics
│   │   ├── Retarget/    # Humanoid skeletal retargeting system
│   │   ├── RHI/         # Rendering Hardware Interface (Vulkan / OpenGL)
│   │   ├── Scene/       # Scene tree, nodes, spatial management
│   │   ├── UVScript/    # Script compiler (uvsc) and runtime interpreter
│   │   └── Window/      # OS windowing, events, multi-monitor management
│   └── Tools/           # Build tools, asset processors, static checkers
│       └── UVBT/        # UniVex Build Tool (C# / Python build system)
├── Test/                # 5,200+ unit, integration, and performance tests
├── ThirdParty/          # Bundled third-party libraries (GLFW, GLEW, ufbx, etc.)
├── .github/             # Continuous integration workflows and CI configs
└── uvbt                 # UVBT launcher script (Linux / macOS / Windows)
```

---

## Supported Platforms

| Platform | Architecture | Graphics Backends | Compiler Toolchain |
|---|---|---|---|
| **Linux (Ubuntu 22.04+)** | x86_64, arm64 | Vulkan 1.3, OpenGL 4.5 Core | GCC 13+ / Clang 17+ |
| **Windows 10 / 11** | x86_64 | Vulkan 1.3, OpenGL 4.5 Core | MSYS2 UCRT64 / MinGW-w64 |
| **Android (Experimental)** | arm64-v8a | Vulkan 1.1 Mobile, OpenGL ES 3.2 | Android NDK r25+ |

---

## Getting Started

### Prerequisites

#### Linux (Ubuntu / Debian)
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  g++ \
  ninja-build \
  libglfw3-dev \
  libglew-dev \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  zlib1g-dev \
  libjpeg-dev \
  libvulkan-dev \
  vulkan-tools \
  python3
```

#### Windows (MSYS2 UCRT64)
In an MSYS2 UCRT64 shell:
```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-python \
  mingw-w64-ucrt-x86_64-glfw \
  mingw-w64-ucrt-x86_64-glew \
  mingw-w64-ucrt-x86_64-zlib \
  mingw-w64-ucrt-x86_64-libjpeg-turbo
```

---

### Building with UVBT

UniVex Engine uses **UVBT** to manage compilation and linking:

#### 1. Build the Editor
```bash
./uvbt build UniVexEditor Linux Debug -j$(nproc)
```
*(On Windows: `./uvbt build UniVexEditor Windows Release -j4`)*

#### 2. Build the Standalone Runtime
```bash
./uvbt build UniVexRuntime Linux Debug -j$(nproc)
```

#### 3. Build the Asset Packer
```bash
./uvbt build UniVexPack Linux Debug -j$(nproc)
```

#### 4. Build and Run the Test Suite
```bash
# Build test runner
./uvbt build UniVexTests Linux Debug -j$(nproc)

# Run full test suite
./.uvbt/bin/Linux/Debug/UniVexTests
```

---

### Launching the Editor

Once compiled, launch the UniVex Editor executable:

```bash
# Linux
./.uvbt/bin/Linux/Debug/uve_editor

# Headless verification run
./.uvbt/bin/Linux/Debug/uve_editor --headless --frames 60
```

---

## Testing & Verification

UniVex Engine enforces strict engineering standards through automated CI gates on every commit and pull request:

* **5,200+ Unit & Integration Tests**: Comprehensive GoogleTest suites verifying Math, Allocators, Containers, ECS, Asset Streaming, Animation, Physics, Windowing, OpenGL, and Vulkan RHI.
* **Math Boundary Verification**: `python3 Engine/Tools/check_math_boundary.py` guarantees numerical safety across all matrix and vector transformations.
* **Vocabulary Ratchet Audit**: `python3 Engine/Tools/check_engine_vocabulary.py` enforces modern, unified naming conventions across all APIs.
* **Panel Include Audit**: `bash Engine/Tools/check_panel_includes.sh` ensures zero transitive standard library pollution across Editor translation units.
* **Cooked Asset Workload Pipeline**: End-to-end cooking verification measuring process memory footprints, asset loading latency, and frame delivery.

---

## Documentation

* [Engine Foundation Types & Resources (`FOUNDATION.md`)](FOUNDATION.md) — Deep architectural guide to foundation types, handles, and components.
* [Engine Subsystems Roadmap (`ROADMAP.md`)](ROADMAP.md) — Milestone tracking and future production systems.
* [Build Variants & Matrix (`BUILD_VARIANTS.md`)](BUILD_VARIANTS.md) — Supported configurations, targets, and compiler profiles.
* [Contributing Guidelines (`CONTRIBUTING.md`)](CONTRIBUTING.md) — Coding conventions, PR guidelines, and style requirements.

---

## License

Copyright &copy; 2026 UniVex Studios. All Rights Reserved.
