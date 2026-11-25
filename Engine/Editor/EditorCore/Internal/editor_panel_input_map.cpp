// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Opening, changing and saving the project's input map (drawn by the PRISM shell's
// EditorInputMapWindow).

#include "editor.h"

#include <utility>
#include <vector>

namespace UVE::Editor {

using Input::InputAction;

void Editor::OpenInputMap() noexcept {
    m_inputMapWindow.visible = true;
    m_inputMapWindow.focusPending = true;
}

bool Editor::SaveInputMap() {
    Core::InputMapDocument& map = m_services->GetInputMap();
    return !map.IsDirty() || map.Save();
}

void Editor::CommitInputMap(std::vector<InputAction> actions) {
    Core::InputMapDocument& map = m_services->GetInputMap();
    map.SetActions(std::move(actions));
    map.Apply(m_services->GetInputSystem());
}

} // namespace UVE::Editor
