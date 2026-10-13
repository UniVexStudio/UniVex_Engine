// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/font_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> SfntBytesUVE(const std::uint32_t magic) {
    return {static_cast<std::byte>((magic >> 24U) & 0xFFU), static_cast<std::byte>((magic >> 16U) & 0xFFU),
            static_cast<std::byte>((magic >> 8U) & 0xFFU), static_cast<std::byte>(magic & 0xFFU),
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
}

void RemoveFilesUVE(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        std::filesystem::remove(path);
    }
}

TEST(FontAssetUVETest, IsValid_AcceptsEverySfntMagicAndRejectsTheRest) {
    EXPECT_TRUE(IsFontAssetValidUVE(FontAssetUVE{"Family", SfntBytesUVE(0x00010000U)}));
    EXPECT_TRUE(IsFontAssetValidUVE(FontAssetUVE{"Family", SfntBytesUVE(0x4F54544FU)})); // OTTO.
    EXPECT_TRUE(IsFontAssetValidUVE(FontAssetUVE{"Family", SfntBytesUVE(0x74727565U)})); // true.
    EXPECT_TRUE(IsFontAssetValidUVE(FontAssetUVE{"Family", SfntBytesUVE(0x74797031U)})); // typ1.
    EXPECT_FALSE(IsFontAssetValidUVE(FontAssetUVE{"Family", SfntBytesUVE(0xDEADBEEFU)}));
    EXPECT_FALSE(IsFontAssetValidUVE(FontAssetUVE{"Family", {}}));
    EXPECT_FALSE(IsFontAssetValidUVE(FontAssetUVE{"Family", {std::byte{0x00}, std::byte{0x01}}}));
    EXPECT_FALSE(
        IsFontAssetValidUVE(FontAssetUVE{std::string(kMaximumFontFamilyNameBytesUVE + 1U, 'x'),
                                         SfntBytesUVE(0x00010000U)}));
}

TEST(FontAssetUVETest, SaveThenLoad_RoundTripsFamilyAndBytes) {
    const std::filesystem::path path = "uve_font_asset_tests_roundtrip.uvfont";
    RemoveFilesUVE({path});
    const FontAssetUVE font{"Liberation Sans", SfntBytesUVE(0x00010000U)};
    ASSERT_TRUE(SaveFontAssetUVE(font, path));
    FontAssetUVE loaded;
    ASSERT_TRUE(LoadFontAssetUVE(path, loaded));
    EXPECT_EQ(loaded.familyName, "Liberation Sans");
    EXPECT_EQ(loaded.fontData, font.fontData);
    RemoveFilesUVE({path});
}

TEST(FontAssetUVETest, Save_RefusesInvalidAssetsWithoutWriting) {
    const std::filesystem::path path = "uve_font_asset_tests_invalid.uvfont";
    RemoveFilesUVE({path});
    EXPECT_FALSE(SaveFontAssetUVE(FontAssetUVE{"Family", {}}, path));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(FontAssetUVETest, Load_RejectsWrongKindAndTruncatedPayloads) {
    const std::filesystem::path wrongKind = "uve_font_asset_tests_wrongkind.uvfont";
    const std::filesystem::path truncated = "uve_font_asset_tests_truncated.uvfont";
    RemoveFilesUVE({wrongKind, truncated});
    ASSERT_TRUE(WriteUveFileUVE(wrongKind, AssetKindUVE::Blob, {std::byte{0x01}}));
    ASSERT_TRUE(WriteUveFileUVE(truncated, AssetKindUVE::Font, {std::byte{0x01}, std::byte{0x02}}));
    FontAssetUVE loaded;
    EXPECT_FALSE(LoadFontAssetUVE(wrongKind, loaded));
    EXPECT_TRUE(loaded.familyName.empty());
    EXPECT_FALSE(LoadFontAssetUVE(truncated, loaded));
    RemoveFilesUVE({wrongKind, truncated});
}

} // namespace
} // namespace UVE::Asset::Tests
