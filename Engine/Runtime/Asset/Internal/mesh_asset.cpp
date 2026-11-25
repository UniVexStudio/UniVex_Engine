// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_asset.h"
#include "model_animations.h"
#include "uve_file_envelope.h"
#include <cstring>

#include <cmath>
#include <cstring>
#include <span>

#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "binary_buffer.h"

namespace UVE::Asset {

namespace {

/// WP7: tag that opens the optional material-reference section ('UVMT' little-endian). It is
/// unambiguous against the skin section's joint count because a count this large fails the
/// joint-count bound check on any real file.
constexpr std::uint32_t kMeshMaterialSectionTag = 0x544D5655U;
constexpr std::uint32_t kMaximumMeshMaterialSlots = 4096U;

/// Tag that opens the optional morph-target section ('UVMP' little-endian), written after the
/// material section. It carries the same unambiguity argument: read as a joint count it would be
/// 1.3 billion, which fails the bound check on any file small enough to exist.
constexpr std::uint32_t kMeshMorphSectionTag = 0x504D5655U;

/// Tag that opens the optional surface section ('UVMS' little-endian), written after the morph
/// section. Same unambiguity argument again: as a joint count it is 1.4 billion, which no file
/// that fits in memory can pass.
constexpr std::uint32_t kMeshSurfaceSectionTag = 0x534D5655U;

/// One serialized surface: first index, index count, material slot.
constexpr std::size_t kSerializedMeshSurfaceBytes = sizeof(std::uint32_t) * 3U;

/// Upper bound on a single target or joint NAME's serialized length. Names identify shapes and
/// bones, not paragraphs; a longer one is corruption rather than content.
constexpr std::uint32_t kMaximumMeshSectionNameBytes = 256U;

/// One sparse morph delta on disk: a vertex index plus three vec3s.
constexpr std::size_t kSerializedMorphDeltaBytes = sizeof(std::uint32_t) + sizeof(float) * 9U;

[[nodiscard]] bool PeeksMaterialSection(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    return Utilities::ReadUint32LeFromBuffer(payload, peekOffset, tag) && tag == kMeshMaterialSectionTag;
}

[[nodiscard]] bool PeeksMorphSection(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    return Utilities::ReadUint32LeFromBuffer(payload, peekOffset, tag) && tag == kMeshMorphSectionTag;
}

/// Whether a known tagged section starts here. Every "does anything trail this?" test must use
/// this rather than one specific tag, or a file whose next section is a different one gets read as
/// if it were not there at all.
[[nodiscard]] bool PeeksSurfaceSection(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    return Utilities::ReadUint32LeFromBuffer(payload, peekOffset, tag) && tag == kMeshSurfaceSectionTag;
}

[[nodiscard]] bool PeeksTrailingSection(const std::vector<std::byte>& payload, const std::size_t offset) {
    std::size_t peekOffset = offset;
    std::uint32_t tag = 0U;
    // A model's own takes (model_animations.h) trail every mesh section; they are not mesh data.
    const bool takes = Utilities::ReadUint32LeFromBuffer(payload, peekOffset, tag) && tag == kModelAnimationSectionTag;
    return takes || PeeksMaterialSection(payload, offset) || PeeksMorphSection(payload, offset) ||
           PeeksSurfaceSection(payload, offset);
}

[[nodiscard]] bool ReadVector3FromBuffer(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             Math::Vector3& outValue) {
    return Utilities::ReadFloatLeFromBuffer(buffer, offset, outValue.x) &&
           Utilities::ReadFloatLeFromBuffer(buffer, offset, outValue.y) &&
           Utilities::ReadFloatLeFromBuffer(buffer, offset, outValue.z);
}

void AppendVector3(std::vector<std::byte>& buffer, const Math::Vector3& value) {
    Utilities::AppendFloatLe(buffer, value.x);
    Utilities::AppendFloatLe(buffer, value.y);
    Utilities::AppendFloatLe(buffer, value.z);
}

[[nodiscard]] Math::Vector3 DeterministicTangentFallback(const Math::Vector3& normal) noexcept {
    const Math::Vector3 axis = std::abs(normal.y) < 0.999F ? Math::Vector3{0.0F, 1.0F, 0.0F}
                                                               : Math::Vector3{1.0F, 0.0F, 0.0F};
    const Math::Vector3 tangent = Math::Cross(axis, normal);
    if (Math::LengthSquared(tangent) <= 0.00000001F) {
        return Math::Vector3{1.0F, 0.0F, 0.0F};
    }
    return Math::Normalize(tangent);
}

} // namespace

[[nodiscard]] bool TryGenerateMeshTangents(std::span<MeshVertex> vertices,
                                               std::span<const std::uint32_t> indices) {
    const auto isFiniteVector = [](const Math::Vector3 value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const auto addFinite = [&isFiniteVector](Math::Vector3& sum,
                                              const Math::Vector3 value) noexcept {
        sum += value;
        return isFiniteVector(sum);
    };
    std::vector<Math::Vector3> tangentSums(vertices.size());
    std::vector<Math::Vector3> bitangentSums(vertices.size());

    for (std::size_t indexOffset = 0; indexOffset + 2U < indices.size(); indexOffset += 3U) {
        const std::uint32_t firstIndex = indices[indexOffset];
        const std::uint32_t secondIndex = indices[indexOffset + 1U];
        const std::uint32_t thirdIndex = indices[indexOffset + 2U];
        if (firstIndex >= vertices.size() || secondIndex >= vertices.size() || thirdIndex >= vertices.size()) {
            continue;
        }

        const MeshVertex& first = vertices[firstIndex];
        const MeshVertex& second = vertices[secondIndex];
        const MeshVertex& third = vertices[thirdIndex];
        const Math::Vector3 positionEdgeOne = second.position - first.position;
        const Math::Vector3 positionEdgeTwo = third.position - first.position;
        const float uEdgeOne = second.u - first.u;
        const float vEdgeOne = second.v - first.v;
        const float uEdgeTwo = third.u - first.u;
        const float vEdgeTwo = third.v - first.v;
        const float determinant = uEdgeOne * vEdgeTwo - vEdgeOne * uEdgeTwo;
        if (std::abs(determinant) <= 0.00000001F) {
            continue;
        }

        const float inverseDeterminant = 1.0F / determinant;
        const Math::Vector3 triangleTangent =
            (positionEdgeOne * vEdgeTwo - positionEdgeTwo * vEdgeOne) * inverseDeterminant;
        const Math::Vector3 triangleBitangent =
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

    std::vector<MeshVertex> generated(vertices.begin(), vertices.end());
    for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
        MeshVertex& vertex = generated[vertexIndex];
        const float normalLengthSquared = Math::LengthSquared(vertex.normal);
        if (!std::isfinite(normalLengthSquared)) {
            return false;
        }
        const Math::Vector3 normal = normalLengthSquared > 0.00000001F
                                            ? Math::Normalize(vertex.normal)
                                            : Math::Vector3{0.0F, 1.0F, 0.0F};
        if (!isFiniteVector(normal)) {
            return false;
        }
        const float normalDotTangentSum = Math::Dot(normal, tangentSums[vertexIndex]);
        if (!std::isfinite(normalDotTangentSum)) {
            return false;
        }
        Math::Vector3 tangent =
            tangentSums[vertexIndex] - normal * normalDotTangentSum;
        if (!isFiniteVector(tangent)) {
            return false;
        }
        const float tangentLengthSquared = Math::LengthSquared(tangent);
        if (!std::isfinite(tangentLengthSquared)) {
            return false;
        }
        if (tangentLengthSquared <= 0.00000001F) {
            tangent = DeterministicTangentFallback(normal);
        } else {
            tangent = Math::Normalize(tangent);
        }
        if (!isFiniteVector(tangent)) {
            return false;
        }
        const float handednessDot = Math::Dot(Math::Cross(normal, tangent), bitangentSums[vertexIndex]);
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

void GenerateMeshTangents(std::span<MeshVertex> vertices, std::span<const std::uint32_t> indices) {
    if (TryGenerateMeshTangents(vertices, indices)) {
        return;
    }
    for (MeshVertex& vertex : vertices) {
        const float normalLengthSquared = Math::LengthSquared(vertex.normal);
        const Math::Vector3 normal = std::isfinite(normalLengthSquared) && normalLengthSquared > 0.00000001F
                                            ? Math::Normalize(vertex.normal)
                                            : Math::Vector3{0.0F, 1.0F, 0.0F};
        vertex.tangent = DeterministicTangentFallback(normal);
        vertex.tangentHandedness = 1.0F;
    }
}

bool IsSkinningInfluenceNormalized(const MeshSkinningInfluence& influence,
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

bool IsSkeletonTopologicallyOrdered(const std::span<const MeshJoint> joints) noexcept {
    for (std::size_t index = 0U; index < joints.size(); ++index) {
        const std::uint32_t parent = joints[index].parentIndex;
        if (parent == kInvalidJointParent) {
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

bool IsMeshSkinningDataValid(const MeshAsset& mesh) {
    // A static mesh is valid. Most meshes are static, and making every caller special-case that
    // would guarantee somebody forgets.
    if (mesh.skinningInfluences.empty() && mesh.joints.empty()) {
        return true;
    }
    if (mesh.skinningInfluences.empty() || mesh.joints.empty()) {
        UVE_ERROR("MeshAsset: skinning data is half-present ({} influences, {} joints) - a mesh "
                  "is either fully skinned or static",
                  mesh.skinningInfluences.size(), mesh.joints.size());
        return false;
    }
    if (mesh.skinningInfluences.size() != mesh.vertices.size()) {
        UVE_ERROR("MeshAsset: {} skinning influences for {} vertices - a partially skinned mesh "
                  "is not representable",
                  mesh.skinningInfluences.size(), mesh.vertices.size());
        return false;
    }
    if (!IsSkeletonTopologicallyOrdered(mesh.joints)) {
        UVE_ERROR("MeshAsset: the skeleton is not topologically ordered - every joint's parent "
                  "must appear before it, which is what lets a pose resolve in one forward pass");
        return false;
    }
    for (const MeshJoint& joint : mesh.joints) {
        for (const auto& row : joint.inverseBindMatrix.m) {
            for (const float value : row) {
                if (!std::isfinite(value)) {
                    UVE_ERROR("MeshAsset: a joint's inverse bind matrix is non-finite");
                    return false;
                }
            }
        }
    }
    const auto jointCount = static_cast<std::uint32_t>(mesh.joints.size());
    for (std::size_t index = 0U; index < mesh.skinningInfluences.size(); ++index) {
        const MeshSkinningInfluence& influence = mesh.skinningInfluences[index];
        for (std::size_t slot = 0U; slot < kMaxJointInfluences; ++slot) {
            // Checked even when the weight is zero: an out-of-range index is a malformed asset
            // whether or not the multiplication happens to cancel it out, and a later GPU path
            // indexing a joint array with it would read out of bounds regardless.
            if (influence.joints[slot] >= jointCount) {
                UVE_ERROR("MeshAsset: vertex {} references joint {} but the skeleton has {}",
                          index, influence.joints[slot], jointCount);
                return false;
            }
            if (!std::isfinite(influence.weights[slot]) || influence.weights[slot] < 0.0F) {
                UVE_ERROR("MeshAsset: vertex {} has a negative or non-finite skinning weight",
                          index);
                return false;
            }
        }
        if (!IsSkinningInfluenceNormalized(influence)) {
            UVE_ERROR("MeshAsset: vertex {}'s skinning weights do not sum to one", index);
            return false;
        }
    }
    return true;
}

bool IsMeshMorphTargetDataValid(const MeshAsset& mesh) {
    // A mesh with neither shapes nor default weights is valid - that is every static mesh, and
    // every mesh from before blend shapes existed.
    if (mesh.morphTargets.empty() && mesh.defaultMorphWeights.empty()) {
        return true;
    }
    if (mesh.morphTargets.size() > kMaximumMeshMorphTargets) {
        UVE_ERROR("MeshAsset: {} morph targets exceeds the format's cap of {}",
                  mesh.morphTargets.size(), kMaximumMeshMorphTargets);
        return false;
    }
    if (!mesh.defaultMorphWeights.empty() && mesh.defaultMorphWeights.size() != mesh.morphTargets.size()) {
        UVE_ERROR("MeshAsset: {} default morph weights for {} morph targets - a partial weight "
                  "set has no meaning for the shapes it omits",
                  mesh.defaultMorphWeights.size(), mesh.morphTargets.size());
        return false;
    }
    for (const float weight : mesh.defaultMorphWeights) {
        if (!std::isfinite(weight)) {
            UVE_ERROR("MeshAsset: a default morph weight is non-finite");
            return false;
        }
    }
    const auto vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t targetIndex = 0U; targetIndex < mesh.morphTargets.size(); ++targetIndex) {
        const MeshMorphTarget& target = mesh.morphTargets[targetIndex];
        if (target.deltas.size() > kMaximumMeshMorphTargetDeltas) {
            UVE_ERROR("MeshAsset: morph target {} carries {} deltas, past the cap of {}",
                      targetIndex, target.deltas.size(), kMaximumMeshMorphTargetDeltas);
            return false;
        }
        std::uint32_t previousIndex = 0U;
        bool hasPrevious = false;
        for (const MeshMorphTargetDelta& delta : target.deltas) {
            if (delta.vertexIndex >= vertexCount) {
                UVE_ERROR("MeshAsset: morph target {} moves vertex {} but the mesh has {}",
                          targetIndex, delta.vertexIndex, vertexCount);
                return false;
            }
            // Strictly ascending, which is what makes a duplicate entry a detectable defect and
            // what lets an eval pass depend on accumulation order being fixed.
            if (hasPrevious && delta.vertexIndex <= previousIndex) {
                UVE_ERROR("MeshAsset: morph target {} lists vertex {} out of order or twice",
                          targetIndex, delta.vertexIndex);
                return false;
            }
            previousIndex = delta.vertexIndex;
            hasPrevious = true;
            if (!Math::IsFinite(delta.positionDelta) || !Math::IsFinite(delta.normalDelta) ||
                !Math::IsFinite(delta.tangentDelta)) {
                UVE_ERROR("MeshAsset: morph target {} has a non-finite delta on vertex {}",
                          targetIndex, delta.vertexIndex);
                return false;
            }
        }
    }
    return true;
}

bool IsMeshSurfaceDataValid(const MeshAsset& mesh) {
    // No explicit list means one implicit surface over the whole index buffer, which is every
    // asset from before surfaces existed and every single-material mesh. Nothing to check.
    if (mesh.surfaces.empty()) {
        return true;
    }
    if (mesh.surfaces.size() > kMaximumMeshSurfaces) {
        UVE_ERROR("MeshAsset: {} surfaces exceeds the format's cap of {}", mesh.surfaces.size(),
                  kMaximumMeshSurfaces);
        return false;
    }
    std::uint32_t expectedFirstIndex = 0U;
    for (std::size_t surfaceIndex = 0U; surfaceIndex < mesh.surfaces.size(); ++surfaceIndex) {
        const MeshSurface& surface = mesh.surfaces[surfaceIndex];
        if (surface.indexCount == 0U || surface.indexCount % 3U != 0U) {
            UVE_ERROR("MeshAsset: surface {} has an index count of {}, which is not a whole "
                      "number of triangles", surfaceIndex, surface.indexCount);
            return false;
        }
        if (surface.firstIndex != expectedFirstIndex) {
            UVE_ERROR("MeshAsset: surface {} starts at index {} but the previous surfaces end at "
                      "{} - surfaces must tile the index buffer with no gap and no overlap",
                      surfaceIndex, surface.firstIndex, expectedFirstIndex);
            return false;
        }
        const auto rangeEnd = static_cast<std::uint64_t>(surface.firstIndex) + surface.indexCount;
        if (rangeEnd > mesh.indices.size()) {
            UVE_ERROR("MeshAsset: surface {} ends at index {} but the mesh has {} indices",
                      surfaceIndex, rangeEnd, mesh.indices.size());
            return false;
        }
        if (surface.materialSlot != kInvalidMeshSurfaceMaterialSlot &&
            surface.materialSlot >= mesh.materialGuids.size()) {
            UVE_ERROR("MeshAsset: surface {} names material slot {} but the mesh has {} slots",
                      surfaceIndex, surface.materialSlot, mesh.materialGuids.size());
            return false;
        }
        expectedFirstIndex = static_cast<std::uint32_t>(rangeEnd);
    }
    if (static_cast<std::size_t>(expectedFirstIndex) != mesh.indices.size()) {
        UVE_ERROR("MeshAsset: surfaces cover {} of {} indices - the tail of the mesh would "
                  "never be drawn", expectedFirstIndex, mesh.indices.size());
        return false;
    }
    return true;
}

bool AccumulateMorphTargets(const std::span<const MeshVertex> bindVertices,
                               const std::span<const MeshMorphTarget> targets,
                               const std::span<const float> weights,
                               std::vector<MeshVertex>& outVertices) {
    if (targets.size() != weights.size()) {
        UVE_ERROR("MeshAsset: {} morph weights for {} morph targets - the weight array is "
                  "indexed by target, so it must match one for one",
                  weights.size(), targets.size());
        return false;
    }
    for (const float weight : weights) {
        if (!std::isfinite(weight)) {
            UVE_ERROR("MeshAsset: a morph weight is non-finite");
            return false;
        }
    }
    std::vector<MeshVertex> accumulated(bindVertices.begin(), bindVertices.end());
    for (std::size_t targetIndex = 0U; targetIndex < targets.size(); ++targetIndex) {
        // Exactly zero is the common case on a large rig - 52 shapes, three of them active - so
        // skipping is what keeps an unused shape free rather than merely cheap.
        const float weight = weights[targetIndex];
        if (weight == 0.0F) {
            continue;
        }
        for (const MeshMorphTargetDelta& delta : targets[targetIndex].deltas) {
            // Structurally invalid input (an out-of-range index) must not read out of bounds.
            if (delta.vertexIndex >= accumulated.size()) {
                UVE_ERROR("MeshAsset: morph target {} moves vertex {} but the mesh has {}",
                          targetIndex, delta.vertexIndex, accumulated.size());
                return false;
            }
            MeshVertex& vertex = accumulated[delta.vertexIndex];
            vertex.position += delta.positionDelta * weight;
            vertex.normal += delta.normalDelta * weight;
            vertex.tangent += delta.tangentDelta * weight;
            if (!Math::IsFinite(vertex.position) || !Math::IsFinite(vertex.normal) ||
                !Math::IsFinite(vertex.tangent)) {
                UVE_ERROR("MeshAsset: morph target {} overflowed vertex {} into a non-finite value",
                          targetIndex, delta.vertexIndex);
                return false;
            }
        }
    }
    outVertices = std::move(accumulated);
    return true;
}

bool LoadMeshAsset(const std::filesystem::path& path, MeshAsset& outMesh) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false; // ReadUveFile already logged the specific reason.
    }
    if (file->first.assetType != AssetKind::Mesh) {
        UVE_ERROR("MeshAsset: \"{}\" is not a mesh file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payload = file->second;
    std::size_t offset = 0;

    std::uint32_t vertexCount = 0;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, vertexCount)) {
        UVE_ERROR("MeshAsset: \"{}\" has a truncated vertex count", path.string());
        return false;
    }
    constexpr std::size_t kSerializedMeshVertexBytes = sizeof(float) * 8U;
    constexpr std::size_t kSerializedMeshTailBytes = sizeof(std::uint32_t) + sizeof(float) * 6U;
    if (payload.size() < offset || payload.size() - offset < kSerializedMeshTailBytes ||
        vertexCount > (payload.size() - offset - kSerializedMeshTailBytes) /
                          kSerializedMeshVertexBytes) {
        UVE_ERROR("MeshAsset: \"{}\" has an impossible vertex count", path.string());
        return false;
    }

    std::vector<MeshVertex> vertices;
    vertices.reserve(vertexCount);
    for (std::uint32_t index = 0; index < vertexCount; ++index) {
        MeshVertex vertex;
        if (!ReadVector3FromBuffer(payload, offset, vertex.position) ||
            !ReadVector3FromBuffer(payload, offset, vertex.normal) ||
            !Utilities::ReadFloatLeFromBuffer(payload, offset, vertex.u) || !Utilities::ReadFloatLeFromBuffer(payload, offset, vertex.v)) {
            UVE_ERROR("MeshAsset: \"{}\" has truncated vertex data", path.string());
            return false;
        }
        vertices.push_back(vertex);
    }

    std::uint32_t indexCount = 0;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, indexCount)) {
        UVE_ERROR("MeshAsset: \"{}\" has a truncated index count", path.string());
        return false;
    }
    if (payload.size() < offset || indexCount > (payload.size() - offset) / sizeof(std::uint32_t)) {
        UVE_ERROR("MeshAsset: \"{}\" has an impossible index count", path.string());
        return false;
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index) {
        std::uint32_t value = 0;
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, value)) {
            UVE_ERROR("MeshAsset: \"{}\" has truncated index data", path.string());
            return false;
        }
        if (value >= vertices.size()) {
            UVE_ERROR("MeshAsset: \"{}\" has an out-of-bounds index {} (only {} vertices)", path.string(), value,
                       vertices.size());
            return false;
        }
        indices.push_back(value);
    }

    Math::Aabb localBounds;
    if (!ReadVector3FromBuffer(payload, offset, localBounds.min) ||
        !ReadVector3FromBuffer(payload, offset, localBounds.max)) {
        UVE_ERROR("MeshAsset: \"{}\" has a truncated local bounds", path.string());
        return false;
    }

    // The skinning section is OPTIONAL and trails the bounds, which is what keeps every existing
    // .uvmodel loadable byte for byte. The envelope's version field is global across every asset
    // kind, so bumping it to advertise skinning would invalidate scenes, materials and textures
    // that have nothing to do with meshes; the payload's own natural end is the honest place to
    // detect "this file predates skinning". A file that stops here is a static mesh, not an error.
    std::vector<MeshSkinningInfluence> skinningInfluences;
    std::vector<MeshJoint> joints;
    if (offset < payload.size() && !PeeksTrailingSection(payload, offset)) {
        std::uint32_t jointCount = 0;
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, jointCount)) {
            UVE_ERROR("MeshAsset: \"{}\" has a truncated joint count", path.string());
            return false;
        }
        constexpr std::size_t kSerializedJointBytes = sizeof(std::uint32_t) + sizeof(float) * 16U;
        if (payload.size() < offset ||
            jointCount > (payload.size() - offset) / kSerializedJointBytes) {
            UVE_ERROR("MeshAsset: \"{}\" has an impossible joint count", path.string());
            return false;
        }
        joints.reserve(jointCount);
        for (std::uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            MeshJoint joint;
            if (!Utilities::ReadUint32LeFromBuffer(payload, offset, joint.parentIndex)) {
                UVE_ERROR("MeshAsset: \"{}\" has truncated joint data", path.string());
                return false;
            }
            bool matrixComplete = true;
            for (auto& row : joint.inverseBindMatrix.m) {
                for (float& value : row) {
                    matrixComplete = matrixComplete && Utilities::ReadFloatLeFromBuffer(payload, offset, value);
                }
            }
            if (!matrixComplete) {
                UVE_ERROR("MeshAsset: \"{}\" has a truncated inverse bind matrix", path.string());
                return false;
            }
            joints.push_back(joint);
        }

        // One influence per vertex, always - the count is not re-serialized because a value other
        // than vertices.size() would be unrepresentable anyway, and storing it would just create a
        // second source of truth to disagree with the first.
        skinningInfluences.reserve(vertices.size());
        for (std::size_t vertexIndex = 0U; vertexIndex < vertices.size(); ++vertexIndex) {
            MeshSkinningInfluence influence;
            bool complete = true;
            for (std::uint32_t& jointSlot : influence.joints) {
                complete = complete && Utilities::ReadUint32LeFromBuffer(payload, offset, jointSlot);
            }
            for (float& weight : influence.weights) {
                complete = complete && Utilities::ReadFloatLeFromBuffer(payload, offset, weight);
            }
            if (!complete) {
                UVE_ERROR("MeshAsset: \"{}\" has truncated skinning influences", path.string());
                return false;
            }
            skinningInfluences.push_back(influence);
        }

        if (offset < payload.size() && !PeeksTrailingSection(payload, offset)) {
            std::uint32_t nameCount = 0;
            if (!Utilities::ReadUint32LeFromBuffer(payload, offset, nameCount) || nameCount != joints.size()) {
                UVE_ERROR("MeshAsset: \"{}\" has a joint name table that does not match its joints", path.string());
                return false;
            }
            for (MeshJoint& joint : joints) {
                std::uint32_t length = 0;
                if (!Utilities::ReadUint32LeFromBuffer(payload, offset, length) ||
                    length > kMaximumMeshSectionNameBytes ||
                    payload.size() - offset < length) {
                    UVE_ERROR("MeshAsset: \"{}\" has a truncated joint name", path.string());
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
    std::vector<AssetGuid> materialGuids;
    if (offset < payload.size() && PeeksMaterialSection(payload, offset)) {
        std::uint32_t tag = 0U;
        std::uint32_t materialCount = 0U;
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, tag) ||
            !Utilities::ReadUint32LeFromBuffer(payload, offset, materialCount) ||
            materialCount > kMaximumMeshMaterialSlots ||
            materialCount > (payload.size() - offset) / sizeof(std::uint64_t)) {
            UVE_ERROR("MeshAsset: \"{}\" has a truncated or impossible material reference table", path.string());
            return false;
        }
        materialGuids.reserve(materialCount);
        for (std::uint32_t materialIndex = 0U; materialIndex < materialCount; ++materialIndex) {
            std::uint64_t guidValue = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(payload, offset, guidValue)) {
                UVE_ERROR("MeshAsset: \"{}\" has a truncated material reference", path.string());
                return false;
            }
            materialGuids.push_back(AssetGuid{guidValue});
        }
    }

    // Blend shapes trail every other section, behind their own tag, for the same reason skinning
    // and materials do: a file written before shapes existed simply ends, and a mesh without
    // shapes keeps writing the exact bytes it always did.
    std::vector<MeshMorphTarget> morphTargets;
    std::vector<float> defaultMorphWeights;
    if (offset < payload.size() && PeeksMorphSection(payload, offset)) {
        std::uint32_t tag = 0U;
        std::uint32_t targetCount = 0U;
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, tag) ||
            !Utilities::ReadUint32LeFromBuffer(payload, offset, targetCount) ||
            targetCount > kMaximumMeshMorphTargets) {
            UVE_ERROR("MeshAsset: \"{}\" has a truncated or impossible morph target count", path.string());
            return false;
        }
        // Every target costs at least its name length + delta count, so a count larger than the
        // bytes left cannot be honest. Checked before reserve() so a hostile file cannot make the
        // loader allocate gigabytes it will never fill.
        if (targetCount > (payload.size() - offset) / (sizeof(std::uint32_t) * 2U)) {
            UVE_ERROR("MeshAsset: \"{}\" claims {} morph targets with {} bytes left", path.string(),
                      targetCount, payload.size() - offset);
            return false;
        }
        morphTargets.reserve(targetCount);
        for (std::uint32_t targetIndex = 0U; targetIndex < targetCount; ++targetIndex) {
            MeshMorphTarget target;
            std::uint32_t nameLength = 0U;
            std::uint32_t deltaCount = 0U;
            if (!Utilities::ReadUint32LeFromBuffer(payload, offset, nameLength) ||
                nameLength > kMaximumMeshSectionNameBytes || payload.size() - offset < nameLength) {
                UVE_ERROR("MeshAsset: \"{}\" has a truncated morph target name", path.string());
                return false;
            }
            target.name.assign(reinterpret_cast<const char*>(payload.data() + offset), nameLength);
            offset += nameLength;
            if (!Utilities::ReadUint32LeFromBuffer(payload, offset, deltaCount) ||
                deltaCount > kMaximumMeshMorphTargetDeltas ||
                deltaCount > (payload.size() - offset) / kSerializedMorphDeltaBytes) {
                UVE_ERROR("MeshAsset: \"{}\" has a truncated or impossible morph delta count", path.string());
                return false;
            }
            target.deltas.reserve(deltaCount);
            for (std::uint32_t deltaIndex = 0U; deltaIndex < deltaCount; ++deltaIndex) {
                MeshMorphTargetDelta delta;
                if (!Utilities::ReadUint32LeFromBuffer(payload, offset, delta.vertexIndex) ||
                    !ReadVector3FromBuffer(payload, offset, delta.positionDelta) ||
                    !ReadVector3FromBuffer(payload, offset, delta.normalDelta) ||
                    !ReadVector3FromBuffer(payload, offset, delta.tangentDelta)) {
                    UVE_ERROR("MeshAsset: \"{}\" has truncated morph delta data", path.string());
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
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, weightCount) ||
            (weightCount != 0U && weightCount != targetCount)) {
            UVE_ERROR("MeshAsset: \"{}\" has an impossible default morph weight count", path.string());
            return false;
        }
        if (weightCount > 0U) {
            defaultMorphWeights.reserve(weightCount);
            for (std::uint32_t weightIndex = 0U; weightIndex < weightCount; ++weightIndex) {
                float weight = 0.0F;
                if (!Utilities::ReadFloatLeFromBuffer(payload, offset, weight)) {
                    UVE_ERROR("MeshAsset: \"{}\" has truncated default morph weights", path.string());
                    return false;
                }
                defaultMorphWeights.push_back(weight);
            }
        }
    }

    // Surfaces trail every other section, behind their own tag, for the same reason skinning,
    // materials and shapes do: an asset written before surfaces existed simply ends, and a mesh
    // without surfaces keeps writing the exact bytes it always did.
    std::vector<MeshSurface> surfaces;
    if (offset < payload.size() && PeeksSurfaceSection(payload, offset)) {
        std::uint32_t tag = 0U;
        std::uint32_t surfaceCount = 0U;
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, tag) ||
            !Utilities::ReadUint32LeFromBuffer(payload, offset, surfaceCount) || surfaceCount == 0U ||
            surfaceCount > kMaximumMeshSurfaces ||
            surfaceCount > (payload.size() - offset) / kSerializedMeshSurfaceBytes) {
            UVE_ERROR("MeshAsset: \"{}\" has a truncated or impossible surface table", path.string());
            return false;
        }
        surfaces.reserve(surfaceCount);
        for (std::uint32_t surfaceIndex = 0U; surfaceIndex < surfaceCount; ++surfaceIndex) {
            MeshSurface surface;
            if (!Utilities::ReadUint32LeFromBuffer(payload, offset, surface.firstIndex) ||
                !Utilities::ReadUint32LeFromBuffer(payload, offset, surface.indexCount) ||
                !Utilities::ReadUint32LeFromBuffer(payload, offset, surface.materialSlot)) {
                UVE_ERROR("MeshAsset: \"{}\" has truncated surface data", path.string());
                return false;
            }
            surfaces.push_back(surface);
        }
    }

    if (!TryGenerateMeshTangents(vertices, indices)) {
        UVE_ERROR("MeshAsset: \"{}\" has non-finite generated tangent data", path.string());
        return false;
    }
    // Validate before publishing, and validate on the CANDIDATE rather than on outMesh - the
    // loader's existing contract is that a rejected file leaves the caller's mesh untouched.
    MeshAsset candidate;
    candidate.vertices = std::move(vertices);
    candidate.indices = std::move(indices);
    candidate.localBounds = localBounds;
    candidate.skinningInfluences = std::move(skinningInfluences);
    candidate.joints = std::move(joints);
    candidate.materialGuids = std::move(materialGuids);
    candidate.surfaces = std::move(surfaces);
    candidate.morphTargets = std::move(morphTargets);
    candidate.defaultMorphWeights = std::move(defaultMorphWeights);
    if (!IsMeshSkinningDataValid(candidate)) {
        UVE_ERROR("MeshAsset: \"{}\" has structurally invalid skinning data", path.string());
        return false;
    }
    if (!IsMeshMorphTargetDataValid(candidate)) {
        UVE_ERROR("MeshAsset: \"{}\" has structurally invalid morph target data", path.string());
        return false;
    }
    if (!IsMeshSurfaceDataValid(candidate)) {
        UVE_ERROR("MeshAsset: \"{}\" has structurally invalid surface data", path.string());
        return false;
    }
    outMesh = std::move(candidate);
    return true;
}

bool SaveMeshAsset(const MeshAsset& mesh, const std::filesystem::path& path) {
    std::vector<std::byte> payload;
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.vertices.size()));
    for (const MeshVertex& vertex : mesh.vertices) {
        AppendVector3(payload, vertex.position);
        AppendVector3(payload, vertex.normal);
        Utilities::AppendFloatLe(payload, vertex.u);
        Utilities::AppendFloatLe(payload, vertex.v);
    }

    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.indices.size()));
    for (std::uint32_t index : mesh.indices) {
        Utilities::AppendUint32Le(payload, index);
    }

    AppendVector3(payload, mesh.localBounds.min);
    AppendVector3(payload, mesh.localBounds.max);

    // A static mesh writes nothing further, so its bytes stay identical to what previous versions
    // of this engine produced - which is the property that makes the optional trailing section
    // safe in both directions, not just on read.
    if (mesh.IsSkinned()) {
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.joints.size()));
        for (const MeshJoint& joint : mesh.joints) {
            Utilities::AppendUint32Le(payload, joint.parentIndex);
            for (const auto& row : joint.inverseBindMatrix.m) {
                for (const float value : row) {
                    Utilities::AppendFloatLe(payload, value);
                }
            }
        }
        for (const MeshSkinningInfluence& influence : mesh.skinningInfluences) {
            for (const std::uint32_t joint : influence.joints) {
                Utilities::AppendUint32Le(payload, joint);
            }
            for (const float weight : influence.weights) {
                Utilities::AppendFloatLe(payload, weight);
            }
        }
        // Joint names trail the influences for the same reason the skin trails the bounds: a file
        // from before they were kept simply ends here.
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.joints.size()));
        for (const MeshJoint& joint : mesh.joints) {
            Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(joint.name.size()));
            const auto* const bytes = reinterpret_cast<const std::byte*>(joint.name.data());
            payload.insert(payload.end(), bytes, bytes + joint.name.size());
        }
    }

    // WP7: material references, tagged, and only when there are any - a mesh without materials
    // keeps writing the exact bytes it always did.
    if (!mesh.materialGuids.empty()) {
        if (mesh.materialGuids.size() > kMaximumMeshMaterialSlots) {
            UVE_ERROR("MeshAsset: \"{}\" references {} materials; the format caps slots at {}", path.string(),
                      mesh.materialGuids.size(), kMaximumMeshMaterialSlots);
            return false;
        }
        Utilities::AppendUint32Le(payload, kMeshMaterialSectionTag);
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.materialGuids.size()));
        for (const AssetGuid guid : mesh.materialGuids) {
            Utilities::AppendUint64Le(payload, guid.value);
        }
    }

    // Blend shapes, tagged and last, written only when the mesh has any - a mesh without shapes
    // keeps producing the exact bytes every previous version of this engine produced.
    if (!mesh.morphTargets.empty()) {
        if (mesh.morphTargets.size() > kMaximumMeshMorphTargets) {
            UVE_ERROR("MeshAsset: \"{}\" carries {} morph targets; the format caps them at {}",
                      path.string(), mesh.morphTargets.size(), kMaximumMeshMorphTargets);
            return false;
        }
        if (!mesh.defaultMorphWeights.empty() && mesh.defaultMorphWeights.size() != mesh.morphTargets.size()) {
            UVE_ERROR("MeshAsset: \"{}\" has {} default morph weights for {} targets", path.string(),
                      mesh.defaultMorphWeights.size(), mesh.morphTargets.size());
            return false;
        }
        Utilities::AppendUint32Le(payload, kMeshMorphSectionTag);
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.morphTargets.size()));
        for (const MeshMorphTarget& target : mesh.morphTargets) {
            if (target.name.size() > kMaximumMeshSectionNameBytes) {
                UVE_ERROR("MeshAsset: \"{}\" has a morph target name of {} bytes; the format caps "
                          "names at {}", path.string(), target.name.size(), kMaximumMeshSectionNameBytes);
                return false;
            }
            Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(target.name.size()));
            const auto* const bytes = reinterpret_cast<const std::byte*>(target.name.data());
            payload.insert(payload.end(), bytes, bytes + target.name.size());
            if (target.deltas.size() > kMaximumMeshMorphTargetDeltas) {
                UVE_ERROR("MeshAsset: \"{}\" has a morph target with {} deltas; the format caps them "
                          "at {}", path.string(), target.deltas.size(), kMaximumMeshMorphTargetDeltas);
                return false;
            }
            Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(target.deltas.size()));
            for (const MeshMorphTargetDelta& delta : target.deltas) {
                Utilities::AppendUint32Le(payload, delta.vertexIndex);
                AppendVector3(payload, delta.positionDelta);
                AppendVector3(payload, delta.normalDelta);
                AppendVector3(payload, delta.tangentDelta);
            }
        }
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.defaultMorphWeights.size()));
        for (const float weight : mesh.defaultMorphWeights) {
            Utilities::AppendFloatLe(payload, weight);
        }
    }

    // Surfaces, tagged and last, written only when the mesh has more than the implicit single
    // one. A single-material mesh therefore keeps producing the exact bytes it always did.
    if (!mesh.surfaces.empty()) {
        if (!IsMeshSurfaceDataValid(mesh)) {
            UVE_ERROR("MeshAsset: \"{}\" has invalid surface data; refusing to write it", path.string());
            return false;
        }
        Utilities::AppendUint32Le(payload, kMeshSurfaceSectionTag);
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(mesh.surfaces.size()));
        for (const MeshSurface& surface : mesh.surfaces) {
            Utilities::AppendUint32Le(payload, surface.firstIndex);
            Utilities::AppendUint32Le(payload, surface.indexCount);
            Utilities::AppendUint32Le(payload, surface.materialSlot);
        }
    }

    // The model's own takes stay with it when its mesh is written again (a retarget, a re-save).
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) {
            if (const auto previous = ReadUveFile(path); previous.has_value() && previous->first.assetType == AssetKind::Mesh) {
                const std::vector<std::byte> takes = ExtractModelAnimationSection(previous->second);
                if (!takes.empty()) {
                    // The footer points at where the section starts: here, in the new payload.
                    const std::size_t sectionAt = payload.size();
                    payload.insert(payload.end(), takes.begin(), takes.end() - 12);
                    std::uint64_t at = sectionAt;
                    std::byte footer[12];
                    std::memcpy(footer, &at, 8);
                    const std::uint32_t tag = kModelAnimationSectionTag;
                    std::memcpy(footer + 8, &tag, 4);
                    payload.insert(payload.end(), footer, footer + 12);
                }
            }
        }
    }
    return WriteUveFile(path, AssetKind::Mesh, payload);
}

} // namespace UVE::Asset
