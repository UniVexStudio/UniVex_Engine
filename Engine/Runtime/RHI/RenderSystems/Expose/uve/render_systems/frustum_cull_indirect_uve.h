// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/render_systems/frustum_cull_compute_uve.h"
#include "uve/render_systems/i_compute_system_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"

namespace UVE::Render {

/// The indirect-cull kernel's parameters, matching its std430 block. One int, in a storage buffer
/// for the same reason every other kernel's parameters are: SPIR-V has no non-opaque global
/// uniforms.
struct FrustumCullIndirectParamsGpuUVE final {
    std::int32_t boxCount = 0;
};

/// A lifetime-to-date account of observable FrustumCullIndirectUVE work. Note what is NOT here: a
/// visible-object count. This system's whole purpose is that the CPU never learns that number, and
/// a diagnostic that quietly read it back would reintroduce the very round trip the pass exists to
/// remove. `drawsPrepared` counts commands built, not objects drawn.
struct FrustumCullIndirectDiagnosticsUVE final {
    std::uint32_t cullsRequested = 0U;
    std::uint32_t cullsRejected = 0U;
    std::uint32_t cullsSkipped = 0U;
    std::uint64_t boxesTested = 0U;
    std::uint32_t drawsPrepared = 0U;
    std::uint32_t uploadFailures = 0U;
    std::uint32_t bufferReallocations = 0U;
};

/// FrustumCullIndirectUVE is CS5's frustum cull with the CPU taken off the critical path.
///
/// CS5 answers "which of these boxes are visible" and hands the answer back, which means a full
/// GPU->CPU round trip before anything can be drawn - the one cost a culling pass is supposed to
/// avoid. This class instead leaves the answer in device memory in the exact shape a draw call
/// consumes: the `instanceCount` field of a `DrawIndexedIndirectCommandUVE`, plus a compacted
/// buffer of surviving box indices for the vertex shader to index with `gl_InstanceID`. The caller
/// then records `DrawIndexedIndirectUVE` (CS7) and the GPU draws exactly the survivors. Nothing on
/// the CPU is ever told how many there were.
///
/// That property is the feature, and it is also what makes this class awkward to test - so the
/// tests do the one honest thing available: they read the buffers back AFTERWARDS, purely as
/// observers, and check the compacted set against CS5's per-box visibility over the same inputs.
/// `PrepareUVE()` itself performs no readback, and the diagnostics deliberately expose no visible
/// count, so no production path can come to depend on one.
///
/// Relationship to FrustumCullComputeUVE: sibling, not replacement. CS5 remains the right tool
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
class FrustumCullIndirectUVE final {
public:
    /// Matches the kernel's `local_size_x`. Dispatches round up; the kernel discards the tail.
    static constexpr std::uint32_t kWorkgroupSizeUVE = 64U;

    FrustumCullIndirectUVE(IRenderDeviceUVE& renderDevice, IComputeSystemUVE& computeSystem) noexcept;
    ~FrustumCullIndirectUVE();

    FrustumCullIndirectUVE(const FrustumCullIndirectUVE&) = delete;
    FrustumCullIndirectUVE& operator=(const FrustumCullIndirectUVE&) = delete;

    /// Compiles the kernel and allocates the fixed buffers. Returns false (logging why, and filling
    /// `outInfoLog` when non-null) on a backend without compute support or if the kernel fails to
    /// build; IsReadyUVE() then stays false and the caller should fall back to CPU culling plus an
    /// ordinary DrawIndexedUVE. Calling it again once ready is a no-op returning true.
    [[nodiscard]] bool InitializeUVE(std::string* outInfoLog = nullptr);

    [[nodiscard]] bool IsReadyUVE() const noexcept;

    /// Culls `boxes` against `frustum` on the GPU and leaves the result in device memory.
    ///
    /// `meshParams` supplies the four fields of the draw command that describe the MESH rather than
    /// the culling outcome - indexCount, firstIndex, vertexOffset, firstInstance. Its instanceCount
    /// is IGNORED and overwritten: the whole point is that the GPU decides that field. Passing it
    /// by value here rather than taking four arguments keeps the call site readable and matches the
    /// struct the GPU actually writes.
    ///
    /// On success the caller may record `DrawIndexedIndirectUVE(GetDrawCommandBufferUVE(), 0)` and
    /// bind `GetVisibleIndexBufferUVE()` as an SSBO for the vertex shader to read.
    ///
    /// Returns false on a non-ready system, non-finite input (refused for the same reason CS5
    /// refuses it - the CPU's double-precision fallback is not reproducible in a float shader), or
    /// any failed GPU step. An empty `boxes` is a successful no-op that still leaves a valid,
    /// zero-instance draw command in the buffer, so a caller that always draws stays correct.
    [[nodiscard]] bool PrepareUVE(std::span<const Math::AabbUVE> boxes, const Math::FrustumUVE& frustum,
                                  const DrawIndexedIndirectCommandUVE& meshParams);

    /// The IndirectStorage buffer holding one DrawIndexedIndirectCommandUVE, ready for
    /// DrawIndexedIndirectUVE. Invalid until InitializeUVE() succeeds.
    [[nodiscard]] BufferHandleUVE GetDrawCommandBufferUVE() const noexcept;

    /// The storage buffer of surviving box indices, one uint per survivor, in slots [0,
    /// instanceCount). Invalid until the first successful PrepareUVE().
    [[nodiscard]] BufferHandleUVE GetVisibleIndexBufferUVE() const noexcept;

    [[nodiscard]] const FrustumCullIndirectDiagnosticsUVE& GetDiagnosticsUVE() const noexcept;

private:
    [[nodiscard]] bool EnsureBufferCapacityUVE(std::size_t boxCount);

    IRenderDeviceUVE& m_device;
    IComputeSystemUVE& m_computeSystem;
    PipelineHandleUVE m_program = kInvalidPipelineHandleUVE;
    BufferHandleUVE m_boxBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_planeBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_drawCommandBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_visibleIndexBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_paramBuffer = kInvalidBufferHandleUVE;
    std::size_t m_bufferCapacityBoxes = 0U;
    std::vector<CullBoxGpuUVE> m_boxScratch;
    FrustumCullIndirectDiagnosticsUVE m_diagnostics;
};

} // namespace UVE::Render
