// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "font_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> SfntBytes(const std::uint32_t magic) {
    return {static_cast<std::byte>((magic >> 24U) & 0xFFU), static_cast<std::byte>((magic >> 16U) & 0xFFU),
            static_cast<std::byte>((magic >> 8U) & 0xFFU), static_cast<std::byte>(magic & 0xFFU),
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
}

void RemoveFiles(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        std::filesystem::remove(path);
    }
}

TEST(FontAssetTest, IsValid_AcceptsEverySfntMagicAndRejectsTheRest) {
    EXPECT_TRUE(IsFontAssetValid(FontAsset{"Family", SfntBytes(0x00010000U)}));
    EXPECT_TRUE(IsFontAssetValid(FontAsset{"Family", SfntBytes(0x4F54544FU)})); // OTTO.
    EXPECT_TRUE(IsFontAssetValid(FontAsset{"Family", SfntBytes(0x74727565U)})); // true.
    EXPECT_TRUE(IsFontAssetValid(FontAsset{"Family", SfntBytes(0x74797031U)})); // typ1.
    EXPECT_FALSE(IsFontAssetValid(FontAsset{"Family", SfntBytes(0xDEADBEEFU)}));
    EXPECT_FALSE(IsFontAssetValid(FontAsset{"Family", {}}));
    EXPECT_FALSE(IsFontAssetValid(FontAsset{"Family", {std::byte{0x00}, std::byte{0x01}}}));
    EXPECT_FALSE(
        IsFontAssetValid(FontAsset{std::string(kMaximumFontFamilyNameBytes + 1U, 'x'),
                                         SfntBytes(0x00010000U)}));
}

TEST(FontAssetTest, SaveThenLoad_RoundTripsFamilyAndBytes) {
    const std::filesystem::path path = "uve_font_asset_tests_roundtrip.uvfont";
    RemoveFiles({path});
    const FontAsset font{"Liberation Sans", SfntBytes(0x00010000U)};
    ASSERT_TRUE(SaveFontAsset(font, path));
    FontAsset loaded;
    ASSERT_TRUE(LoadFontAsset(path, loaded));
    EXPECT_EQ(loaded.familyName, "Liberation Sans");
    EXPECT_EQ(loaded.fontData, font.fontData);
    RemoveFiles({path});
}

TEST(FontAssetTest, Save_RefusesInvalidAssetsWithoutWriting) {
    const std::filesystem::path path = "uve_font_asset_tests_invalid.uvfont";
    RemoveFiles({path});
    EXPECT_FALSE(SaveFontAsset(FontAsset{"Family", {}}, path));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(FontAssetTest, Load_RejectsWrongKindAndTruncatedPayloads) {
    const std::filesystem::path wrongKind = "uve_font_asset_tests_wrongkind.uvfont";
    const std::filesystem::path truncated = "uve_font_asset_tests_truncated.uvfont";
    RemoveFiles({wrongKind, truncated});
    ASSERT_TRUE(WriteUveFile(wrongKind, AssetKind::Blob, {std::byte{0x01}}));
    ASSERT_TRUE(WriteUveFile(truncated, AssetKind::Font, {std::byte{0x01}, std::byte{0x02}}));
    FontAsset loaded;
    EXPECT_FALSE(LoadFontAsset(wrongKind, loaded));
    EXPECT_TRUE(loaded.familyName.empty());
    EXPECT_FALSE(LoadFontAsset(truncated, loaded));
    RemoveFiles({wrongKind, truncated});
}

} // namespace
} // namespace UVE::Asset::Tests
