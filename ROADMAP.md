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
- [x] Shadow maps for spot lights — one perspective tile per shadow-casting spot light in a
  unified 4096x4096 Depth32F atlas, allocated by a quadtree buddy allocator (power-of-two tiles
  from 4096 down to 128, with recursive four-way coalescing on free so a scene that drops a light
  leaves no fragmentation behind). `ShadowAtlasManagerUVE` landed first and sat unused for a
  while - the type, the tile-rect math and twelve tests all existed with ZERO call sites, which is
  exactly the shape of a feature that looks finished when read and does nothing when run. The renderer half is what this closes: the atlas is created lazily on
  the first frame that has an eligible spot or point light (64 MiB of Depth32F is not a cost an
  empty scene should pay) and the planner decides the layout. Eligibility is a pure function - type,
  positive intensity, the light's own shadow switch, finite transform, a cone angle the shader
  can represent, and an influence sphere that actually reaches the view frustum. The tile size
  is the project's existing "Point/Spot Cube Resolution" setting, halved until the frame's tiles
  fit; the 128 floor still leaves 1024 tiles in a 4096 atlas against a worst case of 24, so the
  budget can only ever reduce quality, never drop a light - which is why there is no per-light
  priority scheme to get wrong. Casters are culled against each light's own
  frustum, and a tile that culled nothing is skipped entirely rather than recording a draw-less
  pass. Rendering reuses `shadow_depth.glsl` verbatim: a tile pass is the cascade pass with a
  viewport override and `depthLoadOp = Load`, and the atlas is cleared ONCE per frame by a
  separate full-attachment pass - per-tile Clear is not a style choice but a bug, because a
  Vulkan attachment clear ignores the scissor and would wipe every tile already rendered. The
  uniforms (`uLightShadowMatrices[4]`, `uLightShadowTileRects[4]`, `uLightShadowEnabled[4]`,
  `uLightShadowDepthRange[4]`, `uShadowAtlasTexelSize`) are uploaded for all four light slots
  every frame, so a slot that stopped casting cannot keep sampling a tile that now belongs to a
  different light; they are indexed by `uLights[]` slot, not by "nth shadowed light", which is
  what ties a tile to the light it shadows. Bias is the part a naive port gets wrong: a
  perspective projection's depth slope changes by orders of magnitude along one ray, so a
  constant normalized-depth bias is either acne beside the near plane or metres of peter-panning
  out at the range - and the same light is both depending on where the surface is. The shader
  therefore converts a world-space bias through the LOCAL slope, and the CPU mirrors that exact
  formula (`SpotShadowLinearDepthUVE` / `SpotShadowDepthSlopeUVE`) with tests that pin the two
  together, including a finite-difference check of the slope and a round-trip against what
  `PerspectiveUVE` actually writes. The atlas moved to texture slot 15 (the last unit inside
  GLES3's 16-unit floor) because the built-in lit path already packs 0-14 densely and a fixed
  slot in the middle is one rebinding away from shadowing a cascade. Frame diagnostics report
  candidates, active tiles, the resolution actually used, passes recorded and draws issued, so
  "why is there no shadow here?" is answerable from a number instead of from the scene graph.
- [x] Shadow maps for point lights — omnidirectional, as six perspective faces. The atlas, the
  tile allocator and the per-light uniform contract were already shared with the spot path, so the
  only question was how to rasterize a light that has no single frustum. A 2D octahedral map is
  what such a light "wants" - and it is what the shader carried first - but the mapping is rational,
  so NOTHING in a plain depth-only pipeline can write it: producing one needs a cube render plus a
  resolve pass or a geometry shader, and a shader function whose producer does not exist is a
  feature that reads as done and samples nothing. Six perspective faces are the shape a depth pass
  can actually write, so the shader's octahedral pair was deleted rather than kept as a comment
  claiming a producer that was never coming. Each point light allocates six tiles in ascending
  face order (+X, -X, +Y, -Y, +Z, -Z), one per face, and the material shader picks a face by
  dominant axis - the same rule a cube-map sampler uses internally, which is why the six faces have
  no seams to fade. The face basis lives in ONE table (`PointLightShadowFaceTableUVE`) that the
  renderer builds each face's view matrix from and the shader mirrors line for line, and the tests
  pin the two together: a one-face disagreement would sample a neighbouring face's depth, which
  reads as a plausible shadow in the wrong direction instead of as a bug. Depth is compared along
  the face axis, not along the ray, because clip-space w - what the depth attachment actually
  stores - is the axis distance; comparing true euclidean distance instead would under-shadow
  every surface near a face corner by a factor of up to sqrt(3). Each face is culled and recorded
  independently, so the five faces that contain no caster cost nothing, and a point light whose six
  allocations cannot all be satisfied is rolled back rather than rendered three-faced. Budget: a
  point light spends six tiles against a spot light's one, so four point lights is the 24-tile worst
  case the shader's fixed arrays describe - and 24 tiles still fit at 512 in a 4096 atlas, which is
  why the resolution policy needs no per-type special case.
- [x] Contact shadows / screen-space shadow refinement - the last few centimetres a shadow map
  cannot resolve. At the atlas resolutions the engine uses, one depth texel covers several
  centimetres of the receiving surface, so geometry that touches it - a crate on a floor, a foot on
  a stair - either never covers a whole texel and loses its shadow entirely, or loses it to the
  normal bias pushing the comparison far enough off the surface to miss a caster standing on it.
  Both failures read as the same thing: the object is floating. The pass asks the question against
  the camera's own depth buffer instead, marching from the shaded surface toward the light and
  counting samples that sit behind the ray by more than a depth bias but less than a configured
  thickness. That upper bound is the whole trick and the part that is easy to omit: without it every
  distant wall behind a grazing ray darkens the surface, which is the classic screen-space
  over-shadow. The march is quadratic rather than uniform - step i is at `length * ((i+1)/n)^2` -
  because contact is by definition at the near end of the ray, so a uniform schedule spends most of
  its samples where nothing can be occluded. Occlusion maps to a visibility fraction capped by
  strength, never to a binary in/out, because a hard edge between marched and unmarched pixels is
  more visible than the shadow it draws. The pass runs at FULL resolution, unlike the half-res SSAO
  term beside it, since it exists to recover detail a blurred buffer would erase; it composites by
  multiplying before bloom so a bright surface next to its contact shadow cannot bleed light back
  over it through the bright-pass threshold; and it refines rather than replaces a shadow map,
  because an occluder that is off-screen or behind the surface is simply not in the depth buffer.
  That limitation is why it is off by default rather than on. The CPU mirror in contact_shadows_uve.h
  - the same schedule, bias rule and visibility mapping, testable without a GPU - is what keeps the
  header and contact_shadow.glsl honest, since the pass has no parity harness to catch arithmetic
  that drifts between the two.
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
- [x] Ragdoll physics (driven skeletal bodies + constraints layered over an animated
  skeleton) — `RagdollSolverUVE`: sequential-impulse articulated bodies at a 1/240 s fixed step,
  Unreal's Free/Limited/Locked per-axis model (swing1/swing2 cone + signed twist, so a knee and an
  elbow are ranges rather than joint types), limit stiffness/damping, positional projection,
  capsule inertia from the shape, sleeping, and bit-exact determinism. `Ragdoll3DComponentUVE`
  builds one capsule per bone from the rig itself (length from the child bone, not a table),
  generates the standard 18-row humanoid profile when no bones are authored, and writes the result
  back as rotation only, root first, so the skinned mesh follows without stretching.
  `BuildRagdollContactsUVE` resolves the body capsules against the world's collider cache
  (sphere/capsule/oriented box/AABB, combined material), and `ApplyRagdollImpactUVE` carries a hit
  that never became a contact into the nearest body, waking it. 32 headless tests
- [ ] Destructible/fracturable physics objects
- [x] Joint types beyond hinge (ball socket, slider/prismatic, fixed, spring/distance) — DistanceConstraintUVE
  (rest-length spring/distance), HingeConstraintUVE (coincident anchors with rotation axis),
  BallSocketConstraintUVE (spherical joint with 3 rotational DOF), and FixedConstraintUVE
  (weld joint locking relative anchor position) are implemented in PhysicsConstraintSystemUVE with
  deterministic island plan generation and mass-weighted solver relaxation; locked by unit tests
- [x] Continuous collision detection for fast-moving small objects (tunneling prevention).
  The fixed step was integrate-then-resolve, which is exact for anything that moves less than its
  own extent per step and silently wrong for anything that does not: a bullet at 100 m/s over a
  10 Hz step travels ten metres between two samples, so a two-centimetre wall is never "between"
  anything the integrator looks at. `continuous_collision_uve` answers the one question the
  discrete step cannot - how far can this body travel before it touches something - with an exact
  swept-sphere/AABB slab test (the box expanded by the mover's radius, which turns the sphere into
  a point) and an exact swept-sphere/sphere quadratic. Both report the impact as a fraction of the
  step with the face normal, so the caller can shorten the step AND drop the velocity component
  pressed into the surface while keeping the tangential part - stopping outright would make every
  wall sticky. The mover is swept as a sphere built from its own world AABB rather than its
  authored shape: rotation-independent, so a spinning body cannot sweep a different volume on two
  machines, and conservative by construction - it can stop a body early but never late, and early
  is recoverable while a tunneled hit is not. Contacts report at `timeOfImpact == 0` when the body
  is ALREADY touching, and the resolver deliberately does not clamp that case: clamping would
  freeze every grounded body, since sliding happens entirely inside a contact. Ties between two
  targets at the same impact time resolve to the lowest enumeration index, so the same scene
  produces the same contact every run. Off by default and gated by a motion threshold - the sweep
  costs several times an integration, and a scene full of slow bodies would pay it to protect
  bodies that cannot tunnel. Layer/mask filtering reuses the collision system's two-way rule, so
  CCD can never stop a body against geometry the discrete resolver would have ignored.
- [x] Multi-threaded physics stepping for large scenes - islands, not threads sprinkled over the
  pair loop. The solver is sequential because every resolved pair writes positions the next pair
  reads, so the only parallelism that cannot change the answer is between groups of bodies that
  share nothing: the contact graph split into connected components. Pairs are grouped with a
  serial union-find over a dense body index. Serial is the deliberate part: a lock-free merge - the
  technique Jolt Physics uses to rebuild islands inside its parallel narrow phase - numbers islands
  by whichever thread won the compare-and-swap, which is exactly the hidden order dependence
  deterministic stepping forbids, and Box2D v3 reached the same conclusion and kept its merge
  serial. Island ids follow first appearance in the pair list and every island's pairs keep their
  original relative order, so the partition is a pure function of the list
  `CanonicalizeCollisionPairsUVE` already sorted.
  The harder half in this engine is that the entity manager and the scene graph are single-threaded
  by contract: a worker may not read a component or write a transform. The step therefore snapshots
  the fields the resolver touches - local position, velocity, inverse mass, yield weight, the
  combined material, the penetration and the two shares - solves whole islands on the pool over
  plain values, and scatters the result from the calling thread. That snapshot is also why the
  sequential and parallel paths cannot drift: both walk the same slot-indexed list in the same
  order, and the parallel version differs only in which thread runs which island. The promise is
  bit-identical state, not approximately equal state, so the test that guards it compares digests
  step by step and then compares the final floats exactly over thirty steps of a two-island scene.
  Off by default in the settings struct and on in the engine, with a break-even pair count because
  dispatch plus the snapshot cost more than the loop they split below a few dozen contacts; the step
  falls back to one thread when there is no pool, one worker, one island, or too few pairs, and it
  reports both the island count and how many islands actually ran on the pool so a caller can see
  whether its scene is even shaped to benefit.
- [x] Deterministic/networked-safe physics stepping (fixed-point or otherwise reproducible)
  for competitive multiplayer use cases. Two things make a fixed step reproducible, and the
  feature is only honest if both are stated separately. **Order** first: the solver is sequential -
  every resolved pair writes positions the next pair reads - so the result depends on the sequence
  pairs are resolved in, and that sequence came from entity storage, which is not a contract a
  netcode layer should ever lean on. Collision pairs are now canonicalized before resolution:
  sorted by their stable (index, generation) keys and re-oriented so the lower key is `first`
  again, with `separationAxis` negated on every swap. The negation is the part that is easy to
  get wrong and expensive to miss - the axis points from `first` toward `second`, so swapping
  endpoints without flipping it pushes both bodies deeper into each other. Canonicalization is
  exact: no approximation, no quality cost. **Quantization** second: identical code and identical
  order still produce identical bits on one build, but not necessarily across compilers or CPUs
  that contract floating-point operations differently, so every comparison happens on an integer
  lattice - state differing by less than a quantum is treated as equal, and a NaN maps to a fixed
  lattice point rather than a payload-dependent bit pattern. Crucially the lattice is applied to
  the DIGEST, not to the simulation: snapping positions every step would silently stop any body
  moving less than a quantum per step, which trades real accuracy for a guarantee that can be had
  without paying it. The digest is FNV-1a over the sorted body keys and their quantized
  position/rotation/velocity, taken after resolution and constraint solving - the state a peer
  would actually send - and it sorts internally, so it describes the world rather than the
  container the world lives in. `PhysicsSystemUVE::GetLastStepDigestUVE()` exposes it, and a
  rollback verifier compares two peers' digests for the same step number. Opt-in, because a digest
  is per-body per-step work that a single-player build has no use for.

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
- [x] Full-body IK, as a stretch goal beyond the two-bone object above - `FullBodyIK3D` is a scene
  object that solves the whole skeleton at once so several limbs share one answer: every effector is
  a bone/target/weight pull on the same pose, and the solve iterates until the pulls agree or the
  budget runs out. The solver pairs bounded CCD sweeps (tip-inward, per-sweep rotation cap, inner
  per-effector convergence) with an exact analytic two-bone finish on every chain's last two joints -
  which removes CCD's straight-limb dilemma outright - and averages the finish where two chains
  claim the same joints so alternating exact solves converge on the pose both can have instead of
  oscillating between them. `rootTranslationWeight` lets the hips step toward unreachable targets
  (the foot-planting case), `positionWeight` leans a limb part-way without leaving the animation's
  pose, and a locked per-sweep `maxJointStepRadians` turns a hard pull into a pose that changes at
  a rate an animator would key. It runs after the two-bone pass and before attachments, refuses
  rather than falls back (a gone target object, a renamed bone, a skeleton no driver posed), writes
  through the shared `BoneModifierComponentUVE` influence, and ships with its serializer
  (per-effector target local ids), Inspector metadata with an editable effector list, and 24 cases
  covering solver geometry, error budgets, spine sharing, root translation, weights, step limits,
  determinism, pass integration, and file round trips.
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
- [x] Animation compression (both curve compression and a runtime decompression path) -
  `CompressAnimationClipAssetUVE` keyframe-reduces each track with a greedy error-bounded pass
  (worst-key insertion until every source key reconstructs inside the reduction budget) and
  quantizes what remains into fixed-stride packed key records: smallest-three 16-bit rotations
  plus range-quantized 16/32-bit position and scale channels picked adaptively so the stated
  tolerance holds even on huge-range tracks. The runtime
  `TrySampleCompressedAnimationTrackUVE` path decodes on the fly with a binary search and exactly
  mirrors the uncompressed clip sampler's clamp/loop and lerp/nlerp semantics; events and the
  rest skeleton stay lossless so retargeting and gameplay timing are unchanged.
  `MeasureAnimationCompressionErrorUVE` proves the bounds on a uniform probe grid, and the suite
  covers error budgets, determinism, quaternion sign flips, root-motion travel, constant-axis
  exactness, and corrupt-stream rejection.
- [x] Additive animation layers (e.g. aim offsets, lean, breathing) on top of a base pose - an
  Additive graph node adds its clip's pose over the base at the parameter's weight, locked by the
  layer cases in the graph suite.
- [x] Blend spaces (1D and 2D) for locomotion blending, distinct from the existing blend
  tree - `AnimationBlendSpace1DWeightsUVE()` / `AnimationBlendSpace2DWeightsUVE()` place the clips
  around the parameter, with synced points and time scaling locked by their own cases.
- [x] Morph targets (blend shapes) - `MeshAssetUVE` carries `MeshMorphTargetUVE` shapes as sparse,
  strictly ascending vertex-delta lists (position/normal/tangent), because a blend shape is a local
  edit: a 50k-vertex head with 52 ARKit-style shapes stores ~100k deltas instead of 2.6M dense
  entries, and the ascending invariant is what makes accumulation order-independent and a duplicate
  delta a detected defect rather than a silent double displacement. The `.uvmodel` payload keeps
  them in a tagged trailing section written only when shapes exist, so every existing file still
  loads and a static mesh still writes the bytes it always did. `AccumulateMorphTargetsUVE` is the
  CPU authority (bind-space accumulation, then skinning - the glTF order), glTF import reads
  `primitives[].targets`, `mesh.weights` and `extras.targetNames` and finally honours
  `importMorphTargets`, and `MorphTargetComponentUVE` addresses shapes BY NAME so re-exporting a
  face with an inserted shape cannot animate the wrong muscle. Locked by 27 cases across the mesh
  asset, the glTF converter, the importer, the component and the scene serializer.
- [x] Facial animation: weight tracks that drive morph targets over time - `AnimationClipAssetUVE`
  carries `AnimationAssetMorphTrackUVE` tracks, one per named mesh, each holding the whole weight
  vector at every key (the shape glTF's `weights` channel defines, so an exporter's keys survive the
  round trip). The clip schema gains `uve-animation-v4`, written ONLY when a clip has morph tracks,
  so every existing clip keeps its exact bytes. `SampleMorphWeightTrackUVE` interpolates linearly
  between keys and clamps outside them, and a facial clip with no object and no bone movement is
  valid for the first time - the validator's "nothing moves" rule counted only those two.
- [x] glTF `weights` animation channel: the one path that carries lip-sync out of a DCC tool.
  `GltfAnimationPathUVE::Weights` reads the SCALAR stream glTF 2.0 section 3.11 defines - every
  target's weight for one key, laid end to end - by resolving `target.node` through the node's
  mesh to its morph-target count, rejecting a mesh whose primitives disagree about that count, and
  lifting the key times and weights as views rather than copies. Conversion emits one
  `morphWeights` track per node carrying the file's OWN keys: a clip's morph track already
  interpolates linearly and clamps outside its range, which is exactly what a LINEAR weights
  channel means, so nothing is resampled and import cannot drift from playback. STEP weights
  channels are rejected instead of resampled - the format has no step mode, and silently lerping
  would change the performance the animator authored. A document with no skin at all is accepted
  when every channel is a weights channel, so an audio-driven take exported from a bare head
  imports; a transform channel still requires a skeleton. Node names come from the whole node array
  in document order (`name`, else "Node", uniquified with the same `_N` suffix bones use), so one
  node keeps one track name across every animation in the file - that name is what the runtime
  addresses. Locked by 21 cases across parse, conversion, rejection and the `uve-animation-v4`
  round trip.
- [x] Physically-simulated secondary motion (cloth bones, jiggle, spring bones) -
  `SpringBoneSimulator3D` now drives a CHAIN: the bone named by the simulator plus its first-child
  descendants, up to `chainLength` (16), solved root to tip so a displaced root carries the rest of
  the chain instead of tearing it apart. Each segment simulates the world position of its bone's
  tail - a spring toward where animation put it, gravity, sphere obstacles, and the link length
  enforced - and the resulting direction is written back into the skeleton's runtime pose as a bone
  rotation, which is what makes the simulation visible at all: the previous implementation computed
  a tip that nothing ever read. Time is consumed in whole 1/120 s steps from a per-simulator
  accumulator, and velocity decays as `exp(-damping * 20 * dt)` rather than `1 - damping` per call,
  so one second of motion traces the same path at 30, 60 and 144 fps. Each joint is clamped to
  `limitAngleDegrees` around the direction ANIMATION gave it, measured against a captured reference
  rather than the solver's own previous output - without that the cone creeps outward a little every
  frame until the chain hangs straight down (a defect the limit case caught on the first run). A
  sphere hit also cancels the inward velocity component, so a chain cannot buzz against an obstacle.
  Locked by 5 cases: per-segment link lengths plus pose write-back, frame-rate independence, the
  per-joint angular cone, sphere push-out, and an unbound simulator being left alone.
- [x] A dedicated animation-authoring/preview tool in the editor - the Timeline tab is a dope sheet
  over the selected AnimationSequencer's clip: one row per bone, each opening into its Position /
  Rotation / Scale lanes, under a ruler with a playhead and a transport (play, scrub, step, loop,
  fit and zoom). Keys are selected, moved, copied, pasted and deleted, curves are edited point by
  point, events are added, renamed and dragged, and every edit is one undo step written back to the
  clip's `.uvanim`. Blend shapes - the part of §4 that had no face in the editor - now get a row
  each, in the clip's own order BELOW the rig (a 90-bone character would otherwise push the face off
  the bottom of the tab), keyed where a weight actually changes and drawn in a colour of their own;
  a skin-less lip-sync clip, which has no bone row at all, finally shows its timing. The preview
  writes runtime state only: the skeleton's pose, and every blend-shape track sampled through
  `SampleMorphWeightTrackUVE` at the playhead and written into the target object's
  `MorphTargetComponentUVE` - the weights each object held are captured before the first write and
  put back when the preview stops, so scrubbing never dirties an entity or its file. The mesh's
  shape names are read once per object per mesh and cached, and a track that cannot play (no object
  with that name, no Morph Targets component, or a weight count that does not match the mesh) is
  counted in the bottom bar instead of failing silently. Which rows the tab shows, in what order,
  and which weights a frame puts on which object are pure decisions in
  `editor_timeline_tracks_uve.h`, locked by 9 cases: bone-only, blend-shape-only and mixed clips,
  the filter, the no-skeleton fallback, the key-showing rule, and the preview resolving, clamping
  outside the key range, and counting what it cannot play. (The broader sequencer/state-machine
  authoring item stays open under Editor Tooling below.)

---

## 5. Audio

- [x] A real audio device abstraction with a null backend for headless use
- [x] A production audio output backend (miniaudio) driving real hardware, with automatic
  NullAudioDeviceUVE fallback on machines without a usable output device
- [x] WAV import/decoding, a PCM16 decoder, and an attenuation model
- [x] A mixer-group concept and a basic gain effect
- [x] A source/listener system with orientation validation
- [x] Additional common audio formats beyond WAV - `EncodedAudioFormatUVE` (FLAC, MP3,
  Ogg/Vorbis) decoded through the already-pinned miniaudio decoder, with Vorbis coming from its
  bundled stb_vorbis. The format is chosen explicitly by the importer rather than sniffed from the
  file name, so an `.ogg` that holds an MP3 stream fails instead of silently decoding as something
  else. `DecodeEncodedAudioUVE` publishes only after every bound holds: source <= 64 MiB, 1..32
  channels, nonzero rate, output within `kMaximumAudioAssetSamplesUVE`, every sample finite, and a
  lossy encoder's overshoot clipped into [-1, 1]. A stream that declares its length must deliver
  exactly that many frames, so a truncated file is refused instead of half-played; an unknown length
  decodes under the same output cap; malformed or empty input leaves the caller's asset untouched.
  `flac`, `mp3` and `ogg` register as importers that write the SAME `.uvaudio` envelope WAV already
  wrote - replacing an existing one atomically through a temporary, so a failed decode never damages
  it - and both the WAV and the compressed importers are registered at bootstrap. The Content
  Browser shows every source format an importer can read as audio (and deliberately leaves `.m4a`,
  `.aac` and `.wma` as plain files, since no importer can turn them into an asset). Locked by cases
  over real fixtures for all three codecs: decode, import and GUID registration, the wrong-format
  matrix (no fallback between codecs), a truncated header, a malformed file preserving the
  destination, an oversize sparse source refused before its contents are read, a declared
  larger-than-cap length refused before allocating, publication failure leaving the destination
  intact, and replacement of an existing envelope. (Turning a dropped source file into an asset is
  still the importer's job rather than the Content Browser's: the editor has no audio auto-import
  action yet, exactly as for WAV, which is why that workflow is not claimed here.)
- [x] A real DSP effect chain beyond gain (reverb, low-pass/occlusion filtering, EQ,
  compression/limiting) - every mixer bus now runs a fixed, ordered stack of typed inserts instead
  of a bare gain multiplier: `Gain`, `LowPass`, `HighPass`, `PeakEq`, `Compressor`, `Limiter` and
  `Reverb`. Filters are RBJ biquads in transposed direct form II (Robert Bristow-Johnson's Audio EQ
  Cookbook), the reverb is a Schroeder/Freeverb comb-and-allpass network with damping in the
  feedback path, and the dynamics are a feed-forward envelope follower with a hard knee plus a
  brick-wall limiter whose attack is instantaneous - a sample that arrives over the ceiling is
  already under it when it leaves. Slots run in ascending index order, which is what makes a
  limiter-then-gain chain audibly different from gain-then-limiter, and the chain clamps into
  [-1, 1] exactly ONCE at the end: no stage clips before the next filter runs, and a NaN or an
  infinity is scrubbed to silence instead of reaching the device. Insert configuration - including
  the only allocation in the whole path, the reverb delay lines - happens in `SetMixerInsertUVE()`
  and `SetSampleRateUVE()`, never in `ProcessUVE()`: a rejected description leaves the previous
  insert and its running state untouched, and a rate outside 8-384 kHz is refused rather than
  silently detuning every filter. A cutoff is clamped to 0.45 of the device rate, so a value past
  Nyquist cannot make a biquad unstable. The device rate is pushed from the project config at
  bootstrap, and every bus registered later inherits it. This is the engine half only: occlusion
  still has to be DRIVEN by the physics/collision system, there is no analyser/metering, and the
  chain is mono because the PCM path it sits on is mono - stereo arrives with the spatialization
  item. Locked by 16 DSP cases (validation, DC/stop-band response, EQ gain at centre, compressor
  slope and 1:1 transparency, limiter ceiling, reverb tail decay, bypass identity, slot order,
  single-clamp, non-finite scrubbing) and the 12 bus-graph cases that drive them through the mixer.
- [x] Full 3D spatialization (HRTF-based or at minimum proper distance/cone/doppler
  modeling beyond basic attenuation) - distance attenuation is now joined by a stereo pan, a
  directional emitter cone and Doppler. The pan is the classic equal-power law, normalised so pan 0
  is EXACTLY unity on both channels: centred content mixes at the level it had before panning
  existed, hard left is sqrt(2) on the left and exact zero on the right (a float cos(pi/2) is a hair
  below zero, and a negative gain would invert one channel instead of silencing it, so the ends are
  pinned), and the power sum is constant across the range. Azimuth comes from the source offset
  projected onto the listener's right axis, derived from the same forward/up pair the listener
  already carries, with +X when both the default orientation and a degenerate pair ask for it; a
  source sitting exactly on the listener pans centre rather than dividing by zero. The cone takes
  full cone angles in degrees measured from the emitter's own axis: full volume inside the inner
  angle, `coneOuterGain` beyond the outer one, linear in angle between - and the omnidirectional
  360/360 default multiplies by unity, so a source that never sets a cone behaves exactly as before.
  A zero-length axis or offset reports 0 degrees (full volume), so an unset direction can never be
  the reason a source goes silent, while a non-finite angle fails closed to silence. Doppler is the
  classic moving-source/moving-listener form at 343 m/s: motion along listener-to-source is measured
  for both parties, so a source closing on the listener raises pitch and a receding one lowers it,
  and a listener running at a stationary source shifts it too. Velocities are derived from the frame
  delta the engine now hands `UpdateUVE(deltaSeconds)`, capped at 0.1 s so a frame hitch cannot
  fabricate a 200 m/s source, and a source's first frame after it appears is treated as stationary
  rather than as a displacement from the origin. A caller that passes 0 (or a non-finite delta) gets
  exactly 1.0 - no
  division by zero and no Doppler, which is what headless tests and any caller that does not track
  frame time want. The factor is per source ([0, 1], 0 disabling it), the result is clamped to [0.5, 2] so a
  supersonic or impossible velocity can never hand the device a poisoned pitch, and a source sitting
  on the listener is exactly unity. The mixed pitch is the source pitch times the Doppler shift,
  still through the one `EvaluateAudioMixParametersUVE` contract, and the finished gain/pitch/pan
  triple is what reaches `IAudioDeviceUVE::SetVoiceParamsUVE` - the device never re-derives any of
  it. miniaudio applies the pan gains per output channel while mixing (a centred voice is
  bit-identical to the pre-pan mix), and both backends validate the finished pan in [-1, 1] like
  every other parameter. HRTF, ambisonics and occlusion are deliberately NOT claimed here: a head-
  related transfer function needs a binaural renderer and per-listener HRIR data, which belongs to
  the ambisonics/VR item below, and occlusion belongs to the physics item. Locked by nine unit cases
  on the laws themselves (centre exactness, constant power, clamping, monotonic pan, the right-axis
  derivation including the degenerate fallback, cone edges and the degenerate cone, cone angle
  geometry, static/zero-factor/coincident Doppler, approaching/receding/listener-motion symmetry,
  and the clamp) plus eight integration cases through the real system and the real null device
  (right/left pan, behind-is-centred, a 2D source staying centred, a cone turning a source down by
  its outer gain at equal distance, an approaching and a receding source shifting the device-facing
  pitch, zero delta disabling Doppler, a freshly spawned source not inventing a velocity, and the
  descriptor rejecting an inverted cone, an out-of-range factor and a zero axis).
- [x] Audio occlusion/obstruction driven by the physics/collision system - a blocked source is now
  both quieter and muffled, and what decides "blocked" is geometry rather than a flag. The audio
  module declares a one-method query interface (`IAudioOcclusionProviderUVE`, returning blockage in
  [0, 1]) instead of depending on physics - physics has no reason to know about audio and the
  library dependency would be a cycle - and `RaycastAudioOcclusionUVE` in `uve_core`, the module
  that already owns both, answers it from `IRaycastSystemUVE`. It traces three paths - left ear,
  head centre, right ear, 10 cm either side of the listener - rather than one, because a single ray
  is a hard oracle that flips between "clear" and "fully blocked" as a source crosses a doorway
  edge; three rays give a blocked fraction in thirds, so a source sliding out from behind a wall
  fades instead of popping. Each trace stops one centimetre short of the emitter and excludes the
  source's own `ownerEntity`, so a radio with a collider is not occluded by its own geometry, and
  the query carries the source's layer mask so decorative and trigger geometry can be excluded in
  the level rather than paid for at runtime. Cost is bounded on purpose: the system queries each
  source at most once per `kAudioOcclusionQueryIntervalSecondsUVE` (0.1 s, ten hertz - two orders
  below the frame rate) and smooths every frame toward the answer, with a 60 ms attack and a 250 ms
  release so blocking is immediate and clearing is not twitchy. The smoothed value maps to gain
  (`ComputeAudioOcclusionGainUVE`, linear to the authored floor, default 0.35 = about -9 dB, because
  a wall is a level drop and not a switch) and to a low-pass corner
  (`ComputeAudioOcclusionCutoffUVE`, interpolated in LOG frequency from 20 kHz to the authored floor,
  default 700 Hz, since hertz are perceived logarithmically) and snaps to the target within the
  bypass threshold, so an uncovered source reaches exactly the 0 sentinel the devices need to skip
  the filter rather than staying slightly dull forever. The muffling itself is a per-voice one-pole
  low-pass (`AudioVoiceLowPassUVE`) run by the backend on the interpolated sample before gain and
  pan, in both the clip and the streaming mix paths; it is bypassed bit-for-bit at cutoff 0, has
  unity DC gain so occlusion changes level only through the gain mapping, and is reconfigured only
  when the corner actually moves. Everything is reachable from authored data:
  `AudioSourceComponentUVE` gained `occlusionEnabled` / `occlusionGain` / `occlusionLowPassHz` /
  `occlusionLayerMask` with inspector metadata and scene persistence through the existing
  reflection-driven serializer, `AudioSourceSystemUVE` carries the owning entity into the descriptor
  and treats an occlusion edit as a voice replacement exactly like a distance edit, and the engine
  wires the provider once at startup - with no provider installed the mixer is bit-identical to
  before, which is why every pre-existing audio test still passes untouched. Locked by 13 model
  cases (gain and cutoff endpoints, monotonicity, the geometric-mean midpoint, non-finite and
  out-of-range containment, the bypass threshold, attack-versus-release, the convergence snap, and
  the filter's own DC unity, bypass bit-equality, rolloff and reset), 9 system cases (gain and
  muffling applied, clear-path bypass sentinel, ten-hertz query throttling, smoothing ramp, provider
  removal fading back to clear, non-spatial and opted-out sources never queried, the query carrying
  the listener basis and owning entity, a non-finite answer failing open, and descriptor
  validation), 8 provider cases against the real entity manager, scene graph and raycast system
  (empty world, wall between, wall behind, own-collider exclusion, layer mask, coincident source,
  and the one-of-three partial blocker) and 3 device cases (parameter validation on the null device,
  a real audio-thread drain of an occluded stream, and sweeping the corner between frames).
  Deliberately NOT claimed: obstruction is a binary per-ray hit rather than a material-aware
  transmission model (concrete and glass block equally - `PhysicsMaterialUVE` has no acoustic
  property yet), there is no reverb-zone or portal coupling, and the ear offset is a constant rather
  than a per-listener setting.
- [x] Streaming playback for long audio (music, VO) instead of fully-decoded-in-memory
  playback only - a streaming voice never decodes a clip at all. The device keeps a bounded ring of
  `kAudioStreamRingFramesUVE` frames (8192, a little under a fifth of a second at 48 kHz) per stream
  and the caller pushes interleaved frames into it, so the cost of a ten-minute music bed is one
  ring rather than its whole decoded length. `IAudioDeviceUVE` gained the matching contract -
  `CreateStreamVoiceUVE(format)`, `PushStreamFramesUVE` (reports exactly how many frames it took,
  dropping the newest when the ring is full instead of growing anything), `EndStreamUVE` (play out
  what is buffered, then stop), and the two ring queries the pump paces itself on. Lifetime,
  Play/Stop/state and gain/pitch/pan remain the clip voice's, so nothing above the RHI needs a
  second code path. Both backends implement it: the null device keeps the ring and documents a
  drain hook for tests, and miniaudio's callback mixes from the ring with the same fractional
  stepping and linear interpolation it already used for clips - including the end-game, where the
  final frame plays on its own instead of being dropped for want of a successor, and a drained
  ended voice stops itself so an entity never reads as playing silence forever. Above the device,
  `IAudioStreamSourceUVE` is a pull interface (`format`, `frameCount`, `readFrames`, `seek`) that
  the system drives once per frame from ordinary `UpdateUVE()`: `CreateStreamingSourceUVE(desc,
  source)` borrows the source, ignores the descriptor's asset path (the stream owns the media) and
  keeps every other field's meaning - spatialisation, cone, Doppler, volume, pitch, mixer group - so
  a streaming source pans, attenuates and ducks exactly like a clip. `PumpStreamingSourceUVE()` is
  exposed for pre-buffering before `PlayUVE()` and for tests; `looping` is honoured by rewinding a
  seekable source and end of stream is reported to the device, after which pumping is a no-op.
  `AudioFileStreamSourceUVE` is the real decoder-facing implementation: it reads one `.uvaudio`
  envelope header (magic, asset kind, envelope and payload versions, channels, rate, frame count),
  checks the declared size against the bytes on disk, then seeks per window and decodes only that
  window, validating every sample before it is published, so a truncated or corrupt file fails the
  read instead of feeding the mixer. The envelope-header and payload-version constants now live in
  their owning headers rather than being duplicated by the reader. Locked by 15 cases over the ring
  and the file source (partial writes, wraparound, peek/consume, reconfiguration, mono versus stereo
  frames, windows in order, seeking, and the missing/wrong-kind/truncated/bad-version refusals), 6
  null-device cases (ring accounting, whole-frame rejection, clip-voice rejection, end-then-drain
  stop), 5 system cases (one pump per frame, loop rewind, end-and-drain, clip sources staying
  non-streams, and pan plus distance gain still applying) and 3 miniaudio cases (real audio-thread
  drain to Stopped, invalid formats, clip voices rejecting stream calls). Deliberately NOT claimed:
  a compressed file still goes through the importer and becomes a fully decoded `.uvaudio` asset
  first, so streaming a 200 MB FLAC straight off disk would need an incremental decoder that this
  item does not add, and there is no per-stream decode budget or priority yet.
- [x] A real-time audio mixing graph / bus system with runtime-adjustable submixes - the mixer is
  now a bounded tree instead of a flat list. Master is the root and can never be removed or
  reparented; every other bus names a parent (Master when none is given, which is what the flat
  registration call has always meant), and a source hears the product of its own bus and every
  ancestor, so ducking Master ducks the whole game while ducking `Music` ducks only its subtree.
  Mute silences a bus and everything under it. Solo keeps exactly the soloed chain alive - the
  soloed bus, everything under it, and every bus it feeds into - and silences the rest, with mute
  still winning over solo on the muted path. Neither switch overwrites an authored multiplier, so
  unmuting restores precisely what the mixer had. Every structural edit is validated before it
  lands: a move that would make a bus its own ancestor is refused rather than risking a later
  infinite walk, unknown parents are refused, the table is capped at 32 buses including Master,
  removing a bus that still has routed sources is refused, and removing an empty one reparents its
  children onto its own parent so no edge can dangle. `UpdateUVE()` now feeds the device the
  effective value, and a pitch product that leaves the accepted range is clamped instead of being
  rejected into silence. Each bus also carries four fixed-order insert slots that run on the sample
  path after its effective volume (`ProcessBusChainUVE`), which is the seam the typed DSP chain
  below plugs into. Locked by 11 mixer cases (parent composition,
  cycle refusal, mute/solo interaction, safe reparent-on-removal, the cap, slot ordering, atomic
  failure, tree-shaped diagnostics) and 6 system cases proving the graph reaches both the device and
  the sample path. Typed DSP nodes, EQ/reverb, and per-bus metering remain open below.
- [x] An in-editor audio authoring tool (mixer view, real-time meter, attenuation curve editor) -
  the mixer tree, a live level for every bus, and a falloff editor for the selected source, as an
  `Audio` tab in the lower workspace. The parts that can be wrong live in a model with no ImGui
  dependency (`editor_audio_mixer_uve.h`), the same split the render-stat rows already use, so a
  mixer bug is reproducible in a unit test instead of only by opening the editor. The mixer lists
  every bus the engine reports - name, indentation by depth, authored volume, mute, solo, routed
  source count, live level - and flags the two rows a level alone cannot explain: a bus that
  carries sources yet produces silence (muted, soloed out, or multiplied to zero) and one whose peak
  is past full scale. Volume edits go through the project settings for the four registered buses
  (Master/Music/SFX/Voice), where the engine's existing "Audio" settings subscription already
  applies the change to the live mixer, so the running mix and the saved project can never
  disagree; a bus with no setting is edited live and says so in its tooltip rather than pretending
  to persist. Mute and solo are session state by nature and never persist. The meter is the real
  sample path: the backend measures each voice's own contribution while it mixes (post-filter,
  post-gain, post-pan) once per block, holds the peak and releases it at 20 dB/s, and averages RMS
  over a 0.3 s window, all without allocating on the audio thread. The reading is idempotent, so one
  frame can draw a source row and the bus above it without the second read seeing zero; a bus's
  level is the power sum of every voice routed to it or under it, which for the uncorrelated signals
  a game mix is made of equals what a true bus-buffer sum would show and costs no per-bus memory.
  The falloff editor samples `ComputeAudioSourceDistanceAttenuationUVE` itself - the preview cannot
  drift from what is heard - plots it in dBFS from 0 dB down to the meter floor, marks the authored
  min and max distances over the plot, and draws a quarter more range past the falloff point so the
  tail reads as a tail rather than as a clipped graph. Edits route through the same metadata-driven
  property writes the inspector uses, so a drag is one undo step and the component's own validation
  applies; during play the fields are read-only while the curve stays visible. Locked by 11 meter
  cases (dB conversions, the floor as exact silence, normalized scale, hold-then-release at a
  constant dB rate, the averaging window, zero-delta handling, non-finite containment, power sums),
  6 system cases (source and bus readings, unknown names, sub-bus membership, read-order
  independence, an empty bus), 2 backend cases against the real audio thread (a measured
  contribution and its release to silence, and an unknown handle) and 10 model cases (row order and
  levels, dB text, the silence and clipping flags, which buses persist, truncation, the curve
  matching the engine sample for sample, the tail range, a 2D source, and degenerate sample counts).
  Deliberately NOT claimed: the bus tree itself is still engine configuration rather than authored
  document data, so a bus cannot be created or reparented from the panel yet; the bus level is an
  energy sum rather than a true bus-buffer measurement, because the backend mixes straight to the
  output and applies no per-bus inserts; and bus DSP inserts are not drawn, since they are applied by
  callers on their own PCM rather than by the device.
- [x] Ambisonics / spatial audio bed support for VR/360 use cases - first-order ambisonics in the
  AmbiX convention (ACN channel order, SN3D normalisation), the layout the 360/VR ecosystem actually
  exchanges, so a bed authored for a 360 workstation or a 360 video drops in without a conversion
  pass. Two decisions are recorded here rather than left to be discovered later. First, the adapter
  produces STEREO instead of a new voice type: a bed is a stream whose samples happen to be a
  decoded sound field, so it reuses the streaming ring, the once-per-frame pump, mixer routing,
  pitch and the device path untouched, and memory stays at one ring plus one decode window no
  matter how long the ambience is. Four-channel `.uvaudio` sources became readable for exactly
  this: sources may now carry four channels while device voices still carry at most two, so an
  ambisonic file can never be played as if it were plain stereo. Second, the decode is a coincident
  cardioid pair - two virtual microphones pointing hard left and right - normalised so an
  omnidirectional field is exactly unity on both channels, which is the engine's own "centre is
  unchanged" pan rule, so a bed can crossfade against a mono voice without a level jump. That pair
  cannot tell front from back; the decoder says so in as many words rather than glossing over it.
  Elevation still rotates into the side channels, so head roll and pitch do move overhead content,
  and a binaural decoder would replace the decode function, not the format. Rotation is one
  orthonormal 3x3 per block with determinant +1 - W and the field's energy are preserved exactly,
  not approximately - driven by the same forward/up pair the listener already carries, so a camera
  or an HMD feeds both. Read windows are capped at the device ring instead of sized by an argument,
  a source that overruns its window is dropped rather than read past, and a non-finite sample
  becomes silence. Locked by 12 math cases (the axis mapping including cross-product preservation, a
  default basis that must be the identity, a yaw that must land a world-front wave on the right,
  orthonormality and determinant across seven head bases, W and energy preservation, roll bringing
  overhead content to the side, degenerate bases, in-place rotation, decode unity and steering,
  front/back symmetry, gain and non-finite input) and 14 adapter cases (the source/device channel
  caps pulled apart, refused channel counts, rate and length forwarding, unity and hard-left decodes, head yaw steering, gain validation, a
  refused orientation keeping the last good one, ring-sized windows, end-of-source and seek,
  non-finite silence, an overrunning source, four-channel file reading, and a file-backed bed
  pumped through AudioSystemUVE and the null device across a loop). Deliberately NOT done: nothing
  in the ECS creates a bed automatically - no component, no editor panel, no camera-to-bed wiring -
  and FuMa (WXYZ) input is an import-time conversion rather than a runtime layout, so a mis-ordered
  file is refused instead of being quietly mis-localised.

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
- [x] More host bindings: collision/overlap events, audio, physics forces, other node kinds —
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
- [x] Additional widget types: sliders, checkboxes, dropdowns, text input fields, scroll
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
- [x] Input focus and gamepad/keyboard UI navigation (tab order, D-pad navigation) for
  controller- and accessibility-friendly menus — Tab and Shift+Tab cycle focus through focusable
  widgets (buttons, checkboxes, sliders, dropdowns, text fields) in spatial reading order with wrapping,
  Enter activates buttons and opens/picks dropdown items, Space toggles checkboxes, arrow keys step sliders,
  Escape releases focus, and mouse-focused fields update the tab order start; locked by UIFocusNavigationUVETest
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
- [x] Build variants (debug/development/shipping) with shipping builds stripping
  debug-only code paths and assets. Four profiles now exist - Debug, Development, Release,
  Shipping - and `Engine/CMake/UveBuildConfiguration.cmake` is the only place that decides
  which one is in effect. It also fixes the reason this row went unnoticed for so long: the
  module used to force `CMAKE_BUILD_TYPE` back to `Debug`, so `-DCMAKE_BUILD_TYPE=Release`
  produced an unoptimized binary with assertions enabled and every test still passed. An
  explicitly named profile now wins and drives the build type; otherwise the profile is
  derived from `CMAKE_BUILD_TYPE`; otherwise it is Debug with a warning. The profile also
  owns `UVE_DEBUG` instead of `NDEBUG`, because CMake's stock `RelWithDebInfo` defines
  `NDEBUG` and would strip assertions from the Development build that playtesting needs.
  Shipping is the profile that actually strips: `UVE_COMPILED_LOG_LEVEL=3` deletes
  `UVE_TRACE`/`UVE_INFO` calls in the preprocessor (format strings and arguments never
  reach the compiler; `Warning` and above survive because the runtime threshold would have
  printed them), and the threshold is bound by a static assertion to the profile's runtime
  default so a variant cannot delete messages it claims it can emit. The same header
  rejects, at compile time, a shipping build carrying live assertions, two profiles
  defined at once, or no profile at all. Assets are still not cooked or stripped per
  variant - that stays the asset-cooking row below - and there is no per-platform
  packaging yet. The profile table, the precedence rules and the strip thresholds are
  documented in `BUILD_VARIANTS.md`.
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
- [x] A public contribution guide with coding standards, PR process, and issue templates —
  CONTRIBUTING.md details C++23 standards, module boundary discipline, warnings-as-errors policy,
  naming conventions, and PR workflow, accompanied by .github/PULL_REQUEST_TEMPLATE.md and
  issue templates (bug report and feature request)

## Suggested near-term focus (once this document itself is in place)

The items below are the ones most likely to unblock the largest number of *other* items on
this list, and are a reasonable place to resume work first:

1. Real skeletal animation + skinning (section 4) — almost every "character game" feature
   downstream of it (ragdoll, IK, cloth bones, proper retargeting) is blocked without it.
2. A physically-based material model and cascaded shadows (section 1.1) — the single
   biggest visible quality gap against current-generation visual bars.
3. A true docking window system for the editor (section 10.1) — every future editor tool
   (material editor, sequencer, profiler) is more valuable once panels can be freely
   arranged, and building each new tool against the current fixed-position system means
   redoing layout work later.
4. Navmesh + pathfinding (section 7.3) — DONE and checked above (bake, A*, string-pulling,
   agents with separation steering, 48 tests green). Behavior trees and perception remain.
5. A minimal real networking transport + replication slice (section 6) — currently the
   least-built major system in the entire engine, and the one most games eventually need
   in some form.

This list is a starting suggestion, not a mandate — revisit it whenever priorities change,
and keep the checkboxes above as the durable record either way.
