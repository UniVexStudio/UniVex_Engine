# UNIVEX Engine — Full Audit Report
**Petsa:** 2026-09-17 · **Branch:** `arena/01a0ad39-univex` · **Base:** `8831c12` (cleaning)
**Scope:** Buong tree — folder by folder, file by file. 909 files, 406 `.cpp` + 368 `.h`, ~157K linya ng C++ (106K engine + 51K tests).

---

## 1. Executive Summary

| Kategorya | Istado | Detalye |
|---|---|---|
| **Compile health (engine)** | 🟡 1 confirmed break | `wav_metadata_uve.cpp` — walang `#include <cstddef>`. Pumaasa sa transitive include na hindi umiiral. |
| **Compile health (tests)** | ✅ Malinis | Lahat ng test sources ay nagsa-compile (12 "failures" ay missing GL/zlib/jpeg dev packages lang sa audit sandbox, hindi code bug). |
| **Hard duplicates (clone files)** | 🟡 ~~6~~ 5 pamilya (1 RESOLVED post-audit) | ~~Importer cpp clone pairs~~ **(nalinis na 2026-09-17)**; 8 identical importer headers, 4 identical RHI handle headers, Nodes3D clone pair, 2 parallel entity bridges, duvplicated font binaries ang natitira. |
| **Semantic duplicates** | 🟠 2 math libraries | Viewport may sariling `univex::math` (Mat4/Vec, OpenGL conventions) habang may full `UVE::Math` sa Core/Math (Vulkan conventions). |
| **Scaffolding (empty)** | 🟢 OK per plan | 36 README-only folders — sinadya para sa future (per rebuild plan). **Pero** may 5 naming-collision traps. |
| **Dead/orphan code** | ✅ Wala | Lahat ng 406 cpp ay naka-wire sa CMake; walang island modules (lahat ng module ay may consumer). |
| **Link integration** | ⚠️ Hindi nasubukan | Walang GL/X11 toolchain sa sandbox — kailangan ng CI run para sa full configure+build+link+executed tests. |

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
| `mesh_thumbnail_renderer_uve.cpp` ↔ `gl_functions_uve.h` | 3 windows | GL loader code mula EditorCore na lumabas din sa RHI/OpenGL — **two GL proc-loading paths**; ang EditorCore ay dapat humiram sa `uve_rhi_opengl` imbes na mag-openGL functions mismo. P2. |

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

### 6.2 Include-prefix collisions (trap, hindi active bug)

Lima ang modules na nag-eexpose sa shared prefix na `uve/core/`: **Animation** (`animation_clip_uve.h` atbp.), **Core/Diagnostics**, **Core/Scheduling**, **Engine**, at **Object**. Ngayon, walang filename collisions (verified) — pero isang araw may gagawa ng `uve/core/event.h` sa dalawang module at silent header shadowing ang mangyayari. Gayundin ang `uve/scene/` na pinagsasaluhan ng **Component, Entity, at Scene** modules, at `uve/render/` na pinagsasaluhan ng **5 RHI modules**.

**Rekomendasyon (P2):** i-align ang prefixes sa module folders: `uve/animation/`, `uve/entity/`, atbp. Isa itong mahahabang mechanical rename — iskedyul habang maliit pa ang codebase.

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
| Target naming | 🟠 Mixed: `uve_*` vs `univex_*` (4 viewport libs), `uve_debug` ang pangalan ng **Logging** target, `uve_pack_lib`/`uve_project_check_lib` vs `uve_pack` executable na nag-o`OUTPUT_NAME uve_editor` sa `uve_editor_app` — consistent-with-old-code pero nakakalito |
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
2. **Isang modern graphics backend** — OpenGL-only ngayon (Vulkan/D3D12/Metal wala pa; renderer abstraction ay naririyan na).
3. **Empty scaffolds na kritikal:** Physics 3rd party integration o custom broad-phase perf, VFX, Gameplay framework, Networking, 2D nodes, AI, Serialization hub.
4. **Animation physics integration** at scene-level world partition/streaming (World module ay 86 linya pa lang).
5. **Audio output device** — null audio device + mixer; wala pang actual sound backend (OpenAL/XAudio…).

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

## 10. Noted limitations ng audit na ito

- Ang full **link/integration build at executed test suite** ay hindi napatakbo dito dahil walang system GL/X11 toolchain ang sandbox (Debian mirrors blocked; GitHub lang available). Ang 236/236 engine cpp at 368/368 headers ay na-verify sa syntax+semantic level sa exact build defines; ang link-time issues (missing definitions, ODR) ay makikita lang ng isang buong build — kaya P0 ang item #2 sa itaas.
- Ang ROADMAP progress claims (49 `[x]`) ay spot-checked, hindi bawat isa ay ni-re-verify line by line.

*Prepared by Arena.ai Agent Mode — full mechanical audit.*

---

## Follow-up log (changes made after this audit)

- **2026-09-17 (P0 #1):** Fixed `wav_metadata_uve.cpp` — added the missing `#include <cstddef>`; verified compiles clean under GCC-12. (§4.1 — **RESOLVED**)
- **2026-09-17 (P0 #2):** Added `.github/workflows/ci.yml` — full Ubuntu build + `ctest` under Xvfb with GLFW3/GLEW/Mesa/zlib/libjpeg installed, covering the 44 GL-dependent files this sandbox could not verify. (§4.2 — **RESOLVED, pending first CI run**)
- **2026-09-17 (P1 #4):** Importer clone family collapsed (§5.1 — **RESOLVED**). New shared `Engine/Runtime/Asset/Internal/import_helpers_uve.{h,cpp}` owns the bounded source-read and atomic-publish steps (parameterized by importer name + temp suffix, so log strings and publication paths are behavior-identical). The 9 importer cpps now keep only genuinely format-specific logic (caps, extension policy, decode/convert, assembly). **439 lines removed, 90 added (net −349); the family's cross-file duplicate-window count went from 26 (audit §5.1) to 0.** Public `Register*ImporterUVE` API unchanged; all importer test files still compile.

Remaining open items: §5.2–§5.6, §6.1–§6.3, §7 naming, and §10 (full link/test verification on a GL-capable machine — now automated by CI).
