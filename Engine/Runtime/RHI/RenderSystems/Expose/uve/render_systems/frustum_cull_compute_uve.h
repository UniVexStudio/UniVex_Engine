// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/render_systems/i_compute_system_uve.h"
#include "uve/rhi/i_render_device_uve.h"

namespace UVE::Render {

/// One box as the cull kernel sees it: centre and extents, eight std430-packed floats. Centre and
/// extents rather than min/max on purpose - AabbUVE::GetCenterUVE() does the halving in the CPU's
/// own arithmetic (including its double-precision fallback for boxes whose min+max overflows), and
/// recomputing that in the shader would add a second place for the two paths to disagree for no
/// benefit. Spelled out as scalars for the same reason the particle kernel's state struct is: a
/// vec3 in std430 is 16-byte aligned and would silently introduce padding the host does not have.
struct CullBoxGpuUVE final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    float extentX = 0.0F;
    float extentY = 0.0F;
    float extentZ = 0.0F;
    float padding0 = 0.0F;
    float padding1 = 0.0F;
};

/// One frustum plane exactly as Math::PlaneUVE stores it: unit normal plus distance.
struct CullPlaneGpuUVE final {
    float normalX = 0.0F;
    float normalY = 0.0F;
    float normalZ = 0.0F;
    float distance = 0.0F;
};

static_assert(sizeof(CullBoxGpuUVE) == 8U * sizeof(float),
              "CullBoxGpuUVE must match the kernel's std430 CullBox exactly");
static_assert(sizeof(CullPlaneGpuUVE) == 4U * sizeof(float),
              "CullPlaneGpuUVE must match the kernel's std430 CullPlane exactly");
static_assert(alignof(CullBoxGpuUVE) == alignof(float) && alignof(CullPlaneGpuUVE) == alignof(float),
              "the GPU cull structs must not acquire alignment padding the shader does not have");

/// The kernel's parameters, matching its std430 FrustumCullParams block. A storage buffer rather
/// than a bare `uniform` for the portability reason spelled out on ParticleSimulateParamsGpuUVE:
/// SPIR-V has no non-opaque global uniforms, so the uniform form cannot compile for Vulkan.
struct FrustumCullParamsGpuUVE final {
    std::int32_t boxCount = 0;
};

/// A lifetime-to-date account of observable FrustumCullComputeUVE work, in the same
/// evidence-naming spirit as ComputeSystemDiagnosticsUVE: every counter names something the system
/// actually did. `boxesCulled` counts boxes the GPU reported invisible - a real result that was
/// read back, never an estimate.
struct FrustumCullComputeDiagnosticsUVE final {
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

/// FrustumCullComputeUVE runs Math::FrustumUVE::IntersectsUVE over many boxes at once on the GPU -
/// the second workload named by ROADMAP section 2's compute item, after CS4's particle simulation,
/// and built on the same three pieces: ComputeSystemUVE owns the program and the dispatch queue,
/// IRenderDeviceUVE owns the storage buffers, and CS3's ReadbackBufferUVE is what lets engine code
/// see the answer.
///
/// Culling produces a BOOLEAN, which makes it tempting to accept "nearly the same" results. This
/// class does not: `CullUVE()` must return EXACTLY the visibility the CPU test would have produced
/// for every box, and the tests assert that with no tolerance. A box lying exactly on a plane is
/// both where the two paths are most likely to diverge and where divergence is most visible - an
/// object popping in or out depending on which path ran - so the kernel forbids fused multiply-add
/// to keep the arithmetic under the boolean bit-identical.
///
/// Scope, chosen deliberately: this answers "which of these boxes are visible", nothing more. It
/// does not walk the scene, resolve mesh assets, or build a render queue - MeshRenderEligibilityUVE
/// owns the rest of that decision (valid assets, finite transforms, sort depth) and calls the same
/// frustum test as one step among several. Making this class a drop-in for that whole pipeline
/// would mean moving asset validation onto the GPU, which is neither possible nor desirable; a
/// caller with many boxes and one frustum is the case worth accelerating, and that is the case
/// this class serves.
///
/// Plane extraction stays on the CPU. Six planes is not work worth a dispatch, and keeping
/// FrustumUVE::FromViewProjectionUVE the single authority means the engine has exactly one
/// plane-extraction implementation to be correct.
///
/// Like CS4's particle simulation this is a synchronous GPU round trip on CS3's documented cold
/// path: honest work, a real win on large box counts, and not a free per-frame path. Culling whose
/// result never returns to the CPU - feeding an indirect draw straight from the GPU - is the next
/// step and needs indirect-draw support in the RHI first.
///
/// Thread-safety: not thread-safe. Main engine thread only, like every other render system.
class FrustumCullComputeUVE final {
public:
    /// The kernel's workgroup size. Dispatches round the box count up to whole workgroups; the
    /// kernel's own bounds check discards the tail invocations.
    static constexpr std::uint32_t kWorkgroupSizeUVE = 64U;

    FrustumCullComputeUVE(IRenderDeviceUVE& renderDevice, IComputeSystemUVE& computeSystem) noexcept;
    ~FrustumCullComputeUVE();

    FrustumCullComputeUVE(const FrustumCullComputeUVE&) = delete;
    FrustumCullComputeUVE& operator=(const FrustumCullComputeUVE&) = delete;

    /// Compiles the cull kernel through ComputeSystemUVE. Returns false (logging the reason, and
    /// storing the backend's info log in `outInfoLog` when non-null) if the backend has no compute
    /// support or the kernel fails to build - on such a device this is the expected answer, and
    /// IsReadyUVE() stays false so callers fall back to the CPU test. Calling it again once ready
    /// is a no-op that returns true.
    [[nodiscard]] bool InitializeUVE(std::string* outInfoLog = nullptr);

    /// True once InitializeUVE() has succeeded and the kernel is usable.
    [[nodiscard]] bool IsReadyUVE() const noexcept;

    /// Tests every box in `boxes` against `frustum` on the GPU and fills `outVisible` with one
    /// entry per box: true exactly when `frustum.IntersectsUVE(box)` would be true. `outVisible` is
    /// resized to match `boxes`.
    ///
    /// Returns false, leaving `outVisible` untouched, when the system is not ready, when any box or
    /// plane is non-finite, or when a GPU step fails. Non-finite input is refused rather than
    /// forwarded because the CPU test handles it through PlaneUVE's double-precision fallback,
    /// which a float shader cannot reproduce - answering differently would be worse than answering
    /// honestly that this path does not cover that case, and the caller still has the CPU test.
    ///
    /// An empty `boxes` is a successful no-op that empties `outVisible`.///
/// One consequence worth stating: this call PRESENTS the device in order to make its own dispatch
/// execute. Vulkan replays submitted recordings in PresentUVE(), so without it the readback would
/// return pre-dispatch contents (it did - a red CI run proved it). That makes this a step to run
/// outside the render frame's own present, not in the middle of one.
    [[nodiscard]] bool CullUVE(std::span<const Math::AabbUVE> boxes, const Math::FrustumUVE& frustum,
                               std::vector<bool>& outVisible);

    [[nodiscard]] const FrustumCullComputeDiagnosticsUVE& GetDiagnosticsUVE() const noexcept;

private:
    /// Grows the box and visibility buffers to hold at least `boxCount` entries. Buffers are reused
    /// across calls and only ever grow, so a steady-state caller stops reallocating entirely.
    [[nodiscard]] bool EnsureBufferCapacityUVE(std::size_t boxCount);

    IRenderDeviceUVE& m_device;
    IComputeSystemUVE& m_computeSystem;
    PipelineHandleUVE m_program = kInvalidPipelineHandleUVE;
    BufferHandleUVE m_boxBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_planeBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_visibilityBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_paramBuffer = kInvalidBufferHandleUVE;
    std::size_t m_bufferCapacityBoxes = 0U;
    std::vector<CullBoxGpuUVE> m_boxScratch;
    std::vector<std::uint32_t> m_visibilityScratch;
    FrustumCullComputeDiagnosticsUVE m_diagnostics;
};

} // namespace UVE::Render
