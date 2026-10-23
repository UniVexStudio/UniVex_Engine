// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Raster fixtures for tests that need real image bytes rather than a mocked decoder: a model
// importer is only proved to carry a texture if the texture it published decodes back to the
// pixels that went in. The builder is header-only and deterministic, so two tests that need a
// 2x1 RGBA8 PNG share one definition instead of one copy each.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <zlib.h>

namespace UVE::Tests {

inline void AppendU32BigEndianUVE(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 24U;; shift -= 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
        if (shift == 0U) {
            break;
        }
    }
}

inline void AppendPngChunkUVE(std::vector<std::byte>& png, const std::vector<std::byte>& type,
                              const std::vector<std::byte>& payload) {
    AppendU32BigEndianUVE(png, static_cast<std::uint32_t>(payload.size()));
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), payload.begin(), payload.end());
    std::vector<std::byte> crcInput(type);
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    const uLong crc =
        crc32(0L, reinterpret_cast<const Bytef*>(crcInput.data()), static_cast<uInt>(crcInput.size()));
    AppendU32BigEndianUVE(png, static_cast<std::uint32_t>(crc));
}

/// A valid 2x1 RGBA8 PNG: red texel then white texel. Small enough to embed inside a GLB fixture
/// and non-trivial enough that a decoder that ignores pixel data cannot pass by accident.
[[nodiscard]] inline std::vector<std::byte> MakePngTwoByOneRgba8UVE() {
    std::vector<std::byte> png{std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
                               std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    AppendPngChunkUVE(png, {std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}},
                      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0},
                       std::byte{0}, std::byte{1}, std::byte{8}, std::byte{6}, std::byte{0}, std::byte{0},
                       std::byte{0}});
    const std::vector<std::byte> raw{std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF},
                                     std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}};
    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::byte> compressed(static_cast<std::size_t>(compressedSize));
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                  Z_BEST_SPEED) != Z_OK) {
        return {};
    }
    compressed.resize(static_cast<std::size_t>(compressedSize));
    AppendPngChunkUVE(png, {std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}}, compressed);
    AppendPngChunkUVE(png, {std::byte{'I'}, std::byte{'E'}, std::byte{'N'}, std::byte{'D'}}, {});
    return png;
}

} // namespace UVE::Tests
