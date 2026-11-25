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

TEST(BlueprintEditorServicesTest, UndoRedoStackManagesSnapshots) {
    Asset::BlueprintAsset v0 =
        Resources::InstantiateBlueprintTemplate(Resources::BlueprintTemplateKind::EmptyObject);
    Asset::BlueprintAsset v1 = v0;
    v1.nodes.push_back({1U, "event_ready", {}, 10.0F, 20.0F, {}});
    Asset::BlueprintAsset v2 = v1;
    v2.nodes.push_back({2U, "print", {}, 200.0F, 20.0F, {"", "Hi"}});

    Editor::BlueprintUndoStack stack;
    stack.Reset(v0, "Initial");
    EXPECT_FALSE(stack.CanUndo());
    EXPECT_FALSE(stack.CanRedo());

    stack.PushSnapshot(v1, "Add Event Ready");
    stack.PushSnapshot(v2, "Add Print");
    EXPECT_TRUE(stack.CanUndo());
    EXPECT_EQ(stack.CurrentLabel(), "Add Print");

    const auto undo1 = stack.Undo();
    ASSERT_TRUE(undo1.has_value());
    EXPECT_EQ(*undo1, v1);
    EXPECT_TRUE(stack.CanRedo());

    const auto redo1 = stack.Redo();
    ASSERT_TRUE(redo1.has_value());
    EXPECT_EQ(*redo1, v2);
}

TEST(BlueprintEditorServicesTest, ClipboardCopyPasteRemapsNodeIdsAndWires) {
    Asset::BlueprintAsset bp =
        Resources::InstantiateBlueprintTemplate(Resources::BlueprintTemplateKind::InteractiveTrigger);
    ASSERT_EQ(bp.nodes.size(), 2U);
    ASSERT_EQ(bp.wires.size(), 1U);

    const std::array<std::uint32_t, 2> selection{1U, 2U};
    const std::string clipboard = Editor::CopyBlueprintSelection(bp, selection);

    const Editor::BlueprintPasteResult pasted = Editor::PasteBlueprintSelection(bp, clipboard, 50.0F, 60.0F);
    ASSERT_TRUE(pasted.success) << pasted.error;
    EXPECT_EQ(pasted.createdNodeIds.size(), 2U);
    EXPECT_EQ(pasted.createdWireCount, 1U);
    EXPECT_EQ(bp.nodes.size(), 4U);
    EXPECT_EQ(bp.wires.size(), 2U);
    EXPECT_TRUE(Asset::IsBlueprintAssetValid(bp));
}

TEST(BlueprintEditorServicesTest, SearchPaletteShortcutsVisualizationAndAccessibility) {
    const Asset::BlueprintAsset bp =
        Resources::InstantiateBlueprintTemplate(Resources::BlueprintTemplateKind::InteractiveTrigger);

    const auto hits = Editor::SearchBlueprintGraph(bp, "overlap");
    EXPECT_GE(hits.size(), 1U);

    const auto palette = Editor::BuildBlueprintPalette(Asset::BlueprintGraphKind::Object, "print",
                                                          Asset::BlueprintPinType::Exec);
    EXPECT_FALSE(palette.empty());

    EXPECT_EQ(Editor::MatchBlueprintShortcut("f7", false, false, false), Editor::BlueprintShortcutAction::Compile);
    EXPECT_EQ(Editor::MatchBlueprintShortcut("z", true, false, false), Editor::BlueprintShortcutAction::Undo);
    EXPECT_EQ(Editor::MatchBlueprintShortcut("f9", false, false, false),
              Editor::BlueprintShortcutAction::ToggleBreakpoint);

    Editor::BlueprintExecutionVisualization vis;
    vis.TriggerWirePulse(bp.wires[0], 0.5F);
    EXPECT_EQ(vis.ActivePulses().size(), 1U);
    vis.Tick(0.25F);
    EXPECT_EQ(vis.ActivePulses().size(), 1U);
    vis.Tick(0.30F);
    EXPECT_TRUE(vis.ActivePulses().empty());

    const std::string a11y = Editor::DescribeBlueprintNodeForAccessibility(bp, 2U, "Missing target");
    EXPECT_NE(a11y.find("ID 2"), std::string::npos);
    EXPECT_NE(a11y.find("Missing target"), std::string::npos);
}

} // namespace
} // namespace UVE::Blueprint::Tests
