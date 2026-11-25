// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace UVE::Asset {

enum class GltfContainerKind : std::uint8_t { Json, Binary };

inline constexpr std::uint64_t kMaximumGltfAccessorElements = 1'000'000ULL;
inline constexpr std::size_t kMaximumGltfResourceUriBytes = 4'096U;
inline constexpr std::size_t kMaximumGltfDataUriDecodedBytes = 64U * 1024U * 1024U;

enum class GltfResourceUriKind : std::uint8_t { Invalid, RelativePath, DataUri };

/// Classifies one bounded glTF image/buffer URI without filesystem access or data decoding. Relative
/// paths must be traversal-safe; data URIs are recognized only as bounded non-empty tokens.
[[nodiscard]] GltfResourceUriKind ClassifyGltfResourceUri(std::string_view uri) noexcept;

/// Decodes one bounded glTF data URI into caller-owned bytes with failure-atomic publication.
/// It supports percent-encoded payloads and strict base64 payloads, but does not parse media types,
/// allocate image/buffer objects, or perform format conversion.
[[nodiscard]] bool DecodeGltfDataUri(
    std::string_view uri, std::vector<std::byte>& outBytes,
    std::size_t maximumBytes = kMaximumGltfDataUriDecodedBytes);

/// Resolves one safe relative glTF resource URI against the asset's directory into a bounded VFS
/// path. Data URIs, filesystem I/O, and resource decoding are intentionally outside this contract.
[[nodiscard]] bool ResolveGltfResourceVirtualPath(
    std::string_view assetVirtualPath, std::string_view resourceUri, std::string& outVirtualPath);

struct GltfMetadata final {
    GltfContainerKind container = GltfContainerKind::Json;
    std::uint32_t objectCount = 0U;
    std::uint32_t meshCount = 0U;
    std::uint32_t materialCount = 0U;
    std::uint32_t imageCount = 0U;
    std::uint32_t bufferCount = 0U;
    /// Skins (joint hierarchies) in the file: a mesh bound to bones is a rigged model rather than
    /// a static mesh, which the editor presents differently.
    std::uint32_t skinCount = 0U;
    bool hasBinaryChunk = false;
};

/// Validates one copied accessor byte span against a caller-owned buffer length. The check is
/// overflow-safe and does not parse JSON, read buffers, decode components, or allocate output.
[[nodiscard]] bool ValidateGltfAccessorSpan(
    std::uint64_t bufferByteLength,
    std::uint64_t byteOffset,
    std::uint64_t elementCount,
    std::uint64_t elementStride,
    std::uint64_t elementSize,
    std::uint64_t maximumElements = kMaximumGltfAccessorElements) noexcept;

/// Validates bounded glTF 2.0 JSON or GLB structure and returns copied top-level counts only.
/// It does not resolve buffers, decode images, convert meshes, load external resources, or own
/// renderer/importer state.
[[nodiscard]] std::optional<GltfMetadata> ParseGltfMetadata(std::string_view jsonSource);
/// Validates the complete bounded GLB chunk stream, including trailing chunk headers and payload
/// bounds, and rejects duplicate BIN chunks before publishing copied metadata counts.
[[nodiscard]] std::optional<GltfMetadata> ParseGlbMetadata(const std::vector<std::byte>& bytes);

} // namespace UVE::Asset
