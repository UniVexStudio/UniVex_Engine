# UniVex Engine — System-by-System Architecture Guide

This guide summarizes the runtime and editor subsystems so new contributors can navigate directly to the right module.

## 1. Core & Foundation (`Engine/Runtime/Core/`)
- **Math (`Core/Math`)**: Left/right-handed matrix, quaternion (`TryMakeAxisAngleUVE`, `TryNormalizeUVE`), AABB, frustum, and color-space conversion utilities.
- **Diagnostics (`Core/Diagnostics`)**: `ProfilerCaptureUVE`, counters, spans, and consent-gated `TelemetryServiceUVE` (`telemetry_service.h`).
- **Platform (`Platform`)**: OS runtime, `CrashReporterUVE`, `IPlatformStorefrontUVE` (achievements/stats/entitlements/cloud-save), and `IXrRuntimeUVE` (stereo views & tracked controllers).

## 2. RHI & Rendering (`Engine/Runtime/RHI/`)
- **Backends**: `GlRenderDeviceUVE` (OpenGL 4.3+ / GLES3), `VulkanRenderDeviceUVE` (Vulkan 1.3 dynamic rendering, SSBOs, compute, bindless descriptor indexing), and `NullRenderDeviceUVE`.
- **Single-Source Shaders (`RHI/Shader`)**: Built-in GLSL shaders synced byte-for-byte in `built_in_shaders.cpp`, validated/cross-compiled via `Engine/Tools/compile_shaders.py` and `TranspileShaderSourceForDialectUVE`.
- **RenderSystems (`RHI/RenderSystems`)**:
  - Forward+ clustered lighting (`LightClusterGridUVE`, up to 256 dynamic lights).
  - Temporal Anti-Aliasing (`taa_math.h` + `taa_resolve.glsl`) & WBOIT (`wboit_composite.glsl`).
  - HDR Reflection Probes with 2-probe blending & L2 SH (`gi_reflection_probes.h`), Hybrid GI (`BakeSceneLightmapUVE` + `GiProbeGridUVE`), and Compute Ray-Tracing (`ray_traced_effects.h` + `rt_trace.glsl`).
  - Spatial BVH culling (`DynamicMeshBvhUVE`), Hi-Z depth-pyramid occlusion (`HiZDepthPyramidUVE` + `hiz_downsample.glsl`), GPU instancing, terrain, water, and foliage wind.

## 3. World, Scene & Physics (`Engine/Runtime/World`, `Scene`, `Physics`, `Objects/3D`)
- **World Streaming & Large Worlds**: `WorldPartition3D`, `LevelStreamer3D`, double-precision floating-origin rebasing (`OriginRebaseStateUVE`), and `AsyncWorldStreamQueueUVE`.
- **Physics**: Rigid bodies, CCD, constraints, ragdolls, XPBD 2D cloth (`SoftBody3DComponentUVE`), 6-DOF raycast vehicles with Ackermann steering (`VehicleBody3DComponentUVE`), and BSP destructibles (`Destructible3DComponentUVE`).

## 4. Networking, Audio & Asset Pipeline (`Networking`, `Audio`, `Asset`)
- **Networking**: Replication, RPCs, interest management, packet capture, and `VoiceChatSessionUVE` (4:1 IMA-ADPCM, VAD/PTT, jitter buffer).
- **Asset Pipeline**: `.uve*` envelope codecs, glTF/FBX/OBJ/PNG/JPEG/SVG (`RasterizeSvgToTextureAssetUVE`), Basis texture compression, visual `.uvmatgraph` PBR compiler (`material_graph_compiler.cpp`), and VCS diff/merge (`Engine/Tools/uve_asset_diff.py`).

## 5. UI, Editor & Live Operations (`UI`, `Prism`, `EditorCore`, `Pack`)
- **UI Runtime**: Screen-space & world-space canvases, layout/anchors, tweens, Rich Text (`ParseRichTextSpansUVE`), UTF-8/BiDi RTL layout + font fallback (`ui_rich_text.h`), Daltonization colorblind filter, UI scaling, and `IAccessibilityBridgeUVE`.
- **Editor (`EditorCore`)**: PRISM-based shell with Content Browser, Asset Dependency Index, Multi-Scene editing, Prefab Override Diff/Apply/Revert, Visual Material Graph, VFX Editor, Terrain Brush, Git VCS badges, Output Log click-to-navigate, Find-in-Project Search, Extension Registry, and Project Validator.
- **Packaging & Live Ops (`Pack`)**: Asset cooker, `ProjectPackagerUVE`, `PatchManagerUVE` (priority hotfix mounting + rollback), and `store_packaging.h` (InnoSetup `.iss`, AppImage `.desktop`, code-signing).
