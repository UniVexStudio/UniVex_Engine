// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Input Map window: the project's named actions and what triggers them. A binding is added by
// pressing it (listen-for-input) or by picking it from a menu, a binding another action also uses
// is flagged with that action's name, and every change is registered with the input system at
// once, so Play uses it without a restart.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/core/input_map_document_uve.h"
#include "uve/editor/editor_input_map_uve.h"
#include "uve/input/input_names_uve.h"
#include "uve/input/input_system_uve.h"

namespace UVE::Editor {
namespace {

using Input::InputActionTypeUVE;
using Input::InputActionUVE;
using Input::InputBindingUVE;

/// Amber, for a binding another action also uses: worth a look, not necessarily wrong.
constexpr ImVec4 kConflictColorUVE{0.93F, 0.70F, 0.30F, 1.0F};
constexpr ImVec4 kErrorColorUVE{0.93F, 0.42F, 0.40F, 1.0F};

[[nodiscard]] bool ContainsIgnoringCaseUVE(const std::string_view text, const std::string_view word) {
    const auto lower = [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), word.begin(), word.end(),
                       [&lower](const char a, const char b) { return lower(a) == lower(b); }) != text.end();
}

[[nodiscard]] bool HasConflictUVE(const std::vector<InputActionUVE>& actions, const std::size_t index) {
    const InputActionUVE& action = actions[index];
    for (const auto* side : {&action.positiveBindings, &action.negativeBindings}) {
        for (const InputBindingUVE& binding : *side) {
            if (!FindInputConflictsUVE(actions, index, binding).empty()) {
                return true;
            }
        }
    }
    return false;
}

/// A menu of every input by device, for binding one without pressing it (a pad that is not
/// connected, a key the window itself would react to). Returns the one picked, if any.
[[nodiscard]] std::optional<InputBindingUVE> DrawChooseInputMenuUVE() {
    std::optional<InputBindingUVE> picked;
    if (ImGui::BeginMenu("Keyboard")) {
        for (int value = static_cast<int>(Input::KeyCodeUVE::A); value < static_cast<int>(Input::KeyCodeUVE::Count);
             ++value) {
            const auto key = static_cast<Input::KeyCodeUVE>(value);
            if (ImGui::MenuItem(std::string(Input::GetKeyNameUVE(key)).c_str())) {
                picked = Input::KeyBindingUVE(key);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mouse")) {
        for (int value = 0; value < static_cast<int>(Input::MouseButtonUVE::Count); ++value) {
            const auto button = static_cast<Input::MouseButtonUVE>(value);
            if (ImGui::MenuItem(std::string(Input::GetMouseButtonNameUVE(button)).c_str())) {
                picked = Input::MouseBindingUVE(button);
            }
        }
        ImGui::EndMenu();
    }
    for (std::size_t pad = 0U; pad < Input::kMaximumGamepadCountUVE; ++pad) {
        const std::string label = "Gamepad " + std::to_string(pad + 1U);
        if (ImGui::BeginMenu(label.c_str())) {
            for (int value = 0; value < static_cast<int>(Input::GamepadButtonUVE::Count); ++value) {
                const auto button = static_cast<Input::GamepadButtonUVE>(value);
                if (ImGui::MenuItem(std::string(Input::GetGamepadButtonNameUVE(button)).c_str())) {
                    picked = Input::GamepadButtonBindingUVE(pad, button);
                }
            }
            ImGui::Separator();
            for (int value = 0; value < static_cast<int>(Input::GamepadAxisUVE::Count); ++value) {
                const auto axis = static_cast<Input::GamepadAxisUVE>(value);
                const std::string name(Input::GetGamepadAxisNameUVE(axis));
                if (ImGui::MenuItem((name + " +").c_str())) {
                    picked = Input::GamepadAxisBindingUVE(pad, axis, 1.0F);
                }
                if (ImGui::MenuItem((name + " -").c_str())) {
                    picked = Input::GamepadAxisBindingUVE(pad, axis, -1.0F);
                }
            }
            ImGui::EndMenu();
        }
    }
    return picked;
}

} // namespace

void EditorUVE::OpenInputMapUVE() noexcept {
    m_inputMapWindow.visible = true;
}

bool EditorUVE::SaveInputMapUVE() {
    Core::InputMapDocumentUVE& map = m_services->GetInputMapUVE();
    return !map.IsDirtyUVE() || map.SaveUVE();
}

void EditorUVE::CommitInputMapUVE(std::vector<InputActionUVE> actions) {
    Core::InputMapDocumentUVE& map = m_services->GetInputMapUVE();
    map.SetActionsUVE(std::move(actions));
    map.ApplyUVE(m_services->GetInputSystemUVE());
}

void EditorUVE::DrawInputBindingListUVE(std::vector<InputActionUVE>& actions, const std::size_t actionIndex,
                                        const bool negative, bool& changed) {
    InputActionUVE& action = actions[actionIndex];
    std::vector<InputBindingUVE>& bindings = negative ? action.negativeBindings : action.positiveBindings;
    ImGui::PushID(negative ? "negative" : "positive");
    std::optional<std::size_t> removeIndex;
    if (bindings.empty()) {
        ImGui::TextDisabled("Nothing triggers this yet.");
    } else if (ImGui::BeginTable("##bindings", 3, ImGuiTableFlags_SizingStretchProp)) {
        const float frame = ImGui::GetFrameHeight();
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.45F);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch, 0.55F);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Rebind").x + (ImGui::GetStyle().FramePadding.x * 2.0F) + frame +
                                    ImGui::GetStyle().ItemSpacing.x);
        for (std::size_t index = 0U; index < bindings.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Input::DescribeInputBindingUVE(bindings[index]).c_str());
            ImGui::TableSetColumnIndex(1);
            const std::vector<std::string> conflicts = FindInputConflictsUVE(actions, actionIndex, bindings[index]);
            if (!conflicts.empty()) {
                std::string note = "Also " + conflicts.front();
                if (conflicts.size() > 1U) {
                    note += " +" + std::to_string(conflicts.size() - 1U);
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kConflictColorUVE, "%s", note.c_str());
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted("The same input triggers:");
                    for (const std::string& name : conflicts) {
                        ImGui::BulletText("%s", name.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("Rebind")) {
                m_inputMapWindow.listening = true;
                m_inputMapWindow.listenAction = actionIndex;
                m_inputMapWindow.listenNegative = negative;
                m_inputMapWindow.listenReplace = index;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                removeIndex = index;
            }
            ImGui::SetItemTooltip("Remove this binding");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex) {
        bindings.erase(bindings.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
        changed = true;
    }
    if (ImGui::Button("Add Binding...")) {
        m_inputMapWindow.listening = true;
        m_inputMapWindow.listenAction = actionIndex;
        m_inputMapWindow.listenNegative = negative;
        m_inputMapWindow.listenReplace.reset();
    }
    ImGui::SetItemTooltip("Press the key or button to bind.");
    ImGui::SameLine();
    if (ImGui::Button("Choose...")) {
        ImGui::OpenPopup("##choose");
    }
    if (ImGui::BeginPopup("##choose")) {
        if (const std::optional<InputBindingUVE> picked = DrawChooseInputMenuUVE()) {
            bindings.push_back(*picked);
            changed = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void EditorUVE::DrawInputMapWindowUVE() {
    if (!m_inputMapWindow.visible) {
        return;
    }
    InputMapWindowStateUVE& state = m_inputMapWindow;
    Core::InputMapDocumentUVE& map = m_services->GetInputMapUVE();
    std::vector<InputActionUVE> actions = map.GetActionsUVE();
    bool changed = false;
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2{fontSize * 46.0F, fontSize * 28.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSizeConstraints(ImVec2{fontSize * 30.0F, fontSize * 16.0F},
                                        ImVec2{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
    if (ImGui::Begin("Input Map", &state.visible, ImGuiWindowFlags_NoCollapse)) {
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        // Left: the actions.
        if (ImGui::BeginChild("##actions", ImVec2{fontSize * 14.0F, -footerHeight}, ImGuiChildFlags_Borders)) {
            ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
            ImGui::InputTextWithHint("##filter", "Filter actions", state.filter.data(), state.filter.size(),
                                     ImGuiInputTextFlags_EscapeClearsAll);
            const std::string_view filter{state.filter.data()};
            std::optional<std::size_t> deleteIndex;
            for (std::size_t index = 0U; index < actions.size(); ++index) {
                if (!filter.empty() && !ContainsIgnoringCaseUVE(actions[index].name, filter)) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                const bool axis = actions[index].type == InputActionTypeUVE::Axis1D;
                if (ImGui::Selectable(actions[index].name.c_str(), state.selected == index)) {
                    state.selected = index;
                }
                // The kind on the right, and a mark when a binding is shared with another action.
                const ImVec2 max = ImGui::GetItemRectMax();
                const char* kind = axis ? "Axis" : "Button";
                const float kindWidth = ImGui::CalcTextSize(kind).x;
                ImDrawList& drawList = *ImGui::GetWindowDrawList();
                const float textY = ImGui::GetItemRectMin().y;
                drawList.AddText(ImVec2{max.x - kindWidth - 4.0F, textY}, ImGui::GetColorU32(ImGuiCol_TextDisabled), kind);
                if (HasConflictUVE(actions, index)) {
                    drawList.AddText(ImVec2{max.x - kindWidth - 4.0F - ImGui::CalcTextSize("! ").x, textY},
                                     ImGui::GetColorU32(kConflictColorUVE), "!");
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Duplicate")) {
                        InputActionUVE copy = actions[index];
                        copy.name = MakeUniqueActionNameUVE(actions, copy.name);
                        actions.insert(actions.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
                        state.selected = index + 1U;
                        changed = true;
                    }
                    if (ImGui::MenuItem("Delete")) {
                        deleteIndex = index;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (actions.empty()) {
                ImGui::TextDisabled("No actions yet.");
            }
            if (deleteIndex) {
                actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(*deleteIndex));
                changed = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Add Action", ImVec2{-std::numeric_limits<float>::min(), 0.0F})) {
                actions.push_back(InputActionUVE{MakeUniqueActionNameUVE(actions, "new_action"),
                                                 InputActionTypeUVE::Button, {}, {}});
                state.selected = actions.size() - 1U;
                state.renameFor = std::numeric_limits<std::size_t>::max();
                changed = true;
            }
        }
        ImGui::EndChild();

        // Right: the selected action.
        ImGui::SameLine();
        if (ImGui::BeginChild("##action", ImVec2{0.0F, -footerHeight}, ImGuiChildFlags_Borders)) {
            if (state.selected < actions.size()) {
                InputActionUVE& action = actions[state.selected];
                // The name is edited in a buffer and applied when the field is left: a half-typed
                // name that clashes with another action never reaches the map.
                if (state.renameFor != state.selected || state.renameSource != action.name) {
                    state.rename.fill('\0');
                    std::memcpy(state.rename.data(), action.name.data(),
                                std::min(action.name.size(), state.rename.size() - 1U));
                    state.renameFor = state.selected;
                    state.renameSource = action.name;
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Name");
                ImGui::SameLine(fontSize * 4.0F);
                ImGui::SetNextItemWidth(fontSize * 14.0F);
                ImGui::InputText("##name", state.rename.data(), state.rename.size());
                const std::string candidate{state.rename.data()};
                const bool taken = std::any_of(actions.begin(), actions.end(), [&](const InputActionUVE& other) {
                    return &other != &action && other.name == candidate;
                });
                if (ImGui::IsItemDeactivatedAfterEdit() && !candidate.empty() && !taken) {
                    action.name = candidate;
                    changed = true;
                }
                if (candidate.empty() || taken) {
                    ImGui::SameLine();
                    ImGui::TextColored(kErrorColorUVE, "%s", candidate.empty() ? "Needs a name" : "Name taken");
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Kind");
                ImGui::SameLine(fontSize * 4.0F);
                ImGui::SetNextItemWidth(fontSize * 14.0F);
                int kind = action.type == InputActionTypeUVE::Axis1D ? 1 : 0;
                if (ImGui::Combo("##kind", &kind, "Button\0Axis\0")) {
                    action.type = kind == 1 ? InputActionTypeUVE::Axis1D : InputActionTypeUVE::Button;
                    changed = true;
                }
                ImGui::SetItemTooltip("A button is pressed or not; an axis runs from -1 to 1, its negative "
                                      "bindings pushing one way and its positive ones the other.");
                ImGui::Spacing();
                if (action.type == InputActionTypeUVE::Axis1D) {
                    ImGui::SeparatorText("Positive");
                    DrawInputBindingListUVE(actions, state.selected, false, changed);
                    ImGui::SeparatorText("Negative");
                    DrawInputBindingListUVE(actions, state.selected, true, changed);
                } else {
                    ImGui::SeparatorText("Bindings");
                    DrawInputBindingListUVE(actions, state.selected, false, changed);
                }
            } else {
                ImGui::TextDisabled(actions.empty() ? "Add an action, then give it the keys and buttons that trigger it."
                                                    : "Select an action.");
            }
        }
        ImGui::EndChild();

        // Footer: where the map lives, and saving it.
        const std::string fileName = map.GetPathUVE().filename().string();
        ImGui::AlignTextToFramePadding();
        if (map.IsDirtyUVE() || changed) {
            ImGui::TextDisabled("Unsaved changes to %s. Play uses them already.", fileName.c_str());
        } else {
            ImGui::TextDisabled("Saved in %s, with the project.", fileName.c_str());
        }
        const float saveWidth = ImGui::CalcTextSize("Save").x + (ImGui::GetStyle().FramePadding.x * 2.0F);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - saveWidth));
        ImGui::BeginDisabled(!map.IsDirtyUVE() && !changed);
        if (ImGui::Button("Save")) {
            if (changed) {
                CommitInputMapUVE(actions);
                changed = false;
            }
            static_cast<void>(SaveInputMapUVE());
        }
        ImGui::EndDisabled();

        // Listening for the input to bind.
        if (state.listening && !ImGui::IsPopupOpen("Bind Input")) {
            ImGui::OpenPopup("Bind Input");
        }
        // No keyboard navigation inside: a key pressed here is a key to bind, never a button press.
        if (ImGui::BeginPopupModal("Bind Input", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav)) {
            ImGui::TextUnformatted("Press a key or a gamepad button, or push a stick.");
            // Mouse buttons bind only inside this box, so clicking Cancel binds nothing.
            const ImVec2 boxSize{fontSize * 20.0F, fontSize * 3.0F};
            ImGui::InvisibleButton("##mouse-box", boxSize, ImGuiButtonFlags_MouseButtonLeft |
                                                              ImGuiButtonFlags_MouseButtonRight |
                                                              ImGuiButtonFlags_MouseButtonMiddle);
            const bool overBox = ImGui::IsItemHovered();
            const ImVec2 boxMin = ImGui::GetItemRectMin();
            const ImVec2 boxMax = ImGui::GetItemRectMax();
            ImDrawList& drawList = *ImGui::GetWindowDrawList();
            drawList.AddRect(boxMin, boxMax, ImGui::GetColorU32(overBox ? ImGuiCol_ButtonHovered : ImGuiCol_Border));
            const char* boxText = "Click here for a mouse button";
            const ImVec2 textSize = ImGui::CalcTextSize(boxText);
            drawList.AddText(ImVec2{boxMin.x + ((boxSize.x - textSize.x) * 0.5F), boxMin.y + ((boxSize.y - textSize.y) * 0.5F)},
                             ImGui::GetColorU32(ImGuiCol_TextDisabled), boxText);
            const Input::IInputSystemUVE& input = m_services->GetInputSystemUVE();
            bool close = ImGui::Button("Cancel") || input.WasKeyPressedThisFrameUVE(Input::KeyCodeUVE::Escape);
            ImGui::SameLine();
            ImGui::TextDisabled("or Esc");
            if (!close && state.listenAction < actions.size()) {
                if (const std::optional<InputBindingUVE> captured =
                        CaptureInputBindingUVE(input, m_services->GetGamepadInputSystemUVE(), overBox)) {
                    InputActionUVE& target = actions[state.listenAction];
                    std::vector<InputBindingUVE>& bindings =
                        state.listenNegative ? target.negativeBindings : target.positiveBindings;
                    if (state.listenReplace && *state.listenReplace < bindings.size()) {
                        bindings[*state.listenReplace] = *captured;
                    } else {
                        bindings.push_back(*captured);
                    }
                    changed = true;
                    close = true;
                }
            }
            if (close || state.listenAction >= actions.size()) {
                state.listening = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    if (changed) {
        CommitInputMapUVE(std::move(actions));
    }
    // Closing the window keeps what was changed in it.
    if (!state.visible) {
        state.listening = false;
        static_cast<void>(SaveInputMapUVE());
    }
}

} // namespace UVE::Editor
