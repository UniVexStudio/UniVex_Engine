// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_icons.h"

#include "prism/Menu.h"
#include "scene_object_registry.h"

#include <functional>
#include <vector>

namespace UVE::Editor {

/// Where an "+ Add" menu creates objects. Each offers only what belongs there (the standard Place Actors
/// for a level, the Components panel's Add for a Blueprint):
///   Level  - what a level holds: meshes, lights, cameras, the environment and sky, volumes, world
///            streaming, physics props, UI. Not a character's parts (those are built in an Entity).
///   Entity - what an entity is built from: meshes, cameras, lights on it, its skeleton, IK, bone
///            attachments, animation, colliders, hitboxes, audio, particles. Not the level's
///            environment, sky, streaming, navigation volumes or probes.
enum class AddObjectContext { Level, Entity };

/// What an entity is built on, for its "+ Add": a character (Character3D, Player3D) takes its
/// parts - meshes, skeleton, animation, colliders, hitboxes, camera rig; a body (Static3D,
/// Rigid3D, Kinematic3D) takes what a prop has - meshes, colliders, lights, sounds, effects, a
/// sequencer for moving parts - and no skeleton, IK, navigation or camera arm. Neither takes a
/// second physics body. Any: everything an entity may hold.
enum class EntityRoot { Any, Character, Body };

/// The root category of an object kind (Any for the kinds that are neither).
[[nodiscard]] EntityRoot EntityRootOf(Scene::Objects::SceneObjectKind rootKind) noexcept;

/// True when `kind` fits an entity whose root is `root` (see EntityRoot).
[[nodiscard]] bool IsObjectOfferedForRoot(Scene::Objects::SceneObjectKind kind, EntityRoot root) noexcept;

/// True when `kind` is offered in `context`.
[[nodiscard]] bool IsObjectOffered(Scene::Objects::SceneObjectKind kind, AddObjectContext context) noexcept;

/// The dedicated glyph of an object kind (prism::Icon::None when it has none).
[[nodiscard]] prism::Icon ObjectKindGlyph(Scene::Objects::SceneObjectKind kind) noexcept;

/// The menu items of an "+ Add": one submenu per group (Basic, Meshes, Lights, Camera, Physics,
/// Animation, ...) holding the kinds offered in `context`, each with its icon; `favorites` first, as
/// plain items. For prism::showSearchMenu, which shows the groups as headers and searches them all.
[[nodiscard]] std::vector<prism::MenuItem> BuildAddObjectMenu(
    AddObjectContext context, EditorIcons& icons,
    const std::function<void(Scene::Objects::SceneObjectKind)>& create,
    const std::vector<Scene::Objects::SceneObjectKind>& favorites = {}, EntityRoot root = EntityRoot::Any);

} // namespace UVE::Editor
