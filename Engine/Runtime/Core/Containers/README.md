# uve_containers

Fixed-capacity and small-buffer-optimized sequence containers (`UVE::Containers`, Tier 0
foundation). Header-only — the types are class templates, so `uve_containers` is an INTERFACE
target with no compiled sources.

## Types

| Type | Storage | Overflow | Use when |
|------|---------|----------|----------|
| `FixedArrayUVE<T, N>` | Inline, `N` slots | `PushBackUVE` asserts; check `FullUVE()` first | The cap is a compile-time contract (frame tasks, extracted lights) |
| `SmallVectorUVE<T, N>` | Inline `N`, spills to heap (doubling) | Grows; never full | The common case fits inline but the unbounded case must work |
| `HandleTableUVE<T, H>` | Slot vector + free list, `H` handle concept | Unbounded; slots reuse, stale handles go dead | Destroy/recreate cycles must not alias (audio voices; entities later) |

Both track a live size, construct elements in place (no default-constructed fill), expose
`AsSpanUVE()` for read paths, and support range-based-for directly.

## Conventions (Tier 0, item 0.2)

- **Bounds are asserted, not thrown.** Element access uses `UVE_ASSERT` (debug) and is unchecked
  in release — a violation is a programming error, never a runtime error code. Callers facing
  possibly-overflowing input check `FullUVE()` and take their own fallible path (the exemplar is
  `FrameTaskGraphUVE::AddTaskUVE` returning `CapacityExceeded`).
- **Read paths take `std::span<const T>`.** Functions that inspect container contents without
  owning them accept a span (the exemplar is the scheduling module's `FindTaskIndexUVE`); call
  sites pass `container.AsSpanUVE()`. Spans decouple readers from the concrete container.
- **`begin`/`end` are lowercase** (STL interop for range-based-for); every other member carries
  the `UVE` suffix.
- **No shrinking, no unchecked growth surprises.** `FixedArrayUVE` never grows;
  `SmallVectorUVE` doubles from its inline capacity and keeps a spilled buffer until destruction.
- **Handles are a concept, not a type.** `HandleTableUVE` is generic over any handle providing
  `FromIndexAndGenerationUVE`/`IndexUVE`/`GenerationUVE`/`NextGenerationUVE` (`SlotHandleUVE`
  is the default); packed layouts like the voice handle's 20+12-bit u32 own their own width
  and wrap rules.

## Consumers

- `uve_scheduling`: `FrameTaskGraphUVE` stores its bounded task list in a
  `FixedArrayUVE<FrameTaskDefinitionUVE, kMaximumTasksUVE>` and exposes it as a span.
- `uve_render_systems`: `LightListUVE` is a `FixedArrayUVE<LightDataUVE, kMaxLightsUVE>` — only
  extracted lights occupy slots; the renderer zero-fills the unused fixed shader slots.
- `uve_audio`: both audio devices keep their voices in a `HandleTableUVE<..., VoiceHandleUVE>`
  — one table per device holding voice + playback state together, with slot reuse across
  create/destroy cycles.

## Tests

`Test/Core/Containers/` — per-type suites covering inline/spill behavior, copy/move semantics
(including self-assign and moved-from-empty), span interop, and construction/destruction
accounting via a lifetime probe (the manual slot storage must neither leak nor double-destroy).
