// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

#include <imgui.h>

namespace UVE::Editor {

/// Opens a combo whose value sits in the middle of its own frame.
///
/// ImGui draws a combo's preview against the frame's left edge, and no combo flag moves it. A
/// picker is a field that holds one value - a mesh, a material, a bone, or the word Empty - and the
/// Inspector centres what its fields hold, so the words are laid over the frame rather than left
/// hanging next to it.
///
/// Two details are why this is not a one-line wrapper. The frame is measured before the combo
/// opens, and the drawing goes through the draw list captured beforehand: once a combo is open its
/// popup is the current window, so neither the current draw list nor the last item's rect is the
/// frame's any more. And the value is shortened with an ellipsis when it is wider than the space
/// left of the arrow, so a long asset name ends inside the frame instead of running under the
/// arrow.
///
/// The colour comes from ImGuiCol_Text through GetColorU32, which already folds in the alpha a
/// surrounding BeginDisabled applies; a disabled picker's value therefore dims exactly like the
/// combo's own text would, without the caller having to say whether it is disabled.
[[nodiscard]] inline bool BeginCenteredComboUVE(const char* const label, const char* const preview,
                                                const ImGuiComboFlags flags = 0) {
    // Captured before BeginCombo, which may make its popup the current window by the time it
    // returns. The frame's rect is the one BeginCombo itself computes: the combo starts at the
    // cursor, is as wide as the pushed item width, and is a frame height tall.
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    ImFont* const font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImVec2 frameMin = ImGui::GetCursorScreenPos();
    const ImVec2 frameMax{frameMin.x + ImGui::CalcItemWidth(), frameMin.y + ImGui::GetFrameHeight()};

    const bool open = ImGui::BeginCombo(label, "", flags);

    const ImGuiStyle& style = ImGui::GetStyle();
    // The arrow button owns a frame-height strip at the right end of the frame, so the value is
    // centred in the space beside it: centring across the whole frame would crowd the arrow.
    const float left = frameMin.x + style.FramePadding.x;
    const float right = frameMax.x - ImGui::GetFrameHeight();
    const float available = right - left;
    if (available <= 0.0F || preview == nullptr || preview[0] == '\0') {
        return open;
    }

    std::string shown{preview};
    if (ImGui::CalcTextSize(shown.c_str()).x > available) {
        // The widest prefix that still fits with the ellipsis beside it. Measured, not counted: a
        // name's glyphs are not all the same width.
        constexpr const char* kEllipsisUVE = "...";
        const float ellipsisWidth = ImGui::CalcTextSize(kEllipsisUVE).x;
        std::size_t low = 0U;
        std::size_t high = shown.size();
        while (low < high) {
            const std::size_t middle = low + ((high - low + 1U) / 2U);
            if (ImGui::CalcTextSize(shown.c_str(), shown.c_str() + middle).x + ellipsisWidth <= available) {
                low = middle;
            } else {
                high = middle - 1U;
            }
        }
        shown.resize(low);
        shown.append(kEllipsisUVE);
    }

    const ImVec2 textSize = ImGui::CalcTextSize(shown.c_str());
    const ImVec2 position{left + std::max(0.0F, (available - textSize.x) * 0.5F),
                          frameMin.y + std::max(0.0F, ((frameMax.y - frameMin.y) - textSize.y) * 0.5F)};
    // Clipped to the frame, so a row scrolled half out of the panel never paints past it.
    const ImVec4 clip{frameMin.x, frameMin.y, frameMax.x, frameMax.y};
    drawList->AddText(font, fontSize, position, ImGui::GetColorU32(ImGuiCol_Text), shown.c_str(), nullptr,
                      0.0F, &clip);
    return open;
}

} // namespace UVE::Editor
