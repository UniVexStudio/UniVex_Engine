// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Opening, changing and saving the project's input map (drawn by the PRISM shell's
// EditorInputMapWindowUVE).

#include "editor.h"

#include <utility>
#include <vector>

namespace UVE::Editor {

using Input::InputActionUVE;

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

} // namespace UVE::Editor
