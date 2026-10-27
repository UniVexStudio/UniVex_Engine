// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <imgui.h>
#include <string>

namespace UVE::Editor {

struct OSWindowChromeActionUVE {
    bool closed = false;
    bool minimized = false;
    bool maximizedToggled = false;
};

/// Draws a real OS-like window title bar with Minimize (—), Maximize/Restore (□/❐), and Close (✕) buttons.
/// Replaces the default ImGui window decorations so secondary windows look like true AAA desktop windows.
[[nodiscard]] inline OSWindowChromeActionUVE DrawOSWindowTitleBarUVE(
    const char* const title,
    const char* const badgeIcon,
    const ImU32 badgeColor,
    bool& isMinimized,
    bool& isMaximized,
    float& restoredPosX,
    float& restoredPosY,
    float& restoredWidth,
    float& restoredHeight) {
    OSWindowChromeActionUVE action{};
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const float winWidth = ImGui::GetWindowWidth();
    constexpr float kTitleBarHeight = 28.0F;
    const ImVec2 barMin = winPos;
    const ImVec2 barMax = ImVec2{winPos.x + winWidth, winPos.y + kTitleBarHeight};

    // 1. Sleek dark titlebar background with subtle bottom border
    drawList->AddRectFilled(barMin, barMax, IM_COL32(20, 24, 30, 255), 4.0F, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2{barMin.x, barMax.y - 1.0F}, ImVec2{barMax.x, barMax.y - 1.0F},
                      IM_COL32(48, 55, 66, 255), 1.0F);

    // Left accent strip
    drawList->AddRectFilled(barMin, ImVec2{barMin.x + 3.0F, barMax.y}, badgeColor);

    // 2. Icon badge and title text
    float textStartX = barMin.x + 12.0F;
    const float textCenterY = barMin.y + kTitleBarHeight * 0.5F;
    if (badgeIcon && badgeIcon[0] != '\0') {
        const ImVec2 badgeSize = ImGui::CalcTextSize(badgeIcon);
        drawList->AddText(ImVec2{textStartX, textCenterY - badgeSize.y * 0.5F}, badgeColor, badgeIcon);
        textStartX += badgeSize.x + 8.0F;
    }
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    drawList->AddText(ImVec2{textStartX, textCenterY - titleSize.y * 0.5F}, IM_COL32(230, 235, 242, 255), title);

    // 3. Middle drag zone: dragging the title bar moves the window; double clicking toggles maximize
    constexpr float kButtonWidth = 34.0F;
    constexpr float kButtonsTotalWidth = kButtonWidth * 3.0F + 8.0F;
    const float dragWidth = std::max(10.0F, winWidth - kButtonsTotalWidth);
    ImGui::SetCursorScreenPos(barMin);
    ImGui::InvisibleButton("##title_bar_drag", ImVec2{dragWidth, kTitleBarHeight});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (!isMaximized) {
            ImGui::SetWindowPos(ImVec2{winPos.x + delta.x, winPos.y + delta.y});
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        action.maximizedToggled = true;
    }

    // 4. Three OS Window buttons: Minimize, Maximize/Restore, Close
    const float buttonY = barMin.y + 2.0F;
    const float buttonH = kTitleBarHeight - 4.0F;

    // Helper to draw a clean titlebar button
    auto drawButton = [&](const char* id, const float posX, const char* tooltip) -> bool {
        ImGui::SetCursorScreenPos(ImVec2{posX, buttonY});
        const bool clicked = ImGui::InvisibleButton(id, ImVec2{kButtonWidth, buttonH});
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 bMin = ImGui::GetItemRectMin();
        const ImVec2 bMax = ImGui::GetItemRectMax();

        if (active) {
            drawList->AddRectFilled(bMin, bMax, IM_COL32(255, 255, 255, 45), 2.0F);
        } else if (hovered) {
            drawList->AddRectFilled(bMin, bMax, IM_COL32(255, 255, 255, 25), 2.0F);
        }
        if (hovered && tooltip) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return clicked;
    };

    // A. Minimize (—)
    const float minX = barMax.x - kButtonWidth * 3.0F - 4.0F;
    if (drawButton("##btn_minimize", minX, "Minimize")) {
        isMinimized = true;
        action.minimized = true;
        ImGuiViewport* const vp = ImGui::GetWindowViewport();
        if (vp && vp->PlatformHandle) {
            glfwIconifyWindow(static_cast<GLFWwindow*>(vp->PlatformHandle));
        }
    }
    const ImVec2 minCenter{minX + kButtonWidth * 0.5F, buttonY + buttonH * 0.5F};
    drawList->AddLine(ImVec2{minCenter.x - 5.0F, minCenter.y}, ImVec2{minCenter.x + 5.0F, minCenter.y},
                      IM_COL32(200, 205, 215, 255), 1.6F);

    // B. Maximize / Restore (□ / ❐)
    const float maxX = barMax.x - kButtonWidth * 2.0F - 4.0F;
    const bool maxClicked = drawButton("##btn_maximize", maxX, isMaximized ? "Restore Down" : "Maximize");
    if (maxClicked || action.maximizedToggled) {
        isMaximized = !isMaximized;
        action.maximizedToggled = true;
        const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
        ImGuiViewport* const vp = ImGui::GetWindowViewport();
        if (isMaximized) {
            restoredPosX = winPos.x;
            restoredPosY = winPos.y;
            const ImVec2 currentSize = ImGui::GetWindowSize();
            restoredWidth = currentSize.x;
            restoredHeight = currentSize.y;
            ImGui::SetWindowPos(mainViewport->WorkPos);
            ImGui::SetWindowSize(mainViewport->WorkSize);
            if (vp && vp->PlatformHandle && vp != mainViewport) {
                glfwMaximizeWindow(static_cast<GLFWwindow*>(vp->PlatformHandle));
            }
        } else {
            ImGui::SetWindowPos(ImVec2{restoredPosX, restoredPosY});
            ImGui::SetWindowSize(ImVec2{restoredWidth, restoredHeight});
            if (vp && vp->PlatformHandle && vp != mainViewport) {
                glfwRestoreWindow(static_cast<GLFWwindow*>(vp->PlatformHandle));
            }
        }
    }
    const ImVec2 maxCenter{maxX + kButtonWidth * 0.5F, buttonY + buttonH * 0.5F};
    if (isMaximized) {
        // Overlapping rectangles (❐)
        drawList->AddRect(ImVec2{maxCenter.x - 3.0F, maxCenter.y - 5.0F},
                          ImVec2{maxCenter.x + 5.0F, maxCenter.y + 3.0F},
                          IM_COL32(200, 205, 215, 255), 0.0F, 0, 1.2F);
        drawList->AddRectFilled(ImVec2{maxCenter.x - 5.0F, maxCenter.y - 3.0F},
                                ImVec2{maxCenter.x + 3.0F, maxCenter.y + 5.0F},
                                IM_COL32(20, 24, 30, 255));
        drawList->AddRect(ImVec2{maxCenter.x - 5.0F, maxCenter.y - 3.0F},
                          ImVec2{maxCenter.x + 3.0F, maxCenter.y + 5.0F},
                          IM_COL32(200, 205, 215, 255), 0.0F, 0, 1.2F);
    } else {
        // Single rectangle (□)
        drawList->AddRect(ImVec2{maxCenter.x - 4.5F, maxCenter.y - 4.5F},
                          ImVec2{maxCenter.x + 4.5F, maxCenter.y + 4.5F},
                          IM_COL32(200, 205, 215, 255), 0.0F, 0, 1.4F);
    }

    // C. Close (✕) - with OS Red hover
    const float closeX = barMax.x - kButtonWidth - 4.0F;
    ImGui::SetCursorScreenPos(ImVec2{closeX, buttonY});
    const bool closeClicked = ImGui::InvisibleButton("##btn_close", ImVec2{kButtonWidth, buttonH});
    const bool closeHovered = ImGui::IsItemHovered();
    const bool closeActive = ImGui::IsItemActive();
    const ImVec2 cMin = ImGui::GetItemRectMin();
    const ImVec2 cMax = ImGui::GetItemRectMax();

    if (closeActive) {
        drawList->AddRectFilled(cMin, cMax, IM_COL32(241, 112, 122, 255), 2.0F);
    } else if (closeHovered) {
        drawList->AddRectFilled(cMin, cMax, IM_COL32(232, 17, 35, 230), 2.0F);
    }
    if (closeHovered) {
        ImGui::SetTooltip("Close");
    }
    const ImVec2 closeCenter{closeX + kButtonWidth * 0.5F, buttonY + buttonH * 0.5F};
    const ImU32 crossColor = (closeHovered || closeActive) ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 205, 215, 255);
    drawList->AddLine(ImVec2{closeCenter.x - 4.0F, closeCenter.y - 4.0F},
                      ImVec2{closeCenter.x + 4.0F, closeCenter.y + 4.0F}, crossColor, 1.6F);
    drawList->AddLine(ImVec2{closeCenter.x + 4.0F, closeCenter.y - 4.0F},
                      ImVec2{closeCenter.x - 4.0F, closeCenter.y + 4.0F}, crossColor, 1.6F);

    if (closeClicked) {
        action.closed = true;
    }

    // 5. Restore cursor position directly under the titlebar for window body content
    ImGui::SetCursorScreenPos(ImVec2{winPos.x + 6.0F, barMax.y + 6.0F});
    return action;
}

} // namespace UVE::Editor
