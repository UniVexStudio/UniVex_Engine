// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_editor_services.h"
#include "blueprint_resources.h"

namespace UVE::Blueprint::Tests {
namespace {

TEST(BlueprintEditorServicesUVETest, UndoRedoStackManagesSnapshots) {
    Asset::BlueprintAssetUVE v0 =
        Resources::InstantiateBlueprintTemplateUVE(Resources::BlueprintTemplateKindUVE::EmptyObject);
    Asset::BlueprintAssetUVE v1 = v0;
    v1.nodes.push_back({1U, "event_ready", {}, 10.0F, 20.0F, {}});
    Asset::BlueprintAssetUVE v2 = v1;
    v2.nodes.push_back({2U, "print", {}, 200.0F, 20.0F, {"", "Hi"}});

    Editor::BlueprintUndoStackUVE stack;
    stack.ResetUVE(v0, "Initial");
    EXPECT_FALSE(stack.CanUndoUVE());
    EXPECT_FALSE(stack.CanRedoUVE());

    stack.PushSnapshotUVE(v1, "Add Event Ready");
    stack.PushSnapshotUVE(v2, "Add Print");
    EXPECT_TRUE(stack.CanUndoUVE());
    EXPECT_EQ(stack.CurrentLabelUVE(), "Add Print");

    const auto undo1 = stack.UndoUVE();
    ASSERT_TRUE(undo1.has_value());
    EXPECT_EQ(*undo1, v1);
    EXPECT_TRUE(stack.CanRedoUVE());

    const auto redo1 = stack.RedoUVE();
    ASSERT_TRUE(redo1.has_value());
    EXPECT_EQ(*redo1, v2);
}

TEST(BlueprintEditorServicesUVETest, ClipboardCopyPasteRemapsNodeIdsAndWires) {
    Asset::BlueprintAssetUVE bp =
        Resources::InstantiateBlueprintTemplateUVE(Resources::BlueprintTemplateKindUVE::InteractiveTrigger);
    ASSERT_EQ(bp.nodes.size(), 2U);
    ASSERT_EQ(bp.wires.size(), 1U);

    const std::array<std::uint32_t, 2> selection{1U, 2U};
    const std::string clipboard = Editor::CopyBlueprintSelectionUVE(bp, selection);

    const Editor::BlueprintPasteResultUVE pasted = Editor::PasteBlueprintSelectionUVE(bp, clipboard, 50.0F, 60.0F);
    ASSERT_TRUE(pasted.success) << pasted.error;
    EXPECT_EQ(pasted.createdNodeIds.size(), 2U);
    EXPECT_EQ(pasted.createdWireCount, 1U);
    EXPECT_EQ(bp.nodes.size(), 4U);
    EXPECT_EQ(bp.wires.size(), 2U);
    EXPECT_TRUE(Asset::IsBlueprintAssetValidUVE(bp));
}

TEST(BlueprintEditorServicesUVETest, SearchPaletteShortcutsVisualizationAndAccessibility) {
    const Asset::BlueprintAssetUVE bp =
        Resources::InstantiateBlueprintTemplateUVE(Resources::BlueprintTemplateKindUVE::InteractiveTrigger);

    const auto hits = Editor::SearchBlueprintGraphUVE(bp, "overlap");
    EXPECT_GE(hits.size(), 1U);

    const auto palette = Editor::BuildBlueprintPaletteUVE(Asset::BlueprintGraphKindUVE::Object, "print",
                                                          Asset::BlueprintPinTypeUVE::Exec);
    EXPECT_FALSE(palette.empty());

    EXPECT_EQ(Editor::MatchBlueprintShortcutUVE("f7", false, false, false), Editor::BlueprintShortcutActionUVE::Compile);
    EXPECT_EQ(Editor::MatchBlueprintShortcutUVE("z", true, false, false), Editor::BlueprintShortcutActionUVE::Undo);
    EXPECT_EQ(Editor::MatchBlueprintShortcutUVE("f9", false, false, false),
              Editor::BlueprintShortcutActionUVE::ToggleBreakpoint);

    Editor::BlueprintExecutionVisualizationUVE vis;
    vis.TriggerWirePulseUVE(bp.wires[0], 0.5F);
    EXPECT_EQ(vis.ActivePulsesUVE().size(), 1U);
    vis.TickUVE(0.25F);
    EXPECT_EQ(vis.ActivePulsesUVE().size(), 1U);
    vis.TickUVE(0.30F);
    EXPECT_TRUE(vis.ActivePulsesUVE().empty());

    const std::string a11y = Editor::DescribeBlueprintNodeForAccessibilityUVE(bp, 2U, "Missing target");
    EXPECT_NE(a11y.find("ID 2"), std::string::npos);
    EXPECT_NE(a11y.find("Missing target"), std::string::npos);
}

} // namespace
} // namespace UVE::Blueprint::Tests
