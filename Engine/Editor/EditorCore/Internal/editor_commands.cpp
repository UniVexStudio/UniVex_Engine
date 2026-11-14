// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The editor's commands - everything it can do by name - and the three ways to reach them:
// keyboard shortcuts, the menu bar, and the command palette (Ctrl+Shift+P). A command is declared
// once here; its shortcuts are the person's to change in the Keyboard Shortcuts window and are
// kept with the editor preferences.

#include "editor.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "editor_commands.h"

namespace UVE::Editor {
namespace {

constexpr std::string_view kShortcutSettingPrefixUVE = "editor.shortcuts.";
constexpr std::size_t kMaximumRecentCommandsUVE = 8U;

[[nodiscard]] EditorShortcutUVE KeyUVE(const EditorKeyUVE key, const bool ctrl = false, const bool shift = false,
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

    // Saves the active document: the level, or the open entity inside its window.
    add("file.saveScene", "Save Scene", "File", {KeyUVE(EditorKeyUVE::S, true)},
        [this] { return IsAuthoringCommandAllowedUVE() && (!m_activeScenePath.empty() || m_entityDocumentActive); },
        [this] { static_cast<void>(SaveSceneUVE()); }, true);
    add("file.saveAll", "Save All", "File", {KeyUVE(EditorKeyUVE::S, true, true)},
        [this] { return IsAuthoringCommandAllowedUVE(); }, [this] { static_cast<void>(SaveAllUVE()); }, true);
    add("file.loadScene", "Load Scene", "File", {}, always, [this] { static_cast<void>(LoadSceneUVE()); });
    add("file.projectSettings", "Project Settings", "File", {}, always, [this] { OpenProjectSettingsUVE(); });
    add("file.inputMap", "Input Map", "File", {}, always, [this] { OpenInputMapUVE(); });
    add("file.preferences", "Editor Preferences", "File", {KeyUVE(EditorKeyUVE::Comma, true)}, always,
        [this] { OpenEditorPreferencesUVE(); }, true);
    add("file.keyboardShortcuts", "Keyboard Shortcuts", "File", {}, always, [this] { OpenKeyboardShortcutsUVE(); });
    add("file.commandPalette", "Command Palette", "File", {KeyUVE(EditorKeyUVE::P, true, true)}, always,
        [this] { OpenCommandPaletteUVE(); }, true);

    add("entity.compile", "Compile", "Entity", {KeyUVE(EditorKeyUVE::F7)}, [this] { return m_entityDocumentActive; },
        [this] { static_cast<void>(CompileEntityEditorUVE()); });

    add("edit.undo", "Undo", "Edit", {KeyUVE(EditorKeyUVE::Z, true)}, [this] { return CanUndoUVE(); },
        [this] { static_cast<void>(UndoUVE()); });
    add("edit.redo", "Redo", "Edit", {KeyUVE(EditorKeyUVE::Y, true), KeyUVE(EditorKeyUVE::Z, true, true)},
        [this] { return CanRedoUVE(); }, [this] { static_cast<void>(RedoUVE()); });
    add("edit.duplicate", "Duplicate", "Edit", {KeyUVE(EditorKeyUVE::D, true)}, canChangeSelection,
        [this] { static_cast<void>(DuplicateSelectedEntityUVE()); });
    add("edit.delete", "Delete", "Edit", {KeyUVE(EditorKeyUVE::Delete)}, canChangeSelection,
        [this] { static_cast<void>(DeleteSelectedEntityUVE()); });
    add("edit.cut", "Cut", "Edit", {KeyUVE(EditorKeyUVE::X, true)}, canChangeSelection,
        [this] { static_cast<void>(CutSelectedObjectUVE()); });
    add("edit.copy", "Copy", "Edit", {KeyUVE(EditorKeyUVE::C, true)}, canChangeSelection,
        [this] { static_cast<void>(CopySelectedObjectUVE()); });
    add("edit.paste", "Paste", "Edit", {KeyUVE(EditorKeyUVE::V, true)}, [this] { return CanPasteObjectUVE(); },
        [this] { static_cast<void>(PasteObjectUVE(false)); });
    const auto addMove = [&](std::string id, std::string label, const EditorSiblingMoveUVE move,
                             const EditorShortcutUVE shortcut) {
        add(std::move(id), std::move(label), "Edit", {shortcut},
            [this, move] { return CanMoveDocumentEntityUVE(m_selectedEntity, move); },
            [this, move] { static_cast<void>(MoveDocumentEntityUVE(m_selectedEntity, move)); });
    };
    addMove("edit.moveUp", "Move Up", EditorSiblingMoveUVE::Up, KeyUVE(EditorKeyUVE::Up, true));
    addMove("edit.moveDown", "Move Down", EditorSiblingMoveUVE::Down, KeyUVE(EditorKeyUVE::Down, true));
    addMove("edit.moveToTop", "Move to Top", EditorSiblingMoveUVE::ToTop, EditorShortcutUVE{});
    addMove("edit.moveToBottom", "Move to Bottom", EditorSiblingMoveUVE::ToBottom, EditorShortcutUVE{});

    add("create.empty", "Create Empty", "Create", {}, canAuthor,
        [this] { static_cast<void>(CreateDocumentEntityUVE(EditorEntityKindUVE::Empty)); });
    add("create.cube", "Create Cube", "Create", {}, canAuthor,
        [this] { static_cast<void>(CreateDocumentEntityUVE(EditorEntityKindUVE::Cube)); });

    add("play.start", "Play", "Play", {KeyUVE(EditorKeyUVE::F5)},
        [this] { return m_simulationControl != nullptr && m_playModeState == EditorPlayModeStateUVE::Edit; },
        [this] { static_cast<void>(EnterPlayModeUVE()); });
    add("play.pauseResume", "Pause or Resume", "Play", {KeyUVE(EditorKeyUVE::F6)},
        [this] { return m_playModeState != EditorPlayModeStateUVE::Edit; },
        [this] {
            static_cast<void>(m_playModeState == EditorPlayModeStateUVE::Playing ? PausePlayModeUVE()
                                                                                 : ResumePlayModeUVE());
        });
    add("play.step", "Step One Frame", "Play", {KeyUVE(EditorKeyUVE::F10)},
        [this] { return m_playModeState == EditorPlayModeStateUVE::Paused; },
        [this] { static_cast<void>(StepPlayModeUVE()); });
    add("play.stop", "Stop", "Play", {KeyUVE(EditorKeyUVE::F5, false, true)},
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
            Config::SettingDescriptorUVE descriptor = Config::MakeKeyBindingSettingUVE(
                ShortcutSettingIdUVE(command.id, slot), FormatEditorShortcutUVE(command.defaultShortcuts[slot]),
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

void EditorUVE::OpenCommandPaletteUVE() noexcept {
    m_commandPalette.open = true;
    m_commandPalette.focus = true;
    m_commandPalette.query.fill('\0');
    m_commandPalette.highlighted = 0;
}

void EditorUVE::OpenKeyboardShortcutsUVE() noexcept {
    m_shortcutsWindow.visible = true;
}

bool EditorUVE::AssignCommandShortcutUVE(const std::string_view id, const std::size_t slot, const EditorShortcutUVE& shortcut) {
    return SetEditorSettingUVE(ShortcutSettingIdUVE(std::string(id), slot), Config::SettingValueUVE{FormatEditorShortcutUVE(shortcut)});
}

void EditorUVE::ResetCommandShortcutsUVE(const std::string_view id) {
    const EditorCommandUVE* const command = FindEditorCommandUVE(id);
    if (command == nullptr) {
        return;
    }
    const auto defaults = command->defaultShortcuts;
    for (std::size_t slot = 0U; slot < defaults.size(); ++slot) {
        static_cast<void>(AssignCommandShortcutUVE(id, slot, defaults[slot]));
    }
}

std::vector<std::string> EditorUVE::CommandsUsingShortcutUVE(const EditorShortcutUVE& shortcut, const std::string_view exceptId) const {
    std::vector<std::string> users;
    if (shortcut.IsEmptyUVE()) {
        return users;
    }
    for (const EditorCommandUVE& command : m_commands) {
        if (command.id != exceptId && std::find(command.shortcuts.begin(), command.shortcuts.end(), shortcut) != command.shortcuts.end()) {
            users.push_back(CommandTitleUVE(command));
        }
    }
    return users;
}

std::string EditorUVE::CommandTitleTextUVE(const EditorCommandUVE& command) {
    return CommandTitleUVE(command);
}

bool EditorUVE::IsCommandEnabledUVE(const EditorCommandUVE& command) {
    return IsEnabledUVE(command);
}

} // namespace UVE::Editor
