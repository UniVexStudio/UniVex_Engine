// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Editor Preferences window: every visible editor setting, drawn from its descriptor. Nothing
// here knows what a particular setting means - a new setting declared in editor_settings_uve.cpp
// appears in the right category with the right control, range, tooltip and reset button.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "editor_color_field_uve.h"
#include "uve/editor/editor_settings_uve.h"

namespace UVE::Editor {
namespace {

using Config::SettingDescriptorUVE;
using Config::SettingTypeUVE;
using Config::SettingValueUVE;

/// The theme's check-mark blue: marks a setting that differs from its default.
constexpr ImVec4 kModifiedAccentUVE{0.561F, 0.706F, 0.847F, 1.0F};

/// One visible setting and whether it differs from its default, worked out once per frame.
struct PreferenceRowUVE final {
    const SettingDescriptorUVE* descriptor = nullptr;
    bool modified = false;
};

/// A counter-clockwise arrow, "back to the default".
void DrawResetGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const ImU32 color) {
    constexpr float kPi = 3.14159265F;
    const float radius = size * 0.3F;
    const float thickness = std::max(1.0F, size * 0.09F);
    const float start = (-0.5F * kPi) + 0.6F;
    drawList.PathArcTo(center, radius, start, start + (1.55F * kPi), 18);
    drawList.PathStroke(color, 0, thickness);
    // The head sits where the arc starts and points back along it, counter-clockwise.
    const ImVec2 radial{std::cos(start), std::sin(start)};
    const ImVec2 along{radial.y, -radial.x};
    const ImVec2 point{center.x + (radial.x * radius), center.y + (radial.y * radius)};
    const float head = size * 0.2F;
    drawList.AddTriangleFilled(ImVec2{point.x + (along.x * head), point.y + (along.y * head)},
                               ImVec2{point.x + (radial.x * head * 0.75F), point.y + (radial.y * head * 0.75F)},
                               ImVec2{point.x - (radial.x * head * 0.75F), point.y - (radial.y * head * 0.75F)}, color);
}

/// "Editor/Viewport/Grid" shown as "Viewport / Grid": the root is the window's own subject.
[[nodiscard]] std::string CategoryTitleUVE(const std::string_view category) {
    const std::size_t first = category.find('/');
    std::string title;
    for (const char c : first == std::string_view::npos ? category : category.substr(first + 1U)) {
        if (c == '/') {
            title += " / ";
        } else {
            title += c;
        }
    }
    return title;
}

[[nodiscard]] float DragSpeedUVE(const SettingDescriptorUVE& descriptor, const double value) {
    if (descriptor.step) {
        return static_cast<float>(*descriptor.step * 0.2);
    }
    return static_cast<float>(std::max(std::abs(value) * 0.005, 0.0001));
}

/// The control for one setting's value. Returns the new value when the person changed it.
[[nodiscard]] std::optional<SettingValueUVE> DrawSettingControlUVE(const SettingDescriptorUVE& descriptor,
                                                                   const SettingValueUVE& current,
                                                                   ColorPickerPreferencesUVE& pickerPreferences) {
    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
    switch (descriptor.type) {
    case SettingTypeUVE::Bool: {
        bool value = std::get<bool>(current);
        return ImGui::Checkbox("##value", &value) ? std::optional<SettingValueUVE>(value) : std::nullopt;
    }
    case SettingTypeUVE::Int: {
        std::int64_t value = std::get<std::int64_t>(current);
        const auto minimum = descriptor.minimum ? static_cast<std::int64_t>(*descriptor.minimum)
                                                : std::numeric_limits<std::int64_t>::lowest();
        const auto maximum = descriptor.maximum ? static_cast<std::int64_t>(*descriptor.maximum)
                                                : std::numeric_limits<std::int64_t>::max();
        const float speed = descriptor.step ? static_cast<float>(*descriptor.step * 0.2) : 0.2F;
        return ImGui::DragScalar("##value", ImGuiDataType_S64, &value, speed, &minimum, &maximum, "%lld",
                                 ImGuiSliderFlags_AlwaysClamp)
                   ? std::optional<SettingValueUVE>(value)
                   : std::nullopt;
    }
    case SettingTypeUVE::Float: {
        double value = std::get<double>(current);
        const double minimum = descriptor.minimum.value_or(std::numeric_limits<double>::lowest());
        const double maximum = descriptor.maximum.value_or(std::numeric_limits<double>::max());
        return ImGui::DragScalar("##value", ImGuiDataType_Double, &value, DragSpeedUVE(descriptor, value), &minimum,
                                 &maximum, "%.4g", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat)
                   ? std::optional<SettingValueUVE>(value)
                   : std::nullopt;
    }
    case SettingTypeUVE::String: {
        std::string buffer = std::get<std::string>(current);
        const std::size_t capacity = descriptor.maxLength != 0U ? descriptor.maxLength : 256U;
        buffer.resize(std::max(capacity, buffer.size()) + 1U, '\0');
        if (!ImGui::InputText("##value", buffer.data(), capacity + 1U)) {
            return std::nullopt;
        }
        buffer.resize(std::char_traits<char>::length(buffer.c_str()));
        return SettingValueUVE{std::move(buffer)};
    }
    case SettingTypeUVE::Enum: {
        const std::int64_t value = std::get<std::int64_t>(current);
        std::optional<SettingValueUVE> picked;
        if (ImGui::BeginCombo("##value", FormatSettingValueUVE(descriptor, current).c_str())) {
            for (const Config::SettingEnumEntryUVE& entry : descriptor.enumEntries) {
                const bool selected = entry.value == value;
                if (ImGui::Selectable(entry.label.c_str(), selected) && !selected) {
                    picked = entry.value;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return picked;
    }
    case SettingTypeUVE::Color: {
        const auto& stored = std::get<Config::SettingColorUVE>(current);
        EditorColorUVE color{stored.r, stored.g, stored.b, stored.a};
        // Edited, committed and cancelled all carry the colour to show now; applying each keeps the
        // setting live while the picker is open, and puts it back when it is cancelled.
        if (DrawColorFieldUVE("##value", descriptor.displayName.c_str(), color, descriptor.colorHasAlpha,
                              pickerPreferences) == ColorFieldEventUVE::None) {
            return std::nullopt;
        }
        return SettingValueUVE{Config::SettingColorUVE{color.r, color.g, color.b, color.a}};
    }
    }
    return std::nullopt;
}

} // namespace

void EditorUVE::OpenEditorPreferencesUVE() noexcept {
    m_preferencesWindowVisible = true;
    m_preferencesFocusSearch = true;
}

void EditorUVE::DrawEditorSettingRowUVE(const SettingDescriptorUVE& descriptor, const bool modified) {
    const std::optional<SettingValueUVE> current = GetEditorSettingUVE(descriptor.id);
    if (!current) {
        return;
    }
    ImGui::PushID(descriptor.id.c_str());
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    const ImVec2 labelMin = ImGui::GetCursorScreenPos();
    if (modified) {
        // A thin bar in the row's left margin, so modified settings stand out when scanning.
        const float gutter = ImGui::GetStyle().CellPadding.x + 1.0F;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2{labelMin.x - gutter, labelMin.y + 2.0F},
            ImVec2{labelMin.x - gutter + 2.0F, labelMin.y + ImGui::GetFrameHeight() - 2.0F},
            ImGui::GetColorU32(kModifiedAccentUVE));
    }
    ImGui::TextUnformatted(descriptor.displayName.c_str());
    const bool restartRequired = descriptor.HasFlagUVE(Config::kSettingFlagRestartRequiredUVE);
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0F);
        if (!descriptor.tooltip.empty()) {
            ImGui::TextUnformatted(descriptor.tooltip.c_str());
        }
        if (restartRequired) {
            ImGui::TextUnformatted("Takes effect after the editor restarts.");
        }
        ImGui::TextDisabled("Default: %s", FormatSettingValueUVE(descriptor, descriptor.defaultValue).c_str());
        ImGui::TextDisabled("%s", descriptor.id.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (restartRequired) {
        ImGui::SameLine();
        ImGui::TextDisabled("(restart)");
    }

    ImGui::TableSetColumnIndex(1);
    std::optional<SettingValueUVE> edited = DrawSettingControlUVE(descriptor, *current, m_colorPickerPreferences);

    ImGui::TableSetColumnIndex(2);
    if (modified) {
        const float size = ImGui::GetFrameHeight();
        if (ImGui::InvisibleButton("##reset", ImVec2{size, size})) {
            edited = descriptor.defaultValue;
        }
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        if (hovered) {
            drawList.AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
        }
        DrawResetGlyphUVE(drawList, ImVec2{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F}, size,
                          ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        ImGui::SetItemTooltip("Reset to %s", FormatSettingValueUVE(descriptor, descriptor.defaultValue).c_str());
    }

    if (edited) {
        static_cast<void>(SetEditorSettingUVE(descriptor.id, *edited));
    }
    ImGui::PopID();
}

void EditorUVE::DrawEditorPreferencesWindowUVE() {
    if (!m_preferencesWindowVisible) {
        return;
    }
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2{fontSize * 50.0F, fontSize * 30.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSizeConstraints(ImVec2{fontSize * 32.0F, fontSize * 16.0F},
                                        ImVec2{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
    if (!ImGui::Begin("Editor Preferences", &m_preferencesWindowVisible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Every setting a person may change, with whether it differs from its default.
    std::vector<PreferenceRowUVE> rows;
    bool anyAdvanced = false;
    for (const SettingDescriptorUVE* descriptor : m_settingsRegistry.GetAllUVE()) {
        if (descriptor->HasFlagUVE(Config::kSettingFlagHiddenUVE)) {
            continue;
        }
        anyAdvanced = anyAdvanced || descriptor->HasFlagUVE(Config::kSettingFlagAdvancedUVE);
        if (descriptor->HasFlagUVE(Config::kSettingFlagAdvancedUVE) && !m_preferencesShowAdvanced) {
            continue;
        }
        const std::optional<SettingValueUVE> value = GetEditorSettingUVE(descriptor->id);
        rows.push_back(PreferenceRowUVE{descriptor, value.has_value() && *value != descriptor->defaultValue});
    }

    // Search, across every category, and the filters beside it.
    const std::string_view query{m_preferencesSearch.data()};
    const float filtersWidth = ImGui::CalcTextSize("Modified only").x + ImGui::GetFrameHeight() +
                               ImGui::GetStyle().ItemInnerSpacing.x +
                               (anyAdvanced ? ImGui::CalcTextSize("Advanced").x + ImGui::GetFrameHeight() +
                                                  (ImGui::GetStyle().ItemSpacing.x * 2.0F)
                                            : 0.0F);
    if (m_preferencesFocusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_preferencesFocusSearch = false;
    }
    ImGui::SetNextItemWidth(-(filtersWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##preferences-search", "Search settings", m_preferencesSearch.data(),
                             m_preferencesSearch.size(), ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::SameLine();
    ImGui::Checkbox("Modified only", &m_preferencesModifiedOnly);
    if (anyAdvanced) {
        ImGui::SameLine();
        ImGui::Checkbox("Advanced", &m_preferencesShowAdvanced);
    }

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const bool searching = !query.empty();
    const auto isShown = [&](const PreferenceRowUVE& row) {
        return MatchesSettingSearchUVE(*row.descriptor, query) &&
               (searching || m_preferencesCategory.empty() ||
                IsInSettingCategoryUVE(row.descriptor->category, m_preferencesCategory)) &&
               (!m_preferencesModifiedOnly || row.modified);
    };

    // Left: the category tree. A dot marks a category holding a modified setting.
    std::vector<const SettingDescriptorUVE*> descriptors;
    descriptors.reserve(rows.size());
    for (const PreferenceRowUVE& row : rows) {
        descriptors.push_back(row.descriptor);
    }
    const std::vector<SettingCategoryNodeUVE> tree = BuildSettingCategoryTreeUVE(descriptors);
    const float treeWidth = fontSize * 12.0F;
    if (ImGui::BeginChild("##preferences-tree", ImVec2{treeWidth, -footerHeight}, ImGuiChildFlags_Borders)) {
        ImGui::BeginDisabled(searching);
        if (ImGui::Selectable("All Settings", searching || m_preferencesCategory.empty())) {
            m_preferencesCategory.clear();
        }
        for (const SettingCategoryNodeUVE& node : tree) {
            const bool anyModified = std::any_of(rows.begin(), rows.end(), [&node](const PreferenceRowUVE& row) {
                return row.modified && IsInSettingCategoryUVE(row.descriptor->category, node.path);
            });
            ImGui::PushID(node.path.c_str());
            ImGui::Indent(ImGui::GetStyle().IndentSpacing * static_cast<float>(node.depth + 1));
            if (ImGui::Selectable(node.name.c_str(), !searching && m_preferencesCategory == node.path)) {
                m_preferencesCategory = node.path;
            }
            if (anyModified) {
                const ImVec2 max = ImGui::GetItemRectMax();
                const float radius = fontSize * 0.16F;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2{max.x - (radius * 3.0F), (ImGui::GetItemRectMin().y + max.y) * 0.5F}, radius,
                    ImGui::GetColorU32(kModifiedAccentUVE), 12);
            }
            ImGui::Unindent(ImGui::GetStyle().IndentSpacing * static_cast<float>(node.depth + 1));
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    ImGui::EndChild();

    // Right: the settings, grouped under their category in tree order.
    ImGui::SameLine();
    std::size_t shownCount = 0U;
    std::size_t shownModifiedCount = 0U;
    if (ImGui::BeginChild("##preferences-rows", ImVec2{0.0F, -footerHeight}, ImGuiChildFlags_Borders)) {
        for (const SettingCategoryNodeUVE& node : tree) {
            bool groupOpen = false;
            for (const PreferenceRowUVE& row : rows) {
                if (row.descriptor->category != node.path || !isShown(row)) {
                    continue;
                }
                if (!groupOpen) {
                    if (shownCount != 0U) {
                        ImGui::Spacing();
                    }
                    ImGui::SeparatorText(CategoryTitleUVE(node.path).c_str());
                    groupOpen = ImGui::BeginTable(node.path.c_str(), 3, ImGuiTableFlags_SizingStretchProp);
                    if (!groupOpen) {
                        break;
                    }
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.42F);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
                    ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                }
                DrawEditorSettingRowUVE(*row.descriptor, row.modified);
                ++shownCount;
                shownModifiedCount += row.modified ? 1U : 0U;
            }
            if (groupOpen) {
                ImGui::EndTable();
            }
        }
        if (shownCount == 0U) {
            // Say why the list is empty, and offer the way back.
            ImGui::Spacing();
            if (searching) {
                ImGui::TextDisabled("No settings match \"%s\".", m_preferencesSearch.data());
                if (ImGui::Button("Clear Search")) {
                    m_preferencesSearch.fill('\0');
                }
            } else if (m_preferencesModifiedOnly) {
                ImGui::TextDisabled("Every setting here is at its default.");
            } else {
                ImGui::TextDisabled("No settings in this category.");
            }
        }
    }
    ImGui::EndChild();

    // Footer: where changes go, and resetting what is shown.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Changes apply at once and are kept with your editor session.");
    const char* resetLabel = "Reset to Defaults...";
    const float resetWidth = ImGui::CalcTextSize(resetLabel).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - resetWidth));
    ImGui::BeginDisabled(shownModifiedCount == 0U);
    if (ImGui::Button(resetLabel)) {
        ImGui::OpenPopup("Reset Preferences");
    }
    ImGui::EndDisabled();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    ImGui::SetNextWindowPos(ImVec2{windowPos.x + (windowSize.x * 0.5F), windowPos.y + (windowSize.y * 0.5F)},
                            ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    if (ImGui::BeginPopupModal("Reset Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Reset %zu shown setting%s to %s default?", shownModifiedCount,
                    shownModifiedCount == 1U ? "" : "s", shownModifiedCount == 1U ? "its" : "their");
        ImGui::Spacing();
        if (ImGui::Button("Reset") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            for (const PreferenceRowUVE& row : rows) {
                if (row.modified && isShown(row)) {
                    static_cast<void>(SetEditorSettingUVE(row.descriptor->id, row.descriptor->defaultValue));
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace UVE::Editor
