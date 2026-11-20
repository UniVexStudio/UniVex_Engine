// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Opening the Editor Preferences and Project Settings windows (drawn by the PRISM shell's
// EditorSettingsWindowUVE) and saving the project's settings.

#include "editor.h"
#include "engine_project_settings.h"

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
    const bool saved = !project.IsDirtyUVE() || project.SaveUVE();
    if (saved) {
        Core::EngineConfigUVE resolvedConfig{};
        Core::ApplyEngineProjectSettingsUVE(project, resolvedConfig);
        Core::ApplyEngineConfigToRenderer3DUVE(resolvedConfig, m_services->GetRenderer3DUVE());
    }
    return saved;
}

} // namespace UVE::Editor
