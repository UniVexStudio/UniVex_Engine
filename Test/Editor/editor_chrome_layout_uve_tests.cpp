// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_chrome_layout_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] ImGuiViewport MakeViewportUVE(const float width, const float height, const float originX = 0.0F,
                                            const float originY = 0.0F) {
    ImGuiViewport viewport{};
    viewport.WorkPos = ImVec2{originX, originY};
    viewport.WorkSize = ImVec2{width, height};
    return viewport;
}

TEST(EditorChromeLayoutUVETest, ThreeColumnsExactlyFillTheWidthWithNoGapOrOverlap) {
    // The invariant the whole helper exists for, and the reason it is now a shared header rather
    // than a copy per panel: the scene, viewport and inspector must tile the width exactly. A gap
    // shows the window behind; an overlap hides one panel's edge under another.
    for (const float width : {1280.0F, 1920.0F, 2560.0F, 3840.0F}) {
        const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(MakeViewportUVE(width, 1080.0F), true);
        EXPECT_FLOAT_EQ(layout.sceneSize.x + layout.viewportSize.x + layout.inspectorSize.x, width)
            << "at width " << width;
        // Adjacency, not just total: three widths can sum correctly while the panels sit in the
        // wrong places.
        EXPECT_FLOAT_EQ(layout.viewportPos.x, layout.scenePos.x + layout.sceneSize.x) << "at width " << width;
        EXPECT_FLOAT_EQ(layout.inspectorPos.x, layout.viewportPos.x + layout.viewportSize.x)
            << "at width " << width;
    }
}

TEST(EditorChromeLayoutUVETest, PanelsStartBelowTheTopChromeAndShareOneRow) {
    // Every structural panel begins under the title bar and toolbar, and all three are the same
    // height - otherwise one of them leaves a strip of empty window below it.
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F), true);

    EXPECT_FLOAT_EQ(layout.scenePos.y, kEditorTopChromeHeightUVE);
    EXPECT_FLOAT_EQ(layout.viewportPos.y, kEditorTopChromeHeightUVE);
    EXPECT_FLOAT_EQ(layout.inspectorPos.y, kEditorTopChromeHeightUVE);
    EXPECT_FLOAT_EQ(layout.sceneSize.y, layout.viewportSize.y);
    EXPECT_FLOAT_EQ(layout.sceneSize.y, layout.inspectorSize.y);
}

TEST(EditorChromeLayoutUVETest, HidingTheBottomDockGivesItsHeightBackToTheWorkspace) {
    // The dock is toggleable, and the workspace must reclaim exactly what it gave up - not
    // approximately, or the viewport creeps a pixel each time it is toggled.
    const EditorChromeLayoutUVE shown = ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F), true);
    const EditorChromeLayoutUVE hidden = ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F), false);

    EXPECT_FLOAT_EQ(hidden.viewportSize.y - shown.viewportSize.y, kAssetsPanelHeightUVE);
    // Width is unaffected by a vertical toggle.
    EXPECT_FLOAT_EQ(hidden.viewportSize.x, shown.viewportSize.x);
}

TEST(EditorChromeLayoutUVETest, SidePanelsStayWithinTheirClampsOnExtremeWidths) {
    // The side panels are a fraction of the width, clamped. On a very narrow window the fraction
    // would otherwise leave no viewport at all; on a very wide one the panels would grow absurd.
    const EditorChromeLayoutUVE narrow = ComputeEditorChromeLayoutUVE(MakeViewportUVE(640.0F, 480.0F), true);
    EXPECT_GE(narrow.sceneSize.x, kScenePanelWidthMinUVE);
    EXPECT_GE(narrow.inspectorSize.x, kInspectorPanelWidthMinUVE);
    EXPECT_GE(narrow.viewportSize.x, kMinimumViewportWidthUVE)
        << "the viewport must never be squeezed out of existence";

    const EditorChromeLayoutUVE wide = ComputeEditorChromeLayoutUVE(MakeViewportUVE(5120.0F, 1440.0F), true);
    EXPECT_LE(wide.sceneSize.x, kScenePanelWidthMaxUVE);
    EXPECT_LE(wide.inspectorSize.x, kInspectorPanelWidthMaxUVE);
}

TEST(EditorChromeLayoutUVETest, AZeroSizedViewportStillProducesAUsableLayout) {
    // Reached in practice, not theoretically: a minimized or freshly created window reports a zero
    // work size for a frame or two, and a layout that collapses to nothing there can produce
    // negative sizes that imgui asserts on.
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(MakeViewportUVE(0.0F, 0.0F), true);

    EXPECT_GE(layout.viewportSize.x, kMinimumViewportWidthUVE);
    EXPECT_GE(layout.viewportSize.y, kMinimumViewportHeightUVE);
    EXPECT_GE(layout.sceneSize.y, 0.0F);
    EXPECT_GE(layout.inspectorSize.y, 0.0F);
}

TEST(EditorChromeLayoutUVETest, LayoutIsOffsetByTheViewportOrigin) {
    // WorkPos is non-zero whenever the OS reserves screen space - a taskbar, a notch, a menu bar.
    // Ignoring it puts every panel under that furniture.
    constexpr float kOriginX = 100.0F;
    constexpr float kOriginY = 50.0F;
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F, kOriginX, kOriginY), true);

    EXPECT_FLOAT_EQ(layout.scenePos.x, kOriginX);
    EXPECT_FLOAT_EQ(layout.scenePos.y, kOriginY + kEditorTopChromeHeightUVE);
    EXPECT_FLOAT_EQ(layout.contentBrowserPos.x, kOriginX);
}

} // namespace
} // namespace UVE::Editor::Tests
