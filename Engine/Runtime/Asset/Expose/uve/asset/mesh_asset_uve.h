// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "uve/math/aabb_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

/// One vertex of a MeshAssetUVE: position, normal, one UV set, and a tangent-space basis
/// direction. `tangent`/`tangentHandedness` are runtime-derived rather than serialized in the
/// current `.uvemodel` payload, preserving compatibility with existing meshes while providing the
/// canonical material shader the TBN data normal mapping needs. Skinned meshes/LODs remain
/// future-increment work.
struct MeshVertexUVE {
    Math::Vector3UVE position;
    Math::Vector3UVE normal;
    float u = 0.0F;
    float v = 0.0F;
    Math::Vector3UVE tangent{1.0F, 0.0F, 0.0F};
    float tangentHandedness = 1.0F;
};

/// How many joints may influence a single vertex. Four is the near-universal choice - it is what
/// glTF stores, what fits one vec4 pair in a vertex shader, and what every GPU skinning path in
/// common use assumes. A mesh needing more influences must have them pruned and renormalized at
/// import time rather than silently truncated here.
inline constexpr std::size_t kMaxJointInfluencesUVE = 4U;

/// The skinning influences of ONE vertex: up to four joints and their weights.
///
/// Parallel to MeshAssetUVE::vertices rather than a member of MeshVertexUVE, deliberately. A
/// skinned mesh is the exception, not the rule: folding 32 bytes of joint data into every vertex
/// of every static mesh in the engine would cost memory on the common case to serve the rare one,
/// and the existing `.uvemodel` payload writes MeshVertexUVE field by field - growing that struct
/// would change the serialized vertex stride for files that contain no skinning at all.
///
/// Weights are stored as they were authored, NOT silently renormalized. A weight set that does
/// not sum to one is an asset defect, and IsSkinningInfluenceNormalizedUVE() is how a caller
/// detects it; quietly fixing it here would hide the defect from the importer that could actually
/// correct it at the source.
///
/// A zero weight makes its joint index irrelevant, but the index must still be in range - an
/// out-of-range index with a zero weight is still a malformed asset, and validation says so
/// rather than relying on the multiplication happening to cancel it out.
struct MeshSkinningInfluenceUVE {
    std::uint32_t joints[kMaxJointInfluencesUVE]{0U, 0U, 0U, 0U};
    float weights[kMaxJointInfluencesUVE]{0.0F, 0.0F, 0.0F, 0.0F};
};

/// Sentinel parent index marking a root joint. Chosen as the maximum uint32 rather than -1-as-
/// unsigned so the value reads the same in the serialized payload as it does in memory.
inline constexpr std::uint32_t kInvalidJointParentUVE = 0xFFFFFFFFU;

/// One joint of a skeleton: its parent, and the matrix taking a vertex from model space into this
/// joint's bind-pose space.
///
/// `parentIndex` is kInvalidJointParentUVE for a root. Parents are required to appear BEFORE their
/// children in MeshAssetUVE::joints, which is what lets a pose be resolved in a single forward
/// pass with no recursion and no visited set; IsSkeletonTopologicallyOrderedUVE() enforces it.
///
/// `inverseBindMatrix` is stored rather than derived because deriving it means inverting the bind
/// pose, and an inverse computed at load time from float data is not bit-identical to the one the
/// exporter computed - which would put a silent discrepancy between the CPU and GPU skinning paths
/// this data exists to keep in agreement.
struct MeshJointUVE {
    std::uint32_t parentIndex = kInvalidJointParentUVE;
    Math::Matrix4x4UVE inverseBindMatrix;
};

/// The CPU-side, engine-native representation of a `.uvemodel` asset (Part 2's file-format
/// table): triangle vertex/index data plus a precomputed local-space bounding box (used for
/// frustum culling once `MeshRendererUVE` exists, Increment 13). Purely CPU-side data — turning
/// this into GPU buffers is a future increment's concern (Renderer3DUVE's resource cache); this
/// type has no dependency on `engine/render` at all.
struct MeshAssetUVE {
    std::vector<MeshVertexUVE> vertices;
    std::vector<std::uint32_t> indices;
    Math::AabbUVE localBounds;

    /// Empty for a static mesh. When non-empty it must have exactly one entry per vertex - a
    /// partially skinned mesh is not a representable state, because the renderer would have no
    /// answer for what to do with the vertices that lack influences.
    std::vector<MeshSkinningInfluenceUVE> skinningInfluences;

    /// Empty for a static mesh. Parents precede children.
    std::vector<MeshJointUVE> joints;

    /// True when this mesh carries skinning data. Both arrays are populated together or not at
    /// all; IsMeshSkinningDataValidUVE() is what verifies that, this only asks the question.
    [[nodiscard]] bool IsSkinnedUVE() const noexcept {
        return !skinningInfluences.empty() && !joints.empty();
    }
};

/// Whether `influence`'s weights sum to 1 within `tolerance`. Callers decide what to do about a
/// false - an importer should renormalize at the source, a validator should reject. Returns false
/// for non-finite weights, which no tolerance should ever accept.
[[nodiscard]] bool IsSkinningInfluenceNormalizedUVE(const MeshSkinningInfluenceUVE& influence,
                                                     float tolerance = 1.0e-4F) noexcept;

/// Whether every joint's parent appears before it, and every parent index is either in range or
/// the root sentinel. This is the precondition that lets pose resolution run as one forward pass.
[[nodiscard]] bool IsSkeletonTopologicallyOrderedUVE(std::span<const MeshJointUVE> joints) noexcept;

/// Full structural validation of `mesh`'s skinning data, logging the specific defect found.
///
/// A static mesh (both arrays empty) is VALID - most meshes are static, and making callers
/// special-case that would guarantee somebody forgets. Anything else must satisfy: one influence
/// per vertex, every joint index below joints.size(), every weight finite and non-negative,
/// weights summing to one per vertex, a topologically ordered skeleton, and finite inverse-bind
/// matrices.
[[nodiscard]] bool IsMeshSkinningDataValidUVE(const MeshAssetUVE& mesh);

/// Derives a normalized tangent and handedness for every vertex from indexed triangles and UVs.
/// Degenerate triangles/UVs receive a deterministic orthogonal fallback. Returns false before
/// mutating the caller-owned vertices when a finite triangle-derived tangent or bitangent
/// accumulator overflows; callers converting assets can therefore reject the candidate
/// failure-atomically. The function does not serialize data or mutate indices. Thread-safety:
/// operates only on caller-owned spans.
[[nodiscard]] bool TryGenerateMeshTangentsUVE(std::span<MeshVertexUVE> vertices,
                                               std::span<const std::uint32_t> indices);

/// Derives tangents through TryGenerateMeshTangentsUVE and applies deterministic orthogonal
/// fallbacks if malformed runtime geometry cannot produce a finite tangent basis. Runtime callers
/// that can reject an asset should use TryGenerateMeshTangentsUVE instead.
void GenerateMeshTangentsUVE(std::span<MeshVertexUVE> vertices, std::span<const std::uint32_t> indices);

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::Mesh`, filling `outMesh`. Returns false
/// (logging the reason) if the file is missing/malformed, isn't actually a Mesh asset, its
/// serialized vertex/index counts do not fit the remaining payload before allocation, or its
/// index data is structurally invalid (any index `>= outMesh.vertices.size()`, which would cause
/// out-of-bounds reads once a future increment consumes this data). Candidate vectors and tangent
/// fields remain private until every validation step succeeds — matching the signature
/// `IAssetManagerUVE::RegisterLoaderUVE<MeshAssetUVE>()` expects.
[[nodiscard]] bool LoadMeshAssetUVE(const std::filesystem::path& path, MeshAssetUVE& outMesh);

/// Writes `mesh` to `path` as a `.uve*` envelope with `AssetKindUVE::Mesh`. Returns false (logging
/// the reason) if the file can't be written.
[[nodiscard]] bool SaveMeshAssetUVE(const MeshAssetUVE& mesh, const std::filesystem::path& path);

} // namespace UVE::Asset
