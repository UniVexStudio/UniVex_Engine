# Particle Graph (VFX): the implementation plan

This plan is for the engineer, human or agent, who builds the UniVex VFX system on the branch
`feature/vfx-particle-graph`. It stands on its own. Read all of it before writing code, then do
the steps **in order**. Every step ends with something you can build, test and see.

The goal is a production-grade particle system, not a demo. It has:

- an asset that holds several emitters;
- a node graph per emitter that compiles to a fast module program;
- a CPU simulator (and later a GPU one) over structure-of-arrays data;
- real renderers: sprite, ribbon, mesh and light;
- an editor window with a live preview;
- hooks into Action Clips, Logic Graphs and levels.

---

## 0. House rules (read first: breaking one means the work is redone)

### 0.1 Naming and layout
- Every engine type, function and constant ends in `UVE`: `ParticleSystemAssetUVE`,
  `SpawnBurstModuleUVE`, `kMaximumEmittersUVE`. Look at any file in `Engine/Runtime/Asset/` and
  copy its style exactly: comments, `[[nodiscard]]`, `noexcept`, `final`, braces, and
  `static_cast` instead of C casts.
- PRISM (the UI library in `Engine/Runtime/Prism`) is the exception: it uses plain camelCase in
  `namespace prism`. Do not add `UVE` there.
- Modules live in `Engine/Runtime/<Module>/`. Public headers go in `Expose/` and sources in
  `Internal/`. Each module has a `<Module>.Build.cs`. See `Engine/Runtime/Navigation/` for a small
  complete example.
- Tests live in `Test/<Area>/` and are compiled into the `UniVexTests` binary through
  `Test/CoreTests.Build.cs`. **`Test/Editor/` is excluded from the build.** Do not put tests there:
  they will never run.
- **No file name may contain the word "prism".** Editor files are named `editor_*.cpp/.h`.
- **Never name another engine, studio or product** (in code, comments, UI text, data or commit
  messages). Describe the feature itself.
- All UI text is **English**.
- No ImGui anywhere. The editor UI is PRISM only.

### 0.2 Building and testing
```
./uvbt build UniVexEditor Linux Release -j8
./uvbt build UniVexTests Linux Release -j8
./uvbt build UniVexPrismTests Linux Release -j8
```
- Binaries go to `.uvbt/bin/Linux/Release/`.
- Run the tests from the **repository root** with a display: `DISPLAY=:99`, with `Xvfb :99` running.
  Without a display, about 160 GL and window tests are skipped.
  - `DISPLAY=:99 ./.uvbt/bin/Linux/Release/UniVexTests`. Exactly **5 tests fail before you start**,
    and they must stay the only failures:
    - `EngineCoreUVETest.LoopStages_ExecuteInDocumentedOrder`
    - `EngineCoreUVETest.Renderer3D_ReachableAfterInit_NoActiveCameraStillNoOps`
    - `EntityEnabledUVETest.StaleHandle_AssertsInDebugAndFailsClosedInRelease`
    - the two `Camera2DSystemUVETest.*_Asserts`
  - `./.uvbt/bin/Linux/Release/uve_prism_tests Engine/Runtime/Prism/assets` must report 0 failed.
    Always pass that path, because the golden images are compared against the repository's.
- **Do not edit a header while a build is running.** The build tool tracks header dependencies by
  timestamp. An object compiled from the old header gets a newer time and is never rebuilt, and
  the editor then crashes in unrelated places. If it happens, `touch` every source whose `.d` file
  under `.uvbt/obj/Linux/Release/` lists that header, then rebuild.
- The built-in shaders exist twice and must stay **byte-identical**:
  - the C++ string in `Engine/Runtime/RHI/Shader/Internal/built_in_shaders.cpp`;
  - the file in `Engine/Runtime/RHI/Shader/built_in/*.glsl`.

  `BuiltInShaderParityUVETest` checks this. Edit the C++ string, then regenerate the `.glsl` from it.

### 0.3 Before every commit
Run the three guards. All three must pass:
```
bash Engine/Tools/check_panel_includes.sh
timeout 300 python3 Engine/Tools/check_engine_vocabulary.py
python3 Engine/Tools/check_math_boundary.py
```
- `check_panel_includes.sh` fails when a `std::` type is used without its `<header>`.
- The math guard forbids `univex/math` (the viewport's own kit) outside `Engine/Editor/Viewport`
  and `Engine/App`. Engine code uses `UVE::Math` (`vector3.h`, `quaternion.h`, `color.h`,
  `math_random.h`, `aabb.h`).

### 0.4 Commits
- Author every commit as:
  `git -c user.name="UniVex Engine" -c user.email="145521884+UniVexStudio@users.noreply.github.com" commit`
- **No `Co-Authored-By`, no session links, no model or tool names** in a commit message.
- Use one commit per step below. The message is a short title, then bullets describing the
  behaviour (what the user can now do), not a list of files.
- Push to `feature/vfx-particle-graph` only. Never force-push. Never open or merge a PR yourself.

### 0.5 Things you must not do
- Do not commit a stub, a placeholder, `TODO: implement`, or a function that returns a fake value
  and call the step done. If a step cannot be finished, stop and write down why in
  `Docs/Plans/VFX_Particle_Graph_STATUS.md`.
- Do not skip, disable, delete or weaken a test to get green.
- Do not change code outside the areas a step names without writing down why in that step's commit.
- Do not reformat files you only read.
- Do not invent APIs. Every existing function named below was checked in the code. Read it before
  you call it.

### 0.6 Seeing your work (xvfb)
```
Xvfb :99 -screen 0 1920x1080x24 &
cd <a test project folder with an assets/ dir> && DISPLAY=:99 <repo>/.uvbt/bin/Linux/Release/uve_editor --decorated --frames 100000 &
DISPLAY=:99 import -window root shot.png      # ImageMagick screenshot
DISPLAY=:99 xdotool mousemove X Y; xdotool mousedown 1; sleep 0.1; xdotool mouseup 1
```
- Click with mousedown, a short pause, then mouseup. Instant clicks are sometimes missed.
- Every step has an "xvfb check". Take the screenshots it names and keep them in
  `Docs/Plans/vfx_shots/` (do not commit large files: crop to the area that matters).

---

## 1. What exists today (start from here, do not duplicate it)

| Piece | Where | What it does now |
|---|---|---|
| `ParticleEmitterComponentUVE` | `Engine/Runtime/Component/Expose/particle_emitter_component.h` | `maxParticles`, `emitting`, `emissionRate`, `lifetimeSeconds` only |
| `ParticleEmitter3D` object | `Engine/Runtime/Objects/3D/Expose/particle_emitter_3d.h` | the scene object that carries the component |
| `ParticleRuntimeUVE` | `Engine/Runtime/Scene/Expose/particle_runtime.h`, `i_particle_runtime.h` | position, velocity and lifetime with one global acceleration, on the CPU |
| `ParticleComputeSimulationUVE` | `Engine/Runtime/RHI/RenderSystems/Expose/particle_compute_simulation.h` | the same integration as a compute kernel, with readback |
| Particle drawing | `particle_draw_command.h`, `particle_render_bridge.h`, `renderer_3d.cpp` | a fixed 0.05 world-unit quad per particle, not camera-facing, flat colour, at most 16,384 |
| Light budget | `kMaxLightsUVE = 4` in the renderer | four lights per draw |
| Graph UI | `Engine/Runtime/Prism/Internal/BlueprintGraphs.cpp:147` `loadParticleGraphPreset()` | a demo graph style (colours and icons) for particle graphs, with no data behind it |
| Action Clip notify | `Engine/Runtime/Asset/Expose/animation_montage_asset.h` (`MontageNotifyUVE`, `MontageNotifyStateUVE`) | raised in `engine_core.cpp` (~2978) as the `montage_notify*` script events |
| Action Clip editor | `Engine/Editor/EditorCore/Internal/shell/montage/editor_montage_editor.*` | a separate native PRISM window with a preview and a timeline. **This is the model for the VFX editor window.** |
| Logic Graph nodes | `Engine/Runtime/Asset/Expose/blueprint_nodes.h` and its `.cpp` | the node catalogue, translated to UVScript |
| Asset envelope | `Engine/Runtime/Asset/Expose/uve_file_envelope.h` | `AssetKindUVE` enum (last value `Blueprint = 18`) and `WriteUveFileUVE`/`ReadUveFileUVE` |
| JSON asset example | `Engine/Runtime/Asset/Internal/montage_library_asset.cpp` | a complete small asset: schema string, validation, save and load. **Copy this pattern.** |
| Deterministic random | `Engine/Runtime/Core/Math/Expose/math_random.h` `Math::RandomUVE` | PCG32 with `NextFloatUVE`, `RangeUVE` and `ChanceUVE`; seed plus stream |
| Asset Library | `Engine/Editor/EditorCore/Internal/shell/editor_content_browser.cpp` | the Add menu (`items.push_back(item("Action Clip", "animation-montage", ...))` ~1468), tile badges (~239) and double-click to open |

---

## 2. The design

### 2.1 Words
- **Particle System** (`.uvfx`): what a level, an Action Clip or a Logic Graph plays. It has one
  or more emitters and a list of user parameters.
- **Emitter**: one stream of particles. It has a graph with four stage outputs.
- **Stages**, in the order they run:
  1. **Emitter Update**: per emitter, each frame. Decides how many particles to spawn.
  2. **Particle Spawn**: per new particle, once. Sets its initial values.
  3. **Particle Update**: per live particle, each frame. Forces, over-life changes, collision, kill.
  4. **Render**: how the particles are drawn (one or more renderers).
- **Module**: one node that does one job (Spawn Rate, Shape: Sphere, Gravity, Colour over Life...).
  Modules are chained into a stage in graph order.
- **Value input**: any number, vector or colour a module reads. It is one of:
  - a Constant;
  - a Random Range;
  - a Curve (over normalized age 0..1);
  - a Gradient (colours over age);
  - a User Parameter;
  - a wire from a Math/Value node.
- **Program**: the compiled form of an emitter graph. It is a flat, ordered list of module ops
  per stage, with each input resolved. The simulator runs programs, never graphs.

### 2.2 Data flow
```
.uvfx (JSON)  ──load──►  ParticleSystemAssetUVE
                                │  CompileParticleEmitterUVE (per emitter)
                                ▼
                         ParticleProgramUVE  (ops + constant pool + curves + gradients)
                                │
            ┌───────────────────┴────────────────────┐
            ▼                                        ▼
  VfxInstanceUVE (CPU, SoA)               GPU path (step V2c)
            │  draw lists per renderer
            ▼
  Renderer3D particle passes (sprite / ribbon / mesh / light)
```

---

## 3. Module and file layout

| File | Module | Purpose |
|---|---|---|
| `Engine/Runtime/Asset/Expose/particle_system_asset.h` + `Internal/particle_system_asset.cpp` | Asset | the asset types, validation, save and load |
| `Engine/Runtime/VFX/VFX.Build.cs` | **new module `VFX`** | dependencies: `Platform`, `Logging`, `Math`, `Asset` |
| `Engine/Runtime/VFX/Expose/vfx_program.h` + `Internal/vfx_program.cpp` | VFX | graph → program compiler and its errors |
| `Engine/Runtime/VFX/Expose/vfx_values.h` + `Internal/vfx_values.cpp` | VFX | curve and gradient sampling, random ranges |
| `Engine/Runtime/VFX/Expose/vfx_instance.h` + `Internal/vfx_instance.cpp` | VFX | the SoA simulator for one playing system |
| `Engine/Runtime/VFX/Expose/vfx_shapes.h` + `Internal/vfx_shapes.cpp` | VFX | spawn-shape sampling |
| `Engine/Runtime/VFX/Expose/vfx_world.h` + `Internal/vfx_world.cpp` | VFX | all instances, pooling, budget and scalability |
| `Engine/Runtime/VFX/Expose/vfx_templates.h` + `Internal/vfx_templates.cpp` | VFX | the built-in templates (Fountain, Sparks...) as code |
| `Test/VFX/*.cpp` | tests | add `"VFX"` to `PublicDependencyModuleNames` in `Test/CoreTests.Build.cs` |
| `Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_editor.{h,cpp}` | Editor | the editor window (step V4a) |

**The new asset kind:**
- In `uve_file_envelope.h`, add `ParticleSystem = 19` after `Blueprint = 18`, with a `///` comment.
- Raise the upper bound in the validity check in `Engine/Runtime/Asset/Internal/uve_file_envelope.cpp`
  (~line 26: `assetType <= static_cast<std::uint32_t>(AssetKindUVE::Blueprint)`) to `ParticleSystem`.
- In `Engine/Runtime/Pack/Internal/asset_cooker.cpp` (~line 87):
  - add `{".uvfx", Asset::AssetKindUVE::ParticleSystem}` to the extension map;
  - add a `case` that loads it and calls `CheckGuid` on every texture, mesh and material GUID it references.
- The extension is `.uvfx`. Files are created with the prefix `VFX_`.

---

## 4. The data model (exact)

Put this in `particle_system_asset.h` (namespace `UVE::Asset`). Field names, types and defaults
are part of the plan.

```cpp
inline constexpr std::size_t kMaximumParticleEmittersUVE = 32U;
inline constexpr std::size_t kMaximumParticleModulesPerStageUVE = 64U;
inline constexpr std::size_t kMaximumParticleUserParametersUVE = 64U;
inline constexpr std::uint32_t kMaximumParticlesPerEmitterUVE = 1'000'000U;
inline constexpr std::size_t kMaximumCurveKeysUVE = 64U;
inline constexpr std::size_t kMaximumGradientStopsUVE = 16U;
inline constexpr std::size_t kMaximumParticleSystemPayloadBytesUVE = 8U * 1024U * 1024U;

enum class ParticleValueKindUVE : std::uint8_t { Constant, RandomRange, Curve, Gradient, UserParameter, Wire };

struct ParticleCurveKeyUVE final { float time = 0.0F; float value = 0.0F; float inTangent = 0.0F; float outTangent = 0.0F; bool operator==(...) const = default; };
struct ParticleCurveUVE final { std::vector<ParticleCurveKeyUVE> keys; bool smooth = true; /* Hermite with tangents; false = linear */ };
struct ParticleGradientStopUVE final { float time = 0.0F; Math::ColorUVE color{1,1,1}; float alpha = 1.0F; };
struct ParticleGradientUVE final { std::vector<ParticleGradientStopUVE> stops; };

/// A float input. `curve` is sampled at the particle's normalized age (or the emitter's, in Emitter
/// Update), then multiplied by `scale`.
struct ParticleFloatInputUVE final {
    ParticleValueKindUVE kind = ParticleValueKindUVE::Constant;
    float constant = 0.0F;
    float min = 0.0F, max = 0.0F;          // RandomRange: uniform per particle (per spawn)
    ParticleCurveUVE curve; float scale = 1.0F;
    std::string parameter;                 // UserParameter name
    std::uint32_t wireNode = 0U;           // Wire: the Math/Value node id feeding it
};
struct ParticleVectorInputUVE final { ParticleValueKindUVE kind; Math::Vector3UVE constant; Math::Vector3UVE min, max; std::string parameter; std::uint32_t wireNode = 0U; };
struct ParticleColorInputUVE final { ParticleValueKindUVE kind; Math::ColorUVE constant{1,1,1}; float alpha = 1.0F;
                                     Math::ColorUVE minColor{1,1,1}, maxColor{1,1,1}; ParticleGradientUVE gradient; std::string parameter; };

enum class ParticleModuleTypeUVE : std::uint16_t {
    // Emitter Update
    SpawnRate, SpawnBurst, SpawnPerDistance,
    // Particle Spawn
    ShapePoint, ShapeSphere, ShapeBox, ShapeCone, ShapeCylinder, ShapeTorus, ShapeMeshSurface,
    InitialVelocity, VelocityInCone, InitialLifetime, InitialColor, InitialSize, InitialRotation,
    InitialSpin, InitialSpriteFrame, InitialMass, InheritOwnerVelocity,
    // Particle Update
    Gravity, Drag, AddVelocity, CurlNoise, Vortex, PointAttractor, Wind, LimitSpeed, Orbit,
    ColorOverLife, SizeOverLife, RotationRate, SizeBySpeed, SpriteFlipbook,
    CollisionPlane, CollisionDepth, CollisionPhysics, KillByAge, KillByBounds, KillByCondition,
    EventOnDeath, EventOnCollision,
    // Render
    SpriteRenderer, RibbonRenderer, MeshRenderer, LightRenderer,
};

/// One module: its type, whether it runs, and its inputs by name (each module documents its names,
/// see §6). Unknown names are a load error; a missing name takes the module's default.
struct ParticleModuleUVE final {
    std::uint32_t id = 0U;                          // stable within the emitter (graph node id)
    ParticleModuleTypeUVE type{};
    bool enabled = true;
    std::map<std::string, ParticleFloatInputUVE> floats;
    std::map<std::string, ParticleVectorInputUVE> vectors;
    std::map<std::string, ParticleColorInputUVE> colors;
    std::map<std::string, std::int32_t> ints;      // enums and counts (a shape's surface flag, a renderer's blend mode...)
    std::map<std::string, std::string> strings;    // asset paths (texture, mesh, material), sub-emitter names
    float graphX = 0.0F, graphY = 0.0F;            // where the node sits in the editor graph
};

enum class ParticleSimSpaceUVE : std::uint8_t { World, Local };
enum class ParticleSimTargetUVE : std::uint8_t { Cpu, Gpu, Auto };

struct ParticleMathNodeUVE final { std::uint32_t id = 0U; std::string op; std::vector<std::uint32_t> inputs; float constant = 0.0F; float graphX = 0.0F, graphY = 0.0F; };

struct ParticleEmitterAssetUVE final {
    std::string name = "Emitter";
    bool enabled = true;
    ParticleSimSpaceUVE space = ParticleSimSpaceUVE::World;
    ParticleSimTargetUVE target = ParticleSimTargetUVE::Cpu;
    std::uint32_t maxParticles = 1000U;
    std::uint32_t seed = 0U;                        // 0: from the instance seed
    std::vector<ParticleModuleUVE> emitterUpdate, particleSpawn, particleUpdate, render;
    std::vector<ParticleMathNodeUVE> mathNodes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> comments; // optional editor comments (rect ids)
};

enum class ParticleUserParameterTypeUVE : std::uint8_t { Float, Vector, Color, Bool, Int };
struct ParticleUserParameterUVE final { std::string name; ParticleUserParameterTypeUVE type{}; Math::Vector3UVE value{}; float alpha = 1.0F; };

struct ParticleScalabilityUVE final { float spawnScale = 1.0F; std::uint32_t maxParticlesCap = 0U; float cullDistance = 0.0F; };

struct ParticleSystemAssetUVE final {
    std::vector<ParticleEmitterAssetUVE> emitters;
    std::vector<ParticleUserParameterUVE> parameters;
    float duration = 2.0F;                          // seconds; one loop
    bool looping = true;
    float warmUpSeconds = 0.0F;
    Math::AabbUVE fixedBounds{{-2,-2,-2},{2,2,2}};  // for culling; local space
    std::array<ParticleScalabilityUVE, 3> scalability{}; // Low, Medium, High
};

[[nodiscard]] bool IsParticleSystemAssetValidUVE(const ParticleSystemAssetUVE& system) noexcept;
[[nodiscard]] bool SaveParticleSystemAssetUVE(const ParticleSystemAssetUVE& system, const std::filesystem::path& path);
[[nodiscard]] bool LoadParticleSystemAssetUVE(const std::filesystem::path& path, ParticleSystemAssetUVE& outSystem);
```

**Validation** (`IsParticleSystemAssetValidUVE`), every rule tested:
- 1..32 emitters, with unique non-empty names (≤ 64 bytes, no NUL).
- `maxParticles` is in 1..1,000,000.
- At most 64 modules per stage.
- Every module type belongs to the stage it sits in.
- Every float is finite. For a RandomRange, `min <= max`. Curve keys are sorted by time and lie
  in [0,1]. Gradient stops are sorted and lie in [0,1].
- User parameters have unique names, and every `UserParameter` input names an existing one of a
  matching type.
- `duration > 0`, `warmUpSeconds >= 0`, and `fixedBounds.min <= max` on every axis.
- `ints`, `strings` and the input names are known to the module's type (§6). The table of allowed
  names lives in one place, `vfx_module_catalog`, shared by the validator, the compiler and the editor.

**JSON** (schema `"uve-particle-system-v1"`), saved through `WriteUveFileUVE(path, AssetKindUVE::ParticleSystem, bytes)`.
The writer omits inputs at their module default, so a simple system stays small. Example:
```json
{
  "schema": "uve-particle-system-v1",
  "duration": 2.0, "looping": true, "warmUp": 0.0,
  "bounds": {"min": [-2,-2,-2], "max": [2,4,2]},
  "parameters": [{"name": "Tint", "type": "color", "value": [1,0.6,0.2], "alpha": 1}],
  "scalability": [{"spawnScale": 0.25, "maxParticles": 500}, {"spawnScale": 0.5}, {}],
  "emitters": [{
    "name": "Sparks", "space": "world", "target": "cpu", "maxParticles": 2000, "seed": 0,
    "emitterUpdate": [{"id": 1, "type": "SpawnRate", "floats": {"Rate": {"constant": 200}}, "at": [0, 0]}],
    "particleSpawn": [
      {"id": 2, "type": "ShapeSphere", "floats": {"Radius": {"constant": 0.1}}, "ints": {"Surface": 1}},
      {"id": 3, "type": "VelocityInCone", "floats": {"Angle": {"constant": 25}, "Speed": {"random": [3, 6]}}},
      {"id": 4, "type": "InitialLifetime", "floats": {"Lifetime": {"random": [0.4, 0.9]}}},
      {"id": 5, "type": "InitialColor", "colors": {"Color": {"parameter": "Tint"}}}
    ],
    "particleUpdate": [
      {"id": 6, "type": "Gravity", "vectors": {"Acceleration": {"constant": [0, -9.8, 0]}}},
      {"id": 7, "type": "ColorOverLife", "colors": {"Color": {"gradient": [[0, 1,1,1,1], [1, 1,0.3,0,0]]}}},
      {"id": 8, "type": "SizeOverLife", "floats": {"Scale": {"curve": [[0,1],[1,0]], "smooth": true}}}
    ],
    "render": [{"id": 9, "type": "SpriteRenderer", "ints": {"Facing": 1, "Blend": 0}, "strings": {"Texture": "VFX/T_Spark.uvtex"}}]
  }]
}
```
- Input shorthands: `{"constant": x}`, `{"random": [a, b]}`, `{"curve": [[t, v], ...]}` (with an
  optional `"tangents"`), `{"gradient": [[t, r,g,b,a], ...]}`, `{"parameter": "Name"}` and
  `{"wire": nodeId}`.
- Tests round-trip **every** module type with non-default inputs.

---

## 5. The simulator (CPU)

### 5.1 Attributes (structure of arrays)
For each emitter, keep one `std::vector<float>` per attribute, all the same length (the count of
live particles):
- `posX/Y/Z`, `velX/Y/Z`, `age`, `lifetime`;
- `colR/G/B/A`, `sizeX/sizeY`, `rotation`, `spin`, `frame`, `mass`;
- `id` (`std::uint32_t`), `seed` (`std::uint32_t`, the particle's own random stream);
- `prevX/Y/Z` (for ribbons and collision).

Allocate only the attributes the program reads or writes. The compiler returns a bitmask. Store the
arrays in `VfxEmitterStateUVE`.

### 5.2 One frame (`VfxInstanceUVE::TickUVE(float dt, const VfxOwnerUVE& owner)`)
- `owner` holds the world position, rotation (quaternion), velocity and the quality level (0..2).
- Clamp `dt` to [0, 0.1]. A longer gap is split into steps of at most 1/30 s.

For each enabled emitter:
1. **Age.** `emitterAge += dt`. When `looping` is set and `emitterAge >= duration`, subtract
   `duration` and start a new loop: bursts fire again. When `looping` is off and the duration has
   passed, the emitter stops spawning. It completes when its particle count reaches 0.
2. **Emitter Update.** This gives `spawnCount`.
   - **SpawnRate:** `carry += rate * dt * spawnScale`, then `n = floor(carry)` and `carry -= n`.
     `rate` may be a curve over `emitterAge / duration`.
   - **SpawnBurst:** inputs `Time`, `Count`, `Cycles` (0 means every loop) and `Interval`. A burst
     fires when `emitterAge` crosses `Time + k * Interval` for `k < Cycles`. Count only crossings
     within this step, so a burst never fires twice.
   - **SpawnPerDistance:** `carry += |ownerPos - lastOwnerPos| * perUnit`. Spawned particles are
     spread linearly between the last and the current owner position (no gaps in trails).
   - Clamp `spawnCount` so the live count never exceeds `min(maxParticles, scalability cap)`.
3. **Kill.** Remove particles with `age >= lifetime`, and those killed by KillByBounds or
   KillByCondition. Use **swap-remove**: copy the last particle into the hole and pop, for every
   attribute array. The order is not stable. The ribbon renderer sorts by `id` instead.
   - **EventOnDeath:** record the dying particle's position, velocity and colour into the
     sub-emitter's spawn queue before removal.
4. **Particle Spawn.** For each new particle:
   - `id = nextId++`;
   - `seed = hash(emitterSeed, id)`, where `Math::RandomUVE rng(seed, stream = emitterIndex)`.
   - Then run the spawn ops in order. A RandomRange input draws from the particle's `rng`. A
     Curve input is sampled at age 0.
   - Positions come from the Shape modules: the emitter's last shape wins, and a missing shape
     means a point at the origin.
   - In **World** space, the position and velocity are transformed by the owner. In **Local**
     space they stay in local space, and the renderer applies the owner transform.
5. **Particle Update.** Run the ops in order over all particles, in tight loops: one loop per op,
   over the arrays.
   - Then integrate: `vel += acc * dt; pos += vel * dt; rotation += spin * dt; age += dt`. The
     ops add to `acc`, which is a per-frame scratch array zeroed each frame.
   - The normalized age `t = age / lifetime` is computed once per frame into a scratch array.
6. **Bounds.** Compute the live AABB (min and max over the positions) for the debug display and
   for culling when `fixedBounds` is empty.

**Determinism:** the same asset, seed, dt sequence and owner path must give bit-identical arrays.
Never use `rand()` or time. Never iterate over an unordered container in a way that changes results.

### 5.3 Module formulas (Particle Update)
| Module | Inputs (defaults) | Effect per particle |
|---|---|---|
| Gravity | `Acceleration` vec (0,-9.8,0) | `acc += a` |
| Drag | `Coefficient` (1) | `acc -= vel * c` |
| AddVelocity | `Velocity` vec | `vel += v * dt` (a constant push) |
| CurlNoise | `Strength` (1), `Frequency` (1), `Speed` (0.5) | `acc += curl(noise3(pos*f + time*speed)) * s`; use a 32³ tiled noise table built once from the seed (`vfx_values.cpp`); curl by finite differences |
| Vortex | `Axis` vec (0,1,0), `Center` vec, `Strength` (2), `PullIn` (0) | tangential `cross(axis, r)` normalized × strength, plus `-r̂ * pullIn` |
| PointAttractor | `Position` vec, `Strength` (5), `Radius` (5), `KillRadius` (0) | `acc += dir * s * (1 - d/radius)` when `d < radius`; kill when `d < killRadius` |
| Wind | `Velocity` vec, `Turbulence` (0) | `acc += (wind - vel) * 0.5 + noise * turbulence` (relaxes toward the wind) |
| LimitSpeed | `MaxSpeed` (10), `Damping` (1) | if `|v| > max`: `v = lerp(v, v̂*max, damping)` |
| Orbit | `Axis`, `Radius`, `Speed` | sets the position on a circle around the spawn point (overrides integration for x and z) |
| ColorOverLife | `Color` (gradient) | `col = gradient(t) * spawnColor` |
| SizeOverLife | `Scale` (curve, 1) | `size = spawnSize * curve(t)` |
| RotationRate | `Rate` (deg/s) | `spin = rate` |
| SizeBySpeed | `MinSpeed`, `MaxSpeed`, `MinScale`, `MaxScale` | scale by `smoothstep(min, max, |v|)` |
| SpriteFlipbook | ints `Rows`, `Columns`; `Mode` (0 over life, 1 fps); `Fps` | `frame = floor(t * rows * cols)` or `floor(age * fps) % (rows*cols)` |
| CollisionPlane | `Point`, `Normal`, `Bounce` (0.4), `Friction` (0.1), `LifeLoss` (0), int `KillOnHit` | below the plane: reflect the normal component × bounce, scale the tangential by (1-friction), push back onto the plane; raise EventOnCollision |
| CollisionPhysics | same as Plane | a ray from `prev` to `pos` through `IRaycastSystemUVE`; CPU only, and only when the live count ≤ 2,000 (else it is ignored, with a compile warning) |
| CollisionDepth | same as Plane | GPU path only (V2c); on the CPU it compiles with a warning and does nothing |
| KillByAge | (none) | always on, implicit; the module exists so the graph shows it |
| KillByBounds | `Min`, `Max` vecs, int `Inside` | kill outside (or inside) the box |
| KillByCondition | a wire from a Math node | kill where the value > 0.5 |

**Spawn modules:**
- InitialVelocity sets `vel`.
- VelocityInCone takes `Axis` (0,1,0), `Angle` in degrees, and `Speed`. It picks a uniform
  direction inside the cone: `cosθ = lerp(1, cos(angle), u)`, φ = 2πv.
- InitialLifetime, InitialColor, InitialSize (vec2, or a float applied to both), InitialRotation
  (degrees), InitialSpin, InitialSpriteFrame and InitialMass set those attributes.
- InheritOwnerVelocity adds `ownerVel * Scale`.

**Shapes** (`vfx_shapes.cpp`). Each takes the int `Surface` (0 volume, 1 surface) and returns a
local position plus the outward normal (used by the "velocity along normal" option, the float
`NormalSpeed` on every shape):
- **Point.**
- **Sphere:** `Radius`, int `Hemisphere`. Uniform in volume: `r * cbrt(u)` and a uniform
  direction. On the surface: a uniform direction × r.
- **Box:** `Size` vec.
- **Cone:** `Angle`, `Length`, `Radius`.
- **Cylinder:** `Radius`, `Height`.
- **Torus:** `MajorRadius`, `MinorRadius`.
- **MeshSurface:** string `Mesh`, a `.uvmodel`. Pick a triangle with probability ∝ area (a
  cumulative-area table built once), then a uniform barycentric point (√u trick).

A test for every shape draws 10,000 points and checks each lies inside the shape (or on the
surface, within 1e-4), and that the mean is near the centre.

### 5.4 Values (`vfx_values.cpp`)
- **Curve:**
  - smooth: cubic Hermite with the key tangents;
  - linear otherwise;
  - clamped outside [first, last].
  - Bake to a 256-entry table on compile for the hot loop. A test checks the table against the
    exact sampling within 1e-3.
- **Gradient:** linear in **linear colour space**, with alpha separate, baked to 256 entries.
- **RandomRange:** uniform `[min, max)`, from the particle's `rng`.
- **UserParameter:** read from the instance's parameter block. An override set at runtime wins
  over the asset value.

### 5.5 Instances, pooling, budget (`vfx_world.cpp`)
- `VfxWorldUVE` owns every playing instance.
  - `SpawnUVE(assetPath, transform, attachTo)` returns a `VfxHandleUVE` (index plus generation).
  - It also offers `StopUVE` (stop spawning, let the particles die), `KillUVE` (remove now),
    `SetParameterUVE(handle, name, value)` and `IsAliveUVE`.
- Loaded assets and compiled programs are cached by path. A hot reload of the `.uvfx` recompiles
  and restarts the instances that use it.
- **Global budget** (`MaxLiveParticles`, default 200,000): when it is over, the spawn scale of
  every instance drops in proportion for that frame.
- **Quality level** (Low, Medium, High) comes from Project Settings and picks the
  `scalability[level]` row. Android defaults to Low.
- **Distance LOD:** beyond `cullDistance`, an instance does not spawn. Instances outside the camera
  frustum, using `fixedBounds` transformed by the owner, update at a quarter rate (accumulate dt).

---

## 6. The compiler (`vfx_program.cpp`)

```cpp
struct ParticleCompileMessageUVE final { enum class Severity { Error, Warning } severity; std::uint32_t moduleId; std::string text; };
struct ParticleProgramUVE final { /* ops per stage, constant pool, baked curves/gradients, attribute mask, renderers */ };
[[nodiscard]] bool CompileParticleEmitterUVE(const Asset::ParticleSystemAssetUVE& system, std::size_t emitter,
                                             ParticleProgramUVE& out, std::vector<ParticleCompileMessageUVE>& messages);
```

**Rules:**
- **Errors**, each tested:
  - a module in the wrong stage;
  - a Wire input that names a missing node, or a cycle of Math nodes;
  - a UserParameter of the wrong type;
  - a Render stage with no renderer;
  - a SpriteRenderer or RibbonRenderer texture that is not a `.uvtex`, or a missing file;
  - a MeshRenderer or ShapeMeshSurface mesh that is missing;
  - an EventOnDeath or EventOnCollision whose target emitter does not exist, or that targets itself.
- **Warnings**, each tested:
  - CollisionDepth on the CPU;
  - CollisionPhysics on more than 2,000 particles;
  - `maxParticles` above 100,000 on the CPU;
  - two shapes in Particle Spawn (only the last is used).
- **Math nodes:** compiled to a small stack bytecode evaluated per particle. The ops are `add`,
  `sub`, `mul`, `div`, `min`, `max`, `lerp`, `clamp`, `sin`, `cos`, `abs`, `noise`, `random`,
  `age`, `normalizedAge`, `speed`, `posY`, `const` and `param`. Bound the bytecode at 256 ops per
  input.

---

## 7. Rendering (RenderSystems + shaders)

The renderer receives, per emitter and per renderer, a **draw list**: a pointer to the SoA arrays,
the count, the owner transform (Local space), the material or texture, and the renderer settings.
Add `VfxDrawListUVE` to `particle_draw_command.h`, or to a new `vfx_draw_list.h` in RenderSystems,
and feed it through `particle_render_bridge.h`. The old fixed quads stay for
`ParticleEmitterComponentUVE` objects that have no `.uvfx`.

### 7.1 Sprite renderer (V3a)
- **Instanced:** one unit quad. Per-instance data is packed each frame into one dynamic buffer:
  position, size, rotation, colour (RGBA8), frame, and velocity (for velocity alignment).
- **Facing** (int `Facing`):
  - 0: camera plane;
  - 1: camera-facing about the particle (spherical);
  - 2: velocity-aligned, stretched by `|v| * StretchScale`;
  - 3: a fixed axis (vec `Axis`).
- **Flipbook:** the UVs come from `frame`, `Rows` and `Columns`. With `FrameBlend=1`, the shader
  samples two frames and mixes by `fract(frame)`.
- **Blend** (int `Blend`): 0 Additive, 1 Alpha (premultiplied), 2 Opaque cutout (`AlphaCutoff`).
  - Alpha emitters are **sorted back to front** by view depth each frame. On the CPU, use a radix
    sort on 32-bit keys.
  - Additive and opaque emitters are not sorted.
- **Soft particles** (`SoftDistance`, 0 = off): fade the alpha by
  `saturate((sceneDepth - particleDepth) / SoftDistance)`, using the scene depth texture.
- **Camera fade** (`CameraFadeNear`, `CameraFadeFar`).
- **Material:** either a texture (string `Texture`) with an HDR emissive multiplier (`Emissive`),
  or a `.uvmaterial` (string `Material`).
- **Budget:** at most 1,000,000 instances per frame. Above that, draw the first N in sorted order
  and report it in the stats.
- Add the shader to `built_in_shaders.cpp` **and** `built_in/vfx_sprite.glsl` (byte-identical), and
  register it in the parity test list in `Test/RHI/Shader/shader_manager_tests.cpp`.

### 7.2 Ribbon renderer (V3b)
- Particles of one emitter, ordered by `id` (spawn order), form one strip. With int `PerParticle=1`,
  each particle keeps its own trail of its last N `prev` positions instead (a ring buffer per
  particle; `TrailLength`).
- The strip is camera-facing: each segment's side vector is `normalize(cross(tangent, toCamera))`
  × width/2.
- Width comes from `size.x` (or a `Width` curve along the ribbon). The colour is per particle.
- UV mode: 0 stretch (u = index/count), or 1 tile (u = distance / `TileLength`).
- The ribbon is drawn as a triangle strip, built on the CPU into a dynamic buffer.

### 7.3 Mesh renderer (V3b)
- An instanced draw of a `.uvmodel` (string `Mesh`) with its own material, or an override
  (string `Material`).
- Orientation (int `Orient`): 0 from `rotation` about `Axis`; 1 the velocity direction; 2 facing
  the camera.
- The scale is `size`.

### 7.4 Light renderer (V3b)
- Each particle can be a point light: `Radius`, `Intensity` multiplied by alpha, and its colour.
- The light system has 4 slots (`kMaxLightsUVE`). Per emitter, take the 3 brightest particles by
  intensity / distance² to the camera. Sum the rest into a 4th light at their intensity-weighted
  centroid, with the summed intensity. **Never** exceed the slots.

---

## 8. The editor window (V4a)

Model it on `shell/montage/editor_montage_editor.*`: how it opens a second native window, its
studio preview scene, its timeline, its toolbar and its Save. Read that code first.

- **Files:** `Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_editor.{h,cpp}` (window and
  layout), `editor_vfx_graph.{h,cpp}` (graph ↔ asset), `editor_vfx_preview.{h,cpp}` (preview
  scene and stats), `editor_vfx_inspector.{h,cpp}` (module inputs, curve and gradient editors).
- **Opening:** double-click a `.uvfx` in the Asset Library. Add an `ItemTypeUVE::ParticleSystem`
  with the badge "VFX" (see the badges around `editor_content_browser.cpp:239`).
- **Creating:** Asset Library > Add > Visual Effects > Particle System. A dialog picks a template
  (Empty, Fountain, Sparks, Fire, Smoke; more arrive in V2b and V3b) and a name (`VFX_` prefix).
  It creates the file and opens it.
- **Layout:**
  - **Left:** the emitter list, with add (from a template), duplicate, rename, enable, solo,
    reorder and delete. Below it, the user parameters, each with its name, type and value.
  - **Centre top:** the preview viewport: the studio backdrop (sky, floor, optional grid), an orbit
    camera, and a bounds box toggle.
    - Transport: play, pause, restart, speed (0.1×–4×), loop, and a fixed seed toggle.
    - Stats overlay: particles alive per emitter, CPU ms per emitter, and the instances drawn.
  - **Centre bottom:** the timeline: the duration, a scrub head, burst markers, and the warm-up region.
  - **Right:** the inspector for the selected module (or the emitter, or the system).
  - **Graph:** a tab beside the preview, or below it in a split. It uses the PRISM `BlueprintGraph`
    with the ParticleGraph style (`loadParticleGraphPreset`).
    - Four stage output nodes are fixed.
    - Module nodes chain into a stage.
    - Math and Value nodes wire into inputs.
- **Palette** (searchable, right-click or Tab), grouped as in §4's module list, plus Math/Value.
  Each entry has a one-line English description.
- **Inspector editors:**
  - a **curve editor**: drag keys, add with a double-click, delete with Del, tangents with Alt,
    and presets (Linear, Ease In, Ease Out, Bell, Fade Out);
  - a **gradient editor**: colour stops above, alpha stops below, click to add, drag, Del;
  - a random range shows min and max fields;
  - a parameter binding is a dropdown of the user parameters of that type;
  - every number is a **typed value field** (`prism::NumberField` with `setScrubbable(false)`).
- **Compile:** on every edit, recompile the emitter right away (it is cheap). Show the errors and
  warnings in a Compile Results tab, like the Logic Graph's: a click selects the node, and the node
  gets a red or yellow badge. The preview keeps the last good program.
- **Undo/redo:** every edit is an undo step. Use the graph's own history for graph edits, and
  snapshot the asset for inspector edits.
- **Thumbnails:** the Asset Library tile is the preview rendered at 60% of the duration (the peak),
  through the existing thumbnail path. That is V6, but leave the hook in V4a.

---

## 9. Hooks (V5)
- **Action Clip "Spawn VFX" notify:**
  - Add `kind` to `MontageNotifyUVE`: `Event` (today's), `SpawnVfx` or `PlaySound`. For a
    SpawnVfx notify, add `vfxAsset`, `bone` (string, empty = root), `offset` position and rotation,
    `attached` (bool) and the parameter overrides.
  - Keep JSON backward compatible: a missing `kind` means Event.
  - Runtime: in `engine_core.cpp` where the notify is raised (~2978), a SpawnVfx notify calls
    `VfxWorldUVE::SpawnUVE` at the bone's world transform (attached: follows the bone each frame).
  - `MontageNotifyStateUVE` gets the same fields. It spawns at begin and calls `StopUVE` at end.
  - The Action Clip editor's notify inspector shows these fields, and a `.uvfx` dropped on a notify
    track creates a SpawnVfx notify there. The Action Clip preview plays the effect.
- **Logic Graph nodes** (`blueprint_nodes.cpp`, translated like the existing nodes): Spawn VFX at
  Location, Spawn VFX Attached (object, bone), Set VFX Float, Set VFX Vector, Set VFX Colour,
  Stop VFX, Kill VFX and Is VFX Active. The handle is an int value. Add the `vfx.*` host functions
  to the script services, next to the existing ones.
- **Levels and entities:**
  - `ParticleEmitterComponentUVE` gains `systemAssetPath`, `autoPlay`, `seed` and a parameter
    override list.
  - When `systemAssetPath` is set, the object plays the `.uvfx` through `VfxWorldUVE` instead of
    the old fixed emitter.
  - The Inspector shows a `.uvfx` slot and the overrides.

---

## 10. Steps (do them in this order; each one is one commit)

Each step lists what to build, the tests that must exist and pass, and an xvfb check. "Done" means
all of the following are true:
- it builds;
- the three guards pass;
- the full tests show only the 5 known failures, and the PRISM tests show 0 failed;
- the xvfb screenshots exist;
- it is committed and pushed.

### V2a: Asset + CPU simulator core
- **Build:**
  - §3 asset kind and cooker;
  - §4 entirely;
  - the `VFX` module with `vfx_values`, `vfx_shapes` (all shapes except MeshSurface), `vfx_program`
    (with no Math nodes yet), `vfx_instance` (§5.1, §5.2) and the Emitter Update modules;
  - the Spawn modules;
  - the Particle Update modules Gravity, Drag, AddVelocity, LimitSpeed, ColorOverLife,
    SizeOverLife, RotationRate, SizeBySpeed, SpriteFlipbook, KillByAge and KillByBounds;
  - `vfx_templates`: Fountain, Sparks, Fire and Smoke as `ParticleSystemAssetUVE` built in code.
- **Tests** (`Test/VFX/`):
  - `ParticleSystemAssetUVETest.RoundTripsEveryModuleType`
  - `.RejectsEachInvalidCase` (one case per validation rule)
  - `.RejectsWrongSchemaAndKind`
  - `VfxValuesUVETest.CurveTableMatchesExactSampling`
  - `.GradientInterpolatesInLinearSpace`
  - `VfxShapesUVETest.<Shape>StaysInside` (each)
  - `.SurfaceSamplesLieOnTheSurface`
  - `VfxInstanceUVETest.SpawnRateCarriesFractions` (100.5/s over 10 s → 1005 ± 1)
  - `.BurstFiresOncePerCycle`
  - `.PerDistanceSpawnsAlongThePath`
  - `.MaxParticlesIsNeverExceeded`
  - `.SameSeedSameState` (1,000 steps, bit-identical)
  - `.DifferentSeedDifferentState`
  - `.KillByAgeRemovesExactlyTheExpired`
  - `.WorldVsLocalSpace`
  - `.NonLoopingCompletes`
  - `.WarmUpAdvancesBeforeTheFirstFrame`
  - `.EachTemplateCompilesAndRunsTenSeconds`
- **xvfb:** none yet (no renderer). Instead, write a 10-second Fountain run's live count per second
  to the test log.

### V3a: Sprite renderer
- **Build:**
  - §7.1;
  - the engine core ticks `VfxWorldUVE` after animation and before rendering;
  - a temporary path that lets a `ParticleEmitter3D` play a `.uvfx` (V5 finishes it).
- **Tests:**
  - sprite corner expansion for each facing mode (pure math in a helper);
  - the radix sort order;
  - the soft-particle fade function;
  - the flipbook UV for frame k;
  - the budget cut.
- **xvfb:** a level with the Fountain and Sparks templates (written to `.uvfx` files) in front of
  the camera. Screenshots: additive sparks, alpha smoke sorted correctly, soft particles meeting
  the floor.

### V4a: Editor window
- **Build:** §8 in full, except Math nodes in the palette (they come with V2b), plus the thumbnail hook.
- **Tests:**
  - graph ↔ asset conversion round-trips: the asset becomes graph nodes and back to the same asset;
  - curve and gradient editor helpers (the add, move and delete key logic);
  - a PRISM test for the gradient widget, if it is a new PRISM control.
- **xvfb:** create Sparks from the Asset Library; change Spawn Rate, the Colour gradient and the
  Size curve, and see the preview change at once; scrub the timeline; a compile error shown on a
  node (for example, delete the renderer).

### V2b: Forces, collision, events, Math nodes
- **Build:**
  - CurlNoise, Vortex, PointAttractor, Wind and Orbit;
  - CollisionPlane and CollisionPhysics;
  - KillByCondition;
  - EventOnDeath and EventOnCollision with sub-emitter spawn queues;
  - ShapeMeshSurface;
  - Math nodes (§6);
  - the Explosion template: a flash, debris with collision, smoke, and sparks on the debris' death.
- **Tests:**
  - curl noise is divergence-free within tolerance;
  - the vortex keeps the radius with PullIn 0;
  - plane collision bounce and kill;
  - a sub-emitter spawns N on each death;
  - Math bytecode results and cycle errors;
  - mesh-surface samples lie on triangles.
- **xvfb:** the Explosion in the editor; sparks bouncing on the floor.

### V3b: Ribbon, mesh and light renderers
- **Build:** §7.2–7.4, plus the Magic Trail (a ribbon on a moving orbit emitter) and Rain (mesh
  drops plus a splash sub-emitter on collision) templates.
- **Tests:**
  - ribbon vertex generation (count and facing);
  - the mesh orientation matrices;
  - the light reduction keeps at most 4 lights, and the summed energy is preserved.
- **xvfb:** a ribbon following a moving emitter; mesh debris; the explosion lighting the floor.

### V5: Hooks
- **Build:** §9 in full.
- **Tests:**
  - a notify JSON with no `kind` loads as Event;
  - a SpawnVfx notify spawns exactly one instance at the bone's world position (a skeleton test
    fixture exists in the Animation tests: reuse it);
  - the notify state stops at its end;
  - the Logic Graph VFX nodes translate and compile;
  - `vfx.*` host calls round-trip a handle.
- **xvfb:** an Action Clip with a Spawn VFX notify on a hand bone plays in the Action Clip preview;
  the same in game (Play); a level ParticleEmitter3D with a `.uvfx` and a Tint override.

### V2c: GPU simulation and scalability
- **Build:**
  - extend `ParticleComputeSimulationUVE` with a kernel that runs the Particle Update program;
  - the ops become a uniform/storage buffer of op records, and curves and gradients become 1D
    textures (256 wide);
  - the attributes live in SSBOs and are drawn by the sprite renderer straight from the GPU
    buffers (no readback);
  - spawning stays on the CPU, which uploads new particles each frame;
  - Auto = GPU above 2,000 `maxParticles` when compute is available;
  - CollisionDepth on the GPU;
  - Android uses GLES compute when present, else the CPU;
  - the quality level and distance LOD from §5.5.
- **Tests:**
  - the CPU and GPU give the same positions after 60 steps within 1e-3, for a deterministic
    program with no random inputs (run only when a GL context exists; follow how the GL tests
    skip otherwise);
  - scalability caps;
  - the global budget scaling.
- **xvfb:** 1,000,000 GPU sprites in the preview; report the frame time in the commit message.
  Also report 100,000 CPU sprites.

### V6: Profiler and thumbnails
- **Build:**
  - a VFX page in the editor's Profiler (systems, emitters, live counts, CPU ms, GPU ms);
  - Asset Library thumbnails for `.uvfx` from the preview at the peak frame;
  - Project Settings > Quality for the VFX level.
- **Tests:** the thumbnail frame choice; the settings round trip.
- **xvfb:** the Profiler page while the Explosion plays; the Asset Library tiles for every template.

---

## 11. Status file
Keep `Docs/Plans/VFX_Particle_Graph_STATUS.md` up to date after every step:
- the step;
- its commit hash;
- the test counts (passed, failed, and which ones);
- the screenshots taken;
- anything not finished, and why.

The reviewer reads this file first.
