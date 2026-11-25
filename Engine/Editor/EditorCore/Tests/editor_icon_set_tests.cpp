// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_icon_set.h"

#include <array>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "scene_object_registry.h"

namespace UVE::Editor::Tests {
namespace {

TEST(EditorIconSetTest, EveryObjectTypeHasItsOwnIcon) {
    for (const Scene::Objects::SceneObjectDescriptor& descriptor : Scene::Objects::GetSceneObjectDescriptors()) {
        EXPECT_NE(FindEditorIconSource(EditorIconGroup::Object, descriptor.typeId), nullptr)
            << "no icon for object type " << descriptor.typeId;
    }
}

TEST(EditorIconSetTest, EveryPaletteCategoryHasAnIconByItsDisplayName) {
    std::set<std::string_view> categories;
    for (const Scene::Objects::SceneObjectDescriptor& descriptor : Scene::Objects::GetSceneObjectDescriptors()) {
        categories.insert(descriptor.category);
    }
    ASSERT_FALSE(categories.empty());
    for (const std::string_view category : categories) {
        EXPECT_NE(FindEditorIconSource(EditorIconGroup::ObjectCategory, category), nullptr)
            << "no icon for category " << category;
    }
}

TEST(EditorIconSetTest, AnOpenFolderHasItsOwnIcon) {
    // Every asset table type's icon is checked in editor_uve_tests.cpp, which can see the
    // editor's private type list; the open folder is the one icon no type names.
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::ContentType, "folder_open"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::ContentType, "folder_open"),
              FindEditorIconSource(EditorIconGroup::ContentType, "folder"));
}

TEST(EditorIconSetTest, ObjectsAndContentTypesAndComponentsExist) {
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::Object, "character_3d"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::Object, "player_3d"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::Object, "camera_3d"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::ContentType, "material"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::ContentType, "shader"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::ContentType, "folder"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::Component, "component_transform"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::Component, "component_mesh"), nullptr);
    EXPECT_NE(FindEditorIconSource(EditorIconGroup::Component, "component_collider"), nullptr);
}

TEST(EditorIconSetTest, LookupIsPerGroupAndIgnoresOnlyCase) {
    // "script" is both an object and a content type; the group decides which picture comes back.
    const EditorIconSource* const object = FindEditorIconSource(EditorIconGroup::Object, "script");
    const EditorIconSource* const file = FindEditorIconSource(EditorIconGroup::ContentType, "Script");
    ASSERT_NE(object, nullptr);
    ASSERT_NE(file, nullptr);
    EXPECT_NE(object, file);
    EXPECT_EQ(FindEditorIconSource(EditorIconGroup::ObjectCategory, "PHYSICS"),
              FindEditorIconSource(EditorIconGroup::ObjectCategory, "physics"));
    EXPECT_EQ(FindEditorIconSource(EditorIconGroup::Object, "box_mesh"), nullptr);
    EXPECT_EQ(FindEditorIconSource(EditorIconGroup::Object, ""), nullptr);
    EXPECT_EQ(FindEditorIconSource(EditorIconGroup::ContentType, "box_mesh_3d"), nullptr);
}

TEST(EditorIconSetTest, EveryEmbeddedIconDecodesToAFullMipChainWithSomethingDrawn) {
    const auto sources = GetEditorIconSources();
    ASSERT_FALSE(sources.empty());
    for (const EditorIconSource& source : sources) {
        const std::vector<EditorIconLevel> levels = DecodeEditorIcon(source.png);
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

TEST(EditorIconSetTest, DownsamplingWeighsColourByAlphaSoEdgesDoNotDarken) {
    // One opaque white pixel beside three fully transparent black ones: the average is white at a
    // quarter coverage. A plain average would give a grey, the dark rim this function exists to avoid.
    const std::array<std::uint8_t, 16> block{255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<std::uint8_t> half = DownsampleEditorIcon(block, 2);
    ASSERT_EQ(half.size(), 4U);
    EXPECT_EQ(half[0], 255U);
    EXPECT_EQ(half[1], 255U);
    EXPECT_EQ(half[2], 255U);
    EXPECT_EQ(half[3], 64U);

    // Nothing covered stays nothing, rather than dividing by zero.
    const std::array<std::uint8_t, 16> empty{};
    EXPECT_EQ(DownsampleEditorIcon(empty, 2), (std::vector<std::uint8_t>{0, 0, 0, 0}));
    // A size that does not match the pixels is refused.
    EXPECT_TRUE(DownsampleEditorIcon(block, 4).empty());
    EXPECT_TRUE(DownsampleEditorIcon(block, 1).empty());
}

TEST(EditorIconSetTest, BytesThatAreNotAnIconPngDecodeToNothing) {
    EXPECT_TRUE(DecodeEditorIcon({}).empty());
    const std::array<std::uint8_t, 8> garbage{1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_TRUE(DecodeEditorIcon(garbage).empty());
    // A real PNG cut short.
    const auto png = GetEditorIconSources().front().png;
    EXPECT_TRUE(DecodeEditorIcon(png.first(png.size() / 2U)).empty());
}

} // namespace
} // namespace UVE::Editor::Tests
