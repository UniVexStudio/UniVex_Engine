# Scene Objects Roadmap

A detailed, node-by-node checklist of every placeable scene node / UI element / AI element this
engine should eventually offer, grouped by domain (3D, 2D, CanvasLayer/UI, AI). This is a companion
to the top-level `ROADMAP.md` — that file tracks whole engine systems; this file tracks individual
node types specifically, since "the object exists in the Add-Object list" and "the object actually does
something at runtime" are two different, easily-confused claims.

No third-party engine or product name appears anywhere in this document — node names below are
generic, descriptive names for well-understood game-engine concepts, not references to a specific
product.

## How to read this file

Four levels, not three — a system existing is not the same claim as a system being verified
correct, and collapsing those two into one checkmark is exactly how a stale `[x]` happens:

- `[x]` — **verified.** A real system ticks/uses it every frame AND that behavior is locked by
  dedicated tests covering more than one case (not just construction/serialization round-trips).
  Attaching it to an entity has a real, checked effect.
- `[/]` — **wired, not fully verified.** A real system ticks/uses it every frame — confirmed by
  reading the actual sync function, not assumed — but either no dedicated behavior test exists
  yet, or coverage is thin (one test, or only the pure-logic helper is tested and not its
  per-frame integration). Treat this as "probably works, hasn't earned the checkmark yet."
- `[~]` — **authored data only.** The node exists as authored data (fields + validation) but
  nothing reads or writes it at runtime. It can be added in the editor and will save/load
  correctly, but it does nothing.
- `[ ]` — **not started.** The node does not exist at all yet, in any form.

For every `[~]`/`[/]` entry (and the declared gaps of `[x]` entries), `STUB_IMPLEMENTATION_ROADMAP.md`
is the drill-down tracker: the exact component fields/arrays awaiting work, the system each one
needs, dependencies, and the per-item checklist ticked as implementation lands. This file keeps
the high-level status; that file holds the working plan.

Update this file in the same change that adds, fixes, or wires up any node. A stale checklist is
worse than no checklist.

---

## 3D Objects

### Working today

- [x] SceneRoot — the document's single structural root (Godot-style one-root scene): created
  automatically with every new document, every loaded legacy multi-root file is auto-migrated
  under it on load, all new nodes join the hierarchy under the current selection (or the root
  when nothing is selected), and it can never be deleted, re-parented, or duplicated. Structural
  only by design — name + identity transform; scene-wide settings get their own authored homes
  when the systems that consume them exist, not before.
- [x] Empty — plain transform-only node, the base of every scene hierarchy.
- [x] Camera3D — real camera, drives view/projection for rendering.
- [x] MeshInstance3D — real mesh + material rendering through the lit shader pipeline.
- [x] BoxMesh3D / SphereMesh3D / PlaneMesh3D — primitive mesh shapes, real rendering.
- [x] Light3D (Directional / Point / Spot) — real, shades meshes.
- [x] Collider3D — real collision shape, used by the physics/collision systems.
- [x] Static3D — real, non-moving collidable body.
- [x] Rigid3D — real, physics-simulated body (gravity, collision response).
- [x] Character3D — real kinematic character controller (move/jump/ground state).
- [x] SpringArm3D — real camera-boom behaviour (`EngineCoreUVE::SyncSpringArm3DObjectsUVE()` →
  `Physics::StepSpringArm3DUVE()`): a ray per arm per fixed step along the arm's own local +Z, the
  target length under the authored `margin`, and a motion law where retraction snaps (a camera never
  clips through a wall for one smooth frame's sake) while extension springs back at `smoothing`/s
  and `smoothing = 0` reproduces Godot's snap-both-ways. Every direct child rides the change in
  length, so obstruct-then-clear restores the authored pose exactly; the arm is switched off by
  handing its length back rather than freezing in a wall. Locked by 17 dedicated step tests
  (`Test/Physics/spring_arm_uve_tests.cpp`) plus the seven resolver cases and the runtime-truth
  round-trip rule.
- [x] Kinematic3D — real kinematic body driver (`EngineCoreUVE::SyncKinematic3DObjectsUVE()`, run
  before the character step): every Kinematic3D moves by its authored `targetVelocity`, eased by
  `interpolation` on a per-second curve, through the same swept kinematic move the character
  controller uses — a wall stops it, a thin wall cannot be tunnelled through, and the rigid bodies
  it walks into are pushed with the character's own push policy. The velocity it actually achieved
  is written back to its body, which is what a rider standing on it reads; `active` and the
  object's PhysicsObject3D participation leave it exactly where it is, with no velocity to hand
  anyone. Locked by 29 dedicated tests (`Test/Physics/kinematic_body_uve_tests.cpp`).
- [x] AudioSource3D — real, plays positional audio.
- [x] ParticleEmitter3D — real, ticked particle simulation.
- [x] Area3D — real overlap-detection trigger volume.
- [x] WorldEnvironment3D — real, feeds ambient color/energy into the renderer.
- [x] Script — real, ticked by the script VM/runtime, with real input/collision bindings.
- [x] RayCast3D — real per-frame raycast against the actual RaycastSystemUVE (`direction` is
  local-space, rotated by the entity's world rotation), correctly excludes its own entity, and
  writes `hit`/`hitPosition`/`hitNormal`/`hitEntity` back every tick. Every authored field is
  honored: `exclusions` names other objects the ray refuses to hit, held as real entity
  references - remapped by the scene file the way a visibility parent is, so they still point at
  the same object after a save/load, and dropped rather than renumbered when the object is not in
  the file - and handed to the query every tick, where they are checked before the layer mask, so
  no layer can bring an excluded object back.
- [x] Projectile3D — real per-fixed-step kinematic integration *and* real hit resolution. The
  engine sweeps the authored `radius` along each step's motion against the layers `collisionMask`
  accepts (never the projectile's own entity) and resolves the contact through an authored
  `hitPolicy`: Stop halts it at the contact and clears `active`, Bounce reflects it through
  `restitution`/`friction`. Every authored field is honored, the last contact lands in the
  component's runtime hit fields (`hitEntity`/`hitPosition`/`hitNormal`/`impactSpeed`/`bounceCount`),
  `remainingLifetime` still counts down to clear `active`, and every resolved contact is queued as
  a typed `Physics::Projectile3DHitEventUVE` - damage, effects and despawning stay with gameplay,
  which is what the event is for. An overlap the step begins inside is not a contact: a projectile
  fired from inside a launcher's own volume is allowed to leave.
- [x] LevelStreamer3D — real per-frame system (`EngineCoreUVE::SyncLevelStreamer3DNodesUVE()`):
  `loadDistance`/`unloadDistance` against the nearest viewer drive `loaded` through a hysteresis
  band, `loadRequested` forces a manual load, a failed load latches closed and never retries in
  the session, and per-tick load requests are budget-capped with overflow carried into the next
  tick. Locked by five dedicated `EngineCoreUVETest` cases (hysteresis, character-as-viewer,
  disable-pulls-content, failed-load latch, budget carryover).
- [x] WorldPartition3D — real per-frame system (`EngineCoreUVE::SyncWorldPartition3DNodesUVE()`):
  `cellSize` + `cellCounts` define the grid, meshes are assigned cell membership by nearest-viewer
  distance, `maximumLoadedCells` bounds the working set (nearest cell wins on contention), nested
  partitions let the inner one own its subtree, and disabling releases every member. Locked by
  five dedicated tests (membership scope, budget/contention, nesting, disable-releases,
  subtree-growth tracking).
- [x] VisibilityRegion3D — real per-frame system (`EngineCoreUVE::SyncVisibilityRegion3DNodesUVE()`):
  a mesh joins a region's visibility set only while a viewer is inside the region's extents AND
  the mesh passes the region's `visibilityLayers` gate; leaving releases the verdict on the very
  next tick, and disabling or destroying the region rehomes its members. Locked by four dedicated
  tests (camera in/out, layer gate, immediate release, disable/destroy rehoming).
- [x] ReflectionProbe3D — real per-frame system (`EngineCoreUVE::SyncReflectionProbe3DNodesUVE()`):
  `updateMode` (`Once` captures on first tick then goes silent forever; `Always` recaptures only
  while a camera is inside its influence volume; on-demand services `updateRequested` then clears
  the latch), `cameraInfluenceWeight` tracks camera position exactly, and the per-tick capture
  budget ages out starvation instead of always favoring the nearest probe. Locked by five
  dedicated tests. The renderer-side sampling half of this feature (feeding the captured cubemap
  into ambient/reflection shading) is a separate, real gap — see `ROADMAP.md`.

- [x] InteractionArea3D — the whole per-frame contract now lives in one callable seam,
  `Physics::SyncInteractionAreasUVE()`, which the tick calls: interactor snapshot (character
  controllers with a valid collider and a world pose), per-area refresh behind every fail-closed
  gate, symmetric layer/mask acceptance, the exact oriented-box overlap (touching boundaries do not
  count), a bounded candidate list that reports its own overflow, and exactly one focused area -
  nearest to the PRIMARY interactor, ties by (index, generation). Locked by 16 dedicated tests
  (`Test/Physics/interaction_area_uve_tests.cpp`) plus the engine-core tick test, and by the
  contract an authored `maximumCandidates` of 0 is refused as an invalid component rather than read
  as a zero budget.
- [x] Occluder3D — wired into the render queue's visibility build (`MeshRendererUVE` asks
  `ResolveOccluder3DFullyHiddenUVE()` against every occluder for every candidate and counts what it
  hides in `occlusionCulledEntities`). The pure geometry is covered by its own suite (13+ cases:
  fully hidden, edge cases fail open, degenerate box, never false-culls) and the render-queue
  integration by four `MeshRendererUVETest.BuildVisibilitySetUVE_*` cases - hidden-behind-a-wall,
  camera-move-re-answers (no state), disabled occluder covers nothing, and any-of-several-walls
  hides once. All 56 tests in that suite were run headlessly against the null RHI for this entry.

### Authored data only, not yet wired to a system

- [~] NavigationRegion3D — bounds + navmesh path fields exist, no navmesh baking/pathfinding system exists yet.
- [~] NavigationAgent3D — target/path fields exist, no pathfinding/steering system exists yet.
- [~] Skeleton3D — bone hierarchy data exists, no skinning/animation system reads it.
- [~] BoneAttachment3D — attach-to-bone fields exist, nothing resolves/follows a bone transform.
- [~] Marker3D — a plain position/orientation hint, has no behavior by design (this one may never need a "system" — it's meant to be read by other tools/scripts, not ticked itself).
- [x] Hitbox3D — real per-frame strike detection: `EngineCoreUVE::SyncHitbox3DObjectsUVE()`
  (the same engine-core home the RayCast3D/Projectile3D syncs use) pairs every enabled hitbox
  against every enabled Hurtbox3D with an exact 15-axis oriented-box-vs-oriented-box test (the
  same public Physics::Detail helper AreaOverlapSystemUVE uses), symmetric layer/mask
  acceptance, damage-channel equality, and self-exclusion, writing a bounded runtime-only
  strike list (hurtbox entity + penetration depth + minimum-translation axis, overflow flagged)
  back into the component every frame.
  The consequence contract is real too: `Physics::SyncHitboxes3DUVE()` owns the scan and
  `Physics::Hitbox3DStrikeLifecycleTrackerUVE` diffs it against the previous report, so a strike
  start and a strike end each reach gameplay as one typed event
  (`Hitbox3DStrikeEnteredEventUVE` / `Hitbox3DStrikeExitedEventUVE`, carrying the pair, the depth,
  the axis and the damage channel) - including the exit an authored gate causes, not only the one
  separation causes. What a strike *means* (damage numbers, knockback, i-frames, hit reactions)
  stays gameplay code by design; the engine reports the edge and never invents the consequence.
- [x] Hurtbox3D — the receiving side of that same pairing: its extents/layer/mask/channel
  genuinely gate which hitboxes can strike it every frame (locked by engine-core tests on both
  sides of every gate), and switching it off now ends every live strike on that tick with a real
  exit event instead of leaving a strike nothing can end. What being struck *means* is the same
  gameplay-owned consequence as Hitbox3D's.
- [x] SpawnPoint3D — the real thing: `Scene::QuerySpawnPointsUVE()` (bounded, overflow-flagged
  result list in (index, generation) order) filters by enabled/validator/world-transform/tag,
  composes each point's world pose with its authored offset, and spends one-shot points only when
  asked (`query.consumeOneShot`, or `Scene::ConsumeSpawnPointUVE` for a single point after a spawn
  that succeeded). The editor's play-entry spawn is now a caller of it rather than a second copy of
  its rules, and it is locked by 12 dedicated query tests plus the editor's own play-mode cases.
  Acting on a spawn (respawn flow, teams, per-player tags) is still the gameplay layer no system
  owns yet - the tag is carried precisely so that layer has something to ask with.
- [~] Decal3D — the lifetime runtime and the projected-geometry pass exist (countdown + expiry event, unit-space volume, patch clipping on receiving surfaces, frame diagnostics); the remaining gap is the decal DRAW: no program binds the patch stream yet, so nothing is visible on screen.
- [x] LODGroup3D — the chain is real: `Scene::ResolveLodGroup3DLevelUVE()` selects the level from the camera distance with a configurable hysteresis band (entered past a threshold, left under it, the previous level kept in the band), the renderer culls past the last threshold and draws `lodMeshGuids[level]` — falling back to the object's own `MeshComponentUVE` mesh for a level that overrides nothing — and a scene written before per-level meshes still loads unchanged. The Inspector authors the chain (levels, thresholds, band, per-level meshes) and shows the resolved level during Play.
- [~] AnimationSequencer — clip/speed/loop fields exist, nothing decodes a clip or evaluates a pose (see `ROADMAP.md`'s Animation section for the real gap: no skeleton/skinning/clip-sampling pipeline exists).
- [~] AnimationGraph — not even creatable yet in the editor (registry marks it `libraryCreatable = false`); depends on the same missing animation pipeline as AnimationSequencer.

### Missing entirely

- [ ] GPUParticles3D / CPUParticles3D — distinct hardware-accelerated vs. CPU-simulated particle emitters (clarify whether the existing ParticleEmitter3D should absorb this distinction or split in two).
- [ ] MultiMeshInstance3D — cheap mass-instanced mesh rendering.
- [ ] FogVolume — localized volumetric fog contribution.
- [ ] VoxelGI — real-time voxel-based global illumination probe.
- [ ] LightmapGI / LightmapProbe — baked static lighting + manual dynamic-object probes.
- [ ] Sprite3D / AnimatedSprite3D — billboard sprites in 3D space.
- [ ] Label3D — billboard 3D text.
- [ ] VehicleBody3D / VehicleWheel3D — raycast-vehicle car physics.
- [ ] PhysicalBone3D — ragdoll physics bone driven by a Skeleton3D.
- [ ] SoftBody3D — deformable cloth/jelly physics mesh.
- [ ] PinJoint3D / HingeJoint3D / SliderJoint3D / ConeTwistJoint3D / Generic6DOFJoint3D — 3D physics joint/constraint types.
- [ ] CollisionPolygon3D — extruded-polygon collision shape.
- [ ] NavigationObstacle3D / NavigationLink3D — navmesh obstacle carving and off-mesh links.
- [ ] RemoteTransform3D — pushes this node's transform onto another remote node.
- [ ] Path3D / PathFollow3D — curve authoring + travel-along-curve node.
- [ ] ShapeCast3D — swept-shape collision query node.
- [ ] VisibleOnScreenNotifier3D — signals when a region enters/exits camera view.
- [ ] AudioListener3D — overrides the point 3D audio is heard from.
- [ ] SpringBoneSimulator3D / SpringBoneCollision3D — secondary-motion "jiggle bone" simulation.
- [ ] GridMap — grid-based block/tile 3D level building.
- [ ] CSGBox3D / CSGSphere3D / CSGCylinder3D / CSGTorus3D / CSGMesh3D / CSGPolygon3D / CSGCombiner3D — constructive-solid-geometry primitives and boolean combiner for quick level blockouts.
- [ ] XROrigin3D / XRCamera3D / XRController3D / XRFaceModifier3D — AR/VR tracking-space origin, headset camera, controller, and face tracking.

---

## 2D Objects

Nothing in this section exists yet — this engine currently has no 2D rendering/physics/nav
pipeline at all.

- [ ] Sprite2D — 2D texture display.
- [ ] AnimatedSprite2D — frame-based 2D sprite animation.
- [ ] MeshInstance2D / MultiMeshInstance2D — 2D mesh rendering, single and instanced.
- [ ] Polygon2D — filled 2D polygon shape.
- [ ] Line2D — segmented, textured 2D polyline.
- [ ] CanvasModulate — global color tint for a canvas.
- [ ] CanvasGroup — merges child draw calls into one composite.
- [ ] BackBufferCopy — copies a screen region into a shader-readable buffer.
- [ ] LightOccluder2D — shadow-casting occluder for Light2D.
- [ ] Light2D / PointLight2D / DirectionalLight2D — 2D lighting with shadows.
- [ ] AudioListener2D / AudioStreamPlayer2D — 2D positional audio listener + player.
- [ ] RemoteTransform2D — pushes this node's transform onto a remote node.
- [ ] Marker2D — non-rendering 2D position/orientation hint.
- [ ] Path2D / PathFollow2D — 2D curve authoring + travel-along-curve.
- [ ] Parallax2D — scrolling-background depth effect.
- [ ] Bone2D / Skeleton2D — 2D skeletal animation hierarchy.
- [ ] CollisionShape2D / CollisionPolygon2D — 2D collision shape sources.
- [ ] Area2D — 2D overlap-detection trigger volume.
- [ ] StaticBody2D / RigidBody2D / CharacterBody2D — 2D physics bodies (static, simulated, kinematic-character).
- [ ] RayCast2D / ShapeCast2D — 2D raycast and swept-shape queries.
- [ ] PinJoint2D / GrooveJoint2D / DampedSpringJoint2D — 2D physics joints.
- [ ] NavigationRegion2D / NavigationAgent2D / NavigationObstacle2D / NavigationLink2D — 2D pathfinding region, agent, obstacle, and off-mesh link.
- [ ] Camera2D — 2D scrolling camera.
- [ ] VisibleOnScreenNotifier2D — signals when a region enters/exits the visible screen.
- [ ] GPUParticles2D / CPUParticles2D — 2D particle emitters.
- [ ] TouchScreenButton — on-screen touch input button.
- [ ] TileMapLayer — grid-based tile authoring/rendering.

---

## CanvasLayer / UI Objects

### Working today (Inspector-addable components, not yet promoted to the Scene node registry)

- [x] Canvas — UI root/layer concept.
- [x] UI Text — plain text label, real font-atlas rendering.
- [x] UI Image — textured rectangle, real rendering.
- [x] UI Button — real hover/press hit-testing against actual input.

These four are real and GPU-rendered (including in the live editor's own Viewport panel during
Play), but are only reachable via the Inspector's "Add Component" list today, not the Scene
panel's node-creation popup — worth reconciling later so UI authoring has one consistent entry
point.

### Missing entirely

- [ ] Layout containers — horizontal/vertical box, grid, flow-wrap, panel, center, margin, scroll, split, tab, aspect-ratio, sub-viewport containers.
- [ ] More buttons — checkbox, toggle switch, dropdown/option button, menu button, hyperlink button, texture-based button, color-picker button.
- [ ] More text/display — rich (formatted) text, single-line text input, multiline text editor, code editor, solid color rect, 9-slice panel, plain panel, reference/debug rect, video player.
- [ ] Value/range controls — scrollbar, slider, progress bar, texture-based progress bar, numeric spin box.
- [ ] Selection/lists — item list, hierarchical tree view, tab bar.
- [ ] Menus/windows/dialogs — sub-window, popup panel, popup menu, menu bar, accept dialog, confirmation dialog, file picker dialog.
- [ ] Color picker widget, on-screen virtual joystick.
- [ ] Node-graph editing canvas (graph editor + connectable graph nodes) — useful for building this engine's own future visual tools, not just player-facing UI.

---

## AI Objects / Components

Nothing placeable exists yet. `NavigationAgent3D`/`NavigationRegion3D` (listed above, 3D section)
are the closest existing pieces, and they are themselves still data-only stubs with no pathfinding
system behind them.

- [ ] AI controller — a non-rendering "brain" that possesses and drives an entity's behavior.
- [ ] Behavior tree (asset + runtime component) — data-driven decision tree of tasks/decorators/services.
- [ ] Blackboard (component + data asset) — typed shared memory for a behavior tree / decision system.
- [ ] Perception component (sight / hearing / damage stimuli) — detects other entities and world events.
- [ ] World-query system — queries the world for tactical decisions (best cover point, best target, etc.).
- [ ] Navigation crowd-following / avoidance component — follows a computed path while avoiding other agents.
- [ ] Smart object component — marks an entity as something an AI agent can discover and use.
- [ ] State tree / state-machine behavior — hierarchical state-machine authoring for behavior, as an alternative to a full behavior tree.

---

## Suggested near-term order

1. **Done**: every scene-node kind has its own `.h`+`.cpp` home under `Engine/Runtime/Objects`,
   grouped by domain: general 3D nodes under `3D`, physics-dependent 3D nodes under `3D/Physics`,
   AI/navigation nodes under `AI/3D`, and animation nodes under `Animation`. This grew out of
   splitting one shared node header; the old thin compatibility-alias facade layer in
   `Engine/Runtime/Scene` was also removed once confirmed nothing used it. This is purely
   structural: no `[~]` entry above changed status, since organizing a stub's data is not giving it
   a backing system. The 17 kinds whose authored data already lives in a shared component (Empty,
   Camera3D, Light3D, the three primitive meshes, physics bodies, Area3D, AudioSource3D,
   ParticleEmitter3D, Script, AnimationSequencer, AnimationGraph) each have their own `*ObjectDefinitionUVE`
   recipe in the owning folder, and the editor's creation switch sources those recipes instead of
   hardcoding them. The save format is untouched: a definition is a recipe, never a serialized
   component.
2. **RayCast3D, Projectile3D, and Hitbox3D/Hurtbox3D done** (real per-frame raycast against the
   actual query system with correct self-exclusion and authored exclusions; real kinematic
   integration, lifetime expiry *and* swept-sphere hit resolution with an authored stop/bounce
   motion policy for projectiles; real per-frame hitbox-vs-hurtbox strike pairing whose every
   start and end now leaves the engine as one typed edge event, so gameplay acts on the strike
   rather than re-deriving it from the per-frame list).
   Wire up the remaining highest-value already-authored 3D
   stubs next: Skeleton3D + AnimationSequencer + AnimationGraph (blocked on the same missing
   skinning/clip-sampling pipeline — see `ROADMAP.md`), NavigationRegion3D/NavigationAgent3D
   (needed for any AI movement).
3. Only after 3D nodes are in good shape, start a real 2D pipeline (rendering + physics + nav) —
   right now 2D is 100% unstarted, not partially built.
4. **Done**: Canvas/UI Text/UI Image/UI Button are promoted into the Scene node registry
   (`canvas`/`ui_text`/`ui_image`/`ui_button`, category "UI"), each with a `*ObjectDefinitionUVE`
   `.h`+`.cpp` in `Engine/Runtime/Objects/UI` following the Objects/3D convention — 2D/UI
   authoring now has the same single Add-Object entry point, and the Add-Component path still
   works for adding these components to existing entities.
5. AI nodes come last — they need real navigation (item 2/3) and real gameplay systems to act on
   before a behavior tree/blackboard has anything meaningful to drive.
