// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The editor's commands - everything it can do by name - and the three ways to reach them:
// keyboard shortcuts, the menu bar, and the command palette (Ctrl+Shift+P). A command is declared
// once here; its shortcuts are the person's to change in the Keyboard Shortcuts window and are
// kept with the editor preferences.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/editor/editor_commands_uve.h"

namespace UVE::Editor {
namespace {

constexpr ImVec4 kConflictColorUVE{0.93F, 0.70F, 0.30F, 1.0F};
constexpr std::string_view kShortcutSettingPrefixUVE = "editor.shortcuts.";
constexpr std::size_t kMaximumRecentCommandsUVE = 8U;

[[nodiscard]] EditorShortcutUVE KeyUVE(const ImGuiKey key, const bool ctrl = false, const bool shift = false,
                                       const bool alt = false) {
    return EditorShortcutUVE{static_cast<int>(key), ctrl, shift, alt};
}

[[nodiscard]] std::string ShortcutSettingIdUVE(const std::string& commandId, const std::size_t slot) {
    return std::string(kShortcutSettingPrefixUVE) + commandId + (slot == 0U ? ".primary" : ".alternate");
}

/// "Edit: Undo" - how a command reads outside its menu.
[[nodiscard]] std::string CommandTitleUVE(const EditorCommandUVE& command) {
    return command.category + ": " + command.label;
}

/// "Ctrl+Z", or "Ctrl+Z, Ctrl+Y" when both shortcuts are set.
[[nodiscard]] std::string ShortcutTextUVE(const EditorCommandUVE& command) {
    std::string text = DisplayEditorShortcutUVE(command.shortcuts[0]);
    const std::string alternate = DisplayEditorShortcutUVE(command.shortcuts[1]);
    if (!alternate.empty()) {
        text += text.empty() ? alternate : ", " + alternate;
    }
    return text;
}

[[nodiscard]] bool IsEnabledUVE(const EditorCommandUVE& command) {
    return !command.isEnabled || command.isEnabled();
}

} // namespace

void EditorUVE::RegisterEditorCommandsUVE() {
    const auto add = [this](std::string id, std::string label, std::string category,
                            std::array<EditorShortcutUVE, 2> shortcuts, std::function<bool()> isEnabled,
                            std::function<void()> run, const bool worksWhileTyping = false) {
        m_commands.push_back(EditorCommandUVE{std::move(id), std::move(label), std::move(category), shortcuts,
                                              shortcuts, std::move(isEnabled), std::move(run), worksWhileTyping});
    };
    const auto always = [] { return true; };
    const auto canAuthor = [this] { return IsAuthoringCommandAllowedUVE(); };
    const auto canChangeSelection = [this] {
        return IsLifecycleCommandAllowedUVE() && IsDocumentEntityUVE(m_selectedEntity);
    };

    add("file.saveScene", "Save Scene", "File", {KeyUVE(ImGuiKey_S, true)},
        [this] { return IsAuthoringCommandAllowedUVE() && !m_activeScenePath.empty(); },
        [this] { static_cast<void>(SaveSceneUVE()); }, true);
    add("file.loadScene", "Load Scene", "File", {}, always, [this] { static_cast<void>(LoadSceneUVE()); });
    add("file.projectSettings", "Project Settings", "File", {}, always, [this] { OpenProjectSettingsUVE(); });
    add("file.inputMap", "Input Map", "File", {}, always, [this] { OpenInputMapUVE(); });
    add("file.preferences", "Editor Preferences", "File", {KeyUVE(ImGuiKey_Comma, true)}, always,
        [this] { OpenEditorPreferencesUVE(); }, true);
    add("file.keyboardShortcuts", "Keyboard Shortcuts", "File", {}, always, [this] { OpenKeyboardShortcutsUVE(); });
    add("file.commandPalette", "Command Palette", "File", {KeyUVE(ImGuiKey_P, true, true)}, always,
        [this] { OpenCommandPaletteUVE(); }, true);

    add("edit.undo", "Undo", "Edit", {KeyUVE(ImGuiKey_Z, true)}, [this] { return CanUndoUVE(); },
        [this] { static_cast<void>(UndoUVE()); });
    add("edit.redo", "Redo", "Edit", {KeyUVE(ImGuiKey_Y, true), KeyUVE(ImGuiKey_Z, true, true)},
        [this] { return CanRedoUVE(); }, [this] { static_cast<void>(RedoUVE()); });
    add("edit.duplicate", "Duplicate", "Edit", {KeyUVE(ImGuiKey_D, true)}, canChangeSelection,
        [this] { static_cast<void>(DuplicateSelectedEntityUVE()); });
    add("edit.delete", "Delete", "Edit", {KeyUVE(ImGuiKey_Delete)}, canChangeSelection,
        [this] { static_cast<void>(DeleteSelectedEntityUVE()); });
    const auto addMove = [&](std::string id, std::string label, const EditorSiblingMoveUVE move,
                             const EditorShortcutUVE shortcut) {
        add(std::move(id), std::move(label), "Edit", {shortcut},
            [this, move] { return CanMoveDocumentEntityUVE(m_selectedEntity, move); },
            [this, move] { static_cast<void>(MoveDocumentEntityUVE(m_selectedEntity, move)); });
    };
    addMove("edit.moveUp", "Move Up", EditorSiblingMoveUVE::Up, KeyUVE(ImGuiKey_UpArrow, true));
    addMove("edit.moveDown", "Move Down", EditorSiblingMoveUVE::Down, KeyUVE(ImGuiKey_DownArrow, true));
    addMove("edit.moveToTop", "Move to Top", EditorSiblingMoveUVE::ToTop, EditorShortcutUVE{});
    addMove("edit.moveToBottom", "Move to Bottom", EditorSiblingMoveUVE::ToBottom, EditorShortcutUVE{});

    add("create.empty", "Create Empty", "Create", {}, canAuthor,
        [this] { static_cast<void>(CreateDocumentEntityUVE(EditorEntityKindUVE::Empty)); });
    add("create.cube", "Create Cube", "Create", {}, canAuthor,
        [this] { static_cast<void>(CreateDocumentEntityUVE(EditorEntityKindUVE::Cube)); });

    add("play.start", "Play", "Play", {KeyUVE(ImGuiKey_F5)},
        [this] { return m_simulationControl != nullptr && m_playModeState == EditorPlayModeStateUVE::Edit; },
        [this] { static_cast<void>(EnterPlayModeUVE()); });
    add("play.pauseResume", "Pause or Resume", "Play", {KeyUVE(ImGuiKey_F6)},
        [this] { return m_playModeState != EditorPlayModeStateUVE::Edit; },
        [this] {
            static_cast<void>(m_playModeState == EditorPlayModeStateUVE::Playing ? PausePlayModeUVE()
                                                                                 : ResumePlayModeUVE());
        });
    add("play.step", "Step One Frame", "Play", {KeyUVE(ImGuiKey_F10)},
        [this] { return m_playModeState == EditorPlayModeStateUVE::Paused; },
        [this] { static_cast<void>(StepPlayModeUVE()); });
    add("play.stop", "Stop", "Play", {KeyUVE(ImGuiKey_F5, false, true)},
        [this] { return m_playModeState != EditorPlayModeStateUVE::Edit; },
        [this] { static_cast<void>(StopPlayModeUVE()); });

    add("window.defaultLayout", "Default Layout", "Window", {}, always,
        [this] { ApplyLayoutPresetUVE(EditorLayoutPresetUVE::Default); });
    add("window.focusViewport", "Focus Viewport Layout", "Window", {}, always,
        [this] { ApplyLayoutPresetUVE(EditorLayoutPresetUVE::FocusViewport); });
    add("window.sceneWorkspace", "Scene Workspace", "Window", {}, always,
        [this] { m_activeWorkspace = EditorWorkspaceUVE::Library; });
    add("window.scriptingWorkspace", "Scripting Workspace", "Window", {}, always,
        [this] { m_activeWorkspace = EditorWorkspaceUVE::Scripting; });

    // Each shortcut is a hidden editor setting, so it is saved, restored and validated with the
    // rest of the preferences.
    for (const EditorCommandUVE& command : m_commands) {
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            Config::SettingDescriptorUVE descriptor = Config::MakeStringSettingUVE(
                ShortcutSettingIdUVE(command.id, slot), FormatEditorShortcutUVE(command.defaultShortcuts[slot]), 48U,
                command.label + (slot == 0U ? "" : " (Alternate)"), "Editor/Shortcuts");
            descriptor.flags |= Config::kSettingFlagHiddenUVE;
            if (!m_settingsRegistry.RegisterUVE(std::move(descriptor))) {
                throw std::logic_error("Failed to register the shortcut setting of " + command.id);
            }
        }
    }
}

const std::vector<EditorCommandUVE>& EditorUVE::GetEditorCommandsUVE() const noexcept {
    return m_commands;
}

EditorCommandUVE* EditorUVE::FindEditorCommandUVE(const std::string_view id) noexcept {
    const auto found = std::find_if(m_commands.begin(), m_commands.end(),
                                    [id](const EditorCommandUVE& command) { return command.id == id; });
    return found != m_commands.end() ? &*found : nullptr;
}

bool EditorUVE::RunEditorCommandUVE(const std::string_view id) {
    EditorCommandUVE* command = FindEditorCommandUVE(id);
    if (command == nullptr || !IsEnabledUVE(*command)) {
        return false;
    }
    command->run();
    // Remembered for the palette, most recent first, once each.
    std::erase(m_recentCommandIds, command->id);
    m_recentCommandIds.push_front(command->id);
    if (m_recentCommandIds.size() > kMaximumRecentCommandsUVE) {
        m_recentCommandIds.pop_back();
    }
    return true;
}

bool EditorUVE::SetEditorCommandShortcutUVE(const std::string_view id, const std::size_t slot,
                                            const EditorShortcutUVE& shortcut) {
    EditorCommandUVE* command = FindEditorCommandUVE(id);
    if (command == nullptr || slot >= command->shortcuts.size() ||
        (!shortcut.IsEmptyUVE() && !IsShortcutKeyUVE(shortcut.key))) {
        return false;
    }
    command->shortcuts[slot] = shortcut;
    return true;
}

std::optional<Config::SettingValueUVE> EditorUVE::GetShortcutSettingUVE(const std::string_view id) const {
    if (!id.starts_with(kShortcutSettingPrefixUVE)) {
        return std::nullopt;
    }
    for (const EditorCommandUVE& command : m_commands) {
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            if (ShortcutSettingIdUVE(command.id, slot) == id) {
                return Config::SettingValueUVE{FormatEditorShortcutUVE(command.shortcuts[slot])};
            }
        }
    }
    return std::nullopt;
}

bool EditorUVE::SetShortcutSettingUVE(const std::string_view id, const Config::SettingValueUVE& value) {
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || !id.starts_with(kShortcutSettingPrefixUVE)) {
        return false;
    }
    const std::optional<EditorShortcutUVE> shortcut = ParseEditorShortcutUVE(*text);
    if (!shortcut) {
        return false;
    }
    for (const EditorCommandUVE& command : m_commands) {
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            if (ShortcutSettingIdUVE(command.id, slot) == id) {
                return SetEditorCommandShortcutUVE(command.id, slot, *shortcut);
            }
        }
    }
    return false;
}

void EditorUVE::DispatchEditorShortcutsUVE() {
    const ImGuiIO& io = ImGui::GetIO();
    // A window listening for a new shortcut gets the keys first; and typing into a field is
    // typing, never a command, unless the command says it works while typing.
    if (m_shortcutsWindow.listening) {
        return;
    }
    for (EditorCommandUVE& command : m_commands) {
        if (io.WantTextInput && !command.worksWhileTyping) {
            continue;
        }
        for (const EditorShortcutUVE& shortcut : command.shortcuts) {
            // Modifiers must match exactly, so F5 and Shift+F5 are two different commands.
            if (!shortcut.IsEmptyUVE() && io.KeyCtrl == shortcut.ctrl && io.KeyShift == shortcut.shift &&
                io.KeyAlt == shortcut.alt && ImGui::IsKeyPressed(static_cast<ImGuiKey>(shortcut.key), false) &&
                IsEnabledUVE(command)) {
                static_cast<void>(RunEditorCommandUVE(command.id));
                return;
            }
        }
    }
}

void EditorUVE::DrawCommandMenuItemUVE(const std::string_view id) {
    EditorCommandUVE* command = FindEditorCommandUVE(id);
    if (command == nullptr) {
        return;
    }
    const std::string shortcut = DisplayEditorShortcutUVE(command->shortcuts[0]);
    if (ImGui::MenuItem(command->label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false,
                        IsEnabledUVE(*command))) {
        static_cast<void>(RunEditorCommandUVE(id));
    }
}

void EditorUVE::OpenCommandPaletteUVE() noexcept {
    m_commandPalette.open = true;
    m_commandPalette.focus = true;
    m_commandPalette.query.fill('\0');
    m_commandPalette.highlighted = 0;
}

void EditorUVE::OpenKeyboardShortcutsUVE() noexcept {
    m_shortcutsWindow.visible = true;
}

void EditorUVE::DrawCommandPaletteUVE() {
    if (!m_commandPalette.open) {
        return;
    }
    CommandPaletteStateUVE& state = m_commandPalette;
    const float fontSize = ImGui::GetFontSize();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(fontSize * 36.0F, viewport->WorkSize.x - (fontSize * 2.0F));
    ImGui::SetNextWindowPos(ImVec2{viewport->WorkPos.x + ((viewport->WorkSize.x - width) * 0.5F),
                                   viewport->WorkPos.y + (fontSize * 4.0F)});
    ImGui::SetNextWindowSize(ImVec2{width, 0.0F});
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("##command-palette", nullptr, kFlags)) {
        ImGui::End();
        return;
    }
    if (state.focus) {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetWindowFocus();
        state.focus = false;
    }
    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
    if (ImGui::InputTextWithHint("##query", "Type a command", state.query.data(), state.query.size())) {
        state.highlighted = 0;
    }
    const std::string_view query{state.query.data()};

    // Matches, best first; with no query, recent commands first, then everything in order.
    struct MatchUVE final {
        std::size_t index;
        int score;
    };
    std::vector<MatchUVE> matches;
    for (std::size_t index = 0U; index < m_commands.size(); ++index) {
        const EditorCommandUVE& command = m_commands[index];
        int score = std::max(ScoreFuzzyMatchUVE(command.label, query), ScoreFuzzyMatchUVE(CommandTitleUVE(command), query));
        if (score < 0) {
            continue;
        }
        const auto recent = std::find(m_recentCommandIds.begin(), m_recentCommandIds.end(), command.id);
        if (recent != m_recentCommandIds.end()) {
            score += 50 - static_cast<int>(std::distance(m_recentCommandIds.begin(), recent));
        }
        score += IsEnabledUVE(command) ? 25 : 0;
        matches.push_back(MatchUVE{index, score});
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const MatchUVE& a, const MatchUVE& b) { return a.score > b.score; });
    constexpr std::size_t kShownUVE = 12U;
    if (matches.size() > kShownUVE) {
        matches.resize(kShownUVE);
    }
    const int count = static_cast<int>(matches.size());
    if (count > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            state.highlighted = (state.highlighted + 1) % count;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            state.highlighted = (state.highlighted + count - 1) % count;
        }
        state.highlighted = std::clamp(state.highlighted, 0, count - 1);
    }

    std::optional<std::size_t> chosen;
    for (int row = 0; row < count; ++row) {
        const EditorCommandUVE& command = m_commands[matches[static_cast<std::size_t>(row)].index];
        const bool enabled = IsEnabledUVE(command);
        ImGui::PushID(row);
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable("##command", row == state.highlighted, ImGuiSelectableFlags_None,
                              ImVec2{0.0F, ImGui::GetTextLineHeight()})) {
            chosen = matches[static_cast<std::size_t>(row)].index;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            state.highlighted = row;
        }
        // The category dim before the label, the shortcut dim at the far right.
        ImGui::SameLine(ImGui::GetStyle().ItemSpacing.x);
        ImGui::TextDisabled("%s:", command.category.c_str());
        ImGui::SameLine();
        if (enabled) {
            ImGui::TextUnformatted(command.label.c_str());
        } else {
            ImGui::TextDisabled("%s", command.label.c_str());
        }
        const std::string shortcut = ShortcutTextUVE(command);
        if (!shortcut.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(shortcut.c_str()).x);
            ImGui::TextDisabled("%s", shortcut.c_str());
        }
        ImGui::PopID();
    }
    if (count == 0) {
        ImGui::TextDisabled("No command matches.");
    }
    if (count > 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        chosen = matches[static_cast<std::size_t>(state.highlighted)].index;
    }
    // Escape, or a click anywhere else, closes it without running anything.
    const bool dismissed = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                           (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    ImGui::End();
    if (chosen) {
        state.open = false;
        static_cast<void>(RunEditorCommandUVE(m_commands[*chosen].id));
    } else if (dismissed) {
        state.open = false;
    }
}

void EditorUVE::DrawKeyboardShortcutsWindowUVE() {
    if (!m_shortcutsWindow.visible) {
        m_shortcutsWindow.listening = false;
        return;
    }
    ShortcutsWindowStateUVE& state = m_shortcutsWindow;
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2{fontSize * 40.0F, fontSize * 28.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2{0.5F, 0.5F});
    if (!ImGui::Begin("Keyboard Shortcuts", &state.visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // While listening, the next key pressed with its modifiers becomes the shortcut; Escape cancels.
    if (state.listening) {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            state.listening = false;
        } else {
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                if (IsShortcutKeyUVE(key) && ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
                    static_cast<void>(SetEditorCommandShortcutUVE(m_commands[state.listenCommand].id,
                                                                 state.listenSlot,
                                                                 EditorShortcutUVE{key, io.KeyCtrl, io.KeyShift, io.KeyAlt}));
                    state.listening = false;
                    break;
                }
            }
        }
    }

    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
    ImGui::InputTextWithHint("##filter", "Search commands or shortcuts", state.filter.data(), state.filter.size(),
                             ImGuiInputTextFlags_EscapeClearsAll);
    const std::string_view filter{state.filter.data()};

    // Every shortcut in use, to mark the ones two commands share.
    const auto usersOf = [this](const EditorShortcutUVE& shortcut, const std::size_t except) {
        std::vector<std::string> users;
        for (std::size_t index = 0U; index < m_commands.size(); ++index) {
            if (index != except && std::find(m_commands[index].shortcuts.begin(), m_commands[index].shortcuts.end(),
                                             shortcut) != m_commands[index].shortcuts.end()) {
                users.push_back(m_commands[index].label);
            }
        }
        return users;
    };

    if (ImGui::BeginChild("##shortcuts", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_None) &&
        ImGui::BeginTable("##table", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 0.40F);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 0.28F);
        ImGui::TableSetupColumn("Alternate", ImGuiTableColumnFlags_WidthStretch, 0.28F);
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Reset").x + 12.0F);
        ImGui::TableHeadersRow();
        for (std::size_t index = 0U; index < m_commands.size(); ++index) {
            EditorCommandUVE& command = m_commands[index];
            if (!filter.empty() && ScoreFuzzyMatchUVE(CommandTitleUVE(command), filter) < 0 &&
                ScoreFuzzyMatchUVE(ShortcutTextUVE(command), filter) < 0) {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s:", command.category.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(command.label.c_str());
            for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
                ImGui::TableSetColumnIndex(static_cast<int>(slot) + 1);
                ImGui::PushID(static_cast<int>(slot));
                const bool listeningHere = state.listening && state.listenCommand == index && state.listenSlot == slot;
                const std::string text = DisplayEditorShortcutUVE(command.shortcuts[slot]);
                const std::string label = listeningHere ? "Press keys..." : (text.empty() ? "-" : text);
                const std::vector<std::string> users =
                    command.shortcuts[slot].IsEmptyUVE() ? std::vector<std::string>{} : usersOf(command.shortcuts[slot], index);
                if (!users.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kConflictColorUVE);
                }
                if (ImGui::Button(label.c_str(), ImVec2{-ImGui::GetFrameHeight() - 4.0F, 0.0F})) {
                    state.listening = true;
                    state.listenCommand = index;
                    state.listenSlot = slot;
                }
                if (!users.empty()) {
                    ImGui::PopStyleColor();
                    if (ImGui::BeginItemTooltip()) {
                        ImGui::TextUnformatted("Also the shortcut for:");
                        for (const std::string& user : users) {
                            ImGui::BulletText("%s", user.c_str());
                        }
                        ImGui::EndTooltip();
                    }
                } else {
                    ImGui::SetItemTooltip(listeningHere ? "Press the new shortcut, or Escape to keep this one."
                                                        : "Click, then press the new shortcut.");
                }
                ImGui::SameLine(0.0F, 4.0F);
                ImGui::BeginDisabled(command.shortcuts[slot].IsEmptyUVE());
                if (ImGui::Button("x", ImVec2{ImGui::GetFrameHeight(), 0.0F})) {
                    command.shortcuts[slot] = EditorShortcutUVE{};
                }
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("Remove this shortcut");
                ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(3);
            if (command.shortcuts != command.defaultShortcuts && ImGui::SmallButton("Reset")) {
                command.shortcuts = command.defaultShortcuts;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace UVE::Editor
