// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Opening the Editor Preferences and Project Settings windows (drawn by the PRISM shell's
// EditorSettingsWindow) and saving the project's settings.

#include "editor.h"
#include "engine_project_settings.h"

namespace UVE::Editor {

void Editor::OpenEditorPreferences() noexcept {
    m_preferencesWindow.visible = true;
    m_preferencesWindow.focusSearch = true;
    m_preferencesWindow.focusPending = true;
}

void Editor::OpenProjectSettings() noexcept {
    m_projectSettingsWindow.visible = true;
    m_projectSettingsWindow.focusSearch = true;
    m_projectSettingsWindow.focusPending = true;
}

bool Editor::SaveProjectSettings() {
    Config::SettingsDocument& project = m_services->GetProjectSettings();
    const bool saved = !project.IsDirty() || project.Save();
    if (saved) {
        Core::EngineConfig resolvedConfig{};
        Core::ApplyEngineProjectSettings(project, resolvedConfig);
        Core::ApplyEngineConfigToRenderer3D(resolvedConfig, m_services->GetRenderer3D());
    }
    return saved;
}

} // namespace UVE::Editor
