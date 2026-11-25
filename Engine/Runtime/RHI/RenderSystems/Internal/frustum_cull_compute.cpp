// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "frustum_cull_compute.h"

#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

#include "logging_macros.h"
#include "vector3.h"
#include "built_in_compute_spirv.h"
#include "built_in_shaders.h"

namespace UVE::Render {
namespace {

constexpr std::size_t kFrustumPlaneCount = 6U;


/// Picks the kernel source the injected backend can actually consume. This is not a nicety: the
/// RHI's ShaderDesc takes each backend's own shader language - GLSL text on OpenGL, SPIR-V
/// bytes on Vulkan - and GLSL->SPIR-V translation at runtime remains an open ROADMAP item with no
/// toolchain in this build. Without this selection the compute workloads compile on GL and
/// silently refuse to initialize on Vulkan, which is a capability gap disguised as a working
/// feature. The Null backend compiles nothing, so either payload satisfies it; it gets the GLSL
/// so a Null-backed test reads like the GL one.
[[nodiscard]] std::string SelectKernelSource(const IRenderDevice& device,
                                                const std::string_view glslSource,
                                                const char* const spirvBytes,
                                                const std::size_t spirvSize) {
    if (device.GetBackendName().starts_with("Vulkan")) {
        // Length-explicit construction: SPIR-V is full of NUL bytes, and a cstring construction
        // would truncate at the first one (the M2a lesson, in the one place it still applies).
        return std::string(spirvBytes, spirvSize);
    }
    return std::string(glslSource);
}


/// Makes a just-submitted dispatch actually execute, on every backend, before its result is read.
///
/// This is the difference between the two backends' submission models, and it cost a red CI run to
/// find. GlRenderDevice runs every command at RECORD time - Submit only releases the buffer -
/// so a readback straight after submission sees finished work. VulkanRenderDevice queues the
/// recording into frameSubmissions and replays it in Present(); reading before that point
/// returns whatever the buffer held BEFORE the dispatch, which is exactly what the Vulkan arms of
/// CS4 and CS5 saw. Calling Present() is what drains the queue, and it is harmless on GL (a
/// buffer swap). CS3's readback then supplies the device synchronization on top.
void FlushComputeSubmission(IRenderDevice& device) {
    device.Present();
}

} // namespace

FrustumCullCompute::FrustumCullCompute(IRenderDevice& renderDevice,
                                             IComputeSystem& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

FrustumCullCompute::~FrustumCullCompute() {
    // The buffers are this class's own; the program belongs to the compute system, which is why it
    // goes back through DestroyProgram() rather than straight to the device.
    for (BufferHandle* const buffer : {&m_boxBuffer, &m_planeBuffer, &m_visibilityBuffer, &m_paramBuffer}) {
        if (*buffer != kInvalidBufferHandle) {
            m_device.DestroyBuffer(*buffer);
            *buffer = kInvalidBufferHandle;
        }
    }
    if (m_program != kInvalidPipelineHandle) {
        m_computeSystem.DestroyProgram(m_program);
        m_program = kInvalidPipelineHandle;
    }
}

bool FrustumCullCompute::Initialize(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandle) {
        return true;
    }

    ComputeProgramDesc desc;
    desc.sourceCode = SelectKernelSource(m_device, Shader::BuiltIn::kFrustumCullSource,
                                            BuiltInSpirv::kFrustumCullSpirvBytes,
                                            BuiltInSpirv::kFrustumCullSpirvSize);
    desc.debugName = "FrustumCull";
    m_program = m_computeSystem.CreateProgram(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandle) {
        UVE_WARNING("FrustumCullCompute could not create its cull kernel; callers must keep using "
                    "the CPU frustum test.");
        return false;
    }

    // The plane buffer is a fixed six planes for the life of the system - allocate it once here
    // rather than re-checking its existence on every cull.
    BufferDesc planeDesc;
    planeDesc.usage = BufferUsage::Storage;
    planeDesc.sizeBytes = kFrustumPlaneCount * sizeof(CullPlaneGpu);
    m_planeBuffer = m_device.CreateBuffer(planeDesc);
    if (m_planeBuffer == kInvalidBufferHandle) {
        UVE_WARNING("FrustumCullCompute could not allocate its frustum-plane buffer.");
        m_computeSystem.DestroyProgram(m_program);
        m_program = kInvalidPipelineHandle;
        return false;
    }

    BufferDesc paramDesc;
    paramDesc.usage = BufferUsage::Storage;
    paramDesc.sizeBytes = sizeof(FrustumCullParamsGpu);
    m_paramBuffer = m_device.CreateBuffer(paramDesc);
    if (m_paramBuffer == kInvalidBufferHandle) {
        UVE_WARNING("FrustumCullCompute could not allocate its parameter buffer.");
        m_device.DestroyBuffer(m_planeBuffer);
        m_planeBuffer = kInvalidBufferHandle;
        m_computeSystem.DestroyProgram(m_program);
        m_program = kInvalidPipelineHandle;
        return false;
    }
    return true;
}

bool FrustumCullCompute::IsReady() const noexcept {
    return m_program != kInvalidPipelineHandle;
}

bool FrustumCullCompute::EnsureBufferCapacity(const std::size_t boxCount) {
    if (m_boxBuffer != kInvalidBufferHandle && m_bufferCapacityBoxes >= boxCount) {
        return true;
    }

    // Grow-only, and never shrink back: a caller whose box count oscillates below its own
    // high-water mark should stop touching the allocator after the first few frames.
    for (BufferHandle* const buffer : {&m_boxBuffer, &m_visibilityBuffer}) {
        if (*buffer != kInvalidBufferHandle) {
            m_device.DestroyBuffer(*buffer);
            *buffer = kInvalidBufferHandle;
        }
    }
    m_bufferCapacityBoxes = 0U;

    BufferDesc boxDesc;
    boxDesc.usage = BufferUsage::Storage;
    boxDesc.sizeBytes = boxCount * sizeof(CullBoxGpu);
    m_boxBuffer = m_device.CreateBuffer(boxDesc);

    BufferDesc visibilityDesc;
    visibilityDesc.usage = BufferUsage::Storage;
    visibilityDesc.sizeBytes = boxCount * sizeof(std::uint32_t);
    visibilityDesc.memory = BufferMemory::Readback; // compute writes it, the CPU reads it every pass
    visibilityDesc.updateFrequency = BufferUpdateFrequency::Dynamic;
    m_visibilityBuffer = m_device.CreateBuffer(visibilityDesc);

    if (m_boxBuffer == kInvalidBufferHandle || m_visibilityBuffer == kInvalidBufferHandle) {
        UVE_WARNING("FrustumCullCompute failed to allocate its cull buffers.");
        return false;
    }
    m_bufferCapacityBoxes = boxCount;
    ++m_diagnostics.bufferReallocations;
    return true;
}

bool FrustumCullCompute::Cull(const std::span<const Math::Aabb> boxes,
                                    const Math::Frustum& frustum, std::vector<bool>& outVisible) {
    ++m_diagnostics.cullsRequested;

    if (!IsReady()) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullCompute was asked to cull before its kernel was ready.");
        return false;
    }
    if (boxes.empty()) {
        ++m_diagnostics.cullsSkipped;
        outVisible.clear();
        return true;
    }

    // Refuse non-finite input rather than forwarding it. The CPU test absorbs it through Plane's
    // double-precision fallback, which a float shader cannot reproduce - so this path would answer
    // differently, and answering differently is worse than saying honestly that it does not cover
    // the case. The caller still has the CPU test.
    for (const Math::Plane& plane : frustum.planes) {
        if (!Math::IsFinite(plane.normal) || !std::isfinite(plane.distance)) {
            ++m_diagnostics.cullsRejected;
            UVE_WARNING("FrustumCullCompute refuses a non-finite frustum plane; the CPU test is "
                        "the path that handles those.");
            return false;
        }
    }

    const std::size_t boxCount = boxes.size();
    m_boxScratch.assign(boxCount, CullBoxGpu{});
    for (std::size_t index = 0U; index < boxCount; ++index) {
        const Math::Vector3 center = boxes[index].GetCenter();
        const Math::Vector3 extents = boxes[index].GetExtents();
        if (!Math::IsFinite(center) || !Math::IsFinite(extents)) {
            ++m_diagnostics.cullsRejected;
            UVE_WARNING("FrustumCullCompute refuses a non-finite bounding box; the CPU test is "
                        "the path that handles those.");
            return false;
        }
        CullBoxGpu& packed = m_boxScratch[index];
        packed.centerX = center.x;
        packed.centerY = center.y;
        packed.centerZ = center.z;
        packed.extentX = extents.x;
        packed.extentY = extents.y;
        packed.extentZ = extents.z;
    }

    if (!EnsureBufferCapacity(boxCount)) {
        ++m_diagnostics.uploadFailures;
        return false;
    }

    std::array<CullPlaneGpu, kFrustumPlaneCount> packedPlanes{};
    for (std::size_t index = 0U; index < kFrustumPlaneCount; ++index) {
        packedPlanes[index].normalX = frustum.planes[index].normal.x;
        packedPlanes[index].normalY = frustum.planes[index].normal.y;
        packedPlanes[index].normalZ = frustum.planes[index].normal.z;
        packedPlanes[index].distance = frustum.planes[index].distance;
    }

    const std::span<const std::byte> boxUpload{reinterpret_cast<const std::byte*>(m_boxScratch.data()),
                                               m_boxScratch.size() * sizeof(CullBoxGpu)};
    const std::span<const std::byte> planeUpload{reinterpret_cast<const std::byte*>(packedPlanes.data()),
                                                 packedPlanes.size() * sizeof(CullPlaneGpu)};
    FrustumCullParamsGpu params;
    params.boxCount = static_cast<std::int32_t>(boxCount);
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBuffer(m_boxBuffer, boxUpload) ||
        !m_device.UpdateBuffer(m_planeBuffer, planeUpload) ||
        !m_device.UpdateBuffer(m_paramBuffer, paramUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullCompute could not upload its cull inputs.");
        return false;
    }

    ComputeDispatchDesc dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX = static_cast<std::uint32_t>((boxCount + kWorkgroupSize - 1U) / kWorkgroupSize);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_boxBuffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_planeBuffer, 1U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_visibilityBuffer, 2U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_paramBuffer, 3U});
    if (!m_computeSystem.EnqueueDispatch(dispatch)) {
        ++m_diagnostics.cullsRejected;
        return false;
    }

    // Own submission rather than the frame queue, for the same reason CS4's particle simulation
    // does it: Cull() promises an answer by the time it returns, and reading the visibility
    // buffer before the dispatch has been submitted would read whatever was there before.
    std::unique_ptr<ICommandBuffer> commands = m_device.CreateCommandBuffer();
    if (commands == nullptr) {
        m_computeSystem.ClearQueue();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullCompute could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatches(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullCompute recorded no dispatch; no visibility was produced.");
        return false;
    }
    m_device.Submit(std::move(commands));
    FlushComputeSubmission(m_device);

    m_visibilityScratch.assign(boxCount, 0U);
    const std::span<std::byte> readback{reinterpret_cast<std::byte*>(m_visibilityScratch.data()),
                                        m_visibilityScratch.size() * sizeof(std::uint32_t)};
    if (!m_device.ReadbackBuffer(m_visibilityBuffer, readback)) {
        ++m_diagnostics.readbackFailures;
        UVE_WARNING("FrustumCullCompute could not read its visibility result back.");
        return false;
    }

    outVisible.assign(boxCount, false);
    std::uint64_t visibleCount = 0U;
    for (std::size_t index = 0U; index < boxCount; ++index) {
        const bool isVisible = m_visibilityScratch[index] != 0U;
        outVisible[index] = isVisible;
        visibleCount += isVisible ? 1U : 0U;
    }

    m_diagnostics.boxesTested += static_cast<std::uint64_t>(boxCount);
    m_diagnostics.boxesVisible += visibleCount;
    m_diagnostics.boxesCulled += static_cast<std::uint64_t>(boxCount) - visibleCount;
    return true;
}

const FrustumCullComputeDiagnostics& FrustumCullCompute::GetDiagnostics() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
