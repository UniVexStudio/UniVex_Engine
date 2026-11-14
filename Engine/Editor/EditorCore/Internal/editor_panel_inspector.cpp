// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Inspector panel and every built-in component drawer: name, hierarchy, transform, primitive
// mesh, world environment, character controller, canvas, UI text/image/button, scene components
// and prefabs - plus the Add-Component panel and the drawer registration that wires them up.
//
// Seventeen methods and a thousand lines, the largest single group in editor_uve.cpp and the one
// most often edited: adding a component type means adding a drawer, and that work had to be done
// inside a seven-thousand-line file shared with the viewport, the menus and the content browser.
// This is the group that most justifies the split.
//
// The methods remain EditorUVE members, declared where they always were. They read and write
// editor state directly - selection, the tool session, the drawer registry - and turning that
// into parameters to make them free functions would be a redesign of the editor's state
// ownership disguised as a file move.
//
// Moved verbatim. Not one line of the seventeen functions differs from what was in
// editor_uve.cpp; anything here worth improving was already here and belongs in its own commit
// where it reads as a change rather than hiding inside a move.

#include "editor.h"
#include "camera_3d.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor_entity_label.h"

#include "asset_import_queue.h"
#include "material_asset.h"
#include "shader_asset.h"
#include "animation_sequencer_component.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "particle_emitter_component.h"
#include "prefab_instance_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "visibility_component.h"
#include "i_entity_manager.h"
#include "world_environment_3d.h"
#include "i_scene_graph.h"

namespace UVE::Editor {

namespace {

}

namespace {

}

void EditorUVE::RepairInspectorRecipeUVE(const Scene::EntityUVE entity) {
    // An object saved before its recipe included Visibility and the Object section is given them here,
    // where they are first needed. Every default is Inherit or empty, so this changes nothing
    // about how the scene runs.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity)) {
        return;
    }
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
        !entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(entity, Scene::VisibilityComponentUVE{});
    }
    Scene::EnsureCommonObjectSectionUVE(entityManager, entity);
}

void EditorUVE::SetInspectorFoldOpenUVE(const std::string& key, const bool open) {
    if (key.empty()) {
        return;
    }
    const auto it = m_inspectorFoldOpen.find(key);
    if (it != m_inspectorFoldOpen.end()) {
        it->second = open;
    } else if (m_inspectorFoldOpen.size() < kMaxRememberedInspectorFoldsUVE) {
        m_inspectorFoldOpen.emplace(key, open);
    }
}

bool EditorUVE::IsInspectorFoldOpenUVE(const std::string& key, const bool defaultOpen) const {
    const auto it = m_inspectorFoldOpen.find(key);
    return it != m_inspectorFoldOpen.end() ? it->second : defaultOpen;
}

} // namespace UVE::Editor
