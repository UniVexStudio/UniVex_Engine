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

#include "gltf_mesh_converter.h"
#include "gltf_metadata.h"

namespace UVE::Asset::Detail {

struct AccessorDefinition final {
    std::uint64_t bufferView = 0U;
    std::uint64_t byteOffset = 0U;
    std::uint64_t count = 0U;
    std::uint32_t componentType = 0U;
    std::string type;
};

struct BufferViewDefinition final {
    std::uint64_t buffer = 0U;
    std::uint64_t byteOffset = 0U;
    std::uint64_t byteLength = 0U;
    std::uint64_t byteStride = 0U;
};

[[nodiscard]] inline std::optional<std::uint64_t> ReadJsonUint(const nlohmann::json& object,
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

[[nodiscard]] inline std::optional<std::uint32_t> ReadJsonU32(const nlohmann::json& object, const char* key) {
    const auto value = ReadJsonUint(object, key, true);
    if (!value.has_value() || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

[[nodiscard]] inline std::optional<nlohmann::json::size_type> CheckedJsonArrayIndex(
    const nlohmann::json& array, const std::uint64_t index) {
    if (!array.is_array() || index > static_cast<std::uint64_t>(std::numeric_limits<nlohmann::json::size_type>::max()) ||
        index >= array.size()) {
        return std::nullopt;
    }
    return static_cast<nlohmann::json::size_type>(index);
}

[[nodiscard]] inline std::optional<BufferViewDefinition> ReadBufferView(const nlohmann::json& document,
                                                                              const std::uint64_t index) {
    if (!document.contains("bufferViews")) {
        return std::nullopt;
    }
    const auto bufferViewsIndex = CheckedJsonArrayIndex(document.at("bufferViews"), index);
    if (!bufferViewsIndex || !document.at("bufferViews").at(*bufferViewsIndex).is_object()) {
        return std::nullopt;
    }
    const auto& value = document.at("bufferViews").at(*bufferViewsIndex);
    const auto buffer = ReadJsonUint(value, "buffer", true);
    const auto byteOffset = ReadJsonUint(value, "byteOffset", false);
    const auto byteLength = ReadJsonUint(value, "byteLength", true);
    const auto byteStride = ReadJsonUint(value, "byteStride", false);
    if (!buffer || !byteOffset || !byteLength || !byteStride) {
        return std::nullopt;
    }
    return BufferViewDefinition{*buffer, *byteOffset, *byteLength, *byteStride};
}

[[nodiscard]] inline std::optional<AccessorDefinition> ReadAccessor(const nlohmann::json& document,
                                                                           const std::uint64_t index) {
    if (!document.contains("accessors")) {
        return std::nullopt;
    }
    const auto accessorsIndex = CheckedJsonArrayIndex(document.at("accessors"), index);
    if (!accessorsIndex || !document.at("accessors").at(*accessorsIndex).is_object()) {
        return std::nullopt;
    }
    const auto& value = document.at("accessors").at(*accessorsIndex);
    const auto bufferView = ReadJsonUint(value, "bufferView", true);
    const auto byteOffset = ReadJsonUint(value, "byteOffset", false);
    const auto count = ReadJsonUint(value, "count", true);
    const auto componentType = ReadJsonU32(value, "componentType");
    if (!bufferView || !byteOffset || !count || !componentType || !value.contains("type") ||
        !value.at("type").is_string() || *count > kMaximumGltfAccessorElements) {
        return std::nullopt;
    }
    return AccessorDefinition{*bufferView, *byteOffset, *count, *componentType, value.at("type").get<std::string>()};
}

[[nodiscard]] inline std::optional<GltfComponentType> ToComponentType(const std::uint32_t value) noexcept {
    switch (value) {
    case 5121U:
        return GltfComponentType::UnsignedByte;
    case 5123U:
        return GltfComponentType::UnsignedShort;
    case 5125U:
        return GltfComponentType::UnsignedInt;
    case 5126U:
        return GltfComponentType::Float;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] inline bool AddU64(const std::uint64_t left, const std::uint64_t right,
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
[[nodiscard]] inline std::optional<GltfAccessorView> BuildAccessorView(
    const nlohmann::json& document, const std::vector<std::byte>& buffer, const std::uint64_t accessorIndex,
    const std::string_view expectedType, const bool allowIndices) {
    const auto accessor = ReadAccessor(document, accessorIndex);
    if (!accessor || accessor->type != expectedType) {
        return std::nullopt;
    }
    const auto componentType = ToComponentType(accessor->componentType);
    if (!componentType || (!allowIndices && *componentType != GltfComponentType::Float) ||
        (allowIndices && *componentType == GltfComponentType::Float)) {
        return std::nullopt;
    }
    const auto view = ReadBufferView(document, accessor->bufferView);
    if (!view || view->buffer != 0U) {
        return std::nullopt;
    }
    std::uint64_t totalOffset = 0U;
    std::uint64_t viewEnd = 0U;
    if (!AddU64(view->byteOffset, view->byteLength, viewEnd) || viewEnd > buffer.size() ||
        !AddU64(view->byteOffset, accessor->byteOffset, totalOffset) ||
        accessor->byteOffset > view->byteLength) {
        return std::nullopt;
    }
    const std::uint64_t componentSize = *componentType == GltfComponentType::UnsignedByte
                                             ? 1U
                                             : *componentType == GltfComponentType::UnsignedShort ? 2U : 4U;
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
    if (!ValidateGltfAccessorSpan(view->byteLength, accessor->byteOffset, accessor->count, stride, elementSize) ||
        !ValidateGltfAccessorSpan(buffer.size(), totalOffset, accessor->count, stride, elementSize)) {
        return std::nullopt;
    }
    return GltfAccessorView{std::span<const std::byte>{buffer.data(), buffer.size()}, totalOffset,
                               accessor->count, view->byteStride, *componentType};
}

// --- Accessor element readers -----------------------------------------------------------
// Bounds-checked little-endian readers over caller-built accessor views, shared by the mesh,
// skin, and animation converters so the span math exists exactly once. (The mesh converter's
// originals moved here verbatim when the skin converter needed the same readers.)

[[nodiscard]] inline std::uint64_t GltfComponentSize(const GltfComponentType componentType) noexcept {
    switch (componentType) {
    case GltfComponentType::UnsignedByte:
        return 1U;
    case GltfComponentType::UnsignedShort:
        return 2U;
    case GltfComponentType::UnsignedInt:
    case GltfComponentType::Float:
        return 4U;
    }
    return 0U;
}

[[nodiscard]] inline bool ValidateGltfAccessorView(const GltfAccessorView& accessor,
                                                      const std::uint64_t elementSize,
                                                      const std::uint64_t maximumElements) noexcept {
    if (GltfComponentSize(accessor.componentType) == 0U || accessor.elementCount == 0U ||
        accessor.elementCount > maximumElements) {
        return false;
    }
    const std::uint64_t stride = accessor.byteStride == 0U ? elementSize : accessor.byteStride;
    if (stride < elementSize || stride > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    return ValidateGltfAccessorSpan(accessor.buffer.size(), accessor.byteOffset, accessor.elementCount, stride,
                                       elementSize, maximumElements);
}

[[nodiscard]] inline const std::byte* GltfAccessorElement(const GltfAccessorView& accessor,
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

[[nodiscard]] inline std::uint32_t ReadGltfU32LE(const std::byte* bytes) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 24U);
}

[[nodiscard]] inline std::uint16_t ReadGltfU16LE(const std::byte* bytes) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1]) << 8U);
}

[[nodiscard]] inline float ReadGltfFloatLE(const std::byte* bytes) noexcept {
    return std::bit_cast<float>(ReadGltfU32LE(bytes));
}

} // namespace UVE::Asset::Detail
