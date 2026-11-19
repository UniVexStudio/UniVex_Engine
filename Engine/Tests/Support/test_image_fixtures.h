// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Raster fixtures for tests that need real image bytes rather than a mocked decoder: a model
// importer is only proved to carry a texture if the texture it published decodes back to the
// pixels that went in. The builder is header-only and deterministic, so two tests that need a
// 2x1 RGBA8 PNG share one definition instead of one copy each.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
    // One filter byte, then two opaque texels: red, then white.
    const std::vector<std::byte> raw{std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF},
                                     std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
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


/// Standard base64 with padding, the encoding an FBX uses for the `Content` of a `Video` element
/// that embeds a texture. Written here rather than borrowed so a fixture stays a pure function of
/// its input and a test never depends on how a platform's base64 happens to pad.
[[nodiscard]] inline std::string Base64EncodeUVE(const std::span<const std::byte> bytes) {
    constexpr std::string_view kAlphabetUVE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);
    std::size_t index = 0U;
    while (index + 3U <= bytes.size()) {
        const auto b0 = static_cast<std::uint32_t>(bytes[index]);
        const auto b1 = static_cast<std::uint32_t>(bytes[index + 1U]);
        const auto b2 = static_cast<std::uint32_t>(bytes[index + 2U]);
        const std::uint32_t triple = (b0 << 16U) | (b1 << 8U) | b2;
        encoded.push_back(kAlphabetUVE[(triple >> 18U) & 0x3FU]);
        encoded.push_back(kAlphabetUVE[(triple >> 12U) & 0x3FU]);
        encoded.push_back(kAlphabetUVE[(triple >> 6U) & 0x3FU]);
        encoded.push_back(kAlphabetUVE[triple & 0x3FU]);
        index += 3U;
    }
    const std::size_t remaining = bytes.size() - index;
    if (remaining == 1U) {
        const auto b0 = static_cast<std::uint32_t>(bytes[index]);
        encoded.push_back(kAlphabetUVE[(b0 >> 2U) & 0x3FU]);
        encoded.push_back(kAlphabetUVE[(b0 << 4U) & 0x3FU]);
        encoded += "==";
    } else if (remaining == 2U) {
        const auto b0 = static_cast<std::uint32_t>(bytes[index]);
        const auto b1 = static_cast<std::uint32_t>(bytes[index + 1U]);
        encoded.push_back(kAlphabetUVE[(b0 >> 2U) & 0x3FU]);
        encoded.push_back(kAlphabetUVE[((b0 << 4U) | (b1 >> 4U)) & 0x3FU]);
        encoded.push_back(kAlphabetUVE[(b1 << 2U) & 0x3FU]);
        encoded.push_back('=');
    }
    return encoded;
}

[[nodiscard]] inline std::string Base64EncodeUVE(const std::vector<std::byte>& bytes) {
    return Base64EncodeUVE(std::span<const std::byte>(bytes));
}

} // namespace UVE::Tests
