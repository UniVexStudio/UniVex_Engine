// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "aabb.h"
#include "frustum.h"
#include "i_compute_system.h"
#include "i_render_device.h"

namespace UVE::Render {

/// One box as the cull kernel sees it: centre and extents, eight std430-packed floats. Centre and
/// extents rather than min/max on purpose - Aabb::GetCenter() does the halving in the CPU's
/// own arithmetic (including its double-precision fallback for boxes whose min+max overflows), and
/// recomputing that in the shader would add a second place for the two paths to disagree for no
/// benefit. Spelled out as scalars for the same reason the particle kernel's state struct is: a
/// vec3 in std430 is 16-byte aligned and would silently introduce padding the host does not have.
struct CullBoxGpu final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    float extentX = 0.0F;
    float extentY = 0.0F;
    float extentZ = 0.0F;
    float padding0 = 0.0F;
    float padding1 = 0.0F;
};

/// One frustum plane exactly as Math::Plane stores it: unit normal plus distance.
struct CullPlaneGpu final {
    float normalX = 0.0F;
    float normalY = 0.0F;
    float normalZ = 0.0F;
    float distance = 0.0F;
};

static_assert(sizeof(CullBoxGpu) == 8U * sizeof(float),
              "CullBoxGpu must match the kernel's std430 CullBox exactly");
static_assert(sizeof(CullPlaneGpu) == 4U * sizeof(float),
              "CullPlaneGpu must match the kernel's std430 CullPlane exactly");
static_assert(alignof(CullBoxGpu) == alignof(float) && alignof(CullPlaneGpu) == alignof(float),
              "the GPU cull structs must not acquire alignment padding the shader does not have");

/// The kernel's parameters, matching its std430 FrustumCullParams block. A storage buffer rather
/// than a bare `uniform` for the portability reason spelled out on ParticleSimulateParamsGpu:
/// SPIR-V has no non-opaque global uniforms, so the uniform form cannot compile for Vulkan.
struct FrustumCullParamsGpu final {
    std::int32_t boxCount = 0;
};

/// A lifetime-to-date account of observable FrustumCullCompute work, in the same
/// evidence-naming spirit as ComputeSystemDiagnostics: every counter names something the system
/// actually did. `boxesCulled` counts boxes the GPU reported invisible - a real result that was
/// read back, never an estimate.
struct FrustumCullComputeDiagnostics final {
    std::uint32_t cullsRequested = 0U;
    std::uint32_t cullsRejected = 0U;
    std::uint32_t cullsSkipped = 0U;
    std::uint64_t boxesTested = 0U;
    std::uint64_t boxesVisible = 0U;
    std::uint64_t boxesCulled = 0U;
    std::uint32_t uploadFailures = 0U;
    std::uint32_t readbackFailures = 0U;
    std::uint32_t bufferReallocations = 0U;
};

/// FrustumCullCompute runs Math::Frustum::Intersects over many boxes at once on the GPU -
/// the second workload named by ROADMAP section 2's compute item, after CS4's particle simulation,
/// and built on the same three pieces: ComputeSystem owns the program and the dispatch queue,
/// IRenderDevice owns the storage buffers, and CS3's ReadbackBuffer is what lets engine code
/// see the answer.
///
/// Culling produces a BOOLEAN, which makes it tempting to accept "nearly the same" results. This
/// class does not: `Cull()` must return EXACTLY the visibility the CPU test would have produced
/// for every box, and the tests assert that with no tolerance. A box lying exactly on a plane is
/// both where the two paths are most likely to diverge and where divergence is most visible - an
/// object popping in or out depending on which path ran - so the kernel forbids fused multiply-add
/// to keep the arithmetic under the boolean bit-identical.
///
/// Scope, chosen deliberately: this answers "which of these boxes are visible", nothing more. It
/// does not walk the scene, resolve mesh assets, or build a render queue - MeshRenderEligibility
/// owns the rest of that decision (valid assets, finite transforms, sort depth) and calls the same
/// frustum test as one step among several. Making this class a drop-in for that whole pipeline
/// would mean moving asset validation onto the GPU, which is neither possible nor desirable; a
/// caller with many boxes and one frustum is the case worth accelerating, and that is the case
/// this class serves.
///
/// Plane extraction stays on the CPU. Six planes is not work worth a dispatch, and keeping
/// Frustum::FromViewProjection the single authority means the engine has exactly one
/// plane-extraction implementation to be correct.
///
/// Like CS4's particle simulation this is a synchronous GPU round trip on CS3's documented cold
/// path: honest work, a real win on large box counts, and not a free per-frame path. Culling whose
/// result never returns to the CPU - feeding an indirect draw straight from the GPU - is the next
/// step and needs indirect-draw support in the RHI first.
///
/// Thread-safety: not thread-safe. Main engine thread only, like every other render system.
class FrustumCullCompute final {
public:
    /// The kernel's workgroup size. Dispatches round the box count up to whole workgroups; the
    /// kernel's own bounds check discards the tail invocations.
    static constexpr std::uint32_t kWorkgroupSize = 64U;

    FrustumCullCompute(IRenderDevice& renderDevice, IComputeSystem& computeSystem) noexcept;
    ~FrustumCullCompute();

    FrustumCullCompute(const FrustumCullCompute&) = delete;
    FrustumCullCompute& operator=(const FrustumCullCompute&) = delete;

    /// Compiles the cull kernel through ComputeSystem. Returns false (logging the reason, and
    /// storing the backend's info log in `outInfoLog` when non-null) if the backend has no compute
    /// support or the kernel fails to build - on such a device this is the expected answer, and
    /// IsReady() stays false so callers fall back to the CPU test. Calling it again once ready
    /// is a no-op that returns true.
    [[nodiscard]] bool Initialize(std::string* outInfoLog = nullptr);

    /// True once Initialize() has succeeded and the kernel is usable.
    [[nodiscard]] bool IsReady() const noexcept;

    /// Tests every box in `boxes` against `frustum` on the GPU and fills `outVisible` with one
    /// entry per box: true exactly when `frustum.Intersects(box)` would be true. `outVisible` is
    /// resized to match `boxes`.
    ///
    /// Returns false, leaving `outVisible` untouched, when the system is not ready, when any box or
    /// plane is non-finite, or when a GPU step fails. Non-finite input is refused rather than
    /// forwarded because the CPU test handles it through Plane's double-precision fallback,
    /// which a float shader cannot reproduce - answering differently would be worse than answering
    /// honestly that this path does not cover that case, and the caller still has the CPU test.
    ///
    /// An empty `boxes` is a successful no-op that empties `outVisible`.///
/// One consequence worth stating: this call PRESENTS the device in order to make its own dispatch
/// execute. Vulkan replays submitted recordings in Present(), so without it the readback would
/// return pre-dispatch contents (it did - a red CI run proved it). That makes this a step to run
/// outside the render frame's own present, not in the middle of one.
    [[nodiscard]] bool Cull(std::span<const Math::Aabb> boxes, const Math::Frustum& frustum,
                               std::vector<bool>& outVisible);

    [[nodiscard]] const FrustumCullComputeDiagnostics& GetDiagnostics() const noexcept;

private:
    /// Grows the box and visibility buffers to hold at least `boxCount` entries. Buffers are reused
    /// across calls and only ever grow, so a steady-state caller stops reallocating entirely.
    [[nodiscard]] bool EnsureBufferCapacity(std::size_t boxCount);

    IRenderDevice& m_device;
    IComputeSystem& m_computeSystem;
    PipelineHandle m_program = kInvalidPipelineHandle;
    BufferHandle m_boxBuffer = kInvalidBufferHandle;
    BufferHandle m_planeBuffer = kInvalidBufferHandle;
    BufferHandle m_visibilityBuffer = kInvalidBufferHandle;
    BufferHandle m_paramBuffer = kInvalidBufferHandle;
    std::size_t m_bufferCapacityBoxes = 0U;
    std::vector<CullBoxGpu> m_boxScratch;
    std::vector<std::uint32_t> m_visibilityScratch;
    FrustumCullComputeDiagnostics m_diagnostics;
};

} // namespace UVE::Render
