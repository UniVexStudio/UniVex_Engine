// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_layer_mask_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

LayerNamesUVE MakeNamesUVE() {
    LayerNamesUVE names;
    names[0] = "Default";
    names[1] = "Player";
    names[3] = "Enemies";
    return names;
}

TEST(EditorLayerMaskUVETest, UnnamedLayersAreCountedFromOne) {
    const LayerNamesUVE names = MakeNamesUVE();
    EXPECT_EQ(GetLayerLabelUVE(names, 0U), "Default");
    EXPECT_EQ(GetLayerLabelUVE(names, 2U), "Layer 3");
    EXPECT_EQ(GetLayerLabelUVE(names, 31U), "Layer 32");
    EXPECT_EQ(GetLayerLabelUVE(names, 40U), "Layer 41");
}

TEST(EditorLayerMaskUVETest, SummaryNamesTheLayersInTheMask) {
    const LayerNamesUVE names = MakeNamesUVE();
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0U, names), "None");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0xFFFFFFFFU, names), "All");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0b1U, names), "Default");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0b1011U, names), "Default, Player, Enemies");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0b1111U, names), "Default, Player, Layer 3 +1");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0x80000000U, names), "Layer 32");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0xFFFFFFFEU, names, 1U), "Player +30");
}

} // namespace
} // namespace UVE::Editor::Tests
