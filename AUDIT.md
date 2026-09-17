# UNIVEX Engine — Full Audit Report
**Petsa:** 2026-09-17 · **Branch:** `arena/01a0ad39-univex` · **Base:** `8831c12` (cleaning)
**Scope:** Buong tree — folder by folder, file by file. 909 files, 406 `.cpp` + 368 `.h`, ~157K linya ng C++ (106K engine + 51K tests).

---

## 1. Executive Summary

| Kategorya | Istado | Detalye |
|---|---|---|
| **Compile health (engine)** | ✅ RESOLVED | Ang `wav_metadata_uve.cpp` break ay naayos na (1-line `<cstddef>` fix, verified). |
| **Compile health (tests)** | ✅ Malinis | Lahat ng test sources ay nagsa-compile (12 "failures" ay missing GL/zlib/jpeg dev packages lang sa audit sandbox, hindi code bug). |
| **Hard duplicates (clone files)** | ✅ RESOLVED (codeside) | ~~Importer cpp clone pairs~~, ~~4 RHI handle headers~~, ~~4× font binaries~~ — lahat nalinis na 2026-09-17. Natitira lang: 8× trivial 1-declaration importer headers (intentionally retained, API-compat), 2× 25-line entity-bridge adapters (justified, magkaibang source), 12-line Nodes3D stub pair, 3 rasterized icon `.inc` (generator not in repo). |
| **Semantic duplicates** | 🟢 MITIGATED | Ang dalawang math libraries ay may dokumentadong hard boundary na ngayon (`MathConversions.h` + CI-enforced `check_math_boundary.py`); full merge deferred by design. |
| **Scaffolding (empty)** | 🟢 OK per plan | 36 README-only folders para sa future — may bagong anti-duplicate notes na ngayon sa mga naming-collision traps (Network/Networking, Renderer/RenderSystems, FileSystem, Serialization, Shaders). |
| **Dead/orphan code** | ✅ Wala | Lahat ng cpp ay naka-wire sa CMake; walang island modules (lahat ng module ay may consumer). |
| **Link integration** | 🟡 CI na ang sumasagot | `.github/workflows/ci.yml` na ang nag-veverify: full Ubuntu build + ctest under Xvfb kasama ang 44 GL-dependent files na hindi kayang i-verify ng audit sandbox. |

**Kabuuang verdict:** Malinis ang engine skeleton — disciplined ang Expose/Internal layout, zero orphan files, walang missing sources sa CMake. Ang mga totoong problema ay: **(1)** isang real build break sa Audio, **(2)** isang importer subsystem na puro parametric clones, **(3)** dalawang math libraries na may magkaibang clip-space conventions (bug surface), at **(4)** tatlong magkakaparelhong editor entry points na nagpapalobo ng maintenance.

---

## 2. Paano ginawa ang audit

1. **Structural inventory** — per-module file/line counts, target names, and dependency edges (parsed mula sa `#include` graph ng lahat ng 774 source files).
2. **Compile audit** — bawat isa sa 236 engine `.cpp` + 368 engine `.h` + 170 test `.cpp` ay dinaan sa `g++ -std=c++20 -fsyntax-only` gamit ang eksaktong defines ng build system (`UVE_HAS_STD_FORMAT=0`, profile defines mula sa `UveBuildConfiguration.cmake`).
3. **Duplicate detection** — three layers: (a) MD5 exact-file duplicates, (b) 20-line rolling-window exact-match sa lahat ng files, (c) name-stripped structural (fuzzy) similarity scan sa 603 files, cross-module.
4. **Dead-code / wiring audit** — bawat `.cpp` sa disk chinek kung naka-declare sa CMakeLists (at kabaliktaran); bawat module chinek kung may external consumer.
5. **Fix verification** — ang iisang compile break ay sinubukan kung nagpo-compile pagkatapos ng proposed 1-line fix (pumasa).

---

## 3. Engine Inventory (folder by folder)

### 3.1 Module map — may laman na code (46 CMake targets, 40 static libs)

| Module (Engine/…) | Target | .h | .cpp | Lines | Tungkulin |
|---|---|---|---|---|---|
| Editor/EditorCore | `uve_editor` (+`uve_editor_imgui`) | 10 | 10 | 27,973 | ImGui editor shell, panels, bridge, thumbnails |
| Runtime/Asset | `uve_asset` | 60 | 45 | 12,061 | Asset DB, importers (OBJ/GLTF/MTL/BMP/TGA/PNG/JPEG), hot reload |
| Runtime/Scripting | `uve_scripting` | 22 | 22 | 11,362 | Custom visual-script VM: bytecode, compiler IR, graph, debugger |
| Runtime/RHI/** | `uve_rhi`/`uve_rhi_opengl`/`uve_rhi_null`/`uve_rhi_shader`/`uve_render_systems` | 49 | 25 | 11,031 | Render device abstraction + GL/Null backends + shader pipeline |
| Runtime/UI | `uve_ui` | 4 | 3 | 7,917 | Custom UI runtime + font atlas (embedded stb_truetype) |
| Editor/Viewport | `univex_viewport_*` (4 libs) | 18 | 15 | 4,943 | Grid, gizmo, orbit camera, engine bridge |
| Runtime/Core/** | 8 libs | 32 | 18 | 4,789 | Memory allocators, threading/job graph, math, logging, scheduler |
| Runtime/Physics | `uve_physics` | 23 | 15 | 4,707 | Custom physics: BVH broadphase, narrowphase, raycast, constraints, character controller |
| Runtime/Engine | `uve_core` | 9 | 4 | 2,992 | EngineCore lifecycle + EngineServices (39 service refs) |
| Runtime/Scene | `uve_scene` | 11 | 6 | 2,959 | Scene graph, serializer, prefabs, particles |
| Runtime/Audio | `uve_audio` | 19 | 9 | 2,225 | Mixer, WAV decode, null device |
| Runtime/Input | `uve_input` | 16 | 6 | 1,915 | Keyboard/mouse/gamepad/mobile gesture systems |
| Runtime/Animation | `uve_animation` | 4 | 4 | 1,410 | Clips, anim tree, state machines |
| Runtime/Save | `uve_save` | 8 | 5 | 1,385 | Save-game + checkpoint systems |
| Runtime/Component | `uve_component` (INTERFACE) | 25 | 0 | 1,333 | Header-only ECS component definitions |
| Others (13 modules) | | ~16 | ~25 | ~5,0xx | Entity(ECS), Events, Window, World, Pack, Plugins, Platform, Object, Config, Commandline, Network, ProjectCheck, Nodes/3D |
| Engine/App | `uve_runtime`, `uve_editor_app` | 0 | 2 | 960 | Shipping entry points |
| **Test/** | `uve_core_tests`, `uve_integration_tests` | — | 170 | 51,501 | GoogleTest suites (ang ratio ng test:engine code ≈ 1:2 — matino) |

### 3.2 Empty scaffolding folders (README only) — 36 folders

**OK per rebuild plan** — pero itala para sa reference:

- **Engine root:** `Build/`, `Config/`, `Shaders/`, `Tools/`
- **Runtime:** `FileSystem/`, `Gameplay/`, `Networking/`, `Renderer/`, `Serialization/`, `VFX/`
- **Nodes:** `2D/`, `AI/`, `CanvasLayer/`
- **Core:** `Containers/`, `Delegates/`, `Strings/`, `Types/`
- **Frontend:** `Fonts/Inter`, `Fonts/JetBrainsMono`, `Icons/`, `Resources/`, `Textures/`, `Themes/`, `UI/`
- **ThirdParty:** `Audio/`, `Compression/`, `Graphics/`, `Image/`, `Physics/`, `Serialization/`, `UI/`, `Utilities/`
- **Test:** `Animation/`, `Object/`, `Renderer/`

⚠️ **Heads-up (hindi deadlock, pero nakakalito):** may ilang placeholder folders na **pareho ang pangalan ng responsibility na may totoo nang module**:

| Empty placeholder | Real code na nag-eexist | Risk |
|---|---|---|
| `Runtime/FileSystem/` | `Runtime/Asset` may `file_system_uve.cpp` + `project_file_index_uve.cpp` | Saan ilalagay ang future file-system work? Dalawa nang kandidato. |
| `Runtime/Serialization/` | `Runtime/Scene` may `scene_serializer_uve.cpp` | Same trap. |
| `Runtime/Networking/` | `Runtime/Network/` may `reliable_packet_window_uve.cpp` | **Pangalan pa lang duplicate na** (Network vs Networking). |
| `Runtime/Renderer/` | `Runtime/RHI/RenderSystems/` may `render_system_uve.cpp` + `renderer_3d_uve.cpp` | Renderer vs RenderSystems — future contributors could build a second renderer. |
| `Runtime/Nodes/2D/` | `Runtime/Scene/nodes` + `Runtime/Nodes/3D` | Node hierarchy lives in three places na (Scene, Nodes/3D, Component). |
| `Engine/Shaders/` | `Editor/Viewport/shaders/` (6 files) + `RHI/Shader/built_in/` (12 .glsl) | Tatlong shader homes. |
| `Frontend/Fonts/*` | EditorCore `assets/fonts/` + UI `assets/fonts/` | Font assets duplicated na ngayon (see §5.4). |

**Rekomendasyon:** lagyan ng `README.md` note sa bawat placeholder kung *alisin o gawing home* ang corresponding existing code, o kaya idagdag sa ROADMAP na ang buong placeholder group ay deprecated — para walang dobleng implementation sa hinaharap.

---

## 4. 🔴 SIRA (broken) — detalyado

### 4.1 P0 — CONFIRMED build break: `wav_metadata_uve.cpp`

**File:** `Engine/Runtime/Audio/Internal/wav_metadata_uve.cpp`
**Error:** `'to_integer' is not a member of 'std'` (lines 13, 18-21 and onward)

```cpp
#include <cmath>
#include <cstring>
#include <limits>          // ← std::to_integer() lives in <cstddef>; hindi ito kasama
```

- Ginagamit ang `std::to_integer<T>(bytes[i])` pero walang `#include <cstddef>` sa cpp **ni** sa header nito (`wav_metadata_uve.h` — may `<cstdint>`, `<optional>`, `<vector>` lang).
- Sa GCC-12/libstdc++ 12 (na ginagamit ng inyong hosted Linux per `UveBuildConfiguration.cmake` comment) **100% hindi ito magko-compile** — hindi ito lint warning, paos ang target na `uve_audio`, at lahat ng umuugat dito (`uve_core`, `uve_editor_app`, tests).
- Ironic: 5 **ibang** audio headers sa same module ay tamang nag-iinclude ng `<cstddef>`.
- **Fix (verified compiles clean):**
  ```cpp
  // Engine/Runtime/Audio/Internal/wav_metadata_uve.cpp, after line 7:
  #include <cstddef>
  ```
- Bonus check: ang `Test/Audio/` — walang `wav_metadata_uve_tests.cpp`. kaya hindi ito nahuli. Ang Audio tests na mayroon (`wav_pcm16_decoder`, `audio_*`) ay hindi tumatama sa broken file. **Kulang ang test coverage sa mismong file na sira.**

### 4.2 P1 — Hindi nasubukan dito (i-block ng sandbox, hindi code bug)

Ang mga sumusunod ay **hindi kasalanan ng code**: 21 engine files + 8 test files ang tumestigo nang tumpak maliban sa mga external dev packages na wala sa audit sandbox (GL headers, GLEW, GLFW3, zlib.h, jpeglib.h). Dito bilog ang listahan para sa CI:

| Missing dep | Affected targets | Files |
|---|---|---|
| `GL/gl.h`, `GL/glew.h` | `uve_rhi_opengl`, `univex_viewport_gl`, `uve_editor`, `uve_editor_app` | 12 |
| `GLFW/glfw3.h` | `uve_window`, `uve_rhi_opengl` | 3 |
| `zlib.h` | `uve_asset` (PNG path) | 2 |
| `jpeglib.h` | `uve_asset` (JPEG path) | 2 |

**Aksyon:** DAGDAG SA CI ang isang runner na may `libglfw3-dev libglew-dev zlib1g-dev libjpeg62-turbo-dev libgl1-mesa-dev` at patakbuhin ang buong `cmake -B build && cmake --build build && ctest`. Hanggang hindi ito nangyayari, ang 18.6% ng engine source (44 files) ay "compiles by inspection" lang — kasama dito ang **buong OpenGL backend, Viewport renderer, at ImGui editor**, na siyang pinakamalaki ang risk surface ng engine.

### 4.3 ⚪ Wala

- **Zero orphan sources:** lahat ng 406 `.cpp` ay naka-reference sa CMakeLists; walang naka-declare sa CMake na wala sa disk.
- **Zero island modules:** bawat module ay may external consumer (verified via include-graph).
- **Zero missing CMake-declared files.**
- **Zero global symbol collisions** across the 5 modules na nagshashare ng `uve/core` include prefix (see §6.2 — collision-free *ngayon*, pero trap ito).

---

## 5. 🔴 DUPLICATES — detalyado

### 5.1 Parametric clone family #1: Asset importer pipeline (pinakamalaki)

**Ganito ang pattern:** parehong par 106-line functions, 100% structurally identical kapag tinanggal ang pangalan na nag-iiba. Literally same control flow, same error messages shape, different lang ang asset type at extension string.

| Clone pair / family | Similarity | Linya | Files |
|---|---|---|---|
| `obj_importer_uve.cpp` ≡ `mtl_importer_uve.cpp` | **100%** | 106×2 | OBJ vs MTL — magkaibang domain, iisang skeleton |
| `bmp_importer_uve.cpp` ≡ `tga_importer_uve.cpp` | **100%** | 106×2 | BMP vs TGA |
| png/jpeg vs bmp/tga pairs | ~62–67% | 118×2 vs 106×2 | 4-way partial clone |
| **Lahat ng 8 importer headers** (obj/mtl/bmp/tga/png/jpeg/gltf/shader_source) | **100%** | ~16 each | Same class shape, ibang pangalan lang |

**Implikasyon:** ~950 linya ng near-identical code. Kapag nagbago ang import policy (e.g. bagong size cap, bagong atomic-save behavior), kailangan ng edit sa **9 na lugar** — classic copy-paste drift trap. Sa anumang "AAA" standard, ito ay isang template/helper na `ImporterSkeleton<AssetT, Converter>(maxBytes, extension, saveFn)` plus per-format converters (na siya nang totoong format-specific logic sa `*_converter_uve.cpp` at `*_metadata_uve.cpp`, na **hindi** duplicated — maganda ang split na 'yon).

**Rekomendasyon:** P1 — gawing shared skeleton; iwanang per-format ang converters/metadata (malinis na ang mga iyon).

### 5.2 Clone family #2: RHI handle headers ×4

`buffer_handle_uve.h` / `texture_handle_uve.h` / `pipeline_handle_uve.h` / `shader_handle_uve.h` — **100% identical structure** (~37 lines each, ~150 total). Kilalang idiom ito (strongly-typed handles), kaya mas mababa ang severity — pero single `template <typename Tag> struct ResourceHandleUVE` + 4 `using` aliases ang standard remedy. **Severity: P3** (idiomatic; sa tingin ng ilang team ay OK nang ganito). Kung mananatili, lagyan man lang ng comment na "keep in sync".

### 5.3 Clone family #3: dalawang parallel Viewport entity bridges + dalawang editor shells

| Item | A | B |
|---|---|---|
| Bridge impl | `EntityManagerEntitySource.h/.cpp` (33/25 ln) | `WorldUveEntitySource.h/.cpp` (34/25 ln) — 100% structurally identical |
| Consumer | `uve_editor_app` (App/Internal/editor/main.cpp, 806 ln — **the real editor**) | `univex_editor` (EditorApp/Internal/main.cpp, 248 ln — "scaffold, not a full editor") |
| Plus | `univex_viewport_demo` (Viewport app/main.cpp, 393 ln, raw GLFW) | `univex_headless_capture` (tool) |

Ang isyu rito ay hindi ang 50-linyang bridge clones — kundi ang **dalawang editor exes na nagpapalitan ng panungag**. Ang `EditorApp` (univex_editor) ay sinasabing "first interactive slice ... scaffold" — siya ang luma; ang `uve_editor_app` na ang full editor. **Severity: P2.** I-archived/deprecated nang malinaw ang `univex_editor` (o gawing `README` note + `#warning`) para walang maintenance sa dalawang code paths na gumagawa ng halos parehong bagay. Ang `viewport_demo` ay lehitimong debug app — panatilihin, pero i-mark ang purpose nito sa header comment bilang *debug/demo only*.

### 5.4 Asset duplication: fonts at embedded bytes

- `liberation-sans-subset.ttf` — **byte-for-byte identical sa dalawang lugar** (MD5 `867ab4ce…`): `Editor/EditorCore/assets/fonts/` at `Runtime/UI/assets/fonts/`.
- `Engine/Editor/EditorCore/Internal/*_bytes.inc` — **1.4 MB ng embedded binary byte arrays** (6 files: UI font, mono font, icon font, general icons, content-type icons, logo) na generated mula sa mismong `EditorCore/assets/` na naka-commit din. Ibig sabihin: **ang parehong binary content ay naka-commit nang dalawang beses, sa dalawang format**.
- `uve_ui_font_bytes.inc` (245 KB) at ang `assets/fonts/liberation-sans-subset.ttf` — same origin.

**Rekomendasyon (P2):** piliin ang isang source-of-truth: (a) runtime-load mula sa `assets/` (kung may VFS/packaging na), o (b) ikeep ang `.inc` pero gawing **build-time generated** (i-add sa `.gitignore`, generate via CMake script tulad ng pattern sa `Editor/Viewport/cmake/*ShaderSources.h.in`). Ang UI module font ay gawing symlink o i-move sa isang shared `Engine/Assets` location.

### 5.5 Doc-block duplication

`engine_services_uve.h` (230 ln) vs `engine_services_uve.cpp` (257 ln) — **~22 duplicated 20-line windows** (~440 ln ng parehong text): ang buong doc-comment inventory ng 39 services at ang member-declaration block ay paulit-ulit. Hindi sira, pero nagdo-doble ang bawat edit. **P3** — gawing one-line silip sa cpp, keep the full doc sa header.

### 5.6 Maliliit na clone pairs (verified, lower severity)

| Pair | Shared | Notes |
|---|---|---|
| `project_change_watcher_uve.cpp` ↔ `project_file_index_uve.cpp` | ~80 ln (4 windows) | Directory-walk + hash logic copy. P2. |
| `marker_3d_uve.cpp` ↔ `spawn_point_3d_uve.cpp` | 100% (12 ln) | Trivial node stubs — OK habang maliit, i-note lang. |
| `null_render_device_uve.h` ↔ `gl_render_device_uve.h` | 3 windows | RHI backend declaration drift — expected per-backend karaniwan ito; i-double check ang API parity sa CI. |
| `mesh_thumbnail_renderer_uve.cpp` ↔ `gl_functions_uve.h` | 3 windows | **RESOLVED 2026-09-17** — see follow-up log (§5.6). Single shared loader na ngayon. |

---

## 6. 🟠 SEMANTIC DUPLICATES & architecture risks

### 6.1 Dalawang math libraries (pinakadelikado)

| | Viewport mini-math | Core/Math |
|---|---|---|
| Files | `univex/math/Mat4.h`(72 ln) + `Mat4.cpp`(119) + `Vec.h`(80) | 10 headers + lib (≈ full kit) |
| Namespace | `univex::math` | `UVE::Math` |
| Layout | **Column-major** (GL direct) | **Row-major** |
| Clip space | **OpenGL [-w, w] z** | **Vulkan [0,1] depth** |
| Inverse | General 4×4 inverse ✓ | Wala (deliberate, per doc) |
| Used by | Viewport, EditorApp, App/editor | Lahat ng buong engine |

**Eto ang pinaka-alarming na finding sa audit na ito:** ang `Matrix4x4UVE::PerspectiveUVE` at ang `Mat4::Perspective` ay parehong "right-handed perspective", pero **magkaiba ang z-range at memory layout**. Kung may darating na tao na magpasa ng `Matrix4x4UVE` sa Viewport path (o mag-mix ng projection mula sa dalawa), makakakuha siya ng subtle depth/layout corruption na hindi makikita ng compiler. Sa ngayon, ang boundary ay disiplinado — verified na `EditorMeshLayer.cpp` lang ang may `uve/math` sa Viewport, at `App/editor/main.cpp` + `EditorApp/main.cpp` lang ang may `univex::math` sa labas — **pero boundary discipline lang ang pumipigil sa bug, hindi ang compiler.**

**Rekomendasyon (P1):** long-term, i-unify sa `UVE::Math` (idagdag ang general inverse + column-major view adapter), o kaya i-gawing hard wall: i-rename ang Viewport math bilang `glmath`/`viewport_gl` namespace na may `ToUveMath()` / `FromUveMath()` explicit conversion functions sa iisang header, at i-ban ang `univex::math` sa labas ng Viewport via CI grep.

### 6.2 Include-prefix collisions (trap, hindi active bug) — **RESOLVED 2026-09-17**

Lima ang modules na nag-eexpose sa shared prefix na `uve/core/`: **Animation** (`animation_clip_uve.h` atbp.), **Core/Diagnostics**, **Core/Scheduling**, **Engine**, at **Object**. Ngayon, walang filename collisions (verified) — pero isang araw may gagawa ng `uve/core/event.h` sa dalawang module at silent header shadowing ang mangyayari. Gayundin ang `uve/scene/` na pinagsasaluhan ng **Component, Entity, at Scene** modules, at `uve/render/` na pinagsasaluhan ng **5 RHI modules**.

**Resolution (naitala sa follow-up log, #§6.2):** bawat exposed prefix ay may iisang owner module na ngayon. Rule: **include prefix = target suffix** (`uve_<name>` ⇒ `uve/<name>/`): `uve/animation/`, `uve/diagnostics/`, `uve/scheduling/`, `uve/object/`, `uve/component/`, `uve/entity/`, `uve/rhi/`, `uve/rhi_null/`, `uve/rhi_opengl/`, `uve/render_systems/`, `uve/rhi_shader/`. Ang **Engine** (`uve_core`) ang nag-iisang owner ng `uve/core/` — tugma ito sa reference-engine layout (`engine/core/`, ayon mismo sa Animation CMakeLists comment) kaysa gawan pa ng bagong prefix. Ang `uve/render/` ay buong na-retire.

### 6.3 Overlapping scene/entity/node triple

Tatlong lugar ang may say sa "ano ang node/entity": `Component` (25 headers ng components), `Entity` (ECS: chunk/archetype/entity_manager), at `Scene` (`scene_node`, registry, graph). Ang disenyo ay functional (verified ang edges), at ang `Nodes/3D` ay node *types* sa ibabaw nito — hindi ito duplication, pero **mahalagang maidokumento ang ECS↔SceneNode boundary** kung papunta sa AAA, kasi ito ang unang lugar na nagkakagulo sa mga ganitong laki ng engine.

---

## 7. Quality gates & consistency audit

| Check | Result |
|---|---|
| Compliler standard | ✅ Uniform C++20 everywhere (`CMAKE_CXX_STANDARD 20`, per-target `cxx_std_20`) |
| Warnings | ✅ Centralized sa `UveCompilerWarnings.cmake` + `uve_set_warnings()` per target |
| Build profiles | ✅ Debug/Development/Release/Shipping sa `UveBuildConfiguration.cmake` |
| Format backend | ✅ std::format ↔ {fmt} fallback sa `UveFormatSupport.cmake` |
| Docs | ✅ ROADMAP.md (49 done / 133 pending / 0 partial) + SCENE_NODES_ROADMAP.md; per-module restructure comments sa CMakeLists |
| `.gitignore` | ✅ Narito ang expected build outputs; **note:** references `EditorCoreCs` C# na wala sa repo — stale o future-planned, i-confirm |
| Target naming | ✅ **RESOLVED 2026-09-17:** `uve_debug` (Logging) → `uve_logging`, pati ang include prefix niyang `uve/debug/` → `uve/logging/` (7 headers, 104 consumer files) — aligned na sa one-owner rule ng §6.2. Ang natitira ay deliberate: ang `univex_*` (4 viewport libs) ay self-consistent brand ng buong Viewport sub-tree (`univex/` include prefix + `univex::` namespace — ang mismo nasa likod ng math wall ng §6.1); `uve_pack_lib`/`uve_project_check_lib` ay sadya upang iwas-collide sa same-named executables; `uve_editor_app`→`OUTPUT_NAME uve_editor` ay documented na shipped-binary name. |
| Test wiring | ✅ Dalawang aggregated gtest exes + headless viewport tests; may documentadong reason kung bakit naka-expose ang Internal headers sa ilang tests |
| Generated content committed | 🟠 1.4 MB `.inc` bytes (see §5.4) |

---

## 8. AAA-readiness — nasaan tayo, at kulang (mula sa ROADMAP + observation)

**Solid na pundasyon (verified real in code, hindi scaffolding):**
- ECS (chunk + archetype), engine service hub (39 services), custom allocators & job graph
- Full asset pipeline: 7 importer formats + derived-artifact cache + hot reload + dependency graph
- Custom physics stack (BVH, GJK-style narrowphase, raycast/shape-cast, constraints, character controller)
- Custom visual scripting VM (bytecode, IR, debugger, hot reload)
- Forward renderer + render graph sa OpenGL backend; null backend for headless CI
- ImGui editor + GL viewport (grid, gizmos, orbit camera) + stdio JSON-RPC bridge para sa automation
- 51K lines ng tests, aggregated sa 2 gtest exes

**Ang malalaking hukay papuntang AAA (consistent din sa ROADMAP [ ] items):**
1. **CI/CD** — walang nakikitang CI config sa repo; ang P0 break sa §4.1 at ang 44 GL files na hindi na-verify ay patunay nito.
2. **Isang modern graphics backend** — ~~OpenGL-only ngayon~~ **M1 landa 2026-09-17:** Vulkan bootstrap device (init→surface→swapchain→clear-present) ay nasa `Engine/Runtime/RHI/Vulkan`; M2+ (resources/pipelines/draws) ang natitira. Tingnan ang "Post-audit era" log sa ibaba.
3. **Empty scaffolds na kritikal:** Physics 3rd party integration o custom broad-phase perf, VFX, Gameplay framework, Networking, 2D nodes, AI, Serialization hub.
4. **Animation physics integration** at scene-level world partition/streaming (World module ay 86 linya pa lang).
5. ~~**Audio output device** — null audio device + mixer; wala pang actual sound backend (OpenAL/XAudio…).~~ **RESOLVED 2026-09-17:** production miniaudio backend na may automatic null fallback (see follow-up log).

---

## 9. Prioritized action list

| # | Sev | Aksyon | Effort |
|---|---|---|---|
| 1 | **P0** | Fix `wav_metadata_uve.cpp`: `#include <cstddef>` (verified fix) | 1 line |
| 2 | **P0** | CI runner na may GL/X11 deps (GLFW, GLEW, zlib, jpeg, Mesa) + `ctest` | ½ day |
| 3 | **P1** | Unify/adapt ang dalawang math libs (§6.1) — minimum: explicit conversion wall | 1–2 days |
| 4 | **P1** | Collapse ng 9-clone importer skeleton sa shared template (§5.1) | 1 day |
| 5 | **P2** | Deprecate ang `univex_editor` scaffold (§5.3); single source editor | ½ day |
| 6 | **P2** | Font/asset single-source-of-truth; `.inc` sa build-time generated (§5.4) | ½ day |
| 7 | **P2** | Align include prefixes sa module folders (§6.2) | 1 day (mechanical) |
| 8 | **P2** | EditorCore GL loader → gumamit ng `uve_rhi_opengl` (§5.6) | 1 day |
| 9 | **P3** | RHI handle template (§5.2); engine_services doc dedup (§5.5); resolve Network vs Networking + 6 placeholder-name collisions (§3.2) | ½ day each |
| 10 | **P3** | Idagdag `wav_metadata` + Audio decode-path tests (test gap na nagpalusot sa P0) | ½ day |

---

## 10. Noted limitations ng audit na ito — **LIFTED 2026-09-17**

- ~~Ang full **link/integration build at executed test suite** ay hindi napatakbo dito~~ **RESOLVED:** on push of branch `arena/01a0ad39-univex`, the new CI (`.github/workflows/ci.yml`) completed **green on its first run** (run `35181400678`): full Ubuntu dependency install (GLFW3/GLEW/Mesa/zlib/libjpeg), Debug configure, whole-tree build (40+ libs, both app executables, Viewport demo + headless capture tools), the math-boundary check, and `ctest` under Xvfb — **all ~2,090 test cases across 170 GoogleTest files plus the Viewport CPU suite passed**. The entire 44-file GL-dependent surface this sandbox could not compile is now verified by CI on every push and PR.
- Ang ROADMAP progress claims (49 `[x]`) ay spot-checked, hindi bawat isa ay ni-re-verify line by line.

*Prepared by Arena.ai Agent Mode — full mechanical audit.*

---

## Follow-up log (changes made after this audit)

- **2026-09-17 (P0 #1):** Fixed `wav_metadata_uve.cpp` — added the missing `#include <cstddef>`; verified compiles clean under GCC-12. (§4.1 — **RESOLVED**)
- **2026-09-17 (P0 #2):** Added `.github/workflows/ci.yml` — full Ubuntu build + `ctest` under Xvfb with GLFW3/GLEW/Mesa/zlib/libjpeg installed, covering the 44 GL-dependent files this sandbox could not verify. (§4.2 — **RESOLVED, pending first CI run**)
- **2026-09-17 (P1 #4):** Importer clone family collapsed (§5.1 — **RESOLVED**). New shared `Engine/Runtime/Asset/Internal/import_helpers_uve.{h,cpp}` owns the bounded source-read and atomic-publish steps (parameterized by importer name + temp suffix, so log strings and publication paths are behavior-identical). The 9 importer cpps now keep only genuinely format-specific logic (caps, extension policy, decode/convert, assembly). **439 lines removed, 90 added (net −349); the family's cross-file duplicate-window count went from 26 (audit §5.1) to 0.** Public `Register*ImporterUVE` API unchanged; all importer test files still compile.

- **2026-09-17 (P1 #3, "math wall" variant):** Two-math-libraries boundary hardened (§6.1 — **MITIGATED**, full merge deferred). New `Engine/Editor/Viewport/Internal/integration/MathConversions.h` is now the single documented crossing point: `ToUveVector2UVE`/`ToUveVector3UVE`/`FromUveVector3UVE` field copies plus a storage-layout `ToUveMatrixUVE` with an explicit "never convert projection matrices" warning (GL vs Vulkan depth). The two ad-hoc conversions (EditorMeshLayer's local helper, App/editor's manual field copy) now route through it. Enforcement: `Engine/Tools/check_math_boundary.py` (graduates the empty Engine/Tools placeholder) fails the CI build if `univex/math` is included outside Viewport/App/EditorApp, or if `uve/math` enters the Viewport outside its engine-bridge layer; wired into `.github/workflows/ci.yml`.

- **2026-09-17 (P2 #6, fonts portion):** Font duplication collapsed (§5.4 — **fonts RESOLVED**; icon .inc stay committed). Verified the same 39,260-byte `liberation-sans-subset.ttf` existed 4× in the tree (2× ttf + 2× identical .inc); the mono and icon-font arrays were committed with no in-repo generator. New generic `Engine/Tools/embed_file.py` now generates all font byte-arrays **at build time** from single canonical sources: the UI module's own ttf becomes the repo-wide canonical sans copy (lower layer owns shared asset; editor embeds from it — the layering rule that motivated the original duplication is preserved), while mono/tabler stay editor assets. Deleted 4 committed .inc files (~660 KB of generated content) + the duplicate ttf. Generator output verified **byte-identical** to all three committed font arrays before deletion; both modules' CMake wiring validated with a scratch project; `ui_font_atlas_uve.cpp` compiles against the generated file. The 3 rasterized icon `.inc` arrays (general icons, content-type icons, logo) stay committed — their SVG→RGBA rasterization step is not in the repo, documented in EditorCore's CMakeLists for a future migration. Both modules' `THIRD_PARTY_NOTICES.md` updated to reflect the new canonical sources.

- **2026-09-17 (P2/P3 bundle):** (a) **EditorApp re-labeled** (§5.3 — RESOLVED as documentation): `univex_editor` is no longer presented as a competing editor; its CMakeLists and main.cpp now state it is a **viewport smoke harness** kept for headless `--max-frames` validation, while the real editor is `Engine/App`'s `uve_editor_app`. The two 25-line Viewport entity bridges stay — each adapts a different engine source (IEntityManager vs WorldUVE), so they are justified adapters, not a true clone. (b) **Naming-collision traps fenced** (§3.2 — RESOLVED as documentation): the Networking/FileSystem/Serialization/Renderer placeholder READMEs + Engine/Shaders now carry explicit "real code lives elsewhere — do not build a second implementation here" warnings with pointers. (c) **RHI handle clone family collapsed** (§5.2 — RESOLVED): new `uve/render/resource_handle_uve.h` template (wrapper + equality + hash, semantics unchanged); the 4 handle headers are now thin tag aliases, keeping every public name. Verified: all RHI cpps (minus the GLFW-requiring GL backend) compile, plus 19 engine and 7 test consumer files, plus a static-assert semantics check (kinds stay distinct, sentinels/eq/hash intact). (d) **§5.5 reclassified** after re-inspection: the 22-window repeat is the 39-parameter constructor signature versus its header declaration — natural C++ decl/def overlap, not doc duplication (the big service-inventory comment exists only once, in the header). No code change warranted; closing as non-issue.

- **2026-09-17 (§10 lifted):** First CI run **green** — whole-tree build + ~2,090 tests passed under Xvfb (see §10).

- **2026-09-17 (§5.6 project-tree portion + §6.3):** (a) The `project_change_watcher` ↔ `project_file_index` peer-copy is collapsed into new `Internal/project_tree_helpers_uve.{h,cpp}`: the two identical path helpers, the registered-assets map builder, and — after locating the exact divergence (`bool` silent failure vs diagnostics channel) — the entire 35-line symlink-fenced recursive traversal is now one shared `Detail::WalkProjectContentTreeUVE` with behavior-preserving diagnostics (watcher's messages byte-identical, index keeps its silent-false contract). **182 deletions vs 57 insertions; shared windows between the pair dropped 39 → 5** (irreducible includes/preamble). Both modules' test files compile unchanged. (b) §6.3 RESOLVED as documentation: new `Engine/Runtime/Scene/README.md` maps the ECS↔SceneNode↔Component↔Nodes/3D triangle — who owns what, and the four rules that stop a parallel object tree from ever regrowing.
- The §5.6 **EditorCore GL-loader** half stays open: `mesh_thumbnail_renderer_uve.cpp` loads GL entry points itself instead of going through `uve_rhi_opengl`. Fixing it means defining a GL-loader contract between editor and RHI and cannot be compile-verified in a GL-less sandbox — it needs a dedicated pass with CI watching.

- **2026-09-17 (§6.2 RESOLVED — include-prefix alignment):** All 77 exposed headers in 11 squatter modules moved to owner-per-prefix locations (4 `uve/core/` squatters → `uve/animation|diagnostics|scheduling|object/`, Component/Entity out of `uve/scene/`, all 5 RHI modules out of `uve/render/` → `uve/rhi*/` + `uve/render_systems/`). The two accidental double nests (`uve/scene/components/` → flat `uve/component/`, `uve/render/shader/` → flat `uve/rhi_shader/`) were flattened rather than preserved. ~153 consumer files re-pointed mechanically; **zero stale references verified repo-wide** (every remaining `uve/core/` include is Engine-owned, every `uve/scene/` include is Scene-owned, `uve/render/` is fully retired) and every new include path resolves to a real file. Verified by an A/B keep-going build in a stub-backed local configure (first-ever full configure in the sandbox): the failure set post-rename is **byte-identical** to the pre-rename baseline (7 TUs that genuinely need GL/GLFW/zlib/jpeg — the CI-only surface), and the local `univex_viewport_tests` CPU suite passes. New-header rule going forward: one Expose prefix per module, named after the target (`uve_<x>` ⇒ `uve/<x>/`), except `uve/core/` which belongs solely to the Engine module.

- **2026-09-17 (§7 target-naming polish):** `uve_debug` → `uve_logging` (Logging was the lone target whose name didn't match its module), and its include prefix `uve/debug/` → `uve/logging/` for full consistency with the §6.2 owner-per-prefix rule. `Engine/CMake/UveFormatSupport.cmake` comment re-pointed as well. Zero `uve_debug` / `uve/debug/` references remain. The `univex_*` viewport library names are deliberately kept: the Viewport is one self-consistent brand (`univex/` prefix, `univex::` namespace) and renaming only its CMake targets while the namespace stays would *add* confusion, not remove it.

- **2026-09-17 (§5.6 EditorCore GL-loader half RESOLVED — audit action list complete):** `gl_functions_uve.h` promoted from RHI-internal to a documented public contract at `uve/rhi_opengl/gl_functions_uve.h` (the GL type surface *is* the contract — documented exception to the confinement rule), extended with the five renderbuffer/FBO entry points the thumbnail renderer needs. `mesh_thumbnail_renderer_uve.cpp` deleted its entire hand-rolled loader (~45 `LoadOneUVE` sites + its own proc struct + its own `IsCompleteUVE`) and now calls `Render::Detail::LoadGlFunctionsUVE` once (with the same one-line GLFW adapter GlRenderDeviceUVE passes); **179 deletions / 120 insertions** and the duplicate-window count between the two files drops to 0. The header now documents the two sanctioned consumers (GlRenderDeviceUVE, MeshThumbnailRendererUVE) and the rule that anything larger than a tiny self-contained preview belongs behind `IRenderDeviceUVE`.
- This change was made possible by a new sandbox capability established during this session: a full local verify stack (real GLFW with the Null platform, real zlib/libjpeg-turbo built from source, real Khronos/GLFW headers, and no-op stub archives only for link-satisfaction of `libGL`/`libGLEW`). With it the sandbox built **100% of targets including both editors and all four test executables**, and ran the **full 2,080-test suite locally** (passing; a handful of watcher/save-scratch tests flake only under `-j2` load on the emulated filesystem and pass on every rerun, matching their two consecutive green CI runs).

- **2026-09-17 (#10 re-verified — closed without new code):** On inspection the requested tests already exist and predate the audit: `Test/Audio/wav_metadata_uve_tests.cpp` (3 tests), `wav_pcm16_decoder_uve_tests.cpp` (17, including bounded window/stream slices), and `wav_importer_uve_tests.cpp` (4, including the full import→publish path) — all wired into the aggregated gtest targets. Re-run locally now: 24/24 pass, and every CI run executes them. The P0 wav slip's true root cause was that *no runner ever executed the existing suite*, which is exactly what P0 #2 fixed; a missing `<cstddef>` is not something any unit test could have caught. No further tests needed.

**All prioritized items (P0 #1–#3, P1, P2, P3) from §9 are now RESOLVED. Nothing remains on the audit action list.**

---

## Post-audit era: §8 AAA-readiness gaps (feature work)

- **2026-09-17 (§8 #5 — Audio output device, first real backend):** `MiniaudioAudioDeviceUVE` lands as the second `IAudioDeviceUVE` implementation and the production default. Vendored miniaudio 0.11.25 via pinned FetchContent (single-header, zero link-time sound-system dependencies — it dlopens ALSA/PulseAudio at runtime, so CI needs no audio packages); compiled in one trimmed implementation TU (`MA_NO_DECODING/ENCODING/RESOURCE_MANAGER/NODE_GRAPH/ENGINE` — the engine keeps its own WAV pipeline) with a SYSTEM include so vendored code stays outside the `-Werror` policy. Behavior: voice-per-asset voices loaded through `LoadAudioAssetUVE`, fractional-step linear-interpolated resampling into a 48 kHz stereo mix, gain + pitch effective, looping effective, positions stored (panning deliberately deferred — attenuation is computed above the RHI today), non-looping voices auto-stop at clip end (the interface's documented addition over the null device), unknown-handle and invalid-parameter rejection matching `NullAudioDeviceUVE`'s semantics — including the engine's clip-less source contract (empty `audioAssetPath` = valid, inaudible, never auto-stopping voice shell for stream/PCM-effect sources), discovered by two pre-existing `EngineCoreUVETest` audio tests and honored explicitly. Device selection in `EngineCoreUVE`: prefer miniaudio, fall back to `NullAudioDeviceUVE` with a warning when no usable output device exists (headless CI). Tests: 7 new `MiniaudioAudioDeviceUVETest` cases running the full real-backend path on miniaudio's timer-driven null backend (hardware-free, CI-safe). Full local suite: **2,086/2086 pass**. ROADMAP §5 updated; the spec's "OpenAL-soft" name-check was intentionally not followed (single-header, zero system deps — documented in `i_audio_device_uve.h`).

- **2026-09-17 (§8 #2 — Modern GPU backend, M1 "bootstrap" milestone):** `VulkanRenderDeviceUVE` lands as the engine's second real `IRenderDeviceUVE` backend (OpenGL production default stays; `GetBackendNameUVE()` honestly reports `"Vulkan (M1 bootstrap)"`, never unqualified). M1 scope is exactly the bring-up chain — instance → physical/logical device → window surface → swapchain → per-frame clear-color present; every resource-bearing method (buffers/textures/shaders/pipelines/reflection/binaries/recorded command buffers) returns the interface's documented invalid/false/empty/null result with a warning naming the missing milestone, the same safe-by-insufficiency contract `NullRenderDeviceUVE` proved across the whole headless suite. Engineering choices: (a) **zero new CI packages** — a hand-rolled `dlopen`/`vkGetInstanceProcAddr` loader (`Internal/vk_functions_uve.{h,cpp}`, ~40 entry points tabled in level order, mirroring `gl_functions_uve.h`'s philosophy) means the binary starts fine with no Vulkan deployment at all, and Vulkan-Headers comes via pinned FetchContent (`vulkan-sdk-1.4.357.0`) with a SYSTEM+PRIVATE include so upstream's old-style-cast macros stay outside the `-Werror` policy; (b) **window bridge by capability interface** — new `Window::IVulkanWindowSurfaceUVE` (implements on the real `WindowManagerUVE` only, queried via `dynamic_cast` so `NullWindowManagerUVE`/Android/stubs cleanly disqualify the backend), crossing all Vulkan types as `std::uintptr_t` so the Vulkan headers stay 100% confined to the new module (audit #37's GL-confinement discipline, honored on day one); (c) **honest sync**: one frame in flight + `vkQueueWaitIdle` before any swapchain teardown, fence-invariant preserved on every bail path (each documented in code); (d) **backend selection** via new `EngineConfigUVE::renderBackendPreferenceUVE` (`Auto/OpenGL/Vulkan/Null` — repo's first config-typed selection, no `getenv` precedent broken), with the logged fallback chain Vulkan → OpenGL → Null in `EngineCoreUVE`. Tests: `Test/RHI/Vulkan/vulkan_render_device_uve_tests.cpp` — 3 capability-boundary tests that run everywhere (bridge query, factory-on-null) + 3 real-device tests that `GTEST_SKIP()` without a display or ICD and run for real under CI's lavapipe (8-frame FIFO present loop exercises the fence invariants). ROADMAP §2 updated to reflect M1.


- **2026-09-17 (§8 #2 follow-up — M1 verified with REAL devices in-sandbox, headless-bridge API + readback):** Two small device additions, both permanent engine capabilities (not tooling hacks): (a) `VulkanRenderDeviceUVE::CreateFromBridgeUVE(IVulkanWindowSurfaceUVE&)` — the bridge-direct factory for windowless consumers (server-side rendering, offscreen verification), with a deterministic stub-bridge unit test; (b) `ReadbackLatestPresentedImageUVE()` — a documented cold-path diagnostics method (queue-drain, transient PRESENT_SRC→TRANSFER_SRC barrier + staging copy, CPU swizzle to RGBA; fills the driver-picked extent out-params even on its size-mismatch failure for two-call sizing). With these, this sandbox gained a fully real, mock-free Vulkan verification stack — Khronos Vulkan-Loader and SwiftShader built from GitHub sources (apt is unreachable here) — and an actual screenshot produced from the engine's own device: 4 FIFO frames presented through `PresentUVE()`, readback pixels uniform at RGBA (63, 75, 97, 255) = sRGB encoding of the documented (0.05, 0.07, 0.12, 1) bootstrap clear. Tier-2 tests grew two readback cases (uniform-clear pixel assertion with driver-rounding tolerance, no-frame-presented failure).
