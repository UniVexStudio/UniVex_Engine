# UniVex Engine

Production-grade real-time 3D engine and PRISM editor targeting Desktop (Linux, Windows) and Mobile (Android), built in C++23 with **UVBT** (`Engine/Tools/UVBT/uvbt.py`).

## Key Capabilities

- **Rendering & Graphics**: Forward+ clustered lighting (up to 256 dynamic lights), Cook-Torrance PBR, 3-cascade directional + spot/point shadow atlas + contact shadows, Temporal Anti-Aliasing (TAA), WBOIT transparency, SSR, Volumetric Fog, HDR Reflection Probes with 2-probe blending & L2 SH, Hybrid GI (UV2 lightmaps + L2 SH probe grids), Compute Ray-Tracing (`rt_trace.glsl`), spatial BVH + Hi-Z occlusion culling, GPU instancing, terrain, water, and foliage wind.
- **Explicit RHI & Cross-Compilation**: OpenGL 4.3+/GLES3, Vulkan 1.3 dynamic rendering (SSBOs, compute, bindless texture table), Null headless backend, and single-source shader validation/cross-compilation (`Engine/Tools/compile_shaders.py`).
- **Physics & Simulation**: Rigid bodies, CCD, joints, ragdolls, XPBD 2D cloth (`SoftBody3DComponentUVE`), 6-DOF raycast wheeled vehicles with Ackermann steering, and BSP destructibles.
- **World Scale & Networking**: World Partition, Level Streamer, double-precision floating-origin rebasing, async cell streaming, replication, RPCs, interest management, packet capture, and Voice Chat (4:1 IMA-ADPCM, VAD/PTT, jitter buffer).
- **Asset Pipeline & Editor**: glTF/FBX/OBJ/PNG/JPEG/SVG importers, Basis texture compression, Visual Material Graph (`.uvmatgraph`), VFX Editor, Animation Sequencer/State Machine/Blend Spaces, Widget Designer, Asset Dependency Index, Multi-Scene editing, Prefab Override Diff/Apply/Revert, Terrain Brush, Git VCS badges, Find-in-Project Search, Extension Registry, and Project Validator.
- **UI, Accessibility & Live Ops**: Screen/world-space UI, Rich Text, UTF-8/BiDi RTL layout, Unicode font fallback, LMS Daltonization colorblind compensation, UI scaling, screen-reader hooks, Crash Reporter + Telemetry, Priority Hotfix Patch Manager, Storefront SDK abstraction, and Windows/Linux store packaging manifests.

## Quick Start

```bash
# Build and run automated tests (Linux)
./uvbt build UniVexTests Linux Debug
xvfb-run -a ./.uvbt/bin/Linux/Debug/UniVexTests

# Build the Editor
./uvbt build UniVexEditor Linux Release
```

UniVex Hub (the desktop launcher, Linux/Windows installers, and Android Hub APK host) is developed in its own standalone repository at [UniVexStudio/UniVex_Hub](https://github.com/UniVexStudio/UniVex_Hub) and integrates with engine `.uvpackage` releases through the frozen contracts in [contracts/v1/](contracts/v1/README.md).

## License

UniVex Engine and UniVex Hub are source-available under the [UniVex Engine & Hub EULA v1.0](LICENSE). You can use the engine and ship personal, indie, or commercial games and applications free of charge with no royalties; redistributing or rebranding the engine, editor, Hub, or source code itself is not permitted.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party library and font attributions, and [CONTRIBUTING.md](CONTRIBUTING.md#5-contributor-license-agreement-cla) for the Contributor License Agreement.
