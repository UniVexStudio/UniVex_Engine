// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Marker component identifying the document's one scene-root entity. Structural only by
/// design: the root carries a name and an identity transform and exists so every document has
/// exactly one top-of-hierarchy anchor (Godot-style single root). Scene-wide settings
/// (environment, navigation, streaming) are deliberately NOT on it yet - they get their own
/// authored homes when the systems that consume them exist, not before.
struct SceneRootComponentUVE final {};

[[nodiscard]] constexpr bool IsSceneRootComponentValidUVE(const SceneRootComponentUVE&) noexcept {
    return true; // a pure marker: no authored state to invalidate
}

/// Authoring definition for the scene root. The recipe is the marker component (plus the
/// creation shell's Name + Transform); `defaultName` is what a fresh document's root is
/// called. The kind is `libraryCreatable = false`: the root is created by the document
/// lifecycle (new document, load-time migration), never through the Add-Object library.
struct SceneRootObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "SceneRoot";
};

[[nodiscard]] constexpr bool IsSceneRootObjectDefinitionValidUVE(const SceneRootObjectDefinitionUVE&) noexcept {
    return true;
}

/// Attaches the scene-root recipe to `entity`: the Object3D transform baseline shared with every
/// other 3D scene object (via EnsureObject3DBaselineUVE), plus the scene-root marker. The entity
/// must be alive; existing components keep their values.
void ApplySceneRootObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const SceneRootObjectDefinitionUVE& value);

} // namespace UVE::Scene
