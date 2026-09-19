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
    ImVec2 contentBrowserPos;
    ImVec2 contentBrowserSize;
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
constexpr const char* kPanelLabelSceneUVE = "\xEF\xAB\xBA Scene##scene-panel";
constexpr std::size_t kMaximumEntityNameBytesUVE = 96U;
constexpr float kHierarchyNodeIconRadiusUVE = 7.0F;

constexpr float kMinimumViewportWidthUVE = 64.0F;
constexpr float kMinimumViewportHeightUVE = 64.0F;
constexpr float kAssetsPanelHeightUVE = 192.0F;
constexpr float kEditorTitleBarHeightUVE = 24.0F;
constexpr float kEditorToolbarHeightUVE = 26.0F;
constexpr float kEditorTopChromeHeightUVE = kEditorTitleBarHeightUVE + kEditorToolbarHeightUVE;
constexpr float kScenePanelWidthFractionUVE = 0.15F;
constexpr float kScenePanelWidthMinUVE = 184.0F;
constexpr float kScenePanelWidthMaxUVE = 264.0F;
constexpr float kInspectorPanelWidthFractionUVE = 0.18F;
constexpr float kInspectorPanelWidthMinUVE = 220.0F;
constexpr float kInspectorPanelWidthMaxUVE = 300.0F;

[[nodiscard]] inline EditorChromeLayoutUVE ComputeEditorChromeLayoutUVE(const ImGuiViewport& viewport,
                                                                        const bool bottomDockVisible) {
    const float originX = viewport.WorkPos.x;
    const float originY = viewport.WorkPos.y;
    const float totalWidth = viewport.WorkSize.x;
    const float totalHeight = viewport.WorkSize.y;

    const float chromeHeight = kEditorTopChromeHeightUVE;
    const float reservedBottom = bottomDockVisible ? kAssetsPanelHeightUVE : 0.0F;
    const float workspaceHeight =
        std::max(kMinimumViewportHeightUVE, totalHeight - chromeHeight - reservedBottom);

    const float sceneWidth =
        std::clamp(totalWidth * kScenePanelWidthFractionUVE, kScenePanelWidthMinUVE, kScenePanelWidthMaxUVE);
    const float inspectorWidth = std::clamp(totalWidth * kInspectorPanelWidthFractionUVE,
                                            kInspectorPanelWidthMinUVE, kInspectorPanelWidthMaxUVE);
    const float viewportWidth =
        std::max(kMinimumViewportWidthUVE, totalWidth - sceneWidth - inspectorWidth);

    EditorChromeLayoutUVE layout{};
    layout.scenePos = ImVec2{originX, originY + chromeHeight};
    layout.sceneSize = ImVec2{sceneWidth, workspaceHeight};
    layout.viewportPos = ImVec2{originX + sceneWidth, originY + chromeHeight};
    layout.viewportSize = ImVec2{viewportWidth, workspaceHeight};
    layout.inspectorPos = ImVec2{originX + totalWidth - inspectorWidth, originY + chromeHeight};
    layout.inspectorSize = ImVec2{inspectorWidth, workspaceHeight};
    layout.contentBrowserPos = ImVec2{originX, originY + totalHeight - kAssetsPanelHeightUVE};
    layout.contentBrowserSize = ImVec2{totalWidth, kAssetsPanelHeightUVE};
    return layout;
}

} // namespace UVE::Editor
