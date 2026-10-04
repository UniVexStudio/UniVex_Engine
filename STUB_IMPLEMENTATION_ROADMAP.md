# Stub Implementation Roadmap

A working tracker for every scene node that already owns its `.h` + `.cpp` pair but whose
**component fields and arrays still receive no real work at runtime** — plus the declared
honest gaps of nodes that are otherwise real. This is the drill-down companion to
`SCENE_NODES_ROADMAP.md`: that file keeps the high-level `[x]`/`[/]`/`[~]`/`[ ]` status per
node (four levels — see that file's "How to read" section for why `[/]` exists as a
step short of `[x]`); THIS file is the per-item implementation checklist we tick off as
each one gets built.

Scope: the 3D nodes still at `[~]` (Decal3D, the Navigation pair, the
Skeleton/animation group), and the one item that is explicitly half-open (ReflectionProbe3D — its
sync half is done, the renderer-side sampling is not). 2D/UI/AI nodes have no files yet, so
they have nothing to track here — they stay in `SCENE_NODES_ROADMAP.md`'s missing-entirely
sections until they exist. An item that reaches `[x]` with every box ticked is retired to the
Done section at the bottom, row and all.

## How to read this file

Every entry below names:

- **Component** — the authored struct that will receive the work (and where it lives).
- **Fields / arrays to give work** — the exact authored members, with arrays called out
  explicitly, that a real system must consume. An entry is NOT done while any listed member
  is still ignored.
- **The work** — the real system/behavior to build, and where it should live (engine-core sync
  for per-frame node behavior, matching the RayCast3D/Projectile3D/Hitbox3D precedent; a real
  subsystem when the gap is bigger than one node).
- **Depends on** — what must exist first. Items sharing a dependency are grouped.
- **Size** — S (one sync + tests), M (cross-system integration), L (new subsystem/pipeline).

Rules, same spirit as `SCENE_NODES_ROADMAP.md`:

- Update this file in the same change that implements (or re-scopes) an item.
- An item's boxes are only ticked when a real, ticked system consumes every listed
  field/array AND tests lock the behavior — not when data merely round-trips.
- Keep `SCENE_NODES_ROADMAP.md`'s `[~]` → `[x]` flip in sync with the last box of an item.
- No third-party engine or product name appears anywhere in this document.

## Summary table (suggested order)

Status column uses the same `[x]`/`[/]`/`[~]` marks as `SCENE_NODES_ROADMAP.md`: `[~]` still
authored-data-only (nothing below has moved yet), `[/]` a real sync function is wired and
called every frame but lacks dedicated behavior tests, `[x]` wired and locked by dedicated
tests. An item's row here is retired to "Done" (bottom of file) only once it reaches `[x]`
**and** the boxes below its write-up are all ticked.

| # | Object | Status | Component | Arrays to give work | The work | Depends on | Size |
|---|------|--------|-----------|---------------------|----------|------------|------|
| — | *done items* | — | — | — | rows retired to the Done section at the bottom: 1–11, 15 (12 stays — the renderer-side half is still open). Their numbered write-ups stay below as the audit trail. | — | — |
| 12 | ReflectionProbe3D | `[x]` (sync half) | `ReflectionProbe3DComponentUVE` | — | probe capture scheduling — **done**, five dedicated tests; renderer-side sampling still `[~]` | renderer (big) | L |
| 13 | NavigationRegion3D + NavigationAgent3D | `[~]` | `NavigationRegion3DComponentUVE` / `NavigationAgent3DComponentUVE` | — | navmesh bake + pathfind + steer | new Navigation subsystem | L |
| 14 | Skeleton3D + BoneAttachment3D + AnimationSequencer + AnimationGraph | `[~]` | `Skeleton3DComponentUVE`, `BoneAttachment3DComponentUVE`, `AnimationPlayerComponentUVE`, `AnimationTreeUVE` | `bones` vector | clip sampling → bone pose → skinning | new Animation pipeline | L |
| — | Marker3D | — | `Marker3DComponentUVE` | — | **none, by design** — read by tools/scripts, never ticked | — | — |

---

## S-tier — one engine-core sync each (start here)

Items 1-4 and 9 have reached `[x]` with every box ticked, so their rows are retired to the Done
section at the bottom; the numbered write-ups below remain as the audit trail of what was actually
built. Rows 10, 12 and 15 were already `[x]` when this file's retirement rule was last applied and
are left in place here rather than re-litigated in an unrelated change.

### 1. SpringArm3D — camera-boom raycast clamp — DONE

- [x] Implement — `EngineCoreUVE::SyncSpringArm3DObjectsUVE()` calls
      `Physics::StepSpringArm3DUVE()` (`Engine/Runtime/Physics/Internal/spring_arm_uve.cpp`) once
      per arm per fixed step, in fixed-step order. The step is where the behaviour lives, so it can
      be tested against the real raycast system and the real scene graph: it casts from the pivot
      along the arm's own local +Z (the camera convention looks down -Z, so this is behind the
      pivot), resolves the target through `Scene::ResolveSpringArm3DTargetUVE` (full reach when
      clear, hit distance minus `margin` when not, clamped to the authored envelope), commits it
      through `Scene::ResolveSpringArm3DLengthUVE` (retraction snaps so a camera never clips for one
      smooth frame's sake; extension springs back at `smoothing`/s; `smoothing = 0` reproduces
      Godot's snap-both-ways exactly), and shifts every direct child by the change in length along
      the arm's own Z. Two things changed while extracting it: a step duration the arm cannot honour
      is refused instead of half-run, and an arm that is switched off now hands its length back
      rather than freezing wherever it happened to be - disabling a boom must not strand a camera
      inside a wall.
- [x] Tests lock it — 17 dedicated step tests in `Test/Physics/spring_arm_uve_tests.cpp`: shortens
      behind geometry by the margin, never past zero at the pivot, mask filters, never hits itself,
      casts along its own axis rather than the world's, disabled stops casting and comes home (in one
      step at `smoothing = 0`), every direct child rides the delta, a child with no pose is skipped,
      obstruct-then-clear restores the authored pose, retraction snaps while extension springs back
      as a function of elapsed time and not of frame rate, and the refusal matrix (non-finite or
      non-positive dt, unknown entity, not an arm, no world transform, malformed component). The
      seven resolver cases and the arm's runtime-truth reseeding on load are pinned in the
      object-definition and serializer suites.
- [x] `SCENE_NODES_ROADMAP.md` `[/]` → `[x]` — done.

**Component:** `SpringArm3DComponentUVE` — `Engine/Runtime/Objects/3D` (own file pair).
**Fields to give work:** `armLength`, `margin`, `smoothing`, `collisionMask`, `enabled`;
runtime result `currentLength` (seeded to `armLength`, re-derived every step, never serialized).
**The work:** an engine-core sync that casts from the arm's origin along its own axis every fixed
step, clamps `currentLength` to the hit distance minus `margin`, applies `smoothing` as an
exponential approach, and moves the children that ride it.
**The child-resolution question, answered:** there is no socket and no "nearest camera" search -
every direct child rides the change in length, so the author parents whatever the arm carries
(camera, lamp, an entire rig) exactly where they want it, and because the shift is a delta the
authored pose comes back exactly when the way clears. A child that is a pure object with no pose of
its own is skipped. The cast ignores the arm's own entity; what else it may meet is the authored
`collisionMask`, the same layer contract every other physics object uses - which is how a
character's rig keeps the camera off the character.
**Depends on:** RaycastSystemUVE — already real. **Size: S.**

### 2. Kinematic3D — target-velocity kinematic mover — DONE

- [x] Implement — `Physics::StepKinematicBodyUVE()`
      (`Engine/Runtime/Physics/Internal/kinematic_body_uve.cpp`) does the whole move in one call:
      it refuses a body it cannot drive, eases the body's velocity toward the authored
      `targetVelocity` on a per-second curve, moves it through the world with the character
      controller's own swept kinematic move (so a wall stops it, a thin wall cannot be tunnelled
      through, and the rigid bodies it walks into are pushed with the character's push policy), and
      writes back the velocity that actually happened. `EngineCoreUVE::SyncKinematic3DObjectsUVE()`
      runs it for every ticking Kinematic3D in fixed-step order (Process `physicsPriority`, ties by hierarchy order) before `SyncCharacterControllersUVE()`, so a
      character standing on a platform is carried by the move the platform made this step.
      `active` and the object's PhysicsObject3D participation both leave the body exactly where it is
      with no velocity to hand anyone.
- [x] Tests lock it — 29 dedicated tests in `Test/Physics/kinematic_body_uve_tests.cpp`: the drive
      and its write-back, the ease curve (including frame-rate independence and the non-finite
      values it refuses), walls/floors/thin geometry (no tunnelling at 60 m/s), `active` and
      participation as separate switches, the refusal matrix, the authored component left untouched,
      the crate a platform shoves, and the rider carried the same step the platform moves.
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` — done.

**Component:** `Kinematic3DComponentUVE` — own file pair; the recipe now attaches the
PhysicsObject3D base, a collider and a kinematic rigid body.
**Fields to give work:** `targetVelocity`, `interpolation`, `active`.
**The work:** a fixed-step engine-core sync that displaces the entity kinematically by
`targetVelocity` (eased by `interpolation`) using the same kinematic move machinery the
character controller already exercises, so rigid bodies it touches respond honestly.
**Depends on:** existing physics kinematic move. **Size: S.**

### 3. SpawnPoint3D — tag-based spawn query — DONE

- [x] Implement — the query seam lives in `Engine/Runtime/Scene/{Expose/uve/scene,Internal}`:
      `Scene::QuerySpawnPointsUVE(entityManager, query)` returns a bounded, overflow-flagged list
      of every spawn point that is enabled, passes its own validator, carries a swept world
      transform, matches the query's tag (empty = any) and composes to a finite pose, in
      (index, generation) order; `Scene::ConsumeSpawnPointUVE` spends a single one-shot point, and
      `query.consumeOneShot` spends the batch it hands out. The point itself stays unticked - it
      is a query, not a system, and it never runs without being asked. Extracting it fixed a
      second copy of the participation rules: the editor's play-entry spawn had hand-rolled the
      enabled/valid/world-posed filter and the first-in-content-order pick, and now calls the
      query and keeps only its own two decisions (which entity is the player, and that a
      checkpoint is spent after the teleport succeeds rather than before).
- [x] Tests lock it — 12 dedicated cases
      (`Test/Integration/Scene/spawn_point_query_uve_tests.cpp`): find every live point in stable
      content order, tag select / empty tag = all / a tag that matches nothing, the pose is the
      object's world pose composed with the authored offset (rotation included), disabled /
      invalid / unswept / degenerate points are skipped individually rather than hiding the rest,
      one-shot consumption (batch and single-point, spent only after it is handed out, reusable
      points never spent, a spent point refuses a second consume), the bound keeps the FIRST
      points in content order and reports overflow, and the point that overflowed is not spent
      because it was never handed out. `SortsBeforeSpawnPointUVE` - the ordering rule the resolver
      and the query now share - is pinned in the object-definition suite.
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` — done.

**Component:** `SpawnPoint3DComponentUVE` — own file pair.
**Fields to give work:** `spawnTag`, `localPosition`, `localRotation`, `enabled`, `oneShot`.
**The work:** a small spawn query (system or engine-core seam): find enabled spawn points by
tag, resolve each one's world transform (own local fields composed with the entity's world
transform), hand them to the caller, and consume one-shot points so they can't spawn twice.
This is a query others call — the point itself stays unticked, by design.
**The child-resolution question, answered:** the query resolves a POSE, not an entity to write:
the object's world position/rotation composed with the authored offset (rotated, never scaled -
an offset is a distance, not a volume), and `ResolveSpawnPointPlayerLocalUVE` turns that world
pose into whatever local pose a caller's actor needs under its own parent. So a spawn point on a
moving platform, under a scaled group, or in a rotating prefab all answer correctly without the
query knowing what an actor is.
**A note on the list bound:** 32 points with `overflowed` reported, and the bound keeps the first
points in content order rather than dropping whatever the pool happened to reach last, so paging
through results is stable and repeatable. Nothing about a spawn point is per-frame state, which
is why this is a return-by-value list and not a component field.
**Depends on:** nothing new. **Size: S.**

---

## M-tier — cross-system integration

### 4. InteractionArea3D — per-frame interactable candidate tracking — DONE

- [x] Implement — the per-frame contract now lives in `Physics::SyncInteractionAreasUVE()`
      (`Engine/Runtime/Physics/{Expose/uve/physics,Internal}/interaction_area_uve.h/.cpp`), and
      `EngineCoreUVE::SyncInteractionArea3DObjectsUVE()` is the tick that calls it. Three passes:
      snapshot every interactor (character controller + valid collider + world pose) once, refresh
      every area against that snapshot, then mark the single focused area. Extracting it was the
      point: the same three passes used to live inside `engine_core_uve.cpp`, reachable only by
      standing up an EngineCoreUVE, which is exactly why the edge cases below had no tests.
- [x] Tests lock it — 16 dedicated cases (`Test/Physics/interaction_area_uve_tests.cpp`):
      every overlapping interactor tracked in content order and cleared when they leave, the
      authored `maximumCandidates` budget respected with `interactorsTruncated` reported (and
      `truncatedAreaCount`), the cap bounding the stored list but never the focus, a disabled /
      invalid / unswept area cleared in full and coming back when re-enabled, symmetric layer/mask
      acceptance in both directions, an area never listing the interactor on its own entity, only
      controllers with a usable collider ever interacting (capsule half extents are shape-aware),
      touching boundaries not counting, focus belonging to the primary interactor alone and moving
      when it moves, equal distances tying on (index, generation), no-interactor and no-area scenes,
      degenerate world rotations falling back to identity, and the scan writing only the runtime
      half of the area. The engine-core tick test still covers the same contract through a real
      `EngineCoreUVE`.
- [x] `SCENE_NODES_ROADMAP.md` `[/]` → `[x]` — done.

**Component:** `InteractionArea3DComponentUVE` — own file pair.
**Fields to give work:** `halfExtents`, `collisionLayer`, `collisionMask`, `interactionTag`,
`maximumCandidates` (the authored bound for the runtime list).
**The work:** the Hitbox3D pattern applied to interaction: a bounded, runtime-only candidate
array (size `kMaximum...` capped by the authored `maximumCandidates`) refreshed every frame
from real overlap queries against other InteractionArea3D volumes, filtered by tag and
symmetric layer/mask. Prompt/UI consumption stays gameplay-side.
**What the tests clarified, kept here because it is a contract:** an authored
`maximumCandidates` of 0 fails the component validator, so it is an invalid area (cleared), not
an area that tracks nobody - that is what `enabled = false` is for; the cap bounds only the stored
list, never focus (the primary interactor still earns the area a focus when it is the candidate
that did not fit); and `interactionTag` is carried, not filtered - tag gating belongs to the
gameplay layer that acts on the focus.
**Depends on:** AreaOverlapSystemUVE — already real. **Size: M.**

### 5. RayCast3D — honor the `exclusions` array (declared gap of a working node) — DONE

- [x] Extend the raycast query API past one ignored entity —
      `Physics::RaycastQueryUVE::excludedEntities` (a `std::span<const Scene::EntityUVE>` the caller
      owns, so a query stays a cheap value and unused slots are never read). `RaycastSystemUVE`
      checks it together with `ignoreEntity` BEFORE the layer mask: an exclusion is not a mask, so
      no layer can bring an excluded object back.
- [x] Define the save/load-stable entity reference (the real blocker — a raw `EntityUVE`
      handle is runtime-only) — `exclusions` is now `std::array<Scene::EntityUVE, 8>`: real
      references, remapped through the scene file's local-id table exactly like the visibility
      parent, the hierarchy parent and an animation target, written as `exclusionsLocalIds`. A
      reference the file does not contain is DROPPED, never renumbered into a stranger; a file
      written while the field held raw indices still loads, with those numbers ignored.
      `exclusionCount` is gone: with real references in the slots the array already says how many
      there are (a dense prefix ended by `kInvalidEntityUVE`), and the validator refuses a live
      reference behind an empty slot.
- [x] Tests lock it (excluded entity is skipped, list bound respected) — five physics cases
      (`Test/Physics/raycast_system_uve_tests.cpp`: every named collider skipped, an exclusion
      beating a matching layer mask, `ignoreEntity` + exclusions together, an empty span, an empty
      slot matching nothing), the validator/prefix cases in `Test/Objects/3D/objects_3d_uve_tests.cpp`,
      three serializer cases (the reference follows the object across a save/load, one outside the
      saved set is dropped for good, a legacy raw list is ignored), the RayCast3D metadata section
      test, and the engine-core end-to-end case that drives exclusions through a real
      `EngineCoreUVE` tick.
- [x] `SCENE_NODES_ROADMAP.md` gap note removed — done.

**Component:** `RayCast3DComponentUVE` — own file pair.
**Arrays to give work:** `exclusions[8]` (`kMaximumRayCastExclusionsUVE`).
`EngineCoreUVE::SyncRayCast3DObjectsUVE()` hands the component's live prefix to the query as
`excludedEntities`, alongside `ignoreEntity` (which stays spent on self-exclusion — a ray never
hits the collider it starts inside).
**Two contract decisions, kept here because they are the item's real answer:** the query takes a
span rather than owning a list (the caller keeps its storage; the query stays a value), and the
component stores a dense prefix of references instead of an array plus a count (one authored
value, one thing to keep in sync, and the Inspector's list control writes it in one edit).
**Depends on:** `Physics::RaycastQueryUVE` accepting multiple ignores + a persistent entity
reference scheme. **Size: M.**

### 6. Projectile3D — honor `radius` + `collisionMask` (declared gap of a working node) — DONE

- [x] Define the hit-decision contract (stop / bounce / event / all three, and who owns it) — the
      engine owns the **motion**, gameplay owns the **consequences**. The motion is an authored
      `hitPolicy` (`Stop`, the default, or `Bounce` with `restitution`/`friction`), executed by
      `Physics::StepProjectile3DUVE()`; the consequences are nobody's business but gameplay's, and
      the engine hands it the evidence: the last contact is written into the component's runtime
      hit fields, and every resolved contact is queued as a typed
      `Physics::Projectile3DHitEventUVE` (which projectile, what it hit, where, the normal, the
      speed into the surface, what the policy did, how many bounces it has taken). The engine never
      destroys the entity and never decides what a hit *means*.
- [x] Implement swept-sphere overlap vs colliders by mask — `EngineCoreUVE::SyncProjectile3DObjectsUVE()`
      now hands every live projectile to `Physics::StepProjectile3DUVE()`, which integrates
      `velocity += acceleration * dt`, sweeps the sphere of `radius` along that step with
      `ShapeCastSystemUVE::SphereCastUVE()` (world space, layer mask vs the obstacle's own layer,
      its own entity ignored), and resolves the contact through the policy. A sweep that *starts*
      overlapping reports no normal and is not a contact, so a projectile fired from inside a
      launcher volume is allowed to leave; a bounce leaves one contact-skin off the surface so the
      next sweep starts outside it; a bounce that leaves no motion at all is a stop.
- [x] Tests lock it (radius actually gates hits, mask filters layers, hit result written) — 16 cases
      in `Test/Physics/projectile_3d_step_uve_tests.cpp` (full step, integration order, stop at the
      contact with the whole result written, the radius - not a ray - deciding the hit, mask
      filtering, self-exclusion, bounce reflection, bounce friction, bounce-that-is-a-stop,
      overlap-at-step-start not being a contact, lifetime expiry, expiry + bounce in one step, every
      refusal code leaving the entity untouched, disabled, world-rotation sweep, and the contact
      normal being reflected back into the object's own axes), plus the component's validator and
      bounce math in `Test/Objects/3D/objects_3d_uve_tests.cpp`, two serializer cases (the authored
      contract round-trips while the runtime result does not, and a file from before the contract
      loads on the authored defaults), the metadata section case, and two engine-core end-to-end
      cases (stop + typed event, bounce + `stopped == false`).
- [x] `SCENE_NODES_ROADMAP.md` gap note removed — done.

**Component:** `Projectile3DComponentUVE` — own file pair.
**Fields to give work:** `radius` and `collisionMask`, plus the authored half of the hit contract
the item had to decide (`hitPolicy`, `restitution`, `friction`) and the runtime result it writes
(`hit`, `hitEntity`, `hitPosition`, `hitNormal`, `impactSpeed`, `bounceCount`).
**Two contract decisions, kept here because they are the item's real answer:** the projectile's hit
result is *sticky* where RayCast3D's is per-frame (a ray answers "what is in front of me now", a
projectile's last hit is a terminal fact gameplay reads after the fact), and the bounce
coefficients are the **projectile's own**, not blended with the surface's - this engine's colliders
default to a restitution of 0, so blending would silently delete every default bounce, while the
surface's material still travels with the contact for gameplay to react to.
**Depends on:** the hit-decision contract. **Size: M.**

### 7. Hitbox3D/Hurtbox3D — strike consequences (declared gap of working nodes) — DONE

- [x] Define the consequence contract (typed strike event first; damage numbers are gameplay) —
      the engine owns the **pairing and the edge**, gameplay owns **what being struck means**. The
      pairing itself was already real; what did not exist was any consequence of it, because a
      per-frame list is not something gameplay can act on without re-implementing the diff. So the
      pairing moved into `Physics::SyncHitboxes3DUVE()` and is diffed by
      `Physics::Hitbox3DStrikeLifecycleTrackerUVE` into exactly one typed edge per change:
      `Physics::Hitbox3DStrikeEnteredEventUVE` when a hitbox starts striking a hurtbox,
      `Physics::Hitbox3DStrikeExitedEventUVE` when it stops - whether the boxes moved apart or a
      gate closed (a disabled box, an authored layer/mask or channel change, a destroyed entity).
      Each event carries the pair, its penetration depth, the minimum-translation axis (hitbox to
      hurtbox, the direction a shove needs) and the authored damage channel. Damage numbers,
      knockback, i-frames and hit reactions stay gameplay's; the engine never destroys anything and
      never decides what a hit *means*.
- [x] Implement the consumer — `EngineCoreUVE::SyncHitbox3DObjectsUVE()` is now the seam call plus
      the tracker plus one queued event per transition (the tracker is reset at teardown beside the
      area-overlap tracker). Two decisions worth recording: identity is the (hitbox, hurtbox) pair
      **only**, so a strike that merely gets deeper is the same strike rather than a second one;
      and a truncated input report keeps the previous baseline instead of inferring exits, so an
      overflowing frame can never fabricate a hit-ended event.
- [x] Tests lock it (strike → event carries hitbox/hurtbox/depth/channel) — 18 cases in
      `Test/Physics/hitbox_strike_uve_tests.cpp` covering the report and the tracker (overlap with
      depth and axis, axis direction from both sides, self-exclusion, symmetric masks, channel
      equality, touching-is-not-overlapping, the bounded result plus its overflow flag, authored
      half-extents with world scale deliberately not applied, a rotated oriented box swept as
      itself rather than as its axis-aligned bounds, enter-once, no re-enter when the strike only
      deepens, exit-on-separation, disable-ends-strike, destroy-ends-strike, truncated input
      inventing no exits, reset, deterministic order, and the event carrying the pair by value),
      plus the combat metadata case, an engine-core end-to-end case (one Entered on the first
      overlapping tick, no second Entered while it holds, Exited on the tick a side is disabled
      with nothing moving, and a fresh Entered when it is switched back on), and the editor's
      drawer census moved 42 → 44 for the two new sections.
- [x] `SCENE_NODES_ROADMAP.md` gap note removed — done.

**Components:** `Hitbox3DComponentUVE` / `Hurtbox3DComponentUVE` — own file pairs.
**Arrays to give work:** `strikes[16]` (`kMaximumHitbox3DStrikesUVE`) + `strikeCount` +
`strikesTruncated`. The per-hitbox list is still refreshed every frame by
`EngineCoreUVE::SyncHitbox3DObjectsUVE()` (now through `Physics::SyncHitboxes3DUVE()`), and a
strike record gained `axis` beside the depth so a consequence that pushes never has to recompute
the direction from poses that have already moved on by the time it reads the record. The bounded
report (`kMaximumHitbox3DStrikeResultsUVE`) is what the tracker diffs; the per-hitbox list keeps
recording past that bound, so a truncated report is a property of the report, not of the pairing.
**Two contract decisions, kept here because they are the item's real answer:** the edge, not the
state, is the deliverable (damage-on-Entered fires once per hit instead of once per frame, which is
only true if the engine owns the baseline), and an exit is a real event when a *gate* closes, not
only when the boxes separate - the alternative is a live strike that nothing can ever end.
**Depends on:** event/consequence contract decision. **Size: M.**

### 8. LODGroup3D — camera-distance LOD switching — DONE

- [x] Define the multi-level mesh source — a per-level mesh array on the component itself:
      `lodMeshGuids[8]`, index for index with `distanceThresholds`, an invalid GUID meaning "draw the
      object's own `MeshComponentUVE` mesh at this level". The alternatives were considered and
      rejected: child mesh entities make the chain an authoring *convention* the renderer has to
      trust, and per-level slots on `MeshComponentUVE` grow a component every object carries with
      fields only LOD objects use. The published rule is one function,
      `Scene::ResolveLodGroup3DMeshGuidUVE()`, so "which mesh" cannot drift between the extraction
      walk and anything else that draws.
- [x] Implement level selection (camera distance vs `distanceThresholds`, hysteresis to stop
      level-flapping at thresholds) — `Scene::ResolveLodGroup3DLevelUVE()` now resolves with
      hysteresis: a level is entered at `threshold * (1 + hysteresis)` and left at
      `threshold * (1 - hysteresis)`, with the previous level kept in between, and culling has the
      same band at the end of the chain (an object stays culled until it is clearly back inside).
      The band defaults to 0, which takes the stateless path the renderer always used, so nothing
      changes for an object that never opts in; authoring caps it at `kMaximumLodHysteresisUVE`
      (0.5) so neighbouring bands cannot overlap. The previous answer is read from the component's
      own `currentLevel`/`culledByDistance` - the only state the rule carries - which keeps the call
      site a single statement, and the degenerate cases (disabled, empty chain, non-finite distance)
      still resolve to full detail rather than hiding geometry.
- [x] Tests lock it (levels switch at thresholds, renderer respects the active level) — 12 resolver
      cases + 2 mesh-fetch cases in `Test/Objects/3D/objects_3d_uve_tests.cpp`: each threshold picks
      its own level with inclusive boundaries, past the end clamps to the last level and culls,
      `levelCount` shortens the chain, every degenerate case draws at full detail, resolving never
      invalidates the component, the band holds a level across every crossing, each threshold
      carries its own band, a culled object stays culled inside the band and returns once,
      hysteresis 0 reproduces the stateless scan through jumps in both directions, a teleport across
      several levels lands on the level its distance is in, an out-of-range band clamps, per-level
      meshes fall back to the component's own mesh, and the validator bounds the band. The renderer
      swap is locked end to end in `Test/RHI/RenderSystems/mesh_renderer_uve_tests.cpp`: the same
      entity draws its own mesh at level 0 and its level-1 override at 40 m, and the placement cache
      misses across the change rather than reusing bounds from the wrong mesh. Two serializer cases
      in `Test/Integration/Scene/scene_serializer_uve_tests.cpp` cover the authored half round-
      tripping while the resolved answer is reseeded, and a file written before per-level meshes
      loading with no overrides and no band. One metadata case locks the declaration (both lists,
      their drawers and capacities, the ranges, and the runtime-state flags on the resolved answer).
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` — done.

**Component:** `LodGroup3DComponentUVE` — own file pair.
**Arrays to give work:** `distanceThresholds[8]` (`kMaximumLodLevelsUVE`) plus the new
`lodMeshGuids[8]`, with `levelCount` and `hysteresis` deciding what they mean; runtime result
`currentLevel` is no longer a dead copy of the default - the renderer writes it every frame and now
draws with it - and `culledByDistance` is what its name says. The Inspector authors the chain
(levels, thresholds, band) and shows the resolved level and the cull verdict during Play.
**Depends on:** the mesh swap the renderer now performs. **Size: M.**

### 9. Occluder3D — conservative-box occlusion culling — DONE

- [x] Implement — wired into the render queue's visibility build: `MeshRendererUVE` asks
      `Scene::ResolveOccluder3DFullyHiddenUVE()` against every occluder for every candidate (a plain
      OR over the frame's occluder snapshot - any strict cover hides the candidate) and counts what
      it hides in `occlusionCulledEntities`.
- [x] Tests lock it — the pure geometry has its own suite (fully hidden, edge cases fail open,
      degenerate box, never false-culls), and the render-queue integration was already locked by
      four `MeshRendererUVETest.BuildVisibilitySetUVE_*` cases: a mesh behind an occluder is culled
      in its own counter while its off-silhouette twin still draws, moving the camera re-answers
      with no state at all, a disabled occluder covers nothing, and any of several walls hides once.
      Those tests had landed in the base commit while this row still said "no test exercises the
      render-queue integration" - the row was stale, not the wiring. Verified by running the whole
      `MeshRendererUVETest` suite headlessly in this checkout (56/56 green, `uve_rhi_null`).
- [x] `SCENE_NODES_ROADMAP.md` `[/]` → `[x]` — done.

**Component:** `Occluder3DComponentUVE` — own file pair.
**Fields to give work:** `halfExtents`, `mode` (`ConservativeBox` first - sphere mode after),
`enabled`.
**The cost note, kept:** `mesh_render_eligibility_uve.h` carries a measurement of what occluder
count costs the CPU cull; it is why the count stays an authored decision (the curve flattens hard
after about 16). The payoff of the shipped wiring is the draw calls and GPU work never submitted,
which a null-RHI test run cannot measure - the counters report what it did, and a GPU timing path
is what would let anyone tune the count against the number that matters.
**Depends on:** render queue integration. **Size: M.**

### 10. VisibilityRegion3D — layer-gated visibility culling — DONE

- [x] Implement — `EngineCoreUVE::SyncVisibilityRegion3DNodesUVE()`, region extents +
      `visibilityLayers` gate membership every frame.
- [x] Tests lock it — four dedicated `EngineCoreUVETest` cases (camera in/out, layer gate,
      immediate release on leaving, disable/destroy rehoming).
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` — done, see that file's "Working today" section.

**Component:** `VisibilityRegion3DComponentUVE` — own file pair.
**Fields to give work:** `halfExtents`, `visibilityLayers`, `enabled`, `active`.
**Depends on:** render queue integration (shares the seam with Occluder3D). **Size: M.**

---

## L-tier — new pipelines (grouped by shared dependency)

### 11. Decal3D — decal-projection rendering

- [x] Implement the lifetime runtime and the projected-geometry pass (the sub-boxes below say
      exactly what landed).
- [x] Tests lock it — 8 object cases (projection, fades, expiry), a serializer re-arm case, a
      metadata case, an engine-core expiry case, 14 pass cases and one end-to-end frame case.
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` — the draw landed: `decal.glsl` projects the patches,
      evaluates the authored fades per pixel against the CPU's own world-to-unit matrix, and blends
      the material over the surface, so a decal is visible on screen and the row is `[x]`.

**Component:** `Decal3DComponentUVE` — own file pair.
**Fields to give work:** `materialAssetPath`, `size`, `projection`, `lifetime` (0 = permanent;
> 0 = expires), `enabled`, `cullMask`, the fade bands — all of them now have a consumer.
**The work:** real projected-decal rendering (project the box/`size` volume onto receiving
geometry with the material, expire by `lifetime`). A rendering-lane feature — likely lands
with/beside the renderer's own roadmap, not alone.
**Landed, in three commits:**

1. **Lifetime runtime.** `remainingLifetime` / `expired` are runtime-only fields (never saved);
   `AdvanceDecal3DLifetimeUVE` advances on simulated seconds, arms itself from the authored
   `lifetime` on the first advance, treats lifetime 0 as permanent, and reports the crossing exactly
   once; `Decal3DExpiredEventUVE` carries the entity and the material path; `SyncDecal3DObjectsUVE`
   runs it from `EngineCoreUVE::Update()` on the fixed-step clock (so a paused simulation freezes
   decals rather than aging them behind the pause); the serializer re-arms the countdown on load and
   a scene saved before the countdown existed loads permanent and alive; the inspector shows the
   countdown under a Result group, readable but never writable.
2. **Projection.** `Decal3DProjectionUVE` is the per-frame world-space volume and
   `Decal3DSampleUVE` the answer to "is this surface point painted, and how strongly" — box and
   cylinder share one unit-space footprint test, `normalFade` turns the surface facing into weight,
   the upper/lower bands fade the ends of the volume, the distance band fades it with camera
   distance, and each weight is kept separately so a caller can say which fade removed a sample.
   `Decal3DWorldToUnitUVE` / `Decal3DUnitToWorldUVE` are the round trip the pass clips in.
3. **The pass.** `DecalRendererUVE::BuildDrawListUVE` walks the decals once per frame, gates each
   through `IsDecal3DPaintingUVE`, culls by `cullMask` against both the view mask and each receiver's
   `RenderInstanceComponentUVE::renderLayers` (the first consumer that field has had), rejects what
   the frustum or the distance band removes, then clips the receiving bounds' camera-facing faces
   against the volume in unit space and emits world-space patches with the decal's unit coordinates
   per vertex, weighted at the patch centre and sorted back to front. `AppendVertexStreamUVE`
   publishes them in the canonical `MeshVertexUVE` layout an asset-backed mesh already uses, and the
   frame reports draws, patches and triangles through the renderer diagnostics and the F1 panel.

4. **The draw.** `DecalDrawPlanUVE` (built by `BuildDecalDrawPlanUVE`) turns the pass's draw list
   into GPU work: per decal, the volume's world-to-unit matrix (the inverse of
   `ComposeTrsUVE(position, rotation, halfExtents)` - the same transform `Decal3DWorldToUnitUVE()`
   applies, so the CPU's clipping space and the shader's coordinate space are one space), the
   material's albedo texture - sampled with the patch's unit coordinates, which is the [0, 1] range
   a texture is sampled with and what makes a decal the shape the artist drew rather than the
   rectangle it was clipped to - its albedo colour tinted by `modulate`, its emissive scaled by
   `emissionEnergy`, `albedoMix` as the blend's alpha scale, and the authored fades. `decal.glsl` is
   the program: world-space
   patches through `uViewProjection`, depth-tested but not depth-written, blended `SourceAlphaOver`,
   the texture resolved by the same GUID-to-handle path the mesh materials use, with the same white
   fallback that leaves a flat-coloured decal intact. The fades are re-evaluated per pixel (a patch is
   one receiving face,
   and a wall's face spans the whole volume - a weight flat across it would end in the hard
   rectangle the authored fade exists to avoid). Each decal is drawn from its own vertex/index
   buffers, created on the first frame it paints and released once the plan stops naming it, because
   this engine's `DrawIndexedUVE` has no first-index offset - the same reason a skinned mesh owns
   its vertex buffer. The plan is bounded (command and vertex caps) and reports what it refused, and
   the F1 panel now shows the recorded draw calls beside the extracted draws.

**Depends on:** renderer decal pass. **Size: L.**

### 12. ReflectionProbe3D — probe capture scheduling (done) + sampling (still open)

- [x] Implement capture scheduling — `EngineCoreUVE::SyncReflectionProbe3DNodesUVE()`:
      `updateMode` (`Once`/`Always`/on-demand), `cameraInfluenceWeight`, capture budget with
      starvation aging.
- [x] Tests lock the scheduling half — five dedicated `EngineCoreUVETest` cases.
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` — done for the sync half.
- [ ] Implement the renderer-side sampling half — still `[~]`: capture the probe's volume
      into a reflection texture and feed ambient/reflection sampling.

**Component:** `ReflectionProbe3DComponentUVE` — own file pair.
**Remaining fields:** `size`, `visibilityLayers` are scheduled but not yet consumed by any
renderer capture.
**The work:** capture the probe's volume into a reflection texture and feed ambient/reflection
sampling. Also a rendering-lane feature.
**Depends on:** renderer cubemap capture. **Size: L.**

### 13. NavigationRegion3D + NavigationAgent3D — navmesh, pathfinding, steering (paired)

- [ ] Navigation subsystem: navmesh representation + baking from region bounds
- [ ] Pathfinding: region → agent path requests honoring `navigationLayers`
- [ ] Agent steering: `desiredVelocity`/`nextPathPosition`/`pathStatus`/`targetReached`
      refreshed on `pathUpdateInterval`, `pathChanged` on reroute
- [ ] Tests lock each layer
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` (both entries)

**Components:** `NavigationRegion3DComponentUVE` (fields: `boundsHalfExtents`,
`navigationMeshAssetPath`, `navigationLayers`, `enabled`, `rebuildRequested`) and
`NavigationAgent3DComponentUVE` (fields: `targetPosition`, `nextPathPosition`,
`desiredVelocity`, `radius`, `height`, `maxSpeed`, `pathUpdateInterval`, `navigationLayers`,
`pathStatus`, `avoidanceEnabled`, `enabled`, `pathChanged`, `targetReached`) — both own file
pairs. Nothing between them runs: there is no navmesh, no pathfinder, no steering.
**Depends on:** a whole new Navigation subsystem (AI nodes in `SCENE_NODES_ROADMAP.md` wait on
this too). **Size: L.**

### 14. Skeleton3D + BoneAttachment3D + AnimationSequencer + AnimationGraph — the animation pipeline (paired)

- [ ] Clip sampling: decode `AnimationClipAssetUVE` tracks into bone-local pose over time
- [ ] Skeleton pose: evaluate `bones` hierarchy into per-bone world transforms
- [ ] Skinning: renderer consumes the posed skeleton for mesh deformation
- [ ] AnimationSequencer: `clipAssetPath`/`playbackSpeed`/`looping`/`playOnAwake` drive the
      sampler (data lives in the shared `AnimationPlayerComponentUVE`; the Objects/3D file holds
      its `*ObjectDefinitionUVE` recipe)
- [ ] BoneAttachment3D: `boneIndex`/`boneName` resolve against a posed skeleton and the
      entity follows the bone transform
- [ ] AnimationGraph: becomes editor-creatable once the pipeline exists
      (`libraryCreatable = false` today, honestly)
- [ ] Tests lock each layer
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` (all four entries)

**Components:** `Skeleton3DComponentUVE` (fields: `skeletonAssetPath`,
**`bones` vector** — the authored bone hierarchy array — and `enabled`),
`BoneAttachment3DComponentUVE` (`skeletonLocalId`, `boneIndex`, `boneName`, authored
local TRS, `enabled`), `AnimationPlayerComponentUVE` (shared component), and
`AnimationTreeUVE` (Core/Animation module).
**Depends on:** the missing skinning/clip-sampling pipeline — `ROADMAP.md`'s Animation
section owns that gap. **Size: L** (largest item here).

### 15. LevelStreamer3D + WorldPartition3D — streaming + cell partitioning (paired) — DONE

- [x] External-scene lifecycle: load/unload a saved scene file at runtime —
      `SyncLevelStreamer3DNodesUVE()` tracks loaded roots and load-failure latches per
      streamer entity, cleaned up against `IsAliveUVE()`, never blind destruction.
- [x] LevelStreamer3D: `loadDistance`/`unloadDistance` vs the streaming origin drive
      `loaded` (hysteresis between the two distances, `loadRequested` for manual control).
- [x] WorldPartition3D: `cellSize` + **`cellCounts[3]`** define the grid;
      `maximumLoadedCells` bounds the working set, `loadedCellCount` reports it.
- [x] Tests lock each layer — five dedicated tests for LevelStreamer3D, five for
      WorldPartition3D (see the entries in `SCENE_NODES_ROADMAP.md`'s "Working today" section
      for the exact case names).
- [x] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` (both entries) — done.

**Components:** `LevelStreamer3DComponentUVE` (fields: `levelPath`, `loadDistance`,
`unloadDistance`, `enabled`, `loaded`, `loadRequested`) and
`WorldPartition3DComponentUVE` (fields: `cellSize`, `cellCounts[3]` array,
`maximumLoadedCells`, `loadedCellCount`, `enabled`) — both own file pairs.
**Depends on:** runtime external-scene load/unload (serializer can save/load; the runtime
lifecycle is the missing part). **Size: L.**

---

## No work, by design

### Marker3D — position/orientation hint

**Component:** `Marker3DComponentUVE` — own file pair. Its fields are consumed the moment
anything reads the entity's transform; it is authoring data for tools/scripts by design and
correctly stays unticked. Listed here so the audit trail shows it was considered, not missed.

---

## Done (moved here when the last box ticks)

### Decal3D — the projected patches are painted: `decal.glsl` evaluates the authored fades per pixel against the CPU's own world-to-unit matrix, each decal draws from its own buffers, and the plan's caps/refusals are reported — done in the change that gave the patch stream a program to bind (8 plan cases, 1 end-to-end frame case that asserts the recorded draw, 2 F1 rows with their own case)
### LODGroup3D — the resolved level draws its own mesh: per-level `lodMeshGuids` with the object's own mesh as the fallback, a hysteresis band on every threshold and on the cull point, locked by 12 resolver + 2 mesh-fetch + 1 renderer-swap + 2 serializer + 1 metadata case — done in the change that gave `distanceThresholds` something to draw (drawer census 44 → 45)

### SpringArm3D — the camera boom, tested against the world it casts into — done in 0992c86
### Kinematic3D — target-velocity kinematic mover over the swept kinematic move — done in 7758f3e
### InteractionArea3D — the interaction scan extracted into `Physics::SyncInteractionAreasUVE()`, locked by 16 dedicated tests — done in the change that moved it out of the engine-core tick
### Occluder3D — render-queue occlusion culling, locked by four `MeshRendererUVETest` integration cases (stale roadmap row; verified green here) — done in the change that corrected the row
### VisibilityRegion3D — layer-gated visibility culling, four dedicated EngineCoreUVE cases — done in the change that added `SyncVisibilityRegion3DNodesUVE()` (stale row; retired here)
### LevelStreamer3D + WorldPartition3D — streaming and the cell grid, five dedicated tests each — done in the change that added their syncs (stale rows; retired here)
### Hitbox3D/Hurtbox3D — the strike consequence contract: one typed edge event per strike start/end, diffed from the per-frame pairing — done in the change that gave `strikes` something to mean (18 dedicated strike/tracker cases, 1 engine-core edge case, combat metadata + drawer sections)
### Projectile3D — swept-sphere hit resolution with an authored stop/bounce motion policy and a typed hit event — done in the change that gave `radius` and `collisionMask` something to do (16 dedicated step cases, 2 engine-core end-to-end cases)
### RayCast3D — `exclusions` honored: multi-entity exclusions with save/load-stable references, locked by 5 physics + 3 serializer + 2 metadata/validator + 1 engine-core test — done in the change that gave the array real references
### SpawnPoint3D — the spawn query seam (`Scene::QuerySpawnPointsUVE`, `Scene::ConsumeSpawnPointUVE`) — done in the change that rewired the editor's play-entry spawn onto it (12 dedicated tests in `Test/Integration/Scene/spawn_point_query_uve_tests.cpp`)
