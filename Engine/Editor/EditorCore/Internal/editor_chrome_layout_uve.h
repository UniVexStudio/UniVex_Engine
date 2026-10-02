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
    /// The strip of dock tabs along the very bottom, full width.
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
constexpr const char* kPanelLabelSceneUVE = "\xEF\xAB\xBA Outliner##scene-panel";
constexpr std::size_t kMaximumEntityNameBytesUVE = 96U;
// A object icon is 16 px: its texture is 64 px, so 16 is an exact mip level and draws crisp.
constexpr float kHierarchyObjectIconSizeUVE = 16.0F;

constexpr float kMinimumViewportWidthUVE = 64.0F;
constexpr float kMinimumViewportHeightUVE = 64.0F;
/// The bottom dock's default height, and its limits while being dragged.
constexpr float kAssetsPanelHeightUVE = 192.0F;
constexpr float kMinimumBottomDockHeightUVE = 96.0F;
constexpr float kDockTabBarHeightUVE = 24.0F;
constexpr float kEditorTitleBarHeightUVE = 24.0F;
constexpr float kEditorToolbarHeightUVE = 26.0F;
constexpr float kEditorTopChromeHeightUVE = kEditorTitleBarHeightUVE + kEditorToolbarHeightUVE;
constexpr float kScenePanelWidthFractionUVE = 0.15F;
constexpr float kScenePanelWidthMinUVE = 184.0F;
constexpr float kScenePanelWidthMaxUVE = 264.0F;
constexpr float kInspectorPanelWidthFractionUVE = 0.18F;
constexpr float kInspectorPanelWidthMinUVE = 220.0F;
constexpr float kInspectorPanelWidthMaxUVE = 300.0F;

/// The editor's panel rectangles.
///
///   [logo][ title bar: Menu, status ...................................................... ]
///   [logo][ Outliner   ][ Viewport name  > []  tools ................... ][ Inspector tabs ]
///   [ Outliner tree    ][ viewport image                                ][ Inspector      ]
///   [                  ][ bottom dock                                   ][                ]
///   [ dock tabs ............................................................................ ]
///
/// The logo's box spans the title bar and the header row. The Inspector runs from under the title
/// bar; the Outliner's tree and the viewport's image start under the header row. The tab strip spans the full width at the very bottom and stays when the dock is hidden,
/// since it is how the dock is brought back. `dockHeight` is the dock body's height, clamped so the
/// viewport keeps its minimum.
[[nodiscard]] inline EditorChromeLayoutUVE ComputeEditorChromeLayoutUVE(const ImGuiViewport& viewport,
                                                                        const bool bottomDockVisible,
                                                                        const float dockHeight = kAssetsPanelHeightUVE) {
    const float originX = viewport.WorkPos.x;
    const float originY = viewport.WorkPos.y;
    const float totalWidth = viewport.WorkSize.x;
    const float totalHeight = viewport.WorkSize.y;

    const float chromeHeight = kEditorTopChromeHeightUVE;
    // The centre column, under the tool strip.
    const float columnHeight =
        std::max(kMinimumViewportHeightUVE, totalHeight - chromeHeight - kDockTabBarHeightUVE);
    // The side columns also take the tool strip's height: they start right under the title bar.
    const float sideTop = originY + kEditorTitleBarHeightUVE;
    const float sideHeight = columnHeight + kEditorToolbarHeightUVE;
    const float maximumDock = std::max(kMinimumBottomDockHeightUVE, columnHeight - kMinimumViewportHeightUVE);
    const float dock =
        bottomDockVisible ? std::clamp(dockHeight, kMinimumBottomDockHeightUVE, maximumDock) : 0.0F;

    const float sceneWidth =
        std::clamp(totalWidth * kScenePanelWidthFractionUVE, kScenePanelWidthMinUVE, kScenePanelWidthMaxUVE);
    const float inspectorWidth = std::clamp(totalWidth * kInspectorPanelWidthFractionUVE,
                                            kInspectorPanelWidthMinUVE, kInspectorPanelWidthMaxUVE);
    const float viewportWidth =
        std::max(kMinimumViewportWidthUVE, totalWidth - sceneWidth - inspectorWidth);
    const float top = originY + chromeHeight;

    EditorChromeLayoutUVE layout{};
    layout.logoPos = ImVec2{originX, originY};
    layout.logoSize = ImVec2{kEditorTopChromeHeightUVE, kEditorTopChromeHeightUVE};
    layout.outlinerHeaderPos = ImVec2{originX + kEditorTopChromeHeightUVE, sideTop};
    layout.outlinerHeaderSize = ImVec2{std::max(0.0F, sceneWidth - kEditorTopChromeHeightUVE), kEditorToolbarHeightUVE};
    layout.scenePos = ImVec2{originX, top};
    layout.sceneSize = ImVec2{sceneWidth, columnHeight};
    layout.viewportHeaderPos = ImVec2{originX + sceneWidth, sideTop};
    layout.viewportHeaderSize = ImVec2{viewportWidth, kEditorToolbarHeightUVE};
    layout.viewportPos = ImVec2{originX + sceneWidth, top};
    layout.viewportSize = ImVec2{viewportWidth, std::max(kMinimumViewportHeightUVE, columnHeight - dock)};
    layout.inspectorPos = ImVec2{originX + sceneWidth + viewportWidth, sideTop};
    layout.inspectorSize = ImVec2{inspectorWidth, sideHeight};
    layout.contentBrowserPos = ImVec2{originX + sceneWidth, top + layout.viewportSize.y};
    layout.contentBrowserSize = ImVec2{viewportWidth, dock};
    layout.dockTabBarPos = ImVec2{originX, top + columnHeight};
    layout.dockTabBarSize = ImVec2{std::max(totalWidth, sceneWidth + viewportWidth + inspectorWidth),
                                   kDockTabBarHeightUVE};
    return layout;
}

} // namespace UVE::Editor
