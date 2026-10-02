# Naming Plan — making the object set UniVex's own

**Date:** 2026-10-02
**Status:** decisions taken (see the log below); Phase 1 applied, Phase 2 open (narrowed to 6 objects).
**Companion:** `NODE_NAMING_AUDIT.md` (what the names are today, where they live, what is broken).

## Decision log

| Decision | Choice | Date |
|---|---|---|
| Folder target | **One `Objects/` tree** — `Engine/Runtime/Nodes` → `Objects/`, `Object` → `Objects/Core` (so no `Object` + `Objects` pair exists), `CanvasLayer` → `Objects/UI` | 2026-10-02 |
| ~~Name strategy~~ | ~~Strict: all 47 objects get UniVex names~~ — **withdrawn.** The invented set read as forced (Object3D, Eye3D, Lamp3D, Stamp3D …) and is dropped in full | 2026-10-02 |
| Name strategy (final) | **Keep every existing object name** except two small families: the four **`*Body3D`** physics kinds (the word "Body" goes) and the two **`Animation*`** kinds (the word "Animation" stays, `Player`/`Tree` go) | 2026-10-02 |
| The six new names | `Static3D`, `Rigid3D`, `Character3D`, `Kinematic3D` (short, direct — chosen from three options) and `AnimationSequencer`, `AnimationGraph` | 2026-10-02 |
| Phase 2 (the six renames) | **Applied** — registry rows + legacy ids, `defaultName`s, C++ enum/definition names, file names, icon pairs, content catalogue, inspector labels, tests and docs | 2026-10-02 |
| Everything else | `Camera3D`, `Light3D`, `DirectionalLight3D`, `WorldEnvironment3D`, `ReflectionProbe3D`, `Decal3D`, `FogVolume3D`, `MeshInstance3D`, `BoxMesh3D`, `SphereMesh3D`, `PlaneMesh3D`, `ParticleEmitter3D`, `AudioSource3D`, `Area3D`, `RayCast3D`, `SpringArm3D`, `Collider3D`, `Hitbox3D`, `Hurtbox3D`, `Projectile3D`, `InteractionArea3D`, `LODGroup3D`, `Occluder3D`, `VisibilityRegion3D`, `SpawnPoint3D`, `LevelStreamer3D`, `WorldPartition3D`, `NavigationRegion3D`, `NavigationAgent3D`, `Skeleton3D`, `BoneAttachment3D`, `Marker3D`, `SceneRoot`, `Folder`, `Viewport`, `Script`, `Canvas`, `UIText`, `UIImage`, `UIButton`, `Node3D` — **unchanged** | 2026-10-02 |
| Phase 1 (folders) | **Applied** — 110 runtime files + 5 test files moved, 20 files' path references updated, no include line changed | 2026-10-02 |

### Phase 1 — what actually moved

| Was | Now |
|---|---|
| `Engine/Runtime/Nodes/` | `Engine/Runtime/Objects/` |
| `Engine/Runtime/Nodes/3D/` (78 files) | `Engine/Runtime/Objects/3D/` |
| `Engine/Runtime/Nodes/Animation/` | `Engine/Runtime/Objects/Animation/` |
| `Engine/Runtime/Nodes/AI/3D/` | `Engine/Runtime/Objects/AI/3D/` |
| `Engine/Runtime/Nodes/CanvasLayer/` | `Engine/Runtime/Objects/UI/` |
| `Engine/Runtime/Object/` | `Engine/Runtime/Objects/Core/` |
| `Test/Nodes/` | `Test/Objects/` (`CanvasLayer/` → `UI/`) |

Include strings (`uve/nodes/3d/...`, `uve/object/...`) are unchanged on purpose: they resolve through
each library's `Expose/` directory, so Phase 1 could not break a single `#include`. That property is
what made it safe to land first — and it is verified: all 50 `.cpp` under `Engine/Runtime/Objects/**`
plus 50 under `Scene`/`Component`/`Animation` compile clean with `g++ -std=c++23 -fsyntax-only`
(the sandbox has no GLFW/GL, so the full CMake build and the 2,519 tests remain a CI job).

**Goal:** the engine should stand on its own feet — its folder tree, its module names, and the
names a user sees on a placeable object should be UniVex's, not another engine's. The engine keeps
working exactly as it does now; this is a rename plan, not a rewrite.

**Not legal advice.** Godot is MIT-licensed and class *names* are generally not the licensed
material. But the user's goal is identity, not just licensing: an engine that ships with
`SpringArm3D`, `WorldEnvironment`, `AnimationTree` and `RayCast3D` in its Add-Object list reads as a
Godot derivative whatever the licence says. The plan below removes that reading.

---

## 1. What is borrowed today (the size of the job)

| Surface | Today | Change needed |
|---|---|---|
| Module folder | `Engine/Runtime/Nodes/` — 110 files (3D 78, Animation 13, CanvasLayer 11, AI 7, 2D 1) | ✅ **done** — moved to `Objects/` |
| Second module | `Engine/Runtime/Object/` — 5 files (the object model itself) | ✅ **done** — moved to `Objects/Core/` |
| Test folder | `Test/Nodes/` — 5 test files | ✅ **done** — moved to `Test/Objects/` |
| Registry folder | `Engine/Runtime/Scene/.../nodes/`, `Engine/Runtime/Scene/Internal/nodes/` | ✅ **done** — path references updated |
| Virtual include path | `uve/nodes/...` — **239** include lines across **110** files | optional Phase 3 (make includes match the `Objects/` folder) |
| The word "Node" in code | **2,734** occurrences, **238** distinct identifiers, **217** files | **dropped** — not requested |
| User-visible object names | 47 labels + 47 default names + 47 `typeId`s + 47 icon files | **only 6 change** — Section 4 |
| Docs / comments | 31 path references to `Runtime/Nodes`, plus `SCENE_NODES_ROADMAP.md` (285 lines), `STUB_IMPLEMENTATION_ROADMAP.md`, `AUDIT.md` | follow the renames |

Phases are ordered so that each one is a small, reviewable change that builds and passes on its own.

---

## 2. The module tree

### Option A — rename only (smallest, recommended first step)

```
Engine/Runtime/Objects/          <- was Engine/Runtime/Nodes/
    CMakeLists (new, optional aggregate)
    3D/            (78 files: Internal/, Expose/uve/objects/3d/, Physics/)
    2D/            (placeholder, future)
    AI/3D/
    Animation/
    UI/            <- was CanvasLayer/  (CanvasLayer is itself a borrowed name)
Engine/Runtime/Object/           <- unchanged: the object model (ObjectUVE, TypeMetadataUVE)
Engine/Runtime/Objects/          <- the placeable objects built on it
```

Cost: `git mv` of one folder, 2 `add_subdirectory` lines in the root `CMakeLists.txt`, the relative
paths inside `Objects/3D/CMakeLists.txt` stay valid, 31 doc/comment paths updated.
**Include lines do not change in this option** — they resolve through each library's `Expose/` dir,
so `#include "uve/nodes/3d/camera_3d_uve.h"` keeps working while the physical folder is renamed.
That makes Option A safe with zero compile risk; the virtual path is renamed separately in Phase 3.

### Option B — one tree (what the request literally described: "ilagay na lang sa object folder")

```
Engine/Runtime/Objects/
    Core/          <- was Engine/Runtime/Object/ (object model, type metadata, object registry)
    Common/        <- was Nodes/3D/Internal/node_3d_common_uve.*
    3D/  2D/  AI/  Animation/  UI/
```

Cost: Option A **plus** moving `Engine/Runtime/Object` into `Objects/Core` and fixing every include
that points at `uve/object/...`. Bigger diff, but afterwards there is exactly one place where
authorable things live — and the singular/plural pair `Object/` + `Objects/` never exists.

### Also worth renaming in the same sweep

| Old | New | Why |
|---|---|---|
| `Engine/Runtime/Nodes/CanvasLayer/` | `Engine/Runtime/Objects/UI/` | `CanvasLayer` is a borrowed class name |
| `Engine/Runtime/Nodes/AI/3D/` | `Engine/Runtime/Objects/AI/3D/` | fine as-is, just moves |
| `uv_s`/`Nodes` in docs and comments | `Objects` | keeps docs true |

---

## 3. Naming rules (final, small)

1. **Existing names stay.** Ordinary engine vocabulary is fine: the test of a name is not "did
   another engine use this word", it is "does a game developer read it correctly at a glance".
   `MeshInstance3D`, `Camera3D`, `Light3D`, `Hitbox3D` all pass that test, so they do not move.
2. **Two exceptions only.** The word `Body` in the four physics kinds (it reads as anatomy, not
   physics) and the `Player`/`Tree` pair in the two animation kinds (the word `Animation` stays).
   New strings must be plain words that already exist in the field — nothing invented.
3. **One string per object.** The placeable label and the default Outliner name are the *same
   string*. Today 16 kinds break this badly (a new `Hitbox3D` is called "Node3D") and 6 break it
   mildly — Phase 0 fixes the label/default-name drift for every kind, renamed or not.
4. **`typeId` = snake_case of the label**, icon file = `typeId`, and the icon follows every rename.
5. **Locked by a test.** A registry invariant test (every kind: unique `typeId`, icon exists,
   `label == defaultName`) so the five layers can never drift again.

---

## 4. The rename set (final scope: 6 objects)

Everything not in this table keeps the name it has today. Two families change, and the new strings
must be plain engine words — no invented vocabulary.

### 4a. The `Body` word

| # | Was | Now | `typeId` |
|---|---|---|---|
| 1 | `StaticBody3D` | **`Static3D`** | `static_3d` |
| 2 | `RigidBody3D` | **`Rigid3D`** | `rigid_3d` |
| 3 | `CharacterBody3D` | **`Character3D`** | `character_3d` |
| 4 | `AnimatableBody3D` | **`Kinematic3D`** | `kinematic_3d` |

What these four actually are in this engine (so the new word describes them correctly):

| Kind | What it attaches | What it does at runtime |
|---|---|---|
| `StaticBody3D` | `ColliderComponentUVE` only | never moves; world geometry |
| `RigidBody3D` | `RigidBodyComponentUVE` | gravity + collision response |
| `CharacterBody3D` | `ColliderComponentUVE` + `CharacterControllerComponentUVE` | kinematic move/jump/ground |
| `AnimatableBody3D` | `ColliderComponentUVE` + `RigidBodyComponentUVE` + `AnimatableBody3DNodeComponentUVE` | moved by animation/script, pushes others |

The engine's own internal vocabulary for them is already `PhysicsObject3DNodeDefinitionUVE` (all
four) and `SolidBody3DNodeDefinitionUVE` (static + animatable) — so a "Solid" family would match the
code that exists.

### 4b. The `Animation` kinds

| # | Was | Now | `typeId` |
|---|---|---|---|
| 5 | `AnimationPlayer` | **`AnimationSequencer`** | `animation_sequencer` |
| 6 | `AnimationTree` | **`AnimationGraph`** | `animation_graph` |

What they are: both are pure nodes (no transform, no visibility). `AnimationPlayer` plays one clip
onto a target (or its parent); `AnimationTree` evaluates an animation graph (blends, layers, a state
machine, root motion) onto the same targets. The graph word is the natural replacement for "Tree".

### What a rename touches (measured, for the 6 objects)

| Touch point | Where | Count |
|---|---|---|
| Saved `typeId` + label | `Engine/Runtime/Scene/Internal/nodes/scene_node_registry_uve.cpp` (rows 55, 62, 70, 73, 77-78, 81) | 6 rows + 6 legacy aliases |
| Legacy alias block | same file, `FindSceneNodeDescriptorUVE(std::string_view)` | 1 spot |
| Default Outliner name | the 6 per-kind headers (`defaultName`) | 6 lines |
| Icons | `assets/icons/nodes/{static_body,rigid_body,character_body,animatable_body,animation_player,animation_tree}_3d?.{png,svg}` | 6 icon-file pairs (rename; filename **is** the lookup key) |
| C++ kind enum | `scene_node_registry_uve.h` (`SceneNodeKindUVE::StaticBody3D` …) | 6 constants |
| Definition structs | `*NodeDefinitionUVE` in the 6 headers | 6 structs |
| Content Browser templates | `editor_content_catalogue_uve.cpp:66, 105-108` (`"AnimationPlayer"`, `"AnimationTree"`, the Character description) | 4 lines |
| Inspector component label | `scene_component_metadata_uve.cpp` (`"CharacterBody3D"` under `component.character_controller`) | 1 line |
| Tests asserting the strings | `Test/Objects/3D/node_definitions_3d_uve_tests.cpp:137-164, 338-343`, `Test/Editor/editor_uve_tests.cpp:1163-1176`, `Test/Editor/editor_hierarchy_view_uve_tests.cpp:25` | ~14 assertions |
| Docs | `SCENE_NODES_ROADMAP.md`, `STUB_IMPLEMENTATION_ROADMAP.md` | a handful of lines |
| Icon generator | `Engine/Tools/editor_icons/icons.py` | 6 entries |

**Not touched on purpose:** the component structs (`RigidBodyComponentUVE`,
`CharacterControllerComponentUVE`, `AnimationPlayerComponentUVE`, `AnimationTreeComponentUVE`,
`AnimatableBody3DNodeComponentUVE`, `ColliderComponentUVE`) — those names are saved as component
JSON keys (`scene_serializer_uve.cpp:1403+`), so renaming them would need a save-format migration
for zero user-visible gain. The node's user-facing name is what changes.

### Legacy names the engine must keep loading

Old `typeId` strings stay readable forever through aliases (the existing `"empty"` → `node_3d` row
plus the 6 new ones: `static_body_3d`, `rigid_body_3d`, `character_body_3d`, `animatable_body_3d`,
`animation_player`, `animation_tree`). Old `.uvscene` and prefab files therefore open with the right
kind, with one test per alias.

## 5. Migration mechanics (so nothing breaks)

1. **Saved scenes.** Only `typeId` is written into a document. Each renamed `typeId` gets a legacy
   alias in `FindSceneNodeDescriptorUVE(std::string_view)`
   (`Engine/Runtime/Scene/Internal/nodes/scene_node_registry_uve.cpp:119-126`), exactly like the
   existing `"empty"` → `node_3d` row. Old `.uvscene`/prefab files keep opening with the right kind.
   One test per alias: write a document with the old id, load it, assert the new kind.
2. **Component JSON keys are not touched.** The component structs keep their names on purpose
   (Section 4), so no save-format migration is needed at all — the only saved string that changes is
   the `typeId`, which has aliases.
3. **Icons.** `assets/icons/nodes/<typeId>.png` is looked up by `typeId`
   (`editor_ui_assets_uve.cpp:181-190`), so each rename is a paired `git mv` of the `.png` + `.svg`;
   `Engine/Tools/editor_icons/icons.py` carries the same six keys and follows.
4. **Tests + docs.** `Test/Objects/3D/node_definitions_3d_uve_tests.cpp:137-164, 338-343`,
   `Test/Editor/editor_uve_tests.cpp:1163-1176`, `Test/Editor/editor_hierarchy_view_uve_tests.cpp:25`,
   plus the two roadmap checklists, in the same change as the rename they describe.
5. **Registry invariant test** (Phase 0 below): every kind has a unique non-empty `typeId`, an icon
   file named after it, and a label equal to its `defaultName`. This is what keeps the drift the
   audit found from coming back.

---

## 6. Phases (each one builds and passes on its own)

| Phase | What | Blast radius | Verification |
|---|---|---|---|
| **0** | Add the registry invariant test + fix the 16 wrong default names (audit 5.1) so it passes | 1 new test file, 16 headers, 1 editor lambda | new test + existing suite |
| **1** ✅ | `git mv Engine/Runtime/Nodes Engine/Runtime/Objects`; `CanvasLayer` → `UI`; `Object` → `Objects/Core`; `Test/Nodes` → `Test/Objects`; root CMake 43, 66-67; 20 files' path references | 115 files moved, **0** include lines | 100 `.cpp` files pass `g++ -fsyntax-only`; full build + 2,519 tests in CI |
| **2** ✅ | The 6 renames of Section 4: label + `typeId` + `defaultName` + icon pair + C++ enum/definition names + 6 legacy aliases + test/doc updates | 6 kinds, 24 files renamed + 51 files' text + 2 icon-file sets | 122 runtime TUs + 41 editor TUs + 12 test TUs compile clean (`g++ -fsyntax-only`); full build + tests in CI |
| **3** *(optional)* | Virtual path: `Expose/uve/nodes/...` → `Expose/uve/objects/...` and the 239 include lines, so the include path matches the physical `Objects/` folder | 110 files, mechanical, zero behavior change | full build + tests |
| **4** *(optional)* | Docs and comments: `SCENE_NODES_ROADMAP.md` → `SCENE_OBJECTS_ROADMAP.md`, `STUB_IMPLEMENTATION_ROADMAP.md`, `AUDIT.md`, and the comment lines naming other engines (**Godot** 35 lines in 19 files, **Unreal** 25 in 18, **Unity** 6 in 5; docs 48 / 2 / 2) | docs + a few cpp comments | full build + tests |
| ~~5~~ | ~~`Node` → `Object` identifier pass (2,734 occurrences)~~ — **dropped**: not requested, and the user-facing names are what matter | — | — |

Phases 0-2 are the whole requested job. Phase 3 is optional polish (it makes includes match the
folder); Phase 4 is doc hygiene.

One doc disagrees with itself and Phase 4 should fix it: `SCENE_NODES_ROADMAP.md:13` states
"No third-party engine or product name appears anywhere in this document", while line 44 of the same
file says "Godot-style one-root scene". The claim or the line has to go.

---

## 7. Open points

1. **The 6 new strings** (Section 4a/4b) — applied. A later change of mind costs another legacy
   alias in `FindSceneNodeDescriptorUVE`, nothing else.
2. **File/struct renaming depth:** rename only the user-facing strings, or also the C++ enum
   constants and `*NodeDefinitionUVE` structs + the six `*_3d_uve.h` filenames so the code reads like
   the UI? The plan assumes yes for enum/definition/file names, no for `*ComponentUVE` structs.
3. **`3D` suffix:** the six new names keep the `3D` suffix (they are spatial objects). Dropping it is
   a one-line change per name.
