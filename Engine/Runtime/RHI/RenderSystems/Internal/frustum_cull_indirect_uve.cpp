// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/frustum_cull_indirect_uve.h"

#include <array>
#include <cmath>
#include <cstring>
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

/// See the identical helper in frustum_cull_compute_uve.cpp: the RHI takes each backend's own
/// shader language, and runtime GLSL->SPIR-V translation is still an open ROADMAP item.
[[nodiscard]] std::string SelectKernelSourceUVE(const IRenderDeviceUVE& device,
                                                const std::string_view glslSource,
                                                const char* const spirvBytes,
                                                const std::size_t spirvSize) {
    if (device.GetBackendNameUVE().starts_with("Vulkan")) {
        return std::string(spirvBytes, spirvSize);
    }
    return std::string(glslSource);
}

/// The two backends do not submit at the same time - GL executes at record time, Vulkan replays in
/// PresentUVE(). Unlike CS5 this system does not read anything back, so it does not strictly need
/// the flush for its own sake; it issues one anyway because the contract is that the draw command
/// is USABLE when PrepareUVE() returns, and on Vulkan a dispatch still sitting in frameSubmissions
/// has not written it yet. A caller recording an indirect draw immediately afterwards would
/// otherwise draw with whatever instanceCount the buffer held before.
void FlushComputeSubmissionUVE(IRenderDeviceUVE& device) {
    device.PresentUVE();
}

} // namespace

FrustumCullIndirectUVE::FrustumCullIndirectUVE(IRenderDeviceUVE& renderDevice,
                                               IComputeSystemUVE& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

FrustumCullIndirectUVE::~FrustumCullIndirectUVE() {
    for (BufferHandleUVE* const buffer : {&m_boxBuffer, &m_planeBuffer, &m_drawCommandBuffer,
                                          &m_visibleIndexBuffer, &m_paramBuffer}) {
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

bool FrustumCullIndirectUVE::InitializeUVE(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandleUVE) {
        return true;
    }

    ComputeProgramDescUVE desc;
    desc.sourceCode = SelectKernelSourceUVE(m_device, Shader::BuiltIn::kFrustumCullIndirectSource,
                                            BuiltInSpirv::kFrustumCullIndirectSpirvBytesUVE,
                                            BuiltInSpirv::kFrustumCullIndirectSpirvSizeUVE);
    desc.debugName = "FrustumCullIndirect";
    m_program = m_computeSystem.CreateProgramUVE(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandleUVE) {
        UVE_WARNING("FrustumCullIndirectUVE could not create its cull kernel; callers must keep "
                    "culling on the CPU and drawing with DrawIndexedUVE.");
        return false;
    }

    // Three fixed-size buffers, allocated once: six planes, one draw command, one int of
    // parameters. Only the per-box buffers grow with the workload.
    struct FixedBufferUVE final {
        BufferHandleUVE* handle;
        std::uint64_t sizeBytes;
        BufferUsageUVE usage;
        const char* name;
    };
    const std::array<FixedBufferUVE, 3U> fixedBuffers{{
        {&m_planeBuffer, kFrustumPlaneCountUVE * sizeof(CullPlaneGpuUVE), BufferUsageUVE::Storage,
         "frustum-plane"},
        // IndirectStorage, not Storage: the kernel writes instanceCount through an SSBO binding
        // and the draw reads the same memory as indirect parameters. That dual role is exactly
        // what CS7 added the usage for.
        {&m_drawCommandBuffer, sizeof(DrawIndexedIndirectCommandUVE),
         BufferUsageUVE::IndirectStorage, "draw-command"},
        {&m_paramBuffer, sizeof(FrustumCullIndirectParamsGpuUVE), BufferUsageUVE::Storage,
         "parameter"},
    }};

    for (const FixedBufferUVE& fixed : fixedBuffers) {
        BufferDescUVE bufferDesc;
        bufferDesc.usage = fixed.usage;
        bufferDesc.sizeBytes = fixed.sizeBytes;
        *fixed.handle = m_device.CreateBufferUVE(bufferDesc);
        if (*fixed.handle == kInvalidBufferHandleUVE) {
            UVE_WARNING("FrustumCullIndirectUVE could not allocate its {} buffer.", fixed.name);
            // Unwind everything allocated so far rather than leaking on a partial failure.
            for (const FixedBufferUVE& allocated : fixedBuffers) {
                if (*allocated.handle != kInvalidBufferHandleUVE) {
                    m_device.DestroyBufferUVE(*allocated.handle);
                    *allocated.handle = kInvalidBufferHandleUVE;
                }
            }
            m_computeSystem.DestroyProgramUVE(m_program);
            m_program = kInvalidPipelineHandleUVE;
            return false;
        }
    }
    return true;
}

bool FrustumCullIndirectUVE::IsReadyUVE() const noexcept {
    return m_program != kInvalidPipelineHandleUVE;
}

bool FrustumCullIndirectUVE::EnsureBufferCapacityUVE(const std::size_t boxCount) {
    if (m_boxBuffer != kInvalidBufferHandleUVE && m_bufferCapacityBoxes >= boxCount) {
        return true;
    }

    for (BufferHandleUVE* const buffer : {&m_boxBuffer, &m_visibleIndexBuffer}) {
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

    // One slot per box, because the worst case is everything visible. The kernel relies on this
    // exactly: its atomic hands out slots with no bounds check, which is only safe while capacity
    // equals the box count. Shrinking this to some expected-survivor estimate would turn a full
    // frustum into an out-of-bounds write.
    BufferDescUVE indexDesc;
    indexDesc.usage = BufferUsageUVE::Storage;
    indexDesc.sizeBytes = boxCount * sizeof(std::uint32_t);
    m_visibleIndexBuffer = m_device.CreateBufferUVE(indexDesc);

    if (m_boxBuffer == kInvalidBufferHandleUVE || m_visibleIndexBuffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("FrustumCullIndirectUVE failed to allocate its per-box buffers.");
        return false;
    }
    m_bufferCapacityBoxes = boxCount;
    ++m_diagnostics.bufferReallocations;
    return true;
}

bool FrustumCullIndirectUVE::PrepareUVE(const std::span<const Math::AabbUVE> boxes,
                                        const Math::FrustumUVE& frustum,
                                        const DrawIndexedIndirectCommandUVE& meshParams) {
    ++m_diagnostics.cullsRequested;

    if (!IsReadyUVE()) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullIndirectUVE was asked to prepare a draw before its kernel was ready.");
        return false;
    }

    // The seed command, written before the dispatch. instanceCount starts at zero no matter what
    // the caller put there - the GPU's atomic is what fills it, and a non-zero seed would silently
    // add phantom instances.
    DrawIndexedIndirectCommandUVE seedCommand = meshParams;
    seedCommand.instanceCount = 0U;
    const std::span<const std::byte> commandUpload{
        reinterpret_cast<const std::byte*>(&seedCommand), sizeof(seedCommand)};

    if (boxes.empty()) {
        // Still write the zero-instance command: a caller that unconditionally records an indirect
        // draw must get a well-formed no-op, not last frame's parameters.
        if (!m_device.UpdateBufferUVE(m_drawCommandBuffer, commandUpload)) {
            ++m_diagnostics.uploadFailures;
            return false;
        }
        ++m_diagnostics.cullsSkipped;
        ++m_diagnostics.drawsPrepared;
        return true;
    }

    for (const Math::PlaneUVE& plane : frustum.planes) {
        if (!IsFiniteVectorUVE(plane.normal) || !std::isfinite(plane.distance)) {
            ++m_diagnostics.cullsRejected;
            UVE_WARNING("FrustumCullIndirectUVE refuses a non-finite frustum plane; the CPU test "
                        "is the path that handles those.");
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
            UVE_WARNING("FrustumCullIndirectUVE refuses a non-finite bounding box; the CPU test is "
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

    FrustumCullIndirectParamsGpuUVE params;
    params.boxCount = static_cast<std::int32_t>(boxCount);

    const std::span<const std::byte> boxUpload{
        reinterpret_cast<const std::byte*>(m_boxScratch.data()),
        m_boxScratch.size() * sizeof(CullBoxGpuUVE)};
    const std::span<const std::byte> planeUpload{
        reinterpret_cast<const std::byte*>(packedPlanes.data()),
        packedPlanes.size() * sizeof(CullPlaneGpuUVE)};
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBufferUVE(m_boxBuffer, boxUpload) ||
        !m_device.UpdateBufferUVE(m_planeBuffer, planeUpload) ||
        !m_device.UpdateBufferUVE(m_paramBuffer, paramUpload) ||
        !m_device.UpdateBufferUVE(m_drawCommandBuffer, commandUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullIndirectUVE could not upload its cull inputs.");
        return false;
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX =
        static_cast<std::uint32_t>((boxCount + kWorkgroupSizeUVE - 1U) / kWorkgroupSizeUVE);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_boxBuffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_planeBuffer, 1U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_drawCommandBuffer, 2U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_visibleIndexBuffer, 3U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_paramBuffer, 4U});
    if (!m_computeSystem.EnqueueDispatchUVE(dispatch)) {
        ++m_diagnostics.cullsRejected;
        return false;
    }

    std::unique_ptr<ICommandBufferUVE> commands = m_device.CreateCommandBufferUVE();
    if (commands == nullptr) {
        m_computeSystem.ClearQueueUVE();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullIndirectUVE could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatchesUVE(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullIndirectUVE recorded no dispatch; the draw command still holds a "
                    "zero instance count.");
        return false;
    }
    m_device.SubmitUVE(std::move(commands));
    FlushComputeSubmissionUVE(m_device);

    // Note what does NOT happen here: no readback. The result stays on the GPU, which is the whole
    // reason this class exists next to FrustumCullComputeUVE.
    m_diagnostics.boxesTested += static_cast<std::uint64_t>(boxCount);
    ++m_diagnostics.drawsPrepared;
    return true;
}

BufferHandleUVE FrustumCullIndirectUVE::GetDrawCommandBufferUVE() const noexcept {
    return m_drawCommandBuffer;
}

BufferHandleUVE FrustumCullIndirectUVE::GetVisibleIndexBufferUVE() const noexcept {
    return m_visibleIndexBuffer;
}

const FrustumCullIndirectDiagnosticsUVE& FrustumCullIndirectUVE::GetDiagnosticsUVE() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
