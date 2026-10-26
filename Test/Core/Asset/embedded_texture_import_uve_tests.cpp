// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Tests for the part of a model import that turns somebody else's images into this engine's
// textures: bytes carried inside a model file, and files that sit beside it.
//
// Both are the difference between "the character imported" and "the character imported and looks
// like the character". A model that names `C:/Users/someone/Textures/Wood.PNG` was authored on
// another machine, with another case-fold, and that name has to be resolved into something real - or
// honestly reported as missing - without ever guessing.

#include "import_helpers_uve.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_image_fixtures_uve.h"
#include "uve/asset/texture_asset_uve.h"

namespace UVE::Asset::Tests {
namespace {

/// A directory under the test's own scratch space, removed when the test ends so a failed assertion
/// cannot leave a tree behind for the next run to trip over.
class ScratchDirectoryUVE final {
public:
    explicit ScratchDirectoryUVE(const std::string_view name) {
        m_path = std::filesystem::temp_directory_path() / ("uve_embedded_texture_" + std::string{name});
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::create_directories(m_path, error);
    }
    ~ScratchDirectoryUVE() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    ScratchDirectoryUVE(const ScratchDirectoryUVE&) = delete;
    ScratchDirectoryUVE& operator=(const ScratchDirectoryUVE&) = delete;

    [[nodiscard]] const std::filesystem::path& PathUVE() const noexcept { return m_path; }

    void WriteUVE(const std::filesystem::path& relative, const std::vector<std::byte>& bytes) const {
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

TEST(EmbeddedTextureImportUVETest, PngBytesThatArrivedInsideAModelDecodeToTheirRealPixels) {
    // The fixture is a 2x1 image: an opaque red texel then an opaque white one. Asserting the pixels
    // rather than "it returned true" is what makes this a test of decoding instead of plumbing.
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    TextureImportSettingsUVE settings;
    settings.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    settings.usage = TextureUsageUVE::Color;

    TextureAssetUVE texture;
    ASSERT_TRUE(Detail::DecodeImageBytesUVE(png, settings, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.format, TextureAssetFormatUVE::RGBA8Unorm);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpaceUVE::Srgb);
    EXPECT_EQ(texture.usage, TextureUsageUVE::Color);
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
    EXPECT_TRUE(IsTextureAssetValidUVE(texture));
}

TEST(EmbeddedTextureImportUVETest, ANormalMapSlotIsLinearAndCarriesNormalUsage) {
    // The same bytes under a different policy: this is the import bug that makes a whole character
    // look wrong (a normal map read as sRGB bends every surface), so the policy is asserted, not
    // assumed.
    TextureImportSettingsUVE settings;
    settings.colorSpace = TextureAssetColorSpaceUVE::Linear;
    settings.usage = TextureUsageUVE::Normal;

    TextureAssetUVE texture;
    ASSERT_TRUE(Detail::DecodeImageBytesUVE(UVE::Tests::MakePngTwoByOneRgba8UVE(), settings, texture));
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpaceUVE::Linear);
    EXPECT_EQ(texture.usage, TextureUsageUVE::Normal);
}

TEST(EmbeddedTextureImportUVETest, BytesThatAreNotAnImageAreRefusedRatherThanGuessedAt) {
    TextureImportSettingsUVE settings;
    TextureAssetUVE texture;
    const std::vector<std::byte> text{std::byte{'n'}, std::byte{'o'}, std::byte{'t'}};
    EXPECT_FALSE(Detail::DecodeImageBytesUVE(text, settings, texture));
    EXPECT_FALSE(Detail::DecodeImageBytesUVE({}, settings, texture));
    // A truncated PNG keeps its signature and must still fail, not decode into noise.
    std::vector<std::byte> truncated = UVE::Tests::MakePngTwoByOneRgba8UVE();
    truncated.resize(20U);
    EXPECT_FALSE(Detail::DecodeImageBytesUVE(truncated, settings, texture));
}

TEST(EmbeddedTextureImportUVETest, AnAuthoredWindowsPathResolvesOnlyByTheNameAtItsEnd) {
    // No machine in this project has `C:/Users/artist/Textures`, and following that path verbatim is
    // how a model from another computer ends up with no textures at all.
    const ScratchDirectoryUVE scratch{"resolve"};
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    scratch.WriteUVE("textures/wood_diffuse.png", png);
    scratch.WriteUVE("model.fbx", {});

    const auto resolved =
        Detail::ResolveAuthoredTextureFileUVE(scratch.PathUVE(), "model", "C:/Users/artist/Textures/wood_diffuse.png");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->filename().string(), "wood_diffuse.png");
    EXPECT_TRUE(std::filesystem::is_regular_file(*resolved));
}

TEST(EmbeddedTextureImportUVETest, AReferenceIsMatchedIgnoringCaseBecauseTheExporterWasOnWindows) {
    const ScratchDirectoryUVE scratch{"case"};
    scratch.WriteUVE("Textures/Wood_Diffuse.PNG", UVE::Tests::MakePngTwoByOneRgba8UVE());
    const auto resolved = Detail::ResolveAuthoredTextureFileUVE(scratch.PathUVE(), "model", "wood_diffuse.png");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->filename().string(), "Wood_Diffuse.PNG");
}

TEST(EmbeddedTextureImportUVETest, ATextureInTheFbxSiblingFolderIsFoundToo) {
    // `<model>.fbm/` is where an FBX exporter that copies its textures next to the model puts them.
    const ScratchDirectoryUVE scratch{"fbm"};
    scratch.WriteUVE("model.fbm/wood.png", UVE::Tests::MakePngTwoByOneRgba8UVE());
    const auto resolved = Detail::ResolveAuthoredTextureFileUVE(scratch.PathUVE(), "model", "wood.png");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->parent_path().filename().string(), "model.fbm");
}

TEST(EmbeddedTextureImportUVETest, AMissingTextureIsReportedAsMissingAndNothingIsInvented) {
    const ScratchDirectoryUVE scratch{"missing"};
    EXPECT_FALSE(Detail::ResolveAuthoredTextureFileUVE(scratch.PathUVE(), "model", "nope.png").has_value());
    EXPECT_FALSE(Detail::ResolveAuthoredTextureFileUVE(scratch.PathUVE(), "model", "").has_value());
    // A directory with the right name is not a texture.
    std::error_code error;
    std::filesystem::create_directories(scratch.PathUVE() / "textures" / "folder.png", error);
    EXPECT_FALSE(Detail::ResolveAuthoredTextureFileUVE(scratch.PathUVE(), "model", "folder.png").has_value());
}

TEST(EmbeddedTextureImportUVETest, AnEmbeddedImageIsPublishedAsAUvtexThatLoadsBackAsTheSamePixels) {
    // The whole path: bytes that arrived inside a model -> a `.uvtex` on disk -> read back. If any
    // step re-encoded, dropped mips or swapped channels, this is where it shows.
    const ScratchDirectoryUVE scratch{"publish"};
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    TextureImportSettingsUVE settings;
    settings.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    settings.usage = TextureUsageUVE::Color;

    const std::filesystem::path destination = scratch.PathUVE() / "Agent_material_0_albedo.uvtex";
    ASSERT_TRUE(Detail::PublishEmbeddedTextureUVE(destination, png, settings, "TestImporterUVE", "a test image"));
    ASSERT_TRUE(std::filesystem::is_regular_file(destination));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(destination, loaded));
    EXPECT_EQ(loaded.width, 2U);
    EXPECT_EQ(loaded.height, 1U);
    ASSERT_GE(loaded.pixels.size(), 8U);
    EXPECT_EQ(loaded.pixels[0], std::byte{255});
    EXPECT_EQ(loaded.pixels[3], std::byte{255});
}

TEST(EmbeddedTextureImportUVETest, BytesThatCannotBeDecodedPublishNothingAtAll) {
    // A slot whose image is broken must leave no half-written `.uvtex` behind - the material would
    // otherwise reference a file that exists but holds nothing.
    const ScratchDirectoryUVE scratch{"refusal"};
    const std::filesystem::path destination = scratch.PathUVE() / "Agent_material_0_albedo.uvtex";
    const std::vector<std::byte> garbage{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    EXPECT_FALSE(Detail::PublishEmbeddedTextureUVE(destination, garbage, TextureImportSettingsUVE{}, "TestImporterUVE",
                                                   "garbage"));
    EXPECT_FALSE(std::filesystem::exists(destination));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{destination.string() + ".uve_embedded_tex_tmp"}));
}

} // namespace
} // namespace UVE::Asset::Tests
