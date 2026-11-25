// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "tga_metadata.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

void AppendU16LittleEndian(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

[[nodiscard]] std::vector<std::byte> MakeTga24TwoByTwoBottomLeft() {
    std::vector<std::byte> tga;
    tga.reserve(30U);
    tga.insert(tga.end(), 12U, std::byte{0});
    tga[2] = std::byte{2};
    AppendU16LittleEndian(tga, 2U);
    AppendU16LittleEndian(tga, 2U);
    tga.push_back(std::byte{24});
    tga.push_back(std::byte{0});
    // Bottom-left storage, BGR pixels with no row padding.
    const std::byte pixels[] = {
        // Bottom row: blue, white.
        std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
        // Top row: red, green.
        std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}};
    tga.insert(tga.end(), std::begin(pixels), std::end(pixels));
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga24RleTwoByTwoTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(28U);
    tga.insert(tga.end(), 12U, std::byte{0});
    tga[2] = std::byte{10};
    AppendU16LittleEndian(tga, 2U);
    AppendU16LittleEndian(tga, 2U);
    tga.push_back(std::byte{24});
    tga.push_back(std::byte{0x20});
    // One raw packet for red/green, followed by one run packet for two blue pixels.
    tga.insert(tga.end(), {std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                           std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x81},
                           std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga8GrayscaleTwoByOneBottomRight() {
    std::vector<std::byte> tga;
    tga.reserve(20U);
    tga.insert(tga.end(), 12U, std::byte{0});
    tga[2] = std::byte{3};
    AppendU16LittleEndian(tga, 2U);
    AppendU16LittleEndian(tga, 1U);
    tga.push_back(std::byte{8});
    tga.push_back(std::byte{0x10});
    tga.insert(tga.end(), {std::byte{0x11}, std::byte{0xEE}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga8GrayscaleRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(20U);
    tga.insert(tga.end(), 12U, std::byte{0});
    tga[2] = std::byte{11};
    AppendU16LittleEndian(tga, 3U);
    AppendU16LittleEndian(tga, 1U);
    tga.push_back(std::byte{8});
    tga.push_back(std::byte{0x20});
    tga.insert(tga.end(), {std::byte{0x82}, std::byte{0x77}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga15TrueColorTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{2};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{15};
    tga[17] = std::byte{0x20};
    // Little-endian BGR555: opaque red, opaque blue with no attribute bit.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x7C}, std::byte{0x1F}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga15TrueColorRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(23U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{10};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{15};
    tga[17] = std::byte{0x20};
    // One opaque green BGR555 sample in a three-pixel run.
    tga.insert(tga.end(), {std::byte{0x82}, std::byte{0xE0}, std::byte{0x03}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16TrueColorTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{2};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x21};
    // Little-endian BGR5551: opaque red, transparent blue.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0xFC}, std::byte{0x1F}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16TrueColor565TwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{2};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x20};
    // Little-endian BGR565: opaque red, opaque blue.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0xF8}, std::byte{0x1F}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16TrueColor565RleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(23U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{10};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x20};
    // One opaque green BGR565 sample in a three-pixel run.
    tga.insert(tga.end(), {std::byte{0x82}, std::byte{0xE0}, std::byte{0x07}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16TrueColorRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(23U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{10};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x21};
    // One opaque green BGR5551 sample in a three-pixel run.
    tga.insert(tga.end(), {std::byte{0x82}, std::byte{0xE0}, std::byte{0x83}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16GrayscaleAlphaTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{3};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x20};
    tga.insert(tga.end(), {std::byte{0x40}, std::byte{0x10}, std::byte{0xC0}, std::byte{0x80}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16GrayscaleAlphaRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{11};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x20};
    tga.insert(tga.end(), {std::byte{0x82}, std::byte{0x7F}, std::byte{0x55}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga8PaletteTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(26U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{1};
    tga[5] = std::byte{2};
    tga[7] = std::byte{24};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x20};
    // Palette entries: red, green (BGR order), followed by palette indices 0 and 1.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00},
                           std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga8PaletteRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(23U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{9};
    tga[5] = std::byte{1};
    tga[7] = std::byte{24};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x20};
    // One blue palette entry and one run packet containing three index-zero pixels.
    tga.insert(tga.end(), {std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x82}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga32PaletteTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(26U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{1};
    tga[5] = std::byte{2};
    tga[7] = std::byte{32};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x28};
    // BGRA palette entries: red at alpha 0x40, blue at alpha 0x90, followed by indices 0 and 1.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x40},
                           std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x90},
                           std::byte{0x00}, std::byte{0x01}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga32PaletteRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(24U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{9};
    tga[5] = std::byte{1};
    tga[7] = std::byte{32};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x28};
    // One BGRA green palette entry at alpha 0x66 and one run packet of three index-zero pixels.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x66},
                           std::byte{0x82}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16IndexedPixelsTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(28U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{1};
    tga[3] = std::byte{0x2C};
    tga[4] = std::byte{0x01};
    tga[5] = std::byte{2};
    tga[7] = std::byte{24};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x20};
    // 16-bit little-endian indices 300 and 301 select red and blue BGR palette entries.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                           std::byte{0xFF}, std::byte{0x00}, std::byte{0x00},
                           std::byte{0x2C}, std::byte{0x01}, std::byte{0x2D}, std::byte{0x01}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16IndexedPixelsRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(26U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{9};
    tga[3] = std::byte{0x2C};
    tga[4] = std::byte{0x01};
    tga[5] = std::byte{1};
    tga[7] = std::byte{24};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{16};
    tga[17] = std::byte{0x20};
    // One green BGR palette entry and a run packet carrying 16-bit index 300.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0xFF}, std::byte{0x00},
                           std::byte{0x82}, std::byte{0x2C}, std::byte{0x01}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga15PaletteTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(24U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{1};
    tga[5] = std::byte{2};
    tga[7] = std::byte{15};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x20};
    // BGR555 palette entries: opaque red, opaque blue, followed by indices 0 and 1.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x7C}, std::byte{0x1F}, std::byte{0x00},
                           std::byte{0x00}, std::byte{0x01}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga15PaletteRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{9};
    tga[5] = std::byte{1};
    tga[7] = std::byte{15};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x20};
    // One BGR555 green palette entry and one run packet containing three index-zero pixels.
    tga.insert(tga.end(), {std::byte{0xE0}, std::byte{0x03}, std::byte{0x82}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16PaletteTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{1};
    tga[5] = std::byte{2};
    tga[7] = std::byte{16};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x21};
    // BGR5551 palette entries: opaque red, transparent blue, followed by indices 0 and 1.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0xFC}, std::byte{0x1F}, std::byte{0x00},
                           std::byte{0x00}, std::byte{0x01}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga16PaletteRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(22U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[1] = std::byte{1};
    tga[2] = std::byte{9};
    tga[5] = std::byte{1};
    tga[7] = std::byte{16};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{8};
    tga[17] = std::byte{0x21};
    // One opaque green BGR5551 palette entry and one run packet of three index-zero pixels.
    tga.insert(tga.end(), {std::byte{0xE0}, std::byte{0x83}, std::byte{0x82}, std::byte{0x00}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga32TrueColorAlphaTwoByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(26U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{2};
    tga[12] = std::byte{2};
    tga[14] = std::byte{1};
    tga[16] = std::byte{32};
    tga[17] = std::byte{0x28};
    // BGRA true-color pixels: red at alpha 0x40, blue at alpha 0x90.
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x40},
                           std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x90}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga32TrueColorAlphaRleThreeByOneTopLeft() {
    std::vector<std::byte> tga;
    tga.reserve(23U);
    tga.insert(tga.end(), 18U, std::byte{0});
    tga[2] = std::byte{10};
    tga[12] = std::byte{3};
    tga[14] = std::byte{1};
    tga[16] = std::byte{32};
    tga[17] = std::byte{0x28};
    // One BGRA red sample at alpha 0x66 in a three-pixel run.
    tga.insert(tga.end(), {std::byte{0x82}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x66}});
    return tga;
}

[[nodiscard]] std::vector<std::byte> MakeTga32OneByTwoTopRight() {
    std::vector<std::byte> tga;
    tga.reserve(26U);
    tga.insert(tga.end(), 12U, std::byte{0});
    tga[2] = std::byte{2};
    AppendU16LittleEndian(tga, 1U);
    AppendU16LittleEndian(tga, 2U);
    tga.push_back(std::byte{32});
    tga.push_back(std::byte{0x30});
    // Top-right storage; source alpha is intentionally ignored for opaque output.
    const std::byte pixels[] = {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x12},
                                std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x34}};
    tga.insert(tga.end(), std::begin(pixels), std::end(pixels));
    return tga;
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesBottomLeft24BitToTopDownOpaqueRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga24TwoByTwoBottomLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 2U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF},
                                 std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle24BitPacketsToTopDownOpaqueRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga24RleTwoByTwoTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 2U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes8BitGrayscaleWithOriginToOpaqueRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga8GrayscaleTwoByOneBottomRight(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE}, std::byte{0xFF},
                                 std::byte{0x11}, std::byte{0x11}, std::byte{0x11}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle8BitGrayscaleToOpaqueRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga8GrayscaleRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x77}, std::byte{0x77}, std::byte{0x77}, std::byte{0xFF},
                                 std::byte{0x77}, std::byte{0x77}, std::byte{0x77}, std::byte{0xFF},
                                 std::byte{0x77}, std::byte{0x77}, std::byte{0x77}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes16BitTrueColor565ToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16TrueColor565TwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle16BitTrueColor565ToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16TrueColor565RleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes15BitTrueColorToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga15TrueColorTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle15BitTrueColorToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga15TrueColorRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes16BitTrueColor5551ToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16TrueColorTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle16BitTrueColor5551ToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16TrueColorRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes16BitGrayscaleAlphaToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16GrayscaleAlphaTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x40}, std::byte{0x40}, std::byte{0x40}, std::byte{0x10},
                                 std::byte{0xC0}, std::byte{0xC0}, std::byte{0xC0}, std::byte{0x80}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle16BitGrayscaleAlphaToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16GrayscaleAlphaRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x7F}, std::byte{0x7F}, std::byte{0x7F}, std::byte{0x55},
                                 std::byte{0x7F}, std::byte{0x7F}, std::byte{0x7F}, std::byte{0x55},
                                 std::byte{0x7F}, std::byte{0x7F}, std::byte{0x7F}, std::byte{0x55}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes8BitPaletteToTopDownOpaqueRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga8PaletteTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle8BitPaletteToOpaqueRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga8PaletteRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes32BitPaletteAlphaToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga32PaletteTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x90}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle32BitPaletteAlphaToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga32PaletteRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x66},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x66},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x66}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes16BitIndexedPixelsToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16IndexedPixelsTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle16BitIndexedPixelsToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16IndexedPixelsRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes15BitPaletteToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga15PaletteTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle15BitPaletteToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga15PaletteRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes16BitPaletteToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16PaletteTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle16BitPaletteToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga16PaletteRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF},
                                 std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_Decodes32BitTrueColorAlphaToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga32TrueColorAlphaTwoByOneTopLeft(), image));
    EXPECT_EQ(image.width, 2U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
                                 std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x90}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesRle32BitTrueColorAlphaToRgba) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga32TrueColorAlphaRleThreeByOneTopLeft(), image));
    EXPECT_EQ(image.width, 3U);
    EXPECT_EQ(image.height, 1U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x66},
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x66},
                                 std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x66}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_DecodesTopRight32BitWithOpaqueAlpha) {
    TgaRgba8Image image;
    ASSERT_TRUE(DecodeTgaRgba8Image(MakeTga32OneByTwoTopRight(), image));
    EXPECT_EQ(image.width, 1U);
    EXPECT_EQ(image.height, 2U);
    EXPECT_EQ(image.pixels, (std::vector<std::byte>{std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
                                                      std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}}));
}

TEST(TgaMetadataTest, DecodeTgaRgba8Image_RejectsMalformedInputAtomically) {
    const TgaRgba8Image original{1U, 1U, {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}}};
    TgaRgba8Image image = original;
    std::vector<std::byte> truncated = MakeTga24TwoByTwoBottomLeft();
    truncated.resize(29U);
    EXPECT_FALSE(DecodeTgaRgba8Image(truncated, image));
    EXPECT_EQ(image.width, original.width);
    EXPECT_EQ(image.height, original.height);
    EXPECT_EQ(image.pixels, original.pixels);
    std::vector<std::byte> invalidDescriptor = MakeTga24TwoByTwoBottomLeft();
    invalidDescriptor[17] = std::byte{0x40};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalidDescriptor, image));
    EXPECT_EQ(image.pixels, original.pixels);
    std::vector<std::byte> truncatedRle = MakeTga24RleTwoByTwoTopLeft();
    truncatedRle.resize(truncatedRle.size() - 1U);
    EXPECT_FALSE(DecodeTgaRgba8Image(truncatedRle, image));
    std::vector<std::byte> invalidGrayscaleDepth = MakeTga8GrayscaleTwoByOneBottomRight();
    invalidGrayscaleDepth[16] = std::byte{24};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalidGrayscaleDepth, image));
    std::vector<std::byte> invalidTrueColorDescriptor = MakeTga16TrueColorTwoByOneTopLeft();
    invalidTrueColorDescriptor[17] = std::byte{0x22};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalidTrueColorDescriptor, image));
    std::vector<std::byte> invalid15BitDescriptor = MakeTga15TrueColorTwoByOneTopLeft();
    invalid15BitDescriptor[17] = std::byte{0x21};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalid15BitDescriptor, image));
    std::vector<std::byte> invalidPaletteDescriptor = MakeTga16PaletteTwoByOneTopLeft();
    invalidPaletteDescriptor[17] = std::byte{0x22};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalidPaletteDescriptor, image));
    std::vector<std::byte> invalid15BitPaletteDescriptor = MakeTga15PaletteTwoByOneTopLeft();
    invalid15BitPaletteDescriptor[17] = std::byte{0x21};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalid15BitPaletteDescriptor, image));
    std::vector<std::byte> invalidPaletteIndex = MakeTga8PaletteTwoByOneTopLeft();
    invalidPaletteIndex[25] = std::byte{2};
    EXPECT_FALSE(DecodeTgaRgba8Image(invalidPaletteIndex, image));
    std::vector<std::byte> truncatedPalette = MakeTga8PaletteTwoByOneTopLeft();
    truncatedPalette.resize(20U);
    EXPECT_FALSE(DecodeTgaRgba8Image(truncatedPalette, image));
    std::vector<std::byte> oversizedImageId = MakeTga8PaletteTwoByOneTopLeft();
    oversizedImageId[0] = std::byte{0xFF};
    EXPECT_FALSE(DecodeTgaRgba8Image(oversizedImageId, image));
    EXPECT_EQ(image.width, original.width);
    EXPECT_EQ(image.height, original.height);
    EXPECT_EQ(image.pixels, original.pixels);
}

} // namespace
} // namespace UVE::Asset::Tests
