// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "texture_asset.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] TextureAsset Grey(const std::uint32_t width, const std::uint32_t height, const std::uint8_t value) {
    TextureAsset texture;
    texture.width = width;
    texture.height = height;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.pixels.assign(static_cast<std::size_t>(width) * height * 4U, std::byte{value});
    return texture;
}

TEST(TextureOrmPackTest, SeparateMapsLandInTheirChannels) {
    const TextureAsset roughness = Grey(4U, 4U, 100U);
    const TextureAsset metallic = Grey(2U, 2U, 200U);
    TextureAsset orm;
    ASSERT_TRUE(PackOrmTexture(nullptr, &roughness, &metallic, orm));
    // As large as the largest map, with a mip chain down to 1x1.
    EXPECT_EQ(orm.width, 4U);
    EXPECT_EQ(orm.height, 4U);
    EXPECT_EQ(orm.mipLevels.size(), 2U);
    EXPECT_EQ(orm.colorSpace, TextureAssetColorSpace::Linear);
    for (std::size_t at = 0U; at < orm.pixels.size(); at += 4U) {
        EXPECT_EQ(orm.pixels[at], std::byte{255}) << "no occlusion map: white";
        EXPECT_EQ(orm.pixels[at + 1U], std::byte{100}) << "roughness in green";
        EXPECT_EQ(orm.pixels[at + 2U], std::byte{200}) << "metallic in blue";
    }
}

TEST(TextureOrmPackTest, NoMapOrAnUnreadableOneIsRefused) {
    TextureAsset orm;
    EXPECT_FALSE(PackOrmTexture(nullptr, nullptr, nullptr, orm));
    TextureAsset hdr = Grey(2U, 2U, 0U);
    hdr.format = TextureAssetFormat::RGBA16Float;
    EXPECT_FALSE(PackOrmTexture(&hdr, nullptr, nullptr, orm));
}

} // namespace
} // namespace UVE::Asset
