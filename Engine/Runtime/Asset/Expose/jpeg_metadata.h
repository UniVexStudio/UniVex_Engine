// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <optional>
#include <vector>
namespace UVE::Asset {

inline constexpr std::uint64_t kMaximumJpegDecodedPixelBytes = 64ULL * 1024ULL * 1024ULL;
struct JpegMetadata final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint8_t precision = 0U;
    std::uint8_t componentCount = 0U;
    bool progressive = false;
};
/// Validates the normalized RGBA8 decoded-pixel budget without entropy decoding or allocation.
[[nodiscard]] bool ValidateJpegRgba8PixelBudget(
    const JpegMetadata& metadata,
    std::uint64_t maximumBytes = kMaximumJpegDecodedPixelBytes) noexcept;

/// Parses bounded JPEG SOI/segment/SOF metadata from caller-owned bytes. It validates segment
/// lengths and frame dimensions but never decodes entropy data, owns files, allocates pixels, or
/// selects a texture/renderer backend.
[[nodiscard]] std::optional<JpegMetadata> ParseJpegMetadata(const std::vector<std::byte>& bytes);

struct JpegRgba8Image final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::byte> pixels;
};

/// Decodes bounded JPEG source bytes into copied RGBA8 pixels using the approved codec boundary.
/// It rejects unsupported/unsafe input atomically and owns no filesystem or GPU resources.
[[nodiscard]] bool DecodeJpegRgba8Image(
    const std::vector<std::byte>& bytes, JpegRgba8Image& outImage) noexcept;
} // namespace UVE::Asset
