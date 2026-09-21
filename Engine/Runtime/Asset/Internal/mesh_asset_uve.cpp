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

[[nodiscard]] bool ReadVector3FromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             Math::Vector3UVE& outValue) {
    return Utilities::ReadFloatFromBufferUVE(buffer, offset, outValue.x) &&
           Utilities::ReadFloatFromBufferUVE(buffer, offset, outValue.y) &&
           Utilities::ReadFloatFromBufferUVE(buffer, offset, outValue.z);
}

void AppendVector3UVE(std::vector<std::byte>& buffer, const Math::Vector3UVE& value) {
    Utilities::AppendFloatUVE(buffer, value.x);
    Utilities::AppendFloatUVE(buffer, value.y);
    Utilities::AppendFloatUVE(buffer, value.z);
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
    if (!Utilities::ReadUint32FromBufferUVE(payload, offset, vertexCount)) {
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
            !Utilities::ReadFloatFromBufferUVE(payload, offset, vertex.u) || !Utilities::ReadFloatFromBufferUVE(payload, offset, vertex.v)) {
            UVE_ERROR("MeshAssetUVE: \"{}\" has truncated vertex data", path.string());
            return false;
        }
        vertices.push_back(vertex);
    }

    std::uint32_t indexCount = 0;
    if (!Utilities::ReadUint32FromBufferUVE(payload, offset, indexCount)) {
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
        if (!Utilities::ReadUint32FromBufferUVE(payload, offset, value)) {
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
    // .uvemodel loadable byte for byte. The envelope's version field is global across every asset
    // kind, so bumping it to advertise skinning would invalidate scenes, materials and textures
    // that have nothing to do with meshes; the payload's own natural end is the honest place to
    // detect "this file predates skinning". A file that stops here is a static mesh, not an error.
    std::vector<MeshSkinningInfluenceUVE> skinningInfluences;
    std::vector<MeshJointUVE> joints;
    if (offset < payload.size()) {
        std::uint32_t jointCount = 0;
        if (!Utilities::ReadUint32FromBufferUVE(payload, offset, jointCount)) {
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
            if (!Utilities::ReadUint32FromBufferUVE(payload, offset, joint.parentIndex)) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has truncated joint data", path.string());
                return false;
            }
            bool matrixComplete = true;
            for (auto& row : joint.inverseBindMatrix.m) {
                for (float& value : row) {
                    matrixComplete = matrixComplete && Utilities::ReadFloatFromBufferUVE(payload, offset, value);
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
                complete = complete && Utilities::ReadUint32FromBufferUVE(payload, offset, jointSlot);
            }
            for (float& weight : influence.weights) {
                complete = complete && Utilities::ReadFloatFromBufferUVE(payload, offset, weight);
            }
            if (!complete) {
                UVE_ERROR("MeshAssetUVE: \"{}\" has truncated skinning influences", path.string());
                return false;
            }
            skinningInfluences.push_back(influence);
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
    if (!IsMeshSkinningDataValidUVE(candidate)) {
        UVE_ERROR("MeshAssetUVE: \"{}\" has structurally invalid skinning data", path.string());
        return false;
    }
    outMesh = std::move(candidate);
    return true;
}

bool SaveMeshAssetUVE(const MeshAssetUVE& mesh, const std::filesystem::path& path) {
    std::vector<std::byte> payload;
    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(mesh.vertices.size()));
    for (const MeshVertexUVE& vertex : mesh.vertices) {
        AppendVector3UVE(payload, vertex.position);
        AppendVector3UVE(payload, vertex.normal);
        Utilities::AppendFloatUVE(payload, vertex.u);
        Utilities::AppendFloatUVE(payload, vertex.v);
    }

    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(mesh.indices.size()));
    for (std::uint32_t index : mesh.indices) {
        Utilities::AppendUint32UVE(payload, index);
    }

    AppendVector3UVE(payload, mesh.localBounds.min);
    AppendVector3UVE(payload, mesh.localBounds.max);

    // A static mesh writes nothing further, so its bytes stay identical to what previous versions
    // of this engine produced - which is the property that makes the optional trailing section
    // safe in both directions, not just on read.
    if (mesh.IsSkinnedUVE()) {
        Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(mesh.joints.size()));
        for (const MeshJointUVE& joint : mesh.joints) {
            Utilities::AppendUint32UVE(payload, joint.parentIndex);
            for (const auto& row : joint.inverseBindMatrix.m) {
                for (const float value : row) {
                    Utilities::AppendFloatUVE(payload, value);
                }
            }
        }
        for (const MeshSkinningInfluenceUVE& influence : mesh.skinningInfluences) {
            for (const std::uint32_t joint : influence.joints) {
                Utilities::AppendUint32UVE(payload, joint);
            }
            for (const float weight : influence.weights) {
                Utilities::AppendFloatUVE(payload, weight);
            }
        }
    }

    return WriteUveFileUVE(path, AssetKindUVE::Mesh, payload);
}

} // namespace UVE::Asset
