// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace UVE::Editor {

/// One vector field per axis, sharing the available width equally, each led by a coloured axis tag
/// (X red, Y green, Z blue, W grey) drawn inside the field's own frame. The number is drawn after
/// the tag and clipped to the field, so it can never spill over the box or into its neighbour -
/// which is what happened when three fixed-width fields were squeezed into a narrow panel.
///
/// Returns true when any component changed this frame. `count` is 2, 3 or 4. `typedInput` swaps
/// the drag fields for typed ones.
inline bool DrawAxisVectorInputUVE(const char* const id, float* const values, const int count, const float speed,
                                   const float minimum = 0.0F, const float maximum = 0.0F,
                                   const bool typedInput = false) {
    static constexpr std::array<ImU32, 4> kAxisColors{IM_COL32(214, 72, 72, 255), IM_COL32(96, 180, 72, 255),
                                                      IM_COL32(72, 128, 222, 255), IM_COL32(140, 140, 150, 255)};
    static constexpr std::array<const char*, 4> kAxisNames{"X", "Y", "Z", "W"};
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = 3.0F;
    const float total = ImGui::GetContentRegionAvail().x;
    const float width = std::max(24.0F, (total - (spacing * static_cast<float>(count - 1))) / static_cast<float>(count));
    const float tagWidth = ImGui::CalcTextSize("X").x + 6.0F;
    bool changed = false;
    for (int axis = 0; axis < count; ++axis) {
        if (axis > 0) {
            ImGui::SameLine(0.0F, spacing);
        }
        ImGui::PushID(axis);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        // The tag is its own small block joined to the field's left edge. Dear ImGui centres a drag
        // field's number in the whole frame whatever the padding, so a tag drawn inside the frame
        // would sit under the number; beside it, the number always has the field to itself.
        ImGui::Dummy(ImVec2{tagWidth, height});
        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        drawList.AddRectFilled(origin, ImVec2{origin.x + tagWidth, origin.y + height},
                               kAxisColors[static_cast<std::size_t>(axis)], style.FrameRounding,
                               ImDrawFlags_RoundCornersLeft);
        const char* const name = kAxisNames[static_cast<std::size_t>(axis)];
        const ImVec2 labelSize = ImGui::CalcTextSize(name);
        drawList.AddText(ImVec2{origin.x + ((tagWidth - labelSize.x) * 0.5F), origin.y + ((height - labelSize.y) * 0.5F)},
                         IM_COL32(255, 255, 255, 240), name);
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0F, style.FramePadding.y});
        const float fieldWidth = std::max(8.0F, width - tagWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        // As many decimals as the field can show whole: a narrow panel shows "1.25" rather than a
        // cut-off "1.250" - but never so few that the number shown is a different one. The exact
        // value is always in the tooltip, and typing is never limited.
        const char* fieldFormat = "%.3f";
        static constexpr std::array<const char*, 4> kFallbacks{"%.3f", "%.2f", "%.1f", "%.0f"};
        std::array<char, 64> preview{};
        for (const char* const candidate : kFallbacks) {
            // Whole numbers only when the value is one: 0.5 shown as "0" is a wrong number, while a
            // clipped "0.5" is merely cut short (and the tooltip has it exactly).
            if (candidate == kFallbacks.back() && fieldFormat != nullptr &&
                std::abs(values[axis] - std::round(values[axis])) > 1.0e-4F) {
                break;
            }
            fieldFormat = candidate;
            std::snprintf(preview.data(), preview.size(), candidate, static_cast<double>(values[axis]));
            if (ImGui::CalcTextSize(preview.data()).x + 6.0F <= fieldWidth) {
                break;
            }
        }
        // Typed input for values whose every change is an undo step (a drag would record one per
        // frame); dragging everywhere else.
        changed = (typedInput ? ImGui::InputFloat("##axis", &values[axis], 0.0F, 0.0F, fieldFormat)
                              : ImGui::DragFloat("##axis", &values[axis], speed, minimum, maximum, fieldFormat)) ||
                  changed;
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %.6g", name, static_cast<double>(values[axis]));
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

} // namespace UVE::Editor
