// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <filesystem>

#include "material_graph_asset.h"
#include "svg_importer.h"

namespace UVE::Asset {
namespace {

TEST(SvgImporterUVETest, RasterizesRectCircleAndPathIntoRgba8Texture) {
    const std::string_view svg =
        R"SVG(<svg width="32" height="32" viewBox="0 0 32 32">
            <rect x="0" y="0" width="32" height="32" fill="#204080"/>
            <circle cx="16" cy="16" r="8" fill="#ff0000"/>
            <path d="M 2 2 L 10 2 L 6 10 Z" fill="#00ff00"/>
        </svg>)SVG";

    SvgRasterOptionsUVE opts{};
    opts.rasterScale = 2.0F;
    TextureAssetUVE tex{};
    ASSERT_TRUE(RasterizeSvgToTextureAssetUVE(svg, opts, tex));
    EXPECT_EQ(tex.width, 64U);
    EXPECT_EQ(tex.height, 64U);
    EXPECT_EQ(tex.format, TextureAssetFormatUVE::RGBA8Unorm);
    ASSERT_EQ(tex.pixels.size(), 64U * 64U * 4U);

    // Center pixel (32, 32) is inside the red circle.
    const std::size_t centerIdx = (32U * 64U + 32U) * 4U;
    EXPECT_GT(static_cast<unsigned int>(tex.pixels[centerIdx + 0U]), 200U);
    EXPECT_LT(static_cast<unsigned int>(tex.pixels[centerIdx + 1U]), 30U);

    // Bottom-right corner (60, 60) is the #204080 background rect.
    const std::size_t cornerIdx = (60U * 64U + 60U) * 4U;
    EXPECT_GT(static_cast<unsigned int>(tex.pixels[cornerIdx + 2U]), 100U);
    EXPECT_EQ(static_cast<unsigned int>(tex.pixels[cornerIdx + 3U]), 255U);
}

TEST(MaterialGraphCompilerUVETest, CompilesGraphDetectsCyclesAndRoundTripsFile) {
    MaterialGraphAssetUVE graph = CreateDefaultPbrMaterialGraphUVE();
    const auto compiled = CompileMaterialGraphToGlslUVE(graph);
    ASSERT_TRUE(compiled.ok);
    EXPECT_TRUE(compiled.errors.empty());
    EXPECT_NE(compiled.glslSource.find("EvaluateMaterialGraphUVE"), std::string::npos);
    EXPECT_NE(compiled.glslSource.find("s.baseColor = n_1;"), std::string::npos);
    EXPECT_NE(compiled.glslSource.find("s.roughness = clamp(n_2"), std::string::npos);

    // Save and reload round-trip.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "uve_test_matgraph.uvmatgraph";
    ASSERT_TRUE(SaveMaterialGraphAssetUVE(graph, tmp));
    MaterialGraphAssetUVE reloaded{};
    ASSERT_TRUE(LoadMaterialGraphAssetUVE(tmp, reloaded));
    std::filesystem::remove(tmp);
    EXPECT_EQ(reloaded.name, graph.name);
    EXPECT_EQ(reloaded.nodes.size(), graph.nodes.size());
    EXPECT_EQ(reloaded.links.size(), graph.links.size());

    // Cycle detection rejects cyclic graphs.
    MaterialGraphAssetUVE cyclic = CreateDefaultPbrMaterialGraphUVE();
    MaterialGraphNodeUVE addA{};
    addA.id = 10U;
    addA.kind = MaterialGraphNodeKindUVE::Add;
    MaterialGraphNodeUVE addB{};
    addB.id = 11U;
    addB.kind = MaterialGraphNodeKindUVE::Add;
    cyclic.nodes.push_back(addA);
    cyclic.nodes.push_back(addB);
    cyclic.links.push_back(MaterialGraphLinkUVE{10U, 0U, 11U, 0U});
    cyclic.links.push_back(MaterialGraphLinkUVE{11U, 0U, 10U, 0U});
    cyclic.links.push_back(MaterialGraphLinkUVE{11U, 0U, 3U, 3U});
    const auto bad = CompileMaterialGraphToGlslUVE(cyclic);
    EXPECT_FALSE(bad.ok);
    EXPECT_FALSE(bad.errors.empty());
}

} // namespace
} // namespace UVE::Asset
