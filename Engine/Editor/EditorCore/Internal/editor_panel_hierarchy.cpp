// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Scene Hierarchy panel: the outliner tree, its filter, rename-in-place, and the
// drag-and-drop reparenting that goes with it.
//
// Split out of editor_uve.cpp, which had grown past seven thousand lines with twenty-eight draw
// methods in it - a file where fixing one panel means scrolling past every other. The methods are
// still EditorUVE members and are declared in the same header they always were: they read and
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

bool EditorUVE::SetHierarchyBranchOpenUVE(const Scene::EntityUVE entity, const bool open) {
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }
    // A request left for a row that has since been deleted would never be drawn; drop those here,
    // so the map only ever holds live rows.
    std::erase_if(m_hierarchyPendingRowOpen,
                  [this](const auto& pending) { return !IsDocumentEntityUVE(pending.first); });

    // One pass over the parent links instead of a children query per object, which scans the whole
    // scene each time.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<std::pair<Scene::EntityUVE, Scene::EntityUVE>> links;
    entityManager.ForEachUVE<Scene::HierarchyComponentUVE>(
        [&links](const Scene::EntityUVE child, const Scene::HierarchyComponentUVE& hierarchy) {
            links.emplace_back(hierarchy.parent, child);
        });
    std::unordered_map<Scene::EntityUVE, std::vector<Scene::EntityUVE>> childrenOf;
    for (const auto& [parent, child] : links) {
        if (parent != Scene::kInvalidEntityUVE && IsDocumentEntityUVE(child)) {
            childrenOf[parent].push_back(child);
        }
    }

    // Only rows with children have an open state; a leaf has nothing to open. The visited set
    // keeps a malformed parent loop from walking forever.
    std::vector<std::pair<Scene::EntityUVE, std::int64_t>> pending{{entity, 0}};
    std::unordered_set<Scene::EntityUVE> visited;
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
        for (const Scene::EntityUVE child : children->second) {
            pending.emplace_back(child, depth + 1);
        }
    }
    return true;
}

bool EditorUVE::OpenHierarchyRowTargetUVE(const Scene::EntityUVE entity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(entity) || !IsDocumentEntityUVE(entity)) {
        return false;
    }
    // Rows that stand for a file open that file. Two of them exist: an instance placed from an
    // entity asset, and the level's own Viewport. Everything else in the Outliner is a thing the
    // scene owns, with nothing outside the scene to open, so the Double-Click preference keeps
    // deciding for those (rename, focus in the viewport, open-and-close the row).
    if (entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(entity) && IsAuthoringCommandAllowedUVE()) {
        const Scene::PrefabInstanceComponentUVE& instance =
            entityManager.GetComponentUVE<Scene::PrefabInstanceComponentUVE>(entity);
        if (instance.sourcePrefabGuid != Asset::kInvalidAssetGuidUVE) {
            // The path is read before anything opens: opening the Entity Editor replaces the
            // document, and this handle would be dead by the time it returned.
            const std::filesystem::path source =
                m_services->GetAssetDatabaseUVE().ResolveUVE(instance.sourcePrefabGuid);
            std::error_code error;
            if (!source.empty() && std::filesystem::is_regular_file(source, error) && OpenEntityEditorUVE(source)) {
                return true;
            }
        }
    }
    if (entityManager.HasComponentUVE<Scene::OutlinerViewport3DComponentUVE>(entity)) {
        // The Viewport row is the level itself, so opening it means opening its .uvscene. A
        // document with unsaved work is not reloaded from disk: a double-click must never be able
        // to throw away edits. The click is still consumed, so it cannot fall through to a rename.
        if (m_activeScenePath.empty()) {
            return false;
        }
        if (m_sceneDirty) {
            return true;
        }
        return OpenSceneAssetUVE(m_activeScenePath);
    }
    return false;
}

std::optional<bool> EditorUVE::GetPendingHierarchyRowOpenUVE(const Scene::EntityUVE entity) const {
    const auto pending = m_hierarchyPendingRowOpen.find(entity);
    if (pending == m_hierarchyPendingRowOpen.end()) {
        return std::nullopt;
    }
    return pending->second;
}

bool EditorUVE::IsEntityLockedUVE(const Scene::EntityUVE entity) const noexcept {
    return entity != Scene::kInvalidEntityUVE && m_lockedFolders.contains(entity);
}

void EditorUVE::SetEntityLockedUVE(const Scene::EntityUVE entity, const bool locked) noexcept {
    if (entity == Scene::kInvalidEntityUVE) {
        return;
    }
    if (locked) {
        m_lockedFolders.insert(entity);
    } else {
        m_lockedFolders.erase(entity);
    }
}

void EditorUVE::ToggleEntityLockUVE(const Scene::EntityUVE entity) noexcept {
    SetEntityLockedUVE(entity, !IsEntityLockedUVE(entity));
}

std::vector<std::string> EditorUVE::GetObjectWarningsUVE(const Scene::EntityUVE entity) const {
    std::vector<std::string> warnings;
    if (!IsDocumentEntityUVE(entity)) {
        return warnings;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Asset::IAssetDatabaseUVE& assets = m_services->GetAssetDatabaseUVE();
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
        !IsTransformFiniteUVE(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity))) {
        warnings.emplace_back("The transform holds a value that is not a number or is infinite.");
    }
    if (entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (!path.empty() && !Scene::IsScriptAssetPathValidUVE(path)) {
            warnings.emplace_back("The script path is not a valid project path.");
        }
    }
    if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        const Scene::MeshComponentUVE& mesh = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
        if (mesh.meshGuid == Asset::kInvalidAssetGuidUVE) {
            warnings.emplace_back("No mesh is assigned, so nothing is drawn.");
        } else if (!assets.HasGuidUVE(mesh.meshGuid)) {
            warnings.emplace_back("The assigned mesh is no longer in the project.");
        }
        if (mesh.materialGuid != Asset::kInvalidAssetGuidUVE && !assets.HasGuidUVE(mesh.materialGuid)) {
            warnings.emplace_back("The assigned material is no longer in the project.");
        }
    }
    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(entity) &&
        entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(entity).skeletonAssetPath.empty()) {
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
    const Scene::Objects::SceneObjectKindUVE kind = Scene::ResolveSceneObjectKindUVE(entityManager, entity);
    if (kind == Scene::Objects::SceneObjectKindUVE::Character3D ||
        kind == Scene::Objects::SceneObjectKindUVE::Player3D) {
        Scene::IEntityManagerUVE& entities = m_services->GetEntityManagerUVE();
        const Scene::EntityUVE body =
            Scene::ResolveCharacterShapeEntityUVE(entities, m_services->GetSceneGraphUVE(), entity);
        if (body == Scene::kInvalidEntityUVE) {
            warnings.emplace_back("No Collider3D: this character has no body, so it falls through the "
                                  "level. A mesh it wears is not a shape - add a Collider3D under it.");
        } else if (entities.HasComponentUVE<Scene::ColliderComponentUVE>(body) &&
                   Scene::IsLegacyCharacterCapsuleUVE(
                       entities.GetComponentUVE<Scene::ColliderComponentUVE>(body))) {
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

std::optional<std::string> EditorUVE::GetObjectScriptPathUVE(const Scene::EntityUVE entity) const {
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return std::nullopt;
    }
    const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
    return path.empty() ? std::nullopt : std::optional<std::string>{path};
}

} // namespace UVE::Editor
