// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

/// Marker component identifying the document's one scene-root entity. Structural only by
/// design: the root carries a name and an identity transform and exists so every document has
/// exactly one top-of-hierarchy anchor (Godot-style single root). Scene-wide settings
/// (environment, navigation, streaming) are deliberately NOT on it yet - they get their own
/// authored homes when the systems that consume them exist, not before.
struct SceneRootComponent final {};

[[nodiscard]] constexpr bool IsSceneRootComponentValid(const SceneRootComponent&) noexcept {
    return true; // a pure marker: no authored state to invalidate
}

/// Authoring definition for the scene root. The recipe is the marker component (plus the
/// creation shell's Name + Transform); `defaultName` is what a fresh document's root is
/// called. The kind is `libraryCreatable = false`: the root is created by the document
/// lifecycle (new document, load-time migration), never through the Add-Object library.
struct SceneRootObjectDefinition final {
    static constexpr std::string_view defaultName = "SceneRoot";
};

[[nodiscard]] constexpr bool IsSceneRootObjectDefinitionValid(const SceneRootObjectDefinition&) noexcept {
    return true;
}

/// Attaches the scene-root recipe to `entity`: the Object3D transform baseline shared with every
/// other 3D scene object (via EnsureObject3DBaseline), plus the scene-root marker. The entity
/// must be alive; existing components keep their values.
void ApplySceneRootObjectDefinition(IEntityManager& entityManager, Entity entity,
                                     const SceneRootObjectDefinition& value);

} // namespace UVE::Scene
