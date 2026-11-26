// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "frustum_cull_indirect.h"

#include <array>
#include <cmath>
#include <cstring>
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

/// See the identical helper in frustum_cull_compute_uve.cpp: the RHI takes each backend's own
/// shader language, and runtime GLSL->SPIR-V translation is still an open ROADMAP item.
[[nodiscard]] std::string SelectKernelSource(const IRenderDevice& device,
                                                const std::string_view glslSource,
                                                const char* const spirvBytes,
                                                const std::size_t spirvSize) {
    if (device.GetBackendName().starts_with("Vulkan")) {
        return std::string(spirvBytes, spirvSize);
    }
    return std::string(glslSource);
}

/// The two backends do not submit at the same time - GL executes at record time, Vulkan replays in
/// Present(). Unlike CS5 this system does not read anything back, so it does not strictly need
/// the flush for its own sake; it issues one anyway because the contract is that the draw command
/// is USABLE when Prepare() returns, and on Vulkan a dispatch still sitting in frameSubmissions
/// has not written it yet. A caller recording an indirect draw immediately afterwards would
/// otherwise draw with whatever instanceCount the buffer held before.
void FlushComputeSubmission(IRenderDevice& device) {
    if (device.GetBackendName().starts_with("Vulkan")) {
        device.Present();
    }
}

} // namespace

FrustumCullIndirect::FrustumCullIndirect(IRenderDevice& renderDevice,
                                               IComputeSystem& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

FrustumCullIndirect::~FrustumCullIndirect() {
    for (BufferHandle* const buffer : {&m_boxBuffer, &m_planeBuffer, &m_drawCommandBuffer,
                                          &m_visibleIndexBuffer, &m_paramBuffer}) {
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

bool FrustumCullIndirect::Initialize(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandle) {
        return true;
    }

    ComputeProgramDesc desc;
    desc.sourceCode = SelectKernelSource(m_device, Shader::BuiltIn::kFrustumCullIndirectSource,
                                            BuiltInSpirv::kFrustumCullIndirectSpirvBytes,
                                            BuiltInSpirv::kFrustumCullIndirectSpirvSize);
    desc.debugName = "FrustumCullIndirect";
    m_program = m_computeSystem.CreateProgram(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandle) {
        UVE_WARNING("FrustumCullIndirect could not create its cull kernel; callers must keep "
                    "culling on the CPU and drawing with DrawIndexed.");
        return false;
    }

    // Three fixed-size buffers, allocated once: six planes, one draw command, one int of
    // parameters. Only the per-box buffers grow with the workload.
    struct FixedBuffer final {
        BufferHandle* handle;
        std::uint64_t sizeBytes;
        BufferUsage usage;
        const char* name;
    };
    const std::array<FixedBuffer, 3U> fixedBuffers{{
        {&m_planeBuffer, kFrustumPlaneCount * sizeof(CullPlaneGpu), BufferUsage::Storage,
         "frustum-plane"},
        // IndirectStorage, not Storage: the kernel writes instanceCount through an SSBO binding
        // and the draw reads the same memory as indirect parameters. That dual role is exactly
        // what CS7 added the usage for.
        {&m_drawCommandBuffer, sizeof(DrawIndexedIndirectCommand),
         BufferUsage::IndirectStorage, "draw-command"},
        {&m_paramBuffer, sizeof(FrustumCullIndirectParamsGpu), BufferUsage::Storage,
         "parameter"},
    }};

    for (const FixedBuffer& fixed : fixedBuffers) {
        BufferDesc bufferDesc;
        bufferDesc.usage = fixed.usage;
        bufferDesc.sizeBytes = fixed.sizeBytes;
        bufferDesc.updateFrequency = BufferUpdateFrequency::Dynamic; // rewritten every pass
        *fixed.handle = m_device.CreateBuffer(bufferDesc);
        if (*fixed.handle == kInvalidBufferHandle) {
            UVE_WARNING("FrustumCullIndirect could not allocate its {} buffer.", fixed.name);
            // Unwind everything allocated so far rather than leaking on a partial failure.
            for (const FixedBuffer& allocated : fixedBuffers) {
                if (*allocated.handle != kInvalidBufferHandle) {
                    m_device.DestroyBuffer(*allocated.handle);
                    *allocated.handle = kInvalidBufferHandle;
                }
            }
            m_computeSystem.DestroyProgram(m_program);
            m_program = kInvalidPipelineHandle;
            return false;
        }
    }
    return true;
}

bool FrustumCullIndirect::IsReady() const noexcept {
    return m_program != kInvalidPipelineHandle;
}

bool FrustumCullIndirect::EnsureBufferCapacity(const std::size_t boxCount) {
    if (m_boxBuffer != kInvalidBufferHandle && m_bufferCapacityBoxes >= boxCount) {
        return true;
    }

    for (BufferHandle* const buffer : {&m_boxBuffer, &m_visibleIndexBuffer}) {
        if (*buffer != kInvalidBufferHandle) {
            m_device.DestroyBuffer(*buffer);
            *buffer = kInvalidBufferHandle;
        }
    }
    m_bufferCapacityBoxes = 0U;

    BufferDesc boxDesc;
    boxDesc.usage = BufferUsage::Storage;
    boxDesc.sizeBytes = boxCount * sizeof(CullBoxGpu);
    boxDesc.updateFrequency = BufferUpdateFrequency::Dynamic; // rewritten every pass
    m_boxBuffer = m_device.CreateBuffer(boxDesc);

    // One slot per box, because the worst case is everything visible. The kernel relies on this
    // exactly: its atomic hands out slots with no bounds check, which is only safe while capacity
    // equals the box count. Shrinking this to some expected-survivor estimate would turn a full
    // frustum into an out-of-bounds write.
    BufferDesc indexDesc;
    indexDesc.usage = BufferUsage::Storage;
    indexDesc.sizeBytes = boxCount * sizeof(std::uint32_t);
    m_visibleIndexBuffer = m_device.CreateBuffer(indexDesc);

    if (m_boxBuffer == kInvalidBufferHandle || m_visibleIndexBuffer == kInvalidBufferHandle) {
        UVE_WARNING("FrustumCullIndirect failed to allocate its per-box buffers.");
        return false;
    }
    m_bufferCapacityBoxes = boxCount;
    ++m_diagnostics.bufferReallocations;
    return true;
}

bool FrustumCullIndirect::Prepare(const std::span<const Math::Aabb> boxes,
                                        const Math::Frustum& frustum,
                                        const DrawIndexedIndirectCommand& meshParams) {
    ++m_diagnostics.cullsRequested;

    if (!IsReady()) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullIndirect was asked to prepare a draw before its kernel was ready.");
        return false;
    }

    // The seed command, written before the dispatch. instanceCount starts at zero no matter what
    // the caller put there - the GPU's atomic is what fills it, and a non-zero seed would silently
    // add phantom instances.
    DrawIndexedIndirectCommand seedCommand = meshParams;
    seedCommand.instanceCount = 0U;
    const std::span<const std::byte> commandUpload{
        reinterpret_cast<const std::byte*>(&seedCommand), sizeof(seedCommand)};

    if (boxes.empty()) {
        // Still write the zero-instance command: a caller that unconditionally records an indirect
        // draw must get a well-formed no-op, not last frame's parameters.
        if (!m_device.UpdateBuffer(m_drawCommandBuffer, commandUpload)) {
            ++m_diagnostics.uploadFailures;
            return false;
        }
        ++m_diagnostics.cullsSkipped;
        ++m_diagnostics.drawsPrepared;
        return true;
    }

    for (const Math::Plane& plane : frustum.planes) {
        if (!Math::IsFinite(plane.normal) || !std::isfinite(plane.distance)) {
            ++m_diagnostics.cullsRejected;
            UVE_WARNING("FrustumCullIndirect refuses a non-finite frustum plane; the CPU test "
                        "is the path that handles those.");
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
            UVE_WARNING("FrustumCullIndirect refuses a non-finite bounding box; the CPU test is "
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

    FrustumCullIndirectParamsGpu params;
    params.boxCount = static_cast<std::int32_t>(boxCount);

    const std::span<const std::byte> boxUpload{
        reinterpret_cast<const std::byte*>(m_boxScratch.data()),
        m_boxScratch.size() * sizeof(CullBoxGpu)};
    const std::span<const std::byte> planeUpload{
        reinterpret_cast<const std::byte*>(packedPlanes.data()),
        packedPlanes.size() * sizeof(CullPlaneGpu)};
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBuffer(m_boxBuffer, boxUpload) ||
        !m_device.UpdateBuffer(m_planeBuffer, planeUpload) ||
        !m_device.UpdateBuffer(m_paramBuffer, paramUpload) ||
        !m_device.UpdateBuffer(m_drawCommandBuffer, commandUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullIndirect could not upload its cull inputs.");
        return false;
    }

    ComputeDispatchDesc dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX =
        static_cast<std::uint32_t>((boxCount + kWorkgroupSize - 1U) / kWorkgroupSize);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_boxBuffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_planeBuffer, 1U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_drawCommandBuffer, 2U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_visibleIndexBuffer, 3U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_paramBuffer, 4U});
    if (!m_computeSystem.EnqueueDispatch(dispatch)) {
        ++m_diagnostics.cullsRejected;
        return false;
    }

    std::unique_ptr<ICommandBuffer> commands = m_device.CreateCommandBuffer();
    if (commands == nullptr) {
        m_computeSystem.ClearQueue();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("FrustumCullIndirect could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatches(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.cullsRejected;
        UVE_WARNING("FrustumCullIndirect recorded no dispatch; the draw command still holds a "
                    "zero instance count.");
        return false;
    }
    m_device.Submit(std::move(commands));
    FlushComputeSubmission(m_device);

    // Note what does NOT happen here: no readback. The result stays on the GPU, which is the whole
    // reason this class exists next to FrustumCullCompute.
    m_diagnostics.boxesTested += static_cast<std::uint64_t>(boxCount);
    ++m_diagnostics.drawsPrepared;
    return true;
}

BufferHandle FrustumCullIndirect::GetDrawCommandBuffer() const noexcept {
    return m_drawCommandBuffer;
}

BufferHandle FrustumCullIndirect::GetVisibleIndexBuffer() const noexcept {
    return m_visibleIndexBuffer;
}

const FrustumCullIndirectDiagnostics& FrustumCullIndirect::GetDiagnostics() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
