// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_hierarchy_view_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

TEST(EditorHierarchyViewUVETest, EyeIsDrawnPerModeAndAlwaysOnAHiddenNodeWhenOnHover) {
    using Mode = HierarchyVisibilityColumnUVE;
    for (const bool hovered : {false, true}) {
        for (const bool visible : {false, true}) {
            EXPECT_TRUE(ShouldDrawHierarchyEyeUVE(Mode::Always, hovered, visible));
            EXPECT_FALSE(ShouldDrawHierarchyEyeUVE(Mode::Hidden, hovered, visible));
        }
    }
    EXPECT_FALSE(ShouldDrawHierarchyEyeUVE(Mode::OnHover, false, true));
    EXPECT_TRUE(ShouldDrawHierarchyEyeUVE(Mode::OnHover, true, true));
    // A hidden node keeps its closed eye, so it never looks like a shown one.
    EXPECT_TRUE(ShouldDrawHierarchyEyeUVE(Mode::OnHover, false, false));
}

TEST(EditorHierarchyViewUVETest, TypeHintIsLeftOutWhenItWouldOnlyRepeatTheName) {
    EXPECT_EQ(GetHierarchyTypeHintUVE("Player", "CharacterBody3D"), "CharacterBody3D");
    EXPECT_EQ(GetHierarchyTypeHintUVE("BoxMesh3D", "BoxMesh3D"), "");
    EXPECT_EQ(GetHierarchyTypeHintUVE("Player", ""), "");
    // Only an exact repeat is left out; a name that differs only in case still shows the type.
    EXPECT_EQ(GetHierarchyTypeHintUVE("boxmesh3d", "BoxMesh3D"), "BoxMesh3D");
}

TEST(EditorHierarchyViewUVETest, DefaultsAreThePanelsBehaviourBeforeItHadPreferences) {
    const HierarchyViewSettingsUVE view{};
    EXPECT_TRUE(view.revealSelection);
    EXPECT_TRUE(view.showIcons);
    EXPECT_TRUE(view.showTypeName); // new with node types, and on
    EXPECT_EQ(view.visibilityColumn, HierarchyVisibilityColumnUVE::Always);
    EXPECT_EQ(view.doubleClick, HierarchyDoubleClickUVE::Rename);
    EXPECT_TRUE(view.dragToReparent);
    EXPECT_EQ(view.treeLines, HierarchyTreeLinesUVE::None);
    EXPECT_GE(view.indentWidth, kMinimumHierarchyIndentUVE);
    EXPECT_LE(view.indentWidth, kMaximumHierarchyIndentUVE);
}

} // namespace
} // namespace UVE::Editor::Tests
