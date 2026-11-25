// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "aabb.h"
#include "frustum.h"
#include "frustum_cull_compute.h"
#include "i_compute_system.h"
#include "i_render_device.h"
#include "render_resource_descs.h"

namespace UVE::Render {

/// The indirect-cull kernel's parameters, matching its std430 block. One int, in a storage buffer
/// for the same reason every other kernel's parameters are: SPIR-V has no non-opaque global
/// uniforms.
struct FrustumCullIndirectParamsGpu final {
    std::int32_t boxCount = 0;
};

/// A lifetime-to-date account of observable FrustumCullIndirect work. Note what is NOT here: a
/// visible-object count. This system's whole purpose is that the CPU never learns that number, and
/// a diagnostic that quietly read it back would reintroduce the very round trip the pass exists to
/// remove. `drawsPrepared` counts commands built, not objects drawn.
struct FrustumCullIndirectDiagnostics final {
    std::uint32_t cullsRequested = 0U;
    std::uint32_t cullsRejected = 0U;
    std::uint32_t cullsSkipped = 0U;
    std::uint64_t boxesTested = 0U;
    std::uint32_t drawsPrepared = 0U;
    std::uint32_t uploadFailures = 0U;
    std::uint32_t bufferReallocations = 0U;
};

/// FrustumCullIndirect is CS5's frustum cull with the CPU taken off the critical path.
///
/// CS5 answers "which of these boxes are visible" and hands the answer back, which means a full
/// GPU->CPU round trip before anything can be drawn - the one cost a culling pass is supposed to
/// avoid. This class instead leaves the answer in device memory in the exact shape a draw call
/// consumes: the `instanceCount` field of a `DrawIndexedIndirectCommand`, plus a compacted
/// buffer of surviving box indices for the vertex shader to index with `gl_InstanceID`. The caller
/// then records `DrawIndexedIndirect` (CS7) and the GPU draws exactly the survivors. Nothing on
/// the CPU is ever told how many there were.
///
/// That property is the feature, and it is also what makes this class awkward to test - so the
/// tests do the one honest thing available: they read the buffers back AFTERWARDS, purely as
/// observers, and check the compacted set against CS5's per-box visibility over the same inputs.
/// `Prepare()` itself performs no readback, and the diagnostics deliberately expose no visible
/// count, so no production path can come to depend on one.
///
/// Relationship to FrustumCullCompute: sibling, not replacement. CS5 remains the right tool
/// when the CPU genuinely needs the answer (editor picking, gameplay queries, or any caller that
/// must branch on visibility). This one is for the render path.
///
/// The compacted index list is a SET, not a sequence. Slots are handed out by an atomic in the
/// kernel, so two runs over identical input can order the survivors differently. Draw order
/// therefore is not stable across frames, which matters for anything depending on submission order
/// - alpha blending most obviously. Opaque geometry, which is what an indirect cull pass is for,
/// does not care.
///
/// Thread-safety: not thread-safe. Main engine thread only, like every other render system.
class FrustumCullIndirect final {
public:
    /// Matches the kernel's `local_size_x`. Dispatches round up; the kernel discards the tail.
    static constexpr std::uint32_t kWorkgroupSize = 64U;

    FrustumCullIndirect(IRenderDevice& renderDevice, IComputeSystem& computeSystem) noexcept;
    ~FrustumCullIndirect();

    FrustumCullIndirect(const FrustumCullIndirect&) = delete;
    FrustumCullIndirect& operator=(const FrustumCullIndirect&) = delete;

    /// Compiles the kernel and allocates the fixed buffers. Returns false (logging why, and filling
    /// `outInfoLog` when non-null) on a backend without compute support or if the kernel fails to
    /// build; IsReady() then stays false and the caller should fall back to CPU culling plus an
    /// ordinary DrawIndexed. Calling it again once ready is a no-op returning true.
    [[nodiscard]] bool Initialize(std::string* outInfoLog = nullptr);

    [[nodiscard]] bool IsReady() const noexcept;

    /// Culls `boxes` against `frustum` on the GPU and leaves the result in device memory.
    ///
    /// `meshParams` supplies the four fields of the draw command that describe the MESH rather than
    /// the culling outcome - indexCount, firstIndex, vertexOffset, firstInstance. Its instanceCount
    /// is IGNORED and overwritten: the whole point is that the GPU decides that field. Passing it
    /// by value here rather than taking four arguments keeps the call site readable and matches the
    /// struct the GPU actually writes.
    ///
    /// On success the caller may record `DrawIndexedIndirect(GetDrawCommandBuffer(), 0)` and
    /// bind `GetVisibleIndexBuffer()` as an SSBO for the vertex shader to read.
    ///
    /// Returns false on a non-ready system, non-finite input (refused for the same reason CS5
    /// refuses it - the CPU's double-precision fallback is not reproducible in a float shader), or
    /// any failed GPU step. An empty `boxes` is a successful no-op that still leaves a valid,
    /// zero-instance draw command in the buffer, so a caller that always draws stays correct.
    [[nodiscard]] bool Prepare(std::span<const Math::Aabb> boxes, const Math::Frustum& frustum,
                                  const DrawIndexedIndirectCommand& meshParams);

    /// The IndirectStorage buffer holding one DrawIndexedIndirectCommand, ready for
    /// DrawIndexedIndirect. Invalid until Initialize() succeeds.
    [[nodiscard]] BufferHandle GetDrawCommandBuffer() const noexcept;

    /// The storage buffer of surviving box indices, one uint per survivor, in slots [0,
    /// instanceCount). Invalid until the first successful Prepare().
    [[nodiscard]] BufferHandle GetVisibleIndexBuffer() const noexcept;

    [[nodiscard]] const FrustumCullIndirectDiagnostics& GetDiagnostics() const noexcept;

private:
    [[nodiscard]] bool EnsureBufferCapacity(std::size_t boxCount);

    IRenderDevice& m_device;
    IComputeSystem& m_computeSystem;
    PipelineHandle m_program = kInvalidPipelineHandle;
    BufferHandle m_boxBuffer = kInvalidBufferHandle;
    BufferHandle m_planeBuffer = kInvalidBufferHandle;
    BufferHandle m_drawCommandBuffer = kInvalidBufferHandle;
    BufferHandle m_visibleIndexBuffer = kInvalidBufferHandle;
    BufferHandle m_paramBuffer = kInvalidBufferHandle;
    std::size_t m_bufferCapacityBoxes = 0U;
    std::vector<CullBoxGpu> m_boxScratch;
    FrustumCullIndirectDiagnostics m_diagnostics;
};

} // namespace UVE::Render
