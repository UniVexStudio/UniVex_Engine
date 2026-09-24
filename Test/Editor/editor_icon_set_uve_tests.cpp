// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_icon_set_uve.h"

#include <array>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Editor::Tests {
namespace {

TEST(EditorIconSetUVETest, EveryNodeTypeHasItsOwnIcon) {
    for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : Scene::Nodes::GetSceneNodeDescriptorsUVE()) {
        EXPECT_NE(FindEditorIconSourceUVE(EditorIconGroupUVE::Node, descriptor.typeId), nullptr)
            << "no icon for node type " << descriptor.typeId;
    }
}

TEST(EditorIconSetUVETest, EveryPaletteCategoryHasAnIconByItsDisplayName) {
    std::set<std::string_view> categories;
    for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : Scene::Nodes::GetSceneNodeDescriptorsUVE()) {
        categories.insert(descriptor.category);
    }
    ASSERT_FALSE(categories.empty());
    for (const std::string_view category : categories) {
        EXPECT_NE(FindEditorIconSourceUVE(EditorIconGroupUVE::NodeCategory, category), nullptr)
            << "no icon for category " << category;
    }
}

TEST(EditorIconSetUVETest, AnOpenFolderHasItsOwnIcon) {
    // Every content browser type's icon is checked in editor_uve_tests.cpp, which can see the
    // editor's private type list; the open folder is the one icon no type names.
    EXPECT_NE(FindEditorIconSourceUVE(EditorIconGroupUVE::ContentType, "folder_open"), nullptr);
    EXPECT_NE(FindEditorIconSourceUVE(EditorIconGroupUVE::ContentType, "folder_open"),
              FindEditorIconSourceUVE(EditorIconGroupUVE::ContentType, "folder"));
}

TEST(EditorIconSetUVETest, LookupIsPerGroupAndIgnoresOnlyCase) {
    // "script" is both a node and a content type; the group decides which picture comes back.
    const EditorIconSourceUVE* const node = FindEditorIconSourceUVE(EditorIconGroupUVE::Node, "script");
    const EditorIconSourceUVE* const file = FindEditorIconSourceUVE(EditorIconGroupUVE::ContentType, "Script");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(file, nullptr);
    EXPECT_NE(node, file);
    EXPECT_EQ(FindEditorIconSourceUVE(EditorIconGroupUVE::NodeCategory, "PHYSICS"),
              FindEditorIconSourceUVE(EditorIconGroupUVE::NodeCategory, "physics"));
    EXPECT_EQ(FindEditorIconSourceUVE(EditorIconGroupUVE::Node, "box_mesh"), nullptr);
    EXPECT_EQ(FindEditorIconSourceUVE(EditorIconGroupUVE::Node, ""), nullptr);
    EXPECT_EQ(FindEditorIconSourceUVE(EditorIconGroupUVE::ContentType, "box_mesh_3d"), nullptr);
}

TEST(EditorIconSetUVETest, EveryEmbeddedIconDecodesToAFullMipChainWithSomethingDrawn) {
    const auto sources = GetEditorIconSourcesUVE();
    ASSERT_FALSE(sources.empty());
    for (const EditorIconSourceUVE& source : sources) {
        const std::vector<EditorIconLevelUVE> levels = DecodeEditorIconUVE(source.png);
        ASSERT_EQ(levels.size(), 7U) << source.name;
        EXPECT_EQ(levels.front().size, 64) << source.name;
        for (std::size_t level = 0U; level < levels.size(); ++level) {
            const auto size = static_cast<std::size_t>(64 >> level);
            EXPECT_EQ(levels[level].rgba.size(), size * size * 4U) << source.name;
        }
        // Transparent corners (it is an icon, not a square) and some opaque artwork.
        const std::vector<std::uint8_t>& top = levels.front().rgba;
        EXPECT_EQ(top[3], 0U) << source.name;
        bool opaque = false;
        for (std::size_t alpha = 3U; alpha < top.size(); alpha += 4U) {
            opaque = opaque || top[alpha] == 255U;
        }
        EXPECT_TRUE(opaque) << source.name;
    }
}

TEST(EditorIconSetUVETest, DownsamplingWeighsColourByAlphaSoEdgesDoNotDarken) {
    // One opaque white pixel beside three fully transparent black ones: the average is white at a
    // quarter coverage. A plain average would give a grey, the dark rim this function exists to avoid.
    const std::array<std::uint8_t, 16> block{255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<std::uint8_t> half = DownsampleEditorIconUVE(block, 2);
    ASSERT_EQ(half.size(), 4U);
    EXPECT_EQ(half[0], 255U);
    EXPECT_EQ(half[1], 255U);
    EXPECT_EQ(half[2], 255U);
    EXPECT_EQ(half[3], 64U);

    // Nothing covered stays nothing, rather than dividing by zero.
    const std::array<std::uint8_t, 16> empty{};
    EXPECT_EQ(DownsampleEditorIconUVE(empty, 2), (std::vector<std::uint8_t>{0, 0, 0, 0}));
    // A size that does not match the pixels is refused.
    EXPECT_TRUE(DownsampleEditorIconUVE(block, 4).empty());
    EXPECT_TRUE(DownsampleEditorIconUVE(block, 1).empty());
}

TEST(EditorIconSetUVETest, BytesThatAreNotAnIconPngDecodeToNothing) {
    EXPECT_TRUE(DecodeEditorIconUVE({}).empty());
    const std::array<std::uint8_t, 8> garbage{1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_TRUE(DecodeEditorIconUVE(garbage).empty());
    // A real PNG cut short.
    const auto png = GetEditorIconSourcesUVE().front().png;
    EXPECT_TRUE(DecodeEditorIconUVE(png.first(png.size() / 2U)).empty());
}

} // namespace
} // namespace UVE::Editor::Tests
