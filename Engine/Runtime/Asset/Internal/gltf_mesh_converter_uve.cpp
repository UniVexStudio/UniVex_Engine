// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/gltf_mesh_converter_uve.h"

#include "gltf_document_uve.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

#include "uve/math/vector3_uve.h"

namespace UVE::Asset {
namespace {

constexpr std::uint64_t kMaximumPrimitiveIndicesUVE = kMaximumGltfAccessorElementsUVE * 3U;
constexpr float kMinimumNormalLengthSquaredUVE = 1.0e-12F;

[[nodiscard]] UVE::Math::Vector3UVE ReadVector3UVE(const GltfAccessorViewUVE& accessor,
                                                   const std::size_t index) noexcept {
    const std::byte* const bytes = Detail::GltfAccessorElementUVE(accessor, index, 12U);
    return UVE::Math::Vector3UVE{Detail::ReadGltfFloatLEUVE(bytes), Detail::ReadGltfFloatLEUVE(bytes + 4U),
                                 Detail::ReadGltfFloatLEUVE(bytes + 8U)};
}

[[nodiscard]] std::uint32_t ReadIndexUVE(const GltfAccessorViewUVE& accessor,
                                         const std::size_t index) noexcept {
    const std::byte* const bytes = Detail::GltfAccessorElementUVE(
        accessor, index, Detail::GltfComponentSizeUVE(accessor.componentType));
    switch (accessor.componentType) {
    case GltfComponentTypeUVE::UnsignedByte:
        return std::to_integer<std::uint8_t>(bytes[0]);
    case GltfComponentTypeUVE::UnsignedShort:
        return Detail::ReadGltfU16LEUVE(bytes);
    case GltfComponentTypeUVE::UnsignedInt:
        return Detail::ReadGltfU32LEUVE(bytes);
    case GltfComponentTypeUVE::Float:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] bool HasUsableNormalLengthUVE(const UVE::Math::Vector3UVE& value) noexcept {
    const float lengthSquared = UVE::Math::LengthSquaredUVE(value);
    return std::isfinite(lengthSquared) && lengthSquared > kMinimumNormalLengthSquaredUVE;
}

[[nodiscard]] UVE::Math::Vector3UVE NormalizeOrFallbackUVE(const UVE::Math::Vector3UVE& value) noexcept {
    if (!HasUsableNormalLengthUVE(value)) {
        return UVE::Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    }
    return UVE::Math::NormalizeUVE(value);
}

} // namespace

bool ConvertGltfPrimitiveUVE(const GltfPrimitiveSourceUVE& source, MeshAssetUVE& outMesh) {
    try {
        if (source.mode != 4U || source.positions.componentType != GltfComponentTypeUVE::Float ||
        source.positions.elementCount == 0U || source.positions.elementCount > kMaximumGltfAccessorElementsUVE ||
        !Detail::ValidateGltfAccessorViewUVE(source.positions, 12U, kMaximumGltfAccessorElementsUVE)) {
        return false;
    }

    const std::size_t vertexCount = static_cast<std::size_t>(source.positions.elementCount);
    if (source.normals.has_value()) {
        if (source.normals->componentType != GltfComponentTypeUVE::Float ||
            source.normals->elementCount != source.positions.elementCount ||
            !Detail::ValidateGltfAccessorViewUVE(*source.normals, 12U, kMaximumGltfAccessorElementsUVE)) {
            return false;
        }
    }
    if (source.texcoords0.has_value()) {
        if (source.texcoords0->componentType != GltfComponentTypeUVE::Float ||
            source.texcoords0->elementCount != source.positions.elementCount ||
            !Detail::ValidateGltfAccessorViewUVE(*source.texcoords0, 8U, kMaximumGltfAccessorElementsUVE)) {
            return false;
        }
    }

    const std::uint64_t indexCount = source.indices.has_value() ? source.indices->elementCount
                                                                  : source.positions.elementCount;
    if (indexCount == 0U || indexCount % 3U != 0U || indexCount > kMaximumPrimitiveIndicesUVE) {
        return false;
    }
    if (source.indices.has_value()) {
        const std::uint64_t indexElementSize = Detail::GltfComponentSizeUVE(source.indices->componentType);
        if ((source.indices->componentType != GltfComponentTypeUVE::UnsignedByte &&
             source.indices->componentType != GltfComponentTypeUVE::UnsignedShort &&
             source.indices->componentType != GltfComponentTypeUVE::UnsignedInt) ||
            !Detail::ValidateGltfAccessorViewUVE(*source.indices, indexElementSize, kMaximumPrimitiveIndicesUVE)) {
            return false;
        }
    }

    MeshAssetUVE candidate;
    candidate.vertices.resize(vertexCount);
    candidate.indices.resize(static_cast<std::size_t>(indexCount));
    bool hasBounds = false;
    for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex) {
        const UVE::Math::Vector3UVE position = ReadVector3UVE(source.positions, vertexIndex);
        if (!Math::IsFiniteUVE(position)) {
            return false;
        }
        candidate.vertices[vertexIndex].position = position;
        if (source.normals.has_value()) {
            const UVE::Math::Vector3UVE normal = ReadVector3UVE(*source.normals, vertexIndex);
            if (!Math::IsFiniteUVE(normal) || !HasUsableNormalLengthUVE(normal)) {
                return false;
            }
            candidate.vertices[vertexIndex].normal = UVE::Math::NormalizeUVE(normal);
        }
        if (source.texcoords0.has_value()) {
            const std::byte* const bytes = Detail::GltfAccessorElementUVE(*source.texcoords0, vertexIndex, 8U);
            candidate.vertices[vertexIndex].u = Detail::ReadGltfFloatLEUVE(bytes);
            candidate.vertices[vertexIndex].v = Detail::ReadGltfFloatLEUVE(bytes + 4U);
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
                                        ? ReadIndexUVE(*source.indices, index)
                                        : static_cast<std::uint32_t>(index);
        if (value >= vertexCount) {
            return false;
        }
        candidate.indices[index] = value;
    }

    std::vector<UVE::Math::Vector3UVE> normalAccumulation;
    if (!source.normals.has_value()) {
        normalAccumulation.resize(vertexCount);
        for (std::size_t triangle = 0U; triangle < candidate.indices.size(); triangle += 3U) {
            const auto& a = candidate.vertices[candidate.indices[triangle]].position;
            const auto& b = candidate.vertices[candidate.indices[triangle + 1U]].position;
            const auto& c = candidate.vertices[candidate.indices[triangle + 2U]].position;
            const UVE::Math::Vector3UVE faceNormal = UVE::Math::CrossUVE(b - a, c - a);
            if (!Math::IsFiniteUVE(faceNormal) || !HasUsableNormalLengthUVE(faceNormal)) {
                return false;
            }
            normalAccumulation[candidate.indices[triangle]] += faceNormal;
            if (!Math::IsFiniteUVE(normalAccumulation[candidate.indices[triangle]])) {
                return false;
            }
            normalAccumulation[candidate.indices[triangle + 1U]] += faceNormal;
            if (!Math::IsFiniteUVE(normalAccumulation[candidate.indices[triangle + 1U]])) {
                return false;
            }
            normalAccumulation[candidate.indices[triangle + 2U]] += faceNormal;
            if (!Math::IsFiniteUVE(normalAccumulation[candidate.indices[triangle + 2U]])) {
                return false;
            }
        }
        for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex) {
            candidate.vertices[vertexIndex].normal = NormalizeOrFallbackUVE(normalAccumulation[vertexIndex]);
        }
    }

    if (!TryGenerateMeshTangentsUVE(candidate.vertices, candidate.indices)) {
        return false;
    }
    outMesh = std::move(candidate);
    return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace UVE::Asset
