// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "uve/input/i_input_system_uve.h"
#include "uve/input/input_action_uve.h"

namespace UVE::Core {

/// The project's input map: its named actions and their bindings, kept in project.uveinput beside
/// the project settings and committed with the project. EngineCoreUVE reads it at startup and
/// registers every action with the input system, so gameplay code asks for "jump", never for Space.
///
/// The file names keys and buttons ("Space", "Mouse Left", "South"), so it reads and diffs like
/// the map it is. A malformed action or binding in a hand-edited file is skipped on its own; the
/// rest of the map still loads.
///
/// Not thread-safe: loaded at startup, edited and applied from the main thread.
class InputMapDocumentUVE final {
public:
    /// Reads `path`, which later saves write back to. A missing file is an empty map and returns
    /// true; a file that does not parse leaves the map as it was and returns false.
    bool LoadUVE(const std::filesystem::path& path);
    /// Writes the map to the path it was loaded from. Clears IsDirtyUVE on success.
    bool SaveUVE();
    [[nodiscard]] const std::filesystem::path& GetPathUVE() const noexcept { return m_path; }
    [[nodiscard]] bool IsDirtyUVE() const noexcept { return m_dirty; }

    [[nodiscard]] const std::vector<Input::InputActionUVE>& GetActionsUVE() const noexcept { return m_actions; }
    /// Replaces the map. Actions are kept in the given order; ones with an empty, overlong or
    /// repeated name are dropped, as are bindings the input system would refuse.
    void SetActionsUVE(std::vector<Input::InputActionUVE> actions);

    /// Registers every action with `inputSystem`, replacing same-named ones, and unregisters the
    /// actions an earlier call registered that the map no longer has.
    void ApplyUVE(Input::IInputSystemUVE& inputSystem);

    /// The actions as they would be after SetActionsUVE: what a file or an edit is cleaned to.
    [[nodiscard]] static std::vector<Input::InputActionUVE> SanitizeUVE(std::vector<Input::InputActionUVE> actions);

private:
    std::vector<Input::InputActionUVE> m_actions;
    std::vector<std::string> m_appliedNames;
    std::filesystem::path m_path;
    bool m_dirty = false;
};

} // namespace UVE::Core
