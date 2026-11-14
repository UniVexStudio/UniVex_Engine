// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Scripting slot: the Inspector row that says which script an object runs, and the commands
// behind its actions - New UVScript, Open, Quick Load, Load and Clear.
//
// An object's script is one `.uvs` file, the asset its Script component names. New UVScript writes
// that file with a header naming the object and its kind and opens it in the text editor; Quick
// Load and Load point the object at a script that already exists.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <typeindex>
#include <utility>
#include <vector>

#include "script_component.h"
#include "i_entity_manager.h"
#include "scene_object_type.h"

namespace UVE::Editor {
namespace {

constexpr std::string_view kScriptFolderUVE = "scripts";
constexpr std::string_view kUVScriptExtensionUVE = ".uvs";

/// A file stem from an object name: letters, digits, '-' and '_' kept, anything else an underscore,
/// runs of underscores collapsed. "Main Menu (old)" becomes "Main_Menu_old".
[[nodiscard]] std::string ScriptFileStemUVE(const std::string& name) {
    std::string stem;
    for (const char character : name) {
        const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (keep) {
            stem.push_back(character);
        } else if (!stem.empty() && stem.back() != '_') {
            stem.push_back('_');
        }
    }
    while (!stem.empty() && stem.back() == '_') {
        stem.pop_back();
    }
    constexpr std::size_t kMaximumStemBytesUVE = 64U;
    if (stem.size() > kMaximumStemBytesUVE) {
        stem.resize(kMaximumStemBytesUVE);
    }
    return stem.empty() ? std::string{"script"} : stem;
}

/// The Script component's declared path property, through which every slot edit goes so it is one
/// undo entry exactly like any other property edit.
struct ScriptPathPropertyUVE final {
    const Core::TypeMetadataEntryUVE* entry = nullptr;
    const Core::TypeMetadataPropertyUVE* property = nullptr;
};

[[nodiscard]] ScriptPathPropertyUVE FindScriptPathPropertyUVE() {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::ScriptComponentUVE)));
    if (entry == nullptr) {
        return {};
    }
    const auto property = std::find_if(entry->properties.cbegin(), entry->properties.cend(),
                                       [](const Core::TypeMetadataPropertyUVE& candidate) {
                                           return candidate.name == "scriptAssetPath";
                                       });
    return property == entry->properties.cend() ? ScriptPathPropertyUVE{} : ScriptPathPropertyUVE{entry, &*property};
}

[[nodiscard]] bool IsUVScriptPathUVE(const std::string_view path) noexcept { return path.ends_with(kUVScriptExtensionUVE); }

/// A free script path for `stem`: not a file that exists, and not a path another object already
/// names (it may not have been saved yet). Bounded, so a folder full of collisions gives up
/// (empty) instead of spinning.
template <typename ExistsFn>
[[nodiscard]] std::string FindFreeScriptPathUVE(const std::string& stem, const std::vector<std::string>& taken,
                                                const std::string_view extension, const ExistsFn& exists) {
    constexpr int kMaximumAttemptsUVE = 1000;
    for (int attempt = 1; attempt <= kMaximumAttemptsUVE; ++attempt) {
        const std::string candidate = std::string{kScriptFolderUVE} + "/" + stem +
                                      (attempt == 1 ? std::string{} : "_" + std::to_string(attempt)) +
                                      std::string{extension};
        if (std::find(taken.cbegin(), taken.cend(), candidate) == taken.cend() && !exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

/// A UVScript identifier from free text: letters, digits and '_', never starting with a digit.
[[nodiscard]] std::string ScriptIdentifierUVE(const std::string_view text, const std::string_view fallback) {
    std::string identifier;
    for (const char character : text) {
        const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '_';
        if (keep) {
            identifier.push_back(character);
        }
    }
    if (identifier.empty() || (identifier.front() >= '0' && identifier.front() <= '9')) {
        identifier.insert(0, fallback);
    }
    return identifier;
}

} // namespace

std::string EditorUVE::DescribeScriptAssetProblemUVE(const std::string& path) const {
    if (path.empty()) {
        return "Enter the path of a script, such as scripts/player.uvs.";
    }
    if (!Scene::IsScriptAssetPathValidUVE(path)) {
        return "Use a project-relative path: no drive, no leading '/', no '..'.";
    }
    if (!IsUVScriptPathUVE(path)) {
        return "A script is a UVScript file ending in .uvs.";
    }
    if (!ReadProjectTextFileUVE(path).has_value()) {
        return "There is no file at this path.";
    }
    // A script with errors is still assignable: the text editor shows them.
    return {};
}

std::vector<std::string> EditorUVE::GetKnownScriptAssetPathsUVE() const {
    std::vector<std::string> paths;
    m_services->GetEntityManagerUVE().ForEachUVE<Scene::ScriptComponentUVE>(
        [this, &paths](const Scene::EntityUVE entity, const Scene::ScriptComponentUVE& script) {
            if (IsDocumentEntityUVE(entity) && !script.scriptAssetPath.empty()) {
                paths.push_back(script.scriptAssetPath);
            }
        });
    // The scripts folder, resolved by the same rule scripts are written by.
    std::filesystem::path folder = m_services->GetFileSystemUVE().ResolveRealPathUVE(kScriptFolderUVE);
    if (folder.empty()) {
        folder = std::filesystem::path{kScriptFolderUVE};
    }
    std::error_code error;
    if (std::filesystem::is_directory(folder, error)) {
        for (std::filesystem::recursive_directory_iterator iterator{folder, error}, end; !error && iterator != end;
             iterator.increment(error)) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == kUVScriptExtensionUVE) {
                const std::filesystem::path relative = std::filesystem::relative(iterator->path(), folder, error);
                if (!error) {
                    paths.push_back((std::filesystem::path{kScriptFolderUVE} / relative).generic_string());
                }
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

bool EditorUVE::AssignScriptToSelectedEntityUVE(const std::string& path) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !m_services->GetEntityManagerUVE().HasComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity) ||
        (!path.empty() && !DescribeScriptAssetProblemUVE(path).empty())) {
        return false;
    }
    const ScriptPathPropertyUVE target = FindScriptPathPropertyUVE();
    return target.entry != nullptr && SetSelectedComponentPropertyUVE(*target.entry, *target.property, &path);
}

bool EditorUVE::CreateUVScriptForSelectedEntityUVE() {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::EntityUVE entity = m_selectedEntity;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity) ||
        !entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath.empty()) {
        return false;
    }
    const ScriptPathPropertyUVE target = FindScriptPathPropertyUVE();
    if (target.entry == nullptr) {
        return false;
    }
    const std::string label = GetEntityDisplayLabelUVE(entity);
    const std::string path =
        FindFreeScriptPathUVE(ScriptFileStemUVE(label), GetKnownScriptAssetPathsUVE(), kUVScriptExtensionUVE,
                              [this](const std::string& candidate) { return ReadProjectTextFileUVE(candidate).has_value(); });
    if (path.empty() || !Scene::IsScriptAssetPathValidUVE(path)) {
        return false;
    }

    // The header names the object and the kind it drives; the two handlers every script starts from.
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(Scene::ResolveSceneObjectKindUVE(entityManager, entity));
    const std::string kind = ScriptIdentifierUVE(descriptor != nullptr ? descriptor->displayName : "Object", "Object");
    const std::string text = "entity " + ScriptIdentifierUVE(label, "Object_") + " : " + kind +
                             "\n\non ready:\n    print(\"{name} is ready\")\n\non tick(dt):\n    pass\n";
    // The file first: if it cannot be written, nothing about the object has changed yet.
    if (!WriteProjectTextFileUVE(path, text) || !SetSelectedComponentPropertyUVE(*target.entry, *target.property, &path)) {
        return false;
    }
    return OpenUVScriptForEntityUVE(entity);
}

void EditorUVE::OpenCreateScriptModalUVE(const std::filesystem::path& targetDirectory, const Scene::EntityUVE entity) {
    m_createScriptModal = CreateScriptModalStateUVE{};
    m_createScriptModal.isOpen = true;
    m_createScriptModal.openRequested = true;
    m_createScriptModal.targetDirectory = targetDirectory.empty() ? m_contentBrowserDirectory : targetDirectory;
    m_createScriptModal.targetEntity = entity;

    std::string defaultName = "NewScript";
    if (entity != Scene::kInvalidEntityUVE && m_services != nullptr) {
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        if (entityManager.IsAliveUVE(entity)) {
            const std::string entityLabel = GetEntityDisplayLabelUVE(entity);
            defaultName = ScriptIdentifierUVE(entityLabel, "Script");
            const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
                Scene::Objects::FindSceneObjectDescriptorUVE(Scene::ResolveSceneObjectKindUVE(entityManager, entity));
            m_createScriptModal.inheritsClass =
                descriptor != nullptr ? ScriptIdentifierUVE(descriptor->displayName, "Object") : "Object";
        }
    } else {
        m_createScriptModal.inheritsClass = "Object";
    }

    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path contentRoot = snapshot.contentRoot;
    std::filesystem::path fullDir = m_createScriptModal.targetDirectory;
    if (fullDir.is_relative()) {
        fullDir = contentRoot / fullDir;
    }
    const std::filesystem::path fullDefault = fullDir / (defaultName + ".uvs");
    const std::filesystem::path relDefault = fullDefault.lexically_relative(contentRoot);
    m_createScriptModal.scriptPathText = (relDefault.empty() || *relDefault.begin() == "..") ? fullDefault.generic_string() : relDefault.generic_string();
}

} // namespace UVE::Editor
