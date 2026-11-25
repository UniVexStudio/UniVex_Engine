// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "i_input_system.h"
#include "input_action.h"

namespace UVE::Core {

/// The project's input map: its named actions and their bindings, kept in project.uvinput beside
/// the project settings and committed with the project. EngineCore reads it at startup and
/// registers every action with the input system, so gameplay code asks for "jump", never for Space.
///
/// The file names keys and buttons ("Space", "Mouse Left", "South"), so it reads and diffs like
/// the map it is. A malformed action or binding in a hand-edited file is skipped on its own; the
/// rest of the map still loads.
///
/// Not thread-safe: loaded at startup, edited and applied from the main thread.
class InputMapDocument final {
public:
    /// Reads `path`, which later saves write back to. A missing file is an empty map and returns
    /// true; a file that does not parse leaves the map as it was and returns false.
    bool Load(const std::filesystem::path& path);
    /// Writes the map to the path it was loaded from. Clears IsDirty on success.
    bool Save();
    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_path; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }

    [[nodiscard]] const std::vector<Input::InputAction>& GetActions() const noexcept { return m_actions; }
    /// Replaces the map. Actions are kept in the given order; ones with an empty, overlong or
    /// repeated name are dropped, as are bindings the input system would refuse.
    void SetActions(std::vector<Input::InputAction> actions);

    /// Registers every action with `inputSystem`, replacing same-named ones, and unregisters the
    /// actions an earlier call registered that the map no longer has.
    void Apply(Input::IInputSystem& inputSystem);

    /// The actions as they would be after SetActions: what a file or an edit is cleaned to.
    [[nodiscard]] static std::vector<Input::InputAction> Sanitize(std::vector<Input::InputAction> actions);

private:
    std::vector<Input::InputAction> m_actions;
    std::vector<std::string> m_appliedNames;
    std::filesystem::path m_path;
    bool m_dirty = false;
};

} // namespace UVE::Core
