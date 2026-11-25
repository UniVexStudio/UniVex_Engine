// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Scene Hierarchy panel: the outliner tree, its filter, rename-in-place, and the
// drag-and-drop reparenting that goes with it.
//
// Split out of editor_uve.cpp, which had grown past seven thousand lines with twenty-eight draw
// methods in it - a file where fixing one panel means scrolling past every other. The methods are
// still Editor members and are declared in the same header they always were: they read and
// write nine pieces of editor state (selection, the rename buffer, the filter, panel visibility),
// and turning those into parameters purely to make the functions free would have been a redesign
// of the editor's state ownership dressed up as a file move. A translation unit boundary gets the
// navigability without touching the design.
//
// Each row also has a right-click menu - add a child, rename, duplicate, delete - running the same
// commands as the keyboard shortcuts, so both paths share one set of rules (the scene root, for
// one, can be renamed but never duplicated, deleted or dragged).

#include "editor.h"
#include "scalar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "editor_text_search.h"

#include "i_asset_database.h"
#include "collider_component.h"
#include "hierarchy_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "prefab_instance_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "i_entity_manager.h"
#include "i_scene_graph.h"
#include "character_3d.h"
#include "preview_lighting.h"
#include "skeleton_3d.h"
#include "scene_folder.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"

namespace UVE::Editor {

namespace {

}

bool Editor::SetHierarchyBranchOpen(const Scene::Entity entity, const bool open) {
    if (!IsDocumentEntity(entity)) {
        return false;
    }
    // A request left for a row that has since been deleted would never be drawn; drop those here,
    // so the map only ever holds live rows.
    std::erase_if(m_hierarchyPendingRowOpen,
                  [this](const auto& pending) { return !IsDocumentEntity(pending.first); });

    // One pass over the parent links instead of a children query per object, which scans the whole
    // scene each time.
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<std::pair<Scene::Entity, Scene::Entity>> links;
    entityManager.ForEach<Scene::HierarchyComponent>(
        [&links](const Scene::Entity child, const Scene::HierarchyComponent& hierarchy) {
            links.emplace_back(hierarchy.parent, child);
        });
    std::unordered_map<Scene::Entity, std::vector<Scene::Entity>> childrenOf;
    for (const auto& [parent, child] : links) {
        if (parent != Scene::kInvalidEntity && IsDocumentEntity(child)) {
            childrenOf[parent].push_back(child);
        }
    }

    // Only rows with children have an open state; a leaf has nothing to open. The visited set
    // keeps a malformed parent loop from walking forever.
    std::vector<std::pair<Scene::Entity, std::int64_t>> pending{{entity, 0}};
    std::unordered_set<Scene::Entity> visited;
    const std::int64_t depthLimit = m_extendedPreferences.hierarchyExpandDepthLimit > 0
                                        ? m_extendedPreferences.hierarchyExpandDepthLimit
                                        : 16;
    while (!pending.empty()) {
        const auto [current, depth] = pending.back();
        pending.pop_back();
        if (open && depth >= depthLimit) {
            continue;
        }
        const auto children = childrenOf.find(current);
        if (!visited.insert(current).second || children == childrenOf.end()) {
            continue;
        }
        m_hierarchyPendingRowOpen[current] = open;
        for (const Scene::Entity child : children->second) {
            pending.emplace_back(child, depth + 1);
        }
    }
    return true;
}

bool Editor::OpenHierarchyRowTarget(const Scene::Entity entity) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(entity) || !IsDocumentEntity(entity)) {
        return false;
    }
    // Rows that stand for a file open that file. Two of them exist: an instance placed from an
    // entity asset, and the level's own Viewport. Everything else in the Outliner is a thing the
    // scene owns, with nothing outside the scene to open, so the Double-Click preference keeps
    // deciding for those (rename, focus in the viewport, open-and-close the row).
    if (entityManager.HasComponent<Scene::PrefabInstanceComponent>(entity) && IsAuthoringCommandAllowed()) {
        const Scene::PrefabInstanceComponent& instance =
            entityManager.GetComponent<Scene::PrefabInstanceComponent>(entity);
        if (instance.sourcePrefabGuid != Asset::kInvalidAssetGuid) {
            // The path is read before anything opens: opening the Entity Editor replaces the
            // document, and this handle would be dead by the time it returned.
            const std::filesystem::path source =
                m_services->GetAssetDatabase().Resolve(instance.sourcePrefabGuid);
            std::error_code error;
            if (!source.empty() && std::filesystem::is_regular_file(source, error) && OpenEntityEditor(source)) {
                return true;
            }
        }
    }
    if (entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(entity)) {
        // The Viewport row is the level itself, so opening it means opening its .uvscene. A
        // document with unsaved work is not reloaded from disk: a double-click must never be able
        // to throw away edits. The click is still consumed, so it cannot fall through to a rename.
        if (m_activeScenePath.empty()) {
            return false;
        }
        if (m_sceneDirty) {
            return true;
        }
        return OpenSceneAsset(m_activeScenePath);
    }
    return false;
}

std::optional<bool> Editor::GetPendingHierarchyRowOpen(const Scene::Entity entity) const {
    const auto pending = m_hierarchyPendingRowOpen.find(entity);
    if (pending == m_hierarchyPendingRowOpen.end()) {
        return std::nullopt;
    }
    return pending->second;
}

bool Editor::IsEntityLocked(const Scene::Entity entity) const noexcept {
    return entity != Scene::kInvalidEntity && m_lockedFolders.contains(entity);
}

void Editor::SetEntityLocked(const Scene::Entity entity, const bool locked) noexcept {
    if (entity == Scene::kInvalidEntity) {
        return;
    }
    if (locked) {
        m_lockedFolders.insert(entity);
    } else {
        m_lockedFolders.erase(entity);
    }
}

void Editor::ToggleEntityLock(const Scene::Entity entity) noexcept {
    SetEntityLocked(entity, !IsEntityLocked(entity));
}

std::vector<std::string> Editor::GetObjectWarnings(const Scene::Entity entity) const {
    std::vector<std::string> warnings;
    if (!IsDocumentEntity(entity)) {
        return warnings;
    }
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Asset::IAssetDatabase& assets = m_services->GetAssetDatabase();
    if (entityManager.HasComponent<Scene::TransformComponent>(entity) &&
        !IsTransformFinite(entityManager.GetComponent<Scene::TransformComponent>(entity))) {
        warnings.emplace_back("The transform holds a value that is not a number or is infinite.");
    }
    if (entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
        const std::string& path = entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
        if (!path.empty() && !Scene::IsScriptAssetPathValid(path)) {
            warnings.emplace_back("The script path is not a valid project path.");
        }
    }
    if (entityManager.HasComponent<Scene::MeshComponent>(entity)) {
        const Scene::MeshComponent& mesh = entityManager.GetComponent<Scene::MeshComponent>(entity);
        if (mesh.meshGuid == Asset::kInvalidAssetGuid) {
            warnings.emplace_back("No mesh is assigned, so nothing is drawn.");
        } else if (!assets.HasGuid(mesh.meshGuid)) {
            warnings.emplace_back("The assigned mesh is no longer in the project.");
        }
        if (mesh.materialGuid != Asset::kInvalidAssetGuid && !assets.HasGuid(mesh.materialGuid)) {
            warnings.emplace_back("The assigned material is no longer in the project.");
        }
    }
    if (entityManager.HasComponent<Scene::Skeleton3DComponent>(entity) &&
        entityManager.GetComponent<Scene::Skeleton3DComponent>(entity).skeletonAssetPath.empty()) {
        warnings.emplace_back("No source model is set, so the skeleton has no bones.");
    }
    // A character with no shape is a controller with no body. The controller sweeps a shape
    // through the world to decide what it stands on, what it can step over and where it stops;
    // with no shape the sweep finds nothing, so the body neither walks nor stands - it is simply
    // refused, and the object looks authored while doing nothing. That silence is what a row badge
    // is for.
    //
    // The shape counts whether it is on the character or on a Collider3D of its own under it, so
    // this asks the same question the stepper asks. Both Character3D and Player3D are in the same
    // position: neither is born with a collider, by design, because a built-in one made the
    // Collider3D an author then added a second shape in the same place. Computed from the tree
    // every frame, so adding the Collider3D clears it with nothing to dismiss.
    const Scene::Objects::SceneObjectKind kind = Scene::ResolveSceneObjectKind(entityManager, entity);
    if (kind == Scene::Objects::SceneObjectKind::Character3D ||
        kind == Scene::Objects::SceneObjectKind::Player3D) {
        Scene::IEntityManager& entities = m_services->GetEntityManager();
        const Scene::Entity body =
            Scene::ResolveCharacterShapeEntity(entities, m_services->GetSceneGraph(), entity);
        if (body == Scene::kInvalidEntity) {
            warnings.emplace_back("No Collider3D: this character has no body, so it falls through the "
                                  "level. A mesh it wears is not a shape - add a Collider3D under it.");
        } else if (entities.HasComponent<Scene::ColliderComponent>(body) &&
                   Scene::IsLegacyCharacterCapsule(
                       entities.GetComponent<Scene::ColliderComponent>(body))) {
            // The one shape an author is most likely to have forgotten, because they never added
            // it: characters used to be born with this capsule, and a document saved back then
            // still carries it. A Collider3D draws nothing, so it holds the body up invisibly -
            // the body stands on a shape that is not in the Outliner and not in the viewport.
            warnings.emplace_back("Body: the 0.4 m x 1.8 m capsule characters used to be born with. "
                                  "If you did not add it, it was saved into this asset by an older "
                                  "version - remove the Collider to let the body fall, or keep it and "
                                  "move the mesh down to where it stands.");
        }
    }
    return warnings;
}

std::optional<std::string> Editor::GetObjectScriptPath(const Scene::Entity entity) const {
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(entity) || !entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
        return std::nullopt;
    }
    const std::string& path = entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
    return path.empty() ? std::nullopt : std::optional<std::string>{path};
}

} // namespace UVE::Editor
