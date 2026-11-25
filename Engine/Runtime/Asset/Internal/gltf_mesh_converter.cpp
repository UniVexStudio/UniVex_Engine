// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gltf_mesh_converter.h"

#include "gltf_document.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

#include "vector3.h"

namespace UVE::Asset {
namespace {

constexpr std::uint64_t kMaximumPrimitiveIndices = kMaximumGltfAccessorElements * 3U;
constexpr float kMinimumNormalLengthSquared = 1.0e-12F;

[[nodiscard]] UVE::Math::Vector3 ReadVector3(const GltfAccessorView& accessor,
                                                   const std::size_t index) noexcept {
    const std::byte* const bytes = Detail::GltfAccessorElement(accessor, index, 12U);
    return UVE::Math::Vector3{Detail::ReadGltfFloatLE(bytes), Detail::ReadGltfFloatLE(bytes + 4U),
                                 Detail::ReadGltfFloatLE(bytes + 8U)};
}

[[nodiscard]] std::uint32_t ReadIndex(const GltfAccessorView& accessor,
                                         const std::size_t index) noexcept {
    const std::byte* const bytes = Detail::GltfAccessorElement(
        accessor, index, Detail::GltfComponentSize(accessor.componentType));
    switch (accessor.componentType) {
    case GltfComponentType::UnsignedByte:
        return std::to_integer<std::uint8_t>(bytes[0]);
    case GltfComponentType::UnsignedShort:
        return Detail::ReadGltfU16LE(bytes);
    case GltfComponentType::UnsignedInt:
        return Detail::ReadGltfU32LE(bytes);
    case GltfComponentType::Float:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] bool HasUsableNormalLength(const UVE::Math::Vector3& value) noexcept {
    const float lengthSquared = UVE::Math::LengthSquared(value);
    return std::isfinite(lengthSquared) && lengthSquared > kMinimumNormalLengthSquared;
}

[[nodiscard]] UVE::Math::Vector3 NormalizeOrFallback(const UVE::Math::Vector3& value) noexcept {
    if (!HasUsableNormalLength(value)) {
        return UVE::Math::Vector3{0.0F, 1.0F, 0.0F};
    }
    return UVE::Math::Normalize(value);
}

/// Every displacement stream of one morph target shares the base mesh's vertex count; glTF says so
/// and an exporter that disagrees has produced data no eval pass can index safely.
[[nodiscard]] bool ValidateMorphStream(const GltfAccessorView& stream,
                                          const std::uint64_t vertexCount,
                                          const std::uint64_t elementSize) noexcept {
    return stream.componentType == GltfComponentType::Float && stream.elementCount == vertexCount &&
           Detail::ValidateGltfAccessorView(stream, elementSize, kMaximumGltfAccessorElements);
}

[[nodiscard]] bool IsZeroVector(const UVE::Math::Vector3& value) noexcept {
    return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

/// glTF carries morph target names out-of-band in `mesh.extras.targetNames`. A missing or short
/// array is not an error - it just means those targets need the deterministic fallback name.
[[nodiscard]] std::string MorphTargetName(const std::vector<std::string>& names, const std::size_t index) {
    if (index < names.size() && !names[index].empty()) {
        return names[index];
    }
    return "target" + std::to_string(index);
}

} // namespace

namespace {

[[nodiscard]] bool ConvertGltfPrimitiveInto(const GltfPrimitiveSource& source,
                                              const GltfMorphTargetSet* const morphTargets,
                                              MeshAsset& outMesh) {
    try {
        if (source.mode != 4U || source.positions.componentType != GltfComponentType::Float ||
        source.positions.elementCount == 0U || source.positions.elementCount > kMaximumGltfAccessorElements ||
        !Detail::ValidateGltfAccessorView(source.positions, 12U, kMaximumGltfAccessorElements)) {
        return false;
    }

    const std::size_t vertexCount = static_cast<std::size_t>(source.positions.elementCount);
    if (source.normals.has_value()) {
        if (source.normals->componentType != GltfComponentType::Float ||
            source.normals->elementCount != source.positions.elementCount ||
            !Detail::ValidateGltfAccessorView(*source.normals, 12U, kMaximumGltfAccessorElements)) {
            return false;
        }
    }
    if (source.texcoords0.has_value()) {
        if (source.texcoords0->componentType != GltfComponentType::Float ||
            source.texcoords0->elementCount != source.positions.elementCount ||
            !Detail::ValidateGltfAccessorView(*source.texcoords0, 8U, kMaximumGltfAccessorElements)) {
            return false;
        }
    }

    const std::uint64_t indexCount = source.indices.has_value() ? source.indices->elementCount
                                                                  : source.positions.elementCount;
    if (indexCount == 0U || indexCount % 3U != 0U || indexCount > kMaximumPrimitiveIndices) {
        return false;
    }
    if (source.indices.has_value()) {
        const std::uint64_t indexElementSize = Detail::GltfComponentSize(source.indices->componentType);
        if ((source.indices->componentType != GltfComponentType::UnsignedByte &&
             source.indices->componentType != GltfComponentType::UnsignedShort &&
             source.indices->componentType != GltfComponentType::UnsignedInt) ||
            !Detail::ValidateGltfAccessorView(*source.indices, indexElementSize, kMaximumPrimitiveIndices)) {
            return false;
        }
    }

    MeshAsset candidate;
    candidate.vertices.resize(vertexCount);
    candidate.indices.resize(static_cast<std::size_t>(indexCount));
    bool hasBounds = false;
    for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex) {
        const UVE::Math::Vector3 position = ReadVector3(source.positions, vertexIndex);
        if (!Math::IsFinite(position)) {
            return false;
        }
        candidate.vertices[vertexIndex].position = position;
        if (source.normals.has_value()) {
            const UVE::Math::Vector3 normal = ReadVector3(*source.normals, vertexIndex);
            if (!Math::IsFinite(normal) || !HasUsableNormalLength(normal)) {
                return false;
            }
            candidate.vertices[vertexIndex].normal = UVE::Math::Normalize(normal);
        }
        if (source.texcoords0.has_value()) {
            const std::byte* const bytes = Detail::GltfAccessorElement(*source.texcoords0, vertexIndex, 8U);
            candidate.vertices[vertexIndex].u = Detail::ReadGltfFloatLE(bytes);
            candidate.vertices[vertexIndex].v = Detail::ReadGltfFloatLE(bytes + 4U);
            if (!std::isfinite(candidate.vertices[vertexIndex].u) ||
                !std::isfinite(candidate.vertices[vertexIndex].v)) {
                return false;
            }
        }
        if (!hasBounds) {
            candidate.localBounds.min = position;
            candidate.localBounds.max = position;
            hasBounds = true;
        } else {
            candidate.localBounds.min.x = std::min(candidate.localBounds.min.x, position.x);
            candidate.localBounds.min.y = std::min(candidate.localBounds.min.y, position.y);
            candidate.localBounds.min.z = std::min(candidate.localBounds.min.z, position.z);
            candidate.localBounds.max.x = std::max(candidate.localBounds.max.x, position.x);
            candidate.localBounds.max.y = std::max(candidate.localBounds.max.y, position.y);
            candidate.localBounds.max.z = std::max(candidate.localBounds.max.z, position.z);
        }
    }

    for (std::size_t index = 0U; index < candidate.indices.size(); ++index) {
        const std::uint32_t value = source.indices.has_value()
                                        ? ReadIndex(*source.indices, index)
                                        : static_cast<std::uint32_t>(index);
        if (value >= vertexCount) {
            return false;
        }
        candidate.indices[index] = value;
    }

    std::vector<UVE::Math::Vector3> normalAccumulation;
    if (!source.normals.has_value()) {
        normalAccumulation.resize(vertexCount);
        for (std::size_t triangle = 0U; triangle < candidate.indices.size(); triangle += 3U) {
            const auto& a = candidate.vertices[candidate.indices[triangle]].position;
            const auto& b = candidate.vertices[candidate.indices[triangle + 1U]].position;
            const auto& c = candidate.vertices[candidate.indices[triangle + 2U]].position;
            const UVE::Math::Vector3 faceNormal = UVE::Math::Cross(b - a, c - a);
            if (!Math::IsFinite(faceNormal) || !HasUsableNormalLength(faceNormal)) {
                return false;
            }
            normalAccumulation[candidate.indices[triangle]] += faceNormal;
            if (!Math::IsFinite(normalAccumulation[candidate.indices[triangle]])) {
                return false;
            }
            normalAccumulation[candidate.indices[triangle + 1U]] += faceNormal;
            if (!Math::IsFinite(normalAccumulation[candidate.indices[triangle + 1U]])) {
                return false;
            }
            normalAccumulation[candidate.indices[triangle + 2U]] += faceNormal;
            if (!Math::IsFinite(normalAccumulation[candidate.indices[triangle + 2U]])) {
                return false;
            }
        }
        for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex) {
            candidate.vertices[vertexIndex].normal = NormalizeOrFallback(normalAccumulation[vertexIndex]);
        }
    }

    if (!TryGenerateMeshTangents(candidate.vertices, candidate.indices)) {
        return false;
    }

    // Morph targets, in declaration order - that order IS the weight index space, so it is never
    // sorted or deduplicated here.
    if (morphTargets != nullptr && morphTargets->targets.size() > kMaximumMeshMorphTargets) {
        return false;
    }
    const std::size_t targetCount = morphTargets == nullptr ? 0U : morphTargets->targets.size();
    candidate.morphTargets.reserve(targetCount);
    for (std::size_t targetIndex = 0U; targetIndex < targetCount; ++targetIndex) {
        const GltfMorphTargetSource& targetSource = morphTargets->targets[targetIndex];
        if (!ValidateMorphStream(targetSource.positions, source.positions.elementCount, 12U) ||
            (targetSource.normals.has_value() &&
             !ValidateMorphStream(*targetSource.normals, source.positions.elementCount, 12U)) ||
            (targetSource.tangents.has_value() &&
             !ValidateMorphStream(*targetSource.tangents, source.positions.elementCount, 12U))) {
            return false;
        }
        MeshMorphTarget target;
        target.name = MorphTargetName(morphTargets->names, targetIndex);
        // A delta whose three streams are all exactly zero moves nothing. Exporters emit these by
        // the thousand for the vertices a shape leaves alone, and dropping them is what makes the
        // sparse representation pay for itself: it is lossless because a missing delta IS a zero
        // delta, and the accumulator skips zeroes anyway.
        for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex) {
            const UVE::Math::Vector3 positionDelta = ReadVector3(targetSource.positions, vertexIndex);
            const UVE::Math::Vector3 normalDelta =
                targetSource.normals.has_value() ? ReadVector3(*targetSource.normals, vertexIndex)
                                                 : UVE::Math::Vector3{};
            const UVE::Math::Vector3 tangentDelta =
                targetSource.tangents.has_value() ? ReadVector3(*targetSource.tangents, vertexIndex)
                                                  : UVE::Math::Vector3{};
            if (!Math::IsFinite(positionDelta) || !Math::IsFinite(normalDelta) ||
                !Math::IsFinite(tangentDelta)) {
                return false;
            }
            if (IsZeroVector(positionDelta) && IsZeroVector(normalDelta) && IsZeroVector(tangentDelta)) {
                continue;
            }
            if (target.deltas.size() >= kMaximumMeshMorphTargetDeltas) {
                return false;
            }
            target.deltas.push_back(MeshMorphTargetDelta{
                static_cast<std::uint32_t>(vertexIndex), positionDelta, normalDelta, tangentDelta});
        }
        candidate.morphTargets.push_back(std::move(target));
    }
    if (morphTargets != nullptr && !morphTargets->defaultWeights.empty()) {
        if (morphTargets->defaultWeights.size() != candidate.morphTargets.size()) {
            return false;
        }
        for (const float weight : morphTargets->defaultWeights) {
            if (!std::isfinite(weight)) {
                return false;
            }
        }
        candidate.defaultMorphWeights = morphTargets->defaultWeights;
    }
    if (!IsMeshMorphTargetDataValid(candidate)) {
        return false;
    }

    outMesh = std::move(candidate);
    return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

bool ConvertGltfPrimitive(const GltfPrimitiveSource& source, MeshAsset& outMesh) {
    return ConvertGltfPrimitiveInto(source, nullptr, outMesh);
}

bool ConvertGltfPrimitive(const GltfPrimitiveSource& source, const GltfMorphTargetSet& morphTargets,
                             MeshAsset& outMesh) {
    return ConvertGltfPrimitiveInto(source, &morphTargets, outMesh);
}

} // namespace UVE::Asset
