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
target_compile_features(uve_<name> PUBLIC cxx_std_23)
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
types** in total, while the (since removed) node-graph scripting module alone declared
134. That imbalance was the single most useful fact in this document: the layer everything
depends on is the layer that has had the least work.

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
- `[x]` **`Vector2UVE`** — `vector2_uve.h`. Now carries the same dot/length/normalize/finite
  surface as `Vector3UVE` (`DotUVE` with the same double-precision overflow fallback;
  `NormalizeUVE` under the same caller-ensures-nonzero contract, not `TryNormalizeUVE`).
  (Correction: the earlier "only `+`, `-`, `==`, `!=`" claim was already stale — scalar `*`
  predates this change.) Compound/unary operators stay minimal per the header's own note.
  Tests extended in `Test/Math/vector2_uve_tests.cpp`. Verified: full suite green (1318 core + 1668 integration, 2026-10-09).
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

- `[x]` **`Vector4UVE`** — `vector4_uve.h` + `Internal/vector4_uve.cpp`, registered in
  `uve_math`. Mirrors `Vector3UVE` exactly (component-wise arithmetic, overflow-safe `DotUVE`,
  `Length`/`Normalize`/`ToString`/`IsFinite`); no `CrossUVE` (no 4D meaning). First in-tree user:
  the `matrix4x4_uve_tests.cpp` homogeneous-clip helper, whose local 4-float struct is deleted.
  Tests in `Test/Math/vector4_uve_tests.cpp`. Verified: full suite green (1668 integration + 1306 core, 2026-10-09).
- `[x]` **`Matrix3x3UVE`** — `matrix3x3_uve.h` + `Internal/matrix3x3_uve.cpp`, registered in
  `uve_math`. Identity, `operator*`, `TransposeUVE`, `TryInverseUVE` (a line-for-line 3x3 port of
  the 4x4 Gauss-Jordan, same pivot threshold), `ToMatrix3x3UVE`/`ToMatrix4x4UVE` conversions,
  `ToStringUVE`. The renderer's `ComputeNormalMatrixUVE` now computes in 3x3; both uniform call
  sites go through the new `ShaderProgramUVE::SetMatrix3x3UVE`, which embeds in a 4x4 for the
  mat4-only uniform chain (shaders consume `mat3(...)` of it, so the embedding is invisible).
  Tests in `Test/Math/matrix3x3_uve_tests.cpp`, including a 3x3-vs-legacy-4x4 normal-matrix
  equivalence test. Verified: full suite green (1668 integration + 1306 core, 2026-10-09).
- `[x]` **A TRS value type** — `trs_uve.h` + `Internal/trs_uve.cpp` (`Math::TrsUVE`,
  registered in `uve_math`): translation/rotation/scale value with `ComposeUVE`,
  `TransformPointUVE`/`TransformDirectionUVE`, exact inverse-application
  `TryInverseTransformPointUVE`/`TryInverseTransformDirectionUVE`, equality,
  `ToStringUVE`. No materialized inverse: rotation and non-uniform scale do not
  commute, so the inverse of a general TRS is not a TRS (the reparent path's
  refusal of non-uniformly-scaled rotated parents is the in-tree precedent). Four
  migrations: the gizmo drag world-to-local delta, the reparent keep-world position,
  the scene-graph parent/child composition (byte-identical expressions), and both
  bone-attachment transforms — all prior zero-scale cutoffs and guards kept
  verbatim at the call sites. The rotate-drag conjugation stays quaternion-level
  (already values; TRS would add nothing). Tests in `Test/Math/trs_uve_tests.cpp`,
  including associativity, a non-uniform-rotated round-trip, and a legacy-arithmetic
  pin. Verified: full suite green (1668 integration + 1306 core, 2026-10-09), with the associativity test corrected to the uniform-scale domain the math actually guarantees.
- `[x]` **`ColorUVE`** — `color_uve.h` + `Internal/color_uve.cpp` (`Math::ColorUVE`,
  registered in `uve_math`): linear-working-space RGB value with `r/g/b`
  channels, component-wise arithmetic, `IsFinite`/`Luminance` (Rec.709),
  `ToVector3UVE`/`FromVector3UVE` bridges, and the exact sRGB EOTF pair
  (`DisplayToLinearUVE`/`LinearToDisplayUVE`, IEC 61966-2-1, double-math,
  honest HDR extension past 1). Four migrations: `LightComponentUVE`,
  `LightEmitterComponentUVE`, `MaterialAssetUVE` (albedo + emissive), and
  `LightDataUVE` — JSON stays byte-stable (`[x,y,z]` scene arrays,
  `{"x","y","z"}` material keys; MTL Kd/Ke convert display→linear at import).
  New `kPropertyTypeLinearColorUVE` vocabulary with an inspector branch that
  transports `ColorUVE` and converts display↔linear at the picker edge.
  Primitive/Env/UI/text colours stay display-`Vector3UVE` by design. Tests in
  `Test/Math/color_uve_tests.cpp`, including sRGB round-trips and MTL-literal
  pins. Verified: full suite green (1668 integration + 1306 core, 2026-10-09).
- `[x]` **`RectUVE` / `RectIntUVE`** — `rect_uve.h` + `Internal/rect_uve.cpp` and
  `rect_int_uve.h` + `Internal/rect_int_uve.cpp` (`Math::RectUVE`, `Math::RectIntUVE`,
  registered in `uve_math`): position-plus-size rects with inclusive `ContainsUVE`,
  strict `IntersectsUVE`, `Intersection`/`Union`, `TransformUVE` (scale + offset) and
  `ToStringUVE`; the int twin widens corner arithmetic to int64 so rects near INT32_MAX
  compare correctly. `UIImageComponentUVE`, `UIButtonComponentUVE` and `UIQuadUVE` carry
  one `rect` (defaults preserved; JSON keeps the `positionPixels`/`sizePixels` keys, so old
  scene files load unchanged), and `Render::ViewportRectUVE` is now an alias for
  `RectIntUVE` — the RHI's four-uint32 struct retired, the backend fit-checks expressed as
  `ContainsUVE` against the target bounds. `UITextComponentUVE` keeps its position point,
  `PresentationLayoutUVE` its layout record. Tests in `Test/Math/rect_uve_tests.cpp` and
  `Test/Math/rect_int_uve_tests.cpp`, including the hover edge-inclusion contract and an
  INT32_MAX overflow pin. Verified: full suite green (1668 integration + 1306 core, 2026-10-09).
- `[x]` **Integer vectors** (`Vector2iUVE`, `Vector3iUVE`) — `vector2i_uve.h` /
  `vector3i_uve.h` + `Internal` twins, registered in `uve_math`: int32 x/y(/z) with plain
  component-wise arithmetic, widened int64 `DotUVE`/`LengthSquaredUVE`, float `LengthUVE`,
  component-wise `Min`/`Max`/`Clamp`, `ToVector2UVE`/`ToVector3UVE` conversions and
  `ToStringUVE`. No normalize (a unit vector is not an integer vector). `Vector2iUVE` backs
  `RectIntUVE` and every viewport-override construction site; `Vector3iUVE` has no in-tree
  user yet and completes the family. Tests in `Test/Math/vector2i_uve_tests.cpp` and
  `Test/Math/vector3i_uve_tests.cpp`, including an int64 dot-exactness pin past int32 range.
  Verified: full suite green (1668 integration + 1306 core, 2026-10-09).
- `[x]` **A scalar-utility header** — `uve/math/scalar_uve.h` (`UVE::Math`, header-only):
  `kPiUVE`/`kPiDoubleUVE` from `std::numbers`, `DegToRadUVE`/`RadToDegUVE`, `LerpUVE`,
  `ClampUVE` (via `std::clamp`), `SmoothStepUVE`, `ApproximatelyEqualUVE` with explicit
  epsilon — all `constexpr`, floating-point-constrained except `ClampUVE`. The physics
  (character controller) and animation (two-bone IK) copies used it first; Oct 2026 migrated
  the rest of the literals engine-wide (player/character/world-env/collider, navmesh,
  renderer sky, perception, the editor panels, clip editing). What stays outside the header
  is deliberate: viewport-core (`univex/`) has no `uve_math` linkage and stays on
  `std::numbers` because that target is host-independent by contract, and the shader-source
  pi literals are GLSL, not C++. `Test/Math/scalar_uve_tests.cpp` covers values, boundaries
  and constexpr-use. Verified: full suite green (1508 core + 1713 integration + 14 settings, 2026-10-10).
- `[x]` **`SphereUVE` and an OBB type** — `sphere_uve.h` + `Internal/sphere_uve.cpp` and
  `oriented_box_uve.h` + `Internal/oriented_box_uve.cpp` (`Math::SphereUVE`, `Math::OrientedBoxUVE`,
  registered in `uve_math`): center/radius with `Contains`/`ClosestPoint`/`IsValid`, and a
  center/half-extents/rotation box with local/world transforms plus `ToAabbUVE` (exact 8-corner
  bound). The physics narrow phase now takes the shapes instead of loose parameters (ray/sweep/
  penetration queries migrated with validation semantics preserved verbatim — the narrow phase
  stays one step stricter than `IsValidUVE`, and the OBB query keeps normalizing handed-in
  rotations rather than requiring unit ones). Tests in `Test/Math/sphere_uve_tests.cpp` and
  `Test/Math/oriented_box_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **A random-number source** — `random_uve.h` (`Math::RandomUVE`, header-only, registered
  in `uve_math`): PCG32 XSH-RR with `NextUInt32/NextUInt64/NextFloat01/Range/Shuffle`, explicit
  seeding, value semantics (copying forks the stream — documented, not prevented). (Correction:
  the earlier "`ParticleEmissionUVE` carries a `seed` field" claim is stale — the struct has no
  seed; emission determinism comes from per-particle sequence numbers instead. `RandomUVE` is
  the generator the next jitter/spread feature reaches for.) Tests in
  `Test/Math/random_uve_tests.cpp`, including PCG paper-vector pins. Verified: full suite green
  (3998 ctest cases, 2026-10-12).
- `[x]` **Curves / easing / noise** — `easing_uve.h` (`Math::EaseKindUVE` + `EaseUVE`, all 31
  easings.net curves, header-only), `curves_uve.h` (cubic Bézier point/tangent, Catmull-Rom
  point, header-only), `noise_uve.h` (seeded value + gradient noise 2D, header-only), all
  registered in `uve_math`. The UI tween system now maps its serialized ease vocabulary onto
  `Math::EaseUVE` instead of owning duplicate formulas. No curve *asset* (keyframed,
  serializable) — deliberately: the math layer carries the functions, an asset is authoring
  work for a later tier. Tests in `Test/Math/easing_uve_tests.cpp`, `Test/Math/curves_uve_tests.cpp`
  and `Test/Math/noise_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **Deliberate SIMD boundary with scalar ABI/fallback** — `point_transform_batch_uve.h`
  and its private implementation add `TryTransformPointsUVE`; `AabbUVE::TransformUVE` is the
  first real consumer. SSE2 is selected only when the compiler target guarantees it, including
  the corresponding MSVC target macros. Two rows accumulate in double in the scalar operation
  order; other targets use `TransformPointUVE`. Public vector/quaternion/matrix sizes and float
  alignment do not change, no intrinsic type is exposed, and no CPU/fast-math build flags are
  enabled. This is not blanket vectorization or a measured speedup claim. Exact in-place and
  adjacent disjoint spans work; partial overlap, invalid sizes/paths and unsupported explicit
  SIMD fail before mutation. Seven `PointTransformBatchUVETest` cases pin layout, unaligned-
  to-16-byte storage, odd tails, scalar equivalence, cancellation/non-finite policy and AABB
  consumer parity. They also pass with focused ASan+UBSan and a separately compiled `-mno-sse2`
  fallback on this host. Full verification: 69 focused math cases and **4,076 CTest cases,
  zero failures, seven existing Vulkan skips** (2026-10-13).

## 2. Containers — `Engine/Runtime/Core/Containers`

`[x]` **The directory exists and contains zero code files.** Stale as written: `Core/Containers`
now holds `FixedArrayUVE`, `SmallVectorUVE`, `HandleTableUVE`, `SparseSetUVE`, `RingBufferUVE`
and `BitSetUVE`, and `Core/Strings` holds `StringIdUVE` plus the string utilities. What stays
true is `Core/Types` and `Core/Delegates` — both still README-only placeholders for a future
restructuring stage, deliberately out of scope here.

This is the largest structural hole in the foundation, and it gets worse with
time rather than staying constant: every system written before a container layer
exists grows around `std::vector` and `std::unordered_map`, and each one is a call
site that has to be revisited later. That is why this sits at Tier 0 of the
roadmap in Part V.

What a runtime of this shape needs, and what each unblocks:

- `[x]` **`FixedArrayUVE<T, N>`** — fixed-capacity, stack-allocated, size tracked
  separately from capacity. The workhorse for bounded per-frame data. The engine
  already has many bounded limits expressed as raw `std::array` plus a manual
  count (`LightListUVE`, the frame-task graph's 256-task cap, the diagnostics
  capture caps); each is the same pattern rewritten.
  Implemented in `Core/Containers` (Tier 0, item 0.2); `LightListUVE` and the
  frame-task graph's 256-task cap now use it. Verified: full suite green (1318 core + 1668 integration, 2026-10-09). Both migrations confirmed in-tree (`LightListUVE` alias + scheduler `m_tasks`).
- `[x]` **`SmallVectorUVE<T, N>`** — inline storage for the first `N`, heap
  spill beyond. The single highest-impact container for a game engine, because the
  overwhelming majority of per-entity and per-frame lists are small and currently
  each one heap-allocates.
  Implemented alongside `FixedArrayUVE` in `Core/Containers` (Tier 0, item 0.2)
  with its own test suite; first migration landed Oct 2026 (frame-task dependencies
  inline at the 32-cap, zero allocations for accepted tasks, braced-init supported).
  Verified: full suite green (1511 core + 1713 integration + 14 settings, 2026-10-10).
- `[x]` **`SparseSetUVE`** — `sparse_set_uve.h` (`Containers::SparseSetUVE<T>`, header-only):
  dense array plus sparse index, O(1) insert/remove with contiguous iteration, uint32 ids with
  swap-remove and a parallel id span. The archetype storage keeps its own layout (migrating the
  ECS onto the shared primitive is future work, explicitly not attempted). Tests in
  `Test/Core/Containers/sparse_set_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **`HandleTableUVE<T>`** — generational slot map. The engine already has
  **three independent hand-rolled versions** of this idea: `EntityUVE`
  (index + generation), `VoiceHandleUVE` (generational voice id), and
  `ResourceHandleUVE<Tag>` (phantom-tagged u32). They are each correct; they are
  also each a separate implementation of the same primitive.
  Implemented in `Core/Containers` (Tier 0, item 0.5) as a handle-concept generic
  (`SlotHandleUVE` default; `VoiceHandleUVE` packs index+generation into its u32);
  both audio devices rebuilt on it with unified voice+state slots. Correction: the
  "generational voice id" above was actually a monotonic counter plus a map — the
  migration is what made voices truly generational (slot reuse, stale-safe).
  `EntityUVE`/`ResourceHandleUVE<Tag>` migrations remain future work. Verified: full suite green (1318 core + 1668 integration, 2026-10-09). The `VoiceHandleUVE` rebuild is confirmed truly generational (audio tests green).
- `[x]` **`RingBufferUVE<T>`** — `ring_buffer_uve.h` (`Containers::RingBufferUVE<T, Capacity>`,
  header-only): stack-allocated bounded FIFO with overwrite-oldest semantics, an explicit
  dropped-count and oldest-to-newest iteration. `MemorySinkUVE` keeps its private ring (its
  message shape predates the primitive; migrating it is future work). Tests in
  `Test/Core/Containers/ring_buffer_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **`BitSetUVE`** — `bit_set_uve.h` (`Containers::BitSetUVE<N>`, header-only): uint64
  words with spare-bit masking and the full `& | ^ ~ ==` vocabulary, for layer masks and
  archetype signatures. `ArchetypeSignatureUVE` keeps its private copy (same future-migration
  note as the sparse set). Tests in `Test/Core/Containers/bit_set_uve_tests.cpp`. Verified: full
  suite green (3998 ctest cases, 2026-10-12).
- `[x]` **`StringIdUVE` / interned name** — a hashed, comparable, cheap-to-copy
  name. Type ids in `TypeMetadataEntryUVE` are raw `std::string`, compared by
  value; every reflected property lookup is a string compare. An interned id turns
  those into integer compares and makes name-keyed maps cheap.
  Implemented in `Core/Strings` (Tier 0, item 0.4): process-wide intern table
  (deduped, immortal strings, mutex-guarded interning, lock-free compare); entry
  + property type ids and nesting hosts are ids, and the 17-name property-type
  vocabulary is interned once at startup so inspector dispatch compares integers.
  Snapshot order still sorts by recovered text. Verified: full suite green (1318 core + 1668 integration, 2026-10-09). Entry + property + nesting-host ids confirmed in `TypeMetadataEntryUVE`.
- `[x]` **`SpanUVE<T>`** — non-owning view. `std::span` covers this in C++20; what
  is missing is the convention of using it, since most interfaces here take
  `const std::vector<T>&` and therefore cannot accept a subrange or a fixed array.
  Convention established (Tier 0, item 0.2): read paths take `std::span<const T>`
  — exemplar is the scheduling module (`FrameTaskGraphUVE::GetTasksUVE`,
  `FindTaskIndexUVE`); most interfaces still take `const std::vector<T>&` (future
  migrations). Verified: full suite green (1318 core + 1668 integration, 2026-10-09). Exemplar confirmed (`GetTasksUVE` returns `std::span<const FrameTaskDefinitionUVE>`; `FindTaskIndexUVE` takes one).

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

- `[x]` **An STL-compatible allocator adaptor (`StdAllocatorUVE<T>`)** — and this
  is the problem. Without it, no `std::vector`, `std::string` or `std::unordered_map`
  in the engine can allocate through any of the allocators above. The allocators
  are real, tested, and used by almost nothing. Pairing this with the container
  layer in Part I §2 is what turns the memory module from infrastructure into
  something the engine actually runs on.
  Implemented in `Core/Memory` (Tier 0, item 0.3): thin stateful adaptor over an
  `IAllocatorUVE&` (pmr-style non-propagating traits, identity equality,
  construction-site file/line labels); a `std::vector` test allocates through
  `PoolAllocatorUVE` with the `MemoryManagerUVE` tracker observing. Verified: full suite green (1318 core + 1668 integration, 2026-10-09).
- `[x]` **A frame/linear arena distinct from the stack allocator** — `linear_arena_uve.h`
  (`Memory::LinearArenaUVE`, header-only): bump allocation with alignment, no-op deallocation
  (with a range assert), `ResetUVE` that runs no destructors, and a throw (after assert+log) on
  exhaustion. Tests in `Test/Core/Memory/linear_arena_uve_tests.cpp`, including exhaustion death
  tests. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **Smart pointers paired with the allocator API** — `unique_ptr_uve.h`
  (`Memory::UniquePtrUVE<T>`, the allocator travels with the pointer, `UVE_MAKE_UNIQUE` captures
  the call site) and `intrusive_ptr_uve.h` (`Memory::RefCountedUVE` with an atomic count starting
  at 0 plus `Memory::IntrusivePtrUVE<T>`, fresh-object wrap convention, heap-only rule). Tests in
  `Test/Core/Memory/unique_ptr_uve_tests.cpp` and `Test/Core/Memory/intrusive_ptr_uve_tests.cpp`.
  Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **A per-thread or thread-safe allocator wrapper** — `thread_safe_allocator_uve.h`
  (`Memory::ThreadSafeAllocatorUVE`, header-only): a mutex decorator over any `IAllocatorUVE`
  (the inner allocator must outlive it). Tests in
  `Test/Core/Memory/thread_safe_allocator_uve_tests.cpp`, including a 4-thread hammer. Verified:
  full suite green (3998 ctest cases, 2026-10-12).

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

`[x]` **The frame graph has a runtime foundation consumer:** `WorldUVE` uses its stable
execution plan and `FrameSchedulerUVE::ExecuteSerialUVE` for registered scene systems and
transform propagation. This deliberately preserves deterministic main-thread ownership.
`EngineCoreUVE`'s overall update chain is not converted to parallel gameplay; that is a separate
system-integration choice, not a missing job primitive. Existing worker consumers also include
particles, so the older "only assets and shaders" claim was too narrow. Serial planning/action
failure tests live in `frame_serial_uve_tests.cpp`; existing parallel scheduler tests are unchanged.

Missing primitives:

- `[x]` **`ParallelForUVE`** over the pool — `parallel_for_uve.h` (`Threading::ParallelForUVE`,
  header-only): chunked index ranges over `IThreadPoolUVE` with an empty-range early-out. Nesting
  a second `ParallelForUVE` over the same pool deadlocks when the outer chunks occupy every
  worker — documented in the header with the rule (outer chunks strictly below the worker count,
  or a second pool) and pinned by a test. Tests in
  `Test/Core/Threading/parallel_for_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **Named synchronisation primitives** — `spin_lock_uve.h` (`Threading::SpinLockUVE`,
  test-and-set with STL `lock_guard` interop), `rw_lock_uve.h` (`Threading::RwLockUVE` over
  `std::shared_mutex`, same interop) and `semaphore_uve.h` (`Threading::SemaphoreUVE`, an
  unbounded counting gate over mutex+condvar), all header-only. Tests in
  `Test/Core/Threading/spin_lock_uve_tests.cpp`, `rw_lock_uve_tests.cpp` and
  `semaphore_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **A `ThreadIdUVE` type** — `thread_id_uve.h` (`Threading::ThreadIdUVE`, header-only): a
  hashed-`std::thread::id` uint64 wrapper with `CurrentUVE`/`FromNativeUVE`, `std::hash` support
  and 0 meaning unknown. `Core/Diagnostics` keeps taking raw `uint64` thread ids on purpose — that
  module depends on zero `uve/*` targets by design, and naming this type would be the forbidden
  edge; `ValueUVE()` is the bridge at emission. Tests in
  `Test/Core/Threading/thread_id_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **Capability-aware thread control** — new `thread_control_uve.h/.cpp` adds affinity
  and priority without changing existing naming or pool defaults. Current-thread and caller-owned
  `std::thread&` operations report Applied/Unsupported/InvalidThread/InvalidArgument/PermissionDenied/
  SystemError; no raw native handles or silent downgrades. Non-joinable threads are rejected before
  native access. Reads publish output only on success, and callers synchronize join/move/detach.
  Linux uses pthread affinity and explicit OTHER/BATCH/RR scheduler classes; realtime may be denied.
  Windows x64 uses processor-group masks (one group per request); priority never changes the
  process class, and Realtime is unsupported unless that process is already realtime. MSVC native
  thread handles are supported; MinGW's different std::thread ABI reports Unsupported for that
  overload rather than casting a pthread descriptor to HANDLE. Other platforms honestly report
  unsupported operations. GetThreadControlCapabilitiesUVE distinguishes these boundaries.
  Six new `ThreadControlUVETest` cases cover invalid requests/handles, real affinity and priority
  readback, permission failure and worker-name preservation; 22 focused threading tests pass.
  Full Linux verification: **4,056 CTest cases, zero failures, seven existing Vulkan skips**.
  Windows x64 current-thread code also compiles to a COFF object using Zig/Clang's Windows-GNU
  target with strict warnings; this is a compile check, **not** Windows/MSVC execution coverage.
  Nothing here makes gameplay parallel or automatically assigns worker affinity/priority.

## 5. Utilities and diagnostics

- `[x]` **`TimerUVE`** (`Core/Utilities`) — steady-clock frame timing with a
  fixed-step accumulator returning `FixedStepResultUVE` (step count + interpolation
  alpha). Owned exclusively by the frame-pipeline thread.
- `[x]` **`binary_buffer_uve.h`** (`Core/Utilities`) — explicit-endian integer and float
  codec over `std::vector<std::byte>`: `AppendUint16/32/64/FloatLe/BeUVE` plus advancing,
  bounds-checked `Read...Le/BeFromBufferUVE` (same failure contract as before: false, offset
  and value untouched). The old host-order functions are deleted — there is exactly one way
  to serialize an integer, and it names its byte order. 7 format migrations: the universal
  `.uve*` envelope header, the asset bundle, the texture wrapper (+ KTX2 header reads, which
  are LE by spec), the mesh format (incl. vertex floats), the `.uvsave` length prefixes, the
  save-payload header (local helpers deleted), and the audio asset (local LE codec deleted;
  sample payload was host-order bulk bytes, now per-sample LE). Zero byte changes on
  little-endian targets (all of them): every pre-existing test passes unchanged, old files
  load bit-for-bit. Deliberately untouched: the external-spec importers (PNG BE / WAV+TGA LE
  readers are already spec-correct), the network packet codec (already BE network order),
  and `AUDIT.md`'s frozen API inventory. `AppendString/Bool/Double` remain unadded — the
  census showed no binary site needs them (strings ride length-prefixed raw bytes; no format
  stores doubles or bools). First tests the API ever had: 9 cases incl. LE/BE literal pins,
  IEEE-754 float pins, and the simulated byte-swap round-trip asserting byte-identical
  re-emission. Verified: 1318 core + 1668 integration green (2026-10-09).
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
- `[x]` **Hashing** — `uve/utilities/hash_uve.h` (`UVE::Utilities`, header-only): `HashCombineUVE`
  (exact boost formula, for in-memory tables), incremental `Fnv1a64UVE` plus one-shot
  `HashBytesUVE`/`HashStringUVE` (fixed-width `uint64_t`, for persisted/cross-process identity).
  9 migrations: the content fingerprint, the save checksum, the shader source hash (whose
  `Detail::ComputeFnv1aHashUVE` is deleted), the FBX corner key, the cache file names, the
  data-table cache version, and the archetype/pose/mesh-pair combiners. Zero value changes
  everywhere except the mesh pair hash (64-bit → 32-bit golden constant; frame-built table,
  never persisted): the two typo-seed sites keep their legacy seed explicitly, since the save
  checksum is a format. Two copies stay deliberately local with pointer comments — the
  application-runtime identifier hash (`uve_platform` sits below `uve_utilities`; an edge would
  be a target cycle) and the UVScript program fingerprint (that module is std-only by design).
  Trivial single-field `std::hash` pass-throughs were left alone (nothing to gain). Verified by
  the full suite: 1309 core + 1668 integration green, including 6 new hashing tests with
  `static_assert`ed FNV known-answer vectors (2026-10-09).
- `[x]` **A UUID/GUID type** — `uuid_uve.h` (`Utilities::UuidUVE`, header-only): 16 bytes with
  v4 construction from caller entropy, canonical string form, strict `TryParseUVE` and an FNV-1a
  `std::hash`. (`AssetGuidUVE` stays asset-specific; this is the general one.) Tests in
  `Test/Core/Utilities/uuid_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **String utilities**, **`ScopeGuardUVE`**, **`NonCopyableUVE`**, and an
  **enum-flags helper** — `string_utils_uve.h` (`Trim`/`Split` (empties kept)/`Join`/`ToLower`/
  `ToUpper`, ASCII-only), `scope_guard_uve.h` (`ScopeGuardUVE` + `UVE_DEFER`), `non_copyable_uve.h`
  (`NonCopyableUVE`, boost shape, move also deleted) and `flags_uve.h` (`FlagsUVE<TEnum>` with
  implicit enum conversion and the full typed operator set), all header-only. The property-flags
  vocabulary migrated onto `FlagsUVE` (the bespoke `operator|` and `HasPropertyFlagUVE` are
  deleted; all readers go through `.HasUVE`). `TypeMetadataMethodUVE::flags` stays a raw `uint32`
  with no vocabulary — nothing reads it, so there is nothing to migrate it to yet. Tests in
  `Test/Core/Strings/string_utils_uve_tests.cpp` and `Test/Core/Utilities/{scope_guard,non_copyable,flags}_uve_tests.cpp`.
  Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **`Core/Scheduling` and `Core/Diagnostics` carry no doc comments at all** — both now
  carry module headers: Scheduling explains the two-graph split (`JobGraphUVE` for one-shot
  cascades, `FrameTaskGraphUVE` for the repeating frame plan) and the honest unwired status;
  Diagnostics states its three constraints (bounded capture with dropped counters, no clock,
  zero `uve/*` deps) and the raw-uint64 thread-id bridge. Verified by reading, 2026-10-12.

## 6. Reflection — `Engine/Runtime/Objects/Core`, namespace `UVE::Core`

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
- `[x]` **A reflection factory.** `TypeMetadataEntryUVE` carries a heap factory
  (`createDefaultInstance`/`destroyInstance`/`cloneInstance`/`assignInstance`, bound by
  `BindTypeUVE`, queried via `HasFactoryUVE`) plus the archetype-slot construction trio
  (`instanceSize`/`instanceAlignment`, in-place construct/move/destroy) that
  `IEntityManagerUVE::AddComponentErased` needs — the Core-native mirror of
  `Scene::ComponentTypeInfoUVE`. The editor already builds on it (reset-to-default,
  previews, snapshots); `SceneSerializerUVE` now deserializes through it too, via a
  metadata-driven property codec (`MakeMetadataRegistrationUVE`) whose skip rule is exactly
  `TypeMetadataPropertyUVE::IsSerializedUVE()` (no runtime state, no editor-only authoring,
  no unbound properties). `CanvasComponentUVE` is the pilot: it round-trips with no
  hand-written JSON on either side, byte-identical to before. An editor "add component by
  type name" flow and the remaining component migrations were Tier 1.6's mechanical payoff (done: 31 migrated, `ComponentRegistryUVE` live).
  Verified: full suite green (1668 integration + 1306 core, 2026-10-09).
- `[x]` **No `TypeIdUVE` strong type** — resolved twice over: type ids stopped being raw
  `std::string` with the `StringIdUVE` migration, and the name now exists too —
  `Core::TypeIdUVE` aliases `Strings::StringIdUVE` in `type_metadata_uve.h`, and every entry,
  property and lookup signature in that header spells it. Verified: full suite green
  (3998 ctest cases, 2026-10-12).
- `[x]` **Opt-in native identity/lifetime registry** — `Core::ObjectUVE`, `ObjectHandleUVE`
  and `ObjectRegistryUVE` in `Objects/Core/Expose/uve/object/`. The non-owning registry uses
  `HandleTableUVE<ObjectUVE*>` privately and tags handles with a non-recycled registry identity.
  It rejects stale/foreign/duplicate handles, detaches without deleting caller objects, supports
  either destruction order, and permanently closes registration at Shutdown. Objects cannot
  move/copy; lookup pointers are borrowed and all access/destruction is caller-synchronized.
  ECS, asset, GPU handles, reflection and `Memory::RefCountedUVE` are not replaced. Handles are
  runtime-only, not serialized or a native-plugin ABI. Nine `ObjectRegistryUVETest` cases cover
  ownership, both shutdown orders, growth, foreign/duplicate/stale ids and externally locked
  cross-thread destruction. Eight `HandleTableLifetimeUVETest` cases additionally verify clear/
  reuse, small-domain rollover, constructor failure, move/copy and iteration. The shared table
  now retains generations on Clear, retires exhausted slots and keeps its free list in dead
  slots, so Release/Clear allocate nothing. Its raw handles remain table-relative, not owner-tagged.
  Both stale-handle regressions failed before this fix. Normal verification: 28 focused cases;
  **4,015 CTest cases, zero failures, seven existing Vulkan skips** (2026-10-13).

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
   directory or a `.uvbundle`, so the same virtual path resolves in development
   and in a packaged build.

Supporting infrastructure, all real: `AssetImporterUVE` (per-extension registry
with per-type settings), `AssetImportQueueUVE` (deterministic main-thread
scheduler, one job per tick, with a full diagnostic taxonomy),
`DerivedArtifactCacheUVE` (proves an import result still matches source bytes plus
settings version), `ProjectFileIndexUVE` and `ProjectChangeWatcherUVE` (the asset
browser's tree and change journal), `ResourceDependencyGraphUVE` (BFS invalidation
plan for reimport), `AssetBundleUVE` (`.uvbundle` pack/unpack), and
`UveFileHeaderUVE` — a universal envelope with an `AssetKindUVE` covering Scene,
Prefab, Blob, Bundle, Mesh, Texture, Shader, Material, Save, DataTable, Audio and
Animation.

`[x]` **Capability-detected hot reload with polling fallback** — `HotReloadUVE` now
supplements path/existence/mtime/size snapshots with nonblocking Linux inotify in
`Asset/Internal/native_file_watcher_uve.{h,cpp}`. Automatic reports the backend that actually
initialized; forced Polling and unsupported/unwatchable native paths retain real snapshot
polling. There is no new worker or duplicate project scanner. Parent and symlink-target
directories are watched; native bursts coalesce at the existing interval, requests are GUID-
sorted, and real queue overflow/directory replacement forces a bounded-drain rescan. Duplicate
Track does not erase history; epoch timestamps are valid; deletion retains last-good data;
recreation/DB relocation reloads, and a change during Loading remains pending. Manager/event
callbacks are outside the lock. Events mean triggered, not succeeded. Shutdown closes watches.
Polling alone still cannot see identical-metadata content changes entirely between scans;
Windows/macOS use that explicit fallback, not a pretend native watcher. Thirteen new
`hot_reload_backend_uve_tests.cpp` cases cover these contracts, including a real kernel queue
overflow, symlink target writes and descriptor cleanup. All 17 hot-reload tests pass, the
in-flight-load case passes 20 repetitions, and the full strict build/CTest passes **4,069 cases,
zero failures, seven existing Vulkan skips**, with no watcher skips on this host (2026-10-13).

## 8. Asset types

| Asset | Holds | Importer | Status |
|---|---|---|---|
| `MeshAssetUVE` | `MeshVertexUVE` array (position, normal, uv, tangent, handedness), `uint32` indices, `AabbUVE localBounds`, skinning influences, joints | **OBJ** and **glTF/GLB**, plus a `.uvmodel` envelope | `[x]` |
| `TextureAssetUVE` | width, height, format, pixels, a CPU-generated mip chain and Basis-compressed uploads | **PNG, JPEG, BMP, TGA**, plus `.uvtex` | `[x]` mips + compression landed (see below) |
| `MaterialAssetUVE` | albedo colour, albedo/normal/AO texture GUIDs, metallic, roughness, emissive colour, shader GUIDs, transparency flag | **MTL**, plus `.uvmat` | `[~]` see below |
| `ShaderAssetUVE` | stage, source text, entry point — stored as-is, not compiled at asset level | source importer + `.uvshader` | `[x]` |
| `AudioAssetUVE` | channels, sample rate, bounded interleaved normalized float samples | **WAV, FLAC, MP3, OGG/Vorbis** | `[x]` bounded import/validation; streaming is a separate runtime contract |
| `AnimationClipAssetUVE` | clip id, duration, sampled TRS poses, timed string events | envelope only | `[~]` see below |
| `DataTableUVE` | typed schema'd rows (bool / int64 / double / string) | **CSV, TSV, JSON** | `[x]` the most built-out asset in the module |
| `BlobAssetUVE` | raw bytes, unparsed | — | `[x]` |

Three things about this table matter more than the rest:

- `[x]` **`AnimationClipAssetUVE` stores a single TRS track.** Connected since Tier 2.6:
  per-joint `AnimationAssetBoneTrackUVE` channels, a separate `SkeletonAssetUVE`, a glTF skin
  converter and `TrySkinMeshUVE` output — the two halves join through the skinning path, and the
  renderer CPU-skins posed skeletons (Tier 3.1).
- `[x]` **`MaterialAssetUVE` has three of the six PBR texture slots** its own doc
  comment describes. Complete since Tier 2.7: metallic/roughness/emissive map slots sit on the
  asset, the renderer binds slots 12/13, and `lit_shadowed_3d` samples them (B metallic, G
  roughness).
- `[x]` **Audio format import** — existing `RegisterWavImporterUVE` is unchanged; new
  `RegisterCompressedAudioImportersUVE` / `DecodeEncodedAudioUVE` in `Runtime/Audio` add
  FLAC, MP3 and OGG/Vorbis and are registered by `engine_core_uve.cpp`. The already pinned
  miniaudio 0.11.25 supplies FLAC/MP3 plus its bundled `extras/stb_vorbis.c`; decoder support
  is now enabled without another engine dependency or a change to warning policy. Vendor
  sources retain their existing MIT/Unlicense terms. There is no claim of AAC/Opus support.
  Bounds: 64 MiB encoded input, 1..32 channels, the existing 1,048,576-sample asset cap,
  nonzero rate and finite normalized samples (lossy overshoot is explicitly clipped). Explicit
  format selection never falls back to a different codec. Rejected input preserves the output;
  import publishes the existing version-one `.uvaudio` envelope by temporary rename and
  preserves an existing destination on failure. This is import, not long-file streaming.
  Seventeen `compressed_audio_importer_uve_tests.cpp` cases cover real decoding, classification,
  GUID publication, format mismatch, malformed/truncated headers, size/output bounds and failed
  publication cleanup; the three existing WAV importer tests are unchanged. Fixtures in
  `Test/Audio/Fixtures/` are self-generated 440 Hz mathematical tones, with a reproducible
  FFmpeg 7.0.2 generation script; no recording was copied and no FFmpeg binary/code is shipped
  or linked. All 20 focused cases and **4,050 CTest cases** pass, with zero failures and the
  same seven explicit Vulkan skips (2026-10-13).

Also missing, as of the last audit: `[~]` an FBX importer (static meshes convert via vendored
ufbx; skins, materials and animation are explicitly out of scope), `[x]` any animation importer
(`gltf_animation_converter_uve` samples glTF animation channels into clips), `[x]` a skeleton
asset separate from the mesh (Tier 2.6: `skeleton_asset_uve`), and `[x]` a font asset —
`FontAssetUVE` (raw sfnt bytes + family name, `.uvfont` envelope, `AssetKindUVE::Font`) with a
`.ttf`/`.otf` importer, and `UIFontAtlasUVE` bakes from caller bytes now (the embedded Liberation
Sans subset stays the default, exposed for the equivalence test). Tests in
`Test/Core/Asset/font_asset_uve_tests.cpp`, `Test/Core/Asset/ttf_importer_uve_tests.cpp` and the
extended `Test/UI/ui_font_atlas_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).

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
| `TextureDescUVE` | width, height, format, mipLevels, array layers, cubemap shape | no depth, no sample count, no usage flags (Tier 2.3 landed layers + cubes) |
| `TextureFormatUVE` | `RGBA8Unorm`, `RGBA16Float`, `Depth32Float`, BC1/BC3/BC7, ETC2/ETC2A8 | eight formats; no sRGB, no single/two-channel |
| `ShaderDescUVE` | stage, source, entry point | `Geometry` compiles but has no pipeline slot |
| `PipelineDescUVE` | vertex+fragment shader, vertex layout, stride, topology, depth test/write/compare, cull/winding/fill/bias, blend mode | rasterizer tail landed (Tier 2.1, RHI side) |
| `RenderPassDescUVE` | up to 4 colour attachments, one depth attachment, load+store ops, clear values, optional viewport override | multi-target + `StoreOpUVE` landed (Tiers 2.4/2.5) |
| `PrimitiveTopologyUVE` | `Triangles`, `Lines` | no points or strips (lines serve the debug renderer) |

Three consequences follow directly from that table and are worth stating plainly,
because each one blocks a whole feature:

- `[~]` **No rasterizer state.** The RHI side landed (Tier 2.1): cull/winding/fill/bias plus a
  full `DepthCompareUVE` sit on `PipelineDescUVE` and all three backends apply them. What stays
  open is the consumer: the gizmo overlay and the studio backdrop still drive raw GL depth state
  — and both live in host-independent `univex/` viewport-core, which cannot see the RHI by
  contract. The migration is a design pass (how viewport-core overlays adopt RHI pipelines), not
  a missing API.
- `[x]` **No multiple render targets.** Landed (Tier 2.4): gap-rejecting colour slots 1..3 on
  `RenderPassDescUVE`, with a Vulkan test recording a multi-target pass and sampling per-target
  colours.
- `[x]` **No texture dimensionality.** Landed (Tier 2.3): `TextureTypeUVE::Cubemap` +
  `Texture2DArray` with creation-time shape validation, HW-ordered `CubemapFaceUVE` and
  render-into-layer. (Image-based lighting itself is still absent — see §10.)

Also absent: `[x]` a sampler object — landed (Tier 2.2: `SamplerDescUVE` with Point filtering
in all three backends). `[x]` **Validated bind-group packets** now live in
`RHI/RHI/Expose/uve/rhi/bind_group_uve.h`: bounded borrowed packets reuse existing typed resource
commands, reject invalid/duplicate bindings or incompatible declared layouts before any bind,
and retain input ordering. The material renderer consumes the packet for its five PBR textures.
GL applies the same resource calls, Vulkan retains/replays them and Null records them; no new
GPU resource lifetime or arbitrary native descriptor-set numbering is pretended. Unlisted slots,
resource ownership, hardware limits and graphics/compute pass rules keep their existing meaning.
Five new `BindGroupUVETest` cases and 88 renderer cases pass; full CPU-only CTest has **4,093
cases, zero failures, seven expected Vulkan skips**. A new native GL binding-state case and the
existing Vulkan point-sampler pixel assertions exercise this packet in graphics-capable CI.
The native Ubuntu/GCC/Debug workflow for `37f0b0b` passed (run `38402183962`); the earlier
pending-verification label is now resolved, without reimplementing the packet.

`[/]` **Explicit device-operation contracts** — `device_operations_uve.h`, optional methods
on `IRenderDeviceUVE`, and `ICommandBufferUVE::MemoryBarrierUVE` add capability-aware operations.
`Success` means the host call/native command was accepted, not implicit GPU completion;
`Recorded` distinguishes a retained command or Null model. Failures leave output arguments
untouched. Missing capabilities explicitly return `Unsupported`, never pretend success.

- **Fences / idle:** real GL sync objects and Vulkan queue fences, non-recycled handles,
  finite/zero waits, stale rejection and safe retirement. Vulkan rejects fence/idle/copy calls
  while CPU submissions still await Present; it cannot signal completion before that work runs.
- **Barriers:** validated outside-pass visibility masks; GL memory barriers, Vulkan global
  memory dependencies on its dynamic-rendering path, and exact Null command records. Existing
  pass/texture binding owns image layout transitions; queue-family transfers are not implied.
- **Buffer copy:** native GL/Vulkan range copies plus actual Null host-byte copying/recording;
  overlap/overflow/zero-length rejection. Vulkan advertises 4-byte copy alignment. No hidden
  CPU readback fallback is used for the native paths.
- **Texture blit / mipgen:** real GL color blits (point/exact copy or linear scale), mip/layer/
  extent/format checks, caller-binding/scissor preservation, and lower-mip generation on
  preallocated color textures. Null validates/records; Vulkan explicitly reports these paths
  unsupported instead of accepting a no-op. Depth/stencil blits and format conversion are not
  part of this color-transfer contract.
- **Queries:** real GL GPU timestamps and any-sample occlusion with state/lifetime validation
  and nonblocking result polling. Null records query operations but NEVER invents visibility or
  GPU nanoseconds. Vulkan currently advertises queries unsupported.
- **Surface view:** `RenderSurfaceUVE` exposes the backend-owned default surface/swapchain's
  current drawable extent and presentation boundary, without duplicating window or swapchain
  ownership. GL/Vulkan provide actual surface metadata; Null explicitly has no native surface.
  Present acceptance is not a claim of completed scan-out.

Ten new CPU `DeviceOperationsUVETest` cases pass, alongside existing binding tests. Full strict
Debug build/CTest: **4,103 cases, zero failures, seven existing Vulkan skips**. Added three GL
native tests assert copied bytes, generated/blitted pixels, preserved state, real 0/1 visibility
and timestamp ordering; two Vulkan tests verify copies/fences and the deferred-CPU-FIFO guard.
Native sources/tests pass strict syntax checks against real headers. Execution of these new
native cases is pending graphics-capable CI, so this implementation remains `[/]` until verified.
Semaphores/timelines and stencil state remain `[ ]`; they are not closed by a binary fence or a
surface metadata view. General Vulkan texture-transfer/query parity remains explicitly partial.

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
eligibility path but no occlusion test runs — Tier 3.6, needs the GPU query path above),
image-based lighting, geometry shaders (`ShaderStageUVE::Geometry` compiles standalone but still
has no pipeline slot, deferred by its own header comment), and the particle GPU upload path
(`ParticleRenderBridgeUVE` still states it performs no GPU work). Texture compression and
mipmaps left this list: Basis Universal transcoding plus CPU mip chains are real (`[x]`).

`[ ]` Missing renderer-level types: no material system (material→program binding
is inlined in the renderer), no text renderer, no skybox, no terrain. Landed since: `[x]` the 2D
renderer and sprite batcher (`Renderer2D v1`: Sprite2D + Camera2D + viewport split, Oct 2026),
`[x]` the debug/line renderer (Tier 2.8: `DebugRendererUVE::DrawBox3D` from one call over a 16K-line
batch), `[x]` reflection probe capture (a capture scheduler with per-camera influence mixing and a
budgeted recapture latch), and `[x]` a decal system (CPU-built receive patches plus draw commands
with frustum culling). (Verified present-or-absent by source read, 2026-10-12.)

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
| `[x]` `WorldTransformComponentUVE` | Scene-graph-authorized live pose writes | Get/ForEach/View expose const live poses; mutable erased access is rejected. `SceneGraphWriteAccessUVE` is available only to graph implementations. Initial component value construction stays supported; the mutable dirty bit is explicitly an invalidation request, not a pose writer. Renderer/physics/audio readers keep scalar layouts unchanged |
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
· `[x]` `CharacterControllerComponentUVE` · `[x]`
`PhysicsInterpolationComponentUVE` (optional render smoothing, not authoritative simulation).
`ISceneGraphUVE::TryTeleportLocalTransformUVE` and `ResetInterpolationSubtreeUVE` reset history
without changing serialized modes; reparenting snaps the affected subtree, idempotent setters
preserve useful history, and an unchanged reset pose is seeded correctly. Disabled simulation
tags keep world transforms current but discard their own interpolation history (the existing
non-inherited disabled-tag policy is unchanged); World scene resume also reseeds history.
Unknown modes/non-finite poses fail without output mutation, and double weighted interpolation
avoids overflow between large finite endpoints. Ordinary motion still records previous/current
poses for the existing renderer consumer. Twelve new tests in
`Test/Integration/Scene/derived_pose_contract_uve_tests.cpp` cover access authority, invalidation,
teleport/reparent/mode/reset/disable/resume, finite bounds and byte-identical serialized snapshots.
Existing corruption tests now construct malformed cache values rather than mutating const live
poses; every prior assertion remains. Verified: 106 focused cases and **4,088 CTest cases,
zero failures, seven existing Vulkan skips** (2026-10-13).

### Audio, animation, scripting, UI

`[x]` `AudioSourceComponentUVE` + `AudioAttenuationCurveUVE` · `[x]`
`AnimationPlayerComponentUVE` (Tier 3.1: step functions sample clips, `SyncAnimationUVE` ticks
every frame and physics step, the renderer CPU-skins the poses)
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

- `[x]` **A component registry** mapping type ↔ name/id for serialization. Every
  `TypeMetadataEntryUVE` names its C++ type (`cppName`), and `SceneSerializerUVE`
  auto-discovers a generic registration for every qualifying entry (factory + covered
  property types + at least one serialized property), so adding a component is a metadata
  declaration and never a serializer edit. 31 components migrated by pure deletion
  (byte-identical JSON, proven by their existing round-trip tests); 31 stay hand-written
  under a ledger in `GetRegistrationsByNameUVE` (entity references, custom shapes, legacy
  readers, reseed-on-load, metadata/JSON disagreements, missing entries). Verified by the
  full suite: 1668 integration + 1306 core tests green, including new pins for the
  discovery guard, a BitMask32 shape, and two previously uncovered round-trips.
- `[x]` **Tag / layer / mask component** — stale as written, landed on all three clauses:
  colliders carry `collisionLayer` and queries filter via `layerMask`; `RaycastQueryUVE` excludes
  a single entity *plus* a span of further exclusions; and gameplay tags exist as
  `GameplayTagComponentUVE`. (Verified by source read, 2026-10-12.)
- `[x]` **An enabled/disabled state** distinct from `VisibilityComponentUVE` —
  `DisabledComponentUVE` (a presence-means-disabled tag: absence keeps every existing document and
  archetype layout working unchanged) with `SetEntityEnabledUVE`/`IsEntityEnabledUVE` helpers in
  `Entity` and a hand-written serializer branch (empty object under "DisabledComponentUVE" — the
  metadata registry's contract is entries *with* properties, so a fieldless tag deliberately has
  no entry). Per-system adoption (each simulator checking the flag) is follow-up work. Tests in
  `Test/Integration/Entity/entity_enabled_uve_tests.cpp` plus a serializer round-trip. Verified:
  full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **A children index** to pair with `HierarchyComponentUVE` — `ChildrenIndexUVE`: one
  O(n) `RebuildUVE`, then O(1) sibling-ordered child lists (identical ordering to the scan, pinned
  by test). A derived cache, never a second source of truth — freshness is the caller's, and
  `SceneGraphUVE` owns one served via `RebuildChildrenIndexUVE`/`GetIndexedChildrenUVE` with no
  implicit rebuild. `GetChildrenUVE` keeps its always-correct scan. Tests in
  `Test/Integration/Scene/children_index_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).
- `[x]` **Entity command buffer** for deferred structural changes —
  `EntityCommandBufferUVE`: record create/destroy/add/remove during iteration (closures over
  staged payloads, `move_only_function`-erased so move-only components work), `PlaybackUVE` in
  record order afterwards, with `DeferredEntityUVE` placeholders for entities created by the same
  buffer. Tests in `Test/Integration/Entity/entity_command_buffer_uve_tests.cpp`. Verified: full
  suite green (3998 ctest cases, 2026-10-12).
- `[x]` **Query/view types** — `EntityViewUVE<T...>` via `manager.ViewUVE<T...>()`: a snapshotted
  iterable over the matches (range-for with `entry.entity` plus live `entry.GetUVE<T>()`), with
  `ForEachUVE`'s pointer-lifetime rule and no cached-query pretensions. Tests in
  `Test/Integration/Entity/entity_view_uve_tests.cpp`. Verified: full suite green (3998 ctest cases, 2026-10-12).

---

# Part IV — Systems, in one line each

Detail for these lives in `ROADMAP.md`; this is an index, and a note of what is
driven by nothing.

- `[x]` **Entity/Scene** — archetype ECS (`EntityManagerUVE`, PIMPL), stateless
  `SceneGraphUVE`, `SceneSerializerUVE` (explicit-roots, never implicit
  whole-world), `PrefabSystemUVE`, `ParticleRuntimeUVE`.
- `[x]` **World/scene lifecycle and system registration** — `WorldUVE` now owns a persistent
  primary scene and independently unloadable `World::SceneUVE` instances, each reusing the
  existing EntityManager/SceneGraph. Scene identity reuses `ObjectRegistryUVE`; secondary entity
  event buses are isolated so equal numeric entity ids cannot notify the wrong scene. Existing
  constructor, Tick counters, primary entity-manager access and serialized scene formats remain
  compatible. Optional population callbacks use the existing SceneSerializer/PrefabSystem.
  `WorldSystemDescUVE` registers start/tick/stop callbacks, named dependencies, priority and
  Before/AfterTransforms phases. The existing FrameTaskGraph/FrameScheduler now supplies stable
  topological planning and owner-thread serial execution; this is not parallel gameplay.
  Graph validation precedes initialization; failed starts roll back (including the failing
  start); reentrant world mutation is rejected; tick exceptions fail closed; unloading stops
  systems while the scene is alive, invalidates its handle and destroys entities in index order.
  Shutdown is permanent/idempotent, and scene pause preserves its contents. No second ECS,
  scene serializer, or general-purpose job graph was introduced. Fifteen tests in
  `Test/Integration/World/world_lifecycle_uve_tests.cpp` and three
  `Test/Core/Scheduling/frame_serial_uve_tests.cpp` cases cover these contracts, including old
  snapshot loading, stale/foreign scene handles, callback failure and event isolation. The two
  original World tests remain unchanged. Full verification: **4,033 CTest cases, zero failures,
  seven existing Vulkan skips** (2026-10-13).
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
- `[x]` **Scripting** — UVScript (`Engine/Runtime/UVScript`): parser, type checker,
  bytecode VM, engine binding with reload on save, and a text editor. The node-graph
  scripting module was removed. Collections and tuples have landed (typed lists, maps,
  tuples, `for`-in, unpacking, native-code parity). `[ ]` No cross-node calls, no debugger.
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
`Networking`, `Renderer`, `Serialization`, `VFX`, `Objects/2D`, `Objects/AI`. Two of
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
| 0.1 | Scalar utility header — `Pi`, `DegToRad`, `RadToDeg`, `LerpUVE`, `ClampUVE`, `SmoothStepUVE`, `ApproximatelyEqualUVE` | `Core/Math` | removes duplicated copies in gizmo, animation and physics code | the three existing copies are deleted and call sites use the shared header - landed (Oct 2026): player/character/world-env/collider/navmesh/renderer/perception runtime literals plus the editor panel/clip-editing literals migrated; viewport-core stays on `std::numbers` by contract |
| 0.2 | `FixedArrayUVE<T,N>`, `SmallVectorUVE<T,N>`, `SpanUVE` conventions | `Core/Containers` (new build entry) | every bounded list in the engine | `LightListUVE` and the frame-task graph caps are expressed with it - landed (Oct 2026): `LightListUVE` alias + scheduler `m_tasks` on `FixedArrayUVE`, task dependencies on inline `SmallVectorUVE` (spans via `AsSpanUVE`) |
| 0.3 | `StdAllocatorUVE<T>` adaptor | `Core/Memory` | makes the existing allocators reachable from any container | a `std::vector` in a test allocates through `PoolAllocatorUVE` and the tracker sees it - landed: `VectorAllocatesThroughPool_TrackerSeesAllocationAndRelease` |
| 0.4 | `StringIdUVE` interned name | `Core/Strings` | reflection lookups, name-keyed maps | `TypeMetadataEntryUVE` type ids are ids, not strings - landed: `typeId` is `StringIdUVE` |
| 0.5 | `HandleTableUVE<T>` generational slot map | `Core/Containers` | replaces three hand-rolled versions | at least one of `VoiceHandleUVE` / entity slots is rebuilt on it, with tests unchanged - landed: both audio devices back voices with `HandleTableUVE` |

## Tier 1 — completing the value layer

| # | Item | Module | Unblocks | Done when |
|---|---|---|---|---|
| 1.1 | `Vector4UVE`, `Matrix3x3UVE` | `Core/Math` | correct normal matrices; shader-facing vec4 | renderer's normal-matrix path uses `Matrix3x3UVE` - landed (ComputeNormalMatrixUVE) |
| 1.2 | `TrsUVE` TRS value type with compose/inverse | `Core/Math` | scene graph, prefabs, gizmo drag, physics interpolation | at least the gizmo drag path composes transforms as values - landed (Oct 2026): translate drags go through `TryInverseTransformDirectionUVE`, rotate drags through the new `TryInverseTransformRotationUVE` (parent-frame conjugation as one `TrsUVE` value op, byte-identical to the quaternion sandwich it replaces) |
| 1.3 | `ColorUVE` with explicit linear/display distinction | `Core/Math` | removes ad-hoc float triples; makes the HDR path type-safe | `LightComponentUVE` and `MaterialAssetUVE` use it - landed |
| 1.4 | `RectUVE`, integer vectors | `Core/Math` | UI layout, pixel coordinates | `UIImageComponentUVE` uses `RectUVE`; `ViewportRectUVE` is reconciled with it - landed (old RHI struct retired for RectIntUVE) |
| 1.5 | Reflection **factory** pointer on `TypeMetadataEntryUVE` | `Object` | generic deserialization; editor add-by-type-name | one component round-trips through the serializer without hand-written code - landed in spirit (generic path runs via the registry; createDefaultInstance exists but has no callers yet) |
| 1.6 | Component registry (type ↔ name/id) | `Component` | serializer stops needing per-component edits | adding a component requires no `SceneSerializerUVE` change - landed (31 migrated, registry live) |
| 1.7 | Hashing utilities (`HashCombineUVE`, a named non-cryptographic hash) | `Core/Utilities` | replaces the private fingerprint hash and hand-written `std::hash` specialisations | `AssetContentFingerprintUVE` is built on it - landed (Fnv1a64UVE; HashCombineUVE used by pose graph, archetypes, mesh renderer) |
| 1.8 | Endian-explicit binary buffer read/write | `Core/Utilities` | makes every save and asset format portable | round-trip test asserts byte-identical output on a simulated byte-swap - landed (SimulatedByteSwap_PeerReemitsByteIdenticalOutput) |

## Tier 2 — resource layer

| # | Item | Module | Unblocks | Done when |
|---|---|---|---|---|
| 2.1 | Rasterizer state on `PipelineDescUVE` — cull, winding, fill, depth bias, **depth-compare function** | `RHI/RHI` + all three backends | shadow bias; removes the editor's direct GL depth-function call | the gizmo overlay expresses its depth mode through the pipeline - PARTIAL (verified Oct 2026 by source read): RHI side landed — rasterizer tail (cull/winding/fill/bias/depth-compare) on PipelineDescUVE + PipelineBinaryDescUVE, all three backends validate/apply (GL state, VK ToVk* incl. depthCompareOp, Null validation); the done-when consumer is still open — and audited (Oct 2026) as a design pass, not a small migration: both consumers live in host-independent `univex/` viewport-core (raw GL by contract, cannot see the RHI), so expressing their depth state through pipelines needs a layering decision first |
| 2.2 | Sampler object + descriptor | `RHI/RHI` + backends | point filtering, wrapping, anisotropy | a texture can be sampled point-filtered - landed (verified Oct 2026 by source read): SamplerDescUVE (mag/min/mip/wrapU/V/W/maxAnisotropy) + SamplerFilterUVE::Point threaded through all three backend devices; GL maps Point to GL_NEAREST and GL creation tests cover Point filters |
| 2.3 | Texture dimensionality — array layers, cubemaps | `RHI/RHI` + backends | image-based lighting; cascade arrays | a cubemap is created and sampled - landed (verified Oct 2026 by source read): TextureTypeUVE::Cubemap + Texture2DArray with arrayLayers, creation-time shape validation (cube: square + 6 layers), CubemapFaceUVE order matching HW face order, render-into-layer (colorLayer/depthLayer); GL cube paths + VK maxImageDimensionCube query; cube coverage in the GL/VK device tests |
| 2.4 | Multiple render targets on `RenderPassDescUVE` | `RHI/RHI` + backends | any deferred/G-buffer path | a two-attachment pass records and replays - landed (verified Oct 2026 by source read): ColorAttachmentUVE slots 1..3 on RenderPassDescUVE (gap-rejecting); VulkanRenderDeviceUVETest.MultiTargetCaptureAndSample_RendersPerTargetColors records a multi-target pass and samples per-target colors, plus Null/GL attachment coverage |
| 2.5 | `StoreOpUVE` to pair with `LoadOpUVE` | `RHI/RHI` + backends | tiler efficiency; explicit resolve/discard | every pass declares both - landed (verified Oct 2026 by source read): StoreOpUVE::{Store,DontCare} sits next to its LoadOp on location 0, depth, and every extra slot; Null/GL command buffers validate both per slot and VK maps to VkAttachmentStoreOp at render-pass creation |
| 2.6 | Per-joint animation channels in `AnimationClipAssetUVE` + a skeleton asset | `Asset` | the entire skeletal animation pipeline, and four data-only nodes | a glTF skinned mesh animates — landed (verified Oct 2026 by source read): AnimationAssetBoneTrackUVE per-joint channels, skeleton_asset_uve, glTF skin converter + tests (reversed skin order, u16/u32 joints, inverse-bind defaults), TrySkinMeshUVE output tests (two-joint blend, normal rotation); renderer CPU-skins posed skeletons per 3.1; confirm the literal glTF eyeball on a GL run |
| 2.7 | Remaining PBR texture slots on `MaterialAssetUVE` | `Asset` | full PBR materials | metallic/roughness/emissive maps render - landed: slots on the asset, renderer binds slots 12/13, lit_shadowed_3d samples them (B metallic, G roughness); confirm pixels on a GL run |
| 2.8 | Debug line/shape renderer | `RHI/RenderSystems` | visualising bounds, frusta, contacts — pays for itself on the first spatial bug | a bounding box can be drawn from one call - landed (verified Oct 2026 by source read): DebugRendererUVE::DrawBox3D(AabbUVE, color) emits the 12-line box from one call (+ DrawLine3D/DrawSphere3D/DrawRect2D over a 16K-line batch); the header cites this item's done-when |

## Tier 3 — systems that are blocked, not missing

Each of these has its infrastructure already built; what is missing is the
consumer. They are cheap relative to how large they look.

| # | Item | Blocked on | Note |
|---|---|---|---|
| 3.1 | Animation runtime — clip sampler, skinning pass, engine tick | 2.6 | Landed (verified Oct 2026 by source read): step fns in Objects/Animation for sequencer + authored graph, SyncAnimationUVE ticks every frame and physics step, renderer_3d CPU-skins posed skeletons to the GPU. GPU compute skin (mesh_skin_compute + mesh_skin.glsl) built but unwired: perf follow-up, GL-side. |
| 3.2 | Parallel gameplay systems | Tier 0 containers | PARTIAL (Oct 2026): ThreadPoolUVE is live in assets, particles and shaders; JobGraphUVE/FrameSchedulerUVE built and tested but unwired. No defined consumer until frame scheduling or Tier 4 gameplay exists - do not start without one. |
| 3.3 | Audio mixer routing | — | Landed (verified Oct 2026 by source read): AudioSystemUVE::UpdateUVE applies group multipliers to every live voice per frame; component mixerGroup syncs incl. live reassignment |
| 3.4 | Script bytecode execution | — | Landed (verified Oct 2026): the bytecode VM executes (editor instant reload) and release builds generate C++23 from the same bytecode; the visual graph was removed. Decision made: execute. |
| 3.5 | Plugin host | — | Landed: NativePluginHostUVE loads/unloads native plugins (dlopen + LoadLibrary), negotiates the ABI, registers manifests, rolls back failed loads; tested incl. real .so fixtures |
| 3.6 | Occlusion culling | 2.8 (to see it) and GPU queries | Still open (Oct 2026): frustum culling live (camera, decal, compute); occlusion queries explicitly deferred, GL-side (needs a GPU query path in a real backend). |

## Tier 4 — genuinely new subsystems

Out of scope for this document beyond naming them; each needs its own design pass
and each has a `ROADMAP.md` section already: networking transport and replication
(started Oct 2026: endpoint seam + loopback pair, UDP sockets (connected non-blocking datagram endpoints with getaddrinfo hostname resolution plus a multi-client listener with accept-replay, POSIX + Winsock), session core with handshake/disconnect/timeout/redial-resume, replication core with fixed-rate position snapshots plus authority roles, relevancy filters, delta compression, an ownership-transfer handshake, and the replicated-entity Gameplay wiring, simulated latency/loss conditions, RPC core with async ValueUVE calls over owned sessions plus the rpc.* script surface and rpc_response event, prediction core with multi-pawn ticked inputs with per-input dt, authoritative states, snap-plus-replay reconcile, server relay fan-out with observers, and the predicted-pawn Gameplay adapter, lag compensation with rewound overlap/ray hit resolves plus the Gameplay shot/ray wiring (prediction-ack view ticks, fail-closed resolves, pre-tracker merge, ray damage with health events, fractional-tick interpolation between consecutive snapshots, rewound world scale, engine-tick auto-record with a submitted-shot inbox, a shot-intent protocol carrying view tick plus fire fraction with an inspector mirror and a one-call client firing API), a packet inspector decoding captured session datagrams to structured views plus one-line log lines, plus a bounded capture ring behind the editor's Capture dock (filterable packet list with per-packet decode and hexdump) fed by live session taps (every sent/received datagram with direction, pump clock, and channel byte) proven by a tap-fed capture demo over mixed loopback traffic, a Profiler dock over live diagnostics captures (spans/counters/breadcrumbs with a Net category and per-stats network emitters, proven by the fan-out sample profiling itself), spatial interest with grid relevance plus priority tiers, per-entity rate overrides, and per-viewer fan-out budgets over tier-distance ranking feeding the replicator's relevancy filters through the interest-relevancy Gameplay adapter (proven by an interest fan-out sample: one server, three budgeted bots, divergent views), and non-position property frames sharing the replication session (pushed values over a shared wire-value codec with RPC), and channel multiplexing (one session, tagged channels, shared-session features), and a room-scoped server relay (opaque fan-out by peer room tags with lobby-room binding and pairwise interest mutes; replication and RPC ride through; relay.* script surface for rooms, mutes, removals, stats, and client send/poll), and a matchmaking lobby (room registry over direct RPC sessions with roster-change push plus a typed client facade and the lobby.* script surface), and a headless dedicated-server sample composing it all (one server, three loopback bot clients; plus a lag-comp server demo: one lagged bot firing rewound shots/rays at a walking victim with pre-tracker merge: lobby rooms, room-bound relay shouts, predicted pawns as replication Owners, a server-owned patrol NPC, an untagged local walker)), navmesh and pathfinding (landed per ROADMAP §7.3), an AI
layer (landed per §7.3 except crowd sim), a gameplay framework (landed per §7.2 —
`Hitbox3D` strikes apply damage through `ApplyHitboxStrikeToHealthUVE`), a 2D
pipeline, VFX authoring, terrain, and localization (UI side landed:
`LocalizationServiceUVE` + auto-translate; wider asset scope open).

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
Arm3D,
InteractionArea3D, LevelStreamer3D, WorldPartition3D, VisibilityRegion3D,
LODGroup3D, Occluder3D, and partially ReflectionProbe3D. That correction belongs
in that file and is not folded in here.

## 2026-10-13 implementation ledger

Work is based on merged `master` commit `e26f91d`, whose Foundation blob is
`394f389235e2606e5703d21ae45031a55bed795d`. Earlier interrupted-run outputs were not
present in the current checkout; this baseline was rerun, not assumed.

- Debian 12 / GCC 12.2 / CMake 3.31.10, Debug with the existing `-Werror` policy.
  Native desktop GL/Vulkan SDK/runtime dependencies are unavailable locally; the
  existing CPU-only configuration is used. Tool installations and logs stay in ignored `build/`.
- Baseline build: 948 steps completed. CTest: 3,998 cases, one UDP setup failure,
  seven existing `ComputeWorkloadsVulkanUVETest` capability skips. Focused parallel
  execution reproduced contention for fixed ports 41741/41742 before RPC logic.
- `Test/Support/ctest_resource_locks.cmake` declares the shared ports after discovery.
  No socket behavior or assertion was changed. All 14 UDP cases passed 20 repeats;
  the subsequent full build/CTest passed: **3,991 passed, 7 expected skips, 0 failures**.
- Initial math-boundary, vocabulary, panel-include and whitespace checks passed.
  The remaining markers above are work to resolve, not a completion claim.

### Native baseline reconciliation (2026-10-13)

The saved Foundation branch was integrated with `master` through `9f0a039`, retaining upstream
UDP pair isolation and default-framebuffer routing rather than reimplementing them. The strict
CPU-only baseline was rerun: **4,103 cases, zero failures, seven existing Vulkan skips**.
Native CI run `38426263544` reported one failure, the same shadow pixel test failing on master.
The full lighting shader keeps `uReflectionProbeCube` active; the direct-RHI test left that
samplerCube at unit 0 alongside a sampler2D. Its fixture now mirrors the real renderer's separate
cubemap unit, with a real complete cube texture and additional GL-error assertions. Neither
shadow pixel assertion nor either shader copy was weakened/changed. Local full CTest and all
16 embedded/physical shader-parity cases pass; native re-execution is required before marking
this correction or the pending device-operations batch fully verified.
