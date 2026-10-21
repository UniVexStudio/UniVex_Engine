// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

/// One vertex of a MeshAssetUVE: position, normal, one UV set, and a tangent-space basis
/// direction. `tangent`/`tangentHandedness` are runtime-derived rather than serialized in the
/// current `.uvmodel` payload, preserving compatibility with existing meshes while providing the
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
/// and the existing `.uvmodel` payload writes MeshVertexUVE field by field - growing that struct
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
    /// The bone's name, which is how a skeleton's pose and a clip's tracks find this joint. Empty
    /// for joints saved before names were kept.
    std::string name;
};

/// One vertex's displacement in one morph target: which vertex moves, and by how much in each
/// direction channel. `vertexIndex` is an index into MeshAssetUVE::vertices.
///
/// Deltas are sparse rather than one entry per vertex. A facial blend shape moves a few hundred
/// vertices on a head that may have tens of thousands, so a dense array would spend the entire
/// memory budget of the asset on zeroes: a 50k-vertex head with 52 ARKit-style shapes stores
/// roughly 100k deltas sparsely instead of 2.6M dense entries. Exporters routinely emit explicit
/// zero displacements for the vertices a shape does not move, and the importer drops exactly-zero
/// deltas for the same reason - the omission is lossless because a missing delta IS a zero delta.
///
/// `normalDelta` and `tangentDelta` are zero for shapes that only move positions, which is how
/// most authoring tools export them. Applying a zero normal delta is a no-op, so the two cases
/// need no separate representation.
struct MeshMorphTargetDeltaUVE {
    std::uint32_t vertexIndex = 0U;
    Math::Vector3UVE positionDelta;
    Math::Vector3UVE normalDelta;
    Math::Vector3UVE tangentDelta;

    [[nodiscard]] bool operator==(const MeshMorphTargetDeltaUVE&) const noexcept = default;
};

/// One blend shape of a mesh: a name and the sparse vertex displacements that name drives.
///
/// Deltas are stored in ascending `vertexIndex` order with no duplicates. That is not cosmetic:
/// it makes accumulation order independent of authoring order (two assets authored in different
/// orders then produce bit-identical results), it lets an eval pass walk the delta array linearly
/// without touching every vertex, and it makes a duplicate entry a detectable asset defect rather
/// than a silent double displacement. `IsMeshMorphTargetDataValidUVE` enforces it.
///
/// `name` is how animation weight tracks and the Inspector address the shape ("jawOpen",
/// "mouthSmile_L"). glTF supplies names through `mesh.extras.targetNames`; a mesh that lacks them
/// gets deterministic `target0`, `target1`, ... names at import rather than empty strings, because
/// an unnamed shape cannot be animated or authored by name later.
struct MeshMorphTargetUVE {
    std::string name;
    std::vector<MeshMorphTargetDeltaUVE> deltas;

    [[nodiscard]] bool operator==(const MeshMorphTargetUVE&) const noexcept = default;
};

/// The CPU-side, engine-native representation of a `.uvmodel` asset (Part 2's file-format
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

    /// Foundation WP7: the materials this mesh was authored with, one GUID per source material
    /// slot in source order (FBX material order). Empty when the source had no materials or the
    /// mesh predates this field; an entry is `kInvalidAssetGuidUVE` only when the importer could
    /// not publish that slot. Serialised as a tagged trailing section so older files load
    /// unchanged and a mesh without materials still writes byte-identical files.
    std::vector<AssetGuidUVE> materialGuids;

    /// The blend shapes this mesh carries, in the order the source declared them (glTF morph
    /// target order, which is the order a `weights` array indexes into). Empty for a mesh with no
    /// morph targets - the overwhelmingly common case, and the one that must keep writing the
    /// bytes it always did.
    std::vector<MeshMorphTargetUVE> morphTargets;

    /// The weights a freshly instantiated mesh starts at: one per entry in `morphTargets`, or empty
    /// for "all zero" (the rest shape). glTF stores this as `mesh.weights`, and a glTF node may
    /// override it per instance; the mesh-level value is the one an asset owns, so that is the one
    /// that lives here.
    ///
    /// Empty is not the same as a vector of zeroes for serialization purposes: it is what a mesh
    /// without morph targets carries, and it costs nothing to write.
    std::vector<float> defaultMorphWeights;

    /// True when this mesh carries skinning data. Both arrays are populated together or not at
    /// all; IsMeshSkinningDataValidUVE() is what verifies that, this only asks the question.
    [[nodiscard]] bool IsSkinnedUVE() const noexcept {
        return !skinningInfluences.empty() && !joints.empty();
    }

    /// True when this mesh carries blend shapes.
    [[nodiscard]] bool HasMorphTargetsUVE() const noexcept { return !morphTargets.empty(); }
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

/// How many blend shapes one mesh may carry. 512 is far past any production face rig (ARKit's
/// canonical set is 52, MetaHuman's is around 800 including corrective shapes) while still bounding
/// the per-instance weight array a runtime sampler has to walk every frame.
inline constexpr std::size_t kMaximumMeshMorphTargetsUVE = 512U;

/// How many sparse deltas one blend shape may carry. A shape is a local edit - the largest
/// production shapes (a full-body corrective) touch a few tens of thousands of vertices - so this
/// bound only ever fires on corrupt or hostile input, never on authored content.
inline constexpr std::size_t kMaximumMeshMorphTargetDeltasUVE = 1U << 20U;

/// Full structural validation of `mesh`'s morph-target data, logging the specific defect found.
///
/// A mesh with no morph targets and no default weights is VALID (that is every static mesh). When
/// targets exist, anything else must satisfy: at most kMaximumMeshMorphTargetsUVE shapes, every
/// shape's deltas strictly ascending by vertex index (which also rules out duplicates), every index
/// below vertices.size(), every delta component finite, and - when defaultMorphWeights is non-empty
/// - exactly one finite weight per shape.
[[nodiscard]] bool IsMeshMorphTargetDataValidUVE(const MeshAssetUVE& mesh);

/// Applies `weights` to `targets` on top of `bindVertices` and writes the result to `outVertices`.
///
/// This is the CPU authority for blend-shape evaluation, in the same spirit as TrySkinMeshUVE: the
/// maths is written, tested and deterministic here, and a GPU path is verified against it rather
/// than the other way round.
///
/// The composition order matters and is the glTF one: morph targets displace the mesh in its own
/// local space, and skinning then transforms that displaced mesh. Callers therefore accumulate
/// morphs into the BIND pose and feed the result to TrySkinMeshUVE - doing it the other way round
/// would displace an already-skinned mesh by deltas authored relative to joints that no longer
/// exist in that space.
///
/// Semantics: `outVertices[i] = bindVertices[i] + sum over targets of weight[t] * delta(t, i)`.
/// Accumulation is in target order, and each target's deltas are walked in vertex order, so the
/// result is bit-identical for any two assets that describe the same shapes - a property floating
/// point addition otherwise takes away. A target with weight 0 is skipped, which is also what makes
/// a 52-shape rig cost nothing on the shapes a frame does not use.
///
/// Returns false, leaving `outVertices` untouched, when a size disagrees (weights vs targets),
/// when any weight is non-finite, when the data is structurally invalid, or when any accumulated
/// component is non-finite. `outVertices` is sized to match `bindVertices`.
[[nodiscard]] bool AccumulateMorphTargetsUVE(std::span<const MeshVertexUVE> bindVertices,
                                            std::span<const MeshMorphTargetUVE> targets,
                                            std::span<const float> weights,
                                            std::vector<MeshVertexUVE>& outVertices);

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
