// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "png_metadata.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

namespace UVE::Asset::Tests {
namespace {

void AppendU32BE(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 24U;; shift -= 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
        if (shift == 0U) {
            break;
        }
    }
}

[[nodiscard]] std::vector<std::byte> MakePngIhdr(const std::uint32_t width = 1920U,
                                                    const std::uint32_t height = 1080U,
                                                    const std::uint8_t bitDepth = 8U,
                                                    const std::uint8_t colorType = 6U,
                                                    const std::uint8_t interlace = 0U) {
    std::vector<std::byte> bytes{
        std::byte{137}, std::byte{80}, std::byte{78}, std::byte{71},
        std::byte{13}, std::byte{10}, std::byte{26}, std::byte{10},
    };
    AppendU32BE(bytes, 13U);
    bytes.insert(bytes.end(), {std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}});
    AppendU32BE(bytes, width);
    AppendU32BE(bytes, height);
    bytes.push_back(std::byte{bitDepth});
    bytes.push_back(std::byte{colorType});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{interlace});
    bytes.insert(bytes.end(), {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
    return bytes;
}

TEST(PngMetadataTest, ParsePngMetadata_ReturnsRgbaIhdrFacts) {
    const std::optional<PngMetadata> metadata = ParsePngMetadata(MakePngIhdr());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->width, 1920U);
    EXPECT_EQ(metadata->height, 1080U);
    EXPECT_EQ(metadata->bitDepth, 8U);
    EXPECT_EQ(metadata->colorType, 6U);
    EXPECT_TRUE(metadata->hasAlpha);
    EXPECT_EQ(metadata->interlaceMethod, 0U);
}

TEST(PngMetadataTest, ParsePngMetadata_ReportsIndexedColorWithoutAlpha) {
    const std::optional<PngMetadata> metadata = ParsePngMetadata(MakePngIhdr(64U, 32U, 4U, 3U, 1U));
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->width, 64U);
    EXPECT_EQ(metadata->height, 32U);
    EXPECT_EQ(metadata->bitDepth, 4U);
    EXPECT_EQ(metadata->colorType, 3U);
    EXPECT_FALSE(metadata->hasAlpha);
    EXPECT_EQ(metadata->interlaceMethod, 1U);
}

TEST(PngMetadataTest, UnfilterPngRgba8Scanline_ReconstructsAllSupportedFilters) {
    const std::vector<std::byte> original{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
                                           std::byte{50}, std::byte{60}, std::byte{70}, std::byte{80}};
    const std::vector<std::byte> previous{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                            std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    std::vector<std::byte> output;
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::None, original, {}, output));
    EXPECT_EQ(output, original);
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Sub,
                                            {std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
                                             std::byte{40}, std::byte{40}, std::byte{40}, std::byte{40}}, {}, output));
    EXPECT_EQ(output, original);
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Up,
                                            {std::byte{9}, std::byte{18}, std::byte{27}, std::byte{36},
                                             std::byte{45}, std::byte{54}, std::byte{63}, std::byte{72}}, previous, output));
    EXPECT_EQ(output, original);
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Average,
                                            {std::byte{10}, std::byte{19}, std::byte{29}, std::byte{38},
                                             std::byte{43}, std::byte{47}, std::byte{52}, std::byte{56}}, previous, output));
    EXPECT_EQ(output, original);
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Paeth,
                                            {std::byte{9}, std::byte{18}, std::byte{27}, std::byte{36},
                                             std::byte{40}, std::byte{40}, std::byte{40}, std::byte{40}}, previous, output));
    EXPECT_EQ(output, original);
}

TEST(PngMetadataTest, UnfilterPngRgba8Scanline_FirstRowPredictsFromZerosForEveryFilter) {
    // The first scanline has no row above it; the specification treats that row as zeros, so Up is
    // the bytes as they are, Average halves only the left neighbour, and Paeth reduces to Sub.
    const std::vector<std::byte> filtered{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
                                          std::byte{2},  std::byte{4},  std::byte{6},  std::byte{8}};
    std::vector<std::byte> output;
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Up, filtered, {}, output));
    EXPECT_EQ(output, filtered);
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Average, filtered, {}, output));
    EXPECT_EQ(output, (std::vector<std::byte>{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
                                              std::byte{7}, std::byte{14}, std::byte{21}, std::byte{28}}));
    std::vector<std::byte> sub;
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Sub, filtered, {}, sub));
    ASSERT_TRUE(UnfilterPngRgba8Scanline(PngFilterType::Paeth, filtered, {}, output));
    EXPECT_EQ(output, sub);
}

TEST(PngMetadataTest, UnfilterPngRgba8Scanline_RejectsInvalidInputsAtomically) {
    const std::vector<std::byte> original{std::byte{0xAA}, std::byte{0xBB}};
    std::vector<std::byte> output = original;
    EXPECT_FALSE(UnfilterPngRgba8Scanline(static_cast<PngFilterType>(9U), {std::byte{1}, std::byte{2}}, {}, output));
    EXPECT_EQ(output, original);
    EXPECT_FALSE(UnfilterPngRgba8Scanline(PngFilterType::None, {std::byte{1}, std::byte{2}}, {std::byte{1}}, output));
    EXPECT_EQ(output, original);
    EXPECT_FALSE(UnfilterPngRgba8Scanline(PngFilterType::None, {}, {}, output));
    EXPECT_EQ(output, original);
    const std::vector<std::byte> oversized(kMaximumPngRgba8ScanlineBytes + 1U, std::byte{0});
    EXPECT_FALSE(UnfilterPngRgba8Scanline(PngFilterType::None, oversized, {}, output));
    EXPECT_EQ(output, original);
}

TEST(PngMetadataTest, ValidatePngRgba8PixelBudget_AcceptsDefaultHdBudget) {
    const std::optional<PngMetadata> metadata = ParsePngMetadata(MakePngIhdr());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(*metadata));
}

[[nodiscard]] std::vector<std::byte> MakePngOneByOne(
    const std::uint8_t colorType, const std::vector<std::byte>& raw,
    const std::vector<std::byte>& palette = {}, const std::vector<std::byte>& alpha = {},
    const std::uint8_t bitDepth = 8U, const std::uint32_t width = 1U,
    const std::uint32_t height = 1U, const std::uint8_t interlace = 0U,
    const std::vector<std::byte>& transparency = {}) {
    std::vector<std::byte> png{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47}, std::byte{0x0D}, std::byte{0x0A},
        std::byte{0x1A}, std::byte{0x0A}};
    const auto appendChunk = [](std::vector<std::byte>& output, const std::vector<std::byte>& type,
                                const std::vector<std::byte>& payload) {
        AppendU32BE(output, static_cast<std::uint32_t>(payload.size()));
        output.insert(output.end(), type.begin(), type.end());
        output.insert(output.end(), payload.begin(), payload.end());
        std::vector<std::byte> crcInput(type);
        crcInput.insert(crcInput.end(), payload.begin(), payload.end());
        const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(crcInput.data()), static_cast<uInt>(crcInput.size()));
        AppendU32BE(output, static_cast<std::uint32_t>(crc));
    };
    appendChunk(png, {std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}},
                {std::byte{static_cast<unsigned char>((width >> 24U) & 0xFFU)},
                 std::byte{static_cast<unsigned char>((width >> 16U) & 0xFFU)},
                 std::byte{static_cast<unsigned char>((width >> 8U) & 0xFFU)},
                 std::byte{static_cast<unsigned char>(width & 0xFFU)},
                 std::byte{static_cast<unsigned char>((height >> 24U) & 0xFFU)},
                 std::byte{static_cast<unsigned char>((height >> 16U) & 0xFFU)},
                 std::byte{static_cast<unsigned char>((height >> 8U) & 0xFFU)},
                 std::byte{static_cast<unsigned char>(height & 0xFFU)},
                 std::byte{bitDepth}, std::byte{colorType}, std::byte{0}, std::byte{0}, std::byte{interlace}});
    if (colorType == 3U) {
        if (palette.empty() || palette.size() % 3U != 0U || palette.size() > 768U ||
            (!alpha.empty() && alpha.size() > palette.size() / 3U)) {
            return {};
        }
        appendChunk(png, {std::byte{'P'}, std::byte{'L'}, std::byte{'T'}, std::byte{'E'}}, palette);
        if (!alpha.empty()) appendChunk(png, {std::byte{'t'}, std::byte{'R'}, std::byte{'N'}, std::byte{'S'}}, alpha);
    } else if (!transparency.empty()) {
        appendChunk(png, {std::byte{'t'}, std::byte{'R'}, std::byte{'N'}, std::byte{'S'}}, transparency);
    }
    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::byte> compressed(static_cast<std::size_t>(compressedSize));
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        return {};
    }
    compressed.resize(static_cast<std::size_t>(compressedSize));
    appendChunk(png, {std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}}, compressed);
    appendChunk(png, {std::byte{'I'}, std::byte{'E'}, std::byte{'N'}, std::byte{'D'}}, {});
    return png;
}

[[nodiscard]] std::vector<std::byte> MakePngRgbOneByOne() {
    return MakePngOneByOne(2U, {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}});
}

[[nodiscard]] std::vector<std::byte> MakePngGrayOneByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x80}});
}

[[nodiscard]] std::vector<std::byte> MakePngGrayAlphaOneByOne() {
    return MakePngOneByOne(4U, {std::byte{0}, std::byte{0x80}, std::byte{0xC0}});
}

[[nodiscard]] std::vector<std::byte> MakePngGray1BitEightByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x59}}, {}, {}, 1U, 8U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngGray2BitFourByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x1B}}, {}, {}, 2U, 4U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngGray4BitTwoByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x1E}}, {}, {}, 4U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngPackedGrayTransparent(
    const std::uint8_t bitDepth, const std::vector<std::byte>& raw, const std::uint32_t width,
    const std::uint32_t height, const std::uint8_t interlace, const std::uint16_t key) {
    return MakePngOneByOne(0U, raw, {}, {}, bitDepth, width, height, interlace,
                               {std::byte{static_cast<unsigned char>(key >> 8U)},
                                std::byte{static_cast<unsigned char>(key & 0xFFU)}});
}

[[nodiscard]] std::vector<std::byte> MakePngGray1BitTransparentEightByOne() {
    return MakePngPackedGrayTransparent(1U, {std::byte{0}, std::byte{0x59}}, 8U, 1U, 0U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngGray2BitTransparentFourByOne() {
    return MakePngPackedGrayTransparent(2U, {std::byte{0}, std::byte{0x1B}}, 4U, 1U, 0U, 2U);
}

[[nodiscard]] std::vector<std::byte> MakePngGray4BitTransparentTwoByOne() {
    return MakePngPackedGrayTransparent(4U, {std::byte{0}, std::byte{0x1E}}, 2U, 1U, 0U, 14U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedGray1BitTransparentTwoByTwo() {
    return MakePngPackedGrayTransparent(1U,
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x80}, std::byte{0}, std::byte{0x80}},
        2U, 2U, 1U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedGray2BitTransparentTwoByTwo() {
    return MakePngPackedGrayTransparent(2U,
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xC0}, std::byte{0}, std::byte{0x60}},
        2U, 2U, 1U, 2U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedGray4BitTransparentTwoByTwo() {
    return MakePngPackedGrayTransparent(4U,
        {std::byte{0}, std::byte{0x10}, std::byte{0}, std::byte{0xE0}, std::byte{0}, std::byte{0x2C}},
        2U, 2U, 1U, 14U);
}

[[nodiscard]] std::vector<std::byte> MakePngIndexed1BitEightByOne() {
    return MakePngOneByOne(3U, {std::byte{0}, std::byte{0x59}},
                               {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0}},
                               {std::byte{0xFF}, std::byte{0x80}}, 1U, 8U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngIndexed2BitFourByOne() {
    return MakePngOneByOne(3U, {std::byte{0}, std::byte{0x1B}},
                               {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0},
                                std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
                               {}, 2U, 4U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngIndexed4BitTwoByOne() {
    std::vector<std::byte> palette(16U * 3U, std::byte{0});
    palette[3U] = std::byte{0xFF};
    palette[14U * 3U + 1U] = std::byte{0xFF};
    return MakePngOneByOne(3U, {std::byte{0}, std::byte{0x1E}}, palette, {}, 4U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedGray1BitTwoByTwo() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x80},
                                   std::byte{0}, std::byte{0x80}}, {}, {}, 1U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedGray2BitTwoByTwo() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xC0},
                                   std::byte{0}, std::byte{0x60}}, {}, {}, 2U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedGray4BitTwoByTwo() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x10}, std::byte{0}, std::byte{0xE0},
                                   std::byte{0}, std::byte{0x2C}}, {}, {}, 4U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedIndexed1BitTwoByTwo() {
    return MakePngOneByOne(3U, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x80},
                                   std::byte{0}, std::byte{0x80}},
                               {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0}},
                               {std::byte{0xFF}, std::byte{0x80}}, 1U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedIndexed2BitTwoByTwo() {
    return MakePngOneByOne(3U, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xC0},
                                   std::byte{0}, std::byte{0x60}},
                               {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0},
                                std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
                               {}, 2U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7PackedIndexed4BitTwoByTwo() {
    std::vector<std::byte> palette(16U * 3U, std::byte{0});
    palette[3U] = std::byte{0xFF};
    palette[14U * 3U + 1U] = std::byte{0xFF};
    palette[2U * 3U + 2U] = std::byte{0xFF};
    palette[12U * 3U] = std::byte{0xFF};
    return MakePngOneByOne(3U, {std::byte{0}, std::byte{0x10}, std::byte{0}, std::byte{0xE0},
                                   std::byte{0}, std::byte{0x2C}}, palette, {}, 4U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngIndexedOneByOne(const std::byte index = std::byte{1}) {
    return MakePngOneByOne(3U, {std::byte{0}, index},
                               {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0}},
                               {std::byte{0xFF}, std::byte{0x80}});
}

[[nodiscard]] std::vector<std::byte> MakePngRgba16OneByOne() {
    return MakePngOneByOne(6U, {std::byte{0}, std::byte{0x12}, std::byte{0x34}, std::byte{0xAB}, std::byte{0xCD},
                                   std::byte{0x56}, std::byte{0x78}, std::byte{0x9A}, std::byte{0xBC}},
                               {}, {}, 16U);
}

[[nodiscard]] std::vector<std::byte> MakePngGray16OneByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x34}, std::byte{0x56}}, {}, {}, 16U);
}

[[nodiscard]] std::vector<std::byte> MakePngRgb16OneByOne() {
    return MakePngOneByOne(2U, {std::byte{0}, std::byte{0x12}, std::byte{0x34}, std::byte{0xAB}, std::byte{0xCD},
                                   std::byte{0xEF}, std::byte{0x01}}, {}, {}, 16U);
}

[[nodiscard]] std::vector<std::byte> MakePngGrayAlpha16OneByOne() {
    return MakePngOneByOne(4U, {std::byte{0}, std::byte{0x34}, std::byte{0x56}, std::byte{0xAB},
                                   std::byte{0xCD}}, {}, {}, 16U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7RgbaTwoByTwo() {
    return MakePngOneByOne(6U,
                               {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF},
                                std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF},
                                std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF},
                                std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
                               {}, {}, 8U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7RgbTwoByTwo() {
    return MakePngOneByOne(2U,
                               {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0},
                                std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0},
                                std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0xFF},
                                std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
                               {}, {}, 8U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7GrayTwoByTwo() {
    return MakePngOneByOne(0U,
                               {std::byte{0}, std::byte{0x10},
                                std::byte{0}, std::byte{0x80},
                                std::byte{0}, std::byte{0xC0}, std::byte{0xFF}},
                               {}, {}, 8U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngTransparentGray8OneByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x80}}, {}, {}, 8U, 1U, 1U, 0U,
                                   {std::byte{0}, std::byte{0x80}});
}

[[nodiscard]] std::vector<std::byte> MakePngTransparentRgb8OneByOne() {
    return MakePngOneByOne(2U, {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}}, {}, {}, 8U, 1U, 1U, 0U,
                                   {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
}

[[nodiscard]] std::vector<std::byte> MakePngTransparentGray16OneByOne() {
    return MakePngOneByOne(0U, {std::byte{0}, std::byte{0x34}, std::byte{0x56}}, {}, {}, 16U, 1U, 1U, 0U,
                                   {std::byte{0x34}, std::byte{0x56}});
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7TransparentGrayTwoByTwo() {
    return MakePngOneByOne(0U,
                               {std::byte{0}, std::byte{0x10},
                                std::byte{0}, std::byte{0x80},
                                std::byte{0}, std::byte{0xC0}, std::byte{0xFF}},
                               {}, {}, 8U, 2U, 2U, 1U, {std::byte{0}, std::byte{0x80}});
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7IndexedTwoByTwo() {
    return MakePngOneByOne(3U,
                               {std::byte{0}, std::byte{0},
                                std::byte{0}, std::byte{1},
                                std::byte{0}, std::byte{2}, std::byte{3}},
                               {std::byte{0xFF}, std::byte{0}, std::byte{0},
                                std::byte{0}, std::byte{0xFF}, std::byte{0},
                                std::byte{0}, std::byte{0}, std::byte{0xFF},
                                std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
                               {std::byte{0xFF}, std::byte{0x80}, std::byte{0x40}, std::byte{0xFF}},
                               8U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7Gray16TwoByTwo() {
    return MakePngOneByOne(0U,
                               {std::byte{0}, std::byte{0x12}, std::byte{0x34},
                                std::byte{0}, std::byte{0xAB}, std::byte{0xCD},
                                std::byte{0}, std::byte{0x10}, std::byte{0x20},
                                std::byte{0xFF}, std::byte{0xEE}},
                               {}, {}, 16U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7Rgb16TwoByTwo() {
    return MakePngOneByOne(2U,
                               {std::byte{0}, std::byte{0x12}, std::byte{0x01}, std::byte{0x34}, std::byte{0x02},
                                std::byte{0x56}, std::byte{0x03}, std::byte{0}, std::byte{0xAB}, std::byte{0x05},
                                std::byte{0xCD}, std::byte{0x06}, std::byte{0xEF}, std::byte{0x07}, std::byte{0},
                                std::byte{0x10}, std::byte{0x09}, std::byte{0x20}, std::byte{0x0A}, std::byte{0x30},
                                std::byte{0x0B}, std::byte{0xFF}, std::byte{0x0C}, std::byte{0xEE}, std::byte{0x0D},
                                std::byte{0xDD}, std::byte{0x0E}},
                               {}, {}, 16U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7GrayAlpha16TwoByTwo() {
    return MakePngOneByOne(4U,
                               {std::byte{0}, std::byte{0x12}, std::byte{0x34}, std::byte{0xAB}, std::byte{0xCD},
                                std::byte{0}, std::byte{0x56}, std::byte{0x78}, std::byte{0xEF}, std::byte{0x01},
                                std::byte{0}, std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
                                std::byte{0xFF}, std::byte{0xEE}, std::byte{0x80}, std::byte{0x90}},
                               {}, {}, 16U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7GrayAlphaTwoByTwo() {
    return MakePngOneByOne(4U,
                               {std::byte{0}, std::byte{0x12}, std::byte{0xAB},
                                std::byte{0}, std::byte{0x56}, std::byte{0xEF},
                                std::byte{0}, std::byte{0x10}, std::byte{0x30}, std::byte{0xFF}, std::byte{0x80}},
                               {}, {}, 8U, 2U, 2U, 1U);
}

[[nodiscard]] std::vector<std::byte> MakePngAdam7Rgba16TwoByTwo() {
    return MakePngOneByOne(6U,
                               {std::byte{0}, std::byte{0x12}, std::byte{0x01}, std::byte{0x34}, std::byte{0x02},
                                std::byte{0x56}, std::byte{0x03}, std::byte{0x78}, std::byte{0x04},
                                std::byte{0}, std::byte{0xAB}, std::byte{0x05}, std::byte{0xCD}, std::byte{0x06},
                                std::byte{0xEF}, std::byte{0x07}, std::byte{0x01}, std::byte{0x08},
                                std::byte{0}, std::byte{0x10}, std::byte{0x09}, std::byte{0x20}, std::byte{0x0A},
                                std::byte{0x30}, std::byte{0x0B}, std::byte{0x40}, std::byte{0x0C},
                                std::byte{0xFF}, std::byte{0x0D}, std::byte{0xFF}, std::byte{0x0E}, std::byte{0xFF},
                                std::byte{0x0F}, std::byte{0xFF}, std::byte{0x10}},
                               {}, {}, 16U, 2U, 2U, 1U);
}

TEST(PngMetadataTest, ValidatePngRgba8PixelBudget_RejectsZeroAndAcceptsGrayRgbFacts) {
    PngMetadata zeroWidth{.width = 0U, .height = 1080U, .bitDepth = 8U, .colorType = 6U};
    EXPECT_FALSE(ValidatePngRgba8PixelBudget(zeroWidth));

    PngMetadata gray{.width = 1920U, .height = 1080U, .bitDepth = 8U, .colorType = 0U};
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(gray));
    PngMetadata rgb{.width = 1920U, .height = 1080U, .bitDepth = 8U, .colorType = 2U};
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(rgb));
    PngMetadata rgba16{.width = 1U, .height = 1U, .bitDepth = 16U, .colorType = 6U};
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(rgba16));
    PngMetadata gray16{.width = 1U, .height = 1U, .bitDepth = 16U, .colorType = 0U};
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(gray16));
    PngMetadata rgb16Accepted{.width = 1U, .height = 1U, .bitDepth = 16U, .colorType = 2U};
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(rgb16Accepted));
    PngMetadata indexed16{.width = 1U, .height = 1U, .bitDepth = 16U, .colorType = 3U};
    EXPECT_FALSE(ValidatePngRgba8PixelBudget(indexed16));
}

TEST(PngMetadataTest, ValidatePngRgba8PixelBudget_RejectsOverflowAndOversizedBudget) {
    PngMetadata huge{.width = 0xFFFFFFFFU, .height = 0xFFFFFFFFU, .bitDepth = 8U, .colorType = 6U};
    EXPECT_FALSE(ValidatePngRgba8PixelBudget(huge, std::numeric_limits<std::uint64_t>::max()));

    PngMetadata small{.width = 4U, .height = 4U, .bitDepth = 8U, .colorType = 6U};
    EXPECT_FALSE(ValidatePngRgba8PixelBudget(small, 63ULL));
    EXPECT_TRUE(ValidatePngRgba8PixelBudget(small, 64ULL));
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsAdam7Gray16TwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7Gray16TwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0x12});
    EXPECT_EQ(image.pixels[2], std::byte{0x12});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0xAB});
    EXPECT_EQ(image.pixels[5], std::byte{0xAB});
    EXPECT_EQ(image.pixels[6], std::byte{0xAB});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
    EXPECT_EQ(image.pixels[8], std::byte{0x10});
    EXPECT_EQ(image.pixels[9], std::byte{0x10});
    EXPECT_EQ(image.pixels[10], std::byte{0x10});
    EXPECT_EQ(image.pixels[11], std::byte{0xFF});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsAdam7Rgb16TwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7Rgb16TwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0x34});
    EXPECT_EQ(image.pixels[2], std::byte{0x56});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0xAB});
    EXPECT_EQ(image.pixels[5], std::byte{0xCD});
    EXPECT_EQ(image.pixels[6], std::byte{0xEF});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
    EXPECT_EQ(image.pixels[8], std::byte{0x10});
    EXPECT_EQ(image.pixels[9], std::byte{0x20});
    EXPECT_EQ(image.pixels[10], std::byte{0x30});
    EXPECT_EQ(image.pixels[11], std::byte{0xFF});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xEE});
    EXPECT_EQ(image.pixels[14], std::byte{0xDD});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsAdam7GrayAlpha16TwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7GrayAlpha16TwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0x12});
    EXPECT_EQ(image.pixels[2], std::byte{0x12});
    EXPECT_EQ(image.pixels[3], std::byte{0xAB});
    EXPECT_EQ(image.pixels[4], std::byte{0x56});
    EXPECT_EQ(image.pixels[5], std::byte{0x56});
    EXPECT_EQ(image.pixels[6], std::byte{0x56});
    EXPECT_EQ(image.pixels[7], std::byte{0xEF});
    EXPECT_EQ(image.pixels[8], std::byte{0x10});
    EXPECT_EQ(image.pixels[9], std::byte{0x10});
    EXPECT_EQ(image.pixels[10], std::byte{0x10});
    EXPECT_EQ(image.pixels[11], std::byte{0x30});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0x80});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7GrayAlphaTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7GrayAlphaTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0x12});
    EXPECT_EQ(image.pixels[2], std::byte{0x12});
    EXPECT_EQ(image.pixels[3], std::byte{0xAB});
    EXPECT_EQ(image.pixels[4], std::byte{0x56});
    EXPECT_EQ(image.pixels[5], std::byte{0x56});
    EXPECT_EQ(image.pixels[6], std::byte{0x56});
    EXPECT_EQ(image.pixels[7], std::byte{0xEF});
    EXPECT_EQ(image.pixels[8], std::byte{0x10});
    EXPECT_EQ(image.pixels[9], std::byte{0x10});
    EXPECT_EQ(image.pixels[10], std::byte{0x10});
    EXPECT_EQ(image.pixels[11], std::byte{0x30});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0x80});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsAdam7Rgba16TwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7Rgba16TwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0x34});
    EXPECT_EQ(image.pixels[2], std::byte{0x56});
    EXPECT_EQ(image.pixels[3], std::byte{0x78});
    EXPECT_EQ(image.pixels[4], std::byte{0xAB});
    EXPECT_EQ(image.pixels[5], std::byte{0xCD});
    EXPECT_EQ(image.pixels[6], std::byte{0xEF});
    EXPECT_EQ(image.pixels[7], std::byte{0x01});
    EXPECT_EQ(image.pixels[8], std::byte{0x10});
    EXPECT_EQ(image.pixels[9], std::byte{0x20});
    EXPECT_EQ(image.pixels[10], std::byte{0x30});
    EXPECT_EQ(image.pixels[11], std::byte{0x40});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7IndexedTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7IndexedTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0xFF});
    EXPECT_EQ(image.pixels[1], std::byte{0});
    EXPECT_EQ(image.pixels[2], std::byte{0});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0});
    EXPECT_EQ(image.pixels[5], std::byte{0xFF});
    EXPECT_EQ(image.pixels[6], std::byte{0});
    EXPECT_EQ(image.pixels[7], std::byte{0x80});
    EXPECT_EQ(image.pixels[8], std::byte{0});
    EXPECT_EQ(image.pixels[9], std::byte{0});
    EXPECT_EQ(image.pixels[10], std::byte{0xFF});
    EXPECT_EQ(image.pixels[11], std::byte{0x40});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7GrayTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7GrayTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0x10});
    EXPECT_EQ(image.pixels[1], std::byte{0x10});
    EXPECT_EQ(image.pixels[2], std::byte{0x10});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0x80});
    EXPECT_EQ(image.pixels[5], std::byte{0x80});
    EXPECT_EQ(image.pixels[6], std::byte{0x80});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
    EXPECT_EQ(image.pixels[8], std::byte{0xC0});
    EXPECT_EQ(image.pixels[9], std::byte{0xC0});
    EXPECT_EQ(image.pixels[10], std::byte{0xC0});
    EXPECT_EQ(image.pixels[11], std::byte{0xFF});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7RgbTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7RgbTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0xFF});
    EXPECT_EQ(image.pixels[1], std::byte{0});
    EXPECT_EQ(image.pixels[2], std::byte{0});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0});
    EXPECT_EQ(image.pixels[5], std::byte{0xFF});
    EXPECT_EQ(image.pixels[6], std::byte{0});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
    EXPECT_EQ(image.pixels[8], std::byte{0});
    EXPECT_EQ(image.pixels[9], std::byte{0});
    EXPECT_EQ(image.pixels[10], std::byte{0xFF});
    EXPECT_EQ(image.pixels[11], std::byte{0xFF});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7PackedGray1BitTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7PackedGray1BitTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    const std::array<std::byte, 4> expected{std::byte{0}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0}};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        EXPECT_EQ(image.pixels[pixel * 4U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 1U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 2U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 3U], std::byte{0xFF});
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7PackedGray2BitTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7PackedGray2BitTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    const std::array<std::byte, 4> expected{std::byte{0}, std::byte{0xFF}, std::byte{0x55}, std::byte{0xAA}};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        EXPECT_EQ(image.pixels[pixel * 4U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 1U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 2U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 3U], std::byte{0xFF});
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7PackedGray4BitTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7PackedGray4BitTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    const std::array<std::byte, 4> expected{std::byte{0x11}, std::byte{0xEE}, std::byte{0x22}, std::byte{0xCC}};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        EXPECT_EQ(image.pixels[pixel * 4U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 1U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 2U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 3U], std::byte{0xFF});
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7PackedIndexed1BitTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7PackedIndexed1BitTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    const std::array<std::array<std::byte, 4>, 4> expected{{
        {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0x80}},
        {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0x80}},
        {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}},
    }};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            EXPECT_EQ(image.pixels[pixel * 4U + channel], expected[pixel][channel]);
        }
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7PackedIndexed2BitTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7PackedIndexed2BitTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    const std::array<std::array<std::byte, 4>, 4> expected{{
        {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}},
    }};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            EXPECT_EQ(image.pixels[pixel * 4U + channel], expected[pixel][channel]);
        }
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7PackedIndexed4BitTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7PackedIndexed4BitTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    const std::array<std::array<std::byte, 4>, 4> expected{{
        {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}},
        {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}},
    }};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            EXPECT_EQ(image.pixels[pixel * 4U + channel], expected[pixel][channel]);
        }
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsAdam7PackedIndexedOutOfRangeAtomically) {
    const std::vector<std::byte> png = MakePngOneByOne(3U,
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x80}, std::byte{0}, std::byte{0x80}},
        {std::byte{0xFF}, std::byte{0}, std::byte{0}}, {}, 1U, 2U, 2U, 1U);
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesAdam7RgbaTwoByTwo) {
    const std::vector<std::byte> png = MakePngAdam7RgbaTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 2U);
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[0], std::byte{0xFF});
    EXPECT_EQ(image.pixels[1], std::byte{0});
    EXPECT_EQ(image.pixels[2], std::byte{0});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0});
    EXPECT_EQ(image.pixels[5], std::byte{0xFF});
    EXPECT_EQ(image.pixels[6], std::byte{0});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
    EXPECT_EQ(image.pixels[8], std::byte{0});
    EXPECT_EQ(image.pixels[9], std::byte{0});
    EXPECT_EQ(image.pixels[10], std::byte{0xFF});
    EXPECT_EQ(image.pixels[11], std::byte{0xFF});
    EXPECT_EQ(image.pixels[12], std::byte{0xFF});
    EXPECT_EQ(image.pixels[13], std::byte{0xFF});
    EXPECT_EQ(image.pixels[14], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesKnownOneByOnePixel) {
    const std::vector<std::byte> png{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47}, std::byte{0x0D}, std::byte{0x0A},
        std::byte{0x1A}, std::byte{0x0A}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0D},
        std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x08}, std::byte{0x06}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x1F},
        std::byte{0x15}, std::byte{0xC4}, std::byte{0x89}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x0D}, std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}, std::byte{0x78},
        std::byte{0x9C}, std::byte{0x63}, std::byte{0x60}, std::byte{0xF8}, std::byte{0xCF}, std::byte{0xF0},
        std::byte{0x1F}, std::byte{0x00}, std::byte{0x04}, std::byte{0x01}, std::byte{0x01}, std::byte{0xFF},
        std::byte{0x71}, std::byte{0xEB}, std::byte{0x47}, std::byte{0xE5}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
        std::byte{'I'}, std::byte{'E'}, std::byte{'N'}, std::byte{'D'}, std::byte{0xAE}, std::byte{0x42},
        std::byte{0x60}, std::byte{0x82}};
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 1U);
    EXPECT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x00});
    EXPECT_EQ(image.pixels[1], std::byte{0xFF});
    EXPECT_EQ(image.pixels[2], std::byte{0x00});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_AppliesTransparentGray8Key) {
    const std::vector<std::byte> png = MakePngTransparentGray8OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x80});
    EXPECT_EQ(image.pixels[1], std::byte{0x80});
    EXPECT_EQ(image.pixels[2], std::byte{0x80});
    EXPECT_EQ(image.pixels[3], std::byte{0});
}

TEST(PngMetadataTest, DecodePngRgba8Image_AppliesTransparentRgb8Key) {
    const std::vector<std::byte> png = MakePngTransparentRgb8OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0xFF});
    EXPECT_EQ(image.pixels[1], std::byte{0});
    EXPECT_EQ(image.pixels[2], std::byte{0});
    EXPECT_EQ(image.pixels[3], std::byte{0});
}

TEST(PngMetadataTest, DecodePngRgba8Image_AppliesTransparentGray16Key) {
    const std::vector<std::byte> png = MakePngTransparentGray16OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x34});
    EXPECT_EQ(image.pixels[1], std::byte{0x34});
    EXPECT_EQ(image.pixels[2], std::byte{0x34});
    EXPECT_EQ(image.pixels[3], std::byte{0});
}

TEST(PngMetadataTest, DecodePngRgba8Image_AppliesAdam7TransparentGrayKey) {
    const std::vector<std::byte> png = MakePngAdam7TransparentGrayTwoByTwo();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 16U);
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[7], std::byte{0});
    EXPECT_EQ(image.pixels[11], std::byte{0xFF});
    EXPECT_EQ(image.pixels[15], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_AppliesPackedGrayTransparencyKeys) {
    struct Case final {
        std::vector<std::byte> png;
        std::vector<std::byte> expectedAlpha;
    };
    const std::array<Case, 3> cases{{
        {MakePngGray1BitTransparentEightByOne(),
         {std::byte{0xFF}, std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0}}},
        {MakePngGray2BitTransparentFourByOne(),
         {std::byte{0xFF}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF}}},
        {MakePngGray4BitTransparentTwoByOne(), {std::byte{0xFF}, std::byte{0}}},
    }};
    for (const Case& testCase : cases) {
        ASSERT_FALSE(testCase.png.empty());
        PngRgba8Image image;
        ASSERT_TRUE(DecodePngRgba8Image(testCase.png, image));
        ASSERT_EQ(image.pixels.size(), testCase.expectedAlpha.size() * 4U);
        for (std::size_t pixel = 0U; pixel < testCase.expectedAlpha.size(); ++pixel) {
            EXPECT_EQ(image.pixels[pixel * 4U + 3U], testCase.expectedAlpha[pixel]);
        }
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_AppliesAdam7PackedGrayTransparencyKeys) {
    struct Case final {
        std::vector<std::byte> png;
        std::array<std::byte, 4> expectedAlpha;
    };
    const std::array<Case, 3> cases{{
        {MakePngAdam7PackedGray1BitTransparentTwoByTwo(),
         {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}}},
        {MakePngAdam7PackedGray2BitTransparentTwoByTwo(),
         {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0}}},
        {MakePngAdam7PackedGray4BitTransparentTwoByTwo(),
         {std::byte{0xFF}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}}},
    }};
    for (const Case& testCase : cases) {
        ASSERT_FALSE(testCase.png.empty());
        PngRgba8Image image;
        ASSERT_TRUE(DecodePngRgba8Image(testCase.png, image));
        ASSERT_EQ(image.pixels.size(), 16U);
        for (std::size_t pixel = 0U; pixel < testCase.expectedAlpha.size(); ++pixel) {
            EXPECT_EQ(image.pixels[pixel * 4U + 3U], testCase.expectedAlpha[pixel]);
        }
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsPackedGrayTransparencyKeyOutOfRangeAtomically) {
    const std::vector<std::byte> png = MakePngPackedGrayTransparent(
        2U, {std::byte{0}, std::byte{0x1B}}, 4U, 1U, 0U, 4U);
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsMalformedTrnsKeyAtomically) {
    const std::vector<std::byte> png = MakePngOneByOne(0U, {std::byte{0}, std::byte{0x80}}, {}, {}, 8U, 1U, 1U, 0U,
                                                               {std::byte{0x80}});
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, DecodePngRgba8Image_ExpandsRgbToRgba) {
    const std::vector<std::byte> png = MakePngRgbOneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 1U);
    EXPECT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0xFF});
    EXPECT_EQ(image.pixels[1], std::byte{0x00});
    EXPECT_EQ(image.pixels[2], std::byte{0x00});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesPackedGray1BitToRgba) {
    const std::vector<std::byte> png = MakePngGray1BitEightByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 8U);
    ASSERT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 32U);
    const std::array<std::byte, 8> expected{std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                             std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        EXPECT_EQ(image.pixels[pixel * 4U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 1U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 2U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 3U], std::byte{0xFF});
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesPackedGray2BitToRgba) {
    const std::vector<std::byte> png = MakePngGray2BitFourByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 4U);
    ASSERT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 16U);
    const std::array<std::byte, 4> expected{std::byte{0x00}, std::byte{0x55}, std::byte{0xAA}, std::byte{0xFF}};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        EXPECT_EQ(image.pixels[pixel * 4U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 1U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 2U], expected[pixel]);
        EXPECT_EQ(image.pixels[pixel * 4U + 3U], std::byte{0xFF});
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesPackedIndexed1BitToRgba) {
    const std::vector<std::byte> png = MakePngIndexed1BitEightByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 8U);
    ASSERT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 32U);
    const std::array<std::byte, 8> expectedAlpha{std::byte{0xFF}, std::byte{0x80}, std::byte{0xFF}, std::byte{0x80},
                                                  std::byte{0x80}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0x80}};
    for (std::size_t pixel = 0U; pixel < expectedAlpha.size(); ++pixel) {
        const std::byte expectedRed = (pixel == 0U || pixel == 2U || pixel == 5U || pixel == 6U) ? std::byte{0xFF} : std::byte{0};
        const std::byte expectedGreen = expectedRed == std::byte{0xFF} ? std::byte{0} : std::byte{0xFF};
        EXPECT_EQ(image.pixels[pixel * 4U], expectedRed);
        EXPECT_EQ(image.pixels[pixel * 4U + 1U], expectedGreen);
        EXPECT_EQ(image.pixels[pixel * 4U + 2U], std::byte{0});
        EXPECT_EQ(image.pixels[pixel * 4U + 3U], expectedAlpha[pixel]);
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesPackedIndexed2BitToRgba) {
    const std::vector<std::byte> png = MakePngIndexed2BitFourByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 4U);
    ASSERT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 16U);
    const std::array<std::array<std::byte, 4>, 4> expected{{
        {std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF}},
        {std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}},
        {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}},
    }};
    for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            EXPECT_EQ(image.pixels[pixel * 4U + channel], expected[pixel][channel]);
        }
    }
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesPackedIndexed4BitToRgba) {
    const std::vector<std::byte> png = MakePngIndexed4BitTwoByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 8U);
    EXPECT_EQ(image.pixels[0], std::byte{0xFF});
    EXPECT_EQ(image.pixels[1], std::byte{0});
    EXPECT_EQ(image.pixels[2], std::byte{0});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0});
    EXPECT_EQ(image.pixels[5], std::byte{0xFF});
    EXPECT_EQ(image.pixels[6], std::byte{0});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsOutOfRangePackedIndexedPixelAtomically) {
    const std::vector<std::byte> png = MakePngOneByOne(3U, {std::byte{0}, std::byte{0x80}},
                                                           {std::byte{0xFF}, std::byte{0}, std::byte{0}}, {}, 1U, 8U, 1U);
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, DecodePngRgba8Image_DecodesPackedGray4BitToRgba) {
    const std::vector<std::byte> png = MakePngGray4BitTwoByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.width, 2U);
    ASSERT_EQ(image.height, 1U);
    ASSERT_EQ(image.pixels.size(), 8U);
    EXPECT_EQ(image.pixels[0], std::byte{0x11});
    EXPECT_EQ(image.pixels[1], std::byte{0x11});
    EXPECT_EQ(image.pixels[2], std::byte{0x11});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
    EXPECT_EQ(image.pixels[4], std::byte{0xEE});
    EXPECT_EQ(image.pixels[5], std::byte{0xEE});
    EXPECT_EQ(image.pixels[6], std::byte{0xEE});
    EXPECT_EQ(image.pixels[7], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsTruncatedPackedGrayRowAtomically) {
    const std::vector<std::byte> png = MakePngOneByOne(0U, {std::byte{0}}, {}, {}, 4U, 2U, 1U);
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, DecodePngRgba8Image_ExpandsGrayscaleAlphaToRgba) {
    const std::vector<std::byte> png = MakePngGrayAlphaOneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x80});
    EXPECT_EQ(image.pixels[1], std::byte{0x80});
    EXPECT_EQ(image.pixels[2], std::byte{0x80});
    EXPECT_EQ(image.pixels[3], std::byte{0xC0});
}

TEST(PngMetadataTest, DecodePngRgba8Image_ExpandsGrayscaleToRgba) {
    const std::vector<std::byte> png = MakePngGrayOneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x80});
    EXPECT_EQ(image.pixels[1], std::byte{0x80});
    EXPECT_EQ(image.pixels[2], std::byte{0x80});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsRgb16ToRgba8) {
    const std::vector<std::byte> png = MakePngRgb16OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0xAB});
    EXPECT_EQ(image.pixels[2], std::byte{0xEF});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsGray16ToRgba8) {
    const std::vector<std::byte> png = MakePngGray16OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x34});
    EXPECT_EQ(image.pixels[1], std::byte{0x34});
    EXPECT_EQ(image.pixels[2], std::byte{0x34});
    EXPECT_EQ(image.pixels[3], std::byte{0xFF});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsGrayAlpha16ToRgba8) {
    const std::vector<std::byte> png = MakePngGrayAlpha16OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x34});
    EXPECT_EQ(image.pixels[1], std::byte{0x34});
    EXPECT_EQ(image.pixels[2], std::byte{0x34});
    EXPECT_EQ(image.pixels[3], std::byte{0xAB});
}

TEST(PngMetadataTest, DecodePngRgba8Image_DownconvertsRgba16ToRgba8) {
    const std::vector<std::byte> png = MakePngRgba16OneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x12});
    EXPECT_EQ(image.pixels[1], std::byte{0xAB});
    EXPECT_EQ(image.pixels[2], std::byte{0x56});
    EXPECT_EQ(image.pixels[3], std::byte{0x9A});
}

TEST(PngMetadataTest, DecodePngRgba8Image_ExpandsIndexedToRgba) {
    const std::vector<std::byte> png = MakePngIndexedOneByOne();
    ASSERT_FALSE(png.empty());
    PngRgba8Image image;
    ASSERT_TRUE(DecodePngRgba8Image(png, image));
    ASSERT_EQ(image.pixels.size(), 4U);
    EXPECT_EQ(image.pixels[0], std::byte{0x00});
    EXPECT_EQ(image.pixels[1], std::byte{0xFF});
    EXPECT_EQ(image.pixels[2], std::byte{0x00});
    EXPECT_EQ(image.pixels[3], std::byte{0x80});
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsOutOfRangeIndexedPixelAtomically) {
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(MakePngIndexedOneByOne(std::byte{2}), image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, DecodePngRgba8Image_RejectsCorruptOrUnsupportedInputAtomically) {
    const std::vector<std::byte> png{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47}, std::byte{0x0D}, std::byte{0x0A},
        std::byte{0x1A}, std::byte{0x0A}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0D},
        std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x08}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x1F},
        std::byte{0x15}, std::byte{0xC4}, std::byte{0x89}};
    PngRgba8Image image{7U, 8U, {std::byte{0xAA}}};
    EXPECT_FALSE(DecodePngRgba8Image(png, image));
    EXPECT_EQ(image.width, 7U);
    EXPECT_EQ(image.height, 8U);
    EXPECT_EQ(image.pixels, std::vector<std::byte>({std::byte{0xAA}}));
}

TEST(PngMetadataTest, ParsePngMetadata_RejectsMalformedIhdrFacts) {
    std::vector<std::byte> badSignature = MakePngIhdr();
    badSignature[0] = std::byte{'X'};
    EXPECT_FALSE(ParsePngMetadata(badSignature).has_value());

    std::vector<std::byte> badDepth = MakePngIhdr(1U, 1U, 4U, 2U);
    EXPECT_FALSE(ParsePngMetadata(badDepth).has_value());

    std::vector<std::byte> badMethod = MakePngIhdr(1U, 1U, 8U, 6U);
    badMethod[26] = std::byte{1};
    EXPECT_FALSE(ParsePngMetadata(badMethod).has_value());

    std::vector<std::byte> truncated = MakePngIhdr();
    truncated.resize(20U);
    EXPECT_FALSE(ParsePngMetadata(truncated).has_value());
}

} // namespace
} // namespace UVE::Asset::Tests
