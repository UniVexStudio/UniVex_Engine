// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cstddef>

#include <imgui.h>

namespace UVE::Editor {

/// Where each of the editor's structural panels sits this frame.
///
/// WHY THIS IS A SHARED HEADER RATHER THAN A FILE-LOCAL HELPER. The five core panels must tile
/// the screen with no gaps and no overlaps on every launch, which only holds while every one of
/// them derives its rect from the SAME arithmetic. It lived in editor_uve.cpp's anonymous
/// namespace, which was fine while all five panels lived in that file too - but they are being
/// split into their own translation units, and a per-file copy of this would let two panels
/// disagree about the layout while each looked correct on its own.
///
/// So it moves here, once, before the first panel moves out. Copying it into each new file would
/// have been the smaller diff and the worse decision.
///
/// WHY THIS IS THE ONLY AUTHORITY. A native ImGui dockspace used to run alongside it, laying a
/// second set of rectangles over the same pixels with ratio-based splits of the whole window. The
/// two never agreed: the bottom dock was cut from screen y=0 instead of from the editor's own
/// frame, so it drew under the toolbar; every panel grew a dock tab and a close button on top of
/// the header this layout already draws; and imgui.ini remembered a floating position that then
/// overrode the computed rect. The content browser is not a separate window the user may park
/// anywhere - it is a region of one editor - and the code that let it became one has been removed
/// rather than merely switched off. Everything below is computed for exactly one frame.
struct EditorChromeLayoutUVE final {
    ImVec2 scenePos;
    ImVec2 sceneSize;
    ImVec2 viewportPos;
    ImVec2 viewportSize;
    ImVec2 inspectorPos;
    ImVec2 inspectorSize;
    /// The bottom dock body (Content, Output, Console): under the viewport, between the side panels.
    ImVec2 contentBrowserPos;
    ImVec2 contentBrowserSize;
    /// The strip of dock tabs: the dock's header, on the dock's own left and right edges. While the
    /// dock is hidden it drops to the bottom edge of the frame, because it is then the way back.
    ImVec2 dockTabBarPos;
    ImVec2 dockTabBarSize;
    /// The viewport's header row, over its image: its name, the play controls, and the editor's tools.
    ImVec2 viewportHeaderPos;
    ImVec2 viewportHeaderSize;
    /// The logo's box in the top-left corner, as tall as the title bar and the header row together.
    ImVec2 logoPos;
    ImVec2 logoSize;
    /// The Outliner's header row, beside the logo; the Outliner's tree starts under it.
    ImVec2 outlinerHeaderPos;
    ImVec2 outlinerHeaderSize;
    /// Whether a hidable side panel is showing this frame. A hidden panel's rects are zero-width
    /// and the space it used goes back to the viewport - hiding a panel must widen the canvas,
    /// not leave a hole where the panel used to be.
    bool sceneVisible = true;
    bool inspectorVisible = true;
    /// Drag handles straddling each seam. Zero-sized when the seam does not exist.
    ImVec2 sceneSplitterPos;
    ImVec2 sceneSplitterSize;
    ImVec2 inspectorSplitterPos;
    ImVec2 inspectorSplitterSize;
    ImVec2 dockSplitterPos;
    ImVec2 dockSplitterSize;
    /// The way back for a hidden side panel: a strip on the edge it left behind, sized only while
    /// the panel is hidden. A close button with no matching way back is the bug this fixes.
    ImVec2 outlinerRestorePos;
    ImVec2 outlinerRestoreSize;
    ImVec2 inspectorRestorePos;
    ImVec2 inspectorRestoreSize;
};

/// Shared across the editor's panel translation units.
///
/// These four were file-local constants while every panel lived in editor_uve.cpp. Each is used
/// both inside and outside the hierarchy panel, so splitting that panel out means they must be
/// reachable from two files - and a per-file copy of a drag-drop payload id or a name-length cap
/// is the kind of duplicate that stays correct right up until someone edits one of them.
///
/// Payload id in particular: imgui matches drag sources to drop targets by exact string, so two
/// copies that drift apart do not produce a compile error or a crash. Drag-and-drop simply stops
/// working, silently.
constexpr const char* kHierarchyEntityPayloadUVE = "UVE_SCENE_HIERARCHY_ENTITY";
/// An entity asset dragged out of Content: the payload is its absolute path, NUL-terminated.
constexpr const char* kContentEntityPayloadUVE = "UVE_CONTENT_ENTITY_ASSET";
/// Any other file or folder dragged out of Content (onto a shelf): its content-relative path,
/// NUL-terminated.
constexpr const char* kContentItemPayloadUVE = "UVE_CONTENT_ITEM";
/// Several files dragged out of Content together (a multi-selection): their content-relative
/// paths, NUL-joined with a NUL after each. Acceptors that only take one path ignore this shape.
constexpr const char* kContentItemsPayloadUVE = "UVE_CONTENT_ITEMS";
constexpr const char* kPanelLabelViewportUVE = "\xEE\xA9\x94 Viewport##viewport";
constexpr const char* kPanelLabelSceneUVE = "\xEF\xAB\xBA Outliner##scene-panel";
constexpr const char* kPanelLabelInspectorUVE = "\xEE\xA8\x83 Inspector##right-panel";
/// The one window every bottom-dock tab shares. See DrawBottomDockContentUVE()'s comment on why
/// the dock is a region of the editor rather than six windows that take turns being visible - the
/// six per-section names that used to live here are gone with the six windows they named.
constexpr const char* kPanelLabelBottomDockUVE = "##bottom-dock";
/// The full-width strip along the very bottom. It is drawn whether or not the dock body is, because
/// it is also how a hidden dock is brought back.
constexpr const char* kPanelLabelDockTabsUVE = "##bottom-dock-tabs";

constexpr std::size_t kMaximumEntityNameBytesUVE = 96U;
// An object icon is 16 px: its texture is 64 px, so 16 is an exact mip level and draws crisp.
constexpr float kHierarchyObjectIconSizeUVE = 16.0F;

constexpr float kMinimumViewportWidthUVE = 64.0F;
constexpr float kMinimumViewportHeightUVE = 64.0F;
/// The bottom dock's default height.
constexpr float kAssetsPanelHeightUVE = 192.0F;
constexpr float kMinimumBottomDockHeightUVE = 96.0F;
constexpr float kDockTabBarHeightUVE = 24.0F;
constexpr float kEditorTitleBarHeightUVE = 24.0F;
constexpr float kEditorToolbarHeightUVE = 26.0F;
constexpr float kEditorTopChromeHeightUVE = kEditorTitleBarHeightUVE + kEditorToolbarHeightUVE;
constexpr float kScenePanelWidthFractionUVE = 0.16F;
constexpr float kScenePanelWidthMinUVE = 184.0F;
constexpr float kScenePanelWidthMaxUVE = 600.0F;
constexpr float kInspectorPanelWidthFractionUVE = 0.22F;
constexpr float kInspectorPanelWidthMinUVE = 240.0F;
constexpr float kInspectorPanelWidthMaxUVE = 800.0F;

/// Resize limits - the one place they are written down.
///
/// WHY A SHARED PAIR OF NUMBERS AND NOT A MIN AND MAX IN THE DRAG HANDLER. The handle used to carry
/// its own `std::clamp(..., 140.0F, totalWidth * 0.45F)` while the layout carried a different pair.
/// Dragging then hit whatever the handler said, and the panel settled somewhere the layout had not
/// agreed to - the handle appearing to slip under the cursor near the ends of its travel. The
/// layout function and DrawEditorChromeHandlesUVE() now call these same clamps, so a drag can only
/// ever land on a width the layout will reproduce on the next frame.
///
/// The maxima are fractions rather than pixels because the same editor runs at 1280 and at 4K, and
/// a fixed 640px cap is generous on one and a third of the screen on the other.
constexpr float kScenePanelResizeMinUVE = kScenePanelWidthMinUVE;
constexpr float kScenePanelResizeMaxFractionUVE = 0.45F;
constexpr float kInspectorPanelResizeMinUVE = kInspectorPanelWidthMinUVE;
constexpr float kInspectorPanelResizeMaxFractionUVE = 0.60F;
/// The dock may take most of the column, but never all of it: the viewport keeps this much.
constexpr float kBottomDockMaxFractionUVE = 0.80F;
/// The drag grip. It straddles the seam, so half of it lies over each neighbour; the visible
/// divider is the 1px line the panels already draw, and the grip itself paints nothing until
/// it is hovered or held - then a 2px line on the seam lights up.
///
/// WHY TEN AND NOT SIX. Six was the width ImGui's own edge grip used to cover anyway, so the two
/// never had to be told apart. They do now: the layout-owned panels are NoResize (see
/// kLayoutOwnedPanelFlagsUVE), so this grip is the only thing near a seam that takes the pointer,
/// and the pointer that misses it by four pixels does nothing at all. Ten is +/-5 either side of
/// the divider - the band the eye treats as "the line" - and it costs a panel five pixels of
/// clickable margin at its edge, which is inside the padding every panel already keeps clear.
constexpr float kPanelSplitterThicknessUVE = 10.0F;
/// A hidden side panel leaves this strip behind. Wide enough to hit, thin enough to read as an
/// edge rather than a panel, and identified by a chevron pointing the way the panel returns.
constexpr float kPanelRestoreStripWidthUVE = 14.0F;

/// `requested` clamped to the widths a side panel may actually occupy at this window size.
[[nodiscard]] inline float ClampScenePanelWidthUVE(const float totalWidth, const float requested) {
    const float maximum = std::max(kScenePanelResizeMinUVE, totalWidth * kScenePanelResizeMaxFractionUVE);
    return std::clamp(requested, kScenePanelResizeMinUVE, maximum);
}

[[nodiscard]] inline float ClampInspectorPanelWidthUVE(const float totalWidth, const float requested) {
    const float maximum = std::max(kInspectorPanelResizeMinUVE, totalWidth * kInspectorPanelResizeMaxFractionUVE);
    return std::clamp(requested, kInspectorPanelResizeMinUVE, maximum);
}

/// `requested` clamped to the heights the dock may occupy, given the column it shares with the
/// viewport. Two ceilings apply and the tighter one wins: a fraction of the column, and whatever
/// still leaves the viewport its minimum height.
[[nodiscard]] inline float ClampBottomDockHeightUVE(const float columnHeight, const float requested) {
    const float maximum = std::max(kMinimumBottomDockHeightUVE,
                                   std::min(columnHeight * kBottomDockMaxFractionUVE,
                                            columnHeight - kMinimumViewportHeightUVE));
    return std::clamp(requested, kMinimumBottomDockHeightUVE, maximum);
}

/// The window flags every panel whose rect comes from ComputeEditorChromeLayoutUVE() is created
/// with.
///
/// WHY THIS IS SHARED, AND WHY NoResize IS LOAD-BEARING. ImGui resizes a window from its own
/// edges by default - io.ConfigWindowsResizeFromEdges is on out of the box - so any panel created
/// without NoResize hands its outer few pixels to ImGui instead of to the seam grip that straddles
/// the same pixels. Outliner, Viewport and Inspector were created with only NoCollapse|NoTitleBar,
/// which meant the border of every one of them was a rival resize handle: dragging there resized
/// the ImGui window for exactly one frame, and the next frame's
/// SetNextWindowSize(..., ImGuiCond_Always) put it back. The panel followed the pointer while the
/// button was held and jumped to where it had been the moment it was released - "the editor does
/// not resize", from the outside, while the seam grip straddling that same border worked all along.
///
/// NoSavedSettings is the same argument one frame earlier: uve_editor_layout.ini must never be able
/// to park a panel somewhere the layout did not compute. NoMove is the same argument for dragging:
/// a layout-owned panel cannot be moved away from its rect, only resized by the seam that owns it.
///
/// AND NoBringToFrontOnFocus IS THE HALF THAT MAKES THE OTHER HALF WORK. The seam grips are their
/// own windows, created after every panel each frame so that they sit on top of both neighbours.
/// ImGui's window stack is not redrawn-into-order: a window is raised by being clicked, and a click
/// anywhere in the Viewport - orbiting the camera is the first thing anyone does - moved that panel
/// above the grips and left it there. From then on the ten pixels of seam were the panel's, not the
/// grip's: the seam looked pinned, and the only thing that ever answered a drag on it was ImGui's
/// own edge resize on the panel, which is exactly the resize the layout puts back on the next frame.
/// The editor's panels are fixed regions of one editor, not floating windows competing for the top,
/// so they never need to raise - and with them refusing to, the grips stay where they were put.
inline constexpr ImGuiWindowFlags kLayoutOwnedPanelFlagsUVE =
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBringToFrontOnFocus;

/// The editor's panel rectangles.
///
///   [logo][ title bar: Menu, status ...................................................... ]
///   [logo][ Outliner   ][ Viewport name  > []  tools ................... ][ Inspector tabs ]
///   [ Outliner tree    ][ dock tabs                                       ][ Inspector      ]
///   [                  ][ bottom dock                                     ][                ]
///
/// The logo's box spans the title bar and the header row. The Inspector runs from under the title
/// bar; the Outliner's tree and the viewport's image start under the header row. The dock tabs are
/// the dock's header: same left and right edges as the dock body, directly above it, so the tabs and
/// the body read as one block attached under the canvas. The strip stays when the dock is hidden,
/// dropping to the bottom edge of the frame, since it is how the dock is brought back.
/// `dockHeight` is the whole dock's height - tabs and body - clamped so the viewport keeps its
/// minimum.
///
/// `sceneVisible`/`inspectorVisible` are the panels' own visibility, passed rather than assumed: a
/// hidden panel contributes zero width and the viewport takes the space, which is the whole point of
/// hiding it. The width parameters above still apply when the panel is shown again, so hiding and
/// restoring a panel returns it to the width the author dragged it to.
[[nodiscard]] inline EditorChromeLayoutUVE ComputeEditorChromeLayoutUVE(const ImGuiViewport& viewport,
                                                                        const bool bottomDockVisible,
                                                                        const float dockHeight = kAssetsPanelHeightUVE,
                                                                        const float sceneWidthCustom = -1.0F,
                                                                        const float inspectorWidthCustom = -1.0F,
                                                                        const bool sceneVisible = true,
                                                                        const bool inspectorVisible = true) {
    const float originX = viewport.WorkPos.x;
    const float originY = viewport.WorkPos.y;
    const float totalWidth = viewport.WorkSize.x;
    const float totalHeight = viewport.WorkSize.y;

    const float chromeHeight = kEditorTopChromeHeightUVE;
    // The centre column, under the tool strip. The dock's tab row is reserved only while the dock
    // is hidden, because that is the one state in which the row has to live at the bottom edge on
    // its own. While the dock is showing, its tabs are the dock's header and cost the column
    // nothing - which is why the panels reach the bottom of the frame instead of stopping 24px
    // short above a band that belonged to no one.
    const float columnHeight = std::max(
        kMinimumViewportHeightUVE,
        totalHeight - chromeHeight - (bottomDockVisible ? 0.0F : kDockTabBarHeightUVE));
    // The side columns also take the tool strip's height: they start right under the title bar.
    const float sideTop = originY + kEditorTitleBarHeightUVE;
    const float sideHeight = columnHeight + kEditorToolbarHeightUVE;

    const float defaultSceneWidth =
        std::clamp(totalWidth * kScenePanelWidthFractionUVE, kScenePanelWidthMinUVE, kScenePanelWidthMaxUVE);
    const float defaultInspectorWidth = std::clamp(totalWidth * kInspectorPanelWidthFractionUVE,
                                                   kInspectorPanelWidthMinUVE, kInspectorPanelWidthMaxUVE);

    const float sceneWidth =
        !sceneVisible ? 0.0F
                      : (sceneWidthCustom > 0.0F ? ClampScenePanelWidthUVE(totalWidth, sceneWidthCustom) : defaultSceneWidth);
    const float inspectorWidth = !inspectorVisible
                                     ? 0.0F
                                     : (inspectorWidthCustom > 0.0F
                                            ? ClampInspectorPanelWidthUVE(totalWidth, inspectorWidthCustom)
                                            : defaultInspectorWidth);
    const float viewportWidth = std::max(kMinimumViewportWidthUVE, totalWidth - sceneWidth - inspectorWidth);
    const float top = originY + chromeHeight;
    const float dock = bottomDockVisible ? ClampBottomDockHeightUVE(columnHeight, dockHeight) : 0.0F;

    EditorChromeLayoutUVE layout{};
    layout.sceneVisible = sceneVisible;
    layout.inspectorVisible = inspectorVisible;
    layout.logoPos = ImVec2{originX, originY};
    layout.logoSize = ImVec2{kEditorTopChromeHeightUVE, kEditorTopChromeHeightUVE};
    layout.outlinerHeaderPos = ImVec2{originX + kEditorTopChromeHeightUVE, sideTop};
    layout.outlinerHeaderSize =
        ImVec2{std::max(0.0F, sceneWidth - kEditorTopChromeHeightUVE), kEditorToolbarHeightUVE};
    layout.scenePos = ImVec2{originX, top};
    layout.sceneSize = ImVec2{sceneWidth, columnHeight};
    // The header row starts past the logo even when the Outliner is hidden, so the logo never
    // paints over the viewport's own tab and transport.
    layout.viewportHeaderPos = ImVec2{originX + std::max(sceneWidth, kEditorTopChromeHeightUVE), sideTop};
    layout.viewportHeaderSize = ImVec2{viewportWidth, kEditorToolbarHeightUVE};
    layout.viewportPos = ImVec2{originX + sceneWidth, top};
    layout.viewportSize = ImVec2{viewportWidth, std::max(kMinimumViewportHeightUVE, columnHeight - dock)};
    layout.inspectorPos = ImVec2{originX + sceneWidth + viewportWidth, sideTop};
    layout.inspectorSize = ImVec2{inspectorWidth, sideHeight};
    // The dock is the bottom half of the viewport column, not a band across the whole editor. Its
    // tab row is the top of that block and shares its left and right edges, so the row reads as
    // the dock's own header: one thing attached under the canvas. Drawn full-width at the bottom
    // edge of the screen, the row looked like a second window that had landed there by accident,
    // and nothing connected it to the panel it controlled. When the dock is hidden the row drops
    // to the reserved strip row, still on the column's edges, because it is the way back.
    const float dockStripY = bottomDockVisible ? top + layout.viewportSize.y : top + columnHeight;
    layout.dockTabBarPos = ImVec2{originX + sceneWidth, dockStripY};
    layout.dockTabBarSize = ImVec2{viewportWidth, kDockTabBarHeightUVE};
    layout.contentBrowserPos = ImVec2{originX + sceneWidth, dockStripY + kDockTabBarHeightUVE};
    layout.contentBrowserSize = ImVec2{viewportWidth, std::max(0.0F, dock - kDockTabBarHeightUVE)};

    // The grips straddle their seam, so a drag that started on the divider keeps tracking when the
    // pointer wanders a pixel or two onto either panel.
    constexpr float halfGrip = kPanelSplitterThicknessUVE * 0.5F;
    layout.sceneSplitterPos = ImVec2{originX + sceneWidth - halfGrip, top};
    layout.sceneSplitterSize = sceneVisible ? ImVec2{kPanelSplitterThicknessUVE, columnHeight} : ImVec2{0.0F, 0.0F};
    layout.inspectorSplitterPos = ImVec2{originX + sceneWidth + viewportWidth - halfGrip, sideTop};
    layout.inspectorSplitterSize =
        inspectorVisible ? ImVec2{kPanelSplitterThicknessUVE, sideHeight} : ImVec2{0.0F, 0.0F};
    layout.dockSplitterPos = ImVec2{originX + sceneWidth, top + layout.viewportSize.y - halfGrip};
    layout.dockSplitterSize =
        bottomDockVisible ? ImVec2{viewportWidth, kPanelSplitterThicknessUVE} : ImVec2{0.0F, 0.0F};

    layout.outlinerRestorePos = ImVec2{originX, top};
    layout.outlinerRestoreSize =
        sceneVisible ? ImVec2{0.0F, 0.0F} : ImVec2{kPanelRestoreStripWidthUVE, columnHeight};
    layout.inspectorRestorePos = ImVec2{originX + totalWidth - kPanelRestoreStripWidthUVE, sideTop};
    layout.inspectorRestoreSize =
        inspectorVisible ? ImVec2{0.0F, 0.0F} : ImVec2{kPanelRestoreStripWidthUVE, sideHeight};
    return layout;
}

} // namespace UVE::Editor
