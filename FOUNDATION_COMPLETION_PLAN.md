# Foundation completion plan

**Status (2026-10-14, end of the originating session):** WP1 done (`0003`), WP2a–e done
(`0004`–`0008`) — built and verified green. **`0010`–`0014` are UNCOMPILED DRAFTS**: WP3
(`0010`), WP4 (`0011`), WP7 (`0012`), WP5c + the WP5 audit verdicts (`0013`), WP6 step 1
(`0014`) were written after the sandbox lost GitHub egress (no toolchain, see §4). Null /
renderer / shader / asset / test translation units passed a real `g++ -std=c++23 -fsyntax-only`
pass with the project's `-Werror` warning set against stand-in fmt/gtest headers; the GL and
Vulkan backend TUs and their tests could NOT be checked. **The next session must build, fix
and run every test before any of these commits is called done.** Still open after the drafts:
WP5 slices 5a/5b/5d/5e (5b+5e share one fix and block Vulkan shadows), WP6 step 2 (outline/
backdrop/render-pass sites + pixel evidence). Delivered as the patch series in `handoff/` on
top of `origin/master` `10455561`.

Target: close every open marker that is **inside the scope of `FOUNDATION.md`** (Parts I–II and
the Part V tiers 0–3) and nothing beyond it. System-level gaps that `FOUNDATION.md` only names
and hands to `ROADMAP.md` (image-based lighting, terrain, GPU/Hi-Z occlusion as a feature,
networking, a platform service, editor UI polish) stay out.

Reference state: `origin/master` at `10455561` (merge of PR #10). All file paths and status
markers below were read from that tree, not from the session base `c8019f6`, which predates
the last 59 commits and lacks 12.4k lines of the RHI work the open items build on. **The work
must be based on master** — see §4.

Rules carried over from `FOUNDATION.md` and applied to every work package:

- Survey first: read the real implementation before designing; the plan below says what was
  read and what still has to be read at the start of each package.
- Update `FOUNDATION.md` **in the same change** that moves an item (its own rule), and keep
  `ROADMAP.md` consistent where it mirrors the item.
- `[x]` is earned only with dedicated tests that lock more than one case **and** a real-driver
  run (this sandbox has software GL 3.3→4.6 and a Vulkan 1.3 CPU ICD, so native evidence can be
  produced locally and confirmed by the graphics CI).
- Backends never pretend: missing capability returns `Unsupported`, never a silent success.
- No third-party engine or product names in docs; strict `-Werror` Debug build stays green.

---

## 1. What is still open on master (Foundation scope only)

| ID | Open item (where in `FOUNDATION.md`) | Marker | Package |
|---|---|---|---|
| O1 | Descriptor table "where the RHI is thin": `TextureFormatUVE` has no single/two-channel formats; `TextureDescUVE` has no depth (3D), sample count or usage flags; `BufferDescUVE` has no memory/CPU-access/lifetime hint; `PrimitiveTopologyUVE` has no points/strips; `VertexAttributeFormatUVE` is float-only (§9 table) | table gaps | WP2 |
| O2 | Rasterizer-state **consumer** — gizmo overlay / grid / selection outline drive raw GL depth state inside `univex_viewport_gl`; Tier 2.1 done-when ("the gizmo overlay expresses its depth mode through the pipeline") unmet; audited as a design pass (§9 bullet 1, Part V 2.1) | `[~]` / PARTIAL | WP6 |
| O3 | Submission-level queue sync — semaphores as `SubmitUVE` parameters (§9 "Timeline semaphores" + the closing paragraph) | `[ ]` | WP4 |
| O4 | Vulkan device still milestone-sliced: >1 descriptor set, load-op semantics beyond clear on the swapchain path, per-op depth compare, device-local staging (upload/readback warn), sampling `Depth32Float`, offscreen passes skipped on pre-1.3 (§9 "Command recording and backends") | `[~]` | WP5 |
| O5 | Vulkan occlusion queries `Unsupported` by design because device-level begin/end cannot bracket draws; Tier 3.6 occlusion culling consumer still open even though GL queries and the debug renderer (its two blockers) exist (§9 "Queries", Part V 3.6) | explicit gap / open | WP3 |
| O6 | Items implemented but **awaiting native verification**: Vulkan blit/mipgen fixtures, Vulkan timestamp queries, stencil state backend tests, render-graph GL pixel/worker-retirement + Vulkan CPU-FIFO/GPU-completion tests, geometry-stage pixel probe (§9, §10) | `[/]` ×5 | WP1 |
| O7 | FBX materials converted but not published as `.uvmat` with texture references resolved to GUIDs (§8 importer reconciliation) | `[~]` / `[/]` | WP7 |
| — | Tier 3.2 parallel gameplay systems: "do not start without a consumer" — respected, not scheduled | PARTIAL | none |

Everything else in Parts I (math, containers, memory, threading, utilities, reflection), II §7–8
(asset lifecycle, asset types other than O7) and III (components) is already `[x]` on master.

---

## 2. Work packages, in dependency order

### WP1 — Convert the five `[/]` items to `[x]` with real-driver evidence (size S)

Goal: the cheapest honest progress — run the already-written native tests on real drivers and
either mark them verified or turn them into bug reports with repros.

- Run on this sandbox's software GL and CPU Vulkan: `Test/RHI/Vulkan/vulkan_render_device_uve_tests.cpp`
  (blit/mipgen, timestamps, stencil creation, CPU-FIFO/GPU-completion), the GL stencil pixel-mask and
  depth/stencil discard tests, the render-graph GL pixel/worker-retirement tests, and
  `GlRenderDeviceUVETest.GeometryStageChangesRasterizedPositionAndSurvivesShaderDeletion`.
- Known sandbox-only deviations to classify, not paper over: `GenerateMipmapsRebuildsEveryLevelWithTheDownsampledAverage`
  (blit rounding on the CPU ICD — decide whether the test tolerance or the backend is wrong),
  `StencilPipelineAndDepthStencilTextureCreateOnDynamicDevice` (the ICD lacks `D24_UNORM_S8_UINT`
  → the backend must report `Unsupported` for the format and the test must skip, not fail),
  `FoundationQueriesMeasureRealVisibilityAndGpuTimestamps` (software-GL occlusion-query result).
- Cross-check the graphics CI run ids already cited in the doc (`38518413266`, `38515088834`, …)
  from the new session (`gh` works there) and record the green run id per item.
- Done when: each of the five paragraphs in `FOUNDATION.md` carries a run id + case names and
  its marker is `[x]`, or a concrete defect is filed and fixed in a follow-up package.

### WP2 — Close the descriptor gaps (size M, split in five independent slices)

All slices follow the Tier 2 precedent: descriptor + validation in `RHI/RHI`, then Null
(validate/record), GL, Vulkan; CPU tests in `Test/RHI/RHI`, device tests in `Test/RHI/OpenGL`
and `Test/RHI/Vulkan`.

- **2a Texture formats** — add `R8Unorm`, `RG8Unorm`, `R16Float`, `RG16Float`, `R32Float`,
  `RG32Float`, `RGBA32Float` to `TextureFormatUVE` (`render_resource_descs_uve.h`). Shared
  helpers `BytesPerTexelUVE` / `IsDepthFormatUVE` / `IsCompressedFormatUVE` so upload-size and
  readback validation stop special-casing. GL internal/format/type tables, Vulkan `VkFormat`
  map + per-format feature queries, Null validation. First consumer: the SSAO/bloom path or the
  shadow mask can move off RGBA8/RGBA16F where a one/two-channel target is correct (survey which;
  if none is clean, the format lands with tests only and the table says so). sRGB already exists as
  `TextureColorSpaceUVE::Srgb`; the doc's "no sRGB" cell is stale and gets corrected.
- **2b Texture usage + 3D** — `TextureUsageUVE` flags (`Sampled`, `RenderTarget`, `Storage`,
  `TransferSrc`, `TransferDst`) defaulting to today's implicit "everything" so existing callers
  are unchanged; Vulkan derives `VkImageUsageFlags` from them instead of a catch-all; Null
  rejects use outside the declared flags. `TextureTypeUVE::Texture3D` with `depth` on the desc
  (GL `glTexImage3D`, VK `VK_IMAGE_TYPE_3D`, upload/readback validation). `sampleCount` is
  **deferred** to a later design (it needs resolve attachments on `RenderPassDescUVE`); the
  table says so explicitly instead of leaving the cell blank.
- **2c Buffer memory hint** — `BufferMemoryUVE { DeviceLocal, Upload, Readback }` and
  `BufferUpdateFrequencyUVE { Static, Dynamic, Stream }` on `BufferDescUVE`. GL maps to
  `GL_*_DRAW/READ` usage; Vulkan picks memory properties and uses a staging copy for
  `DeviceLocal` uploads/readbacks (this removes the two "device-local" warnings listed under O4);
  Null records the hint. Tests: Vulkan upload→readback round trip through a device-local buffer.
- **2d Topologies** — `Points`, `LineStrip`, `TriangleStrip` in `PrimitiveTopologyUVE`; GL
  enables `GL_PROGRAM_POINT_SIZE` for point pipelines; Vulkan `VkPrimitiveTopology`; Null
  validation of index/vertex counts per topology. Consumer: the debug renderer's point and
  polyline paths, which currently expand to line lists.
- **2e Vertex attribute formats** — `UByte4Norm` (colors), `UShort4`/`UInt4` (joint indices),
  `Short2Norm`/`Half2`/`Half4` (packed UVs/normals) in `VertexAttributeFormatUVE` with size/
  alignment helpers; GL `glVertexAttrib(I)Pointer` paths; Vulkan `VkFormat` map. Consumer:
  the GPU compute-skin path (built, unwired per Tier 3.1) needs integer joint indices.

Each slice updates the §9 descriptor table row it closes.

### WP3 — Recorded queries + the occlusion-culling consumer (size M) — DRAFTED in `0010` (uncompiled)

Design: queries move from device-level bracketing to **recorded commands**:
`ICommandBufferUVE::BeginQueryUVE/EndQueryUVE/WriteTimestampUVE` become `RecordedCommandUVE`
variants (device-level entry points stay for timestamps and for compatibility, delegating to a
one-shot buffer). This is exactly the condition the doc states for Vulkan occlusion
(`vkCmdBeginQuery`/`vkCmdEndQuery` must bracket the caller's own draws in one command buffer), so
Vulkan `AnySamplesPassed` becomes real instead of `Unsupported`; GL replays `glBeginQuery`
around the recorded draws; Null records the exact sequence and still never invents results.

Consumer (Tier 3.6): a conservative GPU occlusion pass in `Renderer3DUVE` behind a capability
check — frame N draws AABB proxies (color/depth writes off) inside queries, frame N+1 skips a
draw only when its query returned "0 samples"; `NotReady`/`Unsupported` never culls. Diagnostics
counter (`occlusionCulledDraws`), debug visualization through `DebugRendererUVE::DrawBox3D`, and
the existing CPU occluder path untouched. Tests: Null command-sequence pins; GL and Vulkan pixel
tests where an object fully behind a wall is culled on the second frame and the visible one is
not; a one-frame-latency pin (an object that becomes visible is drawn the frame after).

Done when: §9 "Queries" says Vulkan occlusion is real, Part V 3.6 is "Landed" with the test
names, and `ROADMAP.md` §1.2 mirrors it (GPU/Hi-Z occlusion stays `[ ]` — a different feature).

#### WP3 — implementation survey (2026-10-14, read against master `10455561` + this series)

Facts that fix the shape of the change (file:line as of the survey):

- **RHI surface.** `ICommandBufferUVE` already has one optional, result-returning op —
  `MemoryBarrierUVE(MemoryBarrierMaskUVE)` with a non-pure default of `Unsupported`
  (`i_command_buffer_uve.h:53-58`). The three new ops follow that exact pattern so no existing
  implementer or test spy breaks:
  `RenderOperationResultUVE BeginQueryUVE(QueryHandleUVE)`, `EndQueryUVE(QueryHandleUVE)`,
  `WriteTimestampUVE(QueryHandleUVE)`, all defaulting to `Unsupported`. Validation rules shared
  by all backends: unknown handle → `InvalidHandle`; `Begin/End` on a `Timestamp` handle or
  `WriteTimestamp` on an `AnySamplesPassed` handle → `InvalidArgument`; nested `Begin`, `End`
  without `Begin`, or an `End` outside the render pass the `Begin` was recorded in →
  `InvalidState` (Vulkan requires begin and end in the same subpass, VUID
  `vkCmdEndQuery-None-01923`, and GL has no reason to be looser). Device-level
  `IRenderDeviceUVE::BeginQueryUVE/EndQueryUVE/WriteTimestampUVE` keep their current meaning
  (`i_render_device_uve.h:127-147`): GL keeps bracketing immediately, Vulkan timestamps keep the
  transient submission, and Vulkan device-level occlusion stays `Unsupported` — the recorded path
  is the one that can bracket draws, so the §9 sentence changes from "unsupported" to
  "device-level unsupported, recorded supported".
- **Null** (`Engine/Runtime/RHI/Null/Internal/null_command_buffer_uve.h`, spy vector): add
  `BeginQueryCommandUVE{handle}`, `EndQueryCommandUVE{handle}`, `WriteTimestampCommandUVE{handle}`
  to the `RecordedCommandUVE` variant (`recorded_command_uve.h:97`). The device already records
  `QueryOperationUVE{handle, kind, begin}` (`null_render_device_uve.cpp:612-643`); the command
  buffer validates against the device's query table and `GetQueryResultUVE` keeps returning
  `NotReady` — a recorded bracket never produces a number.
- **GL** issues GL calls at record time (`gl_command_buffer_uve.h:21-22`): the recorded ops are
  `glBeginQuery(GL_ANY_SAMPLES_PASSED)` / `glEndQuery` / `glQueryCounter(GL_TIMESTAMP)` sharing
  `state.activeOcclusion` and the per-query `issued` flag with the device path
  (`gl_render_device_uve.cpp:1678-1712`) but WITHOUT the device path's
  `FinishDeviceOperationUVE()` — a `glFinish` inside a recorded stream would serialise every draw.
- **Vulkan.** `VulkanCommandBufferUVE` (`vulkan_render_device_uve.cpp:2700`) records a private
  op variant replayed under `std::visit` (`~5541`) inside `SubmitUVE`; add three ops there.
  `vkCmdResetQueryPool` is illegal inside a render pass, so the replay prologue (before the
  first `vkCmdBeginRenderPass`) resets every pool the buffer references once; `vkCmdBeginQuery`
  uses flags `0` (an imprecise occlusion query is still zero iff no sample passed, which is the
  `AnySamplesPassed` contract, so `occlusionQueryPrecise` is not required). `CreateQueryUVE
  (AnySamplesPassed)` (`~6807`) creates a one-entry `VK_QUERY_TYPE_OCCLUSION` pool next to the
  existing timestamp pools; `GetQueryResultUVE` (`~6870`) reads it with `VK_QUERY_RESULT_64_BIT`
  and normalises to `value = samples != 0` so GL (`GL_ANY_SAMPLES_PASSED` boolean) and Vulkan
  report the same thing; `VK_NOT_READY` → `NotReady` as today. **Reuse hazard:** re-recording a
  handle in frame N+1 resets it in queue order, which destroys frame N's result for any reader
  that comes later — the consumer therefore double-buffers handles per item (parity = frame
  index & 1: read `parity ^ 1`, record `parity`). `caps.occlusionQueries` flips to `Native`
  when the queue family supports graphics (always, for this device) — the existing test
  `TimestampQueriesReportRealTicksAndRefuseOcclusion` must change its occlusion expectation to
  "create succeeds, device-level begin/end still `Unsupported`".
- **Pipeline state for proxies.** `PipelineDescUVE` (`render_resource_descs_uve.h:974`) has
  `depthTestEnabled`, `depthWriteEnabled`, `blendMode {Opaque, SourceAlphaOver, Additive,
  Multiply}`, `cullMode`, `depthBiasEnabled`, but **no colour write mask**. Preferred: add
  `bool colorWriteEnabled = true` (GL `glColorMask`, Vulkan
  `VkPipelineColorBlendAttachmentState::colorWriteMask = 0`, Null records it) — a one-field,
  three-backend change with its own Null/GL/Vulkan pin. Fallback if that is judged out of scope:
  `SourceAlphaOver` with a fragment shader writing alpha 0 leaves the destination untouched.
  Proxy pipeline: depth test on with `DepthCompareUVE::LessOrEqual`, depth write off, colour
  write off, `CullModeUVE::None`, topology `Triangles`, one shared unit-cube vertex/index buffer
  (36 indices) placed per item through the model matrix. New built-in shader
  `shaders/occlusion_proxy.glsl` needs its physical twin under
  `Engine/Runtime/RHI/Shader/built_in/` and a byte-parity row.
- **Where it plugs into `Renderer3DUVE`** (`renderer_3d_uve.cpp`): the frame is a
  `RenderGraphUVE` rebuilt per frame (`~3472-3700`): shadow cascade passes
  (`kShadowCascadeCountUVE`), `"MainColor"` (`~3518`), `"ProceduralSky"`, `"SSAO"`, bloom.
  Mesh extraction applies frustum + CPU occluder rejection at `~1938-1952`
  (`IsOccluder3DAabbDrawHiddenUVE(occluders, viewPosition, worldBounds)`), primitives at
  `~2059`. The proxy draws are recorded at the END of the `"MainColor"` pass callback (same
  render pass instance as the opaque draws, so the depth attachment just written is the one
  tested and the Vulkan same-subpass rule holds for free). Shadow cascades have their own
  extraction and instanced draws (diagnostics `i_renderer_3d_uve.h:~122`): GPU occlusion results
  are applied ONLY to the main-camera mesh list, never to cascades — a mesh hidden from the
  camera still casts shadows.
- **Conservative skip rule (frame N+1 for a mesh item):** draw unless ALL hold —
  (1) `caps.occlusionQueries == Native` and last frame's bracket calls returned `Success`;
  (2) the item's `parity ^ 1` query reads `Success` with `value == 0`;
  (3) the view-projection used when that query was recorded equals the current one within an
  epsilon and the item's world AABB is unchanged (stale-result guard: moving cameras and moving
  objects never pop); (4) the camera position is outside the item's AABB expanded by the near
  plane distance (a near-clipped proxy would report 0 samples for a visible object).
  Culled items are still proxied every frame so they come back one frame after they become
  visible. `NotReady`, `Unsupported`, any error, or a Null device → draw. Only mesh items in
  WP3 (primitives, decals, particles untouched). CPU occluders stay exactly as they are.
- **Diagnostics / debug:** `Renderer3DFrameDiagnosticsUVE` (`i_renderer_3d_uve.h:72+`) gains
  `occlusionQueriesIssued`, `occlusionCulledDraws`, `occlusionResultsNotReady`; a renderer
  setting next to `frustumCullingFrozen` (`i_renderer_3d_uve.h:153`) — `gpuOcclusionEnabled`
  (default on; the capability gate makes it a no-op on Null) and `showOcclusionCulledBounds`
  (draws the culled AABBs through `DebugRendererUVE::DrawBox3D`, `debug_renderer_uve.h:79`).
- **Tests to add (names fixed now so the doc rows can cite them):**
  `Test/RHI/null_render_device_uve_tests.cpp` — `NullCommandBufferRecordsQueryBracketsInOrder`,
  `NullCommandBufferRejectsBadQueryBrackets`, `NullRecordedQueriesNeverInventResults`;
  `Test/RHI/OpenGL/gl_render_device_uve_tests.cpp` —
  `RecordedOcclusionQueryCountsOnlyVisibleDraws` (NO timestamp in the same test: the sandbox's
  softpipe zeroes every occlusion result after a `glQueryCounter`), `RecordedTimestampsAreMonotonic`;
  `Test/RHI/Vulkan/vulkan_render_device_uve_tests.cpp` —
  `RecordedOcclusionQueryDistinguishesHiddenAndVisibleTriangles` (baked `kTriangle*` shaders,
  offscreen colour+depth target in the colour format `Renderer3DUVE` already uses on Vulkan;
  occluder at depth 0.2 with depth write, hidden triangle at 0.8 inside query A, visible triangle
  at 0.1 inside query B; `WaitIdleUVE` then A==0, B==1) and
  `RecordedOcclusionQueryIsNotReadyUntilCompletion`;
  renderer integration (GL + Vulkan fixtures) — `Renderer3DOcclusionCullsHiddenMeshWithoutChangingImage`
  (wall + cube; frame 1 both drawn and `occlusionQueriesIssued == 2`; frame 2 same camera →
  `occlusionCulledDraws == 1` and the readback is byte-identical to frame 1; frame 3 camera moved →
  0 culled by the stale guard; cascade draw counts constant) and on Null —
  `Renderer3DOcclusionPassIsSkippedOnNull` (counters 0, recorded draw list identical to pre-WP3).
- **Doc rows touched in the same change:** `FOUNDATION.md` §9 "Queries" (`~914-926`), the
  2026-10-14 evidence paragraph (`~954-960`), presence reconciliation (`~1068-1071`), Part V row
  3.6 (`~1422`) → "Landed (conservative GPU occlusion; Hi-Z stays `[ ]`)", a ledger entry;
  `ROADMAP.md` §1.2 mirror.
- **Effort:** one working day in a sandbox with the headless toolchain: RHI half (three
  backends + pins), consumer half (pass, policy, diagnostics, integration pins, docs).

### WP4 — Submission-level queue sync (size S/M) — DRAFTED in `0011` (uncompiled; final API `SubmitWithSyncUVE` + `SubmitDescUVE`)

Design: `SubmitDescUVE { span<SemaphoreWaitUVE{handle, value, stageMask}> waits;
span<SemaphoreSignalUVE{handle, value}> signals; FenceHandleUVE fence; }` and a new
`SubmitUVE(std::unique_ptr<ICommandBufferUVE>, const SubmitDescUVE&)` overload (the old
signature forwards an empty desc). Semaphores reuse the existing timeline objects — Vulkan via
`VkTimelineSemaphoreSubmitInfo`; binary semaphores are not introduced because a timeline wait/
signal pair as a submit parameter is strictly more expressive and already exists. GL (single
queue, in-order) validates handles and completes waits/signals in submission order so the
counters stay truthful; Null records the exact parameters. Tests: Null record pins; Vulkan: a
submit that signals value N is observed by `GetSemaphoreCounterUVE` after the fence, a wait on a
future value blocks until a second submit signals it, stale/destroyed handles are rejected.

Done when: the `[ ]` sentence in §9 is replaced by the contract and the test names.

#### WP4 — implementation survey (2026-10-14)

- **Where submissions really go.** `VulkanRenderDeviceUVE::SubmitUVE`
  (`vulkan_render_device_uve.cpp:4931-4953`) only pushes the recorded ops into
  `frameSubmissions` under `submissionMutex`; `PresentUVE` replays every submission of the frame
  into ONE native command buffer inside one shared swapchain render pass and issues ONE
  `vkQueueSubmit` (`~6367-6375`: waits `imageAvailableSemaphore`, signals
  `renderFinishedSemaphore`, fences `inFlightFence`). Consequence: per-`SubmitUVE` queue sync
  can only be honoured at **frame-batch granularity** on Vulkan — the waits of all submissions
  in a frame gate that frame's `vkQueueSubmit`, the signals of all submissions are performed
  when that batch completes. That is still true submission-level sync (never earlier than
  asked, never claimed before the GPU reached it) and the doc paragraph must say exactly this.
  Splitting the batch per submission would mean closing and reopening the shared swapchain
  pass with `LOAD_OP_LOAD`; not worth it inside Foundation.
- **Contract shape.** `SubmitDescUVE { std::span<const SemaphoreWaitUVE> waits;
  std::span<const SemaphoreSignalUVE> signals; }` with `SemaphoreWaitUVE { SemaphoreHandleUVE
  semaphore; std::uint64_t value; }` and the same for signals (timeline values; no binary
  semaphore type is introduced — the §9 sentence's "binary semaphores" is satisfied by a
  timeline pair, which is strictly more expressive and already exists). No fence member:
  `InsertFenceUVE` (`i_render_device_uve.h:78-88`, Vulkan `~6668-6690`, a zero-command
  `vkQueueSubmit` with a fence) already is the one-shot "after everything submitted so far"
  fence, and its comment already says a retained backend refuses it while the FIFO is
  non-empty. New overload `[[nodiscard]] RenderOperationResultUVE SubmitUVE(
  std::unique_ptr<ICommandBufferUVE>, const SubmitDescUVE&)` — result-returning so validation
  is visible; the old `void SubmitUVE(buffer)` forwards an empty desc. Validation on every
  backend: unknown semaphore → `InvalidHandle`; a signal value not greater than the current
  counter → `InvalidArgument` (timeline monotonicity, same rule `NullRenderDeviceUVE::
  SignalSemaphoreUVE` enforces at `null_render_device_uve.cpp:545-556`); the same handle in
  waits and signals of one desc → `InvalidArgument`.
- **Vulkan.** `timelineSemaphoresEnabled` gates everything (`caps.timelineSemaphores` Native,
  `~6612`); `VkTimelineSemaphoreSubmitInfo` is not used anywhere yet (grep) — the Present-time
  `VkSubmitInfo` gets it in `pNext` with value arrays aligned to the wait/signal arrays (the two
  binary swapchain semaphores get `0` entries, which the spec ignores for binary semaphores);
  `pWaitDstStageMask` for timeline waits = `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`. The per-frame
  wait/signal lists are collected under the submission lock and cleared with the FIFO. Host
  signalling stays `vkSignalSemaphore` (`~6957-6984`). **Hang guard:** the inFlight wait is
  `vkWaitForFences(..., UINT64_MAX)` (`:6129`); a frame whose waits can never be satisfied would
  hang the next Present forever — add a bounded wait (e.g. 10 s) on that path that turns the
  device inert with a warning instead; pin it with a test only if it can be made deterministic
  (a wait on value 1 of a semaphore nobody signals, then `PresentUVE` twice).
- **GL.** The GL device has no semaphore implementation at all (`grep SemaphoreUVE
  gl_render_device_uve.cpp` is empty; `caps.timelineSemaphores` stays Unsupported) — the GL
  overload validates the handle set it has (none) and returns `Unsupported` for any non-empty
  desc, `Success` (plain submit) for an empty one. That is the truthful GL answer; do not
  simulate counters there.
- **Null.** Records a new `SubmitOperationUVE { waits, signals }` next to the existing
  `SignalSemaphoreOperationUVE`/`WaitSemaphoreOperationUVE`
  (`device_operations_uve.h:176-200`), advances nothing (no invented GPU progress — the
  existing rule), and validates as above.
- **Tests.** Null: `NullSubmitDescRecordsWaitsAndSignalsVerbatim`,
  `NullSubmitDescRejectsBadSemaphores`. Vulkan (`vulkan_render_device_uve_tests.cpp`, after the
  mid-file helpers): `SubmitSignalIsObservedAfterTheFrameCompletes` (submit with signal 3,
  `PresentUVE`, `WaitIdleUVE`/fence, `GetSemaphoreCounterUVE == 3`),
  `SubmitWaitBlocksTheQueueUntilHostSignal` (semaphore at 0; submit waits 5 and signals a second
  semaphore to 1; `PresentUVE`; second counter stays 0; `SignalSemaphoreUVE(5)` from the host;
  fence; second counter becomes 1), `SubmitDescRejectsDestroyedSemaphore`. GL:
  `SubmitDescIsUnsupportedWithoutTimelines`. Doc: replace the `[ ]` sentence at
  `FOUNDATION.md:950-953` with the batch-granularity contract and these names.

### WP5 — Vulkan parity on the remaining honest gaps (size M/L) — audit verdicts + 5c DRAFTED in `0013`; 5a/5b/5d/5e OPEN

The doc's list is partly stale (per-op depth compare now lives on the pipeline; device-local
staging is closed by WP2c). First step is a one-page audit of `vulkan_render_device_uve.cpp`
(6,986 lines) and `vulkan_command_buffer_uve.cpp` listing every `UVE_WARNING`/`Unsupported`
path with a verdict. Expected slices after the audit:

- **Sampling `Depth32Float`** — depth textures created with `SAMPLED_BIT`, tracked transitions
  to `DEPTH_STENCIL_READ_ONLY_OPTIMAL` when bound, matching sampler rules; this is what unblocks
  the shadow-map path on Vulkan. Pixel test mirroring the GL shadow test.
- **Load-op semantics beyond clear on the classic/swapchain path** — `Load`/`DontCare` mapped
  to `VkAttachmentLoadOp` for the presented image, with a pixel test that a `Load` pass keeps
  the previous frame's contents.
- **More than one descriptor set** — per-set layouts from reflection, bound per bind-group
  packet; test with a pipeline whose reflection spans two sets.
- **Pre-1.3 offscreen passes** — classic `VkRenderPass`/`VkFramebuffer` fallback for offscreen
  targets; only if the audit shows a reachable device class that needs it, otherwise the doc
  states the 1.3 floor as a decision.

Done when: the `VulkanRenderDeviceUVE` bullet in §9 reads `[x]` or lists only decisions, not gaps.

#### WP5 — the audit (2026-10-14; every `Unsupported` return and warning site in the Vulkan device)

67 sites were listed mechanically (`grep -n "Unsupported\|UVE_WARNING"`) plus 60 one-shot
`WarnOnceUVE` messages in the replay. Verdicts:

| Site(s) | What it is | Verdict |
|---|---|---|
| `CreatePipelineUVE` reflection `:4203-4207` — "descriptor set N — the M2b layout contract is set-0-only" | pipeline creation **fails** for SPIR-V that uses set ≥ 1 | Real gap. Slice **5a**: per-set `VkDescriptorSetLayout`s from reflection, bind-group packets carry the set index; test with a two-set shader baked via the SPIR-V recipe. Alternative decision: keep set-0-only and say so (all engine shaders are set-0, GL has no sets) — acceptable only if written as a decision. |
| `BeginRenderPassUVE` offscreen `:5625` — "only RGBA8Unorm textures in the swapchain's format can be offscreen color targets" | WP2a's `R8/RG8/RGBA16F/RGBA32F` targets cannot be attached on Vulkan (they can on GL) | Real gap, the largest. Slice **5b**: graphics pipelines are created today against the swapchain colour format; make the pipeline's colour/depth formats part of its cache key (`VkPipelineRenderingCreateInfo` per target format, lazily created variants) so any `RenderTarget`-usage format is attachable. Pixel test: render into an `RGBA16Float` target, read it back after a blit to RGBA8. |
| Present `:6254` — Load/DontCare on the swapchain path "bakes a full-frame clear" | swapchain pass always clears | Real gap, small. Slice **5c**: `VkAttachmentLoadOp` from the first swapchain pass of the frame; pixel test: frame 2 with `Load` keeps frame 1's pixels. |
| `BindTextureUVE` depth `:5206` — Depth32Float sampling needs the dynamic-rendering arm | works on 1.3 devices (SwiftShader, lavapipe); classic arm only | **Stale in the doc**: §9 lists "sampling Depth32Float" as a gap without the qualifier. Rewrite as a 1.3-floor decision (see last row). |
| "per-op depth compare" in §9 | `PipelineDescUVE::depthCompare` exists (`render_resource_descs_uve.h:990`) | **Stale**: already closed; delete from the list. |
| Replay `:6024` — `BindUniformBufferUVE` "stays a no-op in M2c" | GL binds the UBO (`gl_command_buffer_uve.cpp:1015` → `glBindBufferBase`); only caller is `bind_group_uve.cpp:77` | Real parity gap not in the doc's list. Slice **5d**: descriptor binding for `Uniform`-usage buffers by slot through the same per-tuple set cache storage buffers use; test: a UBO-fed colour matches on Null (record), GL and Vulkan. |
| `:5589` depth-only offscreen passes refused | GL allows depth-only FBOs (shadow maps attach depth only?) — check `Renderer3DUVE`'s cascade targets first | If the engine's shadow passes bind a colour target anyway, write it as a decision; else slice **5e** (colourless `VkRenderingInfo`). |
| `:5840`, `:5874`, `:5940`, `:5026`, `:5206` — classic (pre-1.3) arm: offscreen passes skipped, compute skipped, storage images, depth sampling | Vulkan < 1.3 devices | **Decision, not gap**: no reachable test device is pre-1.3 (SwiftShader 1.3, lavapipe 1.3). Record "core 1.3 dynamic rendering is the floor; the classic arm presents and draws the swapchain only" in §9 and delete the per-feature caveats. |
| `:5182` layered storage images, `:5194` depth as storage, `:5173` compressed as storage | explicit sinks with warnings | Decisions already stated in the warnings; mirror them in §9 in one sentence. |
| `:5121` 1 MiB frame uniform ring, `:5312` descriptor pool exhaustion | capacity limits with loud warnings | Decisions; mention the numbers. |
| `MemoryBarrierUVE :2712`, `CreateSemaphoreUVE :6922`, `SignalSemaphoreUVE :6963`, `CreateQueryUVE :6816/6820`, `BeginQueryUVE :6844` | feature-gated `Unsupported` (no barriers, no timeline feature, occlusion device-level) | Correct behaviour; WP3 changes the occlusion one. |
| Blit/mips `:7102-7120`, `:7207-7209` | format-capability refusals | Correct and documented (WP1/WP2b). |
| `:7297-7326` readback refusals | validation of a 2D RGBA8 helper | Correct. |
| Everything else (`CreateBufferUVE`, `UpdateBufferUVE`, swapchain/present-mode fallbacks, validation-layer availability, fallback textures, `SubmitUVE` foreign buffer) | error reporting | Not gaps. |

Order inside WP5: 5b (unblocks the most), 5d, 5c, 5a-or-decision, 5e-or-decision, then the
§9 rewrite. Done-when unchanged: the `[~]` bullet at `FOUNDATION.md:1014` reads `[x]` or lists
decisions only.

### WP6 — Overlay depth state through RHI pipelines (size L) — candidate B chosen; step 1 DRAFTED in `0014` (gizmo + grid); step 2 + pixel evidence OPEN

Facts read: `univex_viewport_core` is host-independent (no GL), `univex_viewport_gl` holds the
raw-GL `GizmoRenderer`, `InfiniteGridRenderer`, `SelectionOutlineRenderer` (the `glDepthFunc`/
`glDepthMask` calls at `GizmoRenderer.cpp:232-256`, `InfiniteGridRenderer.cpp:98-134`,
`SelectionOutlineRenderer.cpp:163`), and `univex_viewport_engine_bridge` is the first layer that
links the engine (`uve_world`, `uve_core`).

Design to validate in the session (two candidates, pick one after measuring the surface area):

- **A. Overlay renderers move onto the RHI** — new `univex_viewport_rhi` (or inside the
  engine bridge): gizmo/grid/outline geometry built by viewport-core as today, uploaded and
  drawn through `IRenderDeviceUVE` with `PipelineDescUVE` carrying `depthTest/depthWrite/
  DepthCompareUVE/cull/blend`; `univex_viewport_gl` keeps only the standalone demo path or is
  retired. Meets the done-when literally; largest change.
- **B. Depth state becomes data** — viewport-core emits `OverlayRasterStateUVE` (depth test,
  write, compare, cull, blend) per overlay batch; `univex_viewport_gl` applies it from one
  table-driven function and the engine bridge translates the same value into an RHI pipeline
  when the host renders overlays through the device. Smaller, but the raw GL path survives for
  the demo/capture tools, so the doc would still say "expressed as data, applied by a pipeline
  where one exists".

Done when: no `glDepthFunc`/`glDepthMask` call remains in overlay code paths used by the editor,
pixel tests (headless capture) prove the gizmo overlay depth modes are unchanged, Tier 2.1 reads
"landed", §9 bullet 1 becomes `[x]`.

#### WP6 — implementation survey (2026-10-14)

- **Complete site list** (the plan's three files were incomplete): `GizmoRenderer.cpp:244-260`
  (5 calls), `InfiniteGridRenderer.cpp:99,133`, `SelectionOutlineRenderer.cpp:183,203`,
  `StudioBackdropRenderer.cpp:152-202` (6), `app/ViewportRenderPass.cpp:208-337` (7) — all
  under `Engine/Editor/Viewport/` — plus `EditorCore/Internal/mesh_thumbnail_renderer_uve.cpp:
  319,380` (thumbnails, not an overlay; leave it and say so).
- **Who draws the editor overlay:** `Engine/App/Internal/editor/editor_viewport_backends_uve.cpp`
  (`ViewportPanelBackendUVE`, `:549`; `ViewportRenderPass::Create` at `:758`) → `app/
  ViewportRenderPass.cpp` (target `univex_viewport_app_support`) → the `univex_viewport_gl`
  renderers (`Viewport/CMakeLists.txt:81-100`). `univex_viewport_engine_bridge` links
  `univex_viewport_app_support` + `uve_world` + `uve_core` (`:106-118`). The editor overlay is
  therefore GL-only today; on a Vulkan device the engine viewport renders but these overlays
  cannot exist — the real motivation for candidate A.
- **What the RHI already offers for candidate A:** `PipelineDescUVE` carries depth test/write/
  compare, blend mode, cull mode and depth bias (`render_resource_descs_uve.h:974-1000`);
  `DebugRendererUVE` (`debug_renderer_uve.h`) already draws lines/boxes/points through the
  device on every backend (WP2d added points). Gizmo lines carry widths 1.5–2.0 px
  (`editor_viewport_backends_uve.cpp:181,236,291`): core GL ignores `glLineWidth > 1` and Vulkan
  needs `wideLines`, so an RHI overlay path must expand wide lines into screen-space quads
  (or accept 1 px) — decide before measuring candidate A.
- **Measure first, then choose:** count the public surface of the three renderers that
  `ViewportRenderPass.cpp` uses (`grep -n "gizmo_\.\|grid_\.\|outline_\." app/
  ViewportRenderPass.cpp`) — if it is a handful of `Render(view, proj, mesh)` calls, A is a
  bridge-level reimplementation on `IRenderDeviceUVE` (new `univex_viewport_rhi`) and the GL
  target stays only for `tools/headless_capture.cpp`; otherwise B (`OverlayRasterStateUVE`
  as data, one table-driven apply function in the GL target, the same value mapped to a
  `PipelineDescUVE` in the bridge). Pixel evidence for either: the existing headless capture
  tool plus the frame-dump shim (`Engine/Tools/headless_framedump.c`) before/after, byte-equal
  for every gizmo depth mode.

### WP7 — Publish FBX materials as `.uvmat` with GUID texture references (size S/M) — DRAFTED in `0012` (uncompiled; the importer gains `RegisterDatabaseAwareImporterUVE` instead of changing every importer signature)

`ReadFbxMaterialsUVE` already yields `FbxMaterialUVE`. Add the publication step beside mesh
publication: write one `.uvmat` per material, resolve each authored relative texture filename
against the import's sibling files through the asset database (unresolved → empty reference +
import diagnostic, never a fabricated GUID), and reference the materials from the published
`.uvmodel`. Tests: multi-material file publishes N `.uvmat`, resolution hit/miss, re-import
idempotence. Done when the §8 paragraph reads `[x]`.

#### WP7 — implementation survey (2026-10-14)

- **Entry point:** `ImportFbxSourceUVE(sourcePath, destinationPath, settings)`
  (`fbx_importer_uve.cpp:22-45`) converts with `ConvertFbxMeshUVE` and publishes ONE
  `.uvmodel` through `Detail::PublishAssetAtomicallyUVE`. Importers are plain functions
  registered by extension (`i_asset_importer_uve.h:94`, `asset_importer_uve.cpp:230-233`) and
  do **not** receive the asset database — `AssetImporterUVE::ImportUVE(source, destination,
  IAssetDatabaseUVE&, settings)` registers the destination afterwards. Material GUIDs must be
  known before the mesh is written, so the importer function signature gains
  `IAssetDatabaseUVE&` (one mechanical change across the registered importers and their tests);
  `RegisterUVE` is idempotent per path (`i_asset_database_uve.h:51`: "deterministically selects
  the smallest existing GUID for that lexical identity"), which gives re-import idempotence
  for free.
- **Material payload:** `FbxMaterialUVE` (`fbx_mesh_converter_uve.h:82-100`: name, colours/
  factors, `albedoTextureFile`, `normalTextureFile`, `emissiveTextureFile`,
  `ambientOcclusionTextureFile`) → `MaterialAssetUVE` (`material_asset_uve.h:26`; GUID slots
  for albedo/normal/AO, emissive colour, metallic, roughness, transparency) written with
  `SaveMaterialAssetUVE` (`material_asset_uve.cpp:163`) exactly as `mtl_importer_uve.cpp:46`
  does. File naming: `<model-stem>.<sanitised-material-name>.uvmat` beside the `.uvmodel`;
  empty names → `material_<index>`, duplicates → `_<n>` suffix; order = FBX material order
  (already pinned by the converter's multi-material test).
- **Texture resolution:** the database has no path→GUID query (`RegisterUVE`, `ResolveUVE`,
  `HasGuidUVE`, `GetRegisteredAssetsUVE` only). Resolve an authored relative filename against
  the import's sibling files: if `<source-dir>/<file>` exists AND a registered asset record
  (`GetRegisteredAssetsUVE()`, `AssetRecordUVE{guid, path}`) points at the published texture for
  it (same directory, same stem, `.uvtex`), reference that GUID; otherwise leave the slot
  invalid and emit an import diagnostic (never register or fabricate a GUID for a texture that
  was not imported). Put the resolver in one small helper so the MTL path (which still says
  "syntax-only until an explicit texture resolver", `mtl_material_converter_uve.cpp:103`) can
  adopt it later.
- **Mesh side:** `MeshAssetUVE` has no material references today (no `material*` field in
  `mesh_asset_uve.h`). Add `std::vector<AssetGuidUVE> materialGuids` (per FBX material slot),
  bump the envelope version in `SaveMeshAssetUVE`/`LoadMeshAssetUVE`, and keep loading old files
  (missing field → empty list); round-trip test.
- **Tests** (next to `Test/Core/Asset/fbx_material_converter_uve_tests.cpp`):
  `FbxImportPublishesOneUvmatPerMaterial` (multi-material fixture → N `.uvmat` + GUID list in
  the `.uvmodel`), `FbxImportResolvesSiblingTextureGuid` (hit) and
  `FbxImportLeavesUnresolvedTextureEmptyWithDiagnostic` (miss),
  `FbxImportIsIdempotentAcrossReimport` (same GUIDs, same files), `MeshAssetRoundTripsMaterialGuids`.
  Doc: the §8 sentence at `FOUNDATION.md:709-711` flips to `[x]` with these names.

---

## 3. Explicitly out of scope for this plan

- `ROADMAP.md`-level features named in Part IV/§10: prefiltered IBL, terrain, GPU/Hi-Z
  occlusion as a rendering feature, networking, platform service, MSAA (needs its own pass).
- Editor UI design work (e.g. the Animation Library window, file picker layout) — parked for a
  separate editor-polish plan.
- Tier 3.2 (job graph consumer) — the doc forbids starting it without a consumer.

---

## 4. Logistics

- **Base:** master. The session that produced WP1–WP2 is pinned to `c8019f6` with its remote
  access closed (PR #12 was closed), and on 2026-10-14 that sandbox also lost all GitHub egress
  (`github.com`, `api.github.com`, `codeload.github.com` refuse connections; only `pypi.org`,
  `files.pythonhosted.org` and `registry.npmjs.org` answer). Nothing can be pushed, and nothing
  can be built there either: the headless toolchain fetches Mesa/GLFW/GLEW from `codeload`, and
  the engine's own `FetchContent` dependencies (json, zlib, libjpeg-turbo, fmt, imgui,
  basis_universal, ufbx, miniaudio, Vulkan-Headers, SPIRV-Reflect, googletest) are all GitHub
  repositories. Execution therefore continues in a NEW coding session on a branch from master.
- **Carry-over:** everything is one mbox, `handoff/UniVex-foundation-WP1-WP2-all-in-one.patch`
  (nine commits on `10455561`: the PR #11 timeline-semaphore fix, the toolchain/fixture/gizmo
  commit, WP1, WP2a–WP2e, and this plan). Apply with
  `git checkout -b <branch> origin/master && git am UniVex-foundation-WP1-WP2-all-in-one.patch`;
  drop commit 0001 if PR #11 has been merged by then. `handoff/README.md` lists each commit and
  its evidence.
- **Environment:** `bash Engine/Tools/headless_gl_toolchain.sh` (≈45 min on two vCPUs for a cold
  `~/toolchain`, mostly Mesa; ≈2 min when it is intact), then the Ninja Debug configure/build
  (`-j2`, ≈25–30 min cold) and `ctest`.
- **Evidence per package:** focused test names + full `ctest` count on the sandbox drivers,
  plus the graphics CI run id once the branch is pushed; both go into `FOUNDATION.md` as the
  existing paragraphs do.
- **Order and cadence:** WP1 → WP2 (a,c,d first; b,e after) → WP3 → WP4 → WP5 → WP6 → WP7.
  Each slice is one commit with its doc update; nothing is committed without an explicit go.

---

## 5. Execution protocol for the next session (read before touching code)

1. **Set up and prove the baseline before any edit.** Apply the mbox, run
   `bash Engine/Tools/headless_gl_toolchain.sh`, `source ~/.univex_env`, configure/build as in
   `Engine/Tools/README.md`, then the four suites. Expected at the end of WP2b on the sandbox
   drivers: 0 compiler warnings; `uve_integration_tests` 2138/2138; `uve_core_tests` 2276 pass /
   3 skip / 1 known softpipe failure (`GlRenderDeviceUVETest.FoundationQueriesMeasureRealVisibilityAndGpuTimestamps`);
   `uve_engine_tests` 154/154; `uve_editor_tests` 338/338. Anything else is a setup problem —
   fix it first, never "adjust" a test to pass.
2. **Known non-failures, and only these:** (a) the softpipe occlusion-after-timestamp defect
   above (documented in `FOUNDATION.md`; passes on CI's llvmpipe); (b) two load-sensitive
   flakes that pass in isolation — `HotReloadUVETest.PollUVE_DetectsOnDiskChange*` and
   `EngineCoreUVETest.WindowedMode_UIOverlayRenders*`; re-run them alone before believing a red.
3. **How to run tests:** always from a scratch directory (binaries write files into the CWD),
   e.g. `mkdir -p /tmp/run && cd /tmp/run && <build>/Test/uve_core_tests --gtest_filter=... --gtest_brief=1`;
   rebuild the specific test target before citing a result; GL tests need
   `source ~/.univex_env`; Vulkan tests use the SwiftShader ICD the toolchain installs.
4. **Per work package:** survey (already written above for WP3–WP7) → implement Null, GL and
   Vulkan together → targeted tests on all three → full four-suite regression → the
   `FOUNDATION.md` rows for that package in the SAME commit (the doc's own rule) →
   `git format-patch` export after each commit so nothing lives only in the build tree.
   New embedded shaders need their physical twin under `Engine/Runtime/RHI/Shader/built_in/`
   and a byte-parity row. SPIR-V for Vulkan tests is baked with `glslangValidator -V` and
   embedded in `Test/RHI/Vulkan/spirv_test_shaders_uve.h` with the GLSL in a comment.
5. **Commit hygiene:** one commit per package/slice, message `rhi: ...` / `asset: ...` /
   `editor: ...` / `docs: ...`; never `git add -A`; `Screenshots/` and `handoff/` never enter a
   commit; keep the diff readable — no reformatting of untouched code.
6. **Landing:** push the new branch, wait for the graphics CI (llvmpipe/lavapipe) to be green,
   open the PR with the per-package test names and counts in the description. The user merges;
   do not merge yourself.
7. **Scope guard:** if a package needs something outside `FOUNDATION.md` (IBL, terrain, Hi-Z,
   MSAA, networking, editor UI redesign), stop and write it down as a decision instead of
   building it.
