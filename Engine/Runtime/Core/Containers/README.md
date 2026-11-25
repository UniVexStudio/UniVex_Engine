# uve_containers

Fixed-capacity and small-buffer-optimized sequence containers (`UVE::Containers`, Tier 0
foundation). Header-only — the types are class templates, so `uve_containers` is an INTERFACE
target with no compiled sources.

## Types

| Type | Storage | Overflow | Use when |
|------|---------|----------|----------|
| `FixedArray<T, N>` | Inline, `N` slots | `PushBack` asserts; check `Full()` first | The cap is a compile-time contract (frame tasks, extracted lights) |
| `SmallVector<T, N>` | Inline `N`, spills to heap (doubling) | Grows; never full | The common case fits inline but the unbounded case must work |
| `HandleTable<T, H>` | Slot vector + free list, `H` handle concept | Unbounded; slots reuse, stale handles go dead | Destroy/recreate cycles must not alias (audio voices; entities later) |

Both track a live size, construct elements in place (no default-constructed fill), expose
`AsSpan()` for read paths, and support range-based-for directly.

## Conventions (Tier 0, item 0.2)

- **Bounds are asserted, not thrown.** Element access uses `UVE_ASSERT` (debug) and is unchecked
  in release — a violation is a programming error, never a runtime error code. Callers facing
  possibly-overflowing input check `Full()` and take their own fallible path (the exemplar is
  `FrameTaskGraph::AddTask` returning `CapacityExceeded`).
- **Read paths take `std::span<const T>`.** Functions that inspect container contents without
  owning them accept a span (the exemplar is the scheduling module's `FindTaskIndex`); call
  sites pass `container.AsSpan()`. Spans decouple readers from the concrete container.
- **`begin`/`end` are lowercase** (STL interop for range-based-for); every other member carries
  the `UVE` suffix.
- **No shrinking, no unchecked growth surprises.** `FixedArray` never grows;
  `SmallVector` doubles from its inline capacity and keeps a spilled buffer until destruction.
- **Handles are a concept, not a type.** `HandleTable` is generic over any handle providing
  `FromIndexAndGeneration`/`Index`/`Generation`/`NextGeneration` (`SlotHandle`
  is the default); packed layouts like the voice handle's 20+12-bit u32 own their own width
  and wrap rules.

## Consumers

- `uve_scheduling`: `FrameTaskGraph` stores its bounded task list in a
  `FixedArray<FrameTaskDefinition, kMaximumTasks>` and exposes it as a span.
- `uve_render_systems`: `LightList` is a `FixedArray<LightData, kMaxLights>` — only
  extracted lights occupy slots; the renderer zero-fills the unused fixed shader slots.
- `uve_audio`: both audio devices keep their voices in a `HandleTable<..., VoiceHandle>`
  — one table per device holding voice + playback state together, with slot reuse across
  create/destroy cycles.

## Tests

`Test/Core/Containers/` — per-type suites covering inline/spill behavior, copy/move semantics
(including self-assign and moved-from-empty), span interop, and construction/destruction
accounting via a lifetime probe (the manual slot storage must neither leak nor double-destroy).
