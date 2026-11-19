# UniVex Engine - Audit Report (in progress: Phases 1-2 complete)

Status of this report: **partial**. Phase 1 (discovery) and Phase 2 (VFX graph fixes) are done. Phases 3-10 are not started; see "Open decisions".

## 1. Source and branch
- Input was a source archive named `UniVex_Engine-feature-vfx-particle-graph.zip` (2,577 files). It contains **no `.git` directory**, so branch listing, `git log`, authorship, dates and rename history could not be inspected. Whether `feature/vfx-particle-graph` is the newest/only implementation is **unverified**.
- No commits were made and no git configuration was changed. The sandbox's global git identity is not the repository owner, so commits would have carried the wrong author; changes are delivered as a patch instead (`phase2_vfx_graph.patch`, made with `git diff --no-index` against the untouched archive contents). Apply it on your branch and commit it as yourself.

## 2. Repository facts that differ from the task's assumptions
| Assumed | Found |
|---|---|
| `UVBT.cs` | `Engine/Tools/UVBT/uvbt.py` + `UVBT.csproj`; modules/targets are `*.Build.cs` / `*.Target.cs` (56 modules, 6 targets: UniVexEditor, UniVexRuntime, UniVexPack, UniVexAssetWorkload, UniVexPrismTests, UniVexTests) |
| top-level `Editor/` | `Engine/Editor/{EditorApp,EditorCore,Viewport}` |
| separate test dir | `Test/` (module `CoreTests`, `Test/CoreTests.Build.cs`), built by target `UniVexTests` |
| `Engine/Runtime/Network/` | exists (37 files); `Test/Network/` also exists |
| UVScript | `Engine/Runtime/UVScript/` (28 files, ~8.3k lines), `Test/UVScript/`, `Test/Script/`; build tool `uvsc` is compiled by `uvbt.py` |
| Blueprint | already present: `Engine/Runtime/Prism/*Blueprint*` (graph widget/VM), `Engine/Runtime/Asset/*blueprint*`, `Engine/Editor/EditorCore/Internal/shell/blueprint/` (~13.6k lines). The UI label "Logic Graph" is used for the code type `Blueprint` (`editor_content_browser.cpp:244,346,1477`, `editor.cpp:5683`). |
| "2DS" | Only matches are substrings of unrelated names (`Camera2DSystemUVE`, `Path2DSampledPoseUVE`, `Collider2DSnapshotUVE`, `kBasic2DSource`). No standalone "2DS" system found. Nothing to remove. |

## 3. VFX: original problems and root causes
**A. The graph did not drive behavior.**
1. `ExtractEmitterFromGraphUVE` (`Engine/Editor/EditorCore/Internal/shell/vfx/editor_vfx_graph.cpp`) rebuilt each stage by walking the chain wires back from the stage output node, then **appended every module not on the chain** ("so freshly spawned nodes are never lost"). Chain wires were therefore decorative: deleting all of them gave the identical module set, hence identical compiled program.
2. Removing a Math/Value wire from a float/vector input switched the local value back to Constant but only stored it when the pin text differed, so the asset kept `kind = Wire` pointing at the Math node (same function, float and vector branches).
3. Editor policy (unchanged, by design and covered by an existing test): on a compile error the preview keeps the last good program (`editor_vfx_window.cpp` ~205-250). An emitter whose Render stage has no reachable renderer is a compile error, so the live preview holds its previous frame while the status bar/Render node show the error. See open decision 4.

The compiler (`vfx_program.cpp`) and runtime consume the stage lists faithfully; the bypass was entirely in the graph-to-asset layer. Templates (`vfx_templates.cpp`) are plain asset data (modules per stage); in the graph they are real nodes plus one chain wire per module.

**B. Nodes could not be added.** `BuildVfxGraphNodeLibraryUVE` had no caller; the VFX editor never called `setNodeLibrary` or set `onSpawn`, so the palette offered the generic Blueprint library. Those nodes have no `vfx_module:` type key and the extractor silently dropped them. Related: the Delete key calls `BlueprintGraph::removeSelected()` directly (not `onNodeAction`), so the four fixed stage nodes could be deleted from the keyboard.

## 4. VFX fixes implemented (patch `phase2_vfx_graph.patch`, 9 files, +630/-40)
- `particle_system_asset.h/.cpp`: new `ParticleEmitterAssetUVE::detached` (modules present in the graph but not wired into a stage chain). Saved as `"detached"`, loaded, validated (any stage, cap 256, same per-module checks). Older files without the key still load. The compiler never reads it.
- `editor_vfx_graph.cpp/.h`: extraction now makes the chain the only source of a stage's modules; everything else goes to `detached`. Detached modules are rebuilt as unconnected nodes. Wire removal now reverts inputs to Constant in the asset. New `SpawnVfxGraphNodeUVE` (collision-free ids, never a stage id) and `CanConnectVfxPinsUVE` (chains stay inside one stage, one outgoing chain link per module, Math results only into Float module inputs).
- `editor_vfx_editor.cpp`: installs the VFX node library (modules by stage + 21 Math/Value nodes), spawner, connection hook and stage-node lock on the graph; id scans for copy/duplicate/new-emitter and user-parameter rename include detached modules.
- `editor_vfx_inspector.cpp`: module lookup includes detached modules, so a dangling node can be selected and edited.
- `Prism/BlueprintGraph.h/.cpp`: generic `nodeLocked` predicate (blocks delete, cut, copy, duplicate of locked nodes). Default behavior unchanged when unset.
- Behavior change to know about: a node that is not wired into its stage no longer runs. New nodes from the palette start unconnected and do nothing until wired.
- Problem C (editor layout): the palette now has organised categories (Emitter Update / Particle Spawn / Particle Update / Render / Math / Value) with search keywords, and detached nodes get their own row. No further visual redesign was done; the GUI could not be run or looked at here, so layout/appearance changes are **not done / unverified**.

## 5. Verification actually run
Full engine build: **not possible here.** Baseline (untouched) `./uvbt.py build UniVexTests Linux Debug` stops at missing system dev packages (`GLFW/glfw3.h`, `jpeglib.h`; also `-lglfw -lGLEW -lGL` at link). So UniVexTests/UVBT test counts before/after could not be produced.

What was run instead (a throwaway harness outside the repo: a temporary module + target, libjpeg dependency removed, a stub for `DecodeJpegRgba8ImageUVE`, link done by hand without glfw/GLEW; modules compiled by UVBT: Platform, Logging, Math, Asset, VFX, Prism and dependencies):
- 173 translation units compiled with no errors, including every changed file.
- `./UniVexVerify`: **27 passed, 0 failed**: 11 new `VfxGraphAuthorityUVETest.*` tests, plus the existing `particle_system_asset_tests`, `vfx_program_tests`, `vfx_values_tests`, `vfx_shapes_tests` and the existing template graph round-trip test (`BuildThenExtract_RoundTripsEveryTemplateEmitter`).
- The new tests first exposed bug A.2 (one failed, then passed after the fix).
- Not run: the remaining `Test/VFX` tests that need the engine core (`vfx_editor_tests` session tests, `vfx_instance_tests`, `vfx_world_tests`), the rest of the suite, and any GUI interaction (palette/context menu/search clicks). The palette/context-menu/search paths are verified only at the API level (library, spawner, `connect`), not by clicking.

New tests (`Test/VFX/vfx_graph_authority_tests.cpp`): templates are real node graphs; removing every chain wire leaves no compiled ops and no program; cutting one wire drops exactly the upstream modules; removing a node removes it everywhere; changing a pin value changes the compiled constant (120 -> 321); every library node can be spawned with a distinct id; a new node is detached and inert until connected, then adds exactly one op; Math wire drives an input and removing it restores Constant; cross-stage/unwirable connections are rejected; stage nodes cannot be deleted; detached modules survive save/load and graph rebuild.

## 6. Remaining / open decisions (nothing below has been started)
1. **Git**: send a clone/bundle (or confirm the branch) to do Phase 1 branch checks, `git mv` renames, history-based Phase 3, and per-phase commits under your identity. Without history, "restore the original Logic Graph name" cannot be determined (the code name is `Blueprint`, the label is "Logic Graph"; which one is original is unknown).
2. Build environment: `libglfw`, `libGLEW`, `libGL`, libjpeg dev packages are needed for a full build and test run here (package installs are disabled in this session).
3. Blueprint target path: the task says `Editor/Blueprint/`; the repo's editor lives at `Engine/Editor/`. Confirm the intended root, and how existing Blueprint code (Prism graph/VM, Asset blueprint nodes, EditorCore blueprint shell, 13.6k lines) and UVScript (8.3k lines + tests + `uvsc`) should migrate. Phase 6 as written (about 200 folders of real code with compiler/VM/serialization) is a multi-week design, not a rename.
4. Preview policy: keep "last good program on compile error" (current, tested) or show an empty preview when a graph has no reachable renderer? Currently the compile result and error badge reflect the graph, but the preview stays on the last good frame.
5. Module naming for Phase 5: rename only the directory `Engine/Runtime/Network/` -> `Networking/`, or also the `Network` module name referenced by `*.Build.cs` files (`CoreTests.Build.cs` lists `"Network"`)?
