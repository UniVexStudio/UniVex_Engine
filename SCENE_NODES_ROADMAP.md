# Scene Nodes Roadmap

A detailed, node-by-node checklist of every placeable scene node / UI element / AI element this
engine should eventually offer, grouped by domain (3D, 2D, CanvasLayer/UI, AI). This is a companion
to the top-level `ROADMAP.md` — that file tracks whole engine systems; this file tracks individual
node types specifically, since "the node exists in the Add-Node list" and "the node actually does
something at runtime" are two different, easily-confused claims.

No third-party engine or product name appears anywhere in this document — node names below are
generic, descriptive names for well-understood game-engine concepts, not references to a specific
product.

## How to read this file

- `[x]` — the node exists AND is backed by a real system that ticks/uses it every frame. Attaching
  it to an entity has a real, verifiable effect.
- `[~]` — the node exists as authored data (fields + validation) but nothing reads or writes it at
  runtime yet. It can be added in the editor and will save/load correctly, but it does nothing.
- `[ ]` — the node does not exist at all yet, in any form.

Update this file in the same change that adds, fixes, or wires up any node. A stale checklist is
worse than no checklist.

---

## 3D Nodes

### Working today

- [x] Empty — plain transform-only node, the base of every scene hierarchy.
- [x] Camera3D — real camera, drives view/projection for rendering.
- [x] MeshInstance3D — real mesh + material rendering through the lit shader pipeline.
- [x] BoxMesh3D / SphereMesh3D / PlaneMesh3D — primitive mesh shapes, real rendering.
- [x] Light3D (Directional / Point / Spot) — real, shades meshes.
- [x] Collider3D — real collision shape, used by the physics/collision systems.
- [x] StaticBody3D — real, non-moving collidable body.
- [x] RigidBody3D — real, physics-simulated body (gravity, collision response).
- [x] CharacterBody3D — real kinematic character controller (move/jump/ground state).
- [x] AudioSource3D — real, plays positional audio.
- [x] ParticleEmitter3D — real, ticked particle simulation.
- [x] Area3D — real overlap-detection trigger volume.
- [x] WorldEnvironment3D — real, feeds ambient color/energy into the renderer.
- [x] Script — real, ticked by the script VM/runtime, with real input/collision bindings.

### Authored data only, not yet wired to a system

- [~] RayCast3D — has direction/length/hit fields, but nothing performs the raycast or fills them in automatically each frame (the underlying raycast *query* system exists, it just isn't hooked to this node type yet).
- [~] AnimatableBody3D — target-velocity fields exist, no system drives a kinematic body from them.
- [~] NavigationRegion3D — bounds + navmesh path fields exist, no navmesh baking/pathfinding system exists yet.
- [~] NavigationAgent3D — target/path fields exist, no pathfinding/steering system exists yet.
- [~] Skeleton3D — bone hierarchy data exists, no skinning/animation system reads it.
- [~] BoneAttachment3D — attach-to-bone fields exist, nothing resolves/follows a bone transform.
- [~] SpringArm3D — arm-length/collision fields exist, no camera-boom system consumes it.
- [~] Marker3D — a plain position/orientation hint, has no behavior by design (this one may never need a "system" — it's meant to be read by other tools/scripts, not ticked itself).
- [~] Hitbox3D — extents/damage-channel fields exist, no combat/damage system consumes them.
- [~] Hurtbox3D — same as Hitbox3D, other side of the interaction.
- [~] Projectile3D — velocity/lifetime fields exist, no system moves it or expires it.
- [~] InteractionArea3D — candidate-tracking fields exist, no interact/prompt system consumes it.
- [~] ReflectionProbe3D — size/update-mode fields exist, no reflection-probe capture/render system exists.
- [~] Decal3D — material/size/lifetime fields exist, no decal-projection rendering exists.
- [~] LODGroup3D — distance-threshold fields exist, no LOD-switching system exists.
- [~] Occluder3D — extents/mode fields exist, no occlusion-culling system exists.
- [~] VisibilityRegion3D — extents/layers fields exist, no visibility-culling system consumes it.
- [~] SpawnPoint3D — tag/one-shot fields exist, no spawn system reads it.
- [~] LevelStreamer3D — load/unload distance fields exist, no level-streaming system exists.
- [~] WorldPartition3D — cell-size/loaded-cell fields exist, no world-partitioning/streaming system exists.
- [~] AnimationPlayer — clip/speed/loop fields exist, nothing decodes a clip or evaluates a pose (see `ROADMAP.md`'s Animation section for the real gap: no skeleton/skinning/clip-sampling pipeline exists).
- [~] AnimationTree — not even creatable yet in the editor (registry marks it `libraryCreatable = false`); depends on the same missing animation pipeline as AnimationPlayer.

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

## 2D Nodes

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

## CanvasLayer / UI Nodes

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

## AI Nodes / Components

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

1. Finish separating every 3D node's data definition into its own real file (in progress — see
   `Engine/Runtime/Nodes/3D`), so future systems have a clean, discoverable home to attach real
   behavior to.
2. Wire up the highest-value already-authored 3D stubs first: RayCast3D (query system already
   exists, just needs connecting), Skeleton3D + AnimationPlayer + AnimationTree (blocked on the
   same missing skinning/clip-sampling pipeline — see `ROADMAP.md`), Hitbox3D/Hurtbox3D (needed for
   any combat gameplay), NavigationRegion3D/NavigationAgent3D (needed for any AI movement).
3. Only after 3D nodes are in good shape, start a real 2D pipeline (rendering + physics + nav) —
   right now 2D is 100% unstarted, not partially built.
4. Promote Canvas/UI Text/UI Image/UI Button into the Scene node registry so 2D/UI authoring has
   one consistent "Add Node" entry point instead of being Inspector-only.
5. AI nodes come last — they need real navigation (item 2/3) and real gameplay systems to act on
   before a behavior tree/blackboard has anything meaningful to drive.
