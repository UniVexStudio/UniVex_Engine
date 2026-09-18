// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/render_systems/i_compute_system_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/scene/i_particle_runtime_uve.h"
#include "uve/scene/particle_runtime_uve.h"

namespace UVE::Render {

/// One particle exactly as the GPU kernel sees it: eight consecutive floats, std430-packed
/// with no padding surprises. Deliberately NOT built from Math::Vector3UVE - a `vec3` in an
/// std430 struct is 16-byte aligned, so a vec3-shaped host struct and the shader's own layout
/// would disagree the moment a second member followed one. Spelling the components out keeps
/// the host and shader declarations trivially comparable, which matters because a silent
/// layout mismatch here would not fail to compile; it would produce plausible wrong numbers.
struct ParticleGpuStateUVE final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float velocityZ = 0.0F;
    float remainingLifetimeSeconds = 0.0F;
    float padding = 0.0F;
};

static_assert(sizeof(ParticleGpuStateUVE) == 8U * sizeof(float),
              "ParticleGpuStateUVE must match the kernel's std430 ParticleGpuState exactly");
static_assert(alignof(ParticleGpuStateUVE) == alignof(float),
              "ParticleGpuStateUVE must not acquire alignment padding the shader does not have");

/// The kernel's parameters, laid out to match its std430 ParticleSimulateParams block exactly.
/// These travel in a storage buffer rather than as bare `uniform` scalars for a portability
/// reason worth stating plainly: SPIR-V has no non-opaque global uniforms, so a kernel written
/// with `uniform float uDeltaSeconds` cannot be compiled for Vulkan at all - glslang rejects it
/// outright. A std430 block compiles unchanged for both backends.
struct ParticleSimulateParamsGpuUVE final {
    float deltaSeconds = 0.0F;
    float accelerationX = 0.0F;
    float accelerationY = 0.0F;
    float accelerationZ = 0.0F;
    std::int32_t particleCount = 0;
};

static_assert(sizeof(ParticleSimulateParamsGpuUVE) == 4U * sizeof(float) + sizeof(std::int32_t),
              "ParticleSimulateParamsGpuUVE must match the kernel's std430 params block exactly");

/// A lifetime-to-date account of observable ParticleComputeSimulationUVE work, in the same
/// evidence-naming spirit as ComputeSystemDiagnosticsUVE: each counter names something the
/// system actually did, and "simulated" means a dispatch was queued and its result read back -
/// never a claim about visual outcome.
struct ParticleComputeSimulationDiagnosticsUVE final {
    std::uint32_t simulationsRequested = 0U;
    std::uint32_t simulationsRejected = 0U;
    std::uint32_t simulationsSkipped = 0U;
    std::uint32_t instancesSimulated = 0U;
    std::uint64_t particlesSimulated = 0U;
    std::uint32_t uploadFailures = 0U;
    std::uint32_t readbackFailures = 0U;
    std::uint32_t bufferReallocations = 0U;
};

/// ParticleComputeSimulationUVE runs Scene::ParticleRuntimeUVE's per-particle integration on the
/// GPU (ROADMAP section 2's compute item names particle simulation as a target workload). It is
/// the first real consumer of the compute stack: ComputeSystemUVE owns the program and the
/// dispatch queue, IRenderDeviceUVE owns the storage buffer, and CS3's ReadbackBufferUVE is what
/// makes the result observable - without that read this class could dispatch but never prove
/// anything, which is why it was built only after readback existed.
///
/// Division of labour, chosen deliberately: the CPU runtime stays the authority on WHICH
/// particles exist. Emission, budget enforcement, lifetime culling and array compaction remain
/// in ParticleRuntimeUVE, where they are bounded, ordered and already tested; the GPU does only
/// the pure arithmetic over the particle array. That keeps this class's contract narrow enough
/// to verify: SimulateUVE() must leave the runtime in EXACTLY the state
/// ParticleRuntimeUVE::SimulateDetailedUVE() would have - the engine's tests assert the two
/// agree bit-for-bit, and the kernel forbids fused multiply-add so that equality is real rather
/// than approximate.
///
/// This is a per-instance GPU round trip (upload, dispatch, read back, write home) and therefore
/// synchronous by construction: CS3 documents readback as a cold path that drains the device.
/// It is honest work on real particle counts and a genuine win for the arithmetic, but it is not
/// a zero-cost per-frame path - a fully resident simulation, where particle state never returns
/// to the CPU at all, is the next step and needs GPU-side emission and compaction first.
///
/// Thread-safety: not thread-safe. Main engine thread only, like every other render system.
class ParticleComputeSimulationUVE final {
public:
    /// The kernel's workgroup size. Dispatches round the particle count up to a whole number of
    /// these; the kernel's own bounds check discards the tail invocations.
    static constexpr std::uint32_t kWorkgroupSizeUVE = 64U;

    ParticleComputeSimulationUVE(IRenderDeviceUVE& renderDevice, IComputeSystemUVE& computeSystem) noexcept;
    ~ParticleComputeSimulationUVE();

    ParticleComputeSimulationUVE(const ParticleComputeSimulationUVE&) = delete;
    ParticleComputeSimulationUVE& operator=(const ParticleComputeSimulationUVE&) = delete;

    /// Compiles the simulation kernel through ComputeSystemUVE. Returns false (logging the
    /// reason, and storing the backend's info log in `outInfoLog` when non-null) if the backend
    /// has no compute support or the kernel fails to build - on a device without compute this is
    /// the expected answer, and IsReadyUVE() stays false so callers fall back to the CPU path.
    /// Calling it again once ready is a no-op that returns true.
    [[nodiscard]] bool InitializeUVE(std::string* outInfoLog = nullptr);

    /// True once InitializeUVE() has succeeded and the kernel is usable.
    [[nodiscard]] bool IsReadyUVE() const noexcept;

    /// Advances `particles` by `deltaSeconds` under `acceleration` on the GPU, then culls the
    /// expired ones and compacts the survivors - the GPU counterpart of one instance's worth of
    /// Scene::ParticleRuntimeUVE::SimulateDetailedUVE(), and required to leave `particles` in
    /// EXACTLY the state that function would have left it in, element for element, bit for bit.
    ///
    /// It takes the particle array rather than the runtime on purpose. The runtime exposes no
    /// write-back entry point, and inventing one so this class could reach inside it would widen
    /// a gameplay-facing interface to serve a render-side implementation detail. The array is the
    /// real unit of work anyway: callers hold it, pass it, and get it back integrated.
    ///
    /// Same input contract as the CPU path, deliberately not a looser one: `deltaSeconds` must be
    /// finite, non-negative and no greater than kMaximumSimulationDeltaSecondsUVE, and
    /// `acceleration` must be finite. Violations return false and change nothing.
    ///
    /// Atomic on failure. If the system is not ready, a GPU step fails (upload, enqueue, dispatch
    /// or readback), or the GPU produces a non-finite value - the same NonFiniteSimulation
    /// condition the CPU path refuses to half-apply - `particles` is left exactly as it was and
    /// the caller can fall back to the CPU simulation with nothing to undo.
    ///
    /// An empty array, or a zero delta, is a successful no-op.
    [[nodiscard]] bool SimulateUVE(std::vector<Scene::ParticleStateUVE>& particles, float deltaSeconds,
                                   const Math::Vector3UVE& acceleration);

    [[nodiscard]] const ParticleComputeSimulationDiagnosticsUVE& GetDiagnosticsUVE() const noexcept;

private:
    /// Grows the shared storage buffer to hold at least `particleCount` particles. Buffers are
    /// reused across instances and frames and only ever grow, so a steady-state simulation stops
    /// reallocating entirely.
    [[nodiscard]] bool EnsureBufferCapacityUVE(std::size_t particleCount);

    IRenderDeviceUVE& m_device;
    IComputeSystemUVE& m_computeSystem;
    PipelineHandleUVE m_program = kInvalidPipelineHandleUVE;
    BufferHandleUVE m_buffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_paramBuffer = kInvalidBufferHandleUVE;
    std::size_t m_bufferCapacityParticles = 0U;
    std::vector<ParticleGpuStateUVE> m_scratch;
    ParticleComputeSimulationDiagnosticsUVE m_diagnostics;
};

} // namespace UVE::Render
