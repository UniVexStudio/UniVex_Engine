// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/render_systems/i_compute_system_uve.h"
#include "uve/rhi/i_render_device_uve.h"

namespace UVE::Render {

/// One vertex as the skinning kernel sees it, matching its std430 SkinVertex exactly. Ten scalars,
/// no vec3s - a vec3 in std430 is 16-byte aligned and would introduce padding the host struct does
/// not have.
///
/// Note this is NOT MeshVertexUVE: the UV pair is absent, because skinning does not touch it and
/// shipping it to the GPU and back would be pure bandwidth. The host copies UVs across from the
/// source vertices when it assembles the result.
struct MeshSkinVertexGpuUVE final {
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
/// Asset::MeshSkinningInfluenceUVE field for field.
struct MeshSkinInfluenceGpuUVE final {
    std::uint32_t joints[4]{0U, 0U, 0U, 0U};
    float weights[4]{0.0F, 0.0F, 0.0F, 0.0F};
};

struct MeshSkinParamsGpuUVE final {
    std::int32_t vertexCount = 0;
};

static_assert(sizeof(MeshSkinVertexGpuUVE) == 10U * sizeof(float),
              "MeshSkinVertexGpuUVE must match the kernel's std430 SkinVertex exactly");
static_assert(sizeof(MeshSkinInfluenceGpuUVE) == 4U * sizeof(std::uint32_t) + 4U * sizeof(float),
              "MeshSkinInfluenceGpuUVE must match the kernel's std430 SkinInfluence exactly");
static_assert(alignof(MeshSkinVertexGpuUVE) == alignof(float),
              "the GPU skin vertex must not acquire alignment padding the shader does not have");

/// A lifetime-to-date account of observable MeshSkinComputeUVE work.
struct MeshSkinComputeDiagnosticsUVE final {
    std::uint32_t skinsRequested = 0U;
    std::uint32_t skinsRejected = 0U;
    std::uint64_t verticesSkinned = 0U;
    std::uint32_t uploadFailures = 0U;
    std::uint32_t readbackFailures = 0U;
    std::uint32_t bufferReallocations = 0U;
};

/// MeshSkinComputeUVE runs Asset::TrySkinMeshUVE on the GPU - the third workload named by ROADMAP
/// section 2's compute item, and the one that was blocked until CS9 built both the data model and
/// the CPU authority it is verified against.
///
/// SkinUVE() produces EXACTLY the vertices TrySkinMeshUVE would have produced, bit for bit, and
/// the tests assert that with no tolerance. That is only possible because CS9's CPU path
/// accumulates in float rather than through Math::TransformPointUVE's double - GLSL has no
/// portable float64, and a double CPU path would have left a permanent ~1 ULP disagreement on
/// roughly one vertex in six. See TransformPointFloatUVE in mesh_skinning_uve.cpp.
///
/// Scope, chosen deliberately: POSE RESOLUTION STAYS ON THE CPU. Walking a parent chain over a
/// handful of joints is serial work a dispatch cannot accelerate, and keeping
/// Asset::TryResolvePoseUVE as the single authority means there is one implementation of it to be
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
class MeshSkinComputeUVE final {
public:
    /// Matches the kernel's `local_size_x`. Dispatches round up; the kernel discards the tail.
    static constexpr std::uint32_t kWorkgroupSizeUVE = 64U;

    MeshSkinComputeUVE(IRenderDeviceUVE& renderDevice, IComputeSystemUVE& computeSystem) noexcept;
    ~MeshSkinComputeUVE();

    MeshSkinComputeUVE(const MeshSkinComputeUVE&) = delete;
    MeshSkinComputeUVE& operator=(const MeshSkinComputeUVE&) = delete;

    /// Compiles the skinning kernel. Returns false (logging why, filling `outInfoLog` when
    /// non-null) on a backend without compute support or if the kernel fails to build; IsReadyUVE()
    /// then stays false and callers should use Asset::TrySkinMeshUVE. Idempotent once ready.
    [[nodiscard]] bool InitializeUVE(std::string* outInfoLog = nullptr);

    [[nodiscard]] bool IsReadyUVE() const noexcept;

    /// Skins `mesh` with `skinningMatrices` (from Asset::TryResolvePoseUVE) into `outVertices`,
    /// which is resized to match `mesh.vertices`.
    ///
    /// Returns false, leaving `outVertices` untouched, when the system is not ready, the mesh is
    /// not skinned, its skinning data is structurally invalid, the matrix count does not match the
    /// joint count, any input is non-finite, or a GPU step fails - the same refusal set as the CPU
    /// path, so a caller can switch between them without changing its error handling.
    ///
    /// One consequence worth stating: this call PRESENTS the device to make its own dispatch
    /// execute, for the reason documented on the other compute workloads (Vulkan replays submitted
    /// recordings in PresentUVE()). That makes it a step to run outside the render frame's own
    /// present, not in the middle of one.
    [[nodiscard]] bool SkinUVE(const Asset::MeshAssetUVE& mesh,
                               std::span<const Math::Matrix4x4UVE> skinningMatrices,
                               std::vector<Asset::MeshVertexUVE>& outVertices);

    [[nodiscard]] const MeshSkinComputeDiagnosticsUVE& GetDiagnosticsUVE() const noexcept;

private:
    [[nodiscard]] bool EnsureBufferCapacityUVE(std::size_t vertexCount, std::size_t jointCount);

    IRenderDeviceUVE& m_device;
    IComputeSystemUVE& m_computeSystem;
    PipelineHandleUVE m_program = kInvalidPipelineHandleUVE;
    BufferHandleUVE m_inputVertexBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_influenceBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_matrixBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_outputVertexBuffer = kInvalidBufferHandleUVE;
    BufferHandleUVE m_paramBuffer = kInvalidBufferHandleUVE;
    std::size_t m_bufferCapacityVertices = 0U;
    std::size_t m_bufferCapacityJoints = 0U;
    std::vector<MeshSkinVertexGpuUVE> m_vertexScratch;
    std::vector<MeshSkinInfluenceGpuUVE> m_influenceScratch;
    std::vector<float> m_matrixScratch;
    MeshSkinComputeDiagnosticsUVE m_diagnostics;
};

} // namespace UVE::Render
