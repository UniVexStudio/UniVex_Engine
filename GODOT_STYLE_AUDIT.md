# Godot-Style Audit — deep pass over UniVex_Engine

**Date:** 2026-10-03
**Branch:** `arena/01a0ffb2-univex-engine` (from `693893b`)
**Status:** REPORT ONLY. No engine source was changed by this audit. Everything below is a finding
plus a proposed action, for us to decide on together before anything is renamed.

---

## 0. How to check every claim in this file

Nothing here is from memory or from the older audit documents in this repo. Each claim was produced
by a command that was actually run against this tree, and the Godot side was checked against the
real `godotengine/godot` `4.4-stable` source fetched from GitHub, not recalled.

| Claim in this file | How it was produced |
|---|---|
| "880 Godot classes" | `git/trees/4.4-stable?recursive` → counted `doc/classes/*.xml` = **880** |
| "N of the 47 kinds are Godot class names" | parsed the 47 rows out of `scene_object_registry_uve.cpp` with a script, set-intersected against that 880-name list |
| "3 enums are verbatim Godot" | read `scene/main/node.h` at `4.4-stable` (blob `2d6fa157`) lines 72–76, 92–94, 120–122 and compared enumerator-for-enumerator |
| "`is_on_floor` is Godot's" | read `scene/3d/physics/character_body_3d.h` at `4.4-stable` (blob `430d9c35`) line 56: `bool is_on_floor() const;` |
| "Variant mirrors Godot's Variant::Type" | read `core/variant/variant.h` at `4.4-stable` (blob `633b9c9e`), `enum Type` block |
| "the 16 kinds get the default name `Object3D`" | read `editor_uve.cpp:1923-1930` (`createObjectWithComponent`) → `CreateDocumentEntityInternalUVE(EditorEntityKindUVE::Empty)` → `editor_uve.cpp:4003` → `Object3DObjectDefinitionUVE::defaultName` |

Note on the older documents: `NODE_NAMING_AUDIT.md` is **stale** relative to the code. Its master
table still lists `StaticBody3D`, `AnimatableBody3D`, `AnimationPlayer`, `AnimationTree`, and
`Empty` as current kinds. The code no longer has them (they are `Static3D`, `Kinematic3D`,
`AnimationSequencer`, `AnimationGraph`, `Object3D`). Section 2 below is built from the code, not
from that table. Its two open findings (5.1 wrong default names, 5.2 label/default mismatch) are
**still real** and are re-verified here.

---

## 1. Verification baseline (what actually ran)

A full `cmake` build of the repo cannot run in this sandbox: `Engine/Runtime/Window`,
`RHI/OpenGL`, `RHI/Vulkan`, `RHI/RHI`, `RHI/Shader`, `Editor/Viewport`, `Editor/EditorCore`,
`Editor/EditorApp` and `App` need `glfw3`, `Vulkan`, `GLEW`, `OpenGL` and X11 headers, none of which
are installed and none of which can be installed (apt has no network egress in this sandbox).

To still run real project code, I built a **subset** using the repo's own module `CMakeLists.txt`
files (via `add_subdirectory` on each), plus the repo's own test sources, with `ZLIB`/`JPEG` headers
supplied locally:

```
cmake -S /tmp/subset -B /tmp/sbuild -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/sbuild --target uve_audit_subset_tests -j 2     # 0 errors
./uve_audit_subset_tests
[==========] 192 tests from 30 test suites ran. (96 ms total)
[  PASSED  ] 192 tests.
```

Suites that ran and cover this audit's findings:

| Suite | Tests | What it locks |
|---|---|---|
| `SceneObjectRegistryUVETest` | 4 | the 47-kind descriptor table |
| `SceneObjectTypeUVETest` | 4 | typeId ↔ kind resolution |
| `SceneComponentMetadataUVETest` | 11 | inspector component labels |
| `SceneSerializerUVETest` | 66 | legacy component-name + typeId aliases |
| `VariantUVETest` | 4 | the Variant type table |
| `Object3DDefinitionsUVETest` | 39 | per-kind definitions incl. `defaultName` |
| `Expanded3DObjectComponentsUVETest` | 8 | expanded 3D object components |
| `CanvasLayerObjectDefinitionsUVETest` | 3 | the `canvas_layer` family |
| `AnimationTreeUVETest` | 6 | the Godot-named `AnimationTree*` code |
| `CharacterBodyMotionUVETest` | 8 | the Godot-named `CharacterBody*` code |

**This is the green baseline any rename below has to land on.** The editor, RHI and window suites
were **not** run here — so a rename touching `Engine/Editor/**` is verified by compile only in this
sandbox, and must be confirmed on CI.

---

## 2. What is NOT a problem (ruled out, so we don't waste a phase on it)

I grepped for the whole Godot API surface. These came back **clean** — the engine has no trace of
them:

`get_node` · `get_node_or_null` · `NodePath` (as a live type) · `ClassDB` · `GDCLASS` · `ObjectID`
· `Variant`'s `Callable`/`Signal` types · `Callable` · `PackedScene` · `ResourceLoader` ·
`queue_free` · `add_child` · `remove_child` · `get_parent` · `is_inside_tree` · `_enter_tree` ·
`_exit_tree` · `_process` · `_physics_process` · `_input` · `_unhandled_input` · `_notification` ·
`onready` · `@export` · `SceneTree` · `CanvasItem` · `SubViewport` · `Tween` · `EditorPlugin` ·
`move_and_slide` · `to_local`/`to_global` · `.tscn`

Also clean, and worth stating because they *look* like hits:

- **`Node` as a word.** 92 hits in `Engine`/`Test`
  (`grep -rIn --include=*.h --include=*.cpp -E '\b[A-Za-z0-9_]*Node[A-Za-z0-9_]*\b' Engine Test | wc -l`).
  All of them are one of: Dear ImGui's own
  `ImGuiTreeNodeFlags_*` / `TreeNodeEx` API (third-party, not Godot), the FBX ASCII format's
  `NodeAttribute`/`LimbNode` keywords in test fixtures, or the load-time legacy alias tables. There
  is **no engine type, file, folder, label or field named `Node`.** The earlier `Node` → `Object`
  pass did land.
- **`Spatial` (20 hits).** Every one is the English adjective or the `AudioSourceComponentUVE::spatial`
  field. Godot's `Spatial` class is not used.
- **`Label` (128 hits).** All are method-name suffixes (`GetEntityDisplayLabelUVE`,
  `AnimationGraphSlotLabelUVE`). There is no type called `Label`. False positive.
- **`Vector2/3/4`, `Quaternion`, `Plane`, `Object`, `Timer`, `Skin`, `Translation`.** These collide
  with Godot class names but are universal engine vocabulary, not Godot-specific. Recommend
  leaving them alone.
- **Asset extensions** are already UniVex-branded: `.uvanim`, `.uvtex`, `.uvs`, `.uvsave`,
  `.uvmat`, `.uvsky`, `.uvskel`, `.uvnav`. No `.tscn`/`.tres` anywhere.
- **UVScript is not GDScript.** It deliberately rejects Godot's `signal` keyword —
  `Test/UVScript/uvscript_vm_uve_tests.cpp:463` asserts `ParseUVE("signal x\n", ...)` **fails**.
  Its own keywords are `entity`/`fn`/`let`/`on`/`wait`/`none`, which GDScript does not have.

And one category I recommend we **keep deliberately**: the ~35 comments that mention Godot by name
(e.g. `engine_core_uve.h:534` "…instead of popping the way Godot's SpringArm3D does (Godot ships no
smoothing member)"). Those are comparative design rationale — they explain *why* UniVex differs, and
they name Godot precisely because the difference is the point. Removing them would delete
information. Flagging them here so the decision is explicit rather than accidental.

---

## 3. Findings

Ranked by how much work/risk each carries, cheapest-and-safest first.

### Finding A — Three enums are Godot's, enumerator-for-enumerator ⭐ highest-confidence, lowest-risk

| UniVex | File:line | Godot 4.4 (`scene/main/node.h`) | Match |
|---|---|---|---|
| `ProcessModeUVE { Inherit, Pausable, WhenPaused, Always, Disabled }` | `Engine/Runtime/Component/Expose/uve/component/process_component_uve.h:16` | `PROCESS_MODE_{INHERIT, PAUSABLE, WHEN_PAUSED, ALWAYS, DISABLED}` @72–76 | **5/5, same order** |
| `PhysicsInterpolationModeUVE { Inherit, On, Off }` | `.../physics_interpolation_component_uve.h:13` | `PHYSICS_INTERPOLATION_MODE_{INHERIT, ON, OFF}` @92–94 | **3/3, same order** |
| `AutoTranslateModeUVE { Inherit, Always, Disabled }` | `.../auto_translate_component_uve.h:17` | `AUTO_TRANSLATE_MODE_{INHERIT, ALWAYS, DISABLED}` @120–122 | **3/3, same order** |

Even the defaults match — Godot's `node.h:253` has `auto_translate_mode = AUTO_TRANSLATE_MODE_INHERIT`,
and `auto_translate_component_uve.h:31` has `mode = AutoTranslateModeUVE::Inherit`.

The *implementations* are genuinely UniVex (the `WHY` comments and the hierarchy-resolution logic
are original and well argued). It is only the vocabulary that is Godot's.

**Blast radius: none on disk.** I checked how they persist — `scene_serializer_uve.cpp:1997-1998`,
`2008-2009` and `2074-2075` all write and read these as
`get<std::underlying_type_t<...>>()`, i.e. **by number, not by name**. So renaming the enumerators
(and the enum types) is a pure source-level change; no saved document is affected as long as the
enumerator *order* is preserved.

Files touched: 15 (`Engine/Runtime/Component/**`, `Engine/Runtime/Engine/Internal/engine_core_uve.cpp`,
`Engine/Runtime/Scene/**`, plus 5 test files).

---

### Finding B — The component layer kept the Godot names the object layer already dropped

The earlier pass renamed the **scene-object kinds** but stopped at the **components those kinds
own**. The registry maps each renamed kind onto an un-renamed Godot-named component:

| Kind (renamed ✅) | Component it owns (still Godot ❌) | Occurrences | Header |
|---|---|---|---|
| `AnimationGraph` | `AnimationTreeComponentUVE` | **175** | `Component/Expose/uve/component/animation_tree_component_uve.h` |
| `AnimationSequencer` | `AnimationPlayerComponentUVE` | **130** | `.../animation_player_component_uve.h` |
| (base of both) | `AnimationMixerComponentUVE` | **73** | `.../animation_mixer_component_uve.h` |
| `Rigid3D` | `RigidBodyComponentUVE` | **170** | `.../rigid_body_component_uve.h` |
| `Kinematic3D` | `AnimatableBody3DComponentUVE` | **23** | (declared in `ray_cast_3d_uve.h`'s module) |

`AnimationMixer` is worth calling out separately: it is a **Godot 4.3+ class name** (`scene/animation/animation_mixer.h`
exists in the `4.4-stable` tree), so this is a name that post-dates Godot 3 — it is not a leftover
from old code, it was adopted from current Godot.

This is visible to users through the registry's `runtimeOwner` strings, which are stored in the
descriptor table (`scene_object_registry_uve.cpp:50-102`):

```cpp
SceneObjectDescriptorUVE{SceneObjectKindUVE::AnimationGraph,      "animation_graph",      ..., "Scene/AnimationTreeComponentUVE",   kAnimationTreeContracts,   true},
SceneObjectDescriptorUVE{SceneObjectKindUVE::AnimationSequencer,  "animation_sequencer",  ..., "Scene/AnimationPlayerComponentUVE", kAnimationPlayerContracts, true},
SceneObjectDescriptorUVE{SceneObjectKindUVE::Kinematic3D,         "kinematic_3d",         ..., "Scene/AnimatableBody3DComponentUVE",kAnimatableBodyContracts,  true},
```

So the Add-Object list says "AnimationGraph" while the same row's runtime owner says
"AnimationTreeComponentUVE".

Related file/folder names still carrying Godot names:

```
Engine/Runtime/Animation/Expose/uve/animation/animation_tree_uve.h
Engine/Runtime/Animation/Internal/animation_tree_uve.cpp
Engine/Runtime/Component/Expose/uve/component/animation_player_component_uve.h
Engine/Runtime/Component/Expose/uve/component/animation_tree_component_uve.h
Engine/Runtime/Component/Expose/uve/component/rigid_body_component_uve.h
Engine/Runtime/Component/Internal/animation_player_component_uve.cpp
Engine/Runtime/Component/Internal/animation_tree_component_uve.cpp
Engine/Runtime/Component/Internal/rigid_body_component_uve.cpp
Engine/Runtime/Physics/Expose/uve/physics/character_body_motion_uve.h
Engine/Runtime/Physics/Internal/character_body_motion_uve.cpp
Test/Integration/Animation/animation_tree_uve_tests.cpp
Test/Physics/character_body_motion_uve_tests.cpp
```

Plus test-suite names: `AnimationTreeUVETest` (41 occurrences), `AnimationPlayerUVETest` (16),
`CharacterBodyMotionUVETest` (8), `AnimationTreeObjectKindUVE` (40).

**Measured size of this phase: 73 distinct files** across `Engine` and `Test`
(`grep -rIl --include=*.h --include=*.cpp --include=*.txt -E "AnimationTree|AnimationPlayer|AnimationMixer|RigidBodyComponentUVE|AnimatableBody3D|animation_tree|animation_player|animation_mixer|rigid_body|animatable_body|character_body" Engine Test | wc -l`),
of which **4 are build files** that list the renamed sources by path and must be edited in the same
commit or the build breaks: `Engine/Runtime/Animation/CMakeLists.txt`,
`Engine/Runtime/Component/CMakeLists.txt`, `Engine/Runtime/Physics/CMakeLists.txt`,
`Test/CMakeLists.txt`. That is what makes this the largest phase in the table — not conceptual
difficulty, just width.

**Blast radius: saved files, but already covered.** Component names are the JSON keys in a saved
document, and the repo already has the exact machinery for this — `CanonicalComponentNameUVE`
(`scene_serializer_uve.cpp:1407-1433`), whose comment states the rule: *"a component renamed in code
still loads files written before the rename; writing always uses the current name… every rename
leaves its previous name readable here forever."* Adding 5 rows there is all the migration this needs.

---

### Finding C — The `VariantUVE` type set is Godot's `Variant::Type`, including the `Packed*Array` family

`Engine/Runtime/Objects/Core/Expose/uve/object/variant_uve.h:29` and the name table at
`Engine/Runtime/Objects/Core/Internal/variant_uve.cpp:22-47`. Compared against
`core/variant/variant.h` `enum Type` at `4.4-stable`:

| UniVex type name | Godot 4.4 | Verbatim? |
|---|---|---|
| `StringName` | `STRING_NAME` | ✅ |
| `PackedByteArray` | `PACKED_BYTE_ARRAY` | ✅ |
| `PackedInt32Array` | `PACKED_INT32_ARRAY` | ✅ |
| `PackedInt64Array` | `PACKED_INT64_ARRAY` | ✅ |
| `PackedFloat32Array` | `PACKED_FLOAT32_ARRAY` | ✅ |
| `PackedFloat64Array` | `PACKED_FLOAT64_ARRAY` | ✅ |
| `PackedStringArray` | `PACKED_STRING_ARRAY` | ✅ |
| `PackedVector2Array` | `PACKED_VECTOR2_ARRAY` | ✅ |
| `PackedVector3Array` | `PACKED_VECTOR3_ARRAY` | ✅ |
| `PackedColorArray` | `PACKED_COLOR_ARRAY` | ✅ |
| `Array` / `Dictionary` / `Color` | `ARRAY` / `DICTIONARY` / `COLOR` | ✅ |
| `ObjectPath` | `NODE_PATH` | already renamed, legacy alias kept |

**9 of 9 `Packed*Array` names are verbatim Godot.** "Packed" for a typed array is Godot's coinage
specifically — Unreal uses `TArray`, Unity uses `List<T>`/native arrays. This is the single strongest
Godot fingerprint left in the codebase, and it is **user-visible**: `GetVariantTypeNameUVE` returns
these strings, and the inspector's type picker shows `GetVariantTypeCategoryUVE` = `"Packed Array"`.

**Blast radius: saved files, but the alias machinery already exists.** `variant_uve.h:26-27` states
values persist *by name, never by number*, and `TryParseVariantTypeNameUVE`
(`variant_uve.cpp:494-508`) already does exactly the `NodePath` → `ObjectPath` / `Node` → `Object`
legacy mapping. Extending that function with 10 more rows covers it.

Only 4 files reference these names: `variant_uve.h`, `variant_uve.cpp`,
`Test/Integration/Object/variant_uve_tests.cpp`, `Test/Integration/Scene/scene_serializer_uve_tests.cpp`
(65 occurrences total). Smallest surface of any finding here.

---

### Finding D — 15 of the 47 kinds are still exact Godot class names

Verified by parsing the 47 rows out of `scene_object_registry_uve.cpp` and intersecting against the
880-class list:

| # | Kind | typeId | Godot 4.4 class of that exact name |
|---|---|---|---|
| 1 | `Area3D` | `area_3d` | `Area3D` |
| 2 | `RayCast3D` | `ray_cast_3d` | `RayCast3D` |
| 3 | `NavigationRegion3D` | `navigation_region_3d` | `NavigationRegion3D` |
| 4 | `NavigationAgent3D` | `navigation_agent_3d` | `NavigationAgent3D` |
| 5 | `Skeleton3D` | `skeleton_3d` | `Skeleton3D` |
| 6 | `BoneAttachment3D` | `bone_attachment_3d` | `BoneAttachment3D` |
| 7 | `SpringArm3D` | `spring_arm_3d` | `SpringArm3D` |
| 8 | `Marker3D` | `marker_3d` | `Marker3D` |
| 9 | `Occluder3D` | `occluder_3d` | `Occluder3D` |
| 10 | `Camera3D` | `camera_3d` | `Camera3D` |
| 11 | `MeshInstance3D` | `mesh_instance_3d` | `MeshInstance3D` |
| 12 | `Light3D` | `light_3d` | `Light3D` |
| 13 | `Script` | `script` | `Script` |
| 14 | `DirectionalLight3D` | `directional_light_3d` | `DirectionalLight3D` |
| 15 | `Viewport` | `viewport` | `Viewport` |

Plus one label-only collision: kind `WorldEnvironment3D` has `displayName = "WorldEnvironment"`, and
`WorldEnvironment` **is** a Godot class (Godot's version drops the `3D`). So **16 of 47** displayed
labels are Godot class names.

For contrast, the earlier renames did work — these now have **no** Godot twin:
`Object3D` (Godot's is `Node3D`), `Static3D`, `Rigid3D`, `Character3D`, `Kinematic3D`,
`AnimationGraph`, `AnimationSequencer`, `Folder`, `Canvas`, `Hitbox3D`, `Hurtbox3D`, `Projectile3D`,
`InteractionArea3D`, `ReflectionProbe3D`, `Decal3D`, `FogVolume3D`, `LODGroup3D`, `VisibilityRegion3D`,
`SpawnPoint3D`, `LevelStreamer3D`, `WorldPartition3D`, `Collider3D`, `AudioSource3D`,
`ParticleEmitter3D`, `BoxMesh3D`, `SphereMesh3D`, `PlaneMesh3D`, `UIText`, `UIImage`, `UIButton`.

**Judgement call needed from you.** Several of these are arguably generic industry vocabulary
(`Camera3D`, `Marker3D`, `Script`, `Viewport`, `Skeleton3D`) rather than Godot-specific the way
`MeshInstance3D` or `SpringArm3D` are. This is the one finding where "remove all of it" may cost
more familiarity than it buys in distinctiveness. See the question in §5.

**Blast radius: saved files + editor UI.** `typeId` is the one layer persisted in a document, and
`FindSceneObjectDescriptorUVE(typeId)` (`scene_object_registry_uve.cpp:106+`) already carries the
legacy-id table (`empty`/`node_3d` → `Object3D`, `static_body_3d` → `Static3D`, …). Same pattern.

---

### Finding E — The scripting API exposes Godot's `is_on_floor` verbatim

`Engine/Runtime/Engine/Internal/uvscript_object_host_uve.cpp:50`:

```cpp
if (name == "is_on_floor") {
    return HostPropertyUVE{TypeUVE::BoolUVE(), false};
}
```

Godot `4.4-stable` `scene/3d/physics/character_body_3d.h:56` is `bool is_on_floor() const;` —
same name, same snake_case, same read-only semantics.

This one matters more than its size suggests because it is **script-author-facing**: every
`Character3D` script written against UniVex will contain the literal token `is_on_floor`.
`Test/UVScript/native/player.uvs:11` already does:

```
on tick(dt):
    velocity.x = input.axis("left", "right") * speed
    if is_on_floor:
```

It is also documented in the public header (`uvscript_object_host_uve.h:21`) and used in
`uvscript_host_description_uve.h:19` plus 8 places in `Test/UVScript/`.

The rest of the script surface is fine — `name`, `position`, `scale`, `velocity`,
`input.pressed/held/released/axis` are generic. The lifecycle events are `ready`, `tick`,
`animation_event` (`uvscript_object_host_uve.cpp:67-79`); `on ready:` is a nod to Godot's `_ready()`
but spelled UniVex's own way, so I'd leave it.

**Blast radius: small in code, large in intent.** There is no legacy-alias mechanism for script
property names yet, so renaming this either breaks existing `.uvs` scripts or needs a new alias
layer in `DescribePropertyUVE`. Worth deciding before the scripting runtime ships and scripts
proliferate.

---

### Finding F — A folder named after a Godot class

`Engine/Runtime/Objects/UI/Expose/uve/objects/canvas_layer/` — `CanvasLayer` is a Godot class; the
kind inside it is called `Canvas`, so the folder name is the leftover. The Godot name also appears
in prose in 5 headers ("the CanvasLayer family") and in
`Test/Objects/UI/object_definitions_canvas_layer_uve_tests.cpp` (17 occurrences of `canvas_layer`
across the tree).

**Blast radius: near zero.** Includes resolve through each library's `Expose/` root, and the folder
sits *below* that root, so a folder rename only changes the `#include` path suffix. `CanvasLayerObjectDefinitionsUVETest`
(3 tests) ran green here and is the guard.

---

### Finding G — Two stale strings the earlier `Node` pass missed (plain bugs)

1. **`Engine/Runtime/Scene/Internal/scene_serializer_uve.cpp:2099`** — a live error message still
   names a retired component:
   ```cpp
   if (!IsObjectMetadataComponentValidUVE(metadata)) {
       throw std::runtime_error("Invalid NodeMetadataComponentUVE payload");
   }
   ```
   The component is `ObjectMetadataComponentUVE` — `scene_serializer_uve.cpp:1411` even has the
   alias row mapping the old name to it. The message should say `ObjectMetadataComponentUVE`.

2. **`Engine/Editor/EditorCore/Internal/editor_panel_inspector_uve.cpp:170`** — a UI string using
   Godot's vocabulary for a feature that does not exist yet:
   ```cpp
   ImGui::TextDisabled("Signal bindings remain unavailable until the scripting runtime is added.");
   ```
   "Signal" is Godot's term. UVScript has no signal concept and its parser actively rejects the
   `signal` keyword, so this string promises something the language deliberately doesn't have.

---

### Finding H — 16 kinds still get the wrong default name (re-verified; not Godot-specific but same audit)

`NODE_NAMING_AUDIT.md` §5.1 claimed 16 kinds create their entity with the default name `Object3D`.
I re-verified this in the current code and it is **still exactly 16**.

The path: `editor_uve.cpp:1923-1930` defines

```cpp
const auto createObjectWithComponent = [this, &entityManager](auto component) {
    Scene::EntityUVE created = CreateDocumentEntityInternalUVE(EditorEntityKindUVE::Empty, std::nullopt);
    ...
```

→ `GetDefaultEntityNameUVE(Empty)` → `editor_uve.cpp:4003` returns
`Object3DObjectDefinitionUVE::defaultName` → `"Object3D"` (`object_3d_uve.h:35`).

So these 16 all appear in the Outliner as "Object3D", "Object3D.001", … no matter what they are:

`RayCast3D` (:2017) · `NavigationRegion3D` (:2028) · `NavigationAgent3D` (:2031) ·
`BoneAttachment3D` (:2038) · `Marker3D` (:2048) · `Hitbox3D` (:2051) · `Hurtbox3D` (:2054) ·
`Projectile3D` (:2057) · `InteractionArea3D` (:2060) · `ReflectionProbe3D` (:2071) · `LODGroup3D`
(:2082) · `Occluder3D` (:2085) · `VisibilityRegion3D` (:2088) · `SpawnPoint3D` (:2091) ·
`LevelStreamer3D` (:2094) · `WorldPartition3D` (:2097)

These are precisely the 16 kinds that have no `*ObjectDefinitionUVE` struct of their own — they
call `createObjectWithComponent` instead of `CreateObjectDefinitionEntityInternalUVE`. The counts
come straight out of that switch: **16** `entity = createObjectWithComponent(...)` cases,
**29** `entity = CreateObjectDefinitionEntityInternalUVE(...)` cases, and 2 cases (`Viewport`,
`SceneRoot`) that return `kInvalidEntityUVE` because the document lifecycle creates them — 47 total.
Those other 29 kinds do have a definition with a correct `defaultName`.

**This is a real user-visible bug, independent of Godot.** The fix is to give each of the 16 a real
`ObjectDefinitionUVE` (the pattern the other 31 already follow), or to thread the descriptor's
`displayName` into `createObjectWithComponent`. The second is a ~3-line change.

There is also a smaller label/default mismatch group from §5.2 of the old audit: kind `Light3D` has
`displayName = "Light3D"` but `defaultName = "Directional Light"` (`light_3d_uve.h:26`);
`BoxMesh3D` → `"Cube"`, `SphereMesh3D` → `"UV Sphere"`, `PlaneMesh3D` → `"Plane"`, `Camera3D` →
`"Camera"`, `Collider3D` → `"Collision Box"`. Those six look **intentional** (friendly names for
common primitives) and I'd keep them — but they should be a conscious decision, not drift.

---

## 4. Proposed plan

Phased so each phase lands independently green, and so the risky stuff is last.

| Phase | Content | Findings | Files | Saved-format risk | Test guard |
|---|---|---|---|---|---|
| **0** | Trivial string fixes | G | 2 | none | `SceneSerializerUVETest` |
| **1** | Rename the 3 Godot enums + their enumerators | A | 15 | **none** (persisted by number) | `SceneGraphUVETest`, `object_common_components_uve_tests`, `engine_core_uve_tests` |
| **2** | Rename `canvas_layer/` folder + "CanvasLayer family" prose | F | ~6 | none | `CanvasLayerObjectDefinitionsUVETest` |
| **3** | Fix the 16 wrong default names | H | 1–16 | none (names aren't a format) | `Object3DDefinitionsUVETest` + new per-kind assertions |
| **4** | Rename the 5 Godot-named components (+ 12 files, + test suites), add 5 alias rows to `CanonicalComponentNameUVE` | B | 73 | low — alias table | `SceneSerializerUVETest`, `AnimationTreeUVETest`, `CharacterBodyMotionUVETest` |
| **5** | Rename the `Packed*Array` + `StringName` type names, add 10 alias rows to `TryParseVariantTypeNameUVE` | C | 4 | low — alias table | `VariantUVETest` |
| **6** | Decide on `is_on_floor` (rename with a script-level alias, or keep) | E | ~12 | none on disk; breaks `.uvs` | `uvscript_vm_uve_tests`, `uvscript_parser_uve_tests` |
| **7** | Decide on the 15 Godot-kind names (all, some, or none), add alias rows to `FindSceneObjectDescriptorUVE(typeId)` | D | large | low — alias table | `SceneObjectRegistryUVETest`, `SceneObjectTypeUVETest`, editor suites (**CI only**) |

Rules I'd hold every phase to:

1. **Every rename adds its legacy alias in the same commit.** The three alias tables
   (`CanonicalComponentNameUVE`, `TryParseVariantTypeNameUVE`, `FindSceneObjectDescriptorUVE(typeId)`)
   already exist and already document this rule. No rename without its row.
2. **Never reorder an enum.** The three in Finding A persist by number.
3. **One phase per commit**, each landing on a green 192-test subset (plus full CI for editor/RHI).
4. **Update `NODE_NAMING_AUDIT.md` and `SCENE_NODES_ROADMAP.md` in the same commit** — the first is
   already stale, and a stale checklist is worse than none.

My recommendation: do **Phases 0–5** (all mechanical, all covered by tests that ran green here),
treat **Phase 6** as a decision to make before the scripting runtime ships, and treat **Phase 7**
as a genuine naming-strategy decision rather than a cleanup.

---

## 5. Questions before I touch anything

1. **Phase 7 scope.** Rename all 15 Godot-named kinds, or only the Godot-*specific* ones
   (`MeshInstance3D`, `SpringArm3D`, `BoneAttachment3D`, `NavigationRegion3D`,
   `NavigationAgent3D`, `Occluder3D`, `RayCast3D`) and keep the generic ones (`Camera3D`,
   `Marker3D`, `Script`, `Viewport`, `Skeleton3D`, `Area3D`, `Light3D`, `DirectionalLight3D`)?
2. **`Packed*Array` replacement vocabulary.** Options: `ByteArray`/`Int32Array`/… (drop "Packed"),
   or a `Buffer*` family, or `Typed*Array`. This changes what the inspector's type picker shows.
3. **`is_on_floor`.** Rename it (and add a script-level alias so existing `.uvs` keeps loading), or
   keep it as a deliberate familiarity choice? Only 1 shipped example script exists today
   (`Test/UVScript/native/player.uvs`), so this is cheapest to decide now.
4. **The ~35 comparative Godot comments** — keep (my recommendation), or strip?
5. **Finding H** — do you want the 16 default names fixed as part of this, or is that a separate
   piece of work? It's a user-visible bug either way.
