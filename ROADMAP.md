# Engine Roadmap — Path to a Full, Modern, Production-Grade Game Engine

This document is the single source of truth for everything still needed to take this
codebase from where it is today to a complete, professional, AAA-capable real-time 3D
engine and editor — the kind of feature completeness found in the current generation of
commercial game engines. It exists so that nothing gets forgotten across sessions, teams,
or years of work: every real gap is written down, every completed system is checked off,
and the scope of "done" for this project is defined here, not in anyone's memory.

No third-party engine or product name is used anywhere in this document, by policy. Where
a section says "match current-generation quality," it means: benchmarked against the best
publicly shipping real-time engines as of today, without naming any of them.

This document works at the **system** level. The layer underneath it — the math
primitives, containers, allocators, handles, resources and components every system here
is built from — is catalogued in `FOUNDATION.md`, which follows the same status legend.

The **settings** surfaces every system here eventually needs to expose — project settings,
editor preferences, the colour picker, material and import options — are catalogued
separately in `SETTINGS_ROADMAP.md`, which follows the same status legend.

## How to read this document

- `[ ]` = not started, or only a stub/placeholder exists.
- `[x]` = **verified** — real and working in the current codebase, confirmed by reading the
  actual source (not assumed), and for anything with meaningful logic, backed by dedicated
  tests that lock more than one case. A system existing is not the same claim as a system
  being verified correct; this mark is reserved for the second one.
- `[/]` = **wired, not fully verified** — a real system runs it, confirmed by reading the
  actual implementation, but it lacks the dedicated test coverage (or end-to-end integration
  check) needed to call it verified. This sits between "foundation only" and "done"; treat it
  as "probably works, hasn't earned the checkmark yet."
- `[~]` = partially implemented — the foundation exists but the feature is not complete
  or not production-ready yet. The note after the item says what's missing.
- Items are grouped by engine subsystem, and within each subsystem roughly from
  foundational (must exist before anything downstream can) to advanced/polish. Treat the
  ordering as a dependency hint, not a strict schedule.
- This is intentionally organized by *system*, not by literal future file path. A
  file-by-file plan for work that hasn't been designed yet would be fiction; each checked
  item below should be broken into its own concrete file/module plan at the point someone
  actually starts it, following the same survey-first discipline used for every item
  already shipped.
- Update this file as part of the same change that finishes an item. A roadmap that drifts
  from reality is worse than no roadmap.

---

## 1. Rendering & Graphics

### 1.1 Core forward pipeline
- [x] Real-time forward 3D rendering with a working render graph, render queue, and
  per-frame command buffer submission
- [x] Camera system with perspective/orthographic projection
- [x] Point/directional/spot light system
- [x] One PBR-capable lit shader path with real-time shadow mapping (single shadow map,
  not cascaded)
- [x] Bloom (bright-pass + blur) and SSAO post-process passes, tonemapping
- [x] Basic unlit/textured 2D and 3D shaders, a fullscreen-quad/copy utility pass
- [x] A screen-space UI overlay draw path (quads + a baked font atlas)
- [x] Cascaded shadow maps for directional lights (3-cascade directional shadow mapping with PCF and distance fade)
- [x] Viewport shading modes (Lit, Unshaded, Wireframe, Lighting Only, Normals) wired directly from editor overlay pill to Renderer3DUVE and shaders
- [x] Low-end device scalability presets (Low, Medium, High) with adaptive shadow resolution, PCF kernel, and SSAO/bloom LOD
- [x] Dedicated 2D Viewport scene creation in Content Browser (+ Add Viewport2D) with flat orthographic navigation, separating 2D and 3D scenes cleanly
- [ ] Shadow maps for point/spot lights (cube-map and perspective shadow variants)
- [ ] Contact shadows / screen-space shadow refinement
- [x] A true physically-based material model — verified rather than assumed: lit_shadowed_3d.glsl
  already implements real Cook-Torrance (GGX distribution, Smith geometry, Schlick Fresnel), the
  correct (1-F)(1-metallic) energy split, mix(0.04, albedo, metallic) base reflectance, and
  tangent-space normal mapping with Gram-Schmidt re-orthogonalization and handedness. What was
  missing was the AMBIENT half: ambient was purely diffuse, so a metal - whose diffuse response is
  zero by definition - rendered BLACK wherever no direct light reached it. That is the single most
  visible way a correct BRDF still looks wrong. Ambient now carries the same diffuse/specular
  split as the direct term, with roughness-aware Fresnel so rough surfaces do not gain a bright
  grazing rim, and AO applied to both halves. This is not image-based lighting - there is no
  environment probe yet, so the ambient colour stands in for average environment radiance - but
  the energy split is now right, and a real IBL probe later replaces the source of that radiance
  without changing the structure. Spot lights also gained a smooth cone falloff; the hard binary
  cutoff produced an aliased cone edge that no MSAA could fix, because the edge was in the shading
  rather than the geometry.
- [ ] Deferred or forward+/clustered lighting path for scenes with many dynamic lights
- [ ] Screen-space reflections
- [ ] Real-time reflection probes (baked and/or dynamically updated cubemaps)
- [ ] Global illumination (baked lightmaps at minimum; a real-time or hybrid GI solution
  as a long-term goal)
- [ ] Volumetric fog / volumetric lighting
- [ ] Temporal anti-aliasing (and/or a modern upscaling technique)
- [ ] HDR display output and a real color-grading / LUT pipeline
- [ ] Order-independent or improved transparency sorting
- [x] Decal rendering — the `decal` scene-node kind is real end to end: the lifetime runtime counts
  down and reports the expiry, the projected-geometry pass clips the receiving surfaces against the
  box or cylinder volume, and the built-in `decal.glsl` program paints the surviving patches with the
  material's albedo texture and colour (tinted by `modulate`, plus emissive scaled by
  `emissionEnergy`, with the texture's alpha joining `albedoMix`) evaluating the authored fades per
  pixel, back to front
- [ ] Ray-traced reflections/shadows/GI as an optional high-end path (long-term)

### 1.2 Scene scale & performance
- [ ] GPU instancing for repeated meshes — the CPU half and the shader half have landed:
  BuildRenderBatchesUVE groups a sorted queue's ADJACENT mesh+material runs into instanced
  batches (never reordering, because the queue is already depth-sorted front-to-back for early-z
  and back-to-front for correct alpha, and regrouping to chase a lower batch count would silently
  undo both), and lit_shadowed_3d.glsl gained a UVE_INSTANCED variant reading per-instance model
  and normal matrices from storage buffers indexed by uInstanceBaseIndex + gl_InstanceID. One file
  behind a define rather than two shaders, so the ~240 lines of lighting cannot drift between an
  instanced object and a non-instanced one. Renderer3DUVE now records those draws: it batches each
  bucket, uploads the frame's model and inverse-transpose matrices once, and issues one
  DrawIndexedUVE per batch with a real instanceCount. Instancing is OPT-IN PER MATERIAL and
  DETECTED from the material's own vertex source rather than declared by a flag - a flag could
  claim support the shader does not implement, and that lie fails silently by stacking every
  instance on the first one's transform. A material without the contract falls back per BATCH, so
  a scene mixing new and legacy materials still instances what it can. The SHADOW cascades are now
  instanced too, which is where the draw calls actually were: the shadow pass runs once per
  cascade, so an uninstanced 200-object scene issued 600 shadow draws against 200 main-pass ones.
  Shadow batching groups by MESH ALONE - a depth-only pass binds no material, so same-mesh objects
  write identical depth however differently they are painted - which makes it batch strictly
  better than the main pass on the same queue. Remaining: pointing the instanceCount at CS8's
  GPU-written draw command instead of a CPU-known batch size, which is the last step to giving the
  indirect cull a consumer.
- [ ] Frustum culling at scale — the per-frame redundancy is gone: extraction is split into a
  frustum-INDEPENDENT build (asset resolution, transform compose, world-bounds transform) and a
  cheap per-frustum cull, so a frame that culls against four frusta (three shadow cascades plus the
  main view) does the expensive half ONCE instead of four times. Previously all four passes
  recomputed identical matrices and bounds to reach four different plane tests. This is also the
  hook occlusion culling and LOD both need - each wants world bounds detached from any particular
  frustum, and while bounds were computed inside the frustum test there was nowhere to attach.
  The build no longer recomputes what did not change: placements are cached per entity and reused
  when the world transform, mesh guid and local bounds are all bit-identical to last frame's.
  Measured on this engine's own maths, a cache hit is ~29x cheaper than recomputing, and placement
  dominates the frame - so this beats accelerating the cull, which was already the smaller half.
  The walk itself also stopped being wasteful: ForEachErased used to heap-allocate a
  std::vector<void*> and hash-look-up every requested component column ONCE PER ROW, even though a
  chunk's column layout is fixed for its lifetime. Columns are now resolved once per chunk into a
  reused buffer - 535us to 31us per 10000-entity walk, about 17x, and every one of the 22
  ForEachUVE call sites across 13 systems benefits, not just rendering.
  Remaining: spatial acceleration so the walk stops VISITING every entity at all. Note that a BVH
  would only speed up the cull, which measurement puts at roughly a third of the extraction cost,
  so the honest next win is skipping entities entirely rather than culling them faster.
- [ ] Occlusion culling (the `occluder` scene-node kind exists as a descriptor only)
- [x] Level-of-detail switching (LODGroup3D): the chain resolves from the camera distance with a
  hysteresis band (entered past a threshold, left under it, the previous level kept between), the
  renderer culls past the last threshold, and each level draws its own mesh — `lodMeshGuids[level]`,
  falling back to the object's own `MeshComponentUVE` mesh for a level that overrides nothing. The
  whole chain is authored in the Inspector, which also shows the resolved level and the cull verdict
  during Play.
- [ ] A world-partition / large-world streaming system (the scene-node kind exists as a
  descriptor only; no streaming, no grid/cell system, no origin rebasing for large worlds)
- [ ] A terrain system (heightfield or mesh-based, sculpting, texture splatting, LOD)
- [ ] A foliage/vegetation instancing and wind-animation system
- [ ] A water rendering system (at minimum a stylized/simple ocean or lake shader with
  reflection + refraction)

### 1.3 Renderer-adjacent modules that are currently empty placeholders
- [ ] `Renderer` module (currently a one-line placeholder folder) — decide whether its
  scope absorbs/renames the existing `RHI/RenderSystems` split or is genuinely a new layer,
  then design it deliberately rather than leaving two same-purpose folders

---

## 2. Graphics Backend & Platform Abstraction

- [x] A render-hardware-interface abstraction with a real backend (OpenGL) and a null
  backend for headless/testing use
- [x] A modern explicit graphics API backend (the kind that supports multi-threaded command
  recording, explicit memory/barrier management) as a second real backend, so the RHI
  abstraction is proven against more than one implementation — **delivered in slices:** the
  Vulkan backend reached slice M4 2026-09-18: real buffers (host-visible policy), SPIR-V shader
  modules, fixed-function pipelines, recorded command buffers, Draw/DrawIndexed replay,
  SPIRV-Reflect-driven uniforms (set-0 UBO blocks over one shared 1 MiB/frame dynamic-offset
  ring plus push constants, SetUniform* by reflected member name), a real depth attachment
  honoring depthTest/depthWrite, and TEXTURES: DEVICE_LOCAL images uploaded through
  HOST_VISIBLE staging buffers (one-shot transfer submissions with full layout transitions),
  GL-mirrored fixed samplers (linear/clamp-to-edge), combined-image-sampler reflection, and a
  per-(pipeline × bound-texture tuple) descriptor-set cache with destruction-time
  invalidation plus a 1×1 white fallback for unbound slots — and OFFSCREEN RENDER TARGETS:
  on 1.3-capable devices the whole frame path switches to core dynamic rendering (probed
  feature-gated; classic devices keep byte-identical M2c behavior), pipelines declare their
  attachment contract via VkPipelineRenderingCreateInfo, passes open lazily with
  vkCmdBeginRendering, textures allocate in the swapchain's own 4×8 format with an
  unorm-sibling sampling view + image-native attachment view so every RGBA8 texture is a
  legal target, re-opened swapchain instances resume with LOAD (GL's FBO semantics), and
  color-only passes borrow a per-extent scratch depth image — and DEPTH-TEXTURE SAMPLING +
  REAL LOAD-OP POLICIES (M2e, 2026-09-18): Depth32Float textures rest in
  SHADER_READ_ONLY_OPTIMAL and bind real sampled views on the dynamic arm (the bit-15
  1×1-white fallback survives only on classic pre-1.3 devices, where no offscreen pass can
  ever produce depth content), offscreen passes honor caller colorLoadOp/depthLoadOp for
  real (Clear/Load/DontCare → VkAttachmentLoadOp over content-preserving tracked-layout
  entry barriers — GL's FBO clear-once-vs-accumulate semantics; the bit-6 warn is now
  swapchain-default-pass only), and a new bit-16 feedback guard deterministically samples
  the 1×1-white fallback (warn-once) when a draw binds the open pass's own attachment —
  and SSBOs + SEPARATE SAMPLERS (M2f, 2026-09-18): BufferUsageUVE::Storage buffers
  (STORAGE_BUFFER_BIT on Vulkan, whole-buffer glBindBufferBase on desktop GL 4.3+, recorded
  faithfully by Null) bind through the new BindStorageBufferUVE command, SPIR-V reflection
  now understands STORAGE_BUFFER slots and split SAMPLED_IMAGE+SAMPLER pairs alongside
  combined samplers (the split form samples through the device's one fixed linear/clamp
  sampler, so checker pixels come back byte-identical to the M2c combined case), unbound /
  wrong-usage / destroyed-after-bind storage slots deterministically resolve to a
  device-owned zero-filled fallback SSBO — the buffer analogue of the 1×1-white texture —
  with warn-once replay bits 17/18 and DestroyBufferUVE invalidating every cached
  descriptor set that references the freed buffer; storage images still fail loudly at
  pipeline creation naming the compute milestone (ComputeSystemUVE, Part 7.2), and the
  honest name rebadges to "Vulkan (M2f SSBO+separate samplers)" on dynamic devices (classic
  stays M2c — the descriptor work is arm-independent) —
  and DEVICE-LOCAL STAGING (M3, 2026-09-18): vertex/index buffers now allocate DEVICE_LOCAL
  memory with TRANSFER_DST (the performance shape M2a's host-visible policy explicitly
  deferred), fed at creation and on every UpdateBufferUVE by one-shot staging copies that
  mirror the M2c texture-upload discipline exactly — transient HOST_VISIBLE|COHERENT
  TRANSFER_SRC buffer, vkCmdCopyBuffer in a one-time command buffer with buffer barriers
  around the copy (TRANSFER_WRITE published to VERTEX_ATTRIBUTE_READ / INDEX_READ), and a
  full-idle wait before teardown — while uniform buffers (host writes ARE the SetUniform
  ring mechanism) and storage buffers (the M2f zero-fallback contract stays simple) remain
  host-visible; the caller-visible copy-on-update contract is identical for every usage,
  only placement/traffic differs, and the honest name rebadges to "Vulkan (M3 device-local
  staging)" on dynamic devices (classic stays M2c — the memory policy is arm-independent) —
  and MULTI-THREADED COMMAND RECORDING (M4, 2026-09-18): the capability this roadmap entry
  itself names is now real and contract-documented — VulkanCommandBufferUVE objects carry no
  device state (recording appends to per-object retained lists), so any number of threads may
  create, record, and submit their own command buffers concurrently; SubmitUVE pushes into a
  mutex-guarded submission FIFO from any thread, and PresentUVE drains that FIFO into a local
  snapshot under the same lock before replaying strictly main-thread (one GPU-timeline owner;
  a submit racing a present lands in the next frame's FIFO). The Null backend mirrors the
  contract for its spy; GL stays inherently context-thread (it executes at record time) —
  and COMPUTE DISPATCH & STORAGE IMAGES (M5a + M5b, 2026-09-18): the RHI-level compute
  capability is complete — CreateComputePipelineUVE (ComputePipelineDescUVE, same pipeline-handle
  domain, vkCreateComputePipelines on Vulkan, a linked GL_COMPUTE_SHADER program on desktop GL
  4.3+, faithful Null bookkeeping with a Compute-stage check) plus DispatchUVE recorded
  OUTSIDE render-pass markers (Vulkan forbids compute inside a rendering instance): the
  Vulkan replay closes any lazily-open dynamic-rendering pass, brackets the dispatch in
  conservative global memory barriers (prior shader writes visible to compute, compute
  writes visible to all later shader readers and COLOR_ATTACHMENT_OUTPUT), flushes
  descriptors at the COMPUTE bind point, and dispatches — while the classic pre-1.3 arm
  warns once (bit 19) and skips; the bind/uniform/storage gates relaxed accordingly on all
  backends. Compute reflection accepts uniform blocks (ring-dynamic), STORAGE_BUFFER slots
  (the M2f SSBO machinery feeds compute unchanged), and — since M5b — STORAGE_IMAGE
  descriptors (the M2f refusal is lifted): storage images join the ONE texture-slot space
  fed by BindTextureUVE (ascending binding index across the entire texture family). Vulkan
  permanently transitions storage-image textures to VK_IMAGE_LAYOUT_GENERAL at first use
  (the barrier closes/reopens an open pass with LOAD semantics; classic arm warns once bit
  22 and skips) and sampled descriptors of pinned textures rewrite with GENERAL layout;
  depth textures in storage slots deterministically fall back to a 1x1 black sink (bit 21);
  GL binds GL_IMAGE_2D uniforms through glBindImageTexture(slot, ..., GL_READ_WRITE). A
  latent M2c-era flush bug died on the way: the "no shader-bound state" early-return ignored
  storage/sampler-only pipelines, so their descriptor sets were never bound (invisible until
  a storage-only compute pipeline made it fatal). GL executes glDispatchCompute at record
  time + glMemoryBarrier(GL_ALL_BARRIER_BITS); the honest name rebadges to
  "Vulkan (M5b storage images)" on dynamic devices — all verified pixel-wise locally
  (SwiftShader: triangle, depth-overlap, checker-quad, and offscreen render-to-texture
  interleave screenshots) with the same scenes plus the four M2e, six M2f, two M3, one M4,
  four M5a, and three M5b proof in tier-2 CI tests (lavapipe), where the sampled-depth
  reconstruction byte check accepts both honest software-stack dualities: an SRGB-typed
  swapchain stores the shader's linear 0.25 as ≈137 while a UNORM-typed one stores ≈64 (both
  correct encodings of the same sampled depth), and the depth-read swizzle alpha is
  spec-undefined on pre-maintenance5 devices (255 or 0 both pass — the specified .g/.b
  zeros and the .r reconstruction carry the proof). Latent M1 readback-fence hazard also
  fixed (the readback submission rides its own transient fence now). Both capabilities this
  entry names — multi-threaded command recording (M4) and explicit memory/barrier management
  (the M2c staging discipline, M2e tracked-layout barriers, M3 device-local placement, M5b
  image barriers) — are now real and pixel-proven; the remaining known gaps are tracked as
  their own entries below: the engine-level ComputeSystemUVE layer (Part 7.2) and shader
  cross-compilation tooling
- [ ] A backend for each target OS's native graphics API where OpenGL is not the best
  choice on that platform
- [ ] Shader cross-compilation so one shader source authors once and targets every backend
  (currently shaders are authored directly in one shading language for one backend)
- [ ] GPU compute-shader support (for culling, particle simulation, skinning, etc. on the
  GPU instead of the CPU) — RHI level completed with M5a (compute pipelines, DispatchUVE,
  SSBO write path) and M5b (STORAGE_IMAGE descriptors, GENERAL transitions + image barriers,
  unified texture-slot space, pixel-proven on lavapipe and GL); the engine-level
  ComputeSystemUVE consumer layer (Part 7.2) has landed as its own system — compute-program
  lifecycle over any injected IRenderDeviceUVE, a validated dispatch queue recorded outside
  pass markers in enqueue order, diagnostics, Null-spy plus real-GL byte-verified proofs —
  and is wired into the frame loop: EngineCoreUVE owns it as its fortieth service and drains
  the queue as Render()'s first statement, into its own command buffer submitted before any
  render pass opens (an empty queue submits nothing). CS3 added the capability that makes GPU
  compute RESULTS usable rather than merely dispatched: IRenderDeviceUVE::ReadbackBufferUVE
  (the read direction of UpdateBufferUVE) on all three backends — Vulkan reads host-visible
  Uniform/Storage memory after a queue drain and refuses device-local vertex/index buffers
  loudly, GL barriers then reads through glGetBufferSubData, and the Null backend now models
  real buffer CONTENTS so headless assertions are honest; a compute-written palette is read
  back as exact floats on lavapipe, and one GL test proves an engine-queued dispatch end to
  end through engine APIs alone. CS4 is the first real GPU WORKLOAD on that foundation:
  ParticleComputeSimulationUVE runs Scene::ParticleRuntimeUVE's per-particle integration in a
  compute kernel (built-in shaders/particle_simulate.glsl) and is held to the strictest
  standard available - its result must equal the CPU runtime's bit for bit, float for float,
  including lifetime culling and compaction, proven on a real GL context over 1000 particles
  and sixty compounding steps (the kernel forbids fused multiply-add so that equality is real
  rather than approximate). The CPU keeps authority over WHICH particles exist; emission,
  budgets and compaction stay where they are bounded and tested. Remaining: a fully resident
  simulation with no CPU round trip (needs GPU-side emission/compaction). CS5 added the
  second workload, frustum culling: FrustumCullComputeUVE runs Math::FrustumUVE::IntersectsUVE
  over many boxes at once (built-in shaders/frustum_cull.glsl) and is held to the same standard
  - the GPU's visibility must equal the CPU test's for every box, including boxes placed to
  touch a plane exactly and nudged one ULP either way, which is where a contracted multiply-add
  would flip a decision and make an object pop in or out depending on which path ran. Plane
  extraction stays on the CPU (six planes is not worth a dispatch, and one authority is easier
  to keep correct), and non-finite input is refused rather than answered differently, since the
  CPU test absorbs it through a double-precision fallback a float shader cannot reproduce.
  CS6 then closed a gap the first two workloads had hidden: their kernels existed only as GLSL,
  so on Vulkan - which takes SPIR-V, runtime translation still being an open item below - they
  compiled nothing and refused to initialize, making two "engine systems" quietly GL-only.
  Both kernels now pass their parameters in std430 storage blocks instead of bare `uniform`
  scalars (SPIR-V has no non-opaque global uniforms; glslang rejects the uniform form outright),
  are baked to SPIR-V beside their GLSL, and are selected per backend - with both workloads now
  proven against a real headless Vulkan device at the same bit-for-bit standard they meet on GL.
  CS7 then added the missing draw path itself: `ICommandBufferUVE::DrawIndexedIndirectUVE`,
  fed by a new `BufferUsageUVE::IndirectStorage` that is deliberately BOTH an indirect buffer
  and an SSBO - an indirect buffer the compute stage cannot write would serve nothing the CPU
  could not already do with `DrawIndexedUVE`. Implemented on all four backends against a shared
  `DrawIndexedIndirectCommandUVE` mirror of the five-word GPU parameter block.
  CS8 then built the pass CS7 existed for: FrustumCullIndirectUVE runs the same frustum test as
  CS5 but writes its answer as an indirect draw's instanceCount plus a compacted list of
  surviving indices, both in device memory. The CPU is never told how many objects survived -
  the diagnostics deliberately expose no visible count, because the only way to fill one would
  be the readback the pass exists to remove.
  CS9 then unblocked skinning by building what was missing: MeshAssetUVE now carries per-vertex
  joint influences and a skeleton, and TrySkinMeshUVE is the CPU linear-blend implementation a
  GPU kernel can be verified against - the baseline whose absence was the actual blocker.
  The skinning section is an OPTIONAL trailing part of the .uvmodel payload, so every existing
  static mesh serializes to byte-identical output (the envelope's version field is global across
  all asset kinds, so bumping it would have invalidated scenes and textures to describe a mesh
  feature).
  CS10 then shipped the kernel: MeshSkinComputeUVE produces exactly what TrySkinMeshUVE produces,
  bit for bit, on both backends. Reaching that standard required a real change to CS9 - the CPU
  path now accumulates in float rather than through Math::TransformPointUVE's double, because
  GLSL has no portable float64 and a double CPU path would have left a permanent ~1 ULP
  disagreement on roughly one vertex in six, forcing every skinning test onto a tolerance.
  Pose resolution stays on the CPU: walking a parent chain is serial work a dispatch cannot help.
- [ ] Bindless/descriptor-indexing-style resource binding for reduced per-draw overhead

---

## 3. Physics & Collision

- [x] Rigid-body dynamics with angular dynamics, a real narrow-phase collision system, and
  a broad-phase AABB cache
- [x] Raycasts, shape casts, and a general physics query system
- [x] Multi-object raycast exclusions (RayCast3D's `exclusions`): a ray refuses a set of authored
  objects on top of itself, holds them as real entity references that survive a save/load rather
  than as runtime handles, and checks them before the layer mask — an exclusion is not a mask, so
  no layer can bring an excluded object back
- [x] Projectile hit resolution (Projectile3D): each fixed step sweeps the projectile's authored
  `radius` against the layers its `collisionMask` accepts and resolves the contact through an
  authored motion policy — Stop halts it at the contact, Bounce reflects it through
  `restitution`/`friction` — writing the last contact into runtime hit fields and queueing a typed
  `Projectile3DHitEventUVE`; the engine owns the motion, gameplay owns what a hit means
- [x] Hitbox/hurtbox strike consequences (Hitbox3D/Hurtbox3D): the pairing the engine already
  resolved every frame is diffed into real edges — one typed `Hitbox3DStrikeEnteredEventUVE` when a
  hitbox starts striking a hurtbox and one `Hitbox3DStrikeExitedEventUVE` when it stops, whether by
  separation or by an authored gate (a disabled box, a layer/mask or channel change, a destroyed
  entity); each event carries the pair, the penetration depth, the minimum-translation axis and the
  damage channel, and damage/knockback/i-frames stay gameplay's
- [x] Trigger volumes with enter/exit lifecycle tracking
- [x] A constraint system with hinge motors and limits
- [x] A kinematic character controller with slide/step-up sweep behavior, exposed as a
  real, addable component with gravity/jump/ground-state handling
- [x] Kinematic bodies driven by an authored target velocity — moving platforms, lifts and doors
  that go through the world instead of around it: geometry stops them, they cannot tunnel through a
  thin wall at speed, they push the rigid bodies they meet with the character's own push policy, and
  the velocity they actually achieved is what carries a character standing on them
- [x] Configurable per-surface physics materials (friction/restitution)
- [x] A third-person camera boom (SpringArm3D) that casts along its own axis every fixed step,
  snaps in behind geometry so a camera never clips through a wall, springs back out at an authored
  rate once the way is clear, and carries its children by the change in length so authored poses
  round-trip without drift
- [ ] Soft-body / cloth simulation
- [ ] Vehicle physics (wheeled at minimum)
- [ ] Ragdoll physics (driven skeletal bodies + constraints layered over an animated
  skeleton)
- [ ] Destructible/fracturable physics objects
- [ ] Joint types beyond hinge (ball socket, slider/prismatic, fixed, spring/distance)
- [ ] Continuous collision detection for fast-moving small objects (tunneling prevention)
- [ ] Multi-threaded physics stepping for large scenes
- [ ] Deterministic/networked-safe physics stepping (fixed-point or otherwise reproducible)
  for competitive multiplayer use cases

---

## 4. Animation & Characters

The current animation system is intentionally thin — a real gap against a modern engine
and one of the highest-priority areas below.

- [x] Animation clips, an animation state machine, and a blend-tree style animation graph
- [x] A shared time/pose data contract used across the animation stack
- [x] A real skeletal mesh + bone hierarchy + GPU skinning system. Bones are imported from a rigged
  model (glTF/FBX skeleton readers), the clip pipeline poses them every step, the renderer poses a
  skinned mesh against the nearest Skeleton3D above it and re-uploads that entity's own vertex buffer
  every frame (`MeshSkinComputeUVE`), and a BoneAttachment3D object rides a bone through the ordinary
  transform path (`Scene::SyncBoneAttachment3DObjectsUVE()`). Locked by 17 skinning cases, the
  resolver cases, 7 pass tests and the serializer's reference round trip.
- [x] Two-bone inverse kinematics: `TwoBoneIK3D` is a real scene object - an analytic two-circle
  solve (no iteration, so a limb cannot shiver between two nearly-equal answers) over a
  root/middle/end bone chain. The target is another object's world position or a point authored in
  the skeleton's own space; the pole that picks which of the joint's circle of answers is used is
  either another object or a direction in the skeleton's space, and with neither the chain keeps the
  bend the pose already has. Reach is respected rather than exceeded - a target out of reach leaves
  the limb straight, aimed and short, with `reached` false - influence blends the solve over the
  animation through the shared `BoneModifierComponentUVE`, and the resolved bone indices plus
  `solved`/`reached`/`endToTargetDistanceMetres` are declared runtime-only so the Inspector shows
  what a solve did while nothing can save a stale answer.
  `Scene::SyncTwoBoneIK3DObjectsUVE()` runs inside the animation step, after the drivers that pose
  skeletons and before the attachment pass, and is gated on the skeletons those drivers posed this
  pass so a solve is never blended twice. Locked by 9 solver cases (geometry, reach, folding,
  refusals, blending), 8 pass cases on a real entity manager + scene graph (target/pole resolution,
  the gate, influence, refusals, index-beats-name, priority ordering), the serializer's
  three-reference round trip with documents whose ids name nothing, the section's metadata case, and
  an engine-core tick test that ends with an attachment on the wrist the IK moved.
- [ ] Full-body IK, as a stretch goal beyond the two-bone object above
- [x] Root motion extraction and application - `AnimationRootMotionModeUVE`: Off, In Place (the root
  bone's ground travel is taken out of the pose) and Apply To Target (the target is moved by it, as
  velocity when it is a Character3D so collision still applies). Locked by the sequencer's travel
  cases and the engine-core test that runs a character through its skeleton's frame.
- [x] Animation retargeting done properly, as its own scoped system with real bone-mapping
  validation. It landed as its own module, `Engine/Runtime/Retarget` - a humanoid reference, bone-name
  matching, joint checks, A-pose conforming for both the skeleton and the mesh, a playback plan, and
  the editor's Retarget window on top - locked by the plan, conform and playback tests. Hardened with:
  - Multi-track skeletal clip support with per-bone tracks in `.uvanim` and auto-discovery of
    existing project animation clips during retarget planning.
  - Resilient joint-to-bone name matching (handling `mixamorig:`, `Armature|`, case-folding, and
    differing third-party naming conventions without T-pose freezes).
  - Entity Editor live retargeting preview session (fixing previous blackout bug).
  - Both rigs brought to a common A-pose automatically before mapping.
- [ ] Animation compression (both curve compression and a runtime decompression path)
- [x] Additive animation layers (e.g. aim offsets, lean, breathing) on top of a base pose - an
  Additive graph node adds its clip's pose over the base at the parameter's weight, locked by the
  layer cases in the graph suite.
- [x] Blend spaces (1D and 2D) for locomotion blending, distinct from the existing blend
  tree - `AnimationBlendSpace1DWeightsUVE()` / `AnimationBlendSpace2DWeightsUVE()` place the clips
  around the parameter, with synced points and time scaling locked by their own cases.
- [ ] Facial animation / morph targets (blend shapes)
- [ ] Physically-simulated secondary motion (cloth bones, jiggle, spring bones)
- [ ] A dedicated animation-authoring/preview tool in the editor (a timeline/sequencer for
  scrubbing clips and state machines is covered again under Editor Tooling below)

---

## 5. Audio

- [x] A real audio device abstraction with a null backend for headless use
- [x] A production audio output backend (miniaudio) driving real hardware, with automatic
  NullAudioDeviceUVE fallback on machines without a usable output device
- [x] WAV import/decoding, a PCM16 decoder, and an attenuation model
- [x] A mixer-group concept and a basic gain effect
- [x] A source/listener system with orientation validation
- [ ] Additional common audio formats (compressed formats such as an Ogg/Vorbis- or
  MP3-class codec, not just uncompressed WAV)
- [ ] A real DSP effect chain beyond gain (reverb, low-pass/occlusion filtering, EQ,
  compression/limiting)
- [ ] Full 3D spatialization (HRTF-based or at minimum proper distance/cone/doppler
  modeling beyond basic attenuation)
- [ ] Audio occlusion/obstruction driven by the physics/collision system
- [ ] Streaming playback for long audio (music, VO) instead of fully-decoded-in-memory
  playback only
- [ ] A real-time audio mixing graph / bus system with runtime-adjustable submixes
- [ ] An in-editor audio authoring tool (mixer view, real-time meter, attenuation curve
  editor)
- [ ] Ambisonics / spatial audio bed support for VR/360 use cases (long-term)

---

## 6. Networking & Multiplayer

This is close to entirely unbuilt. Two folders exist (one placeholder, one with a single
real utility file) — networked multiplayer is a from-scratch, long-term project.

- [x] A reliable packet window utility (ordering/retransmission bookkeeping primitive)
- [x] A transport seam plus an in-memory loopback pair (endpoint contract + bounded FIFO queues; proves the interface the UDP transport implements)
- [x] A real transport layer (UDP-based: connected non-blocking datagram sockets behind the endpoint seam, POSIX + Winsock, dotted quads plus getaddrinfo hostnames)
- [x] UDP listener: one socket serving many clients (recvfrom demux by sender address, per-peer endpoint handles with sendto routing, bounded stranger backlog with accept-replay so Hellos survive the gap, exact peer queue counts)
- [x] A client-server session/connection lifecycle (connect, handshake, disconnect,
  timeout, reconnection)
- [x] Session core over the seam: Hello/Welcome handshake, version-checked (v2 Hello carries a session token), best-effort Bye disconnect, silence timeout on an explicit clock, client redial with bounded attempts, server resume recognition
- [x] Channel multiplexing: one session carries many features on tagged channels (opt-in per session with a single untagged handshake, per-channel queues, replication/RPC/prediction ride shared sessions; mode and channel ids agreed out of band)
- [x] Server relay: broadcast fan-out over N server sessions (opaque byte forwarding, channel-blind, no echo, established-only delivery, explicit join/leave; script surface relay.peer_count/set_room/room/set_interest/interested/remove/stat_*/peer_state plus relay.send/poll, all synchronous with no events)
- [x] Matchmaking lobby: room registry over direct RPC sessions (create/list/join/leave/roster over RPC, one room per client with auto-leave on move, unique names per room, empty rooms removed, roster-change push to survivors; typed client facade with room/roster cache and stale-push guards; script surface lobby.list/create/join/leave/roster/result_* plus lobby_result/lobby_roster)
- [x] Relay rooms: routed fan-out by peer room tags (implicit rooms with no registry, the default room is exactly broadcast, room-skips silent like unestablished-skips; lobby rooms bind verbatim via a ClientRoomUVE sync loop)
- [x] Relay interest: pairwise mute matrix over fan-out (default interested, one-way mutes, self-mutes stored but never consulted, removals prune both directions; composes with rooms; app syncs teams, proximity, or subscriptions)
- [ ] Entity/state replication (which properties replicate, at what rate, to which clients)
- [x] Replication core: fixed-rate position snapshots over sessions (net-id registry with sampler/applier callbacks, strict little-endian framing, per-entity authority roles, per-entity relevancy filters, delta compression between fulls, non-position property frames with a shared wire-value codec)
- [x] Ownership-transfer handshake: request/accept/deny control frames move authority over the wire (single-flight per entity, timeouts with retry, stale-answer guards, manual rebind still overrides)
- [x] Replication deltas: changed-only sends between scheduled full snapshots (epsilon change detection, first-sighting ships, baselines survive authority flips, drops heal at the next full; period 1 keeps legacy fulls)
- [x] Replication Gameplay adapter: sampler/applier wiring to the scene (world-read samplers with last-known hold, scene-graph appliers, net-id map with register/unregister/find/prune; predicted pawns ride as Owner)
- [ ] A remote-procedure-call framework callable from the scripting/gameplay layer
- [x] RPC core: async calls over an owned session (method registry, per-call timeouts, at-most-once, 64-deep retained results, strict magic+version framing, ValueUVE args/results incl. collections; script surface rpc.begin/push/send/result_* plus rpc_response; script-registered methods, sync calls still open)
- [x] Client-side prediction and server reconciliation for responsive movement
- [x] Prediction core: per-tick input capture with immediate local stepping, triple-repeat input stream, ordered server apply with per-tick states, epsilon reconcile (snap + replay), gap-timeout skip, offline prediction across reconnects (u64 ticks per pawn, opaque app determinism word; multi-pawn streams with seeded spawns, per-input dt with stored-dt replay, server relay fan-out with pawn-tracking observers)
- [x] Prediction Gameplay adapter: component <-> PredictedStateUVE (packed clocks, real-intent stepper, tick/sync pawn drivers with pawn overloads; local mover refuses tagged bodies)
- [x] Dedicated-server sample: headless server + 3 bot clients over loopback (lobby rooms, relay shouts bound to rooms, predicted pawns as replication Owners, server-owned patrol NPC, untagged local walker; quiet relay rooms need longer session timeouts than the 5 s default)
- [x] Lag compensation for hit registration (shot-intent protocol transports the view tick plus the fire fraction; one-call FireLagShotUVE client firing with outcome receipts)
- [x] Lag compensation core: per-tick pose history for combatants (64-tick window), rewound overlap + ray resolves through the live narrow-phase with live acceptance gates, merged-report damage via the lifecycle tracker (idempotent replays, no spurious exits)
- [x] Lag-comp Gameplay wiring: shooter view ticks sourced from prediction acks, invalid-tick fail-closed shot resolve, pre-tracker rewound merge, rewound-ray damage with live-parity health events
- [x] Lag-comp interpolation: fractional-tick rewound resolves (position lerp + rotation slerp between consecutive snapshots; gap/newest/clamped floor fallbacks, fail-closed fractions; fractions ride the shot-intent protocol)
- [x] Lag-comp scale rewind: recorded world scale with rewound half-extents (authored size times absolute scale; zero/non-finite unhittable, mirrors occupy the same box; scale lerps between snapshots)
- [x] Lag-comp engine tick: per-frame auto-record with 1-based engine ticks, submitted-shot inbox merged pre-tracker, Shutdown drain (prediction ticks map app-side)
- [x] Lag-comp server demo: headless server + lagged bot over loopback (prediction-ack views, rewound shot/ray saves, pre-tracker merge, fractional-tick coverage)
- [ ] Interest management / relevancy (only replicate what a client can see or needs)
- [x] Interest core: spatial relevance between viewers and entities (uniform grid, per-viewer radii, hysteresis band, always-relevant flags, fail-open unknowns with counters; feeds the replicator's per-entity relevancy filters; priority tiers with id-staggered send rates, per-entity rate overrides, multi-client fan-out with per-viewer budgets over tier-distance ranking, proven by a fan-out sample: one server, three budgeted bots, divergent views)
- [x] Interest Gameplay adapter: scene <-> interest sync with filter binding (world-pose upserts that never flip always-relevant flags, viewer-follows-pawn moves, idempotent per-viewer binds; short counts surface map/core drift)
- [ ] Voice chat
- [ ] A matchmaking/lobby layer, or at minimum a clean integration point for a third-party
  one
- [x] Network debugging tools (packet inspector, simulated latency/loss for testing)
- [x] Simulated transport conditions: deterministic drop periods + fixed delays per direction over any endpoint
- [x] Packet inspector core: captured session datagrams decoded to structured records plus one-line summaries (hello version/token with mismatch flags, strict replication/RPC/prediction payload mirrors incl. RPC value args, `> [rpc] ...` log lines, malformed flagging, opaque unknown channels)
- [x] Editor capture UI: bounded decode-on-push packet ring (512, drop-oldest, direction/channel/malformed/text filters, hexdump) feeding a Capture bottom-dock tab with filter toolbar, packet list, and selected decode (Clear + stats; app-fed via CapturePacketUVE until live taps land)
- [x] Live stream taps: session-level tap sink copying every sent/received datagram (direction, pump clock, channel byte; ignored garbage included, failed sends excluded) straight into the capture ring
- [x] Capture demo: tap-fed ring over mixed loopback traffic (handshake, replication, RPC, shots, garbage) with host channel mapping, forced drops, filter proofs, and a printed hexdump
- [ ] The now-redundant `Networking` placeholder folder and the real `Network` folder
  should be reconciled into one clearly-named module once real network code exists, instead
  of carrying two same-purpose folders forward

---

## 7. Scripting, Gameplay Framework & AI

### 7.1 Scripting (UVScript)
The node-graph scripting was removed in favour of UVScript (`.uvs`), a text language with one
script per node; see `Engine/Runtime/UVScript/README.md`.
- [x] Lexer and parser (indentation, unit literals, string interpolation), with line/column
  diagnostics and recovery
- [x] Static type checker against the node's host, and a bytecode VM (handlers, functions,
  fields, `wait`, an instruction budget)
- [x] Engine binding: a node's `.uvs` runs `ready`/`tick(dt)` in process order, reloads on save
- [x] Editor: New UVScript on the script slot, a text editor with live diagnostics, `export`
  fields in the Inspector
- [ ] Collections and tuples, calling methods on other nodes, `wait` inside a `fn` — methods
  on other nodes have landed (`node(name)` lookup plus fire-and-forget `ref.method(args)`, reaching
  the target's own script functions first with `hide()`/`show()` built in), and so have collections
  and tuples (typed `list[int]`/`map[str, int]`/`tuple[int, str]`, literals, `[]`, `for x in xs`,
  `let (a, b) = pair()`, `length`/`push`/`keys`/`contains`/`remove` with native-code parity); only
  `wait` inside a `fn` remains
- [ ] More host bindings: collision/overlap events, audio, physics forces, other node kinds —
  physics forces have landed (persistent force/torque plus mass-scaled impulses on rigid
  bodies, with rigid-body velocity readable/writable), as have audio bindings (play/stop
  plus a playing query on audio sources, with volume/pitch readable/writable) and
  collision/overlap events (both parties' scripts hear each enter/exit with the other's
  name); other node kinds have landed too (writable `visible` on every object, `intensity`
  on lights, `fov` on cameras), and gameplay pools and tags (`attributes.has/get/max/damage/heal`
  and `tags.has/add/remove` on entities with the components)
- [ ] An in-editor debugger (breakpoints, stepping, live values)
- [x] C++23 output for release builds: bytecode translated to C++, registered by program fingerprint,
  checked against the interpreter
- [x] Typed C++ output: unboxed locals for functions whose types are known throughout (Debug
  `fib(20)`: 68.8 ms interpreted, 0.62 ms native; Release not measured)

### 7.2 Gameplay framework
- [x] A formal actor/pawn/controller-style gameplay object model above raw ECS entities +
  components (Pawn/Controller components with mutual-or-absent links, Possess/Unpossess stealing
  both sides, input routing to Player pawns, the player-look/interact/character flow resolved
  through possession, character motion steered from pawn input, possessed/unpossessed
  lifecycle events, and follow cameras that track the possessed pawn)
- [x] An input-action-mapping layer (bind a logical action like "Jump" to any physical
  input across keyboard/gamepad, with rebinding support), rather than scripts polling raw
  key codes directly
- [x] A gameplay tag / gameplay-attribute system (health, stamina, status effects) as a
  reusable framework rather than one-off components per game
- [x] A cinematic/sequencer tool for cutscenes (keyframing cameras, animation, audio, and
  gameplay events on a shared timeline)
- [x] A trigger/event layer for level scripting lighter than a full script
  (lightweight "on overlap, do X" level logic)

### 7.3 AI
- [x] Navmesh generation from level geometry (BakeNavmeshUVE bakes NavMeshVolume3D regions to
  rectangles-and-portals meshes, queried through NavmeshUVE)
- [x] A* / pathfinding over the generated navmesh, with agent avoidance (FindNavPathUVE +
  string-pulling; NavAgentUVE steers with separation from neighbours)
- [x] A behavior-tree or utility-AI framework for authoring NPC decision-making (utility-AI: considerations, response curves, hysteresis selection over blackboards)
- [x] A perception system (sight/hearing cones feeding AI decisions: raycast-occluded sight cones over watched tags, hearing radius against decaying noise emitters, sensed into blackboards)
- [ ] Crowd simulation for large numbers of agents (long-term)

---

## 8. Asset Pipeline & Content Management

- [x] A real asset database, file-envelope format, and asset manager with dependency
  tracking
- [x] Format importers: PNG, JPEG, BMP, TGA (raster images), OBJ + material, glTF + mesh
  conversion, WAV (audio)
- [x] Hot-reload, an asset import queue, and project file/change watching
- [x] Real, non-generic content-browser thumbnails for raw source images (not just already
  -imported assets)
- [x] A data-table asset family (structured tabular game data, with its own importer and
  registry)
- [x] A binary shader cache
- [x] A mesh/animation interchange format importer beyond glTF/OBJ (an FBX-class importer,
  with embedded texture extraction, folder searching across textures/, Textures/, and .fbm/, and skeleton/animation
  conversion — verified and production-grade in `ReadFbxSkeletonUVE`, `FbxMeshConverterUVE`, and import helpers).
- [ ] Texture compression (BCn on desktop, ASTC/ETC on mobile) baked at import time, not
  just raw decoded pixels
- [ ] SVG/vector asset support (a real rasterizer — confirmed not to exist anywhere in this
  codebase today, so raw vector source files fall back to a generic icon)
- [ ] An asset "cooking"/bake step that produces a platform-optimized, shippable form of
  content distinct from the editor-time source form
- [ ] Addressable/streamable asset loading (load-by-reference at runtime without every
  asset being a hard, always-loaded dependency)
- [ ] Version-control-friendly asset diffing/merging tools for binary or semi-binary asset
  formats
- [ ] An in-editor material editor / shader graph (author materials visually; currently
  materials are authored as data with no visual node graph)
- [ ] A particle-effect authoring tool (currently particles are a runtime system with no
  dedicated editor UI for authoring effects)

---

## 9. User Interface & UX Runtime

- [x] A real, GPU-rendered screen-space UI runtime: Canvas/Text/Image/Button components,
  authored through the editor's Inspector, hit-tested against real input, rendered both in
  the plain runtime and the live editor's own viewport during play
- [x] A baked bitmap font atlas approach for UI text rendering
- [x] Layout containers (horizontal/vertical stacks, grids, anchors/margins that respond to
  screen-size and aspect-ratio changes) — horizontal/vertical stack containers with
  padding/spacing/alignment run inside UIRuntimeUVE::TickUVE, stacks wrap into
  uniform-cell grids via wrapAfter, normalized anchors with pixel margins resolve
  against parents or the viewport ahead of layout, text paces by measured atlas
  advances, and containers auto-size to content per axis deepest-first
- [ ] Additional widget types: sliders, checkboxes, dropdowns, text input fields, scroll
  views, progress bars, tooltips — sliders, progress bars, checkboxes, hover-delay
  tooltips, and dropdowns have landed with draw batching and layout/anchor
  participation, as have single-line text input (focus, caret, basic editing) and
  scroll views (clipped vertical stacks with wheel scrolling) — the widget box is
  fully checked
- [ ] Rich text (multiple fonts/sizes/styles/colors within one text block, not just one
  baked font per label)
- [x] 9-slice/scalable image borders for resolution-independent UI art — textured images
  slice into up to 9 quads with authored pixel margins and texture-fraction borders,
  hollow-frame and proportional-shrink fallbacks included
- [x] UI animation/tweening (transitions, easing) as a first-class authoring feature —
  UITweenComponentUVE drives rect/alpha with 9 easings, delay, and once/loop/ping-pong
  modes, ticked on the real frame clock after layout so active tweens override the
  resting arrangement; alpha tweens multiply authored alpha, runtime state reseeds
- [ ] Input focus and gamepad/keyboard UI navigation (tab order, D-pad navigation) for
  controller- and accessibility-friendly menus
- [ ] World-space UI (a Canvas rendered as a 3D object in the scene, e.g. floating health
  bars or diegetic screens), distinct from the current screen-space-only Canvas
  implementation
- [ ] Localization support for UI text (string tables, right-to-left text layout, font
  fallback for non-Latin scripts — the current embedded UI font only covers a Latin-1
  subset)
- [ ] Accessibility features (colorblind modes, UI scaling, screen-reader hooks)
- [ ] A dedicated in-editor UI layout tool (visually place and preview UI without running
  the game)

---

## 10. Editor & Tooling

### 10.1 What exists today
- [x] A real, docked-looking ImGui-based editor shell: title bar with a consolidated menu, workspace tabs,
  a real 3D/2D Viewport panel with gizmos, overlay bubbles, orbit/pan/zoom camera control,
  and dedicated Viewport Toolbar controls:
  - 2D / 3D mode segmented toggle
  - Shading Mode selector (Lit, Unlit, Wireframe, Lighting Only, Normals)
  - Physics simulation toggle (live pause/resume with visual indicator)
  - Procedural sky with radiant sun disc and sharpened horizon blend
- [x] A Scene Hierarchy panel with per-category node icons and a centralized node-creation
  registry covering dozens of node kinds, plus real-time search filtering in the Entity Editor
  and main editor hierarchy popups.
- [x] An Inspector panel with per-component drawers, add/remove/undo/redo authoring, and a
  contextual component list (only offers components relevant to what an entity already is)
- [x] A merged Content Browser (file tree + thumbnail grid) with UE5-style rounded card thumbnails,
  dark inset previews, colored category badges (Cyan: Mesh, Orange: Texture, Green: Material,
  Purple: Animation, Gold: Scene), search, and favorites
- [x] Content Browser layout: one toolbar (Add, Import, Save All, back/forward, a path whose
  arrows list the folders inside), a sidebar with Pinned, the project's folder tree (with its own
  folder search) and Shelves (hand-picked groups of files, saved per user), a type filter, a search
  that looks through every folder below, and an item count
- [x] Entity Editor (first part): an entity asset opens in its own OS window (Open Tree or
  double-click) with its node tree, a live view and the Inspector; Save (Ctrl+S), Revert, and the
  window's X (asks when there are unsaved changes). The scene waits untouched meanwhile, the
  simulation is held so nothing moves, and placed copies in the scene follow a saved change
- [ ] Entity Editor: Scripting and Signals tabs, a Compile check with a list of problems, and a
  bottom dock (Content, Timeline, Anim Graph)
- [x] Content Browser modes, each its own way of working: Tiles (one folder by picture),
  Columns (walk down folder levels side by side, with a preview of the picked file), Details
  (a table sorted by name, kind, size, last change or folder), Recent (what changed lately below
  this folder, by day) and Board (everything below laid out in lanes by kind)
- [ ] Content Browser: a dependency view (which asset uses which) once assets record their
  references
- [x] Content Browser: team shelves saved in the project (project.uvshelves, reloaded when it
  changes on disk) beside personal ones, and dragging files and folders onto a shelf
- [x] A Scripting workspace: a UVScript text editor that checks the script against its node as
  you type
- [x] A developer console / bridge for programmatic/scripted control of the editor
- [x] Play-mode simulation with a real game-camera switch and correct state
  snapshot/restore on stop (a real crash in this exact path was found and fixed)
- [ ] A true docking window system (drag a panel anywhere, split/tab it with any other
  panel, save/restore arbitrary layouts) — every panel today is individually
  positioned/sized by formula, not a real dock tree; this is a known, explicitly-scoped-out
  architectural gap
- [ ] Multi-scene editing (open and edit more than one scene/level at once, or reference
  sub-scenes inside a parent scene)
- [ ] A real prefab workflow with instance overrides that visibly diff against the prefab
  source, and "apply to prefab" / "revert instance" actions (partial prefab-maturity/
  revision-policy plumbing already exists; the editor-facing workflow needs verifying and
  filling in)
- [ ] A profiler / frame-debugger panel (CPU and GPU timings per system, a frame capture
  view for the render graph, a memory-usage view; capture panel landed: live snapshots over ProfilerCaptureUVE, Net category, network emitters, fan-out sample profiles itself - GPU timings, render-graph capture, memory view still open)
- [ ] A material editor / shader graph (see also Asset Pipeline above)
- [ ] A terrain-sculpting tool
- [ ] A particle-effect editor
- [ ] An animation timeline/sequencer for previewing and authoring clips, blend spaces, and
  state machine transitions visually
- [ ] A cinematic/sequencer tool (see also Gameplay Framework above) shared with animation
  authoring where it makes sense
- [ ] Source-control integration (status indicators, checkout/add/revert from inside the
  editor) for at least one common version-control system
- [ ] A real log/output console panel distinct from the developer console (build output,
  warnings/errors surfaced with click-to-navigate)
- [ ] Editor extensibility: a real plugin/extension API so third parties can add panels,
  importers, or menu items without editing engine source
- [ ] Live C++ reloading (recompile and hot-swap gameplay code without a full editor
  restart) — a major productivity feature in modern engines, currently fully absent
- [ ] In-editor performance/validation checks that run automatically (e.g. flag missing
  colliders, unused assets, broken references) — some of this exists narrowly (project
  health checks); expand into a general "project validator" panel

### 10.2 Editor-adjacent cleanup
- [ ] Retire or consolidate the older, now-redundant standalone editor scaffold binary that
  predates the real editor, once confirmed nothing still depends on it
- [ ] A dockable "Output Log" and "Search" (find-in-project) panel, standard in every
  mature content-creation editor

---

## 11. Platform Support

- [x] Desktop windowing, input, and OpenGL rendering on the current development platform
- [x] A mobile input/gesture system exists at the input-abstraction level
- [ ] Verified, tested builds on every major desktop operating system (not just the one
  this codebase has been developed on)
- [ ] A real mobile rendering + lifecycle path (app suspend/resume, safe-area handling,
  touch-first UI scaling) beyond the existing input abstraction
- [ ] Console platform support (each console's own certification requirements, controller
  input, and storage/save APIs) — a long-term goal gated behind actual console dev kit
  access
- [ ] VR/AR support (stereo rendering, tracked controllers, room-scale/seated play areas)
- [ ] Web/browser export (a build target that runs in-browser)

---

## 12. Packaging, Distribution & Live Operations

- [x] A minimal packaging pipeline: bundle the runtime binary, a project manifest, and
  content into one distributable, runnable folder
- [x] A save-game system with checkpointing, versioned save-payload migration, and
  compression
- [ ] Platform-store-ready packaging (installers/app bundles per OS, code signing)
- [ ] Build variants (debug/development/shipping) with shipping builds stripping
  debug-only code paths and assets
- [ ] Asset cooking integrated into packaging (see Asset Pipeline) so shipped builds don't
  carry editor-only source formats
- [ ] Crash reporting and basic telemetry/analytics hooks
- [ ] An in-game patch/content-update mechanism (downloadable content, hotfixes) for
  live games
- [ ] Platform achievement/storefront SDK integration points (left generic/pluggable
  rather than tied to one storefront)
- [ ] Automated build pipelines (continuous integration building every target platform on
  every change) — this codebase currently validates locally per change, with no CI wired
  up yet

---

## 13. Engineering Quality, Performance & Documentation

- [x] A real, fast, comprehensive automated test suite (thousands of tests) covering
  nearly every module, run before every merge
- [x] Consistent compiler-warning discipline (a full, clean, warnings-as-errors build
  across the whole codebase)
- [x] A documented module-boundary discipline (public/private split per module, plugin
  functionality kept out of core runtime) that has been enforced and corrected multiple
  times already
- [ ] Continuous integration running the full test suite automatically on every change,
  not just locally
- [ ] Automated static analysis / sanitizer runs (address/undefined-behavior sanitizers,
  a linter pass) as part of the standard validation gate
- [ ] Formal performance budgets and automated performance-regression testing (frame time,
  memory, load time tracked over time, not just correctness)
- [ ] A public-facing API reference generated from source comments
- [ ] Getting-started and system-by-system guides for new contributors, generated/kept
  alongside the code they document rather than living only in chat history
- [ ] Sample/template projects demonstrating each major system (a "third-person
  character" sample, a "multiplayer" sample once networking exists - done: Samples/DedicatedServer,
  a "UI" sample, etc.)
- [ ] A public contribution guide with coding standards, PR process, and issue templates

---

## Version 0.2 Roadmap — Core Milestones

With Version 0.1 foundations, core 3D scene objects, skeletal skinning, FBX importing, resilient retargeting,
and modern editor tooling fully delivered and tested, Version 0.2 shifts focus toward end-to-end production
capabilities:

1. **Dedicated 2D Engine Pipeline & Viewport (Section 1.1 & 2D Objects)**
   - Dedicated 2D orthographic canvas and camera controls inside the Viewport.
   - Tilemap authoring and rendering (`TileMapLayerComponentUVE`), tileset slicing, and autotiling.
   - 2D Physics integration (Box2D / custom 2D colliders, RigidBody2D, Area2D triggers).
   - Sprite sheet frame-by-frame animation sequencer.

2. **Visual Scripting & Gameplay Node Graph (Section 7.1 & 7.2)**
   - Visual event graph canvas for node-based scripting (events, math, branching, and node calls).
   - Seamless interoperability between UVScript text scripts and visual nodes.
   - Gameplay variable management, timer nodes, character movement controllers, and health/damage dispatch.

3. **AAA Post-Processing & Real-Time Lighting (Section 1.1)**
   - Cascaded Shadow Maps (CSM) for directional lights across multiple depth splits.
   - Post-processing pipeline maturity: Bloom, ACES Filmic Tone Mapping, Screen-Space Ambient Occlusion (SSAO), and Fast Approximate Anti-Aliasing (FXAA).
   - Volumetric fog integration (`FogVolume3DComponentUVE`).

4. **Standalone Packaging & Shipping Pipeline (Section 12)**
   - One-click build and export wizard from the editor to produce standalone Windows `.exe` game distributions.
   - Asset cooking step packaging optimized assets into compact archive blobs (`.uvpackage`).
   - Clean shipping build configuration that strips editor debugging symbols and tools.
