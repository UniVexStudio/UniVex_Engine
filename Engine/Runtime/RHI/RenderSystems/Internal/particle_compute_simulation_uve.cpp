// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/particle_compute_simulation_uve.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"

namespace UVE::Render {
namespace {

[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& vector) noexcept {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

/// The same input bounds Scene::ParticleRuntimeUVE::SimulateDetailedUVE() enforces. Duplicated
/// rather than delegated because the runtime's validation is welded to its own state mutation;
/// the point is that the GPU path must not accept one single input the CPU path would refuse.
[[nodiscard]] bool AreSimulationInputsValidUVE(const float deltaSeconds,
                                               const Math::Vector3UVE& acceleration) noexcept {
    return std::isfinite(deltaSeconds) && deltaSeconds >= 0.0F &&
           deltaSeconds <= Scene::ParticleRuntimeUVE::kMaximumSimulationDeltaSecondsUVE &&
           IsFiniteVectorUVE(acceleration);
}

} // namespace

ParticleComputeSimulationUVE::ParticleComputeSimulationUVE(IRenderDeviceUVE& renderDevice,
                                                           IComputeSystemUVE& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

ParticleComputeSimulationUVE::~ParticleComputeSimulationUVE() {
    // The buffer is this class's own; the program belongs to the compute system, which is why it
    // goes back through DestroyProgramUVE() rather than straight to the device.
    if (m_buffer != kInvalidBufferHandleUVE) {
        m_device.DestroyBufferUVE(m_buffer);
        m_buffer = kInvalidBufferHandleUVE;
    }
    if (m_program != kInvalidPipelineHandleUVE) {
        m_computeSystem.DestroyProgramUVE(m_program);
        m_program = kInvalidPipelineHandleUVE;
    }
}

bool ParticleComputeSimulationUVE::InitializeUVE(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandleUVE) {
        return true;
    }

    ComputeProgramDescUVE desc;
    desc.sourceCode = std::string(Shader::BuiltIn::kParticleSimulateSource);
    desc.debugName = "ParticleSimulate";
    m_program = m_computeSystem.CreateProgramUVE(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandleUVE) {
        UVE_WARNING("ParticleComputeSimulationUVE could not create its simulation kernel; "
                        "callers must keep using the CPU particle simulation.");
        return false;
    }
    return true;
}

bool ParticleComputeSimulationUVE::IsReadyUVE() const noexcept {
    return m_program != kInvalidPipelineHandleUVE;
}

bool ParticleComputeSimulationUVE::EnsureBufferCapacityUVE(const std::size_t particleCount) {
    if (m_buffer != kInvalidBufferHandleUVE && m_bufferCapacityParticles >= particleCount) {
        return true;
    }

    // Grow-only, and never shrink back: a simulation whose particle count oscillates below its
    // own high-water mark should stop touching the allocator entirely after the first few frames.
    if (m_buffer != kInvalidBufferHandleUVE) {
        m_device.DestroyBufferUVE(m_buffer);
        m_buffer = kInvalidBufferHandleUVE;
        m_bufferCapacityParticles = 0U;
    }

    BufferDescUVE desc;
    desc.usage = BufferUsageUVE::Storage;
    desc.sizeBytes = particleCount * sizeof(ParticleGpuStateUVE);
    m_buffer = m_device.CreateBufferUVE(desc);
    if (m_buffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("ParticleComputeSimulationUVE failed to allocate its particle storage buffer.");
        return false;
    }
    m_bufferCapacityParticles = particleCount;
    ++m_diagnostics.bufferReallocations;
    return true;
}

bool ParticleComputeSimulationUVE::SimulateUVE(std::vector<Scene::ParticleStateUVE>& particles,
                                               const float deltaSeconds,
                                               const Math::Vector3UVE& acceleration) {
    ++m_diagnostics.simulationsRequested;

    if (!AreSimulationInputsValidUVE(deltaSeconds, acceleration)) {
        ++m_diagnostics.simulationsRejected;
        UVE_WARNING("ParticleComputeSimulationUVE requires finite acceleration and a bounded "
                        "non-negative delta time, exactly like the CPU particle runtime.");
        return false;
    }
    if (!IsReadyUVE()) {
        ++m_diagnostics.simulationsRejected;
        UVE_WARNING("ParticleComputeSimulationUVE was asked to simulate before its kernel was ready.");
        return false;
    }
    if (particles.empty() || deltaSeconds == 0.0F) {
        ++m_diagnostics.simulationsSkipped;
        return true;
    }

    const std::size_t particleCount = particles.size();
    if (!EnsureBufferCapacityUVE(particleCount)) {
        ++m_diagnostics.uploadFailures;
        return false;
    }

    m_scratch.assign(particleCount, ParticleGpuStateUVE{});
    for (std::size_t index = 0U; index < particleCount; ++index) {
        const Scene::ParticleStateUVE& particle = particles[index];
        ParticleGpuStateUVE& packed = m_scratch[index];
        packed.positionX = particle.position.x;
        packed.positionY = particle.position.y;
        packed.positionZ = particle.position.z;
        packed.velocityX = particle.velocity.x;
        packed.velocityY = particle.velocity.y;
        packed.velocityZ = particle.velocity.z;
        packed.remainingLifetimeSeconds = particle.remainingLifetimeSeconds;
    }
    // The sequence numbers never make the trip: they are identity, not physics, and the kernel has
    // no business touching them. They are re-attached from the untouched CPU array below, which is
    // also what keeps the compaction below able to reproduce the CPU's exact output.

    const std::span<const std::byte> upload{reinterpret_cast<const std::byte*>(m_scratch.data()),
                                            m_scratch.size() * sizeof(ParticleGpuStateUVE)};
    if (!m_device.UpdateBufferUVE(m_buffer, upload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("ParticleComputeSimulationUVE could not upload particle state; the CPU array is unchanged.");
        return false;
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX =
        static_cast<std::uint32_t>((particleCount + kWorkgroupSizeUVE - 1U) / kWorkgroupSizeUVE);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_buffer, 0U});
    dispatch.uniforms.push_back(MakeComputeUniformFloatUVE("uDeltaSeconds", deltaSeconds));
    dispatch.uniforms.push_back(MakeComputeUniformVector3UVE("uAcceleration", acceleration));
    dispatch.uniforms.push_back(
        MakeComputeUniformIntUVE("uParticleCount", static_cast<std::int32_t>(particleCount)));
    if (!m_computeSystem.EnqueueDispatchUVE(dispatch)) {
        ++m_diagnostics.simulationsRejected;
        return false;
    }

    // This class drives its own submission rather than riding the frame queue. SimulateUVE()
    // promises integrated particles by the time it returns, and CS3's readback is only meaningful
    // after the dispatch has actually been submitted - leaving the dispatch on the frame queue
    // would read the buffer before the GPU ever saw the work.
    std::unique_ptr<ICommandBufferUVE> commands = m_device.CreateCommandBufferUVE();
    if (commands == nullptr) {
        m_computeSystem.ClearQueueUVE();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("ParticleComputeSimulationUVE could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatchesUVE(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.simulationsRejected;
        UVE_WARNING("ParticleComputeSimulationUVE recorded no dispatch; particle state is unchanged.");
        return false;
    }
    m_device.SubmitUVE(std::move(commands));

    const std::span<std::byte> readback{reinterpret_cast<std::byte*>(m_scratch.data()),
                                        m_scratch.size() * sizeof(ParticleGpuStateUVE)};
    if (!m_device.ReadbackBufferUVE(m_buffer, readback)) {
        ++m_diagnostics.readbackFailures;
        UVE_WARNING("ParticleComputeSimulationUVE could not read its simulation result back; "
                        "the CPU particle array is left unchanged.");
        return false;
    }

    // Validate the whole result before writing a single element home. The CPU runtime refuses to
    // half-apply a non-finite integration, and a GPU that produced one is exactly the case where
    // partial application would be worst: the caller would be left with an array that is neither
    // the old state nor a valid new one.
    for (const ParticleGpuStateUVE& packed : m_scratch) {
        if (!std::isfinite(packed.positionX) || !std::isfinite(packed.positionY) ||
            !std::isfinite(packed.positionZ) || !std::isfinite(packed.velocityX) ||
            !std::isfinite(packed.velocityY) || !std::isfinite(packed.velocityZ) ||
            !std::isfinite(packed.remainingLifetimeSeconds)) {
            ++m_diagnostics.readbackFailures;
            UVE_WARNING("ParticleComputeSimulationUVE rejected a non-finite integrated state atomically.");
            return false;
        }
    }

    // Cull and compact in place, in index order, exactly as the CPU path does - survivors keep
    // their relative order and their original sequence numbers, so the resulting array is
    // element-for-element identical to the CPU runtime's.
    std::size_t writeIndex = 0U;
    for (std::size_t index = 0U; index < particleCount; ++index) {
        const ParticleGpuStateUVE& packed = m_scratch[index];
        if (packed.remainingLifetimeSeconds <= 0.0F) {
            continue;
        }
        particles[writeIndex++] = Scene::ParticleStateUVE{
            Math::Vector3UVE{packed.positionX, packed.positionY, packed.positionZ},
            Math::Vector3UVE{packed.velocityX, packed.velocityY, packed.velocityZ},
            packed.remainingLifetimeSeconds, particles[index].sequence};
    }
    particles.resize(writeIndex);

    ++m_diagnostics.instancesSimulated;
    m_diagnostics.particlesSimulated += static_cast<std::uint64_t>(particleCount);
    return true;
}

const ParticleComputeSimulationDiagnosticsUVE& ParticleComputeSimulationUVE::GetDiagnosticsUVE() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
