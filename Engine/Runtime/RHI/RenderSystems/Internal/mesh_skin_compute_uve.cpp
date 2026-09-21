// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_skin_compute_uve.h"

#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

#include "uve/logging/logging_macros_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/rhi_shader/built_in_compute_spirv_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"

namespace UVE::Render {
namespace {

constexpr std::size_t kMatrixFloatsUVE = 16U;

/// See the identical helper in the other compute workloads: the RHI takes each backend's own
/// shader language, and runtime GLSL->SPIR-V translation remains an open ROADMAP item.
[[nodiscard]] std::string SelectKernelSourceUVE(const IRenderDeviceUVE& device,
                                                const std::string_view glslSource,
                                                const char* const spirvBytes,
                                                const std::size_t spirvSize) {
    if (device.GetBackendNameUVE().starts_with("Vulkan")) {
        return std::string(spirvBytes, spirvSize);
    }
    return std::string(glslSource);
}

/// GL executes at record time; Vulkan replays in PresentUVE(). Reading a result without this
/// returns pre-dispatch contents on Vulkan - a red CI run proved it during CS6.
void FlushComputeSubmissionUVE(IRenderDeviceUVE& device) {
    device.PresentUVE();
}

} // namespace

MeshSkinComputeUVE::MeshSkinComputeUVE(IRenderDeviceUVE& renderDevice,
                                       IComputeSystemUVE& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

MeshSkinComputeUVE::~MeshSkinComputeUVE() {
    for (BufferHandleUVE* const buffer : {&m_inputVertexBuffer, &m_influenceBuffer, &m_matrixBuffer,
                                          &m_outputVertexBuffer, &m_paramBuffer}) {
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

bool MeshSkinComputeUVE::InitializeUVE(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandleUVE) {
        return true;
    }

    ComputeProgramDescUVE desc;
    desc.sourceCode = SelectKernelSourceUVE(m_device, Shader::BuiltIn::kMeshSkinSource,
                                            BuiltInSpirv::kMeshSkinSpirvBytesUVE,
                                            BuiltInSpirv::kMeshSkinSpirvSizeUVE);
    desc.debugName = "MeshSkin";
    m_program = m_computeSystem.CreateProgramUVE(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandleUVE) {
        UVE_WARNING("MeshSkinComputeUVE could not create its skinning kernel; callers must keep "
                    "using Asset::TrySkinMeshUVE.");
        return false;
    }

    BufferDescUVE paramDesc;
    paramDesc.usage = BufferUsageUVE::Storage;
    paramDesc.sizeBytes = sizeof(MeshSkinParamsGpuUVE);
    m_paramBuffer = m_device.CreateBufferUVE(paramDesc);
    if (m_paramBuffer == kInvalidBufferHandleUVE) {
        UVE_WARNING("MeshSkinComputeUVE could not allocate its parameter buffer.");
        m_computeSystem.DestroyProgramUVE(m_program);
        m_program = kInvalidPipelineHandleUVE;
        return false;
    }
    return true;
}

bool MeshSkinComputeUVE::IsReadyUVE() const noexcept {
    return m_program != kInvalidPipelineHandleUVE;
}

bool MeshSkinComputeUVE::EnsureBufferCapacityUVE(const std::size_t vertexCount,
                                                 const std::size_t jointCount) {
    // Vertex-sized and joint-sized buffers grow independently: a caller animating one mesh with a
    // fixed skeleton would otherwise reallocate its joint buffer every time the vertex count moved.
    const bool vertexCapacityOk =
        m_inputVertexBuffer != kInvalidBufferHandleUVE && m_bufferCapacityVertices >= vertexCount;
    const bool jointCapacityOk =
        m_matrixBuffer != kInvalidBufferHandleUVE && m_bufferCapacityJoints >= jointCount;
    if (vertexCapacityOk && jointCapacityOk) {
        return true;
    }

    bool reallocated = false;
    if (!vertexCapacityOk) {
        for (BufferHandleUVE* const buffer :
             {&m_inputVertexBuffer, &m_influenceBuffer, &m_outputVertexBuffer}) {
            if (*buffer != kInvalidBufferHandleUVE) {
                m_device.DestroyBufferUVE(*buffer);
                *buffer = kInvalidBufferHandleUVE;
            }
        }
        m_bufferCapacityVertices = 0U;

        BufferDescUVE vertexDesc;
        vertexDesc.usage = BufferUsageUVE::Storage;
        vertexDesc.sizeBytes = vertexCount * sizeof(MeshSkinVertexGpuUVE);
        m_inputVertexBuffer = m_device.CreateBufferUVE(vertexDesc);
        m_outputVertexBuffer = m_device.CreateBufferUVE(vertexDesc);

        BufferDescUVE influenceDesc;
        influenceDesc.usage = BufferUsageUVE::Storage;
        influenceDesc.sizeBytes = vertexCount * sizeof(MeshSkinInfluenceGpuUVE);
        m_influenceBuffer = m_device.CreateBufferUVE(influenceDesc);

        if (m_inputVertexBuffer == kInvalidBufferHandleUVE ||
            m_outputVertexBuffer == kInvalidBufferHandleUVE ||
            m_influenceBuffer == kInvalidBufferHandleUVE) {
            UVE_WARNING("MeshSkinComputeUVE failed to allocate its per-vertex buffers.");
            return false;
        }
        m_bufferCapacityVertices = vertexCount;
        reallocated = true;
    }

    if (!jointCapacityOk) {
        if (m_matrixBuffer != kInvalidBufferHandleUVE) {
            m_device.DestroyBufferUVE(m_matrixBuffer);
            m_matrixBuffer = kInvalidBufferHandleUVE;
        }
        m_bufferCapacityJoints = 0U;

        BufferDescUVE matrixDesc;
        matrixDesc.usage = BufferUsageUVE::Storage;
        matrixDesc.sizeBytes = jointCount * kMatrixFloatsUVE * sizeof(float);
        m_matrixBuffer = m_device.CreateBufferUVE(matrixDesc);
        if (m_matrixBuffer == kInvalidBufferHandleUVE) {
            UVE_WARNING("MeshSkinComputeUVE failed to allocate its skinning-matrix buffer.");
            return false;
        }
        m_bufferCapacityJoints = jointCount;
        reallocated = true;
    }

    if (reallocated) {
        ++m_diagnostics.bufferReallocations;
    }
    return true;
}

bool MeshSkinComputeUVE::SkinUVE(const Asset::MeshAssetUVE& mesh,
                                 const std::span<const Math::Matrix4x4UVE> skinningMatrices,
                                 std::vector<Asset::MeshVertexUVE>& outVertices) {
    ++m_diagnostics.skinsRequested;

    if (!IsReadyUVE()) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinComputeUVE was asked to skin before its kernel was ready.");
        return false;
    }
    // The same refusal set as the CPU path, deliberately, so a caller can switch between them
    // without changing its error handling.
    if (!mesh.IsSkinnedUVE()) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinComputeUVE was given a static mesh.");
        return false;
    }
    if (!Asset::IsMeshSkinningDataValidUVE(mesh)) {
        ++m_diagnostics.skinsRejected;
        return false; // Already logged the specific defect.
    }
    if (skinningMatrices.size() != mesh.joints.size()) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinComputeUVE: {} skinning matrices for {} joints",
                    skinningMatrices.size(), mesh.joints.size());
        return false;
    }

    const std::size_t vertexCount = mesh.vertices.size();
    const std::size_t jointCount = mesh.joints.size();

    m_vertexScratch.assign(vertexCount, MeshSkinVertexGpuUVE{});
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        const Asset::MeshVertexUVE& source = mesh.vertices[index];
        if (!Math::IsFiniteUVE(source.position) || !Math::IsFiniteUVE(source.normal) ||
            !Math::IsFiniteUVE(source.tangent) || !std::isfinite(source.tangentHandedness)) {
            ++m_diagnostics.skinsRejected;
            UVE_WARNING("MeshSkinComputeUVE refuses a non-finite source vertex at index {}", index);
            return false;
        }
        MeshSkinVertexGpuUVE& packed = m_vertexScratch[index];
        packed.positionX = source.position.x;
        packed.positionY = source.position.y;
        packed.positionZ = source.position.z;
        packed.normalX = source.normal.x;
        packed.normalY = source.normal.y;
        packed.normalZ = source.normal.z;
        packed.tangentX = source.tangent.x;
        packed.tangentY = source.tangent.y;
        packed.tangentZ = source.tangent.z;
        packed.handedness = source.tangentHandedness;
    }

    m_influenceScratch.assign(vertexCount, MeshSkinInfluenceGpuUVE{});
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        const Asset::MeshSkinningInfluenceUVE& source = mesh.skinningInfluences[index];
        for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluencesUVE; ++slot) {
            m_influenceScratch[index].joints[slot] = source.joints[slot];
            m_influenceScratch[index].weights[slot] = source.weights[slot];
        }
    }

    // TRANSPOSED on upload. Matrix4x4UVE stores row-major (m[row][col]); std430 mat4 is
    // column-major with a 16-byte column stride. Transposing here rather than indexing
    // differently in the shader keeps the kernel's element access readable AND keeps the upload a
    // single dense block; the kernel's comment records the other half of this arrangement.
    m_matrixScratch.assign(jointCount * kMatrixFloatsUVE, 0.0F);
    for (std::size_t jointIndex = 0U; jointIndex < jointCount; ++jointIndex) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t column = 0U; column < 4U; ++column) {
                const float value = skinningMatrices[jointIndex].m[row][column];
                if (!std::isfinite(value)) {
                    ++m_diagnostics.skinsRejected;
                    UVE_WARNING("MeshSkinComputeUVE refuses a non-finite skinning matrix at joint {}",
                                jointIndex);
                    return false;
                }
                m_matrixScratch[jointIndex * kMatrixFloatsUVE + column * 4U + row] = value;
            }
        }
    }

    if (!EnsureBufferCapacityUVE(vertexCount, jointCount)) {
        ++m_diagnostics.uploadFailures;
        return false;
    }

    MeshSkinParamsGpuUVE params;
    params.vertexCount = static_cast<std::int32_t>(vertexCount);

    const std::span<const std::byte> vertexUpload{
        reinterpret_cast<const std::byte*>(m_vertexScratch.data()),
        m_vertexScratch.size() * sizeof(MeshSkinVertexGpuUVE)};
    const std::span<const std::byte> influenceUpload{
        reinterpret_cast<const std::byte*>(m_influenceScratch.data()),
        m_influenceScratch.size() * sizeof(MeshSkinInfluenceGpuUVE)};
    const std::span<const std::byte> matrixUpload{
        reinterpret_cast<const std::byte*>(m_matrixScratch.data()),
        m_matrixScratch.size() * sizeof(float)};
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBufferUVE(m_inputVertexBuffer, vertexUpload) ||
        !m_device.UpdateBufferUVE(m_influenceBuffer, influenceUpload) ||
        !m_device.UpdateBufferUVE(m_matrixBuffer, matrixUpload) ||
        !m_device.UpdateBufferUVE(m_paramBuffer, paramUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("MeshSkinComputeUVE could not upload its skinning inputs.");
        return false;
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX =
        static_cast<std::uint32_t>((vertexCount + kWorkgroupSizeUVE - 1U) / kWorkgroupSizeUVE);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_inputVertexBuffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_influenceBuffer, 1U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_matrixBuffer, 2U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_outputVertexBuffer, 3U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{m_paramBuffer, 4U});
    if (!m_computeSystem.EnqueueDispatchUVE(dispatch)) {
        ++m_diagnostics.skinsRejected;
        return false;
    }

    std::unique_ptr<ICommandBufferUVE> commands = m_device.CreateCommandBufferUVE();
    if (commands == nullptr) {
        m_computeSystem.ClearQueueUVE();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("MeshSkinComputeUVE could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatchesUVE(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinComputeUVE recorded no dispatch; no vertices were skinned.");
        return false;
    }
    m_device.SubmitUVE(std::move(commands));
    FlushComputeSubmissionUVE(m_device);

    std::vector<MeshSkinVertexGpuUVE> readback(vertexCount, MeshSkinVertexGpuUVE{});
    const std::span<std::byte> readbackSpan{reinterpret_cast<std::byte*>(readback.data()),
                                            readback.size() * sizeof(MeshSkinVertexGpuUVE)};
    if (!m_device.ReadbackBufferUVE(m_outputVertexBuffer, readbackSpan)) {
        ++m_diagnostics.readbackFailures;
        UVE_WARNING("MeshSkinComputeUVE could not read its skinned vertices back.");
        return false;
    }

    // Assembled into a local and moved out at the end: a failure above must leave the caller's
    // vector untouched, matching the CPU path's failure-atomic contract.
    std::vector<Asset::MeshVertexUVE> skinned;
    skinned.reserve(vertexCount);
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        // UVs come from the source, not the GPU: skinning does not touch them, so shipping them
        // across the bus in both directions would be pure bandwidth.
        Asset::MeshVertexUVE vertex = mesh.vertices[index];
        const MeshSkinVertexGpuUVE& result = readback[index];
        vertex.position = Math::Vector3UVE{result.positionX, result.positionY, result.positionZ};
        vertex.normal = Math::Vector3UVE{result.normalX, result.normalY, result.normalZ};
        vertex.tangent = Math::Vector3UVE{result.tangentX, result.tangentY, result.tangentZ};
        vertex.tangentHandedness = result.handedness;
        skinned.push_back(vertex);
    }

    outVertices = std::move(skinned);
    m_diagnostics.verticesSkinned += static_cast<std::uint64_t>(vertexCount);
    return true;
}

const MeshSkinComputeDiagnosticsUVE& MeshSkinComputeUVE::GetDiagnosticsUVE() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
