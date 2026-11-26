// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_skin_compute.h"

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

constexpr std::size_t kMatrixFloats = 16U;

/// See the identical helper in the other compute workloads: the RHI takes each backend's own
/// shader language, and runtime GLSL->SPIR-V translation remains an open ROADMAP item.
[[nodiscard]] std::string SelectKernelSource(const IRenderDevice& device,
                                                const std::string_view glslSource,
                                                const char* const spirvBytes,
                                                const std::size_t spirvSize) {
    if (device.GetBackendName().starts_with("Vulkan")) {
        return std::string(spirvBytes, spirvSize);
    }
    return std::string(glslSource);
}

/// GL executes at record time; Vulkan replays in Present(). Reading a result without this
/// returns pre-dispatch contents on Vulkan - a red CI run proved it during CS6.
void FlushComputeSubmission(IRenderDevice& device) {
    device.Present();
}

} // namespace

MeshSkinCompute::MeshSkinCompute(IRenderDevice& renderDevice,
                                       IComputeSystem& computeSystem) noexcept
    : m_device(renderDevice), m_computeSystem(computeSystem) {}

MeshSkinCompute::~MeshSkinCompute() {
    for (BufferHandle* const buffer : {&m_inputVertexBuffer, &m_influenceBuffer, &m_matrixBuffer,
                                          &m_outputVertexBuffer, &m_paramBuffer}) {
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

bool MeshSkinCompute::Initialize(std::string* const outInfoLog) {
    if (m_program != kInvalidPipelineHandle) {
        return true;
    }

    ComputeProgramDesc desc;
    desc.sourceCode = SelectKernelSource(m_device, Shader::BuiltIn::kMeshSkinSource,
                                            BuiltInSpirv::kMeshSkinSpirvBytes,
                                            BuiltInSpirv::kMeshSkinSpirvSize);
    desc.debugName = "MeshSkin";
    m_program = m_computeSystem.CreateProgram(desc, outInfoLog);
    if (m_program == kInvalidPipelineHandle) {
        UVE_WARNING("MeshSkinCompute could not create its skinning kernel; callers must keep "
                    "using Asset::TrySkinMesh.");
        return false;
    }

    BufferDesc paramDesc;
    paramDesc.usage = BufferUsage::Storage;
    paramDesc.sizeBytes = sizeof(MeshSkinParamsGpu);
    paramDesc.updateFrequency = BufferUpdateFrequency::Dynamic; // rewritten every pass
    m_paramBuffer = m_device.CreateBuffer(paramDesc);
    if (m_paramBuffer == kInvalidBufferHandle) {
        UVE_WARNING("MeshSkinCompute could not allocate its parameter buffer.");
        m_computeSystem.DestroyProgram(m_program);
        m_program = kInvalidPipelineHandle;
        return false;
    }
    return true;
}

bool MeshSkinCompute::IsReady() const noexcept {
    return m_program != kInvalidPipelineHandle;
}

bool MeshSkinCompute::EnsureBufferCapacity(const std::size_t vertexCount,
                                                 const std::size_t jointCount) {
    // Vertex-sized and joint-sized buffers grow independently: a caller animating one mesh with a
    // fixed skeleton would otherwise reallocate its joint buffer every time the vertex count moved.
    const bool vertexCapacityOk =
        m_inputVertexBuffer != kInvalidBufferHandle && m_bufferCapacityVertices >= vertexCount;
    const bool jointCapacityOk =
        m_matrixBuffer != kInvalidBufferHandle && m_bufferCapacityJoints >= jointCount;
    if (vertexCapacityOk && jointCapacityOk) {
        return true;
    }

    bool reallocated = false;
    if (!vertexCapacityOk) {
        for (BufferHandle* const buffer :
             {&m_inputVertexBuffer, &m_influenceBuffer, &m_outputVertexBuffer}) {
            if (*buffer != kInvalidBufferHandle) {
                m_device.DestroyBuffer(*buffer);
                *buffer = kInvalidBufferHandle;
            }
        }
        m_bufferCapacityVertices = 0U;

        BufferDesc vertexDesc;
        vertexDesc.usage = BufferUsage::Storage;
        vertexDesc.sizeBytes = vertexCount * sizeof(MeshSkinVertexGpu);
        vertexDesc.updateFrequency = BufferUpdateFrequency::Dynamic; // CPU uploads, every pass
        m_inputVertexBuffer = m_device.CreateBuffer(vertexDesc);
        BufferDesc outputDesc = vertexDesc;
        outputDesc.updateFrequency = BufferUpdateFrequency::Static; // written by the kernel
        m_outputVertexBuffer = m_device.CreateBuffer(outputDesc);

        BufferDesc influenceDesc;
        influenceDesc.usage = BufferUsage::Storage;
        influenceDesc.sizeBytes = vertexCount * sizeof(MeshSkinInfluenceGpu);
        influenceDesc.updateFrequency = BufferUpdateFrequency::Dynamic; // CPU uploads, every pass
        m_influenceBuffer = m_device.CreateBuffer(influenceDesc);

        if (m_inputVertexBuffer == kInvalidBufferHandle ||
            m_outputVertexBuffer == kInvalidBufferHandle ||
            m_influenceBuffer == kInvalidBufferHandle) {
            UVE_WARNING("MeshSkinCompute failed to allocate its per-vertex buffers.");
            return false;
        }
        m_bufferCapacityVertices = vertexCount;
        reallocated = true;
    }

    if (!jointCapacityOk) {
        if (m_matrixBuffer != kInvalidBufferHandle) {
            m_device.DestroyBuffer(m_matrixBuffer);
            m_matrixBuffer = kInvalidBufferHandle;
        }
        m_bufferCapacityJoints = 0U;

        BufferDesc matrixDesc;
        matrixDesc.usage = BufferUsage::Storage;
        matrixDesc.sizeBytes = jointCount * kMatrixFloats * sizeof(float);
        matrixDesc.updateFrequency = BufferUpdateFrequency::Dynamic; // CPU uploads, every pass
        m_matrixBuffer = m_device.CreateBuffer(matrixDesc);
        if (m_matrixBuffer == kInvalidBufferHandle) {
            UVE_WARNING("MeshSkinCompute failed to allocate its skinning-matrix buffer.");
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

bool MeshSkinCompute::Skin(const Asset::MeshAsset& mesh,
                                 const std::span<const Math::Matrix4x4> skinningMatrices,
                                 std::vector<Asset::MeshVertex>& outVertices) {
    ++m_diagnostics.skinsRequested;

    if (!IsReady()) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinCompute was asked to skin before its kernel was ready.");
        return false;
    }
    // The same refusal set as the CPU path, deliberately, so a caller can switch between them
    // without changing its error handling.
    if (!mesh.IsSkinned()) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinCompute was given a static mesh.");
        return false;
    }
    if (!Asset::IsMeshSkinningDataValid(mesh)) {
        ++m_diagnostics.skinsRejected;
        return false; // Already logged the specific defect.
    }
    if (skinningMatrices.size() != mesh.joints.size()) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinCompute: {} skinning matrices for {} joints",
                    skinningMatrices.size(), mesh.joints.size());
        return false;
    }

    const std::size_t vertexCount = mesh.vertices.size();
    const std::size_t jointCount = mesh.joints.size();

    m_vertexScratch.assign(vertexCount, MeshSkinVertexGpu{});
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        const Asset::MeshVertex& source = mesh.vertices[index];
        if (!Math::IsFinite(source.position) || !Math::IsFinite(source.normal) ||
            !Math::IsFinite(source.tangent) || !std::isfinite(source.tangentHandedness)) {
            ++m_diagnostics.skinsRejected;
            UVE_WARNING("MeshSkinCompute refuses a non-finite source vertex at index {}", index);
            return false;
        }
        MeshSkinVertexGpu& packed = m_vertexScratch[index];
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

    m_influenceScratch.assign(vertexCount, MeshSkinInfluenceGpu{});
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        const Asset::MeshSkinningInfluence& source = mesh.skinningInfluences[index];
        for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluences; ++slot) {
            m_influenceScratch[index].joints[slot] = source.joints[slot];
            m_influenceScratch[index].weights[slot] = source.weights[slot];
        }
    }

    // TRANSPOSED on upload. Matrix4x4 stores row-major (m[row][col]); std430 mat4 is
    // column-major with a 16-byte column stride. Transposing here rather than indexing
    // differently in the shader keeps the kernel's element access readable AND keeps the upload a
    // single dense block; the kernel's comment records the other half of this arrangement.
    m_matrixScratch.assign(jointCount * kMatrixFloats, 0.0F);
    for (std::size_t jointIndex = 0U; jointIndex < jointCount; ++jointIndex) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t column = 0U; column < 4U; ++column) {
                const float value = skinningMatrices[jointIndex].m[row][column];
                if (!std::isfinite(value)) {
                    ++m_diagnostics.skinsRejected;
                    UVE_WARNING("MeshSkinCompute refuses a non-finite skinning matrix at joint {}",
                                jointIndex);
                    return false;
                }
                m_matrixScratch[jointIndex * kMatrixFloats + column * 4U + row] = value;
            }
        }
    }

    if (!EnsureBufferCapacity(vertexCount, jointCount)) {
        ++m_diagnostics.uploadFailures;
        return false;
    }

    MeshSkinParamsGpu params;
    params.vertexCount = static_cast<std::int32_t>(vertexCount);

    const std::span<const std::byte> vertexUpload{
        reinterpret_cast<const std::byte*>(m_vertexScratch.data()),
        m_vertexScratch.size() * sizeof(MeshSkinVertexGpu)};
    const std::span<const std::byte> influenceUpload{
        reinterpret_cast<const std::byte*>(m_influenceScratch.data()),
        m_influenceScratch.size() * sizeof(MeshSkinInfluenceGpu)};
    const std::span<const std::byte> matrixUpload{
        reinterpret_cast<const std::byte*>(m_matrixScratch.data()),
        m_matrixScratch.size() * sizeof(float)};
    const std::span<const std::byte> paramUpload{reinterpret_cast<const std::byte*>(&params),
                                                 sizeof(params)};

    if (!m_device.UpdateBuffer(m_inputVertexBuffer, vertexUpload) ||
        !m_device.UpdateBuffer(m_influenceBuffer, influenceUpload) ||
        !m_device.UpdateBuffer(m_matrixBuffer, matrixUpload) ||
        !m_device.UpdateBuffer(m_paramBuffer, paramUpload)) {
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("MeshSkinCompute could not upload its skinning inputs.");
        return false;
    }

    ComputeDispatchDesc dispatch;
    dispatch.program = m_program;
    dispatch.groupCountX =
        static_cast<std::uint32_t>((vertexCount + kWorkgroupSize - 1U) / kWorkgroupSize);
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_inputVertexBuffer, 0U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_influenceBuffer, 1U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_matrixBuffer, 2U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_outputVertexBuffer, 3U});
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{m_paramBuffer, 4U});
    if (!m_computeSystem.EnqueueDispatch(dispatch)) {
        ++m_diagnostics.skinsRejected;
        return false;
    }

    std::unique_ptr<ICommandBuffer> commands = m_device.CreateCommandBuffer();
    if (commands == nullptr) {
        m_computeSystem.ClearQueue();
        ++m_diagnostics.uploadFailures;
        UVE_WARNING("MeshSkinCompute could not create a command buffer for its dispatch.");
        return false;
    }
    const std::size_t recorded = m_computeSystem.ExecuteQueuedDispatches(*commands);
    if (recorded == 0U) {
        ++m_diagnostics.skinsRejected;
        UVE_WARNING("MeshSkinCompute recorded no dispatch; no vertices were skinned.");
        return false;
    }
    m_device.Submit(std::move(commands));
    FlushComputeSubmission(m_device);

    std::vector<MeshSkinVertexGpu> readback(vertexCount, MeshSkinVertexGpu{});
    const std::span<std::byte> readbackSpan{reinterpret_cast<std::byte*>(readback.data()),
                                            readback.size() * sizeof(MeshSkinVertexGpu)};
    if (!m_device.ReadbackBuffer(m_outputVertexBuffer, readbackSpan)) {
        ++m_diagnostics.readbackFailures;
        UVE_WARNING("MeshSkinCompute could not read its skinned vertices back.");
        return false;
    }

    // Assembled into a local and moved out at the end: a failure above must leave the caller's
    // vector untouched, matching the CPU path's failure-atomic contract.
    std::vector<Asset::MeshVertex> skinned;
    skinned.reserve(vertexCount);
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        // UVs come from the source, not the GPU: skinning does not touch them, so shipping them
        // across the bus in both directions would be pure bandwidth.
        Asset::MeshVertex vertex = mesh.vertices[index];
        const MeshSkinVertexGpu& result = readback[index];
        vertex.position = Math::Vector3{result.positionX, result.positionY, result.positionZ};
        vertex.normal = Math::Vector3{result.normalX, result.normalY, result.normalZ};
        vertex.tangent = Math::Vector3{result.tangentX, result.tangentY, result.tangentZ};
        vertex.tangentHandedness = result.handedness;
        skinned.push_back(vertex);
    }

    outVertices = std::move(skinned);
    m_diagnostics.verticesSkinned += static_cast<std::uint64_t>(vertexCount);
    return true;
}

const MeshSkinComputeDiagnostics& MeshSkinCompute::GetDiagnostics() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
