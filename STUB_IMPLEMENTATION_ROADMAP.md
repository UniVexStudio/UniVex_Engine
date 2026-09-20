# Stub Implementation Roadmap

A working tracker for every scene node that already owns its `.h` + `.cpp` pair but whose
**component fields and arrays still receive no real work at runtime** — plus the declared
honest gaps of nodes that are otherwise real. This is the drill-down companion to
`SCENE_NODES_ROADMAP.md`: that file keeps the high-level `[x]`/`[~]`/`[ ]` status per node;
THIS file is the per-item implementation checklist we tick off as each one gets built.

Scope: the 18 authored-data-only 3D stubs + the 4 declared gaps of working nodes (2D/UI/AI
nodes have no files yet, so they have nothing to track here — they stay in
`SCENE_NODES_ROADMAP.md`'s missing-entirely sections until they exist).

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

| # | Node | Component | Arrays to give work | The work | Depends on | Size |
|---|------|-----------|---------------------|----------|------------|------|
| 1 | SpringArm3D | `SpringArm3DNodeComponentUVE` | — | camera-boom raycast clamp | RaycastSystemUVE (exists) | S |
| 2 | AnimatableBody3D | `AnimatableBody3DNodeComponentUVE` | — | target-velocity kinematic mover | physics kinematic move (exists) | S |
| 3 | SpawnPoint3D | `SpawnPoint3DNodeComponentUVE` | — | tag-based spawn query + one-shot | — | S |
| 4 | InteractionArea3D | `InteractionArea3DNodeComponentUVE` | candidate list (new, bounded by `maximumCandidates`) | per-frame interactable candidate tracking | AreaOverlapSystemUVE (exists) | M |
| 5 | RayCast3D gap | `RayCast3DNodeComponentUVE` | `exclusions[8]` + `exclusionCount` | multi-entity exclusion queries | query API + stable entity refs | M |
| 6 | Projectile3D gap | `Projectile3DNodeComponentUVE` | — (`radius`, `collisionMask` scalars) | swept-sphere hit resolution | hit-decision contract | M |
| 7 | Hitbox3D/Hurtbox3D gap | `Hitbox3DNodeComponentUVE` / `Hurtbox3DNodeComponentUVE` | `strikes[16]` + `strikeCount` | strike consequences (events first) | gameplay/event contract | M |
| 8 | LODGroup3D | `LodGroup3DNodeComponentUVE` | `distanceThresholds[8]` | camera-distance LOD switching | multi-level mesh source | M |
| 9 | Occluder3D | `Occluder3DNodeComponentUVE` | — | conservative-box occlusion culling | render queue integration | M |
| 10 | VisibilityRegion3D | `VisibilityRegion3DNodeComponentUVE` | — | layer-gated visibility culling | render queue integration | M |
| 11 | Decal3D | `Decal3DNodeComponentUVE` | — | decal-projection rendering | renderer (big) | L |
| 12 | ReflectionProbe3D | `ReflectionProbe3DNodeComponentUVE` | — | probe capture + sampling | renderer (big) | L |
| 13 | NavigationRegion3D + NavigationAgent3D | `NavigationRegion3DNodeComponentUVE` / `NavigationAgent3DNodeComponentUVE` | — | navmesh bake + pathfind + steer | new Navigation subsystem | L |
| 14 | Skeleton3D + BoneAttachment3D + AnimationPlayer + AnimationTree | `Skeleton3DNodeComponentUVE`, `BoneAttachment3DNodeComponentUVE`, `AnimationPlayerComponentUVE`, `AnimationTreeUVE` | `bones` vector | clip sampling → bone pose → skinning | new Animation pipeline | L |
| 15 | LevelStreamer3D + WorldPartition3D | `LevelStreamer3DNodeComponentUVE` / `WorldPartition3DNodeComponentUVE` | `cellCounts[3]` | streaming + cell grid load/unload | external-scene lifecycle | L |
| — | Marker3D | `Marker3DNodeComponentUVE` | — | **none, by design** — read by tools/scripts, never ticked | — | — |

---

## S-tier — one engine-core sync each (start here)

### 1. SpringArm3D — camera-boom raycast clamp

- [ ] Implement
- [ ] Tests lock it (arm shortens behind geometry, margin honored, smoothing converges,
      mask filters, disabled = full length)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `SpringArm3DNodeComponentUVE` — `Engine/Runtime/Nodes/3D` (own file pair).
**Fields to give work:** `armLength`, `margin`, `smoothing`, `collisionMask`, `enabled`;
runtime result `currentLength` (today a dead copy of the default).
**The work:** an engine-core sync (RayCast3D precedent) that raycasts from the arm's origin
along its axis every frame, clamps `currentLength` to the hit distance minus `margin`, applies
`smoothing` as an exponential approach, and positions the attached child (the camera) at the
clamped distance. Needs a child-resolution rule (nearest Camera3D child, or explicit socket).
**Depends on:** RaycastSystemUVE — already real. **Size: S.**

### 2. AnimatableBody3D — target-velocity kinematic mover

- [ ] Implement
- [ ] Tests lock it (moves at targetVelocity, interpolation eases, inactive = stays put,
      pushes bodies per the kinematic contract)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `AnimatableBody3DNodeComponentUVE` — own file pair; recipe attaches a collider +
kinematic rigid body.
**Fields to give work:** `targetVelocity`, `interpolation`, `active`.
**The work:** a fixed-step engine-core sync that displaces the entity kinematically by
`targetVelocity` (eased by `interpolation`) using the same kinematic move machinery the
character controller already exercises, so rigid bodies it touches respond honestly.
**Depends on:** existing physics kinematic move. **Size: S.**

### 3. SpawnPoint3D — tag-based spawn query

- [ ] Implement
- [ ] Tests lock it (find by tag, returns authored transform, one-shot consumption,
      disabled points are skipped)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `SpawnPoint3DNodeComponentUVE` — own file pair.
**Fields to give work:** `spawnTag`, `localPosition`, `localRotation`, `enabled`, `oneShot`.
**The work:** a small spawn query (system or engine-core seam): find enabled spawn points by
tag, resolve each one's world transform (own local fields composed with the entity's world
transform), hand them to the caller, and consume one-shot points so they can't spawn twice.
This is a query others call — the point itself stays unticked, by design.
**Depends on:** nothing new. **Size: S.**

---

## M-tier — cross-system integration

### 4. InteractionArea3D — per-frame interactable candidate tracking

- [ ] Implement
- [ ] Tests lock it (candidates refresh, tag/layer/mask gates, bound respected + overflow
      flagged, disabled clears stale candidates)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `InteractionArea3DNodeComponentUVE` — own file pair.
**Fields to give work:** `halfExtents`, `collisionLayer`, `collisionMask`, `interactionTag`,
`maximumCandidates` (the authored bound for the runtime list).
**The work:** the Hitbox3D pattern applied to interaction: a bounded, runtime-only candidate
array (size `kMaximum...` capped by the authored `maximumCandidates`) refreshed every frame
from real overlap queries against other InteractionArea3D volumes, filtered by tag and
symmetric layer/mask. Prompt/UI consumption stays gameplay-side.
**Depends on:** AreaOverlapSystemUVE — already real. **Size: M.**

### 5. RayCast3D — honor the `exclusions` array (declared gap of a working node)

- [ ] Extend the raycast query API past one ignored entity
- [ ] Define the save/load-stable entity reference (the real blocker — a raw `EntityUVE`
      handle is runtime-only)
- [ ] Tests lock it (excluded entity is skipped, list bound respected)
- [ ] `SCENE_NODES_ROADMAP.md` gap note removed

**Component:** `RayCast3DNodeComponentUVE` — own file pair.
**Arrays to give work:** `exclusions[8]` (`kMaximumRayCastExclusionsUVE`) + `exclusionCount`.
Today `SyncRayCast3DNodesUVE()` spends the query API's single ignore-slot on self-exclusion
and silently ignores the rest.
**Depends on:** `Physics::RaycastQueryUVE` accepting multiple ignores + a persistent node
reference scheme. **Size: M.**

### 6. Projectile3D — honor `radius` + `collisionMask` (declared gap of a working node)

- [ ] Define the hit-decision contract (stop / bounce / event / all three, and who owns it)
- [ ] Implement swept-sphere overlap vs colliders by mask
- [ ] Tests lock it (radius actually gates hits, mask filters layers, hit result written)
- [ ] `SCENE_NODES_ROADMAP.md` gap note removed

**Component:** `Projectile3DNodeComponentUVE` — own file pair.
**Fields to give work:** `radius`, `collisionMask`. The node already integrates velocity and
expires, but a projectile today flies through everything.
**The work:** a real hit test (sphere sweep of `radius` against colliders accepted by
`collisionMask`) writing a hit result (entity/point/normal) into runtime-only fields, plus the
gameplay decision of what a hit does — the honest part this engine must decide, not fake.
**Depends on:** the hit-decision contract. **Size: M.**

### 7. Hitbox3D/Hurtbox3D — strike consequences (declared gap of working nodes)

- [ ] Define the consequence contract (typed strike event first; damage numbers are gameplay)
- [ ] Implement the consumer
- [ ] Tests lock it (strike → event carries hitbox/hurtbox/depth/channel)
- [ ] `SCENE_NODES_ROADMAP.md` gap note removed

**Components:** `Hitbox3DNodeComponentUVE` / `Hurtbox3DNodeComponentUVE` — own file pairs.
**Arrays to give work:** `strikes[16]` (`kMaximumHitbox3DStrikesUVE`) + `strikeCount` +
`strikesTruncated`. The strike list is written every frame by
`EngineCoreUVE::SyncHitbox3DNodesUVE()` — nothing reads it yet.
**Depends on:** event/consequence contract decision. **Size: M.**

### 8. LODGroup3D — camera-distance LOD switching

- [ ] Define the multi-level mesh source (LOD slots on the mesh component, or child-mesh
      convention — must be decided, not assumed)
- [ ] Implement level selection (camera distance vs `distanceThresholds`, hysteresis to stop
      level-flapping at thresholds)
- [ ] Tests lock it (levels switch at thresholds, renderer respects the active level)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `LodGroup3DNodeComponentUVE` — own file pair.
**Arrays to give work:** `distanceThresholds[8]` (`kMaximumLodLevelsUVE`) + `levelCount`;
runtime result `currentLevel` (today a dead copy of the default).
**Depends on:** multi-level mesh source + renderer cooperation. **Size: M.**

### 9. Occluder3D — conservative-box occlusion culling

- [ ] Implement (occluder boxes vs camera frustum → cull fully-hidden renderables)
- [ ] Tests lock it (occluded mesh culled, edge cases fail open, never false-culls)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `Occluder3DNodeComponentUVE` — own file pair.
**Fields to give work:** `halfExtents`, `mode` (`ConservativeBox` first — sphere mode after),
`enabled`.
**Depends on:** render queue integration. **Size: M.**

### 10. VisibilityRegion3D — layer-gated visibility culling

- [ ] Implement (region extents + `visibilityLayers` gate what renders inside it)
- [ ] Tests lock it (layers gate, `active` toggles, no region = current behavior)
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `VisibilityRegion3DNodeComponentUVE` — own file pair.
**Fields to give work:** `halfExtents`, `visibilityLayers`, `enabled`, `active`.
**Depends on:** render queue integration (shares the seam with Occluder3D). **Size: M.**

---

## L-tier — new pipelines (grouped by shared dependency)

### 11. Decal3D — decal-projection rendering

- [ ] Implement
- [ ] Tests lock it
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `Decal3DNodeComponentUVE` — own file pair.
**Fields to give work:** `materialAssetPath`, `size`, `projection` (`Box` first),
`lifetime` (0 = permanent; > 0 = expires), `enabled`.
**The work:** real projected-decal rendering (project the box/`size` volume onto receiving
geometry with the material, expire by `lifetime`). A rendering-lane feature — likely lands
with/beside the renderer's own roadmap, not alone.
**Depends on:** renderer decal pass. **Size: L.**

### 12. ReflectionProbe3D — probe capture + sampling

- [ ] Implement
- [ ] Tests lock it
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]`

**Component:** `ReflectionProbe3DNodeComponentUVE` — own file pair.
**Fields to give work:** `size`, `visibilityLayers`, `updateMode` (`Once` first —
`Always`/`Request` after), `updateRequested`, `enabled`.
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

**Components:** `NavigationRegion3DNodeComponentUVE` (fields: `boundsHalfExtents`,
`navigationMeshAssetPath`, `navigationLayers`, `enabled`, `rebuildRequested`) and
`NavigationAgent3DNodeComponentUVE` (fields: `targetPosition`, `nextPathPosition`,
`desiredVelocity`, `radius`, `height`, `maxSpeed`, `pathUpdateInterval`, `navigationLayers`,
`pathStatus`, `avoidanceEnabled`, `enabled`, `pathChanged`, `targetReached`) — both own file
pairs. Nothing between them runs: there is no navmesh, no pathfinder, no steering.
**Depends on:** a whole new Navigation subsystem (AI nodes in `SCENE_NODES_ROADMAP.md` wait on
this too). **Size: L.**

### 14. Skeleton3D + BoneAttachment3D + AnimationPlayer + AnimationTree — the animation pipeline (paired)

- [ ] Clip sampling: decode `AnimationClipAssetUVE` tracks into bone-local pose over time
- [ ] Skeleton pose: evaluate `bones` hierarchy into per-bone world transforms
- [ ] Skinning: renderer consumes the posed skeleton for mesh deformation
- [ ] AnimationPlayer: `clipAssetPath`/`playbackSpeed`/`looping`/`playOnAwake` drive the
      sampler (data lives in the shared `AnimationPlayerComponentUVE`; the Nodes/3D file holds
      its NodeDefinition recipe)
- [ ] BoneAttachment3D: `boneIndex`/`boneName` resolve against a posed skeleton and the
      entity follows the bone transform
- [ ] AnimationTree: becomes editor-creatable once the pipeline exists
      (`libraryCreatable = false` today, honestly)
- [ ] Tests lock each layer
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` (all four entries)

**Components:** `Skeleton3DNodeComponentUVE` (fields: `skeletonAssetPath`,
**`bones` vector** — the authored bone hierarchy array — and `enabled`),
`BoneAttachment3DNodeComponentUVE` (`skeletonLocalId`, `boneIndex`, `boneName`, authored
local TRS, `enabled`), `AnimationPlayerComponentUVE` (shared component), and
`AnimationTreeUVE` (Core/Animation module).
**Depends on:** the missing skinning/clip-sampling pipeline — `ROADMAP.md`'s Animation
section owns that gap. **Size: L** (largest item here).

### 15. LevelStreamer3D + WorldPartition3D — streaming + cell partitioning (paired)

- [ ] External-scene lifecycle: load/unload a saved scene file at runtime
- [ ] LevelStreamer3D: `loadDistance`/`unloadDistance` vs the streaming origin drive
      `loaded` (hysteresis between the two distances, `loadRequested` for manual control)
- [ ] WorldPartition3D: `cellSize` + **`cellCounts[3]`** define the grid;
      `maximumLoadedCells` bounds the working set, `loadedCellCount` reports it
- [ ] Tests lock each layer
- [ ] `SCENE_NODES_ROADMAP.md` `[~]` → `[x]` (both entries)

**Components:** `LevelStreamer3DNodeComponentUVE` (fields: `levelPath`, `loadDistance`,
`unloadDistance`, `enabled`, `loaded`, `loadRequested`) and
`WorldPartition3DNodeComponentUVE` (fields: `cellSize`, `cellCounts[3]` array,
`maximumLoadedCells`, `loadedCellCount`, `enabled`) — both own file pairs.
**Depends on:** runtime external-scene load/unload (serializer can save/load; the runtime
lifecycle is the missing part). **Size: L.**

---

## No work, by design

### Marker3D — position/orientation hint

**Component:** `Marker3DNodeComponentUVE` — own file pair. Its fields are consumed the moment
anything reads the entity's transform; it is authoring data for tools/scripts by design and
correctly stays unticked. Listed here so the audit trail shows it was considered, not missed.

---

## Done (moved here when the last box ticks)

<!-- Move completed items here with their commit hash, e.g.:
### Hitbox3D/Hurtbox3D — real per-frame strike pairing — done in b72a062/5d07fd7
-->
