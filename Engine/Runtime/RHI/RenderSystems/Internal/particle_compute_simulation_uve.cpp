// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/particle_compute_simulation_uve.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>
#include <span>
#include <utility>

#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi_shader/built_in_compute_spirv_uve.h"
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


/// Picks the kernel source the injected backend can actually consume. This is not a nicety: the
/// RHI's ShaderDescUVE takes each backend's own shader language - GLSL text on OpenGL, SPIR-V
/// bytes on Vulkan - and GLSL->SPIR-V translation at runtime remains an open ROADMAP item with no
/// toolchain in this build. Without this selection the compute workloads compile on GL and
/// silently refuse to initialize on Vulkan, which is a capability gap disguised as a working
/// feature. The Null backend compiles nothing, so either payload satisfies it; it gets the GLSL
/// so a Null-backed test reads like the GL one.
[[nodiscard]] std::string SelectKernelSourceUVE(const IRenderDeviceUVE& device,
                                                const std::string_view glslSource,
                                                const char* const spirvBytes,
                                                const std::size_t spirvSize) {
    if (device.GetBackendNameUVE().starts_with("Vulkan")) {
        // Length-explicit construction: SPIR-V is full of NUL bytes, and a cstring construction
        // would truncate at the first one (the M2a lesson, in the one place it still applies).
        return std::string(spirvBytes, spirvSize);
    }
    return std::string(glslSource);
}


/// Makes a just-submitted dispatch actually execute, on every backend, before its result is read.
///
/// This is the difference between the two backends' submission models, and it cost a red CI run to
/// find. GlRenderDeviceUVE runs every command at RECORD time - SubmitUVE only releases the buffer -
/// so a readback straight after submission sees finished work. VulkanRenderDeviceUVE queues the
/// recording into frameSubmissions and replays it in PresentUVE(); reading before that point
/// returns whatever the buffer held BEFORE the dispatch, which is exactly what the Vulkan arms of
/// CS4 and CS5 saw. Calling PresentUVE() is what drains the queue, and it is harmless on GL (a
/// buffer swap). CS3's readback then supplies the device synchronization on top.
void FlushComputeSubmissionUVE(IRenderDeviceUVE& device) {
    device.PresentUVE();
}

} // namespace

ParticleComputeSimulationUVE::ParticleComputeSimulationUVE(IRenderDeviceUVE& renderDevice,
                                                           IComputeSystemUVE& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

ParticleComputeSimulationUVE::~ParticleComputeSimulationUVE() {
    // The buffer is this class's own; the program belongs to the compute system, which is why it
    // goes back through DestroyProgramUVE() rather than straight to the device.
    for (BufferHandleUVE* const buffer : {&m_buffer, &m_paramBuffer}) {
        if (*buffer != kInvalidBufferHandleUVE) {
            m_device.DestroyBufferUVE(*buffer);
            *buffer = kInvalidBufferHandleUVE;
        }
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
    desc.sourceCode = SelectKernelSourceUVE(m_device, Shader::BuiltIn::kParticleSimulateSource,
                                            BuiltInSpirv::kParticleSimulateSpirvBytesUVE,
                                            BuiltInSpirv::kParticleSimulateSpirvSizeUVE);
    desc.debugName = "ParticleSimulate";
    m_program = m_computeSystem.CreateProgramUVE(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandleUVE) {
        UVE_WARNING("ParticleComputeSimulationUVE could not create its simulation kernel; "
                        "callers must keep using the CPU particle simulation.");
        return false;
    }

    // The parameter block is one small fixed-size record for the life of the system - allocate it
    // once here rather than re-checking it on every simulation step.
    BufferDescUVE paramDesc;
    paramDesc.usage = BufferUsageUVE::Storage;
    paramDesc.sizeBytes = sizeof(ParticleSimulateParamsGpuUVE);
    m_paramBuffer = m_device.CreateBufferUVE(paramDesc);
    if (m_paramBuffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("ParticleComputeSimulationUVE could not allocate its parameter buffer.");
        m_computeSystem.DestroyProgramUVE(m_program);
        m_program = kInvalidPipelineHandleUVE;
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
    ParticleSimulateParamsGpuUVE params;
    params.deltaSeconds = deltaSeconds;
    params.accelerationX = acceleration.x;
    params.accelerationY = acceleration.y;
    params.accelerationZ = acceleration.z;
    params.particleCount = static_cast<std::int32_t>(particleCount);
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBufferUVE(m_buffer, upload) ||
        !m_device.UpdateBufferUVE(m_paramBuffer, paramUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("ParticleComputeSimulationUVE could not upload particle state; the CPU array is unchanged.");
        return false;
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX =
        static_cast<std::uint32_t>((particleCount + kWorkgroupSizeUVE - 1U) / kWorkgroupSizeUVE);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_buffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_paramBuffer, 1U});
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
    FlushComputeSubmissionUVE(m_device);

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
