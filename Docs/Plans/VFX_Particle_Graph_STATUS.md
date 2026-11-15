# Particle Graph (VFX) — Implementation Status

- **Branch:** `feature/vfx-particle-graph`
- **Plan:** `Docs/Plans/VFX_Particle_Graph.md`

---

## Step V2a — Asset + CPU simulator core
- **Status:** Complete
- **Commit:** `a51a9577` (`VFX core: add particle system asset and CPU simulator`, parent `89458dd9`)
- **Builds verified (`Linux Release`):**
  - `UniVexEditor` (`.uvbt/bin/Linux/Release/uve_editor`)
  - `UniVexTests` (`.uvbt/bin/Linux/Release/UniVexTests`)
  - `UniVexPrismTests` (`.uvbt/bin/Linux/Release/uve_prism_tests`)
- **Guards:**
  - `bash Engine/Tools/check_panel_includes.sh` — clean
  - `timeout 300 python3 Engine/Tools/check_engine_vocabulary.py` — passed (0 reintroductions)
  - `python3 Engine/Tools/check_math_boundary.py` — passed
- **Tests:**
  - Full `UniVexTests` (`Xvfb` / SwiftShader): **5422 tests** from 574 suites — **5411 passed**, **6 skipped**, **5 failed** (only the 5 pre-existing baseline failures):
    - `EngineCoreUVETest.LoopStages_ExecuteInDocumentedOrder`
    - `EngineCoreUVETest.Renderer3D_ReachableAfterInit_NoActiveCameraStillNoOps`
    - `EntityEnabledUVETest.StaleHandle_AssertsInDebugAndFailsClosedInRelease`
    - `Camera2DSystemUVETest.SetActiveCamera_OutOfRangeIndex_Asserts` (and sibling `*_Asserts`)
  - Targeted V2a filter (`*ParticleSystemAsset*:*Vfx*:*AssetCooker*`): **36 passed, 0 failed**:
    - `ParticleSystemAssetUVETest` (3/3): round-trip of all 46 module types with non-default inputs, invalid-case rejection, schema/kind rejection
    - `VfxValuesUVETest` (2/2): baked curve table parity within `1e-3`, linear-space gradient interpolation
    - `VfxShapesUVETest` (7/7): volume and surface sampling for Point, Sphere, Box, Cone, Cylinder, Torus
    - `VfxProgramUVETest` (3/3): all required compile errors & warnings, minimal SoA attribute allocation
    - `VfxInstanceUVETest` (11/11): fractional SpawnRate carry, SpawnBurst per cycle, SpawnPerDistance path interpolation, maxParticles clamp, deterministic seed parity & distinctness, KillByAge, World vs Local space, non-looping completion, warm-up, 10s simulation across all 5 built-in templates (`Empty`, `Fountain`, `Sparks`, `Fire`, `Smoke`)
    - `AssetCookerUVETest` (10/10): `.uvfx` cooking and referenced texture/mesh/material GUID dependency checks
  - `uve_prism_tests Engine/Runtime/Prism/assets`: **13419 checks, 0 failed**
- **10-second Fountain live count log (`VfxInstanceUVETest.EachTemplateCompilesAndRunsTenSeconds`):**
  - `t=1s: 119`, `t=2s: 200`, `t=3s: 201`, `t=4s: 202`, `t=5s: 191`, `t=6s: 196`, `t=7s: 197`, `t=8s: 197`, `t=9s: 195`, `t=10s: 201`
- **Screenshots (xvfb):** None (no renderer in V2a per plan).
- **Incomplete items:** None.

---

## Step V3a — Sprite renderer
- **Status:** Complete
- **Commit:** Pending V3a commit SHA (parent `a51a9577`; title: `VFX sprite renderer: instanced billboards, sorting, soft particles, and VfxWorld integration`; SHA recorded on next status update)
- **Builds verified (`Linux Release`):**
  - `UniVexEditor` (`.uvbt/bin/Linux/Release/uve_editor`)
  - `UniVexTests` (`.uvbt/bin/Linux/Release/UniVexTests`)
  - `UniVexPrismTests` (`.uvbt/bin/Linux/Release/uve_prism_tests`)
- **Guards:**
  - `bash Engine/Tools/check_panel_includes.sh` — clean
  - `timeout 300 python3 Engine/Tools/check_engine_vocabulary.py` — passed (0 reintroductions)
  - `python3 Engine/Tools/check_math_boundary.py` — passed
- **Tests:**
  - Full `UniVexTests` (`Xvfb` `:99`): **5443 tests** from 576 suites — **5350 passed**, **88 skipped**, **5 failed** (only the 5 pre-existing baseline failures):
    - `EngineCoreUVETest.LoopStages_ExecuteInDocumentedOrder`
    - `EngineCoreUVETest.Renderer3D_ReachableAfterInit_NoActiveCameraStillNoOps`
    - `EntityEnabledUVETest.StaleHandle_AssertsInDebugAndFailsClosedInRelease`
    - `Camera2DSystemUVETest.ComputeProjectionMatrixUVE_InvalidViewportSize_Asserts`
    - `Camera2DSystemUVETest.ComputeProjectionMatrixUVE_InvalidCameraParameters_Asserts`
  - Targeted V3a tests: **52 passed, 0 failed**:
    - `VfxWorldUVETest` (6/6): spawn/stop/kill handle lifecycle, runtime parameter overrides, global budget proportional spawn scaling, quality level scalability row selection, distance LOD and frustum quarter-rate update, hot reload recompilation & restart
    - `VfxSpriteRendererUVETest` (9/9): sprite corner expansion for all 4 facing modes (camera plane, spherical, velocity-aligned stretch, fixed axis), 32-bit radix sort back-to-front ordering, soft-particle depth fade, flipbook UV frame calculation and blend factor, 1,000,000 instance budget cap
    - `GlRenderDeviceUVETest.DrawInstancedAndDrawIndexedInstanced_RenderEachInstanceViaGlInstanceId` (1/1): verifies `glDrawArraysInstanced` and `glDrawElementsInstanced` with `gl_InstanceID` and vertex-pulling `vertexStride == 0`
    - `AllBuiltInShaders/BuiltInShaderParityUVETest.*` (26/26): includes `vfx_sprite.glsl` byte-identical parity check
    - `SceneSerializerUVETest.*Particle*` & `ParticleEmitterComponentUVE.*` (8/8): `systemAssetPath`, `autoPlay`, `seed`, and parameter override round-trip
    - `EngineCoreUVETest.VfxRuntime_ParticleEmitterWithSystemAssetPathSpawnsInVfxWorldNotLegacyRuntime` (1/1)
    - `EngineCoreUVETest.WindowedMode_VfxSpritesRenderAdditiveAlphaSoftAndTemplateLevelWithScreenshots` (1/1)
  - `uve_prism_tests Engine/Runtime/Prism/assets`: **13419 checks, 0 failed**
- **Screenshots (xvfb, cropped real-GL PNGs under `Docs/Plans/vfx_shots/`):**
  - `Docs/Plans/vfx_shots/v3a_additive_sparks.png` — additive HDR sparks with bloom
  - `Docs/Plans/vfx_shots/v3a_alpha_smoke.png` — back-to-front radix-sorted alpha smoke puffs
  - `Docs/Plans/vfx_shots/v3a_soft_particles.png` — soft particles intersecting the floor plane with smooth depth fade
  - `Docs/Plans/vfx_shots/v3a_fountain_sparks_level.png` — level playing `.uvfx` Fountain and Sparks templates side by side
- **Incomplete items:** None.

