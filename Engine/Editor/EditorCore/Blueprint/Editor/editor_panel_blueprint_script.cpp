// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The UVScript text editor: the Scripting workspace while a `.uvs` file is open. The text is
// checked against the object it belongs to on every edit, so a mistake shows up while typing - by
// line and column - rather than when the game runs. Saving writes the file; the running engine
// notices the new text and restarts that script on its own.

#include "editor.h"


#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "blueprint_object_host.h"
#include "i_entity_manager.h"
#include "blueprint_compiler.h"
#include "blueprint_parser.h"
#include "script.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool IsUVScriptPath(const std::string_view path) noexcept { return Scene::Script::IsUVScriptPath(path); }

/// Every problem in the document's text. Checked against the object while it exists - its
/// components decide which properties are in scope - and only parsed once it is gone.
void RecheckUVScript(Editor::UVScriptDocument& document, Scene::IEntityManager& entityManager) {
    if (entityManager.IsAlive(document.entity)) {
        Core::BlueprintObjectHost host(entityManager, nullptr, document.entity);
        document.diagnostics = Blueprint::CompileUVScriptSource(document.text, host).diagnostics;
    } else {
        document.diagnostics = Blueprint::ParseUVScript(document.text).diagnostics;
    }
}

}

bool Editor::OpenUVScriptForEntity(const Scene::Entity entity) {
    if (m_state != EditorState::Running || m_services == nullptr) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
        return false;
    }
    const std::string& path = entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
    if (!IsUVScriptPath(path)) {
        return false;
    }
    // The same file already open keeps its unsaved text; only the object it is checked against moves.
    if (!m_openUVScript.has_value() || m_openUVScript->path != path) {
        UVScriptDocument document;
        document.path = path;
        document.text = ReadProjectTextFile(path).value_or(std::string{});
        document.savedText = document.text;
        m_openUVScript = std::move(document);
    }
    m_openUVScript->entity = entity;
    RecheckUVScript(*m_openUVScript, entityManager);
    m_activeWorkspace = EditorWorkspace::Scripting;
    return true;
}

bool Editor::OpenUVScriptPath(const std::filesystem::path& path) {
    if (m_state != EditorState::Running || m_services == nullptr) {
        return false;
    }
    const std::string pathStr = path.generic_string();
    if (!Scene::Script::IsUVScriptPath(pathStr)) {
        return false;
    }
    if (!m_openUVScript.has_value() || m_openUVScript->path != pathStr) {
        UVScriptDocument document;
        document.path = pathStr;
        document.text = ReadProjectTextFile(path).value_or(std::string{});
        document.savedText = document.text;
        m_openUVScript = std::move(document);
    }
    m_openUVScript->entity = Scene::kInvalidEntity;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    entityManager.ForEach<Scene::ScriptComponent>([&](const Scene::Entity entity, const Scene::ScriptComponent& script) {
        if (m_openUVScript->entity == Scene::kInvalidEntity &&
            (script.scriptAssetPath == pathStr || pathStr.ends_with(script.scriptAssetPath))) {
            m_openUVScript->entity = entity;
        }
    });
    RecheckUVScript(*m_openUVScript, entityManager);
    m_activeWorkspace = EditorWorkspace::Scripting;
    return true;
}

const std::optional<Editor::UVScriptDocument>& Editor::GetOpenUVScript() const noexcept {
    return m_openUVScript;
}

void Editor::SetOpenUVScriptText(std::string text) {
    if (!m_openUVScript.has_value() || m_services == nullptr) {
        return;
    }
    m_openUVScript->text = std::move(text);
    RecheckUVScript(*m_openUVScript, m_services->GetEntityManager());
}

bool Editor::SaveOpenUVScript() {
    if (!m_openUVScript.has_value() || !WriteProjectTextFile(m_openUVScript->path, m_openUVScript->text)) {
        return false;
    }
    m_openUVScript->savedText = m_openUVScript->text;
    return true;
}

void Editor::CloseOpenUVScript() {
    m_openUVScript.reset();
    if (m_activeWorkspace == EditorWorkspace::Scripting) {
        m_activeWorkspace = EditorWorkspace::Library;
    }
}

} // namespace UVE::Editor
