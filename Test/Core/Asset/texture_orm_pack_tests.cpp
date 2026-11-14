// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "texture_asset.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] TextureAssetUVE GreyUVE(const std::uint32_t width, const std::uint32_t height, const std::uint8_t value) {
    TextureAssetUVE texture;
    texture.width = width;
    texture.height = height;
    texture.format = TextureAssetFormatUVE::RGBA8Unorm;
    texture.pixels.assign(static_cast<std::size_t>(width) * height * 4U, std::byte{value});
    return texture;
}

TEST(TextureOrmPackUVETest, SeparateMapsLandInTheirChannels) {
    const TextureAssetUVE roughness = GreyUVE(4U, 4U, 100U);
    const TextureAssetUVE metallic = GreyUVE(2U, 2U, 200U);
    TextureAssetUVE orm;
    ASSERT_TRUE(PackOrmTextureUVE(nullptr, &roughness, &metallic, orm));
    // As large as the largest map, with a mip chain down to 1x1.
    EXPECT_EQ(orm.width, 4U);
    EXPECT_EQ(orm.height, 4U);
    EXPECT_EQ(orm.mipLevels.size(), 2U);
    EXPECT_EQ(orm.colorSpace, TextureAssetColorSpaceUVE::Linear);
    for (std::size_t at = 0U; at < orm.pixels.size(); at += 4U) {
        EXPECT_EQ(orm.pixels[at], std::byte{255}) << "no occlusion map: white";
        EXPECT_EQ(orm.pixels[at + 1U], std::byte{100}) << "roughness in green";
        EXPECT_EQ(orm.pixels[at + 2U], std::byte{200}) << "metallic in blue";
    }
}

TEST(TextureOrmPackUVETest, NoMapOrAnUnreadableOneIsRefused) {
    TextureAssetUVE orm;
    EXPECT_FALSE(PackOrmTextureUVE(nullptr, nullptr, nullptr, orm));
    TextureAssetUVE hdr = GreyUVE(2U, 2U, 0U);
    hdr.format = TextureAssetFormatUVE::RGBA16Float;
    EXPECT_FALSE(PackOrmTextureUVE(&hdr, nullptr, nullptr, orm));
}

} // namespace
} // namespace UVE::Asset
