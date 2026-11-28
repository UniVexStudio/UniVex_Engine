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

#include "script.h"
#include "script_component.h"
#include "i_entity_manager.h"
#include "scene_object_type.h"

namespace UVE::Editor {
namespace {

constexpr std::string_view kScriptFolder = "scripts";
constexpr std::string_view kUVScriptExtension = ".uvs";

/// A file stem from an object name: letters, digits, '-' and '_' kept, anything else an underscore,
/// runs of underscores collapsed. "Main Menu (old)" becomes "Main_Menu_old".
[[nodiscard]] std::string ScriptFileStem(const std::string& name) {
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
    constexpr std::size_t kMaximumStemBytes = 64U;
    if (stem.size() > kMaximumStemBytes) {
        stem.resize(kMaximumStemBytes);
    }
    return stem.empty() ? std::string{"script"} : stem;
}

/// The Script component's declared path property, through which every slot edit goes so it is one
/// undo entry exactly like any other property edit.
struct ScriptPathProperty final {
    const Core::TypeMetadataEntry* entry = nullptr;
    const Core::TypeMetadataProperty* property = nullptr;
};

[[nodiscard]] ScriptPathProperty FindScriptPathProperty() {
    const Core::TypeMetadataEntry* const entry =
        Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::ScriptComponent)));
    if (entry == nullptr) {
        return {};
    }
    const auto property = std::find_if(entry->properties.cbegin(), entry->properties.cend(),
                                       [](const Core::TypeMetadataProperty& candidate) {
                                           return candidate.name == "scriptAssetPath";
                                       });
    return property == entry->properties.cend() ? ScriptPathProperty{} : ScriptPathProperty{entry, &*property};
}

/// A UVScript text file: `.uvs` only (`Scene::Script::IsUVScriptPath` also accepts Blueprint graphs).
[[nodiscard]] bool IsUVScriptPath(const std::string_view path) noexcept { return path.ends_with(kUVScriptExtension); }

/// A free script path for `stem`: not a file that exists, and not a path another object already
/// names (it may not have been saved yet). Bounded, so a folder full of collisions gives up
/// (empty) instead of spinning.
template <typename ExistsFn>
[[nodiscard]] std::string FindFreeScriptPath(const std::string& stem, const std::vector<std::string>& taken,
                                                const std::string_view extension, const ExistsFn& exists) {
    constexpr int kMaximumAttempts = 1000;
    for (int attempt = 1; attempt <= kMaximumAttempts; ++attempt) {
        const std::string candidate = std::string{kScriptFolder} + "/" + stem +
                                      (attempt == 1 ? std::string{} : "_" + std::to_string(attempt)) +
                                      std::string{extension};
        if (std::find(taken.cbegin(), taken.cend(), candidate) == taken.cend() && !exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

/// A UVScript identifier from free text: letters, digits and '_', never starting with a digit.
[[nodiscard]] std::string ScriptIdentifier(const std::string_view text, const std::string_view fallback) {
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

std::string Editor::DescribeScriptAssetProblem(const std::string& path) const {
    if (path.empty()) {
        return "Enter the path of a script, such as scripts/player.uvs.";
    }
    if (!Scene::IsScriptAssetPathValid(path)) {
        return "Use a project-relative path: no drive, no leading '/', no '..'.";
    }
    if (!IsUVScriptPath(path)) {
        return "A script is a UVScript file ending in .uvs.";
    }
    if (!ReadProjectTextFile(path).has_value()) {
        return "There is no file at this path.";
    }
    // A script with errors is still assignable: the text editor shows them.
    return {};
}

std::vector<std::string> Editor::GetKnownScriptAssetPaths() const {
    std::vector<std::string> paths;
    m_services->GetEntityManager().ForEach<Scene::ScriptComponent>(
        [this, &paths](const Scene::Entity entity, const Scene::ScriptComponent& script) {
            if (IsDocumentEntity(entity) && !script.scriptAssetPath.empty()) {
                paths.push_back(script.scriptAssetPath);
            }
        });
    // The scripts folder, resolved by the same rule scripts are written by.
    std::filesystem::path folder = m_services->GetFileSystem().ResolveRealPath(kScriptFolder);
    if (folder.empty()) {
        folder = std::filesystem::path{kScriptFolder};
    }
    std::error_code error;
    if (std::filesystem::is_directory(folder, error)) {
        for (std::filesystem::recursive_directory_iterator iterator{folder, error}, end; !error && iterator != end;
             iterator.increment(error)) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == kUVScriptExtension) {
                const std::filesystem::path relative = std::filesystem::relative(iterator->path(), folder, error);
                if (!error) {
                    paths.push_back((std::filesystem::path{kScriptFolder} / relative).generic_string());
                }
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

bool Editor::AssignScriptToSelectedEntity(const std::string& path) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !m_services->GetEntityManager().HasComponent<Scene::ScriptComponent>(m_selectedEntity) ||
        (!path.empty() && !DescribeScriptAssetProblem(path).empty())) {
        return false;
    }
    const ScriptPathProperty target = FindScriptPathProperty();
    return target.entry != nullptr && SetSelectedComponentProperty(*target.entry, *target.property, &path);
}

bool Editor::CreateUVScriptForSelectedEntity() {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection()) {
        return false;
    }
    const Scene::Entity entity = m_selectedEntity;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::ScriptComponent>(entity) ||
        !entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath.empty()) {
        return false;
    }
    const ScriptPathProperty target = FindScriptPathProperty();
    if (target.entry == nullptr) {
        return false;
    }
    const std::string label = GetEntityDisplayLabel(entity);
    const std::string path =
        FindFreeScriptPath(ScriptFileStem(label), GetKnownScriptAssetPaths(), kUVScriptExtension,
                              [this](const std::string& candidate) { return ReadProjectTextFile(candidate).has_value(); });
    if (path.empty() || !Scene::IsScriptAssetPathValid(path)) {
        return false;
    }

    // The header names the object and the kind it drives; the two handlers every script starts from.
    const Scene::Objects::SceneObjectDescriptor* const descriptor =
        Scene::Objects::FindSceneObjectDescriptor(Scene::ResolveSceneObjectKind(entityManager, entity));
    const std::string kind = ScriptIdentifier(descriptor != nullptr ? descriptor->displayName : "Object", "Object");
    const std::string text = "entity " + ScriptIdentifier(label, "Object_") + " : " + kind +
                             "\n\non ready:\n    print(\"{name} is ready\")\n\non tick(dt):\n    pass\n";
    // The file first: if it cannot be written, nothing about the object has changed yet.
    if (!WriteProjectTextFile(path, text) || !SetSelectedComponentProperty(*target.entry, *target.property, &path)) {
        return false;
    }
    return OpenUVScriptForEntity(entity);
}

void Editor::OpenCreateScriptModal(const std::filesystem::path& targetDirectory, const Scene::Entity entity) {
    m_createScriptModal = CreateScriptModalState{};
    m_createScriptModal.isOpen = true;
    m_createScriptModal.openRequested = true;
    m_createScriptModal.targetDirectory = targetDirectory.empty() ? m_assetTableDirectory : targetDirectory;
    m_createScriptModal.targetEntity = entity;

    std::string defaultName = "NewScript";
    if (entity != Scene::kInvalidEntity && m_services != nullptr) {
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        if (entityManager.IsAlive(entity)) {
            const std::string entityLabel = GetEntityDisplayLabel(entity);
            defaultName = ScriptIdentifier(entityLabel, "Script");
            const Scene::Objects::SceneObjectDescriptor* const descriptor =
                Scene::Objects::FindSceneObjectDescriptor(Scene::ResolveSceneObjectKind(entityManager, entity));
            m_createScriptModal.inheritsClass =
                descriptor != nullptr ? ScriptIdentifier(descriptor->displayName, "Object") : "Object";
        }
    } else {
        m_createScriptModal.inheritsClass = "Object";
    }

    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
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
