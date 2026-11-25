// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace UVE::Asset {
inline constexpr std::uint64_t kMaximumMtlTextureReferenceBytes = 256ULL;

enum class MtlMaterialPropertyKind : std::uint8_t { Invalid, Scalar, Vector3, TextureReference };

struct MtlMaterialProperty final {
    MtlMaterialPropertyKind kind = MtlMaterialPropertyKind::Invalid;
    float scalarValue = 0.0F;
    std::array<float, 3U> vectorValue{};
    std::string textureReference;
    [[nodiscard]] bool operator==(const MtlMaterialProperty&) const noexcept = default;
};

/// Parses one bounded MTL scalar, three-component vector, or validated texture-map property into
/// copied facts. It performs no shader/material conversion, filesystem I/O, or renderer ownership.
[[nodiscard]] bool ParseMtlMaterialProperty(
    std::string_view sourceLine, MtlMaterialProperty& outProperty);

struct MtlTextureMap final {
    std::string textureReference;
    std::array<float, 3U> scale{1.0F, 1.0F, 1.0F};
    std::array<float, 3U> offset{};
    bool clamp = false;
    [[nodiscard]] bool operator==(const MtlTextureMap&) const noexcept = default;
};

/// Parses one bounded MTL map directive with strict three-component `-s`/`-o` options and
/// `-clamp on|off`. It performs no texture loading, filesystem I/O, shader compilation, or material conversion.
[[nodiscard]] bool ParseMtlTextureMap(std::string_view sourceLine, MtlTextureMap& outMap);

struct MtlMetadata final {
    std::uint32_t materialCount = 0U;
    std::uint32_t textureMapCount = 0U;
    std::uint32_t scalarPropertyCount = 0U;
    std::uint32_t vectorPropertyCount = 0U;
    std::uint32_t ignoredStatementCount = 0U;
};
/// Validates one bounded relative MTL texture-reference token without filesystem access.
/// Empty, absolute, NUL-containing, and traversal segments fail closed.
[[nodiscard]] bool ValidateMtlTextureReference(std::string_view path) noexcept;

/// Parses bounded Wavefront MTL declaration statistics from caller-owned text. It validates
/// required tokens and complete recognized-declaration consumption, including supported map options,
/// and returns counts only; it does not load texture paths, compile shaders, resolve assets, or own
/// material/renderer state.
[[nodiscard]] std::optional<MtlMetadata> ParseMtlMetadata(std::string_view source);
} // namespace UVE::Asset
