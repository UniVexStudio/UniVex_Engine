# UniVex Engine - Audit and Refactor Report

## 1. Branch Discovery and Working Branch

- **Located feature branch**: `feature/vfx-particle-graph` (`origin/feature/vfx-particle-graph`, commit `5be1902a`, branched from `02659ffc` on top of `master`).
  - Other repository branches verified via `git branch -a`: `master` (`f698eeaf`, `origin/master` at `ece5c7b5`) and `origin/feature/prism-ui-editor` (`fdb2c16a`).
  - `feature/vfx-particle-graph` contained the VFX particle graph implementation plus Phase 2 graph authority fixes (`5be1902a`).
- **Working branch**: `fix/vfx-blueprint-audit`, branched from `feature/vfx-particle-graph` (`5be1902a`) and containing one commit per phase for Phases 3 through 10.
- **Repository build layout notes discovered during audit**:
  - Build tool entrypoint: `./uvbt` -> `Engine/Tools/UVBT/uvbt.py` (with companion C# definitions in `Engine/Tools/UVBT/UVBT.csproj`, `UVBT.Core/`, `UVBT.App/`). All 56 modules (`*.Build.cs`) and 6 targets (`*.Target.cs` under `Engine/Source/Targets/`) are driven through `./uvbt`.
  - Existing editor modules reside under `Engine/Editor/` (`EditorApp`, `EditorCore`, `Viewport`), while the Blueprint module architecture created in Phase 6 lives at top-level `Editor/Blueprint/` (`Editor/Blueprint/Blueprint.Build.cs`).

---

## 2. Original VFX Problems and Root Causes

### A. Graph did not drive runtime/compiled behavior
1. **Unwired modules were appended back into stages**: In `ExtractEmitterFromGraphUVE` (`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_graph.cpp`), stage rebuilding walked chain wires back from each fixed stage output node, then appended every remaining module not on the chain so spawned nodes would not be lost. Deleting all chain wires therefore produced the identical module set and compiled bytecode program.
2. **Removing Math/Value wires left stale wire bindings**: In `ExtractEmitterFromGraphUVE` (`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_graph.cpp`), disconnecting a Math/Value wire from a float or vector input pin reverted the local value to `Constant` only when the pin text differed, leaving `kind = Wire` pointing to the removed Math node in the asset.
3. **Compiler/runtime layer vs graph layer**: `vfx_program.cpp` and runtime evaluation consumed stage lists faithfully; the authority gap was exclusively in the graph-to-asset extraction layer. Templates (`vfx_templates.cpp`) were plain asset data converted to graph nodes plus chain wires.

### B. Nodes could not be added from the VFX graph canvas
- `BuildVfxGraphNodeLibraryUVE` had no caller. `EditorVfxEditorUVE` (`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_editor.cpp`) never called `setNodeLibrary(...)` or registered `onSpawn`, so the graph palette fell back to the generic Blueprint node library. Those generic nodes lacked `vfx_module:` keys and were silently dropped by `ExtractEmitterFromGraphUVE`.
- Pressing Delete invoked `BlueprintGraph::removeSelected()` directly (`Engine/Runtime/Prism/Internal/BlueprintGraph.cpp`), bypassing `onNodeAction` and allowing the four fixed stage output nodes to be deleted.

### C. Editor preview policy on compile error
- `editor_vfx_window.cpp` retains the last valid compiled program in the live preview when the graph fails compilation (e.g., an emitter whose Render stage has no reachable renderer), while updating the status bar and Render node error indicator. This behavior is intentional and covered by existing editor tests.

---

## 3. VFX Fixes Implemented (Phase 2, Commit `5be1902a`)

- **`Engine/Runtime/Asset/Expose/particle_system_asset.h`, `Engine/Runtime/Asset/Internal/particle_system_asset.cpp`**:
  - Added `ParticleEmitterAssetUVE::detached` to store graph nodes that are not connected into a stage chain. Serialized under `"detached"`, loaded, and validated (cap 256, same per-module validation). Backward-compatible with existing `.uvfx` files without `"detached"`. Never read by the VFX bytecode compiler.
- **`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_graph.h`, `Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_graph.cpp`**:
  - Made stage chain traversal in `ExtractEmitterFromGraphUVE` the sole authority for stage module lists; any unwired module moves to `detached` and does not compile.
  - Fixed Math/Value wire disconnection to unconditionally revert affected float/vector inputs to `Constant`.
  - Added `SpawnVfxGraphNodeUVE` (collision-free IDs, never colliding with fixed stage IDs) and `CanConnectVfxPinsUVE` (chains stay within one stage, single outgoing chain wire per module, Math outputs wire only into Float module inputs).
- **`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_editor.cpp`**:
  - Installed the VFX node library (stage modules plus 21 Math/Value nodes), spawner, pin connection validator, and stage-node lock predicate on the graph.
  - Updated ID scans for copy/duplicate/new-emitter and user-parameter renames to include `detached` modules.
- **`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_inspector.cpp`**:
  - Extended module lookup to include `detached` modules so unconnected nodes can still be selected and inspected.
- **`Engine/Runtime/Prism/Expose/prism/BlueprintGraph.h`, `Engine/Runtime/Prism/Internal/BlueprintGraph.cpp`**:
  - Added generic `nodeLocked` predicate blocking delete, cut, copy, and duplicate on locked nodes (fixed VFX stage output nodes).
- **`Engine/Runtime/VFX/Tests/vfx_graph_authority_tests.cpp`** (relocated from `Test/VFX/` in Phase 8):
  - Added 11 authority tests covering template graph structure, removing all chain wires, cutting one chain wire, node deletion, pin value mutation affecting compiled constants, spawning every library node with unique IDs, detached node inertness until connected, Math wire attach/detach constant restoration, connection rule rejection, stage-node lock enforcement, and detached module save/load round-trip.

---

## 4. Architecture Changes

1. **Replaced `Engine/Runtime/UVScript/` with `Editor/Blueprint/` (Phase 6, Commit `5d795d4a`)**:
   - Removed `Engine/Runtime/UVScript/` completely (`UVScript.Build.cs`, `README.md`, and all 18 source files migrated).
   - Created UVBT module `Editor/Blueprint/Blueprint.Build.cs` (`BlueprintModule`, zero engine runtime dependencies) containing:
     - `Editor/Blueprint/Compiler/Frontend/`: `blueprint_ast.h`, `blueprint_parser.h`, `blueprint_parser.cpp`
     - `Editor/Blueprint/Compiler/Intermediate/`: `blueprint_program.h`
     - `Editor/Blueprint/Compiler/Backend/`: `blueprint_compiler.h`, `blueprint_compiler.cpp`, `blueprint_codegen.h`, `blueprint_codegen.cpp`, `blueprint_compiler_main.cpp`
     - `Editor/Blueprint/Runtime/VirtualMachine/`: `blueprint_instance.h`, `blueprint_instance.cpp`
     - `Editor/Blueprint/Runtime/Variables/`: `blueprint_value.h`, `blueprint_value.cpp`
     - `Editor/Blueprint/Runtime/ObjectBinding/`: `blueprint_host.h`, `blueprint_host_description.h`, `blueprint_host_description.cpp`, `blueprint_native.h`, `blueprint_native.cpp`
     - `Editor/Blueprint/Nodes/Networking/`: `blueprint_networking_nodes.h`, `blueprint_networking_nodes.cpp`
     - `Editor/Blueprint/Testing/{Compiler,Runtime,Integration}/`: migrated Blueprint compiler, VM, debugger, and integration tests plus native `.uvs`/`.uvhost` fixtures.
   - Tracked architectural placeholder folders deliberately with intent `README.md` files (no fake/empty source stubs):
     - **Populated with real code**: `Compiler` (`Frontend`, `Intermediate`, `Backend`), `Nodes/Networking`, `Runtime` (`VirtualMachine`, `Variables`, `ObjectBinding`), `Testing` (`Compiler`, `Runtime`, `Integration`).
     - **Not yet populated (tracked with intent `README.md` only)**: `Core`, `Editor`, `Serialization`, `Assets`, `Plugins`, `Debug`, `Profiling`, `Resources`, `Platform`.
   - Migrated namespace `UVE::UVScript` -> `UVE::Blueprint` and `UVScriptObjectHostUVE` -> `BlueprintObjectHostUVE` (`Engine/Runtime/Engine/Expose/blueprint_object_host.h`, `Engine/Runtime/Engine/Internal/blueprint_object_host.cpp`).
   - Updated `Engine/Tools/UVBT/uvbt.py` to compile the native codegen tool `uvsc` from `Editor/Blueprint/Compiler/Backend/blueprint_compiler_main.cpp` and `Editor/Blueprint/Testing/Compiler/native/`.
2. **Blueprint Networking Nodes (Phase 6, Commit `5d795d4a`)**:
   - Added 23 Blueprint networking nodes in `Editor/Blueprint/Nodes/Networking/blueprint_networking_nodes.{h,cpp}` (`GetNetworkingBlueprintNodesUVE()`) registered into `Engine/Runtime/Asset/Internal/blueprint_nodes.cpp`:
     - **RPC (11 nodes)**: `net_rpc_begin`, `net_rpc_push_int`, `net_rpc_push_float`, `net_rpc_push_str`, `net_rpc_push_bool`, `net_rpc_push_vec3`, `net_rpc_send`, `net_rpc_cancel`, `net_rpc_result_kind`, `net_rpc_result_int`, `net_rpc_result_str` -> delegate to `Engine/Runtime/Networking/` (`NetworkRpcUVE`).
     - **Lobby (7 nodes)**: `net_lobby_list`, `net_lobby_create`, `net_lobby_join`, `net_lobby_leave`, `net_lobby_room`, `net_lobby_roster_count`, `net_lobby_roster_name` -> delegate to `Engine/Runtime/Networking/` (`NetworkLobbyClientUVE`).
     - **Relay (5 nodes)**: `net_relay_peer_count`, `net_relay_peer_room`, `net_relay_peer_state`, `net_relay_set_room`, `net_relay_set_interest` -> delegate to `Engine/Runtime/Networking/` (`NetworkRelayUVE`).
   - Verified via `BlueprintUVETest.NetworkingNodesDelegateToEngineRuntimeNetworking` in `Engine/Runtime/Gameplay/Tests/blueprint_execution_tests.cpp`.
3. **Co-located Test Architecture (Phase 8, Commit `5635b880`)**:
   - Relocated all 490 test files from top-level `Test/` into module-local `Tests/` directories (`Engine/Runtime/<Module>/Tests/`, `Engine/Editor/EditorCore/Tests/`, `Editor/Blueprint/Testing/`) and shared harness in `Engine/Tests/{CoreTests.Build.cs,Support/}`. Removed top-level `Test/`.
   - Updated `Engine/Tools/UVBT/uvbt.py` so non-test modules automatically exclude `Tests`, `tests`, and `Testing` directories from production source discovery, while `CoreTests.Build.cs` gathers all 44 co-located test directories via `ExtraSourceDirectories`.

---

## 5. Engine Networking Rename and Architectural Boundary

- **Exact rename (Phase 5, Commit `3ae8b36d`)**:
  - Directory renamed via `git mv`: `Engine/Runtime/Network/` -> `Engine/Runtime/Networking/` (37 files).
  - Module file renamed: `Engine/Runtime/Network/Network.Build.cs` -> `Engine/Runtime/Networking/Networking.Build.cs` (`NetworkModule` -> `NetworkingModule`).
  - Consumer build files updated: `Engine/Editor/EditorCore/EditorCore.Build.cs`, `Engine/Runtime/Engine/Engine.Build.cs`, `Engine/Runtime/Objects/3D/Objects3D.Build.cs`, `Engine/Tests/CoreTests.Build.cs` (previously `Test/CoreTests.Build.cs`).
  - Tests relocated in Phase 8: `Test/Network/` -> `Engine/Runtime/Networking/Tests/`.
- **Confirmation**:
  - `Engine/Runtime/Networking/` exists; `Engine/Runtime/Network/` does not exist.
  - Zero networking runtime files were moved into `Editor/` or `Editor/Blueprint/`.
  - `grep` across `Editor/Blueprint/` confirms zero socket/transport/packet implementations (`socket(`, `bind(`, `sendto(`, `recvfrom(`, `sockaddr`, `UdpSocket`, `TcpSocket` = 0 matches). Blueprint networking nodes solely emit calls into `Engine/Runtime/Networking/` APIs through `BlueprintObjectHostUVE`.

---

## 6. Blueprint Runtime Boundary Confirmation

`Editor/Blueprint/Runtime/` contains **exclusively** Blueprint execution infrastructure (9 files total):
- `Editor/Blueprint/Runtime/VirtualMachine/blueprint_instance.h` / `.cpp` (VM bytecode execution, call frames, wait states, debugger hooks)
- `Editor/Blueprint/Runtime/Variables/blueprint_value.h` / `.cpp` (Blueprint typed values, arrays, maps)
- `Editor/Blueprint/Runtime/ObjectBinding/blueprint_host.h`, `blueprint_host_description.h` / `.cpp`, `blueprint_native.h` / `.cpp` (host reflection bridge and native compiled-script registry)

No Engine Runtime subsystem (Networking, Physics, Audio, Animation, Rendering, Input, Navigation, AI, Gameplay) resides in `Editor/Blueprint/Runtime/`.

---

## 7. Renamed Files, Classes, and Editor Systems (Old -> New)

### Phase 4: Editor and Animation System Renames (Commit `f1d27d11`)
| Category | Old | New |
|---|---|---|
| Asset files | `Engine/Runtime/Asset/Expose/animation_montage_asset.h` | `Engine/Runtime/Asset/Expose/motion_montage_asset.h` |
| Asset files | `Engine/Runtime/Asset/Internal/animation_montage_asset.cpp` | `Engine/Runtime/Asset/Internal/motion_montage_asset.cpp` |
| Asset files | `Engine/Runtime/Asset/Expose/animation_montage_editing.h` | `Engine/Runtime/Asset/Expose/motion_montage_editing.h` |
| Asset files | `Engine/Runtime/Asset/Internal/animation_montage_editing.cpp` | `Engine/Runtime/Asset/Internal/motion_montage_editing.cpp` |
| Object files | `Engine/Runtime/Objects/Animation/Expose/animation_montage.h` | `Engine/Runtime/Objects/Animation/Expose/motion_montage.h` |
| Object files | `Engine/Runtime/Objects/Animation/Internal/animation_montage.cpp` | `Engine/Runtime/Objects/Animation/Internal/motion_montage.cpp` |
| Component file | `Engine/Runtime/Component/Expose/montage_player_component.h` | `Engine/Runtime/Component/Expose/montage_sequencer_component.h` |
| Editor files | `Engine/Editor/EditorCore/Expose/editor_content_browser_model.h` | `Engine/Editor/EditorCore/Expose/editor_asset_table_model.h` |
| Editor files | `Engine/Editor/EditorCore/Internal/editor_content_browser_model.cpp` | `Engine/Editor/EditorCore/Internal/editor_asset_table_model.cpp` |
| Editor files | `Engine/Editor/EditorCore/Internal/shell/editor_content_browser.h` | `Engine/Editor/EditorCore/Internal/shell/editor_asset_table.h` |
| Editor files | `Engine/Editor/EditorCore/Internal/shell/editor_content_browser.cpp` | `Engine/Editor/EditorCore/Internal/shell/editor_asset_table.cpp` |
| Test files | `animation_montage_asset_tests.cpp`, `animation_montage_tests.cpp`, `editor_content_browser_model_tests.cpp` | `motion_montage_asset_tests.cpp`, `motion_montage_tests.cpp`, `editor_asset_table_model_tests.cpp` |
| Classes / Types | `AnimationMontageAssetUVE`, `AnimationMontageUVE` | `MotionMontageAssetUVE`, `MotionMontageUVE` |
| Classes / Types | `MontagePlayerComponentUVE`, `MontagePlayerUVE` | `MontageSequencerComponentUVE`, `MontageSequencerUVE` |
| Classes / Types | `EditorContentBrowserUVE`, `ContentBrowserModelUVE` | `EditorAssetTableUVE`, `AssetTableModelUVE` |
| UI Systems & Labels | `Animation Montage` / `Action Clip` | `Motion Montage` (prefix `MM_`, badges `MM` / `MML`) |
| UI Systems & Labels | `Montage Player` / `Action Clip Player` | `Montage Sequencer` |
| UI Systems & Labels | `Content Browser` / `Asset Library` | `Asset Table` |
| Compatibility aliases kept | `"montage_player"` (`scene_object_registry.cpp`), `"MontagePlayerComponentUVE"` -> `"MontageSequencerComponentUVE"` (`scene_serializer.cpp`), `"montagePlayer"` JSON fallback (`AnimationGraphComponentUVE`) | Preserved for existing scene files |

### Phase 5: Networking Module Rename (Commit `3ae8b36d`)
| Old | New |
|---|---|
| `Engine/Runtime/Network/` (37 files) | `Engine/Runtime/Networking/` |
| `Engine/Runtime/Network/Network.Build.cs` (`NetworkModule`) | `Engine/Runtime/Networking/Networking.Build.cs` (`NetworkingModule`) |

### Phase 6: UVScript -> Blueprint Migration (Commit `5d795d4a`)
| Old | New |
|---|---|
| `Engine/Runtime/UVScript/UVScript.Build.cs` | `Editor/Blueprint/Blueprint.Build.cs` |
| `Engine/Runtime/UVScript/Expose/uvscript_ast.h` | `Editor/Blueprint/Compiler/Frontend/blueprint_ast.h` |
| `Engine/Runtime/UVScript/Expose/uvscript_parser.h` / `Internal/uvscript_parser.cpp` | `Editor/Blueprint/Compiler/Frontend/blueprint_parser.{h,cpp}` |
| `Engine/Runtime/UVScript/Internal/uvscript_program.h` | `Editor/Blueprint/Compiler/Intermediate/blueprint_program.h` |
| `Engine/Runtime/UVScript/Expose/uvscript_compiler.h` / `Internal/uvscript_compiler.cpp` | `Editor/Blueprint/Compiler/Backend/blueprint_compiler.{h,cpp}` |
| `Engine/Runtime/UVScript/Expose/uvscript_codegen.h` / `Internal/uvscript_codegen.cpp` | `Editor/Blueprint/Compiler/Backend/blueprint_codegen.{h,cpp}` |
| `Engine/Runtime/UVScript/Tool/uvsc_main.cpp` | `Editor/Blueprint/Compiler/Backend/blueprint_compiler_main.cpp` |
| `Engine/Runtime/UVScript/Expose/uvscript_instance.h` / `Internal/uvscript_instance.cpp` | `Editor/Blueprint/Runtime/VirtualMachine/blueprint_instance.{h,cpp}` |
| `Engine/Runtime/UVScript/Expose/uvscript_value.h` / `Internal/uvscript_value.cpp` | `Editor/Blueprint/Runtime/Variables/blueprint_value.{h,cpp}` |
| `Engine/Runtime/UVScript/Expose/uvscript_host.h` | `Editor/Blueprint/Runtime/ObjectBinding/blueprint_host.h` |
| `Engine/Runtime/UVScript/Expose/uvscript_host_description.h` / `Internal/uvscript_host_description.cpp` | `Editor/Blueprint/Runtime/ObjectBinding/blueprint_host_description.{h,cpp}` |
| `Engine/Runtime/UVScript/Expose/uvscript_native.h` / `Internal/uvscript_native.cpp` | `Editor/Blueprint/Runtime/ObjectBinding/blueprint_native.{h,cpp}` |
| `Engine/Runtime/Engine/Expose/uvscript_object_host.h` / `Internal/uvscript_object_host.cpp` | `Engine/Runtime/Engine/Expose/blueprint_object_host.{h,cpp}` (`BlueprintObjectHostUVE`) |
| `Engine/Editor/EditorCore/Internal/editor_panel_uvscript.cpp` | `Engine/Editor/EditorCore/Internal/editor_panel_blueprint_script.cpp` |
| `Test/Gameplay/uvscript_blueprint_tests.cpp` | `Engine/Runtime/Gameplay/Tests/blueprint_execution_tests.cpp` |
| `namespace UVE::UVScript` | `namespace UVE::Blueprint` |

### Phase 9: Global Naming & Vocabulary Fixes (Commit `576dae72`)
| File | Change | Reason |
|---|---|---|
| `Engine/Editor/EditorCore/Internal/editor_icon_set.h` | `"a asset table"` -> `"an asset table"` (2 lines) | Caught by `check_engine_vocabulary.py` after Phase 4 Asset Table rename |
| `Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_editor.cpp` | `"Spawn from UVScript: ..."` -> `"Spawn from Blueprint: ..."` | Stale UI Help menu string referencing retired UVScript name |
| `Engine/Runtime/Asset/Tests/motion_montage_asset_tests.cpp` | `"uve_animation_montage_round_trip.uvmontage"` -> `"uve_motion_montage_round_trip.uvmontage"` | Stale temp filename from Phase 4 rename |
| `Engine/Tools/check_engine_vocabulary.py` | Removed obsolete `rel.startswith("Test/")` check | Top-level `Test/` removed in Phase 8 |

---

## 8. Restored Graph Names (Phase 3, Commit `b052d173`)

Git history established commit `afbc0738` (*"Editor names: Logic Graph, UI Graph, Motion Graph, Blend Grid, Action Clip, Timeline, Asset Library, Inspector"*, 2026-11-15) and follow-ups (`2e6c1990`, `2d25c98f`, `7405bb2a`, `fbd084e5`) as the commits that renamed the original graph titles. Phase 3 restored all four graph names across 19 files:

| Replaced Temporary Name | Restored Original Name | Details |
|---|---|---|
| `Logic Graph` (`LG_`, badge `LG`) | `Blueprint` / `Blueprint Class` (`BP_`, badge `BP`) | Also restored `PendingLogicGraphUVE` -> `PendingBlueprintCreateUVE`, `CreateLogicGraphOnUVE` -> `CreateBlueprintOnUVE` |
| `UI Graph` (badge `UI`) | `Widget Blueprint` (badge `WBP`) | Restored in editor menus, asset creation, Asset Table badges, and Prism Blueprint graphs |
| `Motion Graph` | `Animation Graph` | Restored in editor panels, menus, tooltips, and `editor_anim_graph_*.cpp` |
| `Blend Grid` | `Blend Space` | Restored in `editor_blend_space_view.cpp`, `animation_graph_editing.cpp`, and `editor_details.cpp` |

Unrelated editor names (`Inspector`, `Timeline`) and `UIGraphEdit*` widget-graph internal identifiers were preserved untouched.

---

## 9. Dead-Code Audit Table (Phase 7)

Audit method: traced all 56 `*.Build.cs` modules, every `.cpp` compilation unit across `Engine/` and `Editor/`, all 54 `SceneObjectDescriptorUVE` entries in `scene_object_registry.cpp`, `EngineCoreUVE` runtime loop registrations, `editor_viewport_2d_backend.h`, and test suites.

| Item | Evidence of Use / Non-Use | Verdict | Action Taken |
|---|---|---|---|
| All 56 `*.Build.cs` modules | Every module is transitively referenced by at least one of the 6 `*.Target.cs` targets (`UniVexEditor`, `UniVexRuntime`, `UniVexPack`, `UniVexAssetWorkload`, `UniVexPrismTests`, `UniVexTests`). | Active | Kept |
| 14 `.cpp` files under `Engine/Runtime/Objects/AI/3D` and `Engine/Runtime/Objects/Animation` | Compiled via `Objects3D.Build.cs` `ExtraSourceDirectories`; referenced by scene registry and runtime systems. | Active | Kept |
| Legacy editor panels / standalone viewport demo (22 files, -3,544 lines) | Already deleted in prior commit `4b23aeb6` (*"Remove dead code: old panels, helpers and the standalone viewport demo"*). | Previously removed | None needed |
| `Sprite2D`, `Camera2D`, `TileMapLayer` (`Engine/Runtime/Objects/2D/`) | Registered in `scene_object_registry.cpp`, executed in runtime rendering/camera loops (`Renderer2D`, `Camera2DSystemUVE`), visualized in editor, unit-tested. | Active | Kept |
| 26 2D object types: `AudioListener2D`, `AudioStreamPlayer2D`, `Bone2D`, `NavigationAgent2D`, `NavigationLink2D`, `NavigationObstacle2D`, `NavigationRegion2D`, `Particles2D`, `Skeleton2D`, `TouchScreenButton`, `CanvasModulate`, `Line2D`, `Polygon2D`, `Marker2D`, `Path2D`, `PathFollow2D`, `Area2D`, `CollisionPolygon2D`, `CollisionShape2D`, `PhysicsBody2D`, `RayCast2D`, `ShapeCast2D`, `Light2D`, `LightOccluder2D`, `MeshInstance2D`, `AnimatedSprite2D`, `Parallax2D`, `VisibleOnScreenNotifier2D` | Compiled in `Objects2D`, registered in `scene_object_registry.cpp` and `editor_viewport_2d_backend.h` (visible/selectable in the 2D editor viewport), serialized in scenes, and unit-tested in `Engine/Runtime/Objects/2D/Tests/`, but have **no runtime simulation/execution loop wired in `EngineCoreUVE`** beyond component storage and editor preview drawing. | **Editor-only with no runtime function** | **Kept and flagged** (not deleted per Phase 7 rules; serialization and unit tests depend on them) |
| 4 2D object helpers: `Joint2D`, `RemoteTransform2D`, `BackBufferCopy`, `CanvasGroup` | Compiled into `Objects2D`, exported via `all_objects_2d.h`, and exercised by `Engine/Runtime/Objects/2D/Tests/`. | **Indirectly used** | Kept |
| Standalone `"2DS"` subsystem | Does not exist in source tree or Git history (see Section 10). | N/A | Nothing deleted |

**Summary of removals in Phase 7**: 0 code files deleted (no verified-dead unreferenced runtime modules or orphan `.cpp` files remained after commit `4b23aeb6`).

---

## 10. 2DS Findings and Remaining Uncertainty

- Searched the entire current repository and full Git history (`git log --all` paths and commit messages) for `2DS` / `2ds`.
- **Finding**: No standalone `2DS` subsystem, module, file format, or asset type exists or ever existed in Git history.
- Every case-insensitive match for `2ds` is a substring across a word boundary of active 2D identifiers:
  - `Camera2DSystemUVE` (`2D` + `System`)
  - `Path2DSampledPoseUVE` (`2D` + `Sampled`)
  - `Collider2DSnapshotUVE` (`2D` + `Snapshot`)
  - `Sprite2DSize*` / `Sprite2DSource*` (`2D` + `Size`/`Source`)
  - `viewport2DShow*` (`2D` + `Show`)
  - `kBasic2DSource` (`2D` + `Source`)
- **Uncertainty**: If "2DS" was meant to refer to the 26 editor-only 2D scene objects listed in Section 9 (or a planned/external 2D subsystem never merged into this repository), deleting them without explicit scope confirmation would break `Objects2D`, scene serialization, and unit tests. Per Phase 7 rules, **nothing was deleted** and the uncertainty is recorded here.

---

## 11. Tests Relocated (Phase 8, Commit `5635b880`)

- **Files moved**: 490 tracked files moved via `git mv` from top-level `Test/` into co-located `Tests/` folders inside their owning modules:
  - 43 runtime modules under `Engine/Runtime/<Module>/Tests/` (including `Core/*/Tests`, `Objects/*/Tests`, `RHI/*/Tests`, `Networking/Tests`, `VFX/Tests`, `Retarget/Tests`, etc.)
  - `Engine/Editor/EditorCore/Tests/` (preserved pre-migration exclusion from `CoreTests`, matching the prior `ExcludedFiles` `"Editor/"` entry)
  - `Editor/Blueprint/Testing/{Compiler,Runtime,Integration}/` (including `Compiler/native/` `.uvs` / `.uvhost` fixtures)
  - `Engine/Tests/CoreTests.Build.cs` and `Engine/Tests/Support/{test_image_fixtures.h,test_scratch.h,test_scratch.cpp}`
- **Deleted**: Top-level `Test/` directory (including `Test/CoreTests.Build.cs` and empty stub files `Test/Object/README.md`, `Test/Renderer/README.md`).
- **Fixture path updates**:
  - `Engine/Runtime/Audio/Tests/compressed_audio_importer_tests.cpp`: `"Test/Audio/Fixtures"` -> `"Engine/Runtime/Audio/Tests/Fixtures"`.
  - `Engine/Tools/UVBT/uvbt.py`: plugin fixture build path -> `Engine/Runtime/Plugins/Tests/Fixtures/`; native codegen `.uvs` path -> `Editor/Blueprint/Testing/Compiler/native/`; production modules skip `Tests`/`tests`/`Testing`.
  - `Engine/Tests/CoreTests.Build.cs`: added `../Runtime/Retarget/Tests` to `PrivateIncludePaths` and all 44 co-located directories to `ExtraSourceDirectories`.
- **Test count verification (`UniVexTests --gtest_list_tests | grep -c "^  "`)**:
  - **Before relocation (baseline at `5be1902a`)**: `5,464` tests
  - **After Phase 6 (+1 new `BlueprintUVETest.NetworkingNodesDelegateToEngineRuntimeNetworking`)**: `5,465` tests
  - **After Phase 8 test relocation (`5635b880`)**: **`5,465` tests** (exact match — zero tests lost)
  - **`uve_prism_tests` checks**: `13,436 checks, 0 failed` before and after.

---

## 12. Build Results, Test Results, and Verification Checklist

### Full Builds via UVBT (`Linux Release`, all 6 targets)
| Command | Modules / Sources | Result |
|---|---|---|
| `./uvbt build UniVexEditor Linux Release -j2` | 49 modules, 661 production sources (0 test files) | **BUILD SUCCESSFUL** -> `.uvbt/bin/Linux/Release/uve_editor` |
| `./uvbt build UniVexTests Linux Release -j2` | 50 modules, 1,087 sources (444 in `CoreTests`) | **BUILD SUCCESSFUL** -> `.uvbt/bin/Linux/Release/UniVexTests` |
| `./uvbt build UniVexPrismTests Linux Release -j2` | 2 modules, 53 sources | **BUILD SUCCESSFUL** -> `.uvbt/bin/Linux/Release/uve_prism_tests` |
| `./uvbt build UniVexRuntime Linux Release -j2` | 44 modules, 491 sources | **BUILD SUCCESSFUL** -> `.uvbt/bin/Linux/Release/uve_runtime` |
| `./uvbt build UniVexPack Linux Release -j2` | 44 modules, 491 sources | **BUILD SUCCESSFUL** -> `.uvbt/bin/Linux/Release/uve_pack` |
| `./uvbt build UniVexAssetWorkload Linux Release -j2` | 44 modules, 491 sources | **BUILD SUCCESSFUL** -> `.uvbt/bin/Linux/Release/uve_asset_workload` |

### Static Guards
| Command | Result |
|---|---|
| `bash Engine/Tools/check_panel_includes.sh` | **PASS** (`include audit: clean`) |
| `python3 Engine/Tools/check_math_boundary.py` | **PASS** (`math boundary check passed`) |
| `timeout 300 python3 Engine/Tools/check_engine_vocabulary.py` | **PASS** (`42 retired names and 7 retired name stems enforced, 0 reintroductions`) |

### Test Execution Results
| Command | Total | Passed | Skipped | Failed | Notes |
|---|---|---|---|---|---|
| `./.uvbt/bin/Linux/Release/uve_prism_tests Engine/Runtime/Prism/assets` | 13,436 checks | 13,436 | 0 | 0 | All Prism UI & Blueprint graph checks passed |
| `xvfb-run -a ./.uvbt/bin/Linux/Release/UniVexTests --gtest_filter="*Vfx*:*Particle*:*Blueprint*:*Networking*:*Network*:*Montage*:*AssetTable*"` | 409 | 408 | 1 | 0 | 1 skipped: `ComputeWorkloadsVulkanUVETest.ParticleSimulation_InitializesAndMatchesTheCpuBitForBit` (no Vulkan headless ICD on host) |
| `xvfb-run -a ./.uvbt/bin/Linux/Release/UniVexTests --gtest_filter="*Vfx*:*Particle*"` | 120 | 119 | 1 | 0 | Includes `VfxGraphAuthorityUVETest` (11/11), `VfxEditorUVETest`, `VfxInstanceUVETest`, `VfxWorldUVETest` |
| `./.uvbt/bin/Linux/Release/UniVexTests --gtest_filter="*CompressedAudio*:*NativePluginHost*:*UVScriptNative*:*UVScriptDebugger*:*Retarget*:*BlueprintUVETest*:*VfxGraphAuthority*"` | 134 | 133 | 1 | 0 | 1 skipped: `RetargetRealRigUVETest` (requires `UVE_RETARGET_MODEL` env var); verifies relocated audio/plugin/native-codegen fixtures |
| `DISPLAY=:99 ./.uvbt/bin/Linux/Release/UniVexTests --gtest_brief=1` (full suite) | 5,465 | 5,207 | 253 | 5 | Exact match to pre-refactor `5be1902a` baseline failures (all 5 are Release-build death/assert tests):<br>1. `EngineCoreUVETest.LoopStages_ExecuteInDocumentedOrder`<br>2. `EngineCoreUVETest.Renderer3D_ReachableAfterInit_NoActiveCameraStillNoOps`<br>3. `EntityEnabledUVETest.StaleHandle_AssertsInDebugAndFailsClosedInRelease`<br>4. `Camera2DSystemUVETest.ComputeProjectionMatrixUVE_InvalidViewportSize_Asserts`<br>5. `Camera2DSystemUVETest.ComputeProjectionMatrixUVE_InvalidCameraParameters_Asserts` |

### Phase 10 Verification Checklist
- [x] Full build of all affected targets through `./uvbt` (6/6 targets built cleanly)
- [x] Test counts before (`5,464`) and after (`5,465` = `5,464` + 1 new Blueprint networking test) verified
- [x] VFX node creation, connections, wire removal, node removal, value change, template evaluation (`VfxGraphAuthorityUVETest` 11/11 passed; `*Vfx*:*Particle*` 119/120 passed, 1 Vulkan ICD skip)
- [x] Stale naming grep verification across `Engine/` and `Editor/`:
  - `Animation Montage` / `Content Browser` / `content_browser` / `animation_montage`: **0 hits**
  - `Montage Player` / `montage_player`: **1 justified hit** (`Engine/Runtime/Scene/Internal/objects/scene_object_registry.cpp:159` legacy scene load alias `if (typeId == "montage_player")`)
  - `Runtime/Network/`: **0 hits**
  - `UVScript`: **remaining hits justified** — preserved only for `.uvs` script file format compatibility (`kUVScriptExtensionUVE`, `OpenUVScript*`, `ParseUVScriptUVE`, `CompileUVScriptSourceUVE`, `TranslateBlueprintToUVScriptUVE`), `UVE_UVSCRIPT_NATIVE_DIR` macro, legacy `.uvs` file extension migration, and pre-existing `uvscript_*_tests.cpp` test suite filenames/names so existing `.uvs` assets and test filters keep working.
- [x] Graph names restored and consistent (`Blueprint`, `Widget Blueprint`, `Animation Graph`, `Blend Space`)
- [x] `Editor/Blueprint/` matches required tree (populated modules with real code; unpopulated future folders tracked explicitly via `README.md`)
- [x] `Engine/Runtime/Networking/` exists; `Engine/Runtime/Network/` does not
- [x] Blueprint networking nodes (`Editor/Blueprint/Nodes/Networking/`) call `Engine/Runtime/Networking/` APIs
- [x] No networking implementation duplicated in Blueprint
- [x] `Editor/Blueprint/Runtime/` contains only Blueprint execution infrastructure
- [x] No accidental deletions (`git diff -M --diff-filter=D --name-only 5be1902a..HEAD` shows only the 5 intentionally replaced files: `Engine/Runtime/UVScript/{UVScript.Build.cs,README.md}`, `Test/{CoreTests.Build.cs,Object/README.md,Renderer/README.md}`)
- [x] Migrated tests all execute

---

## 13. Remaining Issues, Unverified Items, and Incomplete Work

1. **Interactive GUI clicking / visual inspection**: Editor binaries (`uve_editor`) build cleanly and headless API/editor tests pass, including VFX graph authority tests under `xvfb-run`, but manual interactive GUI clicks (palette search/context menu visual layout, docking appearance) were not visually inspected on a live desktop display.
2. **Vulkan headless compute test**: `ComputeWorkloadsVulkanUVETest.ParticleSimulation_InitializesAndMatchesTheCpuBitForBit` skipped because this Linux VM has no Vulkan headless ICD (`VK_EXT_headless_surface` / SwiftShader / lavapipe).
3. **Full suite under `xvfb-run` display**: Running all 5,465 tests simultaneously under `xvfb-run` triggered a VM restart mid-run; display-backed verification was therefore run on the targeted 409-test slice (`*Vfx*:*Particle*:*Blueprint*:*Networking*:*Network*:*Montage*:*AssetTable*`, 408 passed / 1 Vulkan skip / 0 failed) while the full 5,465-test suite completed cleanly with `DISPLAY=:99` (253 display/GPU skips, only the 5 pre-existing Release-assert baseline failures).
4. **5 pre-existing Release-build failures**: The 5 failures listed in Section 12 exist identically on the starting commit `5be1902a` in `Linux Release` (`NDEBUG` disables debug asserts expected by those death tests).
5. **EditorCore unit tests (`Engine/Editor/EditorCore/Tests/`)**: Matching the pre-migration `Test/CoreTests.Build.cs` exclusion (`ExcludedFiles: "Editor/"`), these tests were relocated next to `EditorCore` and remain excluded from `CoreTests` so test scope matches baseline. Enabling them requires adding `../Editor/EditorCore/Internal` to a dedicated `EditorTests` target/module.
6. **Unpopulated `Editor/Blueprint/` folders**: `Core`, `Editor`, `Serialization`, `Assets`, `Plugins`, `Debug`, `Profiling`, `Resources`, `Platform` contain intent `README.md` files only; existing Blueprint UI shell (`Engine/Editor/EditorCore/Internal/shell/blueprint/`), Prism graph widget (`Engine/Runtime/Prism/`), and asset definitions (`Engine/Runtime/Asset/`) remain in place to avoid destabilizing working editor features.
7. **2DS clarification**: Confirm whether the 26 editor-only 2D objects flagged in Section 9 should receive runtime simulation systems or be removed in a dedicated follow-up.
8. **Legacy `.uvs` API/test names**: Internal `.uvs` text-script helpers (`ParseUVScriptUVE`, `CompileUVScriptSourceUVE`, `uvscript_*_tests.cpp`) were intentionally retained for `.uvs` backward compatibility.

---

## 14. Commit List per Phase

Branch: `fix/vfx-blueprint-audit` (based on `feature/vfx-particle-graph` at `5be1902a`)

| Phase | Commit | Summary |
|---|---|---|
| Phase 2 (base on `feature/vfx-particle-graph`) | `5be1902a` | VFX graph authority: chain-driven extraction, detached modules, node library, spawner, pin rules, and stage node lock |
| Phase 3 | `b052d173` | Restore original graph names: Blueprint, Widget Blueprint, Animation Graph, and Blend Space |
| Phase 4 | `f1d27d11` | Rename Animation Montage to Motion Montage, Montage Player to Montage Sequencer, and Content Browser to Asset Table |
| Phase 5 | `3ae8b36d` | Rename Engine/Runtime/Network to Engine/Runtime/Networking |
| Phase 6 | `5d795d4a` | Replace Engine/Runtime/UVScript with Editor/Blueprint architecture and Blueprint Networking nodes |
| Phase 7 | *(audit only — no deletions required)* | Verified all 56 modules and all `.cpp` units active; documented 26 editor-only 2D objects and 2DS findings in Section 9 and Section 10 |
| Phase 8 | `5635b880` | Move tests next to engine and Blueprint sources and remove top-level Test directory |
| Phase 9 | `576dae72` | Fix remaining naming and vocabulary audit findings |
| Phase 10 | *(report commit on HEAD)* | Write final `AUDIT_REPORT.md` with full verification results |
