// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_layer_mask_uve.h"

#include <algorithm>
#include <bit>
#include <string>

#include <imgui.h>

#include "editor_layer_mask_field_uve.h"

namespace UVE::Editor {
namespace {

constexpr std::size_t kLayerCountUVE = std::tuple_size_v<LayerNamesUVE>;

[[nodiscard]] bool IsLayerSetUVE(const std::uint32_t mask, const std::size_t index) noexcept {
    return ((mask >> index) & 1U) != 0U;
}

} // namespace

std::string GetLayerLabelUVE(const LayerNamesUVE& names, const std::size_t index) {
    if (index < kLayerCountUVE && !names[index].empty()) {
        return names[index];
    }
    return "Layer " + std::to_string(index + 1U);
}

std::string FormatLayerMaskSummaryUVE(const std::uint32_t mask, const LayerNamesUVE& names,
                                      const std::size_t maxShown) {
    if (mask == 0U) {
        return "None";
    }
    if (mask == 0xFFFFFFFFU) {
        return "All";
    }
    std::string summary;
    std::size_t shown = 0U;
    for (std::size_t index = 0U; index < kLayerCountUVE && shown < maxShown; ++index) {
        if (IsLayerSetUVE(mask, index)) {
            summary += (shown == 0U ? "" : ", ") + GetLayerLabelUVE(names, index);
            ++shown;
        }
    }
    const auto total = static_cast<std::size_t>(std::popcount(mask));
    if (total > shown) {
        summary += " +" + std::to_string(total - shown);
    }
    return summary;
}

LayerMaskFieldEventUVE DrawLayerMaskFieldUVE(const char* const id, std::uint32_t& mask, const LayerNamesUVE& names) {
    LayerMaskFieldEventUVE event = LayerMaskFieldEventUVE::None;
    const float fontSize = ImGui::GetFontSize();
    // Wide enough for two columns of names, whatever the field's own width.
    ImGui::SetNextWindowSizeConstraints(ImVec2{fontSize * 22.0F, 0.0F}, ImVec2{fontSize * 40.0F, fontSize * 40.0F});
    const std::string summary = FormatLayerMaskSummaryUVE(mask, names);
    if (ImGui::BeginCombo(id, summary.c_str(), ImGuiComboFlags_HeightLargest)) {
        std::uint32_t edited = mask;
        if (ImGui::SmallButton("All")) {
            edited = 0xFFFFFFFFU;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) {
            edited = 0U;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Invert")) {
            edited = ~mask;
        }
        const char* editLabel = "Edit Names...";
        const float editWidth = ImGui::CalcTextSize(editLabel).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - editWidth));
        if (ImGui::SmallButton(editLabel)) {
            event = LayerMaskFieldEventUVE::EditNames;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        // Two columns, 1-16 and 17-32, so every layer is in view at once. Unnamed layers are dimmed
        // so the named ones - the ones a project uses - stand out.
        constexpr std::size_t kRowsUVE = kLayerCountUVE / 2U;
        if (ImGui::BeginTable("##layers", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (std::size_t row = 0U; row < kRowsUVE; ++row) {
                ImGui::TableNextRow();
                for (const std::size_t index : {row, row + kRowsUVE}) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(index));
                    bool on = IsLayerSetUVE(edited, index);
                    const bool named = !names[index].empty();
                    // "3  Enemies" for a named layer; just its number, dimmed, for one the project
                    // has not named.
                    const std::string label =
                        std::to_string(index + 1U) + (named ? "  " + names[index] : std::string{});
                    if (!named) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    }
                    if (ImGui::Checkbox(label.c_str(), &on)) {
                        edited = on ? (edited | (1U << index)) : (edited & ~(1U << index));
                    }
                    if (!named) {
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (edited != mask && event == LayerMaskFieldEventUVE::None) {
            mask = edited;
            event = LayerMaskFieldEventUVE::Changed;
        }
        ImGui::EndCombo();
    } else if (ImGui::BeginItemTooltip()) {
        // The full list, when the preview had to shorten it.
        for (std::size_t index = 0U; index < kLayerCountUVE; ++index) {
            if (IsLayerSetUVE(mask, index)) {
                ImGui::Text("%zu  %s", index + 1U, GetLayerLabelUVE(names, index).c_str());
            }
        }
        if (mask == 0U) {
            ImGui::TextUnformatted("No layers");
        }
        ImGui::TextDisabled("0x%08X", mask);
        ImGui::EndTooltip();
    }
    return event;
}

} // namespace UVE::Editor
