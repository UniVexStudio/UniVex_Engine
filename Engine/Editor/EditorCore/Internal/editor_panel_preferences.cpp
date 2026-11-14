// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Opening the Editor Preferences and Project Settings windows (drawn by the PRISM shell's
// EditorSettingsWindowUVE) and saving the project's settings.

#include "editor.h"

namespace UVE::Editor {

void EditorUVE::OpenEditorPreferencesUVE() noexcept {
    m_preferencesWindow.visible = true;
    m_preferencesWindow.focusSearch = true;
}

void EditorUVE::OpenProjectSettingsUVE() noexcept {
    m_projectSettingsWindow.visible = true;
    m_projectSettingsWindow.focusSearch = true;
}

bool EditorUVE::SaveProjectSettingsUVE() {
    Config::SettingsDocumentUVE& project = m_services->GetProjectSettingsUVE();
    return !project.IsDirtyUVE() || project.SaveUVE();
}

} // namespace UVE::Editor
