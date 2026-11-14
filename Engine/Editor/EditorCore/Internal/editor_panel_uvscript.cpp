// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The UVScript text editor: the Scripting workspace while a `.uvs` file is open. The text is
// checked against the object it belongs to on every edit, so a mistake shows up while typing - by
// line and column - rather than when the game runs. Saving writes the file; the running engine
// notices the new text and restarts that script on its own.

#include "editor.h"

#include "editor_uvscript_highlight.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "uvscript_object_host.h"
#include "i_entity_manager.h"
#include "uvscript_compiler.h"
#include "uvscript_parser.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool IsUVScriptPathUVE(const std::string_view path) noexcept { return path.ends_with(".uvs"); }

/// Every problem in the document's text. Checked against the object while it exists - its
/// components decide which properties are in scope - and only parsed once it is gone.
void RecheckUVScriptUVE(EditorUVE::UVScriptDocumentUVE& document, Scene::IEntityManagerUVE& entityManager) {
    if (entityManager.IsAliveUVE(document.entity)) {
        Core::UVScriptObjectHostUVE host(entityManager, nullptr, document.entity);
        document.diagnostics = UVScript::CompileUVScriptSourceUVE(document.text, host).diagnostics;
    } else {
        document.diagnostics = UVScript::ParseUVScriptUVE(document.text).diagnostics;
    }
}

}

bool EditorUVE::OpenUVScriptForEntityUVE(const Scene::EntityUVE entity) {
    if (m_state != EditorStateUVE::Running || m_services == nullptr) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return false;
    }
    const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
    if (!IsUVScriptPathUVE(path)) {
        return false;
    }
    // The same file already open keeps its unsaved text; only the object it is checked against moves.
    if (!m_openUVScript.has_value() || m_openUVScript->path != path) {
        UVScriptDocumentUVE document;
        document.path = path;
        document.text = ReadProjectTextFileUVE(path).value_or(std::string{});
        document.savedText = document.text;
        m_openUVScript = std::move(document);
    }
    m_openUVScript->entity = entity;
    RecheckUVScriptUVE(*m_openUVScript, entityManager);
    m_activeWorkspace = EditorWorkspaceUVE::Scripting;
    return true;
}

bool EditorUVE::OpenUVScriptPathUVE(const std::filesystem::path& path) {
    if (m_state != EditorStateUVE::Running || m_services == nullptr) {
        return false;
    }
    const std::string pathStr = path.generic_string();
    if (!pathStr.ends_with(".uvs")) {
        return false;
    }
    if (!m_openUVScript.has_value() || m_openUVScript->path != pathStr) {
        UVScriptDocumentUVE document;
        document.path = pathStr;
        document.text = ReadProjectTextFileUVE(path).value_or(std::string{});
        document.savedText = document.text;
        m_openUVScript = std::move(document);
    }
    m_openUVScript->entity = Scene::kInvalidEntityUVE;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    entityManager.ForEachUVE<Scene::ScriptComponentUVE>([&](const Scene::EntityUVE entity, const Scene::ScriptComponentUVE& script) {
        if (m_openUVScript->entity == Scene::kInvalidEntityUVE &&
            (script.scriptAssetPath == pathStr || pathStr.ends_with(script.scriptAssetPath))) {
            m_openUVScript->entity = entity;
        }
    });
    RecheckUVScriptUVE(*m_openUVScript, entityManager);
    m_activeWorkspace = EditorWorkspaceUVE::Scripting;
    return true;
}

const std::optional<EditorUVE::UVScriptDocumentUVE>& EditorUVE::GetOpenUVScriptUVE() const noexcept {
    return m_openUVScript;
}

void EditorUVE::SetOpenUVScriptTextUVE(std::string text) {
    if (!m_openUVScript.has_value() || m_services == nullptr) {
        return;
    }
    m_openUVScript->text = std::move(text);
    RecheckUVScriptUVE(*m_openUVScript, m_services->GetEntityManagerUVE());
}

bool EditorUVE::SaveOpenUVScriptUVE() {
    if (!m_openUVScript.has_value() || !WriteProjectTextFileUVE(m_openUVScript->path, m_openUVScript->text)) {
        return false;
    }
    m_openUVScript->savedText = m_openUVScript->text;
    return true;
}

void EditorUVE::CloseOpenUVScriptUVE() {
    m_openUVScript.reset();
    if (m_activeWorkspace == EditorWorkspaceUVE::Scripting) {
        m_activeWorkspace = EditorWorkspaceUVE::Library;
    }
}

} // namespace UVE::Editor
