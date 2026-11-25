// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The editor's commands - everything it can do by name - and the three ways to reach them:
// keyboard shortcuts, the menu bar, and the command palette (Ctrl+Shift+P). A command is declared
// once here; its shortcuts are the person's to change in the Keyboard Shortcuts window and are
// kept with the editor preferences.

#include "editor.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "editor_commands.h"

#include "prism/Application.h"
#include "prism/Controls.h"
#include "prism/Views.h"

namespace UVE::Editor {
namespace {

constexpr std::string_view kShortcutSettingPrefix = "editor.shortcuts.";
constexpr std::size_t kMaximumRecentCommands = 8U;

[[nodiscard]] EditorShortcut Key(const EditorKey key, const bool ctrl = false, const bool shift = false,
                                       const bool alt = false) {
    return EditorShortcut{static_cast<int>(key), ctrl, shift, alt};
}

[[nodiscard]] std::string ShortcutSettingId(const std::string& commandId, const std::size_t slot) {
    return std::string(kShortcutSettingPrefix) + commandId + (slot == 0U ? ".primary" : ".alternate");
}

/// "Edit: Undo" - how a command reads outside its menu.
[[nodiscard]] std::string CommandTitle(const EditorCommand& command) {
    return command.category + ": " + command.label;
}

[[nodiscard]] bool IsEnabled(const EditorCommand& command) {
    return !command.isEnabled || command.isEnabled();
}

} // namespace

void Editor::RegisterEditorCommands() {
    const auto add = [this](std::string id, std::string label, std::string category,
                            std::array<EditorShortcut, 2> shortcuts, std::function<bool()> isEnabled,
                            std::function<void()> run, const bool worksWhileTyping = false) {
        m_commands.push_back(EditorCommand{std::move(id), std::move(label), std::move(category), shortcuts,
                                              shortcuts, std::move(isEnabled), std::move(run), worksWhileTyping});
    };
    const auto always = [] { return true; };
    const auto canAuthor = [this] { return IsAuthoringCommandAllowed(); };
    const auto canChangeSelection = [this] {
        return IsLifecycleCommandAllowed() && IsDocumentEntity(m_selectedEntity);
    };

    // Saves the active document: the level, or the open entity inside its window.
    add("file.saveScene", "Save Scene", "File", {Key(EditorKey::S, true)},
        [this] { return IsAuthoringCommandAllowed() && (!m_activeScenePath.empty() || m_entityDocumentActive); },
        [this] { static_cast<void>(SaveScene()); }, true);
    add("file.saveAll", "Save All", "File", {Key(EditorKey::S, true, true)},
        [this] { return IsAuthoringCommandAllowed(); }, [this] { static_cast<void>(SaveAll()); }, true);
    add("file.loadScene", "Load Scene", "File", {}, always, [this] { static_cast<void>(LoadScene()); });
    add("file.projectSettings", "Project Settings", "File", {}, always, [this] { OpenProjectSettings(); });
    add("file.inputMap", "Input Map", "File", {}, always, [this] { OpenInputMap(); });
    add("file.preferences", "Editor Preferences", "File", {Key(EditorKey::Comma, true)}, always,
        [this] { OpenEditorPreferences(); }, true);
    add("file.keyboardShortcuts", "Keyboard Shortcuts", "File", {}, always, [this] { OpenKeyboardShortcuts(); });
    add("file.commandPalette", "Command Palette", "File", {Key(EditorKey::P, true, true)}, always,
        [this] { OpenCommandPalette(); }, true);

    add("entity.compile", "Compile", "Entity", {Key(EditorKey::F7)}, [this] { return m_entityDocumentActive; },
        [this] { static_cast<void>(CompileEntityEditor()); });

    add("edit.undo", "Undo", "Edit", {Key(EditorKey::Z, true)}, [this] { return CanUndo(); },
        [this] { static_cast<void>(Undo()); });
    add("edit.redo", "Redo", "Edit", {Key(EditorKey::Y, true), Key(EditorKey::Z, true, true)},
        [this] { return CanRedo(); }, [this] { static_cast<void>(Redo()); });
    add("edit.duplicate", "Duplicate", "Edit", {Key(EditorKey::D, true)}, canChangeSelection,
        [this] { static_cast<void>(DuplicateSelectedEntity()); });
    add("edit.delete", "Delete", "Edit", {Key(EditorKey::Delete)},
        [this] { return !CollectDeletableSelection().empty(); }, [this] { RequestDeleteSelectedEntities(); });
    add("edit.cut", "Cut", "Edit", {Key(EditorKey::X, true)}, canChangeSelection,
        [this] { static_cast<void>(CutSelectedObject()); });
    add("edit.copy", "Copy", "Edit", {Key(EditorKey::C, true)}, canChangeSelection,
        [this] { static_cast<void>(CopySelectedObject()); });
    add("edit.paste", "Paste", "Edit", {Key(EditorKey::V, true)}, [this] { return CanPasteObject(); },
        [this] { static_cast<void>(PasteObject(false)); });
    const auto addMove = [&](std::string id, std::string label, const EditorSiblingMove move,
                             const EditorShortcut shortcut) {
        add(std::move(id), std::move(label), "Edit", {shortcut},
            [this, move] { return CanMoveDocumentEntity(m_selectedEntity, move); },
            [this, move] { static_cast<void>(MoveDocumentEntity(m_selectedEntity, move)); });
    };
    addMove("edit.moveUp", "Move Up", EditorSiblingMove::Up, Key(EditorKey::Up, true));
    addMove("edit.moveDown", "Move Down", EditorSiblingMove::Down, Key(EditorKey::Down, true));
    addMove("edit.moveToTop", "Move to Top", EditorSiblingMove::ToTop, EditorShortcut{});
    addMove("edit.moveToBottom", "Move to Bottom", EditorSiblingMove::ToBottom, EditorShortcut{});

    add("create.empty", "Create Empty", "Create", {}, canAuthor,
        [this] { static_cast<void>(CreateDocumentEntity(EditorEntityKind::Empty)); });
    add("create.cube", "Create Cube", "Create", {}, canAuthor,
        [this] { static_cast<void>(CreateDocumentEntity(EditorEntityKind::Cube)); });

    add("play.start", "Play", "Play", {Key(EditorKey::F5)},
        [this] { return m_simulationControl != nullptr && m_playModeState == EditorPlayModeState::Edit; },
        [this] { static_cast<void>(EnterPlayMode()); });
    add("play.pauseResume", "Pause or Resume", "Play", {Key(EditorKey::F6)},
        [this] { return m_playModeState != EditorPlayModeState::Edit; },
        [this] {
            static_cast<void>(m_playModeState == EditorPlayModeState::Playing ? PausePlayMode()
                                                                                 : ResumePlayMode());
        });
    add("play.step", "Step One Frame", "Play", {Key(EditorKey::F10)},
        [this] { return m_playModeState == EditorPlayModeState::Paused; },
        [this] { static_cast<void>(StepPlayMode()); });
    add("play.stop", "Stop", "Play", {Key(EditorKey::F5, false, true)},
        [this] { return m_playModeState != EditorPlayModeState::Edit; },
        [this] { static_cast<void>(StopPlayMode()); });

    add("window.defaultLayout", "Default Layout", "Window", {}, always,
        [this] { ApplyLayoutPreset(EditorLayoutPreset::Default); });
    add("window.focusViewport", "Focus Viewport Layout", "Window", {}, always,
        [this] { ApplyLayoutPreset(EditorLayoutPreset::FocusViewport); });
    add("window.sceneWorkspace", "Scene Workspace", "Window", {}, always,
        [this] { m_activeWorkspace = EditorWorkspace::Library; });
    add("window.scriptingWorkspace", "Scripting Workspace", "Window", {}, always,
        [this] { m_activeWorkspace = EditorWorkspace::Scripting; });

    // Each shortcut is a hidden editor setting, so it is saved, restored and validated with the
    // rest of the preferences.
    for (const EditorCommand& command : m_commands) {
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            Config::SettingDescriptor descriptor = Config::MakeKeyBindingSetting(
                ShortcutSettingId(command.id, slot), FormatEditorShortcut(command.defaultShortcuts[slot]),
                command.label + (slot == 0U ? "" : " (Alternate)"), "Editor/Shortcuts");
            descriptor.flags |= Config::kSettingFlagHidden;
            if (!m_settingsRegistry.Register(std::move(descriptor))) {
                throw std::logic_error("Failed to register the shortcut setting of " + command.id);
            }
        }
    }
}

void Editor::RequestDeleteSelectedEntities() {
    const std::size_t count = CollectDeletableSelection().size();
    if (count <= 1U || !prism::Application::exists()) {
        static_cast<void>(DeleteSelectedEntities());
        return;
    }
    const bool entityDocument = m_entityDocumentActive;
    const std::vector<Scene::Entity> asked = m_selectedEntities;
    const auto erase = [this, entityDocument, asked] {
        std::optional<EntityDocumentScope> scope;
        if (entityDocument) {
            scope.emplace(*this);
            if (!m_entityDocumentActive) {
                return;   // the entity was closed meanwhile
            }
        }
        // Only what the question named: a selection changed since then is left alone.
        if (m_selectedEntities == asked) {
            static_cast<void>(DeleteSelectedEntities());
        }
    };
    auto text = prism::make<prism::Label>("Delete " + std::to_string(count) + " objects?");
    prism::showDialog("Delete Objects", text,
                      {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"Delete", true, erase}}, 380.0F);
}

const std::vector<EditorCommand>& Editor::GetEditorCommands() const noexcept {
    return m_commands;
}

EditorCommand* Editor::FindEditorCommand(const std::string_view id) noexcept {
    const auto found = std::find_if(m_commands.begin(), m_commands.end(),
                                    [id](const EditorCommand& command) { return command.id == id; });
    return found != m_commands.end() ? &*found : nullptr;
}

bool Editor::RunEditorCommand(const std::string_view id) {
    EditorCommand* command = FindEditorCommand(id);
    if (command == nullptr || !IsEnabled(*command)) {
        return false;
    }
    command->run();
    // Remembered for the palette, most recent first, once each.
    std::erase(m_recentCommandIds, command->id);
    m_recentCommandIds.push_front(command->id);
    if (m_recentCommandIds.size() > kMaximumRecentCommands) {
        m_recentCommandIds.pop_back();
    }
    return true;
}

bool Editor::SetEditorCommandShortcut(const std::string_view id, const std::size_t slot,
                                            const EditorShortcut& shortcut) {
    EditorCommand* command = FindEditorCommand(id);
    if (command == nullptr || slot >= command->shortcuts.size() ||
        (!shortcut.IsEmpty() && !IsShortcutKey(shortcut.key))) {
        return false;
    }
    command->shortcuts[slot] = shortcut;
    return true;
}

std::optional<Config::SettingValue> Editor::GetShortcutSetting(const std::string_view id) const {
    if (!id.starts_with(kShortcutSettingPrefix)) {
        return std::nullopt;
    }
    for (const EditorCommand& command : m_commands) {
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            if (ShortcutSettingId(command.id, slot) == id) {
                return Config::SettingValue{FormatEditorShortcut(command.shortcuts[slot])};
            }
        }
    }
    return std::nullopt;
}

bool Editor::SetShortcutSetting(const std::string_view id, const Config::SettingValue& value) {
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || !id.starts_with(kShortcutSettingPrefix)) {
        return false;
    }
    const std::optional<EditorShortcut> shortcut = ParseEditorShortcut(*text);
    if (!shortcut) {
        return false;
    }
    for (const EditorCommand& command : m_commands) {
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            if (ShortcutSettingId(command.id, slot) == id) {
                return SetEditorCommandShortcut(command.id, slot, *shortcut);
            }
        }
    }
    return false;
}

void Editor::OpenCommandPalette() noexcept {
    m_commandPalette.open = true;
    m_commandPalette.focus = true;
    m_commandPalette.query.fill('\0');
    m_commandPalette.highlighted = 0;
}

void Editor::OpenKeyboardShortcuts() noexcept {
    m_shortcutsWindow.visible = true;
    m_shortcutsWindow.focusPending = true;
}

bool Editor::AssignCommandShortcut(const std::string_view id, const std::size_t slot, const EditorShortcut& shortcut) {
    return SetEditorSetting(ShortcutSettingId(std::string(id), slot), Config::SettingValue{FormatEditorShortcut(shortcut)});
}

void Editor::ResetCommandShortcuts(const std::string_view id) {
    const EditorCommand* const command = FindEditorCommand(id);
    if (command == nullptr) {
        return;
    }
    const auto defaults = command->defaultShortcuts;
    for (std::size_t slot = 0U; slot < defaults.size(); ++slot) {
        static_cast<void>(AssignCommandShortcut(id, slot, defaults[slot]));
    }
}

std::vector<std::string> Editor::CommandsUsingShortcut(const EditorShortcut& shortcut, const std::string_view exceptId) const {
    std::vector<std::string> users;
    if (shortcut.IsEmpty()) {
        return users;
    }
    for (const EditorCommand& command : m_commands) {
        if (command.id != exceptId && std::find(command.shortcuts.begin(), command.shortcuts.end(), shortcut) != command.shortcuts.end()) {
            users.push_back(CommandTitle(command));
        }
    }
    return users;
}

std::string Editor::CommandTitleText(const EditorCommand& command) {
    return CommandTitle(command);
}

bool Editor::IsCommandEnabled(const EditorCommand& command) {
    return IsEnabled(command);
}

} // namespace UVE::Editor
