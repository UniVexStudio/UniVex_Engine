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

## How to read this document

- `[ ]` = not started, or only a stub/placeholder exists.
- `[x]` = verified real and working in the current codebase (confirmed by reading the
  actual source, not assumed).
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
- [ ] Cascaded shadow maps for directional lights (multiple shadow-distance bands)
- [ ] Shadow maps for point/spot lights (cube-map and perspective shadow variants)
- [ ] Contact shadows / screen-space shadow refinement
- [ ] A true physically-based material model (metallic/roughness or specular/gloss,
  energy-conserving BRDF, image-based lighting for ambient specular)
- [ ] Deferred or forward+/clustered lighting path for scenes with many dynamic lights
- [ ] Screen-space reflections
- [ ] Real-time reflection probes (baked and/or dynamically updated cubemaps)
- [ ] Global illumination (baked lightmaps at minimum; a real-time or hybrid GI solution
  as a long-term goal)
- [ ] Volumetric fog / volumetric lighting
- [ ] Temporal anti-aliasing (and/or a modern upscaling technique)
- [ ] HDR display output and a real color-grading / LUT pipeline
- [ ] Order-independent or improved transparency sorting
- [ ] Decal rendering (the `decal` scene-node kind already exists as a descriptor; no
  rendering system backs it yet)
- [ ] Ray-traced reflections/shadows/GI as an optional high-end path (long-term)

### 1.2 Scene scale & performance
- [ ] GPU instancing for repeated meshes
- [ ] Frustum culling at scale (currently unverified beyond basic per-object draw calls)
- [ ] Occlusion culling (the `occluder` scene-node kind exists as a descriptor only)
- [ ] Level-of-detail switching (the `LOD group` scene-node kind exists as a descriptor
  only; no runtime LOD selection system exists)
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
  simulation with no CPU round trip (needs GPU-side emission/compaction), plus culling and
  skinning
- [ ] Bindless/descriptor-indexing-style resource binding for reduced per-draw overhead

---

## 3. Physics & Collision

- [x] Rigid-body dynamics with angular dynamics, a real narrow-phase collision system, and
  a broad-phase AABB cache
- [x] Raycasts, shape casts, and a general physics query system
- [x] Trigger volumes with enter/exit lifecycle tracking
- [x] A constraint system with hinge motors and limits
- [x] A kinematic character controller with slide/step-up sweep behavior, exposed as a
  real, addable component with gravity/jump/ground-state handling
- [x] Configurable per-surface physics materials (friction/restitution)
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
- [ ] A real skeletal mesh + bone hierarchy + GPU skinning system (the `skeleton` and `bone
  attachment` scene-node kinds already exist as descriptors; no skinning/rig runtime backs
  them yet)
- [ ] Inverse kinematics (two-bone IK for limbs at minimum; full-body IK as a stretch goal)
- [ ] Root motion extraction and application
- [ ] Animation retargeting done properly, as its own scoped system with real bone-mapping
  validation (a prior, non-functional retargeting attempt was deliberately removed from
  this codebase rather than kept half-working — see the project's own change history; this
  is the "do it right" follow-up)
- [ ] Animation compression (both curve compression and a runtime decompression path)
- [ ] Additive animation layers (e.g. aim offsets, lean, breathing) on top of a base pose
- [ ] Blend spaces (1D and 2D) for locomotion blending, distinct from the existing blend
  tree
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
- [ ] A real transport layer (UDP-based, with a real socket abstraction per platform)
- [ ] A client-server session/connection lifecycle (connect, handshake, disconnect,
  timeout, reconnection)
- [ ] Entity/state replication (which properties replicate, at what rate, to which clients)
- [ ] A remote-procedure-call framework callable from the scripting/gameplay layer
- [ ] Client-side prediction and server reconciliation for responsive movement
- [ ] Lag compensation for hit registration
- [ ] Interest management / relevancy (only replicate what a client can see or needs)
- [ ] Voice chat
- [ ] A matchmaking/lobby layer, or at minimum a clean integration point for a third-party
  one
- [ ] Network debugging tools (packet inspector, simulated latency/loss for testing)
- [ ] The now-redundant `Networking` placeholder folder and the real `Network` folder
  should be reconciled into one clearly-named module once real network code exists, instead
  of carrying two same-purpose folders forward

---

## 7. Scripting, Gameplay Framework & AI

### 7.1 Visual scripting (the strongest area right now)
- [x] A real node-based visual scripting graph: authoring, a persistent node/pin/link data
  model, undo/redo, branching graphs
- [x] A real compiler pipeline: graph → intermediate representation → bytecode, with real
  diagnostics on failure
- [x] A bytecode virtual machine that actually executes compiled graphs at runtime, ticked
  per-frame against live entities
- [x] Real, non-test-only bindings from script nodes to engine systems: keyboard/mouse
  input, and on-collision-enter/exit callbacks
- [x] Hot-reload support for scripts (a dedicated hot-reload path exists in the module)
- [x] A script debugger module exists in the codebase
- [ ] Full language-completeness parity: loop control (break/continue), user-defined
  functions/subgraphs with parameters and return values, arrays/maps as first-class script
  values, user-defined structs
- [ ] Real bindings for the remaining unwired categories: gamepad/action-mapped input,
  direct entity transform read/write from script, audio playback (`play sound`), physics
  forces/impulses beyond raycast queries
- [ ] A visible, steppable in-editor debugger (breakpoints, single-step, live variable
  inspection) — distinct from the debugger module's existence, which needs a real UI on
  top of it
- [ ] A text-based scripting language option (for programmers who prefer code over graphs),
  or first-class embedding of an existing scripting language, sharing the same engine
  bindings as the visual graph

### 7.2 Gameplay framework
- [ ] A formal actor/pawn/controller-style gameplay object model above raw ECS entities +
  components (something a gameplay programmer authors against directly, not just entities
  and components)
- [ ] An input-action-mapping layer (bind a logical action like "Jump" to any physical
  input across keyboard/gamepad, with rebinding support), rather than scripts polling raw
  key codes directly
- [ ] A gameplay tag / gameplay-attribute system (health, stamina, status effects) as a
  reusable framework rather than one-off components per game
- [ ] A cinematic/sequencer tool for cutscenes (keyframing cameras, animation, audio, and
  gameplay events on a shared timeline)
- [ ] A trigger/event graph layer for level scripting distinct from full visual scripting
  (lightweight "on overlap, do X" level logic)

### 7.3 AI
- [ ] Navmesh generation from level geometry (the `navigation region`/`navigation agent`
  scene-node kinds already exist as descriptors; no navmesh baking or pathfinding system
  backs them yet)
- [ ] A* / pathfinding over the generated navmesh, with agent avoidance (steering around
  other agents)
- [ ] A behavior-tree or utility-AI framework for authoring NPC decision-making
- [ ] A perception system (sight/hearing cones feeding AI decisions)
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
- [ ] A mesh/animation interchange format importer beyond glTF/OBJ (an FBX-class importer,
  since most DCC tool exports still default to it in many pipelines) — or a documented,
  deliberate decision to standardize on glTF only and require artists to export to it
- [ ] Texture compression (BCn on desktop, ASTC/ETC on mobile) baked at import time, not
  just raw decoded pixels
- [ ] Mipmap generation as a first-class import step
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
- [ ] Layout containers (horizontal/vertical stacks, grids, anchors/margins that respond to
  screen-size and aspect-ratio changes) — current UI elements are placed at fixed pixel
  positions
- [ ] Additional widget types: sliders, checkboxes, dropdowns, text input fields, scroll
  views, progress bars, tooltips
- [ ] Rich text (multiple fonts/sizes/styles/colors within one text block, not just one
  baked font per label)
- [ ] 9-slice/scalable image borders for resolution-independent UI art
- [ ] UI animation/tweening (transitions, easing) as a first-class authoring feature
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
- [x] A real, docked-looking (though not true-docking — see below) ImGui-based editor
  shell: title bar with a consolidated menu, workspace tabs, a real 3D Viewport panel with
  gizmos, overlay bubbles, and orbit/pan/zoom camera control
- [x] A Scene Hierarchy panel with per-category node icons and a centralized node-creation
  registry covering dozens of node kinds
- [x] An Inspector panel with per-component drawers, add/remove/undo/redo authoring, and a
  contextual component list (only offers components relevant to what an entity already is)
- [x] A merged Content Browser (file tree + thumbnail grid) with real thumbnails, search,
  and favorites
- [x] A visual scripting workspace: a real node-graph canvas with category-colored/iconed
  nodes, a persistent zoom/fit control, a floating searchable node picker reachable by
  right-click, long-press, or dragging a wire into empty space, and real drag-to-wire
  connection
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
  view for the render graph, a memory-usage view)
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
  character" sample, a "multiplayer" sample once networking exists, a "UI" sample, etc.)
- [ ] A public contribution guide with coding standards, PR process, and issue templates

---

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
4. Navmesh + pathfinding (section 7.3) — the most commonly needed AI building block, and
   currently completely absent despite scene-node descriptors already anticipating it.
5. A minimal real networking transport + replication slice (section 6) — currently the
   least-built major system in the entire engine, and the one most games eventually need
   in some form.

This list is a starting suggestion, not a mandate — revisit it whenever priorities change,
and keep the checkboxes above as the durable record either way.
