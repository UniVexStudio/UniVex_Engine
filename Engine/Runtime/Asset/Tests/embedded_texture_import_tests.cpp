// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Tests for the part of a model import that turns somebody else's images into this engine's
// textures: bytes carried inside a model file, and files that sit beside it.
//
// Both are the difference between "the character imported" and "the character imported and looks
// like the character". A model that names `C:/Users/someone/Textures/Wood.PNG` was authored on
// another machine, with another case-fold, and that name has to be resolved into something real - or
// honestly reported as missing - without ever guessing.

#include "import_helpers.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_image_fixtures.h"
#include "texture_asset.h"

namespace UVE::Asset::Tests {
namespace {

/// A directory under the test's own scratch space, removed when the test ends so a failed assertion
/// cannot leave a tree behind for the next run to trip over.
class ScratchDirectory final {
public:
    explicit ScratchDirectory(const std::string_view name) {
        m_path = std::filesystem::temp_directory_path() / ("uve_embedded_texture_" + std::string{name});
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::create_directories(m_path, error);
    }
    ~ScratchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

    void Write(const std::filesystem::path& relative, const std::vector<std::byte>& bytes) const {
        const std::filesystem::path target = m_path / relative;
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        std::ofstream file(target, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << target.string();
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file.good());
    }

private:
    std::filesystem::path m_path;
};

TEST(EmbeddedTextureImportTest, PngBytesThatArrivedInsideAModelDecodeToTheirRealPixels) {
    // The fixture is a 2x1 image: an opaque red texel then an opaque white one. Asserting the pixels
    // rather than "it returned true" is what makes this a test of decoding instead of plumbing.
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8();
    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Srgb;
    settings.usage = TextureUsage::Color;

    TextureAsset texture;
    ASSERT_TRUE(Detail::DecodeImageBytes(png, settings, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.format, TextureAssetFormat::RGBA8Unorm);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);
    EXPECT_EQ(texture.usage, TextureUsage::Color);
    ASSERT_GE(texture.pixels.size(), 8U);
    const auto channel = [&texture](const std::size_t index) {
        return static_cast<unsigned int>(static_cast<unsigned char>(texture.pixels[index]));
    };
    EXPECT_EQ(channel(0U), 255U) << "first texel red";
    EXPECT_EQ(channel(1U), 0U) << "first texel green";
    EXPECT_EQ(channel(2U), 0U) << "first texel blue";
    EXPECT_EQ(channel(3U), 255U) << "first texel alpha";
    EXPECT_EQ(channel(4U), 255U) << "second texel red";
    EXPECT_EQ(channel(5U), 255U) << "second texel green";
    EXPECT_EQ(channel(6U), 255U) << "second texel blue";
    EXPECT_EQ(channel(7U), 255U) << "second texel alpha";
    EXPECT_TRUE(IsTextureAssetValid(texture));
}

TEST(EmbeddedTextureImportTest, ANormalMapSlotIsLinearAndCarriesNormalUsage) {
    // The same bytes under a different policy: this is the import bug that makes a whole character
    // look wrong (a normal map read as sRGB bends every surface), so the policy is asserted, not
    // assumed.
    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Linear;
    settings.usage = TextureUsage::Normal;

    TextureAsset texture;
    ASSERT_TRUE(Detail::DecodeImageBytes(UVE::Tests::MakePngTwoByOneRgba8(), settings, texture));
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Linear);
    EXPECT_EQ(texture.usage, TextureUsage::Normal);
}

TEST(EmbeddedTextureImportTest, BytesThatAreNotAnImageAreRefusedRatherThanGuessedAt) {
    TextureImportSettings settings;
    TextureAsset texture;
    const std::vector<std::byte> text{std::byte{'n'}, std::byte{'o'}, std::byte{'t'}};
    EXPECT_FALSE(Detail::DecodeImageBytes(text, settings, texture));
    EXPECT_FALSE(Detail::DecodeImageBytes({}, settings, texture));
    // A truncated PNG keeps its signature and must still fail, not decode into noise.
    std::vector<std::byte> truncated = UVE::Tests::MakePngTwoByOneRgba8();
    truncated.resize(20U);
    EXPECT_FALSE(Detail::DecodeImageBytes(truncated, settings, texture));
}

TEST(EmbeddedTextureImportTest, AnAuthoredWindowsPathResolvesOnlyByTheNameAtItsEnd) {
    // No machine in this project has `C:/Users/artist/Textures`, and following that path verbatim is
    // how a model from another computer ends up with no textures at all.
    const ScratchDirectory scratch{"resolve"};
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8();
    scratch.Write("textures/wood_diffuse.png", png);
    scratch.Write("model.fbx", {});

    const auto resolved =
        Detail::ResolveAuthoredTextureFile(scratch.Path(), "model", "C:/Users/artist/Textures/wood_diffuse.png");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->filename().string(), "wood_diffuse.png");
    EXPECT_TRUE(std::filesystem::is_regular_file(*resolved));
}

TEST(EmbeddedTextureImportTest, AReferenceIsMatchedIgnoringCaseBecauseTheExporterWasOnWindows) {
    const ScratchDirectory scratch{"case"};
    scratch.Write("Textures/Wood_Diffuse.PNG", UVE::Tests::MakePngTwoByOneRgba8());
    const auto resolved = Detail::ResolveAuthoredTextureFile(scratch.Path(), "model", "wood_diffuse.png");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->filename().string(), "Wood_Diffuse.PNG");
}

TEST(EmbeddedTextureImportTest, ATextureInTheFbxSiblingFolderIsFoundToo) {
    // `<model>.fbm/` is where an FBX exporter that copies its textures next to the model puts them.
    const ScratchDirectory scratch{"fbm"};
    scratch.Write("model.fbm/wood.png", UVE::Tests::MakePngTwoByOneRgba8());
    const auto resolved = Detail::ResolveAuthoredTextureFile(scratch.Path(), "model", "wood.png");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->parent_path().filename().string(), "model.fbm");
}

TEST(EmbeddedTextureImportTest, AMissingTextureIsReportedAsMissingAndNothingIsInvented) {
    const ScratchDirectory scratch{"missing"};
    EXPECT_FALSE(Detail::ResolveAuthoredTextureFile(scratch.Path(), "model", "nope.png").has_value());
    EXPECT_FALSE(Detail::ResolveAuthoredTextureFile(scratch.Path(), "model", "").has_value());
    // A directory with the right name is not a texture.
    std::error_code error;
    std::filesystem::create_directories(scratch.Path() / "textures" / "folder.png", error);
    EXPECT_FALSE(Detail::ResolveAuthoredTextureFile(scratch.Path(), "model", "folder.png").has_value());
}

TEST(EmbeddedTextureImportTest, AnEmbeddedImageIsPublishedAsAUvtexThatLoadsBackAsTheSamePixels) {
    // The whole path: bytes that arrived inside a model -> a `.uvtex` on disk -> read back. If any
    // step re-encoded, dropped mips or swapped channels, this is where it shows.
    const ScratchDirectory scratch{"publish"};
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8();
    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Srgb;
    settings.usage = TextureUsage::Color;

    const std::filesystem::path destination = scratch.Path() / "Agent_material_0_albedo.uvtex";
    ASSERT_TRUE(Detail::PublishEmbeddedTexture(destination, png, settings, "TestImporter", "a test image"));
    ASSERT_TRUE(std::filesystem::is_regular_file(destination));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(destination, loaded));
    EXPECT_EQ(loaded.width, 2U);
    EXPECT_EQ(loaded.height, 1U);
    ASSERT_GE(loaded.pixels.size(), 8U);
    EXPECT_EQ(loaded.pixels[0], std::byte{255});
    EXPECT_EQ(loaded.pixels[3], std::byte{255});
}

TEST(EmbeddedTextureImportTest, BytesThatCannotBeDecodedPublishNothingAtAll) {
    // A slot whose image is broken must leave no half-written `.uvtex` behind - the material would
    // otherwise reference a file that exists but holds nothing.
    const ScratchDirectory scratch{"refusal"};
    const std::filesystem::path destination = scratch.Path() / "Agent_material_0_albedo.uvtex";
    const std::vector<std::byte> garbage{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    EXPECT_FALSE(Detail::PublishEmbeddedTexture(destination, garbage, TextureImportSettings{}, "TestImporter",
                                                   "garbage"));
    EXPECT_FALSE(std::filesystem::exists(destination));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{destination.string() + ".uve_embedded_tex_tmp"}));
}

TEST(EmbeddedTextureImportTest, PrepareTextureForImport_DownsamplesOversizedTexturePreservingAspectRatio) {
    TextureAsset texture;
    texture.width = 64U;
    texture.height = 32U;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpace::Srgb;
    texture.usage = TextureUsage::Color;
    texture.pixels.resize(64U * 32U * 4U);
    for (std::size_t i = 0U; i < 64U * 32U; ++i) {
        texture.pixels[i * 4U + 0U] = std::byte{200};
        texture.pixels[i * 4U + 1U] = std::byte{100};
        texture.pixels[i * 4U + 2U] = std::byte{50};
        texture.pixels[i * 4U + 3U] = std::byte{255};
    }

    TextureImportSettings clamped;
    clamped.colorSpace = TextureAssetColorSpace::Srgb;
    clamped.usage = TextureUsage::Color;
    clamped.generateMipmaps = true;
    clamped.compressionMode = TextureCompressionMode::None;
    clamped.maxResolutionClamp = 16U;

    ASSERT_TRUE(Detail::PrepareTextureForImport(texture, clamped));
    EXPECT_EQ(texture.width, 16U);
    EXPECT_EQ(texture.height, 8U);
    EXPECT_GT(texture.mipLevels.size(), 1U);
    ASSERT_GE(texture.pixels.size(), 4U);
    EXPECT_EQ(texture.pixels[0], std::byte{200});
    EXPECT_EQ(texture.pixels[1], std::byte{100});
    EXPECT_EQ(texture.pixels[2], std::byte{50});
    EXPECT_EQ(texture.pixels[3], std::byte{255});
}

} // namespace
} // namespace UVE::Asset::Tests
