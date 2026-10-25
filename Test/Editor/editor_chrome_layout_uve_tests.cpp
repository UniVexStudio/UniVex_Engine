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

TEST(EditorChromeLayoutUVETest, SidePanelsRunFullHeightAndTheDockSitsUnderTheViewport) {
    // Outliner and Inspector run from under the title bar down to the dock tab strip; the centre
    // column holds the tool strip, the viewport under it and the dock under that.
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F), true);

    // Header row: the Outliner's name beside the logo, the viewport's header, the Inspector's tabs.
    EXPECT_FLOAT_EQ(layout.inspectorPos.y, kEditorTitleBarHeightUVE);
    EXPECT_FLOAT_EQ(layout.viewportHeaderPos.y, kEditorTitleBarHeightUVE);
    EXPECT_FLOAT_EQ(layout.outlinerHeaderPos.y, kEditorTitleBarHeightUVE);
    EXPECT_FLOAT_EQ(layout.scenePos.y, kEditorTopChromeHeightUVE);
    EXPECT_FLOAT_EQ(layout.viewportPos.y, kEditorTopChromeHeightUVE);
    // The side columns reach the bottom edge of the frame: the dock's tab row belongs to the dock,
    // not to a band shared with the panels beside it.
    EXPECT_FLOAT_EQ(layout.inspectorSize.y, 1080.0F - kEditorTitleBarHeightUVE);
    EXPECT_FLOAT_EQ(layout.sceneSize.y + kEditorToolbarHeightUVE, layout.inspectorSize.y);
    // The centre column holds the viewport, then the dock's tab row, then the dock's body.
    EXPECT_FLOAT_EQ(layout.viewportSize.y + kDockTabBarHeightUVE + layout.contentBrowserSize.y, layout.sceneSize.y);
    // The viewport's header spans exactly its column; the logo spans both top rows, and the
    // Outliner's header fills the rest of its column beside it.
    EXPECT_FLOAT_EQ(layout.viewportHeaderPos.x, layout.viewportPos.x);
    EXPECT_FLOAT_EQ(layout.viewportHeaderSize.x, layout.viewportSize.x);
    EXPECT_FLOAT_EQ(layout.logoSize.y, kEditorTopChromeHeightUVE);
    EXPECT_FLOAT_EQ(layout.outlinerHeaderPos.x, layout.logoPos.x + layout.logoSize.x);
    EXPECT_FLOAT_EQ(layout.outlinerHeaderPos.x + layout.outlinerHeaderSize.x, layout.scenePos.x + layout.sceneSize.x);
    EXPECT_FLOAT_EQ(layout.contentBrowserPos.x, layout.viewportPos.x);
    EXPECT_FLOAT_EQ(layout.contentBrowserSize.x, layout.viewportSize.x);
    EXPECT_FLOAT_EQ(layout.contentBrowserPos.y, layout.viewportPos.y + layout.viewportSize.y);
    // The tab strip is the dock's own header: the viewport column's width, directly on top of the
    // dock's body, and the block as a whole ends at the bottom edge.
    EXPECT_FLOAT_EQ(layout.dockTabBarPos.x, layout.viewportPos.x);
    EXPECT_FLOAT_EQ(layout.dockTabBarSize.x, layout.viewportSize.x);
    EXPECT_FLOAT_EQ(layout.dockTabBarPos.y + layout.dockTabBarSize.y, layout.contentBrowserPos.y);
    EXPECT_FLOAT_EQ(layout.contentBrowserPos.y + layout.contentBrowserSize.y, 1080.0F);
}

TEST(EditorChromeLayoutUVETest, HidingTheBottomDockGivesItsHeightBackToTheViewport) {
    const EditorChromeLayoutUVE shown = ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F), true);
    const EditorChromeLayoutUVE hidden = ComputeEditorChromeLayoutUVE(MakeViewportUVE(1920.0F, 1080.0F), false);

    // The dock body's height goes back to the viewport; the tab row stays, because with the dock
    // hidden it is the only way back to it.
    EXPECT_FLOAT_EQ(hidden.viewportSize.y - shown.viewportSize.y, kAssetsPanelHeightUVE - kDockTabBarHeightUVE);
    EXPECT_FLOAT_EQ(hidden.contentBrowserSize.y, 0.0F);
    // The side columns follow the column they share: it keeps the tab row, so it is that much
    // shorter, and the strip itself drops to the bottom edge where it can still be clicked.
    EXPECT_FLOAT_EQ(hidden.sceneSize.y, shown.sceneSize.y - kDockTabBarHeightUVE);
    EXPECT_FLOAT_EQ(hidden.dockTabBarPos.y + hidden.dockTabBarSize.y, 1080.0F);
    EXPECT_LT(shown.dockTabBarPos.y, hidden.dockTabBarPos.y);
    EXPECT_FLOAT_EQ(hidden.viewportSize.x, shown.viewportSize.x);
}

TEST(EditorChromeLayoutUVETest, TheDockHeightIsDraggableWithinLimits) {
    const ImGuiViewport viewport = MakeViewportUVE(1920.0F, 1080.0F);
    // The requested height is the whole dock - its tab row and its body - so the body carries the
    // difference.
    const EditorChromeLayoutUVE tall = ComputeEditorChromeLayoutUVE(viewport, true, 400.0F);
    EXPECT_FLOAT_EQ(tall.contentBrowserSize.y + kDockTabBarHeightUVE, 400.0F);
    // Too small is lifted to the minimum.
    EXPECT_FLOAT_EQ(ComputeEditorChromeLayoutUVE(viewport, true, 10.0F).contentBrowserSize.y,
                    kMinimumBottomDockHeightUVE - kDockTabBarHeightUVE);
    // Too large is capped, and the cap has two ceilings: a fraction of the column, and whatever
    // still leaves the viewport its minimum height. The tighter one wins.
    const EditorChromeLayoutUVE huge = ComputeEditorChromeLayoutUVE(viewport, true, 100000.0F);
    const float column = huge.viewportSize.y + huge.contentBrowserSize.y + kDockTabBarHeightUVE;
    EXPECT_LE(huge.contentBrowserSize.y + kDockTabBarHeightUVE, column * kBottomDockMaxFractionUVE + 0.5F);
    EXPECT_GE(huge.viewportSize.y, kMinimumViewportHeightUVE);
}

TEST(EditorChromeLayoutUVETest, HidingASidePanelGivesItsWidthToTheViewport) {
    // The point of hiding an Outliner is a wider canvas, not a hole where the Outliner was. This is
    // the assertion that would have caught the old layout, which had no visibility input at all and
    // left the space reserved.
    const ImGuiViewport viewport = MakeViewportUVE(1920.0F, 1080.0F);
    const EditorChromeLayoutUVE both = ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE);
    const EditorChromeLayoutUVE noOutliner =
        ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE, -1.0F, -1.0F, false, true);
    const EditorChromeLayoutUVE noInspector =
        ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE, -1.0F, -1.0F, true, false);

    EXPECT_FLOAT_EQ(noOutliner.sceneSize.x, 0.0F);
    EXPECT_FLOAT_EQ(noOutliner.viewportSize.x - both.viewportSize.x, both.sceneSize.x);
    EXPECT_FLOAT_EQ(noInspector.inspectorSize.x, 0.0F);
    EXPECT_FLOAT_EQ(noInspector.viewportSize.x - both.viewportSize.x, both.inspectorSize.x);
    EXPECT_FLOAT_EQ(noOutliner.viewportPos.x, viewport.WorkPos.x);
}

TEST(EditorChromeLayoutUVETest, AHiddenSidePanelLeavesAStripToBringItBack) {
    // A close button with no visible way back is a dead end. The strip exists only while the panel
    // is hidden, and it takes the edge the panel left behind.
    const ImGuiViewport viewport = MakeViewportUVE(1920.0F, 1080.0F);
    const EditorChromeLayoutUVE shown = ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE);
    const EditorChromeLayoutUVE hidden =
        ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE, -1.0F, -1.0F, false, false);

    EXPECT_FLOAT_EQ(shown.outlinerRestoreSize.x, 0.0F) << "nothing to restore while the panel is showing";
    EXPECT_FLOAT_EQ(shown.inspectorRestoreSize.x, 0.0F);
    EXPECT_FLOAT_EQ(hidden.outlinerRestoreSize.x, kPanelRestoreStripWidthUVE);
    EXPECT_FLOAT_EQ(hidden.inspectorRestoreSize.x, kPanelRestoreStripWidthUVE);
    EXPECT_FLOAT_EQ(hidden.outlinerRestorePos.x, viewport.WorkPos.x);
    EXPECT_FLOAT_EQ(hidden.inspectorRestorePos.x,
                    viewport.WorkPos.x + viewport.WorkSize.x - kPanelRestoreStripWidthUVE);
}

TEST(EditorChromeLayoutUVETest, SplitterGripsExistOnlyWhereThereIsASeam) {
    // A grip over a seam that does not exist is an invisible button that eats a click meant for the
    // viewport. Each one is sized only while both neighbours are on screen.
    const ImGuiViewport viewport = MakeViewportUVE(1920.0F, 1080.0F);
    const EditorChromeLayoutUVE full = ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE);
    EXPECT_FLOAT_EQ(full.sceneSplitterSize.x, kPanelSplitterThicknessUVE);
    EXPECT_FLOAT_EQ(full.inspectorSplitterSize.x, kPanelSplitterThicknessUVE);
    EXPECT_FLOAT_EQ(full.dockSplitterSize.y, kPanelSplitterThicknessUVE);

    // The Outliner | viewport seam still exists when the Outliner is showing, and is at the
    // Outliner's right edge.
    EXPECT_FLOAT_EQ(full.sceneSplitterPos.x + (kPanelSplitterThicknessUVE * 0.5F),
                    full.scenePos.x + full.sceneSize.x);

    const EditorChromeLayoutUVE noOutliner =
        ComputeEditorChromeLayoutUVE(viewport, true, kAssetsPanelHeightUVE, -1.0F, -1.0F, false, true);
    EXPECT_FLOAT_EQ(noOutliner.sceneSplitterSize.x, 0.0F) << "no Outliner, no seam";
    EXPECT_FLOAT_EQ(noOutliner.inspectorSplitterSize.x, kPanelSplitterThicknessUVE);

    const EditorChromeLayoutUVE noDock =
        ComputeEditorChromeLayoutUVE(viewport, false, kAssetsPanelHeightUVE);
    EXPECT_FLOAT_EQ(noDock.dockSplitterSize.y, 0.0F) << "no dock, no grab line above it";
}

TEST(EditorChromeLayoutUVETest, PanelResizeClampsAgreeWithTheLayout) {
    // The drag handles and the layout both call these, so the two cannot disagree about how far a
    // panel may go. That is what stops a handle from sliding away from the cursor near its travel
    // limits.
    constexpr float kTotalWidth = 1920.0F;
    const float sceneMax = kTotalWidth * kScenePanelResizeMaxFractionUVE;
    const float inspectorMax = kTotalWidth * kInspectorPanelResizeMaxFractionUVE;

    EXPECT_FLOAT_EQ(ClampScenePanelWidthUVE(kTotalWidth, 100000.0F), sceneMax);
    EXPECT_FLOAT_EQ(ClampScenePanelWidthUVE(kTotalWidth, -100.0F), kScenePanelResizeMinUVE);
    EXPECT_FLOAT_EQ(ClampInspectorPanelWidthUVE(kTotalWidth, 100000.0F), inspectorMax);
    EXPECT_FLOAT_EQ(ClampInspectorPanelWidthUVE(kTotalWidth, 0.0F), kInspectorPanelResizeMinUVE);

    // The layout honours the same numbers, so a width the handle produced survives the next frame.
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(MakeViewportUVE(kTotalWidth, 1080.0F), true, kAssetsPanelHeightUVE, 100000.0F,
                                     100000.0F);
    EXPECT_FLOAT_EQ(layout.sceneSize.x, sceneMax);
    EXPECT_FLOAT_EQ(layout.inspectorSize.x, inspectorMax);
    EXPECT_GE(layout.viewportSize.x, kMinimumViewportWidthUVE);

    // On a window narrow enough that the two minimums would not both fit, the viewport keeps its
    // minimum and the layout stays usable rather than going negative.
    const EditorChromeLayoutUVE tiny =
        ComputeEditorChromeLayoutUVE(MakeViewportUVE(320.0F, 240.0F), true, kAssetsPanelHeightUVE);
    EXPECT_GE(tiny.viewportSize.x, kMinimumViewportWidthUVE);
    EXPECT_GE(tiny.viewportSize.y, kMinimumViewportHeightUVE);
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
    EXPECT_FLOAT_EQ(layout.logoPos.y, kOriginY);
    EXPECT_FLOAT_EQ(layout.contentBrowserPos.x, kOriginX + layout.sceneSize.x);
    // The dock's tab row belongs to the viewport's column, so it starts where that column does.
    EXPECT_FLOAT_EQ(layout.dockTabBarPos.x, layout.viewportPos.x);
}

} // namespace
} // namespace UVE::Editor::Tests
