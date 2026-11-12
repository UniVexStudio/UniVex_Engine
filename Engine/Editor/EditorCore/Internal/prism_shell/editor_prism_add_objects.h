// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_prism_icons.h"

#include "prism/Menu.h"
#include "scene_object_registry.h"

#include <functional>
#include <vector>

namespace UVE::Editor {

/// Where an "+ Add" menu creates objects. Each offers only what belongs there (Unreal's Place Actors
/// for a level, the Components panel's Add for a Blueprint):
///   Level  - what a level holds: meshes, lights, cameras, the environment and sky, volumes, world
///            streaming, physics props, UI. Not a character's parts (those are built in an Entity).
///   Entity - what an entity is built from: meshes, cameras, lights on it, its skeleton, IK, bone
///            attachments, animation, colliders, hitboxes, audio, particles. Not the level's
///            environment, sky, streaming, navigation volumes or probes.
enum class AddObjectContextUVE { Level, Entity };

/// True when `kind` is offered in `context`.
[[nodiscard]] bool IsObjectOfferedUVE(Scene::Objects::SceneObjectKindUVE kind, AddObjectContextUVE context) noexcept;

/// The dedicated glyph of an object kind (prism::Icon::None when it has none).
[[nodiscard]] prism::Icon ObjectKindGlyphUVE(Scene::Objects::SceneObjectKindUVE kind) noexcept;

/// The menu items of an "+ Add": one submenu per group (Basic, Meshes, Lights, Camera, Physics,
/// Animation, ...) holding the kinds offered in `context`, each with its icon; `favorites` first, as
/// plain items. For prism::showSearchMenu, which shows the groups as headers and searches them all.
[[nodiscard]] std::vector<prism::MenuItem> BuildAddObjectMenuUVE(
    AddObjectContextUVE context, EditorPrismIconsUVE& icons,
    const std::function<void(Scene::Objects::SceneObjectKindUVE)>& create,
    const std::vector<Scene::Objects::SceneObjectKindUVE>& favorites = {});

} // namespace UVE::Editor
