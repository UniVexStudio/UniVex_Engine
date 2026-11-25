// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mesh_asset.h"
#include "matrix4x4.h"
#include "i_compute_system.h"
#include "i_render_device.h"

namespace UVE::Render {

/// One vertex as the skinning kernel sees it, matching its std430 SkinVertex exactly. Ten scalars,
/// no vec3s - a vec3 in std430 is 16-byte aligned and would introduce padding the host struct does
/// not have.
///
/// Note this is NOT MeshVertex: the UV pair is absent, because skinning does not touch it and
/// shipping it to the GPU and back would be pure bandwidth. The host copies UVs across from the
/// source vertices when it assembles the result.
struct MeshSkinVertexGpu final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float normalX = 0.0F;
    float normalY = 0.0F;
    float normalZ = 0.0F;
    float tangentX = 0.0F;
    float tangentY = 0.0F;
    float tangentZ = 0.0F;
    float handedness = 0.0F;
};

/// Four joint indices and four weights, matching the kernel's std430 SkinInfluence and
/// Asset::MeshSkinningInfluence field for field.
struct MeshSkinInfluenceGpu final {
    std::uint32_t joints[4]{0U, 0U, 0U, 0U};
    float weights[4]{0.0F, 0.0F, 0.0F, 0.0F};
};

struct MeshSkinParamsGpu final {
    std::int32_t vertexCount = 0;
};

static_assert(sizeof(MeshSkinVertexGpu) == 10U * sizeof(float),
              "MeshSkinVertexGpu must match the kernel's std430 SkinVertex exactly");
static_assert(sizeof(MeshSkinInfluenceGpu) == 4U * sizeof(std::uint32_t) + 4U * sizeof(float),
              "MeshSkinInfluenceGpu must match the kernel's std430 SkinInfluence exactly");
static_assert(alignof(MeshSkinVertexGpu) == alignof(float),
              "the GPU skin vertex must not acquire alignment padding the shader does not have");

/// A lifetime-to-date account of observable MeshSkinCompute work.
struct MeshSkinComputeDiagnostics final {
    std::uint32_t skinsRequested = 0U;
    std::uint32_t skinsRejected = 0U;
    std::uint64_t verticesSkinned = 0U;
    std::uint32_t uploadFailures = 0U;
    std::uint32_t readbackFailures = 0U;
    std::uint32_t bufferReallocations = 0U;
};

/// MeshSkinCompute runs Asset::TrySkinMesh on the GPU - the third workload named by ROADMAP
/// section 2's compute item, and the one that was blocked until CS9 built both the data model and
/// the CPU authority it is verified against.
///
/// Skin() produces EXACTLY the vertices TrySkinMesh would have produced, bit for bit, and
/// the tests assert that with no tolerance. That is only possible because CS9's CPU path
/// accumulates in float rather than through Math::TransformPoint's double - GLSL has no
/// portable float64, and a double CPU path would have left a permanent ~1 ULP disagreement on
/// roughly one vertex in six. See TransformPointFloat in mesh_skinning_uve.cpp.
///
/// Scope, chosen deliberately: POSE RESOLUTION STAYS ON THE CPU. Walking a parent chain over a
/// handful of joints is serial work a dispatch cannot accelerate, and keeping
/// Asset::TryResolvePose as the single authority means there is one implementation of it to be
/// correct. The caller resolves a pose, this skins the vertices.
///
/// Like every other compute workload here this is a synchronous GPU round trip on CS3's documented
/// cold path - honest work and a real win on large meshes, but not a free per-frame path. A
/// production skinned-mesh renderer would keep the skinned vertices on the GPU and draw from them
/// directly rather than reading them back; this class deliberately returns them, because a
/// workload whose output nobody can inspect is a workload nobody can verify. Feeding the skinned
/// buffer straight into a draw is the natural next step, and CS7's indirect draw is the mechanism.
///
/// Thread-safety: not thread-safe. Main engine thread only, like every other render system.
class MeshSkinCompute final {
public:
    /// Matches the kernel's `local_size_x`. Dispatches round up; the kernel discards the tail.
    static constexpr std::uint32_t kWorkgroupSize = 64U;

    MeshSkinCompute(IRenderDevice& renderDevice, IComputeSystem& computeSystem) noexcept;
    ~MeshSkinCompute();

    MeshSkinCompute(const MeshSkinCompute&) = delete;
    MeshSkinCompute& operator=(const MeshSkinCompute&) = delete;

    /// Compiles the skinning kernel. Returns false (logging why, filling `outInfoLog` when
    /// non-null) on a backend without compute support or if the kernel fails to build; IsReady()
    /// then stays false and callers should use Asset::TrySkinMesh. Idempotent once ready.
    [[nodiscard]] bool Initialize(std::string* outInfoLog = nullptr);

    [[nodiscard]] bool IsReady() const noexcept;

    /// Skins `mesh` with `skinningMatrices` (from Asset::TryResolvePose) into `outVertices`,
    /// which is resized to match `mesh.vertices`.
    ///
    /// Returns false, leaving `outVertices` untouched, when the system is not ready, the mesh is
    /// not skinned, its skinning data is structurally invalid, the matrix count does not match the
    /// joint count, any input is non-finite, or a GPU step fails - the same refusal set as the CPU
    /// path, so a caller can switch between them without changing its error handling.
    ///
    /// One consequence worth stating: this call PRESENTS the device to make its own dispatch
    /// execute, for the reason documented on the other compute workloads (Vulkan replays submitted
    /// recordings in Present()). That makes it a step to run outside the render frame's own
    /// present, not in the middle of one.
    [[nodiscard]] bool Skin(const Asset::MeshAsset& mesh,
                               std::span<const Math::Matrix4x4> skinningMatrices,
                               std::vector<Asset::MeshVertex>& outVertices);

    [[nodiscard]] const MeshSkinComputeDiagnostics& GetDiagnostics() const noexcept;

private:
    [[nodiscard]] bool EnsureBufferCapacity(std::size_t vertexCount, std::size_t jointCount);

    IRenderDevice& m_device;
    IComputeSystem& m_computeSystem;
    PipelineHandle m_program = kInvalidPipelineHandle;
    BufferHandle m_inputVertexBuffer = kInvalidBufferHandle;
    BufferHandle m_influenceBuffer = kInvalidBufferHandle;
    BufferHandle m_matrixBuffer = kInvalidBufferHandle;
    BufferHandle m_outputVertexBuffer = kInvalidBufferHandle;
    BufferHandle m_paramBuffer = kInvalidBufferHandle;
    std::size_t m_bufferCapacityVertices = 0U;
    std::size_t m_bufferCapacityJoints = 0U;
    std::vector<MeshSkinVertexGpu> m_vertexScratch;
    std::vector<MeshSkinInfluenceGpu> m_influenceScratch;
    std::vector<float> m_matrixScratch;
    MeshSkinComputeDiagnostics m_diagnostics;
};

} // namespace UVE::Render
