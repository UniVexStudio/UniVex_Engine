// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Blueprint slot: the Inspector row that says which Blueprint an object runs, and the commands behind
// it - Quick Load, Load and Clear.
//
// An object's gameplay is one Blueprint (`.uvbp`), the asset its Script component names. Quick Load and Load
// point the object at a Blueprint that already exists.

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
constexpr std::string_view kBlueprintExtension = ".uvbp";

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

} // namespace

std::string Editor::DescribeScriptAssetProblem(const std::string& path) const {
    if (path.empty()) {
        return "Enter the path of a Blueprint, such as scripts/player.uvbp.";
    }
    if (!Scene::IsScriptAssetPathValid(path)) {
        return "Use a project-relative path: no drive, no leading '/', no '..'.";
    }
    if (!Scene::Script::IsBlueprintPath(path)) {
        return "A Blueprint is a file ending in .uvbp.";
    }
    if (!ReadProjectTextFile(path).has_value()) {
        return "There is no file at this path.";
    }
    // A Blueprint with errors is still assignable: Compile lists them.
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
    // The scripts folder, resolved by the same rule Blueprints are written by.
    std::filesystem::path folder = m_services->GetFileSystem().ResolveRealPath(kScriptFolder);
    if (folder.empty()) {
        folder = std::filesystem::path{kScriptFolder};
    }
    std::error_code error;
    if (std::filesystem::is_directory(folder, error)) {
        for (std::filesystem::recursive_directory_iterator iterator{folder, error}, end; !error && iterator != end;
             iterator.increment(error)) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == kBlueprintExtension) {
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

} // namespace UVE::Editor
