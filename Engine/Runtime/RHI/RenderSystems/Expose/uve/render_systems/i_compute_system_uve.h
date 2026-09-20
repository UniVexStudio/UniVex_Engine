// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/render_systems/compute_dispatch_desc_uve.h"
#include "uve/render_systems/compute_program_desc_uve.h"

namespace UVE::Render {

/// A lifetime-to-date account of observable IComputeSystemUVE work, in the spirit of
/// Renderer3DFrameDiagnosticsUVE: every counter names evidence the system actually has.
/// "Recorded" means the dispatch's commands were written into a command buffer - on the immediate
/// GL backend that is also execution, on deferred backends it is what their replay will execute.
/// Rejected/skipped distinguish authoring-time validation failures (enqueue) from programs that
/// died between enqueue and execute; nothing here claims a GPU finished anything.
struct ComputeSystemDiagnosticsUVE final {
    std::uint32_t programsCreated = 0U;
    std::uint32_t programCreationsFailed = 0U;
    std::size_t livePrograms = 0U;
    std::uint32_t dispatchesEnqueued = 0U;
    std::uint32_t dispatchesRejected = 0U;
    std::uint32_t dispatchesSkipped = 0U;
    std::uint32_t dispatchesRecorded = 0U;
    std::uint32_t dispatchesCleared = 0U;
};

/// IComputeSystemUVE is the engine-level compute consumer layer (the spec's `ComputeSystemUVE`,
/// Part 7.2 - the family RenderSystemUVE/CameraSystemUVE/LightSystemUVE also belong to; ROADMAP
/// section 2's "GPU compute shader support ... engine-level consumer layer"). It is where engine
/// and game code hand compute work over without touching RHI primitives: the system owns compute
/// PROGRAM lifecycle on its injected IRenderDeviceUVE (create, destroy, track what is live) and a
/// dispatch QUEUE that ExecuteQueuedDispatchesUVE() records into a command buffer in enqueue
/// order, outside pass markers - the same portable flow the RHI compute slice (M5a/M5b) settled
/// on: compute first, graphics passes after, barriers handled by each backend.
///
/// The M5a/M5b RHI slices made DispatchUVE, storage buffers and storage images real on the GL,
/// Vulkan and Null backends; this layer is their engine-side consumer. Deliberately NOT part of
/// this slice: EngineCoreUVE/Renderer3DUVE frame wiring (the queue's per-frame caller), the
/// ShaderAssetUVE-reserved Compute stage's asset-driven program path, and the concrete workloads
/// the ROADMAP names (culling, particle simulation, skinning) - each lands as its own slice on
/// top of the contracts here.
///
/// Thread-safety: not thread-safe. Every method must be called only from the main engine thread,
/// matching IRenderSystemUVE's and EngineCoreUVE's single-threaded frame-loop contract.
class IComputeSystemUVE {
public:
    virtual ~IComputeSystemUVE() = default;

    /// Compiles and links one compute program through the injected device and returns its
    /// pipeline handle - the same shared handle domain graphics pipelines live in, so
    /// BindPipelineUVE accepts it unchanged. Any failure (empty source, backend compile or link
    /// error, a backend or driver without compute support) returns kInvalidPipelineHandleUVE,
    /// logs a warning naming `desc.debugName`, counts programCreationsFailed, and - when
    /// `outInfoLog` is non-null - stores the backend's info log there. The intermediate RHI
    /// shader object is destroyed on every path before returning; the pipeline handle is the
    /// only object callers ever track, and this system is its owner until DestroyProgramUVE().
    [[nodiscard]] virtual PipelineHandleUVE CreateProgramUVE(const ComputeProgramDescUVE& desc,
                                                             std::string* outInfoLog = nullptr) = 0;

    /// Destroys a program this system created. A handle that is invalid, foreign, or already
    /// destroyed is a logged no-op - this system never passes handles it does not own to the
    /// device. Queued dispatches still referencing the program are not erased here; they are
    /// skipped loudly when the queue next executes (see ExecuteQueuedDispatchesUVE()).
    virtual void DestroyProgramUVE(PipelineHandleUVE program) = 0;

    /// True only for programs CreateProgramUVE() returned and DestroyProgramUVE() has not taken
    /// back. A handle another IRenderDeviceUVE consumer created may be perfectly live at the RHI
    /// and still report false here: liveness in this system means ownership by this system.
    [[nodiscard]] virtual bool IsProgramLiveUVE(PipelineHandleUVE program) const noexcept = 0;

    /// Validates `desc` and copies it onto the queue for the next ExecuteQueuedDispatchesUVE().
    /// Returns false - queueing nothing, warning, and counting dispatchesRejected - when the
    /// program is not live in this system, any group count is zero, any storage-buffer or texture
    /// binding holds an invalid handle, or any uniform write has an empty name. Structural
    /// validation is this layer's; whether a handle is still live at the RHI, or a uniform name
    /// actually exists on the program, remains the backends' loud-warning contracts at replay.
    [[nodiscard]] virtual bool EnqueueDispatchUVE(const ComputeDispatchDescUVE& desc) = 0;

    /// Records every queued dispatch into `commands` in enqueue order - per dispatch:
    /// BindPipelineUVE, storage-buffer binds, texture binds, uniform writes, DispatchUVE - then
    /// clears the queue and returns how many dispatches were recorded. A dispatch whose program
    /// died between enqueue and execute is skipped (warning + dispatchesSkipped), never recorded
    /// against a dead handle. Contract: call OUTSIDE pass markers - before the frame's first
    /// BeginRenderPassUVE or after its last EndRenderPassUVE - so the portable flow the RHI
    /// documents is the flow that actually gets submitted.
    virtual std::size_t ExecuteQueuedDispatchesUVE(ICommandBufferUVE& commands) = 0;

    /// How many dispatches are currently queued (enqueued minus executed minus cleared).
    [[nodiscard]] virtual std::size_t GetQueuedDispatchCountUVE() const noexcept = 0;

    /// Drops every queued dispatch without recording anything - the deliberate "this frame's GPU
    /// work is abandoned" path (a skipped render step, a teardown between enqueue and execute).
    /// Counted in dispatchesCleared so an abandoned queue is visible in diagnostics rather than
    /// silently evaporating.
    virtual void ClearQueueUVE() = 0;

    /// The lifetime-to-date counters described by ComputeSystemDiagnosticsUVE. Reference stays
    /// valid for the system's lifetime; the values change with every call above.
    [[nodiscard]] virtual const ComputeSystemDiagnosticsUVE& GetDiagnosticsUVE() const noexcept = 0;
};

} // namespace UVE::Render
