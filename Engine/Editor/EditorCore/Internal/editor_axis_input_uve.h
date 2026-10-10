// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace UVE::Editor {

/// The Inspector's float formats: one spelling per decimal count, so every float row -
/// Transform, metadata scalars and vectors, script exports - shows the same precision the
/// same way. The setting's own range (EditorUVE::kInspectorFloatPrecisionMinUVE..MaxUVE)
/// matches this table; the clamp keeps a caller that passes anything else on a real entry
/// instead of off the end.
[[nodiscard]] inline const char* InspectorFloatFormatUVE(const int decimals) noexcept {
    static constexpr std::array<const char*, 7> kFormats{"%.0f", "%.1f", "%.2f", "%.3f",
                                                         "%.4f", "%.5f", "%.6f"};
    return kFormats[static_cast<std::size_t>(std::clamp(decimals, 0, 6))];
}

/// One vector field per axis, sharing the available width equally, each led by a coloured axis tag
/// (X red, Y green, Z blue, W grey) drawn inside the field's own frame. The number is drawn after
/// the tag and clipped to the field, so it can never spill over the box or into its neighbour -
/// which is what happened when three fixed-width fields were squeezed into a narrow panel.
///
/// Returns true when any component changed this frame. `count` is 2, 3 or 4. The fields form one
/// group, so ImGui::IsItemActive() and IsItemDeactivated() after the call describe the whole
/// vector: a drag on any axis is one edit, which callers record as one undo step when it ends.
/// `maxDecimals` caps the chain at the author's chosen precision - 3 is the shipped look, and
/// the Inspector passes its float-precision setting while every other caller keeps the default.
inline bool DrawAxisVectorInputUVE(const char* const id, float* const values, const int count, const float speed,
                                   const float minimum = 0.0F, const float maximum = 0.0F,
                                   const int maxDecimals = 3) {
    static constexpr std::array<ImU32, 4> kAxisColors{IM_COL32(214, 72, 72, 255), IM_COL32(96, 180, 72, 255),
                                                      IM_COL32(72, 128, 222, 255), IM_COL32(140, 140, 150, 255)};
    static constexpr std::array<const char*, 4> kAxisNames{"X", "Y", "Z", "W"};
    ImGui::PushID(id);
    ImGui::BeginGroup();
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
        // As many decimals as the field can show whole, up to the author's chosen precision: a
        // narrow panel shows "1.25" rather than a cut-off "1.250" - but squeezing never shows a
        // different number, and the choice itself is always honoured. The exact value is always
        // in the tooltip, and typing is never limited.
        const int capped = std::clamp(maxDecimals, 0, 6);
        const char* fieldFormat = InspectorFloatFormatUVE(capped);
        std::array<char, 64> preview{};
        for (int decimals = capped; decimals >= 0; --decimals) {
            // Whole numbers only when the value is one: 0.5 shown as "0" is a wrong number, while a
            // clipped "0.5" is merely cut short (and the tooltip has it exactly). The guard applies
            // while squeezing below the choice, never to the choice itself.
            if (decimals == 0 && decimals < capped &&
                std::abs(values[axis] - std::round(values[axis])) > 1.0e-4F) {
                break;
            }
            fieldFormat = InspectorFloatFormatUVE(decimals);
            std::snprintf(preview.data(), preview.size(), fieldFormat, static_cast<double>(values[axis]));
            if (ImGui::CalcTextSize(preview.data()).x + 6.0F <= fieldWidth) {
                break;
            }
        }
        // Drag to scrub; double-click or Ctrl+click to type.
        changed = ImGui::DragFloat("##axis", &values[axis], speed, minimum, maximum, fieldFormat) || changed;
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %.6g", name, static_cast<double>(values[axis]));
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace UVE::Editor
