# Engine Foundation — Types, Resources and Components

This document catalogues the **building blocks** the engine is assembled from: the
math primitives, containers, allocators, handles, resources and components that
every system above them depends on. For each one it records what it is, how it
works, where it is used, and — for the ones that do not exist yet — why a
production engine needs it and what stays blocked until it lands.

It exists because the other four planning documents all work one level higher, at
the *system* level, and none of them describes the layer underneath:

| Document | Covers |
|---|---|
| `ROADMAP.md` | Feature gaps per subsystem — rendering, physics, audio, networking, scripting, assets, UI, editor, platform, packaging |
| `SCENE_NODES_ROADMAP.md` | Which scene nodes work, which are authored-data-only, which are missing |
| `STUB_IMPLEMENTATION_ROADMAP.md` | Specific stubs to finish, tiered by cost |
| `AUDIT.md` | Code-quality findings |
| **`FOUNDATION.md`** (this file) | **The type, resource and component layer everything above is built from** |

The boundary is deliberate and this document stays on its own side of it. Where a
gap here has a system-level counterpart in `ROADMAP.md`, this file links to it
rather than restating it. No third-party engine or product name appears anywhere
below, matching `ROADMAP.md`'s stated policy.

## How to read this document

Status markers are the same four `ROADMAP.md` uses, so all five documents read as
one set:

- `[x]` — verified: exists, works, confirmed by reading the source, and for anything with
  real logic behind it, backed by dedicated tests.
- `[/]` — wired but not fully verified: a real implementation exists and is confirmed by
  reading the source, but it lacks the test coverage to call it verified.
- `[~]` — exists but is incomplete, unused, or narrower than it looks. The note
  says exactly how.
- `[ ]` — does not exist. Confirmed by listing the directory, not assumed.

Depth is **tiered on purpose**. The runtime declares 844 public types across 371
headers; explaining every one at equal length would produce something nobody
reads. So:

- **Deep** — the foundation layer (Part I) and the resource layer (Part II). These
  are the types every other system touches, and the ones whose absence forces
  workarounds elsewhere. Each gets its representation, its real capability, and
  its gotchas.
- **Medium** — components (Part III): what writes each one and what reads it.
- **One line** — everything else (Part IV), pointing into `ROADMAP.md`.

Every claim below was verified against the tree. Where two sources disagreed the
code won, and two such corrections are called out explicitly in the text. Anything
still uncertain says so rather than guessing.

---

## Module conventions — how to add anything in this document

New types are mechanical to add because every module has the same shape. This
section is here so that "implement the missing X" never requires reverse-
engineering the layout again.

**Directory layout.** Each module is one directory under `Engine/Runtime/`:

```
Engine/Runtime/<Area>/
    CMakeLists.txt
    Expose/uve/<namespace>/<name>_uve.h     public — anything may include this
    Internal/<name>_uve.cpp                 private — implementation only
```

`Expose/` is on the target's public include path; `Internal/` never is. A type
that is not in `Expose/` cannot be referenced from another module, which is how
the layering is enforced rather than by convention alone.

**Naming.** Files are lower_snake_case with a `_uve.h` suffix; types are PascalCase with a
`UVE` suffix. The `UVE`
suffix is universal and is what keeps engine types distinguishable from standard
library and third-party names at call sites.

**CMake scaffold.** Every module's `CMakeLists.txt` is the same five lines:

```cmake
add_library(uve_<name> STATIC
    Internal/<file>_uve.cpp
    ...)
target_include_directories(uve_<name> PUBLIC Expose)
target_link_libraries(uve_<name> PUBLIC <dependencies>)
target_compile_features(uve_<name> PUBLIC cxx_std_20)
uve_set_warnings(uve_<name>)
```

`uve_set_warnings` applies the full set — `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion -Wnon-virtual-dtor -Woverloaded-virtual
-Wold-style-cast -Wcast-align -Werror`. There is no opt-out; new code compiles
clean under all of it or it does not compile.

A header-only type needs no `.cpp` and no entry in `add_library` —
`plane_uve.h` and `ray_uve.h` are the existing precedent.

**Registration.** Add one `add_subdirectory(Engine/Runtime/<Area>)` line to the
root `CMakeLists.txt`. That list is **dependency-ordered**: a module must appear
after everything it links. The root file's own comment records the rule that
`add_subdirectory` lines exist "only for modules that already exist", which is why
several directories in the tree are present with no build entry.

**Tests.** There are four runtime test binaries, wired in `Test/CMakeLists.txt`:
`uve_core_tests`, `uve_integration_tests`, `uve_engine_tests`, `uve_editor_tests`.
Add the new test `.cpp` to whichever matches the layer, and link the module.
Foundation types belong in `uve_core_tests`.

---

# Part I — Foundation

The foundation is the thinnest part of this engine. `Core/` declares **58 public
types** in total; the scripting module alone declares 134. That imbalance is the
single most useful fact in this document: the layer everything depends on is the
layer that has had the least work.

## 1. Math — `Engine/Runtime/Core/Math`, namespace `UVE::Math`

Eight types, all value types, all documented "safe to copy/pass freely, no shared
state". Most of the surface is `constexpr`, using a hand-rolled finite check
rather than `std::isfinite` so it stays usable in constant expressions.

**A convention worth knowing before reading any of it:** fallible operations are
uniformly `bool Try<Name>UVE(in…, out&)` rather than throwing or returning a sentinel.
Normalising a zero vector, inverting a singular matrix and converting a
degenerate quaternion all return `false` and leave the out-parameter untouched.

### What exists

- `[x]` **`Vector3UVE`** — `vector3_uve.h`. Three floats; the position, direction
  and scale primitive of the whole engine. Real surface: `DotUVE`, `CrossUVE`,
  `LengthUVE`, `LengthSquaredUVE`, `TryNormalizeUVE`, `IsFiniteUVE`, arithmetic
  and comparison operators. This is the one math type that is genuinely complete.
- `[~]` **`Vector2UVE`** — `vector2_uve.h`. Two floats, and **only** `+`, `-`,
  `==`, `!=`. No dot, no length, no normalise, no scalar multiply. Anything doing
  2D maths — the UI layer, touch input, texture coordinates — either promotes to
  `Vector3UVE` or hand-rolls the operation. Completing this is nearly free and
  removes a class of duplicated code.
- `[x]` **`QuaternionUVE`** — `quaternion_uve.h`. Rotation, identity `(0,0,0,1)`.
  Fuller than it first appears: `TrySlerpUVE`, `TryMakeEulerUVE` /
  `TryToEulerUVE`, the ordered variants with an explicit `EulerOrderUVE`, and
  `TryMakeAxisAngleUVE` / `TryToAxisAngleUVE`. *(An earlier survey recorded slerp
  as missing; it is present at `quaternion_uve.h:115`. Corrected here.)*
- `[x]` **`Matrix4x4UVE`** — `matrix4x4_uve.h`. Column-vector convention,
  Y-up NDC. `ComposeTrsUVE`, `TransformPointUVE`, `TryInverseUVE`, `TransposeUVE`,
  `operator*`, plus projection and view builders. The Vulkan Y-flip is explicitly
  deferred and documented as such.
- `[x]` **`AabbUVE`** — `aabb_uve.h`. Axis-aligned box; mesh bounds, culling, and
  collision. Ships with `PenetrationUVE` (minimum-translation overlap),
  `RayHitUVE`, `SweptAabbHitUVE`, `IntersectRayUVE` (slab method),
  `SweepAabbUVE`, `TransformUVE`, `UnionUVE`. The ray/AABB test here is reused by
  the physics raycast, the shape casts, and the editor's click-to-select picker —
  a good example of a foundation type earning its keep.
- `[x]` **`PlaneUVE`**, **`FrustumUVE`** — `plane_uve.h`, `frustum_uve.h`. Six
  inward-facing planes plus a signed-distance query; the culling primitive.
  Header-only, no `.cpp`.
- `[x]` **`RayUVE`** — `ray_uve.h`. Origin + direction. Unit direction is
  documented as expected but **not enforced**; a non-unit direction still gives a
  correct hit/miss, but reported distances are then in units of the direction's
  own length. Worth knowing before trusting a distance.

### What is missing, and what it costs

- `[ ]` **`Vector4UVE`** — no homogeneous coordinate type, and no RGBA vector.
  Shader-facing code that needs a `vec4` assembles it from loose floats.
- `[ ]` **`Matrix3x3UVE`** — so there is no normal matrix and no pure-rotation
  matrix type. The renderer computes `transpose(inverse(model))` as a full 4×4 and
  uses its upper-left corner, which works but costs more than it needs to and
  makes the intent unclear at the call site.
- `[ ]` **A TRS value type** — this is the most conspicuous gap. TRS exists only
  as three loose fields on `TransformComponentUVE`, so there is no composable
  transform *value*. Every place that wants to combine two transforms, invert one,
  or pass one around does it as three separate members plus a matrix conversion. A
  value type with compose / inverse / transform-point would simplify the scene
  graph, the prefab system, the gizmo drag path and the physics interpolation cache
  at once. **Name it something other than `TransformUVE`**: that name is already
  taken by `AabbUVE::TransformUVE(matrix)`, a member function on the bounds type.
  `TrsUVE` avoids the collision.
- `[ ]` **`ColorUVE`** — nothing named `color` exists anywhere in the tree. Colours
  are ad-hoc float triples on `PrimitiveMeshComponentUVE`, `LightComponentUVE`,
  `MaterialAssetUVE` and the UI components, with no shared clamping, no linear/sRGB
  distinction and no conversion helpers. Given the renderer is HDR internally and
  tone-maps on output, the absence of an explicit linear-vs-display colour type is
  a real correctness hazard, not just a tidiness one.
- `[ ]` **`RectUVE` / `Rect2UVE` / `RectIntUVE`** — nothing named `rect` exists,
  despite a full UI component set. `UIImageComponentUVE` carries a bare
  `sizePixels`; `ViewportRectUVE` in the RHI is a one-off re-invention of the same
  idea scoped to render passes.
- `[ ]` **Integer vectors** (`Vector2iUVE`, `Vector3iUVE`) — needed for pixel
  coordinates, grid cells and texture dimensions, all of which currently use loose
  `uint32_t` pairs or floats.
- `[ ]` **A scalar-utility header** — there is no `Pi`, no `DegToRad`/`RadToDeg`,
  no `LerpUVE`, `ClampUVE`, `SmoothStepUVE` or `ApproximatelyEqualUVE` anywhere in
  `Core/Math`. Every call site re-derives them; the rotate-gizmo code, the
  animation contracts and the physics solver each carry their own copy of the same
  three lines. This is the cheapest high-value item in this document.
- `[ ]` **`SphereUVE` and an OBB type** — both named as deferred inside
  `aabb_uve.h` itself. Their absence is why the physics narrow phase carries
  sphere and oriented-box geometry as loose parameters instead of shapes.
- `[ ]` **A random-number source** — no `RandomUVE`, no PCG/xorshift.
  `ParticleEmissionUVE` carries a `seed` field with no engine-owned generator to
  consume it, so particle determinism is currently the caller's problem.
- `[ ]` **Curves / easing / noise** — no spline, no curve asset, no easing set, no
  value or gradient noise. Animation blending, camera transitions and procedural
  placement all want these.
- `[ ]` **SIMD** — no alignment attributes and no vectorised path on any type,
  documented as deferred.

## 2. Containers — `Engine/Runtime/Core/Containers`

`[ ]` **The directory exists and contains zero code files.** So do
`Core/Types`, `Core/Strings` and `Core/Delegates`. There is no engine-owned
container of any kind.

This is the largest structural hole in the foundation, and it gets worse with
time rather than staying constant: every system written before a container layer
exists grows around `std::vector` and `std::unordered_map`, and each one is a call
site that has to be revisited later. That is why this sits at Tier 0 of the
roadmap in Part V.

What a runtime of this shape needs, and what each unblocks:

- `[ ]` **`FixedArrayUVE<T, N>`** — fixed-capacity, stack-allocated, size tracked
  separately from capacity. The workhorse for bounded per-frame data. The engine
  already has many bounded limits expressed as raw `std::array` plus a manual
  count (`LightListUVE`, the frame-task graph's 256-task cap, the diagnostics
  capture caps); each is the same pattern rewritten.
- `[ ]` **`SmallVectorUVE<T, N>`** — inline storage for the first `N`, heap
  spill beyond. The single highest-impact container for a game engine, because the
  overwhelming majority of per-entity and per-frame lists are small and currently
  each one heap-allocates.
- `[ ]` **`SparseSetUVE`** — dense array plus sparse index, O(1) insert/remove with
  contiguous iteration. This is the canonical ECS storage structure; the archetype
  storage in `Entity/Internal/` solves the same problem its own way and could not
  reuse a shared one if it wanted to.
- `[ ]` **`HandleTableUVE<T>`** — generational slot map. The engine already has
  **three independent hand-rolled versions** of this idea: `EntityUVE`
  (index + generation), `VoiceHandleUVE` (generational voice id), and
  `ResourceHandleUVE<Tag>` (phantom-tagged u32). They are each correct; they are
  also each a separate implementation of the same primitive.
- `[ ]` **`RingBufferUVE<T>`** — bounded FIFO. `MemorySinkUVE` in the logger
  already implements one privately for its recent-message ring.
- `[ ]` **`BitSetUVE`** — fixed-width bit operations for layer masks and archetype
  signatures. `ArchetypeSignatureUVE` (private, `Entity/Internal/`) is exactly this.
- `[ ]` **`StringIdUVE` / interned name** — a hashed, comparable, cheap-to-copy
  name. Type ids in `TypeMetadataEntryUVE` are raw `std::string`, compared by
  value; every reflected property lookup is a string compare. An interned id turns
  those into integer compares and makes name-keyed maps cheap.
- `[ ]` **`SpanUVE<T>`** — non-owning view. `std::span` covers this in C++20; what
  is missing is the convention of using it, since most interfaces here take
  `const std::vector<T>&` and therefore cannot accept a subrange or a fixed array.

## 3. Memory — `Engine/Runtime/Core/Memory`, namespace `UVE::Memory`

Real allocators with a genuinely good tracking design, undermined by one missing
adaptor.

- `[x]` **`IAllocatorUVE`** — base interface. `AllocateUVE`/`DeallocateUVE` carry
  source file and line the same way the logging macros do, so every allocation is
  attributable without a debugger.
- `[x]` **`HeapAllocatorUVE`** — aligned malloc/free with outstanding-allocation
  bookkeeping.
- `[x]` **`PoolAllocatorUVE`** — fixed block size, fixed capacity, free list.
  Asserts on exhaustion; never grows. Correct for a pool, and the assert is the
  documented contract rather than an oversight.
- `[x]` **`StackAllocatorUVE`** + **`StackMarkerUVE`** — LIFO bump allocator with
  marker rewind. The marker is **owner-tagged**, so a marker taken from one stack
  cannot be used to rewind a different one — a small design detail that turns a
  whole class of bug into an assert.
- `[x]` **`MemoryManagerUVE`** — mutex-guarded tracker, stats and leak reporting.
  Allocation ids are manager-assigned rather than pointer-derived, specifically so
  leak reports survive pointer reuse.
- `[x]` **`ConstructUVE<T>` / `DestroyUVE<T>`** — the sanctioned placement-new and
  destroy-through-an-allocator pair, used via the `UVE_CONSTRUCT` macro.

Thread-safety is explicit and consistent: **the allocators are not thread-safe and
say so**; only `MemoryManagerUVE` is.

- `[ ]` **An STL-compatible allocator adaptor (`StdAllocatorUVE<T>`)** — and this
  is the problem. Without it, no `std::vector`, `std::string` or `std::unordered_map`
  in the engine can allocate through any of the allocators above. The allocators
  are real, tested, and used by almost nothing. Pairing this with the container
  layer in Part I §2 is what turns the memory module from infrastructure into
  something the engine actually runs on.
- `[ ]` **A frame/linear arena distinct from the stack allocator** — the standard
  "allocate freely during a frame, reset the pointer at end of frame" allocator.
  `StackAllocatorUVE` is close but its LIFO discipline is stricter than a frame
  arena needs.
- `[ ]` **Smart pointers paired with the allocator API** — no `UniquePtrUVE` or
  intrusive ref-counted pointer, so callers must hand-pair `ConstructUVE` with a
  matching `DestroyUVE` and get the lifetime right themselves.
- `[ ]` **A per-thread or thread-safe allocator wrapper** — every allocator is
  single-threaded by contract, yet `IMemoryTrackerUVE` explicitly anticipates
  worker-thread allocation. Nothing bridges that gap today.

## 4. Threading and scheduling — `Core/Threading`, `Core/Scheduling`

**This section corrects a common assumption: the engine does have a job system.**

- `[x]` **`ThreadPoolUVE`** / **`IThreadPoolUVE`** — worker pool. The mutex is
  never held while a job runs, so a job may submit nested jobs without deadlocking
  — stated as a hard requirement on any implementation.
- `[x]` **`JobCounterUVE`** — fan-out/fan-in counter with a blocking wait.
- `[x]` **`JobGraphUVE`** — single-use dependency DAG that cascades submissions as
  dependencies clear. `AddDependencyUVE` runs a reachability check and **returns
  false on a cycle** rather than asserting. Both this and `JobCounterUVE` are
  non-copyable *and* non-movable, because in-flight jobs capture `this`; address
  stability is a documented contract, not an accident.
- `[x]` **`FrameSchedulerUVE`** + **`FrameTaskGraphUVE`** (`Core/Scheduling`) — a
  second, higher-level graph above `JobGraphUVE`: tasks are tagged with a
  `FrameTaskDomainUVE` (Animation, ECS, RenderPreparation, Physics, Streaming,
  Audio, Assets, Scripting, Editor), named for profiling, capacity-capped, and
  validated before execution with result codes instead of asserts.

`[~]` **The capability is built and gameplay does not use it.** The entire
`Update()` / `LateUpdate()` chain in `EngineCoreUVE` is single-threaded main-thread
code. The job graph is used by asset import and shader preprocessing only. Nothing
is broken; the parallelism is simply not claimed yet. Making a system parallel is
therefore a wiring problem, not a missing-infrastructure problem — which is a much
better place to be, and worth knowing before anyone proposes building a job system.

Missing primitives:

- `[ ]` **`ParallelForUVE`** over the pool — every caller hand-rolls submit plus
  `JobCounterUVE`.
- `[ ]` **Named synchronisation primitives** — no `SpinLockUVE`, `RWLockUVE` or
  semaphore type.
- `[ ]` **A `ThreadIdUVE` type** — `Core/Diagnostics` needs thread ids and takes
  them as raw `uint64` parameters.
- `[ ]` **Thread naming / affinity / priority** — nothing, which also means worker
  threads are anonymous in any external profiler.

## 5. Utilities and diagnostics

- `[x]` **`TimerUVE`** (`Core/Utilities`) — steady-clock frame timing with a
  fixed-step accumulator returning `FixedStepResultUVE` (step count + interpolation
  alpha). Owned exclusively by the frame-pipeline thread.
- `[~]` **`binary_buffer_uve.h`** (`Core/Utilities`) — `AppendBytes/Uint32/Uint64/
  Float` and bounds-checked readers over `std::vector<std::byte>`. Readers are
  `[[nodiscard]] bool` and leave the offset untouched on failure, which is the
  right shape. **But it writes in host byte order with no endian handling**, and
  every save file, asset envelope and pack format is built on it. Today that is
  invisible because everything is little-endian; it becomes a format-compatibility
  break the first time a big-endian target or a cross-machine save appears.
  There is also no `AppendString`/`AppendBool`/`AppendDouble`, despite the config
  layer storing doubles and the save layer storing strings.
- `[x]` **Logging** (`Core/Logging`) — `LoggerUVE` with `ConsoleSinkUVE`,
  `FileSinkUVE` and `MemorySinkUVE` (the editor console's ring), severity-ordered
  `LogLevelUVE`, and `UVE_LOG`/`UVE_INFO`/`UVE_WARN`/`UVE_ERROR`/`UVE_FATAL`
  macros that capture source location automatically. A process-global active-logger
  atomic makes logging safe before engine init and after shutdown. `UVE_ASSERT`'s
  release form evaluates only the *type* of its expression, so call sites never
  need `#if` guards around it.
- `[x]` **`ProfilerCaptureUVE`** (`Core/Diagnostics`) — bounded span/counter/
  breadcrumb capture with **explicit dropped-record counters** rather than
  unbounded growth or silent loss. Timestamps are caller-supplied nanoseconds; the
  module owns no clock, which makes captures deterministic and testable.
- `[ ]` **Hashing** — no FNV/xxHash/`HashCombineUVE`. `AssetContentFingerprintUVE`
  implements its own content hash privately; `std::hash` specialisations are
  hand-written per type.
- `[ ]` **A UUID/GUID type** — `AssetGuidUVE` is asset-specific; nothing general.
- `[ ]` **String utilities**, **`ScopeGuardUVE`**, **`NonCopyableUVE`**, and an
  **enum-flags helper** (`TypeMetadataMethodUVE::flags` is a raw `uint32`).
- `[~]` **`Core/Scheduling` and `Core/Diagnostics` carry no doc comments at all** —
  the only two modules in this layer without them, which is jarring against a
  codebase where every other header explains its own trade-offs.

## 6. Reflection — `Engine/Runtime/Object`, namespace `UVE::Core`

- `[x]` **`TypeMetadataRegistryUVE`** — bounded (≤256 types, ≤128 members each)
  registry of `TypeMetadataEntryUVE`, each carrying a kind (Component / Resource /
  VisualScriptNode / InspectorTarget / Other), display name, version, and property
  and method lists. Snapshots are stamped with a **generation counter** so an
  inspector can detect changes cheaply.
- `[x]` The design is **data-driven and opt-in** — no base class, no intrusive
  macro, no RTTI dependency. Properties are raw function pointers with a documented
  owner/value contract, wrapped by type-safe `GetPropertyValueUVE` /
  `SetPropertyValueUVE` free functions and built by `MakePropertyUVE()` from a
  pointer-to-member.
- `[ ]` **There is no factory.** `TypeMetadataEntryUVE` has properties and methods
  but **no constructor/create function pointer**, so a type cannot be instantiated
  from its id. That single omission is what blocks generic deserialization (every
  component's read/write is hand-written in `SceneSerializerUVE` instead) and an
  editor "add component by type name" flow. It is a small addition with a large
  payoff and is Tier 1 in Part V.
- `[ ]` **No `TypeIdUVE` strong type** — type ids are raw `std::string`.
- `[ ]` **No object/handle layer** — no base object, no reference counting, no
  registry of live instances. The module is metadata only, which is a legitimate
  choice; it is recorded here so nobody goes looking for the rest of it.

---

# Part II — Resources

"Resource" means two different things in this engine, and keeping them apart makes
the rest of this section readable:

- **A GPU resource** — a buffer, texture, shader or pipeline living in the RHI,
  referred to by an opaque handle, owned by the render device.
- **An asset** — a mesh, texture, material, audio clip or data table living on
  disk, identified by a GUID, loaded asynchronously, reference-counted.

They meet at the renderer, which resolves an asset into GPU resources and caches
the result. Note a naming collision worth knowing about:
`Render::ResourceHandleUVE<Tag>` (the RHI handle template) and
`Asset::ResourceHandleUVE` (a node in the dependency graph) are unrelated types
with the same name, both public.

## 7. The asset lifecycle

`[x]` The chain is **GUID → database → handle → record → ref-count**:

1. `[x]` **`AssetGuidUVE`** (`asset_guid_uve.h`) — path-independent identity, with
   a `std::hash` specialisation. Scenes reference assets by GUID so that moving a
   file on disk does not break a scene.
2. `[x]` **`AssetDatabaseUVE`** (`asset_database_uve.h`) — GUID → path registry,
   JSON-backed, with `AssetRecordUVE` entries.
3. `[x]` **`AssetManagerUVE`** (`asset_manager_uve.h`) — the loader. A loader
   function is registered per C++ type; `LoadUVE<T>(guid, db)` runs it on the
   thread pool and publishes `AssetLoadCompletedEventUVE` when done.
4. `[x]` **`AssetHandleUVE`** (`asset_handle_uve.h`) — ref-counted typed handle.
   `AssetLoadStateUVE` reports where a record is in its lifecycle.
5. `[x]` **`IFileSystemUVE` / `FileSystemUVE`** — a real VFS that mounts either a
   directory or a `.uvebundle`, so the same virtual path resolves in development
   and in a packaged build.

Supporting infrastructure, all real: `AssetImporterUVE` (per-extension registry
with per-type settings), `AssetImportQueueUVE` (deterministic main-thread
scheduler, one job per tick, with a full diagnostic taxonomy),
`DerivedArtifactCacheUVE` (proves an import result still matches source bytes plus
settings version), `ProjectFileIndexUVE` and `ProjectChangeWatcherUVE` (the asset
browser's tree and change journal), `ResourceDependencyGraphUVE` (BFS invalidation
plan for reimport), `AssetBundleUVE` (`.uvebundle` pack/unpack), and
`UveFileHeaderUVE` — a universal envelope with an `AssetKindUVE` covering Scene,
Prefab, Blob, Bundle, Mesh, Texture, Shader, Material, Save, DataTable, Audio and
Animation.

`[~]` **`HotReloadUVE`** polls mtimes. There is no OS file-watcher backend, so
reload latency is the poll interval and a rename reads as remove-plus-create.

## 8. Asset types

| Asset | Holds | Importer | Status |
|---|---|---|---|
| `MeshAssetUVE` | `MeshVertexUVE` array (position, normal, uv, tangent, handedness), `uint32` indices, `AabbUVE localBounds`, skinning influences, joints | **OBJ** and **glTF/GLB**, plus a `.uvemodel` envelope | `[x]` |
| `TextureAssetUVE` | width, height, format, raw uncompressed pixels | **PNG, JPEG, BMP, TGA**, plus `.uvetex` | `[~]` no mips, no compression |
| `MaterialAssetUVE` | albedo colour, albedo/normal/AO texture GUIDs, metallic, roughness, emissive colour, shader GUIDs, transparency flag | **MTL**, plus `.uvemat` | `[~]` see below |
| `ShaderAssetUVE` | stage, source text, entry point — stored as-is, not compiled at asset level | source importer + `.uveshader` | `[x]` |
| `AudioAssetUVE` | channels, sample rate, interleaved normalized float samples | **WAV** | `[~]` WAV only |
| `AnimationClipAssetUVE` | clip id, duration, sampled TRS poses, timed string events | envelope only | `[~]` see below |
| `DataTableUVE` | typed schema'd rows (bool / int64 / double / string) | **CSV, TSV, JSON** | `[x]` the most built-out asset in the module |
| `BlobAssetUVE` | raw bytes, unparsed | — | `[x]` |

Three things about this table matter more than the rest:

- `[~]` **`AnimationClipAssetUVE` stores a single TRS track.** `MeshAssetUVE` in
  the same module carries a `MeshJointUVE` array with inverse bind matrices, and
  the clip format cannot address it — there are no per-joint channels. So the two
  halves of a skeletal animation pipeline exist in the same directory and cannot
  be connected. This is the largest coherence gap in the asset layer, and it is
  why `Skeleton3D`, `BoneAttachment3D`, `AnimationPlayer` and `AnimationTree` are
  all authored-data-only nodes.
- `[~]` **`MaterialAssetUVE` has three of the six PBR texture slots** its own doc
  comment describes: albedo, normal and AO are present; metallic, roughness and
  emissive texture GUIDs are not. Scalar metallic and roughness exist, so the
  material is usable — it just cannot be textured per-pixel.
- `[x]` **A WAV importer does exist**, in `Engine/Runtime/Audio/` rather than in
  `Asset/` — `RegisterWavImporterUVE`, registered from `engine_core_uve.cpp:260`.
  *(An earlier survey reported no audio importer at all, having searched only
  `Asset/Internal/`. Corrected here.)* OGG, MP3 and FLAC are genuinely absent.

Also missing: `[ ]` an FBX importer, `[ ]` any animation importer (glTF is parsed
for meshes only), `[ ]` a skeleton asset separate from the mesh, and `[ ]` a font
asset — the UI's `UIFontAtlasUVE` builds its atlas from an embedded font at
runtime with no asset behind it.

## 9. GPU resources — the RHI

### Handles

`[x]` The handle design is the strongest part of the RHI. One template,
`ResourceHandleUVE<Tag>` (`resource_handle_uve.h`), is a phantom-tagged `uint32`
that is type-safe, comparable and hashable. Four domains instantiate it —
`BufferHandleUVE`, `TextureHandleUVE`, `ShaderHandleUVE`, `PipelineHandleUVE` —
each with its own `kInvalid…` sentinel. A buffer handle cannot be passed where a
texture handle is expected; that is a compile error, not a runtime one.

Two deliberate overloads worth noting: `PipelineHandleUVE` is a **shared domain
for graphics and compute** pipelines (the backend picks the bind point), and
`kInvalidTextureHandleUVE` doubles as "the default framebuffer" when used as a
render-pass attachment.

### Descriptors — where the RHI is thin

| Descriptor | Carries | Gap |
|---|---|---|
| `BufferDescUVE` | size, usage (`Vertex`/`Index`/`Uniform`/`Storage`/`IndirectStorage`) | no memory/CPU-access/lifetime hint |
| `TextureDescUVE` | width, height, format, mipLevels | **no array layers, no depth, no cubemap, no sample count, no usage flags** |
| `TextureFormatUVE` | `RGBA8Unorm`, `RGBA16Float`, `Depth32Float` | three formats; no sRGB, no compressed, no single/two-channel |
| `ShaderDescUVE` | stage, source, entry point | `Geometry` compiles but has no pipeline slot |
| `PipelineDescUVE` | vertex+fragment shader, vertex layout, stride, topology, `depthTestEnabled`, `depthWriteEnabled`, blend mode | **no rasterizer state at all** — see below |
| `RenderPassDescUVE` | **one** colour attachment, one depth attachment, load ops, clear values, optional viewport override | single colour attachment; `LoadOpUVE` with no `StoreOp` |
| `PrimitiveTopologyUVE` | `Triangles` | no lines, points or strips |

Three consequences follow directly from that table and are worth stating plainly,
because each one blocks a whole feature:

- `[ ]` **No rasterizer state.** `PipelineDescUVE` has no cull mode, no winding
  order, no fill mode, no depth bias and **no depth-compare function**. Anything
  needing a comparison other than the fixed default has to reach around the
  abstraction — which is exactly what the editor's gizmo overlay does today,
  setting the GL depth function directly because the pipeline cannot express it.
  Shadow-map depth bias is unexpressible for the same reason.
- `[ ]` **No multiple render targets.** One colour attachment means a deferred or
  G-buffer path cannot be described at all, regardless of what the backends can do.
- `[ ]` **No texture dimensionality.** No cubemaps means no environment maps and
  therefore no image-based lighting; no array layers means cascade shadow maps
  cannot use a texture array.

Also absent: `[ ]` a sampler object (filtering is hardcoded — the Vulkan backend
documents "one device-owned fixed sampler, linear/clamp"), `[ ]` descriptor sets
or bind groups (binding is flat global slot indices), `[ ]` any synchronisation
primitive (no fence, semaphore, barrier or timeline — `SubmitUVE` + `PresentUVE`
is the entire model), `[ ]` a swapchain/surface abstraction, `[ ]` stencil state,
`[ ]` mip generation, `[ ]` copy/blit operations (only `UpdateBufferUVE` and
`ReadbackBufferUVE`), and `[ ]` timestamp or occlusion queries — which is precisely
why the occlusion-culling note elsewhere says its payoff cannot be demonstrated.

### Command recording and backends

- `[x]` **`ICommandBufferUVE`** — a **retained** command buffer. Recorded commands
  become `RecordedCommandUVE`, a variant over ~16 command structs, which backends
  replay. One thread records; submit once.
- `[x]` **`GlRenderDeviceUVE`** — the most complete backend. Full resource
  creation, FBO render passes, program-binary cache, indirect draw, SSBO, dispatch.
- `[~]` **`VulkanRenderDeviceUVE`** — real but milestone-sliced, and unusually
  honest about it: instance/device/swapchain/present, buffers, SPIR-V, pipelines,
  reflection and dynamic-rendering offscreen targets all work, while >1 descriptor
  set, load-op semantics beyond clear, per-op depth compare, device-local staging
  and sampling `Depth32Float` **warn and fail loudly rather than silently
  degrading**. On pre-1.3 devices offscreen passes are skipped entirely.
- `[x]` **`NullRenderDeviceUVE`** — a deliberate no-op that validates every call
  and records the exact RHI call sequence through a spy command buffer. It is what
  CI runs against, which is worth remembering: a green test suite proves the call
  sequence is right, not that pixels are.

## 10. Render features — real versus not

`[x]` Real, each with diagnostics counters proving it ran: cascaded shadow maps,
bloom, SSAO, tone mapping, instancing, CPU frustum culling (clustered), GPU
frustum culling, GPU indirect culling, distance/LOD culling, GPU skinning, GPU
particle simulation.

`[~]` **Render graph** — `RenderGraphUVE` exists but imports externally-owned
textures and executes passes **in insertion order**, with no culling, aliasing,
barriers, transient allocation or reordering. It is a validation and sequencing
layer, not a frame graph.

`[ ]` Not real: occlusion culling (`Occluder3D` data is consumed by the
eligibility path but no occlusion test runs), image-based lighting, texture
compression and mipmaps, geometry shaders, and the particle GPU upload path —
`ParticleRenderBridgeUVE` and `ParticleDrawRecorderUVE` both state in their own
comments that they allocate no GPU buffers and submit nothing.

`[ ]` Missing renderer-level types: no material system (material→program binding
is inlined in the renderer), no 2D renderer or sprite batcher (`basic_2d.glsl`
exists with nothing consuming it), **no debug/line renderer** — nothing in the
engine can draw a bounding box or a frustum, which makes every spatial bug harder
than it needs to be — no text renderer, no skybox, no reflection probe capture, no
decal system, no terrain.

---

# Part III — Components

Components are the authored data of the ECS. Two conventions hold across all of
them and are worth stating once:

- **They are authored data, not runtime state.** Where a component omits live
  state, its header says why: excluding it keeps serialization deterministic.
  `UIButtonComponentUVE`'s `wasClickedThisFrame` is the one acknowledged
  exception.
- **Enums are explicitly `uint8_t` and append-only**, so that adding a case never
  invalidates a saved scene.

One inconsistency to know: every component lives in `namespace UVE::Scene`, not
`UVE::Component` — the directory name and the namespace disagree.

### Identity and hierarchy

| Component | Written by | Read by |
|---|---|---|
| `[x]` `EntityUVE` (`entity_uve.h`) | — | everything. Index + generation; no data, no behaviour, no manager binding. Deliberately lives in Component, not Entity, so component headers do not depend on the manager |
| `[x]` `ComponentTypeInfoUVE` | registration | archetype storage. Type-erased size/align/construct/move/destroy vtable |
| `[x]` `TransformComponentUVE` | authoring, gizmo drag, scripts | `SceneGraphUVE`. Local TRS plus `RotationEditModeUVE` recording whether Euler or quaternion is the source of truth |
| `[~]` `WorldTransformComponentUVE` | **`SceneGraphUVE` only** | renderer, physics, audio, picking. Cached world TRS + dirty flag. The single-writer rule is documented but **not enforced** |
| `[x]` `HierarchyComponentUVE` | scene graph, reparenting | scene graph. **Parent link only** — children are found by linear scan, so `GetChildrenUVE` is O(n) in entity count |
| `[x]` `NameComponentUVE`, `VisibilityComponentUVE` | authoring | editor, renderer |

### Rendering

`[x]` `CameraComponentUVE` (FOV/near/far; projection policy owned by
`CameraSystemUVE`) · `[x]` `LightComponentUVE` + `LightTypeUVE`
(directional/point/spot; position and direction derived from the world transform)
· `[x]` `MeshComponentUVE` (mesh + material GUID pair; invalid GUID means skip)
· `[x]` `PrimitiveMeshComponentUVE` + `PrimitiveMeshKindUVE` (box/sphere/plane
plus a bounded linear-RGB base colour — explicitly *not* a material).

### Physics

`[x]` `ColliderComponentUVE` + `ColliderShapeTypeUVE` (box/sphere/capsule; layout
append-only, capsule height is total end-to-end including caps) · `[x]`
`RigidBodyComponentUVE` (mass, velocity, kinematic flag — **a collider without one
means static world geometry**, an overload worth knowing) · `[x]`
`AreaComponentUVE` (non-solid trigger volume; never enters collision resolution)
· `[x]` `CharacterControllerComponentUVE` · `[~]`
`PhysicsInterpolationComponentUVE` (previous/current pose cache for render
smoothing; optional per entity).

### Audio, animation, scripting, UI

`[x]` `AudioSourceComponentUVE` + `AudioAttenuationCurveUVE` · `[~]`
`AnimationPlayerComponentUVE` (records which clip is attached; nothing samples it)
· `[~]` `ParticleEmitterComponentUVE` (authored parameters; the CPU runtime
consumes them, the GPU upload path does not) · `[x]` `ScriptComponentUVE` · `[x]`
`CanvasComponentUVE`, `UITextComponentUVE`, `UIImageComponentUVE`,
`UIButtonComponentUVE`.

### Editor and prefabs

`[x]` `EditorDescriptionComponentUVE` · `[x]` `EditorInternalEntityComponentUVE`
(empty tag marking editor-owned hidden entities — the viewport's camera proxy and
headlight carry it) · `[x]` `PrefabInstanceComponentUVE` with
`PrefabPropertyOverrideUVE`, `IPrefabOverrideTargetUVE` and the conflict-report
types.

### Missing components

- `[ ]` **A component registry** mapping type ↔ name/id for serialization. Every
  component's read and write is hand-written in `SceneSerializerUVE`; adding a
  component means editing the serializer. This is the same gap as the missing
  reflection factory in Part I §6 — fix one and this becomes mechanical.
- `[ ]` **Tag / layer / mask component** — physics queries cannot filter by layer;
  `RaycastQueryUVE` excludes exactly one entity.
- `[ ]` **An enabled/disabled state** distinct from `VisibilityComponentUVE`,
  which is render-only. There is no way to disable an entity's simulation.
- `[ ]` **A children index** to pair with `HierarchyComponentUVE`, removing the
  linear scan.
- `[ ]` **Entity command buffer** for deferred structural changes — structural
  changes invalidate references, and nothing provides a safe deferred path.
- `[ ]` **Query/view types** — only `ForEachUVE` exists.

---

# Part IV — Systems, in one line each

Detail for these lives in `ROADMAP.md`; this is an index, and a note of what is
driven by nothing.

- `[x]` **Entity/Scene** — archetype ECS (`EntityManagerUVE`, PIMPL), stateless
  `SceneGraphUVE`, `SceneSerializerUVE` (explicit-roots, never implicit
  whole-world), `PrefabSystemUVE`, `ParticleRuntimeUVE`.
- `[~]` **`WorldUVE`** — owns one entity manager and one scene graph; `TickUVE`
  bumps counters and propagates transforms. No system registration, no multi-scene,
  no scene-unload. `[ ]` **There is no `SceneUVE` type at all** — no object
  representing a loaded level.
- `[x]` **Physics** — box/sphere/capsule shapes, raycast, sphere/box/capsule casts,
  area overlap, character controller with CCD, fixed-step integration with
  positional resolution, island-partitioned distance and hinge constraints.
  `[ ]` No convex/mesh/heightfield shapes, no friction or restitution impulse model
  despite `PhysicsMaterialUVE` carrying the fields, no body sleeping, no layer
  filtering.
- `[~]` **Animation** — **the entire module is dead code at runtime.** Clip, state
  machine, blend tree, pose buffer and time contracts all exist and are tested;
  `EngineCoreUVE` never calls any of them. There is no clip sampler and no skinning
  pass. See Part II §8 for why the asset format blocks it.
- `[x]` **Audio** — voice allocation, listener pose, attenuation, real miniaudio
  backend plus a recording null device. `[~]` `AudioMixerGroupUVE` (the whole bus
  graph), the PCM streaming path and the gain-ramp scheduler have no consumers.
- `[x]` **Input** — keyboard, mouse, gamepad, action bindings, touch and gesture
  recognition. `[~]` Gesture output is computed and read by nobody; mobile input
  has no platform backend pushing snapshots.
- `[x]` **UI** — `UIRuntimeUVE` hit-tests buttons against real input and emits a
  quad batch; font atlas via stb_truetype. `[ ]` No layout, no clipping, no focus
  or keyboard navigation, no widgets beyond text/image/button.
- `[x]` **Scripting** — the most built-out area: graph model, node registry,
  validator, VM with flow latches/gates/loops/delays, debugger, canvas model,
  persistence, hot-reload manager. `[~]` The IR and bytecode pipeline is a real
  encoder/decoder that **nothing executes** — the runtime walks the graph through
  the VM instead. `[~]` `ScriptHotReloadManagerUVE` has no consumer.
- `[~]` **Network** — one header of reliable-packet-window value logic with **no
  consumer anywhere**. No socket, no transport, no replication.
- `[~]` **Plugins** — manifest validation and an in-memory registry that **never
  calls `dlopen`**. No plugin is ever loaded.
- `[x]` **Save / Pack / ProjectCheck** — slot-based saves with atomic temp-file
  rename, a bounded migration chain, RLE compression; project packaging and launch;
  a standalone project checker CLI.
- `[~]` **Platform** — the name oversells it: compiler macros plus the project
  package codec. There is no platform abstraction layer for filesystem, process,
  time or dynamic libraries.

**Empty module directories** (README only, zero code): `Core/Containers`,
`Core/Types`, `Core/Strings`, `Core/Delegates`, `FileSystem`, `Gameplay`,
`Networking`, `Renderer`, `Serialization`, `VFX`, `Nodes/2D`, `Nodes/AI`. Two of
those are placeholders guarding against duplication — `Renderer/README.md`
explicitly says not to start a second renderer there — and the rest are
unimplemented intent.

---

# Part V — Implementation roadmap

Ordered by dependency, not by appeal. Each tier assumes the ones above it.

## Tier 0 — the layer everything waits on

These are first because **the cost of adding them rises with every system written
without them.** A container layer introduced late means revisiting every call site
that grew around `std::vector`; an allocator adaptor introduced late means those
call sites are already allocating from the wrong place.

| # | Item | Module | Unblocks | Done when |
|---|---|---|---|---|
| 0.1 | Scalar utility header — `Pi`, `DegToRad`, `RadToDeg`, `LerpUVE`, `ClampUVE`, `SmoothStepUVE`, `ApproximatelyEqualUVE` | `Core/Math` | removes duplicated copies in gizmo, animation and physics code | the three existing copies are deleted and call sites use the shared header |
| 0.2 | `FixedArrayUVE<T,N>`, `SmallVectorUVE<T,N>`, `SpanUVE` conventions | `Core/Containers` (new build entry) | every bounded list in the engine | `LightListUVE` and the frame-task graph caps are expressed with it |
| 0.3 | `StdAllocatorUVE<T>` adaptor | `Core/Memory` | makes the existing allocators reachable from any container | a `std::vector` in a test allocates through `PoolAllocatorUVE` and the tracker sees it |
| 0.4 | `StringIdUVE` interned name | `Core/Strings` | reflection lookups, name-keyed maps | `TypeMetadataEntryUVE` type ids are ids, not strings |
| 0.5 | `HandleTableUVE<T>` generational slot map | `Core/Containers` | replaces three hand-rolled versions | at least one of `VoiceHandleUVE` / entity slots is rebuilt on it, with tests unchanged |

## Tier 1 — completing the value layer

| # | Item | Module | Unblocks | Done when |
|---|---|---|---|---|
| 1.1 | `Vector4UVE`, `Matrix3x3UVE` | `Core/Math` | correct normal matrices; shader-facing vec4 | renderer's normal-matrix path uses `Matrix3x3UVE` |
| 1.2 | `TrsUVE` TRS value type with compose/inverse | `Core/Math` | scene graph, prefabs, gizmo drag, physics interpolation | at least the gizmo drag path composes transforms as values |
| 1.3 | `ColorUVE` with explicit linear/display distinction | `Core/Math` | removes ad-hoc float triples; makes the HDR path type-safe | `LightComponentUVE` and `MaterialAssetUVE` use it |
| 1.4 | `RectUVE`, integer vectors | `Core/Math` | UI layout, pixel coordinates | `UIImageComponentUVE` uses `RectUVE`; `ViewportRectUVE` is reconciled with it |
| 1.5 | Reflection **factory** pointer on `TypeMetadataEntryUVE` | `Object` | generic deserialization; editor add-by-type-name | one component round-trips through the serializer without hand-written code |
| 1.6 | Component registry (type ↔ name/id) | `Component` | serializer stops needing per-component edits | adding a component requires no `SceneSerializerUVE` change |
| 1.7 | Hashing utilities (`HashCombineUVE`, a named non-cryptographic hash) | `Core/Utilities` | replaces the private fingerprint hash and hand-written `std::hash` specialisations | `AssetContentFingerprintUVE` is built on it |
| 1.8 | Endian-explicit binary buffer read/write | `Core/Utilities` | makes every save and asset format portable | round-trip test asserts byte-identical output on a simulated byte-swap |

## Tier 2 — resource layer

| # | Item | Module | Unblocks | Done when |
|---|---|---|---|---|
| 2.1 | Rasterizer state on `PipelineDescUVE` — cull, winding, fill, depth bias, **depth-compare function** | `RHI/RHI` + all three backends | shadow bias; removes the editor's direct GL depth-function call | the gizmo overlay expresses its depth mode through the pipeline |
| 2.2 | Sampler object + descriptor | `RHI/RHI` + backends | point filtering, wrapping, anisotropy | a texture can be sampled point-filtered |
| 2.3 | Texture dimensionality — array layers, cubemaps | `RHI/RHI` + backends | image-based lighting; cascade arrays | a cubemap is created and sampled |
| 2.4 | Multiple render targets on `RenderPassDescUVE` | `RHI/RHI` + backends | any deferred/G-buffer path | a two-attachment pass records and replays |
| 2.5 | `StoreOpUVE` to pair with `LoadOpUVE` | `RHI/RHI` + backends | tiler efficiency; explicit resolve/discard | every pass declares both |
| 2.6 | Per-joint animation channels in `AnimationClipAssetUVE` + a skeleton asset | `Asset` | the entire skeletal animation pipeline, and four data-only nodes | a glTF skinned mesh animates |
| 2.7 | Remaining PBR texture slots on `MaterialAssetUVE` | `Asset` | full PBR materials | metallic/roughness/emissive maps render |
| 2.8 | Debug line/shape renderer | `RHI/RenderSystems` | visualising bounds, frusta, contacts — pays for itself on the first spatial bug | a bounding box can be drawn from one call |

## Tier 3 — systems that are blocked, not missing

Each of these has its infrastructure already built; what is missing is the
consumer. They are cheap relative to how large they look.

| # | Item | Blocked on | Note |
|---|---|---|---|
| 3.1 | Animation runtime — clip sampler, skinning pass, engine tick | 2.6 | The evaluators, pose buffers and time contracts already exist and are tested |
| 3.2 | Parallel gameplay systems | Tier 0 containers | `ThreadPoolUVE`, `JobGraphUVE` and `FrameSchedulerUVE` are built and unused by gameplay |
| 3.3 | Audio mixer routing | — | `AudioMixerGroupUVE` exists; voices simply do not route through it |
| 3.4 | Script bytecode execution | — | The IR and bytecode encoder/decoder exist; the VM walks the graph instead. Decide whether to execute the bytecode or delete the pipeline |
| 3.5 | Plugin host | — | Manifest validation exists; `dlopen`/`LoadLibrary` does not |
| 3.6 | Occlusion culling | 2.8 (to see it) and GPU queries | The data path and cost table are documented at the hook point |

## Tier 4 — genuinely new subsystems

Out of scope for this document beyond naming them; each needs its own design pass
and each has a `ROADMAP.md` section already: networking transport and replication,
navmesh and pathfinding, an AI layer, a gameplay framework (the reason
`Hitbox3D` cannot apply damage), a 2D pipeline, VFX authoring, terrain, and
localization.

---

## Keeping this document true

The same rule the other roadmaps state, repeated because the node roadmap shows
what happens when it lapses: **update this file in the change that moves an item,
not afterwards.** A catalogue that drifts from the code is worse than none,
because it is trusted.

At the time of writing, `SCENE_NODES_ROADMAP.md` lists eight nodes as
authored-data-only that now have real per-frame systems — SpringArm3D,
InteractionArea3D, LevelStreamer3D, WorldPartition3D, VisibilityRegion3D,
LODGroup3D, Occluder3D, and partially ReflectionProbe3D. That correction belongs
in that file and is not folded in here.
