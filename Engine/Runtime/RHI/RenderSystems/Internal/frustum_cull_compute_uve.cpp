// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/frustum_cull_compute_uve.h"

#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi_shader/built_in_compute_spirv_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"

namespace UVE::Render {
namespace {

constexpr std::size_t kFrustumPlaneCountUVE = 6U;

[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& vector) noexcept {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
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

FrustumCullComputeUVE::FrustumCullComputeUVE(IRenderDeviceUVE& renderDevice,
                                             IComputeSystemUVE& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

FrustumCullComputeUVE::~FrustumCullComputeUVE() {
    // The buffers are this class's own; the program belongs to the compute system, which is why it
    // goes back through DestroyProgramUVE() rather than straight to the device.
    for (BufferHandleUVE* const buffer : {&m_boxBuffer, &m_planeBuffer, &m_visibilityBuffer, &m_paramBuffer}) {
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

bool FrustumCullComputeUVE::InitializeUVE(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandleUVE) {
        return true;
    }

    ComputeProgramDescUVE desc;
    desc.sourceCode = SelectKernelSourceUVE(m_device, Shader::BuiltIn::kFrustumCullSource,
                                            BuiltInSpirv::kFrustumCullSpirvBytesUVE,
                                            BuiltInSpirv::kFrustumCullSpirvSizeUVE);
    desc.debugName = "FrustumCull";
    m_program = m_computeSystem.CreateProgramUVE(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandleUVE) {
        UVE_WARNING("FrustumCullComputeUVE could not create its cull kernel; callers must keep using "
                    "the CPU frustum test.");
        return false;
    }

    // The plane buffer is a fixed six planes for the life of the system - allocate it once here
    // rather than re-checking its existence on every cull.
    BufferDescUVE planeDesc;
    planeDesc.usage = BufferUsageUVE::Storage;
    planeDesc.sizeBytes = kFrustumPlaneCountUVE * sizeof(CullPlaneGpuUVE);
    m_planeBuffer = m_device.CreateBufferUVE(planeDesc);
    if (m_planeBuffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("FrustumCullComputeUVE could not allocate its frustum-plane buffer.");
        m_computeSystem.DestroyProgramUVE(m_program);
        m_program = kInvalidPipelineHandleUVE;
        return false;
    }

    BufferDescUVE paramDesc;
    paramDesc.usage = BufferUsageUVE::Storage;
    paramDesc.sizeBytes = sizeof(FrustumCullParamsGpuUVE);
    m_paramBuffer = m_device.CreateBufferUVE(paramDesc);
    if (m_paramBuffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("FrustumCullComputeUVE could not allocate its parameter buffer.");
        m_device.DestroyBufferUVE(m_planeBuffer);
        m_planeBuffer = kInvalidBufferHandleUVE;
        m_computeSystem.DestroyProgramUVE(m_program);
        m_program = kInvalidPipelineHandleUVE;
        return false;
    }
    return true;
}

bool FrustumCullComputeUVE::IsReadyUVE() const noexcept {
    return m_program != kInvalidPipelineHandleUVE;
}

bool FrustumCullComputeUVE::EnsureBufferCapacityUVE(const std::size_t boxCount) {
    if (m_boxBuffer != kInvalidBufferHandleUVE && m_bufferCapacityBoxes >= boxCount) {
        return true;
    }

    // Grow-only, and never shrink back: a caller whose box count oscillates below its own
    // high-water mark should stop touching the allocator after the first few frames.
    for (BufferHandleUVE* const buffer : {&m_boxBuffer, &m_visibilityBuffer}) {
        if (*buffer != kInvalidBufferHandleUVE) {
            m_device.DestroyBufferUVE(*buffer);
            *buffer = kInvalidBufferHandleUVE;
        }
    }
    m_bufferCapacityBoxes = 0U;

    BufferDescUVE boxDesc;
    boxDesc.usage = BufferUsageUVE::Storage;
    boxDesc.sizeBytes = boxCount * sizeof(CullBoxGpuUVE);
    m_boxBuffer = m_device.CreateBufferUVE(boxDesc);

    BufferDescUVE visibilityDesc;
    visibilityDesc.usage = BufferUsageUVE::Storage;
    visibilityDesc.sizeBytes = boxCount * sizeof(std::uint32_t);
    m_visibilityBuffer = m_device.CreateBufferUVE(visibilityDesc);

    if (m_boxBuffer == kInvalidBufferHandleUVE || m_visibilityBuffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("FrustumCullComputeUVE failed to allocate its cull buffers.");
        return false;
    }
    m_bufferCapacityBoxes = boxCount;
    ++m_diagnostics.bufferReallocations;
    return true;
}

bool FrustumCullComputeUVE::CullUVE(const std::span<const Math::AabbUVE> boxes,
                                    const Math::FrustumUVE& frustum, std::vector<bool>& outVisible) {
    ++m_diagnostics.cullsRequested;

    if (!IsReadyUVE()) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullComputeUVE was asked to cull before its kernel was ready.");
        return false;
    }
    if (boxes.empty()) {
        ++m_diagnostics.cullsSkipped;
        outVisible.clear();
        return true;
    }

    // Refuse non-finite input rather than forwarding it. The CPU test absorbs it through PlaneUVE's
    // double-precision fallback, which a float shader cannot reproduce - so this path would answer
    // differently, and answering differently is worse than saying honestly that it does not cover
    // the case. The caller still has the CPU test.
    for (const Math::PlaneUVE& plane : frustum.planes) {
        if (!IsFiniteVectorUVE(plane.normal) || !std::isfinite(plane.distance)) {
            ++m_diagnostics.cullsRejected;
            UVE_WARNING("FrustumCullComputeUVE refuses a non-finite frustum plane; the CPU test is "
                        "the path that handles those.");
            return false;
        }
    }

    const std::size_t boxCount = boxes.size();
    m_boxScratch.assign(boxCount, CullBoxGpuUVE{});
    for (std::size_t index = 0U; index < boxCount; ++index) {
        const Math::Vector3UVE center = boxes[index].GetCenterUVE();
        const Math::Vector3UVE extents = boxes[index].GetExtentsUVE();
        if (!IsFiniteVectorUVE(center) || !IsFiniteVectorUVE(extents)) {
            ++m_diagnostics.cullsRejected;
            UVE_WARNING("FrustumCullComputeUVE refuses a non-finite bounding box; the CPU test is "
                        "the path that handles those.");
            return false;
        }
        CullBoxGpuUVE& packed = m_boxScratch[index];
        packed.centerX = center.x;
        packed.centerY = center.y;
        packed.centerZ = center.z;
        packed.extentX = extents.x;
        packed.extentY = extents.y;
        packed.extentZ = extents.z;
    }

    if (!EnsureBufferCapacityUVE(boxCount)) {
        ++m_diagnostics.uploadFailures;
        return false;
    }

    std::array<CullPlaneGpuUVE, kFrustumPlaneCountUVE> packedPlanes{};
    for (std::size_t index = 0U; index < kFrustumPlaneCountUVE; ++index) {
        packedPlanes[index].normalX = frustum.planes[index].normal.x;
        packedPlanes[index].normalY = frustum.planes[index].normal.y;
        packedPlanes[index].normalZ = frustum.planes[index].normal.z;
        packedPlanes[index].distance = frustum.planes[index].distance;
    }

    const std::span<const std::byte> boxUpload{reinterpret_cast<const std::byte*>(m_boxScratch.data()),
                                               m_boxScratch.size() * sizeof(CullBoxGpuUVE)};
    const std::span<const std::byte> planeUpload{reinterpret_cast<const std::byte*>(packedPlanes.data()),
                                                 packedPlanes.size() * sizeof(CullPlaneGpuUVE)};
    FrustumCullParamsGpuUVE params;
    params.boxCount = static_cast<std::int32_t>(boxCount);
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBufferUVE(m_boxBuffer, boxUpload) ||
        !m_device.UpdateBufferUVE(m_planeBuffer, planeUpload) ||
        !m_device.UpdateBufferUVE(m_paramBuffer, paramUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullComputeUVE could not upload its cull inputs.");
        return false;
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX = static_cast<std::uint32_t>((boxCount + kWorkgroupSizeUVE - 1U) / kWorkgroupSizeUVE);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_boxBuffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_planeBuffer, 1U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_visibilityBuffer, 2U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_paramBuffer, 3U});
    if (!m_computeSystem.EnqueueDispatchUVE(dispatch)) {
        ++m_diagnostics.cullsRejected;
        return false;
    }

    // Own submission rather than the frame queue, for the same reason CS4's particle simulation
    // does it: CullUVE() promises an answer by the time it returns, and reading the visibility
    // buffer before the dispatch has been submitted would read whatever was there before.
    std::unique_ptr<ICommandBufferUVE> commands = m_device.CreateCommandBufferUVE();
    if (commands == nullptr) {
        m_computeSystem.ClearQueueUVE();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullComputeUVE could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatchesUVE(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullComputeUVE recorded no dispatch; no visibility was produced.");
        return false;
    }
    m_device.SubmitUVE(std::move(commands));
    FlushComputeSubmissionUVE(m_device);

    m_visibilityScratch.assign(boxCount, 0U);
    const std::span<std::byte> readback{reinterpret_cast<std::byte*>(m_visibilityScratch.data()),
                                        m_visibilityScratch.size() * sizeof(std::uint32_t)};
    if (!m_device.ReadbackBufferUVE(m_visibilityBuffer, readback)) {
        ++m_diagnostics.readbackFailures;
        UVE_WARNING("FrustumCullComputeUVE could not read its visibility result back.");
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

const FrustumCullComputeDiagnosticsUVE& FrustumCullComputeUVE::GetDiagnosticsUVE() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
