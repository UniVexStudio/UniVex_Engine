// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace UVE::Editor {

/// Default axis tags for DrawAxisVectorInputUVE (X red, Y green, Z blue, W grey).
inline constexpr std::array<const char*, 4> kDefaultAxisNamesUVE{"X", "Y", "Z", "W"};
/// Axis tags for a rect extent row: width/height instead of X/Y (only the first two are read).
inline constexpr std::array<const char*, 4> kRectSizeAxisNamesUVE{"W", "H", "", ""};

/// High-performance, modern, soft vector input field inspired by Prism UI and AAA studio tools.
/// Features a unified rounded capsule, soft non-neon axis badges, subtle dark slate surface,
/// and smooth hover/active glow borders.
///
/// Returns true when any component changed this frame. `count` is 2, 3 or 4.
inline bool DrawAxisVectorInputUVE(const char* const id, float* const values, const int count, const float speed,
                                   const float minimum = 0.0F, const float maximum = 0.0F,
                                   const std::array<const char*, 4>& axisNames = kDefaultAxisNamesUVE) {
    // Soft, refined modern studio palette (Soft Crimson Rose, Emerald Sage, Sky Cobalt, Muted Slate)
    static constexpr std::array<ImU32, 4> kAxisColors{
        IM_COL32(228, 86, 98, 235),  // X: Soft Rose
        IM_COL32(52, 178, 108, 235), // Y: Soft Sage
        IM_COL32(68, 142, 238, 235), // Z: Soft Cobalt
        IM_COL32(115, 125, 145, 235) // W: Soft Slate
    };

    ImGui::PushID(id);
    ImGui::BeginGroup();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = 3.0F;
    const float total = ImGui::GetContentRegionAvail().x;
    const float width = std::max(24.0F, (total - (spacing * static_cast<float>(count - 1))) / static_cast<float>(count));
    const float tagWidth = ImGui::CalcTextSize("X").x + 7.0F;
    const float rounding = std::clamp(style.FrameRounding, 3.5F, 5.5F);
    bool changed = false;

    for (int axis = 0; axis < count; ++axis) {
        if (axis > 0) {
            ImGui::SameLine(0.0F, spacing);
        }
        ImGui::PushID(axis);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        const ImVec2 cellMax{origin.x + width, origin.y + height};

        ImDrawList& drawList = *ImGui::GetWindowDrawList();

        // 1. Soft unified capsule background card
        drawList.AddRectFilled(origin, cellMax, IM_COL32(17, 18, 25, 255), rounding);

        // 2. Soft left axis pill tag
        const ImVec2 tagMax{origin.x + tagWidth, origin.y + height};
        drawList.AddRectFilled(origin, tagMax, kAxisColors[static_cast<std::size_t>(axis)], rounding, ImDrawFlags_RoundCornersLeft);

        // 3. Crisp centered letter
        const char* const name = axisNames[static_cast<std::size_t>(axis)];
        const ImVec2 labelSize = ImGui::CalcTextSize(name);
        drawList.AddText(ImVec2{origin.x + ((tagWidth - labelSize.x) * 0.5F), origin.y + ((height - labelSize.y) * 0.5F)},
                         IM_COL32(255, 255, 255, 250), name);

        // Tag dummy spacer for ImGui cursor
        ImGui::Dummy(ImVec2{tagWidth, height});
        ImGui::SameLine(0.0F, 0.0F);

        // 4. Input numeric drag field with transparent background (showing our soft capsule card)
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{3.0F, style.FramePadding.y});
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{1.0F, 1.0F, 1.0F, 0.03F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.39F, 0.40F, 0.95F, 0.10F});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});

        const float fieldWidth = std::max(8.0F, width - tagWidth);
        ImGui::SetNextItemWidth(fieldWidth);

        const char* fieldFormat = "%.3f";
        static constexpr std::array<const char*, 4> kFallbacks{"%.3f", "%.2f", "%.1f", "%.0f"};
        std::array<char, 64> preview{};
        for (const char* const candidate : kFallbacks) {
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

        changed = ImGui::DragFloat("##axis", &values[axis], speed, minimum, maximum, fieldFormat) || changed;

        const bool isFieldActive = ImGui::IsItemActive();
        const bool isFieldHovered = ImGui::IsItemHovered();

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        // 5. Soft border stroke & reactive active glow
        const ImU32 borderColor = isFieldActive
            ? IM_COL32(99, 102, 241, 230) // Soft Indigo active ring
            : (isFieldHovered ? IM_COL32(65, 70, 95, 255) : IM_COL32(36, 39, 54, 255));
        drawList.AddRect(origin, cellMax, borderColor, rounding, 0, isFieldActive ? 1.5F : 1.0F);

        if (isFieldHovered) {
            ImGui::SetTooltip("%s: %.6g", name, static_cast<double>(values[axis]));
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace UVE::Editor
