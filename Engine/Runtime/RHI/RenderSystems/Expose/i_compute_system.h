// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "i_command_buffer.h"
#include "compute_dispatch_desc.h"
#include "compute_program_desc.h"

namespace UVE::Render {

/// A lifetime-to-date account of observable IComputeSystem work, in the spirit of
/// Renderer3DFrameDiagnostics: every counter names evidence the system actually has.
/// "Recorded" means the dispatch's commands were written into a command buffer - on the immediate
/// GL backend that is also execution, on deferred backends it is what their replay will execute.
/// Rejected/skipped distinguish authoring-time validation failures (enqueue) from programs that
/// died between enqueue and execute; nothing here claims a GPU finished anything.
struct ComputeSystemDiagnostics final {
    std::uint32_t programsCreated = 0U;
    std::uint32_t programCreationsFailed = 0U;
    std::size_t livePrograms = 0U;
    std::uint32_t dispatchesEnqueued = 0U;
    std::uint32_t dispatchesRejected = 0U;
    std::uint32_t dispatchesSkipped = 0U;
    std::uint32_t dispatchesRecorded = 0U;
    std::uint32_t dispatchesCleared = 0U;
};

/// IComputeSystem is the engine-level compute consumer layer (the spec's `ComputeSystem`,
/// Part 7.2 - the family RenderSystem/CameraSystem/LightSystem also belong to; ROADMAP
/// section 2's "GPU compute shader support ... engine-level consumer layer"). It is where engine
/// and game code hand compute work over without touching RHI primitives: the system owns compute
/// PROGRAM lifecycle on its injected IRenderDevice (create, destroy, track what is live) and a
/// dispatch QUEUE that ExecuteQueuedDispatches() records into a command buffer in enqueue
/// order, outside pass markers - the same portable flow the RHI compute slice (M5a/M5b) settled
/// on: compute first, graphics passes after, barriers handled by each backend.
///
/// The M5a/M5b RHI slices made Dispatch, storage buffers and storage images real on the GL,
/// Vulkan and Null backends; this layer is their engine-side consumer. Deliberately NOT part of
/// this slice: EngineCore/Renderer3D frame wiring (the queue's per-frame caller), the
/// ShaderAsset-reserved Compute stage's asset-driven program path, and the concrete workloads
/// the ROADMAP names (culling, particle simulation, skinning) - each lands as its own slice on
/// top of the contracts here.
///
/// Thread-safety: not thread-safe. Every method must be called only from the main engine thread,
/// matching IRenderSystem's and EngineCore's single-threaded frame-loop contract.
class IComputeSystem {
public:
    virtual ~IComputeSystem() = default;

    /// Compiles and links one compute program through the injected device and returns its
    /// pipeline handle - the same shared handle domain graphics pipelines live in, so
    /// BindPipeline accepts it unchanged. Any failure (empty source, backend compile or link
    /// error, a backend or driver without compute support) returns kInvalidPipelineHandle,
    /// logs a warning naming `desc.debugName`, counts programCreationsFailed, and - when
    /// `outInfoLog` is non-null - stores the backend's info log there. The intermediate RHI
    /// shader object is destroyed on every path before returning; the pipeline handle is the
    /// only object callers ever track, and this system is its owner until DestroyProgram().
    [[nodiscard]] virtual PipelineHandle CreateProgram(const ComputeProgramDesc& desc,
                                                             std::string* outInfoLog = nullptr) = 0;

    /// Destroys a program this system created. A handle that is invalid, foreign, or already
    /// destroyed is a logged no-op - this system never passes handles it does not own to the
    /// device. Queued dispatches still referencing the program are not erased here; they are
    /// skipped loudly when the queue next executes (see ExecuteQueuedDispatches()).
    virtual void DestroyProgram(PipelineHandle program) = 0;

    /// True only for programs CreateProgram() returned and DestroyProgram() has not taken
    /// back. A handle another IRenderDevice consumer created may be perfectly live at the RHI
    /// and still report false here: liveness in this system means ownership by this system.
    [[nodiscard]] virtual bool IsProgramLive(PipelineHandle program) const noexcept = 0;

    /// Validates `desc` and copies it onto the queue for the next ExecuteQueuedDispatches().
    /// Returns false - queueing nothing, warning, and counting dispatchesRejected - when the
    /// program is not live in this system, any group count is zero, any storage-buffer or texture
    /// binding holds an invalid handle, or any uniform write has an empty name. Structural
    /// validation is this layer's; whether a handle is still live at the RHI, or a uniform name
    /// actually exists on the program, remains the backends' loud-warning contracts at replay.
    [[nodiscard]] virtual bool EnqueueDispatch(const ComputeDispatchDesc& desc) = 0;

    /// Records every queued dispatch into `commands` in enqueue order - per dispatch:
    /// BindPipeline, storage-buffer binds, texture binds, uniform writes, Dispatch - then
    /// clears the queue and returns how many dispatches were recorded. A dispatch whose program
    /// died between enqueue and execute is skipped (warning + dispatchesSkipped), never recorded
    /// against a dead handle. Contract: call OUTSIDE pass markers - before the frame's first
    /// BeginRenderPass or after its last EndRenderPass - so the portable flow the RHI
    /// documents is the flow that actually gets submitted.
    virtual std::size_t ExecuteQueuedDispatches(ICommandBuffer& commands) = 0;

    /// How many dispatches are currently queued (enqueued minus executed minus cleared).
    [[nodiscard]] virtual std::size_t GetQueuedDispatchCount() const noexcept = 0;

    /// Drops every queued dispatch without recording anything - the deliberate "this frame's GPU
    /// work is abandoned" path (a skipped render step, a teardown between enqueue and execute).
    /// Counted in dispatchesCleared so an abandoned queue is visible in diagnostics rather than
    /// silently evaporating.
    virtual void ClearQueue() = 0;

    /// The lifetime-to-date counters described by ComputeSystemDiagnostics. Reference stays
    /// valid for the system's lifetime; the values change with every call above.
    [[nodiscard]] virtual const ComputeSystemDiagnostics& GetDiagnostics() const noexcept = 0;
};

} // namespace UVE::Render
