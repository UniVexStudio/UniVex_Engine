// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <imgui.h>

#include <cstdint>
#include <cstdio>

namespace UVE::Editor {

/// Dock position slots for panels and docks
enum class EditorDockSlotUVE : std::uint8_t {
    Left,
    Right,
    Bottom,
    Floating
};

inline constexpr float kViewportTabInsetX = 0.0F;

/// Empty dark header background shared behind the titlebar, menu row, and tab strips so the
/// rounded tabs sit directly on an empty dark row without boxed frames or border gaps.
inline constexpr ImU32 kEditorHeaderStripColorUVE = IM_COL32(14, 17, 22, 255);

/// The active tab look used by every tab strip: top-only rounded corners, a flat bottom that
/// merges into the panel under it, and a thin blue rim that fades out within the first few pixels.
/// `drawFill` is false where ImGui has already drawn the tab body and its label.
inline void DrawActiveTopRoundedTabUVE(ImDrawList& drawList, const ImVec2 tabMin, const ImVec2 tabMax,
                                       const ImU32 fillCol, const float topRounding = 6.0F,
                                       const bool drawFill = true) {
    if (drawFill) {
        drawList.AddRectFilled(tabMin, tabMax, fillCol, topRounding, ImDrawFlags_RoundCornersTop);
    }
    const float tabHeight = std::max(1.0F, tabMax.y - tabMin.y);
    const int fadeSteps = std::max(2, static_cast<int>(std::floor(std::min(8.0F, tabHeight * 0.4F))));
    for (int step = 0; step < fadeSteps; ++step) {
        const float y = static_cast<float>(step);
        float inset = 0.0F;
        if (y + 0.5F < topRounding) {
            const float dy = topRounding - (y + 0.5F);
            inset = topRounding - std::sqrt(std::max(0.0F, (topRounding * topRounding) - (dy * dy)));
        }
        int alpha = 0;
        if (step == 0) {
            alpha = 140;
        } else {
            const float t = static_cast<float>(step - 1) / static_cast<float>(std::max(1, fadeSteps - 1));
            alpha = static_cast<int>(std::lround(40.0F * (1.0F - t) * (1.0F - t)));
        }
        if (alpha > 0 && (tabMin.x + inset) < (tabMax.x - inset)) {
            drawList.AddRectFilled(ImVec2{tabMin.x + inset, tabMin.y + y},
                                   ImVec2{tabMax.x - inset, tabMin.y + y + 1.0F}, IM_COL32(46, 136, 248, alpha));
        }
    }
    if (drawFill) {
        drawList.AddRect(tabMin, tabMax, IM_COL32(60, 70, 84, 175), topRounding, ImDrawFlags_RoundCornersTop, 1.0F);
        drawList.AddLine(ImVec2{tabMin.x + 1.0F, tabMax.y - 1.0F}, ImVec2{tabMax.x - 1.0F, tabMax.y - 1.0F}, fillCol,
                         1.0F);
    }
}

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

/// 3-dot (⋮) button for dock options with pill hover background and crisp vertical circles.
inline bool DrawDockThreeDotsButtonUVE(ImDrawList& drawList, const char* strId, const ImVec2 pos, const ImVec2 size) {
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(strId, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 bMin = ImGui::GetItemRectMin();
    const ImVec2 bMax = ImGui::GetItemRectMax();
    const ImVec2 center{(bMin.x + bMax.x) * 0.5F, (bMin.y + bMax.y) * 0.5F};
    if (hovered) {
        drawList.AddRectFilled(bMin, bMax, IM_COL32(56, 66, 80, 220), 3.0F);
        ImGui::SetTooltip("Dock Options & Position");
    }
    const ImU32 dotCol = hovered ? IM_COL32(245, 248, 255, 255) : IM_COL32(185, 196, 212, 230);
    for (int dot = -1; dot <= 1; ++dot) {
        drawList.AddCircleFilled(ImVec2{center.x, center.y + (static_cast<float>(dot) * 4.0F)}, 1.4F, dotCol, 8);
    }
    return pressed;
}

/// AAA Visual Dock Position Selector popup with interactive miniature layout and real-time screen overlay highlights.
inline void DrawVisualDockPositionSelectorUVE(const char* popupId, const char* panelName,
                                             const EditorDockSlotUVE currentSlot, bool* isVisible,
                                             const EditorChromeLayoutUVE& layout,
                                             const ImVec2 anchorMin = ImVec2{0, 0},
                                             const ImVec2 anchorMax = ImVec2{0, 0}) {
    const ImGuiViewport* const mainVp = ImGui::GetMainViewport();
    const float edMinX = mainVp->WorkPos.x;
    const float edMinY = mainVp->WorkPos.y;
    const float edMaxX = edMinX + mainVp->WorkSize.x;
    const float edMaxY = edMinY + mainVp->WorkSize.y;

    constexpr float kEstPopupW = 188.0F;
    constexpr float kEstPopupH = 196.0F;

    if (anchorMax.x > anchorMin.x) {
        float posX = anchorMin.x;
        float pivotX = 0.0F;
        // When opening rightward would exceed the editor's right boundary (or within 8px margin),
        // flip to open INWARD to the left!
        if (anchorMin.x + kEstPopupW > edMaxX - 8.0F) {
            posX = anchorMax.x;
            pivotX = 1.0F;
        } else if (anchorMin.x < edMinX + 8.0F) {
            posX = edMinX + 8.0F;
            pivotX = 0.0F;
        }

        float posY = anchorMax.y + 2.0F;
        float pivotY = 0.0F;
        // When opening downward would exceed the editor's bottom boundary,
        // flip to open INWARD upward!
        if (posY + kEstPopupH > edMaxY - 8.0F) {
            posY = anchorMin.y - 2.0F;
            pivotY = 1.0F;
        } else if (anchorMin.y < edMinY + 8.0F) {
            posY = edMinY + 8.0F;
            pivotY = 0.0F;
        }

        ImGui::SetNextWindowPos(ImVec2{posX, posY}, ImGuiCond_Appearing, ImVec2{pivotX, pivotY});
    }

    if (ImGui::BeginPopup(popupId)) {
        // Enforce hard boundary clamping: the popup window must NEVER protrude outside the editor window!
        const ImVec2 curPos = ImGui::GetWindowPos();
        const ImVec2 curSize = ImGui::GetWindowSize();
        const float clampedX = std::clamp(curPos.x, edMinX + 4.0F, std::max(edMinX + 4.0F, edMaxX - curSize.x - 4.0F));
        const float clampedY = std::clamp(curPos.y, edMinY + 4.0F, std::max(edMinY + 4.0F, edMaxY - curSize.y - 4.0F));
        if (clampedX != curPos.x || clampedY != curPos.y) {
            ImGui::SetWindowPos(ImVec2{clampedX, clampedY});
        }

        ImGui::TextDisabled("DOCK POSITION: %s", panelName);
        ImGui::Separator();

        // Visual Dock Layout Icon widget: 164px wide x 92px high
        const ImVec2 widgetPos = ImGui::GetCursorScreenPos();
        constexpr float kW = 164.0F;
        constexpr float kH = 92.0F;
        const ImVec2 widgetMin = widgetPos;
        const ImVec2 widgetMax{widgetMin.x + kW, widgetMin.y + kH};

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        ImDrawList* const fgDrawList = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());

        // Background of mini editor frame
        drawList->AddRectFilled(widgetMin, widgetMax, IM_COL32(18, 21, 26, 255), 4.0F);
        drawList->AddRect(widgetMin, widgetMax, IM_COL32(48, 56, 68, 220), 4.0F, 0, 1.0F);

        // Mini Titlebar (top 10px)
        drawList->AddRectFilled(widgetMin, ImVec2{widgetMax.x, widgetMin.y + 10.0F}, IM_COL32(28, 33, 40, 255), 4.0F,
                                ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2{widgetMin.x, widgetMin.y + 10.0F}, ImVec2{widgetMax.x, widgetMin.y + 10.0F},
                          IM_COL32(42, 50, 62, 220));

        // Slots layout:
        const ImVec2 leftMin{widgetMin.x + 3.0F, widgetMin.y + 13.0F};
        const ImVec2 leftMax{widgetMin.x + 44.0F, widgetMin.y + 64.0F};

        const ImVec2 centerMin{widgetMin.x + 46.0F, widgetMin.y + 13.0F};
        const ImVec2 centerMax{widgetMin.x + 118.0F, widgetMin.y + 64.0F};

        const ImVec2 rightMin{widgetMin.x + 120.0F, widgetMin.y + 13.0F};
        const ImVec2 rightMax{widgetMax.x - 3.0F, widgetMin.y + 64.0F};

        const ImVec2 bottomMin{widgetMin.x + 3.0F, widgetMin.y + 66.0F};
        const ImVec2 bottomMax{widgetMax.x - 3.0F, widgetMax.y - 3.0F};

        // Draw center viewport dummy
        drawList->AddRectFilled(centerMin, centerMax, IM_COL32(24, 28, 35, 255), 2.0F);
        drawList->AddRect(centerMin, centerMax, IM_COL32(38, 44, 54, 200), 2.0F);
        const ImVec2 vpTextSize = ImGui::CalcTextSize("3D");
        drawList->AddText(ImVec2{(centerMin.x + centerMax.x - vpTextSize.x) * 0.5F,
                                 (centerMin.y + centerMax.y - vpTextSize.y) * 0.5F},
                          IM_COL32(90, 100, 115, 200), "3D");

        // Helper for interactive slot button
        const auto handleSlot = [&](const char* slotId, const ImVec2 sMin, const ImVec2 sMax, const char* label,
                                    const EditorDockSlotUVE slotType, const ImVec2 screenOverlayMin,
                                    const ImVec2 screenOverlayMax) {
            ImGui::SetCursorScreenPos(sMin);
            const ImVec2 sSize{sMax.x - sMin.x, sMax.y - sMin.y};
            const bool pressed = ImGui::InvisibleButton(slotId, sSize);
            const bool hovered = ImGui::IsItemHovered();
            const bool isCurrent = (currentSlot == slotType);

            ImU32 fillCol = IM_COL32(28, 33, 42, 230);
            ImU32 borderCol = IM_COL32(48, 58, 72, 220);
            ImU32 textCol = IM_COL32(160, 172, 190, 230);

            if (hovered) {
                fillCol = IM_COL32(32, 112, 228, 240);
                borderCol = IM_COL32(110, 185, 255, 255);
                textCol = IM_COL32(255, 255, 255, 255);

                // Live screen overlay highlight on the actual editor window!
                if (screenOverlayMax.x > screenOverlayMin.x && screenOverlayMax.y > screenOverlayMin.y) {
                    fgDrawList->AddRectFilled(screenOverlayMin, screenOverlayMax, IM_COL32(36, 120, 240, 75), 4.0F);
                    fgDrawList->AddRect(screenOverlayMin, screenOverlayMax, IM_COL32(96, 185, 255, 245), 4.0F, 0, 2.5F);

                    char badgeText[64]{};
                    std::snprintf(badgeText, sizeof(badgeText), "Dock: %s", label);
                    const ImVec2 badgeTextSize = ImGui::CalcTextSize(badgeText);
                    const ImVec2 badgeCenter{(screenOverlayMin.x + screenOverlayMax.x) * 0.5F,
                                             (screenOverlayMin.y + screenOverlayMax.y) * 0.5F};
                    constexpr float kBadgePadX = 14.0F;
                    constexpr float kBadgePadY = 6.0F;
                    const ImVec2 bMin{badgeCenter.x - (badgeTextSize.x * 0.5F) - kBadgePadX,
                                      badgeCenter.y - (badgeTextSize.y * 0.5F) - kBadgePadY};
                    const ImVec2 bMax{badgeCenter.x + (badgeTextSize.x * 0.5F) + kBadgePadX,
                                      badgeCenter.y + (badgeTextSize.y * 0.5F) + kBadgePadY};
                    fgDrawList->AddRectFilled(bMin, bMax, IM_COL32(16, 20, 28, 230), 6.0F);
                    fgDrawList->AddRect(bMin, bMax, IM_COL32(96, 185, 255, 240), 6.0F, 0, 1.5F);
                    fgDrawList->AddText(ImVec2{badgeCenter.x - (badgeTextSize.x * 0.5F),
                                               badgeCenter.y - (badgeTextSize.y * 0.5F)},
                                        IM_COL32(245, 250, 255, 255), badgeText);
                }
            } else if (isCurrent) {
                fillCol = IM_COL32(26, 68, 128, 220);
                borderCol = IM_COL32(60, 130, 220, 220);
                textCol = IM_COL32(220, 235, 255, 255);
            }

            drawList->AddRectFilled(sMin, sMax, fillCol, 2.0F);
            drawList->AddRect(sMin, sMax, borderCol, 2.0F, 0, hovered ? 1.5F : 1.0F);

            const ImVec2 lSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2{(sMin.x + sMax.x - lSize.x) * 0.5F,
                                     (sMin.y + sMax.y - lSize.y) * 0.5F},
                              textCol, label);

            return pressed;
        };

        const ImVec2 leftScreenMin = layout.scenePos;
        const ImVec2 leftScreenMax{layout.scenePos.x + layout.sceneSize.x, layout.scenePos.y + layout.sceneSize.y};

        const ImVec2 rightScreenMin = layout.inspectorPos;
        const ImVec2 rightScreenMax{layout.inspectorPos.x + layout.inspectorSize.x,
                                    layout.inspectorPos.y + layout.inspectorSize.y};

        const ImVec2 bottomScreenMin = layout.contentBrowserPos;
        const ImVec2 bottomScreenMax{layout.contentBrowserPos.x + layout.contentBrowserSize.x,
                                     layout.contentBrowserPos.y + layout.contentBrowserSize.y + layout.dockTabBarSize.y};

        if (handleSlot("##slot-left", leftMin, leftMax, "Left", EditorDockSlotUVE::Left, leftScreenMin, leftScreenMax)) {
            ImGui::CloseCurrentPopup();
        }
        if (handleSlot("##slot-right", rightMin, rightMax, "Right", EditorDockSlotUVE::Right, rightScreenMin,
                       rightScreenMax)) {
            ImGui::CloseCurrentPopup();
        }
        if (handleSlot("##slot-bottom", bottomMin, bottomMax, "Bottom", EditorDockSlotUVE::Bottom, bottomScreenMin,
                       bottomScreenMax)) {
            ImGui::CloseCurrentPopup();
        }

        // Advance cursor past the miniature layout widget
        ImGui::SetCursorScreenPos(ImVec2{widgetMin.x, widgetMax.y + 6.0F});
        ImGui::Separator();

        if (ImGui::MenuItem("Make Floating")) {
            ImGui::CloseCurrentPopup();
        }

        char hideBuf[64]{};
        std::snprintf(hideBuf, sizeof(hideBuf), "Hide %s", panelName);
        if (ImGui::MenuItem(hideBuf)) {
            if (isVisible != nullptr) {
                *isVisible = false;
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

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
constexpr float kEditorOsTitleBarRowHeightUVE = 24.0F;
constexpr float kEditorMenuBarRowHeightUVE = 24.0F;
constexpr float kEditorTitleBarHeightUVE = kEditorOsTitleBarRowHeightUVE + kEditorMenuBarRowHeightUVE;
constexpr float kEditorTabRowHeightUVE = 26.0F;
constexpr float kEditorToolbarHeightUVE = 52.0F;
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
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;

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
    const float availableTotalHeight =
        std::max(kMinimumViewportHeightUVE + kDockTabBarHeightUVE, totalHeight - chromeHeight);
    const float dock = bottomDockVisible ? ClampBottomDockHeightUVE(availableTotalHeight, dockHeight) : 0.0F;
    const float dockTabBarHeight = kDockTabBarHeightUVE;
    const float dockBodyHeight = bottomDockVisible ? std::max(0.0F, dock - dockTabBarHeight) : 0.0F;

    // The dock tab bar sits anchored at the very bottom edge of the frame.
    const float dockTabBarY = originY + totalHeight - dockTabBarHeight;
    // When the dock is visible, the dock body sits directly above the dock tab bar.
    const float dockBodyY = dockTabBarY - dockBodyHeight;

    // The upper panels (Outliner, Viewport, Inspector) extend from their respective tops
    // down to the top of the dock area (either dockBodyY if visible, or dockTabBarY if collapsed).
    const float upperAreaBottom = bottomDockVisible ? dockBodyY : dockTabBarY;

    const float top = originY + chromeHeight;
    const float sideTop = originY + kEditorTitleBarHeightUVE;
    const float sceneTop = originY + kEditorTitleBarHeightUVE + kEditorTabRowHeightUVE;
    const float upperHeight = std::max(kMinimumViewportHeightUVE, upperAreaBottom - top);
    const float sceneHeight = std::max(kMinimumViewportHeightUVE, upperAreaBottom - sceneTop);
    const float sideHeight = std::max(kMinimumViewportHeightUVE + kEditorToolbarHeightUVE, upperAreaBottom - sideTop);

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

    EditorChromeLayoutUVE layout{};
    layout.sceneVisible = sceneVisible;
    layout.inspectorVisible = inspectorVisible;
    layout.logoPos = ImVec2{originX, originY};
    layout.logoSize = ImVec2{kEditorTitleBarHeightUVE, kEditorTitleBarHeightUVE};
    layout.outlinerHeaderPos = ImVec2{originX, sideTop};
    layout.outlinerHeaderSize = ImVec2{sceneWidth, kEditorTabRowHeightUVE};
    layout.scenePos = ImVec2{originX, sceneTop};
    layout.sceneSize = ImVec2{sceneWidth, sceneHeight};
    layout.viewportHeaderPos = ImVec2{originX + sceneWidth, sideTop};
    layout.viewportHeaderSize = ImVec2{viewportWidth, kEditorToolbarHeightUVE};
    layout.viewportPos = ImVec2{originX + sceneWidth, top};
    layout.viewportSize = ImVec2{viewportWidth, upperHeight};
    layout.inspectorPos = ImVec2{originX + sceneWidth + viewportWidth, sideTop};
    layout.inspectorSize = ImVec2{inspectorWidth, sideHeight};

    // Full-width bottom dock: spans across the whole editor at the bottom under Outliner, Viewport, and Inspector
    layout.dockTabBarPos = ImVec2{originX, dockTabBarY};
    layout.dockTabBarSize = ImVec2{totalWidth, dockTabBarHeight};
    layout.contentBrowserPos = ImVec2{originX, dockBodyY};
    layout.contentBrowserSize = ImVec2{totalWidth, dockBodyHeight};

    // The grips straddle their seam, so a drag that started on the divider keeps tracking when the
    // pointer wanders a pixel or two onto either panel.
    constexpr float halfGrip = kPanelSplitterThicknessUVE * 0.5F;
    layout.sceneSplitterPos = ImVec2{originX + sceneWidth - halfGrip, sideTop};
    layout.sceneSplitterSize = sceneVisible ? ImVec2{kPanelSplitterThicknessUVE, sideHeight} : ImVec2{0.0F, 0.0F};
    layout.inspectorSplitterPos = ImVec2{originX + sceneWidth + viewportWidth - halfGrip, sideTop};
    layout.inspectorSplitterSize =
        inspectorVisible ? ImVec2{kPanelSplitterThicknessUVE, sideHeight} : ImVec2{0.0F, 0.0F};

    // Full-width horizontal dock splitter above the dock body
    layout.dockSplitterPos = ImVec2{originX, dockBodyY - halfGrip};
    layout.dockSplitterSize =
        bottomDockVisible ? ImVec2{totalWidth, kPanelSplitterThicknessUVE} : ImVec2{0.0F, 0.0F};

    layout.outlinerRestorePos = ImVec2{originX, top};
    layout.outlinerRestoreSize =
        sceneVisible ? ImVec2{0.0F, 0.0F} : ImVec2{kPanelRestoreStripWidthUVE, upperHeight};
    layout.inspectorRestorePos = ImVec2{originX + totalWidth - kPanelRestoreStripWidthUVE, sideTop};
    layout.inspectorRestoreSize =
        inspectorVisible ? ImVec2{0.0F, 0.0F} : ImVec2{kPanelRestoreStripWidthUVE, sideHeight};
    return layout;
}

} // namespace UVE::Editor
