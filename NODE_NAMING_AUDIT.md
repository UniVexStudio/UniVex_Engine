# Node Naming Audit — the Godot-style names in the UniVex node set

**Date:** 2026-10-02
**Branch:** `arena/01a0fd0d-univex-engine` (from `43a68128`)
**Scope:** every placeable scene node kind this engine has, the strings it is called by, where each
string lives in code, and how each name lines up with the Godot 4.4 class it is modelled on.
**Godot reference used:** `godotengine/godot` `4.4-stable`, `doc/classes/` — 880 classes total,
241 descendants of `Node`, **216 instantiable node classes**, 201 of those outside the editor's own
classes. Names below are compared against that set, not against memory.

This file is documentation only — no engine behavior, saved format or test changed by adding it.
Section 8 lists the fixes that *would* change behavior, each with its blast radius.

> **Note 2 (same day):** six of the names in the table below were then renamed, on the user's
> instruction and no further: the four `*Body3D` kinds (`StaticBody3D` → `Static3D`,
> `RigidBody3D` → `Rigid3D`, `CharacterBody3D` → `Character3D`, `AnimatableBody3D` →
> `Kinematic3D`) and the two animation kinds (`AnimationPlayer` → `AnimationSequencer`,
> `AnimationTree` → `AnimationGraph`). Old `typeId`s still load through legacy aliases. Every other
> name in this audit — including the 16 wrong default names of 5.1 and the label/default-name
> mismatches of 5.2 — is unchanged and still open.
>
> **Note 2a (same day):** that change is applied and verified. The registry was executed against the
> new table (each old `typeId` returns the same descriptor row as its new one), the editor icon
> contract was executed against the real embedded artwork (47/47 kinds have an icon that decodes),
> 225 tests from 30 suites were run locally, and CI ran the whole build plus the full test suite
> green on the branch. CI did catch one thing the local runs could not reach, and it is fixed:
> `UVScriptHighlightUVETest.ColoursEachKindOfWord` asserted a 15-byte span for the base kind,
> the length of the old `CharacterBody3D`, while the line it highlights already said
> `Character3D` (11 bytes).
>
> **Note 1 (same day, after this audit):** Phase 1 of `NAMING_PLAN.md` landed. The physical folders
> moved — `Engine/Runtime/Nodes` → `Engine/Runtime/Objects`, `CanvasLayer/` → `UI/`,
> `Engine/Runtime/Object` → `Objects/Core/`, `Test/Nodes` → `Test/Objects` — while every `#include`
> stayed the same, because includes resolve through each library's `Expose/` directory. The paths
> quoted in this audit are the paths as they were when it was written; for the current tree, read
> `Nodes/` as `Objects/`. The counts and the findings are unaffected.

---

## 1. What was counted

| What | Count |
|---|---|
| Scene node kinds registered in the engine | **47** |
| Of those, offered by the add/create UI (`libraryCreatable = true`) | **45** |
| Internal-only kinds (created by the document, never by the add UI) | **2** — `SceneRoot`, `Viewport` |
| Category icons / node icons on disk vs kinds needing one | **14 / 14** and **47 / 47** — all present, no gaps |
| Type ids (`typeId`) | **47**, all unique, no duplicates, no empty |
| Kinds whose C++ name is *exactly* a Godot class name | **22** |
| — of those, names that are an instantiable Godot **node** | **18** |
| — of those, names that are a Godot class but **not** an instantiable node (collision) | **4** — `Light3D`, `Occluder3D`, `Script`, `Viewport` |
| Kinds with **no** Godot class of that name | **25** (24 UniVex-only + `WorldEnvironment3D`, whose Godot twin drops the `3D`) |
| Kind labels that are exactly a Godot class name | **23** of 47 — 19 of those are instantiable Godot nodes, 4 collide (same four as above) |
| Kinds that create their entity with the **wrong default name** | **16** (see Section 5.1) |
| Kinds whose default entity name disagrees with their own add-UI label | **6** (see Section 5.2) |

### The five layers a node name lives in

A name is not stored in one place; every kind has up to five, and they can drift apart independently:

| # | Layer | Where it lives | Example |
|---|---|---|---|
| 1 | **C++ kind** (`SceneNodeKindUVE`) | `Engine/Runtime/Scene/Expose/uve/scene/nodes/scene_node_registry_uve.h:13-64` | `WorldEnvironment3D` |
| 2 | **Saved type id** (`typeId`) | `Engine/Runtime/Scene/Internal/nodes/scene_node_registry_uve.cpp:50-102` | `world_environment_3d` |
| 3 | **Add/create label** (`displayName`) | same registry rows | `WorldEnvironment` |
| 4 | **Default node name** (`defaultName`) | per-kind definition header, e.g. `Engine/Runtime/Nodes/3D/Expose/uve/nodes/3d/world_environment_3d_uve.h:32` | `WorldEnvironment` |
| 5 | **Component label** (inspector) | `Engine/Runtime/Scene/Internal/scene_component_metadata_uve.cpp` | `WorldEnvironment` |

Two more surfaces reuse names on top of those: the Content Browser's "+ Add" templates
(`editor_content_catalogue_uve.cpp`, e.g. "Empty Entity", "Prop", "Sun", "UI Canvas") and the
in-editor type tag drawn in the Outliner (`EditorUVE::GetOutlinerTypeTagUVE`,
`editor_uve.cpp:2663-2690`).

Layer 2 is the only one that is saved in a document. Layer 4 is the only one the user sees on a
freshly created node in the Outliner. Both are covered separately below because their rename risk
is completely different.

---

## 2. Master table — all 47 kinds

`Create label` = what the add/create UI shows. `Default node name` = what a newly created node is
called in the Outliner. **Bold** marks a broken default name.

| # | Kind (C++) | typeId | Create label | Default node name | Category | Definition / component header | Godot 4.4 counterpart |
|---|---|---|---|---|---|---|---|
| 1 | `SceneRoot` | `scene_root` | SceneRoot | SceneRoot | Scene | `Scene/Expose/uve/scene/nodes/scene_root_uve.h` | none — a Godot scene root is just any node |
| 2 | `Object3D` | `object_3d` | Object3D | Object3D | Scene | `Nodes/3D/Expose/uve/nodes/3d/object_3d_uve.h` | **Object3D** ✅ |
| 3 | `Area3D` | `area_3d` | Area3D | Area3D | Physics | `Nodes/3D/Physics/Expose/uve/nodes/3d/area_3d_uve.h` | **Area3D** ✅ |
| 4 | `RayCast3D` | `ray_cast_3d` | RayCast3D | **Object3D** | Physics | `.../3d/ray_cast_3d_uve.h` | **RayCast3D** ✅ |
| 5 | `StaticBody3D` | `static_body_3d` | StaticBody3D | StaticBody3D | Physics | `.../3d/static_body_3d_uve.h` | **StaticBody3D** ✅ |
| 6 | `AnimatableBody3D` | `animatable_body_3d` | AnimatableBody3D | AnimatableBody3D | Physics | `.../3d/animatable_body_3d_uve.h` | **AnimatableBody3D** ✅ |
| 7 | `NavigationRegion3D` | `navigation_region_3d` | NavigationRegion3D | **Object3D** | Navigation | `Nodes/AI/3D/Expose/uve/nodes/3d/navigation_region_3d_uve.h` | **NavigationRegion3D** ✅ |
| 8 | `NavigationAgent3D` | `navigation_agent_3d` | NavigationAgent3D | **Object3D** | Navigation | `.../3d/navigation_agent_3d_uve.h` | **NavigationAgent3D** ✅ |
| 9 | `Skeleton3D` | `skeleton_3d` | Skeleton3D | Skeleton3D | Animation | `Nodes/Animation/Expose/uve/nodes/3d/skeleton_3d_uve.h` | **Skeleton3D** ✅ |
| 10 | `BoneAttachment3D` | `bone_attachment_3d` | BoneAttachment3D | **Object3D** | Animation | `.../3d/bone_attachment_3d_uve.h` | **BoneAttachment3D** ✅ |
| 11 | `SpringArm3D` | `spring_arm_3d` | SpringArm3D | SpringArm3D | Camera | `.../3d/spring_arm_3d_uve.h` | **SpringArm3D** ✅ |
| 12 | `Marker3D` | `marker_3d` | Marker3D | **Object3D** | Scene | `.../3d/marker_3d_uve.h` | **Marker3D** ✅ |
| 13 | `Hitbox3D` | `hitbox_3d` | Hitbox3D | **Object3D** | Combat | `.../3d/hitbox_3d_uve.h` | none — Godot games build these out of `Area3D` |
| 14 | `Hurtbox3D` | `hurtbox_3d` | Hurtbox3D | **Object3D** | Combat | `.../3d/hurtbox_3d_uve.h` | none — same |
| 15 | `Projectile3D` | `projectile_3d` | Projectile3D | **Object3D** | Combat | `.../3d/projectile_3d_uve.h` | none |
| 16 | `InteractionArea3D` | `interaction_area_3d` | InteractionArea3D | **Object3D** | Gameplay | `.../3d/interaction_area_3d_uve.h` | none — the interactor/focus loop is per-game in Godot |
| 17 | `WorldEnvironment3D` | `world_environment_3d` | **WorldEnvironment** | WorldEnvironment | Rendering | `.../3d/world_environment_3d_uve.h` | `WorldEnvironment` (no `3D`) |
| 18 | `ReflectionProbe3D` | `reflection_probe_3d` | ReflectionProbe3D | **Object3D** | Rendering | `.../3d/reflection_probe_3d_uve.h` | `ReflectionProbe` (no `3D`) |
| 19 | `Decal3D` | `decal_3d` | Decal3D | Decal3D | Rendering | `.../3d/decal_3d_uve.h` | `Decal` (no `3D`) |
| 20 | `FogVolume3D` | `fog_volume_3d` | FogVolume3D | FogVolume3D | Rendering | `.../3d/fog_volume_3d_uve.h` | `FogVolume` (no `3D`) |
| 21 | `LODGroup3D` | `lod_group_3d` | LODGroup3D | **Object3D** | Optimization | `.../3d/lod_group_3d_uve.h` | none — Godot does LOD inside the mesh/importer |
| 22 | `Occluder3D` | `occluder_3d` | Occluder3D | **Object3D** | Optimization | `.../3d/occluder_3d_uve.h` | ⚠ name taken: Godot's `Occluder3D` is an abstract **Resource**; the node is `OccluderInstance3D` |
| 23 | `VisibilityRegion3D` | `visibility_region_3d` | VisibilityRegion3D | **Object3D** | Optimization | `.../3d/visibility_region_3d_uve.h` | none — nearest relative is `VisibleOnScreenNotifier3D` |
| 24 | `SpawnPoint3D` | `spawn_point_3d` | SpawnPoint3D | **Object3D** | Gameplay | `.../3d/spawn_point_3d_uve.h` | none — a Godot game uses `Marker3D` |
| 25 | `LevelStreamer3D` | `level_streamer_3d` | LevelStreamer3D | **Object3D** | World | `.../3d/level_streamer_3d_uve.h` | none |
| 26 | `WorldPartition3D` | `world_partition_3d` | WorldPartition3D | **Object3D** | World | `.../3d/world_partition_3d_uve.h` | none |
| 27 | `AnimationTree` | `animation_tree` | AnimationTree | AnimationTree | Animation | `Nodes/Animation/Expose/uve/nodes/3d/animation_tree_uve.h` | **AnimationTree** ✅ |
| 28 | `AnimationPlayer` | `animation_player` | AnimationPlayer | AnimationPlayer | Animation | `.../3d/animation_player_uve.h` | **AnimationPlayer** ✅ |
| 29 | `CharacterBody3D` | `character_body_3d` | CharacterBody3D | CharacterBody3D | Physics | `Nodes/3D/Physics/Expose/uve/nodes/3d/character_body_3d_uve.h` | **CharacterBody3D** ✅ |
| 30 | `Camera3D` | `camera_3d` | Camera3D | **Camera** | Rendering | `.../3d/camera_3d_uve.h` | **Camera3D** ✅ |
| 31 | `MeshInstance3D` | `mesh_instance_3d` | MeshInstance3D | MeshInstance3D | Rendering | `.../3d/mesh_instance_3d_uve.h` | **MeshInstance3D** ✅ |
| 32 | `BoxMesh3D` | `box_mesh_3d` | BoxMesh3D | **Cube** | Rendering | `.../3d/box_mesh_3d_uve.h` | none — Godot's `BoxMesh` is a **Mesh resource** used on `MeshInstance3D` |
| 33 | `SphereMesh3D` | `sphere_mesh_3d` | SphereMesh3D | **UV Sphere** | Rendering | `.../3d/sphere_mesh_3d_uve.h` | none — Godot's `SphereMesh` is a resource (and is called *SphereMesh*, not *UV Sphere*) |
| 34 | `PlaneMesh3D` | `plane_mesh_3d` | PlaneMesh3D | **Plane** | Rendering | `.../3d/plane_mesh_3d_uve.h` | none — Godot's `PlaneMesh` is a resource |
| 35 | `Light3D` | `light_3d` | Light3D | **Directional Light** | Rendering | `.../3d/light_3d_uve.h` | ⚠ name taken: Godot's `Light3D` is the **abstract base** of `OmniLight3D` / `SpotLight3D` / `DirectionalLight3D` |
| 36 | `Collider3D` | `collider_3d` | Collider3D | **Collision Box** | Physics | `.../3d/collider_3d_uve.h` | none — Godot's node is `CollisionShape3D` |
| 37 | `RigidBody3D` | `rigid_body_3d` | RigidBody3D | RigidBody3D | Physics | `.../3d/rigid_body_3d_uve.h` | **RigidBody3D** ✅ |
| 38 | `AudioSource3D` | `audio_source_3d` | AudioSource3D | AudioSource3D | Audio | `.../3d/audio_source_3d_uve.h` | `AudioStreamPlayer3D` |
| 39 | `ParticleEmitter3D` | `particle_emitter_3d` | ParticleEmitter3D | ParticleEmitter3D | VFX | `.../3d/particle_emitter_3d_uve.h` | `GPUParticles3D` / `CPUParticles3D` |
| 40 | `Script` | `script` | Script | Script | Logic | `.../3d/script_uve.h` | ⚠ name taken: Godot's `Script` is a **Resource**; behavior is attached to any node |
| 41 | `Canvas` | `canvas` | Canvas | Canvas | UI | `Nodes/CanvasLayer/Expose/uve/nodes/canvas_layer/canvas_uve.h` | `CanvasLayer` (node) / `Control` (widgets) |
| 42 | `UIText` | `ui_text` | UI Text | UI Text | UI | `.../canvas_layer/ui_text_uve.h` | `Label` |
| 43 | `UIImage` | `ui_image` | UI Image | UI Image | UI | `.../canvas_layer/ui_image_uve.h` | `TextureRect` |
| 44 | `UIButton` | `ui_button` | UI Button | UI Button | UI | `.../canvas_layer/ui_button_uve.h` | `Button` |
| 45 | `Folder` | `folder` | Folder | Folder | Scene | `Scene/Expose/uve/scene/nodes/scene_folder_uve.h` | none — Godot groups with plain `Node`s / node groups |
| 46 | `DirectionalLight3D` | `directional_light_3d` | DirectionalLight3D | DirectionalLight3D | Rendering | `.../3d/directional_light_3d_uve.h` | **DirectionalLight3D** ✅ |
| 47 | `Viewport` | `viewport` | Viewport | Viewport | Scene | `Scene/Expose/uve/scene/nodes/scene_folder_uve.h` | ⚠ name taken: Godot's `Viewport` is an **abstract render-target node** (`SubViewport`, `Window` are the real ones); UniVex's `Viewport` is the Outliner's level root |

Exact-name matches (rows marked ✅, 18 node kinds): `Object3D`, `Area3D`, `RayCast3D`,
`StaticBody3D`, `AnimatableBody3D`, `NavigationRegion3D`, `NavigationAgent3D`, `Skeleton3D`,
`BoneAttachment3D`, `SpringArm3D`, `Marker3D`, `AnimationTree`, `AnimationPlayer`,
`CharacterBody3D`, `Camera3D`, `MeshInstance3D`, `RigidBody3D`, `DirectionalLight3D`.

There is deliberately **no** `Light3D` in that list: the string matches a Godot class, but the class
it matches is an abstract base, so a UniVex `Light3D` and a Godot `Light3D` are not the same thing.

---

## 3. Where the names live in code (touch points)

| Layer | File | Lines |
|---|---|---|
| Kind enum | `Engine/Runtime/Scene/Expose/uve/scene/nodes/scene_node_registry_uve.h` | 13-64 |
| Registry rows (typeId, label, category, contracts, `libraryCreatable`) | `Engine/Runtime/Scene/Internal/nodes/scene_node_registry_uve.cpp` | 13-46 (contracts), 50-102 (rows) |
| Legacy type-id alias (`"empty"` → `Object3D`) | `Engine/Runtime/Scene/Internal/nodes/scene_node_registry_uve.cpp` | 119-126 |
| Kind inference from components (for scenes saved before the type was stored) | `Engine/Runtime/Scene/Internal/nodes/scene_node_type_uve.cpp` | whole file |
| Per-kind default node names | `Engine/Runtime/Nodes/**/<kind>_uve.h` (`defaultName`) | 31 headers, see Section 5.2 |
| Creation switch (definition vs bare component) | `Engine/Editor/EditorCore/Internal/editor_uve.cpp` | 1919-2115 |
| Legacy editor kinds → node kinds mapper | `Engine/Editor/EditorCore/Internal/editor_uve.cpp` | 2937-3010 |
| Outliner type tag / type name | `Engine/Editor/EditorCore/Internal/editor_uve.cpp` | 2654-2690 |
| Node icons (filename **is** the typeId) | `Engine/Editor/EditorCore/assets/icons/nodes/<typeId>.png` | 47 files |
| Icon lookup | `Engine/Editor/EditorCore/Internal/editor_ui_assets_uve.cpp` | 181-190 |
| Content Browser templates (third naming layer) | `Engine/Editor/EditorCore/Internal/editor_content_catalogue_uve.cpp` | 58-125 |
| Inspector component labels | `Engine/Runtime/Scene/Internal/scene_component_metadata_uve.cpp` | 173-430 (file is 1370 lines) |
| Scene "+" popup (Folder / sun / sky only) | `Engine/Editor/EditorCore/Internal/editor_panel_hierarchy_uve.cpp` | 595-670 |
| Tests that lock names | `Test/Nodes/3D/node_definitions_3d_uve_tests.cpp:131-160`, `Test/Nodes/CanvasLayer/node_definitions_canvas_layer_uve_tests.cpp`, `Test/Editor/editor_uve_tests.cpp:564-590, 2020-2100, 1148-1170` | — |

---

## 4. Save-format risk, in one place

* **`typeId` (layer 2)** is written into every `.uvscene`/prefab
  (`scene_serializer_uve.cpp:810`, `"type"` key). On load, an unknown id is *silently dropped*
  (`scene_serializer_uve.cpp:1645-1656`) — the node keeps its components but loses its explicit
  type and falls back to component inference. Renaming a `typeId` therefore needs the same legacy
  alias the `"empty"` → `node_3d` rename used (`scene_node_registry_uve.cpp:119-126`).
* **Component names** are also saved, as JSON object keys taken from the C++ struct name
  (`scene_serializer_uve.cpp:1403-1700`). Renaming a `*NodeComponentUVE` struct needs a migration
  or an alias entry in that table; there is currently **no** generic alias mechanism for component
  keys (only per-feature migrations such as the animation ones).
* **`displayName`, `category`, `defaultName`, icon files and C++ enum names are not saved** — they
  can be renamed freely, and only tests and docs need updating.

---

## 5. Findings

### 5.1 🔴 Sixteen of the 45 creatable kinds are created as "Object3D"

`EditorUVE::CreateSceneNodeEntityInternalUVE` (`editor_uve.cpp:1919-2115`) has two creation paths:

* 29 kinds go through `CreateNodeDefinitionEntityInternalUVE`, which creates the entity **with that
  kind's `defaultName`** (the recipe headers in `Engine/Runtime/Nodes/**`);
* 16 kinds call the local `createNodeWithComponent` lambda (`editor_uve.cpp:1922-1930`), which
  creates the entity through `EditorEntityKindUVE::Empty` — i.e. with **`Object3DNodeDefinitionUVE::defaultName`**,
  the string `"Object3D"` — and only then attaches the component.

The 16 affected kinds: `RayCast3D`, `NavigationRegion3D`, `NavigationAgent3D`, `BoneAttachment3D`,
`Marker3D`, `Hitbox3D`, `Hurtbox3D`, `Projectile3D`, `InteractionArea3D`, `ReflectionProbe3D`,
`LODGroup3D`, `Occluder3D`, `VisibilityRegion3D`, `SpawnPoint3D`, `LevelStreamer3D`,
`WorldPartition3D`.

Consequence: adding a `Hitbox3D` in the editor produces an Outliner row named **"Object3D"** (then
"Object3D 2", …), while the row's type tag says `Hitbox3D`. The user-visible name and the node's type
disagree for a third of the library.

Not locked by tests: the one test that creates every creatable kind in a loop
(`Test/Editor/editor_uve_tests.cpp:1148-1155`) asserts the *type tag* only, never the entity name,
so it will keep passing whichever way this is fixed. Adding a `defaultName` to these 16 headers
would also make the add-UI label and the Outliner name agree for them, exactly as they already do
for the other 29.

### 5.2 🟠 Six kinds' default node names disagree with their own add-UI label

| Kind | Add-UI label | Default node name | File |
|---|---|---|---|
| `Camera3D` | Camera3D | `Camera` | `camera_3d_uve.h:24` |
| `Light3D` | Light3D | `Directional Light` | `light_3d_uve.h:26` |
| `Collider3D` | Collider3D | `Collision Box` | `collider_3d_uve.h:24` |
| `BoxMesh3D` | BoxMesh3D | `Cube` | `box_mesh_3d_uve.h:26` |
| `SphereMesh3D` | SphereMesh3D | `UV Sphere` | `sphere_mesh_3d_uve.h:26` |
| `PlaneMesh3D` | PlaneMesh3D | `Plane` | `plane_mesh_3d_uve.h:26` |

These are locked by tests (`Test/Nodes/3D/node_definitions_3d_uve_tests.cpp:131-135, 148-160`;
`Test/Editor/editor_uve_tests.cpp:2035-2088`), so changing them is a deliberate, test-visible
change — not an accident. Note `UV Sphere` is Blender terminology and `Cube` clashes with the fact
that the node is a *BoxMesh*; Godot's own primitives are `BoxMesh`, `SphereMesh`, `PlaneMesh`.

### 5.3 🟠 `WorldEnvironment3D` is the only kind whose label drops the `3D`

Kind `WorldEnvironment3D` → label `WorldEnvironment` → Godot class `WorldEnvironment`. This one is
self-consistent (the Godot class has no `3D`), but it is the only 3D kind whose C++ name and label
differ in the trailing `3D`. Either the kind becomes `WorldEnvironment` or the label becomes
`WorldEnvironment3D`; today it is both, depending on which layer you read.

### 5.4 🟠 Two kinds mean "a directional light"

* `Light3D` — `LightComponentUVE` with `LightTypeUVE::{Directional, Point, Spot}`
  (`light_component_uve.h:17`) and **default `Directional`** (`light_3d_uve.h:34`), label `Light3D`,
  default name "Directional Light".
* `DirectionalLight3D` — a different component pair (`DirectionalLight3DComponentUVE` +
  `LightEmitterComponentUVE`, with shadow-cascade fields), the top-level singleton "sun".

So a user adding "Light3D" from the library and a user adding "DirectionalLight3D" from the Scene
"+"/Content Browser both get a directional light, built from two different component stacks, with
two different names. This is a naming problem on top of a real design question (Gap in
`SCENE_NODES_ROADMAP.md`? — not currently listed there).

### 5.5 🟡 `LODGroup3D` vs `LodGroup3DNodeComponentUVE`

The kind, typeId, label and icon all say **LOD**; the component struct, its validity function and
its free function say **Lod** (`lod_group_3d_uve.h:27,61,63`), and the registry's contract string
is `"LodGroup3DNodeComponentUVE"` (`scene_node_registry_uve.cpp:35`). Harmless to the build, but it
is the one acronym in the node set that is not spelled the same way twice. Note the struct name is
also a **saved component key** (Section 4), so renaming it needs an alias.

### 5.6 🔴 Four names collide with a Godot class that means something else

| UniVex name | Godot class of the same name | What Godot's is | Risk |
|---|---|---|---|
| `Light3D` | `Light3D` | abstract base of `OmniLight3D`/`SpotLight3D`/`DirectionalLight3D` (`GDREGISTER_ABSTRACT_CLASS`) | a reader porting code expects the base class |
| `Occluder3D` | `Occluder3D` | abstract **Resource**; the node is `OccluderInstance3D` | wrong kind of object entirely |
| `Script` | `Script` | **Resource** holding source, attached to any node | wrong kind of object entirely |
| `Viewport` | `Viewport` | abstract render-target **node** (`SubViewport`/`Window` are concrete); UniVex's is the Outliner's level root | same word, unrelated concept |

### 5.7 🔵 Same idea, different name (the intentional ones)

These are the UniVex-only nodes — no Godot node class exists with this meaning, so there is nothing
to align to:

`Hitbox3D`, `Hurtbox3D`, `Projectile3D`, `InteractionArea3D`, `LODGroup3D`, `VisibilityRegion3D`,
`SpawnPoint3D`, `LevelStreamer3D`, `WorldPartition3D`, `SceneRoot`, `Folder`, `Script` (as a node).

And these are UniVex names for a Godot node that exists under a different string:

| UniVex | Godot node | Comment |
|---|---|---|
| `Collider3D` | `CollisionShape3D` | Godot also separates `CollisionPolygon3D` |
| `AudioSource3D` | `AudioStreamPlayer3D` | |
| `ParticleEmitter3D` | `GPUParticles3D` / `CPUParticles3D` | Godot names the backend, UniVex names the role |
| `Canvas` | `CanvasLayer` | |
| `UIText` / `UIImage` / `UIButton` | `Label` / `TextureRect` / `Button` | Godot's UI kinds have no `UI` prefix at all (`Control` is the base) |
| `BoxMesh3D` / `SphereMesh3D` / `PlaneMesh3D` | `MeshInstance3D` + `BoxMesh` / `SphereMesh` / `PlaneMesh` **resources** | UniVex fuses node+resource into one placeable kind |
| `ReflectionProbe3D` / `Decal3D` / `FogVolume3D` | `ReflectionProbe` / `Decal` / `FogVolume` | Godot drops the `3D` on exactly these four rendering kinds (`WorldEnvironment` included) |
| `VisibilityRegion3D` | `VisibleOnScreenNotifier3D` | approximate only — different mechanism (region ownership vs screen rect) |

### 5.8 🔵 The third naming layer (Content Browser templates) uses short, non-Godot names

`editor_content_catalogue_uve.cpp:58-125` names its create entries: `Empty Entity`, `Character`,
`Prop`, `Physics Prop`, `Trigger`, `Spawner`, `Box`, `Sphere`, `Plane`, `Mesh`, `Sun`, `Light`,
`World Environment`, `Reflection Probe`, `Fog Volume`, `Decal`, `Camera`, `Follow Camera`,
`Static Body`, `Rigid Body`, `Collider`, `Area`, `RayCast`, `Audio Source`, `Particles`,
`UI Canvas`, `Level Streamer`, `World Partition`, `Navigation Region`, `Occluder`. These are
**template** names (an entity asset containing a small tree), not node type names — but they are
the names most users actually click, and several of them (`Sun`, `Box`, `Collider`, `RayCast`,
`Occluder`) collide with the default node names of the kinds they build. Worth deciding whether
templates should read like Godot ("BoxMesh") or like content ("Box") — the file already answers
"content", which is reasonable; it just needs to be consistent with Section 5.2, where the same
object is called "Cube".

### 5.9 🟡 Inspector component labels borrow node names

`scene_component_metadata_uve.cpp` labels the *components* with node-style names:
`component.mesh` → "MeshInstance3D", `component.camera` → "Camera", `component.ui_text` →
"UI Text", `component.world_environment` → "WorldEnvironment", `component.character_controller` →
"CharacterBody3D", `component.skeleton_3d` → "Skeleton3D", `component.particle_emitter` →
"ParticleEmitter3D", `component.script` → "Script". A reader inspecting a `StaticBody3D` sees a
component literally called "MeshInstance3D". Naming the component after its node kind is exactly
the confusion the registry was introduced to end (see `SceneNodeTypeComponentUVE`'s own comment,
`scene_node_type_uve.h:9-17`).

### 5.10 🟢 Verified consistent (no action)

* Every one of the 47 `typeId`s has a matching `assets/icons/nodes/<typeId>.png`; no orphan icon,
  no missing icon.
* All 14 registry categories have a matching `assets/icons/node_categories/<name>.png` and vice
  versa — no unused category icon, no iconless category.
* All 47 `typeId`s are unique and non-empty; `kMaximumSceneNodeDescriptorsUVE = 64`
  (`scene_node_registry_uve.h:79`, which also sizes the editor's node-icon texture array,
  `editor_ui_assets_uve.h:53`) leaves 17 slots of headroom before the array needs growing.
* `kDescriptors` holds exactly 47 rows, one per enumerator; `SceneNodeKindUVE` and the row list
  agree in both directions.

---

## 6. What a fix would look like, by blast radius

### Level A — no save-format impact (labels, defaults, code spelling)

1. Give the 16 component-only kinds their own `defaultName` (Section 5.1) — either by adding a
   `defaultName` field to those `*NodeComponentUVE` headers and teaching `createNodeWithComponent`
   to rename after creation, or by promoting them to full `*NodeDefinitionUVE` recipes like the
   other 29. Expected result: a new `Hitbox3D` is called "Hitbox3D" in the Outliner.
2. Align the six disagreeing default names (Section 5.2), e.g. `Camera3D`, `Light3D`,
   `Collider3D`, `BoxMesh3D`, `SphereMesh3D`, `PlaneMesh3D` — or, if the short names are wanted,
   change the *labels* instead. Either way the two strings should be the same string.
3. Fix the `Lod`/`LOD` spelling in the C++ identifiers (`lod_group_3d_uve.h`) *only after* adding a
   component-key alias; the string is saved (Section 4).
4. Decide `WorldEnvironment3D` (Section 5.3) and the `Light3D`/`DirectionalLight3D` split
   (Section 5.4) — the second one is a design decision, not a rename.

### Level B — renames `typeId` (needs a legacy alias, or old scenes lose their node type)

Follow the `"empty"` → `Object3D` pattern (`scene_node_registry_uve.cpp:119-126`): add
`if (typeId == "<old id>") return FindSceneNodeDescriptorUVE(SceneNodeKindUVE::<Kind>);` before the
lookup loop, keep the icon file renamed to match, and add a test that loads a document written with
the old id. Candidate id renames, if strict Godot spelling is wanted:
`world_environment_3d`, `reflection_probe_3d`, `decal_3d`, `fog_volume_3d` → drop the `3d`.

### Level C — renaming the C++ enum or component structs

Free, except for the component JSON keys (Section 4) and the tests that name them. No engine design
is affected.

---

## 7. Godot nodes with no UniVex counterpart (gap list, for the roadmap)

201 instantiable Godot node classes exist outside the editor's own classes. UniVex has a kind for
about 30 of them (counting the different-name mappings in 5.7). The notable families still missing,
useful when `SCENE_NODES_ROADMAP.md` is next updated:

* **3D lighting/shadow:** `OmniLight3D`, `SpotLight3D` (UniVex covers these as `Light3D` types, not
  kinds), `LightmapGI`, `LightmapProbe`, `VoxelGI`, `RootMotionView`.
* **3D shape/query:** `CollisionShape3D`, `CollisionPolygon3D`, `ShapeCast3D`, `Path3D`,
  `PathFollow3D`, `RemoteTransform3D`, `NavigationObstacle3D`, `NavigationLink3D`.
* **3D physics joints:** `PinJoint3D`, `HingeJoint3D`, `SliderJoint3D`, `ConeTwistJoint3D`,
  `Generic6DOFJoint3D`, `SoftBody3D`, `PhysicalBone3D`, `PhysicalBoneSimulator3D`,
  `VehicleBody3D`, `VehicleWheel3D`.
* **3D rendering:** `MultiMeshInstance3D`, `Sprite3D`, `AnimatedSprite3D`, `Label3D`,
  `GPUParticlesAttractor*3D`, `GPUParticlesCollision*3D`, `VisibleOnScreenNotifier3D`,
  `VisibleOnScreenEnabler3D`.
* **Animation extras:** `SkeletonIK3D`, `LookAtModifier3D`, `RetargetModifier3D`,
  `SpringBoneSimulator3D`, `SpringBoneCollision*3D`, `XRBodyModifier3D`, `XRHandModifier3D`.
* **XR:** `XROrigin3D`, `XRObject3D`, `XRController3D`, `XRAnchor3D`, `XRCamera3D`,
  `XRFaceModifier3D`.
* **Audio/utility:** `AudioStreamPlayer`, `AudioListener3D`, `Timer`, `HTTPRequest`,
  `ResourcePreloader`, `ShaderGlobalsOverride`, `MultiplayerSpawner`, `MultiplayerSynchronizer`,
  `InstancePlaceholder`.
* **Window/popups:** `Window`, `AcceptDialog`, `ConfirmationDialog`, `FileDialog`, `Popup`,
  `PopupMenu`, `PopupPanel`, `SubViewport`, `SubViewportContainer`.
* **UI (`Control` family, ~90 classes):** `Control`, `Label`, `TextureRect`, `Button`,
  `CheckBox`, `CheckButton`, `OptionButton`, `LineEdit`, `TextEdit`, `CodeEdit`, `RichTextLabel`,
  `Panel`, `PanelContainer`, `ProgressBar`, `HSlider`, `VSlider`, `ScrollContainer`,
  `TabContainer`, `TabBar`, `Tree`, `ItemList`, `MenuBar`, `GraphEdit`, `GraphNode`, `VideoStreamPlayer`, …
* **2D (the whole `Node2D` family, ~70 classes):** already listed with Godot names in
  `SCENE_NODES_ROADMAP.md`'s 2D section — that list and this audit agree.

---

## 8. Suggested exact strings (pick one column per row before touching code)

Only the rows that are currently inconsistent. "Keep" means the existing value is already the one
recommended by this audit.

| Kind | `typeId` | Add label | Default node name | Options |
|---|---|---|---|---|
| `Camera3D` | `camera_3d` (keep) | `Camera3D` (keep) | `Camera` | → `Camera3D`, or change the label to `Camera` |
| `Light3D` | `light_3d` (keep) | `Light3D` (keep) | `Directional Light` | → `Light3D`; rename the kind to `Light` if it keeps 3 types |
| `Collider3D` | `collider_3d` (keep) | `Collider3D` (keep) | `Collision Box` | → `Collider3D`, or `CollisionShape3D` if Godot spelling is wanted |
| `BoxMesh3D` | `box_mesh_3d` (keep) | `BoxMesh3D` (keep) | `Cube` | → `BoxMesh3D` or `Box` |
| `SphereMesh3D` | `sphere_mesh_3d` (keep) | `SphereMesh3D` (keep) | `UV Sphere` | → `SphereMesh3D` or `Sphere` |
| `PlaneMesh3D` | `plane_mesh_3d` (keep) | `PlaneMesh3D` (keep) | `Plane` | → `PlaneMesh3D` or `Plane` (already `Plane`) |
| `WorldEnvironment3D` | `world_environment_3d` (keep) | `WorldEnvironment` (keep) | `WorldEnvironment` (keep) | rename the *kind* to `WorldEnvironment`, or the label to `WorldEnvironment3D` |
| 16 kinds in 5.1 | keep | keep | `Object3D` | → each kind's own label |
