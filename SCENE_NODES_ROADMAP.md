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

A row's marker is the verdict, checked against the code rather than inherited: `[x]` means the system
that consumes the object exists and is tested, `[~]` means it is still authored data waiting for one.
Several rows below were corrected in BOTH directions once the systems landed (or were found already
there).

- [x] NavigationRegion3D — it bakes. `Navigation::BakeNavmeshUVE()` rasterizes the region's own world
  volume through `IRaycastSystemUVE` - ground rays, slope and headroom gates, agent-radius erosion,
  merged polygons and the shared-edge portals between them - so what the navmesh believes is walkable
  is exactly what a body standing there collides with. `NavigationRuntimeUVE` keeps one mesh per
  region and re-bakes it on first sight, after a moved or resized volume, after changed bake
  settings, or when `rebuildRequested` asks; the request is cleared once the bake has run. The mesh
  records the agent it was baked for, so an agent wider than that radius is visibly a different
  question rather than a silent one.
- [x] NavigationAgent3D — it walks. `Navigation::FindNavPathUVE()` A*s the polygon graph under the
  request's `navigationLayers` and string-pulls the chain into the corners a body actually walks;
  `NavAgentUVE` follows that route, skipping waypoints it has passed, easing the published velocity
  through its acceleration limit, slowing into the target and pushing aside the agents inside its
  avoidance radius. `EngineCoreUVE::SyncNavigationUVE()` drives it on the fixed step and writes
  `desiredVelocity`, `nextPathPosition`, `pathStatus`, `pathChanged` and `targetReached` back into
  `NavSeeker3DComponentUVE` on that agent's own `pathUpdateInterval`.
- [x] Skeleton3D — real bones, posed and skinned. A rigged model's hierarchy is imported (glTF/FBX
  skeleton readers, with their own tests), the clip pipeline writes the runtime `pose` per bone every
  step, and the renderer poses a skinned mesh against the nearest Skeleton3D above it and re-uploads
  that entity's own vertex buffer every frame (`MeshSkinComputeUVE`, locked by 17 skinning cases).
  Reading a bone's world frame is a public resolver (`Scene::TryResolveSkeletonBoneWorldFrameUVE()`,
  chain composed root-down, posed value where the runtime has one and the rest pose otherwise).
- [x] BoneAttachment3D — the real thing: `Scene::SyncBoneAttachment3DObjectsUVE()` runs every frame
  after the animation step that posed the skeleton and before world-transform propagation, so an
  attachment is on the bone in the same frame the pose arrives. It resolves the skeleton entity
  reference, the bone (by index when that names a real bone, else by exact name), the bone's world
  frame and writes the object's LOCAL transform so the scene graph's own propagation lands it exactly
  on the bone - parents-first, so an attachment riding another attachment is composed after the one
  it hangs off, and tick modes are respected like every other system. A reference that names nothing,
  a bone the skeleton no longer has, a skeleton switched off or a parent frame that cannot be
  inverted leaves the object where it was authored and records itself unbound (`bound` and
  `resolvedBoneIndex`, runtime-only, both shown in the Inspector). Locked by 7 pass tests on a real
  entity manager + scene graph, 5 resolver cases, the serializer's reference round trip (including
  documents written while the skeleton was still a bare numeric id) and an engine-core tick test.
- [x] TwoBoneIK3D — a limb solved back from where it should end, which is what foot planting and hand
  placement actually are: `Scene::SyncTwoBoneIK3DObjectsUVE()` runs inside the animation step after the
  drivers pose a skeleton and before the attachment pass, so a weapon in a hand follows the hand the IK
  moved in the same frame. The chain is three successive bones and the END bone's own origin is the
  effector; the solve is analytic (the intersection of two circles in the plane the pole defines), so
  it costs no iterations and returns the same answer every time, the pole is another object's position
  seen from the chain's root or an authored direction in the skeleton's space, and a target beyond the
  chain's own length leaves the limb straight and aimed rather than stretched. Influence comes from the
  shared `BoneModifierComponentUVE`, so an arm can ease onto a prop instead of snapping to it; the
  answers (`solved`, `reached`, `endToTargetDistanceMetres`, the resolved indices) are runtime-only and
  shown in the Inspector. Locked by 9 solver cases, 8 pass cases, the serializer's reference round trip
  and an engine-core tick test.
- [x] Marker3D — position/orientation hint with `Marker3DUVE` world-pose composition/axis helpers, `Scene::QueryMarkers3DUVE` / `TryFindMarker3DPoseByNameUVE` query seam, UVScript `find_marker_position` host binding, and Editor viewport fly-to-marker support.
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
- [x] Decal3D — it draws. The lifetime runtime counts down and reports the expiry, the pass clips
  the receiving surfaces against the box or cylinder volume and publishes the surviving polygons, and
  the frame now paints them: `decal.glsl` (a built-in program) projects each patch, evaluates the
  authored fades per pixel against the SAME world-to-unit matrix the CPU clipped in, and blends the
  material's albedo texture and colour - tinted by `modulate`, plus its emissive scaled by
  `emissionEnergy`, with the texture's alpha joining `albedoMix` - over the surface, back to front. Each decal draws from its own vertex/index buffers, created
  the first frame it paints and released once it stops - the same lifetime rule the skinned-mesh
  buffers follow - and the F1 panel reports both the draws extracted and the draw calls recorded, so
  a decal whose geometry never reaches the GPU is visible as its own failure.
- [x] LODGroup3D — the chain is real: `Scene::ResolveLodGroup3DLevelUVE()` selects the level from the camera distance with a configurable hysteresis band (entered past a threshold, left under it, the previous level kept in the band), the renderer culls past the last threshold and draws `lodMeshGuids[level]` — falling back to the object's own `MeshComponentUVE` mesh for a level that overrides nothing — and a scene written before per-level meshes still loads unchanged. The Inspector authors the chain (levels, thresholds, band, per-level meshes) and shows the resolved level during Play.
- [x] AnimationSequencer — clips really decode and play: `.uvanim` assets load through the asset
  manager, `StepAnimationSequencerUVE()` samples a clip at the playhead with Once/Loop/Ping-Pong,
  blend-in, relative mode, start offset and speed (negative plays backwards), per-channel masks leave
  the channels an author did not ask for alone, skeletal clips pose the skeleton's bones by name,
  clip events reach the target's script, and root motion can be left out, kept in place, or applied
  to the target - as velocity when the target is a Character3D, so walls still stop it.
- [x] AnimationGraph — creatable in the editor (the registry descriptor's `libraryCreatable` is true
  and the graph panel authors states, transitions and blend spaces) and evaluated every fixed step by
  `EngineCoreUVE::SyncAnimationUVE()`: state machine with conditions/triggers/leave-after, blend tree,
  1D and 2D blend spaces, additive layers, one-shots and time scaling, all locked by the pose-graph
  and state-machine suites.

### Implemented in Batch 3–5 (Particles, GI Probes, Instancing, Billboards, Physics, AI/Nav, CSG, GridMap, and XR)

- [x] GPUParticles3D / CPUParticles3D — distinct hardware-accelerated (`ParticleSimulationBackend3DUVE::GPU`) vs. CPU-simulated (`ParticleSimulationBackend3DUVE::CPU`) particle emitter recipes (`GPUParticles3DObjectDefinitionUVE` / `CPUParticles3DObjectDefinitionUVE` + `ParticleBackendTagComponentUVE` on top of `ParticleEmitterComponentUVE`, locked by `Object3DDefinitionsUVETest.AppliesBatch5Particles3DGIProbes3DAndXRObjects3DUVE`).
- [x] MultiMeshInstance3D — mass-instanced mesh rendering built on `SurfaceInstance3DBaseUVE` (`MultiMeshInstance3DComponentUVE` + `MultiMeshInstance3DUVE::{ResolveActiveInstanceCountUVE, BuildInstanceWorldMatrixUVE, ComputeCombinedWorldBoundsUVE}`, locked by `Object3DDefinitionsUVETest.MultiMeshInstance3DSprite3DAnimatedSprite3DAndLabel3DExtendSurfaceInstanceBase`).
- [x] FogVolume — localized volumetric fog contribution (`FogVolume3DComponentUVE` + `FogVolume3DObjectDefinitionUVE`, uploaded as localized 3D fog volumes by `Renderer3DUVE` and locked by `Test/Objects/3D/fog_volume_3d_uve_tests.cpp`).
- [x] VoxelGI — real-time voxel-based global illumination probe (`VoxelGI3DComponentUVE` + `VoxelGI3DObjectDefinitionUVE` + `EvaluateIndirectGI3DIrradianceAtPointUVE()`, locked by `Object3DDefinitionsUVETest.AppliesBatch5Particles3DGIProbes3DAndXRObjects3DUVE`).
- [x] LightmapGI / LightmapProbe — baked static lighting + manual dynamic-object irradiance probes (`LightmapGI3DComponentUVE` + `LightmapProbe3DComponentUVE` + `EvaluateIndirectGI3DIrradianceAtPointUVE()`, locked by `Object3DDefinitionsUVETest.AppliesBatch5Particles3DGIProbes3DAndXRObjects3DUVE`).
- [x] Sprite3D / AnimatedSprite3D — billboard sprites and sprite-sheet frame animation in 3D space built on `SurfaceInstance3DBaseUVE` (`Sprite3DComponentUVE` + `Sprite3DUVE::{ResolveBillboardWorldRotationUVE, ResolveFrameUVRectUVE, StepAnimationUVE}` + `UpdateSprite3DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Object3DDefinitionsUVETest.MultiMeshInstance3DSprite3DAnimatedSprite3DAndLabel3DExtendSurfaceInstanceBase`).
- [x] Label3D — billboard 3D text built on `SurfaceInstance3DBaseUVE` (`Label3DComponentUVE` + `Label3DUVE::EstimateWorldSizeMetresUVE()`, locked by `Object3DDefinitionsUVETest.MultiMeshInstance3DSprite3DAnimatedSprite3DAndLabel3DExtendSurfaceInstanceBase`).
- [x] VehicleBody3D / VehicleWheel3D — raycast-vehicle car physics (`VehicleBody3DComponentUVE`, `VehicleWheel3DComponentUVE`, `EvaluateVehicleWheelSuspensionForceUVE()`, and `UpdateVehicle3DObjectsUVE()`, wired into `EngineCoreUVE::Update()` and locked by `Object3DDefinitionsUVETest.Vehicle3DPhysicalBone3DSoftBody3DAndCollisionPolygon3DContracts`).
- [x] PhysicalBone3D — ragdoll physics bone driven by or driving a `Skeleton3D` (`PhysicalBone3DComponentUVE` + `UpdatePhysicalBone3DObjectsUVE()`, wired into `EngineCoreUVE::Update()` and locked by `Object3DDefinitionsUVETest.Vehicle3DPhysicalBone3DSoftBody3DAndCollisionPolygon3DContracts`).
- [x] SoftBody3D — deformable cloth/jelly physics mesh built on `SurfaceInstance3DBaseUVE` (`SoftBody3DComponentUVE` + `UpdateSoftBody3DObjectsUVE()`, wired into `EngineCoreUVE::Update()` and locked by `Object3DDefinitionsUVETest.Vehicle3DPhysicalBone3DSoftBody3DAndCollisionPolygon3DContracts`).
- [x] PinJoint3D / HingeJoint3D / SliderJoint3D / ConeTwistJoint3D / Generic6DOFJoint3D — 3D physics joint/constraint nodes (`Joint3DComponentUVE` + per-joint `*ObjectDefinitionUVE` recipes + `UpdateJoint3DObjectsUVE()` syncing with `PhysicsConstraintSystemUVE` and `EvaluateHingeMotorLimitUVE`, wired into `EngineCoreUVE::Update()` and locked by `Object3DDefinitionsUVETest.Joint3DDefinitionsSyncWithPhysicsConstraintSystemAndHingeMotorLimits`).
- [x] CollisionPolygon3D — extruded-polygon collision shape (`CollisionPolygon3DComponentUVE` + `CollisionPolygon3DUVE::{ComputeLocalAabbUVE, ContainsLocalPointUVE}`, locked by `Object3DDefinitionsUVETest.Vehicle3DPhysicalBone3DSoftBody3DAndCollisionPolygon3DContracts`).
- [x] NavigationObstacle3D / NavigationLink3D — dynamic navigation obstacle avoidance repulsion (`NavigationObstacle3DComponentUVE` + `ApplyNavigationObstacles3DToAgentsUVE()`) and off-mesh traversal link (`NavigationLink3DComponentUVE` + `ResolveNavigationLink3DWorldEndpointsAndCostUVE()`), wired into `EngineCoreUVE::SyncNavigationUVE()` and locked by `Object3DDefinitionsUVETest.NavigationObstacle3DNavigationLink3DSmartObject3DAndWorldQuery3DContracts`.
- [x] RemoteTransform3D — pushes this node's transform channels (`position`, `rotation`, `scale`) onto another remote entity (`RemoteTransform3DComponentUVE` + `UpdateRemoteTransform3DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Object3DDefinitionsUVETest.RemoteTransform3DPushesSelectedChannelsToTargetEntity`).
- [x] Path3D / PathFollow3D — curve authoring + travel-along-curve node (`Path3DComponentUVE` + `Path3DUVE::SampleWorldPoseAtDistanceUVE` + `PathFollow3DComponentUVE` + `UpdatePathFollow3DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Object3DDefinitionsUVETest.Path3DAndPathFollow3DSampleAndStepFollowerTransform`).
- [x] ShapeCast3D — swept-shape (`Sphere`, `Box`, `Capsule`) collision query node (`ShapeCast3DComponentUVE` + `UpdateShapeCast3DObjectsUVE()` backed by `Physics::ShapeCastSystemUVE`, wired into `EngineCoreUVE::SyncRayCast3DObjectsUVE()` and locked by `Object3DDefinitionsUVETest.ShapeCast3DSweepsSphereAgainstColliderAndRecordsHit`).
- [x] VisibleOnScreenNotifier3D — tracks when a local-space AABB enters or exits the active camera frustum (`VisibleOnScreenNotifier3DComponentUVE` + `UpdateVisibleOnScreenNotifier3DObjectsUVE()`, wired into `EngineCoreUVE::Update()` and locked by `Object3DDefinitionsUVETest.VisibleOnScreenNotifier3DTracksCameraFrustumTransitions`).
- [x] AudioListener3D — overrides the point and orientation 3D spatial audio is heard from (`AudioListener3DComponentUVE` + `AudioListener3DUVE::TryResolveActiveListenerPoseUVE()`, wired into `EngineCoreUVE::LateUpdate()` and locked by `Object3DDefinitionsUVETest.AudioListener3DRecipeAndActiveResolutionOverrideCameraPose`).
- [x] SpringBoneSimulator3D / SpringBoneCollision3D — secondary-motion "jiggle bone" spring simulation with sphere obstacle push-out (`SpringBoneSimulator3DComponentUVE` + `SpringBoneCollision3DComponentUVE` + `UpdateSpringBoneSimulator3DObjectsUVE()`, wired into `EngineCoreUVE::SyncBoneAttachment3DObjectsUVE()` and locked by `Object3DDefinitionsUVETest.SpringBoneSimulator3DStepsTipAndPushesOutOfCollisionSphere`).
- [x] GridMap — grid-based block/tile 3D level building (`GridMap3DComponentUVE` + `GridMap3DUVE::{MapToLocalPositionUVE, LocalPositionToMapUVE, SetCellItemUVE, GetCellItemUVE}`, locked by `Object3DDefinitionsUVETest.CSGShapesGridMap3DAndStateTree3DContracts`).
- [x] CSGBox3D / CSGSphere3D / CSGCylinder3D / CSGTorus3D / CSGMesh3D / CSGPolygon3D / CSGCombiner3D — constructive-solid-geometry primitives and boolean combiner built on `SurfaceInstance3DBaseUVE` (`CSGShape3DComponentUVE` + per-kind `*ObjectDefinitionUVE` recipes + `CSGShape3DUVE::{ComputeLocalAabbUVE, EvaluateLocalSignedDistanceUVE, CombineSignedDistanceUVE}`, locked by `Object3DDefinitionsUVETest.CSGShapesGridMap3DAndStateTree3DContracts`).
- [x] XROrigin3D / XRCamera3D / XRController3D / XRFaceModifier3D — AR/VR tracking-space origin, headset camera, controller, and face-expression tracking (`XROrigin3DComponentUVE`, `XRCamera3DComponentUVE`, `XRController3DComponentUVE`, `XRFaceModifier3DComponentUVE`, and `UpdateXRObjects3DUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Object3DDefinitionsUVETest.AppliesBatch5Particles3DGIProbes3DAndXRObjects3DUVE`).

---

## 2D Objects

Sprite2D v1 has landed as scene data (component + definition + quad composer + registry +
editor creation + save/load), and Renderer2D v1 + Camera2DSystem v1 draw it, alongside 2D physics, lighting, navigation, audio, animation, and input runtime evaluators.

- [x] Sprite2D — 2D texture display (v1: data + quad composer; drawn by Renderer2D v1).
- [x] AnimatedSprite2D — frame-based 2D sprite animation (`AnimatedSprite2DComponentUVE` + `UpdateAnimatedSprite2DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).
- [x] MeshInstance2D / MultiMeshInstance2D — 2D mesh rendering, single and instanced (`MeshInstance2DComponentUVE`, `MultiMeshInstance2DComponentUVE`, and `MultiMeshInstance2DUVE::ComputeCombinedLocalBoundsUVE()`, locked by `Camera2DObjectDefinitionTest.AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE`).
- [x] Polygon2D — filled 2D polygon shape (`Polygon2DComponentUVE` + `Polygon2DUVE::{ComputeSignedAreaUVE, ContainsLocalPointUVE}`, locked by `Camera2DObjectUVETest.Path2DPathFollow2DLine2DPolygon2DAndCanvasModulateContractsUVE`).
- [x] Line2D — segmented 2D polyline (`Line2DComponentUVE` + `Line2DUVE::{ResolveSegmentCountUVE, ComputeLocalBoundingRectUVE}`, locked by `Camera2DObjectUVETest.Path2DPathFollow2DLine2DPolygon2DAndCanvasModulateContractsUVE`).
- [x] CanvasModulate — global color and alpha tint for a 2D canvas (`CanvasModulateComponentUVE` + `ResolveActiveCanvasModulateUVE()`, locked by `Camera2DObjectUVETest.Path2DPathFollow2DLine2DPolygon2DAndCanvasModulateContractsUVE`).
- [x] CanvasGroup — merges child draw calls into one composite (`CanvasGroupComponentUVE` + `CanvasGroupObjectDefinitionUVE`, locked by `Camera2DObjectDefinitionTest.AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE`).
- [x] BackBufferCopy — copies a screen region into a shader-readable buffer (`BackBufferCopyComponentUVE` + `BackBufferCopyObjectDefinitionUVE`, locked by `Camera2DObjectDefinitionTest.AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE`).
- [x] LightOccluder2D — shadow-casting occluder for Light2D (`LightOccluder2DComponentUVE` + segment-blocking occlusion in `EvaluateLight2DIlluminationAtPointUVE()`).
- [x] Light2D / PointLight2D / DirectionalLight2D — 2D lighting with shadows (`Light2DComponentUVE` + `Light2DObjectDefinitionUVE` / `PointLight2DObjectDefinitionUVE` / `DirectionalLight2DObjectDefinitionUVE` + `EvaluateLight2DIlluminationAtPointUVE()`, locked by `Camera2DObjectDefinitionTest.AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE`).
- [x] AudioListener2D / AudioStreamPlayer2D — 2D positional audio listener + player (`AudioListener2DComponentUVE`, `AudioStreamPlayer2DComponentUVE`, and `UpdateAudioAnimNavInput2DObjectsUVE()` distance attenuation, wired into `EngineCoreUVE::SyncCameraFollowUVE()`).
- [x] RemoteTransform2D — pushes this 2D node's X/Y position, rotation, and X/Y scale channels onto a remote entity (`RemoteTransform2DComponentUVE` + `UpdateRemoteTransform2DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Camera2DObjectUVETest.Marker2DRemoteTransform2DAndVisibleOnScreenNotifier2DContractsUVE`).
- [x] Marker2D — non-rendering 2D position/orientation hint (`Marker2DComponentUVE` + `Marker2DUVE::{ComposeWorldPositionUVE, ResolveForwardDirectionUVE}`, locked by `Camera2DObjectUVETest.Marker2DRemoteTransform2DAndVisibleOnScreenNotifier2DContractsUVE`).
- [x] Path2D / PathFollow2D — 2D curve authoring + travel-along-curve (`Path2DComponentUVE` + `Path2DUVE::SamplePoseAtDistanceUVE` + `PathFollow2DComponentUVE` + `UpdatePathFollow2DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Camera2DObjectUVETest.Path2DPathFollow2DLine2DPolygon2DAndCanvasModulateContractsUVE`).
- [x] Parallax2D — scrolling-background depth effect (`Parallax2DComponentUVE` + `Parallax2DUVE::ComputeEffectiveOffsetUVE()`, locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).
- [x] Bone2D / Skeleton2D — 2D skeletal animation hierarchy (`Bone2DComponentUVE` + `Skeleton2DComponentUVE` + forward-kinematics evaluation in `UpdateAudioAnimNavInput2DObjectsUVE()`).
- [x] CollisionShape2D / CollisionPolygon2D — 2D collision shape sources (`CollisionShape2DComponentUVE`, `CollisionPolygon2DComponentUVE`, locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).
- [x] Area2D — 2D overlap-detection trigger volume (`Area2DComponentUVE` + `UpdatePhysics2DObjectsUVE()`, wired into `EngineCoreUVE::Update()` and locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).
- [x] StaticBody2D / RigidBody2D / CharacterBody2D — 2D physics bodies (`PhysicsBody2DComponentUVE` + `Static2DObjectDefinitionUVE` / `Rigid2DObjectDefinitionUVE` / `Character2DObjectDefinitionUVE` + `UpdatePhysics2DObjectsUVE()`, wired into `EngineCoreUVE::Update()` and locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).
- [x] RayCast2D / ShapeCast2D — 2D raycast and swept-shape queries (`RayCast2DComponentUVE`, `ShapeCast2DComponentUVE`, evaluated by `UpdatePhysics2DObjectsUVE()` and locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).
- [x] PinJoint2D / GrooveJoint2D / DampedSpringJoint2D — 2D physics joints (`Joint2DComponentUVE` + per-joint `*ObjectDefinitionUVE` recipes solved in `UpdatePhysics2DObjectsUVE()`).
- [x] NavigationRegion2D / NavigationAgent2D / NavigationObstacle2D / NavigationLink2D — 2D pathfinding region, agent, obstacle repulsion, and off-mesh link (`NavigationRegion2DComponentUVE`, `NavigationAgent2DComponentUVE`, `NavigationObstacle2DComponentUVE`, `NavigationLink2DComponentUVE`, and `UpdateAudioAnimNavInput2DObjectsUVE()`, wired into `EngineCoreUVE::SyncCameraFollowUVE()` and locked by `Camera2DObjectDefinitionTest.AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE`).
- [x] Camera2D — 2D scrolling camera (v1: data; consumed by Renderer2D v1 through Camera2DSystem v1).
- [x] VisibleOnScreenNotifier2D — signals when a 2D bounding rectangle enters/exits the visible screen (`VisibleOnScreenNotifier2DComponentUVE` + `UpdateVisibleOnScreenNotifier2DObjectsUVE()`, locked by `Camera2DObjectUVETest.Marker2DRemoteTransform2DAndVisibleOnScreenNotifier2DContractsUVE`).
- [x] GPUParticles2D / CPUParticles2D — 2D particle emitters (`Particles2DComponentUVE` + `GPUParticles2DObjectDefinitionUVE` / `CPUParticles2DObjectDefinitionUVE` + `UpdateAudioAnimNavInput2DObjectsUVE()`).
- [x] TouchScreenButton — on-screen touch input button (`TouchScreenButtonComponentUVE` + `TouchScreenButtonUVE::EvaluateTouchHitUVE()`, locked by `Camera2DObjectDefinitionTest.AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE`).
- [x] TileMapLayer — grid-based tile authoring/rendering (`TileMapLayerComponentUVE` + `TileMapLayerUVE::{MapToLocalPositionUVE, LocalPositionToMapUVE, SetCellTileUVE, GetCellTileUVE}`, locked by `Camera2DObjectUVETest.Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE`).

---

## CanvasLayer / UI Objects

### Working today

- [x] Canvas — UI root/layer concept (`CanvasObjectDefinitionUVE` + `CanvasComponentUVE`, creatable from the Scene node registry).
- [x] UI Text — plain text label, real font-atlas rendering (`UITextObjectDefinitionUVE` + `UITextComponentUVE`).
- [x] UI Image — textured rectangle and 9-slice panel (`UIImageObjectDefinitionUVE` + `UIImageComponentUVE`).
- [x] UI Button — real hover/press hit-testing against actual input (`UIButtonObjectDefinitionUVE` + `UIButtonComponentUVE`).
- [x] Layout containers (horizontal/vertical box, grid, scroll, anchor) — `UILayoutContainerComponentUVE`, `UIScrollContainerComponentUVE`, and `UIAnchorComponentUVE` driven every frame by `UIRuntimeUVE` and covered by UI runtime + component domain tests.
- [x] Checkbox, dropdown, and tooltip controls — `UICheckboxComponentUVE`, `UIDropdownComponentUVE`, and `UITooltipComponentUVE` driven by `UIRuntimeUVE` hit-testing, focus, and draw generation.
- [x] Single-line text input — `UITextInputComponentUVE` with cursor, selection, max-length clamping, and keyboard editing in `UIRuntimeUVE`.
- [x] Value/range controls & UI tweens — `UIScrollbarComponentUVE`, `UISliderComponentUVE`, `UIProgressBarComponentUVE`, and `UITweenComponentUVE` evaluated and rendered by `UIRuntimeUVE`.
- [x] Additional layout containers — flow-wrap, panel, center, margin, split, tab, aspect-ratio, sub-viewport containers (`UIExtendedContainerComponentUVE` + `UIExtendedContainerUVE::ResolveContentRectUVE()`, locked by `CanvasObjectDefinitionsUVETest`).
- [x] Additional buttons — toggle switch, menu button, hyperlink button, texture-based button, color-picker button (`UIExtendedButtonComponentUVE` + `UIExtendedButtonObjectDefinitionUVE`).
- [x] Additional text/display — rich (formatted) text, multiline text editor, code editor, solid color rect, plain panel, reference/debug rect, video player (`UIDisplayWidgetComponentUVE` + `UpdateUIExtendedObjectsUVE()`).
- [x] Additional value/range controls — texture-based progress bar, numeric spin box (`UIRangeControlComponentUVE` + step quantization in `UpdateUIExtendedObjectsUVE()`, locked by `CanvasObjectDefinitionsUVETest`).
- [x] Selection/lists — item list, hierarchical tree view, tab bar (`UISelectionWidgetComponentUVE` + `UISelectionWidgetObjectDefinitionUVE`).
- [x] Menus/windows/dialogs — sub-window, popup panel, popup menu, menu bar, accept dialog, confirmation dialog, file picker dialog (`UIDialogWindowComponentUVE` + `UIDialogWindowObjectDefinitionUVE`).
- [x] Color picker widget, on-screen virtual joystick (`UISpecialInputComponentUVE` + radial deadzone normalization in `UpdateUIExtendedObjectsUVE()`).
- [x] Node-graph editing canvas (graph editor + connectable graph nodes) — `UIGraphEditComponentUVE` + `UIGraphEditUVE::TryConnectPortsUVE()`, locked by `CanvasObjectDefinitionsUVETest`.

---

## AI Objects / Components

`NavigationRegion3D` (`NavMeshVolume3D`) and `NavigationAgent3D` (`NavSeeker3D`) are listed above in the 3D section, and the core AI decision, memory, and sensing components below are live and ticked by `EngineCoreUVE`:

- [x] AI controller / Utility AI brain — `AiBrainComponentUVE` evaluated every step by `EngineCoreUVE::SyncAiBrainsUVE()` (`SelectAiActionUVE()` scores considerations against `BlackboardComponentUVE` and emits `Ai::AiActionSelectedUVE`).
- [x] Behavior tree (asset + runtime component) — `BehaviorTreeComponentUVE` evaluated against `BlackboardComponentUVE` with sequence/selector/action/condition nodes (`Test/AI/behavior_tree_uve_tests.cpp`).
- [x] Blackboard (component + data asset) — `BlackboardComponentUVE` typed shared memory (`int`, `float`, `bool`, `Vector3UVE`, `EntityUVE`) read and written by perception, behavior trees, and utility AI.
- [x] Perception component (sight / hearing stimuli) — `PerceptionComponentUVE` and `NoiseEmitterComponentUVE` ticked by `EngineCoreUVE::SyncPerceptionUVE()` (sight cone + raycast occlusion and hearing radius decay written into `BlackboardComponentUVE`).
- [x] World-query system — `WorldQuery3DComponentUVE` (`WorldQuery3DObjectDefinitionUVE` + `UpdateSmartObjectAndWorldQuery3DUVE()`, wired into `EngineCoreUVE::SyncAiBrainsUVE()` and locked by `Object3DDefinitionsUVETest.NavigationObstacle3DNavigationLink3DSmartObject3DAndWorldQuery3DContracts`).
- [x] Navigation crowd-following / avoidance component — `NavSeeker3DComponentUVE` (`NavAgentUVE` path following + neighbor avoidance radius separation in `EngineCoreUVE::SyncNavigationUVE()`).
- [x] Smart object component — `SmartObject3DComponentUVE` (`SmartObject3DObjectDefinitionUVE` + `SmartObject3DUVE::{CanClaimUVE, TryClaimUVE, ReleaseUVE, ResolveWorldInteractionPointUVE}`, wired into `EngineCoreUVE::SyncAiBrainsUVE()` and locked by `Object3DDefinitionsUVETest.NavigationObstacle3DNavigationLink3DSmartObject3DAndWorldQuery3DContracts`).
- [x] State tree / state-machine behavior — `StateTree3DComponentUVE` (`StateTree3DObjectDefinitionUVE` + `StateTree3DUVE::{SetParameterUVE, GetParameterUVE, StepUVE}` + `UpdateStateTree3DObjectsUVE()`, wired into `EngineCoreUVE::SyncAiBrainsUVE()` and locked by `Object3DDefinitionsUVETest.CSGShapesGridMap3DAndStateTree3DContracts`).

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
   Both pairs this step named have since landed: the animation pipeline
   (Skeleton3D + AnimationSequencer + AnimationGraph) and NavigationRegion3D/NavigationAgent3D.
3. Only after 3D nodes are in good shape, start a real 2D pipeline (rendering + physics + nav) —
   right now 2D is 100% unstarted, not partially built.
4. **Done**: Canvas/UI Text/UI Image/UI Button are promoted into the Scene node registry
   (`canvas`/`ui_text`/`ui_image`/`ui_button`, category "UI"), each with a `*ObjectDefinitionUVE`
   `.h`+`.cpp` in `Engine/Runtime/Objects/UI` following the Objects/3D convention — 2D/UI
   authoring now has the same single Add-Object entry point, and the Add-Component path still
   works for adding these components to existing entities.
5. AI nodes come last — they need real navigation (item 2/3) and real gameplay systems to act on
   before a behavior tree/blackboard has anything meaningful to drive.
