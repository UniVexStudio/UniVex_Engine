# Engine/Runtime/RHI

RHI is a container module for all rendering hardware interface layers.

This folder does NOT contain code itself — its submodules do:

- `RHI/` — The backend-agnostic interface: `IRenderDeviceUVE`, `ICommandBufferUVE`, buffer/texture/pipeline/shader handles, `RenderResourceDescs`. This is the contract every backend implements.
- `OpenGL/` — Real desktop backend (`GlRenderDeviceUVE`, `GlCommandBufferUVE`). Handles GLFW proc loading, Android EGL fallback, uniform reflection, sampler handling.
- `Null/` — Headless/testing backend (`NullRenderDeviceUVE`). Bookkeeping no-op, always usable, used for CI and tests.
- `Shader/` — Shader manager (`ShaderManagerUVE`, `BuiltInShadersUVE`). Async preprocessing, compile/link on main thread, on-disk binary cache via `GetPipelineBinaryUVE` / `CreatePipelineFromBinaryUVE`, hot-reload.
- `RenderSystems/` — High-level systems that sit ON TOP of RHI: `Renderer3DUVE` (offscreen targets, GPU cache, shadow cascades, fallback textures), `CameraSystemUVE`, `LightSystemUVE`, `MeshRendererUVE`, `RenderGraphUVE`, `RenderQueue`.

**Design decision:** The old empty `Engine/Runtime/Renderer` duplicate folder was removed (2026-09-16) — its functionality lives in `RenderSystems`. The old `Networking` duplicate was also removed, keeping `Network`.

**Future work (per ROADMAP):**
- Second real backend (Vulkan/D3D12/Metal) to prove RHI abstraction
- Shader cross-compilation
- GPU compute support
- Bindless/descriptor indexing

See `UNIVEX_Engine_Scratch_Rebuild_Architecture_Prompt` and `ROADMAP.md` section 1 & 2 for full plan.
