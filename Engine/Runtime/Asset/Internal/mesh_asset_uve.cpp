// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mesh_asset_uve.h"

#include <cmath>
#include <cstring>
#include <span>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Asset {

namespace {

/// WP7: tag that opens the optional material-reference section ('UVMT' little-endian). It is
/// unambiguous against the skin section's joint count because a count this large fails the
/// joint-count bound check on any real file.
constexpr std::uint32_t kMeshMaterialSectionTagUVE = 0x544D5655U;
constexpr std::uint32_t kMaximumMeshMaterialSlotsUVE = 4096U;

/// Tag that opens the optional morph-target section ('UVMP' little-endian), written after the
/// material section. It carries the same unambiguity argument: read as a joint count it would be
/// 1.3 billion, which fails the bound check on any file small enough to exist.
constexpr std::uint32_t kMeshMorphSectionTagUVE = 0x504D5655U;

/// Tag that opens the optional surface section ('UVMS' little-endian), written after the morph
/// section. Same unambiguity argument again: as a joint count it is 1.4 billion, which no file
/// that fits in memory can pass.
constexpr std::uint32_t kMeshSurfaceSectionTagUVE = 0x534D5655U;

/// One serialized surface: first index, index count, material slot.
constexpr std::size_t kSerializedMeshSurfaceBytesUVE = sizeof(std::uint32_t) * 3U;

/// Upper bound on a single target or joint NAME's serialized length. Names identify shapes and
/// bones, not paragraphs; a longer one is corruption rather than content.
constexpr std::uint32_t kMaximumMeshSectionNameBytesUVE = 256U;

/// One sparse morph delta on disk: a vertex index plus three vec3s.
constexpr std::size_t kSerializedMorphDeltaBytesUVE = sizeof(std::uint32_t) + sizeof(float) * 9U;

[[nodiscard]] bool PeeksMaterialSectionUVE(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    return Utilities::ReadUint32LeFromBufferUVE(payload, peekOffset, tag) && tag == kMeshMaterialSectionTagUVE;
}

[[nodiscard]] bool PeeksMorphSectionUVE(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    return Utilities::ReadUint32LeFromBufferUVE(payload, peekOffset, tag) && tag == kMeshMorphSectionTagUVE;
}

/// Whether a known tagged section starts here. Every "does anything trail this?" test must use
/// this rather than one specific tag, or a file whose next section is a different one gets read as
/// if it were not there at all.
[[nodiscard]] bool PeeksSurfaceSectionUVE(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    return Utilities::ReadUint32LeFromBufferUVE(payload, peekOffset, tag) && tag == kMeshSurfaceSectionTagUVE;
}

[[nodiscard]] bool PeeksTrailingSectionUVE(const std::vector<std::byte>& payload, const std::size_t offset) {
    return PeeksMaterialSectionUVE(payload, offset) || PeeksMorphSectionUVE(payload, offset) ||
           PeeksSurfaceSectionUVE(payload, offset);
}

[[nodiscard]] bool ReadVector3FromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             Math::Vector3UVE& outValue) {
    return Utilities::ReadFloatLeFromBufferUVE(buffer, offset, outValue.x) &&
           Utilities::ReadFloatLeFromBufferUVE(buffer, offset, outValue.y) &&
           Utilities::ReadFloatLeFromBufferUVE(buffer, offset, outValue.z);
}

void AppendVector3UVE(std::vector<std::byte>& buffer, const Math::Vector3UVE& value) {
    Utilities::AppendFloatLeUVE(buffer, value.x);
    Utilities::AppendFloatLeUVE(buffer, value.y);
    Utilities::AppendFloatLeUVE(buffer, value.z);
}

[[nodiscard]] Math::Vector3UVE DeterministicTangentFallbackUVE(const Math::Vector3UVE& normal) noexcept {
    const Math::Vector3UVE axis = std::abs(normal.y) < 0.999F ? Math::Vector3UVE{0.0F, 1.0F, 0.0F}
                                                               : Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE tangent = Math::CrossUVE(axis, normal);
    if (Math::LengthSquaredUVE(tangent) <= 0.00000001F) {
        return Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    }
    return Math::NormalizeUVE(tangent);
}

} // namespace

[[nodiscard]] bool TryGenerateMeshTangentsUVE(std::span<MeshVertexUVE> vertices,
                                               std::span<const std::uint32_t> indices) {
    const auto isFiniteVector = [](const Math::Vector3UVE value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const auto addFinite = [&isFiniteVector](Math::Vector3UVE& sum,
                                              const Math::Vector3UVE value) noexcept {
        sum += value;
        return isFiniteVector(sum);
    };
    std::vector<Math::Vector3UVE> tangentSums(vertices.size());
    std::vector<Math::Vector3UVE> bitangentSums(vertices.size());

    for (std::size_t indexOffset = 0; indexOffset + 2U < indices.size(); indexOffset += 3U) {
        const std::uint32_t firstIndex = indices[indexOffset];
        const std::uint32_t secondIndex = indices[indexOffset + 1U];
        const std::uint32_t thirdIndex = indices[indexOffset + 2U];
        if (firstIndex >= vertices.size() || secondIndex >= vertices.size() || thirdIndex >= vertices.size()) {
            continue;
        }

        const MeshVertexUVE& first = vertices[firstIndex];
        const MeshVertexUVE& second = vertices[secondIndex];
        const MeshVertexUVE& third = vertices[thirdIndex];
        const Math::Vector3UVE positionEdgeOne = second.position - first.position;
        const Math::Vector3UVE positionEdgeTwo = third.position - first.position;
        const float uEdgeOne = second.u - first.u;
        const float vEdgeOne = second.v - first.v;
        const float uEdgeTwo = third.u - first.u;
        const float vEdgeTwo = third.v - first.v;
        const float determinant = uEdgeOne * vEdgeTwo - vEdgeOne * uEdgeTwo;
        if (std::abs(determinant) <= 0.00000001F) {
            continue;
        }

        const float inverseDeterminant = 1.0F / determinant;
        const Math::Vector3UVE triangleTangent =
            (positionEdgeOne * vEdgeTwo - positionEdgeTwo * vEdgeOne) * inverseDeterminant;
        const Math::Vector3UVE triangleBitangent =
            (positionEdgeTwo * uEdgeOne - positionEdgeOne * uEdgeTwo) * inverseDeterminant;
        if (!isFiniteVector(triangleTangent) || !isFiniteVector(triangleBitangent) ||
            !addFinite(tangentSums[firstIndex], triangleTangent) ||
            !addFinite(tangentSums[secondIndex], triangleTangent) ||
            !addFinite(tangentSums[thirdIndex], triangleTangent) ||
            !addFinite(bitangentSums[firstIndex], triangleBitangent) ||
            !addFinite(bitangentSums[secondIndex], triangleBitangent) ||
            !addFinite(bitangentSums[thirdIndex], triangleBitangent)) {
            return false;
        }
    }

    std::vector<MeshVertexUVE> generated(vertices.begin(), vertices.end());
    for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
        MeshVertexUVE& vertex = generated[vertexIndex];
        const float normalLengthSquared = Math::LengthSquaredUVE(vertex.normal);
        if (!std::isfinite(normalLengthSquared)) {
            return false;
        }
        const Math::Vector3UVE normal = normalLengthSquared > 0.00000001F
                                            ? Math::NormalizeUVE(vertex.normal)
                                            : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        if (!isFiniteVector(normal)) {
            return false;
        }
        const float normalDotTangentSum = Math::DotUVE(normal, tangentSums[vertexIndex]);
        if (!std::isfinite(normalDotTangentSum)) {
            return false;
        }
        Math::Vector3UVE tangent =
            tangentSums[vertexIndex] - normal * normalDotTangentSum;
        if (!isFiniteVector(tangent)) {
            return false;
        }
        const float tangentLengthSquared = Math::LengthSquaredUVE(tangent);
        if (!std::isfinite(tangentLengthSquared)) {
            return false;
        }
        if (tangentLengthSquared <= 0.00000001F) {
            tangent = DeterministicTangentFallbackUVE(normal);
        } else {
            tangent = Math::NormalizeUVE(tangent);
        }
        if (!isFiniteVector(tangent)) {
            return false;
        }
        const float handednessDot = Math::DotUVE(Math::CrossUVE(normal, tangent), bitangentSums[vertexIndex]);
        if (!std::isfinite(handednessDot)) {
            return false;
        }

        vertex.tangent = tangent;
        vertex.tangentHandedness = handednessDot < 0.0F ? -1.0F : 1.0F;
    }

    for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
        vertices[vertexIndex] = generated[vertexIndex];
    }
    return true;
}

void GenerateMeshTangentsUVE(std::span<MeshVertexUVE> vertices, std::span<const std::uint32_t> indices) {
    if (TryGenerateMeshTangentsUVE(vertices, indices)) {
        return;
    }
    for (MeshVertexUVE& vertex : vertices) {
        const float normalLengthSquared = Math::LengthSquaredUVE(vertex.normal);
        const Math::Vector3UVE normal = std::isfinite(normalLengthSquared) && normalLengthSquared > 0.00000001F
                                            ? Math::NormalizeUVE(vertex.normal)
                                            : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        vertex.tangent = DeterministicTangentFallbackUVE(normal);
        vertex.tangentHandedness = 1.0F;
    }
}

bool IsSkinningInfluenceNormalizedUVE(const MeshSkinningInfluenceUVE& influence,
                                      const float tolerance) noexcept {
    float sum = 0.0F;
    for (const float weight : influence.weights) {
        if (!std::isfinite(weight)) {
            return false; // No tolerance should ever accept a NaN sum.
        }
        sum += weight;
    }
    return std::fabs(sum - 1.0F) <= tolerance;
}

bool IsSkeletonTopologicallyOrderedUVE(const std::span<const MeshJointUVE> joints) noexcept {
    for (std::size_t index = 0U; index < joints.size(); ++index) {
        const std::uint32_t parent = joints[index].parentIndex;
        if (parent == kInvalidJointParentUVE) {
            continue; // A root.
        }
        // Strictly less than `index`, which rules out both a forward reference and a joint that
        // is its own parent in one comparison. That is the whole precondition a single forward
        // resolution pass needs - a cycle cannot exist if every parent precedes its child.
        if (parent >= index) {
            return false;
        }
    }
    return true;
}

bool IsMeshSkinningDataValidUVE(const MeshAssetUVE& mesh) {
    // A static mesh is valid. Most meshes are static, and making every caller special-case that
    // would guarantee somebody forgets.
    if (mesh.skinningInfluences.empty() && mesh.joints.empty()) {
        return true;
    }
    if (mesh.skinningInfluences.empty() || mesh.joints.empty()) {
        UVE_ERROR("MeshAssetUVE: skinning data is half-present ({} influences, {} joints) - a mesh "
                  "is either fully skinned or static",
                  mesh.skinningInfluences.size(), mesh.joints.size());
        return false;
    }
    if (mesh.skinningInfluences.size() != mesh.vertices.size()) {
        UVE_ERROR("MeshAssetUVE: {} skinning influences for {} vertices - a partially skinned mesh "
                  "is not representable",
                  mesh.skinningInfluences.size(), mesh.vertices.size());
        return false;
    }
    if (!IsSkeletonTopologicallyOrderedUVE(mesh.joints)) {
        UVE_ERROR("MeshAssetUVE: the skeleton is not topologically ordered - every joint's parent "
                  "must appear before it, which is what lets a pose resolve in one forward pass");
        return false;
    }
    for (const MeshJointUVE& joint : mesh.joints) {
        for (const auto& row : joint.inverseBindMatrix.m) {
            for (const float value : row) {
                if (!std::isfinite(value)) {
                    UVE_ERROR("MeshAssetUVE: a joint's inverse bind matrix is non-finite");
                    return false;
                }
            }
        }
    }
    const auto jointCount = static_cast<std::uint32_t>(mesh.joints.size());
    for (std::size_t index = 0U; index < mesh.skinningInfluences.size(); ++index) {
        const MeshSkinningInfluenceUVE& influence = mesh.skinningInfluences[index];
        for (std::size_t slot = 0U; slot < kMaxJointInfluencesUVE; ++slot) {
            // Checked even when the weight is zero: an out-of-range index is a malformed asset
            // whether or not the multiplication happens to cancel it out, and a later GPU path
            // indexing a joint array with it would read out of bounds regardless.
            if (influence.joints[slot] >= jointCount) {
                UVE_ERROR("MeshAssetUVE: vertex {} references joint {} but the skeleton has {}",
                          index, influence.joints[slot], jointCount);
                return false;
            }
            if (!std::isfinite(influence.weights[slot]) || influence.weights[slot] < 0.0F) {
                UVE_ERROR("MeshAssetUVE: vertex {} has a negative or non-finite skinning weight",
                          index);
                return false;
            }
        }
        if (!IsSkinningInfluenceNormalizedUVE(influence)) {
            UVE_ERROR("MeshAssetUVE: vertex {}'s skinning weights do not sum to one", index);
            return false;
        }
    }
    return true;
}

bool IsMeshMorphTargetDataValidUVE(const MeshAssetUVE& mesh) {
    // A mesh with neither shapes nor default weights is valid - that is every static mesh, and
    // every mesh from before blend shapes existed.
    if (mesh.morphTargets.empty() && mesh.defaultMorphWeights.empty()) {
        return true;
    }
    if (mesh.morphTargets.size() > kMaximumMeshMorphTargetsUVE) {
        UVE_ERROR("MeshAssetUVE: {} morph targets exceeds the format's cap of {}",
                  mesh.morphTargets.size(), kMaximumMeshMorphTargetsUVE);
        return false;
    }
    if (!mesh.defaultMorphWeights.empty() && mesh.defaultMorphWeights.size() != mesh.morphTargets.size()) {
        UVE_ERROR("MeshAssetUVE: {} default morph weights for {} morph targets - a partial weight "
                  "set has no meaning for the shapes it omits",
                  mesh.defaultMorphWeights.size(), mesh.morphTargets.size());
        return false;
    }
    for (const float weight : mesh.defaultMorphWeights) {
        if (!std::isfinite(weight)) {
            UVE_ERROR("MeshAssetUVE: a default morph weight is non-finite");
            return false;
        }
    }
    const auto vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t targetIndex = 0U; targetIndex < mesh.morphTargets.size(); ++targetIndex) {
        const MeshMorphTargetUVE& target = mesh.morphTargets[targetIndex];
        if (target.deltas.size() > kMaximumMeshMorphTargetDeltasUVE) {
            UVE_ERROR("MeshAssetUVE: morph target {} carries {} deltas, past the cap of {}",
                      targetIndex, target.deltas.size(), kMaximumMeshMorphTargetDeltasUVE);
            return false;
        }
        std::uint32_t previousIndex = 0U;
        bool hasPrevious = false;
        for (const MeshMorphTargetDeltaUVE& delta : target.deltas) {
            if (delta.vertexIndex >= vertexCount) {
                UVE_ERROR("MeshAssetUVE: morph target {} moves vertex {} but the mesh has {}",
                          targetIndex, delta.vertexIndex, vertexCount);
                return false;
            }
            // Strictly ascending, which is what makes a duplicate entry a detectable defect and
            // what lets an eval pass depend on accumulation order being fixed.
            if (hasPrevious && delta.vertexIndex <= previousIndex) {
                UVE_ERROR("MeshAssetUVE: morph target {} lists vertex {} out of order or twice",
                          targetIndex, delta.vertexIndex);
                return false;
            }
            previousIndex = delta.vertexIndex;
            hasPrevious = true;
            if (!Math::IsFiniteUVE(delta.positionDelta) || !Math::IsFiniteUVE(delta.normalDelta) ||
                !Math::IsFiniteUVE(delta.tangentDelta)) {
                UVE_ERROR("MeshAssetUVE: morph target {} has a non-finite delta on vertex {}",
                          targetIndex, delta.vertexIndex);
                return false;
            }
        }
    }
    return true;
}

bool IsMeshSurfaceDataValidUVE(const MeshAssetUVE& mesh) {
    // No explicit list means one implicit surface over the whole index buffer, which is every
    // asset from before surfaces existed and every single-material mesh. Nothing to check.
    if (mesh.surfaces.empty()) {
        return true;
    }
    if (mesh.surfaces.size() > kMaximumMeshSurfacesUVE) {
        UVE_ERROR("MeshAssetUVE: {} surfaces exceeds the format's cap of {}", mesh.surfaces.size(),
                  kMaximumMeshSurfacesUVE);
        return false;
    }
    std::uint32_t expectedFirstIndex = 0U;
    for (std::size_t surfaceIndex = 0U; surfaceIndex < mesh.surfaces.size(); ++surfaceIndex) {
        const MeshSurfaceUVE& surface = mesh.surfaces[surfaceIndex];
        if (surface.indexCount == 0U || surface.indexCount % 3U != 0U) {
            UVE_ERROR("MeshAssetUVE: surface {} has an index count of {}, which is not a whole "
                      "number of triangles", surfaceIndex, surface.indexCount);
            return false;
        }
        if (surface.firstIndex != expectedFirstIndex) {
            UVE_ERROR("MeshAssetUVE: surface {} starts at index {} but the previous surfaces end at "
                      "{} - surfaces must tile the index buffer with no gap and no overlap",
                      surfaceIndex, surface.firstIndex, expectedFirstIndex);
            return false;
        }
        const auto rangeEnd = static_cast<std::uint64_t>(surface.firstIndex) + surface.indexCount;
        if (rangeEnd > mesh.indices.size()) {
            UVE_ERROR("MeshAssetUVE: surface {} ends at index {} but the mesh has {} indices",
                      surfaceIndex, rangeEnd, mesh.indices.size());
            return false;
        }
        if (surface.materialSlot != kInvalidMeshSurfaceMaterialSlotUVE &&
            surface.materialSlot >= mesh.materialGuids.size()) {
            UVE_ERROR("MeshAssetUVE: surface {} names material slot {} but the mesh has {} slots",
                      surfaceIndex, surface.materialSlot, mesh.materialGuids.size());
            return false;
        }
        expectedFirstIndex = static_cast<std::uint32_t>(rangeEnd);
    }
    if (static_cast<std::size_t>(expectedFirstIndex) != mesh.indices.size()) {
        UVE_ERROR("MeshAssetUVE: surfaces cover {} of {} indices - the tail of the mesh would "
                  "never be drawn", expectedFirstIndex, mesh.indices.size());
        return false;
    }
    return true;
}

bool AccumulateMorphTargetsUVE(const std::span<const MeshVertexUVE> bindVertices,
                               const std::span<const MeshMorphTargetUVE> targets,
                               const std::span<const float> weights,
                               std::vector<MeshVertexUVE>& outVertices) {
    if (targets.size() != weights.size()) {
        UVE_ERROR("MeshAssetUVE: {} morph weights for {} morph targets - the weight array is "
                  "indexed by target, so it must match one for one",
                  weights.size(), targets.size());
        return false;
    }
    for (const float weight : weights) {
        if (!std::isfinite(weight)) {
            UVE_ERROR("MeshAssetUVE: a morph weight is non-finite");
            return false;
        }
    }
    std::vector<MeshVertexUVE> accumulated(bindVertices.begin(), bindVertices.end());
    for (std::size_t targetIndex = 0U; targetIndex < targets.size(); ++targetIndex) {
        // Exactly zero is the common case on a large rig - 52 shapes, three of them active - so
        // skipping is what keeps an unused shape free rather than merely cheap.
        const float weight = weights[targetIndex];
        if (weight == 0.0F) {
            continue;
        }
        for (const MeshMorphTargetDeltaUVE& delta : targets[targetIndex].deltas) {
            // Structurally invalid input (an out-of-range index) must not read out of bounds.
            if (delta.vertexIndex >= accumulated.size()) {
                UVE_ERROR("MeshAssetUVE: morph target {} moves vertex {} but the mesh has {}",
                          targetIndex, delta.vertexIndex, accumulated.size());
                return false;
            }
            MeshVertexUVE& vertex = accumulated[delta.vertexIndex];
            vertex.position += delta.positionDelta * weight;
            vertex.normal += delta.normalDelta * weight;
            vertex.tangent += delta.tangentDelta * weight;
            if (!Math::IsFiniteUVE(vertex.position) || !Math::IsFiniteUVE(vertex.normal) ||
                !Math::IsFiniteUVE(vertex.tangent)) {
                UVE_ERROR("MeshAssetUVE: morph target {} overflowed vertex {} into a non-finite value",
                          targetIndex, delta.vertexIndex);
                return false;
            }
        }
    }
    outVertices = std::move(accumulated);
    return true;
}

bool LoadMeshAssetUVE(const std::filesystem::path& path, MeshAssetUVE& outMesh) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value()) {
        return false; // ReadUveFileUVE already logged the specific reason.
    }
    if (file->first.assetType != AssetKindUVE::Mesh) {
        UVE_ERROR("MeshAssetUVE: \"{}\" is not a mesh file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payload = file->second;
    std::size_t offset = 0;

    std::uint32_t vertexCount = 0;
    if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, vertexCount)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated vertex count", path.string());
        return false;
    }
    constexpr std::size_t kSerializedMeshVertexBytesUVE = sizeof(float) * 8U;
    constexpr std::size_t kSerializedMeshTailBytesUVE = sizeof(std::uint32_t) + sizeof(float) * 6U;
    if (payload.size() < offset || payload.size() - offset < kSerializedMeshTailBytesUVE ||
        vertexCount > (payload.size() - offset - kSerializedMeshTailBytesUVE) /
                          kSerializedMeshVertexBytesUVE) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has an impossible vertex count", path.string());
        return false;
    }

    std::vector<MeshVertexUVE> vertices;
    vertices.reserve(vertexCount);
    for (std::uint32_t index = 0; index < vertexCount; ++index) {
        MeshVertexUVE vertex;
        if (!ReadVector3FromBufferUVE(payload, offset, vertex.position) ||
            !ReadVector3FromBufferUVE(payload, offset, vertex.normal) ||
            !Utilities::ReadFloatLeFromBufferUVE(payload, offset, vertex.u) || !Utilities::ReadFloatLeFromBufferUVE(payload, offset, vertex.v)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has truncated vertex data", path.string());
            return false;
        }
        vertices.push_back(vertex);
    }

    std::uint32_t indexCount = 0;
    if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, indexCount)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated index count", path.string());
        return false;
    }
    if (payload.size() < offset || indexCount > (payload.size() - offset) / sizeof(std::uint32_t)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has an impossible index count", path.string());
        return false;
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index) {
        std::uint32_t value = 0;
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, value)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has truncated index data", path.string());
            return false;
        }
        if (value >= vertices.size()) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has an out-of-bounds index {} (only {} vertices)", path.string(), value,
                       vertices.size());
            return false;
        }
        indices.push_back(value);
    }

    Math::AabbUVE localBounds;
    if (!ReadVector3FromBufferUVE(payload, offset, localBounds.min) ||
        !ReadVector3FromBufferUVE(payload, offset, localBounds.max)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated local bounds", path.string());
        return false;
    }

    // The skinning section is OPTIONAL and trails the bounds, which is what keeps every existing
    // .uvmodel loadable byte for byte. The envelope's version field is global across every asset
    // kind, so bumping it to advertise skinning would invalidate scenes, materials and textures
    // that have nothing to do with meshes; the payload's own natural end is the honest place to
    // detect "this file predates skinning". A file that stops here is a static mesh, not an error.
    std::vector<MeshSkinningInfluenceUVE> skinningInfluences;
    std::vector<MeshJointUVE> joints;
    if (offset < payload.size() && !PeeksTrailingSectionUVE(payload, offset)) {
        std::uint32_t jointCount = 0;
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, jointCount)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated joint count", path.string());
            return false;
        }
        constexpr std::size_t kSerializedJointBytesUVE = sizeof(std::uint32_t) + sizeof(float) * 16U;
        if (payload.size() < offset ||
            jointCount > (payload.size() - offset) / kSerializedJointBytesUVE) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has an impossible joint count", path.string());
            return false;
        }
        joints.reserve(jointCount);
        for (std::uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            MeshJointUVE joint;
            if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, joint.parentIndex)) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has truncated joint data", path.string());
                return false;
            }
            bool matrixComplete = true;
            for (auto& row : joint.inverseBindMatrix.m) {
                for (float& value : row) {
                    matrixComplete = matrixComplete && Utilities::ReadFloatLeFromBufferUVE(payload, offset, value);
                }
            }
            if (!matrixComplete) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated inverse bind matrix", path.string());
                return false;
            }
            joints.push_back(joint);
        }

        // One influence per vertex, always - the count is not re-serialized because a value other
        // than vertices.size() would be unrepresentable anyway, and storing it would just create a
        // second source of truth to disagree with the first.
        skinningInfluences.reserve(vertices.size());
        for (std::size_t vertexIndex = 0U; vertexIndex < vertices.size(); ++vertexIndex) {
            MeshSkinningInfluenceUVE influence;
            bool complete = true;
            for (std::uint32_t& jointSlot : influence.joints) {
                complete = complete && Utilities::ReadUint32LeFromBufferUVE(payload, offset, jointSlot);
            }
            for (float& weight : influence.weights) {
                complete = complete && Utilities::ReadFloatLeFromBufferUVE(payload, offset, weight);
            }
            if (!complete) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has truncated skinning influences", path.string());
                return false;
            }
            skinningInfluences.push_back(influence);
        }

        if (offset < payload.size() && !PeeksTrailingSectionUVE(payload, offset)) {
            std::uint32_t nameCount = 0;
            if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, nameCount) || nameCount != joints.size()) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a joint name table that does not match its joints", path.string());
                return false;
            }
            for (MeshJointUVE& joint : joints) {
                std::uint32_t length = 0;
                if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, length) ||
                    length > kMaximumMeshSectionNameBytesUVE ||
                    payload.size() - offset < length) {
                    UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated joint name", path.string());
                    return false;
                }
                joint.name.assign(reinterpret_cast<const char*>(payload.data() + offset), length);
                offset += length;
            }
        }
    }

    // WP7: the material references trail everything else behind their tag. A file that ends
    // before the tag simply has none; bytes after an unknown tag are ignored exactly as trailing
    // bytes after the joint names always were.
    std::vector<AssetGuidUVE> materialGuids;
    if (offset < payload.size() && PeeksMaterialSectionUVE(payload, offset)) {
        std::uint32_t tag = 0U;
        std::uint32_t materialCount = 0U;
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, tag) ||
            !Utilities::ReadUint32LeFromBufferUVE(payload, offset, materialCount) ||
            materialCount > kMaximumMeshMaterialSlotsUVE ||
            materialCount > (payload.size() - offset) / sizeof(std::uint64_t)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated or impossible material reference table", path.string());
            return false;
        }
        materialGuids.reserve(materialCount);
        for (std::uint32_t materialIndex = 0U; materialIndex < materialCount; ++materialIndex) {
            std::uint64_t guidValue = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(payload, offset, guidValue)) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated material reference", path.string());
                return false;
            }
            materialGuids.push_back(AssetGuidUVE{guidValue});
        }
    }

    // Blend shapes trail every other section, behind their own tag, for the same reason skinning
    // and materials do: a file written before shapes existed simply ends, and a mesh without
    // shapes keeps writing the exact bytes it always did.
    std::vector<MeshMorphTargetUVE> morphTargets;
    std::vector<float> defaultMorphWeights;
    if (offset < payload.size() && PeeksMorphSectionUVE(payload, offset)) {
        std::uint32_t tag = 0U;
        std::uint32_t targetCount = 0U;
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, tag) ||
            !Utilities::ReadUint32LeFromBufferUVE(payload, offset, targetCount) ||
            targetCount > kMaximumMeshMorphTargetsUVE) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated or impossible morph target count", path.string());
            return false;
        }
        // Every target costs at least its name length + delta count, so a count larger than the
        // bytes left cannot be honest. Checked before reserve() so a hostile file cannot make the
        // loader allocate gigabytes it will never fill.
        if (targetCount > (payload.size() - offset) / (sizeof(std::uint32_t) * 2U)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" claims {} morph targets with {} bytes left", path.string(),
                      targetCount, payload.size() - offset);
            return false;
        }
        morphTargets.reserve(targetCount);
        for (std::uint32_t targetIndex = 0U; targetIndex < targetCount; ++targetIndex) {
            MeshMorphTargetUVE target;
            std::uint32_t nameLength = 0U;
            std::uint32_t deltaCount = 0U;
            if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, nameLength) ||
                nameLength > kMaximumMeshSectionNameBytesUVE || payload.size() - offset < nameLength) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated morph target name", path.string());
                return false;
            }
            target.name.assign(reinterpret_cast<const char*>(payload.data() + offset), nameLength);
            offset += nameLength;
            if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, deltaCount) ||
                deltaCount > kMaximumMeshMorphTargetDeltasUVE ||
                deltaCount > (payload.size() - offset) / kSerializedMorphDeltaBytesUVE) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated or impossible morph delta count", path.string());
                return false;
            }
            target.deltas.reserve(deltaCount);
            for (std::uint32_t deltaIndex = 0U; deltaIndex < deltaCount; ++deltaIndex) {
                MeshMorphTargetDeltaUVE delta;
                if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, delta.vertexIndex) ||
                    !ReadVector3FromBufferUVE(payload, offset, delta.positionDelta) ||
                    !ReadVector3FromBufferUVE(payload, offset, delta.normalDelta) ||
                    !ReadVector3FromBufferUVE(payload, offset, delta.tangentDelta)) {
                    UVE_ERROR("MeshAssetUVE: \"{}\" has truncated morph delta data", path.string());
                    return false;
                }
                target.deltas.push_back(delta);
            }
            morphTargets.push_back(std::move(target));
        }
        // "All zero" and "the source said nothing" are different states - the first is an authored
        // rest pose, the second is an asset that has no opinion - and an implicit size cannot tell
        // them apart at the end of a payload. One explicit count resolves both without a sentinel.
        std::uint32_t weightCount = 0U;
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, weightCount) ||
            (weightCount != 0U && weightCount != targetCount)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has an impossible default morph weight count", path.string());
            return false;
        }
        if (weightCount > 0U) {
            defaultMorphWeights.reserve(weightCount);
            for (std::uint32_t weightIndex = 0U; weightIndex < weightCount; ++weightIndex) {
                float weight = 0.0F;
                if (!Utilities::ReadFloatLeFromBufferUVE(payload, offset, weight)) {
                    UVE_ERROR("MeshAssetUVE: \"{}\" has truncated default morph weights", path.string());
                    return false;
                }
                defaultMorphWeights.push_back(weight);
            }
        }
    }

    // Surfaces trail every other section, behind their own tag, for the same reason skinning,
    // materials and shapes do: an asset written before surfaces existed simply ends, and a mesh
    // without surfaces keeps writing the exact bytes it always did.
    std::vector<MeshSurfaceUVE> surfaces;
    if (offset < payload.size() && PeeksSurfaceSectionUVE(payload, offset)) {
        std::uint32_t tag = 0U;
        std::uint32_t surfaceCount = 0U;
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, tag) ||
            !Utilities::ReadUint32LeFromBufferUVE(payload, offset, surfaceCount) || surfaceCount == 0U ||
            surfaceCount > kMaximumMeshSurfacesUVE ||
            surfaceCount > (payload.size() - offset) / kSerializedMeshSurfaceBytesUVE) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has a truncated or impossible surface table", path.string());
            return false;
        }
        surfaces.reserve(surfaceCount);
        for (std::uint32_t surfaceIndex = 0U; surfaceIndex < surfaceCount; ++surfaceIndex) {
            MeshSurfaceUVE surface;
            if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, surface.firstIndex) ||
                !Utilities::ReadUint32LeFromBufferUVE(payload, offset, surface.indexCount) ||
                !Utilities::ReadUint32LeFromBufferUVE(payload, offset, surface.materialSlot)) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has truncated surface data", path.string());
                return false;
            }
            surfaces.push_back(surface);
        }
    }

    if (!TryGenerateMeshTangentsUVE(vertices, indices)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has non-finite generated tangent data", path.string());
        return false;
    }
    // Validate before publishing, and validate on the CANDIDATE rather than on outMesh - the
    // loader's existing contract is that a rejected file leaves the caller's mesh untouched.
    MeshAssetUVE candidate;
    candidate.vertices = std::move(vertices);
    candidate.indices = std::move(indices);
    candidate.localBounds = localBounds;
    candidate.skinningInfluences = std::move(skinningInfluences);
    candidate.joints = std::move(joints);
    candidate.materialGuids = std::move(materialGuids);
    candidate.surfaces = std::move(surfaces);
    candidate.morphTargets = std::move(morphTargets);
    candidate.defaultMorphWeights = std::move(defaultMorphWeights);
    if (!IsMeshSkinningDataValidUVE(candidate)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has structurally invalid skinning data", path.string());
        return false;
    }
    if (!IsMeshMorphTargetDataValidUVE(candidate)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has structurally invalid morph target data", path.string());
        return false;
    }
    if (!IsMeshSurfaceDataValidUVE(candidate)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has structurally invalid surface data", path.string());
        return false;
    }
    outMesh = std::move(candidate);
    return true;
}

bool SaveMeshAssetUVE(const MeshAssetUVE& mesh, const std::filesystem::path& path) {
    std::vector<std::byte> payload;
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.vertices.size()));
    for (const MeshVertexUVE& vertex : mesh.vertices) {
        AppendVector3UVE(payload, vertex.position);
        AppendVector3UVE(payload, vertex.normal);
        Utilities::AppendFloatLeUVE(payload, vertex.u);
        Utilities::AppendFloatLeUVE(payload, vertex.v);
    }

    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.indices.size()));
    for (std::uint32_t index : mesh.indices) {
        Utilities::AppendUint32LeUVE(payload, index);
    }

    AppendVector3UVE(payload, mesh.localBounds.min);
    AppendVector3UVE(payload, mesh.localBounds.max);

    // A static mesh writes nothing further, so its bytes stay identical to what previous versions
    // of this engine produced - which is the property that makes the optional trailing section
    // safe in both directions, not just on read.
    if (mesh.IsSkinnedUVE()) {
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.joints.size()));
        for (const MeshJointUVE& joint : mesh.joints) {
            Utilities::AppendUint32LeUVE(payload, joint.parentIndex);
            for (const auto& row : joint.inverseBindMatrix.m) {
                for (const float value : row) {
                    Utilities::AppendFloatLeUVE(payload, value);
                }
            }
        }
        for (const MeshSkinningInfluenceUVE& influence : mesh.skinningInfluences) {
            for (const std::uint32_t joint : influence.joints) {
                Utilities::AppendUint32LeUVE(payload, joint);
            }
            for (const float weight : influence.weights) {
                Utilities::AppendFloatLeUVE(payload, weight);
            }
        }
        // Joint names trail the influences for the same reason the skin trails the bounds: a file
        // from before they were kept simply ends here.
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.joints.size()));
        for (const MeshJointUVE& joint : mesh.joints) {
            Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(joint.name.size()));
            const auto* const bytes = reinterpret_cast<const std::byte*>(joint.name.data());
            payload.insert(payload.end(), bytes, bytes + joint.name.size());
        }
    }

    // WP7: material references, tagged, and only when there are any - a mesh without materials
    // keeps writing the exact bytes it always did.
    if (!mesh.materialGuids.empty()) {
        if (mesh.materialGuids.size() > kMaximumMeshMaterialSlotsUVE) {
            UVE_ERROR("MeshAssetUVE: \"{}\" references {} materials; the format caps slots at {}", path.string(),
                      mesh.materialGuids.size(), kMaximumMeshMaterialSlotsUVE);
            return false;
        }
        Utilities::AppendUint32LeUVE(payload, kMeshMaterialSectionTagUVE);
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.materialGuids.size()));
        for (const AssetGuidUVE guid : mesh.materialGuids) {
            Utilities::AppendUint64LeUVE(payload, guid.value);
        }
    }

    // Blend shapes, tagged and last, written only when the mesh has any - a mesh without shapes
    // keeps producing the exact bytes every previous version of this engine produced.
    if (!mesh.morphTargets.empty()) {
        if (mesh.morphTargets.size() > kMaximumMeshMorphTargetsUVE) {
            UVE_ERROR("MeshAssetUVE: \"{}\" carries {} morph targets; the format caps them at {}",
                      path.string(), mesh.morphTargets.size(), kMaximumMeshMorphTargetsUVE);
            return false;
        }
        if (!mesh.defaultMorphWeights.empty() && mesh.defaultMorphWeights.size() != mesh.morphTargets.size()) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has {} default morph weights for {} targets", path.string(),
                      mesh.defaultMorphWeights.size(), mesh.morphTargets.size());
            return false;
        }
        Utilities::AppendUint32LeUVE(payload, kMeshMorphSectionTagUVE);
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.morphTargets.size()));
        for (const MeshMorphTargetUVE& target : mesh.morphTargets) {
            if (target.name.size() > kMaximumMeshSectionNameBytesUVE) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a morph target name of {} bytes; the format caps "
                          "names at {}", path.string(), target.name.size(), kMaximumMeshSectionNameBytesUVE);
                return false;
            }
            Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(target.name.size()));
            const auto* const bytes = reinterpret_cast<const std::byte*>(target.name.data());
            payload.insert(payload.end(), bytes, bytes + target.name.size());
            if (target.deltas.size() > kMaximumMeshMorphTargetDeltasUVE) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has a morph target with {} deltas; the format caps them "
                          "at {}", path.string(), target.deltas.size(), kMaximumMeshMorphTargetDeltasUVE);
                return false;
            }
            Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(target.deltas.size()));
            for (const MeshMorphTargetDeltaUVE& delta : target.deltas) {
                Utilities::AppendUint32LeUVE(payload, delta.vertexIndex);
                AppendVector3UVE(payload, delta.positionDelta);
                AppendVector3UVE(payload, delta.normalDelta);
                AppendVector3UVE(payload, delta.tangentDelta);
            }
        }
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.defaultMorphWeights.size()));
        for (const float weight : mesh.defaultMorphWeights) {
            Utilities::AppendFloatLeUVE(payload, weight);
        }
    }

    // Surfaces, tagged and last, written only when the mesh has more than the implicit single
    // one. A single-material mesh therefore keeps producing the exact bytes it always did.
    if (!mesh.surfaces.empty()) {
        if (!IsMeshSurfaceDataValidUVE(mesh)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has invalid surface data; refusing to write it", path.string());
            return false;
        }
        Utilities::AppendUint32LeUVE(payload, kMeshSurfaceSectionTagUVE);
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(mesh.surfaces.size()));
        for (const MeshSurfaceUVE& surface : mesh.surfaces) {
            Utilities::AppendUint32LeUVE(payload, surface.firstIndex);
            Utilities::AppendUint32LeUVE(payload, surface.indexCount);
            Utilities::AppendUint32LeUVE(payload, surface.materialSlot);
        }
    }

    return WriteUveFileUVE(path, AssetKindUVE::Mesh, payload);
}

} // namespace UVE::Asset
