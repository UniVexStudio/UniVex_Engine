// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Bounded glTF JSON-document machinery shared by the mesh importer and the skeletal
// converters: bufferView/accessor definition readers plus the overflow-safe accessor-view
// builder. Moved verbatim out of gltf_importer_uve.cpp's anonymous namespace when the skin and
// animation converters needed the same readers — the audit that produced import_helpers_uve.h
// taught us not to grow a second copy of bounded-parse code. Behavior is identical; only VEC4
// and MAT4 element sizes are new (JOINTS_0/WEIGHTS_0 are VEC4, inverse bind matrices are MAT4).

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/gltf_mesh_converter_uve.h"
#include "uve/asset/gltf_metadata_uve.h"

namespace UVE::Asset::Detail {

struct AccessorDefinitionUVE final {
    std::uint64_t bufferView = 0U;
    std::uint64_t byteOffset = 0U;
    std::uint64_t count = 0U;
    std::uint32_t componentType = 0U;
    std::string type;
};

struct BufferViewDefinitionUVE final {
    std::uint64_t buffer = 0U;
    std::uint64_t byteOffset = 0U;
    std::uint64_t byteLength = 0U;
    std::uint64_t byteStride = 0U;
};

[[nodiscard]] inline std::optional<std::uint64_t> ReadJsonUintUVE(const nlohmann::json& object,
                                                                  const char* key,
                                                                  const bool required) {
    if (!object.contains(key)) {
        return required ? std::nullopt : std::optional<std::uint64_t>{0U};
    }
    const auto& value = object.at(key);
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
        return static_cast<std::uint64_t>(value.get<std::int64_t>());
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<std::uint32_t> ReadJsonU32UVE(const nlohmann::json& object, const char* key) {
    const auto value = ReadJsonUintUVE(object, key, true);
    if (!value.has_value() || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

[[nodiscard]] inline std::optional<nlohmann::json::size_type> CheckedJsonArrayIndexUVE(
    const nlohmann::json& array, const std::uint64_t index) {
    if (!array.is_array() || index > static_cast<std::uint64_t>(std::numeric_limits<nlohmann::json::size_type>::max()) ||
        index >= array.size()) {
        return std::nullopt;
    }
    return static_cast<nlohmann::json::size_type>(index);
}

[[nodiscard]] inline std::optional<BufferViewDefinitionUVE> ReadBufferViewUVE(const nlohmann::json& document,
                                                                              const std::uint64_t index) {
    if (!document.contains("bufferViews")) {
        return std::nullopt;
    }
    const auto bufferViewsIndex = CheckedJsonArrayIndexUVE(document.at("bufferViews"), index);
    if (!bufferViewsIndex || !document.at("bufferViews").at(*bufferViewsIndex).is_object()) {
        return std::nullopt;
    }
    const auto& value = document.at("bufferViews").at(*bufferViewsIndex);
    const auto buffer = ReadJsonUintUVE(value, "buffer", true);
    const auto byteOffset = ReadJsonUintUVE(value, "byteOffset", false);
    const auto byteLength = ReadJsonUintUVE(value, "byteLength", true);
    const auto byteStride = ReadJsonUintUVE(value, "byteStride", false);
    if (!buffer || !byteOffset || !byteLength || !byteStride) {
        return std::nullopt;
    }
    return BufferViewDefinitionUVE{*buffer, *byteOffset, *byteLength, *byteStride};
}

[[nodiscard]] inline std::optional<AccessorDefinitionUVE> ReadAccessorUVE(const nlohmann::json& document,
                                                                           const std::uint64_t index) {
    if (!document.contains("accessors")) {
        return std::nullopt;
    }
    const auto accessorsIndex = CheckedJsonArrayIndexUVE(document.at("accessors"), index);
    if (!accessorsIndex || !document.at("accessors").at(*accessorsIndex).is_object()) {
        return std::nullopt;
    }
    const auto& value = document.at("accessors").at(*accessorsIndex);
    const auto bufferView = ReadJsonUintUVE(value, "bufferView", true);
    const auto byteOffset = ReadJsonUintUVE(value, "byteOffset", false);
    const auto count = ReadJsonUintUVE(value, "count", true);
    const auto componentType = ReadJsonU32UVE(value, "componentType");
    if (!bufferView || !byteOffset || !count || !componentType || !value.contains("type") ||
        !value.at("type").is_string() || *count > kMaximumGltfAccessorElementsUVE) {
        return std::nullopt;
    }
    return AccessorDefinitionUVE{*bufferView, *byteOffset, *count, *componentType, value.at("type").get<std::string>()};
}

[[nodiscard]] inline std::optional<GltfComponentTypeUVE> ToComponentTypeUVE(const std::uint32_t value) noexcept {
    switch (value) {
    case 5121U:
        return GltfComponentTypeUVE::UnsignedByte;
    case 5123U:
        return GltfComponentTypeUVE::UnsignedShort;
    case 5125U:
        return GltfComponentTypeUVE::UnsignedInt;
    case 5126U:
        return GltfComponentTypeUVE::Float;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] inline bool AddU64UVE(const std::uint64_t left, const std::uint64_t right,
                                    std::uint64_t& outValue) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    outValue = left + right;
    return true;
}

/// Resolves one accessor to a bounded view over `buffer`, checking the declared type, component
/// width, bufferView containment, and stride math without overflow. `allowIndices` selects integer
/// components (indices, JOINTS_0) versus float32 (everything else). Unknown `expectedType` values
/// fail closed rather than sizing as scalar.
[[nodiscard]] inline std::optional<GltfAccessorViewUVE> BuildAccessorViewUVE(
    const nlohmann::json& document, const std::vector<std::byte>& buffer, const std::uint64_t accessorIndex,
    const std::string_view expectedType, const bool allowIndices) {
    const auto accessor = ReadAccessorUVE(document, accessorIndex);
    if (!accessor || accessor->type != expectedType) {
        return std::nullopt;
    }
    const auto componentType = ToComponentTypeUVE(accessor->componentType);
    if (!componentType || (!allowIndices && *componentType != GltfComponentTypeUVE::Float) ||
        (allowIndices && *componentType == GltfComponentTypeUVE::Float)) {
        return std::nullopt;
    }
    const auto view = ReadBufferViewUVE(document, accessor->bufferView);
    if (!view || view->buffer != 0U) {
        return std::nullopt;
    }
    std::uint64_t totalOffset = 0U;
    std::uint64_t viewEnd = 0U;
    if (!AddU64UVE(view->byteOffset, view->byteLength, viewEnd) || viewEnd > buffer.size() ||
        !AddU64UVE(view->byteOffset, accessor->byteOffset, totalOffset) ||
        accessor->byteOffset > view->byteLength) {
        return std::nullopt;
    }
    const std::uint64_t componentSize = *componentType == GltfComponentTypeUVE::UnsignedByte
                                             ? 1U
                                             : *componentType == GltfComponentTypeUVE::UnsignedShort ? 2U : 4U;
    std::uint64_t elementSize = 0U;
    if (expectedType == "SCALAR") {
        elementSize = componentSize;
    } else if (expectedType == "VEC2") {
        elementSize = componentSize * 2U;
    } else if (expectedType == "VEC3") {
        elementSize = componentSize * 3U;
    } else if (expectedType == "VEC4") {
        elementSize = componentSize * 4U;
    } else if (expectedType == "MAT4") {
        elementSize = componentSize * 16U;
    } else {
        return std::nullopt;
    }
    const std::uint64_t stride = view->byteStride == 0U ? elementSize : view->byteStride;
    if (!ValidateGltfAccessorSpanUVE(view->byteLength, accessor->byteOffset, accessor->count, stride, elementSize) ||
        !ValidateGltfAccessorSpanUVE(buffer.size(), totalOffset, accessor->count, stride, elementSize)) {
        return std::nullopt;
    }
    return GltfAccessorViewUVE{std::span<const std::byte>{buffer.data(), buffer.size()}, totalOffset,
                               accessor->count, view->byteStride, *componentType};
}

// --- Accessor element readers -----------------------------------------------------------
// Bounds-checked little-endian readers over caller-built accessor views, shared by the mesh,
// skin, and animation converters so the span math exists exactly once. (The mesh converter's
// originals moved here verbatim when the skin converter needed the same readers.)

[[nodiscard]] inline std::uint64_t GltfComponentSizeUVE(const GltfComponentTypeUVE componentType) noexcept {
    switch (componentType) {
    case GltfComponentTypeUVE::UnsignedByte:
        return 1U;
    case GltfComponentTypeUVE::UnsignedShort:
        return 2U;
    case GltfComponentTypeUVE::UnsignedInt:
    case GltfComponentTypeUVE::Float:
        return 4U;
    }
    return 0U;
}

[[nodiscard]] inline bool ValidateGltfAccessorViewUVE(const GltfAccessorViewUVE& accessor,
                                                      const std::uint64_t elementSize,
                                                      const std::uint64_t maximumElements) noexcept {
    if (GltfComponentSizeUVE(accessor.componentType) == 0U || accessor.elementCount == 0U ||
        accessor.elementCount > maximumElements) {
        return false;
    }
    const std::uint64_t stride = accessor.byteStride == 0U ? elementSize : accessor.byteStride;
    if (stride < elementSize || stride > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    return ValidateGltfAccessorSpanUVE(accessor.buffer.size(), accessor.byteOffset, accessor.elementCount, stride,
                                       elementSize, maximumElements);
}

[[nodiscard]] inline const std::byte* GltfAccessorElementUVE(const GltfAccessorViewUVE& accessor,
                                                             const std::size_t elementIndex,
                                                             const std::uint64_t elementSize) noexcept {
    const std::uint64_t stride = accessor.byteStride == 0U ? elementSize : accessor.byteStride;
    const std::uint64_t offset = accessor.byteOffset + static_cast<std::uint64_t>(elementIndex) * stride;
    if (offset > accessor.buffer.size() || accessor.buffer.size() - static_cast<std::size_t>(offset) <
                                              static_cast<std::size_t>(elementSize)) {
        return nullptr;
    }
    return accessor.buffer.data() + static_cast<std::size_t>(offset);
}

[[nodiscard]] inline std::uint32_t ReadGltfU32LEUVE(const std::byte* bytes) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 24U);
}

[[nodiscard]] inline std::uint16_t ReadGltfU16LEUVE(const std::byte* bytes) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1]) << 8U);
}

[[nodiscard]] inline float ReadGltfFloatLEUVE(const std::byte* bytes) noexcept {
    return std::bit_cast<float>(ReadGltfU32LEUVE(bytes));
}

} // namespace UVE::Asset::Detail
