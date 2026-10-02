// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// The object type an entity was created as. Stamped when a object is made, saved with the scene by
/// its stable type id, and copied with the rest of the object by duplicate, undo and prefabs.
///
/// It exists because the type cannot be read back from components alone: a Static3D and a
/// Collider3D carry the same collider, and a Object3D, a BoxMesh3D and a SphereMesh3D share every
/// component but one. Without it, anything that wants to say what a object is guesses, and the
/// guesses disagree.
struct SceneObjectTypeComponentUVE final {
    Objects::SceneObjectKindUVE kind = Objects::SceneObjectKindUVE::Object3D;
};

/// True when `value` names a kind the object registry knows.
[[nodiscard]] bool IsSceneObjectTypeComponentValidUVE(const SceneObjectTypeComponentUVE& value) noexcept;

/// The object kind `entity` is: its stored type when it has one, otherwise InferSceneObjectKindUVE.
/// Object3D for a dead entity.
[[nodiscard]] Objects::SceneObjectKindUVE ResolveSceneObjectKindUVE(const IEntityManagerUVE& entityManager,
                                                              EntityUVE entity);

/// The best reading of `entity`'s kind from its components alone, for objects that have no stored
/// type: those in scenes saved before the type was stored, and entities made in code. Exact for
/// every kind with a component of its own; where two kinds are built from the same components
/// it picks one and says which in the implementation. Object3D when nothing more specific fits.
[[nodiscard]] Objects::SceneObjectKindUVE InferSceneObjectKindUVE(const IEntityManagerUVE& entityManager,
                                                            EntityUVE entity);

/// Stores `kind` as `entity`'s object type, replacing any it had.
void SetSceneObjectKindUVE(IEntityManagerUVE& entityManager, EntityUVE entity, Objects::SceneObjectKindUVE kind);

} // namespace UVE::Scene
