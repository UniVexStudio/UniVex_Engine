# Standalone Strategy — making UniVex its own engine, not a Godot-shaped one

**Date:** 2026-10-03
**Branch:** `arena/01a0ffb2-univex-engine`
**Companion to:** `GODOT_STYLE_AUDIT.md` (the findings), this file (what to do about them)
**Status:** PLAN ONLY. No source changed.

---

## 1. The finding that reframes everything

**UniVex's architecture is already standalone. Only its vocabulary is borrowed.**

I checked the actual structure rather than assuming:

| What I checked | File | What it is |
|---|---|---|
| Object storage | `Engine/Runtime/Entity/Internal/{archetype_uve,chunk_uve,archetype_signature_uve}.h` | **Archetype/chunk ECS** — `ArchetypeSignatureUVE` over `std::type_index`, `ChunkUVE` contiguous storage |
| Confirmed by the repo's own docs | `FOUNDATION.md:662` | "**Entity/Scene** — archetype ECS (`EntityManagerUVE`, PIMPL), stateless" |
| The hierarchy | `i_scene_graph_uve.h:30-34` | *"built on top of IEntityManagerUVE's TransformComponentUVE/HierarchyComponentUVE/WorldTransformComponentUVE **rather than a separate tree structure** (see docs — "Object3D is a thin handle over ECS data")"* |
| The scene object | `object_3d_uve.h:12-26` | a kind is a **recipe of components**, not a subclass: *"Every other kind is this plus its own components, so BoxMesh3D is Object3D plus a primitive mesh and a collider, Camera3D is Object3D plus a camera."* |

Godot is the opposite of this. Godot is an **object-oriented `Node` inheritance tree**: 880 classes,
`Node3D` → `VisualInstance3D` → `MeshInstance3D`, behaviour through virtual overrides
(`_process`, `_ready`, `_notification`), and a `SceneTree` that owns the nodes.

UniVex has **none** of that. `grep` confirms zero hits for `SceneTree`, `ClassDB`, `GDCLASS`,
`_process`, `_ready`, `_notification`, `get_node`, `add_child`, `queue_free`, `CanvasItem`,
`Callable`, `PackedScene`, `.tscn`. A "kind" in UniVex is a row in a descriptor table plus a list
of component names — `SceneObjectDescriptorUVE{kind, typeId, displayName, category, runtimeOwner,
authoredContracts, libraryCreatable}`. That is a data-driven ECS recipe. Godot has no equivalent
structure at all.

**So this is not a rewrite. It is a vocabulary replacement sitting on top of an architecture that
is already UniVex's own.** That is the cheap and good news.

### Already-standalone assets — protect these, don't "fix" them

- **Archetype/chunk ECS** with a stateless scene-graph facade (no tree object)
- **Data-driven kind registry** — 47 rows, `libraryCreatable`, `authoredContracts`, `placement`
  (World vs Entity) — Godot has no counterpart
- **UVScript** — its own language: `entity`/`fn`/`let`/`on`/`wait`/`none`, and it *actively rejects*
  GDScript's `signal` keyword (`uvscript_vm_uve_tests.cpp:463` asserts the parse **fails**)
- **Own asset formats** — `.uvanim` `.uvtex` `.uvs` `.uvsave` `.uvmat` `.uvsky` `.uvskel` `.uvnav`
- **Own conventions** — the `UVE` suffix, `Expose/`+`Internal/` per module, `Apply*ObjectDefinitionUVE`
  recipes, the "WHY" comment discipline
- **Original systems with no Godot equivalent**, already documented as such in the code:
  `VisibilityRegion3D`, `WorldPartition3D`, `LevelStreamer3D`, `LODGroup3D`, `SpringArm3D`
  smoothing, `InteractionArea3D`'s one-focus-per-player rule, `ReflectionProbe3D`'s
  influence-box/time-sliced capture

These are the differentiators. Every phase below must leave them untouched.

---

## 2. The four layers, and what "no copy" means at each

Borrowing happens at four depths. Cost and payoff rise together, and **only Layer 1 is mechanical**.

| Layer | What it is | UniVex today | Work to make it standalone |
|---|---|---|---|
| **1. Vocabulary** | type names, enum names, kind names, folder names | ❌ **borrowed** — the 8 findings in `GODOT_STYLE_AUDIT.md` | Mechanical rename + alias rows. Tests already green. |
| **2. API surface** | what a script/programmer actually types | ⚠️ **mostly own**, one leak: `is_on_floor` | Small, but needs a decision before scripts proliferate |
| **3. Architecture** | how objects, hierarchy, updates, ownership work | ✅ **already own** (archetype ECS) | **None** |
| **4. Concepts** | which systems exist and what they do | ✅ **already own**, several beyond Godot | **None** — keep extending |

The honest conclusion: **Layer 1 is ~90% of the visible problem and ~10% of the cost.** Layers 3 and
4 are already done. Layer 2 is one identifier.

---

## 3. Layer 1 — a proposed UniVex vocabulary

The naming rule I'd adopt, so future additions don't re-borrow:

> **Name the thing for what it does in UniVex's own systems, not for what another engine calls it.**
> Where UniVex already has its own word for a thing somewhere in the tree, that word wins.

That second clause does a lot of work, because **UniVex already coined better names in several
places and just never propagated them.** Evidence from the registry's own `runtimeOwner` column
(the engine's own description of who implements the kind):

| Kind (= Godot name) | Its own `runtimeOwner` in `scene_object_registry_uve.cpp` | Proposal | Why |
|---|---|---|---|
| `MeshInstance3D` | **`Render/MeshRendererUVE`** | **`MeshRenderer3D`** | The repo *already* calls it a MeshRenderer. Free. |
| `Light3D` | `Render/LightSystemUVE`; `LightEmitterComponentUVE` already exists (23 uses) | **`LightEmitter3D`** | Component name already coined. |
| `Viewport` | `Scene/Editor`; enum comment: *"The level at the top of the Outliner"* | **`Level`** | The comment already calls it a level. |
| `Area3D` | `Physics/AreaOverlapSystemUVE` | **`OverlapVolume3D`** | Names the behaviour, not Godot's noun. |
| `RayCast3D` | `Physics/RaycastSystemUVE` | keep **`RayCast3D`** | `Raycast` is generic CG, and the system is already `RaycastSystemUVE`. |
| `SpringArm3D` | `Physics/RaycastSystemUVE` | **`CameraBoom3D`** | "Boom" is the film-crane term, older than Godot's usage. |
| `Marker3D` | `Scene/Marker3DComponentUVE`; `editor_uve.h:922` calls it *"a viewpoint the viewport can fly into"* | **`Viewpoint3D`** | The editor code already describes it as a viewpoint. |
| `BoneAttachment3D` | `Scene/BoneAttachment3DComponentUVE` | **`BoneSocket3D`** | "Socket" is generic industry (Unreal), not Godot's. |
| `NavigationRegion3D` | `Scene/NavigationRegion3DComponentUVE` | **`NavMeshVolume3D`** | Says what it holds. |
| `NavigationAgent3D` | `Scene/NavigationAgent3DComponentUVE` | **`NavSeeker3D`** | Says what it does. |
| `Occluder3D` | `Render/Occluder3DComponentUVE` | keep **`Occluder3D`** | "Occluder" is standard CG vocabulary. |
| `Script` | `Scripting/ScriptRuntimeUVE` | keep **`Script`** | Universal; renaming costs familiarity for nothing. |
| `Camera3D` | `Render/CameraSystemUVE` | keep **`Camera3D`** | Universal. |
| `Skeleton3D` | `Scene/Skeleton3DComponentUVE` | keep **`Skeleton3D`** | Universal. |
| `DirectionalLight3D` | `Render/LightSystemUVE` | **`SunLight3D`** or keep | Registry comment already says *"The sun"*. |
| `WorldEnvironment3D` | `Render/WorldEnvironment3DComponentUVE` | keep kind, **fix the label** | Label `"WorldEnvironment"` is the Godot collision; kind already differs. |

That gives **8 renames, 8 keeps** — not a blanket sweep. I deliberately keep `Camera3D`,
`Skeleton3D`, `Script`, `Occluder3D`, `RayCast3D`: those are computer-graphics words that predate
and outlive Godot, and renaming them makes UniVex *less* familiar without making it more original.

### The component layer (Finding B) — the biggest block

| Now (Godot) | Proposed | Note |
|---|---|---|
| `AnimationTreeComponentUVE` (175) | `AnimationGraphComponentUVE` | The kind is **already** `AnimationGraph`. Pure propagation. |
| `AnimationPlayerComponentUVE` (130) | `AnimationSequencerComponentUVE` | Kind is **already** `AnimationSequencer`. |
| `AnimationMixerComponentUVE` (73) | `AnimationBlendBaseComponentUVE` or `AnimationLayerComponentUVE` | `AnimationMixer` is a **Godot 4.3+** class — the newest borrow in the tree. Needs a real coinage. |
| `RigidBodyComponentUVE` (170) | `Rigid3DComponentUVE` | Kind is **already** `Rigid3D`. |
| `AnimatableBody3DComponentUVE` (23) | `Kinematic3DComponentUVE` | Kind is **already** `Kinematic3D`. |

Note the pattern: **4 of the 5 are just catching the components up to renames the kinds already
had.** Only `AnimationMixerComponentUVE` needs a genuinely new word.

### The enums (Finding A) — pure coinage, zero format risk

These persist **by number** (`scene_serializer_uve.cpp:1997`, `2008`, `2074` all use
`get<std::underlying_type_t<...>>()`), so renaming is source-only. Do not reorder.

| Now (= Godot) | Proposed | Enumerators |
|---|---|---|
| `ProcessModeUVE` | `TickModeUVE` | `Inherit, Running, PausedOnly, Always, Never` (was `Pausable, WhenPaused, …, Disabled`) |
| `PhysicsInterpolationModeUVE` | `PoseSmoothingUVE` | `Inherit, Blended, Exact` (was `On, Off`) |
| `AutoTranslateModeUVE` | `LocalizeModeUVE` | `Inherit, Localized, Literal` (was `Always, Disabled`) |

### The `Packed*Array` family (Finding C) — the strongest fingerprint

All 9 names are verbatim Godot (`PACKED_BYTE_ARRAY` … `PACKED_COLOR_ARRAY` in
`core/variant/variant.h`). "Packed" for a typed array is Godot's own coinage. Three options:

| Option | Example | Feel |
|---|---|---|
| **A. Drop "Packed"** | `ByteArray`, `Int32Array`, `Vector3Array` | Smallest diff, still reads naturally |
| **B. `Buffer` family** | `ByteBuffer`, `Float32Buffer` | Most distinct, implies contiguous storage (true here) |
| **C. `Typed` family** | `TypedByteArray` | Explicit, but wordy in the inspector |

I'd pick **A** — `StringName` → `InternedString` alongside it, which is the accurate technical term
for what it is.

---

## 4. Layer 2 — the one real API leak

`is_on_floor` (`uvscript_object_host_uve.cpp:50`) is verbatim Godot
(`character_body_3d.h:56`, `4.4-stable`), and it is **what script authors type**.

Everything else on that surface is generic: `name`, `position`, `scale`, `velocity`,
`input.pressed/held/released/axis`. Events are `ready`/`tick`/`animation_event` — UniVex's own
spelling of the concept.

Proposal: `is_on_floor` → **`grounded`** (one word, says the state rather than the query), with a
read-only alias in `DescribePropertyUVE` so existing `.uvs` keeps loading. Only **one** shipped
script uses it today (`Test/UVScript/native/player.uvs:11`), so this is cheapest to do now and
expensive to do later.

---

## 5. The ratchet — how it *stays* standalone

A rename pass decays. New code re-borrows a name in six months and nobody notices. The repo already
solves exactly this shape of problem elsewhere: `Engine/Tools/check_math_boundary.py` is a text
lint that **runs as its own CI step before the build** (`.github/workflows/ci.yml`, step
"Math boundary check"), and `Engine/Tools/check_panel_includes.sh` does the same for includes.

So: add **`Engine/Tools/check_engine_vocabulary.py`**, same shape, same CI slot. It holds:

1. **A denylist** of the ~40 foreign class names (`AnimationTree`, `AnimationPlayer`,
   `AnimationMixer`, `RigidBody`, `MeshInstance`, `SpringArm3D`, `BoneAttachment3D`,
   `Marker3D`, `NavigationRegion3D`, `NavigationAgent3D`, `PackedByteArray`, `StringName`, …).
   Any new occurrence in `Engine/**` fails CI.
2. **An allowlist** for the deliberate exceptions — the legacy alias tables in
   `scene_serializer_uve.cpp` / `variant_uve.cpp` / `scene_object_registry_uve.cpp`, which *must*
   keep the old strings forever to load old files, and the ~35 comparative design comments that
   name Godot precisely to explain a difference.
3. **Exit non-zero printing every violation**, exactly like `check_math_boundary.py`.

This is the part that actually answers "how do we stay standalone" rather than "how do we clean up
once". Without it, every phase below is temporary.

### Side finding while reading the existing tooling

`Engine/Tools/check_panel_includes.sh:14` begins with a hardcoded absolute path:

```bash
cd /home/user/UNIVEX
```

That directory does not exist (the repo root here is `/home/user/UniVex_Engine`). The script has no
`set -e`, so the failed `cd` prints an error and execution **continues in the caller's working
directory** — meaning the script only works when invoked from the repo root, and silently does the
wrong thing from anywhere else.

I ran it from the repo root to see what it actually reports:

```
$ bash Engine/Tools/check_panel_includes.sh ; echo exit=$?
Engine/Tools/check_panel_includes.sh: line 14: cd: /home/user/UNIVEX: No such file or directory
MISSING <cstdint> for std::uint32_t in editor_panel_anim_graph_uve.cpp
...
exit=1
```

**26 real `MISSING` violations, exit code 1.** The script is working as designed — it correctly
signals failure. The problem is that it is **not wired into CI**: the only tool step in
`.github/workflows/ci.yml` is `check_math_boundary.py` (line 46). That is why 26 violations are
sitting in the tree unnoticed.

(Correction: my first draft of this section claimed the bad `cd` made the check "pass by finding no
files". That was wrong, and I only believed it because I had piped the script through `head` and read
`head`'s exit code. The script does fail; it just isn't being listened to.)

Two fixes worth folding into Phase 9, since we are touching this area anyway: derive the root the way
`check_math_boundary.py` does (`check_math_boundary.py:53` takes an optional `[repo_root]` argument
and defaults to `Path(__file__).resolve().parents[2]` — it passes cleanly here), and add both scripts
as CI steps. Wiring this one up means triaging those 26 violations first, so it may need its own
small pass.

---

## 5b. What has actually landed (2026-10-03)

Phases 0, 1, 2 and 9 are **done on this branch**, not proposed. 44 files changed,
`220 insertions(+), 220 deletions(-)` — a rename pass, no behaviour change.

| Phase | What changed | Verified by |
|---|---|---|
| **0** | `scene_serializer_uve.cpp:2099` error message `NodeMetadataComponentUVE` → `ObjectMetadataComponentUVE`. The editor's Godot-vocabulary `Signals` tab → `Events` (2 enum values, 3 tab labels, 1 settings entry, 3 comments) — `Events` is the engine's own module name (`Engine/Runtime/Events`, `EventSystemUVE`). | `SceneSerializerUVETest`; 4 editor TUs syntax-checked |
| **1** | `ProcessModeUVE` → `TickModeUVE` (`Inherit, Running, PausedOnly, Always, Never`); `AutoTranslateModeUVE` → `LocalizeModeUVE` (`Inherit, Localized, Literal`); `PhysicsInterpolationModeUVE` → `PoseSmoothingUVE` (`Inherit, Blended, Exact`); `IsProcessingUVE` → `IsTickingUVE`; the `Resolve*` functions; the inspector dropdown labels; 4 test names. **Enumerator order preserved** — these persist by number. | `ResolveTickModeUVE_*`, `IsTickingUVE_*`, `ResolveLocalizeModeUVE_*`, `UpdateUVE_ResolvesTickMode*` all ran green; by-number round-trip asserted in `CaptureThenRestore_AllRegisteredComponentTypes_RoundTrip` at lines 308/320/332 |
| **2** | Folder `objects/canvas_layer/` → `objects/canvas/`; `all_objects_canvas_layer_uve.h` → `all_objects_canvas_uve.h`; test file renamed; CMake targets `uve_nodes_3d` → `uve_objects_3d` and `uve_nodes_canvas_layer` → `uve_objects_canvas` (the "nodes" in those target names was itself a leftover from the Node→Object pass); "CanvasLayer family" prose → "Canvas family"; two stale `SceneNodeKindUVE` doc references corrected. | `CanvasObjectDefinitionsUVETest` ran green |
| **9** | New `Engine/Tools/check_engine_vocabulary.py` — 15 retired names hard-enforced, 29 foreign class names reported but not enforced (see §5). Wired into `.github/workflows/ci.yml` as a step after the math boundary check. | **Proven to fail:** injecting `struct ProcessModeUVE {};` gave `exit=1`; removing it gave `exit=0` |

Also fixed while in the area: `Engine/Tools/check_panel_includes.sh:14` had `cd /home/user/UNIVEX`
(a path from another machine). It now derives the repo root from `${BASH_SOURCE[0]}`, so it works
from any directory. It still reports **26 real violations** and exits 1 — so it is deliberately
**not** wired into CI yet; triaging those 26 is its own small pass, and adding a failing lint to CI
would just block every unrelated PR.

### Also landed the same session: Phase 3 and two real bug classes

**Phase 3 — the 16 mis-named kinds (Finding H), fixed.** `createObjectWithComponent` in
`editor_uve.cpp` routed through `EditorEntityKindUVE::Empty`, which resolves to
`Object3DObjectDefinitionUVE::defaultName`, so a freshly added `RayCast3D`, `Marker3D` and
`LevelStreamer3D` all appeared in the Outliner as "Object3D", "Object3D 2", "Object3D 3". It now
takes the name from the registry's `displayName` and still passes it through
`MakeUniqueDocumentEntityNameUVE`, matching the 29 kinds that do have an `ObjectDefinition`. Also
added the missing `#include <optional>` to that TU (it used `std::optional` in 57 places and got the
header transitively — the exact failure mode the panel lint exists to catch).

Locked by a new test, `EditorUVETest.ComponentOnlySceneObjectsUVE_AreNamedForTheirOwnKindNotObject3D`,
which creates all 16 kinds, asserts each is named for its own kind, and asserts the second of a kind
is suffixed rather than duplicated. No pre-existing test created any of the 16, which is how the bug
survived this long.

**The 26 missing standard includes, fixed.** `check_panel_includes.sh` was reporting 26 real
violations across 11 panel TUs — `<cstddef>`, `<cstdint>`, `<utility>`, `<functional>`, `<optional>`,
`<string_view>`, `<system_error>` used but never included, borrowed transitively through `imgui.h`.
Each compiles today and each is one header-reshuffle away from not compiling. All 26 are fixed; the
script now reports `include audit: clean` and is wired into CI as a third check.

`check_panel_includes.sh` itself was also broken independently of its findings: `cd /home/user/UNIVEX`
is a path from another machine, and with no `set -e` the failed `cd` left the script running in the
caller's working directory. It now derives the repo root from `${BASH_SOURCE[0]}`.

### Phase 4 — Finding B, the four components, and a latent ordering bug it exposed

The four components that kept the name their scene-object kind had already dropped are renamed, so
the Add-Object list and the row it creates finally agree:

| Was | Now | Kind it matches |
|---|---|---|
| `AnimationTreeComponentUVE` | `AnimationGraphComponentUVE` | `SceneObjectKindUVE::AnimationGraph` |
| `AnimationPlayerComponentUVE` | `AnimationSequencerComponentUVE` | `SceneObjectKindUVE::AnimationSequencer` |
| `RigidBodyComponentUVE` | `Rigid3DComponentUVE` | `SceneObjectKindUVE::Rigid3D` |
| `AnimatableBody3DComponentUVE` | `Kinematic3DComponentUVE` | `SceneObjectKindUVE::Kinematic3D` |

That is the tip of it. The same rename carried the whole derived families — roughly forty symbols,
`AnimationTreeObjectUVE`, `IsAnimationTreeComponentValidUVE`, `SetAnimationTreeParameterUVE`,
`StepSkeletalAnimationPlayerUVE`, `EvaluateAnimationTreeUVE` and the rest — plus nine files
(`animation_graph_uve.{h,cpp}`, `animation_sequencer_component_uve.{h,cpp}`, `rigid_3d_component_uve.{h,cpp}`,
the three component headers and the integration test) and the CMake lists that name them.

**Documents written before this still load.** The four old names went into `CanonicalComponentNameUVE`
alongside the existing `*NodeComponentUVE` rows, and the live registration keys, the registry's
`authoredContracts`/`runtimeOwner` strings and the `kAnimationTreeContracts`-family constants all
moved together. The legacy `typeId` aliases (`"rigid_body_3d"`, `"animation_player"`, `"animation_tree"`)
and the stable drawer ids (`"component.rigid_body"`, …) were deliberately **left alone** — a drawer id
is persisted in editor layout, and the labels beside them were updated instead.

**The rename broke a real bug into the open, and it is now fixed.** `SceneSerializerUVE::RestoreUVE`
carried an undocumented dependency on *alphabetical key order*. Its comment claimed "keys are read in
name order, so a saved mixer is already here", and the migration branch that synthesises a base
`AnimationMixerComponentUVE` for a pre-mixer document relied on it: `AnimationMixer` sorted ahead of
`AnimationPlayer` and `AnimationTree`, so the real mixer was always visited first. Renaming those two
put **`AnimationGraph` ahead of `AnimationMixer`**, so a graph now synthesised a throwaway mixer and
the real one collided with it — `RestoreUVE_AnimationTargetsRemapToTheRestoredEntities` trapped in
`UVE_DEBUG_BREAK`. The fix removes the ordering assumption rather than reordering the names: the loop
reads `documentHasMixer` once, and the migration branch only fires when the document genuinely has no
mixer of its own. Both sides stay covered — `RestoreUVE_AnimationTargetsRemapToTheRestoredEntities`
(a document with mixer + graph + sequencer) and `RestoreUVE_LegacyAnimationSequencerFieldsCarryOver`
(a sequencer with no mixer, which asserts `HasComponentUVE<AnimationMixerComponentUVE>` — "an old
player gains its base").

**One deliberate non-rename, and it needs your call.** `Engine/Runtime/Animation/Expose/uve/animation/`
holds a *second*, older animation library in `UVE::Core` — `AnimationGraphObjectKindUVE` there is
`{ClipPlayer, Blend, Parameter, State, Transition, OneShot, TimeScale, Sync, Subtree, PoseCache,
OutputPose}` with string `clipId` and `inputA`/`inputB`, while `UVE::Scene`'s is `{Output, Clip,
Blend2, BlendSpace1D, Additive, OneShot, TimeScale, …}` with `blendPoints` and `bones`. Two
generations of one idea now share a name across two namespaces. It compiles and no TU includes both,
but it is exactly the kind of trap the rest of this work removes. Renaming Core to something distinct
— `PoseGraph*`, or folding it into the Scene model — is a decision about which of the two survives,
not a mechanical rename, so it is flagged rather than guessed at.

The vocabulary guard gained a **stem** tier for this: `RETIRED_STEMS` matches an identifier *prefix*,
so one entry retires a whole family instead of listing forty symbols. Proven to fail — injecting
`struct AnimationTreeThingUVE {};` and `struct RigidBodyProbeUVE {};` yields exit 1 with both flagged;
removing them returns to exit 0. The four component names also left `FOREIGN_CLASS_NAMES`, which is
why the reported foreign count fell from 1017 to 532: they are enforced now, not merely counted.

### Phase 5 — Finding C, the `Packed*Array` family and `StringName`

This was the strongest fingerprint in the engine: all nine `Packed*Array` names present, in Godot's
own order, minus `PACKED_VECTOR4_ARRAY` — plus `StringName`. No other engine names a contiguous typed
array this way (Unreal: `TArray<T>`, Unity: `List<T>`). They are now named for what they hold, and
the element type carries the meaning without a prefix:

| Was | Now | | Was | Now |
|---|---|---|---|---|
| `PackedByteArray` | `ByteArray` | | `PackedStringArray` | `StringArray` |
| `PackedInt32Array` | `Int32Array` | | `PackedVector2Array` | `Vector2Array` |
| `PackedInt64Array` | `Int64Array` | | `PackedVector3Array` | `Vector3Array` |
| `PackedFloat32Array` | `Float32Array` | | `PackedColorArray` | `ColorArray` |
| `PackedFloat64Array` | `Float64Array` | | `StringName` | `InternedString` |

The picker category `"Packed Array"` became `"Typed Array"`, which says what actually distinguishes
them from `Array` — homogeneous and typed, not heap-boxed — without borrowing the other engine's word.

**Variant types are persisted by name, so this is the highest format-risk phase so far, and it is
handled the way the two earlier renames were.** `TryParseVariantTypeNameUVE` already carried
`NodePath`→`ObjectPath` and `Node`→`Object` as `if` statements; those two plus the ten new pairs are
now one `constexpr` table, because a list that grows one branch per rename stops being readable.
Reading accepts every retired name; writing only ever emits the current one.

The header used to claim a type name "once shipped, is never changed". That was already false the
moment `NodePath` became `ObjectPath`, so the comment now states the actual invariant: a rename is a
load-time alias, never a silent break.

**The alias guarantee was untested, and now is not.** No existing test called
`TryParseVariantTypeNameUVE` with a retired name — including the two that had shipped that way.
`VariantUVETest.RetiredTypeNamesStillParseButAreNeverWritten` covers all twelve pairs and asserts
both directions: the old name parses to the type it always meant, *and* `GetVariantTypeNameUVE` never
hands the old name back, so the alias cannot quietly become load-bearing again. Proven to have teeth —
deleting the `PackedVector3Array` row makes it fail at `variant_uve_tests.cpp:49`.

`variant_uve.cpp` gained the `#include <utility>` its new `std::pair` table needs; it had been
relying on a transitive include, the same failure mode as the 26 panel headers in Phase 10, in a
non-panel TU again.

### Phase 6 — Finding B's last component, named by the engine's own convention

`AnimationMixerComponentUVE` was the one component that needed a coinage rather than a catch-up, so
it got one from the tree instead of from a guess. It sits in the Inspector's `kSectionOrderObjectBaseUVE`
group, and every other member of that group is a base component named for what it makes an object:

| Type | Inspector label |
|---|---|
| `PhysicsObjectComponentUVE` | `PhysicsObject3D` |
| `SolidBody…` | `SolidBody3D` |
| `RenderInstance…` | `RenderInstance3D` |
| `SurfaceInstance…` | `SurfaceInstance3D` |
| `LightEmitter…` | `LightEmitter3D` |
| `BoneModifier…` | `BoneModifier3D` |
| ~~`AnimationMixerComponentUVE`~~ | ~~`AnimationMixer`~~ ← the only one breaking the pattern, and a verbatim foreign class |

The component's own doc comment already said what it is: *"the abstract base AnimationSequencer and
AnimationGraph share — what they move, which channels, on which clock and how fast."* It never mixed
anything; the name was borrowed. It is now `AnimatedObjectComponentUVE` with the label
`"AnimatedObject3D"`, which follows the group exactly, and `ApplyAnimationMixerBaseUVE` became
`ApplyAnimatedObjectBaseUVE` — the word "base" was already in the function's name.

20 files, ~113 occurrences (`AnimationMixerComponentUVE` 78, bare `AnimationMixer` 16,
`IsAnimationMixerComponentValidUVE` 8, `ApplyAnimationMixerBaseUVE` 6, `AnimationMixerFromJsonUVE` 3,
plus the ObjectDefinition and one test name). Four spots needed handling by hand rather than by `sed`:

- `AnimatedObject3DObjectDefinitionUVE`, not `AnimatedObjectObjectDefinitionUVE` — the ObjectDefinition
  structs are named after the *label*, matching `BoneModifier3DObjectDefinitionUVE`.
- `typeName` and the Inspector label both take the `3D` suffix; a blanket replace would have left them
  `"AnimatedObject"` and out of step with the group.
- `RestoreUVE_PlayerSavedBeforeAnimationMixerMovesItsSettingsIntoTheMixer` became
  `RestoreUVE_SequencerSavedBeforeTheAnimatedObjectBaseMovesItsSettingsIntoOne` — the old name was a
  sentence with two occurrences of the word, and a blind replace made it unreadable.
- The Phase 4 comment in `RestoreUVE` that explains the ordering bug names `"AnimationMixer"`
  **on purpose**: that is the name the code actually had when the bug existed, so it stays.

`component.animation_mixer` — the drawer id — was left alone, like every other drawer id in this work:
it is persisted in editor layout. `AnimationMixerComponentUVE` went into `CanonicalComponentNameUVE`,
so documents written before this load unchanged.

**A first proposal for this was wrong and was rejected.** `AnimationPlaybackComponentUVE` was proposed
before reading `animation_sequencer_uve.h`, which already uses "Playback" as the section heading for
`PlayAnimationSequencerUVE` / `StopAnimationSequencerUVE` / `StepAnimationSequencerUVE`. Two different
things sharing one word is precisely the confusion this whole pass exists to remove. The lesson is
recorded here because it is the mechanism, not the slip: a coinage has to be checked against the
vocabulary already in the tree before it is offered, the same way a rename target is.

### Verification actually run

```
cmake --build /tmp/sbuild --target uve_audit_subset_tests -j 2      # 0 errors
./uve_audit_subset_tests
[==========] 264 tests from 38 test suites ran.
[  PASSED  ] 264 tests.

python3 Engine/Tools/check_math_boundary.py         → math boundary check passed   (exit 0)
python3 Engine/Tools/check_engine_vocabulary.py     → 25 retired names + 5 stems, 0 reintroductions (exit 0)
bash    Engine/Tools/check_panel_includes.sh        → include audit: clean         (exit 0)
```

Every file this pass touched is either compiled-and-tested by that subset, or — for the graphics,
editor and RHI files the subset cannot link — individually syntax-checked with
`g++ -fsyntax-only -std=c++23` against the real Expose include tree plus fetched imgui/GLFW/Mesa GL
headers. All clean: `editor_uve.h`, `editor_uve.cpp`, `editor_panel_inspector_uve.cpp`,
`editor_panel_inspector_metadata_uve.cpp`, `editor_entity_editor_uve.cpp`, `editor_settings_uve.cpp`,
`engine_core_uve.cpp`, `engine_core_uve_tests.cpp`, `mesh_renderer_uve_tests.cpp`,
`editor_uve_tests.cpp` (the new Phase 3 test), and all 11 panel TUs that gained an include. A full
link of the editor and RHI targets still needs CI, because this sandbox has no GLFW/Vulkan/GLEW/X11
libraries and apt has no network egress — so **the Phase 3 fix is syntax-verified and test-written
here, but the test itself first executes on CI.**

---

## 6. Sequencing

Remaining phases, after §5b. **0–6, 9 and 10 are already landed** — this table is what is
left.

| Phase | Content | Files | Format risk | Guard |
|---|---|---|---|---|
| ~~**0**~~ | ~~2 stale strings + the `Signals` tab~~ | ~~2~~ | — | **DONE, see §5b** |
| ~~**1**~~ | ~~3 enums (§3)~~ | ~~15~~ | — | **DONE, see §5b** |
| ~~**2**~~ | ~~`canvas_layer/` folder → `canvas/`, plus the `uve_nodes_*` target names~~ | ~~6~~ | — | **DONE, see §5b** |
| ~~**3**~~ | ~~Fix the 16 kinds whose Outliner name is `"Object3D"`~~ | ~~2~~ | ~~none~~ | **DONE, see §5b** |
| ~~**4**~~ | ~~4 component renames that just catch up to existing kind names~~ | ~~64~~ | ~~low~~ | **DONE, see §5b** |
| ~~**5**~~ | ~~`Packed*Array` + `StringName` (§3 option A)~~ | ~~5~~ | ~~low~~ | **DONE, see §5b** |
| ~~**6**~~ | ~~`AnimationMixerComponentUVE` — the one needing a new coinage~~ | ~~20~~ | ~~low~~ | **DONE, see §5b** |
| **7** | 8 kind renames + label fixes (§3) | large | low | `SceneObjectRegistryUVETest` + **CI** for editor |
| **8** | `is_on_floor` → `grounded` + script alias | ~12 | breaks `.uvs` without alias | `uvscript_vm_uve_tests` |
| ~~**9**~~ | ~~`check_engine_vocabulary.py` + CI step~~ | ~~2~~ | — | **DONE, see §5b** |
| ~~**10**~~ | ~~Triage the 26 `check_panel_includes.sh` violations, then wire that script into CI~~ | ~~13~~ | ~~none~~ | **DONE, see §5b** |
| **11** | Resolve the two `AnimationGraph*` libraries in `UVE::Core` vs `UVE::Scene` (see §5b) | ~4 | none | `AnimationGraphUVETest` |

Rules for every phase: add the legacy alias in the **same commit**; never reorder an enum; one phase
per commit; update `NODE_NAMING_AUDIT.md` and `SCENE_NODES_ROADMAP.md` in the same commit (the first
is already stale).

**Baseline each phase must land on:** the 264-test subset documented in §5b (it was 192 before
Phases 3–4 added their own coverage), plus full CI for anything touching `Engine/Editor/**` or RHI
(those suites cannot run in this sandbox — no GLFW/Vulkan/GLEW/X11 headers and no apt network).

Phases 0–6 are ~75% of the borrowed surface and all mechanical. Phase 9 is the one that makes it
stick.

---

## 7. Decisions I need from you

1. **Kind renames: the 8/8 split in §3, or all 15, or none?** (My pick: the 8/8 split.)
2. **`Packed*Array` → option A / B / C?** (My pick: A, plus `StringName` → `InternedString`.)
3. **`AnimationMixerComponentUVE` → what?** This is the only one with no answer already in the tree.
   Candidates: `AnimationBlendBaseComponentUVE`, `AnimationLayerComponentUVE`, `AnimationStageComponentUVE`.
4. **`is_on_floor` → `grounded`?** Or keep as a deliberate familiarity choice.
5. **Enum renames in §3 — accept `TickModeUVE`/`PoseSmoothingUVE`/`LocalizeModeUVE`,** or propose your own?
6. **Build the CI guard (Phase 9)?** Strongly recommended — it's the difference between a cleanup and
   a policy.
7. **Start where?** I'd start at Phase 0+1 (2 files + 3 enums, zero format risk) so you can see the
   shape of a landed phase before committing to the wide ones.
