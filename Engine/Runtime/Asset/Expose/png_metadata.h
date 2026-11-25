// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace UVE::Asset {

inline constexpr std::uint64_t kMaximumPngDecodedPixelBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaximumPngRgba8ScanlineBytes = 4ULL * 1024ULL * 1024ULL;

struct PngMetadata final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint8_t bitDepth = 0U;
    std::uint8_t colorType = 0U;
    std::uint8_t compressionMethod = 0U;
    std::uint8_t filterMethod = 0U;
    std::uint8_t interlaceMethod = 0U;
    bool hasAlpha = false;
};

/// Parses bounded PNG signature/IHDR metadata from copied caller-owned bytes. The parser validates
/// the first IHDR chunk and its supported color/depth/method combinations; it never decodes pixels,
/// verifies or owns a filesystem path, allocates GPU resources, or selects a texture backend.
[[nodiscard]] std::optional<PngMetadata> ParsePngMetadata(const std::vector<std::byte>& bytes);
enum class PngFilterType : std::uint8_t { None = 0U, Sub = 1U, Up = 2U, Average = 3U, Paeth = 4U };

/// Reconstructs one bounded RGBA8 PNG scanline from its filtered bytes and an optional previous row.
/// With no previous row (the first scanline) every filter predicts from a row of zeros, as the PNG
/// specification defines it.
/// It performs no zlib/IDAT decoding, chunk parsing, image allocation, filesystem I/O, or texture conversion.
[[nodiscard]] bool UnfilterPngRgba8Scanline(
    PngFilterType filter, const std::vector<std::byte>& filteredBytes,
    const std::vector<std::byte>& previousRow, std::vector<std::byte>& outRow);

/// Checks the supported grayscale/grayscale-alpha/RGB/indexed/RGBA decoded-pixel budget after normalization to RGBA8,
/// including the bounded non-interlaced 1/2/4-bit grayscale/indexed and Adam7-interlaced 1/2/4-bit grayscale/indexed plus 16-bit grayscale/grayscale-alpha/RGB/RGBA source forms, without allocating or decoding pixel data.
[[nodiscard]] bool ValidatePngRgba8PixelBudget(
    const PngMetadata& metadata,
    std::uint64_t maximumBytes = kMaximumPngDecodedPixelBytes) noexcept;

/// Bytes per pixel of a decoded RGBA8 image, in R, G, B, A order.
inline constexpr std::size_t kPngRgba8Channels = 4U;

struct PngRgba8Image final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::byte> pixels;
};

/// Decodes bounded non-interlaced and Adam7-interlaced 1/2/4-bit grayscale/indexed, non-interlaced 8-bit grayscale/grayscale-alpha/RGB/indexed/RGBA, and 16-bit grayscale/grayscale-alpha/RGB/RGBA PNG forms using zlib and the existing scanline
/// unfilter primitive, normalizing grayscale/RGB input to opaque RGBA8, indexed input through validated PLTE/tRNS
/// palette facts, packed/8/16-bit grayscale and 8/16-bit RGB tRNS key transparency, 16-bit grayscale/grayscale-alpha/RGB/RGBA input through big-endian high-byte down-conversion, and Adam7-interlaced
/// 8-bit grayscale/grayscale-alpha/RGB/indexed/RGBA plus Adam7-interlaced 8-bit grayscale/grayscale-alpha/RGB/indexed/RGBA and 16-bit grayscale/grayscale-alpha/RGB/RGBA input through bounded pass reconstruction,
/// validated palette mapping, high-byte conversion, and opaque-alpha normalization. It rejects unsupported
/// color/depth/interlace modes and owns no filesystem or GPU state.
[[nodiscard]] bool DecodePngRgba8Image(
    const std::vector<std::byte>& bytes, PngRgba8Image& outImage) noexcept;

} // namespace UVE::Asset
